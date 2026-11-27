// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TextRenderContext, DeviceTRC (fonts, images, GDI output) and the rich-text font tables
// (retail RichDisplayList.cpp).

#include "common_text.h"
#include "Text/common_text_internal.h"
#include "common_misc.h" // safe_strcpy (SetStandardFont)
#include "dacom.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <map>
#include <new>
#include <string>
#include <deque>
#include <list>
#include <vector>
#include <windows.h>

// ============================================================================
// TextRenderContext -- base class, virtual device interface.
// ============================================================================

TextRenderContext::TextRenderContext()
    : CurrentAttrs()
    , Unused20(0)
    , DefaultAttrs()
    , Unused28(0)
{
    // .text:06327380. Retail never writes +0x20 or +0x28; the port zeroes them.
    DefaultAttrs.Packed = GDefaultTextAttribsPacked;
    CurrentAttrs.Packed = GDefaultTextAttribsPacked;
    PosH = 0;
    PosV = 0;
    OriginH = 0;
    OriginV = 0;
}

TextRenderContext::TextRenderContext(const TextRenderContext& Other)
    : StateStack(Other.StateStack)
    , AttrStack(Other.AttrStack)
    , CurrentAttrs(Other.CurrentAttrs)
    , Unused20(0)
    , DefaultAttrs(Other.DefaultAttrs)
    , Unused28(0)
{
    // .text:06324e90 (list range inserts FUN_0632c3a0 / FUN_0632c420). +0x20 and +0x28 are not
    // copied (zeroed here).
    PosV = Other.PosV;
    PosH = Other.PosH;
    OriginV = Other.OriginV;
    OriginH = Other.OriginH;
}

TextRenderContext& TextRenderContext::operator=(const TextRenderContext& Other)
{
    // .text:06324f60. VC6 list::operator= on each list, same result as std::list's. +0x20 /
    // +0x28 are left alone. No self-assignment test beyond the lists'.
    StateStack = Other.StateStack;
    AttrStack = Other.AttrStack;
    CurrentAttrs = Other.CurrentAttrs;
    DefaultAttrs = Other.DefaultAttrs;
    PosV = Other.PosV;
    PosH = Other.PosH;
    OriginV = Other.OriginV;
    OriginH = Other.OriginH;
    return *this;
}

TextRenderContext::~TextRenderContext()
{
    // .text:06327c90 (ICF-shared with ~DeviceTRC): the two list destructors, AttrStack first.
}

const TextRenderAttributes& TextRenderContext::get_current_attributes()
{
    // .text:06327470 (ICF-shared with CSteering::GetAxisThrottle)
    return CurrentAttrs;
}

const TextRenderAttributes& TextRenderContext::get_default_attributes()
{
    // .text:06327480: the most recently pushed attributes, else DefaultAttrs (+0x24).
    if (AttrStack.size() == 0)
    {
        return DefaultAttrs;
    }
    return AttrStack.front();
}

void TextRenderContext::set_current_attributes(const TextRenderAttributes& Attrs)
{
    // RVA 0xc74a0
    CurrentAttrs = Attrs;
}

void TextRenderContext::set_default_attributes(const TextRenderAttributes& Attrs)
{
    // RVA 0xc74b0
    DefaultAttrs = Attrs;
}

void TextRenderContext::push_default_attributes()
{
    // .text:063274c0. Despite the name it pushes the CURRENT attributes;
    // get_default_attributes returns them.
    AttrStack.push_front(CurrentAttrs);
}

void TextRenderContext::pop_default_attributes()
{
    // .text:06327510. No empty check in retail: an empty stack frees the head node itself (UB,
    // not reproduced); the port leaves an empty stack alone.
    if (AttrStack.empty())
    {
        return;
    }
    AttrStack.pop_front();
}

void TextRenderContext::clear_default_attributes()
{
    // .text:06327540
    AttrStack.clear();
}

void TextRenderContext::set_pos(int H, int V)
{
    // RVA 0xC4E50
    PosH = H;
    PosV = V;
}

void TextRenderContext::set_origin(int H, int V)
{
    // RVA 0xC4E70
    OriginH = H;
    OriginV = V;
}

