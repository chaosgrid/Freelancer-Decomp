// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Engine-instance hierarchy helpers: Hierarchy::GetDepth, IsDescendant, GetRoot, FindChild,
// the CompoundInstance* walkers, hardpoint queries (HardpointPosition[A], HardpointToObject,
// NumHardpoints) and JointEnumCallback.
//
// Engine instance handles are `long` (InstanceIndex, -1 = none).
#include "common_cobject.h"
#include "x86math.h"

#include "DALib.h"
#include "dacom.h"

#include <new>
#include <string.h>

int __cdecl Hierarchy::GetDepth(long ParentInst, long ChildInst)
{
    // FUN_0630e6d0. GetInstanceParent is slot +0xd4; no null test on DALib::Engine.
    int Depth = 0;
    while (ChildInst != -1)
    {
        if (ChildInst == ParentInst)
            return Depth;
        ++Depth;
        ChildInst = DALib::Engine->GetInstanceParent(ChildInst);
    }
    return -1;
}

bool __cdecl IsDescendant(long ParentInst, long ChildInst)
{
    // FUN_0630e710, GetDepth inlined. A child of -1 is never a descendant, even of -1.
    int Depth = 0;
    while (true)
    {
        if (ChildInst == -1)
        {
            Depth = -1;
            break;
        }
        if (ChildInst == ParentInst)
            break;
        ++Depth;
        ChildInst = DALib::Engine->GetInstanceParent(ChildInst);
    }
    return Depth != -1;
}

bool __cdecl HardpointPosition(long Instance, char const* HpName, Vector* OutPos, Matrix* OutOrient)
{
    // FUN_0630ef30. FindHardpoint_OS (FUN_0630e870) searches Instance and its compound
    // children; the result is moved into world space with the found instance's position
    // (Engine +0x90) and orientation (+0x98).
    long FoundOn;
    HardpointInfo Info;
    if (!FindHardpoint_OS(Instance, HpName, FoundOn, Info))
        return false;

    const Vector* const InstancePos = DALib::Engine->GetInstancePosition(FoundOn);
    float const PosX = InstancePos->x;
    float const PosY = InstancePos->y;
    float const PosZ = InstancePos->z;
    Matrix const InstanceOrient =
        *DALib::Engine->GetInstanceOrientation(FoundOn);

    if (OutPos != nullptr)
    {
        Vector Rotated;
        Acquire3DMathEngine()->TransformMatrix(Rotated, InstanceOrient, Info.Position);
        OutPos->x = Rotated.x + PosX;
        OutPos->y = Rotated.y + PosY;
        OutPos->z = Rotated.z + PosZ;
    }
    if (OutOrient != nullptr)
    {
        Matrix Combined;
        Acquire3DMathEngine()->MulMatrix(Combined, InstanceOrient, Info.Orient);
        *OutOrient = Combined;
    }
    return true;
}

// HardpointPositionA is defined after JointEnumCallback, whose list it walks.

// DACOM.DLL!stricmp through the isolated TU common_dacom_stricmp.cpp.
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    // Engine slot 54 (+0xd8) with flag 1: the next child of Instance after Previous (-1 = the
    // first); -1 when there are no more.
    long NextChildInstance(long Instance, long Previous)
    {
        return DALib::Engine->GetRelatedInstance(Instance, 1, Previous);
    }
}

void __cdecl CompoundInstanceAnalyze(long Instance, void (__cdecl* Callback)(long, void*), void* Context)
{
    // FUN_063034f0. Pre-order. No checks of Callback, Instance or DALib::Engine.
    Callback(Instance, Context);
    long Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, Callback, Context);
    } while (Child != -1);
}

// FUN_06303550
static void __cdecl CompoundInstanceCount_AnalyzeCallback(long /*Instance*/, void* Context)
{
    int* const Count = static_cast<int*>(Context);
    ++*Count;
}

int __cdecl CompoundInstanceCount(long Instance)
{
    // FUN_06303560
    int Count = 1;
    long Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, CompoundInstanceCount_AnalyzeCallback, &Count);
    } while (Child != -1);
    return Count;
}

