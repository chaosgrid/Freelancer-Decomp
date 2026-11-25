// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/menu_keymap_config.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath, GetUserDataPath
#include "dacom.h"
#include "Space/HUD/hud_command_dispatch.h"
#include "Common/Fonts/ids_strings.h"
#include "Common/input_manager.h" // GetClientMouseButtons (DAT_00616850)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <list>
#include <string.h>
#include <vector>
#include <windows.h>

namespace
{
    // Retail PTR_s_USER_NONE_00614dd8 -- 0xcc entries.
    unsigned const kKeyCmdNicknameCount = 0xCCu;

    char const* const GKeyCmdNicknames[kKeyCmdNicknameCount] = {
#include "Interface/menu_keymap_nicknames.inc"
    };

    // DAT_0067c254..DAT_0067c25c: every KeyCmd, by value in 0x28-byte list nodes (static init
    // LAB_00576950, atexit LAB_00576990).
    std::list<FKeyCmd> GKeyCmds;
    // DAT_0067c224..DAT_0067c22c: every [KeyMap] group, by value in 0x24-byte nodes (static init
    // LAB_00576a10, atexit LAB_00576a50).
    std::list<FKeyMapGroup> GKeyMapGroups;
    // DAT_0067c218..DAT_0067c220: the active groups, newest first (static init LAB_005773f0,
    // atexit LAB_00577430). The walkers read a node's next pointer after its body, as a list
    // iterator does.
    std::list<FKeyMapGroup*> GActiveGroups;
    bool G_bKeyMapLoaded = false;                     // DAT_0067c3b0
    char GUserKeyMapPath[0x104] = {};                 // DAT_0067c2a0

    // DAT_0067c260..DAT_0067c268 -- the keys held down, each with the modifier mask captured at
    // its key-down (0x10-byte nodes; static init LAB_005768b0, atexit LAB_005768f0).
    struct FKeyMapHeldKey
    {
        unsigned Key;       // node+0x08
        unsigned Modifiers; // node+0x0c
    };
    std::list<FKeyMapHeldKey> GHeldKeys;

    // The nickname lookup retail inlines in three places: stricmp against the 0xcc names in
    // order, the first match wins, no match gives 0 (USER_NONE).
    unsigned ResolveNicknameId(char const* Name)
    {
        unsigned Index = 0;
        for (; Index < kKeyCmdNicknameCount; ++Index)
        {
            if (_stricmp(Name, GKeyCmdNicknames[Index]) == 0)
                return Index;
        }
        return 0;
    }

    // The first KeyCmd in list order with this id.
    FKeyCmd* FindKeyCmdByNicknameId(unsigned NicknameId)
    {
        for (std::list<FKeyCmd>::iterator It = GKeyCmds.begin(); It != GKeyCmds.end(); ++It)
        {
            if (It->NicknameId == NicknameId)
                return &*It;
        }
        return nullptr;
    }

    // FUN_005765b0 -- parse one "key" value into VirtualKey + Modifiers.
    bool ParseKeyValue(INI_Reader& Reader, FKeyBinding* OutBinding)
    {
        OutBinding->VirtualKey = 0;
        OutBinding->Modifiers = 0;

        char const* Token = Reader.get_value_string(0);
        if (Token == nullptr || Token[0] == '\0')
            return false;

        if (Token[0] == '"')
        {
            OutBinding->VirtualKey = static_cast<unsigned char>(Token[1]);
        }
        else if (Token[0] == '0')
        {
            unsigned Parsed = 0;
            if (sscanf(Token, "%X", &Parsed) != 1)
            {
                OutBinding->VirtualKey = 0;
                return false;
            }
            OutBinding->VirtualKey = static_cast<int>(Parsed);
        }
        else
        {
            int Parsed = MsvcrtAtoi(Token);
            if (Parsed < -8 || Parsed > 0xff)
            {
                FDUMP(kFdumpError, "%s(%d) : *** ERROR: invalid KeyMap key value (%s)",
                      "E:\\FL\\Scratch\\Source\\Client\\Interface\\KeyMap.cpp", 0x1e6, Token);
            }
            OutBinding->VirtualKey = Parsed;
        }

        unsigned ParamIndex = 1;
        while (!Reader.is_value_empty(ParamIndex))
        {
            char const* Mod = Reader.get_value_string(ParamIndex);
            if (Mod == nullptr || Mod[0] == '\0')
                break;
            if (_strnicmp(Mod, "shift", 5) == 0)
                OutBinding->Modifiers = kKeyModShift;
            else if (_strnicmp(Mod, "control", 7) == 0)
                OutBinding->Modifiers = kKeyModControl;
            else if (_strnicmp(Mod, "alt", 3) == 0)
                OutBinding->Modifiers = kKeyModAlt;
            else
                return false;
            ++ParamIndex;
        }
        return true;
    }