int TextRenderContext::get_pos_h()
{
    // RVA 0xC75A0
    return PosH;
}

int TextRenderContext::get_pos_v()
{
    // RVA 0xC7580
    return PosV;
}

int TextRenderContext::get_origin_h()
{
    // RVA 0xC75E0
    return OriginH;
}

int TextRenderContext::get_origin_v()
{
    // RVA 0xC75C0
    return OriginV;
}

void TextRenderContext::set_pos_h(int H)
{
    // RVA 0xC75B0
    PosH = H;
}

void TextRenderContext::set_pos_v(int V)
{
    // RVA 0xC7590
    PosV = V;
}

void TextRenderContext::set_origin_h(int H)
{
    // RVA 0xC75F0
    OriginH = H;
}

void TextRenderContext::set_origin_v(int V)
{
    // RVA 0xC75D0
    OriginV = V;
}

void TextRenderContext::push_state()
{
    // .text:06327690
    State Saved;
    Saved.Attributes = CurrentAttrs.Packed;
    Saved.Unused04 = 0; // retail leaves this stack dword unwritten; nothing reads it
    Saved.PosV = PosV;
    Saved.PosH = PosH;
    get_clip_rect(Saved.ClipRect);
    StateStack.push_front(Saved);
}

void TextRenderContext::pop_state()
{
    // .text:06327700. No empty check in retail: an empty stack restores from the head node's
    // uninitialised payload and frees the head (UB, not reproduced); the port leaves an empty
    // stack alone.
    if (StateStack.empty())
    {
        return;
    }
    const State& Top = StateStack.front();
    CurrentAttrs.Packed = Top.Attributes;
    PosV = Top.PosV;
    PosH = Top.PosH;
    set_clip_rect(Top.ClipRect);
    StateStack.pop_front();
}

void TextRenderContext::reset_to_default()
{
    // .text:06327600. The "size %d" OutputDebugStringA is retail's own leak diagnostic
    // (FUN_06306fe0 formats). The non-empty branch after the clear is dead but kept as retail
    // has it. PosH / PosV are not touched.
    if (AttrStack.size() != 0)
    {
        char Buffer[0x2000];
        _snprintf(Buffer, 0x2000, "size %d\n", (int)AttrStack.size());
        OutputDebugStringA(Buffer);
    }
    AttrStack.clear();
    if (AttrStack.size() == 0)
    {
        CurrentAttrs = DefaultAttrs;
    }
    else
    {
        CurrentAttrs = AttrStack.front();
    }
}

void TextRenderContext::get_state(State& OutState)
{
    // .text:06327410
    OutState.Attributes = CurrentAttrs.Packed;
    OutState.PosH = PosH;
    OutState.PosV = PosV;
    get_clip_rect(OutState.ClipRect);
}

void TextRenderContext::set_state(const State& InState)
{
    // .text:06327440
    CurrentAttrs.Packed = InState.Attributes;
    PosV = InState.PosV;
    PosH = InState.PosH;
    set_clip_rect(InState.ClipRect);
}

// TextRenderContext's eleven device virtuals are pure (_purecall in .rdata:063a3de8).

// ============================================================================
// DeviceTRC -- GDI-based text rendering context.
// ============================================================================

// .data:063ed670 / .data:063ed674, both statically initialised to 2.
int DeviceTRC::mDropShadowX = 2;
int DeviceTRC::mDropShadowY = 2;

DeviceTRC::DeviceTRC()
{
    // .text:06327b40. MHdc +0x3C, ClipRect +0x40.
    MHdc = nullptr;
    ClipRect.left = 0;
    ClipRect.top = 0;
    ClipRect.right = 0;
    ClipRect.bottom = 0;
}

DeviceTRC::DeviceTRC(HDC InDC)
{
    // .text:06327bf0. Retail leaves ClipRect unwritten; the port zeroes it.
    MHdc = InDC;
    ClipRect.left = 0;
    ClipRect.top = 0;
    ClipRect.right = 0;
    ClipRect.bottom = 0;
}