namespace
{
    // FUN_063036d0. Engine slot 26 (+0x68) fills an 8-byte record whose first word is the
    // name; null reads as "" (DAT_063997b0).
    bool __cdecl CompoundInstanceNameMatches(long Instance, char const* Name)
    {
        FEngineName Info = {nullptr, 0};
        FEngineName* const Result = DALib::Engine->GetInstanceName(&Info, Instance);
        char const* Found = Result->Text;
        if (Found == nullptr)
            Found = "";
        return CallDacomStricmp(Found, Name) == 0;
    }

    // CompoundInstanceFindFirstName's context: the match counter on the first walk and the
    // write cursor on the second, the predicate (always FUN_063036d0), and its Name.
    struct FNameScan
    {
        union
        {
            int* MatchCount;
            long* Cursor;
        };
        bool (__cdecl* Predicate)(long, char const*);
        char const* Name;
    };

    // FUN_06303680
    void __cdecl CountMatchingName(long Instance, void* Context)
    {
        FNameScan* const Scan = static_cast<FNameScan*>(Context);
        if (Scan->Predicate(Instance, Scan->Name))
            *Scan->MatchCount += 1;
    }

    // FUN_063036a0
    void __cdecl AppendMatchingName(long Instance, void* Context)
    {
        FNameScan* const Scan = static_cast<FNameScan*>(Context);
        if (!Scan->Predicate(Instance, Scan->Name))
            return;
        *Scan->Cursor = Instance;
        Scan->Cursor += 1;
    }

    // FUN_063035b0
    void __cdecl AppendInstance(long Instance, void* Context)
    {
        long** const Cursor = static_cast<long**>(Context);
        **Cursor = Instance;
        *Cursor += 1;
    }
}

void __cdecl CompoundInstanceList(long Instance, int* OutCount, long** OutArray)
{
    // FUN_063035c0, with CompoundInstanceCount inlined. No null checks of the outputs. The
    // caller frees the array with operator delete, hence the raw allocation.
    int Count = 1;
    long Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, CompoundInstanceCount_AnalyzeCallback, &Count);
    } while (Child != -1);

    *OutCount = Count;
    long* const Array = (long*)::operator new((size_t)Count * sizeof(long));
    *OutArray = Array;
    Array[0] = Instance;

    long* Cursor = Array + 1;
    Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, AppendInstance, &Cursor);
    } while (Child != -1);
}

long __cdecl CompoundInstanceFindFirstName(long Instance, char const* Name)
{
    // FUN_06303720. Counts the matches, allocates, collects them in a second walk and returns
    // the first; with none it returns the child walk's final value (-1). No null check of Name.
    FNameScan Scan;
    int Count = 0;
    Scan.MatchCount = &Count;
    Scan.Predicate = CompoundInstanceNameMatches;
    Scan.Name = Name;
    if (CompoundInstanceNameMatches(Instance, Name))
        ++Count;
    long Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, CountMatchingName, &Scan);
    } while (Child != -1);

    long* const Array = new long[Count];
    FNameScan Fill;
    Fill.Cursor = Array;
    Fill.Predicate = CompoundInstanceNameMatches;
    Fill.Name = Name;
    if (CompoundInstanceNameMatches(Instance, Name))
    {
        *Fill.Cursor = Instance;
        Fill.Cursor += 1;
    }
    Child = -1;
    do
    {
        Child = NextChildInstance(Instance, Child);
        if (Child != -1)
            CompoundInstanceAnalyze(Child, AppendMatchingName, &Fill);
    } while (Child != -1);

    if (Count == 0)
    {
        delete[] Array;
        return Child;
    }
    long const Result = Array[0];
    delete[] Array;
    return Result;
}

// FUN_0630e750 (export ordinal 1439). DFS for the first descendant named Name (CRT _stricmp,
// null name read as ""). Retail quirk kept: Found is tested at the top of the loop, so after a
// successful recursive find one more sibling is fetched before returning. No null test of
// DALib::Engine or the GetInstanceName result.
long __cdecl FindChild(long Instance, char const* Name)
{
    long Found = -1;
    long Child = DALib::Engine->GetRelatedInstance(Instance, 1, InvalidInstanceIndex);
    if (Child != -1)
    {
        do
        {
            if (Found != -1)
                return Found;
            FEngineName Info = {nullptr, 0};
            char const* ChildName = DALib::Engine->GetInstanceName(&Info, Child)->Text;
            if (ChildName == nullptr)
                ChildName = "";
            if (_stricmp(ChildName, Name) == 0)
                return Child;
            Found = FindChild(Child, Name);
            Child = DALib::Engine->GetRelatedInstance(Instance, 1, Child);
        } while (Child != -1);
    }
    return Found;
}

