// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The 162 retail packet classes and the three registrators.
//
// Transcribed from the rpclocal.dll disassembly and maintained as source; every number
// below is an instruction operand, quoted with its address.
//
// THE HIERARCHY (from the .rdata vtables):
//   * the refcounted root, vtable 06db3c54 = { LAB_06d8f4d0 }       -> FRpcRefObject
//   * CDAPacket, vtable 06db4fbc = { 06d9c5d0, 06da13d0, 06da1470,
//     no-op dump }                                                  -> FPacketBase
//   * an abstract create-object class, vtable 06db3fb4, slot 0 the purecall stub
//     LAB_06db1dd0; its dtor part is inlined into CREATESHIP's and CREATESOLAR's
//                                                                   -> FRpcCreateObjectPacket
//   * an equipment-list class, vtable 06db5e54, dtor FUN_06da21d0, under the five
//     classes whose own dtor is `vptr store ; JMP FUN_06da21d0`     -> FRpcEquipListPacket
//   * the 162 concrete classes, one vtable each                     -> FPacket_<name>
//
// FACTORIES. All 162 have the same body (e.g. FUN_06d9af70): clear *Out, operator new,
// return 5 on failure, construct, store, return 0 -- __cdecl, one argument, no null test
// on it. RpcPacketFactory<T> below is that body.
//
// CONSTRUCTORS store exactly what the retail ctor stores and nothing else: retail's
// operator new does not zero, so neither does the port. The CDAPacket stores (+0x04 = 1,
// +0x08, +0x10, +0x1c = 0, +0x0c = id) are FRpcRefObject's and FPacketBase's; the rest
// is listed per class in the order the listing makes them. A byte the listing copies out
// of an uninitialised stack slot -- the empty allocator object of an MSVCP60 container,
// or the top byte of a pushed register -- is stored as 0; nothing ever reads it.
//
// DESTRUCTORS are the retail bodies, offset for offset; the implicit call to
// ~FPacketBase at the end is retail's closing jump to FUN_06da1370. A class without one
// shares retail's scalar dtor 06d9af50, whose dtor is the thunk 06da11e0 -> 06da1370.
//
// ENCODE / DECODE / DUMP are the class's own vtable slots, forwarding to the codec bodies
// in rpc_wire_*.cpp / rpc_containers.cpp / rpc_packet_dump.cpp. Five classes keep
// CDAPacket's own 06da13d0 / 06da1470, and 150 keep the no-op dump.

#include "rpc_transport.h"
#include "rpc_containers.h"
#include "rpc_payloads.h"
#include "common_misc.h"        // SUBOBJ_ID_NONE, the default of three word fields

#include <cstdlib>
#include <new>
#include <ostream>

#include "rpc_packet_classes.h"

