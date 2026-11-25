// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Timing -- retail CTiming.cpp (0x06307f20-0x063089d0). Declared in common_timing.h.
#include "common_timing.h"
#include "dacom.h" // FDUMP

#include <cmath>
#include <cstddef>
#include <intrin.h>
#include <windows.h>

namespace
{
    const char kTimingSource[] = "E:\\FL\\Scratch\\Source\\Common\\CTiming.cpp";
    double g_timing_frequency = 0.0;
    double g_timing_period = 0.0;
    double g_timing_global = 0.0;

    // DAT_063ed210: the cached GetMachineSpeed() result in whole MHz; negative = not measured.
    int g_machine_speed_mhz = -1;

    long long ReadPerformanceCounterOrZero()
    {
        LARGE_INTEGER Counter;
        if (!QueryPerformanceCounter(&Counter))
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kTimingSource, 0x56,
                  "QueryPerformanceCounter returned false");
            return 0;
        }
        return Counter.QuadPart;
    }

    void CaptureStart(Timing::Delta* Self)
    {
        Timing::init();
        Self->StartTicks = ReadPerformanceCounterOrZero();
        Self->StartTickCount = GetTickCount();
        Self->bStarted = true;
        Self->bStopped = false;
    }

    void CaptureStop(Timing::Delta* Self)
    {
        Timing::init();
        Self->StopTicks = ReadPerformanceCounterOrZero();
        Self->StopTickCount = GetTickCount();
        Self->bStopped = true;
    }

    double CalculateDeltaSeconds(Timing::Delta* Self)
    {
        if (!Self->bStarted || !Self->bStopped)
        {
            Self->Dt = 0.0;
            return Self->Dt;
        }

        const long long QpcDelta = Self->StopTicks - Self->StartTicks;
        Self->Dt = (double)QpcDelta * g_timing_period;

        // _ftol keeps the low 32 bits; the difference and its abs wrap in 32 bits and the test
        // is signed. More than 50 ms of disagreement between QPC and GetTickCount falls back
        // to the tick count (unsigned) * 0.001.
        const unsigned long FtolNegMs = (unsigned long)(long long)(Self->Dt * -1000.0);
        const unsigned long TickDelta = Self->StopTickCount - Self->StartTickCount;
        int AbsDiff = (int)(0UL - FtolNegMs - TickDelta);
        if (AbsDiff < 0)
            AbsDiff = (int)(0UL - (unsigned long)AbsDiff);
        if (AbsDiff > 50)
            Self->Dt = (double)TickDelta * 0.001;

        return Self->Dt;
    }

    // FUN_06307ff0. Each round takes six RDTSC deltas across a 20 ms Sleep (a sample that goes
    // backward is retaken), drops the smallest and largest, and converges when the rest
    // deviate from their mean by under 0.1%: Hz = mean * 50. After 25 failed rounds it warns
    // and returns 750 MHz. The RDTSC readings are compared signed.
    double TestClockSpeed()
    {
        for (int Round = 0; Round <= 0x18; ++Round)
        {
            double Samples[6];
            for (int i = 0; i < 6; ++i)
            {
                long long Start, End;
                do
                {
                    Start = (long long)__rdtsc();
                    Sleep(0x14);
                    End = (long long)__rdtsc();
                } while (End < Start);
                Samples[i] = (double)(End - Start);
            }

            int Count = 6;
            {
                int MinIdx = 0;
                for (int i = 1; i < Count; ++i)
                    if (Samples[i] < Samples[MinIdx])
                        MinIdx = i;
                for (int i = MinIdx; i < Count - 1; ++i)
                    Samples[i] = Samples[i + 1];
                --Count;
            }
            {
                int MaxIdx = 0;
                for (int i = 1; i < Count; ++i)
                    if (Samples[i] > Samples[MaxIdx])
                        MaxIdx = i;
                for (int i = MaxIdx; i < Count - 1; ++i)
                    Samples[i] = Samples[i + 1];
                --Count;
            }

            double Mean = 0.0;
            for (int i = 0; i < Count; ++i)
                Mean += Samples[i];
            Mean /= Count;

            double MaxDev = 0.0;
            for (int i = 0; i < Count; ++i)
            {
                const double Dev = std::fabs(Samples[i] - Mean);
                if (Dev > MaxDev)
                    MaxDev = Dev;
            }

            if (MaxDev / Mean < 0.001)
                return Mean * 50.0;
        }

        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kTimingSource, 0x11e,
              "TestClockSpeed - failed!");
        return 1000000.0 * 750.0;
    }
}

