// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU34 (06f00670-06f03fa0): generic bar scripts and GCS exclusions.
// See GenericScripts.h. The bar behavior builder FUN_06f00b90 and its script-set helpers
// (06f00870-06f01a30, fed by the TU17 bar character and its offer) are at the end.

// 06f00670-06f00850: TU34's own 16 static-ID initializers (DAT_06fcf474..DAT_06fcf438),
// before GGcsExclusions and GGenericScripts.
#include "Core/ContentStaticIds.h"

#include "Bases/GenericScripts.h"

#include "Core/ContentInternal.h"
#include "Core/ContentPlayerState.h"  // FindContentPlayerState, FContentPlayerState
#include "RandomMissions/RandomMission.h"       // FRandomMissionLoader::CanAccept, FRandomMissionData
#include "RandomMissions/RandomMissionDB.h"     // GRmRankDifficulties
#include "common_iface.h"      // CharacterBehaviorDescription
#include "common_ini.h"
#include "common_server.h"
#include "common_text.h"         // FmtStr

#include <cstdlib>
#include <cstring>

// _initterm order: LAB_06f01af0, then LAB_06f01c00.
std::map<unsigned int, std::vector<std::string>> GGcsExclusions;
std::vector<std::string> GGenericScripts[0x108];

namespace
{
    struct FGenericScriptSegment
    {
        int Value;
        char const* Name;
    };

    // .data 06fce380: 0x42 {value, name} pairs read only by FUN_06f01d70; every value
    // equals its index.
    FGenericScriptSegment const GGenericScriptSegments[GSEG_COUNT] = {
        {GSEG_FIDGET, "FIDGET"}, {GSEG_A01aG, "A01aG"}, {GSEG_A02aG, "A02aG"},
        {GSEG_A02cG, "A02cG"}, {GSEG_A02dG, "A02dG"}, {GSEG_A02eB, "A02eB"},
        {GSEG_A03aG, "A03aG"}, {GSEG_A04aG, "A04aG"}, {GSEG_C01aR, "C01aR"},
        {GSEG_D01aN, "D01aN"}, {GSEG_D01bG, "D01bG"}, {GSEG_D01cG, "D01cG"},
        {GSEG_D01dG, "D01dG"}, {GSEG_D01fG, "D01fG"}, {GSEG_D01fGm, "D01fGm"},
        {GSEG_D01hG, "D01hG"}, {GSEG_D01kG, "D01kG"}, {GSEG_D01kGm, "D01kGm"},
        {GSEG_D02aP, "D02aP"}, {GSEG_D02aPm, "D02aPm"}, {GSEG_D02bP, "D02bP"},
        {GSEG_D02dP, "D02dP"}, {GSEG_D02eP, "D02eP"}, {GSEG_D03aN, "D03aN"},
        {GSEG_D04aG, "D04aG"}, {GSEG_D04bB, "D04bB"}, {GSEG_D05aP, "D05aP"},
        {GSEG_D05bP, "D05bP"}, {GSEG_D06aG, "D06aG"}, {GSEG_D07aG, "D07aG"},
        {GSEG_D07bG, "D07bG"}, {GSEG_F01aB, "F01aB"}, {GSEG_F01bB, "F01bB"},
        {GSEG_F01dG, "F01dG"}, {GSEG_F01eG, "F01eG"}, {GSEG_F01fG, "F01fG"},
        {GSEG_F01jG, "F01jG"}, {GSEG_F02aP, "F02aP"}, {GSEG_F03aR, "F03aR"},
        {GSEG_F03bR, "F03bR"}, {GSEG_F03cR, "F03cR"}, {GSEG_F03dR, "F03dR"},
        {GSEG_F04aG, "F04aG"}, {GSEG_F04dG, "F04dG"}, {GSEG_F04fG, "F04fG"},
        {GSEG_F04gP, "F04gP"}, {GSEG_F05aG, "F05aG"}, {GSEG_F05bG, "F05bG"},
        {GSEG_F05cG, "F05cG"}, {GSEG_F06aP, "F06aP"}, {GSEG_F07aP, "F07aP"},
        {GSEG_F07bP, "F07bP"}, {GSEG_F07cP, "F07cP"}, {GSEG_F08dR, "F08dR"},
        {GSEG_F08bR, "F08bR"}, {GSEG_F08cR, "F08cR"}, {GSEG_F09bP, "F09bP"},
        {GSEG_F10aR, "F10aR"}, {GSEG_F10bR, "F10bR"}, {GSEG_F10bRm, "F10bRm"},
        {GSEG_W01cG, "W01cG"}, {GSEG_W01bB, "W01bB"}, {GSEG_W02aF, "W02aF"},
        {GSEG_W03bB, "W03bB"}, {GSEG_W04bP, "W04bP"}, {GSEG_W04cP, "W04cP"},
    };

