// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// RDL readers and writers: the RDLX tag-markup parser (SAX2 content handler), BinaryRDLReader /
// BinaryRDLWriter and XMLReader / XMLWriter (retail RichDisplayList.cpp).

#include "common_text.h"
#include "Text/common_text_internal.h"
#include "dacom.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <map>
#include <string>
#include <deque>
#include <list>
#include <vector>
#include <windows.h>
#include <objbase.h>   // CoCreateInstance, IUnknown -- for the XMLReader::read_buffer/read_file
#include <oleauto.h>   // BSTR/VARIANT, SysAllocStringByteLen family -- same purpose
#include <comdef.h>    // _bstr_t / _com_issue_error (XMLReader::read_buffer); links comsuppw(d).lib

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")   // IID_IUnknown's definition lives here

// DAT_063ed98c / PTR_u_gray_063ed9ac: the named colors of the TRA tag's "color=" attribute.
struct FRdlxNamedColor
{
    wchar_t const* Name;
    unsigned long Value; // COLORREF (0x00BBGGRR)
};
static const FRdlxNamedColor GRdlxNamedColors[8] = {
    {L"gray", 0x00808080},   {L"blue", 0x00E04848},  {L"green", 0x001DBF3B},
    {L"aqua", 0x00E0C387},   {L"red", 0x001D1DBF},    {L"fuchsia", 0x00C20088},
    {L"yellow", 0x0052EAF5}, {L"white", 0x00FFFFFF},
};

// ============================================================================
// RDLX tag-markup parser. The SAX-handler helpers are Common.dll-internal (not exported), so
// they are file-local here.
// ============================================================================

// The MSXML SAX2 interfaces the RDLX parser talks to, hand-declared from Microsoft's public SAX2
// IDL because sax2.h / msxml2.h are not available in this toolchain.
namespace
{
    // {079AA557-4A18-424A-8EEE-E39F0A8D41B9} CLSID_SAXXMLReader and
    // {A4F96ED0-F829-476E-81C0-CDC7BD2A0802} IID_ISAXXMLReader (DAT_063ed96c / DAT_063ed97c).
    const CLSID CLSID_SAXXMLReader_Rdlx =
        { 0x079aa557, 0x4a18, 0x424a, { 0x8e, 0xee, 0xe3, 0x9f, 0x0a, 0x8d, 0x41, 0xb9 } };
    const IID IID_ISAXXMLReader_Rdlx =
        { 0xa4f96ed0, 0xf829, 0x476e, { 0x81, 0xc0, 0xcd, 0xc7, 0xbd, 0x2a, 0x08, 0x02 } };

    // ISAXAttributes. ParseRdlxTag uses getLength (+0x0C), getLocalName (+0x14) and getValue
    // (+0x34).
    struct ISAXAttributesRdlx : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE getLength(int* pnLength) = 0;
        virtual HRESULT STDMETHODCALLTYPE getURI(int nIndex, const wchar_t** ppwchUri, int* pcchUri) = 0;
        virtual HRESULT STDMETHODCALLTYPE getLocalName(int nIndex, const wchar_t** ppwchLocalName,
                                                       int* pcchLocalName) = 0;
        virtual HRESULT STDMETHODCALLTYPE getQName(int nIndex, const wchar_t** ppwchQName, int* pcchQName) = 0;
        virtual HRESULT STDMETHODCALLTYPE getName(int nIndex, const wchar_t** ppwchUri, int* pcchUri,
                                                  const wchar_t** ppwchLocalName, int* pcchLocalName,
                                                  const wchar_t** ppwchQName, int* pcchQName) = 0;
        virtual HRESULT STDMETHODCALLTYPE getIndexFromName(const wchar_t* pwchUri, int cchUri,
                                                           const wchar_t* pwchLocalName, int cchLocalName,
                                                           int* pnIndex) = 0;
        virtual HRESULT STDMETHODCALLTYPE getIndexFromQName(const wchar_t* pwchQName, int cchQName,
                                                            int* pnIndex) = 0;
        virtual HRESULT STDMETHODCALLTYPE getType(int nIndex, const wchar_t** ppwchType, int* pcchType) = 0;
        virtual HRESULT STDMETHODCALLTYPE getTypeFromName(const wchar_t* pwchUri, int cchUri,
                                                          const wchar_t* pwchLocalName, int cchLocalName,
                                                          const wchar_t** ppwchType, int* pcchType) = 0;
        virtual HRESULT STDMETHODCALLTYPE getTypeFromQName(const wchar_t* pwchQName, int cchQName,
                                                           const wchar_t** ppwchType, int* pcchType) = 0;
        virtual HRESULT STDMETHODCALLTYPE getValue(int nIndex, const wchar_t** ppwchValue, int* pcchValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE getValueFromName(const wchar_t* pwchUri, int cchUri,
                                                           const wchar_t* pwchLocalName, int cchLocalName,
                                                           const wchar_t** ppwchValue, int* pcchValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE getValueFromQName(const wchar_t* pwchQName, int cchQName,
                                                            const wchar_t** ppwchValue, int* pcchValue) = 0;
    };

    // ISAXContentHandler, implemented by the RDLX sink below.
    struct ISAXContentHandlerRdlx : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE putDocumentLocator(IUnknown* pLocator) = 0;
        virtual HRESULT STDMETHODCALLTYPE startDocument() = 0;
        virtual HRESULT STDMETHODCALLTYPE endDocument() = 0;
        virtual HRESULT STDMETHODCALLTYPE startPrefixMapping(const wchar_t* pwchPrefix, int cchPrefix,
                                                              const wchar_t* pwchUri, int cchUri) = 0;
        virtual HRESULT STDMETHODCALLTYPE endPrefixMapping(const wchar_t* pwchPrefix, int cchPrefix) = 0;
        virtual HRESULT STDMETHODCALLTYPE startElement(const wchar_t* pwchNamespaceUri, int cchNamespaceUri,
                                                       const wchar_t* pwchLocalName, int cchLocalName,
                                                       const wchar_t* pwchQName, int cchQName,
                                                       ISAXAttributesRdlx* pAttributes) = 0;
        virtual HRESULT STDMETHODCALLTYPE endElement(const wchar_t* pwchNamespaceUri, int cchNamespaceUri,
                                                     const wchar_t* pwchLocalName, int cchLocalName,
                                                     const wchar_t* pwchQName, int cchQName) = 0;
        virtual HRESULT STDMETHODCALLTYPE characters(const wchar_t* pwchChars, int cchChars) = 0;
        virtual HRESULT STDMETHODCALLTYPE ignorableWhitespace(const wchar_t* pwchChars, int cchChars) = 0;
        virtual HRESULT STDMETHODCALLTYPE processingInstruction(const wchar_t* pwchTarget, int cchTarget,
                                                                 const wchar_t* pwchData, int cchData) = 0;
        virtual HRESULT STDMETHODCALLTYPE skippedEntity(const wchar_t* pwchName, int cchName) = 0;
    };

