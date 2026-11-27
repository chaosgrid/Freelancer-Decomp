// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Graphics/graph_library.h"

#include "common_ini.h"
#include "dacom.h"
#include "Core/globals.h"

#include <windows.h>

#include <list>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    char const* const kGraphLibraryCpp = "E:\\FL\\Scratch\\Source\\Client\\cmn\\GraphLibrary.cpp";

    // DAT_005c75f8 (double 0.001): how far below Max a clamped parameter stops.
    double const kGraphClampMargin = 0.001;

    // DAT_0061065c, initialised to 1 and only ever read: non-zero interpolates between the
    // bracketing keys, zero snaps to the nearer one.
    unsigned char G_bGraphInterpolate = 1;

    // DAT_0061669c / DAT_006166a0 / DAT_006166a4. Constructed by LAB_00419050 and destroyed by
    // LAB_00419090 in retail -- the static constructor/destructor of this object.
    std::list<FGraphData> G_GraphLibrary;

    // The bracketing-key search shared by slots 6 and 7. Every compare's unordered outcome
    // follows retail: a NaN X searches, and moves Lo up.
    void FindBracketingKeys(FGraphPoint const* Keys, unsigned Count, double X, int& OutLo,
                            int& OutHi)
    {
        int Hi = static_cast<int>(Count) - 1;
        int Lo = 0;
        if (X <= Keys[0].X)
        {
            Hi = 0;
        }
        else if (X >= Keys[Hi].X)
        {
            Lo = Hi;
        }
        else if (Hi > 1)
        {
            do
            {
                int const Mid = (Hi + Lo) / 2;
                if (!(X < Keys[Mid].X))
                    Lo = Mid;
                else
                    Hi = Mid;
            } while (Hi - Lo > 1);
        }

        if (G_bGraphInterpolate == 0)
        {
            // Not farther from Hi (or unordered) snaps both to Hi.
            if (!((Keys[Hi].X - X) > (X - Keys[Lo].X)))
                Lo = Hi;
            else
                Hi = Lo;
        }

        OutLo = Lo;
        OutHi = Hi;
    }
}

// FUN_00419460.
double FGraphRange::Wrap(float T) const
{
    if (bClamp != 0)
    {
        if (!(Min < T))
            T = Min;
        // Max - 0.001 stays on the FPU stack; unordered returns the limit.
        double const Limit = static_cast<double>(Max) - kGraphClampMargin;
        if (static_cast<double>(T) < Limit)
            return T;
        return Limit;
    }

    float const Span = Max - Min;
    double const Frac = (static_cast<double>(T) - Min) / Span;
    return fabs(Frac - floor(Frac)) * Span;
}

// FUN_004194e0.
double FGraphRange::Normalize(float T) const
{
    return (static_cast<double>(T) - Min) /
           (static_cast<double>(Max) - Min);
}

FGraphData::FGraphData()
    : Count(0), Data(nullptr), RefCount(0)
{
    Range.bClamp = 0;
    Range.Min = 0.0f;
    Range.Max = 0.0f;
}

FGraphData::FGraphData(FGraphData const& Other)
    : Name(Other.Name), ElemType(Other.ElemType), Count(Other.Count), Data(Other.Data),
      Range(Other.Range), RefCount(Other.RefCount)
{
}

FGraphData::~FGraphData()
{
    if (Data != nullptr)
    {
        free(Data);
        Data = nullptr;
    }
}

// LAB_004191d0.
char const* FGraphData::GetName()
{
    return Name.c_str();
}

// FUN_00415070 (ICF-folded).
int FGraphData::GetElemType()
{
    return ElemType;
}

// LAB_004191e0.
unsigned FGraphData::GetCount()
{
    return Count;
}

// LAB_00431680 (ICF-folded).
FGraphRange* FGraphData::GetRange()
{
    return &Range;
}

// LAB_004191f0, RET 4.
FGraphDesc* FGraphData::GetDesc(FGraphDesc* Out)
{
    Out->ElemType = ElemType;
    Out->Count = Count;
    Out->Data = Data;
    return Out;
}

// FUN_00419210, RET 4.
void FGraphData::SetData(FGraphDesc const* Desc)
{
    ElemType = Desc->ElemType;
    bool const bSameBuffer = Desc->Data == Data && Desc->Count <= Count;

    unsigned const NewCount = Desc->Count;
    if (NewCount != Count)
    {
        if (NewCount != 0)
        {
            // Retail also tests (Data != 0 && NewCount == Count) here; that cannot hold inside
            // this branch, so the realloc always runs.
            Data = static_cast<FGraphPoint*>(realloc(Data, NewCount * sizeof(FGraphPoint)));
            Count = NewCount;
        }
        else if (Data != nullptr)
        {
            // Count is left as it was.
            free(Data);
            Data = nullptr;
        }
    }

    if (!bSameBuffer)
        memcpy(Data, Desc->Data, Desc->Count * sizeof(FGraphPoint));

    // X extent of the keys, reading Count (not Desc->Count) keys; unordered takes X.
    Range.Max = Data[0].X;
    Range.Min = Data[0].X;
    for (unsigned Index = 1; Index < Count; ++Index)
    {
        float const X = Data[Index].X;
        Range.Min = (Range.Min < X) ? Range.Min : X;
        Range.Max = (Range.Max > X) ? Range.Max : X;
    }
}

