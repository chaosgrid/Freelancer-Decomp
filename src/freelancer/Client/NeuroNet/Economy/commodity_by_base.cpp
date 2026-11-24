// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/Economy/commodity_by_base.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerVisitFlagsRetail (FUN_004c4da0)
#include "Core/globals.h"                // IsClientServerTickPaused (FUN_0042d730)
#include "Space/HUD/hud_command_dispatch.h"   // HudPostCommand (FUN_005763b0)
#include "Space/HUD/hud_target.h"             // HudIsMousePickBlocked (FUN_0045a740)
#include "Common/Fonts/ids_rdl.h"                // AppendTextMaybeXmlRdl (FUN_0057e2c0)
#include "Common/Fonts/ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Common/input_manager.h"          // GetClientSoftwareCursor, GetClientMouseCursorDrawFlag
#include "Common/misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "GameFlow/nav_bar.h"                // NavBarHasServiceWindow (FUN_00442720), NavBarIsAnimating (FUN_00442790)
#include "NeuroNet/nn_inventory.h"           // IsPlayerDockedRetail (FUN_004c4810)
#include "NeuroNet/nn_modal.h"               // NnFloatBits, NnMainButtonHasActivePanel, G_bNnScreenUp
#include "NeuroNet/NavMap/nn_navmap.h"              // NavMapRequestFocus (FUN_004a2aa0)
#include "NeuroNet/nn_text_scratch.h"        // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "NeuroNet/nn_ui_pulse.h"            // NnUiPulseBlendArgb (FUN_00561da0), palette colours
#include "Space/player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "Space/Cameras/space_camera.h"           // FSpaceCamera
#include "Space/space_letterbox.h"        // IsLetterboxVisible (FUN_0041a3e0)
#include "Interface/ui_scroll.h"
#include "Interface/win3d.h"                  // Win3DFindByName, Win3DDeleteTree, Win3DGetCamera
#include "Interface/win_factory.h"

#include "common_archetype.h"
#include "common_csys.h"            // Acquire3DMathEngine (FUN_005b73e0)
#include "common_goods.h"
#include "common_universe.h"
#include "x86math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

std::vector<FCommodityByBaseRow> G_CommodityByBaseRows;
std::map<unsigned, std::wstring> G_CommodityByBaseBaseNames;
std::map<unsigned, std::wstring> G_CommodityByBaseSystemNames;

// Retail DAT_0066dacc -- WINFACTORY<NN_CommodityByBase> static (LAB_0046ed70).
WINFACTORY<NnCommodityByBase> G_NnCommodityByBaseFactory("NN_CommodityByBase");

namespace
{
    // DAT_0066db04 -- this TU's 3DMathEngine, acquired on first use by Update.
    I3DMathEngine* G_CommodityByBaseMath = nullptr;

    char const kCommodityByBaseMesh[] = "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp"; // 005ce994
    char const kCommodityByBaseCloseMesh[] = "Interface\\NeuroNet\\Inventory\\x.3db";           // 005d01d0

    // The column layout Create pushes as immediates: each column's x (shared by its title and
    // its 13 line texts), the title row's y, and the texts' widths, height and font scale.
    float const kCbbPriceX = NnFloatBits(0xbec7ae14);      // -0.39
    float const kCbbBuySellX = NnFloatBits(0xbe8b4396);    // -0.272
    float const kCbbBaseX = NnFloatBits(0xbdcac083);       // -0.099
    float const kCbbSystemX = NnFloatBits(0x3e343958);     // 0.176
    float const kCbbTitleY = NnFloatBits(0x3e428f5c);      // 0.19
    float const kCbbPriceWidth = NnFloatBits(0x3dcccccd);  // 0.1 (the buy / sell column too)
    float const kCbbBaseWidth = NnFloatBits(0x3e851eb8);   // 0.26
    float const kCbbSystemWidth = NnFloatBits(0x3e4ccccd); // 0.2
    float const kCbbTextHeight = NnFloatBits(0x3cf5c28f);  // 0.03
    float const kCbbFontScale = NnFloatBits(0x3ff33333);   // 1.9