    // ISAXXMLReader. XMLReader::read_buffer / read_file use putContentHandler (+0x28), parse
    // (+0x4c) and parseURL (+0x50). Uncalled slots take IUnknown* for their handler types.
    struct ISAXXMLReaderRdlx : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE getFeature(const wchar_t* pwchName, VARIANT_BOOL* pvfValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE putFeature(const wchar_t* pwchName, VARIANT_BOOL vfValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE getProperty(const wchar_t* pwchName, VARIANT* pvarValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE putProperty(const wchar_t* pwchName, VARIANT varValue) = 0;
        virtual HRESULT STDMETHODCALLTYPE getEntityResolver(IUnknown** ppResolver) = 0;
        virtual HRESULT STDMETHODCALLTYPE putEntityResolver(IUnknown* pResolver) = 0;
        virtual HRESULT STDMETHODCALLTYPE getContentHandler(ISAXContentHandlerRdlx** ppHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE putContentHandler(ISAXContentHandlerRdlx* pHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE getDTDHandler(IUnknown** ppHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE putDTDHandler(IUnknown* pHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE getErrorHandler(IUnknown** ppHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE putErrorHandler(IUnknown* pHandler) = 0;
        virtual HRESULT STDMETHODCALLTYPE getBaseURL(const wchar_t** ppwchBaseUrl) = 0;
        virtual HRESULT STDMETHODCALLTYPE putBaseURL(const wchar_t* pwchBaseUrl) = 0;
        virtual HRESULT STDMETHODCALLTYPE getSecureBaseURL(const wchar_t** ppwchSecureBaseUrl) = 0;
        virtual HRESULT STDMETHODCALLTYPE putSecureBaseURL(const wchar_t* pwchSecureBaseUrl) = 0;
        virtual HRESULT STDMETHODCALLTYPE parse(VARIANT varInput) = 0;
        virtual HRESULT STDMETHODCALLTYPE parseURL(const wchar_t* pwchUrl) = 0;
    };
}

// The parser state retail's SAX sink carries behind its vtable (layout from XMLReader::
// read_buffer's stack initialisation, .text:0632ac5a).
struct RdlxParseContext
{
    ISAXXMLReaderRdlx* Reader;    // +0x04 -- created straight into this slot by CoCreateInstance
    unsigned char bSkipParse;     // +0x08 -- zeroed and never written again; read_buffer / read_file
                                  //          skip the parse and fail when it is set
    RenderDisplayList* Rdl;       // +0x0C -- target list new nodes are appended to
    bool bTextNodeOpen;           // +0x10 -- set once a TEXT tag has created its TextNode for the
                                  //          current RDL; cleared by the matching </TEXT>
};

// The three content callbacks of the RDLX sink (vftable .rdata:063a4644): slot 8 startElement
// (FUN_0632b020), slot 9 endElement (LAB_0632bdd0), slot 10 characters (LAB_0632be60). Retail
// passes the full SAX argument list; the port passes only the arguments that are read. Every
// other sink slot is a no-op.

// startElement: RDL returns at once; TRA, TEXT, PARA, POS, STYLE, JUST, PUSH and POP each append
// one node to Ctx.Rdl; any other tag name does nothing.
static void __cdecl ParseRdlxTag(RdlxParseContext& Ctx, wchar_t const* TagName,
                                 unsigned int TagNameLen, ISAXAttributesRdlx* Attributes);

// endElement: only a "TEXT" end tag has an effect (clears Ctx.bTextNodeOpen).
static void __cdecl ParseRdlxEndElement(RdlxParseContext& Ctx, wchar_t const* TagName,
                                        unsigned int TagNameLen);

// characters: appends the run to the last node of Ctx.Rdl when a TEXT tag is open and that node
// is a TextNode (RDLNode slot +0x20).
static void __cdecl ParseRdlxCharacters(RdlxParseContext& Ctx, wchar_t const* Text,
                                        unsigned int Len);

// ============================================================================
// ParseRdlxTag (FUN_0632b020). PUSH/POP tags only build an AttribStackNode; the attribute
// push/pop happens at AttribStackNode::Execute() time.
// ============================================================================

namespace CmnText
{
    // Every append site of the RDLX parser and of BinaryRDLReader::read_buffer: the stack node
    // is cloned, FUN_0632c490 links a second Clone() of that clone, then the first clone is
    // deleted.
    void RdlxAppendNode(RenderDisplayList& Rdl, RDLNode const& Node)
    {
        RDLNode* FirstClone = Node.Clone();
        InsertRdlCloneBefore(Rdl, Rdl.NodeList, FirstClone);
        delete FirstClone;
    }
}

namespace
{
    // FUN_0632bd10: single hex digit -> value, anything else -> 0.
    unsigned int RdlxHexNibble(wchar_t Ch)
    {
        if (iswdigit(Ch)) return (unsigned int)Ch - 0x30;
        if (Ch > 0x40 && Ch < 0x47) return (unsigned int)Ch - 0x37; // 'A'-'F'
        if (Ch > 0x60 && Ch < 0x67) return (unsigned int)Ch - 0x57; // 'a'-'f'
        return 0;
    }

    // FUN_0632bd50, also inlined at every numeric RDLX attribute. Retail copies Len+1
    // characters (the one after the value, normally the closing quote, too) into an alloca
    // buffer without a terminator; "0x" is case-sensitive. The port copies the same Len+1
    // characters and terminates them; retail's parse could run on into uninitialised stack
    // (undefined behaviour, not reproduced).
    unsigned long ParseRdlxNumber(wchar_t const* Value, int Len)
    {
        std::wstring Temp(Value, (std::wstring::size_type)Len + 1u);
        if (Len > 2 && Temp[0] == L'0' && Temp[1] == L'x')
        {
            return wcstoul(Temp.c_str(), nullptr, 16);
        }
        return (unsigned long)wcstol(Temp.c_str(), nullptr, 10);
    }

    // Boolean attribute parser for TRA's bold/italic/underline and POS's relV/relH (inlined at
    // each site in retail, e.g. near 0632b1f0).
    bool ParseRdlxBool(wchar_t const* Value, int Len)
    {
        if (wcsncmp(Value, L"true", Len) == 0)
        {
            return true;
        }
        if (wcsncmp(Value, L"false", Len) != 0)
        {
            FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
                  "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0xd5a,
                  "Invalid boolean in RDLX file. Assuming false.\n");
        }
        return false;
    }
}

void __cdecl ParseRdlxTag(RdlxParseContext& Ctx, wchar_t const* TagName, unsigned int TagNameLen,
                          ISAXAttributesRdlx* Attributes)
{
    if (wcsncmp(TagName, L"RDL", TagNameLen) == 0)
    {
        return;
    }

    // Ctx.Rdl is dereferenced at each append without a test.
    RenderDisplayList* Rdl = Ctx.Rdl;

    if (wcsncmp(TagName, L"TRA", TagNameLen) == 0)
    {
        unsigned int Mask = 0;    // which bit-ranges this node applies
        unsigned int DefBits = 0; // which bit-ranges are inherited from context
        unsigned int AttrsIn = 0; // explicit packed value (color in bits 8-31, font index +
                                  // bold/italic/underline in bits 0-7)

        auto ApplyDefaultBit = [&](unsigned int Bit)
        {
            // LAB_0632b543: a value of "default" marks its bit range as applied and inherited,
            // leaving AttrsIn's bits untouched.
            Mask |= Bit;
            DefBits |= Bit;
        };

        int AttrCount = 0;
        Attributes->getLength(&AttrCount);
        for (int i = 0; i < AttrCount; ++i)
        {
            wchar_t const* Name = nullptr; int NameLen = 0;
            wchar_t const* Value = nullptr; int ValueLen = 0;
            Attributes->getLocalName(i, &Name, &NameLen);
            Attributes->getValue(i, &Value, &ValueLen);

            bool bExplicitValue = (wcsncmp(Value, L"default", ValueLen) != 0);

            if (wcsncmp(Name, L"mask", NameLen) == 0)
            {
                Mask = ParseRdlxNumber(Value, ValueLen);
            }
            else if (wcsncmp(Name, L"def", NameLen) == 0)
            {
                DefBits = ParseRdlxNumber(Value, ValueLen);
            }
            else if (wcsncmp(Name, L"data", NameLen) == 0)
            {
                AttrsIn = ParseRdlxNumber(Value, ValueLen);
            }
            else if (wcsncmp(Name, L"color", NameLen) == 0)
            {
                unsigned int ColorValue = 0xffffff;
                bool bApply = true;
                if (Value[0] == L'#')
                {
                    if (ValueLen == 7)
                    {
                        // #RRGGBB. Retail quirk kept: each channel's two hex digits are
                        // SWAPPED (second digit in the high nibble) before packing as a
                        // 0x00BBGGRR COLORREF.
                        unsigned int R2 = RdlxHexNibble(Value[2]);
                        unsigned int R1 = RdlxHexNibble(Value[1]);
                        unsigned int G2 = RdlxHexNibble(Value[4]);
                        unsigned int G1 = RdlxHexNibble(Value[3]);
                        unsigned int B2 = RdlxHexNibble(Value[6]);
                        unsigned int B1 = RdlxHexNibble(Value[5]);
                        unsigned int SwappedR = (R2 * 0x10 + R1) & 0xff;
                        unsigned int SwappedG = (G2 * 0x10 + G1) & 0xff;
                        unsigned int SwappedB = (B2 * 0x10 + B1) & 0xff;
                        ColorValue = SwappedR | (SwappedG << 8) | (SwappedB << 16);
                    }
                    else
                    {
                        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
                              "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0xd67,
                              "Invalid color name in RDLX file. Assuming white.\n");
                        ColorValue = 0xffffff;
                    }
                }
                else if (!bExplicitValue)
                {
                    ApplyDefaultBit(kTextAttrColorMask);
                    bApply = false;
                }
                else if (!iswdigit(Value[0]))
                {
                    bool bFound = false;
                    for (int c = 0; c < 8; ++c)
                    {
                        if (wcsncmp(Value, GRdlxNamedColors[c].Name, ValueLen) == 0)
                        {
                            ColorValue = GRdlxNamedColors[c].Value;
                            bFound = true;
                            break;
                        }
                    }
                    if (!bFound)
                    {
                        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
                              "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0xd94,
                              "Invalid color name in RDLX file. Assuming white.\n");
                        ColorValue = 0xffffff;
                    }
                }
                else
                {
                    ColorValue = ParseRdlxNumber(Value, ValueLen) - 1;
                }

                if (bApply)
                {
                    AttrsIn = (AttrsIn & 0xffu) | (ColorValue << 8);
                    Mask |= kTextAttrColorMask;
                }
            }
            else if (wcsncmp(Name, L"font", NameLen) == 0)
            {
                if (bExplicitValue)
                {
                    // Byte arithmetic (0632b45b): the font index lands in bits 3-7 (1-based,
                    // hence -1) and bits 0-2 keep bold/italic/underline; an index of 0 or above
                    // 32 wraps inside bits 3-7 and never reaches the colour bits.
                    unsigned char const FontByte = (unsigned char)ParseRdlxNumber(Value, ValueLen);
                    unsigned char const OldByte0 = (unsigned char)(AttrsIn & 0xffu);
                    unsigned char const Byte0 =
                        (unsigned char)((unsigned char)(FontByte * 8 - 1) ^ (~OldByte0 & kTextAttrStyleMask));
                    AttrsIn = (AttrsIn & ~0xffu) | Byte0;
                    Mask |= kTextAttrFontMask;
                }
                else
                {
                    ApplyDefaultBit(kTextAttrFontMask);
                }
            }
            else if (wcsncmp(Name, L"bold", NameLen) == 0)
            {
                if (bExplicitValue)
                {
                    bool bSet = ParseRdlxBool(Value, ValueLen);
                    unsigned int Byte0 = AttrsIn & 0xffu;
                    Byte0 = bSet ? (Byte0 | kTextAttrBold) : (Byte0 & ~kTextAttrBold);
                    AttrsIn = (AttrsIn & ~0xffu) | Byte0;
                    Mask |= kTextAttrBold;
                }
                else
                {
                    ApplyDefaultBit(kTextAttrBold);
                }
            }
            else if (wcsncmp(Name, L"italic", NameLen) == 0)
            {
                if (bExplicitValue)
                {
                    bool bSet = ParseRdlxBool(Value, ValueLen);
                    unsigned int Byte0 = AttrsIn & 0xffu;
                    Byte0 = bSet ? (Byte0 | kTextAttrItalic) : (Byte0 & ~kTextAttrItalic);
                    AttrsIn = (AttrsIn & ~0xffu) | Byte0;
                    Mask |= kTextAttrItalic;
                }
                else
                {
                    ApplyDefaultBit(kTextAttrItalic);
                }
            }
            else if (wcsncmp(Name, L"underline", NameLen) == 0)
            {
                if (bExplicitValue)
                {
                    bool bSet = ParseRdlxBool(Value, ValueLen);
                    unsigned int Byte0 = AttrsIn & 0xffu;
                    Byte0 = bSet ? (Byte0 | kTextAttrUnderline) : (Byte0 & ~kTextAttrUnderline);
                    AttrsIn = (AttrsIn & ~0xffu) | Byte0;
                    Mask |= kTextAttrUnderline;
                }
                else
                {
                    ApplyDefaultBit(kTextAttrUnderline);
                }
            }
        }

        TextRenderAttributes RawAttrs;
        RawAttrs.Packed = AttrsIn;
        TRANode Temp(RawAttrs, Mask, DefBits); // ctor masks Packed by Mask internally
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"TEXT", TagNameLen) == 0)
    {
        // An EMPTY TextNode; ParseRdlxCharacters appends the text.
        if (!Ctx.bTextNodeOpen)
        {
            TextNode Temp;
            RdlxAppendNode(*Rdl, Temp);
            Ctx.bTextNodeOpen = true;
        }
        return;
    }

    if (wcsncmp(TagName, L"PARA", TagNameLen) == 0)
    {
        ParagraphNode Temp;
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"POS", TagNameLen) == 0)
    {
        PositionNode Temp;
        float PosH = 0.0f, PosV = 0.0f;
        int AttrCount = 0;
        Attributes->getLength(&AttrCount);
        for (int i = 0; i < AttrCount; ++i)
        {
            wchar_t const* Name = nullptr; int NameLen = 0;
            wchar_t const* Value = nullptr; int ValueLen = 0;
            Attributes->getLocalName(i, &Name, &NameLen);
            Attributes->getValue(i, &Value, &ValueLen);

            // "v"/"h": wcstol straight on the attribute pointer, no copy and no "0x" case.
            if (wcsncmp(Name, L"v", NameLen) == 0)
            {
                PosV = (float)wcstol(Value, nullptr, 10);
            }
            else if (wcsncmp(Name, L"h", NameLen) == 0)
            {
                PosH = (float)wcstol(Value, nullptr, 10);
            }
            else if (wcsncmp(Name, L"relV", NameLen) == 0)
            {
                Temp.set_rel_v(ParseRdlxBool(Value, ValueLen));
            }
            else if (wcsncmp(Name, L"relH", NameLen) == 0)
            {
                Temp.set_rel_h(ParseRdlxBool(Value, ValueLen));
            }
        }
        Temp.set_pos(PosH, PosV);
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"STYLE", TagNameLen) == 0)
    {
        // Retail also compares "name" but discards the result.
        StyleNode Temp;
        unsigned short StyleId = 0;
        int AttrCount = 0;
        Attributes->getLength(&AttrCount);
        for (int i = 0; i < AttrCount; ++i)
        {
            wchar_t const* Name = nullptr; int NameLen = 0;
            wchar_t const* Value = nullptr; int ValueLen = 0;
            Attributes->getLocalName(i, &Name, &NameLen);
            Attributes->getValue(i, &Value, &ValueLen);
            if (wcsncmp(Name, L"id", NameLen) == 0)
            {
                StyleId = (unsigned short)ParseRdlxNumber(Value, ValueLen);
            }
        }
        Temp.set_style_id(StyleId);
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"JUST", TagNameLen) == 0)
    {
        TextJustify Mode = TextJustifyLeft;
        int AttrCount = 0;
        Attributes->getLength(&AttrCount);
        for (int i = 0; i < AttrCount; ++i)
        {
            wchar_t const* Name = nullptr; int NameLen = 0;
            wchar_t const* Value = nullptr; int ValueLen = 0;
            Attributes->getLocalName(i, &Name, &NameLen);
            Attributes->getValue(i, &Value, &ValueLen);
            if (wcsncmp(Name, L"loc", NameLen) == 0)
            {
                wchar_t C = Value[0];
                if (C == L'C' || C == L'c') Mode = TextJustifyCenter;
                else if (C == L'R' || C == L'r') Mode = TextJustifyRight;
                else Mode = TextJustifyLeft;
            }
        }
        JustifyNode Temp(Mode);
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"PUSH", TagNameLen) == 0)
    {
        AttribStackNode Temp(AttribStackNode::Op_Push);
        RdlxAppendNode(*Rdl, Temp);
        return;
    }

    if (wcsncmp(TagName, L"POP", TagNameLen) == 0)
    {
        AttribStackNode Temp(AttribStackNode::Op_Pop);
        RdlxAppendNode(*Rdl, Temp);
        return;
    }
}

