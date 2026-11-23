// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_aisteer.h"        // FAiTarget (the 0x88-byte AI-target cache record)
#include "common_iface.h"          // IObjInspect
#include "common_systemmanager.h"  // SystemManagerTimerTable / FAiTargetVector -- this
                                   // cluster's container IS that struct (see below).
                                   // common_systemmanager.h stays independent of THIS
                                   // header, so the dependency runs one way only.

#include <cstddef>

// ============================================================================
// The AI "hostile candidate" target list -- address range 0x062e7580-0x062e8400
// plus its vector mechanics at 0x062ed0d0-0x062ed460.
//
// (The file name is historical; there is no IUStateGraph class here.)
//
// The owner is always SystemManager+0x34 (SystemManagerTimerTable): all 8 call sites of
// FUN_062e7580 reach it as `<SystemManager> + 0x34`, e.g. LargeShipBehavior
// `MOV ECX,[EBX+0x24]; ADD ECX,0x34` (.text:062d01be), and the export
// IBehaviorManager::get_closest_trailing_ship (0x062d4a30) is
// `MOV ECX,[ECX+8]; ADD ECX,0x34; JMP FUN_062e7820`.
//
// The list is an MSVC-7 `std::vector<FAiTarget>` at the table's +0x0C (built empty by the
// table ctor FUN_062e6af0). FUN_062e7580 returns a pointer to that vector (`LEA EAX,[ESI+0xc]`
// at 0x062e77cb); callers read `[EAX+4]` / `[EAX+8]` as begin / end.
//
// Ghidra's local_9c in FUN_062e7580 is get_scanlist's out-parameter (`LEA EAX,[ESP+0x18];
// PUSH EAX; CALL [EDX+0x104]` at 0x062e766e), so `[ECX+0x800]` is ScanList::Count.
// ============================================================================

// sizeof(FAiTarget) == 0x88. Kept as a named constant because retail's own
// pointer arithmetic is written in raw bytes throughout this cluster.
inline constexpr std::size_t kElemStride = 0x88;

// ----------------------------------------------------------------------------
// The object at SystemManager+0x34; its fields (Owner, ScannerEquipId, Targets, bBuilt,
// bKeepCache) are declared on SystemManagerTimerTable in common_systemmanager.h.
// ----------------------------------------------------------------------------
using FAiTargetList = SystemManagerTimerTable;

// The SystemManager+0x34 accessor every call site spells out by hand.
FAiTargetList* GetAiTargetList(SystemManager* Sys);

// ----------------------------------------------------------------------------
// Vector mechanics
// ----------------------------------------------------------------------------

// FUN_062ed0d0, thiscall(this=the vector).
// `(End - Begin) / 0x88`, i.e. size(), with retail's null-Begin early-out.
// The IMUL-by-0x78787879/SAR-6/sign-correct sequence is the standard MSVC
// signed-divide-by-0x88 reciprocal idiom.
unsigned ElemVectorSize(void const* VectorHeader);

// FUN_062ed100, __stdcall, 3 stack args
// (RET 0xc). Element-wise `rep movsd` copy of [SrcBegin, SrcEnd) into
// DestBegin at the 0x88 stride.
void CopyElemRange(void const* SrcBegin, void const* SrcEnd, void* DestBegin);

// The destroy-a-range helper at 0x0628f050 (ICF-labelled `JustifyNode::Execute`).
// thiscall(this=the vector) plus (First, Last); an empty loop for FAiTarget. Kept so both
// call sites (the inlined erase(begin(),end()) and the post-reallocation cleanup) stay
// visible.
void DestroyAiTargetRange(FAiTargetVector* Self, FAiTarget* First, FAiTarget* Last);

// The inlined `erase(begin(), end())` at the head of FUN_062e7580
// (0x062e7597-0x062e75d0): a `rep movsd` shift loop whose source is seeded
// from End instead of Begin and therefore never runs, then
// DestroyAiTargetRange, then `End = Begin`.
void ClearAiTargetVector(FAiTargetVector* Self);

// FUN_062e77e0, __thiscall(this = the list), no
// arguments. `bBuilt = 0; Targets.End = Targets.Begin` -- the same inlined, never-iterating
// erase(begin(), end()) shape, but without ClearAiTargetVector's destroy-range call. Sole
// caller TargetManager::GetScanResponse (FUN_062eeb70, .text:062eeb89).
void ResetAiTargetList(FAiTargetList* Self);

