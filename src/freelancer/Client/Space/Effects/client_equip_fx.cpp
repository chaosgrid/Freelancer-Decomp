// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Client equipment-FX — EquippedObj factory + FlashLight light arm.

#include "Space/Effects/client_equip_fx.h"

#include "Space/Effects/cli_projectile_fx.h"
#include "Space/Effects/cli_tractor_beam.h" // CliTractorBeamCreate / FCliTractorBeam::Init (FUN_0052fa10)
#include "Space/Effects/effect_base.h"
#include "Space/Effects/effect_type_info.h" // EFT_FLASHLIGHT
#include "Common/engine_light.h"
#include "Space/Effects/particle_effect.h"
#include "DALib.h"
#include "engine/engbase.h"
#include "Common/Audio/menu_sound_arch.h"
#include "Interface/Pages/character_select_equip.h"
#include "Common/client_cliobj.h" // CliTradeLaneAddEffectRef / CliTradeLaneReleaseEffectRef
#include "Space/client_gameobj.h"
#include "Space/client_objdb.h"
#include "common_animdb.h" // AnimDB::Add (LAB_00516ea0)
#include "common_archetype.h"
#include "common_cequip.h"
#include "common_cobject.h"
#include "common_csys.h"
#include "Space/Effects/client_fuse.h"
#include "common_goods.h"
#include "GameFlow/gf_object_ship.h" // ShipTransposeRotate (FUN_00447be0)
#include "common_misc.h"  // SinglePlayer
#include "Core/game_options.h"
#include "NeuroNet/nn_messaging.h" // NnVoiceEnqueue (FUN_00489e80) -- engine notify 0xa
#include "Space/player_behavior.h" // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"
#include "render/IFLAppearance.h" // RemoveAppearanceInstanceValue (FUN_00532170)
#include "render/IRenderPipeline.h"
#include "render/shading.h"
#include "Common/Graphics/screen_quad_state.h"
#include "Space/Cameras/space_camera.h"
#include "soundmanager.h"
#include "Common/Graphics/texture_shapes.h"
#include "Common/Graphics/vis_effect_arch.h"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include <windows.h>

namespace
{
    char const* HardpointNameOf(CEquip* Equip)
    {
        CExternalEquip* Ext = CExternalEquip::cast(Equip);
        if (Ext == nullptr)
            return "(internal)";
        char const* Hp = Ext->HardpointName.Value;
        if (Hp == nullptr || Hp[0] == '\0')
            return "(no-hp)";
        return Hp;
    }

    // Retail Archetype::CliEQLight (vtable 005c7eb8, sizeof 0x98, ctor FUN_0040a9e0) is private
    // to client_cliobj.cpp (FCliEQLight); this is its tail from +0x54 as FlashLight.cpp reads
    // it. Names are the INI keys of its ReadUnknown (LAB_0040ab30).
    struct FCliEQLightView
    {
        float Color[3];          // +0x54 "color"
        float GlowColor[3];      // +0x60 "glow_color"
        float MinColor[3];       // +0x6c "min_color"
        float BulbSize;          // +0x78 "bulb_size"
        float GlowSize;          // +0x7c "glow_size"
        float AvgDelay;          // +0x80 "avg_delay"
        float BlinkDuration;     // +0x84 "blink_duration"
        float Intensity;         // +0x88 "intensity" -- the real light's radius (FUN_00411300)
        float FlareConeCos[2];   // +0x8c cos(second value), +0x90 cos(first value)
        float LightsourceCone;   // +0x94 "lightsource_cone"
    };
    static_assert(sizeof(FCliEQLightView) == 0x98 - 0x54, "CliEQLight tail +0x54..+0x98");

    // Every reader dereferences the companion without a test.
    FCliEQLightView const* CompanionAsLight(Archetype::ICliObj* Companion)
    {
        return reinterpret_cast<FCliEQLightView const*>(
            reinterpret_cast<unsigned char const*>(Companion) + 0x54);
    }

    struct FCliEquipGameLight;

    // Client\space\FlashLight.cpp's CliEquipLight: operator new through FUN_004f79a0(0x9c),
    // constructor FUN_004fd9d0, the Effect head (FUN_004f79f0, type 0x2e = EFT_EQUIP_LIGHT's
    // bucket) and this tail. Retail links it into bucket 0x2e from the owner's effect list sweep
    // (FUN_004fd040 sets +8 bit 0 and FUN_004f7af0 for a node's effect slot), and the bucket walk
    // FUN_004f7810 then runs slots +0x14 / +0x18 / +0x1c / +0x10 on it.
    //
    // PTR_LAB_005da3f8 (9 slots) -- G_CliEquipLightVtable below:
    //   0 LAB_004f7ab0  Effect base start (FEffectBase::Start)
    //   1 FUN_004f7bd0  Effect base kill (FEffectBase::Kill)
    //   2 LAB_004fe040  world position out, radius back
    //   3 0x0053e4a0    empty (a shared folded stub)
    //   4 LAB_004fd990  Flags +8 bit 0
    //   5 LAB_004fdcf0  clear bit 0, return the old bit 0
    //   6 FUN_004fdd00  blink tick
    //   7 LAB_004fdfe0  FLASHLIGHTS gate, then the flare FUN_004fdda0
    //   8 FUN_004fd9a0  scalar deleting dtor (body FUN_004fd920, free 0x9c)
    struct FCliEquipLight : FEffectBase
    {
        explicit FCliEquipLight(unsigned char bPlayerOwnedParam);
        ~FCliEquipLight() override;
        float GetWorldPos(Vector* Out) override;
        unsigned char IsAlive() override;
        unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
        unsigned char TickDraw(float Dt, FSpaceCamera* Cam) override;
        unsigned char BuildGeometry(FSpaceCamera* Cam) override;
        Archetype::ICliObj* Companion;      // +0x6c the CliEQLight (FUN_004fda00)
        float BlinkDuty;                    // +0x70
        float BlinkRate;                    // +0x74
        float BlinkPhase;                   // +0x78
        float BlinkBrightness;              // +0x7c
        unsigned char bPlayerOwned;         // +0x80 FUN_004fd9d0's argument
        unsigned char Pad81[3];
        unsigned long TexFrame[5];          // +0x84 TextureLib slot +0x58 frame of "shine"
        FCliEquipGameLight* EngineLight;    // +0x98 the real light (player-owned only)
    };
    static_assert(sizeof(FCliEquipLight) == 0x9c, "CliEquipLight 0x9c");
    static_assert(offsetof(FCliEquipLight, Companion) == 0x6c, "Companion");
    static_assert(offsetof(FCliEquipLight, EngineLight) == 0x98, "EngineLight");

    // The real light FUN_004fda00 creates for a player-owned CliEquipLight: FUN_00537a90(0x130),
    // GameLight constructor FUN_005374e0, +0x12c = the owner, vtable PTR_FUN_005da424 =
    // {FUN_005375a0, FUN_00537690, FUN_005376f0, LAB_004fdcb0}.
    struct FCliEquipGameLight : FGameLight
    {
        void Tick(float Dt) override;
        FCliEquipLight* Owner;              // +0x12c
    };
    static_assert(sizeof(FCliEquipGameLight) == 0x130, "CliEquipLight GameLight 0x130");

    // EquippedObj+0xb8 list node (FUN_0042a7e0 operator_new(0xc)), published as FEquipFxListNode.
    using FCliEquipFxListNode = FEquipFxListNode;

    // EquippedObj +0xb8 / +0xbc.
    FCliEquipFxListNode*& EquipFxSentinelRef(FClientGameobj* Host)
    {
        return reinterpret_cast<FCliEquipFxListNode*&>(
            static_cast<FClientEquippedObj*>(Host)->EquipFx.Head);
    }

    int& EquipFxCountRef(FClientGameobj* Host)
    {
        return static_cast<FClientEquippedObj*>(Host)->EquipFx.Count;
    }

    // EquippedObj +0xc4 / +0xc8 -- the std::list of FFuseBurnNode (FUN_00534240 Ignite /
    // FUN_00534010 Burn). Sentinel allocated in the EquippedObj ctor.
    FFuseBurnNode*& HostFuseSentinelRef(FClientGameobj* Host)
    {
        return reinterpret_cast<FFuseBurnNode*&>(static_cast<FClientEquippedObj*>(Host)->Fuses.Head);
    }

    int& HostFuseCountRef(FClientGameobj* Host)
    {
        return static_cast<FClientEquippedObj*>(Host)->Fuses.Count;
    }

    // FUN_00534240 — the host's Ignite slot (+0x1c0; NOT FUN_005340b0 at +0x1a0). Published as
    // FClientEquippedObj::LightFuse (end of file); FClientEquippedObj::LightFuse runs it.
    bool HostIgniteFuseImpl(FClientGameobj* Host, unsigned SourceId, ID_String const& FuseId,
                            unsigned short SubObjId, float AtT, float OverrideLifetime)
    {
        // The first burn-list node whose fuse nickname matches is ignited and the walk stops.
        FFuseBurnNode* Sentinel = HostFuseSentinelRef(Host);
        for (FFuseBurnNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        {
            if (Node->Live.Name().Id == FuseId.Id)
            {
                Node->Live.IgniteAt(SourceId, SubObjId, AtT);
                return true;
            }
        }

        ClientFuse* Proto = ClientFuseDB::Find(FuseId.Id);
        if (Proto == nullptr)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Fuse(%s) not found in FuseDB. Aborting.",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\EquippedObj.cpp", 0x2b8,
                  FuseId.get_string());
            return false;
        }

        // A temporary copy of the prototype (the Fuse copy ctor clones the actions), pushed back
        // as a copy of it (FUN_00535a20), then destroyed.
        {
            ClientFuse Temp(*Proto);
            FFuseBurnNode* NewNode = new FFuseBurnNode{Sentinel, Sentinel->Prev, Temp};
            Sentinel->Prev->Next = NewNode;
            Sentinel->Prev = NewNode;
            HostFuseCountRef(Host) = HostFuseCountRef(Host) + 1;
        }

