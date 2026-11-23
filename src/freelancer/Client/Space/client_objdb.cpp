// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/client_objdb.h"

#include "Space/client_gameobj.h"
#include "Core/client_tick_profile.h" // Port-only owner-requested collision profiling
#include "common_archetype.h"
#include "common_cequip.h"    // CEShield (FUN_0052fe60)
#include "common_csys.h"      // Acquire3DMathEngine (FUN_005b73e0)
#include "common_misc.h"      // ICamera, Geometry::Sphere
#include "common_physys.h"    // GenerateCollisions / GetCollisions (FUN_0052fc20)
#include "common_server.h"
#include "common_universe.h"
#include "Common/Audio/menu_sound_arch.h"  // GetClientSoundListenerObject (FUN_0040f130)
#include "Space/player_spawn.h"     // GetPlayerConnectionId (DAT_00673344)
#include "Network/server_connect.h"   // GetLocalServerInterface (DAT_0067ecd0)
#include "Space/star_sys.h"
#include "x86math.h"          // I3DMathEngine::MatrixToQuaternion

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <new>
#include <vector>
#include <windows.h>

// ============================================================================
// Client IObjDB — retail DAT_00678a2c (PTR_DAT_00613ed0), vtable .rdata:005df8ac.
//
// Type-bucket heads (FUN_00540fc0 First pointers → Count at First-4):
//   kind 1 ships  DAT_00678aa8   mask walk 0x7f0000
//   kind 2 solars DAT_00678ab4   mask walk 0xfdff
//   kind 3..10    DAT_00678a90 … (see FUN_00540fc0)
// Link/unlink: FUN_00542350 / FUN_00542390. List advance: FUN_00542330.
// GetBucket (+0x0c): DAT_00678ae0 — Common ISpatialPartition, freed by FUN_00540e90 and
// (re)built with NewSpatial("BSP") by FUN_00540eb0 around every system INI load.
// GetZoneDb (+0x08): DAT_00679954 = Universe::ISystem* (EnterStarSystem).
// ============================================================================

namespace
{
    struct FGameobjList
    {
        int Count;
        FClientGameobj* First;
        FClientGameobj* Last;
    };

    struct FObjDbIterState
    {
        FClientGameobj* Cursor;
        FGameobjList* ListHead;
        unsigned Mask;
    };

    struct FIObjDB : public IObjDB
    {
        // Lists[1..10] are the ManagerKind buckets (FUN_00540fc0). Lists[0] is DAT_00678a54,
        // the eleventh list: nothing links into it, but its static initialiser (LAB_00540b00),
        // FUN_00541210, FUN_00541380 and FUN_00541510 all walk it, so it is kept.
        FGameobjList Lists[11];
        ISpatialPartition* Spatial; // DAT_00678ae0

        IObjRW* __thiscall GetNext(void* IterStateRaw) override;
        IObjRW* __thiscall FindById(unsigned const* ObjectId) override;
        Universe::ISystem* __thiscall GetZoneDb() override;
        void* __thiscall GetBucket(unsigned TypeMask) override;
    };

    unsigned TotalCount(FIObjDB const* Db)
    {
        unsigned Sum = 0;
        for (int Kind = 1; Kind <= 10; ++Kind)
            Sum += static_cast<unsigned>(Db->Lists[Kind].Count);
        return Sum;
    }

    // FUN_00542350: append at the tail. A null Gameobj is a no-op.
    void LinkGameobj(FGameobjList* List, FClientGameobj* Go)
    {
        if (Go == nullptr)
            return;

        Go->ListPrev = List->Last;
        if (List->Last != nullptr)
        {
            List->Last->ListNext = Go;
            List->Last = Go;
        }
        else
        {
            List->First = Go;
            List->Last = Go;
        }
        Go->ListNext = nullptr;
        ++List->Count;
    }

    // FUN_00542390. The three shapes are retail's own branches. A null Gameobj skips the unlink
    // but still decrements the count; no caller passes one.
    void UnlinkGameobj(FGameobjList* List, FClientGameobj* Go)
    {
        if (Go != nullptr)
        {
            FClientGameobj* const Prev = Go->ListPrev;
            FClientGameobj* const Next = Go->ListNext;
            if (Prev == nullptr)
            {
                List->First = Next;
                if (Next != nullptr)
                    Next->ListPrev = nullptr;
                else
                    List->Last = nullptr;
            }
            else if (Next == nullptr)
            {
                List->Last = Prev;
                Prev->ListNext = nullptr;
            }
            else
            {
                Prev->ListNext = Next;
                Next->ListPrev = Prev;
            }
            Go->ListPrev = nullptr;
            Go->ListNext = nullptr;
        }
        --List->Count;
    }

    // FUN_00542330: a null cursor starts at First, otherwise the cursor's ListNext. No null
    // tests.
    FClientGameobj* AdvanceListCursor(FGameobjList* List, FClientGameobj** CursorInOut)
    {
        if (*CursorInOut == nullptr)
        {
            *CursorInOut = List->First;
            return List->First;
        }
        *CursorInOut = (*CursorInOut)->ListNext;
        return *CursorInOut;
    }

