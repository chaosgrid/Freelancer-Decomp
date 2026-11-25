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

#include <cstdlib>
#include <new>
#include <ostream>

// A field at a packet-relative offset, exactly as the retail bodies address it.
template<typename T>
inline T& RpcPacketField(FPacketBase* Packet, unsigned int Offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<unsigned char*>(Packet) + Offset);
}

// One node of an MSVCP60 std::list<T*> embedded in a packet: { _Next, _Prev, _Myval }.
struct FRpcPacketListNode {
    FRpcPacketListNode* Next;   // +0x00
    FRpcPacketListNode* Prev;   // +0x04
    void* Value;                // +0x08
};

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
        FRpcEquipWireHandle* Handle = RpcPacketField<FRpcEquipWireHandle*>(this, 0x20u);
        if (Handle != nullptr) {
            Handle->Destroy();              // FUN_06d91340
            ::operator delete(Handle);
        }
    }
    unsigned char EquipBody[0x04];   // +0x20 the handle pointer
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

static_assert(sizeof(FRpcEquipListPacket) == 0x24u, "equipment-list packets are 0x24 bytes");
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
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0x7f7fffffu);
        RpcPacketStoreField(this, 0x28u, 4, 0x7f7fffffu);
        RpcPacketStoreField(this, 0x2cu, 4, 0x7f7fffffu);
        RpcPacketStoreField(this, 0x30u, 1, 0u);   // byte copied from an uninitialised stack slot
        RpcPacketStoreField(this, 0x34u, 4, 0u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
    }
    // FUN_06d9b020
    ~FPacket_FLPACKET_COMMON_FIREWEAPON() override {
        // No test: free, then the three dwords of the hardpoint vector cleared.
        ::operator delete(RpcPacketField<void*>(this, 0x34u));
        RpcPacketField<unsigned int>(this, 0x34u) = 0;
        RpcPacketField<unsigned int>(this, 0x38u) = 0;
        RpcPacketField<unsigned int>(this, 0x3cu) = 0;
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da4f60
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da5550
    unsigned char Body[0x20];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_FIREWEAPON) == 0x40u, "FLPACKET_COMMON_FIREWEAPON is operator new(0x40)");

// FLPACKET_COMMON_UPDATEOBJECT -- id 0x10001, registered at 06d9ad03.
// Factory FUN_06d9aed0: operator new(0x4c) + ctor FUN_06d9af10. Vtable .rdata:06db4378 =
// { 06d9af50, 06da49e0, 06da4ca0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_UPDATEOBJECT : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_UPDATEOBJECT() : FPacketBase(0x10001u) {
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0x3f800000u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
        RpcPacketStoreField(this, 0x40u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da49e0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da4ca0
    unsigned char Body[0x2c];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_UPDATEOBJECT) == 0x4cu, "FLPACKET_COMMON_UPDATEOBJECT is operator new(0x4c)");

// FLPACKET_COMMON_CHATMSG -- id 0x10005, registered at 06d9ad19.
// Factory FUN_06d9b730: operator new(0x30) + ctor FUN_06d9b770. Vtable .rdata:06db4428 =
// { 06d9b7a0, 06da6420, 06da65d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_CHATMSG : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_CHATMSG() : FPacketBase(0x10005u) {
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x20u, 4, 0u);
    }
    // FUN_06d9b7c0
    ~FPacket_FLPACKET_COMMON_CHATMSG() override {
        if (RpcPacketField<void*>(this, 0x2cu) != nullptr) {
            ::operator delete(RpcPacketField<void*>(this, 0x2cu));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da6420
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da65d0
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_CHATMSG) == 0x30u, "FLPACKET_COMMON_CHATMSG is operator new(0x30)");

// FLPACKET_COMMON_ACTIVATEEQUIP -- id 0x10008, registered at 06d9ad2f.
// Factory FUN_06d9b7f0: operator new(0x28) + ctor FUN_06d9b830. Vtable .rdata:06db4438 =
// { 06d9af50, 06da67a0, 06da68f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_ACTIVATEEQUIP : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_ACTIVATEEQUIP() : FPacketBase(0x10008u) {
        RpcPacketStoreField(this, 0x26u, 2, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da67a0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da68f0
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_ACTIVATEEQUIP) == 0x28u, "FLPACKET_COMMON_ACTIVATEEQUIP is operator new(0x28)");

// FLPACKET_COMMON_SETTARGET -- id 0x10004, registered at 06d9ad45.
// Factory FUN_06d9ba20: operator new(0x2c) + ctor FUN_06d9ba60. Vtable .rdata:06db4488 =
// { 06d9af50, 06da8b50, 06da8cd0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_SETTARGET : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_SETTARGET() : FPacketBase(0x10004u) {
        RpcPacketStoreField(this, 0x28u, 2, 0u);
        RpcPacketStoreField(this, 0x2au, 2, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da8b50
        return RpcEncodeWireDwordDwordWordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da8cd0
        return RpcDecodeWireDwordDwordWordWord(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_SETTARGET) == 0x2cu, "FLPACKET_COMMON_SETTARGET is operator new(0x2c)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_ACTIVATECRUISE) == 0x28u, "FLPACKET_COMMON_ACTIVATECRUISE is operator new(0x28)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_ACTIVATETHRUSTERS) == 0x28u, "FLPACKET_COMMON_ACTIVATETHRUSTERS is operator new(0x28)");

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
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_GOTRADELANE) == 0x30u, "FLPACKET_COMMON_GOTRADELANE is operator new(0x30)");

// FLPACKET_COMMON_STOPTRADELANE -- id 0x10010, registered at 06d9ad9d.
// Factory FUN_06d9b9b0: operator new(0x2c) + ctor FUN_06d9b9f0. Vtable .rdata:06db4478 =
// { 06d9af50, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_STOPTRADELANE : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_STOPTRADELANE() : FPacketBase(0x10010u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_STOPTRADELANE) == 0x2cu, "FLPACKET_COMMON_STOPTRADELANE is operator new(0x2c)");

// FLPACKET_COMMON_SET_WEAPON_GROUP -- id 0x10011, registered at 06d9adb3.
// Factory FUN_06d9b050: operator new(0x2c) + ctor FUN_06d9b090. Vtable .rdata:06db4398 =
// { 06d9b0c0, 06da6190, 06da5bc0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP() : FPacketBase(0x10011u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 1, 0u);
    }
    // FUN_06d9b0e0
    ~FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP() override {
        // The byte at +0x28 says whether the packet owns the block at +0x20 -- the
        // IClient-side sender sets it on the CALLER's pointer, the IServerImpl-side one on
        // its own copy, the wire decoder on its own allocation.
        if (RpcPacketField<unsigned char>(this, 0x28u) != 0 &&
            RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d9b0f0
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
            RpcPacketField<void*>(this, 0x20u) = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da5bc0
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP) == 0x2cu, "FLPACKET_COMMON_SET_WEAPON_GROUP is operator new(0x2c)");

// FLPACKET_COMMON_PLAYER_TRADE -- id 0x10012, registered at 06d9adc9.
// Factory FUN_06d9ba90: operator new(0x44) + ctor FUN_06d9bad0. Vtable .rdata:06db4498 =
// { 06d9af50, 06da7030, 06da7430, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_PLAYER_TRADE : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_PLAYER_TRADE() : FPacketBase(0x10012u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0xffffffffu);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 1, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da7030
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da7430
    unsigned char Body[0x24];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_PLAYER_TRADE) == 0x44u, "FLPACKET_COMMON_PLAYER_TRADE is operator new(0x44)");

// FLPACKET_COMMON_SET_VISITED_STATE -- id 0x10013, registered at 06d9addf.
// Factory FUN_06d9b110: operator new(0x2c) + ctor FUN_06d9b150. Vtable .rdata:06db43a8 =
// { 06d9b180, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_SET_VISITED_STATE : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_SET_VISITED_STATE() : FPacketBase(0x10013u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 1, 0u);
    }
    // FUN_06d9b1a0
    ~FPacket_FLPACKET_COMMON_SET_VISITED_STATE() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (RpcPacketField<unsigned char>(this, 0x28u) != 0 &&
            RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d9b1b0
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
            RpcPacketField<void*>(this, 0x20u) = nullptr;
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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_SET_VISITED_STATE) == 0x2cu, "FLPACKET_COMMON_SET_VISITED_STATE is operator new(0x2c)");

// FLPACKET_COMMON_JETTISONCARGO -- id 0x10014, registered at 06d9adf5.
// Factory FUN_06d9bb10: operator new(0x2c) + ctor FUN_06d9bb50. Vtable .rdata:06db44a8 =
// { 06d9af50, 06dae620, 06da78d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_JETTISONCARGO : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_JETTISONCARGO() : FPacketBase(0x10014u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 2, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dae620
        return RpcEncodeWireDwordWordDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da78d0
        return RpcDecodeWireDwordWordDword(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_JETTISONCARGO) == 0x2cu, "FLPACKET_COMMON_JETTISONCARGO is operator new(0x2c)");

// FLPACKET_COMMON_REQUEST_BEST_PATH -- id 0x10016, registered at 06d9ae0b.
// Factory FUN_06d9b1d0: operator new(0x2c) + ctor FUN_06d9b210. Vtable .rdata:06db43b8 =
// { 06d9b240, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH() : FPacketBase(0x10016u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 1, 0u);
    }
    // FUN_06d9b260
    ~FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (RpcPacketField<unsigned char>(this, 0x28u) != 0 &&
            RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d9b270
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
            RpcPacketField<void*>(this, 0x20u) = nullptr;
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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH) == 0x2cu, "FLPACKET_COMMON_REQUEST_BEST_PATH is operator new(0x2c)");

// FLPACKET_COMMON_REQUEST_PLAYER_STATS -- id 0x10018, registered at 06d9ae21.
// Factory FUN_06d9b290: operator new(0x2c) + ctor FUN_06d9b2d0. Vtable .rdata:06db43c8 =
// { 06d9b300, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS() : FPacketBase(0x10018u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 1, 0u);
    }
    // FUN_06d9b320
    ~FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (RpcPacketField<unsigned char>(this, 0x28u) != 0 &&
            RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d9b330
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
            RpcPacketField<void*>(this, 0x20u) = nullptr;
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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS) == 0x2cu, "FLPACKET_COMMON_REQUEST_PLAYER_STATS is operator new(0x2c)");

