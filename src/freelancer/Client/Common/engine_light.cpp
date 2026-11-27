// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/engine_light.h"

#include "common_csys.h"
#include "common_ini.h"
#include "common_misc.h"
#include "Core/game_options.h"
#include "render/slots.h"          // Render::EPipelineState
#include "DALib.h"
#include "dacom.h"
#include "Core/globals.h"
#include "GameFlow/room_camera.h" // LightsCameraMath (DAT_006799bc)
#include "thorn_iface.h"          // FThornLightProps
#include "x86math.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <map>
#include <new>
#include <windows.h>

namespace
{
    char const* const kEngineLightCpp = "E:\\FL\\Scratch\\Source\\Client\\cmn\\EngineLight.cpp";

    float const kPi = 3.1415927f;

    // DAT_00610588 — pipeline state 7, latched once on the first light. -1 means "not asked
    // yet"; retail pre-sets 1 so a failed query leaves the "no hardware lights" answer.
    int G_PipelineLightState = -1;

    void QueryPipelineLightState()
    {
        // No test of the pipeline.
        if (G_PipelineLightState != -1)
            return;
        G_PipelineLightState = 1;
        DALib::Pipeline->GetPipelineState(Render::PipelineStateViewspaceLights, &G_PipelineLightState);
    }

    // The tail every setter shares: when the pipeline owns hardware lights and this one is
    // already registered (Flags bit 2), push the D3DLIGHT8 through slot 75. Retail tests
    // nothing else.
    void PushLightUpdate(FEngineLight* Light)
    {
        if (G_PipelineLightState != 0 || (Light->Flags & kLightFlagRegistered) == 0)
            return;
        DALib::Pipeline->UpdateLight(&Light->D3d);
    }

    void StoreType(FEngineLight* Light, int LightType, bool bAlsoD3dType)
    {
        // switchD_00410d14: 1/2/3 also write the D3DLIGHT8 type; -2 keeps itself; the rest
        // collapse to -1.
        switch (LightType)
        {
        case kLightPoint:
        case kLightSpot:
        case kLightDirectional:
            if (bAlsoD3dType)
                Light->D3d.Type = static_cast<unsigned long>(LightType);
            Light->Type = LightType;
            break;
        case kLightAmbient:
            Light->Type = LightType;
            break;
        default:
            Light->Type = kLightNone;
            break;
        }
    }

    // DAT_006164e0/e4/e8 — global intrusive list of registered lights (head/tail/count),
    // linked through FEngineLight::RegListNext (next) / RegListPrev (prev). Insert via
    // LinkIntoRegisteredList (FUN_004104e0 bit4 arm).
    FEngineLight* G_LightListHead = nullptr;
    FEngineLight* G_LightListTail = nullptr;
    int G_LightListCount = 0;

    // DAT_006164ec — soft-light 3DMathEngine cache (FUN_005b73e0).
    I3DMathEngine* G_SoftLightMath = nullptr;

    FEngineLight* ListNext(FEngineLight const* L) { return reinterpret_cast<FEngineLight*>(static_cast<uintptr_t>(L->RegListNext)); }
    FEngineLight* ListPrev(FEngineLight const* L) { return reinterpret_cast<FEngineLight*>(static_cast<uintptr_t>(L->RegListPrev)); }
    void SetListNext(FEngineLight* L, FEngineLight* V) { L->RegListNext = static_cast<unsigned>(reinterpret_cast<uintptr_t>(V)); }
    void SetListPrev(FEngineLight* L, FEngineLight* V) { L->RegListPrev = static_cast<unsigned>(reinterpret_cast<uintptr_t>(V)); }

    I3DMathEngine* AcquireSoftLightMath()
    {
        if (G_SoftLightMath == nullptr)
            G_SoftLightMath = Acquire3DMathEngine();
        return G_SoftLightMath;
    }

    void SoftCameraRewriteLight(FEngineLight* Light, ICamera* Camera, Vector const* DirWorld)
    {
        // FUN_004104e0's soft arm (DAT_00610588 != 0). Nothing is tested but the type; each arm
        // fetches the view transform and then DAT_006164ec.
        // Not directional -> the D3D position is the light position in camera space, z negated.
        if (Light->Type != kLightDirectional)
        {
            Transform const* const View =
                Camera->GetViewTransform();
            I3DMathEngine* const Math = AcquireSoftLightMath();
            Vector CamSpace;
            Math->TransformXform(CamSpace, *View,
                                 Light->Position);
            Light->D3d.Position.x = CamSpace.x;
            Light->D3d.Position.y = CamSpace.y;
            Light->D3d.Position.z = -CamSpace.z;
        }

        // Not a point light -> the D3D direction is the world direction rotated into camera
        // space, z negated.
        if (Light->Type != kLightPoint)
        {
            Transform const* const View =
                Camera->GetViewTransform();
            I3DMathEngine* const Math = AcquireSoftLightMath();
            Vector CamDir;
            Math->Rotate(CamDir, *View, *reinterpret_cast<Vector const*>(DirWorld));
            Light->D3d.Direction[0] = CamDir.x;
            Light->D3d.Direction[1] = CamDir.y;
            Light->D3d.Direction[2] = -CamDir.z;
        }

        DALib::Pipeline->SetLight(&Light->D3d, (const D3DLIGHT8*)&Light->D3d);
    }

    // Shared by the destructor (FUN_00410ab0/ba0) and FEngineLight::Disable (FUN_00410e20):
    // unlink from the global light list and notify the pipeline (vtable slot 41 — a
    // RemoveLight-shaped call taking the D3DLIGHT8 pointer plus a trailing 0).
    void UnlinkFromList(FEngineLight* Light)
    {
        FEngineLight* const Next = ListNext(Light);
        FEngineLight* const Prev = ListPrev(Light);
        if (Prev == nullptr) G_LightListHead = Next; else SetListNext(Prev, Next);
        if (Next == nullptr) G_LightListTail = Prev; else SetListPrev(Next, Prev);
        --G_LightListCount;

        // Unconditional in retail: no pipeline null test.
        DALib::Pipeline->SetLightEnable(&Light->D3d, 0);
    }

    // Shared body of FUN_00410ab0 (base dtor) and FUN_00410ba0 (scalar deleting dtor, which is
    // this plus an optional operator-delete on a flag bit the port doesn't need since callers
    // always go through EngineLightDestroy).
    void DestructLightBody(FEngineLight* Light)
    {
        unsigned char const Was = Light->Flags;
        if ((Was & kLightFlagRegistered) != 0) // registered
        {
            if ((Was & kLightFlagLinked) != 0) // linked into the global list
            {
                Light->Flags = static_cast<unsigned char>(Light->Flags & ~kLightFlagLinked);
                UnlinkFromList(Light);
            }

            // Slot 74 or slot 39, both with the D3DLIGHT8. No test of the pipeline.
            if (G_PipelineLightState == 0)
                DALib::Pipeline->RemoveLight(&Light->D3d);
            else
                DALib::Pipeline->DestroyLight(&Light->D3d);
            // Clear "registered".
            Light->Flags = static_cast<unsigned char>(Light->Flags & ~kLightFlagRegistered);
        }
    }

    void SetColorFromBytes(FEngineLight* Light, unsigned char const RGB[3])
    {
        // FUN_004114f0 / FUN_004115b0 — byte-identical bodies in retail.
        Light->D3d.Diffuse[3] = 1.f;
        Light->Color[0] = static_cast<float>(RGB[0]) * (1.f / 255.f);
        Light->Color[1] = static_cast<float>(RGB[1]) * (1.f / 255.f);
        Light->Color[2] = static_cast<float>(RGB[2]) * (1.f / 255.f);
        Light->D3d.Diffuse[0] = Light->Color[0];
        Light->D3d.Diffuse[1] = Light->Color[1];
        Light->D3d.Diffuse[2] = Light->Color[2];
        PushLightUpdate(Light);
    }
}

void EngineLightInit(FEngineLight* Light, int LightType)
{
    ::new (Light) FEngineLight(LightType);
}

