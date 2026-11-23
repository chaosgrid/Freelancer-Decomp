// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// Zero-SDK D3D9 image, surface, texture, and HRESULT helpers (uses Windows SDK WIC + native DDS/TGA/BMP).

#include "Rp9Image.h"

#include <wincodec.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    // =========================================================================
    // DDS file format structures & constants
    // =========================================================================
    const uint32_t kDdsMagic = 0x20534444u; // "DDS "
    const uint32_t kDdpfAlphaPixels = 0x00000001u;
    const uint32_t kDdpfAlpha       = 0x00000002u;
    const uint32_t kDdpfFourCC      = 0x00000004u;
    const uint32_t kDdpfRgb         = 0x00000040u;
    const uint32_t kDdpfLuminance   = 0x00020000u;

    const uint32_t kDdsCaps2Cubemap          = 0x00000200u;
    const uint32_t kDdsCaps2CubemapPosX      = 0x00000400u;
    const uint32_t kDdsCaps2CubemapNegX      = 0x00000800u;
    const uint32_t kDdsCaps2CubemapPosY      = 0x00001000u;
    const uint32_t kDdsCaps2CubemapNegY      = 0x00002000u;
    const uint32_t kDdsCaps2CubemapPosZ      = 0x00004000u;
    const uint32_t kDdsCaps2CubemapNegZ      = 0x00008000u;
    const uint32_t kDdsCaps2Volume           = 0x00200000u;

    struct FDdsPixelFormat
    {
        uint32_t Size;
        uint32_t Flags;
        uint32_t FourCC;
        uint32_t RgbBitCount;
        uint32_t RBitMask;
        uint32_t GBitMask;
        uint32_t BBitMask;
        uint32_t ABitMask;
    };

    struct FDdsHeader
    {
        uint32_t Size;
        uint32_t Flags;
        uint32_t Height;
        uint32_t Width;
        uint32_t PitchOrLinearSize;
        uint32_t Depth;
        uint32_t MipMapCount;
        uint32_t Reserved1[11];
        FDdsPixelFormat PixelFormat;
        uint32_t Caps;
        uint32_t Caps2;
        uint32_t Caps3;
        uint32_t Caps4;
        uint32_t Reserved2;
    };

    static_assert(sizeof(FDdsHeader) == 124, "DDS_HEADER must be 124 bytes");

    bool IsDxtFormat(D3DFORMAT Fmt)
    {
        return Fmt == D3DFMT_DXT1 || Fmt == D3DFMT_DXT2 || Fmt == D3DFMT_DXT3 ||
               Fmt == D3DFMT_DXT4 || Fmt == D3DFMT_DXT5;
    }

    UINT DxtBlockBytes(D3DFORMAT Fmt)
    {
        return (Fmt == D3DFMT_DXT1) ? 8u : 16u;
    }

    UINT UncompressedBytesPerPixel(D3DFORMAT Fmt)
    {
        switch (Fmt)
        {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
        case D3DFMT_A8B8G8R8:
        case D3DFMT_X8B8G8R8:
        case D3DFMT_A2R10G10B10:
        case D3DFMT_A2B10G10R10:
            return 4u;
        case D3DFMT_R8G8B8:
            return 3u;
        case D3DFMT_R5G6B5:
        case D3DFMT_X1R5G5B5:
        case D3DFMT_A1R5G5B5:
        case D3DFMT_A4R4G4B4:
        case D3DFMT_X4R4G4B4:
        case D3DFMT_A8L8:
        case D3DFMT_A8P8:
            return 2u;
        case D3DFMT_A8:
        case D3DFMT_L8:
        case D3DFMT_P8:
        case D3DFMT_A4L4:
        case D3DFMT_R3G3B2:
            return 1u;
        default:
            return 0u;
        }
    }

    UINT ComputeSurfacePitch(D3DFORMAT Fmt, UINT Width)
    {
        if (IsDxtFormat(Fmt))
            return std::max<UINT>(1u, (Width + 3u) / 4u) * DxtBlockBytes(Fmt);
        return Width * UncompressedBytesPerPixel(Fmt);
    }

    UINT ComputeSurfaceRows(D3DFORMAT Fmt, UINT Height)
    {
        if (IsDxtFormat(Fmt))
            return std::max<UINT>(1u, (Height + 3u) / 4u);
        return Height;
    }

    UINT ComputeSurfaceByteSize(D3DFORMAT Fmt, UINT Width, UINT Height)
    {
        return ComputeSurfacePitch(Fmt, Width) * ComputeSurfaceRows(Fmt, Height);
    }

    D3DFORMAT DdsPixelFormatToD3d(const FDdsPixelFormat& Pf)
    {
        if ((Pf.Flags & kDdpfFourCC) != 0)
        {
            switch (Pf.FourCC)
            {
            case MAKEFOURCC('D', 'X', 'T', '1'): return D3DFMT_DXT1;
            case MAKEFOURCC('D', 'X', 'T', '2'): return D3DFMT_DXT2;
            case MAKEFOURCC('D', 'X', 'T', '3'): return D3DFMT_DXT3;
            case MAKEFOURCC('D', 'X', 'T', '4'): return D3DFMT_DXT4;
            case MAKEFOURCC('D', 'X', 'T', '5'): return D3DFMT_DXT5;
            case 36:  return D3DFMT_A16B16G16R16;
            case 111: return D3DFMT_R16F;
            case 112: return D3DFMT_G16R16F;
            case 113: return D3DFMT_A16B16G16R16F;
            case 114: return D3DFMT_R32F;
            case 115: return D3DFMT_G32R32F;
            case 116: return D3DFMT_A32B32G32R32F;
            default:  return D3DFMT_UNKNOWN;
            }
        }
        if ((Pf.Flags & kDdpfRgb) != 0)
        {
            if (Pf.RgbBitCount == 32)
            {
                if (Pf.RBitMask == 0x00FF0000u && Pf.GBitMask == 0x0000FF00u && Pf.BBitMask == 0x000000FFu)
                    return (Pf.ABitMask == 0xFF000000u) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
                if (Pf.RBitMask == 0x000000FFu && Pf.GBitMask == 0x0000FF00u && Pf.BBitMask == 0x00FF0000u)
                    return (Pf.ABitMask == 0xFF000000u) ? D3DFMT_A8B8G8R8 : D3DFMT_X8B8G8R8;
            }
            else if (Pf.RgbBitCount == 24)
            {
                if (Pf.RBitMask == 0x00FF0000u && Pf.GBitMask == 0x0000FF00u && Pf.BBitMask == 0x000000FFu)
                    return D3DFMT_R8G8B8;
            }
            else if (Pf.RgbBitCount == 16)
            {
                if (Pf.RBitMask == 0xF800u && Pf.GBitMask == 0x07E0u && Pf.BBitMask == 0x001Fu && Pf.ABitMask == 0u)
                    return D3DFMT_R5G6B5;
                if (Pf.RBitMask == 0x7C00u && Pf.GBitMask == 0x03E0u && Pf.BBitMask == 0x001Fu)
                    return (Pf.ABitMask == 0x8000u) ? D3DFMT_A1R5G5B5 : D3DFMT_X1R5G5B5;
                if (Pf.RBitMask == 0x0F00u && Pf.GBitMask == 0x00F0u && Pf.BBitMask == 0x000Fu)
                    return (Pf.ABitMask == 0xF000u) ? D3DFMT_A4R4G4B4 : D3DFMT_X4R4G4B4;
            }
        }
        if ((Pf.Flags & kDdpfLuminance) != 0)
        {
            if (Pf.RgbBitCount == 8 && Pf.RBitMask == 0xFFu)
                return D3DFMT_L8;
            if (Pf.RgbBitCount == 16 && Pf.RBitMask == 0x00FFu && Pf.ABitMask == 0xFF00u)
                return D3DFMT_A8L8;
        }
        if ((Pf.Flags & kDdpfAlpha) != 0 && Pf.RgbBitCount == 8)
            return D3DFMT_A8;
        return D3DFMT_UNKNOWN;
    }

    // =========================================================================
    // Pixel unpack/pack helpers (canonical intermediate = 0xAARRGGBB)
    // =========================================================================
    inline uint8_t Expand5(uint32_t V) { return (uint8_t)((V << 3) | (V >> 2)); }
    inline uint8_t Expand6(uint32_t V) { return (uint8_t)((V << 2) | (V >> 4)); }
    inline uint8_t Expand4(uint32_t V) { return (uint8_t)((V << 4) | V); }

    inline uint32_t PackArgb(uint8_t A, uint8_t R, uint8_t G, uint8_t B)
    {
        return ((uint32_t)A << 24) | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
    }

    uint32_t ReadPixelArgb(const uint8_t* Ptr, D3DFORMAT Fmt, const PALETTEENTRY* Palette)
    {
        switch (Fmt)
        {
        case D3DFMT_A8R8G8B8:
        {
            uint32_t V;
            std::memcpy(&V, Ptr, 4);
            return V;
        }
        case D3DFMT_X8R8G8B8:
        {
            uint32_t V;
            std::memcpy(&V, Ptr, 4);
            return V | 0xFF000000u;
        }
        case D3DFMT_A8B8G8R8:
        {
            uint32_t V;
            std::memcpy(&V, Ptr, 4);
            return PackArgb((uint8_t)(V >> 24), (uint8_t)(V), (uint8_t)(V >> 8), (uint8_t)(V >> 16));
        }
        case D3DFMT_X8B8G8R8:
        {
            uint32_t V;
            std::memcpy(&V, Ptr, 4);
            return PackArgb(255u, (uint8_t)(V), (uint8_t)(V >> 8), (uint8_t)(V >> 16));
        }
        case D3DFMT_R8G8B8:
            return PackArgb(255u, Ptr[2], Ptr[1], Ptr[0]);
        case D3DFMT_R5G6B5:
        {
            uint16_t V;
            std::memcpy(&V, Ptr, 2);
            return PackArgb(255u, Expand5((V >> 11) & 0x1Fu), Expand6((V >> 5) & 0x3Fu), Expand5(V & 0x1Fu));
        }
        case D3DFMT_A1R5G5B5:
        {
            uint16_t V;
            std::memcpy(&V, Ptr, 2);
            return PackArgb((V & 0x8000u) ? 255u : 0u, Expand5((V >> 10) & 0x1Fu),
                            Expand5((V >> 5) & 0x1Fu), Expand5(V & 0x1Fu));
        }
        case D3DFMT_X1R5G5B5:
        {
            uint16_t V;
            std::memcpy(&V, Ptr, 2);
            return PackArgb(255u, Expand5((V >> 10) & 0x1Fu), Expand5((V >> 5) & 0x1Fu), Expand5(V & 0x1Fu));
        }
        case D3DFMT_A4R4G4B4:
        {
            uint16_t V;
            std::memcpy(&V, Ptr, 2);
            return PackArgb(Expand4((V >> 12) & 0xFu), Expand4((V >> 8) & 0xFu),
                            Expand4((V >> 4) & 0xFu), Expand4(V & 0xFu));
        }
        case D3DFMT_X4R4G4B4:
        {
            uint16_t V;
            std::memcpy(&V, Ptr, 2);
            return PackArgb(255u, Expand4((V >> 8) & 0xFu), Expand4((V >> 4) & 0xFu), Expand4(V & 0xFu));
        }
        case D3DFMT_A8:
            return PackArgb(Ptr[0], 255u, 255u, 255u);
        case D3DFMT_L8:
            return PackArgb(255u, Ptr[0], Ptr[0], Ptr[0]);
        case D3DFMT_A8L8:
            return PackArgb(Ptr[1], Ptr[0], Ptr[0], Ptr[0]);
        case D3DFMT_A4L4:
        {
            uint8_t L = Expand4(Ptr[0] & 0xFu);
            uint8_t A = Expand4((Ptr[0] >> 4) & 0xFu);
            return PackArgb(A, L, L, L);
        }
        case D3DFMT_P8:
        {
            if (Palette != nullptr)
            {
                const PALETTEENTRY& E = Palette[Ptr[0]];
                return PackArgb(E.peFlags, E.peRed, E.peGreen, E.peBlue);
            }
            return PackArgb(255u, Ptr[0], Ptr[0], Ptr[0]);
        }
        case D3DFMT_A8P8:
        {
            if (Palette != nullptr)
            {
                const PALETTEENTRY& E = Palette[Ptr[0]];
                return PackArgb(Ptr[1], E.peRed, E.peGreen, E.peBlue);
            }
            return PackArgb(Ptr[1], Ptr[0], Ptr[0], Ptr[0]);
        }
        default:
            return 0xFF000000u;
        }
    }

    void WritePixelArgb(uint8_t* Ptr, D3DFORMAT Fmt, uint32_t Argb)
    {
        const uint8_t A = (uint8_t)(Argb >> 24);
        const uint8_t R = (uint8_t)(Argb >> 16);
        const uint8_t G = (uint8_t)(Argb >> 8);
        const uint8_t B = (uint8_t)(Argb);
        switch (Fmt)
        {
        case D3DFMT_A8R8G8B8:
            std::memcpy(Ptr, &Argb, 4);
            break;
        case D3DFMT_X8R8G8B8:
        {
            uint32_t V = Argb | 0xFF000000u;
            std::memcpy(Ptr, &V, 4);
            break;
        }
        case D3DFMT_A8B8G8R8:
        {
            uint32_t V = PackArgb(A, B, G, R);
            std::memcpy(Ptr, &V, 4);
            break;
        }
        case D3DFMT_X8B8G8R8:
        {
            uint32_t V = PackArgb(255u, B, G, R);
            std::memcpy(Ptr, &V, 4);
            break;
        }
        case D3DFMT_R8G8B8:
            Ptr[0] = B;
            Ptr[1] = G;
            Ptr[2] = R;
            break;
        case D3DFMT_R5G6B5:
        {
            uint16_t V = (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3));
            std::memcpy(Ptr, &V, 2);
            break;
        }
        case D3DFMT_A1R5G5B5:
        {
            uint16_t V = (uint16_t)(((A >= 128u ? 1u : 0u) << 15) | ((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3));
            std::memcpy(Ptr, &V, 2);
            break;
        }
        case D3DFMT_X1R5G5B5:
        {
            uint16_t V = (uint16_t)(((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3));
            std::memcpy(Ptr, &V, 2);
            break;
        }
        case D3DFMT_A4R4G4B4:
        {
            uint16_t V = (uint16_t)(((A >> 4) << 12) | ((R >> 4) << 8) | ((G >> 4) << 4) | (B >> 4));
            std::memcpy(Ptr, &V, 2);
            break;
        }
        case D3DFMT_X4R4G4B4:
        {
            uint16_t V = (uint16_t)(((R >> 4) << 8) | ((G >> 4) << 4) | (B >> 4));
            std::memcpy(Ptr, &V, 2);
            break;
        }
        case D3DFMT_A8:
            Ptr[0] = A;
            break;
        case D3DFMT_L8:
            Ptr[0] = (uint8_t)((77u * R + 150u * G + 29u * B) >> 8);
            break;
        case D3DFMT_A8L8:
            Ptr[0] = (uint8_t)((77u * R + 150u * G + 29u * B) >> 8);
            Ptr[1] = A;
            break;
        case D3DFMT_A4L4:
        {
            uint8_t L = (uint8_t)((77u * R + 150u * G + 29u * B) >> 12);
            Ptr[0] = (uint8_t)(((A >> 4) << 4) | (L & 0xFu));
            break;
        }
        default:
            break;
        }
    }

    // =========================================================================
    // Software DXT1 / DXT3 / DXT5 decompressor (for DXT -> uncompressed fallback)
    // =========================================================================
    void DecodeDxtColorBlock(const uint8_t* Block, bool bDxt1Alpha, uint32_t OutColors[16])
    {
        uint16_t C0, C1;
        uint32_t Bits;
        std::memcpy(&C0, Block, 2);
        std::memcpy(&C1, Block + 2, 2);
        std::memcpy(&Bits, Block + 4, 4);

        uint8_t R[4], G[4], B[4], A[4];
        R[0] = Expand5((C0 >> 11) & 0x1Fu);
        G[0] = Expand6((C0 >> 5) & 0x3Fu);
        B[0] = Expand5(C0 & 0x1Fu);
        A[0] = 255u;

        R[1] = Expand5((C1 >> 11) & 0x1Fu);
        G[1] = Expand6((C1 >> 5) & 0x3Fu);
        B[1] = Expand5(C1 & 0x1Fu);
        A[1] = 255u;

        if (!bDxt1Alpha || C0 > C1)
        {
            R[2] = (uint8_t)((2u * R[0] + R[1]) / 3u);
            G[2] = (uint8_t)((2u * G[0] + G[1]) / 3u);
            B[2] = (uint8_t)((2u * B[0] + B[1]) / 3u);
            A[2] = 255u;

            R[3] = (uint8_t)((R[0] + 2u * R[1]) / 3u);
            G[3] = (uint8_t)((G[0] + 2u * G[1]) / 3u);
            B[3] = (uint8_t)((B[0] + 2u * B[1]) / 3u);
            A[3] = 255u;
        }
        else
        {
            R[2] = (uint8_t)((R[0] + R[1]) / 2u);
            G[2] = (uint8_t)((G[0] + G[1]) / 2u);
            B[2] = (uint8_t)((B[0] + B[1]) / 2u);
            A[2] = 255u;

            R[3] = G[3] = B[3] = A[3] = 0u;
        }

        for (int i = 0; i < 16; ++i)
        {
            const uint32_t Idx = (Bits >> (i * 2)) & 3u;
            OutColors[i] = PackArgb(A[Idx], R[Idx], G[Idx], B[Idx]);
        }
    }

    void DecompressDxtToArgb(const uint8_t* Src, UINT SrcPitch, D3DFORMAT Fmt,
                             UINT Width, UINT Height, std::vector<uint32_t>& OutArgb)
    {
        OutArgb.assign((size_t)Width * (size_t)Height, 0u);
        const UINT BlocksX = std::max<UINT>(1u, (Width + 3u) / 4u);
        const UINT BlocksY = std::max<UINT>(1u, (Height + 3u) / 4u);
        const UINT Stride = DxtBlockBytes(Fmt);

        for (UINT by = 0; by < BlocksY; ++by)
        {
            const uint8_t* Row = Src + (size_t)by * SrcPitch;
            for (UINT bx = 0; bx < BlocksX; ++bx)
            {
                const uint8_t* Blk = Row + (size_t)bx * Stride;
                uint32_t Colors[16];
                uint8_t Alphas[16];
                bool bHasExplicitAlpha = false;

                if (Fmt == D3DFMT_DXT1)
                {
                    DecodeDxtColorBlock(Blk, true, Colors);
                }
                else if (Fmt == D3DFMT_DXT2 || Fmt == D3DFMT_DXT3)
                {
                    bHasExplicitAlpha = true;
                    for (int i = 0; i < 8; ++i)
                    {
                        Alphas[i * 2]     = Expand4(Blk[i] & 0xFu);
                        Alphas[i * 2 + 1] = Expand4((Blk[i] >> 4) & 0xFu);
                    }
                    DecodeDxtColorBlock(Blk + 8, false, Colors);
                }
                else // DXT4 / DXT5
                {
                    bHasExplicitAlpha = true;
                    uint8_t Pal[8];
                    Pal[0] = Blk[0];
                    Pal[1] = Blk[1];
                    if (Pal[0] > Pal[1])
                    {
                        for (int k = 1; k <= 6; ++k)
                            Pal[k + 1] = (uint8_t)(((7 - k) * Pal[0] + k * Pal[1]) / 7);
                    }
                    else
                    {
                        for (int k = 1; k <= 4; ++k)
                            Pal[k + 1] = (uint8_t)(((5 - k) * Pal[0] + k * Pal[1]) / 5);
                        Pal[6] = 0u;
                        Pal[7] = 255u;
                    }
                    uint64_t AlphaBits = 0;
                    for (int i = 0; i < 6; ++i)
                        AlphaBits |= ((uint64_t)Blk[2 + i]) << (i * 8);
                    for (int i = 0; i < 16; ++i)
                        Alphas[i] = Pal[(AlphaBits >> (i * 3)) & 7u];
                    DecodeDxtColorBlock(Blk + 8, false, Colors);
                }

                for (UINT py = 0; py < 4; ++py)
                {
                    const UINT Y = by * 4 + py;
                    if (Y >= Height)
                        break;
                    for (UINT px = 0; px < 4; ++px)
                    {
                        const UINT X = bx * 4 + px;
                        if (X >= Width)
                            break;
                        uint32_t C = Colors[py * 4 + px];
                        if (bHasExplicitAlpha)
                            C = (C & 0x00FFFFFFu) | ((uint32_t)Alphas[py * 4 + px] << 24);
                        OutArgb[(size_t)Y * Width + X] = C;
                    }
                }
            }
        }
    }

    // =========================================================================
    // Resampling (nearest or bilinear)
    // =========================================================================
    void ResampleArgb(const uint32_t* Src, UINT SrcW, UINT SrcH,
                      std::vector<uint32_t>& Dst, UINT DstW, UINT DstH, DWORD Filter)
    {
        Dst.resize((size_t)DstW * (size_t)DstH);
        const bool bNearest = (Filter == D3DX_FILTER_NONE || Filter == D3DX_FILTER_POINT ||
                               (SrcW == DstW && SrcH == DstH));
        if (bNearest)
        {
            for (UINT y = 0; y < DstH; ++y)
            {
                const UINT sy = (SrcH == DstH) ? y : (UINT)(((uint64_t)y * SrcH) / DstH);
                const uint32_t* SrcRow = Src + (size_t)sy * SrcW;
                uint32_t* DstRow = Dst.data() + (size_t)y * DstW;
                if (SrcW == DstW)
                {
                    std::memcpy(DstRow, SrcRow, (size_t)DstW * 4u);
                }
                else
                {
                    for (UINT x = 0; x < DstW; ++x)
                    {
                        const UINT sx = (UINT)(((uint64_t)x * SrcW) / DstW);
                        DstRow[x] = SrcRow[sx];
                    }
                }
            }
            return;
        }

        // Bilinear filter
        for (UINT y = 0; y < DstH; ++y)
        {
            const float fy = ((float)y + 0.5f) * ((float)SrcH / (float)DstH) - 0.5f;
            const int y0 = std::max<int>(0, std::min<int>((int)SrcH - 1, (int)std::floor(fy)));
            const int y1 = std::min<int>((int)SrcH - 1, y0 + 1);
            const float wy = std::max<float>(0.0f, std::min<float>(1.0f, fy - (float)y0));
            const uint32_t* Row0 = Src + (size_t)y0 * SrcW;
            const uint32_t* Row1 = Src + (size_t)y1 * SrcW;
            uint32_t* DstRow = Dst.data() + (size_t)y * DstW;

            for (UINT x = 0; x < DstW; ++x)
            {
                const float fx = ((float)x + 0.5f) * ((float)SrcW / (float)DstW) - 0.5f;
                const int x0 = std::max<int>(0, std::min<int>((int)SrcW - 1, (int)std::floor(fx)));
                const int x1 = std::min<int>((int)SrcW - 1, x0 + 1);
                const float wx = std::max<float>(0.0f, std::min<float>(1.0f, fx - (float)x0));

                const uint32_t C00 = Row0[x0];
                const uint32_t C10 = Row0[x1];
                const uint32_t C01 = Row1[x0];
                const uint32_t C11 = Row1[x1];

                const float W00 = (1.0f - wx) * (1.0f - wy);
                const float W10 = wx * (1.0f - wy);
                const float W01 = (1.0f - wx) * wy;
                const float W11 = wx * wy;

                auto Blend = [&](int Shift) -> uint8_t {
                    const float V = ((C00 >> Shift) & 0xFFu) * W00 +
                                    ((C10 >> Shift) & 0xFFu) * W10 +
                                    ((C01 >> Shift) & 0xFFu) * W01 +
                                    ((C11 >> Shift) & 0xFFu) * W11 + 0.5f;
                    return (uint8_t)std::max<int>(0, std::min<int>(255, (int)V));
                };
                DstRow[x] = PackArgb(Blend(24), Blend(16), Blend(8), Blend(0));
            }
        }
    }

    // =========================================================================
    // TGA decoder (uncompressed & RLE, 8/16/24/32-bit, top/bottom origin)
    // =========================================================================
    bool ParseTgaHeader(const uint8_t* Data, UINT Size, UINT& OutW, UINT& OutH,
                        D3DFORMAT& OutFmt, uint8_t& OutImgType, uint8_t& OutBpp,
                        uint8_t& OutDescriptor, UINT& OutPixelOffset,
                        const uint8_t*& OutColorMap, UINT& OutColorMapEntries, uint8_t& OutColorMapBpp)
    {
        if (Size < 18)
            return false;
        const uint8_t IdLen = Data[0];
        const uint8_t ColorMapType = Data[1];
        const uint8_t ImgType = Data[2];
        const uint16_t CmLen = (uint16_t)(Data[5] | (Data[6] << 8));
        const uint8_t CmBpp = Data[7];
        const uint16_t W = (uint16_t)(Data[12] | (Data[13] << 8));
        const uint16_t H = (uint16_t)(Data[14] | (Data[15] << 8));
        const uint8_t Bpp = Data[16];
        const uint8_t Desc = Data[17];

        if (ColorMapType > 1)
            return false;
        if (ImgType != 1 && ImgType != 2 && ImgType != 3 && ImgType != 9 && ImgType != 10 && ImgType != 11)
            return false;
        if (W == 0 || H == 0)
            return false;
        if (Bpp != 8 && Bpp != 15 && Bpp != 16 && Bpp != 24 && Bpp != 32)
            return false;

        const UINT CmBytes = (ColorMapType == 1) ? (UINT)CmLen * ((CmBpp + 7u) / 8u) : 0u;
        const UINT PixOff = 18u + (UINT)IdLen + CmBytes;
        if (PixOff >= Size)
            return false;

        OutW = W;
        OutH = H;
        OutImgType = ImgType;
        OutBpp = Bpp;
        OutDescriptor = Desc;
        OutPixelOffset = PixOff;
        OutColorMap = (ColorMapType == 1) ? (Data + 18u + IdLen) : nullptr;
        OutColorMapEntries = CmLen;
        OutColorMapBpp = CmBpp;

        if (Bpp == 32)
            OutFmt = D3DFMT_A8R8G8B8;
        else if (Bpp == 24)
            OutFmt = D3DFMT_R8G8B8;
        else if (Bpp == 16 || Bpp == 15)
            OutFmt = D3DFMT_A1R5G5B5;
        else
            OutFmt = (ImgType == 3 || ImgType == 11) ? D3DFMT_L8 : D3DFMT_A8R8G8B8;
        return true;
    }

    uint32_t DecodeTgaSample(const uint8_t* P, uint8_t Bpp, uint8_t ImgType,
                             const uint8_t* ColorMap, UINT ColorMapEntries, uint8_t ColorMapBpp)
    {
        if (ImgType == 1 || ImgType == 9)
        {
            const uint8_t Idx = P[0];
            if (ColorMap != nullptr && Idx < ColorMapEntries)
            {
                const UINT Stride = (ColorMapBpp + 7u) / 8u;
                return DecodeTgaSample(ColorMap + (size_t)Idx * Stride, ColorMapBpp, 2, nullptr, 0, 0);
            }
            return PackArgb(255u, Idx, Idx, Idx);
        }
        if (ImgType == 3 || ImgType == 11)
            return PackArgb(255u, P[0], P[0], P[0]);
        if (Bpp == 32)
            return PackArgb(P[3], P[2], P[1], P[0]);
        if (Bpp == 24)
            return PackArgb(255u, P[2], P[1], P[0]);
        if (Bpp == 16 || Bpp == 15)
        {
            const uint16_t V = (uint16_t)(P[0] | (P[1] << 8));
            const uint8_t A = (Bpp == 16 && (V & 0x8000u) == 0) ? 0u : 255u;
            return PackArgb(A, Expand5((V >> 10) & 0x1Fu), Expand5((V >> 5) & 0x1Fu), Expand5(V & 0x1Fu));
        }
        return PackArgb(255u, P[0], P[0], P[0]);
    }

    bool DecodeTgaToArgb(const uint8_t* Data, UINT Size, UINT& OutW, UINT& OutH,
                         std::vector<uint32_t>& OutArgb)
    {
        D3DFORMAT Fmt = D3DFMT_UNKNOWN;
        uint8_t ImgType = 0, Bpp = 0, Desc = 0, CmBpp = 0;
        UINT PixOff = 0, CmEntries = 0;
        const uint8_t* ColorMap = nullptr;
        if (!ParseTgaHeader(Data, Size, OutW, OutH, Fmt, ImgType, Bpp, Desc, PixOff, ColorMap, CmEntries, CmBpp))
            return false;

        const UINT PixelBytes = (Bpp + 7u) / 8u;
        const size_t TotalPixels = (size_t)OutW * (size_t)OutH;
        std::vector<uint32_t> Linear(TotalPixels, 0u);

        const uint8_t* Cur = Data + PixOff;
        const uint8_t* End = Data + Size;
        const bool bRle = (ImgType >= 9);

        if (!bRle)
        {
            if ((size_t)(End - Cur) < TotalPixels * PixelBytes)
                return false;
            for (size_t i = 0; i < TotalPixels; ++i)
            {
                Linear[i] = DecodeTgaSample(Cur, Bpp, ImgType, ColorMap, CmEntries, CmBpp);
                Cur += PixelBytes;
            }
        }
        else
        {
            size_t Written = 0;
            while (Written < TotalPixels && Cur < End)
            {
                const uint8_t Header = *Cur++;
                const UINT Count = (Header & 0x7Fu) + 1u;
                if ((Header & 0x80u) != 0)
                {
                    if ((size_t)(End - Cur) < PixelBytes)
                        break;
                    const uint32_t Val = DecodeTgaSample(Cur, Bpp, ImgType, ColorMap, CmEntries, CmBpp);
                    Cur += PixelBytes;
                    for (UINT k = 0; k < Count && Written < TotalPixels; ++k)
                        Linear[Written++] = Val;
                }
                else
                {
                    for (UINT k = 0; k < Count && Written < TotalPixels; ++k)
                    {
                        if ((size_t)(End - Cur) < PixelBytes)
                            break;
                        Linear[Written++] = DecodeTgaSample(Cur, Bpp, ImgType, ColorMap, CmEntries, CmBpp);
                        Cur += PixelBytes;
                    }
                }
            }
        }

        const bool bTopOrigin = (Desc & 0x20u) != 0;
        OutArgb.resize(TotalPixels);
        for (UINT y = 0; y < OutH; ++y)
        {
            const UINT SrcY = bTopOrigin ? y : (OutH - 1u - y);
            std::memcpy(OutArgb.data() + (size_t)y * OutW, Linear.data() + (size_t)SrcY * OutW, (size_t)OutW * 4u);
        }
        return true;
    }

    // =========================================================================
    // WIC helper (BMP / PNG / JPG / GIF / TIFF decoding & PNG/JPG encoding)
    // =========================================================================
    struct FWicInit
    {
        bool bNeedUninit = false;
        IWICImagingFactory* Factory = nullptr;

        FWicInit()
        {
            const HRESULT CoHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            bNeedUninit = (CoHr == S_OK || CoHr == S_FALSE);
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&Factory));
        }

        ~FWicInit()
        {
            if (Factory != nullptr)
                Factory->Release();
            if (bNeedUninit)
                CoUninitialize();
        }
    };

    D3DXIMAGE_FILEFORMAT WicContainerToIff(const GUID& Container)
    {
        if (IsEqualGUID(Container, GUID_ContainerFormatBmp)) return D3DXIFF_BMP;
        if (IsEqualGUID(Container, GUID_ContainerFormatJpeg)) return D3DXIFF_JPG;
        if (IsEqualGUID(Container, GUID_ContainerFormatPng)) return D3DXIFF_PNG;
        return D3DXIFF_BMP;
    }

    bool DecodeWicImageInfo(const void* SrcData, UINT SrcSize, _D3DXIMAGE_INFO* Info)
    {
        FWicInit Wic;
        if (Wic.Factory == nullptr)
            return false;

        IWICStream* Stream = nullptr;
        if (FAILED(Wic.Factory->CreateStream(&Stream)))
            return false;
        HRESULT Hr = Stream->InitializeFromMemory((BYTE*)SrcData, SrcSize);
        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(Hr))
            Hr = Wic.Factory->CreateDecoderFromStream(Stream, nullptr, WICDecodeMetadataCacheOnDemand, &Decoder);
        IWICBitmapFrameDecode* Frame = nullptr;
        if (SUCCEEDED(Hr))
            Hr = Decoder->GetFrame(0, &Frame);
        if (SUCCEEDED(Hr))
        {
            UINT W = 0, H = 0;
            GUID Container = {};
            Frame->GetSize(&W, &H);
            Decoder->GetContainerFormat(&Container);
            Info->Width = W;
            Info->Height = H;
            Info->Depth = 1;
            Info->MipLevels = 1;
            Info->Format = D3DFMT_A8R8G8B8;
            Info->ResourceType = D3DRTYPE_TEXTURE;
            Info->ImageFileFormat = WicContainerToIff(Container);
        }
        if (Frame != nullptr) Frame->Release();
        if (Decoder != nullptr) Decoder->Release();
        Stream->Release();
        return SUCCEEDED(Hr);
    }

    bool DecodeWicToArgb(const void* SrcData, UINT SrcSize, UINT& OutW, UINT& OutH,
                         std::vector<uint32_t>& OutArgb, D3DXIMAGE_FILEFORMAT* OutIff = nullptr)
    {
        FWicInit Wic;
        if (Wic.Factory == nullptr)
            return false;

        IWICStream* Stream = nullptr;
        if (FAILED(Wic.Factory->CreateStream(&Stream)))
            return false;
        HRESULT Hr = Stream->InitializeFromMemory((BYTE*)SrcData, SrcSize);
        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(Hr))
            Hr = Wic.Factory->CreateDecoderFromStream(Stream, nullptr, WICDecodeMetadataCacheOnDemand, &Decoder);
        IWICBitmapFrameDecode* Frame = nullptr;
        if (SUCCEEDED(Hr))
            Hr = Decoder->GetFrame(0, &Frame);
        IWICFormatConverter* Conv = nullptr;
        if (SUCCEEDED(Hr))
            Hr = Wic.Factory->CreateFormatConverter(&Conv);
        if (SUCCEEDED(Hr))
            Hr = Conv->Initialize(Frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                  nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(Hr))
        {
            UINT W = 0, H = 0;
            Frame->GetSize(&W, &H);
            OutW = W;
            OutH = H;
            OutArgb.resize((size_t)W * (size_t)H);
            Hr = Conv->CopyPixels(nullptr, W * 4u, W * H * 4u, (BYTE*)OutArgb.data());
            if (SUCCEEDED(Hr) && OutIff != nullptr)
            {
                GUID Container = {};
                Decoder->GetContainerFormat(&Container);
                *OutIff = WicContainerToIff(Container);
            }
        }
        if (Conv != nullptr) Conv->Release();
        if (Frame != nullptr) Frame->Release();
        if (Decoder != nullptr) Decoder->Release();
        Stream->Release();
        return SUCCEEDED(Hr);
    }

    bool IsDdsBuffer(const void* SrcData, UINT SrcSize)
    {
        if (SrcData == nullptr || SrcSize < 4u + sizeof(FDdsHeader))
            return false;
        uint32_t Magic = 0;
        std::memcpy(&Magic, SrcData, 4);
        if (Magic != kDdsMagic)
            return false;
        FDdsHeader Hdr;
        std::memcpy(&Hdr, (const uint8_t*)SrcData + 4, sizeof(Hdr));
        return Hdr.Size == 124u && Hdr.Width > 0 && Hdr.Height > 0;
    }

    bool CheckDeviceFormatSupported(IDirect3DDevice9* Device, D3DFORMAT Format, D3DRESOURCETYPE RType)
    {
        IDirect3D9* D3d = nullptr;
        if (FAILED(Device->GetDirect3D(&D3d)) || D3d == nullptr)
            return false;
        D3DDEVICE_CREATION_PARAMETERS Cp = {};
        D3DDISPLAYMODE Mode = {};
        bool bOk = false;
        if (SUCCEEDED(Device->GetCreationParameters(&Cp)) &&
            SUCCEEDED(D3d->GetAdapterDisplayMode(Cp.AdapterOrdinal, &Mode)))
        {
            bOk = SUCCEEDED(D3d->CheckDeviceFormat(Cp.AdapterOrdinal, Cp.DeviceType, Mode.Format,
                                                   0, RType, Format));
        }
        D3d->Release();
        return bOk;
    }

    HRESULT SaveWicFile(LPCSTR FileName, const GUID& ContainerFormat,
                        const uint32_t* Bgra, UINT Width, UINT Height)
    {
        FWicInit Wic;
        if (Wic.Factory == nullptr)
            return E_FAIL;

        int WideLen = MultiByteToWideChar(CP_ACP, 0, FileName, -1, nullptr, 0);
        if (WideLen <= 0)
            return E_INVALIDARG;
        std::vector<wchar_t> WidePath((size_t)WideLen);
        MultiByteToWideChar(CP_ACP, 0, FileName, -1, WidePath.data(), WideLen);

        IWICStream* Stream = nullptr;
        HRESULT Hr = Wic.Factory->CreateStream(&Stream);
        if (SUCCEEDED(Hr))
            Hr = Stream->InitializeFromFilename(WidePath.data(), GENERIC_WRITE);
        IWICBitmapEncoder* Encoder = nullptr;
        if (SUCCEEDED(Hr))
            Hr = Wic.Factory->CreateEncoder(ContainerFormat, nullptr, &Encoder);
        if (SUCCEEDED(Hr))
            Hr = Encoder->Initialize(Stream, WICBitmapEncoderNoCache);
        IWICBitmapFrameEncode* Frame = nullptr;
        if (SUCCEEDED(Hr))
            Hr = Encoder->CreateNewFrame(&Frame, nullptr);
        if (SUCCEEDED(Hr))
            Hr = Frame->Initialize(nullptr);
        if (SUCCEEDED(Hr))
            Hr = Frame->SetSize(Width, Height);
        WICPixelFormatGUID PixFmt = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(Hr))
            Hr = Frame->SetPixelFormat(&PixFmt);
        if (SUCCEEDED(Hr))
            Hr = Frame->WritePixels(Height, Width * 4u, Width * Height * 4u, (BYTE*)Bgra);
        if (SUCCEEDED(Hr))
            Hr = Frame->Commit();
        if (SUCCEEDED(Hr))
            Hr = Encoder->Commit();

        if (Frame != nullptr) Frame->Release();
        if (Encoder != nullptr) Encoder->Release();
        if (Stream != nullptr) Stream->Release();
        return Hr;
    }

    HRESULT SaveBmpFile(LPCSTR FileName, const uint32_t* Bgra, UINT Width, UINT Height)
    {
        FILE* Fp = std::fopen(FileName, "wb");
        if (Fp == nullptr)
            return E_FAIL;
        const UINT RowBytes = ((Width * 3u + 3u) & ~3u);
        const UINT PixelBytes = RowBytes * Height;

        BITMAPFILEHEADER Bfh = {};
        Bfh.bfType = 0x4D42; // "BM"
        Bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        Bfh.bfSize = Bfh.bfOffBits + PixelBytes;

        BITMAPINFOHEADER Bih = {};
        Bih.biSize = sizeof(BITMAPINFOHEADER);
        Bih.biWidth = (LONG)Width;
        Bih.biHeight = (LONG)Height; // bottom-up
        Bih.biPlanes = 1;
        Bih.biBitCount = 24;
        Bih.biCompression = BI_RGB;
        Bih.biSizeImage = PixelBytes;

        std::fwrite(&Bfh, sizeof(Bfh), 1, Fp);
        std::fwrite(&Bih, sizeof(Bih), 1, Fp);

        std::vector<uint8_t> Row(RowBytes, 0u);
        for (int y = (int)Height - 1; y >= 0; --y)
        {
            const uint32_t* Src = Bgra + (size_t)y * Width;
            for (UINT x = 0; x < Width; ++x)
            {
                const uint32_t C = Src[x];
                Row[x * 3 + 0] = (uint8_t)(C);
                Row[x * 3 + 1] = (uint8_t)(C >> 8);
                Row[x * 3 + 2] = (uint8_t)(C >> 16);
            }
            std::fwrite(Row.data(), 1, RowBytes, Fp);
        }
        std::fclose(Fp);
        return S_OK;
    }

    HRESULT SaveTgaFile(LPCSTR FileName, const uint32_t* Bgra, UINT Width, UINT Height)
    {
        FILE* Fp = std::fopen(FileName, "wb");
        if (Fp == nullptr)
            return E_FAIL;
        uint8_t Header[18] = {};
        Header[2] = 2; // uncompressed true-color
        Header[12] = (uint8_t)(Width & 0xFFu);
        Header[13] = (uint8_t)((Width >> 8) & 0xFFu);
        Header[14] = (uint8_t)(Height & 0xFFu);
        Header[15] = (uint8_t)((Height >> 8) & 0xFFu);
        Header[16] = 32;
        Header[17] = 0x28; // top-left origin + 8 alpha bits
        std::fwrite(Header, 1, sizeof(Header), Fp);
        std::fwrite(Bgra, 4, (size_t)Width * (size_t)Height, Fp);
        std::fclose(Fp);
        return S_OK;
    }

    HRESULT SaveDdsFile(LPCSTR FileName, const uint32_t* Bgra, UINT Width, UINT Height)
    {
        FILE* Fp = std::fopen(FileName, "wb");
        if (Fp == nullptr)
            return E_FAIL;
        FDdsHeader Hdr = {};
        Hdr.Size = 124;
        Hdr.Flags = 0x1u | 0x2u | 0x4u | 0x8u | 0x1000u; // CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT
        Hdr.Height = Height;
        Hdr.Width = Width;
        Hdr.PitchOrLinearSize = Width * 4u;
        Hdr.PixelFormat.Size = 32;
        Hdr.PixelFormat.Flags = kDdpfRgb | kDdpfAlphaPixels;
        Hdr.PixelFormat.RgbBitCount = 32;
        Hdr.PixelFormat.RBitMask = 0x00FF0000u;
        Hdr.PixelFormat.GBitMask = 0x0000FF00u;
        Hdr.PixelFormat.BBitMask = 0x000000FFu;
        Hdr.PixelFormat.ABitMask = 0xFF000000u;
        Hdr.Caps = 0x1000u; // DDSCAPS_TEXTURE
        std::fwrite(&kDdsMagic, 4, 1, Fp);
        std::fwrite(&Hdr, sizeof(Hdr), 1, Fp);
        std::fwrite(Bgra, 4, (size_t)Width * (size_t)Height, Fp);
        std::fclose(Fp);
        return S_OK;
    }
} // namespace

