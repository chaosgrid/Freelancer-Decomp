// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Gf script spawn -- E:\FL\Scratch\Source\Client\Gf\Script.cpp: FGfScript::Spawn (FUN_0044ed90)
// and the per-entity factories it switches to. Each concrete Thorn object is built with (script
// frame = the script's +0x00, owner = the script, entity snapshot).

#include "GameFlow/Thorn/gf_script_object.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "common_csys.h"
#include "common_fuses.h"   // PetalInterfaceDatabase::get_category
#include "common_goods.h"   // ThornGetUser*, ThornGetRenderType, RoomData, SetpointProperties
#include "common_misc.h"    // malloc_strcpy
#include "common_newchar.h" // RtcSlider::is_enabled
#include "dacom.h"
#include "GameFlow/gf_character_object.h"
#include "GameFlow/gf_object_ship.h"
#include "GameFlow/Thorn/gf_thorn_camera.h"
#include "GameFlow/Thorn/gf_thorn_component.h"
#include "GameFlow/Thorn/gf_thorn_compound.h"
#include "GameFlow/Thorn/gf_thorn_deformable.h"
#include "GameFlow/Thorn/gf_thorn_light.h"
#include "GameFlow/Thorn/gf_thorn_marker.h"
#include "GameFlow/Thorn/gf_thorn_psys.h"
#include "GameFlow/Thorn/gf_thorn_scene.h"
#include "GameFlow/Thorn/gf_thorn_sound.h"
#include "NeuroNet/nn_arrival_text.h"
#include "GameFlow/object_prop.h"
#include "GameFlow/room_control.h"
#include "thorn_iface.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // 0x005cc32c -- the file name every FDUMP of this unit passes.
    char const* const GScriptFile = "E:\\FL\\Scratch\\Source\\Client\\Gf\\Script.cpp";

    FThornEntitySnap const* EntitySnap(IScriptEngine* Engine, int Index)
    {
        return static_cast<FThornEntitySnap const*>(Engine->GetEntityDesc(Index));
    }

    // The geo facet (+0x0c) slots 3 / 4 take non-const pointers; retail hands them the
    // snapshot's / a local's floats directly.
    void SetGeoPose(FThornComponent* Object, float const* Pos3, float const* Orient9)
    {
        Object->SetPosition((const Vector*)(const_cast<float*>(Pos3)));
        Object->SetOrientation((const Matrix*)(const_cast<float*>(Orient9)));
    }

}

