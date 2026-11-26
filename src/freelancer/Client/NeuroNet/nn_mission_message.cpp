// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_mission_message.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerEquipDescListRetail (FUN_004c4060),
                                    // FindPlayerNonMissionEquipRetail (FUN_004c45d0)
#include "Space/client_gameobj.h"         // FClientGameobj
#include "Space/client_objdb.h"           // ClientObjDb_FindObject (FUN_005416c0)
#include "Core/globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_target.h"             // GetShipPilotName (FUN_005472d0), FHudWideText
#include "Common/Fonts/ids_rdl.h"                // RdlAppendNode, AppendTextMaybeXmlRdl (FUN_0057e2c0)
#include "Common/input_manager.h"          // Get/SetClientMouseCursorDrawFlag, MouseSetCursor
#include "Network/login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "Core/main_command_handler.h"   // SetClientQuitRequested (FUN_005b2c50)
#include "Common/misc_util.h"              // FmtStrSprintf (FUN_0041c970)
#include "GameFlow/nav_bar.h"                // NavBarCloseService (FUN_004424f0), NavBarHasServiceWindow (FUN_00442720)
#include "NeuroNet/nn_inventory.h"           // IsPlayerDockedRetail (FUN_004c4810)
#include "NeuroNet/nn_main_button.h"         // NN_MainButton::ResetPanel (FUN_0045cef0)
#include "NeuroNet/nn_modal.h"               // G_NnMissionMessage, NnMissionMessageDestroy, G_bNnScreenUp,
                                    // NnModalPauseGame / ResumeGame, NnMainButtonOnModalOpen,
                                    // NnMainButtonHasActivePanel, NnFloatBits
#include "NeuroNet/nn_text_scratch.h"        // G_NnTextScratchB (DAT_0066fc60), DAT_006119f8
#include "Space/player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "Interface/rich_text_win.h"
#include "common_server.h"             // EPopUpButton
#include "Space/star_sys.h"               // GetAttitudeClass (FUN_0045a490)
#include "Interface/win3d.h"
#include "Interface/win_factory.h"
#include "NeuroNet/Chat/chat_init.h"        // EChatTextStyle

#include "common_cobject.h"         // CObject::ObjectClass
#include "common_goods.h"           // Arch2Good / Good2Arch, EquipDesc, CBase
#include "common_iface.h"           // IObjRW, IObjAffect::slot_38
#include "common_server.h"          // IServer::PopUpDialog (slot 74)

#include <list>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>                // CharUpperW (FUN_005b74d9)

// ---------------------------------------------------------------------------------------------
// The builders' state. Only retail NN_Messaging.cpp references these.

namespace
{
    unsigned G_NnMissionMessageGood = 0;      // DAT_00671f74 -- the scanned good (kind 4)
    unsigned G_NnMissionMessageScanner = 0;   // DAT_00671f78 -- the scanning object's id
    unsigned G_NnMissionMessageButtons = 0;   // DAT_00671f7c -- kind 6: bit 0 yes, 1 no,
                                              // 2 later, 3 ok
    int G_NnMissionMessageKind = 0;           // DAT_00671ff8 -- 4 or 6, read by Create

    // DAT_00671f80 (static init LAB_0048a0c0, atexit LAB_0048a120 ~FmtStr) -- kind 6's title.
    FmtStr G_NnMissionMessageTitle;
    // DAT_00671f98 (static init LAB_0048a050, atexit LAB_0048a0b0 ~FmtStr) -- the text.
    FmtStr G_NnMissionMessageText;

    int const kKindScanNotice = 4;
    int const kKindPopUp = 6;

    char const kFrameMesh[] = "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp"; // 005ce994
    char const kCloseMesh[] = "Interface\\NeuroNet\\Inventory\\x.3db";              // 005d01d0
    char const kSelectSound[] = "ui_select_item";                                   // 005ced30
    char const kButtonTextStyle[] = "ButtonText";                                   // 005d0410
}

// LAB_00489fe0 -> FUN_0048b150 ("NeuroNetMissionMessage"); atexit LAB_0048a000 -> FUN_00579380.
WINFACTORY<NeuroNetMissionMessage> G_NeuroNetMissionMessageFactory("NeuroNetMissionMessage"); // DAT_00671fb8

// ---------------------------------------------------------------------------------------------
// The builders.

