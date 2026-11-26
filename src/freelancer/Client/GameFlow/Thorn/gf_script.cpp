// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/Thorn/gf_script.h"

#include "GameFlow/character_info.h" // GetCharacterInfoDb (FUN_004356c0)
#include "Common/engine_light.h"
#include "Core/game_options.h"
#include "GameFlow/room_camera.h"
#include "Interface/win3d_immediate.h"

#include "DALib.h"
#include "common_newchar.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"
#include "thorn_iface.h"

#include <math.h>
#include <stddef.h>
#include <string.h>
#include <cstring>

static_assert(sizeof(FGfCameraSlot) == 0x40, "pool slot stride 0x40");
static_assert(offsetof(FGfCameraSlot, Orient) == 0x10, "slot orientation +0x10");
static_assert(offsetof(FGfCameraSlot, HalfFovXDeg) == 0x34, "slot fov +0x34");
static_assert(offsetof(FGfCameraPool, Priority) == 0x4000, "pool priorities +0x4000");
static_assert(offsetof(FGfCameraPool, bCanBeActive) == 0x4400, "pool flags +0x4400");
static_assert(offsetof(FGfCameraPool, bOccupied) == 0x4500, "pool occupancy +0x4500");
static_assert(offsetof(FGfCameraPool, ActiveIndex) == 0x4600, "pool active +0x4600");

namespace
{
    // The slot defaults every writer of the pool uses (static init, Allocate, Free).
    void ResetSlot(FGfCameraSlot& Slot)
    {
        Slot.Camera = nullptr;
        Slot.Pos.x = 0.0f;
        Slot.Pos.y = 0.0f;
        Slot.Pos.z = 0.0f;
        int Index = 0;
        for (Index = 0; Index < 9; ++Index)
            Slot.Orient[Index] = 0.0f;
        Slot.Orient[0] = 1.0f;
        Slot.Orient[4] = 1.0f;
        Slot.Orient[8] = 1.0f;
        Slot.HalfFovXDeg = 35.0f;
        Slot.ZNear = 0.25f;
        Slot.ZFar = 1000000.0f;
    }

    // DAT_0066d130 / DAT_0066d230.
    const int kGfLightRegistrySize = 0x40;
    FGfLightWrapper* G_GfLightRegistry[kGfLightRegistrySize] = {};
    unsigned char G_GfLightEnabled[kGfLightRegistrySize] = {};
}

// Retail builds this object in LAB_00450370 from the _initterm table and resets it again in
// LAB_00450440 at exit; a static object with a ctor and a dtor is that pair.
FGfCameraPool G_GfCameraPool;

FGfCameraPool::FGfCameraPool()
{
    Reset();
}

FGfCameraPool::~FGfCameraPool()
{
    // LAB_00450440 runs the slot loop twice (defaults first, then defaults with the priority
    // and byte arrays); the second pass alone leaves the same state.
    Reset();
}

void FGfCameraPool::Reset()
{
    int Index = 0;
    for (Index = 0; Index < 0x100; ++Index)
    {
        ResetSlot(Slots[Index]);
        Priority[Index] = -1;
        bCanBeActive[Index] = 0;
        bOccupied[Index] = 0;
    }
    ActiveIndex = -1;
}

int FGfCameraPool::Allocate(int SlotPriority, FRoomCamera* Camera, unsigned char bSlotCanBeActive)
{
    // FUN_00450580.
    int Slot = 0;
    for (Slot = 0; Slot < 0x100; ++Slot)
    {
        if (bOccupied[Slot] == 0)
            break;
    }
    if (Slot == 0x100)
        return -1;

    ResetSlot(Slots[Slot]);
    Slots[Slot].Camera = Camera;
    Priority[Slot] = SlotPriority;
    bCanBeActive[Slot] = bSlotCanBeActive;
    bOccupied[Slot] = 1;

    // An empty pool compares against -0x100 (signed).
    int ActivePriority = -0x100;
    if (ActiveIndex != -1)
        ActivePriority = Priority[ActiveIndex];
    if (bSlotCanBeActive != 0 && SlotPriority > ActivePriority)
        ActiveIndex = Slot;
    return Slot;
}