const char* Rp9HrToString(HRESULT Hr)
{
    switch (Hr)
    {
    case D3D_OK:                                return "D3D_OK";
    case S_FALSE:                               return "S_FALSE";
    case E_FAIL:                                return "E_FAIL";
    case E_INVALIDARG:                          return "E_INVALIDARG";
    case E_OUTOFMEMORY:                         return "E_OUTOFMEMORY";
    case E_NOINTERFACE:                         return "E_NOINTERFACE";
    case E_POINTER:                             return "E_POINTER";
    case E_NOTIMPL:                             return "E_NOTIMPL";
    case E_ACCESSDENIED:                        return "E_ACCESSDENIED";
    case D3DERR_WRONGTEXTUREFORMAT:             return "D3DERR_WRONGTEXTUREFORMAT";
    case D3DERR_UNSUPPORTEDCOLOROPERATION:      return "D3DERR_UNSUPPORTEDCOLOROPERATION";
    case D3DERR_UNSUPPORTEDCOLORARG:            return "D3DERR_UNSUPPORTEDCOLORARG";
    case D3DERR_UNSUPPORTEDALPHAOPERATION:      return "D3DERR_UNSUPPORTEDALPHAOPERATION";
    case D3DERR_UNSUPPORTEDALPHAARG:            return "D3DERR_UNSUPPORTEDALPHAARG";
    case D3DERR_TOOMANYOPERATIONS:              return "D3DERR_TOOMANYOPERATIONS";
    case D3DERR_CONFLICTINGTEXTUREFILTER:       return "D3DERR_CONFLICTINGTEXTUREFILTER";
    case D3DERR_UNSUPPORTEDFACTORVALUE:         return "D3DERR_UNSUPPORTEDFACTORVALUE";
    case D3DERR_CONFLICTINGRENDERSTATE:         return "D3DERR_CONFLICTINGRENDERSTATE";
    case D3DERR_UNSUPPORTEDTEXTUREFILTER:       return "D3DERR_UNSUPPORTEDTEXTUREFILTER";
    case D3DERR_CONFLICTINGTEXTUREPALETTE:      return "D3DERR_CONFLICTINGTEXTUREPALETTE";
    case D3DERR_DRIVERINTERNALERROR:            return "D3DERR_DRIVERINTERNALERROR";
    case D3DERR_NOTFOUND:                       return "D3DERR_NOTFOUND";
    case D3DERR_MOREDATA:                       return "D3DERR_MOREDATA";
    case D3DERR_DEVICELOST:                     return "D3DERR_DEVICELOST";
    case D3DERR_DEVICENOTRESET:                 return "D3DERR_DEVICENOTRESET";
    case D3DERR_NOTAVAILABLE:                   return "D3DERR_NOTAVAILABLE";
    case D3DERR_OUTOFVIDEOMEMORY:               return "D3DERR_OUTOFVIDEOMEMORY";
    case D3DERR_INVALIDDEVICE:                  return "D3DERR_INVALIDDEVICE";
    case D3DERR_INVALIDCALL:                    return "D3DERR_INVALIDCALL";
    case D3DERR_DRIVERINVALIDCALL:              return "D3DERR_DRIVERINVALIDCALL";
    case D3DERR_WASSTILLDRAWING:                return "D3DERR_WASSTILLDRAWING";
    case D3DOK_NOAUTOGEN:                       return "D3DOK_NOAUTOGEN";
    default:
    {
        static char UnknownBuf[32];
        _snprintf(UnknownBuf, sizeof(UnknownBuf), "HRESULT(0x%08lX)", (unsigned long)Hr);
        UnknownBuf[sizeof(UnknownBuf) - 1] = '\0';
        return UnknownBuf;
    }
    }
}