// FUN_0044ed90. A "priority" the RtcSlider does not enable skips the entity (its entry stays
// null). The initial writes in the last pass are skipped for a background sound (slot 22) and
// for a reference entity.
void FGfScript::Spawn()
{
    int const Count = static_cast<int>(Engine->GetEntityCount());
    Spawned = new FThornComponent*[Count];
    SpawnedCount = Count;
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
        Spawned[Index] = nullptr;

    FThornComponent* LastScene = nullptr;
    for (Index = 0; Index < Count; ++Index)
    {
        FThornEntitySnap const* const Snap = EntitySnap(Engine, Index);
        unsigned char const bReference = static_cast<unsigned char>(Snap->Flags & 1);
        unsigned char const bFlagBit1 = static_cast<unsigned char>((Snap->Flags >> 1) & 1);
        unsigned char const bFlagBit2 = static_cast<unsigned char>((Snap->Flags >> 2) & 1);
        unsigned char bFog = 1;
        if (ThornGetUserString(Engine, Index, "nofog") != nullptr)
            bFog = 0;
        unsigned char bRunningLights = 0;
        if (ThornGetUserString(Engine, Index, "running_lights") != nullptr)
            bRunningLights = 1;
        char const* const PriorityName = ThornGetUserString(Engine, Index, "priority");
        if (PriorityName != nullptr && !RtcSlider::is_enabled(PriorityName))
            continue;

        FThornComponent* Object = nullptr;
        switch (Snap->Type)
        {
        case kThornEntityScene:
        {
            // FUN_0045b430.
            float TextStart = 0.0f;
            int TextString = 0;
            bool const bHasStart = ThornGetUserFloat(Engine, Index, "TextStart", &TextStart);
            bool const bHasString = ThornGetUserInt(Engine, Index, "TextString", &TextString);
            if (bHasStart && bHasString)
                NnArrivalTextShowRaw(TextStart, static_cast<uintptr_t>(TextString));
            // The scene's "priority" (read again) is the script's.
            char const* const ScenePriority = ThornGetUserString(Engine, Index, "priority");
            if (ScenePriority != nullptr)
                Priority = MsvcrtAtoi(ScenePriority);
            Object = SpawnScene(Snap);
            LastScene = Object; // null or not
            break;
        }
        case kThornEntityMarker:
            Object = SpawnMarker(Snap);
            break;
        case kThornEntitySound:
            if (bReference != 0)
                Object = SpawnSoundReference(Snap);
            else if (_stricmp(Snap->TemplateName, "PlayerShipSound") != 0)
                Object = SpawnSound(Index, Snap);
            else
                Object = SpawnPlayerEngineSound(Snap);
            break;
        case kThornEntityDeformable:
            Object = SpawnDeformable(Index, Snap);
            break;
        case kThornEntityCompound:
            Object = SpawnCompound(Index, Snap);
            break;
        case kThornEntityMonitor:
            Object = SpawnMonitor(Snap);
            break;
        case kThornEntityCamera:
            Object = bReference != 0 ? SpawnCameraReference(Snap) : SpawnCamera(Snap);
            break;
        case kThornEntityLight:
            Object = bReference != 0 ? SpawnLightReference(Snap) : SpawnLight(Snap);
            break;
        case kThornEntityPsys:
        {
            // Without a "priority" the "script_particles" slider decides.
            if (PriorityName == nullptr && !RtcSlider::is_enabled("script_particles"))
                break;
            char const* const Template = Snap->TemplateName;
            if (_stricmp(Template, "PlayerShipEngines") == 0)
            {
                Object = SpawnPlayerEnginePsys(Snap);
                break;
            }
            // "PlayerShipHover" only while the Gf ship exists.
            if (_stricmp(Template, "PlayerShipHover") == 0 && GfObjectShip() == nullptr)
                break;
            Object = SpawnPsys(Snap);
            break;
        }
        default:
            // 8 (hardpoint), 10, 11, 12 and anything else: no object.
            break;
        }

        if (Object != nullptr)
            Object->SetFlags(bFlagBit1, bFlagBit2, bFog, bRunningLights); // slot 20
        Spawned[Index] = Object;
    }

    for (Index = 0; Index < Count; ++Index)
    {
        if (Spawned[Index] == nullptr)
            continue;
        Spawned[Index]->SetScene(LastScene);         // slot 19
        Spawned[Index]->SetSpawnTable(Spawned, Count); // slot 15
    }

    for (Index = 0; Index < Count; ++Index)
    {
        if (Spawned[Index] == nullptr)
            continue;
        Engine->AttachEntity(Index, static_cast<ICompoundControl*>(Spawned[Index]));
        Spawned[Index]->EntityIndex = Index;
    }

    for (Index = 0; Index < Count; ++Index)
    {
        FThornComponent* const Object = Spawned[Index];
        if (Object == nullptr)
            continue;
        FThornEntitySnap const* const Snap = EntitySnap(Engine, Index);
        unsigned char const bReference = static_cast<unsigned char>(Snap->Flags & 1);
        if (Object->IsBackgroundSound() || bReference != 0)
            continue;

        int const Type = Snap->Type;
        if (Type == kThornEntityCamera)
        {
            Object->SetFov(&Snap->Fovh);       // camera facet slot 3
            Object->SetNear(&Snap->NearPlane); // slot 6
            Object->SetFar(&Snap->FarPlane);   // slot 7
        }
        else if (Type == kThornEntitySound)
        {
            Object->SetAudioProps(Snap->Audio, -1); // sound facet slot 9
        }

        if (bApplySpatial != 0)
        {
            if (Type == kThornEntityCompound || Type == kThornEntityDeformable ||
                Type == kThornEntityCamera || Type == kThornEntitySound || Type == kThornEntityPsys)
            {
                SetGeoPose(Object, Snap->Pos, Snap->Orient);
            }
            if (Type == kThornEntityDeformable)
                Object->SetFloorHeight(Snap->FloorHeight); // identity slot 5
        }

        if (Type == kThornEntityMarker)
        {
            ApplySpecialMarker(Snap, Object);
        }
        else if (Type == kThornEntityCamera)
        {
            float CameraProps[4];
            CameraProps[0] = Snap->Fovh;
            CameraProps[1] = Snap->NearPlane;
            CameraProps[2] = Snap->FarPlane;
            CameraProps[3] = Snap->Aspect;
            ApplySpecialCamera(Snap, Object, CameraProps);
        }
    }
}

