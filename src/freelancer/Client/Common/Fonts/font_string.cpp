// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Fonts/font_string.h"
#include "msvcrt_numeric_format.h"

#include "Common/Fonts/font_atlas.h"
#include "Common/Fonts/font_imp.h"
#include "Core/game_options.h"
#include "Core/globals.h"
#include "Common/Graphics/screen_quad_state.h"
#include "Interface/win3d_engine_submit.h" // GetWin3DSceneCamera (DAT_0067dbf8)

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <map>
#include <new>
#include <vector>
#include <windows.h>

// ---------------------------------------------------------------------------
// Retail mirrors (Font.cpp BSS)
// ---------------------------------------------------------------------------

// DAT_006105b0 -- .data initial value 1 (every cached layout starts at 0, so the first use always
// rebuilds). Its only writer is the rendermgr bump at FUN_004180c0 (INC, wrapping 0 back to 1).
int G_FontLayoutGeneration = 1;

// DAT_0061669a — NumberString paint kill switch (XC 0).
unsigned char G_NumberStringPaintDisable = 0;

namespace
{

// Defined further down in this file's second anonymous block; FontStringChild::Paint uses both.
FFontAtlasPage* FontStringChildEnsurePages(FontStringChild* Glyph, unsigned char bSkipSizeCheck);
unsigned NumberPackColor(unsigned char Alpha, unsigned Source);

// DAT_00616524 -- VC6 std::map<unsigned, NumberFace*>, key compared unsigned. Built by XC thunk
// LAB_00413610 and torn down by its atexit thunk LAB_00413640, which frees the tree nodes only
// -- a face still referenced at exit is not deleted.
std::map<unsigned, FNumberFace*> G_NumberFaceMap;

wchar_t* AllocWideBytes(unsigned ByteCount)
{
    return static_cast<wchar_t*>(std::malloc(ByteCount));
}

wchar_t* ReallocWide(wchar_t* P, unsigned ByteCount)
{
    return static_cast<wchar_t*>(std::realloc(P, ByteCount));
}

void FreeWide(wchar_t* P)
{
    std::free(P);
}

// FUN_00418180 (cdecl: font, text, width, lines, &count, max lines) -- greedy word-wrap via
// IFont::GetGlyphAdvance (+0x14). Retail's lines are one array of 8-byte {wchar_t*, capacity}
// elements; the port passes the two halves as parallel arrays. Nothing is null-tested.
int FontStringWordWrap(IFont* Font, wchar_t const* Text, float MaxWidth,
                       wchar_t** LinePtrs, unsigned* LineCaps, int* OutCount, int MaxLines)
{
    // The length is signed.
    int Remain = static_cast<int>(wcslen(Text));
    int LineCount = 0;
    if (Remain <= 0)
    {
        *OutCount = LineCount;
        return LineCount;
    }
    wchar_t const* Cursor = Text;
    wchar_t LineBuf[0x1000];

    // Every line is measured and consumed even past MaxLines; only the store is skipped.
    do
    {
        wchar_t* Out = LineBuf;
        bool bLineDone = false;
        bool bFirstChunk = true;
        float WidthLeft = MaxWidth;
        do
        {
            // One chunk up to a break. A NUL is uncounted; CR/LF and space/tab are counted and
            // break; any other character is counted while chunk + advance (unrounded x87 sum)
            // is not greater than the width left, else overflow.
            int Taken = 0;
            float Accum = 0.0f;
            int BreakKind = -1; // 0 space/tab, 1 CR/LF, 2 NUL, 3 overflow
            bool bHitBreak = false;
            do
            {
                wchar_t const Ch = Cursor[Taken];
                float const Adv = Font->GetGlyphAdvance(Ch);
                if (Ch == L'\0')
                {
                    BreakKind = 2;
                    break;
                }
                if (Ch == L'\r' || Ch == L'\n')
                {
                    BreakKind = 1;
                    bHitBreak = true;
                }
                else if (Ch == L' ' || Ch == L'\t')
                {
                    BreakKind = 0;
                    bHitBreak = true;
                }
                else if (!(static_cast<double>(Accum) + static_cast<double>(Adv) <=
                           static_cast<double>(WidthLeft)))
                {
                    BreakKind = 3;
                    break;
                }
                ++Taken;
                Accum = static_cast<float>(static_cast<double>(Adv) + static_cast<double>(Accum));
            } while (!bHitBreak);

            // An overflow chunk after the first is left for the next line.
            if (bFirstChunk || BreakKind != 3)
            {
                for (int Left = Taken; Left > 0; --Left)
                {
                    wchar_t const Ch = *Cursor;
                    if (Ch != L'\r' && Ch != L'\n')
                    {
                        *Out = Ch;
                        ++Out;
                    }
                    ++Cursor;
                }
                WidthLeft = static_cast<float>(static_cast<double>(WidthLeft) -
                                               static_cast<double>(Accum));
                Remain = Remain - Taken;
                *Out = L'\0';
            }
            if (BreakKind != 0)
            {
                bLineDone = true;
            }
            bFirstChunk = false;
        } while (!bLineDone);

        // Signed compare.
        if (LineCount < MaxLines)
        {
            unsigned const Len = static_cast<unsigned>(wcslen(LineBuf));
            unsigned const Need = Len + 1u;
            if (Need > LineCaps[LineCount])
            {
                if (LinePtrs[LineCount] != nullptr)
                {
                    LinePtrs[LineCount] = ReallocWide(LinePtrs[LineCount], Need * 2u);
                }
                else
                {
                    LinePtrs[LineCount] = AllocWideBytes(Need * 2u);
                    LinePtrs[LineCount][0] = L'\0';
                }
                LineCaps[LineCount] = Need;
            }
            wcsncpy(LinePtrs[LineCount], LineBuf, Len);
            LinePtrs[LineCount][Len] = L'\0';
            ++LineCount;
        }
    } while (Remain > 0);

    *OutCount = LineCount;
    return LineCount;
}

// FUN_00418f10 (cdecl: Dest, Source) -- a child slot's copy constructor run in place; the null
// test is the placement new's own.
void ChildSlotCopyConstruct(FFontStringChildSlot* Dest, FFontStringChildSlot const& Source)
{
    if (Dest == nullptr)
        return;
    Dest->Child = Source.Child;
    Dest->Kind = Source.Kind;
    if (Dest->Child != nullptr)
        Dest->Child->AddRef();
}

// FUN_00418610 (thiscall on the child vector at Font::String +0xc, RET 0xc: Where, Count,
// Value) -- VC6 vector<child slot>::insert(Where, Count, Value). Both callers pass Where =
// last, so only the append paths are kept. The grown capacity is size + max(Count, size),
// clamped at 0 when negative.
void ChildVectorInsertAtEnd(FFontString* Self, unsigned Count, FFontStringChildSlot const& Value)
{
    FFontStringChildSlot* const OldFirst = Self->ChildBegin;
    FFontStringChildSlot* const OldLast = Self->ChildEnd;
    unsigned const Room = static_cast<unsigned>(Self->ChildCap - OldLast);
    if (Room < Count)
    {
        unsigned const Size = (OldFirst != nullptr) ? static_cast<unsigned>(OldLast - OldFirst) : 0u;
        unsigned Grow = Count;
        if (OldFirst != nullptr && Grow < Size)
            Grow = Size;
        int NewCap = static_cast<int>(Grow + Size);
        if (NewCap < 0)
            NewCap = 0;
        FFontStringChildSlot* const NewBuf = static_cast<FFontStringChildSlot*>(
            operator new[](static_cast<unsigned>(NewCap) * sizeof(FFontStringChildSlot)));
        FFontStringChildSlot* Out = NewBuf;
        for (FFontStringChildSlot* It = OldFirst; It != OldLast; ++It, ++Out)
            ChildSlotCopyConstruct(Out, *It);
        for (unsigned Index = 0; Index < Count; ++Index, ++Out)
            ChildSlotCopyConstruct(Out, Value);
        for (FFontStringChildSlot* It = OldFirst; It != OldLast; ++It)
        {
            if (It->Child != nullptr)
                It->Child->Release();
        }
        operator delete[](OldFirst);
        Self->ChildCap = NewBuf + NewCap;
        Self->ChildEnd = (OldFirst == nullptr) ? NewBuf + Count : NewBuf + Size + Count;
        Self->ChildBegin = NewBuf;
        return;
    }
    for (unsigned Index = 0; Index < Count; ++Index)
        ChildSlotCopyConstruct(OldLast + Index, Value);
    Self->ChildEnd = OldLast + Count;
}

// The glyph grid back to the DAT_00616640 pool (textures kept).
void FreeGlyphList(FontStringChild* Child)
{
    if (Child->GlyphHead_ != nullptr)
    {
        FontAtlasFreePages(Child->GlyphHead_);
    }
}

void ReleaseChildSlot(FFontStringChildSlot& Slot)
{
    if (Slot.Child == nullptr)
    {
        return;
    }
    Slot.Child->Release();
    Slot.Child = nullptr;
}

// A child slot held by value on the stack: the argument FUN_00418610 copies from. Built as
// {child, kind} with the child AddRef'd when non-null; FUN_00414790 is its destructor (a
// non-null child's Release), which retail reaches through the EH unwind funclets
// Unwind@005b8826 (FUN_00414640) and Unwind@005b8899 (FUN_004155c0) and runs inline on the
// normal path. A local of this type gives both routes.
struct FFontStringChildTemp
{
    FFontStringChildTemp(FontStringElement* InChild, unsigned char InKind)
    {
        Slot.Child = InChild;
        Slot.Kind = InKind;
        Slot.Pad[0] = 0;
        Slot.Pad[1] = 0;
        Slot.Pad[2] = 0;
        if (Slot.Child != nullptr)
            Slot.Child->AddRef();
    }

