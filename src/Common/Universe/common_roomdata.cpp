// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll RoomData (retail RoomData.cpp) and the Thorn script helpers it reads rooms through
// (retail ThornScript.cpp): ThornGetUser*, ThornScriptLoad / Destroy / GetCsys, the room-record
// templates and the FlashLightSet / FlashLightLine key reader.

#include "common_goods.h"
#include "Archetypes/common_goods_internal.h" // kPathChars, MallocCopy
#include "msvcrt_compat.h"         // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates
#include "Core/msvcrt_atof.h"      // MsvcrtAtof
#include "thorn_iface.h"
#include "common_fuses.h"   // PetalInterfaceDatabase::get_category (prop categories)
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"    // SUBOBJ_ID_NONE, GetFileSysPath
#include "common_newchar.h" // RtcSlider::validate_setting (RoomData priority tokens)
#include "DALib.h"
#include "dacom.h"
#include "x86math.h"        // I3DMathEngine::TransformMatrix (RoomData set_script lights)

#include <cmath>
#include <cstring>
#include <vector>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

using CmnGoods::kPathChars;
using CmnGoods::MallocCopy;

namespace
{
    const char kRoomDataSource[] = "E:\\FL\\Scratch\\Source\\Common\\RoomData.cpp";
    const char kThornScriptSource[] = "E:\\FL\\Scratch\\Source\\Common\\ThornScript.cpp";

    // The %s argument of read_Hotspot_block's zoom_in / zoom_out errors: retail pushes the
    // ADDRESS of INI_Reader::get_name (06263450), not its result, so the message prints that
    // function's code bytes. Retail bug kept: the port passes the code address of its own
    // INI_Reader::get_name the same way.
    char const* IniReaderGetNameCode()
    {
        char const* (INI_Reader::*Method)() const = &INI_Reader::get_name;
        static_assert(sizeof(Method) == sizeof(char const*),
                      "INI_Reader member pointer is a plain code address");
        char const* Code = nullptr;
        std::memcpy(&Code, &Method, sizeof(Code));
        return Code;
    }

    // Shared body of RoomData::read_FlashLightSet_block (0x06276c00) and
    // read_FlashLightLine_block (0x06276df0), which differ only in the Mode they stamp and in
    // whether they accept the "numlights" key.
    void ReadFlashLightKeys(INI_Reader& Reader, FlashLightSetInfo& Set, bool bAcceptNumLights)
    {
        // 1.0f/255.0f, the .rdata float at 0639ac78.
        const float kByteToUnit = 0.003921568859368563f;

        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("color"))
            {
                Set.ColorR = Reader.get_value_float(0);
                Set.ColorG = Reader.get_value_float(1);
                Set.ColorB = Reader.get_value_float(2);
            }
            else if (Reader.is_value("icolor"))
            {
                // Values 0 and 1 are rounded to float before the multiply; value 2 is still
                // the unrounded x87 result when its multiply runs (value_num).
                Set.ColorR = Reader.get_value_float(0) * kByteToUnit;
                Set.ColorG = Reader.get_value_float(1) * kByteToUnit;
                Set.ColorB = static_cast<float>(Reader.value_num(2) * static_cast<double>(kByteToUnit));
            }
            else if (Reader.is_value("scale"))
            {
                Set.Scale = Reader.get_value_float(0);   // +0x0c
            }
            else if (Reader.is_value("gap"))
            {
                Set.Gap = Reader.get_value_float(0);   // +0x10
            }
            else if (Reader.is_value("blink"))
            {
                Set.Blink = Reader.get_value_float(0);   // +0x14
            }
            else if (Reader.is_value("endpause"))
            {
                Set.EndPause = Reader.get_value_float(0);   // +0x18
            }
            else if (Reader.is_value("hardpoint"))
            {
                // Retail quirk: "hardpoint" and "setpoint" share ONE counter at +0xe20, so they
                // advance the same slot index. The 64-slot arrays are not bound-checked: a 65th
                // name runs on into the next bank, reproduced as is.
                malloc_strcpy(const_cast<char**>(&Set.HardpointNames[Set.NumLights]),
                              Reader.get_value_string());
                Set.NumLights += 1;
            }
            else if (Reader.is_value("setpoint"))
            {
                // SetpointNames at +0x120.
                malloc_strcpy(const_cast<char**>(&Set.SetpointNames[Set.NumLights]),
                              Reader.get_value_string());
                Set.NumLights += 1;
            }
            else if (bAcceptNumLights && Reader.is_value("numlights"))
            {
                // Line blocks only. +0xe24, truncated from the unrounded x87 value.
                Set.NumLineLights = static_cast<long>(Reader.value_num(0));
            }
            // Retail's final else calls get_file_name and discards the result (a gutted
            // diagnostic).
        } while (Reader.read_value());
    }
}

char const* __cdecl ThornGetUserString(IScriptEngine* Engine, int Index, char const* Key)
{
    // 0627bc60: slot 7 (+0x1c, entity name by index), slot 33 (+0x84, user property). No null
    // test on anything.
    void* EntityName = Engine->GetEntityHandle(Index);
    char* Out = nullptr;
    GENRESULT Cr = Engine->GetUserProp(EntityName, Key, &Out);
    if (Cr != 0)
        return nullptr;
    return Out;
}

bool __cdecl ThornGetUserInt(IScriptEngine* Engine, int Index, char const* Key, int* Out)
{
    // 0627bd00 (ThornGetUserString inlined). No null test on Out.
    char const* Text = ThornGetUserString(Engine, Index, Key);
    if (Text == nullptr)
        return false;
    *Out = static_cast<int>(MsvcrtAtoi(Text));
    return true;
}

bool __cdecl ThornGetUserFloat(IScriptEngine* Engine, int Index, char const* Key, float* Out)
{
    // 0627bca0
    char const* Text = ThornGetUserString(Engine, Index, Key);
    if (Text == nullptr)
        return false;
    *Out = static_cast<float>(MsvcrtAtof(Text)); // MSVCRT.DLL atof, IAT 063992f0
    return true;
}

unsigned long __cdecl ThornGetRenderType(ThornEntity const* Entity, char const* Unused)
{
    // 0627bd60. Retail ignores the second argument; no null test of Entity (UsrFlg +0x42).
    (void)Unused;
    unsigned char const Flags = Entity->UsrFlg;
    unsigned long Result = (Flags & 1u) != 0 ? 1ul : 0ul;
    if (((Flags >> 1) & 1u) != 0)
        Result = 2ul;
    return Result;
}

