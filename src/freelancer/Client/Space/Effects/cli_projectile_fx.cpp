// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/Effects/cli_projectile_fx.h"

// Before anything that could instantiate std::vector<unsigned short>: Common.dll exports that
// instantiation (extern template in common_fuses.h); CliShipFireWeapons' shot-id vector must
// import it, not define its members here (LNK2005 against Common.lib otherwise).
#include "common_fuses.h"

#include "Space/client_gameobj.h"
#include "Space/client_objdb.h"
#include "Space/Effects/effect_base.h"          // EffectAlloc (FUN_004f79a0)
#include "Space/Effects/effect_vertex_batch.h"
#include "Common/engine_light.h"
#include "Core/globals.h"
#include "Space/HUD/hud_command_dispatch.h"
#include "Common/Audio/menu_sound_arch.h"
#include "NeuroNet/nn_messaging.h"         // NnVoiceEnqueue (FUN_00489e80) -- launcher-family notifies
#include "Space/Effects/particle_effect.h"
#include "Space/player_behavior.h"
#include "Space/player_spawn.h"
#include "Network/server_connect.h"
#include "Space/Cameras/space_camera.h"
#include "Space/star_sys.h"             // StarSysNoteTargetChange (FUN_005532d0) -- launcher fire
#include "Interface/Pages/character_select_equip.h" // FUN_004c45d0 / FUN_004c4320 -- launcher ammo
#include "Common/Graphics/vis_effect_arch.h"

#include "DALib.h"
#include "dacom.h"
#include "common_archetype.h"
#include "common_csys.h"
#include "common_iface.h"
#include "common_misc.h"
#include "common_animdb.h"
#include "common_server.h"        // IServer (DAT_0067ecd0) -- CliShipActivateEquip
#include "common_server_types.h"
#include "engine/engbase.h"
#include "soundmanager.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace
{
    constexpr float kProgressLengthPad = 7.5f; // DAT_005d9e0c
    constexpr float kProgressFallback = 50.0f; // DAT_005d440c

    // Each retail translation unit keeps its own lazily filled I3DMathEngine cache.
    // CliBeamActivateEffects reads IClientS.cpp's, DAT_00678a28 (no other reference);
    // CliExplosionAttachLocalXform shares the game light unit's DAT_006789c0 (G_GameLightMath,
    // engine_light.h).
    I3DMathEngine* G_BeamActivateMath = nullptr; // DAT_00678a28

    // .rdata 0x005dd484, the FDUMP file argument.
    char const kCliLauncherCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\CliLauncher.cpp";

    void MatrixStore9(FMatrix* Dst, float A0, float A1, float A2, float A3, float A4, float A5,
                      float A6, float A7, float A8)
    {
        Dst->D[0][0] = A0;
        Dst->D[0][1] = A1;
        Dst->D[0][2] = A2;
        Dst->D[1][0] = A3;
        Dst->D[1][1] = A4;
        Dst->D[1][2] = A5;
        Dst->D[2][0] = A6;
        Dst->D[2][1] = A7;
        Dst->D[2][2] = A8;
    }

    void MatrixSetIdentity(FMatrix* Out)
    {
        std::memset(Out, 0, sizeof(FMatrix));
        Out->D[0][0] = 1.0f;
        Out->D[1][1] = 1.0f;
        Out->D[2][2] = 1.0f;
    }

    void MatrixCopyFromCommon(FMatrix* Dst, Matrix const& Src)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Dst->D[Row][Col] = Src.E[Row][Col];
        }
    }

    // The object narrowed by (ObjectClass & Mask) == Mask, null otherwise -- the test every
    // Beam function makes on +0x10 before using it.
    CObject* NarrowBeamObject(FClientBeamGameobj const* Beam, unsigned Mask)
    {
        CObject* const Obj = Beam->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & Mask) != Mask)
            return nullptr;
        return Obj;
    }

    void CopyOwnerOrientation(FMatrix* Out, CObject* OwnerObj)
    {
        // Nine floats from owner+8 (the EngineObject orientation block).
        float const* Src = reinterpret_cast<float const*>(reinterpret_cast<unsigned char*>(OwnerObj) + 8);
        std::memcpy(Out, Src, sizeof(FMatrix));
    }

    float OwnerVelocityLength(CObject* OwnerObj)
    {
        // Retail: vtable+0x54 GetVelocity.
        Vector Vel = OwnerObj->get_velocity();
        return sqrtf(Vel.x * Vel.x + Vel.y * Vel.y + Vel.z * Vel.z);
    }

    // Effect-list node: next/prev + FUN_004fd480 payload (4 dwords = slots +0xC0..+0xCC).
    struct FEffectListNode
    {
        FEffectListNode* Next; // +0x00
        FEffectListNode* Prev; // +0x04
        void* EffectArch;           // +0x08
        void* SlotOther;       // +0x0C
        FEffectBase* Effect;   // +0x10
        void* SlotExplosion;   // +0x14
    };
    static_assert(sizeof(FEffectListNode) == 0x18, "effect list node 0x18");
}

void BeamEffectSetProgressFromSpeed(FBeamEffect* This, float Speed)
{
    FBeamEffectArch* Arch = This->Arch;
    if (Arch != nullptr)
    {
        This->ProgressDenom80 =
            (Arch->RadiusMid + Arch->Radius1 + kProgressLengthPad) / Speed;
        return;
    }
    This->ProgressDenom80 = kProgressFallback / Speed;
}

void BeamEffectSetHitFlags(FBeamEffect* This, bool bHit, bool bHitPlayer)
{
    // FUN_004fc8f0: ((arg1 << 1) ^ f) & 2 ^ f, then ((arg2 << 2) ^ that) & 4 ^ that.
    unsigned char Flags = This->Flags70;
    Flags = static_cast<unsigned char>(((((bHit ? 1u : 0u) << 1) ^ Flags) & 2u) ^ Flags);
    Flags = static_cast<unsigned char>(((((bHitPlayer ? 1u : 0u) << 2) ^ Flags) & 4u) ^ Flags);
    This->Flags70 = Flags;
}

void CliEffectCompanionInit(void* Companion16, void* Arch, FEffectBase* Particle,
                            ISoundSource* Sound, FGameLight* Light)
{
    // FUN_005154a0: +0x00 = Arch, +0x04 = Sound (third argument), +0x08 = Particle (second),
    // +0x0c = Light; then AddRef each non-null one.
    void** const Slots = static_cast<void**>(Companion16);
    Slots[1] = Sound;
    Slots[0] = Arch;
    Slots[2] = Particle;
    Slots[3] = Light;
    if (Particle != nullptr)
        EffectAddRef(Particle);
    if (Sound != nullptr)
        Sound->AddRef();
    if (Light != nullptr)
        GameLightAddRef(Light);
}

void CliEffectListCopyPayload(void* DstSlots, void const* SrcSlots)
{
    // FUN_004fd480: the companion copy constructor run as a placement new. Only the destination
    // is tested (the placement-new null test); the source is read unconditionally.
    if (DstSlots == nullptr)
        return;

    void** Dst = static_cast<void**>(DstSlots);
    void* const* Src = static_cast<void* const*>(SrcSlots);
    Dst[0] = Src[0];
    Dst[1] = Src[1];
    Dst[2] = Src[2];
    Dst[3] = Src[3];

    if (Dst[2] != nullptr)
    {
        EffectAddRef(static_cast<FEffectBase*>(Dst[2]));
    }
    if (Dst[1] != nullptr)
    {
        // The sound's IDAComponent::AddRef; no test of the vtable.
        static_cast<ISoundSource*>(Dst[1])->AddRef();
    }
    if (Dst[3] != nullptr)
        GameLightAddRef(static_cast<FGameLight*>(Dst[3]));
}

void GameLightSetFreeMotionOffset(FGameLight* Light, float const* Offset3)
{
    // FUN_005377a0: Light+0x120, the free-motion velocity FUN_00537860 integrates. No null
    // tests.
    Light->VelocityX = Offset3[0];
    Light->VelocityY = Offset3[1];
    Light->VelocityZ = Offset3[2];
}

void CliEffectListInsert(void* ListOwnerPlus28, void const* Payload16)
{
    // FUN_004fcea0, on the list base (+0 allocator byte, +4 head, +8 count). No null tests: the
    // owner's constructor allocates the head (FGameobjEffectListStorage).
    unsigned char* ListBase = static_cast<unsigned char*>(ListOwnerPlus28);
    int& CountRef = *reinterpret_cast<int*>(ListBase + 8);

    FEffectListNode* Head = *reinterpret_cast<FEffectListNode**>(ListBase + 4);
    FEffectListNode* OldPrev = Head->Prev;
    FEffectListNode* Node =
        static_cast<FEffectListNode*>(::operator new(sizeof(FEffectListNode)));
    Node->Next = Head;
    if (OldPrev == nullptr)
        OldPrev = Node;
    Node->Prev = OldPrev;
    Head->Prev = Node;
    Node->Prev->Next = Node;

    CliEffectListCopyPayload(&Node->EffectArch, Payload16);
    CountRef = CountRef + 1;
}

void CliEffectCompanionClear(void* Companion16)
{
    // FUN_00502d90. No test of the companion pointer; every caller passes an embedded member.
    void** const Slots = static_cast<void**>(Companion16);
    if (Slots[2] != nullptr)
    {
        EffectRelease(static_cast<FEffectBase*>(Slots[2]));
        Slots[2] = nullptr;
    }
    if (Slots[1] != nullptr)
    {
        ISoundSource* const Sound = static_cast<ISoundSource*>(Slots[1]);
        Sound->Release();
        Slots[1] = nullptr;
    }
    if (Slots[3] != nullptr)
    {
        GameLightRelease(static_cast<FGameLight*>(Slots[3]));
        Slots[3] = nullptr;
    }
    Slots[0] = nullptr;
}

void CliEffectCompanionArmStop(void* Companion16)
{
    // Prelude shared by FUN_004fcef0 / FUN_005139d0 / FUN_005137c0 before clear; the
    // JumpShipEffect dtor FUN_00503a80 inlines it three times. No copy tests the companion
    // pointer itself.
    void** const Slots = static_cast<void**>(Companion16);
    // The effect's own slot 1 (dispatched): the slot holds a particle, a beam or a
    // CliEquipLight, whose slot 1 is FUN_004f7bd0 (EffectKill).
    if (Slots[2] != nullptr)
    {
        typedef void(__thiscall * FEffectSlot1)(void*);
        ((FEffectBase*)Slots[2])->Kill();
    }
    if (Slots[1] != nullptr)
    {
        ISoundSource* const Sound = static_cast<ISoundSource*>(Slots[1]);
        float Ident[12];
        Ident[0] = 1.0f;
        Ident[1] = 0.0f;
        Ident[2] = 0.0f;
        Ident[3] = 0.0f;
        Ident[4] = 1.0f;
        Ident[5] = 0.0f;
        Ident[6] = 0.0f;
        Ident[7] = 0.0f;
        Ident[8] = 1.0f;
        Ident[9] = 0.0f;
        Ident[10] = 0.0f;
        Ident[11] = 0.0f;
        Sound->SetPose(-1, Ident);
        ((ISoundSource*)Slots[1])->Stop();
    }
    // FUN_005379b0, then the light's own vtable slot 2 -- FUN_005376f0 on a GameLight and on the
    // CliEquipLight table PTR_FUN_005da424 alike, but dispatched.
    if (Slots[3] != nullptr)
    {
        FGameLight* const Light = static_cast<FGameLight*>(Slots[3]);
        GameLightAnimDetach(Light);
        GameLightVirtualDisable((FGameLight*)Slots[3]);
    }
}

