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


// FUN_06611190: From itself, then its descendants through IEngine GetRelatedInstance.
static FEngineInstance* AnimEngineFindInstanceByName(FEngineInstance* From, const char* Name)
{
    FEngineInstance* const Invalid =
        (FEngineInstance*)((ptrdiff_t)(-1));
    FArchetypeName Out;
    const char* Text = G_AnimEngine->GetInstanceName(&Out, (InstanceIndex)From)->Text;
    if (Text == nullptr)
        Text = "";
    if (std::strcmp(Text, Name) == 0)
        return From;
    FEngineInstance* Cursor =
        (FEngineInstance*)(G_AnimEngine->GetRelatedInstance((InstanceIndex)From, 0, (InstanceIndex)Invalid));
    while (Cursor != Invalid)
    {
        Text = G_AnimEngine->GetInstanceName(&Out, (InstanceIndex)Cursor)->Text;
        if (Text == nullptr)
            Text = "";
        if (std::strcmp(Text, Name) == 0)
            return Cursor;
        Cursor = (FEngineInstance*)(G_AnimEngine->GetRelatedInstance((InstanceIndex)From, 0, (InstanceIndex)Cursor));
    }
    return Invalid;
}

// FUN_06611290 (thiscall on the track, which it does not use). No checks.
static unsigned long ReadUtfNameCrc(void* FileSystem, const char* Name, char** Out)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Name, nullptr);
    const int Handle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (Handle != -1)
    {
        const unsigned long Size = Fs->GetSize((unsigned long)Handle, 0); // +0x28
        *Out = (char*)(std::malloc(Size));
        unsigned long BytesRead = 0;
        Fs->Read((unsigned long)Handle, *Out, Size, &BytesRead, 0); // +0x14
        Fs->CloseFile((unsigned long)Handle); // +0x10
    }
    return DACOM_CRC::GetCRC32(*Out);
}

// FUN_06611350. No checks.
void FAnimTrack::LoadChannelNode(IFileSystem* FileSystem)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameChannel) == 0) // +0x60
        return;
    if (Channel != nullptr)
    {
        Channel->Release();
        Channel = nullptr;
    }
    FAggDesc8 Desc;
    Desc.Size = G_AggDescSizeChannelArchetype;
    Desc.Name = G_NameChannelArchetype;
    ICOManager* Dacom = DACOM_Acquire();
    if (Dacom->CreateInstance((AGGDESC*)(&Desc),
                              (void**)(&Channel)) == 0) // +0xC
        Channel->Load(FileSystem); // +0xC
    Fs->OpenNode(G_NameParentDot); // +0x60
}

FAnimNameEntry::~FAnimNameEntry()
{
    std::free(Name);
    std::vector<FAnimTrack>().swap(Tracks);
    if (LongestChannel != nullptr)
    {
        LongestChannel->Release();
        LongestChannel = nullptr;
    }
}

static unsigned long G_JointMapLength = 0;
static unsigned long G_ObjectMapLength = 0;
static unsigned long G_EventMapLength = 0;

