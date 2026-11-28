// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Client/Space/tradelane.cpp -- the client trade lane ring runtime, 0x005135b0..0x00516ae3,
// plus the IClient entry points that drive it (LAB_0053c760, LAB_0053c830, FUN_0053d940) and the
// solar activate hook FUN_005484c0.

#include "Space/Effects/tradelane_fx.h"

#include "Space/Effects/cli_projectile_fx.h"
#include "Space/Effects/client_equip_fx.h"
#include "Common/client_cliobj.h"
#include "Space/client_objdb.h"
#include "common_cequip.h"
#include "common_cobject.h"
#include "common_csys.h"
#include "common_id.h"
#include "common_iface.h"
#include "common_server_types.h"
#include "common_misc.h" // SinglePlayer
#include "dacom.h"
#include "Space/HUD/hud_target.h"
#include "NeuroNet/nn_arrival_text.h"
#include "Space/Effects/particle_effect.h"
#include "Space/player_behavior.h"
#include "Common/Graphics/vis_effect_arch.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    // .rdata 005db798 / 005dec60 -- the source paths the FDUMP lines carry.
    char const kTradeLaneCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\tradelane.cpp";
    char const kIClientSCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\IClientS.cpp";

    // {head, tail, count} -- the live nodes DAT_006751c0/c4/c8 and the free list
    // DAT_006751b4/b8/bc.
    struct FTlRingList
    {
        FTradeLaneRingRuntime* Head;
        FTradeLaneRingRuntime* Tail;
        int Count;
    };
    FTlRingList G_TlLiveRings = {nullptr, nullptr, 0}; // DAT_006751c0
    FTlRingList G_TlFreeRings = {nullptr, nullptr, 0}; // DAT_006751b4

    // FUN_00514110's statics: DAT_006751fc (cached once), DAT_006751dc/e0/e4 (HpRightLane) and
    // DAT_006751d0/d4/d8 (HpLeftLane). DAT_006751e8 only guards the atexit registration of the two
    // vectors' (empty) destructors, LAB_00514250 / LAB_00514240 -- nothing to register here.
    bool G_TlHpCached = false;
    Vector G_TlHpRight = {};
    Vector G_TlHpLeft = {};

    // DAT_00675200 -- .bss, read by FUN_00513b40 / FUN_005163e0 and never written anywhere in the
    // exe, so the travel effect is only ever started by FUN_005142e0 / FUN_00515890.
    unsigned char G_TlStartTravelOnEnter = 0;

    // DAT_006751f8 -- tradelane.cpp's 3DMathEngine, acquired on first use.
    I3DMathEngine* G_TlMath = nullptr;

    I3DMathEngine* TlMath()
    {
        if (G_TlMath == nullptr)
            G_TlMath = Acquire3DMathEngine();
        return G_TlMath;
    }

    // CliEQTradeLane (client_cliobj.cpp FCliTradeLane) -- the fields this file reads.
    enum : unsigned
    {
        kTlPlayerTravel = 0x58,
        kTlShipTravel = 0x6c,
        kTlActivationStart = 0xa0,
        kTlActivationEnd = 0xa4,
    };

    ID_String const* TlCompanionId(Archetype::ICliObj* Companion, unsigned Offset)
    {
        return reinterpret_cast<ID_String const*>(reinterpret_cast<unsigned char*>(Companion) +
                                                  Offset);
    }

    float TlCompanionFloat(Archetype::ICliObj* Companion, unsigned Offset)
    {
        return *reinterpret_cast<float const*>(reinterpret_cast<unsigned char*>(Companion) +
                                               Offset);
    }

    FTlShipListNode* TlShipListHead(FTradeLaneRingRuntime* Ring)
    {
        return Ring->ShipListSentinel;
    }

    // FUN_0042a7e0: operator_new(0xc), Next / Prev as given, a null one pointing back at the
    // node itself.
    FTlShipListNode* TlNewShipListNode(FTlShipListNode* Next, FTlShipListNode* Prev)
    {
        FTlShipListNode* const Node = static_cast<FTlShipListNode*>(operator new(sizeof(FTlShipListNode)));
        Node->Next = (Next != nullptr) ? Next : Node;
        Node->Prev = (Prev != nullptr) ? Prev : Node;
        return Node;
    }

    // push_back, inlined twice in retail.
    void TlShipListPushBack(FTradeLaneRingRuntime* Ring, unsigned ShipId)
    {
        FTlShipListNode* const Head = TlShipListHead(Ring);
        FTlShipListNode* const Node = TlNewShipListNode(Head, Head->Prev);
        Head->Prev = Node;
        Node->Prev->Next = Node;
        Node->ShipId = ShipId; // FUN_005999b0 (copy-construct the value)
        ++Ring->ShipListCount;
    }

    // FUN_00516f70 (erase) -- unlink, free (FUN_005b7e1d), size - 1.
    void TlShipListErase(FTradeLaneRingRuntime* Ring, FTlShipListNode* Node)
    {
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        operator delete(Node);
        --Ring->ShipListCount;
    }

    // The find loop every leave path inlines: the node holding ShipId, or null.
    FTlShipListNode* TlShipListFind(FTradeLaneRingRuntime* Ring, unsigned ShipId)
    {
        FTlShipListNode* const Head = TlShipListHead(Ring);
        for (FTlShipListNode* Node = Head->Next; Node != Head; Node = Node->Next)
        {
            if (Node->ShipId == ShipId)
                return Node;
        }
        return nullptr;
    }

    // FUN_00404d50 -- insert Node before Before (null: at the tail).
    void TlRingListInsert(FTlRingList* List, FTradeLaneRingRuntime* Node,
                          FTradeLaneRingRuntime* Before)
    {
        FTradeLaneRingRuntime* Prev = nullptr;
        if (Before == nullptr)
        {
            Prev = List->Tail;
            List->Tail = Node;
        }
        else
        {
            Prev = Before->LivePrev;
            Before->LivePrev = Node;
        }
        if (Prev == nullptr)
            List->Head = Node;
        else
            Prev->LiveNext = Node;
        Node->LiveNext = Before;
        Node->LivePrev = Prev;
        ++List->Count;
    }

    // FUN_00404d90 -- unlink Node; its own links are left as they are.
    void TlRingListRemove(FTlRingList* List, FTradeLaneRingRuntime* Node)
    {
        if (Node->LivePrev == nullptr)
            List->Head = Node->LiveNext;
        else
            Node->LivePrev->LiveNext = Node->LiveNext;
        if (Node->LiveNext == nullptr)
            List->Tail = Node->LivePrev;
        else
            Node->LiveNext->LivePrev = Node->LivePrev;
        --List->Count;
    }

    // The watched ring gameobj: RingWatcher's Watchable minus 8, or null.
    FClientGameobj* TlWatchedGameobj(FTradeLaneRingRuntime const* Ring)
    {
        Watchable const* const Watched = Ring->RingWatcher.target();
        if (Watched == nullptr)
            return nullptr;
        return reinterpret_cast<FClientGameobj*>(
            reinterpret_cast<unsigned char*>(const_cast<Watchable*>(Watched)) - 8);
    }

    // The CSolar narrowing every function here inlines: null gameobj, null object or a class
    // that is not a solar ((class & 0x303) != 0x303) gives null.
    CSolar* TlNarrowSolar(FClientGameobj* Gameobj)
    {
        if (Gameobj == nullptr)
            return nullptr;
        CObject* const Obj = Gameobj->GetObject();
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
            return nullptr;
        return static_cast<CSolar*>(Obj);
    }

    // FUN_005140c0 -- the ring solar's TradeLane equipment (0x800000) companion: the
    // CliEQTradeLane. FindFirst runs on the narrowed solar with no null test.
    Archetype::FCliTradeLane* TlTradeLaneCompanion(FClientGameobj* RingGameobj)
    {
        CSolar* const Solar = TlNarrowSolar(RingGameobj);
        CEquip const* const Equip = Solar->EquipManager.FindFirst(EquipTypeTradeLane);
        if (Equip == nullptr)
            return nullptr;
        Archetype::Equipment const* const Arch = Equip->EquipArch();
        if (Arch == nullptr)
            return nullptr;
        return static_cast<Archetype::FCliTradeLane*>(Arch->Companion);
    }

    FTradeLaneRingRuntime*& TlShipRingPtr(FClientGameobj* Ship)
    {
        return static_cast<FClientShipGameobj*>(Ship)->TradeLaneRing;
    }

    unsigned char& TlShipFlags184(FClientGameobj* Ship)
    {
        return static_cast<FClientShipGameobj*>(Ship)->TradeLaneShipFlags;
    }

    unsigned char& TlShipFlags185(FClientGameobj* Ship)
    {
        return static_cast<FClientShipGameobj*>(Ship)->TradeLaneOneshotFlags;
    }

    // The +0x184 dword store, inlined at several sites.
    void TlShipClearFlags(FClientGameobj* Ship)
    {
        static_cast<FClientShipGameobj*>(Ship)->TradeLaneFlags = 0;
    }

    // The ship's lane effect companion (+0x164).
    FEffectCompanion* TlShipLaneCompanion(FClientGameobj* Ship)
    {
        return reinterpret_cast<FEffectCompanion*>(
            static_cast<FClientShipGameobj*>(Ship)->TradeLaneShipCompanion);
    }

    // The player's tunnel effect companion (+0x174).
    FEffectCompanion* TlShipTunnelCompanion(FClientGameobj* Ship)
    {
        return reinterpret_cast<FEffectCompanion*>(
            static_cast<FClientShipGameobj*>(Ship)->TradeLaneTunnelCompanion);
    }

    // FUN_0054baf0 -- the player's gameobj (the watchable at DAT_00679744 minus eight) or null.
    FClientGameobj* TlPlayerGameobj()
    {
        return static_cast<FClientGameobj*>(GetPlayerObjRW());
    }

    // FUN_00515640 (on a companion) -- the stop sequence, then the release FUN_00502d90
    // inlined. Also inlined at five other sites.
    void TlCompanionStopAndClear(FEffectCompanion* Companion)
    {
        CliEffectCompanionArmStop(Companion);
        CliEffectCompanionClear(Companion);
    }

    // FUN_00514070 (on the factory) -- an empty id gives false; else
    // FUN_00503070 on the same factory with FindEffectArch(Id).
    unsigned char TlCreateEffectFromId(FCliEffectFactory const* Factory, ID_String const* Id,
                                       FClientGameobj* ListOwner, int EngineIndex, Transform const* Xform,
                                       FEffectCompanion* CompanionOut)
    {
        if (Id->IsEmpty())
            return 0;
        return Factory->CreateEffectOneShotOn(FindEffectArch(&Id->Id), ListOwner, EngineIndex,
                                              Xform, CompanionOut);
    }

    // FUN_00515dd0 (on the factory) -- the world-space twin of FUN_00514070:
    // an empty id gives false, else FUN_00503f40 with FindEffectArch(Id).
    unsigned char TlCreateWorldEffectFromId(FCliEffectFactory const* Factory, ID_String const* Id,
                                            Transform const* Xform, float const* Pos3,
                                            FEffectCompanion* CompanionOut)
    {
        if (Id->IsEmpty())
            return 0;
        return Factory->CreateEffectWorldOneShotOn(FindEffectArch(&Id->Id), Xform, Pos3,
                                                   CompanionOut);
    }

    void TlIdentityXform(Transform* Out)
    {
        // FUN_004b5150 / FUN_004215b0 + zero translation.
        std::memset(Out, 0, sizeof(Transform));
        Out->Rotation.E[0][0] = 1.0f;
        Out->Rotation.E[1][1] = 1.0f;
        Out->Rotation.E[2][2] = 1.0f;
    }

    // The camera-space player one-shot of FUN_00514960 / 00514bf0 / 00514f20 / 00515b60 /
    // 00515e20: identity orientation at (0, 0, -radius) (get_physical_radius, vt+0x2c), a zero
    // velocity, on the CameraParticles factory DAT_00674c5c with no companion out.
    void TlPlayerCameraOneShot(FClientGameobj* Ship, ID_String const* Id)
    {
        float Radius = 0.0f;
        Vector Center = {};
        Ship->get_physical_radius(Radius, Center);
        Transform Xform;
        TlIdentityXform(&Xform);
        Xform.Translation.z = -Radius;
        float const Velocity[3] = {0.0f, 0.0f, 0.0f};
        TlCreateWorldEffectFromId(&G_CliEffectFactoryCameraParticles, Id, &Xform, Velocity,
                                  nullptr);
    }

    // FUN_00513fb0 -- |V| through the 0x5f3759df inverse square root: (1.5 - y*y*s*0.5) * y * s
    // with s = V.V (DAT_005c75e0 = 0.5, DAT_005ca220 = 1.5). s stays unrounded (only its float
    // copy feeds the bit trick) and so does the result: callers that round it get a float (the
    // distance), the speed stays wide -- hence double here.
    double TlVecLength(Vector const* V)
    {
        double const Sqr = static_cast<double>(V->x) * V->x + static_cast<double>(V->y) * V->y +
                           static_cast<double>(V->z) * V->z;
        float const SqrFloat = static_cast<float>(Sqr);
        int Bits = 0;
        std::memcpy(&Bits, &SqrFloat, sizeof(Bits));
        int const Guess = 0x5f3759df - (Bits >> 1);
        float Y = 0.0f;
        std::memcpy(&Y, &Guess, sizeof(Y));
        double const YD = Y;
        return (1.5 - YD * YD * Sqr * 0.5) * YD * Sqr;
    }

    // FUN_004fcbe0 (on ring + 0x20): the current glow to 1.0f.
    void TlGlowCtor(FTradeLaneRingRuntime* Ring)
    {
        Ring->GlowScale = 1.0f;
    }

    // FUN_004fcbf0 (on ring + 0x20): max / min of A and B (A > B keeps A as
    // the max, anything else -- NaN included -- B), the rate Param when above 0, else 0.
    void TlGlowSetRange(FTradeLaneRingRuntime* Ring, float Param, float A, float B)
    {
        Ring->GlowMax = B;
        Ring->GlowMin = A;
        if (A > B)
        {
            Ring->GlowMin = B;
            Ring->GlowMax = A;
        }
        if (Param > 0.0f)
            Ring->GlowParam = Param;
        else
            Ring->GlowParam = 0.0f;
    }

    // FUN_004fcc40 (on ring + 0x20) -- the glow moves towards its target at
    // GlowParam per second: 0 with no ship probe or the probe outside GlowMax, 1 inside GlowMin,
    // a linear ramp in between.
    float TlGlowUpdate(FTradeLaneRingRuntime* Ring, float Dt, Vector const* ProbeA,
                       Vector const* ProbeB)
    {
        float* const Block = &Ring->GlowMax;
        if (ProbeB == nullptr)
        {
            if (Block[2] >= 0.0f)
            {
                if (Block[2] > 0.0f)
                {
                    float const Next = Block[2] - Dt * Block[3];
                    Block[2] = Next;
                    if (Next < 0.0f)
                        Block[2] = 0.0f;
                }
            }
            else
            {
                float const Next = Dt * Block[3] + Block[2];
                Block[2] = Next;
                if (Next > 0.0f)
                    Block[2] = 0.0f;
            }
            return Block[2];
        }
        float const Dx = ProbeA->x - ProbeB->x;
        float const Dy = ProbeA->y - ProbeB->y;
        float const Dz = ProbeA->z - ProbeB->z;
        float const DistSqr = Dx * Dx + Dy * Dy + Dz * Dz;
        // The squared radii and the ramp target (1.0 at DAT_005c89b8) stay unrounded: double here.
        double const MaxR = Block[0];
        if (static_cast<double>(DistSqr) < MaxR * MaxR)
        {
            double const MinR = Block[1];
            if (static_cast<double>(DistSqr) > MinR * MinR)
            {
                double const Dist = std::sqrt(static_cast<double>(DistSqr));
                double const Target = 1.0 - (Dist - MinR) / (MaxR - MinR);
                if (Target <= Block[2])
                {
                    if (Target < Block[2])
                    {
                        float const Next = Block[2] - Dt * Block[3];
                        Block[2] = Next;
                        if (Next < Target)
                            Block[2] = static_cast<float>(Target);
                    }
                }
                else
                {
                    float const Next = Dt * Block[3] + Block[2];
                    Block[2] = Next;
                    if (Target < Next)
                        Block[2] = static_cast<float>(Target);
                }
            }
            else if (Block[2] >= 1.0f)
            {
                if (Block[2] > 1.0f)
                {
                    float const Next = Block[2] - Dt * Block[3];
                    Block[2] = Next;
                    if (Next < 1.0f)
                        Block[2] = 1.0f;
                }
            }
            else
            {
                float const Next = Dt * Block[3] + Block[2];
                Block[2] = Next;
                if (Next > 1.0f)
                    Block[2] = 1.0f;
            }
        }
        else if (Block[2] >= 0.0f)
        {
            if (Block[2] > 0.0f)
            {
                float const Next = Block[2] - Dt * Block[3];
                Block[2] = Next;
                if (Next < 0.0f)
                    Block[2] = 0.0f;
            }
        }
        else
        {
            float const Next = Dt * Block[3] + Block[2];
            Block[2] = Next;
            if (Next > 0.0f)
                Block[2] = 0.0f;
        }
        return Block[2];
    }

    // FUN_00514050 (on ring + 0x30): the companion's particle (+8), when there
    // is one, gets its own vtable slot +0x0c with the glow -- a tail JMP.
    void TlNotifyGlowScale(FTradeLaneRingRuntime* Ring, float Glow)
    {
        FEffectBase* const Effect = Ring->EffectCompanion.Effect;
        if (Effect == nullptr)
            return;
        Effect->SetIntensity(Glow);
    }

    // FUN_00514260 -- the next ring of the lane in the node's direction (traverse_rings with the
    // side key) while the current ring's health is not exactly 0.0f (NaN goes on), from ObjDB
    // bucket 2.
    FClientGameobj* TlNextRingGameobj(FTradeLaneRingRuntime* Ring)
    {
        unsigned NextId = 0;
        if (TlWatchedGameobj(Ring)->traverse_rings(NextId, Ring->SideKey != 0) != 0)
            return nullptr;
        float Health = 1.0f;
        TlWatchedGameobj(Ring)->get_status(Health);
        if (Health == 0.0f)
            return nullptr;
        return ClientObjDb_FindObject(NextId, static_cast<int>(EClientGameobjKind::Solar));
    }

    // FUN_005139d0 (the node is never read): the player's tunnel companion
    // stopped and cleared (0x40 dropped), a live lane companion taken off the ship's effect list
    // (FUN_004fcef0), then flags and the node pointer zeroed.
    void TlShipLeaveRingEffects(FTradeLaneRingRuntime* /*Ring*/, FClientGameobj* Ship)
    {
        if (Ship == TlPlayerGameobj())
        {
            TlCompanionStopAndClear(TlShipTunnelCompanion(Ship));
            TlShipFlags184(Ship) = static_cast<unsigned char>(TlShipFlags184(Ship) & ~kTradeLaneFlagTunnelFx);
        }
        FEffectCompanion* const Lane = TlShipLaneCompanion(Ship);
        if (CreateEffectCompanionAlive(Lane) != 0)
            CliEffectListRemove(&Ship->EffectList, Lane);
        TlShipClearFlags(Ship);
        TlShipRingPtr(Ship) = nullptr;
    }

    // FUN_005145a0 -- the enter effect, once (+0x185 bit 1; bits
    // 0..1 set): tl_player_enter for the player when it names one, else tl_ship_enter; identity
    // at the ship's engine index, factory DAT_00674c60, no companion out.
    void TlOneshot145a0(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship)
    {
        bool const bPlayer = Ship == TlPlayerGameobj();
        Archetype::FCliTradeLane* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
        if ((TlShipFlags185(Ship) & 2u) != 0)
            return;
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) | 3u);
        ID_String const* Id = &Companion->ShipEnter;
        if (bPlayer && !Companion->PlayerEnter.IsEmpty())
            Id = &Companion->PlayerEnter;
        Transform Xform;
        TlIdentityXform(&Xform);
        long const Index = Ship->get_index();
        if (Id->IsEmpty())
            return;
        G_CliEffectFactoryC60.CreateEffectOneShotOn(FindEffectArch(&Id->Id), Ship,
                                                    static_cast<int>(Index), &Xform, nullptr);
    }

    // FUN_00514750 -- the travel effect, once (+0x184 bit 0x20).
    // Player: tl_player_travel on the tunnel factory DAT_006751cc, oriented like the ring (its
    // third column negated unless +0x184 bit 0 is set), at the ship's position, engine index -1,
    // into the tunnel companion +0x174. Anyone else: tl_ship_travel on DAT_00674c60, identity at
    // the ship's engine index, into the lane companion +0x164. FUN_005142e0 inlines this whole
    // body through FUN_00514070.
    void TlOneshot14750(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship)
    {
        bool const bPlayer = Ship == TlPlayerGameobj();
        Archetype::ICliObj* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
        if ((TlShipFlags184(Ship) & kTradeLaneFlagTravelFx) != 0)
            return;
        TlShipFlags184(Ship) = static_cast<unsigned char>(TlShipFlags184(Ship) | kTradeLaneFlagTravelFx);
        if (bPlayer)
        {
            Transform const& RingXform = TlWatchedGameobj(Ring)->get_transform();
            Transform Xform;
            std::memcpy(&Xform.Rotation, &RingXform.Rotation, sizeof(Xform.Rotation));
            if ((TlShipFlags184(Ship) & kTradeLaneFlagSideKey) == 0)
            {
                Xform.Rotation.E[0][2] = -Xform.Rotation.E[0][2];
                Xform.Rotation.E[1][2] = -Xform.Rotation.E[1][2];
                Xform.Rotation.E[2][2] = -Xform.Rotation.E[2][2];
            }
            Vector const& Pos = Ship->get_position();
            Xform.Translation.x = Pos.x;
            Xform.Translation.y = Pos.y;
            Xform.Translation.z = Pos.z;
            TlCreateEffectFromId(&G_CliEffectFactoryTunnel, TlCompanionId(Companion, kTlPlayerTravel),
                                 Ship, -1, &Xform, TlShipTunnelCompanion(Ship));
            return;
        }
        Transform Xform;
        TlIdentityXform(&Xform);
        long const Index = Ship->get_index();
        TlCreateEffectFromId(&G_CliEffectFactoryC60, TlCompanionId(Companion, kTlShipTravel), Ship,
                             static_cast<int>(Index), &Xform,
                             TlShipLaneCompanion(Ship));
    }

    // FUN_00514960 -- the splash ahead of the next ring, once
    // (+0x185 bit 0). The player gets tl_player_splash in camera space when it names one; else
    // tl_ship_splash in world space at the ring's lane hardpoint for the side in +0x184 bit 0
    // (FUN_00514110, then DAT_006751f8's TransformXform with the ring's transform), factory
    // DAT_00674c60 through FUN_00503f40, no companion out.
    void TlOneshot14960(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship)
    {
        if ((TlShipFlags185(Ship) & 1u) != 0)
            return;
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) | 1u);
        bool const bPlayer = Ship == TlPlayerGameobj();
        Archetype::FCliTradeLane* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
        if (bPlayer && !Companion->PlayerSplash.IsEmpty())
        {
            TlPlayerCameraOneShot(Ship, &Companion->PlayerSplash);
            return;
        }
        FClientGameobj* const RingGo = TlWatchedGameobj(Ring);
        Vector HpLocal;
        TradeLaneRingLaneHardpoint(&HpLocal, RingGo,
                                   static_cast<unsigned char>(TlShipFlags184(Ship) & kTradeLaneFlagSideKey));
        Transform const* const RingXform = &RingGo->get_transform();
        Vector HpWorld;
        TlMath()->TransformXform(HpWorld, *RingXform, HpLocal);
        Transform Xform;
        TlIdentityXform(&Xform);
        Xform.Translation = HpWorld;
        float const Velocity[3] = {0.0f, 0.0f, 0.0f};
        ID_String const* const Id = &Companion->ShipSplash;
        if (Id->IsEmpty())
            return;
        G_CliEffectFactoryC60.CreateEffectWorldOneShotOn(FindEffectArch(&Id->Id), &Xform,
                                                         Velocity, nullptr);
    }

    // FUN_00514bf0 -- the exit, once (+0x185 bit 2; bits 0 and 2
    // set). The player: with bAnnounce, the lane's space name (CSolar::get_tradelane_space_name)
    // through NN_ArrivalText (FUN_0045b340); the tunnel companion stopped and cleared (0x40
    // dropped). A live lane companion leaves the ship's effect list. Then tl_player_exit in camera
    // space for the player when it names one, else tl_ship_exit at the ship on DAT_00674c60.
    void TlOneshot14bf0(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship, bool bAnnounce)
    {
        if ((TlShipFlags185(Ship) & 4u) != 0)
            return;
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) | 5u);
        bool const bPlayer = Ship == TlPlayerGameobj();
        Archetype::FCliTradeLane* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
        if (bPlayer)
        {
            if (bAnnounce)
            {
                CSolar* const Solar = TlNarrowSolar(TlWatchedGameobj(Ring));
                if (Solar != nullptr)
                {
                    unsigned const NameIds = Solar->get_tradelane_space_name();
                    if (NameIds != 0)
                        NnArrivalTextShowNamed(NameIds);
                }
            }
            TlCompanionStopAndClear(TlShipTunnelCompanion(Ship));
            TlShipFlags184(Ship) = static_cast<unsigned char>(TlShipFlags184(Ship) & ~kTradeLaneFlagTunnelFx);
        }
        FEffectCompanion* const Lane = TlShipLaneCompanion(Ship);
        if (CreateEffectCompanionAlive(Lane) != 0)
            CliEffectListRemove(&Ship->EffectList, Lane);
        if (bPlayer && !Companion->PlayerExit.IsEmpty())
        {
            TlPlayerCameraOneShot(Ship, &Companion->PlayerExit);
            return;
        }
        Transform Xform;
        TlIdentityXform(&Xform);
        long const Index = Ship->get_index();
        ID_String const* const Id = &Companion->ShipExit;
        if (Id->IsEmpty())
            return;
        G_CliEffectFactoryC60.CreateEffectOneShotOn(FindEffectArch(&Id->Id), Ship,
                                                    static_cast<int>(Index), &Xform, nullptr);
    }

    // FUN_00514f20 -- leaving a lane early, unless the exit already
    // ran (+0x185 bit 2). The player's tunnel companion stopped and cleared (0x40 dropped first),
    // a live lane companion off the effect list. When the ring gameobj of the ship's node has
    // +0x1f0 set (no ship is using the lane, FUN_00516910) it is a normal exit: FUN_00514bf0 with
    // no announcement. Otherwise the disrupt effect: tl_player_disrupt in camera space for the
    // player when it names one, else tl_ship_disrupt at the ship. +0x185 |= 5 either way.
    void TlLeaveOneshot14f20(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship)
    {
        if ((TlShipFlags185(Ship) & 4u) != 0)
            return;
        bool const bPlayer = Ship == TlPlayerGameobj();
        Archetype::FCliTradeLane* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
        if (bPlayer)
        {
            TlShipFlags184(Ship) = static_cast<unsigned char>(TlShipFlags184(Ship) & ~kTradeLaneFlagTunnelFx);
            TlCompanionStopAndClear(TlShipTunnelCompanion(Ship));
        }
        FEffectCompanion* const Lane = TlShipLaneCompanion(Ship);
        if (CreateEffectCompanionAlive(Lane) != 0)
            CliEffectListRemove(&Ship->EffectList, Lane);
        FClientSolarGameobj const* const ShipRingGo =
            static_cast<FClientSolarGameobj const*>(TlWatchedGameobj(TlShipRingPtr(Ship)));
        if (ShipRingGo->bTradeLaneIdle != 0)
        {
            TlOneshot14bf0(Ring, Ship, false);
        }
        else if (Ship == TlPlayerGameobj() &&
                 !Companion->PlayerDisrupt.IsEmpty())
        {
            TlPlayerCameraOneShot(Ship, &Companion->PlayerDisrupt);
        }
        else
        {
            Transform Xform;
            TlIdentityXform(&Xform);
            long const Index = Ship->get_index();
            ID_String const* const Id = &Companion->ShipDisrupt;
            if (!Id->IsEmpty())
                G_CliEffectFactoryC60.CreateEffectOneShotOn(FindEffectArch(&Id->Id), Ship,
                                                            static_cast<int>(Index), &Xform, nullptr);
        }
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) | 5u);
    }

    void TlBadThings()
    {
        FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s", kTradeLaneCpp, 0x367, "bad things (tm)");
    }

    // FUN_005142e0 -- put Ship on this node. Already on it: only the
    // travel effect when riding (the FUN_00514750 body inlined). On another node: off that one
    // (not listed there: FDUMP "bad things (tm)"; listed: FUN_00514f20 when riding, FUN_005139d0,
    // erase). Then +0x160 = this, +0x185 bits 0 and 2 cleared, the travel effect when riding, and
    // the ship id appended to the list.
    void TlAssignShipToRing(FTradeLaneRingRuntime* Ring, FClientGameobj* Ship)
    {
        FTradeLaneRingRuntime* const Old = TlShipRingPtr(Ship);
        if (Old != nullptr)
        {
            if (Old == Ring)
            {
                if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) != 0)
                    TlOneshot14750(Ring, Ship);
                return;
            }
            FTlShipListNode* const Node = TlShipListFind(Old, Ship->get_id());
            if (Node == nullptr)
            {
                TlBadThings();
            }
            else
            {
                if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) != 0)
                    TlLeaveOneshot14f20(Old, Ship);
                TlShipLeaveRingEffects(Old, Ship);
                TlShipListErase(Old, Node);
            }
        }
        TlShipRingPtr(Ship) = Ring;
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) & 0xfau);
        if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) != 0)
            TlOneshot14750(Ring, Ship);
        TlShipListPushBack(Ring, Ship->get_id());
    }

    // FUN_005163e0 -- one pass over the node's ships; returns the nearest
    // ship not riding the lane (1e7 to start), or null. Gone ships and ships on another node are
    // erased. Each ship's time to the ring (distance over speed; 2500 while riding, 100 when the
    // speed is not above 1) triggers the enter effect (FUN_005145a0, plus FUN_00514750 under
    // DAT_00675200) below SecsBeforeEnter; while riding, the splash (next ring exists) below
    // SecsBeforeSplash or the exit (last ring) below SecsBeforeExit, and passing the ring
    // (get_ring_side flips) moves the ship to the next ring's node or, at the end, clears it.
    // FUN_00513b40 runs the same loop inline (calling FUN_005145a0 / FUN_00514750 where this
    // inlines them).
    FClientGameobj* TlRingUpdateShips(FTradeLaneRingRuntime* Ring)
    {
        FClientGameobj* Nearest = nullptr;
        float NearestDist = 1.0e7f; // 0x4b189680
        FTlShipListNode* const Head = TlShipListHead(Ring);
        FTlShipListNode* Node = Head->Next;
        while (Node != Head)
        {
            FTlShipListNode* const Next = Node->Next;
            FClientGameobj* const Ship = ClientObjDb_FindObject(Node->ShipId, static_cast<int>(EClientGameobjKind::Ship));
            if (Ship == nullptr)
            {
                TlShipListErase(Ring, Node);
                Node = Next;
                continue;
            }
            Archetype::FCliTradeLane* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
            if (TlShipRingPtr(Ship) != Ring)
            {
                TlShipListErase(Ring, Node);
                Node = Next;
                continue;
            }

            Vector const& RingPos = TlWatchedGameobj(Ring)->get_position();
            Vector const& ShipPos = Ship->get_position();
            Vector Delta;
            Delta.x = ShipPos.x - RingPos.x;
            Delta.y = ShipPos.y - RingPos.y;
            Delta.z = ShipPos.z - RingPos.z;
            float const Dist = static_cast<float>(TlVecLength(&Delta));

            double Speed = 2500.0; // DAT_005db7d8
            if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) == 0)
            {
                Vector const Velocity = Ship->get_velocity();
                Vector V;
                V.x = Velocity.x;
                V.y = Velocity.y;
                V.z = Velocity.z;
                Speed = TlVecLength(&V);
            }
            // Only a speed strictly above 1 divides.
            float Ratio = 100.0f;
            if (Speed > 1.0)
                Ratio = static_cast<float>(Dist / Speed);

            if (Ratio < Companion->SecsBeforeEnter)
            {
                TlOneshot145a0(Ring, Ship);
                if (G_TlStartTravelOnEnter != 0)
                    TlOneshot14750(Ring, Ship);
            }

            if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) == 0)
            {
                if (Dist < NearestDist)
                {
                    NearestDist = Dist;
                    Nearest = Ship;
                }
                Node = Next;
                continue;
            }

            FClientGameobj* const NextRing = TlNextRingGameobj(Ring);
            if (NextRing != nullptr)
            {
                if (Ratio < Companion->SecsBeforeSplash)
                    TlOneshot14960(Ring, Ship);
            }
            else if (Ratio < Companion->SecsBeforeExit)
            {
                TlOneshot14bf0(Ring, Ship, true);
            }

            bool bSide = true;
            FClientGameobj* const RingGo = TlWatchedGameobj(Ring);
            RingGo->get_ring_side(Ship->get_transform(), &bSide);
            unsigned char const WasSide =
                static_cast<unsigned char>((TlShipFlags184(Ship) >> 1) & 1u);
            if (WasSide != static_cast<unsigned char>(bSide ? 1u : 0u))
            {
                TlShipListErase(Ring, Node);
                TlShipRingPtr(Ship) = nullptr;
                if (NextRing != nullptr)
                {
                    FTradeLaneRingRuntime* const NewRing =
                        TradeLaneRingFindOrCreate(NextRing, Ring->SideKey, true);
                    if ((TlShipFlags184(Ship) & kTradeLaneFlagTravelFx) != 0 && Ship == TlPlayerGameobj())
                        TlShipFlags184(Ship) = static_cast<unsigned char>(TlShipFlags184(Ship) | kTradeLaneFlagTunnelFx);
                    TlAssignShipToRing(NewRing, Ship);
                }
                else
                {
                    TlShipClearFlags(Ship);
                }
            }
            Node = Next;
        }
        return Nearest;
    }

    // The tail FUN_00513b40 and FUN_00516060's leave path share: the glow probe at the ring's
    // lane hardpoint in world space; with a nearest ship, the ring's tl_ring_active effect
    // restarted at the lane hardpoint (DAT_00674c60, into ring + 0x30) when its companion is no
    // longer alive and the node is active; the glow updated against the nearest ship's position;
    // false when the node has no ship and the glow is at or below 0.001 (double DAT_005c75f8),
    // else the glow handed to the effect (FUN_00514050) and true.
    bool TlRingGlowTail(FTradeLaneRingRuntime* Ring, FClientGameobj* Nearest, float Dt)
    {
        FClientGameobj* const RingGo = TlWatchedGameobj(Ring);
        Vector HpLocal;
        TradeLaneRingLaneHardpoint(&HpLocal, RingGo, Ring->SideKey);
        Vector HpWorld;
        HudTransformPoint(&RingGo->get_transform(), &HpWorld,
                          &HpLocal);
        Transform Probe;
        TradeLaneRingMakeTranslationXform(&Probe, &HpWorld);

        Vector const* ProbeShip = nullptr;
        if (Nearest != nullptr)
        {
            ProbeShip = &Nearest->get_position();
            if (CreateEffectCompanionAlive(&Ring->EffectCompanion) == 0 && Ring->bActive != 0)
            {
                Archetype::FCliTradeLane* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));
                Vector Hp;
                TradeLaneRingLaneHardpoint(&Hp, TlWatchedGameobj(Ring), Ring->SideKey);
                Transform Xform;
                TradeLaneRingMakeTranslationXform(&Xform, &Hp);
                FClientGameobj* const Owner = TlWatchedGameobj(Ring);
                long const Index = TlWatchedGameobj(Ring)->get_index();
                TlCreateEffectFromId(&G_CliEffectFactoryC60, &Companion->RingActive,
                                     Owner, static_cast<int>(Index), &Xform, &Ring->EffectCompanion);
            }
        }

        float const Glow = TlGlowUpdate(Ring, Dt, &Probe.Translation, ProbeShip);
        if (Ring->ShipListCount == 0 && !(static_cast<double>(Glow) > 0.001))
            return false;
        TlNotifyGlowScale(Ring, Glow);
        return true;
    }

    // FUN_00513b40 -- false when the ring gameobj is gone; else the
    // ship pass (the FUN_005163e0 loop, inline in retail) and the glow tail.
    bool TlRingKeepAlive(FTradeLaneRingRuntime* Ring, float Dt)
    {
        if (TlWatchedGameobj(Ring) == nullptr)
            return false;
        FClientGameobj* const Nearest = TlRingUpdateShips(Ring);
        return TlRingGlowTail(Ring, Nearest, Dt);
    }

    // FUN_005137c0 -- a node going to the free list: every listed ship
    // still in ObjDB gets the FUN_005139d0 body (inline here), every entry is erased, and the
    // ring's own companion is stopped and cleared. The watcher and the rest stay as they are.
    void TlDestroyRingRuntime(FTradeLaneRingRuntime* Ring)
    {
        if (Ring->ShipListCount != 0)
        {
            FTlShipListNode* const Head = TlShipListHead(Ring);
            FTlShipListNode* Node = Head->Next;
            while (Node != Head)
            {
                FClientGameobj* const Ship = ClientObjDb_FindObject(Node->ShipId, static_cast<int>(EClientGameobjKind::Ship));
                if (Ship != nullptr)
                    TlShipLeaveRingEffects(Ring, Ship);
                TlShipListErase(Ring, Node);
                Node = Head->Next;
            }
        }
        TlCompanionStopAndClear(&Ring->EffectCompanion);
    }

    // FUN_00515730 -- the node's destructor: the FUN_005137c0 ship pass
    // (with FUN_00515640 for the player's tunnel companion), the ring companion stopped and
    // cleared, then the members torn down in reverse order (companion release, watcher, list).
    void TlRingRuntimeDestruct(FTradeLaneRingRuntime* Ring)
    {
        if (Ring->ShipListCount != 0)
        {
            FTlShipListNode* const Head = TlShipListHead(Ring);
            FTlShipListNode* Node = Head->Next;
            while (Node != Head)
            {
                FClientGameobj* const Ship = ClientObjDb_FindObject(Node->ShipId, static_cast<int>(EClientGameobjKind::Ship));
                if (Ship != nullptr)
                    TlShipLeaveRingEffects(Ring, Ship);
                TlShipListErase(Ring, Node);
                Node = Head->Next;
            }
        }
        TlCompanionStopAndClear(&Ring->EffectCompanion);
        CliEffectCompanionClear(&Ring->EffectCompanion); // the member's own release
        Ring->RingWatcher.set(nullptr);
        FTlShipListNode* const Head = TlShipListHead(Ring);
        FTlShipListNode* Node = Head->Next;
        while (Node != Head)
        {
            FTlShipListNode* const Next = Node->Next;
            TlShipListErase(Ring, Node);
            Node = Next;
        }
        operator delete(Head);
        Ring->ShipListSentinel = nullptr;
        Ring->ShipListCount = 0;
    }

    // FUN_00515b60 -- the ship leaves its node: no node, nothing; not listed there, FDUMP
    // "bad things (tm)"; listed, the FUN_00514f20 body when riding and the exit has not run, the
    // FUN_005139d0 body (both inlined in retail), erase.
    void TlShipLeaveCurrentRing(FClientGameobj* Ship)
    {
        FTradeLaneRingRuntime* const Ring = TlShipRingPtr(Ship);
        if (Ring == nullptr)
            return;
        FTlShipListNode* const Node = TlShipListFind(Ring, Ship->get_id());
        if (Node == nullptr)
        {
            TlBadThings();
            return;
        }
        if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) != 0 && (TlShipFlags185(Ship) & 4u) == 0)
            TlLeaveOneshot14f20(Ring, Ship);
        TlShipLeaveRingEffects(Ring, Ship);
        TlShipListErase(Ring, Node);
    }

    // IBehaviorManager event 4 (a trade lane) to the player's CEqObj behavior manager, when the
    // player has one (inlined in both IClient slots). The player's object is
    // narrowed to a CEqObj ((class & 0x103) == 0x103) with no null test on the result.
    void TlNotifyPlayerBehavior(FBehaviorTradelaneEvent* Block)
    {
        FClientGameobj* const Player = TlPlayerGameobj();
        if (Player == nullptr)
            return;
        CObject* const PlayerObj = Player->GetObject();
        CEqObj* const PlayerEq =
            (PlayerObj != nullptr && (static_cast<unsigned>(PlayerObj->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
                ? static_cast<CEqObj*>(PlayerObj)
                : nullptr;
        IBehaviorManager* const Behavior = PlayerEq->get_behavior_interface();
        if (Behavior == nullptr)
            return;
        FBehaviorManagerEvent Event;
        Event.Type = FBehaviorManagerEvent::kEventTradelane;
        Event.Data = Block;
        Behavior->OnEvent(&Event);
    }

    // The CShip narrowing of the IClient handlers ((class & 0x503) == 0x503).
    CShip* TlNarrowShip(FClientGameobj* Gameobj)
    {
        CObject* const Obj = Gameobj->GetObject();
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
            return nullptr;
        return static_cast<CShip*>(Obj);
    }
}

FTradeLaneRingRuntime* TradeLaneRingLiveHead()
{
    return G_TlLiveRings.Head;
}

int TradeLaneRingLiveCount()
{
    return G_TlLiveRings.Count;
}

void TradeLaneRingMakeTranslationXform(Transform* Out, Vector const* Translation)
{
    // FUN_00514010.
    Out->Rotation.E[0][0] = 1.0f;
    Out->Rotation.E[0][1] = 0.0f;
    Out->Rotation.E[0][2] = 0.0f;
    Out->Rotation.E[1][0] = 0.0f;
    Out->Rotation.E[1][1] = 1.0f;
    Out->Rotation.E[1][2] = 0.0f;
    Out->Rotation.E[2][0] = 0.0f;
    Out->Rotation.E[2][1] = 0.0f;
    Out->Rotation.E[2][2] = 1.0f;
    Out->Translation = *Translation;
}

void TradeLaneRingLaneHardpoint(Vector* Out, FClientGameobj* RingGameobj, unsigned char Side)
{
    // FUN_00514110. The hardpoints are looked up once for the
    // process, on the ring's engine index (vt+0x1c); FindHardpoint_PS's result is not tested.
    if (!G_TlHpCached)
    {
        G_TlHpCached = true;
        long HpHandle = 0;
        HardpointInfo HpInfo = {};
        FindHardpoint_PS(RingGameobj->get_index(), "HpRightLane", HpHandle, HpInfo);
        G_TlHpRight.x = HpInfo.Position.x;
        G_TlHpRight.y = HpInfo.Position.y;
        G_TlHpRight.z = HpInfo.Position.z;
        FindHardpoint_PS(RingGameobj->get_index(), "HpLeftLane", HpHandle, HpInfo);
        G_TlHpLeft.x = HpInfo.Position.x;
        G_TlHpLeft.y = HpInfo.Position.y;
        G_TlHpLeft.z = HpInfo.Position.z;
    }
    if (Side != 0)
        *Out = G_TlHpRight;
    else
        *Out = G_TlHpLeft;
}

FTradeLaneRingRuntime* TradeLaneRingFindOrCreate(FClientGameobj* RingGameobj,
                                                  unsigned char Side,
                                                  bool bCreate)
{
    // FUN_00515200. TypeBits bit 7 of the narrowed solar, read with no null test.
    if (!TlNarrowSolar(RingGameobj)->is_lane_ring())
        return nullptr;

    for (FTradeLaneRingRuntime* Node = G_TlLiveRings.Head; Node != nullptr; Node = Node->LiveNext)
    {
        if (TlWatchedGameobj(Node) == RingGameobj && Node->SideKey == Side)
            return Node;
    }
    if (!bCreate)
        return nullptr;

    FTradeLaneRingRuntime* Ring = nullptr;
    if (G_TlFreeRings.Count == 0)
    {
        // operator_new(0x40) and the inlined constructor.
        Ring = new FTradeLaneRingRuntime;
        Ring->LiveNext = nullptr;
        Ring->LivePrev = nullptr;
        Ring->Side = Side;
        Ring->ShipListSentinel = TlNewShipListNode(nullptr, nullptr);
        Ring->ShipListCount = 0;
        Ring->RingWatcher.set(nullptr);
        Ring->bActive = 1;
        TlGlowCtor(Ring);
        CliEffectCompanionInit(&Ring->EffectCompanion, nullptr, nullptr, nullptr, nullptr);
    }
    else
    {
        // The free list's head, unlinked inline.
        Ring = G_TlFreeRings.Head;
        TlRingListRemove(&G_TlFreeRings, Ring);
    }

    Ring->bActive = 1;
    Watchable const* WatchTarget = nullptr;
    if (RingGameobj != nullptr)
        WatchTarget = reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char*>(RingGameobj) + 8);
    Ring->RingWatcher.set(WatchTarget);
    Ring->SideKey = Side;

    Archetype::ICliObj* const Companion = TlTradeLaneCompanion(TlWatchedGameobj(Ring));

    // The lane hardpoint in world space made into a transform nothing reads (FUN_00514110
    // fills its cache as a side effect).
    Vector HpLocal;
    TradeLaneRingLaneHardpoint(&HpLocal, TlWatchedGameobj(Ring), Ring->SideKey);
    Vector HpWorld;
    HudTransformPoint(&TlWatchedGameobj(Ring)->get_transform(),
                      &HpWorld, &HpLocal);
    Transform Unused;
    TradeLaneRingMakeTranslationXform(&Unused, &HpWorld);

    Ring->GlowScale = 0.0f;
    TlGlowSetRange(Ring, 0.0f, TlCompanionFloat(Companion, kTlActivationEnd),
                   TlCompanionFloat(Companion, kTlActivationStart));

    // Pushed in front of the live head (FUN_00404d50's body inlined).
    TlRingListInsert(&G_TlLiveRings, Ring, G_TlLiveRings.Head);
    return Ring;
}

bool TradeLaneStartCommand(FClientGameobj* EntranceRing, FClientGameobj* ExitRing,
                           FClientGameobj* Ship, unsigned char SideByte)
{
    // FUN_00515890.
    unsigned NextId = 0;
    unsigned char LaneSide = 1;
    if (EntranceRing->traverse_rings(NextId, true) < 0 || NextId != ExitRing->get_id())
    {
        LaneSide = 0;
        if (EntranceRing->traverse_rings(NextId, false) < 0 || NextId != ExitRing->get_id())
        {
            FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s", kTradeLaneCpp, 0x3bc,
                  "server passed non-contigious ring information.... start tradlane command ignored");
            return false;
        }
    }

    FClientGameobj* const Anchor = (SideByte != 0) ? ExitRing : EntranceRing;
    FTradeLaneRingRuntime* const NewRing = TradeLaneRingFindOrCreate(Anchor, LaneSide, true);

    FTradeLaneRingRuntime* Old = TlShipRingPtr(Ship);
    if (Old != nullptr && Old != NewRing)
    {
        TradeLaneRingFindOrCreate(Anchor, LaneSide, true); // result unused
        Old = TlShipRingPtr(Ship);
        if (Old != nullptr)
        {
            FTlShipListNode* const Node = TlShipListFind(Old, Ship->get_id());
            if (Node == nullptr)
            {
                TlBadThings();
            }
            else
            {
                // The FUN_005139d0 body is inlined in retail.
                if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) != 0)
                    TlLeaveOneshot14f20(Old, Ship);
                TlShipLeaveRingEffects(Old, Ship);
                TlShipListErase(Old, Node);
            }
        }
    }

    // +0x184: the ring side against the exit ring (bit 1), riding (0x80), the node's side key
    // (bit 0).
    bool bRingSide = false;
    ExitRing->get_ring_side(Ship->get_transform(), &bRingSide);
    unsigned char Flags = TlShipFlags184(Ship);
    unsigned char const SideBit = static_cast<unsigned char>((bRingSide ? 1u : 0u) << 1);
    Flags = static_cast<unsigned char>((((SideBit ^ Flags) & kTradeLaneFlagRingSide) ^ Flags) | kTradeLaneFlagRiding);
    Flags = static_cast<unsigned char>(((NewRing->SideKey ^ Flags) & kTradeLaneFlagSideKey) ^ Flags);
    TlShipFlags184(Ship) = Flags;
    if (SideByte != 0)
        TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) | 2u);

    FTradeLaneRingRuntime* const Current = TlShipRingPtr(Ship);
    if (Current != nullptr)
    {
        if (Current == NewRing)
        {
            if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) != 0)
                TlOneshot14750(NewRing, Ship);
            return true;
        }
        TlShipLeaveCurrentRing(Ship);
    }

    TlShipRingPtr(Ship) = NewRing;
    TlShipFlags185(Ship) = static_cast<unsigned char>(TlShipFlags185(Ship) & 0xfau);
    if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) != 0)
        TlOneshot14750(NewRing, Ship);
    TlShipListPushBack(NewRing, Ship->get_id());
    return true;
}

