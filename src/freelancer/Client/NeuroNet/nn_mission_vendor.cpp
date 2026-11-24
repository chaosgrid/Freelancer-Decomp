// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_mission_vendor.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerShipArchetypeIdRetail (FUN_004c3e10)
#include "Core/globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_command_dispatch.h"   // HudPushCancelHandler (FUN_00576360)
#include "Common/Fonts/ids_rdl.h"                // AppendTextMaybeXmlRdl (FUN_0057e2c0), RdlAppendNode
#include "NeuroNet/Economy/knowledge_map.h"          // KnowledgeMapRevealFmtStr (FUN_00459950)
#include "Network/login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "GameFlow/menu_universe_base.h"     // GetClientLocationBaseMissionOffers (FUN_0043a8d0),
                                    // GetClientCurrentLocationRuntime (FUN_0043a460),
                                    // GetClientLocationCurrentBase (DAT_0066873c)
#include "GameFlow/mirror_property_bucket.h"
#include "GameFlow/nav_bar.h"                // NavBarCloseService (FUN_004424f0)
#include "Common/misc_util.h"              // FmtStrSprintf (FUN_0041c970),
                                    // MiscUtilLoadMaterialLibrary (FUN_0041c850)
#include "Space/player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "Interface/rich_text_win.h"
#include "GameFlow/room_control.h"           // GetRoomRuntimeBaseId (+0x34)
#include "Interface/ui_scroll.h"
#include "Interface/win3d.h"                  // Win3DFindByName, Win3DFindByClassName, Win3DDeleteTree
#include "Interface/win_factory.h"
#include "Interface/wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)
#include "NeuroNet/Chat/chat_init.h"        // EChatTextStyle

#include "DALib.h"
#include "common_goods.h"           // MissionVendorProperties, MissionVendorAcceptance
#include "common_server.h"          // IServer::MissionResponse
#include "common_text.h"            // FmtStr, RenderDisplayList, StyleNode
#include "dacom.h"                  // FDUMP
#include "render/shading.h"         // IMaterialLibrary::DeleteGroup

#include <new>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

// Retail DAT_00671ffc -- WINFACTORY<NN_MissionVendor> static (LAB_0048b2b0 -> FUN_0048d5b0;
// Create FUN_0048d620; atexit LAB_0048b2d0 -> FUN_00579380).
WINFACTORY<NnMissionVendor> G_NnMissionVendorFactory("NN_MissionVendor");

int G_MissionVendorNoMissionsReason = 0; // DAT_00672038 (.data, zero)

namespace
{
    char const kMissionVendorCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_MissionVendor.cpp";
    char const kNoMaterialInVendor[] = "%s(%d) : *** ERROR: Cannot find material %s in vendor\n";
    char const kVideoMaterialName[] = "MV_vidmaterial";
    char const kVideoTextureLibrary[] = "Interface\\NeuroNet\\MissionVendor\\MissionVendor.txm";
    char const kVideoScreenMesh[] = "Interface\\NeuroNet\\MissionVendor\\MV_videoscreen.3db";
    char const kHeadlineIconMesh[] = "Interface\\NeuroNet\\NewsVendor\\NEWS_headlineicon.3db";

    // DAT_00611bf0 -- the tooltip ids of the seven icons (MissionVendorProperties +0x10 indexes
    // it; read only at 0x0048c9cb and 0x0048d2f7).
    int const G_MissionVendorIconIds[7] = {0, 0x32fa, 0x32fb, 0x32fc, 0x32fd, 0x32fe, 0x32ff};

    // The seven paths Create stores into IconPaths (0x0048c36b), in that order.
    char const* const kMissionIconMeshes[7] = {
        kHeadlineIconMesh,
        "Interface\\NeuroNet\\MissionVendor\\MV_missiondestroy.3db",
        "Interface\\NeuroNet\\MissionVendor\\MV_missiondestroyinstall.3db",
        "Interface\\NeuroNet\\MissionVendor\\MV_missionassassinate.3db",
        "Interface\\NeuroNet\\MissionVendor\\MV_missiondestroycontra.3db",
        "Interface\\NeuroNet\\MissionVendor\\MV_missionbounty.3db",
        "Interface\\NeuroNet\\MissionVendor\\MV_missionretrieve.3db",
    };

