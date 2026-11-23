// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_lootbehavior.h"

#include "common_aisteer.h"        // FAiTarget
#include "common_archetype.h"      // Archetype::Ship::HoldSize, Archetype::Root::get_class_type
#include "common_cequip.h"         // ScanList
#include "common_cobject.h"        // CObject, CShip, CLoot, CEqObj::get_behavior_interface
#include "common_iface.h"          // IObjInspect, IObjAffect, IObjRW, IBehaviorManager, EquipmentVal
#include "common_misc.h"           // SUBOBJ_ID_NONE
#include "common_systemmanager.h"  // SystemManager -- what NavHelper points at

#include <cstring>

namespace
{
    IObjRW* WatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // `MOV EAX,[Sys+8]; ... LEA ECX,[EAX-8]` -- the owner ship, null arm included.
    IObjRW* OwnShip(SystemManager* Sys)
    {
        return WatchedObject(Sys->Watcher);
    }

    // The inlined `cobject(); (+0x4c & Mask) == Mask ? obj : 0` filter.
    CObject const* CObjectOfClass(IObjInspect const* Obj, unsigned Mask)
    {
        CObject const* const C = Obj->cobject();
        if (C != nullptr && (static_cast<unsigned>(C->ObjectClass) & Mask) == Mask)
            return C;
        return nullptr;
    }

    // The cargo-hold check retail inlines twice (062ce8a4, 062ceb9e). The own ship's CShip is
    // null when it is not one, and retail then faults on [ECX+0x88]. True when the free
    // fraction exceeds 0.2 (double .rdata:0639fa50).
    bool OwnHoldHasRoom(SystemManager* Sys)
    {
        CShip const* const Ship = static_cast<CShip const*>(CObjectOfClass(OwnShip(Sys), CObject::ClassShip));
        Archetype::Ship const* const Arch = static_cast<Archetype::Ship const*>(Ship->Archetype);
        double Fraction = 0.0;
        if (static_cast<double>(Arch->HoldSize) > 0.0)
            Fraction = static_cast<double>(Ship->get_cargo_hold_remaining()) / Arch->HoldSize;
        return Fraction > 0.2;
    }

    // The inlined 0x5f3759df distance (062cea9b, 062cec90): (dz*dz + dx*dx) + dy*dy kept on the
    // x87 stack, only its float image seeding the magic.
    double FastDistance(Vector const& A, Vector const& B)
    {
        double const Dx = static_cast<double>(A.x) - B.x;
        double const Dy = static_cast<double>(A.y) - B.y;
        double const Dz = static_cast<double>(A.z) - B.z;
        double const DistSq = (Dz * Dz + Dx * Dx) + Dy * Dy;
        float const DistSqF = static_cast<float>(DistSq);
        int Bits;
        std::memcpy(&Bits, &DistSqF, sizeof(Bits));
        int const SeedBits = 0x5f3759df - (Bits >> 1);
        float Seed;
        std::memcpy(&Seed, &SeedBits, sizeof(Seed));
        return ((1.5f - ((static_cast<double>(Seed) * Seed) * DistSq) * 0.5f) * Seed) * DistSq;
    }

    // FUN_062ecd90: `this` is the SystemManager+0x1498 tractor record {Owner1498, Watcher149C,
    // Unknown14A4 = tractor equipment id}. Points the tractor at Target through the owner
    // ship's IObjAffect::tractor_single (slot 0x40).
    bool TractorActivate(SystemManager* Sys, IObjRW* Target)
    {
        if (Target == nullptr)
            return false;
        unsigned short const* const Id = reinterpret_cast<unsigned short const*>(&Sys->Unknown14A4);
        if (*Id == SUBOBJ_ID_NONE)
            return false;
        IObjRW* const Current = WatchedObject(Sys->Watcher149C);
        if (Current != nullptr && Current->get_id() == Target->get_id())
            return false;
        Sys->Watcher149C.set(reinterpret_cast<Watchable const*>(reinterpret_cast<char*>(Target) + 8));
        IObjRW* const Owner = WatchedObject(Sys->Owner1498->Watcher);
        int const Result = static_cast<IObjAffect*>(Owner)->tractor_single(Id, WatchedObject(Sys->Watcher149C), -1);
        return Result == 0;
    }