// FLPACKET_COMMON_POP_UP_DIALOG -- id 0x1001b, registered at 06d9ae37.
// Factory FUN_06d9b350: operator new(0x34) + ctor FUN_06d9b390. Vtable .rdata:06db43d8 =
// { 06d9b3d0, 06da5de0, 06da5fe0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_POP_UP_DIALOG : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_POP_UP_DIALOG() : FPacketBase(0x1001bu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
    }
    // FUN_06d9b3f0
    ~FPacket_FLPACKET_COMMON_POP_UP_DIALOG() override {
        // Retail frees +0x2c FIRST and +0x24 second.
        if (RpcPacketField<void*>(this, 0x2cu) != nullptr) {   // 06d9b3f9..06d9b403
            ::operator delete(RpcPacketField<void*>(this, 0x2cu));
        }
        if (RpcPacketField<void*>(this, 0x24u) != nullptr) {   // 06d9b409..06d9b413
            ::operator delete(RpcPacketField<void*>(this, 0x24u));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da5de0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da5fe0
    unsigned char Body[0x14];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_POP_UP_DIALOG) == 0x34u, "FLPACKET_COMMON_POP_UP_DIALOG is operator new(0x34)");

// FLPACKET_COMMON_REQUEST_GROUP_POSITIONS -- id 0x10017, registered at 06d9ae4d.
// Factory FUN_06d9b430: operator new(0x2c) + ctor FUN_06d9b470. Vtable .rdata:06db43e8 =
// { 06d9b4a0, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS() : FPacketBase(0x10017u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 1, 0u);
    }
    // FUN_06d9b4c0
    ~FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (RpcPacketField<unsigned char>(this, 0x28u) != 0 &&
            RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d9b4d0
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
            RpcPacketField<void*>(this, 0x20u) = nullptr;
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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS) == 0x2cu, "FLPACKET_COMMON_REQUEST_GROUP_POSITIONS is operator new(0x2c)");

// FLPACKET_COMMON_SET_MISSION_LOG -- id 0x10019, registered at 06d9ae63.
// Factory FUN_06d9b4f0: operator new(0x2c) + ctor FUN_06d9b530. Vtable .rdata:06db43f8 =
// { 06d9b560, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_SET_MISSION_LOG : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_SET_MISSION_LOG() : FPacketBase(0x10019u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 1, 0u);
    }
    // FUN_06d9b580
    ~FPacket_FLPACKET_COMMON_SET_MISSION_LOG() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (RpcPacketField<unsigned char>(this, 0x28u) != 0 &&
            RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d9b590
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
            RpcPacketField<void*>(this, 0x20u) = nullptr;
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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_SET_MISSION_LOG) == 0x2cu, "FLPACKET_COMMON_SET_MISSION_LOG is operator new(0x2c)");

// FLPACKET_COMMON_REQUEST_RANK_LEVEL -- id 0x1001a, registered at 06d9ae79.
// Factory FUN_06d9b670: operator new(0x2c) + ctor FUN_06d9b6b0. Vtable .rdata:06db4418 =
// { 06d9b6e0, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL() : FPacketBase(0x1001au) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 1, 0u);
    }
    // FUN_06d9b700
    ~FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (RpcPacketField<unsigned char>(this, 0x28u) != 0 &&
            RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d9b710
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
            RpcPacketField<void*>(this, 0x20u) = nullptr;
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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL) == 0x2cu, "FLPACKET_COMMON_REQUEST_RANK_LEVEL is operator new(0x2c)");

// FLPACKET_COMMON_SET_INTERFACE_STATE -- id 0x1001c, registered at 06d9ae8f.
// Factory FUN_06d9b5b0: operator new(0x2c) + ctor FUN_06d9b5f0. Vtable .rdata:06db4408 =
// { 06d9b620, 06da6190, 06da62d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE() : FPacketBase(0x1001cu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 1, 0u);
    }
    // FUN_06d9b640
    ~FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE() override {
        // +0x28: owns-the-block flag, as in SET_WEAPON_GROUP.
        if (RpcPacketField<unsigned char>(this, 0x28u) != 0 &&
            RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d9b650
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
            RpcPacketField<void*>(this, 0x20u) = nullptr;
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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE) == 0x2cu, "FLPACKET_COMMON_SET_INTERFACE_STATE is operator new(0x2c)");

// FLPACKET_COMMON_TRACTOROBJECTS -- id 0x1001d, registered at 06d9aea5.
// Factory FUN_06d9bb80: operator new(0x38) + ctor FUN_06d9bbc0. Vtable .rdata:06db44b8 =
// { 06d9bc00, 06da7a00, 06da7b90, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_COMMON_TRACTOROBJECTS : public FPacketBase {
public:
    FPacket_FLPACKET_COMMON_TRACTOROBJECTS() : FPacketBase(0x1001du) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 2, 0u);
        RpcPacketStoreField(this, 0x28u, 1, 0u);   // byte copied from an uninitialised stack slot
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0u);
    }
    // FUN_06d9bc20
    ~FPacket_FLPACKET_COMMON_TRACTOROBJECTS() override {
        // No test: free, then the three vector dwords cleared.
        ::operator delete(RpcPacketField<void*>(this, 0x2cu));
        RpcPacketField<unsigned int>(this, 0x2cu) = 0;
        RpcPacketField<unsigned int>(this, 0x30u) = 0;
        RpcPacketField<unsigned int>(this, 0x34u) = 0;
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da7a00
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da7b90
    unsigned char Body[0x18];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_COMMON_TRACTOROBJECTS) == 0x38u, "FLPACKET_COMMON_TRACTOROBJECTS is operator new(0x38)");

// ===========================================================================
// Channel 2 -- the classes registered by FUN_06d9bc80, in its registration order.
// ===========================================================================

// FLPACKET_SERVER_CONNECTRESPONSE -- id 0x20001, registered at 06d9bc8d.
// Factory FUN_06d9c7b0: operator new(0x24) + ctor FUN_06d9c7f0. Vtable .rdata:06db4fec =
// { 06d9af50, 06db0310, 06db03f0, 06da8240 }.
class FPacket_FLPACKET_SERVER_CONNECTRESPONSE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CONNECTRESPONSE() : FPacketBase(0x20001u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CONNECTRESPONSE) == 0x24u, "FLPACKET_SERVER_CONNECTRESPONSE is operator new(0x24)");

// FLPACKET_SERVER_LOGINRESPONSE -- id 0x20002, registered at 06d9bca3.
// Factory FUN_06d9c820: operator new(0x24) + ctor FUN_06d9c860. Vtable .rdata:06db4ffc =
// { 06d9af50, 06da82e0, 06da83c0, 06da84a0 }.
class FPacket_FLPACKET_SERVER_LOGINRESPONSE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_LOGINRESPONSE() : FPacketBase(0x20002u) {
        RpcPacketStoreField(this, 0x20u, 1, 0u);
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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_LOGINRESPONSE) == 0x24u, "FLPACKET_SERVER_LOGINRESPONSE is operator new(0x24)");

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
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 1, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dab0e0
        return RpcEncodeWireDwordByte(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06daf230
        return RpcDecodeWireDwordByte(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_DESTROYOBJECT) == 0x28u, "FLPACKET_SERVER_DESTROYOBJECT is operator new(0x28)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REQUEST_RETURNED) == 0x28u, "FLPACKET_SERVER_REQUEST_RETURNED is operator new(0x28)");

// FLPACKET_SERVER_CHARACTERINFO -- id 0x20003, registered at 06d9bd11.
// Factory FUN_06d9d4b0: operator new(0x30) + ctor FUN_06d9d4f0. Vtable .rdata:06db3dc4 =
// { 06d95b70, 06d949d0, 06d934d0, 06d93310 }.
class FPacket_FLPACKET_SERVER_CHARACTERINFO : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CHARACTERINFO() : FPacketBase(0x20003u) {
        RpcPacketBuildListSentinel(this, 0x24u, 0x0cu);   // operator new(0xc), Head
    }
    // FUN_06d93350
    ~FPacket_FLPACKET_SERVER_CHARACTERINFO() override {
        // Every record first (two id blocks, the collision list, the equipment handle, the
        // two strings, the record), then the nodes through FUN_06d901f0 and the sentinel.
        (reinterpret_cast<FRpcCharacterInfoWireList*>(Payload() + 0x04))->Release();
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d949d0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d934d0
    void Dump(std::ostream& Out) const override;   // 06d93310
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CHARACTERINFO) == 0x30u, "FLPACKET_SERVER_CHARACTERINFO is operator new(0x30)");

// FLPACKET_SERVER_LAUNCH -- id 0x20007, registered at 06d9bd27.
// Factory FUN_06d9d570: operator new(0x48) + ctor FUN_06d9d5b0. Vtable .rdata:06db50ec =
// { 06d9af50, 06daab40, 06daad40, 06daaee0 }.
class FPacket_FLPACKET_SERVER_LAUNCH : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_LAUNCH() : FPacketBase(0x20007u) {
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0u);
        RpcPacketStoreField(this, 0x38u, 4, 0x3f800000u);
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
        RpcPacketStoreField(this, 0x40u, 4, 0u);
        RpcPacketStoreField(this, 0x44u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daab40
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daad40
    void Dump(std::ostream& Out) const override;   // 06daaee0
    unsigned char Body[0x28];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_LAUNCH) == 0x48u, "FLPACKET_SERVER_LAUNCH is operator new(0x48)");

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
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CHARSELECTVERIFIED) == 0x30u, "FLPACKET_SERVER_CHARSELECTVERIFIED is operator new(0x30)");

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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_ACTIVATEOBJECT) == 0x2cu, "FLPACKET_SERVER_ACTIVATEOBJECT is operator new(0x2c)");

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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_LAND) == 0x2cu, "FLPACKET_SERVER_LAND is operator new(0x2c)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST) == 0x24u, "FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST) == 0x24u, "FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFCOMPLETECHARLIST) == 0x24u, "FLPACKET_SERVER_GFCOMPLETECHARLIST is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST) == 0x24u, "FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST) == 0x24u, "FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST is operator new(0x24)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_OUT) == 0x28u, "FLPACKET_SERVER_SYSTEM_SWITCH_OUT is operator new(0x28)");

