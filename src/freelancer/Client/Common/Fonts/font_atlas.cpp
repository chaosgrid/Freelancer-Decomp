// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Fonts/font_atlas.h"

#include "font.h"
#include "Common/Fonts/font_imp.h"
#include "Common/Fonts/font_string.h"
#include "Common/Graphics/pixel_format.h"

#include "DALib.h"
#include "dacom.h"
#include "render/IRenderPipeline.h"

#include <cstdlib>
#include <cstring>
#include <vector>
#include <wchar.h>

namespace
{

// Retail DAT_00616654..6c — shared GDI scratch for glyph upload.
HBITMAP G_AtlasBitmap = nullptr;   // DAT_00616654
HGDIOBJ G_AtlasPrevObj = nullptr;  // DAT_00616658
unsigned short* G_AtlasBits = nullptr; // DAT_0061665c
BITMAPINFO* G_AtlasBmi = nullptr;  // DAT_00616660
HDC G_AtlasDc = nullptr;           // DAT_00616664
// DAT_00616668 / DAT_0061666c are 0 in the PE; FUN_00412670 (the scratch object's +0x14 / +0x18,
// thiscall on DAT_00616654) is their only writer and sets both to 0x100.
unsigned G_AtlasMaxW = 0;          // DAT_00616668
unsigned G_AtlasMaxH = 0;          // DAT_0061666c
// DAT_00616670 -- seeded at load time by XC thunk LAB_00412650. Swapped by FUN_00417790.
unsigned G_AtlasMaterialKey = DACOM_CRC::GetCRC32("font");
// DAT_00616674 -- zeroed by XC thunk LAB_00412620; the only writer after that is
// FUN_00417780 (FontAtlasSetDrawColor).
unsigned G_AtlasDrawColor = 0;
unsigned G_AtlasTexBytes = 0;      // DAT_00616640+0x10

// DAT_00616644..4c — free page pointer vector (FUN_00412cc0 / FUN_00537260 push).
std::vector<FFontAtlasPage*> G_AtlasFreePages;

unsigned RoundUpPow2(unsigned Value)
{
    // Inlined in FUN_004127d0 / FUN_00412cc0. 0 is kept as it is.
    if ((Value & (Value - 1u)) == 0u)
    {
        return Value;
    }
    unsigned Result = (Value > 0x40u) ? 0x80u : 1u;
    while (Result < Value)
    {
        Result <<= 1u;
    }
    return Result;
}

IRenderPipeline* Pipe()
{
    if (DALib::Pipeline == nullptr)
    {
        return nullptr;
    }
    return DALib::Pipeline;
}

FFontAtlasPage* CreateAtlasPage(unsigned NeedW, unsigned NeedH)
{
    // FUN_00412cc0 (thiscall on the pool DAT_00616640, RET 8). A reused page keeps the offsets,
    // used size and content height its last string left there.
    for (std::vector<FFontAtlasPage*>::iterator It = G_AtlasFreePages.begin();
         It != G_AtlasFreePages.end(); ++It)
    {
        FFontAtlasPage* const Page = *It;
        if (Page->PageW >= NeedW && Page->PageH >= NeedH)
        {
            G_AtlasFreePages.erase(It);
            return Page;
        }
    }

    // operator new(0x28).
    FFontAtlasPage* const Page = new FFontAtlasPage();
    memset(Page, 0, sizeof(*Page));

    // FUN_005b7050 -- 16-bit 5/5/5/1.
    unsigned long Format = 0;
    PixelFormatFind(&Format, 0x10, 5, 5, 5, 1);

    unsigned const TexW = RoundUpPow2(NeedW);
    Page->PageW = TexW;
    unsigned const TexH = RoundUpPow2(NeedH);
    Page->PageH = TexH;
    Page->Texture = nullptr;

    unsigned Format32 = static_cast<unsigned>(Format);
    GENRESULT const Gr = DALib::Pipeline->CreateTexture(
        static_cast<int>(TexW), static_cast<int>(TexH), &Format32, 1u, 0u, &Page->Texture);
    if (Gr != GR_OK)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\Font.cpp", 0x192,
              "FontString - failed to create texture");
        delete Page;
        return nullptr;
    }
    G_AtlasTexBytes += TexW * TexH * 2u;
    return Page;
}

// FUN_00412cb0 -- the font selection FUN_004127d0 holds over its draw loop. Its destructor runs
// inline on the normal exit and as the EH unwind funclet (005b87f0).
struct FAtlasFontSelection
{
    HDC Dc;
    HGDIOBJ Previous;