// LAB_004192f0, RET 8.
double FGraphData::Evaluate(float T, FGraphRange const* Caller)
{
    double X;
    if (Caller != nullptr)
    {
        // FUN_00419460 inlined with the result stored as a float, then mapped from the caller's
        // range onto this curve's X extent.
        float const Wrapped = static_cast<float>(Caller->Wrap(T));
        X = Caller->Normalize(Wrapped) *
                (static_cast<double>(Range.Max) - Range.Min) +
            Range.Min;
    }
    else
    {
        X = Range.Wrap(T);
    }

    int Lo;
    int Hi;
    FindBracketingKeys(Data, Count, X, Lo, Hi);
    FGraphPoint const& LoKey = Data[Lo];
    FGraphPoint const& HiKey = Data[Hi];

    double const Span = static_cast<double>(HiKey.X) - LoKey.X;
    double Weight;
    // A non-positive (or unordered) span weights the low key 1.
    if (Span > 0.0)
        Weight = (HiKey.X - X) / Span;
    else
        Weight = 1.0;
    return (1.0 - Weight) * HiKey.Value + LoKey.Value * Weight;
}

// LAB_00419500, RET 8.
unsigned FGraphData::EvaluateColor(float T, FGraphRange const* Caller)
{
    double X;
    if (Caller != nullptr)
    {
        float const Wrapped = static_cast<float>(Caller->Wrap(T));
        X = Caller->Normalize(Wrapped) *
                (static_cast<double>(Range.Max) - Range.Min) +
            Range.Min;
    }
    else
    {
        X = Range.Wrap(T);
    }

    int Lo;
    int Hi;
    FindBracketingKeys(Data, Count, X, Lo, Hi);
    FGraphPoint const& LoKey = Data[Lo];
    FGraphPoint const& HiKey = Data[Hi];

    double const Span = static_cast<double>(HiKey.X) - LoKey.X;
    double Weight;
    if (Span > 0.0)
    {
        // Retail bug kept: the weight here is measured from the LOW key, the opposite of slot
        // 6's, while the blend below still puts (1 - Weight) on the high key. Retail's colour
        // curves therefore run backwards between keys.
        Weight = (X - LoKey.X) / Span;
    }
    else
    {
        Weight = 1.0;
    }
    double const HiWeight = 1.0 - Weight;

    unsigned const HiBits = HiKey.Bits;
    unsigned const LoBits = LoKey.Bits;
    // Each channel is read unsigned, weighted, summed, then _ftol.
    int const Red = static_cast<int>(((HiBits >> 0x10) & 0xffu) * HiWeight +
                                     ((LoBits >> 0x10) & 0xffu) * Weight);
    int const Alpha = static_cast<int>((HiBits >> 0x18) * HiWeight + (LoBits >> 0x18) * Weight);
    int const Green = static_cast<int>(((HiBits >> 8) & 0xffu) * HiWeight +
                                       ((LoBits >> 8) & 0xffu) * Weight);
    int const Blue = static_cast<int>((HiBits & 0xffu) * HiWeight + (LoBits & 0xffu) * Weight);

    // The alpha result is shifted in without the 0xFF mask the others get.
    unsigned Packed = static_cast<unsigned>(Red) & 0xffu;
    Packed = (Packed | (static_cast<unsigned>(Alpha) << 8)) << 8;
    Packed = (Packed | (static_cast<unsigned>(Green) & 0xffu)) << 8;
    return (static_cast<unsigned>(Blue) & 0xffu) | Packed;
}

// LAB_00419160.
int FGraphData::AddRef()
{
    return ++RefCount;
}

// LAB_00419170.
int FGraphData::Release()
{
    int const NewCount = --RefCount;
    if (NewCount != 0)
        return NewCount;

    // Walk the library for the node holding `this`; erase runs FUN_00419100 and frees the node.
    for (std::list<FGraphData>::iterator It = G_GraphLibrary.begin(); It != G_GraphLibrary.end();
         ++It)
    {
        if (&*It == this)
        {
            G_GraphLibrary.erase(It);
            return 0;
        }
    }
    return 0;
}

