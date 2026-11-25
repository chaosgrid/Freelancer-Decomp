// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_misc.h"
#include "x86math.h" // I3DMathEngine
#include "common_reputation.h" // Reputation::Vibe::FreeDynamic (CleanUpDatabases)
#include "readfile.h"      // IFileSystem, FFileSystemDesc (DAEngine_create_filesystem)
#include "DALib.h"         // DALib::Data for filesystem ops
#include "dacom.h"         // FDUMP
#include "x87_trig.h"      // retail's inline FSIN / FCOS / FPTAN / FPATAN (random_*, get_projected_bounding_sphere)

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <direct.h>
#include <io.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>          // SHGetFolderPathA (GetUserLocalDataPath / GetUserDataPath / GetScreenShotPath)
#include "common_csys.h"
#include "common_universe.h"
#include "common_id.h"       // ID_String -- the four globals below are really ID_String
#include "common_net.h"      // StateMachineMessage::GenerateUniqueMessageType (SMM_* ids)
#include "common_text.h"     // ViewRect (Left/Top/Right/Bottom) for get_projected_bounding_box

// ============================================================================
// Math utilities
// ============================================================================

int CallDacomStricmp(const char* Left, const char* Right);  // common_dacom_stricmp.cpp

// .text:06303840. strtok only cuts the first token in place; every compare then uses String
// itself (DACOM.DLL stricmp). DefaultName is never read.
COMMON_API unsigned long __cdecl select_option_from_string(char* String, option_spec const* const Options, int Count, char const* DefaultName)
{
    char* Token = strtok(String, ",;/ ");
    if (Token != nullptr && Count > 0)
    {
        for (int i = 0; i < Count; ++i)
        {
            if (CallDacomStricmp(String, Options[i].Name) == 0)
            {
                return Options[i].Value;
            }
        }
    }
    return 0xffffffff;
}

// ============================================================================
// CRT-heap string helpers (malloc_strcpy family; StringAlloc is the StringCache in common_stringcache.cpp)
// ============================================================================

// CRT-heap string helpers. Unlike StringAlloc (StringCache pool, never freed) these strings
// are owned by the caller and released with malloc_strfree / malloc_wstrfree.

char const* __cdecl malloc_strcpy(char** Dest, char const* Src)
{
    // FUN_06334400. The old *Dest is not freed; Dest is dereferenced unchecked.
    if (Src != nullptr)
    {
        *Dest = _strdup(Src);
        return *Dest;
    }
    *Dest = nullptr;
    return *Dest;
}

void __cdecl malloc_strfree(char** Str)
{
    // FUN_06334430 (also exported as malloc_wstrfree, one body). Str is dereferenced unchecked.
    if (*Str != nullptr)
    {
        free(*Str);
        *Str = nullptr;
    }
}

wchar_t const* __cdecl malloc_wstrcpy(wchar_t** Dest, wchar_t const* Src)
{
    // FUN_06334450
    if (Src != nullptr)
    {
        *Dest = _wcsdup(Src);
        return *Dest;
    }
    *Dest = nullptr;
    return *Dest;
}

void __cdecl malloc_wstrfree(wchar_t** Str)
{
    // FUN_06334430, the same body as malloc_strfree.
    if (*Str != nullptr)
    {
        free(*Str);
        *Str = nullptr;
    }
}

// ============================================================================
// Client/server run-permission handshake
// ============================================================================

namespace
{
    // Both events come from CRT static initialisers, client first (LAB_0631d970 / LAB_0631d990):
    // manual reset, initially signalled. Nothing closes them.
    HANDLE GRunPermissionClientEvent =                       // DAT_06401810
        CreateEventA(nullptr, TRUE, TRUE, "MicrosoftFreelancerClientCanRunEvent");
    HANDLE GRunPermissionServerEvent =                       // DAT_0640180c
        CreateEventA(nullptr, TRUE, TRUE, "MicrosoftFreelancerServerCanRunEvent");

    unsigned long const kClientRunTimeoutMs = 0x1388;   // 5000
    unsigned long const kServerRunTimeoutMs = 0x1f4;    // 500
}

void __cdecl WaitForClientRunPermission()
{
    // FUN_0631d9b0
    ResetEvent(GRunPermissionServerEvent);
    if (WaitForSingleObject(GRunPermissionClientEvent, kClientRunTimeoutMs) == WAIT_TIMEOUT)
        SetEvent(GRunPermissionClientEvent);
}

void __cdecl WaitForServerRunPermission()
{
    // FUN_0631d9f0: the mirror image, with the shorter budget.
    ResetEvent(GRunPermissionClientEvent);
    if (WaitForSingleObject(GRunPermissionServerEvent, kServerRunTimeoutMs) == WAIT_TIMEOUT)
        SetEvent(GRunPermissionServerEvent);
}

void __cdecl GiveClientRunPermission()
{
    // FUN_0631da30
    SetEvent(GRunPermissionClientEvent);
}

void __cdecl GiveServerRunPermission()
{
    // FUN_0631da40
    SetEvent(GRunPermissionServerEvent);
}

unsigned long __cdecl SafeWaitForSingleObject(HANDLE Handle, unsigned long Timeout)
{
    // FUN_062fd4c0. Wait on one object but keep the message queue moving. WM_QUIT stops the
    // wait and reports a timeout; retail removes it from the queue and does not re-post it.
    for (;;)
    {
        MSG Msg;
        while (PeekMessageA(&Msg, nullptr, 0, 0, PM_REMOVE) != 0)
        {
            if (Msg.message == WM_QUIT)
                return WAIT_TIMEOUT;
            DispatchMessageA(&Msg);
        }

        // Retail passes 0xff literally. That was QS_ALLINPUT when this shipped;
        // on Vista and later QS_ALLINPUT gained QS_RAWINPUT and became 0x4ff, so the constant
        // is spelled out here to keep the wake mask retail's.
        unsigned long const kRetailWakeMask = 0xff;
        DWORD const Result =
            MsgWaitForMultipleObjects(1, &Handle, FALSE, Timeout, kRetailWakeMask);
        if (Result != 1)
            return Result;
    }
}