unsigned char CliEffectListRemove(void* ListOwnerPlus28, void* Payload16)
{
    // FUN_004fcef0 (list base, companion). No test of the list, its head or the companion. The
    // node is not unlinked.
    unsigned char* const ListBase = static_cast<unsigned char*>(ListOwnerPlus28);
    FEffectListNode* const Head = *reinterpret_cast<FEffectListNode**>(ListBase + 4);

    void** const Want = static_cast<void**>(Payload16);
    for (FEffectListNode* Cursor = Head->Next; Cursor != Head; Cursor = Cursor->Next)
    {
        if (Cursor->Effect != Want[2] || Cursor->SlotOther != Want[1] ||
            Cursor->SlotExplosion != Want[3])
            continue;

        // Clear node-owned copies first.
        void* NodeSlots[4] = {Cursor->EffectArch, Cursor->SlotOther, Cursor->Effect,
                              Cursor->SlotExplosion};
        if (NodeSlots[2] != nullptr)
        {
            EffectRelease(static_cast<FEffectBase*>(NodeSlots[2]));
            Cursor->Effect = nullptr;
        }
        if (NodeSlots[1] != nullptr)
        {
            static_cast<ISoundSource*>(NodeSlots[1])->Release();
            Cursor->SlotOther = nullptr;
        }
        if (NodeSlots[3] != nullptr)
        {
            GameLightRelease(static_cast<FGameLight*>(NodeSlots[3]));
            Cursor->SlotExplosion = nullptr;
        }
        Cursor->EffectArch = nullptr;

        CliEffectCompanionArmStop(Payload16);
        CliEffectCompanionClear(Payload16);
        return 1;
    }
    return 0;
}

void CliEffectListClear(void* ListOwnerPlus28, void* ArchFilter)
{
    // FUN_004fd0d0. No null tests: the owner's ctor allocates the head.
    unsigned char* const ListBase = static_cast<unsigned char*>(ListOwnerPlus28);
    FEffectListNode* const Head = *reinterpret_cast<FEffectListNode**>(ListBase + 4);
    int& CountRef = *reinterpret_cast<int*>(ListBase + 8);

    // Pass 1. With a filter, a node whose slot 0 is null or does not hold the filter in its
    // first dword is skipped. Every other node is stopped and released (both sequences inlined
    // in retail).
    for (FEffectListNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (ArchFilter != nullptr)
        {
            if (Node->EffectArch == nullptr || *static_cast<void**>(Node->EffectArch) != ArchFilter)
                continue;
        }
        CliEffectCompanionArmStop(&Node->EffectArch);
        CliEffectCompanionClear(&Node->EffectArch);
    }

    // Pass 2: erase every node, filtered or not (the clear is a no-op for those pass 1 cleared).
    FEffectListNode* Node = Head->Next;
    while (Node != Head)
    {
        FEffectListNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        CliEffectCompanionClear(&Node->EffectArch);
        ::operator delete(Node);
        CountRef = CountRef - 1;
        Node = Next;
    }
}

namespace
{
    // FUN_004fd420: a new 0x18 node linked in before Where, the payload copied in with its
    // references taken, ++count. A node with a null Prev links to itself.
    FEffectListNode* EffectListInsertBefore(void* ListBase, FEffectListNode* Where,
                                            void const* Payload16)
    {
        int& CountRef = *reinterpret_cast<int*>(static_cast<unsigned char*>(ListBase) + 8);
        FEffectListNode* const Node =
            static_cast<FEffectListNode*>(::operator new(sizeof(FEffectListNode)));
        Node->Next = Where;
        FEffectListNode* Prev = Where->Prev;
        if (Prev == nullptr)
            Prev = Node;
        Node->Prev = Prev;
        Where->Prev = Node;
        Node->Prev->Next = Node;
        CliEffectListCopyPayload(&Node->EffectArch, Payload16);
        CountRef = CountRef + 1;
        return Node;
    }

    // FUN_004f7d80 (on the particle): point the particle's owner watcher (+0x44) at the
    // new owner's Watchable (Gameobj+8, null stays null). The compare-then-set_pointer is
    // BaseWatcher::set.
    void ParticleEffectSetOwner(FEffectBase* Particle, FClientGameobj* Owner)
    {
        Watchable const* const Target =
            (Owner != nullptr) ? static_cast<Watchable const*>(static_cast<IObjRW*>(Owner))
                               : nullptr;
        Particle->Watcher.set(Target);
    }
}

void CliEffectListDetachPart(void* ListOwnerPlus28, long PartIndex, FClientGameobj* NewOwner)
{
    // FUN_004fd230 (on the list). Every effect node attached to PartIndex or
    // one of its descendants leaves this list: with NewOwner, one that is still alive moves to
    // the end of NewOwner's list; everything else is stopped and released.
    unsigned char* const ListBase = static_cast<unsigned char*>(ListOwnerPlus28);
    FEffectListNode* const* const HeadSlot = reinterpret_cast<FEffectListNode**>(ListBase + 4);
    int& CountRef = *reinterpret_cast<int*>(ListBase + 8);

    FEffectListNode* Next = (*HeadSlot)->Next;
    while (Next != *HeadSlot)
    {
        FEffectListNode* const Node = Next;
        Next = Node->Next;
        void** const Companion = reinterpret_cast<void**>(&Node->EffectArch);
        FEffectBase* const Particle = static_cast<FEffectBase*>(Companion[2]);
        ISoundSource* const Sound = static_cast<ISoundSource*>(Companion[1]);

        // The node's engine instance: the particle's (+0x40), else the sound's attach handle
        // (slot 39), else, for a light-only node, the dword at absolute address 0x40 (retail
        // faults there, 0x004fd27f). A node with nothing, or with instance -1, stays.
        long InstIndex = 0;
        if (Particle != nullptr)
            InstIndex = Particle->EngineIndex;
        else if (Sound != nullptr)
            InstIndex = Sound->GetAttachHandle();
        else if (Companion[3] != nullptr)
            InstIndex = *reinterpret_cast<long volatile*>(static_cast<uintptr_t>(0x40));
        else
            continue;
        if (InstIndex == -1)
            continue;

        if (!IsDescendant(PartIndex, InstIndex))
            continue;

        bool bMoved = false;
        if (NewOwner != nullptr)
        {
            // Still alive: a particle without the dead bit (Flags08 & 2), else an active sound,
            // else a light with no animation keys (+0xd4).
            bool bAlive = false;
            if (Particle != nullptr && (Particle->Flags08 & 2u) == 0u)
                bAlive = true;
            else if (Sound != nullptr && Sound->IsActive() != 0)
                bAlive = true;
            else if (Companion[3] != nullptr &&
                     static_cast<FGameLight*>(Companion[3])->Anim.Keys == nullptr)
                bAlive = true;

            if (bAlive)
            {
                // At the end of NewOwner's list, then the particle's owner watcher.
                EffectListInsertBefore(&NewOwner->EffectList,
                                       static_cast<FEffectListNode*>(NewOwner->EffectList.Head),
                                       Companion);
                if (Particle != nullptr)
                    ParticleEffectSetOwner(Particle, NewOwner);
                bMoved = true;
            }
        }

        if (!bMoved)
        {
            CliEffectCompanionArmStop(Companion);
            CliEffectCompanionClear(Companion);
        }

        // Unlink; the clear drops this node's references after a move.
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        CliEffectCompanionClear(Companion);
        ::operator delete(Node);
        CountRef = CountRef - 1;
    }
}

void CliExplosionAttachLocalXform(void* ExplosionFx, int EngineIndex, FTransform const* LocalXform)
{
    // FUN_005377c0 (on the game light). No null test inside: all eight retail call sites test
    // the light first.
    unsigned char* const Base = static_cast<unsigned char*>(ExplosionFx);

    // +0xec = the engine index, +0xf0 = the local transform.
    *reinterpret_cast<int*>(Base + 0xec) = EngineIndex;
    std::memcpy(Base + 0xf0, LocalXform, sizeof(FTransform));

    float* const InstRaw =
        DALib::Engine->GetInstanceTransform(reinterpret_cast<void*>(static_cast<intptr_t>(EngineIndex)));

    // DAT_006789c0.
    if (G_GameLightMath == nullptr)
        G_GameLightMath = Acquire3DMathEngine();

    FTransform World;
    G_GameLightMath->MulTransform(World, *reinterpret_cast<FTransform const*>(InstRaw),
                                  *reinterpret_cast<FTransform const*>(Base + 0xf0));

    // Copied to a second local first, as retail does (FUN_00410fe0).
    FTransform const Copy = World;
    EngineLightSetTransform(static_cast<FEngineLight*>(ExplosionFx), &Copy.Rotation.D[0][0],
                            &Copy.Translation.X);

    // The free-motion velocity +0x120..+0x128 = 0.
    *reinterpret_cast<float*>(Base + 0x120) = 0.0f;
    *reinterpret_cast<float*>(Base + 0x124) = 0.0f;
    *reinterpret_cast<float*>(Base + 0x128) = 0.0f;
}