FEngineLight::FEngineLight(int LightType)
{
    // FUN_00410ca0 (thiscall, RET 4). No null test, and only the fields below are written:
    // +0x04 / +0x08 (the list links) are left as they are. Stores in retail order.
    this->Orient.E[2][1] = 0.f;
    this->Orient.E[2][0] = 0.f;
    this->Orient.E[1][2] = 0.f;
    this->Orient.E[1][0] = 0.f;
    this->Orient.E[0][2] = 0.f;
    this->Orient.E[0][1] = 0.f;
    this->Orient.E[2][2] = 1.f;
    this->Orient.E[1][1] = 1.f;
    this->Orient.E[0][0] = 1.f;
    this->Position.z = 0.f;
    this->Position.y = 0.f;
    this->Position.x = 0.f;
    this->Flags = 0;
    this->PadB5[0] = 0;
    this->PadB5[1] = 0;
    this->PadB5[2] = 0;
    // Retail quirk: the type switch (types 1..3 also write D3d.Type) runs before the D3DLIGHT8
    // is zeroed, so only +0xB0 survives.
    StoreType(this, LightType, true);

    QueryPipelineLightState();

    memset(&this->D3d, 0, sizeof(this->D3d));
    this->Color[0] = 1.f;
    this->Color[1] = 1.f;
    this->Color[2] = 1.f;
    this->D3d.Attenuation1 = 1.f;
    this->AmbientCurve = nullptr;
    this->ZoneCurve = nullptr;
    this->ColorCurve = nullptr;
    this->ColorCurvePeriod = 0.f;
}

void FEngineLight::SetLight(FD3dLight8 const* Source)
{
    // FUN_00411120 (thiscall, RET 4). No null tests.
    unsigned char const Was = Flags;
    Flags = static_cast<unsigned char>(Was | kLightFlagHasD3d);
    if ((Was & kLightFlagRegistered) == 0)
    {
        // Not registered yet: the incoming type still gets to set both fields.
        Flags = static_cast<unsigned char>((Was & ~kLightFlagDynamic) | kLightFlagHasD3d);
        StoreType(this, static_cast<int>(Source->Type), true);
    }

    memcpy(D3d.Diffuse, Source->Diffuse, sizeof(float) * 4);
    memcpy(D3d.Specular, Source->Specular, sizeof(float) * 4);
    memcpy(D3d.Ambient, Source->Ambient, sizeof(float) * 4);
    // Position / Direction are skipped — FUN_00410ef0 and FUN_00410f60 own them.
    D3d.Range = Source->Range;
    D3d.Falloff = Source->Falloff;
    D3d.Attenuation0 = Source->Attenuation0;
    D3d.Attenuation1 = Source->Attenuation1;
    D3d.Attenuation2 = Source->Attenuation2;
    D3d.Theta = Source->Theta;
    D3d.Phi = Source->Phi;

    Color[2] = D3d.Diffuse[2];
    Color[0] = D3d.Diffuse[0];
    Color[1] = D3d.Diffuse[1];

    if (D3d.Type != kLightDirectional)
    {
        // Unordered does not warn.
        if (D3d.Phi > kPi)
        {
            FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", kEngineLightCpp, 0x23b,
                  "invalid light detected: phi > pi");
            D3d.Phi = kPi;
        }
        // Retail quirk: the warning fires when all three are <= 0 (ordered), not only when all
        // three are exactly zero.
        if (D3d.Attenuation0 <= 0.f && D3d.Attenuation1 <= 0.f &&
            D3d.Attenuation2 <= 0.f)
        {
            FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", kEngineLightCpp, 0x241,
                  "invalid light detected: atten all zero");
            D3d.Attenuation0 = 0.1f;
        }
    }

    PushLightUpdate(this);
}

void FEngineLight::SetPosition(Vector const* Pos3)
{
    // FUN_00410ef0 (thiscall, RET 4). No null tests.
    Position.x = Pos3->x;
    Position.y = Pos3->y;
    Position.z = Pos3->z;

    // A directional light has no position; everything else gets it with Z negated.
    if (Type != kLightDirectional)
    {
        D3d.Position.x = Position.x;
        D3d.Position.y = Position.y;
        D3d.Position.z = -Position.z;
    }

    PushLightUpdate(this);
}

void FEngineLight::SetOrientation(float const* Orient9)
{
    // FUN_00410f60 (thiscall, RET 4): anything but a point light also gets the D3D direction
    // from FUN_00411370, z negated. No null tests.
    memcpy(&Orient, Orient9, sizeof(float) * 9);

    if (Type != kLightPoint)
    {
        Vector Dir;
        ComputeDirection(&Dir, nullptr, nullptr);
        D3d.Direction[0] = Dir.x;
        D3d.Direction[1] = Dir.y;
        D3d.Direction[2] = -Dir.z;
    }

    PushLightUpdate(this);
}

bool FEngineLight::Enable()
{
    // FUN_00410dd0 (thiscall): bit 3 is set and returned (always 1). No test of the light or
    // the pipeline.
    unsigned char const Was = Flags;
    if ((Was & kLightFlagRegistered) == 0)
    {
        Flags = static_cast<unsigned char>(Was | kLightFlagRegistered);
        if (G_PipelineLightState == 0)
            DALib::Pipeline->AddLight(&D3d);
    }
    Flags = static_cast<unsigned char>(Flags | kLightFlagEnabled);
    return ((Flags >> 3) & 1u) != 0;
}

FEngineLight* EngineLightCreate(float const* Frame12, FD3dLight8 const* Source,
                                Vector const* Direction)
{
    // The light part of FUN_00556fd0. The ctor always gets -1, never the source type. No test
    // of any argument anywhere below.
    FEngineLight* Light = static_cast<FEngineLight*>(::operator new(sizeof(FEngineLight)));
    EngineLightInit(Light, kLightNone);

    // A local copy; a point light has theta and phi forced to pi before FUN_00411120 sees them.
    FD3dLight8 Local = *Source;
    if (Local.Type == kLightPoint)
    {
        Local.Theta = kPi;
        Local.Phi = kPi;
    }

    Light->SetLight(&Local);
    Light->SetPosition(reinterpret_cast<Vector const*>(Frame12));

    // Direction turned by the frame's orientation (Frame12 + 3, the 3x3 after the position),
    // then LookMatrixYup on that vector.
    I3DMathEngine* const Math = LightsCameraMath();
    Vector Rotated;
    Math->TransformMatrix(Rotated, *reinterpret_cast<Matrix const*>(Frame12 + 3), *Direction);
    Vector Dir;
    Dir.x = Rotated.x;
    Dir.y = Rotated.y;
    Dir.z = Rotated.z;
    const Matrix Look = LookMatrixYup(Dir);
    Light->SetOrientation(&Look.E[0][0]);

    // Vtable slot 1 of the plain EngineLight table, i.e. FUN_00410dd0. This is not a
    // GameLight: it is never on the DAT_00675570 list and has no +0xc8..+0xd0 fields.
    Light->Enable();
    return Light;
}

// The two address computations FUN_00452fa0 inlines; no test.
FD3dLight8 const* ThornLightPropsD3d(FThornLightProps const* Props)
{
    return reinterpret_cast<FD3dLight8 const*>(&Props->Type); // +0x28
}

Vector const* ThornLightPropsTarget(FThornLightProps const* Props)
{
    return &Props->Direction; // +0x0C
}

void EngineLightDestroy(FEngineLight* Light)
{
    // FUN_00557130 calls vtable slot 0 with 1 (the deleting destructor, FUN_00410ba0); its
    // null test lives in the caller GfLightWrapperDestroy.
    EngineLightDelete(Light);
}

void EngineLightDelete(FEngineLight* Light)
{
    // FUN_00410ba0 via vtable slot 0, with the scalar-delete flag.
    delete Light;
}

void FEngineLight::Disable()
{
    // FUN_00410e20 (thiscall). Retail's relink arm after the unlink is dead (the bit was just
    // cleared). No null test.
    unsigned char const Was = Flags;
    Flags = static_cast<unsigned char>(Was & ~kLightFlagEnabled); // clear "enabled"
    if ((Was & kLightFlagLinked) != 0)
    {
        Flags = static_cast<unsigned char>(Flags & ~kLightFlagLinked);
        UnlinkFromList(this);
    }
}