// Retail FUN_06d9ace0, __cdecl, one argument. A null serializer returns 4; otherwise
// every RegisterType result is ignored and the function returns 0.
int RpcRegisterChannel1Types(FPacketSerializer* Serializer) {
    if (Serializer == nullptr) {
        return 4;
    }
    Serializer->RegisterType(0x10002u, "FLPACKET_COMMON_FIREWEAPON",   // 06d9aced
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_FIREWEAPON>);
    Serializer->RegisterType(0x10001u, "FLPACKET_COMMON_UPDATEOBJECT",   // 06d9ad03
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_UPDATEOBJECT>);
    Serializer->RegisterType(0x10005u, "FLPACKET_COMMON_CHATMSG",   // 06d9ad19
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_CHATMSG>);
    Serializer->RegisterType(0x10008u, "FLPACKET_COMMON_ACTIVATEEQUIP",   // 06d9ad2f
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_ACTIVATEEQUIP>);
    Serializer->RegisterType(0x10004u, "FLPACKET_COMMON_SETTARGET",   // 06d9ad45
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_SETTARGET>);
    Serializer->RegisterType(0x1000eu, "FLPACKET_COMMON_ACTIVATECRUISE",   // 06d9ad5b
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_ACTIVATECRUISE>);
    Serializer->RegisterType(0x10015u, "FLPACKET_COMMON_ACTIVATETHRUSTERS",   // 06d9ad71
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_ACTIVATETHRUSTERS>);
    Serializer->RegisterType(0x1000fu, "FLPACKET_COMMON_GOTRADELANE",   // 06d9ad87
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_GOTRADELANE>);
    Serializer->RegisterType(0x10010u, "FLPACKET_COMMON_STOPTRADELANE",   // 06d9ad9d
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_STOPTRADELANE>);
    Serializer->RegisterType(0x10011u, "FLPACKET_COMMON_SET_WEAPON_GROUP",   // 06d9adb3
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP>);
    Serializer->RegisterType(0x10012u, "FLPACKET_COMMON_PLAYER_TRADE",   // 06d9adc9
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_PLAYER_TRADE>);
    Serializer->RegisterType(0x10013u, "FLPACKET_COMMON_SET_VISITED_STATE",   // 06d9addf
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_SET_VISITED_STATE>);
    Serializer->RegisterType(0x10014u, "FLPACKET_COMMON_JETTISONCARGO",   // 06d9adf5
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_JETTISONCARGO>);
    Serializer->RegisterType(0x10016u, "FLPACKET_COMMON_REQUEST_BEST_PATH",   // 06d9ae0b
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH>);
    Serializer->RegisterType(0x10018u, "FLPACKET_COMMON_REQUEST_PLAYER_STATS",   // 06d9ae21
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS>);
    Serializer->RegisterType(0x1001bu, "FLPACKET_COMMON_POP_UP_DIALOG",   // 06d9ae37
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_POP_UP_DIALOG>);
    Serializer->RegisterType(0x10017u, "FLPACKET_COMMON_REQUEST_GROUP_POSITIONS",   // 06d9ae4d
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS>);
    Serializer->RegisterType(0x10019u, "FLPACKET_COMMON_SET_MISSION_LOG",   // 06d9ae63
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_SET_MISSION_LOG>);
    Serializer->RegisterType(0x1001au, "FLPACKET_COMMON_REQUEST_RANK_LEVEL",   // 06d9ae79
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL>);
    Serializer->RegisterType(0x1001cu, "FLPACKET_COMMON_SET_INTERFACE_STATE",   // 06d9ae8f
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE>);
    Serializer->RegisterType(0x1001du, "FLPACKET_COMMON_TRACTOROBJECTS",   // 06d9aea5
                             &RpcPacketFactory<FPacket_FLPACKET_COMMON_TRACTOROBJECTS>);
    return 0;
}

