// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/client_cliobj.h"     // FlushMaterialCache (FUN_0041b150)
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates
#include "Core/client_frame_tick.h"
#include "Interface/client_ime_focus.h" // ReleaseClientIMEFocus (FUN_005641d0)
#include "Core/client_state.h"
#include "Network/intro_connect.h"

#include "common_ini.h"
#include "common_id.h"
#include "common_reputation.h"
#include "common_async.h"        // Async::Shutdown
#include "common_dataconfig.h"   // DataConfig::shutdown
#include "common_errorhandler.h" // ErrorHandler::activate / deactivate / new_log
#include "common_misc.h"         // GetUserLocalDataPath
#include "common_text.h"
#include "GameFlow/character_info.h"   // ShutdownCharacterInfo (FUN_00435500)
#include "NeuroNet/Chat/chat_init.h"        // ChatShutdown (FUN_00469b30)
#include "Space/client_debris.h"    // ShutdownDebrisArchDb (FUN_00530fa0)
#include "Network/connect_flow.h"
#include "Space/Effects/effect_base.h"      // EffectSystemShutdown (FUN_004f7140)
#include "Common/Graphics/vis_effect_arch.h"  // EffectArchShutdown (FUN_0040ef30)
#include "Common/Fonts/font_imp.h"         // FontShutdown (FUN_004174a0)
#include "Core/client_tick_profile.h" // Port-only diagnostics, released before fonts
#include "GameFlow/gameflow.h"
#include "Core/globals.h"
#include "Common/Graphics/graph_library.h"
#include "dacom.h"
#include "DALib.h"
#include "dalib_net.h"
#include "Common/Fonts/ids_strings.h"
#include "Interface/loading_screen.h"   // LoadingScreenShutdown (FUN_005791c0)
#include "Interface/menu_keymap_config.h"
#include "Common/Audio/menu_sound_arch.h"
#include "Interface/menu_ui_init.h"
#include "Network/login_mgr.h"
#include "Interface/mission_briefing.h"  // GetMissionBriefingState (FUN_0043d560)
#include "NeuroNet/nn_error.h"
#include "NeuroNet/nn_modal.h"         // NnSystemShutdown (FUN_0045a140)
#include "NeuroNet/nn_montage_draw.h"  // ClearButtonMontage (FUN_00559d20)
#include "NeuroNet/nn_rollover.h"      // NnRolloverDestroyGlobal (FUN_004b6580)
#include "NeuroNet/nn_tooltip.h"       // GetNnToolTipSingleton (DAT_0067244c)
#include "Space/space_state.h"      // GetSpaceState (FUN_0054bd30)
#include "Interface/win3d.h"
#include "Network/server_connect.h"
#include "Common/Graphics/texture_shapes.h"   // ClearEffectShapesFiles (FUN_004f73b0)
#include "Interface/ui_scroll.h"
#include "Common/Audio/voice_profiles.h"
#ifdef FREELANCER_RP9
#include "Common/Graphics/render_ex.h"        // RenderEx::Shutdown (port-only Direct3D 9 extensions)
#endif

#include <ctype.h>
#include <float.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

// DAT_0067ecc0 -- "+G"/"-G" (FUN_005b2cb0); read by CLoginMgr::Login (FUN_005aa510), hence
// outside the anonymous namespace (declared in login_mgr.h).
bool G_bCommandLineG = false;

namespace
{
    // DAT_0067e670 -- the application name FUN_005a5910 hands Dr. Watson (0x80 wchar, the last
    // one forced to zero at DAT_0067e76e).
    wchar_t G_CrashAppName[0x80];

    // DAT_0067de50 -- the '|'-separated list of files Dr. Watson attaches: FUN_005b2800 puts the
    // spew log path here, FUN_005a5390 appends the dxdiag report. 0x410 wchar, ending right
    // where G_CrashAppName begins in retail's .data.
    wchar_t G_CrashReportFiles[0x410];