DeviceTRC::DeviceTRC(const DeviceTRC& Other)
    : TextRenderContext(Other)
{
    // .text:063250f0
    MHdc = Other.MHdc;
    ClipRect = Other.ClipRect;
}

DeviceTRC::~DeviceTRC()
{
    // RVA 0xC7BA0
}

DeviceTRC& DeviceTRC::operator=(const DeviceTRC& Other)
{
    // .text:06325200
    TextRenderContext::operator=(Other);
    MHdc = Other.MHdc;
    ClipRect = Other.ClipRect;
    return *this;
}

// The font table, retail DeviceTRC::mFont at .data:06401968: 32 styles x 8 bold/italic/
// underline combinations of 0x7C-byte WinFont entries. A font index is (Style << 3) |
// Combination, one byte of TextRenderAttributes::Packed.
static const int GMaxFontIndices = 32;
static const int GSubEntriesPerFont = 8;
static_assert(sizeof(DeviceTRC::WinFont) == 0x7C, "retail WinFont stride is 0x7C");
DeviceTRC::WinFont DeviceTRC::mFont[256];

// The 32 standard styles, retail DAT_06409588. Name points at the block's own NameBuffer.
struct FStandardFontStyle
{
    char const* Name;           // +0x00
    unsigned long Size;         // +0x04
    unsigned char bBold;        // +0x08
    unsigned char bItalic;      // +0x09
    unsigned char bUnderline;   // +0x0A
    unsigned char Pad0B;        // +0x0B
    char NameBuffer[0x80];      // +0x0C
};
static_assert(sizeof(FStandardFontStyle) == 0x8C, "retail style stride is 0x8C");
static FStandardFontStyle GStandardFonts[GMaxFontIndices];

// RVA 0xC5F20 (LAB_06325f20), a static initializer (table DAT_063ea574) that gives all 32
// styles retail's built-in plain defaults in a repeating 4-style cycle starting at pattern 2.
static struct FDefaultStandardFontsInit
{
    FDefaultStandardFontsInit()
    {
        static const char* const Names[4] = {
            "Vibrocentric", "Vibrocentric", "Arial Unicode MS", "Arial Unicode MS"
        };
        static const unsigned long Sizes[4] = { 16, 18, 22, 26 };
        for (int i = 0; i < GMaxFontIndices; ++i)
        {
            const int Pattern = (i + 2) % 4;
            FStandardFontStyle& Style = GStandardFonts[i];
            // FUN_06326250 (inlined for the first three).
            Style.Name = Style.NameBuffer;
            safe_strcpy(Style.NameBuffer, 0x80, Names[Pattern]);
            Style.Size = Sizes[Pattern];
            Style.bBold = 0;
            Style.bItalic = 0;
            Style.bUnderline = 0;
        }
    }
} GDefaultStandardFontsInit;

// DAT_063ed608: eight three-dword rows whose third dword packs {bold, italic, underline} as
// bytes, indexed bold | italic<<1 | underline<<2.
struct FRichFontCombination
{
    unsigned char bBold;
    unsigned char bItalic;
    unsigned char bUnderline;
};
static const FRichFontCombination GFontCombinations[GSubEntriesPerFont] = {
    {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1},
};

// DAT_063ed668 (font scale) / DAT_063ed66c (image scale), DAT_0640a708 / DAT_0640a709.
static float GRichTextFontScale = 1.0f;
namespace CmnText
{
    float GRichTextImageScale = 1.0f; // declared in common_text_internal.h (ImageNode reads it)
}
static bool GRichTextInitialised = false;
static bool GRichTextOsAboveNt4 = false;

// LAB_063277b0 -- mFont's element constructor. +0x3C and the TEXTMETRICA are not touched.
DeviceTRC::WinFont::WinFont()
{
    memset(&LogFont, 0, sizeof(LogFont));
    FontHandle = nullptr;
}

// FUN_063277d0 -- mFont's element destructor.
DeviceTRC::WinFont::~WinFont()
{
    if (FontHandle != nullptr)
    {
        DeleteObject(FontHandle);
        FontHandle = nullptr;
    }
}