    ~FFontStringChildTemp()
    {
        // FUN_00414790.
        ReleaseChildSlot(Slot);
    }

    FFontStringChildTemp(FFontStringChildTemp const&) = delete;
    FFontStringChildTemp& operator=(FFontStringChildTemp const&) = delete;

    FFontStringChildSlot Slot;
};

} // namespace

// ---------------------------------------------------------------------------
// The {Text, Capacity} wide-string head (FHudWideText)
// ---------------------------------------------------------------------------

void WideTextReserve(FHudWideText* Self, unsigned Need)
{
    // FUN_004142a0. Allocation results are stored unchecked.
    if (Need <= Self->Capacity)
        return;
    if (Self->Text != nullptr)
    {
        Self->Text = static_cast<wchar_t*>(realloc(Self->Text, Need * 2));
        Self->Capacity = Need;
        return;
    }
    Self->Text = static_cast<wchar_t*>(malloc(Need * 2));
    Self->Text[0] = 0;
    Self->Capacity = Need;
}

void WideTextClear(FHudWideText* Self)
{
    // FUN_00414280.
    if (Self->Text != nullptr && Self->Text[0] != 0)
        Self->Text[0] = 0;
}

void WideTextFree(FHudWideText* Self)
{
    // FUN_00444e20.
    if (Self->Text != nullptr)
    {
        free(Self->Text);
        Self->Text = nullptr;
        Self->Capacity = 0;
    }
}

// ---------------------------------------------------------------------------
// FontStringChild — `.?AUFontString@@` size 0x2c (PTR_LAB_005c8cb8)
// ---------------------------------------------------------------------------

FontStringChild::FontStringChild()
    : Text_(nullptr)
    , Capacity_(0)
    , Font_(nullptr)
    , RefCount_(1)
    , GlyphHead_(nullptr)
    , CachedW_(0)
    , CachedH_(0)
    , LayoutGen_(0)
    , Color_(0xff080808u)
    , bShadow_(1) // FUN_00414850(1) from FUN_004147a0
{
    Pad29_[0] = 0;
    Pad29_[1] = 0;
    Pad29_[2] = 0;
}

FontStringChild::~FontStringChild()
{
    FreeGlyphList(this);
    FreeWide(Text_);
    Text_ = nullptr;
    Capacity_ = 0;
}

int FontStringChild::AddRef()
{
    // LAB_00414860
    ++RefCount_;
    return RefCount_;
}

int FontStringChild::Release()
{
    // FUN_00414870 → FUN_00414c50 cleanup + operator_delete
    int const Next = --RefCount_;
    if (Next < 1)
    {
        delete this;
        return Next;
    }
    return Next;
}

void FontStringChild::BindFont(IFont* Font)
{
    // LAB_004148b0
    if (Font_ == Font)
    {
        return;
    }
    FreeGlyphList(this);
    LayoutGen_ = 0;
    Font_ = Font;
}

IFont* FontStringChild::GetFont() const
{
    // LAB_004148a0
    return Font_;
}

wchar_t const* FontStringChild::GetText() const
{
    // LAB_004313f0 (shared by ICF with the vtable at 0x005cacf8).
    return Text_;
}

void FontStringChild::SetText(wchar_t const* Text)
{
    // LAB_00414920 (RET 4). A null on either side compares as DAT_005c8ce0 (L"").
    wchar_t const* Incoming = Text;
    if (Text_ == Incoming)
    {
        return;
    }
    wchar_t const* Left = Incoming != nullptr ? Incoming : L"";
    wchar_t const* Right = Text_ != nullptr ? Text_ : L"";
    if (wcscmp(Left, Right) == 0)
    {
        return;
    }

    FreeGlyphList(this);

    if (Incoming == nullptr)
    {
        if (Text_ != nullptr && Text_[0] != L'\0')
        {
            Text_[0] = L'\0';
        }
        LayoutGen_ = 0;
        return;
    }

    // Allocation results are not tested.
    size_t const Len = wcslen(Incoming);
    unsigned const Need = static_cast<unsigned>(Len + 1u);
    if (Need > Capacity_)
    {
        if (Text_ != nullptr)
        {
            Text_ = ReallocWide(Text_, Need * sizeof(wchar_t));
        }
        else
        {
            Text_ = AllocWideBytes(Need * sizeof(wchar_t));
            Text_[0] = L'\0';
        }
        Capacity_ = Need;
    }
    wcsncpy(Text_, Incoming, Len);
    Text_[Len] = L'\0';
    LayoutGen_ = 0;
}

int* FontStringChild::GetSize(int* OutWH)
{
    // LAB_00414a40 (RET 4; FUN_004127d0 reads the returned OutWH).
    // The generation is stamped before the font test, so a null font also just returns the
    // cached size.
    if (LayoutGen_ != G_FontLayoutGeneration)
    {
        LayoutGen_ = G_FontLayoutGeneration;
        if (Font_ != nullptr)
        {
            // The text pointer goes as it is (FontImp measures a null string as 0 x 0).
            int Measure[2];
            Font_->MeasureString(Measure, Text_, -1);
            // FUN_00413e80 inlined.
            if (Measure[0] != CachedW_ || Measure[1] != CachedH_)
            {
                FreeGlyphList(this);
                CachedW_ = Measure[0];
                CachedH_ = Measure[1];
            }
        }
    }
    OutWH[0] = CachedW_;
    OutWH[1] = CachedH_;
    return OutWH;
}

void FontStringChild::SetColor(unsigned const* Argb)
{
    // LAB_00414830: then a virtual tail jump into slot 8 with 1.
    Color_ = *Argb;
    SetShadow(1);
}

void FontStringChild::SetShadow(unsigned char Value)
{
    // FUN_00414850.
    bShadow_ = Value;
}

