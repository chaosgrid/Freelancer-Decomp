// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <memory>
#include <vector>

// Opcodes of the compiled program, in the dialect of Henry Spencer's regexp: node = opcode byte
// plus a two-byte big-endian link.
enum ERegexOpcode
{
    kRegexEnd = 0,       // end of program; the match succeeded
    kRegexBol = 1,       // '^'
    kRegexEol = 2,       // '$'
    kRegexAny = 3,       // '.'
    kRegexAnyOf = 4,     // [...]
    kRegexAnyBut = 5,    // [^...]
    kRegexBranch = 6,    // one alternative of '|'
    kRegexBack = 7,      // link that points backwards (loops)
    kRegexExactly = 8,   // literal string
    kRegexNothing = 9,   // empty match
    kRegexStar = 10,     // simple operand repeated zero or more times
    kRegexPlus = 11,     // simple operand repeated one or more times
    kRegexWordStart = 12, // \<
    kRegexWordEnd = 13,   // \>
    kRegexOpen = 20,     // + group number 1..9: start of a capture group
    kRegexClose = 30,    // + group number 1..9: end of a capture group
};

// First byte of every compiled program.
enum { kRegexMagic = 0x9c };

// Properties of a compiled piece (the Flags out-parameters of the compiler).
enum ERegexPieceFlag
{
    kRegexHasWidth = 0x1, // matches at least one character
    kRegexSimple = 0x2,   // a single-character operand, so STAR / PLUS can repeat it
    kRegexStartsProgram = 0x4, // starts with a fixed string or anchor (search hint)
};

// Private Shading regexp program; no retail CRT or container layout crosses a DLL.
class FMaterialRegex
{
public:
    static std::unique_ptr<FMaterialRegex> Compile(const char* Pattern);
    static bool Search(const FMaterialRegex* Expression, const char* Text);

private:
    std::vector<unsigned char> Program;
    unsigned char StartCharacter = 0;
    bool bAnchored = false;
    int MustOffset = -1;
    int MustLength = 0;
};
