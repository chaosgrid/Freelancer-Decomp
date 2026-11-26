// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_news.h"

#include "NeuroNet/Economy/client_base_market.h"     // GetClientLocationBaseMarket (FUN_0043a510),
                                    // ClientMarketCollectCategory (FUN_00437f70),
                                    // ClientMarketGetPrice (FUN_00438060)
#include "Common/Fonts/font_string.h"            // FontStringChildCount (FUN_004145a0)
#include "Core/widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "Core/globals.h"                // GetClientFrameTimeCapped (FUN_0042d680),
                                    // G_RenderSurfaceWidth (DAT_00610850)
#include "Space/HUD/hud_command_dispatch.h"   // HudPushCancelHandler (FUN_00576360)
#include "Common/Fonts/ids_rdl.h"                // AppendTextMaybeXmlRdl (FUN_0057e2c0), RdlAppendNode
#include "Common/Fonts/ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "NeuroNet/Economy/knowledge_map.h"          // KnowledgeMapRevealFmtStr (FUN_00459950)
#include "Common/Fonts/font_imp.h"               // FindMenuFontImp (FUN_004177b0)
#include "Common/Audio/menu_sound_arch.h"        // LookupClientSoundArch (FUN_0042ae40),
                                    // PlayClientSoundArch (FUN_00428680),
                                    // StopClientSoundArch (FUN_004285c0)
#include "GameFlow/menu_universe_base.h"     // GetClientLocationBaseNews (FUN_0043a630),
                                    // GetClientLocationCurrentBase (DAT_0066873c)
#include "GameFlow/mirror_property_bucket.h"
#include "GameFlow/nav_bar.h"                // NavBarCloseService (FUN_004424f0)
#include "Common/misc_util.h"              // FmtStrSprintf (FUN_0041c970),
                                    // MiscUtilLoadMaterialLibrary (FUN_0041c850)
#include "NeuroNet/nn_montage_draw.h"        // NnMontageFlushButtons (FUN_00559c20)
#include "NeuroNet/nn_text_control.h"        // NnTextControl::GetFontString (FUN_0058a5d0)
#include "Interface/rich_text_win.h"
#include "Interface/ui_scroll.h"
#include "Interface/win3d.h"                  // Win3DFindByName, Win3DDeleteTree
#include "Interface/win3d_renderables.h"      // ButtonMontageEnsure (FUN_00559c10)
#include "Interface/win_factory.h"
#include "Interface/wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)
#include "NeuroNet/Chat/chat_init.h"        // EChatTextStyle

#include "DALib.h"
#include "common_goods.h"           // NewsBroadcastProperties, GoodList::find_by_id
#include "common_id.h"              // CreateID, ID_String
#include "common_misc.h"            // malloc_strcpy, malloc_strfree
#include "common_text.h"            // FmtStr, RenderDisplayList, StyleNode
#include "dacom.h"                  // FDUMP
#include "font.h"                   // IFont::GetCharWidths (slot 10)
#include "render/shading.h"         // IMaterialLibrary::DeleteGroup

#include <list>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <windows.h>

// Retail DAT_006720cc -- WINFACTORY<NN_News> static (LAB_004a3fc0 -> FUN_004a5fb0; Create
// FUN_004a6020; atexit LAB_004a3fe0 -> FUN_00579380).
WINFACTORY<NnNews> G_NnNewsFactory("NN_News");

namespace
{
    char const kNewsVendorCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_NewsVendor.cpp";
    char const kNoMaterialInNewsVendor[] = "%s(%d) : *** ERROR: Cannot find material %s in News Vendor";
    char const kNoStringForGood[] = "%s(%d) : *** WARNING: Failed to find string resource %d for good %s\n";
    char const kVideoMaterialName[] = "MV_vidmaterial";
    char const kVideoTextureLibrary[] = "Interface\\NeuroNet\\NewsVendor\\NewsVendor.txm";
    char const kVideoScreenMesh[] = "Interface\\NeuroNet\\MissionVendor\\MV_videoscreen.3db";
    char const kHeadlineIconMesh[] = "Interface\\NeuroNet\\NewsVendor\\NEWS_headlineicon.3db";

    // DAT_00611d0c -- a .data pointer to "NewsTicker" (0x005d3fb8): the ticker's text style
    // (0x004a4a01) and the font whose character widths BuildTicker takes (0x004a5c12). No writer.
    char const* G_NewsTickerStyle = "NewsTicker";

