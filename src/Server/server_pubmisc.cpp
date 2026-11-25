// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_id.h"
#include "../include/common_goods.h"
#include "../include/common_universe.h"
#include "../include/common_archetype.h"
#include "../include/common_reputation.h"
#include "../include/common_cobjdb.h"
#include "../include/common_csys.h"
#include "../include/common_cobject.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_exports.h"
#include "../include/server_basedb.h"
#include "../include/common_physys.h"
#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"
#include "../include/common_misc.h"
#include "../include/DALib.h"            // DALib::Engine (pub::FindHardpoint releases the archetype handle)
#include "../include/readfile.h"         // IFileSystem::GetFileSysPath (+0x84, pub::BuildBaseReader)
#include "../include/rpc_iclient.h"
#include "../include/common_casteroid.h"
#include "server_internal.h"
#include <windows.h>
#include <new>
#include <cstring>     // std::memcpy (SpaceObj::Create EquipIdMaker seed)
#include <cstdlib>     // rand, wcstombs
#include "../include/server_iserverimpl.h"   // IServerImpl, Server
#include "../include/common_ini.h"
#include "../include/common_server_controller.h"

// The IClient the server talks back to (DAT_06d8fb14). Defined in IServerImpl.cpp and set
// by IServerImpl::Startup; several pub:: exports dispatch straight into its vtable.
extern IClient* g_pClientInterface;

// Game time accumulator defined in IServerImpl.cpp (updated by ElapseTime)
extern double g_ElapsedTimeAccum;   // DAT_06d8fb28, IServerImpl.cpp
extern double GServerTimeBase;      // DAT_06d8fb20, IServerImpl.cpp

static unsigned int g_TimerCount = 0;            // DAT_06d9046c

// Timer node of the FUN_06d5df60 timer list.
struct FTimerNode {
    FTimerNode* Prev;             // +0x00
    FTimerNode* Next;             // +0x04
    unsigned int TargetController; // +0x08  target controller FLID
    // +0x0c/+0x10 are pub::Controller::TimerExpired's two dwords, copied by pub::SetTimer
    // and handed back to the controller as the message payload when the timer fires.
    unsigned int Param1;          // +0x0C
    unsigned int Param2;          // +0x10
    float RemainingTime;          // +0x14  countdown
};

// Timer list for FUN_06d5df60
static FTimerNode g_TimerListSentinel = { &g_TimerListSentinel, &g_TimerListSentinel, 0, 0, 0, 0.0f };

// Retail 0x06d5df00 -- the timer list's destructor, registered through _onexit by the
// list's static initializer 0x06d5dec0. Retail's head is a 0x18-byte heap node that is
// freed and nulled; the port's head is the static sentinel, relinked to itself instead.
struct FTimerListTeardown {
    ~FTimerListTeardown() {
        FTimerNode* Cur = g_TimerListSentinel.Next;
        while (Cur != &g_TimerListSentinel) {
            FTimerNode* Node = Cur;
            Cur = Cur->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            free(Node);
            g_TimerCount--;
        }
        g_TimerListSentinel.Prev = &g_TimerListSentinel;
        g_TimerListSentinel.Next = &g_TimerListSentinel;
        g_TimerCount = 0;
    }
};

static FTimerListTeardown g_TimerListTeardown;

static void TimerListRemove(FTimerNode* node) {
    node->Prev->Next = node->Next;
    node->Next->Prev = node->Prev;
    node->Prev = node;
    node->Next = node;
}

