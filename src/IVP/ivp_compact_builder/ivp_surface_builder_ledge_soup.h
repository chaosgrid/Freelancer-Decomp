// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

#include "ivp_compact_ledge.h"
#include "../ivp_collision/ivp_compact_surface.h" // IVP_Compact_Ledgetree_Node
#include "../ivp_utility/ivp_u_vector.h"
#include "../ivp_utility/ivp_vhash.h"

struct IVP_SurfaceManager_OutList; // ivp_surface_manager.h (retail IVP_U_BigVector)
struct IVP_U_Matrix;               // ivp_math.h

// Retail's IVP_SurfaceBuilder_Ledge_Soup.
//
// Class name from a string in the shipped Common.dll (outside the Ghidra
// export). Collects already-built IVP_Compact_Ledges via InsertLedge() and
// Compile()s them into one IVP_Compact_Surface blob: a ledge tree (built by
// a recursive spatial split), the ledges (vertices welded through a point
// hash when several are merged), and the mass-property header.
// Both retail call sites (PhyArch::GetDefaultPart @0x6280450 and an OBB-corner
// builder @0x628ae87) use a stack instance, insert exactly ONE ledge and pass
// no options (default {0,1,0,1}), so the N-ledge split / weld paths and the
// options[0] root-hull path are unreachable in retail (ported anyway).
//
// Layout from the ctor (0x6369e70, zeroes 0x94 bytes) and dtor (0x6369f20).
// The six 8-byte {word cap, word count, elems} headers are IVP_U_Vector<T>.
//   m_Ledges  (A): the inserted ledges.
//   m_VectorB/C/F: BuildNode lists. B's only writer is FUN_0636a1e0 (the
//     options[0] root hull), so it is always empty in retail.
//   m_VectorD/E: never read or written by any code in the class; element type
//     unknown.
class IVP_SurfaceBuilder_Ledge_Soup
{
public:
    IVP_SurfaceBuilder_Ledge_Soup();

    // FUN_0636a050. A null ledge is dropped (retail logs a warning first).
    void InsertLedge(IVP_Compact_Ledge* ledge);

private:
    // Unaligned 16-byte point; the alignas(16) IVP_U_Float_Point would pad
    // the class past its retail size of 0x94.
    struct RawPoint3f
    {
        float X, Y, Z, Pad;
    };

    // Port-local name: the 0x28-byte node operator_new'd per inserted ledge
    // (leaf, FUN_0636a350) or per internal split (FUN_0636a860), held in
    // m_VectorC / m_VectorF and compiled recursively by FUN_0636b5e0.
    struct BuildNode
    {
        uint32_t ScratchIndex;  // +0x00: the node's 1-based slot in m_BuildTreeScratch (leaves only); never read.
        float CenterX;          // +0x04
        float CenterY;          // +0x08
        float CenterZ;          // +0x0c
        uint32_t m_Unknown10;   // +0x10: never read. SplitBuildNodeList writes it with the 4th (unwritten) dword of its center IVP_U_Point (retail: stack garbage, port: 0).
        float Radius;           // +0x14
        uint8_t Octant[3];      // +0x18..+0x1a: per-axis octant/bucket-split classification bytes (0..2).
        uint8_t m_Pad1b;        // +0x1b: unused pad byte (FUN_0636b5e0 forces the compiled copy's +0x1b to 0).
        IVP_Compact_Ledge* Ledge; // +0x1c: the source ledge until MergeLedgesIntoSurface (0636b520) repoints it at its copy inside the compiled blob; null on split nodes without a hull.
        BuildNode* Child0;      // +0x20: first child (0 => this is a leaf node).
        BuildNode* Child1;      // +0x24: second child.
    };
    static_assert(sizeof(BuildNode) == 0x28,
                  "must match retail size -- FUN_0636a350/FUN_0636a860 both operator_new(0x28) + REP STOSD (ECX=0xa dwords) this struct");

    // CompileNode() (FUN_0636b5e0) writes one IVP_Compact_Ledgetree_Node
    // (ivp_collision/ivp_compact_surface.h) per compiled BuildNode,
    // bump-allocated from m_CompactNodeCursor (+0x80).