    // The seven paths Create stores into IconPaths (0x004a4bd8), in that order.
    char const* const kNewsIconMeshes[7] = {
        kHeadlineIconMesh,
        "Interface\\NeuroNet\\NewsVendor\\NEWS_missioncriticalnews.3db",
        "Interface\\NeuroNet\\NewsVendor\\NEWS_worldnews.3db",
        "Interface\\NeuroNet\\NewsVendor\\NEWS_missionlead.3db",
        "Interface\\NeuroNet\\NewsVendor\\NEWS_system.3db",
        "Interface\\NeuroNet\\NewsVendor\\NEWS_faction.3db",
        "Interface\\NeuroNet\\NewsVendor\\NEWS_universe.3db",
    };
    // The seven tooltip ids Create stores into IconIds (0x004a4c2e).
    int const kNewsIconIds[7] = {0x4ae, 0x4af, 0x4b0, 0x4b1, 0x67c, 0x67d, 0x67e};
    // The tooltip of an unknown category (0x004a535f / 0x004a57ee).
    int const kNewsDefaultIconIds = 0x42b;

    // DAT_00672100 / DAT_006720fc / DAT_006720f8 (-0.6f / -0.6f / 0.6f, static inits
    // LAB_004a3f90 / LAB_004a3fa0 / LAB_004a3fb0) are this TU's copies of a header float
    // constant that nothing reads; the port has no counterpart.

    uintptr_t PointerArg(void const* Pointer)
    {
        return reinterpret_cast<uintptr_t>(Pointer);
    }

    uintptr_t FloatArg(float Value)
    {
        unsigned Bits = 0;
        memcpy(&Bits, &Value, sizeof(Bits));
        return static_cast<uintptr_t>(Bits);
    }

    // The icon of a news category, inlined in RefreshRows (0x004a534e) and ShowSelected
    // (0x004a57dd); unsigned compare.
    void NewsIconFor(NnNews const& Window, unsigned Category, char const** OutPath, int* OutIds)
    {
        *OutPath = Window.IconPaths[0];
        *OutIds = kNewsDefaultIconIds;
        if (Category < Window.IconPathCount)
        {
            *OutPath = Window.IconPaths[Category];
            *OutIds = Window.IconIds[Category];
        }
    }
}

NnNews::NnNews()
    : ResultCallback(nullptr)
    , ResultContext(nullptr)
    , TickerText(nullptr)
    , TickerWidths(nullptr)
    , TickerLength(0)
    , TickerScroll(0.0f)
    , TickerIndex(0)
    , VideoTexture(nullptr)
    , ScrollOffset(0)
    , Field_0x57c(0)
    , Selected(0)
    , News(nullptr)
    , NewsCount(0)
    , IconPaths(nullptr)
    , IconPathCount(0)
    , Hovered(-1)
    , Clicked(0)
    , bOpenSoundPlaying(0)
{
    // FUN_004a3ff0. MaterialGroup (+0x55c) is not written.
    OpenSound.clear();
    VideoMaterial.Reset(); // FUN_0042cc30
}

NnNews::~NnNews()
{
    // FUN_004a4110.
    if (bOpenSoundPlaying != 0)
        StopClientSoundArch(LookupClientSoundArch(&OpenSound));
    delete[] News;
    ::operator delete(static_cast<void*>(IconPaths));
    free(TickerText);
    ::operator delete(static_cast<void*>(TickerWidths));
    TickerText = nullptr;
    TickerWidths = nullptr;
    TickerLength = 0;
    malloc_strfree(&VideoTexture);
    VideoMaterial.Release(); // FUN_0042cc50
    VideoTexture = nullptr;
    VideoMaterial.Reset();   // FUN_0042cc30
    DALib::MaterialLib->DeleteGroup(MaterialGroup); // slot 10
    MaterialGroup = -1;
}

uintptr_t NnNews::RowDword(size_t ArrayOffset, int Index) const
{
    uintptr_t Value = 0;
    unsigned char const* const Bytes = reinterpret_cast<unsigned char const*>(this);
    memcpy(&Value, Bytes + ArrayOffset + static_cast<ptrdiff_t>(Index) * 4, sizeof(Value));
    return Value;
}