// ParseRdlxEndElement (LAB_0632bdd0). "RDL" and "TRA" are compared FIRST, which matters: wcsncmp
// over TagNameLen also matches prefixes, so a length of 0 or a name such as "T" stops at those
// probes and never clears the flag. The POS/PARA/STYLE probes after TEXT have no effect and are
// not reproduced.
void __cdecl ParseRdlxEndElement(RdlxParseContext& Ctx, wchar_t const* TagName, unsigned int TagNameLen)
{
    if (wcsncmp(TagName, L"RDL", TagNameLen) == 0)
        return;
    if (wcsncmp(TagName, L"TRA", TagNameLen) == 0)
        return;
    if (wcsncmp(TagName, L"TEXT", TagNameLen) == 0)
    {
        Ctx.bTextNodeOpen = false;
    }
}

// ParseRdlxCharacters (LAB_0632be60).
void __cdecl ParseRdlxCharacters(RdlxParseContext& Ctx, wchar_t const* Text, unsigned int Len)
{
    // Ctx.Rdl and the tail pointers are used without a test.
    RenderDisplayList* Rdl = Ctx.Rdl;
    if (Rdl->Count == 0 || !Ctx.bTextNodeOpen)
    {
        return;
    }

    RDLNode* LastNode = Rdl->NodeList->Prev->Node;

    TextNode* AsText = LastNode->operator TextNode*();
    if (AsText == nullptr)
    {
        return;
    }

    // Retail inlines the wstring append here; TextNode::append_text does the same.
    AsText->append_text(Text, (int)Len);
}

