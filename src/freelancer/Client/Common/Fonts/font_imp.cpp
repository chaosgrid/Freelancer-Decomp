// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Fonts/font_imp.h"

#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath
#include "dacom.h"
#include "Common/Fonts/font_atlas.h"
#include "Common/Fonts/font_string.h"
#include "Core/globals.h"
#include "Core/widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "Common/Graphics/screen_quad_state.h"

#include <cmath>
#include <cstring>
#include <cwchar>
#include <list>
#include <stdarg.h>
#include <stdio.h>

namespace
{
char const kFontCpp[] = "E:\\FL\\Scratch\\Source\\Client\\cmn\\Font.cpp";

// .rdata:005c8d38 -- the calibration pangram every autoscale search measures.
wchar_t const kPangram[] = L"In the beginning the Earth was without form and void.";

// .rdata:005c8d30 -- the reference width factor (1/800) of the A-variant target.
float const kPangramWidthScale = 0.00125f;

// DAT_0061651c / DAT_00616520 -- the font list (std::list<FontImp>, node 0x178 = links + 0x170).
// Built by the XC thunk LAB_004163e0 (head node, size 0), torn down by its atexit thunk
// LAB_00416420, which runs FUN_00416490 on every node left.
std::list<FontImp> G_FontList;

// DAT_006105b4 -- "measure the whole string in one call" (PE init 1). The first MeasureString
// settles it from the OS version: set when dwMajorVersion > 4, cleared when GetVersionExA fails.
bool G_bFontWholeStringExtent = true;
// DAT_0061669b -- once-guard of that probe (BSS 0), set before GetVersionExA is called.
bool G_bFontOsVersionProbed = false;

// FUN_004183c0 (cdecl, varargs): TString<64>::format -- vsprintf into a 0x40-byte stack buffer,
// then the TString<64> assignment (strlen clamped to 0x3f, copy, terminator, length).
void FontIdFormat(TString<64>* Dst, char const* Fmt, ...)
{
    char Buffer[0x40];
    va_list Args;
    va_start(Args, Fmt);
#pragma warning(push)
#pragma warning(disable : 4996) // vsprintf is deprecated; retail calls it here.
    vsprintf(Buffer, Fmt, Args);
#pragma warning(pop)
    va_end(Args);
    *Dst = Buffer;
}
} // namespace

FontImp::FontImp()
    : FixedHeight(0.0f)
    , InitLineHeight(0)
    , Dc(nullptr)
    , Font(nullptr)
    , RealizedHeight(0)
    , LayoutGeneration(0)
{
    // FUN_00417d50 (IFont vtable 005c8e1c, FontImp vtable 005c8dec).
    Name.Length = 0;
    Name.Data[0] = '\0';
    Id.Length = 0;
    Id.Data[0] = '\0';
    Face.Length = 0;
    Face.Data[0] = '\0';
}

FontImp::~FontImp()
{
    // FUN_00416490: the handles are only forgotten -- no DeleteObject / DeleteDC.
    if (Font != nullptr)
    {
        Font = nullptr;
    }
    if (Dc != nullptr)
    {
        Dc = nullptr;
    }
    LayoutGeneration = 0;
}

void FontImp::SetName(char const* Nickname)
{
    // FUN_00417ca0
    if (Id.Length == 0)
    {
        Id = Nickname;
    }
    Name = Nickname;
}

bool FontImp::Realize(int Height)
{
    // FUN_00415ea0
    if (Font != nullptr)
    {
        if (RealizedHeight == Height || Height == Metrics.tmHeight)
        {
            return true;
        }
        DeleteObject(Font);
        Font = nullptr;
    }
    LogFont.lfHeight = Height;
    Font = CreateFontIndirectA(&LogFont);
    if (Font == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: failed to create font %s height=%d\n", kFontCpp, 0x9ae,
              Name.Data, Height);
        return false;
    }
    RealizedHeight = Height;
    SelectObject(Dc, Font);
    GetTextMetricsA(Dc, &Metrics);
    LayoutGeneration = G_FontLayoutGeneration;
    return true;
}

double FontImp::MeasurePangramRatio(int Height, int Width)
{
    // FUN_00415e40. The measured width is read unsigned.
    Realize(Height);
    int Extent[2];
    MeasureString(Extent, kPangram, -1);
    return static_cast<double>(static_cast<unsigned>(Extent[0])) / static_cast<double>(Width);
}

