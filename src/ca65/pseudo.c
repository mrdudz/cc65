/*****************************************************************************/
/*                                                                           */
/*                                 pseudo.c                                  */
/*                                                                           */
/*              Pseudo instructions for the ca65 macroassembler              */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* (C) 1998-2012, Ullrich von Bassewitz                                      */
/*                Roemerstrasse 52                                           */
/*                D-70794 Filderstadt                                        */
/* EMail:         uz@cc65.org                                                */
/*                                                                           */
/*                                                                           */
/* This software is provided 'as-is', without any expressed or implied       */
/* warranty.  In no event will the authors be held liable for any damages    */
/* arising from the use of this software.                                    */
/*                                                                           */
/* Permission is granted to anyone to use this software for any purpose,     */
/* including commercial applications, and to alter it and redistribute it    */
/* freely, subject to the following restrictions:                            */
/*                                                                           */
/* 1. The origin of this software must not be misrepresented; you must not   */
/*    claim that you wrote the original software. If you use this software   */
/*    in a product, an acknowledgment in the product documentation would be  */
/*    appreciated but is not required.                                       */
/* 2. Altered source versions must be plainly marked as such, and must not   */
/*    be misrepresented as being the original software.                      */
/* 3. This notice may not be removed or altered from any source              */
/*    distribution.                                                          */
/*                                                                           */
/*****************************************************************************/



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* common */
#include "alignment.h"
#include "assertion.h"
#include "bitops.h"
#include "cddefs.h"
#include "coll.h"
#include "filestat.h"
#include "gentype.h"
#include "intstack.h"
#include "scopedefs.h"
#include "symdefs.h"
#include "tgttrans.h"
#include "xmalloc.h"

/* ca65 */
#include "anonname.h"
#include "asserts.h"
#include "condasm.h"
#include "dbginfo.h"
#include "enum.h"
#include "error.h"
#include "expect.h"
#include "expr.h"
#include "feature.h"
#include "filetab.h"
#include "global.h"
#include "incpath.h"
#include "instr.h"
#include "listing.h"
#include "macro.h"
#include "nexttok.h"
#include "objcode.h"
#include "options.h"
#include "pseudo.h"
#include "repeat.h"
#include "segment.h"
#include "sizeof.h"
#include "span.h"
#include "spool.h"
#include "struct.h"
#include "symbol.h"
#include "symtab.h"



/*****************************************************************************/
/*                                   Data                                    */
/*****************************************************************************/



/* Keyword we're about to handle */
static StrBuf Keyword = STATIC_STRBUF_INITIALIZER;

/* CPU stack */
static IntStack CPUStack = STATIC_INTSTACK_INITIALIZER;

/* Segment stack */
#define MAX_PUSHED_SEGMENTS     16
static Collection SegStack = STATIC_COLLECTION_INITIALIZER;



/*****************************************************************************/
/*                               Forwards                                    */
/*****************************************************************************/



static void DoUnexpected (void);
/* Got an unexpected keyword */

static void DoInvalid (void);
/* Handle a token that is invalid here, since it should have been handled on
** a much lower level of the expression hierarchy. Getting this sort of token
** means that the lower level code has bugs.
** This function differs to DoUnexpected in that the latter may be triggered
** by the user by using keywords in the wrong location. DoUnexpected is not
** an error in the assembler itself, while DoInvalid is.
*/



/*****************************************************************************/
/*                              Helper functions                             */
/*****************************************************************************/



static unsigned char OptionalAddrSize (void)
/* If a colon follows, parse an optional address size spec and return it.
** Otherwise return ADDR_SIZE_DEFAULT.
*/
{
    unsigned AddrSize = ADDR_SIZE_DEFAULT;
    if (CurTok.Tok == TOK_COLON) {
        NextTok ();
        AddrSize = ParseAddrSize ();
        if (!ValidAddrSizeForCPU (AddrSize)) {
            /* Print an error and reset to default */
            Error ("Invalid address size specification for current CPU");
            AddrSize = ADDR_SIZE_DEFAULT;
        }
        NextTok ();
    }
    return AddrSize;
}



static void SetBoolOption (unsigned char* Flag)
/* Read a on/off/+/- option and set flag accordingly */
{
    static const char* const Keys[] = {
        "OFF",
        "ON",
    };

    if (CurTok.Tok == TOK_PLUS) {
        *Flag = 1;
        NextTok ();
    } else if (CurTok.Tok == TOK_MINUS) {
        *Flag = 0;
        NextTok ();
    } else if (CurTok.Tok == TOK_IDENT) {
        /* Map the keyword to a number */
        switch (GetSubKey (Keys, sizeof (Keys) / sizeof (Keys [0]))) {
            case 0:     *Flag = 0; NextTok ();                  break;
            case 1:     *Flag = 1; NextTok ();                  break;
            default:
                ErrorExpect ("Expected ON or OFF");
                SkipUntilSep ();
                break;
        }
    } else if (TokIsSep (CurTok.Tok)) {
        /* Without anything assume switch on */
        *Flag = 1;
    } else {
        ErrorExpect ("Expected ON or OFF");
        SkipUntilSep ();
    }
}



static void ExportWithAssign (SymEntry* Sym, unsigned char AddrSize, unsigned Flags)
/* Allow to assign the value of an export in an .export statement */
{
    /* The name and optional address size spec may be followed by an assignment
    ** or equal token.
    */
    if (CurTok.Tok == TOK_ASSIGN || CurTok.Tok == TOK_EQ) {

        /* Assignment means the symbol is a label */
        if (CurTok.Tok == TOK_ASSIGN) {
            Flags |= SF_LABEL;
        }

        /* Skip the assignment token */
        NextTok ();

        /* Define the symbol with the expression following the '=' */
        SymDef (Sym, Expression(), ADDR_SIZE_DEFAULT, Flags);

    }

    /* Now export the symbol */
    SymExport (Sym, AddrSize, Flags);
}



static void ExportImport (void (*Func) (SymEntry*, unsigned char, unsigned),
                          unsigned char DefAddrSize, unsigned Flags)
/* Export or import symbols */
{
    SymEntry* Sym;
    unsigned char AddrSize;

    while (1) {

        /* We need an identifier here */
        if (!ExpectSkip (TOK_IDENT, "Expected an identifier")) {
            return;
        }

        /* Find the symbol table entry, allocate a new one if necessary */
        Sym = SymFind (CurrentScope, &CurTok.SVal, SYM_ALLOC_NEW);

        /* Skip the name */
        NextTok ();

        /* Get an optional address size */
        AddrSize = OptionalAddrSize ();
        if (AddrSize == ADDR_SIZE_DEFAULT) {
            AddrSize = DefAddrSize;
        }

        /* Call the actual import/export function */
        Func (Sym, AddrSize, Flags);

        /* More symbols? */
        if (CurTok.Tok == TOK_COMMA) {
            NextTok ();
        } else {
            break;
        }
    }
}



static long IntArg (long Min, long Max)
/* Read an integer argument and check a range. Accept the token "unlimited"
** and return -1 in this case.
*/
{
    if (CurTok.Tok == TOK_IDENT && SB_CompareStr (&CurTok.SVal, "unlimited") == 0) {
        NextTok ();
        return -1;
    } else {
        long Val = ConstExpression ();
        if (Val < Min || Val > Max) {
            Error ("Range error");
            Val = Min;
        }
        return Val;
    }
}