    // The 0x1c50-byte block FUN_005a54e0 maps for Dr. Watson (dw15.exe, handed over as
    // "-x -s <mapping>"): the dw15 SDK's shared-memory block. Member names follow that SDK's
    // where the use agrees (the wait loop is its sample loop: wait on EventAlive, then poll
    // EventDone; signal EventDone under the mutex when dw15 stops pinging). The string sizes
    // are the gaps between the members written; the members never written are Field<offset>.
    struct FDwSharedMem
    {
        DWORD Size;                           // +0x00 0x1c50
        DWORD ProcessId;                      // +0x04
        DWORD ThreadId;                       // +0x08
        void* ExceptionAddress;               // +0x0c
        EXCEPTION_POINTERS* ExceptionPointers; // +0x10
        HANDLE EventDone;                     // +0x14 polled after EventAlive; set when giving up
        DWORD Field18;                        // +0x18
        HANDLE EventAlive;                    // +0x1c dw15's ping, waited on for 20 s
        HANDLE Mutex;                         // +0x20
        HANDLE Process;                       // +0x24 DuplicateHandle of this process, inheritable
        DWORD BehaviorFlags;                  // +0x28 0x100
        DWORD Result;                         // +0x2c read back: 0x10 = continue the search
        DWORD Field30;                        // +0x30
        DWORD OfferFlags;                     // +0x34 1
        DWORD Field38;                        // +0x38
        DWORD LetRunFlags;                    // +0x3c 0x10
        DWORD Field40;                        // +0x40
        DWORD Field44;                        // +0x44
        wchar_t FormalAppName[0x38];          // +0x48 G_CrashAppName
        wchar_t ModuleFileName[0x104];        // +0xb8 GetModuleFileNameA, widened
        unsigned char Field2C0[0x1040 - 0x2c0];
        char PidRegKey[0x104];                // +0x1040 the DigitalProductID value's path
        DWORD LcidUI;                         // +0x1144 LangID + 0x400
        char RegSubPath[0xc8];                // +0x1148 "Microsoft\PCHealth\ErrorReporting\DW"
        wchar_t DotDataDlls[0x104];           // +0x1210 L"common.dll"
        wchar_t AdditionalFiles[(0x1c50 - 0x1418) / 2]; // +0x1418 G_CrashReportFiles
    };

    static_assert(sizeof(FDwSharedMem) == 0x1c50, "dw15 shared block 0x1c50");
    static_assert(offsetof(FDwSharedMem, EventDone) == 0x14, "EventDone +0x14");
    static_assert(offsetof(FDwSharedMem, EventAlive) == 0x1c, "EventAlive +0x1c");
    static_assert(offsetof(FDwSharedMem, Mutex) == 0x20, "Mutex +0x20");
    static_assert(offsetof(FDwSharedMem, Process) == 0x24, "Process +0x24");
    static_assert(offsetof(FDwSharedMem, BehaviorFlags) == 0x28, "BehaviorFlags +0x28");
    static_assert(offsetof(FDwSharedMem, Result) == 0x2c, "Result +0x2c");
    static_assert(offsetof(FDwSharedMem, OfferFlags) == 0x34, "OfferFlags +0x34");
    static_assert(offsetof(FDwSharedMem, LetRunFlags) == 0x3c, "LetRunFlags +0x3c");
    static_assert(offsetof(FDwSharedMem, FormalAppName) == 0x48, "FormalAppName +0x48");
    static_assert(offsetof(FDwSharedMem, ModuleFileName) == 0xb8, "ModuleFileName +0xb8");
    static_assert(offsetof(FDwSharedMem, PidRegKey) == 0x1040, "PidRegKey +0x1040");
    static_assert(offsetof(FDwSharedMem, LcidUI) == 0x1144, "LcidUI +0x1144");
    static_assert(offsetof(FDwSharedMem, RegSubPath) == 0x1148, "RegSubPath +0x1148");
    static_assert(offsetof(FDwSharedMem, DotDataDlls) == 0x1210, "DotDataDlls +0x1210");
    static_assert(offsetof(FDwSharedMem, AdditionalFiles) == 0x1418, "AdditionalFiles +0x1418");

    // FUN_005a5390: run "dxdiag.exe <temp>\dxdiag.txt", wait up to 60 s
    // and, unless it timed out, append the report path to G_CrashReportFiles.
    void AppendDxDiagReport()
    {
        STARTUPINFOA StartupInfo;
        memset(&StartupInfo, 0, sizeof(StartupInfo));
        PROCESS_INFORMATION ProcessInfo;
        ProcessInfo.hProcess = nullptr;
        ProcessInfo.hThread = nullptr;
        ProcessInfo.dwProcessId = 0;
        StartupInfo.cb = 0x44;
        ProcessInfo.dwThreadId = 0;

        char ReportPath[0x104];
        GetTempPathA(0x104, ReportPath);
        strcat(ReportPath, "dxdiag.txt");

        char CommandLine[0x104];
        wsprintfA(CommandLine, "dxdiag.exe %s", ReportPath);
        if (CreateProcessA(nullptr, CommandLine, nullptr, nullptr, FALSE, 0x4000020, nullptr,
                           nullptr, &StartupInfo, &ProcessInfo) == 0)
            return;

        if (WaitForSingleObject(ProcessInfo.hProcess, 60000) != WAIT_TIMEOUT)
        {
            if (wcslen(G_CrashReportFiles) != 0)
                wcscat(G_CrashReportFiles, L"|");
            const size_t Length = wcslen(G_CrashReportFiles);
            MultiByteToWideChar(CP_UTF8, 0, ReportPath, -1, G_CrashReportFiles + Length,
                                static_cast<int>(0x410 - Length));
            // Retail's terminator lands 0x104 wchar past the old end, not after the
            // appended text.
            G_CrashReportFiles[Length + 0x104] = L'\0';
        }
        CloseHandle(ProcessInfo.hProcess);
        CloseHandle(ProcessInfo.hThread);
    }