    // FUN_06f02aa0 (06f02aa0-06f02b08) with Pos 0 and Count = Left.size(): _memicmp over
    // the shorter length, then shorter < longer.
    int CompareScriptNames(std::string const& Left, std::string const& Right)
    {
        size_t const Count = Left.size() < Right.size() ? Left.size() : Right.size();
        int const Result = _memicmp(Left.c_str(), Right.c_str(), Count);
        if (Result != 0)
            return Result;
        if (Left.size() < Right.size())
            return -1;
        return Left.size() != Right.size() ? 1 : 0;
    }

    // The pick idiom of FUN_06f020c0: a list of one is taken without calling rand;
    // otherwise rand() * Count / 0x8000.
    unsigned int PickIndex(unsigned int Count)
    {
        if (Count - 1 == 0)
            return 0;
        return static_cast<unsigned int>((rand() * static_cast<int>(Count)) / 0x8000);
    }
}

// FUN_06f01d70 (06f01d70-06f0208f).
void __cdecl LoadGenericScripts(char const* Path)
{
    INI_Reader Reader;
    if (!OpenContentDataIni(Path, &Reader))
        return;

    while (Reader.read_header())
    {
        if (!Reader.is_header("GenericScripts"))
            continue;

        // Fresh defaults per section.
        int Gender = 2;
        int Posture = 2;
        int Segment = GSEG_COUNT;
        while (Reader.read_value())
        {
            if (Reader.is_value("set_gender"))
            {
                Gender = _stricmp(Reader.get_value_string(), "male") != 0 ? 1 : 0;
            }
            else if (Reader.is_value("set_posture"))
            {
                Posture = _stricmp(Reader.get_value_string(), "stand") != 0 ? 1 : 0;
            }
            else if (Reader.is_value("set_segment"))
            {
                char const* Name = Reader.get_value_string();
                Segment = GSEG_COUNT;
                for (int Index = 0; Index < GSEG_COUNT; ++Index)
                {
                    if (_stricmp(GGenericScriptSegments[Index].Name, Name) == 0)
                    {
                        Segment = GGenericScriptSegments[Index].Value;
                        break;
                    }
                }
            }
            else if (Reader.is_value("script"))
            {
                std::string Script(Reader.get_value_string());
                // Reproduced on purpose: no bounds check. The defaults (Gender 2, Posture 2,
                // Segment 0x42 for a missing or unknown set_segment) reach slot 0x10e of the
                // 0x108-slot table (DAT_06fd0558, past its end; slot 0x108 is GGcsExclusions'
                // storage in retail). Only a corrupt ini gets there; the port writes into
                // whatever follows its own table -- undefined behaviour, as in retail.
                std::vector<std::string>* Scripts = GGenericScripts + (Posture + 2 * (Gender + 2 * Segment));
                Scripts->insert(Scripts->end(), 1, Script);                   // FUN_06f031d0
            }
        }
    }
}

// FUN_06f01c70 (06f01c70-06f01d63).
void __cdecl UnloadGenericScripts()
{
    for (int Index = 0; Index < 0x108; ++Index)
        GGenericScripts[Index].clear();
}