void CliBeamActivateEffects(FClientBeamGameobj* This, Vector const* LookDir,
                            unsigned char bUseLookDir)
{
    // FUN_0053f340 (on the Beam gameobj). The class & 7 object's archetype companion (no null
    // test on the object); a null companion goes straight to Projectile's slot 92.
    Archetype::ICliObj* const Companion = NarrowBeamObject(This, 0x7u)->Archetype->Companion;
    if (Companion != nullptr)
    {
        FTransform LocalXform;
        if (bUseLookDir == 0)
        {
            MatrixSetIdentity(&LocalXform.Rotation);
        }
        else
        {
            // transpose(CBeam orientation, class & 0x67, no null test) x LookMatrix(*LookDir).
            Matrix const Look = LookMatrix(*LookDir);
            FMatrix OwnerOri;
            CopyOwnerOrientation(&OwnerOri, NarrowBeamObject(This, 0x67u));
            FMatrix OwnerT;
            MatrixStore9(&OwnerT, OwnerOri.D[0][0], OwnerOri.D[1][0], OwnerOri.D[2][0],
                         OwnerOri.D[0][1], OwnerOri.D[1][1], OwnerOri.D[2][1], OwnerOri.D[0][2],
                         OwnerOri.D[1][2], OwnerOri.D[2][2]);
            FMatrix LookM;
            MatrixCopyFromCommon(&LookM, Look);
            if (G_BeamActivateMath == nullptr)
                G_BeamActivateMath = Acquire3DMathEngine();
            FMatrix Combined;
            G_BeamActivateMath->MulMatrix(Combined, OwnerT, LookM);
            LocalXform.Rotation = Combined;
        }
        LocalXform.Translation.X = 0.0f;
        LocalXform.Translation.Y = 0.0f;
        LocalXform.Translation.Z = 0.0f;

        // The companion's +0x58 const_effect; empty -> slot 92 only.
        ID_String const* const ConstEffect = Archetype::CliProjConstEffect(Companion);
        if (!ConstEffect->IsEmpty())
        {
            // No null test on the found arch.
            FEffectArch* const EffectArch = FindEffectArch(&ConstEffect->Id);
            if (EffectArch->VisBeam != nullptr && EffectArch->VisBeam->Nickname[0] != '\0')
            {
                void* const Mem = EffectAlloc(0x84u);
                FBeamEffect* Beam = nullptr;
                if (Mem != nullptr)
                {
                    Beam = static_cast<FBeamEffect*>(Mem);
                    BeamEffectConstruct(Beam, 0);
                }
                This->BeamAlloc = Beam;
                // Flags70 bit 3 = bUseLookDir; no null test on the allocation.
                Beam->Flags70 = static_cast<unsigned char>(
                    (((bUseLookDir << 3) ^ Beam->Flags70) & 8) ^ Beam->Flags70);
                This->Companion[2] = This->BeamAlloc;
                BeamEffectAddRef(This->BeamAlloc);
            }

            int const EngineIndex = static_cast<int>(This->get_index());
            FBeamEffect* const LiveBeam = static_cast<FBeamEffect*>(This->Companion[2]);
            if (LiveBeam != nullptr)
                BeamEffectAttach(LiveBeam, This, EngineIndex, &LocalXform);
            ISoundSource* const Sound = static_cast<ISoundSource*>(This->Companion[1]);
            if (Sound != nullptr)
                Sound->SetPose(EngineIndex, reinterpret_cast<float const*>(&LocalXform));
            if (This->Companion[3] != nullptr)
                CliExplosionAttachLocalXform(This->Companion[3], EngineIndex, &LocalXform);

            // Start the beam; on success its progress from the CBeam's speed (class & 0x67, no
            // null test); on failure release it and clear Companion[2].
            if (This->Companion[2] != nullptr)
            {
                FBeamEffect* const StartBeam = static_cast<FBeamEffect*>(This->Companion[2]);
                if (BeamEffectStart(StartBeam, EffectArch) != 0)
                {
                    float const Speed = OwnerVelocityLength(NarrowBeamObject(This, 0x67u));
                    BeamEffectSetProgressFromSpeed(This->BeamAlloc, Speed);
                }
                else
                {
                    BeamEffectRelease(StartBeam);
                    This->Companion[2] = nullptr;
                }
            }

            CliEffectListInsert(&This->EffectList, This->Companion);
        }
    }

    // Projectile's slot 92 FUN_005426e0, called directly.
    This->FClientProjectileGameobj::PostCreateInit();
}

void CliLauncherFireProjectiles(FCliLauncherFx* Launcher, Vector const* Target)
{
    // FUN_0052dde0. No null test on the launcher equipment.
    CELauncher* const Launch = Launcher->LaunchEquip();
    std::vector<ProjLaunchInfo> Infos;
    Launch->ComputeLaunchInfo(Infos, *Target);
    unsigned const BarrelCount = static_cast<unsigned>(Infos.size());

    for (unsigned Barrel = 0; Barrel < BarrelCount; ++Barrel)
    {
        ProjLaunchInfo const& Info = Infos[Barrel];
        // operator new(0xd4).
        FClientBeamGameobj* const Beam = new FClientBeamGameobj();

        Archetype::Equipment* const ProjArch = Launch->Projectile;
        ProjArch->load();
        Beam->create(ProjArch);
        ProjArch->Release();

        // The CBeam (class & 0x67) is initialised with the barrel's position, orientation and
        // velocity and the owner's CSimple id.
        CProjectile* const BeamObj = static_cast<CProjectile*>(NarrowBeamObject(Beam, 0x67u));
        CProjectile::CreateParms Parms;
        Parms.Id = 0;
        Parms.ObjDb = GetClientObjDb();
        Parms.OwnerPlayer = GetPlayerConnectionId();
        Parms.Pos = Info.Pos;
        Parms.Orient = Info.Orient;
        Parms.Velocity = Info.Velocity;
        Parms.OwnerId = static_cast<CSimple*>(Launch->Owner)->Id;
        BeamObj->init(Parms);

        // The player's own launcher looks along ComputeProjectilePosAndVelocity_OneBarrel's
        // velocity; any other along the launch velocity.
        Vector LookDir = Info.Velocity;
        unsigned char bUseLook = 0;
        if (static_cast<void*>(Launcher->Host) == GetPlayerObjRW())
        {
            Vector Pos;
            bUseLook = 1;
            Launch->ComputeProjectilePosAndVelocity_OneBarrel(Pos, LookDir,
                                                              static_cast<int>(Barrel), *Target);
        }

        CliBeamActivateEffects(Beam, &LookDir, bUseLook);

        ProjArch->Companion->Load(1);
        ClientObjDb_RegisterGameobj(Beam, false);
    }
}

unsigned char FCliLauncherFx::TryFire(Vector const* Target)
{
    // FUN_0052d410 -- slot 3 of the launcher / mine / CM tables; the gun's slot 3 calls it
    // directly. No test of the equipment.
    CELauncher* const Launch = LaunchEquip();
    FireResult const Result = Launch->Fire(*Target);
    if (Result == FireOk)
    {
        CliLauncherPlayMuzzleFx(this);
        Launch->ConsumeFireResources();
        // The player's ship firing a munition that requires ammo takes the per-fire rounds out
        // of its inventory item (count > per-fire is a signed compare).
        if (IsPlayerShipId(Host->get_id()))
        {
            Archetype::Projectile const* const Munition =
                static_cast<Archetype::Projectile const*>(Launch->Projectile);
            if (Munition->bRequiresAmmo)
            {
                EquipDesc* const Item = FindPlayerNonMissionEquipRetail(Munition->get_id());
                if (Item != nullptr)
                {
                    int const PerFire = static_cast<int>(Launch->GetProjectilesPerFire());
                    unsigned char const bMoreThanOneShot = (Item->get_count() > PerFire) ? 1 : 0;
                    RemovePlayerEquipRetail(bMoreThanOneShot, Item->get_id(), PerFire);
                }
            }
        }
        StarSysNoteTargetChange(Host->get_id());
        return 1;
    }
    if (Result == FireNoAmmo)
    {
        // The player's own launcher seen from the first-person camera plays its dry-fire sound.
        if (static_cast<void*>(static_cast<IObjRW*>(Host)) == GetPlayerObjRW() &&
            IsFirstPersonCameraCurrent())
        {
            PlayClientSoundArch(DryFireSoundArch, nullptr, nullptr, 0);
        }
        return 0;
    }
    if (Result > FireMuzzleCone && Result <= FireCloakActive)
    {
        // Cruise / tradelane / cloak-active (6..8) on the player's own host -> its slot 0x1d0
        // (the ship's LAB_00545cc0).
        if (static_cast<void*>(static_cast<IObjRW*>(Host)) == GetPlayerObjRW())
            static_cast<FClientShipGameobj*>(Host)->PlayFireFailedSound();
    }
    return 0;
}

unsigned char FCliGunFx::TryFire(Vector const* Target)
{
    // FUN_0052dd00 -- slot 3 of the gun table PTR_LAB_005dd5d8.
    unsigned char const bFired = FCliLauncherFx::TryFire(Target);
    if (bFired != 0)
    {
        // The Seeker is read without a null test on the munition; unguided -> launch.
        CEGun const* const Gun = static_cast<CEGun const*>(Equip);
        if (Gun->MunitionArch()->Seeker == 0)
            CliLauncherFireProjectiles(this, Target);
    }
    return bFired;
}

void FCliLauncherFx::FireDirect(Vector const* Target)
{
    // FUN_0052d510 -- slot 5 of the launcher / mine / CM tables. Target is not read.
    (void)Target;
    if (Equip == nullptr)
        return;
    // Resources are consumed only outside single player.
    CliLauncherPlayMuzzleFx(this);
    if (!SinglePlayer())
        LaunchEquip()->ConsumeFireResources();
    StarSysNoteTargetChange(Host->get_id());
}

void FCliGunFx::FireDirect(Vector const* Target)
{
    // FUN_0052dd40 -- slot 5 of the gun table; FUN_0052d510's body inlined.
    if (Equip == nullptr)
        return;
    CliLauncherPlayMuzzleFx(this);
    if (!SinglePlayer())
        LaunchEquip()->ConsumeFireResources();
    StarSysNoteTargetChange(Host->get_id());

    // The host object's owner (class & 3: CSimple +0xb4, else DAT_00673344); a non-zero one
    // sets the gun's target point.
    unsigned Owner = 0;
    CObject* const Object = Host->Object;
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 3u) == 3u)
        Owner = static_cast<CSimple*>(Object)->OwnerPlayer;
    else
        Owner = GetPlayerConnectionId();
    CEGun* const Gun = static_cast<CEGun*>(Equip);
    if (Owner != 0)
        Gun->SetTargetPoint(*Target);
    if (Gun->MunitionArch()->Seeker == 0)
        CliLauncherFireProjectiles(this, Target);
}

unsigned char FCliLauncherFx::FireBarrelAim()
{
    // FUN_0052d280 -- slot 4 of every table.
    CELauncher* const Launch = LaunchEquip();
    Vector const Pos = Launch->GetAvgBarrelPosWS();
    Vector Dir = Launch->GetAvgBarrelDirWS();
    // 1e10 (DAT_005dd55c) over the length in double, no zero test.
    double const Length = std::sqrt((static_cast<double>(Dir.y) * Dir.y +
                                     static_cast<double>(Dir.z) * Dir.z) +
                                    static_cast<double>(Dir.x) * Dir.x);
    double const Scale = 1.0e10 / Length;
    Dir.x = static_cast<float>(Dir.x * Scale);
    Dir.y = static_cast<float>(Dir.y * Scale);
    Dir.z = static_cast<float>(Dir.z * Scale);
    Vector Aim;
    Aim.x = Pos.x + Dir.x;
    Aim.y = Pos.y + Dir.y;
    Aim.z = Pos.z + Dir.z;
    // Slot 3 (virtual) on it.
    unsigned char const bFired = TryFire(&Aim);
    if (bFired != 0)
    {
        // One shot record with this equipment's id to the server.
        XFireWeaponInfo Info;
        Info.ObjectId = Host->get_id();
        Info.Hardpoints.insert(Info.Hardpoints.end(), 1u, Equip->GetID());
        Info.TargetPos = Aim;
        static_cast<IServer*>(GetLocalServerInterface())
            ->FireWeapon(GetPlayerConnectionId(), Info);
    }
    return bFired;
}