namespace Timing
{
    void __cdecl init()
    {
        // RVA 0xa8270
        if (g_timing_frequency != 0.0)
            return;

        LARGE_INTEGER Freq;
        if (!QueryPerformanceFrequency(&Freq))
        {
            FDUMP(kFdumpError,
                  "%s(%d) : *** ERROR: QueryPerformanceFrequence failed. GetLastError returned %u",
                  kTimingSource, 0x137, GetLastError());
            g_timing_frequency = 0.0;
            g_timing_period = 1.0 / g_timing_frequency;
            return;
        }

        // The signed 64-bit count.
        g_timing_frequency = (double)Freq.QuadPart;
        g_timing_period = 1.0 / g_timing_frequency;
    }

    long long __cdecl read_ticks()
    {
        // RVA 0xa8300
        init();
        return ReadPerformanceCounterOrZero();
    }

    double __cdecl seconds(const long long& Ticks)
    {
        // RVA 0xa83e0
        return (double)Ticks * g_timing_period;
    }

    double __cdecl read()
    {
        // RVA 0xa83f0
        init();
        const long long Ticks = ReadPerformanceCounterOrZero();
        return (double)Ticks * g_timing_period;
    }

    void __cdecl UpdateGlobalTime(double Dt)
    {
        // RVA 0xa7f60
        g_timing_global += Dt;
    }

    double __cdecl GetGlobalTime()
    {
        // RVA 0xa7f80
        return g_timing_global;
    }

    int __cdecl GetMachineSpeed()
    {
        // FUN_06307f20: whole MHz via tenths (constants 063a30f0 / 0639c478 / 063a0460); both
        // _ftol calls keep the low 32 bits.
        if (g_machine_speed_mhz < 0)
        {
            int const TenthsMhz = (int)(long long)(TestClockSpeed() / 1000000.0 * 10.0);
            g_machine_speed_mhz = (int)(long long)((double)TenthsMhz * 0.1);
        }
        return g_machine_speed_mhz;
    }

    Delta::Delta()
    {
        // RVA 0xa84f0 (no vptr)
        Dt = 0.0;
        StartTicks = 0;
        StopTicks = 0;
        StartTickCount = 0;
        StopTickCount = 0;
        bStarted = false;
        bStopped = false;
    }

    void Delta::init()
    {
        // RVA 0xa8520
        if (bStarted)
            return;
        CaptureStart(this);
    }

    void Delta::start()
    {
        // RVA 0xa8620
        CaptureStart(this);
    }

    void Delta::stop()
    {
        // RVA 0xa8720
        if (!bStarted)
            CaptureStart(this);
        CaptureStop(this);
    }

    void Delta::swap()
    {
        // RVA 0xa8900
        if (!bStopped)
            return;
        StartTicks = StopTicks;
        StartTickCount = StopTickCount;
        bStarted = true;
        bStopped = false;
    }

    double Delta::calc_dt()
    {
        // RVA 0xa8930
        return CalculateDeltaSeconds(this);
    }

    double Delta::read_dt()
    {
        // RVA 0xa89d0: start / stop / calc_dt / swap inlined. Its FDUMP masks come from an
        // out-of-line builder (FUN_062b7750) and equal 0x100001.
        if (!bStarted)
            start();
        stop();
        const double Elapsed = calc_dt();
        swap();
        return Elapsed;
    }
}