int FontImp::SearchScaleDelta(float Fixed, int ScreenW, int ScreenH, float Target)
{
    // FUN_00416f50
    int const StartHeight = static_cast<int>(
        std::floor(static_cast<double>(ScreenH) * static_cast<double>(Fixed) + 0.5));
    int Delta = 0;
    Realize(StartHeight);
    int Extent[2];
    MeasureString(Extent, kPangram, -1);
    // The width becomes a float once, every ratio divides by it.
    float const WidthF = static_cast<float>(ScreenW);
    double Ratio = static_cast<double>(static_cast<unsigned>(Extent[0])) / WidthF;
    // NaN takes 1.
    int Step = -1;
    if (!(Ratio > Target))
    {
        Step = 1;
    }
    // DAT_005c75f8 (double 0.001).
    if (std::fabs(Ratio - Target) > 0.001)
    {
        for (;;)
        {
            // Compared unsigned.
            if (static_cast<unsigned>(GetLineHeight()) > static_cast<unsigned>(StartHeight + Delta))
            {
                break;
            }
            Realize(StartHeight + Step + Delta);
            MeasureString(Extent, kPangram, -1);
            Ratio = static_cast<double>(static_cast<unsigned>(Extent[0])) / WidthF;
            if (Step > 0)
            {
                // Growing -- stop before taking a step that overshoots.
                if (Ratio > Target)
                {
                    break;
                }
                Delta += Step;
            }
            else
            {
                // Shrinking -- take the step, then stop at or below the target or height 4.
                Delta += Step;
                if (Ratio <= Target)
                {
                    break;
                }
                if (StartHeight + Delta <= 4)
                {
                    break;
                }
            }
            if (!(std::fabs(Ratio - Target) > 0.001))
            {
                break;
            }
        }
    }
    return Delta;
}

namespace
{
    // DAT_00610850 as both scale-delta searches read it. Port-only under
    // FREELANCER_WIDESCREEN_SUPPORT: the width a 4:3 surface of this height would have, so the
    // pangram is fitted to the same share of the (Hor+, height-scaled) interface as on 4:3 --
    // the full width would grow every font by the Hor+ factor. It is also the cache key, which
    // then tells 1280x720 from 1280x960.
    int FontSurfaceWidth()
    {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        return static_cast<int>(std::floor(WidescreenEquivalentSurfaceWidth() + 0.5));
#else
        return static_cast<int>(G_RenderSurfaceWidth);
#endif
    }
}

int FontImp::ComputeScaleDelta()
{
    // FUN_00416110
    if (FixedHeight == 0.0f)
    {
        return 0;
    }
    // Cached per surface width DAT_00610850.
    int const SurfaceWidth = FontSurfaceWidth();
    std::map<int, int>::iterator const Hit = ScaleDeltaByWidth.find(SurfaceWidth);
    if (Hit != ScaleDeltaByWidth.end())
    {
        return Hit->second;
    }
    bool const bHadFont = Font != nullptr;
    int const OldHeight = RealizedHeight;
    // DAT_005c8da4 = 600.
    Realize(static_cast<int>(std::floor(static_cast<double>(FixedHeight) * 600.0 + 0.5)));
    int Extent[2];
    MeasureString(Extent, kPangram, -1);
    float const Target = static_cast<float>(static_cast<double>(static_cast<unsigned>(Extent[0])) *
                                            static_cast<double>(kPangramWidthScale));
    // FUN_00416f50's body inlined.
    int const Delta = SearchScaleDelta(FixedHeight, SurfaceWidth,
                                       static_cast<int>(G_RenderSurfaceHeight), Target);
    ScaleDeltaByWidth[SurfaceWidth] = Delta;
    if (bHadFont)
    {
        Realize(OldHeight);
    }
    return Delta;
}