// FUN_06611440. Retail returns 1, which no caller reads.
void FAnimNameEntry::LoadScriptMaps(const char* Name, void* FileSystem)
{
    this->Name = _strdup(Name);
    IFileSystem* Fs = AsFileSystem(FileSystem);
    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NameStar, // +0x48
                                      &FindData);
    if (FindHandle == -1)
        return;
    float BestDuration = -1.0f;
    do
    {
        if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            if (std::strcmp(FindData.cFileName, G_NameRootHeight) != 0)
                continue;
            FEngBaseFileSystemDesc Desc;
            FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
            Desc.FindHandle = (unsigned long)FindHandle;
            const int Handle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
            if (Handle == -1)
                continue;
            unsigned long BytesRead = 0;
            Fs->Read((unsigned long)Handle, &RootHeight, 4, &BytesRead, 0); // +0x14
            Fs->CloseFile((unsigned long)Handle); // +0x10
            bHasRootHeight = 1;
            continue;
        }
        if (FindData.cFileName[0] == '.')
            continue;
        FEngBaseFileSystemDesc Desc;
        FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
        Desc.FindHandle = (unsigned long)FindHandle;
        IFileSystem* MapFs = nullptr;
        Fs->CreateInstance((AGGDESC*)(&Desc), (void**)&MapFs); // +0xC
        if (std::strncmp(FindData.cFileName, G_NameEventMap, G_EventMapLength) != 0)
        {
            Tracks.resize(Tracks.size() + 1, FAnimTrack());
            FAnimTrack* Track = &Tracks.back();
            if (std::strncmp(FindData.cFileName, G_NameJointMap, G_JointMapLength) == 0)
            {
                Track->ParentCrc = ReadUtfNameCrc(MapFs, G_NameParentName, &Track->ParentName);
                Track->ChildCrc = ReadUtfNameCrc(MapFs, G_NameChildName, &Track->ChildName);
                Track->LoadChannelNode(MapFs);
            }
            else if (std::strncmp(FindData.cFileName, G_NameObjectMap, G_ObjectMapLength) == 0)
            {
                Track->ParentCrc = ReadUtfNameCrc(MapFs, G_NameParentName, &Track->ParentName);
                Track->LoadChannelNode(MapFs);
            }
            // Retail: unreachable under the outer "Event map" test, so event maps are never loaded.
            else if (std::strncmp(FindData.cFileName, G_NameEventMap, G_EventMapLength) == 0)
            {
                Track->LoadChannelNode(MapFs);
            }
            if (Track->Channel != nullptr)
            {
                float Duration;
                Track->Channel->GetDuration(&Duration); // +0x10
                if (BestDuration < Duration)
                {
                    if (LongestChannel != nullptr)
                    {
                        LongestChannel->Release();
                        LongestChannel = nullptr;
                    }
                    LongestChannel = Track->Channel;
                    LongestChannel->AddRef();
                    BestDuration = Duration;
                }
            }
        }
        if (MapFs != nullptr)
            ((IDAComponent*)MapFs)->Release();
    } while (Fs->FindNext((unsigned long)FindHandle, // +0x4C
                        &FindData) != 0);
    Fs->GetFileSystemError(); // +0x78
    Fs->FindClose((unsigned long)FindHandle); // +0x50
}

static FAnimNameEntry* InsertAnimName(FAnimScript* Script, unsigned long Crc)
{
    return &Script->Names[Crc];
}

static void StoreAnimMapNameLengths()
{
    G_JointMapLength = (unsigned long)(std::strlen(G_NameJointMap));
    G_ObjectMapLength = (unsigned long)(std::strlen(G_NameObjectMap));
    G_EventMapLength = (unsigned long)(std::strlen(G_NameEventMap));
}

unsigned long LoadAnimScriptNames(FAnimScript* Script, void* FileSystem)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameScript) == 0)
        return 0;
    StoreAnimMapNameLengths();
    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NameStar,
                                      &FindData);
    if (FindHandle != -1)
    {
        do
        {
            if (FindData.cFileName[0] == '.')
                continue;
            FEngBaseFileSystemDesc Desc;
            FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
            Desc.FindHandle = (unsigned long)FindHandle;
            void* NestedFs = nullptr;
            Fs->CreateInstance((AGGDESC*)(&Desc), &NestedFs);
            if (((FindData.dwFileAttributes >> 4) & 1) != 0)
            {
                FAnimNameEntry* Entry =
                    InsertAnimName(Script, DACOM_CRC::GetCRC32(FindData.cFileName));
                Entry->LoadScriptMaps(FindData.cFileName, NestedFs);
            }
            if (NestedFs != nullptr)
                ((IDAComponent*)NestedFs)->Release();
        } while (Fs->FindNext((unsigned long)FindHandle,
                            &FindData) != 0);
        Fs->GetFileSystemError();
        Fs->FindClose((unsigned long)FindHandle);
    }
    Fs->OpenNode(G_NameParentDot);
    return 0;
}

// .data:0662a0f0 (from the PE bytes): the __FILE__ the FDUMPs of FUN_06611b00 (0x176 / 0x189 /
// 0x193), FUN_06611e50 (0x1D8) and FUN_066124e0 (0x2F5 / 0x2FA) push.
const char* const G_FdumpFileAnim =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Anim\\Anim.cpp";

FAnimNameEntry* FindAnimName(FAnimScript* Script, unsigned long Crc)
{
    FAnimNameMap::iterator It = Script->Names.find(Crc);
    if (It == Script->Names.end())
        return nullptr;
    return &It->second;
}

