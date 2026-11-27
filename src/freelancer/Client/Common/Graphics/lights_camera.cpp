// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Graphics/lights_camera.h"

#include "DALib.h"
#include "alchemy.h"
#include "dacom.h"
#include "Core/game_options.h"
#include "Common/misc_util.h"
#include "Common/Graphics/vis_effect_arch.h"

namespace
{
    char const* const GLightsCameraFile = "E:\\FL\\Scratch\\Source\\Client\\supt\\LightsCamera.cpp";
}

// FUN_00557360.
bool FLightsCameraParticles::Create(FVisEffectArch* InVis)
{
    if (DALib::Alchemy == nullptr)
        return false;

    bool bCreated = false;
    Vis = InVis;
    Vis->AddRef();

    void* Node = nullptr;
    AGGDESC Desc = {8, "FxNode"};
    bool bFailed = true;
    if (DALib::Dacom->CreateInstance(&Desc, &Node) == GR_OK)
    {
        if (DALib::FxEffectLibrary->CreateEffect(Node, reinterpret_cast<void*>(Vis->EffectCrc),
                                                 &Effect) == GR_OK)
        {
            void* Root = nullptr;
            DALib::Alchemy->GetRootNode(&Root);
            DALib::Alchemy->AttachNode(Root, Node);
            static_cast<IDAComponent*>(Root)->Release();
            bCreated = true;
            bFailed = false;
        }
    }
    if (bFailed)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: ParticleEffect could not create Alchemy effect (%s)\n",
              GLightsCameraFile, 0x215, Vis->Nickname);
        Vis->Release();
        Vis = nullptr;
    }
    if (Node != nullptr)
        static_cast<IDAComponent*>(Node)->Release();
    return bCreated;
}

// FUN_005574a0. No null test of Vis.
void FLightsCameraParticles::Destroy()
{
    if (Effect == nullptr)
        return;
    Effect->DestroyNodes(reinterpret_cast<void*>(7));
    static_cast<IDAComponent*>(Effect)->Release();
    Effect = nullptr;
    Vis->Release();
    Vis = nullptr;
}

// FUN_005574d0. The 0x14-byte stack block is shared by slots 4 and 15 as in retail: the centre
// at Scratch[1..3], the radius at Scratch[4].
void FLightsCameraParticles::Draw(ICamera* Camera)
{
    if (Effect == nullptr)
        return;
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptParticles) == 0)
        return;

    float Scratch[5];
    Effect->GetTransform(0, (Transform**)&Scratch[0]);
    if (Effect->GetBoundingSphere(&Scratch[1]) < 0)
        return;
    if (!MiscUtilSphereInView(Camera, reinterpret_cast<Vector const*>(&Scratch[1]), Scratch[4]))
        return;
    Effect->Draw(Camera);
}
