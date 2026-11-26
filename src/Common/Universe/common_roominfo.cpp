// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll room sub-records: RoomCameraInfo, SetpointProperties, SetpointInfo, CharPlaceInfo,
// ShipPlaceInfo, RoomButtonInfo, RoomLightInfo, RoomPropInfo and ExhaustNozzleInfo.

#include "common_goods.h"
#include "common_misc.h" // malloc_strcpy / malloc_strfree
#include "dacom.h"

#include <cstdio>
#include <cstring>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

// ---------------------------------------------------------------------------
// Room sub-structure implementations
// ---------------------------------------------------------------------------

RoomCameraInfo::RoomCameraInfo()
    : Name(nullptr)
    , Fov(35.0f)
    , NearPlane(0.25f)
    , FarPlane(1000000.0f)
{
    Frame.set_identity();
}

RoomCameraInfo::~RoomCameraInfo()
{
    // RVA 0x13300, ICF-shared with destroy().
    destroy();
}

// 06273370: no self-assignment test, so assigning an object to itself drops its Name and
// resets its Frame, as retail.
RoomCameraInfo& RoomCameraInfo::operator=(RoomCameraInfo const& Other)
{
    destroy();
    malloc_strcpy(const_cast<char**>(&Name), Other.Name);
    Frame = Other.Frame;
    Fov = Other.Fov;
    NearPlane = Other.NearPlane;
    FarPlane = Other.FarPlane;
    return *this;
}

void RoomCameraInfo::clear()
{
    Name = nullptr;
    Frame.set_identity();
    Fov = 35.0f;
    NearPlane = 0.25f;
    FarPlane = 1000000.0f;
}

void RoomCameraInfo::destroy()
{
    // RVA 0x13300 (ICF-shared with the dtor).
    malloc_strfree(const_cast<char**>(&Name));
    clear();
}

// SetpointProperties strings: +0x08 (Segment) is owned through malloc_strcpy /
// malloc_strfree in every retail writer (0627b220, 0627b501, 0627b1d8), never StringAlloc.

// 0627b190. +2/+3 and NameBuffer[1..15] are not written.
SetpointProperties::SetpointProperties()
    : bValid(0)
    , bZoneScript(0)
    , CharType(-1)
    , Segment(nullptr)
    , Zone(-1)
    , Posture(-1)
{
    NameBuffer[0] = '\0';
}

// 0627b1d0 (ICF-shared with destroy()).
SetpointProperties::~SetpointProperties()
{
    destroy();
}

// 0627b200. Segment is duplicated here (unlike operator=).
SetpointProperties::SetpointProperties(SetpointProperties const& Other)
    : bValid(Other.bValid)
    , bZoneScript(Other.bZoneScript)
    , CharType(Other.CharType)
    , Segment(nullptr)
{
    malloc_strcpy(const_cast<char**>(&Segment), Other.Segment);
    Zone = Other.Zone;
    strcpy(NameBuffer, Other.NameBuffer);
    Posture = Other.Posture;
}

// 06261270 (??4SetpointProperties, ICF-folded with MissileUseStruct::operator=): a plain
// 9-dword block copy. +0x08 is copied as a pointer, so the two objects share Segment and the
// old one leaks -- retail's own hazard, kept.
SetpointProperties& SetpointProperties::operator=(SetpointProperties const& Other)
{
    bValid = Other.bValid;
    bZoneScript = Other.bZoneScript;
    Pad02 = Other.Pad02;
    Pad03 = Other.Pad03;
    CharType = Other.CharType;
    Segment = Other.Segment;
    Zone = Other.Zone;
    memcpy(NameBuffer, Other.NameBuffer, sizeof(NameBuffer));
    Posture = Other.Posture;
    return *this;
}

// 0627b1b0. +0x08 is overwritten with 0 WITHOUT being freed (retail leaks it here; destroy()
// is the freeing variant).
void SetpointProperties::clear()
{
    bValid = 0;
    bZoneScript = 0;
    CharType = -1;
    Segment = nullptr;
    Zone = -1;
    NameBuffer[0] = '\0';
    Posture = -1;
}

// 0627b1d0
void SetpointProperties::destroy()
{
    malloc_strfree(const_cast<char**>(&Segment));
    clear();
}

// 0627b250. The unbounded copy is retail's (a source longer than 15 characters runs into
// Posture at +0x20), reproduced as is. toupper through unsigned char gives the C-locale result
// MSVCRT gives the sign-extended char retail passes.
void SetpointProperties::set_segment(char const* NewSegment)
{
    strcpy(NameBuffer, NewSegment);
    size_t Length = strlen(NameBuffer);
    size_t Index = 0;
    for (Index = 0; Index < Length; ++Index)
        NameBuffer[Index] = (char)toupper((unsigned char)NameBuffer[Index]);
}