// FLPACKET_SERVER_SYSTEM_SWITCH_IN -- id 0x20022, registered at 06d9be03.
// Factory FUN_06d9cba0: operator new(0x44) + ctor FUN_06d9cbe0. Vtable .rdata:06db505c =
// { 06d9af50, 06da9260, 06da9430, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_IN : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_IN() : FPacketBase(0x20022u) {
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0x3f800000u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
        RpcPacketStoreField(this, 0x40u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da9260
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da9430
    unsigned char Body[0x24];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_IN) == 0x44u, "FLPACKET_SERVER_SYSTEM_SWITCH_IN is operator new(0x44)");

// FLPACKET_SERVER_SETSHIPARCH -- id 0x20023, registered at 06d9be19.
// Factory FUN_06d9c740: operator new(0x24) + ctor FUN_06d9c780. Vtable .rdata:06db4fdc =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETSHIPARCH : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETSHIPARCH() : FPacketBase(0x20023u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETSHIPARCH) == 0x24u, "FLPACKET_SERVER_SETSHIPARCH is operator new(0x24)");

// FLPACKET_SERVER_SETEQUIPMENT -- id 0x20024, registered at 06d9be2f.
// Factory FUN_06d9c660: operator new(0x24) + ctor FUN_06d9c6a0. Vtable .rdata:06db3cf0 =
// { 06d91320, 06d916a0, 06d95bf0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETEQUIPMENT : public FRpcEquipListPacket {
public:
    FPacket_FLPACKET_SERVER_SETEQUIPMENT() : FRpcEquipListPacket(0x20024u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
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
        RpcPacketStoreField(this, 0x20u, 4, 0u);
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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REQUESTCREATESHIPRESP) == 0x28u, "FLPACKET_SERVER_REQUESTCREATESHIPRESP is operator new(0x28)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETSTARTROOM) == 0x28u, "FLPACKET_SERVER_SETSTARTROOM is operator new(0x28)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFDESTROYCHARACTER) == 0x28u, "FLPACKET_SERVER_GFDESTROYCHARACTER is operator new(0x28)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFDESTROYNEWSBROADCAST) == 0x28u, "FLPACKET_SERVER_GFDESTROYNEWSBROADCAST is operator new(0x28)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER) == 0x28u, "FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER is operator new(0x28)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR) == 0x28u, "FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR is operator new(0x28)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT) == 0x28u, "FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT is operator new(0x28)");

// FLPACKET_SERVER_GFSCRIPTBEHAVIOR -- id 0x2001a, registered at 06d9bef5.
// Factory FUN_06d9e100: operator new(0x28) + ctor FUN_06d9e140. Vtable .rdata:06db3f24 =
// { 06d976e0, 06d97570, 06d978f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR() : FPacketBase(0x2001au) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
    }
    // FUN_06d97540
    ~FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR() override {
        if (RpcPacketField<void*>(this, 0x24u) != nullptr) {   // 06d97543..06d97551
            ::operator delete(RpcPacketField<void*>(this, 0x24u));
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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR) == 0x28u, "FLPACKET_SERVER_GFSCRIPTBEHAVIOR is operator new(0x28)");

// FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT -- id 0x2001f, registered at 06d9bf0b.
// Factory FUN_06d9e250: operator new(0x28) + ctor FUN_06d9e290. Vtable .rdata:06db3f44 =
// { 06d978a0, 06da6190, 06d97730, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT() : FPacketBase(0x2001fu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
    }
    // FUN_06d97700
    ~FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT() override {
        if (RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d97703..06d97711
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d97730
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT) == 0x28u, "FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT is operator new(0x28)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_MISSIONSAVEA) == 0x24u, "FLPACKET_SERVER_MISSIONSAVEA is operator new(0x24)");

// FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER -- id 0x2001d, registered at 06d9bf37.
// Factory FUN_06d9e170: operator new(0x28) + ctor FUN_06d9e1b0. Vtable .rdata:06db3f64 =
// { 06d97a90, 06d97570, 06d978f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER() : FPacketBase(0x2001du) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
    }
    // FUN_06d978c0
    ~FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER() override {
        if (RpcPacketField<void*>(this, 0x24u) != nullptr) {   // 06d978c3..06d978d1
            ::operator delete(RpcPacketField<void*>(this, 0x24u));
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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER) == 0x28u, "FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER is operator new(0x28)");

// FLPACKET_SERVER_GFUPDATENEWSBROADCAST -- id 0x2001e, registered at 06d9bf4d.
// Factory FUN_06d9e1e0: operator new(0x28) + ctor FUN_06d9e220. Vtable .rdata:06db3f94 =
// { 06d97b70, 06d97570, 06d978f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST() : FPacketBase(0x2001eu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
    }
    // FUN_06d97ad0
    ~FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST() override {
        if (RpcPacketField<void*>(this, 0x24u) != nullptr) {   // 06d97ad3..06d97ae1
            ::operator delete(RpcPacketField<void*>(this, 0x24u));
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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST) == 0x28u, "FLPACKET_SERVER_GFUPDATENEWSBROADCAST is operator new(0x28)");

// FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE -- id 0x20020, registered at 06d9bf63.
// Factory FUN_06d9e2c0: operator new(0x28) + ctor FUN_06d9e300. Vtable .rdata:06db3f74 =
// { 06d97ab0, 06d97570, 06d978f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE() : FPacketBase(0x20020u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
    }
    // FUN_06d97a30
    ~FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE() override {
        if (RpcPacketField<void*>(this, 0x24u) != nullptr) {   // 06d97a33..06d97a41
            ::operator delete(RpcPacketField<void*>(this, 0x24u));
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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE) == 0x28u, "FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE is operator new(0x28)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY) == 0x24u, "FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY is operator new(0x24)");

// FLPACKET_SERVER_GFUPDATECHAR -- id 0x20026, registered at 06d9bf8f.
// Factory FUN_06d9dbc0: operator new(0x28) + ctor FUN_06d9dc00. Vtable .rdata:06db3f04 =
// { 06d97520, 06d97570, 06d978f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_GFUPDATECHAR : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_GFUPDATECHAR() : FPacketBase(0x20026u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
    }
    // FUN_06d974c0
    ~FPacket_FLPACKET_SERVER_GFUPDATECHAR() override {
        if (RpcPacketField<void*>(this, 0x24u) != nullptr) {   // 06d974c3..06d974d1
            ::operator delete(RpcPacketField<void*>(this, 0x24u));
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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_GFUPDATECHAR) == 0x28u, "FLPACKET_SERVER_GFUPDATECHAR is operator new(0x28)");

// FLPACKET_SERVER_CREATELOOT -- id 0x20028, registered at 06d9bfa5.
// Factory FUN_06d9d100: operator new(0x34) + ctor FUN_06d9d140. Vtable .rdata:06db50ac =
// { 06d9d1d0, 06d928c0, 06d92d20, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CREATELOOT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CREATELOOT() : FPacketBase(0x20028u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 1, 0u);
        RpcPacketBuildListSentinel(this, 0x28u, 0x50u);   // operator new(0x50), Head
    }
    // FUN_06d9d1f0
    ~FPacket_FLPACKET_SERVER_CREATELOOT() override {
        // 06d9d1f5..06d9d238: the list at +0x28 (Head +0x2c, Size +0x30).
        RpcPacketDestroyEmbeddedList(this, 0x28u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d928c0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d92d20
    unsigned char Body[0x14];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CREATELOOT) == 0x34u, "FLPACKET_SERVER_CREATELOOT is operator new(0x34)");

// FLPACKET_SERVER_SETREPUTATION -- id 0x20029, registered at 06d9bfbb.
// Factory FUN_06d9d250: operator new(0x68) + ctor FUN_06d9d290. Vtable .rdata:06db3d50 =
// { 06d91eb0, 06d91ed0, 06d92350, 06d92860 }.
class FPacket_FLPACKET_SERVER_SETREPUTATION : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETREPUTATION() : FPacketBase(0x20029u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x38u, 4, 0x00000002u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0x00000001u);
        RpcPacketStoreField(this, 0x3cu, 4, 0x00000004u);
        RpcPacketStoreField(this, 0x40u, 4, 0x00000008u);
        RpcPacketStoreField(this, 0x44u, 4, 0x00000010u);
        RpcPacketBuildListSentinel(this, 0x48u, 0x0cu);   // operator new(0xc), Head
        RpcPacketStoreField(this, 0x54u, 1, 0u);
        RpcPacketStoreField(this, 0x58u, 4, 0u);
        RpcPacketStoreField(this, 0x60u, 4, 0u);
        RpcPacketStoreField(this, 0x64u, 4, 0u);
        RpcPacketStoreField(this, 0x5cu, 4, 0xffffffffu);
    }
    // FUN_06d91dd0
    ~FPacket_FLPACKET_SERVER_SETREPUTATION() override {
        // 06d91df6..06d91e1c: free every node's value first, testing each for null.
        FRpcPacketListNode* Sentinel = RpcPacketField<FRpcPacketListNode*>(this, 0x4cu);
        for (FRpcPacketListNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
            if (Node->Value != nullptr) {
                ::operator delete(Node->Value);
            }
        }
        if (RpcPacketField<void*>(this, 0x64u) != nullptr) {   // 06d91e1e..06d91e26
            ::operator delete(RpcPacketField<void*>(this, 0x64u));
        }
        RpcPacketDestroyEmbeddedList(this, 0x48u);   // FUN_06d901f0
        if (RpcPacketField<void*>(this, 0x2cu) != nullptr) {   // 06d91e69..06d91e73
            ::operator delete(RpcPacketField<void*>(this, 0x2cu));
        }
        if (RpcPacketField<void*>(this, 0x24u) != nullptr) {   // 06d91e7b..06d91e83
            ::operator delete(RpcPacketField<void*>(this, 0x24u));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d91ed0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d92350
    void Dump(std::ostream& Out) const override;   // 06d92860
    unsigned char Body[0x48];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETREPUTATION) == 0x68u, "FLPACKET_SERVER_SETREPUTATION is operator new(0x68)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_ADJUSTATTITUDE) == 0x28u, "FLPACKET_SERVER_ADJUSTATTITUDE is operator new(0x28)");

// FLPACKET_SERVER_SETGROUPFEELINGS -- id 0x2002b, registered at 06d9bfe7.
// Factory FUN_06d9c500: operator new(0x34) + ctor FUN_06d9c540. Vtable .rdata:06db3d30 =
// { 06d91830, 06d91890, 06d91a40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETGROUPFEELINGS : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETGROUPFEELINGS() : FPacketBase(0x2002bu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketBuildListSentinel(this, 0x28u, 0x0cu);   // operator new(0xc), Head
    }
    // FUN_06d917a0
    ~FPacket_FLPACKET_SERVER_SETGROUPFEELINGS() override {
        // 06d917a4..06d917c9: free every node's value first, testing each for null.
        FRpcPacketListNode* Sentinel = RpcPacketField<FRpcPacketListNode*>(this, 0x2cu);
        for (FRpcPacketListNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
            if (Node->Value != nullptr) {
                ::operator delete(Node->Value);
            }
        }
        if (RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d917cb..06d917d3
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
        }
        RpcPacketDestroyEmbeddedList(this, 0x28u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d91890
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d91a40
    unsigned char Body[0x14];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETGROUPFEELINGS) == 0x34u, "FLPACKET_SERVER_SETGROUPFEELINGS is operator new(0x34)");

