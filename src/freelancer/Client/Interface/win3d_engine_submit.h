// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "engine/engine_instance.h" // InstanceIndex / ArchetypeIndex
#include "Common/Graphics/client_mesh_load.h"
#include "Space/Cameras/space_camera.h"

// Win3D Engine-instance submit for UI widgets (ButtonControl / StaticControl mesh branch), the
// cursor unproject and the WinControl ray-vs-box hit test.

// DAT_0067dbf8 — the Win3D scene camera: the submit's arch anchor and the UI unproject camera.
extern FSpaceCamera G_Win3DSceneCamera;

// DAT_0067dd10 -- the lazily acquired 3DMathEngine of the plain submit and WinButton::Update.
struct I3DMathEngine;
extern I3DMathEngine* G_Win3DObjectMath;

// FUN_0059da60
FSpaceCamera* GetWin3DSceneCamera();

// FUN_00434310 (cdecl, 7 args) — tinted submit (RGBA bytes). MaterialSelector goes to
// IFLAppearance::GetOverrideMaterial; bDepthTested picks the depth func / z-write states as
// above, restored afterwards. Both callers pass 1 for the selector.
// Nothing unless game option 0xb is on.
void SubmitWin3DTintedEngineInstance(void* ArchAnchor, InstanceIndex EngineInstance,
                                     unsigned char MaterialSelector, unsigned long Flags,
                                     unsigned char const ColorRgba[4],
                                     unsigned char bDepthTested, float Scale);

// FUN_00434630 — wraps tinted submit: Flags |= 0x8200; if !bFrontCull Flags |= 0x2000.
void SubmitWin3DTintedEngineInstanceWrapped(void* ArchAnchor, InstanceIndex EngineInstance,
                                            unsigned char bFrontCull,
                                            unsigned char const ColorRgba[4],
                                            unsigned char bDepthTested, float Scale,
                                            unsigned long Flags);

// FUN_00433f40 (cdecl, 8 args) -- the material-parameter submit: nothing for instance -1
// (returns 1); else the depth states from bDepthTested, material 0
// given parameter 0 = ColorRgba / 255 and parameter 0xa = bParam10, then the render with
// Flags | 0x100 (| 0x2000 without bFrontCull, | 0x30 with a scaled identity when Scale != 1).
// Returns the renderer's answer. Callers: SubmitWin3DOutlinedEngineInstance and 0x00548411
// (not ported).
int SubmitWin3DMaterialEngineInstance(void* Camera, InstanceIndex EngineInstance, unsigned char bFrontCull,
                                      unsigned char const ColorRgba[4],
                                      unsigned char bDepthTested, float Scale,
                                      unsigned char bParam10, unsigned long Flags);

// FUN_00434670 (cdecl, 6 args) -- the outlined wire: the material submit in black with
// bDepthTested 0 and parameter 0xa off, then the tinted submit with ColorRgba and
// Flags | 0x8100 (| 0x2000 without bFrontCull). Caller: StaticControl::Draw.
void SubmitWin3DOutlinedEngineInstance(void* Camera, InstanceIndex EngineInstance,
                                       unsigned char bFrontCull, unsigned char const ColorRgba[4],
                                       float Scale, unsigned long Flags);

// LAB_0055b660 / LAB_0040f520 — screen pixel to a normalized world-space direction.
void Win3DUnprojectScreenRayDir(FSpaceCamera* Cam, float ScreenX, float ScreenY, float OutDir[3]);

// FUN_005a1130 (thiscall on the half extents, RET 8) — three slab tests, strict < half.
bool Win3DRayHitsAabbHalfExtents(Vector const* HalfExtents, Transform const* Xform,
                                 Vector const* RayDir);