    // FUN_005766e0 (thiscall on the KeyCmd, RET 4). Only the nickname id is reset; the other
    // fields keep whatever the caller's template put there. True when the nickname resolved.
    bool ParseKeyCmdSection(INI_Reader& Reader, FKeyCmd* Cmd)
    {
        Cmd->NicknameId = 0;

        while (Reader.read_value())
        {
            if (Reader.is_value("nickname"))
            {
                // An unknown nickname stores 0 and fails at once.
                Cmd->NicknameId = ResolveNicknameId(Reader.get_value_string());
                if (Cmd->NicknameId == 0)
                    return false;
            }
            else if (Reader.is_value("ids_name"))
            {
                // Read as a float, as retail.
                Cmd->IdsName = static_cast<unsigned>(static_cast<int>(Reader.get_value_float(0)));
            }
            else if (Reader.is_value("ids_info"))
            {
                Cmd->IdsInfo = static_cast<unsigned>(static_cast<int>(Reader.get_value_float(0)));
            }
            else if (Reader.is_value("state"))
            {
                // An empty first parameter means keydown; otherwise
                // every parameter up to the first empty one ORs its bit in (unknown words add
                // nothing), and a zero result becomes keydown.
                unsigned Mask = 0;
                unsigned ParamIndex = 0;
                if (!Reader.is_value_empty(0))
                {
                    do
                    {
                        char const* const StateTok = Reader.get_value_string(ParamIndex);
                        if (_stricmp(StateTok, "keydown") == 0)
                            Mask |= kKeyCmdStateKeyDown;
                        else if (_stricmp(StateTok, "keyup") == 0)
                            Mask |= kKeyCmdStateKeyUp;
                        else if (_stricmp(StateTok, "repeat") == 0)
                            Mask |= kKeyCmdStateRepeat;
                        else if (_stricmp(StateTok, "shift_ignore") == 0)
                            Mask |= kKeyCmdStateShiftIgnore;
                        ++ParamIndex;
                    } while (!Reader.is_value_empty(ParamIndex));
                }
                if (Mask == 0)
                    Mask = kKeyCmdStateKeyDown;
                Cmd->StateMask = Mask;
            }
            else if (Reader.is_value("key"))
            {
                FKeyBinding Binding;
                Binding.VirtualKey = 0;
                Binding.Modifiers = 0;
                if (ParseKeyValue(Reader, &Binding))
                    Cmd->Bindings.push_back(Binding);
            }
        }

        return Cmd->NicknameId != 0;
    }

    // FUN_00576550 (thiscall on the KeyCmd, RET 8) -- the state / modifier gate for one binding
    // against the event being routed (DAT_0067c278).
    bool BindingMatchesState(FKeyCmd const* Cmd, FKeyBinding const* Binding,
                             FHudCmdRecord const* Record)
    {
        // record+0x18 (key down) selects the keydown bit, else keyup.
        unsigned const NeedBit =
            Record->bKeyDown != 0 ? kKeyCmdStateKeyDown : kKeyCmdStateKeyUp;
        if ((Cmd->StateMask & NeedBit) == 0)
            return false;

        // An auto-repeat key-down (record+0x19) needs the `repeat` bit.
        if (Record->bRepeat != 0 && (Cmd->StateMask & kKeyCmdStateRepeat) == 0)
            return false;

        if ((Cmd->StateMask & kKeyCmdStateShiftIgnore) != 0)
            return true;

        return Binding->Modifiers == Record->Modifiers;
    }

    // FUN_00576410 -- the body is in hud_command_dispatch.cpp (KeyMap handler tree).
    bool DispatchKeyCommand(unsigned NicknameId, int EventKind)
    {
        return HudDispatchCommand(NicknameId, EventKind) != 0;
    }
}

