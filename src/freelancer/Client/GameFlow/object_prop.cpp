// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/object_prop.h"

#include "Common/client_cliobj.h"
#include "Common/engine_light.h"
#include "Core/game_options.h"
#include "Common/misc_util.h"

#include "DALib.h"
#include "common_archetype.h"
#include "common_fuses.h"
#include "common_goods.h"
#include "common_id.h"
#include "common_newchar.h"
#include "common_universe.h"
#include "dacom.h"
#include "engine/engbase.h"

#include <stdint.h>
#include <string.h>

namespace
{
    // .rdata 0x005cbc74 -- the source path ObjectProp.cpp hands FDUMP.
    char const kObjectPropCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Gf\\ObjectProp.cpp";

    // The HardpointSummary FUN_004467e0 allocates (operator new(0x20), then the Common
    // constructor, then vptr 0x005cbd68). That vtable's only slot is FUN_004468d0, the scalar
    // deleting destructor: ~HardpointSummary, then operator delete on flag bit 0.
    class FObjectPropHardpoints : public HardpointSummary
    {
    public:
        ~FObjectPropHardpoints() override {}
    };

    // FUN_005595a0 (cdecl) -- FUN_00406cd0(Handle, 0, 0) on the companion unless Handle is -1.
    void RecomputeCompanionLod(Archetype::ICliObj* Companion, long Handle)
    {
        if (Handle == -1)
            return;
        Archetype::CliObjRecomputeLodPickExplicit(Companion, Handle, nullptr, false);
    }

    // The 0x30-byte frame FUN_004464f0 builds for Engine +0x9c: the 9-dword orientation first,
    // then the position (+0x24..+0x2c).
    struct FInstanceMatrix
    {
        Matrix Orient;
        Vector Pos;
    };
    static_assert(sizeof(FInstanceMatrix) == 0x30, "engine instance matrix 0x30");

    // Inlined in FUN_00446ac0 / FUN_00446b40: which options show a prop of Type.
    bool IsPropTypeShown(int Type)
    {
        if (Type == PetalCategoryRoom)
            return GetGameOption(kGameOptBackgroundGroup) >= 1;
        if (Type == PetalCategorySpaceship)
            return GetGameOption(kGameOptObjectsGroup) != 0 && GetGameOption(kGameOptShips) != 0;
        return GetGameOption(kGameOptObjectsGroup) != 0;
    }
}

FPropData::FPropData()
    : Type(-1)
{
    // FUN_00445e20.
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    MatrixSetIdentity(Orient);
}

FObjectProp::FObjectProp(FRoomRuntime* InOwner, FPropData const& Data)
    : FRoomObject(InOwner)
    , Instance(kRoomObjectNoInstance)
    , Type(0)
    , bDetached(0)
    , bRoomMain(0)
    , Hardpoints(nullptr)
{
    // FUN_00445f40. Retail runs both FUN_00555a40 and FUN_00555a70 on +0x4c.
    Scripts.Init();
    Scripts.Reset();
    Load(Data);
}

FObjectProp::~FObjectProp()
{
    // FUN_00446020. A detached prop leaves its instances alone.
    if (bDetached == 0 && Instance != kRoomObjectNoInstance)
    {
        std::list<void*>::iterator It = Attached.begin();
        for (; It != Attached.end(); ++It)
            DALib::Engine->DestroyInstance(*It);
        DALib::Engine->DestroyInstance(Instance);
    }
    Scripts.Destroy(); // FUN_00555a90
    if (Hardpoints != nullptr)
    {
        delete Hardpoints;
        Hardpoints = nullptr;
    }
    // The member destructors follow (retail runs FUN_00555a90 a second time, on the already
    // empty table), then ~FRoomObject (FUN_005594b0).
}

void FObjectProp::Update(double DeltaSeconds)
{
    // FUN_00446a60. Flashlights.Update is FUN_00558470.
    float const Delta = static_cast<float>(DeltaSeconds);
    if (bRunningLights != 0)
        Flashlights.Update(Hardpoints, Delta);
    DALib::Anim->MixerWalk(reinterpret_cast<unsigned long>(Instance), Delta);
    DALib::Engine->UpdateInstance(Instance, 2, AsOpaqueDword(Delta));
}

void* FObjectProp::GetEngineInstance()
{
    // LAB_00443870.
    return Instance;
}

void FObjectProp::DrawOpaque(ICamera* /*Camera*/)
{
    // FUN_00446ac0.
    if (!IsPropTypeShown(Type))
        return;
    if (bDetached != 0)
        return;
    // A type-5 prop first casts its shadow (FUN_0044b370).
    if (Type == PetalCategoryEquipmentCart)
    {
        RoomControlDrawShadow(Owner, reinterpret_cast<unsigned long>(GetEngineInstance()), 0,
                              0);
    }
    RoomControlDrawObject(Owner, this); // FUN_0044af60
}