// FUN_063277f0 (thiscall on the WinFont) -- FontDesc to LOGFONTA. lfHeight is the UNSCALED
// size (+0x3C); the caller rescales it. No null guard on Desc.Name.
void DeviceTRC::WinFont::BuildRichTextLogFont(TextRenderContext::FontDesc const& Desc)
{
    UnscaledSize = (int)Desc.Size;
    memset(&LogFont, 0, sizeof(LogFont));
    LogFont.lfHeight = UnscaledSize;
    LogFont.lfWidth = 0;
    LogFont.lfEscapement = 0;
    LogFont.lfOrientation = 0;
    LogFont.lfCharSet = 1;
    LogFont.lfOutPrecision = 4;
    LogFont.lfClipPrecision = 0;
    LogFont.lfQuality = 4;
    LogFont.lfPitchAndFamily = 0;
    LogFont.lfWeight = Desc.bBold != 0 ? 700 : 400;
    strncpy(LogFont.lfFaceName, Desc.Name, 0x1f);
    LogFont.lfFaceName[0x1f] = '\0';
    LogFont.lfItalic = Desc.bItalic != 0 ? 1 : 0;
    LogFont.lfStrikeOut = 0;
    LogFont.lfUnderline = Desc.bUnderline != 0 ? 1 : 0;
}

// The delete sweep over all 256 entries that InitializeRichText and ShutdownRichText inline.
static void ReleaseAllRichTextFonts(DeviceTRC::WinFont* Fonts)
{
    for (int Index = 0; Index < GMaxFontIndices * GSubEntriesPerFont; ++Index)
    {
        DeviceTRC::WinFont& Entry = Fonts[Index];
        if (Entry.FontHandle != nullptr)
        {
            DeleteObject(Entry.FontHandle);
            Entry.FontHandle = nullptr;
        }
    }
}

// One style's eight bold/italic/underline fonts. Retail inlines this loop in both
// DeviceTRC::static_set_font and ReinitializeRichTextFonts; only each combination row's flag
// bytes survive (name and size come from Base). The scratch DC is not null-checked.
static void RebuildRichTextStyleFonts(DeviceTRC::WinFont* StyleFonts,
                                      TextRenderContext::FontDesc const& Base, float Scale)
{
    for (int Sub = 0; Sub < GSubEntriesPerFont; ++Sub)
    {
        TextRenderContext::FontDesc Desc = {};
        Desc.Name = Base.Name;
        Desc.Size = Base.Size;
        Desc.bBold = GFontCombinations[Sub].bBold;
        Desc.bItalic = GFontCombinations[Sub].bItalic;
        Desc.bUnderline = GFontCombinations[Sub].bUnderline;

        DeviceTRC::WinFont& Entry = StyleFonts[Sub];
        Entry.BuildRichTextLogFont(Desc);

        if (Entry.FontHandle != nullptr)
        {
            DeleteObject(Entry.FontHandle);
            Entry.FontHandle = nullptr;
        }

        Entry.LogFont.lfHeight = (LONG)((double)Entry.UnscaledSize * (double)Scale);

        Entry.FontHandle = CreateFontIndirectA(&Entry.LogFont);
        if (Entry.FontHandle != nullptr)
        {
            HDC TempDC = CreateCompatibleDC(nullptr);
            HGDIOBJ OldObj = SelectObject(TempDC, Entry.FontHandle);
            GetTextMetricsA(TempDC, &Entry.Metrics);
            SelectObject(TempDC, OldObj);
            DeleteDC(TempDC);
        }
        if (Entry.FontHandle == nullptr)
        {
            FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s",
                  "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0x48e,
                  "Failed to create font.\n");
        }
    }
}

void __cdecl SetStandardFont(int StyleIndex, TextRenderContext::FontDesc const& Desc)
{
    // .text:06326570. No bounds check: an index outside 0..31 writes past the table in retail
    // as well.
    FStandardFontStyle& Style = GStandardFonts[StyleIndex];
    safe_strcpy(Style.NameBuffer, 0x80, Desc.Name);
    Style.Name = Style.NameBuffer;
    Style.bBold = Desc.bBold;
    Style.bItalic = Desc.bItalic;
    Style.bUnderline = Desc.bUnderline;
    Style.Size = Desc.Size;
}