IScriptEngine* __cdecl ThornScriptLoad(char const* Path)
{
    // RVA 0x1b8f0.
    if (Path == nullptr || Path[0] == '\0')
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kThornScriptSource, 0x18,
              "Skipping null script filename...\n");
        return nullptr;
    }

    // None of the CreateInstance / QueryInterface / Release results is tested.
    ICOManager* const Manager = DALib::Dacom;

    static_assert(sizeof(FAGGDESC14) == 0x14, "retail descriptor is 0x14 bytes");
    FAGGDESC14 Desc;
    Desc.Size = 0x14;
    Desc.NameAt4 = "IScriptEngine";
    Desc.OuterUnknown = nullptr;
    Desc.InnerUnknown = nullptr;
    Desc.Reserved10 = 0;

    void* Instance = nullptr;
    Manager->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Instance);

    void* Queried = nullptr;
    IDAComponent* const Component = static_cast<IDAComponent*>(Instance);
    Component->QueryInterface(Iids::ScriptEngine, &Queried);
    Component->Release();
    IScriptEngine* Engine = static_cast<IScriptEngine*>(Queried);

    IFileSystem* Filesys = nullptr;
    DAEngine_create_filesystem(Path, &Filesys, "DOS");
    if (Filesys != nullptr)
    {
        // Slot 4 (+0x10) hands the script file to the engine.
        Engine->Load(Filesys);
        DAEngine_release_filesystem(&Filesys);
        return Engine;
    }

    FDUMP(kFdumpError, "%s(%d) : *** ERROR: Cannot load script file %s\n", kThornScriptSource, 0x30,
          Path);
    Engine->Release();
    return nullptr;
}

void __cdecl ThornScriptDestroy(IScriptEngine** Engine)
{
    // RVA 0x1ba10, ICF-folded with DAEngine_release_filesystem. The outer pointer is not
    // tested.
    if (*Engine != nullptr)
    {
        (*Engine)->Release();
        *Engine = nullptr;
    }
}

Csys __cdecl ThornScriptGetCsys(IScriptEngine* Engine, char const* ActorName)
{
    // 0627ba30. The first type-2 entity that matches gives the frame. With a null ActorName an
    // entity without an "actor" property matches and a "player" actor is skipped.
    Csys Out;
    Out.set_identity();
    if (Engine == nullptr)
        return Out;

    int const Count = static_cast<int>(Engine->GetEntityCount());
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        ThornEntity const* const Entity = static_cast<ThornEntity const*>(Engine->GetEntityDesc(Index));
        if (Entity == nullptr || Entity->Type != kThornEntityDeformable)
            continue;

        float const FloorHeight = Entity->FloorHeight; // +0x44
        void* const Name = Engine->GetEntityHandle(Index);
        char* Value = nullptr;
        char const* Actor = nullptr;
        if (Engine->GetUserProp(Name, "actor", &Value) == 0)
            Actor = Value;

        bool bMatch = false;
        if (ActorName == nullptr)
            bMatch = Actor == nullptr || CallDacomStricmp(Actor, "player") != 0;
        else
            bMatch = Actor != nullptr && CallDacomStricmp(Actor, ActorName) == 0;
        if (!bMatch)
            continue;

        // y comes from the floor height, not the entity position.
        Vector const Position = {Entity->Pos.x, FloorHeight, Entity->Pos.z};
        CsysFromPosOrient(Out, Position, Entity->Orient);
        return Out;
    }

    Out.set_identity();
    return Out;
}

namespace
{
    // The record allocators FUN_062783e0 (+0x134), FUN_06278640 (+0x144), FUN_062788b0
    // (+0x154), FUN_06278b30 (+0x164), FUN_06278e20 (+0x174), FUN_06279120 (+0x184),
    // FUN_06279400 (+0x194), FUN_06279670 (+0x1a4) and FUN_06279920 (+0x1b4) are one shape.
    template <class TRecord>
    TRecord* AppendRoomRecord(std::vector<TRecord*>& Records)
    {
        TRecord* const Record = new TRecord();
        Records.push_back(Record);
        return Record;
    }

    // The per-vector purges ~RoomData runs after destroy() (FUN_06278300 +0x134, 062785e0
    // +0x144, 06278830 +0x154, 06278ac0 +0x164, 06278d30 +0x174, 06279020 +0x184, 06279310
    // +0x194, 062795d0 +0x1a4, 062798b0 +0x1b4).
    template <class TRecord>
    void PurgeRoomRecords(std::vector<TRecord*>& Records)
    {
        size_t Index = 0;
        for (; Index < Records.size(); ++Index)
        {
            delete Records[Index];
            Records[Index] = nullptr;
        }
        Records.clear();
    }

    // Identity frame at the origin, as FUN_0628b030(0,0,0) + FUN_062748a0 build at +0x1cc /
    // +0x1d8.
    void ResetRoomCamera(RoomCameraInfo& Camera)
    {
        Camera.Frame.Pos.x = 0.0f;
        Camera.Frame.Pos.y = 0.0f;
        Camera.Frame.Pos.z = 0.0f;
        MatrixSetIdentity(Camera.Frame.Orient);
        Camera.Fov = 35.0f;
        Camera.NearPlane = 0.25f;
        Camera.FarPlane = 1000000.0f;
    }

    float FloatFromBits(unsigned Bits)
    {
        float Value = 0.0f;
        memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }
}

// 06274150. +0x08 is not written (see the header).
RoomData::RoomData()
{
    Music.clear();
    Ambient.clear();
    clear();
}

// 06274300. The vectors are already empty after destroy(), so the purges find nothing, as in
// retail. Retail destroys the camera (+0x1c8) before the purges; the port leaves it to its
// member dtor (disjoint members).
RoomData::~RoomData()
{
    destroy();
    PurgeRoomRecords(AmbientScripts);
    PurgeRoomRecords(FlashLightSets);
    PurgeRoomRecords(Hotspots);
    PurgeRoomRecords(ShipPlacements);
    PurgeRoomRecords(CharacterPlacements);
    PurgeRoomRecords(Setpoints);
    PurgeRoomRecords(Props);
    PurgeRoomRecords(Lights);
    PurgeRoomRecords(Cameras);
}

namespace
{
    // 1/255 as the double at .rdata 0639a1b8.
    double const kInv255Double = 0.00392156862745098;

    // One channel of the scene entity's ambient colour: the dword read as a signed int.
    float AmbientChannel(unsigned int Raw)
    {
        int const Value = static_cast<int>(Raw);
        return static_cast<float>(Value * kInv255Double);
    }

    // Shadow / glare blob direction turned by the entity's 3x3 through the 3DMathEngine (lazy
    // DAT_063fbbc0) and normalised. No zero-length test, as in retail. The x87 sum is kept in
    // double here.
    void TurnLightDirection(Csys const& Frame, Vector const& BlobDirection, Vector& Out)
    {
        Vector Turned;
        I3DMathEngine* const Math = Acquire3DMathEngine();
        Math->TransformMatrix(Turned, Frame.Orient, BlobDirection);
        double const LengthSq = static_cast<double>(Turned.x) * Turned.x +
                                static_cast<double>(Turned.y) * Turned.y +
                                static_cast<double>(Turned.z) * Turned.z;
        double const InvLength = 1.0 / sqrt(LengthSq);
        Out.x = static_cast<float>(Turned.x * InvLength);
        Out.y = static_cast<float>(Turned.y * InvLength);
        Out.z = static_cast<float>(Turned.z * InvLength);
    }
}