// FUN_06f020c0 (06f020c0-06f02329).
char const* __cdecl PickGenericScript(int Segment, int Gender, int Posture, unsigned int VoiceId)
{
    int const Slot = (Posture != 0 ? 1 : 0) + 2 * ((Gender != kGenderMale ? 1 : 0) + 2 * Segment);
    std::vector<std::string>& Scripts = GGenericScripts[Slot];

    std::vector<std::string>::iterator Chosen;
    std::map<unsigned int, std::vector<std::string>>::iterator Excluded = GGcsExclusions.find(VoiceId);
    if (Excluded != GGcsExclusions.end())
    {
        // Candidates = the scripts this voice is not excluded from.
        std::vector<std::vector<std::string>::iterator> Candidates;
        Candidates.reserve(Scripts.size());
        for (std::vector<std::string>::iterator Script = Scripts.begin(); Script != Scripts.end(); ++Script)
        {
            bool bExcluded = false;
            for (std::vector<std::string>::iterator Name = Excluded->second.begin();
                 Name != Excluded->second.end(); ++Name)
            {
                // Case-insensitive string compare (traits compare = _memicmp).
                if (CompareScriptNames(*Script, *Name) == 0)
                {
                    bExcluded = true;
                    break;
                }
            }
            if (!bExcluded)
                Candidates.insert(Candidates.end(), 1, Script);
        }

        if (Candidates.empty())
        {
            Chosen = Scripts.end();
        }
        else
        {
            unsigned int const Index = PickIndex(static_cast<unsigned int>(Candidates.size()));
            Chosen = Index < Candidates.size() ? Candidates[Index] : Scripts.end();
        }
    }
    else if (Scripts.empty())
    {
        Chosen = Scripts.end();
    }
    else
    {
        Chosen = Scripts.begin() + PickIndex(static_cast<unsigned int>(Scripts.size()));
    }

    if (Chosen == Scripts.end())
        return nullptr;
    return Chosen->c_str();
}

// FUN_06f020a0 (06f020a0-06f020b6).
char const* __cdecl PickFidgetScript(int Gender, int Posture)
{
    return PickGenericScript(GSEG_FIDGET, Gender, Posture, 0);
}

// FUN_06f01ab0 (06f01ab0-06f01ae1).
void AppendBehaviorText(CharacterBehaviorDescription* Behavior, char const* Text,
                        unsigned int Value)
{
    if (Text != nullptr && Text[0] != '\0')
    {
        Behavior->StringArray2[Behavior->StringArray2Count] = Text;
        Behavior->StringArray2Dword[Behavior->StringArray2Count] = Value;
        ++Behavior->StringArray2Count;
    }
}

// ---------------------------------------------------------------------------------------
// The bar behavior builder (06f00870-06f01a30).
// ---------------------------------------------------------------------------------------

namespace
{
    // DAT_06fd0c34 (TU47; set only by static init LAB_06f4b030): (DAT_06fbb230 1.3689 -
    // DAT_06fb5810 1.0) * DAT_06fb605c 0.5 + 1.0, stored as a float. Read only by
    // FUN_06f4b050.
    float const GRmOfferDifficultyTolerance = (1.3689f - 1.0f) * 0.5f + 1.0f;

    // FUN_06f4b050 (06f4b050-06f4b0c5, cdecl; TU47, its only caller is FUN_06f00870
    // 06f0098c): the difficulty of Player's stage in GRmRankDifficulties -- the exact
    // record, else the last one below it (signed), else the first; an empty table reads
    // begin() as retail does -- against Difficulty: -1 when Difficulty lies below
    // stage / tolerance, 1 when above stage * tolerance, else 0.
    int CompareRmOfferDifficulty(FContentPlayerState const* Player, float Difficulty)
    {
        int const Stage = static_cast<int>(Player->MissionNumber);
        std::vector<FRmRankDifficulty>::const_iterator Previous = GRmRankDifficulties.begin();
        float StageDifficulty = 0.0f;
        bool bFound = false;
        for (std::vector<FRmRankDifficulty>::const_iterator It = GRmRankDifficulties.begin();
             It != GRmRankDifficulties.end(); ++It)
        {
            if (It->Stage == Stage)
            {
                StageDifficulty = It->Difficulty;
                bFound = true;
                break;
            }
            if (It->Stage > Stage)
                break;
            Previous = It;
        }
        if (!bFound)
            StageDifficulty = Previous->Difficulty;

        // Unordered compares fall through to 0. The x87 runs at the CRT's 53-bit precision,
        // hence double.
        double const Stage64 = static_cast<double>(StageDifficulty);
        double const Tolerance = static_cast<double>(GRmOfferDifficultyTolerance);
        if (Stage64 / Tolerance > static_cast<double>(Difficulty))
            return -1;
        if (static_cast<double>(Difficulty) > Stage64 * Tolerance)
            return 1;
        return 0;
    }

    // The inlined "trader" / "shipdealer" / "equipment" stricmp chain: the roles that sell
    // something.
    bool IsShopRole(FBarCharacter const* Character)
    {
        char const* Role = Character->Role.Data;
        return _stricmp("trader", Role) == 0 || _stricmp("shipdealer", Role) == 0 ||
               _stricmp("equipment", Role) == 0;
    }