// ============================================================================
// Hex <-> wide-string and GUID text parsing
// ============================================================================

namespace
{
    // Retail inlines this test three times inside ParseGUID, on the raw signed char.
    bool IsGuidHexDigit(char Ch)
    {
        if (Ch >= '0' && Ch <= '9')
            return true;
        if (Ch >= 'a' && Ch <= 'f')
            return true;
        return Ch >= 'A' && Ch <= 'F';
    }

    // Advance past a run of hex digits, stopping at the separator or the terminator.
    char const* SkipGuidField(char const* Cursor)
    {
        while (*Cursor != '\0' && IsGuidHexDigit(*Cursor))
            Cursor += 1;
        return Cursor;
    }

    // The two-character group ParseGUID builds on the stack.
    unsigned long ReadGuidByte(char const* Pair)
    {
        char Buf[3];
        Buf[0] = Pair[0];
        Buf[1] = Pair[1];
        Buf[2] = '\0';
        return strtoul(Buf, nullptr, 16);
    }

    char const kNullGuidText[] = "{00000000-0000-0000-0000-000000000000}";
}

namespace
{
    // DAT_063fcc78 -- one shared static that every filetime_to_wstring call overwrites.
    wchar_t GFileTimeText[0x100];
}

wchar_t const* __cdecl filetime_to_wstring(_FILETIME const& Time)
{
    // FUN_06303360
    if (Time.dwHighDateTime == 0 && Time.dwLowDateTime == 0)
    {
        // A zero FILETIME is the empty string, not "01/01/1601".
        wcscpy(GFileTimeText, L"");
        return GFileTimeText;
    }

    SYSTEMTIME Local;
    FileTimeToSystemTime(&Time, &Local);

    // 0x400 is LOCALE_SYSTEM_DEFAULT, 0xe is
    // TIME_NOSECONDS|TIME_NOTIMEMARKER|TIME_FORCE24HOURFORMAT, 1 is DATE_SHORTDATE.
    char TimeText[0x40];
    GetTimeFormatA(0x400, 0xe, &Local, nullptr, TimeText, 0x40);
    char DateText[0x40];
    GetDateFormatA(0x400, 1, &Local, nullptr, DateText, 0x40);

    wchar_t WideTime[0x40];
    mbstowcs(WideTime, TimeText, 0x40);
    wchar_t WideDate[0x40];
    mbstowcs(WideDate, DateText, 0x40);

    wcscpy(GFileTimeText, WideDate);
    wcscat(GFileTimeText, L" ");
    wcscat(GFileTimeText, WideTime);
    return GFileTimeText;
}

void __cdecl HexToWide(char const* Src, wchar_t* Dst)
{
    // FUN_063032b0. The empty string takes the hex path too, where the first sscanf fails and
    // only the terminator is written.
    bool bAllHex = true;
    for (char const* Scan = Src; *Scan != '\0'; ++Scan)
    {
        if (isxdigit((unsigned char)*Scan) == 0)
        {
            bAllHex = false;
            break;
        }
    }

    if (!bAllHex)
    {
        size_t const Needed = mbstowcs(nullptr, Src, 0);
        mbstowcs(Dst, Src, Needed + 1);
        return;
    }

    char const* Cursor = Src;
    wchar_t* Out = Dst;
    unsigned int Value = 0;
    int Read = sscanf(Cursor, "%04x", &Value);
    while (Read != 0 && Read != -1)
    {
        *Out = (wchar_t)Value;
        Cursor += 4;
        Out += 1;
        Value = 0;
        Read = sscanf(Cursor, "%04x", &Value);
    }
    *Out = 0;
}

void __cdecl WideToHex(wchar_t const* Src, char* Dst)
{
    // FUN_06303260. Four characters per code unit, no terminator until the end.
    wchar_t const* Cursor = Src;
    char* Out = Dst;
    while (*Cursor != 0)
    {
        sprintf(Out, "%04x", (unsigned int)*Cursor);
        Cursor += 1;
        Out += 4;
    }
    *Out = '\0';
}

_GUID* __cdecl ParseGUID(char const* Text, _GUID* Out)
{
    // FUN_0631b980. Retail quirk: the brace search runs over Text, but the null-GUID shortcut
    // compares the original Text, not the position found.
    char const* Cursor = Text;
    while (*Cursor != '\0' && *Cursor != '{')
        Cursor += 1;

    if (strncmp(Text, kNullGuidText, strlen(kNullGuidText)) == 0)
    {
        Out->Data1 = 0;
        Out->Data2 = 0;
        Out->Data3 = 0;
        memset(Out->Data4, 0, sizeof(Out->Data4));
        return nullptr;
    }

    // Retail also strcpy's the tail into a stack buffer here and never reads it back; the copy
    // is omitted, see docs/re/deviations.md.
    Cursor += 1;

    char const* Field = Cursor;
    Cursor = SkipGuidField(Cursor);
    unsigned long const Data1 = strtoul(Field, nullptr, 16);
    Cursor += 1;                                  // past the '-'

    Field = Cursor;
    Cursor = SkipGuidField(Cursor);
    unsigned long const Data2 = strtoul(Field, nullptr, 16);
    Cursor += 1;

    Field = Cursor;
    Cursor = SkipGuidField(Cursor);
    unsigned long const Data3 = strtoul(Field, nullptr, 16);
    Cursor += 1;

    // The fourth group is two bytes, then the '-'.
    unsigned char Data4[8];
    Data4[0] = (unsigned char)ReadGuidByte(Cursor);
    Cursor += 2;
    Data4[1] = (unsigned char)ReadGuidByte(Cursor);
    Cursor += 3;

    int Index = 0;
    for (; Index < 6; ++Index)
    {
        Data4[2 + Index] = (unsigned char)ReadGuidByte(Cursor);
        Cursor += 2;
    }

    Out->Data1 = Data1;
    Out->Data2 = (unsigned short)Data2;
    Out->Data3 = (unsigned short)Data3;
    memcpy(Out->Data4, Data4, sizeof(Data4));

    // Retail compares against a never-written zero static, i.e. GUID_NULL.
    static _GUID const kNullGuid = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
    if (memcmp(Out, &kNullGuid, sizeof(_GUID)) == 0)
        return nullptr;
    return Out;
}