HRESULT Rp9LoadSurfaceFromMemory(IDirect3DSurface9* DstSurface,
                                 const PALETTEENTRY* /*DstPalette*/,
                                 const RECT* DstRect,
                                 LPCVOID SrcMemory,
                                 D3DFORMAT SrcFormat,
                                 UINT SrcPitch,
                                 const PALETTEENTRY* SrcPalette,
                                 const RECT* SrcRect,
                                 DWORD Filter,
                                 D3DCOLOR ColorKey)
{
    if (DstSurface == nullptr || SrcMemory == nullptr || SrcRect == nullptr)
        return D3DERR_INVALIDCALL;

    D3DSURFACE_DESC DstDesc;
    HRESULT Hr = DstSurface->GetDesc(&DstDesc);
    if (FAILED(Hr))
        return Hr;

    RECT EffDst = { 0, 0, (LONG)DstDesc.Width, (LONG)DstDesc.Height };
    if (DstRect != nullptr)
        EffDst = *DstRect;

    const UINT SrcW = (UINT)(SrcRect->right - SrcRect->left);
    const UINT SrcH = (UINT)(SrcRect->bottom - SrcRect->top);
    const UINT DstW = (UINT)(EffDst.right - EffDst.left);
    const UINT DstH = (UINT)(EffDst.bottom - EffDst.top);
    if (SrcW == 0 || SrcH == 0 || DstW == 0 || DstH == 0)
        return D3DERR_INVALIDCALL;

    D3DLOCKED_RECT Locked;
    Hr = DstSurface->LockRect(&Locked, (DstRect != nullptr) ? &EffDst : nullptr, 0);
    if (FAILED(Hr))
        return Hr;

    const uint8_t* SrcBase = (const uint8_t*)SrcMemory;
    uint8_t* DstBase = (uint8_t*)Locked.pBits;

    // Fast path: identical format and identical dimensions, no color key
    if (SrcFormat == DstDesc.Format && SrcW == DstW && SrcH == DstH && ColorKey == 0)
    {
        if (IsDxtFormat(SrcFormat))
        {
            const UINT RowBytes = ComputeSurfacePitch(SrcFormat, SrcW);
            const UINT Rows = ComputeSurfaceRows(SrcFormat, SrcH);
            const uint8_t* S = SrcBase + ((size_t)(SrcRect->top / 4) * SrcPitch) +
                               ((size_t)(SrcRect->left / 4) * DxtBlockBytes(SrcFormat));
            for (UINT r = 0; r < Rows; ++r)
                std::memcpy(DstBase + (size_t)r * Locked.Pitch, S + (size_t)r * SrcPitch, RowBytes);
        }
        else
        {
            const UINT Bpp = UncompressedBytesPerPixel(SrcFormat);
            if (Bpp == 0)
            {
                DstSurface->UnlockRect();
                return D3DERR_INVALIDCALL;
            }
            const UINT RowBytes = SrcW * Bpp;
            const uint8_t* S = SrcBase + (size_t)SrcRect->top * SrcPitch + (size_t)SrcRect->left * Bpp;
            for (UINT y = 0; y < SrcH; ++y)
                std::memcpy(DstBase + (size_t)y * Locked.Pitch, S + (size_t)y * SrcPitch, RowBytes);
        }
        DstSurface->UnlockRect();
        return D3D_OK;
    }

    // Cannot encode uncompressed source into a compressed DXT destination on CPU.
    if (IsDxtFormat(DstDesc.Format))
    {
        DstSurface->UnlockRect();
        return E_NOTIMPL;
    }

    const UINT DstBpp = UncompressedBytesPerPixel(DstDesc.Format);
    if (DstBpp == 0)
    {
        DstSurface->UnlockRect();
        return D3DERR_INVALIDCALL;
    }

    // Decode source rect to canonical ARGB8888
    std::vector<uint32_t> SrcArgb;
    if (IsDxtFormat(SrcFormat))
    {
        const uint8_t* S = SrcBase + ((size_t)(SrcRect->top / 4) * SrcPitch) +
                           ((size_t)(SrcRect->left / 4) * DxtBlockBytes(SrcFormat));
        DecompressDxtToArgb(S, SrcPitch, SrcFormat, SrcW, SrcH, SrcArgb);
    }
    else
    {
        const UINT SrcBpp = UncompressedBytesPerPixel(SrcFormat);
        if (SrcBpp == 0)
        {
            DstSurface->UnlockRect();
            return D3DERR_INVALIDCALL;
        }
        SrcArgb.resize((size_t)SrcW * (size_t)SrcH);
        for (UINT y = 0; y < SrcH; ++y)
        {
            const uint8_t* Row = SrcBase + (size_t)(SrcRect->top + (LONG)y) * SrcPitch +
                                 (size_t)SrcRect->left * SrcBpp;
            uint32_t* Out = SrcArgb.data() + (size_t)y * SrcW;
            for (UINT x = 0; x < SrcW; ++x)
            {
                uint32_t C = ReadPixelArgb(Row + (size_t)x * SrcBpp, SrcFormat, SrcPalette);
                if (ColorKey != 0 && (C & 0x00FFFFFFu) == (ColorKey & 0x00FFFFFFu))
                    C = 0u;
                Out[x] = C;
            }
        }
    }

    std::vector<uint32_t> Resampled;
    const uint32_t* FinalPixels = SrcArgb.data();
    if (SrcW != DstW || SrcH != DstH)
    {
        ResampleArgb(SrcArgb.data(), SrcW, SrcH, Resampled, DstW, DstH, Filter);
        FinalPixels = Resampled.data();
    }

    for (UINT y = 0; y < DstH; ++y)
    {
        uint8_t* DstRow = DstBase + (size_t)y * Locked.Pitch;
        const uint32_t* SrcRow = FinalPixels + (size_t)y * DstW;
        for (UINT x = 0; x < DstW; ++x)
            WritePixelArgb(DstRow + (size_t)x * DstBpp, DstDesc.Format, SrcRow[x]);
    }

    DstSurface->UnlockRect();
    return D3D_OK;
}

