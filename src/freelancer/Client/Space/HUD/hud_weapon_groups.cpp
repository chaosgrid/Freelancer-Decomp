// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/HUD/hud_weapon_groups.h"

#include "Space/HUD/hud_command_dispatch.h" // FSpaceCommand, SpaceSendCommand (FUN_0054bcb0)
#include "Space/HUD/hud_element_list.h"     // USER_ACTIVATE_ITEM
#include "Network/login_mgr.h"            // G_Server (DAT_0067ecd0)
#include "Space/player_spawn.h"         // GetPlayerConnectionId (DAT_00673344)

#include "common_cequip.h"
#include "common_cobject.h"
#include "common_iface.h"
#include "common_server.h"

#include <cstdio>
#include <cstring>
#include <string.h>

FWeaponGroups G_WeaponGroups;

namespace
{
    // The toggle record (command 0x19), through FUN_0054bcb0.
    void SendToggleCommand(unsigned short EquipId)
    {
        FSpaceCommand Command;
        memset(&Command, 0, sizeof(Command));
        Command.Byte26 = 1;
        Command.Id = USER_ACTIVATE_ITEM;
        Command.ObjectId = EquipId;
        Command.SubObjectId = 2;
        SpaceSendCommand(&Command);
    }
}

FWeaponGroups::FWeaponGroups()
    : Current(0)
{
    // FUN_004f53b0.
}

FWeaponGroups::~FWeaponGroups()
{
    // FUN_004f5410; then the member destructors.
    DeleteGroups();
}

void FWeaponGroups::DeleteGroups()
{
    int const Count = static_cast<int>(Groups.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FWeaponGroup* const Group = Groups[Index];
        if (Group != nullptr)
        {
            delete Group;
        }
    }
    Groups.erase(Groups.begin(), Groups.end());
}

CObject* FWeaponGroups::GetShipObject() const
{
    Watchable const* const Watched = Ship.target();
    if (Watched == nullptr)
    {
        return nullptr;
    }
    IObjRW* const Rw = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
    if (Rw == nullptr)
    {
        return nullptr;
    }
    CBase* const Base = static_cast<IObjInspectImpl*>(Rw);
    if (Base == nullptr)
    {
        return nullptr;
    }
    return Base->Object;
}

void FWeaponGroups::SetShip(IObjRW* NewShip)
{
    // FUN_004f5520.
    Watchable const* const Watched = (NewShip == nullptr)
        ? nullptr
        : reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char const*>(NewShip) + 8);
    Ship.set(Watched);
}

int FWeaponGroups::FindGroupIndex(int Id) const
{
    // FUN_004f5540.
    int const Count = static_cast<int>(Groups.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Groups[Index]->Id == Id)
        {
            return Index;
        }
    }
    return -1;
}

void FWeaponGroups::StoreActive(int Id)
{
    // FUN_004f5580. The group, or a new one appended.
    FWeaponGroup* Group = nullptr;
    int const Count = static_cast<int>(Groups.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Groups[Index]->Id == Id)
        {
            Group = Groups[Index];
            break;
        }
    }
    if (Group != nullptr)
    {
        Group->Hardpoints.erase(Group->Hardpoints.begin(), Group->Hardpoints.end());
    }
    else
    {
        Group = new FWeaponGroup;
        Group->Id = Id;
        Groups.insert(Groups.end(), Group);
    }

    // The watched CEqObj's launchers (mask 0xe0); the hardpoint is read before IsActive; an
    // active one's name is appended.
    CObject* const Object = GetShipObject();
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        CEqObj* const EqObj = static_cast<CEqObj*>(Object);
        CEquipTraverser Walk(EquipTypeMineDropper | EquipTypeCmDropper | EquipTypeGun);
        CEquip* Equip = EqObj->EquipManager.Traverse(Walk);
        while (Equip != nullptr)
        {
            char const* const Hardpoint =
                static_cast<CExternalEquip*>(Equip)->GetParentHPName().Value;
            if (Equip->IsActive())
            {
                Group->Hardpoints.insert(Group->Hardpoints.end(), 1,
                                         std::string(Hardpoint, std::strlen(Hardpoint)));
            }
            Equip = EqObj->EquipManager.Traverse(Walk);
        }
    }

    // Every exit sends.
    SendToServer();
}