void FEngineLight::PushUpdate()
{
    // FUN_00410ec0 (thiscall). No null test.
    PushLightUpdate(this);
}

void EngineLightSetTransform(FEngineLight* Light, float const* Orient9, Vector const* Pos3)
{
    // FUN_00410fe0 (thiscall, RET 4) on one {Orient[9], Pos[3]} block: FUN_00410ef0's body,
    // then FUN_00410f60's, inlined. Same order, same two pushes. No null tests.
    Light->SetPosition(Pos3);
    Light->SetOrientation(Orient9);
}

void FEngineLight::SetType(int LightType, bool bDynamic)
{
    // FUN_004110c0 (thiscall, RET 8): the argument's bit 0 into Flags bit 0, then the type
    // switch. No null test.
    Flags = static_cast<unsigned char>((Flags & ~kLightFlagDynamic) |
                                              (bDynamic ? kLightFlagDynamic : 0u));
    StoreType(this, LightType, true);
}

void FEngineLight::SetRangeRaw(float Range)
{
    D3d.Range = Range; // FUN_004112f0. No push, no null test.
}

void FEngineLight::SetRadius(float Radius)
{
    // FUN_00411300 (thiscall, RET 4). r*r stays on the x87 stack (double here), one float
    // store at the end. No null test.
    D3d.Range = Radius;
    D3d.Attenuation0 = 1.f;
    D3d.Attenuation1 = 0.f;
    double const RadiusSq = static_cast<double>(Radius) * static_cast<double>(Radius);
    D3d.Attenuation2 = static_cast<float>(16.0 / RadiusSq);
    PushLightUpdate(this);
}

void FEngineLight::SetDirection(Vector const* Dir3)
{
    // FUN_00411470 (thiscall, RET 4). No null test.
    if (Type != kLightPoint)
    {
        D3d.Direction[0] = Dir3->x;
        D3d.Direction[1] = Dir3->y;
        D3d.Direction[2] = -Dir3->z;
    }
    Orient.E[0][2] = -Dir3->x;
    Orient.E[1][2] = -Dir3->y;
    Orient.E[2][2] = -Dir3->z;

    PushLightUpdate(this);
}

void FEngineLight::SetColorBytes(unsigned char const RGB[3])
{
    // FUN_004114f0 (thiscall, RET 4): no test of the light or the byte pointer.
    SetColorFromBytes(this, RGB);
}

void FEngineLight::SetColorBytesAlt(unsigned char const RGB[3])
{
    // FUN_004115b0 (thiscall, RET 4): the same body; no tests.
    SetColorFromBytes(this, RGB);
}

void FEngineLight::SetConeAngle(float DegreesFullAngle)
{
    // FUN_00411670 (thiscall, RET 4) — no test of the light, and deliberately no
    // PushLightUpdate call, matching retail. Theta = Phi * 0.5 is exact either way.
    D3d.Falloff = 1.f;
    D3d.Phi = DegreesFullAngle * (kPi / 180.f);
    D3d.Theta = D3d.Phi * 0.5f;
}

double Vec3Length(Vector const* V)
{
    // FUN_00410190: the result stays on the x87 stack (returned as double).
    return sqrt((static_cast<double>(V->x) * V->x + static_cast<double>(V->y) * V->y) +
                static_cast<double>(V->z) * V->z);
}

void Vec3Negate(Vector* Out, Vector const* In)
{
    // FUN_00411350 (thiscall on In): all loads happen before the stores; no null tests.
    Out->x = -In->x;
    Out->y = -In->y;
    Out->z = -In->z;
}

void ScaleVec3(float* Out, float const* V, float Scalar)
{
    // FUN_00411d60, no null test. Out may alias V, as texture_panels.cpp's second call does.
    Out[0] = Scalar * V[0];
    Out[1] = Scalar * V[1];
    Out[2] = Scalar * V[2];
}

void PackNibbleByte(unsigned int* Out, int Hi, unsigned int Lo)
{
    // FUN_00411c50 (thiscall on Out, RET 8): (Lo & 0xf) | (Hi << 4). No null test.
    unsigned int const Shifted = static_cast<unsigned int>(Hi) << 4;
    *Out = ((Shifted ^ Lo) & 0xfu) ^ Shifted;
}

unsigned char* EngineLightPackColorBytes(unsigned char* Out4, float const* Rgb3)
{
    // FUN_00411bc0 — __thiscall on the 4-byte output, RET 4. Each channel is _ftol'd and clamped
    // to [0, 0xFF]; retail converts index 2, then 1, then 0, and stores them back in place, so
    // Out4[i] comes from Rgb3[i]. Alpha is 0xFF.
    int Blue = static_cast<int>(Rgb3[2]);
    if (Blue < 0)
        Blue = 0;
    else if (Blue > 0xff)
        Blue = 0xff;
    int Green = static_cast<int>(Rgb3[1]);
    if (Green < 0)
        Green = 0;
    else if (Green > 0xff)
        Green = 0xff;
    int Red = static_cast<int>(Rgb3[0]);
    if (Red < 0)
        Red = 0;
    else if (Red > 0xff)
        Red = 0xff;
    Out4[0] = static_cast<unsigned char>(Red);
    Out4[2] = static_cast<unsigned char>(Blue);
    Out4[1] = static_cast<unsigned char>(Green);
    Out4[3] = 0xff;
    return Out4;
}

float* EngineLightUnpackColorBytes(unsigned char const* Bytes3, float* OutRgb3)
{
    // FUN_00411c80 — __thiscall on the bytes, RET 4.
    OutRgb3[0] = static_cast<float>(Bytes3[0]) * (1.f / 255.f);
    OutRgb3[1] = static_cast<float>(Bytes3[1]) * (1.f / 255.f);
    OutRgb3[2] = static_cast<float>(Bytes3[2]) * (1.f / 255.f);
    return OutRgb3;
}

int EngineLightParseType(char const* Text)
{
    // FUN_00411cd0.
    if (_stricmp(Text, "AMBIENT") == 0)
        return kLightAmbient;
    if (_stricmp(Text, "POINT") == 0)
        return kLightPoint;
    if (_stricmp(Text, "CONE") == 0)
        return kLightSpot;
    if (_stricmp(Text, "DIRECTIONAL") == 0)
        return kLightDirectional;

    FDUMP(kFdumpError, "%s(%d) : *** ERROR: invalid [LightSource] type = '%s'", kEngineLightCpp, 0x2ec,
          Text);
    return kLightNone;
}

void MatrixSetColumn2(Matrix* Matrix9, Vector const* Vec3)
{
    // FUN_00411f70 — __thiscall on the 3x3, RET 4.
    Matrix9->E[0][2] = Vec3->x;
    Matrix9->E[1][2] = Vec3->y;
    Matrix9->E[2][2] = Vec3->z;
}

FLightCurveName* FLightCurveName::Init(char const* Source)
{
    // FUN_00411f00 — __thiscall, RET 4.
    this->Len = 0;
    Text[0] = '\0';
    if (Source == nullptr)
    {
        this->Len = 0;
        Text[0] = '\0';
        return this;
    }
    unsigned Len = static_cast<unsigned>(strlen(Source));
    if (Len >= 0x80)
        Len = 0x7f;
    memcpy(Text, Source, Len);
    Text[Len] = '\0';
    this->Len = Len;
    return this;
}

