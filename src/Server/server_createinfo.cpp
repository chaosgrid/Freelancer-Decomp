// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_createinfo.cpp -- the CREATE* record builders Observer::update sends, plus the
// StarSys.cpp math-engine helpers. The create-info constructors, destructor and clear()
// are inline in server_createinfo.h, which also documents the records.
// Ghidra loses the register-held quaternion dwords in FUN_06d07690 (`unaff_EBX` /
// `unaff_ESI` are the math engine's out-parameter); trust the disassembly.

#include "../include/server_createinfo.h"

#include "../include/common_archetype.h"
#include "../include/common_cequip.h"
#include "../include/common_csys.h"       // Matrix
#include "../include/dacom.h"             // DACOM_Acquire, ICOManager, AGGDESC (FUN_06d5ff70)
#include "../include/common_iface.h"      // IObjRW
#include "../include/common_misc.h"       // DamageList, SUBOBJ_ID_NONE
#include "../include/common_reputation.h" // Reputation::Vibe::GetRank
#include "../include/x86math.h"           // I3DMathEngine::MatrixToQuaternion

// FUN_06d5ff70. See server_createinfo.h. Neither the manager nor the result is tested.
// Retail never initialises the out slot; the port seeds it with null rather than stack
// residue (every caller dereferences the result either way).
I3DMathEngine* Server_Acquire3DMathEngine()
{
    struct FAGGDESC12
    {
        unsigned long Size;       // +0x00
        const char* Name;         // +0x04
        void* OuterUnknown;         // +0x08
    };
    FAGGDESC12 Desc;
    Desc.Size = sizeof(FAGGDESC12);
    Desc.Name = "3DMathEngine";
    Desc.OuterUnknown = nullptr;

    void* Engine = nullptr;
    DACOM_Acquire()->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Engine);
    return static_cast<I3DMathEngine*>(Engine);
}

// Deviation from retail (docs/re/deviations.md): retail keeps one lazily filled engine pointer
// per source file -- twelve in Server.dll, each filled by its own FUN_06d5ff70 call the first
// time that file needs it -- and every user inlines the fetch. The port keeps one.
I3DMathEngine* Server_MathEngine()
{
    static I3DMathEngine* Engine = nullptr;
    if (Engine == nullptr)
        Engine = Server_Acquire3DMathEngine();
    return Engine;
}

// The inlined lazy fetch plus MatrixToQuaternion (I3DMathEngine +0x50). No null test on
// the engine.
void Server_MatrixToQuat(Quaternion& Out, Matrix const& In)
{
    Server_MathEngine()->MatrixToQuaternion(Out, In);
}

// QuaternionToMatrix (+0x54) through the same engine, as inlined in create_solar /
// create_ship. No null test on the engine.
void Server_QuatToMatrix(Matrix& Out, Quaternion const& In)
{
    Server_MathEngine()->QuaternionToMatrix(Out, In);
}

// CEqObjCreateInfo / CShipCreateInfo / CSolarCreateInfo's ctor, dtor and clear() are INLINE
// in server_createinfo.h: retail compiled them from a shared header into both Server.dll
// and rpclocal.dll.