// 06274b40. Files every entity of the set script by type; add_set_script_Prop / _Camera /
// _Setpoint / _light are inlined in retail. Then one CharPlaceInfo per setpoint whose name
// follows the zs/zg convention.
void RoomData::read_set_script(char const* RelPath)
{
    // No script -> nothing at all (not even the placement pass).
    IScriptEngine* Engine = ThornScriptLoad(RelPath);
    if (Engine == nullptr)
        return;

    int const EntityCount = static_cast<int>(Engine->GetEntityCount());
    int Index = 0;
    for (Index = 0; Index < EntityCount; ++Index)
    {
        ThornEntity const* const Entity = static_cast<ThornEntity const*>(Engine->GetEntityDesc(Index));
        Csys Frame;
        CsysFromPosOrient(Frame, Entity->Pos, Entity->Orient);
        char const* const Template = Entity->TemplateName;
        char const* const Name = Entity->Name;
        unsigned char const LightGroup = Entity->LtGrp;
        signed char const Layer = static_cast<signed char>(Entity->SrtGrp);
        int const Type = Entity->Type;
        unsigned const Flags = static_cast<unsigned>(Entity->Flags);
        unsigned long const RenderType = ThornGetRenderType(Entity, RelPath);
        // Read for every entity before the type test.
        bool const bLit = ((Flags >> 2) & 1u) != 0;
        bool const bVisible = ((Flags >> 1) & 1u) != 0;
        char const* const CategoryName = ThornGetUserString(Engine, Index, "category");

        if (Type == kThornEntityCompound)
        {
            bool bFog = true;
            if (ThornGetUserString(Engine, Index, "nofog") != nullptr)
                bFog = false;
            bool bRoomMain = false;
            if (ThornGetUserString(Engine, Index, "hardpoint") != nullptr)
                bRoomMain = true;
            if (CategoryName == nullptr)
            {
                FDUMP(kFdumpError,
                      "%s(%d) : *** ERROR: Entity %s in hardpoint script %s does not have a "
                      "category.\nIt might be incorrectly marked as a reference.",
                      kRoomDataSource, 0x293, Name, RelPath);
                continue;
            }
            // Category 0 is the room's main prop as well.
            int const Category = PetalInterfaceDatabase::get_category(CategoryName);
            if (Category == 0)
                bRoomMain = true;
            // "" is .rdata 063997b0.
            char const* Loadout = ThornGetUserString(Engine, Index, "loadout");
            if (Loadout == nullptr)
                Loadout = "";
            // RtcSlider::validate_setting is 0x0627a710 (labelled AttachedEquipment::is_attached
            // by ICF).
            unsigned long PriorityId = 0;
            char const* const PriorityText = ThornGetUserString(Engine, Index, "priority");
            if (PriorityText != nullptr)
            {
                RtcSlider::validate_setting(PriorityText, RelPath);
                PriorityId = CreateID(PriorityText);
            }
            bool bRunningLights = false;
            if (ThornGetUserString(Engine, Index, "running_lights") != nullptr)
                bRunningLights = true;
            add_set_script_Prop(Template, Name, Category, Frame, bVisible, bLit, bFog, bRoomMain,
                                LightGroup, Layer, RenderType, Loadout, PriorityId,
                                bRunningLights);
        }
        else if (Type == kThornEntityCamera)
        {
            // The camera (props at +0x44: fov, near, far) and a setpoint of the same name and
            // frame. The tail sits in FThornEntitySnap's anonymous union, so its address is
            // taken from its first member.
            add_set_script_Camera(0, Frame, Name,
                                  reinterpret_cast<ThornCameraProperties const*>(&Entity->Fovh));
            add_set_script_Setpoint(0, Frame, Name, Entity);
        }
        else if (Type == kThornEntityLight)
        {
            // The 0x90-byte light blob at +0x44. +0x0c direction, +0x20 the light's on flag,
            // +0x28 a D3DLIGHT8 (its type at +0x28, the diffuse colour at +0x2c).
            FThornLightProps const& Blob = Entity->Light;
            Vector BlobDirection;
            BlobDirection.x = Blob.Direction.x;
            BlobDirection.y = Blob.Direction.y;
            BlobDirection.z = Blob.Direction.z;
            int const LightType = Blob.Type;
            Vector BlobColor;
            BlobColor.x = Blob.Diffuse[0];
            BlobColor.y = Blob.Diffuse[1];
            BlobColor.z = Blob.Diffuse[2];
            if (CallDacomStricmp(Name, "LtShadowSource") == 0)
            {
                bShadowLight = true;
                if (LightType == 3)
                {
                    TurnLightDirection(Frame, BlobDirection, ShadowLightDirection);
                    bShadowLightWorldSpaceDir = true;
                }
                else
                {
                    ShadowLightPosition = Frame.Pos;
                    bShadowLightWorldSpaceDir = false;
                }
                ShadowLightColor = BlobColor;
            }
            else if (CallDacomStricmp(Name, "LtGlareSource") == 0)
            {
                bGlareLight = true;
                if (LightType == 3)
                {
                    TurnLightDirection(Frame, BlobDirection, GlareLightDirection);
                    bGlareLightWorldSpaceDir = true;
                }
                else
                {
                    GlareLightPosition = Frame.Pos;
                    bGlareLightWorldSpaceDir = false;
                }
                GlareLightColor = BlobColor;
            }
            else if (Blob.On != 0)
            {
                add_set_script_light(0, Frame, static_cast<ThornLightProperties const*>(&Blob),
                                     LightGroup);
            }
        }
        else if (Type == kThornEntityMarker)
        {
            add_set_script_Setpoint(0, Frame, Name, Entity);
        }
        else if (Type == kThornEntityScene)
        {
            // Three ints at the head of the scene blob (+0x44) into +0xd8..+0xe0.
            AmbientColor.x = AmbientChannel(Entity->SceneBlob[0]);
            AmbientColor.y = AmbientChannel(Entity->SceneBlob[1]);
            AmbientColor.z = AmbientChannel(Entity->SceneBlob[2]);
        }
    }

    // Retail calls DAEngine_release_filesystem here, the same folded body.
    ThornScriptDestroy(&Engine);

    // Every setpoint whose name follows the zs/zg convention gets a placement (FUN_06278e20).
    size_t SetpointIndex = 0;
    for (SetpointIndex = 0; SetpointIndex < Setpoints.size(); ++SetpointIndex)
    {
        SetpointInfo const* const Setpoint = Setpoints[SetpointIndex];
        if (!Setpoint->Props.bValid)
            continue;
        CharPlaceInfo* const Place = AppendRoomRecord(CharacterPlacements);
        malloc_strcpy(const_cast<char**>(&Place->Name), Setpoint->Name);
        Place->Frame = Setpoint->Frame;
        Place->CharType = Setpoint->Props.CharType;
        Place->Posture = Setpoint->Props.Posture;
        Place->bZoneScript = Setpoint->Props.bZoneScript;
    }
}