        // All on the new last node.
        ClientFuse& Live = Sentinel->Prev->Live;
        Live.BindHost(Host);
        Live.RandomizeActions();
        // Unless exactly -1.0f (DAT_005c7500).
        if (OverrideLifetime != -1.0f)
            Live.OverrideLifetime(OverrideLifetime);
        Live.IgniteAt(SourceId, SubObjId, AtT);
        return true;
    }

    CEqObj* ResolveEqObjFromHost(FClientGameobj* Host)
    {
        if (Host == nullptr || Host->Object == nullptr)
            return nullptr;
        CObject* Obj = Host->Object;
        if ((static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
            return nullptr;
        return static_cast<CEqObj*>(Obj);
    }

    // The light equipment's client wrapper: operator new(0x20) in the factory FUN_00533160,
    // constructor FUN_00531b40, vtable PTR_FUN_005de14c {notify FUN_00531c20, scalar deleting
    // dtor FUN_005323a0 (body FUN_005323c0), FUN_00532000}. Its base (vtable PTR_FUN_005de13c,
    // notify FUN_00531b20, dtor FUN_00531ab0) is FClientEquip; retail inlines its constructor
    // (FUN_00531a30) and destructor (FUN_00531af0) here, the port runs them as the base's.
    // Slot 2 (Update) is the base's FUN_00532000.
    struct FCliLightEquipFx : FClientEquip
    {
        // FUN_00531b40.
        FCliLightEquipFx(CEquip* EquipParam, FClientGameobj* HostParam);
        // FUN_005323c0.
        ~FCliLightEquipFx() override;

        // FUN_00531c20.
        void notify(Event Code, void* Param) override;
        // FUN_00531cf0: a new CliEquipLight on the hardpoint, taken into Companion and the
        // host's effect list.
        unsigned char Activate();

        // +0x10 the 0x10-byte effect companion {arch, ISoundSource*, effect, FGameLight*}; only
        // the effect slot is ever filled here (the CliEquipLight).
        void* Companion[4];
    };

    static_assert(sizeof(FCliLightEquipFx) == 0x20, "CliLightEquipFx 0x20");
    static_assert(offsetof(FCliLightEquipFx, Companion) == 0x10, "CliLightEquipFx +0x10");

    // FUN_00536100 / FUN_00536170 are ExplosionArchAddRef / ExplosionArchRelease
    // (client_gameobj.h).

    // ---- CliEquipLight (Client\space\FlashLight.cpp) -------------------------------------

    constexpr float kRandUnit = 3.0517578125e-05f;          // DAT_005caeec, 1/32768
    constexpr double kBlinkJitterScale = 0.20000004768371582; // DAT_005da4c0
    constexpr double kBlinkJitterBase = 0.8999999761581421;   // DAT_005da4b8
    constexpr float kFullConeDegrees = 180.0f;               // DAT_005da41c
    constexpr float kFastInvSqrtHalf = 0.5f;                 // DAT_005c75e0
    constexpr float kFastInvSqrtThreeHalves = 1.5f;          // DAT_005ca220
    constexpr float kBlinkPeak = 2.0f;                       // DAT_005d55b4
    constexpr char const kFlashLightCpp[] =                  // .rdata 0x005da434
        "E:\\FL\\Scratch\\Source\\Client\\space\\FlashLight.cpp";

    // FUN_004fda00's colour byte: floor(c * 255.0 + 0.5) in double, truncated, low byte kept.
    unsigned char LightColorByte(float Component)
    {
        double const Scaled = static_cast<double>(Component) * 255.0 + 0.5;
        return static_cast<unsigned char>(static_cast<long>(floor(Scaled)));
    }

    // FUN_004fd920 -- the destructor body; the Effect base destructor FUN_004f8dd0 follows.
    FCliEquipLight::~FCliEquipLight()
    {
        FCliEquipLight* const Self = this;
        if (Self->EngineLight != nullptr)
        {
            Self->EngineLight->Disable();
            Self->EngineLight->Release();
            Self->EngineLight = nullptr;
        }
    }

    // FUN_004fd9d0 -- nothing else of the tail is written here; FUN_004fda00 fills it.
    FCliEquipLight::FCliEquipLight(unsigned char bPlayerOwnedParam) : FEffectBase(EFT_FLASHLIGHT)
    {
        FCliEquipLight* const Self = this;
        Self->bPlayerOwned = bPlayerOwnedParam;
        Self->EngineLight = nullptr;
    }

    FCliEquipLight* CliEquipLightConstruct(FCliEquipLight* Self, unsigned char bPlayerOwned)
    {
        return ::new (Self) FCliEquipLight(bPlayerOwned);
    }

    // FUN_004fda00 -- blink parameters from the CliEQLight, the "shine" frame, and for a
    // player-owned light a real GameLight. Always returns 1.
    unsigned char CliEquipLightInit(FCliEquipLight* Self, Archetype::ICliObj* Companion)
    {
        Self->Companion = Companion;
        Self->BlinkRate = 0.0f;
        Self->BlinkDuty = 1.0f;
        FCliEQLightView const* const View = CompanionAsLight(Self->Companion);

        // The period (rand / 32768 * 0.2 + 0.9) * avg_delay is tested unrounded and kept as a
        // float for the rest.
        double const PeriodExt =
            (static_cast<double>(rand()) * kRandUnit * kBlinkJitterScale + kBlinkJitterBase) *
            View->AvgDelay;
        float const Period = static_cast<float>(PeriodExt);
        // Only a period above zero (not NaN) blinks.
        if (PeriodExt > 0.0)
        {
            // The quotient stays unrounded for the duty below.
            double const RateExt = 1.0 / static_cast<double>(Period);
            Self->BlinkRate = static_cast<float>(RateExt);
            // blink_duration at or below the period (or NaN) is used as is, above it the period.
            float const Lit = (View->BlinkDuration > Period) ? Period : View->BlinkDuration;
            Self->BlinkDuty = static_cast<float>(RateExt * static_cast<double>(Lit));
        }

        Self->BlinkBrightness = 1.0f;
        Self->BlinkPhase = static_cast<float>(static_cast<double>(rand()) * kRandUnit);

        // The inlined ShapeDB lookup FDUMPs a miss; the record is not tested before use.
        FTexShape* const Shine = G_EffectShapeDB.Find("shine");
        DALib::TextureLib->GetTextureFrame(Shine->Texture, 0, Self->TexFrame);

        // Only an intensity above zero.
        if (View->Intensity > 0.0f)
        {
            // Only an exact 0.0 rate is quiet.
            if (Self->BlinkRate != 0.0f)
            {
                FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kFlashLightCpp, 0x8d,
                      "CliEquipLight: real light sources so no longer blink");
            }

            if (Self->bPlayerOwned != 0)
            {
                // FUN_00537a90(0x130) is a plain malloc, not the pool; +0x98 stored either way.
                void* const Mem = GameLightHeapAlloc(sizeof(FCliEquipGameLight));
                FCliEquipGameLight* Light = nullptr;
                if (Mem != nullptr)
                {
                    Light = ::new (Mem) FCliEquipGameLight();
                    Light->Owner = Self;
                }
                Self->EngineLight = Light;

                unsigned char Rgb[3];
                Rgb[0] = LightColorByte(View->Color[0]);
                Rgb[1] = LightColorByte(View->Color[1]);
                Rgb[2] = LightColorByte(View->Color[2]);
                ((FEngineLight*)Light)->SetColorBytesAlt(Rgb);
                ((FEngineLight*)Light)->SetRadius(View->Intensity);

                // A cone below 180 is a spot (type 2), anything else (NaN too) a point.
                if (View->LightsourceCone < kFullConeDegrees)
                {
                    ((FEngineLight*)Light)->SetType(kLightSpot, true);
                    ((FEngineLight*)Light)->SetConeAngle(View->LightsourceCone);
                }
                else
                {
                    ((FEngineLight*)Light)->SetType(kLightPoint, true);
                }

                // A refused enable releases the light.
                if (!Light->Enable())
                {
                    Self->EngineLight->Release();
                    Self->EngineLight = nullptr;
                }
            }
        }
        return 1;
    }

    // FUN_004fdda0 -- the flare: the "shine" node of the screen-quad batch, the fade from the
    // flare cone, then FUN_004038f0. Returns 1.
    unsigned char CliEquipLightDrawFlare(FCliEquipLight* Self, FSpaceCamera* Cam)
    {
        Transform World;
        Self->GetWorldTransform(&World);
        ScreenQuadBeginBatchFrame(Self->TexFrame, 1, true);

        FCliEQLightView const* const View = CompanionAsLight(Self->Companion);
        float Fade = 1.0f;
        // Only FlareConeCos[0] strictly above FlareConeCos[1] fades.
        if (View->FlareConeCos[0] > View->FlareConeCos[1])
        {
            const Vector* const CamPos = AsCamera(Cam)->GetPosition();
            float Dir[3];
            Dir[0] = World.Translation.x - CamPos->x;
            Dir[1] = World.Translation.y - CamPos->y;
            Dir[2] = World.Translation.z - CamPos->z;

            // The fast inverse square root on the float length^2, one Newton step in double.
            float const LengthSq = static_cast<float>(
                static_cast<double>(Dir[2]) * Dir[2] + static_cast<double>(Dir[1]) * Dir[1] +
                static_cast<double>(Dir[0]) * Dir[0]);
            int Bits = 0;
            std::memcpy(&Bits, &LengthSq, sizeof(Bits));
            Bits = 0x5f3759df - (Bits >> 1);
            float Guess = 0.0f;
            std::memcpy(&Guess, &Bits, sizeof(Guess));
            double const InvLength =
                (kFastInvSqrtThreeHalves - static_cast<double>(Guess) * Guess * LengthSq *
                                               kFastInvSqrtHalf) *
                Guess;
            Dir[0] = static_cast<float>(Dir[0] * InvLength);
            Dir[1] = static_cast<float>(Dir[1] * InvLength);
            Dir[2] = static_cast<float>(Dir[2] * InvLength);

            // The dot with the forward column, (z + y) + x, rounded to float.
            float const Dot = static_cast<float>(
                static_cast<double>(Dir[2]) * World.Rotation.E[2][2] +
                static_cast<double>(Dir[1]) * World.Rotation.E[1][2] +
                static_cast<double>(Dir[0]) * World.Rotation.E[0][2]);

            // Below the outer cosine no flare, between the two a linear fade, otherwise (NaN
            // too) full.
            if (Dot < View->FlareConeCos[1])
            {
                Fade = 0.0f;
            }
            else if (Dot < View->FlareConeCos[0])
            {
                Fade = static_cast<float>(
                    (static_cast<double>(Dot) - View->FlareConeCos[1]) /
                    (static_cast<double>(View->FlareConeCos[0]) - View->FlareConeCos[1]));
            }
        }

        float const Brightness = Self->BlinkBrightness;
        float Glow[3];
        Glow[0] = Brightness * View->GlowColor[0];
        Glow[1] = Brightness * View->GlowColor[1];
        Glow[2] = Brightness * View->GlowColor[2];

        // The bulb colour, min_color + brightness * (color - min_color); x and y go through a
        // float temporary, z stays unrounded.
        float Span[3];
        Span[0] = View->Color[0] - View->MinColor[0];
        Span[1] = View->Color[1] - View->MinColor[1];
        Span[2] = View->Color[2] - View->MinColor[2];
        float const ScaledX = Brightness * Span[0];
        float const ScaledY = Brightness * Span[1];
        float Bulb[3];
        Bulb[0] = ScaledX + View->MinColor[0];
        Bulb[1] = ScaledY + View->MinColor[1];
        Bulb[2] = static_cast<float>(static_cast<double>(Brightness) * Span[2] + View->MinColor[2]);

        Glow[0] = Glow[0] * Fade;
        Glow[1] = Glow[1] * Fade;
        Glow[2] = Glow[2] * Fade;

        float const Position[3] = {World.Translation.x, World.Translation.y, World.Translation.z};
        ScreenQuadEnqueueFlare(AsCamera(Cam), Position, Bulb, Glow, View->BulbSize,
                               View->GlowSize);
        return 1;
    }

    // Slots 0 and 1 are the Effect base's LAB_004f7ab0 / FUN_004f7bd0.

    // Slot 2 -- LAB_004fe040: the world position out, and the larger of bulb_size / glow_size
    // back (bulb_size unless it is strictly below glow_size).
    float FCliEquipLight::GetWorldPos(Vector* Out)
    {
        FCliEquipLight* const Self = this;
        Transform World;
        Self->GetWorldTransform(&World);
        Out->x = World.Translation.x;
        Out->y = World.Translation.y;
        Out->z = World.Translation.z;
        FCliEQLightView const* const View = CompanionAsLight(Self->Companion);
        if (View->BulbSize < View->GlowSize)
            return View->GlowSize;
        return View->BulbSize;
    }

    // Slot 3 -- 0x0053e4a0, empty.


    // Slot 4 -- LAB_004fd990: Flags +8 bit 0 (set by the owner's list sweep FUN_004fd040).
    unsigned char FCliEquipLight::IsAlive()
    {
        FCliEquipLight* const Self = this;
        return static_cast<unsigned char>(Self->Flags08 & kEffectFlagLive);
    }

    // Slot 5 -- LAB_004fdcf0 (both arguments unread): bit 0 cleared, its old value back.
    unsigned char FCliEquipLight::UpdateVisibility(float /*Dt*/, FSpaceCamera* /*Cam*/)
    {
        FCliEquipLight* const Self = this;
        unsigned char const Flags = Self->Flags08;
        Self->Flags08 = static_cast<unsigned char>(Flags & ~kEffectFlagLive);
        return static_cast<unsigned char>(Flags & kEffectFlagLive);
    }

    // Slot 6 -- FUN_004fdd00: the blink. Returns 1.
    unsigned char FCliEquipLight::TickDraw(float Dt, FSpaceCamera* Cam)
    {
        FCliEquipLight* const Self = this;
        // A rate of exactly 0.0 does nothing.
        if (Self->BlinkRate == 0.0f)
            return 1;

        // The unrounded new phase at or above 1.0 (NaN keeps it) wraps to x - floor(x).
        double const Phase =
            static_cast<double>(Dt) * Self->BlinkRate + static_cast<double>(Self->BlinkPhase);
        Self->BlinkPhase = static_cast<float>(Phase);
        if (Phase >= 1.0)
            Self->BlinkPhase = static_cast<float>(Phase - floor(Phase));

        // Past the duty the light is dark.
        if (Self->BlinkPhase > Self->BlinkDuty)
        {
            Self->BlinkBrightness = 0.0f;
            return 1;
        }

        // The triangle 2/duty * phase, folded above 1.0 as 2 - x.
        double Wave = static_cast<double>(kBlinkPeak) / Self->BlinkDuty * Self->BlinkPhase;
        if (Wave > 1.0)
            Wave = kBlinkPeak - Wave;
        Self->BlinkBrightness = static_cast<float>(Wave);
        return 1;
    }

    // Slot 7 -- LAB_004fdfe0: the FLASHLIGHTS option is read twice.
    unsigned char FCliEquipLight::BuildGeometry(FSpaceCamera* Cam)
    {
        FCliEquipLight* const Self = this;
        if (GetGameOption(kGameOptFlashlights) == 0)
            return 0;
        // Only a negative brightness (ordered) stops it.
        if (Self->BlinkBrightness < 0.0f)
            return 0;
        if (GetGameOption(kGameOptFlashlights) == 1)
            return CliEquipLightDrawFlare(Self, Cam);
        return 1;
    }

    // Slot 8 -- FUN_004fd9a0, the scalar deleting destructor.


    // PTR_FUN_005da424 slot 3 -- LAB_004fdcb0: the GameLight tick, then the owner's world
    // transform.
    void FCliEquipGameLight::Tick(float Dt)
    {
        FCliEquipGameLight* const Self = this;
        Self->FGameLight::Tick(Dt);
        Transform World;
        Self->Owner->GetWorldTransform(&World);
        EngineLightSetTransform((FEngineLight*)Self, &World.Rotation.E[0][0], &World.Translation);
    }

    FCliLightEquipFx::FCliLightEquipFx(CEquip* EquipParam, FClientGameobj* HostParam)
    {
        // FUN_00531b40, with the base constructor inlined. No test of Equip.
        Equip = EquipParam;
        Host = HostParam;
        SeparationExplosion = nullptr;
        Equip->ConnectClientEquip(this);
        CAttachedEquip* const Attached = CAttachedEquip::cast(Equip);
        if (Attached != nullptr)
        {
            if (!Attached->AttachedEquipArch()->SeparationExplosion.IsEmpty())
            {
                SeparationExplosion =
                    Archetype::GetExplosion(Attached->AttachedEquipArch()->SeparationExplosion);
                if (SeparationExplosion != nullptr)
                    ExplosionArchAddRef(SeparationExplosion);
            }
        }

        Companion[0] = nullptr;
        Companion[1] = nullptr;
        Companion[2] = nullptr;
        Companion[3] = nullptr;

        // An equipment already on lights at once.
        if (Equip->IsActive())
            Activate();
    }

    FCliLightEquipFx::~FCliLightEquipFx()
    {
        // FUN_005323c0. The node the host's effect list holds keeps its own references.
        CliEffectCompanionClear(Companion);
        // The inlined base destructor is ~FClientEquip.
    }

    void FCliLightEquipFx::notify(Event Code, void* /*Param*/)
    {
        // FUN_00531c20.
        if (Code == EquipDeactivating)
        {
            // The light leaves the host's effect list while anything of it still runs -- a live
            // effect, an active sound or a light without anim keys.
            FEffectBase const* const Effect = static_cast<FEffectBase const*>(Companion[2]);
            ISoundSource* const Sound = static_cast<ISoundSource*>(Companion[1]);
            FGameLight const* const Light = static_cast<FGameLight const*>(Companion[3]);
            if ((Effect != nullptr && (Effect->Flags08 & kEffectFlagDeactivated) == 0) ||
                (Sound != nullptr && Sound->IsActive() != 0) ||
                (Light != nullptr && Light->Anim.Keys == nullptr))
            {
                CliEffectListRemove(&Host->EffectList, Companion);
            }
            return;
        }

        if (Code == EquipActivating)
        {
            // Nothing while any of the same three still runs, otherwise a fresh light.
            FEffectBase const* const Effect = static_cast<FEffectBase const*>(Companion[2]);
            if (Effect != nullptr && (Effect->Flags08 & kEffectFlagDeactivated) == 0)
                return;
            ISoundSource* const Sound = static_cast<ISoundSource*>(Companion[1]);
            if (Sound != nullptr && Sound->IsActive() != 0)
                return;
            FGameLight const* const Light = static_cast<FGameLight const*>(Companion[3]);
            if (Light != nullptr && Light->Anim.Keys == nullptr)
                return;
            Activate();
            return;
        }

        if (Code == EquipDestroying)
        {
            CliEffectListRemove(&Host->EffectList, Companion);
            Equip->NotifyDisconnecting(this);
            Equip = nullptr;
        }
    }

    unsigned char FCliLightEquipFx::Activate()
    {
        // FUN_00531cf0. "The host is the player" is a raw pointer compare.
        void* const Mem = EffectAlloc(0x9c);
        FCliEquipLight* Light = nullptr;
        if (Mem != nullptr)
        {
            unsigned char const bPlayer = static_cast<unsigned char>(
                static_cast<void*>(static_cast<IObjRW*>(Host)) == GetPlayerObjRW() ? 1 : 0);
            Light = CliEquipLightConstruct(static_cast<FCliEquipLight*>(Mem), bPlayer);
        }

        // Called on the CEquip without a cast check.
        CExternalEquip const* const External = static_cast<CExternalEquip const*>(Equip);
        HardpointInfo Hardpoint;
        if (!External->GetHardPointInfo(Hardpoint))
        {
            Light->Release();
            return 0;
        }

        // The hardpoint as the local frame, on the parent connector's part.
        Transform Local;
        std::memcpy(&Local.Rotation, &Hardpoint.Orient, sizeof(Local.Rotation));
        Local.Translation.x = Hardpoint.Position.x;
        Local.Translation.y = Hardpoint.Position.y;
        Local.Translation.z = Hardpoint.Position.z;
        long const Part = External->GetParentConnector(false);
        Light->Attach(Host, static_cast<int>(Part), &Local);

        // The arch's CliEQLight companion, null without an equipment.
        Archetype::ICliObj* const LightArch = (Equip != nullptr) ? Equip->Arch->Companion : nullptr;
        unsigned char const bOk = CliEquipLightInit(Light, LightArch);
        if (bOk != 0)
        {
            // A local companion {0, 0, light, 0} (its copy takes a reference), assigned into
            // +0x10 (the old contents released), then the local destroyed.
            void* const Local4[4] = {nullptr, nullptr, Light, nullptr};
            if (Light != nullptr)
                Light->AddRef();
            CliEffectCompanionClear(Companion);
            Companion[0] = Local4[0];
            Companion[2] = Local4[2];
            if (Light != nullptr)
                Light->AddRef();
            Companion[1] = Local4[1];
            Companion[3] = Local4[3];
            if (Light != nullptr)
                Light->Release();

            CliEffectListInsert(&Host->EffectList, Companion);
        }

        // The allocation's own reference goes.
        Light->Release();
        return bOk;
    }

    // The factory's light arm (FUN_00533160): operator new(0x20), FUN_00531b40.
    FCliLightEquipFx* CliLightEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        return new FCliLightEquipFx(Equip, Host);
    }

    void EquipFxListInsert(FClientGameobj* Host, FClientEquip* Wrapper)
    {
        if (Host == nullptr || Wrapper == nullptr)
            return;
        FCliEquipFxListNode* Sentinel = EquipFxSentinelRef(Host);
        if (Sentinel == nullptr)
            return;
        FCliEquipFxListNode* Node =
            static_cast<FCliEquipFxListNode*>(::operator new(sizeof(FCliEquipFxListNode)));
        Node->Next = Sentinel;
        Node->Prev = Sentinel->Prev;
        Node->Wrapper = Wrapper;
        Sentinel->Prev = Node;
        if (Node->Prev != nullptr)
            Node->Prev->Next = Node;
        EquipFxCountRef(Host) = EquipFxCountRef(Host) + 1;
    }

    void EquippedObjResolveExplosion(FClientGameobj* Host)
    {
        // Part of FUN_00539740 on EquippedObj: the explosion arch stored at +0x40.
        if (Host == nullptr)
            return;
        CObject* Obj = Host->Object;
        if (Obj == nullptr)
            return;
        Archetype::Root* Arch = Obj->get_archetype();
        if (Arch == nullptr || Arch->ExplosionArch.IsEmpty())
            return;
        Archetype::Explosion* Expl = Archetype::GetExplosion(Arch->ExplosionArch);
        *reinterpret_cast<Archetype::Explosion**>(reinterpret_cast<unsigned char*>(Host) + 0x40) =
            Expl;
        ExplosionArchAddRef(Expl);
    }

    // --- Equip-FX type wrappers (FUN_00531a30 family) ---

    // The ClientEquip base is published as FClientEquip (client_equip_fx.h); its members are
    // defined at the end of this file.
    using FCliEquipFxBase = FClientEquip;
    static_assert(sizeof(FCliEquipFxBase) == 0x10, "CliEquipFxBase 0x10");

    // The code-0xa "the player lost this" test the equipment notifies inline (shield generator,
    // engine, thruster, the launcher family): the host is the player's (a pointer compare), not
    // reaped, and its object narrowed by Mask (null otherwise -- get_hit_pts is called on it
    // regardless) has hit points above 0 (NaN fails).
    // Its first half alone (the repair droid): the player's host, not reaped.
    bool CliEquipHostIsPlayer(FClientGameobj* Host)
    {
        if (static_cast<void*>(static_cast<IObjRW*>(Host)) != GetPlayerObjRW())
            return false;
        return !Host->IsDead();
    }

    bool CliEquipHostIsLivePlayer(FClientGameobj* Host, unsigned Mask)
    {
        if (!CliEquipHostIsPlayer(Host))
            return false;
        CSimple* Simple = nullptr;
        if (Host != nullptr && Host->Object != nullptr &&
            (static_cast<unsigned>(Host->Object->ObjectClass) & Mask) == Mask)
        {
            Simple = static_cast<CSimple*>(Host->Object);
        }
        return Simple->get_hit_pts() > 0.0f;
    }

    // DAT_0067545c, filled by the static initializer LAB_0052ebf0.
    unsigned const G_ShieldDestroyedId = CreateID("shield_destroyed");

    // The shield generator's client wrapper: operator new(0x10), constructor FUN_0052eb10,
    // vtable PTR_FUN_005ddb40 {notify FUN_0052eb60, scalar deleting dtor FUN_0052eb30 (the
    // base's FUN_00531af0 through the thunk 0052eb50), Update FUN_00532000}.
    struct FCliShieldGenEquipFx : FCliEquipFxBase
    {
        // FUN_0052eb60.
        void notify(Event Code, void* Param) override;
    };
    static_assert(sizeof(FCliShieldGenEquipFx) == 0x10, "ShieldGenFx 0x10");

    void FCliShieldGenEquipFx::notify(Event Code, void* Param)
    {
        // FUN_0052eb60: code 0xa on the player's live host (class 0x103) queues
        // "shield_destroyed" (FUN_00489e80 with DAT_0067545c, 0); every code then goes to
        // FUN_00531b20 with both arguments.
        if (Code == EquipDestroying && CliEquipHostIsLivePlayer(Host, CObject::ClassEqObj))
            NnVoiceEnqueue(G_ShieldDestroyedId, 0);
        FClientEquip::notify(Code, Param);
    }

    // The cloaking device's client wrapper: operator new(0x10), constructor FUN_00531e80 (the
    // base FUN_00531a30 inlined), vtable PTR_LAB_005de15c {notify LAB_00532010, scalar deleting
    // dtor FUN_00531f10 (body FUN_00531f30), Update FUN_00532000}.
    struct FCliCloakEquipFx : FCliEquipFxBase
    {
        // FUN_00531f30.
        ~FCliCloakEquipFx() override;
        // LAB_00532010.
        void notify(Event Code, void* Param) override;
    };
    static_assert(sizeof(FCliCloakEquipFx) == 0x10, "CloakFx 0x10");

    FCliCloakEquipFx::~FCliCloakEquipFx()
    {
        // FUN_00531f30 (no test of the host), with FUN_00532170 inlined. The base destructor
        // follows.
        long const Index = Host->get_index();
        RemoveAppearanceInstanceValue(static_cast<int>(Index));
    }

    void FCliCloakEquipFx::notify(Event Code, void* /*Param*/)
    {
        // LAB_00532010. FUN_00531b20 is never called.
        CEquip* const Device = Equip;
        if (Code == EquipActivating)
        {
            // cloakout_fx on the host's root part, identity frame, no companion.
            Transform Identity;
            std::memset(&Identity, 0, sizeof(Identity));
            Identity.Rotation.E[0][0] = 1.0f;
            Identity.Rotation.E[1][1] = 1.0f;
            Identity.Rotation.E[2][2] = 1.0f;
            long const Root = static_cast<CAttachedEquip*>(Device)->GetRootIndex();
            FClientGameobj* const Owner = Host;
            ID_String const* const FxId = reinterpret_cast<ID_String const*>(
                &static_cast<CECloakingDevice*>(Device)->CloakArch()->CloakOutFx);
            if (!FxId->IsEmpty())
            {
                CliCreateEffectOneShot(FindEffectArch(&FxId->Id), Owner, static_cast<int>(Root),
                                       &Identity, nullptr);
            }
            return;
        }
        if (Code == EquipDeactivating)
        {
            // cloakin_fx in world space at the host's frame (the Transform its orientation
            // points into) with its velocity, no companion.
            RemoveAppearanceInstanceValue(static_cast<int>(Host->get_index()));
            Vector const Velocity = Host->get_velocity();
            Matrix const& Orientation = Host->get_orientation();
            ID_String const* const FxId = reinterpret_cast<ID_String const*>(
                &static_cast<CECloakingDevice*>(Device)->CloakArch()->CloakInFx);
            if (!FxId->IsEmpty())
            {
                CliCreateEffectWorldOneShot(FindEffectArch(&FxId->Id),
                                            reinterpret_cast<Transform const*>(&Orientation),
                                            reinterpret_cast<float const*>(&Velocity), nullptr);
            }
            return;
        }
        if (Code == EquipDestroying)
        {
            Device->NotifyDisconnecting(this);
            Equip = nullptr;
        }
    }

    // DAT_00675468 / DAT_0067546c / DAT_00675464 / DAT_00675460, filled by the static
    // initializers LAB_0052ea90 / LAB_0052eab0 / LAB_0052ead0 / LAB_0052eaf0 (LAB_0052eaf5 is
    // only that initializer's tail; its other apparent XREFs are immediates that happen to
    // match).
    unsigned const G_ShieldDeactivatedId = CreateID("shield_deactivated");
    unsigned const G_ShieldActivatedId = CreateID("shield_activated");
    unsigned const G_ShieldFailedId = CreateID("nnv_shield_failed");
    unsigned const G_ShieldRestoredId = CreateID("shield_restored");

    // The shield's client wrapper (RTTI .?AUCliShield@@, seven slots): operator new(0x18) in the
    // factory, constructor FUN_0052e200, vtable PTR_LAB_005ddad4 {notify LAB_0052e3e0, scalar deleting
    // dtor FUN_0052e2c0 (body FUN_0052e2e0), Update FUN_00532000, then four slots of its own:
    // +0xc LAB_0052e350, +0x10 FUN_0052e3c0, +0x14 LAB_0052e760, +0x18 FUN_0052e8c0}. The
    // equipped object's hit slots (FUN_005340b0 / FUN_00534190) reach +0x14 / +0x18 on the
    // wrapper of a shield sub-object (CEquip type 0x100).
    struct FCliShieldEquipFx : FCliEquipFxBase
    {
        // FUN_0052e2e0.
        ~FCliShieldEquipFx() override;
        // LAB_0052e3e0.
        void notify(Event Code, void* Param) override;
        // +0x0c LAB_0052e350: the collapse / rebuilt sound archetypes looked up and acquired
        // again (neither is released or cleared first).
        virtual void ReloadSounds();
        // +0x10 FUN_0052e3c0: both released, neither cleared.
        virtual void ReleaseSounds();
        // +0x14 LAB_0052e760: a collision on the shield bubble. HitInfo is the hit point (+0)
        // and the surface normal (+0xc); the third argument is not read.
        virtual void OnCollisionHit(void const* HitInfo, FClientGameobj* Other,
                                    void const* Unused);
        // +0x18 FUN_0052e8c0: a munition hitting the shield bubble.
        virtual void OnMunitionHit(void const* HitInfo, FClientGameobj* Munition);

        // The body both the constructor and ReloadSounds run.
        void LoadSounds();

        void* CollapseSound; // +0x10  CliEQShieldGenerator +0x58 (FUN_0042ae40, Acquire'd)
        void* RebuiltSound;  // +0x14  CliEQShieldGenerator +0x5c
    };
    static_assert(sizeof(FCliShieldEquipFx) == 0x18, "ShieldFx 0x18");

    // The dominant generator's companion as every shield slot reads it: the equipment (+4),
    // its +0x70 (CEShield::DominantGenArch, read with no cast) and that archetype's +4
    // companion (a CliEQShieldGenerator), null when any link is.
    Archetype::ICliObj* CliShieldGenCompanion(CEquip* Equip)
    {
        if (Equip == nullptr)
            return nullptr;
        Archetype::ShieldGenerator* const Gen = static_cast<CEShield*>(Equip)->DominantGenArch;
        if (Gen == nullptr)
            return nullptr;
        return Gen->Companion;
    }

    void FCliShieldEquipFx::LoadSounds()
    {
        // The generator companion's +0x58 / +0x5c sounds, acquired with no test of the lookup.
        Archetype::ICliObj* const Comp = CliShieldGenCompanion(Equip);
        if (Comp == nullptr)
            return;
        unsigned char const* const CompBytes = reinterpret_cast<unsigned char const*>(Comp);
        ID_String const* const Collapse = reinterpret_cast<ID_String const*>(CompBytes + 0x58);
        if (!Collapse->IsEmpty())
        {
            CollapseSound = LookupClientSoundArch(Collapse);
            static_cast<Archetype::ISound*>(CollapseSound)->Acquire();
        }
        ID_String const* const Rebuilt = reinterpret_cast<ID_String const*>(CompBytes + 0x5c);
        if (!Rebuilt->IsEmpty())
        {
            RebuiltSound = LookupClientSoundArch(Rebuilt);
            static_cast<Archetype::ISound*>(RebuiltSound)->Acquire();
        }
    }

    void FCliShieldEquipFx::ReloadSounds()
    {
        // LAB_0052e350: the constructor's lookup, as is.
        LoadSounds();
    }

    void FCliShieldEquipFx::ReleaseSounds()
    {
        // FUN_0052e3c0.
        if (CollapseSound != nullptr)
            static_cast<Archetype::ISound*>(CollapseSound)->Release();
        if (RebuiltSound != nullptr)
            static_cast<Archetype::ISound*>(RebuiltSound)->Release();
    }

    FCliShieldEquipFx::~FCliShieldEquipFx()
    {
        // FUN_0052e2e0: the body of FUN_0052e3c0 inlined (neither pointer cleared), then the
        // base destructor FUN_00531af0.
        if (CollapseSound != nullptr)
            static_cast<Archetype::ISound*>(CollapseSound)->Release();
        if (RebuiltSound != nullptr)
            static_cast<Archetype::ISound*>(RebuiltSound)->Release();
    }

    // The play key FUN_00428680 gets from both shield sounds: +0 zero, +4 the flag byte with
    // its low three bits cleared and bit 1 set (positional; the upper bits are whatever the
    // stack held in retail, zero here), +8 the position, +0x14 zero.
    void CliShieldPlayAt(void* Sound, float const* Pos)
    {
        unsigned Key[6] = {0u, 2u, 0u, 0u, 0u, 0u};
        std::memcpy(&Key[2], &Pos[0], sizeof(float));
        std::memcpy(&Key[3], &Pos[1], sizeof(float));
        std::memcpy(&Key[4], &Pos[2], sizeof(float));
        PlayClientSoundArch(Sound, Key, nullptr, 0);
    }

    // The shield's world frame: its root part's engine transform (no test of the cast).
    Transform const* CliShieldWorldFrame(CEquip* Equip)
    {
        CEShield* const Shield = CEShield::cast(Equip);
        long const Root = Shield->GetRootIndex();
        return DALib::Engine->GetInstanceTransform(
            Root);
    }

    void FCliShieldEquipFx::notify(Event Code, void* Param)
    {
        // LAB_0052e3e0: every path ends in the base notify with a null Param, whatever came in.
        switch (Code)
        {
        case EquipActivating:
            // The player's host (a pointer compare only).
            if (static_cast<void*>(static_cast<IObjRW*>(Host)) == GetPlayerObjRW())
                NnVoiceEnqueue(G_ShieldActivatedId, 0);
            break;
        case EquipDeactivating:
        {
            // The player's host whose ship (null when it is not one; is_launching is called with
            // no test) is not launching.
            FClientGameobj* const Owner = Host;
            if (static_cast<void*>(static_cast<IObjRW*>(Owner)) != GetPlayerObjRW())
                break;
            CShip* Ship = nullptr;
            if (Owner != nullptr && Owner->Object != nullptr &&
                (static_cast<unsigned>(Owner->Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            {
                Ship = static_cast<CShip*>(Owner->Object);
            }
            if (!Ship->is_launching())
                NnVoiceEnqueue(G_ShieldDeactivatedId, 0);
            break;
        }
        case ShieldGeneratorSetChanged:
            ReleaseSounds();
            ReloadSounds();
            break;
        case ShieldOfflineStateChanged:
        {
            // *(bool*)Param and a rebuilt sound -> played at the shield's world position.
            unsigned char const* const Flag = static_cast<unsigned char const*>(Param);
            if (*Flag != 0 && RebuiltSound != nullptr)
            {
                const float* const Frame = (const float*)CliShieldWorldFrame(Equip);
                CliShieldPlayAt(RebuiltSound, Frame + 9);
            }
            // Then the host's own INotify (EquippedObj +0xac) with the same code and Param.
            static_cast<INotify*>(static_cast<FClientEquippedObj*>(Host))->notify(Code, Param);
            break;
        }
        case ShieldWentOffline:
        {
            // A host already exploding -> nothing.
            if (Host->IsExploding())
                break;
            if (CliEquipHostIsLivePlayer(Host, CObject::ClassShip))
                NnVoiceEnqueue(G_ShieldFailedId, 0);
            const float* const Frame = (const float*)CliShieldWorldFrame(Equip);
            // With a generator companion: the owner's velocity (read before the id test), then
            // collapse_particle in world space at the frame, no companion.
            Archetype::ICliObj* const Comp = CliShieldGenCompanion(Equip);
            if (Comp != nullptr)
            {
                ID_String const* const Particle = reinterpret_cast<ID_String const*>(
                    reinterpret_cast<unsigned char const*>(Comp) + 0x54);
                Vector const Velocity = Equip->Owner->get_velocity();
                if (!Particle->IsEmpty())
                {
                    CliCreateEffectWorldOneShot(FindEffectArch(&Particle->Id),
                                                reinterpret_cast<Transform const*>(Frame),
                                                reinterpret_cast<float const*>(&Velocity),
                                                nullptr);
                }
            }
            if (CollapseSound != nullptr)
                CliShieldPlayAt(CollapseSound, Frame + 9);
            break;
        }
        case ShieldCanReactivate:
            if (CliEquipHostIsLivePlayer(Host, CObject::ClassShip))
                NnVoiceEnqueue(G_ShieldRestoredId, 0);
            break;
        default:
            // 0xd, 0xe, 0xf and anything outside 0xb..0x13.
            break;
        }
        FClientEquip::notify(Code, nullptr);
    }

    void FCliShieldEquipFx::OnCollisionHit(void const* HitInfo, FClientGameobj* Other,
                                           void const* /*Unused*/)
    {
        // LAB_0052e760.
        Archetype::ICliObj* const Comp = CliShieldGenCompanion(Equip);
        if (Comp == nullptr)
            return;
        // The relative speed (the host's velocity read first).
        Vector const HostVelocity = Host->get_velocity();
        Vector const OtherVelocity = Other->get_velocity();
        float const Dz = OtherVelocity.z - HostVelocity.z;
        float const Dy = OtherVelocity.y - HostVelocity.y;
        float const Dx = OtherVelocity.x - HostVelocity.x;
        float const Speed = sqrtf(Dx * Dx + Dy * Dy + Dz * Dz);
        float const Damage = Other->get_mass() * COLLISION_DAMAGE_FACTOR * Speed;
        // The companion is re-read (null tests again) to pick the hit effect (FUN_00409f10).
        FEffectArch* const HitArch = Archetype::CliShieldGeneratorPickHitEffect(Comp, Damage);
        void* Companion[4] = {nullptr, nullptr, nullptr, nullptr};
        Vector const* const Hit = static_cast<Vector const*>(HitInfo);
        Host->SpawnSurfaceHitEffect(Other->get_id(), HitArch, Hit, Hit + 1, Companion);
        // Dead in retail: FUN_00539f90 never writes its fifth argument, so the slot stays null
        // and this does not run.
        FEffectBase* const Effect = static_cast<FEffectBase*>(Companion[2]);
        if (Effect != nullptr)
        {
            Effect->SetIntensity(Equip->GetRelativeHealth());
        }
        CliEffectCompanionClear(Companion);
    }

    void FCliShieldEquipFx::OnMunitionHit(void const* HitInfo, FClientGameobj* Munition)
    {
        // FUN_0052e8c0.
        Archetype::ICliObj* const Comp = CliShieldGenCompanion(Equip);
        if (Comp == nullptr)
            return;
        // The munition's class & 7 object (no null test before the archetype read).
        CObject* const MunObject = Munition->Object;
        CObject* Shot7 = nullptr;
        if (MunObject != nullptr && (static_cast<unsigned>(MunObject->ObjectClass) & CObject::ClassProjectile) == CObject::ClassProjectile)
            Shot7 = MunObject;
        Archetype::Munition const* const MunArch =
            static_cast<Archetype::Munition const*>(Shot7->Archetype);
        float const Damage =
            MunArch->HullDamage * ShieldEquipConsts::HULL_DAMAGE_FACTOR + MunArch->EnergyDamage;
        FEffectArch* const HitArch = Archetype::CliShieldGeneratorPickHitEffect(Comp, Damage);
        // Keyed by the shot's owner (the class & 0x27 object, no test before the read).
        void* Companion[4] = {nullptr, nullptr, nullptr, nullptr};
        CProjectile const* Shot = nullptr;
        if (MunObject != nullptr &&
            (static_cast<unsigned>(MunObject->ObjectClass) & 0x27u) == 0x27u)
        {
            Shot = static_cast<CProjectile const*>(MunObject);
        }
        Vector const* const Hit = static_cast<Vector const*>(HitInfo);
        Host->SpawnSurfaceHitEffect(Shot->OwnerId, HitArch, Hit, Hit + 1, Companion);
        // As in OnCollisionHit (never runs: the slot stays null).
        FEffectBase* const Effect = static_cast<FEffectBase*>(Companion[2]);
        if (Effect != nullptr)
        {
            Effect->SetIntensity(Equip->GetRelativeHealth());
        }
        // A random live effect of the munition archetype's companion (FUN_00408a70).
        FEffectArch* const LiveEffect =
            Archetype::CliMunitionPickLiveEffect(Shot7->Archetype->Companion);
        // The class & 1 object's velocity (no test).
        CObject* Shot1 = nullptr;
        if (MunObject != nullptr && (static_cast<unsigned>(MunObject->ObjectClass) & CObject::ClassObject) == CObject::ClassObject)
            Shot1 = MunObject;
        Vector const Velocity = Shot1->get_velocity();
        Host->SpawnSplashHitEffect(LiveEffect, Hit, Hit + 1, &Velocity, nullptr);
        CliEffectCompanionClear(Companion);
    }

    // The thruster's client wrapper (CliThruster.cpp): operator new(0x20), constructor
    // FUN_0052ec40, vtable PTR_FUN_005ddbd8 {notify FUN_0052ee40, scalar deleting dtor
    // FUN_0052eda0 (body FUN_0052edc0), Update FUN_00532000}.
    struct FCliThrusterEquipFx : FCliEquipFxBase
    {
        // FUN_0052edc0.
        ~FCliThrusterEquipFx() override;
        // FUN_0052ee40.
        void notify(Event Code, void* Param) override;

        // +0x10 the 0x10-byte effect companion {arch, ISoundSource*, effect, FGameLight*} that
        // FUN_00503070 fills with the exhaust particle.
        void* Companion[4];
    };
    static_assert(sizeof(FCliThrusterEquipFx) == 0x20, "ThrusterFx 0x20");
    static_assert(offsetof(FCliThrusterEquipFx, Companion) == 0x10, "Thruster +0x10");

    // Retail Archetype::CliEQAttachedFX (vtable 005c7fe0, 0x5c) is private to client_cliobj.cpp
    // (FCliEQAttachedFX); this is its tail from +0x54 as EquippedObj.cpp reads it.
    struct FCliEQAttachedFXView
    {
        ID_String Particles;        // +0x54 "particles"
        bool bUseThrottle;          // +0x58 "use_throttle"
    };

    // The attached-FX equipment's client wrapper: operator new(0x20) in the factory FUN_00533160,
    // constructor FUN_00532210, vtable PTR_LAB_005de16c {notify LAB_00532460, scalar deleting
    // dtor FUN_005323a0 (body FUN_005323c0 -- the same folded body as the light wrapper's),
    // Update FUN_00532490}.
    struct FCliAttachedFxEquipFx : FCliEquipFxBase
    {
        // FUN_00532210.
        FCliAttachedFxEquipFx(CEquip* EquipParam, FClientGameobj* HostParam);
        // FUN_005323c0.
        ~FCliAttachedFxEquipFx() override;
        // LAB_00532460.
        void notify(Event Code, void* Param) override;
        // FUN_00532490.
        bool Update(float Dt) override;

        // +0x10 the 0x10-byte effect companion {arch, ISoundSource*, effect, FGameLight*} that
        // FUN_00503070 fills.
        void* Companion[4];
    };
    static_assert(sizeof(FCliAttachedFxEquipFx) == 0x20, "AttachedFx 0x20");
    static_assert(offsetof(FCliAttachedFxEquipFx, Companion) == 0x10, "AttachedFx +0x10");

    // The repair droid's client wrapper: operator new(0x20), constructor FUN_00532800 (the base
    // FUN_00531a30 inlined), vtable PTR_LAB_005de18c {notify LAB_00532980, scalar deleting dtor
    // FUN_005328c0 (body FUN_005328e0), Update FUN_00532000}.
    struct FCliRepairDroidEquipFx : FCliEquipFxBase
    {
        // FUN_005328e0.
        ~FCliRepairDroidEquipFx() override;
        // LAB_00532980.
        void notify(Event Code, void* Param) override;

        // +0x10 the 0x10-byte effect companion {arch, ISoundSource*, effect, FGameLight*} that
        // FUN_00503070 fills on activation.
        void* Companion[4];
    };
    static_assert(sizeof(FCliRepairDroidEquipFx) == 0x20, "RepairDroidFx 0x20");
    static_assert(offsetof(FCliRepairDroidEquipFx, Companion) == 0x10, "RepairDroid +0x10");

    // The internal-FX equipment's client wrapper: operator new(0x14), constructor FUN_00532530,
    // vtable PTR_LAB_005de17c {notify LAB_005327d0, scalar deleting dtor FUN_00532720 (body
    // FUN_00532740), Update FUN_00532000 (the base's)}.
    struct FCliInternalFxEquipFx : FCliEquipFxBase
    {
        // FUN_00532740.
        ~FCliInternalFxEquipFx() override;
        // LAB_005327d0.
        void notify(Event Code, void* Param) override;

        ISoundSource* Voice; // +0x10 Source3D looping on the companion's +0x54 sound
    };
    static_assert(sizeof(FCliInternalFxEquipFx) == 0x14, "InternalFx 0x14");

    // The tractor's client wrapper: operator new(0x38), constructor FUN_0052f540, vtable
    // PTR_LAB_005ddcdc {notify LAB_0052f740, scalar deleting dtor FUN_0052f640 (body
    // FUN_0052f660), Update FUN_00532000}.
    struct FCliTractorEquipFx : FCliEquipFxBase
    {
        // FUN_0052f660.
        ~FCliTractorEquipFx() override;
        // LAB_0052f740.
        void notify(Event Code, void* Param) override;
        // FUN_0052fa10: a fresh tractor beam effect into +0x10 and the host's effect list.
        unsigned char StartBeam();
        // FUN_0052f9a0.
        void DropLoot(unsigned LootId, int Units);

        void* BeamCompanion[4];       // +0x10 {0, 0, the CliTractorBeam, 0} (FUN_0052fa10)
        void* OperatingCompanion[4];  // +0x20 the operating_effect one-shot (notify 0xb)
        FEffectArch* OperatingEffect; // +0x30 CliEQTractor +0x58, FUN_0040e960(..., 1)
        void* CompleteSound;          // +0x34 CliEQTractor +0x5c, looked up and acquired
    };
    static_assert(sizeof(FCliTractorEquipFx) == 0x38, "TractorFx 0x38");
    static_assert(offsetof(FCliTractorEquipFx, BeamCompanion) == 0x10, "Tractor +0x10");
    static_assert(offsetof(FCliTractorEquipFx, OperatingCompanion) == 0x20, "Tractor +0x20");
    static_assert(offsetof(FCliTractorEquipFx, OperatingEffect) == 0x30, "Tractor +0x30");

    struct FCliEngineNozzleSlot
    {
        unsigned short SubObjId;    // +0x00
        unsigned short Pad02;
        FParticleEffect* TrailFx;   // +0x04  companion+0x9c
        FParticleEffect* CruiseFx;  // +0x08  companion+0xa0/+0xa4
    };
    static_assert(sizeof(FCliEngineNozzleSlot) == 0xc, "Engine nozzle slot 0xc");

    // The engine wrapper's VC6 std::vector<nozzle slot> at +0x10 {allocator byte, first, last,
    // end}, held as a std::vector: retail's own member functions on it are the VC6 template
    // instantiations -- insert(pos, n, value) FUN_005299b0 (only ever insert(end, 1, empty) =
    // push_back, from FUN_00528980), erase(first, last) FUN_00529920 -- and the element copy
    // FUN_00529ce0 (id, AddRef on both effects) paired with the destroy of the old buffer
    // (release both) leaves every reference count as it was, which is what a bitwise
    // relocation of these plain slots does too. The wrapper is only ever reached by member
    // name in the port, so retail's field offsets after +0x10 are documentation, not layout.
    // The destructor is FUN_005298e0: the EH unwind action of both the engine constructor
    // FUN_00527b80 and the engine destructor FUN_00527d10, whose normal path inlines it. A
    // member with its own destructor, so every exit runs it.
    struct FCliEngineNozzleVector
    {
        FCliEngineNozzleVector()
        {
        }
        // FUN_005298e0.
        ~FCliEngineNozzleVector();
        FCliEngineNozzleVector(FCliEngineNozzleVector const&) = delete;
        FCliEngineNozzleVector& operator=(FCliEngineNozzleVector const&) = delete;

        // [first, last) of the slots, as retail's loops walk them.
        FCliEngineNozzleSlot* First()
        {
            return Slots.data();
        }
        FCliEngineNozzleSlot* Last()
        {
            return Slots.data() + Slots.size();
        }

        std::vector<FCliEngineNozzleSlot> Slots; // retail +0x04 first, +0x08 last, +0x0c end
    };

    // The engine's client wrapper, FUN_00527b80, vtable PTR_LAB_005dd134 (slot 2 LAB_00528020).
    struct FCliEngineEquipFx : FCliEquipFxBase
    {
        // FUN_00527b80.
        FCliEngineEquipFx(CEquip* InEquip, FClientGameobj* InHost);
        // FUN_00527d10 (the body of the scalar deleting FUN_00527cf0).
        ~FCliEngineEquipFx() override;
        // FUN_00527e70.
        void notify(Event Code, void* Param) override;
        // LAB_00528020.
        bool Update(float Dt) override;
        // FUN_00528240: the rumble / character / cruise loop sounds.
        void UpdateSound(float Throttle, float Dt);

        FCliEngineNozzleVector Nozzles;   // +0x10 .. +0x1c
        float FxThrottle;                // +0x20 the throttle the effects follow (LAB_00528020)
        float ThrottleSteadyTime;         // +0x24 seconds FxThrottle has not moved
        float CruiseTime;                 // +0x28 seconds the cruise loop has played
        float AttenMod;                   // +0x2c the character loop's attenuation offset
        float CruiseSpeed;                // +0x30
        int CruiseState;                  // +0x34 0..3, set by FUN_00528560 (notify 0xe)
        ISoundSource* SoundEmitters[9];   // +0x38 … +0x58  FUN_00528f90
        FEffectArch* TrailEffect;         // +0x5c companion+0xa8
    };
    // Retail sizeof 0x60 (operator new(0x60) in FUN_00527b80's caller); the offsets on the
    // members above are retail's -- the std::vector at +0x10 is not VC6-sized, and nothing
    // reaches this wrapper by offset.

    // Companion sound cue CreateID offsets (FCliEngine::ReadUnknown).
    unsigned const kEngineSoundCueOff[9] = {
        0x60u, // rumble_sound
        0x74u, // character_start_sound
        0x78u, // character_loop_sound
        0x84u, // engine_kill_sound
        0x88u, // cruise_start_sound
        0x8cu, // cruise_loop_sound
        0x90u, // cruise_stop_sound
        0x94u, // cruise_disrupt_sound
        0x98u, // cruise_backfire_sound
    };
    unsigned char const kEngineSoundLoop[9] = {1, 0, 1, 0, 0, 1, 0, 0, 0};

    // DAT_006753f8 -- FUN_00528d70's lazily acquired 3DMathEngine.
    I3DMathEngine* G_EngineNozzleMath = nullptr;

    // FUN_00528d70 (on the wrapper): the average nozzle position in the HOST's
    // frame. Adds into *OutPos (the callers zero it) and returns how many nozzles resolved.
    int CliEngineAvgNozzleLocalPos(FCliEngineEquipFx* Wrapper, float OutPos[3])
    {
        // The equipment's nozzle vector (read with no class test): no element returns 0 with
        // *OutPos untouched.
        std::vector<ExhaustNozzleInfo> const& Nozzles =
            static_cast<CEEngine*>(Wrapper->Equip)->NozzleInfos();
        if (Nozzles.empty())
            return 0;

        // One hardpoint frame shared by the whole loop, starting at identity.
        Transform HpXform;
        HpXform.Rotation.E[0][0] = 1.0f;
        HpXform.Rotation.E[0][1] = 0.0f;
        HpXform.Rotation.E[0][2] = 0.0f;
        HpXform.Rotation.E[1][0] = 0.0f;
        HpXform.Rotation.E[1][1] = 1.0f;
        HpXform.Rotation.E[1][2] = 0.0f;
        HpXform.Rotation.E[2][0] = 0.0f;
        HpXform.Rotation.E[2][1] = 0.0f;
        HpXform.Rotation.E[2][2] = 1.0f;
        HpXform.Translation.x = 0.0f;
        HpXform.Translation.y = 0.0f;
        HpXform.Translation.z = 0.0f;

        int Count = 0;
        for (std::vector<ExhaustNozzleInfo>::const_iterator Cur = Nozzles.begin(); Cur != Nozzles.end();
             ++Cur)
        {
            if (!Cur->GetHardpointOffset_NS(HpXform))
                continue;
            // World = instance frame of the nozzle's part (no test) * hardpoint.
            const float* const Instance = (const float*)DALib::Engine->GetInstanceTransform(
                Cur->ParentInstance);
            if (G_EngineNozzleMath == nullptr)
                G_EngineNozzleMath = Acquire3DMathEngine();
            Transform Combined;
            G_EngineNozzleMath->MulTransform(Combined, *reinterpret_cast<Transform const*>(Instance),
                                             HpXform);
            Transform const World = Combined;

            // The inverse of the host's frame (the Transform get_orientation points into carries
            // the position at +0x24): rotation transposed, position -(M^T * P).
            Transform const& HostFrame =
                reinterpret_cast<Transform const&>(Wrapper->Host->get_orientation());
            Matrix const& HostRotation = HostFrame.Rotation;
            Vector HostPos;
            HostPos.x = HostFrame.Translation.x;
            HostPos.y = HostFrame.Translation.y;
            HostPos.z = HostFrame.Translation.z;
            Vector Rotated;
            ShipTransposeRotate(Rotated, HostRotation, HostPos); // FUN_00447be0
            Transform Inverse;
            for (int Row = 0; Row < 3; ++Row)
            {
                for (int Col = 0; Col < 3; ++Col)
                    Inverse.Rotation.E[Row][Col] = HostRotation.E[Col][Row];
            }
            Inverse.Translation.x = -Rotated.x;
            Inverse.Translation.y = -Rotated.y;
            Inverse.Translation.z = -Rotated.z;

            // Local = Inverse * World; its position is added into *OutPos.
            if (G_EngineNozzleMath == nullptr)
                G_EngineNozzleMath = Acquire3DMathEngine();
            Transform Local;
            G_EngineNozzleMath->MulTransform(Local, Inverse, World);
            Transform const LocalCopy = Local;
            OutPos[0] = static_cast<float>(static_cast<double>(LocalCopy.Translation.x) +
                                           static_cast<double>(OutPos[0]));
            ++Count;
            OutPos[1] = static_cast<float>(static_cast<double>(LocalCopy.Translation.y) +
                                           static_cast<double>(OutPos[1]));
            OutPos[2] = static_cast<float>(static_cast<double>(LocalCopy.Translation.z) +
                                           static_cast<double>(OutPos[2]));
        }

        // Retail quirk kept: no test for 0. With no nozzle resolved this is 1/0 = +inf and the
        // sums (0) become NaN, which is what retail hands on (FUN_00528f90 keeps going for an
        // indestructible engine).
        double const Inv = 1.0 / static_cast<double>(Count);
        OutPos[0] = static_cast<float>(Inv * static_cast<double>(OutPos[0]));
        OutPos[1] = static_cast<float>(Inv * static_cast<double>(OutPos[1]));
        OutPos[2] = static_cast<float>(Inv * static_cast<double>(OutPos[2]));
        return Count;
    }

    // FUN_004291c0 on an emitter's own +0x64 archetype: Start(arch, 0).
    void CliEngineStartEmitter(ISoundSource* Emitter)
    {
        Emitter->Start(GetClientEquipSoundEmitterArch(Emitter), 0);
    }

    // The "restart" idiom: IsActive (+0x14) -> SetStartOffset(0) (+0x64), else Start.
    void CliEngineRestartEmitter(ISoundSource* Emitter)
    {
        if (Emitter->IsActive())
            Emitter->SetStartOffset(0);
        else
            CliEngineStartEmitter(Emitter);
    }

    void CliEngineReleaseSoundEmitters(FCliEngineEquipFx* Wrapper)
    {
        // FUN_005295d0: +0x38..+0x58 in order, each when set: Stop (+0x60), Release (+8),
        // clear (every slot is also cleared on the null path).
        for (int Index = 0; Index < 9; ++Index)
        {
            ISoundSource* Src = Wrapper->SoundEmitters[Index];
            if (Src == nullptr)
                continue;
            Src->Stop();
            Src->Release();
            Wrapper->SoundEmitters[Index] = nullptr;
        }
    }

    void CliEngineSoundInit(FCliEngineEquipFx* Wrapper)
    {
        // FUN_00528f90 (on the wrapper). No nozzle resolved and the archetype not
        // indestructible (read with no class test) ends here.
        float NozzleAvg[3] = {0.0f, 0.0f, 0.0f};
        int const NozzleCount = CliEngineAvgNozzleLocalPos(Wrapper, NozzleAvg);
        if (NozzleCount == 0 &&
            !static_cast<Archetype::Engine const*>(Wrapper->Equip->Arch)->bIndestructible)
        {
            return;
        }
        if (DALib::Sound == nullptr)
            return;

        // Identity orientation, the average as the position.
        Transform PoseXform;
        std::memset(&PoseXform, 0, sizeof(PoseXform));
        PoseXform.Rotation.E[0][0] = 1.0f;
        PoseXform.Rotation.E[1][1] = 1.0f;
        PoseXform.Rotation.E[2][2] = 1.0f;
        PoseXform.Translation.x = NozzleAvg[0];
        PoseXform.Translation.y = NozzleAvg[1];
        PoseXform.Translation.z = NozzleAvg[2];

        // The companion (a null equipment gives a null companion that is read anyway): cone
        // angles +0x54 / +0x58 as unsigned ints to float, cone outside volume +0x5c raw.
        unsigned char const* const CompBytes = reinterpret_cast<unsigned char const*>(
            Wrapper->Equip != nullptr ? Wrapper->Equip->Arch->Companion : nullptr);
        float const ConeInner = static_cast<float>(
            static_cast<long long>(*reinterpret_cast<unsigned const*>(CompBytes + 0x54)));
        float const ConeOuter = static_cast<float>(
            static_cast<long long>(*reinterpret_cast<unsigned const*>(CompBytes + 0x58)));
        float const ConeOutsideVol = *reinterpret_cast<float const*>(CompBytes + 0x5c);

        // Per cue, in slot order: a found archetype gets an emitter (FUN_00527a70); the slot
        // takes the emitter or null. Every emitter but engine_kill, cruise_disrupt and
        // cruise_backfire gets the cone; the host's index is read per emitter.
        static unsigned char const kEngineSoundCone[9] = {1, 1, 1, 0, 1, 1, 1, 0, 0};
        for (int Index = 0; Index < 9; ++Index)
        {
            ID_String const* const Cue =
                reinterpret_cast<ID_String const*>(CompBytes + kEngineSoundCueOff[Index]);
            ISoundSource* Emitter = nullptr;
            if (!Cue->IsEmpty())
            {
                void* const SoundArch = LookupClientSoundArch(Cue);
                if (SoundArch != nullptr)
                    Emitter = static_cast<ISoundSource*>(CreateClientEquipSoundEmitter(SoundArch));
            }
            Wrapper->SoundEmitters[Index] = Emitter;
            if (Emitter == nullptr)
                continue;
            if (kEngineSoundCone[Index] != 0)
            {
                SetClientSoundSourceCone(Emitter, ConeInner, ConeOuter);
                SetClientSoundSourceConeOutsideVolume(Emitter, ConeOutsideVol);
            }
            Emitter->SetPose(static_cast<int>(Wrapper->Host->get_index()),
                             &PoseXform);
            Emitter->SetLooping(kEngineSoundLoop[Index]);
        }

        // In cruise (no class test) the cruise loop starts, otherwise rumble, character start
        // and character loop.
        if (static_cast<CEEngine*>(Wrapper->Equip)->bCruiseActive != 0)
        {
            if (Wrapper->SoundEmitters[5] != nullptr)
                CliEngineStartEmitter(Wrapper->SoundEmitters[5]);
            return;
        }
        if (Wrapper->SoundEmitters[0] != nullptr)
            CliEngineStartEmitter(Wrapper->SoundEmitters[0]);
        if (Wrapper->SoundEmitters[1] != nullptr)
            CliEngineStartEmitter(Wrapper->SoundEmitters[1]);
        if (Wrapper->SoundEmitters[2] != nullptr)
            CliEngineStartEmitter(Wrapper->SoundEmitters[2]);
    }

    // FUN_005299b0 as FUN_00528980 uses it: insert(end, 1, an empty slot) on the nozzle vector,
    // i.e. push_back. On a reallocation retail copy-constructs the old slots across
    // (FUN_00529ce0: id, AddRef on both effects) and destroys the originals (release both),
    // which leaves every count as it was -- std::vector relocates the plain slots as they are.
    // The new slot is the empty value: both effects null (its id is a stray stack word the
    // caller overwrites; 0 here).
    FCliEngineNozzleSlot* CliEngineNozzlePushBack(FCliEngineEquipFx* Wrapper)
    {
        FCliEngineNozzleSlot Empty;
        Empty.SubObjId = 0;
        Empty.Pad02 = 0;
        Empty.TrailFx = nullptr;
        Empty.CruiseFx = nullptr;
        Wrapper->Nozzles.Slots.push_back(Empty);
        return &Wrapper->Nozzles.Slots.back();
    }

    // The reference store both arms of FUN_00528980 inline: when the effect is not already the slot's, release the old one
    // (clear), store it and AddRef (FUN_004f7a80).
    void CliEngineAssignParticleRef(FParticleEffect*& Ref, FParticleEffect* Effect)
    {
        if (Ref == Effect)
            return;
        if (Ref != nullptr)
        {
            Ref->Release();
            Ref = nullptr;
        }
        Ref = Effect;
        Effect->AddRef();
    }

    // FUN_00528980 (on the wrapper): one slot per nozzle whose hardpoint resolves, with a trail
    // and a cruise particle effect.
    void CliEngineAttachExhaustEffects(FCliEngineEquipFx* Wrapper)
    {
        // A null equipment gives a null companion that is read anyway.
        unsigned char const* const CompBytes = reinterpret_cast<unsigned char const*>(
            Wrapper->Equip != nullptr ? Wrapper->Equip->Arch->Companion : nullptr);
        FEffectArch* TrailArch = nullptr;
        FEffectArch* CruiseNpcArch = nullptr;
        FEffectArch* CruisePlayerArch = nullptr;
        ID_String const* const TrailId = reinterpret_cast<ID_String const*>(CompBytes + 0x9c);
        if (!TrailId->IsEmpty())
            TrailArch = FindEffectArch(&TrailId->Id);
        // The player's engine (host id against DAT_00673378) takes +0xa4, every other +0xa0.
        if (Wrapper->Host->get_id() != GetPlayerCharacterId())
        {
            ID_String const* const NpcId = reinterpret_cast<ID_String const*>(CompBytes + 0xa0);
            if (!NpcId->IsEmpty())
                CruiseNpcArch = FindEffectArch(&NpcId->Id);
        }
        else
        {
            ID_String const* const PlayerId =
                reinterpret_cast<ID_String const*>(CompBytes + 0xa4);
            if (!PlayerId->IsEmpty())
                CruisePlayerArch = FindEffectArch(&PlayerId->Id);
        }
        if (TrailArch == nullptr && CruiseNpcArch == nullptr && CruisePlayerArch == nullptr)
            return;

        // One hardpoint frame for the whole loop, starting at identity; the nozzle vector is
        // read with no class test.
        Transform HpXform;
        HpXform.Rotation.E[0][0] = 1.0f;
        HpXform.Rotation.E[0][1] = 0.0f;
        HpXform.Rotation.E[0][2] = 0.0f;
        HpXform.Rotation.E[1][0] = 0.0f;
        HpXform.Rotation.E[1][1] = 1.0f;
        HpXform.Rotation.E[1][2] = 0.0f;
        HpXform.Rotation.E[2][0] = 0.0f;
        HpXform.Rotation.E[2][1] = 0.0f;
        HpXform.Rotation.E[2][2] = 1.0f;
        HpXform.Translation.x = 0.0f;
        HpXform.Translation.y = 0.0f;
        HpXform.Translation.z = 0.0f;
        std::vector<ExhaustNozzleInfo> const& Nozzles =
            static_cast<CEEngine*>(Wrapper->Equip)->NozzleInfos();
        for (std::vector<ExhaustNozzleInfo>::const_iterator Cur = Nozzles.begin(); Cur != Nozzles.end();
             ++Cur)
        {
            if (!Cur->GetHardpointOffset_NS(HpXform))
                continue;

            // An empty slot appended, then it takes the nozzle's +0 word.
            FCliEngineNozzleSlot* const Slot = CliEngineNozzlePushBack(Wrapper);
            Slot->SubObjId = Cur->SubObjId;
            Transform const* const Local = &HpXform;
            int const Part = static_cast<int>(Cur->ParentInstance);

            // Trail: a failed allocation leaves null, used anyway; the creation reference is
            // dropped either way.
            if (TrailArch != nullptr)
            {
                FParticleEffect* const Effect = ParticleEffectCreate();
                Effect->Attach(Wrapper->Host, Part, Local);
                if (Effect->FParticleEffect::Start(TrailArch) != 0)
                    CliEngineAssignParticleRef(Slot->TrailFx, Effect);
                Effect->Release();
            }
            // Cruise: the NPC arch makes the 0x90 cruise effect, the player's arch a plain one.
            FEffectArch* CruiseArch = nullptr;
            FParticleEffect* Cruise = nullptr;
            if (CruiseNpcArch != nullptr)
            {
                Cruise = ParticleEffectCreateCruise();
                CruiseArch = CruiseNpcArch;
            }
            else if (CruisePlayerArch != nullptr)
            {
                Cruise = ParticleEffectCreate();
                CruiseArch = CruisePlayerArch;
            }
            if (CruiseArch != nullptr)
            {
                Cruise->Attach(Wrapper->Host, Part, Local);
                if (Cruise->FParticleEffect::Start(CruiseArch) != 0)
                    CliEngineAssignParticleRef(Slot->CruiseFx, Cruise);
                Cruise->Release();
            }
        }
    }

    struct FCliTradeLaneEquipFx;

    // One TLR joint channel: an AnimDB::VChannel (vtable PTR_LAB_005db914 = {Tick LAB_00516cd0,
    // GetId LAB_004313f0}) carrying the engine child instance (-1 = none) and the wrapper whose
    // spin angle it drives.
    struct FTradeLaneRingChannel : AnimDB::VChannel
    {
        // LAB_00516cd0: the owner's spin angle in radians (* DAT_005c77f4) as joint parameter 0
        // of GetId()'s instance. Dt is not read.
        void Tick(float Dt) override;
        // LAB_004313f0 (shared with other channel tables).
        int GetId() const override;

        int ChildIndex;                         // +0x04
        FCliTradeLaneEquipFx* Owner;            // +0x08 set by FUN_00516af0
    };
    static_assert(sizeof(FTradeLaneRingChannel) == 0xc, "TLR channel 0xc");

    // FUN_00516c30 layout 0x34, vtable PTR_FUN_005db904 {notify FUN_00531b20 (the base's),
    // scalar deleting dtor FUN_00516c10 (body FUN_00516bb0), Update LAB_00516ea0}.
    // +0x10 / +0x1c are the two TLR_Left / TLR_Right joint channels.
    struct FCliTradeLaneEquipFx : FCliEquipFxBase
    {
        ~FCliTradeLaneEquipFx() override;
        // LAB_00516ea0.
        bool Update(float Dt) override;

        FTradeLaneRingChannel TlrLeft;          // +0x10
        FTradeLaneRingChannel TlrRight;         // +0x1c
        float SpinAngle;                        // +0x28 degrees, wrapped to (.., 360]
        float SpinSpeed;                        // +0x2c degrees/s, starts at CliEQTradeLane +0xac
        // +0x30: ships using the lane -- zeroed by the constructor, stepped +/-1 by the solar's
        // lane switch FUN_00516910; Update
        // spins the rings up while it is 0 and down otherwise.
        int ShipsUsingLane;
    };
    static_assert(sizeof(FCliTradeLaneEquipFx) == 0x34, "TradeLaneFx 0x34");
    static_assert(offsetof(FCliTradeLaneEquipFx, TlrLeft) == 0x10, "TLR left @+0x10");
    static_assert(offsetof(FCliTradeLaneEquipFx, TlrRight) == 0x1c, "TLR right @+0x1c");
    static_assert(offsetof(FCliTradeLaneEquipFx, SpinAngle) == 0x28, "TradeLane +0x28");
    static_assert(offsetof(FCliTradeLaneEquipFx, ShipsUsingLane) == 0x30, "TradeLane +0x30");

    // DAT_005c77f4.
    float const kTradeLaneDegToRad = 0.01745329238474369f;

    void FTradeLaneRingChannel::Tick(float /*Dt*/)
    {
        float const Radians = Owner->SpinAngle * kTradeLaneDegToRad;
        int const Id = GetId();
        DALib::Engine->SetJointParams(Id, 0,
                                      &Radians);
    }

    int FTradeLaneRingChannel::GetId() const
    {
        return ChildIndex;
    }

    // FUN_00516af0 — walk Engine GetRelatedInstance(OwnerRoot, 0, cursor) for TLR_Left/Right.
    void CliTradeLaneFindTlrHardpoints(FCliTradeLaneEquipFx* Wrapper)
    {
        if (Wrapper == nullptr || Wrapper->Equip == nullptr || DALib::Engine == nullptr)
            return;
        CEqObj* Owner = Wrapper->Equip->Owner;
        if (Owner == nullptr)
            return;
        InstanceIndex Root = Owner->EngineInstanceId;
        InstanceIndex Child = DALib::Engine->GetRelatedInstance(Root, 0, -1);
        while (Child != 0 && Child != InvalidInstanceIndex &&
               static_cast<ptrdiff_t>(Child) != -1)
        {
            FEngineName NameOut = {};
            FEngineName* Name = DALib::Engine->GetInstanceName(&NameOut, Child);
            char const* Text = "";
            if (Name != nullptr && Name->Text != nullptr)
                Text = Name->Text;
            int const ChildId = static_cast<int>(Child);
            if (std::strcmp(Text, "TLR_Right") == 0)
            {
                Wrapper->TlrRight.ChildIndex = ChildId;
                Wrapper->TlrRight.Owner = Wrapper;
                if (Wrapper->TlrLeft.ChildIndex != -1)
                    return;
            }
            else if (std::strcmp(Text, "TLR_Left") == 0)
            {
                Wrapper->TlrLeft.ChildIndex = ChildId;
                Wrapper->TlrLeft.Owner = Wrapper;
                if (Wrapper->TlrRight.ChildIndex != -1)
                    return;
            }
            Child = DALib::Engine->GetRelatedInstance(Root, 0, Child);
        }
    }

    // FUN_00516d10. No test of Host, Equip, arch or companion. The lane effect goes on
    // HpRightLane then HpLeftLane (each tried on its own), the shield effect on HpShield01. Every
    // effect is attached to the root index, not the hardpoint handle FindHardpoint_PS wrote
    // (that out value is unused).
    void CliTradeLaneAttachLaneEffects(FCliTradeLaneEquipFx* Wrapper)
    {
        FClientGameobj* const Host = Wrapper->Host;
        long const Root = Host->get_index();
        unsigned char* const Comp =
            reinterpret_cast<unsigned char*>(Wrapper->Equip->EquipArch()->Companion);

        FEffectArch* const LaneFx = FindEffectArch(reinterpret_cast<unsigned const*>(Comp + 0x84));
        if (LaneFx != nullptr)
        {
            long HpHandle = 0;
            HardpointInfo HpInfo;
            Transform Xform;
            if (FindHardpoint_PS(Root, "HpRightLane", HpHandle, HpInfo))
            {
                std::memcpy(&Xform.Rotation, &HpInfo.Orient, sizeof(Xform.Rotation));
                Xform.Translation.x = HpInfo.Position.x;
                Xform.Translation.y = HpInfo.Position.y;
                Xform.Translation.z = HpInfo.Position.z;
                CliCreateEffectOneShot(LaneFx, Host, static_cast<int>(Root), &Xform, nullptr);
            }
            if (FindHardpoint_PS(Root, "HpLeftLane", HpHandle, HpInfo))
            {
                std::memcpy(&Xform.Rotation, &HpInfo.Orient, sizeof(Xform.Rotation));
                Xform.Translation.x = HpInfo.Position.x;
                Xform.Translation.y = HpInfo.Position.y;
                Xform.Translation.z = HpInfo.Position.z;
                CliCreateEffectOneShot(LaneFx, Host, static_cast<int>(Root), &Xform, nullptr);
            }
        }

        FEffectArch* const ShieldFx =
            FindEffectArch(reinterpret_cast<unsigned const*>(Comp + 0x80));
        if (ShieldFx != nullptr)
        {
            long HpHandle = 0;
            HardpointInfo HpInfo;
            if (FindHardpoint_PS(Root, "HpShield01", HpHandle, HpInfo))
            {
                Transform Xform;
                std::memcpy(&Xform.Rotation, &HpInfo.Orient, sizeof(Xform.Rotation));
                Xform.Translation.x = HpInfo.Position.x;
                Xform.Translation.y = HpInfo.Position.y;
                Xform.Translation.z = HpInfo.Position.z;
                CliCreateEffectOneShot(ShieldFx, Host, static_cast<int>(Root), &Xform, nullptr);
            }
        }
    }

    // FUN_0040c690 (the CliEQTradeLane companion's effect AddRef) is Archetype::
    // CliTradeLaneAddEffectRef in client_cliobj.cpp; its release FUN_0040c7d0 is
    // Archetype::CliTradeLaneReleaseEffectRef.

    void CliEquipFxBaseFill(FCliEquipFxBase* Wrapper, CEquip* Equip, FClientGameobj* Host)
    {
        // FUN_00531a30 (inlined into most type constructors). No test of Equip; an attached
        // equipment's separation_explosion is resolved and referenced when found.
        Wrapper->Equip = Equip;
        Wrapper->Host = Host;
        Wrapper->SeparationExplosion = nullptr;
        Equip->ConnectClientEquip(Wrapper);
        CAttachedEquip* const Attached = CAttachedEquip::cast(Equip);
        if (Attached != nullptr &&
            !Attached->AttachedEquipArch()->SeparationExplosion.IsEmpty())
        {
            Wrapper->SeparationExplosion =
                Archetype::GetExplosion(Attached->AttachedEquipArch()->SeparationExplosion);
            if (Wrapper->SeparationExplosion != nullptr)
                ExplosionArchAddRef(Wrapper->SeparationExplosion);
        }
    }

    FCliShieldGenEquipFx* CliShieldGenEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        // Factory arm: operator new(0x10), null-tested; FUN_0052eb10.
        FCliShieldGenEquipFx* Wrapper = new (std::nothrow) FCliShieldGenEquipFx();
        if (Wrapper == nullptr)
            return nullptr;
        CliEquipFxBaseFill(Wrapper, Equip, Host);
        return Wrapper;
    }

    FCliCloakEquipFx* CliCloakEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        // Factory arm: operator new(0x10), null-tested; FUN_00531e80.
        FCliCloakEquipFx* Wrapper = new (std::nothrow) FCliCloakEquipFx();
        if (Wrapper == nullptr)
            return nullptr;
        CliEquipFxBaseFill(Wrapper, Equip, Host);
        return Wrapper;
    }

    FCliRepairDroidEquipFx* CliRepairDroidEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        // Factory arm: operator new(0x20), null-tested; FUN_00532800.
        FCliRepairDroidEquipFx* Wrapper = new (std::nothrow) FCliRepairDroidEquipFx();
        if (Wrapper == nullptr)
            return nullptr;
        CliEquipFxBaseFill(Wrapper, Equip, Host);
        Wrapper->Companion[0] = nullptr;
        Wrapper->Companion[1] = nullptr;
        Wrapper->Companion[2] = nullptr;
        Wrapper->Companion[3] = nullptr;
        return Wrapper;
    }

    // DAT_00675520 / DAT_0067551c / DAT_00675518, filled by the static initializers
    // LAB_005319d0 / LAB_005319f0 / LAB_00531a10.
    unsigned const G_RepairDroidActivatedId = CreateID("repair_droid_activated");
    unsigned const G_RepairDroidDeactivatedId = CreateID("repair_droid_deactivated");
    unsigned const G_RepairDroidExhaustedId = CreateID("repair_droid_exhausted");

    FCliRepairDroidEquipFx::~FCliRepairDroidEquipFx()
    {
        // FUN_005328e0; the base destructor follows.
        CliEffectCompanionClear(Companion);
    }

    void FCliRepairDroidEquipFx::notify(Event Code, void* /*Param*/)
    {
        // LAB_00532980. FUN_00531b20 is never called.
        if (Code == EquipActivating)
        {
            CliEffectCompanionArmStop(Companion);
            CliEffectCompanionClear(Companion);
            // The companion archetype is null without an equipment and read anyway.
            Transform Identity;
            std::memset(&Identity, 0, sizeof(Identity));
            Identity.Rotation.E[0][0] = 1.0f;
            Identity.Rotation.E[1][1] = 1.0f;
            Identity.Rotation.E[2][2] = 1.0f;
            unsigned char const* const Comp = reinterpret_cast<unsigned char const*>(
                Equip != nullptr ? Equip->Arch->Companion : nullptr);
            long const Index = Host->get_index();
            FClientGameobj* const Owner = Host;
            ID_String const* const FxId = reinterpret_cast<ID_String const*>(Comp + 0x54);
            if (!FxId->IsEmpty())
            {
                CliCreateEffectOneShot(FindEffectArch(&FxId->Id), Owner, static_cast<int>(Index),
                                       &Identity, Companion);
            }
            if (CliEquipHostIsPlayer(Host))
                NnVoiceEnqueue(G_RepairDroidActivatedId, 0);
            return;
        }
        if (Code == EquipDeactivating)
        {
            CliEffectListRemove(&Host->EffectList, Companion);
            // Hit points above 0 pick "deactivated", else (NaN included) "exhausted".
            if (CliEquipHostIsPlayer(Host))
            {
                if (Equip->GetHitPoints() > 0.0f)
                    NnVoiceEnqueue(G_RepairDroidDeactivatedId, 0);
                else
                    NnVoiceEnqueue(G_RepairDroidExhaustedId, 0);
            }
            return;
        }
        if (Code == EquipDestroying)
        {
            // No test of Equip.
            Equip->NotifyDisconnecting(this);
            Equip = nullptr;
        }
    }

    FCliShieldEquipFx* CliShieldEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        // Factory arm: operator new(0x18), null-tested; FUN_0052e200. LoadSounds reads the
        // equipment's +0x70 with no cast, as every shield slot does.
        FCliShieldEquipFx* Wrapper = new (std::nothrow) FCliShieldEquipFx();
        if (Wrapper == nullptr)
            return nullptr;
        CliEquipFxBaseFill(Wrapper, Equip, Host);
        Wrapper->CollapseSound = nullptr;
        Wrapper->RebuiltSound = nullptr;
        Wrapper->LoadSounds();
        return Wrapper;
    }

    FCliTractorEquipFx* CliTractorEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        // Factory arm: operator new(0x38), null-tested; FUN_0052f540.
        FCliTractorEquipFx* Wrapper = new (std::nothrow) FCliTractorEquipFx();
        if (Wrapper == nullptr)
            return nullptr;
        CliEquipFxBaseFill(Wrapper, Equip, Host);
        for (int I = 0; I < 4; ++I)
        {
            Wrapper->BeamCompanion[I] = nullptr;
            Wrapper->OperatingCompanion[I] = nullptr;
        }
        Wrapper->OperatingEffect = nullptr;
        Wrapper->CompleteSound = nullptr;

        // Each use re-reads the companion archetype (null without an equipment), and the
        // +0x58 / +0x5c address is taken either way.
        unsigned char const* const Comp = reinterpret_cast<unsigned char const*>(
            Wrapper->Equip != nullptr ? Wrapper->Equip->Arch->Companion : nullptr);
        ID_String const* const Operating = reinterpret_cast<ID_String const*>(Comp + 0x58);
        if (!Operating->IsEmpty())
        {
            Wrapper->OperatingEffect = FindEffectArch(&Operating->Id);
            Wrapper->OperatingEffect->AddRef(1u);
        }
        // tractor_complete_snd, acquired with no test of the lookup.
        ID_String const* const Complete = reinterpret_cast<ID_String const*>(Comp + 0x5c);
        if (!Complete->IsEmpty())
        {
            Wrapper->CompleteSound = LookupClientSoundArch(Complete);
            static_cast<Archetype::ISound*>(Wrapper->CompleteSound)->Acquire();
        }
        return Wrapper;
    }

    // DAT_00675498 / DAT_00675494, filled by the static initializers LAB_0052f500 /
    // LAB_0052f520.
    unsigned const G_TractorFailureVoiceId = CreateID("nnv_tractor_failure");
    unsigned const G_TargetOutOfRangeVoiceId = CreateID("target_out_of_range");

    FCliTractorEquipFx::~FCliTractorEquipFx()
    {
        // FUN_0052f660.
        if (OperatingEffect != nullptr)
            OperatingEffect->Release();
        // Not cleared.
        if (CompleteSound != nullptr)
            static_cast<Archetype::ISound*>(CompleteSound)->Release();
        // The two companions in reverse member order.
        CliEffectCompanionClear(OperatingCompanion);
        CliEffectCompanionClear(BeamCompanion);
        // The base destructor FUN_00531af0 follows.
    }

    unsigned char FCliTractorEquipFx::StartBeam()
    {
        // FUN_0052fa10. A failed allocation leaves null, used anyway.
        FCliTractorBeam* const Beam = CliTractorBeamCreate();
        unsigned char const bOk = Beam->Init(this);
        if (bOk != 0)
        {
            // A local companion {0, 0, beam, 0} (its copy takes a reference) into the host's
            // list, then destroyed.
            void* Local[4] = {nullptr, nullptr, Beam, nullptr};
            if (Beam != nullptr)
                Beam->AddRef();
            CliEffectListInsert(&Host->EffectList, Local);
            if (Local[2] != nullptr)
                static_cast<FEffectBase*>(Local[2])->Release();

            // A second local {0, 0, beam, 0} assigned into +0x10 (the old contents released),
            // then the local released.
            if (Beam != nullptr)
                Beam->AddRef();
            CliEffectCompanionClear(BeamCompanion);
            BeamCompanion[2] = Beam;
            if (Beam != nullptr)
                Beam->AddRef();
            BeamCompanion[1] = nullptr;
            BeamCompanion[3] = nullptr;
            if (Beam != nullptr)
                Beam->Release();
        }
        // The allocation's own reference, no test.
        Beam->Release();
        return bOk;
    }

    void FCliTractorEquipFx::DropLoot(unsigned LootId, int Units)
    {
        // FUN_0052f9a0.
        if (Equip == nullptr || Equip->IsDestroyed())
            return;
        // No cast check.
        CETractor* const Tractor = static_cast<CETractor*>(Equip);
        TractorArm const* const Arm = Tractor->GetTractorArm(LootId);
        if (Arm == nullptr)
            return;
        // Loot with units left (signed) stays on the arm.
        CLoot const* const Loot = Arm->GetTarget();
        if (Loot != nullptr && static_cast<int>(Loot->Units) - Units > 0)
            return;
        // The player's host (a pointer compare), or any host outside single player.
        if (static_cast<void*>(static_cast<IObjRW*>(Host)) != GetPlayerObjRW() && SinglePlayer())
            return;
        Tractor->RemoveTarget(LootId);
    }

    void FCliTractorEquipFx::notify(Event Code, void* Param)
    {
        // LAB_0052f740: every arm ends in the base notify with both arguments.
        switch (Code)
        {
        case EquipDestroying:
            CliEffectListRemove(&Host->EffectList, BeamCompanion);
            CliEffectListRemove(&Host->EffectList, OperatingCompanion);
            break;
        case EquipActivating:
            StartBeam();
            if (OperatingEffect != nullptr)
            {
                Transform Identity;
                std::memset(&Identity, 0, sizeof(Identity));
                Identity.Rotation.E[0][0] = 1.0f;
                Identity.Rotation.E[1][1] = 1.0f;
                Identity.Rotation.E[2][2] = 1.0f;
                long const Index = Host->get_index();
                CliCreateEffectOneShot(OperatingEffect, Host, static_cast<int>(Index), &Identity,
                                       OperatingCompanion);
            }
            break;
        case EquipDeactivating:
        {
            // The complete sound at the tractor's source (no cast), positional (the key's upper
            // flag bits are stack garbage in retail, zero here); no test of the archetype.
            Vector const Source = static_cast<CETractor*>(Equip)->GetSourcePos();
            unsigned Key[6] = {0u, 2u, 0u, 0u, 0u, 0u};
            std::memcpy(&Key[2], &Source.x, sizeof(float));
            std::memcpy(&Key[3], &Source.y, sizeof(float));
            std::memcpy(&Key[4], &Source.z, sizeof(float));
            PlayClientSoundArch(CompleteSound, Key, nullptr, 0);
            CliEffectListRemove(&Host->EffectList, BeamCompanion);
            CliEffectListRemove(&Host->EffectList, OperatingCompanion);
            break;
        }
        case TractorTargetRejected:
        {
            // The player's host with no arm active: failure code 2 -> "target_out_of_range",
            // 3..6 -> "nnv_tractor_failure".
            CETractor* const Tractor = static_cast<CETractor*>(Equip);
            if (static_cast<void*>(static_cast<IObjRW*>(Host)) != GetPlayerObjRW())
                break;
            if (Tractor->CountActiveArms() != 0)
                break;
            int const Failure = *static_cast<int const*>(Param);
            if (Failure == 2)
                NnVoiceEnqueue(G_TargetOutOfRangeVoiceId, 0);
            else if (Failure > 2 && Failure <= 6)
                NnVoiceEnqueue(G_TractorFailureVoiceId, 0);
            break;
        }
        case TractorArmFailed:
        {
            // The same with exactly one arm active and the arm's own error code (Param is the
            // TractorArm).
            CETractor* const Tractor = static_cast<CETractor*>(Equip);
            if (static_cast<void*>(static_cast<IObjRW*>(Host)) != GetPlayerObjRW())
                break;
            if (Tractor->CountActiveArms() != 1)
                break;
            int const Failure = static_cast<int>(static_cast<TractorArm const*>(Param)->ErrorCode);
            if (Failure == 2)
                NnVoiceEnqueue(G_TargetOutOfRangeVoiceId, 0);
            else if (Failure > 2 && Failure <= 6)
                NnVoiceEnqueue(G_TractorFailureVoiceId, 0);
            break;
        }
        default:
            break;
        }
        FClientEquip::notify(Code, Param);
    }

    FCliThrusterEquipFx* CliThrusterEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        // Factory arm: operator new(0x20), null-tested; FUN_0052ec40.
        FCliThrusterEquipFx* Wrapper = new (std::nothrow) FCliThrusterEquipFx();
        if (Wrapper == nullptr)
            return nullptr;
        CliEquipFxBaseFill(Wrapper, Equip, Host);
        Wrapper->Companion[0] = nullptr;
        Wrapper->Companion[1] = nullptr;
        Wrapper->Companion[2] = nullptr;
        Wrapper->Companion[3] = nullptr;

        // The companion archetype, no tests; hp_particles (+0x58) is a char pointer.
        unsigned char const* const Comp =
            reinterpret_cast<unsigned char const*>(Equip->Arch->Companion);
        char const* const HpName = *reinterpret_cast<char const* const*>(Comp + 0x58);
        if (HpName[0] == '\0')
            return Wrapper;
        ID_String const* const Particles = reinterpret_cast<ID_String const*>(Comp + 0x54);
        if (Particles->IsEmpty())
            return Wrapper;
        // No cast check.
        long const Root = static_cast<CAttachedEquip*>(Equip)->GetRootIndex();
        long HpHandle;
        HardpointInfo HpInfo;
        if (!FindHardpoint_PS(Root, HpName, HpHandle, HpInfo))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Thruster hardpoint not found: %s\n",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliThruster.cpp", 0x2c, HpName);
            return Wrapper;
        }
        Transform Xform;
        std::memcpy(&Xform.Rotation, &HpInfo.Orient, sizeof(Xform.Rotation));
        Xform.Translation.x = HpInfo.Position.x;
        Xform.Translation.y = HpInfo.Position.y;
        Xform.Translation.z = HpInfo.Position.z;
        // Particles tested again (retail).
        if (Particles->IsEmpty())
            return Wrapper;
        // The hardpoint handle is passed as the part index.
        CliCreateEffectOneShot(FindEffectArch(&Particles->Id), Host, static_cast<int>(HpHandle),
                               &Xform, Wrapper->Companion);
        return Wrapper;
    }

    // DAT_00675480, filled by the static initializer LAB_0052ef90.
    unsigned const G_ThrusterDestroyedId = CreateID("thruster_destroyed");

    FCliThrusterEquipFx::~FCliThrusterEquipFx()
    {
        // FUN_0052edc0; the base destructor FUN_00531af0 follows.
        CliEffectCompanionClear(Companion);
    }

    void FCliThrusterEquipFx::notify(Event Code, void* Param)
    {
        // FUN_0052ee40: every arm ends in the base notify with both arguments.
        switch (Code)
        {
        case EquipDestroying:
            if (CliEquipHostIsLivePlayer(Host, CObject::ClassShip))
                NnVoiceEnqueue(G_ThrusterDestroyedId, 0);
            CliEffectListRemove(&Host->EffectList, Companion);
            break;
        case EquipActivating:
        case EquipDeactivating:
        {
            FEffectBase* const Effect = static_cast<FEffectBase*>(Companion[2]);
            if (Effect != nullptr)
            {
                Effect->SetIntensity(Code == EquipActivating ? 1.0f : 0.0f);
            }
            break;
        }
        case NotEnoughPower:
        {
            // The host object's owner (a non-CSimple counts as the local player, DAT_00673344);
            // the local player's thrusters are switched off.
            CObject* const Obj = Host->Object;
            unsigned const Owner =
                (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
                    ? static_cast<CSimple const*>(Obj)->OwnerPlayer
                    : GetPlayerConnectionId();
            if (Owner == GetPlayerConnectionId())
                static_cast<IObjAffect*>(Host)->toggle_thrusters(
                    false, static_cast<int>(GetPlayerConnectionId()));
            break;
        }
        default:
            // 0xd, 0xe and everything out of range: the base only.
            break;
        }
        FClientEquip::notify(Code, Param);
    }

    FCliEQAttachedFXView const* CompanionAsAttachedFX(Archetype::ICliObj* Companion)
    {
        return reinterpret_cast<FCliEQAttachedFXView const*>(
            reinterpret_cast<unsigned char const*>(Companion) + 0x54);
    }

    FCliAttachedFxEquipFx::FCliAttachedFxEquipFx(CEquip* EquipParam, FClientGameobj* HostParam)
    {
        // FUN_00532210, with the base (FUN_00531a30) inlined.
        CliEquipFxBaseFill(this, EquipParam, HostParam);

        Companion[0] = nullptr;
        Companion[1] = nullptr;
        Companion[2] = nullptr;
        Companion[3] = nullptr;

        // The CliEQAttachedFX is null without an equipment -- retail then reads +0x54 off null.
        Archetype::ICliObj* const Comp = (Equip != nullptr) ? Equip->Arch->Companion : nullptr;
        if (CompanionAsAttachedFX(Comp)->Particles.IsEmpty())
            return;

        // GetHardPointInfo's result is not tested.
        CExternalEquip const* const External = static_cast<CExternalEquip const*>(Equip);
        HardpointInfo Hardpoint;
        External->GetHardPointInfo(Hardpoint);
        Transform Local;
        std::memcpy(&Local.Rotation, &Hardpoint.Orient, sizeof(Local.Rotation));
        Local.Translation.x = Hardpoint.Position.x;
        Local.Translation.y = Hardpoint.Position.y;
        Local.Translation.z = Hardpoint.Position.z;

        // The companion read again, and a second IsEmpty on the same "particles".
        Archetype::ICliObj* const Comp2 = (Equip != nullptr) ? Equip->Arch->Companion : nullptr;
        long const Part = External->GetParentConnector(false);
        FCliEQAttachedFXView const* const View = CompanionAsAttachedFX(Comp2);
        if (View->Particles.IsEmpty())
            return;

        CliCreateEffectOneShot(FindEffectArch(&View->Particles.Id), Host, static_cast<int>(Part),
                               &Local, Companion);
    }

    FCliAttachedFxEquipFx::~FCliAttachedFxEquipFx()
    {
        // FUN_005323c0 (shared with the light wrapper): the companion's destructor, then the
        // inlined base destructor (~FClientEquip).
        CliEffectCompanionClear(Companion);
    }

    void FCliAttachedFxEquipFx::notify(Event Code, void* /*Param*/)
    {
        // LAB_00532460: code 0xa only.
        if (Code == EquipDestroying)
        {
            CliEffectListRemove(&Host->EffectList, Companion);
            Equip->NotifyDisconnecting(this);
            Equip = nullptr;
        }
    }

    bool FCliAttachedFxEquipFx::Update(float /*Dt*/)
    {
        // FUN_00532490. A live equipment with a particle whose CliEQAttachedFX has use_throttle,
        // on a ship host: the particle's intensity follows CShip::Throttle (+0x29c).
        if (Equip != nullptr && !Equip->IsDestroyed() && Companion[2] != nullptr)
        {
            Archetype::ICliObj* const Comp = (Equip != nullptr) ? Equip->Arch->Companion : nullptr;
            if (CompanionAsAttachedFX(Comp)->bUseThrottle && Host != nullptr &&
                Host->Object != nullptr &&
                (static_cast<unsigned>(Host->Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            {
                CShip const* const Ship = static_cast<CShip const*>(Host->Object);
                FEffectBase* const Effect = static_cast<FEffectBase*>(Companion[2]);
                Effect->SetIntensity(Ship->Throttle);
            }
        }
        return Equip != nullptr;
    }

    FCliAttachedFxEquipFx* CliAttachedFxEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        // The factory's attached-FX arm (FUN_00533160): operator new(0x20), FUN_00532210.
        return new FCliAttachedFxEquipFx(Equip, Host);
    }

    FCliInternalFxEquipFx* CliInternalFxEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        // FUN_00532530, with the base (FUN_00531a30) inlined.
        FCliInternalFxEquipFx* Wrapper = new (std::nothrow) FCliInternalFxEquipFx();
        if (Wrapper == nullptr)
            return nullptr;
        CliEquipFxBaseFill(Wrapper, Equip, Host);
        Wrapper->Voice = nullptr;

        // A null equipment gives a null companion, read anyway.
        unsigned char const* const Comp = reinterpret_cast<unsigned char const*>(
            Wrapper->Equip != nullptr ? Wrapper->Equip->Arch->Companion : nullptr);
        ID_String const* const SoundId = reinterpret_cast<ID_String const*>(Comp + 0x54);
        if (SoundId->IsEmpty())
            return Wrapper;
        void* const SoundArch = LookupClientSoundArch(SoundId);
        if (SoundArch == nullptr)
            return Wrapper;

        // A Source3D; a failed allocation stores null and is used anyway.
        ISoundSource* const Voice = static_cast<ISoundSource*>(CreateClientSoundSource3D());
        Wrapper->Voice = Voice;
        Voice->SetLooping(1);
        Transform IdentityXform;
        std::memset(&IdentityXform, 0, sizeof(IdentityXform));
        IdentityXform.Rotation.E[0][0] = 1.0f;
        IdentityXform.Rotation.E[1][1] = 1.0f;
        IdentityXform.Rotation.E[2][2] = 1.0f;
        Voice->SetPose(static_cast<int>(Host->get_index()),
                       &IdentityXform);
        Voice->Start(SoundArch, 0);
        return Wrapper;
    }

    FCliEngineEquipFx::FCliEngineEquipFx(CEquip* InEquip, FClientGameobj* InHost)
    {
        // FUN_00527b80. Its EH states unwind the nozzle vector and then the base, as a real
        // constructor does.
        FCliEngineEquipFx* const Wrapper = this;
        CliEquipFxBaseFill(Wrapper, InEquip, InHost);
        // Retail copies a stray stack byte into the nozzle vector's allocator byte (+0x10);
        // nothing reads it.
        Wrapper->FxThrottle = 0.0f;
        Wrapper->ThrottleSteadyTime = 0.0f;
        Wrapper->CruiseTime = 0.0f;
        Wrapper->AttenMod = 0.0f;
        Wrapper->CruiseSpeed = 0.0f;
        Wrapper->CruiseState = 0;
        for (int I = 0; I < 9; ++I)
            Wrapper->SoundEmitters[I] = nullptr;
        Wrapper->TrailEffect = nullptr;

        // The host's CShip (null otherwise -- get_speed is called on it regardless).
        CShip* Ship = nullptr;
        if (Wrapper->Host != nullptr && Wrapper->Host->Object != nullptr &&
            (static_cast<unsigned>(Wrapper->Host->Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
        {
            Ship = static_cast<CShip*>(Wrapper->Host->Object);
        }
        Wrapper->CruiseSpeed = Ship->get_speed(1.0f, ThrustEquipEngine);
        // With game option 0x2d ("[AUDIO]") at 2, the thruster speed replaces it.
        if (GetGameOption(kGameOptAudioGroup) == 2)
        {
            CShip* ThrusterShip = nullptr;
            if (Wrapper->Host != nullptr && Wrapper->Host->Object != nullptr &&
                (static_cast<unsigned>(Wrapper->Host->Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            {
                ThrusterShip = static_cast<CShip*>(Wrapper->Host->Object);
            }
            Wrapper->CruiseSpeed = ThrusterShip->get_speed(1.0f, ThrustEquipThruster);
        }

        // The companion's +0xa8 (a null equipment gives a null companion, read anyway); the
        // found arch is referenced with no null test.
        unsigned char const* const CompBytes = reinterpret_cast<unsigned char const*>(
            Wrapper->Equip != nullptr ? Wrapper->Equip->Arch->Companion : nullptr);
        ID_String const* const BackfireId = reinterpret_cast<ID_String const*>(CompBytes + 0xa8);
        if (!BackfireId->IsEmpty())
        {
            Wrapper->TrailEffect = FindEffectArch(&BackfireId->Id);
            Wrapper->TrailEffect->AddRef(0);
        }

        CliEngineAttachExhaustEffects(Wrapper);
        CliEngineSoundInit(Wrapper);
    }

    FCliEngineEquipFx* CliEngineEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        // operator new(0x60) (null on failure, tested), then FUN_00527b80.
        return new (std::nothrow) FCliEngineEquipFx(Equip, Host);
    }

    FCliTradeLaneEquipFx* CliTradeLaneEquipFxConstruct(CEquip* Equip, FClientGameobj* Host)
    {
        // FUN_00516c30 — TradeLane 0x800000. Wires FUN_00516af0 / FUN_00516d10 / FUN_0040c690.
        FCliTradeLaneEquipFx* Wrapper = new (std::nothrow) FCliTradeLaneEquipFx();
        if (Wrapper == nullptr)
            return nullptr;
        CliEquipFxBaseFill(Wrapper, Equip, Host);
        // The speed starts at CliEQTradeLane +0xac (spin_max), read with no test. The channels'
        // owners are left for FUN_00516af0.
        Wrapper->TlrLeft.Owner = nullptr;
        Wrapper->TlrRight.Owner = nullptr;
        Wrapper->SpinSpeed = 0.0f;
        Wrapper->SpinAngle = 0.0f;
        Wrapper->ShipsUsingLane = 0;
        Wrapper->TlrLeft.ChildIndex = -1;
        Wrapper->TlrRight.ChildIndex = -1;
        Wrapper->SpinSpeed = *reinterpret_cast<float const*>(
            reinterpret_cast<unsigned char const*>(Equip->Arch->Companion) + 0xac);
        CliTradeLaneFindTlrHardpoints(Wrapper);
        CliTradeLaneAttachLaneEffects(Wrapper);
        // FUN_0040c690 on the companion, no test.
        Archetype::CliTradeLaneAddEffectRef(Equip->EquipArch()->Companion);
        return Wrapper;
    }

    // DAT_005ca23c.
    float const kTradeLaneFullTurn = 360.0f;

    bool FCliTradeLaneEquipFx::Update(float Dt)
    {
        // LAB_00516ea0. Nothing without the TRADE_LANES option.
        if (GetGameOption(kGameOptTradeLanes) != 0)
        {
            // The companion is read with no test. No ship on the lane: speed up, capped at
            // spin_max; otherwise slow down, floored at 0 (NaN skips either clamp). The compare
            // is on the unrounded value.
            unsigned char const* const Comp =
                reinterpret_cast<unsigned char const*>(Equip->Arch->Companion);
            float const SpinAccel = *reinterpret_cast<float const*>(Comp + 0xa8);
            float const SpinMax = *reinterpret_cast<float const*>(Comp + 0xac);
            double const Step = static_cast<double>(SpinAccel) * static_cast<double>(Dt);
            if (ShipsUsingLane == 0)
            {
                double const Speed = static_cast<double>(SpinSpeed) + Step;
                SpinSpeed = static_cast<float>(Speed);
                if (Speed > static_cast<double>(SpinMax))
                    SpinSpeed = SpinMax;
            }
            else
            {
                double const Speed = static_cast<double>(SpinSpeed) - Step;
                SpinSpeed = static_cast<float>(Speed);
                if (Speed < 0.0)
                    SpinSpeed = 0.0f;
            }

            // Wrapped by repeated subtraction on the unrounded value, stored once at the end.
            double Angle = static_cast<double>(Dt) * static_cast<double>(SpinSpeed) +
                           static_cast<double>(SpinAngle);
            SpinAngle = static_cast<float>(Angle);
            if (Angle > static_cast<double>(kTradeLaneFullTurn))
            {
                do
                {
                    Angle = Angle - static_cast<double>(kTradeLaneFullTurn);
                } while (Angle > static_cast<double>(kTradeLaneFullTurn));
                SpinAngle = static_cast<float>(Angle);
            }

            // Each bound joint channel is registered for this frame.
            if (TlrLeft.ChildIndex != -1)
                AnimDB::Add(&TlrLeft);
            if (TlrRight.ChildIndex != -1)
                AnimDB::Add(&TlrRight);
        }
        return true;
    }

    // The derived destructors that carry work of their own; ~FClientEquip (FUN_00531af0) runs
    // after each and releases the separation explosion (FUN_00536170).
    FCliInternalFxEquipFx::~FCliInternalFxEquipFx()
    {
        // FUN_00532740; then the inlined base destructor (~FClientEquip).
        if (Voice != nullptr)
        {
            Voice->Stop();
            Voice->Release();
            Voice = nullptr;
        }
    }

    void FCliInternalFxEquipFx::notify(Event Code, void* /*Param*/)
    {
        // LAB_005327d0: code 0xa only; no test of the equipment. The base FUN_00531b20 is not
        // called (its body is what the tail repeats).
        if (Code == EquipDestroying)
        {
            if (Voice != nullptr)
                Voice->Stop();
            Equip->NotifyDisconnecting(this);
            Equip = nullptr;
        }
    }

    // DAT_006753e8, filled by the static initializer LAB_005298a0.
    unsigned const G_EngineDestroyedId = CreateID("engine_destroyed");

    // Vtable +4 (the effect's kill) on a nozzle effect.
    void CliEngineKillParticle(FParticleEffect* Effect)
    {
        Effect->Kill();
    }

    // FUN_005298c0 (on the reference).
    void CliEngineReleaseParticleRef(FParticleEffect*& Ref)
    {
        if (Ref != nullptr)
        {
            Ref->Release();
            Ref = nullptr;
        }
    }

    // FUN_00528d00: the nozzle slot's destructor -- the cruise reference (+8), then the trail
    // reference (+4), each through the FUN_005298c0 body.
    void CliEngineNozzleSlotDestruct(FCliEngineNozzleSlot* Slot)
    {
        CliEngineReleaseParticleRef(Slot->CruiseFx);
        CliEngineReleaseParticleRef(Slot->TrailFx);
    }

    // FUN_00529920 (on the vector at +0x10): erase(begin, end). The copy-down loop
    // over [end, end) moves nothing, so what is left is the element destructor on every slot
    // and End = Begin.
    void CliEngineClearNozzles(FCliEngineEquipFx* Wrapper)
    {
        FCliEngineNozzleVector& Vec = Wrapper->Nozzles;
        for (FCliEngineNozzleSlot* Slot = Vec.First(); Slot != Vec.Last(); ++Slot)
            CliEngineNozzleSlotDestruct(Slot);
        Vec.Slots.clear();
    }

    FCliEngineNozzleVector::~FCliEngineNozzleVector()
    {
        // FUN_005298e0 (also the inlined tail of FUN_00527d10): every slot destroyed; the buffer
        // itself goes with the std::vector member.
        for (FCliEngineNozzleSlot* Slot = First(); Slot != Last(); ++Slot)
            CliEngineNozzleSlotDestruct(Slot);
    }

    // FUN_005288a0: notify 0xb passes 0, 0xc passes 1.
    void CliEngineSetActiveSounds(FCliEngineEquipFx* Wrapper, unsigned char bDeactivating)
    {
        ISoundSource** const Snd = Wrapper->SoundEmitters;
        if (bDeactivating != 0)
        {
            // Stop rumble, character loop and character start.
            if (Snd[0] != nullptr)
                Snd[0]->Stop();
            if (Snd[2] != nullptr)
                Snd[2]->Stop();
            if (Snd[1] != nullptr)
                Snd[1]->Stop();
            // A cruise stop sound still playing ends here; otherwise the kill sound restarts.
            if (Snd[6] != nullptr && Snd[6]->IsActive())
                return;
            if (Snd[3] == nullptr)
                return;
            CliEngineRestartEmitter(Snd[3]);
            return;
        }
        // Stop the kill sound, start rumble and character loop (no IsActive test), restart
        // the character start sound.
        if (Snd[3] != nullptr)
            Snd[3]->Stop();
        if (Snd[0] != nullptr)
            CliEngineStartEmitter(Snd[0]);
        if (Snd[2] != nullptr)
            CliEngineStartEmitter(Snd[2]);
        if (Snd[1] == nullptr)
            return;
        CliEngineRestartEmitter(Snd[1]);
    }

    // FUN_00528560: notify 0xe, the new cruise state. The old one is +0x34.
    void CliEngineSetCruiseState(FCliEngineEquipFx* Wrapper, int State)
    {
        ISoundSource** const Snd = Wrapper->SoundEmitters;
        // Above 3 (unsigned) takes the common tail.
        switch (static_cast<unsigned>(State))
        {
        case 0:
        {
            // Only from state 3 or 2; anything else is the common tail.
            int const Old = Wrapper->CruiseState;
            if (Old != 3 && Old != 2)
                break;
            if (Old == 3)
            {
                // Cruise stop restarted, cruise loop stopped.
                if (Snd[6] != nullptr)
                    CliEngineRestartEmitter(Snd[6]);
                if (Snd[5] != nullptr)
                    Snd[5]->Stop();
            }
            else
            {
                if (Snd[4] != nullptr)
                    Snd[4]->Stop();
            }
            // Rumble and character loop started when idle.
            if (Snd[0] != nullptr && !Snd[0]->IsActive())
                CliEngineStartEmitter(Snd[0]);
            if (Snd[2] != nullptr && !Snd[2]->IsActive())
                CliEngineStartEmitter(Snd[2]);
            break;
        }
        case 1:
        {
            if (Wrapper->CruiseState == 3)
            {
                // Cruise loop stopped, disrupt restarted.
                if (Snd[5] != nullptr)
                    Snd[5]->Stop();
                if (Snd[7] != nullptr)
                    CliEngineRestartEmitter(Snd[7]);
            }
            else
            {
                // Cruise start stopped, backfire restarted.
                if (Snd[4] != nullptr)
                    Snd[4]->Stop();
                if (Snd[8] != nullptr)
                    CliEngineRestartEmitter(Snd[8]);
            }
            if (Snd[0] != nullptr && !Snd[0]->IsActive())
                CliEngineStartEmitter(Snd[0]);
            if (Snd[2] != nullptr && !Snd[2]->IsActive())
                CliEngineStartEmitter(Snd[2]);
            // With a backfire effect, one one-shot per nozzle whose hardpoint resolves (one
            // frame shared by the loop), no companion. The nozzle vector is read without a
            // class test.
            if (Wrapper->TrailEffect == nullptr)
                break;
            Transform Xform;
            Xform.Rotation.E[0][0] = 1.0f;
            Xform.Rotation.E[0][1] = 0.0f;
            Xform.Rotation.E[0][2] = 0.0f;
            Xform.Rotation.E[1][0] = 0.0f;
            Xform.Rotation.E[1][1] = 1.0f;
            Xform.Rotation.E[1][2] = 0.0f;
            Xform.Rotation.E[2][0] = 0.0f;
            Xform.Rotation.E[2][1] = 0.0f;
            Xform.Rotation.E[2][2] = 1.0f;
            Xform.Translation.x = 0.0f;
            Xform.Translation.y = 0.0f;
            Xform.Translation.z = 0.0f;
            std::vector<ExhaustNozzleInfo> const& Nozzles =
                static_cast<CEEngine*>(Wrapper->Equip)->NozzleInfos();
            for (std::vector<ExhaustNozzleInfo>::const_iterator Cur = Nozzles.begin();
                 Cur != Nozzles.end(); ++Cur)
            {
                if (Cur->GetHardpointOffset_NS(Xform))
                {
                    CliCreateEffectOneShot(Wrapper->TrailEffect, Wrapper->Host,
                                           static_cast<int>(Cur->ParentInstance),
                                           &Xform, nullptr);
                }
            }
            break;
        }
        case 2:
            // Stop rumble, character start, character loop and kill; restart cruise start.
            if (Snd[0] != nullptr)
                Snd[0]->Stop();
            if (Snd[1] != nullptr)
                Snd[1]->Stop();
            if (Snd[2] != nullptr)
                Snd[2]->Stop();
            if (Snd[3] != nullptr)
                Snd[3]->Stop();
            if (Snd[4] != nullptr)
                CliEngineRestartEmitter(Snd[4]);
            break;
        case 3:
            // The same four stops, the cruise loop started with no IsActive test -- and only the
            // state is stored: this arm returns without the +0x28 reset.
            if (Snd[0] != nullptr)
                Snd[0]->Stop();
            if (Snd[1] != nullptr)
                Snd[1]->Stop();
            if (Snd[2] != nullptr)
                Snd[2]->Stop();
            if (Snd[3] != nullptr)
                Snd[3]->Stop();
            if (Snd[5] != nullptr)
                CliEngineStartEmitter(Snd[5]);
            Wrapper->CruiseState = State;
            return;
        default:
            break;
        }
        // The common tail.
        Wrapper->CruiseTime = 0.0f;
        Wrapper->CruiseState = State;
    }

    // FUN_005296d0: notify 0xd, after a nozzle's effects went. Re-seats every emitter on the
    // average of the nozzles that are left (FUN_00528d70), and drops all of them when none
    // is left and the engine is not indestructible (archetype +0x84, read without a test).
    void CliEngineRepositionSounds(FCliEngineEquipFx* Wrapper)
    {
        float Avg[3] = {0.0f, 0.0f, 0.0f};
        int const Count = CliEngineAvgNozzleLocalPos(Wrapper, Avg);
        if (Count == 0 &&
            !static_cast<Archetype::Engine const*>(Wrapper->Equip->Arch)->bIndestructible)
        {
            CliEngineReleaseSoundEmitters(Wrapper);
        }
        // Retail goes on after the release; every emitter is null by then.
        Transform Pose;
        std::memset(&Pose, 0, sizeof(Pose));
        Pose.Rotation.E[0][0] = 1.0f;
        Pose.Rotation.E[1][1] = 1.0f;
        Pose.Rotation.E[2][2] = 1.0f;
        Pose.Translation.x = Avg[0];
        Pose.Translation.y = Avg[1];
        Pose.Translation.z = Avg[2];
        // The host's index is read per emitter.
        for (int Index = 0; Index < 9; ++Index)
        {
            ISoundSource* const Emitter = Wrapper->SoundEmitters[Index];
            if (Emitter != nullptr)
            {
                Emitter->SetPose(static_cast<int>(Wrapper->Host->get_index()),
                                 &Pose);
            }
        }
    }

    void FCliEngineEquipFx::notify(Event Code, void* Param)
    {
        // FUN_00527e70: codes 0xa..0xe, then the base notify with both arguments on every path.
        switch (Code)
        {
        case EquipDestroying:
        {
            // Per nozzle, both effects killed and released.
            for (FCliEngineNozzleSlot* Slot = Nozzles.First(); Slot != Nozzles.Last(); ++Slot)
            {
                if (Slot->TrailFx != nullptr)
                {
                    CliEngineKillParticle(Slot->TrailFx);
                    if (Slot->TrailFx != nullptr)
                    {
                        Slot->TrailFx->Release();
                        Slot->TrailFx = nullptr;
                    }
                }
                if (Slot->CruiseFx != nullptr)
                {
                    CliEngineKillParticle(Slot->CruiseFx);
                    CliEngineReleaseParticleRef(Slot->CruiseFx);
                }
            }
            CliEngineClearNozzles(this);
            CliEngineReleaseSoundEmitters(this);
            if (CliEquipHostIsLivePlayer(Host, CObject::ClassShip))
                NnVoiceEnqueue(G_EngineDestroyedId, 0);
            break;
        }
        case EquipActivating:
            CliEngineSetActiveSounds(this, 0);
            break;
        case EquipDeactivating:
            CliEngineSetActiveSounds(this, 1);
            break;
        case ArchGroupDestroyed:
        {
            // The slots whose sub-object id is the low word of Param lose both effects.
            unsigned short const SubObjId =
                static_cast<unsigned short>(reinterpret_cast<uintptr_t>(Param));
            for (FCliEngineNozzleSlot* Slot = Nozzles.First(); Slot != Nozzles.Last(); ++Slot)
            {
                if (Slot->SubObjId != SubObjId)
                    continue;
                if (Slot->TrailFx != nullptr)
                {
                    CliEngineKillParticle(Slot->TrailFx);
                    if (Slot->TrailFx != nullptr)
                    {
                        Slot->TrailFx->Release();
                        Slot->TrailFx = nullptr;
                    }
                }
                if (Slot->CruiseFx != nullptr)
                {
                    CliEngineKillParticle(Slot->CruiseFx);
                    if (Slot->CruiseFx != nullptr)
                    {
                        Slot->CruiseFx->Release();
                        Slot->CruiseFx = nullptr;
                    }
                }
            }
            CliEngineRepositionSounds(this);
            break;
        }
        case EngineCruiseStateChanged:
            CliEngineSetCruiseState(this, *static_cast<int const*>(Param));
            break;
        default:
            break;
        }
        FClientEquip::notify(Code, Param);
    }

    FCliEngineEquipFx::~FCliEngineEquipFx()
    {
        // FUN_00527d10. Per nozzle, trail then cruise: kill, then -- re-read -- release.
        for (FCliEngineNozzleSlot* Slot = Nozzles.First(); Slot != Nozzles.Last(); ++Slot)
        {
            if (Slot->TrailFx != nullptr)
            {
                CliEngineKillParticle(Slot->TrailFx);
                if (Slot->TrailFx != nullptr)
                {
                    Slot->TrailFx->Release();
                    Slot->TrailFx = nullptr;
                }
            }
            if (Slot->CruiseFx != nullptr)
            {
                CliEngineKillParticle(Slot->CruiseFx);
                if (Slot->CruiseFx != nullptr)
                {
                    Slot->CruiseFx->Release();
                    Slot->CruiseFx = nullptr;
                }
            }
        }
        // The inlined erase(begin, end).
        CliEngineClearNozzles(this);
        CliEngineReleaseSoundEmitters(this);
        // The pointer is left as it is.
        if (TrailEffect != nullptr)
            TrailEffect->Release();
        // The vector's destructor (FUN_005298e0 inlined) runs as the Nozzles member's after this
        // body; ~FClientEquip (FUN_00531af0) follows it.
    }

    // Retail Archetype::CliEQEngine (vtable 005c7ac4, 0xac) is private to client_cliobj.cpp
    // (FCliEngine); these are the int ranges FUN_00528240 reads off it.
    struct FCliEQEngineRangesView
    {
        int RumblePitchRange[2];      // +0x64 "rumble_pitch_range"
        int RumbleAttenRange[2];      // +0x6c "rumble_atten_range"
        unsigned CharacterSounds[2];  // +0x74 / +0x78 (ID_Strings, not read here)
        int CharacterPitchRange[2];   // +0x7c "character_pitch_range"
    };
    static_assert(sizeof(FCliEQEngineRangesView) == 0x84 - 0x64, "CliEQEngine +0x64..+0x84");

    FCliEQEngineRangesView const* CompanionAsEngineRanges(Archetype::ICliObj* Companion)
    {
        return reinterpret_cast<FCliEQEngineRangesView const*>(
            reinterpret_cast<unsigned char const*>(Companion) + 0x64);
    }

    // DAT_005c75dc / DAT_005c75f8.
    float const kEngineFxFull = 1.0f;
    double const kEngineRumbleMinSpeed = 0.001;

    bool FCliEngineEquipFx::Update(float Dt)
    {
        // LAB_00528020. Nothing but the base's answer without an equipment.
        if (Equip != nullptr)
        {
            CShip const* Ship = nullptr;
            if (Host != nullptr && Host->Object != nullptr &&
                (static_cast<unsigned>(Host->Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            {
                Ship = static_cast<CShip const*>(Host->Object);
            }
            CEEngine const* const Engine = static_cast<CEEngine const*>(Equip);

            // The target throttle: 0 while the engine is off, 1 in cruise, else the ship's
            // throttle (read with no test of the ship).
            double Target = 0.0;
            if (Equip->IsActive())
            {
                if (Engine->bCruiseActive != 0)
                    Target = static_cast<double>(kEngineFxFull);
                else
                    Target = static_cast<double>(Ship->Throttle);
            }

            // Step FxThrottle toward the target by at most Dt * MAX_DELTA_FX_THROTTLE. The
            // difference and the step are stored as floats; the compare uses the unrounded step.
            // Otherwise the sign of the difference (NaN subtracts) picks FxThrottle +/- step.
            float const Diff =
                static_cast<float>((Target - static_cast<double>(FxThrottle)) * static_cast<double>(Dt));
            double const StepX87 = static_cast<double>(Dt) *
                                   static_cast<double>(EngineEquipConsts::MAX_DELTA_FX_THROTTLE);
            float const Step = static_cast<float>(StepX87);
            float NewThrottle = 0.0f;
            if (StepX87 > static_cast<double>(std::fabs(Diff)))
                NewThrottle = static_cast<float>(Target);
            else if (Diff > 0.0f)
                NewThrottle = static_cast<float>(static_cast<double>(Step) + static_cast<double>(FxThrottle));
            else
                NewThrottle = static_cast<float>(static_cast<double>(FxThrottle) - static_cast<double>(Step));

            // Unchanged counts the steady time up by Dt; any change (or NaN) resets it.
            if (NewThrottle == FxThrottle)
                ThrottleSteadyTime = static_cast<float>(static_cast<double>(Dt) + static_cast<double>(ThrottleSteadyTime));
            else
                ThrottleSteadyTime = 0.0f;

            // The exhaust intensity. In cruise with charge time elapsed and an archetype
            // cruise_charge_time > 0 it blends toward 1:
            // New * MAX_ENGINE_FX_THROTTLE + (elapsed / charge) * (1 - MAX_ENGINE_FX_THROTTLE);
            // charge time 0 gives New itself; otherwise New * MAX_ENGINE_FX_THROTTLE.
            float Intensity = 0.0f;
            double const MaxFx = static_cast<double>(EngineEquipConsts::MAX_ENGINE_FX_THROTTLE);
            if (Engine->bCruiseActive != 0)
            {
                float const Elapsed = Engine->CruiseChargeTimeElapsed;
                if (Elapsed > 0.0f)
                {
                    float const Charge = Engine->EngineArch()->CruiseChargeTime;
                    if (Charge > 0.0f)
                    {
                        double const Blend =
                            static_cast<double>(NewThrottle) * MaxFx +
                            (static_cast<double>(Elapsed) / static_cast<double>(Charge)) *
                                (static_cast<double>(kEngineFxFull) - MaxFx);
                        Intensity = static_cast<float>(Blend);
                    }
                    else
                    {
                        Intensity = NewThrottle;
                    }
                }
                else
                {
                    Intensity = static_cast<float>(static_cast<double>(NewThrottle) * MaxFx);
                }
            }
            else
            {
                Intensity = static_cast<float>(static_cast<double>(NewThrottle) * MaxFx);
            }

            // Every nozzle particle gets the intensity, floored at 0 (NaN gives 0).
            for (FCliEngineNozzleSlot* Slot = Nozzles.First(); Slot != Nozzles.Last(); ++Slot)
            {
                if (Slot->TrailFx != nullptr)
                {
                    float const Value = (Intensity > 0.0f) ? Intensity : 0.0f;
                    FParticleEffect* const Effect = Slot->TrailFx;
                    Effect->SetIntensity(Value);
                }
                if (Slot->CruiseFx != nullptr)
                {
                    float const Value = (Intensity > 0.0f) ? Intensity : 0.0f;
                    FParticleEffect* const Effect = Slot->CruiseFx;
                    Effect->SetIntensity(Value);
                }
            }

            UpdateSound(NewThrottle, Dt);
            FxThrottle = NewThrottle;
        }
        return FClientEquip::Update(Dt);
    }

    void FCliEngineEquipFx::UpdateSound(float Throttle, float Dt)
    {
        // FUN_00528240. Nothing while the engine is off (no test of Equip).
        if (!Equip->IsActive())
            return;
        CEEngine const* const Engine = static_cast<CEEngine const*>(Equip);
        ISoundSource* const Rumble = SoundEmitters[0];         // +0x38 rumble_sound
        ISoundSource* const CharacterLoop = SoundEmitters[2];  // +0x40 character_loop_sound
        ISoundSource* const CruiseLoop = SoundEmitters[5];     // +0x4c cruise_loop_sound

        if (Engine->bCruiseActive == 0)
        {
            if (Rumble != nullptr)
            {
                // The rumble plays only above 0.001 (unrounded; NaN stops it) and off a
                // tradelane.
                bool bUsingTradelane = false;
                Host->is_using_tradelane(&bUsingTradelane);
                Vector const Velocity = Host->get_velocity();
                double const SpeedX87 = std::sqrt(static_cast<double>(Velocity.x) * Velocity.x +
                                                  static_cast<double>(Velocity.y) * Velocity.y +
                                                  static_cast<double>(Velocity.z) * Velocity.z);
                float const Speed = static_cast<float>(SpeedX87);
                if (SpeedX87 > kEngineRumbleMinSpeed && !bUsingTradelane)
                {
                    // An idle rumble restarts on its stashed archetype.
                    if (Rumble->IsActive() == 0)
                        Rumble->Start(GetClientEquipSoundEmitterArch(Rumble), 0);

                    // Speed over the cruise speed; with the [AUDIO] option at 2, capped at 1
                    // (NaN takes 1).
                    float Ratio = static_cast<float>(static_cast<double>(Speed) /
                                                     static_cast<double>(CruiseSpeed));
                    if (GetGameOption(kGameOptAudioGroup) == 2 && !(Ratio < kEngineFxFull))
                        Ratio = kEngineFxFull;

                    // Pitch and attenuation interpolated across the companion's int ranges.
                    FCliEQEngineRangesView const* const Ranges =
                        CompanionAsEngineRanges((Equip != nullptr) ? Equip->Arch->Companion : nullptr);
                    int const PitchSpan = Ranges->RumblePitchRange[1] - Ranges->RumblePitchRange[0];
                    Rumble->SetPitch(static_cast<float>(
                        static_cast<double>(PitchSpan) * static_cast<double>(Ratio) +
                        static_cast<double>(Ranges->RumblePitchRange[0])));
                    int const AttenSpan = Ranges->RumbleAttenRange[1] - Ranges->RumbleAttenRange[0];
                    Rumble->SetAttenuation(static_cast<float>(
                        static_cast<double>(AttenSpan) * static_cast<double>(Ratio) +
                        static_cast<double>(Ranges->RumbleAttenRange[0])));
                }
                else if (Rumble->IsActive() != 0)
                {
                    Rumble->Stop();
                }
            }

            // The character loop; without one, nothing more.
            if (CharacterLoop == nullptr)
                return;
            if (CharacterLoop->IsActive() == 0)
                CharacterLoop->Start(GetClientEquipSoundEmitterArch(CharacterLoop), 0);
            if (CharacterLoop->IsActive() != 0)
            {
                FCliEQEngineRangesView const* const Ranges =
                    CompanionAsEngineRanges((Equip != nullptr) ? Equip->Arch->Companion : nullptr);
                int const PitchSpan =
                    Ranges->CharacterPitchRange[1] - Ranges->CharacterPitchRange[0];
                CharacterLoop->SetPitch(static_cast<float>(
                    static_cast<double>(PitchSpan) * static_cast<double>(Throttle) +
                    static_cast<double>(Ranges->CharacterPitchRange[0])));
            }

            // The attenuation offset. Before THROTTLE_STEADY_TIME (NaN counts as steady) it
            // climbs by Dt * DELTA_THROTTLE_ATTEN_MOD_CHANGING, capped at 0; once steady it moves
            // by Dt * DELTA_THROTTLE_ATTEN_MOD_STEADY, floored at -THROTTLE_ATTEN_MOD_RANGE. The
            // stored value is rounded twice.
            double Mod = 0.0;
            if (ThrottleSteadyTime < EngineEquipConsts::THROTTLE_STEADY_TIME)
            {
                Mod = static_cast<double>(Dt) *
                          static_cast<double>(EngineEquipConsts::DELTA_THROTTLE_ATTEN_MOD_CHANGING) +
                      static_cast<double>(AttenMod);
                AttenMod = static_cast<float>(Mod);
                if (0.0 < Mod)
                    Mod = 0.0;
            }
            else
            {
                Mod = static_cast<double>(Dt) *
                          static_cast<double>(EngineEquipConsts::DELTA_THROTTLE_ATTEN_MOD_STEADY) +
                      static_cast<double>(AttenMod);
                AttenMod = static_cast<float>(Mod);
                double const Floor = -static_cast<double>(EngineEquipConsts::THROTTLE_ATTEN_MOD_RANGE);
                if (Floor > Mod)
                    Mod = Floor;
            }
            AttenMod = static_cast<float>(Mod);
            CharacterLoop->SetAttenuation(AttenMod);
            return;
        }

        // In cruise, a playing cruise loop counts CruiseTime up by Dt; past CRUISE_STEADY_TIME
        // (NaN keeps 0) its attenuation is
        // (CruiseTime - CRUISE_STEADY_TIME) * DELTA_CRUISE_ATTEN_MOD_STEADY, floored at
        // -CRUISE_ATTEN_MOD_RANGE; before that 0.
        if (CruiseLoop != nullptr && CruiseLoop->IsActive() != 0)
        {
            double const Time = static_cast<double>(Dt) + static_cast<double>(CruiseTime);
            CruiseTime = static_cast<float>(Time);
            float Atten = 0.0f;
            if (Time > static_cast<double>(EngineEquipConsts::CRUISE_STEADY_TIME))
            {
                Atten = static_cast<float>(
                    (Time - static_cast<double>(EngineEquipConsts::CRUISE_STEADY_TIME)) *
                    static_cast<double>(EngineEquipConsts::DELTA_CRUISE_ATTEN_MOD_STEADY));
                double const Floor = -static_cast<double>(EngineEquipConsts::CRUISE_ATTEN_MOD_RANGE);
                if (Floor > static_cast<double>(Atten))
                    Atten = static_cast<float>(Floor);
            }
            CruiseLoop->SetAttenuation(Atten);
            return;
        }
        CruiseTime = 0.0f;
    }

    FCliTradeLaneEquipFx::~FCliTradeLaneEquipFx()
    {
        // FUN_00516bc0: FUN_0040c7d0, the release matching the constructor's FUN_0040c690. No
        // null tests in retail.
        Archetype::CliTradeLaneReleaseEffectRef(Equip->EquipArch()->Companion);
    }

    // ClientEquip slot 1 with 1 (the scalar deleting destructor), as FUN_00533080 calls it.
    void DeleteEquipFxWrapper(FClientEquip* Wrapper)
    {
        delete Wrapper;
    }

    void EquippedObjScanFuseIgnitions(FClientGameobj* Host)
    {
        // The fuse block of FUN_00533e10: the hit-point threshold walks that ignite fuses, and
        // Fuse::Load on each prototype. The host's object narrowed by 0x103 (null otherwise, then
        // read anyway); its root fuse list at archetype +0x78.
        CObject* const Obj = ResolveEqObjFromHost(Host);
        Archetype::EqObj const* const EqArch =
            static_cast<Archetype::EqObj const*>(Obj->get_archetype());
        if (EqArch->Fuses != nullptr)
        {
            std::vector<Archetype::FGroupFuse> const& List = *EqArch->Fuses;
            for (size_t Index = 0; Index < List.size(); ++Index)
            {
                Archetype::FGroupFuse const& Node = List[Index];
                // Hit points re-read per node; hp <= Value1 ignites (NaN does not). The host's
                // own Ignite slot (a ship runs FUN_00545a90).
                if (static_cast<CSimple*>(Obj)->get_hit_pts() <= Node.Value1)
                {
                    static_cast<FClientEquippedObj*>(Host)->LightFuse(0u, &Node.Fuse, SUBOBJ_ID_ROOT,
                                                                   Node.Value2, -1.0f);
                }
                // FUN_00432240 find in DAT_00675400.
                ClientFuse* Proto = ClientFuseDB::Find(Node.Fuse.Id);
                if (Proto != nullptr)
                    Proto->Load();
            }
        }

        CEqObj* Eq = static_cast<CEqObj*>(Obj);
        CArchGrpTraverser Trav;
        CArchGroup* Group = Eq->ArchGroups.Traverse(Trav);
        while (Group != nullptr)
        {
            Archetype::FuseIgnitionList const& FuseList = Group->FuseIgnitions();
            if (FuseList.Items != nullptr)
            {
                std::vector<Archetype::FGroupFuse> const& Items = *FuseList.Items;
                for (size_t Index = 0; Index < Items.size(); ++Index)
                {
                    Archetype::FGroupFuse const& Node = Items[Index];
                    // As above, on the group's hit points and its arch id (no test).
                    if (Group->GetHitPoints() <= Node.Value1)
                    {
                        static_cast<FClientEquippedObj*>(Host)->LightFuse(
                            0u, &Node.Fuse, Group->Arch->Id, Node.Value2, -1.0f);
                    }
                    // FUN_0052c7a0 (a find) in DAT_00675400.
                    ClientFuse* Proto = ClientFuseDB::Find(Node.Fuse.Id);
                    if (Proto != nullptr)
                        Proto->Load();
                }
            }
            Group = Eq->ArchGroups.Traverse(Trav);
        }
    }

}

void FClientGameobj::ClearEquipFx()
{
    // FUN_00533080.
    FCliEquipFxListNode* Sentinel = EquipFxSentinelRef(this);
    if (Sentinel == nullptr)
        return;
    for (FCliEquipFxListNode* Node = Sentinel->Next; Node != nullptr && Node != Sentinel;
         Node = Node->Next)
    {
        if (Node->Wrapper != nullptr)
        {
            DeleteEquipFxWrapper(Node->Wrapper);
            Node->Wrapper = nullptr;
        }
    }
    FCliEquipFxListNode* Cur = Sentinel->Next;
    while (Cur != nullptr && Cur != Sentinel)
    {
        FCliEquipFxListNode* Next = Cur->Next;
        if (Cur->Prev != nullptr)
            Cur->Prev->Next = Cur->Next;
        if (Cur->Next != nullptr)
            Cur->Next->Prev = Cur->Prev;
        ::operator delete(Cur);
        EquipFxCountRef(this) = EquipFxCountRef(this) - 1;
        Cur = Next;
    }
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
}

void FClientGameobj::CreateEquipFx(unsigned short EquipId)
{
    // FUN_00533160 — full type switch.
    CEqObj* Eq = ResolveEqObjFromHost(this);
    if (Eq == nullptr)
        return;
    CEquip* Equip = Eq->EquipManager.FindByID(EquipId);
    if (Equip == nullptr)
        return;
    unsigned const Type = Equip->GetType();
    FClientEquip* Wrapper = nullptr;

    if (Type == static_cast<unsigned>(EquipTypeLight))
        Wrapper = CliLightEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeGun))
        Wrapper = CliGunEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeAttachedFX))
        Wrapper = CliAttachedFxEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeEngine))
        Wrapper = CliEngineEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeShieldGen))
        Wrapper = CliShieldGenEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeThruster))
        Wrapper = CliThrusterEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeTractor))
        Wrapper = CliTractorEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeMineDropper))
        Wrapper = CliMineEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeCmDropper))
        Wrapper = CliCmEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeShield))
        Wrapper = CliShieldEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeCloak))
        Wrapper = CliCloakEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeRepairDroid))
        Wrapper = CliRepairDroidEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeInternalFX))
        Wrapper = CliInternalFxEquipFxConstruct(Equip, this);
    else if (Type == static_cast<unsigned>(EquipTypeTradeLane))
        Wrapper = CliTradeLaneEquipFxConstruct(Equip, this);
    else
    {
        return;
    }
    if (Wrapper == nullptr)
        return;
    EquipFxListInsert(this, Wrapper);
}

