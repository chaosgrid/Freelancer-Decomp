// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "rpc_server_channel_codec.h"
#include "common_goods.h" // EquipDesc, EquipDescVector, StringAlloc (0x24/0x25)
#include "common_text.h"
#include "common_net.h" // IsMPServer (0x29 Encode)
#include "server_createinfo.h" // CEqObjCreateInfo (fill / read)
#include <cstdlib>
#include <cwchar>
#include <map>
#include <new>
#include <string>

// ===== shared SERVER helpers =====
// The 127-scale quantizer and its inverse (e.g. CREATEGUIDED 0x37, RC 06b5c4bf / RS 06bef98f).
// The two doubles are RC 06b6c1e0 / 06b6c1e8, RS 06c0df40 / 06c0df48.
static const double GRpcServerScale127 = 127.0;
static const double GRpcServerInverse127 = 1.0 / 127.0;

unsigned char RpcServerQuantize127(const float* Value) {
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        mov ecx, Value
        fld dword ptr [ecx]
        fmul qword ptr GRpcServerScale127
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned char>(Quantized);
}

void RpcServerExpand127(unsigned char Encoded, float* Value) {
    const int Quantized = static_cast<signed char>(Encoded);
    __asm {
        mov ecx, Value
        fild dword ptr Quantized
        fmul qword ptr GRpcServerInverse127
        fstp dword ptr [ecx]
    }
}

// --- create-object block: 0x04 CREATESHIP and 0x42 CREATESOLAR ---
// Addresses are RS unless marked; the RC encoder 06b50e50 is identical to RS 06c08640, the RC
// decoder 06b51750 differs only where the #if branches say.

// The two doubles behind the 255-scale health bytes: RS 06c0e270 (255.0) and
// 06c0e660 (1/255).
static const double GRpcServerScale255 = 255.0;
static const double GRpcServerInverse255 = 1.0 / 255.0;

// float * double 255.0, truncating _ftol, low byte (equipment health 06c08bb8, collision health
// 06c08e01).
static unsigned char RpcServerQuantize255(const float* Value) {
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        mov ecx, Value
        fld dword ptr [ecx]
        fmul qword ptr GRpcServerScale255
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned char>(Quantized);
}

// The byte ZERO-extended, times double 1/255, stored as float (06c09446, 06c09724).
static void RpcServerExpand255(unsigned char Encoded, float* Value) {
    const int Widened = static_cast<int>(Encoded);
    __asm {
        mov ecx, Value
        fild dword ptr Widened
        fmul qword ptr GRpcServerInverse255
        fstp dword ptr [ecx]
    }
}

// 06c089bc, the hull byte: the int hit points are rounded to a float before the divide by
// GetArchMaxHitPts, then * 255.0, _ftol, low byte. A zero maximum divides to an infinity whose
// _ftol result (0x8000000000000000) has a low byte of 0, as in retail.
static unsigned char RpcServerQuantizeHull(int HitPts, unsigned int ArchetypeId) {
    float Points;
    __asm {
        fild dword ptr HitPts
        fstp dword ptr Points
    }
    const int MaxHitPts = Archetype::GetArchMaxHitPts(ArchetypeId);
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        fild dword ptr MaxHitPts
        fdivr dword ptr Points
        fmul qword ptr GRpcServerScale255
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned char>(Quantized);
}

// 06c0928a, the inverse: max * (zero-extended byte * 1/255), _ftol; the low dword is stored.
static int RpcServerExpandHull(unsigned char Encoded, unsigned int ArchetypeId) {
    const int MaxHitPts = Archetype::GetArchMaxHitPts(ArchetypeId);
    const int Widened = static_cast<int>(Encoded);
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Result;
    __asm {
        fild dword ptr MaxHitPts
        fild dword ptr Widened
        fmul qword ptr GRpcServerInverse255
        fmulp st(1), st
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Result
        fldcw OldControl
    }
    return static_cast<int>(Result);
}

// The hardpoint dictionary. Names: RC .data:06b73708, RS .data:06c13700 (42 entries,
// null-terminated, identical in both DLLs). The map is RC/RS .data's CRT-initialised
// std::map<std::string,int> (RS 06c13bbc), the count RC 06b73f48 / RS 06c13bd0.
static const char* const GRpcServerHardpointNames[] = {
    "HpCM01", "HpCargo01", "HpCargo02", "HpContrail01", "HpContrail02",
    "HpDockLight01", "HpDockLight02", "HpDockLight03", "HpHeadlight", "HpMine01",
    "HpRunningLight01", "HpRunningLight02", "HpRunningLight03", "HpRunningLight04",
    "HpRunningLight05", "HpRunningLight06", "HpRunningLight07", "HpRunningLight10",
    "HpRunningLight11", "HpRunningLight12", "HpRunningLight13", "HpShield01",
    "HpThruster01", "HpTurret01", "HpTurret02", "HpTurret03", "HpTurret04", "HpTurret05",
    "HpTurret06", "HpTurret07", "HpTurret08", "HpTurret09", "HpTurret_U1_01",
    "HpTurret_U1_02", "HpTurret_U1_03", "HpTurret_U1_04", "HpTurret_U1_05",
    "HpTurret_U3_01", "HpWeapon01", "HpWeapon02", "HpWeapon03", "HpWeapon04",
    nullptr
};
static std::map<std::string, int> GRpcServerHardpointMap;
static unsigned int GRpcServerHardpointCount = 0;

// RC 06b4fee0 / RS 06bed590. Codes start at 1.
void RpcServerBuildHardpointDictionary() {
    GRpcServerHardpointCount = 0;
    int Code = 1;
    for (const char* const* Entry = &GRpcServerHardpointNames[0]; *Entry != nullptr; ++Entry) {
        GRpcServerHardpointMap[std::string(*Entry)] = Code;
        ++Code;
        ++GRpcServerHardpointCount;
    }
}

// RC 06b50040 / RS 06bed6f0. The caller keeps the low byte.
static int RpcServerHardpointNameToCode(const char* Name) {
    std::map<std::string, int>::const_iterator It =
        GRpcServerHardpointMap.find(std::string(Name));
    if (It == GRpcServerHardpointMap.end()) return 0;
    return It->second;
}

// RC 06b50140 / RS 06bed7f0.
static const char* RpcServerHardpointCodeToName(unsigned int Code) {
    if (Code == 0) return nullptr;
    if (Code > GRpcServerHardpointCount) return nullptr;
    return GRpcServerHardpointNames[Code - 1u];
}

FRpcServerCreateObjectFields::FRpcServerCreateObjectFields()
    : ObjId(0), ArchetypeId(0), DockingBaseId(0), VibeGroupId(0), VoiceId(0), HitPts(0),
      LoadoutId(0) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

// 06c08ac0, one record; the node's record is used untested.
static bool RpcServerEncodeCreateObjectEquip(FRpcWireStream& Stream,
    const FRpcServerEquipRecord* Record) {
    unsigned char Flags = 0;
    if (Record->bEquipped != 0) Flags = kEquipRecordEquipped;
    if (Record->bMission != 0) Flags |= kEquipRecordMission;
    if (Record->Count > 0xff) Flags |= kEquipRecordWideCount;             // signed
    if (Record->Id > 0xffu) Flags |= kEquipRecordWideId;                  // unsigned
    unsigned char Code = 0;
    if (Record->Hardpoint == nullptr) {
        Flags |= kEquipRecordNoHardpoint;
    } else {
        Code = static_cast<unsigned char>(RpcServerHardpointNameToCode(Record->Hardpoint));
        if (Code == 0) Flags |= kEquipRecordHardpointText;
    }
    if (Record->Count == 1) Flags |= kEquipRecordUnitCount;
    // float vs double 1.0 (06c0bab0): only an ordered equal sets the bit.
    if (static_cast<double>(Record->Health) == 1.0) Flags |= kEquipRecordFullHealth;
    if (!Stream.Put(&Flags, 1u)) return false;
    if ((Flags & kEquipRecordUnitCount) == 0) {
        if ((Flags & kEquipRecordWideCount) != 0) {
            if (!Stream.Put(&Record->Count, 4u)) return false;
        } else {
            const unsigned char Narrow = static_cast<unsigned char>(
                static_cast<unsigned int>(Record->Count) & 0xffu);
            if (!Stream.Put(&Narrow, 1u)) return false;
        }
    }
    if ((Flags & kEquipRecordFullHealth) == 0) {
        const unsigned char Health = RpcServerQuantize255(&Record->Health);   // before the check
        if (!Stream.Put(&Health, 1u)) return false;
    }
    const unsigned short SmallArch = Archetype::LargeIDToSmallID(Record->ArchId);
    if (!Stream.Put(&SmallArch, 2u)) return false;
    if ((Flags & kEquipRecordWideId) != 0) {
        if (!Stream.Put(&Record->Id, 2u)) return false;
    } else {
        const unsigned char Narrow = static_cast<unsigned char>(Record->Id & 0xffu);
        if (!Stream.Put(&Narrow, 1u)) return false;
    }
    if ((Flags & kEquipRecordNoHardpoint) != 0) return true;
    if ((Flags & kEquipRecordHardpointText) != 0) {
        // Retail sends the LOW BYTE of the size, then copies the whole word's worth.
        const unsigned char Length = static_cast<unsigned char>(Record->HardpointSize & 0xffu);
        if (!Stream.Put(&Length, 1u)) return false;
        return Stream.PutBlock(Record->Hardpoint, Record->HardpointSize);
    }
    return Stream.Put(&Code, 1u);
}

// RC 06b50e50 / RS 06c08640.
unsigned int RpcServerCreateObjectEncode(FPacketBase& Packet, FRpcWireStream& Stream,
    const FRpcServerCreateObjectFields& Fields) {
    (void)Packet;
    if (!Stream.Put(&Fields.ObjId, 4u)) return kPacketEncodeOverflow;
    // The archetype is narrowed before its two bytes are checked.
    const unsigned short SmallArch = Archetype::LargeIDToSmallID(Fields.ArchetypeId);
    if (!Stream.Put(&SmallArch, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Fields.DockingBaseId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Fields.VibeGroupId, 4u)) return kPacketEncodeOverflow;
    // +0x34 goes out before +0x30; +0x38 / +0x3c never travel.
    if (!Stream.Put(&Fields.Costume.Body, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Fields.Costume.Head, 4u)) return kPacketEncodeOverflow;
    const unsigned char IdCount = static_cast<unsigned char>(
        static_cast<unsigned int>(Fields.Costume.Count) & 0xffu);
    if (!Stream.Put(&IdCount, 1u)) return kPacketEncodeOverflow;
    // Count dwords as one Count << 2 byte block.
    if (!Stream.PutBlock(Fields.Costume.Ids,
            static_cast<unsigned int>(Fields.Costume.Count) << 2))
        return kPacketEncodeOverflow;
    if (!Stream.Put(&Fields.VoiceId, 4u)) return kPacketEncodeOverflow;
    // One 12-byte check, then an integer copy.
    if (!Stream.Put(&Fields.Position, 12u)) return kPacketEncodeOverflow;
    // All four bytes are computed first, x, y, z, w (memory order is w first), and only then
    // checked and written one by one.
    const unsigned char QuatX = RpcServerQuantize127(&Fields.Orientation.x);
    const unsigned char QuatY = RpcServerQuantize127(&Fields.Orientation.y);
    const unsigned char QuatZ = RpcServerQuantize127(&Fields.Orientation.z);
    const unsigned char QuatW = RpcServerQuantize127(&Fields.Orientation.w);
    if (!Stream.Put(&QuatX, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&QuatY, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&QuatZ, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&QuatW, 1u)) return kPacketEncodeOverflow;
    const unsigned char Hull = RpcServerQuantizeHull(Fields.HitPts, Fields.ArchetypeId);
    if (!Stream.Put(&Hull, 1u)) return kPacketEncodeOverflow;
    if (SinglePlayer()) {
        if (!Stream.Put(&Fields.LoadoutId, 4u)) return kPacketEncodeOverflow;
    }
    // A null equipment list ships a zero word.
    const std::list<FRpcServerEquipRecord*>* Records = Fields.Equipment.Records;
    if (Records == nullptr) {
        const unsigned short None = 0;
        if (!Stream.Put(&None, 2u)) return kPacketEncodeOverflow;
    } else {
        const unsigned short Count = static_cast<unsigned short>(Records->size() & 0xffffu);
        if (!Stream.Put(&Count, 2u)) return kPacketEncodeOverflow;
        for (std::list<FRpcServerEquipRecord*>::const_iterator It = Records->begin();
             It != Records->end(); ++It) {
            if (!RpcServerEncodeCreateObjectEquip(Stream, *It)) return kPacketEncodeOverflow;
        }
    }
    // Collision groups: low byte of the count, then per node the Id's low byte and a 255-scale
    // health byte.
    const unsigned char GroupCount =
        static_cast<unsigned char>(Fields.CollisionGroups.size() & 0xffu);
    if (!Stream.Put(&GroupCount, 1u)) return kPacketEncodeOverflow;
    for (std::list<CollisionGroupDesc>::const_iterator Group = Fields.CollisionGroups.begin();
         Group != Fields.CollisionGroups.end(); ++Group) {
        const unsigned char GroupId = static_cast<unsigned char>(Group->Id & 0xffu);
        if (!Stream.Put(&GroupId, 1u)) return kPacketEncodeOverflow;
        const unsigned char Health = RpcServerQuantize255(&Group->Health);
        if (!Stream.Put(&Health, 1u)) return kPacketEncodeOverflow;
    }
    if (!SinglePlayer()) return 0;
    // The damage list only travels in single player; HitPts goes through x87.
    const unsigned int DamageCount = static_cast<unsigned int>(Fields.Damage.Entries.size());
    if (!Stream.Put(&DamageCount, 4u)) return kPacketEncodeOverflow;
    for (std::list<DamageEntry>::const_iterator Entry = Fields.Damage.Entries.begin();
         Entry != Fields.Damage.Entries.end(); ++Entry) {
        if (!Stream.Put(&Entry->SubObjId, 2u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&Entry->Fate, 4u)) return kPacketEncodeOverflow;
        if (!Stream.PutFloat(Entry->HitPts)) return kPacketEncodeOverflow;
    }
    return 0;
}

// 06c09341, one record into the zeroed 0x18 block. Returns 0, the stream error, or (RC) 5
// when the hardpoint text allocation fails.
static unsigned int RpcServerDecodeCreateObjectEquip(FPacketBase& Packet,
    FRpcWireStream& Stream, FRpcServerEquipRecord* Record) {
    unsigned char Flags = 0;
    if (!Stream.Get(&Flags, 1u)) return Packet.MarshalError;
    Record->bMission = static_cast<unsigned char>((Flags >> 1) & 1u);
    Record->bEquipped = static_cast<unsigned char>(Flags & 1u);
    if ((Flags & kEquipRecordUnitCount) != 0) {
        Record->Count = 1;
    } else if ((Flags & kEquipRecordWideCount) != 0) {
        if (!Stream.Get(&Record->Count, 4u)) return Packet.MarshalError;
    } else {
        unsigned char Narrow = 0;
        if (!Stream.Get(&Narrow, 1u)) return Packet.MarshalError;
        Record->Count = static_cast<int>(Narrow);
    }
    if ((Flags & kEquipRecordFullHealth) != 0) {
        Record->Health = 1.0f;
    } else {
        unsigned char Health = 0;
        if (!Stream.Get(&Health, 1u)) return Packet.MarshalError;
        RpcServerExpand255(Health, &Record->Health);
    }
    unsigned short SmallArch = 0;
    if (!Stream.Get(&SmallArch, 2u)) return Packet.MarshalError;
    Record->ArchId = Archetype::SmallIDToLargeID(SmallArch);
    if ((Flags & kEquipRecordWideId) != 0) {
        if (!Stream.Get(&Record->Id, 2u)) return Packet.MarshalError;
    } else {
        unsigned char Narrow = 0;
        if (!Stream.Get(&Narrow, 1u)) return Packet.MarshalError;
        Record->Id = static_cast<unsigned short>(Narrow);
    }
    if ((Flags & kEquipRecordNoHardpoint) != 0) {
        Record->HardpointSize = 0;
        Record->Hardpoint = nullptr;
        return 0;
    }
    if ((Flags & kEquipRecordHardpointText) != 0) {
        unsigned char Length = 0;
        if (!Stream.Get(&Length, 1u)) return Packet.MarshalError;
        Record->HardpointSize = static_cast<unsigned short>(Length);
        if (Record->HardpointSize == 0) return 0;
#if defined(REMOTECLIENT_EXPORTS)
        // RC 06b51d95: the run is checked first (not committed); a failed allocation
        // returns 5.
        if (Packet.MarshalCursor + Record->HardpointSize > Packet.MarshalLimit)
            Packet.MarshalError = kPacketDecodeOverrun;
        if (Packet.MarshalError != 0) return Packet.MarshalError;
        Record->Hardpoint = static_cast<char*>(
            ::operator new(Record->HardpointSize, std::nothrow));
        if (Record->Hardpoint == nullptr) return kPacketNoMemory;
#else
        // RS allocates before the check, no null test.
        Record->Hardpoint = static_cast<char*>(
            ::operator new(Record->HardpointSize, std::nothrow));
#endif
        if (!Stream.Get(Record->Hardpoint, Record->HardpointSize)) return Packet.MarshalError;
        return 0;
    }
    unsigned char Code = 0;
    if (!Stream.Get(&Code, 1u)) return Packet.MarshalError;
    // An unknown code leaves the record without a hardpoint.
    const char* Name = RpcServerHardpointCodeToName(static_cast<unsigned int>(Code));
    if (Name == nullptr) return 0;
    // strlen + 1 as a word, a bare operator new of that word.
    Record->HardpointSize = static_cast<unsigned short>(std::strlen(Name) + 1u);
    Record->Hardpoint = static_cast<char*>(
        ::operator new(static_cast<unsigned int>(Record->HardpointSize), std::nothrow));
    const char* Text = Name;
    char* Out = Record->Hardpoint;
    char Value;
    do {
        Value = *Text++;
        *Out++ = Value;
    } while (Value != '\0');
    return 0;
}