    // FUN_0046fc90 -- true when the window may not stay open: docked with a base-service window
    // or a moving NavBar, in space without a player ship, a "NeuroNetMissionMessage",
    // "NN_Trade" or "NN_Scanner" window, an NN_MainButton panel, a NeuroNet screen, a held
    // server tick, the letterbox, or the software cursor hidden.
    bool CommodityByBaseMustClose()
    {
        if (IsPlayerDockedRetail()) // FUN_004c4810
        {
            if (NavBarHasServiceWindow()) // FUN_00442720
                return true;
            if (NavBarIsAnimating()) // FUN_00442790
                return true;
        }
        else if (GetPlayerObjRW() == nullptr) // FUN_0054baf0
        {
            return true;
        }
        if (Win3DFindByName("NeuroNetMissionMessage") != nullptr)
            return true;
        if (Win3DFindByName("NN_Trade") != nullptr)
            return true;
        if (Win3DFindByName("NN_Scanner") != nullptr)
            return true;
        if (NnMainButtonHasActivePanel()) // FUN_0045a470
            return true;
        if (G_bNnScreenUp) // DAT_0067dcc8
            return true;
        if (IsClientServerTickPaused()) // FUN_0042d730
            return true;
        if (IsLetterboxVisible()) // FUN_0041a3e0
            return true;
        if (GetClientMouseCursorDrawFlag() == 0) // FUN_0041dd90
            return true;
        return false;
    }

    // DAT_005d0214 L"???" -- what every name lookup below falls back to on a miss.
    wchar_t const kCommodityUnknownName[] = L"???";

    // The inlined `find(key)` + `== end ? L"???" : node+0x10` of FUN_00470040 / FUN_004704d0.
    wchar_t const* CommodityByBaseName(std::map<unsigned, std::wstring> const& Names,
                                       unsigned SpaceObjectId)
    {
        std::map<unsigned, std::wstring>::const_iterator Found = Names.find(SpaceObjectId);
        if (Found == Names.end())
            return kCommodityUnknownName;
        return Found->second.c_str();
    }

    void SwapCommodityByBaseRows(FCommodityByBaseRow& A, FCommodityByBaseRow& B)
    {
        FCommodityByBaseRow const Temp = A;
        A = B;
        B = Temp;
    }
}

bool IsCommodityByBaseListedName(int Ids)
{
    // FUN_004a2310, compares in retail's order.
    if (Ids == 0x3fc27 || Ids == 0x3fc2e || Ids == 0x3fc28 || Ids == 0xeb41 || Ids == 0 ||
        Ids == 0xeb3f || Ids == 0x300ea || Ids == 0x30052 || Ids == 0x3009c ||
        Ids == 0x3009d || Ids == 0x300bf)
        return false;
    return true;
}

void BuildCommodityByBaseRows(unsigned ArchId)
{
    // FUN_0046f6d0.
    G_CommodityByBaseRows.clear();
    G_CommodityByBaseBaseNames.clear();
    G_CommodityByBaseSystemNames.clear();

    for (Universe::IBase* Base = Universe::GetFirstBase(); Base != nullptr;
         Base = Universe::GetNextBase())
    {
        unsigned const SpaceObjectId = Base->SpaceObjectId;
        Universe::ISystem const* System = Universe::get_system(Base->SystemId);
        if (System == nullptr || SpaceObjectId == 0)
            continue;

        if (!IsCommodityByBaseListedName(Base->StridName))
            continue;

        // The player's visit flags (nothing written on a miss) OR the object's own visit byte;
        // bit 4 (known) is required.
        unsigned char VisitFlags = 0;
        GetPlayerVisitFlagsRetail(SpaceObjectId, &VisitFlags, 0, 0);
        VisitFlags = static_cast<unsigned char>(VisitFlags | Base->SpaceObjectVisit);
        if ((VisitFlags & 4) == 0)
            continue;

        if (G_CommodityByBaseSystemNames.find(SpaceObjectId) == G_CommodityByBaseSystemNames.end())
        {
            GetIdsStringWWithRawFallback((unsigned)System->GetStridName(),
                                         G_NnTextScratchB, G_NnTextScratchCapacity);
            G_CommodityByBaseSystemNames.insert(
                std::pair<unsigned const, std::wstring>(SpaceObjectId, G_NnTextScratchB));
        }

        if (G_CommodityByBaseBaseNames.find(SpaceObjectId) == G_CommodityByBaseBaseNames.end())
        {
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Base->StridName),
                                         G_NnTextScratchB, G_NnTextScratchCapacity);
            G_CommodityByBaseBaseNames.insert(
                std::pair<unsigned const, std::wstring>(SpaceObjectId, G_NnTextScratchB));
        }

        BaseData* Data = BaseDataList_get()->get_base_data(Base->NicknameId);
        if (Data == nullptr)
            continue;
        std::map<unsigned, MarketGoodInfo> const* Market = &Data->Market;

        // Without a market entry the good list's price is used and "sells" is false -- unless
        // the archetype has no good at all, which leaves price 0 and "sells" true.
        unsigned const GoodId = Arch2Good(ArchId);
        int Price = 0;
        bool bBaseSells = true;
        std::map<unsigned, MarketGoodInfo>::const_iterator Entry = Market->find(GoodId);
        if (Entry != Market->end())
        {
            Price = static_cast<int>(Entry->second.Price);
            if (Entry->second.MaxInventory == 0)
                bBaseSells = false;
        }
        else
        {
            GoodInfo const* Good = GoodList::find_by_archetype(ArchId);
            if (Good != nullptr)
            {
                Price = static_cast<int>(Good->Price);
                bBaseSells = false;
            }
        }

        FCommodityByBaseRow Row;
        Row.SpaceObjectId = SpaceObjectId;
        Row.Price = Price;
        Row.bBaseSells = bBaseSells;
        G_CommodityByBaseRows.push_back(Row);
    }
}

