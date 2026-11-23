// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Jump-effect descriptors of retail "E:\FL\Scratch\Source\Client\Space\JumpEffect.cpp"
// (freelancer.exe .text 0x00500000-0x00520000): the [JumpGateEffect] / [JumpShipEffect]
// records the loader FUN_005020a0 builds (see jump_effect.h/.cpp and space_scene_load.cpp).
//
//   - FUN_00502ba0 is JumpGateEffect's constructor ("null solar passed to JumpGateEffect
//     constructor"): looks up its node by nickname (default "jump_effect_default") in the
//     list FUN_005020a0 builds, then resolves hardpoints via FindHardpoint_PS.
//   - FUN_00502b00 lazily default-constructs the global JumpShipEffectDesc ("no
//     JumpShipEffectDesc was loaded, creating a default").

#include "common_id.h"
#include "common_archetype.h" // CacheString

// One glow ring of a [JumpGateEffect] section (0xc bytes, five of them). FUN_00502a80 is its
// constructor, not a destructor: ID_String::clear on +0, CacheString::clear on +4, return
// this (the loader FUN_005020a0 runs it on each of the five). Keys:
// glow_ring_effect (+0, CreateID), glow_ring_hp (+4, StringAlloc), glow_create_time (+8).
struct FJumpGateGlowRing
{
    ID_String Effect;  // +0x00
    CacheString Hp;    // +0x04
    float CreateTime;  // +0x08

    FJumpGateGlowRing()
    {
        Effect.clear();
        Hp.clear();
    }
};
static_assert(sizeof(FJumpGateGlowRing) == 0xc, "glow ring 0xc");

// The payload of one DAT_00674fd8 list node (the node is operator new(0x70): next, prev, then
// this 0x68-byte record, filled by FUN_005020a0 from a [JumpGateEffect] section and copied in
// by FUN_005048a0). Offsets from the loader's stack record [ESP+0x24] and from the readers
// FUN_00502ba0 (+0x00, +0x04.., +0x40) and FUN_00503440 (+0x44..+0x63).
struct FJumpEffectDesc
{
    ID_String Nickname;               // +0x00  nickname
    FJumpGateGlowRing Rings[5];       // +0x04
    int RingCount;                    // +0x40  min of the three per-ring key counts
    float JumpOutTime;                // +0x44  jump_out_time
    float JumpOutTunnelTime;          // +0x48  jump_out_tunnel_time
    float JumpInTunnelTime;           // +0x4c  jump_in_tunnel_time
    float JumpInTime;                 // +0x50  jump_in_time
    float KillTimeBeforeDone;         // +0x54  kill_time_before_done
    ID_String JumpTunnelEffect;       // +0x58  jump_tunnel_effect
    ID_String JumpTunnel;             // +0x5c  jump_tunnel (a GateTunnel id)
    unsigned char JumpAmbient[3];     // +0x60  jump_ambient (three ftol'd floats)
    unsigned char JumpBackgroundColor[3]; // +0x63 jump_background_color
    unsigned char Pad66[2];
};
static_assert(sizeof(FJumpEffectDesc) == 0x68, "jump effect desc 0x68 (list node 0x70)");

// JumpShipEffectDesc (retail name -- see FUN_00502b00's embedded warning string).
// operator_new(0x14) = 5 consecutive ID_String fields, each ID_String::clear()'d by
// FUN_00502b00's default-construction path. The [JumpShipEffect] loader FUN_005020a0 fills
// Ids[1] jump_out_effect, Ids[2] jump_in_effect, Ids[3] jump_done_effect_player, Ids[4]
// jump_done_effect_nonplayer; Ids[0] is never written by it (meaning unknown).
struct JumpShipEffectDesc
{
    ID_String Ids[5];

    // Retail's lazy-default-construct path (FUN_00502b00) explicitly clears all 5 ids
    // rather than relying on ID_String's (trivial) default state.
    JumpShipEffectDesc()
    {
        for (int Index = 0; Index < 5; ++Index)
            Ids[Index].clear();
    }
};

// The rest of this range is the sun / star-sphere code (star_sphere.h/.cpp). The three empty
// structs below are unused placeholders; the real descriptors live there.
//
// "[spines]" descriptor -- FUN_0050fba0 (star_sphere.cpp). Fields as offsets in the target:
struct NebulaSpineDesc
{
    // +0x10 CacheString Shape;  // "shape=" (FUN_00417c50 = shape-id hash; zeroed when the key's value is empty)
    // +0x38 float MinRadius;    // "min_radius="
    // +0x3c float MaxRadius;    // "max_radius="
    // "radius_scale=" (default 1.0) and "spine=" (5 floats/entry) are also parsed.
};

// "[star_glow]" descriptor -- FUN_0050c950; see FStarGlowDesc (star_sphere.h).
struct StarGlowDesc
{
};

// "[lens_glow]" descriptor -- FUN_0050d420, embedded in FSunEffectBody (+0x288..+0x2f4); see
// LensGlowLoadFromIni (star_sphere.cpp).
struct LensGlowDesc
{
};
