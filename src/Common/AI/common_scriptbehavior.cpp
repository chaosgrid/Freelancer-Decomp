// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- ScriptBehavior (the marshalled script-behavior description).

#include "common_iface.h"
#include "common_misc.h" // malloc_strcpy / malloc_strfree -- ScriptBehavior string slots

#include <cstring>

// ============================================================================
// ScriptBehavior
// ============================================================================

// The string slots (StringArray[i] / StringArray2[i]) own _strdup / MarshalBuffer::get_string
// heap copies that malloc_strfree releases. Only the first IdArrayCount entries of StringArray
// are ever freed (destroy, dtor, operator=, init all loop `i < [this+0x118]`), while all 32
// StringArray2 entries are freed unconditionally. Neither count is clamped to 32 anywhere in
// retail: a count above 32 indexes past the arrays exactly as retail does; every producer
// (init from a description, unmarshal of a server-built buffer) keeps them <= 32.

// .text:0632dcb0. The FmtStr members are zero-constructed inline, then clear() -- which only
// zeroes, never frees, so the uninitialised string slots are never touched.
ScriptBehavior::ScriptBehavior()
{
    clear();
}

// .text:0632d760. A plain memberwise copy: the pointer arrays are block-copied, so the copy
// shares the source's strings; only the FmtStr members are deep-copied. Destroying both would
// free the shared strings twice -- retail's own hazard; no retail consumer calls this overload
// (Server.dll and freelancer.exe import only the default ctor, ordinal 417).
ScriptBehavior::ScriptBehavior(ScriptBehavior const& Other)
    : NameFmtStr(Other.NameFmtStr)
    , DescFmtStr(Other.DescFmtStr)
{
    BehaviorId = Other.BehaviorId;
    LocationId = Other.LocationId;
    bApplySpatial = Other.bApplySpatial;
    bAutoplay = Other.bAutoplay;
    bGenericScript = Other.bGenericScript;
    RelocateLocationId = Other.RelocateLocationId;
    StartRoomLocationId = Other.StartRoomLocationId;
    bBindSingleCharacter = Other.bBindSingleCharacter;
    for (int I = 0; I < 32; ++I)
    {
        IdArray[I] = Other.IdArray[I];
    }
    for (int I = 0; I < 32; ++I)
    {
        StringArray[I] = Other.StringArray[I];
    }
    IdArrayCount = Other.IdArrayCount;
    for (int I = 0; I < 32; ++I)
    {
        StringArray2[I] = Other.StringArray2[I];
    }
    for (int I = 0; I < 32; ++I)
    {
        StringArray2Dword[I] = Other.StringArray2Dword[I];
    }
    StringArray2Count = Other.StringArray2Count;
    DialogType = Other.DialogType;
    OfferId = Other.OfferId;
    OfferKind = Other.OfferKind;
    RtcId = Other.RtcId;
}

// .text:0632ddb0. destroy() inlined, then the compiler-generated ~FmtStr member destructors.
ScriptBehavior::~ScriptBehavior()
{
    destroy();
}

// .text:0632e370. No self-assignment test: retail releases its own strings and FmtStr first
// (destroy() inlined) and then copies from Other.
ScriptBehavior& ScriptBehavior::operator=(ScriptBehavior const& Other)
{
    destroy();
    BehaviorId = Other.BehaviorId;
    LocationId = Other.LocationId;
    bApplySpatial = Other.bApplySpatial;
    bAutoplay = Other.bAutoplay;
    bGenericScript = Other.bGenericScript;
    RelocateLocationId = Other.RelocateLocationId;
    StartRoomLocationId = Other.StartRoomLocationId;
    bBindSingleCharacter = Other.bBindSingleCharacter;

    IdArrayCount = Other.IdArrayCount;
    for (int I = 0; I < IdArrayCount; ++I)
    {
        IdArray[I] = Other.IdArray[I];
        malloc_strcpy(&StringArray[I], Other.StringArray[I]);
    }

    StringArray2Count = Other.StringArray2Count;
    for (int I = 0; I < StringArray2Count; ++I)
    {
        malloc_strcpy(&StringArray2[I], Other.StringArray2[I]);
        StringArray2Dword[I] = Other.StringArray2Dword[I];
    }

    DialogType = Other.DialogType;
    OfferId = Other.OfferId;
    OfferKind = Other.OfferKind;
    NameFmtStr = Other.NameFmtStr;
    DescFmtStr = Other.DescFmtStr;
    RtcId = Other.RtcId;
    return *this;
}