void NnMissionMessageShowScanNotice(unsigned ScannerId, unsigned GoodId)
{
    // FUN_0048a180 (cdecl).
    G_NnMissionMessageScanner = ScannerId;
    G_NnMissionMessageGood = GoodId;

    FmtStr Notice;
    int Count = 0;

    // Retail searches a copy of the player's equipment list, kept to the end of the function.
    std::list<EquipDesc> const Cargo(GetPlayerEquipDescListRetail().Items); // FUN_004c4060
    for (std::list<EquipDesc>::const_iterator It = Cargo.begin(); It != Cargo.end(); ++It)
    {
        if (Arch2Good(It->ArchId) == GoodId)
        {
            Count = It->Count;
            break;
        }
    }

    // The good goes in by reference to DAT_00671f74 itself.
    Notice.begin_mad_lib(0x51e);
    Notice.append_good(G_NnMissionMessageGood);
    Notice.append_string(0x3ca);
    Notice.append_int(static_cast<unsigned>(Count));
    Notice.append_string(0x3cb);
    Notice.end_mad_lib();

    // FUN_0048a130's body, inlined in retail.
    NnMissionMessageDestroy();

    G_NnMissionMessageText = Notice;
    G_NnMissionMessageKind = kKindScanNotice;
    WinFactoryCreate("NeuroNetMissionMessage", "NeuroNetMissionMessage", nullptr, nullptr);
}

void NnMissionMessagePopUp(FmtStr const* Title, FmtStr const* Text, unsigned Buttons)
{
    // FUN_0048a380 (cdecl).
    G_NnMissionMessageTitle = *Title;
    G_NnMissionMessageButtons = Buttons;
    if (G_NnMainButton != nullptr)
        G_NnMainButton->ResetPanel(); // FUN_0045cef0
    // FUN_0048a130's body, inlined in retail.
    NnMissionMessageDestroy();
    G_NnMissionMessageText = *Text;
    G_NnMissionMessageKind = kKindPopUp;
    WinFactoryCreate("NeuroNetMissionMessage", "NeuroNetMissionMessage", nullptr, nullptr);
}

// ---------------------------------------------------------------------------------------------
// NeuroNetMissionMessage

NeuroNetMissionMessage::NeuroNetMissionMessage()
    : SavedCursorDrawFlag(0)
    , CloseButton(nullptr)
    , Title(nullptr)
    , Text(nullptr)
    , Field_0x33c(0)
    , DropCargoButton(nullptr)
    , DropCargoText(nullptr)
    , CancelButton(nullptr)
    , CancelText(nullptr)
    , YesButton(nullptr)
    , NoButton(nullptr)
    , LaterButton(nullptr)
    , OkButton(nullptr)
    , Kind(0)
    , Elapsed(0.0f)
    , bPausedGame(0)
{
    // Inlined into FUN_0048b1c0 (0x0048b1f9). Port-only: every field is zeroed -- retail sets
    // only +0x364 / +0x368 and leaves +0x32c..+0x360 as operator_new left them, though Create
    // writes only the controls its kind uses and SetProperty compares against all of them.
    Pad32d[0] = 0;
    Pad32d[1] = 0;
    Pad32d[2] = 0;
    Pad369[0] = 0;
    Pad369[1] = 0;
    Pad369[2] = 0;
    G_NnMissionMessage = this;
}

NeuroNetMissionMessage::~NeuroNetMissionMessage()
{
    // FUN_0048a440.
    G_NnMissionMessage = nullptr;
    if (bPausedGame != 0)
    {
        bPausedGame = 0;
        NnModalResumeGame(SavedCursorDrawFlag); // FUN_004a7690
    }
}

