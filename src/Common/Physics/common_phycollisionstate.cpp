// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_physys.h"

#include "common_archetype.h"
#include "common_cobject.h"
#include "common_cobjdb.h"
#include "common_id.h"
#include "common_universe.h"
#include "common_net.h"
#include "common_phyarch.h"
#include "common_misc.h"
#include "common_server.h"

#include "dacom.h"
#include "DALib.h"
#include "x86math.h"
#include "ivp_collision/ivp_collision.h" // IVP_Collision::get_objects
#include "ivp_physics/ivp_core.h"
#include "ivp_physics/ivp_real_object.h"
#include "ivp_physics/ivp_environment.h"
#include "ivp_physics/ivp_controller.h"
#include "ivp_physics/ivp_material.h"
#include "ivp_physics/ivp_listener.h"
#include "ivp_physics/ivp_mindist_settings.h"
#include "ivp_physics/ivp_mindist.h"
#include "common_csys.h"
#include "ivp_compact_builder/ivp_compact_ledge.h"
#include "ivp_compact_builder/ivp_surface_builder_pointsoup.h"
#include "ivp_compact_builder/ivp_surface_builder_ledge_soup.h"
#include "ivp_physics/ivp_surman_polygon.h"
#include "ivp_collision/ivp_ray_solver.h"
#include "ivp_collision/ivp_sphere_query.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include <mmsystem.h>

// Win32 macro conflict: PhySys::GetCurrentTime vs windows.h GetCurrentTime()
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include "Physics/common_physys_internal.h"

using namespace CmnPhySys;

namespace
{
    // .rdata:0639b924
    const char kPhyCollisionStateManagerSource[] = "E:\\FL\\Scratch\\Source\\Common\\PhyCollisionStateManager.cpp";
}

// ============================================================================
// PhySys::PhyCollisionStateManager
// ============================================================================

PhySys::PhyCollisionStateManager::PhyCollisionStateManager(CObject* Object)
    : OwnerObject(Object)
    , IntruderCount(0)
    , IntruderTime(0.0f)
    , CollisionType(0)
{
    // FUN_062814b0. Pad11 is not written, as in retail. The archetype is read through an
    // ICF-folded getter (CAsteroid::asteroidarch); Object is not tested.
    Archetype::Root const* const Arch = Object->Archetype;
    if (Arch != nullptr)
        CollisionType = (unsigned char)Arch->bPhantomPhysics;
}

// FUN_06280aa0: the padding is not copied.
PhySys::PhyCollisionStateManager::PhyCollisionStateManager(PhyCollisionStateManager const& Other)
    : OwnerObject(Other.OwnerObject)
    , IntruderCount(Other.IntruderCount)
    , IntruderTime(Other.IntruderTime)
    , CollisionType(Other.CollisionType)
{
}

PhySys::PhyCollisionStateManager::~PhyCollisionStateManager()
{
    // FUN_062814f0. The owner is not tested. A body with a phantom controller has this manager
    // removed from the phantom's listeners.
    IVP_Real_Object* const Body = OwnerObject->BodyPtr;
    if (Body == nullptr)
        return;
    IVP_Controller_Phantom* const Phantom = (IVP_Controller_Phantom*)Body->first_child;
    if (Phantom == nullptr)
        return;
    Phantom->remove_listener_phantom(this);
}

// FUN_06280ad0 (ICF-shared with CPhysControllerEquip::operator=): no self test, padding not
// copied.
PhySys::PhyCollisionStateManager& PhySys::PhyCollisionStateManager::operator=(
    PhyCollisionStateManager const& Other)
{
    OwnerObject = Other.OwnerObject;
    IntruderCount = Other.IntruderCount;
    IntruderTime = Other.IntruderTime;
    CollisionType = Other.CollisionType;
    return *this;
}

