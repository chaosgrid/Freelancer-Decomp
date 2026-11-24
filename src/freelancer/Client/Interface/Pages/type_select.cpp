// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/Pages/type_select.h"

#include "Core/globals.h"
#include "Common/Fonts/ids_rdl.h"      // LoadIdsIntoRdl (FUN_0057da40), RdlRemoveLastParagraph (FUN_0057eb40)
#include "Network/login_mgr.h"
#include "Interface/menu_ui_init.h"
#include "NeuroNet/nn_prompt.h"    // NnPromptRunYesNo, NnPromptRunChoice

#include "common_errorhandler.h" // ErrorHandler::log_last_error, SeverityWarning
#include "common_text.h"  // RenderDisplayList

#include <mmsystem.h>
#include <string.h>

namespace
{
    // DAT_0067c210 -- set once the multiplayer notice has been accepted.
    unsigned char G_bMultiplayerNoticeAccepted = 0;

    // .rdata 0x005e2724 and the WARNING format (0x005e27a0) every ErrorHandler::log_last_error
    // call of Intro.cpp passes.
    char const kIntroSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\Intro.cpp";
    char G_IntroLastErrorFormat[] = "%s(%d) : WARNING:LastError:%s";

    // DAT_00614ae0 -- 01 in the PE image and never written: the IsNetworkAlive call is always
    // skipped.
    unsigned char const G_bSkipNetworkAliveProbe = 1;

    // DLLVERSIONINFO as the inlined DllGetVersion probe fills it (cbSize = 0x14).
    struct FDllVersionInfo
    {
        DWORD Size;
        DWORD Major;
        DWORD Minor;
        DWORD Build;
        DWORD PlatformId;
    };
    typedef HRESULT(CALLBACK* FDllGetVersionFn)(FDllVersionInfo* Info);

    float const kSlideX = -0.455000013f;  // 0xbee8f5c3
    float const kColumnTopY = 0.172499999f; // DAT_005e28c0
    float const kColumnStepY = 0.0822000017f; // DAT_005e28c4
    float const kColumnZ = -1.0f;           // DAT_005c7500

    // Button i's slide position for the current AnimTime; retail inlines it in both the
    // slide-in and the slide-out.
    void PlaceColumnButton(TypeSelect* Self, unsigned Index)
    {
        int const Count = 6 - static_cast<int>(Index);
        double const Offset = static_cast<double>(Count) * 0.1; // DAT_005c7720
        double const SlideX = Offset + Self->AnimTime - 0.45500001311302185; // DAT_005e28d0
        float Position[3];
        Position[1] = static_cast<float>(static_cast<double>(kColumnTopY) -
                                         static_cast<double>(Index) *
                                             static_cast<double>(kColumnStepY));
        Position[2] = kColumnZ;
        float StaticOffset[3] = {0.0f, 0.0f, 0.0f};
        // DAT_005e28c8: at or left of -0.455 (or unordered) the button follows the slide and
        // the mesh is pushed by Offset + AnimTime; right of it the button rests at -0.455 and
        // the mesh at its origin.
        if (SlideX > -0.45500001311302185)
        {
            Position[0] = kSlideX;
        }
        else
        {
            Position[0] = static_cast<float>(SlideX);
            StaticOffset[0] = static_cast<float>(Offset + Self->AnimTime);
        }
        Self->Buttons[Index].SetPosition(Position);
        Self->Statics[Index]->SetPosition(StaticOffset);
    }

    TypeSelect G_TypeSelect; // DAT_0067b780
}

// ---------------------------------------------------------------------------------------
// TextMsgButton
// ---------------------------------------------------------------------------------------

TextMsgButton::TextMsgButton()
    : Command(0)
{
    // FUN_00566380.
    bEnabled = 1;
}

TextMsgButton::~TextMsgButton()
{
}

void TextMsgButton::MouseEnter(float const* /*Ray*/)
{
    // FUN_005664e0.
    if (bEnabled != 0)
        PlayUiRolloverSound();
}