void FClientGameobj::RebuildEquipFx()
{
    // FUN_00533e10 (returns nothing: EAX is whatever FUN_00539740 left).
    ClearEquipFx();
    // Each step narrows the object by 0x103 again (null otherwise) and traverses its equipment
    // without a test.
    CEqObj* Eq = ResolveEqObjFromHost(this);
    CEquipTraverser Walk;
    CEquip* Equip = Eq->EquipManager.Traverse(Walk);
    while (Equip != nullptr)
    {
        CreateEquipFx(Equip->GetID());
        Equip = Eq->EquipManager.Traverse(Walk);
    }
    EquippedObjScanFuseIgnitions(this);
    EquippedObjResolveExplosion(this);
}

// The CliEquipLights are linked into bucket 0x2e by the owner's effect-list sweep (FUN_004fd040,
// space_scene_obj_draw.cpp GameobjSweepEffectList) and ticked / drawn by the bucket walk
// FUN_004f7810 inside FUN_004f7480, whose FUN_00403ea0 flushes the flares.

// ---- ClientEquip (vtable PTR_FUN_005de13c) -------------------------------------------------

// Retail has no out-of-line default constructor: FUN_00531a30 (the base constructor proper, with
// its CEquip::ConnectClientEquip and separation-explosion reference) is what the wrappers run;
// the port's derived constructors still do that part themselves (CliEquipFxBaseFill,
// CliLauncherFxBaseFill, FCliLightEquipFx's constructor), so this one only nulls the fields.
FClientEquip::FClientEquip()
    : Equip(nullptr)
    , Host(nullptr)
    , SeparationExplosion(nullptr)
{
}