void FGfCameraPool::Free(int Slot)
{
    // FUN_00450680. No range check, as retail.
    const bool bWasActive = Slot == ActiveIndex;
    ResetSlot(Slots[Slot]);
    Priority[Slot] = -1;
    bCanBeActive[Slot] = 0;
    bOccupied[Slot] = 0;
    if (!bWasActive)
        return;

    int Best = -1;
    int BestPriority = -0x100;
    int Index = 0;
    for (Index = 0; Index < 0x100; ++Index)
    {
        if (bOccupied[Index] == 0 || bCanBeActive[Index] == 0)
            continue;
        if (Priority[Index] > BestPriority)
        {
            BestPriority = Priority[Index];
            Best = Index;
        }
    }
    ActiveIndex = Best;
    if (Best == -1)
        return;

    // FUN_00556cb0 / cc0 / ce0 / e00 / f10.
    FGfCameraSlot& Next = Slots[Best];
    RoomCameraSetPosition(Next.Camera, &Next.Pos);
    RoomCameraSetOrientation(Next.Camera, Next.Orient);
    RoomCameraSetHalfFovX(Next.Camera, Next.HalfFovXDeg);
    RoomCameraSetZNear(Next.Camera, Next.ZNear);
    RoomCameraSetZFar(Next.Camera, Next.ZFar);
}

int GfLightRegistryCount()
{
    // FUN_00452f70.
    return kGfLightRegistrySize;
}

FGfLightWrapper* GfLightRegistryGet(int Index)
{
    // FUN_00452f80: only an enable byte of exactly 1 returns the slot.
    if (G_GfLightEnabled[Index] != 1)
        return nullptr;
    return G_GfLightRegistry[Index];
}

int GfLightRegistryAdd(FGfLightWrapper* Wrapper, bool bOn)
{
    // Inlined in FUN_00452fa0: the first null slot of DAT_0066d130.
    int Index = 0;
    for (Index = 0; Index < kGfLightRegistrySize; ++Index)
    {
        if (G_GfLightRegistry[Index] == nullptr)
        {
            G_GfLightRegistry[Index] = Wrapper;
            G_GfLightEnabled[Index] = bOn ? 1 : 0;
            return Index;
        }
    }
    return -1;
}

void GfLightRegistrySetEnabled(int Index, bool bOn)
{
    // Thorn_Light's D3D-light slot 3, LAB_00453310. The caller tests the index for -1 first.
    G_GfLightEnabled[Index] = bOn ? 1 : 0;
}

bool GfLightRegistryIsEnabled(int Index)
{
    // Thorn_Light's D3D-light slot 6, LAB_004533f0. The byte only ever holds 0 or 1.
    return G_GfLightEnabled[Index] != 0;
}

void GfLightRegistryRemove(int Index)
{
    // LAB_00453100. No range check, as retail.
    G_GfLightRegistry[Index] = nullptr;
    G_GfLightEnabled[Index] = 0;
}

FGfLightWrapper* GfLightWrapperCreate(float const* Frame12, FD3dLight8 const* Source,
                                      Vector const* Direction, unsigned char Group)
{
    // FUN_00556fd0: operator new(8), both fields zeroed.
    FGfLightWrapper* Wrapper = new FGfLightWrapper;
    Wrapper->Light = nullptr;
    Wrapper->Group = 0;
    // The 0xc8 light, its source, position and look-at orientation, then the enable through its
    // slot 1 -- all of it is EngineLightCreate.
    Wrapper->Light = EngineLightCreate(Frame12, Source, Direction);
    Wrapper->Group = Group;
    return Wrapper;
}

void GfLightWrapperDestroy(FGfLightWrapper** Field)
{
    // FUN_00557130.
    FGfLightWrapper* Wrapper = *Field;
    if (Wrapper == nullptr)
        return;
    if (Wrapper->Light != nullptr)
        EngineLightDestroy(Wrapper->Light);
    Wrapper->Light = nullptr;
    Wrapper->Group = 0;
    delete Wrapper;
    *Field = nullptr;
}

void GfSetSceneAmbient(float R, float G, float B)
{
    // FUN_00556f20.
    unsigned char Rgb[3];
    if (GetGameOption(kGameOptLighting) == 0)
    {
        Rgb[0] = 0xff;
        Rgb[1] = 0xff;
        Rgb[2] = 0xff;
        EngineLightSetAmbientRgbBytes(Rgb);
        return;
    }
    // Per channel * 255.0 (.rdata 0x005c89c0) + 0.5 (.rdata 0x005c75e8), floor, low byte kept.
    Rgb[0] = static_cast<unsigned char>(
        static_cast<int>(floor(static_cast<double>(R) * 255.0 + 0.5)));
    Rgb[1] = static_cast<unsigned char>(
        static_cast<int>(floor(static_cast<double>(G) * 255.0 + 0.5)));
    Rgb[2] = static_cast<unsigned char>(
        static_cast<int>(floor(static_cast<double>(B) * 255.0 + 0.5)));
    EngineLightSetAmbientRgbBytes(Rgb);
}