void TradeLaneShipStop(FClientGameobj* Ship)
{
    // FUN_00515e20: the FUN_00515b60 body again (no node: nothing;
    // not listed: FDUMP; listed: the FUN_00514f20 body when riding and the exit has not run, the
    // FUN_005139d0 body, erase).
    TlShipLeaveCurrentRing(Ship);
}

bool TradeLaneShipGoHandler(FClientGameobj* Ship, FClientGameobj* RingA,
                            FClientGameobj* RingB)
{
    // LAB_00546e90's tail (0x00546f2b).
    return TradeLaneStartCommand(RingA, RingB, Ship, 0);
}

void TradeLaneDetachTick(float Dt)
{
    // FUN_005135b0.
    FClientGameobj* const Player = TlPlayerGameobj();
    if (Player != nullptr)
    {
        CShip* const PlayerShip = TlNarrowShip(Player);
        if (PlayerShip != nullptr && !PlayerShip->is_using_tradelane() &&
            (TlShipFlags184(Player) & kTradeLaneFlagRiding) != 0)
        {
            FTradeLaneRingRuntime* const Ring = TlShipRingPtr(Player);
            if (Ring != nullptr)
            {
                FTlShipListNode* const Node = TlShipListFind(Ring, Player->get_id());
                if (Node == nullptr)
                {
                    TlBadThings();
                }
                else
                {
                    if ((TlShipFlags184(Player) & kTradeLaneFlagRiding) != 0)
                        TlLeaveOneshot14f20(Ring, Player);
                    TlShipLeaveRingEffects(Ring, Player);
                    TlShipListErase(Ring, Node);
                }
            }
            TlCompanionStopAndClear(TlShipLaneCompanion(Player));
            TlCompanionStopAndClear(TlShipTunnelCompanion(Player));
        }
    }

    // A node FUN_00513b40 rejects leaves the live list for the tail of
    // the free list and is emptied (FUN_005137c0); the walk then goes on from the node *after* the
    // rejected one's successor, and restarts at the head when the rejected node was the last --
    // retail's cursor handling, kept as is.
    FTradeLaneRingRuntime* Cursor = nullptr;
    for (;;)
    {
        FTradeLaneRingRuntime* const Node = (Cursor == nullptr) ? G_TlLiveRings.Head : Cursor->LiveNext;
        if (Node == nullptr)
            break;
        if (TlRingKeepAlive(Node, Dt))
        {
            Cursor = Node;
            continue;
        }
        Cursor = Node->LiveNext;
        TlRingListRemove(&G_TlLiveRings, Node);
        TlRingListInsert(&G_TlFreeRings, Node, nullptr);
        TlDestroyRingRuntime(Node);
    }
}

