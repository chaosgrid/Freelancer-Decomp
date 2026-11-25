// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The 162 retail packet classes (and the two shared bases), one per packet type. Each
// class is its constructor's stores, its destructor and its vtable slots; the codec
// bodies of the classes that have their own are defined in rpc_wire_*.cpp,
// rpc_payload_*.cpp, rpc_containers.cpp and rpc_packet_dump.cpp. Registered by
// rpc_packet_registry.cpp.

#include "rpc_transport.h"
#include "rpc_containers.h"
#include "rpc_payloads.h"
#include "common_misc.h"        // SUBOBJ_ID_NONE, the default of three word fields

#include <cfloat>        // FLT_MAX, FIREWEAPON's target-position seed
#include <cstddef>       // offsetof
#include <cstdlib>
#include <new>
#include <ostream>

// The factory every class registers; see the header comment.
template<typename T>
int RpcPacketFactory(FPacketBase** OutPacket) {
    *OutPacket = nullptr;
    T* Packet = new (std::nothrow) T;
    if (Packet == nullptr) {
        return kPacketFactoryNoMemory;
    }
    *OutPacket = Packet;
    return kPacketFactoryOk;
}

// Vtable 06db5e54 { 06da26f0, ... }: the five classes that carry one equipment handle
// POINTER at +0x20. Retail folds its constructor into the derived ones.
class FRpcEquipListPacket : public FPacketBase {
protected:
    explicit FRpcEquipListPacket(unsigned int InPacketId) : FPacketBase(InPacketId) {}
public:
    // FUN_06da21d0
    ~FRpcEquipListPacket() override {
        FRpcEquipWireHandle* Handle = EquipHandle;
        if (Handle != nullptr) {
            Handle->Destroy();              // FUN_06d91340
            ::operator delete(Handle);
        }
    }
    // FUN_06d8fa90: allocates the handle when there is none (untested operator new(4)),
    // then deep-copies Source into it (rpc_containers.cpp).
    void AssignEquipHandle(const FRpcEquipWireHandle* Source);
    // FUN_06d95d20: the inverse, a deep copy into Dest; no handle, no copy.
    void ExtractEquipHandle(FRpcEquipWireHandle* Dest) const;

    // +0x20. A POINTER to the operator new(4) handle the list hangs off.
    FRpcEquipWireHandle* EquipHandle;
};

// Vtable 06db3fb4, slot 0 the purecall stub LAB_06db1dd0: abstract. CREATESHIP and
// CREATESOLAR derive from it. Its constructor and destructor parts are inlined into both
// concrete ones; what they build and tear down is the create-object header at +0x20,
// which the port holds as the first member of each class's payload,
// FRpcCreateObjectWire (rpc_containers.h). That type's constructor and destructor ARE
// this class's parts: the head stores, the COMMON.DLL list and DamageList ctors, and --
// after this vptr is stored -- the DamageList, the list, the handle and the id block.
// Held as a real member, the compiler also emits the EH unwind retail has for it
// (FuncInfo_06db70a4, states 1..3). Destruction order: derived body, then the payload
// member (header last), then FPacketBase.
class FRpcCreateObjectPacket : public FPacketBase {
protected:
    explicit FRpcCreateObjectPacket(unsigned int InPacketId) : FPacketBase(InPacketId) {}
public:
    ~FRpcCreateObjectPacket() override = 0;
};

// 06d97bf9 / 06d9e78a: the vptr store is all that is left of this part; the members it
// destroys belong to FRpcCreateObjectWire and are gone by the time this body runs.
inline FRpcCreateObjectPacket::~FRpcCreateObjectPacket() {
}

// The two collision-group classes, SETCOLLISIONGROUPS (vtable 06db52ac) and
// REQCOLLISIONGROUPS (06db5df4): their vtables hold the same four slots { 06da1130,
// 06dacca0, 06dacd70, no-op dump }, so the destructor and the codec pair live here. The
// derived constructors (FUN_06d9e8b0 / FUN_06da10b0) build the list.
class FRpcCollisionGroupPacket : public FPacketBase {
protected:
    explicit FRpcCollisionGroupPacket(unsigned int InPacketId) : FPacketBase(InPacketId) {}
public:
    // FUN_06d9e930
    ~FRpcCollisionGroupPacket() override {
        // 06d9e935..06d9e978: the list at +0x20 (Head +0x24, Size +0x28).
        RpcPacketDestroyEmbeddedList(&Wire);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dacca0 (rpc_wire_varlen.cpp)
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dacd70
    // +0x20. The retail std::list header itself; FUN_06d8f7d0 hands out its address.
    FRpcCollisionGroupList Wire;
};

// The two CHARACTER_ID classes, SELECTCHARACTER (vtable 06db5c94) and DESTROYCHAR
// (06db5c84): the same four slots { 06d9af50, 06daf830, 06daf970, 06dafab0 }, so the codec
// pair and the dump live here. 06d9af50 is the shared bare destructor.
class FRpcCharacterIdPacket : public FPacketBase {
protected:
    explicit FRpcCharacterIdPacket(unsigned int InPacketId) : FPacketBase(InPacketId) {}
public:
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daf830 (rpc_wire_varlen.cpp)
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daf970
    void Dump(std::ostream& Out) const override;            // 06dafab0 (rpc_packet_dump.cpp)
    // +0x20. A CHARACTER_ID image (its Buffer); held as raw bytes because CHARACTER_ID's
    // own constructor zeroes all 0x200 bytes, while the packet constructors store only the
    // first.
    char CharacterId[0x200];
};

static_assert(sizeof(FRpcEquipListPacket) == 0x24u, "equipment-list packets are 0x24 bytes");
static_assert(offsetof(FRpcEquipListPacket, EquipHandle) == 0x20u, "the handle pointer is at +0x20");
static_assert(sizeof(FRpcCollisionGroupPacket) == 0x2cu, "collision-group packets are 0x2c bytes");
static_assert(offsetof(FRpcCollisionGroupPacket, Wire) == 0x20u, "the list header is at +0x20");
static_assert(sizeof(FRpcCharacterIdPacket) == 0x220u, "CHARACTER_ID packets are 0x220 bytes");
static_assert(offsetof(FRpcCharacterIdPacket, CharacterId) == 0x20u, "the CHARACTER_ID is at +0x20");
static_assert(sizeof(FRpcCreateObjectPacket) == 0x20u,
              "the create-object header is the payload's first member, at +0x20");

// ===========================================================================
// Channel 1 -- the classes registered by FUN_06d9ace0, in its registration order.
// ===========================================================================

// FLPACKET_COMMON_FIREWEAPON -- id 0x10002, registered at 06d9aced.
// Factory FUN_06d9af70: operator new(0x40) + ctor FUN_06d9afb0. Vtable .rdata:06db4388 =
// { 06d9b000, 06da4f60, 06da5550, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_FIREWEAPON : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_FIREWEAPON() : FPacketBase(0x10002u) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.TargetPos.x = FLT_MAX;             // +0x24
        Wire.TargetPos.y = FLT_MAX;             // +0x28
        Wire.TargetPos.z = FLT_MAX;             // +0x2c
        Wire.Hardpoints.AllocPad = 0; // +0x30; byte copied from an uninitialised stack slot
        Wire.Hardpoints.Begin = nullptr;        // +0x34
        Wire.Hardpoints.End = nullptr;          // +0x38
        Wire.Hardpoints.Cap = nullptr;          // +0x3c
    }
    // FUN_06d9b020
    ~FPacket_FLPACKET_COMMON_FIREWEAPON() override {
        // No test: free, then the three dwords of the hardpoint vector cleared.
        ::operator delete(Wire.Hardpoints.Begin);
        Wire.Hardpoints.Begin = nullptr;
        Wire.Hardpoints.End = nullptr;
        Wire.Hardpoints.Cap = nullptr;
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da4f60
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da5550
    FPayloadCommonFireWeapon Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_FIREWEAPON) == 0x40u, "FLPACKET_COMMON_FIREWEAPON is operator new(0x40)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_FIREWEAPON, Wire) == 0x20u, "COMMON_FIREWEAPON: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_FIREWEAPON, Wire.Hardpoints.Begin) == 0x34u, "COMMON_FIREWEAPON: +0x34");
static_assert(offsetof(FPacket_FLPACKET_COMMON_FIREWEAPON, Wire.Hardpoints.End) == 0x38u, "COMMON_FIREWEAPON: +0x38");
static_assert(offsetof(FPacket_FLPACKET_COMMON_FIREWEAPON, Wire.Hardpoints.Cap) == 0x3cu, "COMMON_FIREWEAPON: +0x3c");

// FLPACKET_COMMON_UPDATEOBJECT -- id 0x10001, registered at 06d9ad03.
// Factory FUN_06d9aed0: operator new(0x4c) + ctor FUN_06d9af10. Vtable .rdata:06db4378 =
// { 06d9af50, 06da49e0, 06da4ca0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_UPDATEOBJECT : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_UPDATEOBJECT() : FPacketBase(0x10001u) {
        Wire.Position.x = 0.0f;                 // +0x28
        Wire.Position.y = 0.0f;                 // +0x2c
        Wire.Position.z = 0.0f;                 // +0x30
        Wire.Orientation.w = 1.0f;              // +0x34
        Wire.Orientation.x = 0.0f;              // +0x38
        Wire.Orientation.y = 0.0f;              // +0x3c
        Wire.Orientation.z = 0.0f;              // +0x40
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da49e0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da4ca0
    FPayloadCommonUpdateObject Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_UPDATEOBJECT) == 0x4cu, "FLPACKET_COMMON_UPDATEOBJECT is operator new(0x4c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_UPDATEOBJECT, Wire) == 0x20u, "COMMON_UPDATEOBJECT: payload at +0x20");

// FLPACKET_COMMON_CHATMSG -- id 0x10005, registered at 06d9ad19.
// Factory FUN_06d9b730: operator new(0x30) + ctor FUN_06d9b770. Vtable .rdata:06db4428 =
// { 06d9b7a0, 06da6420, 06da65d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_CHATMSG : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_CHATMSG() : FPacketBase(0x10005u) {
        Wire.Buffer = nullptr;                  // +0x2c
        Wire.Size = 0;                          // +0x28
        Wire.SenderId = 0;                      // +0x24
        Wire.TargetId = 0;                      // +0x20
    }
    // FUN_06d9b7c0
    ~FPacket_FLPACKET_COMMON_CHATMSG() override {
        if (Wire.Buffer != nullptr) {
            ::operator delete(Wire.Buffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da6420
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da65d0
    FPayloadCommonChatMsg Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_CHATMSG) == 0x30u, "FLPACKET_COMMON_CHATMSG is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_CHATMSG, Wire) == 0x20u, "COMMON_CHATMSG: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_CHATMSG, Wire.Buffer) == 0x2cu, "COMMON_CHATMSG: +0x2c");

// FLPACKET_COMMON_ACTIVATEEQUIP -- id 0x10008, registered at 06d9ad2f.
// Factory FUN_06d9b7f0: operator new(0x28) + ctor FUN_06d9b830. Vtable .rdata:06db4438 =
// { 06d9af50, 06da67a0, 06da68f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_ACTIVATEEQUIP : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_ACTIVATEEQUIP() : FPacketBase(0x10008u) {
        Wire.EquipId = 0;                       // +0x26
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da67a0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da68f0
    FPayloadCommonActivateEquip Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_ACTIVATEEQUIP) == 0x28u, "FLPACKET_COMMON_ACTIVATEEQUIP is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_ACTIVATEEQUIP, Wire) == 0x20u, "COMMON_ACTIVATEEQUIP: payload at +0x20");

// FLPACKET_COMMON_SETTARGET -- id 0x10004, registered at 06d9ad45.
// Factory FUN_06d9ba20: operator new(0x2c) + ctor FUN_06d9ba60. Vtable .rdata:06db4488 =
// { 06d9af50, 06da8b50, 06da8cd0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_SETTARGET : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_SETTARGET() : FPacketBase(0x10004u) {
        Wire.SubObjId = 0;                      // +0x28
        Wire.TargetSubObjId = 0;                // +0x2a
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da8b50
        return RpcEncodeWireDwordDwordWordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da8cd0
        return RpcDecodeWireDwordDwordWordWord(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonSetTarget Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_SETTARGET) == 0x2cu, "FLPACKET_COMMON_SETTARGET is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SETTARGET, Wire) == 0x20u, "COMMON_SETTARGET: payload at +0x20");

// FLPACKET_COMMON_ACTIVATECRUISE -- id 0x1000e, registered at 06d9ad5b.
// Factory FUN_06d9b860: operator new(0x28) + ctor FUN_06d9b8a0. Vtable .rdata:06db4448 =
// { 06d9af50, 06db1b60, 06da6a50, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_ACTIVATECRUISE : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_ACTIVATECRUISE() : FPacketBase(0x1000eu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db1b60
        return RpcEncodeWireDwordTwoBytes(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6a50
        return RpcDecodeWireDwordTwoBytes(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonActivateCruise Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_ACTIVATECRUISE) == 0x28u, "FLPACKET_COMMON_ACTIVATECRUISE is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_ACTIVATECRUISE, Wire) == 0x20u, "COMMON_ACTIVATECRUISE: payload at +0x20");

// FLPACKET_COMMON_ACTIVATETHRUSTERS -- id 0x10015, registered at 06d9ad71.
// Factory FUN_06d9b8d0: operator new(0x28) + ctor FUN_06d9b910. Vtable .rdata:06db4458 =
// { 06d9af50, 06dab0e0, 06daf230, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_ACTIVATETHRUSTERS : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_ACTIVATETHRUSTERS() : FPacketBase(0x10015u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dab0e0
        return RpcEncodeWireDwordByte(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06daf230
        return RpcDecodeWireDwordByte(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonActivateThrusters Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_ACTIVATETHRUSTERS) == 0x28u, "FLPACKET_COMMON_ACTIVATETHRUSTERS is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_ACTIVATETHRUSTERS, Wire) == 0x20u, "COMMON_ACTIVATETHRUSTERS: payload at +0x20");

// FLPACKET_COMMON_GOTRADELANE -- id 0x1000f, registered at 06d9ad87.
// Factory FUN_06d9b940: operator new(0x30) + ctor FUN_06d9b980. Vtable .rdata:06db4468 =
// { 06d9af50, 06da6b80, 06da6cf0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_GOTRADELANE : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_GOTRADELANE() : FPacketBase(0x1000fu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da6b80
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da6cf0
    FPayloadCommonGoTradelane Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_GOTRADELANE) == 0x30u, "FLPACKET_COMMON_GOTRADELANE is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_GOTRADELANE, Wire) == 0x20u, "COMMON_GOTRADELANE: payload at +0x20");

// FLPACKET_COMMON_STOPTRADELANE -- id 0x10010, registered at 06d9ad9d.
// Factory FUN_06d9b9b0: operator new(0x2c) + ctor FUN_06d9b9f0. Vtable .rdata:06db4478 =
// { 06d9af50, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_STOPTRADELANE : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_STOPTRADELANE() : FPacketBase(0x10010u) {
        Wire.ShipId = 0;                        // +0x20
        Wire.TradelaneRing1 = 0;                // +0x24
        Wire.TradelaneRing2 = 0;                // +0x28
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonStopTradelane Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_STOPTRADELANE) == 0x2cu, "FLPACKET_COMMON_STOPTRADELANE is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_STOPTRADELANE, Wire) == 0x20u, "COMMON_STOPTRADELANE: payload at +0x20");

// FLPACKET_COMMON_SET_WEAPON_GROUP -- id 0x10011, registered at 06d9adb3.
// Factory FUN_06d9b050: operator new(0x2c) + ctor FUN_06d9b090. Vtable .rdata:06db4398 =
// { 06d9b0c0, 06da6190, 06da5bc0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP() : FPacketBase(0x10011u) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.bOwnsBuffer = 0;                   // +0x28
    }
    // FUN_06d9b0e0
    ~FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP() override {
        // The byte at +0x28 says whether the packet owns the block at +0x20 -- the
        // IClient-side sender sets it on the CALLER's pointer, the IServerImpl-side one on
        // its own copy, the wire decoder on its own allocation.
        if (Wire.bOwnsBuffer != 0 &&
            Wire.Buffer != nullptr) {   // 06d9b0f0
            ::operator delete(Wire.Buffer);
            Wire.Buffer = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da5bc0
    FPayloadCommonBuffer Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP) == 0x2cu, "FLPACKET_COMMON_SET_WEAPON_GROUP is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP, Wire) == 0x20u, "COMMON_SET_WEAPON_GROUP: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP, Wire.bOwnsBuffer) == 0x28u, "COMMON_SET_WEAPON_GROUP: +0x28");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP, Wire.Buffer) == 0x20u, "COMMON_SET_WEAPON_GROUP: +0x20");

// FLPACKET_COMMON_PLAYER_TRADE -- id 0x10012, registered at 06d9adc9.
// Factory FUN_06d9ba90: operator new(0x44) + ctor FUN_06d9bad0. Vtable .rdata:06db4498 =
// { 06d9af50, 06da7030, 06da7430, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_PLAYER_TRADE : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_PLAYER_TRADE() : FPacketBase(0x10012u) {
        Wire.SubType = 0;                       // +0x20
        Wire.Reason = 0;                        // +0x24
        Wire.ShipId = 0;                        // +0x28
        Wire.EquipArchId = 0xffffffffu;         // +0x34
        Wire.EquipCount = 0;                    // +0x38
        Wire.EquipStatus = 0.0f;                // +0x3c
        Wire.Money = 0;                         // +0x2c
        Wire.bAccepted = 0;                     // +0x30
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da7030
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da7430
    FPayloadCommonPlayerTrade Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_PLAYER_TRADE) == 0x44u, "FLPACKET_COMMON_PLAYER_TRADE is operator new(0x44)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_PLAYER_TRADE, Wire) == 0x20u, "COMMON_PLAYER_TRADE: payload at +0x20");

// FLPACKET_COMMON_SET_VISITED_STATE -- id 0x10013, registered at 06d9addf.
// Factory FUN_06d9b110: operator new(0x2c) + ctor FUN_06d9b150. Vtable .rdata:06db43a8 =
// { 06d9b180, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_SET_VISITED_STATE : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_SET_VISITED_STATE() : FPacketBase(0x10013u) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.bOwnsBuffer = 0;                   // +0x28
    }
    // FUN_06d9b1a0
    ~FPacket_FLPACKET_COMMON_SET_VISITED_STATE() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (Wire.bOwnsBuffer != 0 &&
            Wire.Buffer != nullptr) {   // 06d9b1b0
            ::operator delete(Wire.Buffer);
            Wire.Buffer = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da62d0
        return RpcDecodeWireOwnedBuffer(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonBuffer Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_SET_VISITED_STATE) == 0x2cu, "FLPACKET_COMMON_SET_VISITED_STATE is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_VISITED_STATE, Wire) == 0x20u, "COMMON_SET_VISITED_STATE: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_VISITED_STATE, Wire.bOwnsBuffer) == 0x28u, "COMMON_SET_VISITED_STATE: +0x28");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_VISITED_STATE, Wire.Buffer) == 0x20u, "COMMON_SET_VISITED_STATE: +0x20");

// FLPACKET_COMMON_JETTISONCARGO -- id 0x10014, registered at 06d9adf5.
// Factory FUN_06d9bb10: operator new(0x2c) + ctor FUN_06d9bb50. Vtable .rdata:06db44a8 =
// { 06d9af50, 06dae620, 06da78d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_JETTISONCARGO : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_JETTISONCARGO() : FPacketBase(0x10014u) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.ItemId = 0;                        // +0x24
        Wire.Count = 0;                         // +0x28
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dae620
        return RpcEncodeWireDwordWordDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da78d0
        return RpcDecodeWireDwordWordDword(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonJettisonCargo Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_JETTISONCARGO) == 0x2cu, "FLPACKET_COMMON_JETTISONCARGO is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_JETTISONCARGO, Wire) == 0x20u, "COMMON_JETTISONCARGO: payload at +0x20");

