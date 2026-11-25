// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/cli_asteroid_dynamic.h"

#include "DALib.h"
#include "Space/cli_asteroid_med.h"       // CliAsteroidSlot1bcBody (LAB_005250f0)
#include "Space/Effects/cli_projectile_fx.h"      // CliEffectListRemove (FUN_004fcef0)
#include "Common/client_cliobj.h"          // CliDynamicAsteroidHasParticleEffect / ...ParticleEffect
#include "Space/Effects/client_explosion.h"       // Action_Explode (FUN_005361e0)
#include "Space/client_objdb.h"           // ClientObjDb_RegisterGameobj (FUN_00540c30)
#include "common_archetype.h"
#include "common_misc.h"            // random_on_unit_sphere, ICamera, SUBOBJ_ID_NONE
#include "common_physys.h"
#include "common_server.h"          // IServer::MineAsteroid (slot 62)
#include "common_universe.h"        // Archetype::GetDynamicAsteroidID
#include "dacom.h"                  // FDUMP
#include "dacom_iids.h"
#include "engine/engbase.h"         // IEngine::QueryEngineComponent
#include "Common/engine_light.h"           // FGameLight, GameLightRelease (FUN_00537ad0)
#include "Core/game_options.h"           // GetGameOption (FUN_005792b0)
#include "Space/Effects/particle_effect.h"        // FParticleEffect, CliCreateEffectOneShot (FUN_00503070)
#include "Space/player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"           // GetPlayerCharacterId / GetPlayerConnectionId
#include "render/IMaterial.h"
#include "render/IRenderPipeline.h"
#include "render/mesh_iface.h"      // IMesh
#include "render/rendcomp.h"        // IRenderableMesh, IRenderer2
#include "Network/server_connect.h"         // GetLocalServerInterface (DAT_0067ecd0)
#include "soundmanager.h"           // ISoundSource
#include "Space/Cameras/space_camera.h"           // AsCamera
#include "Space/space_scene_obj_draw.h"   // GameobjSweepEffectListRetail (FUN_004fd040)
#include "Space/star_sys.h"               // StarSysGetActiveSystemId (FUN_00554e90)
#include "Common/Graphics/vis_effect_arch.h"        // FindEffectArch (FUN_0040f080)
#include "x86math.h"                // I3DMathEngine::MulTransform, Transform

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <float.h>

namespace
{
    // .rdata:005dc47c -- CliAsteroid.cpp's FDUMP file argument.
    char const kCliAsteroidSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp";

    // DAT_005caeec: 1/32768, the rand() scale.
    constexpr float kRandScale = 3.0517578125e-05f;
    // DAT_005dce9c: 1/16384 (twice the rand() scale).
    constexpr float kRandScale2 = 6.103515625e-05f;
    // DAT_005d3e44: the alpha a dynamic asteroid must exceed to count as visible / be drawn.
    constexpr float kVisibleAlpha = 0.005f;
    // DAT_005d1494: the zone immersion the spawn point must exceed.
    constexpr float kSpawnImmersion = 0.001f;
    // DAT_005d3e6c: the recycle radius factor (1.05) on the squared placement radius.
    constexpr float kFarFactor = 1.05f;
    // DAT_00613858 (.data, 0.35, never written): the fade band on the squared placement
    // radius.
    constexpr float kFadeFactor = 0.35f;

    // The render-state table loop FUN_005261b0 inlines over kRsAsteroidOpaque / kRsAsteroidBlend
    // (cli_asteroid.h): do-while, so the first state is applied without a zero test.
    void ApplyRsTable(IRenderPipeline* Pipe, unsigned const* Table)
    {
        unsigned State = Table[0];
        do
        {
            Pipe->SetRenderState((D3DRENDERSTATETYPE)State, static_cast<int>(Table[1]));
            State = Table[2];
            Table += 2;
        } while (State != 0);
    }

    IServer* LocalServer()
    {
        return static_cast<IServer*>(GetLocalServerInterface());
    }

    void ParticleSetIntensity(FParticleEffect* Particle, float Intensity)
    {
        Particle->SetIntensity(Intensity);
    }
}

// ===========================================================================
// DynamicAsteroid (the gameobj)
// ===========================================================================

FCliDynamicAsteroidGameobj::FCliDynamicAsteroidGameobj(FCliDynamicAsteroid* Owner)
    : FClientGameobj(EClientGameobjKind::DynamicAsteroid)
{
    SetOwner(Owner);
    Companion[0] = nullptr;
    Companion[1] = nullptr;
    Companion[2] = nullptr;
    Companion[3] = nullptr;
    bActive = 0;
    bPlaced = 0;
    Object = nullptr;
    Nickname.Id = 0;
}