void TradeLaneShutdown()
{
    // FUN_00515500. Both walks keep retail's cursor: after a node
    // is handled the next one looked at is the successor of the handled node's successor (the
    // head again once that successor is null), so they stop as soon as that lookup is null.
    FTradeLaneRingRuntime* After = nullptr;
    for (;;)
    {
        FTradeLaneRingRuntime* const Node = (After == nullptr) ? G_TlLiveRings.Head : After->LiveNext;
        if (Node == nullptr)
            break;
        After = Node->LiveNext;
        TlRingListRemove(&G_TlLiveRings, Node);
        TlRingListInsert(&G_TlFreeRings, Node, nullptr);
        if (Node->ShipListCount != 0)
        {
            FTlShipListNode* const Head = TlShipListHead(Node);
            FTlShipListNode* Entry = Head->Next;
            while (Entry != Head)
            {
                FClientGameobj* const Ship = ClientObjDb_FindObject(Entry->ShipId, static_cast<int>(EClientGameobjKind::Ship));
                if (Ship != nullptr)
                    TlShipLeaveRingEffects(Node, Ship);
                TlShipListErase(Node, Entry);
                Entry = Head->Next;
            }
        }
        TlCompanionStopAndClear(&Node->EffectCompanion);
    }

    FTradeLaneRingRuntime* FreeAfter = nullptr;
    for (;;)
    {
        FTradeLaneRingRuntime* const Node =
            (FreeAfter == nullptr) ? G_TlFreeRings.Head : FreeAfter->LiveNext;
        if (Node == nullptr)
            break;
        FreeAfter = Node->LiveNext;
        TlRingListRemove(&G_TlFreeRings, Node);
        TlRingRuntimeDestruct(Node);
        delete Node; // FUN_005b7e1d
    }
}

