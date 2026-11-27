// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_help.h"

#include "NeuroNet/Chat/chat_init.h"                // kTextStyleBody / kTextStyleCardTitle
#include "NeuroNet/Economy/best_path.h"              // BestPathGetWaypoint (FUN_004c4ff0), FBestPathWaypoint
#include "Interface/Pages/character_select_equip.h" // GetPlayerStarSystemIdRetail (FUN_004c4830),
                                    // GetPlayerBaseIdRetail (*FUN_004c4900),
                                    // GetPlayerVisitFlagsRetail (FUN_004c4da0)
#include "NeuroNet/Economy/client_base_market.h"     // ClientMarketFill (FUN_00437de0),
                                    // ClientMarketCollectCategory (FUN_00437f70),
                                    // ClientMarketGetPrice (FUN_00438060)
#include "Space/client_gameobj.h"         // FClientGameobj
#include "Space/client_objdb.h"           // ClientObjDb_FindObject (FUN_005416c0)
#include "Space/HUD/hud_status_core.h"        // HudSampleShieldHitPoints (FUN_004d4b50)
#include "Space/HUD/hud_target.h"             // HudObjectVisitFlags (FUN_004d4d00), HudObjectTypeIds
                                    // (FUN_004d4d50), GetObjectIdsName (FUN_005472a0),
                                    // GetShipPilotName (FUN_005472d0), NavMapSectorName
                                    // (FUN_0048dc10), FHudWideText
#include "Space/HUD/hud_waypoint.h"           // GetHudWaypoint (DAT_00674bc8), GetWaypointEntry
                                    // (FUN_004f42a0)
#include "Common/Fonts/ids_rdl.h"                // AppendTextMaybeXmlRdl (FUN_0057e2c0), LoadIdsIntoRdl
                                    // (FUN_0057da40), RdlAppendNode
#include "Common/Fonts/ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "NeuroNet/Economy/infocard_map.h"           // InfocardMapFind (FUN_00458890)
#include "GameFlow/menu_universe_base.h"     // GetClientCurrentBaseStridName (FUN_0043bbe0)
#include "Common/misc_util.h"              // FormatGroupedNumber (FUN_004779a0),
                                    // FmtStrSprintf (FUN_0041c970)
#include "Space/mission_created_solars.h" // LoadMissionCreatedSolars (FUN_00476c40)
#include "NeuroNet/nn_inventory.h"           // IsPlayerDockedRetail (FUN_004c4810)
#include "NeuroNet/nn_modal.h"               // NnMainButtonOnModalOpen (FUN_0045a460)
#include "NeuroNet/NavMap/nn_navmap.h"              // NavMapZoneAt (FUN_004a1b50)
#include "NeuroNet/nn_button_control.h"
#include "NeuroNet/nn_text_control.h"
#include "NeuroNet/nn_text_scratch.h"        // G_NnTextScratchA / B, G_NnTextScratchCapacity
#include "Space/player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "Interface/rich_text_win.h"
#include "Space/star_sys.h"               // GetAttitudeClass (FUN_0045a490)
#include "Interface/win3d.h"                  // Win3DReparent (FUN_0059dd00), Win3DDeleteTree (FUN_0043d2a0)
#include "Interface/win_factory.h"

#include "common_archetype.h"       // Archetype::GetEquipment / GetShip
#include "common_cobject.h"         // CObject, CSimple, CEqObj, CShip, CLoot
#include "common_goods.h"           // GoodList, GoodInfo, BaseDataList_get, Good2Arch
#include "common_iface.h"           // IObjRW, IObjInspectImpl
#include "common_id.h"              // CreateID
#include "common_reputation.h"      // Reputation::Vibe::GetAffiliation / GetInfocard,
                                    // Reputation::get_info_card
#include "common_text.h"            // RenderDisplayList, StyleNode
#include "common_universe.h"        // Universe::get_system, ISystem, IZone, FSystemObject

#include <list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <wchar.h>
#include <windows.h>                // CharUpperW (FUN_005b74d9)

// Retail DAT_0066dc18 -- WINFACTORY<NN_Help> static (LAB_00474b60 -> FUN_00477070; Create
// FUN_004770e0; atexit LAB_00474b80 -> FUN_00579380).
WINFACTORY<NnHelp> G_NnHelpFactory("NN_Help");

namespace
{
    char const kInfoWindowMesh[] = "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp"; // 0x005ce994
    char const kCloseButtonMesh[] = "Interface\\NeuroNet\\Inventory\\x.3db";            // 0x005d01d0