FClientEquip::~FClientEquip()
{
    // FUN_00531af0 (the body of the scalar deleting FUN_00531ab0).
    if (Equip != nullptr)
        Equip->NotifyDisconnecting(this);
    if (SeparationExplosion != nullptr)
        ExplosionArchRelease(SeparationExplosion);
}

bool FClientEquip::Update(float /*Dt*/)
{
    // FUN_00532000.
    return Equip != nullptr;
}

void FClientEquip::notify(Event Code, void* /*Param*/)
{
    // FUN_00531b20: only 0xa, and no test of Equip before the call.
    if (Code == EquipDestroying)
    {
        Equip->NotifyDisconnecting(this);
        Equip = nullptr;
    }
}

// ---- CliTradeLaneEquip access for the solar's lane switch FUN_00516910 ---------------------

int* CliTradeLaneEquipFxShipCount(FClientEquip* Wrapper)
{
    // FUN_00516910 reads and writes [wrapper+0x30] directly.
    return &static_cast<FCliTradeLaneEquipFx*>(Wrapper)->ShipsUsingLane;
}

void CliTradeLaneEquipFxAttachLaneEffects(FClientEquip* Wrapper)
{
    // FUN_00516910 calls FUN_00516d10 on the wrapper when the count returns to 0.
    CliTradeLaneAttachLaneEffects(static_cast<FCliTradeLaneEquipFx*>(Wrapper));
}

