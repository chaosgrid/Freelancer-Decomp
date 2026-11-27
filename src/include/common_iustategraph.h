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

// ----------------------------------------------------------------------------
// The three real methods on FAiTargetList
// ----------------------------------------------------------------------------