namespace
{
    const char kSetpointSource[] = "E:\\FL\\Scratch\\Source\\Common\\SetpointProperties.cpp";

    // .data:063fbbe0 .. 063fbfe0 -- combine_to_name's result buffer (0x400 bytes).
    char GSetpointNameBuffer[0x400];
}

// 0627b2b0. Name convention "zs|zg / pc|npc|none|rpc / <+8> / <zone> / <+0x10> / posture".
// No reset of the object first: fields not reached keep their previous values.
void SetpointProperties::extract_from_name(char const* FullName)
{
    // A null name crashes in the strlen, as retail. tolower as in set_segment.
    char* Work = nullptr;
    malloc_strcpy(&Work, FullName);
    size_t Length = strlen(Work);
    size_t Index = 0;
    for (Index = 0; Index < Length; ++Index)
        Work[Index] = (char)tolower((unsigned char)Work[Index]);

    char* PrefixToken = strtok(Work, "/");
    char* CharTypeToken = strtok(nullptr, "/");
    char* SegmentToken = strtok(nullptr, "/");
    char* ZoneToken = strtok(nullptr, "/");
    char* NameToken = strtok(nullptr, "/");
    char const* PostureToken = strtok(nullptr, "/");

    // Neither "zs" nor "zg": not a setpoint, no dump. A null prefix crashes, as retail.
    if (strcmp(PrefixToken, "zs") != 0 && strcmp(PrefixToken, "zg") != 0)
    {
        bValid = 0;
        malloc_strfree(&Work);
        return;
    }

    // A missing token 0..4 is reported and parsing carries on regardless (a null token then
    // crashes below, exactly as retail does).
    if (PostureToken == nullptr)
        PostureToken = "";
    bValid = 1;
    if (PrefixToken == nullptr || CharTypeToken == nullptr || SegmentToken == nullptr ||
        ZoneToken == nullptr || NameToken == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: setpoint name %s does not match naming convention\n",
              kSetpointSource, 0x78, FullName);
    }

    if (strcmp(PrefixToken, "zs") == 0)
        bZoneScript = 1;
    else if (strcmp(PrefixToken, "zg") == 0)
        bZoneScript = 0;
    else
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: setpoint name prefix %s (of %s) is not zs/ or zg/\n",
              kSetpointSource, 0x87, PrefixToken, FullName);

    if (strcmp(CharTypeToken, "pc") == 0)
        CharType = 1;
    else if (strcmp(CharTypeToken, "npc") == 0)
        CharType = 0;
    else if (strcmp(CharTypeToken, "none") == 0)
        CharType = -1;
    else if (strcmp(CharTypeToken, "rpc") == 0)
        CharType = 2;
    else
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: setpoint character type %s (of %s) is not pc, npc, none, or rpc\n",
              kSetpointSource, 0x9e, CharTypeToken, FullName);

    // The previous value is not freed.
    malloc_strcpy(const_cast<char**>(&Segment), SegmentToken);

    char* ZoneEnd = nullptr;
    Zone = strtol(ZoneToken, &ZoneEnd, 10);
    if (ZoneEnd != nullptr && *ZoneEnd != '\0')
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: setpoint zone number %s (of %s) is not a number\n",
              kSetpointSource, 0xac, ZoneToken, FullName);

    set_segment(NameToken);

    if (strcmp(PostureToken, "stand") == 0)
        Posture = 0;
    else if (strcmp(PostureToken, "sitlow") == 0)
        Posture = 1;
    else if (strcmp(PostureToken, "sithigh") == 0)
        Posture = 2;
    else
    {
        if (bZoneScript == 0)
            FDUMP(kFdumpError,
                  "%s(%d) : *** ERROR: invalid posture %s in setpoint marker %s - if this is a special "
                  "relocation marker, mark it with Zs\n",
                  kSetpointSource, 0xc8, PostureToken, FullName);
        Posture = 3;
    }

    malloc_strfree(&Work);
}

// 0627b660: unchecked appends into the shared buffer.
char const* SetpointProperties::combine_to_name() const
{
    if (bValid == 0)
        return nullptr;

    GSetpointNameBuffer[0] = '\0';
    strcat(GSetpointNameBuffer, bZoneScript != 0 ? "Zs/" : "Zg/");

    if (CharType == 1)
        strcat(GSetpointNameBuffer, "pc/");
    else if (CharType == 2)
        strcat(GSetpointNameBuffer, "rpc/");
    else if (CharType == 0)
        strcat(GSetpointNameBuffer, "npc/");
    else
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kSetpointSource, 0xfe,
              "Invalid occupancy value for setpoint\n");

    // No null test: a null Segment faults, as retail.
    strcat(GSetpointNameBuffer, Segment);
    strcat(GSetpointNameBuffer, "/");

    char ZoneText[0x40];
    sprintf(ZoneText, "%02d/", Zone);
    strcat(GSetpointNameBuffer, ZoneText);

    strcat(GSetpointNameBuffer, NameBuffer);
    strcat(GSetpointNameBuffer, "/");

    if (Posture == 0)
        strcat(GSetpointNameBuffer, "stand");
    else if (Posture == 1)
        strcat(GSetpointNameBuffer, "sitlow");
    else if (Posture == 2)
        strcat(GSetpointNameBuffer, "sithigh");
    else if (Posture == 3)
        strcat(GSetpointNameBuffer, "special");
    else
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kSetpointSource, 0x127,
              "Invalid posture value for setpoint\n");

    return GSetpointNameBuffer;
}

