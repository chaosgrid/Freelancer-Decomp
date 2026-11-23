// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "render/IBigImage2.h"

// Port-only 2D UI draw helpers (pipeline slot 69 / 70), FVF 0x1C4 (stride 32). The big-image
// tile callback FUN_00404720 is ScreenQuadDrawBigImageTile in screen_quad_state.h.

void MenuSetup2DRenderState();
void MenuRestore3DRenderState();
void MenuSetupAdditive2DRenderState();

void MenuDrawSolidRect(int Left, int Top, int Right, int Bottom, unsigned long PackedArgb);

void MenuDrawTexturedRect(int Left, int Top, int Right, int Bottom, void* TextureHandle,
                          unsigned long PackedArgb, float U0, float V0, float U1, float V1);

void MenuDrawAdditiveTexturedRect(int Left, int Top, int Right, int Bottom, void* TextureHandle,
                                  unsigned long PackedArgb, float U0, float V0, float U1, float V1);

// front_button-style bezel: indexed XYZRHW rings + a 128x128 body + the caption; hover uses an
// additive outer ring.
void MenuDrawNeuroNetButtonOverlay(int Left, int Top, int Right, int Bottom, void* BodyHandle,
                                   float BodyU0, float BodyV0, float BodyU1, float BodyV1,
                                   unsigned long BodyTint, void* LabelHandle, int LabelLeft,
                                   int LabelTop, int LabelRight, int LabelBottom,
                                   unsigned long FrameArgb, unsigned long LabelArgb, float LabelU1,
                                   float LabelV1);

void MenuDrawNeuroNetCursor(int X, int Y);