// FUN_0044d630 -- operator new(0x78), Thorn_Scene ctor FUN_00454390. Retail's index argument is
// not read.
FThornComponent* FGfScript::SpawnScene(FThornEntitySnap const* Snap)
{
    return new FThorn_Scene(Frame, this, Snap);
}

// FUN_0044d6a0. A marker named "special/<segment>" is moved to the room's setpoint the script's
// own name maps to with that segment, in the script frame. "special/p" also hands the player
// character (FUN_00449cd0) the local height (RoomObject slot 3).
void FGfScript::ApplySpecialMarker(FThornEntitySnap const* Snap, FThornComponent* Obj)
{
    char const* const EntityName = Snap->Name;
    size_t const PrefixLength = strlen("special/");
    if (_strnicmp(EntityName, "special/", PrefixLength) != 0)
        return;
    char const* const Segment = EntityName + PrefixLength;

    SetpointProperties Props;
    Props.extract_from_name(Name);
    Props.set_segment(Segment);
    char const* const Mapped = Props.combine_to_name();
    SetpointInfo const* const Info =
        GetRoomRuntimeRoomData(Room)->find_setpoint_generic(Mapped, ScriptFile);
    if (Info == nullptr)
    {
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: Generic script %s applied on trigger %s uses special marker %s "
              "mapping to %s which not exist in setpoint layer\n",
              GScriptFile, 0x80, ScriptFile, Name, EntityName, Mapped);
        return;
    }

    Csys const Local = Frame.trans_to_frame(Info->Frame);
    SetGeoPose(Obj, &Local.Pos.x, &Local.Orient.E[0][0]);
    if (_stricmp(EntityName, "special/p") != 0)
        return;
    FCharacterObject* const Player = RoomControlPlayerCharacter(Room);
    if (Player != nullptr)
        Player->SetFloorHeight(Local.Pos.y);
}

// FUN_0044d810. Thorn_Marker is operator new(0x9c), Thorn_MarkerReference 0x70. A reference
// with no created marker of that name becomes a Thorn_Marker at the room setpoint of that name,
// or at the snapshot when the room has none.
FThornComponent* FGfScript::SpawnMarker(FThornEntitySnap const* Snap)
{
    if ((Snap->Flags & 1) == 0)
    {
        FThornMarker* const Marker = new FThornMarker(Frame, this, Snap);
        SetGeoPose(Marker, Snap->Pos, Snap->Orient);
        return Marker;
    }

    FThornComponent* const Found = G_GfScriptManager.FindCreatedEntity(kThornEntityMarker, Snap->Name);
    if (Found != nullptr)
    {
        FThornMarkerReference* const Reference = new FThornMarkerReference(Frame, this, Snap);
        Reference->Target = Found;
        return Reference;
    }

    FThornMarker* const Marker = new FThornMarker(Frame, this, Snap);
    SetpointInfo const* const Info =
        GetRoomRuntimeRoomData(Room)->find_setpoint(Snap->Name, ScriptFile);
    if (Info != nullptr)
    {
        Csys const Local = Frame.trans_to_frame(Info->Frame);
        SetGeoPose(Marker, &Local.Pos.x, &Local.Orient.E[0][0]);
    }
    else
    {
        SetGeoPose(Marker, Snap->Pos, Snap->Orient);
    }
    return Marker;
}

