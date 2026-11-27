// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "x86math.h"
#include "EngBase.h"
#include "readfile.h"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>

static GENRESULT DACOM_API AnimationQueryInterface(FAnimationObject* This, const char* Iid,
                                                  void** Out);

static_assert(offsetof(FAnimationObject, NextPlaybackId) == 0x10, "Animation.NextPlaybackId");
static_assert(offsetof(FAnimationObject, Playbacks) == 0x14, "Animation.Playbacks");
static_assert(offsetof(FAnimationObject, Channels) == 0x28, "Animation.Channels");
static_assert(offsetof(FAnimationObject, Controller) == 0x50, "Animation.Controller");
static_assert(offsetof(FAnimationObject, Inner) == 0x54, "Animation.Inner");
static_assert(sizeof(FAnimationObject) == G_AnimationObjectSize, "Animation object size");

static void DestroyAnimationLists(FAnimationObject* Object);

static unsigned char FindPlaybackRecord(FAnimationObject* Animation, unsigned long Key,
                                        FAnimPlayback** Out);

static FAnimScript* AllocAnimScript()
{
    return new (std::nothrow) FAnimScript();
}

static FAnimScript* const G_InvalidAnimScript =
    (FAnimScript*)((ptrdiff_t)(-1));

void* DACOM_API FAnimationObject::OpenScript(IFileSystem* FileSystem)
{
    FAnimScript* Script = AllocAnimScript();
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameAnimation) == 0)
    {
        Script->Release();
        return G_InvalidAnimScript;
    }
    LoadAnimScriptNames(Script, FileSystem);
    Fs->OpenNode(G_NameParentDot);
    return Script;
}

void* DACOM_API FAnimationObject::CreateEmptyScript()
{

    return AllocAnimScript();
}

static bool IsValidAnimScript(FAnimScript* Script)
{
    return Script != G_InvalidAnimScript && Script != nullptr;
}

unsigned long DACOM_API FAnimationObject::AddScriptNames(void* InScript, IFileSystem* FileSystem,
                                                       const char** Names, int Count)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameAnimation) == 0)
        return 0xFFFFFFFF;
    const unsigned long Result = LoadAnimScriptsByNameList(Script, FileSystem, Names, Count);
    Fs->OpenNode(G_NameParentDot);
    return Result;
}

static bool RemoveAnimNameByCrc(FAnimScript* Script, unsigned long Crc)
{
    return Script->Names.erase(Crc) != 0;
}

unsigned long DACOM_API FAnimationObject::RemoveScriptNames(void* InScript, const char** Names, int Count)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    for (int Index = 0; Index < Count; ++Index)
    {
        const char* Name = Names[Index];
        if (Name == nullptr)
            continue;
        if (!RemoveAnimNameByCrc(Script, DACOM_CRC::GetCRC32(Name)))
        {
            char Message[0x2000];
            FormatFdumpMessage(Message,"Failed to remove animation \"%s\".\n", Name);
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x1D8, "WARNING", Message);
        }
    }
    return 0;
}

unsigned long DACOM_API FAnimationObject::GetScriptNameCount(void* InScript, unsigned long* Out)
{
    FAnimScript* Script = (FAnimScript*)InScript;

    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    *Out = (unsigned long)(Script->Names.size());
    return 0;
}

GENRESULT DACOM_API FAnimationObject::CloseScript(void* InScript)
{
    FAnimScript* Script = (FAnimScript*)InScript;

    if (!IsValidAnimScript(Script))
        return (GENRESULT)((size_t)Script);
    return (GENRESULT)(Script->Release());
}

float DACOM_API FAnimationObject::GetNamedDuration(void* InScript, const char* Name)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    if (!IsValidAnimScript(Script))
        return -1.0f;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return -1.0f;
    float Duration = -1.0f;
    Entry->LongestChannel->GetDuration(&Duration);
    return Duration;
}

static unsigned char FindPlaybackRecord(FAnimationObject* Animation, unsigned long Key,
                                        FAnimPlayback** Out)
{
    FAnimPlaybackMap::iterator It = Animation->Playbacks->find((long)Key);
    if (It == Animation->Playbacks->end())
        return 0;
    *Out = &It->second;
    return 1;
}

// LAB_06614860 (IAnimation2+0x3C): FUN_066129c0. The interface carries the time as void*.
unsigned long DACOM_API FAnimationObject::StopChannels(unsigned long PlaybackId, void* Time)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) != 0)
        Playback->SetTypedTime(FloatFromOpaqueDword(Time));
    return 0;
}

