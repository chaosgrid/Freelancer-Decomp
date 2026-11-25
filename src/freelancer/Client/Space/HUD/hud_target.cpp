// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/HUD/hud_target.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerBaseIdRetail (DAT_00673358)
#include "Space/client_gameobj.h"         // FClientGameobj : IObjInspectImpl
#include "Space/client_objdb.h"           // ClientObjDb_FindObject (FUN_005416c0)
#include "Common/Fonts/font_string.h"
#include "Core/globals.h"                // IsClientServerTickPaused (FUN_0042d730)
#include "Common/Fonts/ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Common/input_manager.h"          // GetClientMouseCursorDrawFlag, GetClientSoftwareCursor
#include "GameFlow/menu_universe_base.h"     // IsClientLocationInTransition (DAT_00668761)
#include "Common/misc_util.h"              // FmtStrSprintf (FUN_0041c970)
#include "NeuroNet/nn_modal.h"               // G_bNnScreenUp (DAT_0067dcc8)
#include "NeuroNet/nn_text_scratch.h"        // G_NnTextScratchA/B (DAT_0066dc60 / DAT_0066fc60)
#include "NeuroNet/nn_ui_pulse.h"            // G_HudChromeColorRgba (DAT_00679bb4)
#include "Common/engine_light.h"           // ScaleVec3 (FUN_00411d60)
#include "Space/player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"           // IsPlayerShipId (FUN_00548eb0)
#include "Space/Cameras/space_camera.h"           // AsCamera
#include "Space/star_sphere.h"            // Win3DIdentityRotation3x3 (FUN_004215b0)
#include "Interface/win3d_immediate.h"        // Win3DSetViewport (FUN_00422610), FUN_00422910
#include "Space/space_letterbox.h"        // IsLetterboxVisible (FUN_0041a3e0)
#include "Interface/tooltip_hover.h"          // FToolTipHoverInfo, NnToolTipFeedHover (FUN_004be370)
#include "Interface/win3d.h"                  // Win3DGetCamera (DAT_0067dbf8)

#include "common_cequip.h"
#include "common_cobject.h"
#include "common_csys.h"            // Acquire3DMathEngine (FUN_005b73e0)
#include "common_iface.h"           // IObjRW, IObjInspectImpl, ObjRwFromWatchable
#include "common_misc.h"            // SUBOBJ_ID_NONE, ICamera
#include "common_reputation.h"      // Reputation::Vibe::GetAffiliation / GetName, get_short_name
#include "common_universe.h"        // Universe::get_system

#include "DALib.h"
#include "engine/engbase.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace
{
    // DAT_00674b1c -- the 3DMathEngine HUD_Target_Base.cpp acquires lazily (FUN_005b73e0) in
    // the FHudInstanceBounds helpers and PickSubTarget.
    I3DMathEngine* G_HudTargetMath = nullptr;

    // DAT_006649c0 -- FUN_00422c70's own lazily acquired 3DMathEngine.
    I3DMathEngine* G_HudPickMath = nullptr;

    I3DMathEngine* HudTargetMath()
    {
        if (G_HudTargetMath == nullptr)
        {
            G_HudTargetMath = Acquire3DMathEngine();
        }
        return G_HudTargetMath;
    }

    Transform const* InstanceTransform(InstanceIndex Instance)
    {
        // IEngine slot 40 (+0xa0) returns the instance's 12-float transform.
        return DALib::Engine->GetInstanceTransform(Instance);
    }

    // DAT_006127b8 -- the slack PickSubTarget hands FHudSpherePick::Test (0x3ecccccd).
    float const kSubTargetPickSlack = 0.4f;

    // .rdata 0x3f333333 -- the chrome dimming factor of SetViewObject's wireframe colour.
    float const kViewWireDim = 0.7f;

    // DAT_00611400 -- FUN_0045a7c0's 0x15 {type, ids_name} pairs.
    struct FCollisionTypeIds
    {
        unsigned Type;
        unsigned Ids;
    };

    FCollisionTypeIds const kCollisionTypeIds[0x15] = {
        {0x00000000u, 0x708u}, {0x00000001u, 0x708u}, {0x00000002u, 0x709u},
        {0x00000004u, 0x70au}, {0x00000008u, 0x70bu}, {0x00000010u, 0x70cu},
        {0x00000020u, 0x70du}, {0x00000040u, 0x70eu}, {0x00000080u, 0x70fu},
        {0x00000100u, 0x710u}, {0x00000200u, 0x711u}, {0x00000400u, 0x712u},
        {0x00000800u, 0x713u}, {0x00001000u, 0x714u}, {0x00002000u, 0x715u},
        {0x00004000u, 0x716u}, {0x00008000u, 0x717u}, {0x00010000u, 0x718u},
        {0x00020000u, 0x719u}, {0x00040000u, 0x71au}, {0x00080000u, 0x71bu},
    };

    // The inlined chain retail spells out at every use: watched -> -8 (IObjRW) -> +0xc
    // (IObjInspectImpl's CBase) -> +4 (CBase::Object), null anywhere along it -> null.
    CObject* CObjectOfWatcher(BaseWatcher const& Watcher)
    {
        Watchable const* const Watched = Watcher.target();
        if (Watched == nullptr)
        {
            return nullptr;
        }
        IObjRW* const Rw = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
        if (Rw == nullptr)
        {
            return nullptr;
        }
        IObjInspectImpl* const Impl = static_cast<IObjInspectImpl*>(Rw);
        CBase const* const Base = Impl;
        return Base->cobject();
    }

    // FUN_004104c0 (cdecl) -- clamp to 0..255.
    int ClampByte(int Value)
    {
        if (Value < 0)
        {
            return 0;
        }
        if (Value > 0xff)
        {
            return 0xff;
        }
        return Value;
    }

    // The product of an 8-bit integer and a float is exact in double, and ftol truncates.
    int DimChromeByte(unsigned char Byte)
    {
        return static_cast<int>(static_cast<double>(Byte) * static_cast<double>(kViewWireDim));
    }
}

// =============================================================================================
// FHudInstanceBounds
// =============================================================================================

Vector* HudInverseTransformPoint(Transform const* Xform, Vector* Out, Vector const* In)
{
    // FUN_004e6880.
    Vector Local;
    HudTargetMath()->InverseTransform(Local, *Xform, *In);
    *Out = Local;
    return Out;
}

FHudInstanceBounds::FHudInstanceBounds(InstanceIndex Instance, Transform const* Xform,
                                       bool bIncludeShieldParts)
{
    // FUN_004e65d0: the root part's sphere.
    float Radius = 0.0f;
    Vector Center;
    DALib::Engine->GetInstanceExtent(Instance, 0, &Radius, &Center);
    Transform const* const InstanceXform = InstanceTransform(Instance);
    Vector World;
    HudTargetMath()->TransformXform(World, *InstanceXform, Center);
    Center = World;

    // Retail calls InverseTransform twice, once for Min and once for Max.
    Vector Local;
    HudTargetMath()->InverseTransform(Local, *Xform, Center);
    Min.x = Local.x - Radius;
    Min.y = Local.y - Radius;
    Min.z = Local.z - Radius;

    HudTargetMath()->InverseTransform(Local, *Xform, Center);
    Max.x = Local.x + Radius;
    Max.y = Local.y + Radius;
    Max.z = Local.z + Radius;

    ExpandChildren(Instance, Xform, bIncludeShieldParts);
}

Vector* FHudInstanceBounds::GetCenter(Vector* Out) const
{
    // FUN_004e6580 (DAT_005c75e0 = 0.5f).
    Out->x = (Min.x + Max.x) * 0.5f;
    Out->y = (Min.y + Max.y) * 0.5f;
    Out->z = (Min.z + Max.z) * 0.5f;
    return Out;
}

void FHudInstanceBounds::ExpandChildren(InstanceIndex Instance, Transform const* Xform,
                                        bool bIncludeShieldParts)
{
    // FUN_004e69c0.
    InstanceIndex Child = DALib::Engine->GetRelatedInstance(Instance, 1, InvalidInstanceIndex);
    while (Child != InvalidInstanceIndex)
    {
        if (bIncludeShieldParts || !IsShieldPart(Child))
        {
            Vector Center;
            float const Radius = GetPartSphere(Child, &Center);

            // Min: a component moves only when strictly below.
            Vector Local;
            HudInverseTransformPoint(Xform, &Local, &Center);
            Vector Low;
            Low.x = Local.x - Radius;
            Low.y = Local.y - Radius;
            Low.z = Local.z - Radius;
            if (Low.x < Min.x)
            {
                Min.x = Low.x;
            }
            if (Low.y < Min.y)
            {
                Min.y = Low.y;
            }
            if (Low.z < Min.z)
            {
                Min.z = Low.z;
            }

            // Max: the transform again; a component moves only when strictly above.
            HudInverseTransformPoint(Xform, &Local, &Center);
            Vector High;
            High.x = Local.x + Radius;
            High.y = Local.y + Radius;
            High.z = Local.z + Radius;
            if (High.x > Max.x)
            {
                Max.x = High.x;
            }
            if (High.y > Max.y)
            {
                Max.y = High.y;
            }
            if (High.z > Max.z)
            {
                Max.z = High.z;
            }

            ExpandChildren(Child, Xform, bIncludeShieldParts);
        }
        Child = DALib::Engine->GetRelatedInstance(Instance, 1, Child);
    }
}

