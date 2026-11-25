// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_page.h"

#include "Core/globals.h"
#include "Space/Cameras/space_camera.h"
#include "Interface/win3d_engine_submit.h"
#include "Interface/win_factory.h"

#include "common_csys.h"

#include <math.h>
#include <string.h>
#include <utility>

namespace
{
    // DAT_0067d96c -- Instantiate's own lazily acquired 3DMathEngine.
    I3DMathEngine* G_UiPageMathEngine = nullptr;

    // .data 0x006155ec (-1e-08f), added to the depth of the top-left unproject in
    // MasterWin::Create.
    unsigned const kMasterWinDepthBiasBits = 0xb22bcc77u;

    float FloatFromBits(unsigned Bits)
    {
        float Value;
        memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }
}

namespace
{
    // DAT_0067dbc0.
    I3DMathEngine* G_UiWidgetMathEngine = nullptr;
}

I3DMathEngine* UiWidgetMathEngine()
{
    if (G_UiWidgetMathEngine == nullptr)
        G_UiWidgetMathEngine = Acquire3DMathEngine();
    return G_UiWidgetMathEngine;
}

void UiWidgetProjectExtent(Matrix const* Mat, float Z, float Width, float Height,
                           float* OutWidth, float* OutHeight)
{
    Matrix const& M = *Mat;

    Vector Source;
    Vector WidthPoint;
    Vector Origin;

    Source.x = Width;
    Source.y = 0.0f;
    Source.z = Z;
    UiWidgetMathEngine()->TransformMatrix(WidthPoint, M, Source);
    Source.x = 0.0f;
    Source.y = 0.0f;
    Source.z = Z;
    UiWidgetMathEngine()->TransformMatrix(Origin, M, Source);
    *OutWidth = fabsf(WidthPoint.x - Origin.x);

    Vector HeightPoint;
    Source.x = 0.0f;
    Source.y = Height;
    Source.z = Z;
    UiWidgetMathEngine()->TransformMatrix(HeightPoint, M, Source);
    Source.x = 0.0f;
    Source.y = 0.0f;
    Source.z = Z;
    UiWidgetMathEngine()->TransformMatrix(Origin, M, Source);
    *OutHeight = fabsf(HeightPoint.y - Origin.y);
}

namespace UIPage
{
    Page::Page()
        : Aspect(1.0f)
    {
        // Inlined into FUN_00594590.
    }

    Page::~Page()
    {
        // FUN_00591510.
        for (std::map<std::string, Win3D*>::iterator It = Widgets.begin();
             It != Widgets.end(); ++It)
        {
            if (It->second != nullptr)
                delete It->second;
        }
        Widgets.clear();
    }

    unsigned Page::GetCount()
    {
        // LAB_00415070.
        return static_cast<unsigned>(Widgets.size());
    }

    char const* Page::GetWidgetName(unsigned Index)
    {
        // FUN_00591900.
        std::map<std::string, Win3D*>::iterator It = Widgets.begin();
        while (It != Widgets.end())
        {
            unsigned const Remaining = Index;
            --Index;
            if (Remaining == 0)
                break;
            ++It;
        }
        if (It == Widgets.end())
            return nullptr;
        return It->first.c_str();
    }

    Win3D* Page::GetWidget(unsigned Index)
    {
        // FUN_00591980.
        std::map<std::string, Win3D*>::iterator It = Widgets.begin();
        while (It != Widgets.end())
        {
            unsigned const Remaining = Index;
            --Index;
            if (Remaining == 0)
                break;
            ++It;
        }
        if (It == Widgets.end())
            return nullptr;
        return It->second;
    }

    Win3D* Page::FindWidget(char const* Name)
    {
        // FUN_005919f0.
        std::map<std::string, Win3D*>::iterator It = Widgets.find(std::string(Name));
        if (It == Widgets.end())
            return nullptr;
        return It->second;
    }

    bool Page::Instantiate(FUiWidgetEntry** Entries, Win3D* Parent,
                           Transform const* Mat)
    {
        // FUN_005916b0 (thiscall, RET 0xc).
        bool bResult = true;
        if (Entries == nullptr || *Entries == nullptr)
            return bResult;

        for (FUiWidgetEntry** Cursor = Entries; *Cursor != nullptr; ++Cursor)
        {
            FUiWidgetEntry* Entry = *Cursor;

            Win3D* Widget =
                WinFactoryCreate(Entry->InstanceName, Entry->TypeName, Parent, Entry->Payload);
            if (Widget == nullptr)
                return false;

            // Local = identity rotation, translation (Left, Top, 0).
            Transform Local;
            Local.Rotation.E[0][0] = 1.0f;
            Local.Rotation.E[0][1] = 0.0f;
            Local.Rotation.E[0][2] = 0.0f;
            Local.Rotation.E[1][0] = 0.0f;
            Local.Rotation.E[1][1] = 1.0f;
            Local.Rotation.E[1][2] = 0.0f;
            Local.Rotation.E[2][0] = 0.0f;
            Local.Rotation.E[2][1] = 0.0f;
            Local.Rotation.E[2][2] = 1.0f;
            Local.Translation.x = Entry->Left;
            Local.Translation.y = Entry->Top;
            Local.Translation.z = 0.0f;

            if (G_UiPageMathEngine == nullptr)
                G_UiPageMathEngine = Acquire3DMathEngine();

            Transform Placed;
            G_UiPageMathEngine->MulTransform(Placed, *Mat, Local);

            // SetTransform gets a copy, as retail.
            Transform PlacedCopy = Placed;
            Widget->SetTransform(&PlacedCopy);

            // A duplicate name overwrites the earlier entry's pointer.
            std::pair<std::map<std::string, Win3D*>::iterator, bool> Inserted =
                Widgets.insert(std::make_pair(std::string(Entry->InstanceName),
                                              static_cast<Win3D*>(nullptr)));
            Inserted.first->second = Widget;

            // The recursion result overwrites the running result every iteration.
            bResult = Instantiate(Entry->NestedChildren, Widget, Mat);
        }
        return bResult;
    }

