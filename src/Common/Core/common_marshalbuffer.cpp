// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll MarshalBuffer: the network message (de)serialiser -- the primitive / string /
// FmtStr / Costume members and the EquipDescList / GoodDescList members.

#include "common_net.h"
#include "common_text.h"  // FmtStr -- complete type for put_FmtStr / get_FmtStr
#include "common_goods.h" // EquipDescList, GoodDescList, GoodDesc
#include "common_ini.h"   // StringAlloc -- get_EquipDescList's hardpoint names

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <list>

// ===========================================================================
// MarshalBuffer
// ===========================================================================

MarshalBuffer::MarshalBuffer()
    : BufferBase(nullptr)
    , BufferSize(0)
    , Cursor(nullptr)
    , Remaining(0)
    , NumWritten(0)
    , bReadMode(1)
{
}

MarshalBuffer& MarshalBuffer::operator=(const MarshalBuffer& Other)
{
    // RVA 0x3b40 (ICF-folded with Personality::RepairUseStruct::operator=): a raw copy, no
    // self-assignment check.
    BufferBase = Other.BufferBase;
    BufferSize = Other.BufferSize;
    Cursor = Other.Cursor;
    Remaining = Other.Remaining;
    NumWritten = Other.NumWritten;
    bReadMode = Other.bReadMode;
    return *this;
}

void MarshalBuffer::clear()
{
    BufferBase = nullptr;
    BufferSize = 0;
    Cursor = nullptr;
    Remaining = 0;
    NumWritten = 0;
    bReadMode = 1;
}

void MarshalBuffer::use_default()
{
    // The static 0x4000-byte buffer at DAT_063fd2c8.
    static unsigned char G_DefaultBuffer[0x4000];
    BufferBase = G_DefaultBuffer;
    BufferSize = 0x4000;
    Cursor = G_DefaultBuffer;
    Remaining = 0x4000;
    NumWritten = 0;
    bReadMode = 0;
}

void MarshalBuffer::use_read_buffer(const unsigned char* Data, int Size)
{
    BufferBase = const_cast<unsigned char*>(Data);
    BufferSize = Size;
    Cursor = const_cast<unsigned char*>(Data);
    Remaining = Size;
    NumWritten = 0;
    bReadMode = 1;
}

void MarshalBuffer::use_write_buffer(unsigned char* Data, int Size)
{
    BufferBase = Data;
    BufferSize = Size;
    Cursor = Data;
    Remaining = Size;
    NumWritten = 0;
    bReadMode = 0;
}

// --- Core put/get_void ---

void MarshalBuffer::put_void(const void* Src, int Count)
{
    if (Count == 0)
        return;

    unsigned char* Dst = Cursor;
    const unsigned char* S = (const unsigned char*)Src;

    // Dwords, then the 0..3 tail bytes, as retail.
    unsigned int Dwords = (unsigned int)Count >> 2;
    for (unsigned int i = 0; i < Dwords; i++)
    {
        memcpy(Dst, S, 4);
        S += 4;
        Dst += 4;
    }

    unsigned int Bytes = (unsigned int)Count & 3;
    for (unsigned int i = 0; i < Bytes; i++)
    {
        *Dst = *S;
        S++;
        Dst++;
    }

    Cursor = Dst;
    Remaining -= Count;
    NumWritten += Count;
}

void MarshalBuffer::get_void(void* Dst, int Count)
{
    if (Count <= 0 || Count > Remaining)
        return;

    unsigned char* D = (unsigned char*)Dst;
    const unsigned char* S = Cursor;

    unsigned int Dwords = (unsigned int)Count >> 2;
    for (unsigned int i = 0; i < Dwords; i++)
    {
        memcpy(D, S, 4);
        S += 4;
        D += 4;
    }

    unsigned int Bytes = (unsigned int)Count & 3;
    for (unsigned int i = 0; i < Bytes; i++)
    {
        *D = *S;
        S++;
        D++;
    }

    Cursor = const_cast<unsigned char*>(S);
    Remaining -= Count;
}

void MarshalBuffer::skip_over(int Count)
{
    Cursor += Count;
    Remaining -= Count;
}

// --- Primitives ---

void MarshalBuffer::put_U8(unsigned char Value)
{
    put_void(&Value, 1);
}

// get_U8 / get_U16 / get_U32 / get_float (06263aa0 / 06263ad0 / 06263af0 / 06263b20) keep their
// result in the stack slot made by PUSH ECX, so when get_void reads nothing they return the
// bytes of `this` that sit there: the high byte, the high word, the whole pointer, the
// pointer's bits as a float. Retail quirk, reproduced on purpose.
unsigned char MarshalBuffer::get_U8()
{
    unsigned char V = (unsigned char)((std::uintptr_t)this >> 24);
    get_void(&V, 1);
    return V;
}

