// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll hardpoint lookups: FindHardpoint / FindHardpoint_OS / FindHardpoint_PS,
// HardpointToWorld and HardpointSummary.

#include "common_goods.h"
#include "common_cobject.h" // CompoundInstanceAnalyze
#include "common_misc.h"
#include "DALib.h"
#include "x86math.h" // I3DMathEngine::MulTransform (ComposeTransform)

#include <cstring>
#include <new>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    // The engine handle sentinel the whole hardpoint block compares against.
    long const kNoEngineHandle = -1;

    // FUN_0630e870 — the search shared by FindHardpoint and FindHardpoint_OS. Returns the
    // handle it matched on, which for a child hit is the child's, not the caller's.
    bool FindHardpointCore(long Handle, char const* Name, long* OutHandle, HardpointInfo* OutInfo)
    {
        // No null checks on Engine, Hardpoint, Name or the out pointers, as in retail.
        IEngine* const Engine = DALib::Engine;

        // The archetype (part) handle of the instance; -1 when there is none.
        void* const Part = Engine->GetInstanceArchetype((void*)Handle);
        if ((long)Part == kNoEngineHandle)
            return false;

        FHardpointKey Query;
        Query.Name = Name;
        Query.Crc = DACOM_CRC::GetCRC32(Name);

        bool const bFound = DALib::Hardpoint->GetHardpoint(Part, &Query, OutInfo) != 0;

        // The part is released whether or not the name matched.
        Engine->ReleaseHandle(Part);

        if (bFound)
        {
            *OutHandle = Handle;
            return true;
        }

        long Child = kNoEngineHandle;
        while (true)
        {
            Child = (long)Engine->GetRelatedInstance((void*)Handle, 1, (void*)Child);
            if (Child == kNoEngineHandle)
                return false;
            if (FindHardpointCore(Child, Name, OutHandle, OutInfo))
                return true;
        }
    }

    // Engine slot 40 (+0xa0) hands back the instance's transform: a 3x3 followed by a vector,
    // which is exactly the port's Transform.
    Transform const* EngineGetTransform(long Handle)
    {
        // No null gate, as in retail.
        IEngine* const Engine = DALib::Engine;
        return (Transform const*)Engine->GetInstanceTransform((void*)Handle);
    }

    // Out = Parent * Local via the 3DMathEngine's MulTransform. Transform and FTransform have
    // the same layout.
    void ComposeTransform(Transform& Out, Transform const& Parent, Transform const& Local)
    {
        Acquire3DMathEngine()->MulTransform((FTransform&)Out, (FTransform const&)Parent,
                                            (FTransform const&)Local);
    }
}

bool __cdecl FindHardpoint(long Handle, char const* Name, long& OutHandle)
{
    // FUN_0630e940; the info goes to an uninitialised scratch that is thrown away.
    HardpointInfo Scratch;
    return FindHardpointCore(Handle, Name, &OutHandle, &Scratch);
}

bool __cdecl FindHardpoint_OS(long Handle, char const* Name, long& OutHandle,
                              HardpointInfo& OutInfo)
{
    // 0xaeb70 — the core, unmodified.
    return FindHardpointCore(Handle, Name, &OutHandle, &OutInfo);
}