void FObjectProp::DrawTranslucent(ICamera* Camera)
{
    // FUN_00446b40. Flashlights.Draw is FUN_00558580.
    if (!IsPropTypeShown(Type))
        return;
    if (bDetached != 0)
        return;
    if (bRunningLights == 0)
        return;
    Flashlights.Draw(Camera);
}

void FObjectProp::Load(FPropData const& Data)
{
    // FUN_004464f0.
    Name = Data.Name;
    Type = Data.Type;
    char const* Template = Data.Template.c_str();

    Archetype::Root* Arch = nullptr;
    switch (Data.Type)
    {
    case PetalCategoryRoom:
    case PetalCategoryEquipmentCart:
    case PetalCategoryProp:
    {
        // The PetalDB record of that category.
        unsigned const Id = ResolveTemplate(Template);
        PetalInterfaceDatabase::petal_record const* Record =
            PetalDB_get()->find_record(Data.Type, Id);
        LoadPetal(Record, Template);
        break;
    }
    case PetalCategorySpaceship:
        Arch = Archetype::GetShip(ResolveTemplate(Template));
        break;
    case PetalCategorySolar:
        Arch = Archetype::GetSolar(ResolveTemplate(Template));
        break;
    case PetalCategoryAsteroid:
        Arch = Archetype::GetAsteroid(ResolveTemplate(Template));
        break;
    case PetalCategoryEquipment:
        Arch = Archetype::GetEquipment(ResolveTemplate(Template));
        break;
    default:
        break;
    }

    if (Data.Type >= PetalCategorySpaceship && Data.Type <= PetalCategoryEquipment)
    {
        if (Arch == nullptr)
        {
            // The equipment case builds the severity with FUN_00411c50(0x10000, 1), the others
            // pass 0x100001 directly -- the same value.
            unsigned Severity = 0x100001;
            if (Data.Type == PetalCategoryEquipment)
                PackNibbleByte(&Severity, 0x10000, 1);
            FDUMP(Severity, "%s(%d) : *** ERROR: No valid archetype for prop %s\n",
                  kObjectPropCpp, 0x69, Template);
        }
        else
        {
            // +0x13 = the companion's envmap material is not empty; FUN_0041c540, FUN_005595a0.
            Archetype::ICliObj* Companion = Arch->Companion;
            bEnvMap = Archetype::CliObjEnvmapMaterial(Companion)[0] != '\0' ? 1 : 0;
            long Handle = -1;
            Instance = MiscUtilLoadEngineInstance(Arch->DaArchetype.Value, DALib::Data, &Scripts,
                                                  nullptr, Archetype::CliObjMaterialPaths(Companion),
                                                  &MaterialEntries, &Handle);
            RecomputeCompanionLod(Companion, Handle);
        }
    }

    if (Data.Type == PetalCategorySpaceship && Data.Loadout.length() != 0 && RtcSlider::is_enabled("ship_loadouts"))
        LoadLoadout(Data.Loadout.c_str());

    FInstanceMatrix Xform;
    Xform.Orient = Data.Orient;
    Xform.Pos = Data.Pos;
    DALib::Engine->SetInstanceMatrix(Instance, &Xform);
    DALib::Engine->UpdateInstance(Instance, 0, nullptr);
}

void FObjectProp::LoadPetal(void const* RecordPtr, char const* Template)
{
    // FUN_00446160.
    PetalInterfaceDatabase::petal_record const* Record =
        static_cast<PetalInterfaceDatabase::petal_record const*>(RecordPtr);
    if (Record == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: No valid PetalDB archetype for prop %s\n",
              kObjectPropCpp, 0x86, Template);
        return;
    }

    // The record's extra material names as {name, -1} pairs (FUN_0040d830).
    Archetype::FCliVc6Vector<Archetype::FCliMaterialPathPair> Extras;
    Extras.Cookie = 0;
    Extras.Begin = nullptr;
    Extras.End = nullptr;
    Extras.Capacity = nullptr;
    std::vector<char const*>::const_iterator It = Record->Extras.begin();
    for (; It != Record->Extras.end(); ++It)
    {
        CacheString Path;
        Path.clear();
        Path.Value = *It;
        Archetype::PushMaterialPathPair(&Extras, Path.Value, 0xffffffffu);
    }

    char const* Mesh = Record->Mesh.c_str();
    Instance = MiscUtilLoadEngineInstance(Mesh, DALib::Data, &Scripts, nullptr, &Extras,
                                          &MaterialEntries, nullptr);
    delete[] Extras.Begin;
}