// FLPACKET_SERVER_CREATEMINE -- id 0x2002c, registered at 06d9bffd.
// Factory FUN_06d9cdc0: operator new(0x68) + ctor FUN_06d9ce00. Vtable .rdata:06db506c =
// { 06d9af50, 06da95d0, 06da98d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CREATEMINE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CREATEMINE() : FPacketBase(0x2002cu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 2, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0x3f800000u);
        RpcPacketStoreField(this, 0x34u, 4, 0u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
        RpcPacketStoreField(this, 0x40u, 4, 0u);
        RpcPacketStoreField(this, 0x44u, 4, 0u);
        RpcPacketStoreField(this, 0x48u, 4, 0u);
        RpcPacketStoreField(this, 0x4cu, 4, 0u);
        RpcPacketStoreField(this, 0x50u, 4, 0u);
        RpcPacketStoreField(this, 0x54u, 4, 0u);
        RpcPacketStoreField(this, 0x58u, 4, 0u);
        RpcPacketStoreField(this, 0x5cu, 4, 0u);
        RpcPacketStoreField(this, 0x60u, 4, 0u);
        RpcPacketStoreField(this, 0x64u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da95d0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da98d0
    unsigned char Body[0x48];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CREATEMINE) == 0x68u, "FLPACKET_SERVER_CREATEMINE is operator new(0x68)");

// FLPACKET_SERVER_CREATECOUNTER -- id 0x2002d, registered at 06d9c013.
// Factory FUN_06d9ce60: operator new(0x68) + ctor FUN_06d9cea0. Vtable .rdata:06db507c =
// { 06d9af50, 06da9b50, 06da9e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CREATECOUNTER : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CREATECOUNTER() : FPacketBase(0x2002du) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 2, SUBOBJ_ID_NONE);   // COMMON.DLL SUBOBJ_ID_NONE
        RpcPacketStoreField(this, 0x34u, 4, 0x3f800000u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
        RpcPacketStoreField(this, 0x40u, 4, 0u);
        RpcPacketStoreField(this, 0x44u, 4, 0u);
        RpcPacketStoreField(this, 0x48u, 4, 0u);
        RpcPacketStoreField(this, 0x4cu, 4, 0u);
        RpcPacketStoreField(this, 0x50u, 4, 0u);
        RpcPacketStoreField(this, 0x54u, 4, 0u);
        RpcPacketStoreField(this, 0x58u, 4, 0u);
        RpcPacketStoreField(this, 0x5cu, 4, 0u);
        RpcPacketStoreField(this, 0x60u, 4, 0u);
        RpcPacketStoreField(this, 0x64u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da9b50
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da9e40
    unsigned char Body[0x48];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CREATECOUNTER) == 0x68u, "FLPACKET_SERVER_CREATECOUNTER is operator new(0x68)");

// FLPACKET_SERVER_EXPLODEASTEROIDMINE -- id 0x20031, registered at 06d9c029.
// Factory FUN_06d9cf10: operator new(0x30) + ctor FUN_06d9cf50. Vtable .rdata:06db508c =
// { 06d9af50, 06db14f0, 06dad820, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_EXPLODEASTEROIDMINE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_EXPLODEASTEROIDMINE() : FPacketBase(0x20031u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db14f0
        return RpcEncodeWireDwordBlock12(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dad820
        return RpcDecodeWireDwordBlock12(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_EXPLODEASTEROIDMINE) == 0x30u, "FLPACKET_SERVER_EXPLODEASTEROIDMINE is operator new(0x30)");

// FLPACKET_SERVER_LOADHINT -- id 0x2004d, registered at 06d9c03f.
// Factory FUN_06d9cf80: operator new(0x2c) + ctor FUN_06d9cfc0. Vtable .rdata:06db3cd0 =
// { 06d912c0, 06d90a50, 06d90ba0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_LOADHINT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_LOADHINT() : FPacketBase(0x2004du) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
    }
    // FUN_06d909d0
    ~FPacket_FLPACKET_SERVER_LOADHINT() override {
        if (RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06d909d3..06d909e9
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
            RpcPacketField<void*>(this, 0x20u) = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d90a50
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d90ba0
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_LOADHINT) == 0x2cu, "FLPACKET_SERVER_LOADHINT is operator new(0x2c)");

// FLPACKET_SERVER_SETDIRECTIVE -- id 0x2004e, registered at 06d9c055.
// Factory FUN_06d9cff0: operator new(0xb4) + ctor FUN_06d9d030. Vtable .rdata:06db509c =
// { 06d9af50, 06d90d10, 06d90f70, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETDIRECTIVE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETDIRECTIVE() : FPacketBase(0x2004eu) {
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 1, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0u);
        RpcPacketStoreField(this, 0x38u, 4, 0xffffffffu);
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
        RpcPacketStoreField(this, 0x40u, 4, 0u);
        RpcPacketStoreField(this, 0x44u, 4, 0u);
        RpcPacketStoreField(this, 0x48u, 4, 0u);
        RpcPacketStoreField(this, 0x4cu, 4, 0u);
        RpcPacketStoreField(this, 0x50u, 4, 0u);
        RpcPacketStoreField(this, 0x54u, 4, 0u);
        RpcPacketStoreField(this, 0x58u, 4, 0u);
        RpcPacketStoreField(this, 0x5cu, 4, 0u);
        RpcPacketStoreField(this, 0x60u, 4, 0x00000003u);
        RpcPacketStoreField(this, 0x64u, 4, 0u);
        RpcPacketStoreField(this, 0x68u, 4, 0u);
        RpcPacketStoreField(this, 0x6cu, 4, 0u);
        RpcPacketStoreField(this, 0x70u, 4, 0u);
        RpcPacketStoreField(this, 0x74u, 4, 0u);
        RpcPacketStoreField(this, 0x78u, 4, 0u);
        RpcPacketStoreField(this, 0x7cu, 4, 0u);
        RpcPacketStoreField(this, 0x80u, 4, 0u);
        RpcPacketStoreField(this, 0x84u, 4, 0u);
        RpcPacketStoreField(this, 0x88u, 4, 0u);
        RpcPacketStoreField(this, 0x8cu, 4, 0u);
        RpcPacketStoreField(this, 0x90u, 4, 0u);
        RpcPacketStoreField(this, 0x94u, 4, 0u);
        RpcPacketStoreField(this, 0x98u, 4, 0u);
        RpcPacketStoreField(this, 0x9cu, 4, 0u);
        RpcPacketStoreField(this, 0xa0u, 4, 0u);
        RpcPacketStoreField(this, 0xa4u, 4, 0u);
        RpcPacketStoreField(this, 0xa8u, 4, 0u);
        RpcPacketStoreField(this, 0xacu, 1, 0u);
        RpcPacketStoreField(this, 0xadu, 1, 0u);
        RpcPacketStoreField(this, 0xaeu, 1, 0u);
        RpcPacketStoreField(this, 0xafu, 1, 0u);
        RpcPacketStoreField(this, 0xb0u, 1, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d90d10
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d90f70
    unsigned char Body[0x94];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETDIRECTIVE) == 0xb4u, "FLPACKET_SERVER_SETDIRECTIVE is operator new(0xb4)");

// FLPACKET_SERVER_CREATEGUIDED -- id 0x20037, registered at 06d9c06b.
// Factory FUN_06d9c960: operator new(0x58) + ctor FUN_06d9c9a0. Vtable .rdata:06db500c =
// { 06d9af50, 06da84e0, 06da8810, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_CREATEGUIDED : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_CREATEGUIDED() : FPacketBase(0x20037u) {
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
        RpcPacketStoreField(this, 0x40u, 4, 0u);
        RpcPacketStoreField(this, 0x44u, 4, 0u);
        RpcPacketStoreField(this, 0x48u, 4, 0x3f800000u);
        RpcPacketStoreField(this, 0x4cu, 4, 0u);
        RpcPacketStoreField(this, 0x50u, 4, 0u);
        RpcPacketStoreField(this, 0x54u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06da84e0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06da8810
    unsigned char Body[0x38];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CREATEGUIDED) == 0x58u, "FLPACKET_SERVER_CREATEGUIDED is operator new(0x58)");

// FLPACKET_SERVER_ITEMTRACTORED -- id 0x20038, registered at 06d9c081.
// Factory FUN_06d9c9e0: operator new(0x2c) + ctor FUN_06d9ca20. Vtable .rdata:06db501c =
// { 06d9af50, 06da8b50, 06da8cd0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_ITEMTRACTORED : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_ITEMTRACTORED() : FPacketBase(0x20038u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 2, 0u);
        RpcPacketStoreField(this, 0x2au, 2, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da8b50
        return RpcEncodeWireDwordDwordWordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da8cd0
        return RpcDecodeWireDwordDwordWordWord(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_ITEMTRACTORED) == 0x2cu, "FLPACKET_SERVER_ITEMTRACTORED is operator new(0x2c)");