// FLPACKET_COMMON_REQUEST_BEST_PATH -- id 0x10016, registered at 06d9ae0b.
// Factory FUN_06d9b1d0: operator new(0x2c) + ctor FUN_06d9b210. Vtable .rdata:06db43b8 =
// { 06d9b240, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH() : FPacketBase(0x10016u) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.bOwnsBuffer = 0;                   // +0x28
    }
    // FUN_06d9b260
    ~FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (Wire.bOwnsBuffer != 0 &&
            Wire.Buffer != nullptr) {   // 06d9b270
            ::operator delete(Wire.Buffer);
            Wire.Buffer = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da62d0
        return RpcDecodeWireOwnedBuffer(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonBuffer Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH) == 0x2cu, "FLPACKET_COMMON_REQUEST_BEST_PATH is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH, Wire) == 0x20u, "COMMON_REQUEST_BEST_PATH: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH, Wire.bOwnsBuffer) == 0x28u, "COMMON_REQUEST_BEST_PATH: +0x28");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH, Wire.Buffer) == 0x20u, "COMMON_REQUEST_BEST_PATH: +0x20");

// FLPACKET_COMMON_REQUEST_PLAYER_STATS -- id 0x10018, registered at 06d9ae21.
// Factory FUN_06d9b290: operator new(0x2c) + ctor FUN_06d9b2d0. Vtable .rdata:06db43c8 =
// { 06d9b300, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS() : FPacketBase(0x10018u) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.bOwnsBuffer = 0;                   // +0x28
    }
    // FUN_06d9b320
    ~FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (Wire.bOwnsBuffer != 0 &&
            Wire.Buffer != nullptr) {   // 06d9b330
            ::operator delete(Wire.Buffer);
            Wire.Buffer = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da62d0
        return RpcDecodeWireOwnedBuffer(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonBuffer Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS) == 0x2cu, "FLPACKET_COMMON_REQUEST_PLAYER_STATS is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS, Wire) == 0x20u, "COMMON_REQUEST_PLAYER_STATS: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS, Wire.bOwnsBuffer) == 0x28u, "COMMON_REQUEST_PLAYER_STATS: +0x28");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS, Wire.Buffer) == 0x20u, "COMMON_REQUEST_PLAYER_STATS: +0x20");

// FLPACKET_COMMON_POP_UP_DIALOG -- id 0x1001b, registered at 06d9ae37.
// Factory FUN_06d9b350: operator new(0x34) + ctor FUN_06d9b390. Vtable .rdata:06db43d8 =
// { 06d9b3d0, 06da5de0, 06da5fe0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_POP_UP_DIALOG : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_POP_UP_DIALOG() : FPacketBase(0x1001bu) {
        Wire.SizeA = 0;                         // +0x20
        Wire.BufferA = nullptr;                 // +0x24
        Wire.SizeB = 0;                         // +0x28
        Wire.BufferB = nullptr;                 // +0x2c
        Wire.Buttons = 0;                       // +0x30
    }
    // FUN_06d9b3f0
    ~FPacket_FLPACKET_COMMON_POP_UP_DIALOG() override {
        // Retail frees +0x2c FIRST and +0x24 second.
        if (Wire.BufferB != nullptr) {   // 06d9b3f9..06d9b403
            ::operator delete(Wire.BufferB);
        }
        if (Wire.BufferA != nullptr) {   // 06d9b409..06d9b413
            ::operator delete(Wire.BufferA);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da5de0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da5fe0
    FPayloadCommonPopUpDialog Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_POP_UP_DIALOG) == 0x34u, "FLPACKET_COMMON_POP_UP_DIALOG is operator new(0x34)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_POP_UP_DIALOG, Wire) == 0x20u, "COMMON_POP_UP_DIALOG: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_POP_UP_DIALOG, Wire.BufferB) == 0x2cu, "COMMON_POP_UP_DIALOG: +0x2c");
static_assert(offsetof(FPacket_FLPACKET_COMMON_POP_UP_DIALOG, Wire.BufferA) == 0x24u, "COMMON_POP_UP_DIALOG: +0x24");

// FLPACKET_COMMON_REQUEST_GROUP_POSITIONS -- id 0x10017, registered at 06d9ae4d.
// Factory FUN_06d9b430: operator new(0x2c) + ctor FUN_06d9b470. Vtable .rdata:06db43e8 =
// { 06d9b4a0, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS() : FPacketBase(0x10017u) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.bOwnsBuffer = 0;                   // +0x28
    }
    // FUN_06d9b4c0
    ~FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (Wire.bOwnsBuffer != 0 &&
            Wire.Buffer != nullptr) {   // 06d9b4d0
            ::operator delete(Wire.Buffer);
            Wire.Buffer = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da62d0
        return RpcDecodeWireOwnedBuffer(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonBuffer Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS) == 0x2cu, "FLPACKET_COMMON_REQUEST_GROUP_POSITIONS is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS, Wire) == 0x20u, "COMMON_REQUEST_GROUP_POSITIONS: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS, Wire.bOwnsBuffer) == 0x28u, "COMMON_REQUEST_GROUP_POSITIONS: +0x28");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS, Wire.Buffer) == 0x20u, "COMMON_REQUEST_GROUP_POSITIONS: +0x20");

// FLPACKET_COMMON_SET_MISSION_LOG -- id 0x10019, registered at 06d9ae63.
// Factory FUN_06d9b4f0: operator new(0x2c) + ctor FUN_06d9b530. Vtable .rdata:06db43f8 =
// { 06d9b560, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_SET_MISSION_LOG : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_SET_MISSION_LOG() : FPacketBase(0x10019u) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.bOwnsBuffer = 0;                   // +0x28
    }
    // FUN_06d9b580
    ~FPacket_FLPACKET_COMMON_SET_MISSION_LOG() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (Wire.bOwnsBuffer != 0 &&
            Wire.Buffer != nullptr) {   // 06d9b590
            ::operator delete(Wire.Buffer);
            Wire.Buffer = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da62d0
        return RpcDecodeWireOwnedBuffer(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonBuffer Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_SET_MISSION_LOG) == 0x2cu, "FLPACKET_COMMON_SET_MISSION_LOG is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_MISSION_LOG, Wire) == 0x20u, "COMMON_SET_MISSION_LOG: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_MISSION_LOG, Wire.bOwnsBuffer) == 0x28u, "COMMON_SET_MISSION_LOG: +0x28");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_MISSION_LOG, Wire.Buffer) == 0x20u, "COMMON_SET_MISSION_LOG: +0x20");

// FLPACKET_COMMON_REQUEST_RANK_LEVEL -- id 0x1001a, registered at 06d9ae79.
// Factory FUN_06d9b670: operator new(0x2c) + ctor FUN_06d9b6b0. Vtable .rdata:06db4418 =
// { 06d9b6e0, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL() : FPacketBase(0x1001au) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.bOwnsBuffer = 0;                   // +0x28
    }
    // FUN_06d9b700
    ~FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (Wire.bOwnsBuffer != 0 &&
            Wire.Buffer != nullptr) {   // 06d9b710
            ::operator delete(Wire.Buffer);
            Wire.Buffer = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da62d0
        return RpcDecodeWireOwnedBuffer(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonBuffer Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL) == 0x2cu, "FLPACKET_COMMON_REQUEST_RANK_LEVEL is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL, Wire) == 0x20u, "COMMON_REQUEST_RANK_LEVEL: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL, Wire.bOwnsBuffer) == 0x28u, "COMMON_REQUEST_RANK_LEVEL: +0x28");
static_assert(offsetof(FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL, Wire.Buffer) == 0x20u, "COMMON_REQUEST_RANK_LEVEL: +0x20");

// FLPACKET_COMMON_SET_INTERFACE_STATE -- id 0x1001c, registered at 06d9ae8f.
// Factory FUN_06d9b5b0: operator new(0x2c) + ctor FUN_06d9b5f0. Vtable .rdata:06db4408 =
// { 06d9b620, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE() : FPacketBase(0x1001cu) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.bOwnsBuffer = 0;                   // +0x28
    }
    // FUN_06d9b640
    ~FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (Wire.bOwnsBuffer != 0 &&
            Wire.Buffer != nullptr) {   // 06d9b650
            ::operator delete(Wire.Buffer);
            Wire.Buffer = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da62d0
        return RpcDecodeWireOwnedBuffer(*this, Buffer, Size, OutLength);
    }
    FPayloadCommonBuffer Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE) == 0x2cu, "FLPACKET_COMMON_SET_INTERFACE_STATE is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE, Wire) == 0x20u, "COMMON_SET_INTERFACE_STATE: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE, Wire.bOwnsBuffer) == 0x28u, "COMMON_SET_INTERFACE_STATE: +0x28");
static_assert(offsetof(FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE, Wire.Buffer) == 0x20u, "COMMON_SET_INTERFACE_STATE: +0x20");

// FLPACKET_COMMON_TRACTOROBJECTS -- id 0x1001d, registered at 06d9aea5.
// Factory FUN_06d9bb80: operator new(0x38) + ctor FUN_06d9bbc0. Vtable .rdata:06db44b8 =
// { 06d9bc00, 06da7a00, 06da7b90, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_TRACTOROBJECTS : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_TRACTOROBJECTS() : FPacketBase(0x1001du) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.TractorEquipId = 0;                // +0x24
        Wire.TargetIds.AllocPad = 0; // +0x28; byte copied from an uninitialised stack slot
        Wire.TargetIds.Begin = nullptr;         // +0x2c
        Wire.TargetIds.End = nullptr;           // +0x30
        Wire.TargetIds.Cap = nullptr;           // +0x34
    }
    // FUN_06d9bc20
    ~FPacket_FLPACKET_COMMON_TRACTOROBJECTS() override {
        // No test: free, then the three vector dwords cleared.
        ::operator delete(Wire.TargetIds.Begin);
        Wire.TargetIds.Begin = nullptr;
        Wire.TargetIds.End = nullptr;
        Wire.TargetIds.Cap = nullptr;
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da7a00
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da7b90
    FPayloadCommonTractorObjects Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_TRACTOROBJECTS) == 0x38u, "FLPACKET_COMMON_TRACTOROBJECTS is operator new(0x38)");
static_assert(offsetof(FPacket_FLPACKET_COMMON_TRACTOROBJECTS, Wire) == 0x20u, "COMMON_TRACTOROBJECTS: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_COMMON_TRACTOROBJECTS, Wire.TargetIds.Begin) == 0x2cu, "COMMON_TRACTOROBJECTS: +0x2c");
static_assert(offsetof(FPacket_FLPACKET_COMMON_TRACTOROBJECTS, Wire.TargetIds.End) == 0x30u, "COMMON_TRACTOROBJECTS: +0x30");
static_assert(offsetof(FPacket_FLPACKET_COMMON_TRACTOROBJECTS, Wire.TargetIds.Cap) == 0x34u, "COMMON_TRACTOROBJECTS: +0x34");

// ===========================================================================
// Channel 2 -- the classes registered by FUN_06d9bc80, in its registration order.
// ===========================================================================

// FLPACKET_SERVER_CONNECTRESPONSE -- id 0x20001, registered at 06d9bc8d.
// Factory FUN_06d9c7b0: operator new(0x24) + ctor FUN_06d9c7f0. Vtable .rdata:06db4fec =
// { 06d9af50, 06db0310, 06db03f0, 06da8240 }.
class FPacket_FLPACKET_SERVER_CONNECTRESPONSE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CONNECTRESPONSE() : FPacketBase(0x20001u) {
        Wire.ClientId = 0;                      // +0x20
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    void Dump(std::ostream& Out) const override;   // 06da8240
    FPayloadServerConnectResponse Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CONNECTRESPONSE) == 0x24u, "FLPACKET_SERVER_CONNECTRESPONSE is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_CONNECTRESPONSE, Wire) == 0x20u, "SERVER_CONNECTRESPONSE: payload at +0x20");

// FLPACKET_SERVER_LOGINRESPONSE -- id 0x20002, registered at 06d9bca3.
// Factory FUN_06d9c820: operator new(0x24) + ctor FUN_06d9c860. Vtable .rdata:06db4ffc =
// { 06d9af50, 06da82e0, 06da83c0, 06da84a0 }.
class FPacket_FLPACKET_SERVER_LOGINRESPONSE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_LOGINRESPONSE() : FPacketBase(0x20002u) {
        Wire.Field_0x00 = 0;                    // +0x20
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da82e0
        return RpcEncodeWireByte(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da83c0
        return RpcDecodeWireByte(*this, Buffer, Size, OutLength);
    }
    void Dump(std::ostream& Out) const override;   // 06da84a0
    FPayloadServerByte Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_LOGINRESPONSE) == 0x24u, "FLPACKET_SERVER_LOGINRESPONSE is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_LOGINRESPONSE, Wire) == 0x20u, "SERVER_LOGINRESPONSE: payload at +0x20");

// FLPACKET_SERVER_DAMAGEOBJECT -- id 0x20005, registered at 06d9bcb9.
// Factory FUN_06d9c890: operator new(0x40) + ctor FUN_06d9c8d0. Vtable .rdata:06db3cb0 =
// { 06d909b0, 06d90480, 06d906c0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_DAMAGEOBJECT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_DAMAGEOBJECT() : FPacketBase(0x20005u) {
    }
    // FUN_06d90230
    ~FPacket_FLPACKET_SERVER_DAMAGEOBJECT() override {
        // Retail inlines the DamageList destructor here; the payload holds Common.dll's
        // DamageList itself, whose destructor runs after this body.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d90480
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d906c0
    FPayloadServerDamageObject Wire;   // +0x20; Field_0x00 is left alone, as retail's ctor does
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_DAMAGEOBJECT) == 0x20u + sizeof(FPayloadServerDamageObject),
              "FLPACKET_SERVER_DAMAGEOBJECT: the payload member starts at +0x20");

// FLPACKET_SERVER_DESTROYOBJECT -- id 0x20006, registered at 06d9bccf.
// Factory FUN_06d9cac0: operator new(0x28) + ctor FUN_06d9cb00. Vtable .rdata:06db503c =
// { 06d9af50, 06dab0e0, 06daf230, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_DESTROYOBJECT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_DESTROYOBJECT() : FPacketBase(0x20006u) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.DestroyType = 0;                   // +0x24
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dab0e0
        return RpcEncodeWireDwordByte(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06daf230
        return RpcDecodeWireDwordByte(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDestroyObject Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_DESTROYOBJECT) == 0x28u, "FLPACKET_SERVER_DESTROYOBJECT is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_DESTROYOBJECT, Wire) == 0x20u, "SERVER_DESTROYOBJECT: payload at +0x20");

// FLPACKET_SERVER_CREATESHIP -- id 0x20004, registered at 06d9bce5.
// Factory FUN_06d9cc20: operator new(0xec) + ctor FUN_06d9cc60. Vtable .rdata:06db3fc4 =
// { 06d98640, 06d97d20, 06d98250, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CREATESHIP : public FRpcCreateObjectPacket {
public:
    // FUN_06d9cc60. Wire.Common is built first, by FRpcCreateObjectWire's constructors
    // (the head, then COMMON.DLL's std::list<CollisionGroupDesc> ctor at +0x74 and
    // DamageList ctor at +0x80); the body is the tail.
    FPacket_FLPACKET_SERVER_CREATESHIP() : FRpcCreateObjectPacket(0x20004u) {
        Wire.bUsingTradelane = 0;         // byte +0x9c
        Wire.VelXBits = 0;                // +0xa0
        Wire.VelY = 0.0f;                 // +0xa4 (integer 0 == +0.0f)
        Wire.VelZ = 0.0f;                 // +0xa8
        Wire.AngVelXBits = 0;             // +0xac
        Wire.AngVelY = 0.0f;              // +0xb0
        Wire.AngVelZ = 0.0f;              // +0xb4
        Wire.Throttle = 0.0f;             // +0xb8
        Wire.OwnerPlayer = 0;             // +0xbc
        Wire.GroupNameId = 0;             // word +0xc0
        Wire.ReputationRank = 0;          // byte +0xc2
        Wire.GroupId = 0;                 // word +0xc4
        Wire.NameASize = 0;               // +0xc8
        Wire.NameABuffer = nullptr;       // +0xcc
        Wire.NameBSize = 0;               // +0xd0
        Wire.NameBBuffer = nullptr;       // +0xd4
        Wire.DisplayNameLength = 0;       // +0xd8
        Wire.DisplayName = nullptr;       // +0xdc
        Wire.Affiliation = 0;             // +0xe0
        Wire.Attitude = 0.0f;             // +0xe4
        Wire.bMarkedBy = 0;               // byte +0xe8
    }
    // FUN_06d97b90
    ~FPacket_FLPACKET_SERVER_CREATESHIP() override {
        if (Wire.DisplayName != nullptr) {                     // 06d97bab..06d97bc3 (+0xdc)
            ::operator delete(Wire.DisplayName);
            Wire.DisplayName = nullptr;
        }
        if (Wire.NameBBuffer != nullptr) {                     // 06d97bd1..06d97bdc (+0xd4)
            ::operator delete(Wire.NameBBuffer);
        }
        if (Wire.NameABuffer != nullptr) {                     // 06d97be4..06d97bef (+0xcc)
            ::operator delete(Wire.NameABuffer);
        }
        // 06d97bf9 stores the abstract base vptr 06db3fb4; Wire.Common's destructor and
        // ~FPacketBase follow.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d97d20
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d98250
    FPayloadServerCreateShip Wire;   // +0x20; retail's class is operator new(0xec)
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CREATESHIP) == 0x20u + sizeof(FPayloadServerCreateShip),
              "FLPACKET_SERVER_CREATESHIP: the payload member starts at +0x20");

// FLPACKET_SERVER_REQUEST_RETURNED -- id 0x20044, registered at 06d9bcfb.
// Factory FUN_06d9d3d0: operator new(0x28) + ctor FUN_06d9d410. Vtable .rdata:06db50cc =
// { 06d9af50, 06daa820, 06daa990, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_REQUEST_RETURNED : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_REQUEST_RETURNED() : FPacketBase(0x20044u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daa820
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daa990
    FPayloadServerRequestReturned Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REQUEST_RETURNED) == 0x28u, "FLPACKET_SERVER_REQUEST_RETURNED is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_REQUEST_RETURNED, Wire) == 0x20u, "SERVER_REQUEST_RETURNED: payload at +0x20");

// FLPACKET_SERVER_CHARACTERINFO -- id 0x20003, registered at 06d9bd11.
// Factory FUN_06d9d4b0: operator new(0x30) + ctor FUN_06d9d4f0. Vtable .rdata:06db3dc4 =
// { 06d95b70, 06d949d0, 06d934d0, 06d93310 }.
class FPacket_FLPACKET_SERVER_CHARACTERINFO : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CHARACTERINFO() : FPacketBase(0x20003u) {
        RpcPacketBuildListSentinel(&Wire.Records, 0x0cu);   // operator new(0xc), Head
    }
    // FUN_06d93350
    ~FPacket_FLPACKET_SERVER_CHARACTERINFO() override {
        // Every record first (two id blocks, the collision list, the equipment handle, the
        // two strings, the record), then the nodes through FUN_06d901f0 and the sentinel.
        Wire.Records.Release();
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d949d0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d934d0
    void Dump(std::ostream& Out) const override;   // 06d93310
    FPayloadServerCharacterInfo Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CHARACTERINFO) == 0x30u, "FLPACKET_SERVER_CHARACTERINFO is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_CHARACTERINFO, Wire) == 0x20u, "SERVER_CHARACTERINFO: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_CHARACTERINFO, Wire.Records) == 0x24u, "SERVER_CHARACTERINFO: +0x24");

// FLPACKET_SERVER_LAUNCH -- id 0x20007, registered at 06d9bd27.
// Factory FUN_06d9d570: operator new(0x48) + ctor FUN_06d9d5b0. Vtable .rdata:06db50ec =
// { 06d9af50, 06daab40, 06daad40, 06daaee0 }.
class FPacket_FLPACKET_SERVER_LAUNCH : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_LAUNCH() : FPacketBase(0x20007u) {
        Wire.Position.x = 0.0f;                 // +0x2c
        Wire.Position.y = 0.0f;                 // +0x30
        Wire.Position.z = 0.0f;                 // +0x34
        Wire.Orientation.w = 1.0f;              // +0x38
        Wire.Orientation.x = 0.0f;              // +0x3c
        Wire.Orientation.y = 0.0f;              // +0x40
        Wire.Orientation.z = 0.0f;              // +0x44
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daab40
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daad40
    void Dump(std::ostream& Out) const override;   // 06daaee0
    FPayloadServerLaunch Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_LAUNCH) == 0x48u, "FLPACKET_SERVER_LAUNCH is operator new(0x48)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_LAUNCH, Wire) == 0x20u, "SERVER_LAUNCH: payload at +0x20");