void MarshalBuffer::put_U16(unsigned short Value)
{
    put_void(&Value, 2);
}

unsigned short MarshalBuffer::get_U16()
{
    unsigned short V = (unsigned short)((std::uintptr_t)this >> 16);
    get_void(&V, 2);
    return V;
}

void MarshalBuffer::put_U32(unsigned long Value)
{
    put_void(&Value, 4);
}

unsigned long MarshalBuffer::get_U32()
{
    unsigned long V = (unsigned long)(std::uintptr_t)this;
    get_void(&V, 4);
    return V;
}

void MarshalBuffer::put_int(int Value)
{
    put_void(&Value, 4);
}

int MarshalBuffer::get_int()
{
    int V = 0;
    get_void(&V, 4);
    return V;
}

void MarshalBuffer::put_float(float Value)
{
    put_void(&Value, 4);
}

float MarshalBuffer::get_float()
{
    unsigned long Bits = (unsigned long)(std::uintptr_t)this;
    get_void(&Bits, 4);
    float V;
    memcpy(&V, &Bits, sizeof(V));
    return V;
}

void MarshalBuffer::put_bool(bool Value)
{
    // Retail writes the byte directly (not via put_void), with no bounds check.
    *Cursor = Value ? 1 : 0;
    Cursor++;
    Remaining--;
    NumWritten++;
}

bool MarshalBuffer::get_bool()
{
    // FUN_06312970. An empty buffer compares the high byte of the pushed `this` with 1 -- a
    // retail quirk, reproduced on purpose.
    if (Remaining >= 1)
    {
        unsigned char V = *Cursor;
        Cursor++;
        Remaining--;
        return V == 1;
    }
    return (unsigned char)((std::uintptr_t)this >> 24) == 1;
}

// --- Strings ---

void MarshalBuffer::put_string(const char* Str)
{
    if (Str == nullptr)
    {
        int const Sentinel = -1;
        memcpy(Cursor, &Sentinel, 4);
        Cursor += 4;
        Remaining -= 4;
        NumWritten += 4;
        return;
    }

    unsigned int const Len = (unsigned int)strlen(Str);
    memcpy(Cursor, &Len, 4);
    Cursor += 4;
    Remaining -= 4;
    NumWritten += 4;

    if (Len > 0)
    {
        put_void(Str, (int)Len);
    }
}

char* MarshalBuffer::get_string()
{
    // FUN_06312b70. With fewer than 4 bytes left the length is the PUSH ECX slot, i.e.
    // (unsigned)this -- a retail quirk; a real object address is >= 0x4000, so that returns
    // NULL. malloc is not tested.
    unsigned int Len = (unsigned int)(std::uintptr_t)this;
    if (Remaining >= 4)
    {
        Len = *(unsigned int*)Cursor;
        Cursor += 4;
        Remaining -= 4;
    }

    if (Len == 0xFFFFFFFF || Len >= 0x4000)
        return nullptr;

    char* Result = (char*)malloc(Len + 1);
    get_void(Result, (int)Len);
    Result[Len] = '\0';
    return Result;
}

void MarshalBuffer::get_string_to_buffer(char* Buf, int MaxLen)
{
    // FUN_06312bf0. MaxLen - 1 is compared unsigned with the length.
    unsigned int const Cap = (unsigned int)(MaxLen - 1);

    // With fewer than 4 bytes left the length is read from the Buf argument's own slot, i.e.
    // (unsigned)Buf -- a retail quirk; for a real pointer (>= 0x4000) that ends in Buf[0] = 0.
    unsigned int Len = (unsigned int)(std::uintptr_t)Buf;
    if (Remaining >= 4)
    {
        Len = *(unsigned int*)Cursor;
        Cursor += 4;
        Remaining -= 4;
    }

    if (Len == 0xFFFFFFFF || Len >= 0x4000)
    {
        Buf[0] = '\0';
        return;
    }

    if (Len > Cap)
    {
        // Truncating: only Cap bytes fit in Buf, but retail still consumes the full Len bytes
        // so later reads stay cursor-aligned.
        get_void(Buf, (int)Cap);
        Buf[Cap] = '\0';
        Cursor += (Len - Cap);
        Remaining -= (int)(Len - Cap);
    }
    else
    {
        get_void(Buf, (int)Len);
        Buf[Len] = '\0';
    }
}

void MarshalBuffer::put_wstring(const wchar_t* Str)
{
    if (Str == nullptr)
    {
        int const Sentinel = -1;
        memcpy(Cursor, &Sentinel, 4);
        Cursor += 4;
        Remaining -= 4;
        NumWritten += 4;
        return;
    }

    unsigned int const CharLen = (unsigned int)wcslen(Str);
    memcpy(Cursor, &CharLen, 4);
    Cursor += 4;
    Remaining -= 4;
    NumWritten += 4;

    unsigned int const ByteLen = CharLen * 2;
    if (ByteLen > 0)
    {
        put_void(Str, (int)ByteLen);
    }
}