    // DAT_00672030 / DAT_0067202c / DAT_00672028 (-0.6f / -0.6f / 0.6f, static inits
    // LAB_0048b280 / LAB_0048b290 / LAB_0048b2a0) are this TU's copies of a header float
    // constant that nothing reads; the port has no counterpart.

    uintptr_t PointerArg(void const* Pointer)
    {
        return reinterpret_cast<uintptr_t>(Pointer);
    }

    // The reward line retail inlines in RefreshRows (0x0048ca54) and ShowSelected (0x0048cf03).
    void FormatReward(MissionVendorProperties const& Offer, wchar_t* Buffer)
    {
        FmtStr Reward;
        if (Offer.IconTextureId != 0)
        {
            Reward.begin_mad_lib(0x732);
            Reward.append_int(Offer.IconTextureId);
        }
        else
        {
            Reward.begin_mad_lib(0x741);
        }
        Reward.end_mad_lib();
        FmtStrSprintf(&Reward, Buffer, 0x400);
    }

    // The full-text idiom ShowSelected inlines twice (0x0048cd79 / 0x0048cfd4).
    void SetFullText(RichTextWin* Pane, wchar_t const* Buffer)
    {
        RenderDisplayList Text;
        {
            StyleNode Style(kTextStyleBody);
            RdlAppendNode(Text, Style.Clone());
        }
        AppendTextMaybeXmlRdl(Buffer, static_cast<unsigned>(wcslen(Buffer)), Text, false);
        Pane->ClearContent();          // FUN_0057b370
        Pane->AppendContent(Text);     // FUN_0057b3e0
        Pane->SetScrollOrigin(0, 0);   // FUN_005798b0
    }
}

NnMissionVendor::NnMissionVendor()
    : ResultCallback(nullptr)
    , ResultContext(nullptr)
    , bCloseRequested(0)
    , ScrollOffset(0)
    , Field_0x374(0)
    , Selected(0)
    , Offers(nullptr)
    , OfferCount(0)
    , IconPaths(nullptr)
    , IconPathCount(0)
{
    // FUN_0048b2e0.
    StatusText.destroy();
    memset(static_cast<void*>(&StatusText), 0, sizeof(StatusText));
    VideoTexture.clear();
    VideoMaterial.Reset(); // FUN_0042cc30
    // MaterialGroup (+0x348) is not written.
    Clicked = 0;
    bNoMissions = 0;
    Hovered = -1;
}

NnMissionVendor::~NnMissionVendor()
{
    // FUN_0048b510.
    StatusText.destroy();
    delete[] Offers;
    ::operator delete(static_cast<void*>(IconPaths));
    ResetVideo();
    DALib::MaterialLib->DeleteGroup(MaterialGroup); // slot 10
    MaterialGroup = -1;
}

uintptr_t NnMissionVendor::RowDword(size_t ArrayOffset, int Index) const
{
    uintptr_t Value = 0;
    unsigned char const* const Bytes = reinterpret_cast<unsigned char const*>(this);
    memcpy(&Value, Bytes + ArrayOffset + static_cast<ptrdiff_t>(Index) * 4, sizeof(Value));
    return Value;
}

void NnMissionVendor::ResetVideo()
{
    VideoTexture.clear();
    VideoMaterial.Release(); // FUN_0042cc50
    VideoTexture.clear();    // ?erase@ (0, npos)
    VideoMaterial.Reset();   // FUN_0042cc30
}

void NnMissionVendor::RevealSelectedOffer()
{
    // Inlined in retail, e.g. 0x0048c3e2.
    MissionVendorProperties const& Offer = Offers[Selected];
    KnowledgeMapRevealFmtStr(&Offer.Title);
    KnowledgeMapRevealFmtStr(&Offer.Description);
    KnowledgeMapRevealFmtStr(&Offer.Briefing);
}

