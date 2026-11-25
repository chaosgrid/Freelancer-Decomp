// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/Thorn/gf_script_object.h"

#include "DALib.h"
#include "GameFlow/character_info.h"
#include "common_goods.h" // ThornScriptLoad / ThornScriptDestroy
#include "common_ini.h"   // RefreshLoadingProgress
#include "common_misc.h"  // malloc_strcpy / malloc_strfree, SpaceflightLocationID
#include "common_server.h" // IServer::RTCDone / MissionResponse
#include "dacom.h"
#include "Common/Graphics/fade_to_black.h"
#include "GameFlow/gameflow.h"
#include "GameFlow/gf_character_object.h" // FCharacterObject (slot 3, deleting dtor)
#include "GameFlow/Thorn/gf_script.h"
#include "GameFlow/Thorn/gf_thorn_component.h"
#include "GameFlow/Thorn/gf_thorn_sound.h"
#include "Common/input_manager.h"
#include "NeuroNet/Economy/knowledge_map.h"  // KnowledgeMapLookupFmtStr (FUN_00459ad0)
#include "Network/login_mgr.h"      // G_Server (DAT_0067ecd0)
#include "NeuroNet/NavMap/nn_navmap.h"      // NavMapRequestFocus (FUN_004a2aa0)
#include "NeuroNet/nn_offer_dialog.h"
#include "Interface/win_factory.h"    // WinFactoryCreate (FUN_0059db20)
#include "Common/Audio/menu_sound_arch.h"
#include "GameFlow/menu_universe_base.h"
#include "NeuroNet/nn_arrival_text.h"
#include "GameFlow/object_prop.h"
#include "Space/player_spawn.h"   // GetPlayerConnectionId (DAT_00673344)
#include "GameFlow/room_camera.h"
#include "GameFlow/room_control.h"
#include "Common/Audio/sound_archetype.h"
#include "Common/Audio/sound_config.h"
#include "soundmanager.h"
#include "Space/space_letterbox.h"
#include "thorn_iface.h"

#include <math.h>
#include <string.h>

FGfScriptManager G_GfScriptManager;

namespace
{
    // DAT_0066d304.
    bool G_bGfScriptSequenceSkipLatch = false;
}

// LAB_004548f0 (the skip_transition_scripts option callback). Set when Value < 0.5
// (DAT_005c75e8); the x87 compare treats a NaN as less, so a NaN sets it too.
void GfScriptSequenceSetSkipOption(float Value)
{
    G_bGfScriptSequenceSkipLatch = false;
    if (!(static_cast<double>(Value) >= 0.5))
        G_bGfScriptSequenceSkipLatch = true;
}

// FUN_00454910.
bool GfScriptSequenceSkipLatch()
{
    return G_bGfScriptSequenceSkipLatch;
}

namespace
{
    char const* const GScriptSequenceFile = "E:\\FL\\Scratch\\Source\\Client\\Gf\\ScriptSequence.cpp";

    // The fixed-rate ducking of FUN_00455290 / FUN_004554c0: DAT_005ce05c (-20 dB) and
    // DAT_005ce060 (2.5 dB per second); 0x501502f9 (1e10) is "the ducking config".
    float const kSequenceDuckDb = -20.0f;
    float const kSequenceDuckRate = 2.5f;
    float const kUseDuckingConfig = 1.0e10f;

    void SetIdentityCsys(Csys& Out)
    {
        Out.Pos.x = 0.0f;
        Out.Pos.y = 0.0f;
        Out.Pos.z = 0.0f;
        int Row = 0;
        for (Row = 0; Row < 3; ++Row)
        {
            int Col = 0;
            for (Col = 0; Col < 3; ++Col)
                Out.Orient.E[Row][Col] = Row == Col ? 1.0f : 0.0f;
        }
    }

    // The play key FUN_00428680 takes (six dwords; flags byte at +4: bit 0 loop, bit 1 spatial,
    // bit 2 streamer).
    struct FScriptSoundKey
    {
        unsigned Offset;
        unsigned Flags;
        unsigned Rest[4];
    };

    unsigned char MakeKeyFlags(unsigned char bStreamer, unsigned char bSpatial,
                               unsigned char bLoop)
    {
        // Retail's three bit-field inserts into a byte whose low three bits were cleared.
        unsigned char Flags = 0;
        Flags = static_cast<unsigned char>(Flags ^ (((bStreamer << 2) ^ Flags) & 4));
        Flags = static_cast<unsigned char>(Flags ^ (((bSpatial << 1) ^ Flags) & 2));
        Flags = static_cast<unsigned char>(Flags ^ ((bLoop ^ Flags) & 1));
        return Flags;
    }
}

// =========================================================================================
// FGfScript
// =========================================================================================

// FUN_0044eab0. +0x34, +0x5c and +0x60 are not written by retail here (FUN_0044ece0 /
// FUN_0044f720 write them before anything reads them); the port zeroes them.
FGfScript::FGfScript()
    : Sequence(nullptr),
      bPropsToRoom(0),
      bSoundsUseFade(0)
{
    Init();
}

// FUN_0044eb20. Spawned objects are deleted through identity slot 13 (the scalar deleting dtor).
FGfScript::~FGfScript()
{
    int Index = 0;
    for (Index = 0; Index < SpawnedCount; ++Index)
    {
        if (Spawned[Index] != nullptr)
            delete Spawned[Index];
        Spawned[Index] = nullptr;
    }
    delete[] Spawned;
    Spawned = nullptr;
    SoundEvents.clear();
}

// FUN_0044ebf0. +0x34, +0x5c and +0x60 are not touched.
void FGfScript::Init()
{
    SetIdentityCsys(Frame);
    Room = nullptr;
    Mirror = nullptr;
    Engine = nullptr;
    Duration = 0;
    bPaused = 0;
    bTornDown = 0;
    Spawned = nullptr;
    SpawnedCount = 0;
    bFinished = 0;
    bPlayerActor = 0;
    PlayerCharacter = nullptr;
    SetIdentityCsys(PlayerCsys);
    PlayerFloorHeight = 0.0f;
    PlayerDeformInstance = nullptr;
    BindMode = 0;
    bLoop = 0;
    bOwnScript = 0;
    bApplySpatial = 1;
    Priority = 0;
    bFade = 0;
    bFadeStarted = 0;
    Name = nullptr;
    ScriptFile = nullptr;
}

// FUN_0044ece0 (RET 8).
void FGfScript::SetRoom(FRoomRuntime* InRoom, FGfScriptSequence* InSequence)
{
    Room = InRoom;
    Sequence = InSequence;
}