int LoadKeyMapIni(char const* RelPath, bool bFatal)
{
    // FUN_00576bb0 (cdecl: path, bFatal). Appends every [KeyCmd] and [KeyMap] of the file to
    // the two lists (nothing is cleared first) and returns the number of [KeyMap] sections
    // seen; a failed open returns 0.
    char FullPath[MAX_PATH];
    GetFileSysPath(FullPath, RelPath, nullptr);

    INI_Reader Reader;
    if (!Reader.open(FullPath, bFatal))
        return 0;

    // A missing or wrong first header is reported and loading goes on.
    if (!Reader.read_header() || !Reader.is_header("KeyMap=1.1"))
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: KeyMap file (%s) is not version 1.1",
              "E:\\FL\\Scratch\\Source\\Client\\Interface\\KeyMap.cpp", 0x2ea, FullPath);
    }

    // DAT_0067c230, the template every new KeyCmd is copied from (built on first use behind the
    // DAT_0067c250 bit).
    static FKeyCmd const DefaultKeyCmd = {0u, 0u, 0u, kKeyCmdStateKeyDown,
                                          std::vector<FKeyBinding>()};

    int GroupCount = 0;
    while (Reader.read_header())
    {
        if (Reader.is_header("KeyCmd"))
        {
            // If the section does not resolve, the appended node is removed again.
            GKeyCmds.push_back(DefaultKeyCmd);
            if (!ParseKeyCmdSection(Reader, &GKeyCmds.back()))
                GKeyCmds.pop_back();
        }
        else if (Reader.is_header("KeyMap"))
        {
            ++GroupCount;
            GKeyMapGroups.push_back(FKeyMapGroup());
            FKeyMapGroup& Group = GKeyMapGroups.back();
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    Group.Nickname = Reader.get_value_string();
                }
                else if (Reader.is_value("key"))
                {
                    FKeyCmd* const Cmd = FindKeyCmdByNickname(Reader.get_value_string(0));
                    if (Cmd != nullptr)
                        Group.Commands.push_back(Cmd);
                }
            }
        }
    }

    return GroupCount;
}

int LoadUserKeyMapIni(char const* Path)
{
    // FUN_00576f40 (cdecl, 1 arg). The path goes through GetFileSysPath like the base map.
    char FullPath[MAX_PATH];
    GetFileSysPath(FullPath, Path, nullptr);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
        return 0;

    // Without a leading [KeyMap=1.1] nothing changes and 0 returns.
    if (!Reader.read_header() || !Reader.is_header("KeyMap=1.1"))
        return 0;

    for (std::list<FKeyCmd>::iterator It = GKeyCmds.begin(); It != GKeyCmds.end(); ++It)
        It->Bindings.clear();

    // Per [KeyCmd] section, "nickname" selects the existing KeyCmd
    // (the first with that id, or none) and every later "key" is appended to it while one is
    // selected. Nothing else in the section is read. Every [KeyCmd] section is counted.
    int SectionCount = 0;
    while (Reader.read_header())
    {
        if (!Reader.is_header("KeyCmd"))
            continue;

        FKeyCmd* Cmd = nullptr;
        while (Reader.read_value())
        {
            if (Reader.is_value("nickname"))
            {
                Cmd = FindKeyCmdByNicknameId(ResolveNicknameId(Reader.get_value_string()));
            }
            else if (Reader.is_value("key") && Cmd != nullptr)
            {
                FKeyBinding Binding;
                Binding.VirtualKey = 0;
                Binding.Modifiers = 0;
                if (ParseKeyValue(Reader, &Binding))
                    Cmd->Bindings.push_back(Binding);
            }
        }
        ++SectionCount;
    }

    return SectionCount;
}

int SaveUserKeyMapIni(char const* FullPath)
{
    // FUN_005772d0 (Ghidra splits the tail off as LAB_005773ad). Writes only KeyCmds that still
    // have at least one binding; nickname 0 (USER_NONE) is skipped.
    FILE* File = fopen(FullPath, "w");
    if (File == nullptr)
        return 0;

    fprintf(File, "; Freelancer - User Key Commands\n");
    fprintf(File, "\n[KeyMap=1.1]\n");

    for (std::list<FKeyCmd>::iterator It = GKeyCmds.begin(); It != GKeyCmds.end(); ++It)
    {
        FKeyCmd* const Cmd = &*It;
        if (Cmd->Bindings.empty())
            continue;
        if (Cmd->NicknameId == 0)
            continue;

        char const* Nick = (Cmd->NicknameId < kKeyCmdNicknameCount)
                               ? GKeyCmdNicknames[Cmd->NicknameId]
                               : "USER_NONE";
        fprintf(File, "\n[KeyCmd]\n");
        fprintf(File, "nickname = %s\n", Nick);

        unsigned BindIndex = 0;
        for (; BindIndex < Cmd->Bindings.size(); ++BindIndex)
        {
            FKeyBinding const& Binding = Cmd->Bindings[BindIndex];
            fprintf(File, "key = %d", Binding.VirtualKey);
            if (Binding.Modifiers == kKeyModShift)
                fprintf(File, ",SHIFT\n");
            else if (Binding.Modifiers == kKeyModControl)
                fprintf(File, ",CONTROL\n");
            else if (Binding.Modifiers == kKeyModAlt)
                fprintf(File, ",ALT\n");
            else
                fprintf(File, "\n");
        }
    }

    fclose(File);
    return 1;
}

