// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Fonts/menu_fonts_load.h"

#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath
#include "common_text.h"
#include "Core/globals.h"                   // CopyBoundedString (FUN_00417c50)
#include "Common/Fonts/ids_rdl.h"                   // RdlAppendNode

#include <string.h>
#include <windows.h>

namespace
{
    // The style id is a CRC-16/CCITT of the "name" value, seeded 0xffff, byte-wise, with no
    // final xor. Retail keeps it in a 32-bit register and replaces only the low word, so the
    // upper half accumulates bits from the << 12 -- they never reach the low word, and
    // StyleCollection::set_style takes an unsigned short.
    unsigned int HashStyleName(char const* Name)
    {
        unsigned int Crc = 0xffff;
        size_t Length = strlen(Name);
        unsigned char const* Cursor = reinterpret_cast<unsigned char const*>(Name);
        while (Length != 0)
        {
            unsigned int const Swapped = ((Crc & 0xffu) << 8) | ((Crc >> 8) & 0xffu);
            Crc = (Crc & 0xffff0000u) | *Cursor;
            Crc ^= Swapped;
            Crc ^= (Crc & 0xffu) >> 4;
            Crc ^= Crc << 12;
            Crc ^= (Crc & 0xffu) << 5;
            ++Cursor;
            --Length;
        }
        return Crc;
    }

    void AppendJustify(RenderDisplayList& StyleList, TextJustify Mode)
    {
        JustifyNode Node(Mode);
        RdlAppendNode(StyleList, Node.Clone());
    }
}

void LoadRichFontsFromIni(char const* RelPath, IFileSystem* Filesys)
{
    // FUN_0057e310.
    char FullPath[MAX_PATH];
    GetFileSysPath(FullPath, RelPath, Filesys);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
        return;
    if (!Reader.read_header())
        return;

    // The face name as a length-prefixed 0x80-byte string; the length is never read again.
    unsigned int FaceLength = 0;
    char FaceName[0x80];

    // The style id. Written only by a "name" line and never reset per section.
    // Deviation: before the first "name" retail reads whatever the stack slot holds (a [Style]
    // with no "name" registers under the previous section's id, or under garbage if it is the
    // first); the port starts it at 0.
    unsigned int StyleId = 0;

    do
    {
        if (Reader.is_header("TrueType"))
        {
            FaceLength = 0;
            FaceName[0] = '\0';
            if (!Reader.read_value())
                continue;
            do
            {
                if (!Reader.is_value("font"))
                    continue;

                unsigned int const Index = static_cast<unsigned int>(Reader.get_value_int(0));
                char const* const Face = Reader.get_value_string(1);
                if (Face == nullptr)
                {
                    FaceLength = 0;
                    FaceName[0] = '\0';
                }
                else
                {
                    FaceLength = CopyBoundedString(FaceName, 0x80, Face);  // FUN_00417c50
                }

                // The three style bytes are always 0.
                TextRenderContext::FontDesc Desc = {};
                Desc.Name = FaceName;
                Desc.Size = static_cast<unsigned long>(Reader.get_value_int(2));
                Desc.bBold = 0;
                Desc.bItalic = 0;
                Desc.bUnderline = 0;
                if (Index < 0x20)                                // unsigned
                    SetStandardFont(static_cast<int>(Index), Desc);
            } while (Reader.read_value());
            (void)FaceLength;
        }
        else if (Reader.is_header("Style"))
        {
            // Attributes {flags 0, R/G/B 0xff}, the "given" mask 0.
            unsigned int Packed = 0xffffff00u;
            unsigned int Mask = 0;
            RenderDisplayList StyleList;

            if (Reader.read_value())
            {
                do
                {
                    if (Reader.is_value("name"))
                    {
                        StyleId = HashStyleName(Reader.get_value_string());
                    }
                    else if (Reader.is_value("font"))
                    {
                        // The font index goes into bits 3..7 of the flag byte.
                        unsigned char const Font = static_cast<unsigned char>(Reader.get_value_int(0));
                        unsigned char const Flags = static_cast<unsigned char>(Packed);
                        unsigned char const NewFlags =
                            static_cast<unsigned char>((Font << 3) ^ (Flags & kTextAttrStyleMask));
                        Packed = (Packed & kTextAttrColorMask) | NewFlags;
                        Mask |= kTextAttrFontMask;
                    }
                    else if (Reader.is_value("bold"))
                    {
                        unsigned char const Value = static_cast<unsigned char>(Reader.get_value_bool(0));
                        unsigned char const Flags = static_cast<unsigned char>(Packed);
                        unsigned char const NewFlags =
                            static_cast<unsigned char>(Flags ^ ((Value ^ Flags) & kTextAttrBold));
                        Packed = (Packed & kTextAttrColorMask) | NewFlags;
                        Mask |= kTextAttrBold;
                    }
                    else if (Reader.is_value("italic"))
                    {
                        unsigned char const Value = static_cast<unsigned char>(Reader.get_value_bool(0));
                        unsigned char const Flags = static_cast<unsigned char>(Packed);
                        unsigned char const NewFlags =
                            static_cast<unsigned char>(Flags ^ (((Value << 1) ^ Flags) & kTextAttrItalic));
                        Packed = (Packed & kTextAttrColorMask) | NewFlags;
                        Mask |= kTextAttrItalic;
                    }
                    else if (Reader.is_value("underline"))
                    {
                        unsigned char const Value = static_cast<unsigned char>(Reader.get_value_bool(0));
                        unsigned char const Flags = static_cast<unsigned char>(Packed);
                        unsigned char const NewFlags =
                            static_cast<unsigned char>(Flags ^ (((Value << 2) ^ Flags) & kTextAttrUnderline));
                        Packed = (Packed & kTextAttrColorMask) | NewFlags;
                        Mask |= kTextAttrUnderline;
                    }
                    else if (Reader.is_value("color"))
                    {
                        unsigned int const Red = static_cast<unsigned char>(Reader.get_value_int(0));
                        unsigned int const Green = static_cast<unsigned char>(Reader.get_value_int(1));
                        unsigned int const Blue = static_cast<unsigned char>(Reader.get_value_int(2));
                        Packed = (Packed & 0xffu) | (Red << 8) | (Green << 16) | (Blue << 24);
                        Mask |= kTextAttrColorMask;
                    }
                    else if (Reader.is_value("justify"))
                    {
                        char const* const Justify = Reader.get_value_string();
                        if (_stricmp(Justify, "left") == 0)
                            AppendJustify(StyleList, TextJustifyLeft);
                        else if (_stricmp(Justify, "center") == 0)
                            AppendJustify(StyleList, TextJustifyCenter);
                        else if (_stricmp(Justify, "right") == 0)
                            AppendJustify(StyleList, TextJustifyRight);
                    }
                } while (Reader.read_value());
            }

            // The TRANode goes after any justify nodes.
            {
                TextRenderAttributes Attrs;
                Attrs.Packed = Packed;
                TRANode Node(Attrs, Mask, 0);
                RdlAppendNode(StyleList, Node.Clone());
            }

            // The list is copied into the registry.
            StyleCollection::set_style(static_cast<unsigned short>(StyleId), StyleList);
        }
    } while (Reader.read_header());
}