FCliDynamicAsteroidGameobj::~FCliDynamicAsteroidGameobj()
{
    // Retail's this is the CBase sub-object, so +0xb4 / +0xb0 / +0xb8 / +0xac there are the
    // gameobj's +0xc0 / +0xbc / +0xc4 / +0xb8.
    if (Companion[2] != nullptr)
    {
        ParticleEffectRelease(static_cast<FParticleEffect*>(Companion[2]));
        Companion[2] = nullptr;
    }
    if (Companion[1] != nullptr)
    {
        static_cast<ISoundSource*>(Companion[1])->Release();
        Companion[1] = nullptr;
    }
    if (Companion[3] != nullptr)
    {
        GameLightRelease(static_cast<FGameLight*>(Companion[3]));
        Companion[3] = nullptr;
    }
    Companion[0] = nullptr;
}

int FCliDynamicAsteroidGameobj::get_type(uint& OutType) const
{
    OutType = 0x20000000u;
    return 0;
}

void FCliDynamicAsteroidGameobj::create(Archetype::Root* Arch)
{
    GameobjCreateOfClass(this, CObject::ClassDynamicAsteroid, Arch);
    Object->open(Arch);
    Nickname = Arch->Nickname;
}

bool FCliDynamicAsteroidGameobj::find(unsigned const& Id)
{
    (void)Id;
    return false;
}

void FCliDynamicAsteroidGameobj::OnCollision(void const* A, void const* B, FClientGameobj* Other,
                                         void const* D)
{
    // B is not read.
    (void)B;
    // SUBOBJ_ID_NONE's word zero-extended. Unlike GameObject's slot 101 there is no own slot
    // 107 afterwards.
    Other->OnSurfaceCollisionHit(A, D, this,
                   reinterpret_cast<void const*>(static_cast<uintptr_t>(SUBOBJ_ID_NONE)));

    if (bActive == 0)
        return;
    CObject* const OtherObj = Other->GetObject();
    if (OtherObj == nullptr || (static_cast<unsigned>(OtherObj->ObjectClass) & 7u) != 7u)
        return;

    Explode(static_cast<Vector const*>(A));

    // The shot's owner (+0xe4) must be the player's character (DAT_00673378).
    if (static_cast<CProjectile const*>(OtherObj)->OwnerId != GetPlayerCharacterId())
        return;
    FCliDynamicAsteroid* const Owner = GetOwner();
    if (Owner == nullptr)
        return;

    // The manager's common field (no null test) at the camera position the draw last stored
    // (+0x28).
    Universe::IZone const* const Zone = Owner->CmnField->get_lootable_zone(Owner->CameraPos);
    if (Zone == nullptr)
        return;
    CmnAsteroid::LootableZoneEntry const* const Loot =
        static_cast<CmnAsteroid::LootableZoneEntry const*>(Zone->GetLootableInfo());
    if (Loot == nullptr)
        return;

    // On only while 1 / dynamic_loot_difficulty is strictly greater than the roll (NaN not).
    double const Roll = static_cast<double>(rand()) * kRandScale;
    double const Chance = 1.0 / static_cast<double>(Loot->DynamicLootDifficulty);
    if (!(Chance > Roll))
        return;

    // min and max - min are rounded to float first.
    float const Min = static_cast<float>(Loot->DynamicLootCountMin);
    float const InvDetail = Owner->InvDetailScale;
    float const Range = static_cast<float>(static_cast<double>(Loot->DynamicLootCountMax) - Min);
    double const Scaled =
        static_cast<double>(rand()) * InvDetail * Range * kRandScale + Min + 0.5f;
    unsigned const Quantity = static_cast<unsigned>(static_cast<int>(Scaled));

    // get_position runs before StarSysGetActiveSystemId, as retail.
    Vector const& Pos = get_position();
    unsigned const SystemId = StarSysGetActiveSystemId();
    LocalServer()->MineAsteroid(SystemId, Pos, Loot->DynamicLootContainerId.Id,
                                Loot->DynamicLootCommodityId.Id, Quantity,
                                GetPlayerConnectionId());
}

void FCliDynamicAsteroidGameobj::Explode(Vector const* Pos)
{
    bActive = 0;
    if (Object != nullptr)
        PhySys::SetCollisionState(Object, false);
    CliEffectListRemove(&EffectList, Companion);
    Vector const* const Where = (Pos != nullptr) ? Pos : &get_position();
    Vector const Copy = *Where;

    if (ExplosionArch == nullptr)
        return;
    if (ExplosionArch->Process != 2)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x84c,
              "Explosion process for dynamic asteroid should be set to NONE");
        return;
    }
    Actions.add(Action_Explode::Create(ExplosionArch, this, &Copy));
}

