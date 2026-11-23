// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/client_ime_focus.h"

#include "Common/Fonts/font_atlas.h"
#include "Common/Fonts/font_imp.h"
#include "Common/Fonts/font_string.h"
#include "Core/globals.h"
#include "Common/Graphics/screen_quad_state.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_renderables.h"   // ImeFocusDrawPass

#include "DALib.h"
#include "dacom.h"
#include "render/IRenderPipeline.h"

#include <cstring>
#include <new>

namespace
{
    IFont* G_ConfirmPlayerFont = nullptr; // DAT_0067a4a0
    // DAT_0067a4a4.
    ClientIMEFocus* G_ClientImeFocus = nullptr;
    // DAT_0067a480 / DAT_0067a484 -- set by static initializers LAB_00563ad0 / LAB_00563ab0.
    unsigned long G_ImeBackgroundKey = DACOM_CRC::GetCRC32("IMEbkground");
    unsigned long G_ImeTextKey = DACOM_CRC::GetCRC32("IMEtext");

    // FUN_00417790 (cdecl) -- swaps DAT_00616670; no null test on OutPrev.
    void SwapFontAtlasMaterialKey(unsigned* OutPrev, unsigned NewKey)
    {
        unsigned const Prev = FontAtlasMaterialKey();
        FontAtlasSetMaterialKey(NewKey);
        *OutPrev = Prev;
    }

    // DrawTextW's AARRGGBB -> the byte order FontAtlasSetDrawColor takes.
    unsigned PackDrawColorBytes(unsigned Color)
    {
        unsigned char Bytes[4];
        Bytes[0] = static_cast<unsigned char>((Color >> 16) & 0xffu);
        Bytes[1] = static_cast<unsigned char>((Color >> 8) & 0xffu);
        Bytes[2] = static_cast<unsigned char>(Color & 0xffu);
        Bytes[3] = static_cast<unsigned char>((Color >> 24) & 0xffu);
        unsigned Packed = 0;
        Packed |= static_cast<unsigned>(Bytes[0]);
        Packed |= static_cast<unsigned>(Bytes[1]) << 8;
        Packed |= static_cast<unsigned>(Bytes[2]) << 16;
        Packed |= static_cast<unsigned>(Bytes[3]) << 24;
        return Packed;
    }
}

IFont* ClientConfirmPlayerFont()
{
    if (G_ConfirmPlayerFont == nullptr)
        G_ConfirmPlayerFont = FindMenuFontImp("ConfirmPlayerText");
    return G_ConfirmPlayerFont;
}

unsigned long ClientImeBackgroundKey()
{
    return G_ImeBackgroundKey;
}

unsigned long ClientImeTextKey()
{
    return G_ImeTextKey;
}

void ClientImeSwapFontMaterialKey(unsigned* OutPrev, unsigned NewKey)
{
    SwapFontAtlasMaterialKey(OutPrev, NewKey);
}

void ImeFocusDrawPass()
{
    // FUN_00563af0 (cdecl). Sole caller: the tail of Win3D's draw-all.
    IMEFocus* const Focus = IMEFocus::GetSingleton();
    if (Focus == nullptr)
        return;
    if (Focus->NumInputBoxes() == 0)
        return;

    // Retail's glMatrixMode(0x1701) here is an empty stub.
    Win3DPushProjection();
    Win3DSetOrtho2D();

    int SavedStage0Type10 = 0;
    int SavedStage0Type11 = 0;
    DALib::Pipeline->GetTextureStageState(0, 0x10, &SavedStage0Type10);
    DALib::Pipeline->GetTextureStageState(0, 0x11, &SavedStage0Type11);
    DALib::Pipeline->SetTextureStageState(0, 0x10, 1);
    DALib::Pipeline->SetTextureStageState(0, 0x11, 1);

    FScreenQuadRenderStateSaver const SaverA(DALib::Pipeline, 0x17, 8);
    FScreenQuadRenderStateSaver const SaverB(DALib::Pipeline, 0xe, 0);

    Win3DDisable(kWin3DCullFace);

    Focus->Draw(nullptr);

    ScreenQuadFlush(DALib::Pipeline, G_ImeBackgroundKey);
    ScreenQuadFlush(DALib::Pipeline, G_ImeTextKey);

    DALib::Pipeline->SetTextureStageState(0, 0x10, SavedStage0Type10);
    DALib::Pipeline->SetTextureStageState(0, 0x11, SavedStage0Type11);

    Win3DEnable(kWin3DCullFace);

    // The savers restore 0xe, then 0x17, after the projection pop.
    Win3DPopProjection();
}

ClientIMEFocus::ClientIMEFocus(HWND Hwnd)
    : IMEFocus(Hwnd)
    , ConfirmText(nullptr)
{
    // Inlined into FUN_00564060. Retail tests neither the new object nor the font.
    ConfirmText = new FontStringChild();
    ConfirmText->BindFont(ClientConfirmPlayerFont());
    ConfirmText->SetShadow(0);
}

ClientIMEFocus::~ClientIMEFocus()
{
    // FUN_00564170.
    if (ConfirmText != nullptr)
    {
        ConfirmText->Release();
        ConfirmText = nullptr;
    }
}