    bool IsBartenderRole(FBarCharacter const* Character)
    {
        return _stricmp("bartender", Character->Role.Data) == 0;
    }

    // FUN_06f00b10 (06f00b10-06f00b5f, cdecl): a generic script of Segment for the
    // character's gender and voice, appended with Value. Retail also inlines this body many
    // times in the builders below.
    void AppendGenericScript(CharacterBehaviorDescription* Behavior, int Segment,
                             FBarCharacter const* Character, int Posture, unsigned int Value)
    {
        AppendBehaviorText(Behavior,
                           PickGenericScript(Segment, Character->Gender, Posture,
                                             Character->Voice),
                           Value);
    }

    // FUN_06f019c0 (06f019c0-06f01a1c, cdecl): the bribe text -- mad-lib TextIds with the
    // bribe's group, its price, then up to three hostile groups until the first -1. Retail
    // reads the dword after the third id (+0x1c) before the count test; nothing depends on it.
    void FormatBribeOfferText(FmtStr* Text, FBarBribeOffer const* Bribe)
    {
        Text->begin_mad_lib(static_cast<unsigned int>(Bribe->TextIds));
        Text->append_rep_group(Bribe->Group);
        Text->append_int(static_cast<unsigned int>(Bribe->Price));
        for (unsigned int Index = 0; Index < 3 && Bribe->Ids[Index] != 0xffffffffu; ++Index)
            Text->append_rep_group(Bribe->Ids[Index]);
        Text->end_mad_lib();
    }

    // FUN_06f01a30 (06f01a30-06f01aa4, cdecl): the knowledge texts -- +0x240 mad-lib 0x3efe
    // with InfoIds; +0x228 mad-lib 0x3efd + rand() / 0x8000 (always 0: rand() < 0x8000,
    // but the draw is made) with OfferIds and the price.
    void FormatKnowOfferText(CharacterBehaviorDescription* Behavior, FBaseNpcKnow const* Know)
    {
        Behavior->DescFmtStr.begin_mad_lib(0x3efe);
        Behavior->DescFmtStr.append_string(static_cast<unsigned int>(Know->InfoIds));
        Behavior->DescFmtStr.end_mad_lib();
        Behavior->NameFmtStr.begin_mad_lib(static_cast<unsigned int>(rand() / 0x8000 + 0x3efd));
        Behavior->NameFmtStr.append_string(static_cast<unsigned int>(Know->OfferIds));
        Behavior->NameFmtStr.append_int(static_cast<unsigned int>(Know->Price));
        Behavior->NameFmtStr.end_mad_lib();
    }

    // FUN_06f00870 (06f00870-06f00b0e, cdecl): the scripts of a mission offer.
    void BuildMissionOfferScripts(FBarCharacter const* Character,
                                  CharacterBehaviorDescription* Behavior, int Posture,
                                  FContentPlayerState* State)
    {
        Behavior->DialogType = kDialogMission;
        AppendGenericScript(Behavior, GSEG_F01jG, Character, Posture, 0);
        // A player already on a mission.
        if ((State->GetLiveMission() & GIdValueMask) != 0)
        {
            AppendGenericScript(Behavior, GSEG_F04fG, Character, Posture, 0);
            return;
        }

        FmtStr Reason;
        FBarMissionOffer const* Offer = static_cast<FBarMissionOffer const*>(Character->Offer.Object);
        // Loader slot 7 (+0x1c); retail writes the code into the Posture argument slot (the
        // posture itself stays in a register), so a code left unwritten is the posture.
        int Code = Posture;
        if (!Offer->Data.Loader->CanAccept(Offer->Data.Mission, &Reason, &Code))
        {
            if (Code == 1)
                AppendGenericScript(Behavior, GSEG_F04dG, Character, Posture, 0);
            else if (Code == 2)
                AppendGenericScript(Behavior, GSEG_F04aG, Character, Posture, 0);
            return;
        }

        if (!pub::SinglePlayer())
        {
            AppendGenericScript(Behavior, GSEG_F06aP, Character, Posture, 0);
        }
        else
        {
            AppendGenericScript(Behavior, GSEG_F04gP, Character, Posture, 0);
            // FUN_06f4b050 of the mission's difficulty (+0x4c).
            int const Fit = CompareRmOfferDifficulty(State, Offer->Data.Mission->Difficulty);
            int const Segment = (Fit < 0) ? GSEG_F05aG : ((Fit == 0) ? GSEG_F05bG : GSEG_F05cG);
            AppendGenericScript(Behavior, Segment, Character, Posture, 0);
        }
        AppendGenericScript(Behavior, GSEG_C01aR, Character, Posture, 1);
        AppendGenericScript(Behavior, GSEG_F07aP, Character, Posture, 2);
        AppendGenericScript(Behavior, GSEG_F10aR, Character, Posture, 2);
        AppendGenericScript(Behavior, GSEG_F07cP, Character, Posture, 3);
        AppendGenericScript(Behavior, pub::SinglePlayer() ? GSEG_F10bR : GSEG_F10bRm, Character, Posture, 3);
        AppendGenericScript(Behavior, GSEG_F04fG, Character, Posture, 6);
        // The mission's description (+0x24).
        Behavior->NameFmtStr = Offer->Data.Mission->Description;
    }

