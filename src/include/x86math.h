// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Public ABI of x86math.dll -- the DACOM 3DMathEngine component. Everything that does
// vector/matrix/quaternion work through the engine calls these methods; the interface is
// declared here and nowhere else (docs/re/cross_dll_protocol.md).

#include "dacom.h"
#include "math3d.h"

// Digital Anvil I3DMathEngine — vtable order from EXE/Disasm/x86math.dll (PTR_FUN_06f73090).
// Types are 3x3 row-major Matrix, Transform = Matrix + translation, Quaternion = (W,X,Y,Z).

// GR_INVALID_PARMS / GR_INTERFACE_UNSUPPORTED are DACOM-wide result codes (dacom.h), not part
// of this interface.

struct FMathDesc
{
    unsigned long Size;
    const char* InterfaceName;
    const char* Implementation;
};

static_assert(sizeof(FMathDesc) == 0xC, "retail AGGDESC / DA3DMATHDESC size 0xC");

// Retail vtable slots 0–25. Overload grouping does not match reconstructed Liberty 3dMath.h.
struct I3DMathEngine : IDAComponent
{
    virtual GENRESULT DACOM_API Inverse(Matrix& Dst, const Matrix& Source) = 0;                 // +0x0C
    virtual GENRESULT DACOM_API Scale(Matrix& Dst, const Matrix& Source, float Scale) = 0;     // +0x10
    virtual float DACOM_API Det(const Matrix& Source) = 0;                                      // +0x14
    virtual GENRESULT DACOM_API MulQuat(Quaternion& Dst, const Quaternion& A,
                                        const Quaternion& B) = 0;                               // +0x18
    virtual GENRESULT DACOM_API MulTransform(Transform& Dst, const Transform& A,
                                             const Transform& B) = 0;                           // +0x1C
    virtual GENRESULT DACOM_API MulMatrix(Matrix& Dst, const Matrix& A, const Matrix& B) = 0; // +0x20
    virtual GENRESULT DACOM_API TransformQuat(Vector& Dst, const Quaternion& Quat,
                                              const Vector& Source) = 0;                        // +0x24
    virtual GENRESULT DACOM_API TransformXform(Vector& Dst, const Transform& Xform,
                                               const Vector& Source) = 0;                       // +0x28
    virtual GENRESULT DACOM_API TransformMatrix(Vector& Dst, const Matrix& Mat,
                                                const Vector& Source) = 0;                      // +0x2C
    virtual GENRESULT DACOM_API Rotate(Vector& Dst, const Transform& Xform,
                                       const Vector& Source) = 0;                               // +0x30
    virtual GENRESULT DACOM_API InverseRotate(Vector& Dst, const Transform& Xform,
                                              const Vector& Source) = 0;                        // +0x34
    virtual GENRESULT DACOM_API TransposeTransform(Vector& Dst, const Matrix& Mat,
                                                   const Vector& Source) = 0;                   // +0x38
    virtual GENRESULT DACOM_API InverseTransform(Vector& Dst, const Transform& Xform,
                                                 const Vector& Source) = 0;                     // +0x3C
    virtual GENRESULT DACOM_API TransformListXform(Vector* Dst, const Transform& Xform,
                                                   const Vector* Source, int Count) = 0;        // +0x40
    virtual GENRESULT DACOM_API TransformListMatrix(Vector* Dst, const Matrix& Mat,
                                                    const Vector* Source, int Count) = 0;       // +0x44
    virtual GENRESULT DACOM_API TransposeTransformList(Vector* Dst, const Matrix& Mat,
                                                       const Vector* Source, int Count) = 0;    // +0x48
    virtual GENRESULT DACOM_API InverseTransformList(Vector* Dst, const Transform& Xform,
                                                     const Vector* Source, int Count) = 0;      // +0x4C
    virtual GENRESULT DACOM_API MatrixToQuaternion(Quaternion& Dst, const Matrix& Source) = 0; // +0x50
    virtual GENRESULT DACOM_API QuaternionToMatrix(Matrix& Dst, const Quaternion& Quat) = 0;   // +0x54
    virtual GENRESULT DACOM_API QuatSlerp(Quaternion& Dst, const Quaternion& A,
                                          const Quaternion& B, float T) = 0;                    // +0x58
    virtual GENRESULT DACOM_API GeneralInverse(Transform& Dst, float& W,
                                               const Transform& Source) = 0;                    // +0x5C
    virtual GENRESULT DACOM_API InvSqrt(float& Dst, float X) = 0;                                // +0x60
    virtual GENRESULT DACOM_API Sqrt(float& Dst, float X) = 0;                                   // +0x64

    // Modern port-only raw-F32 transport; original retail slots 0..25 are unchanged.
    // The input word is consumed at the kernel FMUL, after the quaternion dot test.
    virtual GENRESULT DACOM_API QuatSlerpValue(Quaternion& Dst, const Quaternion& A,
                                              const Quaternion& B, const float* T) = 0;          // +0x68
};