int FWeaponGroups::GetGroupEquipIds(int Id, unsigned short* OutIds, int MaxIds)
{
    // FUN_004f5790.
    int const Index = FindGroupIndex(Id);
    if (Index == -1)
    {
        return 0;
    }
    FWeaponGroup const* const Group = Groups[Index];
    int const NameCount = static_cast<int>(Group->Hardpoints.size());

    int Found = 0;
    CObject* const Object = GetShipObject();
    if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
    {
        return Found;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);
    CEquipTraverser Walk(EquipTypeMineDropper | EquipTypeCmDropper | EquipTypeGun);
    CEquip* Equip = EqObj->EquipManager.Traverse(Walk);
    while (Equip != nullptr)
    {
        char const* const Hardpoint =
            static_cast<CExternalEquip*>(Equip)->GetParentHPName().Value;
        // The first matching name stores the id; a mismatch with the output already full moves
        // on to the next launcher (a match is stored without a bound test, as retail).
        for (int NameIndex = 0; NameIndex < NameCount; ++NameIndex)
        {
            if (_stricmp(Group->Hardpoints[NameIndex].c_str(), Hardpoint) == 0) // DACOM stricmp
            {
                OutIds[Found] = Equip->Id;
                ++Found;
                break;
            }
            if (Found >= MaxIds)
            {
                break;
            }
        }
        Equip = EqObj->EquipManager.Traverse(Walk);
    }
    return Found;
}