// FUN_0044d9b0 -- a MONITOR: operator new(0x70) Thorn_Camera on the room camera (Room +0x500),
// allowed to become the active camera (FUN_00450830 with 1).
FThornComponent* FGfScript::SpawnMonitor(FThornEntitySnap const* Snap)
{
    if ((Snap->Flags & 1) != 0)
        return nullptr;
    FThornCamera* const Camera = new FThornCamera(Frame, this, Snap);
    Camera->Register(GetRoomRuntimeRoomCamera(Room), Priority, 1);
    return Camera;
}

// FUN_0044da40. A camera named "camera/<segment>" is moved to the room camera the script's own
// name maps to with that segment, in the script frame, and takes CameraProps' fov / near / far.
void FGfScript::ApplySpecialCamera(FThornEntitySnap const* Snap, FThornComponent* Obj,
                                   float const* CameraProps4)
{
    char const* const EntityName = Snap->Name;
    size_t const PrefixLength = strlen("camera/");
    if (_strnicmp(EntityName, "camera/", PrefixLength) != 0)
        return;
    char const* const Segment = EntityName + PrefixLength;

    SetpointProperties Props;
    Props.extract_from_name(Name);
    Props.set_segment(Segment);
    char const* const Mapped = Props.combine_to_name();
    RoomCameraInfo const* const Info =
        GetRoomRuntimeRoomData(Room)->find_camera_generic(Mapped, ScriptFile);
    if (Info == nullptr)
    {
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: Cannot find remapped special camera %s implied by special "
              "camera name %s and character hardpoint name %s\n",
              GScriptFile, 0x121, Mapped, EntityName, Name);
        return;
    }

    Csys const Local = Frame.trans_to_frame(Info->Frame);
    SetGeoPose(Obj, &Local.Pos.x, &Local.Orient.E[0][0]);
    Obj->SetFov(&CameraProps4[0]);
    Obj->SetNear(&CameraProps4[1]);
    Obj->SetFar(&CameraProps4[2]);
}

// FUN_0044dba0 -- operator new(0x70) Thorn_Camera, never the active one by itself (FUN_00450830
// with 0).
FThornComponent* FGfScript::SpawnCamera(FThornEntitySnap const* Snap)
{
    FThornCamera* const Camera = new FThornCamera(Frame, this, Snap);
    Camera->Register(GetRoomRuntimeRoomCamera(Room), Priority, 0);
    return Camera;
}

// FUN_0044dc20. Without a created camera of that name, the room camera of that name becomes a
// Thorn_Camera posed at its frame as it is (no script-frame transform); neither gives a
// Thorn_CameraReference with no target.
FThornComponent* FGfScript::SpawnCameraReference(FThornEntitySnap const* Snap)
{
    FThornComponent* const Found = G_GfScriptManager.FindCreatedEntity(kThornEntityCamera, Snap->Name);
    if (Found != nullptr)
    {
        FThornCameraReference* const Reference = new FThornCameraReference(Frame, this, Snap);
        Reference->Target = Found;
        return Reference;
    }

    RoomCameraInfo const* const Info =
        GetRoomRuntimeRoomData(Room)->find_camera(Snap->Name, ScriptFile);
    if (Info == nullptr)
    {
        FThornCameraReference* const Reference = new FThornCameraReference(Frame, this, Snap);
        Reference->Target = nullptr;
        return Reference;
    }

    FThornCamera* const Camera = new FThornCamera(Frame, this, Snap);
    Camera->Register(GetRoomRuntimeRoomCamera(Room), Priority, 0);
    // The three floats are read before the calls.
    float Fov;
    float Near;
    float Far;
    memcpy(&Fov, &Snap->Fovh, sizeof(Fov));
    memcpy(&Near, &Snap->NearPlane, sizeof(Near));
    memcpy(&Far, &Snap->FarPlane, sizeof(Far));
    SetGeoPose(Camera, &Info->Frame.Pos.x, &Info->Frame.Orient.E[0][0]);
    Camera->SetFov(&Fov);
    Camera->SetNear(&Near);
    Camera->SetFar(&Far);
    return Camera;
}

