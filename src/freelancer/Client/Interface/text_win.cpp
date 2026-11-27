// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/text_win.h"

#include "common_misc.h"
#include "Common/Fonts/font_atlas.h"
#include "Common/Fonts/font_imp.h"
#include "Core/game_options.h"
#include "NeuroNet/nn_ui_pulse.h"
#include "Space/Cameras/space_camera.h"

#include <math.h>
#include <string.h>

TextWin::TextWin()
{
    // Inlined into every derived ctor in retail.
    ColorIdle = 0;
    ColorPulse = 0;
    ColorD4 = 0;
    BackgroundColor = 0;
    ColorInactive = 0;
}

TextWin::~TextWin()
{
    // FUN_005a1af0: the text cleared and the Win3D Destroy body inlined.
    Text.ClearContents();
    Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Destroy();
    Win3DUnregister(this);
}

bool TextWin::HitTest(Vector const* Ray)
{
    // FUN_004b51d0: the ray's point at this object's depth must sit inside [x, x + width] and
    // [y - height, y]. The y product is rounded to float; the x difference stays double.
    double const T = static_cast<double>(Position.z) / Ray->z;
    float const PointY = static_cast<float>(T * Ray->y);
    double const DeltaX = T * Ray->x - Position.x;
    if (DeltaX < 0.0)
        return false;
    if (!(DeltaX <= Width))
        return false;
    double const DeltaY = static_cast<double>(Position.y) - PointY;
    if (DeltaY < 0.0)
        return false;
    if (!(DeltaY <= Height))
        return false;
    return true;
}

bool TextWin::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_004b5180 (RET 8).
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & ~kWin3DUnregistered) | kWin3DActive | kWin3DVisible);
    InitTextDefaults();
    Width = 0.1f;
    Height = 0.1f;
    return true;
}

void TextWin::Destroy()
{
    // FUN_005a1c10.
    Text.ClearContents();
    Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Destroy();
    Win3DUnregister(this);
}

void TextWin::Draw()
{
    // FUN_005a1d40.
    if (GetGameOption(kGameOptInterfaceGroup) == 0)
        return;
    if ((Flags & kWin3DVisible) == 0)
        return;

    Bounds[3] = -Height;
    Bounds[0] = 0.0f;
    Bounds[2] = Width;
    Bounds[1] = 0.0f;

    // Under the mouse the pulse built around ColorPulse; otherwise the idle colour, or the
    // inactive colour when +0xe6 is clear. The pulse is computed either way.
    bool const bHover = Win3DGetHover() == this;
    unsigned long Pulsed = 0;
    NnUiPulseBlendFromArgb(&Pulsed, ColorPulse);
    unsigned long const* Color = &ColorInactive;
    if (bEnabled != 0)
        Color = bHover ? &Pulsed : &ColorIdle;
    FontAtlasSetDrawColor(static_cast<unsigned>(*Color));

    float ScreenWidth = 0.0f;
    float ScreenHeight = 0.0f;
    TextWinProjectExtent(Width, Height, Position.z, &ScreenWidth, &ScreenHeight);
    float ScreenX = 0.0f;
    float ScreenY = 0.0f;
    TextWinProjectPoint(&Position, &ScreenX, &ScreenY);

    FFontStringDrawOrigin Origin;
    Origin.X = ScreenX;
    Origin.Y = ScreenY;
    Origin.Scale = ScreenWidth;
    Origin.Height = ScreenHeight;
    Origin.HAlign = HAlign;
    Origin.VAlign = 0;
    Origin.DrawChildren(&Text, nullptr, nullptr);
}

void TextWin::SetText(wchar_t const* NewText)
{
    // LAB_005a1cb0.
    Text.SetText(NewText);
}

void TextWin::InitTextDefaults()
{
    // FUN_005a1c50.
    FlagE0 = 0;
    FlagE3 = 0;
    bDrawBackground = 0;
    FlagE2 = 1;
    bEnabled = 1;
    HAlign = 0;
    TextScale = 0.002f; // DAT_005e5f88
    ColorIdle = G_NnPaletteCyan;
    ColorPulse = G_NnPaletteAmber;
    ColorInactive = G_NnPaletteGrey;
}

void TextWin::SetFont(char const* FontName, float Scale)
{
    // FUN_005a1cc0.
    IFont* const Font = FindMenuFontImp(FontName);
    if (Text.FontIface != Font)
    {
        // The call does not look at the child slot's kind byte: a FontString child binds the
        // font (LAB_004148b0), a NumberString child takes it as its face key (FUN_00414fb0);
        // both are slot +8.
        Text.FontIface = Font;
        for (unsigned Index = 0;
             Text.ChildBegin != nullptr &&
             Index < static_cast<unsigned>(Text.ChildEnd - Text.ChildBegin);
             ++Index)
        {
            Text.ChildBegin[Index].Child->BindFont(Text.FontIface);
        }
        Text.LayoutGen = 0;
    }
    TextScale = Scale;
}

void TextWinProjectExtent(float Width, float Height, float Z, float* OutWidth,
                          float* OutHeight)
{
    // FUN_004176c0. A depth closer than 0.001 (DAT_005c75f8) is taken as 1.0.
    FSpaceCamera const* const Cam = Win3DGetCamera();
    double Depth = fabs(static_cast<double>(Z));
    if (Depth < 0.001)
        Depth = 1.0;
    double const NearOverDepth = static_cast<double>(Cam->ZNear) / Depth;
    double const InvNear = 1.0 / static_cast<double>(Cam->ZNear);
    *OutWidth = static_cast<float>(static_cast<double>(Cam->XScale) * InvNear * NearOverDepth *
                                   Width);
    *OutHeight = static_cast<float>(-(InvNear * Cam->YScale * NearOverDepth * Height));
}

void TextWinProjectPoint(Vector const* World, float* OutX, float* OutY)
{
    // FUN_00417730.
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    if (AsCamera(Win3DGetCamera())->Project(&X, &Y, &Z, World) == 0)
    {
        X = 0.0f;
        Y = 0.0f;
    }
    *OutX = X;
    *OutY = Y;
}