unsigned long LoadAnimScriptsByNameList(FAnimScript* Script, void* FileSystem,
                                       const char** Names, int Count)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameScript) == 0)
        return 0;
    StoreAnimMapNameLengths();
    for (int Index = 0; Index < Count; ++Index)
    {
        const char* Name = Names[Index];
        if (Name == nullptr)
        {
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x176, "WARNING",
                         "Null name in animation sub set. Aborting subset load.\n");
            break;
        }
        char Message[0x2000];
        if ((Fs->GetPathAttributes(Name) & 0x10) == 0)
        {
            FormatFdumpMessage(Message,"Unknown or old style animation \"%s\". Skipping.\n", Name);
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x193, "WARNING", Message);
            continue;
        }
        if (FindAnimName(Script, DACOM_CRC::GetCRC32(Name)) != nullptr)
            continue;
        FEngBaseFileSystemDesc Desc;
        FillFileSystemDesc(&Desc, Name, nullptr);
        void* NestedFs = nullptr;
        if (Fs->CreateInstance((AGGDESC*)(&Desc), &NestedFs) == 0)
        {
            FAnimNameEntry* Entry = InsertAnimName(Script, DACOM_CRC::GetCRC32(Name));
            Entry->LoadScriptMaps(Name, NestedFs);
        }
        else
        {
            FormatFdumpMessage(Message,"Failed to open animation \"%s\". Skipping.\n", Name);
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x189, "WARNING", Message);
        }
        if (NestedFs != nullptr)
            ((IDAComponent*)NestedFs)->Release();
    }
    Fs->OpenNode(G_NameParentDot);
    return 0;
}

// FUN_06611f90. Record +0 is the name entry.
void FAnimPlayback::CloneTypedChannels(FAnimNameEntry* Entry)
{
    Name = Entry;
    this->Typed.resize(Entry->Tracks.size(), nullptr);
    std::vector<FTypedChannel*>::iterator Slot = this->Typed.begin();
    std::vector<FAnimTrack>::iterator Cursor = Entry->Tracks.begin();
    while (Cursor != Entry->Tracks.end())
    {
        unsigned long Type = 0;
        Cursor->Channel->GetFlags(&Type);
        FTypedChannel* Typed = NewTypedChannelWithArchetype(Type, Cursor->Channel);
        Typed->PlayState &= ~G_PlayStatePlaying;
        *Slot = Typed;
        ++Slot;
        ++Cursor;
    }
}

// FUN_06612250.
unsigned long FAnimPlayback::FilterTyped(IChannel* Filter)
{
    std::vector<FTypedChannel*>::iterator Cursor = this->Typed.begin();
    while (Cursor != this->Typed.end())
    {
        FTypedChannel* Typed = *Cursor;
        const unsigned long Type = Typed->GetType();
        if (Type != G_TypedTypeBase)
        {
            if (Filter->InMixer((unsigned long)((size_t)Typed)) != 0)
                return 0;
        }
        ++Cursor;
    }
    return 1;
}

// FUN_066122a0. Sole XREF LAB_066150a0.
unsigned long FAnimPlayback::HasTyped(void* Object)
{
    std::vector<FTypedChannel*>::iterator Cursor = Typed.begin();
    while (Cursor != Typed.end())
    {
        if (*Cursor == Object)
            return 1;
        ++Cursor;
    }
    return 0;
}

// FUN_066122d0.
unsigned long FAnimPlayback::TouchMappedTyped(Vector* OutPos, Vector* OutSampledPos)
{
    std::vector<FTypedChannel*>::iterator Cursor = this->Typed.begin();
    while (Cursor != this->Typed.end())
    {
        FTypedChannel* Typed = *Cursor;
        const unsigned long Type = Typed->GetType();
        if ((Type & 2) != 0)
        {
            Typed->TouchMapped(OutPos, OutSampledPos);
            return 1;
        }
        ++Cursor;
    }
    return 0;
}

FAnimHandleNode* G_AnimHandleHead = nullptr;

bool FindAnimHandle(unsigned long Key)
{
    FAnimHandleNode* Node = G_AnimHandleHead;
    while (Node != nullptr)
    {
        if (Node->Key == Key)
            return true;
        Node = Node->Next;
    }
    return false;
}