// FUN_0044dd80. operator new(0x190) Thorn_Compound; SetInstance is FUN_00451330.
FThornComponent* FGfScript::SpawnCompound(int Index, FThornEntitySnap const* Snap)
{
    int const PetalCategory =
        PetalInterfaceDatabase::get_category(ThornGetUserString(Engine, Index, "category"));
    if (PetalCategory == 1 && _stricmp(Snap->TemplateName, "PlayerShip") == 0 &&
        GfObjectShip() == nullptr)
    {
        return nullptr;
    }

    FRoomObject* Object = nullptr;
    long Instance = -1;
    void* AnimScript = nullptr;
    Csys Offset;
    // Retail's stack slot for the category is not initialized by FUN_0044e430 when a reference
    // prop is missing; the port starts it at the ctor's default.
    int Category = -1;
    unsigned char bUseScene = 0;
    ResolveCompoundTarget(Index, &Object, &Instance, &AnimScript, &Offset, &Category, &bUseScene);

    FThornCompound* const Compound = new FThornCompound(Frame, Offset, this, Snap);
    Compound->Object = Object;
    Compound->SetInstance(Instance);
    Compound->AnimScript = AnimScript;
    Compound->Category = Category;
    Compound->bUseScene = bUseScene;
    if (Object != nullptr)
    {
        Object->LightGroup = Snap->LtGrp;
        Object->Layer = Snap->SrtGrp;
        Object->RenderMode = static_cast<int>(
            ThornGetRenderType(reinterpret_cast<ThornEntity const*>(Snap), ScriptFile));
    }
    return Compound;
}

// FUN_0044ded0. operator new(0x9f8) Thorn_Deformable. When no character resolves, retail leaks
// the actor copy (it jumps past the free). GetCharacterCsys is FUN_00452e40, GetDeformInstance
// FUN_00452eb0.
FThornComponent* FGfScript::SpawnDeformable(int Index, FThornEntitySnap const* Snap)
{
    if ((Snap->Flags & 1) != 0)
        return nullptr;

    char* Actor = nullptr;
    char const* const ActorValue = ThornGetUserString(Engine, Index, "Actor");
    if (ActorValue != nullptr)
    {
        Actor = _strdup(ActorValue);
        _strlwr(Actor);
    }

    FCharacterObject* const Character = ResolveCharacter(Snap, Actor);
    if (Character == nullptr)
        return nullptr;

    FThornDeformable* const Deformable = new FThornDeformable(Frame, this, Snap);
    Deformable->Character = Character;
    Character->LightGroup = Snap->LtGrp;
    Character->Layer = Snap->SrtGrp;
    Character->RenderMode = 0;
    malloc_strcpy(&Deformable->ActorName, Snap->Name);
    if (Actor == nullptr)
        return Deformable;

    if (_stricmp(Actor, "player") == 0)
    {
        bPlayerActor = 1;
        PlayerCharacter = Character;
        PlayerCsys = Deformable->GetCharacterCsys();
        PlayerFloorHeight = Character->GetFloorHeight();
        PlayerDeformInstance = Deformable->GetDeformInstance();
    }
    free(Actor);
    return Deformable;
}

// FUN_0044e030. operator new(0xd8) Thorn_Sound. A "VoiceProfile" template is a voice line: the
// prefix and one separator are cut off. BindTemplate is FUN_00456ff0, SetSpeaker FUN_004572b0
// (called with a null speaker too), SetSpatial FUN_004572d0.
FThornComponent* FGfScript::SpawnSound(int Index, FThornEntitySnap const* Snap)
{
    if ((Snap->Flags & 1) != 0)
        return nullptr;

    FThornSound* const Sound = new FThornSound(Frame, this, Snap);
    unsigned char const bSpatial = static_cast<unsigned char>((Snap->Flags >> 1) & 1);
    char const* Template = Snap->TemplateName;
    unsigned char bVoice = 0;
    size_t const PrefixLength = strlen("VoiceProfile");
    if (_strnicmp(Template, "VoiceProfile", PrefixLength) == 0)
    {
        bVoice = 1;
        Template += PrefixLength + 1;
    }
    Sound->BindTemplate(Template, bVoice);
    if (bVoice != 0)
    {
        char const* const Speaker = ThornGetUserString(Engine, Index, "speaker");
        if (Speaker == nullptr)
        {
            FDUMP(kFdumpError,
                  "%s(%d) : *** ERROR: Voice profile sound %s in script %s needs a speaker property",
                  GScriptFile, 0x21f, Snap->TemplateName, ScriptFile);
        }
        Sound->SetSpeaker(Speaker);
    }
    Sound->bUseScriptFade = bSoundsUseFade;
    Sound->SetSpatial(bSpatial);
    return Sound;
}