void TradeLaneRingSideCommand(FClientGameobj* RingGameobj, unsigned ShipId, bool bActivate)
{
    // FUN_00516060.
    FClientGameobj* const Ship = ClientObjDb_FindObject(ShipId, static_cast<int>(EClientGameobjKind::Ship));
    if (Ship == nullptr)
        return;

    if (!bActivate)
    {
        FTradeLaneRingRuntime* const Ring = TlShipRingPtr(Ship);
        if (Ring == nullptr)
            return;
        FTlShipListNode* const Node = TlShipListFind(Ring, Ship->get_id());
        if (Node == nullptr)
        {
            TlBadThings();
            return;
        }
        if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) != 0)
            TlLeaveOneshot14f20(Ring, Ship);
        TlShipLeaveRingEffects(Ring, Ship);
        TlShipListErase(Ring, Node);
        if (TlWatchedGameobj(Ring) == nullptr)
            return;
        FClientGameobj* const Nearest = TlRingUpdateShips(Ring);
        TlRingGlowTail(Ring, Nearest, 0.0f);
        return;
    }

    if (TlShipRingPtr(Ship) != nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kTradeLaneCpp, 0x424,
              "tradelane activate: server activating a tradelane twice for the same ship. ignoring. ");
        FTradeLaneRingRuntime* const Old = TlShipRingPtr(Ship);
        if (Old != nullptr)
        {
            FTlShipListNode* const Node = TlShipListFind(Old, Ship->get_id());
            if (Node == nullptr)
            {
                TlBadThings();
            }
            else
            {
                if ((TlShipFlags184(Ship) & kTradeLaneFlagRiding) != 0)
                    TlLeaveOneshot14f20(Old, Ship);
                TlShipLeaveRingEffects(Old, Ship);
                TlShipListErase(Old, Node);
                TlRingKeepAlive(Old, 0.0f); // the answer is not used
            }
        }
    }

    bool bDirection = false;
    if (RingGameobj->get_lane_direction(Ship->get_transform(), &bDirection) != 0)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kTradeLaneCpp, 0x42d,
              " couldnt find lane direction for activation effect. ignoring. ");
        return;
    }
    FTradeLaneRingRuntime* const NewRing =
        TradeLaneRingFindOrCreate(RingGameobj, static_cast<unsigned char>(bDirection ? 1u : 0u),
                                  true);
    if (NewRing != nullptr)
        TlAssignShipToRing(NewRing, Ship);
}

