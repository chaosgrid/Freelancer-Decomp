// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_newchar.h"

#include "common_misc.h"

#include <cstdio>
#include <cstring>
#include <new>

extern "C"
{
    __declspec(dllimport) extern void(__cdecl* FDUMP)(int flags, const char* fmt, ...);
}

namespace
{
    const char kStateGraphSource[] = "E:\\FL\\Scratch\\Source\\Common\\Ai\\pubStateGraph.cpp";
    const unsigned kStateGraphStride = 0x72c;

    int GStateGraphCount = 0;
    IStateGraph* GStateGraphs = nullptr;

    // LAB_062df410 (static-initializer table, .data:063ea3b8) zeroes the count and the array
    // pointer and registers LAB_062df430 through atexit: at module teardown the array is
    // released and the pointer cleared; the count is left. A file-static object with that
    // destructor is the same registration.
    struct FStateGraphStorageTeardown
    {
        ~FStateGraphStorageTeardown()
        {
            if (GStateGraphs != nullptr)
            {
                ::operator delete(GStateGraphs);
                GStateGraphs = nullptr;
            }
        }
    };
    FStateGraphStorageTeardown GStateGraphStorageTeardown;

    static_assert(sizeof(IStateGraph) == kStateGraphStride, "IStateGraph stride 0x72c");
}

int CallDacomStricmp(const char* Left, const char* Right);

void __cdecl pub::StateGraph::Free()
{
    // FUN_062df7a0 (inlined at 0x062df430 / 0x062df580). The count is left as it was.
    if (GStateGraphs != nullptr)
    {
        ::operator delete(GStateGraphs);
        GStateGraphs = nullptr;
    }
}

void __cdecl pub::StateGraph::LoadFromFile(char const* Path)
{
    // FUN_062df5d0
    FILE* File = fopen(Path, "r");
    if (File == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kStateGraphSource, 0x7d,
              "pub::StateGraph: unable to load state graph definitions");
        return;
    }

    int GraphCount = 0;
    int BehaviorCount = 0;
    fscanf(File, "%*s%d", &GraphCount);
    fscanf(File, "%*s%d", &BehaviorCount);
    if (BehaviorCount != kStateGraphBehaviors)
    {
        // Retail leaks the FILE here, and so does the port.
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kStateGraphSource, 0x8d,
              "pub::StateGraph: state graph definitions don't sync with current behavior list, need to re-save state_graph.ini");
        return;
    }

    // FUN_062df740: 32-bit product. The previous array is not freed here (refresh_state_graph
    // frees it first; DataConfig::init at .text:06309470 does not). VC6 operator new answers
    // null on failure. Only weights and id are initialised; +0x04..+0x47 are left as
    // allocated.
    GStateGraphCount = GraphCount;
    unsigned const Bytes = (unsigned)GraphCount * kStateGraphStride;
    GStateGraphs = static_cast<IStateGraph*>(::operator new(Bytes, std::nothrow));
    if (GStateGraphs != nullptr)
    {
        int InitIndex = 0;
        for (; InitIndex < GraphCount; ++InitIndex)
        {
            memset(GStateGraphs[InitIndex].Weights, 0, sizeof(GStateGraphs[InitIndex].Weights));
            GStateGraphs[InitIndex].Id = -1;
        }
    }
    else if (GraphCount > 0)
    {
        // Deviation: retail goes on and writes through the null array; the port stops reading
        // and drops the count, so the lookups (which, like retail, do not test the pointer)
        // find nothing.
        GStateGraphCount = 0;
        fclose(File);
        return;
    }

    int GraphIndex = 0;
    for (; GraphIndex < GraphCount; ++GraphIndex)
    {
        IStateGraph& Graph = GStateGraphs[GraphIndex];

        // Deviation: retail scans the name straight into +0x08 and the type into a 0x100-byte
        // stack buffer with an unbounded "%s", so a longer token overruns. The port bounds both
        // and truncates the name to the field; identical for names up to 0x3f characters.
        char NameBuf[0x100];
        char TypeBuf[0x100];
        NameBuf[0] = '\0';
        TypeBuf[0] = '\0';
        fscanf(File, "%255s", NameBuf);
        fscanf(File, "%255s", TypeBuf);
        strncpy(Graph.Name, NameBuf, kStateGraphNameChars - 1);
        Graph.Name[kStateGraphNameChars - 1] = '\0';

        Graph.Type = (strstr(TypeBuf, "ESCORT") != nullptr) ? ESCORT : LEADER;

        int Row = 0;
        for (; Row < kStateGraphBehaviors; ++Row)
        {
            int Col = 0;
            for (; Col < kStateGraphBehaviors; ++Col)
                fscanf(File, "%f", &Graph.Weights[Row][Col]);
        }

        Graph.Id = GraphIndex;
    }

    fclose(File);
}

void __cdecl pub::StateGraph::refresh_state_graph()
{
    char Path[0x104];
    Free();
    GetFileSysPath(Path, "AI\\state_graph.db", nullptr);
    LoadFromFile(Path);
}

int __cdecl pub::StateGraph::get_state_graph(char const* Name, Type GraphType)
{
    // FUN_062df450; no pointer test. The first match answers its id.
    int Index = 0;
    for (; Index < GStateGraphCount; ++Index)
    {
        if (CallDacomStricmp(GStateGraphs[Index].Name, Name) != 0)
            continue;
        if (GStateGraphs[Index].Type != GraphType)
            continue;
        return GStateGraphs[Index].Id;
    }
    return -1;
}

int __cdecl pub::StateGraph::get_state_graph(int GraphId, Type GraphType)
{
    // FUN_062df4c0: as above with the name of record GraphId. Count <= 0 answers -1 before
    // the record is touched. Retail quirk kept: GraphId is not range-checked, so an id outside
    // [0, count) reads past the array.
    if (GStateGraphCount <= 0)
        return -1;

    char const* Name = GStateGraphs[GraphId].Name;
    int Index = 0;
    for (; Index < GStateGraphCount; ++Index)
    {
        if (CallDacomStricmp(GStateGraphs[Index].Name, Name) != 0)
            continue;
        if (GStateGraphs[Index].Type != GraphType)
            continue;
        return GStateGraphs[Index].Id;
    }
    return -1;
}

IStateGraph const* __cdecl pub::StateGraph::get_state_graph_internal(int GraphId)
{
    // FUN_062df540; no pointer test.
    int Index = 0;
    for (; Index < GStateGraphCount; ++Index)
    {
        if (GStateGraphs[Index].Id == GraphId)
            return &GStateGraphs[Index];
    }
    return nullptr;
}