SetpointInfo::SetpointInfo()
    : Name(nullptr)
{
    Frame.set_identity();
}

SetpointInfo::SetpointInfo(SetpointInfo const& Other)
    : Name(Other.Name)
    , Frame(Other.Frame)
    , Props(Other.Props)
{
    // 0x062612d0. Name is copied as a pointer: the copy shares it with Other, as in retail.
}

SetpointInfo::~SetpointInfo()
{
    destroy();
}

void SetpointInfo::clear()
{
    Name = nullptr;
    Frame.set_identity();
    Props.clear();
}

// 062737c0
void SetpointInfo::destroy()
{
    malloc_strfree(const_cast<char**>(&Name));
    Props.destroy();
    clear();
}

CharPlaceInfo::CharPlaceInfo()
    : Name(nullptr)
    , CharType(-1)
    , Posture(-1)
    , bZoneScript(0)
    , StartScript(nullptr)
{
    Frame.set_identity();
}

CharPlaceInfo::~CharPlaceInfo()
{
    destroy();
}

void CharPlaceInfo::clear()
{
    Name = nullptr;
    Frame.set_identity();
    CharType = -1;
    Posture = -1;
    bZoneScript = 0;
    StartScript = nullptr;
}

// 062738f0 (ICF-shared with the dtor)
void CharPlaceInfo::destroy()
{
    malloc_strfree(const_cast<char**>(&Name));
    malloc_strfree(const_cast<char**>(&StartScript));
    clear();
}

// 06273960
bool CharPlaceInfo::is_named(char const* TestName) const
{
    if (Name == nullptr)
        return false;
    return CallDacomStricmp(Name, TestName) == 0;
}

ShipPlaceInfo::ShipPlaceInfo()
    : Nickname(nullptr)
    , bIsPlayerShip(false)
    , bIsForSale(false)
    , LandingScript(nullptr)
    , LaunchingScript(nullptr)
{
    Frame.set_identity();
}

ShipPlaceInfo::~ShipPlaceInfo()
{
    destroy();
}

void ShipPlaceInfo::clear()
{
    Nickname = nullptr;
    Frame.set_identity();
    bIsPlayerShip = false;
    bIsForSale = false;
    LandingScript = nullptr;
    LaunchingScript = nullptr;
}

void ShipPlaceInfo::destroy()
{
    // 06273a50 (ICF-shared with the dtor)
    malloc_strfree(const_cast<char**>(&Nickname));
    malloc_strfree(const_cast<char**>(&LandingScript));
    malloc_strfree(const_cast<char**>(&LaunchingScript));
    clear();
}

RoomButtonInfo::RoomButtonInfo()
    : Name(nullptr)
    , RoomSwitch(nullptr)
    , VirtualRoom(nullptr)
    , SetVirtualRoom(nullptr)
    , Behavior(-1)
    , StateSend(0)
    , StateRead(0)
    , StateTransition{nullptr, nullptr, nullptr, nullptr}
{
}

RoomButtonInfo::~RoomButtonInfo()
{
    destroy();
}

void RoomButtonInfo::clear()
{
    Name = nullptr;
    RoomSwitch = nullptr;
    VirtualRoom = nullptr;
    SetVirtualRoom = nullptr;
    Behavior = -1;
    StateSend = 0;
    StateRead = 0;
    StateTransition[0] = nullptr;
    StateTransition[1] = nullptr;
    StateTransition[2] = nullptr;
    StateTransition[3] = nullptr;
}

void RoomButtonInfo::destroy()
{
    // 06273b30 (ICF-shared with the dtor)
    malloc_strfree(const_cast<char**>(&Name));
    malloc_strfree(const_cast<char**>(&RoomSwitch));
    malloc_strfree(const_cast<char**>(&VirtualRoom));
    malloc_strfree(const_cast<char**>(&SetVirtualRoom));
    int TransitionIndex = 0;
    for (TransitionIndex = 0; TransitionIndex < 4; ++TransitionIndex)
        malloc_strfree(const_cast<char**>(&StateTransition[TransitionIndex]));
    clear();
}