// FUN_0044e180 -- operator new(0x70) Thorn_PlayerEngineSound.
FThornComponent* FGfScript::SpawnPlayerEngineSound(FThornEntitySnap const* Snap)
{
    return new FThornPlayerEngineSound(Frame, this, Snap);
}

// FUN_0044e1f0 -- operator new(0x70) Thorn_SoundReference.
FThornComponent* FGfScript::SpawnSoundReference(FThornEntitySnap const* Snap)
{
    return new FThornSoundReference(Frame, this, Snap);
}

// FUN_0044e260 -- operator new(0xa4) Thorn_Light (no reference check).
FThornComponent* FGfScript::SpawnLight(FThornEntitySnap const* Snap)
{
    return new FThornLight(Frame, this, Snap);
}

// FUN_0044e2d0 -- operator new(0x70) Thorn_LightReference.
FThornComponent* FGfScript::SpawnLightReference(FThornEntitySnap const* Snap)
{
    return new FThornLightReference(Frame, this, Snap);
}

// FUN_0044e340 -- operator new(0xac) Thorn_Psys bound to the [VisEffect] its template names
// (FUN_00453e60).
FThornComponent* FGfScript::SpawnPsys(FThornEntitySnap const* Snap)
{
    if ((Snap->Flags & 1) != 0)
        return nullptr;
    FThornPsys* const Psys = new FThornPsys(Frame, this, Snap);
    Psys->BindVisEffect(Snap->TemplateName);
    return Psys;
}

// FUN_0044e3c0 -- operator new(0x70) Thorn_PlayerEnginePsys.
FThornComponent* FGfScript::SpawnPlayerEnginePsys(FThornEntitySnap const* Snap)
{
    return new FThornPlayerEnginePsys(Frame, this, Snap);
}

// FUN_0044e430. The category is not written up front. A reference takes the room prop of that
// name (FUN_0044a2f0). Category 1 on the "PlayerShip" template takes the Gf ship and its
// "HpMount" offset (FUN_0043d940 / FUN_004478c0). Anything else creates a Gf::ObjectProp
// (operator new(0x80), FUN_00445f40), handed to the sequence unless bPropsToRoom sends it to the
// room (FUN_0044a380).
void FGfScript::ResolveCompoundTarget(int Index, FRoomObject** OutObject, long* OutInstance,
                                      void** OutAnimScript, Csys* OutOffset, int* OutCategory,
                                      unsigned char* bOutUseScene)
{
    *OutObject = nullptr;
    *OutInstance = -1;
    *OutAnimScript = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    OutOffset->set_identity();
    *bOutUseScene = 0;

    FThornEntitySnap const* const Snap = EntitySnap(Engine, Index);
    if ((Snap->Flags & 1) != 0)
    {
        FObjectProp* const Prop = RoomControlFindProp(Room, Snap->Name);
        if (Prop == nullptr)
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: Cannot find referenced prop %s in the set layer",
                  GScriptFile, 0x2b4, Snap->Name);
            return;
        }
        *OutObject = Prop;
        *OutInstance = Prop->GetEngineInstance();
        *OutAnimScript = reinterpret_cast<void*>(static_cast<uintptr_t>(Prop->GetMainScript()));
        *OutCategory = 6;
        *bOutUseScene = 0;
        return;
    }

    *OutCategory =
        PetalInterfaceDatabase::get_category(ThornGetUserString(Engine, Index, "category"));
    if (*OutCategory == 1 && _stricmp(Snap->TemplateName, "PlayerShip") == 0)
    {
        FObjectShip* const Ship = GfObjectShip();
        if (Ship == nullptr)
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", GScriptFile, 0x2d8, "No player ship");
        // Retail goes on with a null ship.
        *OutObject = Ship;
        *OutInstance = Ship->GetEngineInstance();
        *OutAnimScript = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        const InstanceIndex ShipInstance = Ship->GetEngineInstance();
        *OutOffset = GfShipMountOffset(Ship->GetHardpointSummary(), "HpMount", ShipInstance);
        return;
    }

    int const OldCategory = *OutCategory;
    *OutCategory = 6;
    *bOutUseScene = 1;
    FThornEntitySnap const* const PropSnap = EntitySnap(Engine, Index); // read again, as retail
    FPropData Data;
    Data.Template = PropSnap->TemplateName;
    Data.Name = PropSnap->Name;
    Data.Type = OldCategory;
    Data.Pos.x = PropSnap->Pos[0];
    Data.Pos.y = PropSnap->Pos[1];
    Data.Pos.z = PropSnap->Pos[2];
    memcpy(&Data.Orient.E[0][0], PropSnap->Orient, sizeof(Data.Orient.E));
    char const* const Loadout = ThornGetUserString(Engine, Index, "loadout");
    Data.Loadout = Loadout != nullptr ? Loadout : "";

    FObjectProp* const Prop = new FObjectProp(Room, Data);
    *OutObject = Prop;
    *OutInstance = Prop->GetEngineInstance();
    *OutAnimScript = reinterpret_cast<void*>(static_cast<uintptr_t>(Prop->GetMainScript()));
    *bOutUseScene = 1;
    if (bPropsToRoom == 0)
        Sequence->AddProp(Prop, Data.Name.c_str());
    else
        RoomControlAddProp(Room, Prop);
}