bool TextMsgButton::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_005664a0.
    if (Event->Action != kWin3DMousePress)
        return false;
    if (bEnabled == 0)
        return false;
    if (Event->Button != kWin3DMouseLeft)
        return false;
    if (Parent == nullptr)
        return false;
    return Parent->ProcessMessage(0x3004, static_cast<unsigned>(Command), 0);
}

void TextMsgButton::Init(Win3D* NewParent, wchar_t const* Caption, float BoxWidth,
                         float BoxHeight, int NewCommand)
{
    // FUN_005753d0.
    Command = NewCommand;
    bEnabled = 1;
    Create(NewParent, nullptr); // result not checked
    Width = BoxWidth;
    Height = BoxHeight;
    SetText(Caption);
    SetFont("ButtonText", 1.0f);
}

void TextMsgButton::SetFlagsE4E5(unsigned char A, unsigned char B)
{
    // FUN_005753b0.
    FlagE3 = 1;
    FieldE4 = A;
    FieldE5 = B;
}

// ---------------------------------------------------------------------------------------
// TypeSelect
// ---------------------------------------------------------------------------------------

TypeSelect::TypeSelect()
    : NextState(0)
    , Version(nullptr)
    , SensApi(nullptr)
    , IsNetworkAlive(nullptr)
    , PollIntervalMs(0)
    , LastPollMs(0)
    , Pad55c(0)
    , AnimTime(0.0)
    , PendingCommand(0)
    , bAnimating(0)
    , Phase(0)
{
    // FUN_005662a0 (vftables 0x005e27cc / 0x005e27c4). Retail leaves +0x80, +0x534..+0x548 and
    // +0x554..+0x570 unwritten; the instance is static storage (zero), which the member
    // initialisers reproduce.
    Pad56d[0] = 0;
    Pad56d[1] = 0;
    Pad56d[2] = 0;
    for (unsigned Index = 0; Index < 5; ++Index)
        Statics[Index] = nullptr;
}

TypeSelect::~TypeSelect()
{
    // FUN_005661b0.
    if (SensApi != nullptr)
    {
        if (!FreeLibrary(SensApi))
            ErrorHandler::log_last_error(kIntroSourceFile, 0x17ed, SeverityWarning,
                                         G_IntroLastErrorFormat, GetLastError());
        SensApi = nullptr;
    }
    IsNetworkAlive = nullptr;
}

unsigned char TypeSelect::ProbeMultiplayerAvailable()
{
    // CDPClient::GetClient on G_LoginMgr's CDPClient base (DAT_0067e7bc).
    if (G_LoginMgr.GetClient() == nullptr)
        return 0;
    if (G_bSkipNetworkAliveProbe != 0 || IsNetworkAlive == nullptr)
        return 1;
    DWORD Flags = 0;
    return IsNetworkAlive(&Flags) != 0 ? 1 : 0;
}