// Retail FUN_06d9bc80, __cdecl, one argument. A null serializer returns 4; otherwise
// every RegisterType result is ignored and the function returns 0.
int RpcRegisterChannel2Types(FPacketSerializer* Serializer) {
    if (Serializer == nullptr) {
        return 4;
    }
    Serializer->RegisterType(0x20001u, "FLPACKET_SERVER_CONNECTRESPONSE",   // 06d9bc8d
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CONNECTRESPONSE>);
    Serializer->RegisterType(0x20002u, "FLPACKET_SERVER_LOGINRESPONSE",   // 06d9bca3
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_LOGINRESPONSE>);
    Serializer->RegisterType(0x20005u, "FLPACKET_SERVER_DAMAGEOBJECT",   // 06d9bcb9
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_DAMAGEOBJECT>);
    Serializer->RegisterType(0x20006u, "FLPACKET_SERVER_DESTROYOBJECT",   // 06d9bccf
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_DESTROYOBJECT>);
    Serializer->RegisterType(0x20004u, "FLPACKET_SERVER_CREATESHIP",   // 06d9bce5
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CREATESHIP>);
    Serializer->RegisterType(0x20044u, "FLPACKET_SERVER_REQUEST_RETURNED",   // 06d9bcfb
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_REQUEST_RETURNED>);
    Serializer->RegisterType(0x20003u, "FLPACKET_SERVER_CHARACTERINFO",   // 06d9bd11
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CHARACTERINFO>);
    Serializer->RegisterType(0x20007u, "FLPACKET_SERVER_LAUNCH",   // 06d9bd27
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_LAUNCH>);
    Serializer->RegisterType(0x20008u, "FLPACKET_SERVER_CHARSELECTVERIFIED",   // 06d9bd3d
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CHARSELECTVERIFIED>);
    Serializer->RegisterType(0x2000au, "FLPACKET_SERVER_ACTIVATEOBJECT",   // 06d9bd53
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_ACTIVATEOBJECT>);
    Serializer->RegisterType(0x2000bu, "FLPACKET_SERVER_LAND",   // 06d9bd69
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_LAND>);
    Serializer->RegisterType(0x2000eu, "FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST",   // 06d9bd7f
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST>);
    Serializer->RegisterType(0x20010u, "FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST",   // 06d9bd95
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST>);
    Serializer->RegisterType(0x2000fu, "FLPACKET_SERVER_GFCOMPLETECHARLIST",   // 06d9bdab
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFCOMPLETECHARLIST>);
    Serializer->RegisterType(0x20011u, "FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST",   // 06d9bdc1
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST>);
    Serializer->RegisterType(0x20013u, "FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST",   // 06d9bdd7
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST>);
    Serializer->RegisterType(0x20021u, "FLPACKET_SERVER_SYSTEM_SWITCH_OUT",   // 06d9bded
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_OUT>);
    Serializer->RegisterType(0x20022u, "FLPACKET_SERVER_SYSTEM_SWITCH_IN",   // 06d9be03
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SYSTEM_SWITCH_IN>);
    Serializer->RegisterType(0x20023u, "FLPACKET_SERVER_SETSHIPARCH",   // 06d9be19
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETSHIPARCH>);
    Serializer->RegisterType(0x20024u, "FLPACKET_SERVER_SETEQUIPMENT",   // 06d9be2f
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETEQUIPMENT>);
    Serializer->RegisterType(0x20025u, "FLPACKET_SERVER_SETCARGO",   // 06d9be45
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETCARGO>);
    Serializer->RegisterType(0x20027u, "FLPACKET_SERVER_REQUESTCREATESHIPRESP",   // 06d9be5b
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_REQUESTCREATESHIPRESP>);
    Serializer->RegisterType(0x2000du, "FLPACKET_SERVER_SETSTARTROOM",   // 06d9be71
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETSTARTROOM>);
    Serializer->RegisterType(0x20015u, "FLPACKET_SERVER_GFDESTROYCHARACTER",   // 06d9be87
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFDESTROYCHARACTER>);
    Serializer->RegisterType(0x20014u, "FLPACKET_SERVER_GFDESTROYNEWSBROADCAST",   // 06d9be9d
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFDESTROYNEWSBROADCAST>);
    Serializer->RegisterType(0x20016u, "FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER",   // 06d9beb3
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER>);
    Serializer->RegisterType(0x20017u, "FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR",   // 06d9bec9
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR>);
    Serializer->RegisterType(0x20019u, "FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT",   // 06d9bedf
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT>);
    Serializer->RegisterType(0x2001au, "FLPACKET_SERVER_GFSCRIPTBEHAVIOR",   // 06d9bef5
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFSCRIPTBEHAVIOR>);
    Serializer->RegisterType(0x2001fu, "FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT",   // 06d9bf0b
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT>);
    Serializer->RegisterType(0x2005bu, "FLPACKET_SERVER_MISSIONSAVEA",   // 06d9bf21
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_MISSIONSAVEA>);
    Serializer->RegisterType(0x2001du, "FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER",   // 06d9bf37
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER>);
    Serializer->RegisterType(0x2001eu, "FLPACKET_SERVER_GFUPDATENEWSBROADCAST",   // 06d9bf4d
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFUPDATENEWSBROADCAST>);
    Serializer->RegisterType(0x20020u, "FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE",   // 06d9bf63
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE>);
    Serializer->RegisterType(0x2005au, "FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY",   // 06d9bf79
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY>);
    Serializer->RegisterType(0x20026u, "FLPACKET_SERVER_GFUPDATECHAR",   // 06d9bf8f
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_GFUPDATECHAR>);
    Serializer->RegisterType(0x20028u, "FLPACKET_SERVER_CREATELOOT",   // 06d9bfa5
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CREATELOOT>);
    Serializer->RegisterType(0x20029u, "FLPACKET_SERVER_SETREPUTATION",   // 06d9bfbb
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETREPUTATION>);
    Serializer->RegisterType(0x2002au, "FLPACKET_SERVER_ADJUSTATTITUDE",   // 06d9bfd1
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_ADJUSTATTITUDE>);
    Serializer->RegisterType(0x2002bu, "FLPACKET_SERVER_SETGROUPFEELINGS",   // 06d9bfe7
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETGROUPFEELINGS>);
    Serializer->RegisterType(0x2002cu, "FLPACKET_SERVER_CREATEMINE",   // 06d9bffd
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CREATEMINE>);
    Serializer->RegisterType(0x2002du, "FLPACKET_SERVER_CREATECOUNTER",   // 06d9c013
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CREATECOUNTER>);
    Serializer->RegisterType(0x20031u, "FLPACKET_SERVER_EXPLODEASTEROIDMINE",   // 06d9c029
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_EXPLODEASTEROIDMINE>);
    Serializer->RegisterType(0x2004du, "FLPACKET_SERVER_LOADHINT",   // 06d9c03f
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_LOADHINT>);
    Serializer->RegisterType(0x2004eu, "FLPACKET_SERVER_SETDIRECTIVE",   // 06d9c055
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETDIRECTIVE>);
    Serializer->RegisterType(0x20037u, "FLPACKET_SERVER_CREATEGUIDED",   // 06d9c06b
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CREATEGUIDED>);
    Serializer->RegisterType(0x20038u, "FLPACKET_SERVER_ITEMTRACTORED",   // 06d9c081
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_ITEMTRACTORED>);
    Serializer->RegisterType(0x2002fu, "FLPACKET_SERVER_SETREMOVEITEM",   // 06d9c097
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETREMOVEITEM>);
    Serializer->RegisterType(0x2002eu, "FLPACKET_SERVER_SETADDITEM",   // 06d9c0ad
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETADDITEM>);
    Serializer->RegisterType(0x20030u, "FLPACKET_SERVER_SETCASH",   // 06d9c0c3
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETCASH>);
    Serializer->RegisterType(0x20032u, "FLPACKET_SERVER_REQUESTSPACESCRIPT",   // 06d9c0d9
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_REQUESTSPACESCRIPT>);
    Serializer->RegisterType(0x20033u, "FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE",   // 06d9c0ef
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE>);
    Serializer->RegisterType(0x20047u, "FLPACKET_SERVER_CFGINTERFACENOTIFICATION",   // 06d9c105
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CFGINTERFACENOTIFICATION>);
    Serializer->RegisterType(0x20046u, "FLPACKET_SERVER_MARKOBJ",   // 06d9c11b
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_MARKOBJ>);
    Serializer->RegisterType(0x20035u, "FLPACKET_SERVER_SETMISSIONOBJECTIVES",   // 06d9c131
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES>);
    Serializer->RegisterType(0x20034u, "FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE",   // 06d9c147
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE>);
    Serializer->RegisterType(0x20043u, "FLPACKET_SERVER_SET_STORY_CUE",   // 06d9c15d
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SET_STORY_CUE>);
    Serializer->RegisterType(0x2004bu, "FLPACKET_SERVER_SET_CAMERA",   // 06d9c173
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SET_CAMERA>);
    Serializer->RegisterType(0x2004cu, "FLPACKET_SERVER_REVERT_CAMERA",   // 06d9c189
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_REVERT_CAMERA>);
    Serializer->RegisterType(0x20045u, "FLPACKET_SERVER_SET_MISSION_MESSAGE",   // 06d9c19f
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SET_MISSION_MESSAGE>);
    Serializer->RegisterType(0x2003cu, "FLPACKET_SERVER_REPAIROBJECT",   // 06d9c1b5
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_REPAIROBJECT>);
    Serializer->RegisterType(0x2003eu, "FLPACKET_SERVER_SETNUMKILLS",   // 06d9c1cb
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETNUMKILLS>);
    Serializer->RegisterType(0x2003fu, "FLPACKET_SERVER_SETMISSIONSUCCESSES",   // 06d9c1e1
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETMISSIONSUCCESSES>);
    Serializer->RegisterType(0x20040u, "FLPACKET_SERVER_SETMISSIONFAILURES",   // 06d9c1f7
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETMISSIONFAILURES>);
    Serializer->RegisterType(0x20041u, "FLPACKET_SERVER_BURNFUSE",   // 06d9c20d
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_BURNFUSE>);
    Serializer->RegisterType(0x20042u, "FLPACKET_SERVER_CREATESOLAR",   // 06d9c223
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CREATESOLAR>);
    Serializer->RegisterType(0x20048u, "FLPACKET_SERVER_SETCOLLISIONGROUPS",   // 06d9c239
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS>);
    Serializer->RegisterType(0x20049u, "FLPACKET_SERVER_SETHULLSTATUS",   // 06d9c24f
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETHULLSTATUS>);
    Serializer->RegisterType(0x2004au, "FLPACKET_SERVER_SETGUIDEDTARGET",   // 06d9c265
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SETGUIDEDTARGET>);
    Serializer->RegisterType(0x2004fu, "FLPACKET_SERVER_SENDCOMM",   // 06d9c27b
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SENDCOMM>);
    Serializer->RegisterType(0x20051u, "FLPACKET_SERVER_USE_ITEM",   // 06d9c291
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_USE_ITEM>);
    Serializer->RegisterType(0x20039u, "FLPACKET_SERVER_SCANNOTIFY",   // 06d9c2a7
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SCANNOTIFY>);
    Serializer->RegisterType(0x20052u, "FLPACKET_SERVER_PLAYERLIST",   // 06d9c2bd
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_PLAYERLIST>);
    Serializer->RegisterType(0x2003du, "FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE",   // 06d9c2d3
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE>);
    Serializer->RegisterType(0x20054u, "FLPACKET_SERVER_MISCOBJUPDATE",   // 06d9c2e9
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_MISCOBJUPDATE>);
    Serializer->RegisterType(0x20053u, "FLPACKET_SERVER_FORMATION_UPDATE",   // 06d9c2ff
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_FORMATION_UPDATE>);
    Serializer->RegisterType(0x20055u, "FLPACKET_SERVER_OBJECTCARGOUPDATE",   // 06d9c315
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_OBJECTCARGOUPDATE>);
    Serializer->RegisterType(0x20056u, "FLPACKET_SERVER_SENDNNMESSAGE",   // 06d9c32b
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SENDNNMESSAGE>);
    Serializer->RegisterType(0x20057u, "FLPACKET_SERVER_SET_MUSIC",   // 06d9c341
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_SET_MUSIC>);
    Serializer->RegisterType(0x20058u, "FLPACKET_SERVER_CANCEL_MUSIC",   // 06d9c357
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_CANCEL_MUSIC>);
    Serializer->RegisterType(0x20059u, "FLPACKET_SERVER_PLAY_SOUND_EFFECT",   // 06d9c36d
                             &RpcPacketFactory<FPacket_FLPACKET_SERVER_PLAY_SOUND_EFFECT>);
    return 0;
}

