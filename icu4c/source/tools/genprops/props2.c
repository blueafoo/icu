/*
*******************************************************************************
*
*   Copyright (C) 2002, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  props2.c
*   encoding:   US-ASCII
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2002feb24
*   created by: Markus W. Scherer
*
*   Parse more Unicode Character Database files and store
*   additional Unicode character properties in bit set vectors.
*/

#include <stdio.h>
#include "unicode/utypes.h"
#include "unicode/uchar.h"
#include "unicode/uscript.h"
#include "cstring.h"
#include "cmemory.h"
#include "utrie.h"
#include "uprops.h"
#include "propsvec.h"
#include "uparse.h"
#include "genprops.h"

#define LENGTHOF(array) (int32_t)(sizeof(array)/sizeof((array)[0]))

/* data --------------------------------------------------------------------- */

static UNewTrie *trie;
uint32_t *pv;
static int32_t pvCount;

/* miscellaneous ------------------------------------------------------------ */

static char *
trimTerminateField(char *s, char *limit) {
    /* trim leading whitespace */
    s=(char *)u_skipWhitespace(s);

    /* trim trailing whitespace */
    while(s<limit && (*(limit-1)==' ' || *(limit-1)=='\t')) {
        --limit;
    }
    *limit=0;

    return s;
}

static void
parseTwoFieldFile(char *filename, char *basename,
                  const char *ucdFile, const char *suffix,
                  UParseLineFn *lineFn,
                  UErrorCode *pErrorCode) {
    char *fields[2][2];

    if(pErrorCode==NULL || U_FAILURE(*pErrorCode)) {
        return;
    }

    writeUCDFilename(basename, ucdFile, suffix);

    u_parseDelimitedFile(filename, ';', fields, 2, lineFn, NULL, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        fprintf(stderr, "error parsing %s.txt: %s\n", ucdFile, u_errorName(*pErrorCode));
    }
}

static void
parseArabicShaping(char *filename, char *basename,
                   const char *suffix,
                   UErrorCode *pErrorCode);

static void U_CALLCONV
ageLineFn(void *context,
          char *fields[][2], int32_t fieldCount,
          UErrorCode *pErrorCode);

/* parse files with single enumerated properties ---------------------------- */

struct SingleEnum {
    const char *ucdFile, *propName;
    UProperty prop;
    int32_t vecWord, vecShift;
    uint32_t vecMask;
};
typedef struct SingleEnum SingleEnum;

static void
parseSingleEnumFile(char *filename, char *basename, const char *suffix,
                    const SingleEnum *sen,
                    UErrorCode *pErrorCode);

static const SingleEnum scriptSingleEnum={
    "Scripts", "script",
    UCHAR_SCRIPT,
    0, 0, UPROPS_SCRIPT_MASK
};

static const SingleEnum blockSingleEnum={
    "Blocks", "block",
    UCHAR_BLOCK,
    0, UPROPS_BLOCK_SHIFT, UPROPS_BLOCK_MASK
};

static const SingleEnum lineBreakSingleEnum={
    "LineBreak", "line break",
    UCHAR_LINE_BREAK,
    0, UPROPS_LB_SHIFT, UPROPS_LB_MASK
};

static const SingleEnum eawSingleEnum={
    "EastAsianWidth", "east asian width",
    UCHAR_EAST_ASIAN_WIDTH,
    0, UPROPS_EA_SHIFT, UPROPS_EA_MASK
};