    FAtlasFontSelection(HDC InDc, HGDIOBJ Font)
        : Dc(InDc)
        , Previous(SelectObject(InDc, Font))
    {
    }

    ~FAtlasFontSelection()
    {
        SelectObject(Dc, Previous);
    }

    FAtlasFontSelection(FAtlasFontSelection const&) = delete;
    FAtlasFontSelection& operator=(FAtlasFontSelection const&) = delete;
};

} // namespace

void FontAtlasEnsureScratch()
{
    // FUN_00412670 (thiscall on DAT_00616654, RET 4 with DALib::Pipeline, which it never reads).
    // Its only caller is FUN_004180c0, which deletes the previous objects first: there is no
    // lazy latch and no allocation check. Only the 0x28-byte header is zeroed; the pixel area
    // is left as malloc returns it.
    G_AtlasMaxW = 256;
    G_AtlasMaxH = 256;

    void* Block = std::malloc(0x20028u);
    memset(Block, 0, 0x28u);
    G_AtlasBmi = static_cast<BITMAPINFO*>(Block);
    G_AtlasBits = reinterpret_cast<unsigned short*>(static_cast<unsigned char*>(Block) + 0x28);

    BITMAPINFOHEADER& Hdr = G_AtlasBmi->bmiHeader;
    Hdr.biSize = sizeof(BITMAPINFOHEADER);
    Hdr.biWidth = static_cast<LONG>(G_AtlasMaxW);
    Hdr.biHeight = -static_cast<LONG>(G_AtlasMaxH);
    Hdr.biPlanes = 1;
    Hdr.biBitCount = 16;
    Hdr.biCompression = BI_RGB;
    Hdr.biSizeImage = 0x20000;

    HDC Screen = GetDC(nullptr);
    G_AtlasDc = CreateCompatibleDC(Screen);
    G_AtlasBitmap = CreateCompatibleBitmap(Screen, static_cast<int>(G_AtlasMaxW),
                                           static_cast<int>(G_AtlasMaxH));
    GetDIBits(G_AtlasDc, G_AtlasBitmap, 0, 1, nullptr, G_AtlasBmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, Screen);

    G_AtlasPrevObj = SelectObject(G_AtlasDc, G_AtlasBitmap);
    SetTextAlign(G_AtlasDc, TA_LEFT | TA_TOP);
    SetBkColor(G_AtlasDc, RGB(0, 0, 0));
    SetBkMode(G_AtlasDc, TRANSPARENT);
    SetTextColor(G_AtlasDc, RGB(255, 255, 255));
}

void FontDeviceReset()
{
    // FUN_004180c0 (called from the render-manager init).
    // The layout generation skips 0 on wrap.
    ++G_FontLayoutGeneration;
    if (G_FontLayoutGeneration == 0)
    {
        G_FontLayoutGeneration = 1;
    }
    // FUN_00415050 inlined.
    NumberFaceResetAll();
    FontAtlasReleaseScratch();
    // FUN_00412670.
    FontAtlasEnsureScratch();
}

void FontAtlasReleaseScratch()
{
    // Inlined in FUN_004180c0 and FUN_004174a0.
    std::free(G_AtlasBmi);
    G_AtlasBmi = nullptr;
    SelectObject(G_AtlasDc, G_AtlasPrevObj);
    DeleteObject(G_AtlasBitmap);
    DeleteDC(G_AtlasDc);
}

void FontAtlasReleasePagePool()
{
    // Part of FUN_004174a0. DALib::Pipeline is re-read for every page.
    while (!G_AtlasFreePages.empty())
    {
        FFontAtlasPage* const Page = G_AtlasFreePages.front();
        IRenderPipeline* const Rp = Pipe();
        if (Rp != nullptr)
        {
            Rp->DestroyTexture(Page->Texture);
            G_AtlasTexBytes -= Page->PageH * Page->PageW * 2u;
        }
        delete Page;
        G_AtlasFreePages.erase(G_AtlasFreePages.begin());
    }
}

