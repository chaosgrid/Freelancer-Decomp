// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_surface_builder_ledge_soup.h"
#include "ivp_surface_builder_pointsoup.h"
#include "ivp_point_hash.h"
#include "../ivp_utility/ivp_message_router.h"
#include "../ivp_utility/ivp_math.h"
#include "../ivp_collision/ivp_compact_ledge_solver.h" // calc_hesse_vec_object_not_normized (0635b940), get_all_ledges (0635abf0)
#include "../ivp_collision/ivp_compact_surface.h"
#include "../ivp_physics/ivp_surface_manager.h"         // IVP_SurfaceManager_OutList (IVP_U_BigVector)

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>

// Aligned-block allocator pair (FUN_06343fc0 / FUN_06343ff0).
#include "../ivp_utility/ivp_malloc.h"

namespace
{
    // Raw IEEE-754 bits as a float (retail constants are kept bit-exact).
    float BitsToFloat(uint32_t bits)
    {
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    // MSVCRT ftol: truncation of the x87 value. Every use in this file only
    // keeps the low byte (AL), so a 32-bit truncation gives the same byte.
    int32_t RetailFtol(double Value)
    {
        return static_cast<int32_t>(Value);
    }

    // Retail's IVP_U_BigVector on the stack of FUN_0636d650 / FUN_0635ada0:
    // {memsize, n_elems, elems} with a heap buffer of the given capacity
    // (FUN_06343f70 = malloc). get_all_ledges (FUN_0635abf0) grows it through
    // FUN_0635d3e0. The destructor is the inlined clear at :0636d6b4 /
    // :0635ae1f (also the EH unwind funclet): free elems unless it equals
    // header + 0xc or is null.
    struct FLedgeSoupLeafList
    {
        IVP_SurfaceManager_OutList List;

        explicit FLedgeSoupLeafList(int32_t Capacity)
        {
            List.Capacity = Capacity;
            List.Count = 0;
            List.Elems = static_cast<IVP_Compact_Ledge**>(std::malloc(static_cast<size_t>(Capacity) * 4u));
        }

        ~FLedgeSoupLeafList()
        {
            if (List.Elems != reinterpret_cast<IVP_Compact_Ledge**>(&List + 1) && List.Elems != nullptr)
                std::free(List.Elems);
        }

        FLedgeSoupLeafList(const FLedgeSoupLeafList&) = delete;
        FLedgeSoupLeafList& operator=(const FLedgeSoupLeafList&) = delete;
    };

    // Retail .data:DAT_063edcdc, the reflected CRC-32 table (polynomial
    // 0xEDB88320; entry[1] = 0x77073096 in the listing). constexpr so the
    // table is constant-initialised like retail's .data copy and is valid for
    // IVP_PointHashIndex callers in other translation units at any time.
    struct FLedgeSoupCrc32Table
    {
        uint32_t Entry[256];

        constexpr FLedgeSoupCrc32Table()
            : Entry{}
        {
            for (uint32_t I = 0; I < 256; ++I)
            {
                uint32_t C = I;
                for (int32_t K = 0; K < 8; ++K)
                    C = ((C & 1u) != 0) ? (0xEDB88320u ^ (C >> 1)) : (C >> 1);
                Entry[I] = C;
            }
        }
    };

    constexpr FLedgeSoupCrc32Table GLedgeSoupCrc32Table;
}

// Retail FUN_0636cfa0 -- see ivp_point_hash.h.
unsigned IVP_PointHashIndex(const void* Point)
{
    const uint8_t* Bytes = static_cast<const uint8_t*>(Point);
    uint32_t Crc = 0xffffffffu;
    for (int32_t I = 0; I < 12; ++I)
        Crc = GLedgeSoupCrc32Table.Entry[(Bytes[I] ^ Crc) & 0xffu] ^ (Crc >> 8);
    return Crc | 0x80000000u;
}

// Retail FUN_0636cfe0 -- see ivp_point_hash.h.
int IVP_PointHashCompare(const void* A, const void* B)
{
    return std::memcmp(A, B, 12) == 0 ? 1 : 0;
}

namespace
{
    // The point hash of IVP_SurfaceBuilder_Ledge_Soup, vtable .rdata:063a58f8
    // {FUN_0636cfe0 compare, FUN_0636b380 scalar deleting dtor}. Created by
    // FUN_0636b190 as operator_new(0x10) + FUN_063522a0(size) + vtable store.
    // The retail class name is not in the binary.
    class FLedgeSoupPointHash : public IVP_VHash
    {
    public:
        explicit FLedgeSoupPointHash(int Size)
            : IVP_VHash(Size)
        {
        }

        // Slot 0 = FUN_0636cfe0 (IVP_PointHashCompare).
        int Compare(void* Elem0, void* Elem1) override
        {
            return IVP_PointHashCompare(Elem0, Elem1);
        }