void FClientEquip::BaseFill(CEquip* Equip, FClientGameobj* Host)
{
    // FUN_00531a30 for the constructors outside this file (the launcher family).
    CliEquipFxBaseFill(this, Equip, Host);
}

bool RemoveAppearanceInstanceValue(int Key)
{
    // FUN_00532170. "1.11_IFLAppearance" from DALib::Engine.
    IFLAppearance* Ifl = nullptr;
    if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) < 0)
    {
        // Released if anything came back.
        if (Ifl != nullptr)
            Ifl->Release();
        return false;
    }
    bool const bOk = Ifl->RemoveInstanceValue(Key) >= 0;
    if (Ifl != nullptr)
        Ifl->Release();
    return bOk;
}

FClientEquip* FClientGameobj::FindEquipFx(unsigned short EquipId)
{
    // FUN_00533100 (on the EquippedObj). The narrowed object is used without a test.
    CEqObj* const Eq = ResolveEqObjFromHost(this);
    CEquip const* const Equip = Eq->EquipManager.FindByID(EquipId);
    // The first wrapper on the +0xb8 list whose +4 is that equipment.
    FCliEquipFxListNode* const Sentinel = EquipFxSentinelRef(this);
    for (FCliEquipFxListNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        if (Node->Wrapper->Equip == Equip)
            return Node->Wrapper;
    }
    return nullptr;
}