// ============================================================================
// BinaryRDLReader / BinaryRDLWriter / XMLReader / XMLWriter
// The record format is documented on BinaryRDLReader in common_text.h.
// ============================================================================

namespace
{
    // Every binary RDL record starts with this 8-byte header; Length payload bytes follow.
    struct FRdlRecordHeader
    {
        int Type;               // +0x00  1 TRA, 2 text, 4 position, 5 paragraph, 6 style
        unsigned int Length;    // +0x04  payload bytes after the header
    };
    static_assert(sizeof(FRdlRecordHeader) == 8, "FRdlRecordHeader must be 8 bytes");

    // FRdlRecordHeader::Type of the record kinds the reader and the writer handle.
    enum ERdlRecordType
    {
        kRdlRecordTra = 1,
        kRdlRecordText = 2,
        kRdlRecordPosition = 4,
        kRdlRecordParagraph = 5,
        kRdlRecordStyle = 6,
    };

    // The fixed-size records. Type 2 (text) is a header plus Length bytes of NUL-terminated
    // UTF-16; type 5 (paragraph) is a bare header.
#pragma pack(push, 1)
    struct FRdlTraRecord        // type 1, Length 8
    {
        FRdlRecordHeader Header;
        unsigned long Attrs;    // +0x08  TRANode +0x04
        unsigned int Mask;      // +0x0c  TRANode +0x0c
    };
    struct FRdlPositionRecord   // type 4, Length 9
    {
        FRdlRecordHeader Header;
        unsigned char Flags;    // +0x08  bit 0 relH (+0x04), bit 1 relV (+0x05)
        float PosV;             // +0x09  PositionNode +0x08
        float PosH;             // +0x0d  PositionNode +0x0c
    };
    struct FRdlStyleRecord      // type 6, Length 2
    {
        FRdlRecordHeader Header;
        unsigned short StyleId; // +0x08  StyleNode +0x04
    };
#pragma pack(pop)
    static_assert(sizeof(FRdlTraRecord) == 0x10, "TRA record is 0x10 bytes");
    static_assert(sizeof(FRdlPositionRecord) == 0x11, "position record is 0x11 bytes");
    static_assert(sizeof(FRdlStyleRecord) == 0xa, "style record is 0xa bytes");
}

