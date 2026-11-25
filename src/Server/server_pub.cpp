// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_pathfind.h" // PathfindManager (pub::AI::SubmitState)
#include "../include/common_id.h"
#include "../include/common_goods.h"
#include "../include/common_universe.h"
#include "../include/common_archetype.h"
#include "../include/common_reputation.h"
#include "../include/common_iface.h"
#include "../include/common_cobjdb.h"
#include "../include/common_csys.h"
#include "../include/common_cobject.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_exports.h"
#include "../include/server_basedb.h"
#include "../include/server_cobject.h"
#include "../include/common_physys.h"
#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"
#include "../include/common_misc.h"
#include "../include/common_net.h"      // IsMPServer (AdjustCash's gate)
#include "../include/common_newchar.h"   // RtcSlider::get_slider (pub::GF::GetRtcPerformanceSlider)
#include "../include/DALib.h"            // DALib::Engine (pub::FindHardpoint releases the archetype handle)
#include "../include/readfile.h"         // IFileSystem::GetFileSysPath (+0x84, pub::BuildBaseReader)
#include "../include/x86math.h"      // I3DMathEngine::MatrixToQuaternion (pub::Player::SetCamera)
#include "../include/server_createinfo.h" // Server_Acquire3DMathEngine (FUN_06d5ff70)
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FPayloadServerSetDirective (pub::AI::SubmitDirective)
#include "../include/server_savefile.h"   // SaveGame_Writer / ISave / Server_FLS1Transform
#include "../include/server_savegame.h"   // pub::Save -> PlayerData::auto_save / PlayerData::mission_save
#include "../include/common_casteroid.h"
#include <windows.h>
#include <cstdint>
#include <new>
#include <cmath>
#include <cstdio>
#include <cstring>     // std::memcpy (SpaceObj::Create EquipIdMaker seed)
#include <direct.h>
#include <io.h>        // _unlink (the closeout at .text:06d4dfc7 / .text:06d4e16d)
#include <map>
#include <set>
#include <stdexcept>   // std::out_of_range (Server_VibeID_Free, FUN_06d15f90)
#include <vector>
#include <list>
#include <string>      // Server_AccountDirName's lowercased wide copy
#include <cwchar>      // wcslen / wcstombs / swprintf
#include <cwctype>     // towlower (retail _wcslwr)
#include <cstdlib>     // rand, wcstombs
#include <ctime>       // _strdate_s / _strtime_s (retail _strdate / _strtime)

namespace GlobalReputation = ::Reputation;

// The IClient the server talks back to (DAT_06d8fb14). Defined in IServerImpl.cpp and set
// by IServerImpl::Startup; several pub:: exports dispatch straight into its vtable.
extern IClient* g_pClientInterface;

// Object lookup over Server_FindObject (FUN_06d006c0): its one-entry cache, the cached
// system's blocks, the in-transit list, then every system in SysDB::SysMap. An object is
// found only once it is in a StarSystem block. Returns the GameObject wrapper:
// pub::SpaceObj::GetCenterOfMass (.text:06cf9069) reads CBase::Object at +0x0c+4 of it.
GameObject* Server_LookupGameObject(unsigned int ObjId) {
    GameObject* Obj = nullptr;
    StarSystem* Sys = nullptr;
    Server_FindObject(ObjId, Obj, Sys);
    return Obj;
}

// The guard every pub::Player export opens with (e.g. SetMsnID .text:06d56461):
// id != 0, id-1 < Players.MaxPlayers (DAT_06d90264), and a non-zero client id at
// PlayerData+0x348 (stride 0x418). Retail has no "active" flag; occupancy is the client id.
static bool IsValidPlayer(unsigned int player_id) {
    if (player_id == 0 || (player_id - 1) >= PlayerDB_GetMaxPlayers()) return false;
    return Players[player_id].client_id != 0;
}

// Forward declarations for functions defined in IServerImpl.cpp
unsigned int Server_ControllerCreate(const char* dll_path, const char* instance_name, void* parms, unsigned int priority);
void Server_ControllerDestroy(unsigned int flid);

// Game time accumulator defined in IServerImpl.cpp (updated by ElapseTime)
extern double g_ElapsedTimeAccum;   // DAT_06d8fb28, IServerImpl.cpp
extern double GServerTimeBase;      // DAT_06d8fb20, IServerImpl.cpp
extern char GDllPath[0x104];        // DAT_06d8f654, IServerImpl.cpp (pub::DLL_LoadLibrary)

// ===========================================================================
// pub global functions -- .text:06d5e440..06d5f033 (0x7e440-0x7f033)
// ===========================================================================
namespace pub {

// pub::IsCommodity -- retail 0x7e440 (ordinal 311). Category is GoodInfo+0x4c.
SERVER_API int __cdecl IsCommodity(const unsigned int& GoodId, bool& OutCommodity) {
    GoodInfo const* Info = GoodList::find_by_id(GoodId);
    if (Info == nullptr)
        return -4;
    OutCommodity = (Info->Category == GoodCategoryCommodity);
    return 0;
}

// pub::GetCargoHoldSize -- retail 0x7e480 (ordinal 161). HoldSize is Ship+0x100 (ftol).
SERVER_API int __cdecl GetCargoHoldSize(const unsigned int& ShipArchId, unsigned int& OutSize) {
    OutSize = 0;
    if ((ShipArchId & 0x3fffffff) == 0)
        return -4;
    Archetype::Ship* ShipArch = Archetype::GetShip(ShipArchId);
    if (ShipArch == nullptr)
        return -2;
    OutSize = static_cast<unsigned int>(static_cast<long long>(ShipArch->HoldSize));
    return 0;
}

// pub::GetLoadoutName -- retail 0x7e4d0 (ordinal 203). Retail quirks kept: it rejects an
// output buffer whose first byte is already 0, and strncpy adds no forced terminator.
SERVER_API int __cdecl GetLoadoutName(const unsigned int& LoadoutId, char* OutBuffer, int MaxLen) {
    if (OutBuffer == nullptr || OutBuffer[0] == '\0')
        return -2;
    Loadout::Map const* LoadoutMap = Loadout::Get(LoadoutId);
    if (LoadoutMap == nullptr)
        return -2;
    ID_String Name;
    Name.Id = LoadoutMap->NicknameId;
    char const* Str = Name.get_string();
    if (Str == nullptr)
        return -2;
    strncpy(OutBuffer, Str, static_cast<size_t>(MaxLen));
    return 0;
}

// pub::GetLoadoutID -- retail 0x7e520 (ordinal 202).
SERVER_API int __cdecl GetLoadoutID(unsigned int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    unsigned int Id = Loadout::GetID(Nickname);
    if (Id == 0)
        return -2;
    OutId = Id;
    return 0;
}

// pub::GetEquipmentID -- retail 0x7e740 (ordinal 181).
SERVER_API int __cdecl GetEquipmentID(unsigned int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    ID_String Name;
    Name.Id = CreateID(Nickname);
    unsigned int Id = Archetype::GetEquipmentID(Name);
    OutId = Id;
    return ((Id & 0x3fffffff) != 0) ? 0 : -2;
}

// pub::GetGoodID -- retail 0x7e790 (ordinal 185). Id is GoodInfo+0x48.
SERVER_API int __cdecl GetGoodID(unsigned int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    GoodInfo const* Info = GoodList::find_by_nickname(Nickname);
    if (Info == nullptr)
        return -2;
    unsigned int Id = Info->Id;
    OutId = Id;
    return ((Id & 0x3fffffff) != 0) ? 0 : -2;
}

// pub::TranslateArchToGood -- retail 0x7e7d0 (ordinal 465). The zero test is unmasked.
SERVER_API int __cdecl TranslateArchToGood(const unsigned int& ArchId, unsigned int& OutGoodId) {
    if (ArchId == 0)
        return -4;
    unsigned int GoodId = Arch2Good(ArchId);
    OutGoodId = GoodId;
    return ((GoodId & 0x3fffffff) != 0) ? 0 : -2;
}

// pub::GetVoiceID -- retail 0x7e800 (ordinal 296).
SERVER_API int __cdecl GetVoiceID(unsigned int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    OutId = CreateID(Nickname);
    return 0;
}

// pub::GetCostumeID -- retail 0x7e830 (ordinal 175). OutId is written on a miss too.
SERVER_API int __cdecl GetCostumeID(int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    costume const* Found = GetCostumeDescriptions()->find_costume(Nickname);
    int Id = 0;
    if (Found != nullptr)
        Id = static_cast<int>(Found->Id);
    OutId = Id;
    return (Id != 0) ? 0 : -2;
}

// pub::GetDataPath -- retail 0x7e880 (ordinal 177).
SERVER_API IFileSystem* __cdecl GetDataPath() {
    return DALib::Data;
}

SERVER_API int __cdecl GetTime(double& OutTime) {
    // Retail 0x7e890; the sum is FUN_06d447b0.
    OutTime = (g_ElapsedTimeAccum + GServerTimeBase) * DaysPerSecond;
    return 0;
}

SERVER_API int __cdecl GetRwTime(double& OutTime) {
    // Retail 0x7e8b0: raw seconds, no DaysPerSecond scale.
    OutTime = g_ElapsedTimeAccum + GServerTimeBase;
    return 0;
}

// pub::NextSystemID -- retail 0x7e8c0 (ordinal 346). Retail does not resume Common's
// iterator: it restarts at GetFirstSystem and steps past the system matching InOutId.
SERVER_API bool __cdecl NextSystemID(unsigned int& InOutId) {
    Universe::ISystem const* Sys = Universe::GetFirstSystem();
    if (InOutId != 0) {
        while (Sys != nullptr) {
            bool const bMatch = (Sys->NicknameId == InOutId);
            Sys = Universe::GetNextSystem();
            if (bMatch)
                break;
        }
    }
    if (Sys == nullptr)
        return false;
    InOutId = Sys->NicknameId;
    return true;
}

// pub::NextBaseID -- retail 0x7e910 (ordinal 345). The NextSystemID walk over bases.
SERVER_API bool __cdecl NextBaseID(unsigned int& InOutId) {
    Universe::IBase const* Base = Universe::GetFirstBase();
    if (InOutId != 0) {
        while (Base != nullptr) {
            bool const bMatch = (Base->NicknameId == InOutId);
            Base = Universe::GetNextBase();
            if (bMatch)
                break;
        }
    }
    if (Base == nullptr)
        return false;
    InOutId = Base->NicknameId;
    return true;
}

// pub::GetSystemID -- retail 0x7e960 (ordinal 287).
SERVER_API int __cdecl GetSystemID(unsigned int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    unsigned int Id = Universe::get_system_id(Nickname);
    if ((Id & 0x3fffffff) == 0)
        return -2;
    OutId = Id;
    return 0;
}

SERVER_API int __cdecl GetSystemGateConnection(const unsigned int& GateId, unsigned int& OutConnectedSystemId) {
    // Retail 0x7e9a0.
    unsigned int SysId = Universe::get_gate_system(GateId);
    OutConnectedSystemId = SysId;
    if ((SysId & 0x3fffffff) != 0) {
        return 0;
    }
    return -2;
}

// pub::GetBaseID -- retail 0x7e9d0 (ordinal 153). An unknown base returns -4, not -2.
SERVER_API int __cdecl GetBaseID(unsigned int& OutId, const char* Nickname) {
    OutId = 0;
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    unsigned int Id = Universe::get_base_id(Nickname);
    if ((Id & 0x3fffffff) == 0)
        return -4;
    OutId = Id;
    return 0;
}

// pub::BuildSystemReader -- retail 0x7ea10 (ordinal 68).
SERVER_API int __cdecl BuildSystemReader(INI_Reader& Reader, const unsigned int& SystemId) {
    Reader.close();
    Universe::ISystem const* Sys = Universe::get_system(SystemId);
    if (Sys == nullptr)
        return -2;
    char Path[0x104];
    Universe::get_filename(Path, Sys->File);
    if (Reader.open(Path, false))
        return 0;
    FDUMP(0x100001, "%s(%d) : *** ERROR: missing System file '%s'",
          "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x2b3, Path);
    return -2;
}

// pub::BuildBaseReader -- retail 0x7eab0 (ordinal 66). Unlike BuildSystemReader, no FDUMP
// on a missing file.
SERVER_API int __cdecl BuildBaseReader(INI_Reader& Reader, const unsigned int& BaseId) {
    Reader.close();
    Universe::IBase const* Base = Universe::get_base(BaseId);
    if (Base == nullptr)
        return -2;
    char DataDir[0x104];
    DALib::Data->GetFileSysPath(DataDir, 0x104);
    char Path[0x104];
    _makepath(Path, nullptr, DataDir, Base->File, nullptr);
    if (Reader.open(Path, false))
        return 0;
    return -2;
}

// pub::GetSystemNickname -- retail 0x7eb60 (ordinal 288). Returns the needed size when
// the buffer is too small, 0 on success and on a miss.
SERVER_API unsigned int __cdecl GetSystemNickname(char* OutBuffer, unsigned int MaxLen, const unsigned int& SystemId) {
    Universe::ISystem const* Sys = Universe::get_system(SystemId);
    if (Sys == nullptr)
        return 0;
    unsigned int Needed = static_cast<unsigned int>(strlen(Sys->Nickname)) + 1;
    if (MaxLen < Needed)
        return Needed;
    memcpy(OutBuffer, Sys->Nickname, Needed);
    return 0;
}

// pub::GetBaseNickname -- retail 0x7ebc0 (ordinal 154). As GetSystemNickname.
SERVER_API unsigned int __cdecl GetBaseNickname(char* OutBuffer, unsigned int MaxLen, const unsigned int& BaseId) {
    Universe::IBase const* Base = Universe::get_base(BaseId);
    if (Base == nullptr)
        return 0;
    unsigned int Needed = static_cast<unsigned int>(strlen(Base->Nickname)) + 1;
    if (Needed > MaxLen)
        return Needed;
    memcpy(OutBuffer, Base->Nickname, Needed);
    return 0;
}

// pub::GetBaseStridName -- retail 0x7ec20 (ordinal 156).
SERVER_API int __cdecl GetBaseStridName(unsigned int& OutStrid, const unsigned int& BaseId) {
    Universe::IBase const* base = Universe::get_base(BaseId);
    if (base) {
        OutStrid = base->StridName;
        return 0;
    }
    return -2;
}

// pub::GetLocations -- retail 0x7ec50 (ordinal 207). Tries all seven fixed room names;
// the loop does not stop early when the output is full.
SERVER_API int __cdecl GetLocations(const unsigned int& BaseId, unsigned int* const OutLocations, unsigned int MaxCount, unsigned int& OutCount) {
    static char const* const RoomNames[7] = {
        "Bar", "Deck", "Trader", "Equipment", "Cityscape", "ShipDealer", "Planetside"
    };
    unsigned int Count = 0;
    if (BaseDataList_get()->get_base_data(BaseId) == nullptr) {
        OutCount = Count;
        return 0;
    }
    for (unsigned int Index = 0; Index < 7; ++Index) {
        unsigned int RoomId = MakeLocationID(BaseId, RoomNames[Index]);
        if (BaseDataList_get()->get_room_data(RoomId) != nullptr && Count < MaxCount) {
            OutLocations[Count] = RoomId;
            ++Count;
        }
    }
    OutCount = Count;
    return 0;
}

// pub::GetLocationNickname -- retail 0x7ed10 (ordinal 206). Retail dereferences the room
// (nickname at RoomData+0xc) without a null test.
SERVER_API unsigned int __cdecl GetLocationNickname(char* OutBuffer, unsigned int MaxLen, const unsigned int& RoomId) {
    RoomData* Room = BaseDataList_get()->get_room_data(RoomId);
    unsigned int Needed = static_cast<unsigned int>(strlen(Room->Nickname)) + 1;
    if (Needed > MaxLen)
        return Needed;
    memcpy(OutBuffer, Room->Nickname, Needed);
    return 0;
}

// pub::GetBases -- retail 0x7ed60 (ordinal 157). Walks every base, leaving Common's base
// iterator exhausted.
SERVER_API int __cdecl GetBases(const unsigned int& SystemId, unsigned int* const OutBases, unsigned int MaxCount, unsigned int& OutCount) {
    unsigned int Count = 0;
    Universe::IBase const* Base = Universe::GetFirstBase();
    while (Base != nullptr) {
        if (Base->SystemId == SystemId && Count < MaxCount) {
            OutBases[Count] = Base->NicknameId;
            ++Count;
        }
        Base = Universe::GetNextBase();
    }
    OutCount = Count;
    return 0;
}

SERVER_API int __cdecl GetSystem(unsigned int& OutSystemId, const unsigned int& BaseId) {
    OutSystemId = 0;
    Universe::IBase const* base = Universe::get_base(BaseId);
    if (base) {
        OutSystemId = base->SystemId;
        return 0;
    }
    return -2;
}

SERVER_API unsigned int __cdecl GetNicknameId(const char* Nickname) {
    return CreateID(Nickname);
}

SERVER_API unsigned short __cdecl MakeId(const char* Nickname) {
    return ::MakeId(Nickname);
}

// pub::DebugPrint -- retail 0x7ef40 (ordinal 94). Other levels print nothing.
SERVER_API void __cdecl DebugPrint(const char* Msg, int Level) {
    if (Level == 1) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: pub: %s",
              "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x3c5, Msg);
    } else if (Level == 2) {
        FDUMP(0x100001, "%s(%d) : *** ERROR: pub: %s",
              "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x3c6, Msg);
    }
}

// pub::GetNavMapScale -- retail 0x7f000 (ordinal 226). NavMapScale is ISystem+0x50.
SERVER_API int __cdecl GetNavMapScale(unsigned int SystemId, float& OutScale) {
    OutScale = 0.0f;
    Universe::ISystem const* Sys = Universe::get_system(SystemId);
    if (Sys == nullptr)
        return -1;
    OutScale = Sys->NavMapScale;
    return 0;
}

// DAT_06d8d900 -- the free-terminal mailbox. Server_GetLaunchPosition (FUN_06d00830)
// writes -1 here, dispatches controller event 0x32 through FUN_06d43650 so a mission
// script can answer synchronously with pub::ReportFreeTerminal, then reads it back.
// Global scope (not pub::) to match the extern in server_gameobject.h.
} // namespace pub
int GReportedFreeTerminal = 0;
namespace pub {

// pub::ReportFreeTerminal -- retail .text:06cffd10 (RVA 0x1fd10, ordinal 363). The first
// argument is never read.
SERVER_API int __cdecl ReportFreeTerminal(unsigned int P1, int P2) {
    GReportedFreeTerminal = P2;
    return 0;
}

// pub::DLL_LoadLibrary -- retail 0x06d43c40 (ordinal 93). A relative path is resolved
// against the [Initial * DLLs] `path` (GDllPath, DAT_06d8f654).
SERVER_API HINSTANCE __cdecl DLL_LoadLibrary(const char* DllPath) {
    char FullPath[0x104];
    if (DllPath[0] == '\\' || DllPath[1] == ':') {
        strcpy(FullPath, DllPath);
    } else {
        char Joined[0x104];
        _makepath(Joined, nullptr, GDllPath, DllPath, nullptr);
        _fullpath(FullPath, Joined, 0x104);
    }
    HMODULE Module = LoadLibraryA(FullPath);
    if (Module == nullptr) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: failed to load mission DLL '%s'\n",
              "E:\\FL\\Scratch\\Source\\Server\\dlls.cpp", 0x1de, FullPath);
    }
    return Module;
}

// ===========================================================================
// pub:: archetype utility functions
// ===========================================================================

// pub::GetFullHealth -- retail 0x7e1f0 (ordinal 182). Root::HitPts is +0x1c.
SERVER_API int __cdecl GetFullHealth(const unsigned int& ObjId, unsigned int& OutHealth) {
    int Result = -4;
    if ((ObjId & 0x3fffffff) != 0) {
        Archetype::Ship* ShipArch = Archetype::GetShip(ObjId);
        if (ShipArch != nullptr) {
            // ftol truncates to 64 bits; the low dword is stored.
            OutHealth = static_cast<unsigned int>(static_cast<long long>(ShipArch->HitPts));
            return 0;
        }
        Result = -2;
    }
    return Result;
}

// pub::GetShipArchSTRID -- retail 0x7e230 (ordinal 269). Root::IdsName is +0x14; retail
// reads it without a null test on the archetype.
SERVER_API int __cdecl GetShipArchSTRID(const unsigned int& ObjId, unsigned int& OutStrid) {
    if ((ObjId & 0x3fffffff) != 0) {
        Archetype::Ship* ShipArch = Archetype::GetShip(ObjId);
        unsigned int StridValue = (unsigned int)ShipArch->IdsName;
        OutStrid = StridValue;
        // 0 for a valid strid, -2 otherwise (branchless in retail).
        return (-(int)((StridValue & 0x3fffffff) != 0) & 2) - 2;
    }
    return -4;
}

// pub::GetType -- retail 0x7e330 (ordinal 294). Root::ObjectType is +0x10.
SERVER_API int __cdecl GetType(const unsigned int& ObjId, unsigned int& OutType) {
    int Result = -4;
    if ((ObjId & 0x3fffffff) != 0) {
        Archetype::Ship* ShipArch = Archetype::GetShip(ObjId);
        if (ShipArch != nullptr) {
            OutType = ShipArch->ObjectType;
            return 0;
        }
        Result = -2;
    }
    return Result;
}

// pub::GetSolarType -- retail 0x7e370 (ordinal 279). GetType over Archetype::GetSolar.
SERVER_API int __cdecl GetSolarType(const unsigned int& ObjId, unsigned int& OutType) {
    int Result = -4;
    if ((ObjId & 0x3fffffff) != 0) {
        Archetype::Solar* SolarArch = Archetype::GetSolar(ObjId);
        if (SolarArch != nullptr) {
            OutType = SolarArch->ObjectType;
            return 0;
        }
        Result = -2;
    }
    return Result;
}

// pub::GetMaxHitPoints -- retail 0x7e3b0 (ordinal 211). GetFullHealth over solars.
SERVER_API int __cdecl GetMaxHitPoints(const unsigned int& ObjId, int& OutHP) {
    int Result = -4;
    if ((ObjId & 0x3fffffff) != 0) {
        Archetype::Solar* SolarArch = Archetype::GetSolar(ObjId);
        if (SolarArch != nullptr) {
            // ftol: 64-bit truncation, low dword stored.
            OutHP = static_cast<int>(static_cast<long long>(SolarArch->HitPts));
            return 0;
        }
        Result = -2;
    }
    return Result;
}

// pub::GetGoodProperties -- retail 0x7e3f0 (ordinal 187). Volume +0x5c, Mass +0x20.
SERVER_API int __cdecl GetGoodProperties(const unsigned int& GoodId, float& OutVolume, float& OutMass) {
    int Result = -4;
    if ((GoodId & 0x3fffffff) != 0) {
        unsigned int ArchId = Good2Arch(GoodId);
        Archetype::Equipment* Equip = Archetype::GetEquipment(ArchId);
        if (Equip != nullptr) {
            OutVolume = Equip->Volume;
            OutMass = Equip->Mass;
            return 0;
        }
        Result = -2;
    }
    return Result;
}

// pub::TranslateSystemToMsgIdPrefix -- retail RVA 0x7E160, ordinal 468. GetMsgIdPrefix is
// ISystem slot 1 (SYSTEM's CommReferrable half); an empty prefix still returns 0.
SERVER_API int __cdecl TranslateSystemToMsgIdPrefix(unsigned int SystemId, TString<64>& OutPrefix) {
    Universe::ISystem const* Sys = Universe::get_system(SystemId);
    if (Sys == nullptr) {
        return -4;
    }
    OutPrefix = Sys->GetMsgIdPrefix().Data;
    return 0;
}

// pub::SinglePlayer -- retail 0x7e1e0 (ordinal 451).
SERVER_API bool __cdecl SinglePlayer() {
    return ::SinglePlayer();
}

// pub::GetLoadout -- retail 0x7e560 (ordinal 201). Inlined VC6 vector<EquipDesc> assign
// (EquipDesc is 0x20 bytes). Retail's self-assignment test (.text:06d5e58d) is dropped:
// in the port Map::Items is a std::vector owned by Common, so it cannot alias OutLoadout.
SERVER_API int __cdecl GetLoadout(EquipDescVector& OutLoadout, const unsigned int& LoadoutId) {
    if (LoadoutId == 0) {
        return -4;
    }
    Loadout::Map const* LoadoutMap = Loadout::Get(LoadoutId);
    if (LoadoutMap == nullptr) {
        return -2;
    }
    EquipDesc* SrcBegin = const_cast<EquipDesc*>(LoadoutMap->Items.data());
    unsigned int SrcCount = (unsigned int)LoadoutMap->Items.size();
    EquipDesc* DstBegin = OutLoadout.Begin;
    unsigned int DstSize = 0;
    unsigned int DstCap = 0;
    if (DstBegin != nullptr) {
        DstSize = (unsigned int)(OutLoadout.End - DstBegin);
        DstCap = (unsigned int)(OutLoadout.Cap - DstBegin);
    }
    if (SrcCount <= DstSize) {
        for (unsigned int I = 0; I < SrcCount; I++) {
            DstBegin[I] = SrcBegin[I];
        }
        if (SrcCount > 0) {
            OutLoadout.End = DstBegin + SrcCount;
        } else {
            OutLoadout.End = DstBegin;
        }
        return 0;
    }
    if (SrcCount <= DstCap) {
        for (unsigned int I = 0; I < DstSize; I++) {
            DstBegin[I] = SrcBegin[I];
        }
        for (unsigned int I = DstSize; I < SrcCount; I++) {
            new (&DstBegin[I]) EquipDesc(SrcBegin[I]);
        }
        OutLoadout.End = DstBegin + SrcCount;
        return 0;
    }
    // Reallocate: operator delete (FUN_06d5ffb0) on the old Begin without a null test, then
    // a fresh block with End = Cap. ::operator new/delete pair with ~EquipDescVector
    // (Common 0x06269110).
    ::operator delete(DstBegin);
    EquipDesc* NewBuf = static_cast<EquipDesc*>(::operator new(SrcCount * sizeof(EquipDesc)));
    OutLoadout.Begin = NewBuf;
    for (unsigned int I = 0; I < SrcCount; I++) {
        new (&NewBuf[I]) EquipDesc(SrcBegin[I]);
    }
    OutLoadout.End = NewBuf + SrcCount;
    OutLoadout.Cap = NewBuf + SrcCount;
    return 0;
}

} // namespace pub -- the save-file family below is not in pub::

// ===========================================================================
// The save-file writer family: retail FUN_06d460f0, FUN_06d46370, FUN_06d46930
// and FUN_06d4ccd0. Key names, format strings and source fields are taken from
// the listing at the address in each comment.
// ===========================================================================

// Server_GetAccountsRoot -- retail FUN_06d460f0. Returns one static 0x104 buffer
// (DAT_06d90158), so a caller can pass the result as _makepath's `dir` and reuse its
// own locals. Declared in server_savefile.h (also used by server_savegame.cpp).
static char GAccountsRoot[260];

// Retail DAT_06d902f8, see server_savefile.h.
char GServerLastSavePath[0x104];

char const* Server_GetAccountsRoot() {
    char UserDataPath[260];
    GetUserDataPath(UserDataPath);
    _makepath(GAccountsRoot, nullptr, UserDataPath,
              IsMPServer() ? "Accts\\MultiPlayer" : "Accts", nullptr);
    return GAccountsRoot;
}

// Server_AccountDirName -- retail FUN_06d46370, the MP account-directory name
// "%02x-%08x" (wcslen(name), crc).
//
// The CRC is the Rocksoft-model engine Common.dll's CFLIDMaker uses, but a separate
// instance in Server.dll configured by LAB_06d46250 (DAT_06d90138..DAT_06d9014d):
// register width 32, crc width 31, poly 0x8005, XorIn = XorOut = 0, reflect-in and
// table path both on. The finalize (.text:06d4645a) differs from CreateID and must not
// be copied from it: no `>> (32 - width)` fold-back and no 0x80000000 top bit, so the
// account hash is byteswap32(crc & 0x7fffffff). The input is the lowercased wide name,
// 2 * wcslen bytes.
namespace {
    const unsigned int AccountCrcWidth = 31;
    const unsigned int AccountCrcPoly = 0x8005;
    const unsigned int AccountCrcRegisterWidth = 32;

    unsigned int GAccountCrcTable[256];
    bool bAccountCrcTableReady = false;

    // The bit-overlay reflect CFLIDMaker uses; Server.dll has its own copy.
    unsigned int AccountCrcReflect(unsigned int Value, unsigned int BitCount) {
        unsigned int Result = Value;
        unsigned int Source = Value;
        for (unsigned int BitIndex = 0; BitIndex < BitCount; ++BitIndex) {
            unsigned int Mask = 1u << ((BitCount - BitIndex) - 1u);
            if ((Source & 1u) == 0)
                Result &= ~Mask;
            else
                Result |= Mask;
            Source >>= 1;
        }
        return Result;
    }

    void AccountCrcBuildTable() {
        if (bAccountCrcTableReady)
            return;
        unsigned int const TopBit = 1u << (AccountCrcWidth - 1u);
        unsigned int const Mask = ((TopBit - 1u) * 2u + 1u) | 1u;
        for (unsigned int Index = 0; Index < 256; ++Index) {
            // Reflect-in reflects the table index byte first.
            unsigned int Work = AccountCrcReflect(Index, 8) & 0xffu;
            Work <<= (AccountCrcWidth - 8u);
            for (int Bit = 0; Bit < 8; ++Bit) {
                if ((Work & TopBit) != 0)
                    Work = (Work << 1) ^ AccountCrcPoly;
                else
                    Work <<= 1;
            }
            // Reflect-out folds the entry back.
            Work = AccountCrcReflect(Work, AccountCrcWidth);
            GAccountCrcTable[Index] = Work & Mask;
        }
        bAccountCrcTableReady = true;
    }
}

// Server_AccountCrc -- reset/update/finalize over the DAT_06d8fd38 engine: reset to
// XorIn, FUN_06d46850 (block update; FUN_06d466f0 is the one-byte step), FUN_06d467b0
// (finalize). Only the reflect-in + table branch is live in retail: the two flags are
// written solely by the static initializer (both 1). Declared in server_savefile.h.
unsigned int Server_AccountCrc(unsigned char const* Bytes, unsigned int ByteCount) {
    AccountCrcBuildTable();

    unsigned int Crc = 0; // XorIn
    for (unsigned int I = 0; I < ByteCount; ++I)
        Crc = GAccountCrcTable[(Crc & 0xffu) ^ Bytes[I]] ^ (Crc >> 8);

    // Finalize, reflect path: mask with XorOut (0), then the byte-gather loop.
    unsigned int const TopBit = 1u << (AccountCrcWidth - 1u);
    unsigned int const Mask = ((TopBit - 1u) * 2u + 1u) | 1u;
    unsigned int Remain = Crc & Mask;
    unsigned int Folded = 0;
    for (unsigned int Shift = 8; Shift <= AccountCrcRegisterWidth; Shift += 8) {
        Folded |= (Remain & 0xffu) << (AccountCrcRegisterWidth - Shift);
        Remain >>= 8;
    }
    return Folded;
}

// Declared in server_savefile.h.
void Server_AccountDirName(char* Out, wchar_t const* AccountName) {
    // No null check in retail.
    unsigned int const Length = static_cast<unsigned int>(wcslen(AccountName));

    // Retail _wcsdup + _wcslwr; towlower on a local copy is the same transform.
    std::wstring Lowered;
    Lowered.reserve(Length);
    for (unsigned int I = 0; I < Length; ++I)
        Lowered.push_back(static_cast<wchar_t>(towlower(AccountName[I])));

    unsigned int const Folded = Server_AccountCrc(
        reinterpret_cast<unsigned char const*>(Lowered.c_str()), Length * 2u);

    sprintf(Out, "%02x-%08x", Length, Folded);
}