wchar_t* MarshalBuffer::get_wstring()
{
    // FUN_06312a40, the wide twin of get_string (same `this`-as-length quirk; malloc not
    // tested).
    unsigned int CharLen = (unsigned int)(std::uintptr_t)this;
    if (Remaining >= 4)
    {
        CharLen = *(unsigned int*)Cursor;
        Cursor += 4;
        Remaining -= 4;
    }

    if (CharLen == 0xFFFFFFFF || CharLen >= 0x4000)
        return nullptr;

    wchar_t* Result = (wchar_t*)malloc(CharLen * 2 + 2);
    get_void(Result, (int)(CharLen * 2));
    Result[CharLen] = 0;
    return Result;
}

// --- Image helpers (thin wrappers) ---

void MarshalBuffer::put_image(const unsigned char& V)  { put_void(&V, 1); }
void MarshalBuffer::get_image(unsigned char* V)        { get_void(V, 1); }
void MarshalBuffer::put_image(const unsigned short& V) { put_void(&V, 2); }
void MarshalBuffer::get_image(unsigned short* V)       { get_void(V, 2); }
void MarshalBuffer::put_image(const int& V)            { put_void(&V, 4); }
void MarshalBuffer::get_image(int* V)                  { get_void(V, 4); }
void MarshalBuffer::put_image(const unsigned long& V)  { put_void(&V, 4); }
void MarshalBuffer::get_image(unsigned long* V)        { get_void(V, 4); }
void MarshalBuffer::put_image(const float& V)          { put_void(&V, 4); }
void MarshalBuffer::get_image(float* V)                { get_void(V, 4); }

// --- FL_ID ---

void MarshalBuffer::put_FL_ID(unsigned int Id)
{
    memcpy(Cursor, &Id, 4);
    Cursor += 4;
    Remaining -= 4;
    NumWritten += 4;
}

unsigned int MarshalBuffer::get_FL_ID()
{
    // FUN_06313560. Fewer than 4 bytes left returns the pushed `this` -- a retail quirk,
    // reproduced on purpose.
    if (Remaining >= 4)
    {
        unsigned int Id = *(unsigned int*)Cursor;
        Cursor += 4;
        Remaining -= 4;
        return Id;
    }
    return (unsigned int)(std::uintptr_t)this;
}

// --- Complex type marshalling ---

void MarshalBuffer::put_FmtStr(const FmtStr& Fmt)
{
    // RVA 0xb2ce0: an upper-bound size from the recursive per-node sizer (FUN_06312d80, the
    // same recursion as FmtStr::Val::get_flattened_size), flatten() into a temporary, then
    // exactly what flatten() reports writing is copied out. The allocation is not tested.
    unsigned int TotalSize = 6;
    for (FmtStr::Val const* Node = Fmt.Head; Node != nullptr; Node = Node->Next)
        TotalSize += Node->get_flattened_size();

    unsigned char* TempBuf = new unsigned char[TotalSize];
    int const Written = Fmt.flatten(TempBuf, TotalSize);
    if (Written != 0)
    {
        put_void(TempBuf, Written);
    }

    delete[] TempBuf;
}

FmtStr MarshalBuffer::get_FmtStr()
{
    // FUN_06312dd0: unflatten with a size argument of 0; the cursor moves by its result.
    // Retail copy-constructs the result from the local; returning the local has the same
    // effect.
    FmtStr Result;
    int const Consumed = Result.unflatten(Cursor, 0);
    Remaining -= Consumed;
    Cursor += Consumed;
    return Result;
}

// ---------------------------------------------------------------------------
// MarshalBuffer::put_EquipDescList / get_EquipDescList (0xb2ea0 / 0xb2ff0).
//
// Ghidra names several of get_EquipDescList's setter calls after unrelated classes
// (SetSourcePlayerID, SetOwner, set_cause, set_destroyed): these are /OPT:ICF folds of
// one-line field stores. The real targets: 0x06309b30 ArchId (+0x4), 0x06309b40 Hardpoint
// (+0x8), 0x0633bad0 Count (+0x14), 0x06332f50 Health (+0x10), 0x06309b20 Id (+0x2, u16),
// 0x06309b50 bEquipped (+0xc).
// Wire format: item count (int), then per item: ArchId (u32), Hardpoint (length-prefixed,
// null as -1), Count (int), Health (float), Id (u16 widened to 4 bytes), bEquipped (byte
// 0/1), bMission (byte 0/1). EquipDesc::Cached (+0x00) and Owner (+0x1c) are not on the wire.
// ---------------------------------------------------------------------------

