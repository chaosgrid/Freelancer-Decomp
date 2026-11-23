// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_ui_pulse.h"

// LAB_00561be0.
unsigned char G_HudChromeColorRgba[4] = {0x87u, 0xC3u, 0xE0u, 0xC0u};

// LAB_00561c00 .. LAB_00561d80 (bytes R,G,B,A in memory = little-endian 0xAABBGGRR).
unsigned long G_NnPaletteArgb_679bb0 = 0xFFE0C387u; // LAB_00561c00
unsigned long G_NnPaletteArgb_679bac = 0xFF1DBF3Bu; // LAB_00561c20
unsigned long G_NnPaletteArgb_679ba8 = 0xFF1D1DBFu; // LAB_00561c40
unsigned long G_NnPaletteArgb_679ba4 = 0xFF52EAF5u; // LAB_00561c60
unsigned long G_NnPaletteArgb_679b9c = 0xFFC20088u; // LAB_00561ca0
unsigned long G_NnPaletteArgb_679b98 = 0xFFE04848u; // LAB_00561cc0
unsigned long G_NnPaletteArgb_679b94 = 0xFF808080u; // LAB_00561ce0
unsigned long G_NnPaletteArgb_679b90 = 0xFFC5FBA8u; // LAB_00561d00
unsigned long G_NnPaletteArgb_679b88 = 0xFFFD7EFFu; // LAB_00561d40
unsigned long G_NnPaletteArgb_679b84 = 0xFFFFFFFFu; // LAB_00561d60
unsigned long G_NnPaletteArgb_679b80 = 0xFFFF5ACEu; // LAB_00561d80

namespace
{
    // DAT_0067dce8 / DAT_0067dce4 -- .bss, zero until the first FUN_0059d580 reset.
    float GPulsePhase = 0.0f;
    unsigned char GPulseDir = 0;
    // DAT_00679b7c bit 0 / DAT_00679b78: FUN_00561da0's function-local static, zero until the
    // first call's latch stores 0xFFB9B9B9.
    bool GPulseMidInited = false;
    unsigned long GPulseMidArgb = 0; // DAT_00679b78 lazy init

    unsigned char BlendByte(unsigned char From, unsigned char To, float T)
    {
        // The x87 product is exact; double keeps it exact too.
        int const Diff = static_cast<int>(From) - static_cast<int>(To);
        int const Delta = static_cast<int>(static_cast<double>(Diff) * static_cast<double>(T));
        return static_cast<unsigned char>(To + Delta);
    }

    unsigned long BlendArgb(unsigned long From, unsigned long To, float T)
    {
        unsigned char const Fr = static_cast<unsigned char>(From);
        unsigned char const Fg = static_cast<unsigned char>(From >> 8);
        unsigned char const Fb = static_cast<unsigned char>(From >> 16);
        unsigned char const Fa = static_cast<unsigned char>(From >> 24);
        unsigned char const Tr = static_cast<unsigned char>(To);
        unsigned char const Tg = static_cast<unsigned char>(To >> 8);
        unsigned char const Tb = static_cast<unsigned char>(To >> 16);
        unsigned char const Ta = static_cast<unsigned char>(To >> 24);
        unsigned long const R = BlendByte(Fr, Tr, T);
        unsigned long const G = BlendByte(Fg, Tg, T);
        unsigned long const B = BlendByte(Fb, Tb, T);
        unsigned long const A = BlendByte(Fa, Ta, T);
        return R | (G << 8) | (B << 16) | (A << 24);
    }
}

void NnUiPulseReset()
{
    // FUN_0059d580: 0.3f (DAT_005e5dc4) * 0.5f (DAT_005c75e0); halving is exact.
    GPulseDir = 0;
    GPulsePhase = static_cast<float>(static_cast<double>(0.3f) * static_cast<double>(0.5f));
}

void NnUiPulseAdvance(double DeltaSeconds)
{
    // The head of FUN_0059e080. The sum is compared against 0.3f (DAT_005e5dc4) BEFORE it is
    // rounded; the wrap loop keeps the unrounded value, flips the direction once per wrap and
    // stores once at the end. "<= or unordered" leaves the loop.
    double Phase = static_cast<double>(GPulsePhase) + DeltaSeconds;
    GPulsePhase = static_cast<float>(Phase);
    if (!(Phase > static_cast<double>(0.3f)))
        return;
    unsigned char Dir = GPulseDir;
    do
    {
        Phase -= static_cast<double>(0.3f);
        Dir = (Dir == 0) ? 1 : 0;
    } while (Phase > static_cast<double>(0.3f));
    GPulsePhase = static_cast<float>(Phase);
    GPulseDir = Dir;
}