    // FUN_06f01420 (06f01420-06f015dd, cdecl): the bartender's scripts. VnpcTalkCount is
    // compared unsigned.
    void BuildBartenderScripts(CharacterBehaviorDescription* Behavior,
                               FBarCharacter const* Character, int Posture,
                               unsigned int VnpcTalkCount)
    {
        if (Character->TalkCount != 0)
        {
            AppendGenericScript(Behavior, pub::SinglePlayer() ? GSEG_D01fG : GSEG_D01fGm, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_D02dP, Character, Posture, 0);
            return;
        }
        if (!pub::SinglePlayer())
        {
            AppendGenericScript(Behavior, GSEG_D01kGm, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_D05bP, Character, Posture, 0);
            return;
        }
        if (VnpcTalkCount > 1)
        {
            AppendGenericScript(Behavior, GSEG_D01kG, Character, Posture, 0);
        }
        else
        {
            AppendGenericScript(Behavior, GSEG_D01hG, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_D02eP, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_D03aN, Character, Posture, 0);
        }
        AppendGenericScript(Behavior, GSEG_D05bP, Character, Posture, 0);
    }

    // FUN_06f011f0 (06f011f0-06f0141b, cdecl): the scripts of a character with a vnpc record.
    // false only while its mission is active, which ends the builder without offer scripts.
    bool BuildVnpcScripts(CharacterBehaviorDescription* Behavior, FBarCharacter const* Character,
                          int Posture, unsigned int VnpcTalkCount)
    {
        bool const bResult = true;
        if (IsShopRole(Character))
            return bResult;
        if (IsBartenderRole(Character))
        {
            BuildBartenderScripts(Behavior, Character, Posture, VnpcTalkCount);
            return bResult;
        }

        // FUN_06f48770; not found -> NONE.
        FContentPlayerState* State = FindContentPlayerState(Character->PlayerId);
        unsigned int MissionState = VNPC_MISSION_NONE;
        if (!State->FindVnpcMissionState(State->GetBaseRecord()->BaseId, &Character->Nickname.Id,
                                         &MissionState))
            MissionState = VNPC_MISSION_NONE;
        if (MissionState == VNPC_MISSION_NONE)
        {
            BuildBartenderScripts(Behavior, Character, Posture, VnpcTalkCount);
            return bResult;
        }
        if (MissionState == VNPC_MISSION_ACTIVE)
        {
            AppendGenericScript(Behavior, GSEG_D01dG, Character, Posture, 0);
            return false;
        }
        if (!pub::SinglePlayer())
            return bResult;
        if (MissionState == VNPC_MISSION_SUCCEEDED)
        {
            AppendGenericScript(Behavior, GSEG_D01cG, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_D05bP, Character, Posture, 0);
        }
        else
        {
            AppendGenericScript(Behavior, GSEG_D01bG, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_D02bP, Character, Posture, 0);
        }
        return bResult;
    }