float DACOM_API FAnimationObject::GetStoredDuration(unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return -1.0f;
    return Playback->StoredDuration();
}

unsigned long DACOM_API FAnimationObject::FilterObjects(void* Filter)
{
    unsigned long PlaybackId = (unsigned long)((size_t)Filter);
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0;
    IChannel* ChannelFacet = (IChannel*)this;
    return Playback->FilterTyped(ChannelFacet);
}

// LAB_06614920 (IAnimation2 slot 9). Sink / SinkContext become every bound channel's
// IChannelSink and its context (typed +0x0C / +0x10); a root-bound (event) track needs a sink.
unsigned long DACOM_API FAnimationObject::PlayScript(void* InScript, InstanceIndex InRoot, const char* Name,
                                                      void* Sink, void* SinkContext)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    FEngineInstance* Root = (FEngineInstance*)InRoot;
    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return 0xFFFFFFFF;
    FAnimPlayback Record;
    Record.Name = nullptr;                                         // FUN_06611f60
    Record.Root = (FEngineInstance*)((ptrdiff_t)(-1));
    Record.LongestTyped = nullptr;
    Record.Script = nullptr;
    Record.CloneTypedChannels(Entry);
    Record.Script = Script;
    Record.BindTracks(Root, Sink, SinkContext);
    std::vector<FTypedChannel*>::iterator Cursor = Record.Typed.begin();
    while (Cursor != Record.Typed.end())
    {
        Channels->insert(*Cursor);
        ++Cursor;
    }
    Script->AddRef();
    const unsigned long Id = NextPlaybackId;
    NextPlaybackId = Id + 1;
    Playbacks->insert(FAnimPlaybackMap::value_type((long)Id, Record));
    return Id;
}

GENRESULT DACOM_API FAnimationObject::DestroyScript(void* InScript)
{
    unsigned long PlaybackId = (unsigned long)((size_t)InScript);
    FAnimPlaybackMap::iterator It = Playbacks->find((long)PlaybackId);
    if (It == Playbacks->end())
        return GR_OK;
    FAnimPlayback* Playback = &It->second;
    FAnimScript* Script = Playback->Script;
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    while (Cursor != Playback->Typed.end())
    {
        MixerUnbindTyped(this, *Cursor);
        Channels->erase(*Cursor);
        ++Cursor;
    }
    Playback->FreeTyped();
    Playbacks->erase(It);
    Script->Release();
    return GR_OK;
}

static FAnimationObject* AnimationFromNested(void* Nested, unsigned long Offset)
{
    return (FAnimationObject*)((unsigned char*)Nested - Offset);
}

// LAB_06614b30 (IRootData+0x0C).
unsigned char DACOM_API FAnimationObject::Eval(unsigned long PlaybackId, Vector* OutSampledPos,
                                               unsigned long Flags, float Time, float Speed,
                                               void* BlendDuration, void* Scale,
                                               float TranslationScale)
{
    void* RootThis = (IRootData*)this;
    // The IRootData facet sits at Animation+0xC.
    FAnimationObject* This = AnimationFromNested(RootThis, 0xC);
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    unsigned long TranslationScaleBits = 0;
    std::memcpy(&TranslationScaleBits, &TranslationScale, sizeof(TranslationScaleBits));
    const unsigned long Eval = Playback->EvalTyped(Flags, Time, Speed, (unsigned long)((size_t)BlendDuration),
        (unsigned long)((size_t)Scale), TranslationScaleBits, nullptr);
    if (Eval == 0xFFFFFFFF)
        return 0;
    // The world position lands in a 12-byte local of this frame and is dropped; the sampled
    // (local) position goes to the caller.
    Vector WorldPos;
    std::memset(&WorldPos, 0, sizeof(WorldPos));
    unsigned char bTouch = 0;
    if (Playback->TouchMappedTyped(&WorldPos, OutSampledPos) != 0)
        bTouch = 1;
    Playback->StopTyped();
    return bTouch;
}