// FUN_06281510
void PhySys::PhyCollisionStateManager::enable_collisions(bool bEnable, bool bImmediate)
{
    IVP_Real_Object* const Body = OwnerObject->BodyPtr;
    if (Body == nullptr)
        return;

    // Collision detection is enabled exactly when the two location bits read 01.
    bool const bWasEnabled = (Body->flags & 0x300u) == 0x100u;

    if (!bEnable)
    {
        // Only an enabled body is switched off.
        if (bWasEnabled)
        {
            Body->enable_collision_detection(false);
            IntruderCount = 0;
            IntruderTime = 0.0f;
        }
        return;
    }

    // Neither immediate nor a phantom-physics archetype: the deferred path drops the collision
    // phantom and enables detection.
    if (!bImmediate && CollisionType == 0)
    {
        if (Body->first_child != nullptr)
        {
            Body->convert_phantom_to_object();
            IntruderCount = 0;
            IntruderTime = 0.0f;
        }
        if (bWasEnabled)
            return;
        // The body pointer is re-read here and below, as in retail.
        OwnerObject->BodyPtr->enable_collision_detection(true);
        return;
    }

    // Immediate (or phantom-physics) path; nothing to do when already enabled.
    if (bWasEnabled)
        return;

    // Without a phantom one is built (client_data 1) and this manager becomes its listener.
    if (Body->first_child == nullptr)
    {
        IVP_Template_Phantom Template;
        Template.client_data = (void*)(uintptr_t)1;
        OwnerObject->BodyPtr->convert_to_phantom(&Template);
        IVP_Real_Object* const Converted = OwnerObject->BodyPtr;
        ((IVP_Controller_Phantom*)Converted->first_child)->add_listener_phantom(this);
    }

    OwnerObject->BodyPtr->enable_collision_detection(true);

    // With a phantom, a non-phantom archetype, detection enabled and nobody inside, the
    // phantom is turned back into an object at once (IntruderCount is already 0).
    IVP_Real_Object* const BodyAfter = OwnerObject->BodyPtr;
    if (BodyAfter == nullptr || BodyAfter->first_child == nullptr)
        return;
    if (CollisionType != 0)
        return;
    if ((BodyAfter->flags & 0x300u) != 0x100u)
        return;
    if (IntruderCount != 0)
        return;
    BodyAfter->convert_phantom_to_object();
    IntruderTime = 0.0f;
}

bool PhySys::PhyCollisionStateManager::get_desired_collision_state() const
{
    // FUN_06281630. The owner is not tested.
    IVP_Real_Object const* const Body = OwnerObject->BodyPtr;
    if (Body == nullptr)
        return false;
    return (Body->flags & 0x300u) == 0x100u;
}

bool PhySys::PhyCollisionStateManager::get_actual_collision_state() const
{
    // FUN_06281650: enabled and without a phantom controller.
    IVP_Real_Object const* const Body = OwnerObject->BodyPtr;
    if (Body == nullptr)
        return false;
    if ((Body->flags & 0x300u) != 0x100u)
        return false;
    return Body->first_child == nullptr;
}

void PhySys::PhyCollisionStateManager::update(float Dt)
{
    // FUN_06281680
    IVP_Real_Object* const Body = OwnerObject->BodyPtr;
    if (Body == nullptr)
        return;
    if (Body->first_child == nullptr)         // no phantom controller
        return;
    if (CollisionType != 0)
        return;
    if ((Body->flags & 0x300u) != 0x100u)
        return;
    if (IntruderCount == 0)
    {
        Body->convert_phantom_to_object();
        IntruderTime = 0.0f;
        return;
    }
    IntruderTime += Dt;
}

namespace
{
    // IVP_Mindist_Base slot 1, get_objects, as every callback below starts.
    void PhySys_GetMindistObjects(IVP_Mindist_Base* MindistBase, IVP_Real_Object* OutPair[2])
    {
        MindistBase->get_objects(OutPair);
    }

    // The shared selection: the pair member that is not the owner -- Pair[1] when
    // Pair[0]->client_data (+0xa4) is the owner, else Pair[0].
    IVP_Real_Object* PhySys_SelectOther(IVP_Real_Object* const Pair[2], CObject const* Owner)
    {
        return (Owner == (CObject*)Pair[0]->client_data) ? Pair[1] : Pair[0];
    }

