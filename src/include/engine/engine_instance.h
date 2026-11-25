// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The per-instance callback interface the engine (EngBase) calls for every instance, shared by
// its implementers (Common's EngineObject, EngBase's own FInstanceTransform, Deformable2's
// FBoneRuntime) and by every caller that hands one to IEngine.

#include "dacom.h"
#include "math3d.h"

// Engine instance and archetype ids. Retail's decorated names establish `long` (J):
// ?create_instance@Root@Archetype@@QBEJPAUIEngineInstance@@@Z (returns the instance),
// ?get_engine_index@Root@Archetype@@QBEJXZ (the archetype), ?get_index@EngineObject@@QBEJXZ,
// ?DALib_Engine_get_csys@@YA?AVCsys@@J@Z, ?CompoundInstanceList@@YAXJPAHPAPAJ@Z,
// ?Load@Archetype@@YA_NPAUIFileSystem@@PAJPAH@Z. Both are -1 when invalid. The alias names are
// ours; EngBase hands out the addresses of its own instance and archetype records as the values.
typedef long InstanceIndex;
typedef long ArchetypeIndex;
static const InstanceIndex InvalidInstanceIndex = -1;
static const ArchetypeIndex InvalidArchetypeIndex = -1;

// IEngineInstance: 14 stdcall slots, each with the engine instance as its first argument.
// Retail's spelling of every slot comes from EngineObject's exported overrides in Common.dll
// (?initialize_instance@EngineObject@@EAGXJ@Z, ?set_position@EngineObject@@EAGXJABVVector@@@Z,
// ?get_transform@EngineObject@@EBGABVTransform@@J@Z,
// ?get_centered_radius@EngineObject@@EBGXJPAMPAVVector@@@Z, ...). Retail EngineObject vtable
// @ .rdata 0002:000025BC; EngBase's own implementation is vtable 066295d0, Deformable2's
// PTR_LAB_066010a0. IEngine+0xa0 (GetInstanceTransform) is slot 8, get_transform.
struct IEngineInstance
{
    virtual void DACOM_API initialize_instance(InstanceIndex Inst) = 0;                 // +0x00
    virtual void DACOM_API create_instance(InstanceIndex Inst) = 0;                 // +0x04 RET
    virtual void DACOM_API destroy_instance(InstanceIndex Inst) = 0;                    // +0x08
    virtual void DACOM_API set_position(InstanceIndex Inst, Vector const& Pos) = 0;     // +0x0C
    virtual Vector const& DACOM_API get_position(InstanceIndex Inst) const = 0;         // +0x10
    virtual void DACOM_API set_orientation(InstanceIndex Inst, Matrix const& Orient) = 0; // +0x14
    virtual Matrix const& DACOM_API get_orientation(InstanceIndex Inst) const = 0;      // +0x18
    virtual void DACOM_API set_transform(InstanceIndex Inst, Transform const& Xform) = 0; // +0x1C
    // +0x20: what IEngine+0xa0 (GetInstanceTransform) answers.
    virtual Transform const& DACOM_API get_transform(InstanceIndex Inst) const = 0; // +0x20
    virtual void DACOM_API get_centered_radius(InstanceIndex Inst, float* OutRadius,
                                               Vector* OutCenter) const = 0;           // +0x24
    virtual void DACOM_API set_centered_radius(InstanceIndex Inst, float Radius,
                                               Vector const& Center) = 0;              // +0x28
    virtual void DACOM_API set_instance_flags(InstanceIndex Inst, unsigned long Flags) = 0; // +0x2C
    virtual unsigned long DACOM_API get_instance_flags(InstanceIndex Inst) const = 0;   // +0x30
    // Whether the world-dirty mark propagates to the instance's children (EngBase
    // FUN_0661f210 reads AL); EngineObject and EngBase's own implementation answer false.
    virtual bool DACOM_API joint_changed(InstanceIndex Inst) = 0;                       // +0x34
};