unsigned long NnUiPulseIdleArgb()
{
    return G_NnPaletteArgb_679bb0;
}

float NnUiPulsePhaseNorm()
{
    // FUN_0059d570; every caller stores it as a float.
    return GPulsePhase / 0.3f;
}

unsigned char NnUiPulseDirection()
{
    // FUN_0059d560.
    return GPulseDir;
}

void NnUiPulseBlendArgb(unsigned long* OutArgb)
{
    // FUN_00561da0.
    if (!GPulseMidInited)
    {
        // The local static's atexit registration is not ported.
        GPulseMidInited = true;
        GPulseMidArgb = 0xFFB9B9B9u;
    }

    // FUN_0059d570.
    float const Norm = GPulsePhase / 0.3f;
    unsigned long From = 0;
    unsigned long To = 0;
    float T = 0.0f;

    if (GPulseDir == 0)
    {
        if (Norm < 0.5f)
        {
            From = GPulseMidArgb;
            To = G_NnPaletteArgb_679bb0;
            T = Norm + Norm;
        }
        else
        {
            From = G_NnPaletteArgb_679ba4;
            To = GPulseMidArgb;
            T = (Norm - 0.5f) + (Norm - 0.5f);
        }
    }
    else
    {
        if (Norm < 0.5f)
        {
            From = GPulseMidArgb;
            To = G_NnPaletteArgb_679ba4;
            T = Norm + Norm;
        }
        else
        {
            From = G_NnPaletteArgb_679bb0;
            To = GPulseMidArgb;
            T = (Norm - 0.5f) + (Norm - 0.5f);
        }
    }

    *OutArgb = BlendArgb(From, To, T);
}

void NnUiPulseBlendFromArgb(unsigned long* OutArgb, unsigned long BaseArgb)
{
    // FUN_00561f20.
    float const Norm = GPulsePhase / 0.3f;
    unsigned char const Dir = GPulseDir;

    // Per channel, byte arithmetic (-0.5 is DAT_005cef60); Dim is the grey of their mean at
    // full alpha.
    int MidSum = 0;
    for (int Channel = 0; Channel < 3; ++Channel)
    {
        int const Shift = Channel * 8;
        int const BaseByte = static_cast<int>((BaseArgb >> Shift) & 0xffu);
        int const HotByte = static_cast<int>((G_NnPaletteArgb_679ba4 >> Shift) & 0xffu);
        int const Half = static_cast<int>(static_cast<double>(HotByte - BaseByte) * -0.5);
        MidSum += static_cast<unsigned char>(BaseByte - Half);
    }
    unsigned long const Grey = static_cast<unsigned long>(MidSum / 3) & 0xffu;
    unsigned long const DimArgb = Grey | (Grey << 8) | (Grey << 16) | 0xff000000u;

    // 0.5 is the double DAT_005c75e8.
    unsigned long From = 0;
    unsigned long To = 0;
    float T = 0.0f;
    if (Dir == 0)
    {
        if (Norm < 0.5f)
        {
            From = DimArgb;
            To = BaseArgb;
            T = Norm + Norm;
        }
        else
        {
            From = G_NnPaletteArgb_679ba4;
            To = DimArgb;
            T = (Norm - 0.5f) + (Norm - 0.5f);
        }
    }
    else
    {
        if (Norm < 0.5f)
        {
            From = DimArgb;
            To = G_NnPaletteArgb_679ba4;
            T = Norm + Norm;
        }
        else
        {
            From = BaseArgb;
            To = DimArgb;
            T = (Norm - 0.5f) + (Norm - 0.5f);
        }
    }

    *OutArgb = BlendArgb(From, To, T);
}

void NnReputationColorArgb(unsigned long* OutArgb, float Attitude)
{
    // FUN_0045a650. The thresholds are DAT_005ce7ec, DAT_005ce7f0, DAT_005ce7f8 and
    // DAT_005ce7f4; an unordered attitude ends up white.
    if (Attitude <= -0.6f)
    {
        *OutArgb = G_NnPaletteArgb_679ba8;
        return;
    }
    if (Attitude <= -0.2f)
    {
        *OutArgb = G_NnPaletteArgb_679b84;
        return;
    }
    if (Attitude >= 0.6f)
    {
        *OutArgb = G_NnPaletteArgb_679bac;
        return;
    }
    // Both ways white, as in retail.
    if (Attitude >= 0.2f)
    {
        *OutArgb = G_NnPaletteArgb_679b84;
        return;
    }
    *OutArgb = G_NnPaletteArgb_679b84;
}