// Slot 53 is GetInstanceParent and slot 32 GetInstanceUserData -- which is where a CObject
// rides on its engine instance.

COMMON_API long __cdecl GetRoot(long Inst)
{
    long ParentInst;
    do
    {
        ParentInst = Inst;
        Inst = DALib::Engine->GetInstanceParent(ParentInst);
    } while (Inst != -1);
    return ParentInst;
}

COMMON_API CObject* __cdecl GetRoot(CObject const* Obj)
{
    long Inst = Obj->EngineInstanceId;
    if (Inst != -1)
    {
        long ParentInst;
        do
        {
            ParentInst = Inst;
            Inst = DALib::Engine->GetInstanceParent(ParentInst);
        } while (Inst != -1);
        return static_cast<CObject*>(DALib::Engine->GetInstanceUserData(ParentInst));
    }
    return const_cast<CObject*>(Obj);
}

COMMON_API void __cdecl HardpointToObject(long Inst, HardpointInfo& Info)
{
    // FUN_0630ec40 (from the listing). The mirror of HardpointToWorld
    // (common_hardpointsummary.cpp): a world-space frame into Inst's object space, through the
    // inverse of Engine +0xa0 (TransposeTransform +0x38, Matrix ctor FUN_06284f70, MulTransform
    // +0x1c). No null checks.
    Transform World;
    World.Rotation = Info.Orient;
    World.Translation = Info.Position;

    Transform const* const InstXform =
        DALib::Engine->GetInstanceTransform(Inst);

    I3DMathEngine* const Math = Acquire3DMathEngine();
    Vector Rotated;
    Math->TransposeTransform(Rotated, InstXform->Rotation, InstXform->Translation);
    Matrix const& R = InstXform->Rotation;
    Transform Inverse;
    Inverse.Rotation = Matrix(R.E[0][0], R.E[1][0], R.E[2][0],
                              R.E[0][1], R.E[1][1], R.E[2][1],
                              R.E[0][2], R.E[1][2], R.E[2][2]);
    Inverse.Translation.x = -Rotated.x;
    Inverse.Translation.y = -Rotated.y;
    Inverse.Translation.z = -Rotated.z;

    Transform Local;
    Acquire3DMathEngine()->MulTransform(Local, Inverse, World);
    Info.Position = Local.Translation;
    Info.Orient = Local.Rotation;
}

namespace
{
    // Retail passes FUN_06303550 (which touches only its second argument) to
    // EnumerateHardpoints; this wrapper has that callback's signature.
    void __cdecl NumHardpoints_CountCallback(char const* /*Name*/, void* Context)
    {
        int* const Count = static_cast<int*>(Context);
        ++*Count;
    }
}

COMMON_API int __cdecl NumHardpoints(long Inst, bool bFlag)
{
    // FUN_0630f070 (from the listing). With bFlag, also every child from GetRelatedInstance
    // with flag 0 (not CompoundInstanceAnalyze's 1). The archetype handle (+0x64) is released
    // at once (+0x3c) but stays a valid lookup key. No null tests on DALib.
    int Count = 0;
    auto const CountHardpointsOn = [&](long Handle)
    {
        const ArchetypeIndex Arch = DALib::Engine->GetInstanceArchetype(Handle);
        DALib::Engine->ReleaseHandle(Arch);
        DALib::Hardpoint->EnumerateHardpoints(&NumHardpoints_CountCallback, Arch, &Count);
    };

    CountHardpointsOn(Inst);

    if (bFlag)
    {
        for (long Child = DALib::Engine->GetRelatedInstance(Inst, 0, InvalidInstanceIndex);
             Child != -1;
             Child = DALib::Engine->GetRelatedInstance(Inst, 0, Child))
        {
            CountHardpointsOn(Child);
        }
    }

    return Count;
}