    // One arm of LAB_00541d60, and the whole of FUN_005420a0 (its out-of-line copy, used for the
    // last three arms; the ECX it is handed is never read). A walk already parked in another
    // list yields nothing for this arm. Otherwise the saved cursor advances until a gameobj
    // whose CSimple has a TypeBits bit in Mask; the narrowed pointer is dereferenced without a
    // test. The list's end clears the iterator and yields nothing.
    FClientGameobj* WalkListArm(FObjDbIterState* Iter, FGameobjList* List, unsigned Mask)
    {
        if (Iter->ListHead != nullptr && Iter->ListHead != List)
            return nullptr;

        FClientGameobj* CursorScratch = Iter->Cursor;
        for (;;)
        {
            FClientGameobj* const Go = AdvanceListCursor(List, &CursorScratch);
            if (Go == nullptr)
            {
                Iter->Cursor = nullptr;
                Iter->ListHead = nullptr;
                return nullptr;
            }
            CObject* const Obj = const_cast<CObject*>(Go->cobject());
            CSimple* const Simple = (Obj != nullptr && (Obj->ObjectClass & 3u) == 3u)
                                        ? static_cast<CSimple*>(Obj)
                                        : nullptr;
            if ((Simple->TypeBits & Mask) == 0u)
                continue;
            Iter->Cursor = CursorScratch;
            Iter->ListHead = List;
            return Go;
        }
    }