// FUN_0044e870. The player is FUN_00449cd0, a room role FUN_00449d50. Otherwise BindMode picks
// the sequence's characters (0), the mirror's first (1) or the mirror's of that actor (2); a
// miss falls to the sequence's, and still none creates one (FUN_00435b80). Every character found
// or made gets FUN_004454e0 with bOwnScript.
FCharacterObject* FGfScript::ResolveCharacter(FThornEntitySnap const* Snap, char const* Actor)
{
    FCharacterObject* Character = nullptr;
    bool bPlayer = false;
    bool bRole = false;
    if (Actor != nullptr)
    {
        if (_stricmp(Actor, "player") == 0)
            bPlayer = true;
        if (_stricmp(Actor, "bartender") == 0 || _stricmp(Actor, "trader") == 0 ||
            _stricmp(Actor, "shipdealer") == 0 || _stricmp(Actor, "equipment") == 0)
        {
            bRole = true;
        }
    }

    if (bPlayer)
    {
        Character = RoomControlPlayerCharacter(Room);
        if (Character == nullptr)
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", GScriptFile, 0x358,
                  "Player hardpoint is missing, so script cannot connect to the player.\n");
            return nullptr;
        }
        Character->TakeOverForScript(bOwnScript);
        return Character;
    }
    if (bRole)
    {
        Character = RoomControlFindCharacterByName(Room, Actor);
        if (Character == nullptr)
            return nullptr;
        Character->TakeOverForScript(bOwnScript);
        return Character;
    }

    if (BindMode == 1)
    {
        Character = Mirror->First();
    }
    else if (BindMode == 2)
    {
        if (Actor != nullptr)
            Character = Mirror->Find(Actor);
    }
    else if (BindMode != 0)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", GScriptFile, 0x382,
              "Invalid character binding method\n");
    }
    if (Character == nullptr)
        Character = Sequence->FindCharacter(Actor);

    if (Character == nullptr)
    {
        CharacterProperties Props;
        GetCostumeDescriptions()->get_costume(Snap->TemplateName, Props.Cost);
        Csys CharacterFrame;
        CharacterFrame.Pos.x = Snap->Pos[0];
        CharacterFrame.Pos.y = Snap->Pos[1];
        CharacterFrame.Pos.z = Snap->Pos[2];
        memcpy(&CharacterFrame.Orient.E[0][0], Snap->Orient, sizeof(CharacterFrame.Orient.E));
        Character = CreateCharacterObject(Props, nullptr, -1, Room, 1, CharacterFrame);
        Sequence->AddCharacter(Character, Actor);
    }
    Character->TakeOverForScript(bOwnScript);
    return Character;
}