// FUN_00534240 -- its body is FClientEquippedObj::LightFuse (client_equip_fx.cpp).
bool FClientEquippedObj::LightFuse(unsigned SourceId, ID_String const* InFuseId,
                                 unsigned short SubObjId, float AtT, float OverrideLifetime)
{
    // FUN_00534240 (FClientEquippedObj::LightFuse's body).
    return HostIgniteFuseImpl(this, SourceId, *InFuseId, SubObjId, AtT, OverrideLifetime);
}

void CliTractorEquipFxDropLoot(FClientEquip* Wrapper, unsigned LootId, int Units)
{
    // FUN_0052f9a0 (on the tractor wrapper); the caller picks the wrapper with
    // FUN_00533100 by the tractor's equipment id.
    static_cast<FCliTractorEquipFx*>(Wrapper)->DropLoot(LootId, Units);
}

void CliShieldEquipFxCollisionHit(FClientEquip* Wrapper, void const* HitInfo,
                                  FClientGameobj* Other, void const* Unused)
{
    // Wrapper vtable +0x14: LAB_0052e760 on a shield wrapper.
    static_cast<FCliShieldEquipFx*>(Wrapper)->OnCollisionHit(HitInfo, Other, Unused);
}

void CliShieldEquipFxMunitionHit(FClientEquip* Wrapper, void const* HitInfo,
                                 FClientGameobj* Munition)
{
    // Wrapper vtable +0x18: FUN_0052e8c0 on a shield wrapper.
    static_cast<FCliShieldEquipFx*>(Wrapper)->OnMunitionHit(HitInfo, Munition);
}