void FontStringChild::Paint(void* Origin, void* DrawCtx, void* DrawArg)
{
    // LAB_00412e50 (RET 0xc; no Ghidra function there). Retail's second and third arguments are
    // floats, the left and right trim of the string box (list_box.cpp passes its overflow as the
    // third); the port's void* slots carry their bits.
    float TrimLeft = 0.0f;
    float TrimRight = 0.0f;
    std::memcpy(&TrimLeft, &DrawCtx, sizeof(float));
    std::memcpy(&TrimRight, &DrawArg, sizeof(float));
    FFontStringDrawOrigin const* const Org = static_cast<FFontStringDrawOrigin const*>(Origin);

    // FUN_005792b0(8) "TEXT".
    if (GetGameOption(kGameOptText) == 0)
        return;
    if (Text_ == nullptr || Text_[0] == L'\0')
        return;

    // The shadow offset -- 2 above an 800-wide surface (DAT_00610850, signed), else 1.
    float const Scale =
        static_cast<float>(static_cast<int>(G_RenderSurfaceWidth) > 0x320 ? 2 : 1);
    float OriginX = static_cast<float>(std::floor(static_cast<double>(Org->X)));
    float const OriginY = static_cast<float>(std::floor(static_cast<double>(Org->Y)));
    int Size[2];
    GetSize(Size);

    int ClipLeft = 0;
    int ClipRight = static_cast<int>(G_RenderSurfaceWidth) - 1;
    int const TrimLeftInt = static_cast<int>(std::floor(static_cast<double>(TrimLeft)));
    int const TrimRightInt = static_cast<int>(std::floor(static_cast<double>(TrimRight)));
    if (TrimRightInt > 0 || TrimLeftInt > 0)
    {
        // The width is read unsigned.
        double const Shifted = static_cast<double>(OriginX) - static_cast<double>(TrimLeftInt);
        ClipRight = static_cast<int>(static_cast<double>(static_cast<unsigned>(Size[0])) +
                                     Shifted - static_cast<double>(TrimRightInt));
        if (ClipRight < 0)
            ClipRight = 0;
        if (ClipRight > static_cast<int>(G_RenderSurfaceWidth) - 1)
            ClipRight = static_cast<int>(G_RenderSurfaceWidth) - 1;
        // With a positive left trim the window starts at the old x and the string moves left
        // by the trim.
        if (TrimLeftInt > 0)
        {
            ClipLeft = static_cast<int>(OriginX);
            OriginX = static_cast<float>(Shifted);
            if (ClipLeft < 0)
                ClipLeft = 0;
        }
    }

    // FUN_00402d30(DAT_00616670).
    ScreenQuadSetMaterialKey(FontAtlasMaterialKey());
    FFontAtlasPage* Row = FontStringChildEnsurePages(this, 1);
    if (Row == nullptr)
        return;

    float const ClipLeftF = static_cast<float>(ClipLeft);
    float const ClipRightF = static_cast<float>(ClipRight);
    for (; Row != nullptr; Row = Row->NextY)
    {
        for (FFontAtlasPage* Col = Row; Col != nullptr; Col = Col->NextX)
        {
            // DAT_005c75e0 = 0.5; the offsets are read unsigned.
            float const X0 = static_cast<float>(
                static_cast<double>(static_cast<unsigned>(Col->SrcX)) + OriginX - 0.5);
            float const Y0 = static_cast<float>(
                static_cast<double>(static_cast<unsigned>(Col->SrcY)) + OriginY - 0.5);
            // Divided by the signed page size.
            float U0 = 0.0f;
            float V0 = 0.0f;
            float U1 = static_cast<float>(static_cast<double>(Col->UsedW) /
                                          static_cast<double>(static_cast<int>(Col->PageW)));
            float V1 = static_cast<float>(static_cast<double>(Col->UsedH) /
                                          static_cast<double>(static_cast<int>(Col->PageH)));
            // FUN_00402e30(frame, 2, 0) for every page, before any clipping. Retail writes only
            // the frame's texture dword; the four rect dwords are stack contents. The records
            // carry their own UVs, so the port passes zeros there.
            unsigned long Frame[5] = {
                static_cast<unsigned long>(reinterpret_cast<uintptr_t>(Col->Texture)), 0ul, 0ul,
                0ul, 0ul};
            ScreenQuadBeginBatchFrame(Frame, 2, false);
            // The shadow quad is offset from the unclipped corners.
            float ClipX0 = X0;
            float ClipY0 = Y0;
            float X1 = static_cast<float>(static_cast<double>(Col->UsedW) + X0);
            float Y1 = static_cast<float>(static_cast<double>(Col->UsedH) + Y0);
            float ShadowX0 = static_cast<float>(static_cast<double>(X0) + Scale);
            float ShadowY0 = static_cast<float>(static_cast<double>(Y0) + Scale);
            float ShadowX1 = static_cast<float>(static_cast<double>(X1) + Scale);
            float ShadowY1 = static_cast<float>(static_cast<double>(Y1) + Scale);
            // FUN_00404670. A page clipped away on x goes on to the next one.
            if (ScreenQuadClip1D(&ClipX0, &X1, &U0, &U1, ClipLeftF, ClipRightF,
                                 static_cast<float>(static_cast<double>(X1) - X0 + 1.0)) == 0)
                continue;
            // A page clipped away on y ends the whole paint.
            if (ScreenQuadClip1D(&ClipY0, &Y1, &V0, &V1, 0.0f,
                                 static_cast<float>(static_cast<int>(G_RenderSurfaceHeight) - 1),
                                 static_cast<float>(static_cast<double>(Y1) - ClipY0 + 1.0)) == 0)
                return;

            // The shadow quad first, with its own UVs; colour = the tint's alpha (DAT_00616677)
            // over this string's colour.
            if (bShadow_ != 0)
            {
                float ShadowU0 = 0.0f;
                float ShadowV0 = 0.0f;
                float ShadowU1 = static_cast<float>(
                    static_cast<double>(Col->UsedW) /
                    static_cast<double>(static_cast<int>(Col->PageW)));
                float ShadowV1 = static_cast<float>(
                    static_cast<double>(Col->UsedH) /
                    static_cast<double>(static_cast<int>(Col->PageH)));
                if (ScreenQuadClip1D(&ShadowX0, &ShadowX1, &ShadowU0, &ShadowU1, ClipLeftF,
                                     ClipRightF,
                                     static_cast<float>(static_cast<double>(ShadowX1) -
                                                        ShadowX0 + 1.0)) != 0 &&
                    ScreenQuadClip1D(&ShadowY0, &ShadowY1, &ShadowV0, &ShadowV1, 0.0f,
                                     static_cast<float>(static_cast<int>(G_RenderSurfaceHeight) - 1),
                                     static_cast<float>(static_cast<double>(ShadowY1) -
                                                        ShadowY0 + 1.0)) != 0)
                {
                    unsigned const Color = NumberPackColor(
                        static_cast<unsigned char>(FontAtlasDrawColor() >> 24), Color_);
                    FScreenQuadRecord Record;
                    Record.X0 = ShadowX0;
                    Record.Y0 = ShadowY0;
                    Record.X1 = ShadowX1;
                    Record.Y1 = ShadowY1;
                    Record.DepthA = 1.0f;
                    Record.DepthB = 1.0f;
                    Record.U0 = ShadowU0;
                    Record.V0 = ShadowV0;
                    Record.U1 = ShadowU1;
                    Record.V1 = ShadowV1;
                    Record.ColorBits = Color;
                    ScreenQuadAppendRecord(Record);
                }
            }

            // The page quad in the tint DAT_00616674.
            unsigned const Tint = FontAtlasDrawColor();
            unsigned const Color = NumberPackColor(static_cast<unsigned char>(Tint >> 24), Tint);
            FScreenQuadRecord Record;
            Record.X0 = ClipX0;
            Record.Y0 = ClipY0;
            Record.X1 = X1;
            Record.Y1 = Y1;
            Record.DepthA = 1.0f;
            Record.DepthB = 1.0f;
            Record.U0 = U0;
            Record.V0 = V0;
            Record.U1 = U1;
            Record.V1 = V1;
            Record.ColorBits = Color;
            ScreenQuadAppendRecord(Record);
        }
    }
}

// ---------------------------------------------------------------------------
// Font::String host
// ---------------------------------------------------------------------------

void FontStringConstruct(FFontString* Self)
{
    // FUN_004144b0
    Self->Text = nullptr;
    Self->Capacity = 0;
    Self->MaxLines = 0x7fff;
    Self->Field0C = static_cast<unsigned char>(reinterpret_cast<uintptr_t>(Self) >> 24);
    Self->Pad0D[0] = 0;
    Self->Pad0D[1] = 0;
    Self->Pad0D[2] = 0;
    Self->ChildBegin = nullptr;
    Self->ChildEnd = nullptr;
    Self->ChildCap = nullptr;
    Self->FontIface = nullptr;
    Self->ExtentW = 0;
    Self->ExtentH = 0;
    Self->LayoutGen = 0;
    Self->ScaleKey = 0x7fff;
    Self->bMultiLine = 0;
    Self->Pad31[0] = 0;
    Self->Pad31[1] = 0;
    Self->Pad31[2] = 0;
}

void FontStringClearContents(FFontString* Self)
{
    // FUN_004144f0 — does NOT free the child vector buffer or clear ExtentW/H.
    if (Self->Text != nullptr)
    {
        FreeWide(Self->Text);
        Self->Text = nullptr;
        Self->Capacity = 0;
    }
    if (Self->ChildBegin != nullptr)
    {
        FFontStringChildSlot* It = Self->ChildBegin;
        FFontStringChildSlot* End = Self->ChildEnd;
        while (It != End)
        {
            if (It->Child != nullptr)
            {
                ReleaseChildSlot(*It);
            }
            ++It;
        }
        Self->ChildEnd = Self->ChildBegin;
    }
    Self->LayoutGen = 0;
}

// FUN_00445dc0 (vector<child slot>::_Destroy; thiscall on the vector, RET 8). The slots
// themselves are not cleared.
void FontStringChildSlotsDestroy(FFontStringChildSlot* First, FFontStringChildSlot* Last)
{
    FFontStringChildSlot* It = First;
    for (; It != Last; ++It)
    {
        if (It->Child == nullptr)
            continue;
        It->Child->Release();
    }
}

