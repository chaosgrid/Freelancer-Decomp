// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_win_object.h"

#include "Interface/ui_page.h"
#include "Interface/ui_page_desc.h"
#include "Interface/win_factory.h"

#include "x86math.h"

#include <string.h>

UiWinObject::UiWinObject()
    : Width_(0.0f)
    , Height_(0.0f)
    , Depth_(0.0f)
{
    // Inlined into FUN_0059c940: +0xb8 an identity transform. Retail leaves +0xac..+0xb4 and
    // +0xe8..+0xfc unwritten.
    PageBlock_[0] = 1.0f;
    PageBlock_[1] = 0.0f;
    PageBlock_[2] = 0.0f;
    PageBlock_[3] = 0.0f;
    PageBlock_[4] = 1.0f;
    PageBlock_[5] = 0.0f;
    PageBlock_[6] = 0.0f;
    PageBlock_[7] = 0.0f;
    PageBlock_[8] = 1.0f;
    PageBlock_[9] = 0.0f;
    PageBlock_[10] = 0.0f;
    PageBlock_[11] = 0.0f;

    for (int Index = 0; Index < 3; ++Index)
    {
        Origin_[Index] = 0.0f;
        Corner_[Index] = 0.0f;
    }
}

UiWinObject::~UiWinObject()
{
    // FUN_0059cf70.
}

void UiWinObject::SetPosition(float const* NewPosition)
{
    // LAB_0059ab10 (RET 4).
    WinObject::SetPosition(NewPosition);
    ClientMeshHolderUpdate(&MeshHolder);
}

void UiWinObject::SetTransform(Transform const* NewXform)
{
    // LAB_0059a900 (RET 4).
    Transform const& Block = *NewXform;

    // Keep the page placement at +0xb8.
    memcpy(PageBlock_, NewXform, sizeof(PageBlock_));

    Vector const Translation = Block.Translation;

    // The half extent rotated by the block, plus the block translation.
    Vector HalfExtent;
    HalfExtent.x = Width_ * 0.5f;
    HalfExtent.y = Height_ * -0.5f;
    HalfExtent.z = 0.0f;
    Vector Rotated;
    UiWidgetMathEngine()->TransformMatrix(Rotated, Block.Rotation, HalfExtent);
    Vector Center;
    Center.x = Rotated.x + Translation.x;
    Center.y = Rotated.y + Translation.y;
    Center.z = Rotated.z + Translation.z;

    // x87: the quotient stays double for both products.
    double const Scale = static_cast<double>(Depth_) / static_cast<double>(Translation.z);
    float Position[3];
    Position[0] = static_cast<float>(static_cast<double>(Center.x) * Scale);
    Position[1] = static_cast<float>(static_cast<double>(Center.y) * Scale);
    Position[2] = Depth_;

    Vector Zero;
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    Vector OriginOut;
    UiWidgetMathEngine()->TransformXform(OriginOut, Block, Zero);
    Origin_[0] = OriginOut.x;
    Origin_[1] = OriginOut.y;
    Origin_[2] = OriginOut.z;

    Vector CornerIn;
    CornerIn.x = Width_;
    CornerIn.y = -Height_;
    CornerIn.z = 0.0f;
    Vector CornerOut;
    UiWidgetMathEngine()->TransformXform(CornerOut, Block, CornerIn);
    Corner_[0] = CornerOut.x;
    Corner_[1] = CornerOut.y;
    Corner_[2] = CornerOut.z;

    // Both through the vftable, so SetPosition is this class's override.
    float Identity[9];
    Identity[0] = 1.0f;
    Identity[1] = 0.0f;
    Identity[2] = 0.0f;
    Identity[3] = 0.0f;
    Identity[4] = 1.0f;
    Identity[5] = 0.0f;
    Identity[6] = 0.0f;
    Identity[7] = 0.0f;
    Identity[8] = 1.0f;
    SetOrientation(reinterpret_cast<Matrix const*>(Identity));
    SetPosition(Position);
}

bool UiWinObject::Create(Win3D* NewParent, void* Payload)
{
    // FUN_0059a760 (RET 8).
    if (Payload == nullptr)
        return false;

    FUiPayloadWinObject const* ObjPayload = static_cast<FUiPayloadWinObject const*>(Payload);
    Width_ = ObjPayload->Right;
    Height_ = ObjPayload->Bottom;
    Depth_ = ObjPayload->ExtraFloat;

    // Retail goes through a std::string copy; the loader always stores a non-null path.
    char const* MeshPath = ObjPayload->ExtraName;

    if (!ClientMeshLoadFromPath(&MeshHolder, MeshPath, 0, false))
        return false;

    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);
    return true;
}

// DAT_0067dac0.
WINFACTORY<UiWinObject> G_UIWinObjectFactory("UIWinObject");