    IObjRW* __thiscall FIObjDB::GetNext(void* IterStateRaw)
    {
        // LAB_00541d60 (IObjDB slot 0). The mask is re-read before every arm, and an arm runs
        // only while nothing has been found. The first six arms are inline, the last three go
        // through FUN_005420a0. No null test on the iterator.
        FObjDbIterState* const Iter = static_cast<FObjDbIterState*>(IterStateRaw);
        FClientGameobj* Hit = nullptr;

        if ((Iter->Mask & 0x7f0000u) != 0u)
            Hit = WalkListArm(Iter, &Lists[1], Iter->Mask);
        if ((Iter->Mask & 0xfdffu) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[2], Iter->Mask);
        if ((Iter->Mask & 0x10000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[7], Iter->Mask);
        if ((Iter->Mask & 0x20000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[8], Iter->Mask);
        if ((Iter->Mask & 0x40000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[9], Iter->Mask);
        if ((Iter->Mask & 0x1000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[4], Iter->Mask);
        if ((Iter->Mask & 0x4000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[5], Iter->Mask);
        if ((Iter->Mask & 0x8000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[6], Iter->Mask);
        if ((Iter->Mask & 0x2000000u) != 0u && Hit == nullptr)
            Hit = WalkListArm(Iter, &Lists[3], Iter->Mask);

        return Hit;
    }

    IObjRW* __thiscall FIObjDB::FindById(unsigned const* ObjectId)
    {
        // LAB_00542130: FUN_005416c0(*ObjectId, 0) inlined. The id is dereferenced
        // unconditionally.
        return ClientObjDb_FindObject(*ObjectId, 0);
    }

    Universe::ISystem* __thiscall FIObjDB::GetZoneDb()
    {
        // LAB_005422c0 → FUN_005556a0: return DAT_00679954 (owned by star_sys.cpp).
        return const_cast<Universe::ISystem*>(StarSysGetSystem());
    }

    void* __thiscall FIObjDB::GetBucket(unsigned TypeMask)
    {
        // LAB_005422d0: (TypeMask & 0xfdff) ? DAT_00678ae0 : nullptr. The partition is only
        // created by FUN_00540eb0 (ClientObjDb_RebuildSpatial) at the end of a system load.
        if ((TypeMask & 0xfdffu) == 0u)
            return nullptr;
        return Spatial;
    }

    // DAT_00678ae4 -- incremented by FUN_00540eb0 on every non-empty rebuild; retail never
    // reads it.
    unsigned G_ClientSpatialRebuildCount = 0;

    // DAT_00613ec4: a solar whose radius is above it is a "large" solar.
    constexpr float kLargeSolarRadiusThreshold = 200.0f;
    // DAT_00613ecc: the radius of FUN_00541380's update sphere around the listener camera.
    constexpr float kSpatialUpdateRadius = 10000.0f;

    // Large solars -- DAT_00678ac0. A kind-2 Gameobj goes in when its radius is above 200 and
    // bit 7 of its get_type is clear. FUN_00541380 updates them every frame no matter where the
    // camera is, and skips them in its sphere query; FUN_00541040 erases them.
    std::vector<FClientGameobj*> G_LargeSolars;

    // The 0x2f8-byte block FUN_00540c30 allocates for a remote-physics Gameobj. FUN_00541200
    // is its destructor (the simulation's); FUN_00541040 frees it.
    struct FRemotePhysBlock
    {
        // FUN_00541580 adds Dt to it as a double every multiplayer frame and sends it as
        // SSPObjUpdateInfo::Timestamp.
        double ElapsedTime;                    // +0x00
        CShip* Ship;                           // +0x08  FUN_00541040 matches blocks on it
        unsigned Field_0x0c;                   // +0x0c  never written by FUN_00540c30
        CRemotePhysicsSimulation Simulation;   // +0x10
    };

    // DAT_00678a30: a VC6 std::list<block*> (static initialiser LAB_00540b30). FUN_00541040
    // erases the first node whose block's Ship matches.
    std::list<FRemotePhysBlock*> G_RemotePhysBlocks;

    // The CObject a Gameobj wraps, narrowed the way FUN_00540c30 / FUN_00541040 do it. The null
    // tests on the Gameobj pointer itself in retail are only the compiler's this-adjustment;
    // both functions dereference the Gameobj unconditionally afterwards.
    CSimple* NarrowSimple(FClientGameobj* Go)
    {
        CObject* const Obj = Go->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 3u) != 3u)
            return nullptr;
        return static_cast<CSimple*>(Obj);
    }

    CShip* NarrowShip(FClientGameobj* Go)
    {
        CObject* const Obj = Go->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) != 0x503u)
            return nullptr;
        return static_cast<CShip*>(Obj);
    }

    // Retail builds each of the eleven lists with FUN_00542320 from its own XC entry and leaves
    // DAT_00678ae0 zero in .bss; the value-initialized static object starts in exactly that
    // state.
    static FIObjDB G_ClientObjDb = {};

    // The list FUN_00540c30 / FUN_00541040 switch ManagerKind into: 1 DAT_00678aa8, 2 ab4,
    // 3 a90, 4 a9c, 5 a84, 6 a78, 7 a60, 8 a48, 9 a3c, 10 a6c.
    FGameobjList* KindList(int Kind)
    {
        if (static_cast<unsigned>(Kind - 1) > 9u)
            return nullptr;
        return &G_ClientObjDb.Lists[Kind];
    }

    // FUN_005424b0: the next link is read after the update.
    void UpdateList(FGameobjList* List, float Dt)
    {
        FClientGameobj* Go = List->First;
        while (Go != nullptr)
        {
            Go->update(Dt);
            Go = Go->ListNext;
        }
    }

    // FUN_00542410: unlink (and, with bDelete, delete) every Gameobj that is not in Keep (a null
    // Keep keeps nothing). Unlike FUN_005424e0 it does not go through FUN_00541040: the
    // remote-physics blocks and the large-solar vector are cleared by the caller.
    void ClearListExcept(FGameobjList* List, std::list<FClientGameobj*> const* Keep, bool bDelete)
    {
        FClientGameobj* Go = List->First;
        while (Go != nullptr)
        {
            FClientGameobj* const Next = Go->ListNext;
            bool bKeep = false;
            if (Keep != nullptr)
            {
                std::list<FClientGameobj*>::const_iterator It = Keep->begin();
                for (; It != Keep->end(); ++It)
                {
                    if (*It == Go)
                    {
                        bKeep = true;
                        break;
                    }
                }
            }
            if (!bKeep)
            {
                UnlinkGameobj(List, Go);
                if (bDelete)
                    delete Go;
            }
            Go = Next;
        }
    }

    // FUN_005424e0: the next link is read first.
    void ReapList(FGameobjList* List)
    {
        FClientGameobj* Go = List->First;
        while (Go != nullptr)
        {
            FClientGameobj* const Next = Go->ListNext;
            if (Go->IsDead())
            {
                ClientObjDb_Unregister(Go);
                delete Go;
            }
            Go = Next;
        }
    }
}

IObjDB* GetClientObjDb()
{
    return &G_ClientObjDb;
}

void ClientObjDb_RegisterGameobj(FClientGameobj* Go, bool bRemotePhysics)
{
    // FUN_00540c30
    CSimple* const Simple = NarrowSimple(Go);

    int const Kind = Go->ManagerKind;
    if (Kind == 2)
    {
        // get_type's result is not tested.
        if (Go->cobject()->Radius > kLargeSolarRadiusThreshold)
        {
            uint Type = 0;
            Go->get_type(Type);
            if ((Type & 0x80u) == 0u)
                G_LargeSolars.push_back(Go);
        }

        LinkGameobj(&G_ClientObjDb.Lists[2], Go);

        // Nothing is created here: during a system load the partition was freed by
        // FUN_00540e90 and FUN_00540eb0 bulk-adds every solar afterwards. Simple is not
        // null-tested.
        if (G_ClientObjDb.Spatial != nullptr && (Simple->TypeBits & 0xfdffu) != 0u)
        {
            FSpatialEntry Entry;
            Entry.Object = static_cast<IObjRW*>(Go);
            Entry.Position = Simple->WorldPosition;
            Entry.Radius = Simple->Radius;
            SpatialAddObject(G_ClientObjDb.Spatial, &Entry);
        }
    }
    else
    {
        FGameobjList* const List = KindList(Kind);
        if (List != nullptr)
            LinkGameobj(List, Go);
    }

    if (bRemotePhysics)
    {
        FRemotePhysBlock* const Block = new FRemotePhysBlock;
        Block->ElapsedTime = 0.0;
        Block->Ship = NarrowShip(Go);
        G_RemotePhysBlocks.push_back(Block);
    }
}

void ClientObjDb_Unregister(FClientGameobj* Go)
{
    // FUN_00541040. It only unlinks: both callers delete the Gameobj themselves afterwards.

    // A non-ship (null CShip) matches a block whose Ship is null.
    CShip* const Ship = NarrowShip(Go);
    std::list<FRemotePhysBlock*>::iterator It = G_RemotePhysBlocks.begin();
    for (; It != G_RemotePhysBlocks.end(); ++It)
    {
        if ((*It)->Ship == Ship)
        {
            delete *It;
            G_RemotePhysBlocks.erase(It);
            break;
        }
    }

    int const Kind = Go->ManagerKind;
    if (Kind == 2)
    {
        UnlinkGameobj(&G_ClientObjDb.Lists[2], Go);
        std::vector<FClientGameobj*>::iterator Large = G_LargeSolars.begin();
        for (; Large != G_LargeSolars.end(); ++Large)
        {
            if (*Large == Go)
            {
                G_LargeSolars.erase(Large);
                break;
            }
        }

        // Retail does not test the partition (DAT_00678ae0) for null here.
        uint Type = 0;
        if (Go->get_type(Type) == 0 && (Type & 0xfdffu) != 0u)
        {
            G_ClientObjDb.Spatial->Remove(static_cast<IObjRW*>(Go));
        }
        return;
    }

    FGameobjList* const List = KindList(Kind);
    if (List != nullptr)
        UnlinkGameobj(List, Go);
}

void ClientObjDb_UpdateAll(float Dt)
{
    // FUN_00541380 (called from the space frame).
    UpdateList(&G_ClientObjDb.Lists[1], Dt);
    UpdateList(&G_ClientObjDb.Lists[3], Dt);
    UpdateList(&G_ClientObjDb.Lists[4], Dt);
    UpdateList(&G_ClientObjDb.Lists[5], Dt);
    UpdateList(&G_ClientObjDb.Lists[6], Dt);

    // Solars are only updated while the partition exists.
    ISpatialPartition* const Spatial = G_ClientObjDb.Spatial;
    if (Spatial != nullptr)
    {
        // A sphere around the listener camera (FUN_0040f130).
        ICamera* const Camera = static_cast<ICamera*>(GetClientSoundListenerObject());
        float const* const CameraPos = Camera->GetPosition();
        Geometry::Sphere Query;
        Query.Center.x = CameraPos[0];
        Query.Center.y = CameraPos[1];
        Query.Center.z = CameraPos[2];
        Query.Radius = kSpatialUpdateRadius;
        // The result lives on the stack, as in retail's 0x201c-byte frame.
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        SpatialFill(Spatial, &Query, &Result);

        for (unsigned Index = 0; Index < Result.Count; ++Index)
        {
            IObjRW* const Item = static_cast<IObjRW*>(Result.Items[Index]);
            // A large solar is left to the vector loop below.
            if (Item->cobject()->Radius > kLargeSolarRadiusThreshold)
            {
                uint Type = 0;
                Item->get_type(Type);
                if ((Type & 0x80u) == 0u)
                    continue;
            }
            // Retail's __RTDynamicCast IObjRW -> GameObject.
            FClientGameobj* const Go = dynamic_cast<FClientGameobj*>(Item);
            if (Go != nullptr)
                Go->update(Dt);
        }

        // The end is re-read after each call.
        for (size_t Index = 0; Index < G_LargeSolars.size(); ++Index)
            G_LargeSolars[Index]->update(Dt);
    }

    UpdateList(&G_ClientObjDb.Lists[10], Dt);
    UpdateList(&G_ClientObjDb.Lists[0], Dt);
    UpdateList(&G_ClientObjDb.Lists[7], Dt);
    UpdateList(&G_ClientObjDb.Lists[8], Dt);
    UpdateList(&G_ClientObjDb.Lists[9], Dt);
}

void ClientObjDb_Reap()
{
    // FUN_00541510: every list, in this order.
    ReapList(&G_ClientObjDb.Lists[1]);
    ReapList(&G_ClientObjDb.Lists[3]);
    ReapList(&G_ClientObjDb.Lists[4]);
    ReapList(&G_ClientObjDb.Lists[5]);
    ReapList(&G_ClientObjDb.Lists[6]);
    ReapList(&G_ClientObjDb.Lists[2]);
    ReapList(&G_ClientObjDb.Lists[10]);
    ReapList(&G_ClientObjDb.Lists[0]);
    ReapList(&G_ClientObjDb.Lists[7]);
    ReapList(&G_ClientObjDb.Lists[8]);
    ReapList(&G_ClientObjDb.Lists[9]);
}

FClientGameobj* ClientObjDb_FindByCObject(CObject const* Obj)
{
    // FUN_005419a0. ObjectClass picks the bucket (retail: a byte table for classes 1..0xa7,
    // compares above it). Any other class is no Gameobj.
    int Kind = 0;
    switch (static_cast<int>(Obj->ObjectClass))
    {
    case CObject::ClassShip: Kind = 1; break;
    case CObject::ClassSolar: Kind = 2; break;
    case CObject::ClassBeam: Kind = 3; break;
    case CObject::ClassGuided: Kind = 4; break;
    case CObject::ClassMine: Kind = 5; break;
    case CObject::ClassCounterMeasure: Kind = 6; break;
    case CObject::ClassLoot: Kind = 7; break;
    case CObject::ClassAsteroid: Kind = 8; break;
    case CObject::ClassDynamicAsteroid: Kind = 9; break;
    case CObject::ClassObject: Kind = 10; break;
    default: return nullptr;
    }

    FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);
    for (; Go != nullptr; Go = Go->ListNext)
    {
        if (Go->cobject() == Obj)
            return Go;
    }
    return nullptr;
}

unsigned ClientObjDb_GetRegisteredCount()
{
    return TotalCount(&G_ClientObjDb);
}

unsigned ClientObjDb_GetSize()
{
    return ClientObjDb_GetRegisteredCount();
}

bool ClientObjDb_TryGetWorldPosById(unsigned NicknameId, float OutPos[3])
{
    if (OutPos == nullptr || NicknameId == 0u)
        return false;

    for (int Kind = 1; Kind <= 10; ++Kind)
    {
        for (FClientGameobj* Go = G_ClientObjDb.Lists[Kind].First; Go != nullptr;
             Go = Go->ListNext)
        {
            if (Go->Object == nullptr)
                continue;
            if (Go->get_id() != NicknameId)
                continue;
            Vector const& Pos = Go->get_position();
            OutPos[0] = Pos.x;
            OutPos[1] = Pos.y;
            OutPos[2] = Pos.z;
            return true;
        }
    }
    return false;
}

bool ClientObjDb_TryGetWorldPosByArchetypeId(unsigned ArchetypeId, float OutPos[3])
{
    if (OutPos == nullptr || ArchetypeId == 0u)
        return false;

    for (int Kind = 1; Kind <= 10; ++Kind)
    {
        for (FClientGameobj* Go = G_ClientObjDb.Lists[Kind].First; Go != nullptr;
             Go = Go->ListNext)
        {
            if (Go->Object == nullptr)
                continue;
            Archetype::Root* const Arch = Go->Object->get_archetype();
            if (Arch == nullptr || Arch->Nickname.Id != ArchetypeId)
                continue;
            Vector const& Pos = Go->get_position();
            OutPos[0] = Pos.x;
            OutPos[1] = Pos.y;
            OutPos[2] = Pos.z;
            return true;
        }
    }
    return false;
}

FClientGameobj* ClientObjDb_GetKindHead(int const* Kind)
{
    // FUN_00540fc0: each case returns one bucket's head dword (kind 1 DAT_00678aac, 2 ab8,
    // 3 a94, 4 aa0, 5 a88, 6 a7c, 7 a64, 8 a4c, 9 a40, 10 a70).
    int const Index = *Kind;
    if (Index < 1 || Index > 10)
        return nullptr;
    return G_ClientObjDb.Lists[Index].First;
}

FClientGameobj* ClientObjDb_LargeSolarFirst(size_t* It)
{
    // FUN_00540a00
    *It = 0;
    if (*It == G_LargeSolars.size())
        return nullptr;
    return G_LargeSolars[*It];
}

FClientGameobj* ClientObjDb_LargeSolarNext(size_t* It)
{
    // FUN_00540a20
    *It = *It + 1;
    if (*It == G_LargeSolars.size())
        return nullptr;
    return G_LargeSolars[*It];
}

bool ClientObjDb_IsLargeSolar(FClientGameobj const* Go)
{
    // FUN_00540a40: a null entry ends the walk with false.
    for (size_t Index = 0; Index < G_LargeSolars.size(); ++Index)
    {
        FClientGameobj const* const Entry = G_LargeSolars[Index];
        if (Entry == nullptr)
            return false;
        if (Entry == Go)
            return true;
    }
    return false;
}

void ClientObjDb_MoveBeams(float Dt)
{
    // FUN_00540bd0 (called from the space frame): no null test on the narrowed CBeam.
    int Kind = 3;
    for (FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind); Go != nullptr; Go = Go->ListNext)
    {
        CObject* const Obj = Go->Object;
        CBeam* Beam = nullptr;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x67u) == 0x67u)
            Beam = static_cast<CBeam*>(Obj);
        Beam->move(Dt);
    }
}