bool __cdecl FindHardpoint_PS(long Handle, char const* Name, long& OutHandle,
                              HardpointInfo& OutInfo)
{
    // FUN_0630e960. A hardpoint on Handle itself is returned in object space. Otherwise its
    // frame is lifted to world through the found instance's transform, then brought into
    // Handle's space with the inverse of Handle's transform built by hand (FUN_0630f140:
    // transposed rotation, minus the transposed-rotated translation). No null checks of either
    // engine transform.
    if (!FindHardpointCore(Handle, Name, &OutHandle, &OutInfo))
        return false;
    if (OutHandle == Handle)
        return true;

    Transform Local;
    Local.Rotation = OutInfo.Orient;
    Local.Translation = OutInfo.Position;

    Transform const* const Found = EngineGetTransform(OutHandle);
    Transform World;
    ComposeTransform(World, *Found, Local);
    OutInfo.Position = World.Translation;
    OutInfo.Orient = World.Rotation;
    Local = World;

    Transform const* const Space = EngineGetTransform(Handle);
    FVector Rotated;
    Acquire3DMathEngine()->TransposeTransform(Rotated, (FMatrix const&)Space->Rotation,
                                              (FVector const&)Space->Translation);
    Vector NegRotated;
    Vec3Assign(&NegRotated, -Rotated.X, -Rotated.Y, -Rotated.Z);
    Matrix const& R = Space->Rotation;
    Matrix const Transposed(R.E[0][0], R.E[1][0], R.E[2][0],
                            R.E[0][1], R.E[1][1], R.E[2][1],
                            R.E[0][2], R.E[1][2], R.E[2][2]);
    Transform Inverse;
    Inverse.Rotation = Transposed;
    Inverse.Translation = NegRotated;

    Transform Result;
    ComposeTransform(Result, Inverse, Local);
    OutInfo.Position = Result.Translation;
    OutInfo.Orient = Result.Rotation;
    return true;
}

void __cdecl HardpointToWorld(long Handle, HardpointInfo& Info)
{
    // FUN_0630eba0. No null check on the engine transform.
    Transform const* const Parent = EngineGetTransform(Handle);

    Transform Local;
    Local.Rotation = Info.Orient;
    Local.Translation = Info.Position;

    Transform World;
    ComposeTransform(World, *Parent, Local);
    Info.Position = World.Translation;
    Info.Orient = World.Rotation;
}

// ---------------------------------------------------------------------------
// HardpointSummary
// ---------------------------------------------------------------------------

// 063345c0
HardpointSummary::HardpointSummary()
    : Count(0)
    , Names(nullptr)
    , Infos(nullptr)
    , Instances(nullptr)
    , FillInstance(-1)
    , FillPart((void*)-1)
    , FillCursor(0)
{
}

// 06273150: a shallow copy -- the three array pointers are shared, so destroying both copies
// frees them twice (retail's own hazard).
HardpointSummary::HardpointSummary(HardpointSummary const& Other)
    : Count(Other.Count)
    , Names(Other.Names)
    , Infos(Other.Infos)
    , Instances(Other.Instances)
    , FillInstance(Other.FillInstance)
    , FillPart(Other.FillPart)
    , FillCursor(Other.FillCursor)
{
}

// 063345f0: destroy() inlined.
HardpointSummary::~HardpointSummary()
{
    destroy();
}

// 06273190: shallow; no self test, nothing freed.
HardpointSummary& HardpointSummary::operator=(HardpointSummary const& Other)
{
    Count = Other.Count;
    Names = Other.Names;
    Infos = Other.Infos;
    Instances = Other.Instances;
    FillInstance = Other.FillInstance;
    FillPart = Other.FillPart;
    FillCursor = Other.FillCursor;
    return *this;
}

// 06334660 (nothing freed).
void HardpointSummary::clear()
{
    Count = 0;
    Names = nullptr;
    Infos = nullptr;
    Instances = nullptr;
    FillInstance = -1;
    FillPart = (void*)-1;
    FillCursor = 0;
}

// 06334680. No null tests (Count > 0 implies the name array exists).
void HardpointSummary::destroy()
{
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
        malloc_strfree(const_cast<char**>(&Names[Index]));
    delete[] Names;
    Names = nullptr;
    operator delete(Infos);
    Infos = nullptr;
    delete[] Instances;
    clear();
}

int HardpointSummary::get_count() const
{
    return Count;
}

// 063346f0: Names[Index], unchecked.
char const* HardpointSummary::get_name(int Index) const
{
    return Names[Index];
}

// 06334700: Infos + Index * 0x48, unchecked.
HardpointInfo const& HardpointSummary::get_info(int Index) const
{
    return Infos[Index];
}

// 06334710: Instances[Index], unchecked.
long HardpointSummary::get_instance(int Index) const
{
    return Instances[Index];
}