void FontAtlasFreePages(FFontAtlasPage*& Head)
{
    // FUN_00413e80 (thiscall on the pool DAT_00616640, RET 4). Textures stay alive. The
    // callers clear their grid pointer (+0x14) afterwards -- done here through the reference.
    FFontAtlasPage* Row = Head;
    while (Row != nullptr)
    {
        FFontAtlasPage* const NextRow = Row->NextY;
        FFontAtlasPage* Col = Row;
        while (Col != nullptr)
        {
            FFontAtlasPage* const NextCol = Col->NextX;
            Col->NextY = nullptr;
            Col->NextX = nullptr;
            G_AtlasFreePages.push_back(Col);
            Col = NextCol;
        }
        Row = NextRow;
    }
    Head = nullptr;
}

unsigned FontAtlasMaterialKey()
{
    return G_AtlasMaterialKey;
}

void FontAtlasSetMaterialKey(unsigned Key)
{
    G_AtlasMaterialKey = Key;
}

unsigned FontAtlasDrawColor()
{
    return G_AtlasDrawColor;
}

void FontAtlasSetDrawColor(unsigned PackedArgb)
{
    G_AtlasDrawColor = PackedArgb;
}

unsigned char FontAtlasRasterize(FontStringChild* This, unsigned char bSkipSizeCheck)
{
    // FUN_004127d0 (thiscall on a FontString, RET 4).
    if (This->Font_ == nullptr)
    {
        return 0;
    }

    // GetSize (vt+0x18, LAB_00414a40) may itself send a stale grid back to the pool before
    // +0x14 is read below. The grid is checked and built with the Check extents, the text
    // drawn with the raw ones.
    int Size[2];
    This->GetSize(Size);
    int const RawW = Size[0];
    int const RawH = Size[1];
    unsigned CheckW = static_cast<unsigned>(RawW);
    unsigned CheckH = static_cast<unsigned>(RawH);

    // With a zero argument both extents go to a power of two and must fit the scratch. On a
    // miss retail also sets the write-only byte DAT_00616698 (not kept here).
    if (bSkipSizeCheck == 0)
    {
        CheckW = RoundUpPow2(CheckW);
        CheckH = RoundUpPow2(CheckH);
        if (CheckW > G_AtlasMaxW || CheckH > G_AtlasMaxH)
        {
            return 0;
        }
    }

    // An existing grid is kept when its first row's widths and first column's heights cover
    // the checked extents; with the head's content height equal to the raw height it needs no
    // redraw. A grid that does not cover goes back to the pool (FUN_00413e80 inlined).
    if (This->GlyphHead_ != nullptr)
    {
        unsigned SumW = 0;
        for (FFontAtlasPage* Page = This->GlyphHead_; Page != nullptr; Page = Page->NextX)
        {
            SumW += Page->PageW;
        }
        bool bCovers = SumW >= CheckW;
        if (bCovers)
        {
            unsigned SumH = 0;
            for (FFontAtlasPage* Page = This->GlyphHead_; Page != nullptr; Page = Page->NextY)
            {
                SumH += Page->PageH;
            }
            bCovers = SumH >= CheckH;
        }
        if (bCovers)
        {
            if (This->GlyphHead_->ContentH == static_cast<unsigned>(RawH))
            {
                return 1;
            }
        }
        else
        {
            FontAtlasFreePages(This->GlyphHead_);
        }
    }

    // Build the grid: unsigned clamps, signed loop tests, both limits re-read every step. The
    // pool's result is not tested.
    if (This->GlyphHead_ == nullptr)
    {
        FFontAtlasPage* Head = nullptr;
        FFontAtlasPage* RowTail = nullptr;
        for (int LeftH = static_cast<int>(CheckH); LeftH > 0;
             LeftH -= static_cast<int>(G_AtlasMaxH))
        {
            unsigned TileH = static_cast<unsigned>(LeftH);
            if (TileH >= G_AtlasMaxH)
            {
                TileH = G_AtlasMaxH;
            }
            int const SrcY = static_cast<int>(CheckH) - LeftH;
            FFontAtlasPage* Previous = nullptr;
            for (int LeftW = static_cast<int>(CheckW); LeftW > 0;
                 LeftW -= static_cast<int>(G_AtlasMaxW))
            {
                unsigned TileW = G_AtlasMaxW;
                if (static_cast<unsigned>(LeftW) < TileW)
                {
                    TileW = static_cast<unsigned>(LeftW);
                }
                FFontAtlasPage* const Page = CreateAtlasPage(TileW, TileH);
                if (Previous != nullptr)
                {
                    Previous->NextX = Page;
                }
                else if (Head == nullptr)
                {
                    Head = Page;
                    RowTail = Page;
                }
                else
                {
                    RowTail->NextY = Page;
                    RowTail = Page;
                }
                Page->SrcX = static_cast<int>(CheckW) - LeftW;
                Page->SrcY = SrcY;
                Previous = Page;
            }
        }
        This->GlyphHead_ = Head;
        if (Head == nullptr)
        {
            return 0;
        }
    }

    // IFont vt+0x0c (LAB_00416570) returns the HFONT, selected without a test.
    unsigned char bOk = 1;
    FFontAtlasPage* Row = This->GlyphHead_;
    HFONT const Font = This->Font_->Slot03();
    FAtlasFontSelection const Selection(G_AtlasDc, Font);

    // Row and column counts follow the raw extents alone (no null test on the page links).
    for (int LeftH = RawH; LeftH > 0; LeftH -= static_cast<int>(G_AtlasMaxH))
    {
        unsigned TileH = static_cast<unsigned>(LeftH);
        if (TileH >= G_AtlasMaxH)
        {
            TileH = G_AtlasMaxH;
        }
        FFontAtlasPage* Page = Row;
        for (int LeftW = RawW; LeftW > 0; LeftW -= static_cast<int>(G_AtlasMaxW))
        {
            unsigned TileW = G_AtlasMaxW;
            if (static_cast<unsigned>(LeftW) < TileW)
            {
                TileW = static_cast<unsigned>(LeftW);
            }
            int const SrcX = Page->SrcX;
            int const SrcY = Page->SrcY;
            Page->UsedW = TileW;
            Page->UsedH = TileH;
            RECT Clip;
            Clip.left = 0;
            Clip.top = 0;
            Clip.right = static_cast<LONG>(TileW);
            Clip.bottom = static_cast<LONG>(TileH);

            // ExtTextOutW goes through the unicows thunk FUN_005b7420 in retail.
            UINT const Length =
                This->Text_ != nullptr ? static_cast<UINT>(wcslen(This->Text_)) : 0u;
            if (ExtTextOutW(G_AtlasDc, -SrcX, -SrcY, ETO_OPAQUE, &Clip, This->Text_, Length,
                            nullptr) == FALSE)
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                      "E:\\FL\\Scratch\\Source\\Client\\cmn\\Font.cpp", 0x329,
                      "FontString - ExtTextOutW failed.\n");
                bOk = 0;
            }
            else if (bOk != 0)
            {
                // GetDIBits over the full scratch height.
                SelectObject(G_AtlasDc, G_AtlasPrevObj);
                GetDIBits(G_AtlasDc, G_AtlasBitmap, 0, G_AtlasMaxH, G_AtlasBits, G_AtlasBmi,
                          DIB_RGB_COLORS);
                SelectObject(G_AtlasDc, G_AtlasBitmap);
                unsigned short const* Source = G_AtlasBits;

                // Only out+0x10 is seeded, with FUN_005b7010(0). out+8 = pitch, out+0xc = bits.
                unsigned long LockOut[5];
                LockOut[4] = static_cast<unsigned long>(PixelFormatFromD3D(0));
                IRenderPipeline* const Pipeline =
                    DALib::Pipeline;
                if (Pipeline->LockTexture(Page->Texture, 0, LockOut) != GR_OK)
                {
                    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                          "E:\\FL\\Scratch\\Source\\Client\\cmn\\Font.cpp", 0x34c,
                          "FontString - Could not lock texture.");
                    bOk = 0;
                }
                else
                {
                    // Bit 15 (alpha) set on every non-zero pixel.
                    unsigned long const Pitch = LockOut[2];
                    unsigned char* DestRow = reinterpret_cast<unsigned char*>(LockOut[3]);
                    for (unsigned RowsLeft = TileH; RowsLeft != 0u; --RowsLeft)
                    {
                        unsigned short* const Dest = reinterpret_cast<unsigned short*>(DestRow);
                        for (unsigned Col = 0; Col < TileW; ++Col)
                        {
                            unsigned short const Pixel = Source[Col];
                            Dest[Col] = static_cast<unsigned short>(
                                ((Pixel != 0) ? 0x8000u : 0u) | Pixel);
                        }
                        Source += G_AtlasMaxW;
                        DestRow += Pitch;
                    }
                    // DALib::Pipeline is re-read.
                    DALib::Pipeline
                        ->UnlockTexture(Page->Texture, 0);
                }
            }
            Page = Page->NextX;
        }
        Row = Row->NextY;
    }

    if (bOk != 0)
    {
        This->GlyphHead_->ContentH = static_cast<unsigned>(RawH);
    }
    return bOk;
}
