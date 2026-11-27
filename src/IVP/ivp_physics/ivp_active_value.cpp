// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP active values and their manager. See ivp_active_value.h for the class
// map (vtables, constructors, sizes).

#include "ivp_active_value.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ivp_core.h"
#include "ivp_environment.h"
#include "../ivp_utility/ivp_crc32.h" // g_IVP_Crc32Table (GetHashIndex, FUN_06361530)
#include "../ivp_utility/ivp_hash.h"  // FUN_063569e0, the three-way name compare

namespace
{
    // FUN_063569c0. Returns 0 for a null or empty string, else its length --
    // the "has a name" gate InsertActiveFloat / InsertActiveInt apply before
    // hashing.
    int IVP_StringLengthOrZero(const char* String)
    {
        if (String == nullptr || *String == '\0')
        {
            return 0;
        }
        return (int)strlen(String);
    }
}

// .data:063ee640.
int GActiveValueChangeCounter = -1;

// ============================================================================
// IVP_U_Active_Value
// ============================================================================

// FUN_06361e00: vtable 063a57e8; Name via FUN_06356920 (null-safe strdup).
IVP_U_Active_Value::IVP_U_Active_Value(const char* NameIn)
    : Name(NameIn != nullptr ? _strdup(NameIn) : nullptr)
    , ReferenceCount(0)
{
}

// FUN_06361e50; Name is freed through FUN_06343fb0.
IVP_U_Active_Value::~IVP_U_Active_Value()
{
    if (Name != nullptr)
    {
        free(Name);
        Name = nullptr;
    }
}

// ============================================================================
// IVP_U_Active_Float / IVP_U_Active_Int
// ============================================================================

// FUN_06361e80: vtable 063a57ec; every member zeroed.
IVP_U_Active_Float::IVP_U_Active_Float(const char* NameIn)
    : IVP_U_Active_Value(NameIn)
    , Manager(nullptr)
    , Field_0x18(0)
    , DoubleValue(0.0f)
{
}

// FUN_06362060: highest index first.
void IVP_U_Active_Float::FireListeners()
{
    for (int Index = static_cast<int>(Listeners.n_elems) - 1; Index >= 0; --Index)
    {
        Listeners.elems[Index]->ActiveFloatChanged(this);
    }
}

// FUN_06361f00: as FUN_06361e80 with vtable 063a57f4.
IVP_U_Active_Int::IVP_U_Active_Int(const char* NameIn)
    : IVP_U_Active_Value(NameIn)
    , Manager(nullptr)
    , Field_0x18(0)
    , IntValue(0)
{
}

// FUN_06362060 (shared body).
void IVP_U_Active_Int::FireListeners()
{
    for (int Index = static_cast<int>(Listeners.n_elems) - 1; Index >= 0; --Index)
    {
        Listeners.elems[Index]->ActiveIntChanged(this);
    }
}

// ============================================================================
// IVP_U_Active_Terminal_Double
// ============================================================================

// FUN_06362080: vtables 063a5800 (primary) and 063a57fc (+0x20).
IVP_U_Active_Terminal_Double::IVP_U_Active_Terminal_Double(const char* NameIn, float Value)
    : IVP_U_Active_Float(NameIn)
{
    DoubleValue = Value;
    LastFiredValue = Value;
}

// LAB_063621f0.
int IVP_U_Active_Terminal_Double::PrintTypeName()
{
    printf("DoubleVal");
    return 0;
}

// LAB_063621a0: when delayed and owned by a manager, queue the +0x20
// sub-object (manager slot 6), otherwise fire at once.
void IVP_U_Active_Terminal_Double::SetDoubleValue(float Value, int32_t bDelayed)
{
    DoubleValue = Value;
    ++GActiveValueChangeCounter;
    if (bDelayed != 0 && Manager != nullptr)
    {
        Manager->AddDelayedFloat(static_cast<IVP_U_Active_Float_Delayed*>(this));
        return;
    }
    static_cast<IVP_U_Active_Float_Delayed*>(this)->FireIfChanged();
}

// LAB_06362160 (secondary slot 0). Equal and unordered both return, so the
// listeners fire only when the two values are ordered and different.
void IVP_U_Active_Terminal_Double::FireIfChanged()
{
    if (!(DoubleValue < LastFiredValue) && !(DoubleValue > LastFiredValue))
    {
        return;
    }
    LastFiredValue = DoubleValue;
    FireListeners();
}