bool TypeSelect::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_005668f0.
    AnimTime = 1.0;
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);

    if (SensApi == nullptr)
    {
        // DllGetVersion of shdocvw.dll, inlined.
        DWORD Major;
        DWORD Minor;
        bool bHaveVersion = false;
        if (!IsBadWritePtr(&Major, 4) && !IsBadWritePtr(&Minor, 4))
        {
            Major = 0;
            Minor = 0;
            HMODULE const ShDocVw = LoadLibraryA("shdocvw.dll");
            if (ShDocVw != nullptr)
            {
                HRESULT Result;
                FDllGetVersionFn const GetVersion = reinterpret_cast<FDllGetVersionFn>(
                    GetProcAddress(ShDocVw, "DllGetVersion"));
                if (GetVersion != nullptr)
                {
                    FDllVersionInfo Info;
                    Info.Size = 0x14;
                    Info.Major = 0;
                    Info.Minor = 0;
                    Info.Build = 0;
                    Info.PlatformId = 0;
                    Result = GetVersion(&Info);
                    if (Result >= 0)
                    {
                        Major = Info.Major;
                        Minor = Info.Minor;
                    }
                }
                else
                {
                    Result = static_cast<HRESULT>(0x80004005L); // E_FAIL
                }
                FreeLibrary(ShDocVw);
                // Only E_FAIL itself skips the version test.
                bHaveVersion = Result != static_cast<HRESULT>(0x80004005L);
            }
        }
        else
        {
            // Retail jumps straight to the version test with whatever the locals held; they are
            // stack slots the probe owns, so this branch is never taken.
            Major = 0;
            bHaveVersion = true;
        }
        (void)Minor;

        // Version 5 or later -> sensapi.dll!IsNetworkAlive.
        if (bHaveVersion && Major >= 5)
        {
            SensApi = LoadLibraryA("sensapi.dll");
            if (SensApi != nullptr)
            {
                IsNetworkAlive = reinterpret_cast<FIsNetworkAliveFn>(
                    GetProcAddress(SensApi, "IsNetworkAlive"));
                if (IsNetworkAlive == nullptr)
                {
                    if (!FreeLibrary(SensApi))
                        ErrorHandler::log_last_error(kIntroSourceFile, 0x17d9, SeverityWarning,
                                                     G_IntroLastErrorFormat, GetLastError());
                    IsNetworkAlive = nullptr;
                    SensApi = nullptr;
                }
            }
            else
            {
                IsNetworkAlive = nullptr;
                SensApi = nullptr;
            }
        }
        else
        {
            IsNetworkAlive = nullptr;
            SensApi = nullptr;
        }
    }

    Buttons[2].bEnabled = ProbeMultiplayerAvailable(); // +0x34a, Multiplayer
    PollIntervalMs = 1000;
    LastPollMs = timeGetTime();
    return true;
}

void TypeSelect::Destroy()
{
    // FUN_00566ab0.
    UnregisterAllEvents();
    if (SensApi != nullptr)
    {
        if (!FreeLibrary(SensApi))
            ErrorHandler::log_last_error(kIntroSourceFile, 0x17ed, SeverityWarning,
                                         G_IntroLastErrorFormat, GetLastError());
        SensApi = nullptr;
    }
    IsNetworkAlive = nullptr;
    Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));

    // Every child is destroyed; StaticControls are also deleted. The class name (<= 31 chars) is
    // copied before Destroy and compared afterwards (14 bytes, terminator included).
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        char ClassCopy[32];
        ClassCopy[0] = '\0';
        if (Child->ClassName != nullptr)
        {
            size_t Length = strlen(Child->ClassName);
            if (Length >= 0x20)
                Length = 0x1f;
            memcpy(ClassCopy, Child->ClassName, Length);
            ClassCopy[Length] = '\0';
        }
        Child->Destroy();
        if (memcmp(ClassCopy, "StaticControl", 14) == 0)
            delete Child;
        Child = Next;
    }

    if (Version != nullptr)
        delete Version;
    Version = nullptr;
    Win3DUnregister(this);
}

void TypeSelect::Update()
{
    // FUN_00566510.
    Win3D::Update();

    unsigned const Now = timeGetTime();
    if (Now - LastPollMs > PollIntervalMs)
    {
        Buttons[2].bEnabled = ProbeMultiplayerAvailable();
        LastPollMs = Now;
    }

    // Above 1.0 (or unordered) only a running slide-out continues; otherwise a running
    // slide-out continues and an idle column slides in.
    bool const bSlideIn = (AnimTime <= 1.0) && bAnimating == 0;
    if (!(AnimTime <= 1.0) && bAnimating == 0)
        return;

    if (bSlideIn)
    {
        // dt capped at 0.5 (DAT_005c75e8), times 1.5 (DAT_005d3c48).
        double Dt = GetClientFrameTimeCapped();
        if (Dt > 0.5)
            Dt = 0.5;
        AnimTime = Dt * 1.5 + AnimTime;
        for (unsigned Index = 0; Index < 5; ++Index)
            PlaceColumnButton(this, Index);

        // DAT_005d5078 / DAT_005d5070.
        if (!(AnimTime > -0.80000000000000004))
            return;
        if (AnimTime < -0.10000000000000001)
            PlayUiSoundSlot(0x1b); // "ui_motion_swish"
        return;
    }

    // Slide-out, dt uncapped.
    AnimTime = AnimTime - GetClientFrameTimeCapped() * 1.5;
    for (unsigned Index = 0; Index < 5; ++Index)
        PlaceColumnButton(this, Index);

    // DAT_005e28b8 / DAT_005c8ba8.
    if (AnimTime < 0.0 && AnimTime > -0.40000000000000002)
        PlayUiSoundSlot(0x1b);

    // At or below -0.9 (DAT_005e1b90) the column is hidden; on the second such frame the
    // pending command becomes the next state.
    if (!(AnimTime <= -0.90000000000000002))
        return;
    Phase = Phase + 1;
    for (unsigned Index = 0; Index < 5; ++Index)
    {
        Buttons[Index].Flags =
            static_cast<unsigned char>(Buttons[Index].Flags & ~(kWin3DActive | kWin3DVisible));
        Statics[Index]->Flags =
            static_cast<unsigned char>(Statics[Index]->Flags & ~(kWin3DActive | kWin3DVisible));
    }
    if (Phase >= 2)
    {
        NextState = PendingCommand;
        bAnimating = 0;
    }
}