// FrustumCull / SphereCull and their helpers live in common_zone.cpp (Systems.cpp in retail).

// ============================================================================
// Global constants (initialized from retail .data values)
// ============================================================================

float CHATTER_MAX_DIST = 2000.0f;          // DAT_063ed1a4
float CHATTER_MAX_DIST_ATTEN = -20.0f;     // DAT_063ed1ac
float CHATTER_START_ATTEN = -5.0f;         // DAT_063ed1a8
float COLLISION_DAMAGE_FACTOR = 0.25f;     // DAT_063ed184
int COMM_CONFLICT_PRIORITY_CUTOFF = -2;    // DAT_063ed1b0 (retail default; ini can override)
float COMM_PLAYER_FAR_DIST = 2000.0f;      // DAT_063ed19c
float COMM_PLAYER_FAR_DIST_ATTEN = -15.0f; // DAT_063ed1a0
double DaysPerSecond = 1.0 / 60.0;         // DAT_063edb78 (1/60: the game's "day" is 60 real seconds)
float FIRE_FAILED_DELAY = 1.0f;            // DAT_063ed1c8
float JETTISONED_CARGO_VELOCITY = 10.0f;   // DAT_063ed194
float LOOT_OWNER_SAFE_TIME = 1.0f;         // DAT_063ed188
float LOOT_UNSEEN_LIFE_TIME = 60.0f;       // DAT_063ed18c
float LOOT_UNSEEN_RADIUS = 2500.0f;        // DAT_063ed190
int MAX_PLAYER_AMMO = 50;                  // DAT_063ed1d4
float MUSIC_CROSS_FADE_DELAY = 30.0f;      // DAT_063ed198
float MUZZLE_CONE_ANGLE = 90.0f;           // DAT_063ed1c4
float PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE = 1.0f; // DAT_063ed1d0
float PLAYER_COLLISION_GROUP_HIT_PTS_SCALE = 1.0f; // DAT_063ed1cc
float WALLA_MAX_DIST = 2500.0f;            // DAT_063ed1b4
float WALLA_MAX_DIST_ATTEN = -26.0f;       // DAT_063ed1b8
int WALLA_PRIORITY_CUTOFF = -4;            // DAT_063ed1c0
float WALLA_START_ATTEN = -8.0f;           // DAT_063ed1bc

bool PlayerTrailAvailable = false;         // DAT_063fce0d

// These four are ?<name>@@3UID_String@@A in retail, so they must be the real ID_String.
// Retail sets each from its own static-initializer thunk (LAB_06304930 / 06304950 / 06304970,
// LAB_0632e6d0 for SpaceflightLocationID). CreateID only touches CFLIDMaker's lazily built
// singleton, so this is safe regardless of TU init order. ReadConstants can override the first
// three from the ini, as in retail.
ID_String DEATH_COMM_FUSE = { CreateID("death_comm") };       // DAT_0644ce10
ID_String FIRE_FAILED_SOUND = { CreateID("fire_dry") };       // DAT_0644ce14
ID_String SND_CARGO_JETTISONED = { CreateID("cargo_jettison") }; // DAT_0644ce18
ID_String SpaceflightLocationID = { CreateID("Spaceflight") };   // DAT_063f270c

unsigned short const SUBOBJ_ID_NONE = 0;         // .rdata:063a4aa4 (?SUBOBJ_ID_NONE@@3GB)
unsigned short const SUBOBJ_ID_ROOT = 1;         // .rdata:063a4aa6 (?SUBOBJ_ID_ROOT@@3GB)
unsigned short const SUBOBJ_ID_POWER = 2;        // .rdata:063a4aa8 (?SUBOBJ_ID_POWER@@3GB)

// SMM_CHANGE_STATE (.data 0x0640a768, ordinal 1948) and SMM_TIMEOUT (.data 0x0640a764, ordinal
// 1949) are message-type ids, not literals: each has its own CRT static initialiser
// (LAB_063331a0 / LAB_063331c0), an inlined StateMachineMessage::GenerateUniqueMessageType()
// (0x06332f70). Nothing else draws an id first, so they are 1 and 2 and the first runtime call
// returns 3. Declaration order below is load-bearing: it reproduces the CRT table order.
unsigned const SMM_CHANGE_STATE = StateMachineMessage::GenerateUniqueMessageType();  // == 1
unsigned const SMM_TIMEOUT = StateMachineMessage::GenerateUniqueMessageType();       // == 2

// ============================================================================
// Utility functions
// ============================================================================

// Constants shared by the random_* family (values read from the retail .rdata).
// Values that stay on the x87 stack in retail are held as double here.
namespace
{
float const GRandUnitScale = 3.0517578125e-05f;      // DAT_063997d0 (float, 1/32768)
double const GRandTwoPi = 6.2831854820251465;        // DAT_0639c9e0 (double; 2*pi rounded through float)
double const GRandOneThird = 0.3333333333333333;     // DAT_0639e0e8 (double)
float const GRandZero = 0.0f;                        // DAT_06399408 (float)
float const GRandHalf = 0.5f;                        // DAT_0639c13c (float)
float const GRandOne = 1.0f;                         // DAT_0639a1d0 (float)
double const GRandAlignedDot = 0.998;                // DAT_063a3c50 (double)

// The product stays unrounded.
double RandUnit()
{
    return (double)(rand()) * (double)(GRandUnitScale);
}
} // namespace