// LAB_06614bb0 (IAnimation2+0x2C): FUN_066126e0, then every channel joins its mixer with the
// blend duration. The last four arguments are play header [3]..[6], carried as dwords.
unsigned long DACOM_API FAnimationObject::EvalPlayback(unsigned long PlaybackId, unsigned long Flags,
                                                     float Time, float Speed, void* BlendDuration,
                                                     void* Scale, void* TranslationScale,
                                                     const Transform* Xform)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    const unsigned long Result = Playback->EvalTyped(Flags, Time, Speed, (unsigned long)((size_t)BlendDuration),
        (unsigned long)((size_t)Scale),
        (unsigned long)((size_t)TranslationScale),
        Xform);
    if (Result == 0xFFFFFFFF)
        return Result;
    const float BlendTime = FloatFromOpaqueDword(BlendDuration);
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        MixerBlendAttach(this, Playback->Typed[Index], BlendTime, Flags);
        Index += 1;
    }
    return Result;
}

// LAB_06614c40 (IAnimation2+0x30): FUN_066127e0.
unsigned long DACOM_API FAnimationObject::EvalStored(unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    return Playback->EvalStoredTyped();
}

// LAB_06614c70 (IAnimation2+0x38): FUN_066128f0, then every channel leaves its mixer
// (FUN_06615ec0).
unsigned long DACOM_API FAnimationObject::StopPlayback(unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0;
    Playback->StopTyped();
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        MixerUnbindTyped(this, Playback->Typed[Index]);
        Index += 1;
    }
    return 0;
}

// LAB_06614cc0 (IAnimation2+0x34): FUN_06612920.
unsigned long DACOM_API FAnimationObject::ApplyChannels(unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) != 0)
        Playback->ApplyTyped();
    return 0;
}

GENRESULT DACOM_API FAnimationObject::EnumScriptNames(FAnimNameProc Callback, void* InScript, void* Context)
{
    FAnimScript* Script = (FAnimScript*)InScript;

    if (Callback == nullptr || !IsValidAnimScript(Script))
        return GR_OK;
    FAnimNameProc Enum = Callback;
    FAnimNameMap::iterator It = Script->Names.begin();
    while (It != Script->Names.end())
    {
        Enum(It->second.Name, Context);
        ++It;
    }
    return GR_OK;
}

GENRESULT DACOM_API FAnimationObject::FindScriptName(FAnimChannelProc Callback, void* InScript, const char* Name,
                                                     void* Context)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    const GENRESULT Miss = GR_INVALID_PARMS;
    if (Callback == nullptr || !IsValidAnimScript(Script))
        return Miss;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return Miss;
    FAnimChannelProc Enum = Callback;
    std::vector<FAnimTrack>::iterator Track = Entry->Tracks.begin();
    while (Track != Entry->Tracks.end())
    {
        if (Track->Channel != nullptr)
        {
            ChannelArchetypeAddRef(Track->Channel);
            Enum((IChannelArchetype*)(Track->Channel), Context);
        }
        ++Track;
    }
    return 0;
}

// LAB_06614de0 (IAnimation2+0x48): the playback's root entry in the mixer map
// (FUN_06614f70 on playback +4); when there is one, FUN_06612a70 writes the position into the
// channels and the object overlays.
unsigned long DACOM_API FAnimation2Facet::SetOverlayPos(unsigned long PlaybackId, const Vector* Pos)
{
    FAnimationObject* This = (FAnimationObject*)this;
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    FMixerDescMap* Inner =
        This->MixerFindKey((long)((ptrdiff_t)(Playback->Root)));
    if (Inner != nullptr)
        Playback->WriteOverlayPos(Pos, Inner);
    return 0;
}

// LAB_06614fd0 (IAnimation2+0x4C): as above with FUN_06612b80 (orientation matrix).
unsigned long DACOM_API FAnimation2Facet::SetOverlayExtra(unsigned long PlaybackId, const Matrix* Extra)
{
    FAnimationObject* This = (FAnimationObject*)this;
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    FMixerDescMap* Inner =
        This->MixerFindKey((long)((ptrdiff_t)(Playback->Root)));
    if (Inner != nullptr)
        Playback->WriteOverlayOrient(Extra, Inner);
    return 0;
}

unsigned long DACOM_API FAnimationObject::GetNamedDurationOut(void* InScript, const char* Name, float* Out)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    if (!IsValidAnimScript(Script))
        return 0;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return 0;
    if (Entry->bHasRootHeight == 0)
        return 0;
    *Out = Entry->RootHeight;
    return 1;
}

unsigned long DACOM_API FAnimationObject::HasObject(unsigned long PlaybackId, void* Object)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0;
    return Playback->HasTyped(Object);
}

