// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>
#include <vector>

// Forward declarations
class IVP_Compact_Ledge;
class IVP_Compact_Triangle;
class IVP_Compact_Edge;

// 16-byte point layout (x, y, z, pad)
struct alignas(16) IVP_U_Float_Point
{
    float X;
    float Y;
    float Z;
    float HesseVal; // or padding

    const float* AsArray() const { return &X; }
    float* AsArray() { return &X; }
};

// 16-byte lookup tables matching retail Common.dll DAT_063ee4e0 and DAT_063ee4f0
alignas(16) static const int32_t G_IVP_Compact_Edge_Next_Lut[4] = { 0,  4,  4, -8 }; // DAT_063ee4e0
alignas(16) static const int32_t G_IVP_Compact_Edge_Prev_Lut[4] = { 0,  8, -4, -4 }; // DAT_063ee4f0

// 4-byte packed compact half-edge
class IVP_Compact_Edge
{
public:
    // Data bit 31: the edge's is_piercing flag (IsPiercing).
    static constexpr uint32_t kPiercingFlag = 0x80000000U;

    uint32_t Data; // [15:0] start_point_index, [30:16] opposite_offset (15-bit signed), [31] is_piercing

public:
    // --- Vertex and Offset Extraction ---
    inline uint32_t GetStartPointIndex() const
    {
        return Data & 0xFFFF;
    }

    inline int32_t GetOppositeIndex() const
    {
        return static_cast<int32_t>(Data << 1) >> 17;
    }

    inline bool IsPiercing() const
    {
        return (Data & kPiercingFlag) != 0;
    }

    inline uint32_t GetEdgeIndex() const
    {
        return (((reinterpret_cast<uintptr_t>(this)) & 0x0C) >> 2) - 1; // 0, 1, or 2
    }

    // --- Mesh Traversal Methods ---
    inline const IVP_Compact_Edge* GetNextEdge() const
    {
        const int32_t Offset = *reinterpret_cast<const int32_t*>(
            reinterpret_cast<const char*>(G_IVP_Compact_Edge_Next_Lut) + 
            (reinterpret_cast<uintptr_t>(this) & 0x0C)
        );
        return reinterpret_cast<const IVP_Compact_Edge*>(reinterpret_cast<const char*>(this) + Offset);
    }

    inline IVP_Compact_Edge* GetNextEdge()
    {
        const int32_t Offset = *reinterpret_cast<const int32_t*>(
            reinterpret_cast<const char*>(G_IVP_Compact_Edge_Next_Lut) + 
            (reinterpret_cast<uintptr_t>(this) & 0x0C)
        );
        return reinterpret_cast<IVP_Compact_Edge*>(reinterpret_cast<char*>(this) + Offset);
    }

    inline const IVP_Compact_Edge* GetPrevEdge() const
    {
        const int32_t Offset = *reinterpret_cast<const int32_t*>(
            reinterpret_cast<const char*>(G_IVP_Compact_Edge_Prev_Lut) + 
            (reinterpret_cast<uintptr_t>(this) & 0x0C)
        );
        return reinterpret_cast<const IVP_Compact_Edge*>(reinterpret_cast<const char*>(this) + Offset);
    }

    inline IVP_Compact_Edge* GetPrevEdge()
    {
        const int32_t Offset = *reinterpret_cast<const int32_t*>(
            reinterpret_cast<const char*>(G_IVP_Compact_Edge_Prev_Lut) + 
            (reinterpret_cast<uintptr_t>(this) & 0x0C)
        );
        return reinterpret_cast<IVP_Compact_Edge*>(reinterpret_cast<char*>(this) + Offset);
    }

    inline const IVP_Compact_Edge* GetOppositeEdge() const
    {
        const int32_t Offset = GetOppositeIndex();
        return this + Offset;
    }

    inline IVP_Compact_Edge* GetOppositeEdge()
    {
        const int32_t Offset = GetOppositeIndex();
        return this + Offset;
    }

    // --- Parent Triangle & Ledge Navigation ---
    inline const IVP_Compact_Triangle* GetCompactTriangle() const
    {
        return reinterpret_cast<const IVP_Compact_Triangle*>(
            reinterpret_cast<uintptr_t>(this) & ~static_cast<uintptr_t>(0x0F)
        );
    }

    inline IVP_Compact_Triangle* GetCompactTriangle()
    {
        return reinterpret_cast<IVP_Compact_Triangle*>(
            reinterpret_cast<uintptr_t>(this) & ~static_cast<uintptr_t>(0x0F)
        );
    }

    inline const IVP_Compact_Ledge* GetCompactLedge() const;
    inline IVP_Compact_Ledge* GetCompactLedge();

    // --- Vertex Helper ---
    inline const IVP_U_Float_Point* GetStartPoint() const;
};
static_assert(sizeof(IVP_Compact_Edge) == 4, "IVP_Compact_Edge must be 4 bytes");

// 16-byte triangle representation
class alignas(16) IVP_Compact_Triangle
{
public:
    uint32_t TriInfo;             // [11:0] tri_index, [31:12] triangle flags
    IVP_Compact_Edge CEdge[3];    // CEdge[0] at +0x4, CEdge[1] at +0x8, CEdge[2] at +0xC

public:
    inline uint32_t GetTriIndex() const
    {
        return TriInfo & 0x0FFF;
    }

    // Bits [23:12] of TriInfo (as read by FUN_06353840): the classic IVP
    // pierce_index, a triangle index into the same ledge. The classic
    // material_index:7 / is_virtual:1 sub-fields inside these bits are not
    // confirmed and not modeled.
    inline uint32_t GetPierceIndex() const
    {
        return (TriInfo >> 12) & 0x0FFF;
    }