// FUN_00445d70 (the child vector's destructor; ECX = Font::String + 0xc).
void FontStringChildVectorDestroy(FFontString* Self)
{
    FontStringChildSlotsDestroy(Self->ChildBegin, Self->ChildEnd);
    operator delete[](Self->ChildBegin);
    Self->ChildBegin = nullptr;
    Self->ChildEnd = nullptr;
    Self->ChildCap = nullptr;
}

// FUN_00444e50 (Font::String::~String): FUN_004144f0, then FUN_00445d70 and FUN_00444e20
// inlined.
void FontStringDestroy(FFontString* Self)
{
    FontStringClearContents(Self);
    FontStringChildVectorDestroy(Self);
    if (Self->Text != nullptr)
    {
        FreeWide(Self->Text);
        Self->Text = nullptr;
        Self->Capacity = 0;
    }
}

void FontStringSetFont(FFontString* Self, IFont* Font)
{
    // FUN_004ea670 (thiscall, RET 4). The size is re-read each pass; no null test on the child,
    // no look at the kind byte.
    if (Self->FontIface == Font)
        return;
    Self->FontIface = Font;
    for (unsigned Index = 0;
         Self->ChildBegin != nullptr &&
         Index < static_cast<unsigned>(Self->ChildEnd - Self->ChildBegin);
         ++Index)
    {
        Self->ChildBegin[Index].Child->BindFont(Self->FontIface);
    }
    Self->LayoutGen = 0;
}

void FontStringSetText(FFontString* Self, wchar_t const* Text)
{
    // FUN_00415860
    if (Self->Text == Text)
    {
        return;
    }
    wchar_t const* Left = Text != nullptr ? Text : L"";
    wchar_t const* Right = Self->Text != nullptr ? Self->Text : L"";
    if (wcscmp(Left, Right) == 0)
    {
        return;
    }

    // A non-empty old text is freed, then the child vector is erased whole (inlined
    // erase(begin, end)). No null test on begin -- an empty vector's begin == end.
    if (Self->Text != nullptr && Self->Text[0] != L'\0')
    {
        FreeWide(Self->Text);
        Self->Text = nullptr;
        Self->Capacity = 0;
        FFontStringChildSlot* It = Self->ChildBegin;
        FFontStringChildSlot* End = Self->ChildEnd;
        while (It != End)
        {
            if (It->Child != nullptr)
            {
                ReleaseChildSlot(*It);
            }
            ++It;
        }
        Self->ChildEnd = Self->ChildBegin;
        Self->LayoutGen = 0;
    }

    if (Text != nullptr)
    {
        size_t const Len = wcslen(Text);
        unsigned const Need = static_cast<unsigned>(Len + 1u);
        if (Need > Self->Capacity)
        {
            // Neither allocation result is null-tested.
            if (Self->Text == nullptr)
            {
                Self->Text = AllocWideBytes(Need * sizeof(wchar_t));
                Self->Text[0] = L'\0';
            }
            else
            {
                Self->Text = ReallocWide(Self->Text, Need * sizeof(wchar_t));
            }
            Self->Capacity = Need;
        }
        wcsncpy(Self->Text, Text, Len);
        Self->Text[Len] = L'\0';
        Self->LayoutGen = 0;
        return;
    }

    if (Self->Text != nullptr && Self->Text[0] != L'\0')
    {
        Self->Text[0] = L'\0';
    }
    Self->LayoutGen = 0;
}

void FontStringSetScale(FFontString* Self, float Param1, float Param2)
{
    // FUN_004145d0. DAT_0067dc70 and DAT_0067dc9c are not font globals: they are the XScale
    // (+0x78) and ZNear (+0xa4) of the Win3D scene camera DAT_0067dbf8, read straight out of it.
    // Retail compares against double 0.001; no float lies in [0.001, 0.001f), so the float
    // compare is the same test.
    FSpaceCamera const* const Cam = GetWin3DSceneCamera();
    float Depth = std::fabs(Param2);
    if (Depth < 0.001f)
    {
        Depth = 1.0f;
    }
    double const Term =
        (static_cast<double>(Cam->XScale) / static_cast<double>(Cam->ZNear)) *
        (static_cast<double>(Cam->ZNear) / static_cast<double>(Depth)) *
        static_cast<double>(Param1) +
        0.5;
    int const Key = static_cast<int>(std::floor(Term));
    if (Key != Self->ScaleKey)
    {
        Self->ScaleKey = Key;
        Self->LayoutGen = 0;
    }
}

// FUN_00414640 (thiscall, RET 8: Index, Kind) -- the child at Index of the requested kind,
// created when missing. Returns the element; its callers go through its virtual slots.
FontStringElement* FontStringEnsureChild(FFontString* Self, unsigned Index, unsigned char Kind)
{
    FontStringElement* Result = nullptr;
    bool bReuse = false;

    if (Self->ChildBegin != nullptr && Index < static_cast<unsigned>(Self->ChildEnd - Self->ChildBegin))
    {
        FFontStringChildSlot& Slot = Self->ChildBegin[Index];
        if (Slot.Kind == Kind)
        {
            Result = Slot.Child;
            bReuse = Result != nullptr;
        }
        else
        {
            // No null test.
            Slot.Child->Release();
            Slot.Child = nullptr;
        }
    }

    if (!bReuse)
    {
        // FUN_00414d10 (0x34) / FUN_004147a0 (0x2c).
        if (Kind != 0)
            Result = new NumberStringChild();
        else
            Result = new FontStringChild();
        Result->BindFont(Self->FontIface);
    }

    // The size is re-read. The kind byte of a replaced child of the other kind stays as it was.
    if (Self->ChildBegin != nullptr && Index < static_cast<unsigned>(Self->ChildEnd - Self->ChildBegin))
    {
        Self->ChildBegin[Index].Child = Result;
        return Result;
    }

    // Otherwise one slot is appended at the END whatever Index is; after the temporary and the
    // final Release (no null test) the vector keeps the single reference the constructor gave.
    {
        FFontStringChildTemp Temp(Result, Kind);
        ChildVectorInsertAtEnd(Self, 1u, Temp.Slot);
    }
    Result->Release();
    return Result;
}

NumberStringChild* FontStringEnsureNumberChild(FFontString* Self, unsigned Index)
{
    // FUN_00414640 with kind 1 (from FUN_004159a0), whose result gets vt+0x30 -- a slot
    // only a NumberString has. The element is a NumberString whenever FUN_004159a0 reaches this:
    // a stale kind byte 1 over a FontString needs the FontString children of a non-empty text
    // (FUN_004155c0 builds none for an empty one) to outlive that text, and every path that
    // empties it (FUN_00415860, FUN_004159a0, FUN_004144f0) releases the children first.
    return static_cast<NumberStringChild*>(FontStringEnsureChild(Self, Index, 1));
}

void FontStringRebuild(FFontString* Self)
{
    // FUN_004155c0 (thiscall). Without text or font the extents are left as they are.
    Self->LayoutGen = G_FontLayoutGeneration;

    wchar_t const* Text = Self->Text;
    if (Text == nullptr || Text[0] == L'\0' || Self->FontIface == nullptr)
    {
        return;
    }

    int LineCount = 0;

    if (Self->bMultiLine != 0)
    {
        // 0x400 {line, capacity} elements (FUN_00415850 through the EH vector constructor);
        // MaxLines capped unsigned; the width is the ScaleKey read unsigned.
        unsigned MaxLines = static_cast<unsigned>(Self->MaxLines);
        if (MaxLines >= 0x400u)
        {
            MaxLines = 0x400u;
        }
        wchar_t* LinePtrs[0x400];
        unsigned LineCaps[0x400];
        for (int I = 0; I < 0x400; ++I)
        {
            LinePtrs[I] = nullptr;
            LineCaps[I] = 0u;
        }
        float const WrapWidth =
            static_cast<float>(static_cast<double>(static_cast<unsigned>(Self->ScaleKey)));
        FontStringWordWrap(Self->FontIface, Text, WrapWidth, LinePtrs, LineCaps, &LineCount,
                           static_cast<int>(MaxLines));

        // Trailing spaces cut, never below one character.
        for (int I = 0; I < LineCount; ++I)
        {
            FontStringElement* const Child =
                FontStringEnsureChild(Self, static_cast<unsigned>(I), 0);
            wchar_t* const Line = LinePtrs[I];
            size_t Len = wcslen(Line);
            while (Len > 1u && Line[Len - 1u] == L' ')
            {
                --Len;
                Line[Len] = L'\0';
            }
            Child->SetText(Line);
        }

        // FUN_00444e20 on all 0x400.
        for (int I = 0; I < 0x400; ++I)
        {
            if (LinePtrs[I] != nullptr)
            {
                FreeWide(LinePtrs[I]);
                LinePtrs[I] = nullptr;
                LineCaps[I] = 0u;
            }
        }
    }
    else
    {
        // IFont slot 9: anything that fits gives one line with the whole text.
        if (Self->FontIface->ProbeString(Self->ScaleKey, Text, -1) > 0)
        {
            LineCount = 1;
            FontStringEnsureChild(Self, 0, 0)->SetText(Self->Text);
        }
    }

    // The child vector resized to the line count. The {0, 0} value is a stack temporary
    // (unwind Unwind@005b8899 -> FUN_00414790).
    {
        FFontStringChildTemp EmptySlot(nullptr, 0);
        unsigned const Wanted = static_cast<unsigned>(LineCount);
        unsigned const Used = (Self->ChildBegin == nullptr)
                                  ? 0u
                                  : static_cast<unsigned>(Self->ChildEnd - Self->ChildBegin);
        if (Used < Wanted)
        {
            ChildVectorInsertAtEnd(Self, Wanted - Used, EmptySlot.Slot);
        }
        else if (Self->ChildBegin != nullptr && Wanted < Used)
        {
            // FUN_00418f70(0) on each surplus slot.
            for (unsigned I = Wanted; I < Used; ++I)
            {
                ReleaseChildSlot(Self->ChildBegin[I]);
            }
            Self->ChildEnd = Self->ChildBegin + Wanted;
        }
    }

    // The extents: max width (unsigned compare), summed heights; no test of the child.
    Self->ExtentW = 0;
    Self->ExtentH = 0;
    for (unsigned I = 0; Self->ChildBegin != nullptr &&
                         I < static_cast<unsigned>(Self->ChildEnd - Self->ChildBegin);
         ++I)
    {
        int Size[2] = {0, 0};
        Self->ChildBegin[I].Child->GetSize(Size);
        if (static_cast<unsigned>(Self->ExtentW) < static_cast<unsigned>(Size[0]))
        {
            Self->ExtentW = Size[0];
        }
        Self->ExtentH += Size[1];
    }
}