// LAB_066150e0 (IAnimation2+0x54): lookup FUN_06614580, then FUN_066122d0.
unsigned long DACOM_API FAnimationObject::TouchMapped(unsigned long PlaybackId, Vector* OutPos,
                                                      Vector* OutSampledPos)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0;
    return Playback->TouchMappedTyped(OutPos, OutSampledPos);
}

// LAB_06615120 (IAnimation2+0x58): as SetOverlayPos with FUN_06612c60 (add a delta).
unsigned long DACOM_API FAnimation2Facet::AddOverlayPos(unsigned long PlaybackId, const Vector* Delta)
{
    FAnimationObject* This = (FAnimationObject*)this;
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    FMixerDescMap* Inner =
        This->MixerFindKey((long)((ptrdiff_t)(Playback->Root)));
    if (Inner != nullptr)
        Playback->AddOverlayPos(Delta, Inner);
    return 0;
}

// Slots 5, 10 and 17 take a playback id, not a script (FUN_06615160 / LAB_06614a50 /
// LAB_066148d0 all open with FUN_06614580 or FUN_06612e70 on the +0x14 map); the shared
// header still carries the dword as void*.
float DACOM_API FAnimationObject::GetScriptDuration(void* Script)
{
    unsigned long PlaybackId = (unsigned long)((size_t)Script);
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0.0f;
    float Duration = -1.0f;
    Playback->Name->LongestChannel->GetDuration(&Duration);
    return Duration;
}

unsigned long DACOM_API FAnimationObject::GetFirstInstanceField(unsigned long PlaybackId,
                                                               unsigned long* Out)
{
    FAnimPlaybackMap::iterator It = Playbacks->find((long)PlaybackId);
    if (It == Playbacks->end())
        return 0;
    FAnimNameMap& Names = It->second.Script->Names;
    FAnimNameMap::iterator Cursor = Names.begin();
    while (Cursor != Names.end())
    {
        if (&Cursor->second == It->second.Name)
        {
            *Out = (unsigned long)((size_t)(Cursor->second.Name));
            return 1;
        }
        ++Cursor;
    }
    return 0;
}

unsigned long DACOM_API FAnimationObject::GetDurationIfNamed(unsigned long PlaybackId, float* Out)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0;
    if (Playback->Name == nullptr)
        return 0;
    if (Playback->Name->bHasRootHeight == 0)
        return 0;
    *Out = Playback->Name->RootHeight;
    return 1;
}

unsigned long DACOM_API FAnimationObject::FindObject(unsigned long PlaybackId, void* Query)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    return (unsigned long)(
        Playback->FindTypedByHeader((const FAnimBindOut*)Query));
}

// LAB_066157f0 (IAnimation2+0x70): every mixer list, then every playing channel no
// mixer leaf ticked.
unsigned long DACOM_API FAnimationObject::MixerWalkAll(float Time)
{
    ::MixerWalkAll(this, Time);
    return 0;
}

// .data:0662a208 (from the PE bytes): the __FILE__ that the
// "Unknown data type!" sites of IChannel::CreateTyped (06615a5f, line 0x44D) and
// CreateFromObject (06615c16, line 0x479) push.
static const char* const G_FdumpFileAnimComponent =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Anim\\AnimComponent.cpp";

// The shared tail of IChannel::CreateTyped and CreateFromObject.
static unsigned long ChannelBindAndInsert(FAnimationObject* Animation, FTypedChannel* Typed,
                                          void* BindA, void* BindB, unsigned char bFreeOnRefusal)
{
    const unsigned long bBound =
        Typed->BindMap((unsigned long*)BindA, BindB);
    if ((bBound & 0xFF) == 0)
    {
        if (bFreeOnRefusal != 0)
        {
            if (Typed->Archetype != nullptr)
            {
                Typed->Archetype->Release();
                Typed->Archetype = nullptr;
            }
            ::operator delete(Typed); // FUN_066281d0; nonvirtual base, trivial tails.
        }
        return 0xFFFFFFFF;
    }
    Animation->Channels->insert(Typed);
    return (unsigned long)((size_t)Typed);
}