unsigned short* FCliDynamicAsteroidGameobj::Slot1bc(unsigned short* Out, unsigned A, unsigned B,
                                                   unsigned C, unsigned D)
{
    (void)A;
    (void)B;
    (void)C;
    (void)D;
    return CliAsteroidSlot1bcBody(Out);
}

void FCliDynamicAsteroidGameobj::Place(Vector const* Pos, float MaxVelocity,
                                       float MaxAngularVelocity)
{
    // _control87(0, 0) only reads the control word, as retail.
    Matrix const Identity(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    _control87(0, 0);
    // No null test of Object.
    PhySys::BeamR(Object, *Pos, Identity, true);

    // Each stored as a float.
    float const X = static_cast<float>(rand()) * kRandScale2 - 1.0f;
    float const Y = static_cast<float>(rand()) * kRandScale2 - 1.0f;
    float const Z = static_cast<float>(rand()) * kRandScale2 - 1.0f;
    // Each squared, the square negated unless the value is above 0 (NaN negates).
    float SignedX = X * X;
    if (!(X > 0.0f))
        SignedX = -SignedX;
    float SignedY = Y * Y;
    if (!(Y > 0.0f))
        SignedY = -SignedY;
    float SignedZ = Z * Z;
    if (!(Z > 0.0f))
        SignedZ = -SignedZ;

    Vector Target;
    Target.x = SignedX * MaxVelocity;
    Target.y = SignedY * MaxVelocity;
    Target.z = SignedZ * MaxVelocity;
    Vector const Current = PhySys::GetVelocity(Object);
    Vector Delta;
    Delta.x = Target.x - Current.x;
    Delta.y = Target.y - Current.y;
    Delta.z = Target.z - Current.z;
    PhySys::AddToVelocity(Object, Delta);

    // r1 is drawn first (used for y) and kept as a float; r2 goes to x.
    Vector const AngCurrent = PhySys::GetAngularVelocityOS(Object);
    float const R1 = static_cast<float>(rand()) * kRandScale;
    float const R2Scaled = static_cast<float>(rand()) * kRandScale * MaxAngularVelocity;
    float const AngX = (R2Scaled + R2Scaled) - MaxAngularVelocity;
    float const R1Scaled = R1 * MaxAngularVelocity;
    float const AngY = (R1Scaled + R1Scaled) - MaxAngularVelocity;
    float const AngZ =
        static_cast<float>(rand()) * MaxAngularVelocity * kRandScale2 - MaxAngularVelocity;
    Vector AngDelta;
    AngDelta.x = AngX - AngCurrent.x;
    AngDelta.y = AngY - AngCurrent.y;
    AngDelta.z = AngZ - AngCurrent.z;
    PhySys::AddToAngularVelocityOS(Object, AngDelta);
    // Retail calls GetAngularVelocityOS once more and drops the result.
    (void)PhySys::GetAngularVelocityOS(Object);

    if (Companion[2] != nullptr)
        ParticleSetIntensity(static_cast<FParticleEffect*>(Companion[2]), 0.0f);
}

FCliDynamicAsteroid* FCliDynamicAsteroidGameobj::GetOwner() const
{
    FCliDynamicAsteroid* Owner = nullptr;
    std::memcpy(&Owner, &bWatchListed, sizeof(Owner));
    return Owner;
}

void FCliDynamicAsteroidGameobj::SetOwner(FCliDynamicAsteroid* Owner)
{
    static_assert(sizeof(FCliDynamicAsteroid*) == 4, "the +0xa8 owner is one dword");
    std::memcpy(&bWatchListed, &Owner, sizeof(Owner));
}

// ===========================================================================
// The [DynamicAsteroids] manager
// ===========================================================================

FCliDynamicAsteroid* CliDynamicAsteroidConstruct(FCliDynamicAsteroid* This,
                                                 CmnAsteroid::CAsteroidField* CmnField)
{
    // In retail store order. SlotCount (+0x14), CameraPos (+0x28), MeshDesc (+0x38),
    // PrimitiveType (+0xac), the two totals (+0xb4 / +0xb8) and InvDetailScale (+0xe4) are not
    // written; CliAsteroidLoadFieldIni sets InvDetailScale right after.
    This->CmnField = CmnField;
    This->TintedDiffuse0 = 1.0f;
    This->TintedDiffuse1 = 1.0f;
    This->TintedDiffuse2 = 1.0f;
    This->TintedAmbient0 = 1.0f;
    This->TintedAmbient1 = 1.0f;
    This->TintedAmbient2 = 1.0f;
    This->ColorShift0 = 1.0f;
    This->ColorShift1 = 1.0f;
    This->ColorShift2 = 1.0f;
    This->DetailScale = 1.0f;
    This->MaxVelocity = 50.0f;
    This->MaxAngularVelocity = 5.0f;
    This->CountIni = 10;
    This->PlacementRadius = 150.0f;
    This->PlacementRadiusInner = 75.0f;
    This->Arch = nullptr;
    This->SlotPtrs = nullptr;
    This->bSpawnAllowed = 0;
    This->bAnyVisible = 0;
    This->Zone = nullptr;
    This->Material = nullptr;
    This->Mesh = nullptr;
    if (This->CmnField == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kCliAsteroidSourceFile, 0x8ab,
              "NULL common field passed to dynamic asteroid manager");
    }
    return This;
}