void NnNews::RevealSelected()
{
    // Inlined at 0x004a4ca6 and 0x004a4f54. Retail does not test Selected first (a click below
    // the last item reveals past the array).
    NewsBroadcastProperties const& Item = News[Selected];
    KnowledgeMapRevealFmtStr(&Item.Headline); // +0x28, FUN_00459950
    KnowledgeMapRevealFmtStr(&Item.Text);     // +0x40
}

bool NnNews::Create(Win3D* NewParent, void* Data)
{
    // LAB_004a4250 (RET 8). Extents, hit offset and the base position.
    HalfExtents.x = 0.43f; // +0xb4
    HalfExtents.y = 0.3f;
    HalfExtents.z = 0.0f;
    HitOffset.x = 0.0f;    // +0xc0
    HitOffset.y = 0.033f;
    HitOffset.z = -1.0f;
    BasePosition[0] = 0.0f;
    BasePosition[1] = -0.1f;
    BasePosition[2] = -1.498f;
    float const X = BasePosition[0];
    float const Y = BasePosition[1];
    float const Z = BasePosition[2];
    Vector const Zero = {0.0f, 0.0f, 0.0f};

    // FUN_00423b30 adds a zero offset here.
    {
        Vector const Position = {X + 0.0f, Y + 0.0f, Z + 0.0f};
        Background = CreateStaticControl("NewsBackground",
                                         "Interface\\NeuroNet\\NewsVendor\\NEWS_background.cmp",
                                         &Position, nullptr, &Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, &Zero);
    }
    {
        Vector const Position = {X + -0.215f, Y + 0.217f, Z + 0.0f};
        CreateStaticControl("NewsHeadlinesFrame",
                            "Interface\\NeuroNet\\NewsVendor\\NEWS_headlineframe.3db", &Position,
                            nullptr, &Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, &Zero);
    }
    {
        Vector const Position = {X - 0.006f, Y - 0.149f, Z};
        CreateStaticControl("NewsStoryFrame", "Interface\\NeuroNet\\NewsVendor\\NEWS_textframe.3db",
                            &Position, nullptr, &Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, &Zero);
    }
    {
        Vector const Position = {X + 0.364f, Y + 0.24f, Z};
        CreateStaticControl("NewsVideoFrame",
                            "Interface\\NeuroNet\\NewsVendor\\NEWS_videowindow.3db", &Position,
                            nullptr, &Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, &Zero);
    }
    {
        Vector const Position = {X - 0.003f, Y + 0.471f, Z};
        CreateTextControl("NewsTitle", "PageHeader", &Position, nullptr, 1.22f, 0.03f, 2.0f, 0x4ab, 1,
                          0, 0);
    }
    MaterialGroup = MiscUtilLoadMaterialLibrary(kVideoScreenMesh);
    {
        Vector const Position = {X + 0.364f, Y + 0.24f, Z};
        CreateStaticControl("NewsVideoScreen", kVideoScreenMesh, &Position, nullptr, &Zero, 0, 0, 0, 1,
                            1, 0, 1, 0, 0, 0, &Zero);
    }
    {
        Vector const Position = {X + 0.591f, Y + 0.461f, Z};
        Vector const Half = {0.02f, 0.02f, 0.0f};
        CloseButton = CreateButtonControl("NewsCloseButton", "Interface\\NeuroNet\\Inventory\\x.3db",
                                          nullptr, "ui_select_item", nullptr, &Position, nullptr,
                                          &Half, 0, 0, 0, 0, 0x42a, 0, 0);
    }

    float const Top[3] = {X - 0.39f, Y + 0.36f, Z};
    char Name[0x100];
    for (int Row = 0; Row < 5; ++Row)
    {
        // Stored once as float; the product and the difference are exact in double.
        float const RowY = static_cast<float>(static_cast<double>(Top[1]) -
                                              static_cast<double>(Row) * static_cast<double>(0.0615f));

        sprintf(Name, "NewsHeadlineIcon%d", Row);
        {
            Vector const Position = {Top[0] - 0.125f, RowY, Top[2]};
            HeadlineIcons[Row] = CreateStaticControl(Name, kHeadlineIconMesh, &Position, nullptr, &Zero,
                                                     0, 0, 0, 1, 0, 0, 1, 0, 0, 0, &Zero);
        }
        sprintf(Name, "NewsHeadlineIconFrame%d", Row);
        {
            Vector const Position = {Top[0] - 0.125f, RowY, Top[2]};
            Vector const Half = {0.03f, 0.03f, 0.0f};
            IconFrames[Row] = CreateWireButtonControl(Name, "ui_select_item", &Position, &Half, 0, 0,
                                                      0x42b, 0, 0);
        }
        sprintf(Name, "NewsHeadlineBox%d", Row);
        {
            Vector const Position = {Top[0] + 0.19f, RowY, Top[2]};
            Vector const Half = {0.245f, 0.031f, 0.0f};
            HeadlineBoxes[Row] = CreateWireButtonControl(Name, nullptr, &Position, &Half, 0, 0, 0x42b, 0,
                                                         0);
        }
        sprintf(Name, "NewsHeadlineHeadline%d", Row);
        {
            HeadlinePositions[Row].x = Top[0] - 0.045f;
            HeadlinePositions[Row].y = RowY + 0.036f;
            HeadlinePositions[Row].z = Top[2];
            Headlines[Row] = CreateTextControl(Name, "NewsMission", &HeadlinePositions[Row], nullptr,
                                               0.475f, 0.03f, 1.5f, 0, 0, 0, 1);
        }
        // The headline goes multi-line (the factory's result is not tested).
        {
            FFontString* const Text = static_cast<NnTextControl*>(Headlines[Row])->GetFontString();
            if (Text->bMultiLine != 1)
            {
                Text->bMultiLine = 1; // +0x30
                Text->LayoutGen = 0;  // +0x28
            }
        }
    }

    {
        Vector const Position = {X + 0.09f, Y + 0.397f, Z};
        Scroll = CreateUIScroll("NewsHeadlineScroll", &Position, 0.019f, 0.317f, 0, 5, 5, 0);
    }
    // Style *DAT_00611d0c; 2.7f is DAT_005d3fb0.
    {
        Vector const Position = {X + 0.105f, Y - 0.342f, Z};
        Ticker = CreateTextControl("NewsTicker", G_NewsTickerStyle, &Position, nullptr, 1.35f, 0.03f,
                                   2.7f, 0, 0, 0, 0);
    }
    // Fixed x / y, the base's z.
    {
        Vector const Position = {-0.535f, -0.111f, BasePosition[2]};
        Vector const Half = {0.03f, 0.03f, 0.0f};
        StoryIcon = CreateStaticControl("NewsStoryIcon", kHeadlineIconMesh, &Position, nullptr, &Half, 0,
                                        0, 0, 1, 0, 0, 1, 0, 0, 0, &Zero);
    }
    // Retail sprintf's the name into the buffer first (no conversion).
    sprintf(Name, "NewsStoryHeadline");
    {
        Vector const Position = {X + 0.02f, Y - 0.006f, Z};
        StoryHeadline = CreateTextControl(Name, "Normal", &Position, nullptr, 1.18f, 0.03f, 1.8f, 0, 1,
                                          0, 0);
    }
    {
        Vector const Position = {X - 0.556f, Y - 0.051f, Z};
        StoryText = CreateRichTextWin("NewsStoryText", 1.08f, 0.27f, &Position, true);
    }

    DrawLayer = 0x64;
    SetOffset(0.0f, 0.0f, 0.0f);
    SetPosition(&Position);

    IconPathCount = 7;
    IconPaths = static_cast<char const**>(::operator new(0x1c));
    for (int Index = 0; Index < 7; ++Index)
        IconPaths[Index] = kNewsIconMeshes[Index];
    for (int Index = 0; Index < 7; ++Index)
        IconIds[Index] = kNewsIconIds[Index];

    LoadNews();
    BuildTicker();
    RefreshRows();
    Field_0x57c = 0;
    ShowSelected();
    RevealSelected();

    // The open sound is stopped if it is still going, then (re)started.
    HudPushCancelHandler(AsHudCmdHandler(), 0); // FUN_00576360
    if (bOpenSoundPlaying != 0)
        StopClientSoundArch(LookupClientSoundArch(&OpenSound));
    OpenSound.Id = CreateID("news_vendor_open");
    PlayClientSoundArch(LookupClientSoundArch(&OpenSound), nullptr,
                        nullptr, 0);
    bOpenSoundPlaying = 1;

    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

void NnNews::LoadNews()
{
    // FUN_004a4fd0: the news of the base the player is in (DAT_0066873c).
    TMirrorPropertyBucket<NewsBroadcastProperties>* const Bucket =
        GetClientLocationBaseNews(GetClientLocationCurrentBase());
    int Count = 0;
    MetaNode<NewsBroadcastProperties>* Node = nullptr;
    while (Bucket->List.traverse(Node, true))
        ++Count;

    News = new NewsBroadcastProperties[Count];
    NewsCount = Count;

    unsigned char* const Taken = static_cast<unsigned char*>(::operator new(static_cast<size_t>(Count)));
    if (Count > 0)
        memset(Taken, 0, static_cast<size_t>(Count));

    // Four passes over the nodes in list order, each copying the not-yet-taken nodes that pass
    // its test into the next slot. Retail finds node i by walking from the head for each i; one
    // walk per pass visits the same nodes in the same order.
    int Next = 0;
    for (int Pass = 0; Pass < 4; ++Pass)
    {
        Node = nullptr;
        for (int Index = 0; Index < Count; ++Index)
        {
            Bucket->List.traverse(Node, true);
            if (Taken[Index] != 0)
                continue;
            NewsBroadcastProperties const& Item = *Node->Data;
            bool bTake = false;
            switch (Pass)
            {
            case 0: bTake = Item.bAutoselect; break;      // byte +9
            case 1: bTake = (Item.Icon == 1); break;      // dword +0xc
            case 2: bTake = Item.bBreaking; break;        // byte +8
            default: bTake = true; break;
            }
            if (!bTake)
                continue;
            News[Next] = Item;
            ++Next;
            Taken[Index] = 1;
        }
    }

    ScrollOffset = 0;
    Field_0x57c = 0;
    Selected = 0;
    Scroll->SetProperty(kScrollPropSetRange, 0, static_cast<uintptr_t>(NewsCount - 1));
    ::operator delete(static_cast<void*>(Taken));
}

void NnNews::RefreshRows()
{
    // FUN_004a5270.
    wchar_t Buffer[0x400];
    int Row = 0;
    for (int Index = ScrollOffset; Index < NewsCount; ++Index)
    {
        NewsBroadcastProperties const& Item = News[Index];
        WinControl* const Controls[4] = {HeadlineIcons[Row], IconFrames[Row], HeadlineBoxes[Row],
                                         Headlines[Row]};

        for (int Slot = 0; Slot < 4; ++Slot)
            Controls[Slot]->SetProperty(kWinPropDisable, 0, 0);
        for (int Slot = 0; Slot < 4; ++Slot)
            Controls[Slot]->SetProperty(kWinPropHide, 0, 0);

        char const* Path = nullptr;
        int Ids = 0;
        NewsIconFor(*this, Item.Icon, &Path, &Ids);
        HeadlineIcons[Row]->SetProperty(kWinPropMesh, PointerArg(Path), 0);
        IconFrames[Row]->SetProperty(kWinPropToolTip, static_cast<uintptr_t>(Ids), 0);

        FmtStrSprintf(&Item.Headline, Buffer, 0x400); // +0x28
        Headlines[Row]->SetProperty(kTextPropSetText, PointerArg(Buffer), 0);

        // A headline of fewer than two lines (unsigned) moves 0.016 down. For each of Create's
        // five HeadlinePositions the double difference rounds to the same float as the extended
        // one.
        Vector const* const Stored = &HeadlinePositions[Row];
        Vector Moved = {Stored->x, Stored->y, Stored->z};
        unsigned const Lines = static_cast<unsigned>(
            FontStringChildCount(static_cast<NnTextControl*>(Headlines[Row])->GetFontString()));
        if (Lines < 2)
            Moved.y = static_cast<float>(static_cast<double>(Stored->y) - 0.016);
        Headlines[Row]->SetProperty(kWinPropOffset, PointerArg(&Moved), 0);

        uintptr_t const bSelected = (Index == Selected) ? 1u : 0u;
        IconFrames[Row]->SetProperty(kWinPropLit, bSelected, 0);
        HeadlineBoxes[Row]->SetProperty(kWinPropLit, bSelected, 0);

        ++Row;
        if (Row >= 5)
            return;
    }

    // The unused rows.
    for (; Row < 5; ++Row)
    {
        WinControl* const Controls[4] = {HeadlineIcons[Row], IconFrames[Row], HeadlineBoxes[Row],
                                         Headlines[Row]};
        for (int Slot = 0; Slot < 4; ++Slot)
            Controls[Slot]->SetProperty(kWinPropDisable, 1, 0);
        for (int Slot = 0; Slot < 4; ++Slot)
            Controls[Slot]->SetProperty(kWinPropHide, 1, 0);
        HeadlineIcons[Row]->SetProperty(kWinPropMesh, PointerArg(IconPaths[0]), 0);
        HeadlineBoxes[Row]->SetProperty(kTextPropSetIds, 0, 0);
        Headlines[Row]->SetProperty(kTextPropSetIds, 0, 0);
    }
}

void NnNews::ShowSelected()
{
    // FUN_004a5580.
    if (Selected == -1)
        return;

    wchar_t Buffer[0x1000];
    NewsBroadcastProperties const& Item = News[Selected];
    FmtStrSprintf(&Item.Headline, Buffer, 0x1000); // +0x28
    StoryHeadline->SetProperty(kTextPropSetText, PointerArg(Buffer), 0);

    // +0x40 as the story.
    FmtStrSprintf(&Item.Text, Buffer, 0x1000);
    {
        RenderDisplayList Text;
        {
            StyleNode Style(kTextStyleBody);
            RdlAppendNode(Text, Style.Clone());
        }
        AppendTextMaybeXmlRdl(Buffer, static_cast<unsigned>(wcslen(Buffer)), Text, false);
        StoryText->SetScrollOrigin(0, 0); // FUN_005798b0
        StoryText->ClearContent();        // FUN_0057b370
        StoryText->AppendContent(Text);   // FUN_0057b3e0

        // The logo texture is swapped into "MV_vidmaterial" unless it already is.
        char const* const Texture = News[Selected].get_logo_texture();
        if (VideoTexture == nullptr || strcmp(Texture, VideoTexture) != 0)
        {
            malloc_strfree(&VideoTexture);
            VideoMaterial.Release(); // FUN_0042cc50
            VideoTexture = nullptr;
            VideoMaterial.Reset();   // FUN_0042cc30
            malloc_strcpy(&VideoTexture, Texture);
            if (!VideoMaterial.Init(kVideoMaterialName))
                FDUMP(kFdumpError, kNoMaterialInNewsVendor, kNewsVendorCpp, 0x426, kVideoMaterialName);
            VideoMaterial.Swap(VideoTexture, kVideoTextureLibrary);
        }

        // Not shown or hidden here.
        char const* Path = nullptr;
        int Ids = 0;
        NewsIconFor(*this, Item.Icon, &Path, &Ids);
        StoryIcon->SetProperty(kWinPropMesh, PointerArg(Path), 0);
        StoryIcon->SetProperty(kWinPropToolTip, static_cast<uintptr_t>(Ids), 0);
    }
}

void NnNews::BuildTicker()
{
    // FUN_004a5890.
    wchar_t TickerBuffer[0x400];
    wchar_t Part[0x100];
    wchar_t Space[0x100];        // the headline separator shares it
    wchar_t Separator[0x100];
    wchar_t Number[0x100];
    TickerBuffer[0] = L'\0';
    int Total = 0;

    // The base's commodities (category 0).
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    std::list<FClientMarketGood const*> Goods;
    ClientMarketCollectCategory(Market, &Goods, 0);

    do
    {
        // Every news item with byte +0x08 set, when it still fits.
        for (int Index = 0; Index < NewsCount; ++Index)
        {
            NewsBroadcastProperties const& Item = News[Index];
            if (!Item.bBreaking)
                continue;
            FmtStrSprintf(&Item.Headline, Part, 0x100);
            wcscpy(Space, L" --- ");
            int const Length = static_cast<int>(wcslen(Space)) + static_cast<int>(wcslen(Part)) + Total;
            if (Length >= 0x400)
                continue;
            wcscat(TickerBuffer, Part);
            wcscat(TickerBuffer, Space);
            Total = Length;
        }

        // Every commodity the base trades (TransactionType 2 or 0) with a positive price and a
        // name: "NAME price --- ", when it still fits. Retail: Good is not null-tested.
        for (std::list<FClientMarketGood const*>::const_iterator It = Goods.begin();
             It != Goods.end(); ++It)
        {
            FClientMarketGood const* const Entry = *It;
            int const Type = static_cast<int>(Entry->Info.Type);
            if (Type != TransactionSaleOnly && Type != TransactionBuyAndSell)
                continue;
            unsigned const GoodId = Entry->Info.GoodId;
            int const Price = static_cast<int>(
                floor(static_cast<double>(ClientMarketGetPrice(Market, GoodId))));
            GoodInfo const* const Good = GoodList::find_by_id(GoodId);
            if (Price <= 0)
                continue;
            if (Good->IdsName == 0) // +0x88
                continue;
            if (GetIdsStringWWithRawFallback(static_cast<unsigned>(Good->IdsName), Part, 0x100) == 0)
            {
                ID_String GoodName; // the ID_String at GoodInfo +0x48
                GoodName.Id = Good->Id;
                FDUMP(kFdumpWarning, kNoStringForGood, kNewsVendorCpp, 0x35f, Good->IdsName,
                      GoodName.get_string());
            }
            // wcslen is taken again every pass, as in retail.
            for (size_t Char = 0; Char < wcslen(Part); ++Char)
                Part[Char] = static_cast<wchar_t>(towupper(Part[Char]));
            wcscpy(Space, L" ");
            _itow(Price, Number, 10);
            wcscpy(Separator, L" --- ");
            int const Length = static_cast<int>(wcslen(Separator)) + static_cast<int>(wcslen(Number)) +
                               static_cast<int>(wcslen(Space)) + static_cast<int>(wcslen(Part)) + Total;
            if (Length >= 0x400)
                continue;
            wcscat(TickerBuffer, Part);
            wcscat(TickerBuffer, Space);
            wcscat(TickerBuffer, Number);
            wcscat(TickerBuffer, Separator);
            Total = Length;
        }
    } while (Total != 0 && Total < 0x80);

    // A width per character from the ticker font (IFont slot 10).
    TickerText = _wcsdup(TickerBuffer);
    TickerLength = static_cast<int>(wcslen(TickerText));
    TickerWidths = static_cast<int*>(::operator new(static_cast<size_t>(TickerLength) * 4));
    FindMenuFontImp(G_NewsTickerStyle)->GetCharWidths(TickerText, -1, TickerWidths);

    TickerShow(0, 0.0f);
}

void NnNews::TickerShow(int Start, float Offset)
{
    // FUN_004a5ca0 (RET 8).
    if (TickerLength == 0)
        return;

    // Every value here is exact in double (an integer-valued float, integer widths, a 24-bit
    // float times a small integer).
    double X = -static_cast<double>(Offset);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: the ticker is interface, as wide as on a 4:3 surface of this height.
    double const Limit = WidescreenEquivalentSurfaceWidth() * static_cast<double>(0.73125f);
#else
    double const Limit = static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) *
                         static_cast<double>(0.73125f);
#endif

    // Characters from Start (wrapping) while fewer than 0x100 are taken and X is below Limit
    // (unordered stops).
    int Count = 0;
    if (TickerLength > 0)
    {
        do
        {
            int const Index = (Count + Start) % TickerLength;
            if (Count >= 0x100)
                break;
            if (!(X < Limit))
                break;
            TickerView[Count] = TickerText[Index];
            X += static_cast<double>(TickerWidths[Index]);
            ++Count;
        } while (Count < TickerLength);
    }

    // 0x23 takes the left trim (Offset) and right trim (Overhang) as float bits. Retail writes
    // the overhang over Start's stack slot (0x004a5d35) but still reads the original Offset.
    TickerView[Count] = L'\0';
    float const Overhang = static_cast<float>(X - Limit);
    Ticker->SetProperty(kTextPropSetText, PointerArg(TickerView), 0);
    Ticker->SetProperty(0x22, 0, 1);
    Ticker->SetProperty(0x23, FloatArg(Offset), FloatArg(Overhang));
}