int* FontStringGetExtent(FFontString* Self, int* OutWH)
{
    // FUN_00414570 (thiscall, RET 4). No null test of OutWH.
    if (Self->LayoutGen != G_FontLayoutGeneration)
    {
        FontStringRebuild(Self);
    }
    OutWH[0] = Self->ExtentW;
    OutWH[1] = Self->ExtentH;
    return OutWH;
}

int FontStringChildCount(FFontString* Self)
{
    // FUN_004145a0 (thiscall).
    if (Self->LayoutGen != G_FontLayoutGeneration)
    {
        FontStringRebuild(Self);
    }
    if (Self->ChildBegin == nullptr)
    {
        return 0;
    }
    return static_cast<int>(Self->ChildEnd - Self->ChildBegin);
}

namespace
{

// FUN_004142f0 (thiscall ECX = the draw origin, RET 0xc): the child's size, the box alignment,
// then the child's vt+0x24 with a local origin.
void DrawChildAligned(FFontStringDrawOrigin const* Origin, FontStringElement* Child,
                      void* DrawCtx, void* DrawArg)
{
    int Size[2] = {0, 0};
    Child->GetSize(Size);

    FFontStringDrawOrigin Local;
    // Centring reads the width unsigned, right alignment signed.
    unsigned const HMode = Origin->HAlign;
    if (HMode == 1u)
    {
        double const Span =
            static_cast<double>(Origin->Scale) - static_cast<double>(static_cast<unsigned>(Size[0]));
        Local.X = static_cast<float>(Span * 0.5 + static_cast<double>(Origin->X));
    }
    else if (HMode == 2u)
    {
        Local.X = static_cast<float>(static_cast<double>(Origin->Scale) +
                                     static_cast<double>(Origin->X) -
                                     static_cast<double>(Size[0]));
    }
    else
    {
        Local.X = Origin->X;
    }
    unsigned const VMode = Origin->VAlign;
    if (VMode == 1u)
    {
        double const Span =
            static_cast<double>(Origin->Height) - static_cast<double>(static_cast<unsigned>(Size[1]));
        Local.Y = static_cast<float>(Span * 0.5 + static_cast<double>(Origin->Y));
    }
    else if (VMode == 2u)
    {
        Local.Y = static_cast<float>(static_cast<double>(Origin->Height) +
                                     static_cast<double>(Origin->Y) -
                                     static_cast<double>(Size[1]));
    }
    else
    {
        Local.Y = Origin->Y;
    }
    // The local origin is {x, y} followed on the stack by the {w, h} pair; the child paints
    // read only x and y.
    std::memcpy(&Local.Scale, &Size[0], sizeof(float));
    std::memcpy(&Local.Height, &Size[1], sizeof(float));
    Local.HAlign = 0;
    Local.VAlign = 0;

    Child->Paint(&Local, DrawCtx, DrawArg);
}

} // namespace

void FontStringDrawChildren(FFontStringDrawOrigin* Origin, FFontString* Self, void* DrawCtx,
                            void* DrawArg)
{
    // FUN_004143c0 — thiscall ECX = Origin, args (Font::String*, DrawCtx, DrawArg), RET 0xc.
    if (Self == nullptr)
    {
        return;
    }
    float const SavedX = Origin->X;
    float const SavedY = Origin->Y;

    // ftol(width) (truncation) is the wrap key.
    int const ScaleKey = static_cast<int>(Origin->Scale);
    if (ScaleKey != Self->ScaleKey)
    {
        Self->ScaleKey = ScaleKey;
        Self->LayoutGen = 0;
    }
    if (Self->LayoutGen != G_FontLayoutGeneration)
    {
        FontStringRebuild(Self);
    }

    // The count is taken once; each pass re-checks the vector bounds and a null child.
    int Count = 0;
    if (Self->ChildBegin != nullptr)
    {
        Count = static_cast<int>(Self->ChildEnd - Self->ChildBegin);
    }
    for (int I = 0; I < Count; ++I)
    {
        if (Self->ChildBegin == nullptr ||
            static_cast<unsigned>(I) >= static_cast<unsigned>(Self->ChildEnd - Self->ChildBegin))
        {
            continue;
        }
        FontStringElement* const Child = Self->ChildBegin[I].Child;
        if (Child == nullptr)
        {
            continue;
        }
        // Line I sits at y + h * I, the product read unsigned.
        int Size[2] = {0, 0};
        Child->GetSize(Size);
        unsigned const LineOffset = static_cast<unsigned>(Size[1] * I);
        Origin->X = SavedX;
        Origin->Y = static_cast<float>(static_cast<double>(LineOffset) + static_cast<double>(SavedY));
        DrawChildAligned(Origin, Child, DrawCtx, DrawArg);
    }
}

void FontStringSetNumberText(FFontString* Self, char const* Format)
{
    // FUN_004159a0 (thiscall, RET 8; the first argument is never read).
    // Only a non-empty wide text is dropped (with the children).
    if (Self->Text != nullptr && Self->Text[0] != L'\0')
    {
        FreeWide(Self->Text);
        Self->Text = nullptr;
        Self->Capacity = 0;
        for (FFontStringChildSlot* It = Self->ChildBegin; It != Self->ChildEnd; ++It)
        {
            if (It->Child != nullptr)
            {
                ReleaseChildSlot(*It);
            }
        }
        Self->ChildEnd = Self->ChildBegin;
        Self->LayoutGen = 0;
    }
    NumberStringChild* const Child = FontStringEnsureNumberChild(Self, 0);
    Child->Printf(Format);
    // Widest width (unsigned) and summed height; the children are not null-checked.
    Self->ExtentW = 0;
    Self->ExtentH = 0;
    for (unsigned Index = 0;
         Self->ChildBegin != nullptr &&
         Index < static_cast<unsigned>(Self->ChildEnd - Self->ChildBegin);
         ++Index)
    {
        int Size[2] = {0, 0};
        Self->ChildBegin[Index].Child->GetSize(Size);
        if (static_cast<unsigned>(Size[0]) > static_cast<unsigned>(Self->ExtentW))
        {
            Self->ExtentW = Size[0];
        }
        Self->ExtentH += Size[1];
    }
}

// ---------------------------------------------------------------------------
// NumberString — `.?AUFontNumberString@@` size 0x34 (PTR_LAB_005c8ce8)
// ---------------------------------------------------------------------------