bool __cdecl InitializeRichText(int ScreenHeight)
{
    // .text:06326340.
    if (GRichTextInitialised)
    {
        ReleaseAllRichTextFonts(DeviceTRC::mFont);
        GRichTextInitialised = false;
    }

    // 768.0f DAT_063a3db8, 600.0f DAT_063a3dbc. The exe passes its surface height
    // (DAT_00610854, at 005b1f64).
    GRichTextFontScale = (float)((double)ScreenHeight / 768.0);
    GRichTextImageScale = (float)((double)ScreenHeight / 600.0);

    // Doubled from the stored (float-rounded) scale.
    DeviceTRC::mDropShadowX = (int)((double)GRichTextFontScale + (double)GRichTextFontScale);
    DeviceTRC::mDropShadowY = DeviceTRC::mDropShadowX;

    // Fonts are not created here (see ReinitializeRichTextFonts). GetVersionExA is
    // version-shimmed on modern Windows; any answer above NT4 is retail's answer there too.
    OSVERSIONINFOA Version;
    memset(&Version, 0, sizeof(Version));
    Version.dwOSVersionInfoSize = sizeof(Version);
#pragma warning(push)
#pragma warning(disable : 4996)
    if (GetVersionExA(&Version) != 0)
    {
        GRichTextOsAboveNt4 = Version.dwMajorVersion > 4;
    }
    else
    {
        GRichTextOsAboveNt4 = false;
    }
#pragma warning(pop)

    GRichTextInitialised = true;
    return true;
}

void __cdecl ReinitializeRichTextFonts()
{
    // .text:06326410. Fonts are scaled by GRichTextFontScale (DAT_063ed668).
    for (int StyleIndex = 0; StyleIndex < GMaxFontIndices; ++StyleIndex)
    {
        DeviceTRC::WinFont* const StyleFonts = &DeviceTRC::mFont[StyleIndex * GSubEntriesPerFont];
        for (int Sub = 0; Sub < GSubEntriesPerFont; ++Sub)
        {
            if (StyleFonts[Sub].FontHandle != nullptr)
            {
                DeleteObject(StyleFonts[Sub].FontHandle);
                StyleFonts[Sub].FontHandle = nullptr;
            }
        }

        FStandardFontStyle const& Style = GStandardFonts[StyleIndex];
        TextRenderContext::FontDesc Base = {};
        Base.Name = Style.Name;
        Base.Size = Style.Size;
        RebuildRichTextStyleFonts(StyleFonts, Base, GRichTextFontScale);
    }
}

void __cdecl ShutdownRichText()
{
    // .text:063265c0. The OS-version flag stays.
    ReleaseAllRichTextFonts(DeviceTRC::mFont);
    GRichTextInitialised = false;
}

void __cdecl DeviceTRC::static_set_font(int Index, const FontDesc& Desc, float Scale)
{
    // .text:06327d50. No bounds check; the standard-style table is not touched.
    RebuildRichTextStyleFonts(&mFont[Index * GSubEntriesPerFont], Desc, Scale);
}

static std::string GImageRoot;

void __cdecl DeviceTRC::set_image_root(const char* Path)
{
    // .text:06327e60, root string DAT_0640956c. No null check of Path. An empty Path leaves
    // VC6's string with a null buffer, so the last-character test reads address -1 and faults;
    // that read is reproduced through a volatile pointer.
    GImageRoot = Path;
    const uintptr_t Buffer = GImageRoot.empty() ? 0 : (uintptr_t)GImageRoot.data();
    const volatile char* const Last = (const volatile char*)(Buffer + GImageRoot.size() - 1);
    if (*Last != '\\')
    {
        GImageRoot += '\\';
    }
}

void __cdecl DeviceTRC::set_drop_shadow_offset(int X, int Y)
{
    // RVA 0xC7FB0
    mDropShadowX = X;
    mDropShadowY = Y;
}