ID_String FCliLauncherFx::OneShotSoundId() const
{
    // FUN_0052cf80.
    ID_String Cleared;
    Cleared.clear();
    ID_String Result = Cleared;
    // With a one-shot sound, its lod link's nickname id (slot 12); else the cleared one.
    if (OneShotSoundArch != nullptr)
    {
        void* const Linked = GetClientSoundArchLinked(OneShotSoundArch);
        if (Linked != nullptr)
            Result.Id = GetClientSoundArchId(Linked);
    }
    return Result;
}

void FCliLauncherFx::PlayFireSound(Vector const* Pos, int ShotCount)
{
    // FUN_0052ced0. Nothing without a one-shot sound (+0x18).
    if (OneShotSoundArch == nullptr)
        return;
    // The fire voice (a Source3D) is made on first use, not tested afterwards.
    if (FireVoice == nullptr)
        FireVoice = static_cast<ISoundSource*>(CreateClientSoundSource3D());
    FireVoice->SetPosition(const_cast<Vector*>(Pos));
    // Integer attenuation -(arch attenuation * (ShotCount - 1)).
    Archetype::ISound* const Arch = static_cast<Archetype::ISound*>(OneShotSoundArch);
    int const Attenuation = -(Arch->GetAttenuation() * (ShotCount - 1));
    FireVoice->SetAttenuation(static_cast<float>(Attenuation));
    FireVoice->SetPitch(Arch->GetRandomPitch());
    // An idle voice starts, a playing one is rewound.
    if (!FireVoice->IsActive())
        FireVoice->Start(OneShotSoundArch, 0);
    else
        FireVoice->SetStartOffset(0);
}

namespace
{
    CEqObj* ResolveEqObjFromHost(FClientEquippedObj* Host)
    {
        if (Host == nullptr || Host->Object == nullptr)
            return nullptr;
        CObject* Obj = Host->Object;
        if ((static_cast<unsigned>(Obj->ObjectClass) & 0x103u) != 0x103u)
            return nullptr;
        return static_cast<CEqObj*>(Obj);
    }

    // DAT_00675540 -- the fire dispatcher's 3DMathEngine, acquired on first use.
    I3DMathEngine* G_FireLeadMath = nullptr;

    CObject* ResolvePlayerShipObject()
    {
        void* const PlayerRw = GetPlayerObjRW();
        if (PlayerRw == nullptr || PlayerRw == reinterpret_cast<void*>(static_cast<intptr_t>(-0xc)))
            return nullptr;
        unsigned char* RwBytes = static_cast<unsigned char*>(PlayerRw);
        CObject* Nested = *reinterpret_cast<CObject**>(RwBytes + 0x10);
        if (Nested == nullptr ||
            ((static_cast<unsigned>(Nested->ObjectClass) & 0x503u) != 0x503u))
            return nullptr;
        return Nested;
    }

    FCliGunFxCompanion* GunCompanionFx(CELauncher* Launch)
    {
        if (Launch == nullptr || Launch->Arch == nullptr)
            return nullptr;
        Archetype::Root* Root = Launch->Arch;
        if (Root->Companion == nullptr)
            return nullptr;
        return reinterpret_cast<FCliGunFxCompanion*>(Root->Companion);
    }

    unsigned HostObjectId(FClientEquippedObj* Host)
    {
        // The host's IObjInspect get_id, nothing else.
        IObjInspect* Inspect = Host;
        return Inspect->get_id();
    }

    unsigned TargetObjectId(IObjRW* Target)
    {
        if (Target == nullptr)
            return 0;
        return Target->get_id();
    }

    void InvokeServerSetTarget(XSetTarget const& Info)
    {
        // DAT_0067ecd0 vt+0x10 -- IServer::SetTarget(DAT_00673344, &info), no tests.
        static_cast<IServer*>(GetLocalServerInterface())->SetTarget(GetPlayerConnectionId(), Info);
    }
} // namespace

    // LAB_005468e0 — CShip Host+4 vt+0x48 (target, sub, player). Public: the Ship
    // gameobj's IObjAffect set_target (client_gameobj.cpp) forwards here.
    int CliShipSetTarget(void* AdjustorThis, IObjRW* Target, unsigned short SubId, int PlayerId)
    {
        // Nothing is null-tested except the CShip narrowing's own chain, whose result is used
        // untested.
        int const LocalId = static_cast<int>(GetPlayerConnectionId()); // FUN_0054bb00
        unsigned char bLocal = 0;
        if (PlayerId == -1 || PlayerId == LocalId)
            bLocal = 1;

        FClientEquippedObj* Host =
            static_cast<FClientEquippedObj*>(static_cast<IObjAffect*>(AdjustorThis));
        CEqObj* EqObj = ResolveEqObjFromHost(Host);
        CShip* Ship = nullptr;
        if (EqObj != nullptr &&
            (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) == 0x503u)
            Ship = static_cast<CShip*>(EqObj);

        unsigned short EffectiveSub = SubId;
        if (bLocal != 0)
        {
            if (Target != nullptr)
                HudPostCommand(0xccu, 0);
            IObjRW* Current = Ship->get_target();
            if (Current != Target)
                EffectiveSub = SUBOBJ_ID_NONE;
        }

        if (SinglePlayer() && bLocal == 0)
            return 0;

        // get_sub_target is only asked when the target is unchanged.
        if (Ship->get_target() == Target && Ship->get_sub_target() == EffectiveSub)
            return 0;

        Ship->set_target(Target);
        Ship->set_sub_target(EffectiveSub);

        if (bLocal == 0)
            return 0;

        XSetTarget Info;
        std::memset(&Info, 0, sizeof(Info));
        Info.ObjectId = HostObjectId(Host);
        Info.SubObjId = SUBOBJ_ID_NONE;
        Info.TargetObjectId = TargetObjectId(Target);
        Info.TargetSubObjId = EffectiveSub;
        InvokeServerSetTarget(Info);
        return 0;
    }

    // FUN_005351d0 — EquippedObj/CShip Host+4 vt+0x44 (hp*, target, sub, player).
    int CliEquippedObjSetGunTarget(void* AdjustorThis, unsigned short const* HardpointId,
                                   IObjRW* Target, unsigned short SubId, int PlayerId)
    {
        // Neither the adjustor nor the hardpoint pointer is tested; FindByID runs on the class
        // & 0x103 narrowing's result untested.
        int const LocalId = static_cast<int>(GetPlayerConnectionId()); // FUN_0054bb00
        unsigned char bLocal = 0;
        if (PlayerId == -1 || PlayerId == LocalId)
            bLocal = 1;

        if (SinglePlayer() && bLocal == 0)
            return 0;

        FClientEquippedObj* const Owner =
            static_cast<FClientEquippedObj*>(static_cast<IObjAffect*>(AdjustorThis));
        CObject* const Obj0 = Owner->Object;
        CEqObj* const EqObj =
            (Obj0 != nullptr && (static_cast<unsigned>(Obj0->ObjectClass) & 0x103u) == 0x103u)
                ? static_cast<CEqObj*>(Obj0)
                : nullptr;

        CEquip* Equip = EqObj->EquipManager.FindByID(*HardpointId);
        if (Equip == nullptr || Equip->IsDestroyed())
            return static_cast<int>(0xfffffffe);

        CEGun* Gun = CEGun::cast(Equip);
        if (Gun == nullptr)
            return static_cast<int>(0xfffffffd);

        CSimple* TargetSimple = nullptr;
        if (Target != nullptr)
        {
            CObject const* Obj = Target->cobject();
            if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 3u) == 3u)
                TargetSimple = const_cast<CSimple*>(static_cast<CSimple const*>(Obj));
        }

        bool const Changed = Gun->SetTarget(TargetSimple, SubId);
        if (!Changed || bLocal == 0)
            return 0;

        FClientEquippedObj* Host =
            static_cast<FClientEquippedObj*>(static_cast<IObjAffect*>(AdjustorThis));
        XSetTarget Info;
        std::memset(&Info, 0, sizeof(Info));
        Info.ObjectId = HostObjectId(Host);
        Info.SubObjId = *HardpointId;
        Info.TargetObjectId = TargetObjectId(Target);
        Info.TargetSubObjId = SubId;
        InvokeServerSetTarget(Info);
        return 0;
    }