char const* GetKeyMapIniRelPath()
{
    // FUN_00575d60.
    return "interface/KeyMap.ini";
}

char const* EnsureUserKeyMapPath()
{
    // FUN_00575d70 -- built once.
    if (GUserKeyMapPath[0] == '\0')
    {
        GetUserDataPath(GUserKeyMapPath);
        // An unconditional strcat (no length check), as retail.
        size_t Len = strlen(GUserKeyMapPath);
        memcpy(GUserKeyMapPath + Len, "/UserKeyMap.ini", 16);
    }
    return GUserKeyMapPath;
}

FKeyCmd* FindKeyCmdByNickname(char const* Nickname)
{
    // FUN_00577ef0.
    return FindKeyCmdByNicknameId(ResolveNicknameId(Nickname));
}

bool KeyMapFindCommandBinding(unsigned NicknameId, FKeyBindingLookup* Out, unsigned BindingIndex)
{
    // FUN_00577e10: every command with this id writes Out->NicknameId.
    for (std::list<FKeyCmd>::iterator It = GKeyCmds.begin(); It != GKeyCmds.end(); ++It)
    {
        FKeyCmd* const Cmd = &*It;
        if (Cmd->NicknameId != NicknameId)
            continue;
        Out->NicknameId = NicknameId;
        if (BindingIndex < Cmd->Bindings.size())
        {
            Out->VirtualKey = Cmd->Bindings[BindingIndex].VirtualKey;
            Out->Modifiers = Cmd->Bindings[BindingIndex].Modifiers;
            return true;
        }
    }
    return false;
}

bool KeyMapFindPrimaryBinding(unsigned NicknameId, FKeyBindingLookup* Out)
{
    // FUN_00577e80
    return KeyMapFindCommandBinding(NicknameId, Out, 0);
}

bool KeyMapFindAltBinding(unsigned NicknameId, FKeyBindingLookup* Out)
{
    // FUN_00577ea0
    return KeyMapFindCommandBinding(NicknameId, Out, 1);
}

FKeyCmd* FindKeyCmdById(unsigned NicknameId)
{
    // FUN_00577ec0.
    return FindKeyCmdByNicknameId(NicknameId);
}

unsigned KeyMapGetModifierState()
{
    // FUN_00575dc0.
    unsigned Mods = 0;
    if (GetKeyState(VK_SHIFT) < 0)
        Mods = 1;
    if (GetKeyState(VK_CONTROL) < 0)
        Mods |= 4;
    if (GetKeyState(VK_MENU) < 0)
        Mods |= 0x10;
    return Mods;
}

bool UpdateKeyCmdBinding(unsigned NicknameId, int NewVirtualKey, unsigned NewModifiers,
                         int OldVirtualKey, unsigned OldModifiers)
{
    // FUN_00578220. Retail's inlined vector growth (including a COMDAT-folded
    // IDPMsgHandler::OnReceive call on reallocation) is std::vector here.
    FKeyCmd* Cmd = FindKeyCmdByNicknameId(NicknameId);
    if (Cmd == nullptr)
        return false;

    unsigned BindIndex = 0;
    for (; BindIndex < Cmd->Bindings.size(); ++BindIndex)
    {
        FKeyBinding& Binding = Cmd->Bindings[BindIndex];
        if (Binding.VirtualKey != OldVirtualKey || Binding.Modifiers != OldModifiers)
            continue;

        if (NewVirtualKey != 0)
        {
            Binding.VirtualKey = NewVirtualKey;
            Binding.Modifiers = NewModifiers;
            return true;
        }

        Cmd->Bindings.erase(Cmd->Bindings.begin() + static_cast<int>(BindIndex));
        return true;
    }

    // Not found: insert only when NewVirtualKey != 0 and fewer than two bindings exist.
    if (NewVirtualKey == 0)
        return false;
    if (Cmd->Bindings.size() >= 2)
        return false;

    FKeyBinding Added = {};
    Added.VirtualKey = NewVirtualKey;
    Added.Modifiers = NewModifiers;
    Cmd->Bindings.push_back(Added);
    return false; // retail returns false after an insert too
}

