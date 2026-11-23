// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Intro.cpp Image (`E:\FL\Scratch\Source\Client\Interface\Intro.cpp`): a full-screen
// picture held in an ximage.dll BigImage. Three users:
//   - InitializeEngine's startup screen, a stack local;
//   - the loading screen DAT_0067c3b8 (+0x04, built by FUN_00579220 through FUN_00565d90);
//   - IntroFocus +0x210, the menu logo (dtor FUN_005737c0).

struct IBigImage2;

// RTTI .?AUIImage@@, vftable 0x005e2708: {LAB_00565090 deleting dtor, _purecall, _purecall}.
struct IImage
{
    // FUN_00565080 (body) / LAB_00565090 (deleting): only the vptr store.
    virtual ~IImage();
    // slot 1
    virtual bool Load(char const* Path) = 0;
    // slot 2
    virtual void Draw() = 0;
};

// RTTI .?AUImage@@, vftable 0x005e2718: {FUN_005650b0, FUN_00565430, FUN_00565a10}. 0x18 bytes
// (operator new(0x18) in FUN_00565d90).
struct Image : public IImage
{
    // Inlined at every construction. +0x04..+0x13 are not written.
    Image();
    // FUN_00565010 (FUN_005650b0 deleting).
    ~Image() override;

    // FUN_00565430 (RET 4): by the extension after the last '.', "bmp" (DACOM stricmp) goes to
    // LoadBmp; everything else, including no extension, to LoadTga.
    bool Load(char const* Path) override;
    // FUN_00565a10: nothing without a BigImage; otherwise the 2D ortho set-up, seven saved
    // render states, two texture stages, and the BigImage drawn over the whole surface.
    void Draw() override;

    // FUN_005650d0: release the BigImage and clear +0x14.
    void Release();
    // FUN_005650f0 (RET 4): LoadImageA the DIB <data path>[\]<TgaPath><Path> into a BigImage.
    bool LoadBmp(char const* Path);
    // FUN_00565490 (RET 4): map the TGA through a "FileSystem" rooted at TgaPath and build a
    // BigImage from the mapped pixels (32-bit formats only).
    bool LoadTga(char const* Path);

    // +0x04..+0x13 -- nothing in Image's functions or at the three embedding sites reads or
    // writes them (padding of a base or dead fields).
    unsigned char Field_0x04[0x10];
    IBigImage2* BigImage; // +0x14

private:
    Image(Image const&) = delete;
    Image& operator=(Image const&) = delete;
};

static_assert(sizeof(void*) != 4 || sizeof(Image) == 0x18, "retail Image is 0x18 bytes");

// FUN_00565d90 (cdecl): operator new(0x18). Caller FUN_00579220.
Image* CreateImage();

#ifdef FREELANCER_WIDESCREEN_SUPPORT
// Port-only: where Image::Draw puts its 4:3 picture on a wider surface until told otherwise --
// -1 against the left edge, +1 against the right, 0 centred (the default).
void ImageSetWidescreenAlign(int Side);
#endif