    // FUN_06f01600 (06f01600-06f0199b, cdecl): the scripts of the character's offer. The
    // price tests are unsigned.
    void BuildOfferScripts(FBarCharacter const* Character, CharacterBehaviorDescription* Behavior,
                           int Posture, FContentPlayerState* State, bool bVnpc, int OfferKind)
    {
        switch (OfferKind)
        {
        case BAR_OFFER_MISSION:
            BuildMissionOfferScripts(Character, Behavior, Posture, State);
            return;

        case BAR_OFFER_BRIBE:
        {
            AppendGenericScript(Behavior, GSEG_F01fG, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_F02aP, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_C01aR, Character, Posture, 1);
            AppendGenericScript(Behavior, GSEG_F07aP, Character, Posture, 2);
            AppendGenericScript(Behavior, GSEG_F07cP, Character, Posture, 3);
            FBarBribeOffer const* Bribe = static_cast<FBarBribeOffer const*>(Character->Offer.Object);
            // Retail reads the cash into the Posture argument slot.
            int Cash = Posture;
            pub::Player::InspectCash(Character->PlayerId, Cash);
            AppendGenericScript(Behavior,
                                (static_cast<unsigned int>(Cash) < static_cast<unsigned int>(Bribe->Price)) ? GSEG_F08dR : GSEG_F10aR,
                                Character, Posture, 2);
            AppendGenericScript(Behavior, pub::SinglePlayer() ? GSEG_F10bR : GSEG_F10bRm, Character, Posture, 3);
            FormatBribeOfferText(&Behavior->NameFmtStr, Bribe);
            Behavior->DialogType = kDialogBribe;
            return;
        }

        case BAR_OFFER_KNOW:
        {
            AppendGenericScript(Behavior, IsBartenderRole(Character) ? GSEG_F01bB : GSEG_F01eG, Character,
                                Posture, 0);
            AppendGenericScript(Behavior, GSEG_F02aP, Character, Posture, 0);
            FBaseNpcKnow const* Know = Character->Offer.Know;
            switch (Know->RepLevel)
            {
            case 0:
            case 1:
                AppendGenericScript(Behavior, GSEG_F03aR, Character, Posture, 0);
                break;
            case 2:
                AppendGenericScript(Behavior, GSEG_F03bR, Character, Posture, 0);
                break;
            case 3:
                AppendGenericScript(Behavior, GSEG_F03cR, Character, Posture, 0);
                break;
            default:
                break;
            }
            AppendGenericScript(Behavior, GSEG_C01aR, Character, Posture, 1);
            // Retail reads the cash into the Character argument slot.
            int Cash = static_cast<int>(reinterpret_cast<intptr_t>(Character));
            pub::Player::InspectCash(Character->PlayerId, Cash);
            if (static_cast<unsigned int>(Cash) < static_cast<unsigned int>(Know->Price))
            {
                AppendGenericScript(Behavior, GSEG_F08dR, Character, Posture, 2);
            }
            else
            {
                AppendGenericScript(Behavior, GSEG_C01aR, Character, Posture, 4);
                AppendGenericScript(Behavior, GSEG_F09bP, Character, Posture, 2);
                AppendGenericScript(Behavior, GSEG_F10aR, Character, Posture, 2);
            }
            AppendGenericScript(Behavior, GSEG_F07bP, Character, Posture, 3);
            AppendGenericScript(Behavior, pub::SinglePlayer() ? GSEG_F10bR : GSEG_F10bRm, Character, Posture, 3);
            FormatKnowOfferText(Behavior, Know);
            Behavior->DialogType = kDialogInfo;
            return;
        }

        case BAR_OFFER_RUMOR:
        {
            AppendGenericScript(Behavior, IsBartenderRole(Character) ? GSEG_F01aB : GSEG_F01dG, Character,
                                Posture, 0);
            AppendGenericScript(Behavior, GSEG_F02aP, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_F08bR, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_C01aR, Character, Posture, 1);
            AppendGenericScript(Behavior, GSEG_F09bP, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_F10aR, Character, Posture, 0);
            // No bound on the eight slots, as in retail (the data never fills them).
            FBaseNpcRumor const* Rumor = Character->Offer.Rumor;
            Behavior->DescFmtStr.begin_mad_lib(static_cast<unsigned int>(Rumor->Ids[0]));
            for (int const* Id = &Rumor->Ids[1]; *Id != 0; ++Id)
                Behavior->DescFmtStr.append_string(static_cast<unsigned int>(*Id));
            Behavior->DescFmtStr.end_mad_lib();
            Behavior->DialogType = kDialogRumor;
            return;
        }

        default:
            if (IsBartenderRole(Character))
            {
                AppendGenericScript(Behavior, GSEG_A02eB, Character, Posture, 0);
                return;
            }
            if (IsShopRole(Character))
                return;
            if (!bVnpc)
                AppendGenericScript(Behavior, GSEG_A01aG, Character, Posture, 0);
            AppendGenericScript(Behavior, GSEG_A02aG, Character, Posture, 0);
            return;
        }
    }