// 062770a0. An unopenable file is silently nothing; the headers are tested in retail order and
// an unknown header is passed over.
void RoomData::read_from_ini(char const* RelPath)
{
    char FullPath[kPathChars];
    GetFileSysPath(FullPath, RelPath, DALib::Data);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
        return;

    while (Reader.read_header())
    {
        if (Reader.is_header("Room") || Reader.is_header("Room_Info"))
        {
            read_RoomInfo_block(&Reader);
        }
        else if (Reader.is_header("Cloud"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: [Cloud] is obsolete in %s", kRoomDataSource,
                  0x7fe, FullPath);
        }
        else if (Reader.is_header("Room_Sound") || Reader.is_header("Sound"))
        {
            read_Sound_block(&Reader);
        }
        else if (Reader.is_header("Fog"))
        {
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: [Fog] is obsolete in %s, fog settings should be in scripts",
                  kRoomDataSource, 0x807, FullPath);
        }
        else if (Reader.is_header("Camera"))
        {
            read_Camera_block(&Reader);
        }
        else if (Reader.is_header("CharacterPlacement"))
        {
            read_CharacterPlacement_block(&Reader);
        }
        else if (Reader.is_header("GoodsCartPlacement"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: [GoodsCartPlacement] is obsolete in %s",
                  kRoomDataSource, 0x813, FullPath);
        }
        else if (Reader.is_header("RepairRobotPlacement"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: [RepairRobotPlacement] is obsolete in %s",
                  kRoomDataSource, 0x817, FullPath);
        }
        else if (Reader.is_header("GoodsPilePlacement"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: [GoodsPilePlacement] is obsolete in %s",
                  kRoomDataSource, 0x81b, FullPath);
        }
        else if (Reader.is_header("ShipPlacement"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x81f,
                  "[ShipPlacement] is obsolete");
        }
        else if (Reader.is_header("PlayerShipPlacement"))
        {
            read_PlayerShipPlacement_block(&Reader);
        }
        else if (Reader.is_header("ForSaleShipPlacement"))
        {
            // Inlined in retail.
            read_ForSaleShipPlacement_block(&Reader);
        }
        else if (Reader.is_header("MonitorPlacement"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x82b,
                  "[MonitorPlacement] is obsolete");
        }
        else if (Reader.is_header("Effect"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x82f,
                  "[Effect] is obsolete");
        }
        else if (Reader.is_header("AvailableShip"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x833,
                  "[AvailableShip] is obsolete");
        }
        else if (Reader.is_header("Hotspot"))
        {
            read_Hotspot_block(&Reader);
        }
        else if (Reader.is_header("FlashlightSet"))
        {
            read_FlashLightSet_block(&Reader);
        }
        else if (Reader.is_header("FlashlightLine"))
        {
            read_FlashLightLine_block(&Reader);
        }
        else if (Reader.is_header("Billboard"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x843,
                  "[Billboard] is obsolete");
        }
        else if (Reader.is_header("Light"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x847,
                  "[Light] is obsolete");
        }
        else if (Reader.is_header("Lighting"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x84b,
                  "[Lighting] is obsolete");
        }
    }
    Reader.close();
}

// 06277c50
void RoomData::load()
{
    if (!bLoaded)
        read_from_ini(File);
    bLoaded = true;
}

// 06275bb0. The camera's name pointer is zeroed WITHOUT a free (a name left by an earlier
// setup_Camera with no unsetup_Camera in between leaks, as in retail). The three debug names
// install a fixed pose (retail immediates); any other name takes the FIRST camera of +0x134
// with that name.
void RoomData::setup_Camera()
{
    Camera.Name = nullptr;
    ResetRoomCamera(Camera);

    if (CameraName == nullptr)
    {
        // The defaults are written once more.
        ResetRoomCamera(Camera);
        return;
    }

    if (CallDacomStricmp(CameraName, "debug_detailtest") == 0)
    {
        Camera.Frame.Pos.x = FloatFromBits(0xbf804189u);
        Camera.Frame.Pos.y = FloatFromBits(0x3fb58106u);
        Camera.Frame.Pos.z = FloatFromBits(0x4033a5e3u);
        Camera.Frame.Orient.E[0][0] = FloatFromBits(0x3f7c28f6u);
        Camera.Frame.Orient.E[0][1] = FloatFromBits(0xbcfc5048u);
        Camera.Frame.Orient.E[0][2] = FloatFromBits(0x3e2dab9fu);
        Camera.Frame.Orient.E[1][0] = FloatFromBits(0xbaaa64c3u);
        Camera.Frame.Orient.E[1][1] = FloatFromBits(0x3f7b8bacu);
        Camera.Frame.Orient.E[1][2] = FloatFromBits(0x3e3dbf48u);
        Camera.Frame.Orient.E[2][0] = FloatFromBits(0xbe3089a0u);
        Camera.Frame.Orient.E[2][1] = FloatFromBits(0xbe3b2fecu);
        Camera.Frame.Orient.E[2][2] = FloatFromBits(0x3f77c84bu);
        Camera.Fov = 35.0f;
        Camera.NearPlane = 0.25f;
        Camera.FarPlane = 1000000.0f;
        return;
    }

    if (CallDacomStricmp(CameraName, "debug_straight_up") == 0)
    {
        Camera.Frame.Pos.x = 0.0f;
        Camera.Frame.Pos.y = 0.0f;
        Camera.Frame.Pos.z = 0.0f;
        Camera.Frame.Orient.E[0][0] = 1.0f;
        Camera.Frame.Orient.E[0][1] = 0.0f;
        Camera.Frame.Orient.E[0][2] = 0.0f;
        Camera.Frame.Orient.E[1][0] = 0.0f;
        Camera.Frame.Orient.E[1][1] = 0.0f;
        Camera.Frame.Orient.E[1][2] = -1.0f;
        Camera.Frame.Orient.E[2][0] = 0.0f;
        Camera.Frame.Orient.E[2][1] = 1.0f;
        Camera.Frame.Orient.E[2][2] = 0.0f;
        Camera.Fov = 35.0f;
        Camera.NearPlane = 0.25f;
        Camera.FarPlane = 1000000.0f;
        return;
    }

    if (CallDacomStricmp(CameraName, "debug_anim") == 0)
    {
        // FUN_06275ee0 builds the 3x3 from three COLUMN vectors.
        Camera.Frame.Pos.x = FloatFromBits(0xc0343958u);
        Camera.Frame.Pos.y = FloatFromBits(0x3f9851ecu);
        Camera.Frame.Pos.z = FloatFromBits(0x409ff7cfu);
        Camera.Frame.Orient.E[0][0] = FloatFromBits(0x3d252157u);
        Camera.Frame.Orient.E[1][0] = FloatFromBits(0xbdd2dcb1u);
        Camera.Frame.Orient.E[2][0] = FloatFromBits(0xbf7e6e44u);
        Camera.Frame.Orient.E[0][1] = FloatFromBits(0xbe333db0u);
        Camera.Frame.Orient.E[1][1] = FloatFromBits(0x3f7a8390u);
        Camera.Frame.Orient.E[2][1] = FloatFromBits(0xbdde2ac3u);
        Camera.Frame.Orient.E[0][2] = FloatFromBits(0x3f7bd662u);
        Camera.Frame.Orient.E[1][2] = FloatFromBits(0x3e369ec3u);
        Camera.Frame.Orient.E[2][2] = FloatFromBits(0x3caf8bdfu);
        Camera.Fov = 35.0f;
        Camera.NearPlane = 0.25f;
        Camera.FarPlane = 1000000.0f;
        return;
    }

    // RoomCameraInfo::operator= (06273370) inlined.
    size_t Index = 0;
    for (Index = 0; Index < Cameras.size(); ++Index)
    {
        RoomCameraInfo const* const Candidate = Cameras[Index];
        if (CallDacomStricmp(CameraName, Candidate->Name) == 0)
        {
            Camera = *Candidate;
            return;
        }
    }
}