// FLPACKET_SERVER_CHARSELECTVERIFIED -- id 0x20008, registered at 06d9bd3d.
// Factory FUN_06d9db50: operator new(0x30) + ctor FUN_06d9db90. Vtable .rdata:06db517c =
// { 06d9af50, 06dabdf0, 06dabf10, 06dac020 }.
class FPacket_FLPACKET_SERVER_CHARSELECTVERIFIED : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CHARSELECTVERIFIED() : FPacketBase(0x20008u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dabdf0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dabf10
    void Dump(std::ostream& Out) const override;   // 06dac020
    FPayloadServerCharSelectVerified Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CHARSELECTVERIFIED) == 0x30u, "FLPACKET_SERVER_CHARSELECTVERIFIED is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_CHARSELECTVERIFIED, Wire) == 0x20u, "SERVER_CHARSELECTVERIFIED: payload at +0x20");

// FLPACKET_SERVER_ACTIVATEOBJECT -- id 0x2000a, registered at 06d9bd53.
// Factory FUN_06d9ca50: operator new(0x2c) + ctor FUN_06d9ca90. Vtable .rdata:06db502c =
// { 06d9af50, 06da8e30, 06da8f80, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_ACTIVATEOBJECT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_ACTIVATEOBJECT() : FPacketBase(0x2000au) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da8e30
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da8f80
    FPayloadServerActivateObject Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_ACTIVATEOBJECT) == 0x2cu, "FLPACKET_SERVER_ACTIVATEOBJECT is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_ACTIVATEOBJECT, Wire) == 0x20u, "SERVER_ACTIVATEOBJECT: payload at +0x20");

// FLPACKET_SERVER_LAND -- id 0x2000b, registered at 06d9bd69.
// Factory FUN_06d9d440: operator new(0x2c) + ctor FUN_06d9d480. Vtable .rdata:06db50dc =
// { 06d9af50, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_LAND : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_LAND() : FPacketBase(0x2000bu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    FServerLandInfo Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_LAND) == 0x2cu, "FLPACKET_SERVER_LAND is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_LAND, Wire) == 0x20u, "SERVER_LAND: payload at +0x20");

// FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST -- id 0x2000e, registered at 06d9bd7f.
// Factory FUN_06d9dd10: operator new(0x24) + ctor FUN_06d9dd50. Vtable .rdata:06db51ac =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST() : FPacketBase(0x2000eu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST) == 0x24u, "FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST, Wire) == 0x20u, "SERVER_GFCOMPLETENEWSBROADCASTLIST: payload at +0x20");

// FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST -- id 0x20010, registered at 06d9bd95.
// Factory FUN_06d9dca0: operator new(0x24) + ctor FUN_06d9dce0. Vtable .rdata:06db519c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST() : FPacketBase(0x20010u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST) == 0x24u, "FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST, Wire) == 0x20u, "SERVER_GFCOMPLETEMISSIONCOMPUTERLIST: payload at +0x20");

// FLPACKET_SERVER_GFCOMPLETECHARLIST -- id 0x2000f, registered at 06d9bdab.
// Factory FUN_06d9dc30: operator new(0x24) + ctor FUN_06d9dc70. Vtable .rdata:06db518c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFCOMPLETECHARLIST : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFCOMPLETECHARLIST() : FPacketBase(0x2000fu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFCOMPLETECHARLIST) == 0x24u, "FLPACKET_SERVER_GFCOMPLETECHARLIST is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFCOMPLETECHARLIST, Wire) == 0x20u, "SERVER_GFCOMPLETECHARLIST: payload at +0x20");

// FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST -- id 0x20011, registered at 06d9bdc1.
// Factory FUN_06d9dd80: operator new(0x24) + ctor FUN_06d9ddc0. Vtable .rdata:06db51bc =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST() : FPacketBase(0x20011u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST) == 0x24u, "FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST, Wire) == 0x20u, "SERVER_GFCOMPLETESCRIPTBEHAVIORLIST: payload at +0x20");

// FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST -- id 0x20013, registered at 06d9bdd7.
// Factory FUN_06d9ddf0: operator new(0x24) + ctor FUN_06d9de30. Vtable .rdata:06db51cc =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST() : FPacketBase(0x20013u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST) == 0x24u, "FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST, Wire) == 0x20u, "SERVER_GFCOMPLETEAMBIENTSCRIPTLIST: payload at +0x20");

// FLPACKET_SERVER_SYSTEM_SWITCH_OUT -- id 0x20021, registered at 06d9bded.
// Factory FUN_06d9cb30: operator new(0x28) + ctor FUN_06d9cb70. Vtable .rdata:06db504c =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_OUT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_OUT() : FPacketBase(0x20021u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FServerSystemSwitchOutInfo Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_OUT) == 0x28u, "FLPACKET_SERVER_SYSTEM_SWITCH_OUT is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_OUT, Wire) == 0x20u, "SERVER_SYSTEM_SWITCH_OUT: payload at +0x20");

// FLPACKET_SERVER_SYSTEM_SWITCH_IN -- id 0x20022, registered at 06d9be03.
// Factory FUN_06d9cba0: operator new(0x44) + ctor FUN_06d9cbe0. Vtable .rdata:06db505c =
// { 06d9af50, 06da9260, 06da9430, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_IN : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_IN() : FPacketBase(0x20022u) {
        Wire.Position.x = 0.0f;                 // +0x28
        Wire.Position.y = 0.0f;                 // +0x2c
        Wire.Position.z = 0.0f;                 // +0x30
        Wire.Orientation.w = 1.0f;              // +0x34
        Wire.Orientation.x = 0.0f;              // +0x38
        Wire.Orientation.y = 0.0f;              // +0x3c
        Wire.Orientation.z = 0.0f;              // +0x40
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da9260
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da9430
    FPayloadServerSystemSwitchIn Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_IN) == 0x44u, "FLPACKET_SERVER_SYSTEM_SWITCH_IN is operator new(0x44)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_IN, Wire) == 0x20u, "SERVER_SYSTEM_SWITCH_IN: payload at +0x20");

// FLPACKET_SERVER_SETSHIPARCH -- id 0x20023, registered at 06d9be19.
// Factory FUN_06d9c740: operator new(0x24) + ctor FUN_06d9c780. Vtable .rdata:06db4fdc =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETSHIPARCH : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETSHIPARCH() : FPacketBase(0x20023u) {
        Wire.Field_0x00 = 0;                    // +0x20
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETSHIPARCH) == 0x24u, "FLPACKET_SERVER_SETSHIPARCH is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETSHIPARCH, Wire) == 0x20u, "SERVER_SETSHIPARCH: payload at +0x20");

// FLPACKET_SERVER_SETEQUIPMENT -- id 0x20024, registered at 06d9be2f.
// Factory FUN_06d9c660: operator new(0x24) + ctor FUN_06d9c6a0. Vtable .rdata:06db3cf0 =
// { 06d91320, 06d916a0, 06d95bf0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETEQUIPMENT : public FRpcEquipListPacket {
public:
    FPacket_FLPACKET_SERVER_SETEQUIPMENT() : FRpcEquipListPacket(0x20024u) {
        EquipHandle = nullptr;                  // +0x20
    }
    // FUN_06d91310
    ~FPacket_FLPACKET_SERVER_SETEQUIPMENT() override {
        // Own vptr store, then JMP FUN_06da21d0 -- ~FRpcEquipListPacket.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06d916a0
        return RpcEncodeWireEquipList(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95bf0
        return RpcDecodeWireEquipList(*this, Buffer, Size, OutLength);
    }
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETEQUIPMENT) == 0x24u, "FLPACKET_SERVER_SETEQUIPMENT is operator new(0x24)");

// FLPACKET_SERVER_SETCARGO -- id 0x20025, registered at 06d9be45.
// Factory FUN_06d9c6d0: operator new(0x24) + ctor FUN_06d9c710. Vtable .rdata:06db3d10 =
// { 06d91680, 06d916a0, 06d95bf0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETCARGO : public FRpcEquipListPacket {
public:
    FPacket_FLPACKET_SERVER_SETCARGO() : FRpcEquipListPacket(0x20025u) {
        EquipHandle = nullptr;                  // +0x20
    }
    // FUN_06d91670
    ~FPacket_FLPACKET_SERVER_SETCARGO() override {
        // Own vptr store, then JMP FUN_06da21d0 -- ~FRpcEquipListPacket.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06d916a0
        return RpcEncodeWireEquipList(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95bf0
        return RpcDecodeWireEquipList(*this, Buffer, Size, OutLength);
    }
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETCARGO) == 0x24u, "FLPACKET_SERVER_SETCARGO is operator new(0x24)");

// FLPACKET_SERVER_REQUESTCREATESHIPRESP -- id 0x20027, registered at 06d9be5b.
// Factory FUN_06d9c5f0: operator new(0x28) + ctor FUN_06d9c630. Vtable .rdata:06db4fcc =
// { 06d9af50, 06da7fe0, 06da8100, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_REQUESTCREATESHIPRESP : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_REQUESTCREATESHIPRESP() : FPacketBase(0x20027u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da7fe0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da8100
    FPayloadServerRequestCreateShipResp Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REQUESTCREATESHIPRESP) == 0x28u, "FLPACKET_SERVER_REQUESTCREATESHIPRESP is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_REQUESTCREATESHIPRESP, Wire) == 0x20u, "SERVER_REQUESTCREATESHIPRESP: payload at +0x20");

// FLPACKET_SERVER_SETSTARTROOM -- id 0x2000d, registered at 06d9be71.
// Factory FUN_06d9de60: operator new(0x28) + ctor FUN_06d9dea0. Vtable .rdata:06db51dc =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETSTARTROOM : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETSTARTROOM() : FPacketBase(0x2000du) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDwordPair Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETSTARTROOM) == 0x28u, "FLPACKET_SERVER_SETSTARTROOM is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETSTARTROOM, Wire) == 0x20u, "SERVER_SETSTARTROOM: payload at +0x20");

// FLPACKET_SERVER_GFDESTROYCHARACTER -- id 0x20015, registered at 06d9be87.
// Factory FUN_06d9ded0: operator new(0x28) + ctor FUN_06d9df10. Vtable .rdata:06db51ec =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFDESTROYCHARACTER : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFDESTROYCHARACTER() : FPacketBase(0x20015u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDwordPair Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFDESTROYCHARACTER) == 0x28u, "FLPACKET_SERVER_GFDESTROYCHARACTER is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFDESTROYCHARACTER, Wire) == 0x20u, "SERVER_GFDESTROYCHARACTER: payload at +0x20");

// FLPACKET_SERVER_GFDESTROYNEWSBROADCAST -- id 0x20014, registered at 06d9be9d.
// Factory FUN_06d9dfb0: operator new(0x28) + ctor FUN_06d9dff0. Vtable .rdata:06db520c =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFDESTROYNEWSBROADCAST : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFDESTROYNEWSBROADCAST() : FPacketBase(0x20014u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDwordPair Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFDESTROYNEWSBROADCAST) == 0x28u, "FLPACKET_SERVER_GFDESTROYNEWSBROADCAST is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFDESTROYNEWSBROADCAST, Wire) == 0x20u, "SERVER_GFDESTROYNEWSBROADCAST: payload at +0x20");

// FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER -- id 0x20016, registered at 06d9beb3.
// Factory FUN_06d9df40: operator new(0x28) + ctor FUN_06d9df80. Vtable .rdata:06db51fc =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER() : FPacketBase(0x20016u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDwordPair Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER) == 0x28u, "FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER, Wire) == 0x20u, "SERVER_GFDESTROYMISSIONCOMPUTER: payload at +0x20");

// FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR -- id 0x20017, registered at 06d9bec9.
// Factory FUN_06d9e020: operator new(0x28) + ctor FUN_06d9e060. Vtable .rdata:06db521c =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR() : FPacketBase(0x20017u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDwordPair Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR) == 0x28u, "FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR, Wire) == 0x20u, "SERVER_GFDESTROYSCRIPTBEHAVIOR: payload at +0x20");

// FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT -- id 0x20019, registered at 06d9bedf.
// Factory FUN_06d9e090: operator new(0x28) + ctor FUN_06d9e0d0. Vtable .rdata:06db522c =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT() : FPacketBase(0x20019u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDwordPair Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT) == 0x28u, "FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT, Wire) == 0x20u, "SERVER_GFDESTROYAMBIENTSCRIPT: payload at +0x20");

// FLPACKET_SERVER_GFSCRIPTBEHAVIOR -- id 0x2001a, registered at 06d9bef5.
// Factory FUN_06d9e100: operator new(0x28) + ctor FUN_06d9e140. Vtable .rdata:06db3f24 =
// { 06d976e0, 06d97570, 06d978f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR() : FPacketBase(0x2001au) {
        Wire.Size = 0;                          // +0x20
        Wire.Buffer = nullptr;                  // +0x24
    }
    // FUN_06d97540
    ~FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR() override {
        if (Wire.Buffer != nullptr) {   // 06d97543..06d97551
            ::operator delete(Wire.Buffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06d97570
        return RpcEncodeWireBlobSizeFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d978f0
        return RpcDecodeWireBlobSizeFirst(*this, Buffer, Size, OutLength);
    }
    FPayloadServerBlob Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR) == 0x28u, "FLPACKET_SERVER_GFSCRIPTBEHAVIOR is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR, Wire) == 0x20u, "SERVER_GFSCRIPTBEHAVIOR: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR, Wire.Buffer) == 0x24u, "SERVER_GFSCRIPTBEHAVIOR: +0x24");

// FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT -- id 0x2001f, registered at 06d9bf0b.
// Factory FUN_06d9e250: operator new(0x28) + ctor FUN_06d9e290. Vtable .rdata:06db3f44 =
// { 06d978a0, 06da6190, 06d97730, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT() : FPacketBase(0x2001fu) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.Size = 0;                          // +0x24
    }
    // FUN_06d97700
    ~FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT() override {
        if (Wire.Buffer != nullptr) {   // 06d97703..06d97711
            ::operator delete(Wire.Buffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d97730
    FPayloadServerBlobReversed Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT) == 0x28u, "FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT, Wire) == 0x20u, "SERVER_GFUPDATEAMBIENTSCRIPT: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT, Wire.Buffer) == 0x20u, "SERVER_GFUPDATEAMBIENTSCRIPT: +0x20");

// FLPACKET_SERVER_MISSIONSAVEA -- id 0x2005b, registered at 06d9bf21.
// Factory FUN_06d9f090: operator new(0x24) + ctor FUN_06d9f0d0. Vtable .rdata:06db536c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_MISSIONSAVEA : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_MISSIONSAVEA() : FPacketBase(0x2005bu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_MISSIONSAVEA) == 0x24u, "FLPACKET_SERVER_MISSIONSAVEA is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_MISSIONSAVEA, Wire) == 0x20u, "SERVER_MISSIONSAVEA: payload at +0x20");

// FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER -- id 0x2001d, registered at 06d9bf37.
// Factory FUN_06d9e170: operator new(0x28) + ctor FUN_06d9e1b0. Vtable .rdata:06db3f64 =
// { 06d97a90, 06d97570, 06d978f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER() : FPacketBase(0x2001du) {
        Wire.Size = 0;                          // +0x20
        Wire.Buffer = nullptr;                  // +0x24
    }
    // FUN_06d978c0
    ~FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER() override {
        if (Wire.Buffer != nullptr) {   // 06d978c3..06d978d1
            ::operator delete(Wire.Buffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06d97570
        return RpcEncodeWireBlobSizeFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d978f0
        return RpcDecodeWireBlobSizeFirst(*this, Buffer, Size, OutLength);
    }
    FPayloadServerBlob Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER) == 0x28u, "FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER, Wire) == 0x20u, "SERVER_GFUPDATEMISSIONCOMPUTER: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER, Wire.Buffer) == 0x24u, "SERVER_GFUPDATEMISSIONCOMPUTER: +0x24");

// FLPACKET_SERVER_GFUPDATENEWSBROADCAST -- id 0x2001e, registered at 06d9bf4d.
// Factory FUN_06d9e1e0: operator new(0x28) + ctor FUN_06d9e220. Vtable .rdata:06db3f94 =
// { 06d97b70, 06d97570, 06d978f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST() : FPacketBase(0x2001eu) {
        Wire.Size = 0;                          // +0x20
        Wire.Buffer = nullptr;                  // +0x24
    }
    // FUN_06d97ad0
    ~FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST() override {
        if (Wire.Buffer != nullptr) {   // 06d97ad3..06d97ae1
            ::operator delete(Wire.Buffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06d97570
        return RpcEncodeWireBlobSizeFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d978f0
        return RpcDecodeWireBlobSizeFirst(*this, Buffer, Size, OutLength);
    }
    FPayloadServerBlob Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST) == 0x28u, "FLPACKET_SERVER_GFUPDATENEWSBROADCAST is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST, Wire) == 0x20u, "SERVER_GFUPDATENEWSBROADCAST: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST, Wire.Buffer) == 0x24u, "SERVER_GFUPDATENEWSBROADCAST: +0x24");

// FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE -- id 0x20020, registered at 06d9bf63.
// Factory FUN_06d9e2c0: operator new(0x28) + ctor FUN_06d9e300. Vtable .rdata:06db3f74 =
// { 06d97ab0, 06d97570, 06d978f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE() : FPacketBase(0x20020u) {
        Wire.Size = 0;                          // +0x20
        Wire.Buffer = nullptr;                  // +0x24
    }
    // FUN_06d97a30
    ~FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE() override {
        if (Wire.Buffer != nullptr) {   // 06d97a33..06d97a41
            ::operator delete(Wire.Buffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06d97570
        return RpcEncodeWireBlobSizeFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d978f0
        return RpcDecodeWireBlobSizeFirst(*this, Buffer, Size, OutLength);
    }
    FPayloadServerBlob Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE) == 0x28u, "FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE, Wire) == 0x20u, "SERVER_GFMISSIONVENDORACCEPTANCE: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE, Wire.Buffer) == 0x24u, "SERVER_GFMISSIONVENDORACCEPTANCE: +0x24");

// FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY -- id 0x2005a, registered at 06d9bf79.
// Factory FUN_06d9e330: operator new(0x24) + ctor FUN_06d9e370. Vtable .rdata:06db523c =
// { 06d9af50, 06da82e0, 06da83c0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY() : FPacketBase(0x2005au) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da82e0
        return RpcEncodeWireByte(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da83c0
        return RpcDecodeWireByte(*this, Buffer, Size, OutLength);
    }
    FPayloadServerByte Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY) == 0x24u, "FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY, Wire) == 0x20u, "SERVER_GFMISSIONVENDORWHYEMPTY: payload at +0x20");

// FLPACKET_SERVER_GFUPDATECHAR -- id 0x20026, registered at 06d9bf8f.
// Factory FUN_06d9dbc0: operator new(0x28) + ctor FUN_06d9dc00. Vtable .rdata:06db3f04 =
// { 06d97520, 06d97570, 06d978f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFUPDATECHAR : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFUPDATECHAR() : FPacketBase(0x20026u) {
        Wire.Size = 0;                          // +0x20
        Wire.Buffer = nullptr;                  // +0x24
    }
    // FUN_06d974c0
    ~FPacket_FLPACKET_SERVER_GFUPDATECHAR() override {
        if (Wire.Buffer != nullptr) {   // 06d974c3..06d974d1
            ::operator delete(Wire.Buffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06d97570
        return RpcEncodeWireBlobSizeFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d978f0
        return RpcDecodeWireBlobSizeFirst(*this, Buffer, Size, OutLength);
    }
    FPayloadServerBlob Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFUPDATECHAR) == 0x28u, "FLPACKET_SERVER_GFUPDATECHAR is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFUPDATECHAR, Wire) == 0x20u, "SERVER_GFUPDATECHAR: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_GFUPDATECHAR, Wire.Buffer) == 0x24u, "SERVER_GFUPDATECHAR: +0x24");