HRESULT Rp9LoadSurfaceFromSurface(IDirect3DSurface9* DstSurface,
                                  const PALETTEENTRY* DstPalette,
                                  const RECT* DstRect,
                                  IDirect3DSurface9* SrcSurface,
                                  const PALETTEENTRY* SrcPalette,
                                  const RECT* SrcRect,
                                  DWORD Filter,
                                  D3DCOLOR ColorKey)
{
    if (DstSurface == nullptr || SrcSurface == nullptr)
        return D3DERR_INVALIDCALL;

    D3DSURFACE_DESC SrcDesc;
    HRESULT Hr = SrcSurface->GetDesc(&SrcDesc);
    if (FAILED(Hr))
        return Hr;

    RECT EffSrc = { 0, 0, (LONG)SrcDesc.Width, (LONG)SrcDesc.Height };
    if (SrcRect != nullptr)
        EffSrc = *SrcRect;

    D3DLOCKED_RECT SrcLock;
    Hr = SrcSurface->LockRect(&SrcLock, &EffSrc, D3DLOCK_READONLY);
    if (FAILED(Hr))
        return Hr;

    // EffSrc is locked directly, so SrcLock.pBits starts at (0,0) of EffSrc.
    RECT LocalSrc = { 0, 0, EffSrc.right - EffSrc.left, EffSrc.bottom - EffSrc.top };
    Hr = Rp9LoadSurfaceFromMemory(DstSurface, DstPalette, DstRect,
                                  SrcLock.pBits, SrcDesc.Format, (UINT)SrcLock.Pitch,
                                  SrcPalette, &LocalSrc, Filter, ColorKey);
    SrcSurface->UnlockRect();
    return Hr;
}