namespace
{

// DAT_00616638 / DAT_0061663c -- {text, capacity} built by XC thunk LAB_00413540 from
// u"-.0123456789MK%/" (005c8c90): capacity = wcslen + 1 = 17, freed by its atexit thunk
// LAB_004135e0. Nothing else writes it, so between the two it is this constant pair.
wchar_t const G_NumberGlyphText[] = L"-.0123456789MK%/";
unsigned const G_NumberGlyphTextCapacity = 17u;

// DAT_00616538[0x100] / DAT_00616699 -- the byte -> glyph index map Printf builds on first use:
// zeroed, then map["-.0123456789MK%/"[i]] = i for i < 0x11 (DAT_005c8bb8, the NUL included, so
// map[0] = 16). Any other byte maps to 0, the '-' glyph.
unsigned char G_NumberGlyphMap[0x100];
unsigned char G_bNumberGlyphMapBuilt = 0;
char const G_NumberGlyphChars[] = "-.0123456789MK%/";

// FUN_00413df0 (thiscall on a {wchar_t*, capacity} pair, RET 8): a null source clears a
// non-empty string; otherwise Len (-1: wcslen) characters are copied after growing the buffer
// to Len + 1 (realloc, or malloc with the first character zeroed), and [Len] = 0.
void AssignWideCount(wchar_t*& Text, unsigned& Capacity, wchar_t const* Source, unsigned Len)
{
    if (Source == nullptr)
    {
        if (Text != nullptr && Text[0] != L'\0')
        {
            Text[0] = L'\0';
        }
        return;
    }
    if (Len == 0xffffffffu)
    {
        Len = static_cast<unsigned>(wcslen(Source));
    }
    unsigned const Need = Len + 1u;
    if (Need > Capacity)
    {
        if (Text != nullptr)
        {
            Text = ReallocWide(Text, Need * 2u);
        }
        else
        {
            Text = AllocWideBytes(Need * 2u);
            Text[0] = L'\0';
        }
        Capacity = Need;
    }
    wcsncpy(Text, Source, Len);
    Text[Len] = L'\0';
}

// FUN_004152e0 (thiscall on a FontString, RET 4): a stale grid goes back to the pool, an empty
// one is rasterised (FUN_004127d0 with the argument). The generation is not stamped here.
FFontAtlasPage* FontStringChildEnsurePages(FontStringChild* Glyph, unsigned char bSkipSizeCheck)
{
    if (Glyph->LayoutGen_ != G_FontLayoutGeneration && Glyph->GlyphHead_ != nullptr)
    {
        FontAtlasFreePages(Glyph->GlyphHead_);
    }
    if (Glyph->GlyphHead_ == nullptr)
    {
        FontAtlasRasterize(Glyph, bSkipSizeCheck);
    }
    return Glyph->GlyphHead_;
}

// The glyph FontString of a face, as three retail sites build it inline (FUN_00413710,
// LAB_00415090, FUN_00414070): FUN_00417620, the face key, FUN_00413df0 with DAT_00616638 /
// DAT_0061663c (so 17 characters, capacity 18), then FUN_004152e0's body; a grid that is
// still empty releases the FontString again.
void NumberFaceCreateGlyph(FNumberFace* Face)
{
    FontStringChild* const Glyph = new FontStringChild();
    Face->Glyph = Glyph;
    Glyph->BindFont(Face->FontKey);
    AssignWideCount(Glyph->Text_, Glyph->Capacity_, G_NumberGlyphText, G_NumberGlyphTextCapacity);
    if (FontStringChildEnsurePages(Glyph, 0) == nullptr)
    {
        Glyph->Release();
        Face->Glyph = nullptr;
    }
}

// The advance / texture table of a face from its glyph FontString and its first page, as
// FUN_00414070 and LAB_00415090 both compute it: IFont slot 10 over the glyph text with the
// text's capacity (18) as the count, straight into face+8. Retail lets the widths past the 17th
// run on into Uv[0..], which the loop then overwrites field by field; the port collects them
// in a buffer of the capacity and keeps the first 17, which leaves the face bytes identical.
void NumberFaceBuildTable(FNumberFace* Face, FontStringChild* Glyph, FFontAtlasPage* Page)
{
    IFont* const Font = Glyph->GetFont();
    wchar_t const* const Text = Glyph->GetText();
    std::vector<int> Widths(Glyph->Capacity_ > 17u ? Glyph->Capacity_ : 17u, 0);
    Font->GetCharWidths(Text, static_cast<int>(Glyph->Capacity_), Widths.data());
    for (unsigned Index = 0; Index < 17u && Index < Glyph->Capacity_; ++Index)
    {
        Face->Advance[Index] = Widths[Index];
    }

    // UsedH read unsigned, PageH signed; stays on the FPU stack for the loop.
    double const VRatio = static_cast<double>(Page->UsedH) /
                          static_cast<double>(static_cast<int>(Page->PageH));
    float Previous = 0.0f; // DAT_005c7474
    float Sum = 0.0f;
    for (unsigned Index = 0; Index < 17u; ++Index)
    {
        Sum = static_cast<float>(static_cast<double>(Face->Advance[Index]) +
                                 static_cast<double>(Sum));
        Face->Uv[Index].V0 = 0.0f;
        float const U1 =
            static_cast<float>(static_cast<double>(Sum) / static_cast<double>(Page->PageW));
        Face->Uv[Index].U0 = Previous;
        Face->Uv[Index].U1 = U1;
        Face->Uv[Index].V1 = static_cast<float>(VRatio);
        Previous = U1;
    }
}

// FUN_00414070 (thiscall on the face) -- rebuild the advance / texture table when the face's
// generation is stale.
void NumberFaceRebuild(FNumberFace* Face)
{
    if (Face->LayoutGen == G_FontLayoutGeneration)
    {
        return;
    }
    if (Face->Glyph == nullptr)
    {
        NumberFaceCreateGlyph(Face);
    }
    FontStringChild* const Glyph = Face->Glyph;
    if (Glyph == nullptr)
    {
        return;
    }
    // The generation is stamped before the pages are looked at.
    Face->LayoutGen = G_FontLayoutGeneration;
    FFontAtlasPage* const Page = FontStringChildEnsurePages(Glyph, 0);
    if (Page == nullptr)
    {
        return;
    }
    NumberFaceBuildTable(Face, Glyph, Page);
}

// FUN_00418890(Last, 1, &Glyph) -- VC6 vector<char>::insert at the end: with no spare
// capacity a new buffer of size + max(1, size) (1 for an empty vector), the old bytes copied,
// the old buffer freed; else in place.
void NumberGlyphAppend(NumberStringChild* Self, unsigned char Glyph)
{
    unsigned const Spare = static_cast<unsigned>(Self->GlyphEnd_ - Self->GlyphLast_);
    if (Spare < 1u)
    {
        unsigned const Size = Self->GlyphFirst_ == nullptr
                                  ? 0u
                                  : static_cast<unsigned>(Self->GlyphLast_ - Self->GlyphFirst_);
        unsigned Grow = 1u;
        if (Self->GlyphFirst_ != nullptr && Size > 1u)
        {
            Grow = Size;
        }
        unsigned const NewCapacity = Size + Grow;
        unsigned char* const NewFirst = static_cast<unsigned char*>(::operator new(NewCapacity));
        unsigned char* Out = NewFirst;
        for (unsigned char* In = Self->GlyphFirst_; In != Self->GlyphLast_; ++In)
        {
            *Out = *In;
            ++Out;
        }
        *Out = Glyph;
        ::operator delete(Self->GlyphFirst_);
        Self->GlyphFirst_ = NewFirst;
        Self->GlyphLast_ = NewFirst + Size + 1u;
        Self->GlyphEnd_ = NewFirst + NewCapacity;
        return;
    }
    *Self->GlyphLast_ = Glyph;
    ++Self->GlyphLast_;
}

// ARGB assembled the way LAB_004137a0 does it: alpha given, then bytes 0, 1, 2 of Source from
// high to low -- the red and blue bytes of a D3DCOLOR trade places.
unsigned NumberPackColor(unsigned char Alpha, unsigned Source)
{
    unsigned const B0 = Source & 0xffu;
    unsigned const B1 = (Source >> 8) & 0xffu;
    unsigned const B2 = (Source >> 16) & 0xffu;
    return (static_cast<unsigned>(Alpha) << 24) | (B0 << 16) | (B1 << 8) | B2;
}

} // namespace

void WideTextAssign(FHudWideText* Self, wchar_t const* Source, unsigned Len)
{
    // FUN_00413df0 on a free-standing {Text, Capacity} head (PageCharacterSelectSingle's
    // ShowDetails).
    AssignWideCount(Self->Text, Self->Capacity, Source, Len);
}

NumberStringChild::NumberStringChild()
    : Alloc04_(0)
    , GlyphFirst_(nullptr)
    , GlyphLast_(nullptr)
    , GlyphEnd_(nullptr)
    , Font_(nullptr)
    , RefCount_(1)
    , Color_(0xff080808u)
    , bShadow_(0)
    , LayoutGen_(0)
    , Width_(0)
    , Height_(0)
    , FaceCache_(nullptr)
{
    // FUN_00414d10: +4 is a stack byte in retail (the vector allocator); then FUN_004154e0(1)
    // through the vtable.
    Pad05_[0] = 0;
    Pad05_[1] = 0;
    Pad05_[2] = 0;
    Pad21_[0] = 0;
    Pad21_[1] = 0;
    Pad21_[2] = 0;
    SetShadow(1);
}