// Server_WriteCostume -- retail FUN_06d46930, cdecl (writer, prefix, costume). Writes one
// "<prefix><part>" key per body part via find_bodypart + get_name; a zero part id, a
// missed part or a null name skips the line. Note the body/head slot swap below. The
// prefix is "" for the worn costume and "com_" (.rdata:06d6b12c) for the comm costume;
// Server.dll has no literal "com_body". The writer is SaveGame_Writer (WriteString is
// vtable +0x14).
static void Server_WriteCostume(ISave& Save, char const* Prefix, Costume const& Cost) {
    CostumeDescriptions const* Descriptions = GetCostumeDescriptions();
    if (Descriptions == nullptr)
        return;

    struct FCostumePart {
        int TypeId;
        int PartSlot;
        char const* Format;
    };
    static const FCostumePart Parts[4] = {
        { 0, 1, "%sbody" },
        { 1, 0, "%shead" },
        { 2, 2, "%slefthand" },
        { 3, 3, "%srighthand" },
    };

    for (int I = 0; I < 4; ++I) {
        unsigned int const PartId = Cost.PartIds[Parts[I].PartSlot];
        if (PartId == 0)
            continue;
        char const* PartName = Descriptions->get_part_name(Parts[I].TypeId, PartId);
        if (PartName == nullptr)
            continue;
        char Key[128];
        sprintf_s(Key, Parts[I].Format, Prefix);
        Save.WriteString(Key, PartName);
    }
}

// Server_FormatMissionLogEntry -- retail FUN_06d4cc20, the recursive formatter for one
// FmtStr `log =` line: "id, count" then ", data, tag, flags" per Val. Retail also passes
// an ECX that is only handed to the recursive call, never read; the port drops it.
// The output was checked against saved character files: a nested FmtStr (tag 0x21)
// follows a bare ',' with no space (`..., 33, 0,327682, 2, ...`).
// The destination is FUN_06d4ccd0's 0x1000-byte stack buffer. Retail checks neither
// its size, nor a null subtree on a 0x21 node, nor the chain against the count taken
// first; the port does the same.
static void Server_FormatMissionLogEntry(char* Out, FmtStr const& Entry) {
    int Count = 0;
    for (FmtStr::Val const* Node = Entry.Head; Node != nullptr; Node = Node->Next)
        ++Count;

    sprintf(Out, "%u, %u", Entry.MadLibId, Count);

    // Runs Count times, following Val::Next unchecked.
    FmtStr::Val const* Node = Entry.Head;
    for (int Remaining = Count; Remaining > 0; --Remaining) {
        sprintf(Out + strlen(Out), ", %u, %u, %u",
                Node->Value,   // the raw payload dword, whatever the tag
                (unsigned int)Node->TypeTag,
                (unsigned int)Node->Flags);

        // 0x21 = nested FmtStr (common_text.h): bare ',' then recurse after it.
        if (Node->TypeTag == 0x21) {
            char* const Tail = Out + strlen(Out);
            *Tail = ',';
            Server_FormatMissionLogEntry(Tail + 1, *Node->Subtree);
        }
        Node = Node->Next;
    }
}

// Server_SaveSinglePlayerState -- retail FUN_06d447c0, cdecl (ISave*). The writer half of
// Server_LoadSinglePlayerState (FUN_06d44810, IServerImpl.cpp); only called by the
// serializer below, in single player. The clock sum is narrowed to float.
static void Server_SaveSinglePlayerState(ISave* Writer) {
    Writer->WriteSection("Time");
    Writer->WriteFloat("seconds", static_cast<float>(g_ElapsedTimeAccum + GServerTimeBase));
    ::Reputation::Save(Writer);
    Server_ControllerDispatch(0, pub::Controller::EMessage::SaveGame, Writer->GetFile());
}

// Server_CopyCostumeCounted -- the Costume copy the MP record refresh in FUN_06d4ccd0
// inlines twice (.text:06d4e63c, .text:06d4e6f6): only `count` accessory ids, under a
// signed loop. Slots past the count keep what the destination held.
static void Server_CopyCostumeCounted(Costume& Dst, Costume const& Src) {
    Dst.PartIds[0] = Src.PartIds[0];
    Dst.PartIds[1] = Src.PartIds[1];
    Dst.PartIds[2] = Src.PartIds[2];
    Dst.PartIds[3] = Src.PartIds[3];
    Dst.AccessoryCount = Src.AccessoryCount;
    int const Count = static_cast<int>(Src.AccessoryCount);
    for (int I = 0; I < Count; ++I)
        Dst.AccessoryIds[I] = Src.AccessoryIds[I];
}

// PlayerData::serialize_to_file -- retail FUN_06d4ccd0 (.text:06d4ccd0 .. .text:06d4ea4f).
//
// Signature: thiscall with three dword stack arguments (every exit is `RET 0xc`),
// returning AL; Ghidra's 15-parameter prototype is wrong. FileName is a base file name
// (_makepath's `fname`), Description a wchar_t* (WideToHex'd into `description`),
// DescripStrId is printed as `descrip_strid`. The autosave call site agrees.
//
// The cheat gate and the writer are one function: the save is refused when the cheat
// check fires.
//
// The "FLS1" container is written on every path, single player included: fopen "wb",
// the four bytes "FLS1", then the text ciphered with
// out[i] ^= (("Gene"[i & 3] + (unsigned char)i) | 0x80) (key at .rdata:06d6b318).
// Multiplayer-only is the destination: outside single player the text goes to
// `<accts>\temp.fl`, then the real path is unlinked and the temp renamed onto it.
//
// The MP base-record copy (.text:06d4d4dd) and the base_* keys read FSavedCharacter's
// BaseHullStatus (+0x25c), BaseEquipment (+0x260), BaseCollisionGroups (+0x26c); the
// listing's +0x468/+0x46c/+0x478 are node offsets (record at node + 0x20c). The map key
// is +0x64 alone (FUN_06d57da0 pairs it with a default record from FUN_06d46ab0).
bool PlayerData::serialize_to_file(char const* FileName, wchar_t const* Description,
                                   unsigned int DescripStrId) {
    // No null test on FileName, as retail.

    // The cheat gate (.text:06d4cd01), returning 0 (save refused) from inside it.
    // PlayerData::check_for_cheats (server_classes.cpp) includes the already-reported
    // latch at +0x34C.
    if (check_for_cheats())
        return false;

    // A dead MP ship is put back to the last-base loadout before it is written. Retail
    // compares the hull (+0x268) with 0.0 (DAT_06d65538) so the call runs for hull <= 0
    // only; an unordered (NaN) hull skips it.
    if (!SinglePlayer() && relative_health <= 0.0f)
        reload_ship_data();

    // The pending-visit flush (queue at +0x3B8 into FUN_06d508a0 with flags | 8). The
    // `visit` key reads the committed map at +0x3A0, so the flush must come first or
    // the newest systems are lost.
    flush_pending_visits();

    // The destination, built by retail itself (.text:06d4ce2b): account dir from the wide
    // account name at PlayerData+0x00, then FileName with extension "fl" (.rdata:06d6aea4)
    // in SP only -- in MP FileName carries its own extension.
    char AccountDirName[260];                                               // Stack[0x1f4], no pre-init
    if (IsMPServer()) {
        Server_AccountDirName(AccountDirName, AccountName);
    } else {
        // No return-value test and no explicit terminator. Retail UB kept: a 260+
        // character name, or one that does not convert, leaves the buffer unterminated.
        wcstombs(AccountDirName, AccountName, 0x104);
    }

    char AccountDir[260];
    _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);

    char SavePath[260];
    _makepath(SavePath, nullptr, AccountDir, FileName, IsMPServer() ? nullptr : "fl");

    char TempPath[260];
    sprintf_s(TempPath, "%s\\temp.fl", Server_GetAccountsRoot());

    char const* const WritePath = SinglePlayer() ? SavePath : TempPath;

    // The writer object lives on retail's stack (base vtable 06d6b5c0, concrete 06d6b5a0).
    // Open mode is "w+t" (.rdata:DAT_06d6b598 = 77 2b 74 00; the listing shows only "w"):
    // "+" lets Close() read the file back for encoding, "t" makes saves LF-only.
    SaveGame_Writer Writer;
    if (!Writer.Open(WritePath)) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Failed to open %s for writing",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xad4, WritePath);
        return false;
    }

    // Retail keeps the path being written in DAT_06d902f8; PlayerData::mission_save
    // (FUN_06d4c600) copies that file to AutoSave.fl afterwards.
    safe_strcpy(GServerLastSavePath, 0x104, WritePath);

    // Retail writes most keys straight to the FILE* (writer+0x04); only the costume
    // blocks and the single-player hook go through the writer's vtable.
    FILE* const Out = Writer.File;

    fprintf(Out, "\n[%s]\n", "Player");

    if (Description != nullptr) {
        char HexBuf[2048];
        WideToHex(Description, HexBuf);
        fprintf(Out, "%s = %s\n", "description", HexBuf);
    }

    if (DescripStrId != 0)
        fprintf(Out, "%s = %u\n", "descrip_strid", DescripStrId);

    fprintf(Out, "\n");

    // `tstamp`: the high FILETIME half is printed first, no space after the comma.
    {
        SYSTEMTIME SysTime;
        FILETIME FileTime;
        GetLocalTime(&SysTime);
        SystemTimeToFileTime(&SysTime, &FileTime);
        char TstampBuf[64];
        sprintf_s(TstampBuf, "%u,%u", FileTime.dwHighDateTime, FileTime.dwLowDateTime);
        fprintf(Out, "%s = %s\n", "tstamp", TstampBuf);
    }

    // The reputation block: one Reputation::Vibe::Get on PlayerData::rep_vibe (+0x318) and
    // four keys off its out-parameters. `name` is the vibe's display name (not the
    // character name); `rank` is overridden in SP from rank_level (+0x284); `house` is the
    // per-group line; `rep_group` is written once, only for a valid affiliation.
    // The Relation array (group_count() * 8) and the rank/count live at function scope:
    // the MP record refresh at the end reads them again before retail frees the array.
    // Vibe::Get is called unconditionally, as retail.
    unsigned int const GroupCount = GlobalReputation::group_count();
    GlobalReputation::Relation* const Relations = static_cast<GlobalReputation::Relation*>(
        operator new(GroupCount * sizeof(GlobalReputation::Relation)));
    unsigned int Affiliation = 0xffffffffu;
    unsigned int Rank = 0;
    unsigned char RelationCount = 0;
    FmtStr Name1;
    FmtStr Name2;
    {
        wchar_t const* DisplayName = nullptr;

        GlobalReputation::Vibe::Get(rep_vibe, Affiliation, Rank, RelationCount,
                                    Relations, Name1, Name2, DisplayName);

        // `name`, unconditionally. Retail UB kept as a path: Vibe::Get writes its
        // out-parameters only on a hit, so on a miss retail hands an uninitialised stack
        // word to WideToHex; the port passes nullptr, which faults the same way. Retail
        // also leaves Affiliation, Rank and the count uninitialised on a miss; the port
        // starts them at 0xffffffff, 0, 0.
        {
            char HexBuf[2048];
            WideToHex(DisplayName, HexBuf);
            fprintf(Out, "%s = %s\n", "name", HexBuf);
        }

        if (SinglePlayer())
            Rank = rank_level;
        fprintf(Out, "%s = %u\n", "rank", Rank);

        // The bound is the byte counter Vibe::Get filled.
        for (unsigned int I = 0; I < static_cast<unsigned int>(RelationCount); ++I) {
            TString<16> GroupNick;
            GroupNick.Length = 0;
            GroupNick.Data[0] = '\0';
            GlobalReputation::get_nickname(GroupNick, Relations[I].GroupId);
            char HouseBuf[256];
            sprintf_s(HouseBuf, "%g, %s", static_cast<double>(Relations[I].Feeling),
                      GroupNick.Data);
            fprintf(Out, "%s = %s\n", "house", HouseBuf);
        }

        if (Affiliation != 0xffffffffu) {
            TString<16> AffilNick;
            AffilNick.Length = 0;
            AffilNick.Data[0] = '\0';
            GlobalReputation::get_nickname(AffilNick, Affiliation);
            fprintf(Out, "%s = %s\n", "rep_group", AffilNick.Data);
        }
    }

    // Retail order: money first. Offsets +0x31c, +0x334, +0x338, +0x33c.
    fprintf(Out, "%s = %d\n", "money", cash);
    fprintf(Out, "%s = %d\n", "num_kills", num_kills);
    fprintf(Out, "%s = %d\n", "num_misn_successes", num_mission_successes);
    fprintf(Out, "%s = %d\n", "num_misn_failures", num_mission_failures);

    fprintf(Out, "\n");

    // The inline buffer at +0x2C4.
    fprintf(Out, "%s = %s\n", "voice", voice);

    Server_WriteCostume(Writer, "com_", com_costume);
    Server_WriteCostume(Writer, "", costume);

    fprintf(Out, "\n");

    // `system` of PlayerData+0x3E0. Retail does not null-check get_system; the nickname
    // is ISystem+0x50 (SYSTEM vtable slot 2).
    {
        Universe::ISystem const* Sys = Universe::get_system(system_id);
        fprintf(Out, "%s = %s\n", "system", Sys->Nickname);
    }

    // The base / position branch (.text:06d4d46a). With no live ship and no object bits in
    // current_base (+0x3ec), current_base is refilled from last_base_id (+0x3f0) first.
    //
    // SavedPos/SavedOrient are written only by the `pos`/`rotate` arm, but the MP record
    // refresh at the end reads them whether or not it ran. Retail zeroes the position
    // unconditionally before `system`, so {0,0,0} is retail. Retail UB not reproduced:
    // nothing initialises the orientation outside the `rotate` arm, so retail copies nine
    // uninitialised stack dwords into the record; the port copies zeros.
    float SavedPos[3] = { 0.0f, 0.0f, 0.0f };
    float SavedOrient[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    {
        if (ship_object_id == 0 && (current_base & 0x3fffffffu) == 0)
            current_base = last_base_id;

        Universe::IBase* Base = Universe::get_base(current_base);
        if (Base != nullptr) {
            // IBase+0x8 nickname (BASE vtable slot 1); retail does not null-test it.
            fprintf(Out, "%s = %s\n", "base", Base->Nickname);
            fprintf(Out, "\n");
            // Outside SP the docked state goes into the selected character's record
            // (+0x50): unique insert keyed on +0x64 (an existing record is kept), then
            // relative_health, collision groups and equipment are copied into it.
            if (!SinglePlayer()) {
                // insert(pair(key, default)).first == operator[] (find or
                // default-insert, never overwrite).
                PlayerData::FSavedCharacter& BaseRecord =
                    saved_characters[selected_character];
                BaseRecord.BaseHullStatus = relative_health;
                BaseRecord.BaseCollisionGroups = collision_groups;
                BaseRecord.BaseEquipment.Items = equipment.Items;
            }
        } else {
            // No base: `pos` and `rotate`, skipped (with the newline) when
            // Server_LookupLiveObject (FUN_06d00670, lookup + is_destroyed reject) fails.
            // Angles: FUN_06d4ea60/ea80/eaa0 extract the matrix columns; H =
            // sqrt(M0*M0 + M3*M3) is stored to float and compared with DAT_06d6b6a0
            // (1.9073486e-06f):
            //   H > eps : A = atan2(M7, M8), B = atan2(-M6, H), C = atan2(M3, M0)
            //   else    : A = atan2(-M5, M4), B = atan2(-M6, H), C = 0
            // each times DAT_06d6b69c (57.2957763671875f), printed as degrees in order
            // A, B, C; the loader feeds them to EulerMatrix, which takes degrees.
            GameObject* const LiveObj = Server_LookupLiveObject(ship_object_id);
            if (LiveObj != nullptr) {
                // Position source: SP -> get_position. MP -> the CShip's
                // CRemotePhysicsController (__RTDynamicCast of the controller at ship+0x60):
                // latest sample (+0xe0) when the sample count (+0x388) is non-zero, else
                // get_position; a cast miss leaves the zeros.
                // Retail UB reproduced as a path: a null or non-CShip cobject() makes
                // retail read address 0x60 and fault; so does the port.
                if (SinglePlayer()) {
                    Vector const& LivePos = LiveObj->get_position();
                    SavedPos[0] = LivePos.x;
                    SavedPos[1] = LivePos.y;
                    SavedPos[2] = LivePos.z;
                } else {
                    CObject const* const Cobj = LiveObj->cobject();
                    CObject const* const Ship =
                        (Cobj != nullptr &&
                         (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                            ? Cobj
                            : nullptr;
                    IVP_Controller* const Rc =
                        static_cast<IVP_Controller*>(Ship->GolemController);
                    CRemotePhysicsController* const Remote =
                        dynamic_cast<CRemotePhysicsController*>(Rc);
                    if (Remote != nullptr) {
                        if (Remote->GetSampleCount() != 0) {
                            Vector const Latest = Remote->GetLatestUpdate();
                            SavedPos[0] = Latest.x;
                            SavedPos[1] = Latest.y;
                            SavedPos[2] = Latest.z;
                        } else {
                            Vector const& LivePos = LiveObj->get_position();
                            SavedPos[0] = LivePos.x;
                            SavedPos[1] = LivePos.y;
                            SavedPos[2] = LivePos.z;
                        }
                    }
                }
                char PosBuf[128];
                sprintf(PosBuf, "%g,%g,%g", static_cast<double>(SavedPos[0]),
                        static_cast<double>(SavedPos[1]),
                        static_cast<double>(SavedPos[2]));
                fprintf(Out, "%s = %s\n", "pos", PosBuf);

                ::Matrix const& LiveOrient = LiveObj->get_transform().Rotation;
                for (int I = 0; I < 9; ++I)
                    SavedOrient[I] = LiveOrient.E[I / 3][I % 3];

                float const* const M = SavedOrient;
                double const DegPerRad = static_cast<double>(57.2957763671875f);
                double const HypotExact = sqrt(static_cast<double>(M[0]) * M[0] +
                                               static_cast<double>(M[3]) * M[3]);
                float const Hypot = static_cast<float>(HypotExact);      // FST: compare uses the double
                double AngleA = 0.0;
                double AngleB = 0.0;
                float AngleC = 0.0f;
                if (HypotExact > static_cast<double>(1.9073486328125e-06f)) {
                    AngleA = atan2(static_cast<double>(M[7]), static_cast<double>(M[8]));
                    AngleB = atan2(-static_cast<double>(M[6]), static_cast<double>(Hypot));
                    AngleC = static_cast<float>(atan2(static_cast<double>(M[3]),
                                                      static_cast<double>(M[0])));
                } else {
                    AngleA = atan2(-static_cast<double>(M[5]), static_cast<double>(M[4]));
                    AngleB = atan2(-static_cast<double>(M[6]), static_cast<double>(Hypot));
                    AngleC = 0.0f;
                }
                float const DegA = static_cast<float>(AngleA * DegPerRad); // retail stores A as float
                double const DegB = AngleB * DegPerRad;
                double const DegC = static_cast<double>(AngleC) * DegPerRad;

                char RotBuf[128];
                sprintf(RotBuf, "%g,%g,%g", static_cast<double>(DegA), DegB, DegC);
                fprintf(Out, "%s = %s\n", "rotate", RotBuf);
                fprintf(Out, "\n");
            }
        }
    }

    // +0x3F8, only when non-zero.
    if (current_location != 0)
        fprintf(Out, "%s = %u\n", "location", current_location);

    // ship_archetype / hull_status / collision_group, all gated on the archetype resolving.
    if (ship_archetype != 0) {
        Archetype::Ship* ShipArch = Archetype::GetShip(ship_archetype);
        if (ShipArch != nullptr) {
            // The resolved archetype's Root::Nickname id (+0x8).
            char ArchBuf[32];
            sprintf_s(ArchBuf, "%u", ShipArch->get_id());
            fprintf(Out, "%s = %s\n", "ship_archetype", ArchBuf);

            if (relative_health < 1.0f) {
                fprintf(Out, "%s = %g\n", "hull_status",
                        static_cast<double>(relative_health));
            }

            // The player's own list at +0x26C.
            for (std::list<CollisionGroupDesc>::const_iterator It =
                     collision_groups.begin();
                 It != collision_groups.end(); ++It) {
                if (!(It->Health < 1.0f))
                    continue;
                Archetype::CollisionGroup const* Group = ShipArch->get_group_by_id(It->Id);
                if (Group == nullptr)
                    continue;
                char GroupBuf[256];
                sprintf_s(GroupBuf, "%s, %g", Group->ObjName, static_cast<double>(It->Health));
                fprintf(Out, "%s = %s\n", "collision_group", GroupBuf);
            }
        }
    }

    // Written on the join point of both archetype skips.
    fprintf(Out, "\n");

    // The equipment list: `cargo` and `equip`.
    for (std::list<EquipDesc>::const_iterator CI = equipment.Items.begin();
         CI != equipment.Items.end(); ++CI) {
        EquipDesc const& Item = *CI;

        if (!::SinglePlayer() && Item.bMission)
            continue;

        Archetype::Equipment* Equip = Archetype::GetEquipment(Item.ArchId);
        if (Equip == nullptr) continue;
        unsigned int const NicknameId = Equip->Nickname.Id;

        char Line[512];
        if (!Item.bEquipped) {
            if (Item.Health == 1.0f) {
                sprintf(Line, "%u, %d, , , %d", NicknameId, Item.Count, Item.bMission ? 1 : 0);
            } else {
                sprintf(Line, "%u, %d, , %g, %d", NicknameId, Item.Count,
                        static_cast<double>(Item.Health), Item.bMission ? 1 : 0);
            }
            fprintf(Out, "%s = %s\n", "cargo", Line);
        } else {
            if (Item.is_internal()) {
                sprintf(Line, "%u, , %g", NicknameId, static_cast<double>(Item.Health));
            } else {
                sprintf(Line, "%u, %s, %g", NicknameId, Item.Hardpoint,
                        static_cast<double>(Item.Health));
            }
            fprintf(Out, "%s = %s\n", "equip", Line);
        }
    }

    fprintf(Out, "\n");

    // `last_base` and the base_* keys, MP only and only when a second base resolves from
    // last_base_id (+0x3F0) -- not the base written above. The base_* keys read the
    // selected character's record (+0x50), looked up before `last_base` is written.
    if (!SinglePlayer()) {
        Universe::IBase* LastBase = Universe::get_base(last_base_id);
        if (LastBase != nullptr) {
            PlayerData::FSavedCharacter const& BaseRecord =
                saved_characters[selected_character];

            fprintf(Out, "%s = %s\n", "last_base", LastBase->Nickname);
            fprintf(Out, "\n");

            // Unconditional (no `< 1` test, unlike hull_status).
            fprintf(Out, "%s = %g\n", "base_hull_status",
                    static_cast<double>(BaseRecord.BaseHullStatus));

            // GetShip(+0x264) without a null test; every group, no health filter.
            Archetype::Ship* BaseShip = Archetype::GetShip(ship_archetype);
            for (std::list<CollisionGroupDesc>::const_iterator It =
                     BaseRecord.BaseCollisionGroups.begin();
                 It != BaseRecord.BaseCollisionGroups.end(); ++It) {
                Archetype::CollisionGroup const* Group = BaseShip->get_group_by_id(It->Id);
                if (Group == nullptr)
                    continue;
                char GroupBuf[256];
                sprintf(GroupBuf, "%s, %g", Group->ObjName, static_cast<double>(It->Health));
                fprintf(Out, "%s = %s\n", "base_collision_group", GroupBuf);
            }
            fprintf(Out, "\n");

            // Mission items are skipped in every mode here.
            for (std::list<EquipDesc>::const_iterator It =
                     BaseRecord.BaseEquipment.Items.begin();
                 It != BaseRecord.BaseEquipment.Items.end(); ++It) {
                EquipDesc const& Item = *It;
                if (Item.bMission)
                    continue;
                Archetype::Equipment* Equip = Archetype::GetEquipment(Item.ArchId);
                if (Equip == nullptr)
                    continue;
                unsigned int const NicknameId = Equip->Nickname.Id;

                char Line[512];
                if (!Item.bEquipped) {
                    if (Item.Health == 1.0f) {
                        sprintf(Line, "%u, %d, , , %d", NicknameId, Item.Count,
                                Item.bMission ? 1 : 0);
                    } else {
                        sprintf(Line, "%u, %d, , %g, %d", NicknameId, Item.Count,
                                static_cast<double>(Item.Health), Item.bMission ? 1 : 0);
                    }
                    fprintf(Out, "%s = %s\n", "base_cargo", Line);
                } else {
                    if (Item.is_internal()) {
                        sprintf(Line, "%u, , %g", NicknameId, static_cast<double>(Item.Health));
                    } else {
                        sprintf(Line, "%u, %s, %g", NicknameId, Item.Hardpoint,
                                static_cast<double>(Item.Health));
                    }
                    fprintf(Out, "%s = %s\n", "base_equip", Line);
                }
            }
            fprintf(Out, "\n");
        }
    }

    // The weapon-group block: a pre-rendered block of ini lines (std::string data at
    // +0x384) passed as fprintf's whole format string. Retail UB reproduced as a path:
    // a '%' in it is interpreted with no arguments behind it.
#pragma warning(push)
#pragma warning(disable : 4774 6387)  // non-literal format string: retail's own call shape
    fprintf(Out, weapon_group_name.c_str());
#pragma warning(pop)

    fprintf(Out, "\n");

    // `visit`: one line per entry of the committed map at +0x3A0.
    for (std::map<unsigned int, unsigned char>::const_iterator VI = visit_map.begin();
         VI != visit_map.end(); ++VI) {
        char VisitBuf[64];
        sprintf_s(VisitBuf, "%u, %u", VI->first, static_cast<unsigned int>(VI->second));
        fprintf(Out, "%s = %s\n", "visit", VisitBuf);
    }

    fprintf(Out, "\n");

    // `log`: one line per mission_log FmtStr (list at +0x390).
    for (std::list<FmtStr>::const_iterator LI = mission_log.begin();
         LI != mission_log.end(); ++LI) {
        // FUN_06d4ccd0's 0x1000-byte stack scratch.
        char LogText[0x1000];
        Server_FormatMissionLogEntry(LogText, *LI);
        fprintf(Out, "%s = %s\n", "log", LogText);
    }

    fprintf(Out, "\n");

    // +0x39C.
    fprintf(Out, "%s = %u\n", "interface", interface_state);

    fprintf(Out, "\n");

    // Controller event 4 (FUN_06d43650) with {client_id (+0x348), the open save file}.
    struct FSaveWrittenEvent {
        unsigned int ClientId;                  // PlayerData+0x348
        FILE* File;                             // the writer's +0x04
    };
    FSaveWrittenEvent SaveEvent;
    SaveEvent.ClientId = client_id;
    SaveEvent.File = Out;
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerSave, &SaveEvent);

    // Single player: the [Time] section, the feelings and event 0x41, still in the same
    // file before the error latch and the encode.
    if (SinglePlayer())
        Server_SaveSinglePlayerState(&Writer);

    // The write-error latch (an inlined ferror). On error retail still runs the whole
    // encode below and then unlinks what it just wrote, so a broken file is never
    // loaded back.
    bool const WriteFailed = Writer.HadWriteError();
    if (WriteFailed) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Error writing %s\n",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xc6d, WritePath);
    }

    // The "FLS1" container (.text:06d4e0cf), in every mode; Close() is FUN_06d4eb00.
    Writer.Close();

    if (WriteFailed) {
        _unlink(WritePath);
        // Retail leaks the relation array on this path (only the success path reaches
        // its free at .text:06d4e92a); so does the port.
        return false;
    }

    // The MP swap: the finished temp.fl replaces the real file, so a crash mid-write
    // leaves the previous character file intact.
    if (!SinglePlayer()) {
        _unlink(SavePath);
        rename(TempPath, SavePath);
    }

    // The MP record refresh (.text:06d4e18c): the selected character's record (+0x50) is
    // brought up to what was just written, so it reflects the last save, not the last
    // load. Record lookup is insert(pair(+0x64, default)).first, i.e. operator[]; node
    // offsets below are record + 0x20c.
    //   node+0x44c  HullStatus      = +0x268
    //   node+0x450  Equipment       = +0x278 (list operator=)
    //   node+0x56c  EquipIdMaker    = +0x3c8 (a WORD)
    //   node+0x45c  CollisionGroups = +0x26c (list operator=)
    //   node+0x43c..0x448  Money / NumKills / NumMissionSuccesses /
    //               NumMissionFailures = +0x31c/+0x334/+0x338/+0x33c
    //   node+0x4c8  Rank            = Vibe::Get's rank
    //   node+0x590  Visits          = +0x3a0 (map operator=, FUN_06d57540 / FUN_06d59e10)
    //   node+0x570  WeaponGroups    = +0x380 (string assign)
    //   node+0x484 / 0x488  BaseId / LastBaseId = +0x3ec / +0x3f0
    //   node+0x494  Position        = the `pos` locals
    //   node+0x4a0  Orientation     = the nine matrix dwords
    //   node+0x438 / 0x48c / 0x490  ShipArchetype / LocationId / SystemId =
    //               +0x264 / +0x3f8 / +0x3e0
    //   node+0x580  MissionLog      = +0x390 (FUN_06d58e10 erase, FUN_06d57420 insert)
    //   node+0x4c4  StartingRing    = +0x414
    //   node+0x4e0  ComCostume      = +0x28c (counted copy)
    //   node+0x514  Voice           = +0x2c4 (capped at 0x1f)
    //   node+0x538  BaseCostume     = +0x2e4 (counted copy)
    //   node+0x58c  Interface       = +0x39c
    //   node+0x4cc  Relations: cleared, then one push_back per Vibe::Get relation
    // The relation array is then freed on both the SP and the MP path.
    if (!SinglePlayer()) {
        PlayerData::FSavedCharacter& Record =
            saved_characters[selected_character];
        Record.HullStatus = relative_health;
        Record.Equipment.Items = equipment.Items;
        Record.EquipIdMaker = equip_id_maker;
        Record.CollisionGroups = collision_groups;
        Record.Money = cash;
        Record.NumKills = num_kills;
        Record.NumMissionSuccesses = num_mission_successes;
        Record.NumMissionFailures = num_mission_failures;
        Record.Rank = Rank;
        Record.Visits = visit_map;
        Record.WeaponGroups = weapon_group_name;
        Record.BaseId = current_base;
        Record.LastBaseId = last_base_id;
        Record.Position[0] = SavedPos[0];
        Record.Position[1] = SavedPos[1];
        Record.Position[2] = SavedPos[2];
        for (int I = 0; I < 9; ++I)
            Record.Orientation[I / 3][I % 3] = SavedOrient[I];
        Record.ShipArchetype = ship_archetype;
        Record.LocationId = current_location;
        Record.SystemId = system_id;
        Record.MissionLog = mission_log;
        Record.StartingRing = starting_ring;

        Server_CopyCostumeCounted(Record.ComCostume, com_costume);
        {
            // TString<32>::operator=(char const*) inlined; its null arm cannot fire
            // (the source is an inline buffer).
            size_t VoiceLen = strlen(voice);
            if (VoiceLen >= 0x20)
                VoiceLen = 0x1f;
            memcpy(Record.Voice.Data, voice, VoiceLen);
            Record.Voice.Data[VoiceLen] = '\0';
            Record.Voice.Length = static_cast<unsigned>(VoiceLen);
        }
        Server_CopyCostumeCounted(Record.BaseCostume, costume);
        Record.Interface = interface_state;

        Record.Relations.clear();
        for (unsigned int I = 0; I < static_cast<unsigned int>(RelationCount); ++I)
            Record.Relations.push_back(Relations[I]);
    }

    operator delete(Relations);                                            // .text:06d4e92a
    return true;
}