// FUN_0044ed00. The engine is not tested for null. The spawned objects themselves stay (the
// dtor deletes them).
void FGfScript::Teardown()
{
    if (bTornDown != 0)
        return;
    StopScriptSounds();
    int Index = 0;
    for (Index = 0; Index < SpawnedCount; ++Index)
    {
        FThornComponent* const Object = Spawned[Index];
        if (Object == nullptr)
            continue;
        Object->SetScene(nullptr);
        Spawned[Index]->Stop();
        Engine->DetachEntity(Index);
    }
    Engine = nullptr;
    Sequence->ClearProps();
    Mirror = nullptr;
    malloc_strfree(&Name);
    malloc_strfree(&ScriptFile);
    bTornDown = 1;
}

// FUN_0044f270. Every early exit leaves the rest of Out as it was, including the fields
// already written.
bool FGfScript::CollectSoundEvent(int EventId, FGfScriptSoundEvent* Out)
{
    if (Engine == nullptr)
        return false;
    FThornEventDesc const* const Event =
        static_cast<FThornEventDesc const*>(Engine->GetEventById(EventId));
    if (Event->Type != kThornTypeStartSound)
        return false;
    Out->StartMs = Event->StartMs;
    Out->EndMs = Event->StartMs + Event->DurationMs;
    Out->bLoop = static_cast<unsigned char>((static_cast<unsigned>(Event->Flags) >> 3) & 1);
    int const EntityIndex = Event->PrimaryIndex;
    FThornEntitySnap const* const Snap =
        static_cast<FThornEntitySnap const*>(Engine->GetEntityDesc(EntityIndex));
    if (Snap->Type != kThornEntitySound)
        return false;
    if ((Snap->Flags & 1) != 0)
        return false;
    Out->bSpatial = static_cast<unsigned char>((static_cast<unsigned>(Snap->Flags) >> 1) & 1);
    if (_stricmp(Snap->TemplateName, "PlayerShipSound") == 0)
        return false;
    FThornComponent* const Object = Spawned[EntityIndex];
    if (Object == nullptr)
        return false;
    FThornSound* const Sound = static_cast<FThornSound*>(Object);
    Out->Arch = Sound->Arch;
    Out->Sound = Sound;
    return true;
}

// FUN_0044f310.
void FGfScript::CollectSoundEvents()
{
    if (Engine == nullptr)
        return;
    int const Count = static_cast<int>(Engine->GetEventCount());
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        FThornEventDesc const* const Event =
            static_cast<FThornEventDesc const*>(Engine->GetEventByIndex(Index));
        FGfScriptSoundEvent Local;
        memset(&Local, 0, sizeof(Local));
        if (!CollectSoundEvent(Event->UniqueId, &Local))
            continue;
        Local.EventId = Event->UniqueId;
        Local.Handle = nullptr;
        Local.bStarted = 0;
        SoundEvents.push_back(Local);
    }
}

// FUN_0044f410. A node is marked started even without an archetype.
void FGfScript::StartScriptSounds()
{
    std::list<FGfScriptSoundEvent>::iterator It = SoundEvents.begin();
    for (; It != SoundEvents.end(); ++It)
    {
        FGfScriptSoundEvent& Node = *It;
        if (Node.bStarted != 0)
            continue;
        Archetype::ISound* const Arch = static_cast<Archetype::ISound*>(Node.Arch);
        if (Arch != nullptr)
        {
            if (Arch->IsStreamer() == 0)
            {
                Arch->Acquire();
            }
            else
            {
                FScriptSoundKey Key;
                memset(&Key, 0, sizeof(Key));
                Key.Flags = MakeKeyFlags(Arch->IsStreamer(), Node.bSpatial, Node.bLoop);
                void* Handle = nullptr;
                PlayClientSoundArch(Arch, reinterpret_cast<unsigned*>(&Key), &Handle, 1);
                Node.Handle = Handle;
            }
        }
        Node.bStarted = 1;
    }
}

// FUN_0044f500. The engine is not tested; IsHoldingScript is FUN_00457380.
int FGfScript::GetSoundTailMs()
{
    int Tail = 0;
    int const Clock = Engine->GetClockMs();
    std::list<FGfScriptSoundEvent>::iterator It = SoundEvents.begin();
    for (; It != SoundEvents.end(); ++It)
    {
        if (It->Sound == nullptr)
            continue;
        if (!It->Sound->IsHoldingScript())
            continue;
        int const Remaining = It->EndMs - Clock;
        if (Remaining > Tail)
            Tail = Remaining;
    }
    return Tail;
}

// FUN_0044f550. A node is marked not started even without an archetype.
void FGfScript::StopScriptSounds()
{
    std::list<FGfScriptSoundEvent>::iterator It = SoundEvents.begin();
    for (; It != SoundEvents.end(); ++It)
    {
        FGfScriptSoundEvent& Node = *It;
        if (Node.bStarted == 0)
            continue;
        Archetype::ISound* const Arch = static_cast<Archetype::ISound*>(Node.Arch);
        if (Arch != nullptr)
        {
            if (Arch->IsStreamer() == 0)
            {
                Arch->Release();
            }
            else if (Node.Handle != nullptr)
            {
                ISoundSource* const Handle = static_cast<ISoundSource*>(Node.Handle);
                Handle->Stop();
                Handle->Release();
                Node.Handle = nullptr;
            }
        }
        Node.bStarted = 0;
    }
}

// FUN_0044f5c0.
void FGfScript::RemoveSoundEvent(void* Handle)
{
    if (Handle == nullptr)
        return;
    std::list<FGfScriptSoundEvent>::iterator It = SoundEvents.begin();
    for (; It != SoundEvents.end(); ++It)
    {
        if (It->Handle == Handle)
        {
            SoundEvents.erase(It);
            return;
        }
    }
}

// FUN_0044f610. A held handle is restarted (FUN_0042a2a0, result dropped). Otherwise a new
// source is started (FUN_00428680, bAltStart 0) and answered, not stored in any node.
void* FGfScript::PlayStreamedSoundEvent(int EventId, int Offset)
{
    void* Result = nullptr;
    std::list<FGfScriptSoundEvent>::iterator It = SoundEvents.begin();
    for (; It != SoundEvents.end(); ++It)
    {
        if (It->EventId != EventId)
            continue;
        if (It->Handle != nullptr)
        {
            ISoundSource* const Handle = static_cast<ISoundSource*>(It->Handle);
            RestartClientStreamedSound(Handle, It->Arch, Offset);
            It->bStarted = 0;
            return Handle;
        }
        break;
    }

    FGfScriptSoundEvent Local;
    memset(&Local, 0, sizeof(Local));
    if (CollectSoundEvent(EventId, &Local))
    {
        Archetype::ISound* const Arch = static_cast<Archetype::ISound*>(Local.Arch);
        FScriptSoundKey Key;
        memset(&Key, 0, sizeof(Key));
        Key.Flags = MakeKeyFlags(Arch->IsStreamer(), Local.bSpatial, Local.bLoop);
        Key.Offset = static_cast<unsigned>(Offset);
        PlayClientSoundArch(Arch, reinterpret_cast<unsigned*>(&Key), &Result, 0);
    }
    return Result;
}