FKeyMapGroup* FindKeyMapGroup(char const* GroupNickname)
{
    // FUN_00576b60 (cdecl). An unset nickname reads as "".
    for (std::list<FKeyMapGroup>::iterator It = GKeyMapGroups.begin(); It != GKeyMapGroups.end();
         ++It)
    {
        if (_stricmp(It->Nickname.c_str(), GroupNickname) == 0)
            return &*It;
    }
    return nullptr;
}

namespace
{
    void EnsureKeyMapLoaded()
    {
        // Inlined at the head of Register / UnregisterAcceleratorGroup. The flag is set only
        // after a successful load, so a failed load is retried on the next call.
        if (G_bKeyMapLoaded)
            return;
        int Loaded = LoadKeyMapIni("interface/KeyMap.ini", true);
        if (Loaded == 0)
            return; // retail still resolves the group after a failed load
        G_bKeyMapLoaded = true;
        LoadUserKeyMapIni(EnsureUserKeyMapPath());
    }
}

bool RegisterAcceleratorGroup(char const* GroupNickname)
{
    // FUN_00577490.
    EnsureKeyMapLoaded();

    FKeyMapGroup* const Group = FindKeyMapGroup(GroupNickname);
    if (Group == nullptr)
    {
        MessageBoxA(nullptr, "Invalid accelerator", "Error:", MB_OK);
        return false;
    }

    for (std::list<FKeyMapGroup*>::iterator It = GActiveGroups.begin();
         It != GActiveGroups.end(); ++It)
    {
        if (*It == Group)
            return true;
    }
    // The newest group sits at the front and every walker goes front to back, so the most
    // recently registered group wins.
    GActiveGroups.push_front(Group);
    return true;
}

void UnregisterAcceleratorGroup(char const* GroupNickname)
{
    // FUN_00577590. An empty active list returns before any load, lookup or error box.
    if (GActiveGroups.empty())
        return;
    EnsureKeyMapLoaded();

    FKeyMapGroup* const Group = FindKeyMapGroup(GroupNickname);
    if (Group == nullptr)
    {
        MessageBoxA(nullptr, "Invalid accelerator", "Error:", MB_OK);
        return;
    }

    // Only the first node holding the group is removed.
    for (std::list<FKeyMapGroup*>::iterator It = GActiveGroups.begin();
         It != GActiveGroups.end(); ++It)
    {
        if (*It != Group)
            continue;
        GActiveGroups.erase(It);
        return;
    }
}