// RC 06b51750 / RS 06c08f40. A failed read returns at once; a record being decoded is
// not linked yet and is leaked, as in retail (reproduced on purpose).
unsigned int RpcServerCreateObjectDecode(FPacketBase& Packet, FRpcWireStream& Stream,
    FRpcServerCreateObjectFields& Fields) {
    if (!Stream.Get(&Fields.ObjId, 4u)) return Packet.MarshalError;
    unsigned short SmallArch = 0;
    if (!Stream.Get(&SmallArch, 2u)) return Packet.MarshalError;
    Fields.ArchetypeId = Archetype::SmallIDToLargeID(SmallArch);
    if (!Stream.Get(&Fields.DockingBaseId, 4u)) return Packet.MarshalError;
    if (!Stream.Get(&Fields.VibeGroupId, 4u)) return Packet.MarshalError;
    if (!Stream.Get(&Fields.Costume.Body, 4u)) return Packet.MarshalError;
    if (!Stream.Get(&Fields.Costume.Head, 4u)) return Packet.MarshalError;
    Fields.Costume.LeftHand = 0;
    Fields.Costume.RightHand = 0;
    unsigned char IdCount = 0;
    if (!Stream.Get(&IdCount, 1u)) return Packet.MarshalError;
    Fields.Costume.Count = static_cast<int>(IdCount);
    if (Fields.Costume.Count != 0) {
        const unsigned int Bytes = static_cast<unsigned int>(Fields.Costume.Count) << 2;
#if defined(REMOTECLIENT_EXPORTS)
        // RC 06b518bd: the run is checked first (not committed); a failed allocation
        // returns 5.
        if (Packet.MarshalCursor + Bytes > Packet.MarshalLimit) Packet.MarshalError = kPacketDecodeOverrun;
        if (Packet.MarshalError != 0) return Packet.MarshalError;
        Fields.Costume.Ids = static_cast<unsigned int*>(::operator new(Bytes, std::nothrow));
        if (Fields.Costume.Ids == nullptr) return kPacketNoMemory;
#else
        // RS allocates before the check, no null test.
        Fields.Costume.Ids = static_cast<unsigned int*>(::operator new(Bytes, std::nothrow));
#endif
        if (!Stream.Get(Fields.Costume.Ids, Bytes)) return Packet.MarshalError;
    }
    if (!Stream.Get(&Fields.VoiceId, 4u)) return Packet.MarshalError;
    // One 12-byte check, then an integer copy (06bda4c0).
    if (!Stream.Get(&Fields.Position, 12u)) return Packet.MarshalError;
    // The four bytes are read first, then sign-extended and stored x, y, z, w.
    unsigned char QuatX = 0;
    unsigned char QuatY = 0;
    unsigned char QuatZ = 0;
    unsigned char QuatW = 0;
    if (!Stream.Get(&QuatX, 1u)) return Packet.MarshalError;
    if (!Stream.Get(&QuatY, 1u)) return Packet.MarshalError;
    if (!Stream.Get(&QuatZ, 1u)) return Packet.MarshalError;
    if (!Stream.Get(&QuatW, 1u)) return Packet.MarshalError;
    RpcServerExpand127(QuatX, &Fields.Orientation.x);
    RpcServerExpand127(QuatY, &Fields.Orientation.y);
    RpcServerExpand127(QuatZ, &Fields.Orientation.z);
    RpcServerExpand127(QuatW, &Fields.Orientation.w);
    unsigned char Hull = 0;
    if (!Stream.Get(&Hull, 1u)) return Packet.MarshalError;
    Fields.HitPts = RpcServerExpandHull(Hull, Fields.ArchetypeId);
    if (SinglePlayer()) {
        if (!Stream.Get(&Fields.LoadoutId, 4u)) return Packet.MarshalError;
    }
    unsigned short EquipCount = 0;
    if (!Stream.Get(&EquipCount, 2u)) return Packet.MarshalError;
    // The counter lives in the stack slot the collision loop below copies from.
    unsigned int EquipIndex = 0;
    for (; static_cast<unsigned short>(EquipIndex) < EquipCount; ++EquipIndex) {
        // A null record skips every read.
        FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
            ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
        if (Record == nullptr) continue;
        Record->ArchId = 0;
        Record->Count = 0;
        Record->Health = 0.0f;
        Record->Id = 0;
        Record->bEquipped = 0;
        Record->bMission = 0;
        Record->HardpointSize = 0;
        Record->Hardpoint = nullptr;
        const unsigned int Error = RpcServerDecodeCreateObjectEquip(Packet, Stream, Record);
        if (Error != 0) return Error;
        Fields.Equipment.PushBack(Record);
    }
    unsigned char GroupCount = 0;
    if (!Stream.Get(&GroupCount, 1u)) return Packet.MarshalError;
    for (unsigned int Group = 0; Group < static_cast<unsigned int>(GroupCount); ++Group) {
        // The node is linked first; its value is the stack pair {equipment counter, residue}
        // (06bd59e0). The Id word is overwritten below and the counter's high word is 0; the
        // residue is overwritten by the health read or the packet fails, so 0 here is exact.
        CollisionGroupDesc Value;
        Value.Id = static_cast<unsigned short>(EquipIndex & 0xffffu);
        Value.Pad02 = static_cast<unsigned short>(EquipIndex >> 16);
        Value.Health = 0.0f;
        Fields.CollisionGroups.push_back(Value);
        CollisionGroupDesc& Linked = Fields.CollisionGroups.back();
        unsigned char GroupId = 0;
        if (!Stream.Get(&GroupId, 1u)) return Packet.MarshalError;
        Linked.Id = static_cast<unsigned short>(GroupId);
        unsigned char Health = 0;
        if (!Stream.Get(&Health, 1u)) return Packet.MarshalError;
        RpcServerExpand255(Health, &Linked.Health);
    }
    if (!SinglePlayer()) return 0;
    unsigned int DamageCount = 0;
    if (!Stream.Get(&DamageCount, 4u)) return Packet.MarshalError;
    for (unsigned int Index = 0; Index < DamageCount; ++Index) {
        // The seeded entry is linked and counted before the reads fill it in place.
        DamageEntry Value;
        Value.SubObjId = SUBOBJ_ID_NONE;
        Value.HitPts = -1.0f;
        Value.Fate = DamageEntry::SubObjFate::None;
        Fields.Damage.Entries.push_back(Value);
        ++Fields.Damage.Count;
        DamageEntry& Linked = Fields.Damage.Entries.back();
        if (!Stream.Get(&Linked.SubObjId, 2u)) return Packet.MarshalError;
        if (!Stream.Get(&Linked.Fate, 4u)) return Packet.MarshalError;
        if (!Stream.Get(&Linked.HitPts, 4u)) return Packet.MarshalError;     // integer move
    }
    return 0;
}

#if defined(REMOTECLIENT_EXPORTS)
// RC 06b382f0 (__cdecl). Every store is an integer move except the equipment records' health
// (x87 inside the record builder).
void RpcServerCreateObjectFill(const CEqObjCreateInfo* Info,
    FRpcServerCreateObjectFields* Fields) {
    Fields->ObjId = Info->ObjId;
    Fields->ArchetypeId = Info->ArchetypeNickname;
    Fields->DockingBaseId = Info->DockingBaseId;
    Fields->VibeGroupId = Info->VibeGroupId;
    Fields->VoiceId = Info->VoiceId;
    Fields->HitPts = Info->HitPts;
    Fields->LoadoutId = Info->LoadoutId;
    Fields->Costume.Head = Info->CostumeHead;
    Fields->Costume.Body = Info->CostumeBody;
    Fields->Costume.LeftHand = Info->CostumeLeftHand;
    Fields->Costume.RightHand = Info->CostumeRightHand;
    Fields->Costume.Count = Info->CostumeAccessoryCount;
    // A bare operator new of Count << 2, stored untested; the copy (signed, count > 0) is not
    // bounded by the eight slots: reproduced on purpose.
    Fields->Costume.Ids = static_cast<unsigned int*>(::operator new(
        static_cast<unsigned int>(Info->CostumeAccessoryCount) << 2, std::nothrow));
    const unsigned int* Accessories = &Info->CostumeAccessory[0];
    for (int Index = 0; Index < Info->CostumeAccessoryCount; ++Index) {
        Fields->Costume.Ids[Index] = Accessories[Index];
    }
    std::memcpy(&Fields->Position, &Info->Position, 12u);
    std::memcpy(&Fields->Orientation, &Info->Orientation, 16u);
    // The record builder RC 06b38210 inlined with bAll true.
    RpcServerEquipHandleFromVector(&Info->Equipment, &Fields->Equipment, true);
    Fields->CollisionGroups = Info->CollisionGroups;
    Fields->Damage = Info->Damage;
}
#endif

#if defined(REMOTESERVER_EXPORTS)
// Common's VC6 vector<EquipDesc>::insert(end, 1, Value) as RS 06bd5398 calls it:
// without room, a block of size + max(size, 1) elements, the old elements and the
// new one copy-constructed into it, the old block freed; otherwise one copy at end.
static void RpcServerEquipDescVectorInsertEnd(EquipDescVector* Vector, const EquipDesc& Value) {
    if (static_cast<unsigned int>(Vector->Cap - Vector->End) < 1u) {
        const unsigned int Size = (Vector->Begin == nullptr)
            ? 0u : static_cast<unsigned int>(Vector->End - Vector->Begin);
        const unsigned int Capacity = Size + ((1u < Size) ? Size : 1u);
        EquipDesc* Block = static_cast<EquipDesc*>(
            ::operator new(sizeof(EquipDesc) * Capacity, std::nothrow));
        EquipDesc* Out = Block;
        for (EquipDesc* In = Vector->Begin; In != Vector->End; ++In, ++Out) {
            new (Out) EquipDesc(*In);
        }
        new (Out) EquipDesc(Value);
        ::operator delete(Vector->Begin);
        Vector->Cap = Block + Capacity;
        Vector->End = Block + Size + 1u;
        Vector->Begin = Block;
        return;
    }
    new (Vector->End) EquipDesc(Value);
    ++Vector->End;
}

// RS 06bd52a0 (__cdecl).
void RpcServerCreateObjectRead(const FRpcServerCreateObjectFields* Fields,
    CEqObjCreateInfo* Info) {
    Info->ObjId = Fields->ObjId;
    Info->ArchetypeNickname = Fields->ArchetypeId;
    Info->DockingBaseId = Fields->DockingBaseId;
    Info->CostumeBody = Fields->Costume.Body;
    Info->CostumeHead = Fields->Costume.Head;
    Info->CostumeRightHand = Fields->Costume.RightHand;
    Info->CostumeLeftHand = Fields->Costume.LeftHand;
    Info->CostumeAccessoryCount = Fields->Costume.Count;
    // A signed count > 0, then an unsigned loop that re-reads the packet's count; not bounded
    // by the eight slots: reproduced on purpose.
    if (Fields->Costume.Count > 0) {
        unsigned int* Accessories = &Info->CostumeAccessory[0];
        for (unsigned int Index = 0; Index < static_cast<unsigned int>(Fields->Costume.Count);
             ++Index) {
            Accessories[Index] = Fields->Costume.Ids[Index];
        }
    }
    Info->VibeGroupId = Fields->VibeGroupId;
    Info->VoiceId = Fields->VoiceId;
    Info->HitPts = Fields->HitPts;
    Info->LoadoutId = Fields->LoadoutId;
    // The position moves through x87 (load z, y, x; store x, y, z).
    const float* PositionIn = &Fields->Position.x;
    float* PositionOut = &Info->Position.x;
    __asm {
        mov eax, PositionIn
        mov edx, PositionOut
        fld dword ptr [eax + 8]
        fld dword ptr [eax + 4]
        fld dword ptr [eax]
        fstp dword ptr [edx]
        fstp dword ptr [edx + 4]
        fstp dword ptr [edx + 8]
    }
    // An identity quaternion is built in place, then overwritten with integer moves.
    Info->Orientation.w = 1.0f;
    Info->Orientation.z = 0.0f;
    Info->Orientation.y = 0.0f;
    Info->Orientation.x = 0.0f;
    std::memcpy(&Info->Orientation, &Fields->Orientation, 16u);
    // Per record (used untested) a default EquipDesc is appended and filled through Common's
    // setters in this order.
    const std::list<FRpcServerEquipRecord*>* Records = Fields->Equipment.Records;
    if (Records != nullptr) {
        for (std::list<FRpcServerEquipRecord*>::const_iterator It = Records->begin();
             It != Records->end(); ++It) {
            const FRpcServerEquipRecord* Record = *It;
            const EquipDesc Blank;
            RpcServerEquipDescVectorInsertEnd(&Info->Equipment, Blank);
            EquipDesc* Slot = Info->Equipment.End - 1;
            Slot->set_arch_id(Record->ArchId);
            Slot->set_count(Record->Count);
            Slot->set_id(Record->Id);
            Slot->set_equipped(Record->bEquipped);                           // raw byte
            Slot->set_status(Record->Health);
            if (Record->HardpointSize > 0) {
                CacheString Hardpoint;
                Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
                Slot->set_hardpoint(Hardpoint);
            } else {
                Slot->make_internal();
            }
        }
    }
    Info->CollisionGroups = Fields->CollisionGroups;
    Info->Damage = Fields->Damage;
}
#endif
// ===== end shared SERVER helpers =====

#if defined(REMOTECLIENT_EXPORTS) || defined(REMOTESERVER_EXPORTS)

// RC 06b49675 / RS 06be4135: the same table in both images. Native registration order;
// every RegisterType result is ignored.
#if defined(REMOTECLIENT_EXPORTS)
int RpcRegisterRemoteClientServerPackets(FPacketSerializer* Serializer) {
#else
int RpcRegisterRemoteServerServerPackets(FPacketSerializer* Serializer) {
#endif
    if (Serializer == nullptr) {
        return 4;
    }
    Serializer->RegisterType(FLPACKET_SERVER_CONNECTRESPONSE, "FLPACKET_SERVER_CONNECTRESPONSE", &FRpcConnectResponsePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_LOGINRESPONSE, "FLPACKET_SERVER_LOGINRESPONSE", &FRpcLoginResponsePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_DAMAGEOBJECT, "FLPACKET_SERVER_DAMAGEOBJECT",
        &FRpcServerDamageObjectPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_DESTROYOBJECT, "FLPACKET_SERVER_DESTROYOBJECT",
        &FRpcServerDestroyObjectPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_CREATESHIP, "FLPACKET_SERVER_CREATESHIP", &FRpcServerCreateShipPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_REQUEST_RETURNED, "FLPACKET_SERVER_REQUEST_RETURNED",
        &FRpcServerRequestReturnedPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_CHARACTERINFO, "FLPACKET_SERVER_CHARACTERINFO",
        &FRpcServerCharacterInfoPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_LAUNCH, "FLPACKET_SERVER_LAUNCH", &FRpcServerLaunchPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_CHARSELECTVERIFIED, "FLPACKET_SERVER_CHARSELECTVERIFIED",
        &FRpcServerCharSelectVerifiedPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_ACTIVATEOBJECT, "FLPACKET_SERVER_ACTIVATEOBJECT",
        &FRpcServerActivateObjectPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_LAND, "FLPACKET_SERVER_LAND", &FRpcServerLandPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST, "FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST",
        &FRpcServerGfCompleteNewsBroadcastListPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST, "FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST",
        &FRpcServerGfCompleteMissionComputerListPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFCOMPLETECHARLIST, "FLPACKET_SERVER_GFCOMPLETECHARLIST",
        &FRpcServerGfCompleteCharListPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST, "FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST",
        &FRpcServerGfCompleteScriptBehaviorListPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST, "FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST",
        &FRpcServerGfCompleteAmbientScriptListPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SYSTEM_SWITCH_OUT, "FLPACKET_SERVER_SYSTEM_SWITCH_OUT",
        &FRpcServerSystemSwitchOutPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SYSTEM_SWITCH_IN, "FLPACKET_SERVER_SYSTEM_SWITCH_IN",
        &FRpcServerSystemSwitchInPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETSHIPARCH, "FLPACKET_SERVER_SETSHIPARCH", &FRpcSetShipArchPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETEQUIPMENT, "FLPACKET_SERVER_SETEQUIPMENT",
        &FRpcServerSetEquipmentPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETCARGO, "FLPACKET_SERVER_SETCARGO", &FRpcServerSetCargoPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_REQUESTCREATESHIPRESP, "FLPACKET_SERVER_REQUESTCREATESHIPRESP",
        &FRpcServerRequestCreateShipRespPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETSTARTROOM, "FLPACKET_SERVER_SETSTARTROOM",
        &FRpcServerSetStartRoomPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFDESTROYCHARACTER, "FLPACKET_SERVER_GFDESTROYCHARACTER",
        &FRpcServerGfDestroyCharacterPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFDESTROYNEWSBROADCAST, "FLPACKET_SERVER_GFDESTROYNEWSBROADCAST",
        &FRpcServerGfDestroyNewsBroadcastPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER, "FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER",
        &FRpcServerGfDestroyMissionComputerPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR, "FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR",
        &FRpcServerGfDestroyScriptBehaviorPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT, "FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT",
        &FRpcServerGfDestroyAmbientScriptPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFSCRIPTBEHAVIOR, "FLPACKET_SERVER_GFSCRIPTBEHAVIOR",
        &FRpcServerGfScriptBehaviorPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT, "FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT",
        &FRpcServerGfUpdateAmbientScriptPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_MISSIONSAVEA, "FLPACKET_SERVER_MISSIONSAVEA",
        &FRpcServerMissionSaveAPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER, "FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER",
        &FRpcServerGfUpdateMissionComputerPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFUPDATENEWSBROADCAST, "FLPACKET_SERVER_GFUPDATENEWSBROADCAST",
        &FRpcServerGfUpdateNewsBroadcastPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE, "FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE",
        &FRpcServerGfMissionVendorAcceptancePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY, "FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY",
        &FRpcServerGfMissionVendorWhyEmptyPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_GFUPDATECHAR, "FLPACKET_SERVER_GFUPDATECHAR",
        &FRpcServerGfUpdateCharPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_CREATELOOT, "FLPACKET_SERVER_CREATELOOT", &FRpcServerCreateLootPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETREPUTATION, "FLPACKET_SERVER_SETREPUTATION",
        &FRpcServerSetReputationPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_ADJUSTATTITUDE, "FLPACKET_SERVER_ADJUSTATTITUDE",
        &FRpcServerAdjustAttitudePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETGROUPFEELINGS, "FLPACKET_SERVER_SETGROUPFEELINGS",
        &FRpcServerSetGroupFeelingsPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_CREATEMINE, "FLPACKET_SERVER_CREATEMINE", &FRpcServerCreateMinePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_CREATECOUNTER, "FLPACKET_SERVER_CREATECOUNTER",
        &FRpcServerCreateCounterPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_EXPLODEASTEROIDMINE, "FLPACKET_SERVER_EXPLODEASTEROIDMINE",
        &FRpcServerExplodeAsteroidMinePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_LOADHINT, "FLPACKET_SERVER_LOADHINT", &FRpcServerLoadHintPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETDIRECTIVE, "FLPACKET_SERVER_SETDIRECTIVE",
        &FRpcServerSetDirectivePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_CREATEGUIDED, "FLPACKET_SERVER_CREATEGUIDED",
        &FRpcServerCreateGuidedPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_ITEMTRACTORED, "FLPACKET_SERVER_ITEMTRACTORED",
        &FRpcServerItemTractoredPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETREMOVEITEM, "FLPACKET_SERVER_SETREMOVEITEM",
        &FRpcServerSetRemoveItemPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETADDITEM, "FLPACKET_SERVER_SETADDITEM", &FRpcServerSetAddItemPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETCASH, "FLPACKET_SERVER_SETCASH", &FRpcServerSetCashPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_REQUESTSPACESCRIPT, "FLPACKET_SERVER_REQUESTSPACESCRIPT",
        &FRpcServerRequestSpaceScriptPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE, "FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE",
        &FRpcServerSetMissionObjectiveStatePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_CFGINTERFACENOTIFICATION, "FLPACKET_SERVER_CFGINTERFACENOTIFICATION",
        &FRpcServerCfgInterfaceNotificationPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_MARKOBJ, "FLPACKET_SERVER_MARKOBJ", &FRpcServerMarkObjPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETMISSIONOBJECTIVES, "FLPACKET_SERVER_SETMISSIONOBJECTIVES",
        &FRpcServerSetMissionObjectivesPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE, "FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE",
        &FRpcServerReplaceMissionObjectivePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SET_STORY_CUE, "FLPACKET_SERVER_SET_STORY_CUE",
        &FRpcServerSetStoryCuePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SET_CAMERA, "FLPACKET_SERVER_SET_CAMERA", &FRpcServerSetCameraPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_REVERT_CAMERA, "FLPACKET_SERVER_REVERT_CAMERA",
        &FRpcServerRevertCameraPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SET_MISSION_MESSAGE, "FLPACKET_SERVER_SET_MISSION_MESSAGE",
        &FRpcServerSetMissionMessagePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_REPAIROBJECT, "FLPACKET_SERVER_REPAIROBJECT",
        &FRpcServerRepairObjectPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETNUMKILLS, "FLPACKET_SERVER_SETNUMKILLS", &FRpcServerSetNumKillsPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETMISSIONSUCCESSES, "FLPACKET_SERVER_SETMISSIONSUCCESSES",
        &FRpcServerSetMissionSuccessesPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETMISSIONFAILURES, "FLPACKET_SERVER_SETMISSIONFAILURES",
        &FRpcServerSetMissionFailuresPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_BURNFUSE, "FLPACKET_SERVER_BURNFUSE", &FRpcServerBurnFusePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_CREATESOLAR, "FLPACKET_SERVER_CREATESOLAR", &FRpcServerCreateSolarPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETCOLLISIONGROUPS, "FLPACKET_SERVER_SETCOLLISIONGROUPS",
        &FRpcServerSetCollisionGroupsPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETHULLSTATUS, "FLPACKET_SERVER_SETHULLSTATUS", &FRpcSetHullStatusPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SETGUIDEDTARGET, "FLPACKET_SERVER_SETGUIDEDTARGET",
        &FRpcServerSetGuidedTargetPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SENDCOMM, "FLPACKET_SERVER_SENDCOMM", &FRpcServerSendCommPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_USE_ITEM, "FLPACKET_SERVER_USE_ITEM", &FRpcServerUseItemPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SCANNOTIFY, "FLPACKET_SERVER_SCANNOTIFY", &FRpcServerScanNotifyPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_PLAYERLIST, "FLPACKET_SERVER_PLAYERLIST", &FRpcServerPlayerListPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE, "FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE",
        &FRpcServerRemoteObjectCargoUpdatePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_MISCOBJUPDATE, "FLPACKET_SERVER_MISCOBJUPDATE",
        &FRpcServerMiscObjUpdatePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_FORMATION_UPDATE, "FLPACKET_SERVER_FORMATION_UPDATE",
        &FRpcServerFormationUpdatePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_OBJECTCARGOUPDATE, "FLPACKET_SERVER_OBJECTCARGOUPDATE",
        &FRpcServerObjectCargoUpdatePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SENDNNMESSAGE, "FLPACKET_SERVER_SENDNNMESSAGE",
        &FRpcServerSendNNMessagePacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_SET_MUSIC, "FLPACKET_SERVER_SET_MUSIC", &FRpcServerSetMusicPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_CANCEL_MUSIC, "FLPACKET_SERVER_CANCEL_MUSIC", &FRpcServerCancelMusicPacket::Factory);
    Serializer->RegisterType(FLPACKET_SERVER_PLAY_SOUND_EFFECT, "FLPACKET_SERVER_PLAY_SOUND_EFFECT",
        &FRpcServerPlaySoundEffectPacket::Factory);
    return 0;
}

#endif

// ===== SERVER 0x01..0x1f =====
// Bodies compiled into both DLLs: each RC/RS pair below differs only in addresses.

// RC 06b3e780 / RS 06bdb450 (= rpclocal 06d90480). The count is the low word of the list's
// size. Retail loads all three entry fields before writing (the error exit pops the unused
// float); PutFloat loads after the check instead, with the same bytes and error paths.
unsigned int FRpcServerDamageObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Damage.bDestroyed, 1u)) return kPacketEncodeOverflow;             // raw byte
    if (!Stream.Put(&Damage.Cause, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Damage.InflictorId, 4u)) return kPacketEncodeOverflow;
    const unsigned short Count = static_cast<unsigned short>(Damage.Entries.size());
    if (!Stream.Put(&Count, 2u)) return kPacketEncodeOverflow;
    for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
         It != Damage.Entries.end(); ++It) {
        if (!Stream.Put(&It->SubObjId, 2u)) return kPacketEncodeOverflow;
        const unsigned char Fate = static_cast<unsigned char>(
            static_cast<unsigned int>(It->Fate) & 0xffu);
        if (!Stream.Put(&Fate, 1u)) return kPacketEncodeOverflow;
        if (!Stream.PutFloat(It->HitPts)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b3e9c0 / RS 06bdb690 (= rpclocal 06d906c0). The decoder APPENDS to the list the ctor
// built; the count is only the (unsigned) loop bound. Each entry is read into a temporary (Fate
// byte zero-extended, float through x87) and only then linked in. The byte at +0x30 is stored
// raw, reproduced on purpose.
unsigned int FRpcServerDamageObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&Damage.bDestroyed, 1u)) return MarshalError;
    if (!Stream.Get(&Damage.Cause, 4u)) return MarshalError;
    if (!Stream.Get(&Damage.InflictorId, 4u)) return MarshalError;
    unsigned short Count = 0;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    for (unsigned int Index = 0; Index < static_cast<unsigned int>(Count); ++Index) {
        // Retail seeds the temporary with SUBOBJ_ID_NONE, -1.0f, 0; every field is overwritten
        // below.
        DamageEntry Entry;
        if (!Stream.Get(&Entry.SubObjId, 2u)) return MarshalError;
        unsigned char Fate = 0;
        if (!Stream.Get(&Fate, 1u)) return MarshalError;
        if (!Stream.Reserve(4u, kPacketDecodeOverrun)) return MarshalError;
        RpcServerX87CopyFloat(&Entry.HitPts,
            reinterpret_cast<const float*>(MarshalPtr));
        Stream.CommitReadPointer(4u);
        Entry.Fate = static_cast<DamageEntry::SubObjFate>(static_cast<unsigned int>(Fate));
        Damage.Entries.push_back(Entry);
        ++Damage.Count;
    }
    return Stream.Finish(OutLength, false);
}