void NnNews::Draw()
{
    // FUN_004a5d80. The hovered row's frame or box draws at 0x65, the selected row's at 0x66
    // (gated on Clicked, indexed by Selected; the second test wins). The rows are not
    // range-checked.
    int const SelectedRow = Selected - ScrollOffset;
    int const HoveredRow = Hovered - ScrollOffset;
    size_t const FramesOffset = offsetof(NnNews, IconFrames);
    size_t const BoxesOffset = offsetof(NnNews, HeadlineBoxes);
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

    // Only the box column is joined.
    WireControlsJoinColumn(HeadlineBoxes, 5); // FUN_005a4b50
    Background->DrawLayer = 0x63;
    StoryText->DrawLayer = 0x67;

    WinControl::Draw();     // FUN_005a0760
    NnMontageFlushButtons(); // FUN_00559c20
    ButtonMontageEnsure();   // FUN_00559c10 (tail jump)
}

void NnNews::Update()
{
    // LAB_004a5e50: the frame time scrolls the ticker 50 pixels a second.
    double const Dt = GetClientFrameTimeCapped(); // FUN_0042d680
    if (TickerLength != 0)
    {
        TickerScroll = static_cast<float>(Dt * 50.0 + static_cast<double>(TickerScroll));

        // Whole characters scrolled past are dropped.
        int Width = TickerWidths[TickerIndex];
        if (static_cast<double>(TickerScroll) >= static_cast<double>(Width))
        {
            do
            {
                TickerScroll = static_cast<float>(static_cast<double>(TickerScroll) -
                                                  static_cast<double>(Width));
                TickerIndex = TickerIndex + 1;
                if (!(TickerIndex < TickerLength))
                    TickerIndex = 0;
                Width = TickerWidths[TickerIndex];
            } while (static_cast<double>(Width) <= static_cast<double>(TickerScroll));
        }

        // The remainder rounded to whole pixels.
        float const Offset = static_cast<float>(
            static_cast<int>(floor(static_cast<double>(TickerScroll) + 0.5)));
        TickerShow(TickerIndex, Offset);
    }
    Win3D::Update(); // FUN_005a17b0 (tail jump)
}

