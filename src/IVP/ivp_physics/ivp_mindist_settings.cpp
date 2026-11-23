// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_Mindist_Settings (retail 0634da50-0634db66; see ivp_mindist_settings.h).
//
// x87: retail keeps intermediates on the FPU stack (53-bit precision under the
// VC6 runtime), so chained values are `double` here and cast to float where
// retail stores one.

#include "ivp_mindist_settings.h"

#include <cmath>

// Constructed at DLL load (LAB_0634da30 -> LAB_0634da40 -> FUN_0634db40).
FIVP_MindistSettingsStorage G_IVP_MindistSettingsStorage;
IVP_Mindist_Settings& G_IVP_MindistSettings = G_IVP_MindistSettingsStorage.Settings;

// FUN_0634db40. +0x134 is left alone.
IVP_Mindist_Settings::IVP_Mindist_Settings()
{
    SetCollisionTolerance(0.00999999977648258f); // 0x3c23d70a
    SetEventNudgeEpsScale(1.0f);
    max_spawned_mindist_count = 0x3e8;
}

// FUN_0634db30
void IVP_Mindist_Settings::SetEventNudgeEpsScale(float Value)
{
    EventNudgeEpsScale = Value;
}

// FUN_0634da50. Constants: DAT_063a5464 0.1f, DAT_063a53fc 0.9f, DAT_063a55a0
// 0.015625f, DAT_063a559c 0.3f, DAT_063a5598 19.62f, DAT_063a5404 0.01f,
// DAT_063a5594 2.5f, DAT_063a543c 20.0f.
void IVP_Mindist_Settings::SetCollisionTolerance(float D)
{
    const double Dd = D;

    const double Tenth = Dd * (double)0.1f;
    real_coll_dist = (float)Tenth;
    const double MinDist = Dd * (double)0.9f + Tenth;
    min_coll_dists = (float)MinDist;
    max_coll_dist = (float)MinDist;

    // Filled from the top (k = 0x3f) down to k = 0, re-reading the stored floats.
    for (int K = 0x3f; K >= 0; --K)
    {
        const double Diff = (double)max_coll_dist - (double)min_coll_dists;
        const double Step = (double)K * (double)0.015625f;
        CollDistRamp[K] = (float)(Diff * Step + (double)min_coll_dists);
    }

    // Top and Above stay unrounded on the stack for the later terms.
    const double Top = Dd + (double)max_coll_dist;
    friction_dist = (float)Top;

    const double Above = Dd * (double)0.3f + Top;
    keeper_dist = (float)Above;
    speed_after_keeper_dist = (float)std::sqrt((Above - (double)min_coll_dists) * (double)19.62f);

    distance_keepers_safety = (float)(Dd * (double)0.01f);
    max_distance_for_friction = (float)(Dd * (double)2.5f + Top);
    max_distance_for_impact_system = (float)(Dd * (double)20.0f + Top);
    min_vertical_speed_at_collision = (float)(Dd + Dd);
    mindist_change_force_dist = (float)((double)min_coll_dists * (double)0.1f);
}