Vector __cdecl RandomVector(float Radius)
{
    // FUN_06303450. x and y are rounded to float, z stays exact; no rejection loop. All three
    // draws equal to 0x3fff divide by zero, as in retail.
    float const X = (float)(rand() - 0x3fff);
    float const Y = (float)(rand() - 0x3fff);
    double const Z = (double)(rand() - 0x3fff);
    double const Scale = (double)(Radius) /
        sqrt(Z * Z + (double)(Y) * Y + (double)(X) * X);

    Vector Result;
    Result.x = (float)((double)(X) * Scale);
    Result.y = (float)((double)(Y) * Scale);
    Result.z = (float)(Scale * Z);
    return Result;
}

Vector __cdecl random_on_unit_sphere()
{
    // FUN_0631da50. The lower clamp tests the unrounded value, the upper one the stored float.
    float const U1 = (float)(RandUnit());
    float const U2 = (float)(RandUnit());

    double const CosRaw = (double)(U1) + (double)(U1) - 1.0;
    float CosTheta = (float)(CosRaw);
    if (CosRaw < -1.0)
        CosTheta = -1.0f;
    if (CosTheta > 1.0f)
        CosTheta = 1.0f;

    double const Theta = acos((double)(CosTheta));
    double const Phi = (double)(U2) * GRandTwoPi;
    double const SinTheta = X87Sin(Theta);

    Vector Result;
    Result.x = (float)(X87Sin(Phi) * SinTheta);
    Result.y = X87CosF(Theta);
    Result.z = (float)(X87Cos(Phi) * SinTheta);
    return Result;
}

Vector __cdecl random_inside_sphere(float Radius)
{
    // FUN_0631db20. acos takes the unrounded cosine; the stored float feeds y.
    float const Scaled = (float)(pow(RandUnit(), GRandOneThird) * Radius);

    double const CosRaw = RandUnit() * 2.0 - 1.0;
    float const CosTheta = (float)(CosRaw);
    float const SinScaled = (float)(X87Sin(acos(CosRaw)) * Scaled);

    double const Phi = RandUnit() * GRandTwoPi;

    Vector Result;
    Result.x = (float)(X87Sin(Phi) * SinScaled);
    Result.y = (float)((double)(CosTheta) * Scaled);
    Result.z = (float)(X87Cos(Phi) * SinScaled);
    return Result;
}

Vector __cdecl random_inside_box(float X, float Y, float Z)
{
    // FUN_0631dd30: the box is [0, extent), not centred.
    float const ResultX = (float)((double)(rand()) * X * GRandUnitScale);
    float const ResultY = (float)((double)(rand()) * Y * GRandUnitScale);
    float const ResultZ = (float)((double)(rand()) * Z * GRandUnitScale);

    Vector Result;
    Result.x = ResultX;
    Result.y = ResultY;
    Result.z = ResultZ;
    return Result;
}

Vector __cdecl random_inside_cylinder(float Radius, float HalfHeight)
{
    // FUN_0631dbd0. The cylinder axis is y.
    float const Scaled = (float)(sqrt(RandUnit()) * Radius);
    float const NegHalfHeight = -HalfHeight;
    double const T = RandUnit();
    double const Phi = RandUnit() * GRandTwoPi;

    Vector Result;
    Result.x = (float)(X87Sin(Phi) * Scaled);
    Result.y = (float)(((double)(HalfHeight) - NegHalfHeight) * T + NegHalfHeight);
    Result.z = (float)(X87Cos(Phi) * Scaled);
    return Result;
}

Vector __cdecl random_inside_ellipsoid(float A, float B, float C)
{
    // FUN_0631ddb0. Three radius draws, then theta and phi.
    float const ScaledA = (float)(pow(RandUnit(), GRandOneThird) * A);
    float const ScaledB = (float)(pow(RandUnit(), GRandOneThird) * B);
    float const ScaledC = (float)(pow(RandUnit(), GRandOneThird) * C);

    double const CosRaw = RandUnit() * 2.0 - 1.0;
    float const CosTheta = (float)(CosRaw);
    float const SinTheta = X87SinF(acos(CosRaw));

    double const Phi = RandUnit() * GRandTwoPi;

    Vector Result;
    Result.x = (float)(X87Sin(Phi) * ((double)(SinTheta) * ScaledA));
    Result.y = (float)((double)(CosTheta) * ScaledB);
    Result.z = (float)(X87Cos(Phi) * ((double)(SinTheta) * ScaledC));
    return Result;
}

Vector __cdecl random_inside_ring(float Outer, float Inner, float HalfHeight)
{
    // FUN_0631dc80. As random_inside_cylinder (axis y) with R in [Inner, Outer).
    float const Scaled = (float)(sqrt(RandUnit()) * ((double)(Outer) - Inner) + Inner);
    float const NegHalfHeight = -HalfHeight;
    double const T = RandUnit();
    double const Phi = RandUnit() * GRandTwoPi;

    Vector Result;
    Result.x = (float)(X87Sin(Phi) * Scaled);
    Result.y = (float)(((double)(HalfHeight) - NegHalfHeight) * T + NegHalfHeight);
    Result.z = (float)(X87Cos(Phi) * Scaled);
    return Result;
}