void CliLauncherPlayMuzzleFx(FCliLauncherFx* Launcher)
{
    // FUN_0052cfd0. Callers: the fire slots FUN_0052d410, FUN_0052d510, FUN_0052dd40.
    CELauncher* const Launch = Launcher->LaunchEquip();

    // Zero when the launcher or its archetype is null.
    FCliGunFxCompanion* const Companion = GunCompanionFx(Launch);

    // The barrel position is taken before the launcher is tested.
    Vector const BarrelWs = Launch->GetBarrelPosWS(0);
    if (Launch == nullptr || Launch->IsDestroyed())
        return;

    // The use-animation, when there is one.
    if (Launcher->AnimJointIndex.Value != -1)
    {
        DALib::Anim->EvalPlayback(
            static_cast<unsigned long>(Launcher->AnimJointIndex.Value), 0, 0.0f, 1.0f, nullptr,
            AsOpaqueDword(1.0f), AsOpaqueDword(1.0f), nullptr);
        AnimDB::Add(Launcher->AnimJointIndex.Value);
    }

    // From the cockpit camera, on the player's own ship: a muzzle light instead of a particle.
    if (IsFirstPersonCameraCurrent() && ResolvePlayerShipObject() == Launch->Owner)
    {
        if (Companion->LightAnimLen == 0)
            return;

        Light::AnimationArch* const Anim = GameLightFindAnimByName(Companion->LightAnimName);
        if (Anim == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: light animation (%s) is not defined\n",
                  kCliLauncherCpp, 0x140, Companion->LightAnimName);
            return;
        }

        // A white point light at the barrel with the flash radius.
        float Pos3[3];
        Pos3[0] = BarrelWs.x;
        Pos3[1] = BarrelWs.y;
        Pos3[2] = BarrelWs.z;
        unsigned char const Rgba[4] = {0xff, 0xff, 0xff, 0xff};
        FGameLight* const Light = GameLightCreatePoint(Pos3, Rgba, Companion->FlashRadius);
        if (Light == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: Light creation for gun failed: %s\n",
                  kCliLauncherCpp, 0x13b, Launch->Arch->Nickname.get_string());
            return;
        }

        // The light drifts with the host.
        Vector const HostVelocity = Launcher->Host->get_velocity();
        GameLightSetFreeMotionOffset(Light, &HostVelocity.x);

        // A failed attach disables the light; the creation reference is dropped either way.
        if (!GameLightAttachAnim(Light, Anim, 1.0f, 1.0f, 0.0f))
        {
            typedef void(__thiscall * FDisableFn)(FGameLight*);
            Light->Disable();
        }
        GameLightRelease(Light);
        return;
    }

    if (Companion->OneShotEffect.IsEmpty())
        return;

    // The previous muzzle effect goes.
    if (Launcher->MuzzleEffect != nullptr)
    {
        FParticleEffect* const Prior = static_cast<FParticleEffect*>(Launcher->MuzzleEffect);
        ParticleEffectCleanupAlchemy(Prior);
        ParticleEffectArmDeath(Prior);
        ParticleEffectRelease(Prior);
        Launcher->MuzzleEffect = nullptr;
    }

    // Barrel info not tested; no engine part -> done.
    CELauncher::Barrel const* const Barrel = Launch->GetBarrelInfo(0);
    if (Barrel->EngineIndex == -1)
        return;

    // The arch is not tested; the effect is null when the pool is empty.
    FEffectArch* const EffectArch = FindEffectArch(&Companion->OneShotEffect.Id);
    FParticleEffect* const Effect = ParticleEffectCreate();
    Launcher->MuzzleEffect = Effect;

    // The barrel's frame as the local transform, on the barrel's part.
    FTransform BarrelXform;
    MatrixCopyFromCommon(&BarrelXform.Rotation, Barrel->Orient);
    BarrelXform.Translation.X = Barrel->Position.x;
    BarrelXform.Translation.Y = Barrel->Position.y;
    BarrelXform.Translation.Z = Barrel->Position.z;
    ParticleEffectAttach(Effect, Launcher->Host, Barrel->EngineIndex, &BarrelXform);

    if (ParticleEffectStart(Effect, EffectArch) == 0)
    {
        ParticleEffectRelease(Effect);
        Launcher->MuzzleEffect = nullptr;
    }
}

int CliShipActivateEquip(void* AdjustorThis, unsigned short const* HardpointId,
                         unsigned char bOn, int PlayerId)
{
    // LAB_00535330 -- EquippedObj IObjAffect vt+0x4c, this = the gameobj + 4. Every path
    // returns 0.
    unsigned char bLocal = 1;
    if (PlayerId != -1)
        bLocal = (static_cast<unsigned>(PlayerId) == GetPlayerConnectionId()) ? 1 : 0;
    // In single player only local requests go on.
    if (SinglePlayer() && bLocal == 0)
        return 0;

    // The object narrowed by 0x103, null when the narrow fails -- read through below with no
    // test, as retail does.
    FClientGameobj* const Self =
        static_cast<FClientGameobj*>(static_cast<IObjAffect*>(AdjustorThis));
    CObject* const Obj = Self->Object;
    CEqObj* const EqObj =
        (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) == 0x103u)
            ? static_cast<CEqObj*>(Obj)
            : nullptr;

    CEquip* const Equip = EqObj->EquipManager.FindByID(*HardpointId);
    if (Equip == nullptr || Equip->IsDestroyed())
        return 0;

    // Switching an engine off is refused while the ship strafes or has a thruster on (the
    // object narrowed by 0x503, no null test).
    if (bOn == 0 && Equip->Type == static_cast<unsigned>(EquipTypeEngine))
    {
        CShip* const Ship =
            (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CShip*>(Obj)
                : nullptr;
        if (Ship->get_strafe_dir() != 0)
            return 0;
        if (Ship->any_thrusters_on())
            return 0;
    }

    // A byte compare.
    unsigned char const Active = Equip->IsActive() ? 1 : 0;
    if (Active == bOn)
        return 0;
    Equip->Activate(bOn != 0);

    // A local request goes to the server (no null test). Retail never writes +7; the port
    // zeroes it.
    if (bLocal != 0)
    {
        XActivateEquip Info;
        Info.ObjectId = Self->get_id();
        Info.EquipId = *HardpointId;
        Info.bActivate = bOn != 0;
        Info.Pad_0x07 = 0;
        static_cast<IServer*>(GetLocalServerInterface())->ActivateEquip(GetPlayerConnectionId(),
                                                                         Info);
    }
    return 0;
}

unsigned CliShipFireWeapons(void* AdjustorThis, unsigned short const* HardpointIds,
                            unsigned HardpointCount, Vector const* TargetPos, int PlayerId)
{
    // FUN_00534870 (on the IObjAffect sub-object). Every launcher that fired is remembered (at most ten, retail's
    // stack array at +0x84); afterwards the ones sharing a one-shot sound play it once together.
    FClientEquippedObj* const Host =
        static_cast<FClientEquippedObj*>(static_cast<IObjAffect*>(AdjustorThis));
    FEquipFxListNode* const Head = static_cast<FEquipFxListNode*>(Host->EquipFx.Head);
    // Retail's inlined CEqObj cast of the host object (class & 0x103); a null result would
    // fault at +0xe4 in CEquipManager::FindByID, and an equipped object's object always is one.
    CEqObj* const Ship = static_cast<CEqObj*>(Host->Object);

    FCliLauncherFx* Fired[10];
    unsigned FiredCount = 0;

    if (PlayerId == -1 || PlayerId == static_cast<int>(GetPlayerConnectionId()))
    {
        // The shot record (id 0, target FLT_MAX x3, empty vector), the host id, the target when
        // one is given; the ids that fire go straight into the record's own vector.
        XFireWeaponInfo Info;
        Info.ObjectId = Host->get_id();
        if (TargetPos != nullptr)
            Info.TargetPos = *TargetPos;

        if (HardpointCount != 0)
        {
            // Each requested id -> its CEquip -> the equip-FX wrapper holding it (+0xb8 list); a
            // launcher with a target and no force_gun_ori takes slot 3 and, when it fires, adds
            // the id to the record; otherwise slot 4. No IsActive test on this path.
            for (unsigned I = 0; I < HardpointCount; ++I)
            {
                CEquip* const Equip = Ship->EquipManager.FindByID(HardpointIds[I]);
                FEquipFxListNode* Node = Head->Next;
                while (Node != Head && Node->Wrapper->Equip != Equip)
                    Node = Node->Next;
                if (Node == Head)
                    continue;
                FCliLauncherFx* const Launcher = static_cast<FCliLauncherFx*>(Node->Wrapper);
                if (Launcher == nullptr || Launcher->Equip == nullptr)
                    continue;
                CELauncher* const Launch = CELauncher::cast(Launcher->Equip);
                unsigned char bFired = 0;
                if (TargetPos != nullptr &&
                    !static_cast<Archetype::Projectile const*>(Launch->Projectile)->bForceGunOri)
                {
                    bFired = Launcher->TryFire(TargetPos);
                    if (bFired != 0)
                        Info.Hardpoints.insert(Info.Hardpoints.end(), 1u, HardpointIds[I]);
                }
                else
                {
                    bFired = Launcher->FireBarrelAim();
                }
                if (bFired != 0 && FiredCount < 10u)
                {
                    Fired[FiredCount] = Launcher;
                    FiredCount = FiredCount + 1;
                }
            }
        }
        else
        {
            // No ids: every wrapper whose equipment is an active launcher, the same slot 3 /
            // slot 4 choice, the launcher's own id recorded.
            for (FEquipFxListNode* Node = Head->Next; Node != Head; Node = Node->Next)
            {
                CELauncher* const Launch = CELauncher::cast(Node->Wrapper->Equip);
                if (Launch == nullptr || !Launch->IsActive())
                    continue;
                FCliLauncherFx* const Launcher = static_cast<FCliLauncherFx*>(Node->Wrapper);
                unsigned char bFired = 0;
                if (TargetPos != nullptr &&
                    !static_cast<Archetype::Projectile const*>(Launch->Projectile)->bForceGunOri)
                {
                    bFired = Launcher->TryFire(TargetPos);
                    if (bFired != 0)
                        Info.Hardpoints.insert(Info.Hardpoints.end(), 1u, Launch->GetID());
                }
                else
                {
                    bFired = Launcher->FireBarrelAim();
                }
                if (bFired != 0 && FiredCount < 10u)
                {
                    Fired[FiredCount] = Launcher;
                    FiredCount = FiredCount + 1;
                }
            }
        }

        // A non-empty record goes to the server.
        if (!Info.Hardpoints.empty())
        {
            static_cast<IServer*>(GetLocalServerInterface())
                ->FireWeapon(GetPlayerConnectionId(), Info);
        }
    }
    else
    {
        // Another player's shot. The target is copied without a test; each id's wrapper takes
        // slot 5.
        Vector Aim = *TargetPos;
        bool bLeadAimed = false;
        for (unsigned I = 0; I < HardpointCount; ++I)
        {
            CEquip* const Equip = Ship->EquipManager.FindByID(HardpointIds[I]);
            FEquipFxListNode* Node = Head->Next;
            while (Node != Head && Node->Wrapper->Equip != Equip)
                Node = Node->Next;
            if (Node == Head)
                continue;
            FCliLauncherFx* const Launcher = static_cast<FCliLauncherFx*>(Node->Wrapper);
            if (Launcher == nullptr || Launcher->Equip == nullptr)
                continue;
            if (SinglePlayer())
            {
                // The lead-aimed point once there is one, else the target as sent.
                Launcher->FireDirect(bLeadAimed ? &Aim : TargetPos);
            }
            else
            {
                // FUN_005357f0: re-aim at the target the host's sub-target map holds.
                static_cast<FClientEquippedObj*>(Launcher->Host)
                    ->ApplySubTarget(Launcher->Equip->GetID());
                if (bLeadAimed)
                {
                    Launcher->FireDirect(&Aim);
                }
                else
                {
                    // An unguided gun turns the sent point into world space through its
                    // target's transform (math engine DAT_00675540); every later launcher
                    // reuses it.
                    CEGun* const Gun = CEGun::cast(Launcher->Equip);
                    if (Gun != nullptr && Gun->MunitionArch()->Seeker == 0)
                    {
                        CSimple* Target = nullptr;
                        unsigned short TargetSubObj = 0;
                        Gun->GetTarget(Target, TargetSubObj);
                        if (Target != nullptr)
                        {
                            FTransform TargetXform;
                            std::memcpy(&TargetXform, &Target->get_transform(),
                                        sizeof(TargetXform));
                            if (G_FireLeadMath == nullptr)
                                G_FireLeadMath = Acquire3DMathEngine();
                            FVector World;
                            G_FireLeadMath->TransformXform(
                                World, TargetXform, *reinterpret_cast<FVector const*>(TargetPos));
                            Aim.x = World.X;
                            Aim.y = World.Y;
                            Aim.z = World.Z;
                        }
                        bLeadAimed = true;
                        Launcher->FireDirect(&Aim);
                    }
                    else
                    {
                        Launcher->FireDirect(TargetPos);
                    }
                }
            }
            if (FiredCount < 10u)
            {
                Fired[FiredCount] = Launcher;
                FiredCount = FiredCount + 1;
            }
        }
    }

    // Every remembered launcher, in order, gathers the later ones that share its one-shot
    // sound: projectiles-per-fire and barrel-0 positions add up and they drop out; the summed
    // position times 1.0 (DAT_005c75dc) / the summed count is where the sound plays once.
    for (unsigned I = 0; I < FiredCount; ++I)
    {
        FCliLauncherFx* const Launcher = Fired[I];
        if (Launcher == nullptr)
            continue;
        CELauncher* const Launch = Launcher->LaunchEquip();
        int ShotCount = static_cast<int>(Launch->GetProjectilesPerFire());
        Vector Pos = Launch->GetBarrelPosWS(0);
        ID_String const SoundId = Launcher->OneShotSoundId();
        for (unsigned J = I + 1; J < FiredCount; ++J)
        {
            FCliLauncherFx* const Other = Fired[J];
            if (Other == nullptr || Other->OneShotSoundId().Id != SoundId.Id)
                continue;
            CELauncher* const OtherLaunch = Other->LaunchEquip();
            ShotCount = ShotCount + static_cast<int>(OtherLaunch->GetProjectilesPerFire());
            Vector const OtherPos = OtherLaunch->GetBarrelPosWS(0);
            Fired[J] = nullptr;
            Pos.x = Pos.x + OtherPos.x;
            Pos.y = Pos.y + OtherPos.y;
            Pos.z = Pos.z + OtherPos.z;
        }
        double const Scale = 1.0 / static_cast<double>(ShotCount);
        Pos.x = static_cast<float>(Pos.x * Scale);
        Pos.y = static_cast<float>(Pos.y * Scale);
        Pos.z = static_cast<float>(Pos.z * Scale);
        Launcher->PlayFireSound(&Pos, ShotCount);
    }
    return 0;
}