// LAB_06615950 (IChannel+0x0C).
unsigned long DACOM_API FAnimationChannelFacet::CreateTyped(void* Owner, unsigned long Type,
                                                            void* BindA, void* BindB)
{
    void* ChannelThis = this;
    if (Owner == nullptr)
        return 0xFFFFFFFF;
    if (!IsTypedChannelType(Type))
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileAnimComponent, 0x44D, "FATAL", "Unknown data type!\n");
        // Retail's invalid-type continuation reinterprets the owner as a channel.
        const unsigned long bBound =
            ((FTypedChannel*)Owner)->BindMap((unsigned long*)BindA, BindB);
        if ((bBound & 0xFF) == 0)
        {
            IDAComponent** Held =
                (IDAComponent**)((unsigned char*)Owner + 0x24);
            if (*Held != nullptr)
            {
                (*Held)->Release();
                *Held = nullptr;
            }
            ::operator delete(Owner);                            // FUN_066281d0
            return 0xFFFFFFFF;
        }
        AnimationFromNested(ChannelThis, 8)->Channels->insert((FTypedChannel*)Owner);
        return (unsigned long)((size_t)Owner);
    }
    FTypedChannel* Typed = NewTypedChannelWithOwner(Type, (IChannelOwner*)Owner);
    return ChannelBindAndInsert(AnimationFromNested(ChannelThis, 8), Typed, BindA, BindB, 1);
}

// LAB_06615b00 (IChannel+0x10): the type is the archetype's flags.
unsigned long DACOM_API FAnimationChannelFacet::CreateFromObject(void* Object, void* BindA,
                                                                 void* BindB)
{
    void* ChannelThis = this;
    if (Object == nullptr)
        return 0xFFFFFFFF;
    FChannelArchetypeObject* Archetype =
        (FChannelArchetypeObject*)((IChannelArchetype*)Object);
    unsigned long Type = 0;
    Archetype->GetFlags(&Type);
    if (!IsTypedChannelType(Type))
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileAnimComponent, 0x479, "FATAL", "Unknown data type!\n");
        return 0xFFFFFFFF;
    }
    FTypedChannel* Typed = NewTypedChannelWithArchetype(Type, Archetype);
    return ChannelBindAndInsert(AnimationFromNested(ChannelThis, 8), Typed, BindA, BindB, 0);
}

static FTypedChannel* FindTypedChannel(FAnimationObject* Animation, unsigned long Key)
{
    if (Key == 0xFFFFFFFF)
        return nullptr;
    FTypedChannel* Wanted = (FTypedChannel*)((size_t)Key);
    FTypedChannelSet::iterator It = Animation->Channels->find(Wanted);
    if (It == Animation->Channels->end())
        return nullptr;
    return *It;
}

// LAB_06615d80 (IChannel+0x18): the archetype is at +0x24. A miss leaves Out untouched.
unsigned long DACOM_API FAnimationChannelFacet::GetObject(unsigned long Key, void** Out)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return 0xFFFFFFFF;
    *Out = (IChannelArchetype*)(Typed->Archetype);
    if (Typed->Archetype != nullptr)
        Typed->Archetype->AddRef();
    return 0;
}

// FUN_06615e10 (IChannel+0x14). Retail frees the channel before erasing its pointer from the
// set (FUN_06617c90).
unsigned long DACOM_API FAnimationChannelFacet::Destroy(unsigned long Key)
{
    void* ChannelThis = this;
    FAnimationObject* Animation = AnimationFromNested(ChannelThis, 8);
    FTypedChannel* Typed = FindTypedChannel(Animation, Key);
    if (Typed == nullptr)
        return 0xFFFFFFFF;
    MixerUnbindTyped(Animation, Typed);                          // FUN_06615ec0
    if (Typed->Archetype != nullptr)
    {
        Typed->Archetype->Release();
        Typed->Archetype = nullptr;
    }
    ::operator delete(Typed); // FUN_066281d0; nonvirtual base, trivial tails.
    Animation->Channels->erase(Typed);
    return 0;
}

// LAB_06616430 (IChannel+0x1C): rebind the channel with the caller's play header; the play bit
// follows the bind result, and a playing channel joins its mixer.
unsigned long DACOM_API FAnimationChannelFacet::Play(unsigned long Key, float* Time)
{
    void* ChannelThis = this;
    if (Key == 0xFFFFFFFF || Time == nullptr)
        return 0;
    FAnimationObject* Animation = AnimationFromNested(ChannelThis, 8);
    FTypedChannel* Typed = FindTypedChannel(Animation, Key);
    if (Typed == nullptr)
        return 0;
    const FPlayHeader* Header = (const FPlayHeader*)Time;
    const unsigned long Bound = Typed->TryBindIfTimeValid(Header);
    Typed->PlayState = (unsigned char)(
        ((Typed->PlayState ^ (unsigned char)Bound) & 1) ^ Typed->PlayState);
    const unsigned char bPlaying = Typed->PlayState & 1;
    if (bPlaying != 0)
        MixerBlendAttach(Animation, Typed, Header->BlendDuration, Header->LoopFlags);
    return bPlaying;
}