bool NeuroNetMissionMessage::Create(Win3D* NewParent, void* Data)
{
    // FUN_0048a4c0 (RET 8).
    (void)NewParent;

    Kind = G_NnMissionMessageKind;
    SetMesh(kFrameMesh, 0);
    WinControlHalfExtentsFromMesh(&HalfExtents, &MeshHolder);

    // The pop-up needs the cursor. In space the game also pauses (undone by the dtor).
    if (Kind == kKindPopUp)
    {
        if (!IsPlayerDockedRetail()) // FUN_004c4810
        {
            SavedCursorDrawFlag = NnModalPauseGame(); // FUN_004a7670
            bPausedGame = 1;
        }
        G_bNnScreenUp = true;                  // DAT_0067dcc8
        SetClientMouseCursorDrawFlag(1);       // FUN_0041dda0(1)
        MouseSetCursor("Arrow", false);        // FUN_0041dde0("Arrow", 0)
    }

    DrawSubLayer = 6;
    DrawLayer = 1;

    // Every kind but the pop-up has the close box.
    Vector const ClosePosition = {NnFloatBits(0x3ecd4fdf), NnFloatBits(0x3e72b021), 0.0f};
    Vector const CloseExtents = {NnFloatBits(0x3c9374bc), NnFloatBits(0x3c9374bc), 0.0f};
    if (Kind != kKindPopUp)
    {
        CloseButton = CreateButtonControl("MissionCloseButton", kCloseMesh, nullptr, kSelectSound,
                                          nullptr, &ClosePosition, nullptr, &CloseExtents, 0, 0, 0,
                                          0, 0x409, 0, 0);
    }

    unsigned TitleIds = 0x412;
    if (Kind == 3 || Kind == 1)
        TitleIds = 0x4cf;
    if (Kind == kKindScanNotice)
        TitleIds = 0;
    Vector const TitlePosition = {NnFloatBits(0xbed58106), NnFloatBits(0x3e841893), 0.0f};
    Title = CreateTextControl("MissionMessageTitle", "PageHeader", &TitlePosition, nullptr,
                              NnFloatBits(0x3f5645a2), NnFloatBits(0x3d75c28f), 1.5f, TitleIds,
                              1, 0, 1);

    // The answer buttons share one size; each caption sits below its button.
    Vector const AnswerExtents = {NnFloatBits(0x3dcccccd), NnFloatBits(0x3cd4fdf4), 0.0f};
    float const CaptionHeight = NnFloatBits(0x3cf5c28f);
    float const CaptionArg6 = NnFloatBits(0x3ff33333);

    if (Kind == kKindPopUp)
    {
        // The server's title, upper-cased (the delay-loaded CharUpperW, PTR_FUN_00616420).
        FmtStrSprintf(&G_NnMissionMessageTitle, G_NnTextScratchB, G_NnTextScratchCapacity);
        CharUpperW(G_NnTextScratchB);
        Title->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);

        if ((G_NnMissionMessageButtons & kPopUpButtonYes) != 0)
        {
            Vector const YesPosition = {NnFloatBits(0xbe6b851f), NnFloatBits(0xbe560419), 0.0f};
            YesButton = CreateWireButtonControl("MMYesButton", kSelectSound, &YesPosition,
                                                &AnswerExtents, 0, 0, 0x335, 0, 0);
            Vector const CaptionPosition = {NnFloatBits(0xbea8f5c3), NnFloatBits(0xbe3f7cee), 0.0f};
            CreateTextControl("MMYesText", kButtonTextStyle, &CaptionPosition, nullptr,
                              NnFloatBits(0x3e4ac083), CaptionHeight, CaptionArg6, 0x4dc, 1, 0, 1);
        }
        if ((G_NnMissionMessageButtons & kPopUpButtonNo) != 0)
        {
            Vector const NoPosition = {0.0f, NnFloatBits(0xbe560419), 0.0f};
            NoButton = CreateWireButtonControl("MMNoButton", kSelectSound, &NoPosition, &AnswerExtents,
                                               0, 0, 0x336, 0, 0);
            Vector const CaptionPosition = {NnFloatBits(0xbdc8b439), NnFloatBits(0xbe3f7cee), 0.0f};
            CreateTextControl("MMNoText", kButtonTextStyle, &CaptionPosition, nullptr,
                              NnFloatBits(0x3e4ac083), CaptionHeight, CaptionArg6, 0x4dd, 1, 0, 1);
        }
        if ((G_NnMissionMessageButtons & kPopUpButtonLater) != 0)
        {
            Vector const LaterPosition = {NnFloatBits(0x3e6b851f), NnFloatBits(0xbe560419), 0.0f};
            LaterButton = CreateWireButtonControl("MMLaterButton", kSelectSound, &LaterPosition,
                                                  &AnswerExtents, 0, 0, 0x337, 0, 0);
            Vector const CaptionPosition = {NnFloatBits(0x3e072b02), NnFloatBits(0xbe3f7cee), 0.0f};
            CreateTextControl("MMLaterText", kButtonTextStyle, &CaptionPosition, nullptr,
                              NnFloatBits(0x3e4ac083), CaptionHeight, CaptionArg6, 0x622, 1, 0,
                              1);
        }
        // Ok comes with the close box after all.
        if ((G_NnMissionMessageButtons & kPopUpButtonClose) != 0)
        {
            CloseButton = CreateButtonControl("MissionCloseButton", kCloseMesh, nullptr,
                                              kSelectSound, nullptr, &ClosePosition, nullptr,
                                              &CloseExtents, 0, 0, 0, 0, 0x409, 0, 0);
            Vector const OkPosition = {NnFloatBits(0xbc449ba6), NnFloatBits(0xbe560419), 0.0f};
            OkButton = CreateWireButtonControl("MMOkButton", kSelectSound, &OkPosition, &AnswerExtents,
                                               0, 0, 0, 0, 0);
            Vector const CaptionPosition = {NnFloatBits(0xbddf3b64), NnFloatBits(0xbe3f7cee), 0.0f};
            CreateTextControl("MMOkText", kButtonTextStyle, &CaptionPosition, nullptr,
                              NnFloatBits(0x3e47ae14), CaptionHeight, CaptionArg6, 0x411, 1, 0, 1);
        }
    }

    if (Kind == kKindScanNotice)
    {
        Vector const Extents = {NnFloatBits(0x3e083127), NnFloatBits(0x3cd4fdf4), 0.0f};
        Vector const DropPosition = {NnFloatBits(0xbe3c6a7f), NnFloatBits(0xbe560419), 0.0f};
        DropCargoButton = CreateWireButtonControl("DropCargoButton", kSelectSound, &DropPosition,
                                                  &Extents, 0, 0, 0x338, 0, 0);
        Vector const DropTextPosition = {NnFloatBits(0xbea04189), NnFloatBits(0xbe3f7cee), 0.0f};
        DropCargoText = CreateTextControl("DropCargoText", kButtonTextStyle, &DropTextPosition,
                                          nullptr, NnFloatBits(0x3e851eb8), CaptionHeight,
                                          CaptionArg6, 0x51f, 1, 0, 1);
        Vector const CancelPosition = {NnFloatBits(0x3e3851ec), NnFloatBits(0xbe560419), 0.0f};
        CancelButton = CreateWireButtonControl("DCCancelButton", kSelectSound, &CancelPosition,
                                               &Extents, 0, 0, 0x63e, 0, 0);
        Vector const CancelTextPosition = {NnFloatBits(0x3d4ccccd), NnFloatBits(0xbe3f7cee), 0.0f};
        CancelText = CreateTextControl("DCCancelText", kButtonTextStyle, &CancelTextPosition,
                                       nullptr, NnFloatBits(0x3e851eb8), CaptionHeight,
                                       CaptionArg6, 0x63d, 1, 0, 1);

        // A scanner that is still there and ship-class ((class & 3) == 3) gives its pilot name
        // as the title.
        FClientGameobj* const Scanner = ClientObjDb_FindObject(G_NnMissionMessageScanner, 0);
        if (Scanner != nullptr)
        {
            CBase const* const Base = Scanner;
            CObject const* const Object = Base->Object;
            if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
            {
                // An EH-tracked local in retail: its buffer is freed on the way out and by the
                // unwind funclet.
                struct FPilotName
                {
                    FHudWideText Name;
                    ~FPilotName()
                    {
                        if (Name.Text != nullptr)
                            free(Name.Text);
                    }
                };
                FPilotName Pilot = {{nullptr, 0}};
                GetShipPilotName(Object, &Pilot.Name); // FUN_005472d0
                if (Pilot.Name.Text != nullptr && Pilot.Name.Text[0] != 0)
                {
                    wcscpy(G_NnTextScratchB, Pilot.Name.Text);
                    Title->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
                }
            }
        }
    }

    Vector const TextOffset = {NnFloatBits(0xbec7ae14), NnFloatBits(0x3e8d4fdf),
                               NnFloatBits(0xbfa4dd2f)};
    Text = CreateRichTextWin("MissionMessageText", NnFloatBits(0x3f451eb8),
                             NnFloatBits(0x3ebd70a4), &TextOffset, true);

    // Retail's clone-into-list idiom: the list takes a clone of a clone, the first is deleted.
    FmtStrSprintf(&G_NnMissionMessageText, G_NnTextScratchB, G_NnTextScratchCapacity);
    RenderDisplayList Body;
    {
        StyleNode const Style(kTextStyleBody);
        RDLNode* const Temp = Style.Clone();
        RdlAppendNode(Body, Temp->Clone());
        delete Temp;
    }
    AppendTextMaybeXmlRdl(G_NnTextScratchB, static_cast<unsigned>(wcslen(G_NnTextScratchB)),
                          Body, false);

    Text->SetScrollOrigin(0, 0); // FUN_005798b0
    Text->ClearContent();        // FUN_0057b370
    Text->AppendContent(Body);   // FUN_0057b3e0

    // (0, 0.076, -1.288)
    SetOffset(0.0f, NnFloatBits(0x3d9ba5e3), NnFloatBits(0xbfa4dd2f));
    SetPosition(&Position);

    NnMainButtonOnModalOpen();
    NavBarCloseService(true);

    // FUN_004b5060 with the window's OWN +0x04 parent, not the one Create was given (the
    // factory is always called with null, so it is a root window).
    return Win3D::Create(Parent, Data);
}