void SortCommodityByBaseRows(int Column)
{
    // FUN_00470040 -- a bubble sort until a pass swaps nothing.
    int const Count = static_cast<int>(G_CommodityByBaseRows.size());
    int const Last = Count - 1;
    if (Column != 1 && Column != 2 && Column != 3 && Column != 4)
        return;
    if (Last <= 0)
        return;

    bool bSorted = false;
    while (!bSorted)
    {
        bSorted = true;
        for (int Index = 0; Index < Last; ++Index)
        {
            FCommodityByBaseRow& A = G_CommodityByBaseRows[Index];
            FCommodityByBaseRow& B = G_CommodityByBaseRows[Index + 1];
            bool bSwap = false;

            if (Column == 2)
            {
                wchar_t const* NameB = CommodityByBaseName(G_CommodityByBaseBaseNames, B.SpaceObjectId);
                wchar_t const* NameA = CommodityByBaseName(G_CommodityByBaseBaseNames, A.SpaceObjectId);
                if (_wcsicmp(NameA, NameB) > 0)
                    bSwap = true;
            }
            else if (Column == 1)
            {
                if (A.Price > B.Price)
                {
                    bSwap = false;
                }
                else if (A.Price < B.Price)
                {
                    bSwap = true;
                }
                else
                {
                    wchar_t const* NameB = CommodityByBaseName(G_CommodityByBaseBaseNames, B.SpaceObjectId);
                    wchar_t const* NameA = CommodityByBaseName(G_CommodityByBaseBaseNames, A.SpaceObjectId);
                    if (_wcsicmp(NameA, NameB) > 0)
                        bSwap = true;
                }
            }
            else if (Column == 3)
            {
                wchar_t const* SystemB = CommodityByBaseName(G_CommodityByBaseSystemNames, B.SpaceObjectId);
                wchar_t const* SystemA = CommodityByBaseName(G_CommodityByBaseSystemNames, A.SpaceObjectId);
                int const SystemOrder = _wcsicmp(SystemA, SystemB);
                if (SystemOrder != 0)
                {
                    if (SystemOrder > 0)
                        bSwap = true;
                }
                else if (A.Price > B.Price)
                {
                    bSwap = false;
                }
                else if (A.Price < B.Price)
                {
                    bSwap = true;
                }
                else
                {
                    wchar_t const* NameB = CommodityByBaseName(G_CommodityByBaseBaseNames, B.SpaceObjectId);
                    wchar_t const* NameA = CommodityByBaseName(G_CommodityByBaseBaseNames, A.SpaceObjectId);
                    if (_wcsicmp(NameA, NameB) > 0)
                        bSwap = true;
                }
            }
            else
            {
                if (A.bBaseSells && !B.bBaseSells)
                    bSwap = false;
                else if (!A.bBaseSells && B.bBaseSells)
                    bSwap = true;
                else if (A.Price < B.Price)
                    bSwap = true;
            }

            if (bSwap)
            {
                SwapCommodityByBaseRows(A, B);
                bSorted = false;
            }
        }
    }
}