namespace pub {

// pub::Save -- retail 0x7efa0 (ordinal 397). Single player only. SaveType 1 is the
// autosave (FUN_06d4c430); anything else goes to the mission save (FUN_06d4c600) with
// SaveType itself as the descrip_strid. The slot is not validated. Always returns 0.
SERVER_API int __cdecl Save(unsigned int PlayerId, unsigned int SaveType) {
    if (::SinglePlayer()) {
        PlayerData& Slot = ::Players[PlayerId];
        if (SaveType == 1)
            Slot.auto_save();
        else
            Slot.mission_save(SaveType);
    }
    return 0;
}

} // namespace pub
// Server_ControllerDispatch, pub::Controller::_SendMessage's dispatch core, is declared in
// common_server_controller.h (defined in IServerImpl.cpp).
extern "C" int __cdecl Server_ControllerSetHeartbeat(unsigned int Flid, float Interval);
extern "C" int __cdecl Server_ControllerSetTimer(unsigned int Flid, unsigned int Param1,
                                                 unsigned int Param2, float Timeout);
// FUN_06d43820 (IServerImpl.cpp): is this FLID a live or pending controller.
extern "C" bool __cdecl Server_ControllerExists(const unsigned int& Flid);

// Retail FUN_06d00670, cdecl, Flid by value: look an object up by FLID (Server_FindObject,
// FUN_06d006c0) and reject it when destroyed. Not related to the local player.
// The liveness test is slot +0x160 of the wrapper's own vtable (GameObject::is_destroyed,
// .text:06cee6d0 in all thirteen Server.dll vtables), not an IObjInspect slot (those end
// at +0x150). Retail does not test the CObject's class here; callers that need it apply
// the guard themselves. Declared in server_gameobject.h.
GameObject* Server_LookupLiveObject(unsigned int Flid) {
    GameObject* Obj = Server_LookupGameObject(Flid);
    if (Obj == nullptr) return nullptr;
    if (Obj->is_destroyed()) return nullptr;
    return Obj;
}

// Retail's Dock / InstantDock / Launch / FormationResponse / LaneResponse follow the
// lookup with __RTDynamicCast to Ship; here that is `dynamic_cast< ::Ship*>`.

// The two file-static trees of SpaceObj.cpp, written by pub::SpaceObj::Create, erased by
// pub::SpaceObj::Destroy, and referenced nowhere else in Server.dll:
//   DAT_06d8d834 -- the ship ids Create made.
//   DAT_06d8d820 -- ship id -> the ShipInfo's ControllerId; Create inserts {id, 0} and
//     then stores the id into the mapped dword.
// Retail uses VC6 _Tree (0x18-byte nodes); the port uses std containers (no retail ABI).
static std::set<unsigned int> GSpaceObjCreatedShips;                    // .data:DAT_06d8d834
static std::map<unsigned int, unsigned int> GSpaceObjShipControllers;   // .data:DAT_06d8d820

// ===========================================================================
// pub::Phantom exports and the Server-side phantom record.
//
// Retail allocates one 0x28-byte record per phantom (operator new(0x28) at .text:06cfbea5
// / 06cfc07d) whose vtable .rdata:06d66b8c has exactly two slots, LAB_06cffed0 and
// LAB_06cfff30: it is a PhySys::CObjectCB, handed to PhySys::CreatePhantom as the
// PhantomParms+0x40 listener. The pub::Phantom handle IS that record. Layout:
//   +0x00 vtable          +0x04 std::list<unsigned> of ids that entered (alloc byte,
//   +0x08 head, +0x0c size)  +0x10 the same for ids that left (+0x14 head, +0x18 size)
//   +0x1c TypeMask (tested against CSimple::TypeBits)  +0x20 the PhySys phantom handle
//   +0x24 the system id.
// Every live record sits on the global std::list at .data:DAT_06d8d8f8 (size
// DAT_06d8d8fc), built by the static initializer LAB_06cffd20 and torn down by the atexit
// FUN_06cffd60 -- here a plain static std::list. .data:DAT_06d8d928 is set while
// FUN_06cffdc0 (Server_PumpPhantomEvents) dispatches; every pub::Phantom export returns
// -5 while it is set.
// ===========================================================================
namespace {

class ServerPhantom : public PhySys::CObjectCB
{
public:
    // LAB_06cffed0 (vtable +0x00, RET 4): a CSimple (class & 3) whose TypeBits (+0xe0)
    // share a bit with TypeMask has its id (+0xb0) appended to Entered.
    void added(CObject* Obj) override;
    // LAB_06cfff30 (vtable +0x04): the same test, onto Left.
    void deleted(CObject* Obj) override;

    std::list<unsigned int> Entered;   // +0x04 .. +0x0f
    std::list<unsigned int> Left;      // +0x10 .. +0x1b
    unsigned int TypeMask = 0;         // +0x1c
    void* PhyHandle = nullptr;         // +0x20
    unsigned int SystemId = 0;         // +0x24
    // FUN_06cfbd60 is the destructor (Left, then Entered: member destruction order).
};

void ServerPhantom::added(CObject* Obj)
{
    if (Obj == nullptr) return;
    if ((static_cast<unsigned int>(Obj->ObjectClass) & 3u) != 3u) return;
    if ((static_cast<CSimple*>(Obj)->TypeBits & TypeMask) == 0) return;
    Entered.push_back(static_cast<CSimple*>(Obj)->Id);
}

void ServerPhantom::deleted(CObject* Obj)
{
    if (Obj == nullptr) return;
    if ((static_cast<unsigned int>(Obj->ObjectClass) & 3u) != 3u) return;
    if ((static_cast<CSimple*>(Obj)->TypeBits & TypeMask) == 0) return;
    Left.push_back(static_cast<CSimple*>(Obj)->Id);
}

std::list<ServerPhantom*> GPhantoms;   // .data:DAT_06d8d8f8 / DAT_06d8d8fc
bool GbPhantomPumpBusy = false;        // .data:DAT_06d8d928

// The linear search every export opens with (e.g. .text:06cfbce1).
std::list<ServerPhantom*>::iterator FindPhantom(void* Handle)
{
    std::list<ServerPhantom*>::iterator It = GPhantoms.begin();
    while (It != GPhantoms.end() && *It != Handle)
        ++It;
    return It;
}

} // namespace

// FUN_06cffdc0, called once per server physics tick from FUN_06d001d0. With the busy
// latch set, dispatches controller events 0x2d (enter) / 0x2e (exit) with
// {id, phantom, SystemId} for every queued id, emptying both lists.
void Server_PumpPhantomEvents()
{
    GbPhantomPumpBusy = true;
    for (std::list<ServerPhantom*>::iterator It = GPhantoms.begin(); It != GPhantoms.end(); ++It) {
        ServerPhantom* const Ph = *It;
        for (std::list<unsigned int>::iterator Id = Ph->Entered.begin();
             Id != Ph->Entered.end(); ++Id) {
            unsigned int Event[3];
            Event[0] = *Id;
            Event[1] = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Ph));
            Event[2] = Ph->SystemId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::PhantomEnter, Event);
        }
        Ph->Entered.clear();
        for (std::list<unsigned int>::iterator Id = Ph->Left.begin();
             Id != Ph->Left.end(); ++Id) {
            unsigned int Event[3];
            Event[0] = *Id;
            Event[1] = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Ph));
            Event[2] = Ph->SystemId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::PhantomExit, Event);
        }
        Ph->Left.clear();
    }
    GbPhantomPumpBusy = false;
}

namespace pub {
namespace SpaceObj {

// Retail SpaceObj::Dock calls SpaceObj::ActivateEquipment, which is defined further
// down this file; forward-declare it so the call order matches retail.
SERVER_API EQUIPMENT_RTYPE __cdecl ActivateEquipment(const unsigned int& obj_id_ref, EQInfo* eq_info);

// SpaceObj_BuildTransportLoadout -- retail FUN_06d5f990 (cdecl), sole caller
// pub::SpaceObj::Create (ship archetype, &EquipIdMaker, &ShipInfo+0x5c, &request+0x70).
// Named after its own FDUMP text ("Transport loadout aborted").
// With cargo-pod hardpoints only the first two cargo nodes are used: per node, each hp
// gets a mounted pod plus an unmounted cargo item of Count / hp-count, and the last
// element takes the remainder. A good without pod_appearance aborts the whole loadout.
// Without pod hardpoints every node becomes one internal cargo item.
// Each append is retail's insert(end, 1, EquipDesc()) followed by setters on the new
// last element; add_equipment_item(.., false) never folds.
static void SpaceObj_AppendDefaultEquip(EquipDescVector& Out) {
    EquipDesc const Blank;
    Out.add_equipment_item(Blank, false);
}

static void SpaceObj_BuildTransportLoadout(Archetype::Ship const* Arch,
                                           SubObjectID::EquipIdMaker* IdMaker,
                                           OwnerList<CargoDesc> const* Cargo,
                                           EquipDescVector* Out) {
    // Reserve size + cargo node count.
    unsigned int Need = (Out->Begin == nullptr) ? 0u
        : static_cast<unsigned int>(Out->End - Out->Begin);
    for (MetaNode<CargoDesc> const* Node = Cargo->get_head(); Node != nullptr; Node = Node->Next)
        ++Need;
    unsigned int const Capacity = (Out->Begin == nullptr) ? 0u
        : static_cast<unsigned int>(Out->Cap - Out->Begin);
    if (Capacity < Need) {                                          // unsigned compare
        int const AllocCount = static_cast<int>(Need) >= 0 ? static_cast<int>(Need) : 0;
        EquipDesc* const NewBuf = static_cast<EquipDesc*>(
            ::operator new(static_cast<size_t>(AllocCount) * sizeof(EquipDesc)));
        EquipDesc* Dst = NewBuf;
        for (EquipDesc* Src = Out->Begin; Src != Out->End; ++Src, ++Dst)
            new (Dst) EquipDesc(*Src);
        // _Destroy is a no-op: EquipDesc's destructor is trivial.
        unsigned int const OldSize = (Out->Begin == nullptr) ? 0u
            : static_cast<unsigned int>(Out->End - Out->Begin);
        ::operator delete(Out->Begin);                              // FUN_06d5ffb0
        Out->Cap = NewBuf + Need;
        Out->End = NewBuf + OldSize;
        Out->Begin = NewBuf;
    }

    std::vector<CacheString> const* const Hps = Arch->get_legal_hps(HpTypeCargoPod);
    if (Hps != nullptr && !Hps->empty()) {
        int const HpCount = static_cast<int>(Hps->size());
        int Index = 0;
        for (MetaNode<CargoDesc> const* Node = Cargo->get_head(); Node != nullptr;
             Node = Node->Next) {
            CargoDesc const* const Desc = Node->Data;
            GoodInfo const* const Good = GoodList::find_by_id(Desc->GoodId);
            unsigned int const ArchId = Good->ArchId;
            Archetype::Equipment* const Eq = Archetype::GetEquipment(ArchId);
            Archetype::CargoPod* const Pod = Eq->get_pod_appearance();
            if (Pod == nullptr) {
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: Equipment(%s) has no pod_appearance.  Transport loadout aborted.",
                      "E:\\FL\\Scratch\\Source\\Server\\SrvMisc.cpp", 0x3a,
                      Eq->Nickname.get_string());
                return;
            }
            // Retail divides via FILD/FIDIV/ftol; both operands fit a double exactly and
            // ftol truncates, so this is the same integer.
            int const PerHp = static_cast<int>(
                static_cast<double>(Desc->Count) / static_cast<double>(HpCount));
            int Left = static_cast<int>(Desc->Count);
            for (std::vector<CacheString>::const_iterator Hp = Hps->begin();
                 Hp != Hps->end(); ++Hp) {
                SpaceObj_AppendDefaultEquip(*Out);
                EquipDesc* Last = Out->End - 1;
                Last->set_id(IdMaker->CreateEquipID());
                Last->set_arch_id(Pod->Nickname.Id);
                Last->set_count(1);
                Last->set_status(1.0f);
                Last->set_hardpoint(*Hp);
                Last->set_equipped(true);

                SpaceObj_AppendDefaultEquip(*Out);
                Last = Out->End - 1;
                Last->set_id(IdMaker->CreateEquipID());
                Last->set_arch_id(ArchId);
                Last->set_status(1.0f);
                Last->set_hardpoint(*Hp);
                Last->set_equipped(false);
                Last->set_count(PerHp);
                Left -= PerHp;
            }
            (Out->End - 1)->set_count(Left + PerHp);
            if (++Index > 1)                                        // first two nodes only
                return;
        }
        return;
    }

    // No cargo-pod hardpoint: every node.
    for (MetaNode<CargoDesc> const* Node = Cargo->get_head(); Node != nullptr; Node = Node->Next) {
        CargoDesc const* const Desc = Node->Data;
        SpaceObj_AppendDefaultEquip(*Out);
        EquipDesc* const Last = Out->End - 1;
        Last->set_id(IdMaker->CreateEquipID());
        Last->set_arch_id(Good2Arch(Desc->GoodId));
        Last->set_count(static_cast<int>(Desc->Count));
        Last->set_status(1.0f);
        Last->make_internal();
        Last->set_equipped(false);
    }
}

// SpaceObj::Create -- retail .text:06cf7c90 (ordinal 0x17c90, cdecl). Builds an
// FShipCreateRequest from the ShipInfo and hands it to StarSystem::create_ship
// (FUN_06d0a880, return value unused). No null test on the ship archetype or the loadout.
// The id is CFLIDMaker::AllocateObjectID inlined; a zero id leaves the result at -1.
SERVER_API int __cdecl Create(unsigned int& out_id, const SpaceObj::ShipInfo& info) {
    CFLIDMaker* const IdMaker = CFLIDMaker::GetInstance();
    if (IdMaker == nullptr) {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\pubSpace.cpp", 0x3f,
              "Failed to grab instance of global ID maker.\n");
        return -1;
    }
    int Result = -1;
    FStarSystem* const System = SysDB_LookupSystem(info.SystemID);
    if (System == nullptr) return Result;

    FShipCreateRequest Request;
    Request.EquipStateData = nullptr;
    Request.MarkPlayerId = 0u;
    Request.CostumeBody = info.CostumeBody;
    Request.CostumeHead = info.CostumeHead;
    Request.CostumeRightHand = info.CostumeRightHand;
    Request.CostumeLeftHand = info.CostumeLeftHand;
    Request.CostumeAccessoryCount = info.CostumeAccessoryCount;
    for (int Index = 0; Index < Request.CostumeAccessoryCount; ++Index)
        Request.CostumeAccessory[Index] = info.CostumeAccessory[Index];
    Request.ReputationRank = info.ReputationRank;
    Request.VoiceId = info.VoiceId;
    Request.VibeGroupId = info.VibeGroupId;
    Request.GroupNameId = info.GroupNameId;
    Request.ArchetypeNickname = info.ShipArchID;
    Request.Position = info.Position;
    Quaternion ShipOrient;
    Server_MatrixToQuat(ShipOrient, info.Orientation);
    Request.Orientation = ShipOrient;
    Request.Velocity = info.Velocity;
    Request.AngularVelocity = info.AngularVelocity;
    Request.DockingBaseId = info.DockingBaseId;
    Request.LoadoutId = info.LoadoutID;
    Request.Throttle = 0.0f;
    Request.OwnerPlayer = 0u;
    Request.HitPts = info.HitPts;
    Request.MarkPlayerId = info.MarkPlayerId;
    Archetype::Ship* const ShipArch = Archetype::GetShip(info.ShipArchID);
    ShipArch->get_undamaged_collision_group_list(Request.CollisionGroups);

    SubObjectID::EquipIdMaker EquipIds;
    if (info.LoadoutID != 0u) {
        Loadout::Map const* const LoadoutMap = Loadout::Get(info.LoadoutID);
        EquipIds = LoadoutMap->IdMaker;                                  // word store
    }
    SpaceObj_BuildTransportLoadout(ShipArch, &EquipIds, &info.Cargo, &Request.Equipment);
    Request.EquipIds = EquipIds;

    if (!Server_ControllerExists(info.ControllerId)) {
        Result = -4;
    } else {
        Request.ObjId = IdMaker->AllocateObjectID();
        System->create_ship(Request);
        unsigned int const Id = Request.ObjId;
        if ((Id & 0x3fffffffu) != 0) {
            out_id = Id;
            GSpaceObjCreatedShips.insert(out_id);
            Result = 0;
            std::pair<std::map<unsigned int, unsigned int>::iterator, bool> const Inserted =
                GSpaceObjShipControllers.insert(
                    std::pair<unsigned int const, unsigned int>(Id, 0u));
            Inserted.first->second = info.ControllerId;
        }
    }
    return Result;
}

// SpaceObj::CreateLoot -- retail .text:06cf84a0 (cdecl). One 0x6c-byte FLootRecord built
// on the stack, handed to StarSystem::create_loot (FUN_06d08a40) in mode 4 with
// LootInfo+0x0c as the source object.
SERVER_API int __cdecl CreateLoot(unsigned int& out_id, const SpaceObj::LootInfo& loot_info) {
    StarSystem* const Sys = SysDB_LookupSystem(loot_info.SystemID);
    if (Sys == nullptr) return -4;

    FLootRecord Record = {};
    Record.bCanAiTractor = 1;
    Record.bMission = 0;

    // out_id is written before anything else is read. No null test on the id maker.
    unsigned int const LootId = CFLIDMaker::GetInstance()->AllocateObjectID();
    out_id = LootId;
    Record.Id = LootId;

    // No null test on the contents archetype.
    Archetype::Equipment* const Contents = Archetype::GetEquipment(loot_info.EquipArchID);
    Record.ContentsArch = Contents;
    Record.ContentsHitPts = Contents->HitPts;
    Record.Count = static_cast<int>(loot_info.Units);

    // The crate is the contents' loot appearance (Equipment vt+0x1c).
    Archetype::Equipment* const Appearance = Contents->get_loot_appearance();
    Record.Arch = Appearance;
    Record.HitPts = static_cast<float>(static_cast<double>(Appearance->HitPts) *
                                       static_cast<double>(loot_info.HitPtsScale));

    Record.NameId = loot_info.NameId;                   // record +0x1c
    Record.Velocity = loot_info.Velocity;               // record +0x20
    Record.Position = loot_info.Position;               // record +0x2c
    Record.Orientation = loot_info.Orientation;         // record +0x38
    Record.AngularVelocity = loot_info.AngularVelocity; // record +0x5c
    Record.bCanAiTractor = loot_info.bCanAiTractor;     // record +0x68
    Record.bMission = loot_info.bMission;               // record +0x69
    Record.MarkForPlayer = loot_info.MarkForPlayer;     // record +0x18

    Sys->create_loot(loot_info.OwnerObjId, 4, &Record, 1);
    return 0;
}

// SpaceObj::Destroy -- retail .text:06cf85f0 (ordinal 0x185f0, cdecl). An in-transit
// object (no system) goes to Server_DestroyInTransit (FUN_06d00410) and still returns -2.
// Both tree erases run on both paths.
SERVER_API int __cdecl Destroy(unsigned int obj_id, DestroyType destroy_type) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    int Result = -2;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System))                     // FUN_06d00630
        return Result;

    if (System != nullptr) {
        Obj->destroy(static_cast<int>(destroy_type), 0);
        Result = 0;
    } else {
        Server_DestroyInTransit(Obj);
    }

    GSpaceObjCreatedShips.erase(obj_id);
    GSpaceObjShipControllers.erase(obj_id);
    return Result;
}

// SpaceObj::DrainShields -- retail .text:06cf8930 (0x18930). A stack DamageList with
// Cause 0x17 (DamageCause::DrainShields), filled by EqObj::drain_shields
// (vtable +0x1d8: a -1.0f entry per shield) and applied by apply_damage_list (+0x188) on
// the object the lookup returned, without re-narrowing after the class test.
SERVER_API int __cdecl DrainShields(unsigned int obj_id) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Cobj = Obj->Object;
    if (Cobj == nullptr ||
        (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj) return -1;
    DamageList Damage;
    Damage.Cause = DamageCause::DrainShields;
    static_cast< ::EqObj*>(Obj)->drain_shields(Damage);
    Obj->apply_damage_list(Damage);
    return 0;
}

// SpaceObj::EnumerateCargo -- retail .text:06cf95f0. A lookup miss and a non-CEqObj both
// return -2.
SERVER_API int __cdecl EnumerateCargo(const unsigned int& obj_id, CargoEnumerator& enumerator) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -2;
    CEqObj* const Ceq = static_cast<CEqObj*>(Raw);

    // notify_cargo is the enumerator's own vtable slot 0, fed straight from the CECargo:
    // its archetype id (+0x0c -> +0x08), Count (+0x28) and Id (+0x08). An item whose
    // Arch2Good is 0 is skipped, and the walk never stops early (the slot returns void).
    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* Item = Ceq->EquipManager.Traverse(Walk);
    while (Item != nullptr) {
        unsigned int const good_id = Arch2Good(Item->Arch->get_id());
        if (good_id != 0) {
            enumerator.notify_cargo(good_id, static_cast<CECargo*>(Item)->Count, Item->Id);
        }
        Item = Ceq->EquipManager.Traverse(Walk);
    }
    return 0;
}

// SpaceObj::FormationResponse -- retail .text:06cfa9f0 (cdecl). The Ship cast is not
// tested. request_returned is FUN_06d01e80.
SERVER_API int __cdecl FormationResponse(const unsigned int& obj_id, FORMATION_RTYPE response_type) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);
    ShipRw->request_returned(1, static_cast<int>(response_type), 0);
    return 0;
}

// SpaceObj::LaneResponse -- retail .text:06cfaa50 (cdecl). FormationResponse with
// request type 2.
SERVER_API int __cdecl LaneResponse(const unsigned int& obj_id, int lane_param) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);
    ShipRw->request_returned(2, lane_param, 0);
    return 0;
}

// SpaceObj::DockRequest -- retail 0x1a6b0. No mask check, and a failed lookup returns 0
// (not -4/-2); otherwise a tail call into IObjAffect slot 23 (+0x5c of the second vtable).
SERVER_API int __cdecl DockRequest(const unsigned int& obj_id, const unsigned int& target_id) {
    GameObject* Obj = Server_LookupLiveObject(obj_id);
    if (!Obj) return 0;
    // The third argument is the address of four zero bytes.
    static unsigned const ZeroSecondId = 0;   // .rdata:06d66b04
    return Obj->request_event(0, &target_id, &ZeroSecondId, 1, nullptr);
}

// SpaceObj::Dock -- retail .text:06cfa6e0 (ordinal 0x1a6e0, cdecl). Neither the Ship nor
// the EqObj cast is tested; a failed ship lookup still falls through to the host part.
// For a CShip, every follower watcher (CShip +0x1f8 vector) with a behaviour manager gets
// event 5 (handled by LAB_062d3f90) while the target resolves. dequeue's bool is ignored.
// Deviation: retail writes only EQInfo's dword at +0 and byte at +4; the port zeroes the
// rest.
SERVER_API int __cdecl Dock(const unsigned int& ship_id, const unsigned int& target_id,
                             int dock_port, DOCK_HOST_RESPONSE dock_host_response) {
    if ((ship_id & 0x3fffffffu) == 0) return -4;
    int Result = -2;
    GameObject* const Obj = Server_LookupLiveObject(ship_id);
    if (Obj != nullptr) {
        ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);
        if (ShipRw->bDockInUse != 0) {                                   // Ship byte +0x98
            ShipRw->request_returned(0, static_cast<int>(dock_host_response), dock_port);
            Result = 0;
        }

        EQInfo DockEquip;
        memset(&DockEquip, 0, sizeof(DockEquip));
        DockEquip.Type = EQINFO_TYPE_DOCKING_LIGHTS;
        DockEquip.EquipId = (dock_host_response == 4 || dock_host_response == 3) ? 1 : 0; // a byte in retail
        ActivateEquipment(ship_id, &DockEquip);                          // result ignored

        if (dock_host_response == 3) return Result;

        CObject* const Raw = Obj->Object;
        if (Raw != nullptr &&
            (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip) {
            CShip* const Cship = static_cast<CShip*>(Raw);
            for (std::vector<FObjRWWatcher>::const_iterator Entry = Cship->FollowFollowers.begin();
                 Entry != Cship->FollowFollowers.end(); ++Entry) {
                IObjRW* const Follower = Entry->get();
                if (Follower == nullptr) continue;
                IBehaviorManager* const Manager = GetBehaviorManager(Follower);
                if (Manager == nullptr) continue;
                GameObject* const Target = Server_LookupLiveObject(target_id);
                if (Target == nullptr) continue;

                // LAB_062d3f90's case 5 reads [+0] / [+4] & 0x120 / [+8] / byte [+0xc].
                struct {
                    unsigned int TargetId;   // +0x00
                    unsigned int TargetType; // +0x04 (get_type out)
                    unsigned int ShipId;     // +0x08
                    unsigned char bHasPort;  // +0x0C
                    unsigned char Pad0d[3];
                } Block;
                memset(&Block, 0, sizeof(Block));
                Block.ShipId = ship_id;
                Block.TargetId = target_id;
                Target->get_type(Block.TargetType);
                Block.bHasPort = (dock_port != -1) ? 1 : 0;
                FBehaviorManagerEvent Event;
                Event.Type = 5;
                Event.Data = &Block;
                Manager->OnEvent(&Event);
            }
        }
    }
    if (dock_host_response == 3) return Result;

    GameObject* const Host = Server_LookupLiveObject(target_id);
    if (Host == nullptr) return -3;
    ::EqObj* const HostEq = dynamic_cast< ::EqObj*>(Host);
    if (dock_port >= 0)
        HostEq->queue_dock_request(dock_port, ship_id);                  // FUN_06ce7880
    else
        HostEq->dequeue_dock_request(dock_port, ship_id);                // FUN_06ce78d0
    return 0;
}

// SpaceObj::InstantDock -- retail .text:06cfa8d0 (ordinal 0x1a8d0, cdecl). Past the mask
// checks it always returns -2. The Ship cast is not tested and Server_FindLiveObject's
// result is ignored. The owning system is not null-tested on either call (null for an
// in-transit host; retail dereferences it the same way). The docking-sphere index is
// unchecked; types 3/4/5 (moor_small/medium/large) are moors, everything else lands.
SERVER_API int __cdecl InstantDock(const unsigned int& ship_id, const unsigned int& target_id,
                                    int dock_port) {
    if ((ship_id & 0x3fffffffu) == 0 || (target_id & 0x3fffffffu) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(ship_id);
    if (Obj == nullptr) return -2;

    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);
    GameObject* Host = nullptr;
    StarSystem* Owner = nullptr;
    Server_FindLiveObject(target_id, Host, Owner);
    ::EqObj* const HostEq = dynamic_cast< ::EqObj*>(Host);
    if (HostEq == nullptr) return -2;

    unsigned int const Flags = HostEq->get_type_bits();
    if (Flags == (Flags & 0xc40u)) {
        Owner->complete_dock(ShipRw, HostEq);                            // FUN_06d0c970
        return -2;
    }

    CEqObj* const HostCeq = HostEq->ceqobj();
    Archetype::EqObj const* const Arch =
        static_cast<Archetype::EqObj const*>(HostCeq->Archetype);
    unsigned int const Kind = Arch->DockingSpheres[dock_port].Type;
    bool const bLand = !(Kind == 3 || Kind == 4 || Kind == 5);
    Owner->queue_dock(ShipRw, HostEq, &HostCeq->DockingPointBaseId, &bLand); // FUN_06d0d370
    return -2;
}

// The PhySys::CreateParms that JumpIn, Relocate and Launch each build inline before a
// system transfer. Deviation: retail leaves Name[1..7] and Pad27 unwritten; the port
// zeroes the whole block first.
static void SpaceObj_InitTransferParms(PhySys::CreateParms& Parms)
{
    memset(&Parms, 0, sizeof(Parms));
    Parms.Mass = 1.0f;
    Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
    Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Parms.SystemId = 0;
    Parms.Type = 2;
    Parms.bPhysicalUnmoveable = 0;
    Parms.bEnableCollisions = 1;
    Parms.Name[0] = 0;
    Parms.bHasInertia = 0;
    Parms.Inertia.x = 0.0f;
    Parms.Inertia.y = 0.0f;
    Parms.Inertia.z = 0.0f;
}