// FUN_066164d0 (IChannel+0x20): a playing channel (PlayState bit 0) is posed where it stands
// and stops playing.
unsigned long DACOM_API FAnimationChannelFacet::StopGraph(unsigned long Key)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr || (Typed->PlayState & G_PlayStatePlaying) == 0)
        return 0;
    Typed->ApplyFromArchetype();
    Typed->PlayState &= ~G_PlayStatePlaying;
    return 1;
}

// LAB_06616550 (IChannel+0x24): typed +0x1C, the channel's current time despite the slot's name.
float DACOM_API FAnimationChannelFacet::GetDuration(unsigned long Key)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return -1.0f;
    return Typed->Time;
}

// FUN_066165d0 (IChannel+0x28): typed slot 0x10 (SetTime).
unsigned long DACOM_API FAnimationChannelFacet::Blend(unsigned long Key, float Duration)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return 0;
    return Typed->SetTime(Duration);
}

// FUN_06616640 (IChannel+0x2C): the dword goes to typed +0x2C (Scale).
GENRESULT DACOM_API FAnimationChannelFacet::SetUser(unsigned long Key, unsigned long User)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    std::memcpy(&Typed->Scale, &User, sizeof(Typed->Scale));
    return GR_OK;
}

// FUN_066166a0 (IChannel+0x30): typed +0x2C (Scale).
float DACOM_API FAnimationChannelFacet::GetUser(unsigned long Key)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return 0.0f;
    return Typed->Scale;
}

// FUN_06616720 (IChannel+0x34): typed slot 0x20 and nothing else -- unlike IAnimation2's playback setter (FUN_06612a70), no mixer overlay is touched here.
GENRESULT DACOM_API FAnimationChannelFacet::SetOverlayPos(unsigned long Key, const Vector* Pos)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    Typed->SetOverlayPos(Pos);
    return GR_OK;
}

// FUN_06616790 (IChannel+0x38): typed slot 0x24 only.
GENRESULT DACOM_API FAnimationChannelFacet::SetOverlayExtra(unsigned long Key, const Matrix* Extra)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    Typed->SetOverlayExtra(Extra);
    return GR_OK;
}

// LAB_06616800 (IAnimation2+0x6C): walk every mixer list under one bind key
// -- a playback's root, or the cookie an IChannel caller bound with. Not a playback id.
unsigned long DACOM_API FAnimationObject::MixerWalk(unsigned long Key, float Time)
{
    MixerWalkKey(this, (long)Key, Time);
    return 0;
}

// FUN_066168f0 (IChannel+0x3C): 1 when the channel is a leaf of its mixer list.
unsigned long DACOM_API FAnimationChannelFacet::InMixer(unsigned long Key)
{
    void* ChannelThis = this;
    FAnimationObject* Animation = AnimationFromNested(ChannelThis, 8);
    FTypedChannel* Typed = FindTypedChannel(Animation, Key);
    if (Typed == nullptr)
        return 0;
    return MixerHasLeaf(Animation, Typed);
}

// LAB_066169b0 (IChannel+0x40): Out always receives 16 bytes, the defaults on a miss.
unsigned long* DACOM_API FAnimationChannelFacet::GetHeader(unsigned long* Out, unsigned long Key)
{
    void* ChannelThis = this;
    unsigned long Default[4];
    Default[0] = 2;
    Default[1] = 0;
    Default[2] = 0;
    Default[3] = 0xFFFFFFFF;
    const unsigned long* Source = Default;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed != nullptr)
        Source = (const unsigned long*)(TypedBindDesc(Typed));
    std::memcpy(Out, Source, sizeof(Default));
    return Out;
}

// FUN_06616a50 (IChannel+0x44): typed slot 0x28 only.
GENRESULT DACOM_API FAnimationChannelFacet::AddOverlayPos(unsigned long Key, const Vector* Delta)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    Typed->AddOverlayPos(Delta);
    return GR_OK;
}