    // FUN_062ece40: releases the tractor through IObjAffect::toggle_item (slot 0x4c).
    bool TractorRelease(SystemManager* Sys)
    {
        if (WatchedObject(Sys->Watcher149C) == nullptr)
            return false;
        unsigned short const* const Id = reinterpret_cast<unsigned short const*>(&Sys->Unknown14A4);
        if (*Id == SUBOBJ_ID_NONE)
            return false;
        Sys->Watcher149C.set(nullptr);
        IObjRW* const Owner = WatchedObject(Sys->Owner1498->Watcher);
        return static_cast<IObjAffect*>(Owner)->toggle_item(Id, false, -1) == 0;
    }
}

// ----------------------------------------------------------------------------
// FUN_062ce400.
// ----------------------------------------------------------------------------
LootBehavior::LootBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContext(nullptr)
    , ObjectDb(nullptr)
    , DeltaTime(0.0f)
    , BehaviorMgrPtr(nullptr)
    , NavHelper(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , Move()                        // FUN_062d4e60
    , Face()                        // FUN_062d6750
    , bHasTractor164(0)             // not written by retail's ctor; SetContext writes it
    , Pad165{}
    , TractorReach168(0.0f)         // not written by retail's ctor; SetContext writes it
    , Flag16c(1)
    , Pad16d{}
    , Field170(13.0f)
    , Target{}
{
    ConstructBase(InTypeId);        // FUN_062d2020
}

// FUN_062ce4c0 (scalar deleting FUN_062ce4a0).
LootBehavior::~LootBehavior()
{
}

// ----------------------------------------------------------------------------
// Slot 1, LAB_062ce550.
// ----------------------------------------------------------------------------
bool LootBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bOk = BindContext(Owner, Manager, Sys);              // FUN_062d2090
    if (bOk)
    {
        Move.SetContext(OwnerContext, NavHelper, Manager);
        Face.SetContext(OwnerContext, NavHelper, Manager);
    }

    // The rest runs whether or not the binder accepted the context.
    Target.Watcher.set(nullptr);
    Target.bDirected = 0;
    bHasTractor164 = 0;
    TractorReach168 = 0.0f;

    // Retail reuses its own two argument slots as one EquipmentVal{Value, EquipId}, the id
    // seeded with SUBOBJ_ID_NONE.
    EquipmentVal Tractor;
    Tractor.Value = 0.0f;
    Tractor.EquipId = SUBOBJ_ID_NONE;
    Tractor.Pad = 0;
    IObjRW* const Ship = OwnShip(NavHelper);
    if (Ship->find_equipment(&Tractor.EquipId, 1, 0x100000) == 0)  // tractor class
    {
        bHasTractor164 = 1;
        if (OwnShip(NavHelper)->get_equipment_val(Tractor, Tractor.EquipId,
                                                  static_cast<EquipmentValType>(5), 0.0f) == 0)
        {
            // Double 0.75 (.rdata:0639fe28), rounded once.
            TractorReach168 = static_cast<float>(static_cast<double>(Tractor.Value) * 0.75);
        }
    }
    return bOk;
}

// ----------------------------------------------------------------------------
// Slot 3, LAB_062cd5d0: jumps to Behavior's slot 3.
// ----------------------------------------------------------------------------
void LootBehavior::OnEngage()
{
    Behavior::OnEngage();
}

// ----------------------------------------------------------------------------
// Slot 4, FUN_062ce660.
// ----------------------------------------------------------------------------
void LootBehavior::OnDisengage()
{
    Target.Watcher.set(nullptr);
    Target.bDirected = 0;
    Move.Commit();
    Face.Commit();
    (void)TractorRelease(NavHelper);
    Behavior::OnDisengage();
}

// ----------------------------------------------------------------------------
// Slot 5, FUN_062ce6b0.
// ----------------------------------------------------------------------------
int LootBehavior::OnEvaluateReadiness(float Unused)
{
    (void)Unused;
    // Only with a tractor and the TargetManager in attack state 2.
    if (bHasTractor164 != 0 && NavHelper->Targeting.AttackState == 2)
    {
        if (WatchedObject(Target.Watcher) == nullptr)
            (void)AcquireLootTarget(&Target);
        if (WatchedObject(Target.Watcher) != nullptr)
        {
            ModeSelector0xc = Flag29 ? 3 : 0;
            return ModeSelector0xc;
        }
    }
    ModeSelector0xc = Flag29 ? 3 : 2;
    return ModeSelector0xc;
}