    wchar_t const kLineBreak[] = L"\n";                                    // DAT_005d0254
    wchar_t const kDoubleLineBreak[] = L"\n\n";                            // DAT_005d0a08
    wchar_t const kEmptyParagraph[] = L"<RDL><TEXT><PARA/></TEXT></RDL>";  // 0x005d09c8
    wchar_t const kTitleTail[] = L"\n    ";                                // 0x005d0a64
    wchar_t const kItemTail[] = L"\n     ";                                // 0x005d0a54
    wchar_t const kPriceLead[] = L" ";                                     // DAT_005cef90

    // DAT_0066dc54 / DAT_0066dc58 -- the id of the ship's target, kept by Create between its
    // head and its text; nothing else reads or writes them.
    unsigned G_NnHelpPendingTargetId = 0;
    unsigned char G_bNnHelpPendingTarget = 0;

    // Not a retail function: the append every text builder here spells out -- a string into
    // the second scratch buffer (wcscpy), then FUN_0057e2c0 with its wcslen and no text pointer.
    void AppendScratchB(RenderDisplayList& Card)
    {
        AppendTextMaybeXmlRdl(G_NnTextScratchB, static_cast<unsigned>(wcslen(G_NnTextScratchB)),
                              Card, false);
    }

    void AppendText(RenderDisplayList& Card, wchar_t const* Text)
    {
        wcscpy(G_NnTextScratchB, Text);
        AppendScratchB(Card);
    }

    void AppendStyle(RenderDisplayList& Card, unsigned short StyleId)
    {
        // StyleNode ctor, clone (vt+0x1c), the clone into the list (FUN_0046c800 or the same
        // push_back inline), the StyleNode dtor.
        StyleNode Style(StyleId);
        RdlAppendNode(Card, Style.Clone());
    }

    // Not a retail function: the header FUN_00476080 writes before each of its four lists
    // -- style 0xf64, a line break, the title ids, the title tail
    // and style 0x9ca4.
    void AppendSectionTitle(RenderDisplayList& Card, unsigned TitleIds)
    {
        AppendStyle(Card, kTextStyleCardTitle);
        AppendText(Card, kLineBreak);
        LoadIdsIntoRdl(TitleIds, Card);
        AppendText(Card, kTitleTail);
        AppendStyle(Card, kTextStyleBody);
    }

    // Not a retail function: one priced line (inlined twice) -- the name, then " " + the grouped price (ids 0x2140 prefix) + the item tail.
    void AppendPricedItem(RenderDisplayList& Card, unsigned NameIds, float Price)
    {
        LoadIdsIntoRdl(NameIds, Card);
        FormatGroupedNumber(G_NnTextScratchA, static_cast<int>(Price), -1); // ftol
        wcscpy(G_NnTextScratchB, kPriceLead);
        wcscat(G_NnTextScratchB, G_NnTextScratchA);
        wcscat(G_NnTextScratchB, kItemTail);
        AppendScratchB(Card);
    }

    // The ids string of Ids into Out (Chars wide characters), Out emptied when there is none
    // (FUN_004347e0; a zero result empties Out).
    void IdsIntoOrEmpty(unsigned Ids, wchar_t* Out, int Chars)
    {
        if (GetIdsStringWWithRawFallback(Ids, Out, Chars) == 0)
            Out[0] = L'\0';
    }

    // The "%d%\n" lines (u"%d%\n" at 0x005d0a48, MSVCRT swprintf): the legacy printf state
    // machine drops a '%' followed by a character that is no conversion and writes that
    // character, so retail prints the number and a line break -- no percent sign. The port's
    // CRT rejects "%\n" as an invalid conversion, so the same output is spelled "%d\n".
    void FormatPercentLine(wchar_t* Out, int Value)
    {
        _swprintf(Out, L"%d\n", Value);
    }

    // FUN_00475dd0's 8-byte record: the entry and its index into the name buffer.
    struct FNnHelpNameSortRecord
    {
        FClientMarketGood const* Entry; // +0
        int NameIndex;                  // +4
    };
}