void FEngineLight::SetAttenCurve(char const* GraphName)
{
    // FUN_00411d90 — __thiscall, RET 4.
    if (Type == kLightAmbient)
    {
        // An ambient light takes the graph as its ambient curve instead.
        if (AmbientCurve != nullptr)
        {
            AmbientCurve->Release();
            AmbientCurve = nullptr;
        }
        IGraph* const Graph = GraphLibraryFind(GraphName);
        if (Graph != nullptr)
        {
            AmbientCurve = Graph;
            Flags = static_cast<unsigned char>(Flags | kLightFlagHasCurve);
            return;
        }
        MessageBoxA(nullptr, GraphName, "Error: unknown graph name", 0);
        return;
    }

    if (ZoneCurve != nullptr)
    {
        ZoneCurve->Release();
        ZoneCurve = nullptr;
    }
    IGraph* const Graph = GraphLibraryFind(GraphName);
    if (Graph == nullptr)
    {
        MessageBoxA(nullptr, GraphName, "Error: unknown graph name", 0);
        return;
    }
    if (Type != kLightPoint && Type != kLightSpot && Type != kLightDirectional)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Invalid dynamic light type (%s)", kEngineLightCpp,
              0x360, GraphName);
    }
    ZoneCurve = Graph;
    Flags = static_cast<unsigned char>(Flags | kLightFlagHasCurve);
}

void FEngineLight::SetColorCurve(char const* GraphName, float Period)
{
    // FUN_00411e90 — __thiscall, RET 8.
    if (ColorCurve != nullptr)
    {
        ColorCurve->Release();
        ColorCurve = nullptr;
    }
    IGraph* const Graph = GraphLibraryFind(GraphName);
    if (Graph == nullptr)
    {
        MessageBoxA(nullptr, GraphName, "Error: unknown graph name", 0);
        return;
    }
    ColorCurve = Graph;
    ColorCurvePeriod = Period;
    Flags = static_cast<unsigned char>(Flags | kLightFlagHasCurve);
}

bool FEngineLight::ReadValue(char const* Name, char const* Value)
{
    // FUN_00411690 — __thiscall, RET 8. Every recognised key returns true; ValueNum fields are
    // read highest index first, as retail does.
    if (_stricmp(Name, "pos") == 0)
    {
        float Pos[3];
        Pos[2] = static_cast<float>(ValueNum(Value, 2));
        Pos[1] = static_cast<float>(ValueNum(Value, 1));
        Pos[0] = static_cast<float>(ValueNum(Value, 0));
        Position.x = Pos[0];
        Position.y = Pos[1];
        Position.z = Pos[2];
        if (Type != kLightDirectional)
        {
            D3d.Position.x = Position.x;
            D3d.Position.y = Position.y;
            D3d.Position.z = -Position.z;
        }
        PushLightUpdate(this); // FUN_00410ec0
        return true;
    }

    if (_stricmp(Name, "range") == 0)
    {
        D3d.Range = static_cast<float>(ValueNum(Value, 0));
        return true;
    }

    if (_stricmp(Name, "color") == 0)
    {
        float Rgb[3];
        Rgb[2] = static_cast<float>(ValueNum(Value, 2));
        Rgb[1] = static_cast<float>(ValueNum(Value, 1));
        Rgb[0] = static_cast<float>(ValueNum(Value, 0));
        // Through a clamped byte quad and back, so the colour is quantised to 1/255 steps.
        unsigned char Packed[4];
        EngineLightPackColorBytes(Packed, Rgb);
        float Color[3];
        float const* const Unpacked = EngineLightUnpackColorBytes(Packed, Color);
        this->Color[0] = Unpacked[0];
        this->Color[1] = Unpacked[1];
        this->Color[2] = Unpacked[2];
        D3d.Diffuse[0] = this->Color[0];
        D3d.Diffuse[1] = this->Color[1];
        D3d.Diffuse[2] = this->Color[2];
        D3d.Diffuse[3] = 1.0f;
        PushLightUpdate(this);
        return true;
    }

    if (_stricmp(Name, "type") == 0)
    {
        // FUN_00411cd0 and FUN_004110c0(type, false) inlined; the FDUMP flag word there comes
        // out of FUN_00411c50(0x10000, 1) = 0x100001, the same constant.
        SetType(EngineLightParseType(Value), false);
        return true;
    }

    if (_stricmp(Name, "attenuation") == 0)
    {
        float const Atten2 = static_cast<float>(ValueNum(Value, 2));
        float const Atten1 = static_cast<float>(ValueNum(Value, 1));
        D3d.Attenuation0 = static_cast<float>(ValueNum(Value, 0));
        D3d.Attenuation1 = Atten1;
        D3d.Attenuation2 = Atten2;
        return true;
    }

    if (_stricmp(Name, "direction") == 0)
    {
        Vector Dir;
        Dir.z = static_cast<float>(ValueNum(Value, 2));
        Dir.y = static_cast<float>(ValueNum(Value, 1));
        Dir.x = static_cast<float>(ValueNum(Value, 0));
        // FUN_00410190's length, unrounded; no zero test.
        double const Length = sqrt(static_cast<double>(Dir.x) * Dir.x +
                                   static_cast<double>(Dir.y) * Dir.y +
                                   static_cast<double>(Dir.z) * Dir.z);
        double const InvLength = 1.0 / Length;
        Dir.x = static_cast<float>(Dir.x * InvLength);
        Dir.y = static_cast<float>(Dir.y * InvLength);
        Dir.z = static_cast<float>(Dir.z * InvLength);
        if (Type != kLightPoint)
        {
            D3d.Direction[0] = Dir.x;
            D3d.Direction[1] = Dir.y;
            D3d.Direction[2] = -Dir.z;
        }
        Vector Negated;
        Vec3Negate(&Negated, &Dir);                  // FUN_00411350
        MatrixSetColumn2(&Orient, &Negated);  // FUN_00411f70 on light +0x74
        PushLightUpdate(this);
        return true;
    }

    if (_stricmp(Name, "ambient_curve") == 0)
    {
        if (AmbientCurve != nullptr)
        {
            AmbientCurve->Release();
            AmbientCurve = nullptr;
        }
        IGraph* const Graph = GraphLibraryFind(Value);
        if (Graph != nullptr)
        {
            AmbientCurve = Graph;
            Flags = static_cast<unsigned char>(Flags | kLightFlagHasCurve);
            return true;
        }
        MessageBoxA(nullptr, Value, "Error: unknown graph name", 0);
        return true;
    }

    if (_stricmp(Name, "atten_curve") == 0)
    {
        // "atten_curve = graph[, type]": an optional light type first, then the graph.
        char const* const TypeName = StringIndex(Value, 1);
        if (TypeName != nullptr && TypeName[0] != '\0')
            SetType(EngineLightParseType(TypeName), false);
        SetAttenCurve(StringIndex(Value, 0));
        return true;
    }

    if (_stricmp(Name, "color_curve") == 0)
    {
        FLightCurveName CurveName;
        CurveName.Init(StringIndex(Value, 0));
        float const Period = static_cast<float>(ValueNum(Value, 1));
        SetColorCurve(CurveName.Text, Period);
        return true;
    }

    return false;
}

bool FEngineLight::AffectsPoint(float const* Point3, float ExtraRadius) const
{
    // FUN_00410a40 (thiscall, RET 8). No null test.
    if ((Flags & kLightFlagEnabled) == 0)
        return false;

    if (Type == kLightDirectional && (Flags & kLightFlagHasCurve) == 0)
        return true;

    // Kept on the x87 stack (double here); true only when strictly greater (ordered).
    double const Dx = static_cast<double>(Point3[0]) - Position.x;
    double const Dy = static_cast<double>(Point3[1]) - Position.y;
    double const Dz = static_cast<double>(Point3[2]) - Position.z;
    double const Reach = static_cast<double>(D3d.Range) + ExtraRadius;
    double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
    return Reach * Reach > DistSq;
}

