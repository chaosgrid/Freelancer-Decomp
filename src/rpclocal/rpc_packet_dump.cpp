// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Packet vtable slot +0x0c, the per-type ostream dump.
//
// Every one of the 162 packet vtables is four slots wide { scalar dtor, encode, decode,
// dump }. Twelve types have a dump body of their own; the other 150 share
// IServerImpl::GetServerStats (.rdata:06db4434), an ICF fold of two one-argument no-op
// bodies -- those types dump nothing.
//
// The twelve, with the vtable address the constructor publishes:
//
//   ch2 0x01 CONNECTRESPONSE      LAB_06da8240   vtable 06db4fec, slot .rdata:06db4ff8
//   ch2 0x02 LOGINRESPONSE        LAB_06da84a0   vtable 06db4ffc, slot .rdata:06db5008
//   ch2 0x03 CHARACTERINFO        LAB_06d93310   vtable 06db3dc4, slot .rdata:06db3dd0
//   ch2 0x07 LAUNCH               FUN_06daaee0   vtable 06db50ec, slot .rdata:06db50f8
//   ch2 0x08 CHARSELECTVERIFIED   LAB_06dac020   vtable 06db517c, slot .rdata:06db5188
//   ch2 0x29 SETREPUTATION        FUN_06d92860   vtable 06db3d50, slot .rdata:06db3d5c
//   ch2 0x3c REPAIROBJECT         FUN_06dac2b0   vtable 06db524c, slot .rdata:06db5258
//   ch3 0x01 LOGIN                LAB_06daff50   vtable 06db5ca4, slot .rdata:06db5cb0
//   ch3 0x04 REQUESTLAUNCH        LAB_06daf370   vtable 06db5c14, slot .rdata:06db5c20
//   ch3 0x06 SELECTCHARACTER      LAB_06dafab0   vtable 06db5c94, slot .rdata:06db5ca0
//   ch3 0x3a DESTROYCHAR          LAB_06dafab0   vtable 06db5c84, slot .rdata:06db5c90
//   ch3 0x39 CREATENEWCHAR        LAB_06daf760   vtable 06db5c74, slot .rdata:06db5c80
//
// Eleven bodies for twelve types: LAB_06dafab0 is one address in two vtables.
//
// The vtable column is the LAST store into packet+0x00, not the first: thirteen of the
// 162 constructors first store the intermediate base table .rdata:PTR_FUN_06db4fbc
// (CHARACTERINFO, CREATESHIP, DAMAGEOBJECT, CREATELOOT, SETREPUTATION, SETGROUPFEELINGS,
// CREATECOUNTER, SETMISSIONOBJECTIVES, BURNFUSE, CREATESOLAR, SETCOLLISIONGROUPS,
// MUNCOLLISION, REQCOLLISIONGROUPS). See rpc_wire_generic.cpp.
//
// OSTREAM OVERLOADS. Retail picks a different MSVCP60 overload per field:
//
//   .rdata:06db317c  ordinal 462  std::operator<<(ostream&, unsigned char)
//   .rdata:06db3180  ordinal 432  operator<<(float)
//   .rdata:06db3184  ordinal 433  operator<<(double)
//   .rdata:06db3188  ordinal 428  operator<<(int)
//   .rdata:06db318c  ordinal 439  operator<<(void const*)
//   .rdata:06db3194  ordinal 464  std::operator<<(ostream&, char const*)
//   .rdata:06db319c  ordinal 429  operator<<(unsigned int)
//
// The three wide strings are streamed through operator<<(void const*): VC6's ostream has
// no wchar_t const* overload, so retail's dump prints the ADDRESS of the account /
// character / pilot name, never its text. Reproduced.
//
// The trailing newline is .rdata:DAT_06db3d88, a one-character C string, not std::endl.
//
// Nothing in rpclocal.dll calls slot +0x0c; retail's callers live in the network
// transport.

#include "rpc_containers.h"
#include "rpc_payloads.h"
#include "rpc_packet_classes.h"

#include <ostream>
#include <cstring>

// DAT_06db3d88; every body below pushes this same pointer.
static const char* const kRpcDumpEol = "\n";

// Packet vtable slot +0x0c: eleven bodies serving twelve types. See rpc_packet_dump.cpp.
// ---------------------------------------------------------------------------
// ch2 0x01 FLPACKET_SERVER_CONNECTRESPONSE -- LAB_06da8240.
// ---------------------------------------------------------------------------
void FPacket_FLPACKET_SERVER_CONNECTRESPONSE::Dump(std::ostream& Out) const {
    Out << "   Client ID: ";
    Out << Wire.ClientId;                                     // packet+0x20
    Out << kRpcDumpEol;
}

// ---------------------------------------------------------------------------
// ch2 0x02 FLPACKET_SERVER_LOGINRESPONSE -- LAB_06da84a0.
// The field is read as ONE BYTE and streamed through the unsigned char overload, so it
// prints as a character, not as a number. Reproduced.
// ---------------------------------------------------------------------------
void FPacket_FLPACKET_SERVER_LOGINRESPONSE::Dump(std::ostream& Out) const {
    const unsigned char Result = Wire.Field_0x00;      // packet+0x20
    Out << "   Result   : ";
    Out << Result;
    Out << kRpcDumpEol;
}

// ---------------------------------------------------------------------------
// ch2 0x03 FLPACKET_SERVER_CHARACTERINFO -- LAB_06d93310. The only guarded dump: writes
// nothing at all when the count is zero.
// ---------------------------------------------------------------------------
void FPacket_FLPACKET_SERVER_CHARACTERINFO::Dump(std::ostream& Out) const {
    const unsigned int Count = Wire.Records.Size;      // packet+0x2c
    if (Count == 0) {
        return;
    }
    Out << "   Num Characters : ";
    Out << Count;
    Out << kRpcDumpEol;
}