void CliDynamicAsteroidReleaseSlots(FCliDynamicAsteroid* This)
{
    if (This->SlotPtrs == nullptr)
        return;
    for (int Index = 0; Index < This->SlotCount; ++Index)
    {
        if (This->SlotPtrs[Index] == nullptr)
            continue;
        This->SlotPtrs[Index]->MarkForRemoval(true);
        This->SlotPtrs[Index] = nullptr;
    }
}

void CliDynamicAsteroidDestroy(FCliDynamicAsteroid* This)
{
    // Arch (+8) is left as it is after the release.
    if (This->Arch != nullptr)
        This->Arch->Release();
    // Retail inlines the slot loop.
    CliDynamicAsteroidReleaseSlots(This);
    ::operator delete(This->SlotPtrs);
    This->SlotPtrs = nullptr;
    if (This->Material != nullptr)
    {
        This->Material->Release();
        This->Material = nullptr;
    }
    // Mesh (+0xb0) is not cleared, as retail.
    FCliDynamicAsteroidMesh* const Mesh = This->Mesh;
    if (Mesh != nullptr)
    {
        ::operator delete(Mesh->Verts);
        Mesh->Verts = nullptr;
        Mesh->VertCount = 0;
        ::operator delete(Mesh->Indices);
        Mesh->Indices = nullptr;
        Mesh->IndexCount = 0;
        ::operator delete(Mesh);
    }
}