// FUN_0044f720. The name is copied into +0xa0 without freeing the old one (retail); the file
// is freed first.
void FGfScript::Start(IScriptEngine* InEngine, char const* InScriptFile, int InBindMode,
                      unsigned char bInLoop, unsigned char bInOwnScript,
                      unsigned char bInApplySpatial, int InPriority, unsigned char bInFade,
                      char const* InName, unsigned char bInPropsToRoom,
                      unsigned char bInSoundsUseFade)
{
    Engine = InEngine;
    BindMode = InBindMode;
    bOwnScript = bInOwnScript;
    bApplySpatial = bInApplySpatial;
    bLoop = bInLoop;
    bPropsToRoom = bInPropsToRoom;
    bFade = bInFade;
    Priority = InPriority;
    bFadeStarted = 0;
    bSoundsUseFade = bInSoundsUseFade;
    malloc_strcpy(&Name, InName);
    malloc_strfree(&ScriptFile);
    malloc_strcpy(&ScriptFile, InScriptFile);
    if (Engine == nullptr)
        return;
    Duration = Engine->GetScriptDuration();
    Spawn();
    CollectSoundEvents();
    StartScriptSounds();
    Engine->SetLooping(bLoop);
    Engine->StartPlayback(0);
    Engine->TickPlayback(0);
}

// FUN_0044f7f0. The clock compare is unsigned; the remaining time is loaded as an unsigned
// 64-bit integer. The fade starts once the remaining seconds are not above the fade duration
// DAT_0061650c (FUN_00412070 / FUN_00412050).
bool FGfScript::Update(int Ms)
{
    if (Engine == nullptr)
    {
        bFinished = 1;
        return bFinished != 0;
    }
    if (bFinished == 0)
    {
        if (bPaused == 0)
        {
            Engine->TickPlayback(GetSoundTailMs() + Ms);
            int const Clock = Engine->GetClockMs();
            if (bLoop == 0 && static_cast<unsigned>(Clock) >= static_cast<unsigned>(Duration))
                bFinished = 1;
            if (bFade != 0 && bFadeStarted == 0)
            {
                unsigned const RemainingMs =
                    static_cast<unsigned>(Duration) - static_cast<unsigned>(Clock);
                // The first compare is on the unrounded product still on the x87 stack; the
                // float copy feeds the rest.
                double const SecondsExact = static_cast<double>(RemainingMs) * 0.001;
                float const Seconds = static_cast<float>(SecondsExact);
                if (!(SecondsExact > static_cast<double>(GetFadeDuration())))
                {
                    if (Seconds > 0.0f)
                        SetFadeTarget(1.0f, static_cast<float>(1.0 / static_cast<double>(Seconds)));
                    else
                        SetFadeImmediate(1.0f);
                    bFadeStarted = 1;
                }
            }
        }
        else
        {
            Engine->TickPlayback(Duration - Engine->GetClockMs());
            if (bFade != 0)
            {
                SetFadeImmediate(1.0f);
                bFadeStarted = 1;
            }
            bFinished = 1;
            bPaused = 0;
        }
    }
    if (bFinished != 0)
    {
        Engine->TickPlayback(100);
        if (bFade != 0)
        {
            SetFadeImmediate(1.0f);
            bFadeStarted = 1;
        }
    }
    return bFinished != 0;
}

// FUN_0044f950.
void FGfScript::Restart()
{
    if (Engine == nullptr)
        return;
    Engine->StopPlayback();
    Engine->StartPlayback(0);
}

// FUN_0044f970.
void FGfScript::DrawEffects(ICamera* Camera)
{
    int Index = 0;
    for (Index = 0; Index < SpawnedCount; ++Index)
    {
        if (Spawned[Index] != nullptr)
            Spawned[Index]->DrawEffects(Camera);
    }
}

// FUN_0044f9a0. No null check on the entity or the sequence -- the callers only get here for an
// index FUN_0044fdd0 / FUN_0044ff30 just used.
void FGfScript::SetEntityPropDetached(int Index, unsigned char bDetached)
{
    InstanceIndex Instance = Spawned[Index]->GetEngineInstance();
    Sequence->SetPropDetached(Instance, bDetached);
}

// FUN_0044f9d0.
void FGfScript::Pause()
{
    bPaused = 1;
}

// FUN_0044f9e0. One more millisecond unless Scale < 1.0 (a NaN Scale counts as less).
void FGfScript::Advance(float Scale)
{
    unsigned const RemainingMs =
        static_cast<unsigned>(Duration) - static_cast<unsigned>(Engine->GetClockMs());
    if (RemainingMs == 0)
        return;
    double const Scaled =
        static_cast<double>(RemainingMs) * static_cast<double>(Scale) + 0.5;
    int Ms = static_cast<int>(floor(Scaled));
    if (!(static_cast<double>(Scale) < 1.0))
        ++Ms;
    Update(Ms);
}

// =========================================================================================
// FGfMirrorDatabase
// =========================================================================================

// FUN_004539b0.
FGfMirrorDatabase::FGfMirrorDatabase()
{
}

// FUN_004539e0.
FGfMirrorDatabase::~FGfMirrorDatabase()
{
    Entries.clear();
}

// FUN_00453a40. A null name is "" (DAT_005c865c).
void FGfMirrorDatabase::Add(FCharacterObject* Object, char const* ObjName)
{
    FEntry Entry;
    Entry.Object = Object;
    Entry.Name = ObjName != nullptr ? ObjName : "";
    Entries.push_back(Entry);
}

// FUN_00453b50.
FCharacterObject* FGfMirrorDatabase::First() const
{
    if (Entries.empty())
        return nullptr;
    return Entries.front().Object;
}

// FUN_00453b60. A node with null string storage reads as "".
FCharacterObject* FGfMirrorDatabase::Find(char const* ObjName) const
{
    std::list<FEntry>::const_iterator It = Entries.begin();
    for (; It != Entries.end(); ++It)
    {
        if (_stricmp(It->Name.c_str(), ObjName) == 0)
            return It->Object;
    }
    return nullptr;
}

// =========================================================================================
// FGfScriptEntry
// =========================================================================================

// LAB_00454e20.
FGfScriptEntry::FGfScriptEntry()
    : Script(nullptr),
      Engine(nullptr),
      ScriptFile(nullptr),
      Kind(0)
{
    SetIdentityCsys(Frame);
}

