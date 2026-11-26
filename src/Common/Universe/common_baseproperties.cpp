// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll base-side records: MissionVendorProperties / MissionVendorAcceptance,
// NewsBroadcastProperties, CharacterProperties, AmbientScriptInfo / AmbientScriptProperties,
// CostumeHint / BaseHint and CBase.
//
// The property bags share one shape in retail: the constructor zero-constructs the FmtStr
// members inline and then runs clear() inlined; destroy() releases what the record owns and
// then runs clear() inlined; the destructor is destroy() inlined plus the members' ~FmtStr;
// operator= is destroy() inlined (no self test) followed by a memberwise copy. marshal and
// unmarshal take the MarshalBuffer unchecked.

#include "common_goods.h"
#include "Archetypes/common_goods_internal.h" // AssignCostume
#include "common_cobject.h"        // CObject (CBase)
#include "common_id.h"
#include "common_misc.h"           // malloc_strcpy / malloc_wstrcpy and their frees

#include <cstdint>
#include <cstdlib>
#include <cstring>

using CmnGoods::AssignCostume;

namespace
{
    // The per-member reset retail inlines into every clear(): FmtStr::destroy, then all six
    // dwords zeroed.
    void DestroyAndZero(FmtStr& Text)
    {
        Text.destroy();
        memset(&Text, 0, sizeof(Text));
    }
}

// ---------------------------------------------------------------------------
// MissionVendorProperties
// ---------------------------------------------------------------------------

// .text:0631a770
MissionVendorProperties::MissionVendorProperties()
{
    clear();
}

// .text:06308d10: memberwise.
MissionVendorProperties::MissionVendorProperties(MissionVendorProperties const& Other)
    : Key(Other.Key)
    , BaseId(Other.BaseId)
    , OfferId(Other.OfferId)
    , OfferKind(Other.OfferKind)
    , MissionClass(Other.MissionClass)
    , Title(Other.Title)
    , Description(Other.Description)
    , Briefing(Other.Briefing)
    , IconTextureId(Other.IconTextureId)
{
}

// .text:0631a890
MissionVendorProperties::~MissionVendorProperties()
{
    destroy();
}

// .text:0631ac40: no self test.
MissionVendorProperties& MissionVendorProperties::operator=(MissionVendorProperties const& Other)
{
    destroy();
    Key = Other.Key;
    BaseId = Other.BaseId;
    OfferId = Other.OfferId;
    OfferKind = Other.OfferKind;
    MissionClass = Other.MissionClass;
    Title = Other.Title;
    Description = Other.Description;
    Briefing = Other.Briefing;
    IconTextureId = Other.IconTextureId;
    return *this;
}

// .text:0631a970
void MissionVendorProperties::clear()
{
    Key = -1;
    BaseId = 0;
    OfferId = 0;
    OfferKind = 0;
    MissionClass = 0;
    DestroyAndZero(Title);
    DestroyAndZero(Description);
    DestroyAndZero(Briefing);
    IconTextureId = 0;
}

// .text:0631a9d0. clear() releases the texts again, a no-op on an empty chain.
void MissionVendorProperties::destroy()
{
    Title.destroy();
    Description.destroy();
    Briefing.destroy();
    clear();
}

unsigned long MissionVendorProperties::get_key() const
{
    return (unsigned long)Key;
}

// .text:0631add0
char const* MissionVendorProperties::get_icon_texture() const
{
    switch (MissionClass)
    {
    case kMissionClassDestroy: return "MIS_destroy";
    case kMissionClassDestroyInstallation: return "MIS_destroyinstallation";
    case kMissionClassAssassinate: return "MIS_assassinate";
    case kMissionClassDestroyContraband: return "MIS_destroycontraband";
    case kMissionClassBountyOffer: return "MIS_bountyoffer";
    case kMissionClassRetrieveMissingItem: return "MIS_retrievemissingitem";
    default: return nullptr;
    }
}

