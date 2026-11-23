// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// A header of its own so common_universe.h can hold the zone population (IZone::Population)
// without including common_goods.h, which includes common_net.h, which includes
// common_universe.h. common_goods.h includes this header.

#include "common_exports.h"

#include <cstdlib>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// weighted_vector<T> -- retail Common.dll template, instantiated and exported only for
// T = unsigned int (nine exports). Retail layout is a VC6 std::vector<std::pair<T, float>>;
// the port keeps a modern one. A class in retail (mangled V). The explicit instantiation
// below exports exactly retail's members: three ctors, the dtor, both operator= (returning
// `const&`), GetSumOfWeights and both Pick overloads (one ICF body in retail).
template <typename T>
class weighted_vector
{
public:
    // 0x06266310: empty vector.
    weighted_vector() {}

    // 0x06261100: a buffer of exactly Other's size, every pair copied.
    weighted_vector(weighted_vector const& Other) : Items(Other.Items) {}

    // 0x06266330: reserve(Ids.size()), then every id appended in order with weight 1.0.
    explicit weighted_vector(std::vector<T> const& Ids)
    {
        Items.reserve(Ids.size());
        for (unsigned Index = 0; Index < Ids.size(); ++Index)
            Items.push_back(std::pair<T, float>(Ids[Index], 1.0f));
    }

    // 0x06269110 (ICF-shared with the other VC6 vector dtors).
    ~weighted_vector() {}

    // 0x062665c0. Not std::vector's operator=: erase everything, reserve(Other.size()), then
    // append every pair of Other. Retail quirk kept: no self-assignment test, so `v = v`
    // empties v.
    weighted_vector const& operator=(weighted_vector const& Other)
    {
        Items.clear();
        Items.reserve(Other.Items.size());
        for (unsigned Index = 0; Index < Other.Items.size(); ++Index)
            Items.push_back(Other.Items[Index]);
        return *this;
    }

    // 0x06266490: erase everything, reserve(Ids.size()), append every id with weight 1.0.
    weighted_vector const& operator=(std::vector<T> const& Ids)
    {
        Items.clear();
        Items.reserve(Ids.size());
        for (unsigned Index = 0; Index < Ids.size(); ++Index)
            Items.push_back(std::pair<T, float>(Ids[Index], 1.0f));
        return *this;
    }

    // 0x062666e0. The sum is never stored and is returned unrounded in ST0, hence double
    // (no caller in any module).
    double GetSumOfWeights() const
    {
        double Sum = 0.0;
        for (unsigned Index = 0; Index < Items.size(); ++Index)
            Sum += Items[Index].second;
        return Sum;
    }

    // 0x06266700 (and the non-const overload, same body). Weighted random choice:
    //   - Sum = 0.0 + every weight; empty or Sum == 0.0 -> end();
    //   - Target = rand() * 2^-15 * Sum (.rdata:063997d0);
    //   - walk again and return the first pair whose running total satisfies
    //     Target <= Acc; none -> end().
    // x87: retail keeps both running sums on the FPU stack and stores only the total and
    // Target as floats, so each FADD is rounded by the thread's precision control -- 24-bit
    // in the single-player client (Server/Content run on the main thread after Direct3D 8
    // CreateDevice without FPU_PRESERVE), 53-bit in FLServer.exe (_control87 at 0x0040bb17).
    // The port is x87 (/arch:IA32), so a double running sum held in a register reproduces
    // both; a float variable would force 24-bit everywhere. The zero test reads the
    // unrounded sum.
    std::pair<T, float> const* Pick() const
    {
        std::pair<T, float> const* End = Items.data() + Items.size();
        if (Items.empty())
            return End;
        double Sum = 0.0;
        for (unsigned Index = 0; Index < Items.size(); ++Index)
            Sum += Items[Index].second;
        float const Total = (float)Sum;
        if (Sum == 0.0)
            return End;
        float const Target = (float)((double)rand() * (1.0f / 32768.0f) * Total);
        double Acc = 0.0;
        for (unsigned Index = 0; Index < Items.size(); ++Index)
        {
            Acc += Items[Index].second;
            if (Target <= Acc)
                return &Items[Index];
        }
        return End;
    }

    std::pair<T, float>* Pick()
    {
        weighted_vector const& ConstThis = *this;
        return const_cast<std::pair<T, float>*>(ConstThis.Pick());
    }

    std::vector<std::pair<T, float>> Items;
};

// Retail Common.dll exports the unsigned int instantiation. The explicit instantiation
// definition is in common_equipdesc.cpp; importing modules take it from Common.dll.
#ifndef COMMON_EXPORTS
extern template class COMMON_API weighted_vector<unsigned int>;
#endif