void AppendCommodityTradeReport(RenderDisplayList* Card, unsigned ArchId)
{
    // FUN_004704d0.
    BuildCommodityByBaseRows(ArchId);
    SortCommodityByBaseRows(1);

    // One line per row, built in DAT_0066fc60 with DAT_0066dc60 as scratch.
    int const Count = static_cast<int>(G_CommodityByBaseRows.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FCommodityByBaseRow const& Row = G_CommodityByBaseRows[Index];

        // FUN_004779a0 prefixes ids 0x2140.
        wcscpy(G_NnTextScratchB, L"\n");
        FormatGroupedNumber(G_NnTextScratchA, Row.Price, -1);
        wcscat(G_NnTextScratchB, G_NnTextScratchA);
        wcscat(G_NnTextScratchB, L" ");

        wcscat(G_NnTextScratchB,
               CommodityByBaseName(G_CommodityByBaseBaseNames, Row.SpaceObjectId));
        wcscat(G_NnTextScratchB, L", ");

        wcscat(G_NnTextScratchB,
               CommodityByBaseName(G_CommodityByBaseSystemNames, Row.SpaceObjectId));

        if (Row.bBaseSells)
        {
            GetIdsStringWWithRawFallback(0x3e5, G_NnTextScratchA, G_NnTextScratchCapacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
        }

        // ids 0x3dd (" (Best Known Price)") on the first row.
        if (Index == 0)
        {
            GetIdsStringWWithRawFallback(0x3dd, G_NnTextScratchA, G_NnTextScratchCapacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
        }

        AppendTextMaybeXmlRdl(G_NnTextScratchB, static_cast<unsigned>(wcslen(G_NnTextScratchB)),
                              *Card, false);
    }
}

NnCommodityByBase::NnCommodityByBase()
{
    // Inlined in FUN_00470790.
}

NnCommodityByBase::~NnCommodityByBase()
{
    // FUN_00470870.
}

void NnCommodityByBase::Close()
{
    // Inlined at each close site.
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Destroy();
    delete this;
}

bool NnCommodityByBase::Create(Win3D* /*NewParent*/, void* Data)
{
    // FUN_0046ef80.
    ArchId = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Data));
    FirstRow = 0;
    SortColumn = 0;

    SetMesh(kCommodityByBaseMesh, 0);
    WinControlHalfExtentsFromMesh(HalfExtents, &MeshHolder);

    float const TitlePosition[3] = {NnFloatBits(0xbed58106), NnFloatBits(0x3e841893), 0.0f};
    WinControl* const Title = CreateTextControl(
        "CommodityByBaseTitle", "PageHeader", TitlePosition, nullptr, NnFloatBits(0x3f5645a2),
        NnFloatBits(0x3d75c28f), NnFloatBits(0x3fc00000), 0, 1, 0, 1);

    // ids 0x3d5, then " - " and the equipment's name when the archetype is equipment.
    GetIdsStringWWithRawFallback(0x3d5, G_NnTextScratchB, G_NnTextScratchCapacity);
    if (ArchId != 0)
    {
        Archetype::Equipment* const Equip = Archetype::GetEquipment(ArchId);
        if (Equip != nullptr)
        {
            wcscat(G_NnTextScratchB, L" - ");
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Equip->IdsName), G_NnTextScratchA,
                                         G_NnTextScratchCapacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
        }
    }

    // CharUpperW is delay-loaded in retail (FUN_005b74d9); the title is not null-tested.
    CharUpperW(G_NnTextScratchB);
    Title->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);

    {
        float const Position[3] = {NnFloatBits(0x3ecd4fdf), NnFloatBits(0x3e72b021), 0.0f};
        float const Extents[3] = {NnFloatBits(0x3c9374bc), NnFloatBits(0x3c9374bc), 0.0f};
        CloseButton = CreateButtonControl("CommodityByBaseCloseButton", kCommodityByBaseCloseMesh,
                                          nullptr, "ui_select_item", nullptr, Position, nullptr,
                                          Extents, 0, 0, 0, 0, 0x409, 0, 0);
    }

    // The four column titles; SetProperty is called without a null test.
    {
        float const Position[3] = {kCbbPriceX, kCbbTitleY, 0.0f};
        PriceTitle = CreateTextControl("CommodityByBasePriceTitle", "SubHeader", Position, nullptr,
                                       kCbbPriceWidth, kCbbTextHeight,
                                       kCbbFontScale, 0x3d6, 0, 0, 1);
        PriceTitle->SetProperty(0x24, 1, 0);
    }
    {
        float const Position[3] = {kCbbBaseX, kCbbTitleY, 0.0f};
        BaseTitle = CreateTextControl("CommodityByBaseBaseTitle", "SubHeader", Position, nullptr,
                                      kCbbBaseWidth, kCbbTextHeight,
                                      kCbbFontScale, 0x3cc, 0, 0, 1);
        BaseTitle->SetProperty(0x24, 1, 0);
    }
    {
        float const Position[3] = {kCbbSystemX, kCbbTitleY, 0.0f};
        SystemTitle = CreateTextControl("CommodityByBaseSystemTitle", "SubHeader", Position,
                                        nullptr, kCbbSystemWidth, kCbbTextHeight,
                                        kCbbFontScale, 0x3cd, 0, 0, 1);
        SystemTitle->SetProperty(0x24, 1, 0);
    }
    {
        float const Position[3] = {kCbbBuySellX, kCbbTitleY, 0.0f};
        BuySellTitle = CreateTextControl("CommodityByBaseBuySellTitle", "SubHeader", Position,
                                         nullptr, kCbbPriceWidth, kCbbTextHeight,
                                         kCbbFontScale, 0x3d8, 0, 0, 1);
        BuySellTitle->SetProperty(0x24, 1, 0);
    }

    // 13 lines. Retail computes Y on x87 with one rounding at the store; the product and the
    // difference are exact in double, so the port's one rounding gives the same float.
    float const LineTop = NnFloatBits(0x3e1cac08);   // DAT_005d0118 0.153
    float const LineStep = NnFloatBits(0x3cf5c28f);  // DAT_005d011c 0.03
    for (int Line = 0; Line < 13; ++Line)
    {
        float const Y = static_cast<float>(static_cast<double>(LineTop) -
                                           static_cast<double>(Line) *
                                               static_cast<double>(LineStep));
        char Name[0xf0];
        FCommodityByBaseRowControls& Controls = Rows[Line];

        sprintf(Name, "CommodityByBasePrice%d", Line);
        float const PricePosition[3] = {kCbbPriceX, Y, 0.0f};
        Controls.Price = CreateTextControl(Name, "ItemName", PricePosition, nullptr,
                                           kCbbPriceWidth, kCbbTextHeight,
                                           kCbbFontScale, 0, 0, 0, 1);
        Controls.Price->SetProperty(0x24, 1, 0);

        sprintf(Name, "CommodityByBuySellPrice%d", Line);
        float const BuySellPosition[3] = {kCbbBuySellX, Y, 0.0f};
        Controls.BuySell = CreateTextControl(Name, "ItemName", BuySellPosition, nullptr,
                                             kCbbPriceWidth, kCbbTextHeight,
                                             kCbbFontScale, 0, 0, 0, 1);
        Controls.BuySell->SetProperty(0x24, 1, 0);

        sprintf(Name, "CommodityByBaseBase%d", Line);
        float const BasePosition[3] = {kCbbBaseX, Y, 0.0f};
        Controls.Base = CreateTextControl(Name, "ItemName", BasePosition, nullptr,
                                          kCbbBaseWidth, kCbbTextHeight,
                                          kCbbFontScale, 0, 0, 0, 1);
        Controls.Base->SetProperty(0x24, 1, 0);

        sprintf(Name, "CommodityByBaseSystem%d", Line);
        float const SystemPosition[3] = {kCbbSystemX, Y, 0.0f};
        Controls.System = CreateTextControl(Name, "ItemName", SystemPosition, nullptr,
                                            kCbbSystemWidth, kCbbTextHeight,
                                            kCbbFontScale, 0, 0, 0, 1);
        Controls.System->SetProperty(0x24, 1, 0);
    }

    {
        float const Position[3] = {NnFloatBits(0x3ec83127), NnFloatBits(0x3e6d9168),
                                   NnFloatBits(0xbfa4dd2f)};
        Scroll = CreateUIScroll("CommodityByBaseScroll", Position, NnFloatBits(0x3c9ba5e3),
                                NnFloatBits(0x3ecfdf3b), 0, 5, 3, 0);
    }

    BuildCommodityByBaseRows(ArchId);
    if (SortColumn != 1)
    {
        SortColumn = 1;
        SortCommodityByBaseRows(1);
    }
    RefreshRows();

    RegisterEvent(0xf);
    SetOffset(0.0f, NnFloatBits(0x3d9ba5e3), NnFloatBits(0xbfa4dd2f));
    SetPosition(Position);

    // FUN_004b5060 with the window's current parent and no data, not Create's arguments.
    return Win3D::Create(Parent, nullptr);
}