// ----------------------------------------------------------------------------
// FUN_062ce740: false when another ship is currently looting this same loot.
// ----------------------------------------------------------------------------
bool LootBehavior::IsLootUnclaimed(IObjRW* Loot)
{
    // The CLoot is null when the object is not loot, and retail then reads [0+0xf0]. More
    // than one unit is never contested.
    CLoot const* const LootObj = static_cast<CLoot const*>(CObjectOfClass(Loot, CObject::ClassLoot));
    if (LootObj->Units > 1)
        return true;

    // Every ship-class object in the owner's scan list (FUN_062e74e0).
    ScanList const* List = nullptr;
    (void)NavHelper->Timers.GetOwnScanList(List, 0x7f0000);
    if (List == nullptr)
        return false;

    for (int Index = 0; static_cast<unsigned>(Index) < static_cast<unsigned>(List->Count); ++Index)   // unsigned compare
    {
        IObjRW* const Other = WatchedObject(List->Entries[Index]);
        if (Other == OwnShip(NavHelper))
            continue;
        unsigned OtherType = 0;                                     // not pre-set in retail
        Other->get_type(OtherType);                                 // result unread
        if ((OtherType & 0x7f0000u) == 0)
            continue;
        CObject const* const OtherShip = CObjectOfClass(Other, CObject::ClassShip);
        if (OtherShip == nullptr)
            continue;
        IBehaviorManager* const Manager =
            const_cast<CEqObj*>(static_cast<CEqObj const*>(OtherShip))->get_behavior_interface();
        if (Manager == nullptr)
            continue;
        if (Manager->CurrentBehaviorIndex != 0x10)                  // currently looting
            continue;
        // Its LootBehavior (dispatch slot 16, +0x1b4) chases this loot.
        LootBehavior const* const OtherLoot = static_cast<LootBehavior const*>(Manager->BehaviorDispatchTable[16]);
        if (WatchedObject(OtherLoot->Target.Watcher) == Loot)
            return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// FUN_062ce870: pick the nearest unclaimed loot the personality wants.
// ----------------------------------------------------------------------------
bool LootBehavior::AcquireLootTarget(FLootTarget* Slot)
{
    SystemManager* const Sys = NavHelper;
    pub::AI::Personality::JobStruct const& Job = Sys->PersonalityData.JobParams;
    if (Job.LootPreference == 0)                                    // Sys+0x18fc
        return false;
    if (!OwnHoldHasRoom(Sys))
        return false;

    Vector const& OwnPos = Sys->SelfTarget.Iface->get_transform().Translation;

    IObjRW* Best = nullptr;
    float BestDistance = 3.40282347e+38f;                           // 0x7f7fffff
    ScanList const* List = nullptr;
    (void)Sys->Timers.GetOwnScanList(List, 0x10000000);   // loot class
    if (List == nullptr)
        return false;

    for (int Index = 0; static_cast<unsigned>(Index) < static_cast<unsigned>(List->Count); ++Index)
    {
        IObjRW* const Candidate = WatchedObject(List->Entries[Index]);
        unsigned CandidateType = 0;                                 // not pre-set in retail
        Candidate->get_type(CandidateType);
        if ((CandidateType & 0x10000000u) == 0)
            continue;

        CLoot const* const Loot = static_cast<CLoot const*>(CObjectOfClass(Candidate, CObject::ClassLoot));
        if (Loot->bCanAiTractor == 0)                               // +0xfc
            break;

        int const Kind = static_cast<int>(Loot->ContentsArch->get_class_type());
        if (Kind == 0xa)
        {
            // Commodities need loot flag bit 0 and, when the personality lists goods
            // (+0x1904 count, +0x1908 ids), the candidate's good among them.
            if ((Job.LootPreference & 1) == 0)
                break;
            int const ListCount = Job.ScanForCargoCount;
            if (ListCount > 0)
            {
                unsigned GoodId = 0;
                if (Candidate->get_good_id(GoodId) == 0)
                {
                    int GoodIndex = 0;
                    while (Job.ScanForCargo[GoodIndex] != GoodId)
                    {
                        ++GoodIndex;
                        if (GoodIndex >= ListCount)
                            goto Done;
                    }
                }
            }
        }
        else if (Kind == 0x1a || Kind == 0x1b)
        {
            if ((Job.LootPreference & 4) == 0)
                break;
        }

        if (Loot->Units == 0)                                       // +0xf0
            break;
        if (!IsLootUnclaimed(Candidate))
            continue;

        // The nearest wins; a tie keeps the earlier one.
        Vector const& LootPos = Candidate->get_transform().Translation;
        double const Distance = FastDistance(LootPos, OwnPos);
        if (Distance < BestDistance)
        {
            BestDistance = static_cast<float>(Distance);
            Best = Candidate;
        }
    }

Done:
    // The slot watches the winner (or nothing), undirected.
    Slot->Watcher.set(Best != nullptr ? reinterpret_cast<Watchable const*>(reinterpret_cast<char*>(Best) + 8)
                                      : nullptr);
    Slot->bDirected = 0;
    return Best != nullptr;
}

// ----------------------------------------------------------------------------
// Slot 7, LAB_062ceb50.
// ----------------------------------------------------------------------------
int LootBehavior::OnUpdate()
{
    if (OwnerContext == nullptr)
    {
        UpdateResult10 = 3;
        return 3;
    }
    IObjRW* const Loot = WatchedObject(Target.Watcher);
    if (Loot == nullptr)
    {
        UpdateResult10 = 2;
        return 2;
    }

    SystemManager* const Sys = NavHelper;
    if (!OwnHoldHasRoom(Sys))
    {
        Target.Watcher.set(nullptr);
        Target.bDirected = 0;
        UpdateResult10 = 2;
        return 2;
    }

    // The FAiTarget type refresh (guard set after the call).
    FAiTarget* const Nav = &Sys->SelfTarget;
    if (Nav->bFlagsValid == 0)
    {
        Nav->Iface->get_type(Nav->CachedFlags);
        Nav->bFlagsValid = 1;
    }
    // `(x & 0x10000) | 0x20000` is never zero, so the block below (a distance gate on
    // TractorReach168, then Move.Commit and FUN_062ecd90, or the release exit) is unreachable
    // in retail; it is kept behind the same constant test.
    if ((((Nav->CachedFlags & 0x10000u) | 0x20000u)) == 0)
    {
        IObjRW* const Ship = OwnShip(Sys);
        double const Distance = FastDistance(Ship->get_position(), Loot->get_position());
        if (Distance < TractorReach168)
        {
            Move.Commit();
            if (!TractorActivate(Sys, Loot))
            {
                UpdateResult10 = 1;
                return 1;
            }
        }
        Target.Watcher.set(nullptr);
        Target.bDirected = 0;
        UpdateResult10 = 2;
        return 2;
    }

    // Fly within tractor reach, then face the loot and tractor it in.
    Move.SetWatchObject(Loot);                                      // FUN_062d5760
    Move.SetEnabled(true);                                          // FUN_062d5a10
    Move.SetRange3c(TractorReach168);                               // ICF fold 062a66b0
    if (Move.Update(DeltaTime) == 1)
    {
        Move.Commit();
        Face.SetWatchObject(Loot);                                  // FUN_062d7510
        if (Face.Update(DeltaTime) == 1 && TractorActivate(Sys, Loot))
            return 2;                                               // UpdateResult10 not written
    }
    else
    {
        Face.SetWatchObject(Loot);
        Face.Update(DeltaTime);
    }
    UpdateResult10 = 1;
    return 1;
}

// ----------------------------------------------------------------------------
// Slot 10, FUN_062cedd0.
// ----------------------------------------------------------------------------
bool LootBehavior::OnApplyDirective(void* DirectiveOp)
{
    unsigned char* const Op = static_cast<unsigned char*>(DirectiveOp);
    void* const Found = NavHelper->Timers.FindVerifiedObject((unsigned const*)(Op + 0xc));   // FUN_062e74a0
    Target.Watcher.set(Found != nullptr ? reinterpret_cast<Watchable const*>(static_cast<char*>(Found) + 8)
                                        : nullptr);
    if (WatchedObject(Target.Watcher) != nullptr)
    {
        Target.bDirected = 1;
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        return true;
    }
    Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
    Flag16c = Op[0x10];
    return false;
}