Vector __cdecl random_sphere_constrained(Vector const& Dir, float Angle)
{
    // FUN_0631deb0. A random direction inside a cone of half-angle Angle around +z, rotated so
    // that +z maps onto Dir. Axis = Dir x (0,0,1) keeps retail's 0.0f products. No guard:
    // Dir == +z gives 0/0, as in retail. DAT_06401814 is the lazy math-engine pointer.
    double const ZeroZ = (double)(Dir.z) * GRandZero;
    float const AxisX = (float)((double)(Dir.y) - ZeroZ);
    float const AxisY = (float)(ZeroZ - Dir.x);
    float const ZeroX = (float)((double)(Dir.x) * GRandZero);
    float const ZeroY = (float)((double)(Dir.y) * GRandZero);
    float const AxisZ = (float)((double)(ZeroX) - ZeroY);

    double const HalfAngle = acos((double)(ZeroY) + ZeroX + Dir.z) * GRandHalf;
    Quaternion Rotation;
    Rotation.w = X87CosF(HalfAngle);
    float const AxisScale = (float)(X87Sin(HalfAngle) /
        sqrt((double)(AxisZ) * AxisZ + (double)(AxisY) * AxisY +
             (double)(AxisX) * AxisX));
    // FUN_06288830: each product rounded to float.
    Rotation.x = AxisScale * AxisX;
    Rotation.y = AxisScale * AxisY;
    Rotation.z = AxisScale * AxisZ;

    // One rounding (negation is exact).
    float const NegSinAngle = -X87SinF((double)(Angle));
    float const Height = (float)(RandUnit() * ((double)(GRandOne) - NegSinAngle) + NegSinAngle);
    float const Phi = (float)(RandUnit() * GRandTwoPi);
    double const Ring = X87Cos(asin((double)(Height)));

    Vector Point;
    Point.x = (float)(X87Cos((double)(Phi)) * Ring);
    Point.y = (float)(X87Sin((double)(Phi)) * Ring);
    Point.z = Height;

    Matrix Frame;
    Acquire3DMathEngine()->QuaternionToMatrix(Frame, Rotation);
    Vector Rotated;
    Acquire3DMathEngine()->TransposeTransform(Rotated, Frame, Point);

    Vector Result;
    Result.x = Rotated.x;
    Result.y = Rotated.y;
    Result.z = Rotated.z;
    return Result;
}

Vector __cdecl random_sphere_constrained_2(Vector const& Dir, float MinAngle, float MaxAngle)
{
    // FUN_0631e050. A random direction at an angle in [MinAngle, MaxAngle) from +z, spun about
    // z, then rotated so that +z maps onto Dir -- unless Dir is already within the 0.998 dot
    // (NaN included), when the spun vector is returned as is. Retail retakes cos of the
    // unrounded phi but sin of the float-rounded one.
    double const AngleValue = RandUnit() * ((double)(MaxAngle) - MinAngle) + MinAngle;
    Vector Cone;
    Cone.x = X87SinF(AngleValue);
    Cone.y = 0.0f;
    Cone.z = X87CosF(AngleValue);

    // One rounding each into the float matrix.
    double const PhiRaw = RandUnit() * GRandTwoPi;
    float const PhiStored = (float)(PhiRaw);
    float const CosPhi = X87CosF(PhiRaw);
    float const SinPhi = X87SinF((double)(PhiStored));
    Matrix Spin;
    Spin.E[0][0] = CosPhi;
    Spin.E[0][1] = -SinPhi;
    Spin.E[0][2] = 0.0f;
    Spin.E[1][0] = SinPhi;
    Spin.E[1][1] = CosPhi;
    Spin.E[1][2] = 0.0f;
    Spin.E[2][0] = 0.0f;
    Spin.E[2][1] = 0.0f;
    Spin.E[2][2] = 1.0f;

    Vector Spun;
    Acquire3DMathEngine()->TransformMatrix(Spun, Spin, Cone);

    double const ZeroY = (double)(Dir.y) * GRandZero;
    double const ZeroX = (double)(Dir.x) * GRandZero;
    double const Dot = ZeroX + ZeroY + Dir.z;
    float const DotStored = (float)(Dot);

    Vector Result;
    if (!(Dot < GRandAlignedDot))
    {
        Result.x = Spun.x;
        Result.y = Spun.y;
        Result.z = Spun.z;
        return Result;
    }

    double const ZeroZ = (double)(Dir.z) * GRandZero;
    float const AxisX = (float)((double)(Dir.y) - ZeroZ);
    float const AxisY = (float)(ZeroZ - Dir.x);
    float const AxisZ = (float)(ZeroX - ZeroY);

    double const HalfAngle = acos((double)(DotStored)) * GRandHalf;
    double const AxisScale = X87Sin(HalfAngle) /
        sqrt((double)(AxisZ) * AxisZ + (double)(AxisY) * AxisY +
             (double)(AxisX) * AxisX);

    Quaternion Rotation;
    Rotation.w = X87CosF(HalfAngle);
    Rotation.x = (float)((double)(AxisX) * AxisScale);
    Rotation.y = (float)((double)(AxisY) * AxisScale);
    Rotation.z = (float)((double)(AxisZ) * AxisScale);

    Matrix Frame;
    Acquire3DMathEngine()->QuaternionToMatrix(Frame, Rotation);
    Vector Rotated;
    Acquire3DMathEngine()->TransposeTransform(Rotated, Frame, Spun);

    Result.x = Rotated.x;
    Result.y = Rotated.y;
    Result.z = Rotated.z;
    return Result;
}

char* __cdecl safe_strcpy(char* Dest, int Size, char const* Src)
{
    // FUN_063047d0. No argument checks.
    char* const Result = strncpy(Dest, Src, (size_t)Size);
    Dest[Size - 1] = '\0';
    return Result;
}

char* __cdecl safe_strcat(char* Dest, int Size, char const* Src)
{
    // FUN_06304800. A full destination returns null. strncat's terminator may land on
    // Dest[Size], as in retail.
    int const Length = (int)strlen(Dest);
    if (!(Length < Size))
        return nullptr;

    char* const Result = strncat(Dest, Src, (size_t)(Size - Length));
    Dest[Size - 1] = '\0';
    return Result;
}

wchar_t* __cdecl safe_wcscpy(wchar_t* Dest, int Size, wchar_t const* Src)
{
    // FUN_06304840. Size is a byte count. No checks.
    unsigned int const Count = (unsigned int)Size >> 1;
    wchar_t* const Result = wcsncpy(Dest, Src, Count);
    Dest[(int)Count - 1] = 0;
    return Result;
}

bool __cdecl strequal(char const* A, char const* B)
{
    // RVA 0xd4480: case-sensitive. Both null -> true, exactly one null -> false.
    if (A == nullptr || B == nullptr)
        return A == B;
    return strcmp(A, B) == 0;
}