static void ConDes (const StrBuf* Name, unsigned Type)
/* Parse remaining line for constructor/destructor of the remaining type */
{
    long Prio;


    /* Find the symbol table entry, allocate a new one if necessary */
    SymEntry* Sym = SymFind (CurrentScope, Name, SYM_ALLOC_NEW);

    /* Optional constructor priority */
    if (CurTok.Tok == TOK_COMMA) {
        /* Priority value follows */
        NextTok ();
        Prio = ConstExpression ();
        if (Prio < CD_PRIO_MIN || Prio > CD_PRIO_MAX) {
            /* Value out of range */
            Error ("Given priority is out of range");
            return;
        }
    } else {
        /* Use the default priority value */
        Prio = CD_PRIO_DEF;
    }

    /* Define the symbol */
    SymConDes (Sym, ADDR_SIZE_DEFAULT, Type, (unsigned) Prio);
}



static StrBuf* GenArrayType (StrBuf* Type, unsigned SpanSize,
                             const char* ElementType,
                             unsigned ElementTypeLen)
/* Create an array (or single data) of the given type. SpanSize is the size
** of the span, ElementType is a string that encodes the element data type.
** The function returns Type.
*/
{
    /* Get the size of the element type */
    unsigned ElementSize = GT_GET_SIZE (ElementType[0]);

    /* Get the number of array elements */
    unsigned ElementCount = SpanSize / ElementSize;

    /* The span size must be divideable by the element size */
    CHECK ((SpanSize % ElementSize) == 0);

    /* Encode the array */
    GT_AddArray (Type, ElementCount);
    SB_AppendBuf (Type, ElementType, ElementTypeLen);

    /* Return the pointer to the created array type */
    return Type;
}



/*****************************************************************************/
/*                             Handler functions                             */
/*****************************************************************************/



static void DoA16 (void)
/* Switch the accu to 16 bit mode (assembler only) */
{
    if (GetCPU () != CPU_65816) {
        Error ("Command is only valid in 65816 mode");
    } else {
        /* Immidiate mode has two extension bytes */
        ExtBytes [AM65I_IMM_ACCU] = 2;
    }
}



static void DoA8 (void)
/* Switch the accu to 8 bit mode (assembler only) */
{
    if (GetCPU () != CPU_65816) {
        Error ("Command is only valid in 65816 mode");
    } else {
        /* Immidiate mode has one extension byte */
        ExtBytes [AM65I_IMM_ACCU] = 1;
    }
}



static void DoAddr (void)
/* Define addresses */
{
    /* Element type for the generated array */
    static const char EType[2] = { GT_PTR, GT_VOID };

    /* Record type information */
    Span* S = OpenSpan ();
    StrBuf Type = STATIC_STRBUF_INITIALIZER;

    /* Parse arguments */
    while (1) {
        ExprNode* Expr = Expression ();
        if (GetCPU () == CPU_65816 || ForceRange) {
            /* Do a range check */
            Expr = GenWordExpr (Expr);
        }
        EmitWord (Expr);
        if (CurTok.Tok != TOK_COMMA) {
            break;
        } else {
            NextTok ();
        }
    }

    /* Close the span, then add type information to it */
    S = CloseSpan (S);
    SetSpanType (S, GenArrayType (&Type, GetSpanSize (S), EType, sizeof (EType)));

    /* Free the strings */
    SB_Done (&Type);
}



static void DoAlign (void)
/* Align the PC to some boundary */
{
    long FillVal;
    long Alignment;

    /* Read the alignment value */
    Alignment = ConstExpression ();
    if (Alignment <= 0 || (unsigned long) Alignment > MAX_ALIGNMENT) {
        ErrorSkip ("Alignment is out of range");
        return;
    }

    /* Optional value follows */
    if (CurTok.Tok == TOK_COMMA) {
        NextTok ();
        FillVal = ConstExpression ();
        /* We need a byte value here */
        if (!IsByteRange (FillVal)) {
            ErrorSkip ("Fill value is not in byte range");
            return;
        }
    } else {
        FillVal = -1;
    }

    /* Generate the alignment */
    SegAlign (Alignment, (int) FillVal);
}



static void DoASCIIZ (void)
/* Define text with a zero terminator */
{
    while (1) {
        /* Must have a string constant */
        if (!ExpectSkip (TOK_STRCON, "Expected a string constant")) {
            return;
        }

        /* Translate into target charset and emit */
        TgtTranslateStrBuf (&CurTok.SVal);
        EmitStrBuf (&CurTok.SVal);
        NextTok ();
        if (CurTok.Tok == TOK_COMMA) {
            NextTok ();
        } else {
            break;
        }
    }
    Emit0 (0);
}



static void DoAssert (void)
/* Add an assertion */
{
    static const char* const ActionTab [] = {
        "WARN", "WARNING",
        "ERROR",
        "LDWARN", "LDWARNING",
        "LDERROR",
    };

    AssertAction Action;
    unsigned     Msg;

    /* First we have the expression that has to evaluated */
    ExprNode* Expr = Expression ();

    /* Followed by comma and action */
    if (!ConsumeComma () || !Expect (TOK_IDENT, "Expected an identifier")) {
        SkipUntilSep ();
        return;
    }
    switch (GetSubKey (ActionTab, sizeof (ActionTab) / sizeof (ActionTab[0]))) {

        case 0:
        case 1:
            /* Warning */
            Action = ASSERT_ACT_WARN;
            break;

        case 2:
            /* Error */
            Action = ASSERT_ACT_ERROR;
            break;

        case 3:
        case 4:
            /* Linker warning */
            Action = ASSERT_ACT_LDWARN;
            break;

        case 5:
            /* Linker error */
            Action = ASSERT_ACT_LDERROR;
            break;

        default:
            Error ("Illegal assert action specifier");
            SkipUntilSep ();
            return;

    }
    NextTok ();

    /* We can have an optional message. If no message is present, use
    ** "Assertion failed".
    */
    if (CurTok.Tok == TOK_COMMA) {

        /* Skip the comma */
        NextTok ();

        /* Read the message */
        if (!ExpectSkip (TOK_STRCON, "Expected a string constant")) {
            return;
        }

        /* Translate the message into a string id. We can then skip the input
        ** string.
        */
        Msg = GetStrBufId (&CurTok.SVal);
        NextTok ();

    } else {

        /* Use "Assertion failed" */
        Msg = GetStringId ("Assertion failed");

    }

    /* Remember the assertion */
    AddAssertion (Expr, (AssertAction) Action, Msg);
}



static void DoAutoImport (void)
/* Mark unresolved symbols as imported */
{
    SetBoolOption (&AutoImport);
}


static void DoBankBytes (void)
/* Define bytes, extracting the bank byte from each expression in the list */
{
    while (1) {
        EmitByte (FuncBankByte ());
        if (CurTok.Tok != TOK_COMMA) {
            break;
        } else {
            NextTok ();
        }
    }
}



static void DoBss (void)
/* Switch to the BSS segment */
{
    UseSeg (&BssSegDef);
}



static void DoByteBase (int EnableTranslation)
/* Define bytes or literals */
{
    /* Element type for the generated array */
    static const char EType[1] = { GT_BYTE };

    /* Record type information */
    Span* S = OpenSpan ();
    StrBuf Type = AUTO_STRBUF_INITIALIZER;

    /* Parse arguments */
    while (1) {
        if (CurTok.Tok == TOK_STRCON) {
            /* A string, translate into target charset
               if appropriate */
            if (EnableTranslation) {
                TgtTranslateStrBuf (&CurTok.SVal);
            }
            /* Emit */
            EmitStrBuf (&CurTok.SVal);
            NextTok ();
        } else {
            EmitByte (BoundedExpr (Expression, 1));
        }
        if (CurTok.Tok != TOK_COMMA) {
            break;
        } else {
            NextTok ();
            /* Do smart handling of dangling comma */
            if (CurTok.Tok == TOK_SEP) {
                Error ("Unexpected end of line");
                break;
            }
        }
    }

    /* Close the span, then add type information to it.
    ** Note: empty string operands emit nothing;
    ** so, add a type only if there's a span.
    */
    S = CloseSpan (S);
    if (S != 0) {
        SetSpanType (S, GenArrayType (&Type, GetSpanSize (S), EType, sizeof (EType)));
    }

    /* Free the type string */
    SB_Done (&Type);
}



