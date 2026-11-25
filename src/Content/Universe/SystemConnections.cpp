// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TU41 system connections: the shortest-path tables (load, build FUN_06f2a400, write) and
// their readers. See SystemConnections.h for the table layout.

#include "Universe/SystemConnections.h"

#include "Core/ContentInternal.h"    // OpenContentDataIni (FUN_06f24280)
#include "Core/ContentPlayerState.h" // IsContentGateLocked (TU53 FUN_06f6ce90)
#include "Core/ContentStaticIds.h"   // TU41's 16 static-init thunks
#include "Universe/SystemInfo.h"         // GSystemInfos, FindSystemInfo
#include "common_server.h"  // pub::System::EnumerateConnections, ConnectionEnumerator
#include "common_tstring.h"     // TString
#include "readfile.h"           // IFileSystem (pub::GetDataPath)

#include <cstdio>
#include <stdlib.h>
#include <vector>

// The three path tables. Retail: static map objects (their init/teardown thunks are the
// TU41 static-initializer entries); filled by FUN_06f29750 (load) or FUN_06f2a400 (build).
FSystemPathTable GSystemPathsAll;   // DAT_06fd07c4
FSystemPathTable GSystemPathsGate;  // DAT_06fd07ac
FSystemPathTable GSystemPathsHole;  // DAT_06fd0794

namespace
{
// The type switch every TU41 reader opens with: 0 -> DAT_06fd07c4, 1 -> DAT_06fd07ac,
// 2 -> DAT_06fd0794, anything else -> no table.
FSystemPathTable* GetSystemPathTable(int Type)
{
    switch (Type)
    {
    case 0: return &GSystemPathsAll;
    case 1: return &GSystemPathsGate;
    case 2: return &GSystemPathsHole;
    default: return nullptr;
    }
}

// The enumerator prologue inlined in every reader (the struct has no constructor).
void ResetConnectionEnumerator(pub::System::ConnectionEnumerator& Enumerator)
{
    for (int Index = 0; Index < 10; ++Index)
    {
        Enumerator.Object[Index].clear();
    }
    Enumerator.Count = 0;
}

// DAT_06fd0818: set by the first FUN_06f2a400 and never cleared.
bool GSystemPathTablesBuilt = false;

// DAT_06fb925c (.rdata 0): the player FUN_06f2a400 / FUN_06f2a0b0 ask FUN_06f6ce90 about.
unsigned int const GAnyPlayerId = 0;

// One search of FUN_06f2a400 for one source system and connection type; retail inlines it
// three times. The only difference between the copies: the type-0 pass tests that the SOURCE
// system has a SystemInfo and on a miss keeps the path queued (unreachable, the sources come
// from the SystemInfo table), the other two test the path's LAST system and drop the path on
// a miss.
void BuildSystemPathsFrom(unsigned int SourceSystemId, int Type, FSystemPathTable* Table)
{
    std::list<std::list<unsigned int>> Queue;
    std::list<unsigned int> Neighbours;
    CollectOpenConnectionSystems(SourceSystemId, &Neighbours, static_cast<ConnectionType>(Type));
    for (std::list<unsigned int>::iterator It = Neighbours.begin(); It != Neighbours.end(); ++It)
    {
        std::list<unsigned int> Path;
        Path.push_back(SourceSystemId);
        Path.push_back(*It);
        Queue.push_back(Path);
    }

    while (!Queue.empty())
    {
        // The first of the shortest queued paths (FUN_06f2a2e0 inlined).
        std::list<unsigned int> Path;
        CopyShortestPath(&Queue, &Path);
        unsigned int const LastSystemId = Path.back();

        unsigned int const CheckedSystemId = (Type == 0) ? SourceSystemId : LastSystemId;
        if (FindSystemInfo(&CheckedSystemId) == nullptr)
        {
            if (Type != 0)
                Queue.remove(Path);
            continue;
        }

        // Table[source] exists, and holds source -> {source}; both are unique inserts. The
        // empty map is a local that lives to the end of the iteration (EH map 06fc3610).
        FSystemPathsFrom EmptyPaths;
        Table->insert(FSystemPathTable::value_type(SourceSystemId, EmptyPaths));
        FSystemPathTable::iterator From = Table->find(SourceSystemId);
        if (From != Table->end())
        {
            std::list<unsigned int> Self;
            Self.push_back(SourceSystemId);
            From->second.insert(FSystemPathsFrom::value_type(SourceSystemId, Self));
        }

        // Every queued copy of this path is dropped.
        Queue.remove(Path);

        // A known path of the same length or shorter wins.
        int const KnownLength = GetSystemPathLength(SourceSystemId, LastSystemId, Type);
        if (KnownLength >= 0 && KnownLength <= static_cast<int>(Path.size()))
            continue;

        // The extension list is built before the record step (EH order).
        std::list<unsigned int> Next;

        // Record the path (insert, or assign over the known one).
        From = Table->find(SourceSystemId);
        if (From != Table->end())
        {
            FSystemPathsFrom::iterator To = From->second.find(LastSystemId);
            if (To == From->second.end())
            {
                // Retail first inserts {Last, P} into a throw-away map, then into the inner
                // map.
                FSystemPathsFrom Scratch;
                Scratch.insert(FSystemPathsFrom::value_type(LastSystemId, Path));
                From->second.insert(FSystemPathsFrom::value_type(LastSystemId, Path));
            }
            else
            {
                To->second = Path;
            }
        }

        // Every open connection of the last system extends the path.
        CollectOpenConnectionSystems(LastSystemId, &Next, static_cast<ConnectionType>(Type));
        for (std::list<unsigned int>::iterator It = Next.begin(); It != Next.end(); ++It)
        {
            std::list<unsigned int> Longer(Path);
            Longer.push_back(*It);
            Queue.push_back(Longer);
        }
    }
}

// The body of one file of FUN_06f2bf30 (inlined three times). Retail prints the head node's value for a system without a
// universe.ini nickname; the port's find() == end() dereference is equally undefined (every
// path system comes from universe.ini). bWritePathSystems false drops the ", %s" path
// systems: the second file's stream mix-up (see WriteSystemPathTables).
void WriteSystemPathFile(FILE* File, bool bWritePathSystems, FSystemPathTable const& Table,
                         std::map<unsigned int, TString<0x10>> const& Names)
{
    for (FSystemPathTable::const_iterator From = Table.begin(); From != Table.end(); ++From)
    {
        fprintf(File, "[SystemConnections]\n\n");
        for (FSystemPathsFrom::const_iterator To = From->second.begin(); To != From->second.end(); ++To)
        {
            fprintf(File, "Path = %s, %s", Names.find(From->first)->second.Data,
                    Names.find(To->first)->second.Data);
            if (bWritePathSystems)
            {
                for (std::list<unsigned int>::const_iterator It = To->second.begin();
                     It != To->second.end(); ++It)
                    fprintf(File, ", %s", Names.find(*It)->second.Data);
            }
            fprintf(File, "\n");
        }
        fprintf(File, "\n");
    }
}
} // namespace

