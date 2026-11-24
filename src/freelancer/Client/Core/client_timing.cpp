// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Core/globals.h"
#include "common_misc.h"   // SinglePlayer
#include "common_timing.h" // Timing::Delta
#include "dalib_net.h"
#include "Common/Audio/menu_sound_arch.h" // PauseAllClientSounds / ResumePausedClientSounds (FUN_00428550 / 80)

#include <windows.h>

namespace
{
    // Retail freelancer.exe frame-timer BSS. FUN_0042d770 is the tick, 0042d650..0042d6e0 the
    // accessor family, FUN_005b2360 the main-loop wrapper.
    Timing::Delta G_FrameDelta;   // DAT_00667cf8

    double G_ElapsedTime = 0.0;   // _DAT_00667d38 — sum of capped dt, never pause-gated
    double G_FrameDtCapped = 0.0; // _DAT_00667d40 — dt clamped to 1 / kMinFps
    double G_FrameDtRaw = 0.0;    // _DAT_00667d48 — dt as measured
    double G_AvgFrameTime = 0.0;  // _DAT_00667d60 — seconds/frame over the last window
    // _DAT_00610a00 — seconds accumulated in the window. .data ships the double 10.0, so the
    // very first tick already closes a window.
    double G_FpsWindow = 10.0;
    unsigned G_FrameCount = 0;    // DAT_00667d50
    unsigned G_FpsWindowFrames = 0; // DAT_00667d6c
    // DAT_00667d20 — timeGetTime() of the current frame; seeded at load by the XC entry
    // LAB_0042d650.
    unsigned long G_LastTickMs = timeGetTime();
    bool G_bNetworkStalled = false; // DAT_00667d58
    int G_PauseDepth = 0;         // DAT_00667d68 — frame-time getters (0042d680/6a0)
    int G_ServerTickPauseDepth = 0; // DAT_00667d54 — FUN_0042d740 skips IServer::Update
    double G_AccumulatedClientTime = 0.0; // _DAT_0067ecd8 — FUN_005b2360's own clock

    // DAT_00610a0c — busy-wait fps cap; .data ships 200.0f and both writers take it from
    // COMMON.DLL::RtcSlider.
    float G_FpsCap = 200.0f;
    // DAT_00610a08 — the minimum frame rate: dt is clamped to 1 / it. .data ships 1.0f and
    // nothing in the PE ever writes it, so the capped and raw dt only diverge on frames longer
    // than a whole second.
    const float kMinFps = 1.0f;
    // DAT_005ca228 — the fps window length.
    const double kFpsWindowSeconds = 2.0;

    void TickFrameTimer()
    {
        // FUN_0042d770.
        const double MinFrameSeconds = 1.0 / static_cast<double>(G_FpsCap);
        double Dt = 0.0;
        while (true)
        {
            G_FrameDelta.stop();
            Dt = G_FrameDelta.calc_dt();
            if (Dt >= MinFrameSeconds)
                break;
            Sleep(1);
        }
        G_FrameDelta.swap();

        G_FrameDtRaw = Dt;

        const double MaxFrameSeconds = 1.0 / static_cast<double>(kMinFps);
        // x87 compare: keep Dt only when Dt < Max; Max otherwise, including a NaN Dt.
        G_FrameDtCapped = (Dt < MaxFrameSeconds) ? Dt : MaxFrameSeconds;

        G_ElapsedTime += G_FrameDtCapped;
        G_FrameCount += 1;
        G_FpsWindowFrames += 1;
        G_FpsWindow += G_FrameDtCapped;
        if (G_FpsWindow > kFpsWindowSeconds)
        {
            // Seconds per frame, not frames per second.
            G_AvgFrameTime = G_FpsWindow / static_cast<double>(G_FpsWindowFrames);
            G_FpsWindowFrames = 0;
            G_FpsWindow = 0.0;
        }

        G_LastTickMs = timeGetTime();
        G_bNetworkStalled = false;
        // In multiplayer the flag is set when no DirectPlay message has arrived for more than a
        // second (unsigned compare; GetLastMsgTimestamp is the DALib import 065c2bc0).
        if (!SinglePlayer())
        {
            if (G_LastTickMs - CDPClient::GetLastMsgTimestamp() > 1000)
                G_bNetworkStalled = true;
        }
    }
}