unsigned FObjectProp::ResolveTemplate(char const* Template)
{
    // FUN_00446280.
    if (Template[0] != '$')
        return CreateID(Template);

    // The current base's slot for the indirection. Retail reads each through IBase's vtable
    // (+0x18 .. +0x50, getters of +0x30 .. +0x68); the port's Universe::IBase publishes the same
    // fields directly.
    Universe::IBase* Base = Universe::get_base(GetRoomRuntimeBaseId(Owner));
    if (_stricmp(Template, "$ship_sml_01") == 0)
        return Base->ShipSml[0];
    if (_stricmp(Template, "$ship_sml_02") == 0)
        return Base->ShipSml[1];
    if (_stricmp(Template, "$ship_sml_03") == 0)
        return Base->ShipSml[2];
    if (_stricmp(Template, "$ship_mdm_01") == 0)
        return Base->ShipMdm[0];
    if (_stricmp(Template, "$ship_mdm_02") == 0)
        return Base->ShipMdm[1];
    if (_stricmp(Template, "$ship_mdm_03") == 0)
        return Base->ShipMdm[2];
    if (_stricmp(Template, "$ship_lrg_01") == 0)
        return Base->ShipLrg[0];
    if (_stricmp(Template, "$ship_lrg_02") == 0)
        return Base->ShipLrg[1];
    if (_stricmp(Template, "$ship_lrg_03") == 0)
        return Base->ShipLrg[2];
    if (_stricmp(Template, "$terrain_tiny") == 0)
        return Base->TerrainTiny;
    if (_stricmp(Template, "$terrain_sml") == 0)
        return Base->TerrainSml;
    if (_stricmp(Template, "$terrain_mdm") == 0)
        return Base->TerrainMdm;
    if (_stricmp(Template, "$terrain_lrg") == 0)
        return Base->TerrainLrg;
    if (_stricmp(Template, "$terrain_dyna_01") == 0)
        return Base->TerrainDyna01;
    if (_stricmp(Template, "$terrain_dyna_02") == 0)
        return Base->TerrainDyna02;

    FDUMP(kFdumpError, "%s(%d) : *** ERROR: Invalid indirection template name %s in prop\n",
          kObjectPropCpp, 0xd8, Template);
    return 0;
}

void FObjectProp::LoadLoadout(char const* LoadoutName)
{
    // FUN_004467e0.
    Loadout::Map const* LoadoutMap = Loadout::Get(CreateID(LoadoutName));
    if (LoadoutMap == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Failed to find loadout %s\n", kObjectPropCpp, 0x158,
              LoadoutName);
        return;
    }

    Hardpoints = new FObjectPropHardpoints();
    Hardpoints->analyze_instance(reinterpret_cast<long>(Instance));

    std::vector<EquipDesc>::const_iterator It = LoadoutMap->Items.begin();
    for (; It != LoadoutMap->Items.end(); ++It)
        AttachEquipment(&*It, Hardpoints);
}

void FObjectProp::AttachEquipment(void const* ItemPtr, HardpointSummary* Summary)
{
    // FUN_004468f0.
    EquipDesc const* Item = static_cast<EquipDesc const*>(ItemPtr);
    Archetype::Equipment* Arch = Archetype::GetEquipment(Item->get_arch_id());
    Archetype::ICliObj* Companion = Arch->Companion;

    // A light that is always on blinks on its hardpoint.
    if (Arch->get_class_type() == Archetype::AClassLight)
    {
        if (!static_cast<Archetype::Light*>(Arch)->bAlwaysOn)
            return;
        int const Index = Summary->find_name(Item->get_hardpoint().Value);
        if (Index == -1)
            return;
        Flashlights.Add(Companion, Index);
        return;
    }

    if (!Arch->is_attached())
        return;

    void* Child = MiscUtilLoadEngineInstance(Arch->DaArchetype.Value, DALib::Data, nullptr,
                                             nullptr, Archetype::CliObjMaterialPaths(Companion),
                                             &MaterialEntries, nullptr);
    Attached.push_back(Child);

    // FUN_00406cd0.
    void* ArchHandle = DALib::Engine->GetInstanceArchetype(Child);
    Archetype::CliObjRecomputeLodPickExplicit(Companion, reinterpret_cast<long>(ArchHandle),
                                              nullptr, false);
    DALib::Engine->ReleaseHandle(ArchHandle);

    // The item's HP_child goes to the loadout hardpoint (Hardpoint +0x18).
    char const* HpName = Item->get_hardpoint().Value;
    char const* HpChild = static_cast<Archetype::AttachedEquipment*>(Arch)->HpChild.Value;
    if (HpName == nullptr || HpChild == nullptr)
        return;
    long const Parent = Summary->find_name_instance(HpName);
    if (Parent == -1)
        return;
    FHardpointKey ChildKey;
    ChildKey.Name = HpChild;
    ChildKey.Crc = DACOM_CRC::GetCRC32(HpChild);
    FHardpointKey ParentKey;
    ParentKey.Name = HpName;
    ParentKey.Crc = DACOM_CRC::GetCRC32(HpName);
    DALib::Hardpoint->ConnectHardpoints(reinterpret_cast<void*>(Parent), &ParentKey, Child,
                                        &ChildKey);
}