int FontImp::ComputeScaleDeltaByRatio()
{
    // The prelude body of LAB_00416dc0, LAB_004170e0 and LAB_004172b0.
    if (FixedHeight == 0.0f)
    {
        return 0;
    }
    int const SurfaceWidth = FontSurfaceWidth();
    std::map<int, int>::iterator const Hit = ScaleDeltaByWidth.find(SurfaceWidth);
    if (Hit != ScaleDeltaByWidth.end())
    {
        return Hit->second;
    }
    bool const bHadFont = Font != nullptr;
    int const OldHeight = RealizedHeight;
    float const Target = static_cast<float>(MeasurePangramRatio(
        static_cast<int>(std::floor(static_cast<double>(FixedHeight) * 600.0 + 0.5)), 0x320));
    int const Delta = SearchScaleDelta(FixedHeight, SurfaceWidth,
                                       static_cast<int>(G_RenderSurfaceHeight), Target);
    ScaleDeltaByWidth[SurfaceWidth] = Delta;
    if (bHadFont)
    {
        Realize(OldHeight);
    }
    return Delta;
}

void FontImp::RefreshScale(bool bRatioTarget)
{
    // The prelude of every face slot (e.g. LAB_00416570).
    if (LayoutGeneration == G_FontLayoutGeneration)
    {
        return;
    }
    LayoutGeneration = G_FontLayoutGeneration;
    float const Base = static_cast<float>(static_cast<double>(GetFixedHeight()) *
                                          static_cast<double>(static_cast<int>(G_RenderSurfaceHeight)));
    int const Delta = bRatioTarget ? ComputeScaleDeltaByRatio() : ComputeScaleDelta();
    Realize(static_cast<int>(
        std::floor(static_cast<double>(Delta) + static_cast<double>(Base) + 0.5)));
}

bool FontImp::Init(FFontDesc const& Desc)
{
    // FUN_00415a80
    // The previous handles are only forgotten.
    if (Font != nullptr)
    {
        Font = nullptr;
    }
    if (Dc != nullptr)
    {
        Dc = nullptr;
    }
    LayoutGeneration = 0;
    // Retail bug kept: the format has %d but no argument is pushed, so it reads the next stack
    // slot (the caller's saved EDI).
    FontIdFormat(&Id, "%s,%d", Desc.Face);
    Face = Desc.Face;
    memset(&LogFont, 0, sizeof(LogFont));
    LogFont.lfWidth = 0;
    LogFont.lfEscapement = 0;
    LogFont.lfOrientation = 0;
    LogFont.lfWeight = Desc.bBold ? 700 : 400;
    LogFont.lfCharSet = 1;                     // DEFAULT_CHARSET
    LogFont.lfOutPrecision = 4;                // OUT_TT_PRECIS
    LogFont.lfClipPrecision = 0;
    LogFont.lfQuality = 4;                     // ANTIALIASED_QUALITY
    LogFont.lfPitchAndFamily = 0;
    strncpy(LogFont.lfFaceName, Face.Data, 0x1f);
    LogFont.lfFaceName[0x1f] = '\0';
    LogFont.lfItalic = Desc.bItalic ? 1 : 0;
    LogFont.lfUnderline = Desc.bUnderline ? 1 : 0;
    LogFont.lfStrikeOut = 0;
    if (Dc == nullptr)
    {
        Dc = CreateCompatibleDC(nullptr);
    }
    FixedHeight = Desc.FixedHeight;
    // The autoscale prelude inlined (the generation is 0 here and DAT_006105b0 never is, so it
    // always runs).
    RefreshScale(false);
    InitLineHeight = Metrics.tmHeight;
    return true;
}

char const* FontImp::Slot00()
{
    // LAB_00416550
    return Id.Data;
}

char const* FontImp::GetName()
{
    // LAB_00416560
    return Name.Data;
}

float FontImp::GetFixedHeight()
{
    // LAB_00416ad0
    return FixedHeight;
}

HFONT__* FontImp::Slot03()
{
    // LAB_00416570
    RefreshScale(false);
    return Font;
}

LOGFONTA const* FontImp::GetLogFont()
{
    // LAB_00416820
    RefreshScale(false);
    return &LogFont;
}

float FontImp::GetGlyphAdvance(wchar_t Ch)
{
    // LAB_00416dc0
    RefreshScale(true);
    // GetCharWidthW (FUN_005b7445) result not checked.
    HDC const SelectedDc = Dc;
    HGDIOBJ const Previous = SelectObject(SelectedDc, Font);
    UINT const Glyph = static_cast<unsigned short>(Ch);
    INT Width = 0;
    GetCharWidthW(Dc, Glyph, Glyph, &Width);
    float const Advance = static_cast<float>(Width);
    SelectObject(SelectedDc, Previous);
    return Advance;
}