float FHudInstanceBounds::GetPartSphere(InstanceIndex Instance, Vector* OutCenter) const
{
    // FUN_004e68d0.
    float Radius = 0.0f;
    DALib::Engine->GetInstanceExtent(Instance, 0, &Radius, OutCenter);
    Transform const* const Xform = InstanceTransform(Instance);
    Vector World;
    HudTargetMath()->TransformXform(World, *Xform, *OutCenter);
    *OutCenter = World;
    return Radius;
}

bool FHudInstanceBounds::IsShieldPart(InstanceIndex Instance) const
{
    // FUN_004e6960: the instance's user data is its CObject; retail inlines inst_to_part and
    // is_shield_part over the +0x68..+0x6c part map (no null test on the user data).
    CObject const* const Object = static_cast<CObject const*>(
        DALib::Engine->GetInstanceUserData(Instance));
    long const EngineInst = Instance;
    return Object->is_shield_part(Object->inst_to_part(EngineInst));
}

// =============================================================================================
// FHudSpherePick
// =============================================================================================

FHudSpherePick::FHudSpherePick(unsigned char Flag)
    : HitCount(0)
{
    // FUN_00422ba0.
    BestKey = 0.0f;
    Field50 = -1;
    Field4c = Flag;
}

void FHudSpherePick::Setup(Vector const* From, Vector const* To)
{
    // FUN_00422bc0: one reciprocal of the double-precision length, then each component scaled.
    Origin = *From;
    Dir.x = To->x - Origin.x;
    Dir.y = To->y - Origin.y;
    Dir.z = To->z - Origin.z;
    double const Inverse = 1.0 / sqrt(static_cast<double>(Dir.x) * Dir.x +
                                      static_cast<double>(Dir.y) * Dir.y +
                                      static_cast<double>(Dir.z) * Dir.z);
    Dir.x = static_cast<float>(Inverse * Dir.x);
    Dir.y = static_cast<float>(Inverse * Dir.y);
    Dir.z = static_cast<float>(Inverse * Dir.z);
    Field18 = -1.0f;
    HitCount = 0;
    Field50 = -1;
    BestKey = 0.0f;
}

float HudRaySphereClosest(Vector const* Origin, Vector const* Dir, Vector const* Center,
                          float Radius, Vector* OutRay, Vector* OutSurface,
                          Transform const* Xform)
{
    // FUN_00422c70.
    if (G_HudPickMath == nullptr)
    {
        G_HudPickMath = Acquire3DMathEngine();
    }
    Vector World;
    G_HudPickMath->TransformXform(World, *Xform, *Center);

    Vector ToCenter;
    ToCenter.x = World.x - Origin->x;
    ToCenter.y = World.y - Origin->y;
    ToCenter.z = World.z - Origin->z;

    // t = V.D summed x, z, y, in retail's order.
    float const Along = ToCenter.x * Dir->x + ToCenter.z * Dir->z + ToCenter.y * Dir->y;
    float Cosine = 1.0f;
    if (Along < 0.0f)
    {
        *OutRay = *Origin;
    }
    else
    {
        OutRay->x = Along * Dir->x + Origin->x;
        OutRay->y = Along * Dir->y + Origin->y;
        OutRay->z = Along * Dir->z + Origin->z;
        Cosine = Along / sqrtf(ToCenter.z * ToCenter.z + ToCenter.y * ToCenter.y +
                               ToCenter.x * ToCenter.x);
    }

    Vector Gap;
    Gap.x = OutRay->x - World.x;
    Gap.y = OutRay->y - World.y;
    Gap.z = OutRay->z - World.z;
    float const Distance = sqrtf(Gap.z * Gap.z + Gap.y * Gap.y + Gap.x * Gap.x);
    float const Scale = Radius / Distance;
    OutSurface->x = World.x + Gap.x * Scale;
    OutSurface->y = World.y + Gap.y * Scale;
    OutSurface->z = World.z + Gap.z * Scale;

    // The ray point snaps to the surface only when inside.
    if (Distance < Radius)
    {
        *OutRay = *OutSurface;
    }
    return Cosine;
}

bool FHudSpherePick::Test(InstanceIndex Instance, float Slack)
{
    // FUN_00422e30: every child first.
    bool bHit = false;
    InstanceIndex Child = DALib::Engine->GetRelatedInstance(Instance, 1, InvalidInstanceIndex);
    while (Child != InvalidInstanceIndex)
    {
        if (Test(Child, Slack))
        {
            bHit = true;
        }
        Child = DALib::Engine->GetRelatedInstance(Instance, 1, Child);
    }

    // Then this instance's extent-flag-1 sphere.
    float Radius;
    Vector Center;
    DALib::Engine->GetInstanceExtent(Instance, 1, &Radius, &Center);
    Transform const* const Xform = InstanceTransform(Instance);
    Vector RayPoint;
    Vector SurfacePoint;
    float const Cosine = HudRaySphereClosest(&Origin, &Dir, &Center, Radius, &RayPoint,
                                             &SurfacePoint, Xform);

    // Compared against a double 0.5; only a cosine strictly above goes on (NaN stops).
    if (!(Cosine > 0.5))
    {
        return bHit;
    }
    float const Key = -Cosine;
    float const Dx = RayPoint.x - SurfacePoint.x;
    float const Dy = RayPoint.y - SurfacePoint.y;
    float const Dz = RayPoint.z - SurfacePoint.z;
    float const Gap = sqrtf(Dx * Dx + Dz * Dz + Dy * Dy);
    // NaN stops here too.
    if (!(Gap <= Slack))
    {
        return bHit;
    }
    // A later hit must be strictly better.
    if (HitCount != 0 && !(Key < BestKey))
    {
        return bHit;
    }
    BestKey = Key;
    HitPoint = RayPoint;
    HitSurface = SurfacePoint;
    ++HitCount;
    return true;
}

// =============================================================================================
// Free helpers
// =============================================================================================

bool HudIsMousePickBlocked()
{
    // FUN_0045a740.
    if (G_bNnScreenUp)
    {
        return true;
    }
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        return true;
    }
    if (IsClientServerTickPaused())
    {
        return true;
    }
    if (IsClientLocationInTransition()) // DAT_00668761
    {
        return true;
    }
    // Docked (FUN_004c4810).
    if ((GetPlayerBaseIdRetail() & 0x3fffffffu) != 0)
    {
        return false;
    }
    return GetPlayerObjRW() == nullptr;
}

unsigned HudCollisionGroupTypeIds(unsigned CollisionGroupType)
{
    // FUN_0045a7c0.
    for (int Index = 0; Index < 0x15; ++Index)
    {
        if (kCollisionTypeIds[Index].Type == CollisionGroupType)
        {
            return kCollisionTypeIds[Index].Ids;
        }
    }
    return 0;
}

unsigned long* HudDamageColorArgb(unsigned long* OutArgb, float Damage)
{
    // FUN_0045a1a0. A NaN Damage skips the first two bands and takes the third.
    if (Damage < 0.25f)
    {
        memcpy(OutArgb, G_HudChromeColorRgba, sizeof(G_HudChromeColorRgba)); // DAT_00679bb4
        return OutArgb;
    }
    if (Damage < 0.5f)
    {
        *OutArgb = 0xff4080ffu; // bytes ff, 80, 40, ff
        return OutArgb;
    }
    if (!(Damage > 0.75f))
    {
        *OutArgb = 0xff4040ffu; // bytes ff, 40, 40, ff
        return OutArgb;
    }

    // The pulse phase is rounded to float, then flipped by the direction byte; R = ftol(255 * t)
    // and G = ftol(R * t) are computed in double.
    float const Phase = NnUiPulsePhaseNorm();
    double const Level = (NnUiPulseDirection() != 0) ? 1.0 - static_cast<double>(Phase)
                                                      : static_cast<double>(Phase);
    unsigned char const Red = static_cast<unsigned char>(static_cast<int>(255.0 * Level));
    unsigned char const Green =
        static_cast<unsigned char>(static_cast<int>(static_cast<double>(Red) * Level));
    *OutArgb = 0xff000000u | (static_cast<unsigned long>(Green) << 16) |
               (static_cast<unsigned long>(Green) << 8) | static_cast<unsigned long>(Red);
    return OutArgb;
}

int* HudProjectScreenRect(int* OutRect4, Vector const* A, Vector const* B)
{
    // FUN_004c71b0. Retail's Project outputs x / y / z and the rectangle's fallback values share
    // four stack dwords. Project writes nothing when it fails, and the pair
    // of a failed projection takes the raw bits left there: left / top from the x / y slots,
    // right / bottom from the z slot and a fourth dword nothing writes. The port keeps the same
    // overlay; what it holds on a failure is as undefined as in retail.
    union
    {
        float F[4];
        int I[4];
    } Scratch;
    ICamera* const Camera = AsCamera(Win3DGetCamera());

    int Left = 0;
    int Top = 0;
    if (static_cast<unsigned char>(Camera->Project(&Scratch.F[0], &Scratch.F[1], &Scratch.F[2],
                                                   A)) != 0)
    {
        Left = static_cast<int>(Scratch.F[0]);
        Top = static_cast<int>(Scratch.F[1]);
    }
    else
    {
        Left = Scratch.I[0];
        Top = Scratch.I[1];
    }

    int Right = 0;
    int Bottom = 0;
    if (static_cast<unsigned char>(Camera->Project(&Scratch.F[0], &Scratch.F[1], &Scratch.F[2],
                                                   B)) != 0)
    {
        Right = static_cast<int>(Scratch.F[0]);
        Bottom = static_cast<int>(Scratch.F[1]);
    }
    else
    {
        Right = Scratch.I[2];
        Bottom = Scratch.I[3];
    }

    OutRect4[0] = Left;
    OutRect4[1] = Top;
    OutRect4[2] = Right;
    OutRect4[3] = Bottom;
    return OutRect4;
}