// SpaceObj::Launch -- retail .text:06cfa520 (ordinal 0x1a520, cdecl). Neither cast is
// tested. Across systems the ship is moved to the dock's system at the origin with an
// identity orientation before StarSystem::launch (FUN_06d0cee0).
SERVER_API int __cdecl Launch(const unsigned int& launch_obj_id, const unsigned int& dock_obj_id,
                               int launch_type) {
    if ((launch_obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* LaunchObj = nullptr;
    StarSystem* LaunchSystem = nullptr;
    if (!Server_FindLiveObject(launch_obj_id, LaunchObj, LaunchSystem))
        return -2;
    GameObject* DockObj = nullptr;
    StarSystem* DockSystem = nullptr;
    if (!Server_FindLiveObject(dock_obj_id, DockObj, DockSystem))
        return -3;

    ::EqObj* const DockEq = dynamic_cast< ::EqObj*>(DockObj);
    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(LaunchObj);

    if (DockSystem != LaunchSystem) {
        PhySys::CreateParms Parms;
        SpaceObj_InitTransferParms(Parms);
        LaunchSystem->remove_ship(ShipRw, &Parms);                       // FUN_06d0d000
        Vector Pos;
        Pos.x = 0.0f;
        Pos.y = 0.0f;
        Pos.z = 0.0f;
        Matrix const Orient(1.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f, 1.0f);
        DockSystem->add_ship(ShipRw, &Parms, Pos, Orient);               // FUN_06d0d100
    }
    DockSystem->launch(ShipRw, DockEq, launch_type);
    return 0;
}

// SpaceObj::JumpIn -- retail .text:06cfa220 (ordinal 0x1a220, cdecl). The arrival location
// comes from gate vtable +0x250 (Solar::ChooseSystemArrivalLocation), called on whatever
// the gate object is. An in-transit ship goes through Server_TakeInTransit (FUN_06d00380).
SERVER_API int __cdecl JumpIn(const unsigned int& ship_id, const unsigned int& gate_id) {
    if ((ship_id & 0x3fffffffu) == 0) return -4;
    GameObject* ShipObj = nullptr;
    StarSystem* ShipSystem = nullptr;
    if (!Server_FindLiveObject(ship_id, ShipObj, ShipSystem))
        return -2;
    GameObject* GateObj = nullptr;
    StarSystem* GateSystem = nullptr;
    if (!Server_FindLiveObject(gate_id, GateObj, GateSystem))
        return -3;

    PhySys::CreateParms Parms;
    SpaceObj_InitTransferParms(Parms);
    if (ShipSystem == nullptr)
        Server_TakeInTransit(ShipObj, Parms);
    else if (GateSystem != ShipSystem)
        ShipSystem->remove_ship(ShipObj, &Parms);

    Vector Pos;
    Matrix Orient;
    static_cast<Solar*>(GateObj)->ChooseSystemArrivalLocation(ShipObj, Pos, Orient);
    if (GateSystem != ShipSystem)
        GateSystem->add_ship(ShipObj, &Parms, Pos, Orient);
    GateSystem->arrive_ship(ShipObj, GateObj, Pos, Orient);             // FUN_06d0cc00
    return 0;
}

// SpaceObj::Relocate -- retail .text:06cfa370 (ordinal 0x1a370, cdecl). A zero system id
// parks the object in transit (Server_PushInTransit, FUN_06d00560). Within the same
// system the raw CObject (CBase::Object under the (& 1) test, not null-tested after) is
// beamed via vtable +0x7c. Otherwise it is added to the target system and a CShip gets
// set_control_exclusion(0). Always returns 0 past the lookup.
SERVER_API int __cdecl Relocate(const unsigned int& obj_id, const unsigned int& system_id,
                                 const Vector& pos, const Matrix& orient) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* OwnSystem = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, OwnSystem))
        return -2;

    StarSystem* TargetSystem = nullptr;
    if ((system_id & 0x3fffffffu) != 0)
        TargetSystem = SysDB_LookupSystem(system_id);

    PhySys::CreateParms Parms;
    SpaceObj_InitTransferParms(Parms);
    if (OwnSystem == nullptr)
        Server_TakeInTransit(Obj, Parms);
    else if (TargetSystem != OwnSystem)
        OwnSystem->remove_ship(Obj, &Parms);

    if (TargetSystem == nullptr) {
        Server_PushInTransit(Obj, Parms);
        return 0;
    }
    if (TargetSystem == OwnSystem) {
        CObject* Raw = Obj->Object;
        if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 1u) != 1u)
            Raw = nullptr;
        Raw->beam_object(pos, orient, false);
        return 0;
    }
    TargetSystem->add_ship(Obj, &Parms, pos, orient);
    CObject* const ShipRaw = Obj->Object;
    if (ShipRaw != nullptr &&
        (static_cast<unsigned int>(ShipRaw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
        static_cast<CShip*>(ShipRaw)->set_control_exclusion(0);
    return 0;
}

// SpaceObj::SendComm -- retail .text:06cf9230 (ordinal 401, cdecl). The system comes from
// the target, else from the sender (it is the system that is kept and tested, not the
// object). A CEqObj sender supplies the default voice (+0x13c) and costume (+0x108), and
// whenever that branch is taken caption_ids is zeroed. send_comm is FUN_06d0db30.
SERVER_API int __cdecl SendComm(unsigned int sender_id, unsigned int target_id,
                                 unsigned int voice_id, const Costume* costume,
                                 unsigned int caption_ids, unsigned int* text_ids,
                                 int text_count, unsigned int permutation_ids,
                                 float duration, bool interrupt) {
    if (text_ids == nullptr) return -4;
    if (text_count <= 0) return -4;                                       // signed

    GameObject* LookupObj = nullptr;
    StarSystem* System = nullptr;
    if ((target_id & 0x3fffffffu) != 0) {
        if (!Server_FindObject(target_id, LookupObj, System))
            System = nullptr;
    }
    if (System == nullptr) {
        if ((sender_id & 0x3fffffffu) == 0) return -4;
        if (!Server_FindObject(sender_id, LookupObj, System))
            System = nullptr;
        if (System == nullptr) return -4;
    }

    unsigned int Voice = voice_id;
    const Costume* Cost = costume;
    unsigned int CaptionIds = caption_ids;
    if ((sender_id & 0x3fffffffu) != 0) {
        CBase* const SenderBase = Server_LookupLiveObject(sender_id);
        if (SenderBase != nullptr) {
            CObject* const Cobj = SenderBase->Object;
            if (Cobj != nullptr &&
                (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj) {
                CEqObj* const Eq = static_cast<CEqObj*>(Cobj);
                if (Voice == 0)
                    Voice = Eq->VoiceId;
                if (costume == nullptr)
                    Cost = &Eq->SpaceCostume;
                CaptionIds = 0;
            }
        }
    }
    if (Cost == nullptr) return -2;

    System->send_comm(sender_id, target_id, Voice, *Cost, CaptionIds, text_ids, text_count,
                      permutation_ids, duration, interrupt);
    return 0;
}

// SpaceObj::JettisonEquipment -- retail 0x19390. No id guard: a failed lookup returns -2.
// IObjAffect slot 14 (jettison_cargo); 0 on success, -3 when it reports -2.
SERVER_API int __cdecl JettisonEquipment(const unsigned int& obj_id,
                                          const unsigned short& equip_id, const int& quantity) {
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    int const Result = obj->jettison_cargo(equip_id, quantity, 0);
    return (Result != -2) ? 0 : -3;
}

// SpaceObj::GetArchetypeID -- Good2Arch with no entry guard; the out-param is written on
// failure too.
SERVER_API int __cdecl GetArchetypeID(const unsigned int& good_id, unsigned int& out_arch_id) {
    unsigned int arch = Good2Arch(good_id);
    out_arch_id = arch;
    return (arch != 0) ? 0 : -2;
}

// SpaceObj::GetAtmosphereRange -- retail 0x19800. Planets and suns only
// ((ObjectClass & 0x303) == 0x303).
SERVER_API int __cdecl GetAtmosphereRange(const unsigned int& obj_id, float& out_range) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar) {
        out_range = 0.0f;
        return 0;
    }
    CSolar* const Cs = static_cast<CSolar*>(Raw);
    float atmo = Cs->AtmosphereRange;   // CSolar+0x1ac
    out_range = atmo;
    // A positive range is reported minus the object's radius (EngineObject+0x38).
    if (atmo > 0.0f) {
        out_range = atmo - Cs->Radius;
    }
    return 0;
}

// SpaceObj::GetScannerRange -- ships only ((ObjectClass & 0x503) == 0x503). No scanner
// returns -2 like a failed lookup; a destroyed scanner returns -3; outs untouched on both.
SERVER_API int __cdecl GetScannerRange(unsigned int obj_id, int& radar_range, int& cargo_scan_range) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return -1;
    CShip* const Cs = static_cast<CShip*>(Raw);
    CEScanner const* const Scanner =
        static_cast<CEScanner const*>(Cs->EquipManager.FindFirst(EquipTypeScanner));
    if (Scanner == nullptr) return -2;
    // Scanner vtable +0x08 is CEquip::IsDestroyed (Common 0x06286d10), not IsFunctioning.
    if (Scanner->IsDestroyed()) return -3;
    radar_range = (int)(long)Scanner->GetRadarRange();
    cargo_scan_range = (int)(long)Scanner->GetCargoScanRange();
    return 0;
}

// SpaceObj::GetType -- retail 0x18da0. out_type is zeroed before the id test, so every
// failure path returns it zeroed.
SERVER_API int __cdecl GetType(unsigned int obj_id, unsigned int& out_type) {
    out_type = 0;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id);
    if (!obj) return -2;
    // csimple() is retail's (ObjectClass & 3) == 3 guard; TypeBits is CSimple+0xe0.
    CSimple* Cs = obj->csimple();
    if (Cs == nullptr) return 0;
    out_type = Cs->get_type();
    return 0;
}

// SpaceObj::Activate -- retail .text:06cf9ac0 (cdecl). Deactivating an object whose type
// has 0x400 set is a no-op returning 0. The owner system is used without a null test.
SERVER_API int __cdecl Activate(const unsigned int& obj_id, bool activate, int param) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* Owner = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, Owner)) return -2;
    unsigned int Flags;
    Obj->get_type(Flags);
    if (!activate && (Flags & 0x400u) != 0) return 0;
    return Owner->activate_object(Obj, activate, static_cast<unsigned int>(param)) ? 0 : -3; // FUN_06d0d8a0
}

// SpaceObj::ActivateEquipment -- retail .text:06cf93f0 (ordinal 0x193f0, cdecl); switch
// table at .text:06cf95d8. Type 0 = cruise (IObjAffect +0x50), 1 = scan cargo
// (IObjInspect +0x120), 2 = the first cloak (FindFirst(0x1000)), 3 = every docking light
// (CELightEquip arch +0x71). The EQUIPMENT_RTYPE names are the port's; the values
// (-5/-2/-1/0) are retail's.
SERVER_API EQUIPMENT_RTYPE __cdecl ActivateEquipment(const unsigned int& obj_id_ref, EQInfo* eq_info) {
    if ((obj_id_ref & 0x3fffffffu) == 0) return EQUIPMENT_RTYPE_BAD_TYPE;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindObject(obj_id_ref, Obj, System))                     // system out unused
        return EQUIPMENT_RTYPE_INVALID;
    if (Obj == nullptr) return EQUIPMENT_RTYPE_INVALID;

    switch (eq_info->Type) {                                             // unsigned > 3 -> -1
    case EQINFO_TYPE_CRUISE:
        if (eq_info->Count != 1) return EQUIPMENT_RTYPE_INVALID;
        return Obj->toggle_cruise(true, true, 0) != 0 ? EQUIPMENT_RTYPE_BAD_TYPE
                                                 : EQUIPMENT_RTYPE_OK;
    case EQINFO_TYPE_SCAN_CARGO: {
        if (eq_info->TargetObjId == 0) return EQUIPMENT_RTYPE_INVALID;
        GameObject* const Other = Server_LookupLiveObject(eq_info->TargetObjId);
        if (Other == nullptr) return EQUIPMENT_RTYPE_BAD_TYPE;
        EquipDescVector& Cargo = *reinterpret_cast<EquipDescVector*>(eq_info->CargoOut);
        return Obj->scan_cargo(Other, Cargo) != 0 ? EQUIPMENT_RTYPE_BAD_TYPE
                                                   : EQUIPMENT_RTYPE_OK;
    }
    case EQINFO_TYPE_CLOAK: {
        CObject* const Raw = Obj->Object;
        if (Raw == nullptr ||
            (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
            return EQUIPMENT_RTYPE_INVALID;
        CEqObj* const Ceq = static_cast<CEqObj*>(Raw);
        CEquip const* const Cloak = Ceq->EquipManager.FindFirst(EquipTypeCloak);
        if (Cloak == nullptr) return EQUIPMENT_RTYPE_NOT_FOUND;
        unsigned short const SubObjId = Cloak->Id;
        Obj->toggle_item(&SubObjId, static_cast<unsigned char>(eq_info->EquipId) != 0, 0);
        return EQUIPMENT_RTYPE_OK;
    }
    case EQINFO_TYPE_DOCKING_LIGHTS: {
        CObject* const Raw = Obj->Object;
        if (Raw == nullptr ||
            (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
            return EQUIPMENT_RTYPE_INVALID;
        CEqObj* const Ceq = static_cast<CEqObj*>(Raw);
        CEquipTraverser Walk(EquipTypeLight);
        CEquip* Light = Ceq->EquipManager.Traverse(Walk);
        while (Light != nullptr) {
            if (static_cast<CELightEquip*>(Light)->LightArch()->bDockingLight) {
                unsigned short const SubObjId = Light->Id;
                Obj->toggle_item(&SubObjId, static_cast<unsigned char>(eq_info->EquipId) != 0, 0);
            }
            Light = Ceq->EquipManager.Traverse(Walk);
        }
        return EQUIPMENT_RTYPE_OK;
    }
    default:
        return EQUIPMENT_RTYPE_INVALID;
    }
}

// SpaceObj::SetInvincible -- retail .text:06cf8700 (ordinal 419, cdecl). obj_id is tested
// against 0 as a whole dword, not masked. An in-transit object (null system) returns -2.
// Deviation: retail never writes the two pad bytes (+6/+7); the port zeroes them.
SERVER_API int __cdecl SetInvincible(unsigned int obj_id, bool invincible, bool param3, float duration) {
    if (obj_id == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System)) return -2;
    if (System == nullptr) return -2;

    FInvincibility Cmd;
    Cmd.Type = 3;
    Cmd.Pad06[0] = 0;
    Cmd.Pad06[1] = 0;
    if (invincible) {
        Cmd.bProtect = true;
        Cmd.bFromPlayers = !param3;
        Cmd.MaxHpLoss = duration;
    } else {
        Cmd.bProtect = false;
        Cmd.bFromPlayers = false;
        Cmd.MaxHpLoss = 1.0f;
    }
    Obj->set_invincible(Cmd);                                            // vtable +0x194
    return 0;
}

// SpaceObj::AddImpulse -- retail 0x190b0. The PhySys call is gated on the physics body
// (CObject+0x54); returns 0 either way.
SERVER_API int __cdecl AddImpulse(const unsigned int& obj_id_ref, const Vector& impulse, const Vector& point) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    CObject* Cobj = obj->cobj();
    if (Cobj == nullptr) return 0;
    if (Cobj->BodyPtr != nullptr) {
        PhySys::AddImpulseAtPoint(Cobj, impulse, point);
    }
    return 0;
}

// SpaceObj::GetLocation -- retail .text:06cf8a80 (FUN_06d00670 lookup). The orientation
// is the Rotation of get_transform (IObjInspect slot 3; slots 3 and 4 are ICF-folded in
// retail).
SERVER_API int __cdecl GetLocation(unsigned int obj_id, Vector& out_pos, Matrix& out_orient) {
    out_pos.x = 0.0f; out_pos.y = 0.0f; out_pos.z = 0.0f;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id);
    if (!obj) return -2;
    out_pos = obj->get_position();
    out_orient = obj->get_transform().Rotation;
    return 0;
}

// SpaceObj::GetSystem -- retail .text:06cf8e00 (cdecl). The id is the system's
// (FUN_06d06cd0, SystemInfo vtable slot 0); an in-transit object reports 0 and returns 0.
SERVER_API int __cdecl GetSystem(unsigned int obj_id, unsigned int& out_system_id) {
    out_system_id = 0;
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System)) return -2;
    if (System == nullptr) {
        out_system_id = 0;
        return 0;
    }
    out_system_id = System->SystemInfo->NicknameId;
    return 0;
}

// SpaceObj::GetHealth -- retail .text:06cfab50 (FUN_06d00670 lookup). One IObjInspect slot 15
// (+0x3c) call fills both outs.
SERVER_API int __cdecl GetHealth(const unsigned int& obj_id_ref, float& out_current, float& out_max) {
    out_current = 0.0f; out_max = 0.0f;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    obj->get_status(out_current, out_max);
    return 0;
}

// SpaceObj::GetRelativeHealth -- retail .text:06cfaab0 (FUN_06d00670 lookup), IObjInspect slot 16
// (+0x40). Retail tests the id against 0 unmasked.
SERVER_API int __cdecl GetRelativeHealth(const unsigned int& obj_id_ref, float& out_ratio) {
    out_ratio = 0.0f;
    if (obj_id_ref == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    obj->get_status(out_ratio);
    return 0;
}

// SpaceObj::ExistsAndAlive -- retail .text:06cf8870. The liveness half of FUN_06d00670 is the
// point; a plain lookup hit is not enough.
SERVER_API int __cdecl ExistsAndAlive(unsigned int obj_id) {
    if ((obj_id & 0x3fffffff) == 0) return -2;
    GameObject* obj = Server_LookupLiveObject(obj_id);
    if (!obj) return -2;
    return 0;
}

// SpaceObj::GetTarget -- retail .text:06cf9120 (FUN_06d00670 lookup). Ships only
// ((ObjectClass & 0x503) == 0x503); CShip::get_target, then the target's get_id (+0x20).
// Deviation: retail calls CShip::get_target on the zeroed narrowing for a non-ship (null
// deref at [0x220]); the port returns -1 instead.
SERVER_API int __cdecl GetTarget(const unsigned int& obj_id_ref, unsigned int& out_target_id) {
    out_target_id = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return -1;
    IObjRW* const Target = static_cast<CShip*>(Raw)->get_target();
    if (Target == nullptr) {
        out_target_id = 0;
        return 0;
    }
    out_target_id = Target->get_id();
    return 0;
}

// SpaceObj::CreateSolar -- retail .text:06cf80d0 (ordinal 0x180d0, cdecl). The system
// lookup fails before the request exists. The id is CreateID(nickname), written to out_id
// before the archetype is touched; no null test on the solar archetype.
// StarSystem::create_solar is FUN_06d0a200; a null Solar gives -1.
SERVER_API int __cdecl CreateSolar(unsigned int& out_id, const SpaceObj::SolarInfo& solar_info) {
    FStarSystem* const System = SysDB_LookupSystem(solar_info.SystemID);
    if (System == nullptr) return -4;

    CSolarCreateInfo Request;
    Request.NicknameSrc.clear();
    Request.MarkPlayerId = 0u;
    int Result;
    if (solar_info.Nickname[0] == '\0') {
        Result = -4;
    } else {
        unsigned int const Id = CreateID(solar_info.Nickname);
        out_id = Id;
        Request.NicknameSrc.Id = Id;
        Request.ObjId = Id;
        Request.HitPts = solar_info.HitPts;
        Request.DockingBaseId = solar_info.DockingBaseId;
        Request.ArchetypeNickname = solar_info.SolarArchID;
        Request.CostumeBody = solar_info.CostumeBody;
        Request.CostumeHead = solar_info.CostumeHead;
        Request.CostumeRightHand = solar_info.CostumeRightHand;
        Request.CostumeLeftHand = solar_info.CostumeLeftHand;
        Request.CostumeAccessoryCount = solar_info.CostumeAccessoryCount;
        for (int Index = 0; Index < Request.CostumeAccessoryCount; ++Index)
            Request.CostumeAccessory[Index] = solar_info.CostumeAccessory[Index];
        Request.VoiceId = solar_info.VoiceId;
        Request.VibeGroupId = solar_info.VibeGroupId;
        Request.Position = solar_info.Position;
        Quaternion SolarOrient;
        Server_MatrixToQuat(SolarOrient, solar_info.Orientation);
        Request.Orientation = SolarOrient;
        Request.LoadoutId = solar_info.LoadoutID;
        Archetype::Solar* const SolarArch = Archetype::GetSolar(solar_info.SolarArchID);
        SolarArch->get_undamaged_collision_group_list(Request.CollisionGroups);
        Request.MarkPlayerId = solar_info.MarkPlayerId;
        Request.VisitValue = solar_info.VisitValue;
        Request.bDynamic = 1u;
        Request.bDestructible = 1u;
        ::Solar* const Created = System->create_solar(Request);
        Result = (Created != nullptr) ? 0 : -1;
    }
    return Result;
}

// SpaceObj::GetMotion -- retail 0x18b20 (FUN_06d00670 lookup), IObjInspect slots 1 and 2.
SERVER_API int __cdecl GetMotion(unsigned int obj_id, Vector& out_linear, Vector& out_angular) {

    out_linear.x = 0; out_linear.y = 0; out_linear.z = 0;
    out_angular.x = 0; out_angular.y = 0; out_angular.z = 0;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id);
    if (!obj) return -2;
    out_linear = obj->get_velocity();
    out_angular = obj->get_angular_velocity();
    return 0;
}

// SpaceObj::GetRep -- retail 0x18ba0 (FUN_06d00670 lookup). CEqObj only
// ((ObjectClass & 0x103) == 0x103), vibe at +0x104. out_rep is written only on success;
// Content callers preset it (often -1) and rely on that.
SERVER_API int __cdecl GetRep(unsigned int obj_id, int& out_rep) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -1;
    out_rep = (int)static_cast<CEqObj*>(Raw)->VibeId;
    return 0;
}

// SpaceObj::GetSolarRep -- retail 0x18c00. Echoes the vibe back when Vibe::Verify passes.
SERVER_API int __cdecl GetSolarRep(unsigned int vibe_id, int& out_rep) {
    bool valid = GlobalReputation::Vibe::Verify(vibe_id);
    if (valid) {
        out_rep = (int)vibe_id;
        return 0;
    }
    return -2;
}

// SpaceObj::GetSolarArchetypeID -- retail 0x18c30 (FUN_06d00670 lookup). CEqObj only; the
// id is the archetype's (CObject+0x88) Root::Nickname.Id; the archetype is not null-tested.
SERVER_API int __cdecl GetSolarArchetypeID(unsigned int obj_id, unsigned int& out_arch_id) {
    out_arch_id = 0;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -1;
    out_arch_id = Raw->Archetype->get_id();
    return 0;
}

// SpaceObj::GetSolarArchetypeNickname -- retail 0x18ca0: Archetype::GetSolar, the
// Root::Nickname (+8) string, safe_strcpy.
SERVER_API int __cdecl GetSolarArchetypeNickname(char* out_name, int max_len, unsigned int arch_id) {
    if (!out_name || max_len <= 0) return -4;
    Archetype::Solar* solar = Archetype::GetSolar(arch_id);
    if (!solar) return -2;
    const char* nick = solar->Nickname.get_string();
    if (!nick) return -2;
    int len = 0;
    while (nick[len] != '\0') len++;
    if (len < max_len) {
        for (int i = 0; i <= len; i++) out_name[i] = nick[i];
        return 0;
    }
    return -1;
}

// SpaceObj::GetSolarParent -- retail 0x18d00 (FUN_06d00670 lookup). Solars only
// ((ObjectClass & 0x303) == 0x303 on IObjInspect::cobject, +0x150); the id test is
// unmasked. The parent nickname id is the object id, so a second live lookup doubles as
// the existence check.
SERVER_API int __cdecl GetSolarParent(const unsigned int& obj_id_ref, unsigned int& out_parent_id) {
    out_parent_id = 0;
    if (obj_id_ref == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject const* const Raw = Obj->cobject();
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        return -1;
    CSolar const* const Cs = static_cast<CSolar const*>(Raw);
    ID_String const& Parent = Cs->GetParentNickname();
    if (Parent.IsEmpty()) return -3;
    GameObject* const ParentObj = Server_LookupLiveObject(Parent.Id);
    if (ParentObj == nullptr) return -3;
    out_parent_id = ParentObj->get_id();
    return 0;
}

// SpaceObj::GetHardpoint -- retail 0x18e70 (FUN_06d00670 lookup). get_index (IObjInspect
// slot 7) is the engine instance handle passed to HardpointPosition.
SERVER_API int __cdecl GetHardpoint(const unsigned int& obj_id_ref, const char* hp_name,
                                     Vector* out_pos, Matrix* out_orient) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    bool bOk = HardpointPosition(obj->get_index(), hp_name, out_pos, out_orient);
    return bOk ? 0 : -3;
}

// SpaceObj::GetRadius -- retail 0x18ed0 (FUN_06d00670 lookup). One IObjInspect slot 11
// (+0x2c) call fills radius and centre.
SERVER_API int __cdecl GetRadius(const unsigned int& obj_id_ref, float& out_radius, Vector& out_center) {
    out_radius = 0.0f;
    out_center.x = 0; out_center.y = 0; out_center.z = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    obj->get_physical_radius(out_radius, out_center);
    return 0;
}

// SpaceObj::GetBurnRadius -- retail 0x18f60 (FUN_06d00670 lookup). "Burn radius" is
// IObjInspect slot 80 (+0x140), get_atmosphere_range.
SERVER_API int __cdecl GetBurnRadius(const unsigned int& obj_id_ref, float& out_radius, Vector& out_center) {
    out_radius = 0.0f;
    out_center.x = 0; out_center.y = 0; out_center.z = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    out_center = obj->get_position();
    obj->get_atmosphere_range(out_radius);
    return 0;
}

// SpaceObj::GetMass -- retail 0x19000 (FUN_06d00670 lookup), IObjInspect slot 12.
SERVER_API int __cdecl GetMass(const unsigned int& obj_id_ref, float& out_mass) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    out_mass = obj->get_mass();
    return 0;
}

// SpaceObj::GetCenterOfMass -- retail 0x19040 (FUN_06d00670 lookup). The +0x68 slot is on
// the CObject's own vtable (CObject::get_center_of_mass), not IObjInspect's.
SERVER_API int __cdecl GetCenterOfMass(const unsigned int& obj_id_ref, Vector& out_center) {

    out_center.x = 0; out_center.y = 0; out_center.z = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    // Deviation: retail calls through the CObject even when the (& 3) check zeroed it
    // (null deref); the port returns -2 instead.
    CObject* Cobj = obj->cobj();
    if (Cobj == nullptr) return -2;
    out_center = Cobj->get_center_of_mass();
    return 0;
}

// SpaceObj::GetVoiceID -- retail .text:06cf91a0. The id test is unmasked. A plain lookup
// (FUN_06d006c0) miss is -2; then FUN_06d00670, whose reject of a destroyed object is -3;
// a non-CEqObj is -4 (EDI keeps its initial 0xfffffffc, .text:06cf91bf). Voice at +0x13c.
SERVER_API int __cdecl GetVoiceID(const unsigned int& obj_id_ref, unsigned int& out_voice_id) {
    if (obj_id_ref == 0) return -4;
    GameObject* Found = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindObject(obj_id_ref, Found, System) || Found == nullptr) return -2;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -3;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -4;
    out_voice_id = static_cast<CEqObj*>(Raw)->VoiceId;    // CEqObj+0x13c
    return 0;
}

// SpaceObj::GetInvincible -- retail .text:06cf88a0. Live lookup; the id test is unmasked.
SERVER_API int __cdecl GetInvincible(unsigned int ObjId, unsigned char& OutFromPlayers,
                                     unsigned char& OutProtect, float& OutMaxHpLoss) {
    if (ObjId == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(ObjId, Obj, System)) return -2;
    if (System == nullptr) return -2;
    OutFromPlayers = Obj->get_invincible_flag();  // +0x198 -> raw byte +0x39
    OutProtect = Obj->is_invincible();            // +0x19c -> raw byte +0x38
    OutMaxHpLoss = Obj->get_max_hp_loss();   // +0x1a0 -> float +0x3c
    return 0;
}

// SpaceObj::GetCargoSpaceOccupied -- retail .text:06cf96d0 (0x196d0). HoldSize (+0x100)
// minus get_cargo_hold_remaining, unrounded, then ftol (the decompile hides the FSUBR).
// Retail does not null-test the CShip narrowing before reading its archetype; nor does
// the port.
SERVER_API int __cdecl GetCargoSpaceOccupied(const unsigned int& obj_id_ref, unsigned int& out_occupied) {
    out_occupied = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->cobj();
    CShip* const ShipObj =
        (Raw != nullptr && ((unsigned int)Raw->ObjectClass & CObject::ClassShip) == CObject::ClassShip) ? (CShip*)Raw : nullptr;
    Archetype::Ship const* const Arch = (Archetype::Ship const*)ShipObj->Archetype;
    double const Remaining = ShipObj->get_cargo_hold_remaining();
    out_occupied = (unsigned int)(long long)(Arch->HoldSize - Remaining);
    return 0;
}

// SpaceObj::GetDockingTarget -- retail 0x19880 (FUN_06d00670 lookup). CEqObj only; prefers
// DockingPointBaseId (+0x164) and falls back to DockingBaseId (+0x160).
SERVER_API int __cdecl GetDockingTarget(const unsigned int& obj_id_ref, unsigned int& out_target) {
    out_target = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj) {
        out_target = 0;
        return 0;
    }
    CEqObj* const Ceq = static_cast<CEqObj*>(Raw);
    unsigned int secondary = Ceq->DockingPointBaseId;
    if ((secondary & 0x3fffffff) != 0) {
        out_target = secondary;
    } else {
        out_target = Ceq->DockingBaseId;
    }
    return 0;
}

// SpaceObj::SetRelativeHealth -- retail .text:06cfaaf0 (cdecl). The range tests reject
// NaN as well (unordered compares). The id test is unmasked. vtable +0x1ac.
SERVER_API int __cdecl SetRelativeHealth(const unsigned int& obj_id_ref, float ratio) {
    if (!(ratio >= 0.0f)) return -4;
    if (!(ratio <= 1.0f)) return -4;
    if (obj_id_ref == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    Obj->set_relative_health(ratio);
    return 0;
}

// SpaceObj::GetShieldHealth -- retail 0x1abb0 (FUN_06d00670 lookup). One IObjInspect
// slot 17 (+0x44) call fills all three outs.
SERVER_API int __cdecl GetShieldHealth(const unsigned int& obj_id_ref, float& out_current,
                                        float& out_max, bool& out_active) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    obj->get_shield_status(out_current, out_max, out_active);
    return 0;
}

// SpaceObj::GetTradelaneNextAndPrev -- retail 0x1ac00 (FUN_06d00670 lookup). Solars only
// ((ObjectClass & 0x303) == 0x303).
SERVER_API int __cdecl GetTradelaneNextAndPrev(const unsigned int& obj_id_ref,
                                                unsigned int& out_next, unsigned int& out_prev) {

    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        return -1;
    CSolar* const Cs = static_cast<CSolar*>(Raw);
    out_next = Cs->NextRing;   // CSolar+0x1dc
    out_prev = Cs->PrevRing;   // CSolar+0x1d8
    return 0;
}

// SpaceObj::LightFuse -- retail .text:06cfac70 (cdecl). The id test is unmasked.
// light_fuse is EqObj's vtable +0x1e4, called on the looked-up wrapper without
// re-narrowing.
SERVER_API int __cdecl LightFuse(const unsigned int& obj_id_ref, const char* fuse_name, float delay) {
    if (obj_id_ref == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    ID_String FuseId;
    FuseId.Id = CreateID(fuse_name);
    bool const bLit = static_cast< ::EqObj*>(Obj)->light_fuse(0, FuseId, SUBOBJ_ID_NONE,
                                                              delay, -1.0f);
    return bLit ? 0 : -4;
}

// SpaceObj::GetTerminalInfo -- retail .text:06cf9900 (cdecl). A zero id returns -2, not
// -4. The archetype (+0x88) is not null-tested. Retail's null-begin and index tests on the
// docking-sphere vector collapse to one size test. Name is the sphere's hardpoint string
// (+0x04), Value its type (+0x0c).
SERVER_API int __cdecl GetTerminalInfo(const unsigned int& obj_id_ref, int index, TerminalInfo& out_info) {
    if ((obj_id_ref & 0x3fffffffu) == 0) return -2;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Cobj = Obj->Object;
    if (Cobj == nullptr ||
        (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj) return -2;
    Archetype::EqObj const* const Arch =
        static_cast<Archetype::EqObj const*>(Cobj->Archetype);
    if (static_cast<unsigned int>(index) >= Arch->DockingSpheres.size()) return -3;
    Archetype::EqObj::DockingSphere const& Sphere = Arch->DockingSpheres[index];
    safe_strcpy(out_info.Name, 0x20, Sphere.HardpointName.c_str());
    out_info.Value = Sphere.Type;
    return 0;
}

// SpaceObj::GetGoodID -- retail 0x199a0. Loot only ((ObjectClass & 0x803) == 0x803); the
// good comes from CLoot::ContentsArch (CObject+0xec), not null-tested. A zero id returns -2.
SERVER_API int __cdecl GetGoodID(const unsigned int& obj_id_ref, unsigned int& out_good_id) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -2;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassLoot) != CObject::ClassLoot)
        return -1;
    CLoot* const Cl = static_cast<CLoot*>(Raw);
    out_good_id = Arch2Good(Cl->ContentsArch->get_id());
    return 0;
}

// SpaceObj::GetJumpTarget -- retail 0x19a40. Solars only ((ObjectClass & 0x303) == 0x303)
// whose TypeBits (+0xe0) lie within 0xc40 (jump gate/hole).
SERVER_API int __cdecl GetJumpTarget(const unsigned int& obj_id_ref, unsigned int& out_target1, unsigned int& out_target2) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        return -1;
    CSolar* const Cs = static_cast<CSolar*>(Raw);
    unsigned int otype = Cs->get_type();
    if ((otype & 0xc40) != otype) return -1;
    out_target1 = Cs->DestSystem;    // CSolar+0x1b4
    out_target2 = Cs->DestGate.Id;   // CSolar+0x1b8 (ID_String)
    return 0;
}

// SpaceObj::SetInvincible2 -- retail .text:06cf87f0 (ordinal 418, cdecl). As SetInvincible
// but with no branch and param3 not inverted. Pad bytes zeroed by the port (never written
// in retail).
SERVER_API int __cdecl SetInvincible2(unsigned int obj_id, unsigned char invincible, unsigned char param3, float duration) {
    if (obj_id == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System)) return -2;
    if (System == nullptr) return -2;

    FInvincibility Cmd;
    Cmd.Type = 3;
    Cmd.bProtect = invincible;
    Cmd.bFromPlayers = param3;
    Cmd.Pad06[0] = 0;
    Cmd.Pad06[1] = 0;
    Cmd.MaxHpLoss = duration;
    Obj->set_invincible(Cmd);
    return 0;
}