// Ordinal 3777 at .text:062a8920, ICF-folded with other empty void() members. Retail's unload
// does nothing: it neither clears bLoaded nor frees the room contents, so a later load() does
// not re-read the INI. The contents are freed only by destroy(), which ~RoomData runs when
// BaseData tears its Rooms list down. Callers: freelancer.exe FUN_00439b50 (per room on leaving
// a base) and Server.dll's BaseDB leave-base walk.
void RoomData::unload()
{
}

// ---------------------------------------------------------------------------
// Extended RoomData methods
// ---------------------------------------------------------------------------

// 06277c10. The previous strings are not freed. read_Room_block passes the base id first and
// the room id second.
void RoomData::init(unsigned NewBaseId, unsigned NewRoomId,
                    char const* NewNickname, char const* NewFile)
{
    BaseId = NewBaseId;
    RoomId = NewRoomId;
    malloc_strcpy(&Nickname, NewNickname);
    malloc_strcpy(&File, NewFile);
    bLoaded = false;
}

// 06274540. Resets every field except +0x08 and frees NOTHING: the strings are dropped and the
// record vectors emptied with their records still allocated -- destroy() is the freeing path.
void RoomData::clear()
{
    BaseId = 0;
    RoomId = 0;
    Nickname = nullptr;
    File = nullptr;
    int Slot = 0;
    for (Slot = 0; Slot < 0x20; ++Slot)
    {
        Animations[Slot] = nullptr;
        AnimationLoop[Slot] = false;
    }
    AnimationCount = 0;
    GoodscartScript = nullptr;
    GoodscartPriorityId = 0;
    bCutoffShadowZ = false;
    CutoffShadowZ = 0.0f;
    ExtraShadowHeightOffset = 0.0f;
    Music.clear();
    bMusicLoop = true;
    Ambient.clear();
    AmbientColor.x = 1.0f;
    AmbientColor.y = 1.0f;
    AmbientColor.z = 1.0f;
    ShadowLightPosition.x = 0.0f;
    ShadowLightPosition.y = 0.0f;
    ShadowLightPosition.z = 0.0f;
    ShadowLightDirection.x = 0.0f;
    ShadowLightDirection.y = 1.0f;
    ShadowLightDirection.z = 0.0f;
    ShadowLightColor.x = 0.0f;
    ShadowLightColor.y = 0.0f;
    ShadowLightColor.z = 0.0f;
    GlareLightPosition.x = 0.0f;
    GlareLightPosition.y = 0.0f;
    GlareLightPosition.z = 0.0f;
    GlareLightDirection.x = 0.0f;
    GlareLightDirection.y = 1.0f;
    GlareLightDirection.z = 0.0f;
    bAmbientLoop = true;
    bShadowLight = false;
    bShadowLightWorldSpaceDir = true;
    bGlareLight = false;
    bGlareLightWorldSpaceDir = true;
    GlareLightColor.x = 0.0f;
    GlareLightColor.y = 0.0f;
    GlareLightColor.z = 0.0f;
    CameraName = nullptr;
    Camera.Name = nullptr;
    ResetRoomCamera(Camera);
    // The records are kept (leaked unless destroy() purged them first).
    Cameras.clear();
    Lights.clear();
    Props.clear();
    Setpoints.clear();
    CharacterPlacements.clear();
    ShipPlacements.clear();
    Hotspots.clear();
    FlashLightSets.clear();
    AmbientScripts.clear();
}

// 062748d0
void RoomData::destroy()
{
    malloc_strfree(&Nickname);
    malloc_strfree(&File);
    int Slot = 0;
    for (Slot = 0; Slot < AnimationCount; ++Slot)
        malloc_strfree(&Animations[Slot]);
    malloc_strfree(&GoodscartScript);
    Music.clear();
    Ambient.clear();
    malloc_strfree(&CameraName);
    Camera.destroy();
    PurgeRoomRecords(Cameras);
    PurgeRoomRecords(Lights);
    PurgeRoomRecords(Props);
    PurgeRoomRecords(Setpoints);
    PurgeRoomRecords(CharacterPlacements);
    PurgeRoomRecords(ShipPlacements);
    PurgeRoomRecords(Hotspots);
    PurgeRoomRecords(FlashLightSets);
    PurgeRoomRecords(AmbientScripts);
    clear();
}

void RoomData::warn(INI_Reader* Reader, bool bError)
{
    // 06277090: the diagnostic was compiled out; only get_file_name is called.
    (void)bError;
    Reader->get_file_name();
}

// 06277c70
void RoomData::fixup(long Instance)
{
    setup_Camera();
    HardpointSummary HpSum;
    if (Instance != -1)
        HpSum.analyze_instance(Instance);
    fixup_FlashLight_list(Instance, &HpSum);
    HpSum.destroy();
}

// 06277d20
void RoomData::unfixup()
{
    unsetup_Camera();
    unfixup_FlashLight_list();
}

// 06277000. No null test.
void RoomData::fixup_FlashLight_list(long Instance, HardpointSummary const* HpSum)
{
    size_t SetIndex = 0;
    for (SetIndex = 0; SetIndex < FlashLightSets.size(); ++SetIndex)
        FlashLightSets[SetIndex]->fixup(Instance, HpSum, this);
}

// 06277050. No null test.
void RoomData::unfixup_FlashLight_list()
{
    size_t SetIndex = 0;
    for (SetIndex = 0; SetIndex < FlashLightSets.size(); ++SetIndex)
        FlashLightSets[SetIndex]->unfixup();
}

// 06277960. Re-opens the room ini; the index counts only [CharacterPlacement] blocks.
void RoomData::enumerate_CharacterPlacement_ini(
    void(__cdecl* Callback)(int, INI_Reader*, void*), void* Context) const
{
    char FullPath[kPathChars];
    GetFileSysPath(FullPath, File, DALib::Data);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
        return;

    int BlockIndex = 0;
    while (Reader.read_header())
    {
        if (Reader.is_header("CharacterPlacement"))
        {
            Callback(BlockIndex, &Reader, Context);
            ++BlockIndex;
        }
    }
    Reader.close();
}

RoomCameraInfo const* RoomData::get_camera_info() const
{
    return &Camera;
}