void ClientIMEFocus::DrawRect(int X1, int Y1, int X2, int Y2, DWORD Color)
{
    // LAB_00563e70 (RET 0x14; `this` is not read). +0x9c of the focused box is FLImeInputBox's
    // FontString; the x extents move left by its text width.
    IMEInputBox* Box = GetFocus();
    FontStringChild* SizeProbe =
        *reinterpret_cast<FontStringChild**>(reinterpret_cast<char*>(Box) + 0x9c);
    int SizeWH[2];
    SizeProbe->GetSize(SizeWH);
    int const Left = X1 - SizeWH[0];
    int const Right = X2 - SizeWH[0];

    ScreenQuadSetMaterialKey(G_ImeBackgroundKey);

    float Fx0 = static_cast<float>(Left);
    float Fy0 = static_cast<float>(Y1);
    float Fx1 = static_cast<float>(Right);
    float Fy1 = static_cast<float>(Y2);
    float U0 = 0.0f;
    float V0 = 0.0f;
    float U1 = 1.0f;
    float V1 = 1.0f;

    // Both spans are computed before the x clip.
    float const SpanX = static_cast<float>(static_cast<double>(Fx1) - Fx0 + 1.0);
    float const SpanY = static_cast<float>(static_cast<double>(Fy1) - Fy0 + 1.0);
    if (ScreenQuadClip1D(&Fx0, &Fx1, &U0, &U1, 0.0f,
                         static_cast<float>(static_cast<int>(G_RenderSurfaceWidth) - 1),
                         SpanX) == 0)
        return;
    if (ScreenQuadClip1D(&Fy0, &Fy1, &V0, &V1, 0.0f,
                         static_cast<float>(static_cast<int>(G_RenderSurfaceHeight) - 1),
                         SpanY) == 0)
        return;

    ScreenQuadBeginBatchFrame(G_ScreenQuadDefaultFrame, 0, true);
    FScreenQuadRecord Record;
    Record.X0 = Fx0;
    Record.Y0 = Fy0;
    Record.X1 = Fx1;
    Record.Y1 = Fy1;
    Record.DepthA = 1.0f;
    Record.DepthB = 1.0f;
    Record.U0 = U0;
    Record.V0 = V0;
    Record.U1 = U1;
    Record.V1 = V1;
    Record.ColorBits = Color;
    ScreenQuadAppendRecord(Record);
}

void ClientIMEFocus::DrawTextW(const wchar_t* Text, unsigned Color, void* Layout,
                               unsigned Count)
{
    // LAB_00563cf0 -- a triangle fan over the 0x20-byte layout entries, then the text centred on
    // the first entry.
    float* const Entries = static_cast<float*>(Layout);

    Win3DBegin(kWin3DTriangleFan);
    Win3DVertex3f(Entries[0], Entries[1], 0.0f);

    if (Count > 1u)
    {
        unsigned char* const Base = static_cast<unsigned char*>(Layout);
        unsigned char* Esi = Base + 0x32;
        for (unsigned Edi = 1u; Edi < Count; ++Edi)
        {
            // The entry's AARRGGBB dword at +0x10.
            Win3DColor3ub(Esi[0], Esi[-1], Esi[-2]);

            float* const Cur = reinterpret_cast<float*>(Esi - 0x12);
            Win3DVertex3f(Cur[0], Cur[1], 0.0f);

            unsigned NextIdx = (Edi == Count - 1u) ? 1u : (Edi + 1u);
            float* const Next = reinterpret_cast<float*>(Base + (NextIdx << 5));
            Win3DVertex3f(Next[0], Next[1], 0.0f);

            Esi += 0x20;
        }
    }

    Win3DEnd();

    unsigned PrevMat = 0;
    SwapFontAtlasMaterialKey(&PrevMat, static_cast<unsigned>(G_ImeTextKey));

    int PosX = static_cast<int>(Entries[0]);
    int PosY = static_cast<int>(Entries[1]);
    FontAtlasSetDrawColor(PackDrawColorBytes(Color));

    ConfirmText->SetText(Text);
    int SizeWH[2];
    ConfirmText->GetSize(SizeWH);

    // Retail converts the origin as unsigned, so a negative result becomes a large positive
    // float. Paint reads only x and y.
    unsigned const OriginXBits =
        static_cast<unsigned>(PosX) - (static_cast<unsigned>(SizeWH[0]) >> 1);
    unsigned const OriginYBits =
        static_cast<unsigned>(PosY) - (static_cast<unsigned>(SizeWH[1]) >> 1);
    FFontStringDrawOrigin Origin;
    Origin.X = static_cast<float>(static_cast<double>(OriginXBits));
    Origin.Y = static_cast<float>(static_cast<double>(OriginYBits));
    Origin.Scale = 0.0f;
    ConfirmText->Paint(&Origin, nullptr, nullptr);

    unsigned Discard = 0;
    SwapFontAtlasMaterialKey(&Discard, PrevMat);
}

IMEFocus* EnsureClientIMEFocus()
{
    // On a failed load the current singleton is returned. The handle is never freed (the exe
    // also imports the DLL).
    if (LoadLibraryA("ImeUi.dll") == nullptr)
        return G_ClientImeFocus;

    ClientIMEFocus* Focus = new ClientIMEFocus(g_client.window);
    // Stored before the check.
    G_ClientImeFocus = Focus;

    // IMEFocus byte +0x17.
    if (!Focus->IsImeAvailable())
    {
        delete Focus;
        G_ClientImeFocus = nullptr;
        return nullptr;
    }
    return Focus;
}

void ReleaseClientIMEFocus()
{
    // FUN_005641d0.
    if (G_ClientImeFocus != nullptr)
        delete G_ClientImeFocus;
    G_ClientImeFocus = nullptr;
}