void MarshalBuffer::put_EquipDescList(const EquipDescList& List)
{
    put_int((int)List.Items.size());
    for (std::list<EquipDesc>::const_iterator It = List.Items.begin(); It != List.Items.end(); ++It)
    {
        EquipDesc const& Item = *It;
        put_U32(Item.ArchId);
        put_string(Item.Hardpoint);
        put_int(Item.Count);
        put_float(Item.Health);
        put_U32((unsigned long)Item.Id);
        put_U8(Item.bEquipped ? 1 : 0);
        put_U8(Item.bMission ? 1 : 0);
    }
}

EquipDescList MarshalBuffer::get_EquipDescList()
{
    EquipDescList Result;
    int ItemCount = get_int();
    for (int Index = 0; Index < ItemCount; ++Index)
    {
        EquipDesc Item;
        Item.ArchId = get_U32();

        // The malloc'd text is never freed -- retail leaks it on every item.
        char* HpText = get_string();
        CacheString HpCache;
        HpCache.Value = StringAlloc(HpText, false);
        Item.set_hardpoint(HpCache);

        Item.Count = get_int();
        Item.Health = get_float();
        Item.Id = (unsigned short)get_U32();
        Item.bEquipped = get_U8() == 1;
        Item.bMission = get_U8() == 1;

        Result.Items.push_back(Item);
    }
    return Result;
}

// ---------------------------------------------------------------------------
// MarshalBuffer::put_GoodDescList / get_GoodDescList (0xb3250 / 0xb3330). Wire format: item
// count (int), then per item GoodId (u32), Count (int), Price (float), Field0C (float).
// ---------------------------------------------------------------------------

void MarshalBuffer::put_GoodDescList(const GoodDescList& List)
{
    put_int((int)List.count());
    MetaNode<GoodDesc>* Node = nullptr;
    while (List.traverse(Node, true))
    {
        put_U32(Node->Data->GoodId);
        put_int(Node->Data->Count);
        put_float(Node->Data->Price);
        put_float(Node->Data->Field0C);
    }
}

// Retail allocates each element with ??2GoodDesc and reads straight into it.
GoodDescList MarshalBuffer::get_GoodDescList()
{
    GoodDescList Result;
    int ItemCount = get_int();
    for (int Index = 0; Index < ItemCount; ++Index)
    {
        GoodDesc* Item = new GoodDesc;
        Item->GoodId = get_U32();
        Item->Count = get_int();
        Item->Price = get_float();
        Item->Field0C = get_float();
        Result.append(Item);
    }
    return Result;
}

void MarshalBuffer::put_Costume(const Costume& C)
{
    // FUN_06313590: the four part ids, the accessory count, then that many accessory ids
    // (signed loop, no clamp to 8), with no bounds check.
    for (int Index = 0; Index < 4; ++Index)
        put_FL_ID(C.PartIds[Index]);
    put_FL_ID(C.AccessoryCount);
    for (int Index = 0; Index < (int)C.AccessoryCount; ++Index)
        put_FL_ID(C.AccessoryIds[Index]);
}

Costume MarshalBuffer::get_Costume()
{
    // FUN_06313680, the inverse of put_Costume. The loop bound is re-read each pass, so a count
    // above 8 runs into the count slot itself, as in retail. A short read keeps the previous
    // value. Deviation: retail's short read of the first five dwords stores the hidden return
    // pointer and unwritten accessory slots are stack garbage; the port uses 0 and zeroes the
    // local.
    Costume C;
    memset(&C, 0, sizeof(C));
    unsigned int Scratch = 0;
    for (int Index = 0; Index < 4; ++Index)
    {
        Scratch = 0;
        if (Remaining >= 4)
        {
            Scratch = *(unsigned int*)Cursor;
            Cursor += 4;
            Remaining -= 4;
        }
        C.PartIds[Index] = Scratch;
    }
    Scratch = 0;
    if (Remaining >= 4)
    {
        Scratch = *(unsigned int*)Cursor;
        Cursor += 4;
        Remaining -= 4;
    }
    C.AccessoryCount = Scratch;
    Scratch = 0;
    for (int Index = 0; Index < (int)C.AccessoryCount; ++Index)
    {
        if (Remaining >= 4)
        {
            Scratch = *(unsigned int*)Cursor;
            Cursor += 4;
            Remaining -= 4;
        }
        C.AccessoryIds[Index] = Scratch;
    }
    return C;
}

// --- Status ---

bool MarshalBuffer::is_empty() const
{
    // 0x06263a80
    return Remaining == 0;
}

int MarshalBuffer::get_num_written() const
{
    // .text:06263a70
    return NumWritten;
}

unsigned char const* MarshalBuffer::get_buffer() const
{
    // .text:06261d90 (shared ICF body).
    return BufferBase;
}

int MarshalBuffer::get_buffer_size() const
{
    // .text:062d1ad0 (shared ICF body).
    return BufferSize;
}