bool GfLightScore(FD3dLight8 const* D3d, float const* LightPos3, Vector const* Dir3,
                  float const* Pos3, float /*Radius*/, float* OutScore)
{
    // FUN_005586f0 (cdecl; the fifth argument is never read). The x87 sequence is modelled at
    // the 53-bit precision control MSVC runs with: every float store is a float rounding below,
    // every value left on the stack stays a double.
    if (D3d->Type == kLightDirectional)
    {
        // Directional: always taken.
        *OutScore = 3.0f;
        return true;
    }

    // Only an exact zero (not a NaN) is "not taken".
    double const RangeSq = static_cast<double>(D3d->Range) * static_cast<double>(D3d->Range);
    if (RangeSq == 0.0)
    {
        *OutScore = 0.0f;
        return false;
    }

    // dx / dy are stored as floats, dz is kept on the stack and multiplied by its stored float;
    // sum order (dz*dz + dx*dx) + dy*dy.
    float const Dx = static_cast<float>(static_cast<double>(Pos3[0]) - LightPos3[0]);
    float const Dy = static_cast<float>(static_cast<double>(Pos3[1]) - LightPos3[1]);
    double const DzWide = static_cast<double>(Pos3[2]) - LightPos3[2];
    float const Dz = static_cast<float>(DzWide);
    double const DistSq =
        (DzWide * Dz + static_cast<double>(Dx) * Dx) + static_cast<double>(Dy) * Dy;

    // 2.0f is DAT_005d55b4; a point light keeps it.
    double Base = 2.0;
    if (D3d->Type != kLightPoint)
    {
        // The cone test runs unless DistSq < 1e-5 (DAT_005c75f0).
        if (!(DistSq < 1e-5))
        {
            double const Dot = (static_cast<double>(Dir3->z) * Dz +
                                static_cast<double>(Dir3->y) * Dy) +
                               static_cast<double>(Dx) * Dir3->x;
            double const CosAngle = Dot / sqrt(DistSq);
            float const CosAngleStored = static_cast<float>(CosAngle);
            // 2.0 is kept only when the angle is strictly inside the half cone.
            double const CosHalfPhi = cos(static_cast<double>(D3d->Phi) * 0.5);
            if (!(CosAngle > CosHalfPhi))
                Base = CosAngleStored;
        }
    }

    *OutScore = static_cast<float>(Base - DistSq / RangeSq);
    return true;
}

void GfLightsSelectNearest(FGfLightWrapper* const* List, int Count, float const* Pos3,
                           float Radius, unsigned char Group, int MaxCount,
                           FEngineLight** OutLights, float* OutScores, int* InOutCount)
{
    // FUN_00557170 (cdecl).
    int Index = 0;
    for (Index = 0; Index < *InOutCount; ++Index)
    {
        OutLights[Index] = nullptr;
        OutScores[Index] = 0.0f;
    }
    *InOutCount = 0;

    for (Index = 0; Index < Count; ++Index)
    {
        FGfLightWrapper* Wrapper = List[Index];
        if (Wrapper == nullptr)
            continue;
        FEngineLight* Light = Wrapper->Light;
        if (Light == nullptr)
            continue;
        if (Wrapper->Group != Group)
            continue;

        // FUN_00411370, then FUN_005586f0.
        Vector Dir;
        Vector* DirOut = EngineLightComputeDirection(Light, &Dir, nullptr, nullptr);
        float Score = 0.0f;
        if (!GfLightScore(&Light->D3d, &Light->Position.x, DirOut, Pos3, Radius, &Score))
            continue;

        // The arrays run from the highest score down.
        int const Used = *InOutCount;
        int Slot = 0;
        while (Slot < Used && Score < OutScores[Slot])
            ++Slot;
        int Move = 0;
        for (Move = Used; Move > Slot; --Move)
        {
            OutLights[Move] = OutLights[Move - 1];
            OutScores[Move] = OutScores[Move - 1];
        }
        OutLights[Slot] = Light;
        OutScores[Slot] = Score;
        ++*InOutCount;
    }

    // The count is cut to MaxCount only at the end.
    if (*InOutCount > MaxCount)
        *InOutCount = MaxCount;
}