void __cdecl BuildDirectory(char const* Path)
{
    // FUN_06304410. No null check on Path. Each prefix ending at a '\\', and the whole path, is
    // created when missing; a trailing '\\' ends the walk.
    char Buffer[0x104];
    strncpy(Buffer, Path, 0x104);
    Buffer[0x103] = '\0';

    char* Slash = strchr(Buffer, '/');
    while (Slash != nullptr)
    {
        *Slash = '\\';
        Slash = strchr(Buffer, '/');
    }

    if (Buffer[0] == '\0')
        return;

    char* Cursor = Buffer;
    for (;;)
    {
        char* const Separator = strchr(Cursor, '\\');
        if (Separator != nullptr)
            *Separator = '\0';
        if (Buffer[0] != '\0' && _access(Buffer, 0) != 0)
            _mkdir(Buffer);
        if (Separator == nullptr)
            return;
        Cursor = Separator + 1;
        *Separator = '\\';
        if (*Cursor == '\0')
            return;
    }
}

void __cdecl DebugPrint(char const* Format, ...)
{
    // FUN_06302dc0: formats and sends the text nowhere. Retail's vsprintf is unbounded, so a
    // text of 0x100 chars or more overruns the frame there as here.
    if (Format == nullptr)
        return;

    char Buffer[0x100];
    va_list Args;
    va_start(Args, Format);
    vsprintf(Buffer, Format, Args);
    va_end(Args);
}

// ============================================================================
// CmnMisc.cpp file-system paths, the single-player / MP-server flags,
// CleanUpDatabases and DllMain (retail 0x06302df0..0x06304880).
// ============================================================================

namespace
{
    const char kCmnMiscSource[] = "E:\\FL\\Scratch\\Source\\Common\\CmnMisc.cpp";
    const unsigned long kPathChars = 0x104;

    // DAT_063fccf8, the static buffer GetFullFilename returns.
    char GFullFilename[kPathChars];

    // DAT_063fce0c
    bool GbIsMPServer = false;

    // Desc layout is asserted once, in readfile.h (DOS/UTF prefix 0x34, MEM tail to 0x40).

    void CallIfsGetRootPath(IFileSystem* Filesys, char* Out, unsigned long MaxChars)
    {
        Filesys->GetFileSysPath(Out, MaxChars); // +0x84
    }

    bool AppendAndEnsureDir(char* Dest, char const* Subdir)
    {
        char Folder[kPathChars];
        strncpy(Folder, Dest, kPathChars);
        Folder[kPathChars - 1] = '\0';
        _makepath(Dest, nullptr, Folder, Subdir, nullptr);
        if (_access(Dest, 0) == 0)
            return true;
        if (_mkdir(Dest) == 0)
            return true;
        *Dest = '\0';
        return false;
    }

    // Failure empties Dest.
    bool FillSpecialFolder(char* const Dest, int CsIdl)
    {
        if (SHGetFolderPathA(nullptr, CsIdl, nullptr, 0, Dest) != 0)
        {
            *Dest = '\0';
            return false;
        }
        return true;
    }
}

// DAT_063ed17c, initialised to 1. True = single-player.
bool GbSinglePlayer = true;

bool __cdecl SinglePlayer()
{
    // RVA 0xa3220
    return GbSinglePlayer;
}

void __cdecl SetMultiPlayer(bool bMultiPlayer)
{
    // RVA 0xa3210
    GbSinglePlayer = !bMultiPlayer;
}

bool __cdecl IsMPServer()
{
    // RVA 0xa3250
    return GbIsMPServer;
}

void __cdecl SetIsMPServer(bool bIsMPServer)
{
    // RVA 0xa3230
    GbIsMPServer = bIsMPServer;
    if (bIsMPServer) {
        GbSinglePlayer = false;
    }
}

void __cdecl CleanUpDatabases()
{
    // RVA 0xa4870: the dynamic vibe map only -- not ArchDB / Groups / class lists.
    Reputation::Vibe::FreeDynamic();
}

void __cdecl GetFileSysPath(char* Dest, char const* RelOrAbs, IFileSystem* Filesys)
{
    // FUN_06302df0. An absolute path ('\\' first, or ':' second) is copied as is; otherwise it
    // is joined to the file system's root (Filesys, else DALib::Data).
    if (RelOrAbs == nullptr)
    {
        *Dest = '\0';
        return;
    }

    const bool bAbsSlash = RelOrAbs[0] == '\\';
    const bool bAbsDrive = RelOrAbs[0] != '\0' && RelOrAbs[1] == ':';
    if (bAbsSlash || bAbsDrive)
    {
        char const* Source = RelOrAbs;
        char* Out = Dest;
        char Copied;
        do
        {
            Copied = *Source;
            *Out = Copied;
            ++Source;
            ++Out;
        } while (Copied != '\0');
        return;
    }

    IFileSystem* Resolved = Filesys;
    if (Resolved == nullptr)
        Resolved = DALib::Data;
    if (Resolved == nullptr)
    {
        *Dest = '\0';
        return;
    }

    char RootDir[kPathChars];
    CallIfsGetRootPath(Resolved, RootDir, kPathChars);
    _makepath(Dest, nullptr, RootDir, RelOrAbs, nullptr);
}

char const* __cdecl GetFullFilename(char const* RelOrAbs)
{
    // FUN_06302ea0. No absolute-path short-circuit and no null check of DALib::Data.
    IFileSystem* Filesys = DALib::Data;
    char RootDir[kPathChars];
    char Joined[kPathChars];
    CallIfsGetRootPath(Filesys, RootDir, kPathChars);
    _makepath(Joined, nullptr, RootDir, RelOrAbs, nullptr);
    _fullpath(GFullFilename, Joined, kPathChars);
    return GFullFilename;
}

bool __cdecl GetUserLocalDataPath(char* const Dest)
{
    // FUN_06302f10
    if (!FillSpecialFolder(Dest, CSIDL_FLAG_CREATE | CSIDL_LOCAL_APPDATA))
        return false;
    return AppendAndEnsureDir(Dest, "Freelancer");
}