// .text:0631ad10
void MissionVendorProperties::init(MissionVendorOfferDescription const& Desc)
{
    destroy();
    Key = -1;
    BaseId = Desc.BaseId;
    OfferId = Desc.OfferId;
    OfferKind = Desc.OfferKind;
    MissionClass = Desc.MissionClass;
    Title = Desc.Title;
    Description = Desc.Description;
    Briefing = Desc.Briefing;
    IconTextureId = Desc.IconTextureId;
}

// .text:0631aa50
void MissionVendorProperties::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&Key, 4);
    Buf->put_void(&BaseId, 4);
    Buf->put_void(&OfferId, 4);
    Buf->put_void(&OfferKind, 4);
    Buf->put_void(&MissionClass, 4);
    Buf->put_FmtStr(Title);
    Buf->put_FmtStr(Description);
    Buf->put_FmtStr(Briefing);
    Buf->put_void(&IconTextureId, 4);
}

// .text:0631ab00. Every dword before the texts goes through retail's own parameter stack cell
// (initially the MarshalBuffer pointer), so when get_void has too few bytes left the field
// receives the cell's previous content. IconTextureId goes through a never-initialised cell;
// the port starts that one at 0 (differs from retail only on a truncated buffer).
void MissionVendorProperties::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = (unsigned int)(std::uintptr_t)Buf;
    Buf->get_void(&Scratch, 4);
    Key = (int)Scratch;
    Buf->get_void(&Scratch, 4);
    BaseId = Scratch;
    Buf->get_void(&Scratch, 4);
    OfferId = Scratch;
    Buf->get_void(&Scratch, 4);
    OfferKind = Scratch;
    Buf->get_void(&Scratch, 4);
    MissionClass = Scratch;
    Title = Buf->get_FmtStr();
    Description = Buf->get_FmtStr();
    Briefing = Buf->get_FmtStr();
    unsigned int TrailingScratch = 0;
    Buf->get_void(&TrailingScratch, 4);
    IconTextureId = TrailingScratch;
}

// ---------------------------------------------------------------------------
// MissionVendorAcceptance
// ---------------------------------------------------------------------------

// .text:0631ae20
MissionVendorAcceptance::MissionVendorAcceptance()
{
    clear();
}

// .text:06308da0: memberwise.
MissionVendorAcceptance::MissionVendorAcceptance(MissionVendorAcceptance const& Other)
    : Key(Other.Key)
    , TargetId(Other.TargetId)
    , bAccepted(Other.bAccepted)
    , Reason(Other.Reason)
{
}

// .text:0631aeb0
MissionVendorAcceptance::~MissionVendorAcceptance()
{
    destroy();
}

// .text:0631b080: no self test.
MissionVendorAcceptance& MissionVendorAcceptance::operator=(MissionVendorAcceptance const& Other)
{
    destroy();
    Key = Other.Key;
    TargetId = Other.TargetId;
    bAccepted = Other.bAccepted;
    Reason = Other.Reason;
    return *this;
}

// .text:0631af30
void MissionVendorAcceptance::clear()
{
    Key = -1;
    TargetId = 0;
    bAccepted = false;
    DestroyAndZero(Reason);
}

// .text:0631af60
void MissionVendorAcceptance::destroy()
{
    Reason.destroy();
    clear();
}

// .text:0631af90
void MissionVendorAcceptance::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&Key, 4);
    Buf->put_void(&TargetId, 4);
    Buf->put_bool(bAccepted);
    Buf->put_FmtStr(Reason);
}

// .text:0631afe0. The same stack-cell quirk as MissionVendorProperties::unmarshal.
void MissionVendorAcceptance::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = (unsigned int)(std::uintptr_t)Buf;
    Buf->get_void(&Scratch, 4);
    Key = (int)Scratch;
    Buf->get_void(&Scratch, 4);
    TargetId = Scratch;
    bAccepted = Buf->get_bool();
    Reason = Buf->get_FmtStr();
}

// 0x06261d90
unsigned long MissionVendorAcceptance::get_key() const
{
    return (unsigned long)Key;
}

// ---------------------------------------------------------------------------
// NewsBroadcastProperties
// ---------------------------------------------------------------------------

// .text:0631b0e0
NewsBroadcastProperties::NewsBroadcastProperties()
{
    clear();
}