// FLPACKET_SERVER_CREATELOOT -- id 0x20028, registered at 06d9bfa5.
// Factory FUN_06d9d100: operator new(0x34) + ctor FUN_06d9d140. Vtable .rdata:06db50ac =
// { 06d9d1d0, 06d928c0, 06d92d20, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CREATELOOT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CREATELOOT() : FPacketBase(0x20028u) {
        Wire.OwnerId = 0;                       // +0x20
        Wire.Kind = 0;                          // +0x24
        RpcPacketBuildListSentinel(&Wire.Items, 0x50u);   // operator new(0x50), Head
    }
    // FUN_06d9d1f0
    ~FPacket_FLPACKET_SERVER_CREATELOOT() override {
        // 06d9d1f5..06d9d238: the list at +0x28 (Head +0x2c, Size +0x30).
        RpcPacketDestroyEmbeddedList(&Wire.Items);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d928c0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d92d20

    // The list at Wire.Items (retail accessor FUN_06d931b0); bodies in rpc_containers.cpp.
    // The constructor's share, for a list not built yet: dummy node pointing at itself,
    // Size 0.
    void InitLootList();
    // FUN_06d93160 -- one operator new(0x50) node at the tail, value copied by
    // FUN_06d93280, then ++Size (packet+0x30).
    void PushBackLoot(const FRpcLootWireRecord& Record);
    // Frees the node chain and the dummy node. Retail does it from the packet destructor.
    void ReleaseLootList();
    FPayloadServerCreateLoot Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CREATELOOT) == 0x34u, "FLPACKET_SERVER_CREATELOOT is operator new(0x34)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_CREATELOOT, Wire) == 0x20u, "SERVER_CREATELOOT: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_CREATELOOT, Wire.Items) == 0x28u, "SERVER_CREATELOOT: +0x28");

// FLPACKET_SERVER_SETREPUTATION -- id 0x20029, registered at 06d9bfbb.
// Factory FUN_06d9d250: operator new(0x68) + ctor FUN_06d9d290. Vtable .rdata:06db3d50 =
// { 06d91eb0, 06d91ed0, 06d92350, 06d92860 }.
class FPacket_FLPACKET_SERVER_SETREPUTATION : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETREPUTATION() : FPacketBase(0x20029u) {
        Wire.NameASize = 0;                     // +0x20
        Wire.NameABuffer = nullptr;             // +0x24
        Wire.NameBSize = 0;                     // +0x28
        Wire.NameBBuffer = nullptr;             // +0x2c
        Wire.MaskNames = 2u;                    // +0x38
        Wire.Attitude = 0.0f;                   // +0x30
        Wire.MaskAffiliation = 1u;              // +0x34
        Wire.MaskDisplayName = 4u;              // +0x3c
        Wire.MaskRelationsByte = 8u;            // +0x40
        Wire.MaskRelationsWord = 16u;           // +0x44
        RpcPacketBuildListSentinel(&Wire.Relations, 0x0cu);   // operator new(0xc), Head
        Wire.Flags = 0;                         // +0x54
        Wire.VibeHandle = 0;                    // +0x58
        Wire.DisplayNameLength = 0;             // +0x60
        Wire.DisplayName = nullptr;             // +0x64
        Wire.Affiliation = 0xffffffffu;         // +0x5c
    }
    // FUN_06d91dd0
    ~FPacket_FLPACKET_SERVER_SETREPUTATION() override {
        // 06d91df6..06d91e1c: free every node's value first, testing each for null.
        FRpcRelationNode* Sentinel = Wire.Relations.Head;
        for (FRpcRelationNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
            if (Node->Value != nullptr) {
                ::operator delete(Node->Value);
            }
        }
        if (Wire.DisplayName != nullptr) {   // 06d91e1e..06d91e26
            ::operator delete(Wire.DisplayName);
        }
        RpcPacketDestroyEmbeddedList(&Wire.Relations);   // FUN_06d901f0
        if (Wire.NameBBuffer != nullptr) {   // 06d91e69..06d91e73
            ::operator delete(Wire.NameBBuffer);
        }
        if (Wire.NameABuffer != nullptr) {   // 06d91e7b..06d91e83
            ::operator delete(Wire.NameABuffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d91ed0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d92350
    void Dump(std::ostream& Out) const override;   // 06d92860
    FPayloadServerSetReputation Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETREPUTATION) == 0x68u, "FLPACKET_SERVER_SETREPUTATION is operator new(0x68)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETREPUTATION, Wire) == 0x20u, "SERVER_SETREPUTATION: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETREPUTATION, Wire.Relations) == 0x48u, "SERVER_SETREPUTATION: +0x48");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETREPUTATION, Wire.Relations.Head) == 0x4cu, "SERVER_SETREPUTATION: +0x4c");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETREPUTATION, Wire.DisplayName) == 0x64u, "SERVER_SETREPUTATION: +0x64");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETREPUTATION, Wire.NameBBuffer) == 0x2cu, "SERVER_SETREPUTATION: +0x2c");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETREPUTATION, Wire.NameABuffer) == 0x24u, "SERVER_SETREPUTATION: +0x24");

// FLPACKET_SERVER_ADJUSTATTITUDE -- id 0x2002a, registered at 06d9bfd1.
// Factory FUN_06d9d360: operator new(0x28) + ctor FUN_06d9d3a0. Vtable .rdata:06db50bc =
// { 06d9af50, 06daa0c0, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_ADJUSTATTITUDE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_ADJUSTATTITUDE() : FPacketBase(0x2002au) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daa0c0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadServerAdjustAttitude Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_ADJUSTATTITUDE) == 0x28u, "FLPACKET_SERVER_ADJUSTATTITUDE is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_ADJUSTATTITUDE, Wire) == 0x20u, "SERVER_ADJUSTATTITUDE: payload at +0x20");

// FLPACKET_SERVER_SETGROUPFEELINGS -- id 0x2002b, registered at 06d9bfe7.
// Factory FUN_06d9c500: operator new(0x34) + ctor FUN_06d9c540. Vtable .rdata:06db3d30 =
// { 06d91830, 06d91890, 06d91a40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETGROUPFEELINGS : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETGROUPFEELINGS() : FPacketBase(0x2002bu) {
        Wire.Name = nullptr;                    // +0x20
        Wire.NameSize = 0;                      // +0x24
        RpcPacketBuildListSentinel(&Wire.Relations, 0x0cu);   // operator new(0xc), Head
    }
    // FUN_06d917a0
    ~FPacket_FLPACKET_SERVER_SETGROUPFEELINGS() override {
        // 06d917a4..06d917c9: free every node's value first, testing each for null.
        FRpcRelationNode* Sentinel = Wire.Relations.Head;
        for (FRpcRelationNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
            if (Node->Value != nullptr) {
                ::operator delete(Node->Value);
            }
        }
        if (Wire.Name != nullptr) {   // 06d917cb..06d917d3
            ::operator delete(Wire.Name);
        }
        RpcPacketDestroyEmbeddedList(&Wire.Relations);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d91890
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d91a40
    FPayloadServerSetGroupFeelings Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETGROUPFEELINGS) == 0x34u, "FLPACKET_SERVER_SETGROUPFEELINGS is operator new(0x34)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETGROUPFEELINGS, Wire) == 0x20u, "SERVER_SETGROUPFEELINGS: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETGROUPFEELINGS, Wire.Relations) == 0x28u, "SERVER_SETGROUPFEELINGS: +0x28");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETGROUPFEELINGS, Wire.Relations.Head) == 0x2cu, "SERVER_SETGROUPFEELINGS: +0x2c");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETGROUPFEELINGS, Wire.Name) == 0x20u, "SERVER_SETGROUPFEELINGS: +0x20");

// FLPACKET_SERVER_CREATEMINE -- id 0x2002c, registered at 06d9bffd.
// Factory FUN_06d9cdc0: operator new(0x68) + ctor FUN_06d9ce00. Vtable .rdata:06db506c =
// { 06d9af50, 06da95d0, 06da98d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CREATEMINE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CREATEMINE() : FPacketBase(0x2002cu) {
        Wire.ObjId = 0;                         // +0x20
        Wire.ArchetypeNickname = 0;             // +0x24
        Wire.OwnerId = 0;                       // +0x28
        Wire.SubObjId = 0;                      // +0x2c
        Wire.OrientWBits = 0x3f800000u;         // +0x30; 1.0f
        Wire.OrientXBits = 0;                   // +0x34
        Wire.OrientYBits = 0;                   // +0x38
        Wire.OrientZBits = 0;                   // +0x3c
        Wire.PosXBits = 0;                      // +0x40
        Wire.PosYBits = 0;                      // +0x44
        Wire.PosZBits = 0;                      // +0x48
        Wire.VelXBits = 0;                      // +0x4c
        Wire.VelYBits = 0;                      // +0x50
        Wire.VelZBits = 0;                      // +0x54
        Wire.AngVelXBits = 0;                   // +0x58
        Wire.AngVelYBits = 0;                   // +0x5c
        Wire.AngVelZBits = 0;                   // +0x60
        Wire.OwnerPlayer = 0;                   // +0x64
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da95d0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da98d0
    FPayloadServerCreateMine Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CREATEMINE) == 0x68u, "FLPACKET_SERVER_CREATEMINE is operator new(0x68)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_CREATEMINE, Wire) == 0x20u, "SERVER_CREATEMINE: payload at +0x20");

// FLPACKET_SERVER_CREATECOUNTER -- id 0x2002d, registered at 06d9c013.
// Factory FUN_06d9ce60: operator new(0x68) + ctor FUN_06d9cea0. Vtable .rdata:06db507c =
// { 06d9af50, 06da9b50, 06da9e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CREATECOUNTER : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CREATECOUNTER() : FPacketBase(0x2002du) {
        Wire.ArchetypeNickname = 0;             // +0x20
        Wire.ObjId = 0;                         // +0x24
        Wire.OwnerPlayer = 0;                   // +0x28
        Wire.OwnerId = 0;                       // +0x2c
        Wire.SubObjId = SUBOBJ_ID_NONE;         // +0x30; COMMON.DLL SUBOBJ_ID_NONE
        Wire.OrientWBits = 0x3f800000u;         // +0x34; 1.0f
        Wire.OrientXBits = 0;                   // +0x38
        Wire.OrientYBits = 0;                   // +0x3c
        Wire.OrientZBits = 0;                   // +0x40
        Wire.PosXBits = 0;                      // +0x44
        Wire.PosYBits = 0;                      // +0x48
        Wire.PosZBits = 0;                      // +0x4c
        Wire.VelXBits = 0;                      // +0x50
        Wire.VelYBits = 0;                      // +0x54
        Wire.VelZBits = 0;                      // +0x58
        Wire.AngVelXBits = 0;                   // +0x5c
        Wire.AngVelYBits = 0;                   // +0x60
        Wire.AngVelZBits = 0;                   // +0x64
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da9b50
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da9e40
    FPayloadServerCreateCounter Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CREATECOUNTER) == 0x68u, "FLPACKET_SERVER_CREATECOUNTER is operator new(0x68)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_CREATECOUNTER, Wire) == 0x20u, "SERVER_CREATECOUNTER: payload at +0x20");

// FLPACKET_SERVER_EXPLODEASTEROIDMINE -- id 0x20031, registered at 06d9c029.
// Factory FUN_06d9cf10: operator new(0x30) + ctor FUN_06d9cf50. Vtable .rdata:06db508c =
// { 06d9af50, 06db14f0, 06dad820, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_EXPLODEASTEROIDMINE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_EXPLODEASTEROIDMINE() : FPacketBase(0x20031u) {
        Wire.MineId = 0;                        // +0x20
        Wire.OffsetXBits = 0;                   // +0x24
        Wire.OffsetYBits = 0;                   // +0x28
        Wire.OffsetZBits = 0;                   // +0x2c
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db14f0
        return RpcEncodeWireDwordBlock12(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dad820
        return RpcDecodeWireDwordBlock12(*this, Buffer, Size, OutLength);
    }
    FPayloadServerExplodeAsteroidMine Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_EXPLODEASTEROIDMINE) == 0x30u, "FLPACKET_SERVER_EXPLODEASTEROIDMINE is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_EXPLODEASTEROIDMINE, Wire) == 0x20u, "SERVER_EXPLODEASTEROIDMINE: payload at +0x20");

// FLPACKET_SERVER_LOADHINT -- id 0x2004d, registered at 06d9c03f.
// Factory FUN_06d9cf80: operator new(0x2c) + ctor FUN_06d9cfc0. Vtable .rdata:06db3cd0 =
// { 06d912c0, 06d90a50, 06d90ba0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_LOADHINT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_LOADHINT() : FPacketBase(0x2004du) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.Size = 0;                          // +0x24
        Wire.Kind = 0;                          // +0x28
    }
    // FUN_06d909d0
    ~FPacket_FLPACKET_SERVER_LOADHINT() override {
        if (Wire.Buffer != nullptr) {   // 06d909d3..06d909e9
            ::operator delete(Wire.Buffer);
            Wire.Buffer = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d90a50
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d90ba0
    FPayloadServerLoadHint Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_LOADHINT) == 0x2cu, "FLPACKET_SERVER_LOADHINT is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_LOADHINT, Wire) == 0x20u, "SERVER_LOADHINT: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_LOADHINT, Wire.Buffer) == 0x20u, "SERVER_LOADHINT: +0x20");

// FLPACKET_SERVER_SETDIRECTIVE -- id 0x2004e, registered at 06d9c055.
// Factory FUN_06d9cff0: operator new(0xb4) + ctor FUN_06d9d030. Vtable .rdata:06db509c =
// { 06d9af50, 06d90d10, 06d90f70, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETDIRECTIVE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETDIRECTIVE() : FPacketBase(0x2004eu) {
        Wire.Field_0x04 = 0;                    // +0x24
        Wire.bDockApproachCleared = 0;          // +0x28
        Wire.DelaySeconds = 0.0f;               // +0x2c
        Wire.DockObjectId = 0;                  // +0x30
        Wire.DockSecondaryObjectId = 0;         // +0x34
        Wire.DockPortIndex = -1;                // +0x38
        Wire.ProxTargetId = 0;                  // +0x3c
        Wire.ProxNearDist = 0.0f;               // +0x40
        Wire.ProxFarDist = 0.0f;                // +0x44
        Wire.FollowTargetId = 0;                // +0x48
        Wire.FollowDistance = 0.0f;             // +0x4c
        Wire.FollowOffset.x = 0.0f;             // +0x50
        Wire.FollowOffset.y = 0.0f;             // +0x54
        Wire.FollowOffset.z = 0.0f;             // +0x58
        Wire.FollowEngageRange = 0.0f;          // +0x5c
        Wire.GotoMode = 3;                      // +0x60
        Wire.GotoDestination.x = 0.0f;          // +0x64
        Wire.GotoDestination.y = 0.0f;          // +0x68
        Wire.GotoDestination.z = 0.0f;          // +0x6c
        Wire.GotoTargetId = 0;                  // +0x70
        Wire.SplineP1.Field_0x00 = 0;           // +0x74
        Wire.SplineP1.Field_0x04 = 0;           // +0x78
        Wire.SplineP1.Field_0x08 = 0;           // +0x7c
        Wire.SplineP2.Field_0x00 = 0;           // +0x80
        Wire.SplineP2.Field_0x04 = 0;           // +0x84
        Wire.SplineP2.Field_0x08 = 0;           // +0x88
        Wire.SplineP0.Field_0x00 = 0;           // +0x8c
        Wire.SplineP0.Field_0x04 = 0;           // +0x90
        Wire.SplineP0.Field_0x08 = 0;           // +0x94
        Wire.SplineP3.Field_0x00 = 0;           // +0x98
        Wire.SplineP3.Field_0x04 = 0;           // +0x9c
        Wire.SplineP3.Field_0x08 = 0;           // +0xa0
        Wire.GotoRange = 0.0f;                  // +0xa4
        Wire.GotoSpeed = 0.0f;                  // +0xa8
        Wire.bGotoFlyThrough = 0;               // +0xac
        Wire.Field_0x8d = 0;                    // +0xad
        Wire.bGotoCruise = 0;                   // +0xae
        Wire.bGotoNoCruise = 0;                 // +0xaf
        Wire.bGotoDisableAvoidance = 0;         // +0xb0
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d90d10
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d90f70
    FPayloadServerSetDirective Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETDIRECTIVE) == 0xb4u, "FLPACKET_SERVER_SETDIRECTIVE is operator new(0xb4)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETDIRECTIVE, Wire) == 0x20u, "SERVER_SETDIRECTIVE: payload at +0x20");

// FLPACKET_SERVER_CREATEGUIDED -- id 0x20037, registered at 06d9c06b.
// Factory FUN_06d9c960: operator new(0x58) + ctor FUN_06d9c9a0. Vtable .rdata:06db500c =
// { 06d9af50, 06da84e0, 06da8810, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CREATEGUIDED : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CREATEGUIDED() : FPacketBase(0x20037u) {
        Wire.PosXBits = 0;                      // +0x3c
        Wire.PosYBits = 0;                      // +0x40
        Wire.PosZBits = 0;                      // +0x44
        Wire.OrientWBits = 0x3f800000u;         // +0x48; 1.0f
        Wire.OrientXBits = 0;                   // +0x4c
        Wire.OrientYBits = 0;                   // +0x50
        Wire.OrientZBits = 0;                   // +0x54
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da84e0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da8810
    FPayloadServerCreateGuided Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CREATEGUIDED) == 0x58u, "FLPACKET_SERVER_CREATEGUIDED is operator new(0x58)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_CREATEGUIDED, Wire) == 0x20u, "SERVER_CREATEGUIDED: payload at +0x20");

// FLPACKET_SERVER_ITEMTRACTORED -- id 0x20038, registered at 06d9c081.
// Factory FUN_06d9c9e0: operator new(0x2c) + ctor FUN_06d9ca20. Vtable .rdata:06db501c =
// { 06d9af50, 06da8b50, 06da8cd0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_ITEMTRACTORED : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_ITEMTRACTORED() : FPacketBase(0x20038u) {
        Wire.LootId = 0;                        // +0x20
        Wire.ShipId = 0;                        // +0x24
        Wire.TractorEquipId = 0;                // +0x28
        Wire.UnitsTaken = 0;                    // +0x2a
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da8b50
        return RpcEncodeWireDwordDwordWordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da8cd0
        return RpcDecodeWireDwordDwordWordWord(*this, Buffer, Size, OutLength);
    }
    FPayloadServerItemTractored Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_ITEMTRACTORED) == 0x2cu, "FLPACKET_SERVER_ITEMTRACTORED is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_ITEMTRACTORED, Wire) == 0x20u, "SERVER_ITEMTRACTORED: payload at +0x20");

// FLPACKET_SERVER_SETREMOVEITEM -- id 0x2002f, registered at 06d9c097.
// Factory FUN_06d9c470: operator new(0x2c) + ctor FUN_06d9c4b0. Vtable .rdata:06db4fac =
// { 06d9c4e0, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETREMOVEITEM : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETREMOVEITEM() : FPacketBase(0x2002fu) {
        Wire.ItemId = 0;                        // +0x20
        Wire.Count = 0xffffffffu;               // +0x24
        Wire.RemoveResult = 0xffffffffu;        // +0x28
    }
    // FUN_06da7fa0
    ~FPacket_FLPACKET_SERVER_SETREMOVEITEM() override {
        // Own vptr store, then straight into CDAPacket's dtor: nothing to free.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    FPayloadServerSetRemoveItem Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETREMOVEITEM) == 0x2cu, "FLPACKET_SERVER_SETREMOVEITEM is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETREMOVEITEM, Wire) == 0x20u, "SERVER_SETREMOVEITEM: payload at +0x20");

// FLPACKET_SERVER_SETADDITEM -- id 0x2002e, registered at 06d9c0ad.
// Factory FUN_06d9c400: operator new(0x40) + ctor FUN_06d9c440. Vtable .rdata:06db3ee4 =
// { 06d96f70, 06d96f90, 06d97210, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETADDITEM : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETADDITEM() : FPacketBase(0x2002eu) {
        Wire.HardpointSize = 0;                 // +0x28
        Wire.bMission = 0;                      // +0x39
        Wire.Hardpoint = nullptr;               // +0x3c
    }
    // FUN_06d96f40
    ~FPacket_FLPACKET_SERVER_SETADDITEM() override {
        if (Wire.Hardpoint != nullptr) {   // 06d96f43..06d96f51
            ::operator delete(Wire.Hardpoint);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d96f90
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d97210
    FPayloadServerSetAddItem Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETADDITEM) == 0x40u, "FLPACKET_SERVER_SETADDITEM is operator new(0x40)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETADDITEM, Wire) == 0x20u, "SERVER_SETADDITEM: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETADDITEM, Wire.Hardpoint) == 0x3cu, "SERVER_SETADDITEM: +0x3c");