// RC 06b5cf10 / RS 06bf03e0.
unsigned int FRpcServerDestroyObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&DestroyType, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5d030 / RS 06bf0500.
unsigned int FRpcServerDestroyObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&DestroyType, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// RC 06b4b2e0 / RS 06be5e00.
FRpcServerLaunchPacket::FRpcServerLaunchPacket() : FPacketBase(FLPACKET_SERVER_LAUNCH) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

// RC 06b5e770 / RS 06bf2fc0. Position and Orientation are each one reservation followed by
// an integer dword copy (06b3a160 / 06b39ec0).
unsigned int FRpcServerLaunchPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ShipId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&LaunchObjId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&LaunchPosIndex, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Position, 12u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Orientation, 16u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5e970 / RS 06bf31c0.
unsigned int FRpcServerLaunchPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ShipId, 4u)) return MarshalError;
    if (!Stream.Get(&LaunchObjId, 4u)) return MarshalError;
    if (!Stream.Get(&LaunchPosIndex, 4u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    if (!Stream.Get(&Orientation, 16u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// RC 06b5eb10 / RS 06bf3360: prints Position only.
void FRpcServerLaunchPacket::Dump(std::ostream& Out) const {
    Out << "   X: " << Position.x << "\n";
    Out << "   Y: " << Position.y << "\n";
    Out << "   Z: " << Position.z << "\n";
}

// RC 06b5fed0 / RS 06bf4aa0. The double crosses the x87 stack after its 8-byte reservation;
// signaling NaNs are quieted as in retail. The destination is cached before the store and the
// pointer committed after it.
unsigned int FRpcServerCharSelectVerifiedPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&RepInstance, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Reserve(8u, kPacketEncodeOverflow)) return kPacketEncodeOverflow;
    const double* Source = &Time;
    const unsigned int Cached = MarshalPtr;
    double* Destination = reinterpret_cast<double*>(Cached);
    __asm {
        mov eax, Source
        mov edx, Destination
        fld qword ptr [eax]
        fstp qword ptr [edx]
    }
    MarshalPtr = Cached + 8u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5fff0 / RS 06bf4bc0: the double is read back as two integer dwords.
unsigned int FRpcServerCharSelectVerifiedPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&RepInstance, 4u)) return MarshalError;
    if (!Stream.Get(&Time, 8u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// RC 06b60100 / RS 06bf4cd0.
void FRpcServerCharSelectVerifiedPacket::Dump(std::ostream& Out) const {
    Out << "   RepInstance: " << RepInstance << " time: " << Time << "\n";
}

// RC 06b5cc60 / RS 06bf0130.
unsigned int FRpcServerActivateObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bActivate, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ActivateParam, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5cdb0 / RS 06bf0280.
unsigned int FRpcServerActivateObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&bActivate, 1u)) return MarshalError;
    if (!Stream.Get(&ActivateParam, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// Blob family 1a/1d/1e/1f encode, RC 06b3d170/06b3d9f0/06b3e240/06b3d4d0 and RS
// 06bf68a0/06bf6bc0/06bf7390/06bf76b0. Size 0 still runs every check.
unsigned int RpcServerBlobEncode(FPacketBase& Packet, const unsigned int& Size,
    const unsigned char* Data, unsigned char** BufferHolder, unsigned int Capacity,
    unsigned int* OutLength) {
    FRpcWireStream Stream(Packet, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Size, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(Data, Size)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// Blob family decode. The two DLLs differ:
// - RC 06b3d2b0/06b3db30/06b3e380/06b3d610: read Size; check cursor+Size against the
//   limit WITHOUT committing it (0x17); operator new(Size), null -> return 5; then
//   commit/check the body again and copy it.
// - RS 06bf69e0/06bf6d00/06bf74d0/06bf77f0: read Size; operator new(Size) with no
//   prior check and no null test; then commit/check the body and copy it.
// A block already held is overwritten, not freed (native).
unsigned int RpcServerBlobDecode(FPacketBase& Packet, unsigned int& Size,
    unsigned char*& Data, const unsigned char* Buffer, unsigned int InSize,
    unsigned int* OutLength) {
    FRpcWireStream Stream(Packet, const_cast<unsigned char*>(Buffer), InSize);
    if (!Stream.Get(&Size, 4u)) return Packet.MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    if (Packet.MarshalCursor + Size > Packet.MarshalLimit) Packet.MarshalError = kPacketDecodeOverrun;
    if (Packet.MarshalError != 0) return Packet.MarshalError;
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
    if (Data == nullptr) return kPacketNoMemory;
#else
    // No null test, reproduced on purpose: a failed allocation faults in the copy.
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
#endif
    if (!Stream.Get(Data, Size)) return Packet.MarshalError;
    return Stream.Finish(OutLength, false);
}

// 0x04 CREATESHIP. RS addresses; the RC bodies are identical except the decoder's #if branch.

// RS 06be5340 (the block's members are built by FRpcServerCreateObjectFields).
FRpcServerCreateShipPacket::FRpcServerCreateShipPacket()
    : FPacketBase(FLPACKET_SERVER_CREATESHIP), bUsingTradelane(0), Throttle(0.0f), OwnerPlayer(0),
      GroupNameId(0), ReputationRank(0), GroupId(0), NameASize(0), NameA(nullptr),
      NameBSize(0), NameB(nullptr), DisplayNameLength(0), DisplayName(nullptr),
      Affiliation(0), Attitude(0.0f), bMarkedBy(0) {
    Velocity.x = 0.0f;
    Velocity.y = 0.0f;
    Velocity.z = 0.0f;
    AngularVelocity.x = 0.0f;
    AngularVelocity.y = 0.0f;
    AngularVelocity.z = 0.0f;
}

// RS 06bf0c10.
FRpcServerCreateShipPacket::~FRpcServerCreateShipPacket() {
    if (DisplayName != nullptr) {
        ::operator delete(DisplayName);
        DisplayName = nullptr;
    }
    if (NameB != nullptr) ::operator delete(NameB);
    if (NameA != nullptr) ::operator delete(NameA);
}

// RS 06bf0da0 / RC 06b3f9b0.
unsigned int FRpcServerCreateShipPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    // The return is discarded; the sticky error stops the next field.
    RpcServerCreateObjectEncode(*this, Stream, Object);
    unsigned char Flags = 0;
    if (bUsingTradelane != 0) Flags = kCreateShipFlagUsingTradelane;
    if (OwnerPlayer != 0) Flags |= kCreateShipFlagOwnerPlayer;                                        // dword test
    if (bMarkedBy != 0) Flags |= kCreateShipFlagMarkedBy;
    if (!Stream.Put(&Flags, 1u)) return kPacketEncodeOverflow;
    // One 12-byte check, then an integer copy (06bda4c0).
    if (!Stream.Put(&Velocity, 12u)) return kPacketEncodeOverflow;
    const unsigned char ThrottleByte = RpcServerQuantize127(&Throttle);
    if (!Stream.Put(&ThrottleByte, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&GroupNameId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ReputationRank, 1u)) return kPacketEncodeOverflow;
    if ((Flags & kCreateShipFlagOwnerPlayer) != 0) {
        const unsigned char Owner = static_cast<unsigned char>(OwnerPlayer & 0xffu);
        if (!Stream.Put(&Owner, 1u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&GroupId, 2u)) return kPacketEncodeOverflow;
        if (!SinglePlayer()) {
            // The low byte of the length, then 2 * length bytes.
            const unsigned char Length =
                static_cast<unsigned char>(static_cast<unsigned int>(DisplayNameLength) & 0xffu);
            if (!Stream.Put(&Length, 1u)) return kPacketEncodeOverflow;
            if (!Stream.PutBlock(DisplayName,
                    static_cast<unsigned int>(DisplayNameLength) << 1))
                return kPacketEncodeOverflow;
        }
    } else if (!SinglePlayer()) {
        if (!Stream.Put(&NameASize, 4u)) return kPacketEncodeOverflow;
        if (!Stream.PutBlock(NameA, NameASize)) return kPacketEncodeOverflow;
        if (!Stream.Put(&NameBSize, 4u)) return kPacketEncodeOverflow;
        if (!Stream.PutBlock(NameB, NameBSize)) return kPacketEncodeOverflow;
    }
    if (!SinglePlayer()) {
        if (!Stream.Put(&Affiliation, 4u)) return kPacketEncodeOverflow;
        const unsigned char AttitudeByte = RpcServerQuantize127(&Attitude);
        if (!Stream.Put(&AttitudeByte, 1u)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RS 06bf12d0 / RC 06b3fee0. Returns the stream error, or (RC) 5 when the display name
// allocation fails.
unsigned int FRpcServerCreateShipPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // The return is discarded; the sticky error stops the next field.
    RpcServerCreateObjectDecode(*this, Stream, Object);
    unsigned char Flags = 0;
    if (!Stream.Get(&Flags, 1u)) return MarshalError;
    // The bits only ever SET the bytes.
    if ((Flags & kCreateShipFlagUsingTradelane) != 0) bUsingTradelane = 1;
    if ((Flags & kCreateShipFlagMarkedBy) != 0) bMarkedBy = 1;
    // One 12-byte check, then an integer copy (06bda4c0).
    if (!Stream.Get(&Velocity, 12u)) return MarshalError;
    unsigned char ThrottleByte = 0;
    if (!Stream.Get(&ThrottleByte, 1u)) return MarshalError;
    RpcServerExpand127(ThrottleByte, &Throttle);
    if (!Stream.Get(&GroupNameId, 2u)) return MarshalError;
    if (!Stream.Get(&ReputationRank, 1u)) return MarshalError;
    if ((Flags & kCreateShipFlagOwnerPlayer) != 0) {
        unsigned char Owner = 0;
        if (!Stream.Get(&Owner, 1u)) return MarshalError;
        OwnerPlayer = static_cast<unsigned int>(Owner);
        if (!Stream.Get(&GroupId, 2u)) return MarshalError;
        if (!SinglePlayer()) {
            unsigned char Length = 0;
            if (!Stream.Get(&Length, 1u)) return MarshalError;
            // Sign-extended (06bf14e1): a length byte of 0x80 or more is negative.
            DisplayNameLength = static_cast<int>(static_cast<signed char>(Length));
            const unsigned int Bytes = static_cast<unsigned int>(DisplayNameLength) << 1;
#if defined(REMOTECLIENT_EXPORTS)
            // RC 06b400fb: the run is checked first (not committed); a failed allocation
            // returns 5.
            if (MarshalCursor + Bytes > MarshalLimit) MarshalError = kPacketDecodeOverrun;
            if (MarshalError != 0) return MarshalError;
            DisplayName = static_cast<wchar_t*>(::operator new(Bytes + 2u, std::nothrow));
            if (DisplayName == nullptr) return kPacketNoMemory;
#else
            // RS allocates before the check, no null test.
            DisplayName = static_cast<wchar_t*>(::operator new(Bytes + 2u, std::nothrow));
#endif
            if (!Stream.Get(DisplayName, Bytes)) return MarshalError;
            DisplayName[DisplayNameLength] = L'\0';
        }
    } else if (!SinglePlayer()) {
        // RS 06bec240 / RC 06b4eb30: both returns discarded.
        RpcWireDecodeOwnedBlock(*this, Stream, NameASize, NameA);
        RpcWireDecodeOwnedBlock(*this, Stream, NameBSize, NameB);
    }
    if (!SinglePlayer()) {
        if (!Stream.Get(&Affiliation, 4u)) return MarshalError;
        unsigned char AttitudeByte = 0;
        if (!Stream.Get(&AttitudeByte, 1u)) return MarshalError;
        RpcServerExpand127(AttitudeByte, &Attitude);
    }
    return Stream.Finish(OutLength, false);
}

// ===== end SERVER 0x01..0x1f =====

// ===== SERVER 0x20..0x3f =====
// Bodies compiled into both DLLs; each RC/RS pair named below differs only in addresses.

// --- equipment list (0x24 SETEQUIPMENT, 0x25 SETCARGO) ---

// RC 06b3a450 / RS 06bdc790: every record's hardpoint, then the record, then the
// list (nodes, dummy node, header). The handle pointer itself is left as it is.
FRpcServerEquipHandle::~FRpcServerEquipHandle() {
    if (Records == nullptr) return;
    for (std::list<FRpcServerEquipRecord*>::iterator It = Records->begin();
        It != Records->end(); ++It) {
        FRpcServerEquipRecord* Record = *It;
        if (Record != nullptr) {
            if (Record->Hardpoint != nullptr) ::operator delete(Record->Hardpoint);
            ::operator delete(Record);
        }
    }
    delete Records;
}

// RC 06b3a820: the list is created on the first push_back; when that allocation fails the
// record is dropped.
void FRpcServerEquipHandle::PushBack(FRpcServerEquipRecord* Record) {
    if (Records == nullptr) {
        Records = new (std::nothrow) std::list<FRpcServerEquipRecord*>();
        if (Records == nullptr) return;
    }
    Records->push_back(Record);
}

// RC 06b3a510 / RS 06bdc850. A record whose allocation fails is skipped. Fields are integer
// copies (Health included); the hardpoint is re-allocated with the record's size and copied up
// to its terminator, not by size.
void FRpcServerEquipHandle::AppendCopy(const FRpcServerEquipHandle& Source) {
    if (Source.Records == nullptr) return;
    if (Records == nullptr) {
        Records = new (std::nothrow) std::list<FRpcServerEquipRecord*>();
        if (Records == nullptr) return;
    }
    for (std::list<FRpcServerEquipRecord*>::const_iterator It = Source.Records->begin();
        It != Source.Records->end(); ++It) {
        const FRpcServerEquipRecord* From = *It;
        FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
            ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
        if (Record == nullptr) continue;
        Record->Hardpoint = nullptr;
        Record->HardpointSize = From->HardpointSize;
        Record->ArchId = From->ArchId;
        std::memcpy(&Record->Health, &From->Health, 4u);
        Record->Count = From->Count;
        Record->Id = From->Id;
        Record->bEquipped = From->bEquipped;
        Record->bMission = From->bMission;
        if (Record->HardpointSize > 0) {
            Record->Hardpoint = static_cast<char*>(
                ::operator new(Record->HardpointSize, std::nothrow));
            const char* Text = From->Hardpoint;
            char* Out = Record->Hardpoint;
            char Value;
            do {
                Value = *Text++;
                *Out++ = Value;
            } while (Value != '\0');
        }
        Records->push_back(Record);
    }
}

// RC 06b38210. End is read once; a record allocation is not checked, so a failure writes
// through null, reproduced on purpose. Health moves through x87. The hardpoint size is
// strlen + 1 truncated to 16 bits for both the stored size and the allocation, while the copy
// runs to the terminator.
void RpcServerEquipHandleFromVector(const EquipDescVector* Source,
    FRpcServerEquipHandle* Handle, bool bAll) {
    const EquipDesc* End = Source->End;
    for (const EquipDesc* Desc = Source->Begin; Desc != End; ++Desc) {
        if (!bAll && Desc->bEquipped != 0) continue;
        FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
            ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
        if (Record != nullptr) {
            Record->ArchId = 0;
            Record->Count = 0;
            std::memset(&Record->Health, 0, 4u);
            Record->Id = 0;
            Record->bEquipped = 0;
            Record->bMission = 0;
            Record->HardpointSize = 0;
            Record->Hardpoint = nullptr;
        }
        Record->ArchId = Desc->ArchId;
        Record->Count = Desc->Count;
        RpcServerX87CopyFloat(&Record->Health, &Desc->Health);
        Record->Id = Desc->Id;
        Record->bEquipped = Desc->bEquipped;
        if (!Desc->is_internal()) {
            const unsigned int Length = static_cast<unsigned int>(std::strlen(Desc->Hardpoint)) + 1u;
            Record->HardpointSize = static_cast<unsigned short>(Length);
            Record->Hardpoint = static_cast<char*>(
                ::operator new(static_cast<unsigned short>(Length), std::nothrow));
            const char* Text = Desc->Hardpoint;
            char* Out = Record->Hardpoint;
            char Value;
            do {
                Value = *Text++;
                *Out++ = Value;
            } while (Value != '\0');
        }
        Handle->PushBack(Record);
    }
}

// RS 06bd5130. Reserve(list size) first (Common's vector<EquipDesc> reserve inlined), then one
// default EquipDesc per non-null record, filled through Common's setters in retail order.
// Every caller passes a fresh vector, so the native insert never grows it here.
void RpcServerEquipVectorFromHandle(const FRpcServerEquipHandle* Handle,
    EquipDescVector* Dest) {
    const std::list<FRpcServerEquipRecord*>* List = Handle->Records;
    if (List == nullptr) return;
    const unsigned int Wanted = static_cast<unsigned int>(List->size());
    const unsigned int Capacity = (Dest->Begin == nullptr)
        ? 0u : static_cast<unsigned int>(Dest->Cap - Dest->Begin);
    if (Capacity < Wanted) {
        EquipDesc* Block = static_cast<EquipDesc*>(::operator new(sizeof(EquipDesc) * Wanted));
        EquipDesc* Out = Block;
        for (EquipDesc* In = Dest->Begin; In != Dest->End; ++In, ++Out) {
            new (Out) EquipDesc(*In);
        }
        const unsigned int OldSize = static_cast<unsigned int>(Dest->End - Dest->Begin);
        ::operator delete(Dest->Begin);
        Dest->Cap = Block + Wanted;
        Dest->End = Block + OldSize;
        Dest->Begin = Block;
    }
    for (std::list<FRpcServerEquipRecord*>::const_iterator It = List->begin();
        It != List->end(); ++It) {
        const FRpcServerEquipRecord* Record = *It;
        if (Record == nullptr) continue;
        EquipDesc* Slot = new (Dest->End) EquipDesc();
        ++Dest->End;
        Slot->set_arch_id(Record->ArchId);
        Slot->set_count(Record->Count);
        Slot->set_status(Record->Health);
        Slot->set_id(Record->Id);
        Slot->set_equipped(Record->bEquipped);
        if (Record->HardpointSize > 0) {
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
            Slot->set_hardpoint(Hardpoint);
        } else {
            Slot->make_internal();
        }
    }
}

// RC 06b4f860 (shared by 0x24/0x25 through vtable 06b6e0f4): the handle's contents,
// then the 4-byte handle itself.
FRpcServerEquipListPacketBase::~FRpcServerEquipListPacketBase() {
    delete Equipment;
}

// RC 06b3c510 / 06b3c770 (identical bodies). A failed handle allocation leaves the
// packet without one.
void FRpcServerEquipListPacketBase::AssignEquipment(const FRpcServerEquipHandle& Source) {
    if (Equipment == nullptr) {
        Equipment = new (std::nothrow) FRpcServerEquipHandle();
    }
    if (Equipment != nullptr) {
        Equipment->AppendCopy(Source);
    }
}

// RS 06bdc770 / 06bdce60 (identical bodies).
void FRpcServerEquipListPacketBase::ExtractEquipment(FRpcServerEquipHandle* Dest) const {
    if (Equipment != nullptr) {
        Dest->AppendCopy(*Equipment);
    }
}

// RC 06b3c370 / 06b3c5d0, RS 06bdc590 / 06bdcc80.
unsigned int FRpcServerEquipListPacketBase::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // A nonzero helper result is returned unchanged without touching Error.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned int Result = EncodeEquipment(Stream);
    if (Result != 0) return Result;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b3c440 / 06b3c6a0, RS 06bdc660 / 06bdcd50.
unsigned int FRpcServerEquipListPacketBase::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    const unsigned int Result = DecodeEquipment(Stream);
    if (Result != 0) return Result;
    return Stream.Finish(OutLength, false);
}

// RC 06b4f8d0 / RS 06becfb0. Wire: u16 count (list size truncated), then per record in list
// order: Count, Health (x87), ArchId, u16 Id, byte bEquipped, byte bMission, u16
// HardpointSize, HardpointSize raw bytes. The walk covers the whole list, not the truncated
// count. A failed reservation returns 0x16 without rewriting the sticky Error.
unsigned int FRpcServerEquipListPacketBase::EncodeEquipment(FRpcWireStream& Stream) {
    const std::list<FRpcServerEquipRecord*>* CountList =
        (Equipment != nullptr) ? Equipment->Records : nullptr;
    const unsigned short Count =
        (CountList != nullptr) ? static_cast<unsigned short>(CountList->size()) : static_cast<unsigned short>(0);
    if (!Stream.Put(&Count, 2u)) return kPacketEncodeOverflow;
    if (Count == 0) return 0;
    // The list is fetched again after the count write; a null holder ends the encode
    // successfully.
    const std::list<FRpcServerEquipRecord*>* List =
        (Equipment != nullptr) ? Equipment->Records : nullptr;
    if (List == nullptr) return 0;
    for (std::list<FRpcServerEquipRecord*>::const_iterator It = List->begin();
        It != List->end(); ++It) {
        const FRpcServerEquipRecord* Record = *It;
        if (!Stream.Put(&Record->Count, 4u)) return kPacketEncodeOverflow;
        if (!Stream.PutFloat(Record->Health)) return kPacketEncodeOverflow;
        if (!Stream.Put(&Record->ArchId, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&Record->Id, 2u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&Record->bEquipped, 1u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&Record->bMission, 1u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&Record->HardpointSize, 2u)) return kPacketEncodeOverflow;
        if (!Stream.PutBlock(Record->Hardpoint, Record->HardpointSize)) return kPacketEncodeOverflow;
    }
    return 0;
}

// RC 06b4fb10 / RS 06bed1f0. The decoded records are appended to whatever handle the packet
// already has. A record whose allocation fails is skipped; a field failure returns without
// freeing the half-read record (reproduced on purpose). Hardpoint body: RC bounds-checks it
// before the cursor moves and returns 5 on a failed allocation; RS allocates first, with no
// prior check and no null test.
unsigned int FRpcServerEquipListPacketBase::DecodeEquipment(FRpcWireStream& Stream) {
    unsigned short Count;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    if (Count == 0) return 0;
    if (Equipment == nullptr) {
        Equipment = new (std::nothrow) FRpcServerEquipHandle();
    }
    for (unsigned short Index = 0; Index < Count; ++Index) {
        FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
            ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
        if (Record == nullptr) continue;
        Record->ArchId = 0;
        Record->Count = 0;
        std::memset(&Record->Health, 0, 4u);
        Record->Id = 0;
        Record->bEquipped = 0;
        Record->bMission = 0;
        Record->HardpointSize = 0;
        Record->Hardpoint = nullptr;
        if (!Stream.Get(&Record->Count, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Health, 4u)) return MarshalError;
        if (!Stream.Get(&Record->ArchId, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Id, 2u)) return MarshalError;
        if (!Stream.Get(&Record->bEquipped, 1u)) return MarshalError;
        if (!Stream.Get(&Record->bMission, 1u)) return MarshalError;
        if (!Stream.Get(&Record->HardpointSize, 2u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + Record->HardpointSize > MarshalLimit) MarshalError = kPacketDecodeOverrun;
        if (MarshalError != 0) return MarshalError;
        Record->Hardpoint = static_cast<char*>(::operator new(Record->HardpointSize, std::nothrow));
        if (Record->Hardpoint == nullptr) return kPacketNoMemory;
#else
        Record->Hardpoint = static_cast<char*>(::operator new(Record->HardpointSize, std::nothrow));
#endif
        if (!Stream.Get(Record->Hardpoint, Record->HardpointSize)) return MarshalError;
        Equipment->PushBack(Record);
    }
    return 0;
}

// --- 0x27 REQUESTCREATESHIPRESP: RC 06b5ba90 / 06b5bbb0, RS 06beef60 / 06bef080 ---
unsigned int FRpcServerRequestCreateShipRespPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&bSuccess, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ShipId, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcServerRequestCreateShipRespPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&bSuccess, 1u)) return MarshalError;
    if (!Stream.Get(&ShipId, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x2a ADJUSTATTITUDE: RC 06b5e230 / 06b5e350, RS 06bf21b0 / 06bf22d0 ---
// The float goes through x87 on encode and an integer move on decode.
unsigned int FRpcServerAdjustAttitudePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&VibeHandle, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(Attitude)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcServerAdjustAttitudePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&VibeHandle, 4u)) return MarshalError;
    if (!Stream.Get(&Attitude, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x2d CREATECOUNTER ---
static_assert(sizeof(FRpcServerCreateCounterPacket) == 0x68, "CREATECOUNTER is operator new(0x68)");

// RC 06b4aab0 / RS 06be55d0.
FRpcServerCreateCounterPacket::FRpcServerCreateCounterPacket() : FPacketBase(FLPACKET_SERVER_CREATECOUNTER) {
    ArchetypeNickname = 0;
    ObjId = 0;
    OwnerPlayer = 0;
    OwnerId = 0;
    SubObjId = SUBOBJ_ID_NONE;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Velocity.x = 0.0f;
    Velocity.y = 0.0f;
    Velocity.z = 0.0f;
    AngularVelocity.x = 0.0f;
    AngularVelocity.y = 0.0f;
    AngularVelocity.z = 0.0f;
}

// RC 06b5dcc0 / RS 06bf1c40. The quaternion and the three vectors are integer copies
// (06b39ec0 / 06b3a160).
unsigned int FRpcServerCreateCounterPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ArchetypeNickname, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ObjId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&OwnerPlayer, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&OwnerId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SubObjId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Orientation, 16u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Position, 12u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Velocity, 12u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&AngularVelocity, 12u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5dfb0 / RS 06bf1f30.
unsigned int FRpcServerCreateCounterPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ArchetypeNickname, 4u)) return MarshalError;
    if (!Stream.Get(&ObjId, 4u)) return MarshalError;
    if (!Stream.Get(&OwnerPlayer, 4u)) return MarshalError;
    if (!Stream.Get(&OwnerId, 4u)) return MarshalError;
    if (!Stream.Get(&SubObjId, 2u)) return MarshalError;
    if (!Stream.Get(&Orientation, 16u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    if (!Stream.Get(&Velocity, 12u)) return MarshalError;
    if (!Stream.Get(&AngularVelocity, 12u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x37 CREATEGUIDED ---
static_assert(sizeof(FRpcServerCreateGuidedPacket) == 0x58, "CREATEGUIDED is operator new(0x58)");

// RC 06b4a460 / RS 06be4f60: only Position and Orientation are written.
FRpcServerCreateGuidedPacket::FRpcServerCreateGuidedPacket() : FPacketBase(FLPACKET_SERVER_CREATEGUIDED) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

// RC 06b5c310 / RS 06bef7e0. The small id is computed before its reservation; all four
// orientation bytes are quantized, x, y, z, w, after the Position commit and before the first of
// their one-byte reservations.
unsigned int FRpcServerCreateGuidedPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned short SmallId = Archetype::LargeIDToSmallID(ArchetypeNickname);
    if (!Stream.Put(&SmallId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&OwnerId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SubObjId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TargetId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TargetSubObjId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ObjId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Position, 12u)) return kPacketEncodeOverflow;
    const unsigned char X = RpcServerQuantize127(&Orientation.x);
    const unsigned char Y = RpcServerQuantize127(&Orientation.y);
    const unsigned char Z = RpcServerQuantize127(&Orientation.z);
    const unsigned char W = RpcServerQuantize127(&Orientation.w);
    if (!Stream.Put(&X, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Y, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Z, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&W, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5c640 / RS 06befb10. OwnerPlayer is zeroed before the first read (it is not sent).
// The four bytes are all read before any is expanded into x, y, z, w.
unsigned int FRpcServerCreateGuidedPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    OwnerPlayer = 0;
    unsigned short SmallId;
    if (!Stream.Get(&SmallId, 2u)) return MarshalError;
    ArchetypeNickname = Archetype::SmallIDToLargeID(SmallId);
    if (!Stream.Get(&OwnerId, 4u)) return MarshalError;
    if (!Stream.Get(&SubObjId, 2u)) return MarshalError;
    if (!Stream.Get(&TargetId, 4u)) return MarshalError;
    if (!Stream.Get(&TargetSubObjId, 2u)) return MarshalError;
    if (!Stream.Get(&ObjId, 4u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    unsigned char X;
    unsigned char Y;
    unsigned char Z;
    unsigned char W;
    if (!Stream.Get(&X, 1u)) return MarshalError;
    if (!Stream.Get(&Y, 1u)) return MarshalError;
    if (!Stream.Get(&Z, 1u)) return MarshalError;
    if (!Stream.Get(&W, 1u)) return MarshalError;
    RpcServerExpand127(X, &Orientation.x);
    RpcServerExpand127(Y, &Orientation.y);
    RpcServerExpand127(Z, &Orientation.z);
    RpcServerExpand127(W, &Orientation.w);
    return Stream.Finish(OutLength, false);
}

// --- 0x22 SYSTEM_SWITCH_IN ---
static_assert(sizeof(FRpcServerSystemSwitchInPacket) == 0x44, "SYSTEM_SWITCH_IN is operator new(0x44)");

// RC 06b4a790 / RS 06be5290.
FRpcServerSystemSwitchInPacket::FRpcServerSystemSwitchInPacket() : FPacketBase(FLPACKET_SERVER_SYSTEM_SWITCH_IN) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

// RC 06b5d3d0 / RS 06bf08a0. Vector/quaternion are integer copies (06b3a160 / 06b39ec0).
unsigned int FRpcServerSystemSwitchInPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ShipId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&JumpObjTypeBits, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Position, 12u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Orientation, 16u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5d5a0 / RS 06bf0a70.
unsigned int FRpcServerSystemSwitchInPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ShipId, 4u)) return MarshalError;
    if (!Stream.Get(&JumpObjTypeBits, 4u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    if (!Stream.Get(&Orientation, 16u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x31 EXPLODEASTEROIDMINE ---
static_assert(sizeof(FRpcServerExplodeAsteroidMinePacket) == 0x30,
    "EXPLODEASTEROIDMINE is operator new(0x30)");

// RC 06b4ab90 / RS 06be56b0: +0x20..+0x2c = 0 as dwords.
FRpcServerExplodeAsteroidMinePacket::FRpcServerExplodeAsteroidMinePacket()
    : FPacketBase(FLPACKET_SERVER_EXPLODEASTEROIDMINE) {
    MineId = 0;
    std::memset(&Position, 0, sizeof(Position));
}

// RC 06b42c90 / RS 06bdb9a0: Position is an integer copy (06b3a160).
unsigned int FRpcServerExplodeAsteroidMinePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&MineId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Position, 12u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b42de0 / RS 06bdbaf0.
unsigned int FRpcServerExplodeAsteroidMinePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&MineId, 4u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x38 ITEMTRACTORED ---
static_assert(sizeof(FRpcServerItemTractoredPacket) == 0x2c, "ITEMTRACTORED is operator new(0x2c)");

// RC 06b4a510 / RS 06be5010.
FRpcServerItemTractoredPacket::FRpcServerItemTractoredPacket() : FPacketBase(FLPACKET_SERVER_ITEMTRACTORED) {
    LootId = 0;
    ShipId = 0;
    TractorEquipId = 0;
    UnitsTaken = 0;
}

// RC 06b5c980 / RS 06befe50.
unsigned int FRpcServerItemTractoredPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&LootId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ShipId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TractorEquipId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&UnitsTaken, 2u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5cb00 / RS 06beffd0.
unsigned int FRpcServerItemTractoredPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&LootId, 4u)) return MarshalError;
    if (!Stream.Get(&ShipId, 4u)) return MarshalError;
    if (!Stream.Get(&TractorEquipId, 2u)) return MarshalError;
    if (!Stream.Get(&UnitsTaken, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x2e SETADDITEM ---
static_assert(sizeof(FRpcServerSetAddItemPacket) == 0x40, "SETADDITEM is operator new(0x40)");

// RC 06b49e60 / RS 06be4920.
FRpcServerSetAddItemPacket::FRpcServerSetAddItemPacket() : FPacketBase(FLPACKET_SERVER_SETADDITEM) {
    HardpointSize = 0;
    bMission = 0;
    Hardpoint = nullptr;
}

// RC 06b3c800 / RS 06bee710.
FRpcServerSetAddItemPacket::~FRpcServerSetAddItemPacket() {
    if (Hardpoint != nullptr) ::operator delete(Hardpoint);
}

// RC 06b3cd50.
void FRpcServerSetAddItemPacket::SetHardpoint(const char* Source, unsigned int Length) {
    if (Length == 0 || Source == nullptr) return;
    const unsigned int Size = Length + 1u;
    Hardpoint = static_cast<char*>(::operator new(Size, std::nothrow));
    std::memcpy(Hardpoint, Source, Size);
    HardpointSize = Size;
}

// RC 06b3c850 / RS 06bee740. Health goes through x87.
unsigned int FRpcServerSetAddItemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ArchId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Id, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Count, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(Health)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bEquipped, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bMission, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&AddResult, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&HardpointSize, 4u)) return kPacketEncodeOverflow;
    if (HardpointSize != 0) {
        if (!Stream.PutBlock(Hardpoint, HardpointSize)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b3cad0 / RS 06bee9c0. Health is an integer move here. The hardpoint block: RC checks
// cursor + size WITHOUT committing (0x17) and returns 5 on a failed allocation; RS allocates
// with no prior check and no null test.
unsigned int FRpcServerSetAddItemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ArchId, 4u)) return MarshalError;
    if (!Stream.Get(&Id, 4u)) return MarshalError;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    if (!Stream.Get(&Health, 4u)) return MarshalError;
    if (!Stream.Get(&bEquipped, 1u)) return MarshalError;
    if (!Stream.Get(&bMission, 1u)) return MarshalError;
    if (!Stream.Get(&AddResult, 4u)) return MarshalError;
    if (!Stream.Get(&HardpointSize, 4u)) return MarshalError;
    if (HardpointSize != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + HardpointSize > MarshalLimit) MarshalError = kPacketDecodeOverrun;
        if (MarshalError != 0) return MarshalError;
        Hardpoint = static_cast<char*>(::operator new(HardpointSize, std::nothrow));
        if (Hardpoint == nullptr) return kPacketNoMemory;
#else
        // No null test, reproduced on purpose: a failed allocation faults in the copy.
        Hardpoint = static_cast<char*>(::operator new(HardpointSize, std::nothrow));
#endif
        if (!Stream.Get(Hardpoint, HardpointSize)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

// --- 0x32 REQUESTSPACESCRIPT ---
static_assert(sizeof(FRpcServerRequestSpaceScriptPacket) == 0x3c,
    "REQUESTSPACESCRIPT is operator new(0x3c)");

// RC 06b4b990 / RS 06be64b0.
FRpcServerRequestSpaceScriptPacket::FRpcServerRequestSpaceScriptPacket()
    : FPacketBase(FLPACKET_SERVER_REQUESTSPACESCRIPT) {
    ObjectId = 0;
    Position[0] = 0;
    Position[1] = 0;
    Position[2] = 0;
    Mode = 1;
    Name = nullptr;
}

// RC 06b43ed0.
FRpcServerRequestSpaceScriptPacket::~FRpcServerRequestSpaceScriptPacket() {
    if (Name != nullptr) ::operator delete(Name);
}

// RC 06b44240.
void FRpcServerRequestSpaceScriptPacket::SetName(const char* Source, unsigned int Length) {
    if (Length == 0 || Source == nullptr) return;
    if (Name != nullptr) ::operator delete(Name);
    const unsigned int Size = Length + 1u;
    Name = static_cast<char*>(::operator new(Size, std::nothrow));
    std::memcpy(Name, Source, Size);
    NameSize = Size;
}

// RC 06b43f20 / RS 06bf4780.
unsigned int FRpcServerRequestSpaceScriptPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&NameSize, 4u)) return kPacketEncodeOverflow;
    if (NameSize != 0) {
        if (!Stream.PutBlock(Name, NameSize)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b44090: uncommitted check of the name block (0x17), free the old Name, allocate
// (null -> 5). RS 06bf48f0: copies into Name with no allocation at all.
unsigned int FRpcServerRequestSpaceScriptPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&NameSize, 4u)) return MarshalError;
    if (NameSize != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + NameSize > MarshalLimit) MarshalError = kPacketDecodeOverrun;
        if (MarshalError != 0) return MarshalError;
        if (Name != nullptr) ::operator delete(Name);
        Name = static_cast<char*>(::operator new(NameSize, std::nothrow));
        if (Name == nullptr) return kPacketNoMemory;
#endif
        // RS: Name is still null here; the copy writes through it, reproduced on purpose.
        if (!Stream.Get(Name, NameSize)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

// --- 0x33 SETMISSIONOBJECTIVESTATE ---
static_assert(sizeof(FRpcServerSetMissionObjectiveStatePacket) == 0x2c,
    "SETMISSIONOBJECTIVESTATE is operator new(0x2c)");

// RC 06b5ebc0 / RS 06bf3410.
unsigned int FRpcServerSetMissionObjectiveStatePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&MissionId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ObjectiveIndex, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&State, 2u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5ed10 / RS 06bf3560.
unsigned int FRpcServerSetMissionObjectiveStatePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&MissionId, 4u)) return MarshalError;
    if (!Stream.Get(&ObjectiveIndex, 4u)) return MarshalError;
    if (!Stream.Get(&State, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x34 REPLACEMISSIONOBJECTIVE ---
static_assert(sizeof(FRpcServerReplaceMissionObjectivePacket) == 0x30,
    "REPLACEMISSIONOBJECTIVE is operator new(0x30)");

// RC 06b4b5c0 / RS 06be60e0.
FRpcServerReplaceMissionObjectivePacket::~FRpcServerReplaceMissionObjectivePacket() {
    if (TextData != nullptr) ::operator delete(TextData);
}

// RC 06b5f320 / RS 06bf3b70: the block goes out even when TextSize is 0.
unsigned int FRpcServerReplaceMissionObjectivePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&MissionId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ObjectiveIndex, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TextSize, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(TextData, TextSize)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5f4a0 / RS 06bf3cf0. The block helper's status is discarded and 0 is stored over
// MarshalError: an overrunning block still decodes as success. Reproduced on purpose.
unsigned int FRpcServerReplaceMissionObjectivePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&MissionId, 4u)) return MarshalError;
    if (!Stream.Get(&ObjectiveIndex, 4u)) return MarshalError;
    RpcWireDecodeOwnedBlock(*this, Stream, TextSize, TextData);
    MarshalError = 0;
    return Stream.Finish(OutLength, false);
}

// --- 0x35 SETMISSIONOBJECTIVES ---

// RC 06b4b8c0 / RS 06be63e0.
FRpcServerSetMissionObjectivesPacket::FRpcServerSetMissionObjectivesPacket()
    : FPacketBase(FLPACKET_SERVER_SETMISSIONOBJECTIVES) {
    Count = 0;
}

// RC 06b44d20 / RS 06bdaad0: the records here; TextB and TextA free themselves.
FRpcServerSetMissionObjectivesPacket::~FRpcServerSetMissionObjectivesPacket() {
    for (std::list<FRpcServerMissionObjectiveRecord*>::iterator It = Objectives.begin();
         It != Objectives.end(); ++It) {
        FRpcServerMissionObjectiveRecord* Record = *It;
        if (Record != nullptr) {
            if (Record->Data != nullptr) ::operator delete(Record->Data);
            ::operator delete(Record);
        }
    }
    Objectives.clear();
}

// RC 06b44e20.
void FRpcServerSetMissionObjectivesPacket::PushObjective(FRpcServerMissionObjectiveRecord* Record) {
    if (Record == nullptr) return;
    Objectives.push_back(Record);
    ++Count;
}

// RS 06bdac30.
FRpcServerMissionObjectiveRecord* FRpcServerSetMissionObjectivesPacket::ObjectiveAt(
    unsigned int Index) const {
    unsigned int At = 0;
    for (std::list<FRpcServerMissionObjectiveRecord*>::const_iterator It = Objectives.begin();
         It != Objectives.end(); ++It, ++At) {
        if (At == Index) return *It;
    }
    return nullptr;
}

// RC 06b44ea0 / RS 06bdac60. The records go out only when Count is non-zero; the loop
// then runs to the end of the list, not to Count, and reads each record without a
// null test.
unsigned int FRpcServerSetMissionObjectivesPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&MissionId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Count, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&MissionType, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TextA.Size, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(TextA.Data, TextA.Size)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TextB.Size, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(TextB.Data, TextB.Size)) return kPacketEncodeOverflow;
    if (Count != 0) {
        for (std::list<FRpcServerMissionObjectiveRecord*>::const_iterator It = Objectives.begin();
             It != Objectives.end(); ++It) {
            const FRpcServerMissionObjectiveRecord* Record = *It;
            if (!Stream.Put(&Record->State, 1u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->Size, 4u)) return kPacketEncodeOverflow;
            if (!Stream.PutBlock(Record->Data, Record->Size)) return kPacketEncodeOverflow;
        }
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b451e0 / RS 06bdafa0. RC returns 0x17 when either text block helper fails; RS ignores
// both results. A null record is written through, and a failed record block returns 0x17 and
// leaks the record, as retail; Count is not touched. 0 is stored over MarshalError at the end.
unsigned int FRpcServerSetMissionObjectivesPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&MissionId, 4u)) return MarshalError;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    if (!Stream.Get(&MissionType, 4u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    if (RpcWireDecodeOwnedBlock(*this, Stream, TextA.Size, TextA.Data) != 0) return kPacketDecodeOverrun;
    if (RpcWireDecodeOwnedBlock(*this, Stream, TextB.Size, TextB.Data) != 0) return kPacketDecodeOverrun;
#else
    RpcWireDecodeOwnedBlock(*this, Stream, TextA.Size, TextA.Data);
    RpcWireDecodeOwnedBlock(*this, Stream, TextB.Size, TextB.Data);
#endif
    for (unsigned int Index = 0; Index < Count; ++Index) {
        FRpcServerMissionObjectiveRecord* Record = static_cast<FRpcServerMissionObjectiveRecord*>(
            ::operator new(sizeof(FRpcServerMissionObjectiveRecord), std::nothrow));
        if (Record != nullptr) {
            Record->Size = 0;
            Record->Data = nullptr;
        }
        if (!Stream.Get(&Record->State, 1u)) return MarshalError;
        if (RpcWireDecodeOwnedBlock(*this, Stream, Record->Size, Record->Data) != 0) return kPacketDecodeOverrun;
        Objectives.push_back(Record);
    }
    MarshalError = 0;
    return Stream.Finish(OutLength, false);
}

// --- 0x3c REPAIROBJECT ---
static_assert(sizeof(FRpcServerRepairObjectPacket) == 0x2c, "REPAIROBJECT is operator new(0x2c)");

// RC 06b4c4d0 / RS 06be70d0.
FRpcServerRepairObjectPacket::FRpcServerRepairObjectPacket() : FPacketBase(FLPACKET_SERVER_REPAIROBJECT) {
    ObjectId = 0;
    EquipId = 0;
    Points = 0;
}

// RC 06b61970 / RS 06bf7b50.
unsigned int FRpcServerRepairObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&EquipId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Points, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b61ac0 / RS 06bf7ca0.
unsigned int FRpcServerRepairObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&EquipId, 4u)) return MarshalError;
    if (!Stream.Get(&Points, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// RC 06b61bf0 / RS 06bf7dd0: operator<<(unsigned) for the two ids, operator<<(int) for Points.
void FRpcServerRepairObjectPacket::Dump(std::ostream& Out) const {
    Out << "   Object ID  : " << ObjectId << "\n";
    Out << "   Equip ID   : " << EquipId << "\n";
    Out << "   Points     : " << Points << "\n";
}

// --- 0x28 CREATELOOT ---

// RC 06b44b40 (list node 0x50 bytes).
void FRpcServerCreateLootPacket::AddLoot(const FRpcServerLootRecord& Record) {
    Loot.push_back(Record);
}

// RC 06b442a0 / RS 06bddfd0. The list size goes out as a byte; when that byte is zero no
// record is written, otherwise the walk covers the whole list. Each small id is computed
// before its reservation. Velocity and Orientation travel only in single player.
unsigned int FRpcServerCreateLootPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&OwnerId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Kind, 1u)) return kPacketEncodeOverflow;
    const unsigned char Count = static_cast<unsigned char>(Loot.size());
    if (!Stream.Put(&Count, 1u)) return kPacketEncodeOverflow;
    if (Count != 0) {
        for (std::list<FRpcServerLootRecord>::const_iterator It = Loot.begin();
            It != Loot.end(); ++It) {
            const FRpcServerLootRecord& Record = *It;
            if (!Stream.Put(&Record.LootId, 4u)) return kPacketEncodeOverflow;
            const unsigned short ContentsId = Archetype::LargeIDToSmallID(Record.ContentsNickname);
            if (!Stream.Put(&ContentsId, 2u)) return kPacketEncodeOverflow;
            if (!Stream.PutFloat(Record.ContentsHitPts)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record.Units, 2u)) return kPacketEncodeOverflow;
            const unsigned short ContainerId = Archetype::LargeIDToSmallID(Record.ContainerNickname);
            if (!Stream.Put(&ContainerId, 2u)) return kPacketEncodeOverflow;
            if (!Stream.PutFloat(Record.HitPts)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record.NameOverride, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record.Position, 12u)) return kPacketEncodeOverflow;
            if (SinglePlayer()) {
                if (!Stream.Put(&Record.Velocity, 12u)) return kPacketEncodeOverflow;
                if (!Stream.Put(&Record.Orientation, 16u)) return kPacketEncodeOverflow;
            }
            if (!Stream.Put(&Record.bMarkedBy, 1u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record.bTemporary, 1u)) return kPacketEncodeOverflow;
        }
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b44700 / RS 06bde430. Per record a zeroed record (06b44ba0) is appended first, then the
// fields are read straight into the node, so a field failure leaves it in the list. Floats are
// integer copies.
unsigned int FRpcServerCreateLootPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&OwnerId, 4u)) return MarshalError;
    if (!Stream.Get(&Kind, 1u)) return MarshalError;
    unsigned char Count;
    if (!Stream.Get(&Count, 1u)) return MarshalError;
    for (unsigned char Index = 0; Index < Count; ++Index) {
        FRpcServerLootRecord Fresh;
        Fresh.LootId = 0;
        Fresh.ContentsNickname = 0;
        std::memset(&Fresh.ContentsHitPts, 0, 4u);
        Fresh.Units = 0;
        Fresh.Pad0e = 0;
        Fresh.ContainerNickname = 0;
        std::memset(&Fresh.HitPts, 0, 4u);
        Fresh.NameOverride = 0;
        std::memset(&Fresh.Position, 0, sizeof(Fresh.Position));
        std::memset(&Fresh.Velocity, 0, sizeof(Fresh.Velocity));
        Fresh.Orientation.w = 1.0f;
        Fresh.Orientation.x = 0.0f;
        Fresh.Orientation.y = 0.0f;
        Fresh.Orientation.z = 0.0f;
        Fresh.bMarkedBy = 0;
        Fresh.bTemporary = 0;
        Fresh.Pad46[0] = 0;
        Fresh.Pad46[1] = 0;
        Loot.push_back(Fresh);
        FRpcServerLootRecord& Record = Loot.back();
        if (!Stream.Get(&Record.LootId, 4u)) return MarshalError;
        unsigned short ContentsId;
        if (!Stream.Get(&ContentsId, 2u)) return MarshalError;
        Record.ContentsNickname = Archetype::SmallIDToLargeID(ContentsId);
        if (!Stream.Get(&Record.ContentsHitPts, 4u)) return MarshalError;
        if (!Stream.Get(&Record.Units, 2u)) return MarshalError;
        unsigned short ContainerId;
        if (!Stream.Get(&ContainerId, 2u)) return MarshalError;
        Record.ContainerNickname = Archetype::SmallIDToLargeID(ContainerId);
        if (!Stream.Get(&Record.HitPts, 4u)) return MarshalError;
        if (!Stream.Get(&Record.NameOverride, 4u)) return MarshalError;
        if (!Stream.Get(&Record.Position, 12u)) return MarshalError;
        if (SinglePlayer()) {
            if (!Stream.Get(&Record.Velocity, 12u)) return MarshalError;
            if (!Stream.Get(&Record.Orientation, 16u)) return MarshalError;
        }
        if (!Stream.Get(&Record.bMarkedBy, 1u)) return MarshalError;
        if (!Stream.Get(&Record.bTemporary, 1u)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

// --- relation lists (0x29, 0x2b) ---

// RC 06b3f6c0 / 06b43da0, RS 06bddef0 / 06bdd430.
void RpcServerRelationListAppendCopy(std::list< ::Reputation::Relation*>& Dest,
    const std::list< ::Reputation::Relation*>& Source) {
    for (std::list< ::Reputation::Relation*>::const_iterator It = Source.begin();
        It != Source.end(); ++It) {
        const ::Reputation::Relation* From = *It;
        if (From == nullptr) continue;
        ::Reputation::Relation* Copy = static_cast< ::Reputation::Relation*>(
            ::operator new(sizeof(::Reputation::Relation), std::nothrow));
        std::memcpy(&Copy->Feeling, &From->Feeling, 4u);
        Copy->GroupId = From->GroupId;
        Dest.push_back(Copy);
    }
}

// --- 0x29 SETREPUTATION ---

// RC 06b4af30 / RS 06be5a50.
FRpcServerSetReputationPacket::FRpcServerSetReputationPacket() : FPacketBase(FLPACKET_SERVER_SETREPUTATION) {
    Attitude = 0.0f;
    MaskAffiliation = kSetReputationMaskAffiliation;
    MaskNames = kSetReputationMaskNames;
    MaskDisplayName = kSetReputationMaskDisplayName;
    MaskRelationsByte = kSetReputationMaskRelationsByte;
    MaskRelationsWord = kSetReputationMaskRelationsWord;
    Flags = 0;
    Target = 0;
    Affiliation = kNoAffiliation;
    DisplayNameLength = 0;
    DisplayName = nullptr;
}

// RC 06b3ed00 / RS 06bdd4e0; NameB and NameA free themselves.
FRpcServerSetReputationPacket::~FRpcServerSetReputationPacket() {
    for (std::list< ::Reputation::Relation*>::iterator It = Relations.begin();
        It != Relations.end(); ++It) {
        if (*It != nullptr) ::operator delete(*It);
    }
    if (DisplayName != nullptr) ::operator delete(DisplayName);
    Relations.clear();
}

// RC 06b3f670.
void FRpcServerSetReputationPacket::SetDisplayName(const wchar_t* Source, unsigned int Length) {
    if (Source == nullptr || Length == 0) return;
    const unsigned int Count = Length + 1u;
    DisplayName = static_cast<wchar_t*>(::operator new(Count * 4u, std::nothrow));
    std::memcpy(DisplayName, Source, Count * 2u);
    DisplayNameLength = static_cast<int>(Count);
}

// RC 06b3ee10 / RS 06bdd5e0. Flags is rebuilt on every encode: Affiliation != -1 sets
// the low byte of MaskAffiliation; in single player a non-zero DisplayNameLength sets
// MaskDisplayName, a non-zero first dword of either flattened block sets MaskNames (both
// blocks are read without a null test, reproduced on purpose), and a non-zero list size
// sets MaskRelationsWord when it is above 0xff (signed compare), MaskRelationsByte
// otherwise. The relation count is the list size only in single player (0 otherwise).
// The presence tests use the low byte of each mask.
unsigned int FRpcServerSetReputationPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    unsigned int RelationCount = 0;
    Flags = 0;
    if (Affiliation != kNoAffiliation) {
        Flags = static_cast<unsigned char>(MaskAffiliation);
    }
    if (SinglePlayer()) {
        if (DisplayNameLength != 0) {
            Flags = static_cast<unsigned char>(Flags | static_cast<unsigned char>(MaskDisplayName));
        }
        unsigned int FirstA;
        unsigned int FirstB;
        std::memcpy(&FirstA, NameA.Data, 4u);
        bool bNames = FirstA != 0;
        if (!bNames) {
            std::memcpy(&FirstB, NameB.Data, 4u);
            bNames = FirstB != 0;
        }
        if (bNames) {
            Flags = static_cast<unsigned char>(Flags | static_cast<unsigned char>(MaskNames));
        }
        RelationCount = static_cast<unsigned int>(Relations.size());
        if (RelationCount != 0) {
            if (static_cast<int>(RelationCount) > 0xff) {
                Flags = static_cast<unsigned char>(Flags | static_cast<unsigned char>(MaskRelationsWord));
            } else {
                Flags = static_cast<unsigned char>(Flags | static_cast<unsigned char>(MaskRelationsByte));
            }
        }
    }
    if (!Stream.Put(&Flags, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Target, 4u)) return kPacketEncodeOverflow;
    if ((static_cast<unsigned char>(MaskAffiliation) & Flags) != 0) {
        if (!Stream.Put(&Affiliation, 4u)) return kPacketEncodeOverflow;
    }
    if ((static_cast<unsigned char>(MaskDisplayName) & Flags) != 0) {
        const unsigned char Length = static_cast<unsigned char>(DisplayNameLength);
        if (!Stream.Put(&Length, 1u)) return kPacketEncodeOverflow;
        if (!Stream.PutBlock(DisplayName, static_cast<unsigned int>(DisplayNameLength) << 1)) {
            return kPacketEncodeOverflow;
        }
    }
    if ((static_cast<unsigned char>(MaskNames) & Flags) != 0) {
        if (!Stream.Put(&NameA.Size, 4u)) return kPacketEncodeOverflow;
        if (!Stream.PutBlock(NameA.Data, NameA.Size)) return kPacketEncodeOverflow;
        if (!Stream.Put(&NameB.Size, 4u)) return kPacketEncodeOverflow;
        if (!Stream.PutBlock(NameB.Data, NameB.Size)) return kPacketEncodeOverflow;
    }
    if (IsMPServer()) {
        if (!Stream.PutFloat(Attitude)) return kPacketEncodeOverflow;
    } else if (RelationCount != 0) {
        if ((static_cast<unsigned char>(MaskRelationsWord) & Flags) != 0) {
            const unsigned short Count = static_cast<unsigned short>(RelationCount);
            if (!Stream.Put(&Count, 2u)) return kPacketEncodeOverflow;
        } else {
            const unsigned char Count = static_cast<unsigned char>(RelationCount);
            if (!Stream.Put(&Count, 1u)) return kPacketEncodeOverflow;
        }
        for (std::list< ::Reputation::Relation*>::const_iterator It = Relations.begin();
            It != Relations.end(); ++It) {
            const ::Reputation::Relation* Relation = *It;
            if (!Stream.PutFloat(Relation->Feeling)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Relation->GroupId, 4u)) return kPacketEncodeOverflow;
        }
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b3f290 / RS 06bdda60. Affiliation is -1 when its bit is clear. In single player: the
// display name (signed byte length; RC checks the room before allocating, RS allocates first,
// no null test), the two FmtStr blocks (results discarded), then the relations (allocations
// not null-tested). Otherwise Attitude as an integer copy. 0 is stored over MarshalError at the
// end, so a failed block still decodes as success. Reproduced on purpose.
unsigned int FRpcServerSetReputationPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Flags, 1u)) return MarshalError;
    if (!Stream.Get(&Target, 4u)) return MarshalError;
    if ((static_cast<unsigned char>(MaskAffiliation) & Flags) != 0) {
        if (!Stream.Get(&Affiliation, 4u)) return MarshalError;
    } else {
        Affiliation = kNoAffiliation;
    }
    if (SinglePlayer()) {
        if ((static_cast<unsigned char>(MaskDisplayName) & Flags) != 0) {
            signed char Length;
            if (!Stream.Get(&Length, 1u)) return MarshalError;
            DisplayNameLength = static_cast<int>(Length);
#if defined(REMOTECLIENT_EXPORTS)
            if (MarshalCursor + (static_cast<unsigned int>(DisplayNameLength) << 1) > MarshalLimit) {
                MarshalError = kPacketDecodeOverrun;
            }
            if (MarshalError != 0) return MarshalError;
#endif
            DisplayName = static_cast<wchar_t*>(::operator new(
                static_cast<unsigned int>(DisplayNameLength) * 4u + 4u, std::nothrow));
            if (!Stream.Get(DisplayName, static_cast<unsigned int>(DisplayNameLength) << 1)) {
                return MarshalError;
            }
            DisplayName[DisplayNameLength] = 0;
        } else {
            DisplayNameLength = 0;
        }
        if ((static_cast<unsigned char>(MaskNames) & Flags) != 0) {
            RpcWireDecodeOwnedBlock(*this, Stream, NameA.Size, NameA.Data);
            RpcWireDecodeOwnedBlock(*this, Stream, NameB.Size, NameB.Data);
        }
    }
    if (SinglePlayer()) {
        const unsigned int Present = static_cast<unsigned int>(Flags);
        if ((Present & (MaskRelationsByte | MaskRelationsWord)) != 0) {
            unsigned int Count;
            if ((Present & MaskRelationsWord) != 0) {
                unsigned short Word;
                if (!Stream.Get(&Word, 2u)) return MarshalError;
                Count = Word;
            } else {
                unsigned char Byte;
                if (!Stream.Get(&Byte, 1u)) return MarshalError;
                Count = Byte;
            }
            for (unsigned int Index = 0; Index < Count; ++Index) {
                ::Reputation::Relation* Relation = static_cast< ::Reputation::Relation*>(
                    ::operator new(sizeof(::Reputation::Relation), std::nothrow));
                if (!Stream.Get(&Relation->Feeling, 4u)) return MarshalError;
                if (!Stream.Get(&Relation->GroupId, 4u)) return MarshalError;
                Relations.push_back(Relation);
            }
        }
    } else {
        if (!Stream.Get(&Attitude, 4u)) return MarshalError;
    }
    MarshalError = 0;
    return Stream.Finish(OutLength, false);
}

// RC 06b3f7c0 / RS 06bddf70: both values through operator<<(unsigned).
void FRpcServerSetReputationPacket::Dump(std::ostream& Out) const {
    Out << "   Target     : " << Target << "\n";
    Out << "   Affiliation: " << Affiliation << "\n";
}

// --- 0x2b SETGROUPFEELINGS ---

// RC 06b43870 / RS (06bdcf40's body).
FRpcServerSetGroupFeelingsPacket::~FRpcServerSetGroupFeelingsPacket() {
    for (std::list< ::Reputation::Relation*>::iterator It = Relations.begin();
        It != Relations.end(); ++It) {
        if (*It != nullptr) ::operator delete(*It);
    }
    if (Name != nullptr) ::operator delete(Name);
    Relations.clear();
}

// RC 06b43920.
void FRpcServerSetGroupFeelingsPacket::SetName(const char* Source, unsigned int Size) {
    if (Name != nullptr) ::operator delete(Name);
    Name = static_cast<char*>(::operator new(Size, std::nothrow));
    std::strncpy(Name, Source, Size);
    NameSize = Size;
}

// RC 06b43960 / RS 06bdcfa0. No count is written (see the class comment); the relation
// walk runs only when the low word of the list size is non-zero and skips null entries.
unsigned int FRpcServerSetGroupFeelingsPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&NameSize, 4u)) return kPacketEncodeOverflow;
    if (NameSize > 0) {                                                // unsigned
        if (!Stream.PutBlock(Name, NameSize)) return kPacketEncodeOverflow;
    }
    if (static_cast<unsigned short>(Relations.size()) > 0) {
        for (std::list< ::Reputation::Relation*>::const_iterator It = Relations.begin();
            It != Relations.end(); ++It) {
            const ::Reputation::Relation* Relation = *It;
            if (Relation == nullptr) continue;
            if (!Stream.PutFloat(Relation->Feeling)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Relation->GroupId, 4u)) return kPacketEncodeOverflow;
        }
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b43b10 / RS 06bdd150. The name: RC checks the room first and returns 5 on a failed
// allocation; RS allocates first with no null test. Then a u16 count the encoder never writes
// (reproduced on purpose) and per relation an untested new(8) (a failure leaks it).
unsigned int FRpcServerSetGroupFeelingsPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&NameSize, 4u)) return MarshalError;
    if (NameSize > 0) {
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + NameSize > MarshalLimit) MarshalError = kPacketDecodeOverrun;
        if (MarshalError != 0) return MarshalError;
        Name = static_cast<char*>(::operator new(NameSize, std::nothrow));
        if (Name == nullptr) return kPacketNoMemory;