// FLPACKET_SERVER_SETREMOVEITEM -- id 0x2002f, registered at 06d9c097.
// Factory FUN_06d9c470: operator new(0x2c) + ctor FUN_06d9c4b0. Vtable .rdata:06db4fac =
// { 06d9c4e0, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETREMOVEITEM : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETREMOVEITEM() : FPacketBase(0x2002fu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0xffffffffu);
        RpcPacketStoreField(this, 0x28u, 4, 0xffffffffu);
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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETREMOVEITEM) == 0x2cu, "FLPACKET_SERVER_SETREMOVEITEM is operator new(0x2c)");

// FLPACKET_SERVER_SETADDITEM -- id 0x2002e, registered at 06d9c0ad.
// Factory FUN_06d9c400: operator new(0x40) + ctor FUN_06d9c440. Vtable .rdata:06db3ee4 =
// { 06d96f70, 06d96f90, 06d97210, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETADDITEM : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETADDITEM() : FPacketBase(0x2002eu) {
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x39u, 1, 0u);
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
    }
    // FUN_06d96f40
    ~FPacket_FLPACKET_SERVER_SETADDITEM() override {
        if (RpcPacketField<void*>(this, 0x3cu) != nullptr) {   // 06d96f43..06d96f51
            ::operator delete(RpcPacketField<void*>(this, 0x3cu));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d96f90
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d97210
    unsigned char Body[0x20];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETADDITEM) == 0x40u, "FLPACKET_SERVER_SETADDITEM is operator new(0x40)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETCASH) == 0x24u, "FLPACKET_SERVER_SETCASH is operator new(0x24)");

// FLPACKET_SERVER_REQUESTSPACESCRIPT -- id 0x20032, registered at 06d9c0d9.
// Factory FUN_06d9dad0: operator new(0x3c) + ctor FUN_06d9db10. Vtable .rdata:06db3ff8 =
// { 06d986c0, 06d986e0, 06d98850, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT() : FPacketBase(0x20032u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 1, 0x00000001u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
    }
    // FUN_06d98690
    ~FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT() override {
        if (RpcPacketField<void*>(this, 0x38u) != nullptr) {   // 06d98693..06d986a1
            ::operator delete(RpcPacketField<void*>(this, 0x38u));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d986e0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d98850
    unsigned char Body[0x1c];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT) == 0x3cu, "FLPACKET_SERVER_REQUESTSPACESCRIPT is operator new(0x3c)");

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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE) == 0x2cu, "FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE is operator new(0x2c)");

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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_CFGINTERFACENOTIFICATION) == 0x2cu, "FLPACKET_SERVER_CFGINTERFACENOTIFICATION is operator new(0x2c)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_MARKOBJ) == 0x28u, "FLPACKET_SERVER_MARKOBJ is operator new(0x28)");

// FLPACKET_SERVER_SETMISSIONOBJECTIVES -- id 0x20035, registered at 06d9c131.
// Factory FUN_06d9da00: operator new(0x48) + ctor FUN_06d9da40. Vtable .rdata:06db3c90 =
// { 06d8fbf0, 06d8fc90, 06d8ffd0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES() : FPacketBase(0x20035u) {
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
        RpcPacketBuildListSentinel(this, 0x3cu, 0x0cu);   // operator new(0xc), Head
    }
    // FUN_06d8fb00
    ~FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES() override {
        // 06d8fb28..06d8fb65: every record, then its text block before the record itself.
        FRpcPacketListNode* Sentinel = RpcPacketField<FRpcPacketListNode*>(this, 0x40u);
        for (FRpcPacketListNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
            unsigned char* Record = static_cast<unsigned char*>(Node->Value);
            if (Record != nullptr) {                                   // 06d8fb43
                void* Text = *reinterpret_cast<void**>(Record + 0x08);
                if (Text != nullptr) {
                    ::operator delete(Text);
                }
                ::operator delete(Record);
            }
        }
        RpcPacketDestroyEmbeddedList(this, 0x3cu);   // FUN_06d901f0
        if (RpcPacketField<void*>(this, 0x34u) != nullptr) {   // 06d8fb9a..06d8fba6
            ::operator delete(RpcPacketField<void*>(this, 0x34u));
        }
        if (RpcPacketField<void*>(this, 0x2cu) != nullptr) {   // 06d8fbae..06d8fbb6
            ::operator delete(RpcPacketField<void*>(this, 0x2cu));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d8fc90
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d8ffd0
    unsigned char Body[0x28];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES) == 0x48u, "FLPACKET_SERVER_SETMISSIONOBJECTIVES is operator new(0x48)");

// FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE -- id 0x20034, registered at 06d9c147.
// Factory FUN_06d9d740: operator new(0x30) + ctor FUN_06d9d780. Vtable .rdata:06db512c =
// { 06d9d7b0, 06dab4b0, 06dab630, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE() : FPacketBase(0x20034u) {
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
    }
    // FUN_06d9d7d0
    ~FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE() override {
        if (RpcPacketField<void*>(this, 0x2cu) != nullptr) {   // 06d9d7d3..06d9d7db
            ::operator delete(RpcPacketField<void*>(this, 0x2cu));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dab4b0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dab630
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE) == 0x30u, "FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE is operator new(0x30)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SET_STORY_CUE) == 0x24u, "FLPACKET_SERVER_SET_STORY_CUE is operator new(0x24)");

// FLPACKET_SERVER_SET_CAMERA -- id 0x2004b, registered at 06d9c173.
// Factory FUN_06d9d860: operator new(0x44) + ctor FUN_06d9d8a0. Vtable .rdata:06db514c =
// { 06d9af50, 06dab780, 06dab960, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SET_CAMERA : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SET_CAMERA() : FPacketBase(0x2004bu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0x3f800000u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dab780
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dab960
    unsigned char Body[0x24];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SET_CAMERA) == 0x44u, "FLPACKET_SERVER_SET_CAMERA is operator new(0x44)");

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
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
    }
    // FUN_06d9d9e0
    ~FPacket_FLPACKET_SERVER_SET_MISSION_MESSAGE() override {
        if (RpcPacketField<void*>(this, 0x24u) != nullptr) {   // 06d9d9e3..06d9d9eb
            ::operator delete(RpcPacketField<void*>(this, 0x24u));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dabbb0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dabd00
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SET_MISSION_MESSAGE) == 0x2cu, "FLPACKET_SERVER_SET_MISSION_MESSAGE is operator new(0x2c)");

// FLPACKET_SERVER_REPAIROBJECT -- id 0x2003c, registered at 06d9c1b5.
// Factory FUN_06d9e3a0: operator new(0x2c) + ctor FUN_06d9e3e0. Vtable .rdata:06db524c =
// { 06d9af50, 06db0160, 06da6e40, 06dac2b0 }.
class FPacket_FLPACKET_SERVER_REPAIROBJECT : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_REPAIROBJECT() : FPacketBase(0x2003cu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REPAIROBJECT) == 0x2cu, "FLPACKET_SERVER_REPAIROBJECT is operator new(0x2c)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETNUMKILLS) == 0x24u, "FLPACKET_SERVER_SETNUMKILLS is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETMISSIONSUCCESSES) == 0x24u, "FLPACKET_SERVER_SETMISSIONSUCCESSES is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETMISSIONFAILURES) == 0x24u, "FLPACKET_SERVER_SETMISSIONFAILURES is operator new(0x24)");

// FLPACKET_SERVER_BURNFUSE -- id 0x20041, registered at 06d9c20d.
// Factory FUN_06d9e560: operator new(0x3c) + ctor FUN_06d9e5a0. Vtable .rdata:06db528c =
// { 06d9af50, 06dac3c0, 06dac5c0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_BURNFUSE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_BURNFUSE() : FPacketBase(0x20041u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 2, SUBOBJ_ID_NONE);   // COMMON.DLL SUBOBJ_ID_NONE
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0xbf800000u);
        RpcPacketStoreField(this, 0x38u, 1, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dac3c0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dac5c0
    unsigned char Body[0x1c];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_BURNFUSE) == 0x3cu, "FLPACKET_SERVER_BURNFUSE is operator new(0x3c)");

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
class FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS() : FPacketBase(0x20048u) {
        RpcPacketBuildListSentinel(this, 0x20u, 0x10u);   // operator new(0x10), Head
    }
    // FUN_06d9e930
    ~FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS() override {
        // 06d9e935..06d9e978 (one body, shared with the other collision-group class):
        // the list at +0x20 (Head +0x24, Size +0x28).
        RpcPacketDestroyEmbeddedList(this, 0x20u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dacca0
        return RpcEncodeWireCollisionGroups(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dacd70
        return RpcDecodeWireCollisionGroups(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS) == 0x2cu, "FLPACKET_SERVER_SETCOLLISIONGROUPS is operator new(0x2c)");

// FLPACKET_SERVER_SETHULLSTATUS -- id 0x20049, registered at 06d9c24f.
// Factory FUN_06d9e990: operator new(0x24) + ctor FUN_06d9e9d0. Vtable .rdata:06db52bc =
// { 06d9af50, 06db1cd0, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETHULLSTATUS : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETHULLSTATUS() : FPacketBase(0x20049u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db1cd0
        return RpcEncodeWireFloat(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETHULLSTATUS) == 0x24u, "FLPACKET_SERVER_SETHULLSTATUS is operator new(0x24)");

// FLPACKET_SERVER_SETGUIDEDTARGET -- id 0x2004a, registered at 06d9c265.
// Factory FUN_06d9ea00: operator new(0x2c) + ctor FUN_06d9ea40. Vtable .rdata:06db52cc =
// { 06d9af50, 06daaf90, 06dacea0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SETGUIDEDTARGET : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SETGUIDEDTARGET() : FPacketBase(0x2004au) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 2, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06daaf90
        return RpcEncodeWireDwordDwordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dacea0
        return RpcDecodeWireDwordDwordWord(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SETGUIDEDTARGET) == 0x2cu, "FLPACKET_SERVER_SETGUIDEDTARGET is operator new(0x2c)");