bool KeyMapRouteInputMessage(unsigned Message, unsigned Wparam, unsigned Lparam)
{
    // FUN_00577850 (cdecl). Every message the pump reads comes through here.
    FHudCmdRecord* const Record = HudCommandRecord();

    // Every message clears the auto-repeat flag (record+0x19), including the ones that fall
    // out below.
    int Mode = 1;
    Record->bRepeat = 0;

    // Only 0x100..0x105; WM_DEADCHAR shares the "false" arm.
    unsigned Kind = 0;
    switch (Message)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        // Repeat = previous state down, transition 0, no context bit.
        Record->Key = Wparam;
        Record->bRepeat = (Lparam & 0xF0000000u) == 0x40000000u ? 1 : 0;
        Kind = 1;
        Record->bKeyDown = 1;
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        Record->Key = Wparam;
        Record->bKeyDown = 0;
        Record->bRepeat = (Lparam & 0xF0000000u) == 0x40000000u ? 1 : 0;
        Kind = 1;
        break;
    case WM_CHAR:
        Record->Key = 0;
        Kind = 2;
        Record->bKeyDown = 1;
        break;
    default:
        return false;
    }

    Record->WindowMessage = Message;
    Record->Arg10 = Wparam;
    Record->Arg14 = Lparam;

    unsigned Modifiers = 0;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
        Modifiers = 1;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        Modifiers = Modifiers | 4u;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0)
        Modifiers = Modifiers | 0x10u;
    Record->Modifiers = Modifiers;

    // Kind 0 cannot reach here; kinds 1..2 dispatch in mode 1, anything else in mode 2.
    if (Kind == 0)
        return false;
    if (Kind > 2)
        Mode = 2;

    // The raw kind (1 = key, 2 = char) goes to the handler tree first.
    if (HudDispatchCommand(Kind, Mode) != 0)
        return true;

    // No key (WM_CHAR) skips the held-key list and the group walk.
    if (Record->Key != 0)
    {
        // The live modifiers are restored only when a held key lent its own.
        bool bRestoreModifiers = false;
        unsigned const LiveModifiers = Record->Modifiers;

        // Key events that are not Shift/Ctrl/Alt themselves and not an auto-repeat.
        unsigned const Key = Record->Key;
        if (Kind == 1 && Key != VK_SHIFT && Key != VK_CONTROL && Key != VK_MENU &&
            Record->bRepeat == 0)
        {
            if (Record->bKeyDown != 0)
            {
                FKeyMapHeldKey Held;
                Held.Key = Key;
                Held.Modifiers = LiveModifiers;
                GHeldKeys.push_back(Held);
            }
            else
            {
                // Key up: every held node with this key lends its modifiers to the record and
                // is removed; the walk continues past an erase, so the last match wins.
                std::list<FKeyMapHeldKey>::iterator It = GHeldKeys.begin();
                while (It != GHeldKeys.end())
                {
                    if (It->Key != 0 && It->Key == Record->Key)
                    {
                        Record->Modifiers = It->Modifiers;
                        It = GHeldKeys.erase(It);
                        bRestoreModifiers = true;
                        continue;
                    }
                    ++It;
                }
            }
        }

        // Active groups (newest first) -> their KeyCmds -> each binding. The key is re-read
        // from the record for every binding.
        if (Record->Key != 0)
        {
            for (std::list<FKeyMapGroup*>::iterator GroupIt = GActiveGroups.begin();
                 GroupIt != GActiveGroups.end(); ++GroupIt)
            {
                FKeyMapGroup* const Group = *GroupIt;
                for (std::list<FKeyCmd*>::iterator CmdIt = Group->Commands.begin();
                     CmdIt != Group->Commands.end(); ++CmdIt)
                {
                    FKeyCmd* const Cmd = *CmdIt;
                    unsigned BindIndex = 0;
                    for (; BindIndex < Cmd->Bindings.size(); ++BindIndex)
                    {
                        FKeyBinding const* Binding = &Cmd->Bindings[BindIndex];
                        if (static_cast<unsigned>(Binding->VirtualKey) != Record->Key)
                            continue;
                        if (!BindingMatchesState(Cmd, Binding, Record))
                            continue;
                        // Consumed returns at once: the lent modifiers are NOT restored on
                        // this path, as retail.
                        if (DispatchKeyCommand(Cmd->NicknameId, Mode))
                        {
                            return true;
                        }
                    }
                }
            }
        }

        if (bRestoreModifiers)
            Record->Modifiers = LiveModifiers;
    }

    // A key event for VK_MENU itself is always consumed.
    if (Kind == 1 && Wparam == VK_MENU)
        return true;
    return false;
}

bool KeyMapRouteMouseEvent(FHudCmdRecord const* Event)
{
    // FUN_00577680 (cdecl). Called only by MouseDispatchEvents (FUN_0041ef20) for button and
    // wheel events.
    FHudCmdRecord* const Record = HudCommandRecord();

    unsigned Kind = 0;
    switch (static_cast<int>(Event->Key))
    {
    case -9:
        Kind = 4;
        break;
    case -8:
        Kind = 0xd;
        break;
    case -7:
        Kind = 0xc;
        break;
    case -6:
        Kind = 0xb;
        break;
    case -5:
        Kind = 0xa;
        break;
    case -4:
        Kind = 9;
        break;
    case -3:
        Kind = 8;
        break;
    case -2:
        Kind = 7;
        break;
    case -1:
        Kind = 6;
        break;
    default:
        break;
    }

    *Record = *Event;
    Record->WindowMessage = 0;
    Record->Arg10 = 0;
    Record->Arg14 = 0;

    unsigned Modifiers = 0;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
        Modifiers = 1;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        Modifiers = Modifiers | 4u;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0)
        Modifiers = Modifiers | 0x10u;
    Record->Modifiers = Modifiers;

    if (DispatchKeyCommand(Kind, 2))
        return true;

    // Every binding on the key in the active groups dispatches its KeyCmd in mode 2. The
    // record's flags are re-read after every dispatch.
    if (Record->Key == 0)
        return false;

    for (std::list<FKeyMapGroup*>::iterator GroupIt = GActiveGroups.begin();
         GroupIt != GActiveGroups.end(); ++GroupIt)
    {
        FKeyMapGroup* const Group = *GroupIt;
        for (std::list<FKeyCmd*>::iterator CmdIt = Group->Commands.begin();
             CmdIt != Group->Commands.end(); ++CmdIt)
        {
            FKeyCmd* const Cmd = *CmdIt;
            unsigned BindIndex = 0;
            for (; BindIndex < Cmd->Bindings.size(); ++BindIndex)
            {
                FKeyBinding const* Binding = &Cmd->Bindings[BindIndex];
                if (static_cast<unsigned>(Binding->VirtualKey) != Record->Key)
                    continue;
                if (!BindingMatchesState(Cmd, Binding, Record))
                    continue;
                if (DispatchKeyCommand(Cmd->NicknameId, 2))
                    return true;
            }
        }
    }
    return false;
}