static void U_CALLCONV
singleEnumLineFn(void *context,
                 char *fields[][2], int32_t fieldCount,
                 UErrorCode *pErrorCode) {
    const SingleEnum *sen;
    char *s;
    uint32_t start, limit, uv;
    int32_t value;

    sen=(const SingleEnum *)context;

    u_parseCodePointRange(fields[0][0], &start, &limit, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        fprintf(stderr, "genprops: syntax error in %s.txt field 0 at %s\n", sen->ucdFile, fields[0][0]);
        exit(*pErrorCode);
    }
    ++limit;

    /* parse property alias */
    s=trimTerminateField(fields[1][0], fields[1][1]);
    value=u_getPropertyValueEnum(sen->prop, s);
    if(value<0) {
        if(sen->prop==UCHAR_BLOCK) {
            if(isToken("Greek", s)) {
                value=UBLOCK_GREEK; /* Unicode 3.2 renames this to "Greek and Coptic" */
            } else if(isToken("Combining Marks for Symbols", s)) {
                value=UBLOCK_COMBINING_MARKS_FOR_SYMBOLS; /* Unicode 3.2 renames this to "Combining Diacritical Marks for Symbols" */
            } else if(isToken("Private Use", s)) {
                value=UBLOCK_PRIVATE_USE; /* Unicode 3.2 renames this to "Private Use Area" */
            }
        }
    }
    if(value<0) {
        fprintf(stderr, "genprops error: unknown %s name in %s.txt field 1 at %s\n",
                        sen->propName, sen->ucdFile, s);
        exit(U_PARSE_ERROR);
    }

    uv=(uint32_t)(value<<sen->vecShift);
    if((uv&sen->vecMask)!=uv) {
        fprintf(stderr, "genprops error: %s value overflow (0x%x) at %s\n",
                        sen->propName, uv, s);
        exit(U_INTERNAL_PROGRAM_ERROR);
    }

    if(!upvec_setValue(pv, start, limit, sen->vecWord, (uint32_t)value, sen->vecMask, pErrorCode)) {
        fprintf(stderr, "genprops error: unable to set %s code: %s\n",
                        sen->propName, u_errorName(*pErrorCode));
        exit(*pErrorCode);
    }
}

static void
parseSingleEnumFile(char *filename, char *basename, const char *suffix,
                    const SingleEnum *sen,
                    UErrorCode *pErrorCode) {
    char *fields[2][2];

    if(pErrorCode==NULL || U_FAILURE(*pErrorCode)) {
        return;
    }

    writeUCDFilename(basename, sen->ucdFile, suffix);

    u_parseDelimitedFile(filename, ';', fields, 2, singleEnumLineFn, (void *)sen, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        fprintf(stderr, "error parsing %s.txt: %s\n", sen->ucdFile, u_errorName(*pErrorCode));
    }
}

/* parse files with multiple binary properties ------------------------------ */

struct Binary {
    const char *propName;
    int32_t vecWord, vecShift;
};
typedef struct Binary Binary;

struct Binaries {
    const char *ucdFile;
    const Binary *binaries;
    int32_t binariesCount;
};
typedef struct Binaries Binaries;

static const Binary
propListNames[]={
    { "White_Space",                        1, UPROPS_WHITE_SPACE },
    { "Bidi_Control",                       1, UPROPS_BIDI_CONTROL },
    { "Join_Control",                       1, UPROPS_JOIN_CONTROL },
    { "Dash",                               1, UPROPS_DASH },
    { "Hyphen",                             1, UPROPS_HYPHEN },
    { "Quotation_Mark",                     1, UPROPS_QUOTATION_MARK },
    { "Terminal_Punctuation",               1, UPROPS_TERMINAL_PUNCTUATION },
    { "Other_Math",                         1, UPROPS_OTHER_MATH },
    { "Hex_Digit",                          1, UPROPS_HEX_DIGIT },
    { "ASCII_Hex_Digit",                    1, UPROPS_ASCII_HEX_DIGIT },
    { "Other_Alphabetic",                   1, UPROPS_OTHER_ALPHABETIC },
    { "Ideographic",                        1, UPROPS_IDEOGRAPHIC },
    { "Diacritic",                          1, UPROPS_DIACRITIC },
    { "Extender",                           1, UPROPS_EXTENDER },
    { "Other_Lowercase",                    1, UPROPS_OTHER_LOWERCASE },
    { "Other_Uppercase",                    1, UPROPS_OTHER_UPPERCASE },
    { "Noncharacter_Code_Point",            1, UPROPS_NONCHARACTER_CODE_POINT },
    { "Other_Grapheme_Extend",              1, UPROPS_OTHER_GRAPHEME_EXTEND },
    { "Grapheme_Link",                      1, UPROPS_GRAPHEME_LINK },
    { "IDS_Binary_Operator",                1, UPROPS_IDS_BINARY_OPERATOR },
    { "IDS_Trinary_Operator",               1, UPROPS_IDS_TRINARY_OPERATOR },
    { "Radical",                            1, UPROPS_RADICAL },
    { "Unified_Ideograph",                  1, UPROPS_UNIFIED_IDEOGRAPH },
    { "Other_Default_Ignorable_Code_Point", 1, UPROPS_OTHER_DEFAULT_IGNORABLE_CODE_POINT },
    { "Deprecated",                         1, UPROPS_DEPRECATED },
    { "Soft_Dotted",                        1, UPROPS_SOFT_DOTTED },
    { "Logical_Order_Exception",            1, UPROPS_LOGICAL_ORDER_EXCEPTION },
    { "ID_Start_Exceptions",                1, UPROPS_ID_START_EXCEPTIONS }
};