// .text:0632de70. Resets every field without releasing anything: the string slots are only
// overwritten with 0, so a behaviour that still owns strings leaks them -- retail's contract,
// and what makes clear() safe to run from the default ctor on uninitialised slots. The FmtStr
// members are destroyed and then their 0x18 bytes zeroed.
void ScriptBehavior::clear()
{
    bApplySpatial = 1;
    bGenericScript = 1;
    bBindSingleCharacter = 1;
    BehaviorId = -1;
    LocationId = 0;
    bAutoplay = 0;
    RelocateLocationId = 0;
    StartRoomLocationId = 0;
    for (int I = 0; I < 32; ++I)
    {
        IdArray[I] = 0;
        StringArray[I] = nullptr;
    }
    IdArrayCount = 0;
    for (int I = 0; I < 32; ++I)
    {
        StringArray2[I] = nullptr;
        StringArray2Dword[I] = 0;
    }
    StringArray2Count = 0;
    DialogType = 0;
    OfferId = 0;
    OfferKind = 0;
    NameFmtStr.destroy();
    memset(&NameFmtStr, 0, sizeof(NameFmtStr));
    DescFmtStr.destroy();
    memset(&DescFmtStr, 0, sizeof(DescFmtStr));
    RtcId = 0;
}

// .text:0632df20. Releases the owned strings and the FmtStr contents; counts and the other
// fields are left as they are.
void ScriptBehavior::destroy()
{
    for (int I = 0; I < IdArrayCount; ++I)
    {
        malloc_strfree(&StringArray[I]);
    }
    for (int I = 0; I < 32; ++I)
    {
        malloc_strfree(&StringArray2[I]);
    }
    NameFmtStr.destroy();
    DescFmtStr.destroy();
}

unsigned long ScriptBehavior::get_key() const
{
    // .text:06261d90 (export 2848, ICF-folded with a dozen trivial getters): the dword at
    // +0x00, BehaviorId.
    return static_cast<unsigned long>(BehaviorId);
}

// .text:0632e530. Starts with destroy() inlined (StringArray freed for i < the old
// IdArrayCount) -- no clear(), no zeroing -- then copies the description. malloc_strcpy
// stores a null slot for a null source.
void ScriptBehavior::init(CharacterBehaviorDescription const& Desc)
{
    destroy();
    BehaviorId = -1;
    LocationId = static_cast<unsigned>(Desc.LocationId);
    bApplySpatial = Desc.bApplySpatial;
    bAutoplay = Desc.bAutoplay;
    bGenericScript = Desc.bGenericScript;
    RelocateLocationId = Desc.RelocateLocationId;
    StartRoomLocationId = Desc.StartRoomLocationId;
    bBindSingleCharacter = Desc.bBindSingleCharacter;

    IdArrayCount = Desc.IdArrayCount;
    for (int I = 0; I < IdArrayCount; ++I)
    {
        IdArray[I] = Desc.IdArray[I];
        malloc_strcpy(&StringArray[I], Desc.StringArray[I]);
    }

    StringArray2Count = Desc.StringArray2Count;
    for (int I = 0; I < StringArray2Count; ++I)
    {
        malloc_strcpy(&StringArray2[I], Desc.StringArray2[I]);
        StringArray2Dword[I] = Desc.StringArray2Dword[I];
    }

    DialogType = Desc.DialogType;
    OfferId = Desc.OfferId;
    OfferKind = Desc.OfferKind;
    NameFmtStr = Desc.NameFmtStr;
    DescFmtStr = Desc.DescFmtStr;
    RtcId = Desc.RtcId;
}