void NnHelpSortGoodsByName(std::list<FClientMarketGood const*>* Goods)
{
    // FUN_00475dd0 (cdecl).
    int const Count = static_cast<int>(Goods->size());
    std::vector<FNnHelpNameSortRecord> Records(static_cast<size_t>(Count));
    std::vector<wchar_t> Names(static_cast<size_t>(Count) * 0x40);

    // Per node, in list order -- the record {entry, i} and the equipment's name (empty without
    // one).
    int Index = 0;
    for (std::list<FClientMarketGood const*>::iterator It = Goods->begin(); It != Goods->end();
         ++It, ++Index)
    {
        FClientMarketGood const* const Entry = *It;
        Records[Index].Entry = Entry;
        Records[Index].NameIndex = Index;
        Archetype::Equipment const* const Equip =
            Archetype::GetEquipment(Good2Arch(Entry->Info.GoodId));
        wchar_t* const Name = &Names[static_cast<size_t>(Index) * 0x40];
        if (Equip == nullptr)
            Name[0] = L'\0';
        else
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Equip->IdsName), Name, 0x40);
    }

    // Bubble passes until one makes no swap.
    bool bSorted = false;
    while (!bSorted)
    {
        bSorted = true;
        for (int Pair = 0; Pair < Count - 1; ++Pair)
        {
            FNnHelpNameSortRecord& A = Records[Pair];
            FNnHelpNameSortRecord& B = Records[Pair + 1];
            if (_wcsicmp(&Names[static_cast<size_t>(A.NameIndex) * 0x40],
                         &Names[static_cast<size_t>(B.NameIndex) * 0x40]) > 0)
            {
                FNnHelpNameSortRecord const Saved = A;
                A = B;
                B = Saved;
                bSorted = false;
            }
        }
    }

    Goods->clear();
    for (int Out = 0; Out < Count; ++Out)
        Goods->push_back(Records[Out].Entry);
}

void NnHelpAppendBaseGoods(RenderDisplayList* Card, unsigned BaseId, unsigned char bCommodities,
                           unsigned char bEquipment, unsigned char bShips)
{
    // FUN_00476080 (cdecl).
    if (bCommodities == 0 && bEquipment == 0 && bShips == 0)
        return;

    AppendStyle(*Card, kTextStyleBody);

    // A market of the base's goods, priced with its variance (the
    // BaseData is used untested, as in retail).
    BaseData* const Base = BaseDataList_get()->get_base_data(BaseId);
    FClientMarket Market; // FUN_0042abc0; FUN_00437b70 at 0x00476bd0
    ClientMarketFill(&Market, Base->PriceVariance, Base);

    // Ships for sale (category 3) -- the package's hull good's ship.
    if (bShips != 0)
    {
        AppendSectionTitle(*Card, 0x669);
        std::list<FClientMarketGood const*> Goods;
        ClientMarketCollectCategory(&Market, &Goods, 3);
        int Listed = 0;
        for (std::list<FClientMarketGood const*>::iterator It = Goods.begin(); It != Goods.end();
             ++It)
        {
            // GoodList::find_by_id(good)->HullGoodId (+0x90) ->
            // find_by_id -> ShipArchId (+0x54) -> GetShip -> ids_name (+0x14); no null tests.
            GoodInfo const* const Package = GoodList::find_by_id((*It)->Info.GoodId);
            GoodInfo const* const Hull = GoodList::find_by_id(Package->HullGoodId);
            Archetype::Ship const* const Ship = Archetype::GetShip(Hull->ShipArchId);
            LoadIdsIntoRdl(static_cast<unsigned>(Ship->IdsName), *Card);
            AppendText(*Card, kItemTail);
            ++Listed;
        }
        if (Listed == 0)
            LoadIdsIntoRdl(0x67b, *Card);
    }

    if (bCommodities != 0)
    {
        // The base's stocked commodities (category 0), by name, that sell
        // here at or below their good_buy price (GoodInfo +0x5c).
        AppendSectionTitle(*Card, 0x668);
        std::list<FClientMarketGood const*> Commodities;
        ClientMarketCollectCategory(&Market, &Commodities, 0);
        NnHelpSortGoodsByName(&Commodities);
        int Listed = 0;
        for (std::list<FClientMarketGood const*>::iterator It = Commodities.begin();
             It != Commodities.end(); ++It)
        {
            FClientMarketGood const* const Entry = *It;
            if (Entry->Stock <= 0) // +0x1c, JLE
                continue;
            unsigned const Arch = Good2Arch(Entry->Info.GoodId);
            Archetype::Equipment const* const Equip = Archetype::GetEquipment(Arch);
            GoodInfo const* const Info = GoodList::find_by_archetype(Arch);
            float const Price = ClientMarketGetPrice(&Market, Info->Id); // +0x48
            // x87: skipped when the price is greater or unordered; listed when less or equal.
            if (!(Price <= Info->GoodBuyPrice))
                continue;
            AppendPricedItem(*Card, static_cast<unsigned>(Equip->IdsName), Price);
            ++Listed;
        }
        if (Listed == 0)
            LoadIdsIntoRdl(0x67b, *Card);

        // Every good of GoodList (a copy of its list, FUN_0053e430 /
        // FUN_0041d630) with an archetype whose price here is at or above its good_sell price
        // (GoodInfo +0x64).
        AppendSectionTitle(*Card, 0x667);
        std::list<GoodInfo*> const* const AllGoods = GoodList_get()->get_list();
        std::list<GoodInfo*> Candidates(AllGoods->begin(), AllGoods->end());
        Listed = 0;
        for (std::list<GoodInfo*>::iterator It = Candidates.begin(); It != Candidates.end(); ++It)
        {
            GoodInfo const* const Good = *It;
            if (Good->ArchId == 0) // +0x50, JBE
                continue;
            Archetype::Equipment const* const Equip = Archetype::GetEquipment(Good->ArchId);
            GoodInfo const* const Info = GoodList::find_by_archetype(Good->ArchId);
            float const Price = ClientMarketGetPrice(&Market, Info->Id);
            // x87: skipped when the price is less or unordered; listed when greater or equal.
            if (!(Price >= Info->GoodSellPrice))
                continue;
            AppendPricedItem(*Card, static_cast<unsigned>(Equip->IdsName), Price);
            ++Listed;
        }
        if (Listed == 0)
            LoadIdsIntoRdl(0x67b, *Card);
    }

    // The base's stocked equipment (category 1), by name, without
    // ammunition (Equipment vt+0x18 get_hp_type 3).
    if (bEquipment != 0)
    {
        AppendSectionTitle(*Card, 0x66a);
        std::list<FClientMarketGood const*> Goods;
        ClientMarketCollectCategory(&Market, &Goods, 1);
        NnHelpSortGoodsByName(&Goods);
        int Listed = 0;
        for (std::list<FClientMarketGood const*>::iterator It = Goods.begin(); It != Goods.end();
             ++It)
        {
            FClientMarketGood const* const Entry = *It;
            if (Entry->Stock <= 0)
                continue;
            Archetype::Equipment const* const Equip =
                Archetype::GetEquipment(Good2Arch(Entry->Info.GoodId));
            if (static_cast<int>(Equip->get_hp_type()) == 3)
                continue;
            LoadIdsIntoRdl(static_cast<unsigned>(Equip->IdsName), *Card);
            AppendText(*Card, kItemTail);
            ++Listed;
        }
        if (Listed == 0)
            LoadIdsIntoRdl(0x67b, *Card);
    }
}