// LAB_00454d70. Everything goes back to the ctor's values.
FGfScriptEntry::~FGfScriptEntry()
{
    if (Script != nullptr)
        Script->Teardown();
    malloc_strfree(&ScriptFile);
    ThornScriptDestroy(&Engine);
    if (Script != nullptr)
        delete Script;
    Script = nullptr;
    Engine = nullptr;
    ScriptFile = nullptr;
    SetIdentityCsys(Frame);
    Kind = 0;
}

// FUN_00454820. A second run reloads the engine from the file before the new script
// (operator new(0xb4)).
FGfScript* FGfScriptEntry::Create(FRoomRuntime* InRoom, FGfScriptSequence* InSeq)
{
    if (Script != nullptr)
    {
        ThornScriptDestroy(&Engine);
        delete Script;
        Script = nullptr;
        Engine = ThornScriptLoad(ScriptFile);
    }
    Script = new FGfScript();
    Script->Init();
    Script->SetRoom(InRoom, InSeq);
    Script->Frame = Frame;
    return Script;
}

// =========================================================================================
// FGfScriptSequence
// =========================================================================================

// FUN_00454920. Retail does not write +0x68 (PlayerDeformInstance) here; the port zeroes it.
FGfScriptSequence::FGfScriptSequence()
    : BindMode(0),
      bLoop(0),
      bOwnScript(0),
      bLetterbox(1),
      bFade(0),
      bApplySpatial(1),
      bRestoreCamera(1),
      bDuckAmbient(0),
      bDuckMusic(0),
      bDuckSlow(0),
      bSoundsUseFade(0),
      bRestorePlayer(1),
      bHoldTime(1),
      Priority(0),
      bPropsToRoom(0),
      bSkippable(1),
      bTransition(0),
      Callback(nullptr),
      CallbackA(nullptr),
      CallbackB(nullptr),
      RtcId(0),
      Location(0),
      bStopped(1),
      bPlaying(0),
      bSkip(0),
      bFindable(0),
      bPlayerSaved(0),
      PlayerFloorHeight(0.0f),
      PlayerDeformInstance(nullptr),
      PlayerCharacter(nullptr),
      CameraHalfFovX(0.0f),
      CameraZNear(0.0f),
      CameraZFar(0.0f),
      bHasAmbient(0),
      bHasMusic(0),
      Room(nullptr),
      Entries(nullptr),
      Capacity(0),
      Count(0),
      Current(-1),
      bActive(0),
      OfferType(0),
      OfferId(0),
      OfferKind(0),
      bOfferShown(0),
      bOfferAnswered(0),
      bOfferAccepted(0),
      OfferDialog(nullptr),
      bInfoShown(0),
      bInfoClosed(0),
      InfoDialog(nullptr),
      Name(nullptr)
{
    SetIdentityCsys(PlayerCsys);
    int Index = 0;
    for (Index = 0; Index < 3; ++Index)
        CameraPos[Index] = 0.0f;
    for (Index = 0; Index < 9; ++Index)
        CameraOrient[Index] = 0.0f;
}

// FUN_00454bb0. Only the offer dialog is deleted here; the info dialog is not.
FGfScriptSequence::~FGfScriptSequence()
{
    if (bStopped == 0)
        StopPlayback(1);
    delete[] Entries;
    Entries = nullptr;
    DialogTextA.destroy();
    DialogTextB.destroy();
    if (OfferDialog != nullptr)
        delete OfferDialog;
    OfferDialog = nullptr;
    malloc_strfree(&Name);
    std::list<FProp>::iterator PropIt = Props.begin();
    for (; PropIt != Props.end(); ++PropIt)
    {
        if (PropIt->Prop != nullptr)
            delete PropIt->Prop;
        PropIt->Prop = nullptr;
    }
    Props.clear();
    std::list<FCharacter>::iterator CharIt = Characters.begin();
    for (; CharIt != Characters.end(); ++CharIt)
    {
        if (CharIt->Character != nullptr)
            delete CharIt->Character;
        CharIt->Character = nullptr;
    }
    Characters.clear();
}

// FUN_00454e80.
void FGfScriptSequence::SetName(char const* InName)
{
    malloc_strfree(&Name);
    malloc_strcpy(&Name, InName);
}

// FUN_00454eb0. The old file string is not freed. DAT_006799e4 (the Gameflow arrival caption's
// delay) takes the script's duration. No check against +0xb8 (retail: a 0x21st script writes
// past the 0x20 entries -- no caller adds that many).
void FGfScriptSequence::AddScript(char const* File, int InKind, Csys const* InFrame,
                                  IScriptEngine* InEngine)
{
    FGfScriptEntry& Entry = Entries[Count];
    malloc_strcpy(&Entry.ScriptFile, File);
    Entry.Kind = InKind;
    Entry.Frame = *InFrame;
    IScriptEngine* Engine = InEngine;
    if (Engine == nullptr)
        Engine = ThornScriptLoad(Entry.ScriptFile);
    Entry.Engine = Engine;
    SetGameflowArrivalDelayMs(static_cast<unsigned>(Engine->GetScriptDuration()));
    ++Count;
}

// FUN_00454f30.
void FGfScriptSequence::SetDialog(int InOfferType, int InOfferId, int InOfferKind, FmtStr const& TextA,
                                  FmtStr const& TextB)
{
    OfferType = InOfferType;
    OfferId = InOfferId;
    OfferKind = InOfferKind;
    DialogTextA = TextA;
    DialogTextB = TextB;
}

// FUN_00454f80.
void FGfScriptSequence::Play()
{
    RefreshLoadingProgress();
    bPlaying = 1;
    Setup();
    StartEntry(0);
    bActive = 1;
}