// ============================================================================
// IVP_U_Active_Terminal_Int
// ============================================================================

// FUN_063620f0: vtables 063a5814 (primary) and 063a5810 (+0x20).
IVP_U_Active_Terminal_Int::IVP_U_Active_Terminal_Int(const char* NameIn, int32_t Value)
    : IVP_U_Active_Int(NameIn)
{
    IntValue = Value;
    LastFiredValue = Value;
}

// LAB_06362260.
int IVP_U_Active_Terminal_Int::PrintTypeName()
{
    printf("IntVal");
    return 0;
}

// LAB_06362210: as SetDoubleValue, queueing through manager slot 7.
void IVP_U_Active_Terminal_Int::SetIntValue(int32_t Value, int32_t bDelayed)
{
    IntValue = Value;
    ++GActiveValueChangeCounter;
    if (bDelayed != 0 && Manager != nullptr)
    {
        Manager->AddDelayedInt(static_cast<IVP_U_Active_Int_Delayed*>(this));
        return;
    }
    static_cast<IVP_U_Active_Int_Delayed*>(this)->FireIfChanged();
}

// LAB_06362180.
void IVP_U_Active_Terminal_Int::FireIfChanged()
{
    if (IntValue == LastFiredValue)
    {
        return;
    }
    LastFiredValue = IntValue;
    FireListeners();
}

// ============================================================================
// IVP_U_Active_Value_Hash
// ============================================================================

// LAB_06361580: 1 when FUN_063569e0 reports the names equal, else 0.
int IVP_U_Active_Value_Hash::Compare(void* Elem0, void* Elem1)
{
    const IVP_U_Active_Value* Value0 = static_cast<const IVP_U_Active_Value*>(Elem0);
    const IVP_U_Active_Value* Value1 = static_cast<const IVP_U_Active_Value*>(Elem1);
    const int Result = IVP_CompareStringsNullsLast(Value0->Name, Value1->Name);
    return Result == 0 ? 1 : 0;
}

// FUN_063614a0: every stored value (SizeMm down to 0) loses one reference and
// is deleted at zero; then the base destructor FUN_063522f0.
IVP_U_Active_Value_Hash::~IVP_U_Active_Value_Hash()
{
    struct FElem
    {
        unsigned HashIndex;
        IVP_U_Active_Value* Value;
    };
    FElem* Entries = static_cast<FElem*>(Elems);
    for (int Index = SizeMm; Index >= 0; --Index)
    {
        IVP_U_Active_Value* Value = Entries[Index].Value;
        if (Value == nullptr)
            continue;

        --Value->ReferenceCount;
        if (Value->ReferenceCount == 0)
        {
            delete Value;
        }
    }
}

// FUN_06361530 (thiscall, RET 4; the receiver is unused). CRC-32 of the name
// (IEEE table g_IVP_Crc32Table) | 0x80000000, the IVP_VHash "occupied"
// marker. Retail has no null-name guard (callers gate on FUN_063569c0 first).
unsigned IVP_U_Active_Value_Hash::GetHashIndex(IVP_U_Active_Value* Value)
{
    const unsigned char* s = (const unsigned char*)Value->Name;
    unsigned crc = 0xffffffffu;
    while (*s != 0)
    {
        crc = (crc >> 8) ^ g_IVP_Crc32Table[(*s ^ crc) & 0xffu];
        ++s;
    }
    return crc | 0x80000000u;
}

// ============================================================================
// IVP_U_Active_Value_Manager
// ============================================================================

// FUN_063615a0: vtable 063a57ac; the hashes are built through FUN_063522a0(0x10);
// ends with a direct (non-virtual) call of slot 9, FUN_063617b0.
IVP_U_Active_Value_Manager::IVP_U_Active_Value_Manager(int32_t DeleteOnEnvDeleteIn)
    : DeleteOnEnvDelete(DeleteOnEnvDeleteIn)
    , FloatHash(nullptr)
    , IntHash(nullptr)
    , CurrentTime(nullptr)
    , SearchTemplate(nullptr)
{
    FloatHash = new IVP_U_Active_Value_Hash(0x10);
    IntHash = new IVP_U_Active_Value_Hash(0x10);
    SearchTemplate = new IVP_U_Active_Value(nullptr);
    CurrentTime = nullptr;
    InitActiveValuesGeneric();
}