    // FUN_005a54e0, the unhandled-exception filter FUN_005a5910
    // installs. Breakpoint (0x80000003) and single-step (0x80000004) are left to the next handler.
    // Everything else: close a hosted DirectPlay session, then hand the crash to Dr. Watson
    // (dw15.exe, shipped next to freelancer.exe) through a 0x1c50-byte shared block and wait for
    // it. Returns EXCEPTION_EXECUTE_HANDLER unless dw15 says the process may continue.
    LONG WINAPI ClientCrashFilter(EXCEPTION_POINTERS* Info)
    {
        LONG Result = EXCEPTION_EXECUTE_HANDLER;
        const DWORD Code = Info->ExceptionRecord->ExceptionCode;
        if (Code == EXCEPTION_BREAKPOINT || Code == EXCEPTION_SINGLE_STEP)
            return EXCEPTION_CONTINUE_SEARCH;

        // Guarded by its own __try with a filter that returns 1.
        __try
        {
            CDPServer::CrashCleanup();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        SECURITY_ATTRIBUTES Security;
        Security.lpSecurityDescriptor = nullptr;
        Security.nLength = 0xc;
        Security.bInheritHandle = TRUE;
        HANDLE Mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, &Security, PAGE_READWRITE, 0,
                                            0x1c50, nullptr);
        if (Mapping == nullptr)
            return Result;

        FDwSharedMem* const Block =
            static_cast<FDwSharedMem*>(MapViewOfFile(Mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0,
                                                     0, 0));
        if (Block != nullptr)
        {
            memset(Block, 0, 0x714 * 4);
            HANDLE const EventDone = CreateEventA(&Security, FALSE, FALSE, nullptr);
            if (EventDone != nullptr)
            {
                HANDLE const EventAlive = CreateEventA(&Security, FALSE, FALSE, nullptr);
                if (EventAlive != nullptr)
                {
                    HANDLE const Mutex = CreateMutexA(&Security, FALSE, nullptr);
                    if (Mutex != nullptr)
                    {
                        if (DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
                                            GetCurrentProcess(),
                                            &Block->Process, 0x1f0fff,
                                            TRUE, 0) != 0)
                        {
                            Block->ProcessId = GetCurrentProcessId();
                            Block->ThreadId = GetCurrentThreadId();
                            Block->EventAlive = EventDone;
                            Block->EventDone = EventAlive;
                            Block->Mutex = Mutex;
                            Block->Size = 0x1c50;
                            Block->ExceptionPointers = Info;
                            Block->ExceptionAddress =
                                Info->ExceptionRecord->ExceptionAddress;
                            Block->OfferFlags = 1;
                            Block->LetRunFlags = 0x10;
                            wcscpy(Block->FormalAppName, G_CrashAppName);
                            strcpy(Block->RegSubPath,
                                   "Microsoft\\PCHealth\\ErrorReporting\\DW");
                            strcpy(Block->PidRegKey,
                                   "HKLM\\Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0\\"
                                   "DigitalProductID");

                            HKEY Key;
                            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                              "Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0",
                                              0, KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS)
                            {
                                DWORD LangId;
                                DWORD Size = 4;
                                if (RegQueryValueExA(Key, "LangID", nullptr, nullptr,
                                                     reinterpret_cast<BYTE*>(&LangId),
                                                     &Size) == ERROR_SUCCESS)
                                    Block->LcidUI = LangId + 0x400;
                                RegCloseKey(Key);
                            }

                            Block->BehaviorFlags = 0x100;
                            memcpy(Block->DotDataDlls, L"common.dll", 22);

                            char ModulePath[0x104];
                            GetModuleFileNameA(nullptr, ModulePath, 0x104);
                            MultiByteToWideChar(CP_UTF8, 0, ModulePath, -1,
                                                Block->ModuleFileName, 0x104);

                            AppendDxDiagReport();
                            memcpy(Block->AdditionalFiles, G_CrashReportFiles,
                                   wcslen(G_CrashReportFiles) * 2 + 2);

                            ErrorHandler::deactivate();

                            STARTUPINFOA StartupInfo;
                            memset(&StartupInfo, 0, sizeof(StartupInfo));
                            StartupInfo.cb = 0x44;
                            PROCESS_INFORMATION ProcessInfo;
                            ProcessInfo.hProcess = nullptr;
                            ProcessInfo.hThread = nullptr;
                            ProcessInfo.dwProcessId = 0;
                            ProcessInfo.dwThreadId = 0;

                            char Arguments[0x104];
                            wsprintfA(Arguments, " -x -s %u", reinterpret_cast<UINT_PTR>(Mapping));
                            if (CreateProcessA("dw15.exe", Arguments, nullptr, nullptr, TRUE,
                                               CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                                               nullptr, nullptr, &StartupInfo,
                                               &ProcessInfo) == 0)
                            {
                                Result = EXCEPTION_CONTINUE_SEARCH;
                            }
                            else
                            {
                                bool bWaiting = true;
                                do
                                {
                                    if (WaitForSingleObject(EventDone, 20000) == WAIT_OBJECT_0)
                                    {
                                        if (WaitForSingleObject(EventAlive, 1) == WAIT_OBJECT_0)
                                        {
                                            bWaiting = false;
                                            if (Block->Result == 0x10)
                                                Result = EXCEPTION_CONTINUE_SEARCH;
                                        }
                                    }
                                    else
                                    {
                                        const DWORD MutexWait = WaitForSingleObject(Mutex, 20000);
                                        if (MutexWait == WAIT_TIMEOUT)
                                        {
                                            bWaiting = false;
                                        }
                                        else if (MutexWait == WAIT_ABANDONED)
                                        {
                                            bWaiting = false;
                                            ReleaseMutex(Mutex);
                                        }
                                        else
                                        {
                                            if (WaitForSingleObject(EventDone, 1) != WAIT_OBJECT_0)
                                            {
                                                SetEvent(EventAlive);
                                                bWaiting = false;
                                            }
                                            else if (WaitForSingleObject(EventAlive, 1) ==
                                                     WAIT_OBJECT_0)
                                            {
                                                bWaiting = false;
                                            }
                                            ReleaseMutex(Mutex);
                                        }
                                    }
                                } while (bWaiting);
                            }
                        }
                        CloseHandle(Mutex);
                    }
                    CloseHandle(EventAlive);
                }
                CloseHandle(EventDone);
            }
            UnmapViewOfFile(Block);
        }
        CloseHandle(Mapping);
        return Result;
    }

    // FUN_005a5910.
    void InstallClientCrashFilter(const wchar_t* AppName)
    {
        wcsncpy(G_CrashAppName, AppName, 0x80);
        G_CrashAppName[0x7f] = L'\0';
        SetUnhandledExceptionFilter(ClientCrashFilter);
    }

    // DAT_005b08a0 (code Ghidra left as data) -- installed into DALib::FatalErrorFn by WinMain.
    // Loads caption 0xd1 and the message Id from the resources, appends Text, drops the render
    // device (FUN_004247c0(1)), forces the cursor visible, shows the message box and exits.
    void __cdecl ClientFatalError(unsigned int Id, const char* Text)
    {
        wchar_t Caption[0x100];
        if (GetIdsStringWWithRawFallback(0xd1, Caption, 0x100) == 0)
            wcscpy(Caption, L"Freelancer Fatal Error");

        wchar_t Message[0x1000];
        if (GetIdsStringWWithRawFallback(Id, Message, 0x1000) == 0)
            wcscpy(Message, L"Freelancer has encountered a fatal error and will now terminate.\n");

        if (Text != nullptr)
        {
            wcscat(Message, L"\n\n");
            const size_t Length = wcslen(Message);
            MultiByteToWideChar(CP_ACP, 0, Text, -1, Message + Length,
                                static_cast<int>(0x1000 - Length));
        }

        ShutdownRenderMgr(true);
        while (ShowCursor(TRUE) < 0)
        {
        }
        MessageBoxW(g_client.window, Message, Caption, 0x10030);
        exit(1);
    }

    // LAB_005b09b0 -- installed into DALib::CriticalWarningFn. Same text assembly
    // with caption 0xd2; a Yes/No box. "No" drops the render device and exits; "Yes" puts the
    // cursor count back and returns true.
    bool __cdecl ClientCriticalWarning(unsigned int Id, const char* Text)
    {
        wchar_t Caption[0x100];
        if (GetIdsStringWWithRawFallback(0xd2, Caption, 0x100) == 0)
            wcscpy(Caption, L"Freelancer Critical Warning");

        wchar_t Message[0x1000];
        if (GetIdsStringWWithRawFallback(Id, Message, 0x1000) == 0)
            wcscpy(Message, L"Freelancer has encountered an unknown issue.\n");

        if (Text != nullptr)
        {
            wcscat(Message, L"\n\n");
            const size_t Length = wcslen(Message);
            MultiByteToWideChar(CP_ACP, 0, Text, -1, Message + Length,
                                static_cast<int>(0x1000 - Length));
        }

        int ExtraShows = 0;
        if (ShowCursor(TRUE) < 0)
        {
            do
            {
                ++ExtraShows;
            } while (ShowCursor(TRUE) < 0);
        }

        const bool bContinue = MessageBoxW(g_client.window, Message, Caption, 0x10024) == IDYES;
        if (!bContinue)
        {
            ShutdownRenderMgr(true);
            exit(1);
        }

        if (ExtraShows >= 0)
        {
            for (int Count = ExtraShows + 1; Count != 0; --Count)
                ShowCursor(FALSE);
        }
        return bContinue;
    }

    // .data:00615bb0 / .data:00615bbc -- writable char arrays in retail (only FUN_005b2d60
    // reads them), handed to EBUEula as-is.
    char G_EulaFile[] = "..\\EULA.rtf";
    char G_WarrantyFile[] = "..\\Warranty.rtf";

    // EBUEula.dll's "EBUEula" export as FUN_005b2d60 calls it: cdecl, four arguments, an int
    // result tested for non-zero. EBUEula.dll is the retail third-party binary shipped in bin\
    // (not Digital Anvil code, not ported).
    typedef int(__cdecl* FEBUEulaFn)(const char* RegKey, char* EulaFile, char* WarrantyFile,
                                     int Arg4);

    // The failure tail FUN_005b2d60 carries twice (after both LoadLibraryA calls fail, and after
    // GetProcAddress fails): caption ids 0xd1 and message ids 0xd9 with the built-in fallbacks,
    // the render device dropped, the cursor forced visible, the message box, exit(1). No text is
    // appended. Retail reaches GetProcAddress / MessageBoxW through lazy import thunks
    // FUN_005b74fe / FUN_005b75b7 -- the same APIs.
    __declspec(noreturn) void EulaLoadFailureExit()
    {
        wchar_t Caption[0x100];
        if (GetIdsStringWWithRawFallback(0xd1, Caption, 0x100) == 0)
            wcscpy(Caption, L"Freelancer Fatal Error");

        wchar_t Message[0x1000];
        if (GetIdsStringWWithRawFallback(0xd9, Message, 0x1000) == 0)
            wcscpy(Message, L"Freelancer has encountered a fatal error and will now terminate.\n");

        ShutdownRenderMgr(true);
        while (ShowCursor(TRUE) < 0)
        {
        }
        MessageBoxW(g_client.window, Message, Caption, 0x10030);
        exit(1);
    }

    // FUN_005b2d60: the EULA gate WinMain runs after the ..\DATA probe. False (EBUEula returned
    // 0) makes WinMain return 1.
    bool RunEulaGate()
    {
        HMODULE Eula = LoadLibraryA("..\\EBUEula.dll");
        if (Eula == nullptr)
        {
            Eula = LoadLibraryA("EBUEula.dll");
            if (Eula == nullptr)
                EulaLoadFailureExit();
        }

        FEBUEulaFn const EulaFn = reinterpret_cast<FEBUEulaFn>(GetProcAddress(Eula, "EBUEula"));
        if (EulaFn == nullptr)
        {
            FreeLibrary(Eula);
            EulaLoadFailureExit();
        }

        char* const Warranty =
            (GetFileAttributesA(G_WarrantyFile) != INVALID_FILE_ATTRIBUTES) ? G_WarrantyFile : nullptr;

        // The registry key is .rdata:005e1f64.
        int const Result =
            EulaFn("Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0", G_EulaFile, Warranty, 1);

        FreeLibrary(Eula);
        return Result != 0;
    }

    // FUN_005b0b00 -- the assert callback WinMain hands ErrorHandler::activate. In a fullscreen
    // session (DAT_00679be5) with a registered Win3D (FUN_0059da50) the message goes to the
    // NN_Error modal FUN_00473e00 and the result is 4 + (its bool); otherwise MessageBoxA.
    int __cdecl ClientAssertHandler(const char* Message, const char* Caption, bool bOkCancel)
    {
        if (g_display.fullscreen != 0 && Win3DGetFirstRegistered() != nullptr)
            return NnErrorRunModalMessage(Message) ? 5 : 4;
        const UINT Flags = (bOkCancel ? 2u : 0x12u) | 2u;
        return MessageBoxA(nullptr, Message, Caption, Flags);
    }

    // FUN_005b2cb0, cdecl(FClientCommandLine*): "+G"/"-G", "+P<port>"
    // and "+S<host:port>" in either case, the sign ignored. A token without a sign matches
    // nothing.
    void ApplyClientCommandLineFlags(const FClientCommandLine* CommandLine)
    {
        for (unsigned Index = 0; Index < CommandLine->Count; ++Index)
        {
            const char* Token = CommandLine->Argv[Index];
            char Letter = 0;
            if (Token[0] == '+' || Token[0] == '-')
            {
                Letter = static_cast<char>(toupper(Token[1]));
                Token += 2;
            }

            if (Letter == 'G' || Letter == 'g')
            {
                G_bCommandLineG = true;
            }
            else if (Letter == 'P' || Letter == 'p')
            {
                const int Port = MsvcrtAtoi(Token);
                if (Port != 0)
                    CDPClient::SetSourcePort(static_cast<unsigned long>(Port));
            }
            else if (Letter == 'S' || Letter == 's')
            {
                // FUN_005ac710 on the connect context DAT_0067e7b8; its answer is not read.
                G_LoginMgr.AddDirectServer(Token);
            }
        }
    }

    // WinMain's exception filter: FUN_004247c0(1), ShowCursor(TRUE), EXCEPTION_CONTINUE_SEARCH.
    int ClientWinMainFilter()
    {
        ShutdownRenderMgr(true);
        ShowCursor(TRUE);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // LAB_005b2190 -- registered with atexit right after CoInitialize, so it runs after WinMain
    // has returned. Calls are in retail's order.
    void __cdecl ClientAtExitTeardown()
    {
        CGunWrapper::Shutdown();
        // FUN_005791c0 -- delete the loading screen's Image (DAT_0067c3bc) and its progress bar
        // tree (DAT_0067c3c0), set DAT_0067c3db.
        LoadingScreenShutdown();
        Async::Shutdown();
        if (g_client.current_state != nullptr)
        {
            g_client.current_state->Destroy();
            g_client.current_state = nullptr;
        }
        ShutdownServerConnection(); // FUN_005a8260
        G_LoginMgr.Reset();         // FUN_005a9300 on DAT_0067e7b8
        G_LoginMgr.DestroyGun();    // FUN_005abc60 on DAT_0067e7b8
        // Slot 9 of each singleton: IntroFocus's is 0x00401e90 (empty); GF's LAB_00437290 ->
        // FUN_00558870, the space state's FUN_0054ab20, MissionBriefing's LAB_0043d260.
        GetIntroFocusState()->Shutdown();
        GetGameflowState()->Shutdown();
        GetSpaceState()->Shutdown();
        GetMissionBriefingState()->Shutdown();
        ShutdownCharacterInfo();    // FUN_00435500
        // The ToolTip singleton DAT_0067244c -- slot 10 (Destroy), then, re-read and still set,
        // the scalar deleting dtor (FUN_004bd690 clears DAT_0067244c when it is this tooltip).
        if (GetNnToolTipSingleton() != nullptr)
        {
            GetNnToolTipSingleton()->Destroy();
            NnToolTipControl* const Tip = GetNnToolTipSingleton();
            if (Tip != nullptr)
                delete Tip;
        }
        NnRolloverDestroyGlobal();  // FUN_004b6580
        ChatShutdown();            // FUN_00469b30
        NnSystemShutdown();         // FUN_0045a140
        ClearButtonMontage();       // FUN_00559d20
        // The DAT_0067ecb4 tree (FUN_0043d2a0 inlined). The root is always null in retail
        // (globals.h).
        Win3DDeleteTree(G_WinDebugRoot);
        Win3DShutdownManager(); // FUN_0059d6d0
        ShutdownUiSoundTable(); // FUN_00564540
        StopAllClientSounds();  // FUN_004284f0
        DataConfig::shutdown();
        GraphLibraryShutdown(); // FUN_00419e50
        EffectSystemShutdown();   // FUN_004f7140
        ClearEffectShapesFiles(); // FUN_004f73b0
        UiScrollClearShapes(); // FUN_00595400
        ShutdownClientTickProfile(); // CLIENT-BETA-LABEL: release cached atlas owners first
        FontShutdown(); // FUN_004174a0
        ShutdownRichText();
        ShutdownDebrisArchDb(); // FUN_00530fa0
        EffectArchShutdown();      // FUN_0040ef30
        ClearClientVoices();       // FUN_00431850
        ShutdownClientSoundArch(); // FUN_0042b4d0
        ShutdownRenderMgr(false);  // FUN_004247c0(0)
#ifdef FREELANCER_RP9
        // Port-only: the Direct3D 9 extension objects and interfaces go before the pipeline.
        RenderEx::Shutdown();
#endif
        // Retail FUN_005b4050 frees the PerfOptions std::list (DAT_0067ed04) here. The port's
        // registry is the static G_Options table (perf_options.cpp), so there is nothing to free.
        if (Reputation::Vibe::Count() != 0)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %d REP_INSTANCE(s) orphaned\n",
                  "E:\\FL\\Scratch\\Source\\Client\\main.cpp", 0x4be, Reputation::Vibe::Count());
        }
        Archetype::FlushMaterialCache(); // FUN_0041b150, result discarded
        AleLibShutdown();                // FUN_00401380 (ALELib::Shutdown)
        DALib::CloseData();
        DALib::Shutdown(g_client.window);
        G_LoginMgr.Cleanup(); // CDPClient::Cleanup on DAT_0067e7bc
        FreeLibrary(g_client.resources);
        g_client.resources = nullptr;
        ErrorHandler::deactivate();
        CFLIDMaker::Shutdown();
        StringShutdown();
        ReleaseClientIMEFocus(); // FUN_005641d0: delete DAT_0067a4a4
        CoUninitialize();
    }
}