void __cdecl DeviceTRC::release_font(int Index)
{
    // .text:06327fd0. No bounds check in retail.
    for (int Sub = 0; Sub < GSubEntriesPerFont; Sub++)
    {
        WinFont* Entry = &mFont[Index * GSubEntriesPerFont + Sub];
        if (Entry->FontHandle != nullptr)
        {
            DeleteObject(Entry->FontHandle);
            Entry->FontHandle = nullptr;
        }
    }
}

int DeviceTRC::get_cell_width() const
{
    // RVA 0xC8010 (DAT_064019c4). The attribute byte is the flat (Style<<3)|Sub index into
    // mFont; no bounds check. DAT_064019c4 is 0x18 past get_cell_height's DAT_064019ac, so this
    // is tmMaxCharWidth, not tmAveCharWidth.
    unsigned char const Combined = (unsigned char)(CurrentAttrs.Packed & 0xFF);
    return mFont[Combined].Metrics.tmMaxCharWidth;
}

int DeviceTRC::get_cell_height() const
{
    // RVA 0xC8020 (DAT_064019ac), as get_cell_width.
    unsigned char const Combined = (unsigned char)(CurrentAttrs.Packed & 0xFF);
    return mFont[Combined].Metrics.tmHeight;
}

void DeviceTRC::get_text_visual_size(const wchar_t* Text, VisualSize& Size, int Len) const
{
    // .text:06328030. A null Text writes only Ascent/Descent -- Width/Height are left as they
    // were. No test of the DC.
    if (Text == nullptr)
    {
        Size.Ascent = 0;
        Size.Descent = 0;
        return;
    }
    if (Len == -1)
    {
        Len = (int)wcslen(Text);
    }

    HDC const Dc = MHdc;
    const WinFont& Entry = mFont[(unsigned char)(CurrentAttrs.Packed & 0xFF)];
    HGDIOBJ const OldFont = SelectObject(Dc, Entry.FontHandle);
    SIZE TextSize;
    if (GetTextExtentPoint32W(MHdc, Text, Len, &TextSize))
    {
        Size.Height = TextSize.cy;
        Size.Width = TextSize.cx;
    }
    else
    {
        Size.Width = 0;
        Size.Height = 0;
    }
    Size.Ascent = Entry.Metrics.tmAscent;
    Size.Descent = Entry.Metrics.tmDescent;
    SelectObject(Dc, OldFont);
}

void DeviceTRC::get_font_metrics(int& Ascent, int& Descent) const
{
    // RVA 0xC80F0 (DAT_064019b0 / DAT_064019b4): the cached metrics of the current font, as
    // get_cell_width; MHdc is never touched.
    unsigned char const Combined = (unsigned char)(CurrentAttrs.Packed & 0xFF);
    Ascent = mFont[Combined].Metrics.tmAscent;
    Descent = mFont[Combined].Metrics.tmDescent;
}

void DeviceTRC::get_text_fit(const wchar_t* Text, int MaxWidth, int& FitCount,
                              int*, VisualSize&, int Len) const
{
    // .text:06328120. The Widths and Size parameters are never touched. No test of the DC.
    if (Text == nullptr || Len == 0)
    {
        FitCount = 0;
        return;
    }

    // The font is selected before the wcslen.
    HDC const Dc = MHdc;
    HGDIOBJ const OldFont = SelectObject(Dc, mFont[(unsigned char)(CurrentAttrs.Packed & 0xFF)].FontHandle);
    if (Len == -1)
    {
        Len = (int)wcslen(Text);
    }

    if (GRichTextOsAboveNt4)
    {
        SIZE Extent;
        if (GetTextExtentExPointW(MHdc, Text, Len, MaxWidth, &FitCount, nullptr, &Extent) == 0)
        {
            FitCount = 0;
        }
    }
    else
    {
        // NT4 and earlier: per-character GetCharWidthW. A character that would reach MaxWidth
        // exactly does not fit (>=).
        FitCount = 0;
        int TotalWidth = 0;
        int Remaining = Len;
        const wchar_t* Cur = Text;
        while (Remaining != 0)
        {
            int CharWidth = 0;
            GetCharWidthW(MHdc, *Cur, *Cur, &CharWidth);
            const int NewTotal = TotalWidth + CharWidth;
            if (NewTotal >= MaxWidth)
            {
                break;
            }
            ++FitCount;
            ++Cur;
            --Remaining;
            TotalWidth = NewTotal;
        }
    }

    SelectObject(Dc, OldFont);
}