// FUN_06616ac0 (IChannel+0x48): Rate is +0x28; unordered counts as forward. A miss leaves
// Out untouched.
GENRESULT DACOM_API FAnimationChannelFacet::GetMapped(unsigned long Key, unsigned char* Out)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_INVALID_PARMS;
    *Out = Typed->Rate < 0.0f ? 0 : 1;
    return GR_OK;
}

unsigned long DACOM_API FAnimationObject::EnumFsScripts(FAnimNameProc Callback, IFileSystem* FileSystem, void* Context)
{

    if (FileSystemOpenNode(FileSystem, G_NameAnimation) == 0)
        return 0xFFFFFFFF;

    if (FileSystemOpenNode(FileSystem, G_NameScript) == 0)
    {
        FileSystemOpenNode(FileSystem, G_NameParentDot);
        return 0xFFFFFFFF;
    }

    WIN32_FIND_DATAA FindData = {};
    const int FindHandle = FileSystemFindFirst(FileSystem, G_NameStar, &FindData);
    if (FindHandle != -1)
    {
        FAnimNameProc Enum = Callback;
        do
        {

            if (FindData.cFileName[0] != '.' &&
                (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                Enum(FindData.cFileName, Context);
            }
        } while (FileSystemFindNext(FileSystem, FindHandle, &FindData) != 0);

        AsFileSystem(FileSystem)->GetFileSystemError();
        FileSystemFindClose(FileSystem, FindHandle);
    }
    FileSystemOpenNode(FileSystem, G_NameParentDot);
    FileSystemOpenNode(FileSystem, G_NameParentDot);
    return 0;
}

unsigned long DACOM_API FAnimationObject::HasScriptName(void* InScript, const char* Name)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    if (!IsValidAnimScript(Script) || Name == nullptr)
        return 0xFFFFFFFF;

    const unsigned long Crc = DACOM_CRC::GetCRC32(Name);
    if (Script->Names.find(Crc) != Script->Names.end())
        return 0;
    return 0xFFFFFFFF;
}

unsigned long DACOM_API FAnimationObject::AddRef()
{
    return Controller->AddRef();
}

unsigned long DACOM_API FAnimationObject::Release()
{
    return Controller->Release();
}

// Shared engine cache; Engine2 has its own QI output slot.
FEngineObject* G_AnimEngine = nullptr;
static FEngineObject* G_AnimEngine2 = nullptr;

// LAB_06616d40
GENRESULT DACOM_API FAnimationObject::Initialize()
{
    void* Nested = (IAggregateComponent*)this;
    FAnimationObject* Object = AnimationFromNested(Nested, 4);
    GENRESULT Result =
        AnimationQueryInterface(Object, Iids::Engine, (void**)(&G_AnimEngine));
    if (Result != 0)
        return Result;
    G_AnimEngine->Release();
    Result =
        AnimationQueryInterface(Object, Iids::Engine2, (void**)(&G_AnimEngine2));
    if (Result != 0)
        return Result;
    G_AnimEngine2->Release();
    return GR_OK;
}

unsigned long DACOM_API FAnimationObject::EnumObjects(FAnimObjectProc Callback, unsigned long PlaybackId,
                                                       void* Context)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    FAnimObjectProc Enum = Callback;
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    while (Cursor != Playback->Typed.end())
    {
        if (Enum(*Cursor, Context) == 0)
            break;
        ++Cursor;
    }
    return 0;
}

unsigned long DACOM_API FAnimationObject::RegisterHandle(unsigned long Key)
{
    return RegisterAnimHandle(Key);
}

unsigned long DACOM_API FAnimationObject::HasHandle(unsigned long Key, unsigned char* Out)
{
    if (Out != nullptr)
        *Out = FindAnimHandle(Key) ? 1 : 0;
    return 0;
}

unsigned long DACOM_API FAnimationObject::HasAnyHandle(unsigned long Key)
{
    if (!FindAnimHandle(Key))
        return 0xFFFFFFFF;
    UnregisterAnimHandle(Key);
    return 0;
}

static void DestroyAnimationLists(FAnimationObject* Object)
{

    while (G_AnimHandleHead != nullptr)
    {
        FAnimHandleNode* Next = G_AnimHandleHead->Next;
        std::free(G_AnimHandleHead);
        G_AnimHandleHead = Next;
    }

    Object->MixerDestroyAll();
    delete Object->Channels;
    Object->Channels = nullptr;

    delete Object->Playbacks;
    Object->Playbacks = nullptr;
}