void CliDynamicAsteroidParseIni(FCliDynamicAsteroid* This, INI_Reader& Reader)
{
    if (Reader.read_value())
    {
        do
        {
            if (Reader.is_value("asteroid"))
            {
                char const* const Name = Reader.get_value_string(0);
                ID_String Id;
                Id.Id = CreateID(Name);
                This->Arch = Archetype::GetDynamicAsteroid(Archetype::GetDynamicAsteroidID(Id));
                if (This->Arch == nullptr)
                {
                    FDUMP(kFdumpWarning,
                          "%s(%d) : *** WARNING: dynamic asteroid archetype (%s) not found",
                          kCliAsteroidSourceFile, 0x8cb, Name);
                }
                if (This->Arch != nullptr)
                {
                    This->Arch->load();
                    if (This->Arch->Companion != nullptr)
                        This->Arch->Companion->Load(0);
                }
            }
            else if (Reader.is_value("count"))
            {
                This->CountIni = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("placement_radius"))
            {
                // 0.7 is a double (DAT_005cf0f8).
                float const Radius = Reader.get_value_float(0);
                This->PlacementRadius = Radius;
                This->PlacementRadiusInner = static_cast<float>(Radius * 0.7);
            }
            else if (Reader.is_value("max_velocity"))
            {
                This->MaxVelocity = Reader.get_value_float(0);
            }
            else if (Reader.is_value("max_angular_velocity"))
            {
                This->MaxAngularVelocity = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color_shift"))
            {
                // All three read, then stored.
                float const R = Reader.get_value_float(0);
                float const G = Reader.get_value_float(1);
                float const B = Reader.get_value_float(2);
                This->ColorShift0 = R;
                This->ColorShift1 = G;
                This->ColorShift2 = B;
            }
        } while (Reader.read_value());
    }

    CliDynamicAsteroidReleaseSlots(This);
    ::operator delete(This->SlotPtrs);
    This->SlotPtrs = nullptr;
    This->SlotCount = static_cast<int>(
        static_cast<double>(This->CountIni) * This->DetailScale + 0.5f);
    This->SlotPtrs = static_cast<FCliDynamicAsteroidGameobj**>(::operator new(
        static_cast<unsigned>(This->SlotCount) * sizeof(FCliDynamicAsteroidGameobj*)));
    for (int Index = 0; Index < This->SlotCount; ++Index)
        This->SlotPtrs[Index] = nullptr;

    if (This->Material != nullptr)
    {
        This->Material->Release();
        This->Material = nullptr;
    }
    This->MeshVertTotal = 0;
    This->MeshIndexTotal = 0;

    if (This->Arch != nullptr)
    {
        IMesh* Mesh = nullptr;
        IRenderableMesh* RMesh = nullptr;
        int const EngineIndex = static_cast<int>(This->Arch->EngineIndex);
        if (DALib::Engine->QueryEngineComponent(EngineIndex, Iids::Mesh,
                                                reinterpret_cast<void**>(&Mesh)) < 0)
        {
            // No mesh block and no vertex source descriptor.
            This->Mesh = nullptr;
            if (RMesh != nullptr)
                RMesh->Release();
            if (Mesh != nullptr)
                Mesh->Release();
            return;
        }
        Mesh->OpenFaceGroup(0, 1);
        if (This->Material == nullptr)
            Mesh->GetFaceGroupMaterial(&This->Material);
        Mesh->GetFaceGroupPrimitiveType(&This->PrimitiveType);
        Mesh->CloseFaceGroup();
        DALib::Renderer->SetArchetypeLod(static_cast<intptr_t>(EngineIndex), 0);
        // The result is not tested, as retail.
        DALib::Engine->QueryEngineComponent(EngineIndex,
                                            Iids::RenderableMesh,
                                            reinterpret_cast<void**>(&RMesh));
        FCliDynamicAsteroidMesh* const Block =
            static_cast<FCliDynamicAsteroidMesh*>(::operator new(sizeof(FCliDynamicAsteroidMesh)));
        Block->Verts = nullptr;
        Block->Indices = nullptr;
        Block->IndexCount = 0;
        Block->VertCount = 0;
        This->Mesh = Block;
        // Retail's per-vertex texcoord constructors (FUN_0043d950) write nothing.
        RMesh->GetVertexCount(&Block->VertCount);
        Block->Verts = static_cast<unsigned char*>(
            ::operator new(static_cast<unsigned>(Block->VertCount) * 0x2c));
        This->MeshVertTotal += static_cast<unsigned>(Block->VertCount);
        RMesh->CopyVertexData(Block->Verts, 0x252u,
                     0, Block->VertCount);
        RMesh->GetIndexCount(&Block->IndexCount);
        Block->Indices = static_cast<unsigned short*>(
            ::operator new(static_cast<unsigned>(Block->IndexCount) * 2));
        RMesh->CopyIndices(&Block->Indices, Block->IndexCount);
        This->MeshIndexTotal += static_cast<unsigned>(Block->IndexCount);
        if (RMesh != nullptr)
            RMesh->Release();
        if (Mesh != nullptr)
            Mesh->Release();
    }

    // With no archetype too, through whatever +0xb0 holds (no null test, as retail).
    CliAsteroidFillVertexSource(&This->MeshDesc, This->Mesh->Verts,
                                static_cast<unsigned short>(This->MeshVertTotal));
}

FCliDynamicAsteroidGameobj* CliDynamicAsteroidSpawn(FCliDynamicAsteroid* This)
{
    FCliDynamicAsteroidGameobj* const Go = new FCliDynamicAsteroidGameobj(This);
    Go->create(This->Arch);
    // The object narrowed by class & 0x4001 (null otherwise, not tested).
    CObject* const Obj = Go->GetObject();
    CDynamicAsteroid* const Dynamic =
        (Obj != nullptr &&
         (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassDynamicAsteroid) == CObject::ClassDynamicAsteroid)
            ? static_cast<CDynamicAsteroid*>(Obj)
            : nullptr;
    CDynamicAsteroid::CreateParms Parms;
    Parms.SystemId = 0;
    Parms.Velocity.x = 0.0f;
    Parms.Velocity.y = 0.0f;
    Parms.Velocity.z = 0.0f;
    Parms.AngularVelocity.x = 0.0f;
    Parms.AngularVelocity.y = 0.0f;
    Parms.AngularVelocity.z = 0.0f;
    Parms.SystemId = StarSysGetActiveSystemId();
    Dynamic->init(Parms);
    // Collisions off while it waits to be placed.
    if (Go->GetObject() != nullptr)
        PhySys::SetCollisionState(Go->GetObject(), false);
    Go->PostCreateInit();
    ClientObjDb_RegisterGameobj(Go, false);
    Go->bActive = 0;
    Go->bPlaced = 0;
    return Go;
}

void CliDynamicAsteroidUpdate(FCliDynamicAsteroid* This, FSpaceCamera* Cam, float Dt,
                              Exclusion::AsteroidExclusionZoneMgr* ExclusionMgr)
{
    // Dt is not read.
    (void)Dt;

    if (GetGameOption(kGameOptDynAsteroids) == 0)
    {
        if (This->SlotPtrs == nullptr)
            return;
        for (int Index = 0; Index < This->SlotCount; ++Index)
        {
            if (This->SlotPtrs[Index] == nullptr)
                continue;
            This->SlotPtrs[Index]->MarkForRemoval(true);
            This->SlotPtrs[Index] = nullptr;
        }
        ::operator delete(This->SlotPtrs);
        This->SlotPtrs = nullptr;
        return;
    }

    if (This->SlotPtrs == nullptr)
    {
        This->SlotCount = static_cast<int>(
            static_cast<double>(This->CountIni) * This->DetailScale + 0.5f);
        This->SlotPtrs = static_cast<FCliDynamicAsteroidGameobj**>(::operator new(
            static_cast<unsigned>(This->SlotCount) * sizeof(FCliDynamicAsteroidGameobj*)));
        for (int Index = 0; Index < This->SlotCount; ++Index)
            This->SlotPtrs[Index] = nullptr;
    }

    // The spawn centre: the camera position plus the view axis (the orientation's third
    // column) times -PlacementRadiusInner.
    ICamera* const Camera = AsCamera(Cam);
    const Matrix* const Orient = Camera->GetOrientation();
    float const NegInner = -This->PlacementRadiusInner;
    float const OffsetX = Orient->E[0][2] * NegInner;
    float const OffsetY = Orient->E[1][2] * NegInner;
    float const OffsetZ = Orient->E[2][2] * NegInner;
    const Vector* const CamPos = Camera->GetPosition();
    Vector Centre;
    Centre.x = OffsetX + CamPos->x;
    Centre.y = OffsetY + CamPos->y;
    Centre.z = OffsetZ + CamPos->z;

    // No spawning while the player's ship (class & 0x503) is in a tradelane; otherwise only
    // when there is no zone or its immersion at the centre is above 0.001 (NaN blocks).
    FClientGameobj* const PlayerGo =
        static_cast<FClientGameobj*>(static_cast<IObjRW*>(GetPlayerObjRW()));
    CObject* const PlayerObj = (PlayerGo != nullptr) ? PlayerGo->GetObject() : nullptr;
    if (PlayerObj != nullptr &&
        (static_cast<unsigned>(PlayerObj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip &&
        static_cast<CShip*>(PlayerObj)->is_using_tradelane())
    {
        This->bSpawnAllowed = 0;
    }
    else if (This->Zone != nullptr && !(This->Zone->GetImmersion(Centre) > kSpawnImmersion))
    {
        This->bSpawnAllowed = 0;
    }
    else
    {
        This->bSpawnAllowed = 1;
    }
    // An excluded centre blocks it too.
    if (This->bSpawnAllowed != 0 && ExclusionMgr != nullptr)
    {
        This->bSpawnAllowed =
            ExclusionMgr->is_excluded(Centre, Exclusion::EOT_DynamicAsteroid, nullptr) ? 0 : 1;
    }

    // The recycle distance and the fade band are kept as doubles.
    float const Radius = This->PlacementRadius;
    double const RadiusSq = static_cast<double>(Radius) * Radius;
    double const FarSq = kFarFactor * RadiusSq;
    double const FadeSq = RadiusSq * kFadeFactor;
    double const BandStart = FarSq - FadeSq;
    This->bAnyVisible = 0;

    for (int Index = 0; Index < This->SlotCount; ++Index)
    {
        FCliDynamicAsteroidGameobj* Go = This->SlotPtrs[Index];
        float DistSq = 0.0f;
        bool bFade = false;
        if (Go != nullptr && Go->bPlaced != 0)
        {
            // Summed z, y, then x, stored as a float; inside the recycle distance -> the fade.
            Vector const& Pos = Go->get_position();
            float const Dx = Pos.x - Centre.x;
            float const Dy = Pos.y - Centre.y;
            float const Dz = Pos.z - Centre.z;
            DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
            if (!(DistSq > FarSq))
                bFade = true;
            else
                Go->bPlaced = 0;
        }

        if (bFade)
        {
            if (This->SlotPtrs[Index]->bActive == 0)
                continue;
            double Alpha = 1.0;
            if (DistSq > BandStart)
                Alpha = (FarSq - DistSq) / FadeSq;
            This->SlotPtrs[Index]->Alpha = static_cast<float>(Alpha);
            if (Alpha > kVisibleAlpha)
                This->bAnyVisible = 1;
            continue;
        }

        if (This->bSpawnAllowed == 0)
        {
            // Out of range with spawning blocked -- released.
            if (This->SlotPtrs[Index] != nullptr)
            {
                This->SlotPtrs[Index]->MarkForRemoval(true);
                This->SlotPtrs[Index] = nullptr;
            }
            continue;
        }

        // Both conditions below only warn.
        if (This->SlotPtrs[Index] == nullptr)
            This->SlotPtrs[Index] = CliDynamicAsteroidSpawn(This);
        if (This->SlotPtrs[Index] == nullptr)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x986,
                  "dynamic asteroid not created");
        }
        if (This->SlotPtrs[Index]->bPlaced != 0)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x988,
                  "dynamic asteroid should be flagged as !valid_pos");
        }

        float const PlaceRadius = This->PlacementRadius;
        Vector const Unit = random_on_unit_sphere();
        Vector Place;
        Place.x = PlaceRadius * Unit.x + Centre.x;
        Place.y = PlaceRadius * Unit.y + Centre.y;
        Place.z = PlaceRadius * Unit.z + Centre.z;

        Go = This->SlotPtrs[Index];
        CliEffectListRemove(&Go->EffectList, Go->Companion);
        if (This->SlotPtrs[Index]->GetObject() != nullptr)
            PhySys::SetCollisionState(This->SlotPtrs[Index]->GetObject(), false);
        This->SlotPtrs[Index]->Place(&Place, This->MaxVelocity, This->MaxAngularVelocity);
        if (This->SlotPtrs[Index]->GetObject() != nullptr)
            PhySys::SetCollisionState(This->SlotPtrs[Index]->GetObject(), true);

        // An effect that is still alive (the keep tests of GameobjSweepEffectListRetail) warns
        // and is dropped.
        Go = This->SlotPtrs[Index];
        Archetype::DynamicAsteroid* const Arch = This->Arch;
        FParticleEffect* const Particle = static_cast<FParticleEffect*>(Go->Companion[2]);
        ISoundSource* const Sound = static_cast<ISoundSource*>(Go->Companion[1]);
        FGameLight* const Light = static_cast<FGameLight*>(Go->Companion[3]);
        bool bLive = false;
        if (Particle != nullptr && (Particle->Flags08 & 2u) == 0u)
            bLive = true;
        else if (Sound != nullptr && Sound->IsActive() != 0)
            bLive = true;
        else if (Light != nullptr && Light->Anim.Keys == nullptr)
            bLive = true;
        if (bLive)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x87c,
                  "dynamic asteroid effect already created");
            CliEffectListRemove(&Go->EffectList, Go->Companion);
        }

        // The archetype companion's particle_effect as a one-shot on the gameobj (DAT_00674c60)
        // with an identity frame.
        Archetype::ICliObj* const CliComp = Arch->Companion;
        if (CliComp != nullptr && Archetype::CliDynamicAsteroidHasParticleEffect(CliComp))
        {
            Transform Frame;
            Frame.Rotation.E[0][0] = 1.0f;
            Frame.Rotation.E[0][1] = 0.0f;
            Frame.Rotation.E[0][2] = 0.0f;
            Frame.Rotation.E[1][0] = 0.0f;
            Frame.Rotation.E[1][1] = 1.0f;
            Frame.Rotation.E[1][2] = 0.0f;
            Frame.Rotation.E[2][0] = 0.0f;
            Frame.Rotation.E[2][1] = 0.0f;
            Frame.Rotation.E[2][2] = 1.0f;
            Frame.Translation.x = 0.0f;
            Frame.Translation.y = 0.0f;
            Frame.Translation.z = 0.0f;
            int const EngineIndex = static_cast<int>(Go->get_index());
            ID_String const* const EffectId = Archetype::CliDynamicAsteroidParticleEffect(CliComp);
            if (!EffectId->IsEmpty())
            {
                CliCreateEffectOneShot(FindEffectArch(&EffectId->Id), Go, EngineIndex, &Frame,
                                       Go->Companion);
            }
        }

        This->SlotPtrs[Index]->bPlaced = 1;
        This->SlotPtrs[Index]->bActive = 1;
        This->bAnyVisible = 1;
    }
}