static const Binaries
propListBinaries={
    "PropList", propListNames, LENGTHOF(propListNames)
};

static const Binary
derCorePropsNames[]={
    { "XID_Start",                          1, UPROPS_XID_START },
    { "XID_Continue",                       1, UPROPS_XID_CONTINUE }
};

static const Binaries
derCorePropsBinaries={
    "DerivedCoreProperties", derCorePropsNames, LENGTHOF(derCorePropsNames)
};

static char ignoredProps[100][64];
static int32_t ignoredPropsCount;

static void
addIgnoredProp(char *s, char *limit) {
    int32_t i;

    s=trimTerminateField(s, limit);
    for(i=0; i<ignoredPropsCount; ++i) {
        if(0==uprv_strcmp(ignoredProps[i], s)) {
            return;
        }
    }
    uprv_strcpy(ignoredProps[ignoredPropsCount++], s);
}

static void U_CALLCONV
binariesLineFn(void *context,
               char *fields[][2], int32_t fieldCount,
               UErrorCode *pErrorCode) {
    const Binaries *bin;
    char *s;
    uint32_t start, limit, uv;
    int32_t i;

    bin=(const Binaries *)context;

    u_parseCodePointRange(fields[0][0], &start, &limit, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        fprintf(stderr, "genprops: syntax error in %s.txt field 0 at %s\n", bin->ucdFile, fields[0][0]);
        exit(*pErrorCode);
    }
    ++limit;

    /* parse binary property name */
    s=(char *)u_skipWhitespace(fields[1][0]);
    for(i=0;; ++i) {
        if(i==bin->binariesCount) {
            /* ignore unrecognized properties */
            addIgnoredProp(s, fields[1][1]);
            return;
        }
        if(isToken(bin->binaries[i].propName, s)) {
            break;
        }
    }

    if(bin->binaries[i].vecShift>=32) {
        fprintf(stderr, "genprops error: shift value %d>=32 for %s %s\n",
                        bin->binaries[i].vecShift, bin->ucdFile, bin->binaries[i].propName);
        exit(U_INTERNAL_PROGRAM_ERROR);
    }
    uv=U_MASK(bin->binaries[i].vecShift);

    if(!upvec_setValue(pv, start, limit, bin->binaries[i].vecWord, uv, uv, pErrorCode)) {
        fprintf(stderr, "genprops error: unable to set %s code: %s\n",
                        bin->binaries[i].propName, u_errorName(*pErrorCode));
        exit(*pErrorCode);
    }
}

static void
parseBinariesFile(char *filename, char *basename, const char *suffix,
                  const Binaries *bin,
                  UErrorCode *pErrorCode) {
    char *fields[2][2];
    int32_t i;

    if(pErrorCode==NULL || U_FAILURE(*pErrorCode)) {
        return;
    }

    writeUCDFilename(basename, bin->ucdFile, suffix);

    ignoredPropsCount=0;

    u_parseDelimitedFile(filename, ';', fields, 2, binariesLineFn, (void *)bin, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        fprintf(stderr, "error parsing %s.txt: %s\n", bin->ucdFile, u_errorName(*pErrorCode));
    }

    for(i=0; i<ignoredPropsCount; ++i) {
        printf("genprops: ignoring property %s in %s.txt\n", ignoredProps[i], bin->ucdFile);
    }
}

/* -------------------------------------------------------------------------- */

U_CFUNC void
initAdditionalProperties() {
    pv=upvec_open(UPROPS_VECTOR_WORDS, 20000);
}