void NnCommodityByBase::RefreshRows()
{
    // FUN_0046fa80.
    int const Count = static_cast<int>(G_CommodityByBaseRows.size());

    // Line i shows row FirstRow + i (signed compare with the count).
    int Row = FirstRow;
    for (int Line = 0; Line < 13; ++Line, ++Row)
    {
        FCommodityByBaseRowControls& Controls = Rows[Line];
        if (Row >= Count)
        {
            Controls.Base->Flags = static_cast<unsigned char>(Controls.Base->Flags & 0xfc);
            Controls.Price->Flags = static_cast<unsigned char>(Controls.Price->Flags & 0xfc);
            Controls.System->Flags = static_cast<unsigned char>(Controls.System->Flags & 0xfc);
            Controls.BuySell->Flags = static_cast<unsigned char>(Controls.BuySell->Flags & 0xfc);
            continue;
        }

        Controls.Base->Flags = static_cast<unsigned char>(Controls.Base->Flags | 3);
        Controls.Price->Flags = static_cast<unsigned char>(Controls.Price->Flags | 3);
        Controls.System->Flags = static_cast<unsigned char>(Controls.System->Flags | 3);
        Controls.BuySell->Flags = static_cast<unsigned char>(Controls.BuySell->Flags | 3);

        FCommodityByBaseRow const& Data = G_CommodityByBaseRows[Row];

        FormatGroupedNumber(G_NnTextScratchB, Data.Price, -1);
        Controls.Price->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);

        unsigned const SellsIds = Data.bBaseSells ? 0x3d9u : 0x3dau;
        Controls.BuySell->SetProperty(0x1e, SellsIds, 0);

        Controls.Base->SetProperty(
            0x1d,
            reinterpret_cast<uintptr_t>(
                CommodityByBaseName(G_CommodityByBaseBaseNames, Data.SpaceObjectId)),
            0);

        Controls.System->SetProperty(
            0x1d,
            reinterpret_cast<uintptr_t>(
                CommodityByBaseName(G_CommodityByBaseSystemNames, Data.SpaceObjectId)),
            0);
    }

    // The scroll bar is hidden while the rows fit.
    int const Extra = Count - 13;
    if (Extra <= 0)
    {
        Scroll->SetProperty(2, 1, 0);
        Scroll->Flags = static_cast<unsigned char>(Scroll->Flags & 0xfd);
        return;
    }
    Scroll->SetProperty(2, 0, 0);
    Scroll->Flags = static_cast<unsigned char>(Scroll->Flags | 2);
    Scroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Extra + 2));
}

