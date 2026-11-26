// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ThornScene.h"
#include "readfile.h"
#include "ThornModernAdapters.h"
#include "ThornControlCalls.h"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <malloc.h>
#include <new>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>
#include <windows.h>

// Playback: slots 19/22/23/26 + Start types 1-19. docs/re/thorn.md.

namespace
{
    unsigned int G_NextEventId = 0; // DAT_06f61e24: module lifetime, wrapping counter.
    I3DMathEngine* G_SpatialWriterMathEngine = nullptr; // DAT_06f61df0: retained, no Release.
    I3DMathEngine* G_SceneEventMathEngine = nullptr; // DAT_06f61e20: retained, no Release.
    I3DMathEngine* G_SceneCsysMathEngine = nullptr; // DAT_06f61e1c: retained, no Release.

    I3DMathEngine* AcquireSceneCsysMathEngine()
    {
        if (G_SceneCsysMathEngine == nullptr)
            G_SceneCsysMathEngine = ThornAcquire3DMathEngine();
        return G_SceneCsysMathEngine;
    }

    I3DMathEngine* AcquireSceneEventMathEngine()
    {
        if (G_SceneEventMathEngine == nullptr)
            G_SceneEventMathEngine = ThornAcquire3DMathEngine();
        return G_SceneEventMathEngine;
    }
    // FUN_06f400b0: signed event-type predicate; either null means false.
    bool ActiveEventTypePrecedes(const FThornEvent* Left, const FThornEvent* Right)
    {
        return Left != nullptr && Right != nullptr && Left->Type < Right->Type;
    }

    // FUN_06f400e0: the sorted first sixteen entries provide the sentinel.
    void InsertActiveEventType(FThornEvent** Slot, FThornEvent* Event)
    {
        while (Event != nullptr)
        {
            FThornEvent* Previous = Slot[-1];
            if (Previous == nullptr || Event->Type >= Previous->Type)
                break;
            *Slot = Previous;
            --Slot;
        }
        *Slot = Event;
    }

    // FUN_06f40290: signed Type insertion sort, including the front shift.
    void SortActiveEventTypes(FThornEvent** Begin, FThornEvent** End)
    {
        if (Begin == End)
            return;
        for (FThornEvent** Slot = Begin + 1; Slot != End; ++Slot)
        {
            FThornEvent* Event = *Slot;
            if (Event != nullptr && *Begin != nullptr && Event->Type < (*Begin)->Type)
            {
                FThornEvent** Write = Slot;
                while (Write != Begin)
                {
                    *Write = Write[-1];
                    --Write;
                }
                *Begin = Event;
            }
            else
                InsertActiveEventType(Slot, Event);
        }
    }

    // FUN_06f3ff10: the already sorted prefix supplies the first sentinel.
    void InsertMasterEvent(FThornEvent** Slot, FThornEvent* Event)
    {
        while (Event != nullptr && Slot[-1] != nullptr &&
               static_cast<unsigned int>(Event->StartTimeMs) <
                   static_cast<unsigned int>(Slot[-1]->StartTimeMs))
        {
            *Slot = Slot[-1];
            --Slot;
        }
        *Slot = Event;
    }

    // FUN_06f40120: unsigned-time insertion sort, including the front shift.
    void SortRetailNodesByKey(FThornEvent** Nodes, int Count)
    {
        if (Count == 0)
            return;
        for (int Index = 1; Index < Count; ++Index)
        {
            FThornEvent* Event = Nodes[Index];
            FThornEvent** Slot = Nodes + Index;
            if (Event == nullptr || Nodes[0] == nullptr ||
                static_cast<unsigned int>(Nodes[0]->StartTimeMs) <=
                    static_cast<unsigned int>(Event->StartTimeMs))
            {
                InsertMasterEvent(Slot, Event);
            }
            else
            {
                for (; Slot != Nodes; --Slot)
                    *Slot = Slot[-1];
                *Nodes = Event;
            }
        }
    }

    // FUN_06f3fee0: AL-only unsigned event-time predicate.
    bool MasterEventPrecedes(const FThornEvent* Left, const FThornEvent* Right)
    {
        return Left != nullptr && Right != nullptr &&
               static_cast<unsigned int>(Left->StartTimeMs) <
                   static_cast<unsigned int>(Right->StartTimeMs);
    }

    // FUN_06f401b0: median of three by the unsigned +0x08 key. Branch order
    // and null fallbacks follow retail.
    FThornEvent* SelectRetailMedianNode(FThornEvent* First, FThornEvent* Second, FThornEvent* Third)
    {
        if (First != nullptr)
        {
            if (Second != nullptr)
            {
                const unsigned int FirstKey = static_cast<unsigned int>(First->StartTimeMs);
                const unsigned int SecondKey = static_cast<unsigned int>(Second->StartTimeMs);
                if (FirstKey < SecondKey)
                {
                    if (Third == nullptr)
                        return First;
                    const unsigned int ThirdKey = static_cast<unsigned int>(Third->StartTimeMs);
                    if (SecondKey < ThirdKey)
                        return Second;
                    if (ThirdKey <= FirstKey)
                        return First;
                    return Third;
                }
            }
            if (Third != nullptr)
            {
                if (MasterEventPrecedes(First, Third))
                    return First;
            }
        }
        if (Second != nullptr && Third != nullptr)
        {
            if (MasterEventPrecedes(Second, Third))
                return Third;
        }
        return Second;
    }

    // FUN_06f40230: Hoare partition over [Begin, End) on the unsigned +0x08
    // key, with retail's null-stop branches.
    FThornEvent** PartitionRetailNodes(FThornEvent** Begin, FThornEvent** End, FThornEvent* Pivot)
    {
        FThornEvent** Left = Begin;
        FThornEvent** Right = End;
        while (true)
        {
            FThornEvent* LeftNode = *Left;
            while (LeftNode != nullptr && Pivot != nullptr &&
                   static_cast<unsigned int>(LeftNode->StartTimeMs) <
                   static_cast<unsigned int>(Pivot->StartTimeMs))
            {
                Left = Left + 1;
                LeftNode = *Left;
            }

            do
            {
                Right = Right - 1;
                if (Pivot == nullptr || *Right == nullptr)
                    break;
            }
            while (static_cast<unsigned int>(Pivot->StartTimeMs) <
                   static_cast<unsigned int>((*Right)->StartTimeMs));

            if (Right <= Left)
                return Left;
            *Left = *Right;
            *Right = LeftNode;
            Left = Left + 1;
        }
    }

    // FUN_06f3fd80: partition only; insertion follows in the load caller.
    void SortMasterPartitions(FThornEvent** Begin, FThornEvent** End)
    {
        while (End - Begin > 16)
        {
            const int Count = static_cast<int>(End - Begin);
            FThornEvent* Pivot = SelectRetailMedianNode(Begin[0], Begin[Count / 2], End[-1]);
            FThornEvent** Middle = PartitionRetailNodes(Begin, End, Pivot);
            if (End - Middle <= Middle - Begin)
            {
                SortMasterPartitions(Middle, End);
                End = Middle;
            }
            else
            {
                SortMasterPartitions(Begin, Middle);
                Begin = Middle;
            }
        }
    }

    // Inlined in the scene load (IScriptEngine slot 4): partition the whole
    // range, sort the first 16 entries, then insert the rest.
    void SortMasterEvents(FThornEvent** Events, int Count)
    {
        if (Count <= 16)
        {
            SortRetailNodesByKey(Events, Count);
            return;
        }
        SortMasterPartitions(Events, Events + Count);
        SortRetailNodesByKey(Events, 16);
        for (int Index = 16; Index < Count; ++Index)
            InsertMasterEvent(Events + Index, Events[Index]);
    }

    // FUN_06f3ff50: literal signed-Type median/Hoare partitions.
    void PartitionActiveEventTypes(FThornEvent** Begin, FThornEvent** End)
    {
        while (End - Begin > 16)
        {
            FThornEvent* First = Begin[0];
            FThornEvent* Middle = Begin[(End - Begin) / 2];
            FThornEvent* Last = End[-1];
            FThornEvent* Pivot;
            if (ActiveEventTypePrecedes(First, Middle))
            {
                if (Last == nullptr)
                    Pivot = First;
                else if (Middle->Type < Last->Type)
                    Pivot = Middle;
                else if (First->Type < Last->Type)
                    Pivot = Last;
                else
                    Pivot = First;
            }
            else if (ActiveEventTypePrecedes(First, Last))
                Pivot = First;
            else if (ActiveEventTypePrecedes(Middle, Last))
                Pivot = Last;
            else
                Pivot = Middle;

            FThornEvent** Left = Begin;
            FThornEvent** Right = End;
            while (true)
            {
                FThornEvent* Event = *Left;
                while (ActiveEventTypePrecedes(Event, Pivot))
                {
                    ++Left;
                    Event = *Left;
                }
                do
                {
                    --Right;
                }
                while (Pivot != nullptr && *Right != nullptr && Pivot->Type < (*Right)->Type);
                if (Right <= Left)
                    break;
                *Left = *Right;
                *Right = Event;
                ++Left;
            }
            if (End - Left <= Left - Begin)
            {
                PartitionActiveEventTypes(Left, End);
                End = Left;
            }
            else
            {
                PartitionActiveEventTypes(Begin, Left);
                Begin = Left;
            }
        }
    }

    void IdentityOrient(Matrix* Out9)
    {
        std::memset(Out9, 0, sizeof(float) * 9);
        Out9->E[0][0] = 1.0f;
        Out9->E[1][1] = 1.0f;
        Out9->E[2][2] = 1.0f;
    }

    void Copy8(float* Dst, const float* Src)
    {
        if (Dst == nullptr || Src == nullptr)
            return;
        std::memcpy(Dst, Src, 8 * sizeof(float));
    }

    int BitsAsInt(const float* Slot)
    {
        int Value = 0;
        if (Slot != nullptr)
            std::memcpy(&Value, Slot, sizeof(Value));
        return Value;
    }

    void StoreIntBits(float* Slot, int Value)
    {
        if (Slot != nullptr)
            std::memcpy(Slot, &Value, sizeof(Value));
    }

    unsigned int BitsAsUInt(const float* Slot)
    {
        unsigned int Value = 0;
        if (Slot != nullptr)
            std::memcpy(&Value, Slot, sizeof(Value));
        return Value;
    }

    // Raw DWORD pose copy (FUN_06f38950/06f38a80). Forward reads/stores
    // keep overlapping outputs right and avoid x87 conversion.
    void CopyPoseWordsForward(float* Dst, const float* Src, int Count)
    {
        for (int Index = 0; Index < Count; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, Src + Index, sizeof(Word));
            std::memcpy(Dst + Index, &Word, sizeof(Word));
        }
    }

    void CopyPoseWordsForward(Vector* Dst, const Vector* Src)
    {
        CopyPoseWordsForward(&Dst->x, &Src->x, 3);
    }

    void CopyPoseWordsForward(Matrix* Dst, const Matrix* Src)
    {
        CopyPoseWordsForward(&Dst->E[0][0], &Src->E[0][0], 9);
    }

    void Copy3(float* Dst, const float* Src)
    {
        Dst[0] = Src[0];
        Dst[1] = Src[1];
        Dst[2] = Src[2];
    }

    void Copy3(Vector* Dst, const Vector* Src)
    {
        Dst->x = Src->x;
        Dst->y = Src->y;
        Dst->z = Src->z;
    }

    void Copy9(float* Dst, const float* Src)
    {
        for (int Index = 0; Index < 9; ++Index)
            Dst[Index] = Src[Index];
    }

    void Copy9(Matrix* Dst, const Matrix* Src)
    {
        Copy9(&Dst->E[0][0], &Src->E[0][0]);
    }

    void Copy12(float* Dst, const float* Src)
    {
        for (int Index = 0; Index < 12; ++Index)
            Dst[Index] = Src[Index];
    }

    void PackXform(Transform* Out12, const Matrix* Orient9, const Vector* Pos3)
    {
        Copy9(&Out12->Rotation, Orient9);
        Copy3(&Out12->Translation, Pos3);
    }

    void UnpackXform(const Transform* Xform12, Matrix* Orient9, Vector* Pos3)
    {
        Copy9(Orient9, &Xform12->Rotation);
        Copy3(Pos3, &Xform12->Translation);
    }

    // FUN_06f2ecc0: keep Z/Y/X differences on x87 until every input is read.
    void Cross3(Vector* Result, const Vector* A, const Vector* B)
    {
        __asm
        {
            mov ecx, A
            mov eax, B
            fld dword ptr [ecx]
            fmul dword ptr [eax+4]
            fld dword ptr [ecx+4]
            fmul dword ptr [eax]
            fsubp st(1), st(0)
            fld dword ptr [ecx+8]
            fmul dword ptr [eax]
            fld dword ptr [eax+8]
            fmul dword ptr [ecx]
            fsubp st(1), st(0)
            fld dword ptr [ecx+4]
            fmul dword ptr [eax+8]
            fld dword ptr [ecx+8]
            fmul dword ptr [eax+4]
            mov eax, Result
            fsubp st(1), st(0)
            fstp dword ptr [eax]
            fstp dword ptr [eax+4]
            fstp dword ptr [eax+8]
        }
    }

    // FUN_06f2ec80: (X*X + Y*Y) + Z*Z, then extended reciprocal.
    void Normalize3(Vector* V)
    {
        const float One = 1.0f;
        __asm
        {
            mov ecx, V
            fld dword ptr [ecx+8]
            fld dword ptr [ecx+4]
            fld dword ptr [ecx]
            fld st(0)
            fmul st(0), st(1)
            fld st(2)
            fmul st(0), st(3)
            faddp st(1), st(0)
            fld st(3)
            fmul st(0), st(4)
            faddp st(1), st(0)
            fsqrt
            fstp st(3)
            fstp st(0)
            fstp st(0)
            fdivr dword ptr One
            mov eax, V
            fld st(0)
            fmul dword ptr [eax]
            fstp dword ptr [eax]
            fld st(0)
            fmul dword ptr [eax+4]
            fstp dword ptr [eax+4]
            fmul dword ptr [eax+8]
            fstp dword ptr [eax+8]
        }
    }

    // FUN_06f23090: separate input/output, all FLD/FCHS before stores.
    void NegateVector3(Vector* Result, const Vector* Source)
    {
        __asm
        {
            mov ecx, Source
            fld dword ptr [ecx+8]
            mov eax, Result
            fchs
            fld dword ptr [ecx+4]
            fchs
            fld dword ptr [ecx]
            fchs
            fstp dword ptr [eax]
            fstp dword ptr [eax+4]
            fstp dword ptr [eax+8]
        }
    }

    char* DupHeapString(const char* Src)
    {
        if (Src == nullptr)
            return nullptr;
        const unsigned int Len = static_cast<unsigned int>(std::strlen(Src));
        char* Dst = new (std::nothrow) char[Len + 1];
        if (Dst == nullptr)
            return nullptr;
        std::memcpy(Dst, Src, Len + 1);
        return Dst;
    }

    template<typename TChar>
    void ReplaceOwnedString(TChar*& Target, const char* Source)
    {
        char* Replacement = DupHeapString(Source);
        if (Source != nullptr && Replacement == nullptr)
            return;
        delete[] Target;
        Target = Replacement;
    }

    // LAB_06f38d20: the Count gate precedes signed-map selection and traversal.
    const char* NthSortedUserPropKey(const FThornEntity* Entity, int N)
    {
        if (static_cast<unsigned int>(N) >= Entity->UserProps->size())
            return nullptr;
        FThornUserProps::const_iterator Prop = Entity->UserProps->begin();
        for (int Step = 0; Step < N; ++Step)
            ++Prop;
        return Prop->first.c_str();
    }

    void WriteColumn(Matrix* M9, int Col, const Vector* V)
    {
        // FUN_06f2ed60/80/a0 interleave raw word reads and stores. Floating
        // loads would quiet signalling NaNs in the Debug build.
        for (int Index = 0; Index != 3; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, &V->x + Index, sizeof(Word));
            std::memcpy(&M9->E[Index][Col], &Word, sizeof(Word));
        }
    }

    // FUN_06f2ed00/20/40: column getters convert via x87, Z/Y/X load order.
    void ReadColumn(const Matrix* M9, int Col, Vector* V)
    {
        const float* Source = &M9->E[0][Col];
        __asm
        {
            mov ecx, Source
            fld dword ptr [ecx+24]
            mov eax, V
            fld dword ptr [ecx+12]
            fld dword ptr [ecx]
            fstp dword ptr [eax]
            fstp dword ptr [eax+4]
            fstp dword ptr [eax+8]
        }
    }

    // FUN_06f3b670: load all eight FP words before output; word3 is raw.
    // Preserve x87 sNaN conversion and input/output overlap ordering.
    void Transpose9(Matrix* Out9, const Matrix* In9)
    {
        unsigned int RawWord;
        __asm
        {
            mov ecx, In9
            fld dword ptr [ecx+20h]
            mov eax, dword ptr [ecx+0ch]
            fld dword ptr [ecx+14h]
            mov RawWord, eax
            fld dword ptr [ecx+8]
            mov eax, Out9
            fld dword ptr [ecx+1ch]
            fld dword ptr [ecx+10h]
            fld dword ptr [ecx+4]
            fld dword ptr [ecx+18h]
            fld dword ptr [ecx]
            mov ecx, RawWord
            fstp dword ptr [eax]
            mov dword ptr [eax+4], ecx
            fstp dword ptr [eax+8]
            fstp dword ptr [eax+0ch]
            fstp dword ptr [eax+10h]
            fstp dword ptr [eax+14h]
            fstp dword ptr [eax+18h]
            fstp dword ptr [eax+1ch]
            fstp dword ptr [eax+20h]
        }
    }

    // FUN_06f3b9a0: math +0x38 TransposeTransform(out, csys.orient, csys.pos).
    // Retail consumes the result without testing the GENRESULT and has no
    // manual-math fallback.
    void TransposeTransformCsysPos(Vector* Out3, const FThornCsys* Csys)
    {
        I3DMathEngine* Math = AcquireSceneCsysMathEngine();
        // Snapshot the live input only after factory acquisition, as retail does.
        Matrix Orientation;
        Vector Position;
        Vector Result;
        std::memcpy(&Orientation, &Csys->Rotation, sizeof(Orientation));
        std::memcpy(&Position, &Csys->Translation, sizeof(Position));
        Math->TransposeTransform(Result, Orientation, Position);
        std::memcpy(Out3, &Result, sizeof(Result));
    }

    // FUN_06f3b970: nine then three forward DWORD copies. Reading each word
    // just before its store keeps overlapping buffers right.
    void PackCsysWords(void* Destination, const void* Orientation, const void* Position)
    {
        unsigned char* Output = static_cast<unsigned char*>(Destination);
        const unsigned char* Input = static_cast<const unsigned char*>(Orientation);
        for (int Index = 0; Index < 9; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, Input + Index * 4, sizeof(Word));
            std::memcpy(Output + Index * 4, &Word, sizeof(Word));
        }
        Input = static_cast<const unsigned char*>(Position);
        for (int Index = 0; Index < 3; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, Input + Index * 4, sizeof(Word));
            std::memcpy(Output + 36 + Index * 4, &Word, sizeof(Word));
        }
    }

    // FUN_06f3b9a0 + FUN_06f23090 + FUN_06f3b670 + pack. Inverse of parent Csys.
    void InvertParentCsys(Transform* Out12, const FThornCsys* Csys)
    {
        Vector Tmp;
        TransposeTransformCsysPos(&Tmp, Csys);
        Vector Neg;
        NegateVector3(&Neg, &Tmp);
        Matrix Transposed;
        Transpose9(&Transposed, &Csys->Rotation);
        PackCsysWords(Out12, &Transposed, &Neg);
    }

    enum class EEventInverseNegateOrder
    {
        Shared,
        Position,
        Orientation
    };

    // FUN_06f409a0's inline negate: each component is stored before the next.
    void NegateInversePositionWords(Vector* Words)
    {
        Words->x = -Words->x;
        Words->y = -Words->y;
        Words->z = -Words->z;
    }

    // FUN_06f40b30's inline negate runs Z/Y/X.
    void NegateInverseOrientationWords(Vector* Words)
    {
        Words->z = -Words->z;
        Words->y = -Words->y;
        Words->x = -Words->x;
    }

    // Inline inverse in entity/event consumers uses DAT_06f61e20, distinct
    // from FUN_06f3b9a0's DAT_06f61e1c. The foreign output is consumed even
    // when its GENRESULT reports failure.
    void InvertEventParentCsys(Transform* Out12, const FThornCsys* Csys,
                               EEventInverseNegateOrder NegateOrder = EEventInverseNegateOrder::Shared)
    {
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        Matrix OrientationInput;
        Vector PositionInput;
        Vector TransformedPosition;
        std::memcpy(&OrientationInput, &Csys->Rotation, sizeof(OrientationInput));
        std::memcpy(&PositionInput, &Csys->Translation, sizeof(PositionInput));
        Math->TransposeTransform(TransformedPosition, OrientationInput, PositionInput);
        Vector Tmp;
        std::memcpy(&Tmp, &TransformedPosition, sizeof(Tmp));
        Vector Neg;
        if (NegateOrder == EEventInverseNegateOrder::Shared)
            NegateVector3(&Neg, &Tmp);
        else
        {
            std::memcpy(&Neg, &Tmp, sizeof(Neg));
            if (NegateOrder == EEventInverseNegateOrder::Position)
                NegateInversePositionWords(&Neg);
            else
                NegateInverseOrientationWords(&Neg);
        }
        // Raw word transpose, as in the inline inverse of FUN_06f409a0/06f40b30.
        unsigned int Transposed[9];
        const int SourceIndices[9] = {0, 3, 6, 1, 4, 7, 2, 5, 8};
        for (int Index = 0; Index < 9; ++Index)
            std::memcpy(&Transposed[Index], &Csys->Rotation.E[0][0] + SourceIndices[Index], sizeof(Transposed[Index]));
        PackCsysWords(Out12, Transposed, &Neg);
    }

    // Retail calls the typed foreign slots without native wrapper guards.
    GENRESULT SceneMathTransform(I3DMathEngine* Math, Vector* Out, const Transform* Xform, const Vector* Source)
    {
        // All callers pass a distinct output buffer.
        Transform TransformInput;
        Vector SourceInput;
        Vector Result;
        std::memcpy(&TransformInput, Xform, sizeof(TransformInput));
        std::memcpy(&SourceInput, Source, sizeof(SourceInput));
        const GENRESULT Status = Math->TransformXform(Result, TransformInput, SourceInput);
        std::memcpy(Out, &Result, sizeof(Result));
        return Status;
    }

    GENRESULT SceneMathMulMatrix(I3DMathEngine* Math, Matrix* Out, const Matrix* A, const Matrix* B)
    {
        Matrix LeftInput;
        Matrix RightInput;
        Matrix Result;
        std::memcpy(&LeftInput, A, sizeof(LeftInput));
        std::memcpy(&RightInput, B, sizeof(RightInput));
        const GENRESULT Status = Math->MulMatrix(Result, LeftInput, RightInput);
        std::memcpy(Out, &Result, sizeof(Result));
        return Status;
    }

    GENRESULT SceneMathMulTransform(I3DMathEngine* Math, Transform* Out, const Transform* A, const Transform* B)
    {
        Transform LeftInput;
        Transform RightInput;
        Transform Result;
        std::memcpy(&LeftInput, A, sizeof(LeftInput));
        std::memcpy(&RightInput, B, sizeof(RightInput));
        const GENRESULT Status = Math->MulTransform(Result, LeftInput, RightInput);
        std::memcpy(Out, &Result, sizeof(Result));
        return Status;
    }

    GENRESULT SceneMathRotate(I3DMathEngine* Math, Vector* Out, const Transform* Xform, const Vector* Source)
    {
        Transform TransformInput;
        Vector SourceInput;
        Vector Result;
        std::memcpy(&TransformInput, Xform, sizeof(TransformInput));
        std::memcpy(&SourceInput, Source, sizeof(SourceInput));
        const GENRESULT Status = Math->Rotate(Result, TransformInput, SourceInput);
        std::memcpy(Out, &Result, sizeof(Result));
        return Status;
    }

    GENRESULT SceneQuatToMatrix(I3DMathEngine* Math, Matrix* Out, const Quaternion& Source)
    {
        Quaternion QuaternionInput;
        Matrix MatrixResult;
        std::memcpy(&QuaternionInput, &Source, sizeof(QuaternionInput));
        const GENRESULT Status = Math->QuaternionToMatrix(MatrixResult, QuaternionInput);
        std::memcpy(Out, &MatrixResult, sizeof(MatrixResult));
        return Status;
    }

    // FUN_06f44ce0: takes an already prepared inverse; builds none and checks
    // no pointers.
    void MulTransformByPreparedInverse(const Transform* Inverse, Transform* Destination, const Transform* Source)
    {
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        Transform Result;
        SceneMathMulTransform(Math, &Result, Inverse, Source);
        std::memcpy(Destination, &Result, sizeof(Result));
    }

    // Hardpoint callers use FUN_06f3b9a0 (math DAT_06f61e1c), then pass the
    // inverse to FUN_06f44ce0 (math DAT_06f61e20); both caches are kept.
    void MulXformByInverseCsys(const FThornCsys* Csys, Transform* Xform12)
    {
        Transform Inverse;
        InvertParentCsys(&Inverse, Csys);
        MulTransformByPreparedInverse(&Inverse, Xform12, Xform12);
    }

    // Csys math cache, Z/Y/X negate, raw transpose, then the event math cache.
    void TransformFloorHardpoint(const FThornCsys* Parent, Transform* Hardpoint)
    {
        Vector NegativePosition;
        TransposeTransformCsysPos(&NegativePosition, Parent);
        NegateInverseOrientationWords(&NegativePosition);
        unsigned int Transposed[9];
        const int SourceIndices[9] = {0, 3, 6, 1, 4, 7, 2, 5, 8};
        for (int Index = 0; Index < 9; ++Index)
            std::memcpy(Transposed + Index, &Parent->Rotation.E[0][0] + SourceIndices[Index], sizeof(unsigned int));
        Transform Inverse;
        PackCsysWords(&Inverse, Transposed, &NegativePosition);
        MulTransformByPreparedInverse(&Inverse, Hardpoint, Hardpoint);
    }

    // Type 11 after FUN_06f2e440 (LAB_06f44140): math +0x28 Transform pos,
    // +0x20 MulMatrix orient. Neither GENRESULT is tested.
    void ApplyForwardCsys(const FThornCsys* Csys, Vector* Pos, Matrix* Orient)
    {
        if (Csys == nullptr || Pos == nullptr || Orient == nullptr)
            return;
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        Vector WorldPos;
        SceneMathTransform(Math, &WorldPos, Csys, Pos);
        Copy3(Pos, &WorldPos);
        Matrix WorldOrient;
        SceneMathMulMatrix(Math, &WorldOrient, &Csys->Rotation, Orient);
        Copy9(Orient, &WorldOrient);
    }

    // Each inlined axis normalization multiplies by the still-extended
    // reciprocal and pops it last.
    void NormalizeAxis3(Vector* V)
    {
        const float One = 1.0f;
        __asm
        {
            mov ecx, V
            fld dword ptr [ecx+8]
            fld dword ptr [ecx+4]
            fld dword ptr [ecx]
            fld st(0)
            fmul st(0), st(1)
            fld st(2)
            fmul st(0), st(3)
            faddp st(1), st(0)
            fld st(3)
            fmul st(0), st(4)
            faddp st(1), st(0)
            fsqrt
            fstp st(3)
            fstp st(0)
            fstp st(0)
            fdivr dword ptr One
            mov eax, V
            fld dword ptr [eax]
            fmul st(0), st(1)
            fstp dword ptr [eax]
            fld dword ptr [eax+4]
            fmul st(0), st(1)
            fstp dword ptr [eax+4]
            fld dword ptr [eax+8]
            fmul st(0), st(1)
            fstp dword ptr [eax+8]
            fstp st(0)
        }
    }

    // Inlined in FUN_06f2e440: Up cross Front, Z/Y/X float stores.
    // The initial basis differs only in the operand order of the final product.
    void CrossAxisSide3(Vector* Result, const Vector* Up, const Vector* Front, bool bInitial)
    {
        const float* LastLeft = bInitial ? &Front->y : &Up->z;
        const float* LastRight = bInitial ? &Up->z : &Front->y;
        __asm
        {
            mov ecx, Up
            mov edx, Front
            mov eax, Result
            fld dword ptr [edx+4]
            fmul dword ptr [ecx]
            fld dword ptr [ecx+4]
            fmul dword ptr [edx]
            fsubp st(1), st(0)
            fstp dword ptr [eax+8]
            fld dword ptr [ecx+8]
            fmul dword ptr [edx]
            fld dword ptr [edx+8]
            fmul dword ptr [ecx]
            fsubp st(1), st(0)
            fstp dword ptr [eax+4]
            fld dword ptr [edx+8]
            fmul dword ptr [ecx+4]
            mov ecx, LastLeft
            mov edx, LastRight
            fld dword ptr [ecx]
            fmul dword ptr [edx]
            fsubp st(1), st(0)
            fstp dword ptr [eax]
        }
    }

    // Inlined in FUN_06f2e440: Dir cross Side, Z/Y/X float stores.
    void CrossAxisUp3(Vector* Result, const Vector* Dir, const Vector* Side)
    {
        __asm
        {
            mov ecx, Dir
            mov edx, Side
            mov eax, Result
            fld dword ptr [edx+4]
            fmul dword ptr [ecx]
            fld dword ptr [ecx+4]
            fmul dword ptr [edx]
            fsubp st(1), st(0)
            fstp dword ptr [eax+8]
            fld dword ptr [ecx+8]
            fmul dword ptr [edx]
            fld dword ptr [edx+8]
            fmul dword ptr [ecx]
            fsubp st(1), st(0)
            fstp dword ptr [eax+4]
            fld dword ptr [edx+8]
            fmul dword ptr [ecx+4]
            fld dword ptr [edx+4]
            fmul dword ptr [ecx+8]
            fsubp st(1), st(0)
            fstp dword ptr [eax]
        }
    }

    bool AxisDotNotGreater(const Vector* Up, const Vector* Dir)
    {
        // DAT_06f5a248 = 3fefae147ae147ae (double 0.99).
        const double Threshold = 0.99;
        unsigned char bNotGreater;
        __asm
        {
            mov ecx, Up
            mov edx, Dir
            fld dword ptr [ecx+8]
            fmul dword ptr [edx+8]
            fld dword ptr [ecx+4]
            fmul dword ptr [edx+4]
            faddp st(1), st(0)
            fld dword ptr [ecx]
            fmul dword ptr [edx]
            faddp st(1), st(0)
            fabs
            fcomp qword ptr Threshold
            fnstsw ax
            test ah, 41h
            setnz bNotGreater
        }
        // Not-greater includes unordered; greater takes the near-parallel fallback.
        return bNotGreater != 0;
    }

    // FUN_06f2e440: look-at frame from direction + Lua up/front axis IDs (0..5).
    void BuildAxisFrame(Matrix* Out9, const Vector* Direction, unsigned FrontAxis, unsigned UpAxis,
                        const unsigned int* SceneFrontAxis, const unsigned int* SceneUpAxis)
    {
        static const Vector kAxisTable[6] = {
            {1.0f, 0.0f, 0.0f},  {0.0f, 1.0f, 0.0f},  {0.0f, 0.0f, 1.0f},
            {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};

        int UpIdx = 1;
        int FrontIdx = 2;
        // A null or sentinel (6) scene up axis skips BOTH scene selections.
        if (SceneUpAxis != nullptr && *SceneUpAxis != 6)
        {
            UpIdx = static_cast<int>(*SceneUpAxis);
            if (SceneFrontAxis != nullptr && *SceneFrontAxis != 6)
                FrontIdx = static_cast<int>(*SceneFrontAxis);
        }

        Vector TableFront;
        Copy3(&TableFront, &kAxisTable[FrontIdx]);
        Vector TableUp;
        Copy3(&TableUp, &kAxisTable[UpIdx]);
        Vector TableSide;
        CrossAxisSide3(&TableSide, &TableUp, &TableFront, true);

        Vector Dir;
        Copy3(&Dir, Direction);
        NormalizeAxis3(&Dir);

        Vector UpVec;
        Copy3(&UpVec, &TableUp);

        if (FrontAxis > 2)
            NegateVector3(&Dir, &Dir);
        if (UpAxis > 2)
            NegateVector3(&UpVec, &UpVec);

        Vector OrthoUp;
        Vector Side;
        if (AxisDotNotGreater(&UpVec, &Dir))
        {
            CrossAxisSide3(&Side, &UpVec, &Dir, false);
            NormalizeAxis3(&Side);
            CrossAxisUp3(&OrthoUp, &Dir, &Side);
            NormalizeAxis3(&OrthoUp);
        }
        else
        {
            // Near-parallel: build the final up directly from Dir x TableSide.
            CrossAxisUp3(&OrthoUp, &Dir, &TableSide);
            NormalizeAxis3(&OrthoUp);
            CrossAxisSide3(&Side, &OrthoUp, &Dir, false);
            NormalizeAxis3(&Side);
        }

        bool bCol0 = false;
        bool bCol1 = false;
        bool bCol2 = false;
        bool bUpColumn0 = false;
        switch (FrontAxis)
        {
        case 0:
        case 3:
            WriteColumn(Out9, 0, &Dir);
            bCol0 = true;
            break;
        case 1:
        case 4:
            WriteColumn(Out9, 1, &Dir);
            bCol1 = true;
            break;
        case 2:
        case 5:
            WriteColumn(Out9, 2, &Dir);
            bCol2 = true;
            break;
        }
        switch (UpAxis)
        {
        case 0:
        case 3:
            WriteColumn(Out9, 0, &OrthoUp);
            bUpColumn0 = true;
            break;
        case 1:
        case 4:
            WriteColumn(Out9, 1, &OrthoUp);
            bCol1 = true;
            break;
        case 2:
        case 5:
            WriteColumn(Out9, 2, &OrthoUp);
            bCol2 = true;
            break;
        }

        // Retail: up in column 0 skips the missing-X handling.
        if (!bUpColumn0 && !bCol0)
        {
            Vector ColY;
            Vector ColZ;
            Vector ColX;
            // Retail reads Z as raw DWORDs before the x87 Y getter.
            std::memcpy(&ColZ.x, &Out9->E[0][2], 4);
            std::memcpy(&ColZ.y, &Out9->E[1][2], 4);
            std::memcpy(&ColZ.z, &Out9->E[2][2], 4);
            ReadColumn(Out9, 1, &ColY);
            Cross3(&ColX, &ColY, &ColZ);
            Normalize3(&ColX);
            WriteColumn(Out9, 0, &ColX);
            return;
        }
        if (bCol1)
        {
            if (!bCol2)
            {
                Vector ColX;
                Vector ColY;
                Vector ColZ;
                ReadColumn(Out9, 1, &ColY);
                ReadColumn(Out9, 0, &ColX);
                Cross3(&ColZ, &ColX, &ColY);
                Normalize3(&ColZ);
                WriteColumn(Out9, 2, &ColZ);
            }
            return;
        }
        Vector ColX;
        Vector ColZ;
        Vector ColY;
        ReadColumn(Out9, 0, &ColX);
        ReadColumn(Out9, 2, &ColZ);
        Cross3(&ColY, &ColZ, &ColX);
        Normalize3(&ColY);
        WriteColumn(Out9, 1, &ColY);
    }

    void Lerp3(Vector* Out, const Vector* A, const Vector* B, float T)
    {
        Out->x = A->x + (B->x - A->x) * T;
        Out->y = A->y + (B->y - A->y) * T;
        Out->z = A->z + (B->z - A->z) * T;
    }

    float Lerp1(float A, float B, float T) { return A + (B - A) * T; }

    // AUDIO tick: expects the caller's progress on the x87 stack at entry, as
    // retail keeps it there. Plain C++ changed the FADDP/store and pop stages.
    void AudioLerpFloat(const float* From, const float* To, float* Output)
    {
        __asm
        {
            mov eax, From
            fld dword ptr [eax]
            mov eax, To
            fld dword ptr [eax]
            fsub st(0), st(1)
            fmul st(0), st(2)
            fadd st(0), st(1)
            mov eax, Output
            fstp dword ptr [eax]
            fstp st(0)
        }
    }

    // AUDIO tick: the wrapped 32-bit difference is zero-extended into a qword
    // FILD, while From is added signed. Progress is on ST(0) at entry.
#pragma warning(push)
#pragma warning(disable: 4716) // The x86 double result is left in ST(0).
    __declspec(noinline) double ComputeAudioLerpInteger(int From, int To)
    {
        const unsigned int DifferenceBits = static_cast<unsigned int>(To) -
                                            static_cast<unsigned int>(From);
        const unsigned long long Difference = DifferenceBits;
        __asm
        {
            fild qword ptr Difference
            fmul st(0), st(1)
            fiadd dword ptr From
        }
    }
#pragma warning(pop)

    // Signed 32-bit difference, times float U, plus From, all on x87; plain
    // C++ spilled the operands to double.
#pragma warning(push)
#pragma warning(disable: 4716) // The x86 double result is left in ST(0).
    __declspec(noinline) double ComputeLightColor(int From, int To, const float* U)
    {
        const unsigned int DifferenceBits = static_cast<unsigned int>(To) -
                                            static_cast<unsigned int>(From);
        const int Difference = static_cast<int>(DifferenceBits);
        __asm
        {
            fild dword ptr Difference
            mov eax, U
            fmul dword ptr [eax]
            fiadd dword ptr From
        }
    }
#pragma warning(pop)

    // FOG tick: both inputs stay on x87 through FSUB/FMUL/FADD. Plain C++
    // reloads From or adds early float stores, which differ for SNaN/load order.
    void FogLerpFloat(const float* From, const float* To, const float* U, float* ResultValue)
    {
        __asm
        {
            mov eax, From
            fld dword ptr [eax]
            mov eax, To
            fld dword ptr [eax]
            fsub st(0), st(1)
            mov eax, U
            fmul dword ptr [eax]
            fadd st(0), st(1)
            mov eax, ResultValue
            fstp dword ptr [eax]
            fstp st(0)
        }
    }

    // FOG tick: int32 wrapped difference, FILD, FMUL/FIADD, then _ftol's chop
    // to qword returning the low 32 bits. No float store.
    int FogLerpInteger(int From, int To, const float* U)
    {
        int Difference;
        long long Result;
        unsigned short OldControl;
        unsigned short ChopControl;
        __asm
        {
            mov eax, To
            sub eax, From
            mov Difference, eax
            fild Difference
            mov eax, U
            fmul dword ptr [eax]
            fiadd From
            fstcw OldControl
            mov ax, OldControl
            or ax, 0c00h
            mov ChopControl, ax
            fldcw ChopControl
            fistp qword ptr Result
            fldcw OldControl
        }
        return static_cast<int>(static_cast<unsigned int>(Result));
    }

    void CopyLightProps(FThornLightProps* Dst, const FThornLightProps* Src)
    {
        if (Dst == nullptr || Src == nullptr)
            return;
        std::memcpy(Dst, Src, sizeof(FThornLightProps));
    }

    struct FThornRetailLightTarget
    {
        unsigned char Bytes[0x68];
    };

    static_assert(sizeof(FThornRetailLightTarget) == 0x68, "retail compact light target size");

    float ReadRetailFloat(const unsigned char* Blob, unsigned int Offset)
    {
        float Value = 0.0f;
        std::memcpy(&Value, Blob + Offset, sizeof(Value));
        return Value;
    }

    int ReadRetailInt(const unsigned char* Blob, unsigned int Offset)
    {
        int Value = 0;
        std::memcpy(&Value, Blob + Offset, sizeof(Value));
        return Value;
    }

    void WriteRetailFloat(unsigned char* Blob, unsigned int Offset, float Value)
    {
        std::memcpy(Blob + Offset, &Value, sizeof(Value));
    }

    void WriteRetailInt(unsigned char* Blob, unsigned int Offset, int Value)
    {
        std::memcpy(Blob + Offset, &Value, sizeof(Value));
    }

    // FUN_06f2eb20: compact source record (+0x00..+0x24) to the retail light
    // record (+0x00..+0x64). FThornLightProps is a port-side normalized record,
    // so its adapter is kept separate from this raw-layout code.
    void ConvertCompactLightRetail(const unsigned char* Source, unsigned char* Target)
    {
        // Selective writes and literal x87 branches; the TEST AH,05h + JP
        // compares take equal/greater/unordered, strict less falls through.
        const unsigned int Constant_6f5a1d4 = 0x0u;
        const unsigned int Constant_6f5a230 = 0x3c8efa35u;
        const unsigned long long Constant_6f5a270 = 0x400921fb60000000ull;
        const unsigned int Constant_6f5a26c = 0x3f8ccccdu;
        const unsigned int Constant_6f5a268 = 0x40490fdbu;
        const unsigned int Constant_6f5a1e8 = 0x3b808081u;
        const unsigned long long Constant_6f5a260 = 0x3f1a36e2eb1c432dull;
        const unsigned long long Constant_6f5a258 = 0x47efffffe0000000ull;
        const unsigned int Constant_6f5a250 = 0x3e800000u;
        const unsigned int Constant_6f5a1b8 = 0x3f800000u;
        float Scratch[4];
        __asm
        {
            mov edx, Source
            mov ecx, Target
            lea ebx, Scratch
            mov eax, dword ptr [edx + 024h]
            test eax, eax
            je Compact_6f2eb3a
            mov dword ptr [ecx], 3
            jmp Compact_6f2eba2
            Compact_6f2eb3a:
            fld dword ptr [edx + 01ch]
            fcomp dword ptr Constant_6f5a1d4
            fnstsw ax
            test ah, 5
            jp Compact_6f2eb5d
            mov eax, 040490fdbh
            mov dword ptr [ecx], 1
            mov dword ptr [ecx + 060h], eax
            mov dword ptr [ecx + 064h], eax
            jmp Compact_6f2eba2
            Compact_6f2eb5d:
            mov dword ptr [ecx], 2
            fld dword ptr [edx + 01ch]
            fmul dword ptr Constant_6f5a230
            fcom qword ptr Constant_6f5a270
            fnstsw ax
            test ah, 5
            jnp Compact_6f2eb81
            fstp st(0)
            fld qword ptr Constant_6f5a270
            Compact_6f2eb81:
            fst dword ptr [ecx + 060h]
            fmul dword ptr Constant_6f5a26c
            fcom dword ptr Constant_6f5a268
            fnstsw ax
            test ah, 5
            jnp Compact_6f2eb9f
            fstp st(0)
            fld dword ptr Constant_6f5a268
            Compact_6f2eb9f:
            fstp dword ptr [ecx + 064h]
            Compact_6f2eba2:
            fild dword ptr [edx]
            lea eax, [ecx + 4]
            mov dword ptr [ebx + 0ch], 03f800000h
            fmul dword ptr Constant_6f5a1e8
            fstp dword ptr [ebx]
            mov esi, dword ptr [ebx]
            fild dword ptr [edx + 4]
            fmul dword ptr Constant_6f5a1e8
            fstp dword ptr [ebx + 4]
            fild dword ptr [edx + 8]
            mov dword ptr [eax], esi
            mov esi, dword ptr [ebx + 4]
            mov dword ptr [eax + 4], esi
            fmul dword ptr Constant_6f5a1e8
            fstp dword ptr [ebx + 8]
            mov esi, dword ptr [ebx + 8]
            mov dword ptr [eax + 8], esi
            mov esi, dword ptr [ebx + 0ch]
            mov dword ptr [eax + 0ch], esi
            xor eax, eax
            lea esi, [ecx + 014h]
            mov dword ptr [esi], eax
            mov dword ptr [esi + 4], eax
            mov dword ptr [esi + 8], eax
            mov dword ptr [esi + 0ch], eax
            lea esi, [ecx + 024h]
            mov dword ptr [esi], eax
            mov dword ptr [esi + 4], eax
            mov dword ptr [esi + 8], eax
            mov dword ptr [esi + 0ch], eax
            mov esi, 03f800000h
            mov dword ptr [ecx + 050h], esi
            fld dword ptr [edx + 018h]
            fcomp qword ptr Constant_6f5a260
            fnstsw ax
            test ah, 5
            jp Compact_6f2ec4a
            fld qword ptr Constant_6f5a258
            mov dword ptr [ecx + 058h], 038d1b717h
            fsqrt
            mov dword ptr [ecx + 054h], 0
            mov dword ptr [ecx + 05ch], 0
            fstp dword ptr [ecx + 04ch]
            jmp CompactDone
            Compact_6f2ec4a:
            mov dword ptr [ecx + 054h], esi
            mov dword ptr [ecx + 058h], 0
            fld dword ptr [edx + 018h]
            fld st(0)
            fmul st(0), st(1)
            fmul dword ptr Constant_6f5a250
            fdivr dword ptr Constant_6f5a1b8
            fstp dword ptr [ecx + 05ch]
            fstp st(0)
            fld dword ptr [edx + 018h]
            fadd st(0), st(0)
            fstp dword ptr [ecx + 04ch]
            jmp CompactDone
        CompactDone:
        }
    }

    void ConvertCompactLight(FThornLightProps* Blob)
    {
        unsigned char Source[0x28];
        std::memcpy(Source, Blob, sizeof(Source));
        FThornRetailLightTarget Target;
        ConvertCompactLightRetail(Source, Target.Bytes);
        Blob->Type = ReadRetailInt(Target.Bytes, 0x00);
        for (int Index = 0; Index < 4; ++Index)
            Blob->Diffuse[Index] = ReadRetailFloat(Target.Bytes, static_cast<unsigned int>(0x04 + Index * 4));
        Blob->Range = ReadRetailFloat(Target.Bytes, 0x4c);
        for (int Index = 0; Index < 3; ++Index)
            Blob->Atten[Index] = ReadRetailFloat(Target.Bytes, static_cast<unsigned int>(0x54 + Index * 4));
        // Type3 leaves these two output words untouched in retail.
        if (ReadRetailInt(Source, 0x24) == 0)
        {
            std::memcpy(&Blob->Theta, Target.Bytes + 0x60, sizeof(Blob->Theta));
            std::memcpy(&Blob->Cutoff, Target.Bytes + 0x64, sizeof(Blob->Cutoff));
        }
        Blob->Specular[0] = 0.0f;
        Blob->Specular[1] = 0.0f;
        Blob->Specular[2] = 0.0f;
        Blob->Specular[3] = 0.0f;
        Blob->Ambient[0] = 0.0f;
        Blob->Ambient[1] = 0.0f;
        Blob->Ambient[2] = 0.0f;
        Blob->Ambient[3] = 0.0f;
    }

    void PackEntityLightProps(FThornLightProps* Out, const FThornEntity* Entity)
    {
        std::memset(Out, 0, sizeof(*Out));
        if (Entity == nullptr)
            return;
        if (Entity->Snap.Type == kThornEntityScene || Entity->Snap.Type == kThornEntitySubScene)
        {
            Out->Color[0] = Entity->SceneAmbient[0];
            Out->Color[1] = Entity->SceneAmbient[1];
            Out->Color[2] = Entity->SceneAmbient[2];
            return;
        }
        Out->Color[0] = static_cast<int>(Entity->LightColor[0]);
        Out->Color[1] = static_cast<int>(Entity->LightColor[1]);
        Out->Color[2] = static_cast<int>(Entity->LightColor[2]);
        Copy3(&Out->Direction, &Entity->LightDirection);
        Out->RangeTemp = Entity->LightRange;
        Out->Range = Entity->LightRange;
        Out->CutoffTemp = Entity->LightCutoff;
        Out->Cutoff = Entity->LightCutoff;
        Out->On = Entity->LightOn;
        Out->Infinite = Entity->LightInfinite;
        Out->Type = Entity->LightType;
        Copy3(Out->Diffuse, Entity->LightDiffuse);
        Out->Diffuse[3] = 1.0f;
        Copy3(Out->Specular, Entity->LightSpecular);
        Out->Specular[3] = 1.0f;
        Copy3(Out->Ambient, Entity->LightAmbient);
        Out->Ambient[3] = 1.0f;
        Copy3(Out->Atten, Entity->LightAtten);
        Out->Theta = Entity->LightTheta;
    }

    void UnpackEntityLightProps(FThornEntity* Entity, const FThornLightProps* In)
    {
        if (Entity == nullptr || In == nullptr)
            return;
        if (Entity->Snap.Type == kThornEntityScene || Entity->Snap.Type == kThornEntitySubScene)
        {
            Entity->SceneAmbient[0] = In->Color[0];
            Entity->SceneAmbient[1] = In->Color[1];
            Entity->SceneAmbient[2] = In->Color[2];
            return;
        }
        Entity->LightOn = In->On;
        Entity->LightInfinite = In->Infinite;
        Entity->LightType = In->Type;
        Entity->LightColor[0] = static_cast<float>(In->Color[0]);
        Entity->LightColor[1] = static_cast<float>(In->Color[1]);
        Entity->LightColor[2] = static_cast<float>(In->Color[2]);
        Copy3(&Entity->LightDirection, &In->Direction);
        Entity->LightRange = In->Range;
        Entity->LightCutoff = In->Cutoff;
        Entity->LightTheta = In->Theta;
        Copy3(Entity->LightDiffuse, In->Diffuse);
        Copy3(Entity->LightSpecular, In->Specular);
        Copy3(Entity->LightAmbient, In->Ambient);
        Copy3(Entity->LightAtten, In->Atten);
    }

    // Input-first signalling compare against 1.0; plain C++ reverses the
    // operands or moves the FWAIT boundary.
    bool IsLightInterpolationComplete(const float* U)
    {
        static const float One = 1.0f;
        unsigned short Status;
        __asm
        {
            mov eax, U
            fld dword ptr [eax]
            fcomp dword ptr One
            fnstsw Status
        }
        // Less/unordered skip; equal/greater continue.
        return (Status & 0x0100u) == 0;
    }

    // Keeps the X/Y differences on x87, rounds Z's difference and X's product
    // to float, and stores each result before the next final FSTP. Plain C++
    // changed these lifetimes in both Debug and Release.
    void InterpolateLightDirection(Vector* Out, const Vector* From, const Vector* To, const float* U)
    {
        float Capture[3];
        float DifferenceZ;
        float DeltaX;
        float Result[3];
        __asm
        {
            mov eax, To
            fld dword ptr [eax]
            mov edx, From
            mov ecx, dword ptr [edx]
            mov dword ptr Capture, ecx
            fsub dword ptr Capture
            mov ecx, dword ptr [edx + 4]
            fld dword ptr [eax + 4]
            mov edx, dword ptr [edx + 8]
            mov dword ptr Capture[4], ecx
            fsub dword ptr Capture[4]
            fld dword ptr [eax + 8]
            mov dword ptr Capture[8], edx
            fsub dword ptr Capture[8]
            fstp dword ptr DifferenceZ
            fxch st(1)
            mov eax, U
            fmul dword ptr [eax]
            fstp dword ptr DeltaX
            fmul dword ptr [eax]
            fld dword ptr DifferenceZ
            fmul dword ptr [eax]
            fld dword ptr DeltaX
            fadd dword ptr Capture
            fstp dword ptr Result
            mov eax, dword ptr Result
            fxch st(1)
            fadd dword ptr Capture[4]
            mov ecx, Out
            mov dword ptr [ecx], eax
            fstp dword ptr Result[4]
            mov eax, dword ptr Result[4]
            fadd dword ptr Capture[8]
            mov dword ptr [ecx + 4], eax
            fstp dword ptr Result[8]
            mov eax, dword ptr Result[8]
            mov dword ptr [ecx + 8], eax
        }
    }

    // LAB_06f45890 mask loop. Out starts as the live primary; Target is the
    // target (or the live secondary).
    void EvalLightProps(FThornLightProps* Out, const FThornLightProps* Capture,
                        const FThornLightProps* Target, const volatile int* MaskOwner, const float* U)
    {
        int Mask = *MaskOwner;
        if ((Mask & kThornLightBitRange) != 0)
            FogLerpFloat(&Capture->Range, &Target->Range, U, &Out->Range);
        if ((Mask & kThornLightBitCutoff) != 0)
            FogLerpFloat(&Capture->Cutoff, &Target->Cutoff, U, &Out->Cutoff);
        if ((Mask & kThornLightBitType) != 0 && IsLightInterpolationComplete(U))
            Out->Type = Target->Type;
        if ((Mask & kThornLightBitTheta) != 0)
            FogLerpFloat(&Capture->Theta, &Target->Theta, U, &Out->Theta);
        if ((Mask & kThornLightBitSpecular) != 0)
        {
            for (int Index = 0; Index < 3; ++Index)
                FogLerpFloat(Capture->Specular + Index, Target->Specular + Index, U, Out->Specular + Index);
        }
        if ((Mask & kThornLightBitAmbient) != 0)
        {
            for (int Index = 0; Index < 3; ++Index)
                FogLerpFloat(Capture->Ambient + Index, Target->Ambient + Index, U, Out->Ambient + Index);
        }
        if ((Mask & kThornLightBitDiffuse) != 0)
        {
            for (int Index = 0; Index < 3; ++Index)
                FogLerpFloat(Capture->Diffuse + Index, Target->Diffuse + Index, U, Out->Diffuse + Index);
        }
        if ((Mask & kThornLightBitAtten) != 0)
        {
            for (int Index = 0; Index < 3; ++Index)
                FogLerpFloat(Capture->Atten + Index, Target->Atten + Index, U, Out->Atten + Index);
        }
        if ((Mask & kThornLightBitColor) != 0)
        {
            Out->Color[0] = static_cast<int>(static_cast<unsigned int>(static_cast<__int64>(
                ComputeLightColor(Capture->Color[0], Target->Color[0], U))));
            Out->Color[1] = static_cast<int>(static_cast<unsigned int>(static_cast<__int64>(
                ComputeLightColor(Capture->Color[1], Target->Color[1], U))));
            Out->Color[2] = static_cast<int>(static_cast<unsigned int>(static_cast<__int64>(
                ComputeLightColor(Capture->Color[2], Target->Color[2], U))));
        }
        Mask = *MaskOwner; // Retail rereads the mask after the color conversions.
        if ((Mask & kThornLightBitInfinite) != 0 && IsLightInterpolationComplete(U))
            Out->Infinite = Target->Infinite;
        if ((Mask & kThornLightBitDirection) != 0)
            InterpolateLightDirection(&Out->Direction, &Capture->Direction, &Target->Direction, U);
        if ((Mask & kThornLightBitOn) != 0 && IsLightInterpolationComplete(U))
            Out->On = Target->On;
    }

    void Lerp9(float* Out, const float* A, const float* B, float T)
    {
        for (int Index = 0; Index < 9; ++Index)
            Out[Index] = A[Index] + (B[Index] - A[Index]) * T;
    }

    template <class TControl>
    void ReleaseControl(TControl*& Control)
    {
        IDAComponent* AsComponent = Control;
        FThornModernAdapters::ReleaseOwnedComponent(AsComponent);
        Control = nullptr;
    }

    template <class TControl>
    class FThornControlReleaseStage
    {
    public:
        explicit FThornControlReleaseStage(TControl*& InControl) : Control(InControl) {}
        ~FThornControlReleaseStage() noexcept(false) { ReleaseControl(Control); }
        FThornControlReleaseStage(const FThornControlReleaseStage&) = delete;
        FThornControlReleaseStage& operator=(const FThornControlReleaseStage&) = delete;
    private:
        TControl*& Control;
    };

    // Control calls go through the typed interfaces in src/include/thorn_controls.h.

    void IdentityQuat4(Quaternion& Output)
    {
        Output.w = 1.0f;
        Output.x = 0.0f;
        Output.y = 0.0f;
        Output.z = 0.0f;
    }

    void CopyQuat4(Quaternion* Dst, const Quaternion* Src)
    {
        // Raw copy, no FP evaluation.
        std::memcpy(Dst, Src, sizeof(float) * 4);
    }

    // SPATIAL: math +0x50 writes straight into the event-owned destination, so
    // a later FP exception leaves the components already written.
    void SpatialMatrixToQuaternion(I3DMathEngine* Math, Quaternion& Destination, const void* Source)
    {
        Matrix MatrixInput;
        std::memcpy(&MatrixInput, Source, sizeof(MatrixInput));
        Math->MatrixToQuaternion(Destination, MatrixInput);
    }

    // LAB_06f46a40 (math +0x54): the GENRESULT is not tested and there is no
    // identity fallback.
    void Quat4ToMat9(Matrix* Out9, const Quaternion* Src4)
    {
        Quaternion Quat;
        std::memcpy(&Quat, Src4, sizeof(Quat));
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        SceneQuatToMatrix(Math, Out9, Quat);
    }

    void SlerpQuat4(Quaternion* Out4, const Quaternion& From4, const Quaternion& To4, const float* U)
    {
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        Quaternion FromQuat;
        Quaternion ToQuat;
        Quaternion ResultQuat;
        // Inputs are read after the math cache is acquired; U stays a pointer.
        std::memcpy(&FromQuat, &From4, sizeof(FromQuat));
        std::memcpy(&ToQuat, &To4, sizeof(ToQuat));
        Math->QuatSlerpValue(ResultQuat, FromQuat, ToQuat, U);
        std::memcpy(Out4, &ResultQuat, sizeof(ResultQuat));
    }

    // Each subtraction/product stays on x87 until its final float store.
    void LerpSpatialPosition(Vector* ResultPosition, const Vector* From, const Vector* To, const float* Scale)
    {
        __asm
        {
            mov eax, ResultPosition
            mov ecx, From
            mov edx, To
            mov esi, Scale
            fld dword ptr [edx]
            fsub dword ptr [ecx]
            fmul dword ptr [esi]
            fadd dword ptr [ecx]
            fstp dword ptr [eax]
            fld dword ptr [edx+4]
            fsub dword ptr [ecx+4]
            fmul dword ptr [esi]
            fadd dword ptr [ecx+4]
            fstp dword ptr [eax+4]
            fld dword ptr [edx+8]
            fsub dword ptr [ecx+8]
            fmul dword ptr [esi]
            fadd dword ptr [ecx+8]
            fstp dword ptr [eax+8]
        }
    }

    // Stores the delta as float but adds old Applied to the still-extended
    // delta before storing the new accumulator.
    void AccumulateAxisDelta(const float* AnimT, const float* Angle, float* Applied, float* Delta)
    {
        __asm
        {
            mov eax, Applied
            mov ecx, AnimT
            mov edx, Angle
            fld dword ptr [ecx]
            fmul dword ptr [edx]
            fsub dword ptr [eax]
            mov ecx, Delta
            fst dword ptr [ecx]
            fadd dword ptr [eax]
            fstp dword ptr [eax]
        }
    }

    // FCOS is stored to float, FSIN stays on x87 through all three columns, and
    // the radian product has no intermediate float store.
    // Axis 0/3=X, 1/4=Y, 2/5=Z; unsigned >5 keeps identity without trig.
    void InitializeEventFrameWords(Matrix* Frame);

    void ApplyAxisRotFpu(Matrix* Orient9, const Quaternion& BaseQuat4, int AxisId, const float* DeltaDeg)
    {
        const unsigned int PiOver180Bits = 0x3c8efa35u;
        const unsigned int Kind = static_cast<unsigned int>(AxisId);
        Matrix Rot;
        InitializeEventFrameWords(&Rot);
        if (Kind <= 5u)
        {
            float CosValue;
            __asm
            {
                mov eax, DeltaDeg
                fld dword ptr [eax]
                fmul dword ptr PiOver180Bits
                fld st(0)
                fcos
                fstp dword ptr CosValue
                fsin
                mov edx, Kind
                cmp edx, 0
                je AxisXColumns
                cmp edx, 3
                je AxisXColumns
                cmp edx, 1
                je AxisYColumns
                cmp edx, 4
                je AxisYColumns
                lea eax, Rot
                add eax, 12
                mov ecx, 3
            AxisZLoop:
                fld dword ptr [eax-12]
                add eax, 4
                dec ecx
                fld dword ptr CosValue
                fmul st(0), st(1)
                fld st(2)
                fmul dword ptr [eax-4]
                fsubp st(1), st(0)
                fstp dword ptr [eax-16]
                fld dword ptr CosValue
                fmul dword ptr [eax-4]
                fxch st(1)
                fmul st(0), st(2)
                faddp st(1), st(0)
                fstp dword ptr [eax-4]
                jnz AxisZLoop
                jmp AxisColumnsDone
            AxisXColumns:
                lea eax, Rot
                add eax, 24
                mov ecx, 3
            AxisXLoop:
                fld dword ptr [eax-12]
                add eax, 4
                dec ecx
                fld dword ptr CosValue
                fmul st(0), st(1)
                fld st(2)
                fmul dword ptr [eax-4]
                fsubp st(1), st(0)
                fstp dword ptr [eax-16]
                fmul st(0), st(1)
                fld dword ptr CosValue
                fmul dword ptr [eax-4]
                faddp st(1), st(0)
                fstp dword ptr [eax-4]
                jnz AxisXLoop
                jmp AxisColumnsDone
            AxisYColumns:
                lea eax, Rot
                add eax, 24
                mov ecx, 3
            AxisYLoop:
                fld dword ptr [eax-24]
                add eax, 4
                dec ecx
                fld st(1)
                fmul dword ptr [eax-4]
                fld st(1)
                fmul dword ptr CosValue
                faddp st(1), st(0)
                fstp dword ptr [eax-28]
                fld dword ptr CosValue
                fmul dword ptr [eax-4]
                fxch st(1)
                fmul st(0), st(2)
                fsubp st(1), st(0)
                fstp dword ptr [eax-4]
                jnz AxisYLoop
            AxisColumnsDone:
                fstp st(0)
            }
        }

        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        Quaternion BaseQuaternion;
        Matrix BaseMatrix;
        std::memcpy(&BaseQuaternion, &BaseQuat4, sizeof(BaseQuaternion));
        Math->QuaternionToMatrix(BaseMatrix, BaseQuaternion);
        Matrix Base9;
        std::memcpy(&Base9, &BaseMatrix, sizeof(BaseMatrix));
        Math = AcquireSceneEventMathEngine();
        Matrix ResultMatrix;
        if (Kind <= 2u)
            SceneMathMulMatrix(Math, &ResultMatrix, &Rot, &Base9);
        else
            SceneMathMulMatrix(Math, &ResultMatrix, &Base9, &Rot);
        // Raw nine-word copy before the primary orientation callback.
        std::memcpy(Orient9, &ResultMatrix, sizeof(ResultMatrix));
    }

    struct FThornSceneFogSnapshot
    {
        unsigned int Prefix[7];
        FThornFog8 Fog;
    };
    static_assert(sizeof(FThornSceneFogSnapshot) == 15 * sizeof(unsigned int), "packed scene snapshot");
    static_assert(offsetof(FThornSceneFogSnapshot, Fog) == 7 * sizeof(unsigned int), "packed fog offset");

    // FUN_06f431d0(blob, mask): copy 15 dwords, mirror words 5/6 through the
    // optional output pointers, and push the colour to the light control on
    // mask bit 1.
    void ApplySceneBlob(FThornEntity* Entity, const void* Blob15, unsigned char Mask)
    {
        const unsigned char* Source = static_cast<const unsigned char*>(Blob15);
        for (int Index = 0; Index < 15; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, Source + Index * sizeof(Word), sizeof(Word));
            std::memcpy(&Entity->LiveSceneBlob[Index], &Word, sizeof(Word));
        }
        if (Entity->UpAxisSink != nullptr)
            *Entity->UpAxisSink = Entity->LiveSceneBlob[5];
        if (Entity->FrontAxisSink != nullptr)
            *Entity->FrontAxisSink = Entity->LiveSceneBlob[6];
        if (Entity->LightControl != nullptr && (Mask & 1) != 0)
        {
            // +0x104 is ILightControl (from its QI); slot 3 reads RGB words 0..2.
            Entity->LightControl->SetColor(reinterpret_cast<int*>(Entity->LiveSceneBlob));
        }
    }

    // FUN_06f43140(mask): IFogControl from packed +0x138. Bits 1 / 0xA / 0x70 / 4.
    void ApplySceneFog(FThornEntity* Entity, unsigned char Mask)
    {
        if (Entity->FogControl == nullptr)
            return;
        const unsigned char* FogBytes = reinterpret_cast<const unsigned char*>(&Entity->LiveSceneBlob[7]);
        if ((Mask & 1) != 0)
            Entity->FogControl->SetOn(FogBytes[0]);
        if ((Mask & 0xA) != 0)
            Entity->FogControl->SetTableMode(FogBytes[1], Entity->LiveSceneBlob[11]);
        if ((Mask & 0x70) != 0)
        {
            float RangeValues[3];
            std::memcpy(RangeValues, &Entity->LiveSceneBlob[12], sizeof(RangeValues));
            Entity->FogControl->SetRangeValue(&RangeValues[0], &RangeValues[1], &RangeValues[2]);
        }
        if ((Mask & 4) != 0)
        {
            // The packed block's int colour, not a float copy.
            Entity->FogControl->SetColor(reinterpret_cast<const int*>(&Entity->LiveSceneBlob[8]));
        }
    }

    // LAB_06f456e0 / LAB_06f45890 GET into the 0x24-dword blob, then copy.
    void CaptureLiveLightProps(FThornEntity* Entity, FThornLightProps* Out)
    {
        if (Entity == nullptr || Out == nullptr)
            return;
        if (Entity->Snap.Type == kThornEntityLight)
        {
            // Retail GETs in place into the entity's persistent +0x160 blob and
            // copies all 0x24 dwords out; with no control bound the blob is
            // copied untouched. Seeding from LightEntityDescriptor keeps the
            // RangeTemp/CutoffTemp/Unused words that have no normalized field.
            std::memcpy(Out, &Entity->LightEntityDescriptor, sizeof(*Out));
            char* Blob = reinterpret_cast<char*>(Out);
            if (Entity->D3dLightControl != nullptr)
            {
                Entity->D3dLightControl->GetLight(Blob + 0x28);
                Entity->D3dLightControl->GetOn(reinterpret_cast<int*>(Blob + 0x20));
                Entity->D3dLightControl->GetDirection(&Out->Direction);
            }
            else if (Entity->LightControl != nullptr)
            {
                Entity->LightControl->GetColor(reinterpret_cast<float*>(Blob + 0x00));
                Entity->LightControl->GetDirection(&Out->Direction);
                Entity->LightControl->GetRange(reinterpret_cast<float*>(Blob + 0x18));
                Entity->LightControl->GetCutoff(reinterpret_cast<float*>(Blob + 0x1C));
                Entity->LightControl->GetInfinite(reinterpret_cast<int*>(Blob + 0x24));
                Entity->LightControl->GetOn(reinterpret_cast<int*>(Blob + 0x20));
                ConvertCompactLight(Out);
            }
            std::memcpy(&Entity->LightEntityDescriptor, Out, sizeof(Entity->LightEntityDescriptor));
            return;
        }
        if (Entity->Snap.Type == kThornEntityScene)
        {
            // Retail GETs the ambient (ILightControl slot 9) in place at scene
            // +0x11c and stages only those three dwords; the rest of its 0x24-
            // dword copy is uninitialized stack. Deviation: the tail is defined
            // here. Nothing reads it: the tick's SCENE sink copies only Color[0..2].
            PackEntityLightProps(Out, Entity);
            if (Entity->LightControl != nullptr)
            {
                FThornLightProps Live;
                CopyLightProps(&Live, Out);
                Entity->LightControl->GetColor(reinterpret_cast<float*>(&Live));
                Entity->SceneAmbient[0] = Live.Color[0];
                Entity->SceneAmbient[1] = Live.Color[1];
                Entity->SceneAmbient[2] = Live.Color[2];
                Out->Color[0] = Live.Color[0];
                Out->Color[1] = Live.Color[1];
                Out->Color[2] = Live.Color[2];
            }
            return;
        }
        // Other entity types: retail copies nothing. Deviation: the output is
        // zeroed so callers stay deterministic.
        std::memset(Out, 0, sizeof(*Out));
    }

    int LightColorToInteger(const float* Value)
    {
        // Float * 255.0, qword _ftol, low 32 bits kept.
        const __int64 Converted = static_cast<__int64>(static_cast<double>(*Value) * 255.0);
        const unsigned int Word = static_cast<unsigned int>(Converted);
        int Result;
        std::memcpy(&Result, &Word, sizeof(Result));
        return Result;
    }

    void ApplyEntityLightRuntimeProps(FThornEntity* Entity, const FThornLightProps* Props)
    {
        static const float PiOver180 = 0.017453292f; // Retail 0x3c8efa35.
        ID3DLightControl* D3dLight = Entity->D3dLightControl;
        FThornLightProps& Live = Entity->LightEntityDescriptor;
        for (int Index = 0; Index < 36; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, reinterpret_cast<const unsigned char*>(Props) +
                                   Index * sizeof(Word), sizeof(Word));
            std::memcpy(reinterpret_cast<unsigned char*>(&Live) +
                            Index * sizeof(Word), &Word, sizeof(Word));
        }
        if (D3dLight != nullptr)
        {
            D3dLight->SetOn(Live.On);
            Entity->D3dLightControl->SetDirection(&Live.Direction);
            Entity->D3dLightControl->SetLight(&Live.Type);
        }
        ILightControl* Light = Entity->LightControl;
        if (Light != nullptr)
        {
            int Color[3];
            Color[0] = LightColorToInteger(&Live.Diffuse[0]);
            Color[1] = LightColorToInteger(&Live.Diffuse[1]);
            Color[2] = LightColorToInteger(&Live.Diffuse[2]);
            Light->SetColor(Color);
            Entity->LightControl->SetDirection(&Live.Direction);
            Entity->LightControl->SetRange(Live.Range);
            if (Live.Type == 1)
                Entity->LightControl->SetCutoff(-1.0f);
            else
                Entity->LightControl->SetCutoff(Live.Cutoff * PiOver180);
            Entity->LightControl->SetInfinite((Live.Type == 3) ? 1 : 0);
            Entity->LightControl->SetOn(Live.On);
        }
    }

    void ApplyLightProps(FThornEntity* Entity, const FThornLightProps* Props)
    {
        if (Entity == nullptr || Props == nullptr)
            return;
        if (Entity->Snap.Type == kThornEntityScene || Entity->Snap.Type == kThornEntitySubScene)
        {
            std::memcpy(&Entity->LightEntityDescriptor, Props, sizeof(Entity->LightEntityDescriptor));
            UnpackEntityLightProps(Entity, Props);
            if (Entity->LightControl != nullptr)
                Entity->LightControl->SetColor(const_cast<int*>(Props->Color));
            return;
        }
        ApplyEntityLightRuntimeProps(Entity, Props);
    }

    void GetLivePose(FThornEntity* Entity, Vector* Pos, Matrix* Orient)
    {
        Copy3(Pos, &Entity->Snap.Pos);
        Copy9(Orient, &Entity->Snap.Orient);
        if (Entity->GeoControl != nullptr)
        {
            Entity->GeoControl->GetPosition(Pos);
            Entity->GeoControl->GetOrientation(Orient);
        }
    }

    void CopyMotionPathBytesForward(char* Destination, const char* Source, std::size_t Length)
    {
        std::size_t Offset = 0;
        for (; Offset + sizeof(unsigned int) <= Length; Offset += sizeof(unsigned int))
        {
            unsigned int Word;
            std::memcpy(&Word, Source + Offset, sizeof(Word));
            std::memcpy(Destination + Offset, &Word, sizeof(Word));
        }
        for (; Offset < Length; ++Offset)
            Destination[Offset] = Source[Offset];
    }

    // Port-side string owner; retail used a VC6 std::string with a shared-buffer header.
    const char* AssignMotionPathType(FThornEntity* Entity, const char* Source, bool bConstructionCopy)
    {
        static const char Empty[] = "";
        const std::size_t Length = std::strlen(Source);
        if (Length > 0xfffffffdu)
            throw std::length_error("Thorn descriptor string too long");
        if (Length == 0)
        {
            delete[] Entity->PathType;
            Entity->PathType = nullptr;
            Entity->PathTypeCapacity = 0;
            return Empty;
        }
        // Retail reuses sufficient capacity only while it is <= 31.
        if (Entity->PathTypeCapacity < Length ||
            (!bConstructionCopy && Entity->PathTypeCapacity > 31))
        {
            std::unique_ptr<char[]> AliasedData;
            const UINT_PTR SourceAddress = reinterpret_cast<UINT_PTR>(Source);
            const UINT_PTR DataAddress = reinterpret_cast<UINT_PTR>(Entity->PathType);
            if (Entity->PathType != nullptr && SourceAddress >= DataAddress &&
                SourceAddress - DataAddress <= Entity->PathTypeCapacity)
                AliasedData.reset(Entity->PathType);
            else
                delete[] Entity->PathType;
            Entity->PathType = nullptr;
            Entity->PathTypeCapacity = 0;
            // An aliased source stays alive until copied (retail read freed bytes).
            const std::size_t Capacity = Length | std::size_t(31);
            char* Replacement = new char[Capacity + 1];
            CopyMotionPathBytesForward(Replacement, Source, Length);
            Replacement[Length] = 0;
            Entity->PathType = Replacement;
            Entity->PathTypeCapacity = Capacity;
        }
        else
        {
            CopyMotionPathBytesForward(Entity->PathType, Source, Length);
            Entity->PathType[Length] = 0;
        }
        return Entity->PathType;
    }

    int ReadEventWord(const void* Source, std::size_t Index)
    {
        int Word;
        std::memcpy(&Word, static_cast<const unsigned char*>(Source) + Index * sizeof(Word), sizeof(Word));
        return Word;
    }

    void ApplyMotionPathDesc(FThornEntity* Entity, const FThornMotionPathDesc* Desc)
    {
        // Retail has one string owner (+0xd4) and a separate descriptor pointer
        // (+0xc8). A null input clears only the pointer, keeping the owner.
        const char* Source = Desc->PathType;
        if (Source != nullptr)
        {
            Entity->Snap.MotionPath.PathType = AssignMotionPathType(Entity, Source, false);
        }
        else
            Entity->Snap.MotionPath.PathType = nullptr;

        // Desc may alias the entity's own packet, so Desc->MotionPath is reread
        // after the Release, as retail does.
        if (Entity->Snap.MotionPath.MotionPath != nullptr)
            Entity->Snap.MotionPath.MotionPath->Release();
        Entity->Snap.MotionPath.MotionPath = Desc->MotionPath;
        if (Entity->Snap.MotionPath.MotionPath != nullptr)
            Entity->Snap.MotionPath.MotionPath->AddRef();
    }

    // FUN_06f409a0: GeoControl GetPosition, then the inline parent inverse and
    // TransformXform through the DAT_06f61e20 math engine.
    void GetRetailRelativePosition(FThornEntity* Entity, Vector* Position)
    {
        if (Entity->GeoControl == nullptr)
            return;
        Entity->GeoControl->GetPosition(Position);
        if (Entity->ParentCsys != nullptr)
        {
            const FThornCsys* Parent = Entity->ParentCsys;
            Transform Inv;
            InvertEventParentCsys(&Inv, Parent, EEventInverseNegateOrder::Position);
            I3DMathEngine* Math = AcquireSceneEventMathEngine();
            Vector Result;
            SceneMathTransform(Math, &Result, &Inv, Position);
            std::memcpy(Position, &Result, sizeof(Result));
        }
    }

    // FUN_06f40b30: GeoControl GetOrientation, then the inline parent inverse
    // and MulMatrix through DAT_06f61e20. Retail still computes the inverse
    // translation although only the rotation is used. The GENRESULT is not tested.
    void GetRetailRelativeOrientation(FThornEntity* Entity, Matrix* Orientation)
    {
        if (Entity->GeoControl == nullptr)
            return;
        Entity->GeoControl->GetOrientation(Orientation);
        if (Entity->ParentCsys != nullptr)
        {
            const FThornCsys* Parent = Entity->ParentCsys;
            Transform Inv;
            InvertEventParentCsys(&Inv, Parent, EEventInverseNegateOrder::Orientation);
            I3DMathEngine* Math = AcquireSceneEventMathEngine();
            Matrix Result;
            SceneMathMulMatrix(Math, &Result, &Inv.Rotation, Orientation);
            std::memcpy(Orientation, &Result, sizeof(Result));
        }
    }

    // FUN_06f49f60 (per-type copies FUN_06f4a070/0d0/170/210/2b0/350): entity
    // slot 5 refreshes Snap.Pos in place through FUN_06f409a0 and returns
    // &Snap.Pos; the caller reads Y at +4.
    float GetLocalPosY(FThornEntity* Entity)
    {
        if (Entity == nullptr)
            return 0.0f;
        return Entity->InnerGetPos()->y;
    }

    // ICompoundControl +0x24: (ctrl, target_type int, name, xform).
    bool ResolveCompoundHardpoint(FThornEntity* Entity, int TypeId, const char* Name, Transform* Xform,
                                  bool bRequireType12)
    {
        if (Entity == nullptr || Entity->CompoundControl == nullptr || Xform == nullptr)
            return false;
        if (bRequireType12)
        {
            const int EntType = Entity->Snap.Type;
            if (EntType != kThornEntityCompound && EntType != kThornEntityDeformable)
                return false;
        }
        return Entity->CompoundControl->GetHardpointXform(TypeId, Name, Xform) == GR_OK;
    }

    // Retail stores the position Z/Y/X and the target X/Y/Z; the child word
    // is the first x87 operand.
    void AddAttachOffset(Vector* Result, const Vector* ChildPosition, const Vector* OffsetVector, bool bReverseStores)
    {
        if (bReverseStores)
        {
            Result->z = ChildPosition->z + OffsetVector->z;
            Result->y = ChildPosition->y + OffsetVector->y;
            Result->x = ChildPosition->x + OffsetVector->x;
        }
        else
        {
            Result->x = ChildPosition->x + OffsetVector->x;
            Result->y = ChildPosition->y + OffsetVector->y;
            Result->z = ChildPosition->z + OffsetVector->z;
        }
    }

    // Squared-length test keeps the Y/Z differences on x87. Zero exits;
    // nonzero or unordered goes on to the look-at helper.
    bool BuildAttachLookDirection(Vector* DirectionOut, const Vector* Target, const Vector* Origin)
    {
        unsigned char bUseDirection;
        const float Zero = 0.0f;
        __asm
        {
            mov ecx, Target
            mov edx, Origin
            mov eax, DirectionOut
            fld dword ptr [ecx+8]
            fsub dword ptr [edx+8]
            fld dword ptr [ecx+4]
            fsub dword ptr [edx+4]
            fld dword ptr [ecx]
            fsub dword ptr [edx]
            fstp dword ptr [eax]
            fst dword ptr [eax+4]
            fld st(1)
            fstp dword ptr [eax+8]
            fld dword ptr [eax]
            fmul dword ptr [eax]
            fld st(1)
            fmul st(0), st(2)
            faddp st(1), st(0)
            fld st(2)
            fmul st(0), st(3)
            faddp st(1), st(0)
            fcomp dword ptr Zero
            fnstsw ax
            fstp st(0)
            test ah, 44h
            fstp st(0)
            setp bUseDirection
        }
        return bUseDirection != 0;
    }

    // Identity written with raw MOVs, even with a pending x87 trap.
    void InitializeEventFrameWords(Matrix* Frame)
    {
        static const unsigned int Words[9] = {
            0x3f800000u, 0u, 0u, 0u, 0x3f800000u, 0u, 0u, 0u, 0x3f800000u};
        std::memcpy(Frame, Words, sizeof(Words));
    }
    // The effector xform is event-owned and kept after the first hardpoint write.
    void BuildIkPose(FThornEvent* Event, FThornEntity* Primary, FThornEntity** SecondaryEntry,
                     Vector* Pos, Matrix* Orient)
    {
        if (Primary->CompoundControl != nullptr)
        {
            Primary->CompoundControl->GetHardpointXform(kThornTargetPart, Event->EndEffector,
                                                      &Event->IkEffectorXform);
            const FThornCsys* Parent = Event->ParentCsys;
            if (Parent != nullptr)
                MulXformByInverseCsys(Parent, &Event->IkEffectorXform);
        }

        // Only the local frame is initialized: slot 5 returns the position and
        // neither IK body reads the secondary's orientation.
        InitializeEventFrameWords(Orient);
        std::memset(Pos, 0, sizeof(float) * 3);
        const Vector* SecondaryPosition = (*SecondaryEntry)->InnerGetPos();
        std::memcpy(Pos, SecondaryPosition, sizeof(float) * 3);

        if (Event->TargetPart != nullptr && Event->TargetType != 0)
        {
            const int SecondaryType = (*SecondaryEntry)->Snap.Type;
            if (SecondaryType == kThornEntityCompound || SecondaryType == kThornEntityDeformable)
            {
                ICompoundControl* Compound = (*SecondaryEntry)->CompoundControl;
                if (Compound != nullptr)
                {
                    Transform Hardpoint;
                    PackCsysWords(&Hardpoint, Orient, Pos);
                    Compound->GetHardpointXform(Event->TargetType, Event->TargetPart, &Hardpoint);
                    const FThornCsys* Parent = Event->ParentCsys;
                    // With no parent the hardpoint result is ignored: the
                    // secondary position and identity frame stay.
                    if (Parent != nullptr)
                    {
                        MulXformByInverseCsys(Parent, &Hardpoint);
                        std::memcpy(Orient, &Hardpoint.Rotation, sizeof(float) * 9);
                        std::memcpy(Pos, &Hardpoint.Translation, sizeof(float) * 3);
                    }
                }
            }
        }

        Vector StablePosition;
        std::memcpy(&StablePosition, Pos, sizeof(StablePosition));
        Vector Direction;
        Direction.x = Pos->x - Event->IkEffectorXform.Translation.x;
        Direction.y = Pos->y - Event->IkEffectorXform.Translation.y;
        Direction.z = Pos->z - Event->IkEffectorXform.Translation.z;
        BuildAxisFrame(Orient, &Direction, static_cast<unsigned>(Event->LookAtFrontAxis),
                       static_cast<unsigned>(Event->LookAtUpAxis), Event->SceneFrontAxis,
                       Event->SceneUpAxis);
        const FThornCsys* Parent = Event->ParentCsys;
        if (Parent != nullptr)
        {
            I3DMathEngine* Math = AcquireSceneEventMathEngine();
            Vector WorldPosition;
            SceneMathTransform(Math, &WorldPosition, Parent, &StablePosition);
            std::memcpy(Pos, &WorldPosition, sizeof(WorldPosition));
            // Retail rereads the parent and the math engine after the point transform.
            Parent = Event->ParentCsys;
            Math = AcquireSceneEventMathEngine();
            Matrix WorldOrientation;
            SceneMathMulMatrix(Math, &WorldOrientation, &Parent->Rotation, Orient);
            std::memcpy(Orient, &WorldOrientation, sizeof(WorldOrientation));
        }
    }

    void ApplyGeoPose(FThornEntity* Entity, const Vector* Pos, const Matrix* Orient)
    {
        if (Entity == nullptr)
            return;
        Entity->InnerSetPos(Pos);
        Entity->InnerSetOrient(Orient);
    }

    // Releases and clears Slot, then QIs straight into it (a failed QI leaves
    // whatever the callee wrote).
    template <class TControl>
    void BindQi(void* EngineObj, const char* Iid, TControl*& Slot, int& Result)
    {
        if (Slot != nullptr)
        {
            TControl* Previous = Slot;
            Previous->Release();
            Slot = nullptr;
        }
        IDAComponent* EngineObject = static_cast<IDAComponent*>(EngineObj);
        const GENRESULT Hr = EngineObject->QueryInterface(Iid, reinterpret_cast<void**>(&Slot));
        if (Hr >= 0)
            Result = 0;
    }

    unsigned char HrToAl(GENRESULT Hr)
    {
        return (Hr == GR_OK) ? 1 : 0;
    }

    unsigned char RetailStillRunningPredicate(int TimeMs, int EndTimeMs)
    {
        // LAB_06f483a0 / LAB_06f485e0: unsigned compare (JC), then a negative
        // end time means unbounded. A signed compare differs for a negative
        // clock or a high-bit end value.
        const unsigned int TimeBits = static_cast<unsigned int>(TimeMs);
        const unsigned int EndBits = static_cast<unsigned int>(EndTimeMs);
        if (TimeBits < EndBits)
            return 1;
        if (EndTimeMs < 0)
            return 1;
        return 0;
    }

    void ApplyEndTime(FThornEvent* Event)
    {
        if ((Event->Desc.Flags & kThornFlagUseScriptDuration) != 0)
            Event->EndTimeMs = -1;
        else
        {
            const unsigned int EndBits = static_cast<unsigned int>(Event->Desc.DurationMs) +
                                         static_cast<unsigned int>(Event->Desc.StartMs);
            std::memcpy(&Event->EndTimeMs, &EndBits, sizeof(Event->EndTimeMs));
        }
    }

    // FUN_06f423d0: GET slot 10 -> entity+0xf0, copy 8 dwords out.
    void CaptureAudioFromEntity(FThornEntity* Entity, float* Out8)
    {
        Entity->CaptureAudioProps(Out8);
    }

    // FUN_06f423a0: copy 8 dwords -> entity+0xf0, SET slot 9.
    void ApplyAudioProps(FThornEntity* Entity, const float* Props8)
    {
        // The control is read before the copy.
        ISoundControl* Control = Entity->SoundControl;
        CopyPoseWordsForward(Entity->Audio, Props8, 8);
        if (Control != nullptr)
            Control->SetAudioProps(Entity->Audio, -1);
    }

    // FUN_06f43560: descriptor +0xc8 is untouched; +0xd0 is runtime state.
    void ApplyPsysSparam(FThornEntity* Entity, const float* Value)
    {
        float Argument;
        float* Runtime = &Entity->PsysRuntimeSparam;
        __asm {
            mov eax, Value
            fld dword ptr [eax]
            fst dword ptr Argument
            mov edx, Runtime
            fstp dword ptr [edx]
        }
        // The control is read only after the runtime float is stored.
        IPSysControl* Control = Entity->PsysControl;
        if (Control != nullptr)
        {
            // Slot 6; the argument was already rounded above.
            Control->SetSparam(Argument);
        }
    }

    // LAB_06f46a40: stores T as float but subtracts from the extended value;
    // later compares reload the stored float.
    void SpatialWrapScale(float* Last, const FThornExtendedProgress* Progress, float* RoundedProgress, float* ResultScale)
    {
        static const float Zero = 0.0f;
        static const float One = 1.0f;
        static const double OneDouble = 1.0;
        float T;
        float Delta;
        float Scale;
        __asm
        {
            mov ecx, Last
            mov edx, Progress
            fld tbyte ptr [edx]
            fst T
            fsub dword ptr [ecx]
            fst Delta
            fcomp Zero
            fnstsw ax
            test ah, 44h
            // Equal: AH&44h=40h, odd parity -> JNP. Unordered=44h
            // has even parity and continues to the T comparison.
            jnp SpatialScaleZero
            fld T
            fcomp OneDouble
            fnstsw ax
            test ah, 05h
            // Greater/equal=00h and unordered=05h have even parity.
            // Less=01h has odd parity and enters the remainder branch.
            jp SpatialScaleOne
            fld One
            fsub dword ptr [ecx]
            fcom Zero
            fnstsw ax
            test ah, 44h
            // Equal=40h (odd) falls through; nonzero/unordered (even)
            // keeps the unrounded remainder on ST1 for FDIV.
            jp SpatialScaleDivide
            mov edx, T
            fstp st(0)
            mov Scale, 3f800000h
            mov dword ptr [ecx], edx
            jmp SpatialScaleDone
        SpatialScaleDivide:
            fld Delta
            mov edx, T
            fdiv st(0), st(1)
            mov dword ptr [ecx], edx
            fstp Scale
            fstp st(0)
            jmp SpatialScaleDone
        SpatialScaleOne:
            mov Scale, 3f800000h
            jmp SpatialScaleDone
        SpatialScaleZero:
            mov Scale, 0
        SpatialScaleDone:
        }
        // The initial FST dword feeds the later axis pass.
        std::memcpy(RoundedProgress, &T, sizeof(T));
        std::memcpy(ResultScale, &Scale, sizeof(Scale));
    }

    // FLOOR tick: extended T stays below Delta/Remain and is stored as float
    // only by the conditional final Last update.
    void FloorWrapScale(FThornEvent* Event, const FThornExtendedProgress* Progress, float* ScaleOutput)
    {
        float* Last = &Event->FloorWrapLast;
        const float Zero = 0.0f;
        const float One = 1.0f;
        const double OneDouble = 1.0;
        float Delta;
        float Scale;
        __asm
        {
            mov ecx, Last
            mov edx, Progress
            fld tbyte ptr [edx]
            fld st(0)
            fsub dword ptr [ecx]
            fst Delta
            fcomp Zero
            fnstsw ax
            test ah, 44h
            // Equality=40h, odd parity -> JNP; unordered=44h continues.
            jnp FloorScaleZero
            fcom OneDouble
            fnstsw ax
            test ah, 05h
            // Greater/equal=00h and unordered=05h -> even parity JP.
            jp FloorScaleOne
            fld One
            fsub dword ptr [ecx]
            fcom Zero
            fnstsw ax
            test ah, 44h
            // Nonzero/unordered has even parity -> divide; zero falls through.
            jp FloorScaleDivide
            fstp st(0)
            mov Scale, 3f800000h
            fstp dword ptr [ecx]
            jmp FloorScaleDone
        FloorScaleDivide:
            fld Delta
            fdiv st(0), st(1)
            fstp Scale
            fstp st(0)
            fstp dword ptr [ecx]
            jmp FloorScaleDone
        FloorScaleOne:
            mov Scale, 3f800000h
            jmp FloorScalePopT
        FloorScaleZero:
            mov Scale, 0
        FloorScalePopT:
            fstp st(0)
        FloorScaleDone:
        }
        // Raw copy; avoids an x87 load of a denormal.
        std::memcpy(ScaleOutput, &Scale, sizeof(Scale));
    }

    void InterpolateFloorHeight(const float* Capture, const float* Target, const float* ScaleSource, float* HeightOutput)
    {
        // Capture stays on x87 below the unrounded difference/product.
        float Height;
        __asm
        {
            mov ecx, Capture
            fld dword ptr [ecx]
            mov edx, Target
            fld dword ptr [edx]
            fsub st(0), st(1)
            mov ecx, ScaleSource
            fmul dword ptr [ecx]
            fadd st(0), st(1)
            fstp Height
            fstp st(0)
        }
        std::memcpy(HeightOutput, &Height, sizeof(Height));
    }

    int EventElapsedTime(int TimeMs, int StartTimeMs)
    {
        // LAB_06f45ef0: 32-bit wrapping subtract before FILD; done unsigned
        // to avoid signed-overflow UB.
        const unsigned int ElapsedBits = static_cast<unsigned int>(TimeMs) -
                                         static_cast<unsigned int>(StartTimeMs);
        return static_cast<int>(ElapsedBits);
    }

    void EvaluateAnimT(FThornEvent* Event, int TimeMs, FThornExtendedProgress* Result)
    {
        // Progress is kept in 80 bits.
        Event->EvaluateProgressExtended(TimeMs, Result, Event->Type == kThornTypeReverbPropAnim);
    }

    // Write the final float directly; returning it would add a reload/store.
    void StoreAnimTValue(const FThornExtendedProgress* Progress, float* Output)
    {
        __asm
        {
            mov ecx, Progress
            fld tbyte ptr [ecx]
            mov ecx, Output
            fstp dword ptr [ecx]
        }
    }

    float StoreAnimT(const FThornExtendedProgress* Progress)
    {
        float Value;
        StoreAnimTValue(Progress, &Value);
        return Value;
    }

    void ApplyCameraProps(FThornEntity* Entity, const float* FovNearFarAspect)
    {
        // Raw writes in near/far/fov/aspect order, before the SETs.
        const int StoreOrder[4] = {1, 2, 0, 3};
        for (int Index = 0; Index < 4; ++Index)
        {
            const int Field = StoreOrder[Index];
            unsigned int Word;
            std::memcpy(&Word, &FovNearFarAspect[Field], sizeof(Word));
            std::memcpy(&Entity->CameraRuntimeProps[Field], &Word, sizeof(Word));
        }
        if (Entity->CameraControl != nullptr)
        {
            // Every argument and receiver is reread after the preceding callback.
            Entity->CameraControl->SetFov(&Entity->CameraRuntimeProps[0]);
            Entity->CameraControl->SetAspect(&Entity->CameraRuntimeProps[3]);
            Entity->CameraControl->SetNear(&Entity->CameraRuntimeProps[1]);
            Entity->CameraControl->SetFar(&Entity->CameraRuntimeProps[2]);
        }
        if (Entity->Snap.Type != kThornEntityMonitor)
        {
            FThornMonitorLinkNode* Node = Entity->MonitorLinks.Sentinel->Next;
            while (Node != Entity->MonitorLinks.Sentinel)
            {
                ApplyCameraProps(Node->Entity, Entity->CameraRuntimeProps);
                // Next is read after the child callback, not before.
                Node = Node->Next;
            }
        }
    }

    void InterpolateCameraProps(float* ResultValues, const float* TargetValues,
                                const float* CapturedValues,
                                const FThornExtendedProgress* Progress, int Mask)
    {
        // The extended progress stays on x87 under each pair of loads; only the
        // interpolated fields get float stores.
        __asm
        {
            mov ecx, Progress
            fld tbyte ptr [ecx]
            mov ecx, CapturedValues
            mov edx, TargetValues
            mov edi, ResultValues
            mov eax, Mask
            test al, 1
            jz CameraInterpolationAspect
            fld dword ptr [ecx]
            fld dword ptr [edx]
            fsub st(0), st(1)
            fmul st(0), st(2)
            fadd st(0), st(1)
            fstp dword ptr [edi]
            fstp st(0)
        CameraInterpolationAspect:
            test al, 8
            jz CameraInterpolationNear
            fld dword ptr [ecx + 0ch]
            fld dword ptr [edx + 0ch]
            fsub st(0), st(1)
            fmul st(0), st(2)
            fadd st(0), st(1)
            fstp dword ptr [edi + 0ch]
            fstp st(0)
        CameraInterpolationNear:
            test al, 2
            jz CameraInterpolationFar
            fld dword ptr [ecx + 4]
            fld dword ptr [edx + 4]
            fsub st(0), st(1)
            fmul st(0), st(2)
            fadd st(0), st(1)
            fstp dword ptr [edi + 4]
            fstp st(0)
        CameraInterpolationFar:
            test al, 4
            jz CameraInterpolationDone
            fld dword ptr [ecx + 8]
            fld dword ptr [edx + 8]
            fsub st(0), st(1)
            fmul st(0), st(2)
            fadd st(0), st(1)
            fstp dword ptr [edi + 8]
            fstp st(0)
        CameraInterpolationDone:
            fstp st(0)
        }
    }

    void CaptureCameraProps(FThornEntity* Entity, float* Out4)
    {
        if (Entity->CameraControl != nullptr)
        {
            // FUN_06f460f0 writes GET results into the runtime cache.
            Entity->CameraControl->GetFov(&Entity->CameraRuntimeProps[0]);
            Entity->CameraControl->GetNear(&Entity->CameraRuntimeProps[1]);
            Entity->CameraControl->GetFar(&Entity->CameraRuntimeProps[2]);
            Entity->CameraControl->GetAspect(&Entity->CameraRuntimeProps[3]);
        }
        // Forward raw DWORD copy.
        for (int Index = 0; Index < 4; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, &Entity->CameraRuntimeProps[Index], sizeof(Word));
            std::memcpy(&Out4[Index], &Word, sizeof(Word));
        }
    }

    ISubtitleControl* SubtitleOf(FThornEntity* Entity)
    {
        if (Entity == nullptr)
            return nullptr;
        // FUN_06f49d70/06f49e20 pick the subtitle facet by entity type; other
        // types get none even if a stale pointer exists.
        const int Type = Entity->Snap.Type;
        if (Type != kThornEntityDeformable &&
            Type != kThornEntityCamera && Type != kThornEntityMonitor &&
            Type != kThornEntitySound && Type != kThornEntityScene)
            return nullptr;
        return Entity->SubtitleControl;
    }

    bool GrowPointerArray(void*** Array, int* Cap, int Needed, int ElemSize)
    {
        if (Needed <= *Cap)
            return true;
        int NewCap = (*Cap == 0) ? 8 : (*Cap * 2);
        while (NewCap < Needed)
            NewCap = NewCap * 2;
        unsigned char* NewBuf = new (std::nothrow) unsigned char[static_cast<unsigned int>(NewCap) *
                                                                 static_cast<unsigned int>(ElemSize)];
        if (NewBuf == nullptr)
            return false;
        if (*Array != nullptr)
        {
            std::memcpy(NewBuf, *Array,
                        static_cast<unsigned int>(*Cap) * static_cast<unsigned int>(ElemSize));
            delete[] reinterpret_cast<unsigned char*>(*Array);
        }
        *Array = reinterpret_cast<void**>(NewBuf);
        *Cap = NewCap;
        return true;
    }

    // FUN_06f415f0: append Other unless already linked; returns 1 if added.
    unsigned char AddMonitorLink(FThornEntity* Self, FThornEntity* Other)
    {
        FThornMonitorLinkNode* Sentinel = Self->MonitorLinks.Sentinel;
        FThornMonitorLinkNode* Node = Sentinel->Next;
        while (Node != Sentinel)
        {
            if (Node->Entity == Other)
                return 0;
            Node = Node->Next;
        }
        FThornMonitorLinkNode* Previous = Sentinel->Previous;
        Node = new FThornMonitorLinkNode;
        Node->Next = Sentinel;
        // A null tail falls back to Node itself.
        if (Previous == nullptr)
            Previous = Node;
        Node->Previous = Previous;
        Sentinel->Previous = Node;
        Previous->Next = Node;
        Node->Entity = Other;
        ++Self->MonitorLinks.Count;
        return 1;
    }

    // FUN_06f41660: erase the first link to Other.
    unsigned char RemoveMonitorLink(FThornEntity* Self, FThornEntity* Other)
    {
        FThornMonitorLinkNode* Sentinel = Self->MonitorLinks.Sentinel;
        FThornMonitorLinkNode* Node = Sentinel->Next;
        while (Node != Sentinel)
        {
            if (Node->Entity == Other)
            {
                Self->MonitorLinks.Erase(Node);
                return 1;
            }
            Node = Node->Next;
        }
        return 0;
    }

    unsigned char UnbindMonitorChildren(FThornEntity* Self)
    {
        FThornMonitorLinkNode* Node = Self->MonitorLinks.Sentinel->Next;
        while (Node != Self->MonitorLinks.Sentinel)
        {
            RemoveMonitorLink(Node->Entity, Self);
            // Retail quirk kept: Next is read after the reciprocal removal and
            // the loop continues at SavedNext->Next, skipping a node. A self-link
            // leaves Node dangling, as in retail.
            FThornMonitorLinkNode* SavedNext = Node->Next;
            Self->MonitorLinks.Erase(Node);
            Node = SavedNext->Next;
        }
        return 1; // Also for an empty list.
    }
}

void ThornInitLightEntityDefaults(FThornLightProps* Props)
{
    // FUN_06f41840: runs the retail initialization on a raw-offset buffer,
    // then copies the values into the port-side record.
    static const unsigned int kOneBits = 0x3f800000;
    if (Props == nullptr)
        return;

    unsigned char Retail[0xd4];
    std::memset(Retail, 0, sizeof(Retail));
    WriteRetailFloat(Retail, 0x5c, 1000.0f);
    WriteRetailFloat(Retail, 0x60, 180.0f);
    WriteRetailInt(Retail, 0x64, 1);
    WriteRetailFloat(Retail, 0x58, 1.0f);
    WriteRetailFloat(Retail, 0x68, 0.0f);
    WriteRetailInt(Retail, 0x44, 0xff);
    WriteRetailInt(Retail, 0x48, 0xff);
    WriteRetailInt(Retail, 0x4c, 0xff);
    WriteRetailInt(Retail, 0x6c, 2);
    WriteRetailFloat(Retail, 0x70, 1.0f);
    WriteRetailFloat(Retail, 0x74, 1.0f);
    WriteRetailFloat(Retail, 0x78, 1.0f);
    WriteRetailFloat(Retail, 0x7c, 1.0f);
    WriteRetailFloat(Retail, 0xb8, 1000.0f);
    WriteRetailFloat(Retail, 0xbc, 1.0f);
    WriteRetailFloat(Retail, 0xc0, 0.0f);
    WriteRetailFloat(Retail, 0xc4, 0.0f);
    WriteRetailFloat(Retail, 0xc8, 0.0001f);
    WriteRetailInt(Retail, 0xcc, 0x4020d97c);
    WriteRetailInt(Retail, 0xd0, 0x40490fdb);

    std::memset(Props, 0, sizeof(*Props));
    Props->Color[0] = ReadRetailInt(Retail, 0x44);
    Props->Color[1] = ReadRetailInt(Retail, 0x48);
    Props->Color[2] = ReadRetailInt(Retail, 0x4c);
    Props->Direction.x = ReadRetailFloat(Retail, 0x50);
    Props->Direction.y = ReadRetailFloat(Retail, 0x54);
    Props->Direction.z = ReadRetailFloat(Retail, 0x58);
    Props->RangeTemp = ReadRetailFloat(Retail, 0x5c);
    Props->CutoffTemp = ReadRetailFloat(Retail, 0x60);
    Props->On = ReadRetailInt(Retail, 0x64);
    Props->Infinite = ReadRetailInt(Retail, 0x68);
    Props->Type = ReadRetailInt(Retail, 0x6c);
    Props->Diffuse[0] = ReadRetailFloat(Retail, 0x70);
    Props->Diffuse[1] = ReadRetailFloat(Retail, 0x74);
    Props->Diffuse[2] = ReadRetailFloat(Retail, 0x78);
    Props->Diffuse[3] = ReadRetailFloat(Retail, 0x7c);
    Props->Range = ReadRetailFloat(Retail, 0xb8);
    std::memcpy(&Props->Unused1e, &kOneBits, sizeof(Props->Unused1e));
    Props->Atten[0] = ReadRetailFloat(Retail, 0xc0);
    Props->Atten[1] = ReadRetailFloat(Retail, 0xc4);
    Props->Atten[2] = ReadRetailFloat(Retail, 0xc8);
    Props->Theta = ReadRetailFloat(Retail, 0xcc);
    Props->Cutoff = ReadRetailFloat(Retail, 0xcc + 4);
}

void ThornConvertCompactLight(FThornLightProps* Blob)
{
    ConvertCompactLight(Blob);
}

void ThornCopyLightPropsToEntity(FThornEntity* Entity, const FThornLightProps* Props)
{
    if (Entity == nullptr || Props == nullptr)
        return;
    // The retail loader (FUN_06f28a60 case 5) fills the entity's +0x160 blob;
    // LightEntityDescriptor mirrors it so the START_LIGHT_PROP_ANIM capture can
    // seed from it.
    std::memcpy(&Entity->LightEntityDescriptor, Props, sizeof(Entity->LightEntityDescriptor));
    UnpackEntityLightProps(Entity, Props);
}

static void InitializeLightEntityPortState(FThornEntity* Entity)
{
    if (Entity == nullptr)
        return;
    FThornLightProps Defaults;
    ThornInitLightEntityDefaults(&Defaults);
    // FUN_06f41840 writes these defaults into the +0x160 blob itself.
    std::memcpy(&Entity->LightEntityDescriptor, &Defaults, sizeof(Entity->LightEntityDescriptor));
    UnpackEntityLightProps(Entity, &Defaults);
    Entity->LightRuntimeState[0] = 0;
    Entity->LightRuntimeState[1] = 0;
    Entity->bLightStateInitialized = 1;
}

void ThornPublishEntityDescTail(FThornEntity* Entity)
{
    // Retail's loader builds each entity's descriptor, tail included, and the type's
    // constructor copies it into the entity (LIGHT keeps it at +0x84, LAB_06f4a110);
    // IScriptEngine slots 8 / 9 hand that copy out. The port's Lua loader writes the
    // native fields instead, so the descriptor tail (thorn_iface.h FThornEntitySnap)
    // is filled from them here, once per loaded entity.
    if (Entity == nullptr)
        return;
    const int Type = Entity->Snap.Type;
    Entity->DescriptorOwnerType = Type;
    if (Type == kThornEntityCamera || Type == kThornEntityMonitor)
        ApplyCameraProps(Entity, &Entity->Snap.Fovh);
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable)
        Entity->Snap.FloorHeight = Entity->FloorHeight;
    else if (Type == kThornEntityLight)
    {
        // A reference entity (flag 1) gets the defaults, not the parsed fields.
        if ((Entity->Snap.Flags & 1) != 0)
        {
            ThornInitLightEntityDefaults(&Entity->Snap.Light);
            ThornCopyLightPropsToEntity(Entity, &Entity->Snap.Light);
        }
        else
            std::memcpy(&Entity->Snap.Light, &Entity->LightEntityDescriptor, sizeof(Entity->Snap.Light));
    }
    else if (Type == kThornEntitySound)
    {
        // All eight words, even for reference entities; the constructor
        // (IScriptEngine slot 35) applies the reference gate later.
        std::memcpy(Entity->Snap.Audio, Entity->Audio, sizeof(Entity->Snap.Audio));
    }
    else if (Type == kThornEntityScene || Type == kThornEntitySubScene)
    {
        if (Type == kThornEntityScene && (Entity->Snap.Flags & 1) == 0)
            std::memcpy(Entity->Snap.SceneBlob, Entity->LiveSceneBlob, sizeof(Entity->Snap.SceneBlob));
        else
        {
            std::memset(Entity->Snap.SceneBlob, 0, sizeof(Entity->Snap.SceneBlob));
            Entity->Snap.SceneBlob[5] = 6;
            Entity->Snap.SceneBlob[6] = 6;
        }
        ApplySceneBlob(Entity, Entity->Snap.SceneBlob, 0xFF);
    }
    else if (Type == kThornEntityMotionPath)
    {
        if ((Entity->Snap.Flags & 1) == 0)
        {
            Entity->Snap.MotionPath.PathType = Entity->PathType;
            Entity->Snap.MotionPath.MotionPath = Entity->ParsedMotionPathDescObject;
        }
        else
            Entity->Snap.MotionPath = {nullptr, nullptr};
    }
    else if (Type == kThornEntityPsys && (Entity->Snap.Flags & 1) != 0)
        std::memset(&Entity->Snap.PsysSparam, 0, sizeof(Entity->Snap.PsysSparam));
}

bool FThornConnectString::Assign(const char* Source)
{
    const size_t SourceLength = std::strlen(Source);
    // Retail's inline max_size/_Xlen check. Allocation is plain new[], not
    // the VC6 reference-counted std::string buffer.
    if (SourceLength > 0xfffffffdu)
        throw std::length_error("Thorn descriptor string too long");
    if (SourceLength == 0)
    {
        Reset();
        return false;
    }

    if (Capacity > 31u || Capacity < SourceLength)
    {
        // Retail frees the old buffer before growing (trim=true), leaving an
        // aliased source dangling; here it stays alive until copied.
        std::unique_ptr<char[]> AliasedData;
        const UINT_PTR SourceAddress = reinterpret_cast<UINT_PTR>(Source);
        const UINT_PTR DataAddress = reinterpret_cast<UINT_PTR>(Data.get());
        if (Data && SourceAddress >= DataAddress && SourceAddress - DataAddress <= Length)
            AliasedData = std::move(Data);
        Reset();
        size_t NewCapacity = SourceLength | 31u;
        if (NewCapacity > 0xfffffffdu)
            NewCapacity = SourceLength;
        try
        {
            Data.reset(new char[NewCapacity + 1u]);
        }
        catch (const std::bad_alloc&)
        {
            // The CRT's _Copy retries with the exact length. Its new-handler/COW
            // exception behaviour is not reproduced.
            NewCapacity = SourceLength;
            Data.reset(new char[NewCapacity + 1u]);
        }
        Capacity = NewCapacity;
        std::memcpy(Data.get(), Source, SourceLength);
    }
    else
    {
        // Own-buffer substrings move toward the start, like retail's forward
        // REP copy.
        std::memmove(Data.get(), Source, SourceLength);
    }
    Length = SourceLength;
    Data[Length] = '\0';
    return true;
}

char* FThornConnectString::GetPublishedData()
{
    // Owner data, or a static empty string (retail: the CRT's shared empty byte).
    static char Empty[1] = {'\0'};
    return Data ? Data.get() : Empty;
}

void FThornConnectString::Reset()
{
    Data.reset();
    Length = 0;
    Capacity = 0;
}

void FThornEvent::AdoptConnectStringStorage()
{
    // Strings arrive only through the setters, so nothing is adopted; the flag
    // only routes cleanup.
    bConnectStorageActive = true;
}

void FThornEvent::ReleaseConnectStringStorage()
{
    AdoptConnectStringStorage();
    // Parent hardpoint first, then hardpoint; the common base releases no curve.
    ConnectParentHardpointStorage.Reset();
    ConnectHardpointStorage.Reset();
}

void FThornEvent::SetConnectHardpoint(const char* Name)
{
    AdoptConnectStringStorage();
    Hardpoint = nullptr;
    if (Name != nullptr)
    {
        ConnectHardpointStorage.Assign(Name);
        Hardpoint = ConnectHardpointStorage.GetPublishedData();
    }
}

void FThornEvent::SetConnectParentHardpoint(const char* Name)
{
    AdoptConnectStringStorage();
    ParentHardpoint = nullptr;
    if (Name != nullptr)
    {
        ConnectParentHardpointStorage.Assign(Name);
        ParentHardpoint = ConnectParentHardpointStorage.GetPublishedData();
    }
}

void FThornEvent::AdoptSpatialStringStorage()
{
    bSpatialStorageActive = true;
}

void FThornEvent::SetSpatialTargetPart(const char* Name)
{
    AdoptSpatialStringStorage();
    TargetPart = nullptr;
    // Null skips the assignment; otherwise the owner's data (or the empty
    // string) is published. CRT capacity/COW/allocation-failure details are
    // not reproduced.
    if (Name != nullptr)
    {
        SpatialTargetPartStorage.Assign(Name);
        TargetPart = SpatialTargetPartStorage.GetPublishedData();
    }
}

void FThornEvent::AdoptFloorStringStorage()
{
    bFloorStorageActive = true;
}

void FThornEvent::SetFloorTargetPart(const char* Name)
{
    AdoptFloorStringStorage();
    TargetPart = nullptr;
    // Null skips the assignment: the owner survives while the descriptor
    // pointer stays null.
    if (Name != nullptr)
    {
        FloorTargetPartStorage.Assign(Name);
        TargetPart = FloorTargetPartStorage.GetPublishedData();
    }
}

void FThornEvent::ApplyFloorHeightDesc(const void* DescIn)
{
    AdoptFloorStringStorage();
    ApplyCommonDesc(DescIn);
    // Common descriptor, forced type, cleared target, then the incoming type
    // is reread.
    Desc.Type = kThornTypeFloorHeightAnim;
    TargetPart = nullptr;
    TargetType = 0;
    if (ReadEventWord(DescIn, 0) == kThornTypeFloorHeightAnim)
    {
        std::memcpy(&FloorTarget, static_cast<const unsigned char*>(DescIn) + 44, sizeof(FloorTarget));
        SetFloorTargetPart(reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 12))));
        TargetType = ReadEventWord(DescIn, 13);
    }
    // Common timing/endpoints are mirrored after the payload.
    FinalizeDesc();
}

void FThornEvent::ReleaseFloorOwnedState()
{
    AdoptFloorStringStorage();
    try
    {
        if (ParamCurve != nullptr)
        {
            ParamCurve->Release();
        }
    }
    catch (...)
    {
        // Retail's unwind also frees the string if the curve Release throws.
        FloorTargetPartStorage.Reset();
        throw;
    }
    FloorTargetPartStorage.Reset();
}

FThornEvent::FThornEvent()
    : FThornEvent(0)
{
}

FThornEvent::FThornEvent(int DescriptorStorageType)
    : ParentCsys(nullptr), bOwnsParentCsys(0), SceneUpAxis(nullptr), SceneFrontAxis(nullptr), StartTimeMs(0), UniqueId(static_cast<int>(G_NextEventId++)), Type(0), PrimaryIndex(-1), SecondaryIndex(-1),
      EndTimeMs(-1), Unused20(0), bFloorEventOwner(DescriptorStorageType == kThornTypeFloorHeightAnim), NativeEventType(DescriptorStorageType), UserEventDesc{}, FogDesc{}, PsysPropDesc{}, AudioPropDesc{}, LightDesc{}, ReverbDesc{}, CameraDesc{}, SetCameraDesc{}, FloorDesc{}, PathDesc{},
      ParamCurve(DescriptorStorageType == kThornTypeFogPropAnim
          ? FogDesc.Common.ParamCurve : DescriptorStorageType == kThornTypePsysPropAnim
              ? PsysPropDesc.Common.ParamCurve : DescriptorStorageType == kThornTypeAudioPropAnim
                  ? AudioPropDesc.Common.ParamCurve : DescriptorStorageType == kThornTypeLightPropAnim
                      ? LightDesc.Common.ParamCurve : DescriptorStorageType == kThornTypeReverbPropAnim
                          ? ReverbDesc.Common.ParamCurve : DescriptorStorageType == kThornTypeCameraPropAnim
                              ? CameraDesc.Common.ParamCurve : DescriptorStorageType == kThornTypeSetCamera
                                  ? SetCameraDesc.ParamCurve : DescriptorStorageType == kThornTypeFloorHeightAnim
                                      ? FloorDesc.Common.ParamCurve : DescriptorStorageType == kThornTypePathAnim
                                          ? PathDesc.Common.ParamCurve : UserEventDesc.Common.ParamCurve),
      Desc(DescriptorStorageType == kThornTypeFogPropAnim
          ? FogDesc.Common : DescriptorStorageType == kThornTypePsysPropAnim
              ? PsysPropDesc.Common : DescriptorStorageType == kThornTypeAudioPropAnim
                  ? AudioPropDesc.Common : DescriptorStorageType == kThornTypeLightPropAnim
                      ? LightDesc.Common : DescriptorStorageType == kThornTypeReverbPropAnim
                          ? ReverbDesc.Common : DescriptorStorageType == kThornTypeCameraPropAnim
                              ? CameraDesc.Common : DescriptorStorageType == kThornTypeSetCamera
                                  ? SetCameraDesc : DescriptorStorageType == kThornTypeFloorHeightAnim
                                      ? FloorDesc.Common : DescriptorStorageType == kThornTypePathAnim
                                          ? PathDesc.Common : UserEventDesc.Common),
      PcurvePeriod(DescriptorStorageType == kThornTypeFogPropAnim
          ? FogDesc.Common.ParamCurvePeriod : DescriptorStorageType == kThornTypePsysPropAnim
              ? PsysPropDesc.Common.ParamCurvePeriod : DescriptorStorageType == kThornTypeAudioPropAnim
                  ? AudioPropDesc.Common.ParamCurvePeriod : DescriptorStorageType == kThornTypeLightPropAnim
                      ? LightDesc.Common.ParamCurvePeriod : DescriptorStorageType == kThornTypeReverbPropAnim
                          ? ReverbDesc.Common.ParamCurvePeriod : DescriptorStorageType == kThornTypeCameraPropAnim
                              ? CameraDesc.Common.ParamCurvePeriod : DescriptorStorageType == kThornTypeSetCamera
                                  ? SetCameraDesc.ParamCurvePeriod : DescriptorStorageType == kThornTypeFloorHeightAnim
                                      ? FloorDesc.Common.ParamCurvePeriod : DescriptorStorageType == kThornTypePathAnim
                                          ? PathDesc.Common.ParamCurvePeriod : UserEventDesc.Common.ParamCurvePeriod), SpatialMask(0),
      AxisRotAngle(0.0f), AxisRotAxis(1), AxisRotApplied(0.0f), SpatialWrapLast(0.0f),
      bHasTargetPos(false),
      bHasTargetOrient(false), bCaptured(false), UserEventString(nullptr), CamMask(CameraDesc.Mask), CamTarget(CameraDesc.Target), LightMask(LightDesc.Mask), LightTarget(LightDesc.Target),
      StartPercent(PathDesc.StartPercent), StopPercent(PathDesc.StopPercent),
      PercentDelta(PathDesc.PercentDelta), PathOffset(PathDesc.Offset), Animation(nullptr),
      LockedBone(nullptr), TransTime(0.0f), TimeScale(1.0f), Weight(1.0f), Heading(0.0f),
      TransScale(1.0f), MotionStartTime(0.0f), Field_0x40(0), Field_0x50(0), SubSceneStartTime(0.0f), Hardpoint(nullptr),
      ParentHardpoint(nullptr), EndEffector(nullptr), LegacyTargetPart(nullptr),
      TargetPart(DescriptorStorageType == kThornTypeFloorHeightAnim ? FloorDesc.TargetPart : LegacyTargetPart),
      LegacyTargetType(0), TargetType(DescriptorStorageType == kThornTypeFloorHeightAnim ? FloorDesc.TargetType : LegacyTargetType),
      LookAtUpAxis(PathDesc.LookAtUpAxis), LookAtFrontAxis(PathDesc.LookAtFrontAxis),
      bPointAt(0), bMoveTo(0), IkId(0),
      CountToRoot(0),
      Damping(DescriptorStorageType == kThornTypeStartIk ? 1.0f : 0.0f),
      TransitionDuration(DescriptorStorageType == kThornTypeStartIk ? 1.0f : 0.0f), SparamTarget(PsysPropDesc.Sparam), SparamCapture(0.0f), PsysMask(PsysPropDesc.Mask),
      AudioMask(AudioPropDesc.Mask), AudioTarget(AudioPropDesc.Target), AudioCapture{}, FogMask(FogDesc.Mask), FogTarget(FogDesc.Target), FogCaptureOn(0), FogCaptureStart(0.0f), FogCaptureEnd(0.0f),
      FogCaptureDensity(0.0f), ReverbMask(ReverbDesc.Mask), ReverbTarget(ReverbDesc.Target), FloorTarget(FloorDesc.FloorHeight), FloorCapture(0.0f),
      FloorWrapLast(0.0f), Strid(0),
      SoundStartTime(0)
{
    bIkStorageActive = NativeEventType == kThornTypeStartIk;
    UserEventDesc.ExternalPayloadWord = 0;
    std::memset(&Desc, 0, sizeof(Desc));
    Desc.UniqueId = -1;
    Desc.PrimaryIndex = -1;
    Desc.SecondaryIndex = -1;
    Desc.ParamCurvePeriod = -1;
    StartPercent = 0.0f;
    StopPercent = 1.0f;
    PercentDelta = 1.0f;
    LookAtUpAxis = 1;
    LookAtFrontAxis = 5;
    std::memset(&TargetPos, 0, sizeof(TargetPos));
    IdentityOrient(&TargetOrient);
    std::memset(&CapturePos, 0, sizeof(CapturePos));
    IdentityOrient(&CaptureOrient);
    IdentityQuat4(TargetQuat);
    IdentityQuat4(CaptureQuat);
    std::memset(CamTarget, 0, sizeof(CamTarget));
    std::memset(CamCapture, 0, sizeof(CamCapture));
    std::memset(&LightTarget, 0, sizeof(LightTarget));
    std::memset(&LightCapture, 0, sizeof(LightCapture));
    std::memset(&PathOffset, 0, sizeof(PathOffset));
    std::memset(PointAt, 0, sizeof(PointAt));
    std::memset(&AttachXform, 0, sizeof(AttachXform));
    AttachXform.Rotation.E[0][0] = 1.0f;
    AttachXform.Rotation.E[1][1] = 1.0f;
    AttachXform.Rotation.E[2][2] = 1.0f;
    // Retail audio defaults, written into the descriptor packet.
    AudioTarget.Dmin = 50.0f;
    AudioTarget.Dmax = 1000.0f;
    AudioTarget.Ain = 0x168;
    AudioTarget.Aout = 0x168;
    std::memset(&FogTarget, 0, sizeof(FogTarget));
    std::memset(FogCaptureColor, 0, sizeof(FogCaptureColor));
    std::memset(&FogCapturePacked, 0, sizeof(FogCapturePacked));
    // Deviation: retail (FUN_06f49070) leaves the four reverb target words
    // unwritten on a type mismatch; here they are zeroed.
    std::memset(ReverbCapture, 0, sizeof(ReverbCapture));
}

// Descriptor callbacks run inside construction, before the event joins MASTER.
// If they throw, C++ unwinds the delegated constructor and the storage; retail's
// per-type constructor unwind paths are not reproduced exactly.
FThornEvent::FThornEvent(int Type, const void* Desc)
    : FThornEvent(Type)
{
    FThornEvent* Event = this;
    struct FConstructorUnwindStage
    {
        FThornEvent* Event;
        bool bClearCurveOnFailure;
        bool bCompleted;

        ~FConstructorUnwindStage()
        {
            // Retail's typed constructor unwind destroys only the common base.
            // This runs before the delegated destructor.
            if (!bCompleted && bClearCurveOnFailure)
                Event->ParamCurve = nullptr;
        }
    };
    const bool bClearCurveOnFailure =
        Type == kThornTypeLightPropAnim ||
        Type == kThornTypeReverbPropAnim ||
        Type == kThornTypePsysPropAnim ||
        Type == kThornTypeFogPropAnim ||
        Type == kThornTypeAudioPropAnim ||
        Type == kThornTypeCameraPropAnim ||
        Type == kThornTypeSubtitle ||
        Type == kThornTypePathAnim ||
        Type == kThornTypeFloorHeightAnim ||
        Type == kThornTypeSpatialPropAnim ||
        Type == kThornTypeAttachEntity;
    FConstructorUnwindStage ConstructorUnwind = {Event, bClearCurveOnFailure, false};
    // Each typed constructor overwrites the descriptor's ID with the event's
    // own only after its setter callbacks.
    if (Type == kThornTypeLightPropAnim)
    {
        Event->ApplyLightEventDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeReverbPropAnim)
    {
        Event->ApplyReverbPropAnimDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypePsysPropAnim)
    {
        Event->ApplyPsysPropAnimDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeFogPropAnim)
    {
        Event->ApplyFogPropAnimDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeAudioPropAnim)
    {
        Event->ApplyAudioPropAnimDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeSetCamera)
    {
        Event->ApplySetCameraDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeCameraPropAnim)
    {
        Event->ApplyCameraPropAnimDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeStartSubScene)
    {
        float StartTimeValue = 0.0f;
        std::memcpy(&StartTimeValue, static_cast<const unsigned char*>(Desc) + 44, sizeof(StartTimeValue));
        Event->ApplySubSceneDesc(Desc, StartTimeValue);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeSubtitle)
    {
        Event->ApplySubtitleDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeUserEvent)
    {
        Event->bUserDescriptorOwner = true;
        Event->ApplyUserEventDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeAttachEntity)
    {
        Event->ApplyAttachDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeConnectHardpoints)
        Event->InitializeConnectDesc(Desc);
    else if (Type == kThornTypePathAnim)
    {
        Event->ApplyPathAnimDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeSpatialPropAnim)
    {
        Event->ApplySpatialPropAnimDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeStartMotion)
    {
        Event->ApplyMotionDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeStartIk)
    {
        Event->ApplyStartIkDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeStartPsys)
    {
        Event->ApplyStartPsysDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeStartSound)
    {
        Event->ApplyStartSoundDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeFloorHeightAnim)
    {
        Event->ApplyFloorHeightDesc(Desc);
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else
        Event->ApplyDesc(Desc);


    ConstructorUnwind.bCompleted = true;
}

void FThornEvent::DestroyNative(FThornEvent*& Event)
{
    if (Event == nullptr)
        return;
    // Retail's typed deleting destructors (FUN_06f43a50/FUN_06f43ea0, ...) map
    // to the one destructor, which picks the type-specific string cleanup.
    delete Event;
    Event = nullptr;
}

FThornEvent::~FThornEvent() noexcept(false)
{
    struct FCommonCleanupStage
    {
        FThornEvent* Event;
        ~FCommonCleanupStage() { Event->ParentCsys = nullptr; }
    } CommonCleanup{this};
    const int CleanupType = NativeEventType != 0 ? NativeEventType : Type;
    if (NativeEventType == kThornTypeConnectHardpoints || bConnectStorageActive)
    {
        ReleaseConnectStringStorage();
        return;
    }
    if (NativeEventType == kThornTypeStartMotion || bMotionStorageActive)
    {
        ReleaseMotionOwnedState();
        return;
    }
    if (CleanupType == kThornTypeStartSound || CleanupType == kThornTypeStartSubScene ||
        CleanupType == kThornTypeStartPsys)
        return;

    if (NativeEventType == kThornTypeStartIk || bIkStorageActive)
    {
        // FUN_06f43ec0: target string, end-effector string, then common base.
        // The IK class never releases the copied curve.
        ReleaseIkOwnedState();
        ParentCsys = nullptr;
        return;
    }
    if (&Desc == &FloorDesc.Common)
    {
        // FUN_06f494e0: curve Release, string teardown, then parent clear.
        ReleaseFloorOwnedState();
        ParentCsys = nullptr;
        return;
    }
    if (&Desc == &SetCameraDesc)
    {
        // FUN_06f44e30 -> 06f44e50 -> 06f439a0: clears the borrowed parent; no
        // curve Release.
        ParentCsys = nullptr;
        ParamCurve = nullptr;
        return;
    }
    if (bUserDescriptorOwner)
    {
        // FUN_06f44be0 -> 06f439a0: clears the borrowed parent. USER never
        // releases its copied curve, even on constructor failure.
        ParentCsys = nullptr;
        ParamCurve = nullptr;
        return;
    }
    if (CleanupType == kThornTypeConnectHardpoints || bConnectStorageActive)
        ReleaseConnectStringStorage();
    if (bFloorStorageActive || CleanupType == kThornTypeFloorHeightAnim)
        ReleaseFloorOwnedState();
    else if (bSpatialStorageActive)
        ReleaseSpatialOwnedState();
    else if (&Desc == &CameraDesc.Common || CleanupType == kThornTypeCameraPropAnim)
        ReleaseCameraOwnedState();
    else if (CleanupType == kThornTypeAudioPropAnim)
        ReleaseAudioOwnedState();
    else if (CleanupType == kThornTypeSpatialPropAnim)
        ReleaseSpatialOwnedState();
    else if (bAttachStorageActive || CleanupType == kThornTypeAttachEntity)
        ReleaseAttachOwnedState();
    else if (ParamCurve != nullptr)
    {
        ParamCurve->Release();
        ParamCurve = nullptr;
    }
    delete[] UserEventString;
    UserEventString = nullptr;
    delete[] Animation;
    Animation = nullptr;
    delete[] LockedBone;
    LockedBone = nullptr;
    if (!bConnectStorageActive)
        delete[] Hardpoint;
    Hardpoint = nullptr;
    if (!bConnectStorageActive)
        delete[] ParentHardpoint;
    ParentHardpoint = nullptr;
    if (CleanupType == kThornTypeStartMotion)
        ReleaseMotionOwnedState();
    if (!bSpatialStorageActive && CleanupType == kThornTypeStartIk)
        ReleaseIkOwnedState();
    else
    {
        delete[] EndEffector;
        EndEffector = nullptr;
    }
    if (!bFloorStorageActive && !bSpatialStorageActive && !bAttachStorageActive && CleanupType != kThornTypeStartIk && CleanupType != kThornTypeSpatialPropAnim &&
        CleanupType != kThornTypeAttachEntity)
    {
        delete[] TargetPart;
        TargetPart = nullptr;
    }
    if (bOwnsParentCsys)
        delete ParentCsys;
    ParentCsys = nullptr;
    bOwnsParentCsys = 0;
}

void FThornEvent::ApplyDesc(const void* DescIn)
{
    if (&Desc == &FloorDesc.Common)
    {
        ApplyFloorHeightDesc(DescIn);
        return;
    }
    if (&Desc == &SetCameraDesc)
    {
        ApplySetCameraDesc(DescIn);
        return;
    }
    if (&Desc == &ReverbDesc.Common)
    {
        ApplyReverbPropAnimDesc(DescIn);
        return;
    }
    if (&Desc == &LightDesc.Common)
    {
        ApplyLightEventDesc(DescIn);
        return;
    }
    if (&Desc == &AudioPropDesc.Common)
    {
        ApplyAudioPropAnimDesc(DescIn);
        return;
    }
    // Fallback for types without a typed setter: common record only.
    ApplyCommonDesc(DescIn);
    FinalizeDesc();
}

void FThornEvent::ApplyCommonDesc(const void* DescIn)
{
    // FUN_06f438c0. Retail snapshots the common record before any callback.
    FThornEventDesc Incoming;
    std::memcpy(&Incoming, DescIn, sizeof(Incoming));
    Desc.Type = Incoming.Type;
    Desc.StartMs = Incoming.StartMs;
    Desc.Flags = Incoming.Flags;
    Desc.EventFlags = Incoming.EventFlags;
    Desc.Field10 = Incoming.Field10;
    Desc.UniqueId = Incoming.UniqueId;
    Desc.DurationMs = Incoming.DurationMs;
    Desc.PrimaryIndex = Incoming.PrimaryIndex;
    Desc.SecondaryIndex = Incoming.SecondaryIndex;
    if (ParamCurve != nullptr)
        ParamCurve->Release();
    ParamCurve = Incoming.ParamCurve;
    if (ParamCurve != nullptr)
        ParamCurve->AddRef();
    PcurvePeriod = Incoming.ParamCurvePeriod;
}

void FThornEvent::FinalizeDesc()
{
    // The per-type ApplyDesc tail that follows FUN_06f438c0.
    Type = Desc.Type;
    StartTimeMs = Desc.StartMs;
    PrimaryIndex = Desc.PrimaryIndex;
    SecondaryIndex = Desc.SecondaryIndex;
    // Every retail tail (FUN_06f465f0, FUN_06f48700, ...) sets +0x1c =
    // DurationMs + StartMs unconditionally. USE_SCRIPT_DURATION -> -1 is the
    // Start prologue's job (ApplyEndTime), which START_PSYS alone lacks.
    const unsigned int EndBits = static_cast<unsigned int>(StartTimeMs) +
                                 static_cast<unsigned int>(Desc.DurationMs);
    std::memcpy(&EndTimeMs, &EndBits, sizeof(EndTimeMs));
}

void FThornEvent::ApplyUserEventDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeUserEvent;
    UserEventDesc.ExternalPayloadWord = ReadEventWord(DescIn, 0) == kThornTypeUserEvent
        ? static_cast<unsigned int>(ReadEventWord(DescIn, 11)) : 0u;
    FinalizeDesc();
}

void FThornEvent::ApplySubtitleDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeSubtitle;
    // FUN_06f49d00: a mismatched descriptor keeps the existing subtitle id.
    if (ReadEventWord(DescIn, 0) == kThornTypeSubtitle)
        std::memcpy(&Strid, static_cast<const unsigned char*>(DescIn) + 44, sizeof(Strid));
    FinalizeDesc();
}

void FThornEvent::ApplySubSceneDesc(const void* DescIn, float StartTimeValue)
{
    (void)StartTimeValue;
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeStartSubScene;
    if (ReadEventWord(DescIn, 0) == kThornTypeStartSubScene)
        std::memcpy(&SubSceneStartTime, static_cast<const unsigned char*>(DescIn) + 44, sizeof(SubSceneStartTime));
    else
        SubSceneStartTime = 0.0f;
    FinalizeDesc();
}

void FThornEvent::InitializeConnectDesc(const void* DescIn)
{
    // FUN_06f48050 applies the descriptor twice, both times from the original
    // input (callbacks from the first call may change it). Only the exposed
    // pointers are cleared in between; the string owners survive.
    ApplyConnectDesc(DescIn);
    Hardpoint = nullptr;
    ParentHardpoint = nullptr;
    ApplyConnectDesc(DescIn);
    Desc.UniqueId = UniqueId;
}

void FThornEvent::ApplyConnectDesc(const void* DescIn)
{
    // FUN_06f48190: common eleven words, force CONNECT, two owned strings.
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeConnectHardpoints;
    AdoptConnectStringStorage();
    // Clears the exposed pointers without destroying either owner.
    Hardpoint = nullptr;
    ParentHardpoint = nullptr;
    const bool bMatchingType = ReadEventWord(DescIn, 0) == kThornTypeConnectHardpoints;
    if (bMatchingType)
    {
        const char* HardpointName =
            reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 11)));
        if (HardpointName != nullptr)
            SetConnectHardpoint(HardpointName);
        // The second name is read after the first string operation.
        const char* ParentHardpointName =
            reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 12)));
        if (ParentHardpointName != nullptr)
            SetConnectParentHardpoint(ParentHardpointName);
    }
    FinalizeDesc();
}

void FThornEvent::ApplyAttachDesc(const void* DescIn)
{
    bAttachStorageActive = true;
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeAttachEntity;
    TargetPart = nullptr;
    if (ReadEventWord(DescIn, 0) == kThornTypeAttachEntity)
    {
        std::memcpy(&PathOffset, static_cast<const unsigned char*>(DescIn) + 44, sizeof(PathOffset));
        TargetType = ReadEventWord(DescIn, 14);
        LookAtUpAxis = ReadEventWord(DescIn, 15);
        LookAtFrontAxis = ReadEventWord(DescIn, 16);
        const char* TargetPartName = reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 17)));
        if (TargetPartName != nullptr)
        {
            AttachTargetPartStorage.Assign(TargetPartName);
            TargetPart = AttachTargetPartStorage.GetPublishedData();
        }
    }
    else
    {
        std::memset(&PathOffset, 0, sizeof(PathOffset));
        TargetType = 0;
        LookAtUpAxis = 1;
        LookAtFrontAxis = 5;
    }
    FinalizeDesc();
}

void FThornEvent::ApplyPathAnimDesc(const void* DescIn)
{
    // FUN_06f450a0: common eleven words, force type 6, then dwords 11..18
    // (floats copied as raw bits) or, on a mismatch, the constructor defaults.
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypePathAnim;
    if (ReadEventWord(DescIn, 0) == kThornTypePathAnim)
    {
        std::memcpy(&StartPercent, static_cast<const unsigned char*>(DescIn) + 44, sizeof(StartPercent));
        std::memcpy(&StopPercent, static_cast<const unsigned char*>(DescIn) + 48, sizeof(StopPercent));
        std::memcpy(&PercentDelta, static_cast<const unsigned char*>(DescIn) + 52, sizeof(PercentDelta));
        std::memcpy(&PathOffset, static_cast<const unsigned char*>(DescIn) + 56, sizeof(PathOffset));
        LookAtUpAxis = ReadEventWord(DescIn, 17);
        LookAtFrontAxis = ReadEventWord(DescIn, 18);
    }
    else
    {
        StartPercent = 0.0f;
        StopPercent = 1.0f;
        PercentDelta = 1.0f;
        PathOffset.x = 0.0f;
        PathOffset.y = 0.0f;
        PathOffset.z = 0.0f;
        LookAtUpAxis = 1;
        LookAtFrontAxis = 5;
    }
    FinalizeDesc();
}

void FThornEvent::ApplyLightEventDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    // The type is forced before the input is reread.
    Desc.Type = kThornTypeLightPropAnim;
    if (ReadEventWord(DescIn, 0) == kThornTypeLightPropAnim)
    {
        LightMask = ReadEventWord(DescIn, 11);
        // Forward DWORD copy (REP MOVSD); stays defined if the input aliases
        // the destination.
        unsigned char* Destination = reinterpret_cast<unsigned char*>(&LightTarget);
        const unsigned char* Source = reinterpret_cast<const unsigned char*>(static_cast<const unsigned char*>(DescIn) + 48);
        for (int Index = 0; Index < 0x24; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, Source + Index * sizeof(Word), sizeof(Word));
            std::memcpy(Destination + Index * sizeof(Word), &Word, sizeof(Word));
        }
    }
    else
    {
        // Only the mask is cleared; the previous target survives.
        LightMask = 0;
    }
    FinalizeDesc();
}

void FThornEvent::ApplyMotionDesc(const void* DescIn)
{
    // FUN_06f43af0.
    bMotionStorageActive = true;
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeStartMotion;
    Animation = nullptr;
    LockedBone = nullptr;
    if (ReadEventWord(DescIn, 0) == kThornTypeStartMotion)
    {
        // Every scalar is stored before either string operation.
        std::memcpy(&Heading, static_cast<const unsigned char*>(DescIn) + 68, sizeof(Heading));
        Field_0x40 = static_cast<unsigned char>(ReadEventWord(DescIn, 16));
        std::memcpy(&MotionStartTime, static_cast<const unsigned char*>(DescIn) + 44, sizeof(MotionStartTime));
        std::memcpy(&TimeScale, static_cast<const unsigned char*>(DescIn) + 52, sizeof(TimeScale));
        Field_0x50 = ReadEventWord(DescIn, 20);
        std::memcpy(&TransTime, static_cast<const unsigned char*>(DescIn) + 48, sizeof(TransTime));
        std::memcpy(&TransScale, static_cast<const unsigned char*>(DescIn) + 76, sizeof(TransScale));
        std::memcpy(&Weight, static_cast<const unsigned char*>(DescIn) + 56, sizeof(Weight));
        const char* AnimationName = reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 15)));
        if (AnimationName != nullptr)
        {
            MotionAnimationStorage.Assign(AnimationName);
            Animation = MotionAnimationStorage.GetPublishedData();
        }
        const char* BoneName = reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 18)));
        if (BoneName != nullptr)
        {
            MotionLockedBoneStorage.Assign(BoneName);
            LockedBone = MotionLockedBoneStorage.GetPublishedData();
        }
    }
    else
    {
        MotionStartTime = 0.0f;
        TransTime = 0.0f;
        TimeScale = 1.0f;
        Weight = 1.0f;
        Field_0x40 = 0;
        Heading = -1.0f;
        TransScale = 1.0f;
        Field_0x50 = 0;
    }
    FinalizeDesc();
}

void FThornEvent::ApplySpatialPropAnimDesc(const void* DescIn)
{
    // FUN_06f465f0. Pre-reset: +0x98 axis-rot angle = 0, +0x9c axis id = 1,
    // +0x104 applied rotation = 0; common eleven words; forced type 7; +0x54
    // mask, +0xa0 target-part c_str and +0xa4 target type cleared. Only an
    // input type 7 carries the payload: Desc[11] mask, Desc[12..20] target
    // orientation (+0x58), Desc[21..23] target position (+0x7c), Desc[24..27]
    // quaternion (+0x88) when kThornFlagQuatOrient is set (else derived from
    // the orientation through math +0x50), Desc[28] angle, Desc[29] axis,
    // Desc[30] target part (VC6 basic_string at +0xa8), Desc[31] target type.
    // The Lua loader (FUN_06f25340 case 7) writes the same slots.
    AxisRotAngle = 0.0f;
    AxisRotApplied = 0.0f;
    AxisRotAxis = 1;
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeSpatialPropAnim;
    SpatialMask = 0;
    AdoptSpatialStringStorage();
    TargetPart = nullptr; // Clears the pointer, not the owner.
    TargetType = 0;
    if (ReadEventWord(DescIn, 0) != kThornTypeSpatialPropAnim)
    {
        FinalizeDesc();
        return;
    }
    SpatialMask = ReadEventWord(DescIn, 11);
    const bool bQuaternionOrientation = (Desc.Flags & kThornFlagQuatOrient) != 0;
    // Forward word copy; stays defined if the input aliases this storage.
    for (int Index = 0; Index < 9; ++Index)
        std::memcpy(&TargetOrient.E[0][0] + Index, static_cast<const unsigned char*>(DescIn) + 48 + Index * 4, 4);
    for (int Index = 0; Index < 3; ++Index)
        std::memcpy(&TargetPos.x + Index, static_cast<const unsigned char*>(DescIn) + 84 + Index * 4, 4);
    if (bQuaternionOrientation)
    {
        for (int Index = 0; Index < 4; ++Index)
            std::memcpy(reinterpret_cast<unsigned char*>(&TargetQuat) + Index * 4,
                        static_cast<const unsigned char*>(DescIn) + 96 + Index * 4, 4);
    }
    else
    {
        // Converts the descriptor's matrix, read after the engine is acquired,
        // not the TargetOrient copy.
        if (G_SceneEventMathEngine == nullptr)
            G_SceneEventMathEngine = ThornAcquire3DMathEngine();
        SpatialMatrixToQuaternion(G_SceneEventMathEngine, TargetQuat,
            static_cast<const unsigned char*>(DescIn) + 48);
    }
    const char* TargetPartName = reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 30)));
    // TargetPart was cleared before the math callback; a null name skips the
    // assignment entirely.
    if (TargetPartName != nullptr)
    {
        SpatialTargetPartStorage.Assign(TargetPartName);
        TargetPart = SpatialTargetPartStorage.GetPublishedData();
    }
    // Read only after the string call, as retail does.
    TargetType = ReadEventWord(DescIn, 31);
    std::memcpy(&AxisRotAngle, static_cast<const unsigned char*>(DescIn) + 112, sizeof(AxisRotAngle));
    AxisRotAxis = ReadEventWord(DescIn, 29);
    FinalizeDesc();
}

void FThornEvent::ApplyStartSoundDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeStartSound;
    // The input is reread after the common callbacks and the forced type.
    SoundStartTime = ReadEventWord(DescIn, 0) == kThornTypeStartSound ? ReadEventWord(DescIn, 11) : 0;
    FinalizeDesc();
}

void FThornEvent::ApplySetCameraDesc(const void* DescIn)
{
    // FUN_06f44e60: common record, force type 2, common tail.
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeSetCamera;
    FinalizeDesc();
}

void FThornEvent::ApplyCameraPropAnimDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeCameraPropAnim;
    // The input type is read after the common curve callbacks.
    if (ReadEventWord(DescIn, 0) == kThornTypeCameraPropAnim)
    {
        CamMask = ReadEventWord(DescIn, 11);
        for (int Index = 0; Index < 4; ++Index)
            std::memcpy(CamTarget + Index, static_cast<const unsigned char*>(DescIn) + 48 + Index * 4, 4);
    }
    else
        CamMask = 0; // The four target words are kept on a mismatch.
    FinalizeDesc();
}

void FThornEvent::ApplyStartPsysDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeStartPsys;
    FinalizeDesc();
}

void FThornEvent::ApplyPsysPropAnimDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypePsysPropAnim;
    // The input is reread after the common callbacks and the forced type.
    if (ReadEventWord(DescIn, 0) == kThornTypePsysPropAnim)
    {
        PsysMask = ReadEventWord(DescIn, 11);
        // Raw DWORD read before its store.
        unsigned int TargetBits;
        std::memcpy(&TargetBits, static_cast<const unsigned char*>(DescIn) + 48, sizeof(TargetBits));
        std::memcpy(&SparamTarget, &TargetBits, sizeof(TargetBits));
    }
    else
    {
        PsysMask = 0;
        const unsigned int ZeroBits = 0;
        std::memcpy(&SparamTarget, &ZeroBits, sizeof(SparamTarget));
    }
    FinalizeDesc();
}

void FThornEvent::ApplyFogPropAnimDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeFogPropAnim;
    // The input type is read after the common curve callbacks.
    if (ReadEventWord(DescIn, 0) == kThornTypeFogPropAnim)
    {
        FogMask = ReadEventWord(DescIn, 11);
        // Forward DWORD copy, flag bytes and padding included.
        for (int Index = 0; Index < 8; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, static_cast<const unsigned char*>(DescIn) + 48 + Index * 4, sizeof(Word));
            std::memcpy(reinterpret_cast<unsigned char*>(&FogTarget) + Index * 4, &Word, sizeof(Word));
        }
    }
    else
        FogMask = 0;
    FinalizeDesc();
}

void FThornEvent::ApplyReverbPropAnimDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeReverbPropAnim;
    // Type reread after the callbacks; four raw words copied in order.
    if (ReadEventWord(DescIn, 0) == kThornTypeReverbPropAnim)
    {
        ReverbMask = ReadEventWord(DescIn, 11);
        for (int Index = 0; Index < 4; ++Index)
            std::memcpy(reinterpret_cast<unsigned char*>(&ReverbTarget) + Index * 4,
                        static_cast<const unsigned char*>(DescIn) + 48 + Index * 4, 4);
    }
    else
        ReverbMask = 0;
    FinalizeDesc();
}

void FThornEvent::ApplyAudioPropAnimDesc(const void* DescIn)
{
    // Raw defaults, written before the common curve callbacks.
    AudioTarget.Ain = 0x168;
    AudioTarget.Aout = 0x168;
    const unsigned int ZeroBits = 0;
    const unsigned int MinBits = 0x42480000;
    const unsigned int MaxBits = 0x447a0000;
    std::memcpy(&AudioTarget.Attenuation, &ZeroBits, 4);
    std::memcpy(&AudioTarget.Pan, &ZeroBits, 4);
    std::memcpy(&AudioTarget.Dmin, &MinBits, 4);
    std::memcpy(&AudioTarget.Dmax, &MaxBits, 4);
    std::memcpy(&AudioTarget.Atout, &ZeroBits, 4);
    std::memcpy(&AudioTarget.Rmix, &ZeroBits, 4);
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeAudioPropAnim;
    // Type reread after the callbacks; a mismatch clears only the mask.
    if (ReadEventWord(DescIn, 0) == kThornTypeAudioPropAnim)
    {
        AudioMask = ReadEventWord(DescIn, 11);
        // Forward DWORD copy; defined for overlapping input.
        for (int Index = 0; Index < 8; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, static_cast<const unsigned char*>(DescIn) + 48 + Index * 4, 4);
            std::memcpy(reinterpret_cast<unsigned char*>(&AudioTarget) + Index * 4, &Word, 4);
        }
    }
    else
        AudioMask = 0;
    FinalizeDesc();
}

void FThornEvent::ReleaseAudioOwnedState()
{
    // FUN_06f47230: releases the +0x4c curve (ParamCurve) before the common
    // event cleanup.
    if (ParamCurve != nullptr)
    {
        ParamCurve->Release();
        ParamCurve = nullptr;
    }
}

void FThornEvent::ReleaseCameraOwnedState()
{
    // FUN_06f46010: releases the +0x4c curve (ParamCurve), then FUN_06f439a0.
    if (ParamCurve != nullptr)
    {
        ParamCurve->Release();
        ParamCurve = nullptr;
    }
}

void FThornEvent::ReleaseSpatialOwnedState()
{
    // FUN_06f46550: releases the +0x4c curve, then frees the ref-counted
    // target-part string at +0xac and clears its three words.
    AdoptSpatialStringStorage();
    try
    {
        if (ParamCurve != nullptr)
        {
            ParamCurve->Release();
        }
    }
    catch (...)
    {
        // Retail's unwind also frees the string if the curve Release throws.
        SpatialTargetPartStorage.Reset();
        throw;
    }
    SpatialTargetPartStorage.Reset();
    // As in retail, TargetPart is left stale; nothing reads it afterwards.
}

void FThornEvent::ReleaseAttachOwnedState()
{
    try
    {
        if (ParamCurve != nullptr)
            ParamCurve->Release();
    }
    catch (...)
    {
        // Retail's unwind also frees the string if the curve Release throws.
        AttachTargetPartStorage.Reset();
        throw;
    }
    AttachTargetPartStorage.Reset();
}

void FThornEvent::ReleaseIkOwnedState()
{
    // FUN_06f43ec0 leaves the exposed descriptor pointers set.
    IkTargetPartStorage.Reset();
    IkEndEffectorStorage.Reset();
}

void FThornEvent::SetIkEndEffector(const char* Name)
{
    bIkStorageActive = true;
    EndEffector = nullptr;
    if (Name != nullptr)
    {
        IkEndEffectorStorage.Assign(Name);
        EndEffector = IkEndEffectorStorage.GetPublishedData();
    }
}

void FThornEvent::SetIkTargetPart(const char* Name)
{
    bIkStorageActive = true;
    TargetPart = nullptr;
    if (Name != nullptr)
    {
        IkTargetPartStorage.Assign(Name);
        TargetPart = IkTargetPartStorage.GetPublishedData();
    }
}

void FThornEvent::ReleaseMotionOwnedState()
{
    // FUN_06f43a70: locked bone, animation, then common base; no curve Release.
    MotionLockedBoneStorage.Reset();
    MotionAnimationStorage.Reset();
}

#pragma warning(push)
#pragma warning(disable: 4716) // Preserve the retail extended ST(0) return.
#pragma float_control(precise, off, push) // Keep the complete curve FMUL/FADD result in ST(0).
float FThornEvent::EvaluateControlBackedCurve(float Scale, int TimeMs)
{
    // FUN_06f454f0. TEST AH,44h / JNP takes only ordered equality (equal 40h
    // has odd parity; unordered 44h even).
    const float Zero = 0.0f;
    const float One = 1.0f;
    bool bScaleZero;
    __asm
    {
        fld Scale
        fcomp Zero
        fnstsw ax
        test ah, 44h
        setnp al
        mov bScaleZero, al
    }
    if (bScaleZero)
    {
        __asm { fld One }
    }
    else
    {
        const unsigned int ElapsedBits = static_cast<unsigned int>(TimeMs) -
                                         static_cast<unsigned int>(Desc.StartMs);
        const long long ExtendedElapsed = ElapsedBits;
        const int Duration = Desc.DurationMs;
        IParamCurve* Curve = ParamCurve;
        float Ratio;
        // The ratio is always computed and stored, even on the wrap path. FST
        // does not pop: the unbound path uses the extended ST(0).
        __asm
        {
            fild qword ptr ExtendedElapsed
            fidiv Duration
            fst Ratio
        }
        if (Curve != nullptr)
        {
            const int Period = PcurvePeriod;
            __asm { fstp st(0) }
            // StartPercent is read only after the callback.
            if (Period > 0)
                return Curve->WrapEvaluate(static_cast<int>(ElapsedBits), Period) * Scale + StartPercent;
            return Curve->Evaluate(Ratio) * Scale + StartPercent;
        }
        // The unbound quotient stays extended through both operations.
        const float* StartPercentValue = &StartPercent;
        __asm
        {
            fmul Scale
            mov edx, StartPercentValue
            fadd dword ptr [edx]
        }
    }
}
#pragma float_control(pop)
#pragma warning(pop)

namespace
{
    int GetIkDescriptorType(const FThornIkEventDesc* Descriptor)
    {
        return Descriptor->Desc[0];
    }

    int GetIkDescriptorType(const FThornStartIkDesc* Descriptor)
    {
        return Descriptor->Common.Type;
    }

    template <typename FDescriptor>
    void ApplyIkPayload(FThornEvent* Event, const FDescriptor* Descriptor)
    {
        Event->Desc.Type = kThornTypeStartIk;
        Event->bIkStorageActive = true;
        // Cleared before the type gate and either string assignment.
        Event->EndEffector = nullptr;
        Event->TargetPart = nullptr;
        if (GetIkDescriptorType(Descriptor) == kThornTypeStartIk)
        {
            Event->CountToRoot = Descriptor->CountToRoot;
            Event->LookAtUpAxis = Descriptor->LookAtUpAxis;
            Event->LookAtFrontAxis = Descriptor->LookAtFrontAxis;
            CopyPoseWordsForward(&Event->Damping, &Descriptor->Damping, 1);
            Event->bPointAt = Descriptor->bPointAt;
            Event->bMoveTo = Descriptor->bMoveTo;
            CopyPoseWordsForward(&Event->TransitionDuration, &Descriptor->TransitionDuration, 1);
            if (Descriptor->EndEffector != nullptr)
            {
                Event->IkEndEffectorStorage.Assign(Descriptor->EndEffector);
                Event->EndEffector = Event->IkEndEffectorStorage.GetPublishedData();
            }
            if (Descriptor->TargetPart != nullptr)
            {
                Event->IkTargetPartStorage.Assign(Descriptor->TargetPart);
                Event->TargetPart = Event->IkTargetPartStorage.GetPublishedData();
            }
            Event->TargetType = Descriptor->TargetType;
        }
        else
        {
            Event->CountToRoot = 0;
            Event->LookAtUpAxis = 1;
            Event->LookAtFrontAxis = 5;
            Event->Damping = 1.0f;
            Event->bPointAt = 0;
            Event->bMoveTo = 0;
            Event->TransitionDuration = 1.0f;
            Event->TargetType = 0;
        }
        Event->FinalizeDesc();
    }
}

void FThornEvent::ApplyIkExtendedDesc(const FThornIkEventDesc* DescIn)
{
    ApplyCommonDesc(DescIn->Desc);
    ApplyIkPayload(this, DescIn);
}

void FThornEvent::ApplyStartIkDesc(const void* DescIn)
{
    // FThornStartIkDesc has Key before CountToRoot; FThornIkEventDesc (the Lua
    // parser's) has none. The payload is read after the common callback.
    const FThornStartIkDesc* Descriptor = static_cast<const FThornStartIkDesc*>(DescIn);
    ApplyCommonDesc(&Descriptor->Common);
    ApplyIkPayload(this, Descriptor);
}

static FThornPathEventDesc* GetPathEventDesc(FThornEvent* Event)
{
    // LAB_06f4a4b0: the live PATH descriptor.
    return Event != nullptr ? &Event->PathDesc : nullptr;
}

void* FThornEvent::GetDesc()
{
    if (&Desc == &PathDesc.Common)
        return GetPathEventDesc(this);
    if (&Desc == &FloorDesc.Common)
        return &FloorDesc.Common;
    if (&Desc == &SetCameraDesc)
        return &SetCameraDesc;
    // Live descriptor, even if Type has changed since (LAB_06f4a6d0).
    if (&Desc == &CameraDesc.Common)
        return &CameraDesc.Common;
    if (&Desc == &ReverbDesc.Common)
        return &ReverbDesc.Common;
    if (&Desc == &LightDesc.Common)
        return &LightDesc.Common;
    if (&Desc == &AudioPropDesc.Common)
        return &AudioPropDesc.Common;
    if (Type == kThornTypeStartSubScene)
    {
        std::memcpy(&SubSceneDesc.Common, &Desc, sizeof(SubSceneDesc.Common));
        std::memcpy(&SubSceneDesc.StartTime, &SubSceneStartTime, sizeof(SubSceneStartTime));
        return &SubSceneDesc.Common;
    }
    if (Type == kThornTypeAttachEntity)
    {
        PackAttachDesc();
        return &AttachDesc.Common;
    }
    if (Type == kThornTypeUserEvent)
    {
        return &UserEventDesc.Common;
    }
    if (Type == kThornTypeStartSound)
    {
        PackSoundDesc();
        return &SoundDesc.Common;
    }
    // LAB_06f4a470: event +0x28. For START_MOTION the retail descriptor continues past the
    // common eleven words (FUN_06f43af0 layout); hand out the packed copy.
    if (Type == kThornTypeStartMotion)
    {
        PackMotionDesc();
        return &MotionDesc.Common;
    }
    // START_IK's descriptor continues the same way (FUN_06f43f40 layout).
    if (Type == kThornTypeStartIk)
    {
        PackIkDesc();
        return &IkDesc.Common;
    }
    // CONNECT_HARDPOINTS' descriptor continues with the two hardpoint names.
    if (Type == kThornTypeConnectHardpoints)
    {
        PackConnectDesc();
        return &ConnectDesc.Common;
    }
    if (Type == kThornTypeSpatialPropAnim)
    {
        std::memcpy(&SpatialDesc.Common, &Desc, sizeof(SpatialDesc.Common));
        SpatialDesc.Mask = SpatialMask;
        std::memcpy(&SpatialDesc.Orient, &TargetOrient, sizeof(TargetOrient));
        std::memcpy(&SpatialDesc.Position, &TargetPos, sizeof(TargetPos));
        std::memcpy(&SpatialDesc.Quaternion, &TargetQuat, sizeof(TargetQuat));
        std::memcpy(&SpatialDesc.AxisRotAngle, &AxisRotAngle, sizeof(AxisRotAngle));
        SpatialDesc.AxisRotAxis = AxisRotAxis;
        SpatialDesc.TargetPart = TargetPart;
        SpatialDesc.TargetType = TargetType;
        return &SpatialDesc.Common;
    }
    if (Type == kThornTypeReverbPropAnim)
        return &ReverbDesc.Common;
    if (Type == kThornTypeAudioPropAnim)
    {
        return &AudioPropDesc.Common;
    }
    if (Type == kThornTypePsysPropAnim)
    {
        // The live 13-word packet, not repacked (LAB_06f4a670).
        return &PsysPropDesc.Common;
    }
    if (Type == kThornTypeFogPropAnim)
    {
        return &FogDesc.Common;
    }
    if (Type == kThornTypeSubtitle)
    {
        std::memcpy(&SubtitleDesc.Common, &Desc, sizeof(SubtitleDesc.Common));
        std::memcpy(&SubtitleDesc.Strid, &Strid, sizeof(Strid));
        return &SubtitleDesc.Common;
    }
    return &Desc;
}

void FThornEvent::PackAttachDesc()
{
    std::memcpy(&AttachDesc.Common, &Desc, sizeof(AttachDesc.Common));
    std::memcpy(&AttachDesc.Offset, &PathOffset, sizeof(AttachDesc.Offset));
    AttachDesc.TargetType = TargetType;
    AttachDesc.UpAxis = LookAtUpAxis;
    AttachDesc.FrontAxis = LookAtFrontAxis;
    AttachDesc.TargetPart = TargetPart;
}

void FThornEvent::PackSoundDesc()
{
    std::memcpy(&SoundDesc.Common, &Desc, sizeof(SoundDesc.Common));
    SoundDesc.SoundStartTime = SoundStartTime;
}

void FThornEvent::PackConnectDesc()
{
    // Event +0x28: the common words, then hardpoint (+0x54 = desc +0x2c) and parent_hardpoint
    // (+0x58 = desc +0x30), the name pointers themselves (not copies).
    std::memcpy(&ConnectDesc.Common, &Desc, sizeof(ConnectDesc.Common));
    ConnectDesc.Hardpoint = Hardpoint;
    ConnectDesc.ParentHardpoint = ParentHardpoint;
}

void FThornEvent::PackIkDesc()
{
    // FUN_06f43f40 (thorn_iface.h FThornStartIkDesc): the common words, then the event's own
    // address at +0x2c, count_to_root +0x30, up +0x34, front +0x38, damping +0x3c,
    // point_at +0x40, move_to +0x41, transition_duration +0x44, the two name strings' c_str at
    // +0x48 / +0x4c (null without a name) and target_type +0x50.
    std::memcpy(&IkDesc.Common, &Desc, sizeof(IkDesc.Common));
    IkDesc.Key = this;
    IkDesc.CountToRoot = CountToRoot;
    IkDesc.LookAtUpAxis = LookAtUpAxis;
    IkDesc.LookAtFrontAxis = LookAtFrontAxis;
    std::memcpy(&IkDesc.Damping, &Damping, sizeof(Damping));
    IkDesc.bPointAt = bPointAt;
    IkDesc.bMoveTo = bMoveTo;
    IkDesc.Pad42[0] = 0;
    IkDesc.Pad42[1] = 0;
    std::memcpy(&IkDesc.TransitionDuration, &TransitionDuration, sizeof(TransitionDuration));
    IkDesc.EndEffector = EndEffector;
    IkDesc.TargetPart = TargetPart;
    IkDesc.TargetType = TargetType;
}

void FThornEvent::PackMotionDesc()
{
    // FUN_06f43af0's layout (thorn_iface.h FThornMotionEventDesc): the common words, then
    // start_time +0x2c, trans_time +0x30, time_scale +0x34, weight +0x38, animation +0x3c,
    // the byte +0x40, heading +0x44, locked_bone +0x48, trans_scale +0x4c, +0x50.
    std::memcpy(&MotionDesc.Common, &Desc, sizeof(MotionDesc.Common));
    std::memcpy(&MotionDesc.StartTime, &MotionStartTime, sizeof(MotionStartTime));
    std::memcpy(&MotionDesc.TransTime, &TransTime, sizeof(TransTime));
    std::memcpy(&MotionDesc.TimeScale, &TimeScale, sizeof(TimeScale));
    std::memcpy(&MotionDesc.Weight, &Weight, sizeof(Weight));
    MotionDesc.Animation = Animation;
    // Field40/Field50: meaning unknown, copied through.
    MotionDesc.Field40 = Field_0x40;
    MotionDesc.Pad41[0] = 0;
    MotionDesc.Pad41[1] = 0;
    MotionDesc.Pad41[2] = 0;
    std::memcpy(&MotionDesc.Heading, &Heading, sizeof(Heading));
    MotionDesc.LockedBone = LockedBone;
    std::memcpy(&MotionDesc.TransScale, &TransScale, sizeof(TransScale));
    MotionDesc.Field50 = Field_0x50;
}

#pragma warning(push)
#pragma warning(disable: 4716) // The retail result is returned explicitly in ST(0).
float FThornEvent::EvaluateProgress(int TimeMs) const
{
    // LAB_06f4a450: USER_EVENT's progress is +0.
    if (bUserDescriptorOwner)
        return 0.0f;
    // LAB_06f4a490: SET MONITOR's progress is +0, no curve call.
    if (&Desc == &SetCameraDesc)
        return 0.0f;
    // FUN_06f49b90: the unbound quotient stays extended.
    const int Duration = Desc.DurationMs;
    const float One = 1.0f;
    if (Duration == 0)
    {
        __asm { fld One }
    }
    else
    {
        const unsigned int ElapsedBits = static_cast<unsigned int>(TimeMs) -
                                          static_cast<unsigned int>(Desc.StartMs);
        const long long ExtendedElapsed = ElapsedBits;
        IParamCurve* Curve = ParamCurve;
        if (Curve != nullptr)
        {
            const int Period = PcurvePeriod;
            if (Period > 0)
            {
                return Curve->WrapEvaluate(static_cast<int>(ElapsedBits), Period);
            }
            else
            {
                float Ratio;
                __asm {
                    fild qword ptr ExtendedElapsed
                    fidiv Duration
                    fstp Ratio
                }
                return Curve->Evaluate(Ratio);
            }
        }
        else
        {
            __asm
            {
                fild qword ptr ExtendedElapsed
                fidiv Duration
            }
        }
    }
}
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable: 4716) // Return the reloaded ratio or curve's original ST(0).
float FThornEvent::EvaluateProgressNoPeriodWrap(int TimeMs) const
{
    // FUN_06f493e0: unlike EvaluateProgress, this copy always stores the
    // quotient as float, also on the unbound path.
    const int Duration = Desc.DurationMs;
    const float One = 1.0f;
    if (Duration == 0)
    {
        __asm { fld One }
    }
    else
    {
        const unsigned int ElapsedBits = static_cast<unsigned int>(TimeMs) -
                                          static_cast<unsigned int>(Desc.StartMs);
        const long long ExtendedElapsed = ElapsedBits;
        IParamCurve* Curve = ParamCurve;
        float Ratio;
        __asm
        {
            fild qword ptr ExtendedElapsed
            fidiv Duration
            fstp Ratio
        }
        if (Curve != nullptr)
        {
            return Curve->Evaluate(Ratio);
        }
        else
        {
            __asm { fld Ratio }
        }
    }
}
#pragma warning(pop)

void FThornEvent::EvaluateProgressExtended(int TimeMs, FThornExtendedProgress* Result,
                                           bool bNoPeriodWrap) const
{
    const int Duration = Desc.DurationMs;
    if (Duration == 0)
    {
        const float One = 1.0f;
        __asm
        {
            fld One
            mov edx, Result
            fstp tbyte ptr [edx]
        }
        return;
    }
    IParamCurve* Curve = ParamCurve;
    const unsigned int ElapsedBits = static_cast<unsigned int>(TimeMs) -
                                      static_cast<unsigned int>(Desc.StartMs);
    const long long ExtendedElapsed = ElapsedBits;
    if (!bNoPeriodWrap && Curve != nullptr)
    {
        const int Period = PcurvePeriod;
        if (Period > 0)
        {
            // Keeps the wrap-evaluate result in 80 bits.
            Curve->WrapEvaluateToExtended(static_cast<int>(ElapsedBits), Period, Result);
            return;
        }
    }
    if (!bNoPeriodWrap && Curve == nullptr)
    {
        __asm
        {
            fild qword ptr ExtendedElapsed
            fidiv Duration
            mov edx, Result
            fstp tbyte ptr [edx]
        }
        return;
    }
    // The Evaluate branches take a float argument; NoPeriodWrap also rounds
    // to float on the unbound path (FUN_06f493e0).
    float Ratio;
    __asm
    {
        fild qword ptr ExtendedElapsed
        fidiv Duration
        fstp Ratio
    }
    if (Curve != nullptr)
    {
        // The curve writes its 80-bit result directly.
        Curve->EvaluateToExtended(&Ratio, Result);
    }
    else
    {
        __asm
        {
            fld Ratio
            mov edx, Result
            fstp tbyte ptr [edx]
        }
    }
}

const char* FThornEvent::TypeName() const
{
    // Retail event vtables (0x06f5b1cc .. 0x06f5b40c, 0x20 stride) have a
    // slot-7 getter returning a literal:
    //   LAB_06f4a3d0 "MOTION"            LAB_06f4a400 "IK"
    //   LAB_06f4a430 "SOUND"             LAB_06f4a460 "USER_EVENT"
    //   LAB_06f4a4a0 "SET MONITOR"       LAB_06f4a4d0 "PATH MOTION"
    //   LAB_06f4a500 "LIGHT PROP ANIM"   LAB_06f4a530 "CAMERA PROP ANIM"
    //   LAB_06f4a590 "SPATIAL PROP ANIM" LAB_06f4a560 "AUDIO PROP ANIM"
    //   LAB_06f4a5d0 "ATTACH"            LAB_06f4a600 "CONNECT"
    //   LAB_06f4a630 "START SUB-SCENE"   LAB_06f4a660 "START PSYS"
    //   LAB_06f4a690 "PSYS PROP ANIM"    LAB_06f4a6c0 "FOG PROP ANIM"
    //   LAB_06f4a6f0 "REVERB PROP ANIM"  LAB_06f4a720 "SPATIAL PROP ANIM"
    //   LAB_06f4a750 "SUBTITLE"
    // Type 18 (floor-height) reuses the "SPATIAL PROP ANIM" literal in retail.
    switch (NativeEventType != 0 ? NativeEventType : Type)
    {
    case kThornTypeUserEvent:         return "USER_EVENT";
    case kThornTypeSetCamera:         return "SET MONITOR";
    case kThornTypeStartSound:        return "SOUND";
    case kThornTypeLightPropAnim:     return "LIGHT PROP ANIM";
    case kThornTypeCameraPropAnim:    return "CAMERA PROP ANIM";
    case kThornTypePathAnim:          return "PATH MOTION";
    case kThornTypeSpatialPropAnim:   return "SPATIAL PROP ANIM";
    case kThornTypeAttachEntity:      return "ATTACH";
    case kThornTypeConnectHardpoints: return "CONNECT";
    case kThornTypeStartMotion:       return "MOTION";
    case kThornTypeStartIk:           return "IK";
    case kThornTypeStartSubScene:     return "START SUB-SCENE";
    case kThornTypeStartPsys:         return "START PSYS";
    case kThornTypePsysPropAnim:      return "PSYS PROP ANIM";
    case kThornTypeAudioPropAnim:     return "AUDIO PROP ANIM";
    case kThornTypeFogPropAnim:       return "FOG PROP ANIM";
    case kThornTypeReverbPropAnim:    return "REVERB PROP ANIM";
    case kThornTypeFloorHeightAnim:   return "SPATIAL PROP ANIM";
    case kThornTypeSubtitle:          return "SUBTITLE";
    default:
        // Unreachable in retail: every concrete event vtable has a getter.
        return "";
    }
}

unsigned char FThornEvent::StartSetCamera(FThornScene* Scene)
{
    if (Scene == nullptr)
        return 0;
    FThornEntity* DestEnt = Scene->EntityAt(PrimaryIndex);
    FThornEntity* SourceEnt = nullptr;
    if (SecondaryIndex >= 0)
        SourceEnt = Scene->EntityAt(SecondaryIndex);
    // LAB_06f44ec0: a failed FUN_06f4aa40 lookup returns the scene sentinel;
    // here it is nullptr.
    if (DestEnt == nullptr || SourceEnt == nullptr)
        return 0;

    if (AddMonitorLink(SourceEnt, DestEnt) != 0)
    {
        UnbindMonitorChildren(DestEnt);
        AddMonitorLink(DestEnt, SourceEnt);
    }

    float Props[4];
    CaptureCameraProps(SourceEnt, Props);
    ApplyCameraProps(DestEnt, Props);

    // Retail calls the source's live pose getters (vtable +0x14/+0x18), not a
    // cached descriptor copy.
    Vector Position;
    Matrix Orient;
    GetLivePose(SourceEnt, &Position, &Orient);
    DestEnt->InnerSetPos(&Position);
    DestEnt->InnerSetOrient(&Orient);
    return 0;
}

unsigned char FThornEvent::StartCameraPropAnim(FThornScene* Scene)
{
    // Only a CAMERA (3) primary is captured; otherwise the previous capture stays.
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    if (Primary != nullptr && Primary->Snap.Type == kThornEntityCamera)
        CaptureCameraProps(Primary, CamCapture);
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::StartSpatialPropAnim(FThornScene* Scene)
{
    // LAB_06f46910: the entity is reread from its entry before each getter.
    AxisRotApplied = 0.0f;
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr)
    {
        FThornEntity* Primary = *PrimaryEntry;
        GetRetailRelativePosition(Primary, &Primary->Snap.Pos);
        CopyPoseWordsForward(&CapturePos, &Primary->Snap.Pos);
        Primary = *PrimaryEntry;
        GetRetailRelativeOrientation(Primary, &Primary->Snap.Orient);
        CopyPoseWordsForward(&CaptureOrient, &Primary->Snap.Orient);
        Primary = *PrimaryEntry;
        GetRetailRelativeOrientation(Primary, &Primary->Snap.Orient);
        // The third getter's result pointer is kept across the engine acquire.
        const Matrix* QuaternionMatrix = &Primary->Snap.Orient;
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        SpatialMatrixToQuaternion(Math, CaptureQuat, QuaternionMatrix);
    }
    // The secondary ID is read after the primary callbacks.
    if (SecondaryIndex != -1)
    {
        FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
        if (SecondaryEntry != nullptr)
        {
            FThornEntity* Secondary = *SecondaryEntry;
            GetRetailRelativePosition(Secondary, &Secondary->Snap.Pos);
            CopyPoseWordsForward(&TargetPos, &Secondary->Snap.Pos);
        }
    }
    SpatialWrapLast = 0.0f;
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::StartAttachEntity(FThornScene* Scene)
{
    // LAB_06f478f0: an OR AL,80h makes the apparent flags gate unconditional.
    ApplyEndTime(this);
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry == nullptr)
        return 1;
    FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
    if (SecondaryEntry == nullptr)
        return 1;

    Transform Child;
    IdentityOrient(&Child.Rotation);
    Child.Translation.z = 0.0f;
    Child.Translation.y = 0.0f;
    Child.Translation.x = 0.0f;
    FThornEntity* Secondary = *SecondaryEntry;
    GetRetailRelativePosition(Secondary, &Secondary->Snap.Pos);
    CopyPoseWordsForward(&Child.Translation, &Secondary->Snap.Pos);
    Secondary = *SecondaryEntry;
    GetRetailRelativeOrientation(Secondary, &Secondary->Snap.Orient);
    CopyPoseWordsForward(&Child.Rotation, &Secondary->Snap.Orient);
    if (TargetPart != nullptr && TargetType != 0)
    {
        const int SecondaryType = (*SecondaryEntry)->Snap.Type;
        if (SecondaryType == kThornEntityCompound || SecondaryType == kThornEntityDeformable)
        {
            ICompoundControl* Compound = (*SecondaryEntry)->CompoundControl;
            if (Compound != nullptr)
            {
                Compound->GetHardpointXform(TargetType, TargetPart, &Child);
                const FThornCsys* Parent = ParentCsys;
                if (Parent != nullptr)
                    MulXformByInverseCsys(Parent, &Child);
            }
        }
    }

    Transform PrimaryXform;
    IdentityOrient(&PrimaryXform.Rotation);
    PrimaryXform.Translation.z = 0.0f;
    PrimaryXform.Translation.y = 0.0f;
    PrimaryXform.Translation.x = 0.0f;
    FThornEntity* Primary = *PrimaryEntry;
    GetRetailRelativePosition(Primary, &Primary->Snap.Pos);
    CopyPoseWordsForward(&PrimaryXform.Translation, &Primary->Snap.Pos);
    Primary = *PrimaryEntry;
    GetRetailRelativeOrientation(Primary, &Primary->Snap.Orient);
    CopyPoseWordsForward(&PrimaryXform.Rotation, &Primary->Snap.Orient);

    // The child is read only after the math engine is acquired.
    Vector TransformedPosition;
    I3DMathEngine* Math = AcquireSceneEventMathEngine();
    Matrix ChildOrientation;
    Vector ChildPosition;
    Vector ChildInversePosition;
    std::memcpy(&ChildOrientation, &Child.Rotation, sizeof(ChildOrientation));
    std::memcpy(&ChildPosition, &Child.Translation, sizeof(ChildPosition));
    Math->TransposeTransform(ChildInversePosition, ChildOrientation, ChildPosition);
    std::memcpy(&TransformedPosition, &ChildInversePosition, sizeof(TransformedPosition));
    Vector NegativePosition;
    const Vector* PositionSource = &TransformedPosition;
    Vector* PositionResult = &NegativePosition;
    // Each Z/Y/X word is stored right after its FCHS.
    __asm {
        mov ecx, PositionSource
        mov eax, PositionResult
        fld dword ptr [ecx+8]
        fchs
        fstp dword ptr [eax+8]
        fld dword ptr [ecx+4]
        fchs
        fstp dword ptr [eax+4]
        fld dword ptr [ecx]
        fchs
        fstp dword ptr [eax]
    }
    // Raw DWORD transpose (via FUN_06f428b0); unlike FUN_06f3b670 there is no
    // x87 load/store of the matrix words.
    Transform InverseChild;
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Column = 0; Column < 3; ++Column)
        {
            unsigned int Word;
            std::memcpy(&Word, &Child.Rotation.E[Column][Row], sizeof(Word));
            std::memcpy(&InverseChild.Rotation.E[Row][Column], &Word, sizeof(Word));
        }
    }
    PackCsysWords(&InverseChild, &InverseChild, &NegativePosition);
    Transform Result;
    Math = AcquireSceneEventMathEngine();
    SceneMathMulTransform(Math, &Result, &InverseChild, &PrimaryXform);
    std::memcpy(&AttachXform, &Result, sizeof(Result));
    return 1;
}

// LAB_06f48d40: signed key lookup, raw GET capture.
unsigned char FThornEvent::StartFogPropAnim(FThornScene* Scene)
{
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr && (*PrimaryEntry)->Snap.Type == kThornEntityScene)
    {
        // The SCENE type getter (LAB_06f4a300) is a plain field read.
        FThornEntity* Primary = *PrimaryEntry;
        IFogControl* Control = Primary->FogControl;
        if (Control != nullptr)
            Control->GetPacked(reinterpret_cast<unsigned char*>(&Primary->LiveSceneBlob[7]));
        // Retail keeps the entity pointer captured before the GET.
        std::memcpy(&FogCapturePacked, &Primary->LiveSceneBlob[7], sizeof(FogCapturePacked));
    }
    ApplyEndTime(this);
    return 1;
}

// FUN_06f48de0: FOG tick.
unsigned char FThornEvent::TickFogPropAnim(int TimeMs, FThornScene* Scene)
{
    if (FogMask != 0)
    {
        FThornFog8 Target;
        std::memcpy(&Target, &FogTarget, sizeof(Target));
        FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
        if (PrimaryEntry != nullptr)
        {
            FThornExtendedProgress Progress;
            EvaluateProgressExtended(TimeMs, &Progress, false);
            float U;
            StoreAnimTValue(&Progress, &U);
            if ((*PrimaryEntry)->Snap.Type == kThornEntityScene)
            {
                // Less and unordered skip the discrete fields.
                const bool bSnapDiscrete = U >= 1.0f;
                FThornEntity* Primary = *PrimaryEntry;
                FThornSceneFogSnapshot Out;
                std::memcpy(&Out, Primary->LiveSceneBlob, sizeof(Out));
                FThornFog8* ResultFog = &Out.Fog;
                const FThornFog8* Capture = &FogCapturePacked;
                if (bSnapDiscrete)
                {
                    const unsigned int SnapMask = FogMask;
                    if ((SnapMask & 1) != 0) ResultFog->On = Target.On;
                    if ((SnapMask & 2) != 0) ResultFog->Table = Target.Table;
                    if ((SnapMask & 8) != 0) ResultFog->Mode = Target.Mode;
                }
                const unsigned int InterpolationMask = FogMask;
                if ((InterpolationMask & 0x10) != 0)
                    FogLerpFloat(&Capture->Start, &Target.Start, &U, &ResultFog->Start);
                if ((InterpolationMask & 0x20) != 0)
                    FogLerpFloat(&Capture->End, &Target.End, &U, &ResultFog->End);
                if ((InterpolationMask & 0x40) != 0)
                    FogLerpFloat(&Capture->Density, &Target.Density, &U, &ResultFog->Density);
                if ((InterpolationMask & 4) != 0)
                {
                    ResultFog->Color[0] = FogLerpInteger(Capture->Color[0], Target.Color[0], &U);
                    ResultFog->Color[1] = FogLerpInteger(Capture->Color[1], Target.Color[1], &U);
                    ResultFog->Color[2] = FogLerpInteger(Capture->Color[2], Target.Color[2], &U);
                }
                // Mask 0; retail pushes a third argument that is unused.
                ApplySceneBlob(Primary, &Out, 0);
                ApplySceneFog(Primary, static_cast<unsigned char>(FogMask));
            }
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::StartPsysPropAnim(FThornScene* Scene)
{
    // LAB_06f489a0: primary lookup only.
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    if (Primary != nullptr && Primary->Snap.Type == kThornEntityPsys)
    {
        IPSysControl* Control = Primary->PsysControl;
        if (Control != nullptr)
            Control->GetSparam(&Primary->PsysRuntimeSparam);
        std::memcpy(&SparamCapture, &Primary->PsysRuntimeSparam, sizeof(SparamCapture));
    }
    // Flags/duration/start are read after the capture callbacks.
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::StartReverbPropAnim(FThornScene* Scene)
{
    // LAB_06f491e0: primary lookup only.
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr && (*PrimaryEntry)->Snap.Type == kThornEntityScene)
    {
        IReverbControl* Control = (*PrimaryEntry)->ReverbControl;
        if (Control != nullptr)
            Control->GetReverb(&ReverbCapture[0], &ReverbCapture[1],
                              &ReverbCapture[2], &ReverbCapture[3]);
        else
        {
            const unsigned int Defaults[4] = {0, 0, 0x3f800000, 0x3f800000};
            std::memcpy(ReverbCapture, Defaults, sizeof(ReverbCapture));
        }
    }
    // Flags/start/duration are read after the GET.
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::TickReverbPropAnim(int TimeMs, FThornScene* Scene)
{
    // A zero mask skips the target read, lookup and progress callback.
    if (ReverbMask != 0)
    {
        FThornReverb4 Target;
        std::memcpy(&Target, &ReverbTarget, sizeof(Target));
        FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
        if (PrimaryEntry != nullptr)
        {
            FThornExtendedProgress Progress;
            EvaluateProgressExtended(TimeMs, &Progress, true);
            float U;
            StoreAnimTValue(&Progress, &U);
            // The entry is reread after the progress callback.
            FThornEntity* Primary = *PrimaryEntry;
            FThornReverb4 Output;
            std::memcpy(&Output, ReverbCapture, sizeof(Output));
            if (Primary->Snap.Type == kThornEntityScene)
            {
                IReverbControl* Control = (*PrimaryEntry)->ReverbControl;
                if (Control != nullptr)
                {
                    // Only U >= 1 (ordered) replaces the environment.
                    if (U >= 1.0f && (ReverbMask & 1) != 0)
                        Output.Environment = Target.Environment;
                    const int Mask = ReverbMask;
                    // Same x87 kernel as the FOG floats.
                    if ((Mask & 2) != 0)
                        FogLerpFloat(&ReverbCapture[1], &Target.Volume, &U, &Output.Volume);
                    if ((Mask & 4) != 0)
                        FogLerpFloat(&ReverbCapture[2], &Target.Decay, &U, &Output.Decay);
                    if ((Mask & 8) != 0)
                        FogLerpFloat(&ReverbCapture[3], &Target.Damping, &U, &Output.Damping);
                    ThornSetReverbProperties(Control, &Output);
                }
            }
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::StartAudioPropAnim(FThornScene* Scene)
{
    // LAB_06f47320: primary lookup, SOUND (6) entities only.
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    if (Primary != nullptr && Primary->Snap.Type == kThornEntitySound)
    {
        float CapturedWords[8];
        CaptureAudioFromEntity(Primary, CapturedWords);
        std::memcpy(&AudioCapture, CapturedWords, sizeof(AudioCapture));
    }
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::TickAudioPropAnim(int TimeMs, FThornScene* Scene)
{
    // LAB_06f473a0: the target is copied before either lookup or the curve.
    if (AudioMask != 0)
    {
        FThornAudio8 To;
        std::memcpy(&To, &AudioTarget, sizeof(To));
        if (SecondaryIndex != -1)
        {
            FThornEntity* Secondary = Scene->EntityAt(SecondaryIndex);
            if (Secondary != nullptr && Secondary->Snap.Type == kThornEntitySound)
                std::memcpy(&To, Secondary->Audio, sizeof(To)); // no foreign GET
        }
        FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
        if (PrimaryEntry != nullptr)
        {
            FThornExtendedProgress Progress;
            EvaluateProgressExtended(TimeMs, &Progress, false);
            // The 80-bit progress stays on the x87 stack for every term below.
            const FThornExtendedProgress* ProgressValue = &Progress;
            __asm
            {
                mov eax, ProgressValue
                fld tbyte ptr [eax]
            }
            // The entity is read after the curve callback.
            FThornEntity* Primary = *PrimaryEntry;
            const int Mask = AudioMask;
            FThornAudio8 Out;
            std::memcpy(&Out, Primary->Audio, sizeof(Out));
            if ((Mask & 1) != 0)
                AudioLerpFloat(&AudioCapture.Attenuation, &To.Attenuation, &Out.Attenuation);
            if ((Mask & 2) != 0)
                AudioLerpFloat(&AudioCapture.Pan, &To.Pan, &Out.Pan);
            // Retail repeats these two, FP exception effects included.
            if ((Mask & 1) != 0)
                AudioLerpFloat(&AudioCapture.Attenuation, &To.Attenuation, &Out.Attenuation);
            if ((Mask & 2) != 0)
                AudioLerpFloat(&AudioCapture.Pan, &To.Pan, &Out.Pan);
            if ((Mask & 4) != 0)
                AudioLerpFloat(&AudioCapture.Dmin, &To.Dmin, &Out.Dmin);
            if ((Mask & 8) != 0)
                AudioLerpFloat(&AudioCapture.Dmax, &To.Dmax, &Out.Dmax);
            if ((Mask & 0x10) != 0)
                Out.Ain = static_cast<int>(static_cast<unsigned int>(static_cast<__int64>(
                    ComputeAudioLerpInteger(AudioCapture.Ain, To.Ain))));
            if ((Mask & 0x20) != 0)
                Out.Aout = static_cast<int>(static_cast<unsigned int>(static_cast<__int64>(
                    ComputeAudioLerpInteger(AudioCapture.Aout, To.Aout))));
            if ((Mask & 0x40) != 0)
                AudioLerpFloat(&AudioCapture.Atout, &To.Atout, &Out.Atout);
            if ((Mask & 0x80) != 0)
                AudioLerpFloat(&AudioCapture.Rmix, &To.Rmix, &Out.Rmix);
            // Pop the progress before the output callback.
            __asm { fstp st(0) }
            float OutWords[8];
            std::memcpy(OutWords, &Out, sizeof(Out));
            ApplyAudioProps(Primary, OutWords);
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::StartFloorHeightAnim(FThornScene* Scene)
{
    // FUN_06f497b0: primary capture, then the secondary lookup.
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr && (*PrimaryEntry)->Snap.Type == kThornEntityDeformable)
    {
        FThornEntity* Primary = *PrimaryEntry;
        ICompoundControl* Control = Primary->CompoundControl;
        if (Control != nullptr)
            Control->GetFloorHeight(&FloorCapture);
        else
        {
            // One widened load feeds both stores; reloading would add a
            // denormal exception between them.
            const double Height = static_cast<double>(Primary->Snap.FloorHeight);
            Primary->FloorHeight = static_cast<float>(Height);
            FloorCapture = static_cast<float>(Height);
        }
    }
    if (SecondaryIndex != -1)
    {
        FThornEntity* Secondary = Scene->EntityAt(SecondaryIndex);
        if (Secondary != nullptr)
        {
            GetRetailRelativePosition(Secondary, &Secondary->Snap.Pos);
            std::memcpy(&FloorTarget, &Secondary->Snap.Pos.y, sizeof(FloorTarget));
        }
    }
    const unsigned int Zero = 0;
    std::memcpy(&FloorWrapLast, &Zero, sizeof(Zero));
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::Start(FThornScene* Scene, int TimeMs)
{
    if (NativeEventType == kThornTypeStartIk)
    {
        ApplyEndTime(this);
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary == nullptr)
            return 0;
        FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
        if (SecondaryEntry == nullptr || Primary->IkControl == nullptr)
            return 0;
        Vector Pos;
        Matrix Orient;
        BuildIkPose(this, Primary, SecondaryEntry, &Pos, &Orient);
        PackIkDesc();
        return HrToAl(Primary->IkControl->StartIk(&IkDesc.Common, &Pos, &Orient));
    }
    if (NativeEventType == kThornTypeStartSubScene)
    {
        ApplyEndTime(this);
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary == nullptr || Primary->ScriptControl == nullptr)
            return 0;
        IScriptControl* Control = Primary->ScriptControl;
        const __int64 Converted = static_cast<__int64>(SubSceneStartTime);
        const unsigned int LowWord = static_cast<unsigned int>(Converted);
        int StartValue;
        std::memcpy(&StartValue, &LowWord, sizeof(StartValue));
        return HrToAl(Control->Start(StartValue));
    }
    if (NativeEventType == kThornTypeStartPsys)
    {
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary == nullptr || Primary->PsysControl == nullptr)
            return 0;
        return HrToAl(Primary->PsysControl->Start());
    }
    if (&Desc == &FloorDesc.Common)
        return StartFloorHeightAnim(Scene);
    if (&Desc == &ReverbDesc.Common)
        return StartReverbPropAnim(Scene);
    if (&Desc == &FogDesc.Common)
        return StartFogPropAnim(Scene);
    if (&Desc == &PsysPropDesc.Common)
        return StartPsysPropAnim(Scene);
    if (&Desc == &AudioPropDesc.Common)
        return StartAudioPropAnim(Scene);
    if (Type < kThornEventTypeMin || Type > kThornEventTypeMax)
    {
        ThornFdump("Start attempted on event with unknown type\n");
        return 0;
    }
    if (Type == kThornTypeUserEvent)
    {
        ApplyEndTime(this);
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary == nullptr || Primary->UserEventControl == nullptr)
            return 0;
        IUserEventControl* Control = Primary->UserEventControl;
        // Retail passes event +0x28 directly, not through the GetDesc virtual.
        return HrToAl(Control->StartUserEvent(&UserEventDesc));
    }
    if (Type == kThornTypeCameraPropAnim)
        return StartCameraPropAnim(Scene);
    if (Type == kThornTypeSpatialPropAnim)
        return StartSpatialPropAnim(Scene);
    if (Type == kThornTypeAttachEntity)
        return StartAttachEntity(Scene);
    if (Scene == nullptr)
        return 0;


    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    FThornEntity* Secondary = nullptr;
    if (SecondaryIndex >= 0)
        Secondary = Scene->EntityAt(SecondaryIndex);

    if (Type == kThornTypeSetCamera)
        return StartSetCamera(Scene);

    if (Type == kThornTypePathAnim)
    {
        // LAB_06f45160: only the percent delta and end time; the parent Csys
        // is not touched.
        PercentDelta = StopPercent - StartPercent;
        ApplyEndTime(this);
        return 1;
    }

    if (Type == kThornTypeStartMotion)
    {
        ApplyEndTime(this);
        if (Primary == nullptr || Primary->CompoundControl == nullptr)
            return 0;
        // LAB_06f43cd0: slot 3 with event +0x28, the whole motion descriptor.
        return HrToAl(Primary->CompoundControl->StartMotion(GetDesc()));
    }

    if (Type == kThornTypeStartSound)
    {
        ApplyEndTime(this);
        if (Primary == nullptr || Primary->SoundControl == nullptr)
            return 0;
        return HrToAl(Primary->SoundControl->StartSound(GetDesc(), TimeMs));
    }


    if (Type == kThornTypeLightPropAnim)
    {
        // LAB_06f456e0: GET into the entity blob, copy 0x24 dwords -> event +0xe8.
        if (Primary != nullptr && (Primary->Snap.Type == kThornEntityLight ||
                                   Primary->Snap.Type == kThornEntityScene))
        {
            CaptureLiveLightProps(Primary, &LightCapture);
            UnpackEntityLightProps(Primary, &LightCapture);
        }
        ApplyEndTime(this);
        return 1;
    }


    if (Type == kThornTypeSubtitle)
    {
        ISubtitleControl* Sub = SubtitleOf(Primary);
        if (Sub != nullptr)
            Sub->ShowSubtitle(Strid);
        ApplyEndTime(this);
        return 1;
    }

    if (Type == kThornTypeConnectHardpoints)
    {
        ApplyEndTime(this);
        if (Primary == nullptr || Primary->CompoundControl == nullptr)
            return 0;
        return HrToAl(Primary->CompoundControl->ConnectHardpoints(GetDesc()));
    }

    if (Type == kThornTypeStartIk)
    {
        // LAB_06f44140: both lookups + IIKControl; inverse after the hardpoint;
        // FUN_06f2e440; forward Csys; slot 3. Returns 0 on a miss.
        ApplyEndTime(this);
        if (Primary == nullptr || Secondary == nullptr || Primary->IkControl == nullptr)
            return 0;
        Vector Pos;
        Matrix Orient;
        BuildIkPose(this, Primary, &Secondary, &Pos, &Orient);
        // Event +0x28: the descriptor, IK fields included.
        return HrToAl(Primary->IkControl->StartIk(GetDesc(), &Pos, &Orient));
    }

    if (Type == kThornTypeStartPsys)
    {
        // LAB_06f48760 is the one retail Start without the end-time prologue:
        // EndTimeMs stays as ApplyDesc (FUN_06f48700) left it, StartTime +
        // DurationMs, so a START_PSYS with USE_SCRIPT_DURATION ends at its own
        // start time rather than running unbounded. Retail quirk, kept.
        if (Primary == nullptr || Primary->PsysControl == nullptr)
            return 0;
        return HrToAl(Primary->PsysControl->Start());
    }

    if (Type == kThornTypeReverbPropAnim)
    {
        // LAB_06f491e0: type 9 GET slot 4; NULL control -> 0, 0, 1.0, 1.0.
        if (Primary != nullptr && Primary->Snap.Type == kThornEntityScene)
        {
            if (Primary->ReverbControl != nullptr)
            {
                Primary->ReverbControl->GetReverb(&ReverbCapture[0], &ReverbCapture[1],
                                                  &ReverbCapture[2], &ReverbCapture[3]);
            }
            else
            {
                StoreIntBits(&ReverbCapture[0], 0);
                ReverbCapture[1] = 0.0f;
                ReverbCapture[2] = 1.0f;
                ReverbCapture[3] = 1.0f;
            }
        }
        ApplyEndTime(this);
        return 1;
    }

    return 0;
}

void FThornEvent::DispatchSubtitleControl(FThornScene* Scene, bool bShow)
{
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    ISubtitleControl* Control = SubtitleOf(Primary);
    if (Control != nullptr)
    {
        if (bShow)
            Control->ShowSubtitle(Strid);
        else
            Control->HideSubtitle(Strid);
    }
}

unsigned char FThornEvent::Stop(FThornScene* Scene)
{
    // Dispatch on the constructed type: retail's vtable does not follow Type.
    switch (NativeEventType)
    {
    case kThornTypeStartMotion:
    case kThornTypeStartSound:
    case kThornTypeStartIk:
    case kThornTypeConnectHardpoints:
    case kThornTypeStartSubScene:
    case kThornTypeStartPsys:
    {
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary == nullptr)
            return 0;
        switch (NativeEventType)
        {
        case kThornTypeStartMotion:
            if (Primary->CompoundControl != nullptr)
            {
                ICompoundControl* Control = Primary->CompoundControl;
                PackMotionDesc();
                Control->StopMotion(&MotionDesc.Common);
            }
            break;
        case kThornTypeStartSound:
            if (Primary->SoundControl != nullptr)
                Primary->SoundControl->StopSound(&Desc);
            break;
        case kThornTypeStartIk:
            // The secondary lookup comes first, even for negative keys.
            if (Scene->EntityAt(SecondaryIndex) != nullptr && Primary->IkControl != nullptr)
            {
                IIKControl* Control = Primary->IkControl;
                PackIkDesc();
                Control->StopIk(&IkDesc.Common);
            }
            break;
        case kThornTypeConnectHardpoints:
            if (Primary->CompoundControl != nullptr)
            {
                ICompoundControl* Control = Primary->CompoundControl;
                PackConnectDesc();
                Control->DisconnectHardpoints(&ConnectDesc.Common);
            }
            break;
        case kThornTypeStartSubScene:
            if (Primary->ScriptControl != nullptr)
                Primary->ScriptControl->Stop();
            break;
        case kThornTypeStartPsys:
            if (Primary->PsysControl != nullptr)
                Primary->PsysControl->StopEffect();
            break;
        }
        return 0;
    }
    case kThornTypeSubtitle:
        DispatchSubtitleControl(Scene, false);
        return 0;
    }

    // These Stop slots return 0 without touching the scene: FOG LAB_06f4a6b0,
    // REVERB LAB_06f4a6e0, CAMERA LAB_06f4a520, LIGHT LAB_06f4a4f0, SET
    // MONITOR LAB_06f4a480.
    if (&Desc == &FogDesc.Common)
        return 0;
    if (&Desc == &ReverbDesc.Common)
        return 0;
    if (&Desc == &CameraDesc.Common)
        return 0;
    if (&Desc == &LightDesc.Common)
        return 0;
    if (&Desc == &SetCameraDesc)
        return 0;
    if (&Desc == &AudioPropDesc.Common)
        return 0;
    if (Type == kThornTypeUserEvent)
    {
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary != nullptr && Primary->UserEventControl != nullptr)
        {
            IUserEventControl* Control = Primary->UserEventControl;
                Control->StopUserEvent(&UserEventDesc);
        }
        return 0;
    }
    if (Scene == nullptr)
        return 0;
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);

    if (Type == kThornTypeStartMotion && Primary != nullptr && Primary->CompoundControl != nullptr)
        Primary->CompoundControl->StopMotion(GetDesc());
    else if (Type == kThornTypeStartSound && Primary != nullptr && Primary->SoundControl != nullptr)
        Primary->SoundControl->StopSound(&Desc);
    else if (Type == kThornTypeStartSubScene && Primary != nullptr &&
             Primary->ScriptControl != nullptr)
        Primary->ScriptControl->Stop();
    else if (Type == kThornTypeStartPsys && Primary != nullptr && Primary->PsysControl != nullptr)
        Primary->PsysControl->StopEffect();
    else if (Type == kThornTypeConnectHardpoints && Primary != nullptr &&
             Primary->CompoundControl != nullptr)
    {
        Primary->CompoundControl->DisconnectHardpoints(GetDesc());
    }
    else if (Type == kThornTypeStartIk && Primary != nullptr && Primary->IkControl != nullptr)
    {
        // LAB_06f44860: both indices must resolve before IIKControl slot 4;
        // a missing secondary silently skips the stop.
        if (SecondaryIndex >= 0 && Scene->EntityAt(SecondaryIndex) != nullptr)
            Primary->IkControl->StopIk(GetDesc());
    }
    else if (Type == kThornTypeSubtitle)
    {
        ISubtitleControl* Sub = SubtitleOf(Primary);
        if (Sub != nullptr)
            Sub->HideSubtitle(Strid);
    }
    return 0;
}

unsigned char FThornEvent::TickCameraPropAnim(int TimeMs, FThornScene* Scene)
{
    if (CamMask != 0)
    {
        float TargetValues[4];
        for (int Index = 0; Index < 4; ++Index)
            std::memcpy(&TargetValues[Index], &CamTarget[Index], sizeof(float));
        // Only -1 skips; any other signed ID goes through the lookup.
        if (SecondaryIndex != -1)
        {
            FThornEntity* Secondary = Scene->EntityAt(SecondaryIndex);
            if (Secondary != nullptr && Secondary->Snap.Type == kThornEntityCamera)
                CaptureCameraProps(Secondary, TargetValues);
        }
        FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
        if (PrimaryEntry != nullptr)
        {
            FThornExtendedProgress Progress;
            // Period-aware progress, whatever Type says by now.
            EvaluateProgressExtended(TimeMs, &Progress, false);
            // The entry found before the curve callback is reread after it
            // (no fresh lookup); the primary's runtime cache is used, no GET.
            FThornEntity* Primary = *PrimaryEntry;
            float ResultValues[4];
            for (int Index = 0; Index < 4; ++Index)
                std::memcpy(&ResultValues[Index], &Primary->CameraRuntimeProps[Index], sizeof(float));
            InterpolateCameraProps(ResultValues, TargetValues, CamCapture, &Progress, CamMask);
            ApplyCameraProps(Primary, ResultValues);
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::TickPsysPropAnim(int TimeMs, FThornScene* Scene)
{
    // LAB_06f48a40: the secondary's runtime sparam is read (no GetSparam)
    // before the primary lookup and the curve callback.
    if (PsysMask != 0)
    {
        float Target;
        std::memcpy(&Target, &SparamTarget, sizeof(Target));
        if (SecondaryIndex != -1)
        {
            FThornEntity* Secondary = Scene->EntityAt(SecondaryIndex);
            if (Secondary != nullptr && Secondary->Snap.Type == kThornEntityPsys)
                std::memcpy(&Target, &Secondary->PsysRuntimeSparam, sizeof(Target));
        }
        FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
        if (PrimaryEntry != nullptr)
        {
            FThornExtendedProgress Progress;
            EvaluateProgressExtended(TimeMs, &Progress, false);
            float Scale;
            StoreAnimTValue(&Progress, &Scale);
            // The primary's type and cache are read after the curve call.
            if ((*PrimaryEntry)->Snap.Type == kThornEntityPsys)
            {
                FThornEntity* Primary = *PrimaryEntry;
                float Result;
                std::memcpy(&Result, &Primary->PsysRuntimeSparam, sizeof(Result));
                if ((PsysMask & 1) != 0)
                {
                    const float* Capture = &SparamCapture;
                    __asm {
                        mov edx, Capture
                        fld dword ptr [edx]
                        fld dword ptr Target
                        fsub st(0), st(1)
                        fmul dword ptr Scale
                        fadd st(0), st(1)
                        fstp dword ptr Result
                        fstp st(0)
                    }
                }
                ApplyPsysSparam(Primary, &Result);
            }
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::TickLightPropAnim(int TimeMs, FThornScene* Scene)
{
    // LAB_06f45890: a zero mask skips all lookups and the curve.
    if (LightMask == 0)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);

    FThornLightProps Target;
    std::memcpy(&Target, &LightTarget, sizeof(Target));
    if (SecondaryIndex != -1)
    {
        FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
        if (SecondaryEntry != nullptr)
        {
            if ((*SecondaryEntry)->Snap.Type == kThornEntityLight)
            {
                FThornEntity* Secondary = *SecondaryEntry;
                FThornLightProps& Live = Secondary->LightEntityDescriptor;
                if (Secondary->D3dLightControl != nullptr)
                {
                    // The control is reread before each call.
                    Secondary->D3dLightControl->GetLight(&Live.Type);
                    Secondary->D3dLightControl->GetOn(&Live.On);
                    Secondary->D3dLightControl->GetDirection(&Live.Direction);
                }
                else if (Secondary->LightControl != nullptr)
                {
                    Secondary->LightControl->GetColor(reinterpret_cast<float*>(Live.Color));
                    Secondary->LightControl->GetDirection(&Live.Direction);
                    Secondary->LightControl->GetRange(&Live.RangeTemp);
                    Secondary->LightControl->GetCutoff(&Live.CutoffTemp);
                    Secondary->LightControl->GetInfinite(&Live.Infinite);
                    Secondary->LightControl->GetOn(&Live.On);
                    ConvertCompactLightRetail(reinterpret_cast<const unsigned char*>(&Live),
                                              reinterpret_cast<unsigned char*>(&Live.Type));
                }
                std::memcpy(&Target, &Live, sizeof(Target));
            }
            else if ((*SecondaryEntry)->Snap.Type == kThornEntityScene)
            {
                FThornEntity* Secondary = *SecondaryEntry;
                if (Secondary->LightControl != nullptr)
                    Secondary->LightControl->GetColor(reinterpret_cast<float*>(Secondary->LiveSceneBlob));
                // Retail keeps color 2 in EBX rather than in the target copy;
                // all three feed the colour interpolation either way.
                std::memcpy(Target.Color, Secondary->LiveSceneBlob, sizeof(Target.Color));
            }
        }
    }

    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr)
    {
        FThornExtendedProgress Progress;
        EvaluateProgressExtended(TimeMs, &Progress, false);
        float U;
        StoreAnimTValue(&Progress, &U);

        FThornLightProps Output = {};
        if ((*PrimaryEntry)->Snap.Type == kThornEntityLight)
            std::memcpy(&Output, &(*PrimaryEntry)->LightEntityDescriptor, sizeof(Output));
        else if ((*PrimaryEntry)->Snap.Type == kThornEntityScene)
        {
            // Only the three colour words reach the scene sink; the rest is
            // zeroed here instead of retail's stack garbage.
            std::memcpy(Output.Color, (*PrimaryEntry)->LiveSceneBlob, sizeof(Output.Color));
        }
        EvalLightProps(&Output, &LightCapture, &Target, &LightMask, &U);
        if ((*PrimaryEntry)->Snap.Type == kThornEntityLight)
            ApplyEntityLightRuntimeProps(*PrimaryEntry, &Output);
        else if ((*PrimaryEntry)->Snap.Type == kThornEntityScene)
        {
            unsigned int SceneSnapshot[15];
            std::memcpy(SceneSnapshot, (*PrimaryEntry)->LiveSceneBlob, sizeof(SceneSnapshot));
            std::memcpy(SceneSnapshot, Output.Color, sizeof(Output.Color));
            // Retail passes -1 for both remaining arguments; the third is unused.
            ApplySceneBlob(*PrimaryEntry, SceneSnapshot, 0xff);
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::TickSpatialPropAnim(int TimeMs, FThornScene* Scene)
{
    // LAB_06f46a40: a zero mask skips the progress call and lookups. SPATIAL's
    // progress slot (LAB_06f47110) does not follow Type.
    if (SpatialMask == 0)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    FThornExtendedProgress Progress;
    EvaluateProgressExtended(TimeMs, &Progress, false);
    float U;
    float Scale;
    SpatialWrapScale(&SpatialWrapLast, &Progress, &U, &Scale);
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr)
    {
        // The position is copied to +0xdc before the orientation getter.
        FThornEntity* Primary = *PrimaryEntry;
        GetRetailRelativePosition(Primary, &Primary->Snap.Pos);
        std::memcpy(&CapturePos, &Primary->Snap.Pos, sizeof(CapturePos));
        Primary = *PrimaryEntry;
        GetRetailRelativeOrientation(Primary, &Primary->Snap.Orient);
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        SpatialMatrixToQuaternion(Math, CaptureQuat, &Primary->Snap.Orient);

        // The index is reread after the primary math callback.
        FThornEntity** SecondaryEntry = SecondaryIndex != -1 ? Scene->FindEntityEntry(SecondaryIndex) : nullptr;
        if (SecondaryEntry != nullptr)
        {
            FThornEntity* Secondary = *SecondaryEntry;
            GetRetailRelativePosition(Secondary, &Secondary->Snap.Pos);
            std::memcpy(&TargetPos, &Secondary->Snap.Pos, sizeof(TargetPos));
            Secondary = *SecondaryEntry;
            GetRetailRelativeOrientation(Secondary, &Secondary->Snap.Orient);
            Math = AcquireSceneEventMathEngine();
            SpatialMatrixToQuaternion(Math, TargetQuat, &Secondary->Snap.Orient);
            if (TargetPart != nullptr && TargetType != 0)
            {
                const int SecondaryType = (*SecondaryEntry)->Snap.Type;
                // The name is reread after the type getter.
                const char* PartName = TargetPart;
                if (PartName != nullptr &&
                    (SecondaryType == kThornEntityCompound || SecondaryType == kThornEntityDeformable))
                {
                    ICompoundControl* Compound = (*SecondaryEntry)->CompoundControl;
                    if (Compound != nullptr)
                    {
                        Transform HpXform;
                        InitializeEventFrameWords(&HpXform.Rotation);
                        std::memset(&HpXform.Translation, 0, sizeof(float) * 3);
                        Compound->GetHardpointXform(TargetType, PartName, &HpXform);
                        const FThornCsys* Parent = ParentCsys;
                        if (Parent != nullptr)
                            MulXformByInverseCsys(Parent, &HpXform);
                        // All 12 words are copied and the quaternion rebuilt
                        // even without a parent; the GENRESULT is ignored.
                        std::memcpy(&TargetOrient, &HpXform.Rotation, sizeof(TargetOrient));
                        std::memcpy(&TargetPos, &HpXform.Translation, sizeof(TargetPos));
                        Math = AcquireSceneEventMathEngine();
                        SpatialMatrixToQuaternion(Math, TargetQuat, &TargetOrient);
                    }
                }
            }
        }

        if ((SpatialMask & kThornSpatialBitPos) != 0)
        {
            Vector Pos;
            LerpSpatialPosition(&Pos, &CapturePos, &TargetPos, &Scale);
            (*PrimaryEntry)->InnerSetPos(&Pos);
        }

        // Independent tests: with both bits set the orientation is written,
        // then overwritten by the axis rotation.
        if ((SpatialMask & kThornSpatialBitOrient) != 0)
        {
            Quaternion Quat;
            Matrix Orient;
            SlerpQuat4(&Quat, CaptureQuat, TargetQuat, &Scale);
            Quat4ToMat9(&Orient, &Quat);
            (*PrimaryEntry)->InnerSetOrient(&Orient);
        }
        if ((SpatialMask & kThornSpatialBitAxisRot) != 0)
        {
            Matrix Orient;
            const int AxisId = AxisRotAxis;
            float DeltaDeg;
            AccumulateAxisDelta(&U, &AxisRotAngle, &AxisRotApplied, &DeltaDeg);
            ApplyAxisRotFpu(&Orient, CaptureQuat, AxisId, &DeltaDeg);
            (*PrimaryEntry)->InnerSetOrient(&Orient);
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::TickAttachEntity(int TimeMs, FThornScene* Scene)
{
    // LAB_06f47c30: no progress/curve call on this tick.
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry == nullptr)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
    if (SecondaryEntry == nullptr)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);

    Transform Child;
    IdentityOrient(&Child.Rotation);
    Child.Translation.z = 0.0f;
    Child.Translation.y = 0.0f;
    Child.Translation.x = 0.0f;
    FThornEntity* Secondary = *SecondaryEntry;
    GetRetailRelativePosition(Secondary, &Secondary->Snap.Pos);
    Copy3(&Child.Translation, &Secondary->Snap.Pos);
    Secondary = *SecondaryEntry;
    GetRetailRelativeOrientation(Secondary, &Secondary->Snap.Orient);
    Copy9(&Child.Rotation, &Secondary->Snap.Orient);
    if (TargetPart != nullptr && TargetType != 0)
    {
        const int SecondaryType = (*SecondaryEntry)->Snap.Type;
        if (SecondaryType == kThornEntityCompound || SecondaryType == kThornEntityDeformable)
        {
            ICompoundControl* Compound = (*SecondaryEntry)->CompoundControl;
            if (Compound != nullptr)
            {
                // Unlike SPATIAL there is no second part-name null test.
                Compound->GetHardpointXform(TargetType, TargetPart, &Child);
                const FThornCsys* Parent = ParentCsys;
                if (Parent != nullptr)
                    MulXformByInverseCsys(Parent, &Child);
            }
        }
    }
    if ((Desc.Flags & kThornFlagParentChild) != 0)
    {
        Transform Result;
        SceneMathMulTransform(AcquireSceneEventMathEngine(), &Result, &Child, &AttachXform);
        if ((Desc.Flags & kThornFlagPosition) != 0)
            (*PrimaryEntry)->InnerSetPos(&Result.Translation);
        if ((Desc.Flags & kThornFlagOrientation) != 0)
            (*PrimaryEntry)->InnerSetOrient(&Result.Rotation);
    }
    else
    {
        if ((Desc.Flags & kThornFlagPosition) != 0)
        {
            Vector Offset;
            Copy3(&Offset, &PathOffset);
            if ((Desc.Flags & kThornFlagEntityRelative) != 0)
            {
                Vector Rotated;
                SceneMathRotate(AcquireSceneEventMathEngine(), &Rotated, &Child, &Offset);
                Copy3(&Offset, &Rotated);
            }
            Vector Position;
            AddAttachOffset(&Position, &Child.Translation, &Offset, true);
            (*PrimaryEntry)->InnerSetPos(&Position);
        }
        if ((Desc.Flags & kThornFlagOrientation) != 0)
        {
            if ((Desc.Flags & kThornFlagOrientRelative) != 0)
            {
                Transform Result;
                SceneMathMulTransform(AcquireSceneEventMathEngine(), &Result, &Child, &AttachXform);
                (*PrimaryEntry)->InnerSetOrient(&Result.Rotation);
            }
            else
                (*PrimaryEntry)->InnerSetOrient(&Child.Rotation);
        }
        else if ((Desc.Flags & kThornFlagLookAt) != 0)
        {
            Vector Offset;
            Copy3(&Offset, &PathOffset);
            if ((Desc.Flags & kThornFlagEntityRelative) != 0)
            {
                Vector Rotated;
                SceneMathRotate(AcquireSceneEventMathEngine(), &Rotated, &Child, &Offset);
                Copy3(&Offset, &Rotated);
            }
            Vector Target;
            AddAttachOffset(&Target, &Child.Translation, &Offset, false);
            // The entity's relative position getter, parent inverse included.
            FThornEntity* Primary = *PrimaryEntry;
            GetRetailRelativePosition(Primary, &Primary->Snap.Pos);
            Vector Direction;
            if (BuildAttachLookDirection(&Direction, &Target, &Primary->Snap.Pos))
            {
                Matrix Orientation;
                BuildAxisFrame(&Orientation, &Direction, static_cast<unsigned>(LookAtFrontAxis),
                               static_cast<unsigned>(LookAtUpAxis), SceneFrontAxis, SceneUpAxis);
                (*PrimaryEntry)->InnerSetOrient(&Orientation);
            }
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

// LAB_06f451a0: the secondary is captured before the progress call; the
// primary is reread from its entry after each callback.
unsigned char FThornEvent::TickPathAnim(int TimeMs, FThornScene* Scene)
{
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry == nullptr)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
    if (SecondaryEntry == nullptr)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    FThornEntity* Secondary = *SecondaryEntry;
    if (Secondary->MotionPath == nullptr)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);

    // PATH's progress slot is FUN_06f454f0; its extended result is stored once.
    const float Percent = EvaluateControlBackedCurve(PercentDelta, TimeMs);
    Vector Position;
    Matrix Orientation;
    FThornEntity* Primary = *PrimaryEntry;
    GetRetailRelativePosition(Primary, &Primary->Snap.Pos);
    Copy3(&Position, &Primary->Snap.Pos);
    Primary = *PrimaryEntry;
    GetRetailRelativeOrientation(Primary, &Primary->Snap.Orient);
    Copy9(&Orientation, &Primary->Snap.Orient);

    Vector PathVector;
    if ((Desc.Flags & kThornFlagPosition) != 0 &&
        Secondary->MotionPath->GetPositionAt(Percent, &PathVector) == GR_OK)
    {
        Transform PathXform;
        IdentityOrient(&PathXform.Rotation);
        PathXform.Translation.x = 0.0f;
        PathXform.Translation.y = 0.0f;
        PathXform.Translation.z = 0.0f;
        GetRetailRelativeOrientation(Secondary, &Secondary->Snap.Orient);
        Copy9(&PathXform.Rotation, &Secondary->Snap.Orient);
        GetRetailRelativePosition(Secondary, &Secondary->Snap.Pos);
        Copy3(&PathXform.Translation, &Secondary->Snap.Pos);
        Vector OffsetPosition;
        // Offsets are read after both getters; Z/Y/X stores.
        AddAttachOffset(&OffsetPosition, &PathVector, &PathOffset, true);
        Vector WorldPosition;
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        SceneMathTransform(Math, &WorldPosition, &PathXform, &OffsetPosition);
        Copy3(&Position, &WorldPosition);
    }
    if ((Desc.Flags & kThornFlagOrientation) != 0)
    {
        if ((Desc.Flags & kThornFlagLookAt) != 0)
        {
            if (Secondary->MotionPath->GetTangentAt(Percent, &PathVector) == GR_OK)
            {
                Transform PathXform;
                IdentityOrient(&PathXform.Rotation);
                PathXform.Translation.x = 0.0f;
                PathXform.Translation.y = 0.0f;
                PathXform.Translation.z = 0.0f;
                GetRetailRelativeOrientation(Secondary, &Secondary->Snap.Orient);
                Copy9(&PathXform.Rotation, &Secondary->Snap.Orient);
                Vector Rotated;
                I3DMathEngine* Math = AcquireSceneEventMathEngine();
                SceneMathRotate(Math, &Rotated, &PathXform, &PathVector);
                Copy3(&PathVector, &Rotated);
                BuildAxisFrame(&Orientation, &PathVector,
                               static_cast<unsigned>(LookAtFrontAxis),
                               static_cast<unsigned>(LookAtUpAxis), SceneFrontAxis, SceneUpAxis);
            }
        }
        else
        {
            Quaternion QuaternionWords;
            if (Secondary->MotionPath->GetOrientationAt(Percent, &QuaternionWords) == GR_OK)
            {
                Quaternion Quat;
                std::memcpy(&Quat, &QuaternionWords, sizeof(Quat));
                Matrix PathMatrix;
                I3DMathEngine* Math = AcquireSceneEventMathEngine();
                Math->QuaternionToMatrix(PathMatrix, Quat);
                GetRetailRelativeOrientation(Secondary, &Secondary->Snap.Orient);
                // The getter's result pointer is kept across the next acquire.
                const Matrix* SecondaryMatrix = &Secondary->Snap.Orient;
                Math = AcquireSceneEventMathEngine();
                Matrix Combined;
                SceneMathMulMatrix(Math, &Combined, SecondaryMatrix, &PathMatrix);
                Copy9(&Orientation, &Combined);
            }
        }
    }
    (*PrimaryEntry)->InnerSetOrient(&Orientation);
    (*PrimaryEntry)->InnerSetPos(&Position);
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::TickFloorHeightAnim(int TimeMs, FThornScene* Scene)
{
    // FLOOR's progress slot does not follow Type.
    FThornExtendedProgress Progress;
    EvaluateProgressExtended(TimeMs, &Progress, false);
    float Scale;
    FloorWrapScale(this, &Progress, &Scale);
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    if (Primary != nullptr)
    {
        ICompoundControl* CaptureControl = Primary->CompoundControl;
        if (CaptureControl != nullptr)
            CaptureControl->GetFloorHeight(&FloorCapture);
        else
            std::memcpy(&FloorCapture, &Primary->FloorHeight, sizeof(FloorCapture));
        // The secondary ID is read after the primary GET callback.
        if (SecondaryIndex != -1)
        {
            FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
            if (SecondaryEntry != nullptr)
            {
                FThornEntity* Secondary = *SecondaryEntry;
                GetRetailRelativePosition(Secondary, &Secondary->Snap.Pos);
                std::memcpy(&FloorTarget, &Secondary->Snap.Pos.y, sizeof(FloorTarget));
                if (TargetPart != nullptr && TargetType != 0)
                {
                    const int SecondaryType = (*SecondaryEntry)->Snap.Type;
                    if (TargetPart != nullptr &&
                        (SecondaryType == kThornEntityCompound || SecondaryType == kThornEntityDeformable))
                    {
                        ICompoundControl* HardpointControl = (*SecondaryEntry)->CompoundControl;
                        if (HardpointControl != nullptr)
                        {
                            Transform Hardpoint;
                            IdentityOrient(&Hardpoint.Rotation);
                            std::memset(&Hardpoint.Translation, 0, 3 * sizeof(float));
                            HardpointControl->GetHardpointXform(TargetType, TargetPart, &Hardpoint);
                            // One parent read serves both math calls.
                            const FThornCsys* Parent = ParentCsys;
                            if (Parent != nullptr)
                                TransformFloorHardpoint(Parent, &Hardpoint);
                            std::memcpy(&FloorTarget, &Hardpoint.Translation.y, sizeof(FloorTarget));
                        }
                    }
                }
            }
        }
        // Retail keeps the primary pointer from before the callbacks.
        ICompoundControl* OutputControl = Primary->CompoundControl;
        if (OutputControl != nullptr)
        {
            float Height;
            InterpolateFloorHeight(&FloorCapture, &FloorTarget, &Scale, &Height);
            ThornSetFloorHeight(OutputControl, &Height);
            std::memcpy(&Primary->FloorHeight, &Height, sizeof(Height));
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::StillRunning(int TimeMs, FThornScene* Scene)
{
    const int EventType = NativeEventType != 0 ? NativeEventType : Type;
    if (EventType == kThornTypeLightPropAnim)
        return TickLightPropAnim(TimeMs, Scene);
    if (EventType == kThornTypeStartIk)
    {
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary != nullptr)
        {
            FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
            if (SecondaryEntry != nullptr && Primary->IkControl != nullptr)
            {
                Vector Pos;
                Matrix Orient;
                BuildIkPose(this, Primary, SecondaryEntry, &Pos, &Orient);
                PackIkDesc();
                Primary->IkControl->UpdateIk(&IkDesc.Common, &Pos, &Orient);
            }
        }
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    }
    // These tick slots only test the end time (LAB_06f43d50 etc.): no lookup,
    // no curve call.
    if (EventType == kThornTypeStartMotion || EventType == kThornTypeStartSound ||
        EventType == kThornTypeConnectHardpoints || EventType == kThornTypeStartSubScene ||
        EventType == kThornTypeStartPsys || EventType == kThornTypeSubtitle)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    // LAB_06f44d30: USER_EVENT's tick only tests the end time.
    if (bUserDescriptorOwner)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    // LAB_06f44fb0: SET MONITOR is one-shot; its tick returns 0.
    if (&Desc == &SetCameraDesc)
        return 0;
    if (bFloorEventOwner)
        return TickFloorHeightAnim(TimeMs, Scene);
    if (&Desc == &ReverbDesc.Common)
        return TickReverbPropAnim(TimeMs, Scene);
    if (&Desc == &FogDesc.Common)
        return TickFogPropAnim(TimeMs, Scene);
    if (&Desc == &PsysPropDesc.Common)
        return TickPsysPropAnim(TimeMs, Scene);
    if (&Desc == &AudioPropDesc.Common)
        return TickAudioPropAnim(TimeMs, Scene);
    if (Type == kThornTypeCameraPropAnim)
        return TickCameraPropAnim(TimeMs, Scene);
    if (Type == kThornTypePsysPropAnim)
        return TickPsysPropAnim(TimeMs, Scene);
    if (Type == kThornTypeSpatialPropAnim)
        return TickSpatialPropAnim(TimeMs, Scene);
    if (Type == kThornTypeAttachEntity)
        return TickAttachEntity(TimeMs, Scene);
    if (Type == kThornTypePathAnim)
        return TickPathAnim(TimeMs, Scene);
    if (Scene != nullptr)
    {
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        FThornEntity* Secondary = SecondaryIndex >= 0 ? Scene->EntityAt(SecondaryIndex) : nullptr;
        // FOG skips the curve callback when its mask is zero.
        const bool bSkipCurve = Type == kThornTypeFogPropAnim &&
            (FogMask == 0 || Primary == nullptr);
        FThornExtendedProgress Progress;
        float U = 0.0f;
        if (!bSkipCurve)
        {
            EvaluateAnimT(this, TimeMs, &Progress);
            U = StoreAnimT(&Progress);
        }

        if (Type == kThornTypeStartIk && Primary != nullptr && Secondary != nullptr &&
                 Primary->IkControl != nullptr)
        {
            // LAB_06f444f0: same pose build as Start; IIKControl slot 5.
            Vector Pos;
            Matrix Orient;
            BuildIkPose(this, Primary, &Secondary, &Pos, &Orient);
            Primary->IkControl->UpdateIk(GetDesc(), &Pos, &Orient);
        }
        else if (Type == kThornTypeReverbPropAnim && ReverbMask != 0 && Primary != nullptr &&
                 Primary->ReverbControl != nullptr && Primary->Snap.Type == kThornEntityScene)
        {
            // LAB_06f49290: no secondary; environment snaps at U >= 1; bits 2/4/8 lerp.
            float To[4];
            std::memcpy(To, &ReverbTarget, sizeof(To));
            float Out[4];
            Out[0] = ReverbCapture[0];
            Out[1] = ReverbCapture[1];
            Out[2] = ReverbCapture[2];
            Out[3] = ReverbCapture[3];
            if (U >= 1.0f && (ReverbMask & 0x1) != 0)
                Out[0] = To[0];
            if ((ReverbMask & 0x2) != 0)
                Out[1] = ReverbCapture[1] + (To[1] - ReverbCapture[1]) * U;
            if ((ReverbMask & 0x4) != 0)
                Out[2] = ReverbCapture[2] + (To[2] - ReverbCapture[2]) * U;
            if ((ReverbMask & 0x8) != 0)
                Out[3] = ReverbCapture[3] + (To[3] - ReverbCapture[3]) * U;
            Primary->ReverbControl->SetReverb(BitsAsUInt(&Out[0]), Out[1], Out[2], Out[3]);
        }

    }

    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

FThornScene::FThornScene()
    : EntityCount_(0), Master_(nullptr), MasterCount_(0),
      MasterCap_(0), Pending_(nullptr), PendingCount_(0), PendingCap_(0), PendingBegin_(0),
      Active_(nullptr), ActiveCount_(0), ActiveCap_(0), StopTimes_(std::in_place), FrameEntities_(std::in_place),
      DurationMs_(0), ClockMs_(0), ClockAfterDt_(0), PlaybackState_(0), bLoop_(0),
      LoopCarryMs_(0), SceneUpAxis_(1), SceneFrontAxis_(2)
{
    IdentityOrient(&SceneForwardCsys_.Rotation);
    SceneForwardCsys_.Translation.x = 0.0f;
    SceneForwardCsys_.Translation.y = 0.0f;
    SceneForwardCsys_.Translation.z = 0.0f;
}

// Retail sets the scene's axes (+0x98/+0x94) only from a SCENE entity, and
// binds every entity's output pointers to them after construction. Until a
// SCENE writes them retail leaves them unwritten; the 1/2 defaults are the port's.
void FThornScene::BuildInverseCsys(Transform* Out12) const
{
    InvertParentCsys(Out12, &SceneForwardCsys_);
}

void FThornScene::BindEntitySceneAxes(FThornEntity* Entity)
{
    if (Entity->DescriptorOwnerType == kThornEntityScene)
    {
        SceneUpAxis_ = Entity->Snap.SceneBlob[5];
        SceneFrontAxis_ = Entity->Snap.SceneBlob[6];
    }
    // Borrows the forward frame, not the inverse cache.
    Entity->ParentCsys = &SceneForwardCsys_;
    Entity->UpAxisSink = &SceneUpAxis_;
    Entity->FrontAxisSink = &SceneFrontAxis_;
}

void FThornScene::RegisterFrameEntity(FThornEntity* Entity)
{
    const int Type = Entity->DescriptorOwnerType;
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable ||
        Type == kThornEntitySubScene || Type == kThornEntityPsys)
        FrameEntities_->push_back(Entity);
}

void FThornScene::BindEventSceneAxes(FThornEvent* Event)
{
    // Event +0x20 up / +0x24 front point at the scene's fields; every event
    // borrows the same forward scene frame.
    Event->ParentCsys = &SceneForwardCsys_;
    Event->bOwnsParentCsys = 0;
    Event->SceneUpAxis = &SceneUpAxis_;
    Event->SceneFrontAxis = &SceneFrontAxis_;
}

FThornScene::~FThornScene()
{
    // ~FScriptEngine calls Clear first; this frees only storage.
    FrameEntities_.reset();
    StopTimes_.reset();
    delete[] reinterpret_cast<unsigned char*>(Active_);
    Active_ = nullptr;
    ActiveCount_ = 0;
    ActiveCap_ = 0;
    delete[] reinterpret_cast<unsigned char*>(Pending_);
    Pending_ = nullptr;
    PendingCount_ = 0;
    PendingCap_ = 0;
    PendingBegin_ = 0;
    delete[] reinterpret_cast<unsigned char*>(Master_);
    Master_ = nullptr;
    MasterCount_ = 0;
    MasterCap_ = 0;
}

void FThornScene::Clear()
{
    // FUN_06f37f70: clears the ranges but keeps vector storage and the list
    // sentinel.
    StopAll();
    FrameEntities_->clear();
    // Pending aliases are cleared before the events are deleted.
    PendingCount_ = 0;
    PendingBegin_ = 0;
    if (MasterCount_ != 0)
    {
        FThornEvent** EventSlot = Master_;
        do
        {
            FThornEvent::DestroyNative(*EventSlot);
            *EventSlot = nullptr;
            ++EventSlot;
        } while (EventSlot != Master_ + MasterCount_);
    }
    MasterCount_ = 0;
    for (std::map<int, FThornEntity*>::iterator Entry = Entities_.begin();
         Entry != Entities_.end(); ++Entry)
    {
        DestroyEntity(Entry->second);
        Entry->second = nullptr;
    }
    Entities_.clear();
    EntityCount_ = 0;
    DurationMs_ = 0;
    // Retail does not reset the clocks (+0x24/+0x28).
    PlaybackState_ = 0;
}

void FThornScene::GrowMaster()
{
    GrowPointerArray(reinterpret_cast<void***>(&Master_), &MasterCap_, MasterCount_ + 1,
                     static_cast<int>(sizeof(FThornEvent*)));
}

void FThornScene::GrowPending()
{
    GrowPointerArray(reinterpret_cast<void***>(&Pending_), &PendingCap_, PendingCount_ + 1,
                     static_cast<int>(sizeof(FThornEvent*)));
}

void FThornScene::GrowActive()
{
    GrowPointerArray(reinterpret_cast<void***>(&Active_), &ActiveCap_, ActiveCount_ + 1,
                     static_cast<int>(sizeof(FThornEvent*)));
}

FThornMonitorLinks::FThornMonitorLinks()
    : Sentinel(&InactiveSentinel), Count(0)
{
    Sentinel->Next = Sentinel;
    Sentinel->Previous = Sentinel;
    Sentinel->Entity = nullptr;
}

FThornMonitorLinks::~FThornMonitorLinks()
{
    Reset();
}

void FThornMonitorLinks::Initialize()
{
    // Allocated only after common entity construction (FUN_06f41144 area).
    OwnedSentinel.reset(new FThornMonitorLinkNode);
    Sentinel = OwnedSentinel.get();
    Sentinel->Next = Sentinel;
    Sentinel->Previous = Sentinel;
    Sentinel->Entity = nullptr;
    Count = 0;
}

void FThornMonitorLinks::Reset()
{
    Clear();
    OwnedSentinel.reset();
    Sentinel = &InactiveSentinel;
    Sentinel->Next = Sentinel;
    Sentinel->Previous = Sentinel;
    Sentinel->Entity = nullptr;
    Count = 0;
}

void FThornMonitorLinks::Erase(FThornMonitorLinkNode* Node)
{
    Node->Previous->Next = Node->Next;
    Node->Next->Previous = Node->Previous;
    delete Node;
    --Count;
}

void FThornMonitorLinks::Clear()
{
    FThornMonitorLinkNode* Node = Sentinel->Next;
    while (Node != Sentinel)
    {
        FThornMonitorLinkNode* Next = Node->Next;
        Erase(Node);
        Node = Next;
    }
}

// FUN_06f3fcc0: unsigned minimum length, _memicmp, then length tie-break.
bool FThornUserPropLess::operator()(const std::string& Left, const std::string& Right) const
{
    const size_t Length = Left.size() < Right.size() ? Left.size() : Right.size();
    const int Comparison = _memicmp(Left.data(), Right.data(), Length);
    if (Comparison != 0)
        return Comparison < 0;
    return Left.size() < Right.size();
}

FThornEntity::FThornEntity()
    : NameStorage(std::in_place), TemplateStorage(std::in_place), UserProps(std::in_place)
{
    std::memset(&Snap, 0, sizeof(FThornEntity) - offsetof(FThornEntity, Snap));
    bMotionPathDescriptorOwner = false;
    bMotionPathConstructionComplete = false;
    IdentityOrient(&Snap.Orient);
    Snap.Fovh = 60.0f;
    Snap.NearPlane = 0.1f;
    Snap.FarPlane = 100000.0f;
    Snap.Aspect = 4.0f / 3.0f;
    BoundControlType = -1;
    InitializeLightEntityPortState(this);
}

// Construction completes before LAB_06f3a3f0 binds or publishes the entity.
FThornEntity::FThornEntity(int Type, const FThornEntitySnap* Desc, FThornScene* OwnerScene)
    : FThornEntity()
{
    DescriptorOwnerType = Type;
    // Common construction reads strings before the original descriptor pose.
    AssignEntityName(Desc->Name);
    AssignEntityTemplate(Desc->TemplateName);
    std::memcpy(&LoadPos, &Desc->Pos, sizeof(LoadPos));
    std::memcpy(&LoadOrient, &Desc->Orient, sizeof(LoadOrient));
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable)
        Snap.FloorHeight = 0.0f;
    else if (Type == kThornEntityCamera || Type == kThornEntityMonitor)
    {
        MonitorLinks.Initialize();
        Snap.Fovh = 60.0f;
        Snap.NearPlane = 0.1f;
        Snap.FarPlane = 100000.0f;
        Snap.Aspect = 4.0f / 3.0f;
    }
    else if (Type == kThornEntityLight)
        ThornInitLightEntityDefaults(&Snap.Light);
    else if (Type == kThornEntitySound)
        InitializeSoundDefaults();
    else if (Type == kThornEntityScene || Type == kThornEntitySubScene)
    {
        std::memset(Snap.SceneBlob, 0, sizeof(Snap.SceneBlob));
        Snap.SceneBlob[5] = 6;
        Snap.SceneBlob[6] = 6;
    }
    else if (Type == kThornEntityMotionPath)
    {
        Snap.MotionPath = {nullptr, nullptr};
        bMotionPathDescriptorOwner = true;
    }
    else if (Type == kThornEntityPsys)
        Snap.PsysSparam = 0.0f;
    OwnerScene->ApplyEntitySnap(this, Desc);
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable)
    {
        // The typed constructors replay floor and pose after ApplyDesc.
        std::memcpy(&FloorHeight, &Snap.FloorHeight, sizeof(FloorHeight));
        InnerSetPos(&LoadPos);
        InnerSetOrient(&LoadOrient);
    }
    else if (Type == kThornEntityCamera || Type == kThornEntityMonitor)
    {
        ApplyCameraProps(this, &Snap.Fovh);
        InnerSetPos(&LoadPos);
        InnerSetOrient(&LoadOrient);
    }
    else if (Type == kThornEntityLight)
    {
        ApplyEntityLightRuntimeProps(this, &Snap.Light);
        InnerSetPos(&LoadPos);
        InnerSetOrient(&LoadOrient);
    }
    else if (Type == kThornEntityMarker || Type == kThornEntityHardpoint)
    {
        InnerSetPos(&LoadPos);
        InnerSetOrient(&LoadOrient);
    }
    else if (Type == kThornEntitySound)
    {
        ApplyAudioProps(this, Snap.Audio);
        InnerSetPos(&LoadPos);
        InnerSetOrient(&LoadOrient);
    }
    else if (Type == kThornEntityScene || Type == kThornEntitySubScene)
    {
        ApplySceneBlob(this, Snap.SceneBlob, 0xFF);
        InnerSetPos(&LoadPos);
        InnerSetOrient(&LoadOrient);
    }
    else if (Type == kThornEntityPsys)
    {
        InnerSetPos(&LoadPos);
        InnerSetOrient(&LoadOrient);
    }
    // The MOTION_PATH constructor FUN_06f426f0 follows its ApplyDesc with
    // FUN_06f427d0; the update slot (LAB_06f38bd0) only re-applies the
    // descriptor, so this binding belongs to construction alone.
    if (Type == kThornEntityMotionPath)
    {
        BindMotionPathFromDescObject();
        InnerSetPos(&LoadPos);
        InnerSetOrient(&LoadOrient);
        bMotionPathConstructionComplete = true;
    }
}

FThornEntity::~FThornEntity() noexcept(false)
{
    struct FCommonTeardownStage
    {
        FThornEntity* Entity;
        ~FCommonTeardownStage() noexcept(false) { Entity->ReleaseCommonEntityState(); }
    };
    FCommonTeardownStage CommonTeardown{this};
    // Retail's typed destructors release their specialized controls before
    // the common entity destructor. MOTION_PATH (FUN_06f42cb0) releases +0xe8
    // (bound path object) and +0xcc (descriptor's path object) before the
    // path-type string and +0xd0.
    if (bMotionPathDescriptorOwner)
    {
        FThornControlReleaseStage<IMotionPath> RuntimeTeardown(MotionPath);
        struct FPathTypeTeardownStage
        {
            FThornEntity* Entity;
            ~FPathTypeTeardownStage()
            {
                delete[] Entity->PathType;
                Entity->PathType = nullptr;
                Entity->PathTypeCapacity = 0;
            }
        };
        FPathTypeTeardownStage PathTypeTeardown{this};
        // Each pointer is read after the preceding Release. A constructor
        // (FUN_06f426f0) unwind has only the string/runtime/common stages.
        if (bMotionPathConstructionComplete)
        {
            if (MotionPathBoundObject != nullptr)
                MotionPathBoundObject->Release();
            if (Snap.MotionPath.MotionPath != nullptr)
                Snap.MotionPath.MotionPath->Release();
        }
        // The stages above free the string, then +0xd0, even if a Release throws.
    }
    else
        ReleaseControl(MotionPathBoundObject);
    ReleaseControl(ParsedMotionPathDescObject);
    const bool bCameraOwner = DescriptorOwnerType == kThornEntityCamera ||
                              DescriptorOwnerType == kThornEntityMonitor;
    const bool bPsysOwner = DescriptorOwnerType == kThornEntityPsys;
    const bool bSceneOwner = DescriptorOwnerType == kThornEntityScene ||
                             DescriptorOwnerType == kThornEntitySubScene;
    if (bSceneOwner)
    {
        FThornControlReleaseStage<ILightControl> LightTeardown(LightControl);
        FThornControlReleaseStage<IScriptControl> ScriptTeardown(ScriptControl);
        FThornControlReleaseStage<IFogControl> FogTeardown(FogControl);
        FThornControlReleaseStage<IReverbControl> ReverbTeardown(ReverbControl);
        ReleaseControl(SubtitleControl);
    }
    else if (bPsysOwner)
    {
        // FUN_06f434f0 adds only +0xcc; the common base frees strings and
        // UserProps before +0x14/+0x10.
        ReleaseControl(PsysControl);
    }
    else if (bCameraOwner)
    {
        // FUN_06f41210, unlike Detach (FUN_06f417e0): no reciprocal
        // UnbindMonitorChildren, and +0xe8 is released before +0xe4.
        struct FMonitorTeardownStage
        {
            FThornMonitorLinks& Links;
            ~FMonitorTeardownStage() { Links.Reset(); }
        };
        FMonitorTeardownStage MonitorTeardown{MonitorLinks};
        FThornControlReleaseStage<ICameraControl> CameraTeardown(CameraControl);
        ReleaseControl(SubtitleControl);
    }
    else
        ReleaseDerivedControls();
    // Deletes each node without touching the linked entity.
    MonitorLinks.Clear();

}

void FThornEntity::ReleaseCommonEntityState()
{
    // FUN_06f405e0 (common entity destructor): frees the ref-counted strings
    // and the UserProps tree, then releases the two bound controls.
    UserProps.reset();
    TemplateStorage.reset();
    NameStorage.reset();
    delete[] PathType;
    PathType = nullptr;
    delete[] PathData;
    PathData = nullptr;
    // User is released before Geo.
    {
        // Geo is read when this stage runs, also when the User Release throws.
        FThornControlReleaseStage<IGeoTransformControl> GeoTeardown(GeoControl);
        ReleaseControl(UserEventControl);
    }
    // Borrowed (+0x4); not deleted.
    ParentCsys = nullptr;
}

char* FThornEntity::AssignEntityName(const char* Source)
{
    if (Source == nullptr)
        return nullptr;
    const std::size_t Length = std::strlen(Source);
    const std::size_t SnapshotSize = (Length + 4u) & ~std::size_t(3u);
    char* Snapshot = static_cast<char*>(_alloca(SnapshotSize));
    std::memcpy(Snapshot, Source, Length + 1u);
    NameStorage->assign(Snapshot, Length);
    return NameStorage->data();
}

char* FThornEntity::AssignEntityTemplate(const char* Source)
{
    if (Source == nullptr)
        return nullptr;
    const std::size_t Length = std::strlen(Source);
    const std::size_t SnapshotSize = (Length + 4u) & ~std::size_t(3u);
    char* Snapshot = static_cast<char*>(_alloca(SnapshotSize));
    std::memcpy(Snapshot, Source, Length + 1u);
    TemplateStorage->assign(Snapshot, Length);
    return TemplateStorage->data();
}

void FThornEntity::InnerUnused1()
{
}

void FThornEntity::InnerApplyDesc(const FThornEntitySnap* SnapIn)
{
    // Retail per-type versions: FUN_06f41c50 / FUN_06f41fe0 / FUN_06f42410 /
    // FUN_06f42a10 / FUN_06f43590. The common descriptor was already copied
    // by FThornScene::ApplyEntitySnap; this is the type-specific tail.
    // With a parent Csys bound, the incoming world pose is first converted
    // into the parent frame in place (parent inverse, then math +0x28
    // Transform for Pos and +0x20 MulMatrix for Orient); the GENRESULTs are
    // not tested.
    if (ParentCsys != nullptr)
    {
        Transform Inv;
        InvertEventParentCsys(&Inv, ParentCsys);
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        Vector LocalPos;
        SceneMathTransform(Math, &LocalPos, &Inv, &Snap.Pos);
        CopyPoseWordsForward(&Snap.Pos, &LocalPos);
        Matrix LocalOrient;
        SceneMathMulMatrix(Math, &LocalOrient, &Inv.Rotation, &Snap.Orient);
        CopyPoseWordsForward(&Snap.Orient, &LocalOrient);
    }

    // Every per-type ApplyDesc ends with FUN_06f40c40 / FUN_06f40c60: a plain
    // copy of the Snap pose into entity +0x38 / +0x44, no control publish
    // (FUN_06f40900 / FUN_06f40ab0 are called by the constructors only).
    // LoadPos/LoadOrient are what IScriptEngine slots 10/11 report.
    CopyPoseWordsForward(&LoadPos, &Snap.Pos);
    CopyPoseWordsForward(&LoadOrient, &Snap.Orient);

    if (DescriptorOwnerType == kThornEntityPsys &&
        (SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntityPsys)
        std::memcpy(&Snap.PsysSparam, &SnapIn->PsysSparam, sizeof(Snap.PsysSparam));

    if ((DescriptorOwnerType == kThornEntityScene || DescriptorOwnerType == kThornEntitySubScene) &&
        (SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntityScene)
    {
        for (int Index = 0; Index < 15; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, &SnapIn->SceneBlob[Index], sizeof(Word));
            std::memcpy(&Snap.SceneBlob[Index], &Word, sizeof(Word));
        }
        ApplySceneBlob(this, Snap.SceneBlob, 0xFF);
    }

    if (DescriptorOwnerType == kThornEntitySound)
    {
        if ((SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntitySound)
            CopyPoseWordsForward(Snap.Audio, SnapIn->Audio, 8);
        // Name/Template are read after the pose and tail.
        Snap.Name = AssignEntityName(SnapIn->Name);
        Snap.TemplateName = AssignEntityTemplate(SnapIn->TemplateName);
    }

    if (DescriptorOwnerType == kThornEntityMotionPath &&
        (SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntityMotionPath)
        ApplyMotionPathDesc(this, &SnapIn->MotionPath);

    if (DescriptorOwnerType == kThornEntityLight &&
        (SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntityLight)
    {
        // Raw 36-word tail copy.
        for (int Index = 0; Index < 36; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, reinterpret_cast<const unsigned char*>(&SnapIn->Light) +
                                   Index * sizeof(Word), sizeof(Word));
            std::memcpy(reinterpret_cast<unsigned char*>(&Snap.Light) +
                            Index * sizeof(Word), &Word, sizeof(Word));
        }
    }

    if ((DescriptorOwnerType == kThornEntityCompound || DescriptorOwnerType == kThornEntityDeformable) &&
        (SnapIn->Flags & 1) == 0 &&
        (SnapIn->Type == kThornEntityCompound || SnapIn->Type == kThornEntityDeformable))
    {
        // Only the descriptor copy (FUN_06f40f80); FUN_06f40f90 sets the runtime value.
        std::memcpy(&Snap.FloorHeight, &SnapIn->FloorHeight, sizeof(Snap.FloorHeight));
    }

    if (DescriptorOwnerType == kThornEntityCamera || DescriptorOwnerType == kThornEntityMonitor)
    {
        // The input flags/type/tail are read only after the unlink.
        UnbindMonitorChildren(this);
        if ((SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntityCamera)
        {
            // Raw stores in Near/Far/Fov/Aspect order.
            const int StoreOrder[4] = {1, 2, 0, 3};
            for (int Index = 0; Index < 4; ++Index)
            {
                const int Field = StoreOrder[Index];
                unsigned int Word;
                std::memcpy(&Word, reinterpret_cast<const unsigned char*>(&SnapIn->Fovh) +
                                   Field * sizeof(Word), sizeof(Word));
                std::memcpy(reinterpret_cast<unsigned char*>(&Snap.Fovh) +
                            Field * sizeof(Word), &Word, sizeof(Word));
            }
        }
    }
}

Vector* FThornEntity::InnerGetPos()
{
    GetRetailRelativePosition(this, &Snap.Pos);
    return &Snap.Pos;
}

Matrix* FThornEntity::InnerGetOrient()
{
    GetRetailRelativeOrientation(this, &Snap.Orient);
    return &Snap.Orient;
}

void FThornEntity::InnerSetPos(const Vector* Pos)
{
    // FUN_06f40900 / LAB_06f49f00 (vtable +0x0c). CAMERA (FUN_06f41490): after
    // the store, a non-MONITOR fans out to the +0x88 list via child +0x0c.
    for (int Index = 0; Index < 3; ++Index)
    {
        unsigned int Word;
        std::memcpy(&Word, &Pos->x + Index, sizeof(Word));
        std::memcpy(&Snap.Pos.x + Index, &Word, sizeof(Word));
    }
    if (GeoControl != nullptr)
    {
        Vector World;
        std::memcpy(&World, Pos, sizeof(World));
        if (ParentCsys != nullptr)
        {
            const FThornCsys* Parent = ParentCsys;
            I3DMathEngine* Math = AcquireSceneEventMathEngine();
            Vector Out;
            // The GENRESULT is not tested.
            SceneMathTransform(Math, &Out, Parent, Pos);
            std::memcpy(&World, &Out, sizeof(World));
        }
        GeoControl->SetPosition(&World);
    }
    // Only CAMERA/MONITOR entities fan out (FUN_06f41490).
    if ((DescriptorOwnerType == kThornEntityCamera || DescriptorOwnerType == kThornEntityMonitor) &&
        Snap.Type != kThornEntityMonitor)
    {
        FThornMonitorLinkNode* Node = MonitorLinks.Sentinel->Next;
        while (Node != MonitorLinks.Sentinel)
        {
            Node->Entity->InnerSetPos(Pos);
            Node = Node->Next; // Read after the child callback.
        }
    }
}

void FThornEntity::InnerSetOrient(const Matrix* Orient)
{
    // FUN_06f40ab0 / LAB_06f49f20 (vtable +0x10). CAMERA (FUN_06f414d0): after
    // the store, fans out to the linked list via child +0x10; MONITOR nodes
    // are terminal. Forward word copy (REP MOVSD), defined for overlap.
    for (int Index = 0; Index < 9; ++Index)
    {
        unsigned int Word;
        std::memcpy(&Word, &Orient->E[0][0] + Index, sizeof(Word));
        std::memcpy(&Snap.Orient.E[0][0] + Index, &Word, sizeof(Word));
    }
    if (GeoControl != nullptr)
    {
        Matrix World;
        std::memcpy(&World, Orient, sizeof(World));
        if (ParentCsys != nullptr)
        {
            const FThornCsys* Parent = ParentCsys;
            I3DMathEngine* Math = AcquireSceneEventMathEngine();
            Matrix Out9;
            // The GENRESULT is not tested.
            SceneMathMulMatrix(Math, &Out9, &Parent->Rotation, Orient);
            std::memcpy(&World, &Out9, sizeof(World));
        }
        GeoControl->SetOrientation(&World);
    }
    // Only CAMERA/MONITOR entities fan out (FUN_06f414d0).
    if ((DescriptorOwnerType == kThornEntityCamera || DescriptorOwnerType == kThornEntityMonitor) &&
        Snap.Type != kThornEntityMonitor)
    {
        FThornMonitorLinkNode* Node = MonitorLinks.Sentinel->Next;
        while (Node != MonitorLinks.Sentinel)
        {
            Node->Entity->InnerSetOrient(Orient);
            Node = Node->Next; // Read after the child callback.
        }
    }
}

void FThornEntity::CaptureAudioProps(float* Out8)
{
    // Optional GET (result ignored), then a raw forward 8-word copy.
    ISoundControl* Control = SoundControl;
    if (Control != nullptr)
        Control->GetAudioProps(Audio, -1);
    CopyPoseWordsForward(Out8, Audio, 8);
}

void FThornEntity::InitializeSoundDefaults()
{
    // FUN_06f42210 descriptor defaults: dmin=1, dmax=1e9, integer cones=360.
    // The constructor copies them to the runtime cache after ApplyDesc.
    std::memset(Audio, 0, sizeof(Audio));
    AudioMask = 0;
    Audio[2] = 1.0f;
    Audio[3] = 1000000000.0f;
    const int DefaultAinAout = 0x168;
    std::memcpy(&Audio[4], &DefaultAinAout, sizeof(DefaultAinAout));
    std::memcpy(&Audio[5], &DefaultAinAout, sizeof(DefaultAinAout));
    std::memcpy(Snap.Audio, Audio, sizeof(Snap.Audio));
}

void FThornEntity::ReleaseLightControls()
{
    // FUN_06f41a20: +0x15c (ID3DLightControl), then +0x158 (ILightControl).
    FThornControlReleaseStage<ILightControl> LightTeardown(LightControl);
    ReleaseControl(D3dLightControl);
}

void FThornEntity::InnerUnbindControls()
{
    // Entity vtable +0x24; the release orders differ from the destructors'.
    // Keyed on the constructed type, even if ApplyDesc changed Snap.Type.
    const int Type = DescriptorOwnerType;
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable)
    {
        // FUN_06f410a0: +0x10, +0xd0, +0xcc, +0x14, +0xd4.
        ReleaseControl(GeoControl);
        ReleaseControl(IkControl);
        ReleaseControl(CompoundControl);
        ReleaseControl(UserEventControl);
        ReleaseControl(SubtitleControl);
    }
    else if (Type == kThornEntityCamera || Type == kThornEntityMonitor)
    {
        // FUN_06f417e0: child unlink, +0xe4, +0x10, +0x14, +0xe8.
        UnbindMonitorChildren(this);
        ReleaseControl(CameraControl);
        ReleaseControl(GeoControl);
        ReleaseControl(UserEventControl);
        ReleaseControl(SubtitleControl);
    }
    else if (Type == kThornEntityLight)
    {
        // FUN_06f41ed0: +0x158 before +0x15c, unlike the destructor (FUN_06f41a20).
        ReleaseControl(LightControl);
        ReleaseControl(D3dLightControl);
        ReleaseControl(GeoControl);
        ReleaseControl(UserEventControl);
    }
    else if (Type == kThornEntitySound)
    {
        // FUN_06f42690: +0x10, +0xe8, +0x14, +0xec.
        ReleaseControl(GeoControl);
        ReleaseControl(SoundControl);
        ReleaseControl(UserEventControl);
        ReleaseControl(SubtitleControl);
    }
    else if (Type == kThornEntityScene || Type == kThornEntitySubScene)
    {
        ReleaseSceneBoundControls();
    }
    else if (Type == kThornEntityMotionPath)
    {
        ReleaseMotionPathControls();
    }
    else if (Type == kThornEntityPsys)
    {
        // FUN_06f437e0 (PSYS): +0x10, +0xcc, +0x14.
        ReleaseControl(GeoControl);
        ReleaseControl(PsysControl);
        ReleaseControl(UserEventControl);
    }
    else
    {
        // FUN_06f421e0: +0x10, +0x14.
        ReleaseControl(GeoControl);
        ReleaseControl(UserEventControl);
    }
    bBound = 0;
}

void FThornEntity::ReleaseDerivedControls()
{
    // Keyed on the constructed type. The common User/Geo controls are released
    // later, after the common data teardown.
    const int Type = DescriptorOwnerType;
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable)
    {
        // Each field is read only when its stage runs.
        FThornControlReleaseStage<ICompoundControl> CompoundTeardown(CompoundControl);
        FThornControlReleaseStage<IIKControl> IkTeardown(IkControl);
        ReleaseControl(SubtitleControl);
        return;
    }
    else if (Type == kThornEntityLight)
        ReleaseLightControls();
    else if (Type == kThornEntitySound)
    {
        FThornControlReleaseStage<ISoundControl> SoundTeardown(SoundControl);
        ReleaseControl(SubtitleControl);
    }
    else if (Type == kThornEntityMotionPath)
        ReleaseControl(MotionPath);
    bBound = 0;
}

void FThornEntity::ReleaseSceneBoundControls()
{
    // FUN_06f433c0: +0x10 Geo, +0x104 Light, +0x14 UserEvent,
    // +0x108 Script, +0x10c Fog, +0x114 Subtitle. Retail does not
    // release +0x110 Reverb on this unbind path; the scene destructor
    // releases it later through ReleaseDerivedControls().
    ReleaseControl(GeoControl);
    ReleaseControl(LightControl);
    ReleaseControl(UserEventControl);
    ReleaseControl(ScriptControl);
    ReleaseControl(FogControl);
    ReleaseControl(SubtitleControl);
    bBound = 0;
}

void FThornEntity::ReleaseMotionPathControls()
{
    // FUN_06f42c60: +0xd0 (IMotionPath), +0x10 (IGeo), +0x14 (IUserEvent);
    // retail order, not the reverse of the bind sequence.
    ReleaseControl(MotionPath);
    ReleaseControl(GeoControl);
    ReleaseControl(UserEventControl);
    bBound = 0;
}

void FThornEntity::BindMotionPathFromDescObject()
{
    // FUN_06f427d0, called by the MOTION_PATH constructor (FUN_06f426f0) with
    // the entity's own Snap.MotionPath pair (+0xc8). The +0xe4 pointer is a
    // second cache of the one +0xd4 string, set before any interface call;
    // null clears only the cache. Retail's self-assignment/allocation details
    // for it are unconfirmed.
    const char* Source = Snap.MotionPath.PathType;
    MotionPathBoundType = Source != nullptr ? AssignMotionPathType(this, Source, true) : nullptr;
    // Release the old +0xe8, +0xe8 = +0xcc (AddRef), release +0xd0, then QI
    // "1.11_IMotionPath" from +0xe8 straight into +0xd0.
    if (MotionPathBoundObject != nullptr)
        MotionPathBoundObject->Release();
    // +0xcc is reread after the Release.
    MotionPathBoundObject = Snap.MotionPath.MotionPath;
    if (MotionPathBoundObject == nullptr)
        return;
    MotionPathBoundObject->AddRef();
    ReleaseControl(MotionPath);
    MotionPathBoundObject->QueryInterface(Iids::MotionPath,
                                          reinterpret_cast<void**>(&MotionPath));
}

FThornEntity* FThornScene::AddEntity()
{
    FThornEntity* Entity = new (std::nothrow) FThornEntity();
    if (Entity == nullptr)
        return nullptr;
    // A key collision keeps the existing entity.
    Entities_.insert(std::make_pair(EntityCount_, Entity));
    EntityCount_ = static_cast<int>(static_cast<unsigned int>(EntityCount_) + 1u);
    return Entity;
}

FThornEvent* FThornScene::AddEvent(int DescriptorStorageType)
{
    GrowMaster();
    if (Master_ == nullptr || MasterCount_ >= MasterCap_)
        return nullptr;
    FThornEvent* Event = new (std::nothrow) FThornEvent(DescriptorStorageType);
    if (Event == nullptr)
        return nullptr;
    BindEventSceneAxes(Event);
    Master_[MasterCount_] = Event;
    MasterCount_ = MasterCount_ + 1;
    return Event;
}

unsigned long FThornScene::EntityCount() const
{
    return static_cast<unsigned long>(EntityCount_);
}

unsigned long FThornScene::LiveEntityCount() const
{
    return static_cast<unsigned long>(Entities_.size());
}

bool FThornScene::EntityExists(int Index) const
{
    if (static_cast<unsigned int>(Index) >= static_cast<unsigned int>(EntityCount_))
        return false;
    return Entities_.find(Index) != Entities_.end();
}

void FThornScene::DestroyEntity(FThornEntity* Entity)
{
    // RemoveEntityAt unbinds before calling this; controls and common data
    // are released only in ~FThornEntity().
    delete Entity;
}

namespace
{
    // FUN_06f40380; callers pass the common packet by value.
    void PackEntitySnapCommon(FThornEntitySnap* Destination,
                              const FThornEntitySnap& Snapshot)
    {
        Destination->Type = Snapshot.Type;
        std::memcpy(&Destination->Orient, &Snapshot.Orient, sizeof(Destination->Orient));
        std::memcpy(&Destination->Pos, &Snapshot.Pos, sizeof(Destination->Pos));
        Destination->LtGrp = Snapshot.LtGrp;
        Destination->SrtGrp = Snapshot.SrtGrp;
        Destination->Flags = Snapshot.Flags;
        Destination->UsrFlg = Snapshot.UsrFlg;
        Destination->Name = nullptr;
        Destination->TemplateName = nullptr;
    }
}

void FThornScene::ApplyEntitySnap(FThornEntity* Entity, const FThornEntitySnap* Snap)
{
    // Retail passes a 17-dword snapshot of the common part; Name/Template are
    // reread from the input afterwards.
    FThornEntitySnap CommonSnapshot;
    std::memcpy(&CommonSnapshot, Snap, offsetof(FThornEntitySnap, Fovh));
    PackEntitySnapCommon(&Entity->Snap, CommonSnapshot);
    if (Entity->DescriptorOwnerType != kThornEntitySound)
    {
        Entity->Snap.Name = Entity->AssignEntityName(Snap->Name);
        Entity->Snap.TemplateName = Entity->AssignEntityTemplate(Snap->TemplateName);
    }
    Entity->InnerApplyDesc(Snap);
}

bool FThornScene::EventAllowsMissingSecondary(int Type)
{
    // Table at 06f3a1e8, indexed by type-2: types 2/6/8/9/11 fail when the
    // secondary is < 0; all others (type 1 included) continue.
    if (Type == kThornTypeSetCamera || Type == kThornTypePathAnim ||
        Type == kThornTypeAttachEntity || Type == kThornTypeConnectHardpoints ||
        Type == kThornTypeStartIk)
        return false;
    return true;
}

FThornEvent** FThornScene::EraseMasterEventAt(FThornEvent** Slot)
{
    // Removed from MASTER before the first Stop callback.
    FThornEvent* Event = *Slot;
    FThornEvent** MasterEnd = Master_ + MasterCount_;
    for (FThornEvent** Read = Slot + 1; Read != MasterEnd; ++Read)
        *(Read - 1) = *Read;
    MasterCount_ = MasterCount_ - 1;

    if (ActiveCount_ != 0)
    {
        // Retail keeps these cursors through Stop, but rereads the live end.
        FThornEvent** ActiveSlot = Active_;
        FThornEvent** Successor = ActiveSlot + 1;
        do
        {
            if (*ActiveSlot == Event)
            {
                Event->Stop(this);
                FThornEvent** ActiveEnd = Active_ + ActiveCount_;
                for (FThornEvent** Read = Successor; Read != ActiveEnd; ++Read)
                    *(Read - 1) = *Read;
                ActiveCount_ = ActiveCount_ - 1;
            }
            else
            {
                ++ActiveSlot;
                ++Successor;
            }
        } while (ActiveSlot != Active_ + ActiveCount_);
    }

    // Only the first PENDING match is erased; STOPLIST is untouched.
    for (int PendingIndex = 0; PendingIndex != PendingCount_; ++PendingIndex)
    {
        if (Pending_[PendingIndex] != Event)
            continue;
        for (int Shift = PendingIndex + 1; Shift != PendingCount_; ++Shift)
            Pending_[Shift - 1] = Pending_[Shift];
        PendingCount_ = PendingCount_ - 1;
        break;
    }
    FThornEvent::DestroyNative(Event);
    return Slot;
}

void FThornScene::FinalizeLoad(int DurationMs)
{
    // Tail of the scene load (IScriptEngine slot 4): store the duration, then
    // sort MASTER only.
    DurationMs_ = DurationMs;
    SortMasterEvents(Master_, MasterCount_);
}

int FThornScene::FindEntityByName(const char* Name) const
{
    if (Name == nullptr)
        return -1;
    for (std::map<int, FThornEntity*>::const_iterator Entry = Entities_.begin();
         Entry != Entities_.end(); ++Entry)
    {
        FThornEntity* Entity = Entry->second;
        if (Entity == nullptr)
            continue;
        const char* EntityName = Entity->Snap.Name;
        if (EntityName != nullptr && std::strcmp(EntityName, Name) == 0)
            return Entry->first;
    }
    return -1;
}

FThornEntity** FThornScene::FindEntityEntry(int Index)
{
    // FUN_06f4aa40 / FUN_06f4af00: signed lower_bound plus an equality test.
    // The returned address survives insertions; erasing the entry invalidates
    // it, as with retail's tree node.
    std::map<int, FThornEntity*>::iterator Entry = Entities_.lower_bound(Index);
    if (Entry == Entities_.end() || Index < Entry->first)
        return nullptr;
    return &Entry->second;
}

FThornEntity* FThornScene::EntityAt(int Index)
{
    FThornEntity** Entry = FindEntityEntry(Index);
    return Entry != nullptr ? *Entry : nullptr;
}

const FThornEntity* FThornScene::EntityAt(int Index) const
{
    std::map<int, FThornEntity*>::const_iterator Entry = Entities_.find(Index);
    return Entry != Entities_.end() ? Entry->second : nullptr;
}

void* FThornScene::EntityHandle(int Index)
{
    // Slot 7 enumerates signed keys in order; Index is an unsigned ordinal.
    std::map<int, FThornEntity*>::iterator Entry = Entities_.begin();
    unsigned int Seen = 0;
    const unsigned int Ordinal = static_cast<unsigned int>(Index);
    while (Seen < Ordinal && Entry != Entities_.end())
    {
        ++Entry;
        ++Seen;
    }
    if (Entry == Entities_.end())
        return reinterpret_cast<void*>(static_cast<INT_PTR>(-1));
    return reinterpret_cast<void*>(static_cast<INT_PTR>(Entry->first));
}

FThornEntitySnap* FThornScene::EntitySnap(int Index)
{
    if (!EntityExists(Index))
        return nullptr;
    return &EntityAt(Index)->Snap;
}

FThornEntitySnap* FThornScene::EntitySnapInorder(int Index)
{
    if (Index < 0)
        return nullptr;
    int Seen = 0;
    for (std::map<int, FThornEntity*>::iterator Entry = Entities_.begin();
         Entry != Entities_.end(); ++Entry)
    {
        if (Seen == Index)
            return &Entry->second->Snap;
        ++Seen;
    }
    return nullptr;
}

unsigned long FThornScene::UserPropCount(int Index) const
{
    // LAB_06f38ca0: lookup by key, entity +0x80; a miss gives 0.
    if (!EntityExists(Index))
        return 0;
    return static_cast<unsigned long>(EntityAt(Index)->UserProps->size());
}

const char* FThornScene::UserPropKey(int Index, int N) const
{
    // LAB_06f38d20: slot 31, then N in-order steps from entity +0x74.
    if (!EntityExists(Index))
        return nullptr;
    return NthSortedUserPropKey(EntityAt(Index), N);
}

GENRESULT FThornScene::GetEntityPos(int Index, Vector* OutVec3)
{
    if (!EntityExists(Index))
        return GR_GENERIC;
    // LAB_06f38950: Index >= this+0xa0 or a missing entry -> -1; else copy
    // entity +0x38 (the ApplyDesc-time pose, not the live Snap.Pos) and, with a
    // parent Csys, math +0x28 Transform through it.
    FThornEntity* Entity = EntityAt(Index);
    CopyPoseWordsForward(OutVec3, &Entity->LoadPos);
    const FThornCsys* Parent = Entity->ParentCsys;
    if (Parent != nullptr)
    {
        I3DMathEngine* Math = AcquireSceneCsysMathEngine();
        Transform ParentInput;
        Vector PositionInput;
        Vector World;
        std::memcpy(&ParentInput.Rotation, &Parent->Rotation, sizeof(ParentInput.Rotation));
        std::memcpy(&ParentInput.Translation, &Parent->Translation, sizeof(ParentInput.Translation));
        std::memcpy(&PositionInput, OutVec3, sizeof(PositionInput));
        Math->TransformXform(World, ParentInput, PositionInput);
        // All three result words are loaded before any is stored.
        unsigned int ResultWords[3];
        std::memcpy(ResultWords, &World, sizeof(ResultWords));
        std::memcpy(OutVec3, ResultWords, sizeof(ResultWords));
    }
    return GR_OK;
}

GENRESULT FThornScene::GetEntityOrient(int Index, Matrix* OutMat9)
{
    if (!EntityExists(Index))
        return GR_GENERIC;
    // LAB_06f38a80: same lookup as slot 10, then 9 dwords from entity +0x44
    // and, with a parent Csys, math +0x20 MulMatrix by its orientation.
    FThornEntity* Entity = EntityAt(Index);
    CopyPoseWordsForward(OutMat9, &Entity->LoadOrient);
    const FThornCsys* Parent = Entity->ParentCsys;
    if (Parent != nullptr)
    {
        I3DMathEngine* Math = AcquireSceneCsysMathEngine();
        Matrix ParentOrientation;
        Matrix OrientationInput;
        Matrix World;
        std::memcpy(&ParentOrientation, &Parent->Rotation, sizeof(ParentOrientation));
        std::memcpy(&OrientationInput, OutMat9, sizeof(OrientationInput));
        Math->MulMatrix(World, ParentOrientation, OrientationInput);
        float WorldWords[9];
        std::memcpy(WorldWords, &World, sizeof(WorldWords));
        CopyPoseWordsForward(&OutMat9->E[0][0], WorldWords, 9);
    }
    return GR_OK;
}

GENRESULT FThornScene::Attach(int Index, void* EngineObj)
{
    // Slot 12 (LAB_06f392a0): lookup + entity vtable +0x20. Does not AddRef
    // EngineObj. Retail: no null test at any level; every type's bind QIs
    // EngineObj first, so a null one faults. Callers use Detach (slot 13).
    FThornEntity* Entity = EntityAt(Index);
    if (Entity == nullptr)
        return GR_GENERIC;

    // Keyed on the constructed type, even if slot 2 changed the descriptor Type.
    const int EntType = Entity->DescriptorOwnerType;
    Entity->BoundControlType = EntType;

    int Result = -1;
    if (EntType == kThornEntityCompound || EntType == kThornEntityDeformable)
    {
        BindQi(EngineObj, Iids::CompoundControl, Entity->CompoundControl, Result);
        BindQi(EngineObj, Iids::IKControl, Entity->IkControl, Result);
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
        BindQi(EngineObj, Iids::SubtitleControl, Entity->SubtitleControl, Result);
    }
    else if (EntType == kThornEntityCamera || EntType == kThornEntityMonitor)
    {
        // FUN_06f41700: Geo, Camera, UserEvent, Subtitle, in this order.
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::CameraControl, Entity->CameraControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
        BindQi(EngineObj, Iids::SubtitleControl, Entity->SubtitleControl, Result);
    }
    else if (EntType == kThornEntityLight)
    {
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::LightControl, Entity->LightControl, Result);
        BindQi(EngineObj, Iids::D3DLightControl, Entity->D3dLightControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
    }
    else if (EntType == kThornEntitySound)
    {
        BindQi(EngineObj, Iids::SoundControl, Entity->SoundControl, Result);
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
        BindQi(EngineObj, Iids::SubtitleControl, Entity->SubtitleControl, Result);
    }
    else if (EntType == kThornEntityMarker || EntType == kThornEntityHardpoint)
    {
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
    }
    else if (EntType == kThornEntityScene || EntType == kThornEntitySubScene)
    {
        // LAB_06f43230.
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::LightControl, Entity->LightControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
        BindQi(EngineObj, Iids::ScriptControl, Entity->ScriptControl, Result);
        BindQi(EngineObj, Iids::FogControl, Entity->FogControl, Result);
        BindQi(EngineObj, Iids::ReverbControl, Entity->ReverbControl, Result);
        BindQi(EngineObj, Iids::SubtitleControl, Entity->SubtitleControl, Result);
        // A sentinel 6 axis (FUN_06f42d70) is replaced in descriptor and live blob alike.
        if (Entity->Snap.SceneBlob[6] == 6)
        {
            Entity->Snap.SceneBlob[6] = 5;
            Entity->LiveSceneBlob[6] = 5;
        }
        if (Entity->Snap.SceneBlob[5] == 6)
        {
            Entity->Snap.SceneBlob[5] = 1;
            Entity->LiveSceneBlob[5] = 1;
        }
    }
    else if (EntType == kThornEntityMotionPath)
    {
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::MotionPath, Entity->MotionPath, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
    }
    else if (EntType == kThornEntityPsys)
    {
        // FUN_06f43730.
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::PSysControl, Entity->PsysControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
    }
    else
    {
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
    }

    Entity->bBound = (Result == 0) ? 1 : 0;
    Entity->BindState = 2;
    return (Result == 0) ? GR_OK : GR_GENERIC;
}

GENRESULT FThornScene::Detach(int Index)
{
    // Slot 13: entity vtable +0x24 unbind. Not Attach(index, NULL).
    // CAMERA FUN_06f417e0: FUN_06f416b0 (drop child monitors) then Release.
    FThornEntity* Entity = EntityAt(Index);
    if (Entity == nullptr)
        return GR_GENERIC;
    Entity->InnerUnbindControls();
    Entity->bBound = 0;
    Entity->BindState = 0;
    return GR_OK;
}


void FThornScene::PushBoundGeoFromScene(const Transform& OldInverse, bool bOrient)
{
    // FUN_06f3b530/06f3b6c0: the entity pointer is kept across its getters and
    // setters; the iterator advances only after them.
    for (std::map<int, FThornEntity*>::iterator Entry = Entities_.begin();
         Entry != Entities_.end(); ++Entry)
    {
        FThornEntity* Entity = Entry->second;
        if (Entity == nullptr || Entity->GeoControl == nullptr)
            continue;
        Vector PositionWords;
        Matrix OrientationWords;
        Entity->GeoControl->GetPosition(&PositionWords);
        // GeoControl is reloaded without a second null check; orientation is
        // read before any math.
        if (bOrient)
            Entity->GeoControl->GetOrientation(&OrientationWords);

        I3DMathEngine* Math = AcquireSceneCsysMathEngine();
        Vector Position;
        Vector PositionResult;
        std::memcpy(&Position, &PositionWords, sizeof(Position));
        Math->TransformXform(PositionResult, OldInverse, Position);
        std::memcpy(&PositionWords, &PositionResult, sizeof(PositionWords));
        Entity->InnerSetPos(&PositionWords);

        if (bOrient)
        {
            // The math engine is reacquired after InnerSetPos; the orientation
            // was captured before it.
            Math = AcquireSceneCsysMathEngine();
            Matrix Orientation;
            Matrix OrientationResult;
            std::memcpy(&Orientation, &OrientationWords, sizeof(Orientation));
            Math->MulMatrix(OrientationResult, OldInverse.Rotation, Orientation);
            std::memcpy(&OrientationWords, &OrientationResult, sizeof(OrientationWords));
            Entity->InnerSetOrient(&OrientationWords);
        }
    }
}

GENRESULT FThornScene::UserProp(void* Handle, const char* Key, char** Out)
{
    const int Index = static_cast<int>(reinterpret_cast<INT_PTR>(Handle));
    if (!EntityExists(Index))
        return GR_GENERIC;
    FThornUserProps::iterator Prop;
    {
        // The key is built only after the entity checks.
        const std::string KeyOwner(Key, std::strlen(Key));
        Prop = EntityAt(Index)->UserProps->find(KeyOwner);
    }
    // Retail destroys the temporary key before rereading the entity.
    if (Prop == EntityAt(Index)->UserProps->end())
        return GR_GENERIC;
    *Out = Prop->second.data();
    return GR_OK;
}

GENRESULT FThornScene::SetUserProp(int Index, const char* Key, const char* Value)
{
    if (!EntityExists(Index))
        return GR_GENERIC;
    FThornUserProps::iterator Prop;
    {
        const std::string KeyOwner(Key, std::strlen(Key));
        Prop = EntityAt(Index)->UserProps->find(KeyOwner);
    }
    if (Prop != EntityAt(Index)->UserProps->end())
    {
        Prop->second.assign(Value, std::strlen(Value));
        return GR_OK;
    }
    // Value string, key string, pair copy, then a unique insert.
    const std::string ValueOwner(Value, std::strlen(Value));
    const std::string KeyOwner(Key, std::strlen(Key));
    const std::pair<std::string, std::string> Property(KeyOwner, ValueOwner);
    EntityAt(Index)->UserProps->insert(Property);
    return GR_OK;
}

GENRESULT FThornScene::AddEntityFromDesc(void* Desc, int* OutIndex)
{
    // LAB_06f3a3f0: write -1, construct, bind axes, insert, then write the
    // index and increment the count.
    *OutIndex = -1;
    const FThornEntitySnap* Snap = static_cast<const FThornEntitySnap*>(Desc);
    const int Type = Snap->Type;
    if (Type < 1 || Type > 13 || Type == 12)
        return GR_GENERIC;
    // Throwing new; retail's VC6 operator new could return null and then crash.
    FThornEntity* Entity = new FThornEntity(Type, Snap, this);
    // Frame list append after construction, before bindings and map insert.
    RegisterFrameEntity(Entity);
    BindEntitySceneAxes(Entity);
    // Unique signed-key insert; on a collision the old entity stays and the
    // new one leaks, as in retail.
    Entities_.insert(std::make_pair(EntityCount_, Entity));
    *OutIndex = EntityCount_;
    EntityCount_ = static_cast<int>(static_cast<unsigned int>(EntityCount_) + 1u);
    if (PlaybackState_ == 0)
        PlaybackState_ = kThornPlaybackLoaded;
    return GR_OK;
}

GENRESULT FThornScene::RemoveEntityAt(int Index)
{
    // FUN_06f3a7e0: erase events that reference the entity, destroy it, erase
    // the map entry. EntityCount_ is not decremented.
    if (!EntityExists(Index))
        return GR_GENERIC;
    // The node found here is kept through all event/unbind callbacks.
    std::map<int, FThornEntity*>::iterator Entry = Entities_.find(Index);
    if (MasterCount_ != 0)
    {
        FThornEvent** EventSlot = Master_;
        do
        {
            FThornEvent* Event = *EventSlot;
            if (Event->PrimaryIndex == Index || Event->SecondaryIndex == Index)
                EventSlot = EraseMasterEventAt(EventSlot);
            else
                ++EventSlot;
        } while (EventSlot != Master_ + MasterCount_);
    }
    std::list<FThornEntity*>::iterator FrameIt = FrameEntities_->begin();
    while (FrameIt != FrameEntities_->end())
    {
        if (*FrameIt == Entry->second)
            FrameIt = FrameEntities_->erase(FrameIt);
        else
            ++FrameIt;
    }
    // Entity vtable +0x24 unbind before the destructor and map erase.
    Entry->second->InnerUnbindControls();
    DestroyEntity(Entry->second);
    Entities_.erase(Entry);
    return GR_OK;
}

GENRESULT FThornScene::UpdateEntityFromDesc(int Index, void* Desc)
{
    // LAB_06f38bd0: lookup, then entity vtable +0x08 (apply desc).
    if (!EntityExists(Index))
        return GR_GENERIC;
    ApplyEntitySnap(EntityAt(Index), static_cast<const FThornEntitySnap*>(Desc));
    return GR_OK;
}

GENRESULT FThornScene::AddEventFromDesc(const void* Desc, int* OutId)
{
    // LAB_06f39b50: *OutId = -1; the primary must exist; the secondary too,
    // unless it is < 0 and the type allows that.
    *OutId = -1;
    const int Primary = ReadEventWord(Desc, 7);
    if (!EntityExists(Primary))
        return GR_GENERIC;
    const int Secondary = ReadEventWord(Desc, 8);
    if (Secondary < 0)
    {
        if (!EventAllowsMissingSecondary(ReadEventWord(Desc, 0)))
            return GR_GENERIC;
    }
    else if (!EntityExists(Secondary))
        return GR_GENERIC;

    // The type is reread after validation. An unknown type returns success
    // with *OutId still -1.
    const int Type = ReadEventWord(Desc, 0);
    if (Type < kThornEventTypeMin || Type > kThornEventTypeMax)
        return GR_OK;
    FThornEvent* Event = new FThornEvent(Type, Desc);

    BindEventSceneAxes(Event);
    // Insert after equal start times (unsigned compare).
    int Pos = 0;
    const unsigned int StartKey = static_cast<unsigned int>(Event->StartTimeMs);
    while (Pos < MasterCount_ &&
           static_cast<unsigned int>(Master_[Pos]->StartTimeMs) <= StartKey)
        ++Pos;

    // As in retail, a failed grow leaks the event.
    if (MasterCount_ == MasterCap_)
    {
        // Grow by max(size, 1), no minimum capacity.
        const unsigned int Count = Master_ != nullptr ? static_cast<unsigned int>(MasterCount_) : 0u;
        const unsigned int Growth = Count > 1u ? Count : 1u;
        const unsigned int CapacityBits = Count + Growth;
        const int NewCapacity = static_cast<int>(CapacityBits);
        const unsigned int AllocationCount = NewCapacity < 0 ? 0u : CapacityBits;
        FThornEvent** NewMaster = reinterpret_cast<FThornEvent**>(
            new unsigned char[AllocationCount * static_cast<unsigned int>(sizeof(FThornEvent*))]);
        for (int Index = 0; Index < Pos; ++Index)
            NewMaster[Index] = Master_[Index];
        NewMaster[Pos] = Event;
        for (int Index = Pos; Index < MasterCount_; ++Index)
            NewMaster[Index + 1] = Master_[Index];
        // Free, then capacity, end and begin, in retail's order.
        delete[] reinterpret_cast<unsigned char*>(Master_);
        MasterCap_ = NewCapacity;
        MasterCount_ = static_cast<int>((Master_ != nullptr ? static_cast<unsigned int>(MasterCount_) : 0u) + 1u);
        Master_ = NewMaster;
    }
    else
    {
        for (int Index = MasterCount_; Index > Pos; --Index)
            Master_[Index] = Master_[Index - 1];
        Master_[Pos] = Event;
        MasterCount_ = static_cast<int>(static_cast<unsigned int>(MasterCount_) + 1u);
    }
    *OutId = Event->UniqueId;
    return GR_OK;
}

GENRESULT FThornScene::RemoveEventById(int Id)
{
    // LAB_06f3a330: linear search on event +0xc, then FUN_06f3a240 erase.
    for (int Index = 0; Index < MasterCount_; ++Index)
    {
        FThornEvent* Event = Master_[Index];
        if (Event->UniqueId != Id)
            continue;
        EraseMasterEventAt(Master_ + Index);
        return GR_OK;
    }
    return GR_GENERIC;
}

void* FThornScene::EventDescByIndex(int Index)
{
    if (Index < 0 || Index >= MasterCount_)
        return nullptr;
    FThornEvent* Event = Master_[Index];
    return Event->GetDesc();
}

void* FThornScene::EventDescById(int Id)
{
    for (int Index = 0; Index < MasterCount_; ++Index)
    {
        if (Master_[Index]->UniqueId == Id)
            return Master_[Index]->GetDesc();
    }
    return nullptr;
}

bool FThornScene::HasEventType(int Type) const
{
    for (int Index = 0; Index < MasterCount_; ++Index)
    {
        if (Master_[Index]->Type == Type)
            return true;
    }
    return false;
}

int FThornScene::CompareEventStart(const void* Left, const void* Right)
{
    const FThornEvent* EventLeft = *reinterpret_cast<FThornEvent* const*>(Left);
    const FThornEvent* EventRight = *reinterpret_cast<FThornEvent* const*>(Right);
    if (EventLeft->StartTimeMs < EventRight->StartTimeMs)
        return -1;
    if (EventLeft->StartTimeMs > EventRight->StartTimeMs)
        return 1;
    return 0;
}

void FThornScene::RebuildPending()
{
    // Clear PENDING, then copy MASTER into it unsorted.
    PendingCount_ = 0;
    PendingBegin_ = 0;
    const int SourceCount = Master_ != nullptr ? MasterCount_ : 0;
    const int Capacity = Pending_ != nullptr ? PendingCap_ : 0;
    if (static_cast<unsigned int>(SourceCount) > static_cast<unsigned int>(Capacity))
    {
        // Retail releases old storage before rereading the allocation count.
        delete[] Pending_;
        const int LiveCount = Master_ != nullptr ? MasterCount_ : 0;
        const unsigned int AllocationCount = LiveCount < 0 ? 0u : static_cast<unsigned int>(LiveCount);
        Pending_ = new FThornEvent*[AllocationCount];
        FThornEvent** Read = Master_;
        FThornEvent** End = MasterCount_ == 0 ? Master_ : Master_ + MasterCount_;
        FThornEvent** Write = Pending_;
        for (; Read != End; ++Read, ++Write)
            *Write = *Read;
        PendingCount_ = static_cast<int>(Write - Pending_);
        PendingCap_ = PendingCount_;
        return;
    }
    FThornEvent** Read = Master_;
    FThornEvent** End = MasterCount_ == 0 ? Master_ : Master_ + MasterCount_;
    FThornEvent** Write = Pending_;
    for (; Read != End; ++Read, ++Write)
        *Write = *Read;
    PendingCount_ = Master_ != nullptr ? MasterCount_ : 0;
}

void FThornScene::StopAll()
{
    // STOPLIST is cleared before any Stop callback.
    StopTimes_->clear();
    if (ActiveCount_ == 0)
        return;
    FThornEvent** ActiveBegin = Active_;
    FThornEvent** Successor = ActiveBegin + 1;
    do
    {
        // Begin is kept; the end is reread after each Stop. As in retail, a
        // Stop that reallocates or clears ACTIVE invalidates these cursors.
        (*ActiveBegin)->Stop(this);
        FThornEvent** ActiveEnd = Active_ + ActiveCount_;
        for (FThornEvent** Read = Successor; Read != ActiveEnd; ++Read)
            *(Read - 1) = *Read;
        ActiveCount_ = ActiveCount_ - 1;
    } while (ActiveBegin != Active_ + ActiveCount_);
}

void FThornScene::CompactPendingFrom(FThornEvent** SelectedBegin, FThornEvent** SelectedEnd)
{
    // The end is read after the callbacks.
    FThornEvent** LiveEnd = Pending_ + PendingCount_;
    FThornEvent** Write = SelectedBegin;
    for (FThornEvent** Read = SelectedEnd; Read != LiveEnd; ++Read)
        *Write++ = *Read;
    PendingCount_ = static_cast<int>(Write - Pending_);
}

void FThornScene::DrainTimedStops(const int* TimeMs, bool bBeforeStartTime,
                                 std::set<int>* UpdatedTimes)
{
    std::list<float>::iterator Stop = StopTimes_->begin();
    if (bBeforeStartTime)
    {
        // The event's unsigned start is reread on each iteration.
        while (Stop != StopTimes_->end())
        {
            if (!(static_cast<double>(*Stop) <
                  static_cast<double>(static_cast<unsigned int>(*TimeMs))))
                break;
            UpdateActive(static_cast<int>(static_cast<unsigned int>(
                static_cast<long long>(*Stop))));
            // The same node is read again after the callbacks.
            UpdatedTimes->insert(static_cast<int>(static_cast<unsigned int>(
                static_cast<long long>(*Stop))));
            Stop = StopTimes_->erase(Stop);
        }
    }
    else if (Stop != StopTimes_->end())
    {
        // Tick rounds its signed clock to float once before draining.
        const float RoundedTime = static_cast<float>(*TimeMs);
        do
        {
            if (!(*Stop < RoundedTime))
                break;
            UpdateActive(static_cast<int>(static_cast<unsigned int>(
                static_cast<long long>(*Stop))));
            // The node is erased only after UpdateActive.
            Stop = StopTimes_->erase(Stop);
        } while (Stop != StopTimes_->end());
    }
}

void FThornScene::DispatchWindow(int OldTime, int NewTime)
{
    // FUN_06f3a960: selects an inclusive unsigned start-time window; the
    // prefix before it is not erased.
    int TakeFrom = 0;
    while (TakeFrom < PendingCount_ &&
           static_cast<unsigned int>(Pending_[TakeFrom]->StartTimeMs) <
               static_cast<unsigned int>(OldTime))
        ++TakeFrom;
    if (TakeFrom >= PendingCount_ ||
        static_cast<unsigned int>(Pending_[TakeFrom]->StartTimeMs) >
            static_cast<unsigned int>(NewTime))
        return;
    int TakeTo = TakeFrom;
    while (TakeTo < PendingCount_ &&
           static_cast<unsigned int>(Pending_[TakeTo]->StartTimeMs) <=
               static_cast<unsigned int>(NewTime))
        ++TakeTo;
    FThornEvent** SelectedBegin = Pending_ + TakeFrom;
    FThornEvent** SelectedEnd = Pending_ + TakeTo;
    {
        // FUN_06f3af50: the set records start times already updated.
        std::set<int> UpdatedTimes;
        for (FThornEvent** PendingSlot = SelectedBegin; PendingSlot != SelectedEnd; ++PendingSlot)
        {
            // Retail looks the primary up with map operator[], so a missing
            // primary leaves a null entry in the registry; the event is skipped.
            const int PrimaryId = (*PendingSlot)->PrimaryIndex;
            const std::pair<std::map<int, FThornEntity*>::iterator, bool> Registration =
                Entities_.insert(std::make_pair(PrimaryId, static_cast<FThornEntity*>(nullptr)));
            if (Registration.first->second == nullptr)
                continue;
            FThornEvent* Event = *PendingSlot;

            DrainTimedStops(&Event->StartTimeMs, true, &UpdatedTimes);
            if (UpdatedTimes.find(Event->StartTimeMs) == UpdatedTimes.end())
            {
                UpdateActive(Event->StartTimeMs);
                UpdatedTimes.insert(Event->StartTimeMs);
            }

            // Retail rejects types outside 1..19 here, before Start. Type is
            // reloaded from the slot after UpdateActive, but Start gets the
            // event read earlier.
            const int CurrentType = (*PendingSlot)->Type;
            if (CurrentType < kThornEventTypeMin || CurrentType > kThornEventTypeMax)
            {
                char Diagnostic[128];
                std::sprintf(Diagnostic,
                    "Start attempted on event with unknown event type %d.\n", CurrentType);
                FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
                    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\THORN\\Thorn.cpp",
                    0x5cf, Diagnostic);
            }
            else
            {
                const unsigned char Ok = Event->Start(this, ClockAfterDt_);
                if (Ok != 0)
                {
                    // Into ACTIVE before EndTime is read.
                    AppendActiveGroupEvent(Event);
                    if (Event->EndTimeMs >= 0)
                    {
                        const float StopTime = static_cast<float>(Event->EndTimeMs);
                        std::list<float>::iterator Stop = StopTimes_->begin();
                        while (Stop != StopTimes_->end())
                        {
                            // Erase only on ordered equality.
                            if (*Stop == StopTime)
                                Stop = StopTimes_->erase(Stop);
                            else
                                ++Stop;
                        }
                        StopTimes_->push_back(static_cast<float>(Event->EndTimeMs));
                        StopTimes_->sort();
                    }
                }
            }
            // Rebuilt after every event with a live primary, even a failed Start.
            RebuildActiveGroups();
        }
    }
    CompactPendingFrom(SelectedBegin, SelectedEnd);
}

void FThornScene::AppendActiveGroupEvent(FThornEvent* Event)
{
    // Append to ACTIVE (inlined vector insert).
    if (ActiveCap_ - ActiveCount_ >= 1)
    {
        Active_[ActiveCount_] = Event;
        ++ActiveCount_;
        return;
    }

    FThornEvent** Position = ActiveCount_ == 0 ? Active_ : Active_ + ActiveCount_;
    const int Growth = Active_ != nullptr && static_cast<unsigned int>(ActiveCount_) > 1
                           ? ActiveCount_ : 1;
    const int Capacity = static_cast<int>(static_cast<unsigned int>(ActiveCount_) +
                                         static_cast<unsigned int>(Growth));
    const unsigned int AllocationCount = Capacity < 0 ? 0 : static_cast<unsigned int>(Capacity);
    std::unique_ptr<FThornEvent*[]> Storage(new FThornEvent*[AllocationCount]);
    FThornEvent** Write = Storage.get();
    // The old begin is read after the allocation; Position was saved before it.
    for (FThornEvent** Read = Active_; Read != Position; ++Read)
        *Write++ = *Read;
    *Write++ = Event;
    FThornEvent** End = ActiveCount_ == 0 ? Active_ : Active_ + ActiveCount_;
    for (FThornEvent** Read = Position; Read != End; ++Read)
        *Write++ = *Read;
    delete[] Active_;
    ActiveCap_ = Capacity;
    ++ActiveCount_;
    Active_ = Storage.release();
}

void FThornScene::EmitActiveGroup(std::map<int, FActiveGroup>& Groups,
                                 std::map<int, FActiveGroup>::iterator Group)
{
    // Visited is per group and set before sorting.
    if (Group->second.bVisited)
        return;
    std::vector<FThornEvent*>& Events = Group->second.Events;
    FThornEvent** Begin = Events.data();
    FThornEvent** End = Events.empty() ? Begin : Begin + Events.size();
    Group->second.bVisited = true;
    if (Events.size() <= 16)
        SortActiveEventTypes(Begin, End);
    else
    {
        PartitionActiveEventTypes(Begin, End);
        SortActiveEventTypes(Begin, Begin + 16);
        for (FThornEvent** Slot = Begin + 16; Slot != End; ++Slot)
            InsertActiveEventType(Slot, *Slot);
    }

    // Groups an event depends on (its secondary) are emitted first.
    FThornEvent** Slot = Events.data();
    while (Slot != (Events.empty() ? Events.data() : Events.data() + Events.size()))
    {
        FThornEvent* Event = *Slot;
        if (Event->SecondaryIndex != -1)
        {
            std::map<int, FActiveGroup>::iterator Dependency =
                Groups.lower_bound(Event->SecondaryIndex);
            if (Dependency != Groups.end() && Event->SecondaryIndex >= Dependency->first)
                EmitActiveGroup(Groups, Dependency);
        }
        AppendActiveGroupEvent(Event);
        ++Slot;
    }
}

void FThornScene::RebuildActiveGroups()
{
    // FUN_06f3aa00: ACTIVE regrouped by primary, emitted in dependency order.
    std::map<int, FActiveGroup> Groups;
    FThornEvent** Slot = Active_;
    while (Slot != (ActiveCount_ == 0 ? Active_ : Active_ + ActiveCount_))
    {
        FThornEvent* Event = *Slot;
        const int Primary = Event->PrimaryIndex;
        const std::pair<std::map<int, FActiveGroup>::iterator, bool> Registration =
            Groups.insert(std::make_pair(Primary, FActiveGroup()));
        if (Registration.second)
            Registration.first->second.bVisited = false;
        Registration.first->second.Events.push_back(Event);
        ++Slot;
    }
    // ACTIVE is cleared only after all groups are collected.
    ActiveCount_ = 0;
    for (std::map<int, FActiveGroup>::iterator Group = Groups.begin();
         Group != Groups.end(); ++Group)
        EmitActiveGroup(Groups, Group);
}

void FThornScene::FrameUpdateEntities(int TimeMs)
{
    // FUN_06f3b330: by entity type, 1|2 compound +0x18, 10 IScriptControl +0x10,
    // 0xd psys +0x14, with dt = time - clock; then clock = time. The next node
    // is read after each callback, so appends stay visible.
    std::list<FThornEntity*>::iterator FrameIt = FrameEntities_->begin();
    while (FrameIt != FrameEntities_->end())
    {
        FThornEntity* Entity = *FrameIt;
        const int Type = Entity->Snap.Type;
        if ((Type == kThornEntityCompound || Type == kThornEntityDeformable) &&
            Entity->CompoundControl != nullptr)
            Entity->CompoundControl->AdvanceTime(static_cast<int>(static_cast<unsigned int>(TimeMs) -
                            static_cast<unsigned int>(ClockMs_)));
        else if (Type == kThornEntitySubScene && Entity->ScriptControl != nullptr)
            Entity->ScriptControl->Tick(static_cast<int>(static_cast<unsigned int>(TimeMs) -
                            static_cast<unsigned int>(ClockMs_)));
        else if (Type == kThornEntityPsys && Entity->PsysControl != nullptr)
            Entity->PsysControl->Tick(static_cast<int>(static_cast<unsigned int>(TimeMs) -
                            static_cast<unsigned int>(ClockMs_)));
        ++FrameIt;
    }
    ClockMs_ = TimeMs;
}

void FThornScene::UpdateActive(int TimeMs)
{
    FrameUpdateEntities(TimeMs);
    if (ActiveCount_ != 0)
    {
        FThornEvent** ActiveSlot = Active_;
        FThornEvent** Successor = ActiveSlot + 1;
        do
        {
            if ((*ActiveSlot)->StillRunning(TimeMs, this) != 0)
            {
                ++ActiveSlot;
                ++Successor;
            }
            else
            {
                // The slot is reread after StillRunning.
                (*ActiveSlot)->Stop(this);
                FThornEvent** ActiveEnd = Active_ + ActiveCount_;
                for (FThornEvent** Read = Successor; Read != ActiveEnd; ++Read)
                    *(Read - 1) = *Read;
                ActiveCount_ = ActiveCount_ - 1;
            }
        } while (ActiveSlot != Active_ + ActiveCount_);
    }
}

GENRESULT FThornScene::StartPlayback(int Arg)
{
    // LAB_06f39460: switch on state 2..7.
    const int State = PlaybackState_;
    if (State == kThornPlaybackLoaded || State == kThornPlaybackReset ||
        State == kThornPlaybackDone)
    {
        ClockMs_ = 0;
        RebuildPending();
    }
    else if (State == kThornPlaybackLoop)
    {
        RebuildPending();
    }
    else if (State == kThornPlaybackPaused)
    {
        PlaybackState_ = kThornPlaybackPlay;
        Tick(Arg);
        return GR_OK;
    }

    if (bLoop_ != 0)
    {
        const int Delta = static_cast<int>(static_cast<unsigned int>(Arg) -
                                           static_cast<unsigned int>(LoopCarryMs_));
        PlaybackState_ = kThornPlaybackLoop;
        Tick(Delta);
        return GR_OK;
    }
    PlaybackState_ = kThornPlaybackPlay;
    Tick(Arg);
    return GR_OK;
}

GENRESULT FThornScene::StopPlayback()
{
    ClockMs_ = 0;
    PendingCount_ = 0;
    PendingBegin_ = 0;
    RebuildPending();
    if (PlaybackState_ == kThornPlaybackLoop)
        LoopCarryMs_ = 0;
    StopAll();
    PlaybackState_ = kThornPlaybackReset;
    return GR_OK;
}

GENRESULT FThornScene::PausePlayback()
{
    // LAB_06f397f0: state (+0x2c) 6 or 7 -> 5 and 0; else -1. No Stop, clock
    // untouched; slot 26 still ticks state 5.
    if (PlaybackState_ == kThornPlaybackPlay || PlaybackState_ == kThornPlaybackLoop)
    {
        PlaybackState_ = kThornPlaybackPaused;
        return GR_OK;
    }
    return GR_GENERIC;
}

GENRESULT FThornScene::Tick(int DtMs)
{
    const int OldTime = ClockMs_;
    int NewTime = static_cast<int>(static_cast<unsigned int>(OldTime) +
                                   static_cast<unsigned int>(DtMs));

    if (DtMs < 0)
    {
        if (NewTime < 0)
            NewTime = 0;
        StopAll();
        ClockMs_ = 0;
        RebuildPending();
    }
    // The clock is read after the reset; ClockAfterDt_ is set even when the
    // unsigned backwards test exits.
    const int WindowOld = ClockMs_;
    ClockAfterDt_ = NewTime;
    if (static_cast<unsigned int>(NewTime) < static_cast<unsigned int>(WindowOld))
    {
        return GR_OK;
    }

    const int State = PlaybackState_;
    if (State != kThornPlaybackPaused && State != kThornPlaybackPlay &&
        State != kThornPlaybackLoop)
        return GR_OK;

    DispatchWindow(WindowOld, NewTime);

    DrainTimedStops(&NewTime, false, nullptr);

    UpdateActive(NewTime);

    if (ClockMs_ != 0)
        FrameUpdateEntities(ClockMs_);

    if (static_cast<unsigned int>(NewTime) > static_cast<unsigned int>(DurationMs_))
    {
        if (bLoop_ != 0)
        {
            StopPlayback();
            const int Carry = ClockMs_;
            LoopCarryMs_ = Carry;
            StartPlayback(Carry);
            return GR_OK;
        }
        StopAll();
        PlaybackState_ = kThornPlaybackDone;
    }
    return GR_OK;
}

namespace
{
    static const char* const kEntityTypeNames[] = {
        "UNKNOWN_ENTITY", "COMPOUND",    "DEFORMABLE", "CAMERA",     "MONITOR",
        "LIGHT",          "SOUND",       "MARKER",     "HARDPOINT",  "SCENE",
        "SUB_SCENE",      "MOTION_PATH", "DELETED",    "PSYS"};
    // Retail table PTR_s_UNDEFINED_EVENT_06f5e090 (indexed by FUN_06f27bf0);
    // spellings and order are the serializer/parser format.
    static const char* const kEventTypeNames[] = {
        "UNDEFINED_EVENT",          "START_MOTION",
        "START_SOUND",              "START_IK",
        "START_PATH_ANIMATION",     "ATTACH_ENTITY",
        "START_LIGHT_PROP_ANIM",    "START_CAMERA_PROP_ANIM",
        "START_SPATIAL_PROP_ANIM",  "SET_CAMERA",
        "CONNECT_HARDPOINTS",       "START_SUB_SCENE",
        "USER_EVENT",               "START_PSYS",
        "START_PSYS_PROP_ANIM",     "START_AUDIO_PROP_ANIM",
        "START_FOG_PROP_ANIM",      "START_REVERB_PROP_ANIM",
        "START_FLR_HEIGHT_ANIM",    "SUBTITLE"};
    static const char* const kAxisNames[] = {"X_AXIS",     "Y_AXIS",     "Z_AXIS",
                                             "NEG_X_AXIS", "NEG_Y_AXIS", "NEG_Z_AXIS"};

    const char* EntityTypeName(int Type)
    {
        if (Type < 0 || Type > 13)
            return kEntityTypeNames[0];
        return kEntityTypeNames[Type];
    }

    const char* EventTypeName(int Type)
    {
        if (Type < 0 || Type > 19)
            return kEventTypeNames[0];
        // Retail maps the numeric ID to the table position first.
        static const int EventIds[] = {0, 10, 3, 11, 6, 8, 4, 5, 7, 2, 9, 12, 1,
                                        13, 14, 15, 16, 17, 18, 19};
        for (int Index = 0; Index < 20; ++Index)
        {
            if (EventIds[Index] == Type)
                return kEventTypeNames[Index];
        }
        return kEventTypeNames[0];
    }

    bool SpatialInSave(int Type)
    {
        return Type == 1 || Type == 2 || Type == 3 || Type == 5 || Type == 6 || Type == 7 ||
               Type == 9 || Type == 10 || Type == 11 || Type == 13;
    }

    // Holds the save file system (FUN_06f2d2c0); releases it on unwind.
    struct FThornSaveFileSystemOwner
    {
        IFileSystem* FileSystem;

        explicit FThornSaveFileSystemOwner(IFileSystem* InFileSystem)
            : FileSystem(InFileSystem)
        {
        }

        void Release()
        {
            if (FileSystem != nullptr)
            {
                FileSystem->Release();
                FileSystem = nullptr;
            }
        }

        ~FThornSaveFileSystemOwner() noexcept(false)
        {
            Release();
        }
    };

    struct FThornSaveBuf
    {
        FThornSaveFileSystemOwner FileSystemOwner;
        char* Data;
        int Len;
        int Cap;
        int StorageCapacity;

        explicit FThornSaveBuf(IFileSystem* InFileSystem, int Capacity = 512)
            : FileSystemOwner(InFileSystem), Data(nullptr), Len(0), Cap(Capacity),
              StorageCapacity(Capacity + 1)
        {
            // AddRef before the allocation.
            if (FileSystemOwner.FileSystem != nullptr)
                FileSystemOwner.FileSystem->AddRef();
            Data = new (std::nothrow) char[StorageCapacity];
        }

        ~FThornSaveBuf() noexcept(false)
        {
            // FUN_06f2ca00: no flush.
            delete[] Data;
            FileSystemOwner.Release();
        }

        FThornSaveBuf(const FThornSaveBuf&) = delete;
        FThornSaveBuf& operator=(const FThornSaveBuf&) = delete;
    };

    int SaveBufFlush(FThornSaveBuf* Buf)
    {
        // FUN_06f2ca80: one write, return value ignored; a short write still
        // discards the whole buffer and is not retried.
        if (Buf->Len == 0)
            return 0;
        unsigned long Written = 0;
        Buf->FileSystemOwner.FileSystem->Write(0, Buf->Data, static_cast<unsigned long>(Buf->Len),
                               &Written, nullptr);
        const int Result = Written == static_cast<unsigned long>(Buf->Len) ? 0 : -1;
        Buf->Len = 0;
        return Result;
    }

    int SaveBufSetCapacity(FThornSaveBuf* Buf, int Capacity)
    {
        const int Result = SaveBufFlush(Buf);
        if (Result != 0)
            return Result;
        delete[] Buf->Data;
        Buf->Data = nullptr;
        Buf->Cap = Capacity;
        Buf->StorageCapacity = Capacity;
        Buf->Data = new (std::nothrow) char[static_cast<unsigned int>(Capacity)];
        return 0;
    }

    int SaveBufAppendText(FThornSaveBuf* Buf, const char* Text)
    {
        // FUN_06f2cda0: strlen before the optional flush.
        const unsigned int Length = static_cast<unsigned int>(std::strlen(Text));
        int Result = 0;
        if (static_cast<unsigned int>(Buf->Len) + Length > static_cast<unsigned int>(Buf->Cap))
            Result = SaveBufFlush(Buf);

        // Copies through the NUL even when the flush failed; as in retail,
        // text longer than Cap overruns the buffer.
        char* Destination = Buf->Data + static_cast<unsigned int>(Buf->Len);
        const char* Cursor = Text;
        char Byte;
        do
        {
            Byte = *Cursor;
            ++Cursor;
            *Destination = Byte;
            ++Destination;
        }
        while (Byte != '\0');
        Buf->Len = static_cast<int>(static_cast<unsigned int>(Buf->Len) + Length);
        return Result;
    }

    // FUN_06f2cbb0: bounded format; on overflow flush and retry, then grow.
    int SaveBufAppendVariadic(FThornSaveBuf* Buf, const char* Format, ...)
    {
        va_list Args;
        va_list Attempt;
        va_start(Args, Format);
        va_copy(Attempt, Args);
        int Written = _vsnprintf(Buf->Data + static_cast<unsigned int>(Buf->Len),
            static_cast<unsigned int>(Buf->Cap) - static_cast<unsigned int>(Buf->Len),
            Format, Attempt);
        va_end(Attempt);
        if (Written < 0)
        {
            const int Result = SaveBufFlush(Buf);
            if (Result != 0)
            {
                va_end(Args);
                return Result;
            }
            va_copy(Attempt, Args);
            Written = _vsnprintf(Buf->Data + static_cast<unsigned int>(Buf->Len),
                static_cast<unsigned int>(Buf->Cap) - static_cast<unsigned int>(Buf->Len),
                Format, Attempt);
            va_end(Attempt);
            while (Written < 0)
            {
                // Resize result ignored; no retry bound, as in retail.
                SaveBufSetCapacity(Buf, static_cast<int>(static_cast<unsigned int>(Buf->Cap) * 2u));
                va_copy(Attempt, Args);
                Written = _vsnprintf(Buf->Data + static_cast<unsigned int>(Buf->Len),
                    static_cast<unsigned int>(Buf->Cap) - static_cast<unsigned int>(Buf->Len),
                    Format, Attempt);
                va_end(Attempt);
            }
        }
        va_end(Args);
        Buf->Len = static_cast<int>(static_cast<unsigned int>(Buf->Len) +
                                   static_cast<unsigned int>(Written));
        return 0;
    }

    // Each float is promoted to double once.
    int SaveBufAppendNamedFloat(FThornSaveBuf* Buf, const char* Format,
                                const char* Key, const float* Value)
    {
        const double Promoted = static_cast<double>(*Value);
        return SaveBufAppendVariadic(Buf, Format, Key, Promoted);
    }

    // FUN_06f2ce00 / FUN_06f2ce80: one integer argument, fixed scratch buffer.
    int SaveBufAppendInteger(FThornSaveBuf* Buf, const char* Format, int Value)
    {
        char Temporary[512];
        std::sprintf(Temporary, Format, Value);
        return SaveBufAppendText(Buf, Temporary);
    }

    int SaveBufAppendUnsigned(FThornSaveBuf* Buf, const char* Format, unsigned int Value)
    {
        char Temporary[512];
        std::sprintf(Temporary, Format, Value);
        return SaveBufAppendText(Buf, Temporary);
    }

    int SaveBufAppendLiteral(FThornSaveBuf* Buf, const char* Text)
    {
        char Temporary[512];
        std::sprintf(Temporary, "%s", Text);
        return SaveBufAppendText(Buf, Temporary);
    }

    // FUN_06f2ce40: the float is loaded only here.
    int SaveBufAppendFloat(FThornSaveBuf* Buf, const char* Format, const float* Value)
    {
        char Temporary[512];
        std::sprintf(Temporary, Format, static_cast<double>(*Value));
        return SaveBufAppendText(Buf, Temporary);
    }

    // Writer: zero-extended milliseconds, one float store.
    int SaveBufAppendUnsignedMilliseconds(FThornSaveBuf* Buf, const char* Format,
                                          unsigned int Milliseconds)
    {
        const __int64 ExtendedMilliseconds = Milliseconds;
        static const double GMillisecondsToSeconds = 0.001;
        const float Seconds = static_cast<float>(
            static_cast<double>(ExtendedMilliseconds) * GMillisecondsToSeconds);
        return SaveBufAppendFloat(Buf, Format, &Seconds);
    }

    // Event durations: signed dword, scaled in double.
    int SaveBufAppendSignedMilliseconds(FThornSaveBuf* Buf, const char* Format,
                                        const int* Milliseconds)
    {
        static const double GMillisecondsToSeconds = 0.001;
        const float Seconds = static_cast<float>(
            static_cast<double>(*Milliseconds) * GMillisecondsToSeconds);
        return SaveBufAppendFloat(Buf, Format, &Seconds);
    }

    int SaveBufAppendRawFloat(FThornSaveBuf* Buf, const char* Format, const float* Value)
    {
        return SaveBufAppendFloat(Buf, Format, Value);
    }

    // FUN_06f2cf10 / FUN_06f2d080: loads in Z/Y/X order, double args.
    int SaveBufAppendVector(FThornSaveBuf* Buf, const char* Format, const float* Values)
    {
        char Temporary[512];
        const double Z = static_cast<double>(Values[2]);
        const double Y = static_cast<double>(Values[1]);
        const double X = static_cast<double>(Values[0]);
        std::sprintf(Temporary, Format, X, Y, Z);
        return SaveBufAppendText(Buf, Temporary);
    }

    int SaveBufAppendVector(FThornSaveBuf* Buf, const char* Format, const Vector* Values)
    {
        return SaveBufAppendVector(Buf, Format, &Values->x);
    }

    // FUN_06f2cec0: reads [1], [2], [0], then sprintf.
    int SaveBufAppendColor(FThornSaveBuf* Buf, const char* Format, const int* Values)
    {
        char Temporary[512];
        const int Y = Values[1];
        const int Z = Values[2];
        const int X = Values[0];
        std::sprintf(Temporary, Format, X, Y, Z);
        return SaveBufAppendText(Buf, Temporary);
    }

    bool ShouldSaveNonzeroFloat(const float* Value)
    {
        const float Zero = 0.0f;
        unsigned char Emit;
        __asm {
            mov edx, Value
            fld dword ptr [edx]
            fcomp dword ptr Zero
            fnstsw ax
            test ah, 44h
            setp al
            mov Emit, al
        }
        // Equality skips; unordered and nonzero emit.
        return Emit != 0;
    }

    int SaveBufAppendPromotedFloatAsInteger(FThornSaveBuf* Buf, const char* Format, const float* Value)
    {
        // Retail quirk: passes the float promoted to double to sprintf("%d"),
        // which prints the double's low dword. That word is passed explicitly
        // here to avoid variadic UB.
        const double PromotedValue = static_cast<double>(*Value);
        int IntegerWord;
        static_assert(sizeof(PromotedValue) == 8 && sizeof(IntegerWord) == 4,
                      "Thorn writer requires an eight-byte double and four-byte int");
        std::memcpy(&IntegerWord, &PromotedValue, sizeof(IntegerWord));
        return SaveBufAppendInteger(Buf, Format, IntegerWord);
    }

    int SaveBufAppendIntegerAsDouble(FThornSaveBuf* Buf, unsigned int IntegerWord,
                                    const FThornEntitySnap* SecondaryDesc)
    {
        static_assert(sizeof(SecondaryDesc) == 4, "Thorn writer requires Win32 pointers");
        const unsigned int Words[2] = {IntegerWord,
            static_cast<unsigned int>(reinterpret_cast<ULONG_PTR>(SecondaryDesc))};
        double Payload;
        std::memcpy(&Payload, Words, sizeof(Payload));
        char Temporary[512];
        // Retail quirk: "%.6f" reads a double made of IntegerWord and the next
        // pushed argument. The va_list points at the bytes directly, since
        // passing a double by value would add an x87 load/store.
        va_list Args = reinterpret_cast<va_list>(&Payload);
        std::vsprintf(Temporary, "%.6f", Args);
        va_end(Args);
        return SaveBufAppendText(Buf, Temporary);
    }

    int SaveBufAppendStringValue(FThornSaveBuf* Buf, const char* Format, const char* Value)
    {
        // FUN_06f2cc90: heap scratch sized from the format and value lengths.
        if (Value == nullptr)
            Value = "NULL";
        const unsigned int AllocationSize = static_cast<unsigned int>(std::strlen(Format)) +
                                            static_cast<unsigned int>(std::strlen(Value)) + 2u;
        char* Temporary = static_cast<char*>(::operator new(AllocationSize));
        std::sprintf(Temporary, Format, Value);
        const unsigned int Pending = static_cast<unsigned int>(Buf->Len);
        const unsigned int FormattedLength = static_cast<unsigned int>(std::strlen(Temporary));
        unsigned int Remaining = AllocationSize;
        char* Cursor = Temporary;
        int Result = 0;
        if (Pending + FormattedLength > static_cast<unsigned int>(Buf->Cap))
            Result = SaveBufFlush(Buf);

        // Remaining is the allocation size only on the first test.
        while (Remaining > static_cast<unsigned int>(Buf->Cap))
        {
            const unsigned int ChunkLength = static_cast<unsigned int>(Buf->Cap);
            char* Destination = Buf->Data;
            Buf->Len = static_cast<int>(ChunkLength);
            std::strncpy(Destination, Cursor, ChunkLength);
            Cursor += static_cast<unsigned int>(Buf->Len);
            Remaining = static_cast<unsigned int>(std::strlen(Cursor));
            Result = SaveBufFlush(Buf);
        }
        if (Remaining != 0)
        {
            const int Written = std::sprintf(Buf->Data + static_cast<unsigned int>(Buf->Len), "%s", Cursor);
            Buf->Len = static_cast<int>(static_cast<unsigned int>(Buf->Len) +
                                       static_cast<unsigned int>(Written));
        }
        ::operator delete(Temporary);
        return Result;
    }

    // FUN_06f2c310: copy at most Capacity bytes, escaping \\ \r \n \b when
    // both bytes fit. Used for strings embedded in the saved Lua text.
    char* EscapeThornString(char* Destination, const char* Source, int Capacity)
    {
        if (Source == nullptr)
            return nullptr;
        // Port-only guard; the one retail caller passes a 1024-byte stack buffer.
        if (Destination == nullptr || Capacity <= 0)
            return nullptr;

        char* Out = Destination;
        int Remaining = Capacity;
        const char* In = Source;
        while (*In != '\0' && Remaining > 0)
        {
            char Escape = '\0';
            if (*In == '\\')
                Escape = '\\';
            else if (*In == '\r')
                Escape = 'r';
            else if (*In == '\n')
                Escape = 'n';
            else if (*In == '\b')
                Escape = 'b';

            if (Escape != '\0' && Remaining > 2)
            {
                *Out++ = '\\';
                *Out++ = Escape;
                Remaining -= 2;
            }
            else
            {
                *Out++ = *In;
                --Remaining;
            }
            ++In;
        }
        if (Remaining != 0)
            *Out = '\0';
        else
            *(Out - 1) = '\0';
        return Destination;
    }

    // FUN_06f2cb00: fixed scratch; the first token is used as the format itself.
    int SaveBufAppendStringFormat(FThornSaveBuf* Buf, const char* Token, char* Started)
    {
        char Temporary[512];
        if (*Started != '\0')
            std::sprintf(Temporary, " +%s", Token);
        else
            std::sprintf(Temporary, Token);
        *Started = '\x01'; // Set after the first sprintf.
        const unsigned int Pending = static_cast<unsigned int>(Buf->Len);
        const unsigned int Length = static_cast<unsigned int>(std::strlen(Temporary));
        int Result = 0;
        if (Pending + Length > static_cast<unsigned int>(Buf->Cap))
            Result = SaveBufFlush(Buf);
        const int Written = std::sprintf(Buf->Data + static_cast<unsigned int>(Buf->Len),
                                         "%s", Temporary);
        Buf->Len = static_cast<int>(static_cast<unsigned int>(Buf->Len) +
                                   static_cast<unsigned int>(Written));
        return Result;
    }

    const char* LightTypeName(int Type)
    {
        if (Type == 2)
            return "L_SPOT";
        if (Type == 3)
            return "L_DIRECT";
        return "L_POINT";
    }

    const char* FogModeName(int Mode)
    {
        if (Mode == 1)
            return "F_EXP";
        if (Mode == 2)
            return "F_EXP2";
        if (Mode == 3)
            return "F_LINEAR";
        return "F_NONE";
    }

    // FUN_06f2c3e0: byte mask; each value is read after its key is written.
    void SaveFogProps(FThornSaveBuf* Buf, const FThornFog8* Props, unsigned char Mask)
    {
        if ((Mask & 0x1) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogon");
            SaveBufAppendStringValue(Buf, "%s, ", Props->On != 0 ? "Y" : "N");
        }
        if ((Mask & 0x2) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogtable");
            SaveBufAppendStringValue(Buf, "%s, ", Props->Table != 0 ? "Y" : "N");
        }
        if ((Mask & 0x4) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogcolor");
            SaveBufAppendColor(Buf, "{%d, %d, %d}, ", Props->Color);
        }
        if ((Mask & 0x8) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogmode");
            SaveBufAppendStringValue(Buf, "%s, ", FogModeName(Props->Mode));
        }
        if ((Mask & 0x10) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogstart");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Start);
        }
        if ((Mask & 0x20) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogend");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->End);
        }
        if ((Mask & 0x40) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogdensity");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Density);
        }
    }

    // FUN_06f2c8f0: byte mask; each value is read after its key is written.
    void SaveReverbProps(FThornSaveBuf* Buf, const FThornReverb4* Props, unsigned char Mask)
    {
        if ((Mask & 0x1) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "env");
            int Environment;
            std::memcpy(&Environment, &Props->Environment, sizeof(Environment));
            SaveBufAppendInteger(Buf, "%d, ", Environment);
        }
        if ((Mask & 0x2) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "vol");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Volume);
        }
        if ((Mask & 0x4) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "decay");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Decay);
        }
        if ((Mask & 0x8) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "damping");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Damping);
        }
    }

    const char* YnToken(int Value) { return (Value != 0) ? "Y" : "N"; }

    void SaveLightKey(FThornSaveBuf* Buf, const char* Prefix, const char* Key)
    {
        SaveBufAppendStringValue(Buf, "%s", Prefix);
        SaveBufAppendStringValue(Buf, "%s = ", Key);
    }

    int SaveLightAngle(FThornSaveBuf* Buf, const float* Value)
    {
        const unsigned int ScaleBits = 0x42652ee0u;
        float Scale;
        std::memcpy(&Scale, &ScaleBits, sizeof(Scale));
        const float Scaled = *Value * Scale;
        return SaveBufAppendFloat(Buf, "%.6f, ", &Scaled);
    }

    // FUN_06f2c560: prefix, key, value and suffix calls per selected field.
    // Both retail callers ignore the result.
    void SaveLightProps(FThornSaveBuf* Buf, const char* Prefix, const char* Suffix,
                        const FThornLightProps* Props, unsigned int Mask)
    {
        if ((Mask & kThornLightBitOn) != 0)
        {
            SaveLightKey(Buf, Prefix, "on");
            SaveBufAppendStringValue(Buf, "%s, ", YnToken(Props->On));
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitColor) != 0)
        {
            SaveLightKey(Buf, Prefix, "color");
            SaveBufAppendColor(Buf, "{%d, %d, %d}, ", Props->Color);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitDiffuse) != 0)
        {
            SaveLightKey(Buf, Prefix, "diffuse");
            SaveBufAppendVector(Buf, "{ %.6f, %.6f, %.6f }, ", Props->Diffuse);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitSpecular) != 0)
        {
            SaveLightKey(Buf, Prefix, "specular");
            SaveBufAppendVector(Buf, "{ %.6f, %.6f, %.6f }, ", Props->Specular);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitAmbient) != 0)
        {
            SaveLightKey(Buf, Prefix, "ambient");
            SaveBufAppendVector(Buf, "{ %.6f, %.6f, %.6f }, ", Props->Ambient);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitDirection) != 0)
        {
            SaveLightKey(Buf, Prefix, "direction");
            SaveBufAppendVector(Buf, "{ %.6f, %.6f, %.6f }, ", &Props->Direction);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitRange) != 0)
        {
            SaveLightKey(Buf, Prefix, "range");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Range);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitCutoff) != 0)
        {
            SaveLightKey(Buf, Prefix, "cutoff");
            SaveLightAngle(Buf, &Props->Cutoff);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitType) != 0)
        {
            SaveLightKey(Buf, Prefix, "type");
            SaveBufAppendStringValue(Buf, "%s, ", LightTypeName(Props->Type));
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitTheta) != 0)
        {
            SaveLightKey(Buf, Prefix, "theta");
            SaveLightAngle(Buf, &Props->Theta);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitAtten) != 0)
        {
            // Copied before the prefix append.
            float Atten[3];
            const float* Source = Props->Atten;
            Atten[0] = Source[0];
            Atten[1] = Source[1];
            Atten[2] = Source[2];
            SaveLightKey(Buf, Prefix, "atten");
            SaveBufAppendVector(Buf, "{ %.10f, %.10f, %.10f }, ", Atten);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
    }

    int SaveParamCurve(FThornSaveBuf* Buf, IParamCurve* Curve)
    {
        // FUN_06f2d120: the header is staged before the key callbacks and
        // appended with the first point; later points reuse the scratch from 0.
        const unsigned int Count = Curve->GetKeyCount();
        if (Count == 0)
            return -1;
        char Temporary[512];
        int HeaderLength = std::sprintf(Temporary,
            ", param_curve = {CLSID = \"%s\", points = {", Curve->GetClassId());
        int Result = 0;
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            const int Id = Curve->GetKeyId(static_cast<int>(Index));
            if (Id == -1)
                continue;
            float Values[4];
            if (Curve->ReadKey(Id, &Values[0], &Values[1], &Values[2], &Values[3]) == 0)
                continue;
            char* PointOutput = Temporary + HeaderLength;
            const char* PointFormat = "{ %.6f, %.6f, %.6f, %.6f },";
            const double W = static_cast<double>(Values[3]);
            const double Z = static_cast<double>(Values[2]);
            const double Y = static_cast<double>(Values[1]);
            const double X = static_cast<double>(Values[0]);
            std::sprintf(PointOutput, PointFormat, X, Y, Z, W);
            const unsigned int Length = static_cast<unsigned int>(std::strlen(Temporary));
            const bool bFlush = static_cast<unsigned int>(Buf->Len) + Length >
                static_cast<unsigned int>(Buf->Cap);
            const int AppendResult = SaveBufAppendText(Buf, Temporary);
            if (bFlush)
                Result = AppendResult; // Last flush replaces earlier results.
            HeaderLength = 0;
        }
        std::sprintf(Temporary, "}}");
        const bool bFlush = static_cast<unsigned int>(Buf->Len) + 2u >
            static_cast<unsigned int>(Buf->Cap);
        const int AppendResult = SaveBufAppendText(Buf, Temporary);
        return bFlush ? AppendResult : Result;
    }

    void SaveEventParamCurve(FThornSaveBuf* Buf, const FThornEventDesc* Descriptor)
    {
        if (Descriptor->ParamCurve == nullptr)
            return;
        SaveParamCurve(Buf, Descriptor->ParamCurve);
        // The period is read after the curve and flush callbacks.
        SaveBufAppendInteger(Buf, ", pcurve_period = %d", Descriptor->ParamCurvePeriod);
    }

    // FUN_06f2cf60: the matrix is passed in column order 0,3,6,1,4,7,2,5,8.
    int SaveMatrixValues(FThornSaveBuf* Buf, const char* Format, const Matrix* Orientation)
    {
        char Temporary[512];
        // Read order is retail's: two doubles, then seven float stores before
        // promotion.
        const double M22 = static_cast<double>(Orientation->E[2][2]);
        const double M12 = static_cast<double>(Orientation->E[1][2]);
        const volatile float M02 = Orientation->E[0][2];
        const volatile float M21 = Orientation->E[2][1];
        const volatile float M11 = Orientation->E[1][1];
        const volatile float M01 = Orientation->E[0][1];
        const volatile float M20 = Orientation->E[2][0];
        const volatile float M10 = Orientation->E[1][0];
        const volatile float M00 = Orientation->E[0][0];
        volatile double Arguments[9];
        Arguments[8] = M22;
        Arguments[7] = M12;
        Arguments[6] = static_cast<double>(M02);
        Arguments[5] = static_cast<double>(M21);
        Arguments[4] = static_cast<double>(M11);
        Arguments[3] = static_cast<double>(M01);
        Arguments[2] = static_cast<double>(M20);
        Arguments[1] = static_cast<double>(M10);
        Arguments[0] = static_cast<double>(M00);
        // x86 va_list over the nine doubles.
        va_list Args = reinterpret_cast<va_list>(const_cast<double*>(Arguments));
        std::vsprintf(Temporary, Format, Args);
        va_end(Args);
        return SaveBufAppendText(Buf, Temporary);
    }

    void SaveUseScriptDuration(FThornSaveBuf* Buf, const FThornEventDesc* Descriptor)
    {
        if ((Descriptor->Flags & kThornFlagUseScriptDuration) != 0)
            SaveBufAppendLiteral(Buf, ", flags = USE_SCRIPT_DURATION");
    }

    void SavePathAttachFlags(FThornSaveBuf* Buf, const FThornEventDesc* Descriptor, bool bAttach)
    {
        if ((Descriptor->Flags & 0xf) == 0)
            return;
        SaveBufAppendLiteral(Buf, ", flags =");
        char Started = '\0';
        if ((Descriptor->Flags & kThornFlagUseScriptDuration) != 0)
            SaveBufAppendStringFormat(Buf, " USE_SCRIPT_DURATION", &Started);
        if ((Descriptor->Flags & kThornFlagPosition) != 0)
            SaveBufAppendStringFormat(Buf, " POSITION", &Started);
        if ((Descriptor->Flags & kThornFlagOrientation) != 0)
            SaveBufAppendStringFormat(Buf, " ORIENTATION", &Started);
        if ((Descriptor->Flags & kThornFlagLookAt) != 0)
            SaveBufAppendStringFormat(Buf, " LOOK_AT", &Started);
        if ((Descriptor->Flags & kThornFlagEntityRelative) != 0)
            SaveBufAppendStringFormat(Buf, " ENTITY_RELATIVE", &Started);
        if (bAttach && (Descriptor->Flags & kThornFlagOrientRelative) != 0)
            SaveBufAppendStringFormat(Buf, " ORIENTATION_RELATIVE", &Started);
        if (bAttach && (Descriptor->Flags & kThornFlagParentChild) != 0)
            SaveBufAppendStringFormat(Buf, " PARENT_CHILD", &Started);
    }


}

GENRESULT FThornScene::SaveToFileSystem(void* FileSystem, IThornComContext* ComContext,
                                       unsigned int SavedDurationMs)
{
    if (FileSystem == nullptr)
        return GR_GENERIC;

    FThornSaveBuf Buf(static_cast<IFileSystem*>(FileSystem));

    SaveBufAppendUnsignedMilliseconds(&Buf, "duration = %.3f;\r\n\r\n",
                                      SavedDurationMs);
    SaveBufAppendLiteral(&Buf, "entities =\r\n{\r\n");

    bool bWroteEntity = false;
    for (unsigned int Ordinal = 0; Ordinal < LiveEntityCount(); ++Ordinal)
    {
        const int Index = static_cast<int>(reinterpret_cast<INT_PTR>(
            EntityHandle(static_cast<int>(Ordinal))));
        if (Index == -1)
            continue;
        FThornEntitySnap* Entity = EntitySnap(Index);
        // Both are read before any text is written; results ignored.
        Vector Pos;
        Matrix Orient;
        GetEntityPos(Index, &Pos);
        GetEntityOrient(Index, &Orient);
        if (Entity == nullptr)
            continue;
        const int RegisteredType = Entity->Type; // Read before any text is written.
        if (!bWroteEntity)
            SaveBufAppendLiteral(&Buf, "\r\n\t{\r\n");
        else
            SaveBufAppendLiteral(&Buf, ",\r\n\r\n\t{\r\n");
        bWroteEntity = true;

        const char* Name = Entity->Name;
        // entity_name is written raw; only template_name is escaped.
        SaveBufAppendStringValue(&Buf, "\t\tentity_name = \"%s\",\r\n", Name);
        SaveBufAppendStringValue(&Buf, "\t\ttype = %s,\r\n", EntityTypeName(RegisteredType));
        // The template is read after the name/type writes.
        char TemplateScratch[1024];
        const char* TemplateName = "";
        const char* TemplateSource = Entity->TemplateName;
        if (TemplateSource != nullptr)
            TemplateName = EscapeThornString(TemplateScratch, TemplateSource,
                                             sizeof(TemplateScratch));
        SaveBufAppendStringValue(&Buf, "\t\ttemplate_name = \"%s\",\r\n", TemplateName);
        SaveBufAppendInteger(&Buf, "\t\tlt_grp = %d,", Entity->LtGrp);
        SaveBufAppendInteger(&Buf, " srt_grp = %d,", static_cast<signed char>(Entity->SrtGrp));
        SaveBufAppendInteger(&Buf, " usr_flg = %d", Entity->UsrFlg);

        if (Entity->Flags != 0)
        {
            char FlagStarted = '\0';
            unsigned int KnownFlags = 0x11;
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tflags =");
            if ((Entity->Flags & 1) != 0)
                SaveBufAppendStringFormat(&Buf, " REFERENCE", &FlagStarted);
            if ((Entity->Flags & 0x10) != 0)
                SaveBufAppendStringFormat(&Buf, " HIDDEN", &FlagStarted);
            const int Type = Entity->Type;
            if (Type == 1 || Type == 2 || Type == 13)
            {
                KnownFlags = 0x17;
                if ((Entity->Flags & 4) != 0)
                    SaveBufAppendStringFormat(&Buf, " LIT_DYNAMIC", &FlagStarted);
                if ((Entity->Flags & 2) != 0)
                    SaveBufAppendStringFormat(&Buf, " LIT_AMBIENT", &FlagStarted);
            }
            else if (Type == 6)
            {
                KnownFlags = 0x1f;
                if ((Entity->Flags & 2) != 0)
                    SaveBufAppendStringFormat(&Buf, " SPATIAL", &FlagStarted);
                if ((Entity->Flags & 8) != 0)
                    SaveBufAppendStringFormat(&Buf, " LOOP", &FlagStarted);
                if ((Entity->Flags & 4) != 0)
                    SaveBufAppendStringFormat(&Buf, " STREAM", &FlagStarted);
            }
            else if (Type == 9)
            {
                KnownFlags = 0x31;
                if ((Entity->Flags & 0x20) != 0)
                    SaveBufAppendStringFormat(&Buf, " FOG_PROPS_REMOVED", &FlagStarted);
            }
            const unsigned int UnknownFlags = static_cast<unsigned int>(Entity->Flags) & ~KnownFlags;
            if (UnknownFlags != 0)
            {
                FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\THORN\\LuaScriptParser.cpp",
                      0xa91, "Unknown flag value detected on entity\n");
                if (FlagStarted)
                    SaveBufAppendInteger(&Buf, "+", 0);
                SaveBufAppendInteger(&Buf, " %d", static_cast<int>(
                    static_cast<unsigned int>(Entity->Flags) & ~KnownFlags));
            }
        }

        if (SpatialInSave(Entity->Type))
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tspatialprops =\r\n\t\t{\r\n");
            SaveBufAppendVector(&Buf, "\t\t\tpos = { %.6f, %.6f, %.6f },\r\n", &Pos);
            SaveMatrixValues(&Buf,
                             "\t\t\torient = { { %.6f, %.6f, %.6f },{ %.6f, %.6f, %.6f },"
                             "{ %.6f, %.6f, %.6f } }\r\n", &Orient);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }

        if (Entity->Type == 3)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tcameraprops =\r\n\t\t{\r\n");
            SaveBufAppendRawFloat(&Buf, "\t\t\tfovh = %.6f,\r\n", &Entity->Fovh);
            SaveBufAppendRawFloat(&Buf, "\t\t\thvaspect = %.6f,\r\n", &Entity->Aspect);
            SaveBufAppendRawFloat(&Buf, "\t\t\tnearplane = %.6f,\r\n", &Entity->NearPlane);
            SaveBufAppendRawFloat(&Buf, "\t\t\tfarplane = %.6f\r\n", &Entity->FarPlane);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }
        else if (Entity->Type == 2)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tcompoundprops =\r\n\t\t{\r\n");
            SaveBufAppendRawFloat(&Buf, "\t\t\tfloor_height = %.6f\r\n", &Entity->FloorHeight);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }
        else if (Entity->Type == 5)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tlightprops =\r\n\t\t{\r\n");
            SaveLightProps(&Buf, "\t\t\t", "\r\n", &Entity->Light, 0xffffffffu);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }
        else if (Entity->Type == 6)
        {
            // Key and value in one FUN_06f2cbb0 call each.
            SaveBufAppendLiteral(&Buf, ",\r\n\t\taudioprops =\r\n\t\t{\r\n");
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "attenuation", &Entity->Audio[0]);
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "pan", &Entity->Audio[1]);
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "dmin", &Entity->Audio[2]);
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "dmax", &Entity->Audio[3]);
            SaveBufAppendVariadic(&Buf, "\t\t\t%s = %u,\r\n", "ain", BitsAsUInt(&Entity->Audio[4]));
            SaveBufAppendVariadic(&Buf, "\t\t\t%s = %u,\r\n", "aout", BitsAsUInt(&Entity->Audio[5]));
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "atout", &Entity->Audio[6]);
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "rmix", &Entity->Audio[7]);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }
        else if (Entity->Type == 9 || Entity->Type == 10)
        {
            SaveBufAppendStringValue(&Buf, ",\r\n\t\tup = %s", kAxisNames[Entity->SceneBlob[5]]);
            SaveBufAppendStringValue(&Buf, ",\r\n\t\tfront = %s", kAxisNames[Entity->SceneBlob[6]]);
            SaveBufAppendColor(&Buf, ",\r\n\t\tambient = {%d,%d,%d}",
                               reinterpret_cast<const int*>(Entity->SceneBlob));
        }
        else if (Entity->Type == 11)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tpathprops =\r\n\t\t{\r\n");
            SaveBufAppendStringValue(&Buf, "\r\n\t\t\tpath_type = \"%s\"", Entity->MotionPath.PathType);
            IPersistString* Persist = Entity->MotionPath.MotionPath;
            if (Persist != nullptr)
            {
                const unsigned long Size = Persist->EstimateSize();
                if (Size != 0)
                {
                    char* Data = new char[Size + 1u];
                    // The path object is reread after the size call and allocation.
                    Entity->MotionPath.MotionPath->Save(Data, Size);
                    SaveBufAppendStringValue(&Buf, ",\r\n\t\t\tpath_data = \"%s\"\r\n", Data);
                    delete[] Data;
                }
            }
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }
        else if (Entity->Type == 13)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tpsysprops =\r\n\t\t{\r\n");
            SaveBufAppendRawFloat(&Buf, "\t\t\tsparam = %.6f\r\n", &Entity->PsysSparam);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }

        const unsigned long PropCount = UserPropCount(Index);
        if (PropCount > 0)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tuserprops =\r\n\t\t{\r\n");
            for (unsigned long Prop = 0; Prop < PropCount; ++Prop)
            {
                char* Value = nullptr;
                const char* Key = UserPropKey(Index, static_cast<int>(Prop));
                if (Key == nullptr)
                    continue;
                if (UserProp(reinterpret_cast<void*>(static_cast<INT_PTR>(Index)), Key, &Value) != GR_OK)
                    continue;
                SaveBufAppendStringValue(&Buf, "\t\t\t%s = ", Key);
                SaveBufAppendStringValue(&Buf, "\"%s\",\r\n", Value);
            }
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }

        SaveBufAppendLiteral(&Buf, "\r\n\t}");
    }

    SaveBufAppendLiteral(&Buf, "\r\n};\r\n");
    SaveBufAppendLiteral(&Buf, "\r\nevents =\r\n{");

    for (unsigned int Index = 0; Index < static_cast<unsigned int>(MasterCount_); ++Index)
    {
        // One descriptor read serves all the writes below.
        FThornEventDesc* Descriptor = static_cast<FThornEventDesc*>(
            EventDescByIndex(static_cast<int>(Index)));
        FThornEntitySnap* Primary = EntitySnap(Descriptor->PrimaryIndex);
        FThornEntitySnap* Secondary = Descriptor->SecondaryIndex >= 0
            ? EntitySnap(Descriptor->SecondaryIndex) : nullptr;
        // The type name is looked up before any output.
        const char* EventName = EventTypeName(Descriptor->Type);
        SaveBufAppendStringValue(&Buf, "%s\r\n\t", Index == 0 ? "" : ",");
        SaveBufAppendUnsignedMilliseconds(&Buf, "{%.3f",
                                          static_cast<unsigned int>(Descriptor->StartMs));
        SaveBufAppendStringValue(&Buf, ", %s", EventName);
        SaveBufAppendStringValue(&Buf, ", {\"%s\"", Primary->Name);
        if (Secondary != nullptr)
            SaveBufAppendStringValue(&Buf, ", \"%s\"}", Secondary->Name);
        else
            SaveBufAppendLiteral(&Buf, "}");

        const int EvType = Descriptor->Type;
        if (EvType == kThornTypeSetCamera || EvType < kThornEventTypeMin ||
            EvType > kThornEventTypeMax)
        {
            SaveBufAppendLiteral(&Buf, "}");
            continue;
        }
        if (EvType == kThornTypeUserEvent)
        {
            SaveBufAppendLiteral(&Buf, ",  {");
            SaveBufAppendSignedMilliseconds(&Buf, "duration = %.3f", &Descriptor->DurationMs);
        }
        else if (EvType != kThornTypeStartMotion)
            SaveBufAppendSignedMilliseconds(&Buf, ", {duration = %.3f", &Descriptor->DurationMs);

        if (EvType == kThornTypeUserEvent)
        {
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            // Size+1 allocation; the getter receives Size. Freed on every path.
            if (ComContext != nullptr)
            {
                FThornUserEventDesc* UserDescriptor = reinterpret_cast<FThornUserEventDesc*>(Descriptor);
                const unsigned int Size = ComContext->GetUserEventStringLength(UserDescriptor);
                if (Size != 0)
                {
                    char* Buffer = static_cast<char*>(::operator new(Size + 1u));
                    const GENRESULT Hr = ComContext->GetUserEventString(UserDescriptor, Buffer, Size);
                    if (Hr == GR_OK)
                        SaveBufAppendStringValue(&Buf, ", user_event_string = \"%s\"", Buffer);
                    ::operator delete(Buffer);
                }
            }
        }
        else if (EvType == kThornTypeStartSound)
        {
            const FThornSoundEventDesc* Packet = reinterpret_cast<const FThornSoundEventDesc*>(Descriptor);
            if (Packet->SoundStartTime != 0)
                SaveBufAppendInteger(&Buf, ", start_time = %d", Packet->SoundStartTime);
            if ((Descriptor->Flags & 0xf) != 0)
            {
                SaveBufAppendLiteral(&Buf, ", flags =");
                char Started = '\0';
                if ((Descriptor->Flags & kThornFlagUseScriptDuration) != 0)
                    SaveBufAppendStringFormat(&Buf, " USE_SCRIPT_DURATION", &Started);
                if ((Descriptor->Flags & 2) != 0)
                    SaveBufAppendStringFormat(&Buf, " SPATIAL", &Started);
                if ((Descriptor->Flags & 8) != 0)
                    SaveBufAppendStringFormat(&Buf, " LOOP", &Started);
                if ((Descriptor->Flags & 4) != 0)
                    SaveBufAppendStringFormat(&Buf, " STREAM", &Started);
            }
        }
        else if (EvType == kThornTypeLightPropAnim)
        {
            const FThornLightEventDesc* Packet = reinterpret_cast<const FThornLightEventDesc*>(Descriptor);
            SaveBufAppendLiteral(&Buf, ", lightprops = {");
            SaveLightProps(&Buf, "", "", &Packet->Target,
                           static_cast<unsigned int>(Packet->Mask));
            SaveBufAppendLiteral(&Buf, "}");
            SaveUseScriptDuration(&Buf, Descriptor);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeCameraPropAnim)
        {
            const FThornCameraEventDesc* Packet = reinterpret_cast<const FThornCameraEventDesc*>(Descriptor);
            static const char* const CameraKeys[] = {"fovh", "aspect", "near", "far"};
            static const int CameraBits[] = {
                kThornCamBitFovh, kThornCamBitAspect, kThornCamBitNear, kThornCamBitFar};
            SaveBufAppendLiteral(&Buf, ", cameraprops = {");
            bool bWroteCameraField = false;
            for (int Camera = 0; Camera < 4; ++Camera)
            {
                if ((Packet->Mask & CameraBits[Camera]) == 0)
                    continue;
                if (bWroteCameraField)
                    SaveBufAppendLiteral(&Buf, ", ");
                SaveBufAppendStringValue(&Buf, "%s = ", CameraKeys[Camera]);
                SaveBufAppendRawFloat(&Buf, "%.6f", &Packet->Target[Camera]);
                bWroteCameraField = true;
            }
            SaveBufAppendLiteral(&Buf, "}");
            SaveUseScriptDuration(&Buf, Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypePathAnim)
        {
            const FThornPathEventDesc* Packet = static_cast<const FThornPathEventDesc*>(
                static_cast<const void*>(Descriptor));
            SaveBufAppendRawFloat(&Buf, ", start_percent = %.6f", &Packet->StartPercent);
            SaveBufAppendRawFloat(&Buf, ", stop_percent = %.6f", &Packet->StopPercent);
            SaveBufAppendVector(&Buf, ", offset = { %.6f, %.6f, %.6f }", &Packet->Offset);
            SaveBufAppendStringValue(&Buf, ", up = %s", kAxisNames[Packet->LookAtUpAxis]);
            SaveBufAppendStringValue(&Buf, ", front = %s", kAxisNames[Packet->LookAtFrontAxis]);
            SavePathAttachFlags(&Buf, Descriptor, false);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeSpatialPropAnim)
        {
            const FThornSpatialEventDesc* Packet = reinterpret_cast<const FThornSpatialEventDesc*>(Descriptor);
            if (Packet->TargetPart != nullptr)
                SaveBufAppendStringValue(&Buf, ", target_part = \"%s\"", Packet->TargetPart);
            SaveBufAppendLiteral(&Buf, Packet->TargetType == 1 ? ", target_type = HARDPOINT"
                : Packet->TargetType == 2 ? ", target_type = PART" : ", target_type = ROOT");
            SaveBufAppendLiteral(&Buf, ", spatialprops = {");
            // Retail quirk: only the position branch sets the comma flag, so
            // orient followed by axisrot without pos gets no separating comma.
            const bool bWrotePosition = (Packet->Mask & kThornSpatialBitPos) != 0;
            if (bWrotePosition)
            {
                // Copied before the key is written.
                Vector Position;
                std::memcpy(&Position, &Packet->Position, sizeof(Position));
                SaveBufAppendStringValue(&Buf, "%s = ", "pos");
                SaveBufAppendVector(&Buf, "{ %.6f, %.6f, %.6f }", &Position);
            }
            if ((Packet->Mask & kThornSpatialBitOrient) != 0)
            {
                if (bWrotePosition)
                    SaveBufAppendLiteral(&Buf, ", ");
                if ((Descriptor->Flags & kThornFlagQuatOrient) != 0)
                {
                    // Retail quirk: converts to a matrix (math +0x54) and prints
                    // its first four values (column order) as q_orient.
                    if (G_SpatialWriterMathEngine == nullptr)
                        G_SpatialWriterMathEngine = ThornAcquire3DMathEngine();
                    Quaternion Quat;
                    // Raw W/X/Y/Z, read after the lazy acquire.
                    std::memcpy(&Quat, &Packet->Quaternion, sizeof(Quat));
                    Matrix Orientation;
                    G_SpatialWriterMathEngine->QuaternionToMatrix(Orientation, Quat);
                    Matrix PrintedOrientation;
                    std::memcpy(&PrintedOrientation, Orientation.E, sizeof(PrintedOrientation));
                    SaveMatrixValues(&Buf, "q_orient = { %.6f, %.6f, %.6f, %.6f }", &PrintedOrientation);
                }
                else
                {
                    Matrix PrintedOrientation;
                    std::memcpy(&PrintedOrientation, &Packet->Orient, sizeof(PrintedOrientation));
                    SaveMatrixValues(&Buf,
                                     "orient = { { %.6f, %.6f, %.6f },{ %.6f, %.6f, %.6f },"
                                     "{ %.6f, %.6f, %.6f } }", &PrintedOrientation);
                }
            }
            if ((Packet->Mask & kThornSpatialBitAxisRot) != 0)
            {
                if (bWrotePosition)
                    SaveBufAppendLiteral(&Buf, ", ");
                SaveBufAppendStringValue(&Buf, "%s = ", "axisrot");
                SaveBufAppendRawFloat(&Buf, "{%.6f, ", &Packet->AxisRotAngle);
                SaveBufAppendStringValue(&Buf, "%s}", kAxisNames[Packet->AxisRotAxis]);
            }
            SaveBufAppendLiteral(&Buf, "}");
            SaveUseScriptDuration(&Buf, Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeAttachEntity)
        {
            const FThornAttachEventDesc* Packet = reinterpret_cast<const FThornAttachEventDesc*>(Descriptor);
            SaveBufAppendVector(&Buf, ", offset = { %.6f, %.6f, %.6f }", &Packet->Offset);
            SaveBufAppendStringValue(&Buf, ", up = %s", kAxisNames[Packet->UpAxis]);
            SaveBufAppendStringValue(&Buf, ", front = %s", kAxisNames[Packet->FrontAxis]);
            if (Packet->TargetPart != nullptr)
                SaveBufAppendStringValue(&Buf, ", target_part = \"%s\"", Packet->TargetPart);
            SaveBufAppendLiteral(&Buf, Packet->TargetType == 1 ? ", target_type = HARDPOINT"
                : Packet->TargetType == 2 ? ", target_type = PART" : ", target_type = ROOT");
            SavePathAttachFlags(&Buf, Descriptor, true);
        }
        else if (EvType == kThornTypeConnectHardpoints)
        {
            const FThornConnectEventDesc* Packet = reinterpret_cast<const FThornConnectEventDesc*>(Descriptor);
            SaveBufAppendStringValue(&Buf, ", hardpoint = \"%s\"", Packet->Hardpoint);
            SaveBufAppendStringValue(&Buf, ", parent_hardpoint = \"%s\"", Packet->ParentHardpoint);
            SaveUseScriptDuration(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeStartMotion)
        {
            const FThornMotionEventDesc* Packet = reinterpret_cast<const FThornMotionEventDesc*>(Descriptor);
            if (Packet->Animation != nullptr)
                SaveBufAppendStringValue(&Buf, ", {animation = \"%s\"", Packet->Animation);
            if (Packet->LockedBone != nullptr)
                SaveBufAppendStringValue(&Buf, ", locked_bone = \"%s\"", Packet->LockedBone);
            SaveBufAppendSignedMilliseconds(&Buf, ", duration = %.3f", &Descriptor->DurationMs);
            if (ShouldSaveNonzeroFloat(&Packet->StartTime))
                SaveBufAppendRawFloat(&Buf, ", start_time = %.3f", &Packet->StartTime);
            if (ShouldSaveNonzeroFloat(&Packet->TransTime))
                SaveBufAppendRawFloat(&Buf, ", trans_time = %.3f", &Packet->TransTime);
            if (ShouldSaveNonzeroFloat(&Packet->TimeScale))
                SaveBufAppendRawFloat(&Buf, ", time_scale = %.6f", &Packet->TimeScale);
            if (ShouldSaveNonzeroFloat(&Packet->Weight))
                SaveBufAppendRawFloat(&Buf, ", weight = %.6f", &Packet->Weight);
            // Tests the primary entity's type (read earlier), after weight.
            if (Primary->Type == 2)
                SaveBufAppendRawFloat(&Buf, ", heading = %.6f", &Packet->Heading);
            else if (ShouldSaveNonzeroFloat(&Packet->TransScale))
                SaveBufAppendRawFloat(&Buf, ", trans_scale = %.6f", &Packet->TransScale);
            SaveUseScriptDuration(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeStartIk)
        {
            const FThornStartIkDesc* Packet = reinterpret_cast<const FThornStartIkDesc*>(Descriptor);
            SaveBufAppendStringValue(&Buf, ", end_effector = \"%s\"", Packet->EndEffector);
            SaveBufAppendInteger(&Buf, ", count_to_root = %d", Packet->CountToRoot);
            if (Packet->TargetPart != nullptr)
                SaveBufAppendStringValue(&Buf, ", target_part = \"%s\"", Packet->TargetPart);
            SaveBufAppendLiteral(&Buf, Packet->TargetType == 1 ? ", target_type = HARDPOINT"
                : Packet->TargetType == 2 ? ", target_type = PART" : ", target_type = ROOT");
            SaveBufAppendRawFloat(&Buf, ", damping = %.6f", &Packet->Damping);
            SaveBufAppendStringValue(&Buf, ", up = %s", kAxisNames[Packet->LookAtUpAxis]);
            SaveBufAppendStringValue(&Buf, ", front = %s", kAxisNames[Packet->LookAtFrontAxis]);
            SaveBufAppendInteger(&Buf, ", point_at = %d", static_cast<int>(Packet->bPointAt));
            SaveBufAppendInteger(&Buf, ", move_to = %d", static_cast<int>(Packet->bMoveTo));
            SaveBufAppendRawFloat(&Buf, ", transition_duration = %.3f", &Packet->TransitionDuration);
            SaveUseScriptDuration(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeStartSubScene)
        {
            const FThornSubSceneEventDesc* Packet = reinterpret_cast<const FThornSubSceneEventDesc*>(Descriptor);
            if (ShouldSaveNonzeroFloat(&Packet->StartTime))
                SaveBufAppendPromotedFloatAsInteger(&Buf, ", start_time = %d", &Packet->StartTime);
            // Flags are reread around the prefix append.
            if ((Descriptor->Flags & kThornFlagUseScriptDuration) != 0)
            {
                SaveBufAppendLiteral(&Buf, ", flags =");
                char Started = '\0';
                if ((Descriptor->Flags & kThornFlagUseScriptDuration) != 0)
                    SaveBufAppendStringFormat(&Buf, " USE_SCRIPT_DURATION", &Started);
            }
        }
        else if (EvType == kThornTypePsysPropAnim)
        {
            const FThornPsysPropEventDesc* Packet = reinterpret_cast<const FThornPsysPropEventDesc*>(Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveBufAppendLiteral(&Buf, ", psysprops = {");
            if ((Packet->Mask & 1) != 0)
            {
                SaveBufAppendStringValue(&Buf, "%s = ", "sparam");
                SaveBufAppendRawFloat(&Buf, "%.6f", &Packet->Sparam);
            }
            SaveBufAppendLiteral(&Buf, "}");
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeAudioPropAnim)
        {
            const FThornAudioPropEventDesc* Packet = reinterpret_cast<const FThornAudioPropEventDesc*>(Descriptor);
            static const char* const AudioKeys[] = {
                "attenuation", "pan", "dmin", "dmax", "ain", "aout", "atout", "rmix"};
            static const int AudioBits[] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80};
            SaveBufAppendLiteral(&Buf, ", audioprops = {");
            bool bWroteAudioField = false;
            for (int Audio = 0; Audio < 8; ++Audio)
            {
                if ((Packet->Mask & AudioBits[Audio]) == 0)
                    continue;
                if (bWroteAudioField)
                    SaveBufAppendLiteral(&Buf, ", ");
                SaveBufAppendStringValue(&Buf, "%s = ", AudioKeys[Audio]);
                if (Audio == 4 || Audio == 5)
                {
                    const FThornEntitySnap* SecondaryDesc = Secondary != nullptr
                        ? Secondary : nullptr;
                    SaveBufAppendIntegerAsDouble(&Buf, static_cast<unsigned int>(Audio == 4 ? Packet->Target.Ain : Packet->Target.Aout),
                                                 SecondaryDesc);
                }
                else
                {
                    const float* Fields[8] = {&Packet->Target.Attenuation, &Packet->Target.Pan,
                        &Packet->Target.Dmin, &Packet->Target.Dmax, nullptr, nullptr,
                        &Packet->Target.Atout, &Packet->Target.Rmix};
                    SaveBufAppendRawFloat(&Buf, "%.6f", Fields[Audio]);
                }
                bWroteAudioField = true;
            }
            SaveBufAppendLiteral(&Buf, "}");
            SaveUseScriptDuration(&Buf, Descriptor);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeFogPropAnim)
        {
            const FThornFogEventDesc* Packet = reinterpret_cast<const FThornFogEventDesc*>(Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveBufAppendLiteral(&Buf, ", fogprops = {");
            SaveFogProps(&Buf, &Packet->Target, static_cast<unsigned char>(Packet->Mask));
            SaveBufAppendLiteral(&Buf, "}");
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeReverbPropAnim)
        {
            const FThornReverbEventDesc* Packet = reinterpret_cast<const FThornReverbEventDesc*>(Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveBufAppendLiteral(&Buf, ", reverbprops = {");
            SaveReverbProps(&Buf, &Packet->Target, static_cast<unsigned char>(Packet->Mask));
            SaveBufAppendLiteral(&Buf, "}");
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeFloorHeightAnim)
        {
            const FThornFloorEventDesc* Packet = reinterpret_cast<const FThornFloorEventDesc*>(Descriptor);
            if (Packet->TargetPart != nullptr)
                SaveBufAppendStringValue(&Buf, ", target_part = \"%s\"", Packet->TargetPart);
            SaveBufAppendLiteral(&Buf, Packet->TargetType == 1 ? ", target_type = HARDPOINT"
                : Packet->TargetType == 2 ? ", target_type = PART" : ", target_type = ROOT");
            SaveBufAppendRawFloat(&Buf, ", floor_height = %.6f", &Packet->FloorHeight);
            SaveUseScriptDuration(&Buf, Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeSubtitle)
        {
            const FThornSubtitleEventDesc* Packet = reinterpret_cast<const FThornSubtitleEventDesc*>(Descriptor);
            if (Packet->Strid != 0)
                SaveBufAppendUnsigned(&Buf, ", strid = %u", Packet->Strid);
        }
        // Shared tail of these eight branches.
        if (EvType == kThornTypeStartMotion || EvType == kThornTypeStartSound ||
            EvType == kThornTypeStartIk || EvType == kThornTypeAttachEntity ||
            EvType == kThornTypeConnectHardpoints || EvType == kThornTypeStartSubScene ||
            EvType == kThornTypeSubtitle || EvType == kThornTypeStartPsys)
        {
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
        }

        // Two separate appends, as in retail.
        SaveBufAppendLiteral(&Buf, "}");
        SaveBufAppendLiteral(&Buf, "}");
    }

    SaveBufAppendLiteral(&Buf, "\r\n};\r\n");

    // The final flush status is discarded.
    SaveBufFlush(&Buf);
    return GR_OK;
}

// FUN_06f27bf0. Retail's table (DAT_06f5e1e8) in retail order: entries 6..12
// do not follow the kThornType* numbering (6 is PathAnim, 7 SpatialPropAnim,
// 10..12 StartMotion/StartIk/StartSubScene), so those log the wrong name.
const char* ThornEventTypeDebugName(unsigned int Type)
{
    static const char* const Names[] = {
        "UNDEFINED",
        "USER_EVENT",
        "SET_MONITOR",
        "START_SOUND",
        "START_LIGHT_PROPERTY_ANIM",
        "START_CAMERA_PROPERTY_ANIM",
        "START_SPATIAL_PROPERTY_ANIM",
        "START_IK",
        "START_PATH_MOTION",
        "ATTACH_ENTITY",
        "CONNECT_ENTITY",
        "START_MOTION",
        "USER",
        "START_PSYS",
        "START_PSYS_PROPERTY_ANIM",
        "START_AUDIO_PROPERTY_ANIM",
        "START_FOG_PROPERTY_ANIM",
        "START_REVERB_PROP_ANIM",
        "START_FLR_HEIGHT_ANIM",
        "SUBTITLE",
    };
    if (Type >= 20)
        return "<out of range>";
    return Names[Type];
}