HRESULT Rp9GetImageInfoFromMemory(LPCVOID SrcData, UINT SrcDataSize, _D3DXIMAGE_INFO* Info)
{
    if (SrcData == nullptr || SrcDataSize == 0 || Info == nullptr)
        return D3DERR_INVALIDCALL;

    if (IsDdsBuffer(SrcData, SrcDataSize))
    {
        FDdsHeader Hdr;
        std::memcpy(&Hdr, (const uint8_t*)SrcData + 4, sizeof(Hdr));
        const D3DFORMAT Fmt = DdsPixelFormatToD3d(Hdr.PixelFormat);
        if (Fmt == D3DFMT_UNKNOWN)
            return E_FAIL;
        Info->Width = Hdr.Width;
        Info->Height = Hdr.Height;
        Info->Depth = (Hdr.Caps2 & kDdsCaps2Volume) ? std::max<UINT>(1u, Hdr.Depth) : 1u;
        Info->MipLevels = std::max<UINT>(1u, Hdr.MipMapCount);
        Info->Format = Fmt;
        if ((Hdr.Caps2 & kDdsCaps2Cubemap) != 0)
            Info->ResourceType = D3DRTYPE_CUBETEXTURE;
        else if ((Hdr.Caps2 & kDdsCaps2Volume) != 0)
            Info->ResourceType = D3DRTYPE_VOLUMETEXTURE;
        else
            Info->ResourceType = D3DRTYPE_TEXTURE;
        Info->ImageFileFormat = D3DXIFF_DDS;
        return D3D_OK;
    }

    // Try WIC first (handles BMP, PNG, JPG, GIF, TIFF cleanly by magic header)
    if (DecodeWicImageInfo(SrcData, SrcDataSize, Info))
        return D3D_OK;

    // Fallback: TGA (has no leading magic bytes)
    UINT W = 0, H = 0, PixOff = 0, CmEntries = 0;
    D3DFORMAT Fmt = D3DFMT_UNKNOWN;
    uint8_t ImgType = 0, Bpp = 0, Desc = 0, CmBpp = 0;
    const uint8_t* ColorMap = nullptr;
    if (ParseTgaHeader((const uint8_t*)SrcData, SrcDataSize, W, H, Fmt, ImgType, Bpp, Desc,
                       PixOff, ColorMap, CmEntries, CmBpp))
    {
        Info->Width = W;
        Info->Height = H;
        Info->Depth = 1;
        Info->MipLevels = 1;
        Info->Format = Fmt;
        Info->ResourceType = D3DRTYPE_TEXTURE;
        Info->ImageFileFormat = D3DXIFF_TGA;
        return D3D_OK;
    }

    return E_FAIL;
}