// SpaceObj::RequestSpaceScript -- retail 0x19b40. One 0x118-byte
// FServerRequestSpaceScriptInfo (rpc_iclient_args.h) handed to
// IClient::SendServerRequestSpaceScript (+0xf8) for client_id (Content's Act_CallThorn
// passes the mission's player id). Neither the client interface nor the name is tested.
SERVER_API int __cdecl RequestSpaceScript(const unsigned int& obj_id_ref, const Vector& pos,
                                          const int& camera_mode, unsigned int client_id, const char* script_name) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    FServerRequestSpaceScriptInfo Payload;
    Payload.ObjectId = obj_id_ref;
    memcpy(&Payload.PosXBits, &pos.x, sizeof(float));
    memcpy(&Payload.PosYBits, &pos.y, sizeof(float));
    memcpy(&Payload.PosZBits, &pos.z, sizeof(float));
    Payload.CameraMode = static_cast<unsigned int>(camera_mode);
    safe_strcpy(Payload.Name, static_cast<int>(kRequestSpaceScriptNameSize), script_name);
    ::g_pClientInterface->SendServerRequestSpaceScript(client_id, Payload);
    return 0;
}

// SpaceObj::GetEmptyPos -- retail .text:06cf9bd0 (ordinal 0x19bd0, cdecl). Negative or
// NaN radius/clearance -> -4. Each outer pass scans up to 0x400 objects within R (falling
// back to a scan with radius -1.0f and mode 2 when nothing is found), then tries up to 10
// random offsets of radius R placed by selection_type; an offset collides when
// (distance - object radius) - clearance <= 0. After 10 collisions R doubles and the
// outer pass repeats. An invalid selection_type returns the current result (-4 on the
// first pass). x87 arithmetic is reproduced in double (53-bit precision control).
SERVER_API int __cdecl GetEmptyPos(const unsigned int& system_id_ref, const Transform& base_transform,
                                   const float& start_radius, const float& clearance,
                                   const PosSelectionType& selection_type, Vector& out_pos) {
    int Result = -4;
    if (!(start_radius >= 0.0f)) return Result;
    if (!(clearance >= 0.0f)) return Result;
    float Radius = start_radius;
    unsigned int Ids[0x400];
    bool bEmpty = false;
    do {
        unsigned int Count = 0;
        StarSystem* System = SysDB_LookupSystem(system_id_ref);
        if (System != nullptr)
            Count = System->scan_objects(Ids, 0x400, &base_transform.Translation, Radius,
                                         0xffffffffu);
        if (Count == 0) {
            float const NoRadius = -1.0f;                                // .rdata:06d66b14
            Count = 0;
            System = SysDB_LookupSystem(system_id_ref);
            if (System != nullptr)
                Count = System->scan_objects(Ids, 0x400, &base_transform.Translation,
                                             NoRadius, 2);
        }
        int Attempts = 0;
        for (;;) {
            unsigned int const Selection = static_cast<unsigned int>(selection_type);
            if (Selection > 3u) return Result;
            Vector Offset;
            double const Step = 3.0517578125e-05;                        // .rdata:06d656b8 (float 2^-15)
            double const TwoPi = 6.2831854820251465;                     // .rdata:06d66b80
            if (Selection == 3u) {
                double const U1 = static_cast<double>(rand()) * Step;
                double const U2 = static_cast<double>(rand()) * Step;
                Offset.x = static_cast<float>(static_cast<double>(rand()) * Step *
                                              static_cast<double>(Radius));
                Offset.y = static_cast<float>(static_cast<double>(Radius) * U2);
                Offset.z = static_cast<float>(static_cast<double>(Radius) * U1);
            } else {
                double const Angle = static_cast<double>(rand()) * Step * TwoPi;
                float const C = static_cast<float>(std::cos(Angle) * static_cast<double>(Radius));
                float const S = static_cast<float>(std::sin(Angle) * static_cast<double>(Radius));
                if (Selection == 0u) {
                    Offset.x = C;
                    Offset.y = S;
                    Offset.z = 0.0f;
                } else if (Selection == 1u) {
                    Offset.x = 0.0f;
                    Offset.y = C;
                    Offset.z = S;
                } else {
                    Offset.x = C;
                    Offset.y = 0.0f;
                    Offset.z = S;
                }
            }

            Vector Candidate;
            Server_MathEngine()->TransformXform(reinterpret_cast<FVector&>(Candidate),
                                                reinterpret_cast<FTransform const&>(base_transform),
                                                reinterpret_cast<FVector const&>(Offset));
            out_pos = Candidate;

            bEmpty = true;
            Result = 0;
            bool bCollided = false;
            for (unsigned int Index = 0; Index < Count; ++Index) {
                unsigned int const Id = Ids[Index];
                unsigned int Bits = 0;
                if ((Id & 0x3fffffffu) != 0) {
                    GameObject* const Probe = Server_LookupLiveObject(Id);
                    if (Probe != nullptr) {
                        CObject* const Raw = Probe->Object;
                        if (Raw != nullptr &&
                            (static_cast<unsigned int>(Raw->ObjectClass) & 3u) == 3u)
                            Bits = static_cast<CSimple*>(Raw)->TypeBits;
                    }
                }
                float OtherRadius = 0.0f;
                Vector Other;
                Other.x = 0.0f;
                Other.y = 0.0f;
                Other.z = 0.0f;
                if ((Id & 0x3fffffffu) != 0) {
                    GameObject* const Obj = Server_LookupLiveObject(Id);
                    if (Obj != nullptr) {
                        if ((Bits & 2u) != 0) {
                            Other = Obj->get_position();
                            Obj->get_atmosphere_range(OtherRadius);
                        } else {
                            Obj->get_physical_radius(OtherRadius, Other);
                        }
                    }
                }
                double const Dx = static_cast<double>(out_pos.x) - Other.x;
                double const Dy = static_cast<double>(out_pos.y) - Other.y;
                double const Dz = static_cast<double>(out_pos.z) - Other.z;
                double const Dist = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx); // retail's sum order
                double const Gap = (Dist - static_cast<double>(OtherRadius))
                                   - static_cast<double>(clearance);
                if (Gap <= 0.0) {
                    bCollided = true;
                    break;
                }
            }
            if (!bCollided)
                break;
            ++Attempts;
            bEmpty = false;
            Result = -1;
            if (Attempts >= 10)
                break;
        }
        Radius = static_cast<float>(static_cast<double>(Radius) + static_cast<double>(Radius));
    } while (!bEmpty);
    return Result;
}

// SpaceObj::IsPosEmpty -- retail .text:06cfa060 (ordinal 0x1a060, cdecl). Any hit in the
// radius scan means not empty. Otherwise a second scan (radius -1.0f, mode 2) tests each
// live object's atmosphere range against the distance. An unknown system returns -1 with
// out_empty true. x87 arithmetic is reproduced in double (53-bit precision control).
SERVER_API int __cdecl IsPosEmpty(const unsigned int& system_id_ref, const Vector& pos,
                                  const float& radius, bool& out_empty) {
    out_empty = true;
    float SearchRadius = radius;
    int Result = -1;
    bool bHit = false;
    unsigned int Count2 = 0;
    StarSystem* System = SysDB_LookupSystem(system_id_ref);
    if (System == nullptr) {
        out_empty = true;
        return Result;
    }
    unsigned int Ids[0x400];
    unsigned int const Count1 =
        System->scan_objects(Ids, 0x400, &pos, SearchRadius, 0xffffffffu); // FUN_06d0bcd0
    if (Count1 == 0) {
        SearchRadius = -1.0f;                                            // .rdata:06d66b14
        System = SysDB_LookupSystem(system_id_ref);                      // looked up again
        if (System != nullptr) {
            Count2 = System->scan_objects(Ids, 0x400, &pos, SearchRadius, 2);
            for (unsigned int Index = 0; Index < Count2; ++Index) {
                Vector Other;
                Other.x = 0.0f;
                Other.y = 0.0f;
                Other.z = 0.0f;
                float OtherRadius = 0.0f;
                unsigned int const Id = Ids[Index];
                if ((Id & 0x3fffffffu) != 0) {
                    GameObject* const Obj = Server_LookupLiveObject(Id);
                    if (Obj != nullptr) {
                        Other = Obj->get_position();
                        Obj->get_atmosphere_range(OtherRadius);          // return ignored
                    }
                }
                double const Dx = static_cast<double>(pos.x) - Other.x;
                double const Dy = static_cast<double>(pos.y) - Other.y;
                double const Dz = static_cast<double>(pos.z) - Other.z;
                double const Dist = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx);
                double const Gap = Dist - static_cast<double>(OtherRadius);
                if (Gap <= 0.0) {
                    bHit = true;
                    break;
                }
            }
        }
    }
    Result = 0;
    if (Count1 != 0 || (Count2 != 0 && bHit)) {
        out_empty = false;
        return Result;
    }
    out_empty = true;
    return Result;
}

} // namespace SpaceObj

// ===========================================================================
// pub::Player exports. Retail pattern: the IsValidPlayer guard, then field access on
// the PlayerData slot (stride 0x418).
// ===========================================================================
namespace Player {

// Player::MarkObj -- retail 0x74970. GameObject::mark_for_player (FUN_06cef3b0) records the
// mark, then IClient::SendServerMarkObj (+0x12c, FLPACKET_SERVER_MARKOBJ). The client
// interface (DAT_06d8fb14) is not null-tested.
SERVER_API int __cdecl MarkObj(unsigned int player_id, unsigned int obj_id, int mark_type) {
    if (!IsValidPlayer(player_id)) return -2;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -3;
    obj->mark_for_player(player_id, mark_type);
    ::g_pClientInterface->SendServerMarkObj(player_id, obj_id, mark_type);
    return 0;
}

// Player::CfgInterfaceNotification -- retail 0x74a10. IClient::
// SendServerCfgInterfaceNotification (+0x130) with a 12-byte
// FPayloadServerCfgInterfaceNotification; retail leaves the three pad bytes unwritten,
// and so does this. The client interface is not null-tested.
SERVER_API int __cdecl CfgInterfaceNotification(unsigned int player_id, unsigned int notif_type,
                                      bool enable, int param) {
    if (!IsValidPlayer(player_id)) return -2;
    FPayloadServerCfgInterfaceNotification Payload;
    Payload.NotifType = notif_type;
    Payload.Param = static_cast<unsigned int>(param);
    Payload.bEnable = enable ? 1u : 0u;
    ::g_pClientInterface->SendServerCfgInterfaceNotification(player_id, Payload);
    return 0;
}

// Player::GetSystem -- retail 0x74a80 (slot +0x3e0).
SERVER_API int __cdecl GetSystem(const unsigned int& player_id, unsigned int& out_system_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_system_id = p->system_id;
    return 0;
}

// Player::GetShip -- retail 0x74ad0 (slot +0x3e4).
SERVER_API int __cdecl GetShip(const unsigned int& player_id, unsigned int& out_ship_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_ship_id = p->ship_object_id;
    return 0;
}

// Player::GetShipID -- retail 0x74b20 (slot +0x264).
SERVER_API int __cdecl GetShipID(const unsigned int& player_id, unsigned int& out_ship_arch) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_ship_arch = p->ship_archetype;
    return 0;
}

// Player::GetRep -- retail 0x74b70 (slot +0x318); -1 on failure.
SERVER_API int __cdecl GetRep(const unsigned int& player_id, int& out_rep) {
    out_rep = -1;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_rep = p->rep_vibe;
    return 0;
}

// Player::GetBase -- retail 0x74bd0 (slot +0x3ec).
SERVER_API int __cdecl GetBase(const unsigned int& player_id, unsigned int& out_base_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_base_id = p->current_base;
    return 0;
}

// Player::GetLocation -- retail 0x74c20 (slot +0x3f8).
SERVER_API int __cdecl GetLocation(const unsigned int& player_id, unsigned int& out_location_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_location_id = p->current_location;
    return 0;
}

// Player::GetCharacter -- retail .text:06d54c70 (slot +0x3fc).
SERVER_API int __cdecl GetCharacter(const unsigned int& player_id, unsigned int& out_char_id) {
    if (!IsValidPlayer(player_id)) return -2;
    out_char_id = Players[player_id].character_id;
    return 0;
}

// Player::GetRelativeHealth -- retail 0x74cc0 (slot +0x268); -1.0 on failure.
SERVER_API int __cdecl GetRelativeHealth(const unsigned int& player_id, float& out_health) {
    out_health = -1.0f;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_health = p->relative_health;
    return 0;
}

// Player::EnumerateCargo -- retail 0x74d20, over slot->equipment (+0x278). Mounted items
// are skipped. Unlike SpaceObj::EnumerateCargo, Arch2Good's answer is not tested.
SERVER_API int __cdecl EnumerateCargo(const unsigned int& player_id, CargoEnumerator& enumerator) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    for (std::list<EquipDesc>::const_iterator It = p->equipment.Items.begin();
         It != p->equipment.Items.end(); ++It) {
        EquipDesc const& Item = *It;
        if (Item.bEquipped)
            continue;
        unsigned int const GoodId = Arch2Good(Item.ArchId);
        enumerator.notify_cargo(GoodId, Item.Count, Item.Id);
    }
    return 0;
}

// Player::GetRemainingHoldSize -- retail 0x74db0. Goes through pub::GetCargoHoldSize (an
// unsigned dword, zero-extended by FILD qword) rather than HoldSize directly, minus
// EquipDescList::get_cargo_space_occupied of slot->equipment (+0x278).
SERVER_API int __cdecl GetRemainingHoldSize(const unsigned int& player_id, float& out_remaining) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    unsigned int HoldSize = 0;
    pub::GetCargoHoldSize(p->ship_archetype, HoldSize);
    out_remaining = (float)HoldSize - p->equipment.get_cargo_space_occupied();
    return 0;
}

// Player::AddCargo -- retail 0x74e90. In space: StarSystem::add_cargo (FUN_06d0e8d0) on the
// ship's system. Docked (no ship object): PlayerData::add_equipment (FUN_06d4efc0) into the
// cargo bay.
// mission_flag: docked it is only FUN_06d4efc0's "do not combine with an existing stack"
// byte; in space it also becomes EquipDesc::bMission (direct byte store at .text:06d0eadd).
SERVER_API int __cdecl AddCargo(const unsigned int& player_id, const unsigned int& good_id,
                     unsigned int quantity, float health, bool mission_flag) {
    if (!IsValidPlayer(player_id)) return -2;
    // Retail's compares reject NaN as well, hence the negated tests.
    if (!(health >= 0.0f)) return -4;
    if (!(health <= 1.0f)) return -4;
    PlayerSlot* p = &Players[player_id];

    if (p->ship_object_id != 0) {
        // A failed lookup is the only -2. The system is used untested (null for an
        // in-transit ship, as in retail).
        GameObject* ShipObj = nullptr;
        StarSystem* ShipSys = nullptr;
        if (!Server_FindObject(p->ship_object_id, ShipObj, ShipSys))
            return -2;
        // Good2Arch only after the lookup succeeded.
        ShipSys->add_cargo(ShipObj, Good2Arch(good_id), quantity, health,
                           mission_flag);
        return 0;
    }

    unsigned int const ArchId = Good2Arch(good_id);
    p->add_equipment(ArchId, EquipDesc::CARGO_BAY_HP_NAME,
                     (int)quantity, health, /*bEquipped*/false,
                     mission_flag, /*bNotify*/true);
    return 0;
}

// Player::RemoveCargo -- retail 0x74fb0. In space: StarSystem::remove_cargo (FUN_06d0ebf0,
// three stack args) on the ship's system, used untested; a failed lookup is the only -2.
// Docked: the tail is PlayerData::remove_equipment (FUN_06d4f100) inlined, so the port
// calls the shared helper.
SERVER_API int __cdecl RemoveCargo(const unsigned int& player_id, unsigned short cargo_slot, unsigned int quantity) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = &Players[player_id];

    if (p->ship_object_id != 0) {
        GameObject* ShipObj = nullptr;
        StarSystem* ShipSys = nullptr;
        if (!Server_FindObject(p->ship_object_id, ShipObj, ShipSys))
            return -2;
        ShipSys->remove_cargo(ShipObj, cargo_slot, (int)quantity);
        return 0;
    }

    p->remove_equipment(cargo_slot, (int)quantity, /*bNotify*/true);
    return 0;
}

// Player::SetShipAndLoadout -- retail 0x750a0. set_ship_arch (FUN_06d4ec10), then a local
// list copy of the vector with every id reset to SUBOBJ_ID_NONE, so load_equipment
// (FUN_06d4eeb0) mints a fresh EquipIdMaker id for each item.
SERVER_API int __cdecl SetShipAndLoadout(const unsigned int& player_id, unsigned int ship_arch,
                                          const EquipDescVector& equip_desc_vector) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    p->set_ship_arch(ship_arch);

    EquipDescList Fresh;
    for (EquipDesc const* It = equip_desc_vector.Begin; It != equip_desc_vector.End; ++It) {
        EquipDesc Copy(*It);
        Copy.set_id(SUBOBJ_ID_NONE);
        Fresh.add_equipment_item(Copy, false);
    }
    p->load_equipment(Fresh);
    return 0;
}

// Player::GetRank -- retail 0x75220 (slot +0x284).
SERVER_API int __cdecl GetRank(const unsigned int& player_id, int& out_rank) {
    out_rank = 0;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_rank = (int)p->rank_level;
    return 0;
}

// Player::GetMoneyNeededToNextRank -- retail 0x75280 (slot +0x288).
SERVER_API int __cdecl GetMoneyNeededToNextRank(const unsigned int& player_id, int& out_money) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_money = p->money_needed_to_next_rank;
    return 0;
}

// Player::GetAssetValue -- retail 0x752d0. Ship value is find_by_ship_arch (not
// find_by_archetype) price * 0.8f (.rdata:06d6ae78). Each item of slot->equipment (+0x278):
// combinable (GoodInfo+0x70) -> Count * price; otherwise ftol(Health * price) * Count *
// 0.3f (.rdata:06d6ae7c). FUN_06d4faf0 splits on the same flag with the same 0.3f.
SERVER_API int __cdecl GetAssetValue(const unsigned int& player_id, float& out_value) {
    out_value = 0.0f;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    unsigned int const ship_arch = p->ship_archetype;
    if ((ship_arch & 0x3fffffffu) == 0) return -2;

    GoodInfoList* good_list = GoodList_get();
    if (!good_list) return -2;

    const GoodInfo* ship_good = good_list->find_by_ship_arch(ship_arch);
    if (!ship_good) return -2;

    out_value = 0.8f * ship_good->Price;

    for (std::list<EquipDesc>::const_iterator It = p->equipment.Items.begin();
         It != p->equipment.Items.end(); ++It) {
        EquipDesc const& Item = *It;
        if ((Item.ArchId & 0x3fffffffu) == 0) continue;
        const GoodInfo* item_good = GoodList::find_by_archetype(Item.ArchId);
        if (!item_good) continue;

        if (item_good->bCombinable) {
            out_value += (float)Item.Count * item_good->Price;
        } else {
            int const Unit = (int)(Item.Health * item_good->Price);
            out_value += (float)(Unit * Item.Count) * 0.3f;
        }
    }
    return 0;
}

// Player::AdjustCash (0x06d553c0) -- PlayerData::AdjustCash (0x06d4f4e0) inlined with its
// bool folded to true, so the running total at +0x320 always accumulates (even when the
// MP gate below skips the rest).
SERVER_API int __cdecl AdjustCash(const unsigned int& player_id, int amount) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    // The client id doubles as retail's "slot occupied" flag.
    if (p->client_id == 0) return -2;

    p->total_earned += amount;
    int new_cash = p->cash + amount;

    // +0x64 is the selected character id (common_server_playerdb.h): an MP server only
    // adjusts the cash of a session that has picked a character. Skips the clamp, the
    // store and both notifications, yet returns 0.
    if (IsMPServer() && !p->selected_character.is_valid()) return 0;

    if (new_cash < 0) new_cash = 0;
    else if (new_cash > 999999999) new_cash = 999999999;
    p->cash = new_cash;

    // IClient +0x134; the client interface is not null-tested.
    ::g_pClientInterface->SendServerSetCash(p->client_id, new_cash);
    // FUN_06d43650(0, &client_id): message 0 broadcast to every controller.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &p->client_id);
    return 0;
}

// Player::InspectCash (0x06d55480) -- reads +0x31c and clamps it on the way out without
// writing it back. Same slot-occupied gate as AdjustCash; out_cash is zeroed first.
SERVER_API int __cdecl InspectCash(const unsigned int& player_id, int& out_cash) {
    out_cash = 0;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    if (p->client_id == 0) return -2;
    int cash = p->cash;
    if (cash < 0) cash = 0;
    if (cash > 999999999) cash = 999999999;
    out_cash = cash;
    return 0;
}

// Player::SetInitialPos -- retail 0x75500 (slot +0x350).
SERVER_API int __cdecl SetInitialPos(const unsigned int& player_id, const Vector& pos) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->initial_pos[0] = pos.x;
    p->initial_pos[1] = pos.y;
    p->initial_pos[2] = pos.z;
    return 0;
}

// Player::SetInitialOrnt -- retail 0x75560 (slot +0x35c, nine dwords).
SERVER_API int __cdecl SetInitialOrnt(const unsigned int& player_id, const Matrix& orient) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    for (int i = 0; i < 9; i++) {
        ((float*)p->initial_ornt)[i] = ((const float*)&orient)[i];
    }
    return 0;
}

// Player::GetNumKills -- retail 0x755c0 (slot +0x334); -1 on failure.
SERVER_API int __cdecl GetNumKills(const unsigned int& player_id, int& out_kills) {
    if (!IsValidPlayer(player_id)) { out_kills = -1; return -2; }
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) { out_kills = -1; return -2; }
    out_kills = p->num_kills;
    return 0;
}

// Player::SetNumKills -- retail 0x06d55620. Store at +0x334, then IClient +0x138
// (FLPACKET_SERVER_SETNUMKILLS) with the client id passed by address.
SERVER_API int __cdecl SetNumKills(const unsigned int& player_id, int kills) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->num_kills = kills;
    ::g_pClientInterface->SendServerSetNumKills(p->client_id, kills);
    return 0;
}

// Player::GetNumMissionSuccesses -- retail 0x75690 (slot +0x338); -1 on failure.
SERVER_API int __cdecl GetNumMissionSuccesses(const unsigned int& player_id, int& out_count) {
    if (!IsValidPlayer(player_id)) { out_count = -1; return -2; }
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) { out_count = -1; return -2; }
    out_count = p->num_mission_successes;
    return 0;
}

// Player::SetNumMissionSuccesses -- retail 0x06d556f0. As SetNumKills: +0x338, then
// IClient +0x13c (FLPACKET_SERVER_SETMISSIONSUCCESSES).
SERVER_API int __cdecl SetNumMissionSuccesses(const unsigned int& player_id, int count) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->num_mission_successes = count;
    ::g_pClientInterface->SendServerSetMissionSuccesses(p->client_id, count);
    return 0;
}

// Player::GetNumMissionFailures -- retail 0x75760 (slot +0x33c); -1 on failure.
SERVER_API int __cdecl GetNumMissionFailures(const unsigned int& player_id, int& out_count) {
    if (!IsValidPlayer(player_id)) { out_count = -1; return -2; }
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) { out_count = -1; return -2; }
    out_count = p->num_mission_failures;
    return 0;
}

// Player::SetNumMissionFailures -- retail 0x06d557c0. As SetNumKills: +0x33c, then
// IClient +0x140 (FLPACKET_SERVER_SETMISSIONFAILURES).
SERVER_API int __cdecl SetNumMissionFailures(const unsigned int& player_id, int count) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->num_mission_failures = count;
    ::g_pClientInterface->SendServerSetMissionFailures(p->client_id, count);
    return 0;
}

// Player::DisplayMissionMessage -- retail 0x75830, ordinal 109. Message type 0 sets
// bSkipNextAutosave (+0x340). Broadcast goes to the player's group (+0x404) when there
// is one; otherwise IClient slot 70 (+0x118), the client interface untested.
SERVER_API int __cdecl DisplayMissionMessage(const unsigned int& player_id, const FmtStr& fmt_str,
                                               MissionMessageType msg_type, bool broadcast) {
    if (!IsValidPlayer(player_id)) return -2;
    CSetMissionMessage Record;
    Record.Text = fmt_str;
    Record.Type = static_cast<unsigned int>(msg_type);
    if (msg_type == 0) {
        Players[player_id].bSkipNextAutosave = 1;
    }
    CPlayerGroup* const Group = static_cast<CPlayerGroup*>(Players[player_id].group_ptr);
    if (broadcast && Group != nullptr) {
        Group->SetMissionMessage(Record);
    } else {
        ::g_pClientInterface->SendServerSetMissionMessage(player_id, Record);
    }
    return 0;
}

// Player::SetStoryCue -- retail 0x75d90. IClient slot 90 (+0x168,
// FLPACKET_SERVER_SET_STORY_CUE) with both arguments by address. Nothing is stored on
// the player slot.
SERVER_API int __cdecl SetStoryCue(const unsigned int& player_id, unsigned int cue_id) {
    if (!IsValidPlayer(player_id)) return -2;
    ::g_pClientInterface->SendServerSetStoryCue(player_id, cue_id);
    return 0;
}

// Player::PopUpDialog -- retail 0x06d55df0. No player validation; IClient +0x17c
// (FLPACKET_COMMON_POP_UP_DIALOG); always 0.
SERVER_API int __cdecl PopUpDialog(unsigned int player_id, const FmtStr& title, const FmtStr& text, unsigned int param) {
    ::g_pClientInterface->SendCommonPopUpDialog(player_id, title, text, param);
    return 0;
}

// Player::ReturnBestPath -- retail 0x06d55e20. IClient +0x174
// (FLPACKET_COMMON_REQUEST_BEST_PATH). No validation, always 0.
SERVER_API int __cdecl ReturnBestPath(unsigned int player_id, unsigned char* data, int data_len) {
    ::g_pClientInterface->SendCommonRequestBestPath(player_id, data, (unsigned int)data_len);
    return 0;
}

// Player::ReturnPlayerStats -- retail 0x06d55e40. IClient +0x178
// (FLPACKET_COMMON_REQUEST_PLAYER_STATS). No validation, always 0.
SERVER_API int __cdecl ReturnPlayerStats(unsigned int player_id, unsigned char* data, int data_len) {
    ::g_pClientInterface->SendCommonRequestPlayerStats(player_id, data, (unsigned int)data_len);
    return 0;
}

SERVER_API int __cdecl SetCamera(unsigned int player_id, const Transform& transform, float fov, float aspect) {
    if (!IsValidPlayer(player_id)) return -2;

    FPayloadServerSetCamera Block;
    Block.Position = transform.Translation;

    // No null test on the engine and no seed for the quaternion (the call writes all
    // four dwords). Retail passes &transform, which works because Rotation is at +0x00.
    Server_MathEngine()->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Block.Orient),
                                             reinterpret_cast<FMatrix const&>(transform.Rotation));

    Block.Fov = fov;
    Block.Aspect = aspect;

    // The client interface is not null-tested.
    ::g_pClientInterface->SendServerSetCamera(player_id, Block);
    return 0;
}

// Player::RevertCamera -- retail 0x75f30. IClient +0x164 (FLPACKET_SERVER_REVERT_CAMERA),
// player id by address.
SERVER_API int __cdecl RevertCamera(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    ::g_pClientInterface->SendServerRevertCamera(player_id);
    return 0;
}

// Player::LoadHint -- retail 0x06d55f80. IClient +0x144 (FLPACKET_SERVER_LOADHINT), id by
// address. No validation: neither the client interface nor the hint is tested; always 0.
SERVER_API int __cdecl LoadHint(unsigned int player_id, BaseHint* hint) {
    ::g_pClientInterface->SendServerLoadHint(player_id, hint);
    return 0;
}

// Player::GetGroupSize -- retail 0x06d55fa0, ordinal 192. No player validation; no group
// means a party of one. Always 0.
SERVER_API int __cdecl GetGroupSize(unsigned int player_id, unsigned int& out_size) {
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    CPlayerGroup* grp = p ? (CPlayerGroup*)p->group_ptr : nullptr;
    if (grp) {
        out_size = grp->GetMemberCount();
    } else {
        out_size = 1;
    }
    return 0;
}

// Player::GetName -- retail 0x75fe0 (ordinal 222). Retail's string is VC6's
// basic_string<unsigned short>; std::wstring here. Every path returns 0; an invalid id
// leaves the string untouched. PlayerDB::GetActiveCharacterName is inlined in retail.
SERVER_API int __cdecl GetName(unsigned int player_id, std::wstring& out_name) {
    if (!IsValidPlayer(player_id)) return 0;
    const wchar_t* Name = Players.GetActiveCharacterName(player_id);
    if (Name != nullptr) {
        out_name.assign(Name);
    } else {
        out_name.assign(L"<bad name>");
    }
    return 0;
}

// Player::IsGroupMember -- retail 0x06d56170, ordinal 314. No validation. Equal ids answer
// true; otherwise player1's group is asked, and no group answers false.
SERVER_API int __cdecl IsGroupMember(unsigned int player1_id, unsigned int player2_id, bool& out_result) {
    if (player1_id == player2_id) { out_result = true; return 0; }
    PlayerSlot* p1 = PlayerDB_GetPlayer(player1_id);
    CPlayerGroup* grp = p1 ? (CPlayerGroup*)p1->group_ptr : nullptr;
    out_result = grp ? grp->IsMember(player2_id) : false;
    return 0;
}

// Player::RemoveFromGroup -- retail 0x06d561c0, ordinal 361. No group returns -3.
// CPlayerGroup::DelMember clears group_ptr, unregisters the player and notifies the rest.
SERVER_API int __cdecl RemoveFromGroup(unsigned int player_id) {
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    CPlayerGroup* grp = p ? (CPlayerGroup*)p->group_ptr : nullptr;
    if (!grp) return -3;
    grp->DelMember(player_id);
    return 0;
}

// Player::RewardGroup -- retail 0x06d563a0, ordinal 388. The slot's group (+0x404) is read
// raw, before any bounds or occupancy test. With a group: RewardMembers. Solo: the usual
// guard, then PlayerData::AdjustCash(amount, true) (FUN_06d4f4e0) inlined. Always 0.
SERVER_API int __cdecl RewardGroup(unsigned int player_id, int amount) {
    CPlayerGroup* const Group = static_cast<CPlayerGroup*>(Players[player_id].group_ptr);
    if (Group != nullptr) {
        Group->RewardMembers(amount);
        return 0;
    }
    if (IsValidPlayer(player_id)) {
        Players[player_id].adjust_cash(amount, true);
    }
    return 0;
}

// Player::SetMsnID -- retail 0x76460, ordinal 431. Every path returns 0. Stores +0x408 /
// +0x40c; flag -> IClient slot 74 (+0x128, MISSIONSAVEA) with the client id by value;
// then IClient slot 114 (+0x1c8) unconditionally. The client interface is not null-tested.
SERVER_API int __cdecl SetMsnID(unsigned int player_id, unsigned int msn_id, unsigned int sub_id,
                                  bool flag, unsigned int param) {
    if (!IsValidPlayer(player_id)) return 0;
    PlayerData& Slot = Players[player_id];
    Slot.group_mission_id = msn_id;
    Slot.group_mission_set_by = sub_id;
    if (flag) {
        ::g_pClientInterface->SendServerMissionSaveA(Slot.client_id, param);
    }
    ::g_pClientInterface->SendServerMiscObjUpdateMission(player_id, msn_id, sub_id);
    return 0;
}

// Player::GetMsnID -- retail 0x76500, ordinal 221 (slot +0x408). out is untouched for an
// invalid id; always 0.
SERVER_API int __cdecl GetMsnID(unsigned int player_id, unsigned int& out_msn_id) {
    if (IsValidPlayer(player_id)) {
        out_msn_id = Players[player_id].group_mission_id;
    }
    return 0;
}