namespace
{
    // DAT_006754bc -- the number of PhySys collision events FUN_0052fc20 has walked. Its only
    // reader is that increment.
    int GClientCollisionEventCount = 0;

    // The 0x24-byte record FUN_0052fc20 builds for slot 101's first argument (the vectors
    // negated for side A).
    struct FClientCollisionRecord
    {
        Vector Point;      // +0x00  event +0x10
        Vector SurfaceNormal; // +0x0c  event +0x1c
        Vector RelativeVelocity; // +0x18  event +0x28
    };
    static_assert(sizeof(FClientCollisionRecord) == 0x24, "collision record 0x24");

    // FUN_0052fe60: one side of a PhySys collision -> the Gameobj of the part's root
    // (FUN_005419a0) and the sub-object it was hit on.
    void ResolveCollisionSide(CObject* Part, unsigned PartId, FClientGameobj** OutGo,
                              unsigned short* OutSubObjId)
    {
        *OutGo = nullptr;
        *OutSubObjId = SUBOBJ_ID_NONE;
        FClientGameobj* const Go = ClientObjDb_FindByCObject(GetRoot(Part));
        *OutGo = Go;
        if (Go == nullptr)
            return;
        // A non-CEqObj was hit on its root.
        CObject* const Obj = Go->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) != 0x103u)
        {
            *OutSubObjId = SUBOBJ_ID_ROOT;
            return;
        }
        // CObject::part_to_inst inlined on the PART. No entry, or an entry of -1, falls back to
        // the part's own engine instance.
        long Inst = Part->EngineInstanceId;
        if (Part->SurfaceManager != nullptr)
        {
            for (std::vector<FPartMapEntry>::const_iterator Entry = Part->PartMap.begin();
                 Entry != Part->PartMap.end(); ++Entry)
            {
                if (Entry->PartId == PartId)
                {
                    Inst = Entry->EngineInstanceId;
                    break;
                }
            }
            if (Inst == -1)
                Inst = Part->EngineInstanceId;
        }
        CEqObj* const Eq = static_cast<CEqObj*>(Obj);
        unsigned short const SubObjId = Eq->inst_to_subobj_id(Inst);
        *OutSubObjId = SubObjId;
        if (SubObjectID::IsShieldEquipID(SubObjId))
            return;
        // A functioning shield takes the hit.
        CEShield* const Shield = CEShield::cast(Eq->EquipManager.FindFirst(0x100u));
        if (Shield == nullptr)
            return;
        if (!Shield->IsFunctioning())
            return;
        *OutSubObjId = Shield->Id;
    }

    void const* SubObjArg(unsigned short SubObjId)
    {
        // Slot 101 takes the sub-object ids as dwords.
        return reinterpret_cast<void const*>(static_cast<uintptr_t>(SubObjId));
    }
}