NumberStringChild::~NumberStringChild()
{
    // FUN_004154f0: the face first, then the glyph vector.
    if (FaceCache_ != nullptr)
    {
        NumberFaceRelease(static_cast<FNumberFace*>(FaceCache_));
        FaceCache_ = nullptr;
    }
    ::operator delete(GlyphFirst_);
    GlyphFirst_ = nullptr;
    GlyphLast_ = nullptr;
    GlyphEnd_ = nullptr;
}

int NumberStringChild::AddRef()
{
    // LAB_00414f70
    ++RefCount_;
    return RefCount_;
}

int NumberStringChild::Release()
{
    // FUN_00414f80.
    int const Remaining = --RefCount_;
    if (Remaining < 1)
    {
        delete this;
    }
    return Remaining;
}

void NumberStringChild::BindFont(IFont* Font)
{
    // FUN_00414fb0 (RET 4).
    if (Font_ == Font)
    {
        return;
    }
    Font_ = Font;
    if (FaceCache_ != nullptr)
    {
        NumberFaceRelease(static_cast<FNumberFace*>(FaceCache_));
        FaceCache_ = nullptr;
    }
    // Retail zeroes +0x24 on every change, face or not.
    LayoutGen_ = 0;
}

IFont* NumberStringChild::GetFont() const
{
    // LAB_00415070.
    return Font_;
}

wchar_t const* NumberStringChild::GetText() const
{
    // LAB_00415080: DAT_005c8ce0, the shared empty wide string.
    return L"";
}

void NumberStringChild::SetText(wchar_t const* Text)
{
    // LAB_0053e4a0 (an ICF-folded empty body shared with 20 other slots).
    (void)Text;
}

int* NumberStringChild::GetSize(int* OutWH)
{
    // LAB_00415090 (RET 4).
    if (LayoutGen_ != G_FontLayoutGeneration)
    {
        LayoutGen_ = G_FontLayoutGeneration;
        Width_ = 0;
        Height_ = 0;
        // FUN_00413710.
        if (FaceCache_ == nullptr)
        {
            FaceCache_ = NumberFaceGetOrCreate(Font_);
        }
        FNumberFace* const Face = static_cast<FNumberFace*>(FaceCache_);
        if (Face != nullptr)
        {
            if (Face->Glyph == nullptr)
            {
                NumberFaceCreateGlyph(Face);
            }
            FontStringChild* const Glyph = Face->Glyph;
            if (Glyph != nullptr)
            {
                int GlyphSize[2] = {0, 0};
                Glyph->GetSize(GlyphSize);
                Height_ = GlyphSize[1];
                // A face gone stale meanwhile gets its table rebuilt first (FUN_00414070's
                // steps inlined).
                for (unsigned char* It = GlyphFirst_; It != GlyphLast_; ++It)
                {
                    unsigned const Index = *It;
                    if (Face->LayoutGen != G_FontLayoutGeneration)
                    {
                        if (Face->Glyph == nullptr)
                        {
                            NumberFaceCreateGlyph(Face);
                        }
                        FontStringChild* const Current = Face->Glyph;
                        if (Current != nullptr)
                        {
                            Face->LayoutGen = G_FontLayoutGeneration;
                            FFontAtlasPage* const Page = FontStringChildEnsurePages(Current, 0);
                            if (Page != nullptr)
                            {
                                NumberFaceBuildTable(Face, Current, Page);
                            }
                        }
                    }
                    Width_ += Face->Advance[Index];
                }
            }
        }
    }
    OutWH[0] = Width_;
    OutWH[1] = Height_;
    return OutWH;
}

void NumberStringChild::SetColor(unsigned const* Argb)
{
    // LAB_004154c0: then a virtual tail jump into slot 8 with 1.
    Color_ = *Argb;
    SetShadow(1);
}

void NumberStringChild::SetShadow(unsigned char Value)
{
    // FUN_004154e0.
    bShadow_ = Value;
}

void NumberStringChild::FormatFloat(float Value, int Decimals, char Suffix)
{
    // LAB_00414f00 (RET 0xc).
    char Format[0xc];
    _snprintf(Format, 10, "%%.%df%c", Decimals, static_cast<int>(Suffix));
    Printf(Format, static_cast<double>(Value));
}

void NumberStringChild::FormatInt(int Value, char Suffix)
{
    // LAB_00414f50 (RET 8): "%d%c" is DAT_005c8d28; the suffix is sign-extended.
    Printf("%d%c", Value, static_cast<int>(Suffix));
}

void NumberStringChild::Printf(char const* Format, ...)
{
    // LAB_00414db0 (cdecl through vt+0x30). _vsnprintf into a 0x20-byte local.
    // Deviation: the port keeps one more byte and a NUL there. When the output does not fit,
    // retail gets -1 and an unterminated buffer, reserves new(0) with End = First - 1, and reads
    // the stack past the buffer -- heap and stack corruption that no retail caller reaches
    // (their formats are a number and a suffix).
    char Buffer[0x21];
    va_list Args;
    va_start(Args, Format);
    int const Count = MsvcrtNumericVsnprintf(Buffer, 0x20, Format, Args);
    va_end(Args);
    Buffer[0x20] = '\0';

    // reserve(Count) (FUN_0044ce60 copies the old bytes one by one).
    unsigned const Capacity =
        GlyphFirst_ == nullptr ? 0u : static_cast<unsigned>(GlyphEnd_ - GlyphFirst_);
    if (Count >= 0 && Capacity < static_cast<unsigned>(Count))
    {
        unsigned char* const NewFirst =
            static_cast<unsigned char*>(::operator new(static_cast<size_t>(Count)));
        unsigned char* Out = NewFirst;
        for (unsigned char* In = GlyphFirst_; In != GlyphLast_; ++In)
        {
            *Out = *In;
            ++Out;
        }
        unsigned const Size =
            GlyphFirst_ == nullptr ? 0u : static_cast<unsigned>(GlyphLast_ - GlyphFirst_);
        ::operator delete(GlyphFirst_);
        GlyphEnd_ = NewFirst + Count;
        GlyphLast_ = NewFirst + Size;
        GlyphFirst_ = NewFirst;
    }

    if (GlyphFirst_ != nullptr)
    {
        GlyphLast_ = GlyphFirst_;
    }

    if (G_bNumberGlyphMapBuilt == 0)
    {
        G_bNumberGlyphMapBuilt = 1;
        std::memset(G_NumberGlyphMap, 0, sizeof(G_NumberGlyphMap));
        for (unsigned Index = 0; Index < 0x11u; ++Index)
        {
            G_NumberGlyphMap[static_cast<unsigned char>(G_NumberGlyphChars[Index])] =
                static_cast<unsigned char>(Index);
        }
    }

    // Retail indexes the map with the byte sign-extended, so a byte >= 0x80 would read the 0x80
    // bytes before DAT_00616538; vsnprintf of the callers' formats only yields ASCII.
    for (char const* It = Buffer; *It != '\0'; ++It)
    {
        NumberGlyphAppend(this, G_NumberGlyphMap[static_cast<unsigned char>(*It)]);
    }

    LayoutGen_ = 0;
}