    // The leaf ledges of the compiled surface are collected by
    // IVP_Compact_Ledge_Solver::get_all_ledges (FUN_0635abf0 / FUN_0635aba0,
    // ivp_compact_ledge_solver.cpp) into an IVP_SurfaceManager_OutList.

    // FUN_0636d020 builds it (thiscall): zeroes +0x00/+0x04/+0x08/+0x10/+0x14.
    // The volume / first-moment accumulator of FUN_0636d550 / d510 / d360.
    struct FVolumeIntegralAccumulator
    {
        float Moment[3];     // +0x00: sum of 0.25*D*p over every triangle vertex
        float Field_0x0C;    // +0x0c: never written nor read by retail (the port zeroes it)
        float NormalQuadSum; // +0x10: sum of |n|^2
        float Volume6;       // +0x14: sum of D = n . P0 (six times the volume)

        FVolumeIntegralAccumulator()
            : Moment{ 0.0f, 0.0f, 0.0f }
            , Field_0x0C(0.0f)
            , NormalQuadSum(0.0f)
            , Volume6(0.0f)
        {
        }
    };
    static_assert(sizeof(FVolumeIntegralAccumulator) == 0x18, "0x18-byte stack object of FUN_0636d6f0 (local_68)");

    // FUN_0636d000(csys) builds it (thiscall, RET 4): {csys, 0, 0, 0}. The
    // accumulator of FUN_0636d580 / FUN_0636d040.
    struct FAxisIntegralAccumulator
    {
        const IVP_U_Matrix* Csys; // +0x00
        float Mass;               // +0x04
        float SumA;               // +0x08
        float SumB;               // +0x0c

        explicit FAxisIntegralAccumulator(const IVP_U_Matrix* InCsys)
            : Csys(InCsys)
            , Mass(0.0f)
            , SumA(0.0f)
            , SumB(0.0f)
        {
        }
    };

    // Port-local name: an 8-byte entry of the calloc'd m_BuildTreeScratch
    // array (a doubly linked index). Entry [0] is a header whose
    // NextIndexOrCount holds the current root entry's index (read by
    // CompileTree()); entries [1..n] are the build-tree nodes.
    struct BuildTreeScratchEntry
    {
        int16_t PrevIndex;         // +0x00: previous entry's index in the doubly-linked chain (or the root-slot's own bookkeeping for entry 0).
        int16_t NextIndexOrCount;  // +0x02: next entry's index (0 = end of chain), OR for entry [0] specifically, the current root entry's index.
        BuildNode* Payload;        // +0x04: the BuildNode this entry represents (entries [1..n] only; entry [0]'s payload is never written/read).
    };
    static_assert(sizeof(BuildTreeScratchEntry) == 0x8,
                  "must match retail size -- BuildLeafNodes()/CompileTree() both index this array with a fixed *8 stride");

    void* m_CompactSurface;                    // +0x00: the built surface blob; handed to the caller (and cleared) at the end of Compile().

    int32_t m_LeafCount;        // +0x04: m_Ledges.Len() at BuildLeafNodes() time; Compile() shrink-copies the blob when > 1.
    int32_t m_SplitNodeCount;   // +0x08: ledgetree node count to reserve: FinalizeBuildTree seeds it with m_LeafCount, each internal split adds one. Sizes the node array in BuildCompactSurface().
    float m_MinRadius;          // +0x0c: smallest leaf radius so far; 0.0f (DAT_063a53d4) means "unset".
    int32_t m_LeafByteTotal;    // +0x10: += 0x28 per leaf built; never read.