    // FUN_06f015e0 (06f015e0-06f015fb, cdecl; the character argument is not read).
    unsigned long CreateBarBehavior(FBarCharacter const* Unused,
                                    CharacterBehaviorDescription const* Behavior)
    {
        (void)Unused;
        if (Behavior->StringArray2Count == 0)
            return 0xffffffffu;
        return pub::GF::CharacterBehaviorCreate(*Behavior);
    }

    // The "rand() * 101 / 32768" roll of FUN_06f00b90.
    int RollPercent()
    {
        return (rand() * 101) / 0x8000;
    }

    // FUN_06f00b90 (06f00b90-06f011ea, cdecl): the behavior of a bar character -- generic
    // scripts by role, then the vnpc and offer scripts -- created through
    // pub::GF::CharacterBehaviorCreate; -1 when no script was added.
    unsigned long BuildBarBehavior(FBarCharacter const* Character)
    {
        CharacterBehaviorDescription Behavior;
        Behavior.LocationId = static_cast<int>(Character->RoomId);
        Behavior.bAutoplay = 0;
        Behavior.bGenericScript = 1;
        Behavior.bBindSingleCharacter = 1;
        Behavior.bApplySpatial = 0;
        Behavior.IdArrayCount = 0;
        Behavior.RelocateLocationId = 0;
        Behavior.StartRoomLocationId = 0;
        bool const bShop = IsShopRole(Character);
        Behavior.OfferKind = pub::Controller::MissionOfferBarCharacter;
        Behavior.DialogType = bShop ? kDialogRumor : kDialogBlowoff;
        Behavior.OfferId = Character->CharacterId;
        Behavior.Filter = Character->PlayerId;

        FContentPlayerState* State = FindContentPlayerState(Character->PlayerId);
        // Retail leaves it unset when the query fails.
        unsigned long PostureValue = 0;
        pub::GF::GetCharacterPlacementPosture(Character->RoomId, Character->Placement,
                                              PostureValue);
        int const Posture = static_cast<int>(PostureValue);
        unsigned int const VnpcTalkCount =
            State->GetVnpcTalkCount(State->GetBaseRecord()->BaseId, Character->Nickname.Id);
        // Status ignored; the attitude slot is unset in retail when it fails. 0.0 is
        // DAT_06fb444c; an unordered attitude counts as friendly.
        int const Reputation = State->GetReputation();
        unsigned int const Affiliation = Character->Affiliation;
        float Attitude = 0.0f;
        pub::Reputation::GetReputation(Reputation, Affiliation, Attitude);
        bool const bFriendly = Attitude >= 0.0f;
        int const OfferKind = Character->Offer.Kind;

        if (VnpcTalkCount == 0)
        {
            if (IsBartenderRole(Character))
            {
                AppendGenericScript(&Behavior, GSEG_W01bB, Character, Posture, 0);
                AppendGenericScript(&Behavior, GSEG_W03bB, Character, Posture, 0);
                AppendGenericScript(&Behavior, GSEG_W04bP, Character, Posture, 0);
                bool const bChance = static_cast<unsigned int>(RollPercent()) < 0x19;
                if (pub::SinglePlayer() && bChance)
                {
                    AppendGenericScript(&Behavior, GSEG_D01aN, Character, Posture, 0);
                    AppendGenericScript(&Behavior, GSEG_D02aP, Character, Posture, 0);
                    AppendGenericScript(&Behavior, GSEG_D04bB, Character, Posture, 0);
                    AppendGenericScript(&Behavior, GSEG_D05aP, Character, Posture, 0);
                }
                else
                {
                    AppendGenericScript(&Behavior, GSEG_D01aN, Character, Posture, 0);
                    AppendGenericScript(&Behavior, GSEG_D02aPm, Character, Posture, 0);
                }
                if (static_cast<unsigned int>(RollPercent()) < 0x14)
                    AppendGenericScript(&Behavior, GSEG_W02aF, Character, Posture, 0);
            }
            else if (!IsShopRole(Character))
            {
                AppendGenericScript(&Behavior, GSEG_W01cG, Character, Posture, 0);
                AppendGenericScript(&Behavior, GSEG_W04cP, Character, Posture, 0);
                bool const bChance = static_cast<unsigned int>(RollPercent()) < 0x19;
                if (pub::SinglePlayer() && bChance)
                {
                    AppendGenericScript(&Behavior, GSEG_D01aN, Character, Posture, 0);
                    AppendGenericScript(&Behavior, GSEG_D02aP, Character, Posture, 0);
                    AppendGenericScript(&Behavior, GSEG_D04aG, Character, Posture, 0);
                    AppendGenericScript(&Behavior, GSEG_D05aP, Character, Posture, 0);
                }
                else
                {
                    AppendGenericScript(&Behavior, GSEG_D01aN, Character, Posture, 0);
                    AppendGenericScript(&Behavior, GSEG_D02aPm, Character, Posture, 0);
                }
                AppendGenericScript(&Behavior, GSEG_D06aG, Character, Posture, 0);
                // Retail looks the player state up again here.
                FContentBase const* Base =
                    FindContentPlayerState(Character->PlayerId)->GetBaseRecord();
                AppendGenericScript(&Behavior,
                                    (Base->LocalFaction == Character->Affiliation) ? GSEG_D07aG : GSEG_D07bG,
                                    Character, Posture, 0);
            }
        }

        if (bFriendly)
        {
            if (VnpcTalkCount != 0)
            {
                if (!BuildVnpcScripts(&Behavior, Character, Posture, VnpcTalkCount))
                    return CreateBarBehavior(Character, &Behavior);
            }
            else if (OfferKind != 0 && static_cast<unsigned int>(RollPercent()) < 0x14 &&
                     !IsShopRole(Character) && !IsBartenderRole(Character))
            {
                AppendGenericScript(&Behavior, GSEG_A01aG, Character, Posture, 0);
                AppendGenericScript(&Behavior, GSEG_A02dG, Character, Posture, 0);
            }
            BuildOfferScripts(Character, &Behavior, Posture, State, VnpcTalkCount != 0, OfferKind);
        }
        else if (IsShopRole(Character) || IsBartenderRole(Character))
        {
            if (VnpcTalkCount != 0 && !BuildVnpcScripts(&Behavior, Character, Posture, VnpcTalkCount))
                return CreateBarBehavior(Character, &Behavior);
            BuildOfferScripts(Character, &Behavior, Posture, State, VnpcTalkCount != 0, OfferKind);
        }
        else
        {
            if (VnpcTalkCount == 0)
                AppendGenericScript(&Behavior, GSEG_A01aG, Character, Posture, 0);
            AppendGenericScript(&Behavior, GSEG_A02cG, Character, Posture, 0);
            if (VnpcTalkCount != 0)
            {
                AppendGenericScript(&Behavior, GSEG_A03aG, Character, Posture, 0);
                AppendGenericScript(&Behavior, GSEG_A04aG, Character, Posture, 0);
            }
        }
        return CreateBarBehavior(Character, &Behavior);
    }
}