static void DoByte (void)
/* Define bytes with translation */
{
    DoByteBase (1);
}



static void DoCase (void)
/* Switch the IgnoreCase option */
{
    SetBoolOption (&IgnoreCase);
    IgnoreCase = !IgnoreCase;
}



static void DoCharMap (void)
/* Allow custom character mappings */
{
    long Index;
    long Code;

    /* Read the index as numerical value */
    Index = ConstExpression ();
    if (IsByteRange (Index)) {
        /* Value out of range */
        ErrorSkip ("Index must be in byte range");
        return;
    }

    /* Comma follows */
    if (!ConsumeComma ()) {
        SkipUntilSep ();
        return;
    }

    /* Read the character code */
    Code = ConstExpression ();
    if (!IsByteRange (Code)) {
        /* Value out of range */
        ErrorSkip ("Replacement character code must be in byte range");
        return;
    }

    /* Set the character translation */
    TgtTranslateSet ((unsigned) Index, (unsigned char) Code);
}



static void DoCode (void)
/* Switch to the code segment */
{
    UseSeg (&CodeSegDef);
}



static void DoConDes (void)
/* Export a symbol as constructor/destructor */
{
    static const char* const Keys[] = {
        "CONSTRUCTOR",
        "DESTRUCTOR",
        "INTERRUPTOR",
    };
    StrBuf Name = STATIC_STRBUF_INITIALIZER;
    long Type;

    /* Symbol name follows */
    if (!ExpectSkip (TOK_IDENT, "Expected an identifier")) {
        return;
    }
    SB_Copy (&Name, &CurTok.SVal);
    NextTok ();

    /* Type follows. May be encoded as identifier or numerical */
    if (!ConsumeComma ()) {
        SkipUntilSep ();
        goto ExitPoint;
    }
    if (CurTok.Tok == TOK_IDENT) {

        /* Map the following keyword to a number, then skip it */
        Type = GetSubKey (Keys, sizeof (Keys) / sizeof (Keys [0]));
        NextTok ();

        /* Check if we got a valid keyword */
        if (Type < 0) {
            ErrorExpect ("Expected CONSTRUCTOR, DESTRUCTOR or INTERRUPTOR");
            SkipUntilSep ();
            goto ExitPoint;
        }

    } else {

        /* Read the type as numerical value */
        Type = ConstExpression ();
        if (Type < CD_TYPE_MIN || Type > CD_TYPE_MAX) {
            /* Value out of range */
            ErrorSkip ("Numeric condes type is out of range");
            goto ExitPoint;
        }

    }

    /* Parse the remainder of the line and export the symbol */
    ConDes (&Name, (unsigned) Type);

ExitPoint:
    /* Free string memory */
    SB_Done (&Name);
}



static void DoConstructor (void)
/* Export a symbol as constructor */
{
    StrBuf Name = STATIC_STRBUF_INITIALIZER;

    /* Symbol name follows */
    if (!ExpectSkip (TOK_IDENT, "Expected an identifier")) {
        return;
    }
    SB_Copy (&Name, &CurTok.SVal);
    NextTok ();

    /* Parse the remainder of the line and export the symbol */
    ConDes (&Name, CD_TYPE_CON);

    /* Free string memory */
    SB_Done (&Name);
}



static void DoData (void)
/* Switch to the data segment */
{
    UseSeg (&DataSegDef);
}



static void DoDbg (void)
/* Add debug information from high level code */
{
    static const char* const Keys[] = {
        "FILE",
        "FUNC",
        "LINE",
        "SYM",
    };
    int Key;


    /* We expect a subkey */
    if (!ExpectSkip (TOK_IDENT, "Expected an identifier")) {
        return;
    }

    /* Map the following keyword to a number */
    Key = GetSubKey (Keys, sizeof (Keys) / sizeof (Keys [0]));

    /* Skip the subkey */
    NextTok ();

    /* Check the key and dispatch to a handler */
    switch (Key) {
        case 0:     DbgInfoFile ();             break;
        case 1:     DbgInfoFunc ();             break;
        case 2:     DbgInfoLine ();             break;
        case 3:     DbgInfoSym ();              break;
        default:
            ErrorExpect ("Expected FILE, FUNC, LINE or SYM");
            SkipUntilSep ();
            break;
    }
}



static void DoDByt (void)
/* Output double bytes */
{
    /* Element type for the generated array */
    static const char EType[1] = { GT_DBYTE };

    /* Record type information */
    Span* S = OpenSpan ();
    StrBuf Type = STATIC_STRBUF_INITIALIZER;

    /* Parse arguments */
    while (1) {
        EmitWord (GenSwapExpr (BoundedExpr (Expression, 2)));
        if (CurTok.Tok != TOK_COMMA) {
            break;
        } else {
            NextTok ();
        }
    }

    /* Close the span, then add type information to it */
    S = CloseSpan (S);
    SetSpanType (S, GenArrayType (&Type, GetSpanSize (S), EType, sizeof (EType)));

    /* Free the type string */
    SB_Done (&Type);
}



static void DoDebugInfo (void)
/* Switch debug info on or off */
{
    SetBoolOption (&DbgSyms);
}



static void DoDefine (void)
/* Define a one-line macro */
{
    /* The function is called with the .DEFINE token in place, because we need
    ** to disable .define macro expansions before reading the next token.
    ** Otherwise, the name of the macro might be expanded; therefore,
    ** we never would see it.
    */
    DisableDefineStyleMacros ();
    NextTok ();
    EnableDefineStyleMacros ();

    MacDef (MAC_STYLE_DEFINE);
}



static void DoDelMac (void)
/* Delete a classic macro */
{
    /* We expect an identifier */
    if (ExpectSkip (TOK_IDENT, "Expected an identifier")) {
        MacUndef (&CurTok.SVal, MAC_STYLE_CLASSIC);
        NextTok ();
    }
}



static void DoDestructor (void)
/* Export a symbol as destructor */
{
    StrBuf Name = STATIC_STRBUF_INITIALIZER;

    /* Symbol name follows */
    if (!ExpectSkip (TOK_IDENT, "Expected an identifier")) {
        return;
    }
    SB_Copy (&Name, &CurTok.SVal);
    NextTok ();

    /* Parse the remainder of the line and export the symbol */
    ConDes (&Name, CD_TYPE_DES);

    /* Free string memory */
    SB_Done (&Name);
}



static void DoDWord (void)
/* Define dwords */
{
    while (1) {
        EmitDWord (BoundedExpr (Expression, 4));
        if (CurTok.Tok != TOK_COMMA) {
            break;
        } else {
            NextTok ();
        }
    }
}



static void DoEnd (void)
/* End of assembly */
{
    ForcedEnd = 1;
    NextTok ();
}



static void DoEndProc (void)
/* Leave a lexical level */
{
    if (CurrentScope->Type != SCOPE_SCOPE || CurrentScope->Label == 0) {
        /* No local scope */
        ErrorSkip ("No open .PROC");
    } else {
        SymLeaveLevel ();
    }
}



static void DoEndScope (void)
/* Leave a lexical level */
{
    if (CurrentScope->Type != SCOPE_SCOPE || CurrentScope->Label != 0) {
        /* No local scope */
        ErrorSkip ("No open .SCOPE");
    } else {
        SymLeaveLevel ();
    }
}



static void DoError (void)
/* User error */
{
    if (ExpectSkip (TOK_STRCON, "Expected a string constant")) {
        Error ("User error: %m%p", &CurTok.SVal);
        SkipUntilSep ();
    }
}



static void DoExitMacro (void)
/* Exit a macro expansion */
{
    if (!InMacExpansion ()) {
        /* We aren't expanding a macro currently */
        DoUnexpected ();
    } else {
        MacAbort ();
    }
}