HRESULT Rp9LoadSurfaceFromFileInMemory(IDirect3DSurface9* DstSurface,
                                       const PALETTEENTRY* DstPalette,
                                       const RECT* DstRect,
                                       LPCVOID SrcData,
                                       UINT SrcDataSize,
                                       const RECT* SrcRect,
                                       DWORD Filter,
                                       D3DCOLOR ColorKey,
                                       _D3DXIMAGE_INFO* SrcInfo)
{
    if (DstSurface == nullptr || SrcData == nullptr || SrcDataSize == 0)
        return D3DERR_INVALIDCALL;

    if (IsDdsBuffer(SrcData, SrcDataSize))
    {
        FDdsHeader Hdr;
        std::memcpy(&Hdr, (const uint8_t*)SrcData + 4, sizeof(Hdr));
        const D3DFORMAT Fmt = DdsPixelFormatToD3d(Hdr.PixelFormat);
        if (Fmt == D3DFMT_UNKNOWN)
            return E_FAIL;
        const UINT Pitch = ComputeSurfacePitch(Fmt, Hdr.Width);
        const UINT Needed = ComputeSurfaceByteSize(Fmt, Hdr.Width, Hdr.Height);
        if (4u + sizeof(FDdsHeader) + Needed > SrcDataSize)
            return E_FAIL;

        if (SrcInfo != nullptr)
            Rp9GetImageInfoFromMemory(SrcData, SrcDataSize, SrcInfo);

        RECT FullRect = { 0, 0, (LONG)Hdr.Width, (LONG)Hdr.Height };
        return Rp9LoadSurfaceFromMemory(DstSurface, DstPalette, DstRect,
                                        (const uint8_t*)SrcData + 4 + sizeof(FDdsHeader),
                                        Fmt, Pitch, nullptr,
                                        (SrcRect != nullptr) ? SrcRect : &FullRect,
                                        Filter, ColorKey);
    }

    UINT W = 0, H = 0;
    std::vector<uint32_t> Argb;
    D3DXIMAGE_FILEFORMAT Iff = D3DXIFF_BMP;
    bool bDecoded = DecodeWicToArgb(SrcData, SrcDataSize, W, H, Argb, &Iff);
    if (!bDecoded)
    {
        bDecoded = DecodeTgaToArgb((const uint8_t*)SrcData, SrcDataSize, W, H, Argb);
        Iff = D3DXIFF_TGA;
    }
    if (!bDecoded)
        return E_FAIL;

    if (SrcInfo != nullptr)
    {
        SrcInfo->Width = W;
        SrcInfo->Height = H;
        SrcInfo->Depth = 1;
        SrcInfo->MipLevels = 1;
        SrcInfo->Format = D3DFMT_A8R8G8B8;
        SrcInfo->ResourceType = D3DRTYPE_TEXTURE;
        SrcInfo->ImageFileFormat = Iff;
    }

    RECT FullRect = { 0, 0, (LONG)W, (LONG)H };
    return Rp9LoadSurfaceFromMemory(DstSurface, DstPalette, DstRect,
                                    Argb.data(), D3DFMT_A8R8G8B8, W * 4u, nullptr,
                                    (SrcRect != nullptr) ? SrcRect : &FullRect,
                                    Filter, ColorKey);
}