// FUN_06f00b60 (06f00b60-06f00b80).
void __cdecl StartBarCharacterScript(FBarCharacterList::iterator Character)
{
    if (Character->Placement == -1)
        return;
    Character->SetBehavior(BuildBarBehavior(&*Character));
}

// FUN_06f02330 (06f02330-06f02657).
void __cdecl LoadGcsExclusions(char const* Path)
{
    INI_Reader Reader;
    // One slot for the whole load, written only by pub::GetVoiceID, which leaves it
    // untouched for an empty name -- an empty "voice" reuses the previous voice's id. Before
    // the first voice line retail holds stack garbage there; the port starts at 0.
    unsigned int VoiceId = 0;
    if (!OpenContentDataIni(Path, &Reader))
        return;

    while (Reader.read_header())
    {
        if (!Reader.is_header("GCS_Exclusions"))
            continue;

        // A "script" before any "voice" writes through end() in retail as well
        // (GCSExcl.ini always opens with a voice).
        std::map<unsigned int, std::vector<std::string>>::iterator Current = GGcsExclusions.end();
        while (Reader.read_value())
        {
            if (Reader.is_value("voice"))
            {
                pub::GetVoiceID(VoiceId, Reader.get_value_string());
                Current = GGcsExclusions.find(VoiceId);
                if (Current == GGcsExclusions.end())
                {
                    std::vector<std::string> const Empty;
                    std::pair<unsigned int const, std::vector<std::string>> const Entry(VoiceId, Empty);
                    Current = GGcsExclusions.insert(Entry).first;               // FUN_06f027d0
                }
            }
            else if (Reader.is_value("script"))
            {
                std::string Script(Reader.get_value_string());
                Current->second.insert(Current->second.end(), 1, Script);       // FUN_06f031d0
            }
        }
    }
}
