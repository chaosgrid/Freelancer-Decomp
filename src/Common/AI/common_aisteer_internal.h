// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll-internal helpers shared by the AI steering TUs (common_obstacleavoidance.cpp,
// common_aiintercept.cpp, common_aifirecontrol.cpp, common_airaycast.cpp,
// common_aisteer.cpp, common_faceidea.cpp, common_dodgeidea.cpp,
// common_splinemoveidea.cpp). None of these is a retail export. The small ones are inline
// here, the rest are defined in common_aisteer.cpp. Consumers write
// `using namespace CmnAiSteer;` after their includes.

#include "common_ini.h"           // Vector
#include "common_csys.h"          // Matrix
#include "common_iface.h"         // IObjInspect
#include "common_systemmanager.h" // SystemManager

#include <cstring>

namespace CmnAiSteer
{
    // The fast-length idiom ObstacleAvoidance::Update inlines four times (e.g. 062de596):
    // the squared length LenSq stays on the x87 stack, only its float image feeds
    // the 0x5f3759df guess, and the one Newton step plus the final multiply run on the
    // unrounded value: ((1.5f - y*y*LenSq*0.5f) * y) * LenSq == ~sqrt(LenSq).
    inline double InlineFastLength(double LenSq)
    {
        float const LenSqImage = static_cast<float>(LenSq);
        int Bits;
        std::memcpy(&Bits, &LenSqImage, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Guess;
        std::memcpy(&Guess, &Bits, sizeof(Guess));
        double const Refined = (1.5f - static_cast<double>(Guess) * Guess * LenSq * 0.5f) * Guess;
        return Refined * LenSq;
    }

    inline Vector Sub(Vector const& A, Vector const& B) { return Vector{ A.x - B.x, A.y - B.y, A.z - B.z }; }
    inline Vector Add(Vector const& A, Vector const& B) { return Vector{ A.x + B.x, A.y + B.y, A.z + B.z }; }
    inline Vector Scale(Vector const& A, float S) { return Vector{ A.x * S, A.y * S, A.z * S }; }
    inline float LenSq(Vector const& A) { return A.x * A.x + A.y * A.y + A.z * A.z; }
    inline float Clamp11(float V) { return V < -1.0f ? -1.0f : (V > 1.0f ? 1.0f : V); }

    // Float-precision copy of VecDot3 (FUN_062c47a0), which has internal linkage in
    // common_behavior.cpp.
    inline float Dot3(Vector const& A, Vector const& B) { return A.x * B.x + A.y * B.y + A.z * B.z; }

    // FUN_062c47a0 as retail computes it: (A.z*B.z + A.y*B.y) + A.x*B.x on the x87
    // stack, returned in ST0 unrounded.
    inline double DotZyx(Vector const& A, Vector const& B)
    {
        return ((double)A.z * B.z + (double)A.y * B.y) + (double)A.x * B.x;
    }

    // [-1, 1] clamp of an unrounded x87 cosine, the shape the heading-cone predicates
    // share; NaN survives both tests.
    inline double ClampCos11(double V)
    {
        if (V > 1.0f)
            return 1.0f;
        if (V < -1.0f)
            return -1.0f;
        return V;
    }

    // FaceIdea::Update / DodgeIdea::Update plumbing.
    void RefreshCachedIfacePosition(SystemManager* Sys);
    Vector CachedIfacePosition(SystemManager* Sys);
    Matrix const* IfaceOrientation(SystemManager* Sys);

    // FUN_062e3730, cdecl (Out, Reference, RelDir, UpHint).
    void SteeringFrameFromRelativeDir(Matrix& Out, Matrix const& Reference, Vector const& RelDir, Vector const& UpHint);

    // The owner object PathfindManager's ctor and ObstacleAvoidance::AttachOwner query: the
    // watched object behind IBehaviorManager+0xF0 (Watchable - 8), 0 when nothing is
    // watched. Retail re-reads it before every call and dereferences it without a further
    // test.
    inline IObjRW* PathfindOwnerObject(IBehaviorManager const* Owner)
    {
        return ObjRwFromWatchable(Owner->WatchablePtr);
    }
}