void GfLightsApplyNearest(FGfLightWrapper* const* List, int Count, InstanceIndex Instance,
                          unsigned char Group, ICamera* Camera)
{
    // FUN_005572b0 (cdecl).
    const Vector* Pos = DALib::Engine->GetInstancePosition(Instance);
    // Radius 10000.0f (DAT_005e129c), at most 4 lights, 0x40-entry arrays with the in/out count
    // seeded to 0x40.
    FEngineLight* Lights[0x40];
    float Scores[0x40];
    int Selected = 0x40;
    float const PosCopy[3] = {Pos->x, Pos->y, Pos->z};
    GfLightsSelectNearest(List, Count, PosCopy, 10000.0f, Group, 4, Lights, Scores, &Selected);
    // FUN_004102c0.
    EngineLightApplyList(Lights, static_cast<unsigned>(Selected), Camera, nullptr);
}

// ---------------------------------------------------------------------------
// DAT_0066d2a8 -- the room fog state.
// ---------------------------------------------------------------------------

FGfFogState G_GfFog = {0, 0, 0.0f, 0.0f, 0.0f, 0, 0};

void GfFogReset()
{
    // FUN_004546d0 (and the static initializer LAB_00454220, same seven stores).
    G_GfFog.bOn = 0;
    G_GfFog.Color = 0;
    G_GfFog.Start = 0.0f;
    G_GfFog.End = 0.0f;
    G_GfFog.Density = 0.0f;
    G_GfFog.TableMode = 0;
    G_GfFog.VertexMode = 0;
}

void GfFogSetOn(unsigned char bOn)
{
    // LAB_00454450: RtcSlider::is_enabled("fog") is always asked first; on only when both.
    bool const bSlider = RtcSlider::is_enabled("fog");
    G_GfFog.bOn = (bOn != 0 && bSlider) ? 1 : 0;
}

bool GfFogIsActive()
{
    // FUN_00454700 (LAB_00454480 writes the same answer through its out byte).
    if (G_GfFog.bOn == 0)
        return false;
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return false;
    if (GetGameOption(kGameOptNebula) == 0)
        return false;
    if (G_GfFog.TableMode == 0)
        return true;
    // The call's result is not looked at.
    unsigned long Ability = 0;
    DALib::Pipeline->QueryDeviceAbility(0xd, &Ability);
    return Ability != 0;
}

void GfFogSetColor(int const* Rgb3)
{
    // LAB_00454500: the low byte of each int, 0x00RRGGBB.
    unsigned const R = static_cast<unsigned>(Rgb3[0]) & 0xffu;
    unsigned const G = static_cast<unsigned>(Rgb3[1]) & 0xffu;
    unsigned const B = static_cast<unsigned>(Rgb3[2]) & 0xffu;
    G_GfFog.Color = (((R << 8) | G) << 8) | B;
}

void GfFogGetColor(int* Rgb3)
{
    // LAB_00454530.
    Rgb3[0] = static_cast<int>((G_GfFog.Color >> 16) & 0xffu);
    Rgb3[1] = static_cast<int>((G_GfFog.Color >> 8) & 0xffu);
    Rgb3[2] = static_cast<int>(G_GfFog.Color & 0xffu);
}

void GfFogSetRange(float Start, float End, float Density)
{
    // LAB_00454570.
    GfFogSetRangeValue(&Start, &End, &Density);
}

void GfFogSetRangeValue(const float* Start, const float* End, const float* Density)
{
    // LAB_00454570 loads all three DWORDs before publishing any global.
    unsigned int RangeWords[3];
    std::memcpy(&RangeWords[0], Start, sizeof(RangeWords[0]));
    std::memcpy(&RangeWords[1], End, sizeof(RangeWords[1]));
    std::memcpy(&RangeWords[2], Density, sizeof(RangeWords[2]));
    std::memcpy(&G_GfFog.Start, &RangeWords[0], sizeof(RangeWords[0]));
    std::memcpy(&G_GfFog.End, &RangeWords[1], sizeof(RangeWords[1]));
    std::memcpy(&G_GfFog.Density, &RangeWords[2], sizeof(RangeWords[2]));
}

void GfFogGetRange(float* Start, float* End, float* Density)
{
    // LAB_004545a0.
    *Start = G_GfFog.Start;
    *End = G_GfFog.End;
    *Density = G_GfFog.Density;
}