void NeuroNetMissionMessage::Update()
{
    // FUN_0048adf0.
    Win3D::Update();

    // Unless this window paused the game, another NeuroNet panel or a base-service window takes
    // it down; a pop-up answers "later" (4) first.
    if (bPausedGame == 0 &&
        (NnMainButtonHasActivePanel() || NavBarHasServiceWindow())) // FUN_0045a470 / FUN_00442720
    {
        if (Kind == kKindPopUp)
            G_Server->PopUpDialog(GetPlayerConnectionId(), 4);
        Win3DDeleteTree(this); // children, slot 10, slot 0 with 1
        return;
    }

    // The scan notice counts its 20 seconds. A scanner that is gone, not ship-class, or hostile
    // (FUN_0045a490 == -1) sets the count to 20 at once, so any running frame time closes it.
    if (Kind != kKindScanNotice)
        return;
    IObjRW const* const Player = static_cast<IObjRW const*>(GetPlayerObjRW()); // FUN_0054baf0
    FClientGameobj* const Scanner = ClientObjDb_FindObject(G_NnMissionMessageScanner, 0);
    bool bScannerOk = false;
    if (Scanner != nullptr && Player != nullptr)
    {
        CBase const* const Base = Scanner;
        CObject const* const Object = Base->Object;
        if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple &&
            GetAttitudeClass(Player, Scanner) != -1)
        {
            bScannerOk = true;
        }
    }
    if (!bScannerOk)
        Elapsed = 20.0f; // 0x41a00000

    // Retail compares the double sum, before rounding, against 20.0f (DAT_005d2688).
    double const Sum = GetClientFrameTimeCapped() + static_cast<double>(Elapsed);
    Elapsed = static_cast<float>(Sum);
    if (Sum > 20.0)
        Win3DDeleteTree(this);
}