static void DoExport (void)
/* Export a symbol */
{
    ExportImport (ExportWithAssign, ADDR_SIZE_DEFAULT, SF_NONE);
}



static void DoExportZP (void)
/* Export a zeropage symbol */
{
    ExportImport (ExportWithAssign, ADDR_SIZE_ZP, SF_NONE);
}



static void DoFarAddr (void)
/* Define far addresses (24 bit) */
{
    /* Element type for the generated array */
    static const char EType[2] = { GT_FAR_PTR, GT_VOID };

    /* Record type information */
    Span* S = OpenSpan ();
    StrBuf Type = STATIC_STRBUF_INITIALIZER;

    /* Parse arguments */
    while (1) {
        EmitFarAddr (BoundedExpr (Expression, 3));
        if (CurTok.Tok != TOK_COMMA) {
            break;
        } else {
            NextTok ();
        }
    }

    /* Close the span, then add type information to it */
    S = CloseSpan (S);
    SetSpanType (S, GenArrayType (&Type, GetSpanSize (S), EType, sizeof (EType)));

    /* Free the type string */
    SB_Done (&Type);
}



static void DoFatal (void)
/* Fatal user error */
{
    if (ExpectSkip (TOK_STRCON, "Expected a string constant")) {
        Fatal ("User error: %m%p", &CurTok.SVal);
        SkipUntilSep ();
    }
}



static void DoFeature (void)
/* Switch the Feature option */
{
    feature_t Feature;
    unsigned char On;

    /* Allow a list of comma separated feature keywords with optional +/- or ON/OFF */
    while (1) {

        /* We expect an identifier */
        if (!ExpectSkip (TOK_IDENT, "Expected an identifier")) {
            return;
        }

        /* Make the string attribute lower case */
        LocaseSVal ();
        Feature = FindFeature (&CurTok.SVal);
        if (Feature == FEAT_UNKNOWN) {
            /* Not found */
            ErrorSkip ("Invalid feature: `%m%p'", &CurTok.SVal);
            return;
        }

        if (Feature == FEAT_ADDRSIZE) {
            Warning (1, "Deprecated feature `addrsize'");
            Notification ("Pseudo function `.addrsize' is always available");
        }

        NextTok ();

        /* Optional +/- or ON/OFF */
        On = 1;
        if (CurTok.Tok != TOK_COMMA && !TokIsSep (CurTok.Tok)) {
            SetBoolOption (&On);
        }

        /* Apply feature setting. */
        SetFeature (Feature, On);

        /* Allow more than one feature separated by commas. */
        if (CurTok.Tok == TOK_COMMA) {
            NextTok ();
        } else {
            break;
        }
    }
}



static void DoFileOpt (void)
/* Insert a file option */
{
    long OptNum;

    /* The option type may be given as a keyword or as a number. */
    if (CurTok.Tok == TOK_IDENT) {

        /* Option given as keyword */
        static const char* const Keys [] = {
            "AUTHOR", "COMMENT", "COMPILER"
        };

        /* Map the option to a number */
        OptNum = GetSubKey (Keys, sizeof (Keys) / sizeof (Keys [0]));
        if (OptNum < 0) {
            /* Not found */
            ErrorExpect ("Expected a file option keyword");
            SkipUntilSep ();
            return;
        }

        /* Skip the keyword */
        NextTok ();

        /* Must be followed by a comma and a string option */
        if (!ConsumeComma () || !Expect (TOK_STRCON, "Expected a string constant")) {
            SkipUntilSep ();
            return;
        }

        /* Insert the option */
        switch (OptNum) {

            case 0:
                /* Author */
                OptAuthor (&CurTok.SVal);
                break;

            case 1:
                /* Comment */
                OptComment (&CurTok.SVal);
                break;

            case 2:
                /* Compiler */
                OptCompiler (&CurTok.SVal);
                break;

            default:
                Internal ("Invalid OptNum: %ld", OptNum);

        }

        /* Done */
        NextTok ();

    } else {

        /* Option given as number */
        OptNum = ConstExpression ();
        if (!IsByteRange (OptNum)) {
            ErrorSkip ("Option number must be in byte range");
            return;
        }

        /* Must be followed by a comma plus a string constant */
        if (!ConsumeComma () || !Expect (TOK_STRCON, "Expected a string constant")) {
            SkipUntilSep ();
            return;
        }

        /* Insert the option */
        OptStr ((unsigned char) OptNum, &CurTok.SVal);

        /* Done */
        NextTok ();
    }
}



static void DoForceImport (void)
/* Do a forced import on a symbol */
{
    ExportImport (SymImport, ADDR_SIZE_DEFAULT, SF_FORCED);
}



static void DoGlobal (void)
/* Declare a global symbol */
{
    ExportImport (SymGlobal, ADDR_SIZE_DEFAULT, SF_NONE);
}



static void DoGlobalZP (void)
/* Declare a global zeropage symbol */
{
    ExportImport (SymGlobal, ADDR_SIZE_ZP, SF_NONE);
}


static void DoHiBytes (void)
/* Define bytes, extracting the hi byte from each expression in the list */
{
    while (1) {
        EmitByte (FuncHiByte ());
        if (CurTok.Tok != TOK_COMMA) {
            break;
        } else {
            NextTok ();
        }
    }
}



static void DoI16 (void)
/* Switch the index registers to 16 bit mode (assembler only) */
{
    if (GetCPU () != CPU_65816) {
        Error ("Command is only valid in 65816 mode");
    } else {
        /* Immidiate mode has two extension bytes */
        ExtBytes [AM65I_IMM_INDEX] = 2;
    }
}



static void DoI8 (void)
/* Switch the index registers to 16 bit mode (assembler only) */
{
    if (GetCPU () != CPU_65816) {
        Error ("Command is only valid in 65816 mode");
    } else {
        /* Immidiate mode has one extension byte */
        ExtBytes [AM65I_IMM_INDEX] = 1;
    }
}



static void DoImport (void)
/* Import a symbol */
{
    ExportImport (SymImport, ADDR_SIZE_DEFAULT, SF_NONE);
}



static void DoImportZP (void)
/* Import a zero page symbol */
{
    ExportImport (SymImport, ADDR_SIZE_ZP, SF_NONE);
}