// FUN_06612320.
static unsigned char BindTrackTarget(FAnimBindOut* Out, FEngineInstance* Root, FAnimTrack* Track,
                                    void* Sink, void* SinkContext, unsigned char* Already)
{
    FEngineInstance* const Invalid =
        (FEngineInstance*)((ptrdiff_t)(-1));
    *Already = 0;
    if (Root == Invalid && Sink == nullptr)
    {
        Out->Kind = 0;
        Out->Sink = nullptr;
        Out->SinkContext = nullptr;
        Out->Instance = nullptr;
        return 0;
    }
    Out->SinkContext = SinkContext;
    if (Track->ChildCrc != 0)
    {
        const char* ChildName = Track->ChildName != nullptr ? Track->ChildName : "";
        FEngineInstance* Inst = AnimEngineFindInstanceByName(Root, ChildName);
        if (Inst == Invalid)
            return 0;
        const bool bMapped = FindAnimHandle((unsigned long)((size_t)Inst));
        *Already = bMapped ? 1 : 0;
        if (bMapped)
            return 0;
        FEngineInstance* Parent = (FEngineInstance*)(G_AnimEngine->GetInstanceParent((InstanceIndex)Inst));
        if (Parent == Invalid)
            return 0;
        FArchetypeName NameOut;
        if (Track->ParentCrc != G_AnimEngine->GetInstanceName(&NameOut, (InstanceIndex)Parent)->Crc)
            return 0;
        Out->Kind = G_BindKindJoint;
        Out->Instance = Inst;
        Out->Sink = Sink;
        return 1;
    }
    if (Track->ParentCrc != 0)
    {
        const char* ParentName = Track->ParentName != nullptr ? Track->ParentName : "";
        FEngineInstance* Inst = AnimEngineFindInstanceByName(Root, ParentName);
        if (Inst == Invalid)
            Inst = Root;
        const bool bMapped = FindAnimHandle((unsigned long)((size_t)Inst));
        *Already = bMapped ? 1 : 0;
        if (bMapped)
            return 0;
        Out->Instance = Inst;
        Out->Kind = G_BindKindObject;
        Out->Sink = Sink;
        return 1;
    }
    Out->Instance = Root;
    Out->Kind = G_BindKindRoot;
    Out->Sink = Sink;
    return Sink != nullptr ? 1 : 0;
}

// FUN_066124e0. Record +4 is Root.
void BindPlaybackTracks(FAnimPlayback* Playback, FEngineInstance* Root, void* Sink,
                        void* SinkContext)
{
    Playback->Root = Root;
    Playback->LongestTyped = nullptr;
    float BestDuration = -1.0f;
    if (Playback->Typed.empty())
        return;
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    // The track cursor walks the name value's vector (+0x14 first) alongside, advancing once
    // per iteration whether or not the channel was kept.
    FAnimTrack* Track = Playback->Name->Tracks.data();
    while (Cursor != Playback->Typed.end())
    {
        FAnimBindOut Bind;
        Bind.Kind = G_BindKindRoot;
        Bind.Sink = nullptr;
        Bind.SinkContext = nullptr;
        Bind.Instance = (FEngineInstance*)((ptrdiff_t)-1);
        unsigned char bAlready = 0;
        unsigned char bBound = BindTrackTarget(&Bind, Root, Track, Sink, SinkContext, &bAlready);
        if (bBound != 0)
        {
            FTypedChannel* Typed = *Cursor;
            bBound = (unsigned char)(
                Typed->BindMap(
                    (unsigned long*)(&Bind), Root));
        }
        if (bBound != 0)
        {
            float Duration = 0.0f;
            Track->Channel->GetDuration(&Duration);
            if (BestDuration < Duration)
            {
                Playback->LongestTyped = *Cursor;
                BestDuration = Duration;
            }
            ++Cursor;
        }
        else
        {
            if (bAlready == 0)
            {
                if (Track->ChildCrc != 0)
                {
                    const char* Parent = Track->ParentName != nullptr ? Track->ParentName : "";
                    const char* Child = Track->ChildName != nullptr ? Track->ChildName : "";
                    char Message[0x2000];
                    _snprintf(Message, sizeof(Message),
                              "%s failed to map JOINT (parent: %s, child: %s) channel",
                              Playback->Name->Name, Parent, Child);
                    Message[sizeof(Message) - 1] = '\0';
                    EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x2F5, "WARNING", Message);
                }
                else if (Track->ParentCrc != 0)
                {
                    const char* Parent = Track->ParentName != nullptr ? Track->ParentName : "";
                    char Message[0x2000];
                    _snprintf(Message, sizeof(Message), "%s failed to map OBJECT (%s) channel",
                              Playback->Name->Name, Parent);
                    Message[sizeof(Message) - 1] = '\0';
                    EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x2FA, "WARNING", Message);
                }
            }
            FTypedChannel* Typed = *Cursor;
            if (Typed != nullptr)
            {
                if (Typed->Archetype != nullptr)
                {
                    Typed->Archetype->Release();
                    Typed->Archetype = nullptr;
                }
                ::operator delete(Typed); // Nonvirtual base: archetype released above, trivial tails.
            }
            Cursor = Playback->Typed.erase(Cursor);
        }
        ++Track;
    }
}