void ClientObjDb_ProcessCollisions()
{
    FClientTickScope BeamProfile(EClientTickSection::BeamHits);
    // FUN_0052fc20 (called from the space frame). Each beam's life is read with no null test
    // on the narrowed CBeam; a life below 0 or NaN skips (0.0 is DAT_005c7474).
    int Count = 0;
    int Kind = 3;
    for (FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind); Go != nullptr; Go = Go->ListNext)
    {
        CObject* const Obj = Go->Object;
        CBeam* Beam = nullptr;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x67u) == 0x67u)
            Beam = static_cast<CBeam*>(Obj);
        if (!(Beam->Lifetime >= 0.0f))
            continue;
        if (Go->IsDead())
            continue;
        ++Count;
        PhySys::GenerateCollisions(Beam);
    }

    BeamProfile.Stop();
    LogStream::LogEvent("Timing_cu.collide.num_bullets", static_cast<float>(Count), 4);
    FClientTickScope HitProfile(EClientTickSection::HitDispatch);

    PhySys::CollisionEvent const* Begin = nullptr;
    PhySys::CollisionEvent const* End = nullptr;
    PhySys::GetCollisions(StarSysGetActiveSystemId(), Begin, End);

    // The next element (stride 0x34) is taken before the dispatch, the end compared after it.
    for (PhySys::CollisionEvent const* Event = Begin; Event != End; ++Event)
    {
        ++GClientCollisionEventCount;

        FClientGameobj* GoA = nullptr;
        unsigned short SubA = 0;
        ResolveCollisionSide(Event->ObjectA, Event->PartIdA, &GoA, &SubA);
        if (GoA == nullptr)
            continue;
        FClientGameobj* GoB = nullptr;
        unsigned short SubB = 0;
        ResolveCollisionSide(Event->ObjectB, Event->PartIdB, &GoB, &SubB);
        if (GoB == nullptr)
            continue;

        // Side A's record has both vectors negated.
        FClientCollisionRecord Rec;
        Rec.Point = Event->Point;
        Rec.SurfaceNormal.x = -Event->SurfaceNormal.x;
        Rec.SurfaceNormal.y = -Event->SurfaceNormal.y;
        Rec.SurfaceNormal.z = -Event->SurfaceNormal.z;
        Rec.RelativeVelocity.x = -Event->RelativeVelocity.x;
        Rec.RelativeVelocity.y = -Event->RelativeVelocity.y;
        Rec.RelativeVelocity.z = -Event->RelativeVelocity.z;
        GoA->OnCollision(&Rec, SubObjArg(SubA), GoB, SubObjArg(SubB));

        Rec.SurfaceNormal = Event->SurfaceNormal;
        Rec.RelativeVelocity = Event->RelativeVelocity;
        GoB->OnCollision(&Rec, SubObjArg(SubB), GoA, SubObjArg(SubA));
    }
}