static void DoIncBin (void)
/* Include a binary file */
{
    StrBuf Name = STATIC_STRBUF_INITIALIZER;
    struct stat StatBuf;
    long Start = 0L;
    long Count = -1L;
    long Size;
    FILE* F;

    /* Name must follow */
    if (!ExpectSkip (TOK_STRCON, "Expected a string constant")) {
        return;
    }
    SB_Copy (&Name, &CurTok.SVal);
    SB_Terminate (&Name);
    NextTok ();

    /* A starting offset may follow */
    if (CurTok.Tok == TOK_COMMA) {
        NextTok ();
        Start = ConstExpression ();

        /* And a length may follow */
        if (CurTok.Tok == TOK_COMMA) {
            NextTok ();
            Count = ConstExpression ();
        }

    }

    /* Try to open the file */
    F = fopen (SB_GetConstBuf (&Name), "rb");
    if (F == 0) {

        /* Search for the file in the binary include directory */
        char* PathName = SearchFile (BinSearchPath, SB_GetConstBuf (&Name));
        if (PathName == 0 || (F = fopen (PathName, "rb")) == 0) {
            /* Not found or cannot open, print an error and bail out */
            ErrorSkip ("Cannot open include file `%m%p': %s", &Name, strerror (errno));
            xfree (PathName);
            goto ExitPoint;
        }

        /* Remember the new file name */
        SB_CopyStr (&Name, PathName);

        /* Free the allocated memory */
        xfree (PathName);
    }

    /* Get the size of the file */
    fseek (F, 0, SEEK_END);
    Size = ftell (F);

    /* Stat the file and remember the values. There's a race condition here,
    ** since we cannot use fileno() (non-standard identifier in standard
    ** header file), and therefore not fstat. When using stat with the
    ** file name, there's a risk that the file was deleted and recreated
    ** while it was open. Since mtime and size are only used to check
    ** if a file has changed in the debugger, we will ignore this problem
    ** here.
    */
    SB_Terminate (&Name);
    if (FileStat (SB_GetConstBuf (&Name), &StatBuf) != 0) {
        Fatal ("Cannot stat input file `%m%p': %s", &Name, strerror (errno));
    }

    /* Add the file to the input file table */
    AddFile (&Name, FT_BINARY, Size, (unsigned long) StatBuf.st_mtime);

    /* If a count was not given, calculate it now */
    if (Count < 0) {
        Count = Size - Start;
        if (Count < 0) {
            /* Nothing to read - flag this as a range error */
            ErrorSkip ("Start offset is larger than file size");
            goto Done;
        }
    } else {
        /* Count was given, check if it is valid */
        if (Start > Size) {
            ErrorSkip ("Start offset is larger than file size");
            goto Done;
        } else if (Start + Count > Size) {
            ErrorSkip ("Not enough bytes left in file at offset %ld", Start);
            goto Done;
        }
    }

    /* Seek to the start position */
    fseek (F, Start, SEEK_SET);

    /* Read chunks and insert them into the output */
    while (Count > 0) {

        unsigned char Buf [1024];

        /* Calculate the number of bytes to read */
        size_t BytesToRead = (Count > (long)sizeof(Buf))? sizeof(Buf) : (size_t) Count;

        /* Read chunk */
        size_t BytesRead = fread (Buf, 1, BytesToRead, F);
        if (BytesToRead != BytesRead) {
            /* Some sort of error */
            ErrorSkip ("Cannot read from include file `%m%p': %s",
                       &Name, strerror (errno));
            break;
        }

        /* Insert it into the output */
        EmitData (Buf, BytesRead);

        /* Keep the counters current */
        Count -= BytesRead;
    }

Done:
    /* Close the file, ignore errors since it's r/o */
    (void) fclose (F);

ExitPoint:
    /* Free string memory */
    SB_Done (&Name);
}



static void DoInclude (void)
/* Include another file */
{
    /* Name must follow */
    if (ExpectSkip (TOK_STRCON, "Expected a string constant")) {
        SB_Terminate (&CurTok.SVal);
        if (NewInputFile (SB_GetConstBuf (&CurTok.SVal)) == 0) {
            /* Error opening the file, skip remainder of line */
            SkipUntilSep ();
        }
    }
}



static void DoInterruptor (void)
/* Export a symbol as interruptor */
{
    StrBuf Name = STATIC_STRBUF_INITIALIZER;

    /* Symbol name follows */
    if (!ExpectSkip (TOK_IDENT, "Expected an identifier")) {
        return;
    }
    SB_Copy (&Name, &CurTok.SVal);
    NextTok ();

    /* Parse the remainder of the line and export the symbol */
    ConDes (&Name, CD_TYPE_INT);

    /* Free string memory */
    SB_Done (&Name);
}



static void DoInvalid (void)
/* Handle a token that is invalid here, since it should have been handled on
** a much lower level of the expression hierarchy. Getting this sort of token
** means that the lower level code has bugs.
** This function differs to DoUnexpected in that the latter may be triggered
** by the user by using keywords in the wrong location. DoUnexpected is not
** an error in the assembler itself, while DoInvalid is.
*/
{
    Internal ("Unexpected token: %m%p", &Keyword);
}



static void DoLineCont (void)
/* Switch the use of line continuations */
{
    SetBoolOption (&LineCont);
}



static void DoList (void)
/* Enable/disable the listing */
{
    /* Get the setting */
    unsigned char List = 0;
    SetBoolOption (&List);

    /* Manage the counter */
    if (List) {
        EnableListing ();
    } else {
        DisableListing ();
    }
}



static void DoLiteral (void)
/* Define bytes without translation */
{
    DoByteBase (0);
}



static void DoLoBytes (void)
/* Define bytes, extracting the lo byte from each expression in the list */
{
    while (1) {
        EmitByte (FuncLoByte ());
        if (CurTok.Tok != TOK_COMMA) {
            break;
        } else {
            NextTok ();
        }
    }
}


static void DoListBytes (void)
/* Set maximum number of bytes to list for one line */
{
    SetListBytes (IntArg (MIN_LIST_BYTES, MAX_LIST_BYTES));
}



static void DoLocalChar (void)
/* Define the character that starts local labels */
{
    if (ExpectSkip (TOK_CHARCON, "Expected a character constant")) {
        if (CurTok.IVal != '@' && CurTok.IVal != '?') {
            Error ("Invalid start character for locals");
        } else {
            LocalStart = (char) CurTok.IVal;
        }
        NextTok ();
    }
}



static void DoMacPack (void)
/* Insert a macro package */
{
    /* We expect an identifier */
    if (!ExpectSkip (TOK_IDENT, "Expected an identifier")) {
        return;
    }
    SB_AppendStr (&CurTok.SVal, ".mac");
    SB_Terminate (&CurTok.SVal);
    if (NewInputFile (SB_GetConstBuf (&CurTok.SVal)) == 0) {
        /* Error opening the file, skip remainder of line */
        SkipUntilSep ();
    }
}



static void DoMacro (void)
/* Start a macro definition */
{
    MacDef (MAC_STYLE_CLASSIC);
}



static void DoNull (void)
/* Switch to the NULL segment */
{
    UseSeg (&NullSegDef);
}



static void DoOrg (void)
/* Start absolute code */
{
    long PC = ConstExpression ();
    if (PC < 0 || PC > 0xFFFFFF) {
        Error ("Range error");
        return;
    }
    EnterAbsoluteMode (PC);
}



static void DoOut (void)
/* Output a string */
{
    if (ExpectSkip (TOK_STRCON, "Expected a string constant")) {
        /* Output the string and be sure to flush the output to keep it in
         * sync with any error messages if the output is redirected to a file.
         */
        printf ("%.*s\n",
                (int) SB_GetLen (&CurTok.SVal),
                SB_GetConstBuf (&CurTok.SVal));
        fflush (stdout);
        NextTok ();
    }
}



static void DoP02 (void)
/* Switch to 6502 CPU */
{
    SetCPU (CPU_6502);
}



static void DoP02X (void)
/* Switch to 6502X CPU */
{
    SetCPU (CPU_6502X);
}



static void DoPC02 (void)
/* Switch to 65C02 CPU */
{
    SetCPU (CPU_65C02);
}



static void DoPWC02 (void)
/* Switch to W65C02 CPU */
{
    SetCPU (CPU_W65C02);
}



static void DoPCE02 (void)
/* Switch to 65CE02 CPU */
{
    SetCPU (CPU_65CE02);
}



static void DoP4510 (void)
/* Switch to 4510 CPU */
{
    SetCPU (CPU_4510);
}



static void DoP45GS02 (void)
/* Switch to 45GS02 CPU */
{
    SetCPU (CPU_45GS02);
}



static void DoP6280 (void)
/* Switch to HuC6280 CPU */
{
    SetCPU (CPU_HUC6280);
}



static void DoP816 (void)
/* Switch to 65816 CPU */
{
    SetCPU (CPU_65816);
}



static void DoPDTV (void)
/* Switch to C64DTV CPU */
{
    SetCPU (CPU_6502DTV);
}



static void DoPM740 (void)
/* Switch to M740 CPU */
{
    SetCPU (CPU_M740);
}



static void DoPageLength (void)
/* Set the page length for the listing */
{
    PageLength = IntArg (MIN_PAGE_LEN, MAX_PAGE_LEN);
}