unsigned char EquippedObjHandleEquipMsg(FClientEquippedObj* Host,
                                        FCliEquipFirePacket const* Packet)
{
    // FUN_00534460. Neither `this` nor the packet is tested at entry.
    if (Packet->Op == 0x18)
    {
        // The whole +0x10 dword is tested; the id pushed is its low word.
        unsigned short Hp = Packet->HardpointId;
        unsigned short const* Ids = nullptr;
        unsigned Count = 0;
        if (Packet->HardpointId != 0 || Packet->Pad12 != 0)
        {
            Ids = &Hp;
            Count = 1;
        }
        Vector Target = Packet->TargetPos;
        int const PlayerId = static_cast<int>(GetPlayerConnectionId());
        CliShipFireWeapons(static_cast<IObjAffect*>(Host), Ids, Count, &Target, PlayerId);
        return 1;
    }

    if (Packet->Op != 0x19)
        return 0;

    // ActivateEquip arm, cases 0..4 (all return 0). FindByID is called with no test of the
    // class & 0x103 narrowing's result.
    CEqObj* Ship = ResolveEqObjFromHost(Host);

    CEquip* Equip = Ship->EquipManager.FindByID(Packet->HardpointId);
    if (Equip == nullptr || Equip->IsDestroyed())
        return 0;

    int const PlayerId = static_cast<int>(GetPlayerConnectionId());
    unsigned short HpId = Equip->Id;
    int const Sub = Packet->ActivateSubOp;

    switch (Sub)
    {
    case 0:
        CliShipActivateEquip(static_cast<IObjAffect*>(Host), &HpId, 0, PlayerId);
        return 0;
    case 1:
        CliShipActivateEquip(static_cast<IObjAffect*>(Host), &HpId, 1, PlayerId);
        return 0;
    case 2:
    {
        unsigned char const Active = Equip->IsActive() ? 1 : 0;
        CliShipActivateEquip(static_cast<IObjAffect*>(Host), &HpId, static_cast<unsigned char>(1 - Active),
                             PlayerId);
        return 0;
    }
    case 3:
    {
        // Mute group: activate this, deactivate others of same Type mask.
        CEquipTraverser Trav(static_cast<int>(Equip->Type));
        CEquip* Walk = Ship->EquipManager.Traverse(Trav);
        while (Walk != nullptr)
        {
            unsigned short WalkId = Walk->Id;
            if (Walk == Equip)
            {
                if (!Walk->IsActive())
                    CliShipActivateEquip(static_cast<IObjAffect*>(Host), &WalkId, 1, PlayerId);
            }
            else if (Walk->IsActive())
            {
                CliShipActivateEquip(static_cast<IObjAffect*>(Host), &WalkId, 0, PlayerId);
            }
            Walk = Ship->EquipManager.Traverse(Trav);
        }
        return 0;
    }
    case 4:
    {
        // Sync all of Type to this equip's IsActive state.
        unsigned char const Want = Equip->IsActive() ? 1 : 0;
        CEquipTraverser Trav(static_cast<int>(Equip->Type));
        CEquip* Walk = Ship->EquipManager.Traverse(Trav);
        while (Walk != nullptr)
        {
            unsigned char const Cur = Walk->IsActive() ? 1 : 0;
            if (Cur != Want)
            {
                unsigned short WalkId = Walk->Id;
                CliShipActivateEquip(static_cast<IObjAffect*>(Host), &WalkId, Want, PlayerId);
            }
            Walk = Ship->EquipManager.Traverse(Trav);
        }
        return 0;
    }
    default:
        return 0;
    }
}

void CliShipPreprocessEquipUpdate(FClientEquippedObj* Host, FCliEquipFirePacket* Packet)
{
    // FUN_00544e70. Ops 0x1a / 0x1b -> 0x19 + the next launcher. `this`, its +0xc face and the
    // object are tested; the packet is not. Retail's Op == 1 test (byte +0x25 AND 0x700 ==
    // 0x200) can never be true, so it is not ported.
    if (Host == nullptr)
        return;

    CEqObj* Ship = nullptr;
    if (Host->Object != nullptr &&
        ((static_cast<unsigned>(Host->Object->ObjectClass) & 0x503u) == 0x503u))
        Ship = static_cast<CEqObj*>(Host->Object);
    if (Ship == nullptr)
        return;

    // 0x1fe0 = launcher family mask (gun/mine/cm droppers etc.).
    constexpr int kLauncherMask = 0x1fe0;

    if (Packet->Op == 0x1a)
    {
        Packet->Op = 0x19;
        CEquipTraverser Trav(kLauncherMask);
        CEquipManager* Mgr = &Ship->EquipManager;
        CEquip* First = Mgr->Traverse(Trav);
        while (First != nullptr)
        {
            CELauncher* AsLaunch = CELauncher::cast(First);
            if (AsLaunch != nullptr && First->IsActive())
                break;
            First = Mgr->Traverse(Trav);
        }
        CEquip* Next = Mgr->Traverse(Trav);
        if (Next == nullptr)
            Next = Mgr->Traverse(Trav);
        unsigned short Hp = SUBOBJ_ID_NONE;
        if (Next != nullptr && Next != First)
            Hp = Next->Id;
        // +0x14 = 3, the id zero-extended into the +0x10 dword.
        Packet->ActivateSubOp = 3;
        Packet->HardpointId = Hp;
        Packet->Pad12 = 0;
        return;
    }

    if (Packet->Op == 0x1b)
    {
        Packet->Op = 0x19;
        CEquipTraverser Trav(kLauncherMask);
        CEquipManager* Mgr = &Ship->EquipManager;
        CEquip* First = Mgr->ReverseTraverse(Trav);
        while (First != nullptr)
        {
            CELauncher* AsLaunch = CELauncher::cast(First);
            if (AsLaunch != nullptr && First->IsActive())
                break;
            First = Mgr->ReverseTraverse(Trav);
        }
        CEquip* Next = Mgr->ReverseTraverse(Trav);
        if (Next == nullptr)
            Next = Mgr->ReverseTraverse(Trav);
        unsigned short Hp = SUBOBJ_ID_NONE;
        if (Next != nullptr && Next != First)
            Hp = Next->Id;
        // +0x14 = 3, the id zero-extended into the +0x10 dword.
        Packet->ActivateSubOp = 3;
        Packet->HardpointId = Hp;
        Packet->Pad12 = 0;
    }
}

void CliShipLeadAimFire(FClientEquippedObj* Host)
{
    // FUN_00544880. Nothing is null-tested except the CShip narrowing's own chain, whose result
    // is used untested.
    IObjInspect const* const Inspect = static_cast<IObjInspect const*>(Host);

    // The nose (minus column 2).
    Transform const& Xform = Inspect->get_transform();
    float DirX = -Xform.Rotation.E[0][2];
    float DirY = -Xform.Rotation.E[1][2];
    float DirZ = -Xform.Rotation.E[2][2];

    // Normalised, no zero test.
    double const InvLen =
        1.0 / std::sqrt((static_cast<double>(DirY) * DirY + static_cast<double>(DirZ) * DirZ) +
                        static_cast<double>(DirX) * DirX);
    DirX = static_cast<float>(DirX * InvLen);
    DirY = static_cast<float>(DirY * InvLen);
    DirZ = static_cast<float>(DirZ * InvLen);

    // get_tgt_lead_fire_pos on the CShip narrowing, untested.
    CObject* const Obj = Host->Object;
    CShip* const Ship = (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                            ? static_cast<CShip*>(Obj)
                            : nullptr;
    Vector Lead;
    if (Ship->get_tgt_lead_fire_pos(Lead))
    {
        // Scaled to the distance to the lead point.
        Vector const& Pos = Inspect->get_position();
        double const Dx = static_cast<double>(Lead.x) - Pos.x;
        double const Dy = static_cast<double>(Lead.y) - Pos.y;
        double const Dz = static_cast<double>(Lead.z) - Pos.z;
        double const Dist = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx);
        DirX = static_cast<float>(DirX * Dist);
        DirY = static_cast<float>(DirY * Dist);
        DirZ = static_cast<float>(DirZ * Dist);
    }
    else
    {
        // DAT_005dd55c.
        DirX = static_cast<float>(static_cast<double>(DirX) * static_cast<double>(1e10f));
        DirY = static_cast<float>(static_cast<double>(DirY) * static_cast<double>(1e10f));
        DirZ = static_cast<float>(static_cast<double>(DirZ) * static_cast<double>(1e10f));
    }

    // Fire everything (Host+4 slot 0x34, no hardpoints) at dir + position.
    Vector const& Pos = Inspect->get_position();
    Vector Aim;
    Aim.z = static_cast<float>(static_cast<double>(DirZ) + Pos.z);
    Aim.y = static_cast<float>(static_cast<double>(DirY) + Pos.y);
    Aim.x = static_cast<float>(static_cast<double>(DirX) + Pos.x);
    int const PlayerId = static_cast<int>(GetPlayerConnectionId());
    CliShipFireWeapons(static_cast<IObjAffect*>(Host), nullptr, 0, &Aim, PlayerId);
}