// FUN_06d07690 -- CShipCreateInfo fill (cdecl).
void __cdecl CShipCreateInfo_Fill(CShipCreateInfo* Info, CShip* Ship)
{
    // No null test on either argument or on the archetype.
    Info->bUsingTradelane =
        static_cast<unsigned char>(Ship->is_using_tradelane() ? 1 : 0);

    Archetype::Root const* const Arch = Ship->get_archetype();
    Info->ArchetypeNickname = Arch->get_id();

    // Position and orientation read as FIELDS, not through get_position().
    Info->Position = Ship->WorldPosition;

    Server_MatrixToQuat(Info->Orientation, Ship->Orientation);

    Info->Velocity = Ship->get_velocity();

    Info->AngularVelocity = Ship->get_angular_velocity();

    Info->ObjId = Ship->Id;
    Info->LoadoutId = 0u;

    Ship->get_equip_desc_list(Info->Equipment);

    Ship->get_collision_group_description(Info->CollisionGroups);

    // Moved as a dword in retail; the field is a float (rpclocal's encoder quantizes it).
    Info->Throttle = Ship->Throttle;

    // The costume: four part ids, the count, then exactly `count` accessories.
    Info->VibeGroupId = static_cast<int>(Ship->VibeId);
    Info->CostumeHead = Ship->SpaceCostume.PartIds[0];
    Info->CostumeBody = Ship->SpaceCostume.PartIds[1];
    Info->CostumeLeftHand = Ship->SpaceCostume.PartIds[2];
    Info->CostumeRightHand = Ship->SpaceCostume.PartIds[3];
    Info->CostumeAccessoryCount = Ship->SpaceCostume.AccessoryCount;
    // Unbounded, as retail: a count <= 0 copies nothing, and the bound is re-read each pass.
    for (int Index = 0; Index < Info->CostumeAccessoryCount; ++Index)
        Info->CostumeAccessory[Index] = Ship->SpaceCostume.AccessoryIds[Index];

    Info->VoiceId = Ship->VoiceId;
    Info->GroupNameId = Ship->GroupNameId;
    Info->OwnerPlayer = Ship->OwnerPlayer;
    Info->DockingBaseId = Ship->DockingBaseId;
    // The multiplayer group id.
    Info->GroupId = Ship->GroupId;

    // ftol: truncated toward zero.
    Info->HitPts = static_cast<int>(Ship->get_hit_pts());

    // GetRank takes the vibe id BY REFERENCE, not a Vibe object.
    {
        unsigned int Rank = 0u;
        int const GroupId = static_cast<int>(Ship->VibeId);
        Reputation::Vibe::GetRank(GroupId, Rank);
        Info->ReputationRank = Rank;
    }

    // Not a misread: retail builds a COMPLETE DamageList on the STACK, one entry per
    // equipment item (CEquipTraverser(0x100), a different overload and mask from the
    // damage-list slot's), and destroys it unread. The record's own DamageList (+0x8c)
    // stays empty. Ghidra agrees (`aDStack_40` is a local). Reproduced because
    // CEquip::GetHitPoints is virtual and an override could have a side effect.
    {
        DamageList Discarded;
        CEquipTraverser Walk(EquipTypeShield);
        for (;;) {
            CEquip* const Equip = Ship->EquipManager.Traverse(Walk);
            if (Equip == nullptr)
                break;
            // Retail passes the id as a word with stale high bits; the callee reads only
            // the low word.
            Discarded.add_damage_entry(Equip->Id, Equip->GetHitPoints(),
                                       DamageEntry::SubObjFate::None);
        }
    }
}

// FUN_06d07950 -- CSolarCreateInfo fill (cdecl); only caller StarSystem::add_player's
// multiplayer solar walk. The ship fill minus everything ship-specific, plus two solar
// bytes. Nothing is null-tested; the caller passes only solars.
void __cdecl CSolarCreateInfo_Fill(CSolarCreateInfo* Info, CSolar* Solar)
{
    Info->ArchetypeNickname = Solar->get_archetype()->get_id();

    Info->Position = Solar->WorldPosition;

    Server_MatrixToQuat(Info->Orientation, Solar->Orientation);

    Info->ObjId = Solar->Id;
    Info->LoadoutId = 0u;

    Solar->get_equip_desc_list(Info->Equipment);

    Solar->get_collision_group_description(Info->CollisionGroups);

    // As in the ship fill; the accessory loop is not clamped.
    Info->VibeGroupId = static_cast<int>(Solar->VibeId);
    Info->CostumeHead = Solar->SpaceCostume.PartIds[0];
    Info->CostumeBody = Solar->SpaceCostume.PartIds[1];
    Info->CostumeLeftHand = Solar->SpaceCostume.PartIds[2];
    Info->CostumeRightHand = Solar->SpaceCostume.PartIds[3];
    Info->CostumeAccessoryCount = Solar->SpaceCostume.AccessoryCount;
    for (int Index = 0; Index < Info->CostumeAccessoryCount; ++Index)
        Info->CostumeAccessory[Index] = Solar->SpaceCostume.AccessoryIds[Index];

    Info->VoiceId = Solar->VoiceId;
    Info->DockingBaseId = Solar->DockingBaseId;

    Info->bDestructible = static_cast<unsigned char>(Solar->bDestructible ? 1 : 0);
    Info->bDynamic = static_cast<unsigned char>(Solar->bDynamic ? 1 : 0);

    // ftol.
    Info->HitPts = static_cast<int>(Solar->get_hit_pts());

    // The same dead stack DamageList the ship fill builds; nothing reads it.
    {
        DamageList Discarded;
        CEquipTraverser Walk(EquipTypeShield);
        for (;;) {
            CEquip* const Equip = Solar->EquipManager.Traverse(Walk);
            if (Equip == nullptr)
                break;
            Discarded.add_damage_entry(Equip->Id, Equip->GetHitPoints(),
                                       DamageEntry::SubObjFate::None);
        }
    }
}

