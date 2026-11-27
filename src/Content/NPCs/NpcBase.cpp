// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU20 (06eb6c00-06eb7360): base class of the space objects Content tracks
// (see NpcBase.h).

#include "NPCs/NpcBase.h"
#include "Core/ContentPlayerState.h"
#include "Universe/SystemInfo.h"
#include "Core/ContentStaticIds.h"   // TU20's 16 static-init thunks 06eb6c00-06eb6de0
#include "common_server.h"
#include "server_cobject.h"     // pub::SpaceObj::Destroy, DestroyType

// FUN_06eb7260. Nickname chars past [0] are left as they are.
FContentSpaceObjParams::FContentSpaceObjParams()
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
}

// FUN_06f28e10 (ICF-shared releasing-handle dtor) on +0.
FContentSpaceObjParams::~FContentSpaceObjParams()
{
    if (ReputationHandle != nullptr)
    {
        ReputationHandle->Release();
        ReputationHandle = nullptr;
    }
}

// Inlined in FUN_06f73680.
FContentSpaceObjParams::FContentSpaceObjParams(FContentSpaceObjParams const& Other)
    : ReputationHandle(Other.ReputationHandle)
    , Base(Other.Base)
    , Nickname(Other.Nickname)
    , System(Other.System)
    , HitPoints(Other.HitPoints)
{
    if (ReputationHandle != nullptr)
        ReputationHandle->AddRef();
}

// FUN_06eb7190. The attacker loop leaves each slot's padding unwritten.
FContentSpaceObj::FContentSpaceObj(FContentSpaceObjParams const* Params)
    : SpaceObjId(0)
    , ReputationHandle(Params->ReputationHandle)
    , Base(Params->Base)
    , System(Params->System)
    , Nickname(Params->Nickname)
{
    for (int Index = 0; Index < 8; ++Index)
    {
        FSpaceObjAttacker& Slot = Attackers[Index];
        Slot.VictimId = 0;
        Slot.Hostility = 0.0f;
        Slot.FirstTime = 0.0f;
        Slot.LastTime = 0.0f;
        Slot.HostilityTime = 0.0f;
        Slot.bWarned = 0;
        Slot.bProvoked = 0;
    }
}

// FUN_06eb7230. ReputationHandle is not released here.
FContentSpaceObj::~FContentSpaceObj() = default;

// FUN_06eb6e00. Only the first match is sent and erased.
void FContentSpaceObj::SendPendingComm(unsigned int Unused1, unsigned int Key, unsigned int Unused3)
{
    (void)Unused1;
    (void)Unused3;
    for (std::vector<FSpaceObjPendingComm>::iterator It = PendingComms.begin();
         It != PendingComms.end(); ++It)
    {
        if (It->Key != Key)
            continue;

        // SendComm = FUN_06f72930.
        System->SendComm(SpaceObjId, It->TargetId, It->TextId, 0, 0, nullptr, 0.5f);
        PendingComms.erase(It);
        return;
    }
}

// FUN_06eb7070.
unsigned int FContentSpaceObj::GetAffiliation() const
{
    if (ReputationHandle != nullptr)
        return ReputationHandle->GetAffiliation();
    return 0xffffffffu;
}

// FUN_06eb7080.
int FContentSpaceObj::GetReputation() const
{
    if (ReputationHandle != nullptr)
        return ReputationHandle->GetReputation();
    return -1;
}

// FUN_06eb7090.
void FContentSpaceObj::SetSystem(FSystemInfo* InSystem)
{
    System = InSystem;
}

// FUN_06eb70a0. GetLocation's status is ignored here and in the two getters below.
void FContentSpaceObj::GetPlacement(FSpaceObjPlacement* Out) const
{
    Vector Position;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(SpaceObjId, Position, Orientation);
    Out->Orientation = Orientation;
    Out->Position = Position;
}

// FUN_06eb70f0.
void FContentSpaceObj::GetPosition(Vector* Out) const
{
    Vector Position;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(SpaceObjId, Position, Orientation);
    *Out = Position;
}

// FUN_06eb7130.
void FContentSpaceObj::GetOrientation(Matrix* Out) const
{
    Vector Position;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(SpaceObjId, Position, Orientation);
    *Out = Orientation;
}

// FUN_06eb7170 (status dropped).
void FContentSpaceObj::GetMotion(Vector& OutLinear, Vector& OutAngular) const
{
    pub::SpaceObj::GetMotion(SpaceObjId, OutLinear, OutAngular);
}

// FUN_06eb7280 (FLT_MAX = DAT_06fb4650). The last free slot wins over the oldest; x87:
// unordered times are skipped.
bool FContentSpaceObj::FindOrAllocAttacker(unsigned int VictimId, FSpaceObjAttacker** Out)
{
    float MinTime = 3.40282347e+38f;
    int OldestIndex = -1;
    int EmptyIndex = -1;
    for (int Index = 0; Index < 8; ++Index)
    {
        FSpaceObjAttacker& Slot = Attackers[Index];
        if (Slot.VictimId == VictimId)
        {
            *Out = &Slot;
            return true;
        }
        if (Slot.VictimId == 0)
            EmptyIndex = Index;
        if (MinTime > Slot.LastTime)
        {
            OldestIndex = Index;
            MinTime = Slot.LastTime;
        }
    }

    int Chosen = EmptyIndex;
    if (Chosen == -1)
        Chosen = OldestIndex;
    FSpaceObjAttacker* Slot = &Attackers[0] + Chosen;
    *Out = Slot;
    Slot->VictimId = 0;
    Slot->Hostility = 0.0f;
    Slot->FirstTime = 0.0f;
    Slot->LastTime = 0.0f;
    Slot->HostilityTime = 0.0f;
    Slot->bWarned = 0;
    Slot->bProvoked = 0;
    return true;
}

// FUN_06f34670 (result dropped).
void FContentSpaceObj::DestroySpaceObj()
{
    pub::SpaceObj::Destroy(SpaceObjId, DESTROY_VANISH);
}