bool FEngineLight::GetEffectiveColor(float Dist, float* OutColor3)
{
    // DAT_005c89c8, a double.
    double const kInv255 = 1.0 / 255.0;

    OutColor3[0] = Color[0];
    OutColor3[1] = Color[1];
    OutColor3[2] = Color[2];

    bool bModified = false;
    if (ColorCurve != nullptr)
    {
        // Range {wrap, 0, ColorCurvePeriod}; retail writes only the flag byte of bClamp's dword.
        FGraphRange Period;
        Period.bClamp = 0;
        Period.Min = 0.0f;
        Period.Max = ColorCurvePeriod;
        float const Now = static_cast<float>(GetClientElapsedTime()); // FUN_0042d660
        unsigned const Packed = ColorCurve->EvaluateColor(Now, &Period);
        // Double-precision products, one float store each.
        OutColor3[0] = static_cast<float>(((Packed >> 0x10) & 0xffu) * kInv255 * OutColor3[0]);
        OutColor3[1] = static_cast<float>(((Packed >> 8) & 0xffu) * kInv255 * OutColor3[1]);
        OutColor3[2] = static_cast<float>((Packed & 0xffu) * kInv255 * OutColor3[2]);
        bModified = true;
    }

    if (ZoneCurve != nullptr)
    {
        FGraphRange Reach;
        Reach.bClamp = 1;
        Reach.Min = 0.0f;
        Reach.Max = D3d.Range;
        // The unrounded result multiplies each channel.
        double const Scale = ZoneCurve->Evaluate(Dist, &Reach);
        OutColor3[0] = static_cast<float>(Scale * OutColor3[0]);
        OutColor3[1] = static_cast<float>(Scale * OutColor3[1]);
        OutColor3[2] = static_cast<float>(Scale * OutColor3[2]);
        bModified = true;
    }

    return bModified;
}

Vector* FEngineLight::ComputeDirection(Vector* OutDir3, float const* Point, float* OutDist)
{
    // FUN_00411370 (thiscall, RET 0xc). No null tests.
    int const Type = this->Type;
    bool const bTowardPoint =
        (Type == kLightPoint) ||
        (((Flags & kLightFlagHasCurve) != 0) && (Type == kLightDirectional || Type == kLightSpot));

    if (Point != nullptr && (bTowardPoint || OutDist != nullptr))
    {
        // x87 precision: dx and dz stay on the stack, dy is rounded to a float.
        double const Dx = static_cast<double>(Point[0]) - Position.x;
        float const Dy = Point[1] - Position.y;
        double const Dz = static_cast<double>(Point[2]) - Position.z;
        double const Len = sqrt((Dz * Dz + Dx * Dx) + static_cast<double>(Dy) * Dy);
        if (OutDist != nullptr)
            *OutDist = static_cast<float>(Len);
        if (bTowardPoint)
        {
            // No zero test.
            double const Inv = 1.0 / Len;
            OutDir3->x = static_cast<float>(Dx * Inv);
            OutDir3->y = static_cast<float>(static_cast<double>(Dy) * Inv);
            OutDir3->z = static_cast<float>(Dz * Inv);
            return OutDir3;
        }
    }

    // Default: negate Orient column 2.
    OutDir3->x = -Orient.E[0][2];
    OutDir3->y = -Orient.E[1][2];
    OutDir3->z = -Orient.E[2][2];
    return OutDir3;
}

bool FEngineLight::Sample(float const* Point, float* OutDir3, float* InOutAmbient3,
                          float* OutColor3)
{
    // FUN_00410960.
    if ((Flags & kLightFlagEnabled) == 0)
        return false;

    // Retail reuses the Point argument slot for the distance.
    Vector Dir;
    float Dist;
    Vector const* const Result = ComputeDirection(&Dir, Point, &Dist);
    OutDir3[0] = Result->x;
    OutDir3[1] = Result->y;
    OutDir3[2] = Result->z;

    // The return value is not read.
    GetEffectiveColor(Dist, OutColor3);

    if (Type == kLightAmbient)
    {
        // The unrounded curve result is kept on the x87 stack.
        double Scale = 1.0;
        if (AmbientCurve != nullptr)
        {
            FGraphRange Reach;
            Reach.bClamp = 1;
            Reach.Min = 0.0f;
            Reach.Max = D3d.Range;
            Scale = AmbientCurve->Evaluate(Dist, &Reach);
        }
        // Each product rounded to a float before the add.
        float const AddR = static_cast<float>(Scale * OutColor3[0]);
        float const AddG = static_cast<float>(Scale * OutColor3[1]);
        float const AddB = static_cast<float>(Scale * OutColor3[2]);
        InOutAmbient3[0] = AddR + InOutAmbient3[0];
        InOutAmbient3[1] = AddG + InOutAmbient3[1];
        InOutAmbient3[2] = AddB + InOutAmbient3[2];
    }
    return true;
}

void EngineLightGetAmbientRgbBytes(unsigned char* OutRgb)
{
    // FUN_00410280.
    if (OutRgb == nullptr)
        return;
    int Packed;
    DALib::Pipeline->GetRenderState(D3DRS_AMBIENT, &Packed);
    unsigned const Bits = static_cast<unsigned>(Packed);
    OutRgb[0] = static_cast<unsigned char>(Bits >> 0x10);
    OutRgb[1] = static_cast<unsigned char>(Bits >> 8);
    OutRgb[2] = static_cast<unsigned char>(Bits);
}

namespace
{
    void LinkIntoRegisteredList(FEngineLight* Light)
    {
        // FUN_004104e0's list-insert arm: append at tail.
        FEngineLight* const OldTail = G_LightListTail;
        if (OldTail != nullptr)
            SetListNext(OldTail, Light);
        else
            G_LightListHead = Light;
        G_LightListTail = Light;
        SetListPrev(Light, OldTail);
        SetListNext(Light, nullptr);
        ++G_LightListCount;

        // Unconditional in retail.
        DALib::Pipeline->SetLightEnable(&Light->D3d, 1);
    }
}

bool FEngineLight::ApplyNearby(ICamera* Camera, float const* Pos3, float* InOutAmbient)
{
    // FUN_004104e0 (thiscall, RET 0xc). No argument is tested.
    bool bLit = false;
    Vector Dir = {};
    if ((Flags & kLightFlagEnabled) != 0)
    {
        if ((Flags & kLightFlagHasD3d) != 0)
        {
            ComputeDirection(&Dir, nullptr, nullptr);
            bLit = true;
        }
        else
        {
            float Dist = -1.0f;
            ComputeDirection(&Dir, Pos3, &Dist);

            if ((Flags & kLightFlagHasCurve) != 0 &&
                (Type == kLightDirectional || Type == kLightSpot))
            {
                if (Type != kLightPoint)
                {
                    D3d.Direction[0] = Dir.x;
                    D3d.Direction[1] = Dir.y;
                    D3d.Direction[2] = -Dir.z;
                }
                Vector Neg;
                Vec3Negate(&Neg, &Dir);
                Orient.E[0][2] = Neg.x;
                Orient.E[1][2] = Neg.y;
                Orient.E[2][2] = Neg.z;
                PushUpdate();
            }

            // FUN_00410850 always writes the colour; a modified one becomes the diffuse.
            float Color[3];
            bool const bColorMod = GetEffectiveColor(Dist, Color);
            if (bColorMod)
            {
                D3d.Diffuse[0] = Color[0];
                D3d.Diffuse[1] = Color[1];
                D3d.Diffuse[2] = Color[2];
                D3d.Diffuse[3] = 1.0f;
                PushLightUpdate(this);
            }

            // An ambient light adds its colour, scaled by its ambient curve at Dist when it has
            // one, to the accumulator.
            if (Type == kLightAmbient)
            {
                float Scale = 1.0f;
                if (AmbientCurve != nullptr)
                {
                    FGraphRange Reach;
                    Reach.bClamp = 1;
                    Reach.Min = 0.0f;
                    Reach.Max = D3d.Range;
                    Scale = static_cast<float>(AmbientCurve->Evaluate(Dist, &Reach));
                }
                float Scaled[3];
                ScaleVec3(Scaled, Color, Scale);
                InOutAmbient[0] += Scaled[0];
                InOutAmbient[1] += Scaled[1];
                InOutAmbient[2] += Scaled[2];
            }

            // Only an ordered "less" is unlit; unordered is lit.
            double const Sum = (static_cast<double>(Color[2]) + Color[1]) + Color[0];
            bLit = !(Sum < static_cast<double>(1.0f / 255.0f));
        }

        // DAT_00610588 as it is (no query here) -- non-zero goes soft.
        if (G_PipelineLightState != 0)
            SoftCameraRewriteLight(this, Camera, &Dir);
    }

    // Bit 4 (list link) follows bLit.
    unsigned char const OldFlags = Flags;
    bool const bWasLinked = (OldFlags & kLightFlagLinked) != 0;
    if (bWasLinked != bLit)
    {
        unsigned char const NewFlags = static_cast<unsigned char>(
            (((static_cast<unsigned>(bLit) << 4) ^ OldFlags) & kLightFlagLinked) ^ OldFlags);
        Flags = NewFlags;
        if ((NewFlags & kLightFlagLinked) != 0)
        {
            LinkIntoRegisteredList(this);
            return bLit;
        }
        UnlinkFromList(this);
    }
    return bLit;
}

