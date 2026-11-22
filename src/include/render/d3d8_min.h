// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Zero-SDK Direct3D 9 header bridge: uses <d3d9.h> from the Windows 10/11 SDK exclusively.
// The public render interfaces (IRenderPipeline, Shading, flmaterials, Deformable2, alchemy,
// RendComp, freelancer) keep Freelancer's D3D8-numbered render-state/texture-stage values
// and identical-layout D3DLIGHT/D3DMATERIAL/D3DXIMAGE_INFO structs, which RP9 translates to
// Direct3D 9 calls internally.

#ifndef DIRECT3D_VERSION
#define DIRECT3D_VERSION 0x0900
#endif

#include <stddef.h>
#include <windows.h>

#include <d3d9.h>

// D3DLIGHT9 / D3DMATERIAL9 are field-for-field identical to D3D8's D3DLIGHT8 / D3DMATERIAL8.
typedef D3DLIGHT9 D3DLIGHT8;
typedef D3DMATERIAL9 D3DMATERIAL8;

static_assert(sizeof(D3DLIGHT8) == 0x68, "D3DLIGHT9 has the D3DLIGHT8 layout");
static_assert(sizeof(D3DMATERIAL8) == 0x44, "D3DMATERIAL9 has the D3DMATERIAL8 layout");
static_assert(sizeof(D3DRENDERSTATETYPE) == 4, "D3DRENDERSTATETYPE is one dword");

// D3D8-numbered render states removed or renamed in d3d9types.h (RP9 translates them).
static const D3DRENDERSTATETYPE D3DRS_LINEPATTERN = static_cast<D3DRENDERSTATETYPE>(10);
static const D3DRENDERSTATETYPE D3DRS_ZVISIBLE = static_cast<D3DRENDERSTATETYPE>(30);
static const D3DRENDERSTATETYPE D3DRS_EDGEANTIALIAS = static_cast<D3DRENDERSTATETYPE>(40);
static const D3DRENDERSTATETYPE D3DRS_ZBIAS = static_cast<D3DRENDERSTATETYPE>(47);
static const D3DRENDERSTATETYPE D3DRS_SOFTWAREVERTEXPROCESSING = static_cast<D3DRENDERSTATETYPE>(153);
static const D3DRENDERSTATETYPE D3DRS_PATCHSEGMENTS = static_cast<D3DRENDERSTATETYPE>(164);

// D3D8-numbered texture-stage states moved to D3DSAMP_* in Direct3D 9 (RP9 translates them).
static const D3DTEXTURESTAGESTATETYPE D3DTSS_ADDRESSU = static_cast<D3DTEXTURESTAGESTATETYPE>(13);
static const D3DTEXTURESTAGESTATETYPE D3DTSS_ADDRESSV = static_cast<D3DTEXTURESTAGESTATETYPE>(14);
static const D3DTEXTURESTAGESTATETYPE D3DTSS_BORDERCOLOR = static_cast<D3DTEXTURESTAGESTATETYPE>(15);
static const D3DTEXTURESTAGESTATETYPE D3DTSS_MAGFILTER = static_cast<D3DTEXTURESTAGESTATETYPE>(16);
static const D3DTEXTURESTAGESTATETYPE D3DTSS_MINFILTER = static_cast<D3DTEXTURESTAGESTATETYPE>(17);
static const D3DTEXTURESTAGESTATETYPE D3DTSS_MIPFILTER = static_cast<D3DTEXTURESTAGESTATETYPE>(18);
static const D3DTEXTURESTAGESTATETYPE D3DTSS_MIPMAPLODBIAS = static_cast<D3DTEXTURESTAGESTATETYPE>(19);
static const D3DTEXTURESTAGESTATETYPE D3DTSS_MAXMIPLEVEL = static_cast<D3DTEXTURESTAGESTATETYPE>(20);
static const D3DTEXTURESTAGESTATETYPE D3DTSS_MAXANISOTROPY = static_cast<D3DTEXTURESTAGESTATETYPE>(21);
static const D3DTEXTURESTAGESTATETYPE D3DTSS_ADDRESSW = static_cast<D3DTEXTURESTAGESTATETYPE>(25);

// D3DXIMAGE_FILEFORMAT & _D3DXIMAGE_INFO (identical 7-dword layout in D3DX8 and D3DX9).
#ifndef D3DX_FILTER_NONE
#define D3DX_FILTER_NONE     1u
#define D3DX_FILTER_POINT    2u
#define D3DX_FILTER_LINEAR   3u
#define D3DX_FILTER_TRIANGLE 4u
#define D3DX_FILTER_BOX      5u
#endif

typedef enum _D3DXIMAGE_FILEFORMAT
{
    D3DXIFF_BMP = 0,
    D3DXIFF_JPG = 1,
    D3DXIFF_TGA = 2,
    D3DXIFF_PNG = 3,
    D3DXIFF_DDS = 4,
    D3DXIFF_PPM = 5,
    D3DXIFF_DIB = 6,
    D3DXIFF_FORCE_DWORD = 0x7fffffff
} D3DXIMAGE_FILEFORMAT;

typedef struct _D3DXIMAGE_INFO
{
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    D3DFORMAT Format;
    D3DRESOURCETYPE ResourceType;
    D3DXIMAGE_FILEFORMAT ImageFileFormat;
} D3DXIMAGE_INFO;

static_assert(sizeof(_D3DXIMAGE_INFO) == 0x1C, "D3DXIMAGE_INFO is 7 dwords");
static_assert(offsetof(_D3DXIMAGE_INFO, Format) == 0x10, "D3DXIMAGE_INFO Format");
static_assert(offsetof(_D3DXIMAGE_INFO, ResourceType) == 0x14, "D3DXIMAGE_INFO ResourceType");
static_assert(offsetof(_D3DXIMAGE_INFO, ImageFileFormat) == 0x18, "D3DXIMAGE_INFO ImageFileFormat");