// Player::SetRank -- retail 0x76550. Stores rank_level (+0x284), then IClient slot 117
// (+0x1d4) with (id, id, rank): retail passes the same id twice, by value.
SERVER_API int __cdecl SetRank(unsigned int player_id, int rank) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->rank_level = (unsigned int)rank;
    ::g_pClientInterface->SendServerMiscObjUpdateRank(player_id, player_id, rank);
    return 0;
}

// Player::SetMoneyNeededToNextRank -- slot +0x288.
SERVER_API int __cdecl SetMoneyNeededToNextRank(unsigned int player_id, int money) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->money_needed_to_next_rank = money;
    return 0;
}

// Player::SetMonkey / SetRobot / SetTrent -- retail 0x76600 / 0x76650 / 0x766c0. All three
// write the session Costume at PlayerData+0x2E4 and send nothing. The literals are
// nickname ids (CreateID hashes); SetMonkey touching only PartIds[0] is retail.
SERVER_API int __cdecl SetMonkey(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->costume.PartIds[0] = 0x88db0044u;
    return 0;
}

SERVER_API int __cdecl SetRobot(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->costume.PartIds[0] = 0u;
    p->costume.PartIds[1] = 0x8dffc147u;
    p->costume.PartIds[2] = 0x9dc4c883u;
    p->costume.PartIds[3] = 0x93582d4fu;
    return 0;
}

SERVER_API int __cdecl SetTrent(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    CostumeDescriptions const* Costumes = GetCostumeDescriptions();
    if (Costumes != nullptr) {
        Costumes->get_costume((int)0xb8249f07u, p->costume);
    }
    return 0;
}

// Player::SendNNMessage -- retail 0x76720. Every failure, an invalid player included,
// returns -4. IClient slot 69 (+0x114).
SERVER_API int __cdecl SendNNMessage(unsigned int player_id, unsigned int msg_id) {
    if (msg_id == 0) return -4;
    if (!IsValidPlayer(player_id)) return -4;
    ::g_pClientInterface->SendServerSendNNMessage(player_id, msg_id);
    return 0;
}

// Player::ForceLand -- retail 0x76780: IServerImpl::Dock(base, player), both by reference
// (IServer slot 91). No player validation here; it lives inside Dock. `Server` is a static
// object, so there is nothing to null-test.
SERVER_API int __cdecl ForceLand(unsigned int player_id, unsigned int base_id) {
    ::GetServerInterface()->Dock(base_id, player_id);
    return 0;
}

// Player::SetCostume -- retail 0x767c0 is `XOR EAX,EAX / RET`: a shipped no-op, not a port
// stub. Do not "finish" it.
SERVER_API int __cdecl SetCostume(const unsigned int& player_id, int costume_id) {
    (void)player_id; (void)costume_id;
    return 0;
}

// Player::GetGender -- retail 0x767a0 (ordinal 183): always 1, no validation.
SERVER_API int __cdecl GetGender(const unsigned int& player_id, int& out_gender) {
    out_gender = 1;
    return 0;
}

// Player::GetBody -- retail 0x767b0 (ordinal 158): always 0, no validation.
SERVER_API int __cdecl GetBody(const unsigned int& player_id, unsigned int& out_body) {
    out_body = 0;
    return 0;
}

// Player::GetGroupMembers -- retail 0x761f0: CPlayerGroup::StoreMemberList when grouped.
SERVER_API int __cdecl GetGroupMembers(unsigned int player_id, std::vector<unsigned int>& out_members) {
    out_members.clear();
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (p && p->group_ptr) {
        CPlayerGroup* grp = (CPlayerGroup*)p->group_ptr;
        grp->StoreMemberList(out_members);
    } else {
        out_members.push_back(player_id);
    }
    return 0;
}

// Player::SetMissionObjectives -- retail .text:06d55960. Touches no server-side mission
// log: one FServerSetMissionObjectivesInfo to IClient slot 72 (+0x120), the client
// interface untested. The entry array is an eh-vector block (count cookie + count * 0x1c);
// retail fills it without testing the pointer after a failed new.
SERVER_API int __cdecl SetMissionObjectives(const unsigned int& player_id, const unsigned int& mission_id, const MissionObjective* objs, unsigned int count, const FmtStr& title, unsigned char type, const FmtStr& desc) {
    if (!IsValidPlayer(player_id)) return -2;

    FServerSetMissionObjectivesInfo Payload;
    Payload.MissionId = mission_id;
    Payload.Count = count;
    Payload.TextA = title;
    Payload.MissionType = type;
    Payload.TextB = desc;

    FServerMissionObjectiveEntry* Entries = nullptr;
    if (count != 0) {
        unsigned char* const Block = static_cast<unsigned char*>(
            operator new(count * sizeof(FServerMissionObjectiveEntry) + 4));
        if (Block != nullptr) {
            *reinterpret_cast<unsigned int*>(Block) = count;
            Entries = reinterpret_cast<FServerMissionObjectiveEntry*>(Block + 4);
            for (unsigned int I = 0; I < count; ++I)
                new (&Entries[I]) FServerMissionObjectiveEntry;
        }
        for (unsigned int I = 0; I < count; ++I) {
            Entries[I].Text = objs[I].Description;
            Entries[I].State = objs[I].Type;
        }
    }
    Payload.Entries = Entries;

    ::g_pClientInterface->SendServerSetMissionObjectives(player_id, Payload);

    if (Entries != nullptr) {
        unsigned char* const Block = reinterpret_cast<unsigned char*>(Entries) - 4;
        unsigned int const Cookie = *reinterpret_cast<unsigned int*>(Block);
        for (unsigned int I = Cookie; I != 0; --I)
            Entries[I - 1].~FServerMissionObjectiveEntry();
        operator delete(Block);                                           // FUN_06d5ffb0
    }
    return 0;
}

// Player::ReplaceMissionObjective -- retail 0x75c10. One record to IClient slot 71
// (+0x11c), the client interface untested. Nothing is written to the player slot (the
// mission log at +0x390 is not touched).
SERVER_API int __cdecl ReplaceMissionObjective(const unsigned int& player_id, const unsigned int& mission_id, unsigned int index, const MissionObjective& obj) {
    if (!IsValidPlayer(player_id)) return -2;
    FServerReplaceMissionObjectiveInfo Record;
    Record.MissionId = mission_id;
    Record.ObjectiveIndex = static_cast<unsigned char>(index);
    Record.Text = obj.Description;
    Record.ObjectiveType = obj.Type;
    ::g_pClientInterface->SendServerReplaceMissionObjective(player_id, Record);
    return 0;
}

// Player::SetMissionObjectiveState -- retail 0x75d20, ordinal 426. One record to IClient
// slot 73 (+0x124), the client interface untested.
SERVER_API int __cdecl SetMissionObjectiveState(const unsigned int& player_id, const unsigned int& mission_id, int index, unsigned int state) {
    if (!IsValidPlayer(player_id)) return -2;
    FServerMissionObjectiveStateInfo Record;
    Record.MissionId = mission_id;
    Record.ObjectiveIndex = static_cast<unsigned char>(index);
    Record.State = state;
    ::g_pClientInterface->SendServerSetMissionObjectiveState(player_id, Record);
    return 0;
}

} // namespace Player

// ===========================================================================
// pub::GF exports (Game Framework) -- gf/pubGF.cpp. Every object the family
// creates lives in the BaseDB (FUN_06d15650 = GetBaseDB(), src/Server/server_basedb.cpp).
// ===========================================================================
namespace GF {

// ---- FindBase / FindLocation / GetSpaceflightLocation ----

// pub::GF::FindBase -- retail 0x37ca0 -> FUN_06d141f0, a bare CreateID. No null test.
SERVER_API unsigned int __cdecl FindBase(const char* name) {
    return CreateID(name);
}

// pub::GF::FindLocation -- retail 0x37cc0 -> FUN_06d14210, a bare MakeLocationID. No null
// test.
SERVER_API unsigned int __cdecl FindLocation(unsigned int base_id, const char* name) {
    return MakeLocationID(base_id, name);
}

// pub::GF::GetSpaceflightLocation -- retail 0x37ce0: Common's SpaceflightLocationID data
// export (CreateID("Spaceflight"), common_misc.cpp).
SERVER_API unsigned int __cdecl GetSpaceflightLocation() {
    return ::SpaceflightLocationID.Id;
}

// ---- GetBasePosition ----

// pub::GF::GetBasePosition -- retail .text:06d17d20. Single-entry memo cache DAT_06d8e578
// (base id) + DAT_06d8e57c/580/584 (the Vector), reset by LAB_06d17cf0.
static unsigned int GCachedBasePosId = 0;
static float GCachedBasePos[3] = { 0, 0, 0 };

// Both ids are `unsigned int const&` (?GetBasePosition@GF@pub@@YAHABI0AAVVector@@@Z). The
// base id is tested before the system id; get_system is not null-tested. Walks the
// system's objects (ISystem slots 15/16) for one whose dock_with (+0x80) is the base.
SERVER_API int __cdecl GetBasePosition(const unsigned int& system_id, const unsigned int& base_id, Vector& out_pos) {
    if ((base_id & 0x3fffffff) == 0) return -2;
    if ((system_id & 0x3fffffff) == 0) return -2;

    if (base_id == GCachedBasePosId) {
        out_pos.x = GCachedBasePos[0];
        out_pos.y = GCachedBasePos[1];
        out_pos.z = GCachedBasePos[2];
        return 0;
    }

    Universe::ISystem const* Sys = Universe::get_system(system_id);
    Universe::FSystemObject Record;
    Record.Nickname.Length = 0;
    Record.Nickname.Data[0] = '\0';
    Record.Reputation.Length = 0;
    Record.Reputation.Data[0] = '\0';
    Record.NextRing.Length = 0;
    Record.NextRing.Data[0] = '\0';
    Record.PrevRing.Length = 0;
    Record.PrevRing.Data[0] = '\0';

    unsigned int const Count = (unsigned int)Sys->GetObjectCount();
    for (unsigned int Index = 0; Index < Count; ++Index) {
        if (!Sys->GetSystemObject(Index, &Record)) continue;
        if (Record.DockWith != base_id) continue;
        GCachedBasePosId = base_id;
        GCachedBasePos[0] = Record.Pos.x;
        GCachedBasePos[1] = Record.Pos.y;
        GCachedBasePos[2] = Record.Pos.z;
        out_pos.x = Record.Pos.x;
        out_pos.y = Record.Pos.y;
        out_pos.z = Record.Pos.z;
        return 0;
    }
    return -2;
}

// ---- Costume / BodyPart / Accessory ----

// pub::GF::GetCostumeByID -- retail .text:06d17e70. No null test.
SERVER_API void __cdecl GetCostumeByID(int id, Costume& out_costume) {
    GetCostumeDescriptions()->get_costume(id, out_costume);
}

// pub::GF::GetBodyPart -- retail .text:06d17e90. A hit returns the record's first dword.
// No null tests.
SERVER_API int __cdecl GetBodyPart(const char* name, int type_id) {
    bodypart const* bp = GetCostumeDescriptions()->find_bodypart(type_id, name);
    if (bp) return *(int const*)bp;
    FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot find bodypart %s\n",
          "E:\\FL\\Scratch\\Source\\Server\\gf\\pubGF.cpp", 0x78, name);
    return 0;
}

// pub::GF::GetAccessory -- retail .text:06d17ee0. As GetBodyPart over find_accessory.
SERVER_API int __cdecl GetAccessory(const char* name) {
    accessory const* acc = GetCostumeDescriptions()->find_accessory(name);
    if (acc) return *(int const*)acc;
    FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot find accessory %s\n",
          "E:\\FL\\Scratch\\Source\\Server\\gf\\pubGF.cpp", 0x87, name);
    return 0;
}

// pub::GF::GetCostumeSkeletonGender -- retail .text:06d180c0. No null test.
SERVER_API int __cdecl GetCostumeSkeletonGender(Costume const& desc, int& out_gender) {
    out_gender = GetCostumeDescriptions()->get_costume_gender(desc);
    return 0;
}

// ---- Character Create/Destroy/SetBehavior ----

// pub::GF::CharacterCreate -- retail .text:06d17f30. BaseDB::create_character
// (FUN_06d14870) with description+0x50 as the session filter.
SERVER_API unsigned int __cdecl CharacterCreate(const CharacterDescription& description) {
    CharacterProperties Props;
    Props.init(description);
    return (unsigned int)GetBaseDB()->create_character(Props, description.Filter);
}

// pub::GF::CharacterDestroy -- retail .text:06d17fb0 (destroy_character is FUN_06d149c0).
// No null test of the pointer.
SERVER_API void __cdecl CharacterDestroy(unsigned int* handle) {
    if ((*handle & 0x3fffffff) != 0) {
        GetBaseDB()->destroy_character(*handle);
        *handle = 0;
    }
}

// pub::GF::CharacterSetBehavior -- retail .text:06d17fe0 (FUN_06d14ac0, unconditional).
SERVER_API void __cdecl CharacterSetBehavior(unsigned int character_id, unsigned long behavior_handle) {
    GetBaseDB()->set_character_behavior(character_id, behavior_handle);
}

// ---- CharacterBehavior Create/Destroy ----

// pub::GF::CharacterBehaviorCreate -- retail .text:06d18000. BaseDB::create_behavior
// (FUN_06d14b70) with description+0x25c as the session filter.
SERVER_API unsigned long __cdecl CharacterBehaviorCreate(const CharacterBehaviorDescription& description) {
    ScriptBehavior Behavior;
    Behavior.init(description);
    return GetBaseDB()->create_behavior(Behavior, description.Filter);
}

// pub::GF::CharacterBehaviorDestroy -- retail .text:06d18090 (destroy_behavior is
// FUN_06d14cc0); -1 is the empty handle.
SERVER_API void __cdecl CharacterBehaviorDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_behavior(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// ---- Character Placement functions ----
// All go through BaseDB::find_location (FUN_06d14170; a miss FDUMPs 'Location %u is
// invalid' and returns), then a FUN_06d127xx helper that reads the RoomData [entry+0x8]
// unchecked. The placement helpers index the vector with no bounds or element null test.

// pub::GF::GetNumCharacterPlacements -- retail 0x380e0 (FUN_06d12720: RoomData+0x178/0x17C).
SERVER_API int __cdecl GetNumCharacterPlacements(unsigned int location_id) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return 0;
    RoomData* room = Entry->Data;
    return (int)room->CharacterPlacements.size();
}

// pub::GF::GetCharacterPlacementByName -- retail 0x38100 (FUN_06d12740). Only the record's
// name pointer is null-tested; `name` goes straight to stricmp.
SERVER_API int __cdecl GetCharacterPlacementByName(unsigned int location_id, const char* name, int& out_index) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return -2;
    RoomData* room = Entry->Data;
    for (unsigned int i = 0; i < room->CharacterPlacements.size(); i++) {
        CharPlaceInfo* cp = room->CharacterPlacements[i];
        if (cp->Name && _stricmp(cp->Name, name) == 0) {
            out_index = (int)i;
            return 0;
        }
    }
    out_index = -1;
    return -3;
}

// pub::GF::GetCharacterPlacementName -- retail 0x38140 (FUN_06d127b0). Returns the name
// (record +0x00) as-is, null included.
SERVER_API const char* __cdecl GetCharacterPlacementName(unsigned int location_id, int index) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return "";
    RoomData* room = Entry->Data;
    return room->CharacterPlacements[index]->Name;
}

// pub::GF::GetCharacterPlacementOccupancy -- retail 0x38170. CharPlaceInfo+0x34 CharType
// (-1 none, 0 npc, 1 pc, 2 rpc; see common_goods.h and RoomData::read_set_script).
SERVER_API unsigned long __cdecl GetCharacterPlacementOccupancy(unsigned int location_id, int index) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return 0;
    RoomData* room = Entry->Data;
    return (unsigned long)room->CharacterPlacements[index]->CharType;
}

// pub::GF::GetCharacterPlacementPosture -- retail 0x381a0. CharPlaceInfo+0x38 Posture
// (0 stand, 1 sitlow, 2 sithigh, 3 special, -1 unset).
SERVER_API int __cdecl GetCharacterPlacementPosture(unsigned int location_id, int index, unsigned long& out_posture) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return -2;
    RoomData* room = Entry->Data;
    out_posture = (unsigned long)room->CharacterPlacements[index]->Posture;
    return 0;
}

// pub::GF::IsCharacterPlacementNormal -- retail 0x381e0. CharPlaceInfo+0x3C bZoneScript
// (0 = "zg/" zone-group prefix = normal, 1 = "zs/" zone-script prefix = special).
SERVER_API bool __cdecl IsCharacterPlacementNormal(unsigned int location_id, int index) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return true;
    RoomData* room = Entry->Data;
    return room->CharacterPlacements[index]->bZoneScript == 0;
}

// pub::GF::IsCharacterPlacementSpecial -- retail 0x38210 (bZoneScript == 1).
SERVER_API bool __cdecl IsCharacterPlacementSpecial(unsigned int location_id, int index) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return false;
    RoomData* room = Entry->Data;
    return room->CharacterPlacements[index]->bZoneScript == 1;
}

// pub::GF::EnumerateCharacterPlacementIni -- retail 0x38250 (FUN_06d12850). The mangled
// name's trailing `1` repeats PAX, so the context is a void*. No callback null test.
SERVER_API void __cdecl EnumerateCharacterPlacementIni(unsigned int location_id,
    void (__cdecl *callback)(int, INI_Reader*, void*), void* context) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return;
    RoomData* room = Entry->Data;
    room->enumerate_CharacterPlacement_ini(callback, context);
}

// pub::GF::GetCharacterOnPlacement -- retail .text:06d18280 (FUN_06d127d0).
SERVER_API unsigned int __cdecl GetCharacterOnPlacement(unsigned int location_id, unsigned int filter_id, int placement_index) {
    FRoomEntry* room = GetBaseDB()->find_location(location_id);
    if (!room) return 0;
    return room->get_character_on_placement(filter_id, placement_index);
}

// pub::GF::GetCharacterPlacementProperties -- retail 0x382b0. Only the placement name is
// null-tested; out_props is not.
SERVER_API bool __cdecl GetCharacterPlacementProperties(unsigned int location_id, int index, SetpointProperties* out_props) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return false;
    RoomData* room = Entry->Data;
    CharPlaceInfo* cp = room->CharacterPlacements[index];
    if (!cp->Name) return false;
    out_props->extract_from_name(cp->Name);
    return out_props->bValid != 0;
}

// pub::GF::FormCharacterPlacementName -- retail .text:06d18300, a tail jump to
// SetpointProperties::combine_to_name; no null test.
SERVER_API const char* __cdecl FormCharacterPlacementName(SetpointProperties const* props) {
    return props->combine_to_name();
}

// ---- MissionVendor ----

// pub::GF::MissionVendorOfferCreate -- retail .text:06d18310. BaseDB::create_mission_vendor
// (FUN_06d14dc0) with description+0x5c as the session filter.
SERVER_API unsigned long __cdecl MissionVendorOfferCreate(const MissionVendorOfferDescription& description) {
    MissionVendorProperties Props;
    Props.init(description);
    return GetBaseDB()->create_mission_vendor(Props, description.Filter);
}

// pub::GF::MissionVendorOfferDestroy -- retail .text:06d18390 (FUN_06d14f10).
SERVER_API void __cdecl MissionVendorOfferDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_mission_vendor(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// pub::GF::GetMissionVendorOfferCount -- retail .text:06d183c0 (FUN_06d15010).
SERVER_API int __cdecl GetMissionVendorOfferCount(unsigned int base_id, unsigned int vendor_type) {
    return GetBaseDB()->count_mission_vendors(base_id, vendor_type);
}

// pub::GF::MissionVendorAcceptance -- retail .text:06d183e0 (FUN_06d150a0, unconditional).
SERVER_API void __cdecl MissionVendorAcceptance(unsigned long handle, bool accepted, const FmtStr& reason_fmtstr, unsigned int session_id) {
    GetBaseDB()->mission_vendor_acceptance(handle, accepted, reason_fmtstr, session_id);
}

// pub::GF::ReportWhyMissionVendorEmpty -- retail 0x38410. A real packet send: IClient +0xa8
// (FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY) to client_id (Content passes the mission
// state's PlayerId). The client interface is not null-tested.
SERVER_API int __cdecl ReportWhyMissionVendorEmpty(unsigned int client_id, MVEmptyReason reason) {
    ::g_pClientInterface->SendServerGFMissionVendorWhyEmpty(client_id, (unsigned int)reason);
    return 0;
}

// ---- NewsBroadcast ----

// pub::GF::NewsBroadcastCreate -- retail .text:06d18430. BaseDB::create_news_broadcast
// (FUN_06d15140) with description+0x5c as the session filter.
SERVER_API unsigned long __cdecl NewsBroadcastCreate(const NewsBroadcastDescription& description) {
    NewsBroadcastProperties Props;
    Props.init(description);
    return GetBaseDB()->create_news_broadcast(Props, description.Filter);
}

// pub::GF::NewsBroadcastDestroy -- retail .text:06d184b0 (FUN_06d15290).
SERVER_API void __cdecl NewsBroadcastDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_news_broadcast(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// ---- AmbientScript ----

// pub::GF::AmbientScriptCreate -- retail .text:06d184e0. BaseDB::create_ambient_script
// (FUN_06d15390) with description+0x10 as the session filter.
SERVER_API unsigned long __cdecl AmbientScriptCreate(const AmbientScriptDescription& description) {
    AmbientScriptProperties Props;
    Props.init(description);
    return GetBaseDB()->create_ambient_script(Props, description.Filter);
}

// pub::GF::AmbientScriptDestroy -- retail .text:06d18560 (FUN_06d15510).
SERVER_API void __cdecl AmbientScriptDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_ambient_script(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// ---- GetRtcPerformanceSlider ----

// pub::GF::GetRtcPerformanceSlider -- retail 0x38590. Gated on IsMPServer (not
// SinglePlayer): the slider outside an MP server, else 1.0f.
SERVER_API float __cdecl GetRtcPerformanceSlider() {
    if (!::IsMPServer()) {
        return ::RtcSlider::get_slider();
    }
    return 1.0f;
}

} // namespace GF

// ===========================================================================
// pub::Reputation exports
// ===========================================================================

} // namespace pub -- the vibe-id bitmap is file-level Server state, not pub::

// Vibe ID bitmap, retail .data:06d8f768 (a std::bitset<0x1000>; FUN_06d15f90 is its
// out_of_range throw), and the last allocated index .data:06d8f968. Both .bss.
static unsigned int g_VibeBitmap[128] = {};
static unsigned int g_VibeBitmapHint = 0;

// Server_VibeID_Alloc -- retail FUN_06d45b60, cdecl. Callers: PlayerData::apply_saved_character
// (FUN_06d4a5d0, server_savegame.h) and pub::Reputation below. Scans from hint + 1; the
// wrap to 0 skips the hint test, and arriving back at the hint fails whatever its bit
// says (the hint slot itself is never tried). Failure writes 0xffffffff.
void Server_VibeID_Alloc(unsigned int* OutId)
{
    unsigned int Index = g_VibeBitmapHint + 1;
    bool bFirstLap = true;
    if (Index >= 0x1000)
        Index = 0;
    for (;;) {
        unsigned int const Mask = 1u << (Index & 0x1f);
        bool const bTaken = (g_VibeBitmap[Index >> 5] & Mask) != 0;
        if (!bFirstLap)
            break;
        if (!bTaken) {
            g_VibeBitmap[Index >> 5] |= Mask;
            g_VibeBitmapHint = Index;
            *OutId = Index;
            return;
        }
        ++Index;
        if (Index >= 0x1000)
            Index = 0;
        else if (Index == g_VibeBitmapHint)
            bFirstLap = false;
    }
    *OutId = 0xFFFFFFFFu;
}

// Server_VibeID_Free -- retail FUN_06d45c20, cdecl, id by address. Only caller
// pub::Reputation::Free. An id >= 0x1000 throws std::out_of_range (bitset's _Xran,
// FUN_06d15f90), as retail.
static void Server_VibeID_Free(unsigned int const* Id)
{
    unsigned int const Index = *Id;
    if (Index >= 0x1000)
        throw std::out_of_range("invalid bitset<N> position");
    g_VibeBitmap[Index >> 5] &= ~(1u << (Index & 0x1f));
}

namespace pub {

// Server_NotifyRepChange -- retail FUN_06d5f130, cdecl (int const* Vibe, unsigned int
// SessionId). Called by pub::Reputation's Alloc, SetAffiliation, SetReputation, SetAttitude
// and the group-pair SetReputation. Both branches send IClient +0x108
// (FLPACKET_SERVER_SETREPUTATION) with an FServerSetReputationInfo (rpc_iclient_args.h).
// SP: Vibe::Get fills the record and it goes to every active player.
// MP: only to SessionId itself (not the slot's +0x348), with AttitudeTowards the player's
// vibe (+0x318).
// Deviation: fields retail leaves as stack residue (+0x04 in SP; +0x08/+0x0c in MP, which
// rpclocal serializes) are zeroed by the port; no retail value exists to copy.
static void Server_NotifyRepChange(int const* vibe_handle, unsigned int session_id)
{
    if (SinglePlayer()) {
        FServerSetReputationInfo Info;
        Info.Attitude = 0.0f;               // never written by retail (stack residue)
        Info.Affiliation = 0;
        Info.Rank = 0;
        Info.Relations = nullptr;
        Info.RelationCount = 0;
        Info.Pad_0x15[0] = 0;
        Info.Pad_0x15[1] = 0;
        Info.Pad_0x15[2] = 0;
        Info.DisplayName[0] = L'\0';
        Info.VibeHandle = *vibe_handle;
        Info.Relations = static_cast< ::Reputation::Relation*>(
            ::operator new(GlobalReputation::group_count() * 8));
        wchar_t const* VibeName = nullptr;
        GlobalReputation::Vibe::Get(Info.VibeHandle, Info.Affiliation, Info.Rank,
                                    Info.RelationCount, Info.Relations, Info.NameA,
                                    Info.NameB, VibeName);
        if (VibeName != nullptr) {
            wcscpy(Info.DisplayName, VibeName);                         // unbounded, as retail
        } else {
            Info.DisplayName[0] = L'\0';
        }
        for (PlayerData* Active = Players.traverse_active(nullptr); Active != nullptr;
             Active = Players.traverse_active(Active)) {
            g_pClientInterface->SendServerSetReputation(Active->client_id, Info);
        }
        ::operator delete(Info.Relations);                              // FUN_06d5ffb0
        return;
    }

    if (session_id == 0 || (session_id - 1) >= PlayerDB_GetMaxPlayers()) return;
    if (Players[session_id].client_id == 0) return;

    FServerSetReputationInfo Info;
    Info.Affiliation = 0;                    // stack residue in retail
    Info.Rank = 0;
    Info.Relations = nullptr;
    Info.RelationCount = 0;
    Info.Pad_0x15[0] = 0;
    Info.Pad_0x15[1] = 0;
    Info.Pad_0x15[2] = 0;
    Info.DisplayName[0] = L'\0';
    Info.VibeHandle = *vibe_handle;
    int const PlayerVibe = Players[session_id].rep_vibe;
    Info.Attitude = 0.0f;
    GlobalReputation::Vibe::AttitudeTowards(Info.VibeHandle, PlayerVibe,
                                            Info.Attitude);
    g_pClientInterface->SendServerSetReputation(session_id, Info);
}

namespace Reputation {

// pub::Reputation::Alloc -- retail 0x7f0f0: Server_VibeID_Alloc, Vibe::Alloc, Vibe::SetName,
// Server_NotifyRepChange.
SERVER_API int __cdecl Alloc(int& vibe_handle, const FmtStr& name_fmtstr, const FmtStr& nickname_fmtstr) {
    unsigned int vibe_id = (unsigned int)vibe_handle;
    Server_VibeID_Alloc(&vibe_id);
    vibe_handle = (int)vibe_id;
    // A non-zero Vibe::Alloc result skips SetName and the notify.
    int result = GlobalReputation::Vibe::Alloc(vibe_handle);
    if (result == 0) {
        GlobalReputation::Vibe::SetName(vibe_handle, name_fmtstr, nickname_fmtstr, nullptr);
        int vibe_tmp = vibe_handle;
        Server_NotifyRepChange(&vibe_tmp, 0);
    }
    return result;
}

// pub::Reputation::Free -- retail 0x7f3e0. No guard: Vibe::Free, then the bitmap bit is
// cleared through the same reference (Server_VibeID_Free throws for >= 0x1000).
SERVER_API int __cdecl Free(const int& vibe_handle) {
    int result = GlobalReputation::Vibe::Free(vibe_handle);
    Server_VibeID_Free(reinterpret_cast<unsigned int const*>(&vibe_handle));
    return result;
}

// pub::Reputation::GetAffiliation -- retail 0x7f400. 0xFFFFFFFF on failure.
SERVER_API int __cdecl GetAffiliation(const int& vibe_handle, unsigned int& out_affiliation) {
    int result = GlobalReputation::Vibe::GetAffiliation(vibe_handle, out_affiliation, true);
    if (result == 0) return 0;
    out_affiliation = 0xFFFFFFFF;
    return -2;
}

// pub::Reputation::SetAffiliation -- retail 0x7f430. No guard.
SERVER_API int __cdecl SetAffiliation(const int& vibe_handle, const unsigned int& affiliation) {
    int result = GlobalReputation::Vibe::SetAffiliation(vibe_handle, affiliation, false);
    if (result == 0) {
        Server_NotifyRepChange(&vibe_handle, 0);
        return 0;
    }
    return -2;
}

// pub::Reputation::SetReputation (vibe overload) -- retail 0x7f470.
SERVER_API int __cdecl SetReputation(const int& vibe_handle, const unsigned int& group_id, float value) {
    int result = GlobalReputation::Vibe::SetGroupFeelingsTowards(vibe_handle, group_id, value);
    if (result == 0) {
        int vibe_tmp = vibe_handle;
        Server_NotifyRepChange(&vibe_tmp, 0);
        return 0;
    }
    return -2;
}

// pub::Reputation::GetReputation (vibe overload) -- retail 0x7f4b0.
SERVER_API int __cdecl GetReputation(const int& vibe_handle, const unsigned int& group_id, float& out_value) {
    out_value = 0.0f;
    int result = GlobalReputation::Vibe::GetGroupFeelingsTowards(vibe_handle, group_id, out_value);
    if (result == 0) return 0;
    return -2;
}

// pub::Reputation::GetAttitude -- retail 0x7f4e0.
SERVER_API int __cdecl GetAttitude(const int& vibe1, const int& vibe2, float& out_attitude) {
    out_attitude = 0.0f;
    int result = GlobalReputation::Vibe::AttitudeTowards(vibe1, vibe2, out_attitude);
    if (result == 0) return 0;
    if (result == -1) return -2;
    // Deviation: for -2 retail returns the out pointer cast to int; the port returns -3.
    if (result == -2) return -3;
    return result;
}

// pub::Reputation::SetAttitude -- retail 0x7f520. Outside SP, the target vibe's client
// session is notified.
SERVER_API int __cdecl SetAttitude(const int& vibe1, const int& vibe2, float attitude) {
    int result = GlobalReputation::Vibe::AdjustAttitude(vibe1, vibe2, attitude);
    if (result == 0) {
        if (!SinglePlayer()) {
            unsigned int client_id = GlobalReputation::Vibe::GetClientID(vibe2);
            if (client_id != 0) {
                int vibe_tmp = vibe1;
                Server_NotifyRepChange(&vibe_tmp, client_id);
            }
        }
        return 0;
    }
    if (result == -1) return -2;
    // Deviation, as GetAttitude: retail returns a non-error value for -2.
    if (result == -2) return -3;
    return result;
}

// pub::Reputation::GetRank -- retail 0x7f590. No guard. The rank is converted as an
// unsigned value (FILD of a zero-extended QWORD).
SERVER_API int __cdecl GetRank(const int& vibe_handle, float& out_rank) {
    unsigned rank_val = 0;
    int result = GlobalReputation::Vibe::GetRank(vibe_handle, rank_val);
    if (result == 0) {
        out_rank = (float)rank_val;
        return 0;
    }
    out_rank = 0.0f;
    return -2;
}

// pub::Reputation::SetRank -- retail 0x7f5e0. Outside SP, when the vibe has a client
// (0x3fffffff mask real here) and the rank actually changed, every active player gets
// IClient slot 117 (+0x1d4, SendServerMiscObjUpdateRank); the interface is not null-tested.
SERVER_API int __cdecl SetRank(const int& vibe_handle, float rank) {
    unsigned int client_id = 0;
    // Seeded 0xffffffff, so a rank of 0 counts as a change when GetRank never runs.
    unsigned int old_rank = 0xffffffff;
    if (!SinglePlayer()) {
        client_id = GlobalReputation::Vibe::GetClientID(vibe_handle);
        if ((client_id & 0x3fffffff) != 0) {
            int cid = (int)client_id;
            GlobalReputation::Vibe::GetRank(cid, old_rank);
        }
    }
    unsigned new_rank = (unsigned)(int)rank;                             // ftol
    int result = GlobalReputation::Vibe::SetRank(vibe_handle, new_rank);
    if (result != 0) return -2;
    if ((client_id & 0x3fffffff) != 0 && new_rank != old_rank) {
        for (PlayerData* Active = Players.traverse_active(nullptr); Active != nullptr;
             Active = Players.traverse_active(Active)) {
            g_pClientInterface->SendServerMiscObjUpdateRank(Active->client_id, client_id,
                                                            static_cast<int>(new_rank));
        }
    }
    return 0;
}

// pub::Reputation::GetReputationGroup -- retail 0x7f6a0. The name is copied into a local
// TString<16> (at most 15 chars) for get_id.
SERVER_API int __cdecl GetReputationGroup(unsigned int& out_id, const char* name) {
    char local_name[16] = {};
    if (name != nullptr) {
        unsigned int len = 0;
        while (len < 15 && name[len] != '\0') {
            local_name[len] = name[len];
            len++;
        }
        local_name[len] = '\0';
    }
    // Retail builds the TString in place; Length is zeroed for a null name.
    TString<16> tname;
    tname.Length = 0;
    while (tname.Length < 15 && local_name[tname.Length] != '\0') {
        ++tname.Length;
    }
    memcpy(tname.Data, local_name, 16);
    unsigned int id = GlobalReputation::get_id(tname);
    if (id != 0xFFFFFFFF) {
        out_id = id;
        return 0;
    }
    out_id = 0xFFFFFFFF;
    return -2;
}

// pub::Reputation::GetName -- retail 0x7f730.
SERVER_API int __cdecl GetName(const int& vibe_handle, FmtStr& out_name_fmta, FmtStr& out_name_fmtb) {
    wchar_t const* wide_name = nullptr;
    int result = GlobalReputation::Vibe::GetName(vibe_handle, out_name_fmta, out_name_fmtb, wide_name);
    if (result == 0) return 0;
    return -2;
}

// pub::Reputation::GetReputation (group, group) -- retail 0x7f760,
// ?GetReputation@Reputation@pub@@YAHABI0AAM@Z (an overload, not a separate name).
SERVER_API int __cdecl GetReputation(const unsigned int& group_a, const unsigned int& group_b, float& out_value) {
    if (group_a == 0xFFFFFFFF || group_b == 0xFFFFFFFF) return -4;
    int result = GlobalReputation::get_feelings_towards(group_a, group_b, out_value);
    if (result == 0) return 0;
    return -2;
}

// pub::Reputation::SetReputation (group, group) -- retail 0x7f7a0,
// ?SetReputation@Reputation@pub@@YAHABI0M@Z. Applies value - current via affect_relation.
SERVER_API int __cdecl SetReputation(const unsigned int& group_a, const unsigned int& group_b, float value) {
    if (group_a == 0xFFFFFFFF || group_b == 0xFFFFFFFF) return -4;
    float current = 0.0f;
    int result = GlobalReputation::get_feelings_towards(group_a, group_b, current);
    if (result == 0) {
        float delta = value - current;
        GlobalReputation::affect_relation(group_a, group_b, delta);
        int tmp = (int)group_a;
        Server_NotifyRepChange(&tmp, 0);
        return 0;
    }
    return -2;
}

// pub::Reputation::GetGroupName -- retail 0x7f810. The out-param is written on failure too.
SERVER_API int __cdecl GetGroupName(const unsigned int& group_id, unsigned int& out_name_id) {
    unsigned int name_id = GlobalReputation::get_name(group_id);
    out_name_id = name_id;
    if (name_id != 0xFFFFFFFF) return 0;
    return -2;
}

// pub::Reputation::GetShortGroupName -- retail 0x7f840. As GetGroupName.
SERVER_API int __cdecl GetShortGroupName(const unsigned int& group_id, unsigned int& out_name_id) {
    unsigned int name_id = GlobalReputation::get_short_name(group_id);
    out_name_id = name_id;
    if (name_id != 0xFFFFFFFF) return 0;
    return -2;
}

// pub::Reputation::GetGroupFeelingsTowards -- retail 0x7f870. Only -1 maps to -2.
SERVER_API int __cdecl GetGroupFeelingsTowards(const int& vibe_handle, const unsigned int& group_id, float& out_feeling) {
    int result = GlobalReputation::Vibe::GetGroupFeelingsTowards(vibe_handle, group_id, out_feeling);
    if (result != -1) return 0;
    return -2;
}

// pub::Reputation::GetReputation (name) -- retail 0x7f8a0, ?GetReputation@Reputation@pub@@YAHAAHPBD@Z
// (out param is `int&`, written unconditionally). The id goes to FindSolarRep as a stack
// ID_String.
SERVER_API int __cdecl GetReputation(int& out_rep, const char* name) {
    ID_String Key;
    Key.Id = CreateID(name);
    int rep = ::SolarReps::FindSolarRep(Key);
    out_rep = rep;
    if (rep != -1) return 0;
    return -2;
}

// pub::Reputation::GetReputation (ID_String) -- retail 0x7f8e0,
// ?GetReputation@Reputation@pub@@YAHAAHABUID_String@@@Z. Out written unconditionally.
SERVER_API int __cdecl GetReputation(int& out_rep, const ID_String& id) {
    int rep = ::SolarReps::FindSolarRep(id);
    out_rep = rep;
    if (rep != -1) return 0;
    return -2;
}

// pub::Reputation::EnumerateGroups -- retail 0x06d5f910, ordinal 120. An 8-byte stack
// adapter (vtable 0x06d5f940) for Common's Reputation::enumerate; its thunk dereferences
// the group id and calls the Enumerator's slot 0 by value. Always returns 0.
SERVER_API int __cdecl EnumerateGroups(Enumerator& enumerator) {
    struct FRepGroupAdapter : GlobalReputation::RepGroupCB {
        Enumerator* Sink;
        virtual char Execute(unsigned const* GroupId) { return Sink->Execute(*GroupId); }
    };
    FRepGroupAdapter Adapter;
    Adapter.Sink = &enumerator;
    GlobalReputation::enumerate(&Adapter);
    return 0;
}

} // namespace Reputation

// ===========================================================================
// pub::Controller exports
// ===========================================================================
namespace Controller {

SERVER_API unsigned int __cdecl Create(const char* dll_path, const char* instance_name,
                            const CreateParms* create_parms, PRIORITY priority) {
    return ::Server_ControllerCreate(dll_path, instance_name, (void*)create_parms, (unsigned int)priority);
}

SERVER_API void __cdecl Destroy(unsigned int flid) {
    ::Server_ControllerDestroy(flid);
}

// pub::Controller::SetHeartbeatInterval -- retail 0x06d44030. Sets the controller node's
// interval (+0x10) and a random phase (+0x14) = rand() * 2^-15 * Interval; -2 when the
// node is not found. No controller message is sent.
SERVER_API int __cdecl SetHeartbeatInterval(const unsigned int& Flid, float Interval) {
    return Server_ControllerSetHeartbeat(Flid, Interval);
}

// pub::Controller::_SendMessage -- retail 0x06d44080. Flid 0 is the broadcast arm, the one
// FUN_06d43650 (every bare "send message N" in Server.dll) uses.
SERVER_API int __cdecl _SendMessage(const unsigned int& Flid, EMessage MsgId, const void* MsgData) {
    return Server_ControllerDispatch(Flid, MsgId, MsgData);
}

} // namespace Controller

// pub::SetTimer -- retail 0x06d5ee00. Only Timeout < 0 is rejected (-4); the Flid is not
// validated. A 0x18-byte node goes on the timer list (DAT_06d90468, count DAT_06d9046c).
// Nothing is sent at insert time; message 0x3e goes to that one controller when the tick
// (0x06d5df60) expires the timer.
SERVER_API int __cdecl SetTimer(const unsigned int& Flid, const Controller::TimerExpired& Timer, float Timeout) {
    return Server_ControllerSetTimer(Flid, Timer.Param1, Timer.Param2, Timeout);
}

// ===========================================================================
// pub::Zone exports
// ===========================================================================
namespace Zone {

// pub::Zone::VerifyId -- retail 0x1b920.
SERVER_API bool __cdecl VerifyId(unsigned int zone_id) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    return zone != nullptr;
}

// pub::Zone::GetId -- retail 0x1b940.
SERVER_API unsigned int __cdecl GetId(unsigned int system_id, const char* name) {
    Universe::IZone const* zone = Universe::get_zone(system_id, name);
    if (zone != nullptr) {
        return zone->NicknameId;
    }
    return 0;
}

// pub::Zone::GetSystem -- retail 0x1b970.
SERVER_API unsigned int __cdecl GetSystem(unsigned int zone_id) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        return zone->SystemId;
    }
    return 0;
}