// FLPACKET_SERVER_SETCASH -- id 0x20030, registered at 06d9c0c3.
// Factory FUN_06d9c390: operator new(0x24) + ctor FUN_06d9c3d0. Vtable .rdata:06db4f9c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETCASH : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETCASH() : FPacketBase(0x20030u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETCASH) == 0x24u, "FLPACKET_SERVER_SETCASH is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETCASH, Wire) == 0x20u, "SERVER_SETCASH: payload at +0x20");

// FLPACKET_SERVER_REQUESTSPACESCRIPT -- id 0x20032, registered at 06d9c0d9.
// Factory FUN_06d9dad0: operator new(0x3c) + ctor FUN_06d9db10. Vtable .rdata:06db3ff8 =
// { 06d986c0, 06d986e0, 06d98850, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT() : FPacketBase(0x20032u) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.PosXBits = 0;                      // +0x28
        Wire.PosYBits = 0;                      // +0x2c
        Wire.PosZBits = 0;                      // +0x30
        Wire.CameraModeWire = 1u;               // +0x34
        Wire.Name = nullptr;                    // +0x38
    }
    // FUN_06d98690
    ~FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT() override {
        if (Wire.Name != nullptr) {   // 06d98693..06d986a1
            ::operator delete(Wire.Name);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d986e0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d98850
    FPayloadServerRequestSpaceScript Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT) == 0x3cu, "FLPACKET_SERVER_REQUESTSPACESCRIPT is operator new(0x3c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT, Wire) == 0x20u, "SERVER_REQUESTSPACESCRIPT: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT, Wire.Name) == 0x38u, "SERVER_REQUESTSPACESCRIPT: +0x38");

// FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE -- id 0x20033, registered at 06d9c0ef.
// Factory FUN_06d9d5f0: operator new(0x2c) + ctor FUN_06d9d630. Vtable .rdata:06db50fc =
// { 06d9af50, 06daaf90, 06dacea0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE() : FPacketBase(0x20033u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06daaf90
        return RpcEncodeWireDwordDwordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dacea0
        return RpcDecodeWireDwordDwordWord(*this, Buffer, Size, OutLength);
    }
    FPayloadServerSetMissionObjectiveState Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE) == 0x2cu, "FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE, Wire) == 0x20u, "SERVER_SETMISSIONOBJECTIVESTATE: payload at +0x20");

// FLPACKET_SERVER_CFGINTERFACENOTIFICATION -- id 0x20047, registered at 06d9c105.
// Factory FUN_06d9d6d0: operator new(0x2c) + ctor FUN_06d9d710. Vtable .rdata:06db511c =
// { 06d9af50, 06dab200, 06dab350, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CFGINTERFACENOTIFICATION : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CFGINTERFACENOTIFICATION() : FPacketBase(0x20047u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dab200
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dab350
    FPayloadServerCfgInterfaceNotification Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CFGINTERFACENOTIFICATION) == 0x2cu, "FLPACKET_SERVER_CFGINTERFACENOTIFICATION is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_CFGINTERFACENOTIFICATION, Wire) == 0x20u, "SERVER_CFGINTERFACENOTIFICATION: payload at +0x20");

// FLPACKET_SERVER_MARKOBJ -- id 0x20046, registered at 06d9c11b.
// Factory FUN_06d9d660: operator new(0x28) + ctor FUN_06d9d6a0. Vtable .rdata:06db510c =
// { 06d9af50, 06dab0e0, 06daf230, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_MARKOBJ : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_MARKOBJ() : FPacketBase(0x20046u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dab0e0
        return RpcEncodeWireDwordByte(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06daf230
        return RpcDecodeWireDwordByte(*this, Buffer, Size, OutLength);
    }
    FPayloadServerMarkObj Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_MARKOBJ) == 0x28u, "FLPACKET_SERVER_MARKOBJ is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_MARKOBJ, Wire) == 0x20u, "SERVER_MARKOBJ: payload at +0x20");

// FLPACKET_SERVER_SETMISSIONOBJECTIVES -- id 0x20035, registered at 06d9c131.
// Factory FUN_06d9da00: operator new(0x48) + ctor FUN_06d9da40. Vtable .rdata:06db3c90 =
// { 06d8fbf0, 06d8fc90, 06d8ffd0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES() : FPacketBase(0x20035u) {
        Wire.TextASize = 0;                     // +0x28
        Wire.TextABuffer = nullptr;             // +0x2c
        Wire.TextBSize = 0;                     // +0x30
        Wire.TextBBuffer = nullptr;             // +0x34
        Wire.Count = 0;                         // +0x38
        RpcPacketBuildListSentinel(&Wire.Objectives, 0x0cu);   // operator new(0xc), Head
    }
    // FUN_06d8fb00
    ~FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES() override {
        // 06d8fb28..06d8fb65: every record, then its text block before the record itself.
        FRpcMissionObjectiveNode* Sentinel = Wire.Objectives.Head;
        for (FRpcMissionObjectiveNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
            FRpcMissionObjectiveRecord* Record = Node->Value;
            if (Record != nullptr) {                                   // 06d8fb43
                if (Record->TextBuffer != nullptr) {                   // record+0x08
                    ::operator delete(Record->TextBuffer);
                }
                ::operator delete(Record);
            }
        }
        RpcPacketDestroyEmbeddedList(&Wire.Objectives);   // FUN_06d901f0
        if (Wire.TextBBuffer != nullptr) {   // 06d8fb9a..06d8fba6
            ::operator delete(Wire.TextBBuffer);
        }
        if (Wire.TextABuffer != nullptr) {   // 06d8fbae..06d8fbb6
            ::operator delete(Wire.TextABuffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d8fc90
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d8ffd0
    FPayloadServerSetMissionObjectives Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES) == 0x48u, "FLPACKET_SERVER_SETMISSIONOBJECTIVES is operator new(0x48)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES, Wire) == 0x20u, "SERVER_SETMISSIONOBJECTIVES: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES, Wire.Objectives) == 0x3cu, "SERVER_SETMISSIONOBJECTIVES: +0x3c");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES, Wire.Objectives.Head) == 0x40u, "SERVER_SETMISSIONOBJECTIVES: +0x40");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES, Wire.TextBBuffer) == 0x34u, "SERVER_SETMISSIONOBJECTIVES: +0x34");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES, Wire.TextABuffer) == 0x2cu, "SERVER_SETMISSIONOBJECTIVES: +0x2c");

// FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE -- id 0x20034, registered at 06d9c147.
// Factory FUN_06d9d740: operator new(0x30) + ctor FUN_06d9d780. Vtable .rdata:06db512c =
// { 06d9d7b0, 06dab4b0, 06dab630, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE() : FPacketBase(0x20034u) {
        Wire.TextSize = 0;                      // +0x28
        Wire.TextBuffer = nullptr;              // +0x2c
    }
    // FUN_06d9d7d0
    ~FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE() override {
        if (Wire.TextBuffer != nullptr) {   // 06d9d7d3..06d9d7db
            ::operator delete(Wire.TextBuffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dab4b0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dab630
    FPayloadServerReplaceMissionObjective Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE) == 0x30u, "FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE, Wire) == 0x20u, "SERVER_REPLACEMISSIONOBJECTIVE: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE, Wire.TextBuffer) == 0x2cu, "SERVER_REPLACEMISSIONOBJECTIVE: +0x2c");

// FLPACKET_SERVER_SET_STORY_CUE -- id 0x20043, registered at 06d9c15d.
// Factory FUN_06d9d7f0: operator new(0x24) + ctor FUN_06d9d830. Vtable .rdata:06db513c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SET_STORY_CUE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SET_STORY_CUE() : FPacketBase(0x20043u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SET_STORY_CUE) == 0x24u, "FLPACKET_SERVER_SET_STORY_CUE is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SET_STORY_CUE, Wire) == 0x20u, "SERVER_SET_STORY_CUE: payload at +0x20");

// FLPACKET_SERVER_SET_CAMERA -- id 0x2004b, registered at 06d9c173.
// Factory FUN_06d9d860: operator new(0x44) + ctor FUN_06d9d8a0. Vtable .rdata:06db514c =
// { 06d9af50, 06dab780, 06dab960, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SET_CAMERA : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SET_CAMERA() : FPacketBase(0x2004bu) {
        Wire.Position.x = 0.0f;                 // +0x20
        Wire.Position.y = 0.0f;                 // +0x24
        Wire.Position.z = 0.0f;                 // +0x28
        Wire.Orient.w = 1.0f;                   // +0x2c
        Wire.Orient.x = 0.0f;                   // +0x30
        Wire.Orient.y = 0.0f;                   // +0x34
        Wire.Orient.z = 0.0f;                   // +0x38
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dab780
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dab960
    FPayloadServerSetCamera Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SET_CAMERA) == 0x44u, "FLPACKET_SERVER_SET_CAMERA is operator new(0x44)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SET_CAMERA, Wire) == 0x20u, "SERVER_SET_CAMERA: payload at +0x20");

// FLPACKET_SERVER_REVERT_CAMERA -- id 0x2004c, registered at 06d9c189.
// Factory FUN_06d9d8e0: operator new(0x20) + ctor FUN_06d9d920. Vtable .rdata:06db515c =
// { 06d9af50, 06dae0d0, 06dabad0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_REVERT_CAMERA : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_REVERT_CAMERA() : FPacketBase(0x2004cu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dae0d0
        return RpcEncodeWireEmpty(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dabad0
        return RpcDecodeWireEmpty(*this, Buffer, Size, OutLength);
    }
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REVERT_CAMERA) == 0x20u, "FLPACKET_SERVER_REVERT_CAMERA is operator new(0x20)");

// FLPACKET_SERVER_SET_MISSION_MESSAGE -- id 0x20045, registered at 06d9c19f.
// Factory FUN_06d9d950: operator new(0x2c) + ctor FUN_06d9d990. Vtable .rdata:06db516c =
// { 06d9d9c0, 06dabbb0, 06dabd00, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SET_MISSION_MESSAGE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SET_MISSION_MESSAGE() : FPacketBase(0x20045u) {
        Wire.TextSize = 0;                      // +0x20
        Wire.TextBuffer = nullptr;              // +0x24
    }
    // FUN_06d9d9e0
    ~FPacket_FLPACKET_SERVER_SET_MISSION_MESSAGE() override {
        if (Wire.TextBuffer != nullptr) {   // 06d9d9e3..06d9d9eb
            ::operator delete(Wire.TextBuffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dabbb0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dabd00
    FPayloadServerSetMissionMessage Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SET_MISSION_MESSAGE) == 0x2cu, "FLPACKET_SERVER_SET_MISSION_MESSAGE is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SET_MISSION_MESSAGE, Wire) == 0x20u, "SERVER_SET_MISSION_MESSAGE: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SET_MISSION_MESSAGE, Wire.TextBuffer) == 0x24u, "SERVER_SET_MISSION_MESSAGE: +0x24");

// FLPACKET_SERVER_REPAIROBJECT -- id 0x2003c, registered at 06d9c1b5.
// Factory FUN_06d9e3a0: operator new(0x2c) + ctor FUN_06d9e3e0. Vtable .rdata:06db524c =
// { 06d9af50, 06db0160, 06da6e40, 06dac2b0 }.
class FPacket_FLPACKET_SERVER_REPAIROBJECT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_REPAIROBJECT() : FPacketBase(0x2003cu) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.EquipId = 0;                       // +0x24
        Wire.Points = 0;                        // +0x28
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    void Dump(std::ostream& Out) const override;   // 06dac2b0
    FPayloadServerRepairObject Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REPAIROBJECT) == 0x2cu, "FLPACKET_SERVER_REPAIROBJECT is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_REPAIROBJECT, Wire) == 0x20u, "SERVER_REPAIROBJECT: payload at +0x20");

// FLPACKET_SERVER_SETNUMKILLS -- id 0x2003e, registered at 06d9c1cb.
// Factory FUN_06d9e410: operator new(0x24) + ctor FUN_06d9e450. Vtable .rdata:06db525c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETNUMKILLS : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETNUMKILLS() : FPacketBase(0x2003eu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETNUMKILLS) == 0x24u, "FLPACKET_SERVER_SETNUMKILLS is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETNUMKILLS, Wire) == 0x20u, "SERVER_SETNUMKILLS: payload at +0x20");

// FLPACKET_SERVER_SETMISSIONSUCCESSES -- id 0x2003f, registered at 06d9c1e1.
// Factory FUN_06d9e480: operator new(0x24) + ctor FUN_06d9e4c0. Vtable .rdata:06db526c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETMISSIONSUCCESSES : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETMISSIONSUCCESSES() : FPacketBase(0x2003fu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETMISSIONSUCCESSES) == 0x24u, "FLPACKET_SERVER_SETMISSIONSUCCESSES is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETMISSIONSUCCESSES, Wire) == 0x20u, "SERVER_SETMISSIONSUCCESSES: payload at +0x20");

// FLPACKET_SERVER_SETMISSIONFAILURES -- id 0x20040, registered at 06d9c1f7.
// Factory FUN_06d9e4f0: operator new(0x24) + ctor FUN_06d9e530. Vtable .rdata:06db527c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETMISSIONFAILURES : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETMISSIONFAILURES() : FPacketBase(0x20040u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETMISSIONFAILURES) == 0x24u, "FLPACKET_SERVER_SETMISSIONFAILURES is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETMISSIONFAILURES, Wire) == 0x20u, "SERVER_SETMISSIONFAILURES: payload at +0x20");

// FLPACKET_SERVER_BURNFUSE -- id 0x20041, registered at 06d9c20d.
// Factory FUN_06d9e560: operator new(0x3c) + ctor FUN_06d9e5a0. Vtable .rdata:06db528c =
// { 06d9af50, 06dac3c0, 06dac5c0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_BURNFUSE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_BURNFUSE() : FPacketBase(0x20041u) {
        Wire.SourceId = 0;                      // +0x20
        Wire.ObjectId = 0;                      // +0x24
        Wire.FuseId = 0;                        // +0x28
        Wire.SubObjId = SUBOBJ_ID_NONE;         // +0x2c; COMMON.DLL SUBOBJ_ID_NONE
        Wire.AtTBits = 0;                       // +0x30
        Wire.LifetimeBits = 0xbf800000u;        // +0x34; -1.0f
        Wire.bLit = 0;                          // +0x38
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dac3c0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dac5c0
    FPayloadServerBurnFuse Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_BURNFUSE) == 0x3cu, "FLPACKET_SERVER_BURNFUSE is operator new(0x3c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_BURNFUSE, Wire) == 0x20u, "SERVER_BURNFUSE: payload at +0x20");

// FLPACKET_SERVER_CREATESOLAR -- id 0x20042, registered at 06d9c223.
// Factory FUN_06d9e5f0: operator new(0xb8) + ctor FUN_06d9e630. Vtable .rdata:06db529c =
// { 06d9e720, 06dac7c0, 06daca80, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CREATESOLAR : public FRpcCreateObjectPacket {
public:
    // FUN_06d9e630. Wire.Common is built first, by FRpcCreateObjectWire's constructors
    // (the head, with FUN_06d9cda0 inlined, then COMMON.DLL's list ctor at +0x74 and
    // DamageList ctor at +0x80); the body is the tail. +0x9e/+0x9f and Affiliation /
    // Attitude (+0xb0 / +0xb4) are not stored by retail's constructor and are not stored here.
    FPacket_FLPACKET_SERVER_CREATESOLAR() : FRpcCreateObjectPacket(0x20042u) {
        Wire.NameASize = 0;               // +0xa0
        Wire.NameABuffer = nullptr;       // +0xa4
        Wire.NameBSize = 0;               // +0xa8
        Wire.NameBBuffer = nullptr;       // +0xac
        Wire.bDestructible = 0;           // byte +0x9c
        Wire.bMarkedBy = 0;               // byte +0x9d
    }
    // FUN_06d9e740
    ~FPacket_FLPACKET_SERVER_CREATESOLAR() override {
        if (Wire.NameBBuffer != nullptr) {                     // 06d9e75c..06d9e76f (+0xac)
            ::operator delete(Wire.NameBBuffer);
        }
        if (Wire.NameABuffer != nullptr) {                     // 06d9e777..06d9e782 (+0xa4)
            ::operator delete(Wire.NameABuffer);
        }
        // 06d9e78a stores the abstract base vptr 06db3fb4; Wire.Common's destructor and
        // ~FPacketBase follow.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dac7c0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daca80
    FPayloadServerCreateSolar Wire;   // +0x20; retail's class is operator new(0xb8)
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CREATESOLAR) == 0x20u + sizeof(FPayloadServerCreateSolar),
              "FLPACKET_SERVER_CREATESOLAR: the payload member starts at +0x20");

// FLPACKET_SERVER_SETCOLLISIONGROUPS -- id 0x20048, registered at 06d9c239.
// Factory FUN_06d9e870: operator new(0x2c) + ctor FUN_06d9e8b0. Vtable .rdata:06db52ac =
// { 06da1130, 06dacca0, 06dacd70, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS : public FRpcCollisionGroupPacket {
public:
    FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS() : FRpcCollisionGroupPacket(0x20048u) {
        RpcPacketBuildListSentinel(&Wire, 0x10u);   // operator new(0x10), Head
    }
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS) == 0x2cu, "FLPACKET_SERVER_SETCOLLISIONGROUPS is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS, Wire) == 0x20u, "SERVER_SETCOLLISIONGROUPS: payload at +0x20");

// FLPACKET_SERVER_SETHULLSTATUS -- id 0x20049, registered at 06d9c24f.
// Factory FUN_06d9e990: operator new(0x24) + ctor FUN_06d9e9d0. Vtable .rdata:06db52bc =
// { 06d9af50, 06db1cd0, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETHULLSTATUS : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETHULLSTATUS() : FPacketBase(0x20049u) {
        Wire.HullStatus = 0.0f;                 // +0x20
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db1cd0
        return RpcEncodeWireFloat(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerSetHullStatus Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETHULLSTATUS) == 0x24u, "FLPACKET_SERVER_SETHULLSTATUS is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETHULLSTATUS, Wire) == 0x20u, "SERVER_SETHULLSTATUS: payload at +0x20");

// FLPACKET_SERVER_SETGUIDEDTARGET -- id 0x2004a, registered at 06d9c265.
// Factory FUN_06d9ea00: operator new(0x2c) + ctor FUN_06d9ea40. Vtable .rdata:06db52cc =
// { 06d9af50, 06daaf90, 06dacea0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETGUIDEDTARGET : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETGUIDEDTARGET() : FPacketBase(0x2004au) {
        Wire.GuidedId = 0;                      // +0x20
        Wire.TargetId = 0;                      // +0x24
        Wire.TargetSubObjId = 0;                // +0x28
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06daaf90
        return RpcEncodeWireDwordDwordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dacea0
        return RpcDecodeWireDwordDwordWord(*this, Buffer, Size, OutLength);
    }
    FPayloadServerSetGuidedTarget Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETGUIDEDTARGET) == 0x2cu, "FLPACKET_SERVER_SETGUIDEDTARGET is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SETGUIDEDTARGET, Wire) == 0x20u, "SERVER_SETGUIDEDTARGET: payload at +0x20");