bool __cdecl GetUserDataPath(char* const Dest)
{
    // FUN_06302fa0
    if (!FillSpecialFolder(Dest, CSIDL_FLAG_CREATE | CSIDL_PERSONAL))
        return false;
    if (!AppendAndEnsureDir(Dest, "My Games"))
        return false;
    return AppendAndEnsureDir(Dest, "Freelancer");
}

bool __cdecl GetScreenShotPath(char* const Dest)
{
    // FUN_063030b0
    if (!FillSpecialFolder(Dest, CSIDL_FLAG_CREATE | CSIDL_MYPICTURES))
        return false;
    return AppendAndEnsureDir(Dest, "FreelancerShots");
}

void __cdecl DAEngine_create_filesystem(char const* Path, IFileSystem** Out,
                                        char const* Implementation)
{
    // FUN_06303140. No null checks of Out or DALib::Data, and no retry: a "FileSystem" path
    // that is a UTF file is resolved by the DOS file system itself (ReadFile re-enters
    // ICOManager with Parent = the DOS object), not here.
    *Out = nullptr;
    if (Path == nullptr || Path[0] == '\0')
    {
        *Out = nullptr;
        return;
    }

    IFileSystem* Filesys = DALib::Data;
    FFileSystemDesc Desc;
    memset(&Desc, 0, sizeof(Desc));
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.Implementation = Implementation;
    Desc.Path = Path;
    Desc.Access = 0x80000000;
    Desc.ShareMode = 1;
    Desc.Creation = 3;
    Desc.Flags = 0x8000080;
    Desc.FindHandle = 0xFFFFFFFF;

    // +0x0C IComponentFactory::CreateInstance -- the FileSystem desc stands in for AGGDESC.
    Filesys->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), reinterpret_cast<void**>(Out));

    if (*Out == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not load file %s\n", kCmnMiscSource, 0xd8,
              Path);
    }
}

void __cdecl DAEngine_release_filesystem(IFileSystem** Filesys)
{
    // FUN_0627ba10 (ICF-shared with ThornScriptDestroy). No null check of Filesys itself.
    IFileSystem* Instance = *Filesys;
    if (Instance == nullptr)
        return;

    Instance->Release(); // +0x08
    *Filesys = nullptr;
}

// FUN_06304880, the user DllMain (called from the CRT's _DllMainCRTStartup).

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID /*lpvReserved*/)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}


// FUN_06304320. Dist (the centre distance) is the first argument. Values retail keeps on the
// x87 stack are double here; the result is narrowed by the float return type.
float __cdecl GetSphereIntersectVol(float Dist, float RadiusA, float RadiusB)
{
    // Equal and unordered radii take RadiusB for both.
    const double Small = (RadiusA < RadiusB) ? RadiusA : RadiusB;
    const double Large = (RadiusA > RadiusB) ? RadiusA : RadiusB;

    // The small sphere lies inside the large one: its volume (4/3 pi, DAT_063a2718).
    if (Large - Small > Dist)
        return (float)(Small * (Small * Small) * 4.1887903f);

    // Apart (strictly).
    if ((Large + Small) < Dist)
        return 0.0f;

    // The lens; constants DAT_063a271c (2/3 pi), DAT_0639f520 (1/3), DAT_0639d208 (pi).
    const double D = Dist;
    const double SmallSq = Small * Small;
    const double LargeSq = Large * Large;
    const double H1 = ((D * D + SmallSq) - LargeSq) / (D + D);
    const double H2 = D - H1;
    const double Caps = (LargeSq * Large + SmallSq * Small) * 2.0943952f;
    const double Lens = (((H2 * Large * Large + H1 * Small * Small) - H1 * H1 * H1 * 0.33333334f) -
                         H2 * H2 * H2 * 0.33333334f) * 3.1415927f;
    return (float)(Caps - Lens);
}

// FUN_063044d0. x87 intermediates are carried in double; every retail float store is a float.
//   Out1 / Out2: the sphere centre on screen; Out3: its projected radius; Out4: its depth.
//   Returns 1 when the projected disc reaches into the viewport, 0 otherwise.
long __cdecl get_projected_bounding_sphere(ICamera* Camera, Vector const& Center, float Radius, float& Out1, float& Out2, float& Out3, float& Out4)
{
    // The centre in view space (the math engine cached in DAT_063fce08).
    const float* const CameraXform = (const float*)Camera->GetOrientation();
    Vector View;
    Acquire3DMathEngine()->InverseTransform(View, *reinterpret_cast<Transform const*>(CameraXform), Center);

    // Behind the near plane (or unordered) -> 0.
    if (!(-(double)(Camera->GetZNear()) >= (double)(View.z)))
        return 0;
    Out4 = -View.z;

    // The viewport centre (0.5f is DAT_0639c13c); InvZ = -1.0 (DAT_063a03f0) / z.
    int const* const Viewport = Camera->GetViewport();
    float const CenterX = (float)(
        (double)(Viewport[2] - Viewport[0]) * (double)(0.5f) +
        (double)(Viewport[0]));
    float const CenterY = (float)(
        (double)(Viewport[3] - Viewport[1]) * (double)(0.5f) +
        (double)(Viewport[1]));
    float const InvZ = (float)(-1.0 / (double)(View.z));
    float const XOverZ = (float)((double)(View.x) * (double)(InvZ));

    float const XScale = (float)(Camera->GetXScaleOverNear());
    Out1 = (float)((double)(Camera->GetZNear()) * (double)(XScale) *
                                  (double)(XOverZ) +
                              (double)(CenterX));

    // y * InvZ stays unrounded; the unrounded ScreenY also feeds DeltaY below.
    float const YScale = (float)(Camera->GetYScaleOverNear());
    double const ScreenY = (double)(Camera->GetZNear()) * (double)(YScale) *
                               ((double)(View.y) * (double)(InvZ)) +
                           (double)(CenterY);
    Out2 = (float)(ScreenY);

    double const X = (double)(View.x);
    double const Y = (double)(View.y);
    double const Z = (double)(View.z);
    float const Dist = (float)(std::sqrt((X * X + Z * Z) + Y * Y));
    // A radius beyond the distance is cut to it (unordered keeps it).
    if (Radius > Dist)
        Radius = Dist;

    // DeltaX uses the stored Out1, DeltaY the unrounded ScreenY.
    float const DeltaX = (float)(
        std::fabs((double)(Out1) - (double)(CenterX)));
    float const DeltaY = (float)(std::fabs(ScreenY - (double)(CenterY)));

    // Retail's inline FPATAN / FPTAN with _CIasin.
    float const AbsXOverZ = (float)(std::fabs((double)(XOverZ)));
    double const Angle = X87Atan((double)(AbsXOverZ));
    double const HalfAngle =
        std::asin((double)(Radius) / (double)(Dist));
    float const Delta =
        (float)((double)(AbsXOverZ) - X87Tan(Angle - HalfAngle));

    // The projected radius is stored and also kept unrounded for the tests below.
    float const ScaledDelta = (float)(Camera->GetXScaleOverNear() * (double)(Delta));
    double const ScreenRadius = (double)(Camera->GetZNear()) * (double)(ScaledDelta);
    Out3 = (float)(ScreenRadius);

    // Arithmetic shifts; both tests are strict, unordered fails.
    int const HalfWidth = (Viewport[2] - Viewport[0] + 1) >> 1;
    if (!((double)(HalfWidth) + ScreenRadius > (double)(DeltaX)))
        return 0;
    int const HalfHeight = (Viewport[3] - Viewport[1] + 1) >> 1;
    if (!((double)(DeltaY) < (double)(HalfHeight) + ScreenRadius))
        return 0;
    return 1;
}