void CliDynamicAsteroidDraw(FCliDynamicAsteroid* This, FSpaceCamera* Cam,
                            unsigned char* bAmbientDone, FCliAsteroid* Field)
{
    if (GetGameOption(kGameOptDynAsteroids) == 0)
        return;
    if (This->SlotPtrs == nullptr)
        return;
    if (Cam == nullptr)
        return;

    // The camera position into +0x28 (read by the loot roll).
    ICamera* const Camera = AsCamera(Cam);
    const Vector* const CamPos = Camera->GetPosition();
    This->CameraPos.x = CamPos->x;
    This->CameraPos.y = CamPos->y;
    This->CameraPos.z = CamPos->z;
    if (This->bAnyVisible == 0)
        return;

    IVertexBufferManager* const VbMgr = DALib::VertexBuffer;
    FVertexMemoryLock Lock;
    if (VbMgr->LockVertexMemory(static_cast<unsigned int>(This->MeshDesc.SrcFvf),
                                static_cast<unsigned int>(This->MeshDesc.SrcCount), &Lock) < 0)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x9bd,
              "could not lock the vertex buffer for dynamic asteroids");
        return;
    }
    VbMgr->CopyVertexData(Lock.Vertices, Lock.Listing.Fvf, &This->MeshDesc, 0, This->MeshDesc.SrcCount);
    VbMgr->UnlockVertexMemory(&Lock);

    if (This->Material != nullptr)
        This->Material->Bind(nullptr);

    IRenderPipeline* const Pipe = DALib::Pipeline;
    ApplyRsTable(Pipe, kRsAsteroidOpaque);
    int SavedAlphaBlend = 0;
    Pipe->GetRenderState(D3DRS_ALPHAREF, &SavedAlphaBlend);
    Pipe->SetRenderState(D3DRS_ALPHAREF, 5);

    // The 0x44-byte material block; without LIGHTING the ambient is the diffuse (and its
    // alpha).
    unsigned char bBlending = 0;
    float MaterialBlock[0x11] = {};
    int const bLighting = GetGameOption(kGameOptLighting);
    MaterialBlock[0] = This->TintedDiffuse0;
    MaterialBlock[1] = This->TintedDiffuse1;
    MaterialBlock[2] = This->TintedDiffuse2;
    if (bLighting != 0)
    {
        MaterialBlock[4] = This->TintedAmbient0;
        MaterialBlock[5] = This->TintedAmbient1;
        MaterialBlock[6] = This->TintedAmbient2;
    }
    else
    {
        MaterialBlock[4] = MaterialBlock[0];
        MaterialBlock[7] = MaterialBlock[3];
        MaterialBlock[5] = MaterialBlock[1];
        MaterialBlock[6] = MaterialBlock[2];
    }

    Transform View;
    std::memcpy(&View, Camera->GetViewTransform(), sizeof(View));

    for (int Index = 0; Index < This->SlotCount; ++Index)
    {
        FCliDynamicAsteroidGameobj* const Go = This->SlotPtrs[Index];
        if (Go == nullptr || Go->bActive == 0)
            continue;
        if (Go->Alpha <= kVisibleAlpha)
            continue;

        // The field's ambient pass once per frame.
        if (*bAmbientDone == 0)
        {
            CliAsteroidLightAround(Field, Cam, 100.0f);
            *bAmbientDone = 1;
        }

        // Pipeline state 7 set -> the combined modelview; otherwise world and view apart.
        Transform World;
        std::memcpy(&World, &This->SlotPtrs[Index]->get_transform(), sizeof(World));
        int bModelview = 1;
        Pipe->GetPipelineState(7, &bModelview);
        if (bModelview != 0)
        {
            Transform Combined;
            CliAsteroidMathEngine()->MulTransform(Combined, View, World);
            Transform Modelview = Combined;
            Pipe->SetModelview(&Modelview);
        }
        else
        {
            Pipe->SetWorld(&World);
            Pipe->SetView(&View);
        }

        // Not below 1.0 (NaN included) -> the opaque states every time; below 1.0 -> the blend
        // states, once until an opaque slot resets the flag.
        MaterialBlock[3] = This->SlotPtrs[Index]->Alpha;
        if (!(MaterialBlock[3] < 1.0f))
        {
            ApplyRsTable(Pipe, kRsAsteroidOpaque);
            Pipe->SetRenderState(D3DRS_ALPHAREF, 0x7f);
            bBlending = 0;
        }
        else if (bBlending == 0)
        {
            ApplyRsTable(Pipe, kRsAsteroidBlend);
            Pipe->SetRenderState(D3DRS_ALPHAREF, 5);
            bBlending = 1;
        }

        Pipe->SetMaterial((const D3DMATERIAL8*)MaterialBlock);
        Pipe->DrawIndexedPrimitiveVb(This->PrimitiveType, Lock.Listing.VertexBuffer,
                                     static_cast<unsigned int>(Lock.Listing.BaseVertex),
                                     static_cast<unsigned int>(This->MeshDesc.SrcCount),
                                     This->Mesh->Indices,
                                     static_cast<unsigned int>(This->Mesh->IndexCount), 0);

        // The particle's intensity follows the alpha.
        if (This->SlotPtrs[Index]->Companion[2] != nullptr)
        {
            ParticleSetIntensity(static_cast<FParticleEffect*>(This->SlotPtrs[Index]->Companion[2]),
                                 MaterialBlock[3]);
        }
        GameobjSweepEffectListRetail(&This->SlotPtrs[Index]->EffectList);
    }

    Pipe->SetRenderState(D3DRS_ALPHAREF, SavedAlphaBlend);
    ApplyRsTable(Pipe, kRsAsteroidOpaque);
}