    void* m_BuildTreeScratch;                   // +0x14: calloc(n+1, sizeof(BuildTreeScratchEntry)) from BuildLeafNodes(); walked by CompileTree(), freed by FreeBuildState().
    IVP_U_Vector<IVP_Compact_Ledge> m_Ledges;   // +0x18: the inserted ledges.
    IVP_U_Vector<BuildNode> m_VectorB;          // +0x20: always empty in retail (only writer: FUN_0636a1e0).
    IVP_U_Vector<BuildNode> m_VectorC;          // +0x28
    RawPoint3f m_BBoxMin;                       // +0x30: ctor sets 0x49742400 (1e6f) per axis; SplitBuildNodeList reuses it as scratch.
    RawPoint3f m_BBoxMax;                       // +0x40: ctor sets 0xc9742400 (-1e6f) per axis; same scratch reuse.
    int32_t m_SplitAxis;                        // +0x50: axis (0/1/2) of the largest bbox extent, set by BuildLeafNodes() only when m_LeafCount > 1; never read.
    uint8_t m_Unknown54[0x4];                   // +0x54..+0x57: unidentified.
    uint32_t m_Unknown58;                       // +0x58: unidentified.
    IVP_U_Vector<void> m_VectorD;                // +0x5c: never used (see the class comment).
    IVP_U_Vector<void> m_VectorE;                // +0x64: never used.
    void* m_CompileOptions;                     // +0x6c: the 4-dword options block; Compile() defaults a null one to {0,1,0,1} (both retail callers pass null).

    void* m_VertexWriteCursor;   // +0x70: blob + 0x30 seeded by BuildCompactSurface(); read once by MergeLedgesIntoSurface() and never written back.
    void* m_VertexDedupBase;     // +0x74: welded-point pool (weld path only); zeroed otherwise.
    uint32_t m_VertexDedupCount;  // +0x78: welded-point count (weld path only); zeroed otherwise.
    IVP_VHash* m_VertexDedupIndex; // +0x7c: point hash (vtable 063a58f8, 0x10 bytes) built by BuildCompactSurface() when more than one ledge is merged with options[2]==0 and options[3] in {1,2}; deleted (slot 1, flag 1) after MergeLedgesIntoSurface().

    IVP_Compact_Ledgetree_Node* m_CompactNodeCursor; // +0x80: bump cursor into the blob's ledgetree node array; CompileNode() advances it by 0x1c.
    uint8_t m_Unknown84[0x8];                   // +0x84..+0x8b: unidentified.
    IVP_U_Vector<BuildNode> m_VectorF;          // +0x8c: owns the BuildNodes (FreeBuildState frees them).

    // FUN_0636a720: AABB of a BuildNode list into caller-supplied outMin /
    // outMax (SplitBuildNodeList passes m_BBoxMin / m_BBoxMax as scratch).
    static void ComputeNodeListBounds(const IVP_U_Vector<BuildNode>& nodes, float outMin[3], float outMax[3]);

    // `[node + 4 + axis*4]`, read inline by SplitBuildNodeList in retail
    // (not a retail function). CenterX/Y/Z are contiguous at +0x4/+0x8/+0xc.
    static float GetCenterAxis(const BuildNode* node, int axis) { return (&node->CenterX)[axis]; }

    // FUN_0635aa60: AABB of one ledge over every triangle edge's start point.
    static void ComputeLedgeBounds(const IVP_Compact_Ledge* ledge, float outMin[3], float outMax[3]);

    // FUN_0636a350: one leaf BuildNode per ledge (center = bbox midpoint,
    // radius = distance to the max corner, 3 octant bytes), linked into a
    // fresh m_BuildTreeScratch; grows m_BBoxMin / m_BBoxMax over every leaf
    // sphere and, with more than one ledge, sets m_SplitAxis.
    void BuildLeafNodes();

    // Recursively compiles one BuildNode (and, for internal nodes, both its
    // children) into a bump-allocated IVP_Compact_Ledgetree_Node at
    // m_CompactNodeCursor. Retail: FUN_0636b5e0. Returns the node written (the
    // cursor value read on entry, before the bump).
    IVP_Compact_Ledgetree_Node* CompileNode(BuildNode* source);

    // FUN_0636b740: compiles the tree from m_BuildTreeScratch's root entry
    // via CompileNode(), then clears the root index.
    void CompileTree();

    // FUN_0636a2b0: frees m_VectorC's array (its nodes are shared with
    // m_VectorF), every BuildNode in m_VectorF and its array, and
    // m_BuildTreeScratch.
    void FreeBuildState();