// FUN_06d07b90 -- the 0x48-byte LootDesc (cdecl). No null test on Desc, Loot, ContentsArch
// or the archetype.
void __cdecl ServerLootDesc_Fill(FServerLootDesc* Desc, CLoot* Loot)
{
    Desc->LootId = Loot->Id;

    Archetype::Equipment const* const Contents = Loot->ContentsArch;
    Desc->ContentsNickname = Contents->get_id();

    Desc->ContentsHitPts = Loot->ContentsHitPts;
    Desc->Units = Loot->Units;

    Archetype::Root const* const Container = Loot->get_archetype();
    Desc->ContainerNickname = Container->get_id();

    // Kept as a FLOAT here, unlike CShipCreateInfo::HitPts, which ftol's it.
    Desc->HitPts = Loot->get_hit_pts();

    // The name travels only as an OVERRIDE of the contents' own ids_name; otherwise the
    // field keeps what the caller left (zeroed).
    unsigned int const Name = Loot->get_name();
    if (Name != static_cast<unsigned int>(Contents->IdsName))
        Desc->NameOverride = Name;

    Desc->Position = Loot->WorldPosition;
    Server_MatrixToQuat(Desc->Orientation, Loot->Orientation);
    Desc->Velocity = Loot->get_velocity();
    Desc->bTemporary = static_cast<unsigned char>(Loot->bTemporary ? 1 : 0);
}

// FUN_06d07c80 -- the shared CREATECOUNTER / CREATEMINE builder (cdecl). Its parameters sit
// in the opposite registers from the loot builder's. No null tests.
void __cdecl ServerCreateProjectileInfo_Fill(FServerCreateProjectileInfo* Info,
                                             CProjectile* Projectile)
{
    Info->ObjId = Projectile->Id;

    Archetype::Root const* const Arch = Projectile->get_archetype();
    Info->ArchetypeNickname = Arch->get_id();

    Info->SubObjId = SUBOBJ_ID_NONE;

    Server_MatrixToQuat(Info->Orientation, Projectile->Orientation);
    Info->OwnerId = Projectile->OwnerId;
    Info->Position = Projectile->WorldPosition;
    Info->OwnerPlayer = 0u;
    Info->Velocity = Projectile->get_velocity();
}

// FUN_06d07d40 -- CREATEGUIDED (cdecl). No null test on Info, Guided or the archetype.
void __cdecl ServerCreateGuidedInfo_Fill(FServerCreateGuidedInfo* Info, CGuided* Guided)
{
    Archetype::Root const* const Arch = Guided->get_archetype();
    Info->ArchetypeNickname = Arch->get_id();
    Info->OwnerPlayer = 0u;
    Info->ObjId = Guided->Id;
    Info->Position = Guided->WorldPosition;
    Server_MatrixToQuat(Info->Orientation, Guided->Orientation);
    Info->OwnerId = Guided->OwnerId;

    // get_target() inlined. Retail re-loads the watcher for the call and has an arm that
    // dereferences null, reachable only if the field changed between the loads, which it
    // cannot; the port evaluates get_target() once.
    IObjRW* const Target = static_cast<IObjRW*>(Guided->get_target());
    Info->TargetId = (Target != nullptr) ? Target->get_id() : 0u;

    Info->TargetSubObjId = Guided->SubTarget;
    Info->SubObjId = SUBOBJ_ID_NONE;
}