// FLPACKET_SERVER_SENDCOMM -- id 0x2004f, registered at 06d9c27b.
// Factory FUN_06d9ea70: operator new(0x78) + ctor FUN_06d9eab0. Vtable .rdata:06db52dc =
// { 06d9eaf0, 06dad000, 06dad320, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SENDCOMM : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SENDCOMM() : FPacketBase(0x2004fu) {
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
        RpcPacketStoreField(this, 0x5cu, 4, 0u);
        RpcPacketStoreField(this, 0x64u, 4, 0u);
        RpcPacketStoreField(this, 0x74u, 1, 0x00000001u);
    }
    // FUN_06d9eb10
    ~FPacket_FLPACKET_SERVER_SENDCOMM() override {
        if (RpcPacketField<void*>(this, 0x64u) != nullptr) {   // 06d9eb13..06d9eb21
            ::operator delete(RpcPacketField<void*>(this, 0x64u));
        }
        RpcPacketField<void*>(this, 0x64u) = nullptr;           // unconditional
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dad000
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dad320
    unsigned char Body[0x58];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SENDCOMM) == 0x78u, "FLPACKET_SERVER_SENDCOMM is operator new(0x78)");

// FLPACKET_SERVER_USE_ITEM -- id 0x20051, registered at 06d9c291.
// Factory FUN_06d9eb40: operator new(0x28) + ctor FUN_06d9eb80. Vtable .rdata:06db52ec =
// { 06d9af50, 06db19b0, 06dad690, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_USE_ITEM : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_USE_ITEM() : FPacketBase(0x20051u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 2, 0u);
        RpcPacketStoreField(this, 0x26u, 2, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db19b0
        return RpcEncodeWireDwordWordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dad690
        return RpcDecodeWireDwordWordWord(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_USE_ITEM) == 0x28u, "FLPACKET_SERVER_USE_ITEM is operator new(0x28)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SCANNOTIFY) == 0x28u, "FLPACKET_SERVER_SCANNOTIFY is operator new(0x28)");

// FLPACKET_SERVER_PLAYERLIST -- id 0x20052, registered at 06d9c2bd.
// Factory FUN_06d9ec20: operator new(0x30) + ctor FUN_06d9ec60. Vtable .rdata:06db4038 =
// { 06d99930, 06d99990, 06d99b60, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_PLAYERLIST : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_PLAYERLIST() : FPacketBase(0x20052u) {
        RpcPacketStoreField(this, 0x20u, 4, 0x00000003u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 1, 0u);
    }
    // FUN_06d99900
    ~FPacket_FLPACKET_SERVER_PLAYERLIST() override {
        RpcPacketField<unsigned int>(this, 0x28u) = 0;          // before the test
        if (RpcPacketField<void*>(this, 0x24u) != nullptr) {   // 06d99903..06d99915
            ::free(RpcPacketField<void*>(this, 0x24u));          // MSVCRT free, not delete
            RpcPacketField<void*>(this, 0x24u) = nullptr;
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d99990
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d99b60
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_PLAYERLIST) == 0x30u, "FLPACKET_SERVER_PLAYERLIST is operator new(0x30)");

// FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE -- id 0x2003d, registered at 06d9c2d3.
// Factory FUN_06d9eca0: operator new(0x2c) + ctor FUN_06d9ece0. Vtable .rdata:06db3c70 =
// { 06d8f820, 06d8f840, 06d8f970, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE : public FRpcEquipListPacket {
public:
    FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE() : FRpcEquipListPacket(0x2003du) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 1, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
    }
    // FUN_06d8f810
    ~FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE() override {
        // Own vptr store, then JMP FUN_06da21d0 -- ~FRpcEquipListPacket.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d8f840
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d8f970
    unsigned char Body[0x8];   // +0x24
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE) == 0x2cu, "FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE is operator new(0x2c)");

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
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db14f0
        return RpcEncodeWireDwordBlock12(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dad820
        return RpcDecodeWireDwordBlock12(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_FORMATION_UPDATE) == 0x30u, "FLPACKET_SERVER_FORMATION_UPDATE is operator new(0x30)");

// FLPACKET_SERVER_OBJECTCARGOUPDATE -- id 0x20055, registered at 06d9c315.
// Factory FUN_06d9ee30: operator new(0x34) + ctor FUN_06d9ee70. Vtable .rdata:06db531c =
// { 06d9eeb0, 06dad9b0, 06dadb80, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_OBJECTCARGOUPDATE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_OBJECTCARGOUPDATE() : FPacketBase(0x20055u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 2, 0u);
        RpcPacketStoreField(this, 0x2au, 2, 0x00000001u);
        RpcPacketStoreField(this, 0x2cu, 4, 0x3f800000u);
        RpcPacketStoreField(this, 0x30u, 1, 0u);
    }
    // FUN_06dad9a0
    ~FPacket_FLPACKET_SERVER_OBJECTCARGOUPDATE() override {
        // Own vptr store, then straight into CDAPacket's dtor: nothing to free.
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dad9b0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dadb80
    unsigned char Body[0x14];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_OBJECTCARGOUPDATE) == 0x34u, "FLPACKET_SERVER_OBJECTCARGOUPDATE is operator new(0x34)");

// FLPACKET_SERVER_SENDNNMESSAGE -- id 0x20056, registered at 06d9c32b.
// Factory FUN_06d9eed0: operator new(0x24) + ctor FUN_06d9ef10. Vtable .rdata:06db532c =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_SERVER_SENDNNMESSAGE : public FPacketBase {
public:
    FPacket_FLPACKET_SERVER_SENDNNMESSAGE() : FPacketBase(0x20056u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SENDNNMESSAGE) == 0x24u, "FLPACKET_SERVER_SENDNNMESSAGE is operator new(0x24)");

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
    unsigned char Body[0x18];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_SET_MUSIC) == 0x38u, "FLPACKET_SERVER_SET_MUSIC is operator new(0x38)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_SERVER_PLAY_SOUND_EFFECT) == 0x24u, "FLPACKET_SERVER_PLAY_SOUND_EFFECT is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQCHANGECASH) == 0x24u, "FLPACKET_CLIENT_REQCHANGECASH is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQSETCASH) == 0x24u, "FLPACKET_CLIENT_REQSETCASH is operator new(0x24)");

// FLPACKET_CLIENT_REQREMOVEITEM -- id 0x30019, registered at 06d9f139.
// Factory FUN_06d9f880: operator new(0x28) + ctor FUN_06da05d0. Vtable .rdata:06db5b14 =
// { 06da0600, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQREMOVEITEM : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQREMOVEITEM() : FPacketBase(0x30019u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0xffffffffu);
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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQREMOVEITEM) == 0x28u, "FLPACKET_CLIENT_REQREMOVEITEM is operator new(0x28)");

// FLPACKET_CLIENT_REQMODIFYITEM -- id 0x3001a, registered at 06d9f14f.
// Factory FUN_06d9f900: operator new(0x38) + ctor FUN_06da0660. Vtable .rdata:06db4070 =
// { 06d9a200, 06d9a220, 06d9a410, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQMODIFYITEM : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQMODIFYITEM() : FPacketBase(0x3001au) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 1, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0u);
    }
    // FUN_06d9a1d0
    ~FPacket_FLPACKET_CLIENT_REQMODIFYITEM() override {
        if (RpcPacketField<void*>(this, 0x34u) != nullptr) {   // 06d9a1d3..06d9a1e1
            ::operator delete(RpcPacketField<void*>(this, 0x34u));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d9a220
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d9a410
    unsigned char Body[0x18];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQMODIFYITEM) == 0x38u, "FLPACKET_CLIENT_REQMODIFYITEM is operator new(0x38)");

// FLPACKET_CLIENT_REQCARGO -- id 0x30017, registered at 06d9f165.
// Factory FUN_06d9f940: operator new(0x24) + ctor FUN_06da06a0. Vtable .rdata:06db3dfc =
// { 06d95d00, 06d916a0, 06d95bf0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQCARGO : public FRpcEquipListPacket {
public:
    FPacket_FLPACKET_CLIENT_REQCARGO() : FRpcEquipListPacket(0x30017u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
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
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
        RpcPacketStoreField(this, 0x30u, 1, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0u);
    }
    // FUN_06d99d40
    ~FPacket_FLPACKET_CLIENT_REQADDITEM() override {
        if (RpcPacketField<void*>(this, 0x34u) != nullptr) {   // 06d99d43..06d99d51
            ::operator delete(RpcPacketField<void*>(this, 0x34u));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d99d90
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d99f80
    unsigned char Body[0x18];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQADDITEM) == 0x38u, "FLPACKET_CLIENT_REQADDITEM is operator new(0x38)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQSHIPARCH) == 0x24u, "FLPACKET_CLIENT_REQSHIPARCH is operator new(0x24)");

// FLPACKET_CLIENT_REQEQUIPMENT -- id 0x30016, registered at 06d9f1a7.
// Factory FUN_06d9f980: operator new(0x24) + ctor FUN_06da0700. Vtable .rdata:06db3de0 =
// { 06d95bd0, 06d916a0, 06d95bf0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQEQUIPMENT : public FRpcEquipListPacket {
public:
    FPacket_FLPACKET_CLIENT_REQEQUIPMENT() : FRpcEquipListPacket(0x30016u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
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
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 2, SUBOBJ_ID_NONE);   // COMMON.DLL SUBOBJ_ID_NONE
        RpcPacketStoreField(this, 0x30u, 4, 0u);
        RpcPacketStoreField(this, 0x34u, 4, 0u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dae280
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dae430
    unsigned char Body[0x1c];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_MUNCOLLISION) == 0x3cu, "FLPACKET_CLIENT_MUNCOLLISION is operator new(0x3c)");