// FUN_00454fb0. The fade is forced on when a location to move to is set and this is the last
// entry. Kind 1 opens the offer dialog (FUN_00455850), kind 4 the info box.
void FGfScriptSequence::StartEntry(int Index)
{
    RefreshLoadingProgress();
    Current = Index;
    FGfScriptEntry& Entry = Entries[Index];
    Entry.Create(Room, this);
    Entries[Current].Script->Mirror = &Mirror;

    unsigned char bScriptFade = bFade;
    if (Location != 0 && NextEntryIndex() >= Count)
        bScriptFade = 1;

    FGfScriptEntry& Started = Entries[Current];
    Started.Script->Start(Started.Engine, Started.ScriptFile, BindMode, bLoop, bOwnScript,
                          bApplySpatial, Priority, bScriptFade, Name, bPropsToRoom,
                          bSoundsUseFade);

    if (bTransition != 0 && GfScriptSequenceSkipLatch() && bPlaying != 0 && bSkippable != 0)
        bSkip = 1;
    if (bHoldTime != 0)
        HoldGameflowTime();

    FGfScript* const Script = Entries[Current].Script;
    if (bPlayerSaved == 0 && Script->bPlayerActor != 0)
    {
        bPlayerSaved = 1;
        PlayerCsys = Script->PlayerCsys;
        PlayerFloorHeight = Script->PlayerFloorHeight;
        PlayerDeformInstance = Script->PlayerDeformInstance;
        PlayerCharacter = Script->PlayerCharacter;
    }

    // A kind-1 entry (an offer). The entries after it are scanned while their kinds are 2, 3, 4
    // or 6 -- 2 and 4 mark an Accept branch, 3 a Decline branch, 6 neither. No branch found (or
    // no entry after it) counts as both.
    if (Entries[Current].Kind == 1)
    {
        unsigned char bAcceptBranch = 0;
        unsigned char bDeclineBranch = 0;
        int Next = Current + 1;
        if (Next < Count)
        {
            for (; Next < Count; ++Next)
            {
                int const NextKind = Entries[Next].Kind;
                if (NextKind == 2 || NextKind == 4)
                    bAcceptBranch = 1;
                else if (NextKind == 3)
                    bDeclineBranch = 1;
                else if (NextKind != 6)
                    break;
            }
            if (bAcceptBranch == 0 && bDeclineBranch == 0)
            {
                bAcceptBranch = 1;
                bDeclineBranch = 1;
            }
        }
        else
        {
            bAcceptBranch = 1;
            bDeclineBranch = 1;
        }
        if (bOfferShown == 0)
        {
            ShowOfferDialog(bAcceptBranch, bDeclineBranch);
            if (bLetterbox != 0)
            {
                SetLetterboxTarget(0.0f, 3.0f);
                SetClientMouseCursorDrawFlag(1);
            }
            bOfferShown = 1;
            bOfferAnswered = 0;
            bOfferAccepted = 0;
        }
    }

    // A kind-4 entry (an info box): the nav map is focused on the place the info text names
    // (FUN_00459ad0; FUN_004a2aa0 with bZone = flag bit 0x20).
    if (Entries[Current].Kind == 4 && bInfoShown == 0)
    {
        unsigned Id = 0;
        unsigned char Flags = 0;
        if (KnowledgeMapLookupFmtStr(&DialogTextB, &Id, &Flags))
            NavMapRequestFocus(Id, (Flags & 0x20) != 0 ? 1 : 0);
        InfoDialog = static_cast<NnOfferDialog*>(
            WinFactoryCreate("ScriptSequence_InfoDialog", "NN_OfferDialog", nullptr, nullptr));
        InfoDialog->Open(4, &DialogTextB, true, &FGfScriptSequence::OnInfoDialogClosed, this);
        if (bLetterbox != 0)
        {
            SetLetterboxTarget(0.0f, 3.0f);
            SetClientMouseCursorDrawFlag(1);
        }
        bInfoShown = 1;
        bInfoClosed = 0;
    }
}

// FUN_00455290. The room ambience / music duck unless a script plays its own: at the fixed rate
// with bDuckSlow, else by the ducking config (FUN_00428d00 / FUN_00428c50).
void FGfScriptSequence::Setup()
{
    RefreshLoadingProgress();
    ICamera* const Camera = GetRoomRuntimeCamera(Room);
    const float* const Transform = (const float*)Camera->GetOrientation();
    int Index = 0;
    for (Index = 0; Index < 3; ++Index)
        CameraPos[Index] = Transform[9 + Index];
    for (Index = 0; Index < 9; ++Index)
        CameraOrient[Index] = Transform[Index];
    CameraHalfFovX = Camera->GetHalfFovXDeg();
    CameraZNear = Camera->GetZNear();
    CameraZFar = Camera->GetZFar();

    bHasAmbient = 0;
    bHasMusic = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        IScriptEngine* const Engine = Entries[Index].Engine;
        unsigned char bAmbient = 0;
        unsigned char bMusic = 0;
        GfScriptCharacterAnimations(Engine, true);
        GfScriptFindBackgroundAudio(Engine, &bAmbient, &bMusic);
        GfScriptTouchEntities(Engine, true);
        if (bAmbient != 0)
            bHasAmbient = 1;
        if (bMusic != 0)
            bHasMusic = 1;
    }

    RefreshLoadingProgress();
    GetCharacterInfoDb()->FlushQueuedScripts();

    if (bDuckAmbient != 0 && bHasAmbient == 0)
    {
        if (bDuckSlow != 0)
        {
            FadeClientBackgroundAmbient(3, kSequenceDuckDb, kSequenceDuckRate);
            SetClientAudioFadeTargetRate(kSequenceDuckDb, kSequenceDuckRate);
        }
        else
        {
            FadeClientBackgroundAmbient(3, kUseDuckingConfig, kUseDuckingConfig);
            float DownBy = 0.0f;
            float DownTime = 0.0f;
            GetClientDuckingConfig(&DownBy, nullptr, nullptr);
            GetClientDuckingConfig(nullptr, &DownTime, nullptr);
            // Target down_by; rate |down_by - current| / down_time, or 1e6 when down_time is not
            // above 0 -- FUN_004569d0's body.
            SetClientAudioFade(DownBy, DownTime > 0.0f ? DownTime : 0.0f);
        }
    }
    if (bDuckMusic != 0 && bHasMusic == 0)
    {
        if (bDuckSlow != 0)
            FadeClientBackgroundMusic(3, kSequenceDuckDb, kSequenceDuckRate);
        else
            FadeClientBackgroundMusic(3, kUseDuckingConfig, kUseDuckingConfig);
    }
    if (bLetterbox != 0)
    {
        SetLetterboxTarget(1.0f, 3.0f);
        LockHotspotInput();
        SetClientMouseCursorDrawFlag(0);
    }
    bStopped = 0;
}