    // FUN_0636a640: collects m_BuildTreeScratch's root chain, seeds
    // m_SplitNodeCount = m_LeafCount, reduces the chain to one root via
    // SplitBuildNodeList() and stores it in the root slot. The float
    // argument (Compile() passes 1.0f) is unused.
    void FinalizeBuildTree(float unusedThreshold);

    // FUN_0636a860: reduces a BuildNode list to one node. 1 element: that
    // element; 2: one split node; more: a recursive spatial split (per-axis
    // mean +- epsilon buckets, neighbour tie-breaks, bbox-volume axis
    // choice). Retail only ever reaches the 1-element case.
    BuildNode* SplitBuildNodeList(IVP_U_Vector<BuildNode>& list);

    // Copies one source IVP_Compact_Ledge into the m_CompactSurface blob at
    // destCursor and returns the number of bytes written. Retail FUN_0636b3a0:
    // without a vertex hash it copies the whole ledge ((hdr[+8]>>8)<<4 bytes);
    // with one it copies header+triangles only and welds the points into the
    // shared pool at m_VertexDedupBase.
    uint32_t CopyLedgeIntoSurface(const void* sourceLedge, void* destCursor);

    // Retail FUN_0636a190: depth-first collection of the source ledges below
    // a build node (a node with a ledge is a leaf; otherwise Child0 is
    // recursed and the walk continues with Child1).
    static void CollectNodeLedges(IVP_U_Vector<IVP_Compact_Ledge>& OutLedges, const BuildNode* Node);

    // Retail FUN_0636a1e0: Compile()'s options[0] step - convex hull of every
    // ledge below the root build node, added to m_Ledges, stored as the
    // root's ledge and queued in m_VectorB.
    void BuildRootConvexHull();

    // FUN_0636b520: walks m_VectorC then m_VectorB, copying each node's
    // ledge into the blob via CopyLedgeIntoSurface() and repointing the
    // node at the copy; frees the source ledges when m_CompileOptions[1]
    // == 1 (the default), then clears m_Ledges.
    void MergeLedgesIntoSurface();

    // FUN_0636b190: allocates the blob (m_SplitNodeCount ledgetree nodes plus
    // every node's ledge, 16-byte aligned), writes the header, builds the
    // point hash when welding (see m_VertexDedupIndex), runs
    // MergeLedgesIntoSurface() and sets m_CompactNodeCursor. False when the
    // allocation fails.
    bool BuildCompactSurface();

    // Componentwise min / max, FUN_06348040 / FUN_06348070 (components z, y,
    // x): min copies on less or unordered, max only on greater. Only caller:
    // the fallback of FUN_0636d6f0.
    static void ComponentwiseMin3(float inout[3], const float other[3]);
    static void ComponentwiseMax3(float inout[3], const float other[3]);

    // LAB_0636d866, the fallback tail of FUN_0636d6f0: the combined AABB of every leaf, its midpoint as center of
    // mass and an isotropic inertia from its diagonal. See the .cpp.
    static void ComputeBoundingSphereMassPropertiesFallback(const IVP_SurfaceManager_OutList& leaves,
                                                              float outCenterOfMass[3], float outRotationInertia[3]);

    // FUN_0636d360: one triangle's volume / first-moment contribution.
    static void AccumulateTriangleVolume(FVolumeIntegralAccumulator& Acc, const IVP_Compact_Ledge* Ledge,
                                         const IVP_Compact_Triangle* Triangle);
    // FUN_0636d510: every triangle of one ledge, ascending.
    static void AccumulateLedgeVolume(FVolumeIntegralAccumulator& Acc, const IVP_Compact_Ledge* Ledge);
    // FUN_0636d550: every leaf ledge, descending.
    static void AccumulateVolumeIntegral(FVolumeIntegralAccumulator& Acc, const IVP_SurfaceManager_OutList& Leaves);