// pub::Zone::GetPopulation -- retail 0x1b990, ?GetPopulation@Zone@pub@@YAHIAAPBV?$weighted_vector@I@@@Z
// (a reference to a const pointer). Slot +0x6c is the address of IZone::Population (+0x8c).
SERVER_API int __cdecl GetPopulation(unsigned int zone_id, const weighted_vector<unsigned int>*& out_population) {

    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_population = &zone->Population;
        return 0;
    }
    out_population = nullptr;
    return -2;
}

// pub::Zone::GetPos -- retail 0x1b9d0. No id-mask guard.
SERVER_API Vector __cdecl GetPos(unsigned int zone_id) {
    Vector result = {};
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (!zone) return result;
    result = zone->Pos;
    return result;
}

// pub::Zone::GetOrientation -- retail 0x1ba20 (vtable +0x14).
SERVER_API int __cdecl GetOrientation(const unsigned int& zone_id, Matrix& out_matrix) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_matrix = zone->GetOrientation();
        return 0;
    }
    return -2;
}

// pub::Zone::GetRadius -- retail 0x1ba60. Tail call to ZONE::GetRadius (vtable +0x30, the
// cached per-shape radius).
SERVER_API float __cdecl GetRadius(unsigned int zone_id) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr)
        return zone->GetRadius();
    return 0.0f;
}

// pub::Zone::GetSize -- retail 0x1ba80.
SERVER_API int __cdecl GetSize(unsigned int zone_id, Vector& out_size) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_size = zone->Size;
        return 0;
    }
    return -2;
}

// pub::Zone::GetShape -- retail 0x1bad0.
SERVER_API int __cdecl GetShape(unsigned int zone_id, ZoneShape& out_shape) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_shape = (ZoneShape)zone->Shape;
        return 0;
    }
    out_shape = (ZoneShape)0;
    return -2;
}

// pub::Zone::ClassifyPoint -- retail 0x1bb10 (vtable +0x48). No zone -> 1.0 (outside).
SERVER_API float __cdecl ClassifyPoint(unsigned int zone_id, const Vector& point) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        return zone->ClassifyPoint(point);
    }
    return 1.0f;
}

// pub::Zone::InZone -- retail 0x1bb40 (vtable +0x34).
SERVER_API bool __cdecl InZone(unsigned int zone_id, const Vector& point, float margin) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        return zone->InZone(point, margin);
    }
    return false;
}

// pub::Zone::GetDistance -- retail 0x1bb70 (vtable +0x3c). Signed (negative inside), not
// clamped.
SERVER_API float __cdecl GetDistance(unsigned int zone_id, const Vector& point) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        return zone->GetDistance(point);
    }
    return 0.0f;
}

// pub::Zone::Intersect -- retail 0x1bba0 (vtable +0x4c).
SERVER_API bool __cdecl Intersect(unsigned int zone_id, const Vector& start, const Vector& end, Vector& hit_point) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        return zone->Intersect(start, end, hit_point) ? true : false;
    }
    return false;
}

// pub::Zone::GetProperties -- retail 0x1bbe0 (vtable +0x1c).
SERVER_API int __cdecl GetProperties(unsigned int zone_id, unsigned long& out_props) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_props = (unsigned long)zone->PropertyFlags;
        return 0;
    }
    return -2;
}

// pub::Zone::GetLootableInfo -- retail 0x1bc10. The string outs are ID_String::clear'd and
// the ints zeroed first.
SERVER_API int __cdecl GetLootableInfo(unsigned int zone_id, ID_String& out_asteroid_loot,
                                        ID_String& out_dynamic_loot, int& out_difficulty,
                                        int& out_count_min, int& out_count_max) {
    out_asteroid_loot.clear();
    out_dynamic_loot.clear();
    out_count_max = 0;
    out_count_min = 0;
    out_difficulty = 0;
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // vtable +0x7c returns IZone::OwnedLootEntry, a CmnAsteroid::LootableZoneEntry
        // ("[LootableZone]", installed by the +0x8c setter FUN_0633aea0). A null record
        // still returns 0.
        CmnAsteroid::LootableZoneEntry const* const Loot = zone->GetLootableInfo();
        if (Loot != nullptr) {
            out_asteroid_loot = Loot->AsteroidLootCommodityId;  // +0x08
            out_dynamic_loot = Loot->DynamicLootCommodityId;    // +0x0c
            out_difficulty = Loot->AsteroidLootDifficulty;      // +0x20
            out_count_min = Loot->AsteroidLootCountMin;         // +0x10
            out_count_max = Loot->AsteroidLootCountMax;         // +0x14
        }
        return 0;
    }
    return -2;
}

// pub::Zone::GetName -- retail 0x1bca0 (vtable +0x84).
SERVER_API int __cdecl GetName(unsigned int zone_id, unsigned int& out_name_id) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_name_id = (unsigned int)zone->IdsName;
        return 0;
    }
    return -2;
}

} // namespace Zone

// ===========================================================================
// pub::System exports
// ===========================================================================
namespace System {

// pub::System::LoadSystem -- retail .text:06cface0 (RVA 0x1ace0, ordinal 327). No id mask
// test (that lives inside add_player); a miss returns -1, not -2. StarSystem::add_player(0)
// (FUN_06d07e20) loads the system and then falls out because the session id is zero.
SERVER_API int __cdecl LoadSystem(const unsigned int& system_id) {
    FStarSystem* sys = SysDB_LookupSystem(system_id);
    if (!sys) {
        return -1;
    }
    sys->add_player(0);
    return 0;
}

// pub::System::Find -- retail .text:06cfb260 (RVA 0x1b260, ordinal 124). Looks the solar up
// with StarSystem::find_solar; out_id is written only on success.
SERVER_API int __cdecl Find(const unsigned int& system_id, const char* name, unsigned int& out_id) {
    if (name == nullptr || name[0] == '\0') return -4;
    if ((system_id & 0x3fffffff) == 0) return -4;
    FStarSystem* sys = SysDB_LookupSystem(system_id);
    if (!sys) return -2;
    unsigned int Key = CreateID(name);
    GameObject* Solar = sys->find_solar(&Key);
    if (Solar == nullptr) return -3;
    out_id = Solar->get_id();
    return 0;
}

// pub::System::GetName -- retail 0x1b8e0 (ISystem vtable +0xc).
SERVER_API int __cdecl GetName(unsigned int system_id, unsigned int& out_name_id) {
    out_name_id = 0;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;
    out_name_id = sys->StridName;
    return 0;
}

// pub::System::GetNestedProperties -- retail .text:06cfb880 (RVA 0x1b880). ISystem slot 9
// (+0x24) with a zero-radius sphere at the position.
SERVER_API int __cdecl GetNestedProperties(const unsigned int& system_id, const Vector& position, unsigned long& out_props) {
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;
    Geometry::Sphere Query;
    Query.Center = position;
    Query.Radius = 0.0f;
    out_props = sys->GetNestedProperties(&Query);
    return 0;
}

// pub::System::EnumerateConnections -- retail .text:06cfb170 (RVA 0x1b170). ISystem slots
// 13/14 (+0x34/+0x38); slot 14's (always false) result is not read. Type 0 takes every
// record, 1/2 only that GateType, any other type none. Appended at [Count] with no bound
// check.
SERVER_API int __cdecl EnumerateConnections(const unsigned int& system_id, ConnectionEnumerator& enumerator, ConnectionType connection_type) {
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;

    int const Count = sys->GetConnectionCount();
    for (int Index = 0; Index < Count; ++Index) {
        Universe::FSystemConnection Record;
        Record.Object.clear();
        sys->GetConnection(static_cast<unsigned int>(Index), &Record);
        if (connection_type == 1) {
            if (Record.GateType != 1) continue;
        } else if (connection_type == 2) {
            if (Record.GateType != 2) continue;
        } else if (connection_type != 0) {
            continue;
        }
        unsigned int const Slot = enumerator.Count;
        enumerator.GotoSystemId[Slot] = Record.GotoSystemId;
        enumerator.Object[Slot] = Record.Object;
        enumerator.ObjectId[Slot] = Record.ObjectId;
        enumerator.Pos[Slot] = Record.Pos;
        ++enumerator.Count;
    }
    return 0;
}

// pub::System::EnumerateObjects -- retail .text:06cfb2e0 (RVA 0x1b2e0). Retail constructs an
// INI_Reader it never opens. ISystem slots 15/16 (+0x3c/+0x40), slot 16's result not read.
// Each record becomes a 0x64-byte FSysObj (two strncpy of 0x20, no terminator forced);
// enumerator slot 0 returning false stops the walk.
SERVER_API int __cdecl EnumerateObjects(const unsigned int& system_id, SysObjEnumerator& enumerator) {
    int Result = -2;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    INI_Reader Reader;
    if (!sys) return Result;

    Result = 0;
    int const Count = sys->GetObjectCount();
    for (int Index = 0; Index < Count; ++Index) {
        Universe::FSystemObject Record;
        Record.Nickname.Length = 0;
        Record.Nickname.Data[0] = '\0';
        Record.Reputation.Length = 0;
        Record.Reputation.Data[0] = '\0';
        Record.NextRing.Length = 0;
        Record.NextRing.Data[0] = '\0';
        Record.PrevRing.Length = 0;
        Record.PrevRing.Data[0] = '\0';
        sys->GetSystemObject((unsigned int)Index, &Record);

        FSysObj Object;
        strncpy(Object.Nickname, Record.Nickname.Data, 0x20);
        Object.Pos = Record.Pos;
        Object.ArchetypeId = Record.ArchetypeId;
        Object.IdsName = Record.IdsName;
        Object.IdsInfo = Record.IdsInfo;
        strncpy(Object.Reputation, Record.Reputation.Data, 0x20);
        Object.DockWith = Record.DockWith;
        Object.Goto = Record.Goto;
        Object.SystemId = system_id;
        if (!enumerator.OnObject(&Object)) break;
    }
    return Result;
}

// pub::System::EnumerateZones -- retail .text:06cfad60 (RVA 0x1ad60). Opens the system file
// (ISystem slot 5) and, for every zone-list entry (slot 6), restores the reader state and
// calls enumerator slot 0 with the zone id and the reader. The enumerator's answer is
// ignored.
SERVER_API int __cdecl EnumerateZones(const unsigned int& system_id, ZoneEnumerator& enumerator) {
    int Result = -2;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    INI_Reader Reader;
    if (!sys) return Result;

    char Path[0x104];
    Universe::get_filename(Path, sys->GetFile());
    if (!Reader.open(Path, false)) return Result;

    std::list<Universe::FSystemZone> const* Zones = sys->GetZones();
    std::list<Universe::FSystemZone>::const_iterator It = Zones->begin();
    std::list<Universe::FSystemZone>::const_iterator End = Zones->end();
    for (; It != End; ++It) {
        Reader.set_state(It->State);
        unsigned int ZoneId = It->Zone->GetId();      // zone vt+0x04
        enumerator.OnZone(&ZoneId, &Reader);
    }
    Result = 0;
    return Result;
}

// pub::System::ScanObjects -- retail .text:06cfad10 (ordinal 0x1ad10, cdecl),
// ?ScanObjects@System@pub@@YAHABIQAIIABVVector@@MIAAI@Z (QAI: `unsigned int* const`).
// StarSystem::scan_objects (FUN_06d0bcd0): the third parameter is the buffer capacity, the
// sixth the type mask. No id mask test.
SERVER_API int __cdecl ScanObjects(const unsigned int& system_id, unsigned int* const buffer,
                                    unsigned int max_results, const Vector& position, float radius,
                                    unsigned int type_mask, unsigned int& out_count) {
    out_count = 0;
    StarSystem* const System = SysDB_LookupSystem(system_id);
    if (System == nullptr) return -2;
    out_count = System->scan_objects(buffer, max_results, &position, radius, type_mask);
    return 0;
}

// pub::System::InZones (Vector) -- retail .text:06cfb640 (RVA 0x1b640),
// ?InZones@System@pub@@YAHIABVVector@@MQAIIAAI@Z. Sphere query on the zone tree (ISystem
// slot 7) with a 0x800-entry result buffer; the max_zones bound is tested before each hit.
SERVER_API int __cdecl InZones(unsigned int system_id, const Vector& position, float radius,
                                       unsigned int* const out_zones, unsigned int max_zones,
                                       unsigned int& out_count) {
    out_count = 0;
    if (radius < 0.0f) return -4;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;

    ISpatialPartition* Tree = sys->GetZoneTree();
    Geometry::Sphere Query;
    Query.Center = position;
    Query.Radius = radius;
    FSpatialQueryResult Hits;
    Hits.Capacity = 0x800;
    Hits.Count = 0;
    SpatialFill(Tree, &Query, &Hits);

    for (unsigned int Index = 0; Index < Hits.Count; ++Index) {
        if (out_count >= max_zones) break;
        Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Hits.Items[Index]);
        if (SphereCull(Query, Zone)) continue;
        out_zones[out_count] = Zone->GetId();       // zone vt+0x04
        ++out_count;
    }
    return 0;
}

// pub::System::InZones (Transform) -- retail .text:06cfb760 (RVA 0x1b760),
// ?InZones@System@pub@@YAHIABVTransform@@MMMQAIIAAI@Z. The floats are the frustum's AngleX /
// AngleY / Distance (+0x30/+0x34/+0x38). No radius test in this overload; otherwise as the
// Vector overload with FrustumCull.
SERVER_API int __cdecl InZones(unsigned int system_id, const Transform& transform,
                                          float angle_x, float angle_y, float distance,
                                          unsigned int* const out_zones, unsigned int max_zones,
                                          unsigned int& out_count) {
    out_count = 0;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;

    ISpatialPartition* Tree = sys->GetZoneTree();
    Geometry::Frustum Query;
    Query.Xform = transform;
    Query.AngleX = angle_x;
    Query.AngleY = angle_y;
    Query.Distance = distance;
    FSpatialQueryResult Hits;
    Hits.Capacity = 0x800;
    Hits.Count = 0;
    SpatialFillFrustum(Tree, &Query, &Hits);

    for (unsigned int Index = 0; Index < Hits.Count; ++Index) {
        if (out_count >= max_zones) break;
        Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Hits.Items[Index]);
        if (FrustumCull(Query, Zone)) continue;
        out_zones[out_count] = Zone->GetId();       // zone vt+0x04
        ++out_count;
    }
    return 0;
}

} // namespace System

// ===========================================================================
// pub::AI exports
// ===========================================================================
namespace AI {

// OP_RTYPE values. Retail exports no enumerator names; the names in common_pub.h are the
// port's. The functions below return the literal retail values:
//   0  SubmitState: personality applied / zone cleared
//   1  SubmitDirective: accepted (sent to the player's client, or set on the AI)
//   2  SubmitDirective: the AI already has a current directive
//   4  bad id (both), and every SubmitState zone / flag / unknown-op exit
//   5  op failed validate(), no object, not a CSimple, or no behavior for this op
// (IBehaviorManager::set_directive_priority in Common adds 1 and 3.)

// IDirectiveInfo is three dwords no module names (common_iface.h keeps it as bytes).
// SubmitDirective builds it at S+0x10: word 0 = kind (0 cancel, 1 behavior, 2 player
// maneuver), word 1 = BaseOp::Priority, word 2 = the behavior id (-1 until a branch sets
// it). Common's own builders use the same three words (e.g. common_launchaction.cpp).
static IDirectiveInfo Server_MakeDirectiveInfo(unsigned int Kind, unsigned int OpField08,
                                               unsigned int BehaviorId) {
    IDirectiveInfo Dir;
    unsigned int const Words[3] = { Kind, OpField08, BehaviorId };
    std::memcpy(Dir.Data, Words, sizeof(Words));
    return Dir;
}

// pub::AI::SubmitState -- retail 0x7c8d0 (.text:06d5c8d0). GetBehaviorManager is not
// null-tested (retail would fault). SetZoneBehaviorParams builds a
// PathfindManager::UserZone for the manager's PathfindManager (+0x0c); SetFlagParams sets
// one of two bytes. Zone and flag ops, and unknown ops, return 4.
SERVER_API OP_RTYPE __cdecl SubmitState(unsigned int Flid, BaseOp* Op) {
    if ((Flid & 0x3fffffff) == 0) return static_cast<OP_RTYPE>(4);
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return static_cast<OP_RTYPE>(4);
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);

    if (Op->OpType == OpType_SetPersonalityParams) {
        SetPersonalityParams* Params = static_cast<SetPersonalityParams*>(Op);
        Behavior->set_state_graph(Params->StateGraphId, false);
        // -1 means "leave the callback as it is".
        if (Params->DirectiveCallbackObject != (DirectiveCallback*)-1) {
            Behavior->set_directive_callback(Params->DirectiveCallbackObject);
        }
        if (Params->ContentCallbackObject != (ContentCallback*)-1) {
            Behavior->set_content_callback(Params->ContentCallbackObject);
        }
        if (Params->bSetPersonality != 0) {
            Behavior->set_personality(Params->Value);
        }
        return static_cast<OP_RTYPE>(0);
    }

    if (Op->OpType == OpType_SetZoneBehaviorParams) {
        SetZoneBehaviorParams* Params = static_cast<SetZoneBehaviorParams*>(Op);
        PathfindManager* Pathfind = Behavior->Pathfind;                // IBehaviorManager+0x0c

        // The inline ctor sets BoxRotation = identity, BoxCenter = 0 (common_pathfind.h).
        PathfindManager::UserZone Zone;
        // Deviation: retail writes only the fields each zone kind uses and submits the rest
        // as stack residue (+0x08/+0x14/+0x54 for a box, +0x08 for kind 2, +0x00 for an
        // unknown kind, +0x04 for an unknown mode). The port zeroes those fields first.
        Zone.Mode = 0;
        Zone.Invert = 0;
        Zone.TargetPoint.x = 0.0f;
        Zone.TargetPoint.y = 0.0f;
        Zone.TargetPoint.z = 0.0f;
        Zone.Radius = 0.0f;
        Zone.BoxHalfExtent.x = 0.0f;
        Zone.BoxHalfExtent.y = 0.0f;
        Zone.BoxHalfExtent.z = 0.0f;
        Zone.TargetKey = 0;

        if (Params->InvertMode == 0) {
            Zone.Invert = 0;
        } else if (Params->InvertMode == 1) {
            Zone.Invert = 1;
        } else if (Params->InvertMode == 2) {
            Pathfind->clear_user_zone();
            return static_cast<OP_RTYPE>(0);
        }

        // BoxMin = Params+0x24..+0x2c, BoxMax = Params+0x30..+0x38.
        if (Params->Mode == 0) {
            Zone.Mode = 0;
            Zone.TargetPoint.x = Params->TargetPoint.x;
            Zone.TargetPoint.y = Params->TargetPoint.y;
            Zone.TargetPoint.z = Params->TargetPoint.z;
            Zone.Radius = Params->Radius;
        } else if (Params->Mode == 1) {
            // Center = (max + min) * 0.5f (DAT_06d6b9cc), each component stored to float
            // before use; extents = |min - center|. Retail re-copies an identity into the
            // rotation, which changes nothing.
            Zone.Mode = 1;
            float const CenterX = (Params->BoxMax.x + Params->BoxMin.x) * 0.5f;
            float const CenterY = (Params->BoxMax.y + Params->BoxMin.y) * 0.5f;
            float const CenterZ = (Params->BoxMax.z + Params->BoxMin.z) * 0.5f;
            Zone.BoxCenter.x = CenterX;
            Zone.BoxCenter.y = CenterY;
            Zone.BoxCenter.z = CenterZ;
            Zone.BoxHalfExtent.x = std::fabs(Params->BoxMin.x - CenterX);
            Zone.BoxHalfExtent.y = std::fabs(Params->BoxMin.y - CenterY);
            Zone.BoxHalfExtent.z = std::fabs(Params->BoxMin.z - CenterZ);
        } else if (Params->Mode == 2) {
            Zone.Mode = 2;
            Zone.Radius = Params->Radius;
            Zone.TargetKey = Params->TargetId;
        }
        // Every kind, known or not, is submitted.
        Pathfind->submit_user_zone(Zone);
        return static_cast<OP_RTYPE>(4);
    }

    if (Op->OpType == OpType_SetFlagParams) {
        SetFlagParams* Params = static_cast<SetFlagParams*>(Op);
        if (Params->FlagId == 0) {
            // Byte PathfindManager+0x245 (common_pathfind.h bObstacleAvoidanceEnabled).
            Behavior->Pathfind->bObstacleAvoidanceEnabled = Params->bValue;
        } else if (Params->FlagId == 1) {
            // Byte SystemManager+0x11c0 == GunGroups (+0x12c) + 0x1094, the "fire control
            // active" byte common_aifirecontrol.cpp tests.
            Behavior->Sys->GunGroups.bEnabled = Params->bValue;
        }
        return static_cast<OP_RTYPE>(4);
    }

    return static_cast<OP_RTYPE>(4);
}

// OpType -> behavior id pairs of SubmitDirective's AI branch, in retail's test order
// (.text:06d5d48b: thirteen independent compares, each setting IDirectiveInfo word 0 = 1
// and word 2 = the id before set_current_directive).
struct FSubmitDirectiveBehavior {
    int OpType;
    unsigned int BehaviorId;
};
static const FSubmitDirectiveBehavior GSubmitDirectiveBehaviors[] = {
    { OpType_DirectiveGotoOp,              0x02 },
    { OpType_DirectiveFollowOp,            0x11 },
    { OpType_DirectiveTrailOp,             0x03 },
    { OpType_DirectiveDockOp,              0x07 },
    { OpType_DirectiveDelayOp,             0x14 },
    { OpType_DirectiveIdleOp,              0x06 },
    { OpType_DirectiveLaunchOp,            0x08 },
    { OpType_DirectiveInstantTradelaneOp,  0x09 },
    { OpType_DirectiveGuideOp,             0x0e },
    { OpType_DirectiveFaceOp,              0x0f },
    { OpType_DirectiveEvadeOp,             0x05 },
    { OpType_DirectiveDrasticEvadeOp,      0x12 },
    { OpType_DirectiveTractorBeamOp,       0x10 },
};