#else
        Name = static_cast<char*>(::operator new(NameSize, std::nothrow));
#endif
        if (!Stream.Get(Name, NameSize)) return MarshalError;
    }
    unsigned short Count;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    for (unsigned short Index = 0; Index < Count; ++Index) {
        ::Reputation::Relation* Relation = static_cast< ::Reputation::Relation*>(
            ::operator new(sizeof(::Reputation::Relation), std::nothrow));
        if (!Stream.Get(&Relation->Feeling, 4u)) return MarshalError;
        if (!Stream.Get(&Relation->GroupId, 4u)) return MarshalError;
        Relations.push_back(Relation);
    }
    return Stream.Finish(OutLength, false);
}

// --- 0x2c CREATEMINE ---
static_assert(sizeof(FRpcServerCreateMinePacket) == 0x68, "CREATEMINE is operator new(0x68)");

// RC 06b4a9e0 / RS 06be5500.
FRpcServerCreateMinePacket::FRpcServerCreateMinePacket() : FPacketBase(FLPACKET_SERVER_CREATEMINE) {
    ObjId = 0;
    ArchetypeNickname = 0;
    OwnerId = 0;
    SubObjId = 0;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Velocity.x = 0.0f;
    Velocity.y = 0.0f;
    Velocity.z = 0.0f;
    AngularVelocity.x = 0.0f;
    AngularVelocity.y = 0.0f;
    AngularVelocity.z = 0.0f;
    OwnerPlayer = 0;
}