// ---------------------------------------------------------------------------
// ch2 0x07 FLPACKET_SERVER_LAUNCH -- FUN_06daaee0. Three floats, each on its own line;
// packet+0x2c..+0x37 is FPayloadServerLaunch::Position.
// ---------------------------------------------------------------------------
void FPacket_FLPACKET_SERVER_LAUNCH::Dump(std::ostream& Out) const {
    Out << "   X: ";
    Out << Wire.Position.x;
    Out << kRpcDumpEol;
    Out << "   Y: ";
    Out << Wire.Position.y;
    Out << kRpcDumpEol;
    Out << "   Z: ";
    Out << Wire.Position.z;
    Out << kRpcDumpEol;
}

// ---------------------------------------------------------------------------
// ch2 0x08 FLPACKET_SERVER_CHARSELECTVERIFIED -- LAB_06dac020.
//
// The second value is a DOUBLE at packet+0x28..+0x2f (two pushes consumed by the
// operator<<(double) overload). The payload struct keeps the range as two dwords because
// sender and receiver move it that way; this is the only place that treats the pair as
// one scalar.
// ---------------------------------------------------------------------------
void FPacket_FLPACKET_SERVER_CHARSELECTVERIFIED::Dump(std::ostream& Out) const {
    double Time = 0.0;
    ::memcpy(&Time, &Wire.ServerTimeLo, sizeof(Time));
    Out << "   RepInstance: ";
    Out << Wire.VibeHandle;                                   // packet+0x20
    Out << " time: ";
    Out << Time;
    Out << kRpcDumpEol;
}

// ---------------------------------------------------------------------------
// ch2 0x29 FLPACKET_SERVER_SETREPUTATION -- FUN_06d92860.
// packet+0x58 / +0x5c are payload+0x38 / +0x3c of FPayloadServerSetReputation.
// ---------------------------------------------------------------------------
void FPacket_FLPACKET_SERVER_SETREPUTATION::Dump(std::ostream& Out) const {
    Out << "   Target     : ";
    Out << Wire.VibeHandle;                                   // packet+0x58
    Out << kRpcDumpEol;
    Out << "   Affiliation: ";
    Out << Wire.Affiliation;                                   // packet+0x5c
    Out << kRpcDumpEol;
}

// ---------------------------------------------------------------------------
// ch2 0x3c FLPACKET_SERVER_REPAIROBJECT -- FUN_06dac2b0. The third field goes through
// operator<<(int), so it is SIGNED where the first two are unsigned. Reproduced.
// ---------------------------------------------------------------------------
void FPacket_FLPACKET_SERVER_REPAIROBJECT::Dump(std::ostream& Out) const {
    Out << "   Object ID  : ";
    Out << Wire.ObjectId;                                     // packet+0x20
    Out << kRpcDumpEol;
    Out << "   Equip ID   : ";
    Out << Wire.EquipId;                                      // packet+0x24
    Out << kRpcDumpEol;
    Out << "   Points     : ";
    Out << (int)Wire.Points;                                  // packet+0x28, signed
    Out << kRpcDumpEol;
}

// ---------------------------------------------------------------------------
// ch3 0x01 FLPACKET_CLIENT_LOGIN -- LAB_06daff50. Streams the ADDRESS of the wide
// account name at payload+0x000.
// ---------------------------------------------------------------------------
void FPacket_FLPACKET_CLIENT_LOGIN::Dump(std::ostream& Out) const {
    const void* Account = static_cast<const void*>(Wire.AccountName);
    Out << "   Account: ";
    Out << Account;
    Out << kRpcDumpEol;
}

// ---------------------------------------------------------------------------
// ch3 0x04 FLPACKET_CLIENT_REQUESTLAUNCH -- LAB_06daf370.
// ---------------------------------------------------------------------------
void FPacket_FLPACKET_CLIENT_REQUESTLAUNCH::Dump(std::ostream& Out) const {
    Out << "   Object ID: ";
    Out << Wire.Field_0x00;                                   // packet+0x20
    Out << kRpcDumpEol;
}

// ---------------------------------------------------------------------------
// ch3 0x06 SELECTCHARACTER and ch3 0x3a DESTROYCHAR -- LAB_06dafab0, ONE body in two
// vtables (.rdata:06db5c98 and .rdata:06db5c88).
// ---------------------------------------------------------------------------
void FRpcCharacterIdPacket::Dump(std::ostream& Out) const {
    const void* Character = static_cast<const void*>(CharacterId);
    Out << "   Character: ";
    Out << Character;                                         // the ADDRESS
    Out << kRpcDumpEol;
}

// ---------------------------------------------------------------------------
// ch3 0x39 FLPACKET_CLIENT_CREATENEWCHAR -- LAB_06daf760. Five values on ONE line and
// one newline at the very end, in the order Name, Fac, Base, Pkg, Pilot. The name goes
// through operator<<(void const*), the four dwords through operator<<(unsigned int).
// ---------------------------------------------------------------------------
void FPacket_FLPACKET_CLIENT_CREATENEWCHAR::Dump(std::ostream& Out) const {
    Out << "   Name: ";
    Out << static_cast<const void*>(Wire.Name);               // the ADDRESS
    Out << "   Fac: ";
    Out << Wire.FactionId;                                   // packet+0xa0
    Out << "   Base: ";
    Out << Wire.BaseId;                                   // packet+0xa4
    Out << "   Pkg: ";
    Out << Wire.PackageId;                                   // packet+0xa8
    Out << "   Pilot: ";
    Out << Wire.PilotId;                                   // packet+0xac
    Out << kRpcDumpEol;
}