RoomLightInfo::RoomLightInfo()
    : Type(0)
{
    // RVA 0x13410. Direction (+0x30) is the ThornLightProperties +0xC triple read_set_script
    // rotates and normalises.
    Frame.set_identity();
    Direction.x = 0.0f;
    Direction.y = 0.0f;
    Direction.z = -1.0f;
    // Retail leaves +0x3C..+0xA3 (Mode, Color, Opaque4C) uninitialized; not zero-filled on
    // purpose. add_set_script_light overwrites the whole range before use.
}

RoomLightInfo::~RoomLightInfo()
{
    destroy();
}

void RoomLightInfo::clear()
{
    // RVA 0x13490: as the constructor, leaving Mode/Color/Opaque4C untouched.
    Frame.set_identity();
    Direction.x = 0.0f;
    Direction.y = 0.0f;
    Direction.z = -1.0f;
    Type = 0;
}

RoomLightInfo& RoomLightInfo::operator=(RoomLightInfo const& Other)
{
    // RVA 0x1290: a plain 0xA8-byte copy, no self-assignment check.
    Frame = Other.Frame;
    Direction.x = Other.Direction.x;
    Direction.y = Other.Direction.y;
    Direction.z = Other.Direction.z;
    Mode = Other.Mode;
    Color[0] = Other.Color[0];
    Color[1] = Other.Color[1];
    Color[2] = Other.Color[2];
    memcpy(Opaque4C, Other.Opaque4C, sizeof(Opaque4C));
    Type = Other.Type;
    return *this;
}

void RoomLightInfo::destroy()
{
    // RVA 0x48920 (ICF-folded empty body).
}

RoomPropInfo::RoomPropInfo()
    : Template(nullptr)
    , Name(nullptr)
    , Category(-1)
    , bVisible(true)
    , bLit(true)
    , bFog(true)
    , bRoomMain(false)
    , LightGroup(0)
    , Layer(0)
    , RenderType(0)
    , Loadout(nullptr)
    , PriorityId(0)
    , bRunningLights(false)
{
    // RVA 0x13510. bFog is +0x3e.
    Frame.set_identity();
}

RoomPropInfo::~RoomPropInfo()
{
    destroy();
}

RoomPropInfo& RoomPropInfo::operator=(RoomPropInfo const& Other)
{
    // 0x062612b0: a 0x54-byte block copy, strings as raw pointers (shared); the old strings
    // are not freed.
    Template = Other.Template;
    Name = Other.Name;
    Category = Other.Category;
    Frame = Other.Frame;
    bVisible = Other.bVisible;
    bLit = Other.bLit;
    bFog = Other.bFog;
    bRoomMain = Other.bRoomMain;
    LightGroup = Other.LightGroup;
    Layer = Other.Layer;
    RenderType = Other.RenderType;
    Loadout = Other.Loadout;
    PriorityId = Other.PriorityId;
    bRunningLights = Other.bRunningLights;
    return *this;
}

void RoomPropInfo::clear()
{
    // RVA 0x135b0. The strings are dropped, not freed (destroy() frees them).
    Template = nullptr;
    Name = nullptr;
    Category = -1;
    Frame.set_identity();
    bVisible = true;
    bLit = true;
    bFog = true;
    bRoomMain = false;
    LightGroup = 0;
    Layer = 0;
    RenderType = 0;
    Loadout = nullptr;
    PriorityId = 0;
    bRunningLights = false;
}

void RoomPropInfo::destroy()
{
    // 06273590 (ICF-shared with the dtor). No clear(): the other fields keep their values.
    malloc_strfree(const_cast<char**>(&Template));
    malloc_strfree(const_cast<char**>(&Name));
    malloc_strfree(const_cast<char**>(&Loadout));
}

ExhaustNozzleInfo::ExhaustNozzleInfo()
    : SubObjId(SUBOBJ_ID_NONE)
    , Pad02(0)
    , Index(0)
    , ParentInstance(0)
{
    memset(&Offset, 0, sizeof(Offset));
}

ExhaustNozzleInfo& ExhaustNozzleInfo::operator=(ExhaustNozzleInfo const& Other)
{
    SubObjId = Other.SubObjId;
    Pad02 = Other.Pad02;
    Index = Other.Index;
    ParentInstance = Other.ParentInstance;
    Offset = Other.Offset;
    return *this;
}

bool ExhaustNozzleInfo::GetHardpointOffset_NS(Transform& Out) const
{
    // RVA 0x35bf0. SUBOBJ_ID_NONE is read from .rdata:063a4aa4, not an immediate.
    if (SubObjId == SUBOBJ_ID_NONE)
    {
        return false;
    }
    Out = Offset;
    return true;
}