// FLPACKET_CLIENT_BADLANDSOBJCOLLISION -- id 0x3003d, registered at 06d9f1d3.
// Factory FUN_06d9fc00: operator new(0x2c) + ctor FUN_06da07e0. Vtable .rdata:06db5b64 =
// { 06d9af50, 06dae620, 06da78d0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_BADLANDSOBJCOLLISION : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_BADLANDSOBJCOLLISION() : FPacketBase(0x3003du) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 2, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dae620
        return RpcEncodeWireDwordWordDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da78d0
        return RpcDecodeWireDwordWordDword(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_BADLANDSOBJCOLLISION) == 0x2cu, "FLPACKET_CLIENT_BADLANDSOBJCOLLISION is operator new(0x2c)");

// FLPACKET_CLIENT_OBJCOLLISION -- id 0x3000c, registered at 06d9f1e9.
// Factory FUN_06d9fc40: operator new(0x34) + ctor FUN_06da0810. Vtable .rdata:06db5b74 =
// { 06d9af50, 06dae770, 06dae910, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_OBJCOLLISION : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_OBJCOLLISION() : FPacketBase(0x3000cu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 2, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 2, 0u);
        RpcPacketStoreField(this, 0x30u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06dae770
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06dae910
    unsigned char Body[0x14];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_OBJCOLLISION) == 0x34u, "FLPACKET_CLIENT_OBJCOLLISION is operator new(0x34)");

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
    unsigned char Body[0x14];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUEST_EVENT) == 0x34u, "FLPACKET_CLIENT_REQUEST_EVENT is operator new(0x34)");

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
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUEST_CANCEL) == 0x30u, "FLPACKET_CLIENT_REQUEST_CANCEL is operator new(0x30)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_CARGOSCAN) == 0x28u, "FLPACKET_CLIENT_CARGOSCAN is operator new(0x28)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUESTLAUNCH) == 0x24u, "FLPACKET_CLIENT_REQUESTLAUNCH is operator new(0x24)");

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
class FPacket_FLPACKET_CLIENT_SELECTCHARACTER : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_SELECTCHARACTER() : FPacketBase(0x30006u) {
        RpcPacketStoreField(this, 0x20u, 1, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06daf830
        return RpcEncodeWireCharacterId(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06daf970
        return RpcDecodeWireCharacterId(*this, Buffer, Size, OutLength);
    }
    void Dump(std::ostream& Out) const override {   // 06dafab0
        RpcDumpWireCharacterIdDump(*this, Out);
    }
    unsigned char Body[0x200];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_SELECTCHARACTER) == 0x220u, "FLPACKET_CLIENT_SELECTCHARACTER is operator new(0x220)");

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
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_COMMCOMPLETE) == 0x30u, "FLPACKET_CLIENT_COMMCOMPLETE is operator new(0x30)");

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
    unsigned char Body[0x90];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_CREATENEWCHAR) == 0xb0u, "FLPACKET_CLIENT_CREATENEWCHAR is operator new(0xb0)");

// FLPACKET_CLIENT_DESTROYCHAR -- id 0x3003a, registered at 06d9f2db.
// Factory FUN_06da01c0: operator new(0x220) + ctor FUN_06da0bb0. Vtable .rdata:06db5c84 =
// { 06d9af50, 06daf830, 06daf970, 06dafab0 }.
class FPacket_FLPACKET_CLIENT_DESTROYCHAR : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DESTROYCHAR() : FPacketBase(0x3003au) {
        RpcPacketStoreField(this, 0x20u, 1, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06daf830
        return RpcEncodeWireCharacterId(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06daf970
        return RpcDecodeWireCharacterId(*this, Buffer, Size, OutLength);
    }
    void Dump(std::ostream& Out) const override {   // 06dafab0
        RpcDumpWireCharacterIdDump(*this, Out);
    }
    unsigned char Body[0x200];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DESTROYCHAR) == 0x220u, "FLPACKET_CLIENT_DESTROYCHAR is operator new(0x220)");

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
    unsigned char Body[0x404];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_LOGIN) == 0x424u, "FLPACKET_CLIENT_LOGIN is operator new(0x424)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_ENTERBASE) == 0x24u, "FLPACKET_CLIENT_ENTERBASE is operator new(0x24)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUESTBASEINFO) == 0x28u, "FLPACKET_CLIENT_REQUESTBASEINFO is operator new(0x28)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUESTLOCATIONINFO) == 0x28u, "FLPACKET_CLIENT_REQUESTLOCATIONINFO is operator new(0x28)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE) == 0x24u, "FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_EXITBASE) == 0x24u, "FLPACKET_CLIENT_EXITBASE is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_ENTERLOCATION) == 0x24u, "FLPACKET_CLIENT_ENTERLOCATION is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_EXITLOCATION) == 0x24u, "FLPACKET_CLIENT_EXITLOCATION is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_INTERFACEITEMUSED) == 0x24u, "FLPACKET_CLIENT_INTERFACEITEMUSED is operator new(0x24)");

// FLPACKET_CLIENT_RTCDONE -- id 0x30047, registered at 06d9f3e3.
// Factory FUN_06d9fa80: operator new(0x24) + ctor FUN_06da0d00. Vtable .rdata:06db5cf4 =
// { 06d9af50, 06db0310, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_RTCDONE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_RTCDONE() : FPacketBase(0x30047u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0310
        return RpcEncodeWireDword(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_RTCDONE) == 0x24u, "FLPACKET_CLIENT_RTCDONE is operator new(0x24)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_ABORT_MISSION) == 0x24u, "FLPACKET_CLIENT_ABORT_MISSION is operator new(0x24)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_LAUNCHCOMPLETE) == 0x28u, "FLPACKET_CLIENT_LAUNCHCOMPLETE is operator new(0x28)");

// FLPACKET_CLIENT_JUMPINCOMPLETE -- id 0x30043, registered at 06d9f425.
// Factory FUN_06d9fb40: operator new(0x28) + ctor FUN_06da0d90. Vtable .rdata:06db5d24 =
// { 06d9af50, 06da9110, 06d95d40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_JUMPINCOMPLETE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_JUMPINCOMPLETE() : FPacketBase(0x30043u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06d95d40
        return RpcDecodeWireDwordPair(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_JUMPINCOMPLETE) == 0x28u, "FLPACKET_CLIENT_JUMPINCOMPLETE is operator new(0x28)");

// FLPACKET_CLIENT_HAIL -- id 0x3003f, registered at 06d9f43b.
// Factory FUN_06d9fb80: operator new(0x2c) + ctor FUN_06da1180. Vtable .rdata:06db5e14 =
// { 06d9af50, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_HAIL : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_HAIL() : FPacketBase(0x3003fu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_HAIL) == 0x2cu, "FLPACKET_CLIENT_HAIL is operator new(0x2c)");

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
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_GFGOODBUY) == 0x30u, "FLPACKET_CLIENT_GFGOODBUY is operator new(0x30)");

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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_GFGOODSELL) == 0x2cu, "FLPACKET_CLIENT_GFGOODSELL is operator new(0x2c)");

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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_GFGOODVAPORIZED) == 0x2cu, "FLPACKET_CLIENT_GFGOODVAPORIZED is operator new(0x2c)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_GFSELECTOBJECT) == 0x24u, "FLPACKET_CLIENT_GFSELECTOBJECT is operator new(0x24)");

// FLPACKET_CLIENT_SAVEGAME -- id 0x3001e, registered at 06d9f4a9.
// Factory FUN_06da0000: operator new(0x228) + ctor FUN_06da0a60. Vtable .rdata:06db40a8 =
// { 06d9ac90, 06d9a800, 06d9a950, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_SAVEGAME : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_SAVEGAME() : FPacketBase(0x3001eu) {
        RpcPacketStoreField(this, 0x20u, 1, 0u);
    }
    // FUN_06d9a7d0
    ~FPacket_FLPACKET_CLIENT_SAVEGAME() override {
        if (RpcPacketField<void*>(this, 0x220u) != nullptr) {   // 06d9a7d3..06d9a7e4
            ::operator delete(RpcPacketField<void*>(this, 0x220u));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06d9a800
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d9a950
    unsigned char Body[0x208];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_SAVEGAME) == 0x228u, "FLPACKET_CLIENT_SAVEGAME is operator new(0x228)");

// FLPACKET_CLIENT_MINEASTEROID -- id 0x30020, registered at 06d9f4bf.
// Factory FUN_06d9fd40: operator new(0x3c) + ctor FUN_06da0850. Vtable .rdata:06db5b84 =
// { 06d9af50, 06daeac0, 06daeca0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_MINEASTEROID : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_MINEASTEROID() : FPacketBase(0x30020u) {
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06daeac0
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06daeca0
    unsigned char Body[0x1c];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_MINEASTEROID) == 0x3cu, "FLPACKET_CLIENT_MINEASTEROID is operator new(0x3c)");

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
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_MISSIONRESPONSE) == 0x2cu, "FLPACKET_CLIENT_MISSIONRESPONSE is operator new(0x2c)");

// FLPACKET_CLIENT_TRADERESPONSE -- id 0x30027, registered at 06d9f4eb.
// Factory FUN_06d9fe00: operator new(0x28) + ctor FUN_06da08e0. Vtable .rdata:06db408c =
// { 06d9a7b0, 06da9110, 06d9a660, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_TRADERESPONSE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_TRADERESPONSE() : FPacketBase(0x30027u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
    }
    // FUN_06d9a630
    ~FPacket_FLPACKET_CLIENT_TRADERESPONSE() override {
        ::operator delete(RpcPacketField<void*>(this, 0x24u));   // no test
        RpcPacketField<void*>(this, 0x24u) = nullptr;
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da9110
        return RpcEncodeWireDwordPair(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06d9a660
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_TRADERESPONSE) == 0x28u, "FLPACKET_CLIENT_TRADERESPONSE is operator new(0x28)");