HRESULT Rp9CreateTextureFromFileInMemory(IDirect3DDevice9* Device,
                                         LPCVOID SrcData,
                                         UINT SrcDataSize,
                                         IDirect3DTexture9** OutTexture)
{
    if (Device == nullptr || SrcData == nullptr || SrcDataSize == 0 || OutTexture == nullptr)
        return D3DERR_INVALIDCALL;
    *OutTexture = nullptr;

    if (IsDdsBuffer(SrcData, SrcDataSize))
    {
        FDdsHeader Hdr;
        std::memcpy(&Hdr, (const uint8_t*)SrcData + 4, sizeof(Hdr));
        const D3DFORMAT SrcFmt = DdsPixelFormatToD3d(Hdr.PixelFormat);
        if (SrcFmt == D3DFMT_UNKNOWN)
            return E_FAIL;

        const D3DFORMAT DstFmt = CheckDeviceFormatSupported(Device, SrcFmt, D3DRTYPE_TEXTURE)
                                     ? SrcFmt
                                     : D3DFMT_A8R8G8B8;
        const UINT Mips = std::max<UINT>(1u, Hdr.MipMapCount);

        IDirect3DTexture9* Tex = nullptr;
        HRESULT Hr = Device->CreateTexture(Hdr.Width, Hdr.Height, Mips, 0, DstFmt,
                                           D3DPOOL_MANAGED, &Tex, nullptr);
        if (FAILED(Hr))
            return Hr;

        const uint8_t* Cur = (const uint8_t*)SrcData + 4 + sizeof(FDdsHeader);
        const uint8_t* End = (const uint8_t*)SrcData + SrcDataSize;
        UINT W = Hdr.Width;
        UINT H = Hdr.Height;
        const UINT CreatedLevels = Tex->GetLevelCount();
        for (UINT Level = 0; Level < CreatedLevels && Level < Mips; ++Level)
        {
            const UINT Pitch = ComputeSurfacePitch(SrcFmt, W);
            const UINT Bytes = ComputeSurfaceByteSize(SrcFmt, W, H);
            if (Cur + Bytes > End)
                break;

            IDirect3DSurface9* Surf = nullptr;
            Hr = Tex->GetSurfaceLevel(Level, &Surf);
            if (SUCCEEDED(Hr))
            {
                RECT R = { 0, 0, (LONG)W, (LONG)H };
                Hr = Rp9LoadSurfaceFromMemory(Surf, nullptr, nullptr, Cur, SrcFmt, Pitch,
                                              nullptr, &R, D3DX_FILTER_NONE, 0);
                Surf->Release();
            }
            if (FAILED(Hr))
            {
                Tex->Release();
                return Hr;
            }
            Cur += Bytes;
            W = std::max<UINT>(1u, W >> 1);
            H = std::max<UINT>(1u, H >> 1);
        }

        *OutTexture = Tex;
        return D3D_OK;
    }

    // Non-DDS (WIC BMP/PNG/JPG or TGA)
    UINT W = 0, H = 0;
    std::vector<uint32_t> Argb;
    if (!DecodeWicToArgb(SrcData, SrcDataSize, W, H, Argb) &&
        !DecodeTgaToArgb((const uint8_t*)SrcData, SrcDataSize, W, H, Argb))
        return E_FAIL;

    IDirect3DTexture9* Tex = nullptr;
    HRESULT Hr = Device->CreateTexture(W, H, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &Tex, nullptr);
    if (FAILED(Hr))
        return Hr;

    IDirect3DSurface9* Surf = nullptr;
    Hr = Tex->GetSurfaceLevel(0, &Surf);
    if (SUCCEEDED(Hr))
    {
        RECT R = { 0, 0, (LONG)W, (LONG)H };
        Hr = Rp9LoadSurfaceFromMemory(Surf, nullptr, nullptr, Argb.data(),
                                      D3DFMT_A8R8G8B8, W * 4u, nullptr, &R, D3DX_FILTER_NONE, 0);
        Surf->Release();
    }
    if (FAILED(Hr))
    {
        Tex->Release();
        return Hr;
    }

    *OutTexture = Tex;
    return D3D_OK;
}