// FUN_063046d0: get_projected_bounding_sphere, then centre +/- extent into edges.
long __cdecl get_projected_bounding_box(ICamera* Camera, Vector const& Center, float Radius, ViewRect* Rect, float& OutZ)
{
    float ScreenX, ScreenY, HalfExtent;
    if (!get_projected_bounding_sphere(Camera, Center, Radius, ScreenX, ScreenY, HalfExtent, OutZ))
    {
        return 0;
    }
    // ftol of the unrounded sums.
    Rect->Left   = (long)((double)ScreenX - (double)HalfExtent);
    Rect->Top    = (long)((double)ScreenY - (double)HalfExtent);
    Rect->Right  = (long)((double)ScreenX + (double)HalfExtent);
    Rect->Bottom = (long)((double)ScreenY + (double)HalfExtent);
    return 1;
}

// RVA 0x4a680, ?ComputeExplosiveImpulses@@YAXJAAVVector@@00@Z (Ghidra's decompile of it is
// garbled by x87 / stack-slot reuse). GetInstanceRelative returns {Count, Vector Items[]}.
// The math engine is cached in DAT_063fc2e4. Neither engine pointer is tested.
namespace
{
    // Inlined three times. Only an ordered-equal zero length is skipped (NaN normalises).
    void NormalizeExplosionVector(Vector& V)
    {
        double const X = V.x;
        double const Y = V.y;
        double const Z = V.z;
        double const Len = sqrt((X * X + Y * Y) + Z * Z);
        if (Len != 0.0)
        {
            double const Inv = 1.0 / Len;
            V.x = (float)(Inv * V.x);
            V.y = (float)(Inv * V.y);
            V.z = (float)(Inv * V.z);
        }
    }
}

void __cdecl ComputeExplosiveImpulses(long EngineHandle, Vector& OutLinear, Vector& OutAngular1, Vector& OutAngular2)
{
    const InstanceIndex Instance = EngineHandle;
    const InstanceIndex Parent = DALib::Engine->GetInstanceParent(Instance);
    unsigned long* const Relative = DALib::Engine->GetInstanceRelative(Instance);
    int const Count = (int)Relative[0];
    Vector const* const Items = (Vector const*)(Relative + 1);

    // Signed; 1..4 takes the first branch.
    if (Count > 0 && Count <= 4)
    {
        // Items[1] by the instance's orientation, plus its position.
        Matrix const* const Orient = DALib::Engine->GetInstanceOrientation(Instance);
        Vector Local;
        Acquire3DMathEngine()->TransformMatrix(Local, *Orient, Items[1]);
        OutAngular2.x = Local.x;
        OutAngular2.y = Local.y;
        OutAngular2.z = Local.z;
        const Vector* Pos = DALib::Engine->GetInstancePosition(Instance);
        OutAngular2.x = Pos->x + OutAngular2.x;
        OutAngular2.y = Pos->y + OutAngular2.y;
        OutAngular2.z = Pos->z + OutAngular2.z;

        // Items[0] by the parent's orientation.
        Matrix const* const ParentOrient = DALib::Engine->GetInstanceOrientation(Parent);
        Acquire3DMathEngine()->TransformMatrix(Local, *ParentOrient, Items[0]);
        OutAngular1.x = Local.x;
        OutAngular1.y = Local.y;
        OutAngular1.z = Local.z;
        NormalizeExplosionVector(OutAngular1);

        Pos = DALib::Engine->GetInstancePosition(Instance);
        OutLinear.x = OutAngular2.x - Pos->x;
        OutLinear.y = OutAngular2.y - Pos->y;
        OutLinear.z = OutAngular2.z - Pos->z;
        NormalizeExplosionVector(OutLinear);
        return;
    }

    const Vector* const Pos = DALib::Engine->GetInstancePosition(Instance);
    OutAngular2.x = Pos->x;
    OutAngular2.y = Pos->y;
    OutAngular2.z = Pos->z;

    // Items[1] by the parent's orientation.
    Matrix const* const ParentOrient = DALib::Engine->GetInstanceOrientation(Parent);
    Vector Local;
    Acquire3DMathEngine()->TransformMatrix(Local, *ParentOrient, Items[1]);
    OutAngular1.x = Local.x;
    OutAngular1.y = Local.y;
    OutAngular1.z = Local.z;
    NormalizeExplosionVector(OutAngular1);

    OutLinear.x = -OutAngular1.x;
    OutLinear.y = -OutAngular1.y;
    OutLinear.z = -OutAngular1.z;
}
