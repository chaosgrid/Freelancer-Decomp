// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FlashLightSetInfo -- retail RoomData.cpp TU (0x06273bb0-0x06274120). Declared in
// common_goods.h, before RoomData.
#include "common_goods.h"
#include "common_csys.h"  // Csys, Csys_interpolate, Acquire3DMathEngine
#include "common_misc.h"  // malloc_strfree
#include "x86math.h"      // I3DMathEngine::MulTransform
#include "DALib.h"        // DALib::Engine
#include "dacom.h"        // FDUMP

#include <cstring>

int CallDacomStricmp(const char* Left, const char* Right); // common_dacom_stricmp.cpp

FlashLightSetInfo::FlashLightSetInfo()
{
    // Retail 0x13bb0.
    clear();
}

FlashLightSetInfo::~FlashLightSetInfo()
{
    // .text:06273bf0 is shared with destroy() (ICF).
    destroy();
}

void FlashLightSetInfo::destroy()
{
    // .text:06273bf0: frees both malloc'd-string banks (+0x20 and +0x120), then clear().
    for (int I = 0; I < 64; ++I)
    {
        malloc_strfree(const_cast<char**>(&HardpointNames[I]));
        malloc_strfree(const_cast<char**>(&SetpointNames[I]));
    }
    clear();
}

void FlashLightSetInfo::clear()
{
    // Common.dll.c 0x13c30.
    ColorR = 1.0f;
    ColorG = 1.0f;
    ColorB = 1.0f;
    Scale = 2.0f;
    Gap = 0.1f;
    Blink = 0.2f;
    EndPause = 1.0f;
    Mode = 0;

    for (int I = 0; I < 64; ++I)
    {
        HardpointNames[I] = nullptr;
        SetpointNames[I] = nullptr;
        LightFrames[I].set_identity();
    }

    NumLights = 0;
    NumLineLights = 0;
    Cursor = 0;

    for (int I = 0; I < 64; ++I)
        FinalFrames[I].set_identity();
}

void FlashLightSetInfo::add_light_csys(Csys const& LightFrame)
{
    // Retail 0x13d70. Retail does not bounds-check Cursor against the 64-slot bank.
    FinalFrames[Cursor] = LightFrame;
    ++Cursor;
}

void FlashLightSetInfo::unfixup()
{
    // Retail 0x14120.
    Cursor = 0;
}

void FlashLightSetInfo::fixup(long Instance, HardpointSummary const* HpSum, RoomData const* Room)
{
    // 0x06273db0. Instance (param_1) is never read.
    (void)Instance;
    static char const kRoomDataFile[] = "E:\\FL\\Scratch\\Source\\Common\\RoomData.cpp";

    Cursor = 0;
    for (long Index = 0; Index < NumLights; ++Index)
    {
        char const* const HardpointName = HardpointNames[Index];
        if (HardpointName != nullptr)
        {
            // A miss logs and keeps the frame.
            HardpointInfo const* const Info = HpSum->get_info_by_name(HardpointName);
            if (Info != nullptr)
            {
                // World = instance world transform * the hardpoint's local transform.
                long const HpInstance = HpSum->find_name_instance(HardpointName);
                Transform Local;
                Local.Rotation = Info->Orient;
                Local.Translation = Info->Position;
                Transform const* const InstanceXform =
                    (Transform const*)DALib::Engine->GetInstanceTransform((void*)HpInstance);
                I3DMathEngine* const Math = Acquire3DMathEngine();
                Transform World;
                Math->MulTransform((FTransform&)World, (FTransform const&)*InstanceXform,
                                   (FTransform const&)Local);
                LightFrames[Index].set_transform(World);
            }
            else
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot find hardpoint %s in the ONE room object\n",
                      kRoomDataFile, 0x187, HardpointName);
            }
        }

        // The setpoint override.
        char const* const SetpointName = SetpointNames[Index];
        if (SetpointName != nullptr)
        {
            // Every setpoint is compared; the last case-insensitive match wins.
            SetpointInfo const* Found = nullptr;
            for (unsigned SetpointIndex = 0; SetpointIndex < Room->Setpoints.size(); ++SetpointIndex)
            {
                // DACOM.DLL stricmp, not the CRT.
                if (CallDacomStricmp(SetpointName, Room->Setpoints[SetpointIndex]->Name) == 0)
                    Found = Room->Setpoints[SetpointIndex];
            }

            if (Found != nullptr)
            {
                LightFrames[Index] = Found->Frame;
            }
            else
            {
                // Two errors, the first with an empty script name (.rdata:063997b0).
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: script %s calls for setpoint %s which does not exist in room .ini = %s\n",
                      kRoomDataFile, 0x5a8, "", SetpointName, Room->File);
                FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot find setpoint %s\n",
                      kRoomDataFile, 0x194, SetpointName);
            }
        }
    }

    if (Mode == 1)
    {
        for (long Index = 0; Index < NumLights; ++Index)
        {
            FinalFrames[Cursor] = LightFrames[Index];
            ++Cursor;
        }
    }
    else if (Mode == 2)
    {
        // NumLineLights frames interpolated from LightFrames[0] to [1]; the quotient is formed
        // unrounded and rounded once.
        for (long Step = 0; Step < NumLineLights; ++Step)
        {
            float const T = (float)((double)Step / (NumLineLights - 1));
            FinalFrames[Cursor] = Csys_interpolate(LightFrames[0], LightFrames[1], T);
            ++Cursor;
        }
    }
}