int FontImp::GetLineHeight()
{
    // LAB_00416af0
    RefreshScale(false);
    if (Font != nullptr)
    {
        return Metrics.tmHeight;
    }
    return Slot07();
}

int FontImp::Slot07()
{
    // LAB_00416ae0
    return InitLineHeight;
}

void FontImp::MeasureString(int* OutWH, wchar_t const* Text, int Len)
{
    // LAB_00415f60 (vtable slot 8). OutWH is the SIZE the caller passes by pointer; retail
    // writes both halves on every path.
    RefreshScale(false);

    int Width = 0;
    int Height = 0;
    // A null string or a zero length measures as 0 x 0 without touching the DC.
    if (Text != nullptr && Len != 0)
    {
        HDC const SelectedDc = Dc;
        HGDIOBJ const Previous = SelectObject(SelectedDc, Font);
        if (Len == -1)
        {
            Len = static_cast<int>(wcslen(Text));
        }

        // The one-time OS probe.
        if (!G_bFontOsVersionProbed)
        {
            G_bFontOsVersionProbed = true;
            OSVERSIONINFOA Version;
            Version.dwOSVersionInfoSize = 0x94;
#pragma warning(push)
#pragma warning(disable : 4996) // GetVersionExA is deprecated; retail calls it here.
            BOOL const bVersion = GetVersionExA(&Version);
#pragma warning(pop)
            if (bVersion)
            {
                G_bFontWholeStringExtent = Version.dwMajorVersion > 4;
            }
            else
            {
                G_bFontWholeStringExtent = false;
            }
        }

        SIZE Extent;
        BOOL bOk = FALSE;
        if (G_bFontWholeStringExtent)
        {
            // FUN_005b746a, the delay-load stub of gdi32!GetTextExtentPointW.
            bOk = GetTextExtentPointW(SelectedDc, Text, Len, &Extent);
        }
        else
        {
            // Per-glyph sum through FUN_005b7445 (gdi32!GetCharWidthW). The count is compared
            // unsigned, and the first failing glyph zeroes the result.
            Extent.cx = 0;
            Extent.cy = Metrics.tmHeight;
            unsigned const Count = static_cast<unsigned>(Len);
            for (unsigned Index = 0; Index < Count; ++Index)
            {
                UINT const Glyph = static_cast<unsigned short>(Text[Index]);
                INT GlyphWidth = 0;
                bOk = GetCharWidthW(SelectedDc, Glyph, Glyph, &GlyphWidth);
                if (!bOk)
                {
                    break;
                }
                Extent.cx += GlyphWidth;
            }
        }

        // Any failure (or an empty loop) reports 0 x 0.
        if (bOk)
        {
            Width = Extent.cx;
            Height = Extent.cy;
        }
        SelectObject(SelectedDc, Previous);
    }
    OutWH[0] = Width;
    OutWH[1] = Height;
}

int FontImp::ProbeString(int MaxWidth, wchar_t const* Text, int Len)
{
    // LAB_004172b0 (vtable slot 9, RET 0xc).
    RefreshScale(true);
    int Count = 0;
    unsigned Sum = 0;
    unsigned Index = 0;
    if (Len == -1)
    {
        Len = static_cast<int>(wcslen(Text));
    }
    HDC const SelectedDc = Dc;
    HGDIOBJ const Previous = SelectObject(SelectedDc, Font);
    // Stop once the sum reaches MaxWidth or the next character would pass it; both compares
    // are unsigned.
    unsigned const Limit = static_cast<unsigned>(MaxWidth);
    if (static_cast<unsigned>(Len) > 0u)
    {
        do
        {
            if (Sum >= Limit)
            {
                break;
            }
            UINT const Glyph = static_cast<unsigned short>(Text[Index]);
            INT Width = 0;
            GetCharWidthW(Dc, Glyph, Glyph, &Width);
            unsigned const Next = static_cast<unsigned>(Width) + Sum;
            if (Next > Limit)
            {
                break;
            }
            ++Count;
            Sum = Next;
            ++Index;
        } while (Index < static_cast<unsigned>(Len));
    }
    SelectObject(SelectedDc, Previous);
    return Count;
}