void FClientGameobj::TradeLaneSolarActivate(bool bActivate, unsigned ShipId)
{
    // FUN_005484c0. The object is narrowed with a null test only on
    // the pointer; TypeBits (+0xe0) is then read with no test.
    CObject* const Obj = GetObject();
    CSolar* const Solar =
        (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            ? static_cast<CSolar*>(Obj)
            : nullptr;
    if (Solar->is_lane_ring())
        TradeLaneRingSideCommand(this, ShipId, bActivate);
    if (!SinglePlayer())
        Solar->activate(bActivate, ShipId);
}

unsigned char TradeLaneSolarLaneSwitch(FClientGameobj* SolarGo, unsigned char bShipLeft)
{
    // FUN_00516910. The first equip-FX wrapper (EquippedObj +0xb8 list) whose equipment is a
    // CETradeLaneEquip. No wrapper leaves a null that retail then reads through, as here.
    FEquipFxListNode* const Head =
        static_cast<FEquipFxListNode*>(static_cast<FClientEquippedObj*>(SolarGo)->EquipFx.Head);
    FClientEquip* Wrapper = nullptr;
    for (FEquipFxListNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (CETradeLaneEquip::cast(Node->Wrapper->Equip) != nullptr)
        {
            Wrapper = Node->Wrapper;
            break;
        }
    }

    // The count of ships using the lane.
    int* const ShipCount = CliTradeLaneEquipFxShipCount(Wrapper);
    if (bShipLeft != 0)
        *ShipCount = *ShipCount - 1;
    else
        *ShipCount = *ShipCount + 1;

    // Ships still on it: the host's effect list is cleared of the effects whose arch holds 0x28;
    // none left: the lane one-shots again (FUN_00516d10).
    if (*ShipCount != 0)
        CliEffectListClear(&Wrapper->Host->EffectList,
                           reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x28)));
    else
        CliTradeLaneEquipFxAttachLaneEffects(Wrapper);

    // For the side-1 node, then the side-0 node, of this ring (looked up,
    // never created): +0x1d = "no ship is using the lane"; while ships use it, the node's
    // tl_ring_active effect is stopped and released when still alive (the FUN_00503330 test
    // inline, then the FUN_00515640 body inline).
    unsigned char const bNoShips = (*ShipCount == 0) ? 1u : 0u;
    unsigned char const Sides[2] = {1, 0};
    for (int Index = 0; Index < 2; ++Index)
    {
        FTradeLaneRingRuntime* const Ring = TradeLaneRingFindOrCreate(SolarGo, Sides[Index], false);
        if (Ring == nullptr)
            continue;
        Ring->bActive = bNoShips;
        if (bNoShips != 0)
            continue;
        if (CreateEffectCompanionAlive(&Ring->EffectCompanion) != 0)
            TlCompanionStopAndClear(&Ring->EffectCompanion);
    }
    return bNoShips;
}