bool NnHelp::Create(Win3D* NewParent, void* Data)
{
    // FUN_00474b90. In space, the id of the ship's target is
    // kept for the text below (the player's gameobj -> +0xc CBase -> +4 CObject, a CShip).
    if (G_bNnHelpPendingTarget == 0 && !IsPlayerDockedRetail())
    {
        FClientGameobj* const Player = static_cast<FClientGameobj*>(GetPlayerObjRW());
        if (Player != nullptr)
        {
            CBase* const PlayerBase = static_cast<IObjInspectImpl*>(Player);
            if (PlayerBase != nullptr)
            {
                CObject* const PlayerObject = PlayerBase->cobject();
                if (PlayerObject != nullptr &&
                    (static_cast<unsigned>(PlayerObject->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                {
                    IObjRW* const Target = static_cast<CShip*>(PlayerObject)->get_target();
                    if (Target != nullptr)
                    {
                        G_NnHelpPendingTargetId = Target->get_id(); // vt+0x20
                        G_bNnHelpPendingTarget = 1;
                    }
                }
            }
        }
    }

    // The window mesh (slot 38) and the hit box from it.
    SetMesh(kInfoWindowMesh, 0);
    WinControlHalfExtentsFromMesh(&HalfExtents, &MeshHolder); // FUN_005a1340

    {
        Vector const Position = {0.397f, 0.238f, 0.0f};
        Vector const Half = {0.018f, 0.018f, 0.0f};
        CloseButton = CreateButtonControl("PlayerInfoCloseButton", kCloseButtonMesh, nullptr,
                                          "ui_select_item", nullptr, &Position, nullptr, &Half, 0, 0,
                                          0, 0, 0x409, 0, 0);
    }
    {
        Vector const Position = {-0.418f, 0.256f, 0.0f};
        Title = CreateTextControl("PlayerInfoTitle", "PageHeader", &Position, nullptr, 0.836f, 0.03f,
                                  1.65f, 0x389, 1, 0, 1);
    }
    {
        Vector const Position = {-0.409f, 0.208f, 0.0f};
        Text = CreateRichTextWin("PlayerInfoText", 0.795f, 0.455f, &Position, true);
    }

    if (G_bNnHelpPendingTarget != 0)
    {
        // The target, looked up again by id, fills the window.
        FClientGameobj* const Found = ClientObjDb_FindObject(G_NnHelpPendingTargetId, 0);
        if (Found != nullptr)
            ShowObjectInfo(Found);
        G_NnHelpPendingTargetId = 0;
        G_bNnHelpPendingTarget = 0;
    }
    else
    {
        // The strid_name of the player's system (ISystem slot 3).
        int SystemName = 0; // [ESP+0x24]
        Universe::ISystem const* const System = Universe::get_system(GetPlayerStarSystemIdRetail());
        if (System != nullptr)
            SystemName = System->GetStridName();

        RenderDisplayList Card; // [ESP+0x18]
        AppendStyle(Card, kTextStyleBody);

        if (IsPlayerDockedRetail())
        {
            // The base's strid_name; nothing more without it or the
            // system's.
            int const BaseName = GetClientCurrentBaseStridName(); // FUN_0043bbe0
            if (BaseName != 0 && SystemName != 0)
            {
                // ids 0x55c formatted with the base name and the system
                // name, each loaded into one half of the first scratch buffer.
                wchar_t Format[0x80]; // [ESP+0x158]
                GetIdsStringWWithRawFallback(0x55c, Format, 0x80);
                int const Half = G_NnTextScratchCapacity / 2;
                wchar_t* const SecondHalf = &G_NnTextScratchA[Half]; // DAT_0066ec60
                GetIdsStringWWithRawFallback(static_cast<unsigned>(BaseName), G_NnTextScratchA,
                                             Half);
                GetIdsStringWWithRawFallback(static_cast<unsigned>(SystemName), SecondHalf, Half);
                _swprintf(G_NnTextScratchB, Format, G_NnTextScratchA, SecondHalf);
                AppendScratchB(Card);

                // The system object that is the player's base.
                unsigned char VisitFlags = 0; // [ESP+0x12]
                unsigned const BaseId = GetPlayerBaseIdRetail(); // [ESP+0x24], *FUN_004c4900
                int IdsInfo = 0;                                 // ESI
                bool bShow = false;
                Universe::FSystemObject Object; // [ESP+0x40], shared by both passes
                unsigned const Count = (unsigned)System->GetObjectCount();

                // The visit bits of a matching object: its "visit" byte OR the visit-map flags of
                // its nickname (FUN_004c4da0(id, &flags, 0, 0)). Retail ORs the stack byte
                // [ESP+0x13] whatever the call did; when the nickname has no visit-map entry
                // nothing is written there and retail ORs a stale stack byte -- the port ORs 0.
                // Returns whether the object carries an ids_info (the show path).
                auto TakeMatch = [&VisitFlags, &IdsInfo](Universe::FSystemObject const& Match)
                {
                    VisitFlags = Match.Visit; // +0x114
                    IdsInfo = Match.IdsInfo;  // +0x58
                    unsigned char MapFlags = 0;
                    GetPlayerVisitFlagsRetail(CreateID(Match.Nickname.Data), &MapFlags, 0, 0);
                    VisitFlags = static_cast<unsigned char>(VisitFlags | MapFlags);
                    return IdsInfo != 0;
                };

                // First pass, an object whose "base" is BaseId (+0x88).
                // A match without an ids_info ends the pass and falls into the second one.
                for (unsigned Index = 0; Index < Count; ++Index)
                {
                    if (System->GetSystemObject(Index, &Object) && Object.BaseId == BaseId)
                    {
                        bShow = TakeMatch(Object);
                        break;
                    }
                }
                // Second pass, an object that docks with BaseId (+0x80).
                if (!bShow)
                {
                    for (unsigned Index = 0; Index < Count; ++Index)
                    {
                        if (System->GetSystemObject(Index, &Object) && Object.DockWith == BaseId)
                        {
                            bShow = TakeMatch(Object);
                            break;
                        }
                    }
                }
                // A mission-created base's ids_info (the visit bits as
                // the passes left them); nothing without one.
                if (!bShow)
                {
                    LoadMissionCreatedSolars();
                    int const SolarCount = static_cast<int>(G_MissionCreatedSolars.size());
                    for (int Index = 0; Index < SolarCount; ++Index)
                    {
                        if (G_MissionCreatedSolars[Index].BaseId == BaseId)
                        {
                            IdsInfo = G_MissionCreatedSolars[Index].IdsInfo;
                            bShow = IdsInfo != 0;
                            break;
                        }
                    }
                }

                if (bShow)
                {
                    // Two line breaks, the infocard, then the infocard
                    // map's entry of it.
                    AppendText(Card, kDoubleLineBreak);
                    LoadIdsIntoRdl(static_cast<unsigned>(IdsInfo), Card);
                    FInfocardMapEntry const* Entry = nullptr; // [ESP+0x14]
                    unsigned char bCommodities = 0;           // [ESP+0x28]
                    unsigned char bEquipment = 0;             // [ESP+0x2c]
                    unsigned char bShips = 0;                 // [ESP+0x38]
                    if (InfocardMapFind(static_cast<unsigned>(IdsInfo), &Entry))
                    {
                        // A section only when the visit bit and the
                        // entry's value for it are both set.
                        if ((VisitFlags & kVisitCommodities) != 0 && Entry->bShowCommodities != 0)
                            bCommodities = 1;
                        if ((VisitFlags & kVisitEquipment) != 0 && Entry->bShowEquipment != 0)
                            bEquipment = 1;
                        if ((VisitFlags & kVisitShips) != 0 && Entry->bShowShips != 0)
                            bShips = 1;
                        NnHelpAppendBaseGoods(&Card, BaseId, bCommodities, bEquipment, bShips);

                        // The entry's extra infocard after an empty
                        // paragraph.
                        if ((VisitFlags & kVisitExtraInfo) != 0 && Entry->ExtraIds != 0)
                        {
                            AppendText(Card, kEmptyParagraph);
                            LoadIdsIntoRdl(static_cast<unsigned>(Entry->ExtraIds), Card);
                        }
                    }
                }
            }
        }
        else
        {
            // In space -- the system's name, ids 0x561 and the sector,
            // then the zone the player is in (named, any visit state).
            IObjRW* const Player = GetPlayerObjRW();
            if (Player != nullptr)
            {
                int const Capacity = G_NnTextScratchCapacity;
                GetIdsStringWWithRawFallback(static_cast<unsigned>(SystemName), G_NnTextScratchA,
                                             Capacity);
                wcscpy(G_NnTextScratchB, G_NnTextScratchA);
                GetIdsStringWWithRawFallback(0x561, G_NnTextScratchA, Capacity);
                wcscat(G_NnTextScratchB, G_NnTextScratchA);
                // Retail also pushes the capacity as a third argument;
                // FUN_0048dc10 reads only the first two.
                NavMapSectorName(&Player->get_position(), G_NnTextScratchA);
                wcscat(G_NnTextScratchB, G_NnTextScratchA);
                AppendScratchB(Card);

                // The zone (FUN_004a1b50(system, pos, 1, 0, 0)).
                Universe::IZone const* const Zone = NavMapZoneAt(
                    System, reinterpret_cast<float const*>(&Player->get_position()), 1, 0, 0);
                if (Zone != nullptr)
                {
                    // Its name (vt+0x84) after two line breaks, then its
                    // ids_info (vt+0x88) after an empty paragraph.
                    AppendText(Card, kDoubleLineBreak);
                    LoadIdsIntoRdl(static_cast<unsigned>(Zone->GetName()), Card);
                    AppendText(Card, kEmptyParagraph);
                    LoadIdsIntoRdl(static_cast<unsigned>(Zone->GetIdsInfo()), Card);
                }
            }
        }

        // The text replaced by the card, the card destroyed.
        Text->ClearContent();     // FUN_0057b370
        Text->AppendContent(Card); // FUN_0057b3e0
    }

    SetOffset(0.0f, 0.0f, 0.0f);
    SetPosition(&Position);
    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

bool NnHelp::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_00475430.
    if (Msg != kWinMsgHide)
        return WinControl::ProcessMessage(Msg, A, B); // FUN_005a0840

    // Out of the parent (no parent test), every child deleted, Destroy, delete.
    Win3DReparent(this, nullptr); // FUN_0059dd00
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child); // FUN_0043d2a0
        Child = Next;
    }
    Destroy();
    delete this;
    return true;
}

intptr_t NnHelp::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00475400 (RET 0xc).
    (void)B;
    if (Msg == kWinNotifyClick && A == reinterpret_cast<uintptr_t>(CloseButton))
    {
        NnMainButtonOnModalOpen(); // FUN_0045a460
        return 1;
    }
    return 0;
}