void NumberStringChild::Paint(void* Origin, void* DrawCtx, void* DrawArg)
{
    // LAB_004137a0 (RET 0xc) -- only the origin is read; the two trailing arguments are not.
    (void)DrawCtx;
    (void)DrawArg;
    if (G_NumberStringPaintDisable != 0)
    {
        return;
    }
    if (GlyphFirst_ == nullptr || GlyphLast_ == GlyphFirst_)
    {
        return;
    }
    if (FaceCache_ == nullptr)
    {
        FaceCache_ = NumberFaceGetOrCreate(Font_);
    }
    FNumberFace* const Face = static_cast<FNumberFace*>(FaceCache_);
    if (Face == nullptr)
    {
        return;
    }
    if (Face->Glyph == nullptr)
    {
        NumberFaceCreateGlyph(Face);
    }
    FontStringChild* const Glyph = Face->Glyph;
    if (Glyph == nullptr)
    {
        return;
    }
    // No release of the glyph on failure here.
    FFontAtlasPage* const Page = FontStringChildEnsurePages(Glyph, 0);
    if (Page == nullptr)
    {
        return;
    }

    // FUN_00402d30(DAT_00616670).
    ScreenQuadSetMaterialKey(FontAtlasMaterialKey());
    // The shadow offset / pixel scale -- 2 above an 800-wide surface (signed), else 1.
    int const ScaleInt = static_cast<int>(G_RenderSurfaceWidth) > 0x320 ? 2 : 1;
    float const Scale = static_cast<float>(ScaleInt);
    // DAT_005c75e8 (double 0.5).
    FFontStringDrawOrigin const* const Org = static_cast<FFontStringDrawOrigin const*>(Origin);
    float const X0 = static_cast<float>(std::floor(static_cast<double>(Org->X)) - 0.5);
    float const Y0 = static_cast<float>(std::floor(static_cast<double>(Org->Y)) - 0.5);
    // FUN_00402e30(frame, 2, 1). Retail leaves the frame's four rect dwords unwritten (stack
    // contents); the records below carry their own UVs, so the port passes zeros there.
    unsigned long Frame[5] = {static_cast<unsigned long>(reinterpret_cast<uintptr_t>(Page->Texture)),
                              0ul, 0ul, 0ul, 0ul};
    ScreenQuadBeginBatchFrame(Frame, 2, true);
    if (Face->LayoutGen != G_FontLayoutGeneration)
    {
        NumberFaceRebuild(Face);
    }
    int Size[2] = {0, 0};
    GetSize(Size);

    // The string box, w and h read unsigned, with the UV rect {0, glyph 0's V0, 1, glyph 0's V1}.
    float const X1 = static_cast<float>(
        static_cast<double>(static_cast<unsigned>(Size[0])) + X0 + Scale);
    float const Y1 = static_cast<float>(
        static_cast<double>(static_cast<unsigned>(Size[1])) + Y0 + Scale);
    float ClipX0 = X0;
    float ClipY0 = Y0;
    float ClipX1 = X1;
    float ClipY1 = Y1;
    float ClipU0 = 0.0f;
    float ClipV0 = Face->Uv[0].V0;
    float ClipU1 = 1.0f;
    float ClipV1 = Face->Uv[0].V1;
    int const ScreenW = static_cast<int>(G_RenderSurfaceWidth);
    int const ScreenH = static_cast<int>(G_RenderSurfaceHeight);
    // FUN_00404670, y first.
    if (ScreenQuadClip1D(&ClipY0, &ClipY1, &ClipV0, &ClipV1, 0.0f,
                         static_cast<float>(ScreenH - 1),
                         static_cast<float>(static_cast<double>(Y1) - Y0 + 1.0)) == 0)
    {
        return;
    }
    if (ScreenQuadClip1D(&ClipX0, &ClipX1, &ClipU0, &ClipU1, 0.0f,
                         static_cast<float>(ScreenW - 1),
                         static_cast<float>(static_cast<double>(X1) - X0 + 1.0)) == 0)
    {
        return;
    }

    // The pen starts at the unclipped X0.
    float PenX = X0;
    for (unsigned char* It = GlyphFirst_; It != GlyphLast_; ++It)
    {
        unsigned const Index = *It;
        // FUN_00414070 on a stale face, before and after the advance read.
        if (Face->LayoutGen != G_FontLayoutGeneration)
        {
            NumberFaceRebuild(Face);
        }
        float const Advance = static_cast<float>(Face->Advance[Index]);
        if (Face->LayoutGen != G_FontLayoutGeneration)
        {
            NumberFaceRebuild(Face);
        }
        float const GlyphX0 = PenX;
        PenX = static_cast<float>(static_cast<double>(Advance) + PenX);
        FNumberGlyphUv const& Uv = Face->Uv[Index];

        // A glyph starting left of the clipped box is skipped (ordered less only).
        if (GlyphX0 < ClipX0)
        {
            continue;
        }
        // One ending at or past the clipped right edge ends the string; unordered keeps drawing.
        if (PenX >= ClipX1)
        {
            return;
        }

        // The shadow quad: y from the clipped box top + scale down to the clipped box bottom,
        // clipped on x again; colour = the tint's alpha over this string's colour.
        if (bShadow_ != 0)
        {
            float ShadowX0 = static_cast<float>(static_cast<double>(GlyphX0) + Scale);
            float const ShadowY0 = static_cast<float>(static_cast<double>(ClipY0) + Scale);
            float ShadowX1 = static_cast<float>(static_cast<double>(PenX) + Scale);
            float ShadowU0 = Uv.U0;
            float ShadowU1 = Uv.U1;
            if (ScreenQuadClip1D(&ShadowX0, &ShadowX1, &ShadowU0, &ShadowU1, 0.0f,
                                 static_cast<float>(ScreenW - 1),
                                 static_cast<float>(static_cast<double>(ShadowX1) - ShadowX0 + 1.0)) != 0)
            {
                unsigned const Tint = FontAtlasDrawColor();
                unsigned const Color =
                    NumberPackColor(static_cast<unsigned char>(Tint >> 24), Color_);
                FScreenQuadRecord Record;
                Record.X0 = ShadowX0;
                Record.Y0 = ShadowY0;
                Record.X1 = ShadowX1;
                Record.Y1 = ClipY1;
                Record.DepthA = 1.0f;
                Record.DepthB = 1.0f;
                Record.U0 = ShadowU0;
                Record.V0 = ClipV0;
                Record.U1 = ShadowU1;
                Record.V1 = ClipV1;
                Record.ColorBits = Color;
                ScreenQuadAppendRecord(Record);
            }
        }

        // The glyph quad down to the clipped bottom - scale, skipped when that is above the top
        // (unordered too).
        float const GlyphY1 = static_cast<float>(static_cast<double>(ClipY1) - Scale);
        if (!(GlyphY1 >= ClipY0))
        {
            continue;
        }
        unsigned const Tint = FontAtlasDrawColor();
        unsigned const Color = NumberPackColor(static_cast<unsigned char>(Tint >> 24), Tint);
        FScreenQuadRecord Record;
        Record.X0 = GlyphX0;
        Record.Y0 = ClipY0;
        Record.X1 = PenX;
        Record.Y1 = GlyphY1;
        Record.DepthA = 1.0f;
        Record.DepthB = 1.0f;
        Record.U0 = Uv.U0;
        Record.V0 = ClipV0;
        Record.U1 = Uv.U1;
        Record.V1 = ClipV1;
        Record.ColorBits = Color;
        ScreenQuadAppendRecord(Record);
    }
}

FNumberFace* NumberFaceGetOrCreate(IFont* FontKey)
{
    // FUN_00413710 (retail takes the key by pointer).
    if (FontKey == nullptr)
    {
        return nullptr;
    }
    // FUN_0046d6d0: insert (key, null) or land on the existing node.
    FNumberFace*& Slot =
        G_NumberFaceMap
            .insert(std::map<unsigned, FNumberFace*>::value_type(
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(FontKey)), nullptr))
            .first->second;
    if (Slot != nullptr)
    {
        ++Slot->RefCount;
        return Slot;
    }
    // operator new(0x164); only +0, +4, +0x15c and +0x160 are written. A failed allocation
    // stores null into the node.
    FNumberFace* Face = new (std::nothrow) FNumberFace;
    if (Face != nullptr)
    {
        Face->Glyph = nullptr;
        Face->LayoutGen = 0;
        Face->FontKey = FontKey;
        Face->RefCount = 1;
    }
    Slot = Face;
    return Face;
}

void NumberFaceRelease(FNumberFace* Face)
{
    // The sequence FUN_00414fb0 and the NumberString dtor both inline (find FUN_00432240,
    // erase FUN_0046c8c0). The node is looked up before the count drops. Callers test the face
    // first.
    std::map<unsigned, FNumberFace*>::iterator Found =
        G_NumberFaceMap.find(static_cast<unsigned>(reinterpret_cast<uintptr_t>(Face->FontKey)));
    int const Remaining = --Face->RefCount;
    if (Remaining <= 0)
    {
        // FUN_00415050.
        if (Face->Glyph != nullptr)
        {
            Face->Glyph->Release();
            Face->Glyph = nullptr;
        }
        Face->LayoutGen = 0;
        delete Face;
    }
    if (Found != G_NumberFaceMap.end() && Remaining <= 0)
    {
        G_NumberFaceMap.erase(Found);
    }
}

void NumberFaceResetAll()
{
    // Inlined in FUN_004180c0: FUN_00415050 on every non-null face.
    std::map<unsigned, FNumberFace*>::iterator It = G_NumberFaceMap.begin();
    for (; It != G_NumberFaceMap.end(); ++It)
    {
        FNumberFace* const Face = It->second;
        if (Face == nullptr)
        {
            continue;
        }
        if (Face->Glyph != nullptr)
        {
            Face->Glyph->Release();
            Face->Glyph = nullptr;
        }
        Face->LayoutGen = 0;
    }
}