U_CFUNC void
generateAdditionalProperties(char *filename, const char *suffix, UErrorCode *pErrorCode) {
    char *basename;

    basename=filename+uprv_strlen(filename);

    /* process various UCD .txt files */
    parseTwoFieldFile(filename, basename, "DerivedAge", suffix, ageLineFn, pErrorCode);

    /*
     * UTR 24 says:
     * Section 2:
     *   "Common - For characters that may be used
     *             within multiple scripts,
     *             or any unassigned code points."
     *
     * Section 4:
     *   "The value COMMON is the default value,
     *    given to all code points that are not
     *    explicitly mentioned in the data file."
     */
    if(!upvec_setValue(pv, 0, 0x110000, 0, (uint32_t)USCRIPT_COMMON, UPROPS_SCRIPT_MASK, pErrorCode)) {
        fprintf(stderr, "genprops error: unable to set script code: %s\n", u_errorName(*pErrorCode));
        exit(*pErrorCode);
    }

    parseSingleEnumFile(filename, basename, suffix, &scriptSingleEnum, pErrorCode);

    parseSingleEnumFile(filename, basename, suffix, &blockSingleEnum, pErrorCode);

    parseBinariesFile(filename, basename, suffix, &propListBinaries, pErrorCode);

    parseBinariesFile(filename, basename, suffix, &derCorePropsBinaries, pErrorCode);

    parseSingleEnumFile(filename, basename, suffix, &lineBreakSingleEnum, pErrorCode);

    parseArabicShaping(filename, basename, suffix, pErrorCode);

    /*
     * Preset East Asian Width defaults:
     * N for all
     * A for Private Use
     * W for plane 2
     */
    *pErrorCode=U_ZERO_ERROR;
    if( !upvec_setValue(pv, 0, 0x110000, 0, (uint32_t)(U_EA_NEUTRAL<<UPROPS_EA_SHIFT), UPROPS_EA_MASK, pErrorCode) ||
        !upvec_setValue(pv, 0xe000, 0xf900, 0, (uint32_t)(U_EA_AMBIGUOUS<<UPROPS_EA_SHIFT), UPROPS_EA_MASK, pErrorCode) ||
        !upvec_setValue(pv, 0xf0000, 0xffffe, 0, (uint32_t)(U_EA_AMBIGUOUS<<UPROPS_EA_SHIFT), UPROPS_EA_MASK, pErrorCode) ||
        !upvec_setValue(pv, 0x100000, 0x10fffe, 0, (uint32_t)(U_EA_AMBIGUOUS<<UPROPS_EA_SHIFT), UPROPS_EA_MASK, pErrorCode) ||
        !upvec_setValue(pv, 0x20000, 0x2fffe, 0, (uint32_t)(U_EA_WIDE<<UPROPS_EA_SHIFT), UPROPS_EA_MASK, pErrorCode)
    ) {
        fprintf(stderr, "genprops: unable to set default East Asian Widths: %s\n", u_errorName(*pErrorCode));
        exit(*pErrorCode);
    }

    /* parse EastAsianWidth.txt */
    parseSingleEnumFile(filename, basename, suffix, &eawSingleEnum, pErrorCode);

    trie=utrie_open(NULL, NULL, 50000, 0, FALSE);
    if(trie==NULL) {
        *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
        upvec_close(pv);
        return;
    }

    pvCount=upvec_toTrie(pv, trie, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        fprintf(stderr, "genprops error: unable to build trie for additional properties: %s\n", u_errorName(*pErrorCode));
        exit(*pErrorCode);
    }
}

/* DerivedAge.txt ----------------------------------------------------------- */

static void U_CALLCONV
ageLineFn(void *context,
          char *fields[][2], int32_t fieldCount,
          UErrorCode *pErrorCode) {
    char *s, *end;
    uint32_t value, start, limit, version;

    u_parseCodePointRange(fields[0][0], &start, &limit, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        fprintf(stderr, "genprops: syntax error in DerivedAge.txt field 0 at %s\n", fields[0][0]);
        exit(*pErrorCode);
    }
    ++limit;

    /* parse version number */
    s=(char *)u_skipWhitespace(fields[1][0]);
    value=(uint32_t)uprv_strtoul(s, &end, 10);
    if(s==end || value==0 || value>15 || (*end!='.' && *end!=' ' && *end!='\t' && *end!=0)) {
        fprintf(stderr, "genprops: syntax error in DerivedAge.txt field 1 at %s\n", fields[1][0]);
        *pErrorCode=U_PARSE_ERROR;
        exit(U_PARSE_ERROR);
    }
    version=value<<4;

    /* parse minor version number */
    if(*end=='.') {
        s=(char *)u_skipWhitespace(end+1);
        value=(uint32_t)uprv_strtoul(s, &end, 10);
        if(s==end || value>15 || (*end!=' ' && *end!='\t' && *end!=0)) {
            fprintf(stderr, "genprops: syntax error in DerivedAge.txt field 1 at %s\n", fields[1][0]);
            *pErrorCode=U_PARSE_ERROR;
            exit(U_PARSE_ERROR);
        }
        version|=value;
    }

    if(!upvec_setValue(pv, start, limit, 0, version<<UPROPS_AGE_SHIFT, UPROPS_AGE_MASK, pErrorCode)) {
        fprintf(stderr, "genprops error: unable to set character age: %s\n", u_errorName(*pErrorCode));
        exit(*pErrorCode);
    }
}

/* ArabicShaping.txt -------------------------------------------------------- */