// .text:06308dd0: the compiler's memberwise copy -- LogoTexture is copied as a POINTER, not
// duplicated, although destroy() frees it: destroying both the copy and the original frees the
// string twice, as in retail. No retail module calls this constructor.
NewsBroadcastProperties::NewsBroadcastProperties(NewsBroadcastProperties const& Other)
    : Key(Other.Key)
    , BaseId(Other.BaseId)
    , bBreaking(Other.bBreaking)
    , bAutoselect(Other.bAutoselect)
    , Icon(Other.Icon)
    , CategoryName(Other.CategoryName)
    , Headline(Other.Headline)
    , Text(Other.Text)
    , LogoTexture(Other.LogoTexture)
    , AudioId(Other.AudioId)
{
}

// .text:0631b210
NewsBroadcastProperties::~NewsBroadcastProperties()
{
    destroy();
}

// .text:0631b5c0: LogoTexture is duplicated here (unlike the copy ctor); no self test.
NewsBroadcastProperties& NewsBroadcastProperties::operator=(NewsBroadcastProperties const& Other)
{
    destroy();
    Key = Other.Key;
    BaseId = Other.BaseId;
    bBreaking = Other.bBreaking;
    bAutoselect = Other.bAutoselect;
    Icon = Other.Icon;
    CategoryName = Other.CategoryName;
    Headline = Other.Headline;
    Text = Other.Text;
    malloc_strcpy(&LogoTexture, Other.LogoTexture);
    AudioId = Other.AudioId;
    return *this;
}

// .text:0631b300. The texts are released and zeroed; LogoTexture is only set to null (not
// freed), so clear() on a record that still owns one leaks it, as in retail.
void NewsBroadcastProperties::clear()
{
    Key = -1;
    BaseId = 0;
    bBreaking = false;
    bAutoselect = false;
    Icon = 0;
    DestroyAndZero(CategoryName);
    DestroyAndZero(Headline);
    DestroyAndZero(Text);
    LogoTexture = nullptr;
    AudioId = 0;
}

// .text:0631b360
void NewsBroadcastProperties::destroy()
{
    CategoryName.destroy();
    Headline.destroy();
    Text.destroy();
    malloc_strfree(&LogoTexture);
    clear();
}

unsigned long NewsBroadcastProperties::get_key() const
{
    return (unsigned long)Key;
}

// .text:0631b790
char const* NewsBroadcastProperties::get_logo_texture() const
{
    return (LogoTexture != nullptr) ? LogoTexture : "genericnews";
}

// .text:0631b6a0. No test of AudioName (CreateID maps null to 0).
void NewsBroadcastProperties::init(NewsBroadcastDescription const& Desc)
{
    destroy();
    Key = -1;
    BaseId = Desc.BaseId;
    bBreaking = Desc.bBreaking;
    bAutoselect = Desc.bAutoselect;
    Icon = Desc.Icon;
    CategoryName = Desc.CategoryName;
    Headline = Desc.Headline;
    Text = Desc.Text;
    malloc_strcpy(&LogoTexture, Desc.LogoTexture);
    AudioId = CreateID(Desc.AudioName);
}

// .text:0631b3f0
void NewsBroadcastProperties::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&Key, 4);
    Buf->put_void(&BaseId, 4);
    Buf->put_bool(bBreaking);
    Buf->put_bool(bAutoselect);
    Buf->put_void(&Icon, 4);
    Buf->put_FmtStr(CategoryName);
    Buf->put_FmtStr(Headline);
    Buf->put_FmtStr(Text);
    Buf->put_string(LogoTexture);
    Buf->put_FL_ID(AudioId);
}

// .text:0631b490. The same stack-cell quirk as MissionVendorProperties::unmarshal. LogoTexture
// is overwritten without freeing a previous string.
void NewsBroadcastProperties::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = (unsigned int)(std::uintptr_t)Buf;
    Buf->get_void(&Scratch, 4);
    Key = (int)Scratch;
    Buf->get_void(&Scratch, 4);
    BaseId = Scratch;
    bBreaking = Buf->get_bool();
    bAutoselect = Buf->get_bool();
    Buf->get_void(&Scratch, 4);
    Icon = Scratch;
    CategoryName = Buf->get_FmtStr();
    Headline = Buf->get_FmtStr();
    Text = Buf->get_FmtStr();
    LogoTexture = Buf->get_string();
    AudioId = Buf->get_FL_ID();
}