intptr_t NnCommodityByBase::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0046f550. 0x3e from the scroll bar moves the first row; 0x3e from anything else and
    // 0x26 are clicks.
    if (Msg != 0x26)
    {
        if (Msg != 0x3e)
            return 0;
        if (A == reinterpret_cast<uintptr_t>(Scroll))
        {
            FirstRow = static_cast<int>(B);
            RefreshRows();
            return 1;
        }
    }

    if (A == reinterpret_cast<uintptr_t>(CloseButton))
    {
        Close();
        HudPostCommand(0x66, 0);
        return 1;
    }

    // A column title click returns 0.
    int Column = 0;
    if (A == reinterpret_cast<uintptr_t>(PriceTitle))
        Column = 1;
    else if (A == reinterpret_cast<uintptr_t>(BaseTitle))
        Column = 2;
    else if (A == reinterpret_cast<uintptr_t>(SystemTitle))
        Column = 3;
    else if (A == reinterpret_cast<uintptr_t>(BuySellTitle))
        Column = 4;
    if (Column != 0)
    {
        if (SortColumn != Column)
        {
            SortColumn = Column;
            SortCommodityByBaseRows(Column);
        }
        RefreshRows();
        return 0;
    }

    for (int Line = 0; Line < 13; ++Line)
    {
        FCommodityByBaseRowControls const& Controls = Rows[Line];
        if (A == reinterpret_cast<uintptr_t>(Controls.Base) ||
            A == reinterpret_cast<uintptr_t>(Controls.Price) ||
            A == reinterpret_cast<uintptr_t>(Controls.System) ||
            A == reinterpret_cast<uintptr_t>(Controls.BuySell))
        {
            // Read before the close; no bounds test, as in retail.
            unsigned const Id = G_CommodityByBaseRows[FirstRow + Line].SpaceObjectId;
            Close();
            NavMapRequestFocus(Id, 0); // FUN_004a2aa0
            return 1;
        }
    }
    return 0;
}

