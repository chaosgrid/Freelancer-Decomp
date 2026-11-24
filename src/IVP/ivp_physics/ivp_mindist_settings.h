// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>
#include <cstddef>

// ============================================================================
// IVP_Mindist_Settings: retail's one global mindist/friction tolerance object
// at .data:0658aa10 (0x138 bytes, .bss). Constructed at DLL load (static
// initializer .data:063ea60c -> LAB_0634da30 -> LAB_0634da40 -> FUN_0634db40),
// which calls FUN_0634da50(0.01f) and FUN_0634db30(1.0f), then stores 1000 at
// +0x12c. Neither setter has another caller.
//
// Readers by global address (offset = address - 0x0658aa10):
//   DAT_0658aa10 +0x000   DAT_0658aa18 +0x008 (ramp base)   DAT_0658ab1c +0x10c
//   DAT_0658ab24 +0x114   DAT_0658ab28 +0x118   DAT_0658ab2c +0x11c
//   DAT_0658ab30 +0x120   DAT_0658ab34 +0x124   DAT_0658ab38 +0x128
//   DAT_0658ab3c +0x12c   DAT_0658ab40 +0x130   DAT_0658ab44 +0x134
// Retail strings name none of the fields. Names follow the IVP SDK's
// set_collision_tolerance, whose formulas FUN_0634da50 matches term for term;
// +0x130 and +0x134 have no provable SDK counterpart.
// ============================================================================
class IVP_Mindist_Settings
{
public:
    float real_coll_dist;      // +0x000 = D * 0.1f
    float min_coll_dists;      // +0x004 = D * 0.9f + D * 0.1f
    float CollDistRamp[0x40];  // +0x008..+0x104: [k] = k * (1/64) * (max_coll_dist - min_coll_dists) + min_coll_dists
                               // Read with `(StateFlags >> 22) & 0xff`, which stays <= 0x3f
                               // (see IVP_MindistSettingsRamp).
    float max_coll_dist;       // +0x108 = min_coll_dists (same x87 value, stored twice)
    // +0x10c: the gap a friction contact is driven to (the friction solver's
    // desired distance), every new contact point's starting gap, and the
    // single-friction normal-impulse target (06356568).
    float friction_dist;       // +0x10c = D + max_coll_dist        (DAT_0658ab1c)
    // +0x110: the gap a fresh contact point is parked at (ivp_contact_point.cpp).
    float keeper_dist;         // +0x110 = D * 0.3f + friction_dist
    // +0x114: sqrt(2 * 9.81 * (keeper_dist - min_coll_dists)), the speed of a
    // fall from keeper_dist.
    float speed_after_keeper_dist; // +0x114                        (DAT_0658ab24)
    // +0x118: slack above friction_dist; a kept friction contact whose gap
    // exceeds friction_dist + this moves to the head of its list.
    float distance_keepers_safety; // +0x118 = D * 0.01f             (DAT_0658ab28)
    // +0x11c: the gap at or above which a friction contact leaves the friction
    // system. Other readers: 0635646a, 0634f2b9.
    float max_distance_for_friction; // +0x11c = D * 2.5f + friction_dist (DAT_0658ab2c)
    // +0x120: a contact whose gap exceeds this leaves the impact system.
    float max_distance_for_impact_system; // +0x120 = D * 20.0f + friction_dist (DAT_0658ab30)
    float mindist_change_force_dist;      // +0x124 = min_coll_dists * 0.1f (DAT_0658ab34)
    // +0x128: the base of the impact solver's rescue speed.
    float min_vertical_speed_at_collision; // +0x128 = D + D        (DAT_0658ab38)
    // +0x12c: cap on spawned mindists, checked against get_spawned_mindist_count
    // before each spawn; PhySys overwrites it with MAX_SPAWNED_MINDIST_COUNT.
    int32_t max_spawned_mindist_count; // +0x12c = 1000 (constructor) (DAT_0658ab3c)
    float EventNudgeEpsScale;  // +0x130 = 1.0f (FUN_0634db30)       (DAT_0658ab40)
    int32_t ToleranceDecayCounter; // +0x134 not written by the constructor; a counter of
                               // FUN_0634f320 (DAT_0658ab44). Zero in .bss.

    // FUN_0634db40
    IVP_Mindist_Settings();

    // FUN_0634da50
    void SetCollisionTolerance(float D);

    // FUN_0634db30
    void SetEventNudgeEpsScale(float Value);
};

static_assert(offsetof(IVP_Mindist_Settings, CollDistRamp) == 0x008, "IVP_Mindist_Settings+0x008");
static_assert(offsetof(IVP_Mindist_Settings, max_coll_dist) == 0x108, "IVP_Mindist_Settings+0x108");
static_assert(offsetof(IVP_Mindist_Settings, friction_dist) == 0x10C, "IVP_Mindist_Settings+0x10c");
static_assert(offsetof(IVP_Mindist_Settings, distance_keepers_safety) == 0x118, "IVP_Mindist_Settings+0x118");
static_assert(offsetof(IVP_Mindist_Settings, max_distance_for_friction) == 0x11C, "IVP_Mindist_Settings+0x11c");
static_assert(offsetof(IVP_Mindist_Settings, max_spawned_mindist_count) == 0x12C, "IVP_Mindist_Settings+0x12c");
static_assert(offsetof(IVP_Mindist_Settings, ToleranceDecayCounter) == 0x134, "IVP_Mindist_Settings+0x134");
static_assert(sizeof(IVP_Mindist_Settings) == 0x138, "IVP_Mindist_Settings is 0x138 bytes");

// The global object (retail .data:0658aa10).
extern IVP_Mindist_Settings G_IVP_MindistSettings;

// DAT_0658aa18[Index], Index = `(StateFlags >> 22) & 0xff` (0634f3bc, 0634fe77),
// unclamped as in retail. The index never leaves the ramp: the mindist ctor
// (FUN_0634db70) sets bits 22-29 to 0x3f and the only other writer, 0634f592,
// decrements them only while they are nonzero.
inline float IVP_MindistSettingsRamp(uint32_t Index)
{
    return G_IVP_MindistSettings.CollDistRamp[Index];
}