void EngineLightClearRegisteredList()
{
    // FUN_004101e0.
    FEngineLight* Light = G_LightListHead;
    while (Light != nullptr)
    {
        // Unconditional in retail.
        DALib::Pipeline->SetLightEnable(&Light->D3d, 0);
        Light->Flags = static_cast<unsigned char>(Light->Flags & ~kLightFlagLinked);
        Light = ListNext(Light);
    }
    G_LightListHead = nullptr;
    G_LightListTail = nullptr;
    G_LightListCount = 0;
}

namespace
{
    // FUN_004104c0 (cdecl): clamp an int to [0, 0xff].
    int ClampColorByte(int Value)
    {
        if (Value < 0)
            return 0;
        if (Value > 0xff)
            return 0xff;
        return Value;
    }
}

void EngineLightApplyList(FEngineLight* const* Lights, unsigned Count, ICamera* Camera,
                          float const* Pos3)
{
    // FUN_004102c0.
    if (GetGameOption(kGameOptLighting) == 0)
    {
        // FUN_004101e0's walk, inlined in retail.
        EngineLightClearRegisteredList();
        DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
        return;
    }

    int Packed = 0;
    DALib::Pipeline->GetRenderState(D3DRS_AMBIENT, &Packed);
    const float kInv255 = 1.0f / 255.0f;
    float Ambient[3];
    Ambient[0] = static_cast<float>((static_cast<unsigned>(Packed) >> 16) & 0xffu) * kInv255;
    Ambient[1] = static_cast<float>((static_cast<unsigned>(Packed) >> 8) & 0xffu) * kInv255;
    Ambient[2] = static_cast<float>(static_cast<unsigned>(Packed) & 0xffu) * kInv255;

    EngineLightClearRegisteredList();

    unsigned Index = 0;
    for (Index = 0; Index < Count; ++Index)
        Lights[Index]->ApplyNearby(Camera, Pos3, Ambient);

    // Each float * 255.0 in double, truncated and clamped (G and B through FUN_004104c0).
    int const R = ClampColorByte(static_cast<int>(static_cast<double>(Ambient[0]) * 255.0));
    int const G = ClampColorByte(static_cast<int>(static_cast<double>(Ambient[1]) * 255.0));
    int const B = ClampColorByte(static_cast<int>(static_cast<double>(Ambient[2]) * 255.0));
    unsigned Out = static_cast<unsigned>(R) | 0xffffff00u;
    Out = (Out << 8) | static_cast<unsigned>(G);
    Out = (Out << 8) | static_cast<unsigned>(B);
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, static_cast<int>(Out));
}

namespace
{
    // DAT_0067556c / DAT_00675570 / DAT_00675574 -- the gamelit enable list: count, head, tail,
    // chained through FGameLight::ListNext (+0xc8) / ListPrev (+0xcc).
    int G_GameLightCount = 0;
    FGameLight* G_GameLightHead = nullptr;
    FGameLight* G_GameLightTail = nullptr;

    // GameLight vtable slots 0 / 1 / 2, as retail dispatches them.

    void GameLightCallDestroy(FGameLight* Light)
    {
        delete Light;
    }

    bool GameLightCallEnable(FGameLight* Light)
    {
        return Light->Enable();
    }

    void GameLightCallDisable(FGameLight* Light)
    {
        Light->Disable();
    }
}

// The GameLight base subobject is at +0; null remains null.
FEngineLight* EngineLightGameListHead()
{
    return reinterpret_cast<FEngineLight*>(G_GameLightHead);
}

FEngineLight* EngineLightGameListNext(FEngineLight* Light)
{
    // +0xc8 of the GameLight containing this EngineLight base.
    return reinterpret_cast<FEngineLight*>(reinterpret_cast<FGameLight*>(Light)->ListNext);
}

// FUN_00537690, thiscall on a GameLight.
bool FGameLight::Enable()
{
    if (((Flags >> 3) & 1u) != 0)
        return true;
    if (!FEngineLight::Enable())
        return false;
    // Append at the tail, count and reference.
    ListPrev = G_GameLightTail;
    if (G_GameLightTail != nullptr)
        G_GameLightTail->ListNext = this;
    else
        G_GameLightHead = this;
    G_GameLightTail = this;
    ListNext = nullptr;
    ++G_GameLightCount;
    ++RefCount;
    return true;
}

// FUN_005376f0, thiscall on a GameLight.
void FGameLight::Disable()
{
    if ((Flags & kLightFlagEnabled) == 0)
        return;
    FEngineLight::Disable();
    FGameLight* const Prev = ListPrev;
    if (Prev == nullptr)
    {
        G_GameLightHead = ListNext;
        if (G_GameLightHead != nullptr)
            G_GameLightHead->ListPrev = nullptr;
        else
            G_GameLightTail = nullptr;
    }
    else if (ListNext == nullptr)
    {
        G_GameLightTail = Prev;
        Prev->ListNext = nullptr;
    }
    else
    {
        Prev->ListNext = ListNext;
        ListNext->ListPrev = ListPrev;
    }
    // Clear the links, count and reference; slot 0 (1) at <= 0.
    ListPrev = nullptr;
    ListNext = nullptr;
    --G_GameLightCount;
    int const Remaining = --RefCount;
    if (Remaining <= 0)
        GameLightCallDestroy(this);
}

void EngineLightSetAmbientRgbBytes(unsigned char const Rgb[3])
{
    // FUN_00410240. Retail does not test Rgb or DALib::Pipeline for null.
    unsigned Packed = static_cast<unsigned>(Rgb[0]) | 0xffffff00u;
    Packed = (Packed << 8) | static_cast<unsigned>(Rgb[1]);
    Packed = (Packed << 8) | static_cast<unsigned>(Rgb[2]);
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, static_cast<int>(Packed));
}

namespace
{
    // DAT_00675558. LAB_00538a40 constructs it (FUN_00538a70) and registers FUN_00539000 with
    // atexit -- this object's static constructor and destructor.
    OwnerList<Light::AnimationArch> G_LightAnimations;

    // DAT_006789a4 -- the GameLight block pool {ElementSize 0x12C, FreeList 0, MaxFreeCount -1,
    // CurrentFreeCount 0}, stored by the static initializer LAB_005374b0. FUN_00537a90 and
    // FUN_00537af0 allocate from it, FUN_005375a0 returns every deleted GameLight to it, and
    // FUN_00537ab0 shrinks it.
    Heap G_GameLightHeap(0x12cu, -1);

}

FGameLight* GameLightConstruct(FGameLight* Light)
{
    return ::new (Light) FGameLight;
}

FGameLight::FGameLight() : FEngineLight(kLightNone)
{
    // FUN_005374e0 (thiscall). No null test; the list links +0xc8 / +0xcc are not written
    // (FGameLight::Enable sets them).
    this->RefCount = 1;
    memset(&this->Anim, 0, sizeof(this->Anim));
    this->Anim.CurrentIndex = -1;
    this->Anim.Elapsed = -1.0f;
    this->EngineIndex = 0xffffffffu;
    memset(&this->LocalXform, 0, sizeof(this->LocalXform));
    this->LocalXform.Rotation.E[2][2] = 1.0f;
    this->LocalXform.Rotation.E[1][1] = 1.0f;
    this->LocalXform.Rotation.E[0][0] = 1.0f;
    this->VelocityX = 0.0f;
    this->VelocityY = 0.0f;
    this->VelocityZ = 0.0f;
}