bool NnMissionVendor::Create(Win3D* NewParent, void* Data)
{
    // LAB_0048b6e0 (RET 8).
    HalfExtents[0] = 0.43f; // +0xb4
    HalfExtents[1] = 0.3f;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = 0.0f;    // +0xc0
    HitOffset[1] = 0.033f;
    HitOffset[2] = -1.0f;
    BasePosition[0] = 0.0f;
    BasePosition[1] = -0.1f;
    BasePosition[2] = -1.498f;
    float const X = BasePosition[0];
    float const Y = BasePosition[1];
    float const Z = BasePosition[2];
    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    {
        float const Position[3] = {X, Y, Z};
        Background = CreateStaticControl("MissionVendorBackground",
                                         "Interface\\NeuroNet\\NewsVendor\\NEWS_background.cmp",
                                         Position, nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    {
        float const Position[3] = {X + 0.584f, Y + 0.455f, Z};
        float const Half[3] = {0.025f, 0.025f, 0.0f};
        CloseButton = CreateButtonControl("MissionVendorCloseButton",
                                          "Interface\\NeuroNet\\Inventory\\x.3db", nullptr,
                                          "ui_select_item", nullptr, Position, nullptr, Half, 0, 0,
                                          0, 0, 0x72e, 0, 0);
    }
    {
        float const Position[3] = {X - 0.6f, Y + 0.486f, Z};
        Title = CreateTextControl("MissionVendorTitle", "PageHeader", Position, nullptr, 1.2f, 0.03f,
                                  2.0f, 0x72b, 1, 0, 1);
    }

    // The offers of the base of the current room (FUN_0043a460's +0x34, 0x0048b8d1). Retail:
    // LoadOffers reads those of DAT_0066873c instead (0x0048c788).
    TMirrorPropertyBucket<MissionVendorProperties>* const Bucket = GetClientLocationBaseMissionOffers(
        GetRoomRuntimeBaseId(static_cast<FRoomRuntime*>(GetClientCurrentLocationRuntime())));
    int Count = 0;
    MetaNode<MissionVendorProperties>* Node = nullptr;
    while (Bucket->List.traverse(Node, true))
        ++Count;

    if (Count == 0)
    {
        // Why there is nothing to offer.
        bNoMissions = 1;
        unsigned Ids = 0;
        if (GetPlayerShipArchetypeIdRetail() == 0)
        {
            Ids = 0x3b0;
        }
        else
        {
            // Jump table 0x0048c444; above 4 keeps 0.
            switch (G_MissionVendorNoMissionsReason)
            {
            case 0: Ids = 0x3d0; break;
            case 1: Ids = 0x384; break;
            case 2: Ids = 0x385; break;
            case 3: Ids = 0x386; break;
            case 4: Ids = 0x3d0; break;
            default: break;
            }
        }
        float const Position[3] = {-0.54f, -0.01f, BasePosition[2]};
        CreateTextControl("MissionVendorNoMissions", "SubHeader", Position, nullptr, 1.055f, 0.03f,
                          2.0f, Ids, 1, 0, 1);
        HudPushCancelHandler(AsHudCmdHandler(), 0); // FUN_00576360
        return Win3D::Create(NewParent, Data);      // FUN_004b5060
    }

    {
        float const Position[3] = {X - 0.215f, Y + 0.217f, Z};
        CreateStaticControl("MissionVendorHeadlinesFrame",
                            "Interface\\NeuroNet\\NewsVendor\\NEWS_headlineframe.3db", Position,
                            nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    {
        float const Position[3] = {X - 0.006f, Y - 0.149f, Z};
        CreateStaticControl("MissionVendorStoryFrame",
                            "Interface\\NeuroNet\\NewsVendor\\NEWS_textframe.3db", Position, nullptr,
                            Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    {
        float const Position[3] = {X + 0.364f, Y + 0.288f, Z};
        CreateStaticControl("MissionVendorVideoFrame",
                            "Interface\\NeuroNet\\NewsVendor\\NEWS_videowindow.3db", Position,
                            nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    MaterialGroup = MiscUtilLoadMaterialLibrary(kVideoScreenMesh);
    {
        float const Position[3] = {X + 0.364f, Y + 0.288f, Z};
        CreateStaticControl("MissionVendorVideoScreen", kVideoScreenMesh, Position, nullptr, Zero, 0,
                            0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    {
        float const Position[3] = {X + 0.237f, Y + 0.105f, Z};
        CreateTextControl("MissionVendorAcceptText", "ButtonText", Position, nullptr, 0.277f, 0.03f,
                          2.0f, 0x3e9, 1, 0, 1);
    }
    {
        float const Position[3] = {X + 0.37f, Y + 0.078f, Z};
        float const Half[3] = {0.133f, 0.026f, 0.0f};
        AcceptButton = CreateWireButtonControl("MissionVendorAcceptButton", "ui_execute_transaction",
                                               Position, Half, 0, 0, 0x331, 0, 0);
    }

    float const Top[3] = {X - 0.39f, Y + 0.36f, Z};
    char Name[0x100];
    for (int Row = 0; Row < 5; ++Row)
    {
        // Subtracted at extended precision and stored once as float.
        float const RowPos[3] = {
            Top[0],
            static_cast<float>(static_cast<double>(Top[1]) -
                               static_cast<double>(Row) * static_cast<double>(0.0615f)),
            Top[2]};

        sprintf(Name, "MissionVendorSummaryIcon%d", Row);
        {
            float const Position[3] = {RowPos[0] - 0.125f, RowPos[1], RowPos[2]};
            SummaryIcons[Row] = CreateStaticControl(Name, kHeadlineIconMesh, Position, nullptr, Zero, 0,
                                                    0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
        }
        sprintf(Name, "MissionVendorIconFrame%d", Row);
        {
            float const Position[3] = {RowPos[0] - 0.125f, RowPos[1], RowPos[2]};
            float const Half[3] = {0.03f, 0.03f, 0.0f};
            IconFrames[Row] = CreateWireButtonControl(Name, "ui_select_item", Position, Half, 0, 0,
                                                      0x72c, 0, 0);
        }
        sprintf(Name, "MissionVendorSummaryBox%d", Row);
        {
            float const Position[3] = {RowPos[0] + 0.185f, RowPos[1], RowPos[2]};
            float const Half[3] = {0.245f, 0.031f, 0.0f};
            SummaryBoxes[Row] = CreateWireButtonControl(Name, nullptr, Position, Half, 0, 0, 0x72c, 0,
                                                        0);
        }
        sprintf(Name, "MissionVendorSummaryDest%d", Row);
        {
            float const Position[3] = {RowPos[0] + 0.195f, RowPos[1] + 0.017f, RowPos[2]};
            SummaryDests[Row] = CreateTextControl(Name, "NewsMission", Position, nullptr, 0.49f, 0.03f,
                                                  1.5f, 0, 0, 0, 0);
        }
        sprintf(Name, "MissionVendorSummaryTitle%d", Row);
        {
            float const Position[3] = {RowPos[0] + 0.195f, RowPos[1] - 0.009f, RowPos[2]};
            SummaryTitles[Row] = CreateTextControl(Name, "NewsMission", Position, nullptr, 0.49f,
                                                   0.03f, 1.5f, 0, 0, 0, 0);
        }
        sprintf(Name, "MissionVendorSummaryReward%d", Row);
        {
            float const Position[3] = {RowPos[0] + 0.293f, RowPos[1] + 0.004f, RowPos[2]};
            SummaryRewards[Row] = CreateTextControl(Name, "NewsMission", Position, nullptr, 0.27f,
                                                    0.03f, 1.5f, 0, 2, 0, 0);
        }
    }

    {
        float const Position[3] = {X + 0.09f, Y + 0.397f, Z};
        SummaryScroll = CreateUIScroll("MissionVendorSummaryScroll", Position, 0.019f, 0.317f, 0, 5,
                                       5, 0);
    }
    // The selected offer's lines. Retail sprintf's each name into the buffer first (no
    // conversions).
    {
        float const Position[3] = {X - 0.493f, Y + 0.0085f, Z};
        MissionReject = CreateTextControl("MissionVendorMissionReject", "Normal", Position, nullptr,
                                          0.7f, 0.03f, 1.8f, 0x72f, 0, 0, 1);
    }
    {
        float const Position[3] = {X - 0.494f, Y + 0.022f, Z};
        MissionDest = CreateTextControl("MissionVendorMissionDest", "NewsMission", Position, nullptr,
                                        0.7f, 0.03f, 1.5f, 0, 0, 0, 1);
    }
    {
        float const Position[3] = {X - 0.494f, Y - 0.006f, Z};
        MissionTitle = CreateTextControl("MissionVendorMissionTitle", "NewsMission", Position,
                                         nullptr, 0.7f, 0.03f, 1.5f, 0, 0, 0, 1);
    }
    {
        float const Position[3] = {X + 0.348f, Y + 0.009f, Z};
        MissionReward = CreateTextControl("MissionVendorMissionReward", "NewsMission", Position,
                                          nullptr, 0.2f, 0.03f, 1.5f, 0, 2, 0, 1);
    }
    {
        float const Position[3] = {X - 0.556f, Y - 0.051f, Z};
        FullText = CreateRichTextWin("MissionVendorMissionFullText", 1.08f, 0.27f, Position, true);
    }
    // Fixed x / y, the base's z.
    {
        float const Position[3] = {-0.535f, -0.111f, BasePosition[2]};
        float const Half[3] = {0.03f, 0.03f, 0.0f};
        MissionIcon = CreateStaticControl("MissionVendorIcon", kHeadlineIconMesh, Position, nullptr,
                                          Half, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    }

    DrawLayer = 0x64;
    IconPathCount = 7;
    IconPaths = static_cast<char const**>(::operator new(0x1c));
    for (int Index = 0; Index < 7; ++Index)
        IconPaths[Index] = kMissionIconMeshes[Index];

    LoadOffers();
    RefreshRows();
    ShowSelected();
    if (Selected != -1)
        RevealSelectedOffer();

    HudPushCancelHandler(AsHudCmdHandler(), 0); // FUN_00576360
    return Win3D::Create(NewParent, Data);      // FUN_004b5060
}

void NnMissionVendor::LoadOffers()
{
    // FUN_0048c770: the offers of the base the player is in (DAT_0066873c).
    TMirrorPropertyBucket<MissionVendorProperties>* const Bucket =
        GetClientLocationBaseMissionOffers(GetClientLocationCurrentBase());
    int Count = 0;
    MetaNode<MissionVendorProperties>* Node = nullptr;
    while (Bucket->List.traverse(Node, true))
        ++Count;

    Offers = new MissionVendorProperties[Count];
    OfferCount = Count;

    // Retail walks from the head for each i; one walk visits the same nodes in the same order.
    Node = nullptr;
    for (int Index = 0; Index < Count; ++Index)
    {
        Bucket->List.traverse(Node, true);
        Offers[Index] = *Node->Data; // ??4MissionVendorProperties@@
    }

    ScrollOffset = 0;
    Field_0x374 = 0;
    Selected = 0;
    SummaryScroll->SetProperty(0x37, 0, static_cast<uintptr_t>(OfferCount - 1));
}

void NnMissionVendor::RefreshRows()
{
    // FUN_0048c8a0.
    wchar_t Buffer[0x400];
    int Row = 0;
    for (int Index = ScrollOffset; Index < OfferCount; ++Index)
    {
        MissionVendorProperties const& Offer = Offers[Index];
        WinControl* const Controls[6] = {SummaryIcons[Row], IconFrames[Row], SummaryBoxes[Row],
                                         SummaryDests[Row], SummaryTitles[Row], SummaryRewards[Row]};

        for (int Slot = 0; Slot < 6; ++Slot)
            Controls[Slot]->SetProperty(2, 0, 0);
        for (int Slot = 0; Slot < 6; ++Slot)
            Controls[Slot]->SetProperty(1, 0, 0);

        // An unknown class keeps path 0 and ids 0 (unsigned compare).
        char const* Path = IconPaths[0];
        int Ids = 0;
        if (Offer.MissionClass < IconPathCount)
        {
            Path = IconPaths[Offer.MissionClass];
            Ids = G_MissionVendorIconIds[Offer.MissionClass];
        }
        SummaryIcons[Row]->SetProperty(4, PointerArg(Path), 0);
        IconFrames[Row]->SetProperty(3, static_cast<uintptr_t>(Ids), 0);
        SummaryBoxes[Row]->SetProperty(3, static_cast<uintptr_t>(Ids), 0);

        // The offer's +0x14 and +0x2c texts.
        FmtStrSprintf(&Offer.Title, Buffer, 0x400);
        SummaryDests[Row]->SetProperty(0x1d, PointerArg(Buffer), 0);
        FmtStrSprintf(&Offer.Description, Buffer, 0x400);
        SummaryTitles[Row]->SetProperty(0x1d, PointerArg(Buffer), 0);

        FormatReward(Offer, Buffer);
        SummaryRewards[Row]->SetProperty(0x1d, PointerArg(Buffer), 0);

        uintptr_t const bSelected = (Index == Selected) ? 1u : 0u;
        IconFrames[Row]->SetProperty(0xc, bSelected, 0);
        SummaryBoxes[Row]->SetProperty(0xc, bSelected, 0);

        ++Row;
        if (Row >= 5)
            break;
    }

    // The unused rows.
    for (; Row < 5; ++Row)
    {
        WinControl* const Controls[6] = {SummaryIcons[Row], IconFrames[Row], SummaryBoxes[Row],
                                         SummaryDests[Row], SummaryTitles[Row], SummaryRewards[Row]};
        for (int Slot = 0; Slot < 6; ++Slot)
            Controls[Slot]->SetProperty(2, 1, 0);
        for (int Slot = 0; Slot < 6; ++Slot)
            Controls[Slot]->SetProperty(1, 1, 0);
        SummaryIcons[Row]->SetProperty(4, PointerArg(IconPaths[0]), 0);
        SummaryBoxes[Row]->SetProperty(0x1e, 0, 0);
        SummaryDests[Row]->SetProperty(0x1e, 0, 0);
        SummaryTitles[Row]->SetProperty(0x1e, 0, 0);
        SummaryRewards[Row]->SetProperty(0x1e, 0, 0);
    }
}

void NnMissionVendor::ShowSelected()
{
    // FUN_0048cce0.
    wchar_t Buffer[0x400];
    if (Selected == -1)
    {
        // The video is left as it is.
        MissionReject->SetProperty(0x1e, 0x72f, 0);
        MissionDest->SetProperty(0x1e, 0, 0);
        MissionTitle->SetProperty(0x1e, 0, 0);
        MissionReward->SetProperty(0x1e, 0, 0);
        FmtStrSprintf(&StatusText, Buffer, 0x400);
        SetFullText(FullText, Buffer);
        return;
    }

    // Selected is not range-checked (a click below the last offer selects past the array, as in
    // retail).
    MissionVendorProperties const& Offer = Offers[Selected];
    FmtStrSprintf(&Offer.Title, Buffer, 0x400);
    MissionDest->SetProperty(0x1d, PointerArg(Buffer), 0);
    FmtStrSprintf(&Offer.Description, Buffer, 0x400);
    MissionTitle->SetProperty(0x1d, PointerArg(Buffer), 0);
    FormatReward(Offer, Buffer);
    MissionReward->SetProperty(0x1d, PointerArg(Buffer), 0);
    MissionReject->SetProperty(0x1e, 0, 0);
    FmtStrSprintf(&Offer.Briefing, Buffer, 0x400);
    SetFullText(FullText, Buffer);
    UpdateVideo(); // FUN_0048d140
}

void NnMissionVendor::UpdateVideo()
{
    // FUN_0048d140.
    if (Selected < 0)
    {
        MissionIcon->SetProperty(1, 1, 0);
        return;
    }

    // Retail runs strlen on get_icon_texture's result unchecked.
    MissionVendorProperties const& Offer = Offers[Selected];
    char const* const Texture = Offer.get_icon_texture();
    ResetVideo();
    VideoTexture.assign(Texture, strlen(Texture));

    if (!VideoMaterial.Init(kVideoMaterialName))
        FDUMP(0x100001, kNoMaterialInVendor, kMissionVendorCpp, 0x3af, kVideoMaterialName);
    VideoMaterial.Swap(VideoTexture.c_str(), kVideoTextureLibrary);

    char const* Path = IconPaths[0];
    int Ids = 0;
    if (Offer.MissionClass < IconPathCount)
    {
        Path = IconPaths[Offer.MissionClass];
        Ids = G_MissionVendorIconIds[Offer.MissionClass];
    }
    MissionIcon->SetProperty(4, PointerArg(Path), 0);
    MissionIcon->SetProperty(3, static_cast<uintptr_t>(Ids), 0);
    MissionIcon->SetProperty(1, 0, 0);
}

void NnMissionVendor::Draw()
{
    // FUN_0048d360.
    if (bNoMissions == 0)
    {
        // The hovered row's frame or box draws at 0x65, the selected row's at 0x66 (gated on
        // Clicked, indexed by Selected; the second test wins). The rows are not range-checked.
        int const SelectedRow = Selected - ScrollOffset;
        int const HoveredRow = Hovered - ScrollOffset;
        size_t const FramesOffset = offsetof(NnMissionVendor, IconFrames);
        size_t const BoxesOffset = offsetof(NnMissionVendor, SummaryBoxes);
        for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        {
            Child->DrawLayer = 0x64;
            if (Hovered >= 0)
            {
                uintptr_t const Control = PointerArg(Child->AsWinControl()); // slot 2
                if (Control == RowDword(FramesOffset, HoveredRow) ||
                    Control == RowDword(BoxesOffset, HoveredRow))
                    Child->DrawLayer = 0x65;
            }
            if (Clicked >= 0)
            {
                uintptr_t const Control = PointerArg(Child->AsWinControl());
                if (Control == RowDword(FramesOffset, SelectedRow) ||
                    Control == RowDword(BoxesOffset, SelectedRow))
                    Child->DrawLayer = 0x66;
            }
        }

        WireControlsJoinColumn(IconFrames, 5);   // FUN_005a4b50
        WireControlsJoinColumn(SummaryBoxes, 5);
        Background->DrawLayer = 0x63;
        FullText->DrawLayer = 0x67;
    }
    WinControl::Draw(); // FUN_005a0760 (tail jump)
}

void NnMissionVendor::Update()
{
    // LAB_0048d440: the frame time is fetched and dropped; then a close OnAcceptResult asked for.
    (void)GetClientFrameTimeCapped(); // FUN_0042d680
    Win3D::Update();                  // FUN_005a17b0
    if (bCloseRequested != 0)
    {
        bCloseRequested = 0;
        NavBarCloseService(false); // FUN_004424f0(0)
        Win3DDeleteTree(this);
    }
}

intptr_t NnMissionVendor::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0048c460 (RET 0xc): byte table 0x0048c6f0 over Msg - 0x26. Every path returns 0.
    switch (Msg)
    {
    case 0x4e:
    {
        // Closed from outside: no callback, no NavBar. (Retail tests `this` first.)
        Win3DDeleteTree(this);
        return 0;
    }
    case 0x3e:
    {
        if (A == PointerArg(SummaryScroll) && static_cast<int>(B) != ScrollOffset)
        {
            ScrollOffset = static_cast<int>(B);
            RefreshRows();
        }
        return 0;
    }
    case 0x2c:
    case 0x2d:
    {
        // Retail quirks kept: Hovered is written (0x2c) or cleared (0x2d) on every pass until the
        // control is found, and 0x2d compares against Row - ScrollOffset (0x0048c515). Then the
        // row's other wire button gets the same message.
        int Row = 0;
        for (; Row < 5; ++Row)
        {
            if (Msg == 0x2c)
                Hovered = ScrollOffset + Row;
            else if (Hovered == Row - ScrollOffset)
                Hovered = -1;
            if (A == PointerArg(IconFrames[Row]) || A == PointerArg(SummaryBoxes[Row]))
                break;
        }
        if (Row == 5)
            return 0;
        if (A != PointerArg(IconFrames[Row]))
            IconFrames[Row]->SetProperty(Msg, 0, 0);
        if (A != PointerArg(SummaryBoxes[Row]))
            SummaryBoxes[Row]->SetProperty(Msg, 0, 0);
        return 0;
    }
    case 0x26:
    {
        if (A == PointerArg(CloseButton))
        {
            if (ResultCallback != nullptr)
                ResultCallback(ResultContext, 0);
            NavBarCloseService(false);
            Win3DDeleteTree(this);
            return 0;
        }
        if (A == PointerArg(AcceptButton))
        {
            // IServer slot 43 (+0xac). The window stays open until OnAcceptResult.
            if (Selected < 0)
                return 0;
            if (ResultCallback != nullptr)
                ResultCallback(ResultContext, 1);
            MissionVendorProperties const& Offer = Offers[Selected];
            G_Server->MissionResponse(Offer.OfferId, Offer.OfferKind, true, GetPlayerConnectionId());
            return 0;
        }

        // A row clicked. No match leaves Clicked at -1 and selects ScrollOffset - 1, as retail
        // does.
        Clicked = -1;
        for (int Row = 0; Row < 5; ++Row)
        {
            if (A == PointerArg(IconFrames[Row]) || A == PointerArg(SummaryBoxes[Row]))
            {
                Clicked = Row;
                break;
            }
        }
        Selected = ScrollOffset + Clicked;
        RefreshRows();
        ShowSelected();
        if (Selected != -1)
            RevealSelectedOffer();
        return 0;
    }
    default:
        return 0;
    }
}

bool NnMissionVendor::OnEvent(unsigned MsgId)
{
    // LAB_0048d540 (RET 4), on the +0x32c face: USER_CANCEL (0x10) closes the window like the
    // close button. (Retail tests the adjusted `this` for null.)
    if (MsgId != 0x10)
        return false;
    if (ResultCallback != nullptr)
        ResultCallback(ResultContext, 0);
    NavBarCloseService(false);
    Win3DDeleteTree(this);
    return true;
}

void NnMissionVendor::CloseWindow()
{
    // FUN_0048c720. The NavBar goes first; retail then tests `this` (its caller already has).
    NavBarCloseService(false);
    Win3DDeleteTree(this);
}

void NnMissionVendor::OnAcceptResult(MissionVendorAcceptance const& Result)
{
    // FUN_0048d4a0: the open NN_MissionVendor (FUN_0059d9c0 by class name).
    NnMissionVendor* const Window =
        static_cast<NnMissionVendor*>(Win3DFindByClassName("NN_MissionVendor"));
    if (Window == nullptr)
        return;

    // Accepted (+0x08): close at the next Update.
    if (Result.bAccepted)
    {
        Window->bCloseRequested = 1;
        return;
    }

    // Refused: no selection, the reason (+0x0c) as the status text.
    Window->Selected = -1;
    Window->StatusText.destroy();
    Window->StatusText = Result.Reason;
    Window->RefreshRows();
    Window->ShowSelected();
    if (Window->Selected != -1)
        Window->RevealSelectedOffer();
}