// 0x06325ee0 / 0x06325ef0: store the vtable, nothing else.
BinaryRDLReader::BinaryRDLReader() {}
BinaryRDLReader::BinaryRDLReader(const BinaryRDLReader&) {}

// 0x0632a560. Type 0, 3 and anything above 6 clear Ok; parsing continues. A record with Length 0
// is skipped whatever its type. Retail never writes TRANode +0x08 or StyleNode +0x06 here (the
// port's constructors zero them; neither is read).
bool BinaryRDLReader::read_buffer(RenderDisplayList& Rdl, const char* Buffer, unsigned int Size)
{
    bool bOk = true;
    if (Size == 0)
    {
        return bOk;
    }

    const unsigned char* Cur = (const unsigned char*)Buffer;
    do
    {
        const FRdlRecordHeader* const Header = (const FRdlRecordHeader*)Cur;
        const unsigned int Length = Header->Length;
        if (Length != 0)
        {
            const unsigned char* Payload = Cur + 8;
            switch (Header->Type)
            {
            case kRdlRecordTra:
            {
                const FRdlTraRecord* const Tra = (const FRdlTraRecord*)Cur;
                TextRenderAttributes Attrs;
                Attrs.Packed = Tra->Attrs;
                TRANode Node;
                Node.set_attributes(Attrs, Tra->Mask);
                RdlxAppendNode(Rdl, Node);
                break;
            }
            case kRdlRecordText:
            {
                TextNode Node;
                Node.set_text((const wchar_t*)Payload, -1);
                RdlxAppendNode(Rdl, Node);
                break;
            }
            case kRdlRecordPosition:
            {
                const FRdlPositionRecord* const Pos = (const FRdlPositionRecord*)Cur;
                PositionNode Node;
                Node.bRelH = (Pos->Flags & 1) != 0;
                Node.bRelV = ((Pos->Flags >> 1) & 1) != 0;
                Node.PosV = Pos->PosV; // a float load / store, not a raw copy
                Node.PosH = Pos->PosH;
                RdlxAppendNode(Rdl, Node);
                break;
            }
            case kRdlRecordParagraph:
            {
                ParagraphNode Node;
                RdlxAppendNode(Rdl, Node);
                break;
            }
            case kRdlRecordStyle:
            {
                StyleNode Node(((const FRdlStyleRecord*)Cur)->StyleId);
                RdlxAppendNode(Rdl, Node);
                break;
            }
            default:
                bOk = false;
                break;
            }
        }
        Cur += Length + 8;
        Size -= Length + 8;
    } while (Size != 0);

    return bOk;
}