    // The intruder filter both volume callbacks inline: an object without a client is
    // ignored; when the owner's body location byte (+0x78) is 9, an intruder whose own
    // (signed) location byte is 0x10 or 9 is ignored as well.
    bool PhySys_CountsAsIntruder(CObject const* Owner, IVP_Real_Object const* Other)
    {
        if (Other->client_data == nullptr)
            return false;
        IVP_Real_Object const* const OwnerBody = Owner->BodyPtr;
        if ((uint8_t)OwnerBody->flags != 9)
            return true;
        int8_t const OtherLocation = (int8_t)Other->flags;
        return OtherLocation != 0x10 && OtherLocation != 9;
    }
}

// FUN_062816e0
void PhySys::PhyCollisionStateManager::mindist_entered_volume(IVP_Controller_Phantom* /*PhantomCtrl*/, IVP_Mindist_Base* MindistBase)
{
    IVP_Real_Object* Pair[2];
    PhySys_GetMindistObjects(MindistBase, Pair);
    IVP_Real_Object* const Other = PhySys_SelectOther(Pair, OwnerObject);
    if (PhySys_CountsAsIntruder(OwnerObject, Other))
        ++IntruderCount;
}

// FUN_06281740
void PhySys::PhyCollisionStateManager::mindist_left_volume(IVP_Controller_Phantom* /*PhantomCtrl*/, IVP_Mindist_Base* MindistBase)
{
    IVP_Real_Object* Pair[2];
    PhySys_GetMindistObjects(MindistBase, Pair);
    IVP_Real_Object* const Other = PhySys_SelectOther(Pair, OwnerObject);
    if (!PhySys_CountsAsIntruder(OwnerObject, Other))
        return;
    // A negative count is reported (and kept).
    --IntruderCount;
    if ((int)IntruderCount < 0)
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPhyCollisionStateManagerSource, 0x11d,
              "Negative intruder count!");
}

void PhySys::PhyCollisionStateManager::phantom_is_going_to_be_deleted_event(IVP_Controller_Phantom* PhantomCtrl)
{
    // Empty; ICF-folded with CostumeDescriptions::warn (FUN_062ed730).
    (void)PhantomCtrl;
}

IVP_Real_Object* PhySys::PhyCollisionStateManager::get_other_object(IVP_Mindist_Base* MindistBase)
{
    // FUN_06281940
    IVP_Real_Object* Pair[2];
    PhySys_GetMindistObjects(MindistBase, Pair);
    return PhySys_SelectOther(Pair, OwnerObject);
}

CObject* PhySys::PhyCollisionStateManager::mindist_to_cobject(IVP_Mindist_Base* MindistBase)
{
    // FUN_06281970: the non-owner member's client.
    IVP_Real_Object* Pair[2];
    PhySys_GetMindistObjects(MindistBase, Pair);
    return (CObject*)PhySys_SelectOther(Pair, OwnerObject)->client_data;
}

// FUN_062817c0
int PhySys::PhyCollisionStateManager::get_intruding_cobjs(bool bRoot, CheapSet<CObject*, std::less<CObject*>>& Out)
{
    // An IVP_VHash slot: {hash index, element}. IVP_VHash has no typed slot accessor; this is
    // the only view of its Elems array outside ivp_vhash.h.
    struct FVHashSlot
    {
        unsigned HashIndex;
        IVP_Mindist_Base* Elem;
    };

    IVP_Real_Object* const Body = OwnerObject->BodyPtr;
    if (Body != nullptr)
    {
        IVP_Controller_Phantom* const Phantom = (IVP_Controller_Phantom*)Body->first_child;
        if (Phantom != nullptr)
        {
            // SizeMm + 1 slots, the bound re-read on every pass.
            for (int Slot = 0; Slot < Phantom->mindist_set.SizeMm + 1; ++Slot)
            {
                IVP_Mindist_Base* const Mindist = ((FVHashSlot*)Phantom->mindist_set.Elems)[Slot].Elem;
                if (Mindist == nullptr)
                    continue;

                IVP_Real_Object* Pair[2];
                PhySys_GetMindistObjects(Mindist, Pair);
                CObject* const Client = (CObject*)PhySys_SelectOther(Pair, OwnerObject)->client_data;
                if (Client == nullptr)
                    continue;

                // The root or the client itself, added unless present.
                if (bRoot)
                    Out.Insert(GetRoot(Client));
                else
                    Out.Insert(Client);
            }
        }
    }

    return Out.Num();
}