void NnHelp::ShowObjectInfo(IObjRW* Object)
{
    // FUN_004754a0. A CSimple ((class & 3) == 3) or nothing.
    if (Object == nullptr)
        return;
    CObject const* const Target = Object->cobject(); // vt+0x150
    if (Target == nullptr)
        return;
    if ((static_cast<unsigned>(Target->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
        return;
    CSimple const* const Simple = static_cast<CSimple const*>(Target);

    unsigned const TypeBits = Simple->TypeBits; // +0xe0
    unsigned InfoIds = 0;
    RenderDisplayList Card;
    AppendStyle(Card, kTextStyleBody);

    unsigned char const Visit = HudObjectVisitFlags(Target); // [ESP+0x17]
    G_NnTextScratchB[0] = L'\0';
    wchar_t Name[0x80]; // [ESP+0x48]

    if ((Visit & kVisitShown) == 0)
    {
        // Never visited -- the type caption as the title (upper case)
        // and ids 0xd6ef as the text.
        IdsIntoOrEmpty(HudObjectTypeIds(Simple), Name, 0x80);
        wcscat(G_NnTextScratchB, Name);
        CharUpperW(G_NnTextScratchB);
        Title->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        GetIdsStringWWithRawFallback(0xd6ef, G_NnTextScratchB, G_NnTextScratchCapacity);
        AppendScratchB(Card);
    }
    else
    {
        int const Capacity = G_NnTextScratchCapacity;

        // The name as the title (upper case).
        IdsIntoOrEmpty(GetObjectIdsName(Target), Name, 0x80);
        wcscat(G_NnTextScratchB, Name);
        CharUpperW(G_NnTextScratchB);
        Title->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        G_NnTextScratchB[0] = L'\0';

        // The pilot (ids 0x454).
        IObjRW* const Player = GetPlayerObjRW(); // EBP
        FHudWideText Pilot = {nullptr, 0};                             // [ESP+0x38]
        GetShipPilotName(Target, &Pilot);
        if (Pilot.Text != nullptr && Pilot.Text[0] != L'\0')
        {
            GetIdsStringWWithRawFallback(0x454, G_NnTextScratchA, Capacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
            wcscat(G_NnTextScratchB, Pilot.Text);
            wcscat(G_NnTextScratchB, kLineBreak);
        }

        // The attitude towards the player (ids 0x455, then 0x466
        // hostile / 0x467 neutral / 0x468 friendly).
        if (Player != nullptr && (TypeBits & (OBJ_SHIP_MASK | OBJ_STATION)) != 0)
        {
            int const Attitude = GetAttitudeClass(Player, Object); // FUN_0045a490
            GetIdsStringWWithRawFallback(0x455, G_NnTextScratchA, Capacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
            unsigned AttitudeIds = 0;
            if (Attitude == -1)
                AttitudeIds = 0x466;
            else if (Attitude == 0)
                AttitudeIds = 0x467;
            else if (Attitude == 1)
                AttitudeIds = 0x468;
            if (AttitudeIds != 0)
            {
                GetIdsStringWWithRawFallback(AttitudeIds, G_NnTextScratchA, Capacity);
                wcscat(G_NnTextScratchB, G_NnTextScratchA);
            }
            wcscat(G_NnTextScratchB, kLineBreak);
        }

        if ((TypeBits & OBJ_SHIP_MASK) != 0)
        {
            // The hull (ids 0x457): ftol(hit / max * 100). The x87 divide and multiply are
            // carried in double here; only max is rounded to float.
            float const MaxHitPts = Simple->get_max_hit_pts();
            int const Hull = static_cast<int>(static_cast<double>(Simple->get_hit_pts()) /
                                              static_cast<double>(MaxHitPts) * 100.0);
            GetIdsStringWWithRawFallback(0x457, G_NnTextScratchA, Capacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
            FormatPercentLine(G_NnTextScratchA, Hull);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);

            // A CEqObj's shield (ids 0x464) when its capacity is above 0.
            if ((static_cast<unsigned>(Target->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
            {
                float ShieldMax = 0.0f;
                float ShieldCur = 0.0f;
                HudSampleShieldHitPoints(
                    const_cast<CEqObj*>(static_cast<CEqObj const*>(Target)), &ShieldMax, &ShieldCur,
                    nullptr);
                // x87: skipped when not greater (or unordered).
                if (ShieldMax > 0.0f)
                {
                    int const Shield = static_cast<int>(static_cast<double>(ShieldCur) /
                                                        static_cast<double>(ShieldMax) * 100.0);
                    GetIdsStringWWithRawFallback(0x464, G_NnTextScratchA, Capacity);
                    wcscat(G_NnTextScratchB, G_NnTextScratchA);
                    FormatPercentLine(G_NnTextScratchA, Shield);
                    wcscat(G_NnTextScratchB, G_NnTextScratchA);
                }
            }

            // A ship's group (ids 0x456 + CShip::get_group_name), and the
            // affiliation's infocard kept for the text.
            if ((static_cast<unsigned>(Target->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            {
                CShip const* const Ship = static_cast<CShip const*>(Target);
                unsigned const Group = Ship->get_group_name();
                if (Group != 0)
                {
                    GetIdsStringWWithRawFallback(0x456, G_NnTextScratchA, Capacity);
                    wcscat(G_NnTextScratchB, G_NnTextScratchA);
                    wchar_t GroupName[0x80]; // [ESP+0x148]
                    IdsIntoOrEmpty(Group, GroupName, 0x80);
                    wcscat(G_NnTextScratchB, GroupName);
                    wcscat(G_NnTextScratchB, kLineBreak);

                    int const Vibe = static_cast<int>(Ship->VibeId); // [ESP+0x50], +0x104
                    unsigned Affiliation = 0;                        // [ESP+0x44]
                    Reputation::Vibe::GetAffiliation(Vibe, Affiliation, false);
                    unsigned const AffiliationCard = Reputation::get_info_card(Affiliation);
                    if (AffiliationCard != 0)
                    {
                        wcscat(G_NnTextScratchB, kLineBreak);
                        InfoIds = AffiliationCard;
                    }
                }
            }
        }

        // A waypoint -- the player's own is numbered (ids 0x465), a
        // route's shows its marker.
        if ((TypeBits & OBJ_WAYPOINT) != 0)
        {
            unsigned char bUserWaypoint = 0; // [ESP+0x1f]
            int WaypointIndex = 0;           // [ESP+0x18]
            // ECX = DAT_00674bc8, used untested as in retail.
            if (GetHudWaypoint()->GetWaypointEntry(&bUserWaypoint, &WaypointIndex))
            {
                if (bUserWaypoint != 0)
                {
                    GetIdsStringWWithRawFallback(0x465, G_NnTextScratchA, Capacity);
                    wcscat(G_NnTextScratchB, G_NnTextScratchA);
                    FormatPercentLine(G_NnTextScratchA, WaypointIndex + 1);
                    wcscat(G_NnTextScratchB, G_NnTextScratchA);
                }
                else
                {
                    FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(WaypointIndex);
                    if (Waypoint != nullptr)
                    {
                        FmtStrSprintf(&Waypoint->Marker, G_NnTextScratchA, Capacity);
                        wcscat(G_NnTextScratchB, G_NnTextScratchA);
                        wcscat(G_NnTextScratchB, kLineBreak);
                    }
                }
            }
        }

        // Loot -- the text so far becomes the title, and the infocard
        // is the contents' good's (+0x8c), or the contents archetype's ids_info (+0x18).
        if ((TypeBits & OBJ_LOOT) != 0)
        {
            Title->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
            CObject const* const Loot = Object->cobject();
            if (Loot != nullptr && (static_cast<unsigned>(Loot->ObjectClass) & CObject::ClassLoot) == CObject::ClassLoot)
            {
                Archetype::Equipment const* const Contents =
                    static_cast<CLoot const*>(Loot)->ContentsArch; // +0xec
                if (Contents != nullptr)
                {
                    GoodInfo const* const Good = GoodList::find_by_archetype(Contents->Nickname.Id);
                    if (Good != nullptr)
                        InfoIds = static_cast<unsigned>(Good->IdsInfo);
                    else
                        InfoIds = static_cast<unsigned>(Contents->IdsInfo);
                }
            }
        }

        // The collected lines, then the kept infocard.
        if (G_NnTextScratchB[0] != L'\0')
            AppendScratchB(Card);
        if (InfoIds != 0)
            LoadIdsIntoRdl(InfoIds, Card);

        // A CEqObj's own infocard (Reputation::Vibe::GetInfocard), and
        // with an infocard map entry and a dock base the base goods sections by the visit bits.
        CObject const* const Owner = Object->cobject();
        if (Owner != nullptr && (static_cast<unsigned>(Owner->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
        {
            CEqObj const* const EqObj = static_cast<CEqObj const*>(Owner);
            int const Vibe = static_cast<int>(EqObj->VibeId); // [ESP+0x34]
            unsigned Infocard = 0;                            // [ESP+0x10]
            Reputation::Vibe::GetInfocard(Vibe, Infocard);
            if (Infocard != 0)
            {
                LoadIdsIntoRdl(Infocard, Card);
                AppendText(Card, kLineBreak);
                unsigned const DockBase = EqObj->DockingBaseId; // ESI, +0x160
                FInfocardMapEntry const* Entry = nullptr;      // [ESP+0x34]
                if (InfocardMapFind(Infocard, &Entry) && DockBase != 0)
                {
                    // The visit bits alone pick the sections here.
                    unsigned char const bCommodities = (Visit & kVisitCommodities) != 0 ? 1 : 0; // [ESP+0x20]
                    unsigned char const bEquipment = (Visit & kVisitEquipment) != 0 ? 1 : 0;   // [ESP+0x24]
                    unsigned char const bShips = (Visit & kVisitShips) != 0 ? 1 : 0;    // [ESP+0x18]
                    NnHelpAppendBaseGoods(&Card, DockBase, bCommodities, bEquipment, bShips);
                    AppendText(Card, kLineBreak);
                    if ((Visit & kVisitExtraInfo) != 0 && Entry->ExtraIds != 0)
                    {
                        LoadIdsIntoRdl(static_cast<unsigned>(Entry->ExtraIds), Card);
                        AppendText(Card, kLineBreak);
                    }
                }
            }
        }

        // The pilot name's buffer (CRT free).
        if (Pilot.Text != nullptr)
            free(Pilot.Text);
    }

    // The text replaced by the card and scrolled to the top.
    Text->ClearContent();     // FUN_0057b370
    Text->AppendContent(Card); // FUN_0057b3e0
    Text->ScrollToTop();      // FUN_00579a40
}
