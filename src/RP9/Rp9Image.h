// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// Zero-SDK D3D9 image, surface, texture, and HRESULT helpers (replaces D3DX9 and DxErr).

#pragma once

#include "render/d3d8_min.h"

const char* Rp9HrToString(HRESULT Hr);

HRESULT Rp9LoadSurfaceFromMemory(IDirect3DSurface9* DstSurface,
                                 const PALETTEENTRY* DstPalette,
                                 const RECT* DstRect,
                                 LPCVOID SrcMemory,
                                 D3DFORMAT SrcFormat,
                                 UINT SrcPitch,
                                 const PALETTEENTRY* SrcPalette,
                                 const RECT* SrcRect,
                                 DWORD Filter,
                                 D3DCOLOR ColorKey);

HRESULT Rp9LoadSurfaceFromSurface(IDirect3DSurface9* DstSurface,
                                  const PALETTEENTRY* DstPalette,
                                  const RECT* DstRect,
                                  IDirect3DSurface9* SrcSurface,
                                  const PALETTEENTRY* SrcPalette,
                                  const RECT* SrcRect,
                                  DWORD Filter,
                                  D3DCOLOR ColorKey);

HRESULT Rp9GetImageInfoFromMemory(LPCVOID SrcData, UINT SrcDataSize, _D3DXIMAGE_INFO* Info);

HRESULT Rp9LoadSurfaceFromFileInMemory(IDirect3DSurface9* DstSurface,
                                       const PALETTEENTRY* DstPalette,
                                       const RECT* DstRect,
                                       LPCVOID SrcData,
                                       UINT SrcDataSize,
                                       const RECT* SrcRect,
                                       DWORD Filter,
                                       D3DCOLOR ColorKey,
                                       _D3DXIMAGE_INFO* SrcInfo);

HRESULT Rp9CreateTextureFromFileInMemory(IDirect3DDevice9* Device,
                                         LPCVOID SrcData,
                                         UINT SrcDataSize,
                                         IDirect3DTexture9** OutTexture);

HRESULT Rp9CreateCubeTextureFromFileInMemory(IDirect3DDevice9* Device,
                                             LPCVOID SrcData,
                                             UINT SrcDataSize,
                                             IDirect3DCubeTexture9** OutCube);

HRESULT Rp9SaveSurfaceToFileA(LPCSTR DestFile,
                              D3DXIMAGE_FILEFORMAT DestFormat,
                              IDirect3DSurface9* SrcSurface,
                              const PALETTEENTRY* SrcPalette,
                              const RECT* SrcRect);