// 06334720: whole scan with MSVCRT _stricmp, the LAST match wins; -1 when none.
int HardpointSummary::find_name(char const* Name) const
{
    int Result = -1;
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        if (_stricmp(Names[Index], Name) == 0)
            Result = Index;
    }
    return Result;
}

// 06334770: the same scan (MSVCRT _stricmp, last match wins); Instances[match] or -1.
long HardpointSummary::find_name_instance(char const* Name) const
{
    int Result = -1;
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        if (_stricmp(Names[Index], Name) == 0)
            Result = Index;
    }
    if (Result == -1)
        return -1;
    return Instances[Result];
}

// 063347c0: the same scan with DACOM.DLL!stricmp; &Infos[last match] or null.
HardpointInfo const* HardpointSummary::get_info_by_name(char const* Name) const
{
    HardpointInfo const* Result = nullptr;
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        if (CallDacomStricmp(Names[Index], Name) == 0)
            Result = &Infos[Index];
    }
    return Result;
}

// 06334810
void HardpointSummary::hardpoint_callback1(char const* Name, void* Context)
{
    (void)Name;
    HardpointSummary* const Self = (HardpointSummary*)Context;
    Self->Count++;
}

// 06334820. No null tests.
void HardpointSummary::count_hardpoints_callback(long Instance, void* Context)
{
    void* const Part = DALib::Engine->GetInstanceArchetype((void*)Instance);
    DALib::Hardpoint->EnumerateHardpoints(&hardpoint_callback1, Part, Context);
    DALib::Engine->ReleaseHandle(Part);
}

// 06334860
void HardpointSummary::count_hardpoints(long Instance)
{
    Count = 0;
    CompoundInstanceAnalyze(Instance, count_hardpoints_callback, this);
}

// 06334880. The info is copied only when the low byte of the lookup result is non-zero. No
// bounds or null tests.
void HardpointSummary::hardpoint_callback2(char const* Name, void* Context)
{
    HardpointSummary* const Self = (HardpointSummary*)Context;
    malloc_strcpy(const_cast<char**>(&Self->Names[Self->FillCursor]), Name);

    FHardpointKey Query;
    Query.Name = Name;
    Query.Crc = DACOM_CRC::GetCRC32(Name);
    HardpointInfo Found;
    unsigned long const Hit = DALib::Hardpoint->GetHardpoint(Self->FillPart, &Query, &Found);
    if ((unsigned char)Hit != 0)
        memcpy(&Self->Infos[Self->FillCursor], &Found, sizeof(HardpointInfo));

    Self->Instances[Self->FillCursor] = Self->FillInstance;
    Self->FillCursor++;
}

// 06334900. No null tests.
void HardpointSummary::fill_hardpoints_callback(long Instance, void* Context)
{
    void* const Part = DALib::Engine->GetInstanceArchetype((void*)Instance);
    HardpointSummary* const Self = (HardpointSummary*)Context;
    Self->FillInstance = Instance;
    Self->FillPart = Part;
    DALib::Hardpoint->EnumerateHardpoints(&hardpoint_callback2, Part, Context);
    DALib::Engine->ReleaseHandle(Part);
}

// 06334950
void HardpointSummary::fill_hardpoints(long Instance)
{
    FillCursor = 0;
    CompoundInstanceAnalyze(Instance, fill_hardpoints_callback, this);
}

// 06334970 (destroy, count and fill inlined). The Infos block stays a raw allocation: retail
// leaves it uninitialised.
int HardpointSummary::analyze_instance(long Instance)
{
    destroy();

    if (Instance != -1)
    {
        count_hardpoints(Instance);

        Names = new char const*[Count];
        Infos = (HardpointInfo*)operator new((size_t)Count * sizeof(HardpointInfo));
        Instances = new long[Count];

        int Index = 0;
        for (Index = 0; Index < Count; ++Index)
        {
            Names[Index] = nullptr;
            Instances[Index] = -1;
        }

        fill_hardpoints(Instance);
    }

    FillCursor = 0;
    FillInstance = -1;
    FillPart = (void*)-1;
    return Count;
}

// 06334a60
void HardpointSummary::expire_instance()
{
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
        Instances[Index] = -1;
}