// FLPACKET_SERVER_SENDCOMM -- id 0x2004f, registered at 06d9c27b.
// Factory FUN_06d9ea70: operator new(0x78) + ctor FUN_06d9eab0. Vtable .rdata:06db52dc =
// { 06d9eaf0, 06dad000, 06dad320, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SENDCOMM : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SENDCOMM() : FPacketBase(0x2004fu) {
        Wire.Body.Head = 0;                     // +0x2c
        Wire.Body.Body = 0;                     // +0x30
        Wire.Body.LeftHand = 0;                 // +0x34
        Wire.Body.RightHand = 0;                // +0x38
        Wire.Body.Count = 0;                    // +0x5c
        Wire.Buffer = nullptr;                  // +0x64
        Wire.bReceiverIsOwnShip = 1u;           // +0x74
    }
    // FUN_06d9eb10
    ~FPacket_FLPACKET_SERVER_SENDCOMM() override {
        if (Wire.Buffer != nullptr) {   // 06d9eb13..06d9eb21
            ::operator delete(Wire.Buffer);
        }
        Wire.Buffer = nullptr;           // unconditional
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dad000
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dad320
    FPayloadServerSendComm Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SENDCOMM) == 0x78u, "FLPACKET_SERVER_SENDCOMM is operator new(0x78)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SENDCOMM, Wire) == 0x20u, "SERVER_SENDCOMM: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SENDCOMM, Wire.Buffer) == 0x64u, "SERVER_SENDCOMM: +0x64");

// FLPACKET_SERVER_USE_ITEM -- id 0x20051, registered at 06d9c291.
// Factory FUN_06d9eb40: operator new(0x28) + ctor FUN_06d9eb80. Vtable .rdata:06db52ec =
// { 06d9af50, 06db19b0, 06dad690, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_USE_ITEM : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_USE_ITEM() : FPacketBase(0x20051u) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.EquipId = 0;                       // +0x24
        Wire.Count = 0;                         // +0x26
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db19b0
        return RpcEncodeWireDwordWordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dad690
        return RpcDecodeWireDwordWordWord(*this, Buffer, Size, OutLength);
    }
    FPayloadServerUseItem Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_USE_ITEM) == 0x28u, "FLPACKET_SERVER_USE_ITEM is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_USE_ITEM, Wire) == 0x20u, "SERVER_USE_ITEM: payload at +0x20");

// FLPACKET_SERVER_SCANNOTIFY -- id 0x20039, registered at 06d9c2a7.
// Factory FUN_06d9ebb0: operator new(0x28) + ctor FUN_06d9ebf0. Vtable .rdata:06db52fc =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SCANNOTIFY : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SCANNOTIFY() : FPacketBase(0x20039u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDwordPair Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SCANNOTIFY) == 0x28u, "FLPACKET_SERVER_SCANNOTIFY is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SCANNOTIFY, Wire) == 0x20u, "SERVER_SCANNOTIFY: payload at +0x20");

// FLPACKET_SERVER_PLAYERLIST -- id 0x20052, registered at 06d9c2bd.
// Factory FUN_06d9ec20: operator new(0x30) + ctor FUN_06d9ec60. Vtable .rdata:06db4038 =
// { 06d99930, 06d99990, 06d99b60, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_PLAYERLIST : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_PLAYERLIST() : FPacketBase(0x20052u) {
        Wire.Kind = 3u;                         // +0x20
        Wire.Name = nullptr;                    // +0x24
        Wire.PlayerId = 0;                      // +0x28
        Wire.bAlreadyPresent = 0;               // +0x2c
    }
    // FUN_06d99900
    ~FPacket_FLPACKET_SERVER_PLAYERLIST() override {
        Wire.PlayerId = 0;          // before the test
        if (Wire.Name != nullptr) {   // 06d99903..06d99915
            ::free(Wire.Name);          // MSVCRT free, not delete
            Wire.Name = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d99990
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d99b60
    FPayloadServerPlayerList Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_PLAYERLIST) == 0x30u, "FLPACKET_SERVER_PLAYERLIST is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_PLAYERLIST, Wire) == 0x20u, "SERVER_PLAYERLIST: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_SERVER_PLAYERLIST, Wire.PlayerId) == 0x28u, "SERVER_PLAYERLIST: +0x28");
static_assert(offsetof(FPacket_FLPACKET_SERVER_PLAYERLIST, Wire.Name) == 0x24u, "SERVER_PLAYERLIST: +0x24");

// FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE -- id 0x2003d, registered at 06d9c2d3.
// Factory FUN_06d9eca0: operator new(0x2c) + ctor FUN_06d9ece0. Vtable .rdata:06db3c70 =
// { 06d8f820, 06d8f840, 06d8f970, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE : public FRpcEquipListPacket {
public:
    FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE() : FRpcEquipListPacket(0x2003du) {
        EquipHandle = nullptr;                  // +0x20
        Flag = 0;                               // +0x24
        ObjectId = 0;                           // +0x28
    }
    // FUN_06d8f810
    ~FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE() override {
        // Own vptr store, then JMP FUN_06da21d0 -- ~FRpcEquipListPacket.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d8f840
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d8f970
    // The rest of FPayloadServerRemoteObjectCargoUpdate; its +0x00 is EquipHandle.
    unsigned char Flag;          // +0x24  always 1 from Server.dll, never read by the receiver
    unsigned char Pad_0x25[3];   // +0x25
    unsigned int ObjectId;       // +0x28  the ship whose cargo is synced
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE) == 0x2cu, "FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE, Flag) == 0x20u + offsetof(FPayloadServerRemoteObjectCargoUpdate, Flag),
              "SERVER_REMOTEOBJECTCARGOUPDATE: Flag at +0x24");
static_assert(offsetof(FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE, ObjectId) == 0x20u + offsetof(FPayloadServerRemoteObjectCargoUpdate, ObjectId),
              "SERVER_REMOTEOBJECTCARGOUPDATE: ObjectId at +0x28");

// FLPACKET_SERVER_MISCOBJUPDATE -- id 0x20054, registered at 06d9c2e9.
// Factory FUN_06d9ed80: operator new(0x6c) + ctor FUN_06d9edc0. Vtable .rdata:06db4018 =
// { 06d98aa0, 06d98d30, 06d990d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_MISCOBJUPDATE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_MISCOBJUPDATE() : FPacketBase(0x20054u) {
        Wire.ObjectId = 0;
        Wire.GroupId = 0;
        Wire.Flags = 0;
        Wire.PlayerId = 0;
        Wire.MissionId = 0;
        Wire.MissionSetBy = 0;
        Wire.Field_0x30 = 0;
        Wire.Affiliation = 0xffffffffu;
        Wire.Rank = 0;
        Wire.SystemId = 0;
        Wire.IdCount = 0;
    }
    // FUN_06d98a30
    ~FPacket_FLPACKET_SERVER_MISCOBJUPDATE() override {
        // Retail frees the vector at +0x5c first, then the string at +0x44. Both are real
        // std members here, destroyed in reverse declaration order: Ids, then Text -- the
        // same order.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d98d30
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d990d0
    FPayloadServerMiscObjUpdate Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_MISCOBJUPDATE) == 0x20u + sizeof(FPayloadServerMiscObjUpdate),
              "FLPACKET_SERVER_MISCOBJUPDATE: the payload member starts at +0x20");

// FLPACKET_SERVER_FORMATION_UPDATE -- id 0x20053, registered at 06d9c2ff.
// Factory FUN_06d9ed10: operator new(0x30) + ctor FUN_06d9ed50. Vtable .rdata:06db530c =
// { 06d9af50, 06db14f0, 06dad820, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_FORMATION_UPDATE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_FORMATION_UPDATE() : FPacketBase(0x20053u) {
        Wire.LeaderId = 0;                      // +0x20
        Wire.Offset.x = 0.0f;                   // +0x24
        Wire.Offset.y = 0.0f;                   // +0x28
        Wire.Offset.z = 0.0f;                   // +0x2c
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db14f0
        return RpcEncodeWireDwordBlock12(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dad820
        return RpcDecodeWireDwordBlock12(*this, Buffer, Size, OutLength);
    }
    FPayloadServerFormationUpdate Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_FORMATION_UPDATE) == 0x30u, "FLPACKET_SERVER_FORMATION_UPDATE is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_FORMATION_UPDATE, Wire) == 0x20u, "SERVER_FORMATION_UPDATE: payload at +0x20");

// FLPACKET_SERVER_OBJECTCARGOUPDATE -- id 0x20055, registered at 06d9c315.
// Factory FUN_06d9ee30: operator new(0x34) + ctor FUN_06d9ee70. Vtable .rdata:06db531c =
// { 06d9eeb0, 06dad9b0, 06dadb80, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_OBJECTCARGOUPDATE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_OBJECTCARGOUPDATE() : FPacketBase(0x20055u) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.ArchId = 0;                        // +0x24
        Wire.EquipId = 0;                       // +0x28
        Wire.Count = 1u;                        // +0x2a
        Wire.Status = 1.0f;                     // +0x2c
        Wire.bMission = 0;                      // +0x30
    }
    // FUN_06dad9a0
    ~FPacket_FLPACKET_SERVER_OBJECTCARGOUPDATE() override {
        // Own vptr store, then straight into CDAPacket's dtor: nothing to free.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dad9b0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dadb80
    FPayloadServerObjectCargoUpdate Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_OBJECTCARGOUPDATE) == 0x34u, "FLPACKET_SERVER_OBJECTCARGOUPDATE is operator new(0x34)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_OBJECTCARGOUPDATE, Wire) == 0x20u, "SERVER_OBJECTCARGOUPDATE: payload at +0x20");

// FLPACKET_SERVER_SENDNNMESSAGE -- id 0x20056, registered at 06d9c32b.
// Factory FUN_06d9eed0: operator new(0x24) + ctor FUN_06d9ef10. Vtable .rdata:06db532c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SENDNNMESSAGE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SENDNNMESSAGE() : FPacketBase(0x20056u) {
        Wire.Field_0x00 = 0;                    // +0x20
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SENDNNMESSAGE) == 0x24u, "FLPACKET_SERVER_SENDNNMESSAGE is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SENDNNMESSAGE, Wire) == 0x20u, "SERVER_SENDNNMESSAGE: payload at +0x20");

// FLPACKET_SERVER_SET_MUSIC -- id 0x20057, registered at 06d9c341.
// Factory FUN_06d9ef40: operator new(0x38) + ctor FUN_06d9ef80. Vtable .rdata:06db533c =
// { 06d9af50, 06dadd60, 06dadf30, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SET_MUSIC : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SET_MUSIC() : FPacketBase(0x20057u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dadd60
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dadf30
    FPayloadServerSetMusic Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SET_MUSIC) == 0x38u, "FLPACKET_SERVER_SET_MUSIC is operator new(0x38)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_SET_MUSIC, Wire) == 0x20u, "SERVER_SET_MUSIC: payload at +0x20");

// FLPACKET_SERVER_CANCEL_MUSIC -- id 0x20058, registered at 06d9c357.
// Factory FUN_06d9efb0: operator new(0x20) + ctor FUN_06d9eff0. Vtable .rdata:06db534c =
// { 06d9af50, 06dae0d0, 06dabad0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CANCEL_MUSIC : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CANCEL_MUSIC() : FPacketBase(0x20058u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dae0d0
        return RpcEncodeWireEmpty(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dabad0
        return RpcDecodeWireEmpty(*this, Buffer, Size, OutLength);
    }
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CANCEL_MUSIC) == 0x20u, "FLPACKET_SERVER_CANCEL_MUSIC is operator new(0x20)");

// FLPACKET_SERVER_PLAY_SOUND_EFFECT -- id 0x20059, registered at 06d9c36d.
// Factory FUN_06d9f020: operator new(0x24) + ctor FUN_06d9f060. Vtable .rdata:06db535c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_PLAY_SOUND_EFFECT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_PLAY_SOUND_EFFECT() : FPacketBase(0x20059u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadServerDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_PLAY_SOUND_EFFECT) == 0x24u, "FLPACKET_SERVER_PLAY_SOUND_EFFECT is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_SERVER_PLAY_SOUND_EFFECT, Wire) == 0x20u, "SERVER_PLAY_SOUND_EFFECT: payload at +0x20");

// ===========================================================================
// Channel 3 -- the classes registered by FUN_06d9f100, in its registration order.
// ===========================================================================

// FLPACKET_CLIENT_REQCHANGECASH -- id 0x3001c, registered at 06d9f10d.
// Factory FUN_06d9f640: operator new(0x24) + ctor FUN_06da0570. Vtable .rdata:06db5af4 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQCHANGECASH : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQCHANGECASH() : FPacketBase(0x3001cu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQCHANGECASH) == 0x24u, "FLPACKET_CLIENT_REQCHANGECASH is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQCHANGECASH, Wire) == 0x20u, "CLIENT_REQCHANGECASH: payload at +0x20");

// FLPACKET_CLIENT_REQSETCASH -- id 0x3001b, registered at 06d9f123.
// Factory FUN_06d9f840: operator new(0x24) + ctor FUN_06da05a0. Vtable .rdata:06db5b04 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQSETCASH : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQSETCASH() : FPacketBase(0x3001bu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQSETCASH) == 0x24u, "FLPACKET_CLIENT_REQSETCASH is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQSETCASH, Wire) == 0x20u, "CLIENT_REQSETCASH: payload at +0x20");

// FLPACKET_CLIENT_REQREMOVEITEM -- id 0x30019, registered at 06d9f139.
// Factory FUN_06d9f880: operator new(0x28) + ctor FUN_06da05d0. Vtable .rdata:06db5b14 =
// { 06da0600, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQREMOVEITEM : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQREMOVEITEM() : FPacketBase(0x30019u) {
        Wire.EquipId = 0;                       // +0x20
        Wire.Count = 0xffffffffu;               // +0x24
    }
    // FUN_06dae240
    ~FPacket_FLPACKET_CLIENT_REQREMOVEITEM() override {
        // Own vptr store, then straight into CDAPacket's dtor: nothing to free.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadClientReqRemoveItem Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQREMOVEITEM) == 0x28u, "FLPACKET_CLIENT_REQREMOVEITEM is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQREMOVEITEM, Wire) == 0x20u, "CLIENT_REQREMOVEITEM: payload at +0x20");

// FLPACKET_CLIENT_REQMODIFYITEM -- id 0x3001a, registered at 06d9f14f.
// Factory FUN_06d9f900: operator new(0x38) + ctor FUN_06da0660. Vtable .rdata:06db4070 =
// { 06d9a200, 06d9a220, 06d9a410, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQMODIFYITEM : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQMODIFYITEM() : FPacketBase(0x3001au) {
        Wire.Count = 0;                         // +0x20
        Wire.HardpointSize = 0;                 // +0x24
        Wire.EquipId = 0;                       // +0x28
        Wire.Health = 0.0f;                     // +0x2c
        Wire.bMounted = 0;                      // +0x30
        Wire.Hardpoint = nullptr;               // +0x34
    }
    // FUN_06d9a1d0
    ~FPacket_FLPACKET_CLIENT_REQMODIFYITEM() override {
        if (Wire.Hardpoint != nullptr) {   // 06d9a1d3..06d9a1e1
            ::operator delete(Wire.Hardpoint);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d9a220
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d9a410
    FPayloadClientReqModifyItem Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQMODIFYITEM) == 0x38u, "FLPACKET_CLIENT_REQMODIFYITEM is operator new(0x38)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQMODIFYITEM, Wire) == 0x20u, "CLIENT_REQMODIFYITEM: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQMODIFYITEM, Wire.Hardpoint) == 0x34u, "CLIENT_REQMODIFYITEM: +0x34");

// FLPACKET_CLIENT_REQCARGO -- id 0x30017, registered at 06d9f165.
// Factory FUN_06d9f940: operator new(0x24) + ctor FUN_06da06a0. Vtable .rdata:06db3dfc =
// { 06d95d00, 06d916a0, 06d95bf0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQCARGO : public FRpcEquipListPacket {
public:
    FPacket_FLPACKET_CLIENT_REQCARGO() : FRpcEquipListPacket(0x30017u) {
        EquipHandle = nullptr;                  // +0x20
    }
    // FUN_06d95cf0
    ~FPacket_FLPACKET_CLIENT_REQCARGO() override {
        // Own vptr store, then JMP FUN_06da21d0 -- ~FRpcEquipListPacket.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06d916a0
        return RpcEncodeWireEquipList(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95bf0
        return RpcDecodeWireEquipList(*this, Buffer, Size, OutLength);
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQCARGO) == 0x24u, "FLPACKET_CLIENT_REQCARGO is operator new(0x24)");

// FLPACKET_CLIENT_REQADDITEM -- id 0x30018, registered at 06d9f17b.
// Factory FUN_06d9f8c0: operator new(0x38) + ctor FUN_06da0620. Vtable .rdata:06db4054 =
// { 06d99d70, 06d99d90, 06d99f80, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQADDITEM : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQADDITEM() : FPacketBase(0x30018u) {
        Wire.ArchId = 0;                        // +0x20
        Wire.Count = 0;                         // +0x24
        Wire.HardpointSize = 0;                 // +0x28
        Wire.Health = 0.0f;                     // +0x2c
        Wire.bMounted = 0;                      // +0x30
        Wire.Hardpoint = nullptr;               // +0x34
    }
    // FUN_06d99d40
    ~FPacket_FLPACKET_CLIENT_REQADDITEM() override {
        if (Wire.Hardpoint != nullptr) {   // 06d99d43..06d99d51
            ::operator delete(Wire.Hardpoint);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d99d90
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d99f80
    FPayloadClientReqAddItem Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQADDITEM) == 0x38u, "FLPACKET_CLIENT_REQADDITEM is operator new(0x38)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQADDITEM, Wire) == 0x20u, "CLIENT_REQADDITEM: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQADDITEM, Wire.Hardpoint) == 0x34u, "CLIENT_REQADDITEM: +0x34");

// FLPACKET_CLIENT_REQSHIPARCH -- id 0x30015, registered at 06d9f191.
// Factory FUN_06d9f9c0: operator new(0x24) + ctor FUN_06da0730. Vtable .rdata:06db5b34 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQSHIPARCH : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQSHIPARCH() : FPacketBase(0x30015u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQSHIPARCH) == 0x24u, "FLPACKET_CLIENT_REQSHIPARCH is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQSHIPARCH, Wire) == 0x20u, "CLIENT_REQSHIPARCH: payload at +0x20");

// FLPACKET_CLIENT_REQEQUIPMENT -- id 0x30016, registered at 06d9f1a7.
// Factory FUN_06d9f980: operator new(0x24) + ctor FUN_06da0700. Vtable .rdata:06db3de0 =
// { 06d95bd0, 06d916a0, 06d95bf0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQEQUIPMENT : public FRpcEquipListPacket {
public:
    FPacket_FLPACKET_CLIENT_REQEQUIPMENT() : FRpcEquipListPacket(0x30016u) {
        EquipHandle = nullptr;                  // +0x20
    }
    // FUN_06d95bc0
    ~FPacket_FLPACKET_CLIENT_REQEQUIPMENT() override {
        // Own vptr store, then JMP FUN_06da21d0 -- ~FRpcEquipListPacket.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06d916a0
        return RpcEncodeWireEquipList(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95bf0
        return RpcDecodeWireEquipList(*this, Buffer, Size, OutLength);
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQEQUIPMENT) == 0x24u, "FLPACKET_CLIENT_REQEQUIPMENT is operator new(0x24)");