// 06275490 -- [Room] / [Room_Info]. An unknown key calls get_file_name and drops the result.
void RoomData::read_RoomInfo_block(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("set_script"))
        {
            // Nothing is kept of the path.
            read_set_script(Reader->get_value_string());
        }
        else if (Reader->is_value("mesh"))
        {
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: mesh is obsolete in %s, use the setscript to specify the "
                  "room mesh\n",
                  kRoomDataSource, 0x3d7, Reader->get_file_name());
        }
        else if (Reader->is_value("animation"))
        {
            // No bound check: a 33rd name runs into the loop-flag array, as in retail.
            malloc_strcpy(&Animations[AnimationCount], Reader->get_value_string());
            AnimationLoop[AnimationCount] = true;
            ++AnimationCount;
        }
        else if (Reader->is_value("animation_oneshot"))
        {
            malloc_strcpy(&Animations[AnimationCount], Reader->get_value_string());
            AnimationLoop[AnimationCount] = false;
            ++AnimationCount;
        }
        else if (Reader->is_value("background_script"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: background_script is obsolete in %s, use scene\n",
                  kRoomDataSource, 0x3f5, Reader->get_file_name());
        }
        else if (Reader->is_value("background_script_oneshot"))
        {
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: background_script_oneshot is obsolete in %s, use scene\n",
                  kRoomDataSource, 0x3f9, Reader->get_file_name());
        }
        else if (Reader->is_value("mission_vendor_type"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: mission_vendor_type is obsolete in %s\n",
                  kRoomDataSource, 0x3fd, Reader->get_file_name());
        }
        else if (Reader->is_value("goodscart_script"))
        {
            malloc_strcpy(&GoodscartScript, Reader->get_value_string());
            GoodscartPriorityId = 0;
            if (!Reader->is_value_empty(1))
            {
                char const* const PriorityName = Reader->get_value_string(1);
                RtcSlider::validate_setting(PriorityName, Reader->get_file_name());
                GoodscartPriorityId = CreateID(PriorityName);
            }
        }
        else if (Reader->is_value("goodscart_script_fighter"))
        {
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: goodscart_script_fighter is obsolete in %s, use "
                  "goodscart_script\n",
                  kRoomDataSource, 0x410, Reader->get_file_name());
        }
        else if (Reader->is_value("goodscart_script_elite"))
        {
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: goodscart_script_elite is obsolete in %s, use "
                  "goodscart_script\n",
                  kRoomDataSource, 0x414, Reader->get_file_name());
        }
        else if (Reader->is_value("goodscart_script_freighter"))
        {
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: goodscart_script_freighter is obsolete in %s, use "
                  "goodscart_script\n",
                  kRoomDataSource, 0x418, Reader->get_file_name());
        }
        else if (Reader->is_value("cutoff_shadow_z"))
        {
            bCutoffShadowZ = true;
            CutoffShadowZ = Reader->get_value_float(0);
        }
        else if (Reader->is_value("extra_shadow_height_offset"))
        {
            ExtraShadowHeightOffset = Reader->get_value_float(0);
        }
        else if (Reader->is_value("news_material"))
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: news_material is obsolete in %s\n",
                  kRoomDataSource, 0x427, Reader->get_file_name());
        }
        else if (Reader->is_value("is_outside"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: is_outside is obsolete in %s\n",
                  kRoomDataSource, 0x42b, Reader->get_file_name());
        }
        else if (Reader->is_value("scene"))
        {
            read_ambient_script(Reader);
        }
        else
        {
            Reader->get_file_name();
        }
    }
}

// 062752b0 -- one "scene = [all,] ambient|traffic|once, script.thn [, priority]" line.
void RoomData::read_ambient_script(INI_Reader* Reader)
{
    // Unbounded strcpy: a value over 0x100 bytes overruns the next slot, as in retail.
    char Values[4][0x100];
    int ValueCount = 0;
    while (ValueCount < 4 && !Reader->is_value_empty(ValueCount))
    {
        strcpy(Values[ValueCount], Reader->get_value_string(ValueCount));
        ++ValueCount;
    }

    int First = 0;
    if (ValueCount >= 1 && CallDacomStricmp(Values[0], "all") == 0)
        First = 1;
    ValueCount -= First;
    if (ValueCount != 2 && ValueCount != 3)
    {
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: Invalid scene = specification in [Room] block in file %s\n"
              "Expecting: scene = ambient|traffic|once, script.thn, [priority]\n",
              kRoomDataSource, 0x38a, Reader->get_file_name());
        return;
    }

    char const* const TypeName = Values[First];
    char const* const ScriptPath = Values[First + 1];
    char const* PriorityName = nullptr;
    if (ValueCount == 3)
        PriorityName = Values[First + 2];

    int Type = 0;
    if (CallDacomStricmp(TypeName, "ambient") == 0)
    {
        Type = 1;
    }
    else if (CallDacomStricmp(TypeName, "traffic") == 0)
    {
        Type = 2;
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: traffic ambient is obsolete in room .ini %s\n",
              kRoomDataSource, 0x3a3, Reader->get_file_name());
    }
    else if (CallDacomStricmp(TypeName, "once") == 0)
    {
        Type = 3;
    }
    else
    {
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: Invalid scene = specification in [Room] block in file %s\n"
              "Expecting: scene = ambient|traffic|once, script.thn\n",
              kRoomDataSource, 0x3ad, Reader->get_file_name());
        return;
    }

    unsigned Priority = 0;
    if (PriorityName != nullptr)
    {
        RtcSlider::validate_setting(PriorityName, Reader->get_file_name());
        Priority = CreateID(PriorityName);
    }

    AmbientScriptInfo* const Script = AppendRoomRecord(AmbientScripts);
    Script->Type = Type;
    malloc_strcpy(&Script->Path, ScriptPath);
    Script->Priority = Priority;
}

// 06275aa0 -- [Camera]. The name goes into +0x1c4 without freeing the previous one.
void RoomData::read_Camera_block(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("name"))
            malloc_strcpy(&CameraName, Reader->get_value_string());
        else if (Reader->is_value("index"))
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: index is obsolete in %s, use a camera name\n",
                  kRoomDataSource, 0x4d7, Reader->get_file_name());
        else if (Reader->is_value("near_plane"))
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: near_plane is obsolete in %s, use a camera name\n",
                  kRoomDataSource, 0x4db, Reader->get_file_name());
        else if (Reader->is_value("far_plane"))
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: far_plane is obsolete in %s, use a camera name\n",
                  kRoomDataSource, 0x4df, Reader->get_file_name());
        else
            Reader->get_file_name();
    }
}

// 06275860 -- [Sound] / [Room_Sound]: "music" / "ambient" = id [, oneshot].
void RoomData::read_Sound_block(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("music"))
        {
            Music.Id = CreateID(Reader->get_value_string(0));
            bMusicLoop = true;
            if (!Reader->is_value_empty(1) &&
                CallDacomStricmp(Reader->get_value_string(1), "oneshot") == 0)
                bMusicLoop = false;
        }
        else if (Reader->is_value("ambient"))
        {
            Ambient.Id = CreateID(Reader->get_value_string(0));
            bAmbientLoop = true;
            if (!Reader->is_value_empty(1) &&
                CallDacomStricmp(Reader->get_value_string(1), "oneshot") == 0)
                bAmbientLoop = false;
        }
        else
        {
            Reader->get_file_name();
        }
    }
}

void RoomData::read_CharacterPlacement_block(INI_Reader* Reader)
{
    // 06276540. The block does not create a placement (read_set_script makes them from the
    // setpoints); it only gives an existing one its start script.
    char* Name = nullptr;
    char* StartScript = nullptr;
    while (Reader->read_value())
    {
        // A repeated key overwrites the local without freeing it (retail).
        if (Reader->is_value("name"))
            malloc_strcpy(&Name, Reader->get_value_string());
        else if (Reader->is_value("start_script"))
            malloc_strcpy(&StartScript, Reader->get_value_string());
        else if (Reader->is_value("zoom_in"))
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: zoom_in in file %s is obsolete\n",
                  kRoomDataSource, 0x635, Reader->get_file_name());
        else if (Reader->is_value("zoom_out"))
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: zoom_out in file %s is obsolete\n",
                  kRoomDataSource, 0x639, Reader->get_file_name());
        else
            Reader->get_file_name(); // called, result unused
    }

    // Reported, not a stop -- the lookup below still runs with the null name.
    if (Name == nullptr)
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: [CharacterPlacement] in file %s did not supply name\n",
              kRoomDataSource, 0x647, Reader->get_file_name());

    // The last match wins.
    CharPlaceInfo* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < CharacterPlacements.size(); ++Index)
    {
        if (CallDacomStricmp(Name, CharacterPlacements[Index]->Name) == 0)
            Found = CharacterPlacements[Index];
    }

    // +0x40; the previous start script is not freed.
    if (Found != nullptr)
        malloc_strcpy(const_cast<char**>(&Found->StartScript), StartScript);
    else
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: [CharacterPlacement] in file %s refers to setpoint %s which does "
              "not exist\n",
              kRoomDataSource, 0x656, Reader->get_file_name(), Name);

    malloc_strfree(&Name);
    malloc_strfree(&StartScript);
}