void NnCommodityByBase::Update()
{
    // LAB_0046fd20.
    if (CommodityByBaseMustClose())
    {
        Close();
        return;
    }

    Win3D::Update();

    // The cursor ray through the Win3D camera DAT_0067dbf8.
    FSpaceCamera* const Cam = Win3DGetCamera();
    int CursorX = 0;
    int CursorY = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, nullptr); // DAT_00616840 / DAT_00616844
    FVector Local;
    Local.X = static_cast<float>((static_cast<double>(CursorX) - Cam->ScreenCenterX) /
                                 Cam->XScale * Cam->ZNear);
    Local.Y = static_cast<float>((static_cast<double>(CursorY) - Cam->ScreenCenterY) /
                                 Cam->YScale * Cam->ZNear);
    Local.Z = -Cam->ZNear;
    FMatrix Orientation;
    memcpy(&Orientation, Cam->World, sizeof(Orientation));
    if (G_CommodityByBaseMath == nullptr)
        G_CommodityByBaseMath = Acquire3DMathEngine(); // FUN_005b73e0
    FVector World;
    G_CommodityByBaseMath->TransformMatrix(World, Orientation, Local);
    double const Inv = 1.0 / sqrt(static_cast<double>(World.X) * World.X +
                                  static_cast<double>(World.Y) * World.Y +
                                  static_cast<double>(World.Z) * World.Z);
    float Ray[3];
    Ray[0] = static_cast<float>(World.X * Inv);
    Ray[1] = static_cast<float>(World.Y * Inv);
    Ray[2] = static_cast<float>(World.Z * Inv);

    // Column titles: the sorted column is amber (DAT_00679ba4), one under the mouse pulses.
    WinControl* const Titles[5] = {nullptr, PriceTitle, BaseTitle, SystemTitle, BuySellTitle};
    for (int Column = 1; Column < 5; ++Column)
    {
        WinControl* const Title = Titles[Column];
        if (Title == nullptr || (Title->Flags & kWin3DVisible) == 0)
            continue;
        unsigned long Color = G_NnPaletteCyan;
        if (SortColumn == Column)
        {
            Color = G_NnPaletteAmber;
        }
        else if (Title->HitTest(Ray))
        {
            unsigned long Pulse = 0;
            NnUiPulseBlendArgb(&Pulse);
            Color = Pulse;
        }
        Title->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
    }

    // A visible line (tested on its base name) pulses when the mouse is over any of its four
    // controls.
    for (int Line = 0; Line < 13; ++Line)
    {
        FCommodityByBaseRowControls const& Controls = Rows[Line];
        if (Controls.Base == nullptr || (Controls.Base->Flags & kWin3DVisible) == 0)
            continue;
        unsigned long Color = G_NnPaletteCyan;
        if (Controls.Base->HitTest(Ray) || Controls.BuySell->HitTest(Ray) ||
            Controls.Price->HitTest(Ray) || Controls.System->HitTest(Ray))
        {
            unsigned long Pulse = 0;
            NnUiPulseBlendArgb(&Pulse);
            Color = Pulse;
        }
        Controls.Base->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
        Controls.BuySell->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
        Controls.Price->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
        Controls.System->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
    }
}

bool NnCommodityByBase::OnEvent(unsigned MsgId)
{
    // FUN_0046ffe0 on the +0x32c face.
    if (HudIsMousePickBlocked()) // FUN_0045a740
        return false;
    if (MsgId != 0xf)
        return false;
    Close();
    return true;
}