// 0x0632c0c0: always false in retail.
bool BinaryRDLReader::read_file(RenderDisplayList&, const char*)
{
    return false;
}

// RVA 0xca8a0. Concatenates the text of every type 2 record into Dest, truncating to MaxSize
// wide characters and always NUL-terminating. Retail quirk: everything except 1..5 -- INCLUDING
// 6, the StyleNode record the writer emits -- clears the success flag (the jump table covers only
// 1..5), so a styled message extracts its full text and still returns false. The third
// parameter is never touched.
bool BinaryRDLReader::extract_text_from_buffer(wchar_t* Dest, unsigned int MaxSize,
                                               unsigned int& /*OutSize*/,
                                               const char* Buffer, unsigned int Size)
{
    wchar_t* OutBuf = Dest;
    const unsigned char* Cur = (const unsigned char*)Buffer;
    unsigned int Written = 0;
    bool Ok = true;

    // Retail's early-out checks the INPUT buffer size (Size), not the output
    // capacity (MaxSize) -- an empty input just yields an empty output string.
    if (Size == 0)
    {
        OutBuf[0] = 0;
        return true;
    }

    while (Size != 0 && Written < MaxSize)
    {
        const FRdlRecordHeader* const Header = (const FRdlRecordHeader*)Cur;
        int Type = Header->Type;
        unsigned int Length = Header->Length;
        if (Length != 0)
        {
            if (Type == kRdlRecordText)
            {
                const wchar_t* Src = (const wchar_t*)(Cur + 8);
                size_t SrcLen = wcslen(Src);
                unsigned int Count = (unsigned int)(SrcLen + 1);
                if (MaxSize - Written < Count)
                {
                    Count = MaxSize - Written;
                }
                wcsncpy(OutBuf + Written, Src, Count);
                Written = Written + Count - 1;
            }
            else if (Type != kRdlRecordTra && Type != 3 && Type != kRdlRecordPosition && Type != kRdlRecordParagraph)
            {
                Ok = false;
            }
        }
        Cur += Length + 8;
        Size -= (Length + 8);
    }

    OutBuf[Written] = 0;
    return Ok;
}

// 0x06325f00 / 0x06325f10: store the vtable, nothing else.
BinaryRDLWriter::BinaryRDLWriter() {}
BinaryRDLWriter::BinaryRDLWriter(const BinaryRDLWriter&) {}