    Page* CreatePage(FUiPageData* Data, Transform const* Mat, Win3D* Parent)
    {
        // FUN_00594590.
        Page* NewPage = new Page();

        // Retail clears the map here; on a freshly constructed page it is empty.
        for (std::map<std::string, Win3D*>::iterator It = NewPage->Widgets.begin();
             It != NewPage->Widgets.end(); ++It)
        {
            if (It->second != nullptr)
                delete It->second;
        }
        NewPage->Widgets.clear();

        NewPage->Aspect = Data->Aspect;
        if (!NewPage->Instantiate(Data->Entries, Parent, Mat))
        {
            delete NewPage;
            return nullptr;
        }
        return NewPage;
    }

    MasterWin::MasterWin(FUiPageData* Data)
        : Win3D()
        , PageData(Data)
        , PageObject(nullptr)
    {
        // FUN_00591ae0 (RET 4). Nothing is registered with the Event registry here.
    }

    MasterWin::~MasterWin()
    {
        // FUN_00591ba0.
        if (PageObject != nullptr)
        {
            delete PageObject;
            PageObject = nullptr;
        }
    }

    bool MasterWin::OnEvent(unsigned /*MsgId*/)
    {
        // FUN_0053f330.
        return false;
    }

    bool MasterWin::Create(Win3D* NewParent, void* /*Payload*/)
    {
        // FUN_00591cd0 (RET 8).
        Win3DReparent(this, NewParent);
        Win3DRegister(this);
        Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);

        if (PageData == nullptr)
            return false;

        // The Win3D camera (DAT_0067dbf8).
        ICamera* Camera = AsCamera(GetWin3DSceneCamera());
        float TopLeft[3];
        Camera->Unproject((Vector*)TopLeft, 0.0f, 0.0f);
        float BottomRight[3];
        // The created surface size (DAT_00610850 / DAT_00610854).
        Camera->Unproject((Vector*)BottomRight,
                          static_cast<float>(static_cast<int>(G_RenderSurfaceWidth)),
                          static_cast<float>(static_cast<int>(G_RenderSurfaceHeight)));

        float const Height = TopLeft[1] - BottomRight[1];

        Transform Frame;
        Frame.Rotation.E[0][0] = Height;
        Frame.Rotation.E[0][1] = 0.0f;
        Frame.Rotation.E[0][2] = 0.0f;
        Frame.Rotation.E[1][0] = 0.0f;
        Frame.Rotation.E[1][1] = Height;
        Frame.Rotation.E[1][2] = 0.0f;
        Frame.Rotation.E[2][0] = 0.0f;
        Frame.Rotation.E[2][1] = 0.0f;
        Frame.Rotation.E[2][2] = 1.0f;
        Frame.Translation.x = 0.0f;
        Frame.Translation.y = 0.0f;
        Frame.Translation.z = FloatFromBits(kMasterWinDepthBiasBits) + TopLeft[2];

        PageObject = CreatePage(PageData, &Frame, this);
        return PageObject != nullptr;
    }

    void MasterWin::Destroy()
    {
        // FUN_00591de0 (slot 10).
        if (PageObject != nullptr)
        {
            delete PageObject;
            PageObject = nullptr;
        }
        Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
        for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
            Child->Destroy();
        Win3DUnregister(this);
    }

    unsigned MasterWin::GetCount()
    {
        // LAB_00591c60
        if (PageObject == nullptr)
            return 0;
        return PageObject->GetCount();
    }

    char const* MasterWin::GetWidgetName(unsigned Index)
    {
        // LAB_00591c70
        if (PageObject == nullptr)
            return nullptr;
        return PageObject->GetWidgetName(Index);
    }

    Win3D* MasterWin::GetWidget(unsigned Index)
    {
        // LAB_00591c90
        if (PageObject == nullptr)
            return nullptr;
        return PageObject->GetWidget(Index);
    }

    Win3D* MasterWin::FindWidget(char const* Name)
    {
        // LAB_00591cb0
        if (PageObject == nullptr)
            return nullptr;
        return PageObject->FindWidget(Name);
    }
}