// FUN_004554c0. Once. Moving to a location (FUN_00558a30 for spaceflight, else FUN_0043ba90
// with audio unless the room is already loaded) skips every restore below it.
void FGfScriptSequence::StopPlayback(unsigned char bArg)
{
    if (bStopped != 0)
        return;
    RefreshLoadingProgress();

    bool bMoved = false;
    if (Location != 0)
    {
        if (Location == SpaceflightLocationID.Id)
        {
            RequestGameflowMissionBriefing();
        }
        else if (IsClientLocationRoomLoaded(Location))
        {
            ApplyClientLocationAudio(Location);
            QueueClientLocationChange(Location, 0.0f, false);
        }
        else
        {
            QueueClientLocationChange(Location, 0.0f, true);
        }
        SetFadeImmediate(1.0f);
        HoldGameflowTime();
        bMoved = true;
    }

    if (bRestorePlayer != 0 && !bMoved && bPlayerSaved != 0)
    {
        // Results unused; the character is not null-tested.
        DALib::Deform->SetPosePos(PlayerDeformInstance, &PlayerCsys.Pos.x);
        DALib::Deform->SetPoseOri(PlayerDeformInstance, &PlayerCsys.Orient);
        PlayerCharacter->SetFloorHeight(PlayerFloorHeight);
    }

    if (bLetterbox != 0 && !bMoved)
    {
        SetLetterboxTarget(0.0f, 3.0f);
        UnlockHotspotInput();
        SetClientMouseCursorDrawFlag(1);
        RoomControlRequestCharacterScripts(Room);
    }

    bool const bRestoreAmbient = bDuckAmbient != 0 && !bMoved;
    bool const bRestoreMusic = bDuckMusic != 0 && !bMoved;
    if (bRestoreAmbient)
    {
        if (bDuckSlow != 0)
        {
            FadeClientBackgroundAmbient(3, 0.0f, kSequenceDuckRate);
            SetClientAudioFadeTargetRate(0.0f, kSequenceDuckRate);
        }
        else
        {
            FadeClientBackgroundAmbient(3, 0.0f, kUseDuckingConfig);
            float DownTime = 0.0f;
            GetClientDuckingConfig(nullptr, &DownTime, nullptr);
            SetClientAudioFade(0.0f, DownTime > 0.0f ? DownTime : 0.0f);
        }
    }
    if (bRestoreMusic)
    {
        if (bDuckSlow != 0)
            FadeClientBackgroundMusic(3, 0.0f, kSequenceDuckRate);
        else
            FadeClientBackgroundMusic(3, 0.0f, kUseDuckingConfig);
    }

    if (bRestoreCamera != 0 && !bMoved)
    {
        FRoomCamera* const Camera = GetRoomRuntimeRoomCamera(Room);
        float Transform[12];
        int Index = 0;
        for (Index = 0; Index < 9; ++Index)
            Transform[Index] = CameraOrient[Index];
        for (Index = 0; Index < 3; ++Index)
            Transform[9 + Index] = CameraPos[Index];
        RoomCameraSetTransform(Camera, Transform);
        RoomCameraSetHalfFovX(Camera, CameraHalfFovX);
        RoomCameraSetZNear(Camera, CameraZNear);
        RoomCameraSetZFar(Camera, CameraZFar);
    }

    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        IScriptEngine* const Engine = Entries[Index].Engine;
        GfScriptTouchEntities(Engine, false);
        GfScriptCharacterAnimations(Engine, false);
    }

    bStopped = 1;
    // IServer slot 69; G_Server is not null-tested.
    if (RtcId != 0)
        G_Server->RTCDone(GetPlayerConnectionId(), RtcId);
    if (Callback != nullptr)
        Callback(CallbackA, CallbackB, bArg);
}

// FUN_004557d0. Caller: IClient slot 114 (LAB_0053e370) on DAT_00673350.
void FGfScriptSequence::OnMissionOfferAnswered()
{
    SetMissionOfferSequenceRetail(nullptr); // DAT_00673350
    if (bOfferShown == 0)
        return;
    if (OfferDialog != nullptr)
        delete OfferDialog;
    OfferDialog = nullptr;
    if (bLetterbox != 0)
    {
        SetLetterboxTarget(1.0f, 3.0f);
        SetClientMouseCursorDrawFlag(0);
    }
    Entries[Current].Script->Pause();
    bOfferShown = 0;
}

// FUN_00455850 (thiscall, RET 8).
void FGfScriptSequence::ShowOfferDialog(unsigned char bHasAcceptBranch,
                                        unsigned char bHasDeclineBranch)
{
    (void)bHasAcceptBranch;
    // FUN_0059db20; the result is not null-tested.
    OfferDialog = static_cast<NnOfferDialog*>(
        WinFactoryCreate("ScriptSequence_OfferDialog", "NN_OfferDialog", nullptr, nullptr));

    FmtStr Text;
    int Type = 0;
    if (OfferType == 1)
    {
        Type = 1;
        Text = DialogTextA;
    }
    else if (OfferType == 2)
    {
        Type = 2;
        Text = DialogTextB;
    }
    else if (OfferType == 3)
    {
        Type = 0;
        Text = DialogTextA;
    }
    else if (OfferType == 4)
    {
        Type = 3;
        Text = DialogTextA;
    }
    else
    {
        Text = DialogTextA;
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Invalid offer type %d\n", GScriptSequenceFile,
              0x38b, OfferType);
    }

    // FUN_004a6630.
    OfferDialog->Open(Type, &Text, bHasDeclineBranch == 0, &FGfScriptSequence::OnOfferAnswer,
                      this);
}

// LAB_004559b0 (cdecl).
void __cdecl FGfScriptSequence::OnOfferAnswer(void* Context, int Answer)
{
    FGfScriptSequence* const Seq = static_cast<FGfScriptSequence*>(Context);
    if (Answer == 0)
    {
        // Accept, first answer. A multiplayer group offer (type 3) the server already answered
        // sends nothing; otherwise IServer slot 43 MissionResponse (G_Server not null-tested),
        // and a multiplayer type 3 registers itself in DAT_00673350 first.
        if (Seq->bOfferAnswered == 0)
        {
            Seq->bOfferAnswered = 1;
            Seq->bOfferAccepted = 1;
            if (SinglePlayer() || Seq->OfferType != 3 || GetMissionOfferAnsweredRetail() == 0)
            {
                if (!SinglePlayer() && Seq->OfferType == 3)
                    SetMissionOfferSequenceRetail(Seq);
                G_Server->MissionResponse(static_cast<unsigned>(Seq->OfferId),
                                          static_cast<unsigned long>(Seq->OfferKind),
                                          Seq->bOfferAccepted != 0, GetPlayerConnectionId());
            }
        }
        // A multiplayer group offer the server has not answered yet stays open (FUN_004557d0
        // closes it later).
        if (!SinglePlayer() && Seq->OfferType == 3 && GetMissionOfferAnsweredRetail() == 0)
            return;
    }
    else if (Answer == 1)
    {
        // Decline / Close, first answer.
        if (Seq->bOfferAnswered == 0)
        {
            Seq->bOfferAnswered = 1;
            Seq->bOfferAccepted = 0;
            G_Server->MissionResponse(static_cast<unsigned>(Seq->OfferId),
                                      static_cast<unsigned long>(Seq->OfferKind), false,
                                      GetPlayerConnectionId());
        }
    }
    else
    {
        return;
    }

    if (Seq->bOfferShown == 0)
        return;
    if (Seq->OfferDialog != nullptr)
        delete Seq->OfferDialog;
    Seq->OfferDialog = nullptr;
    if (Seq->bLetterbox != 0)
    {
        SetLetterboxTarget(1.0f, 3.0f);
        SetClientMouseCursorDrawFlag(0);
    }
    Seq->Entries[Seq->Current].Script->Pause();
    Seq->bOfferShown = 0;
}