Vector* HudTransformPoint(Transform const* Xform, Vector* Out, Vector const* In)
{
    // FUN_004e7090.
    Vector Result;
    HudTargetMath()->TransformXform(Result, *Xform, *In);
    *Out = Result;
    return Out;
}

CShip* HudShipOfObjRw(IObjRW* ObjRw)
{
    // FUN_00543320.
    if (ObjRw == nullptr)
    {
        return nullptr;
    }
    IObjInspectImpl* const Impl = static_cast<IObjInspectImpl*>(ObjRw);
    CBase* const Base = Impl;
    if (Base == nullptr)
    {
        return nullptr;
    }
    CObject* const Object = Base->cobject();
    if (Object == nullptr)
    {
        return nullptr;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
    {
        return nullptr;
    }
    return static_cast<CShip*>(Object);
}

namespace
{
    // The inlined `IObjRW + 0xc (CBase) -> +4 (Object)` chain of FUN_0045a4e0, null anywhere
    // -> null.
    CObject* HudCObjectOf(IObjInspectImpl* Impl)
    {
        if (Impl == nullptr)
        {
            return nullptr;
        }
        CBase* const Base = Impl;
        if (Base == nullptr)
        {
            return nullptr;
        }
        return Base->cobject();
    }

    // CShip::GroupId (+0x1b4), the multiplayer group id, read from a CObject with no class test.
    unsigned HudShipGroupId(CObject const* Ship)
    {
        return static_cast<CShip const*>(Ship)->GroupId;
    }
}

bool HudIsUnvisitedSolar(CSimple const* Object)
{
    // FUN_004d4c70. A watch-listed client object of a matching type is known.
    FClientGameobj* const Gameobj = ClientObjDb_FindObject(Object->Id, 0); // FUN_005416c0
    if (Gameobj != nullptr)
    {
        unsigned Type = 0;
        Gameobj->get_type(Type); // vt+0x88; the return value is not looked at
        if ((Type & 0x107ffdffu) != 0 && Gameobj->bWatchListed != 0)
            return false;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        return false;
    // The solar's own visit byte OR the visit-map flags (zero when the id has no entry:
    // FUN_004c4da0 leaves the pre-cleared byte alone); bit 0 means visited.
    unsigned char const OwnVisit = static_cast<CSolar const*>(Object)->GetVisitValue();
    unsigned char MapFlags = 0;
    GetPlayerVisitFlagsRetail(Object->Id, &MapFlags, 0, 0);
    return ((MapFlags | OwnVisit) & 1u) == 0;
}

unsigned char HudObjectVisitFlags(CObject const* Object)
{
    // FUN_004d4d00.
    if (Object == nullptr)
        return 1;
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        return 1;
    CSolar const* const Solar = static_cast<CSolar const*>(Object);
    unsigned char const OwnVisit = Solar->GetVisitValue();
    unsigned char MapFlags = 0;
    GetPlayerVisitFlagsRetail(Solar->Id, &MapFlags, 0, 0);
    return static_cast<unsigned char>(MapFlags | OwnVisit);
}

unsigned HudObjectTypeIds(CSimple const* Object)
{
    // FUN_004d4d50.
    unsigned const Bits = Object->TypeBits;
    if ((Bits & 0x1u) != 0)
        return 0x4a0;
    if ((Bits & 0x2u) != 0)
        return 0x4a1;
    if ((Bits & 0x4u) != 0)
        return 0x4a2;
    if ((Bits & 0x8010u) != 0)
        return 0x4a3;
    if ((Bits & 0x40u) != 0)
        return 0x4a4;
    if ((Bits & 0x80u) != 0)
        return 0x4a5;
    if ((Bits & 0x100u) != 0)
        return 0x4a6;
    return 0x4a7;
}

I3DMathEngine* G_HudObjectsMath = nullptr;

namespace
{
    // DAT_00674b80 / DAT_00674b84 (.data, zero at load; only FUN_004e8100 touches them): the
    // last docking-ring owner base FUN_004e8100 resolved and the ids_name it found for it.
    unsigned G_CaptionOwnerBaseId = 0;
    unsigned G_CaptionOwnerIdsName = 0;

    // The copy every FUN_004e8100 / FUN_005472d0 exit ends with. The reserve is FUN_004142a0
    // (WideTextReserve, font_string.cpp), which retail inlines.
    void HudWideTextAssign(FHudWideText* Out, wchar_t const* Source, size_t Length)
    {
        WideTextReserve(Out, static_cast<unsigned>(Length + 1));
        wcsncpy(Out->Text, Source, Length);
        Out->Text[Length] = 0;
    }

    // FUN_004e8100's exits: the caption built in G_NnTextScratchB goes to Out, true.
    bool HudCaptionFinish(FHudWideText* Out)
    {
        HudWideTextAssign(Out, G_NnTextScratchB, wcslen(G_NnTextScratchB));
        return true;
    }

    // The system's strid_name (ISystem slot 3) and ids 0x561 when the player's system resolves,
    // then the nav-map sector of the object. Retail passes the capacity to FUN_0048dc10 as a
    // third argument it never reads.
    void HudCaptionAppendLocation(CSimple const* Object)
    {
        Universe::ISystem const* const System =
            Universe::get_system(GetPlayerStarSystemIdRetail()); // FUN_004c4830
        if (System != nullptr)
        {
            GetIdsStringWWithRawFallback((unsigned)System->GetStridName(),
                                         G_NnTextScratchA, G_NnTextScratchCapacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
            GetIdsStringWWithRawFallback(0x561, G_NnTextScratchA, G_NnTextScratchCapacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
        }
        NavMapSectorName(&Object->WorldPosition, G_NnTextScratchA); // FUN_0048dc10
        wcscat(G_NnTextScratchB, G_NnTextScratchA);
    }

    // " (%d)" (u" (%d)" 0x005cfb88) for a loot object ((class & 0x803) == 0x803) of more than one unit (CLoot +0xf0).
    void HudCaptionAppendLootCount(CSimple const* Object)
    {
        if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassLoot) != CObject::ClassLoot)
        {
            return;
        }
        unsigned const Units = static_cast<CLoot const*>(Object)->Units;
        if (Units <= 1)
        {
            return;
        }
        wchar_t Count[0x80];
        swprintf(Count, 0x80, L" (%d)", static_cast<int>(Units));
        wcscat(G_NnTextScratchB, Count);
    }

    // "\n" is 0x005d0254.
    bool HudCaptionLocationTail(CSimple const* Object, FHudWideText* Out, int Mode)
    {
        if (Mode == kCaptionNameAndLocation)
        {
            wcscat(G_NnTextScratchB, L"\n");
            HudCaptionAppendLocation(Object);
        }
        else if (Mode == kCaptionLocation)
        {
            HudCaptionAppendLocation(Object);
        }
        return HudCaptionFinish(Out);
    }

    // An object that is not a CEqObj, and (a second retail copy) a CEqObj without a faction:
    // the ids_name as the name line; a loot-type object (TypeBits == 0x10000000) answers only
    // in Mode 0 / 2 and takes the unit count instead of a location.
    bool HudCaptionPlain(CSimple const* Object, unsigned Name, unsigned TypeBits,
                         FHudWideText* Out, int Mode)
    {
        if (Mode == kCaptionName || Mode == kCaptionNameAndLocation)
        {
            GetIdsStringWWithRawFallback(Name, G_NnTextScratchB, G_NnTextScratchCapacity);
        }
        if (TypeBits != OBJ_LOOT)
        {
            return HudCaptionLocationTail(Object, Out, Mode);
        }
        if (Mode != kCaptionName && Mode != kCaptionNameAndLocation)
        {
            return false;
        }
        HudCaptionAppendLootCount(Object);
        return HudCaptionFinish(Out);
    }
}

unsigned GetObjectIdsName(CObject const* Object)
{
    // FUN_005472a0 (cdecl).
    if (Object == nullptr)
    {
        return 0;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
    {
        // No null test on Archetype, as retail.
        return static_cast<unsigned>(Object->Archetype->IdsName);
    }
    return static_cast<CSimple const*>(Object)->get_name();
}

unsigned GetShipPilotName(CObject const* Object, FHudWideText* Out)
{
    // FUN_005472d0. Out = "" (DAT_005c8ce0) first.
    wchar_t const* const Empty = L"";
    HudWideTextAssign(Out, Empty, wcslen(Empty));
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
    {
        wchar_t const* const Pilot = static_cast<CShip const*>(Object)->get_pilot_name();
        if (Pilot != nullptr)
        {
            HudWideTextAssign(Out, Pilot, wcslen(Pilot));
        }
        else
        {
            if (Out->Text == nullptr)
            {
                return 0;
            }
            if (Out->Text[0] != 0)
            {
                Out->Text[0] = 0;
            }
        }
    }
    if (Out->Text == nullptr)
    {
        return 0;
    }
    return static_cast<unsigned>(wcslen(Out->Text));
}

void NavMapSectorName(Vector const* Pos, wchar_t* Out)
{
    // FUN_0048dc10.
    float Scale = 1.0f;
    Universe::ISystem const* const System =
        Universe::get_system(GetPlayerStarSystemIdRetail()); // FUN_004c4830
    if (System != nullptr)
    {
        Scale = System->GetNavMapScale();
    }
    float const RowCoord = Scale * Pos->z + 2700.0f; // DAT_005d2ef4
    float const ColumnCoord = Scale * Pos->x;
    wchar_t Format[0x40];
    GetIdsStringWWithRawFallback(0x54d, Format, 0x40);

    // The first band (from -99999, DAT_005d2ef0, step 33333, DAT_005d2eec) the coordinate lies
    // below, capped at 8; equal and NaN step on.
    int Row = 0;
    float Bound = -99999.0f;
    for (Row = 0; Row < 8; ++Row)
    {
        if (RowCoord < Bound)
        {
            break;
        }
        Bound += 33333.0f;
    }
    int Column = 0;
    Bound = -99999.0f;
    for (Column = 0; Column < 8; ++Column)
    {
        if (ColumnCoord < Bound)
        {
            break;
        }
        Bound += 33333.0f;
    }

    // The legacy unbounded swprintf of msvcrt, as retail (no bound on Out).
    wchar_t RowText[0x10];
    wchar_t ColumnText[0x10];
    GetIdsStringWWithRawFallback(0x515 + Row, RowText, 0x10);
    GetIdsStringWWithRawFallback(0x506 + Column, ColumnText, 0x10);
    _swprintf(Out, Format, RowText, ColumnText);
}

unsigned HudObjectKindIds(unsigned TypeBits)
{
    // FUN_004e8060.
    if ((TypeBits & OBJ_MOON) != 0)
        return 0x563;
    if ((TypeBits & OBJ_PLANET) != 0)
        return 0x44b;
    if ((TypeBits & OBJ_SUN) != 0)
        return 0x564;
    if ((TypeBits & OBJ_BLACKHOLE) != 0)
        return 0x565;
    if ((TypeBits & (OBJ_SATELLITE | OBJ_MISSION_SATELLITE)) != 0)
        return 0x566;
    if ((TypeBits & OBJ_DOCKING_RING) != 0)
        return 0x567;
    if ((TypeBits & OBJ_JUMP_GATE) != 0)
        return 0x568;
    if ((TypeBits & OBJ_TRADELANE_RING) != 0)
        return 0x4fa;
    if ((TypeBits & OBJ_STATION) != 0)
        return 0x44c;
    if ((TypeBits & OBJ_WAYPOINT) != 0)
        return 0x442;
    if ((TypeBits & OBJ_AIRLOCK_GATE) != 0)
        return 0x56a;
    if ((TypeBits & OBJ_JUMP_HOLE) != 0)
        return 0x56b;
    if ((TypeBits & OBJ_WEAPONS_PLATFORM) != 0)
        return 0x56c;
    return (TypeBits & OBJ_DESTROYABLE_DEPOT) != 0 ? 0x56du : 0u;
}

bool HudTradelaneCaption(CSimple const* Object, wchar_t* Out, int Capacity)
{
    // FUN_004e8d30.
    CSolar const* Ring = nullptr;
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
    {
        Ring = static_cast<CSolar const*>(Object);
    }
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr || Ring == nullptr)
    {
        return false;
    }

    // The ring's -z axis (its orientation, +0x08, times (0, 0, -1)), normalised, and the plane
    // offset -(n . ring position).
    Matrix const Orientation = Object->Orientation;
    Vector const Back = {0.0f, 0.0f, -1.0f};
    if (G_HudObjectsMath == nullptr)
    {
        G_HudObjectsMath = Acquire3DMathEngine(); // FUN_005b73e0
    }
    Vector Axis;
    G_HudObjectsMath->TransformMatrix(Axis, Orientation, Back); // vt+0x2c
    // Inline, not FUN_00410190: the reciprocal stays in double. X and Y are rounded to float;
    // Z is too, but the unrounded product feeds the plane offset.
    double const Inverse = 1.0 / sqrt(static_cast<double>(Axis.x) * Axis.x +
                                      static_cast<double>(Axis.y) * Axis.y +
                                      static_cast<double>(Axis.z) * Axis.z);
    float const NormalX = static_cast<float>(Axis.x * Inverse);
    float const NormalY = static_cast<float>(Axis.y * Inverse);
    double const NormalZExact = Inverse * Axis.z;
    float const NormalZ = static_cast<float>(NormalZExact);
    Vector const& RingPos = Object->WorldPosition;
    float const PlaneOffset = static_cast<float>(
        -(NormalZExact * RingPos.z + static_cast<double>(NormalY) * RingPos.y +
          static_cast<double>(NormalX) * RingPos.x));

    // The player's side of that plane.
    Vector const PlayerPos = Player->get_position();
    bool const bSwap =
        (PlayerPos.z * NormalZ + PlayerPos.y * NormalY + PlayerPos.x * NormalX) + PlaneOffset >
        0.0f;

    // Follow PrevRing (+0x1d8) to the first ring of the lane.
    CSolar const* Head = Ring;
    while (Head->PrevRing != 0)
    {
        CObject* const Next = HudCObjectOf(ClientObjDb_FindObject(Head->PrevRing, 0));
        if (Next == nullptr || (static_cast<unsigned>(Next->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        {
            return false;
        }
        Head = static_cast<CSolar const*>(Next);
    }
    unsigned First = Head->get_tradelane_space_name();

    // And NextRing (+0x1dc) to the last.
    CSolar const* Tail = Ring;
    while (Tail->NextRing != 0)
    {
        CObject* const Next = HudCObjectOf(ClientObjDb_FindObject(Tail->NextRing, 0));
        if (Next == nullptr || (static_cast<unsigned>(Next->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        {
            return false;
        }
        Tail = static_cast<CSolar const*>(Next);
    }
    unsigned Second = Tail->get_tradelane_space_name();
    if (First == 0 || Second == 0)
    {
        return false;
    }
    if (bSwap)
    {
        unsigned const Swap = First;
        First = Second;
        Second = Swap;
    }

    // First "->" Second, refused when the running length (+2 for the arrow) reaches Capacity
    // (signed compare).
    wchar_t Text[0x100];
    GetIdsStringWWithRawFallback(First, Text, 0xff);
    int const FirstLength = static_cast<int>(wcslen(Text)) + 2;
    if (FirstLength >= Capacity)
    {
        return false;
    }
    wcscpy(Out, Text);
    wcscat(Out, L"->"); // DAT_005cef58
    GetIdsStringWWithRawFallback(Second, Text, 0xff);
    if (static_cast<int>(wcslen(Text)) + FirstLength >= Capacity)
    {
        return false;
    }
    wcscat(Out, Text);
    return true;
}

bool HudBuildObjectCaption(CSimple const* Object, FHudWideText* Out, int Mode,
                           bool bFactionShortName)
{
    // FUN_004e8100.
    wchar_t* const Line = G_NnTextScratchB;    // DAT_0066fc60
    wchar_t* const Scratch = G_NnTextScratchA; // DAT_0066dc60

    unsigned const TypeBits = Object->TypeBits;
    unsigned const Name = GetObjectIdsName(Object); // FUN_005472a0
    Line[0] = 0;
    unsigned char bWatched = 0;
    FClientGameobj* const Gameobj = ClientObjDb_FindObject(Object->Id, 0); // FUN_005416c0
    if (Gameobj != nullptr && (TypeBits & OBJ_SOLAR_NO_WAYPOINT_MASK) != 0)
    {
        bWatched = Gameobj->bWatchListed;
    }

    if (((TypeBits & OBJ_MISSION_SATELLITE) != 0 || bWatched != 0) && Name != 0)
    {
        if (Mode == kCaptionName || Mode == kCaptionNameAndLocation)
        {
            GetIdsStringWWithRawFallback(Name, Line, G_NnTextScratchCapacity);
        }
        return HudCaptionLocationTail(Object, Out, Mode);
    }

    // Anything but a CEqObj.
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
    {
        if (Name == 0)
        {
            return false;
        }
        return HudCaptionPlain(Object, Name, TypeBits, Out, Mode);
    }

    // The faction of the object's reputation instance (+0x104).
    CEqObj const* const EqObj = static_cast<CEqObj const*>(Object);
    int const VibeId = static_cast<int>(EqObj->VibeId);
    unsigned Affiliation = 0;
    if (Reputation::Vibe::GetAffiliation(VibeId, Affiliation, false) != 0 ||
        Affiliation == 0xffffffffu)
    {
        if (Name == 0)
        {
            return false;
        }
        if ((TypeBits & OBJ_SHIP_MASK) == 0)
        {
            return HudCaptionPlain(Object, Name, TypeBits, Out, Mode);
        }
        if (Mode != kCaptionLocation && Mode != kCaptionNameAndLocation)
        {
            return false;
        }
        // A player's display name, when the instance has one, is the whole caption. Retail
        // points GetName's out argument at the Name slot (+0x4c); GetName always writes it on
        // success, so the initial value is never read.
        FmtStr Name1;
        FmtStr Name2;
        wchar_t const* DisplayName = nullptr;
        if (Reputation::Vibe::GetName(VibeId, Name1, Name2, DisplayName) == 0 &&
            EqObj->OwnerPlayer != 0 && DisplayName != nullptr)
        {
            HudWideTextAssign(Out, DisplayName, wcslen(DisplayName));
            return true;
        }
        return false;
    }

    unsigned const ShortName = Reputation::get_short_name(Affiliation);
    if ((TypeBits & OBJ_SHIP_MASK) != 0)
    {
        // The instance's two FmtStr names.
        FmtStr Name1;
        FmtStr Name2;
        wchar_t const* DisplayName = nullptr; // GetName writes it on success
        if (Reputation::Vibe::GetName(VibeId, Name1, Name2, DisplayName) == 0)
        {
            bool const bNoName1 = Name1.MadLibId == 0;
            if (Mode == kCaptionName || Mode == kCaptionNameAndLocation)
            {
                if (!bNoName1)
                {
                    FmtStrSprintf(&Name1, Line, G_NnTextScratchCapacity); // FUN_0041c970
                }
                if (Mode == kCaptionNameAndLocation && !bNoName1)
                {
                    wcscat(Line, L"\n");
                }
            }
            if (Mode == kCaptionLocation || Mode == kCaptionNameAndLocation)
            {
                if (EqObj->OwnerPlayer != 0 && DisplayName != nullptr)
                {
                    wcscat(Line, DisplayName);
                }
                else
                {
                    if (bNoName1 && ShortName != 0 && ShortName != 0xffffffffu &&
                        bFactionShortName)
                    {
                        wchar_t Short[0x80];
                        if (GetIdsStringWWithRawFallback(ShortName, Short, 0x80) == 0)
                        {
                            Short[0] = 0;
                        }
                        if (wcslen(Short) > 1 || Short[0] != L' ')
                        {
                            wcscat(Line, Short);
                            wcscat(Line, L" - "); // DAT_005d01f4
                        }
                    }
                    FmtStrSprintf(&Name2, Scratch, G_NnTextScratchCapacity); // FUN_0041c970
                    wcscat(Line, Scratch);
                }
            }
            return HudCaptionFinish(Out);
        }
    }

    // Docked at a base (+0x160) with a faction short name: the short name + ids 0x562 as the
    // name line, the ids_name as the location line.
    if (EqObj->DockingBaseId != 0 && ShortName != 0)
    {
        if (Mode == kCaptionName || Mode == kCaptionNameAndLocation)
        {
            GetIdsStringWWithRawFallback(ShortName, Line, G_NnTextScratchCapacity);
            GetIdsStringWWithRawFallback(0x562, Scratch, G_NnTextScratchCapacity);
            wcscat(Line, Scratch);
            if (Mode == kCaptionNameAndLocation)
            {
                wcscat(Line, L"\n");
            }
        }
        if (Mode == kCaptionLocation || Mode == kCaptionNameAndLocation)
        {
            GetIdsStringWWithRawFallback(Name, Scratch, G_NnTextScratchCapacity);
            wcscat(Line, Scratch);
        }
        return HudCaptionFinish(Out);
    }

    // The object-kind ids, else the ids_name, as the second word.
    unsigned const KindIds = HudObjectKindIds(TypeBits);
    unsigned const SecondIds = KindIds != 0 ? KindIds : Name;
    if (Mode == kCaptionName || Mode == kCaptionNameAndLocation)
    {
        // The first word is the faction short name, or for TypeBits
        // 0x20 the ids_name of the solar whose client gameobj owns the base at +0x164 (bucket 2
        // walked through get_base, slot 35; the owner through cobject, slot 84), cached.
        unsigned FirstIds = ShortName;
        if ((TypeBits & OBJ_DOCKING_RING) != 0)
        {
            unsigned const OwnerBase = EqObj->DockingPointBaseId;
            if (OwnerBase != 0)
            {
                if (OwnerBase == G_CaptionOwnerBaseId)
                {
                    FirstIds = G_CaptionOwnerIdsName;
                }
                else
                {
                    FClientGameobj* Scan = nullptr;
                    for (;;)
                    {
                        int Kind = 2; // re-stored every pass, as retail
                        Scan = Scan != nullptr ? Scan->ListNext : ClientObjDb_GetKindHead(&Kind);
                        if (Scan == nullptr)
                        {
                            break;
                        }
                        // Retail reuses the short-name stack slot for this; the short name
                        // is already held elsewhere and the slot is not read again.
                        unsigned Base = 0;
                        Scan->get_base(Base);
                        if (Base != OwnerBase)
                        {
                            continue;
                        }
                        CObject const* const Owner = Scan->cobject();
                        if (Owner != nullptr &&
                            (static_cast<unsigned>(Owner->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
                        {
                            FirstIds = GetObjectIdsName(Owner);
                            G_CaptionOwnerBaseId = OwnerBase;
                            G_CaptionOwnerIdsName = FirstIds;
                        }
                        break;
                    }
                }
            }
        }
        GetIdsStringWWithRawFallback(FirstIds, Line, G_NnTextScratchCapacity);
        GetIdsStringWWithRawFallback(SecondIds, Scratch, G_NnTextScratchCapacity);
        wcscat(Line, L" "); // DAT_005cef90
        wcscat(Line, Scratch);
    }

    // A loot-type object answers only in Mode 0 / 2, with its count.
    if (TypeBits == OBJ_LOOT)
    {
        if (Mode != kCaptionName && Mode != kCaptionNameAndLocation)
        {
            return false;
        }
        HudCaptionAppendLootCount(Object);
        return HudCaptionFinish(Out);
    }

    if (Mode == kCaptionNameAndLocation)
    {
        wcscat(Line, L"\n");
    }
    else if (Mode != kCaptionLocation)
    {
        return HudCaptionFinish(Out);
    }

    // A trade-lane ring (TypeBits bit 7) with an "A->B" caption.
    if ((TypeBits & OBJ_TRADELANE_RING) != 0 &&
        HudTradelaneCaption(Object, Scratch, G_NnTextScratchCapacity))
    {
        wcscat(Line, Scratch);
        return HudCaptionFinish(Out);
    }
    if ((TypeBits & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING | OBJ_JUMP_HOLE)) != 0)
    {
        GetIdsStringWWithRawFallback(Name, Scratch, G_NnTextScratchCapacity);
        wcscat(Line, Scratch);
        return HudCaptionFinish(Out);
    }
    HudCaptionAppendLocation(Object);
    return HudCaptionFinish(Out);
}

void HudReputationColor(unsigned long* OutArgb, IObjRW* Player, IObjRW* Other)
{
    // FUN_0045a4e0.
    if (Other == nullptr || Player == nullptr)
    {
        *OutArgb = G_NnPaletteWhite;
        return;
    }

    FClientGameobj* const Gameobj = ClientObjDb_FindObject(Other->get_id(), 1);
    if (Gameobj != nullptr)
    {
        // Ship gameobj byte +0x18c bit 2 (bucket 1 holds ships only). No instruction in
        // freelancer.exe sets bit 2 (the other writers touch bits 0, 1 and 3 only).
        if ((static_cast<FClientShipGameobj*>(Gameobj)->ShipFlags & 4) != 0)
        {
            *OutArgb = G_NnPalettePurple;
            return;
        }
        // A player ship with a non-zero group id that the player's ship shares.
        if (Gameobj->is_player())
        {
            CObject* const Object = HudCObjectOf(Gameobj);
            if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip &&
                HudShipGroupId(Object) != 0)
            {
                IObjRW* const PlayerRw = static_cast<IObjRW*>(GetPlayerObjRW());
                if (PlayerRw != nullptr)
                {
                    // A player object that is not a ship gives null and retail reads
                    // [0 + 0x1b4]; the player in space is always a ship, and the port keeps
                    // that read unguarded, as retail does.
                    CObject* PlayerShip = HudCObjectOf(static_cast<IObjInspectImpl*>(PlayerRw));
                    if (PlayerShip != nullptr &&
                        (static_cast<unsigned>(PlayerShip->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
                    {
                        PlayerShip = nullptr;
                    }
                    if (HudShipGroupId(PlayerShip) == HudShipGroupId(Object))
                    {
                        *OutArgb = G_NnPalettePink;
                        return;
                    }
                }
            }
        }
    }

    // Other's attitude towards the player (vt+0x68); a failed query is white.
    float Attitude = 0.0f;
    if (Other->get_attitude_towards(Attitude, Player) != 0)
    {
        *OutArgb = G_NnPaletteWhite;
        return;
    }
    if (Attitude <= -0.6f)
    {
        *OutArgb = G_NnPaletteRed;
        return;
    }
    // -0.2f is DAT_005ce7f0. NaN ends white; both white exits are kept as retail spells them.
    if (Attitude <= -0.2f)
    {
        *OutArgb = G_NnPaletteWhite;
        return;
    }
    if (!(Attitude >= 0.6f))
    {
        *OutArgb = G_NnPaletteWhite;
        return;
    }
    *OutArgb = G_NnPaletteGreen;
}

int HudReputationBand(float Attitude)
{
    // FUN_0045a6e0. Bounds DAT_005ce7ec / DAT_005ce7f0 / DAT_005ce7f8 / DAT_005ce7f4; NaN
    // falls through to 2.
    if (Attitude <= -0.6f)
        return 0;
    if (Attitude <= -0.2f)
        return 1;
    if (Attitude >= 0.6f)
        return 4;
    if (Attitude > 0.2f)
        return 3;
    return 2;
}

// =============================================================================================
// HudTargetBase
// =============================================================================================

HudTargetBase::HudTargetBase()
    : ViewObject()
    , PartCache()
    , bSubTargetPickEnabled(0)
    , ViewPanel(nullptr)
    , bExpanded(0)
{
    // FUN_004e6020; the GameCamera ctor is FUN_0051b2f0.
    GameCameraConstruct(&ViewCamera);
    SubTargetId = SUBOBJ_ID_NONE;
}

HudTargetBase::~HudTargetBase()
{
    // FUN_004da630: the camera's watcher cleared, then the part cache; the rest is member and
    // base destructors. Retail inlines `if (watched != 0) set_pointer(0)`; set_pointer is
    // protected in Common, and the same test-and-call is the body of Common's public
    // BaseWatcher::set, which the port calls.
    ViewCamera.Watched.set(nullptr);
    PartCacheClear(&PartCache);
}

void HudTargetBase::SetViewObject(IObjRW* Target)
{
    // LAB_004e6270. The client object (FUN_005416c0, kind 0) of Target's id, kept only when its
    // CObject has class bits 3 (& 3 == 3).
    IObjRW* Found = nullptr;
    if (Target != nullptr)
    {
        FClientGameobj* const Gameobj = ClientObjDb_FindObject(Target->get_id(), 0);
        Found = Gameobj;
        if (Gameobj != nullptr)
        {
            CObject const* const Object = Gameobj->cobject();
            if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
            {
                Found = nullptr;
            }
        }
    }

    IObjRW* const Current = ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target()));
    if (Current == Found)
    {
        return;
    }
    // Retail's inlined test-and-set_pointer; see the destructor for why the port calls set.
    Watchable const* const NewWatched = (Found != nullptr) ? static_cast<Watchable*>(Found) : nullptr;
    ViewObject.set(NewWatched);
    OnViewObjectChanged();

    IObjRW* const Viewed = ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target()));
    if (Viewed == nullptr)
    {
        PartCacheClear(&PartCache);
        return;
    }

    // Retail calls slot 4 (+0x10) get_orientation, whose body is ICF-folded with slot 3
    // get_transform -- the pointer is the object's whole Transform, and FUN_004e65d0 uses it as
    // one. The port asks for the transform by its own name.
    Transform const* const Xform = &Viewed->get_transform();
    const InstanceIndex Instance = Viewed->get_index();
    FHudInstanceBounds Bounds(Instance, Xform, false);

    Vector Center;
    ViewCenter = *Bounds.GetCenter(&Center);

    // Retail stores |Max - Center| (FUN_00423b60, FUN_00410190) to ViewRadius and then
    // overwrites it below.
    float Reach[3];
    Reach[0] = Bounds.Max.x - ViewCenter.x;
    Reach[1] = Bounds.Max.y - ViewCenter.y;
    Reach[2] = Bounds.Max.z - ViewCenter.z;
    ViewRadius = static_cast<float>(Vec3Length(Reach));

    // The X span wins only when strictly larger (NaN takes Z); * 0.5 is DAT_005c75e8, double.
    float const SpanX = Bounds.Max.x - Bounds.Min.x;
    float const SpanZ = Bounds.Max.z - Bounds.Min.z;
    float const Span = (SpanX > SpanZ) ? SpanX : SpanZ;
    ViewRadius = static_cast<float>(static_cast<double>(Span) * 0.5);

    // The chrome colour DAT_00679bb4 dimmed to 70 % per byte (byte 0 clamped inline,
    // bytes 1..3 through FUN_004104c0), packed as byte3 << 24 | byte0 << 16 | byte1 << 8 | byte2.
    unsigned char const* const Chrome = G_HudChromeColorRgba;
    unsigned const Byte0 = static_cast<unsigned>(ClampByte(DimChromeByte(Chrome[0])));
    unsigned const Byte1 = static_cast<unsigned>(ClampByte(DimChromeByte(Chrome[1])));
    unsigned const Byte2 = static_cast<unsigned>(ClampByte(DimChromeByte(Chrome[2])));
    unsigned const Byte3 = static_cast<unsigned>(ClampByte(DimChromeByte(Chrome[3])));
    unsigned const Tag = (Byte3 << 24) | (Byte0 << 16) | (Byte1 << 8) | Byte2;

    // get_index again; the renderer LODs pinned to 3 while the cache is built, then put back
    // (FUN_00402580 / FUN_00405b10 / FUN_00402720).
    const InstanceIndex BuildInstance = Viewed->get_index();
    FRendererLodMap SavedLods;
    BindRendererLods(BuildInstance, 3, &SavedLods);
    PartCacheBuild(&PartCache, BuildInstance, Tag);
    ApplyRendererDetailQueue(BuildInstance, &SavedLods);
    HideGroupAndEquipParts();
}

void HudTargetBase::ToggleExpanded()
{
    // FUN_00401e90.
}

void HudTargetBase::HideExpandedControls(bool /*bHide*/)
{
    // FUN_0053e4a0.
}

void HudTargetBase::DrawViewObject()
{
    // FUN_00401e90.
}

void HudTargetBase::UpdateLabels()
{
    // FUN_00401e90.
}

void HudTargetBase::OnViewObjectChanged()
{
    // FUN_00401e90.
}

CObject* HudTargetBase::GetViewCObject() const
{
    return CObjectOfWatcher(ViewObject);
}

void HudTargetBase::HideGroupAndEquipParts()
{
    // FUN_004e6160: only for a CEqObj (class & 0x103 == 0x103).
    CObject* const Object = GetViewCObject();
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
    {
        return;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);

    // Every collision group root, without recursion; the root index is not tested against -1.
    CArchGrpTraverser GroupWalk;
    CArchGroup* Group = EqObj->ArchGroups.Traverse(GroupWalk);
    while (Group != nullptr)
    {
        PartCacheMarkHidden(&PartCache, Group->GetRootIndex(), false);
        Group = EqObj->ArchGroups.Traverse(GroupWalk);
    }

    // Every attached equipment (mask 0x1fe0) root other than -1, with recursion.
    CEquipTraverser EquipWalk(0x1fe0);
    CEquip* Equip = EqObj->EquipManager.Traverse(EquipWalk);
    while (Equip != nullptr)
    {
        // vt+0x84 CAttachedEquip::GetRootIndex, called on every traversed item.
        long const Root = static_cast<CAttachedEquip*>(Equip)->GetRootIndex();
        if (Root != -1)
        {
            PartCacheMarkHidden(&PartCache, Root, true);
        }
        Equip = EqObj->EquipManager.Traverse(EquipWalk);
    }
}

namespace
{
    // DAT_006127b4 -- the view camera's half fov x (0x420c0000, read-only in the PE).
    float const kViewHalfFovX = 35.0f;

    // The part tag both colouring passes build from a colour's four memory bytes.
    unsigned PackPartTag(unsigned long Colour)
    {
        unsigned const Byte0 = static_cast<unsigned>(Colour & 0xffu);
        unsigned const Byte1 = static_cast<unsigned>((Colour >> 8) & 0xffu);
        unsigned const Byte2 = static_cast<unsigned>((Colour >> 16) & 0xffu);
        unsigned const Byte3 = static_cast<unsigned>((Colour >> 24) & 0xffu);
        return (Byte3 << 24) | (Byte0 << 16) | (Byte1 << 8) | Byte2;
    }

    unsigned long ChromeColourArgb()
    {
        unsigned long Colour = 0;
        memcpy(&Colour, G_HudChromeColorRgba, sizeof(G_HudChromeColorRgba)); // DAT_00679bb4
        return Colour;
    }
}

void HudTargetBase::SetupViewCamera(bool bFixedOrbit, float ScreenShift)
{
    // FUN_004e70e0. The hit box's top-left and bottom-right corners through the Win3D camera.
    Vector Corner0;
    Corner0.x = HitCenter.x - HitHalfWidth;
    Corner0.y = HitCenter.y + HitHalfHeight;
    Corner0.z = HitCenter.z;
    Vector Corner1;
    Corner1.x = HitCenter.x + HitHalfWidth;
    Corner1.y = HitCenter.y - HitHalfHeight;
    Corner1.z = HitCenter.z;
    int Box[4];
    int const* const Projected = HudProjectScreenRect(Box, &Corner0, &Corner1);
    int const Left = Projected[0];
    int const Top = Projected[1];
    int const Right = Projected[2];
    int const Bottom = Projected[3];

    // Moved ScreenShift Win3D-camera widths sideways (DAT_0067dca4 / DAT_0067dcac, the left and
    // right of that camera's viewport), then inset by an eighth of the box: its width on the
    // left and the right, its height at the top -- and its WIDTH again at the bottom (retail
    // quirk: it subtracts the width term still on the FPU stack). The products and sums stay in
    // double, as on the x87 stack.
    int const* const Screen = Win3DGetCamera()->Viewport;
    double const ScreenWidth = static_cast<double>(Screen[2] - Screen[0] + 1);
    double const WidthInset = static_cast<double>(Right - Left) * 0.125;
    double const HeightInset = static_cast<double>(Bottom - Top) * 0.125;
    int const ShiftedLeft =
        static_cast<int>(ScreenWidth * static_cast<double>(ScreenShift) + static_cast<double>(Left));
    int const ShiftedRight =
        static_cast<int>(ScreenWidth * static_cast<double>(ScreenShift) + static_cast<double>(Right));
    int Viewport[4];
    Viewport[0] = static_cast<int>(static_cast<double>(ShiftedLeft) + WidthInset);
    Viewport[1] = static_cast<int>(HeightInset + static_cast<double>(Top));
    Viewport[2] = static_cast<int>(static_cast<double>(ShiftedRight) - WidthInset);
    Viewport[3] = static_cast<int>(static_cast<double>(Bottom) - WidthInset);
    SpaceCameraSetViewportEx(&ViewCamera, Viewport, 1);

    // DAT_00674b08 -- 1 / tan(35 degrees), computed once (bit 0 of DAT_00674b0c); the degree
    // factor 0.0174533f is DAT_005c77f4.
    static float const InvTanHalfFov = static_cast<float>(
        1.0 / tan(static_cast<double>(kViewHalfFovX) * static_cast<double>(0.0174533f)));
    SpaceCameraSetHalfFovX(&ViewCamera, kViewHalfFovX);

    IObjRW* const Viewed = ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target()));
    Transform Xform;
    if (!bFixedOrbit)
    {
        // ViewRadius * 1.1 (DAT_005d8468) / tan(35) away from the object's centre.
        float const Distance = static_cast<float>(static_cast<double>(InvTanHalfFov) *
                                                  static_cast<double>(ViewRadius) *
                                                  static_cast<double>(1.1f));
        Win3DIdentityRotation3x3(&Xform.Rotation);
        Xform.Translation.x = 0.0f;
        Xform.Translation.y = 0.0f;
        Xform.Translation.z = 0.0f;

        // The HUDItem target (+0x28) when it is a ship, else null -- which retail does not test:
        // GetDirectionMatrix is handed null + 8 (EngineObject::get_transform inlined).
        CObject* Ship = CObjectOfWatcher(HUDItem::Target);
        if (Ship != nullptr && (static_cast<unsigned>(Ship->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        {
            Ship = nullptr;
        }
        Matrix const Facing = GetDirectionMatrix(Ship->get_transform(), Viewed->get_position());
        memcpy(&Xform.Rotation, &Facing, sizeof(Matrix));

        // Slot 4 (+0x10), ICF-folded with get_transform -- see SetViewObject.
        Transform const* const ViewXform = &Viewed->get_transform();
        Vector Center;
        HudTransformPoint(ViewXform, &Center, &ViewCenter);
        float const Axis[3] = {Xform.Rotation.E[0][2], Xform.Rotation.E[1][2],
                               Xform.Rotation.E[2][2]};
        float Offset[3];
        ScaleVec3(Offset, Axis, Distance);
        // Loot (class & 0x803) stands four times as far (DAT_005cfc9c).
        CObject* const ViewedObject = GetViewCObject();
        if (ViewedObject != nullptr &&
            (static_cast<unsigned>(ViewedObject->ObjectClass) & CObject::ClassLoot) == CObject::ClassLoot)
        {
            Offset[0] = Offset[0] * 4.0f;
            Offset[1] = Offset[1] * 4.0f;
            Offset[2] = Offset[2] * 4.0f;
        }
        Xform.Translation.x = Center.x + Offset[0];
        Xform.Translation.y = Center.y + Offset[1];
        Xform.Translation.z = Center.z + Offset[2];
    }
    else
    {
        // The object's own orientation (slot 3, +0xc) with its y / z columns turned
        // by -90 degrees (the double DAT_005d8ec0 = (double)-1.57079637f): per row,
        // m1' = m1 cos + sin m2 and m2' = cos m2 - m1 sin.
        Transform const* const ObjectXform = &Viewed->get_transform();
        Xform.Rotation = ObjectXform->Rotation;
        Xform.Translation.x = 0.0f;
        Xform.Translation.y = 0.0f;
        Xform.Translation.z = 0.0f;
        double const Angle = static_cast<double>(-1.57079637f);
        double const Cos = cos(Angle);
        double const Sin = sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            double const M1 = static_cast<double>(Xform.Rotation.E[Row][1]);
            double const M2 = static_cast<double>(Xform.Rotation.E[Row][2]);
            Xform.Rotation.E[Row][1] = static_cast<float>(M1 * Cos + Sin * M2);
            Xform.Rotation.E[Row][2] = static_cast<float>(Cos * M2 - M1 * Sin);
        }

        float const Distance = static_cast<float>(static_cast<double>(InvTanHalfFov) *
                                                  static_cast<double>(ViewRadius) *
                                                  static_cast<double>(1.1f));
        // Slot 4 (+0x10) through DAT_00674b1c directly (FUN_004e7090 inlined).
        Transform const* const ViewXform = &Viewed->get_transform();
        Vector Center;
        HudTargetMath()->TransformXform(Center, *ViewXform, ViewCenter);
        float const OffsetX = Xform.Rotation.E[0][2] * Distance;
        float const OffsetY = Xform.Rotation.E[1][2] * Distance;
        float const OffsetZ = Xform.Rotation.E[2][2] * Distance;
        Xform.Translation.x = Center.x + OffsetX;
        Xform.Translation.y = Center.y + OffsetY;
        Xform.Translation.z = Center.z + OffsetZ;
    }

    ViewCamera.bViewDirty = 1;
    memcpy(&ViewCamera.World, &Xform.Rotation, sizeof(ViewCamera.World));
    memcpy(&ViewCamera.WorldPos, &Xform.Translation, sizeof(ViewCamera.WorldPos));
    Win3DSetViewport(AsCamera(&ViewCamera),
                     "E:\\FL\\Scratch\\Source\\Client\\Space\\HUD_Target_Base.cpp", 0x16b);
    Win3DApplyCameraPerspective(AsCamera(&ViewCamera));
    bSubTargetPickEnabled = 1;
}

void HudTargetBase::ColourViewParts()
{
    // FUN_004e7070.
    PartCacheClearHiddenActive(&PartCache);
    ColourGroupParts();
    ColourEquipParts();
}

void HudTargetBase::ColourGroupParts()
{
    // FUN_004e6cb0: only for a CEqObj (class & 0x103 == 0x103).
    CObject* const Object = GetViewCObject();
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
    {
        return;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);

    CArchGrpTraverser GroupWalk;
    CArchGroup* Group = EqObj->ArchGroups.Traverse(GroupWalk);
    while (Group != nullptr)
    {
        // The chrome colour, or the damage colour while damaged.
        unsigned long Colour = ChromeColourArgb();
        float const Damage = 1.0f - Group->GetRelativeHealth();
        if (Damage > 0.0f)
        {
            unsigned long Damaged = 0;
            Colour = *HudDamageColorArgb(&Damaged, Damage);
        }

        unsigned long const Dim0 = static_cast<unsigned long>(
            ClampByte(DimChromeByte(static_cast<unsigned char>(Colour))));
        unsigned long const Dim1 = static_cast<unsigned long>(
            ClampByte(DimChromeByte(static_cast<unsigned char>(Colour >> 8))));
        unsigned long const Dim2 = static_cast<unsigned long>(
            ClampByte(DimChromeByte(static_cast<unsigned char>(Colour >> 16))));
        unsigned long const Dim3 = static_cast<unsigned long>(
            ClampByte(DimChromeByte(static_cast<unsigned char>(Colour >> 24))));
        Colour = Dim0 | (Dim1 << 8) | (Dim2 << 16) | (Dim3 << 24);

        // The sub-target pulses (FUN_00561f20) unless the mouse may not pick.
        if (SubTargetId == Group->GetID() && !HudIsMousePickBlocked())
        {
            unsigned long Pulsed = 0;
            NnUiPulseBlendFromArgb(&Pulsed, Colour);
            Colour = Pulsed;
        }

        // Re-activated without recursion; the root index is not tested against -1.
        PartCacheActivatePart(&PartCache, Group->GetRootIndex(), false,
                              PackPartTag(Colour));
        Group = EqObj->ArchGroups.Traverse(GroupWalk);
    }
}

void HudTargetBase::ColourEquipParts()
{
    // FUN_004e6ec0: only for a CEqObj (class & 0x103 == 0x103).
    CObject* const Object = GetViewCObject();
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
    {
        return;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);

    // Retail calls FUN_00548eb0 here and ignores the result.
    IsPlayerShipId(EqObj->Id);

    CEquipTraverser EquipWalk(0x1fe0);
    CEquip* Equip = EqObj->EquipManager.Traverse(EquipWalk);
    while (Equip != nullptr)
    {
        unsigned long Colour = G_NnPaletteBlue;
        float const Damage = 1.0f - Equip->GetRelativeHealth();
        if (Damage > 0.0f)
        {
            unsigned long Damaged = 0;
            Colour = *HudDamageColorArgb(&Damaged, Damage);
        }

        // A launcher that is a gun without auto_turret (Gun +0xba): amber while active, else the
        // chrome colour -- in place of the damage colour.
        if (CELauncher::cast(Equip) != nullptr)
        {
            CEGun* const Gun = CEGun::cast(Equip);
            if (Gun != nullptr)
            {
                Archetype::Gun const* const Arch = Gun->GunArch();
                if (Arch != nullptr && !Arch->bAutoTurret)
                {
                    Colour = Equip->IsActive() ? G_NnPaletteAmber : ChromeColourArgb();
                }
            }
        }

        if (SubTargetId == Equip->Id && !HudIsMousePickBlocked())
        {
            unsigned long Pulsed = 0;
            NnUiPulseBlendFromArgb(&Pulsed, Colour);
            Colour = Pulsed;
        }

        // Re-activated with recursion.
        long const Root = static_cast<CAttachedEquip*>(Equip)->GetRootIndex();
        if (Root != -1)
        {
            PartCacheActivatePart(&PartCache, Root, true, PackPartTag(Colour));
        }
        Equip = EqObj->EquipManager.Traverse(EquipWalk);
    }
}

void HudTargetBase::UpdateSubTargetToolTip()
{
    // FUN_004e75b0.
    if (HudIsMousePickBlocked())
    {
        SubTargetId = SUBOBJ_ID_NONE;
        return;
    }

    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ); // DAT_00616840 / DAT_00616844
    unsigned short PickedSub = 0;
    int PickedCount = 0;
    unsigned const Ids = PickSubTarget(CursorX, CursorY, &PickedSub, &PickedCount);
    if (Ids == 0)
    {
        SubTargetId = SUBOBJ_ID_NONE;
        return;
    }
    SubTargetId = PickedSub;

    if (PickedCount > 1)
    {
        wchar_t Name[0x40];
        if (GetIdsStringWWithRawFallback(Ids, Name, 0x40) == 0)
        {
            Name[0] = 0;
        }
        wchar_t Text[0x80];
        swprintf(Text, 0x80, L"(%d) %s", PickedCount, Name);

        // The hover record: key = SubTargetId, no tooltip id, caption = Text. Retail leaves
        // +0x48..+0x5f as stack garbage; the port zeroes them (same as WinControl::ShowToolTip).
        // The caption zeroing and FUN_004144b0 are the record's constructor.
        FToolTipHoverInfo Info;
        Info.IdsAlt = 0;
        Info.Ids = -1;
        Info.AuxField = 0;
        // FUN_00414280 -- clear the (still null) caption.
        if (Info.CaptionW != nullptr && Info.CaptionW[0] != 0)
        {
            Info.CaptionW[0] = 0;
        }
        Info.bLive = 0;
        Info.Ids = static_cast<int>(SubTargetId);

        size_t const Length = wcslen(Text);
        unsigned const Need = static_cast<unsigned>(Length + 1);
        if (Need > Info.CaptionCapacity)
        {
            // FUN_004142a0
            if (Info.CaptionW != nullptr)
            {
                Info.CaptionW = static_cast<wchar_t*>(realloc(Info.CaptionW, Need * sizeof(wchar_t)));
            }
            else
            {
                Info.CaptionW = static_cast<wchar_t*>(malloc(Need * sizeof(wchar_t)));
                Info.CaptionW[0] = 0;
            }
            Info.CaptionCapacity = Need;
        }
        wcsncpy(Info.CaptionW, Text, Length);
        Info.CaptionW[Length] = 0;
        NnToolTipFeedHover(&Info);
        return;
    }

    // One part: key = SubTargetId, tooltip id = the part's ids_name, no caption.
    FToolTipHoverInfo Info;
    Info.IdsAlt = 0;
    Info.Ids = -1;
    Info.AuxField = 0;
    Info.bLive = 0;
    Info.Ids = static_cast<int>(SubTargetId);
    Info.IdsAlt = static_cast<int>(Ids);
    NnToolTipFeedHover(&Info);
}

unsigned HudTargetBase::PickSubTarget(int X, int Y, unsigned short* OutSubId, int* OutCount)
{
    // FUN_004e79f0.
    *OutSubId = 0;
    *OutCount = 0;
    if (bSubTargetPickEnabled == 0)
    {
        return 0;
    }
    CObject* const Object = GetViewCObject();
    if (Object == nullptr)
    {
        return 0;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
    {
        return 0;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);

    // The view camera's unprojection of (X, Y) (slot 0x38), offset by its position (slot 0x0c).
    float const ScreenX = static_cast<float>(X);
    float const ScreenY = static_cast<float>(Y);
    ICamera* const Camera = AsCamera(&ViewCamera);
    Vector Unprojected;
    Camera->Unproject(&Unprojected, ScreenX, ScreenY);
    const Vector* const CameraPos = Camera->GetPosition();
    Vector RayEnd;
    RayEnd.x = Unprojected.x + CameraPos->x;
    RayEnd.y = Unprojected.y + CameraPos->y;
    RayEnd.z = Unprojected.z + CameraPos->z;

    // The same screen point on the Win3D camera's near plane (DAT_0067dbf8: +0x68 / +0x6c
    // centre, +0x78 / +0x7c scales, +0xa4 near), rotated by its orientation (+0x04) -- the
    // direction the WinControl face's HitTest (slot 4) takes.
    FSpaceCamera const* const UiCamera = Win3DGetCamera();
    Vector Near;
    Near.x = static_cast<float>((static_cast<double>(ScreenX) - UiCamera->ScreenCenterX) *
                                (static_cast<double>(UiCamera->ZNear) / UiCamera->XScale));
    Near.y = static_cast<float>((static_cast<double>(ScreenY) - UiCamera->ScreenCenterY) *
                                (static_cast<double>(UiCamera->ZNear) / UiCamera->YScale));
    Near.z = -UiCamera->ZNear;
    Matrix Orientation;
    memcpy(&Orientation, &UiCamera->World, sizeof(Orientation));
    Vector Direction;
    HudTargetMath()->TransformMatrix(Direction, Orientation, Near);
    if (!HitTest(&Direction.x))
    {
        return 0;
    }

    FHudSpherePick Pick(1);
    Pick.Setup(reinterpret_cast<Vector const*>(Camera->GetPosition()), &RayEnd);

    unsigned Result = 0;

    // Attached equipment (mask 0x1ee0).
    CEquipTraverser EquipWalk(0x1ee0);
    CEquip* Equip = EqObj->EquipManager.Traverse(EquipWalk);
    while (Equip != nullptr)
    {
        CAttachedEquip* const Attached = static_cast<CAttachedEquip*>(Equip);
        if (Attached->IsConnected() &&
            Pick.Test(Attached->GetRootIndex(), kSubTargetPickSlack))
        {
            *OutSubId = Equip->Id;
            Result = static_cast<unsigned>(Equip->Arch->IdsName);
            CELauncher* const Launcher = CELauncher::cast(Equip);
            *OutCount = (Launcher != nullptr) ? Launcher->GetAmmoCount() : 1;
        }
        Equip = EqObj->EquipManager.Traverse(EquipWalk);
    }

    // Collision groups: root != -1 (GetRootIndex asked twice), then the group's type name.
    CArchGrpTraverser GroupWalk;
    CArchGroup* Group = EqObj->ArchGroups.Traverse(GroupWalk);
    while (Group != nullptr)
    {
        if (Group->GetRootIndex() != -1 &&
            Pick.Test(Group->GetRootIndex(), kSubTargetPickSlack))
        {
            *OutSubId = Group->GetID();
            Result = HudCollisionGroupTypeIds(Group->GetCollisionGroupType());
            *OutCount = 1;
        }
        Group = EqObj->ArchGroups.Traverse(GroupWalk);
    }
    return Result;
}

bool HudTargetBase::HitTest(float const* Ray)
{
    // LAB_004e7d20.
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        return false;
    }
    if (GetPlayerObjRW() == nullptr)
    {
        return false;
    }
    if (IsLetterboxVisible())
    {
        return false;
    }

    if (bExpanded != 0)
    {
        // The ray scaled to the hit plane's depth, tested against the box around HitCenter.
        float const Scale = HitCenter.z / Ray[2];
        float const PlaneX = Scale * Ray[0];
        float const PlaneY = Scale * Ray[1];
        float const Dx = PlaneX - (HitCenter.x - HitHalfWidth);
        float const Dy = (HitCenter.y + HitHalfHeight) - PlaneY;
        // DAT_005c7474 = 0.0f. Each test falls through to the base on false or unordered.
        if (Dx > 0.0f && Dx < HitHalfWidth + HitHalfWidth && Dy > 0.0f &&
            HitHalfHeight + HitHalfHeight > Dy)
        {
            return true;
        }
    }
    // FUN_005a0340
    return WinControl::HitTest(Ray);
}

void HudTargetBase::Update()
{
    // FUN_004e7980.
    Win3D::Update();
    ButtonMover::Update();
    if (bExpanded != 0)
    {
        UpdateSubTargetToolTip();
    }
}

void HudTargetBase::Draw()
{
    // FUN_004e7890.
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        return;
    }
    if (GetPlayerObjRW() == nullptr)
    {
        return;
    }
    if (IsLetterboxVisible())
    {
        return;
    }

    if (bExpanded != 0 && !ButtonMover::IsActive())
    {
        // Visible children (flag bit 1); DrawViewObject right after the view panel.
        for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        {
            if ((Child->Flags & 2) == 0)
            {
                continue;
            }
            WinControl* const AsControl = Child->AsWinControl();
            Child->Draw();
            if (AsControl == ViewPanel)
            {
                DrawViewObject();
            }
        }
    }
    else
    {
        for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        {
            if ((Child->Flags & 2) == 0)
            {
                continue;
            }
            Child->AsWinControl();
            Child->Draw();
        }
    }
    // Tail jump through the WinControl face's slot 39 (+0x9c).
    PostDraw();
}

intptr_t HudTargetBase::SetProperty(int Msg, uintptr_t A, uintptr_t /*B*/)
{
    // FUN_004e7950: always 0; the base is not called.
    if (Msg == kWinNotifyClick && A == reinterpret_cast<uintptr_t>(ToggleButton))
    {
        ToggleExpanded();
    }
    return 0;
}

void HudTargetBase::OnMoveFinished()
{
    // FUN_004e79b0: still collapsed means the opening slide just ended, so un-hide the panel
    // controls.
    if (bExpanded == 0)
    {
        HideExpandedControls(false);
    }
    bExpanded = (bExpanded == 0) ? 1 : 0;
}

CEquip* HudTargetBase::FindTargetEquip(unsigned short SubId)
{
    // FUN_004e7e20 -- on the HUDItem target (+0x28), not the view object.
    if (SubId == SUBOBJ_ID_NONE)
    {
        return nullptr;
    }
    CObject* const Object = CObjectOfWatcher(HUDItem::Target);
    if (Object == nullptr)
    {
        return nullptr;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
    {
        return nullptr;
    }
    return static_cast<CEqObj*>(Object)->EquipManager.FindByID(SubId);
}