namespace
{
    // DAT_00678adc -- this file's lazily acquired 3DMathEngine; FUN_00541580 is its only user.
    I3DMathEngine* G_RemotePhysMathEngine = nullptr;
}

void ClientObjDb_UpdateRemotePhysics(float Dt)
{
    // FUN_00541580 (called from the space frame).
    if (SinglePlayer())
        return;

    std::list<FRemotePhysBlock*>::iterator It = G_RemotePhysBlocks.begin();
    for (; It != G_RemotePhysBlocks.end(); ++It)
    {
        FRemotePhysBlock* const Block = *It;
        // The sum is formed in double.
        Block->ElapsedTime = static_cast<double>(Dt) + Block->ElapsedTime;
        CShip* const Ship = Block->Ship;
        Block->Simulation.Doit(static_cast<double>(Dt));

        // No null test on the ship anywhere below.
        if (G_RemotePhysMathEngine == nullptr)
            G_RemotePhysMathEngine = Acquire3DMathEngine();
        Quaternion Orientation;
        G_RemotePhysMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orientation),
                                                   reinterpret_cast<FMatrix const&>(
                                                       Ship->Orientation));

        // The position is passed twice.
        if (!Block->Simulation.CheckForSync(Ship->WorldPosition, Ship->WorldPosition,
                                            Orientation))
            continue;

        // IServer slot 52. Port deviation: retail never writes +0x28 and the three bytes after
        // +0x2c (stack contents); the port stores 0.
        SSPObjUpdateInfo Info;
        Info.ObjectId = Ship->Id;
        Info.Orientation = Orientation;
        Info.Position = Ship->WorldPosition;
        Info.Timestamp = Block->ElapsedTime;
        Info.ThrottleBits = 0;
        Info.State = static_cast<unsigned char>(Ship->BehaviorIndex);
        Info.Pad_0x2d[0] = 0;
        Info.Pad_0x2d[1] = 0;
        Info.Pad_0x2d[2] = 0;
        static_cast<IServer*>(GetLocalServerInterface())->SPObjUpdate(Info,
                                                                       GetPlayerConnectionId());

        // The Timestamp is re-read from the info.
        Block->Simulation.RemoteUpdate(Ship->WorldPosition, Orientation, Info.Timestamp);
    }
}