void TickClientFrameTimer()
{
    // FUN_0042d770 on its own — the pre-init call primes the delta without touching the client
    // clock.
    TickFrameTimer();
}

void UpdateClientTiming()
{
    // FUN_005b2360 — tick, then accumulate the client clock: single
    // player uses the capped dt (FUN_0042d680), multiplayer the raw one (FUN_0042d6a0). Both
    // getters return 0 while paused, so _DAT_0067ecd8 is a separate clock from G_ElapsedTime.
    // Timing::UpdateGlobalTime is not here: FUN_005b2890 calls it (client_frame_tick.cpp).
    TickFrameTimer();
    if (SinglePlayer())
        G_AccumulatedClientTime += GetClientFrameTimeCapped();
    else
        G_AccumulatedClientTime += GetClientFrameTimeRaw();
}

double GetClientAccumulatedTime()
{
    return G_AccumulatedClientTime;
}

void SeedClientAccumulatedTime(double Seconds)
{
    // FUN_005ab380 (both paths): the character-select response's +8 / +0xc dwords go into
    // DAT_0067ecd8 / DAT_0067ecdc, the two halves of the double FUN_005b2360 advances.
    G_AccumulatedClientTime = Seconds;
}

float GetClientFrameDeltaSeconds()
{
    return static_cast<float>(SinglePlayer() ? GetClientFrameTimeCapped()
                                             : GetClientFrameTimeRaw());
}

double GetClientFrameTimeCapped()
{
    // FUN_0042d680 — 0.0 while paused.
    if (G_PauseDepth != 0)
        return 0.0;
    return G_FrameDtCapped;
}

double GetClientFrameTimeRaw()
{
    // FUN_0042d6a0 — one call site (FUN_005b2360); 0.0 while paused.
    if (G_PauseDepth != 0)
        return 0.0;
    return G_FrameDtRaw;
}

double GetClientElapsedTime()
{
    // FUN_0042d660 — not pause-gated.
    return G_ElapsedTime;
}

unsigned GetClientFrameCount()
{
    // FUN_0042d6e0.
    return G_FrameCount;
}

unsigned long GetClientFrameTimestampMs()
{
    // FUN_0042d670.
    return G_LastTickMs;
}

void PushClientTimingPause()
{
    // FUN_0042d6c0.
    G_PauseDepth += 1;
}

void PopClientTimingPause()
{
    // FUN_0042d6d0.
    G_PauseDepth -= 1;
}

void SetClientServerTickPause(bool bPause)
{
    // FUN_0042d6f0. Counter is DAT_00667d54. Pausing from 0 in single player pauses every
    // playing sound (FUN_00428550) first; reaching 0 again resumes them (FUN_00428580).
    if (bPause)
    {
        if (G_ServerTickPauseDepth == 0 && SinglePlayer())
            PauseAllClientSounds();
        G_ServerTickPauseDepth += 1;
        return;
    }

    G_ServerTickPauseDepth -= 1;
    if (G_ServerTickPauseDepth != 0)
        return;
    if (SinglePlayer())
        ResumePausedClientSounds();
}

bool IsClientServerTickPaused()
{
    // FUN_0042d730.
    return G_ServerTickPauseDepth > 0;
}

bool ShouldSkipLocalServerTick()
{
    // FUN_0042d740 — the frame loop gates IServer Update + ElapseTime on it.
    if (!SinglePlayer())
        return false;
    return G_ServerTickPauseDepth > 0;
}

void SetClientFpsCap(float Fps)
{
    // DAT_00610a0c — retail stores RtcSlider::get_fps_cap entering a base and
    // RtcSlider::get_default_fps_cap leaving it; no test.
    G_FpsCap = Fps;
}