void DeviceTRC::execute_text(const wchar_t* Text, bool bDraw, int Len)
{
    // .text:06328240. No test of the DC. The text (with its 0x0A0A0A drop shadow) is drawn only
    // when bDraw is set; PosH advances by the measured width either way.
    if (Text == nullptr)
    {
        return;
    }

    HDC const Dc = MHdc;
    HGDIOBJ const OldFont = SelectObject(Dc, mFont[(unsigned char)(CurrentAttrs.Packed & 0xFF)].FontHandle);
    if (Len == -1)
    {
        Len = (int)wcslen(Text);
    }
    if (bDraw)
    {
        SetTextAlign(MHdc, 0);
        SetBkColor(MHdc, 0);
        SetBkMode(MHdc, TRANSPARENT);
        SetTextColor(MHdc, 0x0A0A0A);
        ExtTextOutW(MHdc, PosH + OriginH + mDropShadowX, PosV + OriginV + mDropShadowY,
                    ETO_CLIPPED, nullptr, Text, Len, nullptr);
        SetTextColor(MHdc, (CurrentAttrs.Packed >> 8) & 0xFFFFFF);
        ExtTextOutW(MHdc, PosH + OriginH, PosV + OriginV, ETO_CLIPPED, nullptr, Text, Len, nullptr);
    }

    // Retail's SIZE is uninitialised stack and is added even when the call fails; the port
    // zeroes it (a failed measure advances by 0).
    SIZE TextSize = {};
    GetTextExtentPoint32W(MHdc, Text, Len, &TextSize);
    PosH += TextSize.cx;

    SelectObject(Dc, OldFont);
}

// DeviceTRC::BMPImage (RTTI .?AVBMPImage@DeviceTRC@@, vftable .rdata:063a4608, 0x18 bytes):
// the GDI bitmap behind every image DeviceTRC::create_image hands out. Scalar deleting
// destructor FUN_06327ad0.
class DeviceTRC::BMPImage : public TextRenderContext::Image
{
public:
    // Width (+0x04) / Height (+0x08) live in the Image base (ImageNode reads them there).
    HDC HdcSrc;       // +0x0C
    HBITMAP HBitmap;  // +0x10
    HGDIOBJ HOldBmp;  // +0x14