// Retail FUN_06d9f100, __cdecl, one argument. A null serializer returns 4; otherwise
// every RegisterType result is ignored and the function returns 0.
int RpcRegisterChannel3Types(FPacketSerializer* Serializer) {
    if (Serializer == nullptr) {
        return 4;
    }
    Serializer->RegisterType(0x3001cu, "FLPACKET_CLIENT_REQCHANGECASH",   // 06d9f10d
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQCHANGECASH>);
    Serializer->RegisterType(0x3001bu, "FLPACKET_CLIENT_REQSETCASH",   // 06d9f123
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQSETCASH>);
    Serializer->RegisterType(0x30019u, "FLPACKET_CLIENT_REQREMOVEITEM",   // 06d9f139
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQREMOVEITEM>);
    Serializer->RegisterType(0x3001au, "FLPACKET_CLIENT_REQMODIFYITEM",   // 06d9f14f
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQMODIFYITEM>);
    Serializer->RegisterType(0x30017u, "FLPACKET_CLIENT_REQCARGO",   // 06d9f165
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQCARGO>);
    Serializer->RegisterType(0x30018u, "FLPACKET_CLIENT_REQADDITEM",   // 06d9f17b
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQADDITEM>);
    Serializer->RegisterType(0x30015u, "FLPACKET_CLIENT_REQSHIPARCH",   // 06d9f191
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQSHIPARCH>);
    Serializer->RegisterType(0x30016u, "FLPACKET_CLIENT_REQEQUIPMENT",   // 06d9f1a7
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQEQUIPMENT>);
    Serializer->RegisterType(0x30003u, "FLPACKET_CLIENT_MUNCOLLISION",   // 06d9f1bd
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_MUNCOLLISION>);
    Serializer->RegisterType(0x3003du, "FLPACKET_CLIENT_BADLANDSOBJCOLLISION",   // 06d9f1d3
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_BADLANDSOBJCOLLISION>);
    Serializer->RegisterType(0x3000cu, "FLPACKET_CLIENT_OBJCOLLISION",   // 06d9f1e9
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_OBJCOLLISION>);
    Serializer->RegisterType(0x30031u, "FLPACKET_CLIENT_REQUEST_EVENT",   // 06d9f1ff
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQUEST_EVENT>);
    Serializer->RegisterType(0x30032u, "FLPACKET_CLIENT_REQUEST_CANCEL",   // 06d9f215
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQUEST_CANCEL>);
    Serializer->RegisterType(0x3002bu, "FLPACKET_CLIENT_CARGOSCAN",   // 06d9f22b
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_CARGOSCAN>);
    Serializer->RegisterType(0x30004u, "FLPACKET_CLIENT_REQUESTLAUNCH",   // 06d9f241
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQUESTLAUNCH>);
    Serializer->RegisterType(0x30005u, "FLPACKET_CLIENT_REQUESTCHARINFO",   // 06d9f257
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQUESTCHARINFO>);
    Serializer->RegisterType(0x30042u, "FLPACKET_CLIENT_SKIP_AUTOSAVE",   // 06d9f26d
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_SKIP_AUTOSAVE>);
    Serializer->RegisterType(0x30006u, "FLPACKET_CLIENT_SELECTCHARACTER",   // 06d9f283
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_SELECTCHARACTER>);
    Serializer->RegisterType(0x30037u, "FLPACKET_CLIENT_COMMCOMPLETE",   // 06d9f299
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_COMMCOMPLETE>);
    Serializer->RegisterType(0x30038u, "FLPACKET_CLIENT_REQUESTNEWCHARINFO",   // 06d9f2af
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQUESTNEWCHARINFO>);
    Serializer->RegisterType(0x30039u, "FLPACKET_CLIENT_CREATENEWCHAR",   // 06d9f2c5
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_CREATENEWCHAR>);
    Serializer->RegisterType(0x3003au, "FLPACKET_CLIENT_DESTROYCHAR",   // 06d9f2db
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_DESTROYCHAR>);
    Serializer->RegisterType(0x30001u, "FLPACKET_CLIENT_LOGIN",   // 06d9f2f1
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_LOGIN>);
    Serializer->RegisterType(0x30007u, "FLPACKET_CLIENT_ENTERBASE",   // 06d9f307
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_ENTERBASE>);
    Serializer->RegisterType(0x30008u, "FLPACKET_CLIENT_REQUESTBASEINFO",   // 06d9f31d
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQUESTBASEINFO>);
    Serializer->RegisterType(0x30009u, "FLPACKET_CLIENT_REQUESTLOCATIONINFO",   // 06d9f333
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQUESTLOCATIONINFO>);
    Serializer->RegisterType(0x3000au, "FLPACKET_CLIENT_GFREQUESTSHIPINFO",   // 06d9f349
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_GFREQUESTSHIPINFO>);
    Serializer->RegisterType(0x3000bu, "FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE",   // 06d9f35f
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE>);
    Serializer->RegisterType(0x3000du, "FLPACKET_CLIENT_EXITBASE",   // 06d9f375
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_EXITBASE>);
    Serializer->RegisterType(0x3000eu, "FLPACKET_CLIENT_ENTERLOCATION",   // 06d9f38b
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_ENTERLOCATION>);
    Serializer->RegisterType(0x3000fu, "FLPACKET_CLIENT_EXITLOCATION",   // 06d9f3a1
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_EXITLOCATION>);
    Serializer->RegisterType(0x30010u, "FLPACKET_CLIENT_REQUESTCREATESHIP",   // 06d9f3b7
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQUESTCREATESHIP>);
    Serializer->RegisterType(0x30035u, "FLPACKET_CLIENT_INTERFACEITEMUSED",   // 06d9f3cd
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_INTERFACEITEMUSED>);
    Serializer->RegisterType(0x30047u, "FLPACKET_CLIENT_RTCDONE",   // 06d9f3e3
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_RTCDONE>);
    Serializer->RegisterType(0x30041u, "FLPACKET_CLIENT_ABORT_MISSION",   // 06d9f3f9
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_ABORT_MISSION>);
    Serializer->RegisterType(0x3003eu, "FLPACKET_CLIENT_LAUNCHCOMPLETE",   // 06d9f40f
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_LAUNCHCOMPLETE>);
    Serializer->RegisterType(0x30043u, "FLPACKET_CLIENT_JUMPINCOMPLETE",   // 06d9f425
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_JUMPINCOMPLETE>);
    Serializer->RegisterType(0x3003fu, "FLPACKET_CLIENT_HAIL",   // 06d9f43b
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_HAIL>);
    Serializer->RegisterType(0x30012u, "FLPACKET_CLIENT_GFGOODBUY",   // 06d9f451
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_GFGOODBUY>);
    Serializer->RegisterType(0x30011u, "FLPACKET_CLIENT_GFGOODSELL",   // 06d9f467
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_GFGOODSELL>);
    Serializer->RegisterType(0x3003cu, "FLPACKET_CLIENT_GFGOODVAPORIZED",   // 06d9f47d
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_GFGOODVAPORIZED>);
    Serializer->RegisterType(0x30013u, "FLPACKET_CLIENT_GFSELECTOBJECT",   // 06d9f493
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_GFSELECTOBJECT>);
    Serializer->RegisterType(0x3001eu, "FLPACKET_CLIENT_SAVEGAME",   // 06d9f4a9
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_SAVEGAME>);
    Serializer->RegisterType(0x30020u, "FLPACKET_CLIENT_MINEASTEROID",   // 06d9f4bf
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_MINEASTEROID>);
    Serializer->RegisterType(0x30014u, "FLPACKET_CLIENT_MISSIONRESPONSE",   // 06d9f4d5
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_MISSIONRESPONSE>);
    Serializer->RegisterType(0x30027u, "FLPACKET_CLIENT_TRADERESPONSE",   // 06d9f4eb
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_TRADERESPONSE>);
    Serializer->RegisterType(0x30022u, "FLPACKET_CLIENT_DBGCREATESHIP",   // 06d9f501
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_DBGCREATESHIP>);
    Serializer->RegisterType(0x30023u, "FLPACKET_CLIENT_DBGLOADSYSTEM",   // 06d9f517
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM>);
    Serializer->RegisterType(0x3002eu, "FLPACKET_CLIENT_DBGFREESYSTEM",   // 06d9f52d
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_DBGFREESYSTEM>);
    Serializer->RegisterType(0x30024u, "FLPACKET_CLIENT_DOCK",   // 06d9f543
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_DOCK>);
    Serializer->RegisterType(0x3002du, "FLPACKET_CLIENT_DBGCONSOLE",   // 06d9f559
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_DBGCONSOLE>);
    Serializer->RegisterType(0x30025u, "FLPACKET_CLIENT_DBGDESTROYOBJECT",   // 06d9f56f
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_DBGDESTROYOBJECT>);
    Serializer->RegisterType(0x30030u, "FLPACKET_CLIENT_DBGRELOCATE_SHIP",   // 06d9f585
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_DBGRELOCATE_SHIP>);
    Serializer->RegisterType(0x3002fu, "FLPACKET_CLIENT_SETMANEUVER",   // 06d9f59b
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_SETMANEUVER>);
    Serializer->RegisterType(0x30036u, "FLPACKET_CLIENT_REQCOLLISIONGROUPS",   // 06d9f5b1
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS>);
    Serializer->RegisterType(0x3003bu, "FLPACKET_CLIENT_REQHULLSTATUS",   // 06d9f5c7
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQHULLSTATUS>);
    Serializer->RegisterType(0x30040u, "FLPACKET_CLIENT_REQUEST_USE_ITEM",   // 06d9f5dd
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQUEST_USE_ITEM>);
    Serializer->RegisterType(0x30044u, "FLPACKET_CLIENT_REQINVINCIBILITY",   // 06d9f5f3
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQINVINCIBILITY>);
    Serializer->RegisterType(0x30045u, "FLPACKET_CLIENT_MISSIONSAVEB",   // 06d9f609
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_MISSIONSAVEB>);
    Serializer->RegisterType(0x30046u, "FLPACKET_CLIENT_REQDIFFICULTYSCALE",   // 06d9f61f
                             &RpcPacketFactory<FPacket_FLPACKET_CLIENT_REQDIFFICULTYSCALE>);
    return 0;
}

// The callers, as in retail: IServerImpl::Startup runs FUN_06d9f100 (06d8f2cf) and
// FUN_06d9ace0 (06d8f2df), IClient slot 10 runs FUN_06d9bc80 (06d8ef4e) and FUN_06d9ace0
// (06d8ef5e), each on the serializer FUN_06d95e50 (RpcGetPacketSerializer) hands back.
// That is a singleton over .data:DAT_06dbbe38, so both halves fill ONE table and the
// second channel-1 registration answers 0x42 per id (06d960e4), which these registrators
// ignore.