int FontImp::GetCharWidths(wchar_t const* Text, int Len, int* OutWidths)
{
    // LAB_004170e0 (vtable slot 10, RET 0xc).
    RefreshScale(true);

    HDC const SelectedDc = Dc;
    HGDIOBJ const Previous = SelectObject(SelectedDc, Font);
    if (Len == -1)
    {
        Len = static_cast<int>(wcslen(Text));
    }
    // Unsigned index loop; GetCharWidthW's result is not checked.
    int Sum = 0;
    for (unsigned Index = 0; Index < static_cast<unsigned>(Len); ++Index)
    {
        UINT const Glyph = static_cast<unsigned short>(Text[Index]);
        INT Width = 0;
        GetCharWidthW(Dc, Glyph, Glyph, &Width);
        Sum += Width;
        OutWidths[Index] = Width;
    }
    SelectObject(SelectedDc, Previous);
    return Sum;
}

int LoadFonts(char const* Path, IFileSystem* Filesys)
{
    // FUN_00417800 (from FUN_005b0c30 with DALib::Data).
    char FullPath[MAX_PATH];
    int Count = 0;
    GetFileSysPath(FullPath, Path, Filesys);
    INI_Reader Reader;
    if (Reader.open(FullPath, false) && Reader.read_header())
    {
        do
        {
            if (!Reader.is_header("TrueType"))
            {
                continue;
            }
            TString<0x80> Nickname;
            Nickname.Length = 0;
            Nickname.Data[0] = '\0';
            TString<0x80> FontName;
            FontName.Length = 0;
            FontName.Data[0] = '\0';
            float FixedHeight = 0.0f;
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    char const* const Value = Reader.get_value_string();
                    if (Value == nullptr)
                    {
                        Nickname.Length = 0;
                        Nickname.Data[0] = '\0';
                    }
                    else
                    {
                        Nickname.Length = CopyBoundedString(Nickname.Data, 0x80, Value);
                    }
                }
                else if (Reader.is_value("font"))
                {
                    char const* const Value = Reader.get_value_string(0);
                    if (Value == nullptr)
                    {
                        FontName.Length = 0;
                        FontName.Data[0] = '\0';
                    }
                    else
                    {
                        FontName.Length = CopyBoundedString(FontName.Data, 0x80, Value);
                    }
                }
                else if (Reader.is_value("fixed_height"))
                {
                    FixedHeight = Reader.get_value_float(0);
                }
            }
            // A default FontImp copied onto the list's end (FUN_00418bc0 / FUN_00418cd0).
            {
                FontImp Temp;
                G_FontList.insert(G_FontList.end(), Temp);
            }
            FontImp* const Imp = &G_FontList.back();
            Imp->SetName(Nickname.Data);
            // Retail's own test of the node pointer (never null).
            if (Imp != nullptr)
            {
                FFontDesc Desc;
                Desc.Face = FontName.Data;
                Desc.FixedHeight = FixedHeight;
                Desc.bBold = false;
                Desc.bItalic = false;
                Desc.bUnderline = false;
                if (Imp->Init(Desc))
                {
                    ++Count;
                }
                else
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: failed to create FONT font %s", kFontCpp,
                          0xae4, Nickname.Data);
                }
            }
        } while (Reader.read_header());
    }
    return Count;
}

IFont* FindMenuFontImp(char const* Nickname)
{
    // FUN_004177b0: first list entry whose GetName() (vtable +0x04) matches, DACOM stricmp.
    for (std::list<FontImp>::iterator It = G_FontList.begin(); It != G_FontList.end(); ++It)
    {
        if (_stricmp(It->GetName(), Nickname) == 0)
        {
            return &*It;
        }
    }
    return nullptr;
}

void FontShutdown()
{
    // FUN_004174a0
    FontAtlasReleaseScratch();
    std::list<FontImp>::iterator It = G_FontList.begin();
    while (It != G_FontList.end())
    {
        It = G_FontList.erase(It);
    }
    FontAtlasReleasePagePool();
    // FUN_00415050 inlined.
    NumberFaceResetAll();
    // Tail jump to FUN_00402d50.
    ScreenQuadResetDrawBatchCache();
}