// FLPACKET_CLIENT_MUNCOLLISION -- id 0x30003, registered at 06d9f1bd.
// Factory FUN_06d9fbc0: operator new(0x3c) + ctor FUN_06da0790. Vtable .rdata:06db5b54 =
// { 06d9af50, 06dae280, 06dae430, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_MUNCOLLISION : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_MUNCOLLISION() : FPacketBase(0x30003u) {
        Wire.ProjectileArchId = 0;              // +0x20
        Wire.AttackerId = 0;                    // +0x24
        Wire.ObjectId = 0;                      // +0x28
        Wire.SubObjId = SUBOBJ_ID_NONE;         // +0x2c; COMMON.DLL SUBOBJ_ID_NONE
        Wire.HitPosX = 0;                       // +0x30
        Wire.HitPosY = 0;                       // +0x34
        Wire.HitPosZ = 0;                       // +0x38
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dae280
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dae430
    FPayloadClientMunCollision Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_MUNCOLLISION) == 0x3cu, "FLPACKET_CLIENT_MUNCOLLISION is operator new(0x3c)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_MUNCOLLISION, Wire) == 0x20u, "CLIENT_MUNCOLLISION: payload at +0x20");

// FLPACKET_CLIENT_BADLANDSOBJCOLLISION -- id 0x3003d, registered at 06d9f1d3.
// Factory FUN_06d9fc00: operator new(0x2c) + ctor FUN_06da07e0. Vtable .rdata:06db5b64 =
// { 06d9af50, 06dae620, 06da78d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_BADLANDSOBJCOLLISION : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_BADLANDSOBJCOLLISION() : FPacketBase(0x3003du) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.SubObjId = 0;                      // +0x24
        Wire.BadLandsObjId = 0;                 // +0x28
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dae620
        return RpcEncodeWireDwordWordDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da78d0
        return RpcDecodeWireDwordWordDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientBadLandsObjCollision Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_BADLANDSOBJCOLLISION) == 0x2cu, "FLPACKET_CLIENT_BADLANDSOBJCOLLISION is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_BADLANDSOBJCOLLISION, Wire) == 0x20u, "CLIENT_BADLANDSOBJCOLLISION: payload at +0x20");

// FLPACKET_CLIENT_OBJCOLLISION -- id 0x3000c, registered at 06d9f1e9.
// Factory FUN_06d9fc40: operator new(0x34) + ctor FUN_06da0810. Vtable .rdata:06db5b74 =
// { 06d9af50, 06dae770, 06dae910, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_OBJCOLLISION : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_OBJCOLLISION() : FPacketBase(0x3000cu) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.SubObjId = 0;                      // +0x24
        Wire.TargetObjectId = 0;                // +0x28
        Wire.TargetSubObjId = 0;                // +0x2c
        Wire.DamageBits = 0;                    // +0x30
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dae770
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dae910
    FPayloadClientObjCollision Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_OBJCOLLISION) == 0x34u, "FLPACKET_CLIENT_OBJCOLLISION is operator new(0x34)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_OBJCOLLISION, Wire) == 0x20u, "CLIENT_OBJCOLLISION: payload at +0x20");

// FLPACKET_CLIENT_REQUEST_EVENT -- id 0x30031, registered at 06d9f1ff.
// Factory FUN_06d9fc80: operator new(0x34) + ctor FUN_06da1050. Vtable .rdata:06db5dd4 =
// { 06d9af50, 06daa240, 06daa3e0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQUEST_EVENT : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQUEST_EVENT() : FPacketBase(0x30031u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daa240
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daa3e0
    FPayloadClientRequestEvent Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUEST_EVENT) == 0x34u, "FLPACKET_CLIENT_REQUEST_EVENT is operator new(0x34)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQUEST_EVENT, Wire) == 0x20u, "CLIENT_REQUEST_EVENT: payload at +0x20");

// FLPACKET_CLIENT_REQUEST_CANCEL -- id 0x30032, registered at 06d9f215.
// Factory FUN_06d9fcc0: operator new(0x30) + ctor FUN_06da1080. Vtable .rdata:06db5de4 =
// { 06d9af50, 06daa560, 06daa6d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQUEST_CANCEL : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQUEST_CANCEL() : FPacketBase(0x30032u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daa560
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daa6d0
    FPayloadClientRequestCancel Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUEST_CANCEL) == 0x30u, "FLPACKET_CLIENT_REQUEST_CANCEL is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQUEST_CANCEL, Wire) == 0x20u, "CLIENT_REQUEST_CANCEL: payload at +0x20");

// FLPACKET_CLIENT_CARGOSCAN -- id 0x3002b, registered at 06d9f22b.
// Factory FUN_06d9fd00: operator new(0x28) + ctor FUN_06da06d0. Vtable .rdata:06db5b24 =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_CARGOSCAN : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_CARGOSCAN() : FPacketBase(0x3002bu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadClientScanCargo Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_CARGOSCAN) == 0x28u, "FLPACKET_CLIENT_CARGOSCAN is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_CARGOSCAN, Wire) == 0x20u, "CLIENT_CARGOSCAN: payload at +0x20");

// FLPACKET_CLIENT_REQUESTLAUNCH -- id 0x30004, registered at 06d9f241.
// Factory FUN_06d9ffc0: operator new(0x24) + ctor FUN_06da0a30. Vtable .rdata:06db5c14 =
// { 06d9af50, 06db0310, 06db03f0, 06daf370 }.
class FPacket_FLPACKET_CLIENT_REQUESTLAUNCH : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQUESTLAUNCH() : FPacketBase(0x30004u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    void Dump(std::ostream& Out) const override;   // 06daf370
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUESTLAUNCH) == 0x24u, "FLPACKET_CLIENT_REQUESTLAUNCH is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQUESTLAUNCH, Wire) == 0x20u, "CLIENT_REQUESTLAUNCH: payload at +0x20");

// FLPACKET_CLIENT_REQUESTCHARINFO -- id 0x30005, registered at 06d9f257.
// Factory FUN_06da0040: operator new(0x20) + ctor FUN_06da0af0. Vtable .rdata:06db5c44 =
// { 06d9af50, 06da13d0, 06da1470, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQUESTCHARINFO : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQUESTCHARINFO() : FPacketBase(0x30005u) {
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUESTCHARINFO) == 0x20u, "FLPACKET_CLIENT_REQUESTCHARINFO is operator new(0x20)");

// FLPACKET_CLIENT_SKIP_AUTOSAVE -- id 0x30042, registered at 06d9f26d.
// Factory FUN_06da0080: operator new(0x20) + ctor FUN_06da0b20. Vtable .rdata:06db5c54 =
// { 06d9af50, 06da13d0, 06da1470, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_SKIP_AUTOSAVE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_SKIP_AUTOSAVE() : FPacketBase(0x30042u) {
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_SKIP_AUTOSAVE) == 0x20u, "FLPACKET_CLIENT_SKIP_AUTOSAVE is operator new(0x20)");

// FLPACKET_CLIENT_SELECTCHARACTER -- id 0x30006, registered at 06d9f283.
// Factory FUN_06da00c0: operator new(0x220) + ctor FUN_06da0be0. Vtable .rdata:06db5c94 =
// { 06d9af50, 06daf830, 06daf970, 06dafab0 }.
class FPacket_FLPACKET_CLIENT_SELECTCHARACTER : public FRpcCharacterIdPacket {
public:
    FPacket_FLPACKET_CLIENT_SELECTCHARACTER() : FRpcCharacterIdPacket(0x30006u) {
        CharacterId[0] = 0;                     // +0x20
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_SELECTCHARACTER) == 0x220u, "FLPACKET_CLIENT_SELECTCHARACTER is operator new(0x220)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_SELECTCHARACTER, CharacterId) == 0x20u, "CLIENT_SELECTCHARACTER: payload at +0x20");

// FLPACKET_CLIENT_COMMCOMPLETE -- id 0x30037, registered at 06d9f299.
// Factory FUN_06da0100: operator new(0x30) + ctor FUN_06da0a90. Vtable .rdata:06db5c24 =
// { 06d9af50, 06daffc0, 06d9ab00, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_COMMCOMPLETE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_COMMCOMPLETE() : FPacketBase(0x30037u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06daffc0
        return RpcEncodeWireFourDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d9ab00
        return RpcDecodeWireFourDwords(*this, Buffer, Size, OutLength);
    }
    FPayloadClientCommComplete Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_COMMCOMPLETE) == 0x30u, "FLPACKET_CLIENT_COMMCOMPLETE is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_COMMCOMPLETE, Wire) == 0x20u, "CLIENT_COMMCOMPLETE: payload at +0x20");

// FLPACKET_CLIENT_REQUESTNEWCHARINFO -- id 0x30038, registered at 06d9f2af.
// Factory FUN_06da0140: operator new(0x20) + ctor FUN_06da0b50. Vtable .rdata:06db5c64 =
// { 06d9af50, 06da13d0, 06da1470, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQUESTNEWCHARINFO : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQUESTNEWCHARINFO() : FPacketBase(0x30038u) {
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUESTNEWCHARINFO) == 0x20u, "FLPACKET_CLIENT_REQUESTNEWCHARINFO is operator new(0x20)");

// FLPACKET_CLIENT_CREATENEWCHAR -- id 0x30039, registered at 06d9f2c5.
// Factory FUN_06da0180: operator new(0xb0) + ctor FUN_06da0b80. Vtable .rdata:06db5c74 =
// { 06d9af50, 06daf410, 06daf5b0, 06daf760 }.
class FPacket_FLPACKET_CLIENT_CREATENEWCHAR : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_CREATENEWCHAR() : FPacketBase(0x30039u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daf410
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daf5b0
    void Dump(std::ostream& Out) const override;   // 06daf760
    FPayloadClientCreateNewChar Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_CREATENEWCHAR) == 0xb0u, "FLPACKET_CLIENT_CREATENEWCHAR is operator new(0xb0)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_CREATENEWCHAR, Wire) == 0x20u, "CLIENT_CREATENEWCHAR: payload at +0x20");

// FLPACKET_CLIENT_DESTROYCHAR -- id 0x3003a, registered at 06d9f2db.
// Factory FUN_06da01c0: operator new(0x220) + ctor FUN_06da0bb0. Vtable .rdata:06db5c84 =
// { 06d9af50, 06daf830, 06daf970, 06dafab0 }.
class FPacket_FLPACKET_CLIENT_DESTROYCHAR : public FRpcCharacterIdPacket {
public:
    FPacket_FLPACKET_CLIENT_DESTROYCHAR() : FRpcCharacterIdPacket(0x3003au) {
        CharacterId[0] = 0;                     // +0x20
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DESTROYCHAR) == 0x220u, "FLPACKET_CLIENT_DESTROYCHAR is operator new(0x220)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DESTROYCHAR, CharacterId) == 0x20u, "CLIENT_DESTROYCHAR: payload at +0x20");

// FLPACKET_CLIENT_LOGIN -- id 0x30001, registered at 06d9f2f1.
// Factory FUN_06da0200: operator new(0x424) + ctor FUN_06da0c10. Vtable .rdata:06db5ca4 =
// { 06d9af50, 06dafb10, 06dafd40, 06daff50 }.
class FPacket_FLPACKET_CLIENT_LOGIN : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_LOGIN() : FPacketBase(0x30001u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dafb10
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dafd40
    void Dump(std::ostream& Out) const override;   // 06daff50
    FPayloadClientLogin Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_LOGIN) == 0x424u, "FLPACKET_CLIENT_LOGIN is operator new(0x424)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_LOGIN, Wire) == 0x20u, "CLIENT_LOGIN: payload at +0x20");

// FLPACKET_CLIENT_ENTERBASE -- id 0x30007, registered at 06d9f307.
// Factory FUN_06d9fec0: operator new(0x24) + ctor FUN_06da0970. Vtable .rdata:06db5bd4 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_ENTERBASE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_ENTERBASE() : FPacketBase(0x30007u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_ENTERBASE) == 0x24u, "FLPACKET_CLIENT_ENTERBASE is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_ENTERBASE, Wire) == 0x20u, "CLIENT_ENTERBASE: payload at +0x20");

// FLPACKET_CLIENT_REQUESTBASEINFO -- id 0x30008, registered at 06d9f31d.
// Factory FUN_06d9ff40: operator new(0x28) + ctor FUN_06da09d0. Vtable .rdata:06db5bf4 =
// { 06d9af50, 06dab0e0, 06daf230, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQUESTBASEINFO : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQUESTBASEINFO() : FPacketBase(0x30008u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dab0e0
        return RpcEncodeWireDwordByte(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06daf230
        return RpcDecodeWireDwordByte(*this, Buffer, Size, OutLength);
    }
    FPayloadClientInfoRequest Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUESTBASEINFO) == 0x28u, "FLPACKET_CLIENT_REQUESTBASEINFO is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQUESTBASEINFO, Wire) == 0x20u, "CLIENT_REQUESTBASEINFO: payload at +0x20");

// FLPACKET_CLIENT_REQUESTLOCATIONINFO -- id 0x30009, registered at 06d9f333.
// Factory FUN_06d9ff80: operator new(0x28) + ctor FUN_06da0a00. Vtable .rdata:06db5c04 =
// { 06d9af50, 06dab0e0, 06daf230, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQUESTLOCATIONINFO : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQUESTLOCATIONINFO() : FPacketBase(0x30009u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dab0e0
        return RpcEncodeWireDwordByte(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06daf230
        return RpcDecodeWireDwordByte(*this, Buffer, Size, OutLength);
    }
    FPayloadClientInfoRequest Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUESTLOCATIONINFO) == 0x28u, "FLPACKET_CLIENT_REQUESTLOCATIONINFO is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQUESTLOCATIONINFO, Wire) == 0x20u, "CLIENT_REQUESTLOCATIONINFO: payload at +0x20");

// FLPACKET_CLIENT_GFREQUESTSHIPINFO -- id 0x3000a, registered at 06d9f349.
// Factory FUN_06da0240: operator new(0x20) + ctor FUN_06da0c40. Vtable .rdata:06db5cb4 =
// { 06d9af50, 06da13d0, 06da1470, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_GFREQUESTSHIPINFO : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_GFREQUESTSHIPINFO() : FPacketBase(0x3000au) {
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_GFREQUESTSHIPINFO) == 0x20u, "FLPACKET_CLIENT_GFREQUESTSHIPINFO is operator new(0x20)");

// FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE -- id 0x3000b, registered at 06d9f35f.
// Factory FUN_06d9fd80: operator new(0x24) + ctor FUN_06da0880. Vtable .rdata:06db5b94 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE() : FPacketBase(0x3000bu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE) == 0x24u, "FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE, Wire) == 0x20u, "CLIENT_SYSTEM_SWITCH_OUT_COMPLETE: payload at +0x20");

// FLPACKET_CLIENT_EXITBASE -- id 0x3000d, registered at 06d9f375.
// Factory FUN_06d9ff00: operator new(0x24) + ctor FUN_06da09a0. Vtable .rdata:06db5be4 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_EXITBASE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_EXITBASE() : FPacketBase(0x3000du) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_EXITBASE) == 0x24u, "FLPACKET_CLIENT_EXITBASE is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_EXITBASE, Wire) == 0x20u, "CLIENT_EXITBASE: payload at +0x20");

// FLPACKET_CLIENT_ENTERLOCATION -- id 0x3000e, registered at 06d9f38b.
// Factory FUN_06d9fe40: operator new(0x24) + ctor FUN_06da0910. Vtable .rdata:06db5bb4 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_ENTERLOCATION : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_ENTERLOCATION() : FPacketBase(0x3000eu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_ENTERLOCATION) == 0x24u, "FLPACKET_CLIENT_ENTERLOCATION is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_ENTERLOCATION, Wire) == 0x20u, "CLIENT_ENTERLOCATION: payload at +0x20");

// FLPACKET_CLIENT_EXITLOCATION -- id 0x3000f, registered at 06d9f3a1.
// Factory FUN_06d9fe80: operator new(0x24) + ctor FUN_06da0940. Vtable .rdata:06db5bc4 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_EXITLOCATION : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_EXITLOCATION() : FPacketBase(0x3000fu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_EXITLOCATION) == 0x24u, "FLPACKET_CLIENT_EXITLOCATION is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_EXITLOCATION, Wire) == 0x20u, "CLIENT_EXITLOCATION: payload at +0x20");

// FLPACKET_CLIENT_REQUESTCREATESHIP -- id 0x30010, registered at 06d9f3b7.
// Factory FUN_06d9fa00: operator new(0x20) + ctor FUN_06da0760. Vtable .rdata:06db5b44 =
// { 06d9af50, 06da13d0, 06da1470, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQUESTCREATESHIP : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQUESTCREATESHIP() : FPacketBase(0x30010u) {
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUESTCREATESHIP) == 0x20u, "FLPACKET_CLIENT_REQUESTCREATESHIP is operator new(0x20)");

// FLPACKET_CLIENT_INTERFACEITEMUSED -- id 0x30035, registered at 06d9f3cd.
// Factory FUN_06d9fa40: operator new(0x24) + ctor FUN_06da0cd0. Vtable .rdata:06db5ce4 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_INTERFACEITEMUSED : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_INTERFACEITEMUSED() : FPacketBase(0x30035u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_INTERFACEITEMUSED) == 0x24u, "FLPACKET_CLIENT_INTERFACEITEMUSED is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_INTERFACEITEMUSED, Wire) == 0x20u, "CLIENT_INTERFACEITEMUSED: payload at +0x20");

// FLPACKET_CLIENT_RTCDONE -- id 0x30047, registered at 06d9f3e3.
// Factory FUN_06d9fa80: operator new(0x24) + ctor FUN_06da0d00. Vtable .rdata:06db5cf4 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_RTCDONE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_RTCDONE() : FPacketBase(0x30047u) {
        Wire.Field_0x00 = 0;                    // +0x20
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_RTCDONE) == 0x24u, "FLPACKET_CLIENT_RTCDONE is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_RTCDONE, Wire) == 0x20u, "CLIENT_RTCDONE: payload at +0x20");

// FLPACKET_CLIENT_ABORT_MISSION -- id 0x30041, registered at 06d9f3f9.
// Factory FUN_06d9fac0: operator new(0x24) + ctor FUN_06da0d30. Vtable .rdata:06db5d04 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_ABORT_MISSION : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_ABORT_MISSION() : FPacketBase(0x30041u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_ABORT_MISSION) == 0x24u, "FLPACKET_CLIENT_ABORT_MISSION is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_ABORT_MISSION, Wire) == 0x20u, "CLIENT_ABORT_MISSION: payload at +0x20");

// FLPACKET_CLIENT_LAUNCHCOMPLETE -- id 0x3003e, registered at 06d9f40f.
// Factory FUN_06d9fb00: operator new(0x28) + ctor FUN_06da0d60. Vtable .rdata:06db5d14 =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_LAUNCHCOMPLETE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_LAUNCHCOMPLETE() : FPacketBase(0x3003eu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadClientLaunchComplete Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_LAUNCHCOMPLETE) == 0x28u, "FLPACKET_CLIENT_LAUNCHCOMPLETE is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_LAUNCHCOMPLETE, Wire) == 0x20u, "CLIENT_LAUNCHCOMPLETE: payload at +0x20");

// FLPACKET_CLIENT_JUMPINCOMPLETE -- id 0x30043, registered at 06d9f425.
// Factory FUN_06d9fb40: operator new(0x28) + ctor FUN_06da0d90. Vtable .rdata:06db5d24 =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_JUMPINCOMPLETE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_JUMPINCOMPLETE() : FPacketBase(0x30043u) {
        Wire.SystemId = 0;                      // +0x20
        Wire.ShipId = 0;                        // +0x24
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    FPayloadClientJumpInComplete Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_JUMPINCOMPLETE) == 0x28u, "FLPACKET_CLIENT_JUMPINCOMPLETE is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_JUMPINCOMPLETE, Wire) == 0x20u, "CLIENT_JUMPINCOMPLETE: payload at +0x20");

// FLPACKET_CLIENT_HAIL -- id 0x3003f, registered at 06d9f43b.
// Factory FUN_06d9fb80: operator new(0x2c) + ctor FUN_06da1180. Vtable .rdata:06db5e14 =
// { 06d9af50, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_HAIL : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_HAIL() : FPacketBase(0x3003fu) {
        Wire.ShipId = 0;                        // +0x20
        Wire.TargetId = 0;                      // +0x24
        Wire.SystemId = 0;                      // +0x28
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    FPayloadClientHail Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_HAIL) == 0x2cu, "FLPACKET_CLIENT_HAIL is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_HAIL, Wire) == 0x20u, "CLIENT_HAIL: payload at +0x20");