// 06276730. The placement is appended first (FUN_06279120), so even an empty block adds one.
// "name" names a room setpoint (find_setpoint inlined, last match wins): its name fills an
// unset +0x00 and its frame +0x04.
void RoomData::read_PlayerShipPlacement_block(INI_Reader* Reader)
{
    ShipPlaceInfo* const Place = AppendRoomRecord(ShipPlacements);
    Place->bIsPlayerShip = true;
    Place->bIsForSale = false;

    while (Reader->read_value())
    {
        if (Reader->is_value("name"))
        {
            char const* const FileName = Reader->get_file_name();
            char const* const SetpointName = Reader->get_value_string();
            SetpointInfo const* const Setpoint = find_setpoint(SetpointName, FileName);
            if (Setpoint != nullptr)
            {
                if (Place->Nickname == nullptr)
                    malloc_strcpy(const_cast<char**>(&Place->Nickname), Setpoint->Name);
                Place->Frame = Setpoint->Frame;
            }
        }
        else if (Reader->is_value("landing_script"))
        {
            malloc_strcpy(const_cast<char**>(&Place->LandingScript), Reader->get_value_string());
        }
        else if (Reader->is_value("launching_script"))
        {
            malloc_strcpy(const_cast<char**>(&Place->LaunchingScript),
                          Reader->get_value_string());
        }
        else
        {
            Reader->get_file_name();
        }
    }
}

// 062768b0 (also inlined in read_from_ini). As the player placement with only the "name" key.
void RoomData::read_ForSaleShipPlacement_block(INI_Reader* Reader)
{
    ShipPlaceInfo* const Place = AppendRoomRecord(ShipPlacements);
    Place->bIsPlayerShip = false;
    Place->bIsForSale = true;

    while (Reader->read_value())
    {
        if (Reader->is_value("name"))
        {
            char const* const FileName = Reader->get_file_name();
            char const* const SetpointName = Reader->get_value_string();
            SetpointInfo const* const Setpoint = find_setpoint(SetpointName, FileName);
            if (Setpoint != nullptr)
            {
                if (Place->Nickname == nullptr)
                    malloc_strcpy(const_cast<char**>(&Place->Nickname), Setpoint->Name);
                Place->Frame = Setpoint->Frame;
            }
        }
        else
        {
            Reader->get_file_name();
        }
    }
}

void RoomData::read_Hotspot_block(INI_Reader* Reader)
{
    // RVA 0x169d0. The "behavior=" option table, .data:063eabc8 in table order; no match gives
    // -1.
    static const option_spec kExitDoorBehaviors[] = {
        {"ExitDoor", kHotspotExitDoor},                 {"NewsVendor", kHotspotNewsVendor},
        {"MissionVendor", kHotspotMissionVendor},       {"Launch", kHotspotLaunch},
        {"StartDealer", kHotspotStartDealer},           {"StartShipDealer", kHotspotStartShipDealer},
        {"FrontDesk", kHotspotFrontDesk},               {"StartEquipDealer", kHotspotStartEquipDealer},
        {"MoveRight", kHotspotMoveRight},               {"MoveLeft", kHotspotMoveLeft},
        {"VirtualRoom", kHotspotVirtualRoom},           {"Repair", kHotspotRepair},
    };

    // Appended before the keys are read (FUN_06279400), so every [Hotspot] block adds an entry,
    // even an empty one.
    RoomButtonInfo& Button = *AppendRoomRecord(Hotspots);

    while (Reader->read_value())
    {
        if (Reader->is_value("name"))
            Button.Name = MallocCopy(Reader->get_value_string());
        else if (Reader->is_value("behavior"))
        {
            char const* Text = MallocCopy(Reader->get_value_string());
            Button.Behavior = static_cast<int>(select_option_from_string(
                const_cast<char*>(Text), kExitDoorBehaviors,
                static_cast<int>(sizeof(kExitDoorBehaviors) / sizeof(kExitDoorBehaviors[0])),
                Reader->get_file_name()));
            malloc_strfree(const_cast<char**>(&Text));
        }
        // %s is the ADDRESS of INI_Reader::get_name (see IniReaderGetNameCode), as in retail.
        else if (Reader->is_value("zoom_in"))
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: zoom_in is obsolete in ini file %s\n",
                  kRoomDataSource, 0x6e7, IniReaderGetNameCode());
        else if (Reader->is_value("zoom_out"))
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: zoom_out is obsolete in ini file %s\n",
                  kRoomDataSource, 0x6eb, IniReaderGetNameCode());
        else if (Reader->is_value("room_switch"))
            Button.RoomSwitch = MallocCopy(Reader->get_value_string());
        else if (Reader->is_value("virtual_room"))
            Button.VirtualRoom = MallocCopy(Reader->get_value_string());
        else if (Reader->is_value("set_virtual_room"))
            Button.SetVirtualRoom = MallocCopy(Reader->get_value_string());
        // _ftol (64-bit truncation, low dword kept) on the unrounded x87 value, OR'ed in.
        else if (Reader->is_value("state_send"))
            Button.StateSend |= static_cast<unsigned int>(static_cast<__int64>(Reader->value_num(0)));
        else if (Reader->is_value("state_read"))
            Button.StateRead |= static_cast<unsigned int>(static_cast<__int64>(Reader->value_num(0)));
        else if (Reader->is_value("state_transition"))
        {
            // All four slots in use -> the value is dropped.
            int Slot = 0;
            for (Slot = 0; Slot < 4; ++Slot)
            {
                if (Button.StateTransition[Slot] == nullptr)
                {
                    Button.StateTransition[Slot] = MallocCopy(Reader->get_value_string());
                    break;
                }
            }
        }
        else
            Reader->get_file_name(); // called, result unused
    }
}

void RoomData::read_FlashLightSet_block(INI_Reader* Reader)
{
    // 0x06276c00 (append FUN_06279670, Mode at +0x1c).
    FlashLightSetInfo* const Set = AppendRoomRecord(FlashLightSets);
    Set->Mode = 1;
    ReadFlashLightKeys(*Reader, *Set, false);
}

void RoomData::read_FlashLightLine_block(INI_Reader* Reader)
{
    // 0x06276df0
    FlashLightSetInfo* const Set = AppendRoomRecord(FlashLightSets);
    Set->Mode = 2;
    ReadFlashLightKeys(*Reader, *Set, true);
}