        // FUN_0636b380 -> FUN_0636cf90 (vtable store, JMP FUN_063522f0 base
        // dtor), then FUN_06391d5a (operator delete) when bit 0 of the flag is set.
        ~FLedgeSoupPointHash() override
        {
        }
    };
}

// FUN_06369e70: the whole 0x94-byte object zeroed, then the bbox corners set
// to the +-1e6f sentinels.
IVP_SurfaceBuilder_Ledge_Soup::IVP_SurfaceBuilder_Ledge_Soup()
    : m_CompactSurface(nullptr)
    , m_LeafCount(0)
    , m_SplitNodeCount(0)
    , m_MinRadius(0.0f)
    , m_LeafByteTotal(0)
    , m_BuildTreeScratch(nullptr)
    , m_Ledges()
    , m_HullNodes()
    , m_LeafNodes()
    , m_BBoxMin{ BitsToFloat(0x49742400), BitsToFloat(0x49742400), BitsToFloat(0x49742400), 0.0f }
    , m_BBoxMax{ BitsToFloat(0xc9742400), BitsToFloat(0xc9742400), BitsToFloat(0xc9742400), 0.0f }
    , m_SplitAxis(0)
    , m_Unknown54{}
    , m_Unknown58(0)
    , m_VectorD()
    , m_VectorE()
    , m_CompileOptions(nullptr)
    , m_VertexWriteCursor(nullptr)
    , m_VertexDedupBase(nullptr)
    , m_VertexDedupCount(0)
    , m_VertexDedupIndex(nullptr)
    , m_CompactNodeCursor(nullptr)
    , m_Unknown84{}
    , m_AllNodes()
{
}

// FUN_0636a720 (thiscall, RET 0xc, `this` unused): both corners start at the ctor sentinels
// (0x49742400 / 0xc9742400). Per node S = DAT_063ee784 (0x3B83126F) *
// radius stays on the x87 stack; X and Y terms are float(oct * S) spills,
// the Z term (FIMUL) and the Z max candidate stay extended -- see the body.
void IVP_SurfaceBuilder_Ledge_Soup::ComputeNodeListBounds(const IVP_U_Vector<BuildNode>& nodes, float outMin[3], float outMax[3])
{
    outMin[0] = outMin[1] = outMin[2] = BitsToFloat(0x49742400);
    outMax[0] = outMax[1] = outMax[2] = BitsToFloat(0xc9742400);

    const double kExtentScale = (double)(BitsToFloat(0x3B83126Fu)); // DAT_063ee784

    // :0636a74e-0636a851: n_elems (word) re-read every pass; JBE skips 0.
    for (int i = 0; i < nodes.Len(); ++i)
    {
        const BuildNode* node = nodes[i];
        const double Scaled = kExtentScale * node->Radius;                           // stays on the stack

        const float TermX = (float)((double)(node->Octant[0]) * Scaled); // param_2
        const float MinX = (float)((double)(node->CenterX) - TermX);    // local_18
        const float TermY = (float)((double)(node->Octant[1]) * Scaled); // param_3
        const float MinY = (float)((double)(node->CenterY) - TermY);    // local_14
        const double TermZ = Scaled * (double)(node->Octant[2]);                    // FIMUL, kept
        const float MinZ = (float)((double)(node->CenterZ) - TermZ);    // local_10
        const float MaxX = (float)((double)(TermX) + node->CenterX);    // local_c
        const float MaxY = (float)((double)(TermY) + node->CenterY);    // local_8
        const double MaxZ = TermZ + node->CenterZ;                                             // kept

        // Min takes a candidate on less or unordered, max only on greater.
        if (!(MinX >= outMin[0])) outMin[0] = MinX;
        if (MaxX > outMax[0]) outMax[0] = MaxX;
        if (!(MinY >= outMin[1])) outMin[1] = MinY;
        if (MaxY > outMax[1]) outMax[1] = MaxY;
        if (!(MinZ >= outMin[2])) outMin[2] = MinZ;
        if (MaxZ > outMax[2]) outMax[2] = (float)(MaxZ);
    }
}

// FUN_0635aa60: both corners are seeded from the first edge's start point
// (read even when the ledge has no triangles), then every triangle edge's
// start point is folded in per axis: "less (or unordered) -> min, else
// greater -> max". The Z max lives on the x87 stack and its compare is
// reversed, so it also takes an unordered point.
void IVP_SurfaceBuilder_Ledge_Soup::ComputeLedgeBounds(const IVP_Compact_Ledge* ledge, float outMin[3], float outMax[3])
{
    const IVP_Compact_Edge* seedEdge = ledge->GetFirstEdge();
    const IVP_U_Float_Point* seedPoint = ledge->GetPoint(seedEdge);

    outMin[0] = outMax[0] = seedPoint->X;
    outMin[1] = outMax[1] = seedPoint->Y;
    outMin[2] = outMax[2] = seedPoint->Z;

    const int16_t triCount = ledge->GetNTriangles();
    const IVP_Compact_Triangle* triangle = ledge->GetFirstTriangle();
    for (int16_t t = 0; t < triCount; ++t, ++triangle)
    {
        const IVP_Compact_Edge* edge = triangle->GetFirstEdge();
        for (int k = 0; k < 3; ++k, edge = edge->GetNextEdge())
        {
            const IVP_U_Float_Point* p = ledge->GetPoint(edge);

            if (!(p->X >= outMin[0])) outMin[0] = p->X;
            else if (p->X > outMax[0]) outMax[0] = p->X;

            if (!(p->Y >= outMin[1])) outMin[1] = p->Y;
            else if (p->Y > outMax[1]) outMax[1] = p->Y;

            // Z max: replaced when max < p.z or unordered (:0635ab38).
            if (!(p->Z >= outMin[2])) outMin[2] = p->Z;
            else if (!(outMax[2] >= p->Z)) outMax[2] = p->Z;
        }
    }
}

// FUN_0636a350. Compile() only calls it with a non-empty m_Ledges.
//  1. m_LeafByteTotal = 0; n = m_Ledges.n_elems (word, zero-extended);
//     m_LeafCount = n; m_BuildTreeScratch = FUN_06343f80(n+1, 8) (calloc);
//     entry[0].NextIndexOrCount = 1.
//  2. For each ledge i = 0..n-1 (EBP = i+1):
//     a. node = operator_new(0x28) zeroed, appended to m_AllNodes (retail's
//        null-allocation branch is dropped: `new` throws).
//     b. FUN_0635aa60(ledge, &min = local_10, &max = local_30);
//        node.Center = set_interpolate(p1 = max, p2 = min, 0.5f)
//        (FUN_06347e00 at :0636a418).
//     c. v = max - Center as floats (local_20); len = v.real_length()
//        (FUN_06347e60, unrounded); node.Radius = float(len) (FST).
//     d. Class-wide bbox grows by the leaf's sphere. Only the first test
//        (min X) uses the unrounded len; the other five reload the float
//        radius. Min stores on less or unordered, max only on greater.
//     e. scaled = Radius * DAT_063a54b4 (0x3B83126F) on the stack; octant[k] =
//        byte(ftol((max[k] - Center[k]) / scaled)) + 1.
//     f. +0x1c = ledge, +0x20 = +0x24 = 0, ScratchIndex = EBP;
//        m_LeafByteTotal += 0x28; scratch[EBP] = {EBP-1, EBP+1, node}.
//     g. m_MinRadius: copied when it equals 0.0f (or is unordered), else
//        when Radius < m_MinRadius or unordered.
//     h. node appended to m_LeafNodes.
//  3. scratch[EBP-1].NextIndexOrCount = 0 (EBP == n+1 here, entry[0] for n
//     == 0). When the loop counter > 1: extentX stays on the x87 stack,
//     extentY / extentZ are spilled as floats; the cascade (less or
//     unordered) picks m_SplitAxis = 2 / 1 / 2 / 0.
void IVP_SurfaceBuilder_Ledge_Soup::BuildLeafNodes()
{
    m_LeafByteTotal = 0;                                  // :0636a35d
    const int32_t n = m_Ledges.Len();                     // :0636a360 (word)
    m_LeafCount = n;                                      // :0636a36e
    m_BuildTreeScratch = static_cast<BuildTreeScratchEntry*>(std::calloc(static_cast<size_t>(n) + 1, sizeof(BuildTreeScratchEntry))); // FUN_06343f80(n+1, 8)
    BuildTreeScratchEntry* scratch = m_BuildTreeScratch;
    scratch[0].NextIndexOrCount = 1;                      // :0636a383

    const double kExtentScale = (double)(BitsToFloat(0x3B83126Fu)); // DAT_063a54b4

    int32_t ScratchIndex = 1; // EBP
    int32_t LoopIndex = 0;    // local_40
    for (; LoopIndex < n; ++LoopIndex, ++ScratchIndex)
    {
        IVP_Compact_Ledge* ledge = m_Ledges[LoopIndex];

        BuildNode* node = new BuildNode();
        m_AllNodes.Add(node);

        IVP_U_Point LedgeMin; // local_10 (12 bytes used)
        IVP_U_Point LedgeMax; // local_30
        ComputeLedgeBounds(ledge, LedgeMin.k, LedgeMax.k);

        IVP_U_Point* Center = reinterpret_cast<IVP_U_Point*>(&node->CenterX); // ESI+4 (k[0..2] only)
        Center->set_interpolate(&LedgeMax, &LedgeMin, 0.5f);                  // :0636a418

        IVP_U_Point HalfDiagonal; // local_20: max - Center, each spilled as float
        HalfDiagonal.k[0] = LedgeMax.k[0] - node->CenterX;
        HalfDiagonal.k[1] = LedgeMax.k[1] - node->CenterY;
        HalfDiagonal.k[2] = LedgeMax.k[2] - node->CenterZ;
        const double Length = HalfDiagonal.real_length(); // :0636a449, unrounded
        node->Radius = (float)(Length);         // FST [ESI+0x14]

        // :0636a451-0636a4d4: the class-wide bbox grows by the leaf sphere.
        const double CandMinX = (double)(node->CenterX) - Length; // FSUBR [EDI] on the unrounded length
        if (!(CandMinX >= m_BBoxMin.X)) m_BBoxMin.X = (float)(CandMinX);
        const double CandMaxX = (double)(node->Radius) + node->CenterX;
        if (CandMaxX > m_BBoxMax.X) m_BBoxMax.X = (float)(CandMaxX);

        const double CandMinY = (double)(node->CenterY) - node->Radius;
        if (!(CandMinY >= m_BBoxMin.Y)) m_BBoxMin.Y = (float)(CandMinY);
        const double CandMaxY = (double)(node->Radius) + node->CenterY;
        if (CandMaxY > m_BBoxMax.Y) m_BBoxMax.Y = (float)(CandMaxY);

        const double CandMinZ = (double)(node->CenterZ) - node->Radius;
        if (!(CandMinZ >= m_BBoxMin.Z)) m_BBoxMin.Z = (float)(CandMinZ);
        const double CandMaxZ = (double)(node->Radius) + node->CenterZ;
        if (CandMaxZ > m_BBoxMax.Z) m_BBoxMax.Z = (float)(CandMaxZ);

        // :0636a4d6-0636a51a: octant bytes, ratio and division on the x87 stack.
        const double Scaled = (double)(node->Radius) * kExtentScale;
        node->Octant[0] = static_cast<uint8_t>(static_cast<uint8_t>(RetailFtol(((double)(LedgeMax.k[0]) - node->CenterX) / Scaled)) + 1);
        node->Octant[1] = static_cast<uint8_t>(static_cast<uint8_t>(RetailFtol(((double)(LedgeMax.k[1]) - node->CenterY) / Scaled)) + 1);
        node->Octant[2] = static_cast<uint8_t>(static_cast<uint8_t>(RetailFtol(((double)(LedgeMax.k[2]) - node->CenterZ) / Scaled)) + 1);

        m_LeafByteTotal += 0x28;              // :0636a51d
        node->Ledge = ledge;                  // :0636a521
        node->Child0 = nullptr;
        node->Child1 = nullptr;
        node->ScratchIndex = static_cast<uint32_t>(ScratchIndex);
        scratch[ScratchIndex].PrevIndex = static_cast<int16_t>(ScratchIndex - 1);
        scratch[ScratchIndex].NextIndexOrCount = static_cast<int16_t>(ScratchIndex + 1);
        scratch[ScratchIndex].Payload = node;

        // :0636a54a-0636a572: running minimum radius, 0.0f = unset.
        const bool bMinRadiusUnset = !(m_MinRadius < 0.0f) && !(m_MinRadius > 0.0f); // C3: equal or unordered
        if (bMinRadiusUnset)
            m_MinRadius = node->Radius;
        else if (!(node->Radius >= m_MinRadius))
            m_MinRadius = node->Radius;

        m_LeafNodes.Add(node);
    }

    // :0636a5b4: scratch[EBP-1].NextIndexOrCount = 0, unconditionally.
    scratch[ScratchIndex - 1].NextIndexOrCount = 0;

    // :0636a5be: the loop counter (n, or 0 if the loop never ran) against 1.
    if (LoopIndex <= 1)
        return;

    const double ExtentX = (double)(m_BBoxMax.X) - m_BBoxMin.X;                  // stays on the stack
    const float ExtentY = (float)((double)(m_BBoxMax.Y) - m_BBoxMin.Y); // local_3c
    const float ExtentZ = (float)((double)(m_BBoxMax.Z) - m_BBoxMin.Z); // local_40

    if (!(ExtentX >= ExtentY))
        m_SplitAxis = !(ExtentY >= ExtentZ) ? 2 : 1;
    else
        m_SplitAxis = !(ExtentX >= ExtentZ) ? 2 : 0;
}

void IVP_SurfaceBuilder_Ledge_Soup::InsertLedge(IVP_Compact_Ledge* ledge)
{
    // FUN_0636a050. On a null ledge retail does not substitute anything: it
    // reports through the IVP message router (0636a059..0636a07e: category 3,
    // string 063ee78c) and returns. The router never has a category level
    // set, so the message is never printed (ivp_message_router.h).
    if (ledge == nullptr)
    {
        if (G_IVPMessageRouter.IsCategoryEnabled(3) != 0)
        {
            G_IVPMessageRouter.Report(3, "warning: tried to add NULL ledge in IVP_SurfaceBuilder_Ledge_Soup::insert_ledge()\n");
        }
        return;
    }

    m_Ledges.Add(ledge);
}

// FUN_0636b5e0 (thiscall, RET 4). Writes one ledgetree node per
// BuildNode, depth first (left son directly after its parent), and returns it.
// The node's ledge is an IVP_Compact_Ledge inside the compiled blob (repointed
// by MergeLedgesIntoSurface): +4 is its LedgetreeNodeOffset back-link, +8 its
// SizeAndFlags dword whose bits 0..1 are the has-children flag.
IVP_Compact_Ledgetree_Node* IVP_SurfaceBuilder_Ledge_Soup::CompileNode(BuildNode* source)
{
    // :0636b5e5-:0636b5ee -- bump-allocate; the cursor is read before the bump.
    IVP_Compact_Ledgetree_Node* dest = m_CompactNodeCursor;
    m_CompactNodeCursor = dest + 1;

    dest->center[0] = source->CenterX;
    dest->center[1] = source->CenterY;
    dest->center[2] = source->CenterZ;
    dest->radius = source->Radius;
    dest->box_sizes[0] = source->Octant[0];
    dest->box_sizes[1] = source->Octant[1];
    dest->box_sizes[2] = source->Octant[2];
    dest->free_0 = 0;

    if (source->Child0 != nullptr)
    {
        // Inner node: its hull ledge (if any) is linked both ways and flagged
        // has-children = 01.
        IVP_Compact_Ledge* Ledge = source->Ledge;
        if (Ledge == nullptr)
        {
            dest->offset_compact_ledge = 0;
        }
        else
        {
            dest->offset_compact_ledge = (int32_t)(reinterpret_cast<char*>(Ledge) - reinterpret_cast<char*>(dest));
            Ledge->LedgetreeNodeOffset = (int32_t)(reinterpret_cast<char*>(dest) - reinterpret_cast<char*>(Ledge));
            Ledge->SizeAndFlags = (Ledge->SizeAndFlags & ~0x2u) | 0x1u;
        }

        // The left son is the node written right after this one, so the first call's result is not stored; the right son's
        // offset comes from the second call.
        CompileNode(source->Child0);
        IVP_Compact_Ledgetree_Node* RightSon = CompileNode(source->Child1);
        dest->offset_right_node = (int32_t)(reinterpret_cast<char*>(RightSon) - reinterpret_cast<char*>(dest));
    }
    else
    {
        // Terminal node: its ledge gets has-children = 0.
        IVP_Compact_Ledge* Ledge = source->Ledge;
        Ledge->SizeAndFlags &= ~IVP_Compact_Ledge::kHasChildrenMask;
        dest->offset_compact_ledge = (int32_t)(reinterpret_cast<char*>(Ledge) - reinterpret_cast<char*>(dest));
        dest->offset_right_node = 0;
    }

    return dest;
}

// FUN_0636b740: compiles the tree from scratch[scratch[0].NextIndexOrCount]
// and clears the root index. Retail returns 1; the only caller (:0636a11e)
// ignores it.
void IVP_SurfaceBuilder_Ledge_Soup::CompileTree()
{
    BuildTreeScratchEntry* Scratch = m_BuildTreeScratch;
    const uint16_t RootIndex = (uint16_t)Scratch[0].NextIndexOrCount;
    CompileNode(Scratch[RootIndex].Payload);

    Scratch[0].NextIndexOrCount = 0;
}

// FUN_0636a2b0: m_LeafNodes's array only (its nodes are shared with m_AllNodes),
// then every node of m_AllNodes and its array, then m_BuildTreeScratch.
void IVP_SurfaceBuilder_Ledge_Soup::FreeBuildState()
{
    m_LeafNodes.Clear();

    // FUN_06391d5a (operator delete) on every node; the count is re-read
    // each pass.
    for (int i = 0; i < m_AllNodes.Len(); ++i)
        delete m_AllNodes[i];
    m_AllNodes.Clear();

    if (m_BuildTreeScratch != nullptr)
    {
        free(m_BuildTreeScratch);
        m_BuildTreeScratch = nullptr;
    }
}

// FUN_0636a860. Dispatch on the list's n_elems (word): == 1 returns elems[0].
//
// Shared internal-node setup:
//  - FUN_0636a720(list, &m_BBoxMin, &m_BBoxMax): the class-wide bbox fields
//    are reused as scratch.
//  - local_78 = set_interpolate(p1 = max, p2 = min, 0.5f) (FUN_06347e00);
//    its 4th dword is never written in retail (uninitialised stack).
//  - v = max - center as floats (local_3c); radius = float(real_length(v))
//    (local_b8); scaled = float(DAT_063ee788 * radius) (local_c4).
//  - node = operator_new(0x28) zeroed, appended to m_AllNodes; +0x14 =
//    radius, +0x00 = 0, +0x04..+0x10 = the 4 dwords of local_78 (the 4th
//    is retail stack garbage -- deterministic 0 here, see the body),
//    +0x1c = 0.
//  - octant[k] = byte(ftol((max[k] - center[k]) / scaled)) + 1, ratio on
//    the x87 stack (divided by the float `scaled`).
// n_elems == 2: two zeroed stack vectors A / B get
// elems[0] / elems[1] (FUN_063441a0 grow), Child0 = recurse(A), Child1 =
// recurse(B), ++m_SplitNodeCount, both vectors cleared (FUN_063449d0, B
// first) -- the destructors below.
// n_elems > 2:
//  - six operator_new(8) headers {memsize = n, n_elems = 0, elems =
//    malloc(n*4) or null}: low[0..2] (local_a8/a4/a0), high[0..2]
//    (local_98/94/90). After the last one the EH state is -1: retail does
//    not unwind them on an exception.
//  - per axis: sum = 0.0f + each center[axis] on the x87 stack (ascending);
//    mean = float(sum / n); low = float(mean - eps), high =
//    float(mean + eps) (DAT_063a5610). preferLow (local_b8) = 1.
//    Each element i: v < low (or unordered) -> low; v > high -> high; else
//    neighbour = elems[i-1] if i == n-1 else elems[i+1]: neighbour < low
//    (or unordered) -> high; neighbour > high -> low; else low empty ->
//    low; high empty -> high; preferLow -> low (preferLow = 0); else high
//    (preferLow = 1).
//  - FUN_0636a720(low, &lmin, &lmax), FUN_0636a720(high, &hmin, &hmax);
//    score[axis] = float(((|hz|*|hy|)*|hx|) + ((|lz|*|ly|)*|lx|)), every
//    extent and product on the x87 stack.
//  - best = DAT_063a58cc (float), chosen = 0; for i = 0..2: when best >
//    score[i]: chosen = i if both buckets are
//    non-empty, and best = score[i] regardless.
//  - Child0 = recurse(low[chosen]), Child1 = recurse(high[chosen]); the six
//    headers are cleared and operator-deleted in allocation order;
//    ++m_SplitNodeCount; returns the node.
IVP_SurfaceBuilder_Ledge_Soup::BuildNode* IVP_SurfaceBuilder_Ledge_Soup::SplitBuildNodeList(IVP_U_Vector<BuildNode>& list)
{
    if (list.n_elems == 1)
        return list[0];

    // ---- Shared internal-node setup (:0636a89d-0636a9df). ----
    ComputeNodeListBounds(list, &m_BBoxMin.X, &m_BBoxMax.X); // FUN_0636a720(list, this+0x30, this+0x40)

    const IVP_U_Point* ScratchMin = reinterpret_cast<const IVP_U_Point*>(&m_BBoxMin);
    const IVP_U_Point* ScratchMax = reinterpret_cast<const IVP_U_Point*>(&m_BBoxMax);

    // local_78. Its 4th dword is never written by set_interpolate and is
    // copied into node+0x10 below: retail copies uninitialised stack there
    // (no reader of BuildNode+0x10 exists); the port copies a zero.
    IVP_U_Point Center = {};
    Center.set_interpolate(ScratchMax, ScratchMin, 0.5f);            // :0636a8b8

    IVP_U_Point HalfDiagonal; // local_3c: max - center, each spilled as float
    HalfDiagonal.k[0] = ScratchMax->k[0] - Center.k[0];
    HalfDiagonal.k[1] = ScratchMax->k[1] - Center.k[1];
    HalfDiagonal.k[2] = ScratchMax->k[2] - Center.k[2];
    const float Radius = (float)(HalfDiagonal.real_length());                                  // :0636a8fe, FSTP local_b8
    const float Scaled = (float)((double)(BitsToFloat(0x3B83126Fu)) * Radius);       // DAT_063ee788, FSTP local_c4

    BuildNode* node = new BuildNode();
    m_AllNodes.Add(node);

    node->Radius = Radius;                                            // :0636a969
    node->ScratchIndex = 0;                                           // :0636a96c
    node->CenterX = Center.k[0];                                      // :0636a972-0636a98d: 4 dwords
    node->CenterY = Center.k[1];
    node->CenterZ = Center.k[2];
    std::memcpy(&node->m_Unknown10, &Center.k[3], sizeof(node->m_Unknown10));
    node->Ledge = nullptr;                                            // :0636a990

    node->Octant[0] = static_cast<uint8_t>(static_cast<uint8_t>(RetailFtol(((double)(ScratchMax->k[0]) - Center.k[0]) / Scaled)) + 1);
    node->Octant[1] = static_cast<uint8_t>(static_cast<uint8_t>(RetailFtol(((double)(ScratchMax->k[1]) - Center.k[1]) / Scaled)) + 1);
    node->Octant[2] = static_cast<uint8_t>(static_cast<uint8_t>(RetailFtol(((double)(ScratchMax->k[2]) - Center.k[2]) / Scaled)) + 1);

    if (list.n_elems == 2)
    {
        IVP_U_Vector<BuildNode> BucketA; // local_c0
        IVP_U_Vector<BuildNode> BucketB; // local_cc
        BucketA.Add(list[0]);
        BucketB.Add(list[1]);

        node->Child0 = SplitBuildNodeList(BucketA);
        node->Child1 = SplitBuildNodeList(BucketB);

        ++m_SplitNodeCount;
        return node; // FUN_063449d0 on B, then A: the destructors (reverse order)
    }

    const int32_t Count = list.n_elems;
    IVP_U_Vector<BuildNode>* BucketLow[3];  // local_a8 / a4 / a0
    IVP_U_Vector<BuildNode>* BucketHigh[3]; // local_98 / 94 / 90
    BucketLow[0] = new IVP_U_Vector<BuildNode>(Count);
    BucketLow[1] = new IVP_U_Vector<BuildNode>(Count);
    BucketLow[2] = new IVP_U_Vector<BuildNode>(Count);
    BucketHigh[0] = new IVP_U_Vector<BuildNode>(Count);
    BucketHigh[1] = new IVP_U_Vector<BuildNode>(Count);
    BucketHigh[2] = new IVP_U_Vector<BuildNode>(Count);

    const float kMeanEpsilon = BitsToFloat(0x358637BDu); // DAT_063a5610
    float Score[3];                                      // local_68

    for (int32_t Axis = 0; Axis < 3; ++Axis)
    {
        const int32_t N = list.n_elems;
        double Sum = (double)(BitsToFloat(0x00000000u)); // DAT_063a53d4
        for (int32_t I = 0; I < N; ++I)
            Sum += GetCenterAxis(list[I], Axis);
        const float Mean = (float)(Sum / (double)(N)); // local_80

        if (N > 0)
        {
            const float LowThresh = (float)((double)(Mean) - kMeanEpsilon);  // local_8c
            int32_t bPreferLow = 1;                                                              // local_b8

            for (int32_t I = 0; I < static_cast<int32_t>(list.n_elems); ++I)
            {
                BuildNode* Element = list[I];
                const float V = GetCenterAxis(Element, Axis);

                // Less or unordered.
                if (!(V >= LowThresh))
                {
                    BucketLow[Axis]->Add(Element);
                    continue;
                }

                const float HighThresh = (float)((double)(Mean) + kMeanEpsilon); // local_b4
                if (V > HighThresh)
                {
                    BucketHigh[Axis]->Add(Element);
                    continue;
                }

                const int32_t NeighbourIndex = (I == static_cast<int32_t>(list.n_elems) - 1) ? (I - 1) : (I + 1);
                const float NeighbourV = GetCenterAxis(list[NeighbourIndex], Axis);

                if (!(NeighbourV >= LowThresh))
                {
                    BucketHigh[Axis]->Add(Element);
                }
                else if (NeighbourV > HighThresh)
                {
                    BucketLow[Axis]->Add(Element);
                }
                else if (BucketLow[Axis]->n_elems == 0)
                {
                    BucketLow[Axis]->Add(Element);
                }
                else if (BucketHigh[Axis]->n_elems == 0)
                {
                    BucketHigh[Axis]->Add(Element);
                }
                else if (bPreferLow != 0)
                {
                    BucketLow[Axis]->Add(Element);
                    bPreferLow = 0;
                }
                else
                {
                    BucketHigh[Axis]->Add(Element);
                    bPreferLow = 1;
                }
            }
        }

        float LowMin[3];  // local_2c
        float LowMax[3];  // local_5c
        float HighMin[3]; // local_1c
        float HighMax[3]; // local_4c
        ComputeNodeListBounds(*BucketLow[Axis], LowMin, LowMax);
        ComputeNodeListBounds(*BucketHigh[Axis], HighMin, HighMax);

        const double Lx = std::fabs((double)(LowMax[0]) - LowMin[0]);
        const double Ly = std::fabs((double)(LowMax[1]) - LowMin[1]);
        const double Lz = std::fabs((double)(LowMax[2]) - LowMin[2]);
        const double Hx = std::fabs((double)(HighMax[0]) - HighMin[0]);
        const double Hy = std::fabs((double)(HighMax[1]) - HighMin[1]);
        const double Hz = std::fabs((double)(HighMax[2]) - HighMin[2]);
        const double HighVolume = (Hz * Hy) * Hx;
        const double LowVolume = (Lz * Ly) * Lx;
        Score[Axis] = (float)(HighVolume + LowVolume);
    }

    int32_t ChosenAxis = 0;                              // ESI
    float RunningBest = BitsToFloat(0x60AD78ECu);        // DAT_063a58cc
    for (int32_t Axis = 0; Axis < 3; ++Axis)
    {
        if (RunningBest > Score[Axis])
        {
            if (BucketLow[Axis]->n_elems != 0 && BucketHigh[Axis]->n_elems != 0)
                ChosenAxis = Axis;
            RunningBest = Score[Axis];
        }
    }

    node->Child0 = SplitBuildNodeList(*BucketLow[ChosenAxis]);   // :0636b025
    node->Child1 = SplitBuildNodeList(*BucketHigh[ChosenAxis]);  // :0636b038

    // Inlined clear + operator delete, in allocation order.
    delete BucketLow[0];
    delete BucketLow[1];
    delete BucketLow[2];
    delete BucketHigh[0];
    delete BucketHigh[1];
    delete BucketHigh[2];

    ++m_SplitNodeCount; // :0636b16c
    return node;
}

// FUN_0636a640: every payload on the scratch chain (from entry[0]'s root
// index, unconditionally) goes into a local list; m_SplitNodeCount =
// m_LeafCount; the list is reduced by SplitBuildNodeList() into the root slot.
// The float argument is never read.
void IVP_SurfaceBuilder_Ledge_Soup::FinalizeBuildTree(float /*unusedThreshold*/)
{
    IVP_U_Vector<BuildNode> localList;

    BuildTreeScratchEntry* scratch = m_BuildTreeScratch;

    // The chain indices are read zero-extended.
    for (uint16_t idx = (uint16_t)scratch[0].NextIndexOrCount; idx != 0; )
    {
        localList.Add(scratch[idx].Payload);
        idx = (uint16_t)scratch[idx].NextIndexOrCount;
    }

    m_SplitNodeCount = m_LeafCount;

    BuildNode* result = SplitBuildNodeList(localList);

    // The root index is re-read (zero-extended) after the call.
    scratch[(uint16_t)scratch[0].NextIndexOrCount].Payload = result;

    localList.Clear();
}

// FUN_0636b3a0, once per build node from MergeLedgesIntoSurface().
//  - No vertex hash (+0x7c == 0): copies (src[+8] >> 8) << 4 bytes and
//    returns that size (re-read from src[+8]).
//  - Vertex hash: copies (src.NTriangles + 1) * 16 bytes (header and
//    triangles only); then for every triangle of the copy (count read from
//    the copy, triangles ascending) and each of its 3 edges: the source point
//    srcPoints + (edge & 0xffff) * 16 is looked up in the hash
//    (FUN_0636cfa0 index, FUN_06352580 find); found -> the edge's start index
//    becomes (found - base) >> 4 (arithmetic, masked to 16 bits); not found -> start
//    index = [+0x78], the point (16 bytes) is copied to base + [+0x78] * 16,
//    [+0x78]++ and the copy is inserted (FUN_063523d0). Finally
//    dest.CPointOffset = base - dest and the header size is returned.
uint32_t IVP_SurfaceBuilder_Ledge_Soup::CopyLedgeIntoSurface(const IVP_Compact_Ledge* sourceLedge, IVP_Compact_Ledge* destCursor)
{
    const IVP_Compact_Ledge* Source = sourceLedge;
    if (m_VertexDedupIndex == nullptr)
    {
        const uint32_t ByteSize = (Source->SizeAndFlags >> 8) << 4;
        std::memcpy(destCursor, sourceLedge, ByteSize);
        return (Source->SizeAndFlags >> 8) << 4;
    }

    const uint32_t HeaderBytes = static_cast<uint32_t>(static_cast<int32_t>(Source->NTriangles) + 1) << 4;
    std::memcpy(destCursor, sourceLedge, HeaderBytes);

    const uint8_t* SourcePoints = reinterpret_cast<const uint8_t*>(Source) + Source->CPointOffset;
    IVP_Compact_Ledge* Dest = destCursor;
    uint8_t* PointBase = m_VertexDedupBase;

    const int32_t TriangleCount = Dest->NTriangles;
    IVP_Compact_Edge* TriangleEdges = Dest->GetFirstEdge();
    for (int32_t T = 0; T < TriangleCount; ++T, TriangleEdges += 4) // to the next triangle's first edge
    {
        IVP_Compact_Edge* Edge = TriangleEdges;
        for (int32_t K = 0; K < 3; ++K, ++Edge)
        {
            uint8_t* SourcePoint = const_cast<uint8_t*>(SourcePoints + ((Edge->Data & 0xffffu) << 4));
            void* Found = m_VertexDedupIndex->FindElem(SourcePoint, IVP_PointHashIndex(SourcePoint));
            if (Found != nullptr)
            {
                const int32_t ByteOffset = static_cast<int32_t>(static_cast<uint8_t*>(Found) - PointBase);
                Edge->Data = (Edge->Data & 0xffff0000u) ^ (static_cast<uint32_t>(ByteOffset >> 4) & 0xffffu);
            }
            else
            {
                Edge->Data = (Edge->Data & 0xffff0000u) | (m_VertexDedupCount & 0xffffu);
                uint8_t* NewPoint = PointBase + (m_VertexDedupCount << 4);
                ++m_VertexDedupCount;
                std::memcpy(NewPoint, SourcePoint, 16);
                m_VertexDedupIndex->AddElem(NewPoint, IVP_PointHashIndex(NewPoint));
            }
        }
    }

    Dest->CPointOffset = static_cast<int32_t>(PointBase - reinterpret_cast<uint8_t*>(Dest));
    return HeaderBytes;
}

// FUN_0636b520, called from BuildCompactSurface().
//  - m_LeafNodes (only when options[2] == 0): each node's Ledge is replaced by
//    the output cursor before the copy, CopyLedgeIntoSurface() advances the
//    cursor, and the source ledge is freed (FUN_06343ff0) when options[1]
//    == 1 and it is non-null.
//  - m_HullNodes (the root hull of BuildRootConvexHull()) the same way, with
//    no options tests: the source is freed whenever it is non-null.
//  - Then m_Ledges is cleared.
void IVP_SurfaceBuilder_Ledge_Soup::MergeLedgesIntoSurface()
{
    const int32_t* options = m_CompileOptions;
    uint8_t* cursor = m_VertexWriteCursor;

    if (options[2] == 0)
    {
        for (int i = 0; i < m_LeafNodes.Len(); ++i)
        {
            BuildNode* node = m_LeafNodes[i];
            IVP_Compact_Ledge* sourceLedge = node->Ledge;
            node->Ledge = reinterpret_cast<IVP_Compact_Ledge*>(cursor);
            const uint32_t size = CopyLedgeIntoSurface(sourceLedge, node->Ledge);
            cursor += size;
            if (options[1] == 1 && sourceLedge != nullptr)
                ivp_free_aligned(sourceLedge); // FUN_06343ff0
        }
    }

    for (int i = 0; i < m_HullNodes.Len(); ++i)
    {
        BuildNode* node = m_HullNodes[i];
        IVP_Compact_Ledge* sourceLedge = node->Ledge;
        node->Ledge = reinterpret_cast<IVP_Compact_Ledge*>(cursor);
        const uint32_t size = CopyLedgeIntoSurface(sourceLedge, node->Ledge);
        cursor += size;
        if (sourceLedge != nullptr)
            ivp_free_aligned(sourceLedge); // FUN_06343ff0
    }

    m_Ledges.Clear();
}

// FUN_0636b190, Compile()'s fourth callee. Header packing: blob[+0x1c] =
// ((dataBytes + splitNodeBytes + 0x30) << 8) | the low byte already at
// blob[+0x1c] (uninitialised: FUN_06343fc0 does not zero, reproduced as
// retail). blob[+0x20] = dataBytes + 0x30 (the ledgetree node array, without
// splitNodeBytes); blob[+0x24..+0x2c] = 0; m_CompactNodeCursor = blob +
// blob[+0x20].
bool IVP_SurfaceBuilder_Ledge_Soup::BuildCompactSurface()
{
    const int32_t* options = m_CompileOptions;

    uint32_t byteTotal = 0;       // EBP: summed (ledge[+8] >> 8) << 4.
    int32_t TriangleTotal = 0;    // EDI: summed ledge NTriangles (MOVSX).
    int32_t totalNodeCount = 0;   // local_18

    // m_LeafNodes only when options[2] == 0.
    if (options[2] == 0)
    {
        const int32_t CountC = m_LeafNodes.Len();
        if (CountC > 0)
        {
            totalNodeCount = CountC;
            for (int32_t i = 0; i < CountC; ++i)
            {
                const IVP_Compact_Ledge* ledge = m_LeafNodes.elems[i]->Ledge;
                byteTotal += (ledge->SizeAndFlags >> 8) << 4;
                TriangleTotal += ledge->NTriangles;
            }
        }
    }

    // m_HullNodes always.
    const int32_t CountB = m_HullNodes.Len();
    if (CountB > 0)
    {
        totalNodeCount += CountB;
        for (int32_t i = 0; i < CountB; ++i)
        {
            const IVP_Compact_Ledge* ledge = m_HullNodes.elems[i]->Ledge;
            byteTotal += (ledge->SizeAndFlags >> 8) << 4;
            TriangleTotal += ledge->NTriangles;
        }
    }

    const uint32_t splitNodeBytes = (uint32_t)m_SplitNodeCount * sizeof(IVP_Compact_Ledgetree_Node);
    const uint32_t allocSize = (splitNodeBytes + byteTotal + 0x3f) & ~static_cast<uint32_t>(0xF);

    void* blob = ivp_malloc_aligned(allocSize, 0x10); // FUN_06343fc0
    m_CompactSurface = blob;
    if (blob == nullptr)
        return false;

    // The vertex hash is built when more than one
    // ledge is merged with options[2] == 0 and options[3] in {1, 2}; with
    // another options[3] value +0x74 / +0x7c are left as they are.
    if (totalNodeCount > 1 && options[2] == 0)
    {
        const int32_t Mode = options[3];
        if (Mode == 1 || Mode == 2)
        {
            int32_t HashSize = 2;
            if (TriangleTotal != 0)
            {
                int32_t Rest = TriangleTotal;
                do
                {
                    Rest >>= 1;
                    HashSize <<= 1;
                } while (Rest != 0);
            }
            m_VertexDedupIndex = new FLedgeSoupPointHash(HashSize);
            m_VertexDedupBase = static_cast<uint8_t*>(blob) + ((totalNodeCount + TriangleTotal + 3) << 4);
        }
    }
    else
    {
        m_VertexDedupBase = nullptr;
        m_VertexDedupIndex = nullptr;
    }

    m_VertexWriteCursor = static_cast<uint8_t*>(blob) + 0x30;
    m_VertexDedupCount = 0;

    MergeLedgesIntoSurface(); // FUN_0636b520

    // With a hash, delete it (slot 1, flag 1) and
    // size the data area as (pooled points + ledges + triangles) * 16.
    uint32_t leadingOffset;
    if (m_VertexDedupIndex != nullptr)
    {
        delete m_VertexDedupIndex;
        leadingOffset = (m_VertexDedupCount + static_cast<uint32_t>(totalNodeCount) + static_cast<uint32_t>(TriangleTotal)) << 4;
        m_VertexDedupIndex = nullptr;
    }
    else
    {
        leadingOffset = byteTotal;
    }

    uint8_t* blobHeader = static_cast<uint8_t*>(blob);

    // Uninitialised low byte, read as retail does (see above).
    const uint32_t existingLowByte = *reinterpret_cast<uint32_t*>(blobHeader + 0x1c) & 0xFFu;
    const uint32_t packedTotalSize = leadingOffset + splitNodeBytes + 0x30;
    *reinterpret_cast<uint32_t*>(blobHeader + 0x1c) = (packedTotalSize << 8) | existingLowByte;

    const uint32_t treeArrayOffset = leadingOffset + 0x30; // without splitNodeBytes
    *reinterpret_cast<int32_t*>(blobHeader + 0x20) = static_cast<int32_t>(treeArrayOffset);
    *reinterpret_cast<int32_t*>(blobHeader + 0x24) = 0;
    *reinterpret_cast<int32_t*>(blobHeader + 0x28) = 0;
    *reinterpret_cast<int32_t*>(blobHeader + 0x2c) = 0;

    m_CompactNodeCursor = reinterpret_cast<IVP_Compact_Ledgetree_Node*>(blobHeader + treeArrayOffset);

    return true;
}

// FUN_06348040 / FUN_06348070: components walked z, y, x. Min copies when
// other < this or unordered; max only when other > this.
void IVP_SurfaceBuilder_Ledge_Soup::ComponentwiseMin3(float inout[3], const float other[3])
{
    for (int i = 2; i >= 0; --i)
    {
        if (!(other[i] >= inout[i]))
            inout[i] = other[i];
    }
}

void IVP_SurfaceBuilder_Ledge_Soup::ComponentwiseMax3(float inout[3], const float other[3])
{
    for (int i = 2; i >= 0; --i)
    {
        if (other[i] > inout[i])
            inout[i] = other[i];
    }
}

// LAB_0636d866, the tail of FUN_0636d6f0:
//  - FUN_0635aa60(leaves[0], &min, &max) seeds the box;
//  - EDI = count-1; while (EDI > 0) { FUN_0635aa60(leaves[EDI], &lmin, &lmax);
//    FUN_06348040(min, lmin); FUN_06348070(max, lmax); --EDI; };
//  - FUN_06347e00(centerOfMass, min, max, 0.5f);
//  - d_i = min_i - max_i; len = sqrt(d2*d2 + d1*d1 + d0*d0) on the x87
//    stack; v = len*0.5f; v = v*v; v = v*0.5f (DAT_063a5460); that one
//    value is stored as a float into all three inertia components.
void IVP_SurfaceBuilder_Ledge_Soup::ComputeBoundingSphereMassPropertiesFallback(
    const IVP_SurfaceManager_OutList& leaves,
    float outCenterOfMass[3], float outRotationInertia[3])
{
    IVP_U_Point BBoxMin; // local_8c
    IVP_U_Point BBoxMax; // local_9c
    ComputeLedgeBounds(leaves.Elems[0], BBoxMin.k, BBoxMax.k);

    for (int32_t i = leaves.Count - 1; i > 0; --i)
    {
        float leafMin[3]; // local_78
        float leafMax[3]; // local_10
        ComputeLedgeBounds(leaves.Elems[i], leafMin, leafMax);
        ComponentwiseMin3(BBoxMin.k, leafMin);
        ComponentwiseMax3(BBoxMax.k, leafMax);
    }

    // :0636d8dd FUN_06347e00(this = com; p1 = min, p2 = max, 0.5f): writes k[0..2] only.
    reinterpret_cast<IVP_U_Point*>(outCenterOfMass)->set_interpolate(&BBoxMin, &BBoxMax, 0.5f);
    const float* bboxMin = BBoxMin.k;
    const float* bboxMax = BBoxMax.k;

    const double D0 = (double)(bboxMin[0]) - (double)(bboxMax[0]);
    const double D1 = (double)(bboxMin[1]) - (double)(bboxMax[1]);
    const double D2 = (double)(bboxMin[2]) - (double)(bboxMax[2]);
    double Value = std::sqrt(D2 * D2 + D1 * D1 + D0 * D0);
    Value = Value * 0.5;
    Value = Value * Value;
    Value = Value * 0.5;
    const float Isotropic = (float)(Value);
    outRotationInertia[0] = Isotropic;
    outRotationInertia[1] = Isotropic;
    outRotationInertia[2] = Isotropic;
}

// FUN_0636d360, thiscall on the accumulator,
// (ledge, triangle), RET 8. Straight-line code:
//   P0 = edge0, P1 = next(edge0), P2 = prev(edge0).
//   b = P2 - P0 spilled as floats; a = P1 - P0 kept on the x87 stack.
//   n.x = float(a.y*b.z - a.z*b.y), n.y = float(a.z*b.x - a.x*b.z) are
//   spilled, n.z = a.x*b.y - a.y*b.x stays on the stack.
//   D = float((n.z*z0 + n.x*x0) + n.y*y0).
//   acc+0x10 = float(((n.z^2 + n.y^2) + n.x^2) + acc+0x10);
//   acc+0x14 = float(D + acc+0x14).
//   q = D * 0.25f (DAT_063a5674) stays on the stack; for P0, P1, P2 in turn
//   every moment component becomes float(q*p + moment), each rounded once.
void IVP_SurfaceBuilder_Ledge_Soup::AccumulateTriangleVolume(FVolumeIntegralAccumulator& Acc, const IVP_Compact_Ledge* Ledge,
                                                             const IVP_Compact_Triangle* Triangle)
{
    const IVP_Compact_Edge* Edge0 = Triangle->GetFirstEdge();
    const IVP_U_Float_Point* P0 = Ledge->GetPoint(Edge0);
    const IVP_U_Float_Point* P1 = Ledge->GetPoint(Edge0->GetNextEdge());
    const IVP_U_Float_Point* P2 = Ledge->GetPoint(Edge0->GetPrevEdge());

    const float Bx = (float)((double)(P2->X) - P0->X); // local_14
    const float By = (float)((double)(P2->Y) - P0->Y); // local_18
    const float Bz = (float)((double)(P2->Z) - P0->Z); // local_1c
    const double Ax = (double)(P1->X) - P0->X;
    const double Ay = (double)(P1->Y) - P0->Y;
    const double Az = (double)(P1->Z) - P0->Z;

    const float Nx = (float)(Ay * Bz - Az * By); // local_10
    const float Ny = (float)(Az * Bx - Ax * Bz); // local_c
    const double Nz = Ax * By - Ay * Bx;

    const float D = (float)((Nz * P0->Z + (double)(Nx) * P0->X) + (double)(Ny) * P0->Y);

    Acc.NormalQuadSum = (float)(((Nz * Nz + (double)(Ny) * Ny) + (double)(Nx) * Nx) +
                                           Acc.NormalQuadSum);
    Acc.Volume6 = (float)((double)(D) + Acc.Volume6);

    const double Q = (double)(D) * (double)(BitsToFloat(0x3E800000u)); // DAT_063a5674 = 0.25f
    const IVP_U_Float_Point* const Points[3] = { P0, P1, P2 };
    for (int32_t I = 0; I < 3; ++I)
    {
        const float NewY = (float)(Q * Points[I]->Y + Acc.Moment[1]);
        const float NewZ = (float)(Q * Points[I]->Z + Acc.Moment[2]);
        const float NewX = (float)(Q * Points[I]->X + Acc.Moment[0]);
        Acc.Moment[1] = NewY;
        Acc.Moment[2] = NewZ;
        Acc.Moment[0] = NewX;
    }
}

// FUN_0636d510, thiscall on the accumulator, (ledge), RET 4: triangles
// ascending; the signed word count is re-read every pass.
void IVP_SurfaceBuilder_Ledge_Soup::AccumulateLedgeVolume(FVolumeIntegralAccumulator& Acc, const IVP_Compact_Ledge* Ledge)
{
    const IVP_Compact_Triangle* Triangle = Ledge->GetFirstTriangle(); // EBX = ledge + 0x10
    for (int32_t I = 0; I < static_cast<int32_t>(Ledge->GetNTriangles()); ++I, ++Triangle)
        AccumulateTriangleVolume(Acc, Ledge, Triangle);
}

// FUN_0636d550, thiscall on the accumulator, (leaves), RET 4: leaves
// descending.
void IVP_SurfaceBuilder_Ledge_Soup::AccumulateVolumeIntegral(FVolumeIntegralAccumulator& Acc, const IVP_SurfaceManager_OutList& Leaves)
{
    for (int32_t I = Leaves.Count - 1; I >= 0; --I)
        AccumulateLedgeVolume(Acc, Leaves.Elems[I]);
}

// FUN_0636d040, thiscall on the 4-dword {Csys, mass, sumA, sumB}
// accumulator, (ledge, triangle, A, B, C), RET 0x14. Every term below is in
// retail's x87 order: stack values are double, spills are float.
void IVP_SurfaceBuilder_Ledge_Soup::AccumulateTriangleAxisIntegral(FAxisIntegralAccumulator& Acc, const IVP_Compact_Ledge* Ledge,
                                                                   const IVP_Compact_Triangle* Triangle,
                                                                   int32_t AxisA, int32_t AxisB, int32_t AxisC)
{
    const double kEpsilon = (double)(BitsToFloat(0x358637BDu));   // DAT_063a5610
    const double kHalf = (double)(BitsToFloat(0x3F000000u));      // DAT_063a5460
    const double kMinusHalf = (double)(BitsToFloat(0xBF000000u)); // DAT_063a56f0
    const double kQuarter = (double)(BitsToFloat(0x3E800000u));   // DAT_063a5674
    const double kOneThird = (double)(BitsToFloat(0x3EAAAAABu));  // DAT_063a58f4
    const double kOneFifth = (double)(BitsToFloat(0x3E4CCCCDu));  // DAT_063a5748

    // The face normal of edge0, normalised in place (the normize result is
    // not tested).
    const IVP_Compact_Edge* Edge0 = Triangle->GetFirstEdge(); // EDI
    IVP_U_Point Normal;                                       // local_10
    IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(Edge0, Ledge, &Normal);
    Normal.normize();

    // |n[C]| < |n[B]| (or unordered) -> slope over B; otherwise slope over C
    // when |n[C]| > eps.
    int32_t bUseSlopeOverC; // local_48
    float SlopeCOverB;      // local_58
    float SlopeBOverC;      // param_1 slot
    const double AbsC = std::fabs((double)(Normal.k[AxisC]));
    const double AbsB = std::fabs((double)(Normal.k[AxisB]));
    if (!(AbsC >= AbsB))
    {
        bUseSlopeOverC = 0;
        SlopeBOverC = 0.0f;
        SlopeCOverB = (float)((double)(Normal.k[AxisC]) * kHalf / Normal.k[AxisB]);
    }
    else
    {
        bUseSlopeOverC = 1;
        if (AbsC > kEpsilon)
            SlopeBOverC = (float)((double)(Normal.k[AxisB]) * kMinusHalf / Normal.k[AxisC]);
        else
            SlopeBOverC = 0.0f;
        SlopeCOverB = 0.0f;
    }

    float TriMass = 0.0f; // local_54
    float TriSumA = 0.0f; // local_50
    float TriSumB = 0.0f; // local_4c

    // The three edges of the triangle.
    for (int32_t K = 0; K < 3; ++K)
    {
        const IVP_Compact_Edge* CurEdge = Triangle->GetEdge(K);
        IVP_U_Point Cur; // local_30
        IVP_U_Point Nxt; // local_20
        Acc.Csys->vimult4(reinterpret_cast<const IVP_U_Point*>(Ledge->GetPoint(CurEdge)), &Cur);                 // :0636d143
        Acc.Csys->vimult4(reinterpret_cast<const IVP_U_Point*>(Ledge->GetPoint(CurEdge->GetNextEdge())), &Nxt); // :0636d151

        IVP_U_Point EdgeVec; // local_40, floats
        EdgeVec.k[0] = Nxt.k[0] - Cur.k[0];
        EdgeVec.k[1] = Nxt.k[1] - Cur.k[1];
        EdgeVec.k[2] = Nxt.k[2] - Cur.k[2];

        // The edge is skipped when |E| * eps > float(|E[A]|).
        const float AbsEA = (float)(std::fabs((double)(EdgeVec.k[AxisA])));
        if (EdgeVec.real_length() * kEpsilon > AbsEA)
            continue;

        const double M = (double)(EdgeVec.k[AxisB]) / EdgeVec.k[AxisA];
        const float MF = (float)(M);                       // FST param_2
        const double B0 = (double)(Cur.k[AxisB]) - M * Cur.k[AxisA];
        const float B0F = (float)(B0);                     // FSTP local_64

        double V;
        double W;
        double Y;
        if (bUseSlopeOverC != 0)
        {
            Y = ((double)(B0F) * B0F) * SlopeBOverC;
            const double T = ((double)(B0F) * MF) * SlopeBOverC;
            W = T + T;
            V = ((double)(MF) * MF) * SlopeBOverC;
        }
        else
        {
            const double M2 = (double)(EdgeVec.k[AxisC]) / EdgeVec.k[AxisA];
            const float M2F = (float)(M2);                 // FST local_60
            const double B0C = (double)(Cur.k[AxisC]) - M2 * Cur.k[AxisA];
            const float B0CF = (float)(B0C);               // FST local_5c
            Y = (B0C * SlopeCOverB + B0F) * B0CF;
            const double T = ((double)(B0CF) * M2F) * SlopeCOverB;
            W = ((T + T) + (double)(M2F) * B0F) + (double)(B0CF) * MF;
            V = ((double)(M2F) * SlopeCOverB + MF) * M2F;
        }

        const double CA = Cur.k[AxisA];
        const double NA = Nxt.k[AxisA];
        const double NA2 = NA * NA;
        const float CA2 = (float)(CA * CA);                              // param_2
        const float S1 = (float)((NA2 - CA2) * kHalf);                   // local_60
        const double NA3 = NA2 * NA;
        const float CA3 = (float)((double)(CA2) * CA);        // local_5c
        const float S2 = (float)((NA3 - CA3) * kOneThird);               // param_2
        const double NA4 = NA3 * NA;
        const float CA4 = (float)((double)(CA3) * CA);        // local_5c
        const float S3 = (float)((NA4 - CA4) * kQuarter);                // local_64

        const double DA = NA - CA;
        TriMass = (float)(((DA * Y + (double)(S2) * V) + (double)(S1) * W) + TriMass);
        TriSumA = (float)((((double)(S3) * V + (double)(S2) * W) + (double)(S1) * Y) + TriSumA);

        const double NA5 = NA4 * NA;
        const double CA5 = (double)(CA4) * CA;
        const double S4 = (NA5 - CA5) * kOneFifth;
        TriSumB = (float)(((S4 * V + (double)(S3) * W) + (double)(S2) * Y) + TriSumB);
    }

    Acc.Mass = (float)((double)(TriMass) + Acc.Mass);
    Acc.SumA = (float)((double)(TriSumA) + Acc.SumA);
    Acc.SumB = (float)((double)(TriSumB) + Acc.SumB);
}

// FUN_0636d580, cdecl (leaves, A, B, C, csys, &n1, &mass, &n2).
// FUN_0636d000 builds {csys, 0, 0, 0}; leaves descending, triangles
// ascending (signed word count, re-read each pass). mass < 1e-10f
// (DAT_063a53d8) or unordered gives mass = 0, n1 = 0, n2 = 1.0f; otherwise
// n1 = sumA / mass, mass, n2 = sumB / mass.
void IVP_SurfaceBuilder_Ledge_Soup::AccumulateAxisIntegral(const IVP_SurfaceManager_OutList& Leaves, int32_t AxisA, int32_t AxisB,
                                                             int32_t AxisC, const IVP_U_Matrix* Csys, float* OutNormalized1,
                                                             float* OutRawMass, float* OutNormalized2)
{
    FAxisIntegralAccumulator Acc(Csys); // local_10

    for (int32_t I = Leaves.Count - 1; I >= 0; --I)
    {
        const IVP_Compact_Ledge* Ledge = Leaves.Elems[I];
        const IVP_Compact_Triangle* Triangle = Ledge->GetFirstTriangle();
        for (int32_t T = 0; T < static_cast<int32_t>(Ledge->GetNTriangles()); ++T, ++Triangle)
            AccumulateTriangleAxisIntegral(Acc, Ledge, Triangle, AxisA, AxisB, AxisC);
    }

    if (!(Acc.Mass >= BitsToFloat(0x2EDBE6FFu))) // DAT_063a53d8
    {
        *OutRawMass = 0.0f;
        *OutNormalized1 = 0.0f;
        *OutNormalized2 = 1.0f;
        return;
    }

    *OutNormalized1 = (float)((double)(Acc.SumA) / Acc.Mass);
    *OutRawMass = Acc.Mass;
    *OutNormalized2 = (float)((double)(Acc.SumB) / Acc.Mass);
}

// FUN_0636d6f0: Mirtich's polyhedral mass properties ("Fast and Accurate
// Computation of Polyhedral Mass Properties", 1996); see the header.
// FUN_0628be10 (this = out; vec, scale) is out[i] = float(scale * vec[i]);
// it is written inline below, like its other two file-local copies in this
// port (common_physys.cpp PhySys_Vec3Scale, ivp_friction_solver.cpp).
bool IVP_SurfaceBuilder_Ledge_Soup::ComputeMassProperties(const IVP_SurfaceManager_OutList& leaves,
                                                            float outCenterOfMass[3], float outRotationInertia[3])
{
    // local_50, set to identity by FUN_063481b0; the accumulator is built by
    // FUN_0636d020.
    IVP_U_Matrix Csys;
    Csys.init();
    FVolumeIntegralAccumulator Accumulator;

    AccumulateVolumeIntegral(Accumulator, leaves);

    // metric = sqrt(acc[4])^3 * DAT_063a5914, unrounded; the Mirtich path is
    // taken only when acc[5] > metric (equal or unordered -> fallback).
    const double SqrtTerm = std::sqrt((double)(Accumulator.NormalQuadSum));
    const double VolumeMetric = SqrtTerm * SqrtTerm * SqrtTerm * (double)(BitsToFloat(0x3089705Fu));
    if (!((double)(Accumulator.Volume6) > VolumeMetric))
    {
        ComputeBoundingSphereMassPropertiesFallback(leaves, outCenterOfMass, outRotationInertia);
        return false;
    }

    Csys.init();

    // scale = float(1.0f / acc[5]); FUN_0628be10(this = local_8c; acc, scale) writes
    // com.k[i] = float(scale * acc[i]) and leaves local_8c's 4th dword
    // unwritten. com goes to *outCenterOfMass and its 4 dwords to Csys.vv
    // (retail copies uninitialised stack into vv.hesse, which vimult4 never
    // reads; the port copies a zero).
    const float InvVolume = (float)(1.0 / (double)(Accumulator.Volume6));
    IVP_U_Point Com = {};
    Com.k[0] = (float)((double)(InvVolume) * Accumulator.Moment[0]);
    Com.k[1] = (float)((double)(InvVolume) * Accumulator.Moment[1]);
    Com.k[2] = (float)((double)(InvVolume) * Accumulator.Moment[2]);
    outCenterOfMass[0] = Com.k[0];
    outCenterOfMass[1] = Com.k[1];
    outCenterOfMass[2] = Com.k[2];
    Csys.vv.k[0] = Com.k[0];
    Csys.vv.k[1] = Com.k[1];
    Csys.vv.k[2] = Com.k[2];
    Csys.vv.k[3] = Com.k[3];

    // FUN_0636d580(leaves, 0,1,2, &Csys, &local_78, &local_7c, &B1),
    // (1,2,0, &local_74, &local_7c, &B2), (2,0,1, &local_70, &local_7c, &B3);
    // only the last out-parameter is read later.
    float Normalized1[3];
    float RawMass;
    float B1;
    float B2;
    float B3;
    AccumulateAxisIntegral(leaves, 0, 1, 2, &Csys, &Normalized1[0], &RawMass, &B1);
    AccumulateAxisIntegral(leaves, 1, 2, 0, &Csys, &Normalized1[1], &RawMass, &B2);
    AccumulateAxisIntegral(leaves, 2, 0, 1, &Csys, &Normalized1[2], &RawMass, &B3);

    // Squares stay on the x87 stack;
    // x = sqrt(B3^2 + B2^2), y = sqrt(B3^2 + B1^2), z = sqrt(B1^2 + B2^2).
    const double B1Sq = (double)(B1) * B1;
    const double B2Sq = (double)(B2) * B2;
    const double B3Sq = (double)(B3) * B3;
    outRotationInertia[0] = (float)(std::sqrt(B3Sq + B2Sq));
    outRotationInertia[1] = (float)(std::sqrt(B3Sq + B1Sq));
    outRotationInertia[2] = (float)(std::sqrt(B1Sq + B2Sq));
    return true;
}

// FUN_0636d650: collects every leaf ledge of
// the compiled surface into a local growable array {capacity 0x80, count 0,
// malloc(0x200)} through FUN_0635abf0, runs FUN_0636d6f0 on it and frees the
// array (unless it is the inline sentinel) on both the normal and the unwind
// path; returns FUN_0636d6f0's result. FLedgeSoupLeafList owns the buffer
// the same way (RAII covers the unwind funclet).
bool IVP_SurfaceBuilder_Ledge_Soup::ComputeSurfaceMassProperties(const void* CompiledSurface,
                                                                   float outCenterOfMass[3], float outRotationInertia[3])
{
    FLedgeSoupLeafList Leaves(0x80); // {0x80, 0, FUN_06343f70(0x200)}
    IVP_Compact_Ledge_Solver::get_all_ledges(static_cast<const IVP_Compact_Surface*>(CompiledSurface), &Leaves.List); // :0636d699 FUN_0635abf0
    return ComputeMassProperties(Leaves.List, outCenterOfMass, outRotationInertia);
}

// FUN_0635ac10. Per triangle: N = FUN_0635b940(edge0), inv = 1 / |N|^2; per edge start
// point: d = p - com, local_28 = max(|d|^2), local_24 = max(|d x N|^2 * inv)
// (squared distance from the line com + t*N). Tail (also reached with no
// triangles): *upper = max(*upper, sqrt(local_28)); when the dev POINTER is
// non-null, *dev = max(*dev, sqrt(local_24)).
void IVP_SurfaceBuilder_Ledge_Soup::AccumulateLeafLedgeRadii(const IVP_Compact_Ledge* leaf, const float centerOfMass[3],
                                                               float* upperLimitRadiusAccum, float* maxSurfaceDeviationAccum)
{
    float MaxDistSq = 0.0f; // local_28
    float MaxAuxSq = 0.0f;  // local_24

    const IVP_Compact_Triangle* Triangle = leaf->GetFirstTriangle();
    for (int32_t Remaining = leaf->GetNTriangles(); Remaining > 0; --Remaining, ++Triangle)
    {
        // inv = 1.0f / ((N.y^2 + N.z^2) + N.x^2) stays on the x87 stack.
        IVP_U_Point N; // local_10
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(Triangle->GetFirstEdge(), leaf, &N);
        const double InvNormSq = 1.0 / (((double)(N.k[1]) * N.k[1] + (double)(N.k[2]) * N.k[2]) +
                                        (double)(N.k[0]) * N.k[0]);

        // The start point of each of the three edges.
        for (int32_t K = 0; K < 3; ++K)
        {
            const IVP_U_Float_Point* P = leaf->GetPoint(Triangle->GetEdge(K));
            const float Dx = (float)((double)(P->X) - centerOfMass[0]); // local_20
            const float Dy = (float)((double)(P->Y) - centerOfMass[1]); // local_1c
            const double DzExt = (double)(P->Z) - centerOfMass[2];
            const float Dz = (float)(DzExt);                                        // FST local_18

            // dz stays on the stack and is multiplied by its float copy.
            const double DistSq = (DzExt * Dz + (double)(Dx) * Dx) + (double)(Dy) * Dy;
            if (DistSq > MaxDistSq) MaxDistSq = (float)(DistSq);

            // cross(d, N), each component on the x87 stack.
            const double T1 = (double)(N.k[2]) * Dy - (double)(Dz) * N.k[1];
            const double T2 = (double)(Dz) * N.k[0] - (double)(N.k[2]) * Dx;
            const double T3 = (double)(N.k[1]) * Dx - (double)(Dy) * N.k[0];
            const double AuxSq = ((T2 * T2 + T3 * T3) + T1 * T1) * InvNormSq;
            if (AuxSq > MaxAuxSq) MaxAuxSq = (float)(AuxSq);
        }
    }

    const double Dist = std::sqrt((double)(MaxDistSq));
    if (Dist > *upperLimitRadiusAccum) *upperLimitRadiusAccum = (float)(Dist);

    if (maxSurfaceDeviationAccum != nullptr)
    {
        const double Aux = std::sqrt((double)(MaxAuxSq));
        if (Aux > *maxSurfaceDeviationAccum) *maxSurfaceDeviationAccum = (float)(Aux);
    }
}

// FUN_0635ada0: zeroes both outputs, collects the leaves into a local
// {0x400, 0, FUN_06343f70(0x1000)} vector (FUN_0635abf0, get_all_ledges) and
// runs AccumulateLeafLedgeRadii() on each, descending.
void IVP_SurfaceBuilder_Ledge_Soup::ComputeSurfaceRadii(const void* CompiledSurface, const float centerOfMass[3],
                                                          float* outUpperLimitRadius, float* outMaxSurfaceDeviation)
{
    *outUpperLimitRadius = 0.0f; // :0635adc6
    *outMaxSurfaceDeviation = 0.0f;  // :0635adcd

    FLedgeSoupLeafList Leaves(0x400); // {0x400, 0, FUN_06343f70(0x1000)}
    IVP_Compact_Ledge_Solver::get_all_ledges(static_cast<const IVP_Compact_Surface*>(CompiledSurface), &Leaves.List); // :0635adf2 FUN_0635abf0

    for (int32_t i = Leaves.List.Count - 1; i >= 0; --i)
        AccumulateLeafLedgeRadii(Leaves.List.Elems[i], centerOfMass,
                                 outUpperLimitRadius, outMaxSurfaceDeviation);
}

// FUN_0636b6a0: Compile()'s mass-property
// step between CompileTree() and FreeBuildState(). Writes centerOfMass
// (+0x0), rotationInertia (+0xc), upper_limit_radius (+0x18) into the
// compiled surface header and splices max_factor_surface_deviation into the
// low byte of the packed size/flags dword at +0x1c.
void IVP_SurfaceBuilder_Ledge_Soup::ApplyMassPropertyHeader()
{
    // FUN_0636d650; its return value is ignored.
    float CenterOfMass[3];
    float RotationInertia[3];
    ComputeSurfaceMassProperties(m_CompactSurface, CenterOfMass, RotationInertia);

    float UpperLimitRadius;
    float MaxSurfaceDeviation;
    ComputeSurfaceRadii(m_CompactSurface, CenterOfMass, &UpperLimitRadius, &MaxSurfaceDeviation);

    // Dword copies, inertia to +0xc..+0x14 first,
    // then com to +0x0..+0x8, then upper to +0x18.
    IVP_Compact_Surface* const Blob = static_cast<IVP_Compact_Surface*>(m_CompactSurface);
    std::memcpy(Blob->rotation_inertia, RotationInertia, sizeof(RotationInertia));
    std::memcpy(Blob->mass_center, CenterOfMass, sizeof(CenterOfMass));
    std::memcpy(&Blob->upper_limit_radius, &UpperLimitRadius, sizeof(UpperLimitRadius));

    // byte(ftol(dev / (upper * DAT_063a54b4) + 1.0f)) into the low byte of
    // [+0x1c], all on the x87 stack: the deviation in units of upper / 250.
    const double Scaled = (double)(UpperLimitRadius) * (double)(BitsToFloat(0x3B83126Fu));
    const double Bucket = (double)(MaxSurfaceDeviation) / Scaled + 1.0;
    const uint32_t ClassifiedByte = static_cast<uint32_t>(static_cast<int32_t>(Bucket)) & 0xffu;

    uint32_t PackedSizeAndFlags = Blob->max_deviation_and_size;
    PackedSizeAndFlags = (PackedSizeAndFlags & ~0xffu) | ClassifiedByte;
    Blob->max_deviation_and_size = PackedSizeAndFlags;
}

// FUN_0636a190, thiscall with an unused
// `this`: while (Node) { if (Node->ledge) { OutLedges.Add(ledge)
// (FUN_063556b0); return; } if (Node->Child0) recurse(Child0);
// Node = Node->Child1; }.
void IVP_SurfaceBuilder_Ledge_Soup::CollectNodeLedges(IVP_U_Vector<IVP_Compact_Ledge>& OutLedges, const BuildNode* Node)
{
    while (Node != nullptr)
    {
        if (Node->Ledge != nullptr)
        {
            OutLedges.Add(Node->Ledge);
            return;
        }
        if (Node->Child0 != nullptr)
            CollectNodeLedges(OutLedges, Node->Child0);
        Node = Node->Child1;
    }
}

// FUN_0636a1e0: root = scratch[scratch[0]
// .NextIndexOrCount (word, zero-extended)].Payload; a local hull builder
// (FUN_0636ca00: ledge vector with 0x80 preallocated slots) collects every
// ledge below the root (FUN_0636a190) and FUN_0636cf70 builds their convex
// hull; the hull is appended to m_Ledges, stored as the root's ledge and the
// root is appended to m_HullNodes; the builder's vector is released by
// FUN_063449d0 (EH state 0 -> RAII here).
void IVP_SurfaceBuilder_Ledge_Soup::BuildRootConvexHull()
{
    const BuildTreeScratchEntry* Scratch = m_BuildTreeScratch;
    const uint16_t RootIndex = static_cast<uint16_t>(Scratch[0].NextIndexOrCount);
    BuildNode* Root = Scratch[RootIndex].Payload;

    FLedgeConvexHullBuilder HullBuilder;
    CollectNodeLedges(HullBuilder.Ledges, Root);
    IVP_Compact_Ledge* Hull = HullBuilder.Build();

    m_Ledges.Add(Hull);
    Root->Ledge = Hull;
    m_HullNodes.Add(Root);
}

// FUN_0636a0b0.
void* IVP_SurfaceBuilder_Ledge_Soup::Compile(const void* options)
{
    // Retail defaults to a local {0,1,0,1} block; m_CompileOptions is only
    // read by callees before Compile() returns.
    const int32_t defaultOptions[4] = { 0, 1, 0, 1 };
    if (options == nullptr)
        options = defaultOptions;
    m_CompileOptions = static_cast<const int32_t*>(options);

    if (m_Ledges.Len() == 0)
        return nullptr;

    BuildLeafNodes();
    FinalizeBuildTree(1.0f);

    // options[0] != 0 and more than one ledge (unsigned compare) ->
    // FUN_0636a1e0.
    const int32_t* optArr = m_CompileOptions;
    if (optArr[0] != 0 && m_Ledges.n_elems > 1)
        BuildRootConvexHull();

    BuildCompactSurface(); // FUN_0636b190, return value ignored (a failed allocation is not checked)
    CompileTree();
    ApplyMassPropertyHeader(); // FUN_0636b6a0

    FreeBuildState();

    void* result = m_CompactSurface;
    if (m_LeafCount > 1)
    {
        const int32_t flag2 = optArr[2];
        const int32_t flag3 = optArr[3];
        if (flag2 == 0 && flag3 == 1)
        {
            // Shrink-copy into a new aligned block of (result[+0x1c] >> 8)
            // bytes.
            const IVP_Compact_Surface* header = static_cast<const IVP_Compact_Surface*>(result);
            const int32_t packedSize = static_cast<int32_t>(header->max_deviation_and_size);
            const int32_t byteSize = packedSize >> 8;
            void* newBlock = ivp_malloc_aligned(static_cast<unsigned>(byteSize), 0x10); // FUN_06343fc0
            std::memcpy(newBlock, result, static_cast<size_t>(byteSize));
            ivp_free_aligned(result); // FUN_06343ff0
            result = newBlock;
        }
    }

    m_CompactSurface = nullptr; // ownership transferred to the caller.
    return result;
}