FGameLight* GameLightCreatePoint(Vector const* Pos3, unsigned char const Rgb[3], float Range)
{
    // FUN_00537af0 — __cdecl. The block comes straight from the pool (not through
    // FUN_00537a90). Retail quirk: a null block skips the constructor and the setters then run
    // on null.
    void* const Mem = G_GameLightHeap.Malloc();
    FGameLight* Light = nullptr;
    if (Mem != nullptr)
        Light = GameLightConstruct(static_cast<FGameLight*>(Mem));
    ((FEngineLight*)Light)->SetType(kLightPoint, true);           // FUN_004110c0(1, 1)
    ((FEngineLight*)Light)->SetPosition(Pos3);          // FUN_00410ef0
    ((FEngineLight*)Light)->SetColorBytesAlt(Rgb);      // FUN_004115b0
    ((FEngineLight*)Light)->SetRadius(Range);           // FUN_00411300
    if (Light->Enable()) // vtable slot 1
        return Light;

    int const Remaining = --Light->RefCount;
    if (Remaining <= 0)
    {
        delete Light;
    }
    // Retail bug, kept: the float range goes as a double to a %d field.
    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Could not load light of range %d",
          "E:\\FL\\Scratch\\Source\\Client\\space\\gamelit.cpp", 0x203,
          static_cast<double>(Range));
    return nullptr;
}

Light::AnimationArch::AnimationArch()
    : OwnerList<AnimFrame>(), NameLen(0), KeyBuffers(true)
{
    Name[0] = '\0';
}

Light::AnimationArch::~AnimationArch()
{
    // FUN_00538560: free() each spare copy and clear the slot; the member's and base's
    // destructors then run as in retail.
    for (MetaNode<void>* Node = KeyBuffers.get_head(); Node != nullptr; Node = Node->Next)
    {
        ::free(Node->Data);
        Node->Data = nullptr;
    }
}

void LightAnimationLoad(char const* Path)
{
    INI_Reader Reader;
    if (!Reader.open(Path, false) || !Reader.read_header())
        return;

    do
    {
        if (!Reader.is_header("LightAnim"))
            continue;

        Light::AnimationArch* const Anim = new Light::AnimationArch();
        Light::AnimFrame* LastFrame = nullptr; // cleared per section
        if (Reader.read_value())
        {
            do
            {
                if (Reader.is_value("name"))
                {
                    char const* const Value = Reader.get_value_string();
                    if (Value == nullptr)
                    {
                        Anim->NameLen = 0;
                        Anim->Name[0] = '\0';
                    }
                    else
                    {
                        Anim->NameLen = CopyBoundedString(Anim->Name, 0x20, Value);
                    }
                }
                else if (Reader.is_value("frame"))
                {
                    LastFrame = new Light::AnimFrame;
                    LastFrame->Time = Reader.get_value_float(0);
                    LastFrame->Rgb[0] = static_cast<unsigned char>(
                        static_cast<int>(Reader.get_value_float(1)));
                    LastFrame->Rgb[1] = static_cast<unsigned char>(
                        static_cast<int>(Reader.get_value_float(2)));
                    LastFrame->Rgb[2] = static_cast<unsigned char>(
                        static_cast<int>(Reader.get_value_float(3)));
                    LastFrame->Intensity = Reader.get_value_float(4);
                    // Only a positive (or unordered) intensity is kept.
                    if (LastFrame->Intensity <= 0.0f)
                        LastFrame->Intensity = 0.01f;
                    LastFrame->EndMarker = 0;
                    Anim->append(LastFrame);
                }
            } while (Reader.read_value());
        }

        if (LastFrame != nullptr)
            LastFrame->EndMarker = -1;
        G_LightAnimations.append(Anim);
    } while (Reader.read_header());
}

void LightAnimationFreeAll()
{
    // FUN_00538ab0.
    G_LightAnimations.free();
}

Light::AnimationArch* GameLightFindAnimByName(char const* Name)
{
    // FUN_00538e80.
    for (MetaNode<Light::AnimationArch>* Node = G_LightAnimations.get_head(); Node != nullptr;
         Node = Node->Next)
    {
        if (_stricmp(Node->Data->Name, Name) == 0)
            return Node->Data;
    }
    return nullptr;
}

void FGameLightAnimSlot::BindAnimKeys(Light::AnimationArch* Anim, float RangeScale,
                                      float IntensityScale, float TimeScale, float StartTime)
{
    // FUN_005388e0 — thiscall on the slot, RET 0x14.
    if (Def != nullptr)
    {
        Def->KeyBuffers.append(static_cast<void*>(this->Keys));
        this->Keys = nullptr;
        Def = nullptr;
    }

    Def = Anim;
    MetaNode<void>* Spare = Anim->KeyBuffers.get_head();
    Light::AnimFrame* Buffer;
    if (Spare != nullptr)
        Buffer = static_cast<Light::AnimFrame*>(Spare->Data);
    else
        Buffer = static_cast<Light::AnimFrame*>(malloc(Anim->count() * sizeof(Light::AnimFrame)));
    Anim->KeyBuffers.remove(Spare); // FUN_0043cf80; a null node is a no-op
    this->Keys = Buffer;
    KeyCount = static_cast<int>(Def->count());

    int Index = 0;
    for (MetaNode<Light::AnimFrame>* Node = Def->get_head(); Node != nullptr;
         Node = Node->Next)
    {
        this->Keys[Index] = *Node->Data;
        this->Keys[Index].Time *= TimeScale;
        this->Keys[Index].Intensity *= IntensityScale;
        ++Index;
    }

    Elapsed = StartTime;
    this->RangeScale = RangeScale;
    CurrentIndex = 0;

    int const Count = KeyCount;
    Light::AnimFrame const* const Keys = this->Keys;
    // A last key at or before the start time (ordered) ends it here.
    if (Keys[Count - 1].Time <= StartTime)
    {
        CurrentIndex = -1;
        return;
    }
    for (int Key = 0; Key < Count; ++Key)
    {
        // Keys at or after the start time (and unordered ones) are skipped.
        if (Keys[Key].Time < StartTime)
            CurrentIndex = Key;
    }
}

bool FGameLight::AttachAnim(Light::AnimationArch* Anim, float IntensityScale, float TimeScale,
                            float StartTime)
{
    // FUN_00537960.
    this->Anim.BindAnimKeys(Anim, D3d.Range, IntensityScale, TimeScale,
                            StartTime);
    bool const bBound = this->Anim.Keys != nullptr;
    if (bBound)
        ++RefCount;
    return bBound;
}

void FGameLightAnimSlot::TickAnim(float Dt, FGameLight* Light)
{
    // FUN_005386d0 — thiscall on the slot, RET 8.
    Light::AnimFrame* const Keys = this->Keys;
    if (Keys == nullptr)
        return;
    if (CurrentIndex < 0)
        return;

    int const Count = KeyCount;
    Light::AnimFrame const* const Last = &Keys[Count - 1];
    // The sum stays unrounded on the x87 stack for the compares.
    double const Elapsed = static_cast<double>(Dt) + this->Elapsed;
    this->Elapsed = static_cast<float>(Elapsed);

    // Before the last key (or unordered) scans forward.
    if (!(Elapsed >= Last->Time))
    {
        for (int Index = CurrentIndex; Index < Count; ++Index)
        {
            // Only a key strictly before Elapsed moves the index.
            if (Elapsed > Keys[Index].Time)
                CurrentIndex = Index;
        }
    }
    else
    {
        CurrentIndex = -1;
    }

    int const Current = CurrentIndex;
    if (Current != -1)
    {
        Light::AnimFrame const& From = Keys[Current];
        Light::AnimFrame const& To = Keys[Current + 1];
        // The blend is rounded to float before use.
        float const Blend = static_cast<float>((Elapsed - From.Time) /
                                               (static_cast<double>(To.Time) - From.Time));

        // Per channel in double, truncated, low byte kept.
        unsigned char Rgb[3];
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            int const Delta =
                static_cast<int>(To.Rgb[Channel]) - static_cast<int>(From.Rgb[Channel]);
            Rgb[Channel] = static_cast<unsigned char>(
                static_cast<int>(Delta * static_cast<double>(Blend) + From.Rgb[Channel]));
        }
        ((FEngineLight*)Light)->SetColorBytesAlt(Rgb); // FUN_004115b0

        double const Intensity =
            (static_cast<double>(To.Intensity) - From.Intensity) * Blend + From.Intensity;
        ((FEngineLight*)Light)->SetRadius(static_cast<float>(Intensity * RangeScale));
        return;
    }

    if (Last->EndMarker == -1)
    {
        // FUN_005379b0, inlined in retail.
        Light->AnimDetach();
        return;
    }

    ((FEngineLight*)Light)->SetColorBytesAlt(Last->Rgb);
    ((FEngineLight*)Light)->SetRadius(Last->Intensity * RangeScale);
}