// FUN_0661a750. No checks of This / Desc / Out.
GENRESULT DACOM_API AnimationFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    if (Desc->size != G_AggDescSizeAnimation || std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FAnimationObject* Object = (FAnimationObject*)(std::calloc(1, sizeof(FAnimationObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }

    new (Object) FAnimationObject(); // the four facet tables and the inner unknown's table
    Object->NextPlaybackId = G_AnimFirstPlaybackId;
    // Retail constructs the three trees in place (FUN_06616c10); see FAnimationObject.
    Object->Playbacks = new FAnimPlaybackMap;
    Object->Channels = new FTypedChannelSet;
    Object->Mixers = new FMixerKeyMap;
    // Inner unknown {table 0662946c (installed by the placement new above), 1, object}.
    Object->Inner.RefCount = 1;
    Object->Inner.Object = Object;

    FAggDesc18* Agg = (FAggDesc18*)Desc;
    IDAComponent* const InnerUnknown = &Object->Inner;
    Object->Controller = (IDAComponent*)(Agg->Inner);
    if (Agg->Inner != nullptr)
        *Agg->Outer = InnerUnknown;
    else
        Object->Controller = InnerUnknown;

    const int HookResult = EngBaseAggregationHook(Desc);
    if (HookResult != 0)
    {
        if (Agg->Outer != nullptr)
            *Agg->Outer = nullptr;
        DestroyAnimationLists(Object);
        std::free(Object);
        *Out = nullptr;
        return (GENRESULT)HookResult;
    }

    *Out = Object;
    return GR_OK;
}

// The {iid, offset} walk both inner unknowns inline (LAB_0661a880, LAB_06627e30).
GENRESULT InnerQueryTable(void* Object, const char* Iid, void** Out,
                          const FInnerIidEntry* Table)
{
    for (int Index = 0; Table[Index].Iid != nullptr; ++Index)
    {
        if (std::strcmp(Table[Index].Iid, Iid) != 0)
            continue;
        unsigned char* Base = (unsigned char*)Object;
        const long Offset = Table[Index].Offset;
        IDAComponent* Interface = nullptr;
        if (Offset < 0)
            std::memcpy(&Interface, Base + ((unsigned long)Offset & 0x7FFFFFFFu),
                        sizeof(Interface));
        else
            Interface = (IDAComponent*)(Base + Offset);
        Interface->AddRef();
        *Out = Interface;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

// Inner QI table @ 06629478 (PE bytes): {IAnimation2, 0}, {IAnimation, 0}, {IChannel, 8},
// {IRootData, 0xC}, {IAggregateComponent, 4}, then a null iid.
static const FInnerIidEntry G_AnimationInnerIids[] = {
    {Iids::Animation2, 0},
    {Iids::Animation, 0},
    {Iids::Channel, 0x08},
    {Iids::RootData, 0x0C},
    {Iids::AggregateComponent, 0x04},
    {nullptr, 0},
};

// LAB_0661a880 (inner QI, RET 0xC) over 06629478; the facet offsets are added to the object
// pointer at inner +8.
GENRESULT DACOM_API FAnimationInner::QueryInterface(const char* Iid, void** Out)
{
    return InnerQueryTable(Object, Iid, Out, G_AnimationInnerIids);
}

// LAB_06627ef0, ICF-shared by six inner/factory vtables. No null check of this.
unsigned long DACOM_API FAnimationInner::AddRef()
{
    RefCount += 1;
    return RefCount;
}

// LAB_0661a940. Same shape as FHardpointInner::Release; object pointer at +8, dtor FUN_06616d90.
unsigned long DACOM_API FAnimationInner::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    FAnimationObject* Owner = Object;
    if (Owner != nullptr)
    {
        DestroyAnimationLists(Owner);
        std::free(Owner);
    }
    return 0;
}

// LAB_0661a980 / LAB_0661a990 / LAB_0661a9a0 (IAnimation2 slots 0-2, also reached from the
// three facet thunks): forward to the controller at +0x50. No checks.
static GENRESULT DACOM_API AnimationQueryInterface(FAnimationObject* This, const char* Iid,
                                                  void** Out)
{
    return This->Controller->QueryInterface(Iid, Out);
}

GENRESULT DACOM_API FAnimationObject::QueryInterface(const char* Iid, void** Out)
{
    return AnimationQueryInterface(this, Iid, Out);
}