// FUN_06f29b20.
int GetSystemPathLength(unsigned int FromSystemId, unsigned int ToSystemId, int Type)
{
    FSystemPathTable* Table = GetSystemPathTable(Type);
    if (Table == nullptr)
    {
        return -1;
    }
    FSystemPathTable::iterator Outer = Table->find(FromSystemId);
    if (Outer == Table->end())
    {
        return -1;
    }
    FSystemPathsFrom::iterator Inner = Outer->second.find(ToSystemId);
    if (Inner == Outer->second.end())
    {
        return -1;
    }
    return static_cast<int>(Inner->second.size());
}

// FUN_06f29c10.
unsigned int GetNextHopConnectionObject(unsigned int FromSystemId, unsigned int ToSystemId, int Type)
{
    std::list<unsigned int> Path = GetSystemPath(FromSystemId, ToSystemId, Type);

    unsigned int NextSystemId = 0;
    if (Path.size() != 0)
    {
        std::list<unsigned int>::iterator Second = Path.begin();
        ++Second;
        if (Second != Path.end())
        {
            NextSystemId = *Second;
        }
    }

    unsigned int Result = 0;
    unsigned int SystemId = FromSystemId;
    if ((SystemId & GIdValueMask) != 0 && (NextSystemId & GIdValueMask) != 0)
    {
        pub::System::ConnectionEnumerator Enumerator;
        ResetConnectionEnumerator(Enumerator);
        if (pub::System::EnumerateConnections(SystemId, Enumerator, static_cast<ConnectionType>(Type)) == 0)
        {
            for (int Index = 0; Index < static_cast<int>(Enumerator.Count); ++Index)
            {
                if (Enumerator.GotoSystemId[Index] == NextSystemId)
                {
                    Result = Enumerator.ObjectId[Index];
                    break;
                }
            }
        }
    }
    return Result;
}

// FUN_06f29d80 (returns the list through the hidden pointer).
std::list<unsigned int> GetSystemPath(unsigned int FromSystemId, unsigned int ToSystemId, int Type)
{
    std::list<unsigned int> Path;
    FSystemPathTable* Table = GetSystemPathTable(Type);
    if (Table != nullptr)
    {
        FSystemPathTable::iterator Outer = Table->find(FromSystemId);
        if (Outer != Table->end())
        {
            FSystemPathsFrom::iterator Inner = Outer->second.find(ToSystemId);
            if (Inner != Outer->second.end())
            {
                Path = Inner->second;
            }
        }
    }
    return Path;
}

