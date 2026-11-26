// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll Csys — listing RVA 0x1170 / 0xa7490.
// Layout: Vector @ +0, Matrix @ +0xC, sizeof 0x30. Transform is Matrix then Vector.
// trans_from_frame / trans_to_frame / get_inverse / operator* go through I3DMathEngine
// (Acquire3DMathEngine), as retail does.

#include "common_ini.h"

class COMMON_API Csys
{
public:
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    Csys& operator=(Csys const&) = default;

    // Retail ??0Csys@@QAE@XZ is empty (no identity). Call set_identity / Csys_identity.
    Csys();

    void set_identity();
    void init(Vector const& NewPos, Matrix const& NewOrient);
    Transform get_transform() const;
    void set_transform(Transform const& Xform);

    Vector trans_from_frame(Vector const& Local) const;
    Matrix trans_from_frame(Matrix const& Local) const;
    Csys trans_from_frame(Csys const& Local) const;

    Vector trans_to_frame(Vector const& World) const;
    Matrix trans_to_frame(Matrix const& World) const;
    Csys trans_to_frame(Csys const& World) const;

    Csys get_inverse() const;

    Vector Pos;     // +0x00
    Matrix Orient;  // +0x0C
};

static_assert(sizeof(Vector) == 0x0C, "Vector 3 float");
static_assert(sizeof(Matrix) == 0x24, "Matrix 3x3");
static_assert(sizeof(Transform) == 0x30, "Transform matrix+pos");
static_assert(sizeof(Csys) == 0x30, "Csys pos+matrix");

COMMON_API Csys __cdecl Csys_identity();
COMMON_API Csys __cdecl Csys_init(Vector const& Pos, Matrix const& Orient);
COMMON_API Csys __cdecl operator*(Csys const& Left, Csys const& Right);

// Port helpers, not retail exports. MatrixSetIdentity is header-inline (every module that
// needs an identity 3x3 compiles its own, as retail's inline Matrix code did);
// CsysFromPosOrient is Common.dll-internal (common_roomdata.cpp only).
inline void MatrixSetIdentity(Matrix& Out)
{
    unsigned Row = 0;
    for (; Row < 3; ++Row)
    {
        unsigned Col = 0;
        for (; Col < 3; ++Col)
            Out.E[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
    }
}
void CsysFromPosOrient(Csys& Out, Vector const& Pos, Matrix const& Orient);

// 0x063038b0. Orthonormal 3x3: column 2 = -normalize(Dir).
COMMON_API Matrix __cdecl LookMatrix(Vector const& Direction);

// 0x06303a60, ?LookMatrixYup@@YA?AVMatrix@@ABVVector@@@Z. The Y-up sibling of
// LookMatrix: column 2 is -normalize(Direction), column 1 is the perpendicular that keeps Y
// up, column 0 closes the right-handed basis. Retail picks its reference axis from X and Z
// (LookMatrix uses X and Y) and compares the SQUARED length against 1e-4 before the sqrt.
COMMON_API Matrix __cdecl LookMatrixYup(Vector const& Direction);

// 0x06307900, ?DALib_Engine_get_csys@@YA?AVCsys@@J@Z. The engine instance's
// transform (slot 40, +0xa0) repackaged as a Csys. Retail dereferences the returned
// transform without a null check.
COMMON_API Csys __cdecl DALib_Engine_get_csys(long Handle);

// 0x06303c10, ?GetDirectionMatrix@@YA?AVMatrix@@ABVTransform@@ABVVector@@@Z.
// Orthonormal basis whose column 2 points from Target back to the transform. Column 1 is
// normally the transform's own column 1 orthogonalised against that direction; when the
// two are (exactly) parallel retail falls back to column 0 -- see docs/re/deviations.md
// for the abs()-on-a-float that decides which. Returns the transform's rotation unchanged
// when the two positions coincide.
COMMON_API Matrix __cdecl GetDirectionMatrix(Transform const& Xform, Vector const& Target);

// The MatrixColumn0/1/2 accessors and MatrixSetColumn2 are unexported Common.dll-internal
// helpers (retail thiscall on the Matrix with a hidden return pointer). They read or write
// a raw column with no sign flip -- unlike the many inlined "Forward = -E[*][2]" sites.
Vector __cdecl MatrixColumn2(Matrix const& M);  // FUN_062d3af0, ~12 callers
Vector __cdecl MatrixColumn0(Matrix const& M);  // FUN_062e0480
Vector __cdecl MatrixColumn1(Matrix const& M);  // FUN_062e04a0
// FUN_062e3710 (RET 4): writes Src into E[0][2]/E[1][2]/E[2][2]. Caller
// BuildPerpendicularFrame (FUN_062e3460).
void __cdecl MatrixSetColumn2(Matrix& M, Vector const& Src);

// 0x06303fe0, ?RotateMatrix@@YA?AVMatrix@@ABVVector@@@Z. Euler angles in
// DEGREES, but composed in a different order from EulerMatrix: Ry * Rx * Rz, and each
// axis whose angle is within 0.01 degrees of zero is skipped entirely (its multiply does
// not run at all, so no rounding creeps in).
COMMON_API Matrix __cdecl RotateMatrix(Vector const& AnglesDegrees);

// 0x06304230, ?EulerMatrix@@YA?AVMatrix@@ABVVector@@@Z. Euler angles in DEGREES
// (pitch = x, yaw = y, roll = z), converted with 0.017453292f.
COMMON_API Matrix __cdecl EulerMatrix(Vector const& AnglesDegrees);


COMMON_API float __cdecl Csys_error(Csys const& A, Csys const& B);
// Retail takes both frames by value: ?Csys_interpolate@@YA?AVCsys@@V1@0M@Z.
COMMON_API Csys __cdecl Csys_interpolate(Csys A, Csys B, float T);
COMMON_API void __cdecl DALib_Engine_set_csys(long Handle, Csys const& InCsys);

// Returns x86math.dll's 3DMathEngine singleton (DACOM CreateInstance by component name).
// The interface is in x86math.h, forward-declared here.
//
// Not exported: retail Common.dll's copy is the unexported FUN_06391cf0, and freelancer.exe
// carries its own (FUN_005b73e0, defined in src/freelancer/Client/engine_init.cpp). This
// declaration is shared; each module that calls it defines it.
struct I3DMathEngine;
I3DMathEngine* __cdecl Acquire3DMathEngine();
