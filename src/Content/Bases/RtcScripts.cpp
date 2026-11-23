// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TU23 (06ed4220-06ed5390): the [CharacterEncounter] RTC script object. See RtcScripts.h.

#include "Bases/RtcScripts.h"

#include "Core/ContentInternal.h"    // OpenContentDataIni (FUN_06f24280)
#include "Core/ContentStaticIds.h"   // TU23's 16 static-init thunks 06ed4220-06ed4400
#include "NPCs/NpcShips.h"           // GSpecificNpcs (DAT_06fd081c)
#include "common_id.h"          // CreateID
#include "common_ini.h"         // INI_Reader
#include "common_server.h"      // pub::GetBaseID, pub::GF::*

#include <cstring>
#include <stdlib.h>

// FUN_06ed4420 (06ed4420-06ed446b). +0x00 / +0x04 are left unset, as in retail.
FCharacterEncounter::FCharacterEncounter()
    : bAutoplay(false)
    , MissionTextId(0)
    , NoOfferTextId(0)
    , LoadedMissionTextId(0)
    , RelocateLocationId(0)
    , StartRoomLocationId(0)
    , SequenceId(0)
{
}

// FUN_06ed4470 (06ed4470-06ed4494).
char const* FCharacterEncounter::FindActionText(int Type) const
{
    for (std::vector<FCharacterEncounterAction>::const_iterator It = Actions.begin();
         It != Actions.end(); ++It)
    {
        if (It->Type == Type)
            return It->Text.Data;
    }
    return nullptr;
}

// FUN_06ed44a0 (06ed44a0-06ed5075, thiscall, RET 4).
bool FCharacterEncounter::Load(char const* Path)
{
    INI_Reader Reader;
    bool const bOpened = OpenContentDataIni(Path, &Reader);
    if (!bOpened || !Reader.read_header())
        return bOpened;
    do
    {
        if (Reader.is_header("CharacterEncounter"))
        {
            if (!Reader.read_value())
                continue;
            do
            {
                // The action record's text is emptied on every line; its type is written
                // only by the action keys below.
                FCharacterEncounterAction Action;
                Action.Text.Length = 0;
                Action.Text.Data[0] = '\0';
                bool bAction = true;
                if (Reader.is_value("Location"))
                {
                    TString<0x20> BaseName;
                    BaseName = Reader.get_value_string(0);
                    TString<0x40> RoomName;
                    RoomName = Reader.get_value_string(1);
                    pub::GetBaseID(BaseId, BaseName.Data);
                    LocationId = pub::GF::FindLocation(BaseId, RoomName.Data);
                    bAction = false;
                }
                else if (Reader.is_value("action"))
                    Action.Type = 2;
                else if (Reader.is_value("walkup"))
                    Action.Type = 1;
                else if (Reader.is_value("greet"))
                    Action.Type = 0;
                else if (Reader.is_value("offer"))
                    Action.Type = 3;
                else if (Reader.is_value("decision"))
                    Action.Type = 4;
                else if (Reader.is_value("accept"))
                    Action.Type = 5;
                else if (Reader.is_value("decline") || Reader.is_value("reject"))
                    Action.Type = 6;
                else
                {
                    bAction = false;
                    if (Reader.is_value("autoplay"))
                    {
                        bAutoplay = true;
                    }
                    else if (Reader.is_value("mission_text_id"))
                    {
                        // Retail reads value 0 twice.
                        LoadedMissionTextId = Reader.get_value_int(0);
                        MissionTextId = Reader.get_value_int(0);
                    }
                    else if (Reader.is_value("nooffer_text_id"))
                    {
                        NoOfferTextId = Reader.get_value_int(0);
                    }
                    else if (Reader.is_value("relocate_player"))
                    {
                        TString<0x40> RoomName;
                        RoomName = Reader.get_value_string(0);
                        if (_stricmp(RoomName.Data, "Spaceflight") == 0)
                        {
                            RelocateLocationId = pub::GF::GetSpaceflightLocation();
                        }
                        else
                        {
                            unsigned int RoomBaseId = BaseId;
                            if (!Reader.is_value_empty(1))
                                pub::GetBaseID(RoomBaseId, Reader.get_value_string(1));
                            RelocateLocationId = pub::GF::FindLocation(RoomBaseId, RoomName.Data);
                        }
                    }
                    else if (Reader.is_value("start_room"))
                    {
                        TString<0x40> RoomName;
                        RoomName = Reader.get_value_string(0);
                        unsigned int RoomBaseId = BaseId;
                        if (!Reader.is_value_empty(1))
                            pub::GetBaseID(RoomBaseId, Reader.get_value_string(1));
                        StartRoomLocationId = pub::GF::FindLocation(RoomBaseId, RoomName.Data);
                    }
                }
                if (bAction)
                {
                    Action.Text = Reader.get_value_string();
                    Actions.push_back(Action);
                }
            } while (Reader.read_value());
        }
        else if (Reader.is_header("Reserve"))
        {
            if (!Reader.read_value())
                continue;
            int Spot;
            do
            {
                if (Reader.is_value("spot") &&
                    pub::GF::GetCharacterPlacementByName(LocationId, Reader.get_value_string(),
                                                         Spot) == 0)
                {
                    ReservedSpots.push_back(Spot);
                }
            } while (Reader.read_value());
        }
        else if (Reader.is_header("Char"))
        {
            FRtcScriptRole Role;
            Role.Role.Length = 0;
            Role.Role.Data[0] = '\0';
            Role.Character = nullptr;
            Role.Spot = -1;
            if (Reader.read_value())
            {
                do
                {
                    if (Reader.is_value("actor"))
                    {
                        // Unless a character is already set, the specific NPC of the same name.
                        Role.Role = Reader.get_value_string();
                        if (Role.Character == nullptr)
                        {
                            unsigned int const Key = CreateID(Reader.get_value_string());
                            FNpcCharacterDescMap::const_iterator It = GSpecificNpcs.find(Key);
                            if (It != GSpecificNpcs.end())
                                Role.Character = &It->second;
                        }
                    }
                    else if (Reader.is_value("spot"))
                    {
                        // The result is not tested.
                        pub::GF::GetCharacterPlacementByName(LocationId, Reader.get_value_string(),
                                                             Role.Spot);
                    }
                    else if (Reader.is_value("npc"))
                    {
                        // Replaces any earlier character.
                        unsigned int const Key = CreateID(Reader.get_value_string());
                        FNpcCharacterDescMap::const_iterator It = GSpecificNpcs.find(Key);
                        if (It != GSpecificNpcs.end())
                            Role.Character = &It->second;
                    }
                    else if (Reader.is_value("fidget"))
                    {
                        Role.Fidget = Reader.get_value_string();
                    }
                } while (Reader.read_value());
            }
            Roles.push_back(Role);
        }
    } while (Reader.read_header());
    return bOpened;
}
