// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_sphere_query.h"

#include <cmath>

#include "ivp_compact_surface.h"
#include "../ivp_physics/ivp_core.h"
#include "../ivp_physics/ivp_environment.h"
#include "../ivp_physics/ivp_ov_element.h"
#include "../ivp_physics/ivp_real_object.h"
#include "../ivp_physics/ivp_surman_polygon.h"

// Every distance test below keeps its differences, squares and sums on the x87 stack (double)
// and only compares.

namespace
{
    // DAT_0658ab48: the hit flag the ledge-tree walk sets (0635072b) and collect_objects_in_node
    // clears before the walk (06350977) and reads back after it (063509ac). Nothing else
    // references it.
    int32_t G_bLedgeTreeSphereHit = 0;

    // DAT_063a5460: 0.5f.
    const float G_Half = 0.5f;

    // FUN_06350630, cdecl: 1 when the sphere (P, RadiusP) touches the sphere (Q, RadiusQ);
    // unordered returns 0.
    int32_t IVP_SphereTouchesSphere(const IVP_U_Point* P, float RadiusP, const IVP_U_Point* Q, float RadiusQ)
    {
        const double Dx = (double)(P->k[0]) - Q->k[0];
        const double Dy = (double)(P->k[1]) - Q->k[1];
        const double Dz = (double)(P->k[2]) - Q->k[2];
        const double R = (double)(RadiusP) + RadiusQ;
        const double DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
        if (R * R >= DistSq)
        {
            return 1;
        }
        return 0;
    }

    // FUN_06350690, cdecl: walks a compact ledge tree from Node and sets G_bLedgeTreeSphereHit
    // when the sphere (P, Radius) reaches a terminal node, or any node once Depth reaches
    // MaxDepth. The right son is followed in the loop, the left son by recursion; a miss (or
    // unordered) returns.
    void IVP_LedgeTreeSphereWalk(const IVP_Compact_Ledgetree_Node* Node, const IVP_U_Point* P, float Radius,
                                 int32_t Depth, int32_t MaxDepth)
    {
        int32_t ChildDepth = Depth + 1;
        for (;;)
        {
            const double Dx = (double)(P->k[0]) - Node->center[0];
            const double Dy = (double)(P->k[1]) - Node->center[1];
            const double Dz = (double)(P->k[2]) - Node->center[2];
            const double R = (double)(Node->radius) + Radius;
            const double DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
            if (!(R * R >= DistSq))
            {
                return;
            }
            if (Depth >= MaxDepth || Node->offset_right_node == 0)
            {
                G_bLedgeTreeSphereHit = 1;
                return;
            }
            IVP_LedgeTreeSphereWalk(Node->left_son(), P, Radius, ChildDepth, MaxDepth);
            Node = Node->right_son();
            ++Depth;
            ++ChildDepth;
        }
    }
}

// FUN_06350740: copies the record, then walks the OV tree from the tree manager's
// CurrentRoot (env+0x14 -> +0x178) with a cubic half-size of Radius.
void IVP_Sphere_Object_Query::collect_objects(const FIVP_Sphere_Query_Record* Record, IVP_Environment* Environment)
{
    Center.k[0] = Record->Center[0];
    Center.k[1] = Record->Center[1];
    Center.k[2] = Record->Center[2];
    Radius = Record->Radius;
    MaxLedgeTreeDepth = Record->MaxLedgeTreeDepth;
    TreeManager = Environment->ov_tree_manager;
    IVP_OV_Node* const Root = TreeManager->CurrentRoot;

    IVP_U_Point HalfSize;     // local_10; its fourth dword is never written
    HalfSize.k[0] = Radius;
    HalfSize.k[1] = Radius;
    HalfSize.k[2] = Radius;

    IVP_U_Point QueryCenter;  // local_20; its fourth dword is never written
    QueryCenter.k[0] = Center.k[0];
    QueryCenter.k[1] = Center.k[1];
    QueryCenter.k[2] = Center.k[2];

    collect_objects_in_node(Root, &HalfSize, &QueryCenter);
}

