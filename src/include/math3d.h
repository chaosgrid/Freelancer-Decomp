// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The 3D math types every module shares: the 3DMathEngine interface (x86math.h), the engine and
// render interfaces and Common's exported classes are all spelled in terms of them. Retail's
// decorated names carry the class names (?get_transform@Csys@@QBE?AVTransform@@XZ,
// ...ABVVector@@..., ...ABVMatrix@@...). Matrix is 3x3 row-major, Transform is Matrix then
// translation, Quaternion is (w, x, y, z).

class Vector
{
public:
    float x;
    float y;
    float z;
};

class Matrix
{
public:
    // Leaves the nine floats alone, as retail's does. Defaulted rather than written out, so
    // that a struct holding a Matrix is still zeroed as a whole by `= {}`.
    Matrix() = default;

    // Not exported by Common.dll, Server.dll or freelancer.exe: every module compiles its own
    // copy of this header body (Common .text:06284f70, freelancer.exe .text:00447b60 -- the
    // same 20 instructions, arguments stored row-major to +0x00..+0x20, RET 0x24).
    Matrix(float m00, float m01, float m02,
           float m10, float m11, float m12,
           float m20, float m21, float m22)
    {
        E[0][0] = m00;
        E[0][1] = m01;
        E[0][2] = m02;
        E[1][0] = m10;
        E[1][1] = m11;
        E[1][2] = m12;
        E[2][0] = m20;
        E[2][1] = m21;
        E[2][2] = m22;
    }

    float E[3][3];
};

class Transform
{
public:
    Matrix Rotation;    // +0x00
    Vector Translation; // +0x24
};

// Four floats (w, x, y, z).
class Quaternion
{
public:
    float w;
    float x;
    float y;
    float z;
};

static_assert(sizeof(Quaternion) == 0x10, "Quaternion 4 floats");

static_assert(sizeof(Vector) == 0xC, "Vector is three floats");
static_assert(sizeof(Matrix) == 0x24, "Matrix is nine floats");
static_assert(sizeof(Transform) == 0x30, "Transform is Matrix + Vector");