// FUN_06f2a190.
int GetConnectionPosition(unsigned int SystemId, unsigned int ObjectId, Vector* OutPosition)
{
    pub::System::ConnectionEnumerator Enumerator;
    ResetConnectionEnumerator(Enumerator);
    if (pub::System::EnumerateConnections(SystemId, Enumerator, CONN_TYPE_ALL) == 0)
    {
        for (int Index = 0; Index < static_cast<int>(Enumerator.Count); ++Index)
        {
            if (Enumerator.ObjectId[Index] == ObjectId)
            {
                *OutPosition = Enumerator.Pos[Index];
                return 0;
            }
        }
    }
    return -2;
}

// FUN_06f2a240.
int GetConnectionGotoSystem(unsigned int SystemId, unsigned int ObjectId, unsigned int* OutGotoSystemId)
{
    pub::System::ConnectionEnumerator Enumerator;
    ResetConnectionEnumerator(Enumerator);
    if (pub::System::EnumerateConnections(SystemId, Enumerator, CONN_TYPE_ALL) == 0)
    {
        for (int Index = 0; Index < static_cast<int>(Enumerator.Count); ++Index)
        {
            if (Enumerator.ObjectId[Index] == ObjectId)
            {
                *OutGotoSystemId = Enumerator.GotoSystemId[Index];
                return 0;
            }
        }
    }
    return -2;
}

// FUN_06f29320. Every value line -- "Path" or not -- inserts
// {To, the systems read so far} into the section's map (a unique insert) and empties the
// list; a line other than "Path" re-inserts the previous To (no-op) or {0, empty} first.
// The section's map goes into Table under the last From, unless From & 0x3fffffff is 0.
void __cdecl LoadSystemPathSection(INI_Reader* Reader, FSystemPathTable* Table)
{
    unsigned int ToSystemId = 0;
    unsigned int FromSystemId = 0;
    unsigned int PathSystemId = 0;      // never initialised in retail, kept across lines
    std::list<unsigned int> Path;
    FSystemPathsFrom Paths;
    while (Reader->read_value())
    {
        if (Reader->is_value("Path"))
        {
            // Value 0 = from, value 1 = to, values 2.. = the path.
            pub::GetSystemID(FromSystemId, Reader->get_value_string(0));
            pub::GetSystemID(ToSystemId, Reader->get_value_string(1));
            for (unsigned int Index = 2; !Reader->is_value_empty(Index); ++Index)
            {
                pub::GetSystemID(PathSystemId, Reader->get_value_string(Index));
                Path.push_back(PathSystemId);
            }
        }
        Paths.insert(FSystemPathsFrom::value_type(ToSystemId, Path));
        Path.clear();
    }
    if ((FromSystemId & GIdValueMask) != 0)
        Table->insert(FSystemPathTable::value_type(FromSystemId, Paths));
}

// FUN_06f29750. The three readers are constructed in this order and destroyed together at the
// end.
void __cdecl LoadSystemPathTables()
{
    GSystemPathsAll.clear();
    GSystemPathsGate.clear();
    GSystemPathsHole.clear();

    INI_Reader AllReader;
    OpenContentDataIni("Universe\\systems_shortest_path.ini", &AllReader);   // result dropped
    while (AllReader.read_header())
    {
        if (AllReader.is_header("SystemConnections"))
            LoadSystemPathSection(&AllReader, &GSystemPathsAll);
    }
    AllReader.close();

    INI_Reader GateReader;
    OpenContentDataIni("Universe\\shortest_legal_path.ini", &GateReader);
    while (GateReader.read_header())
    {
        if (GateReader.is_header("SystemConnections"))
            LoadSystemPathSection(&GateReader, &GSystemPathsGate);
    }
    GateReader.close();

    INI_Reader HoleReader;
    OpenContentDataIni("Universe\\shortest_illegal_path.ini", &HoleReader);
    while (HoleReader.read_header())
    {
        if (HoleReader.is_header("SystemConnections"))
            LoadSystemPathSection(&HoleReader, &GSystemPathsHole);
    }
    HoleReader.close();
}

// FUN_06f29a90: the existence test keeps the file open until after the load.
void __cdecl InitSystemPathTables()
{
    char DataDir[0x104];
    char FullPath[0x104];
    IFileSystem* DataFileSystem = pub::GetDataPath();
    DataFileSystem->GetFileSysPath(DataDir, 0x104);                         // vtbl[+0x84]
    _makepath(FullPath, nullptr, DataDir, "Universe\\systems_shortest_path.ini", nullptr);
    FILE* File = fopen(FullPath, "r");
    if (File == nullptr)
    {
        BuildSystemPathTables(true);
        return;
    }
    LoadSystemPathTables();
    fclose(File);
}