// FLPACKET_CLIENT_DBGCREATESHIP -- id 0x30022, registered at 06d9f501.
// Factory FUN_06d9f680: operator new(0x90) + ctor FUN_06da0e20. Vtable .rdata:06db5d54 =
// { 06da0ea0, 06db0560, 06db0b30, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DBGCREATESHIP : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DBGCREATESHIP() : FPacketBase(0x30022u) {
        RpcPacketStoreField(this, 0x34u, 4, 0u);
        RpcPacketStoreField(this, 0x38u, 4, 0u);
        RpcPacketStoreField(this, 0x3cu, 4, 0u);
        RpcPacketStoreField(this, 0x40u, 4, 0u);
        RpcPacketStoreField(this, 0x44u, 4, 0u);
        RpcPacketStoreField(this, 0x48u, 4, 0u);
        RpcPacketStoreField(this, 0x4cu, 4, 0x3f800000u);
        RpcPacketStoreField(this, 0x50u, 4, 0u);
        RpcPacketStoreField(this, 0x54u, 4, 0u);
        RpcPacketStoreField(this, 0x58u, 4, 0u);
        RpcPacketStoreField(this, 0x5cu, 4, 0u);
        RpcPacketStoreField(this, 0x60u, 4, 0u);
        RpcPacketStoreField(this, 0x64u, 4, 0u);
        RpcPacketStoreField(this, 0x68u, 4, 0u);
        RpcPacketStoreField(this, 0x6cu, 4, 0u);
        RpcPacketStoreField(this, 0x84u, 4, 0u);
        RpcPacketStoreField(this, 0x80u, 4, 0u);
        RpcPacketStoreField(this, 0x7cu, 4, 0u);
        RpcPacketStoreField(this, 0x78u, 4, 0u);
        RpcPacketStoreField(this, 0x74u, 4, 0u);
        RpcPacketStoreField(this, 0x70u, 4, 0u);
        RpcPacketStoreField(this, 0x88u, 4, 0u);
        RpcPacketStoreField(this, 0x8cu, 4, 0u);
    }
    // FUN_06db0500
    ~FPacket_FLPACKET_CLIENT_DBGCREATESHIP() override {
        if (RpcPacketField<void*>(this, 0x60u) != nullptr) {   // 06db0503..06db0511
            ::operator delete(RpcPacketField<void*>(this, 0x60u));
        }
        if (RpcPacketField<void*>(this, 0x88u) != nullptr) {   // 06db0519..06db0524
            ::operator delete(RpcPacketField<void*>(this, 0x88u));
        }
        if (RpcPacketField<void*>(this, 0x8cu) != nullptr) {   // 06db052c..06db0537
            ::operator delete(RpcPacketField<void*>(this, 0x8cu));
        }
        if (RpcPacketField<void*>(this, 0x84u) != nullptr) {   // 06db053f..06db054a
            ::operator delete(RpcPacketField<void*>(this, 0x84u));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06db0560
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06db0b30
    unsigned char Body[0x70];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGCREATESHIP) == 0x90u, "FLPACKET_CLIENT_DBGCREATESHIP is operator new(0x90)");

// FLPACKET_CLIENT_DBGLOADSYSTEM -- id 0x30023, registered at 06d9f517.
// Factory FUN_06d9f6c0: operator new(0x28) + ctor FUN_06da0ec0. Vtable .rdata:06db5d64 =
// { 06da0ef0, 06da6190, 06db10e0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM() : FPacketBase(0x30023u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
    }
    // FUN_06db10b0
    ~FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM() override {
        if (RpcPacketField<void*>(this, 0x20u) != nullptr) {   // 06db10b3..06db10c1
            ::operator delete(RpcPacketField<void*>(this, 0x20u));
        }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06da6190
        return RpcEncodeWireBlobBufferFirst(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06db10e0
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM) == 0x28u, "FLPACKET_CLIENT_DBGLOADSYSTEM is operator new(0x28)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DOCK) == 0x24u, "FLPACKET_CLIENT_DOCK is operator new(0x24)");

// FLPACKET_CLIENT_DBGCONSOLE -- id 0x3002d, registered at 06d9f559.
// Factory FUN_06d9f780: operator new(0xa4) + ctor FUN_06da0ff0. Vtable .rdata:06db5db4 =
// { 06d9af50, 06db1670, 06db17a0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DBGCONSOLE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DBGCONSOLE() : FPacketBase(0x3002du) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override;   // 06db1670
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override;   // 06db17a0
    unsigned char Body[0x84];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGCONSOLE) == 0xa4u, "FLPACKET_CLIENT_DBGCONSOLE is operator new(0xa4)");

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
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGDESTROYOBJECT) == 0x28u, "FLPACKET_CLIENT_DBGDESTROYOBJECT is operator new(0x28)");

// FLPACKET_CLIENT_DBGRELOCATE_SHIP -- id 0x30030, registered at 06d9f585.
// Factory FUN_06d9f800: operator new(0x30) + ctor FUN_06da0fc0. Vtable .rdata:06db5da4 =
// { 06d9af50, 06db14f0, 06dad820, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_DBGRELOCATE_SHIP : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_DBGRELOCATE_SHIP() : FPacketBase(0x30030u) {
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
        RpcPacketStoreField(this, 0x2cu, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db14f0
        return RpcEncodeWireDwordBlock12(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dad820
        return RpcDecodeWireDwordBlock12(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x10];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_DBGRELOCATE_SHIP) == 0x30u, "FLPACKET_CLIENT_DBGRELOCATE_SHIP is operator new(0x30)");

// FLPACKET_CLIENT_SETMANEUVER -- id 0x3002f, registered at 06d9f59b.
// Factory FUN_06da0380: operator new(0x2c) + ctor FUN_06da1020. Vtable .rdata:06db5dc4 =
// { 06d9af50, 06db0160, 06da6e40, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_SETMANEUVER : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_SETMANEUVER() : FPacketBase(0x3002fu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 4, 0u);
        RpcPacketStoreField(this, 0x28u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db0160
        return RpcEncodeWireThreeDwords(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6e40
        return RpcDecodeWireThreeDwords(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_SETMANEUVER) == 0x2cu, "FLPACKET_CLIENT_SETMANEUVER is operator new(0x2c)");

// FLPACKET_CLIENT_REQCOLLISIONGROUPS -- id 0x30036, registered at 06d9f5b1.
// Factory FUN_06da03c0: operator new(0x2c) + ctor FUN_06da10b0. Vtable .rdata:06db5df4 =
// { 06da1130, 06dacca0, 06dacd70, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS() : FPacketBase(0x30036u) {
        RpcPacketBuildListSentinel(this, 0x20u, 0x10u);   // operator new(0x10), Head
    }
    // FUN_06d9e930
    ~FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS() override {
        // 06d9e935..06d9e978 (one body, shared with the other collision-group class):
        // the list at +0x20 (Head +0x24, Size +0x28).
        RpcPacketDestroyEmbeddedList(this, 0x20u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06dacca0
        return RpcEncodeWireCollisionGroups(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dacd70
        return RpcDecodeWireCollisionGroups(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0xc];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS) == 0x2cu, "FLPACKET_CLIENT_REQCOLLISIONGROUPS is operator new(0x2c)");

// FLPACKET_CLIENT_REQHULLSTATUS -- id 0x3003b, registered at 06d9f5c7.
// Factory FUN_06da0400: operator new(0x24) + ctor FUN_06da1150. Vtable .rdata:06db5e04 =
// { 06d9af50, 06db1cd0, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQHULLSTATUS : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQHULLSTATUS() : FPacketBase(0x3003bu) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db1cd0
        return RpcEncodeWireFloat(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQHULLSTATUS) == 0x24u, "FLPACKET_CLIENT_REQHULLSTATUS is operator new(0x24)");

// FLPACKET_CLIENT_REQUEST_USE_ITEM -- id 0x30040, registered at 06d9f5dd.
// Factory FUN_06da0440: operator new(0x28) + ctor FUN_06da11b0. Vtable .rdata:06db5e24 =
// { 06d9af50, 06db19b0, 06dad690, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQUEST_USE_ITEM : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQUEST_USE_ITEM() : FPacketBase(0x30040u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 2, 0u);
        RpcPacketStoreField(this, 0x26u, 2, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db19b0
        return RpcEncodeWireDwordWordWord(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06dad690
        return RpcDecodeWireDwordWordWord(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQUEST_USE_ITEM) == 0x28u, "FLPACKET_CLIENT_REQUEST_USE_ITEM is operator new(0x28)");

// FLPACKET_CLIENT_REQINVINCIBILITY -- id 0x30044, registered at 06d9f5f3.
// Factory FUN_06da0480: operator new(0x28) + ctor FUN_06da11f0. Vtable .rdata:06db5e34 =
// { 06d9af50, 06db1b60, 06da6a50, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQINVINCIBILITY : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQINVINCIBILITY() : FPacketBase(0x30044u) {
        RpcPacketStoreField(this, 0x20u, 4, 0u);
        RpcPacketStoreField(this, 0x24u, 1, 0u);
        RpcPacketStoreField(this, 0x25u, 1, 0u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db1b60
        return RpcEncodeWireDwordTwoBytes(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06da6a50
        return RpcDecodeWireDwordTwoBytes(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x8];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQINVINCIBILITY) == 0x28u, "FLPACKET_CLIENT_REQINVINCIBILITY is operator new(0x28)");

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
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_MISSIONSAVEB) == 0x24u, "FLPACKET_CLIENT_MISSIONSAVEB is operator new(0x24)");

// FLPACKET_CLIENT_REQDIFFICULTYSCALE -- id 0x30046, registered at 06d9f61f.
// Factory FUN_06da0500: operator new(0x24) + ctor FUN_06da1220. Vtable .rdata:06db5e44 =
// { 06d9af50, 06db1cd0, 06db03f0, no-op dump (ICF'd onto IServerImpl::GetServerStats) }.
class FPacket_FLPACKET_CLIENT_REQDIFFICULTYSCALE : public FPacketBase {
public:
    FPacket_FLPACKET_CLIENT_REQDIFFICULTYSCALE() : FPacketBase(0x30046u) {
        RpcPacketStoreField(this, 0x20u, 4, 0x3f800000u);
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                        unsigned int* OutLength) override {   // 06db1cd0
        return RpcEncodeWireFloat(*this, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                        unsigned int* OutLength) override {   // 06db03f0
        return RpcDecodeWireDword(*this, Buffer, Size, OutLength);
    }
    unsigned char Body[0x4];   // +0x20
};
static_assert(sizeof(FPacket_FLPACKET_CLIENT_REQDIFFICULTYSCALE) == 0x24u, "FLPACKET_CLIENT_REQDIFFICULTYSCALE is operator new(0x24)");