    // FUN_0636d040: one triangle's axis-integral contribution (A, B, C an
    // axis permutation). Points go through Acc.Csys->vimult4 (FUN_06348470).
    static void AccumulateTriangleAxisIntegral(FAxisIntegralAccumulator& Acc, const IVP_Compact_Ledge* Ledge,
                                               const IVP_Compact_Triangle* Triangle, int32_t AxisA, int32_t AxisB,
                                               int32_t AxisC);

    // FUN_0636d580 (cdecl): the axis integral of every leaf (descending)
    // in the Csys frame; FUN_0636d6f0 calls it with (0,1,2), (1,2,0), (2,0,1).
    // Mass below 1e-10 (or unordered) gives (0, 0, 1.0f).
    static void AccumulateAxisIntegral(const IVP_SurfaceManager_OutList& Leaves, int32_t AxisA, int32_t AxisB, int32_t AxisC,
                                       const IVP_U_Matrix* Csys, float* OutNormalized1, float* OutRawMass,
                                       float* OutNormalized2);

    // FUN_0636d6f0 (cdecl, leaves, &com, &inertia) -- Mirtich's polyhedral
    // mass properties over every leaf ledge:
    //  1. local_50 IVP_U_Matrix = identity (FUN_063481b0); accumulator
    //     (FUN_0636d020); FUN_0636d550.
    //  2. Mirtich path only when Volume6 > sqrt(NormalQuadSum)^3 * DAT_063a5914;
    //     otherwise the fallback (returns 0).
    //  3. Identity again; com = acc.Moment * float(1 / Volume6) (FUN_0628be10)
    //     -> *outCenterOfMass and Csys.vv.
    //  4. FUN_0636d580 with (0,1,2) / (1,2,0) / (2,0,1); inertia =
    //     (sqrt(B3^2+B2^2), sqrt(B3^2+B1^2), sqrt(B1^2+B2^2)); returns 1.
    static bool ComputeMassProperties(const IVP_SurfaceManager_OutList& leaves,
                                       float outCenterOfMass[3], float outRotationInertia[3]);

    // Retail FUN_0636d650: leaf collection (FUN_0635abf0) + FUN_0636d6f0.
    static bool ComputeSurfaceMassProperties(const void* CompiledSurface,
                                              float outCenterOfMass[3], float outRotationInertia[3]);

    // FUN_0635ac10 (cdecl, leaf, &com, &upper, &aux): folds the farthest
    // vertex distance from com into *upper and, when the aux pointer is
    // non-null, the largest distance of a vertex from the line com + t*N of
    // its triangle (N = FUN_0635b940) into *aux.
    static void AccumulateLeafLedgeRadii(const IVP_Compact_Ledge* leaf, const float centerOfMass[3],
                                          float* upperLimitRadiusAccum, float* auxRadiusMetricAccum);

    // FUN_0635ada0 (cdecl, surface, &com, &upper, &aux): zeroes both
    // outputs, collects the leaves (get_all_ledges into a {0x400} big
    // vector) and runs FUN_0635ac10 on each, descending. com is input only.
    static void ComputeSurfaceRadii(const void* CompiledSurface, const float centerOfMass[3],
                                     float* outUpperLimitRadius, float* outAuxRadiusMetric);

    // FUN_0636b6a0: FUN_0636d650 + FUN_0635ada0, then com / inertia / upper
    // into the compiled surface header and a classification byte into the
    // low byte of +0x1c. See the .cpp.
    void ApplyMassPropertyHeader();

public:
    // FUN_0636a0b0: builds and returns the compact surface (owned by the
    // caller); nullptr when no ledge was inserted.
    // Order: BuildLeafNodes, FinalizeBuildTree,
    // [BuildRootConvexHull], BuildCompactSurface, CompileTree,
    // ApplyMassPropertyHeader, FreeBuildState, optional shrink-copy.
    void* Compile(const void* options = nullptr);
};
static_assert(sizeof(IVP_SurfaceBuilder_Ledge_Soup) == 0x94,
              "must match retail object size -- Common.dll+0x6369e70's ctor zeroes exactly 0x94 bytes (REP STOSD, ECX=0x25 dwords)");
