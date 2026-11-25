// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// EngineObject: the IEngineInstance base of every CObject (engine instance id, transform,
// radius, instance flags, velocities).
#include "common_cobject.h"
#include "Objects/common_cobject_internal.h" // CmnCobject::CopyMatrix9

#include "DALib.h"
#include "dacom.h"

#include <string.h>

using CmnCobject::CopyMatrix9;

namespace
{
    // IEngine +0x6c, as update_tree calls it: no test on DALib::Engine.
    void UpdateEngineInstance(long Instance)
    {
        DALib::Engine->UpdateInstance(Instance, 0, nullptr);
    }
}

EngineObject::EngineObject()
{
    // FUN_063098a0
    EngineInstanceId = -1;
    memset(&Orientation, 0, sizeof(Orientation));
    Orientation.E[0][0] = 1.0f;
    Orientation.E[1][1] = 1.0f;
    Orientation.E[2][2] = 1.0f;
    WorldPosition.x = 0.0f;
    WorldPosition.y = 0.0f;
    WorldPosition.z = 0.0f;
    Radius = 0.0f;
    RadiusCenter.x = 0.0f;
    RadiusCenter.y = 0.0f;
    RadiusCenter.z = 0.0f;
    InstanceFlags = 0;
}

EngineObject::~EngineObject()
{
}

void DACOM_API EngineObject::initialize_instance(long Inst)
{
    // FUN_06309910 (+0x04)
    EngineInstanceId = (int)Inst;
}

void DACOM_API EngineObject::create_instance(long Inst)
{
    // FUN_0628f050, an empty body (EngBase slot 1).
    (void)Inst;
}

void DACOM_API EngineObject::destroy_instance(long Inst)
{
    // FUN_06309920
    (void)Inst;
    EngineInstanceId = -1;
}

void DACOM_API EngineObject::set_position(long Inst, Vector const& Pos)
{
    (void)Inst;
    set_position(Pos);
}

Vector const& DACOM_API EngineObject::get_position(long Inst) const
{
    (void)Inst;
    return WorldPosition;
}

void DACOM_API EngineObject::set_orientation(long Inst, Matrix const& Orient)
{
    (void)Inst;
    set_orientation(Orient);
}

Matrix const& DACOM_API EngineObject::get_orientation(long Inst) const
{
    (void)Inst;
    return Orientation;
}

void DACOM_API EngineObject::set_transform(long Inst, Transform const& Xform)
{
    (void)Inst;
    set_transform(Xform);
}

Transform const& DACOM_API EngineObject::get_transform(long Inst) const
{
    (void)Inst;
    return get_transform();
}

void DACOM_API EngineObject::get_centered_radius(long Inst, float* OutRadius,
                                                 Vector* OutCenter) const
{
    // FUN_06309a30 (+0x38, +0x3c). No null tests on the outputs.
    (void)Inst;
    *OutRadius = Radius;
    *OutCenter = RadiusCenter;
}

void DACOM_API EngineObject::set_centered_radius(long Inst, float NewRadius,
                                                 Vector const& Center)
{
    (void)Inst;
    Radius = NewRadius;
    RadiusCenter = Center;
}

void DACOM_API EngineObject::set_instance_flags(long Inst, unsigned long Flags)
{
    (void)Inst;
    InstanceFlags = Flags;
}

unsigned long DACOM_API EngineObject::get_instance_flags(long Inst) const
{
    (void)Inst;
    return InstanceFlags;
}

bool DACOM_API EngineObject::joint_changed(long Inst)
{
    (void)Inst;
    return false;
}

Vector const& DACOM_API EngineObject::get_velocity(long Inst) const
{
    // FUN_063099b0
    (void)Inst;
    static const Vector Zero = {0.0f, 0.0f, 0.0f};
    return Zero;
}

void DACOM_API EngineObject::set_velocity(long Inst, Vector const& Vel)
{
    // FUN_062a8910, an empty body.
    (void)Inst;
    (void)Vel;
}

Vector const& DACOM_API EngineObject::get_angular_velocity(long Inst) const
{
    // FUN_063099f0
    (void)Inst;
    static const Vector Zero = {0.0f, 0.0f, 0.0f};
    return Zero;
}

void DACOM_API EngineObject::set_angular_velocity(long Inst, Vector const& AngVel)
{
    // FUN_062a8910 (shared with set_velocity)
    (void)Inst;
    (void)AngVel;
}

void EngineObject::set_position(Vector const& Pos)
{
    // FUN_0627cb10
    WorldPosition = Pos;
}

void EngineObject::set_orientation(Matrix const& Orient)
{
    // FUN_0627cb30 (+0x08)
    CopyMatrix9(Orientation, Orient);
}

void EngineObject::set_transform(Transform const& Xform)
{
    // FUN_0627cb50
    CopyMatrix9(Orientation, Xform.Rotation);
    WorldPosition = Xform.Translation;
}

Transform const& EngineObject::get_transform() const
{
    // this+0x08: Orientation followed by WorldPosition, one Transform.
    return *(Transform const*)&Orientation;
}

Matrix const& EngineObject::get_orientation() const
{
    // FUN_06263b80 (+0x08)
    return Orientation;
}

Vector const& EngineObject::get_position() const
{
    // FUN_06356500 (+0x2c)
    return WorldPosition;
}

long EngineObject::get_index() const
{
    // FUN_062d1ad0 (+0x04)
    return EngineInstanceId;
}

float const EngineObject::get_radius() const
{
    return Radius;
}

void EngineObject::update_tree() const
{
    // FUN_0627caf0
    UpdateEngineInstance(EngineInstanceId);
}

void EngineObject::set_centered_radius(float NewRadius, Vector const& Center)
{
    // FUN_0627cb80. A virtual call (vtable +0x28) to the IEngineInstance overload.
    set_centered_radius(EngineInstanceId, NewRadius, Center);
}