static void DoPopCharmap (void)
/* Restore a charmap */
{
    if (TgtTranslateStackIsEmpty ()) {
        ErrorSkip ("Charmap stack is empty");
        return;
    }

    TgtTranslatePop ();
}



static void DoPopCPU (void)
/* Pop an old CPU setting from the CPU stack */
{
    /* Must have a CPU on the stack */
    if (IS_IsEmpty (&CPUStack)) {
        ErrorSkip ("CPU stack is empty");
        return;
    }

    /* Set the CPU to the value popped from stack */
    SetCPU (IS_Pop (&CPUStack));
}



static void DoPopSeg (void)
/* Pop an old segment from the segment stack */
{
    SegDef* Def;

    /* Must have a segment on the stack */
    if (CollCount (&SegStack) == 0) {
        ErrorSkip ("Segment stack is empty");
        return;
    }

    /* Pop the last element */
    Def = CollPop (&SegStack);

    /* Restore this segment */
    UseSeg (Def);

    /* Delete the segment definition */
    FreeSegDef (Def);
}



static void DoProc (void)
/* Start a new lexical scope */
{
    StrBuf Name = STATIC_STRBUF_INITIALIZER;
    unsigned char AddrSize;
    SymEntry* Sym = 0;


    if (CurTok.Tok == TOK_IDENT) {

        /* The new scope has a name. Remember it. */
        SB_Copy (&Name, &CurTok.SVal);

        /* Search for the symbol, generate a new one if needed */
        Sym = SymFind (CurrentScope, &Name, SYM_ALLOC_NEW);

        /* Skip the scope name */
        NextTok ();

        /* Read an optional address size specifier */
        AddrSize = OptionalAddrSize ();

        /* Mark the symbol as defined */
        SymDef (Sym, GenCurrentPC (), AddrSize, SF_LABEL);

    } else {

        /* A .PROC statement without a name */
        Warning (1, "Unnamed .PROCs are deprecated, please use .SCOPE");
        AnonName (&Name, "PROC");
        AddrSize = ADDR_SIZE_DEFAULT;

    }

    /* Enter a new scope */
    SymEnterLevel (&Name, SCOPE_SCOPE, AddrSize, Sym);

    /* Free memory for Name */
    SB_Done (&Name);
}



static void DoPSC02 (void)
/* Switch to 65SC02 CPU */
{
    SetCPU (CPU_65SC02);
}



static void DoPSweet16 (void)
/* Switch to Sweet16 CPU */
{
    SetCPU (CPU_SWEET16);
}



static void DoPushCharmap (void)
/* Save the current charmap */
{
    if (!TgtTranslatePush ()) {
        ErrorSkip ("Charmap stack overflow");
    }
}



static void DoPushCPU (void)
/* Push the current CPU setting onto the CPU stack */
{
    /* Can only push a limited size of segments */
    if (IS_IsFull (&CPUStack)) {
        ErrorSkip ("CPU stack overflow");
        return;
    }

    /* Get the current segment and push it */
    IS_Push (&CPUStack, GetCPU ());
}



static void DoPushSeg (void)
/* Push the current segment onto the segment stack */
{
    /* Can only push a limited size of segments */
    if (CollCount (&SegStack) >= MAX_PUSHED_SEGMENTS) {
        ErrorSkip ("Segment stack overflow");
        return;
    }

    /* Get the current segment and push it */
    CollAppend (&SegStack, DupSegDef (GetCurrentSegDef ()));
}



static void DoReferTo (void)
/* Mark given symbol as referenced */
{
    SymEntry* Sym = ParseAnySymName (SYM_ALLOC_NEW);
    if (Sym) {
        SymRef (Sym);
    }
}



static void DoReloc (void)
/* Enter relocatable mode */
{
    EnterRelocMode ();
}



static void DoRepeat (void)
/* Repeat some instruction block */
{
    ParseRepeat ();
}



static void DoRes (void)
/* Reserve some number of storage bytes */
{
    long Count;
    long Val;

    Count = ConstExpression ();
    if (Count > 0xFFFF || Count < 0) {
        ErrorSkip ("Invalid number of bytes specified");
        return;
    }
    if (CurTok.Tok == TOK_COMMA) {
        NextTok ();
        Val = ConstExpression ();
        /* We need a byte value here */
        if (!IsByteRange (Val)) {
            ErrorSkip ("Fill value is not in byte range");
            return;
        }

        /* Emit constant values */
        while (Count--) {
            Emit0 ((unsigned char) Val);
        }

    } else {
        /* Emit fill fragments */
        EmitFill (Count);
    }
}



static void DoROData (void)
/* Switch to the r/o data segment */
{
    UseSeg (&RODataSegDef);
}



static void DoScope (void)
/* Start a local scope */
{
    StrBuf Name = STATIC_STRBUF_INITIALIZER;
    unsigned char AddrSize;


    if (CurTok.Tok == TOK_IDENT) {

        /* The new scope has a name. Remember and skip it. */
        SB_Copy (&Name, &CurTok.SVal);
        NextTok ();

    } else {

        /* An unnamed scope */
        AnonName (&Name, "SCOPE");

    }

    /* Read an optional address size specifier */
    AddrSize = OptionalAddrSize ();

    /* Enter the new scope */
    SymEnterLevel (&Name, SCOPE_SCOPE, AddrSize, 0);

    /* Free memory for Name */
    SB_Done (&Name);
}



static void DoSegment (void)
/* Switch to another segment */
{
    if (ExpectSkip (TOK_STRCON, "Expected a string constant")) {

        SegDef Def;
        StrBuf Name = AUTO_STRBUF_INITIALIZER;

        /* Save the name of the segment and skip it */
        SB_Copy (&Name, &CurTok.SVal);
        NextTok ();

        /* Use the name for the segment definition */
        SB_Terminate (&Name);
        Def.Name = SB_GetBuf (&Name);

        /* Check for an optional address size modifier */
        Def.AddrSize = OptionalAddrSize ();

        /* Set the segment */
        UseSeg (&Def);

        /* Free memory for Name */
        SB_Done (&Name);
    }
}



static void DoSetCPU (void)
/* Switch the CPU instruction set */
{
    /* We expect an identifier */
    if (ExpectSkip (TOK_STRCON, "Expected a string constant")) {
        cpu_t CPU;

        /* Try to find the CPU */
        SB_Terminate (&CurTok.SVal);
        CPU = FindCPU (SB_GetConstBuf (&CurTok.SVal));

        /* Switch to the new CPU */
        SetCPU (CPU);

        /* Skip the identifier. If the CPU switch was successful, the scanner
         * will treat the input now correctly for the new CPU.
         */
        NextTok ();
    }
}



static void DoSmart (void)
/* Smart mode on/off */
{
    SetBoolOption (&SmartMode);
}



static void DoTag (void)
/* Allocate space for a struct */
{
    SymEntry* SizeSym;
    long Size;

    /* Read the struct name */
    SymTable* Struct = ParseScopedSymTable ();

    /* Check the supposed struct */
    if (Struct == 0) {
        ErrorSkip ("Unknown struct");
        return;
    }
    if (GetSymTabType (Struct) != SCOPE_STRUCT) {
        ErrorSkip ("Not a struct");
        return;
    }

    /* Get the symbol that defines the size of the struct */
    SizeSym = GetSizeOfScope (Struct);

    /* Check if it does exist and if its value is known */
    if (SizeSym == 0 || !SymIsConst (SizeSym, &Size)) {
        ErrorSkip ("Size of struct/union is unknown");
        return;
    }

    /* Optional multiplicator may follow */
    if (CurTok.Tok == TOK_COMMA) {
        long Multiplicator;
        NextTok ();
        Multiplicator = ConstExpression ();
        /* Multiplicator must make sense */
        if (Multiplicator <= 0) {
            ErrorSkip ("Range error");
            return;
        }
        Size *= Multiplicator;
    }

    /* Emit fill fragments */
    EmitFill (Size);
}