unsigned char CliShipHandleEquipUpdate(FClientEquippedObj* Host,
                                       FCliEquipFirePacket const* Packet)
{
    // FUN_00544d70. Neither `this` nor the packet is tested.
    if (ShouldSkipLocalServerTick())
        return 1;

    FCliEquipFirePacket Local = *Packet;
    CliShipPreprocessEquipUpdate(Host, &Local);
    unsigned char const Handled = EquippedObjHandleEquipMsg(Host, &Local);
    if (Handled != 0)
        return 1;

    if (Local.Op == 0x1c)
    {
        // Packet +0x10 = object id (dword); +0x14 = sub id.
        unsigned const TargetId = *reinterpret_cast<unsigned const*>(
            reinterpret_cast<unsigned char const*>(&Local) + 0x10);
        unsigned short const SubId = static_cast<unsigned short>(Local.ActivateSubOp);
        FClientGameobj* TargetGo = ClientObjDb_FindObject(TargetId, 0);
        if (TargetGo != nullptr)
        {
            IObjRW* TargetRw = static_cast<IObjRW*>(TargetGo);
            int const PlayerId = static_cast<int>(GetPlayerConnectionId());

            // Host+4 vt+0x48 = LAB_005468e0 (CShip set_target + net).
            CliShipSetTarget(static_cast<IObjAffect*>(Host), TargetRw, SubId, PlayerId);

            // Every gun gets the target too; each pass re-narrows the object (class & 0x503)
            // and walks its manager with no test of the result.
            CEquipTraverser Trav(0x80);
            while (true)
            {
                CObject* const Obj = Host->Object;
                CEqObj* const Ship =
                    (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                        ? static_cast<CEqObj*>(Obj)
                        : nullptr;
                CEquip* const Equip = Ship->EquipManager.Traverse(Trav);
                if (Equip == nullptr)
                    break;
                unsigned short HpId = Equip->Id;
                CliEquippedObjSetGunTarget(static_cast<IObjAffect*>(Host), &HpId, TargetRw, SubId, PlayerId);
            }
        }
    }
    else if (Local.Op == 0xbb)
    {
        CliShipLeadAimFire(Host);
    }
    return 1;
}
FLauncherAnimHandle::~FLauncherAnimHandle()
{
    // FUN_0052ca50 (on +0x10). Reached from the ctor/dtor unwind funclets and inlined at the end
    // of FUN_0052caa0.
    if (Value != -1)
    {
        DALib::Anim->StopPlayback(static_cast<unsigned long>(Value));        // vt+0x38
        DALib::Anim->DestroyScript(reinterpret_cast<void*>(
            static_cast<uintptr_t>(static_cast<unsigned long>(Value))));    // vt+0x28
        Value = -1;
    }
}

FLauncherEffectCompanion::~FLauncherEffectCompanion()
{
    // FUN_00502d90 on +0x30 (Unwind@005c0443 / Unwind@005c0473, and the second pass inlined in
    // FUN_0052caa0).
    CliEffectCompanionClear(Slots);
}

FCliLauncherFx::FCliLauncherFx()
    : DryFireSoundArch(nullptr)
    , OneShotSoundArch(nullptr)
    , FireVoice(nullptr)
    , ConstEffectArch(nullptr)
    , MunitionExplosion(nullptr)
    , MuzzleEffect(nullptr)
    , DefaultEffectArch(nullptr)
{
    AnimJointIndex.Value = -1;
    Companion.Slots[0] = nullptr;
    Companion.Slots[1] = nullptr;
    Companion.Slots[2] = nullptr;
    Companion.Slots[3] = nullptr;
}

namespace
{
    // The NN voice lines the launcher family's notifies queue when the player's own equipment
    // is destroyed (static initializers LAB_0052d6c0, 0x0052d860, LAB_0052e170 / LAB_0052e190 /
    // LAB_0052e1b0).
    unsigned const G_MineDropperDestroyedId = CreateID("mine_dropper_destroyed");      // 00675444
    unsigned const G_CmDropperDestroyedId =
        CreateID("countermeasure_dropper_destroyed");                                   // 00675440
    unsigned const G_GunDestroyedId = CreateID("gun_destroyed");                       // 00675434
    unsigned const G_LauncherDestroyedId = CreateID("launcher_destroyed");             // 00675438
    unsigned const G_TurretDestroyedId = CreateID("turret_destroyed");                 // 0067543c

    // The block every family notify opens its code-0xa arm with: the host is the player's (a
    // pointer compare), not reaped, and its CSimple (class 0x103, else null -- get_hit_pts is
    // called on it regardless) still has hit points strictly above 0 (NaN fails).
    bool IsLivePlayerHost(FClientGameobj* Host)
    {
        if (static_cast<void*>(static_cast<IObjRW*>(Host)) != GetPlayerObjRW())
            return false;
        if (Host->IsDead())
            return false;
        CSimple* Simple = nullptr;
        if (Host != nullptr && Host->Object != nullptr &&
            (static_cast<unsigned>(Host->Object->ObjectClass) & 0x103u) == 0x103u)
        {
            Simple = static_cast<CSimple*>(Host->Object);
        }
        return Simple->get_hit_pts() > 0.0f;
    }

    // An identity FTransform.
    FTransform IdentityTransform()
    {
        FTransform Xform;
        std::memset(&Xform, 0, sizeof(Xform));
        Xform.Rotation.D[0][0] = 1.0f;
        Xform.Rotation.D[1][1] = 1.0f;
        Xform.Rotation.D[2][2] = 1.0f;
        return Xform;
    }

    // The companion "still running" test the launcher Update inlines twice (FUN_00503330's
    // predicate): a live particle, an active sound, or a light without anim keys (+0xd4).
    bool LauncherCompanionAlive(void* const* Companion)
    {
        FEffectBase const* const Effect = static_cast<FEffectBase const*>(Companion[2]);
        if (Effect != nullptr && (Effect->Flags08 & 2u) == 0)
            return true;
        ISoundSource* const Sound = static_cast<ISoundSource*>(Companion[1]);
        if (Sound != nullptr && Sound->IsActive() != 0)
            return true;
        FGameLight const* const Light = static_cast<FGameLight const*>(Companion[3]);
        return Light != nullptr && Light->Anim.Keys == nullptr;
    }
}

FCliLauncherFx::~FCliLauncherFx()
{
    // FUN_0052caa0. The ClientEquip part (FUN_00531af0: disconnect, separation explosion) is
    // ~FClientEquip, which runs after this body.
    if (FireVoice != nullptr)
    {
        FireVoice->Stop();
        FireVoice->Release();
        FireVoice = nullptr;
    }
    // The muzzle effect re-attached to nothing, killed, released.
    if (MuzzleEffect != nullptr)
    {
        FEffectBase* const Effect = static_cast<FEffectBase*>(MuzzleEffect);
        FTransform const Identity = IdentityTransform();
        EffectAttach(Effect, nullptr, -1, &Identity);
        typedef void(__thiscall * FEffectKill)(FEffectBase*);
        Effect->Kill();
        EffectRelease(Effect);
        MuzzleEffect = nullptr;
    }
    // Each only when set; none of the pointers is cleared.
    if (MunitionExplosion != nullptr)
        ExplosionArchRelease(MunitionExplosion);
    if (ConstEffectArch != nullptr)
        EffectArchRelease(ConstEffectArch);
    if (OneShotSoundArch != nullptr)
        ReleaseClientSoundArch(OneShotSoundArch);
    if (DryFireSoundArch != nullptr)
        ReleaseClientSoundArch(DryFireSoundArch);
    // AnimDB::Rem is called even with a -1 handle.
    AnimDB::Rem(AnimJointIndex.Value);
    if (AnimJointIndex.Value != -1)
    {
        DALib::Anim->StopPlayback(static_cast<unsigned long>(AnimJointIndex.Value));
        DALib::Anim->DestroyScript(reinterpret_cast<void*>(
            static_cast<uintptr_t>(static_cast<unsigned long>(AnimJointIndex.Value))));
        AnimJointIndex.Value = -1;
    }
    // The multi_sparks02 arch.
    if (DefaultEffectArch != nullptr)
        EffectArchRelease(DefaultEffectArch);
    // The +0x30 companion released by hand; the member destructors then run (the companion's
    // second pass finds it null, the +0x10 handle's finds -1).
    CliEffectCompanionClear(Companion.Slots);
}

void FCliLauncherFx::DropOnDestroying()
{
    // AnimDB::Rem is called even with a -1 handle.
    AnimDB::Rem(AnimJointIndex.Value);
    if (AnimJointIndex.Value != -1)
    {
        DALib::Anim->StopPlayback(static_cast<unsigned long>(AnimJointIndex.Value));
        DALib::Anim->DestroyScript(reinterpret_cast<void*>(
            static_cast<uintptr_t>(static_cast<unsigned long>(AnimJointIndex.Value))));
        AnimJointIndex.Value = -1;
    }
    CliEffectListRemove(&Host->EffectList, Companion.Slots);
    if (MuzzleEffect != nullptr)
    {
        FEffectBase* const Effect = static_cast<FEffectBase*>(MuzzleEffect);
        typedef void(__thiscall * FEffectKill)(FEffectBase*);
        Effect->Kill();
        EffectRelease(Effect);
        MuzzleEffect = nullptr;
    }
}

void FCliLauncherFx::notify(Event Code, void* Param)
{
    // LAB_0052ccb0.
    if (Code == EquipDestroying)
        DropOnDestroying();
    FClientEquip::notify(Code, Param);
}

bool FCliLauncherFx::Update(float Dt)
{
    // FUN_0052cd40. The spark arm needs a connected, disabled launcher.
    if (!SinglePlayer() && Equip != nullptr)
    {
        CELauncher* const Launch = LaunchEquip();
        if (Launch->IsConnected() && Launch->IsDisabled())
        {
            // Nothing while the companion still runs.
            if (!LauncherCompanionAlive(Companion.Slots))
            {
                ID_String SparksId;
                SparksId.Id = CreateID("multi_sparks02");
                FTransform const Local = IdentityTransform();
                long const Part = static_cast<CAttachedEquip*>(Launch)->GetRootIndex();
                // One-shot effect on DAT_00674c60 (FUN_00503070).
                if (!SparksId.IsEmpty())
                {
                    CliCreateEffectOneShot(FindEffectArch(&SparksId.Id), Host,
                                           static_cast<int>(Part), &Local, Companion.Slots);
                }
            }
        }
        else
        {
            // A companion still running leaves the host's effect list.
            if (LauncherCompanionAlive(Companion.Slots))
                CliEffectListRemove(&Host->EffectList, Companion.Slots);
        }
    }
    return FClientEquip::Update(Dt);
}

void FCliMineDropperFx::notify(Event Code, void* Param)
{
    // FUN_0052d5c0: code 0xa -- the player's live ship queues "mine_dropper_destroyed" -- then
    // LAB_0052ccb0's body.
    if (Code == EquipDestroying)
    {
        if (IsLivePlayerHost(Host))
            NnVoiceEnqueue(G_MineDropperDestroyedId, 0);
        DropOnDestroying();
    }
    FClientEquip::notify(Code, Param);
}

void FCliCmDropperFx::notify(Event Code, void* Param)
{
    // FUN_0052d700.
    if (Code == EquipDestroying)
    {
        if (IsLivePlayerHost(Host))
            NnVoiceEnqueue(G_CmDropperDestroyedId, 0);
        DropOnDestroying();
    }
    else if (Code == Event_0x19)
    {
        // When the host object's owner is this client, fire this one equipment (no target).
        unsigned Owner = 0;
        CObject* const Object = Host->Object;
        if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 3u) == 3u)
            Owner = static_cast<CSimple*>(Object)->OwnerPlayer;
        else
            Owner = GetPlayerConnectionId();
        if (Owner == GetPlayerConnectionId())
        {
            unsigned short const EquipId = Equip->GetID();
            static_cast<IObjAffect*>(Host)->fire_weapons(&EquipId, 1, nullptr,
                                                    static_cast<int>(GetPlayerConnectionId()));
        }
    }
    FClientEquip::notify(Code, Param);
}