// FLPACKET_CLIENT_GFGOODBUY -- id 0x30012, registered at 06d9f451.
// Factory FUN_06da0280: operator new(0x30) + ctor FUN_06da0c70. Vtable .rdata:06db5cc4 =
// { 06d9af50, 06daffc0, 06d9ab00, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_GFGOODBUY : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_GFGOODBUY() : FPacketBase(0x30012u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06daffc0
        return RpcEncodeWireFourDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d9ab00
        return RpcDecodeWireFourDwords(*this, Buffer, Size, OutLength);
    }
    FPayloadClientGFGoodBuy Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_GFGOODBUY) == 0x30u, "FLPACKET_CLIENT_GFGOODBUY is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_GFGOODBUY, Wire) == 0x20u, "CLIENT_GFGOODBUY: payload at +0x20");

// FLPACKET_CLIENT_GFGOODSELL -- id 0x30011, registered at 06d9f467.
// Factory FUN_06da02c0: operator new(0x2c) + ctor FUN_06da0ca0. Vtable .rdata:06db5cd4 =
// { 06d9af50, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_GFGOODSELL : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_GFGOODSELL() : FPacketBase(0x30011u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDwordTriple Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_GFGOODSELL) == 0x2cu, "FLPACKET_CLIENT_GFGOODSELL is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_GFGOODSELL, Wire) == 0x20u, "CLIENT_GFGOODSELL: payload at +0x20");

// FLPACKET_CLIENT_GFGOODVAPORIZED -- id 0x3003c, registered at 06d9f47d.
// Factory FUN_06da0300: operator new(0x2c) + ctor FUN_06da0dc0. Vtable .rdata:06db5d34 =
// { 06d9af50, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_GFGOODVAPORIZED : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_GFGOODVAPORIZED() : FPacketBase(0x3003cu) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDwordTriple Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_GFGOODVAPORIZED) == 0x2cu, "FLPACKET_CLIENT_GFGOODVAPORIZED is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_GFGOODVAPORIZED, Wire) == 0x20u, "CLIENT_GFGOODVAPORIZED: payload at +0x20");

// FLPACKET_CLIENT_GFSELECTOBJECT -- id 0x30013, registered at 06d9f493.
// Factory FUN_06da0340: operator new(0x24) + ctor FUN_06da0df0. Vtable .rdata:06db5d44 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_GFSELECTOBJECT : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_GFSELECTOBJECT() : FPacketBase(0x30013u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_GFSELECTOBJECT) == 0x24u, "FLPACKET_CLIENT_GFSELECTOBJECT is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_GFSELECTOBJECT, Wire) == 0x20u, "CLIENT_GFSELECTOBJECT: payload at +0x20");

// FLPACKET_CLIENT_SAVEGAME -- id 0x3001e, registered at 06d9f4a9.
// Factory FUN_06da0000: operator new(0x228) + ctor FUN_06da0a60. Vtable .rdata:06db40a8 =
// { 06d9ac90, 06d9a800, 06d9a950, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_SAVEGAME : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_SAVEGAME() : FPacketBase(0x3001eu) {
        Wire.CharId[0] = 0;                     // +0x20
    }
    // FUN_06d9a7d0
    ~FPacket_FLPACKET_CLIENT_SAVEGAME() override {
        if (Wire.Description != nullptr) {   // 06d9a7d3..06d9a7e4
            ::operator delete(Wire.Description);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d9a800
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d9a950
    FPayloadClientSaveGame Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_SAVEGAME) == 0x228u, "FLPACKET_CLIENT_SAVEGAME is operator new(0x228)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_SAVEGAME, Wire) == 0x20u, "CLIENT_SAVEGAME: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_SAVEGAME, Wire.Description) == 0x220u, "CLIENT_SAVEGAME: +0x220");

// FLPACKET_CLIENT_MINEASTEROID -- id 0x30020, registered at 06d9f4bf.
// Factory FUN_06d9fd40: operator new(0x3c) + ctor FUN_06da0850. Vtable .rdata:06db5b84 =
// { 06d9af50, 06daeac0, 06daeca0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_MINEASTEROID : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_MINEASTEROID() : FPacketBase(0x30020u) {
        Wire.PositionX = 0.0f;                  // +0x24
        Wire.PositionY = 0.0f;                  // +0x28
        Wire.PositionZ = 0.0f;                  // +0x2c
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daeac0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daeca0
    FPayloadClientMineAsteroid Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_MINEASTEROID) == 0x3cu, "FLPACKET_CLIENT_MINEASTEROID is operator new(0x3c)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_MINEASTEROID, Wire) == 0x20u, "CLIENT_MINEASTEROID: payload at +0x20");

// FLPACKET_CLIENT_MISSIONRESPONSE -- id 0x30014, registered at 06d9f4d5.
// Factory FUN_06d9fdc0: operator new(0x2c) + ctor FUN_06da08b0. Vtable .rdata:06db5ba4 =
// { 06d9af50, 06daeec0, 06daf010, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_MISSIONRESPONSE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_MISSIONRESPONSE() : FPacketBase(0x30014u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daeec0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daf010
    FPayloadClientMissionResponse Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_MISSIONRESPONSE) == 0x2cu, "FLPACKET_CLIENT_MISSIONRESPONSE is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_MISSIONRESPONSE, Wire) == 0x20u, "CLIENT_MISSIONRESPONSE: payload at +0x20");

// FLPACKET_CLIENT_TRADERESPONSE -- id 0x30027, registered at 06d9f4eb.
// Factory FUN_06d9fe00: operator new(0x28) + ctor FUN_06da08e0. Vtable .rdata:06db408c =
// { 06d9a7b0, 06da9110, 06d9a660, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_TRADERESPONSE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_TRADERESPONSE() : FPacketBase(0x30027u) {
        Wire.Size = 0;                          // +0x20
        Wire.Buffer = nullptr;                  // +0x24
    }
    // FUN_06d9a630
    ~FPacket_FLPACKET_CLIENT_TRADERESPONSE() override {
        ::operator delete(Wire.Buffer);   // no test
        Wire.Buffer = nullptr;
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d9a660
    FPayloadClientBlob Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_TRADERESPONSE) == 0x28u, "FLPACKET_CLIENT_TRADERESPONSE is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_TRADERESPONSE, Wire) == 0x20u, "CLIENT_TRADERESPONSE: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_TRADERESPONSE, Wire.Buffer) == 0x24u, "CLIENT_TRADERESPONSE: +0x24");

// FLPACKET_CLIENT_DBGCREATESHIP -- id 0x30022, registered at 06d9f501.
// Factory FUN_06d9f680: operator new(0x90) + ctor FUN_06da0e20. Vtable .rdata:06db5d54 =
// { 06da0ea0, 06db0560, 06db0b30, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DBGCREATESHIP : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DBGCREATESHIP() : FPacketBase(0x30022u) {
        Wire.Field_0x14.x = 0.0f;               // +0x34
        Wire.Field_0x14.y = 0.0f;               // +0x38
        Wire.Field_0x14.z = 0.0f;               // +0x3c
        Wire.Field_0x20.x = 0.0f;               // +0x40
        Wire.Field_0x20.y = 0.0f;               // +0x44
        Wire.Field_0x20.z = 0.0f;               // +0x48
        Wire.Orientation.w = 1.0f;              // +0x4c
        Wire.Orientation.x = 0.0f;              // +0x50
        Wire.Orientation.y = 0.0f;              // +0x54
        Wire.Orientation.z = 0.0f;              // +0x58
        Wire.Field_0x3c = 0;                    // +0x5c
        Wire.NameChars = nullptr;               // +0x60
        Wire.NameCharCount = 0;                 // +0x64
        Wire.BlockASize = 0;                    // +0x68
        Wire.BlockBSize = 0;                    // +0x6c
        Wire.DwordArray = nullptr;              // +0x84
        Wire.DwordArrayCount = 0;               // +0x80
        Wire.Field_0x5c = 0;                    // +0x7c
        Wire.Field_0x58 = 0;                    // +0x78
        Wire.Field_0x54 = 0;                    // +0x74
        Wire.Field_0x50 = 0;                    // +0x70
        Wire.BlockA = nullptr;                  // +0x88
        Wire.BlockB = nullptr;                  // +0x8c
    }
    // FUN_06db0500
    ~FPacket_FLPACKET_CLIENT_DBGCREATESHIP() override {
        if (Wire.NameChars != nullptr) {   // 06db0503..06db0511
            ::operator delete(Wire.NameChars);
        }
        if (Wire.BlockA != nullptr) {   // 06db0519..06db0524
            ::operator delete(Wire.BlockA);
        }
        if (Wire.BlockB != nullptr) {   // 06db052c..06db0537
            ::operator delete(Wire.BlockB);
        }
        if (Wire.DwordArray != nullptr) {   // 06db053f..06db054a
            ::operator delete(Wire.DwordArray);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06db0560
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06db0b30
    FPayloadClientDbgCreateShip Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGCREATESHIP) == 0x90u, "FLPACKET_CLIENT_DBGCREATESHIP is operator new(0x90)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DBGCREATESHIP, Wire) == 0x20u, "CLIENT_DBGCREATESHIP: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DBGCREATESHIP, Wire.NameChars) == 0x60u, "CLIENT_DBGCREATESHIP: +0x60");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DBGCREATESHIP, Wire.BlockA) == 0x88u, "CLIENT_DBGCREATESHIP: +0x88");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DBGCREATESHIP, Wire.BlockB) == 0x8cu, "CLIENT_DBGCREATESHIP: +0x8c");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DBGCREATESHIP, Wire.DwordArray) == 0x84u, "CLIENT_DBGCREATESHIP: +0x84");

// FLPACKET_CLIENT_DBGLOADSYSTEM -- id 0x30023, registered at 06d9f517.
// Factory FUN_06d9f6c0: operator new(0x28) + ctor FUN_06da0ec0. Vtable .rdata:06db5d64 =
// { 06da0ef0, 06da6190, 06db10e0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM() : FPacketBase(0x30023u) {
        Wire.Buffer = nullptr;                  // +0x20
        Wire.Size = 0;                          // +0x24
    }
    // FUN_06db10b0
    ~FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM() override {
        if (Wire.Buffer != nullptr) {   // 06db10b3..06db10c1
            ::operator delete(Wire.Buffer);
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06db10e0
    FPayloadServerBlobReversed Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM) == 0x28u, "FLPACKET_CLIENT_DBGLOADSYSTEM is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM, Wire) == 0x20u, "CLIENT_DBGLOADSYSTEM: payload at +0x20");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM, Wire.Buffer) == 0x20u, "CLIENT_DBGLOADSYSTEM: +0x20");

// FLPACKET_CLIENT_DBGFREESYSTEM -- id 0x3002e, registered at 06d9f52d.
// Factory FUN_06d9f700: operator new(0x20) + ctor FUN_06da0f10. Vtable .rdata:06db5d74 =
// { 06da0f40, 06dae0d0, 06dabad0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DBGFREESYSTEM : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DBGFREESYSTEM() : FPacketBase(0x3002eu) {
    }
    // FUN_06db1250
    ~FPacket_FLPACKET_CLIENT_DBGFREESYSTEM() override {
        // Own vptr store, then straight into CDAPacket's dtor: nothing to free.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dae0d0
        return RpcEncodeWireEmpty(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dabad0
        return RpcDecodeWireEmpty(*this, Buffer, Size, OutLength);
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGFREESYSTEM) == 0x20u, "FLPACKET_CLIENT_DBGFREESYSTEM is operator new(0x20)");

// FLPACKET_CLIENT_DOCK -- id 0x30024, registered at 06d9f543.
// Factory FUN_06d9f740: operator new(0x24) + ctor FUN_06da0f60. Vtable .rdata:06db5d84 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DOCK : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DOCK() : FPacketBase(0x30024u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DOCK) == 0x24u, "FLPACKET_CLIENT_DOCK is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DOCK, Wire) == 0x20u, "CLIENT_DOCK: payload at +0x20");

// FLPACKET_CLIENT_DBGCONSOLE -- id 0x3002d, registered at 06d9f559.
// Factory FUN_06d9f780: operator new(0xa4) + ctor FUN_06da0ff0. Vtable .rdata:06db5db4 =
// { 06d9af50, 06db1670, 06db17a0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DBGCONSOLE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DBGCONSOLE() : FPacketBase(0x3002du) {
        Wire.Field_0x00 = 0;                    // +0x20
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06db1670
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06db17a0
    FPayloadClientDbgConsole Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGCONSOLE) == 0xa4u, "FLPACKET_CLIENT_DBGCONSOLE is operator new(0xa4)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DBGCONSOLE, Wire) == 0x20u, "CLIENT_DBGCONSOLE: payload at +0x20");

// FLPACKET_CLIENT_DBGDESTROYOBJECT -- id 0x30025, registered at 06d9f56f.
// Factory FUN_06d9f7c0: operator new(0x28) + ctor FUN_06da0f90. Vtable .rdata:06db5d94 =
// { 06d9af50, 06db12c0, 06db13e0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DBGDESTROYOBJECT : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DBGDESTROYOBJECT() : FPacketBase(0x30025u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06db12c0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06db13e0
    FPayloadClientDbgDestroyObject Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGDESTROYOBJECT) == 0x28u, "FLPACKET_CLIENT_DBGDESTROYOBJECT is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DBGDESTROYOBJECT, Wire) == 0x20u, "CLIENT_DBGDESTROYOBJECT: payload at +0x20");

// FLPACKET_CLIENT_DBGRELOCATE_SHIP -- id 0x30030, registered at 06d9f585.
// Factory FUN_06d9f800: operator new(0x30) + ctor FUN_06da0fc0. Vtable .rdata:06db5da4 =
// { 06d9af50, 06db14f0, 06dad820, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DBGRELOCATE_SHIP : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DBGRELOCATE_SHIP() : FPacketBase(0x30030u) {
        Wire.Field_0x04 = 0;                    // +0x24
        Wire.Field_0x08 = 0;                    // +0x28
        Wire.Field_0x0c = 0;                    // +0x2c
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db14f0
        return RpcEncodeWireDwordBlock12(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dad820
        return RpcDecodeWireDwordBlock12(*this, Buffer, Size, OutLength);
    }
    FPayloadServerFourDwords Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGRELOCATE_SHIP) == 0x30u, "FLPACKET_CLIENT_DBGRELOCATE_SHIP is operator new(0x30)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_DBGRELOCATE_SHIP, Wire) == 0x20u, "CLIENT_DBGRELOCATE_SHIP: payload at +0x20");

// FLPACKET_CLIENT_SETMANEUVER -- id 0x3002f, registered at 06d9f59b.
// Factory FUN_06da0380: operator new(0x2c) + ctor FUN_06da1020. Vtable .rdata:06db5dc4 =
// { 06d9af50, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_SETMANEUVER : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_SETMANEUVER() : FPacketBase(0x3002fu) {
        Wire.Field_0x00 = 0;                    // +0x20
        Wire.Field_0x04 = 0;                    // +0x24
        Wire.Field_0x08 = 0;                    // +0x28
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDwordTriple Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_SETMANEUVER) == 0x2cu, "FLPACKET_CLIENT_SETMANEUVER is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_SETMANEUVER, Wire) == 0x20u, "CLIENT_SETMANEUVER: payload at +0x20");

// FLPACKET_CLIENT_REQCOLLISIONGROUPS -- id 0x30036, registered at 06d9f5b1.
// Factory FUN_06da03c0: operator new(0x2c) + ctor FUN_06da10b0. Vtable .rdata:06db5df4 =
// { 06da1130, 06dacca0, 06dacd70, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS : public FRpcCollisionGroupPacket {
public:
    FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS() : FRpcCollisionGroupPacket(0x30036u) {
        RpcPacketBuildListSentinel(&Wire, 0x10u);   // operator new(0x10), Head
    }
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS) == 0x2cu, "FLPACKET_CLIENT_REQCOLLISIONGROUPS is operator new(0x2c)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS, Wire) == 0x20u, "CLIENT_REQCOLLISIONGROUPS: payload at +0x20");

// FLPACKET_CLIENT_REQHULLSTATUS -- id 0x3003b, registered at 06d9f5c7.
// Factory FUN_06da0400: operator new(0x24) + ctor FUN_06da1150. Vtable .rdata:06db5e04 =
// { 06d9af50, 06db1cd0, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQHULLSTATUS : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQHULLSTATUS() : FPacketBase(0x3003bu) {
        Wire.Field_0x00 = 0.0f;                 // +0x20
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db1cd0
        return RpcEncodeWireFloat(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientFloat Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQHULLSTATUS) == 0x24u, "FLPACKET_CLIENT_REQHULLSTATUS is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQHULLSTATUS, Wire) == 0x20u, "CLIENT_REQHULLSTATUS: payload at +0x20");

// FLPACKET_CLIENT_REQUEST_USE_ITEM -- id 0x30040, registered at 06d9f5dd.
// Factory FUN_06da0440: operator new(0x28) + ctor FUN_06da11b0. Vtable .rdata:06db5e24 =
// { 06d9af50, 06db19b0, 06dad690, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQUEST_USE_ITEM : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQUEST_USE_ITEM() : FPacketBase(0x30040u) {
        Wire.ObjectId = 0;                      // +0x20
        Wire.ItemId = 0;                        // +0x24
        Wire.Count = 0;                         // +0x26
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db19b0
        return RpcEncodeWireDwordWordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dad690
        return RpcDecodeWireDwordWordWord(*this, Buffer, Size, OutLength);
    }
    FPayloadClientUseItem Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUEST_USE_ITEM) == 0x28u, "FLPACKET_CLIENT_REQUEST_USE_ITEM is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQUEST_USE_ITEM, Wire) == 0x20u, "CLIENT_REQUEST_USE_ITEM: payload at +0x20");

// FLPACKET_CLIENT_REQINVINCIBILITY -- id 0x30044, registered at 06d9f5f3.
// Factory FUN_06da0480: operator new(0x28) + ctor FUN_06da11f0. Vtable .rdata:06db5e34 =
// { 06d9af50, 06db1b60, 06da6a50, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQINVINCIBILITY : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQINVINCIBILITY() : FPacketBase(0x30044u) {
        Wire.ShipId = 0;                        // +0x20
        Wire.Reason = 0;                        // +0x24
        Wire.bInvincible = 0;                   // +0x25
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db1b60
        return RpcEncodeWireDwordTwoBytes(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6a50
        return RpcDecodeWireDwordTwoBytes(*this, Buffer, Size, OutLength);
    }
    FPayloadClientInvincibility Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQINVINCIBILITY) == 0x28u, "FLPACKET_CLIENT_REQINVINCIBILITY is operator new(0x28)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQINVINCIBILITY, Wire) == 0x20u, "CLIENT_REQINVINCIBILITY: payload at +0x20");

// FLPACKET_CLIENT_MISSIONSAVEB -- id 0x30045, registered at 06d9f609.
// Factory FUN_06da04c0: operator new(0x24) + ctor FUN_06da0ac0. Vtable .rdata:06db5c34 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_MISSIONSAVEB : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_MISSIONSAVEB() : FPacketBase(0x30045u) {
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientDword Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_MISSIONSAVEB) == 0x24u, "FLPACKET_CLIENT_MISSIONSAVEB is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_MISSIONSAVEB, Wire) == 0x20u, "CLIENT_MISSIONSAVEB: payload at +0x20");

// FLPACKET_CLIENT_REQDIFFICULTYSCALE -- id 0x30046, registered at 06d9f61f.
// Factory FUN_06da0500: operator new(0x24) + ctor FUN_06da1220. Vtable .rdata:06db5e44 =
// { 06d9af50, 06db1cd0, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQDIFFICULTYSCALE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQDIFFICULTYSCALE() : FPacketBase(0x30046u) {
        Wire.Field_0x00 = 1.0f;                 // +0x20
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db1cd0
        return RpcEncodeWireFloat(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    FPayloadClientFloat Wire;   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQDIFFICULTYSCALE) == 0x24u, "FLPACKET_CLIENT_REQDIFFICULTYSCALE is operator new(0x24)");
static_assert(offsetof(FPacket_FLPACKET_CLIENT_REQDIFFICULTYSCALE, Wire) == 0x20u, "CLIENT_REQDIFFICULTYSCALE: payload at +0x20");