void TradeLaneSPObjActivate(unsigned ObjectId, bool bActivate, unsigned ShipId)
{
    // FUN_0053d940.
    FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
    if (Go == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kIClientSCpp, 0x5f0,
              "SPObjActivate failed... probably jump-gate in old system closing?\n");
        return;
    }
    if (Go->ManagerKind == EClientGameobjKind::Solar)
    {
        Go->TradeLaneSolarActivate(bActivate, ShipId);
        return;
    }
    if (Go->ManagerKind == EClientGameobjKind::Ship)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kIClientSCpp, 0x5e8,
              "Combat mode no longer supported! (Ignorable)");
    }
}

void TradeLaneIClientGoTradelane(XGoTradelane const* Info)
{
    // LAB_0053c830 (IClient slot 6).
    FBehaviorTradelaneEvent Block;
    Block.bEnteringLane = 1;
    Block.bExitRequested = 0;
    Block.bFastExit = static_cast<unsigned char>(Info->bMidLane);
    Block.Pad03 = 0;
    Block.Speed = 0.0f;
    Block.Field_0x08 = 0;
    Block.ShipId = Info->ObjectId;
    Block.EntranceId = Info->TradelaneRing1;
    Block.ExitId = Info->TradelaneRing2;
    TlNotifyPlayerBehavior(&Block);

    FClientGameobj* const Ship = ClientObjDb_FindObject(Info->ObjectId, static_cast<int>(EClientGameobjKind::Ship));
    if (Ship != nullptr)
    {
        CShip* const ShipObj = TlNarrowShip(Ship);
        if (ShipObj != nullptr)
            ShipObj->use_tradelane(true);
    }
    FClientGameobj* const Ring1 = ClientObjDb_FindObject(Info->TradelaneRing1, static_cast<int>(EClientGameobjKind::Solar));
    FClientGameobj* const Ring2 = ClientObjDb_FindObject(Info->TradelaneRing2, static_cast<int>(EClientGameobjKind::Solar));
    if (Ship != nullptr && Ring1 != nullptr && Ring2 != nullptr)
        TradeLaneStartCommand(Ring1, Ring2, Ship, static_cast<unsigned char>(Info->bMidLane));
}

void TradeLaneIClientStopTradelane(unsigned ShipId, unsigned EntranceId)
{
    // LAB_0053c760 (IClient slot 7).
    FBehaviorTradelaneEvent Block;
    Block.bEnteringLane = 0;
    Block.bExitRequested = 0;
    Block.bFastExit = 0;
    Block.Pad03 = 0;
    Block.Speed = 0.0f;
    Block.Field_0x08 = 0;
    Block.ShipId = ShipId;
    Block.EntranceId = EntranceId;
    Block.ExitId = 0;
    TlNotifyPlayerBehavior(&Block);

    FClientGameobj* const Ship = ClientObjDb_FindObject(ShipId, static_cast<int>(EClientGameobjKind::Ship));
    if (Ship == nullptr)
        return;
    CShip* const ShipObj = TlNarrowShip(Ship);
    if (ShipObj != nullptr)
        ShipObj->use_tradelane(false);
    TradeLaneShipStop(Ship);
}
