// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_messaging.h"

#include "Common/Audio/menu_sound_arch.h"  // PlayClientSoundArchIfIdle (FUN_004285f0), SetClientSoundSourceType
#include "Space/player_behavior.h"  // GetPlayerObjRW (FUN_0054baf0)
#include "Space/space_letterbox.h"  // IsLetterboxVisible (FUN_0041a3e0)
#include "Space/star_sys.h"         // StarSysHasVoiceMessage (FUN_00553660), StarSysPreloadVoiceMessage
#include "Common/Audio/voice_profiles.h"   // LookupClientVoice (FUN_00430ab0), FVoiceProfile, FVoiceMsg

#include "common_id.h"        // CreateID
#include "dacom.h"            // FDUMP
#include "soundmanager.h"     // ISoundSource

#include <list>

namespace
{
    // One queue entry: the 12-byte payload of a list node (node +0x08..+0x13).
    struct FNnVoiceMessage
    {
        unsigned MsgId;         // +0x00
        unsigned char bKeyBit2; // +0x04
        ISoundSource* Source;   // +0x08

        FNnVoiceMessage(unsigned InMsgId, unsigned char bInKeyBit2)
            : MsgId(InMsgId), bKeyBit2(bInKeyBit2), Source(nullptr)
        {
        }

        // FUN_00489a50 / FUN_00489f70 (flag 0) / FUN_00489dc0: stop (vt+0x60) and release
        // (vt+8) the playing source.
        ~FNnVoiceMessage()
        {
            if (Source != nullptr)
            {
                Source->Stop();
                Source->Release();
                Source = nullptr;
            }
        }

        FNnVoiceMessage(FNnVoiceMessage const&) = delete;
        FNnVoiceMessage& operator=(FNnVoiceMessage const&) = delete;
    };

    // DAT_00671f50: constructed by the XC entry LAB_00489c70 (FUN_00489c90), destroyed at exit
    // through LAB_00489cc0 -> FUN_00489cd0. std::list keeps the same FIFO order and size count.
    std::list<FNnVoiceMessage> G_NnVoiceQueue;

    unsigned G_NnVoiceId = 0;                  // DAT_00671f5c
    FVoiceProfile* G_NnVoiceProfile = nullptr; // DAT_00671f70

    // NN_Messaging.cpp's overflow limit (0x00489b70).
    int const kNnVoiceQueueCapacity = 4;

    // FUN_00489a80 (thiscall on the entry): start the entry's line through the NNVoice profile.
    void NnVoicePlay(FNnVoiceMessage* Entry)
    {
        if (IsLetterboxVisible()) // FUN_0041a3e0
        {
            return;
        }
        if (G_NnVoiceProfile == nullptr)
        {
            return;
        }
        // With a player ship, make sure the line is on the system's preload list.
        if (GetPlayerObjRW() != nullptr)
        {
            if (!StarSysHasVoiceMessage(G_NnVoiceId, Entry->MsgId)) // FUN_00553660
            {
                StarSysPreloadVoiceMessage(G_NnVoiceId, Entry->MsgId); // FUN_00553590
            }
        }
        FVoiceMsg* const Msg = G_NnVoiceProfile->FindMessage(Entry->MsgId); // profile vt+0x14
        if (Msg == nullptr)
        {
            return;
        }
        // The play key: dword 1 takes the entry's flag in bit 2. Deviation: retail leaves bits
        // 3..31 of dword 1 as stack garbage; the port has 0. Dwords 0, 2..5 are 0.
        unsigned PlayKey[6] = {};
        PlayKey[1] = static_cast<unsigned>((Entry->bKeyBit2 << 2) & 4);
        void* Handle = nullptr;
        PlayClientSoundArchIfIdle(static_cast<Archetype::ISound*>(Msg), PlayKey, &Handle);
        // FUN_004285f0 writes the out pointer only when it starts a source.
        if (Handle != nullptr)
        {
            Entry->Source = static_cast<ISoundSource*>(Handle);
        }
        // Category 5: voice.
        if (Entry->Source != nullptr)
        {
            SetClientSoundSourceType(Entry->Source, 5);
        }
    }

    // FUN_00489b50 (thiscall on DAT_00671f50, RET 8).
    void NnVoiceQueuePush(unsigned MsgId, unsigned char bKeyBit2)
    {
        int const CountBefore = static_cast<int>(G_NnVoiceQueue.size());
        if (CountBefore >= kNnVoiceQueueCapacity)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_Messaging.cpp", 0xa2,
                  "NN Message inbox overflow!");
            return;
        }
        for (std::list<FNnVoiceMessage>::iterator It = G_NnVoiceQueue.begin();
             It != G_NnVoiceQueue.end(); ++It)
        {
            if (It->MsgId == MsgId)
            {
                return;
            }
        }
        G_NnVoiceQueue.emplace_back(MsgId, bKeyBit2);
        // The queue was empty: start the new head now.
        if (CountBefore == 0)
        {
            NnVoicePlay(&G_NnVoiceQueue.front());
        }
    }
}

void NnVoiceInit()
{
    // FUN_00489de0.
    G_NnVoiceId = CreateID("NNVoice");
    G_NnVoiceProfile = LookupClientVoice(G_NnVoiceId);
}

void NnVoiceTick(float Unused)
{
    // FUN_00489e00.
    (void)Unused;
    if (G_NnVoiceQueue.empty())
    {
        return;
    }
    // IsFinished is vt+0x74.
    ISoundSource* const HeadSource = G_NnVoiceQueue.front().Source;
    if (HeadSource != nullptr && HeadSource->IsFinished() == 0)
    {
        return;
    }
    G_NnVoiceQueue.pop_front();
    if (!G_NnVoiceQueue.empty())
    {
        NnVoicePlay(&G_NnVoiceQueue.front());
    }
}

void NnVoiceEnqueue(unsigned MsgId, unsigned char bKeyBit2)
{
    // FUN_00489e80.
    NnVoiceQueuePush(MsgId, bKeyBit2);
}

void NnVoiceEnqueueByName(char const* Name)
{
    // FUN_00489ea0.
    NnVoiceQueuePush(CreateID(Name), 0);
}
