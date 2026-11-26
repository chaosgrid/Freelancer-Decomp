// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_blocking_dialog.h"

#include "Core/globals.h"            // PushClientTimingPause / PopClientTimingPause
#include "Common/input_manager.h"      // MouseSetCursor (FUN_0041dde0)
#include "NeuroNet/nn_modal.h"
#include "Space/space_letterbox.h"    // IsLetterboxVisible (FUN_0041a3e0)
#include "Interface/win3d.h"
#include "Interface/win3d_renderables.h"  // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "Interface/win_factory.h"

// Retail DAT_0066db9c -- WINFACTORY<BlockingDialog> static (LAB_00472150).
WINFACTORY<BlockingDialog> G_BlockingDialogFactory("BlockingDialog");

BlockingDialog::BlockingDialog()
    : bCursorDrawFlag(1)
{
    // Inlined in FUN_004747c0.
    PushClientTimingPause();                      // FUN_0042d6c0
    G_ModalCaptureCount = G_ModalCaptureCount + 1; // DAT_0067dccc
}

BlockingDialog::~BlockingDialog()
{
    // FUN_00474420.
    G_ModalCaptureCount = G_ModalCaptureCount - 1;
    Win3DSetKeyFocus(nullptr);
    PopClientTimingPause();
}

bool BlockingDialog::Create(Win3D* NewParent, void* Data)
{
    // FUN_00474490.
    MouseSetCursor("Arrow", false); // FUN_0041dde0

    DrawLayer = 0x64;
    DrawSubLayer = 6;
    bCursorDrawFlag = NnModalPauseGame();
    Win3DSetKeyFocus(this);
    HalfExtents.x = 2.0f;
    HalfExtents.y = 2.0f;
    HalfExtents.z = 0.0f;

    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

void BlockingDialog::Draw()
{
    // FUN_00488960.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // FUN_005a0760
}

void BlockingDialogOpen()
{
    // FUN_00474520.
    if (Win3DFindByClassName("BlockingDialog") != nullptr)
        return;
    Win3D* const Created =
        WinFactoryCreate("BlockingDialog", "BlockingDialog", nullptr, nullptr);
    if (Created == nullptr)
        return;
    BlockingDialog* const Dialog = static_cast<BlockingDialog*>(Created);
    Vector const Zero = {0.0f, 0.0f, 0.0f};
    Dialog->SetProperty(kWinPropPlaceAtOffset, reinterpret_cast<uintptr_t>(&Zero), 0);
    Dialog->CaptureMouse();
}

void BlockingDialogClose()
{
    // FUN_00474590.
    Win3D* const Found = Win3DFindByClassName("BlockingDialog");
    if (Found == nullptr)
        return;
    BlockingDialog* const Dialog = static_cast<BlockingDialog*>(Found);
    if (IsLetterboxVisible())
        Dialog->bCursorDrawFlag = 0;
    NnModalResumeGame(Dialog->bCursorDrawFlag);
    Dialog->ReleaseMouse();
    Win3D* Child = Dialog->ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Dialog->Destroy();
    delete Dialog;
}