// LAB_00455b10 (cdecl). Answer is not read.
void __cdecl FGfScriptSequence::OnInfoDialogClosed(void* Context, int Answer)
{
    (void)Answer;
    FGfScriptSequence* const Seq = static_cast<FGfScriptSequence*>(Context);
    Seq->bInfoClosed = 1;
    if (Seq->bInfoShown == 0)
        return;
    if (Seq->InfoDialog != nullptr)
        delete Seq->InfoDialog;
    Seq->InfoDialog = nullptr;
    if (Seq->bLetterbox != 0)
    {
        SetLetterboxTarget(1.0f, 3.0f);
        SetClientMouseCursorDrawFlag(0);
    }
    Seq->Entries[Seq->Current].Script->Pause();
    Seq->bInfoShown = 0;
}

// FUN_00455b80. A null name is "".
void FGfScriptSequence::AddProp(FObjectProp* Prop, char const* PropName)
{
    FProp Entry;
    Entry.Prop = Prop;
    Entry.Name = PropName != nullptr ? PropName : "";
    Props.push_back(Entry);
}

// FUN_00455ca0.
void FGfScriptSequence::ClearProps()
{
    std::list<FProp>::iterator It = Props.begin();
    for (; It != Props.end(); ++It)
    {
        if (It->Prop != nullptr)
            delete It->Prop;
        It->Prop = nullptr;
    }
    Props.clear();
}

// FUN_00455d20. The props are not null-tested.
void FGfScriptSequence::SetPropDetached(InstanceIndex Instance, unsigned char bDetached)
{
    std::list<FProp>::iterator It = Props.begin();
    for (; It != Props.end(); ++It)
    {
        if (It->Prop->GetEngineInstance() == Instance)
        {
            It->Prop->bDetached = bDetached;
            return;
        }
    }
}

// FUN_00455d70. FUN_00455b80's body on the +0x124 list.
void FGfScriptSequence::AddCharacter(FCharacterObject* Character, char const* CharName)
{
    FCharacter Entry;
    Entry.Character = Character;
    Entry.Name = CharName != nullptr ? CharName : "";
    Characters.push_back(Entry);
}

// FUN_00455ed0. A node with null string storage reads as "".
FCharacterObject* FGfScriptSequence::FindCharacter(char const* CharName) const
{
    if (CharName == nullptr)
        return nullptr;
    std::list<FCharacter>::const_iterator It = Characters.begin();
    for (; It != Characters.end(); ++It)
    {
        if (_stricmp(It->Name.c_str(), CharName) == 0)
            return It->Character;
    }
    return nullptr;
}

// FUN_00455f50.
int FGfScriptSequence::FindNextOfKinds(int const* Kinds, int KindCount) const
{
    int const Span = Count - Current - 1;
    int Offset = 1;
    if (Span < 1)
        return Span + 1;
    for (Offset = 1; Offset <= Span; ++Offset)
    {
        int const Kind = Entries[Current + Offset].Kind;
        int KindIndex = 0;
        for (KindIndex = 0; KindIndex < KindCount; ++KindIndex)
        {
            if (Kind == Kinds[KindIndex])
                return Offset;
        }
    }
    return Span + 1;
}

// FUN_00455fc0. Kind 5 (the traffic script) and every unlisted kind repeat the current entry.
int FGfScriptSequence::NextEntryIndex() const
{
    int const Kind = Entries[Current].Kind;
    int Step = 0;
    if (Kind == 0)
        return Current + 1;
    if (Kind == 1)
    {
        if (bOfferAnswered != 0)
        {
            if (bOfferAccepted != 0)
            {
                // A multiplayer group offer (type 3) waits for the server's answer
                // (DAT_00673348), then follows the Accept branch when this player is the acceptor
                // (DAT_0067334c) and the kind-6 branch otherwise.
                if (!SinglePlayer() && OfferType == 3)
                {
                    if (GetMissionOfferAnsweredRetail() != 0)
                    {
                        if (GetMissionOfferAcceptorIdRetail() == GetPlayerConnectionId())
                        {
                            int const Kinds[3] = {0, 2, 4};
                            Step = FindNextOfKinds(Kinds, 3);
                        }
                        else
                        {
                            int const Kinds[2] = {0, 6};
                            Step = FindNextOfKinds(Kinds, 2);
                        }
                    }
                }
                else
                {
                    int const Kinds[3] = {0, 2, 4};
                    Step = FindNextOfKinds(Kinds, 3);
                }
            }
            else
            {
                int const Kinds[2] = {0, 3};
                Step = FindNextOfKinds(Kinds, 2);
            }
        }
    }
    else if (Kind == 2)
    {
        int const Kinds[3] = {0, 2, 4};
        Step = FindNextOfKinds(Kinds, 3);
    }
    else if (Kind == 3)
    {
        int const Kinds[2] = {0, 3};
        Step = FindNextOfKinds(Kinds, 2);
    }
    else if (Kind == 4)
    {
        if (bInfoClosed != 0)
        {
            int const Kinds[3] = {0, 2, 4};
            Step = FindNextOfKinds(Kinds, 3);
        }
    }
    else if (Kind == 6)
    {
        int const Kinds[2] = {0, 6};
        Step = FindNextOfKinds(Kinds, 2);
    }
    return Current + Step;
}

// FUN_00456150. While skipping, each script is run out and the next started; an offer or info
// entry (kinds 1 / 4) ends the skip.
bool FGfScriptSequence::Update(int Ms)
{
    if (bPlaying == 0)
        return false;
    if (bActive == 0)
        return true;

    bool bDone = false;
    if (bSkip != 0)
    {
        while (bSkip != 0)
        {
            if (bDone)
                break;
            Entries[Current].Script->Pause();
            Entries[Current].Script->Update(0);
            if (Entries[Current].Script != nullptr)
                Entries[Current].Script->Teardown();
            bDone = false;
            Current = NextEntryIndex();
            if (Current >= Count)
            {
                Current = -1;
                bDone = true;
            }
            if (!bDone)
            {
                int const Kind = Entries[Current].Kind;
                if (Kind == 1 || Kind == 4)
                    bSkip = 0;
                StartEntry(Current);
            }
        }
    }

    if (!bDone)
    {
        if (!Entries[Current].Script->Update(Ms))
            return bActive == 0;
        if (Entries[Current].Script != nullptr)
            Entries[Current].Script->Teardown();
        int const Next = NextEntryIndex();
        Current = Next;
        if (Next < Count)
        {
            StartEntry(Next);
            return bActive == 0;
        }
        Current = -1;
    }

    StopPlayback(0);
    Current = -1;
    bActive = 0;
    return bActive == 0;
}