void FCliGunFx::notify(Event Code, void* Param)
{
    // LAB_0052da00.
    if (Code == EquipDestroying)
    {
        // "turret_destroyed" for an auto_turret gun, else "launcher_destroyed" for a seeking
        // munition, else "gun_destroyed".
        if (IsLivePlayerHost(Host))
        {
            CEGun const* const Gun = static_cast<CEGun const*>(Equip);
            unsigned MsgId = 0;
            if (Gun->GunArch()->bAutoTurret)
                MsgId = G_TurretDestroyedId;
            else if (Gun->MunitionArch()->Seeker != 0)
                MsgId = G_LauncherDestroyedId;
            else
                MsgId = G_GunDestroyedId;
            NnVoiceEnqueue(MsgId, 0);
        }

        // The turret voice stopped and re-posed on no attachment.
        ISoundSource* const Voice = TurretVoice;
        if (Voice != nullptr)
        {
            Voice->Stop();
            FTransform const Identity = IdentityTransform();
            Voice->SetPose(-1, reinterpret_cast<float const*>(&Identity));
        }
        // With a voice, the gun companion's +0x84 sound archetype is unloaded.
        if (TurretVoice != nullptr)
        {
            ID_String const* const MotionSound = reinterpret_cast<ID_String const*>(
                reinterpret_cast<unsigned char const*>(Equip->Arch->Companion) + 0x84);
            if (!MotionSound->IsEmpty())
                UnloadClientSoundArch(MotionSound);
        }
        DropOnDestroying();
    }
    FClientEquip::notify(Code, Param);
}

bool FCliGunFx::Update(float Dt)
{
    // FUN_0052dbf0.
    if (Equip != nullptr && TurretVoice != nullptr)
    {
        CEGun const* const Gun = static_cast<CEGun const*>(Equip);
        if (Gun->IsJointInMotion())
        {
            // The voice (tested again) starts the companion's +0x84 sound.
            ISoundSource* const Voice = TurretVoice;
            if (Voice != nullptr)
            {
                ID_String const* const MotionSound = reinterpret_cast<ID_String const*>(
                    reinterpret_cast<unsigned char const*>(Equip->Arch->Companion) + 0x84);
                Voice->Start(LookupClientSoundArch(MotionSound), 0);
            }
        }
        else if (!Gun->IsJointInMotion())
        {
            // Retail asks IsJointInMotion again; at rest the voice is stopped and re-posed.
            ISoundSource* const Voice = TurretVoice;
            if (Voice != nullptr)
            {
                Voice->Stop();
                FTransform const Identity = IdentityTransform();
                Voice->SetPose(-1, reinterpret_cast<float const*>(&Identity));
            }
        }
    }
    return FCliLauncherFx::Update(Dt);
}

namespace
{
    // DAT_00675448, seeded by the static initializer LAB_0052d550.
    unsigned const G_MultiSparks02Id = CreateID("multi_sparks02");

    // FUN_0052c830 after the object's allocation; the port's default constructor has already
    // zeroed +0x10..+0x3c and set +0x10 to -1.
    void CliLauncherFxBaseFill(FCliLauncherFx* Wrapper, CEquip* Equip, void* Host)
    {
        // FUN_00531a30 (connect, separation explosion).
        ClientEquipBaseFill(Wrapper, Equip, static_cast<FClientGameobj*>(Host));

        // A non-empty use_animation (arch +0x9c, no null test) plays on the equipment's root
        // into +0x10.
        CELauncher* const Launch = Wrapper->LaunchEquip();
        Archetype::Launcher const* const LaunchArch =
            static_cast<Archetype::Launcher const*>(Launch->EquipArch());
        char const* const UseAnimation = LaunchArch->UseAnimation.Value;
        if (UseAnimation[0] != '\0')
        {
            int const ScriptIndex = LaunchArch->AnimIndex;
            long const RootIndex = Launch->GetRootIndex();
            Wrapper->AnimJointIndex.Value = static_cast<int>(DALib::Anim->PlayScript(
                AsOpaqueDword(ScriptIndex), AsOpaqueDword(static_cast<int>(RootIndex)),
                UseAnimation, nullptr, nullptr));
        }

        // The launcher companion's dry_fire_sound (+0x80) into +0x14, acquired when found.
        // Retail's companion accessor null-tests Equip and its arch, but both were already
        // dereferenced above.
        unsigned char const* const LauncherComp =
            reinterpret_cast<unsigned char const*>(Launch->Arch->Companion);
        ID_String const* const DryFireSound =
            reinterpret_cast<ID_String const*>(LauncherComp + 0x80);
        if (!DryFireSound->IsEmpty())
        {
            Wrapper->DryFireSoundArch = LookupClientSoundArch(DryFireSound);
            if (Wrapper->DryFireSoundArch != nullptr)
                AcquireClientSoundArch(Wrapper->DryFireSoundArch);
        }

        // The munition companion's one_shot_sound (+0x5c) the same way into +0x18. The munition
        // and its companion are not tested.
        Archetype::Equipment* const Munition = Launch->Projectile;
        unsigned char const* const MunitionComp =
            reinterpret_cast<unsigned char const*>(Munition->Companion);
        ID_String const* const OneShotSound =
            reinterpret_cast<ID_String const*>(MunitionComp + 0x5c);
        if (!OneShotSound->IsEmpty())
        {
            Wrapper->OneShotSoundArch = LookupClientSoundArch(OneShotSound);
            if (Wrapper->OneShotSoundArch != nullptr)
                AcquireClientSoundArch(Wrapper->OneShotSoundArch);
        }

        // Its const_effect (+0x58) into +0x20, referenced when found, else an FDUMP.
        ID_String const* const ConstEffect =
            reinterpret_cast<ID_String const*>(MunitionComp + 0x58);
        if (!ConstEffect->IsEmpty())
        {
            Wrapper->ConstEffectArch = FindEffectArch(&ConstEffect->Id);
            if (Wrapper->ConstEffectArch != nullptr)
            {
                EffectArchAddRef(Wrapper->ConstEffectArch, 1);
            }
            else
            {
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: %s: [Effect] const_effect_name %s is missing from "
                      "effects.ini.",
                      "E:\\FL\\Scratch\\Source\\Client\\Space\\CliLauncher.cpp", 0x51,
                      Munition->Nickname.get_string(), ConstEffect->get_string());
            }
        }

        // The munition's explosion_arch into +0x24, referenced when found.
        if (!Munition->ExplosionArch.IsEmpty())
        {
            Wrapper->MunitionExplosion = Archetype::GetExplosion(Munition->ExplosionArch);
            if (Wrapper->MunitionExplosion != nullptr)
                ExplosionArchAddRef(Wrapper->MunitionExplosion);
        }

        // multi_sparks02 into +0x2c, referenced when found.
        Wrapper->DefaultEffectArch = FindEffectArch(&G_MultiSparks02Id);
        if (Wrapper->DefaultEffectArch != nullptr)
            EffectArchAddRef(Wrapper->DefaultEffectArch, 1);
    }
}

FCliLauncherFx* CliMineEquipFxConstruct(CEquip* Equip, void* Host)
{
    // FUN_0052d570 — FUN_0052c830, then the mine vtable PTR_FUN_005dd564 (no extra fields).
    FCliLauncherFx* Wrapper = new FCliMineDropperFx();
    CliLauncherFxBaseFill(Wrapper, Equip, Host);
    return Wrapper;
}

FCliLauncherFx* CliCmEquipFxConstruct(CEquip* Equip, void* Host)
{
    // FUN_0052d6e0 — FUN_0052c830, then the CM vtable PTR_FUN_005dd598.
    FCliLauncherFx* Wrapper = new FCliCmDropperFx();
    CliLauncherFxBaseFill(Wrapper, Equip, Host);
    return Wrapper;
}

FCliLauncherFx* CliGunEquipFxConstruct(CEquip* Equip, void* Host)
{
    // FUN_0052d880: FUN_0052c830, +0x40 = 0, the gun vtable PTR_LAB_005dd5d8.
    FCliGunFx* const Wrapper = new FCliGunFx();
    CliLauncherFxBaseFill(Wrapper, Equip, Host);
    Wrapper->TurretVoice = nullptr;
    // The gun companion's turret sound (+0x84, companion not tested) must be non-empty and
    // DALib::Sound live.
    ID_String const* const TurretSound = reinterpret_cast<ID_String const*>(
        reinterpret_cast<unsigned char const*>(Wrapper->Equip->Arch->Companion) + 0x84);
    if (!TurretSound->IsEmpty() && DALib::Sound != nullptr)
    {
        // Look up and Acquire; the result is not read.
        PreloadClientSoundArch(TurretSound);
        // A Source3D (operator new(0x64), vtable 005c9efc), not tested afterwards.
        Wrapper->TurretVoice = static_cast<ISoundSource*>(CreateClientSoundSource3D());
        // Looping, posed on the equipment's root with an identity frame.
        Wrapper->TurretVoice->SetLooping(1);
        long const RootIndex = Wrapper->LaunchEquip()->GetRootIndex();
        FTransform const Identity = IdentityTransform();
        Wrapper->TurretVoice->SetPose(static_cast<int>(RootIndex),
                                      reinterpret_cast<float const*>(&Identity));
    }
    return Wrapper;
}