namespace
{
    // The loop FUN_005416c0 repeats once per bucket.
    FClientGameobj* FindObjectInKind(int Kind, unsigned ObjectId)
    {
        FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);
        for (; Go != nullptr; Go = Go->ListNext)
        {
            if (Go->get_id() == ObjectId)
                return Go;
        }
        return nullptr;
    }

    // The loop FUN_00541890 runs over buckets 1 and 2. Retail compares through a null narrow
    // too; buckets 1 and 2 only ever hold ships and solars, both CEqObj.
    FClientGameobj* FindEqObjInKind(int Kind, unsigned VibeId)
    {
        FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);
        for (; Go != nullptr; Go = Go->ListNext)
        {
            CObject const* const Obj = Go->cobject();
            CEqObj const* EqObj = nullptr;
            if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) == 0x103u)
                EqObj = static_cast<CEqObj const*>(Obj);
            if (EqObj->VibeId == VibeId)
                return Go;
        }
        return nullptr;
    }
}

FClientGameobj* ClientObjDb_FindObject(unsigned ObjectId, int Kind)
{
    // FUN_005416c0
    if ((ObjectId & 0x3fffffffu) == 0u)
        return nullptr;
    if (Kind != 0)
        return FindObjectInKind(Kind, ObjectId);

    int const kOrder[6] = {1, 4, 5, 6, 7, 2};
    for (int OrderIndex = 0; OrderIndex < 6; ++OrderIndex)
    {
        FClientGameobj* const Found = FindObjectInKind(kOrder[OrderIndex], ObjectId);
        if (Found != nullptr)
            return Found;
    }
    return nullptr;
}

FClientGameobj* ClientObjDb_FindEqObjByVibeId(unsigned VibeId, int Kind)
{
    // FUN_00541890. Kind != 0 walks that bucket by get_id -- not by the vibe field.
    if ((VibeId & 0x3fffffffu) == 0u)
        return nullptr;
    if (Kind != 0)
        return FindObjectInKind(Kind, VibeId);

    FClientGameobj* const Ship = FindEqObjInKind(1, VibeId);
    if (Ship != nullptr)
        return Ship;
    return FindEqObjInKind(2, VibeId);
}

unsigned ClientObjDb_GetSolarCount()
{
    if (G_ClientObjDb.Lists[2].Count <= 0)
        return 0u;
    return static_cast<unsigned>(G_ClientObjDb.Lists[2].Count);
}