intptr_t NnNews::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004a4d60 (RET 0xc): byte table 0x004a4fa0 over Msg - 0x26. Every path returns 0.
    switch (Msg)
    {
    case kWinPropCloseFromOutside:
    {
        // Closed from outside: no callback, no NavBar. (Retail tests `this` first.)
        Win3DDeleteTree(this);
        return 0;
    }
    case kWinNotifyScrollValue:
    case kWinNotifyHighlight:
    case kWinNotifyUnhighlight:
    {
        // Retail's case 0x3e falls through into the hover walk below.
        if (Msg == kWinNotifyScrollValue && A == PointerArg(Scroll) && static_cast<int>(B) != ScrollOffset)
        {
            ScrollOffset = static_cast<int>(B);
            RefreshRows();
        }

        // Hovered is written (0x2c) or cleared (anything else) on every pass until the control
        // is found, as in retail.
        int Row = 0;
        for (; Row < 5; ++Row)
        {
            int const Item = ScrollOffset + Row;
            if (Msg == kWinNotifyHighlight)
                Hovered = Item;
            else if (Hovered == Item)
                Hovered = -1;
            if (A == PointerArg(IconFrames[Row]) || A == PointerArg(HeadlineBoxes[Row]))
                break;
        }
        if (Row == 5)
            return 0;

        // The row's other wire button gets the same message.
        if (A != PointerArg(IconFrames[Row]))
            IconFrames[Row]->SetProperty(Msg, 0, 0);
        if (A != PointerArg(HeadlineBoxes[Row]))
            HeadlineBoxes[Row]->SetProperty(Msg, 0, 0);
        return 0;
    }
    case kWinPropPlaceAtOffset:
    {
        float const* const Target = reinterpret_cast<float const*>(A);
        SetOffset(Target[0], Target[1], Target[2]);
        SetPosition(&Position);
        return 0;
    }
    case kWinNotifyClick:
    {
        if (A == PointerArg(CloseButton))
        {
            if (ResultCallback != nullptr)
                ResultCallback(ResultContext);
            NavBarCloseService(false); // FUN_004424f0(0)
            Win3DDeleteTree(this);
            return 0;
        }

        // A row clicked. No match leaves Clicked at -1 and selects ScrollOffset - 1, as retail
        // does.
        Clicked = -1;
        for (int Row = 0; Row < 5; ++Row)
        {
            if (A == PointerArg(IconFrames[Row]) || A == PointerArg(HeadlineBoxes[Row]))
            {
                Clicked = Row;
                break;
            }
        }
        Selected = ScrollOffset + Clicked;
        RefreshRows();
        Field_0x57c = 0;
        ShowSelected();
        RevealSelected();
        return 0;
    }
    default:
        return 0;
    }
}

bool NnNews::OnEvent(unsigned MsgId)
{
    // LAB_004a5f40 (RET 4), on the +0x32c face: USER_STACKABLE_CANCEL (0x10) closes the window like the
    // close button. (Retail tests the adjusted `this` for null.)
    if (MsgId != USER_STACKABLE_CANCEL)
        return false;
    if (ResultCallback != nullptr)
        ResultCallback(ResultContext);
    NavBarCloseService(false); // FUN_004424f0(0)
    Win3DDeleteTree(this);
    return true;
}