struct JointInfo;

namespace
{
    // JointEnumCallback (FUN_0630e7f0) is push_back on a VC6 std::list of {Param1, Param2,
    // JointInfo copy} records that HardpointPositionA builds on its stack. JointInfo is never
    // read, so it stays an opaque 0x5c-byte blob (zeroes for a null Joint).
    struct JointEnumNode
    {
        JointEnumNode* Next;            // +0x00
        JointEnumNode* Prev;            // +0x04
        long Param1;                    // +0x08  the parent handle the engine passed
        long Param2;                    // +0x0c  the child handle
        unsigned char JointCopy[0x5c];  // +0x10
    };
    static_assert(sizeof(JointEnumNode) == 0x6c, "JointEnumNode sizeof 0x6c");

    struct JointEnumContext
    {
        unsigned char Allocator;        // +0x00  the list's allocator byte
        JointEnumNode* Head;            // +0x04  sentinel
        int Count;                      // +0x08
    };
} // namespace

COMMON_API bool __cdecl JointEnumCallback(long Param1, long Param2, struct JointInfo const* Joint, void* Context)
{
    unsigned char JointCopy[0x5c];
    if (Joint != nullptr)
        memcpy(JointCopy, Joint, sizeof(JointCopy));
    else
        memset(JointCopy, 0, sizeof(JointCopy));

    JointEnumContext* const Ctx = static_cast<JointEnumContext*>(Context);
    JointEnumNode* const HeadNode = Ctx->Head;
    JointEnumNode* const OldPrev = HeadNode->Prev;
    JointEnumNode* const NewNode = new JointEnumNode;
    NewNode->Next = HeadNode;
    NewNode->Prev = (OldPrev != nullptr) ? OldPrev : NewNode;
    HeadNode->Prev = NewNode;
    NewNode->Prev->Next = NewNode;
    NewNode->Param1 = Param1;
    NewNode->Param2 = Param2;
    memcpy(NewNode->JointCopy, JointCopy, sizeof(JointCopy));

    ++Ctx->Count;
    return true;
}

bool __cdecl HardpointPositionA(long Instance, char const* HpName, Vector* OutPos, Matrix* OutOrient)
{
    // FUN_0630eda0. A compound archetype (Engine +0x4c) recurses into its children (+0x54)
    // in order; otherwise IHardpoint +0xc on Instance. The outputs are archetype-local, with no
    // world transform.
    bool bFound = false;
    if (DALib::Engine->IsCompoundArchetype(Instance) != 0)
    {
        JointEnumContext Children;
        Children.Allocator = (unsigned char)Instance;
        Children.Head = new JointEnumNode;
        Children.Head->Next = Children.Head;
        Children.Head->Prev = Children.Head;
        Children.Count = 0;
        // Retail passes JointEnumCallback itself; its exported signature differs from the
        // engine's callback type only in parameter and return types of the same width.
        DALib::Engine->EnumerateArchetypeChildren(
            Instance, (int(__cdecl*)(ArchetypeIndex, ArchetypeIndex, int, void*))&JointEnumCallback, &Children);

        for (JointEnumNode* Node = Children.Head->Next; Node != Children.Head; Node = Node->Next)
        {
            if (HardpointPositionA(Node->Param2, HpName, OutPos, OutOrient))
            {
                bFound = true;
                break;
            }
        }

        JointEnumNode* Node = Children.Head->Next;
        while (Node != Children.Head)
        {
            JointEnumNode* const NextNode = Node->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            delete Node;
            --Children.Count;
            Node = NextNode;
        }
        delete Children.Head;
        return bFound;
    }

    FHardpointKey Key;
    Key.Name = HpName;
    Key.Crc = DACOM_CRC::GetCRC32(HpName);
    HardpointInfo Info;
    // Retail tests AL only.
    if ((unsigned char)DALib::Hardpoint->GetHardpoint(Instance, &Key, &Info) == 0)
        return false;
    if (OutPos != nullptr)
        *OutPos = Info.Position;
    if (OutOrient != nullptr)
        *OutOrient = Info.Orient;
    return true;
}
