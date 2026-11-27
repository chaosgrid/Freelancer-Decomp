// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Core/globals.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates
#include "common_ini.h"
#include "dacom.h"
#include "Core/game_options.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// DAT_00679bc0. Zero-initialised like retail's .bss copy; FUN_005621a0 fills it.
// (The definition lives in window.cpp next to CreateGameWindow.)

// DAT_0067ecbc.
bool G_bFpExceptionTrap = false;

// FUN_00417c50.
unsigned CopyBoundedString(char* Dest, unsigned Capacity, const char* Src)
{
    if (Src == nullptr)
        return 0;

    unsigned Length = static_cast<unsigned>(strlen(Src));
    if (Length >= Capacity)
        Length = Capacity - 1;
    memcpy(Dest, Src, Length);
    Dest[Length] = '\0';
    return Length;
}

// FUN_005621a0, __thiscall(this = &DAT_00679bc0, FClientCommandLine*), called once from WinMain.
void DisplaySettings::LoadStartupConfig(const FClientCommandLine* CommandLine)
{
    // Scatter the 71 descriptors at .data:006143b8 into this+0x28. No bound check: every Index
    // in the constant table is in range.
    for (int Row = 0; Row < GGameOptionDescCount; ++Row)
        options[GGameOptionDescs[Row].Index] = GGameOptionDescs[Row].DefaultValue;

    ini_path.Length = CopyBoundedString(ini_path.Text, 0x104, "Freelancer.ini");
    data_path.Length = CopyBoundedString(data_path.Text, 0x104, "..\\Data");

    // In retail's store order.
    present_when_inactive = 0;
    quick_flag = 0;
    color_bpp = -1;
    depth_bpp = -1;
    stencil_bpp = 0;
    width = 1024;
    height = 768;
    interval = 1;
    buffer_count = 2;
    border = 1;
    refresh_rate = 0x3c;
    mode_field00 = -1;
    fullscreen = 1;

    // The movie_file counter is kept across every [Startup] section in the file.
    int MovieCount = 0;

    INI_Reader Reader;
    if (Reader.open(ini_path.Text, false))
    {
        if (Reader.read_header())
        {
            do
            {
                if (Reader.is_header("Freelancer"))
                {
                    if (Reader.read_value())
                    {
                        do
                        {
                            if (Reader.is_value("data path"))
                            {
                                const char* Value = Reader.get_value_string();
                                if (Value == nullptr)
                                {
                                    data_path.Length = 0;
                                    data_path.Text[0] = '\0';
                                }
                                else
                                {
                                    data_path.Length =
                                        CopyBoundedString(data_path.Text, 0x104, Value);
                                }
                            }
                        } while (Reader.read_value());
                    }
                }
                else if (Reader.is_header("Startup"))
                {
                    if (Reader.read_value())
                    {
                        do
                        {
                            // The count is tested after the name, and bumped before the
                            // null check, so a null value still uses up a slot.
                            if (Reader.is_value("movie_file") && MovieCount < 3)
                            {
                                const char* Value = Reader.get_value_string();
                                FClientPathString& Movie = movie_files[MovieCount];
                                ++MovieCount;
                                if (Value == nullptr)
                                {
                                    Movie.Length = 0;
                                    Movie.Text[0] = '\0';
                                }
                                else
                                {
                                    Movie.Length = CopyBoundedString(Movie.Text, 0x104, Value);
                                }
                            }
                        } while (Reader.read_value());
                    }
                }
                else if (Reader.is_header("Data"))
                {
                    if (Reader.read_value())
                    {
                        do
                        {
                            if (Reader.is_value("gate_tunnels"))
                            {
                                const char* Value = Reader.get_value_string();
                                if (Value == nullptr)
                                {
                                    gate_tunnels.Length = 0;
                                    gate_tunnels.Text[0] = '\0';
                                }
                                else
                                {
                                    gate_tunnels.Length =
                                        CopyBoundedString(gate_tunnels.Text, 0x104, Value);
                                }
                            }
                            else if (Reader.is_value("jump_effect"))
                            {
                                const char* Value = Reader.get_value_string();
                                if (Value == nullptr)
                                {
                                    jump_effect.Length = 0;
                                    jump_effect.Text[0] = '\0';
                                }
                                else
                                {
                                    jump_effect.Length =
                                        CopyBoundedString(jump_effect.Text, 0x104, Value);
                                }
                            }
                        } while (Reader.read_value());
                    }
                }
            } while (Reader.read_header());
        }
    }
    else
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Unable to read Freelancer config file (%s)",
              "E:\\FL\\Scratch\\Source\\Client\\config.cpp", 0x17f, ini_path.Text);
    }

    // "+Q"/"-Q" and "+X"/"-X". A token that does not start with '+' or
    // '-' clears both the sign and the letter, so it matches nothing. The sign is '+'
    // (anything but '-'); "Q" ignores it.
    for (unsigned Index = 0; Index < CommandLine->Count; ++Index)
    {
        const char* Token = CommandLine->Argv[Index];
        bool bPlus = false;
        char Letter = 0;
        if (Token[0] == '+' || Token[0] == '-')
        {
            bPlus = Token[0] != '-';
            Letter = static_cast<char>(toupper(Token[1]));
        }

        if (Letter == 'Q')
            quick_flag = 1;
        else if (Letter == 'X')
            G_bFpExceptionTrap = bPlus;
    }
}

// FUN_004242e0, __thiscall(this = render mode block, FClientCommandLine*). The only caller is
// InitializeEngine, on its local mode block after PerfOptions.ini has filled it.
bool DisplaySettings::ApplyCmdlineOverrides(const FClientCommandLine* CommandLine)
{
    bool bFound = false;

    for (unsigned Index = 0; Index < CommandLine->Count; ++Index)
    {
        const char* Token = CommandLine->Argv[Index];
        bool bPlus = false;
        char Letter = 0;    // zero for a token without a sign
        const char* Tail = Token;
        if (Token[0] == '+' || Token[0] == '-')
        {
            bPlus = Token[0] != '-';
            Letter = static_cast<char>(toupper(Token[1]));
            Tail = Token + 2;
        }

        switch (Letter)
        {
        case 'B':
            bFound = true;
            border = bPlus ? 0 : 1;
            break;
        case 'F':
            bFound = true;
            fullscreen = bPlus ? 0 : 1;
            break;
        case 'M':
        {
            bFound = true;
            const int Size = MsvcrtAtoi(Tail);
            if (Size > 800)
            {
                if (Size == 1024)
                {
                    width = 1024;
                    height = 768;
                }
                else
                {
                    width = 640;
                    height = 480;
                }
            }
            else if (Size == 800)
            {
                width = 800;
                height = 600;
            }
            else if (Size == 320)
            {
                width = 320;
                height = 240;
            }
            else if (Size == 512)
            {
                width = 512;
                height = 384;
            }
            else
            {
                width = 640;
                height = 480;
            }
            break;
        }
        case 'W':
            bFound = true;
            fullscreen = 0;
            break;
        default:
            break;
        }
    }

    return bFound;
}