// FUN_063616e0. The two vector members are destroyed after this body, in
// reverse declaration order. CurrentTime is not deleted here: it lives in
// FloatHash, whose destructor drops the reference.
IVP_U_Active_Value_Manager::~IVP_U_Active_Value_Manager()
{
    if (FloatHash != nullptr)
    {
        delete FloatHash;
    }
    FloatHash = nullptr;
    if (IntHash != nullptr)
    {
        delete IntHash;
    }
    IntHash = nullptr;
    if (SearchTemplate != nullptr)
    {
        delete SearchTemplate;
    }
    SearchTemplate = nullptr;
}

// LAB_063616a0.
void IVP_U_Active_Value_Manager::EnvironmentWillBeDeleted(IVP_Environment* Environment)
{
    (void)Environment;
    if (DeleteOnEnvDelete != 0)
    {
        delete this;
    }
}

// FUN_06361860.
void IVP_U_Active_Value_Manager::InsertActiveFloat(IVP_U_Active_Float* Value)
{
    char* ValueName = Value->Name;
    if (IVP_StringLengthOrZero(ValueName) == 0)
    {
        printf("insert_active_float: tried to insert active_IVP_FLOAT without name!\n");
        return;
    }
    if (FloatHash->FindElem(Value, FloatHash->GetHashIndex(Value)) != nullptr)
    {
        printf("insert_active_float: name '%s' already exists in name_hash!\n", ValueName);
        return;
    }
    FloatHash->AddElem(Value, FloatHash->GetHashIndex(Value));
    Value->Manager = this;
    Value->ReferenceCount = Value->ReferenceCount + 1;
}

// LAB_06361980. Retail quirk: the reference is decremented before the null
// test of the removed pointer, so an absent value writes through null.
void IVP_U_Active_Value_Manager::RemoveActiveFloat(IVP_U_Active_Float* Value)
{
    IVP_U_Active_Value* Removed =
        static_cast<IVP_U_Active_Value*>(FloatHash->RemoveElem(Value, FloatHash->GetHashIndex(Value)));
    Removed->ReferenceCount = Removed->ReferenceCount - 1;
    if (Removed->ReferenceCount == 0 && Removed != nullptr)
    {
        delete Removed;
    }
}

// FUN_063618f0. As InsertActiveFloat on IntHash, except that retail adds 2
// references, so one RemoveActiveInt never deletes the value.
void IVP_U_Active_Value_Manager::InsertActiveInt(IVP_U_Active_Int* Value)
{
    char* ValueName = Value->Name;
    if (IVP_StringLengthOrZero(ValueName) == 0)
    {
        printf("insert_active_int: tried to insert active_int without name!\n");
        return;
    }
    if (IntHash->FindElem(Value, IntHash->GetHashIndex(Value)) != nullptr)
    {
        printf("insert_active_int: name '%s' already exists in name_hash!\n", ValueName);
        return;
    }
    IntHash->AddElem(Value, IntHash->GetHashIndex(Value));
    Value->Manager = this;
    Value->ReferenceCount = Value->ReferenceCount + 2;
}

// LAB_063619c0: RemoveActiveFloat on IntHash.
void IVP_U_Active_Value_Manager::RemoveActiveInt(IVP_U_Active_Int* Value)
{
    IVP_U_Active_Value* Removed =
        static_cast<IVP_U_Active_Value*>(IntHash->RemoveElem(Value, IntHash->GetHashIndex(Value)));
    Removed->ReferenceCount = Removed->ReferenceCount - 1;
    if (Removed->ReferenceCount == 0 && Removed != nullptr)
    {
        delete Removed;
    }
}

// LAB_06361a00: appends only when absent.
void IVP_U_Active_Value_Manager::AddDelayedFloat(IVP_U_Active_Float_Delayed* Value)
{
    if (!DelayedFloats.Contains(Value))
    {
        DelayedFloats.Add(Value);
    }
}