bool FGraphData::SetName(char const* NewName)
{
    if (RefCount != 0 || Data != nullptr)
        return false;
    Name.assign(NewName);
    AddRef(); // virtual in retail
    return true;
}

IGraph* GraphLibraryFind(char const* Name)
{
    for (std::list<FGraphData>::iterator It = G_GraphLibrary.begin(); It != G_GraphLibrary.end();
         ++It)
    {
        if (_stricmp(It->GetName(), Name) == 0)
        {
            IGraph* const Graph = &*It;
            Graph->AddRef();
            return Graph;
        }
    }

    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: GraphLib - failed to find '%s'\n", kGraphLibraryCpp,
          0x1b3, Name);
    return nullptr;
}

int GraphLibraryLoad(char const* Path)
{
    int Created = 0;
    INI_Reader Reader;
    if (!Reader.open(Path, false))
    {
        MessageBoxA(nullptr, Path, "Error: unable to open IGraph file", 0);
        return Created;
    }

    // The {len, char[0x80]} name buffer FUN_00417c50 fills, and the 0x2000-byte key array,
    // which retail fills without a bound.
    unsigned NameLen = 0;
    char Name[0x80];
    Name[0] = '\0';
    FGraphPoint Points[0x400];

    if (Reader.read_header())
    {
        do
        {
            if (!Reader.is_header("IGraph"))
                continue;

            NameLen = 0;
            Name[0] = '\0';
            if (!Reader.read_value())
                continue;

            unsigned PointCount = 0;
            do
            {
                if (Reader.is_value("nickname"))
                {
                    char const* const Value = Reader.get_value_string();
                    if (Value == nullptr)
                    {
                        NameLen = 0;
                        Name[0] = '\0';
                    }
                    else
                    {
                        NameLen = CopyBoundedString(Name, 0x80, Value);
                    }
                    continue;
                }

                if (!Reader.is_value("type"))
                    continue;

                // "type" starts the key list: every value after it in the section is read as a
                // key, so a nickname that follows it reports "unknown IGraph parameter".
                char const* const TypeName = Reader.get_value_string();
                int Type;
                if (_stricmp(TypeName, "FLOAT") == 0)
                {
                    Type = GraphElem_Float;
                }
                else if (_stricmp(TypeName, "COLOR") == 0)
                {
                    Type = GraphElem_Color;
                }
                else if (_stricmp(TypeName, "INTEGER") == 0)
                {
                    Type = GraphElem_Integer;
                }
                else
                {
                    MessageBoxA(nullptr, Reader.get_value_string(), "Error: invalid IGraph::ElemType",
                                0);
                    PointCount = 0;
                    continue;
                }

                if (Reader.read_value())
                {
                    do
                    {
                        if (Reader.is_value("point"))
                        {
                            Points[PointCount].X = Reader.get_value_float(0);
                            if (Type == GraphElem_Float)
                            {
                                Points[PointCount].Value = Reader.get_value_float(1);
                            }
                            else if (Type == GraphElem_Integer || Type == GraphElem_Color)
                            {
                                Points[PointCount].Bits = static_cast<unsigned>(
                                    static_cast<int>(Reader.get_value_float(1)));
                            }
                            ++PointCount;
                        }
                        else
                        {
                            MessageBoxA(nullptr, Reader.get_line_ptr(),
                                        "Error: unknown IGraph parameter", 0);
                        }
                    } while (Reader.read_value());
                }

                bool bDuplicate = false;
                for (std::list<FGraphData>::iterator It = G_GraphLibrary.begin();
                     It != G_GraphLibrary.end(); ++It)
                {
                    if (_stricmp(It->GetName(), Name) == 0)
                    {
                        bDuplicate = true;
                        break;
                    }
                }

                if (!bDuplicate)
                {
                    {
                        FGraphData Temp;
                        G_GraphLibrary.push_back(Temp);
                    }
                    FGraphData* const Graph = &G_GraphLibrary.back();
                    if (Graph->SetName(Name))
                    {
                        FGraphDesc Desc;
                        Desc.ElemType = Type;
                        Desc.Count = PointCount;
                        Desc.Data = Points;
                        static_cast<IGraph*>(Graph)->SetData(&Desc);
                        ++Created;
                        PointCount = 0;
                        continue;
                    }
                    G_GraphLibrary.pop_back();
                }

                MessageBoxA(nullptr, Name, "Error: unable to create IGraph object (duplicate?)", 0);
                PointCount = 0;
            } while (Reader.read_value());
        } while (Reader.read_header());
    }

    (void)NameLen;
    return Created;
}

void GraphLibraryShutdown()
{
    // Release the head until it drops off the list (the library holds the last reference).
    while (!G_GraphLibrary.empty())
        G_GraphLibrary.front().Release();
    G_GraphLibrary.clear();
}