// FUN_004562e0.
void FGfScriptSequence::RestartCurrent()
{
    Entries[Current].Script->Restart();
    bPlaying = 1;
}

// FUN_00456300.
void FGfScriptSequence::AdvanceCurrent(float Scale)
{
    if (bPlaying == 0)
        return;
    Entries[Current].Script->Advance(Scale);
}

// =========================================================================================
// FGfScriptManager
// =========================================================================================

// FUN_00456540. operator new(0x13c); the entries are one operator new(0x804) array.
FGfScriptSequence* FGfScriptManager::CreateSequence(FRoomRuntime* Room, unsigned char bFindable)
{
    FGfScriptSequence* const Sequence = new FGfScriptSequence();
    Sequence->Entries = new FGfScriptEntry[0x20];
    Sequence->Capacity = 0x20;
    Sequence->Count = 0;
    Sequence->Room = Room;
    Sequence->bFindable = bFindable;
    Sequences.push_back(Sequence);
    return Sequence;
}

// FUN_00456650. TickClientAudioFade is FUN_00456a40 (the Gf fade); RefreshSound is identity
// slot 21.
void FGfScriptManager::Update(int Ms)
{
    std::list<FGfScriptSequence*>::iterator It = Sequences.begin();
    while (It != Sequences.end())
    {
        FGfScriptSequence* const Sequence = *It;
        if (Sequence->Update(Ms))
        {
            delete Sequence;
            It = Sequences.erase(It);
        }
        else
        {
            ++It;
        }
    }

    float const Seconds = static_cast<float>(static_cast<double>(Ms) * 0.001);
    if (!TickClientAudioFade(Seconds))
        return;
    for (It = Sequences.begin(); It != Sequences.end(); ++It)
    {
        FGfScriptSequence* const Sequence = *It;
        if (Sequence->bSoundsUseFade == 0)
            continue;
        if (Sequence->Current == -1)
            continue;
        FGfScript* const Script = Sequence->Entries[Sequence->Current].Script;
        if (Script == nullptr)
            continue;
        int Index = 0;
        for (Index = 0; Index < Script->SpawnedCount; ++Index)
        {
            if (Script->Spawned[Index] != nullptr)
                Script->Spawned[Index]->RefreshSound();
        }
    }
}

// FUN_00456740. No -1 test on Current: a playing sequence has one.
void FGfScriptManager::DrawEffects(FRoomRuntime* Room, ICamera* Camera)
{
    std::list<FGfScriptSequence*>::iterator It = Sequences.begin();
    for (; It != Sequences.end(); ++It)
    {
        FGfScriptSequence* const Sequence = *It;
        if (Sequence->Room != Room)
            continue;
        if (Sequence->bPlaying == 0)
            continue;
        Sequence->Entries[Sequence->Current].Script->DrawEffects(Camera);
    }
}

// FUN_00456790. MatchesEntity is FUN_00450070.
FThornComponent* FGfScriptManager::FindCreatedEntity(int Type, char const* EntityName)
{
    std::list<FGfScriptSequence*>::iterator It = Sequences.begin();
    for (; It != Sequences.end(); ++It)
    {
        FGfScriptSequence* const Sequence = *It;
        if (Sequence->bFindable == 0)
            continue;
        if (Sequence->Current == -1)
            continue;
        FGfScript* const Script = Sequence->Entries[Sequence->Current].Script;
        if (Script == nullptr)
            continue;
        int Index = 0;
        for (Index = 0; Index < Script->SpawnedCount; ++Index)
        {
            FThornComponent* const Object = Script->Spawned[Index];
            if (Object == nullptr)
                continue;
            if (Object->MatchesEntity(Type, EntityName))
                return Object;
        }
    }
    return nullptr;
}

// LAB_00456840. NnArrivalTextHurry is FUN_0045b3c0.
bool FGfScriptManager::OnEvent(unsigned MsgId)
{
    if (MsgId != USER_CANCEL)
        return false;
    bool bAny = false;
    std::list<FGfScriptSequence*>::iterator It = Sequences.begin();
    for (; It != Sequences.end(); ++It)
    {
        FGfScriptSequence* const Sequence = *It;
        if (Sequence->bLetterbox == 0)
            continue;
        if (Sequence->bPlaying != 0 && Sequence->bSkippable != 0)
            Sequence->bSkip = 1;
        bAny = true;
    }
    if (!bAny)
        return false;
    NnArrivalTextHurry();
    return true;
}

// FUN_004568a0.
bool FGfScriptManager::CanSkipAll() const
{
    std::list<FGfScriptSequence*>::const_iterator It = Sequences.begin();
    for (; It != Sequences.end(); ++It)
    {
        if ((*It)->bLetterbox != 0 && (*It)->bSkippable == 0)
            return false;
    }
    return true;
}

// FUN_004568d0.
void FGfScriptManager::DestroySequence(FGfScriptSequence* Sequence)
{
    std::list<FGfScriptSequence*>::iterator It = Sequences.begin();
    for (; It != Sequences.end(); ++It)
    {
        if (*It == Sequence)
        {
            if (Sequence != nullptr)
                delete Sequence;
            Sequences.erase(It);
            return;
        }
    }
}

// FUN_004563d0. Retail's second erase pass and the head free are the std::list destructor;
// FUN_00576010 is ~EventHandler.
FGfScriptManager::~FGfScriptManager()
{
    std::list<FGfScriptSequence*>::iterator It = Sequences.begin();
    while (It != Sequences.end())
    {
        if (*It != nullptr)
            delete *It;
        It = Sequences.erase(It);
    }
}

// FUN_00456930.
void FGfScriptManager::DestroyAll()
{
    std::list<FGfScriptSequence*>::iterator It = Sequences.begin();
    while (It != Sequences.end())
    {
        if (*It != nullptr)
            delete *It;
        It = Sequences.erase(It);
    }
}

// FUN_004564d0 -- FUN_005760d0(0xf) on the handler face.
void FGfScriptManager::RegisterEvents()
{
    RegisterEvent(USER_CANCEL);
}

// FUN_004564e0 -- FUN_00456930's body, then FUN_005760f0(0xf).
void FGfScriptManager::Shutdown()
{
    DestroyAll();
    UnregisterEvent(USER_CANCEL);
}

// =========================================================================================

// FUN_0044fa60. No check for a -1 instance on the way (the one caller tests the result
// afterwards).
InstanceIndex GfEngineInstanceAncestor(InstanceIndex Instance, int Levels)
{
    InstanceIndex Current = Instance;
    if (Levels > 0)
    {
        int Remaining = Levels;
        do
        {
            Current = DALib::Engine->GetInstanceParent(Current);
            --Remaining;
        } while (Remaining != 0);
    }
    return Current;
}