// RC 06b5d740 / RS 06bf16c0. The small id is computed before its reservation; the last three
// blocks (integer copies) go out only in single player.
unsigned int FRpcServerCreateMinePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjId, 4u)) return kPacketEncodeOverflow;
    const unsigned short SmallId = Archetype::LargeIDToSmallID(ArchetypeNickname);
    if (!Stream.Put(&SmallId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&OwnerId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SubObjId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Position, 12u)) return kPacketEncodeOverflow;
    if (SinglePlayer()) {
        if (!Stream.Put(&Orientation, 16u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&Velocity, 12u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&AngularVelocity, 12u)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5da40 / RS 06bf19c0. OwnerPlayer (+0x44) is zeroed right before the end helper, on
// both single-player paths.
unsigned int FRpcServerCreateMinePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjId, 4u)) return MarshalError;
    unsigned short SmallId;
    if (!Stream.Get(&SmallId, 2u)) return MarshalError;
    ArchetypeNickname = Archetype::SmallIDToLargeID(SmallId);
    if (!Stream.Get(&OwnerId, 4u)) return MarshalError;
    if (!Stream.Get(&SubObjId, 2u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    if (SinglePlayer()) {
        if (!Stream.Get(&Orientation, 16u)) return MarshalError;
        if (!Stream.Get(&Velocity, 12u)) return MarshalError;
        if (!Stream.Get(&AngularVelocity, 12u)) return MarshalError;
    }
    OwnerPlayer = 0;
    return Stream.Finish(OutLength, false);
}

// --- 0x3d REMOTEOBJECTCARGOUPDATE ---

// RC 06b454c0 / RS 06bda7f0. The equipment helper's non-zero result is returned as is.
unsigned int FRpcServerRemoteObjectCargoUpdatePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Flag, 1u)) return kPacketEncodeOverflow;
    const unsigned int Result = EncodeEquipment(Stream);
    if (Result != 0) return Result;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b455f0 / RS 06bda920.