// LAB_06361a50.
void IVP_U_Active_Value_Manager::AddDelayedInt(IVP_U_Active_Int_Delayed* Value)
{
    if (!DelayedInts.Contains(Value))
    {
        DelayedInts.Add(Value);
    }
}

// FUN_06361aa0: lowest index first, n_elems re-read after each call.
void IVP_U_Active_Value_Manager::FireDelayed()
{
    for (int Index = 0; Index < static_cast<int>(DelayedFloats.n_elems); ++Index)
    {
        DelayedFloats.elems[Index]->FireIfChanged();
    }
    DelayedFloats.Clear();
    for (int Index = 0; Index < static_cast<int>(DelayedInts.n_elems); ++Index)
    {
        DelayedInts.elems[Index]->FireIfChanged();
    }
    DelayedInts.Clear();
}

// FUN_063617b0.
void IVP_U_Active_Value_Manager::InitActiveValuesGeneric()
{
    IVP_U_Active_Terminal_Double* DoubleNull = new IVP_U_Active_Terminal_Double("double_null", 0.0f);
    CurrentTime = new IVP_U_Active_Terminal_Double("current_time", 0.0f);
    InsertActiveFloat(DoubleNull);
    InsertActiveFloat(CurrentTime);
}

// LAB_06361b30.
void IVP_U_Active_Value_Manager::RefreshPsiActiveValues(IVP_Environment* Environment)
{
    if (CurrentTime != nullptr)
    {
        const float Now = Environment->current_time.time_offset + Environment->current_time.time_base;
        CurrentTime->SetDoubleValue(Now, 0);
    }
    FireDelayed();
}

// LAB_06361b80.
IVP_U_Active_Float* IVP_U_Active_Value_Manager::GetActiveFloat(const char* Name, float Value)
{
    SearchTemplate->Name = const_cast<char*>(Name);
    void* Found = FloatHash->FindElem(SearchTemplate, FloatHash->GetHashIndex(SearchTemplate));
    SearchTemplate->Name = nullptr;
    if (Found != nullptr)
    {
        return static_cast<IVP_U_Active_Float*>(static_cast<IVP_U_Active_Value*>(Found));
    }
    IVP_U_Active_Terminal_Double* Created = new IVP_U_Active_Terminal_Double(Name, Value);
    InsertActiveFloat(Created);
    return Created;
}

// LAB_06361c20.
IVP_U_Active_Float* IVP_U_Active_Value_Manager::CreateActiveFloat(const char* Name, float Value)
{
    SearchTemplate->Name = const_cast<char*>(Name);
    void* Found = FloatHash->FindElem(SearchTemplate, FloatHash->GetHashIndex(SearchTemplate));
    SearchTemplate->Name = nullptr;
    if (Found != nullptr)
    {
        return nullptr;
    }
    IVP_U_Active_Terminal_Double* Created = new IVP_U_Active_Terminal_Double(Name, Value);
    InsertActiveFloat(Created);
    return Created;
}

// LAB_06361d60.
IVP_U_Active_Int* IVP_U_Active_Value_Manager::GetActiveInt(const char* Name, int32_t Value)
{
    SearchTemplate->Name = const_cast<char*>(Name);
    void* Found = IntHash->FindElem(SearchTemplate, IntHash->GetHashIndex(SearchTemplate));
    SearchTemplate->Name = nullptr;
    if (Found != nullptr)
    {
        return static_cast<IVP_U_Active_Int*>(static_cast<IVP_U_Active_Value*>(Found));
    }
    IVP_U_Active_Terminal_Int* Created = new IVP_U_Active_Terminal_Int(Name, Value);
    InsertActiveInt(Created);
    return Created;
}

// LAB_06361cc0.
IVP_U_Active_Int* IVP_U_Active_Value_Manager::CreateActiveInt(const char* Name, int32_t Value)
{
    SearchTemplate->Name = const_cast<char*>(Name);
    void* Found = IntHash->FindElem(SearchTemplate, IntHash->GetHashIndex(SearchTemplate));
    SearchTemplate->Name = nullptr;
    if (Found != nullptr)
    {
        return nullptr;
    }
    IVP_U_Active_Terminal_Int* Created = new IVP_U_Active_Terminal_Int(Name, Value);
    InsertActiveInt(Created);
    return Created;
}