// FUN_005b33f0.
FClientCommandLine::FClientCommandLine()
{
    Count = 0;
}

// FUN_005b3400.
void FClientCommandLine::Parse(const char* CommandLine)
{
    static const char kDelimiters[] = " \t"; // .rdata:005e6dec

    Count = 0;
    strcpy(Buffer, CommandLine);

    char* Cursor = Buffer;
    for (;;)
    {
        Cursor += strspn(Cursor, kDelimiters);
        char* const Token = Cursor;
        unsigned Length = 0;
        char Ch = *Cursor;
        while (Ch != '\0')
        {
            ++Cursor;
            if (strchr(kDelimiters, Ch) != nullptr)
                break;
            if (Ch == '"')
            {
                // Copy up to the closing quote; a missing one ends the token (and the scan).
                char Inner = *Cursor;
                if (Inner == '\0')
                    break;
                bool bClosed = false;
                do
                {
                    ++Cursor;
                    if (Inner == '"')
                    {
                        bClosed = true;
                        break;
                    }
                    Token[Length] = Inner;
                    ++Length;
                    Inner = *Cursor;
                } while (Inner != '\0');
                if (!bClosed)
                    break;
            }
            else
            {
                Token[Length] = Ch;
                ++Length;
            }
            Ch = *Cursor;
        }

        Token[Length] = '\0';
        if (Token[0] == '\0')
            return;
        Argv[Count] = Token;
        ++Count;
    }
}