// FUN_063507e0. Leaves when the cell (node_cell_bounds) misses the query box on any axis (or
// unordered). For every element whose core's bounding sphere touches the query sphere, the
// query centre goes into the object's cache-slot space (FUN_06353510 inlined, FUN_0634d860)
// and: a polygon walks its ledge tree to MaxLedgeTreeDepth; a ball tests FUN_06350630 with
// Local (SLOT space) against core->MWorldFCore.vv (WORLD space) -- mixed spaces, exactly as
// retail passes them; other types add nothing. Then every child recurses. No pointer is
// null-tested.
void IVP_Sphere_Object_Query::collect_objects_in_node(IVP_OV_Node* Node, const IVP_U_Point* HalfSize,
                                                       const IVP_U_Point* QueryCenter)
{
    float Origin[3];   // local_10..local_8
    float Extent;      // local_3c
    TreeManager->node_cell_bounds(Node, Origin, &Extent);

    const double Half = (double)(Extent) * G_Half;
    const float HalfF = (float)(Half);                          // local_20/1c/18
    const float NodeCenterX = (float)(Half + Origin[0]);        // local_30
    const float NodeCenterY = (float)(Half + Origin[1]);        // local_2c
    const float NodeCenterZ = (float)(Half + Origin[2]);        // local_28

    if (!((double)(HalfF) + HalfSize->k[0] >= fabs((double)(NodeCenterX) - QueryCenter->k[0])))
    {
        return;
    }
    if (!((double)(HalfF) + HalfSize->k[1] >= fabs((double)(NodeCenterY) - QueryCenter->k[1])))
    {
        return;
    }
    if (!((double)(HalfF) + HalfSize->k[2] >= fabs((double)(NodeCenterZ) - QueryCenter->k[2])))
    {
        return;
    }

    for (int32_t i = 0; i < static_cast<int32_t>(Node->Elements.n_elems); ++i)
    {
        IVP_Real_Object* const Object = Node->Elements.elems[i]->real_object;
        IVP_Core* const Core = Object->core;
        const float ObjectRadius = Core->UpperLimitRadius;   // local_34
        const float QueryRadius = Radius;                    // local_38

        IVP_U_Point Delta;                                   // local_20..local_18
        Delta.subtract(&Center, &Core->MWorldFCore.vv);

        const double R = (double)(QueryRadius) + ObjectRadius;
        const double DistSq = ((double)(Delta.k[2]) * Delta.k[2] + (double)(Delta.k[1]) * Delta.k[1])
                            + (double)(Delta.k[0]) * Delta.k[0];
        if (!(R * R >= DistSq))
        {
            continue;
        }

        IVP_NarrowPhaseCacheSlot* const Slot = Object->ResolveNarrowPhaseCacheSlot();

        IVP_U_Point Local;                                   // local_30
        IVP_TransformWorldToSlotSpace(Slot, &Center, &Local);

        G_bLedgeTreeSphereHit = 0;
        const int32_t Type = Object->object_type;
        if (Type == 2)
        {
            const IVP_SurfaceManager_Polygon* const SurfaceManager =
                static_cast<const IVP_SurfaceManager_Polygon*>(Object->surface_manager);
            IVP_LedgeTreeSphereWalk(SurfaceManager->compact_surface->get_compact_ledge_tree_root(),
                                    &Local, Radius, 0, MaxLedgeTreeDepth);
            if (G_bLedgeTreeSphereHit != 0)
            {
                Found.Add(Object);
            }
        }
        else if (Type == 3)
        {
            if (IVP_SphereTouchesSphere(&Local, Radius, &Core->MWorldFCore.vv, Core->UpperLimitRadius) != 0)
            {
                Found.Add(Object);
            }
        }

        Object->ReleaseNarrowPhaseCacheSlot(Slot);
    }

    for (int32_t i = 0; i < static_cast<int32_t>(Node->Children.n_elems); ++i)
    {
        collect_objects_in_node(Node->Children.elems[i], HalfSize, QueryCenter);
    }
}