I3DMathEngine* G_GameLightMath = nullptr; // DAT_006789c0

void FGameLight::Tick(float Dt)
{
    // FUN_00537860 (thiscall, RET 4). No null test on the light: both callers (the list walk
    // FUN_00538390 through vtable slot 3 and the equip light's tick) hand a live one.
    if ((Flags & kLightFlagEnabled) == 0)
        return;
    if (EngineIndex == 0xffffffffu)
    {
        // x87 precision: the z product is rounded to a float before its add; x and y are
        // rounded once, after theirs.
        double const Xv = static_cast<double>(Dt) * static_cast<double>(VelocityX);
        double const Yv = static_cast<double>(Dt) * static_cast<double>(VelocityY);
        float const Zv = static_cast<float>(static_cast<double>(Dt) * static_cast<double>(VelocityZ));
        Vector Pos;
        Pos.x = static_cast<float>(Xv + static_cast<double>(Position.x));
        Pos.y = static_cast<float>(Yv + static_cast<double>(Position.y));
        Pos.z = static_cast<float>(static_cast<double>(Zv) + static_cast<double>(Position.z));
        ((FEngineLight*)this)->SetPosition(&Pos);
    }
    else
    {
        // No null test on the engine or the result.
        const float* const InstRaw = (const float*)DALib::Engine->GetInstanceTransform(
            EngineIndex);
        if (G_GameLightMath == nullptr)
            G_GameLightMath = Acquire3DMathEngine();
        Transform World;
        G_GameLightMath->MulTransform(World, *reinterpret_cast<Transform const*>(InstRaw),
                                      LocalXform);
        Transform const Copy = World;
        EngineLightSetTransform((FEngineLight*)this, &Copy.Rotation.E[0][0], &Copy.Translation);
    }
    if (Anim.Keys != nullptr)
        Anim.TickAnim(Dt, this);
}

void GameLightTickAll(float Dt)
{
    // FUN_00538390: each light's next is read before its vtable slot 3 runs.
    FGameLight* Light = G_GameLightHead;
    while (Light != nullptr)
    {
        FGameLight* const Next = Light->ListNext;
        Light->Tick(Dt);
        Light = Next;
    }
}

void GameLightLeaveSystem()
{
    // FUN_005383c0.
    FGameLight* LocalHead = nullptr;
    FGameLight* LocalTail = nullptr;
    int LocalCount = 0;              // never read
    FGameLight* Light = G_GameLightHead;
    while (Light != nullptr)
    {
        FGameLight* const Next = Light->ListNext;
        ++Light->RefCount;
        // FUN_005379b0, inlined in retail. Retail quirk: a light destroyed here is still used
        // below.
        if (Light->Anim.Keys != nullptr)
        {
            if (Light->Anim.Def != nullptr)
            {
                // FUN_005ae700 on Def+0x38 with Keys.
                Light->Anim.Def->KeyBuffers.append(static_cast<void*>(Light->Anim.Keys));
                Light->Anim.Keys = nullptr;
                Light->Anim.Def = nullptr;
            }
            GameLightCallDisable(Light);
            int const AfterDetach = --Light->RefCount;
            if (AfterDetach <= 0)
                GameLightCallDestroy(Light);
        }
        // Retail quirk: exactly 0 is not relinked, but a negative count falls through into the
        // relink of the destroyed light.
        GameLightCallDisable(Light);
        int const Remaining = --Light->RefCount;
        bool bRelink = true;
        if (Remaining <= 0)
        {
            GameLightCallDestroy(Light);
            if (Remaining == 0)
                bRelink = false;
        }
        if (bRelink)
        {
            Light->ListPrev = LocalTail;
            if (LocalTail != nullptr)
                LocalTail->ListNext = Light;
            else
                LocalHead = Light;
            ++LocalCount;
            LocalTail = Light;
            Light->ListNext = nullptr;
        }
        Light = Next;
    }
    (void)LocalCount;

    // Each local entry unlinked from its neighbours, its links cleared, then re-enabled.
    Light = LocalHead;
    while (Light != nullptr)
    {
        FGameLight* const Prev = Light->ListPrev;
        FGameLight* const Next = Light->ListNext;
        if (Prev == nullptr)
        {
            if (Next != nullptr)
                Next->ListPrev = nullptr;
        }
        else if (Next == nullptr)
        {
            Prev->ListNext = nullptr;
        }
        else
        {
            Prev->ListNext = Next;
            Next->ListPrev = Light->ListPrev;
        }
        Light->ListPrev = nullptr;
        Light->ListNext = nullptr;
        GameLightCallEnable(Light);
        Light = Next;
    }
}

void GameLightReleaseAll()
{
    // FUN_00538520. The list itself is not touched.
    FGameLight* Light = G_GameLightHead;
    while (Light != nullptr)
    {
        FGameLight* const Next = Light->ListNext;
        int const Remaining = --Light->RefCount;
        if (Remaining <= 0)
            GameLightCallDestroy(Light);
        Light = Next;
    }
}

void* GameLightHeapAlloc(size_t Size)
{
    // FUN_00537a90 — GameLight::operator new.
    if (Size != 0x12c)
        return malloc(Size);
    return G_GameLightHeap.Malloc();
}

void GameLightHeapShrink()
{
    // FUN_00537ab0.
    G_GameLightHeap.Shrink();
}

void FGameLight::AddRef()
{
    // FUN_00537ac0 (thiscall). No null test (every caller tests or dereferences the light
    // first).
    RefCount = RefCount + 1;
}

int FGameLight::Release()
{
    // FUN_00537ad0 (thiscall): at <= 0 the deleting destructor (vtable slot 0); the new count
    // returned. No null test.
    int const Next = RefCount - 1;
    RefCount = Next;
    if (Next <= 0)
    {
        delete this;
    }
    return Next;
}

void FGameLight::AnimDetach()
{
    // FUN_005379b0.
    if (Anim.Keys == nullptr)
        return;
    if (Anim.Def != nullptr)
    {
        Anim.Def->KeyBuffers.append(static_cast<void*>(Anim.Keys));
        Anim.Keys = nullptr;
        Anim.Def = nullptr;
    }

    Disable(); // vtable slot 2
    int const Remaining = --RefCount;
    if (Remaining <= 0)
        delete this; // vtable slot 0
}

void FGameLight::Load(INI_Reader* Reader)
{
    // FUN_00537a40 — __thiscall, RET 4: every value of the [LightSource] section goes to
    // FUN_00411690 (value string fetched before the key name, as retail does), then vtable
    // slot 1 (enable).
    while (Reader->read_value())
    {
        char const* const Value = Reader->get_value_string();
        char const* const Name = Reader->get_name_ptr();
        ((FEngineLight*)this)->ReadValue(Name, Value);
    }
    Enable(); // vtable slot 1
}

FEngineLight::~FEngineLight()
{
    DestructLightBody(this);
}

FGameLight::~FGameLight()
{
    Light::AnimationArch* const Def = this->Anim.Def;
    if (Def != nullptr)
    {
        Def->KeyBuffers.append(static_cast<void*>(this->Anim.Keys));
        this->Anim.Keys = nullptr;
        this->Anim.Def = nullptr;
    }
}

void FGameLight::operator delete(void* Memory)
{
    G_GameLightHeap.Free(Memory);
}