void ClientObjDb_ClearExcept(std::list<FClientGameobj*> const* Keep)
{
    // FUN_00541210 (the star-system unload). A remote-physics block survives only when its Ship
    // is some keep-list value's CShip (a null value, or a non-ship, keeps a block whose Ship is
    // null). A null Keep erases every block.
    std::list<FRemotePhysBlock*>::iterator It = G_RemotePhysBlocks.begin();
    while (It != G_RemotePhysBlocks.end())
    {
        bool bKeep = false;
        if (Keep != nullptr)
        {
            std::list<FClientGameobj*>::const_iterator KeepIt = Keep->begin();
            for (; KeepIt != Keep->end(); ++KeepIt)
            {
                FClientGameobj* const KeepGo = *KeepIt;
                CShip* const KeepShip = KeepGo != nullptr ? NarrowShip(KeepGo) : nullptr;
                if (KeepShip == (*It)->Ship)
                {
                    bKeep = true;
                    break;
                }
            }
        }
        if (bKeep)
        {
            ++It;
        }
        else
        {
            delete *It;
            It = G_RemotePhysBlocks.erase(It);
        }
    }

    // FUN_00542410 on every list in this order; the large-solar vector is emptied between.
    ClearListExcept(&G_ClientObjDb.Lists[1], Keep, true);
    ClearListExcept(&G_ClientObjDb.Lists[3], Keep, true);
    ClearListExcept(&G_ClientObjDb.Lists[4], Keep, true);
    ClearListExcept(&G_ClientObjDb.Lists[5], Keep, true);
    ClearListExcept(&G_ClientObjDb.Lists[6], Keep, true);
    G_LargeSolars.clear();
    ClearListExcept(&G_ClientObjDb.Lists[2], Keep, true);
    ClearListExcept(&G_ClientObjDb.Lists[10], Keep, true);
    ClearListExcept(&G_ClientObjDb.Lists[0], Keep, true);
    ClearListExcept(&G_ClientObjDb.Lists[7], Keep, true);
    ClearListExcept(&G_ClientObjDb.Lists[8], Keep, true);
    ClearListExcept(&G_ClientObjDb.Lists[9], Keep, true);

    // FUN_00540e90's body.
    ClientObjDb_FreeSpatial();
}

void ClientObjDb_FreeSpatial()
{
    // FUN_00540e90
    if (G_ClientObjDb.Spatial != nullptr)
    {
        FreeSpatial(G_ClientObjDb.Spatial);
        G_ClientObjDb.Spatial = nullptr;
    }
}

void ClientObjDb_RebuildSpatial()
{
    // FUN_00540eb0
    if (G_ClientObjDb.Spatial != nullptr)
        G_ClientObjDb.Spatial->Clear();

    // The count compares unsigned.
    unsigned const SolarCount = static_cast<unsigned>(G_ClientObjDb.Lists[2].Count);
    if (SolarCount == 0u)
        return;

    ++G_ClientSpatialRebuildCount;
    if (G_ClientObjDb.Spatial == nullptr)
    {
        G_ClientObjDb.Spatial = NewSpatial("BSP"); // PTR_LAB_005df8a4
        if (G_ClientObjDb.Spatial == nullptr)
            return;
    }

    FSpatialEntry* const Entries =
        static_cast<FSpatialEntry*>(operator new(SolarCount * sizeof(FSpatialEntry)));
    unsigned Added = 0;
    FClientGameobj* Cursor = nullptr;
    while (AdvanceListCursor(&G_ClientObjDb.Lists[2], &Cursor) != nullptr)
    {
        uint Type = 0;
        if (Cursor->get_type(Type) != 0)
            continue;
        if ((Type & 0xfdffu) == 0u)
            continue;
        Entries[Added].Object = static_cast<IObjRW*>(Cursor);
        Entries[Added].Position = Cursor->cobject()->WorldPosition;
        Entries[Added].Radius = Cursor->cobject()->Radius;
        ++Added;
    }
    G_ClientObjDb.Spatial->AddArray(Entries, Added);
    operator delete(Entries);
}

bool ClientObjDb_TryGetSolarWorldPosByIndex(unsigned Index, float OutPos[3])
{
    if (OutPos == nullptr)
        return false;

    unsigned Cursor = 0;
    for (FClientGameobj* Go = G_ClientObjDb.Lists[2].First; Go != nullptr; Go = Go->ListNext)
    {
        if (Go->Object == nullptr)
            continue;
        if (Cursor == Index)
        {
            Vector const& Pos = Go->get_position();
            OutPos[0] = Pos.x;
            OutPos[1] = Pos.y;
            OutPos[2] = Pos.z;
            return true;
        }
        ++Cursor;
    }
    return false;
}

// ===========================================================================
// OwnerList<pub::SpaceObj::CargoDesc> -- template defined in src/include/common_server.h.
// Retail FreeLancer.exe exports its own copy of all six members (the same code as Common.dll's
// and Server.dll's): ??0 (ord 2, .text:004c9770), copy ??0 (ord 1, .text:004c9790), ??1
// (ord 14, .text:004ced60), ??4 (ord 15, .text:0041c1a0), ??_7 (ord 25, .rdata:005d711c) and
// free (ord 58, .text:004cecf0). This explicit instantiation definition, with
// SHARED_INLINE_API = dllexport under FREELANCER_EXPORTS, emits and exports them here.
// ===========================================================================
// Member-level export cannot emit ??_7, so the vftable is exported by name. The explicit
// instantiation of the constructors above references the vftable, so this TU always defines
// the symbol.
OWNERLIST_CARGODESC_EXPORTS
#pragma comment(linker, "/EXPORT:??_7?$OwnerList@UCargoDesc@SpaceObj@pub@@@@6B@,DATA")