// pub::AI::SubmitDirective -- retail 0x7cbb0 (.text:06d5cbb0). Jump tables at .text:06d5d768
// (7 dwords) and .text:06d5d784 (19 bytes, indexed by OpType - 3).
// The object must be a CSimple with an ObjectDb (+0xac). When its owner player (+0xb4)
// passes the inlined PlayerDB::is_valid_ship_owner (a live slot whose ship +0x3e4 is this
// object), the directive goes to that player's client (player branch); otherwise to the AI.
SERVER_API OP_RTYPE __cdecl SubmitDirective(unsigned int Flid, BaseOp* Op) {
    if ((Flid & 0x3fffffff) == 0) return static_cast<OP_RTYPE>(4);
    if (!Op->validate()) return static_cast<OP_RTYPE>(5);

    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return static_cast<OP_RTYPE>(5);

    CObject const* Inspected = Obj->cobject();
    if (Inspected == nullptr) return static_cast<OP_RTYPE>(5);
    if ((static_cast<unsigned int>(Inspected->ObjectClass) & 3u) != 3u) return static_cast<OP_RTYPE>(5);
    if (static_cast<CSimple const*>(Inspected)->ObjectDb == nullptr) return static_cast<OP_RTYPE>(5);

    // No null test on csimple() (retail would fault reading +0xb4 of null).
    unsigned int OwnerPlayer = Obj->csimple()->OwnerPlayer;

    if (::Players.is_valid_ship_owner(OwnerPlayer, Flid)) {
        // Player branch. Each of the six supported kinds default-constructs the struct
        // (+0x18 = -1, +0x40 = 3, the rest 0), fills its fields, stores the kind at +0x00
        // and sends IClient slot 100 (+0x190) with (&OwnerPlayer, &Info); every send returns
        // 1. Any other op returns 5 with no send. Pad_0x09 / Pad_0x91 are stack residue in
        // retail; zeroed here (rpclocal's marshal zeroes them again).
        FPayloadServerSetDirective Info = {};
        Info.DockPortIndex = -1;
        Info.GotoMode = 3;

        if (Op->OpType == OpType_DirectiveCancelOp) {
            Info.DirectiveKind = 0;
        } else if (Op->OpType == OpType_DirectiveDelayOp) {
            DirectiveDelayOp* Delay = static_cast<DirectiveDelayOp*>(Op);
            Info.DelaySeconds = Delay->DelaySeconds;
            Info.DirectiveKind = 1;
        } else if (Op->OpType == OpType_DirectiveDockOp) {
            DirectiveDockOp* Dock = static_cast<DirectiveDockOp*>(Op);
            Info.DockObjectId = Dock->DockObjectId;
            Info.DockSecondaryObjectId = Dock->NextRingId;
            Info.bDockApproachCleared = Dock->bHasDockPortIndex;
            Info.DockPortIndex = Dock->DockPortIndex;
            Info.ProxTargetId = static_cast<unsigned int>(Dock->TetherObjectId);
            Info.ProxNearDist = Dock->TetherNearRange;
            Info.ProxFarDist = Dock->TetherFarRange;
            Info.DirectiveKind = 2;
        } else if (Op->OpType == OpType_DirectiveFollowOp) {
            DirectiveFollowOp* Follow = static_cast<DirectiveFollowOp*>(Op);
            Info.FollowTargetId = Follow->TargetId;
            Info.FollowDistance = Follow->FollowDistance;
            Info.FollowOffset.x = Follow->FollowOffsetX;
            Info.FollowOffset.y = Follow->FollowOffsetY;
            Info.FollowOffset.z = Follow->FollowOffsetZ;
            Info.FollowEngageRange = Follow->EngageRange;
            Info.DirectiveKind = 3;
        } else if (Op->OpType == OpType_DirectiveIdleOp) {
            Info.DirectiveKind = 5;
        } else if (Op->OpType == OpType_DirectiveGotoOp) {
            // The only kind that fills +0x84..+0x90 (the others zero it before the send).
            DirectiveGotoOp* Goto = static_cast<DirectiveGotoOp*>(Op);
            Info.GotoMode = Goto->GotoKind;
            Info.GotoDestination = Goto->Destination;
            Info.GotoTargetId = Goto->TargetId;
            Info.SplineP1.Field_0x00 = Goto->SplineP1X;
            Info.SplineP1.Field_0x04 = Goto->SplineP1Y;
            Info.SplineP1.Field_0x08 = Goto->SplineP1Z;
            Info.SplineP2.Field_0x00 = Goto->SplineP2X;
            Info.SplineP2.Field_0x04 = Goto->SplineP2Y;
            Info.SplineP2.Field_0x08 = Goto->SplineP2Z;
            Info.SplineP0.Field_0x00 = Goto->SplineP0X;
            Info.SplineP0.Field_0x04 = Goto->SplineP0Y;
            Info.SplineP0.Field_0x08 = Goto->SplineP0Z;
            Info.SplineP3.Field_0x00 = Goto->SplineP3X;
            Info.SplineP3.Field_0x04 = Goto->SplineP3Y;
            Info.SplineP3.Field_0x08 = Goto->SplineP3Z;
            Info.GotoRange = Goto->ArrivalRange;
            Info.GotoSpeed = Goto->MaxSpeed;
            Info.bGotoFlyThrough = Goto->bFlyThrough;
            Info.Field_0x8d = Goto->Field_59;
            Info.bGotoCruise = Goto->bGotoCruise;
            Info.bGotoNoCruise = Goto->bGotoNoCruise;
            Info.ProxTargetId = Goto->TetherObjectId;
            Info.ProxNearDist = Goto->TetherNearRange;
            Info.ProxFarDist = Goto->TetherFarRange;
            Info.bGotoDisableAvoidance = Goto->bDisableAvoidance;
            Info.DirectiveKind = 4;
        } else {
            // Trail, Strafe, Face, Ram, TractorBeam, Evade, DrasticEvade, Launch,
            // InstantTradelane, Formation, Guide, WaitForShip, WaitForPlayerManeuver and the
            // Set*Params ops.
            return static_cast<OP_RTYPE>(5);
        }

        g_pClientInterface->SendServerSetDirective(OwnerPlayer, Info);
        return static_cast<OP_RTYPE>(1);
    }

    // AI branch. GetBehaviorManager is not null-tested (retail would fault).
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);

    if (Op->OpType == OpType_DirectiveWaitForPlayerManeuverOp) {
        // Kind 2 with the named behaviour; an unknown name returns 5.
        DirectiveWaitForPlayerManeuverOp* Wait = static_cast<DirectiveWaitForPlayerManeuverOp*>(Op);
        int const BehaviorId = BehaviorTypes::get_behavior_id(Wait->BehaviorName);
        if (BehaviorId == -1) return static_cast<OP_RTYPE>(5);
        Behavior->set_current_directive(
            Server_MakeDirectiveInfo(2u, Op->Priority, static_cast<unsigned int>(BehaviorId)), Op);
        return static_cast<OP_RTYPE>(1);
    }

    if (Op->OpType == OpType_DirectiveCancelOp) {
        // The callee's result is ignored.
        Behavior->set_current_directive(Server_MakeDirectiveInfo(0u, Op->Priority, 0xffffffffu), Op);
        return static_cast<OP_RTYPE>(1);
    }

    // A current directive whose word 0 is non-zero blocks every other op.
    unsigned int CurrentKind = 0;
    std::memcpy(&CurrentKind, Behavior->get_current_directive()->Data, sizeof(CurrentKind));
    if (CurrentKind != 0) return static_cast<OP_RTYPE>(2);

    unsigned int Result = 5;
    size_t const PairCount = sizeof(GSubmitDirectiveBehaviors) / sizeof(GSubmitDirectiveBehaviors[0]);
    for (size_t Index = 0; Index < PairCount; ++Index) {
        if (Op->OpType != GSubmitDirectiveBehaviors[Index].OpType) continue;
        if (Behavior->set_current_directive(
                Server_MakeDirectiveInfo(1u, Op->Priority, GSubmitDirectiveBehaviors[Index].BehaviorId), Op)) {
            Result = 1;
        }
    }
    return static_cast<OP_RTYPE>(Result);
}

// pub::AI::set_directive_priority -- retail 0x7db70. No Flid mask test.
SERVER_API OP_RTYPE __cdecl set_directive_priority(unsigned int Flid, DirectivePriority Priority) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return static_cast<OP_RTYPE>(5);
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return static_cast<OP_RTYPE>(5);
    return Behavior->set_directive_priority(Priority);
}

// pub::AI::get_behavior_id -- retail 0x7dbb0. IBehaviorManager+0xc0.
SERVER_API int __cdecl get_behavior_id(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return -1;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return -1;
    return Behavior->CurrentBehaviorIndex;
}

// pub::AI::get_state_graph_id -- retail 0x7dbe0. State graph at IBehaviorManager+0x04.
SERVER_API int __cdecl get_state_graph_id(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return -1;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return -1;
    IStateGraph const* Graph = Behavior->StateGraph;
    if (Graph == nullptr) return -1;
    return Graph->Id;
}

// pub::AI::get_personality -- retail 0x7dc20, ?get_personality@AI@pub@@YA_NIAAVPersonality@12@@Z.
// No Flid mask test.
SERVER_API bool __cdecl get_personality(unsigned int Flid, Personality& OutPersonality) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    Behavior->get_personality(OutPersonality);
    return true;
}

// pub::AI::enable_maneuver -- retail 0x7dc60. The callee's bool is dropped: true whenever
// the behavior exists.
SERVER_API bool __cdecl enable_maneuver(unsigned int Flid, int ManeuverId, bool bEnable) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    Behavior->enable_maneuver(ManeuverId, bEnable);
    return true;
}

// pub::AI::enable_all_maneuvers -- retail 0x7dca0. A tail call, so the callee's result is
// returned.
SERVER_API bool __cdecl enable_all_maneuvers(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    return Behavior->enable_all_maneuvers();
}

// pub::AI::lock_maneuvers -- retail 0x7dcd0. As enable_maneuver: the callee's bool is
// dropped.
SERVER_API bool __cdecl lock_maneuvers(unsigned int Flid, bool bLock) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    Behavior->lock_maneuvers(bLock);
    return true;
}

// pub::AI::get_scan_response -- retail 0x7dd10. The object looked up is the second
// argument. A player-owned target gets IClient::SendServerScanNotify (+0x1b4) and 3;
// otherwise the AI answers. No null test on csimple().
SERVER_API ScanResponse __cdecl get_scan_response(unsigned int ScannerFlid, unsigned int TargetFlid, unsigned int GoodId) {
    GameObject* Obj = Server_LookupLiveObject(TargetFlid);
    if (Obj == nullptr) return static_cast<ScanResponse>(0);
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
    if (::Players.is_valid_ship_owner(OwnerPlayer, TargetFlid)) {
        g_pClientInterface->SendServerScanNotify(OwnerPlayer, ScannerFlid, GoodId);
        return static_cast<ScanResponse>(3);
    }
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return static_cast<ScanResponse>(0);
    return Behavior->get_scan_response(ScannerFlid);
}

// pub::AI::submit_forced_target -- retail 0x7ddc0. No message and no Flid mask. The
// TargetManager is SystemManager+0x142C (SystemManager::Targeting, common_systemmanager.h);
// submit_forced_target (Common 0x062eec10) appends to an 8-entry id array.
SERVER_API int __cdecl submit_forced_target(unsigned int ship_id, unsigned int target_id) {
    GameObject* Obj = Server_LookupLiveObject(ship_id);
    if (Obj == nullptr) return 0;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return 0;
    if (Behavior->Sys == nullptr) return 0;
    return Behavior->Sys->Targeting.submit_forced_target(target_id);
}

// pub::AI::remove_forced_target -- retail 0x7de00. As submit_forced_target, over
// TargetManager::remove_forced_target (Common 0x062eec50).
SERVER_API int __cdecl remove_forced_target(unsigned int ship_id, unsigned int target_id) {
    GameObject* Obj = Server_LookupLiveObject(ship_id);
    if (Obj == nullptr) return 0;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return 0;
    if (Behavior->Sys == nullptr) return 0;
    return Behavior->Sys->Targeting.remove_forced_target(target_id);
}

// pub::AI::set_player_enemy_clamp -- retail 0x7de40. As submit_forced_target, over
// TargetManager::set_player_enemy_clamp (Common 0x062ee5e0, stores DAT_063ec754/758). No
// message.
SERVER_API bool __cdecl set_player_enemy_clamp(unsigned int player_id, int min_rep, int max_rep) {
    GameObject* Obj = Server_LookupLiveObject(player_id);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    if (Behavior->Sys == nullptr) return false;
    Behavior->Sys->Targeting.set_player_enemy_clamp(min_rep, max_rep);
    return true;
}

// pub::AI::update_formation_state -- retail 0x7d7a0,
// ?update_formation_state@AI@pub@@YA?AW4FORMATION_RTYPE@@IIABVVector@@@Z (the global enum).
// Arguments: the ship changing formation, its new leader (may be 0), its offset. Returns -3
// when the ship does not resolve, else 0.
// Outside SP an unchanged leader returns early. Outside SP, before the change the old
// leader's formation is broadcast (old leader id only when it has followers, every
// follower id except the ship's; the ship still receives it), and after the change the
// new leader's (leader id always, no exclusion). Receivers are the followers and the
// leader whose cobject() is a player CShip (byte +0x2bd), via IClient slot 120 (+0x1e0).
// The old leader drops the ship unless it is the ship (compared by get_id); the new leader
// adds it unless it is the ship (compared by pointer).
// Retail reads Followers[i] for i < count with no bound against the 16-entry array; so
// does the port.

// The receiver step retail inlines four times.
static void Formation_SendIdsToOwner(IObjRW const* Obj, std::vector<unsigned int> const& Ids) {
    CObject const* const Cobj = Obj->cobject();                    // +0x150
    if (Cobj == nullptr)
        return;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return;
    CShip const* const ShipCobj = static_cast<CShip const*>(Cobj);
    if (ShipCobj->bIsPlayer == 0)                                  // +0x2bd
        return;
    ::g_pClientInterface->SendServerMiscObjUpdateFormation(ShipCobj->OwnerPlayer, Ids);   // +0xb4
}

SERVER_API FORMATION_RTYPE __cdecl update_formation_state(unsigned int ShipFlid, unsigned int LeaderFlid, const Vector& Offset) {
    GameObject* Ship = Server_LookupLiveObject(ShipFlid);
    if (Ship == nullptr) return FORMATION_RTYPE_NOT_FOUND;

    IObjRW* OldLeader = nullptr;
    Ship->get_formation_leader(OldLeader);                           // IObjInspect +0xb0
    IObjRW* const NewLeader = Server_LookupLiveObject(LeaderFlid);
    if (!SinglePlayer() && NewLeader == OldLeader)
        return FORMATION_RTYPE_OK;

    if (!SinglePlayer() && OldLeader != nullptr) {
        std::vector<unsigned int> Ids;
        int const Count = OldLeader->get_formation_follower_count(); // IObjAffect +0x20
        IObjRW* Followers[0x10];
        OldLeader->get_formation_followers(Followers, 0x10);        // +0x14c
        if (Count != 0)
            Ids.push_back(OldLeader->get_id());
        for (int I = 0; I < Count; ++I) {
            if (Followers[I]->get_id() != Ship->get_id())
                Ids.push_back(Followers[I]->get_id());
        }
        for (int I = 0; I < Count; ++I)
            Formation_SendIdsToOwner(Followers[I], Ids);
        Formation_SendIdsToOwner(OldLeader, Ids);
    }

    if (OldLeader != nullptr && OldLeader->get_id() != Ship->get_id())
        (void)OldLeader->remove_formation_follower(Ship);           // IObjAffect +0x1c
    (void)Ship->set_formation_leader(NewLeader);                    // +0x10
    (void)Ship->set_formation_offset(Offset);                       // +0x14
    if (NewLeader != nullptr && NewLeader != Ship)
        (void)NewLeader->add_formation_follower(Ship);              // +0x18

    if (!SinglePlayer() && NewLeader != nullptr) {
        std::vector<unsigned int> Ids;
        int const Count = NewLeader->get_formation_follower_count();
        IObjRW* Followers[0x10];
        NewLeader->get_formation_followers(Followers, 0x10);
        Ids.push_back(NewLeader->get_id());
        for (int I = 0; I < Count; ++I)
            Ids.push_back(Followers[I]->get_id());
        for (int I = 0; I < Count; ++I)
            Formation_SendIdsToOwner(Followers[I], Ids);
        Formation_SendIdsToOwner(NewLeader, Ids);
    }
    return FORMATION_RTYPE_OK;
}

// pub::AI::refresh_state_graph -- retail 0x7db40, a tail call into
// IBehaviorManager::refresh_state_graph.
SERVER_API void __cdecl refresh_state_graph(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return;
    Behavior->refresh_state_graph();
}

} // namespace AI

// pub::Phantom exports -- the record, the global list and the event pump are above
// namespace pub::SpaceObj (Server_PumpPhantomEvents).
namespace Phantom {

// pub::Phantom::Create (box) -- retail .text:06cfbe20. Size components must be > 0 (NaN
// rejected). CreatePhantom's result is not tested. Deviation: PhantomParms bytes retail
// leaves unwritten (+0x07 .. +0x0f) are zeroed here.
SERVER_API int __cdecl Create(unsigned int SystemId, const Vector& Size, const Vector& Pos,
                              const Matrix& Orient, unsigned int TypeMask, void*& OutHandle) {
    if (GbPhantomPumpBusy) return -5;
    if (!(Size.x > 0.0f)) return -4;
    if (!(Size.y > 0.0f)) return -4;
    if (!(Size.z > 0.0f)) return -4;
    if ((SystemId & 0x3fffffffu) == 0) return -4;

    ServerPhantom* const Ph = new ServerPhantom();                       // operator new(0x28)
    PhySys::PhantomParms Parms;
    memset(&Parms, 0, sizeof(Parms));
    Parms.SystemId = SystemId;
    Parms.bPhysicalUnmoveable = 1;
    Parms.Flag05 = 0;
    Parms.Name[0] = 0;
    Parms.Position = Pos;
    Parms.Orientation = Orient;
    Parms.Listener = Ph;
    Ph->TypeMask = TypeMask;
    Ph->SystemId = SystemId;
    PhySys::CreatePhantom(Parms, Size, Ph->PhyHandle);
    OutHandle = Ph;
    GPhantoms.push_back(Ph);
    return 0;
}

// pub::Phantom::Create (sphere) -- retail .text:06cfc020. As the box version with an
// identity orientation; radius must be > 0 (NaN rejected).
SERVER_API int __cdecl Create(unsigned int SystemId, float Radius, const Vector& Center,
                              unsigned int TypeMask, void*& OutHandle) {
    if (GbPhantomPumpBusy) return -5;
    if (!(Radius > 0.0f)) return -4;
    if ((SystemId & 0x3fffffffu) == 0) return -4;

    ServerPhantom* const Ph = new ServerPhantom();
    PhySys::PhantomParms Parms;
    memset(&Parms, 0, sizeof(Parms));
    Parms.SystemId = SystemId;
    Parms.bPhysicalUnmoveable = 1;
    Parms.Flag05 = 0;
    Parms.Name[0] = 0;
    Parms.Position = Center;
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.Listener = Ph;
    Ph->TypeMask = TypeMask;
    Ph->SystemId = SystemId;
    PhySys::CreatePhantom(Parms, Radius, Ph->PhyHandle);
    OutHandle = Ph;
    GPhantoms.push_back(Ph);
    return 0;
}

// pub::Phantom::Destroy -- retail .text:06cfbcd0. A handle not on the global list -> -2.
SERVER_API int __cdecl Destroy(void* Handle) {
    if (GbPhantomPumpBusy) return -5;
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -2;
    PhySys::DestroyPhantom((*It)->PhyHandle);
    delete *It;                                                          // FUN_06cfbd60
    GPhantoms.erase(It);
    return 0;
}

// pub::Phantom::SetActive -- retail .text:06cfbfd0.
SERVER_API int __cdecl SetActive(void* Handle, bool bActive) {
    if (GbPhantomPumpBusy) return -5;
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -2;
    PhySys::SetCollisionStatePhantom((*It)->PhyHandle, bActive);
    return 0;
}

// pub::Phantom::Attach -- retail .text:06cfc1e0. A missing object is -2, a missing handle
// -3. The CObject is taken under the (& 1) test, not null-tested after.
SERVER_API int __cdecl Attach(const unsigned int& ObjId, void* Handle) {
    if (GbPhantomPumpBusy) return -5;
    GameObject* const Obj = Server_LookupLiveObject(ObjId);
    if (Obj == nullptr) return -2;
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -3;
    CObject* Raw = Obj->Object;
    if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 1u) != 1u)
        Raw = nullptr;
    if (PhySys::AttachPhantom(Raw, (*It)->PhyHandle) != 0) return -1;
    return 0;
}

// pub::Phantom::Detach -- retail .text:06cfc270.
SERVER_API int __cdecl Detach(void* Handle) {
    if (GbPhantomPumpBusy) return -5;
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -2;
    if (PhySys::DetachPhantom((*It)->PhyHandle) != 0) return -1;
    return 0;
}

} // namespace Phantom

// ===========================================================================
// pub::Audio exports
// ===========================================================================
namespace Audio {

// pub::Audio::SetMusic -- retail 0x06d5eec0. No validation; IClient slot 122 (+0x1e8) with
// the Tryptich unpacked field by field (the byte at +0x10 zero-extended).
SERVER_API int __cdecl SetMusic(unsigned int PlayerId, const Tryptich& Track) {
    // Field14 holds the fade's float bits (retail pushes the dword; the sender FLDs it).
    float Fade = 0.0f;
    memcpy(&Fade, &Track.FadeBits, sizeof(Fade));
    g_pClientInterface->SendServerSetMusic(PlayerId, Track.SpaceMusic, Track.DangerMusic, Track.BattleMusic,
                                           Track.OverrideMusic, Track.Field10 != 0, Fade);
    return 0;
}

// pub::Audio::CancelMusic -- retail 0x06d5ef00. IClient slot 123 (+0x1ec).
SERVER_API int __cdecl CancelMusic(unsigned int PlayerId) {
    g_pClientInterface->SendServerCancelMusic(PlayerId);
    return 0;
}

// pub::Audio::PlaySoundEffect -- retail 0x06d5ef20. IClient slot 124 (+0x1f0).
SERVER_API int __cdecl PlaySoundEffect(unsigned int PlayerId, unsigned int SoundId) {
    g_pClientInterface->SendServerPlaySoundEffect(PlayerId, SoundId);
    return 0;
}

} // namespace Audio

// ===========================================================================
// pub::Market exports (retail 0x385c0 - 0x387d0)
// ===========================================================================
namespace Market {

// Every export goes through BaseDB::find_base (FUN_06d140f0; a miss FDUMPs "Base %u is
// invalid", line 0x659) and a FUN_06d12xxx helper over the market map at BaseData+0x10.
// Every failure returns -4, never -1. Only IsGoodInDemand, GetNumCommoditiesForSale and
// GetNumCommoditiesInDemand clear their out-parameter before the bad-id check.

// pub::Market::GetMinInventory -- retail 0x385c0 (helper FUN_06d12c70). The mapped value's
// +0x08 (MinInventory) on a hit, 0 on a miss.
SERVER_API int __cdecl GetMinInventory(unsigned int BaseId, unsigned int GoodId, int& OutInv) {
    if ((BaseId & 0x3fffffff) == 0 || (GoodId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.find(GoodId);
    OutInv = (it != base->Market.end()) ? it->second.MinInventory : 0;
    return 0;
}

// pub::Market::GetPrice -- retail 0x38610 (helper FUN_06d12cd0). Returns the stored price
// (mapped +0x04); Common computes and ftol-truncates it once at load time (MarketGoodInfo,
// common_goods.h), nothing is recomputed here.
SERVER_API int __cdecl GetPrice(unsigned int BaseId, unsigned int GoodId, float& OutPrice) {
    if ((BaseId & 0x3fffffff) == 0 || (GoodId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.find(GoodId);
    if (it != base->Market.end()) {
        OutPrice = it->second.Price;
        return 0;
    }
    // A miss falls back to the nominal GoodInfo+0x58, with no null test.
    OutPrice = GoodList::find_by_id(GoodId)->Price;
    return 0;
}

// pub::Market::GetNominalPrice -- retail 0x38660. GoodInfo+0x58.
SERVER_API int __cdecl GetNominalPrice(unsigned int GoodId, float& OutPrice) {
    if ((GoodId & 0x3fffffff) == 0) return -4;
    GoodInfo const* good = GoodList::find_by_id(GoodId);
    if (!good) return -4;
    OutPrice = good->Price;
    return 0;
}

// pub::Market::GetGoodJumpDist -- retail 0x38690. GoodInfo+0x6c. Unlike its neighbours it
// does not screen the good id first.
SERVER_API int __cdecl GetGoodJumpDist(unsigned int GoodId, unsigned int& OutDist) {
    GoodInfo const* good = GoodList::find_by_id(GoodId);
    if (!good) return -4;
    OutDist = (unsigned int)good->JumpDist;
    return 0;
}

// pub::Market::IsGoodInDemand -- retail 0x386c0 (helper FUN_06d12d20). TransactionType is
// mapped +0x10.
SERVER_API int __cdecl IsGoodInDemand(unsigned int BaseId, unsigned int GoodId, bool& OutDemand) {
    OutDemand = false;
    if ((BaseId & 0x3fffffff) == 0 || (GoodId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.find(GoodId);
    if (it != base->Market.end()) {
        OutDemand = (it->second.Type == TransactionBuyAndSell ||
                     it->second.Type == TransactionDemandOnly);
    }
    return 0;
}

// The two commodity filters. Retail feeds GoodList::find_by_id the id out of the mapped
// value (+0x00), not the map key, and requires the commodity category (GoodInfo+0x4c == 0).
static bool MarketGoodIsCommodity(MarketGoodInfo const& Entry) {
    GoodInfo const* info = GoodList::find_by_id(Entry.GoodId);
    return info != nullptr && info->Category == (unsigned int)GoodCategoryCommodity;
}

static bool MarketGoodForSale(MarketGoodInfo const& Entry) {
    return MarketGoodIsCommodity(Entry) &&
           (Entry.Type == TransactionBuyAndSell || Entry.Type == TransactionSaleOnly);
}

static bool MarketGoodInDemand(MarketGoodInfo const& Entry) {
    return MarketGoodIsCommodity(Entry) &&
           (Entry.Type == TransactionBuyAndSell || Entry.Type == TransactionDemandOnly);
}

// pub::Market::GetNumCommoditiesForSale -- retail 0x38710 (helper FUN_06d12d90).
SERVER_API int __cdecl GetNumCommoditiesForSale(unsigned int BaseId, int* OutCount) {
    *OutCount = 0;   // before the id test, with no pointer test
    if ((BaseId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (MarketGoodForSale(it->second))
            count++;
    }
    *OutCount = count;
    return 0;
}

// pub::Market::GetCommoditiesForSale -- retail 0x38750 (helper FUN_06d12e80). The cap is
// read from *InOutCount once; an over-cap good is skipped but the walk continues. No
// pointer tests.
SERVER_API int __cdecl GetCommoditiesForSale(unsigned int BaseId, unsigned int* const OutGoods, int* InOutCount) {
    if ((BaseId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int const max_count = *InOutCount;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (!MarketGoodForSale(it->second))
            continue;
        if (count >= max_count)
            continue;
        OutGoods[count++] = it->second.GoodId;
    }
    *InOutCount = count;
    return 0;
}

// pub::Market::GetNumCommoditiesInDemand -- retail 0x38790 (helper FUN_06d12df0).
SERVER_API int __cdecl GetNumCommoditiesInDemand(unsigned int BaseId, int* OutCount) {
    *OutCount = 0;   // before the id test, with no pointer test
    if ((BaseId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (MarketGoodInDemand(it->second))
            count++;
    }
    *OutCount = count;
    return 0;
}

// pub::Market::GetCommoditiesInDemand -- retail 0x387d0 (helper FUN_06d12f40). As
// GetCommoditiesForSale.
SERVER_API int __cdecl GetCommoditiesInDemand(unsigned int BaseId, unsigned int* const OutGoods, int* InOutCount) {
    if ((BaseId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int const max_count = *InOutCount;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (!MarketGoodInDemand(it->second))
            continue;
        if (count >= max_count)
            continue;
        OutGoods[count++] = it->second.GoodId;
    }
    *InOutCount = count;
    return 0;
}

} // namespace Market

// ===========================================================================
// pub:: utility batch 2
// ===========================================================================

// pub::FindHardpoint -- retail RVA 0x7E270, ?FindHardpoint@pub@@YAHPBDIAAVVector@@AAVMatrix@@@Z.
// 0 found, -2 load failed, -3 hardpoint not found. GetShip is not null-tested. The engine
// index lives in the ship-id argument's own stack slot, so it starts as the archetype id,
// not -1.
SERVER_API int __cdecl FindHardpoint(char const* Name, unsigned int ShipArchId,
                                     Vector& OutPos, Matrix& OutRot)
{
    Archetype::Ship* ShipArch = Archetype::GetShip(ShipArchId);
    char const* DaPath = ShipArch->DaArchetype.Value;
    long EngineIndex = static_cast<long>(ShipArchId);
    bool bLoaded = Archetype::Load(DaPath, &EngineIndex, nullptr);
    if (!bLoaded)
        return -2;

    bool bFound = HardpointPositionA(EngineIndex, Name, &OutPos, &OutRot);
    // IEngine::ReleaseHandle (+0x3C) straight on DALib::Engine, no null check.
    DALib::Engine->ReleaseHandle(reinterpret_cast<void*>(EngineIndex));
    return bFound ? 0 : -3;
}

// pub::GetShipID -- retail RVA 0x7E2E0, ?GetShipID@pub@@YAHAAIPBD@Z.
SERVER_API int __cdecl GetShipID(unsigned int& OutShipId, char const* ShipName)
{
    if (ShipName != nullptr && ShipName[0] != '\0')
    {
        unsigned int NicknameId = CreateID(ShipName);
        ID_String Name;
        Name.Id = NicknameId;
        unsigned int ShipId = Archetype::GetShipID(Name);
        OutShipId = ShipId;
        if ((ShipId & 0x3fffffff) != 0)
            return 0;
        return -2;
    }
    return -4;
}

// pub::TranslateGoodToMsgIdPrefix -- retail RVA 0x7E050,
// ?TranslateGoodToMsgIdPrefix@pub@@YAHIAAU?$TString@$0EA@@@@Z. GetMsgIdPrefix is
// CommReferrable slot 1; an empty prefix still returns 0.
SERVER_API int __cdecl TranslateGoodToMsgIdPrefix(unsigned int GoodId,
                                                   TString<64>& OutPrefix)
{
    GoodInfo const* Info = GoodList::find_by_id(GoodId);
    if (Info == nullptr)
        return -4;

    OutPrefix = Info->GetMsgIdPrefix().Data;
    return 0;
}

// pub::TranslateShipToMsgIdPrefix -- retail RVA 0x7E0D0. As the Good version, on the
// ship's CommReferrable subobject at +0x94.
SERVER_API int __cdecl TranslateShipToMsgIdPrefix(unsigned int ShipArchId,
                                                   TString<64>& OutPrefix)
{
    Archetype::Ship* ShipArch = Archetype::GetShip(ShipArchId);
    if (ShipArch == nullptr)
        return -4;

    OutPrefix = ShipArch->GetMsgIdPrefix().Data;
    return 0;
}

} // namespace pub