// 06275960. Direction into +0x30, the D3DLIGHT8 into +0x3c, Type into +0xa4. Id is never read.
void RoomData::add_set_script_light(unsigned long Id, Csys const& Frame,
                                    ThornLightProperties const* Props, unsigned char Type)
{
    (void)Id;
    RoomLightInfo* const Light = AppendRoomRecord(Lights);
    Light->Frame = Frame;
    memcpy(&Light->Direction, &Props->Direction, sizeof(Light->Direction));
    // The blob from its +0x28 (Type) to its end: the 0x1a dwords of the D3DLIGHT8.
    static_assert(sizeof(FThornLightProps) - offsetof(FThornLightProps, Type) == 0x1a * sizeof(unsigned),
                  "the light blob's D3DLIGHT8 tail is 0x1a dwords");
    memcpy(&Light->Mode, &Props->Type, 0x1a * sizeof(unsigned));
    Light->Type = Type;
}

// 062759b0
void RoomData::add_set_script_Prop(char const* Template, char const* Name, int Category,
                                   Csys const& Frame, bool bVisible, bool bLit, bool bFog,
                                   bool bRoomMain, unsigned char LightGroup, signed char Layer,
                                   unsigned long RenderType, char const* Loadout,
                                   unsigned long PriorityId, bool bRunningLights)
{
    RoomPropInfo* const Prop = AppendRoomRecord(Props);
    malloc_strcpy(const_cast<char**>(&Prop->Template), Template);
    malloc_strcpy(const_cast<char**>(&Prop->Name), Name);
    Prop->Category = Category;
    Prop->Frame = Frame;
    Prop->bVisible = bVisible;
    Prop->bLit = bLit;
    Prop->bFog = bFog;
    Prop->bRoomMain = bRoomMain;
    Prop->LightGroup = LightGroup;
    Prop->Layer = static_cast<unsigned char>(Layer);
    Prop->RenderType = RenderType;
    malloc_strcpy(const_cast<char**>(&Prop->Loadout), Loadout);
    Prop->PriorityId = PriorityId;
    Prop->bRunningLights = bRunningLights;
}

// 06275a50. No null test on the properties.
void RoomData::add_set_script_Camera(unsigned long Id, Csys const& Frame, char const* Name,
                                     ThornCameraProperties const* CamProps)
{
    (void)Id;
    RoomCameraInfo* const Info = AppendRoomRecord(Cameras);
    malloc_strcpy(const_cast<char**>(&Info->Name), Name);
    Info->Frame = Frame;
    Info->Fov = CamProps->Fovh;
    Info->NearPlane = CamProps->NearPlane;
    Info->FarPlane = CamProps->FarPlane;
}

// 06276180. The id and the entity are not read.
void RoomData::add_set_script_Setpoint(unsigned long Id, Csys const& Frame, char const* Name,
                                       ThornEntity const* Entity)
{
    (void)Id;
    (void)Entity;
    SetpointInfo* const Setpoint = AppendRoomRecord(Setpoints);
    malloc_strcpy(const_cast<char**>(&Setpoint->Name), Name);
    Setpoint->Frame = Frame;
    Setpoint->Props.extract_from_name(Name);
}

// 06275f30
void RoomData::unsetup_Camera()
{
    Camera.destroy();
}

// 06275fa0. The LAST match wins.
RoomCameraInfo const* RoomData::find_camera(char const* Name, char const* ScriptName) const
{
    RoomCameraInfo const* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < Cameras.size(); ++Index)
    {
        if (CallDacomStricmp(Name, Cameras[Index]->Name) == 0)
            Found = Cameras[Index];
    }
    if (Found == nullptr)
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: script %s calls for camera %s which does not exist in room "
              ".ini = %s\n",
              kRoomDataSource, 0x557, ScriptName, Name, File);
    return Found;
}

// 06276030 (make_setpoint_name_generic inlined). The LAST match wins; the miss error names the
// reduced pattern.
RoomCameraInfo const* RoomData::find_camera_generic(char const* Pattern, char const* ScriptName) const
{
    char Generic[0x400];
    make_setpoint_name_generic(Pattern, Generic);

    RoomCameraInfo const* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < Cameras.size(); ++Index)
    {
        char Candidate[0x400];
        make_setpoint_name_generic(Cameras[Index]->Name, Candidate);
        if (CallDacomStricmp(Generic, Candidate) == 0)
            Found = Cameras[Index];
    }
    if (Found == nullptr)
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: script %s calls for a camera name matching */%s/*, but none "
              "exist in room .ini = %s\n",
              kRoomDataSource, 0x578, ScriptName, Generic, File);
    return Found;
}

// 062761c0. The LAST match wins.
SetpointInfo const* RoomData::find_setpoint(char const* Name, char const* ScriptName) const
{
    SetpointInfo const* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < Setpoints.size(); ++Index)
    {
        if (CallDacomStricmp(Name, Setpoints[Index]->Name) == 0)
            Found = Setpoints[Index];
    }
    if (Found == nullptr)
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: script %s calls for setpoint %s which does not exist in room "
              ".ini = %s\n",
              kRoomDataSource, 0x5a8, ScriptName, Name, File);
    return Found;
}

// 06276250. As find_camera_generic, but the miss error names the pattern as given, not its
// reduction.
SetpointInfo const* RoomData::find_setpoint_generic(char const* Pattern, char const* ScriptName) const
{
    char Generic[0x400];
    make_setpoint_name_generic(Pattern, Generic);

    SetpointInfo const* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < Setpoints.size(); ++Index)
    {
        char Candidate[0x400];
        make_setpoint_name_generic(Setpoints[Index]->Name, Candidate);
        if (CallDacomStricmp(Generic, Candidate) == 0)
            Found = Setpoints[Index];
    }
    if (Found == nullptr)
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: script %s calls for setpoint %s which does not exist in room "
              ".ini = %s\n",
              kRoomDataSource, 0x5c8, ScriptName, Pattern, File);
    return Found;
}

// 062763b0. The LAST match wins, no message.
CharPlaceInfo* RoomData::find_CharacterPlacement(char const* Name)
{
    CharPlaceInfo* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < CharacterPlacements.size(); ++Index)
    {
        if (CallDacomStricmp(Name, CharacterPlacements[Index]->Name) == 0)
            Found = CharacterPlacements[Index];
    }
    return Found;
}

// 06276420 (find_setpoint inlined). No null test on OutFrame.
SetpointInfo const* RoomData::apply_setpoint(char const* Name, char** OutName,
                                             Csys* OutFrame, char const* ScriptName)
{
    SetpointInfo const* const Setpoint = find_setpoint(Name, ScriptName);
    if (Setpoint == nullptr)
        return nullptr;
    if (OutName != nullptr && *OutName == nullptr)
        malloc_strcpy(OutName, Setpoint->Name);
    *OutFrame = Setpoint->Frame;
    return Setpoint;
}

// 062764e0. The part between the first and the last '/' (unbounded strcpy); fewer than two
// slashes yield "".
void __cdecl RoomData::make_setpoint_name_generic(char const* Name, char* OutGeneric)
{
    char const* const FirstSlash = strchr(Name, '/');
    if (FirstSlash != nullptr)
    {
        strcpy(OutGeneric, FirstSlash + 1);
        char* const LastSlash = strrchr(OutGeneric, '/');
        if (LastSlash != nullptr)
        {
            *LastSlash = '\0';
            return;
        }
    }
    strcpy(OutGeneric, "");
}