// ---------------------------------------------------------------------------
// CharacterProperties
// ---------------------------------------------------------------------------

// .text:0632d890. The inline Costume constructor zeroes the four part ids and the accessory
// count; retail leaves Cost.AccessoryIds uninitialised, the port zeroes them too.
CharacterProperties::CharacterProperties()
    : Key(0)
    , SessionId(0)
    , RoomId(0)
    , NameIds(0)
    , FactionId(0xffffffffu)
    , NameWide(nullptr)
    , NameAnsi(nullptr)
    , CharType(0)
    , Posture(0)
    , Script(nullptr)
    , BehaviorId(0xffffffffu)
    , VoiceId(0)
{
    memset(&Cost, 0, sizeof(Cost));
}

// .text:0632d8d0 (one body with destroy()).
CharacterProperties::~CharacterProperties()
{
    destroy();
}

// .text:0632db50: no self test and no destroy(): the strings are duplicated over the old
// pointers without freeing them (malloc_strcpy never frees), and the Costume goes through its
// inline operator= (the count-bounded copy).
CharacterProperties& CharacterProperties::operator=(CharacterProperties const& Other)
{
    Key = Other.Key;
    SessionId = Other.SessionId;
    RoomId = Other.RoomId;
    NameIds = Other.NameIds;
    FactionId = Other.FactionId;
    malloc_wstrcpy(&NameWide, Other.NameWide);
    malloc_strcpy(&NameAnsi, Other.NameAnsi);
    CharType = Other.CharType;
    Posture = Other.Posture;
    AssignCostume(Cost, Other.Cost);
    malloc_strcpy(&Script, Other.Script);
    BehaviorId = Other.BehaviorId;
    VoiceId = Other.VoiceId;
    return *this;
}

// 0632d8d0 (ICF-shared with ~CharacterProperties). Frees +0x14 (malloc_wstrfree is the same VA
// as malloc_strfree), +0x18 and +0x58; no other field is touched.
void CharacterProperties::destroy()
{
    malloc_wstrfree(&NameWide);
    malloc_strfree(&NameAnsi);
    malloc_strfree(&Script);
    NameWide = nullptr;
    Script = nullptr;
    NameAnsi = nullptr;
}

unsigned long CharacterProperties::get_key() const
{
    return Key;
}

// .text:0632dc10. No destroy(): NameWide is set to null and the two strings are duplicated
// over the old pointers, so init() on a populated record leaks them, as in retail.
void CharacterProperties::init(CharacterDescription const& Desc)
{
    Key = 0;
    SessionId = 0;
    RoomId = Desc.RoomId;
    NameIds = Desc.NameIds;
    FactionId = Desc.FactionId;
    NameWide = nullptr;
    malloc_strcpy(&NameAnsi, Desc.Location);
    CharType = 0;
    Posture = 0;
    AssignCostume(Cost, Desc.Cost);
    malloc_strcpy(&Script, Desc.Script);
    BehaviorId = Desc.BehaviorId;
    VoiceId = Desc.VoiceId;
}

// .text:0632d910
void CharacterProperties::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&Key, 4);
    Buf->put_void(&SessionId, 4);
    Buf->put_void(&RoomId, 4);
    Buf->put_void(&NameIds, 4);
    Buf->put_void(&FactionId, 4);
    Buf->put_wstring(NameWide);
    Buf->put_Costume(Cost);
    Buf->put_string(Script);
    Buf->put_void(&BehaviorId, 4);
    Buf->put_string(NameAnsi);
    Buf->put_void(&CharType, 4);
    Buf->put_void(&Posture, 4);
    Buf->put_void(&VoiceId, 4);
}