// FUN_06d5df60 -- timer firing.
void Server_TimerFiring(float dt) {
    FTimerNode* expired[128];
    unsigned int expired_count = 0;

    // Every timer loses dt; while fewer than 0x80 are collected, one whose remaining time
    // is below 0.0 is collected.
    FTimerNode* cur = g_TimerListSentinel.Next;
    while (cur != &g_TimerListSentinel) {
        cur->RemainingTime -= dt;
        if (expired_count < 128 && cur->RemainingTime < 0.0f) {
            expired[expired_count] = cur;
            expired_count++;
            // Filling the 128th entry warns once (retail's text says 64).
            if (expired_count == 128) {
                TString<256> Text;
                Text.Length = 0;
                Text.Data[0] = '\0';
                Server_FormatTString256(&Text,
                    "%x timers are trying to fire on the same frame.  Only 64 will fire now.  "
                    "The next ones will fire on the next frame.",
                    g_TimerCount);
                FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
                      "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x3c, Text.Data);
            }
        }
        cur = cur->Next;
    }

    for (unsigned int i = 0; i < expired_count; i++) {
        FTimerNode* timer = expired[i];
        // Delivered to the timer's own controller, not broadcast.
        Server_ControllerDispatch(timer->TargetController, pub::Controller::EMessage::TimerExpired,
                                  &timer->Param1);
        TimerListRemove(timer);
        g_TimerCount--;
        free(timer);
    }
}

// ===========================================================================
// pub global functions -- .text:06d5e440..06d5f033 (0x7e440-0x7f033)
// ===========================================================================

namespace pub {

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
    DALib::Engine->ReleaseHandle(EngineIndex);
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
    FDUMP(kFdumpError, "%s(%d) : *** ERROR: missing System file '%s'",
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

} // namespace pub

static void TimerListInsertBefore(FTimerNode* sentinel, FTimerNode* node) {
    node->Next = sentinel;
    node->Prev = sentinel->Prev;
    sentinel->Prev->Next = node;
    sentinel->Prev = node;
}

// pub::SetTimer's body -- retail 0x06d5ee00: a 0x18-byte node pushed at the END of the
// timer list. Retail checks ONLY Timeout < 0 (-4); neither the Flid nor the allocation is
// checked.
extern "C" int __cdecl Server_ControllerSetTimer(unsigned int Flid, unsigned int Param1,
                                                 unsigned int Param2, float Timeout) {
    if (Timeout < 0.0f) return -4;
    FTimerNode* node = (FTimerNode*)malloc(sizeof(FTimerNode));
    node->TargetController = Flid;
    node->Param1 = Param1;
    node->Param2 = Param2;
    node->RemainingTime = Timeout;
    TimerListInsertBefore(&g_TimerListSentinel, node);
    g_TimerCount++;
    return 0;
}

namespace pub {

// pub::SetTimer -- retail 0x06d5ee00. Only Timeout < 0 is rejected (-4); the Flid is not
// validated. A 0x18-byte node goes on the timer list (DAT_06d90468, count DAT_06d9046c).
// Nothing is sent at insert time; message 0x3e goes to that one controller when the tick
// (0x06d5df60) expires the timer.
SERVER_API int __cdecl SetTimer(const unsigned int& Flid, const Controller::TimerExpired& Timer, float Timeout) {
    return Server_ControllerSetTimer(Flid, Timer.Param1, Timer.Param2, Timeout);
}

SERVER_API unsigned int __cdecl GetNicknameId(const char* Nickname) {
    return CreateID(Nickname);
}

SERVER_API unsigned short __cdecl MakeId(const char* Nickname) {
    return ::MakeId(Nickname);
}

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

// pub::DebugPrint -- retail 0x7ef40 (ordinal 94). Other levels print nothing.
SERVER_API void __cdecl DebugPrint(const char* Msg, int Level) {
    if (Level == 1) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: pub: %s",
              "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x3c5, Msg);
    } else if (Level == 2) {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: pub: %s",
              "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x3c6, Msg);
    }
}

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

// pub::GetNavMapScale -- retail 0x7f000 (ordinal 226). NavMapScale is ISystem+0x50.
SERVER_API int __cdecl GetNavMapScale(unsigned int SystemId, float& OutScale) {
    OutScale = 0.0f;
    Universe::ISystem const* Sys = Universe::get_system(SystemId);
    if (Sys == nullptr)
        return -1;
    OutScale = Sys->NavMapScale;
    return 0;
}

} // namespace pub