// .text:0632df90. Field order on the wire as retail; put_bool normalises to 0/1 itself.
void ScriptBehavior::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&BehaviorId, 4);
    Buf->put_void(&LocationId, 4);
    Buf->put_bool(bApplySpatial != 0);
    Buf->put_bool(bAutoplay != 0);
    Buf->put_bool(bGenericScript != 0);
    Buf->put_void(&RelocateLocationId, 4);
    Buf->put_void(&StartRoomLocationId, 4);
    Buf->put_bool(bBindSingleCharacter != 0);
    Buf->put_void(&IdArrayCount, 4);
    for (int I = 0; I < IdArrayCount; ++I)
    {
        Buf->put_void(&IdArray[I], 4);
        Buf->put_string(StringArray[I]);
    }
    Buf->put_void(&StringArray2Count, 4);
    for (int I = 0; I < StringArray2Count; ++I)
    {
        Buf->put_string(StringArray2[I]);
        Buf->put_void(&StringArray2Dword[I], 4);
    }
    Buf->put_void(&DialogType, 4);
    Buf->put_void(&OfferId, 4);
    Buf->put_void(&OfferKind, 4);
    Buf->put_FmtStr(NameFmtStr);
    Buf->put_FmtStr(DescFmtStr);
    Buf->put_void(&RtcId, 4);
}

// .text:0632e160. No clear(), no clamp: every field is overwritten in place (string slots
// that still owned a string leak, as in retail). Each dword is read through one scratch slot
// (retail reuses its `param_1` stack cell), so when get_void has too few bytes left and
// writes nothing, the field receives the slot's previous content (initially the
// MarshalBuffer pointer itself). Deviation: the trailing +0x25c dword goes through a
// never-initialised stack cell in retail; the port starts it at 0 (differs only on a
// truncated buffer).
void ScriptBehavior::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Buf));

    Buf->get_void(&Scratch, 4);
    BehaviorId = static_cast<int>(Scratch);
    Buf->get_void(&Scratch, 4);
    LocationId = Scratch;
    bApplySpatial = Buf->get_bool();
    bAutoplay = Buf->get_bool();
    bGenericScript = Buf->get_bool();
    Buf->get_void(&Scratch, 4);
    RelocateLocationId = Scratch;
    Buf->get_void(&Scratch, 4);
    StartRoomLocationId = Scratch;
    bBindSingleCharacter = Buf->get_bool();
    Buf->get_void(&Scratch, 4);
    IdArrayCount = static_cast<int>(Scratch);
    for (int I = 0; I < IdArrayCount; ++I)
    {
        Buf->get_void(&Scratch, 4);
        IdArray[I] = Scratch;
        StringArray[I] = Buf->get_string();
    }
    Buf->get_void(&Scratch, 4);
    StringArray2Count = static_cast<int>(Scratch);
    for (int I = 0; I < StringArray2Count; ++I)
    {
        StringArray2[I] = Buf->get_string();
        Buf->get_void(&Scratch, 4);
        StringArray2Dword[I] = Scratch;
    }
    Buf->get_void(&Scratch, 4);
    DialogType = Scratch;
    Buf->get_void(&Scratch, 4);
    OfferId = Scratch;
    Buf->get_void(&Scratch, 4);
    OfferKind = Scratch;
    NameFmtStr = Buf->get_FmtStr();
    DescFmtStr = Buf->get_FmtStr();
    unsigned int TrailingScratch = 0;
    Buf->get_void(&TrailingScratch, 4);
    RtcId = TrailingScratch;
}
