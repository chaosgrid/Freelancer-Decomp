// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/Thorn/gf_thorn_psys.h"

#include "Common/Graphics/alchemy_float_forward.h"
#include "alchemy.h"
#include "common_ini.h" // StringAlloc
#include "dacom.h"
#include "GameFlow/gf_object_ship.h"
#include "Common/Graphics/vis_effect_arch.h"

#include <cstdint>
#include <cstring>

namespace
{
    char const* const GScriptPsysFile = "E:\\FL\\Scratch\\Source\\Client\\GF\\ScriptPsys.cpp";

    // IFxEffect slots 7 / 8 take the float Dt through a pointer-sized argument.
    void* FloatArg(float Value)
    {
        void* Out = nullptr;
        memcpy(&Out, &Value, sizeof(Value));
        return Out;
    }

}

// ---------------------------------------------------------------------------------------
// Thorn_Psys
// ---------------------------------------------------------------------------------------

// FUN_00453d70.
FThornPsys::FThornPsys(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Vis(nullptr),
      Sparam(0.0f)
{
    Particles.Effect = nullptr;
    Particles.Vis = nullptr;
    Local.Pos.x = 0.0f;
    Local.Pos.y = 0.0f;
    Local.Pos.z = 0.0f;
    MatrixSetIdentity(Local.Orient);
}

// FUN_00453e60. FindVisEffectByNickname is FUN_0042dd20, VisEffectArchAddRef FUN_0042e080. The
// StringAlloc'd copy is never freed, as retail.
void FThornPsys::BindVisEffect(char const* EffectName)
{
    Vis = FindVisEffectByNickname(StringAlloc(EffectName, false));
    if (Vis != nullptr)
    {
        VisEffectArchAddRef(Vis);
        return;
    }
    FDUMP(0x100001, "%s(%d) : *** ERROR: Thorn_Psys: Failed to find [VisEffect] %s.",
          GScriptPsysFile, 0x4c, EffectName);
}

// LAB_00453f00. No script-frame transform.
GENRESULT DACOM_API FThornPsys::SetTransform(float const* Transform12)
{
    Local.Pos.x = Transform12[9];
    Local.Pos.y = Transform12[10];
    Local.Pos.z = Transform12[11];
    memcpy(&Local.Orient, Transform12, sizeof(Local.Orient));
    return GR_OK;
}

// LAB_00453f60.
GENRESULT DACOM_API FThornPsys::GetTransform(float* OutTransform12)
{
    memcpy(OutTransform12, &Local.Orient, sizeof(Local.Orient));
    OutTransform12[9] = Local.Pos.x;
    OutTransform12[10] = Local.Pos.y;
    OutTransform12[11] = Local.Pos.z;
    return GR_OK;
}

// LAB_00453ec0.
GENRESULT DACOM_API FThornPsys::SetPosition(float* Position)
{
    Local.Pos.x = Position[0];
    Local.Pos.y = Position[1];
    Local.Pos.z = Position[2];
    return GR_OK;
}

// LAB_00453ee0.
GENRESULT DACOM_API FThornPsys::SetOrientation(float* Orient9)
{
    memcpy(&Local.Orient, Orient9, sizeof(Local.Orient));
    return GR_OK;
}

// LAB_00454770.
GENRESULT DACOM_API FThornPsys::GetPosition(float* OutPosition)
{
    OutPosition[0] = Local.Pos.x;
    OutPosition[1] = Local.Pos.y;
    OutPosition[2] = Local.Pos.z;
    return GR_OK;
}

// LAB_00453f40.
GENRESULT DACOM_API FThornPsys::GetOrientation(float* OutOrient9)
{
    memcpy(OutOrient9, &Local.Orient, sizeof(Local.Orient));
    return GR_OK;
}

// LAB_00453fb0. Particles.Create is FUN_00557360.
GENRESULT DACOM_API FThornPsys::Start()
{
    if (Vis != nullptr)
        Particles.Create(Vis);
    return GR_OK;
}

// LAB_00453fd0. Particles.Destroy is FUN_005574a0.
GENRESULT DACOM_API FThornPsys::StopEffect()
{
    Particles.Destroy();
    return GR_OK;
}

// LAB_00453ff0. 0.001 is the double DAT_005c75f8; Dt is stored as a float.
GENRESULT DACOM_API FThornPsys::Tick(int DeltaMs)
{
    float const Dt = static_cast<float>(static_cast<double>(DeltaMs) * 0.001);
    if (Particles.Effect != nullptr)
    {
        float Xform[12];
        memcpy(Xform, &Local.Orient, sizeof(float) * 9);
        Xform[9] = Local.Pos.x;
        Xform[10] = Local.Pos.y;
        Xform[11] = Local.Pos.z;
        Particles.Effect->SetTransform(reinterpret_cast<void*>(1), Xform);
        Particles.Effect->ComposeTransforms(FloatArg(Dt));
        Particles.Effect->Update(FloatArg(Dt));
    }
    return GR_OK;
}

// LAB_00454070.
GENRESULT DACOM_API FThornPsys::SetSparam(float Value)
{
    Sparam = Value;
    if (Particles.Effect != nullptr)
        ForwardAlchemyEffectFloat(Particles.Effect, &IFxEffect::SetOuterTime, Value);
    return GR_OK;
}

// LAB_004540a0.
GENRESULT DACOM_API FThornPsys::GetSparam(float* OutValue)
{
    *OutValue = Sparam;
    return GR_OK;
}

// FUN_00453e30. StopEffect is called through the vtable; Vis keeps its value after the release
// (FUN_0042e160).
void FThornPsys::Stop()
{
    static_cast<IPSysControl*>(this)->StopEffect();
    if (Vis != nullptr)
        VisEffectArchRelease(Vis);
}

// FUN_00450870.
void* FThornPsys::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

// LAB_00453e50 -- FUN_005574d0 on +0x6c.
void FThornPsys::DrawEffects(ICamera* Camera)
{
    Particles.Draw(Camera);
}

// ---------------------------------------------------------------------------------------
// Thorn_PlayerEnginePsys
// ---------------------------------------------------------------------------------------

// FUN_004540c0.
FThornPlayerEnginePsys::FThornPlayerEnginePsys(Csys const& ScriptFrame, FGfScript* InOwner,
                                               FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Sparam(0.0f)
{
}

// LAB_00454150. GfObjectShip is FUN_00446be0, StartEngineEffects FUN_00447d70.
GENRESULT DACOM_API FThornPlayerEnginePsys::Start()
{
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->StartEngineEffects();
    return GR_OK;
}

// LAB_00454170. StopEngineEffects is FUN_00447f00.
GENRESULT DACOM_API FThornPlayerEnginePsys::StopEffect()
{
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->StopEngineEffects();
    return GR_OK;
}

// LAB_004541a0. SetEngineSparam is FUN_00447f50.
GENRESULT DACOM_API FThornPlayerEnginePsys::SetSparam(float Value)
{
    Sparam = Value;
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->SetEngineSparam(Value);
    return GR_OK;
}

// LAB_004541d0.
GENRESULT DACOM_API FThornPlayerEnginePsys::GetSparam(float* OutValue)
{
    *OutValue = Sparam;
    return GR_OK;
}

// LAB_00454130 -- the psys facet's slot 4 through its vtable.
void FThornPlayerEnginePsys::Stop()
{
    static_cast<IPSysControl*>(this)->StopEffect();
}

// LAB_00454140 -- answers 0xfffffff9, the "not implemented" code, not -1.
void* FThornPlayerEnginePsys::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(kThornSlotNotImplemented));
}