static void DoUnDef (void)
/* Undefine a define-style macro */
{
    /* The function is called with the .UNDEF token in place, because we need
    ** to disable .define macro expansions before reading the next token.
    ** Otherwise, the name of the macro would be expanded; therefore,
    ** we never would see it.
    */
    DisableDefineStyleMacros ();
    NextTok ();
    EnableDefineStyleMacros ();

    /* We expect an identifier */
    if (ExpectSkip (TOK_IDENT, "Expected an identifier")) {
        MacUndef (&CurTok.SVal, MAC_STYLE_DEFINE);
        NextTok ();
    }
}



static void DoUnexpected (void)
/* Got an unexpected keyword */
{
    Error ("Unexpected `%m%p'", &Keyword);
    SkipUntilSep ();
}



static void DoWarning (void)
/* User warning */
{
    if (ExpectSkip (TOK_STRCON, "Expected a string constant")) {
        Warning (0, "User warning: %m%p", &CurTok.SVal);
        SkipUntilSep ();
    }
}



static void DoWord (void)
/* Define words */
{
    /* Element type for the generated array */
    static const char EType[1] = { GT_WORD };

    /* Record type information */
    Span* S = OpenSpan ();
    StrBuf Type = STATIC_STRBUF_INITIALIZER;

    /* Parse arguments */
    while (1) {
        EmitWord (BoundedExpr (Expression, 2));
        if (CurTok.Tok != TOK_COMMA) {
            break;
        } else {
            NextTok ();
        }
    }

    /* Close the span, then add type information to it */
    S = CloseSpan (S);
    SetSpanType (S, GenArrayType (&Type, GetSpanSize (S), EType, sizeof (EType)));

    /* Free the type string */
    SB_Done (&Type);
}



static void DoZeropage (void)
/* Switch to the zeropage segment */
{
    UseSeg (&ZeropageSegDef);
}



/*****************************************************************************/
/*                                Table data                                 */
/*****************************************************************************/



/* Control commands flags */
enum {
    ccNone      = 0x0000,               /* No special flags */
    ccKeepToken = 0x0001                /* Do not skip the control token */
};

/* Control command table */
typedef struct CtrlDesc CtrlDesc;
struct CtrlDesc {
    unsigned    Flags;                  /* Flags for this directive */
    void        (*Handler) (void);      /* Command handler */
};