void TypeSelect::Draw()
{
    // FUN_00566500.
    Win3DDrawChildren(this);
}

bool TypeSelect::ProcessMessage(unsigned Msg, unsigned A, unsigned /*B*/)
{
    // FUN_00566bd0.
    if (Msg != 0x3004)
        return false;
    PlayOrRewindUiSoundSlot(0);

    switch (A)
    {
    case 3:    // Load Game
    case 0xa:  // Options
    case 0x10: // New Game
        // Start the slide-out unless one is running.
        if (bAnimating != 0)
            return false;
        PendingCommand = static_cast<int>(A);
        Phase = 0;
        bAnimating = 1;
        AnimTime = 0.34999999999999998;
        return true;

    case 4:
    {
        // Multiplayer. Until it has been accepted once, the online-play notice (IDS 0x601eb);
        // declined -> true, nothing started, the latch left clear.
        if (G_bMultiplayerNoticeAccepted == 0)
        {
            if (!NnPromptRunYesNo(0x601eb, 0xc9b7, false, 0, 0))
                return true;
        }

        // LAN / INTERNET choice; each caption is trimmed only when its load succeeds.
        G_bMultiplayerNoticeAccepted = 1;
        RenderDisplayList LanCaption;
        RenderDisplayList InternetCaption;
        RenderDisplayList Question;
        if (LoadIdsIntoRdl(0x601ed, LanCaption))
            RdlRemoveLastParagraph(LanCaption);
        if (LoadIdsIntoRdl(0x601ec, InternetCaption))
            RdlRemoveLastParagraph(InternetCaption);
        if (LoadIdsIntoRdl(0x601f0, Question))
            RdlRemoveLastParagraph(Question);

        int Choice = -1;
        if (!NnPromptRunChoice(&Question, 0xc9b7, &LanCaption, &InternetCaption, &Choice,
                               false))
            return true;

        G_bEnumerateLanHosts = 1;
        G_bInternetServerList = 0;
        if (Choice == 1)
            G_bInternetServerList = 1;
        G_LoginMgr.ResetServerLists();

        // The slide-out of case 3, inlined.
        if (bAnimating != 0)
            return false;
        PendingCommand = static_cast<int>(A);
        Phase = 0;
        bAnimating = 1;
        AnimTime = 0.34999999999999998;
        return true;
    }

    case 0xb:
        // NN_Exit.
        RequestClientQuitDialog();
        return true;

    case 0x11:
        NextState = static_cast<int>(A);
        return true;

    default:
        return false;
    }
}

bool TypeSelect::OnEvent(unsigned MsgId)
{
    // FUN_00566fc0: USER_CANCEL (0xf) raises NN_Exit.
    if (MsgId != 0xf)
        return false;
    RequestClientQuitDialog();
    return true;
}

TypeSelect& GetTypeSelect()
{
    return G_TypeSelect;
}