unsigned int FRpcServerRemoteObjectCargoUpdatePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&Flag, 1u)) return MarshalError;
    const unsigned int Result = DecodeEquipment(Stream);
    if (Result != 0) return Result;
    return Stream.Finish(OutLength, false);
}

// ===== end SERVER 0x20..0x3f =====

// ===== SERVER 0x40..0x5b =====
// Bodies compiled into both DLLs. Encoder header helper RC 06b4e740 / RS 06bebe50, stats
// RC 06b4ecd0 / RS 06bec3b0. Encode failures return 0x16, decode failures 0x17.

// --- 0x41 BURNFUSE ---
// RC ctor 06b4c750 / RS 06be7350.
FRpcServerBurnFusePacket::FRpcServerBurnFusePacket()
    : FPacketBase(FLPACKET_SERVER_BURNFUSE), SourceId(0), ObjectId(0), FuseId(0), SubObjId(SUBOBJ_ID_NONE),
      AtT(0.0f), Lifetime(-1.0f), bLit(0) {}

// RC 06b62240 / RS 06bf8420. Both floats go through x87.
unsigned int FRpcServerBurnFusePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&SourceId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&FuseId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SubObjId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(AtT)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(Lifetime)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bLit, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b62440 / RS 06bf8620: the floats are integer moves here.
unsigned int FRpcServerBurnFusePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&SourceId, 4u)) return MarshalError;
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&FuseId, 4u)) return MarshalError;
    if (!Stream.Get(&SubObjId, 2u)) return MarshalError;
    if (!Stream.Get(&AtT, 4u)) return MarshalError;
    if (!Stream.Get(&Lifetime, 4u)) return MarshalError;
    if (!Stream.Get(&bLit, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x44 REQUEST_RETURNED ---
// RC 06b5a540 / RS 06bf2a20: 7 body bytes, no padding byte.
unsigned int FRpcServerRequestReturnedPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&RequestType, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Response, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&DockPort, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5a6b0 / RS 06bf2b90.
unsigned int FRpcServerRequestReturnedPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&RequestType, 1u)) return MarshalError;
    if (!Stream.Get(&Response, 1u)) return MarshalError;
    if (!Stream.Get(&DockPort, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x45 SET_MISSION_MESSAGE ---
// RC 06b4b860 / RS 06be6380 (scalar deleting dtors 06b4b840 / 06be6360).
FRpcServerSetMissionMessagePacket::~FRpcServerSetMissionMessagePacket() {
    if (TextData != nullptr) ::operator delete(TextData);
}

// RC 06b5fc90 / RS 06bf44e0: the block goes out even when TextSize is 0.
unsigned int FRpcServerSetMissionMessagePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Type, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TextSize, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(TextData, TextSize)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5fde0 / RS 06bf4630.
unsigned int FRpcServerSetMissionMessagePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Type, 4u)) return MarshalError;
    // The block helper's status is discarded and 0 is stored over MarshalError: an
    // overrunning block still decodes as success. Reproduced on purpose.
    RpcWireDecodeOwnedBlock(*this, Stream, TextSize, TextData);
    MarshalError = 0;
    return Stream.Finish(OutLength, false);
}

// --- 0x46 MARKOBJ ---
// RC 06b5ee40 / RS 06bf3690.
unsigned int FRpcServerMarkObjPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&MarkType, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5ef60 / RS 06bf37b0.
unsigned int FRpcServerMarkObjPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&MarkType, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x47 CFGINTERFACENOTIFICATION ---
// RC 06b5f070 / RS 06bf38c0: the byte travels between the two dwords.
unsigned int FRpcServerCfgInterfaceNotificationPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&NotifType, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bEnable, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Param, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5f1c0 / RS 06bf3a10.
unsigned int FRpcServerCfgInterfaceNotificationPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&NotifType, 4u)) return MarshalError;
    if (!Stream.Get(&bEnable, 1u)) return MarshalError;
    if (!Stream.Get(&Param, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x4a SETGUIDEDTARGET ---
// RC 06b62ee0 / RS 06bf90c0.
unsigned int FRpcServerSetGuidedTargetPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&GuidedId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TargetId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TargetSubObjId, 2u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b63030 / RS 06bf9210.
unsigned int FRpcServerSetGuidedTargetPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&GuidedId, 4u)) return MarshalError;
    if (!Stream.Get(&TargetId, 4u)) return MarshalError;
    if (!Stream.Get(&TargetSubObjId, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x4b SET_CAMERA ---
// RC ctor 06b4b6c0 / RS 06be61e0: +0x20..+0x28 = 0, +0x2c = 1.0f, +0x30..+0x38 = 0.
FRpcServerSetCameraPacket::FRpcServerSetCameraPacket() : FPacketBase(FLPACKET_SERVER_SET_CAMERA) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

// RC 06b5f7b0 / RS 06bf4000: Position and Orientation through the integer copiers 06b3a160 /
// 06b39ec0, the two floats through x87.
unsigned int FRpcServerSetCameraPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Position, 12u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Orientation, 16u)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(Fov)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(Aspect)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5f990 / RS 06bf41e0: all integer copies.
unsigned int FRpcServerSetCameraPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    if (!Stream.Get(&Orientation, 16u)) return MarshalError;
    if (!Stream.Get(&Fov, 4u)) return MarshalError;
    if (!Stream.Get(&Aspect, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x51 USE_ITEM ---
// RC 06b63850 / RS 06bf9a00.
unsigned int FRpcServerUseItemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&EquipId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Count, 2u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b639a0 / RS 06bf9b50.
unsigned int FRpcServerUseItemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&EquipId, 2u)) return MarshalError;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x53 FORMATION_UPDATE ---
// RC ctor 06b4d010 / RS 06be7c30: all four dwords zero.
FRpcServerFormationUpdatePacket::FRpcServerFormationUpdatePacket()
    : FPacketBase(FLPACKET_SERVER_FORMATION_UPDATE), LeaderId(0) {
    Offset.x = 0.0f;
    Offset.y = 0.0f;
    Offset.z = 0.0f;
}

// RC 06b63d60 / RS 06bfa330: Offset through the integer copier 06b3a160.
unsigned int FRpcServerFormationUpdatePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&LeaderId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Offset, 12u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b63eb0 / RS 06bfa480.
unsigned int FRpcServerFormationUpdatePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&LeaderId, 4u)) return MarshalError;
    if (!Stream.Get(&Offset, 12u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x55 OBJECTCARGOUPDATE ---
// RC ctor 06b4d160 / RS 06be7da0.
FRpcServerObjectCargoUpdatePacket::FRpcServerObjectCargoUpdatePacket()
    : FPacketBase(FLPACKET_SERVER_OBJECTCARGOUPDATE), ObjectId(0), ArchId(0), EquipId(0), Count(1), Status(1.0f),
      bMission(0) {}

// RC 06b64040 / RS 06bfb4c0. Status goes through x87.
unsigned int FRpcServerObjectCargoUpdatePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ArchId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&EquipId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Count, 2u)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(Status)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bMission, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b64210 / RS 06bfb690.
unsigned int FRpcServerObjectCargoUpdatePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&ArchId, 4u)) return MarshalError;
    if (!Stream.Get(&EquipId, 2u)) return MarshalError;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    if (!Stream.Get(&Status, 4u)) return MarshalError;
    if (!Stream.Get(&bMission, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x57 SET_MUSIC ---
// RC 06b645b0 / RS 06bfba30. The float goes through x87.
unsigned int FRpcServerSetMusicPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&SpaceMusic, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&DangerMusic, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&BattleMusic, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&OverrideMusic, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bNoLoop, 1u)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(Fade)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b64780 / RS 06bfbc00.
unsigned int FRpcServerSetMusicPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&SpaceMusic, 4u)) return MarshalError;
    if (!Stream.Get(&DangerMusic, 4u)) return MarshalError;
    if (!Stream.Get(&BattleMusic, 4u)) return MarshalError;
    if (!Stream.Get(&OverrideMusic, 4u)) return MarshalError;
    if (!Stream.Get(&bNoLoop, 1u)) return MarshalError;
    if (!Stream.Get(&Fade, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x4d LOADHINT ---
// RC 06b42f00 / RS 06bdbc10.
FRpcServerLoadHintPacket::~FRpcServerLoadHintPacket() {
    if (Data != nullptr) {
        ::operator delete(Data);
        Data = nullptr;
    }
}

// RC 06b42f40 (SendServerLoadHint's setter): no null test; a block already held is not released.
void FRpcServerLoadHintPacket::SetData(const unsigned char* InData, unsigned int InSize) {
    Data = static_cast<unsigned char*>(::operator new(InSize, std::nothrow));
    std::memcpy(Data, InData, InSize);
    Size = InSize;
}

// RC 06b42f90 / RS 06bdbca0.
unsigned int FRpcServerLoadHintPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Size, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Kind, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(Data, Size)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b430e0: the body is checked without committing (0x17); a failed allocation returns 5.
// RS 06bdbdf0: no prior check and no null test (reproduced on purpose). A block already held
// is overwritten.
unsigned int FRpcServerLoadHintPacket::Decode(const unsigned char* Buffer,
    unsigned int InSize, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), InSize);
    if (!Stream.Get(&Size, 4u)) return MarshalError;
    if (!Stream.Get(&Kind, 4u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    if (MarshalCursor + Size > MarshalLimit) MarshalError = kPacketDecodeOverrun;
    if (MarshalError != 0) return MarshalError;
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
    if (Data == nullptr) return kPacketNoMemory;
#else
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
#endif
    if (!Stream.Get(Data, Size)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x4e SETDIRECTIVE ---

// The field-wise copy of FPayloadServerSetDirective that retail repeats in four places: its
// operator= (RC 06b43710 / RS 06bdc400), the sender 06b35f40, and the decoders 06b434d0 /
// 06bdc1c0. Every field from +0x00 to +0x90 by integer moves; the padding bytes +0x09..+0x0b
// and +0x91..+0x93 are never written.
static void RpcServerCopyDirectiveFields(void* Destination, const void* Source) {
    unsigned char* Dst = static_cast<unsigned char*>(Destination);
    const unsigned char* Src = static_cast<const unsigned char*>(Source);
    std::memcpy(Dst, Src, 0x09u);
    std::memcpy(Dst + 0x0cu, Src + 0x0cu, 0x85u);
}

// RC 06b4aca0 / RS 06be57c0: +0x00 and the padding stay as operator new left them.
FRpcServerSetDirectivePacket::FRpcServerSetDirectivePacket() : FPacketBase(FLPACKET_SERVER_SETDIRECTIVE) {
    Directive.Field_0x04 = 0;
    Directive.bDockApproachCleared = 0;
    Directive.DelaySeconds = 0.0f;
    Directive.DockObjectId = 0;
    Directive.DockSecondaryObjectId = 0;
    Directive.DockPortIndex = -1;
    Directive.ProxTargetId = 0;
    Directive.ProxNearDist = 0.0f;
    Directive.ProxFarDist = 0.0f;
    Directive.FollowTargetId = 0;
    Directive.FollowDistance = 0.0f;
    Directive.FollowOffset.x = 0.0f;
    Directive.FollowOffset.y = 0.0f;
    Directive.FollowOffset.z = 0.0f;
    Directive.FollowEngageRange = 0.0f;
    Directive.GotoMode = 3;
    Directive.GotoDestination.x = 0.0f;
    Directive.GotoDestination.y = 0.0f;
    Directive.GotoDestination.z = 0.0f;
    Directive.GotoTargetId = 0;
    Directive.SplineP1 = FRpcDirectiveBlock{0u, 0u, 0u};
    Directive.SplineP2 = FRpcDirectiveBlock{0u, 0u, 0u};
    Directive.SplineP0 = FRpcDirectiveBlock{0u, 0u, 0u};
    Directive.SplineP3 = FRpcDirectiveBlock{0u, 0u, 0u};
    Directive.GotoRange = 0.0f;
    Directive.GotoSpeed = 0.0f;
    Directive.bGotoFlyThrough = 0;
    Directive.Field_0x8d = 0;
    Directive.bGotoCruise = 0;
    Directive.bGotoNoCruise = 0;
    Directive.bGotoDisableAvoidance = 0;
}

// The copy inlined in the sender RC 06b35f40 (Source -> packet +0x20).
void FRpcServerSetDirectivePacket::SetDirective(const FPayloadServerSetDirective& Source) {
    RpcServerCopyDirectiveFields(&Directive, &Source);
}

// RC 06b43270 / RS 06bdbf60: one 0x94-byte reservation; retail copies the record to a stack
// temporary field by field and assigns it to the buffer with operator=, which writes the same
// bytes as the direct field copy here.
unsigned int FRpcServerSetDirectivePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Reserve(0x94u, kPacketEncodeOverflow)) return kPacketEncodeOverflow;
    const unsigned int Cached = MarshalPtr;
    RpcServerCopyDirectiveFields(reinterpret_cast<void*>(Cached), &Directive);
    MarshalPtr = Cached + 0x94u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b434d0 / RS 06bdc1c0 (identical): one 0x94-byte reservation and the field copy.
unsigned int FRpcServerSetDirectivePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Reserve(0x94u, kPacketDecodeOverrun)) return MarshalError;
    RpcServerCopyDirectiveFields(&Directive, reinterpret_cast<const void*>(MarshalPtr));
    Stream.CommitReadPointer(0x94u);
    return Stream.Finish(OutLength, false);
}

// --- 0x52 PLAYERLIST ---

// RC 06b466a0 / RS 06bf9f40.
FRpcServerPlayerListPacket::~FRpcServerPlayerListPacket() {
    PlayerId = 0;
    if (Name != nullptr) {
        std::free(Name);
        Name = nullptr;
    }
}

// RC 06b466f0 (the setter all three senders call); the _wcsdup result is not null-tested.
void FRpcServerPlayerListPacket::SetEntry(const wchar_t* InName, unsigned int InPlayerId) {
    PlayerId = InPlayerId;
    if (Name != nullptr) {
        std::free(Name);
        Name = nullptr;
    }
    if (InName != nullptr) {
        Name = _wcsdup(InName);
    }
}

// RC 06b46730 / RS 06bf9fb0 (identical): Kind, PlayerId, the byte +0x2c, then the
// character count as one byte -- the low byte of wcslen + 1 (it wraps for 255+ characters),
// 0 for a null Name -- and, when it is not 0, count * 2 bytes from Name.
unsigned int FRpcServerPlayerListPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Kind, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&PlayerId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bAlreadyPresent, 1u)) return kPacketEncodeOverflow;
    unsigned char Count = 0;
    if (Name != nullptr) {
        Count = static_cast<unsigned char>(std::wcslen(Name) + 1u);
    }
    if (!Stream.Put(&Count, 1u)) return kPacketEncodeOverflow;
    if (Count != 0) {
        if (!Stream.PutBlock(Name, static_cast<unsigned int>(Count) * 2u)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b46900 / RS 06bfa180. A zero count leaves Name as it was. RC checks the body without
// committing (0x17) before malloc; RS has no prior check. No null test of malloc's result, and a
// Name already held is overwritten, not freed.
unsigned int FRpcServerPlayerListPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Kind, 4u)) return MarshalError;
    if (!Stream.Get(&PlayerId, 4u)) return MarshalError;
    if (!Stream.Get(&bAlreadyPresent, 1u)) return MarshalError;
    unsigned char Count = 0;
    if (!Stream.Get(&Count, 1u)) return MarshalError;
    if (Count != 0) {
        const unsigned int Bytes = static_cast<unsigned int>(Count) * 2u;
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + Bytes > MarshalLimit) MarshalError = kPacketDecodeOverrun;
        if (MarshalError != 0) return MarshalError;
