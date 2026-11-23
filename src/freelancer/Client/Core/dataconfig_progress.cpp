// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Core/dataconfig_progress.h"

#include "Space/client_debris.h" // LoadDebrisArchFile (FUN_00530d20)
#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath
#include "dacom.h"
#include "DALib.h"
#include "Core/globals.h"
#include "Common/Fonts/font_imp.h"
#include "Common/Graphics/graph_library.h"
#include "Space/HUD/hud_element_list.h"
#include "Common/Fonts/ids_strings.h"   // RegisterIdsResourceModule (FUN_0057d800)
#include "Common/Fonts/menu_fonts_load.h"
#include "Common/Audio/menu_sound_arch.h"
#include "Common/Audio/sound_config.h"  // LoadClientSoundCfgIni (FUN_00427a70)
#include "Common/Graphics/texture_shapes.h"
#include "Common/Graphics/vis_effect_arch.h"
#include "Common/Audio/voice_profiles.h"

#include <stdlib.h>
#include <string.h>

namespace
{
    // .rdata 0x005e6a08, the source path FUN_005b0c30's FDUMP names.
    const char kMainCppPath[] = "E:\\FL\\Scratch\\Source\\Client\\main.cpp";

    // Every string field starts {0, ""} (static initializers LAB_00564a60 on); bDrawScripts is
    // DAT_00614a9c, 01 in the PE image.
    FIntroConfigState GIntroConfigState = {
        {0u, {0}}, {0u, {0}}, {0u, {0}}, {0u, {0}},
        {{0u, {0}}, {0u, {0}}, {0u, {0}}, {0u, {0}}},
        {0u, {0}}, 0, true};

    // FUN_00564b30 per-key store: a null value clears the field, otherwise FUN_00417c50 copies
    // at most 0x3f chars and the length is stored in front.
    void ReadIntroPathValue(INI_Reader& Reader, FIntroPathString& Field)
    {
        char const* const Value = Reader.get_value_string();
        if (Value == nullptr)
        {
            Field.Length = 0;
            Field.Text[0] = '\0';
            return;
        }
        Field.Length = CopyBoundedString(Field.Text, 0x40, Value);
    }

    // FUN_00564b30 (through a thunk from FUN_005b0c30). No reset on entry: a key the file does
    // not carry keeps its previous value. An open failure only runs the reader's cleanup: no
    // logo, no pick.
    void LoadIntroConfigFromIni(char const* IntroIniPath)
    {
        INI_Reader Reader;
        // The argument -- already resolved by the caller -- goes through GetFileSysPath against
        // DALib::Data a second time.
        char ResolvedPath[MAX_PATH];
        GetFileSysPath(ResolvedPath, IntroIniPath, DALib::Data);
        if (!Reader.open(ResolvedPath, false))
            return;

        while (Reader.read_header())
        {
            if (Reader.is_header("Images") && Reader.read_value())
            {
                do
                {
                    if (Reader.is_value("TgaPath"))
                        ReadIntroPathValue(Reader, GIntroConfigState.TgaPath);
                    else if (Reader.is_value("ScreenType"))
                        ReadIntroPathValue(Reader, GIntroConfigState.ScreenType);
                    else if (Reader.is_value("ScreenExit"))
                        ReadIntroPathValue(Reader, GIntroConfigState.ScreenExit);
                    else if (Reader.is_value("ScreenReset"))
                        ReadIntroPathValue(Reader, GIntroConfigState.ScreenReset);
                    else if (Reader.is_value("Screen1"))
                        ReadIntroPathValue(Reader, GIntroConfigState.Screen[0]);
                    else if (Reader.is_value("Screen2"))
                        ReadIntroPathValue(Reader, GIntroConfigState.Screen[1]);
                    else if (Reader.is_value("Screen3"))
                        ReadIntroPathValue(Reader, GIntroConfigState.Screen[2]);
                    else if (Reader.is_value("Screen4"))
                        ReadIntroPathValue(Reader, GIntroConfigState.Screen[3]);
                    else if (Reader.is_value("bDrawScripts"))
                        GIntroConfigState.bDrawScripts = Reader.get_value_bool(0);
                    else
                        Reader.log_link("unknown ini parameter");
                } while (Reader.read_value());
            }
        }

        GIntroConfigState.Logo.Length =
            CopyBoundedString(GIntroConfigState.Logo.Text, 0x40, "FRONT_freelancerlogo.tga");
        // (rand() * 3) >> 15, i.e. 0, 1 or 2.
        GIntroConfigState.IntroBasePick = (rand() * 3) / 0x8000;
    }