// FUN_005b27c0. Enabling unmasks FP exceptions (crash on NaN/overflow/etc.).
void SetFpuExceptionTrap(bool Enable)
{
    if (Enable)
    {
        _clearfp();
        const unsigned int Prev = _controlfp(0, 0);
        _control87(Prev & ~static_cast<unsigned>(_EM_INVALID | _EM_ZERODIVIDE | _EM_OVERFLOW), _MCW_EM);
    }
    else
    {
        _control87(0x9001f, _MCW_EM);
    }
}

// FUN_005b2800.
void OpenSpewLog()
{
    char Path[0x104];
    if (!GetUserLocalDataPath(Path))
        return;
    strcat(Path, "\\FLSpew.txt");
    if (ErrorHandler::new_log(Path, -1, 7, false, false) != nullptr)
        MultiByteToWideChar(CP_UTF8, 0, Path, -1, G_CrashReportFiles, 0x104);
}

// FUN_005b2f50 -- Client/main.cpp WinMain.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE /*prev*/, LPSTR cmd, int /*show*/)
{
    int ExitCode = 1;

    // The whole body runs under a frame handler whose filter drops the render device and shows
    // the cursor, then lets the exception go on.
    __try
    {
        // The retail single-instance guard. DEVIATION (deviations.md "Client: single-instance
        // guard behind FL_SINGLE_INSTANCE"): compiled only with FL_SINGLE_INSTANCE defined, so the
        // owner can run several clients on one machine for multiplayer tests.
#if defined(FL_SINGLE_INSTANCE)
        HANDLE mutex = CreateMutexA(nullptr, FALSE, "FreelancerClient");
        if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(mutex);
            return 1;
        }
#endif

        char ModulePath[MAX_PATH];
        GetModuleFileNameA(nullptr, ModulePath, MAX_PATH);
        char Drive[_MAX_DRIVE];
        char Dir[_MAX_DIR];
        _splitpath(ModulePath, Drive, Dir, nullptr, nullptr);
        char Folder[MAX_PATH];
        _makepath(Folder, Drive, Dir, nullptr, nullptr);
        SetCurrentDirectoryA(Folder);

        // Retail probes ..\DATA after the chdir above (DebugASan runs from binasan\, a sibling of bin\).
        if (_access("..\\DATA\\CHARACTERS\\ANIMATIONS\\FACIALMALE.ANM", 0) != 0)
        {
            STARTUPINFOA StartupInfo;
            memset(&StartupInfo, 0, sizeof(StartupInfo));
            StartupInfo.cb = 0x44;
            PROCESS_INFORMATION ProcessInfo;
            memset(&ProcessInfo, 0, sizeof(ProcessInfo));
            if (CreateProcessA("FLServer.exe", nullptr, nullptr, nullptr, FALSE, 0x20, nullptr,
                               nullptr, &StartupInfo, &ProcessInfo))
            {
                CloseHandle(ProcessInfo.hProcess);
                CloseHandle(ProcessInfo.hThread);
            }
            return 1;
        }

        if (!RunEulaGate())
            return 1;

        // Global DALib data exports ?FatalErrorFn@@3P6AXIPBD@ZA / ?CriticalWarningFn@@3P6A_NIPBD@ZA
        // (not in namespace DALib).
        ::FatalErrorFn = ClientFatalError;
        ::CriticalWarningFn = ClientCriticalWarning;
        CoInitialize(nullptr);
        atexit(ClientAtExitTeardown);
        g_client.instance = instance;
        g_client.resources = LoadLibraryA("resources.dll");
        RegisterIdsResourceModule(g_client.resources); // FUN_0057d800

        unsigned int Major;
        unsigned int Minor;
        unsigned int Build;
        if (DACOM_GetDllVersion(ModulePath, &Major, &Minor, &Build) == 0)
        {
            InstallClientCrashFilter(L"Freelancer");
        }
        else
        {
            Major = 1;
            Minor = 0;
            Build = 0xb;
        }
        g_client.packed_version = (((Minor & 0xFFu) | (Major << 8)) << 16) | (Build & 0xFFFFu); // FUN_005b34b0

        FClientCommandLine CommandLine;
        CommandLine.Parse(cmd);
        ApplyClientCommandLineFlags(&CommandLine);
        g_display.LoadStartupConfig(&CommandLine);
        ErrorHandler::activate(g_display.ini_path.c_str(), ClientAssertHandler);
        OpenSpewLog();
        // FUN_005a82d0(&CommandLine, ini path): [Freelancer] local_server / AppGUID, [ListServer]
        // hostname / port and [Login] name / pwd / char into the connect context. The command
        // line argument is not read.
        G_LoginMgr.LoadConfig(g_display.ini_path.c_str());
        if (G_bFpExceptionTrap)
            SetFpuExceptionTrap(true);

        WNDCLASSA WindowClass;
        char ClassName[] = "Freelancer";
        WindowClass.style = CS_DBLCLKS;
        WindowClass.lpfnWndProc = ClientWndProc;
        WindowClass.cbClsExtra = 0;
        WindowClass.cbWndExtra = 0;
        WindowClass.hInstance = instance;
        WindowClass.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(0x67));
        WindowClass.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        WindowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        WindowClass.lpszMenuName = nullptr;
        WindowClass.lpszClassName = ClassName;
        RegisterClassA(&WindowClass);

        g_client.window = CreateGameWindow(ClassName, &g_display, "Freelancer");
        ShowCursor(FALSE);

        if (InitializeEngine(&CommandLine))
        {
            ClientNoteEngineReady(); // DAT_0067ecc8 = 1; FUN_00471f30(0) if a close came early

            // FUN_005b2a30(FUN_005b20a0(), nullptr).
            g_client.current_state = EnterClientState(ComputeInitialEnvironment(), nullptr);

            RegisterAcceleratorGroup("IDR_ALWAYS_PRESENT"); // FUN_00577490

            for (;;)
            {
                UpdateClientTiming();                              // FUN_005b2360
                const double DeltaSeconds = GetClientFrameTimeCapped(); // FUN_0042d680
                if (!PumpMessages(&ExitCode))                      // FUN_005b0b60
                    break;
                if (ClientPresentAllowed())                        // DAT_00615ba8
                    TickClientFrame(DeltaSeconds);                 // FUN_005b2890
            }
        }
    }
    __except (ClientWinMainFilter())
    {
        // Unreachable: the filter never accepts.
    }

    // Retail WinMain returns straight away; the mutex is left to process exit and the teardown
    // runs from atexit (ClientAtExitTeardown).
    return ExitCode;
}