#endif
        Name = static_cast<wchar_t*>(std::malloc(Bytes));
        if (!Stream.Get(Name, Bytes)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

// --- 0x48 SETCOLLISIONGROUPS ---

// RC 06b3a360 (SendServerSetCollisionGroups's setter): the list's operator=.
void FRpcServerSetCollisionGroupsPacket::SetGroups(const std::list<CollisionGroupDesc>& Source) {
    Groups = Source;
}

// RC 06b62b20 / RS 06bf8d00; payload helper RC 06b3a180 / RS 06bda4e0, whose nonzero return is
// returned as is.
unsigned int FRpcServerSetCollisionGroupsPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned int Result = RpcWireEncodeCollisionGroups(Stream, Groups);
    if (Result != 0) return Result;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b62bf0 / RS 06bf8dd0; payload helper RC 06b3a230 / RS 06bda590, whose nonzero return is
// returned as is.
unsigned int FRpcServerSetCollisionGroupsPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    const unsigned int Result = RpcWireDecodeCollisionGroups(*this, Stream, Groups);
    if (Result != 0) return Result;
    return Stream.Finish(OutLength, false);
}

// --- 0x4f SENDCOMM ---

// The one-byte quantizer of +0x70: float * float 0.1f (RC 06b6e944 / RS 06c0e278) * double
// 255.0 (RC 06b6e390 / RS 06c0e270), truncating _ftol, low byte. Its inverse: the byte
// zero-extended * double 2/51 (RC 06b6e948 / RS 06c0e280, 3fa41414'14141414).
static const float GRpcServerSendCommTenth = 0.1f;
static const double GRpcServerSendCommScale255 = 255.0;
static const double GRpcServerSendCommExpand = 2.0 / 51.0;

static unsigned char RpcServerSendCommQuantize(const float* Value) {
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        mov ecx, Value
        fld dword ptr [ecx]
        fmul dword ptr GRpcServerSendCommTenth
        fmul qword ptr GRpcServerSendCommScale255
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned char>(Quantized);
}

static void RpcServerSendCommExpand(unsigned char Encoded, float* Value) {
    const int Widened = static_cast<int>(Encoded);
    __asm {
        mov ecx, Value
        fild dword ptr Widened
        fmul qword ptr GRpcServerSendCommExpand
        fstp dword ptr [ecx]
    }
}

// RC 06b4cd10 / RS 06be7910.
FRpcServerSendCommPacket::FRpcServerSendCommPacket()
    : FPacketBase(FLPACKET_SERVER_SENDCOMM), TextIds(nullptr), bReceiverIsOwnShip(1) {
    Body.PartIds[0] = 0;
    Body.PartIds[1] = 0;
    Body.PartIds[2] = 0;
    Body.PartIds[3] = 0;
    Body.AccessoryCount = 0;
}

// RC 06b4cd70 / RS 06be7970.
FRpcServerSendCommPacket::~FRpcServerSendCommPacket() {
    if (TextIds != nullptr) ::operator delete(TextIds);
    TextIds = nullptr;
}

// RC 06b63190 / RS 06bf9370 (they differ only in the constant addresses). Part ids 2 and 3
// are not sent. The accessory loop is bounded by the signed count alone, re-read each pass:
// counts above 8 read on past AccessoryIds into the fields that follow and, from 15 on, past
// the packet (as retail does). The extra-array loop is bounded by the signed byte. The
// quantized byte is computed before its reservation.
unsigned int FRpcServerSendCommPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&SenderId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ReceiverId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&VoiceId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bReceiverIsOwnShip, 1u)) return kPacketEncodeOverflow;
    if (bReceiverIsOwnShip != 0) {
        if (!Stream.Put(&Body.PartIds[0], 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&Body.PartIds[1], 4u)) return kPacketEncodeOverflow;
        const unsigned char CountByte = static_cast<unsigned char>(Body.AccessoryCount);
        if (!Stream.Put(&CountByte, 1u)) return kPacketEncodeOverflow;
        const unsigned int* Ids = &Body.AccessoryIds[0];
        for (int Index = 0; Index < static_cast<int>(Body.AccessoryCount); ++Index) {
            if (!Stream.Put(&Ids[Index], 4u)) return kPacketEncodeOverflow;
        }
    }
    if (!Stream.Put(&CaptionIds, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TextCount, 1u)) return kPacketEncodeOverflow;
    for (int Index = 0; Index < static_cast<int>(TextCount); ++Index) {
        if (!Stream.Put(&TextIds[Index], 4u)) return kPacketEncodeOverflow;
    }
    const unsigned short LowWord = static_cast<unsigned short>(PermutationIds);
    if (!Stream.Put(&LowWord, 2u)) return kPacketEncodeOverflow;
    const unsigned char Quantized = RpcServerSendCommQuantize(&Duration);
    if (!Stream.Put(&Quantized, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b634b0 / RS 06bf9690. With the costume byte set: part ids 2 and 3 zeroed, and the
// accessory loop bounded by AccessoryCount re-read each pass -- counts 9..255 write on past
// AccessoryIds (the ninth id lands on AccessoryCount itself and becomes the new bound),
// reproduced on purpose. A held extra array is replaced by new((signed) count * 4): RC checks
// that size without committing (0x17) and returns 5 when new fails; RS does neither. The last
// byte is expanded through x87 into +0x70.
unsigned int FRpcServerSendCommPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&SenderId, 4u)) return MarshalError;
    if (!Stream.Get(&ReceiverId, 4u)) return MarshalError;
    if (!Stream.Get(&VoiceId, 4u)) return MarshalError;
    if (!Stream.Get(&bReceiverIsOwnShip, 1u)) return MarshalError;
    if (bReceiverIsOwnShip != 0) {
        if (!Stream.Get(&Body.PartIds[0], 4u)) return MarshalError;
        if (!Stream.Get(&Body.PartIds[1], 4u)) return MarshalError;
        Body.PartIds[2] = 0;
        Body.PartIds[3] = 0;
        unsigned char CountByte = 0;
        if (!Stream.Get(&CountByte, 1u)) return MarshalError;
        Body.AccessoryCount = CountByte;
        unsigned int* Ids = &Body.AccessoryIds[0];
        for (int Index = 0; Index < static_cast<int>(Body.AccessoryCount); ++Index) {
            if (!Stream.Get(&Ids[Index], 4u)) return MarshalError;
        }
    } else {
        Body.PartIds[0] = 0;
        Body.PartIds[1] = 0;
        Body.PartIds[2] = 0;
        Body.PartIds[3] = 0;
        Body.AccessoryCount = 0;
    }
    if (!Stream.Get(&CaptionIds, 4u)) return MarshalError;
    if (!Stream.Get(&TextCount, 1u)) return MarshalError;
    const unsigned int ExtraBytes = static_cast<unsigned int>(static_cast<int>(TextCount)) * 4u;
#if defined(REMOTECLIENT_EXPORTS)
    if (MarshalCursor + ExtraBytes > MarshalLimit) MarshalError = kPacketDecodeOverrun;
    if (MarshalError != 0) return MarshalError;
#endif
    if (TextIds != nullptr) ::operator delete(TextIds);
    TextIds = static_cast<unsigned int*>(::operator new(ExtraBytes, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
    if (TextIds == nullptr) return kPacketNoMemory;
#endif
    for (int Index = 0; Index < static_cast<int>(TextCount); ++Index) {
        if (!Stream.Get(&TextIds[Index], 4u)) return MarshalError;
    }
    unsigned short LowWord = 0;
    if (!Stream.Get(&LowWord, 2u)) return MarshalError;
    PermutationIds = LowWord;
    unsigned char Quantized = 0;
    if (!Stream.Get(&Quantized, 1u)) return MarshalError;
    RpcServerSendCommExpand(Quantized, &Duration);
    return Stream.Finish(OutLength, false);
}

// --- 0x54 MISCOBJUPDATE ---

// RC 06b4d0b0 / RS 06be7cd0.
FRpcServerMiscObjUpdatePacket::FRpcServerMiscObjUpdatePacket()
    : FPacketBase(FLPACKET_SERVER_MISCOBJUPDATE), Flags(0), PlayerId(0), ObjectId(0), Rank(0),
      SystemId(0), GroupId(0), MissionId(0), MissionSetBy(0), Field_0x50(0),
      Affiliation(kNoAffiliation), IdCount(0) {}

// RC 06b45830.
void FRpcServerMiscObjUpdatePacket::SetObjectId(unsigned int Value) {
    Flags |= kMiscObjUpdateFlagObjectId;
    ObjectId = Value;
}

// RC 06b45840.
void FRpcServerMiscObjUpdatePacket::SetPlayerId(unsigned int Value) {
    Flags |= kMiscObjUpdateFlagPlayerId;
    PlayerId = Value;
}

// RC 06b45850.
void FRpcServerMiscObjUpdatePacket::SetAffiliation(unsigned int Value) {
    Flags |= kMiscObjUpdateFlagAffiliation;
    Affiliation = Value;
}

// RC 06b45860.
void FRpcServerMiscObjUpdatePacket::SetGroupId(unsigned int Value) {
    Flags |= kMiscObjUpdateFlagGroupId;
    GroupId = Value;
}

// RC 06b45870.
void FRpcServerMiscObjUpdatePacket::SetMission(unsigned int InMissionId, unsigned int InMissionSetBy) {
    Flags |= kMiscObjUpdateFlagMission;
    MissionId = InMissionId;
    MissionSetBy = InMissionSetBy;
}

// RC 06b45890 (VC6 basic_string<unsigned short>::assign).
void FRpcServerMiscObjUpdatePacket::SetText(const std::wstring& Source) {
    Flags |= kMiscObjUpdateFlagText;
    Text.assign(Source);
}

// RC 06b45a40.
void FRpcServerMiscObjUpdatePacket::SetSystemId(unsigned int Value) {
    Flags |= kMiscObjUpdateFlagSystemId;
    SystemId = Value;
}

// RC 06b45a50.
void FRpcServerMiscObjUpdatePacket::SetRank(unsigned short Value) {
    Flags |= kMiscObjUpdateFlagRank;
    Rank = Value;
}

// RC 06b45a60: IdCount keeps the low byte of the size.
void FRpcServerMiscObjUpdatePacket::AddId(unsigned int Id) {
    Flags |= kMiscObjUpdateFlagIds;
    Ids.push_back(Id);
    IdCount = static_cast<unsigned char>(Ids.size());
}

// RC 06b45aa0 / RS 06bfa8e0 (identical). The text length is the low word of the string's
// length, and that many characters are copied (the reservation is made for 0 as well); the
// id loop walks the vector, not IdCount.
unsigned int FRpcServerMiscObjUpdatePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Flags, 2u)) return kPacketEncodeOverflow;
    if ((Flags & kMiscObjUpdateFlagObjectId) != 0) {
        if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    }
    if ((Flags & kMiscObjUpdateFlagPlayerId) != 0) {
        if (!Stream.Put(&PlayerId, 4u)) return kPacketEncodeOverflow;
    }
    if ((Flags & kMiscObjUpdateFlagGroupId) != 0) {
        if (!Stream.Put(&GroupId, 4u)) return kPacketEncodeOverflow;
    }
    if ((Flags & kMiscObjUpdateFlagMission) != 0) {
        if (!Stream.Put(&MissionId, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&MissionSetBy, 4u)) return kPacketEncodeOverflow;
    }
    if ((Flags & kMiscObjUpdateFlagText) != 0) {
        const unsigned short Length = static_cast<unsigned short>(Text.size());
        if (!Stream.Put(&Length, 2u)) return kPacketEncodeOverflow;
        if (!Stream.PutBlock(Text.c_str(), static_cast<unsigned int>(Length) * 2u)) return kPacketEncodeOverflow;
    }
    if ((Flags & kMiscObjUpdateFlagAffiliation) != 0) {
        if (!Stream.Put(&Affiliation, 4u)) return kPacketEncodeOverflow;
    }
    if ((Flags & kMiscObjUpdateFlagRank) != 0) {
        if (!Stream.Put(&Rank, 2u)) return kPacketEncodeOverflow;
    }
    if ((Flags & kMiscObjUpdateFlagSystemId) != 0) {
        if (!Stream.Put(&SystemId, 4u)) return kPacketEncodeOverflow;
    }
    if ((Flags & kMiscObjUpdateFlagIds) != 0) {
        if (!Stream.Put(&IdCount, 1u)) return kPacketEncodeOverflow;
        for (std::vector<unsigned int>::const_iterator It = Ids.begin(); It != Ids.end(); ++It) {
            if (!Stream.Put(&*It, 4u)) return kPacketEncodeOverflow;
        }
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b45e40 / RS 06bfac80, the same order as Encode. The text goes through a malloc'd buffer
// of length + 1 characters and is assigned up to the first null. RC checks the length
// (characters counted as bytes, not * 2) without committing (0x17) and returns 5 when malloc
// fails; RS does neither. When the characters overrun the buffer the block leaks (reproduced on
// purpose). The ids are appended (the vector is not cleared first); IdCount, re-read each
// pass, bounds the loop.
unsigned int FRpcServerMiscObjUpdatePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Flags, 2u)) return MarshalError;
    if ((Flags & kMiscObjUpdateFlagObjectId) != 0) {
        if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    }
    if ((Flags & kMiscObjUpdateFlagPlayerId) != 0) {
        if (!Stream.Get(&PlayerId, 4u)) return MarshalError;
    }
    if ((Flags & kMiscObjUpdateFlagGroupId) != 0) {
        if (!Stream.Get(&GroupId, 4u)) return MarshalError;
    }
    if ((Flags & kMiscObjUpdateFlagMission) != 0) {
        if (!Stream.Get(&MissionId, 4u)) return MarshalError;
        if (!Stream.Get(&MissionSetBy, 4u)) return MarshalError;
    }
    if ((Flags & kMiscObjUpdateFlagText) != 0) {
        unsigned short Length = 0;
        if (!Stream.Get(&Length, 2u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + Length > MarshalLimit) MarshalError = kPacketDecodeOverrun;
        if (MarshalError != 0) return MarshalError;
        wchar_t* Chars = static_cast<wchar_t*>(std::malloc(static_cast<unsigned int>(Length) * 2u + 2u));
        if (Chars == nullptr) return kPacketNoMemory;
#else
        wchar_t* Chars = static_cast<wchar_t*>(std::malloc(static_cast<unsigned int>(Length) * 2u + 2u));
#endif
        if (!Stream.Get(Chars, static_cast<unsigned int>(Length) * 2u)) return MarshalError;
        Chars[Length] = 0;
        Text.assign(Chars, std::wcslen(Chars));
        std::free(Chars);
    }
    if ((Flags & kMiscObjUpdateFlagAffiliation) != 0) {
        if (!Stream.Get(&Affiliation, 4u)) return MarshalError;
    }
    if ((Flags & kMiscObjUpdateFlagRank) != 0) {
        if (!Stream.Get(&Rank, 2u)) return MarshalError;
    }
    if ((Flags & kMiscObjUpdateFlagSystemId) != 0) {
        if (!Stream.Get(&SystemId, 4u)) return MarshalError;
    }
    if ((Flags & kMiscObjUpdateFlagIds) != 0) {
        if (!Stream.Get(&IdCount, 1u)) return MarshalError;
        for (unsigned int Index = 0; Index < static_cast<unsigned int>(IdCount); ++Index) {
            unsigned int Id = 0;
            if (!Stream.Get(&Id, 4u)) return MarshalError;
            Ids.push_back(Id);
        }
    }
    return Stream.Finish(OutLength, false);
}