// .text:0632da10. The same stack-cell quirk as MissionVendorProperties::unmarshal. The strings
// are overwritten without freeing previous ones.
void CharacterProperties::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = (unsigned int)(std::uintptr_t)Buf;
    Buf->get_void(&Scratch, 4);
    Key = Scratch;
    Buf->get_void(&Scratch, 4);
    SessionId = Scratch;
    Buf->get_void(&Scratch, 4);
    RoomId = Scratch;
    Buf->get_void(&Scratch, 4);
    NameIds = Scratch;
    Buf->get_void(&Scratch, 4);
    FactionId = Scratch;
    NameWide = Buf->get_wstring();
    AssignCostume(Cost, Buf->get_Costume());
    Script = Buf->get_string();
    Buf->get_void(&Scratch, 4);
    BehaviorId = Scratch;
    NameAnsi = Buf->get_string();
    Buf->get_void(&Scratch, 4);
    CharType = Scratch;
    Buf->get_void(&Scratch, 4);
    Posture = Scratch;
    Buf->get_void(&Scratch, 4);
    VoiceId = Scratch;
}

// ---------------------------------------------------------------------------
// AmbientScriptInfo
// ---------------------------------------------------------------------------

// 0x06274130
AmbientScriptInfo::AmbientScriptInfo()
    : Type(0)
    , Path(nullptr)
    , Priority(0)
{
}

// 0x06274140 (shared with destroy): nothing but Path is touched.
AmbientScriptInfo::~AmbientScriptInfo()
{
    malloc_strfree(&Path);
}

// 0x062ee230: the string pointer is dropped, not freed.
void AmbientScriptInfo::clear()
{
    Type = 0;
    Path = nullptr;
    Priority = 0;
}

// 0x06274140, the dtor's body.
void AmbientScriptInfo::destroy()
{
    malloc_strfree(&Path);
}

// ---------------------------------------------------------------------------
// AmbientScriptProperties
// ---------------------------------------------------------------------------

// .text:0631b7a0
AmbientScriptProperties::AmbientScriptProperties()
    : Key(-1)
    , RoomId(0)
    , ScriptId(0)
    , bFlag(true)
    , Name(nullptr)
{
}

// .text:0631b7c0 (one body with destroy()).
AmbientScriptProperties::~AmbientScriptProperties()
{
    destroy();
}

// .text:0631b7f0. Name is set to null, not freed.
void AmbientScriptProperties::clear()
{
    Key = -1;
    RoomId = 0;
    ScriptId = 0;
    bFlag = true;
    Name = nullptr;
}

// .text:0631b7c0
void AmbientScriptProperties::destroy()
{
    malloc_strfree(&Name);
    clear();
}

// 0x06261d90 (ICF-folded with BaseData::get_base_id and other first-dword getters).
unsigned long AmbientScriptProperties::get_key() const
{
    return (unsigned long)Key;
}

// .text:0631b8e0. With no self test, self-assignment frees Name before reading Other.Name,
// which is then null.
AmbientScriptProperties& AmbientScriptProperties::operator=(AmbientScriptProperties const& Other)
{
    destroy();
    Key = Other.Key;
    RoomId = Other.RoomId;
    ScriptId = Other.ScriptId;
    bFlag = Other.bFlag;
    malloc_strcpy(&Name, Other.Name);
    return *this;
}

// .text:0631b930. Key becomes 0 here (clear() makes it -1); Server's
// FRoomEntry::create_ambient_script (FUN_06d12520) stores the real handle afterwards. The
// flag is the low byte of Desc.Flags; the port's bool normalises a byte other than 0/1 to 1
// -- every producer (Content's ambient-scene actions) passes 0.
void AmbientScriptProperties::init(AmbientScriptDescription const& Desc)
{
    malloc_strfree(&Name);
    RoomId = 0;
    ScriptId = 0;
    Key = 0;
    bFlag = true;
    RoomId = Desc.RoomId;
    ScriptId = Desc.ScriptId;
    bFlag = (unsigned char)Desc.Flags != 0;
    malloc_strcpy(&Name, Desc.Name);
}