// 0x0632a970. Per node, all SEVEN type queries are made before any test, in this order: TRANode,
// PositionNode, TextNode, RDLRefNode, ParagraphNode, StyleNode, TextPtrNode; the first match in
// test order wins, and NullNode/JustifyNode/ClipNode/ImageNode/AttribStackNode are skipped without
// a record. An RDLRefNode recurses through the virtual write_buffer. On overflow OutSize holds the
// bytes of the records already written. All size arithmetic is 32-bit unsigned.
bool BinaryRDLWriter::write_buffer(const RenderDisplayList& Rdl, char* Dest, unsigned int MaxSize,
                                   unsigned int& OutSize)
{
    FRdlListNode* Cur = Rdl.NodeList->Next;
    unsigned int Written = 0;
    if (Cur == Rdl.NodeList)
    {
        OutSize = Written;
        return true;
    }

    do
    {
        RDLNode* Node = Cur->Node;
        char* Rec = Dest + Written;
        FRdlRecordHeader* const Header = (FRdlRecordHeader*)Rec;

        TRANode* AsTra = Node->operator TRANode*();
        PositionNode* AsPos = Node->operator PositionNode*();
        TextNode* AsText = Node->operator TextNode*();
        RDLRefNode* AsRef = Node->operator RDLRefNode*();
        ParagraphNode* AsPara = Node->operator ParagraphNode*();
        StyleNode* AsStyle = Node->operator StyleNode*();
        TextPtrNode* AsTextPtr = Node->operator TextPtrNode*();

        if (AsTra != nullptr)
        {
            if (Written + sizeof(FRdlTraRecord) > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            FRdlTraRecord* const Tra = (FRdlTraRecord*)Rec;
            Tra->Header.Type = kRdlRecordTra;
            Tra->Header.Length = 8;
            Tra->Attrs = AsTra->Attrs.Packed;
            Tra->Mask = AsTra->Mask;
            Written += sizeof(FRdlTraRecord);
        }
        else if (AsPos != nullptr)
        {
            if (Written + sizeof(FRdlPositionRecord) > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            FRdlPositionRecord* const Pos = (FRdlPositionRecord*)Rec;
            Pos->Header.Type = kRdlRecordPosition;
            Pos->Header.Length = 9;
            Pos->Flags = (AsPos->bRelV ? 2 : 0) | (AsPos->bRelH ? 1 : 0);
            Pos->PosV = AsPos->PosV;
            Pos->PosH = AsPos->PosH;
            Written += sizeof(FRdlPositionRecord);
        }
        else if (AsTextPtr != nullptr)
        {
            const unsigned int RecLen = AsTextPtr->TextLen * 2 + 2;
            if (RecLen + Written + 8 > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            Header->Length = RecLen;
            Header->Type = kRdlRecordText;
            memcpy(Rec + 8, AsTextPtr->TextPtr, RecLen - 2);
            *(wchar_t*)(Rec + 8 + RecLen - 2) = 0;
            Written += RecLen + 8;
        }
        else if (AsText != nullptr)
        {
            const unsigned int RecLen = (unsigned int)AsText->Text.size() * 2 + 2;
            if (RecLen + Written + 8 > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            Header->Type = kRdlRecordText;
            Header->Length = RecLen;
            // The string's terminator is copied too (RecLen includes it).
            const wchar_t* Src = AsText->Text.c_str();
            memcpy(Rec + 8, Src, RecLen);
            Written += RecLen + 8;
        }
        else if (AsPara != nullptr)
        {
            if (Written + 8 > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            Header->Type = kRdlRecordParagraph;
            Header->Length = 0;
            Written += 8;
        }
        else if (AsStyle != nullptr)
        {
            if (Written + sizeof(FRdlStyleRecord) > MaxSize)
            {
                OutSize = Written;
                return false;
            }
            FRdlStyleRecord* const StyleRec = (FRdlStyleRecord*)Rec;
            StyleRec->Header.Length = 2;
            StyleRec->Header.Type = kRdlRecordStyle;
            StyleRec->StyleId = AsStyle->get_style_id();
            Written += sizeof(FRdlStyleRecord);
        }
        else if (AsRef != nullptr)
        {
            unsigned int SubSize = 0;
            if (!write_buffer(AsRef->List, Rec, MaxSize - Written, SubSize))
            {
                OutSize = SubSize + Written;
                return false;
            }
            Written += SubSize;
        }

        Cur = Cur->Next;
    } while (Cur != Rdl.NodeList);

    OutSize = Written;
    return true;
}

// 0x0632c0c0: always false in retail.
bool BinaryRDLWriter::write_file(const RenderDisplayList&, const char*)
{
    return false;
}

// RVA 0xc5ea0
XMLReader::XMLReader() {}
XMLReader::XMLReader(const XMLReader&) {}

// ============================================================================
// XMLReader::read_buffer / read_file (0632ac30 / 0632bf80) drive MSXML2's SAXXMLReader COM
// object (not a DOM document) with a sink built on their own stack. Neither initialises COM:
// the calling thread must already have done so, else CoCreateInstance fails and they return
// false, as in retail.
// ============================================================================

namespace
{
    // Retail's base layer of the sink, RTTI .?AVContentHandler@@, vftable .rdata:063a4684 (15
    // slots): every interface slot is an ICF-folded "return 0" stub (QueryInterface returns S_OK
    // WITHOUT writing *ppv); slot 14 is the scalar deleting destructor LAB_0632af90. No reference
    // count: the sink lives on the stack of read_buffer / read_file.
    class ContentHandler : public ISAXContentHandlerRdlx
    {
    public:
        virtual ~ContentHandler() {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return 0; }
        ULONG STDMETHODCALLTYPE AddRef() override { return 0; }
        ULONG STDMETHODCALLTYPE Release() override { return 0; }
        HRESULT STDMETHODCALLTYPE putDocumentLocator(IUnknown*) override { return 0; }
        HRESULT STDMETHODCALLTYPE startDocument() override { return 0; }
        HRESULT STDMETHODCALLTYPE endDocument() override { return 0; }
        HRESULT STDMETHODCALLTYPE startPrefixMapping(const wchar_t*, int, const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE endPrefixMapping(const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE startElement(const wchar_t*, int, const wchar_t*, int, const wchar_t*, int,
                                               ISAXAttributesRdlx*) override { return 0; }
        HRESULT STDMETHODCALLTYPE endElement(const wchar_t*, int, const wchar_t*, int, const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE characters(const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE ignorableWhitespace(const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE processingInstruction(const wchar_t*, int, const wchar_t*, int) override { return 0; }
        HRESULT STDMETHODCALLTYPE skippedEntity(const wchar_t*, int) override { return 0; }
    };

    // The sink itself, RTTI .?AVRDLXParser@@, vftable .rdata:063a4644: overrides startElement /
    // endElement / characters (slots 8/9/10) and the destructor (slot 14, FUN_0632bf60).
    class RDLXParser : public ContentHandler
    {
    public:
        // +0x04..+0x10 behind the vptr. Public because read_file / read_buffer pass &Ctx.Reader
        // straight to CoCreateInstance, as retail does.
        RdlxParseContext Ctx;

        explicit RDLXParser(RenderDisplayList& Rdl)
        {
            Ctx.Reader = nullptr;
            Ctx.bSkipParse = 0;
            Ctx.Rdl = &Rdl;
            Ctx.bTextNodeOpen = false;
        }

        // FUN_0632afb0 (body of the slot-14 scalar deleting destructor). The sink owns the
        // reference CoCreateInstance writes into Ctx.Reader; the Release calls in read_buffer's
        // epilogues are this destructor inlined.
        virtual ~RDLXParser()
        {
            if (Ctx.Reader != nullptr)
            {
                Ctx.Reader->Release();
                Ctx.Reader = nullptr;
            }
        }

        HRESULT STDMETHODCALLTYPE startElement(const wchar_t*, int, const wchar_t* pwchLocalName,
                                               int cchLocalName, const wchar_t*, int,
                                               ISAXAttributesRdlx* pAttributes) override
        {
            ParseRdlxTag(Ctx, pwchLocalName, (unsigned int)cchLocalName, pAttributes);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE endElement(const wchar_t*, int, const wchar_t* pwchLocalName,
                                             int cchLocalName, const wchar_t*, int) override
        {
            ParseRdlxEndElement(Ctx, pwchLocalName, (unsigned int)cchLocalName);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE characters(const wchar_t* pwchChars, int cchChars) override
        {
            ParseRdlxCharacters(Ctx, pwchChars, (unsigned int)cchChars);
            return S_OK;
        }
    };
}

// 0x0632bf80. The sink is constructed FIRST and the reader is created straight into its +0x04;
// only the sink's destructor releases it.
bool XMLReader::read_file(RenderDisplayList& Rdl, const char* Filename)
{
    RDLXParser Handler(Rdl);
    HRESULT Hr = CoCreateInstance(CLSID_SAXXMLReader_Rdlx, nullptr,
                                  CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                                  IID_ISAXXMLReader_Rdlx, (void**)&Handler.Ctx.Reader);
    // Only a negative HRESULT fails; the reader is then used without a null check.
    if (Hr < 0)
    {
        return false;
    }

    // Anything but exactly S_OK fails.
    if (Handler.Ctx.Reader->putContentHandler(&Handler) != S_OK)
    {
        return false;
    }

    // Always zero in practice (never written after the zero-init).
    if (Handler.Ctx.bSkipParse != 0)
    {
        return false;
    }

    // A 999-char cap. Retail does not terminate a name that fills the cap (UB in retail); the
    // port terminates the last slot to stay deterministic.
    wchar_t WideFilename[1000];
    mbstowcs(WideFilename, Filename, 0x3e7);
    WideFilename[999] = L'\0';

    return Handler.Ctx.Reader->parseURL(WideFilename) == S_OK;
}

// RVA 0xcc0a0: always false in retail.
bool XMLReader::extract_text_from_buffer(wchar_t* /*Dest*/, unsigned int /*MaxSize*/,
                                         unsigned int& /*OutSize*/,
                                         const char* /*Buffer*/, unsigned int /*Size*/)
{
    return false;
}

bool XMLReader::read_buffer(RenderDisplayList& Rdl, const char* Buffer, unsigned int Size)
{
    // .text:0632ac30. Same sink / reader ownership and result checks as read_file.
    RDLXParser Handler(Rdl);
    HRESULT Hr = CoCreateInstance(CLSID_SAXXMLReader_Rdlx, nullptr,
                                  CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                                  IID_ISAXXMLReader_Rdlx, (void**)&Handler.Ctx.Reader);
    if (Hr < 0)
    {
        return false;
    }

    if (Handler.Ctx.Reader->putContentHandler(&Handler) != S_OK)
    {
        return false;
    }

    if (Handler.Ctx.bSkipParse != 0)
    {
        return false;
    }

    // _bstr_t::Data_t(BSTR, false) is FUN_0632ae70.
    _bstr_t Raw(SysAllocStringByteLen(Buffer, Size), false);

    // VC6's inline _variant_t(const _bstr_t&). Retail writes only vt and bstrVal; the reserved
    // words are zeroed here rather than left as stack contents.
    VARIANT Input;
    memset(&Input, 0, sizeof(Input));
    Input.vt = VT_BSTR;
    wchar_t* RawChars = (wchar_t*)Raw;
    Input.bstrVal = SysAllocStringByteLen((const char*)RawChars, SysStringByteLen(RawChars));
    if (Input.bstrVal == nullptr)
    {
        _com_issue_error(E_OUTOFMEMORY);
    }

    HRESULT ParseHr = Handler.Ctx.Reader->parse(Input);

    // VC6's ~_variant_t.
    HRESULT ClearHr = VariantClear(&Input);
    if (ClearHr < 0)
    {
        _com_issue_error(ClearHr);
    }

    // Raw is released next (FUN_0632aed0), then the sink.
    return ParseHr == S_OK;
}

// RVA 0xc5ec0
XMLWriter::XMLWriter() {}
XMLWriter::XMLWriter(const XMLWriter&) {}

// RVA 0xcc0b0: always fails in retail.
bool XMLWriter::write_buffer(const RenderDisplayList&, char*, unsigned int, unsigned int&)
{
    return false;
}

// .text:0632c0c0: always fails (ICF stub shared with BinaryRDLWriter::write_file).
bool XMLWriter::write_file(const RenderDisplayList&, const char*)
{
    return false;
}