    // FUN_06327880, called only from create_image.
    explicit BMPImage(const char* Filename)
    {
        if (Filename == nullptr)
        {
            Height = 0.0f;
            Width = 0.0f;
            HdcSrc = nullptr;
            HBitmap = nullptr;
            HOldBmp = nullptr;
            return;
        }

        // A UNC or drive-qualified name is used as given; anything else is appended to the
        // image root verbatim (set_image_root keeps its trailing '\\').
        std::string Path;
        const char* LoadPath = Filename;
        const bool bUnc = Filename[0] == '\\' && Filename[1] == '\\';
        if (!bUnc && Filename[1] != ':')
        {
            Path.assign(GImageRoot, 0, std::string::npos);
            Path.append(Filename);
            LoadPath = Path.c_str();
        }

        Height = 0.0f;
        Width = 0.0f;
        HdcSrc = nullptr;
        HOldBmp = nullptr;
        HBitmap = (HBITMAP)LoadImageA(nullptr, LoadPath, IMAGE_BITMAP, 0, 0,
                                      LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        if (HBitmap == nullptr)
        {
            // Retail formats through FUN_06306fe0.
            char Buffer[0x2000];
            _snprintf(Buffer, 0x2000, "Failed to load image from \"%s\"\n", LoadPath);
            FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s",
                  "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0x43e, Buffer);
            return;
        }

        // GetObjectA's result is not checked: the DIBSECTION is zeroed first, so a failure
        // leaves Width / Height at 0.
        HDC ScreenDC = GetDC(nullptr);
        HdcSrc = CreateCompatibleDC(ScreenDC);
        if (HdcSrc != nullptr)
        {
            DIBSECTION Dib;
            memset(&Dib, 0, sizeof(Dib));
            GetObjectA(HBitmap, sizeof(Dib), &Dib);
            Width = (float)Dib.dsBm.bmWidth;
            Height = (float)Dib.dsBm.bmHeight;
            HOldBmp = SelectObject(HdcSrc, HBitmap);
        }
        ReleaseDC(nullptr, ScreenDC);
    }

    // FUN_06327af0
    virtual ~BMPImage() override
    {
        if (HdcSrc != nullptr)
        {
            SelectObject(HdcSrc, HOldBmp);
            DeleteDC(HdcSrc);
            HdcSrc = nullptr;
            HOldBmp = nullptr;
        }
        if (HBitmap != nullptr)
        {
            DeleteObject(HBitmap);
            HBitmap = nullptr;
        }
    }
};

void DeviceTRC::execute_image(const Image* Img, bool bDraw)
{
    // .text:06328370. Both axes are scaled by the one image scale (DAT_063ed66c). No null checks
    // on the image, its DC or this DC. Every image this context draws came from its own
    // create_image.
    const BMPImage* const DevImg = (const BMPImage*)Img;
    if (bDraw)
    {
        StretchBlt(MHdc, OriginH + PosH, OriginV + PosV,
                   (int)((double)GRichTextImageScale * DevImg->Width),
                   (int)((double)GRichTextImageScale * DevImg->Height),
                   DevImg->HdcSrc, 0, 0,
                   (int)DevImg->Width, (int)DevImg->Height, SRCCOPY);
    }
    // The sum is formed before truncation.
    PosH = (int)((double)GRichTextImageScale * DevImg->Width + PosH);
}

void DeviceTRC::set_clip_rect(const RECT& NewClip)
{
    // .text:063283f0. The rectangle is cached, DC or not.
    if (MHdc != nullptr)
    {
        if (NewClip.bottom == NewClip.top || NewClip.left == NewClip.right)
        {
            SelectClipRgn(MHdc, nullptr);
        }
        else
        {
            RECT OffsetRc = NewClip;
            OffsetRect(&OffsetRc, OriginH, OriginV);
            HRGN const Hrgn = CreateRectRgnIndirect(&OffsetRc);
            if (Hrgn != nullptr)
            {
                if (SelectClipRgn(MHdc, Hrgn) == 0)
                {
                    FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s",
                          "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0x592,
                          "Failed to select clipping region into DC\n");
                }
                DeleteObject(Hrgn);
            }
        }
    }
    ClipRect = NewClip;
}

void DeviceTRC::get_clip_rect(RECT& OutRect)
{
    // .text:063284d0. An error (-1) leaves the cache as it was.
    if (MHdc != nullptr)
    {
        HRGN const Hrgn = CreateRectRgn(0, 0, 10, 10);
        const int Result = GetClipRgn(MHdc, Hrgn);
        if (Result == 0)
        {
            ClipRect.left = 0;
            ClipRect.top = 0;
            ClipRect.right = 0;
            ClipRect.bottom = 0;
        }
        else if (Result != -1)
        {
            RECT RgnBox;
            GetRgnBox(Hrgn, &RgnBox);
            OffsetRect(&RgnBox, -OriginH, -OriginV);
            ClipRect = RgnBox;
        }
        DeleteObject(Hrgn);
    }
    OutRect = ClipRect;
}

TextRenderContext::Image* DeviceTRC::create_image(const char* const Filename)
{
    // .text:06328590. The object is returned whether or not the bitmap loaded.
    return new (std::nothrow) BMPImage(Filename);
}

void DeviceTRC::destroy_image(Image* Img)
{
    // .text:063285f0
    if (Img != nullptr)
    {
        delete Img;
    }
}

void DeviceTRC::bind_to_dc(HDC InDC)
{
    // RVA 0x466b0
    MHdc = InDC;
}