void NeuroNetMissionMessage::Draw()
{
    // FUN_0048af10: drawn only while the cursor is drawn (FUN_0041dd90).
    if (GetClientMouseCursorDrawFlag() != 0)
        WinControl::Draw(); // tail call FUN_005a0760
}

intptr_t NeuroNetMissionMessage::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0048af30 (RET 0xc). Nothing reaches WinControl's slot 42.
    (void)B;
    if (Msg != kWinNotifyClick)
        return 0;

    WinControl* const Clicked = reinterpret_cast<WinControl*>(A);
    if (Clicked == DropCargoButton)
    {
        // Jettison all of the player's first non-mission, unmounted item of the scanned good
        // (IObjAffect slot 14, +0x38).
        unsigned const Arch = Good2Arch(G_NnMissionMessageGood);
        if (Arch != 0)
        {
            EquipDesc* const Desc = FindPlayerNonMissionEquipRetail(Arch); // FUN_004c45d0
            IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
            if (Desc != nullptr && !Desc->bEquipped && Player != nullptr)
            {
                IObjAffect* const Affect = Player;
                Affect->jettison_cargo(Desc->Id, Desc->Count, static_cast<int>(GetPlayerConnectionId()));
            }
        }
    }
    else if (Clicked == CancelButton)
    {
        // Just close.
    }
    else if (Clicked == YesButton)
    {
        G_Server->PopUpDialog(GetPlayerConnectionId(), 1);
        G_bNnScreenUp = false;
    }
    else if (Clicked == NoButton)
    {
        G_Server->PopUpDialog(GetPlayerConnectionId(), 2);
        G_bNnScreenUp = false;
    }
    else if (Clicked == LaterButton)
    {
        G_Server->PopUpDialog(GetPlayerConnectionId(), 4);
        G_bNnScreenUp = false;
    }
    else if (Clicked == CloseButton || Clicked == OkButton)
    {
        // The kind is read from DAT_00671ff8, not +0x360. A pop-up answers "close" (8); kinds 0
        // and 5 (no builder sets either) end the game loop.
        int const CurrentKind = G_NnMissionMessageKind;
        G_bNnScreenUp = false;
        if (CurrentKind == kKindPopUp)
        {
            G_Server->PopUpDialog(GetPlayerConnectionId(), 8);
            G_bNnScreenUp = false;
        }
        else if (CurrentKind == 0 || CurrentKind == 5)
        {
            SetClientQuitRequested(true); // FUN_005b2c50: DAT_0067ecbd = 1
        }
    }
    else
    {
        return 0;
    }

    Win3DDeleteTree(this);
    return 1;
}