    // One [Data] value of FUN_005b0c30: an is_value chain in retail order. Every key but fonts /
    // rich_fonts / intro resolves the value with GetFileSysPath(path, value, 0) first; intro
    // resolves against DALib::Data into a second buffer. Loader results are dropped.
    void LoadClientDataValue(INI_Reader& Reader)
    {
        char Path[MAX_PATH];
        if (Reader.is_value("fonts"))
        {
            LoadFonts(Reader.get_value_string(), DALib::Data); // FUN_00417800
        }
        else if (Reader.is_value("rich_fonts"))
        {
            LoadRichFontsFromIni(Reader.get_value_string(), DALib::Data); // FUN_0057e310
        }
        else if (Reader.is_value("sounds"))
        {
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            LoadClientSoundArchIni(Path, false); // FUN_0042b030
        }
        else if (Reader.is_value("voices"))
        {
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            LoadClientVoicesIni(Path); // FUN_00430b00
        }
        else if (Reader.is_value("effects"))
        {
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            EffectArchLoadIni(Path, false); // FUN_0040eac0
        }
        else if (Reader.is_value("effect_shapes"))
        {
            // FUN_004f7230 -- queued; FUN_004f70d0 drains it at the end of InitializeEngine.
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            AddEffectShapesFile(Path);
        }
        else if (Reader.is_value("debris"))
        {
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            LoadDebrisArchFile(Path, false); // FUN_00530d20
        }
        else if (Reader.is_value("intro"))
        {
            char IntroPath[MAX_PATH];
            GetFileSysPath(IntroPath, Reader.get_value_string(), DALib::Data);
            LoadIntroConfigFromIni(IntroPath);
        }
        else if (Reader.is_value("HUD"))
        {
            // FUN_004c7370 stores the path only; FUN_004c73d0 instantiates the elements later.
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            SetHudIniPath(Path);
        }
        else if (Reader.is_value("igraph"))
        {
            // FUN_004197e0.
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            GraphLibraryLoad(Path);
        }
    }
}

FIntroConfigState const* GetIntroConfigState()
{
    return &GIntroConfigState;
}

bool LoadClientIniData(char const* IniPath)
{
    // FUN_005b0c30. Retail also calls FUN_004b6c50 (returns true) and drops the result.
    INI_Reader Reader;
    if (Reader.open(IniPath, false))
    {
        while (Reader.read_header())
        {
            if (Reader.is_header("Data"))
            {
                if (Reader.read_value())
                {
                    do
                    {
                        LoadClientDataValue(Reader);
                    } while (Reader.read_value());
                }
            }
            else if (Reader.is_header("Resources"))
            {
                // A failed DLL is reported and the null handle is still pushed onto the ids
                // module list (FUN_0057d800), so the ids of the DLLs after it keep their index.
                if (Reader.read_value())
                {
                    do
                    {
                        if (Reader.is_value("DLL"))
                        {
                            HMODULE const Module = LoadLibraryA(Reader.get_value_string());
                            if (Module == nullptr)
                            {
                                char const* const Name = Reader.get_value_string();
                                DWORD const Error = GetLastError();
                                FDUMP(0x100001,
                                      "%s(%d) : *** ERROR: Couldn't load %s! Error code %d\n",
                                      kMainCppPath, 0x22c, Name, Error);
                            }
                            RegisterIdsResourceModule(Module);
                        }
                    } while (Reader.read_value());
                }
            }
        }

        // Still inside the open success -- FUN_00427a70.
        char SoundCfgPath[MAX_PATH];
        GetFileSysPath(SoundCfgPath, "audio\\SoundCFG.ini", nullptr);
        LoadClientSoundCfgIni(SoundCfgPath);
    }
    return true;
}