unsigned KeyMapGetActionNicknameId(char const* Nickname)
{
    return ResolveNicknameId(Nickname);
}

char const* KeyMapGetActionNickname(unsigned NicknameId)
{
    if (NicknameId >= kKeyCmdNicknameCount)
        return GKeyCmdNicknames[0];
    return GKeyCmdNicknames[NicknameId];
}

unsigned KeyMapGetActiveGroupCount()
{
    return static_cast<unsigned>(GActiveGroups.size());
}

bool KeyMapFindActiveBinding(unsigned NicknameId, FKeyBindingLookup* Out)
{
    // FUN_00577dd0 (cdecl), with FUN_00577d60's per-group walk (index 0) inlined. A command with
    // the id writes Out->NicknameId even when it has no binding. Nothing is null-tested.
    for (std::list<FKeyMapGroup*>::iterator GroupIt = GActiveGroups.begin();
         GroupIt != GActiveGroups.end(); ++GroupIt)
    {
        FKeyMapGroup* const Group = *GroupIt;
        for (std::list<FKeyCmd*>::iterator CmdIt = Group->Commands.begin();
             CmdIt != Group->Commands.end(); ++CmdIt)
        {
            FKeyCmd* const Cmd = *CmdIt;
            if (Cmd->NicknameId != NicknameId)
                continue;
            Out->NicknameId = NicknameId;
            if (Cmd->Bindings.empty())
                continue;

            FKeyBinding const& Binding = Cmd->Bindings[0];
            Out->VirtualKey = Binding.VirtualKey;
            Out->Modifiers = Binding.Modifiers;
            return true;
        }
    }
    return false;
}

bool KeyMapIsCommandHeld(unsigned NicknameId)
{
    // FUN_005784a0. Only the first command with this id is looked at.
    FKeyCmd* const Cmd = FindKeyCmdByNicknameId(NicknameId);
    if (Cmd == nullptr)
        return false;

    unsigned Index = 0;
    for (; Index < Cmd->Bindings.size(); ++Index)
    {
        FKeyBinding const& Binding = Cmd->Bindings[Index];

        // Re-read for every binding.
        unsigned Modifiers = 0;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
            Modifiers = 1;
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
            Modifiers = Modifiers | 4u;
        if ((GetKeyState(VK_MENU) & 0x8000) != 0)
            Modifiers = Modifiers | 0x10u;
        if ((Modifiers & Binding.Modifiers) != Binding.Modifiers)
            continue;

        if (Binding.VirtualKey >= 0)
        {
            if ((GetKeyState(Binding.VirtualKey) & 0x8000) != 0)
                return true;
            continue;
        }

        // Mouse buttons -1/-2/-3 -> bits 1/2/4 of DAT_00616850; any other negative key tests
        // mask 0 and never matches.
        unsigned Mask = 0;
        if (Binding.VirtualKey == -3)
            Mask = 4;
        else if (Binding.VirtualKey == -2)
            Mask = 2;
        else if (Binding.VirtualKey == -1)
            Mask = 1;
        if ((GetClientMouseButtons() & Mask) != 0)
            return true;
    }
    return false;
}

