// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/space_solar_world.h"

#include "Interface/Pages/character_select_equip.h"
#include "Common/client_cliobj.h" // Archetype::LoadMountedEquipmentCompanions / HookUpInstanceTreeMaterials
#include "Space/client_gameobj.h"
#include "Space/client_objdb.h"

#include "common_archetype.h"
#include "common_cobject.h"
#include "DALib.h"
#include "engine/engbase.h"
#include "render/rendcomp.h"

#include <cstdarg>
#include <cstdint>

#include <cstdio>
#include <cstring>
#include <new>
#include <windows.h>

// The anim-name list CSolar::ReadObj fills ("anim" keys) is CSolar::CreateParms::ContinualAnims,
// a std::list member: Parms' destructor frees it, as retail's inlined ~CreateParms does on both
// exits.
bool SpawnWorldSolarFromObjectSection(INI_Reader& Reader)
{
    // FUN_00548a20. Vibe::Verify in ReadObj is seeded by
    // SolarReps::LoadObjectVibesFromSystemIni during Universe::LoadSystemFile.

    // The reader's position at the section start, restored before the client half re-reads
    // the section.
    INI_Reader::State SectionStart;
    Reader.get_state(SectionStart);

    // Built in retail's order (FUN_0052e0a0 on the CSimple head, FUN_004cdc10 on the costume).
    // Value-initialisation zeroes what retail leaves unwritten (+0x74..+0x90); ReadObj fills
    // the rest.
    CSolar::CreateParms Parms{};
    InitCreateParmsScratchRetail(&Parms);
    Parms.DockingBaseId = 0;
    Parms.VibeId = 0xffffffffu;
    Parms.SpaceCostume.PartIds[0] = 0;
    Parms.SpaceCostume.PartIds[1] = 0;
    Parms.SpaceCostume.PartIds[2] = 0;
    Parms.SpaceCostume.PartIds[3] = 0;
    Parms.SpaceCostume.AccessoryCount = 0;
    Parms.VoiceId = 0;
    Parms.NicknameSrc.clear();
    Parms.VisitValue = 0;
    reinterpret_cast<ID_String&>(Parms.ParentNicknameId).clear();
    Parms.bDestructible = 0;
    Parms.bDynamic = 0;
    Parms.DestSystem = 0;
    reinterpret_cast<ID_String&>(Parms.DestGateId).clear();
    Parms.PrevRing = 0;
    Parms.NextRing = 0;
    Parms.TradelaneSpaceNameId = 0;
    Parms.AtmosphereRange = 0.0f;
    Parms.DockWithBaseId = 0;

    // ReadObj's bool is not looked at -- the archetype it returned is. Parms.ObjDb is set
    // before that test.
    Archetype::Solar* OutArch = nullptr;
    CSolar::ReadObj(Reader, OutArch, Parms);
    Parms.ObjDb = GetClientObjDb();
    if (OutArch == nullptr)
    {
        return false;
    }

    OutArch->load();

    // Retail has no failure path after the allocation.
    FClientGameobj* const Go = ClientGameobj_CreateSolar();

    // Slot 90 returns nothing; retail goes on without a check.
    Go->create(OutArch);

    // Narrowed to a CSolar, then CSolar::init with no null test.
    CObject* const Obj = Go->Object;
    CSolar* const Solar =
        (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            ? static_cast<CSolar*>(Obj)
            : nullptr;
    Solar->init(Parms);

    // No null test on the companion.
    OutArch->Companion->Load(1);

    Archetype::LoadMountedEquipmentCompanions(&Parms.Equipment, 1);
    Archetype::HookUpInstanceTreeMaterials(Go->get_index());

    // The client half FUN_005485c0 (ring / burn_color / dock_script / voice / jump_effect /
    // star, then the nebula).
    Reader.set_state(SectionStart);
    static_cast<FClientSolarGameobj*>(Go)->ReadIni(Reader);

    ClientObjDb_RegisterGameobj(Go); // FUN_00540c30(Go, 0)

    OutArch->Release();
    return true;
}

void SolarSpheresApplyDetail(float Detail)
{
    // FUN_005488a0. The whole computation stays on the x87 stack (extended precision), so it
    // is done in double here. An unordered T passes both clamps.
    double T = static_cast<double>(Detail) + static_cast<double>(Detail) - 1.0;
    if (T > 1.0)
        T = 1.0;
    else if (T < 0.0)
        T = 0.0;

    int const Tessellation = static_cast<int>(3.0 * T + 7.0);
    int const Level = static_cast<int>(3.5 - (T + T));

    // FUN_00540fc0 on bucket 2 (solars).
    int const SolarKind = 2;
    for (FClientGameobj* Go = ClientObjDb_GetKindHead(&SolarKind); Go != nullptr;
         Go = Go->ListNext)
    {
        // Solar buckets only hold solars, so the null is never read.
        CObject* const Obj = Go->Object;
        CSolar* Solar = nullptr;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            Solar = static_cast<CSolar*>(Obj);
        if ((static_cast<unsigned char>(Solar->TypeBits) & 7u) == 0u)
            continue;

        // The engine instance queried for "1.11_ISphereMesh" through IEngine +0x78.
        CObject* Simple = nullptr;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
            Simple = Obj;
        ISphereMesh* Sphere = nullptr;
        GENRESULT const Result = DALib::Engine->QueryInstance(
            Simple->EngineInstanceId,
            Iids::SphereMesh, reinterpret_cast<void**>(&Sphere));
        if (Result < 0)
        {
            // A failed query still releases whatever came back.
            if (Sphere != nullptr)
                Sphere->Release();
            continue;
        }

        Sphere->SetMaxTessellationLevel(Tessellation);
        Sphere->SetLodDistanceScale(static_cast<float>(Level));
        if (Sphere != nullptr)
            Sphere->Release();
    }
}