// FUN_06f2a0b0.
void __cdecl CollectOpenConnectionSystems(unsigned int SystemId, std::list<unsigned int>* Out,
                                          ConnectionType Type)
{
    pub::System::ConnectionEnumerator Enumerator;
    ResetConnectionEnumerator(Enumerator);
    if (pub::System::EnumerateConnections(SystemId, Enumerator, Type) != 0)
        return;
    for (int Index = 0; Index < static_cast<int>(Enumerator.Count); ++Index)
    {
        // The goto system is appended unless the object is locked.
        unsigned int const GotoSystemId = Enumerator.GotoSystemId[Index];
        if (!IsContentGateLocked(&GAnyPlayerId, &Enumerator.ObjectId[Index]))
            Out->push_back(GotoSystemId);
    }
}

// FUN_06f2a2e0: a later path of equal length does not replace the first.
int __cdecl CopyShortestPath(std::list<std::list<unsigned int>>* Paths, std::list<unsigned int>* Out)
{
    int BestLength = -1;
    for (std::list<std::list<unsigned int>>::iterator It = Paths->begin(); It != Paths->end(); ++It)
    {
        int const Length = static_cast<int>(It->size());
        if (BestLength != -1 && BestLength <= Length)
            continue;
        if (Out != &*It)
            *Out = *It;
        BestLength = Length;
    }
    return BestLength;
}

// FUN_06f2a400. Retail iterates a full copy of the SystemInfo table (FUN_06f75f20 +
// FUN_06f2ccb0) and reads only its keys. The port holds the same copy.
void __cdecl BuildSystemPathTables(bool bWrite)
{
    if (GSystemPathTablesBuilt)
        return;
    GSystemPathsHole.clear();
    GSystemPathsGate.clear();
    GSystemPathsAll.clear();
    GSystemPathTablesBuilt = true;

    std::map<unsigned int, FSystemInfo> const Systems(GSystemInfos);

    for (std::map<unsigned int, FSystemInfo>::const_iterator It = Systems.begin(); It != Systems.end(); ++It)
        BuildSystemPathsFrom(It->first, 0, &GSystemPathsAll);
    for (std::map<unsigned int, FSystemInfo>::const_iterator It = Systems.begin(); It != Systems.end(); ++It)
        BuildSystemPathsFrom(It->first, 1, &GSystemPathsGate);
    for (std::map<unsigned int, FSystemInfo>::const_iterator It = Systems.begin(); It != Systems.end(); ++It)
        BuildSystemPathsFrom(It->first, 2, &GSystemPathsHole);

    if (bWrite)
        WriteSystemPathTables();
}

// FUN_06f2bf30: nicknames of universe.ini [System] sections
// (TString<0x10> = FUN_06f2db50, at most 15 chars; unique insert per id), then the three
// files with their paths hardcoded relative to the working directory.
void __cdecl WriteSystemPathTables()
{
    std::map<unsigned int, TString<0x10>> Names;
    INI_Reader Reader;
    OpenContentDataIni("Universe\\universe.ini", &Reader);
    unsigned int SystemId = 0;          // not initialised in retail
    while (Reader.read_header())
    {
        if (!Reader.is_header("System"))
            continue;
        while (Reader.read_value())
        {
            if (!Reader.is_value("nickname"))
                continue;
            TString<0x10> Nickname;
            Nickname = Reader.get_value_string(0);
            pub::GetSystemID(SystemId, Nickname.Data);
            Names.insert(std::map<unsigned int, TString<0x10>>::value_type(SystemId, Nickname));
        }
    }
    Reader.close();

    FILE* AllFile = fopen("..\\data\\Universe\\systems_shortest_path.ini", "wt");
    bool const bAllFileOpened = AllFile != nullptr;
    if (AllFile != nullptr)
    {
        WriteSystemPathFile(AllFile, true, GSystemPathsAll, Names);
        fclose(AllFile);
    }

    // Retail bug: the second file's ", %s" lines go through the FIRST file's closed FILE*.
    // VC6 msvcrt's fopen takes the first free _iob[] slot and nothing opens a stream in
    // between, so the second fopen gets file 1's slot back and the lines land in the second
    // file, where they belong. When the first fopen failed, retail writes to a null stream; the
    // port drops those lines and never touches the closed stream.
    FILE* GateFile = fopen("..\\data\\Universe\\shortest_legal_path.ini", "wt");
    if (GateFile != nullptr)
    {
        WriteSystemPathFile(GateFile, bAllFileOpened, GSystemPathsGate, Names);
        fclose(GateFile);
    }

    FILE* HoleFile = fopen("..\\data\\Universe\\shortest_illegal_path.ini", "wt");
    if (HoleFile != nullptr)
    {
        WriteSystemPathFile(HoleFile, true, GSystemPathsHole, Names);
        fclose(HoleFile);
    }
}