// --- 0x42 CREATESOLAR ---

// RC 06b4c810 / RS 06be7410: the block occupies +0x20..+0x9b; +0xb0 and +0xb4 are left
// uninitialized.
FRpcServerCreateSolarPacket::FRpcServerCreateSolarPacket()
    : FPacketBase(FLPACKET_SERVER_CREATESOLAR), bDestructible(0), bMarkedBy(0), NameASize(0), NameA(nullptr),
      NameBSize(0), NameB(nullptr) {}

// RC 06b4c920 / RS 06be7520: NameB (+0xac) then NameA (+0xa4), neither cleared.
FRpcServerCreateSolarPacket::~FRpcServerCreateSolarPacket() {
    if (NameB != nullptr) ::operator delete(NameB);
    if (NameA != nullptr) ::operator delete(NameA);
}

// RC 06b62640 / RS 06bf8820. The block helper's return is discarded; its sticky error stops
// the first byte. The attitude byte is quantized after Affiliation is written and before its
// own reservation.
unsigned int FRpcServerCreateSolarPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    RpcServerCreateObjectEncode(*this, Stream, Fields);
    if (!Stream.Put(&bDestructible, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bMarkedBy, 1u)) return kPacketEncodeOverflow;
    if (!SinglePlayer()) {
        if (!Stream.Put(&Affiliation, 4u)) return kPacketEncodeOverflow;
        const unsigned char Quantized = RpcServerQuantize127(&Attitude);
        if (!Stream.Put(&Quantized, 1u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&NameASize, 4u)) return kPacketEncodeOverflow;
        if (!Stream.PutBlock(NameA, NameASize)) return kPacketEncodeOverflow;
        if (!Stream.Put(&NameBSize, 4u)) return kPacketEncodeOverflow;
        if (!Stream.PutBlock(NameB, NameBSize)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b62900 / RS 06bf8ae0. The block helper's return is discarded. Outside single player the
// two name blocks (RC 06b4eb30 / RS 06bec240) have their status discarded and 0 is stored over
// MarshalError, so an overrunning name block still decodes as success. Reproduced on purpose.
unsigned int FRpcServerCreateSolarPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    RpcServerCreateObjectDecode(*this, Stream, Fields);
    if (!Stream.Get(&bDestructible, 1u)) return MarshalError;
    if (!Stream.Get(&bMarkedBy, 1u)) return MarshalError;
    if (!SinglePlayer()) {
        if (!Stream.Get(&Affiliation, 4u)) return MarshalError;
        unsigned char Quantized = 0;
        if (!Stream.Get(&Quantized, 1u)) return MarshalError;
        RpcServerExpand127(Quantized, &Attitude);
        RpcWireDecodeOwnedBlock(*this, Stream, NameASize, NameA);
        RpcWireDecodeOwnedBlock(*this, Stream, NameBSize, NameB);
    }
    MarshalError = 0;
    return Stream.Finish(OutLength, false);
}

// ===== end SERVER 0x40..0x5b =====

// ===== SERVER 0x01..0x1f part 2 =====
// 0x03 CHARACTERINFO. RC and RS bodies are identical except the RC decoder (#if branches
// below). Addresses are RS unless marked.

// RS 06bd4ed0.
void RpcServerCharacterIdBlockFree(FRpcServerCharacterIdBlock* Block) {
    if (Block->Ids != nullptr) ::operator delete(Block->Ids);
    ::operator delete(Block);
}

FRpcServerCharacterRecord::FRpcServerCharacterRecord()
    : bIsRestart(0), bIsAutoSave(0), DescripStrId(0), TimestampLow(0), TimestampHigh(0),
      ShipArchetype(0), Money(0), SystemId(0), BaseId(0), LocationId(0),
      VoiceId(0), Rank(0), Field_0x24c(0), HullStatusBits(0), NumKills(0),
      NumMissionFailures(0), NumMissionSuccesses(0), EquipmentCount(0), BlockA(nullptr), BlockB(nullptr) {
    Name.Buffer[0] = '\0';
}

// 06bdeab2: +0x274, then +0x278; the members follow in reverse declaration order.
FRpcServerCharacterRecord::~FRpcServerCharacterRecord() {
    if (BlockA != nullptr) RpcServerCharacterIdBlockFree(BlockA);
    if (BlockB != nullptr) RpcServerCharacterIdBlockFree(BlockB);
}

// 06bdea60.
FRpcServerCharacterInfoPacket::~FRpcServerCharacterInfoPacket() {
    for (std::list<FRpcServerCharacterRecord*>::iterator It = Records.begin();
         It != Records.end(); ++It) {
        if (*It != nullptr) delete *It;
    }
}

// RS 06be0281 (TextA) / 06be02fb (TextB): the length word in characters is checked; the
// 2*length run is counted (the overflow error is recorded) but NOT tested before the copy, so
// an oversized string is written past the buffer and the next field fails. Reproduced on
// purpose.
static bool RpcServerPutWideText(FRpcWireStream& Stream, const std::wstring& Text) {
    const unsigned short Length = static_cast<unsigned short>(Text.size() & 0xffffu);
    if (!Stream.Put(&Length, 2u)) return false;
    const unsigned int Bytes = static_cast<unsigned int>(Length) * 2u;
    Stream.Reserve(Bytes, kPacketEncodeOverflow);
    Stream.PutReserved(Text.c_str(), Bytes);
    return true;
}

// RS 06be0636 (+0x274) and 06be07d6 (+0x278).
static bool RpcServerPutIdBlock(FRpcWireStream& Stream, const FRpcServerCharacterIdBlock* Block) {
    const unsigned char Present = (Block != nullptr) ? 1u : 0u;
    if (!Stream.Put(&Present, 1u)) return false;
    if (Block == nullptr) return true;
    if (!Stream.Put(&Block->Body, 4u)) return false;
    if (!Stream.Put(&Block->Head, 4u)) return false;
    if (!Stream.Put(&Block->LeftHand, 4u)) return false;
    if (!Stream.Put(&Block->RightHand, 4u)) return false;
    if (!Stream.Put(&Block->Count, 4u)) return false;
    // Count dwords as one Count*4 byte block, checked before the copy.
    return Stream.PutBlock(Block->Ids, static_cast<unsigned int>(Block->Count) << 2);
}

// RS 06be00e0 / RC 06b41aa0.
unsigned int FRpcServerCharacterInfoPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    // The low byte of the list size.
    const unsigned char Count = static_cast<unsigned char>(Records.size() & 0xffu);
    if (!Stream.Put(&Count, 1u)) return kPacketEncodeOverflow;
    // A zero count skips the walk; otherwise the whole ring is walked.
    if (Count != 0) {
        for (std::list<FRpcServerCharacterRecord*>::const_iterator It = Records.begin();
             It != Records.end(); ++It) {
            const FRpcServerCharacterRecord* Record = *It;
            if (Record == nullptr) continue;
            // An unbounded strlen, truncated to a word.
            const unsigned short NameLength =
                static_cast<unsigned short>(std::strlen(Record->Name.Buffer));
            if (!Stream.Put(&NameLength, 2u)) return kPacketEncodeOverflow;
            if (!Stream.PutBlock(Record->Name.Buffer, NameLength)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->bIsRestart, 1u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->bIsAutoSave, 1u)) return kPacketEncodeOverflow;
            if (!RpcServerPutWideText(Stream, Record->TextA)) return kPacketEncodeOverflow;
            if (!RpcServerPutWideText(Stream, Record->TextB)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->DescripStrId, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->TimestampHigh, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->TimestampLow, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->ShipArchetype, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->Money, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->SystemId, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->BaseId, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->LocationId, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->VoiceId, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->Rank, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->Field_0x24c, 4u)) return kPacketEncodeOverflow;
            // The hull status crosses the x87 stack.
            if (!Stream.PutFloat(*reinterpret_cast<const float*>(&Record->HullStatusBits)))
                return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->NumKills, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->NumMissionFailures, 4u)) return kPacketEncodeOverflow;
            if (!Stream.Put(&Record->NumMissionSuccesses, 4u)) return kPacketEncodeOverflow;
            if (!RpcServerPutIdBlock(Stream, Record->BlockA)) return kPacketEncodeOverflow;
            if (!RpcServerPutIdBlock(Stream, Record->BlockB)) return kPacketEncodeOverflow;
            // The stored count byte, then the whole list.
            if (!Stream.Put(&Record->EquipmentCount, 1u)) return kPacketEncodeOverflow;
            if (Record->Equipment.Records != nullptr) {
                for (std::list<FRpcServerEquipRecord*>::const_iterator Item =
                         Record->Equipment.Records->begin();
                     Item != Record->Equipment.Records->end(); ++Item) {
                    const FRpcServerEquipRecord* E = *Item;
                    if (!Stream.Put(&E->Count, 4u)) return kPacketEncodeOverflow;
                    if (!Stream.PutFloat(E->Health)) return kPacketEncodeOverflow;
                    if (!Stream.Put(&E->ArchId, 4u)) return kPacketEncodeOverflow;
                    if (!Stream.Put(&E->Id, 2u)) return kPacketEncodeOverflow;
                    if (!Stream.Put(&E->bEquipped, 1u)) return kPacketEncodeOverflow;
                    if (!Stream.Put(&E->bMission, 1u)) return kPacketEncodeOverflow;
                    if (!Stream.Put(&E->HardpointSize, 2u)) return kPacketEncodeOverflow;
                    if (!Stream.PutBlock(E->Hardpoint, E->HardpointSize))
                        return kPacketEncodeOverflow;
                }
            }
            const unsigned int GroupCount = static_cast<unsigned int>(Record->CollisionGroups.size());
            if (!Stream.Put(&GroupCount, 4u)) return kPacketEncodeOverflow;
            for (std::list<CollisionGroupDesc>::const_iterator Group =
                     Record->CollisionGroups.begin();
                 Group != Record->CollisionGroups.end(); ++Group) {
                if (!Stream.Put(&Group->Id, 2u)) return kPacketEncodeOverflow;
                if (!Stream.PutFloat(Group->Health)) return kPacketEncodeOverflow;
            }
        }
    }
    if (!Stream.Put(&SaveFailureCount, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RS 06bdee6f (and the TextB twin): append(count, ' '), then the 2*count run counted but NOT
// tested before it is copied over the string; the next field fails. Reproduced on purpose.
static bool RpcServerGetWideText(FRpcWireStream& Stream, std::wstring& Text) {
    unsigned short Length = 0;
    if (!Stream.Get(&Length, 2u)) return false;
    Text.append(static_cast<std::wstring::size_type>(Length), L' ');
    const unsigned int Bytes = static_cast<unsigned int>(Length) * 2u;
    Stream.Reserve(Bytes, kPacketDecodeOverrun);
    Stream.GetUncounted(&Text[0], Bytes);
    return true;
}

// RS 06bdf2e6 (+0x274; +0x278 at 06bdf4c1). The new-expression test only skips the zero
// stores; a null block is written through, reproduced on purpose. RC 06b40d6f: the array run is
// checked (not committed) before the allocation, and a null array returns 5.
static unsigned int RpcServerGetIdBlock(FRpcWireStream& Stream, FPacketBase& Packet,
    FRpcServerCharacterIdBlock*& Out) {
    unsigned char Present = 0;
    if (!Stream.Get(&Present, 1u)) return Packet.MarshalError;
    if (Present == 0) return 0;
    FRpcServerCharacterIdBlock* Block = static_cast<FRpcServerCharacterIdBlock*>(
        ::operator new(sizeof(FRpcServerCharacterIdBlock), std::nothrow));
    if (Block != nullptr) {
        Block->Ids = nullptr;
        Block->Count = 0;
        Block->RightHand = 0;
        Block->LeftHand = 0;
        Block->Body = 0;
        Block->Head = 0;
    }
    Out = Block;
    if (!Stream.Get(&Block->Body, 4u)) return Packet.MarshalError;
    if (!Stream.Get(&Block->Head, 4u)) return Packet.MarshalError;
    if (!Stream.Get(&Block->LeftHand, 4u)) return Packet.MarshalError;
    if (!Stream.Get(&Block->RightHand, 4u)) return Packet.MarshalError;
    if (!Stream.Get(&Block->Count, 4u)) return Packet.MarshalError;
    if (static_cast<unsigned int>(Block->Count) != 0) {                      // unsigned
        const unsigned int Bytes = static_cast<unsigned int>(Block->Count) << 2;
#if defined(REMOTECLIENT_EXPORTS)
        if (Packet.MarshalCursor + Bytes > Packet.MarshalLimit) Packet.MarshalError = kPacketDecodeOverrun;
        if (Packet.MarshalError != 0) return Packet.MarshalError;
        Block->Ids = static_cast<unsigned int*>(::operator new(Bytes, std::nothrow));
        if (Block->Ids == nullptr) return kPacketNoMemory;
#else
        // RS allocates before the run is checked, never null-tested.
        Block->Ids = static_cast<unsigned int*>(::operator new(Bytes, std::nothrow));
#endif
        if (!Stream.Get(Block->Ids, Bytes)) return Packet.MarshalError;
    }
    return 0;
}

// RS 06bdebe0 / RC 06b40500. A failed read returns at once; the record being decoded
// is not linked yet and is leaked, as in retail (reproduced on purpose), and so is an
// equipment record that was not yet pushed.
unsigned int FRpcServerCharacterInfoPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    unsigned char Count = 0;
    if (!Stream.Get(&Count, 1u)) return MarshalError;
    // A word counter against the byte count.
    for (unsigned short Index = 0; Index < static_cast<unsigned short>(Count); ++Index) {
        // new(0x27c); null skips the whole record.
        FRpcServerCharacterRecord* Record = new (std::nothrow) FRpcServerCharacterRecord();
        if (Record == nullptr) continue;
        unsigned short NameLength = 0;
        if (!Stream.Get(&NameLength, 2u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
        // RC 06b406c0: the length word is consumed, then bounded.
        if (NameLength >= 0x200u) return 0x4au;
#endif
        // RS: copied with the full wire length and terminated at [length], with no bound
        // (06bdedf0): reproduced on purpose.
        if (!Stream.Get(Record->Name.Buffer, NameLength)) return MarshalError;
        Record->Name.Buffer[NameLength] = '\0';
        if (!Stream.Get(&Record->bIsRestart, 1u)) return MarshalError;
        if (!Stream.Get(&Record->bIsAutoSave, 1u)) return MarshalError;
        if (!RpcServerGetWideText(Stream, Record->TextA)) return MarshalError;
        if (!RpcServerGetWideText(Stream, Record->TextB)) return MarshalError;
        if (!Stream.Get(&Record->DescripStrId, 4u)) return MarshalError;
        if (!Stream.Get(&Record->TimestampHigh, 4u)) return MarshalError;
        if (!Stream.Get(&Record->TimestampLow, 4u)) return MarshalError;
        if (!Stream.Get(&Record->ShipArchetype, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Money, 4u)) return MarshalError;
        if (!Stream.Get(&Record->SystemId, 4u)) return MarshalError;
        if (!Stream.Get(&Record->BaseId, 4u)) return MarshalError;
        if (!Stream.Get(&Record->LocationId, 4u)) return MarshalError;
        if (!Stream.Get(&Record->VoiceId, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Rank, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x24c, 4u)) return MarshalError;
        if (!Stream.Get(&Record->HullStatusBits, 4u)) return MarshalError;     // integer move
        if (!Stream.Get(&Record->NumKills, 4u)) return MarshalError;
        if (!Stream.Get(&Record->NumMissionFailures, 4u)) return MarshalError;
        if (!Stream.Get(&Record->NumMissionSuccesses, 4u)) return MarshalError;
        unsigned int Error = RpcServerGetIdBlock(Stream, *this, Record->BlockA);
        if (Error != 0) return Error;
        Error = RpcServerGetIdBlock(Stream, *this, Record->BlockB);
        if (Error != 0) return Error;
        if (!Stream.Get(&Record->EquipmentCount, 1u)) return MarshalError;
        // Word counter against the stored byte, re-read each round.
        for (unsigned short Item = 0; Item < static_cast<unsigned short>(Record->EquipmentCount);
             ++Item) {
            // A null record skips this element's reads.
            FRpcServerEquipRecord* E = static_cast<FRpcServerEquipRecord*>(
                ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
            if (E == nullptr) continue;
            E->ArchId = 0;
            E->Count = 0;
            E->Health = 0.0f;
            E->Id = 0;
            E->bEquipped = 0;
            E->bMission = 0;
            E->HardpointSize = 0;
            E->Hardpoint = nullptr;
            if (!Stream.Get(&E->Count, 4u)) return MarshalError;
            if (!Stream.Get(&E->Health, 4u)) return MarshalError;            // integer move
            if (!Stream.Get(&E->ArchId, 4u)) return MarshalError;
            if (!Stream.Get(&E->Id, 2u)) return MarshalError;
            if (!Stream.Get(&E->bEquipped, 1u)) return MarshalError;
            if (!Stream.Get(&E->bMission, 1u)) return MarshalError;
            if (!Stream.Get(&E->HardpointSize, 2u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
            // RC 06b411fc: the run is checked first, a null block returns 5.
            if (MarshalCursor + E->HardpointSize > MarshalLimit) MarshalError = kPacketDecodeOverrun;
            if (MarshalError != 0) return MarshalError;
            E->Hardpoint = static_cast<char*>(::operator new(E->HardpointSize, std::nothrow));
            if (E->Hardpoint == nullptr) return kPacketNoMemory;
#else
            // RS allocates (zero bytes included) before the check, no null test.
            E->Hardpoint = static_cast<char*>(::operator new(E->HardpointSize, std::nothrow));
#endif
            if (!Stream.Get(E->Hardpoint, E->HardpointSize)) return MarshalError;
            Record->Equipment.PushBack(E);
        }
        unsigned int GroupCount = 0;
        if (!Stream.Get(&GroupCount, 4u)) return MarshalError;
        for (unsigned int Group = 0; Group < GroupCount; ++Group) {
            // The node is linked first; its value is the stack pair {count just read, residue}.
            // The low word is then overwritten, so the pad keeps the count's high half. The
            // residue is always overwritten by the Health read or the record is leaked on
            // error, so 0 here is exact.
            CollisionGroupDesc Value;
            std::memcpy(&Value, &GroupCount, 4u);
            Value.Health = 0.0f;
            Record->CollisionGroups.push_back(Value);
            CollisionGroupDesc& Linked = Record->CollisionGroups.back();
            if (!Stream.Get(&Linked.Id, 2u)) return MarshalError;
            if (!Stream.Get(&Linked.Health, 4u)) return MarshalError;        // integer move
        }
        Records.push_back(Record);
    }
    if (!Stream.Get(&SaveFailureCount, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// RS 06bdea20 / RC 06b40340.
void FRpcServerCharacterInfoPacket::Dump(std::ostream& Out) const {
    if (Records.size() != 0) {
        Out << "   Num Characters : " << static_cast<unsigned int>(Records.size()) << "\n";
    }
}

// ===== end SERVER 0x01..0x1f part 2 =====
