// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Rect and the Rect / ViewRect / RECT conversions.

#include "common_text.h"

Rect::Rect()
{
    Left = 0;
    Top = 0;
    Width = 0;
    Height = 0;
}

Rect::Rect(int InLeft, int InTop, int InWidth, int InHeight)
{
    Left = InLeft;
    Top = InTop;
    Width = InWidth;
    Height = InHeight;
}

void Rect::clear()
{
    Left = 0;
    Top = 0;
    Width = 0;
    Height = 0;
}

void Rect::init(int InLeft, int InTop, int InWidth, int InHeight)
{
    Left = InLeft;
    Top = InTop;
    Width = InWidth;
    Height = InHeight;
}

void Rect::init_from_ltrb(int InLeft, int InTop, int InRight, int InBottom)
{
    // RVA 0x81590
    Left = InLeft;
    Top = InTop;
    Width = (InRight - InLeft) + 1;
    Height = (InBottom - InTop) + 1;
}

int Rect::rightmost() const
{
    // RVA 0x815C0
    return Left + Width - 1;
}

int Rect::bottommost() const
{
    // RVA 0x815D0
    return Top + Height - 1;
}

int Rect::area() const
{
    // RVA 0x815E0
    return Width * Height;
}

bool Rect::null() const
{
    // RVA 0x815F0
    if (Width != 0 && Height != 0)
    {
        return false;
    }
    return true;
}

bool Rect::nonnull() const
{
    // RVA 0x81610
    if (Width > 0 && Height > 0)
    {
        return true;
    }
    return false;
}

int Rect::leftmost() const
{
    // RVA 0x01D90
    return Left;
}

int Rect::topmost() const
{
    // RVA 0x71AD0
    return Top;
}

int Rect::width() const
{
    // RVA 0x466A0
    return Width;
}

int Rect::height() const
{
    // RVA 0x3FB90
    return Height;
}

void Rect::get_center(int* OutX, int* OutY) const
{
    // RVA 0xBE2A0. Truncating signed division of (2*Left + Width - 1), NOT Left + Width / 2:
    // they differ by 1 for even sizes.
    if (OutX)
    {
        *OutX = (2 * Left + Width - 1) / 2;
    }
    if (OutY)
    {
        *OutY = (2 * Top + Height - 1) / 2;
    }
}

bool Rect::point_is_inside(int X, int Y) const
{
    // RVA 0xBE2E0
    if (X >= Left && X < Left + Width && Y >= Top && Y < Top + Height)
    {
        return true;
    }
    return false;
}

bool operator==(Rect const& A, Rect const& B)
{
    // RVA 0xBE320. Two null rects compare equal regardless of their other fields.
    bool const FieldsEqual = (A.Left == B.Left) && (A.Top == B.Top) &&
                              (A.Width == B.Width) && (A.Height == B.Height);
    bool const BothNull = A.null() && B.null();
    return FieldsEqual || BothNull;
}

bool operator!=(Rect const& A, Rect const& B)
{
    // RVA 0xBE390. Not !(A==B): the "non-empty" test here is > 0 where operator== uses != 0,
    // which differs for a negative Width or Height.
    bool const FieldsDiffer = (A.Left != B.Left) || (A.Top != B.Top) ||
                               (A.Width != B.Width) || (A.Height != B.Height);
    bool const ANonEmpty = (A.Width > 0) && (A.Height > 0);
    bool const BNonEmpty = (B.Width > 0) && (B.Height > 0);
    return FieldsDiffer && (ANonEmpty || BNonEmpty);
}

ViewRect __cdecl Rect_to_ViewRect(Rect const& In)
{
    // 0631e5f0. Right/Bottom are inclusive.
    ViewRect Out;
    Out.Left = In.Left;
    Out.Top = In.Top;
    Out.Right = In.Width + In.Left - 1;
    Out.Bottom = In.Height + In.Top - 1;
    return Out;
}

Rect __cdecl ViewRect_to_Rect(ViewRect const& In)
{
    // 0631e620, the exact inverse.
    Rect Out;
    Out.Left = In.Left;
    Out.Top = In.Top;
    Out.Width = In.Right - In.Left + 1;
    Out.Height = In.Bottom - In.Top + 1;
    return Out;
}

tagRECT __cdecl Rect_to_RECT(Rect const& In)
{
    // 0631e580. Retail: -2, not the -1 of Rect_to_ViewRect.
    tagRECT Out;
    Out.left = In.Left;
    Out.top = In.Top;
    Out.right = In.Left + In.Width - 2;
    Out.bottom = In.Top + In.Height - 2;
    return Out;
}

Rect __cdecl RECT_to_Rect(tagRECT const& In)
{
    // 0631e5b0. Retail: +2, the inverse of Rect_to_RECT.
    Rect Out;
    Out.Left = In.left;
    Out.Top = In.top;
    Out.Width = In.right - In.left + 2;
    Out.Height = In.bottom - In.top + 2;
    return Out;
}

COMMON_API Rect __cdecl rect_intersect(Rect const& A, Rect const& B)
{
    int ALeft = A.Left;
    int BLeft = B.Left;
    int ARight = ALeft - 1 + A.Width;
    int BRight = BLeft - 1 + B.Width;

    int OutLeft = (ALeft >= BLeft) ? ALeft : BLeft;
    int OutRight = (ARight <= BRight) ? ARight : BRight;

    if (OutLeft <= OutRight)
    {
        int ATop = A.Top;
        int BTop = B.Top;
        int ABottom = ATop - 1 + A.Height;
        int BBottom = BTop - 1 + B.Height;

        int OutTop = (ATop >= BTop) ? ATop : BTop;
        int OutBottom = (ABottom <= BBottom) ? ABottom : BBottom;

        if (OutTop <= OutBottom)
        {
            Rect Out;
            Out.Left = OutLeft;
            Out.Top = OutTop;
            Out.Width = OutRight - OutLeft + 1;
            Out.Height = OutBottom - OutTop + 1;
            return Out;
        }
    }

    Rect Out;
    Out.Left = 0;
    Out.Top = 0;
    Out.Width = 0;
    Out.Height = 0;
    return Out;
}

COMMON_API Rect __cdecl rect_union(Rect const& A, Rect const& B)
{
    if (A.Width == 0 || A.Height == 0)
    {
        return B;
    }

    if (B.Width != 0 && B.Height != 0)
    {
        int ALeft = A.Left;
        int ARight = ALeft - 1 + A.Width;
        int ATop = A.Top;
        int ABottom = ATop - 1 + A.Height;

        int BLeft = B.Left;
        int BRight = BLeft - 1 + B.Width;
        int BTop = B.Top;
        int BBottom = BTop - 1 + B.Height;

        int OutLeft = (ALeft < BLeft) ? ALeft : BLeft;
        int OutRight = (ARight > BRight) ? ARight : BRight;
        int OutTop = (ATop < BTop) ? ATop : BTop;
        int OutBottom = (ABottom > BBottom) ? ABottom : BBottom;

        Rect Out;
        Out.Left = OutLeft;
        Out.Top = OutTop;
        Out.Width = OutRight - OutLeft + 1;
        Out.Height = OutBottom - OutTop + 1;
        return Out;
    }

    return A;
}