// Rebind every channel with one play header (inlined in FUN_066126e0 and FUN_066127e0).
static unsigned long PlaybackBindAll(FAnimPlayback* Playback, const FPlayHeader* Header,
                                     unsigned long Result)
{
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        if ((Playback->Typed[Index]->TryBindIfTimeValid(Header) & 0xFF) == 0)
            Result = 0xFFFFFFFF;                                 // no break
        Index += 1;
    }
    return Result;
}

// Reverse playback requires compatible track durations.
static unsigned long PlaybackDurationsDiffer(FAnimPlayback* Playback)
{
    unsigned long Result = 0;
    float First = -1.0f;
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        FTypedChannel* Typed = Playback->Typed[Index];
        if (First < 0.0f)
            Typed->Archetype->GetDuration(&First);
        else
        {
            float Duration = 0.0f;
            Typed->Archetype->GetDuration(&Duration);
            if (!(First == Duration))
                Result = 1;
        }
        Index += 1;
    }
    return Result;
}

// FUN_066126e0: build the play header and bind every channel with it. The three float fields
// after Rate arrive as raw dwords and are stored bit for bit.
unsigned long FAnimPlayback::EvalTyped(unsigned long Flags, float Start, float Speed,
                                       unsigned long BlendDurationBits, unsigned long ScaleBits,
                                       unsigned long TranslationScaleBits, const ::Transform* Transform)
{
    // Flags bit 0 means reverse.
    const long Sign = (Flags & G_PlayFlagReverse) == 0 ? 1 : -1;
    const float Rate = (float)(std::fabs((double)Speed) *
                                          (double)Sign);
    FPlayHeader Header;
    std::memcpy(&Header.StartTime, &Start, sizeof(float));
    Header.LoopFlags = Flags & 0xFE;
    std::memcpy(&Header.Rate, &Rate, sizeof(float));
    std::memcpy(&Header.BlendDuration, &BlendDurationBits, sizeof(float));
    std::memcpy(&Header.Scale, &ScaleBits, sizeof(float));
    std::memcpy(&Header.TranslationScale, &TranslationScaleBits, sizeof(float));
    Header.Transform = Transform;
    unsigned long Result = 0;
    if ((Flags & 1) != 0 && Typed.size() != 0)
        Result = PlaybackDurationsDiffer(this);
    return PlaybackBindAll(this, &Header, Result);
}

// FUN_066127e0: rebind every channel from the longest one's own state.
unsigned long FAnimPlayback::EvalStoredTyped()
{
    FTypedChannel* Longest = LongestTyped;
    const float Start = -2.0f;
    FPlayHeader Header;
    std::memcpy(&Header.StartTime, &Start, sizeof(float));
    Header.LoopFlags = Longest->LoopFlags & 0xFE;
    std::memcpy(&Header.Rate, &Longest->Rate, sizeof(float));
    std::memset(&Header.BlendDuration, 0, sizeof(float));
    std::memcpy(&Header.Scale, &Longest->Scale, sizeof(float));
    std::memcpy(&Header.TranslationScale, &Longest->TranslationScale, sizeof(float));
    Header.Transform = nullptr;
    return PlaybackBindAll(this, &Header, 0);
}

// FUN_066128f0: clear the play bit first, then pose (unlike ChannelStopGraph).
void FAnimPlayback::StopTyped()
{
    unsigned long Index = 0;
    while (Index < this->Typed.size())
    {
        FTypedChannel* Typed = this->Typed[Index];
        Typed->PlayState &= ~G_PlayStatePlaying;
        Typed->ApplyFromArchetype();
        Index += 1;
    }
}

// FUN_06612920.
void FAnimPlayback::ApplyTyped()
{
    unsigned long Index = 0;
    while (Index < Typed.size())
    {
        Typed[Index]->ApplyFromArchetype();
        Index += 1;
    }
}