void KeyMapFormatBindingName(FKeyBindingLookup const* Binding, wchar_t* Dest, unsigned DestChars)
{
    // FUN_00577f50 -- GetKeyNameTextW with IDS fallbacks, behind a modifier prefix.
    if (Binding == nullptr || Dest == nullptr || DestChars == 0)
        return;
    Dest[0] = L'\0';

    unsigned const Vk = static_cast<unsigned>(Binding->VirtualKey);
    wchar_t KeyName[256];
    KeyName[0] = L'\0';

    if (Vk < 0x101u)
    {
        UINT const Scan = MapVirtualKeyA(Vk, MAPVK_VK_TO_VSC);
        LONG LParam = static_cast<LONG>(Scan << 16);
        bool bExtended = false;
        if (Vk == 0x26 || Vk == 0x23 || Vk == 0x24 || Vk == 0x25 || Vk == 0x27 || Vk == 0x28
            || Vk == 0x2d || Vk == 0x2e || Vk == 0x21 || Vk == 0x22)
            bExtended = true;

        if (Scan != 0)
        {
            if (bExtended || GetKeyNameTextW(LParam, KeyName, 256) == 0)
            {
                LParam |= 0x1000000;
                if (GetKeyNameTextW(LParam, KeyName, 256) == 0)
                    KeyName[0] = L'\0';
            }
        }
        if (KeyName[0] == L'\0')
            GetIdsStringWWithRawFallback(0x3ae, KeyName, 256);
    }
    else
    {
        unsigned Ids = 0x3ae;
        switch (static_cast<int>(Binding->VirtualKey))
        {
        case -9:
            Ids = 0x4ea;
            break;
        case -8:
            Ids = 0x3ad;
            break;
        case -7:
            Ids = 0x3ac;
            break;
        case -6:
            Ids = 0x3ab;
            break;
        case -5:
            Ids = 0x3aa;
            break;
        case -4:
            Ids = 0x3a9;
            break;
        case -3:
            Ids = 0x3a8;
            break;
        case -2:
            Ids = 0x3a7;
            break;
        case -1:
            Ids = 0x3a6;
            break;
        default:
            Ids = 0x3ae;
            break;
        }
        GetIdsStringWWithRawFallback(Ids, KeyName, 256);
    }

    if (Binding->Modifiers == 0)
    {
        wcsncpy(Dest, KeyName, DestChars - 1);
        Dest[DestChars - 1] = L'\0';
        return;
    }

    // Modifiers first (Shift/Control/Alt), then the key name.
    wchar_t Prefix[256];
    Prefix[0] = L'\0';
    if ((Binding->Modifiers & kKeyModShift) != 0)
    {
        size_t const Len = wcslen(Prefix);
        UINT const Scan = MapVirtualKeyA(0x10, MAPVK_VK_TO_VSC);
        if (GetKeyNameTextW(static_cast<LONG>(Scan << 16), Prefix + Len,
                            static_cast<int>(256 - Len))
            == 0)
            GetIdsStringWWithRawFallback(0x4eb, Prefix + Len, static_cast<unsigned>(256 - Len));
        else
            wcscat(Prefix, L" + ");
    }
    if ((Binding->Modifiers & kKeyModControl) != 0)
    {
        size_t const Len = wcslen(Prefix);
        UINT const Scan = MapVirtualKeyA(0x11, MAPVK_VK_TO_VSC);
        if (GetKeyNameTextW(static_cast<LONG>(Scan << 16), Prefix + Len,
                            static_cast<int>(256 - Len))
            == 0)
            GetIdsStringWWithRawFallback(0x4ec, Prefix + Len, static_cast<unsigned>(256 - Len));
        else
            wcscat(Prefix, L" + ");
    }
    if ((Binding->Modifiers & kKeyModAlt) != 0)
    {
        size_t const Len = wcslen(Prefix);
        UINT const Scan = MapVirtualKeyA(0x12, MAPVK_VK_TO_VSC);
        if (GetKeyNameTextW(static_cast<LONG>(Scan << 16), Prefix + Len,
                            static_cast<int>(256 - Len))
            == 0)
            GetIdsStringWWithRawFallback(0x4ed, Prefix + Len, static_cast<unsigned>(256 - Len));
        else
            wcscat(Prefix, L" + ");
    }

    size_t const PrefixLen = wcslen(Prefix);
    size_t const KeyLen = wcslen(KeyName);
    if (PrefixLen + KeyLen < DestChars)
    {
        wcscpy(Dest, Prefix);
        wcscat(Dest, KeyName);
    }
    else
        GetIdsStringWWithRawFallback(0x3ae, Dest, DestChars);
}