    inline const IVP_Compact_Edge* GetFirstEdge() const
    {
        return &CEdge[0];
    }

    inline IVP_Compact_Edge* GetFirstEdge()
    {
        return &CEdge[0];
    }

    inline const IVP_Compact_Edge* GetEdge(int32_t Index) const
    {
        return &CEdge[Index];
    }

    inline IVP_Compact_Edge* GetEdge(int32_t Index)
    {
        return &CEdge[Index];
    }

    inline const IVP_Compact_Ledge* GetCompactLedge() const;
    inline IVP_Compact_Ledge* GetCompactLedge();
};
static_assert(sizeof(IVP_Compact_Triangle) == 16, "IVP_Compact_Triangle must be 16 bytes");

// The IVP_Compact_Triangle constructor (FUN_0635aa20, 16 zero bytes) on raw memory; defined in
// ivp_collision/ivp_mindist_limiter.cpp.
extern "C" void* IVP_Compact_Triangle_Init(void* Memory);

// Variable-length compact ledge (convex sub-mesh / hull)
class alignas(16) IVP_Compact_Ledge
{
public:
    // SizeAndFlags bits 0..1, the has-children flag.
    static constexpr uint32_t kHasChildrenMask = 0x3u;

    int32_t CPointOffset;           // +0x00: Byte offset from 'this' to the IVP_U_Float_Point array
    union
    {
        int32_t LedgetreeNodeOffset; // +0x04: Byte offset from 'this' to parent ledgetree node
        int32_t ClientData;
    };
    // +0x08: one packed dword.
    //  - bits 0..1: has-children flag. The ledge-soup compiler clears them on a
    //    terminal node's ledge and sets 01 on an inner node's hull ledge; a
    //    nonzero value selects IVP_Complex_Mindist over the plain mindist
    //    (0634e5ad, 06360cdf).
    //  - bits 8..31: the ledge's byte size / 16.
    //  - bits 2..7: not interpreted by any traced code.
    uint32_t SizeAndFlags;
    int16_t NTriangles;             // +0x0C: Number of triangles in this ledge
    int16_t HasChildrenFlag;        // +0x0E: Flags / has child ledges

    // +0x10: First IVP_Compact_Triangle starts here (size = NTriangles * 16)
    // +c_point_offset: Array of IVP_U_Float_Point vertices starts here

public:
    inline int16_t GetNTriangles() const
    {
        return NTriangles;
    }

    inline const IVP_Compact_Triangle* GetFirstTriangle() const
    {
        return reinterpret_cast<const IVP_Compact_Triangle*>(
            reinterpret_cast<const char*>(this) + 0x10
        );
    }

    inline IVP_Compact_Triangle* GetFirstTriangle()
    {
        return reinterpret_cast<IVP_Compact_Triangle*>(
            reinterpret_cast<char*>(this) + 0x10
        );
    }

    inline const IVP_Compact_Edge* GetFirstEdge() const
    {
        return reinterpret_cast<const IVP_Compact_Edge*>(
            reinterpret_cast<const char*>(this) + 0x14
        );
    }

    inline IVP_Compact_Edge* GetFirstEdge()
    {
        return reinterpret_cast<IVP_Compact_Edge*>(
            reinterpret_cast<char*>(this) + 0x14
        );
    }

    inline const IVP_U_Float_Point* GetPointArray() const
    {
        return reinterpret_cast<const IVP_U_Float_Point*>(
            reinterpret_cast<const char*>(this) + CPointOffset
        );
    }

    inline IVP_U_Float_Point* GetPointArray()
    {
        return reinterpret_cast<IVP_U_Float_Point*>(
            reinterpret_cast<char*>(this) + CPointOffset
        );
    }

    inline const IVP_U_Float_Point* GetPoint(const IVP_Compact_Edge* Edge) const
    {
        return GetPointArray() + Edge->GetStartPointIndex();
    }

    inline IVP_U_Float_Point* GetPoint(const IVP_Compact_Edge* Edge)
    {
        return GetPointArray() + Edge->GetStartPointIndex();
    }
};
static_assert(sizeof(IVP_Compact_Ledge) == 16, "IVP_Compact_Ledge header must be 16 bytes");

// --- Late Inline Definitions ---

inline const IVP_Compact_Ledge* IVP_Compact_Triangle::GetCompactLedge() const
{
    const uint32_t TriIndex = GetTriIndex();
    return reinterpret_cast<const IVP_Compact_Ledge*>(
        reinterpret_cast<const char*>(this) - ((TriIndex + 1) * 16)
    );
}

inline IVP_Compact_Ledge* IVP_Compact_Triangle::GetCompactLedge()
{
    const uint32_t TriIndex = GetTriIndex();
    return reinterpret_cast<IVP_Compact_Ledge*>(
        reinterpret_cast<char*>(this) - ((TriIndex + 1) * 16)
    );
}

inline const IVP_Compact_Ledge* IVP_Compact_Edge::GetCompactLedge() const
{
    return GetCompactTriangle()->GetCompactLedge();
}

inline IVP_Compact_Ledge* IVP_Compact_Edge::GetCompactLedge()
{
    return GetCompactTriangle()->GetCompactLedge();
}

inline const IVP_U_Float_Point* IVP_Compact_Edge::GetStartPoint() const
{
    return GetCompactLedge()->GetPoint(this);
}

// Retail FUN_06353840: starting at the pierce triangle of StartEdge's
// triangle, walks across edges toward Position (ledge object space) until
// every edge of the current triangle has a positive QR weight (FUN_0635b210)
// or leads to an already visited triangle; returns that triangle's edge.
struct IVP_U_Point;
IVP_Compact_Edge* IVP_WalkPiercingTriangleChain(IVP_Compact_Edge* StartEdge, const IVP_U_Point* Position);