// FUN_062ed140, __thiscall(this=the
// vector) + 3 stack args (RET 0xc): MSVC 7.0's
// `std::vector<FAiTarget>::insert(iterator Where, size_type Count, const
// FAiTarget& Value)` / `_Insert_n`, all three branches. Growth, when needed,
// is `NewCapacity = size() + max(size(), Count)`, allocated with a raw
// `operator new` and released with `operator delete` (0x06391d5a) -- no
// element destructors run because the element type has none. The only live
// call site (FUN_062e7580) always uses Where == End and Count == 1, i.e.
// push_back; the other two branches are ported too.
void InsertAiTargets(FAiTargetVector* Self, FAiTarget* Where, unsigned Count, FAiTarget const& Value);

// ----------------------------------------------------------------------------
// The three real methods on FAiTargetList
// ----------------------------------------------------------------------------

// FUN_062e7580 (194 instructions), __thiscall, no stack args, returns a pointer to
// Self->Targets.
//
// Rebuilds the list of nearby objects the owner's scanner can see, unless a
// cached list is still considered good:
//   1. If bKeepCache is clear, ClearAiTargetVector first (0x062e7595; its shift loop is
//      seeded from End, `MOV EAX,EDX` at 0x062e75a0, so it never runs).
//   2. If bBuilt AND bKeepCache are both set, return the cached vector
//      untouched (0x062e75d3-0x062e75df).
//   3. Set bBuilt, then recover the owner's own IObjRW from
//      SystemManager::Watcher (+0x08) by the "-8 Watchable adjustment": the
//      watcher stores the target's Watchable sub-object, which IObjRW places
//      at its own +0x08 (IObjRW : IObjInspect, IObjAffect, Watchable), so
//      subtracting 8 recovers the object. Bail out returning the vector
//      as-is if nothing is watched.
//   4. IObjInspect::get_equipment_status(Status, ScannerEquipId) -- bail if
//      Status.Flags has EquipStatusFlagDestroyed (0x02).
//   5. IObjInspect::get_scanlist(List, 0x7FFDFF, bKeepCache) -- bail unless
//      it returns 0. The 0x7FFDFF mask is the object-type set to scan for.
//   6. Reserve exactly List->Count elements if the CURRENT CAPACITY (not the
//      size) is smaller, by hand: operator new, CopyElemRange the live range
//      into the new buffer, DestroyAiTargetRange the old range, operator
//      delete the old buffer, then Cap/End/Begin.
//   7. For each of List->Count entries, take the watched Watchable, apply the
//      same -8 adjustment, build a fresh FAiTarget whose AttitudeRefIface is
//      the OWNER's object and whose Iface is the candidate, prime
//      CachedFlags via get_type, and append it if
//      `(CachedFlags & 0x7FFDFF) != 0`.
FAiTargetVector* BuildAiTargetList(FAiTargetList* Self);

// FUN_062e7820 (163 instructions), __thiscall, no stack args, returns float: the body of
// the exported IBehaviorManager::get_closest_trailing_ship.
//
// Walks BuildAiTargetList's result and returns the distance to the nearest
// candidate that passes every gate, or FLT_MAX when none does:
//   * CachedFlags & 0x7F0000 (a ship-ish object type)
//   * !is_player()                                        (slot +0x0BC)
//   * get_target(Other) yields a non-null target          (slot +0x074)
//   * that target's get_id() equals the owner's get_id()  (slot +0x020)
//     -- i.e. the candidate is currently targeting US
//   * IsTargetCoAlignedAndAhead(Candidate, OwnerElem, 0.0f)
//   * CachedAttitude <= -0.2f (.rdata:063a0794) -- hostile enough
// Distance uses retail's FastInvSqrt idiom (`FastInvSqrt(d2) * d2`), not a
// real sqrt, and both endpoints come from the cached-position idiom. The
// "owner element" is `SystemManager + 0x14FC`, the single embedded FAiTarget
// the Intercept-clearance cluster already documents.
float GetClosestTrailingShipDistance(FAiTargetList* Self);

// FUN_062e8280 (129 instructions), __thiscall + one float stack arg (RET 0x4), returns
// IObjInspect* or null.
//
// Returns the interface of the physically nearest candidate that is hostile
// and (optionally) close enough, or null:
//   * CachedFlags & 0x7F0000
//   * CachedAttitude <= -0.6f (.rdata:063a0790)
//   * when MaxClearance >= 0.0f, also
//     GetInterceptClearance(OwnerElem, Candidate) < MaxClearance. A negative
//     MaxClearance skips this gate entirely (retail's own
//     `FCOMP 0.0f; JNP` at 0x062e835b), which is how callers ask for
//     "nearest hostile at any range".
//   * among the survivors, minimum Distance3(OwnerPosition, CachedPos),
//     seeded at 100000.0f (.rdata:0x47C35000) -- so a candidate farther than
//     100km is never returned even if it is the only one.
// OwnerPosition comes from GetIfacePositionOrZero on the owner's
// own object, resolved through the same -8 Watchable adjustment.
IObjInspect* FindClosestHostileTarget(FAiTargetList* Self, float MaxClearance);