void FWeaponGroups::Activate(int Id)
{
    // FUN_004f5920.
    int const Index = FindGroupIndex(Id);
    CObject* const Object = GetShipObject();
    if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
    {
        return;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);
    CEquipTraverser Walk(EquipTypeMineDropper | EquipTypeCmDropper | EquipTypeGun);

    if (Index == -1)
    {
        // No such group -- toggle every active launcher off. The hardpoint is fetched and not
        // used.
        CEquip* Equip = EqObj->EquipManager.Traverse(Walk);
        while (Equip != nullptr)
        {
            static_cast<CExternalEquip*>(Equip)->GetParentHPName();
            if (Equip->IsActive())
            {
                SendToggleCommand(Equip->Id);
            }
            Equip = EqObj->EquipManager.Traverse(Walk);
        }
        return;
    }

    FWeaponGroup const* const Group = Groups[Index];
    int const NameCount = static_cast<int>(Group->Hardpoints.size());
    CEquip* Equip = EqObj->EquipManager.Traverse(Walk);
    while (Equip != nullptr)
    {
        char const* const Hardpoint =
            static_cast<CExternalEquip*>(Equip)->GetParentHPName().Value;
        bool const bWasActive = Equip->IsActive();
        bool bInGroup = false;
        for (int NameIndex = 0; NameIndex < NameCount; ++NameIndex)
        {
            if (_stricmp(Group->Hardpoints[NameIndex].c_str(), Hardpoint) == 0) // DACOM stricmp
            {
                bInGroup = true;
                break;
            }
        }
        if (bWasActive != bInGroup)
        {
            SendToggleCommand(Equip->Id);

            // The watched ship, not tested for null before use; a launcher that is active now
            // and is a gun is aimed at the ship's target (IObjAffect slot 17).
            CShip* ShipObject = nullptr;
            CObject* const Watched = GetShipObject();
            if (Watched != nullptr &&
                (static_cast<unsigned>(Watched->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            {
                ShipObject = static_cast<CShip*>(Watched);
            }
            if (Equip->IsActive() && CEGun::cast(Equip) != nullptr && Ship.target() != nullptr)
            {
                IObjRW* const Rw = ObjRwFromWatchable(const_cast<Watchable*>(Ship.target()));
                if (Rw != nullptr)
                {
                    unsigned EquipId = Equip->Id; // retail widens it into a dword local
                    IObjAffect* const Affect = static_cast<IObjAffect*>(Rw);
                    unsigned short const SubTarget = ShipObject->get_sub_target();
                    IObjRW* const Target = ShipObject->get_target();
                    Affect->set_gun_target(reinterpret_cast<unsigned short const*>(&EquipId), Target,
                                    SubTarget, static_cast<int>(GetPlayerConnectionId()));
                }
            }
        }
        Equip = EqObj->EquipManager.Traverse(Walk);
    }
}

void FWeaponGroups::SendToServer()
{
    // FUN_004f5c20.
    Text.erase(0, std::string::npos);

    // One line per hardpoint, groups in list order.
    int const Count = static_cast<int>(Groups.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FWeaponGroup const* const Group = Groups[Index];
        int const NameCount = static_cast<int>(Group->Hardpoints.size());
        for (int NameIndex = 0; NameIndex < NameCount; ++NameIndex)
        {
            char Line[0x1f0];
            sprintf(Line, "wg = %d, %s\n", Group->Id, Group->Hardpoints[NameIndex].c_str());
            Text.append(Line, std::strlen(Line));
        }
    }
    Text.append("\n", 1); // DAT_005cb7d0

    // IServer slot 70 with the player connection and strlen(c_str()).
    char const* const Data = Text.c_str();
    G_Server->SetWeaponGroup(GetPlayerConnectionId(),
                             reinterpret_cast<unsigned char*>(const_cast<char*>(Data)),
                             static_cast<int>(std::strlen(Data)));
}

void FWeaponGroups::ReadFromServer(char const* Data, int Size)
{
    // FUN_004f5e50.
    DeleteGroups();

    // Lines "<digits> , <name>" -- every compare is unsigned; a control character (< 0x20)
    // where a digit or a comma is expected ends the parse, and one inside a name ends that name
    // (the control run after it is skipped).
    unsigned char const* Cursor = reinterpret_cast<unsigned char const*>(Data);
    int Left = Size;
    while (Left > 0)
    {
        // To the first digit.
        while (Left > 0)
        {
            unsigned char const Char = *Cursor;
            if (Char >= '0' && Char <= '9')
            {
                break;
            }
            if (Char < 0x20)
            {
                return;
            }
            ++Cursor;
            --Left;
        }

        int Id = 0;
        bool bStoppedOnDigitEnd = false;
        while (Left > 0)
        {
            unsigned char const Char = *Cursor;
            if (Char < '0' || Char > '9')
            {
                bStoppedOnDigitEnd = true;
                break;
            }
            Id = Id * 10 + (Char - '0');
            ++Cursor;
            --Left;
        }

        if (bStoppedOnDigitEnd)
        {
            // To the comma.
            bool bAtComma = false;
            while (Left > 0)
            {
                unsigned char const Char = *Cursor;
                if (Char == ',')
                {
                    bAtComma = true;
                    break;
                }
                if (Char < 0x20)
                {
                    return;
                }
                ++Cursor;
                --Left;
            }
            if (bAtComma)
            {
                // Past the commas and spaces.
                while (Left > 0)
                {
                    unsigned char const Char = *Cursor;
                    if (Char != ',' && Char != ' ')
                    {
                        break;
                    }
                    ++Cursor;
                    --Left;
                }
            }
        }

        // The name, spaces dropped, up to a control character; then past the control run.
        std::string Name;
        while (Left > 0)
        {
            unsigned char const Char = *Cursor;
            if (Char < 0x20)
            {
                // Retail's first loop stops at once here (the character is <= 0x20); the
                // second skips while below 0x20.
                while (Left > 0 && *Cursor > 0x20)
                {
                    ++Cursor;
                    --Left;
                }
                while (Left > 0 && *Cursor < 0x20)
                {
                    ++Cursor;
                    --Left;
                }
                break;
            }
            if (Char != ' ')
            {
                Name.append(1, static_cast<char>(Char));
            }
            ++Cursor;
            --Left;
        }

        // The group with this id (appended when missing) gets the name (a copy through
        // c_str()).
        FWeaponGroup* Group = nullptr;
        int const Count = static_cast<int>(Groups.size());
        for (int Index = 0; Index < Count; ++Index)
        {
            if (Groups[Index]->Id == Id)
            {
                Group = Groups[Index];
                break;
            }
        }
        if (Group == nullptr)
        {
            Group = new FWeaponGroup;
            Group->Id = Id;
            Groups.insert(Groups.end(), Group);
        }
        char const* const NameText = Name.c_str();
        Group->Hardpoints.insert(Group->Hardpoints.end(), 1,
                                 std::string(NameText, std::strlen(NameText)));
    }
}

int FWeaponGroups::GetCurrent() const
{
    // FUN_004f6170.
    return Current;
}

void FWeaponGroups::SetCurrent(int Group)
{
    // FUN_004f6180.
    Current = Group;
}

void FWeaponGroups::Clear()
{
    // FUN_004f6190.
    DeleteGroups();
    SendToServer();
}

void FWeaponGroups::AddHardpoint(int Id, char const* Hardpoint)
{
    // FUN_004f6200.
    int const Index = FindGroupIndex(Id);
    if (Index == -1)
    {
        return;
    }
    FWeaponGroup* const Group = Groups[Index];
    int const NameCount = static_cast<int>(Group->Hardpoints.size());
    for (int NameIndex = 0; NameIndex < NameCount; ++NameIndex)
    {
        if (_stricmp(Group->Hardpoints[NameIndex].c_str(), Hardpoint) == 0) // DACOM stricmp
        {
            return;
        }
    }
    Group->Hardpoints.insert(Group->Hardpoints.end(), 1,
                             std::string(Hardpoint, std::strlen(Hardpoint)));
    SendToServer();
}

void FWeaponGroups::RemoveHardpoint(char const* Hardpoint)
{
    // FUN_004f6380. The group count is read once; ids 0 .. Count - 1 are looked up afresh.
    int const Count = static_cast<int>(Groups.size());
    for (int GroupId = 0; GroupId < Count; ++GroupId)
    {
        int const Index = FindGroupIndex(GroupId);
        if (Index == -1)
        {
            continue;
        }
        FWeaponGroup* const Group = Groups[Index];
        int NameCount = static_cast<int>(Group->Hardpoints.size());
        for (int NameIndex = 0; NameIndex < NameCount; ++NameIndex)
        {
            if (_stricmp(Group->Hardpoints[NameIndex].c_str(), Hardpoint) != 0) // DACOM stricmp
                continue;

            // The same index is looked at again.
            Group->Hardpoints.erase(Group->Hardpoints.begin() + NameIndex);
            --NameCount;
            --NameIndex;
        }
    }
    SendToServer();
}
