// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

struct IDAComponent;

// Modern owners for a handful of retail Thorn helpers whose original
// implementation was coupled to raw VC6 register/stack conventions.  The
// public API preserves the observed operations while keeping ownership in
// ordinary C++. Many other FUN_ addresses near these are vendored Lua 3.2.2
// internals or compiler-generated EH-vector thunks and have no counterpart here.
class FThornModernAdapters final
{
public:
    static char* FormatDiagnostic(char* Destination, const char* Format, ...);

    // Modern owner for the retail 06f4a910/06f4a930/06f4a950 cleanup
    // thunks: release a non-null interface and always clear the owning slot.
    // The interface pointer is borrowed only for the call; no retail wrapper
    // or VC6 smart-pointer layout crosses this boundary.
    static void ReleaseOwnedComponent(IDAComponent*& Component);

    // Modern source-string owner for retail FUN_06f4ccc0/lua_dostring:
    // measure a NUL-terminated chunk without allowing size_t-to-int wrap.
    static bool MeasureSourceLength(const char* Source, int& Length);

    // Modern value-tag owner for retail FUN_06f4b270/lua_rawgettable.
    static bool IsLuaTableTag(int TypeTag);

    // Modern count owner for retail FUN_06f4d4d0/luaV_pack: negative
    // vararg counts are observable as an empty sequence.
    static int NormalizeVarargCount(int RequestedCount);

    // Modern diagnostic owner for retail FUN_06f4dfc0/call_arith.
    static const char* GetArithmeticDiagnostic();

};