// FUN_06612940: match on the bind descriptor (typed +8). Kinds 0/1 compare the instance (+0xC),
// kind 2 the sink (+4); other kinds never match.
FTypedChannel* FAnimPlayback::FindTypedByHeader(const FAnimBindOut* Header)
{
    unsigned long Index = 0;
    while (Index < this->Typed.size())
    {
        FTypedChannel* Typed = this->Typed[Index];
        const FAnimBindOut* Desc = TypedBindDesc(Typed);
        if (Desc->Kind == Header->Kind)
        {
            if (Desc->Kind == G_BindKindJoint || Desc->Kind == G_BindKindObject)
            {
                if (Desc->Instance == Header->Instance)
                    return Typed;
            }
            else if (Desc->Kind == G_BindKindRoot)
            {
                if (Desc->Sink == Header->Sink)
                    return Typed;
            }
        }
        Index += 1;
    }
    return (FTypedChannel*)((ptrdiff_t)(-1));
}

// FUN_066129c0.
void FAnimPlayback::SetTypedTime(float Time)
{
    unsigned long Index = 0;
    while (Index < this->Typed.size())
    {
        FTypedChannel* Typed = this->Typed[Index];
        Typed->SetTime(Time);
        Index += 1;
    }
}

// FUN_066129f0: the longest channel's current time (+0x1C), despite the caller's name.
float FAnimPlayback::StoredDuration()
{
    return LongestTyped->Time;
}

unsigned long RegisterAnimHandle(unsigned long Key)
{
    if (Key == 0xFFFFFFFF)
        return 0xFFFFFFFF;
    if (FindAnimHandle(Key))
        return 0;
    FAnimHandleNode* Node = (FAnimHandleNode*)(std::calloc(1, sizeof(FAnimHandleNode)));
    if (Node == nullptr)
        return 0xFFFFFFFF;
    Node->Key = Key;
    Node->Next = G_AnimHandleHead;
    G_AnimHandleHead = Node;
    return 0;
}

void UnregisterAnimHandle(unsigned long Key)
{
    FAnimHandleNode* Prev = nullptr;
    FAnimHandleNode* Node = G_AnimHandleHead;
    while (Node != nullptr)
    {
        if (Node->Key == Key)
        {
            if (Prev != nullptr)
                Prev->Next = Node->Next;
            else
                G_AnimHandleHead = Node->Next;
            std::free(Node);
            return;
        }
        Prev = Node;
        Node = Node->Next;
    }
}

// FUN_06612a00.
void FAnimPlayback::FreeTyped()
{
    std::vector<FTypedChannel*>::iterator Cursor = this->Typed.begin();
    while (Cursor != this->Typed.end())
    {
        FTypedChannel* Typed = *Cursor;
        if (Typed != nullptr)
        {
            if (Typed->Archetype != nullptr)
            {
                Typed->Archetype->Release();
                Typed->Archetype = nullptr;
            }
            ::operator delete(Typed); // Nonvirtual base: archetype released above, trivial tails.
        }
        ++Cursor;
    }
    this->Typed.clear();
    LongestTyped = nullptr;
}

unsigned long DACOM_API FAnimScript::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FAnimScript::Release()
{
    RefCount -= 1;
    const unsigned long Left = RefCount;
    if (Left != 0)
        return Left;
    delete this;
    return 0;
}

unsigned long DACOM_API FAnimScript::RejectOperation(void* ArgA, void* ArgB)
{
    (void)ArgA;
    (void)ArgB;
    return 0xFFFFFFFF;
}

FAnimTrack::~FAnimTrack()
{
    if (ParentName != nullptr)
        std::free(ParentName);
    if (ChildName != nullptr)
        std::free(ChildName);
    if (Channel != nullptr)
    {
        Channel->Release();
        Channel = nullptr;
    }
}

FAnimTrack::FAnimTrack(const FAnimTrack& Other)
    : Channel(Other.Channel)
    , ParentCrc(Other.ParentCrc)
    , ChildCrc(Other.ChildCrc)
    , ParentName(nullptr)
    , ChildName(nullptr)
{
    if (Channel != nullptr)
        Channel->AddRef();
    if (Other.ParentName != nullptr)
        ParentName = _strdup(Other.ParentName);
    if (Other.ChildName != nullptr)
        ChildName = _strdup(Other.ChildName);
}