static void U_CALLCONV
arabicShapingLineFn(void *context,
                    char *fields[][2], int32_t fieldCount,
                    UErrorCode *pErrorCode) {
    char *s;
    uint32_t start, limit;
    int32_t jt, jg;

    u_parseCodePointRange(fields[0][0], &start, &limit, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        fprintf(stderr, "genprops: syntax error in ArabicShaping.txt field 0 at %s\n", fields[0][0]);
        exit(*pErrorCode);
    }
    ++limit;

    /* parse joining type */
    jt=u_getPropertyValueEnum(UCHAR_JOINING_TYPE, trimTerminateField(fields[2][0], fields[2][1]));
    if(jt<0) {
        fprintf(stderr, "genprops error: unknown joining type in \"%s\" in ArabicShaping.txt\n", fields[2][0]);
        *pErrorCode=U_PARSE_ERROR;
        exit(U_PARSE_ERROR);
    }

    /* parse joining group */
    s=trimTerminateField(fields[3][0], fields[3][1]);
    jg=u_getPropertyValueEnum(UCHAR_JOINING_GROUP, s);
    if(jg<0) {
        if(isToken("<no shaping>", s)) {
            jg=0;
        }
    }
    if(jg<0) {
        fprintf(stderr, "genprops error: unknown joining group in \"%s\" in ArabicShaping.txt\n", s);
        *pErrorCode=U_PARSE_ERROR;
        exit(U_PARSE_ERROR);
    }

    if(!upvec_setValue(pv, start, limit, 2, ((uint32_t)jt<<UPROPS_JT_SHIFT)|((uint32_t)jg<<UPROPS_JG_SHIFT), UPROPS_JT_MASK|UPROPS_JG_MASK, pErrorCode)) {
        fprintf(stderr, "genprops error: unable to set joining type/group code: %s\n", u_errorName(*pErrorCode));
        exit(*pErrorCode);
    }
}

static void
parseArabicShaping(char *filename, char *basename,
                   const char *suffix,
                   UErrorCode *pErrorCode) {
    char *fields[4][2];

    if(pErrorCode==NULL || U_FAILURE(*pErrorCode)) {
        return;
    }

    writeUCDFilename(basename, "ArabicShaping", suffix);

    u_parseDelimitedFile(filename, ';', fields, 4, arabicShapingLineFn, NULL, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        fprintf(stderr, "error parsing ArabicShaping.txt: %s\n", u_errorName(*pErrorCode));
    }
}

/* data serialization ------------------------------------------------------- */

U_CFUNC int32_t
writeAdditionalData(uint8_t *p, int32_t capacity, int32_t indexes[UPROPS_INDEX_COUNT]) {
    int32_t length;
    UErrorCode errorCode;

    errorCode=U_ZERO_ERROR;
    length=utrie_serialize(trie, p, capacity, getFoldedPropsValue, TRUE, &errorCode);
    if(U_FAILURE(errorCode)) {
        fprintf(stderr, "genprops error: unable to serialize trie for additional properties: %s\n", u_errorName(errorCode));
        exit(errorCode);
    }
    if(p!=NULL) {
        p+=length;
        capacity-=length;
        if(beVerbose) {
            printf("size in bytes of additional props trie:%5u\n", length);
        }

        /* set indexes */
        indexes[UPROPS_ADDITIONAL_VECTORS_INDEX]=
            indexes[UPROPS_ADDITIONAL_TRIE_INDEX]+length/4;
        indexes[UPROPS_ADDITIONAL_VECTORS_COLUMNS_INDEX]=UPROPS_VECTOR_WORDS;
        indexes[UPROPS_RESERVED_INDEX]=
            indexes[UPROPS_ADDITIONAL_VECTORS_INDEX]+pvCount;

        indexes[UPROPS_MAX_VALUES_INDEX]=
            (((int32_t)UBLOCK_COUNT-1)<<UPROPS_BLOCK_SHIFT)|
            ((int32_t)USCRIPT_CODE_LIMIT-1);
    }

    if(p!=NULL && (pvCount*4)<=capacity) {
        uprv_memcpy(p, pv, pvCount*4);
        if(beVerbose) {
            printf("number of additional props vectors:    %5u\n", pvCount/UPROPS_VECTOR_WORDS);
            printf("number of 32-bit words per vector:     %5u\n", UPROPS_VECTOR_WORDS);
        }
    }
    length+=pvCount*4;

    if(p!=NULL) {
        utrie_close(trie);
        upvec_close(pv);
    }
    return length;
}