/* NOTE: .AND, .BITAND, .BITNOT, .BITOR, .BITXOR, .MOD, .NOT, .OR, .SHL, .SHR
** and .XOR do NOT go into this table.
*/
#define PSEUDO_COUNT    (sizeof (CtrlCmdTab) / sizeof (CtrlCmdTab [0]))
static CtrlDesc CtrlCmdTab [] = {
    { ccNone,           DoA16           },      /* .A16 */
    { ccNone,           DoA8            },      /* .A8 */
    { ccNone,           DoAddr          },      /* .ADDR */
    { ccNone,           DoUnexpected    },      /* .ADDRSIZE */
    { ccNone,           DoAlign         },      /* .ALIGN */
    { ccNone,           DoASCIIZ        },      /* .ASCIIZ */
    { ccNone,           DoUnexpected    },      /* .ASIZE */
    { ccNone,           DoAssert        },      /* .ASSERT */
    { ccNone,           DoAutoImport    },      /* .AUTOIMPORT */
    { ccNone,           DoUnexpected    },      /* .BANK */
    { ccNone,           DoUnexpected    },      /* .BANKBYTE */
    { ccNone,           DoBankBytes     },      /* .BANKBYTES */
    { ccNone,           DoUnexpected    },      /* .BLANK */
    { ccNone,           DoBss           },      /* .BSS */
    { ccNone,           DoByte          },      /* .BYT, .BYTE */
    { ccNone,           DoUnexpected    },      /* .CAP */
    { ccNone,           DoCase          },      /* .CASE */
    { ccNone,           DoCharMap       },      /* .CHARMAP */
    { ccNone,           DoCode          },      /* .CODE */
    { ccNone,           DoUnexpected,   },      /* .CONCAT */
    { ccNone,           DoConDes        },      /* .CONDES */
    { ccNone,           DoUnexpected    },      /* .CONST */
    { ccNone,           DoConstructor   },      /* .CONSTRUCTOR */
    { ccNone,           DoUnexpected    },      /* .CPU */
    { ccNone,           DoData          },      /* .DATA */
    { ccNone,           DoDbg,          },      /* .DBG */
    { ccNone,           DoDByt          },      /* .DBYT */
    { ccNone,           DoDebugInfo     },      /* .DEBUGINFO */
    { ccKeepToken,      DoDefine        },      /* .DEF, .DEFINE */
    { ccNone,           DoUnexpected    },      /* .DEFINED */
    { ccNone,           DoUnexpected    },      /* .DEFINEDMACRO */
    { ccNone,           DoDelMac        },      /* .DELMAC, .DELMACRO */
    { ccNone,           DoDestructor    },      /* .DESTRUCTOR */
    { ccNone,           DoDWord         },      /* .DWORD */
    { ccKeepToken,      DoConditionals  },      /* .ELSE */
    { ccKeepToken,      DoConditionals  },      /* .ELSEIF */
    { ccKeepToken,      DoEnd           },      /* .END */
    { ccNone,           DoUnexpected    },      /* .ENDENUM */
    { ccKeepToken,      DoConditionals  },      /* .ENDIF */
    { ccNone,           DoUnexpected    },      /* .ENDMAC, .ENDMACRO */
    { ccNone,           DoEndProc       },      /* .ENDPROC */
    { ccNone,           DoUnexpected    },      /* .ENDREP, .ENDREPEAT */
    { ccNone,           DoEndScope      },      /* .ENDSCOPE */
    { ccNone,           DoUnexpected    },      /* .ENDSTRUCT */
    { ccNone,           DoUnexpected    },      /* .ENDUNION */
    { ccNone,           DoEnum          },      /* .ENUM */
    { ccNone,           DoError         },      /* .ERROR */
    { ccNone,           DoExitMacro     },      /* .EXITMAC, .EXITMACRO */
    { ccNone,           DoExport        },      /* .EXPORT */
    { ccNone,           DoExportZP      },      /* .EXPORTZP */
    { ccNone,           DoFarAddr       },      /* .FARADDR */
    { ccNone,           DoFatal         },      /* .FATAL */
    { ccNone,           DoFeature       },      /* .FEATURE */
    { ccNone,           DoFileOpt       },      /* .FOPT, .FILEOPT */
    { ccNone,           DoForceImport   },      /* .FORCEIMPORT */
    { ccNone,           DoUnexpected    },      /* .FORCEWORD */
    { ccNone,           DoGlobal        },      /* .GLOBAL */
    { ccNone,           DoGlobalZP      },      /* .GLOBALZP */
    { ccNone,           DoUnexpected    },      /* .HIBYTE */
    { ccNone,           DoHiBytes       },      /* .HIBYTES */
    { ccNone,           DoUnexpected    },      /* .HIWORD */
    { ccNone,           DoI16           },      /* .I16 */
    { ccNone,           DoI8            },      /* .I8 */
    { ccNone,           DoUnexpected    },      /* .IDENT */
    { ccKeepToken,      DoConditionals  },      /* .IF */
    { ccKeepToken,      DoConditionals  },      /* .IFBLANK */
    { ccKeepToken,      DoConditionals  },      /* .IFCONST */
    { ccKeepToken,      DoConditionals  },      /* .IFDEF */
    { ccKeepToken,      DoConditionals  },      /* .IFNBLANK */
    { ccKeepToken,      DoConditionals  },      /* .IFNCONST */
    { ccKeepToken,      DoConditionals  },      /* .IFNDEF */
    { ccKeepToken,      DoConditionals  },      /* .IFNREF */
    { ccKeepToken,      DoConditionals  },      /* .IFP02 */
    { ccKeepToken,      DoConditionals  },      /* .IFP02X */
    { ccKeepToken,      DoConditionals  },      /* .IFP4510 */
    { ccKeepToken,      DoConditionals  },      /* .IFP45GS02 */
    { ccKeepToken,      DoConditionals  },      /* .IFP6280 */
    { ccKeepToken,      DoConditionals  },      /* .IFP816 */
    { ccKeepToken,      DoConditionals  },      /* .IFPC02 */
    { ccKeepToken,      DoConditionals  },      /* .IFPCE02 */
    { ccKeepToken,      DoConditionals  },      /* .IFPDTV */
    { ccKeepToken,      DoConditionals  },      /* .IFPM740 */
    { ccKeepToken,      DoConditionals  },      /* .IFPSC02 */
    { ccKeepToken,      DoConditionals  },      /* .IFPSWEET16 */
    { ccKeepToken,      DoConditionals  },      /* .IFPWC02 */
    { ccKeepToken,      DoConditionals  },      /* .IFREF */
    { ccNone,           DoImport        },      /* .IMPORT */
    { ccNone,           DoImportZP      },      /* .IMPORTZP */
    { ccNone,           DoIncBin        },      /* .INCBIN */
    { ccNone,           DoInclude       },      /* .INCLUDE */
    { ccNone,           DoInterruptor   },      /* .INTERRUPTPOR */
    { ccNone,           DoUnexpected    },      /* .ISIZE */
    { ccNone,           DoUnexpected    },      /* .ISMNEMONIC */
    { ccNone,           DoInvalid       },      /* .LEFT */
    { ccNone,           DoLineCont      },      /* .LINECONT */
    { ccNone,           DoList          },      /* .LIST */
    { ccNone,           DoListBytes     },      /* .LISTBYTES */
    { ccNone,           DoLiteral       },      /* .LITERAL */
    { ccNone,           DoUnexpected    },      /* .LOBYTE */
    { ccNone,           DoLoBytes       },      /* .LOBYTES */
    { ccNone,           DoUnexpected    },      /* .LOCAL */
    { ccNone,           DoLocalChar     },      /* .LOCALCHAR */
    { ccNone,           DoUnexpected    },      /* .LOWORD */
    { ccNone,           DoMacPack       },      /* .MACPACK */
    { ccNone,           DoMacro         },      /* .MAC, .MACRO */
    { ccNone,           DoUnexpected    },      /* .MATCH */
    { ccNone,           DoUnexpected    },      /* .MAX */
    { ccNone,           DoInvalid       },      /* .MID */
    { ccNone,           DoUnexpected    },      /* .MIN */
    { ccNone,           DoNull          },      /* .NULL */
    { ccNone,           DoOrg           },      /* .ORG */
    { ccNone,           DoOut           },      /* .OUT */
    { ccNone,           DoP02           },      /* .P02 */
    { ccNone,           DoP02X          },      /* .P02X */
    { ccNone,           DoP4510         },      /* .P4510 */
    { ccNone,           DoP45GS02       },      /* .P45GS02 */
    { ccNone,           DoP6280         },      /* .P6280 */
    { ccNone,           DoP816          },      /* .P816 */
    { ccNone,           DoPageLength    },      /* .PAGELEN, .PAGELENGTH */
    { ccNone,           DoUnexpected    },      /* .PARAMCOUNT */
    { ccNone,           DoPC02          },      /* .PC02 */
    { ccNone,           DoPCE02         },      /* .PCE02 */
    { ccNone,           DoPDTV          },      /* .PDTV */
    { ccNone,           DoPM740         },      /* .PM740 */
    { ccNone,           DoPopCharmap    },      /* .POPCHARMAP */
    { ccNone,           DoPopCPU        },      /* .POPCPU */
    { ccNone,           DoPopSeg        },      /* .POPSEG */
    { ccNone,           DoProc          },      /* .PROC */
    { ccNone,           DoPSC02         },      /* .PSC02 */
    { ccNone,           DoPSweet16      },      /* .PSWEET16 */
    { ccNone,           DoPushCharmap   },      /* .PUSHCHARMAP */
    { ccNone,           DoPushCPU       },      /* .PUSHCPU */
    { ccNone,           DoPushSeg       },      /* .PUSHSEG */
    { ccNone,           DoPWC02         },      /* .PWC02 */
    { ccNone,           DoUnexpected    },      /* .REF, .REFERENCED */
    { ccNone,           DoReferTo       },      /* .REFTO, .REFERTO */
    { ccNone,           DoReloc         },      /* .RELOC */
    { ccKeepToken,      DoRepeat        },      /* .REPEAT */
    { ccNone,           DoRes           },      /* .RES */
    { ccNone,           DoInvalid       },      /* .RIGHT */
    { ccNone,           DoROData        },      /* .RODATA */
    { ccNone,           DoScope         },      /* .SCOPE */
    { ccNone,           DoSegment       },      /* .SEGMENT */
    { ccNone,           DoUnexpected    },      /* .SET */
    { ccNone,           DoSetCPU        },      /* .SETCPU */
    { ccNone,           DoUnexpected    },      /* .SIZEOF */
    { ccNone,           DoSmart         },      /* .SMART */
    { ccNone,           DoUnexpected    },      /* .SPRINTF */
    { ccNone,           DoUnexpected    },      /* .STRAT */
    { ccNone,           DoUnexpected    },      /* .STRING */
    { ccNone,           DoUnexpected    },      /* .STRLEN */
    { ccNone,           DoStruct        },      /* .STRUCT */
    { ccNone,           DoTag           },      /* .TAG */
    { ccNone,           DoUnexpected    },      /* .TCOUNT */
    { ccNone,           DoUnexpected    },      /* .TIME */
    { ccKeepToken,      DoUnDef         },      /* .UNDEF, .UNDEFINE */
    { ccNone,           DoUnion         },      /* .UNION */
    { ccNone,           DoUnexpected    },      /* .VERSION */
    { ccNone,           DoWarning       },      /* .WARNING */
    { ccNone,           DoWord          },      /* .WORD */
    { ccNone,           DoUnexpected    },      /* .XMATCH */
    { ccNone,           DoZeropage      },      /* .ZEROPAGE */
};



/*****************************************************************************/
/*                                   Code                                    */
/*****************************************************************************/



void HandlePseudo (void)
/* Handle a pseudo instruction */
{
    CtrlDesc* D;

    /* Calculate the index into the table */
    unsigned Index = CurTok.Tok - TOK_FIRSTPSEUDO;

    /* Safety check */
    if (PSEUDO_COUNT != (TOK_LASTPSEUDO - TOK_FIRSTPSEUDO + 1)) {
        Internal ("Pseudo mismatch: PSEUDO_COUNT = %u, actual count = %u\n",
                  (unsigned) PSEUDO_COUNT, TOK_LASTPSEUDO - TOK_FIRSTPSEUDO + 1);
    }
    CHECK (Index < PSEUDO_COUNT);

    /* Get the pseudo intruction descriptor */
    D = &CtrlCmdTab [Index];

    /* Remember the instruction, then skip it if needed */
    if ((D->Flags & ccKeepToken) == 0) {
        SB_Copy (&Keyword, &CurTok.SVal);
        NextTok ();
    }

    /* Call the handler */
    D->Handler ();
}



void CheckPseudo (void)
/* Check if the stacks are empty at end of assembly */
{
    if (CollCount (&SegStack) != 0) {
        Warning (1, "Segment stack is not empty");
    }
    if (!IS_IsEmpty (&CPUStack)) {
        Warning (1, "CPU stack is not empty");
    }
    if (!TgtTranslateStackIsEmpty ()) {
        Warning (1, "Charmap stack is not empty");
    }
}
