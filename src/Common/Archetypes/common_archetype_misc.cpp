// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Archetype::Asteroid, DynamicAsteroid, MotorData, Explosion and the explosion map (retail
// Archetype.cpp).

#include "common_archetype.h"
#include "Archetypes/common_archetype_internal.h"

#include "common_cobject.h" // CollisionGroupDesc (get_undamaged_collision_group_list)
#include "common_fuses.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_phyarch.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "readfile.h"

#include <cstddef>
#include <list>
#include <map>
#include <math.h>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace Archetype
{
    std::map<unsigned, Explosion> GExplosionDb;

    Asteroid::Asteroid(ICliObj* InCompanion)
        : Root(InCompanion)
        , DetectRadius(25.0f)
        , ExplosionOffset(0.0f)
        , RechargeTime(1000.0f)
    {
        ObjectType = 0x20000000;
    }

    bool Asteroid::read(INI_Reader& Reader)
    {
        // No "inherit" case: LoadAsteroids (062fa2c0) handles that key before calling read().
        if (Root::read(Reader))
            return true;

        if (Reader.is_value("detect_radius"))
        {
            DetectRadius = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("explosion_offset"))
        {
            ExplosionOffset = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("recharge_time"))
        {
            RechargeTime = Reader.get_value_float(0);
            return true;
        }

        return false;
    }

    AClassType Asteroid::get_class_type() const
    {
        // Retail 0xfd90.
        return AClassAsteroid;
    }

    DynamicAsteroid::DynamicAsteroid(ICliObj* InCompanion)
        : Root(InCompanion)
    {
        ObjectType = 0x40000000;
    }

    // FUN_062fa0b0
    bool DynamicAsteroid::read(INI_Reader& Reader)
    {
        return Root::read(Reader) != false;
    }

    AClassType DynamicAsteroid::get_class_type() const
    {
        // Retail 0xfe80.
        return AClassDynamicAsteroid;
    }

    MotorData::MotorData()
        : VtableUnused(0)
        , Delay(0.0f)
        , Accel(15.0f)
        , Lifetime(1.5f)
    {
        Nickname.clear();
    }

    void MotorData::redefine(MotorData const& Other)
    {
        // FUN_062f7ba0 (vtable 063a1ecc slot 1; not disassembled by Ghidra). Unequal or
        // unordered copies; the nickname is not touched.
        if (Other.Accel != Accel)
            Accel = Other.Accel;
        if (Other.Lifetime != Lifetime)
            Lifetime = Other.Lifetime;
        if (Other.Delay != Delay)
            Delay = Other.Delay;
    }

    bool MotorData::read(INI_Reader& Reader)
    {
        // FUN_062f7b00. "nickname" uses the no-argument get_value_string (the whole value).
        if (Reader.is_value("nickname"))
        {
            Nickname.Id = CreateID(Reader.get_value_string());
            return true;
        }

        if (Reader.is_value("accel"))
        {
            Accel = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("lifetime"))
        {
            Lifetime = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("delay"))
        {
            Delay = Reader.get_value_float(0);
            return true;
        }

        return false;
    }

    // FUN_0630b730; the field values are exactly reset()'s.
    Explosion::Explosion(ID_String const& InNickname)
        : Nickname(InNickname)
    {
        reset();
    }

    void Explosion::reset()
    {
        Radius = 0.0f;
        Impulse = 0.0f;
        HullDamage = 0.0f;
        EnergyDamage = 0.0f;
        Process = ProcessDisappear;
        Lifetime = 0.0f;
        LifetimeMax = 10000.0f;
        Effects.clear();
        NumChildPieces = 0;
        DebrisImpulse = 5.0f;
        InnardsDebrisStartTime = -1.0f;
        InnardsDebrisNum = 0.0f;
        InnardsDebrisRadius = 0.0f;
        InnardsDebrisObjects.clear();
        DebrisTypes.clear();
    }

    bool Explosion::read(INI_Reader& Reader)
    {
        if (Reader.is_value("radius"))
        {
            Radius = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("impulse"))
        {
            Impulse = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("hull_damage"))
        {
            HullDamage = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("energy_damage"))
        {
            EnergyDamage = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("lifetime"))
        {
            Lifetime = Reader.get_value_float(0);
            if (Reader.is_value_empty(1))
                LifetimeMax = Lifetime;
            else
                LifetimeMax = Reader.get_value_float(1);
            return true;
        }
        if (Reader.is_value("effect"))
        {
            FExplosionEffect Effect;
            Effect.Fx.Id = CreateID(Reader.get_value_string(0));
            if (Reader.is_value_empty(1))
                Effect.Strength = -1.0f;
            else
                Effect.Strength = Reader.get_value_float(1);
            Effects.push_back(Effect);
            return true;
        }
        if (Reader.is_value("process"))
        {
            // Table 063ed218 via DACOM.DLL stricmp; no match leaves the value.
            char const* Text = Reader.get_value_string();
            if (CallDacomStricmp(Text, "none") == 0)
                Process = ProcessNone;
            else if (CallDacomStricmp(Text, "shatter") == 0)
                Process = ProcessShatter;
            else if (CallDacomStricmp(Text, "disappear") == 0)
                Process = ProcessDisappear;
            return true;
        }
        if (Reader.is_value("debris_type"))
        {
            FExplosionDebrisType Debris;
            Debris.Debris.Id = CreateID(Reader.get_value_string(0));
            Debris.Count = 0.0f;
            if (!Reader.is_value_empty(1))
                Debris.Count = (float)Reader.get_value_int(1);
            DebrisTypes.push_back(Debris);
            return true;
        }
        if (Reader.is_value("num_child_pieces"))
        {
            // ftol on the unrounded value.
            NumChildPieces = (int)Reader.value_num(0);
            return true;
        }
        if (Reader.is_value("debris_impulse"))
        {
            DebrisImpulse = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("innards_debris_object"))
        {
            InnardsDebrisObjects.push_back(CreateID(Reader.get_value_string()));
            return true;
        }
        if (Reader.is_value("innards_debris_radius"))
        {
            InnardsDebrisRadius = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("innards_debris_start_time"))
        {
            InnardsDebrisStartTime = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("innards_debris_num"))
        {
            InnardsDebrisNum = Reader.get_value_float(0);
            return true;
        }
        return true;
    }
}