HRESULT Rp9CreateCubeTextureFromFileInMemory(IDirect3DDevice9* Device,
                                             LPCVOID SrcData,
                                             UINT SrcDataSize,
                                             IDirect3DCubeTexture9** OutCube)
{
    if (Device == nullptr || SrcData == nullptr || SrcDataSize == 0 || OutCube == nullptr)
        return D3DERR_INVALIDCALL;
    *OutCube = nullptr;

    if (!IsDdsBuffer(SrcData, SrcDataSize))
        return E_FAIL;

    FDdsHeader Hdr;
    std::memcpy(&Hdr, (const uint8_t*)SrcData + 4, sizeof(Hdr));
    if ((Hdr.Caps2 & kDdsCaps2Cubemap) == 0 || Hdr.Width != Hdr.Height)
        return E_FAIL;

    const D3DFORMAT SrcFmt = DdsPixelFormatToD3d(Hdr.PixelFormat);
    if (SrcFmt == D3DFMT_UNKNOWN)
        return E_FAIL;

    const D3DFORMAT DstFmt = CheckDeviceFormatSupported(Device, SrcFmt, D3DRTYPE_CUBETEXTURE)
                                 ? SrcFmt
                                 : D3DFMT_A8R8G8B8;
    const UINT Mips = std::max<UINT>(1u, Hdr.MipMapCount);

    IDirect3DCubeTexture9* Cube = nullptr;
    HRESULT Hr = Device->CreateCubeTexture(Hdr.Width, Mips, 0, DstFmt, D3DPOOL_MANAGED, &Cube, nullptr);
    if (FAILED(Hr))
        return Hr;

    static const uint32_t kFaceFlags[6] = {
        kDdsCaps2CubemapPosX, kDdsCaps2CubemapNegX,
        kDdsCaps2CubemapPosY, kDdsCaps2CubemapNegY,
        kDdsCaps2CubemapPosZ, kDdsCaps2CubemapNegZ
    };
    static const D3DCUBEMAP_FACES kD3dFaces[6] = {
        D3DCUBEMAP_FACE_POSITIVE_X, D3DCUBEMAP_FACE_NEGATIVE_X,
        D3DCUBEMAP_FACE_POSITIVE_Y, D3DCUBEMAP_FACE_NEGATIVE_Y,
        D3DCUBEMAP_FACE_POSITIVE_Z, D3DCUBEMAP_FACE_NEGATIVE_Z
    };

    const uint8_t* Cur = (const uint8_t*)SrcData + 4 + sizeof(FDdsHeader);
    const uint8_t* End = (const uint8_t*)SrcData + SrcDataSize;
    const UINT CreatedLevels = Cube->GetLevelCount();

    for (int f = 0; f < 6; ++f)
    {
        if ((Hdr.Caps2 & kFaceFlags[f]) == 0)
            continue;
        UINT Dim = Hdr.Width;
        for (UINT Level = 0; Level < Mips; ++Level)
        {
            const UINT Pitch = ComputeSurfacePitch(SrcFmt, Dim);
            const UINT Bytes = ComputeSurfaceByteSize(SrcFmt, Dim, Dim);
            if (Cur + Bytes > End)
                break;

            if (Level < CreatedLevels)
            {
                IDirect3DSurface9* Surf = nullptr;
                Hr = Cube->GetCubeMapSurface(kD3dFaces[f], Level, &Surf);
                if (SUCCEEDED(Hr))
                {
                    RECT R = { 0, 0, (LONG)Dim, (LONG)Dim };
                    Hr = Rp9LoadSurfaceFromMemory(Surf, nullptr, nullptr, Cur, SrcFmt, Pitch,
                                                  nullptr, &R, D3DX_FILTER_NONE, 0);
                    Surf->Release();
                }
                if (FAILED(Hr))
                {
                    Cube->Release();
                    return Hr;
                }
            }
            Cur += Bytes;
            Dim = std::max<UINT>(1u, Dim >> 1);
        }
    }

    *OutCube = Cube;
    return D3D_OK;
}

HRESULT Rp9SaveSurfaceToFileA(LPCSTR DestFile,
                              D3DXIMAGE_FILEFORMAT DestFormat,
                              IDirect3DSurface9* SrcSurface,
                              const PALETTEENTRY* SrcPalette,
                              const RECT* SrcRect)
{
    if (DestFile == nullptr || SrcSurface == nullptr)
        return D3DERR_INVALIDCALL;

    D3DSURFACE_DESC Desc;
    HRESULT Hr = SrcSurface->GetDesc(&Desc);
    if (FAILED(Hr))
        return Hr;

    RECT R = { 0, 0, (LONG)Desc.Width, (LONG)Desc.Height };
    if (SrcRect != nullptr)
    {
        R.left   = std::max<LONG>(0, std::min<LONG>((LONG)Desc.Width, SrcRect->left));
        R.top    = std::max<LONG>(0, std::min<LONG>((LONG)Desc.Height, SrcRect->top));
        R.right  = std::max<LONG>(R.left, std::min<LONG>((LONG)Desc.Width, SrcRect->right));
        R.bottom = std::max<LONG>(R.top, std::min<LONG>((LONG)Desc.Height, SrcRect->bottom));
    }
    const UINT W = (UINT)(R.right - R.left);
    const UINT H = (UINT)(R.bottom - R.top);
    if (W == 0 || H == 0)
        return D3DERR_INVALIDCALL;

    D3DLOCKED_RECT Locked;
    Hr = SrcSurface->LockRect(&Locked, &R, D3DLOCK_READONLY);
    if (FAILED(Hr))
        return Hr;

    std::vector<uint32_t> Bgra((size_t)W * (size_t)H);
    const UINT Bpp = UncompressedBytesPerPixel(Desc.Format);
    if (Bpp == 0)
    {
        SrcSurface->UnlockRect();
        return D3DERR_INVALIDCALL;
    }

    for (UINT y = 0; y < H; ++y)
    {
        const uint8_t* Row = (const uint8_t*)Locked.pBits + (size_t)y * Locked.Pitch;
        uint32_t* Out = Bgra.data() + (size_t)y * W;
        for (UINT x = 0; x < W; ++x)
            Out[x] = ReadPixelArgb(Row + (size_t)x * Bpp, Desc.Format, SrcPalette);
    }
    SrcSurface->UnlockRect();

    switch (DestFormat)
    {
    case D3DXIFF_BMP:
    case D3DXIFF_DIB:
        return SaveBmpFile(DestFile, Bgra.data(), W, H);
    case D3DXIFF_TGA:
        return SaveTgaFile(DestFile, Bgra.data(), W, H);
    case D3DXIFF_DDS:
        return SaveDdsFile(DestFile, Bgra.data(), W, H);
    case D3DXIFF_PNG:
        return SaveWicFile(DestFile, GUID_ContainerFormatPng, Bgra.data(), W, H);
    case D3DXIFF_JPG:
        return SaveWicFile(DestFile, GUID_ContainerFormatJpeg, Bgra.data(), W, H);
    default:
        return SaveBmpFile(DestFile, Bgra.data(), W, H);
    }
}