// .text:0631b810
void AmbientScriptProperties::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&Key, 4);
    Buf->put_void(&RoomId, 4);
    Buf->put_void(&ScriptId, 4);
    Buf->put_bool(bFlag);
    Buf->put_string(Name);
}

// .text:0631b880. The same stack-cell quirk as MissionVendorProperties::unmarshal. Name is
// overwritten without freeing a previous string.
void AmbientScriptProperties::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = (unsigned int)(std::uintptr_t)Buf;
    Buf->get_void(&Scratch, 4);
    Key = (int)Scratch;
    Buf->get_void(&Scratch, 4);
    RoomId = Scratch;
    Buf->get_void(&Scratch, 4);
    ScriptId = Scratch;
    bFlag = Buf->get_bool();
    Name = Buf->get_string();
}

// ---------------------------------------------------------------------------
// CostumeHint / BaseHint
// ---------------------------------------------------------------------------

// .text:06311370. Retail leaves Cost.AccessoryIds uninitialised; the port zeroes them.
CostumeHint::CostumeHint()
{
    memset(&Cost, 0, sizeof(Cost));
    Kind = 1;
}

// .text:06311280: all thirteen Costume dwords are copied (not count-bounded).
CostumeHint::CostumeHint(CostumeHint const& Other)
    : BaseHint(Other)
    , Cost(Other.Cost)
{
}

CostumeHint::~CostumeHint()
{
}

// .text:063112b0: the Costume's count-bounded operator=. No self test, no bound on the count.
CostumeHint& CostumeHint::operator=(CostumeHint const& Other)
{
    Kind = Other.Kind;
    AssignCostume(Cost, Other.Cost);
    return *this;
}

// 063113a0. Neither pointer is tested. CleanUp frees the block.
unsigned CostumeHint::Serialize(unsigned char** OutBuf)
{
    unsigned char* Buf = new unsigned char[sizeof(Costume)];
    memcpy(Buf, &Cost, sizeof(Costume));
    *OutBuf = Buf;
    return sizeof(Costume);
}

// 063113d0. No test of Buf or of Size: Size above 0x34 runs past the Costume, as in retail.
void CostumeHint::UnSerialize(unsigned char* Buf, unsigned Size)
{
    memcpy(&Cost, Buf, Size);
}

// 06311400
void CostumeHint::CleanUp(unsigned char* Buf)
{
    if (Buf != nullptr)
        delete[] Buf;
}

// .text:063111e0. Retail leaves Kind uninitialised; the port zeroes it.
BaseHint::BaseHint()
    : Kind(0)
{
}

// .text:063111f0
BaseHint::BaseHint(BaseHint const& Other)
    : Kind(Other.Kind)
{
}

BaseHint::~BaseHint()
{
}

// .text:06325980 (ICF-shared with CBase / ImageNode / ... operator=), no self test.
BaseHint& BaseHint::operator=(BaseHint const& Other)
{
    Kind = Other.Kind;
    return *this;
}

// ---------------------------------------------------------------------------
// CBase
// ---------------------------------------------------------------------------

// .text:062a8250
CBase::CBase()
    : Object(nullptr)
{
}

// .text:062a8260
CBase::CBase(CBase const& Other)
    : Object(Other.Object)
{
}

CBase::~CBase()
{
}

// .text:06325980 (ICF-shared, see BaseHint::operator=).
CBase& CBase::operator=(CBase const& Other)
{
    Object = Other.Object;
    return *this;
}

void CBase::advise(bool bFlag)
{
    // ICF-folded with CostumeDescriptions::warn: no-op in retail.
    (void)bFlag;
}

void CBase::cobject(CObject* Obj)
{
    Object = Obj;
}

CObject* CBase::cobject() const
{
    return Object;
}

long CBase::get_index() const
{
    // .text:062a82e0: Object's EngineInstanceId (EngineObject+0x04), not a field of CBase
    // itself; no null test.
    return Object->EngineInstanceId;
}

void CBase::notify_of_destruction(void* Ptr)
{
    // ICF-folded with CostumeDescriptions::warn: no-op in retail.
    (void)Ptr;
}