void GfFogSetMode(unsigned char bTable, unsigned Mode)
{
    // LAB_004545d0: the mode goes to the table slot or the vertex slot, the other is zeroed.
    if (bTable != 0)
    {
        G_GfFog.TableMode = Mode;
        G_GfFog.VertexMode = 0;
        return;
    }
    G_GfFog.TableMode = 0;
    G_GfFog.VertexMode = Mode;
}

void GfFogGetMode(unsigned char* bTable, unsigned* Mode)
{
    // LAB_00454600: a non-zero vertex mode wins.
    if (G_GfFog.VertexMode != 0)
    {
        *bTable = 0;
        *Mode = G_GfFog.VertexMode;
        return;
    }
    *bTable = 1;
    *Mode = G_GfFog.TableMode;
}

void GfFogApply()
{
    // FUN_00454760 -> FUN_00454250(this = DAT_0066d2a8). The gate is FUN_00454700's, inlined.
    if (!GfFogIsActive())
    {
        Win3DDisable(kWin3DFog); // FUN_00421ed0
        return;
    }
    IRenderPipeline* Pipe = DALib::Pipeline;
    int Bits = 0;
    Pipe->SetRenderState(D3DRS_FOGENABLE, 1);
    Pipe->SetRenderState(D3DRS_FOGCOLOR, static_cast<int>(G_GfFog.Color));
    memcpy(&Bits, &G_GfFog.Start, sizeof(Bits));
    Pipe->SetRenderState(D3DRS_FOGSTART, Bits);
    memcpy(&Bits, &G_GfFog.End, sizeof(Bits));
    Pipe->SetRenderState(D3DRS_FOGEND, Bits);
    memcpy(&Bits, &G_GfFog.Density, sizeof(Bits));
    Pipe->SetRenderState(D3DRS_FOGDENSITY, Bits);
    Pipe->SetRenderState(D3DRS_FOGTABLEMODE, static_cast<int>(G_GfFog.TableMode));
    Pipe->SetRenderState(D3DRS_FOGVERTEXMODE, static_cast<int>(G_GfFog.VertexMode));
}

void GfScriptCharacterAnimations(IScriptEngine* Script, bool bAcquire)
{
    // FUN_0044fa90.
    if (Script == nullptr)
        return;
    int const Count = static_cast<int>(Script->GetEventCount());
    for (int Index = 0; Index < Count; ++Index)
    {
        FThornMotionEventDesc const* const Desc =
            static_cast<FThornMotionEventDesc const*>(Script->GetEventByIndex(Index));
        if (Desc->Common.Type != kThornTypeStartMotion)
            continue;
        // FUN_00435320 (queue) or FUN_00434eb0 (release).
        if (bAcquire)
            GetCharacterInfoDb()->QueueScript(Desc->Animation);
        else
            GetCharacterInfoDb()->ReleaseScriptByName(Desc->Animation);
    }
}

void GfScriptFindBackgroundAudio(IScriptEngine* Script, unsigned char* OutAmbient,
                                 unsigned char* OutMusic)
{
    // FUN_0044faf0.
    if (OutAmbient != nullptr)
        *OutAmbient = 0;
    if (OutMusic != nullptr)
        *OutMusic = 0;
    if (Script == nullptr)
        return;
    int const Count = static_cast<int>(Script->GetEntityCount());
    for (int Index = 0; Index < Count; ++Index)
    {
        FThornEntitySnap const* const Entity =
            static_cast<FThornEntitySnap const*>(Script->GetEntityDesc(Index));
        if (Entity->Type != kThornEntitySound)
            continue;
        unsigned char* Out = nullptr;
        if (_stricmp(Entity->TemplateName, "BackgroundAmbient") == 0)
            Out = OutAmbient;
        else if (_stricmp(Entity->TemplateName, "BackgroundMusic") == 0)
            Out = OutMusic;
        else
            continue;
        if (Out != nullptr)
            *Out = 1;
    }
}

void GfScriptTouchEntities(IScriptEngine* Script, bool bAcquire)
{
    // FUN_0044fb80: slot 6 count, slot 9 per entity, nothing kept. bAcquire is passed by both
    // callers and never read by retail.
    (void)bAcquire;
    if (Script == nullptr)
        return;
    int const Count = static_cast<int>(Script->GetEntityCount());
    for (int Index = 0; Index < Count; ++Index)
        Script->GetEntityDesc(Index);
}
