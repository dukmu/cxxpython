/* Adapted from CPython 3.12.14 Modules/_sre/sre.c and sre.h.
 * Matching algorithms retain the Secret Labs/CNRI/PSF notices in sre_lib.h.
 * See doc/licenses/python-3.12.14.txt for the redistribution terms. */
#include "engine.h"
#include "sre_constants.h"
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef ptrdiff_t Py_ssize_t;
typedef uint32_t SRE_CODE;
#define SRE_CODE_BITS 32
#define SRE_MAXREPEAT UINT32_MAX
#define SRE_MAXGROUPS ((uint32_t)INT32_MAX / 2)
#define LOCAL(type) static inline type
#define TRACE(v)
#define SRE_ERROR_ILLEGAL -1
#define SRE_ERROR_STATE -2
#define SRE_ERROR_MEMORY -9
#define SRE_ERROR_INTERRUPTED -10
#define PyMem_Free free
#define PyMem_Realloc realloc
#define PyObject_Malloc malloc
#define PyObject_Free free
#define Py_ISDIGIT(c) ((c) >= '0' && (c) <= '9')
#define Py_ISSPACE(c) ((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))
#define Py_ISALNUM(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || Py_ISDIGIT(c))
#define Py_TOLOWER(c) ((c) >= 'A' && (c) <= 'Z' ? (c) + ('a' - 'A') : (c))
#define Py_UNICODE_ISDECIMAL cxx_sre_unicode_decimal
#define Py_UNICODE_ISSPACE cxx_sre_unicode_space
#define Py_UNICODE_ISLINEBREAK cxx_sre_unicode_linebreak
#define Py_UNICODE_ISALNUM cxx_sre_unicode_alnum
#define Py_UNICODE_TOLOWER cxx_sre_unicode_lower
#define Py_UNICODE_TOUPPER cxx_sre_unicode_upper
#define PyErr_CheckSignals() (state->poll && state->poll(state->context))
#define CXX_SRE_POLL()                                                                             \
    do {                                                                                           \
        if (!(++state->sigcount & 4095) && PyErr_CheckSignals())                                   \
            return SRE_ERROR_INTERRUPTED;                                                          \
    } while (0)

typedef struct SRE_REPEAT_T {
    Py_ssize_t count;
    const SRE_CODE *pattern;
    const void *last_ptr;
    struct SRE_REPEAT_T *prev, *pool_prev, *pool_next;
} SRE_REPEAT;
typedef struct {
    const void *ptr, *beginning, *start, *end;
    int match_all, must_advance, lastmark, lastindex;
    const void **mark;
    char *data_stack;
    size_t data_stack_size, data_stack_base;
    SRE_REPEAT *repeat, *repeat_pool_used, *repeat_pool_unused;
    unsigned int sigcount;
    int (*poll)(void *);
    void *context;
} SRE_STATE;
#define SRE_IS_DIGIT(ch) ((ch) <= '9' && Py_ISDIGIT(ch))
#define SRE_IS_SPACE(ch) ((ch) <= ' ' && Py_ISSPACE(ch))
#define SRE_IS_LINEBREAK(ch) ((ch) == '\n')
#define SRE_IS_WORD(ch) ((ch) <= 'z' && (Py_ISALNUM(ch) || (ch) == '_'))

static unsigned int sre_lower_ascii(unsigned int ch)
{
    return ((ch) < 128 ? Py_TOLOWER(ch) : ch);
}

/* locale-specific character predicates */
/* !(c & ~N) == (c < N+1) for any unsigned c, this avoids
 * warnings when c's type supports only numbers < N+1 */
#define SRE_LOC_IS_ALNUM(ch) (!((ch) & ~255) ? isalnum((ch)) : 0)
#define SRE_LOC_IS_WORD(ch) (SRE_LOC_IS_ALNUM((ch)) || (ch) == '_')

static unsigned int sre_lower_locale(unsigned int ch)
{
    return ((ch) < 256 ? (unsigned int)tolower((ch)) : ch);
}

static unsigned int sre_upper_locale(unsigned int ch)
{
    return ((ch) < 256 ? (unsigned int)toupper((ch)) : ch);
}

/* unicode-specific character predicates */

#define SRE_UNI_IS_DIGIT(ch) Py_UNICODE_ISDECIMAL(ch)
#define SRE_UNI_IS_SPACE(ch) Py_UNICODE_ISSPACE(ch)
#define SRE_UNI_IS_LINEBREAK(ch) Py_UNICODE_ISLINEBREAK(ch)
#define SRE_UNI_IS_ALNUM(ch) Py_UNICODE_ISALNUM(ch)
#define SRE_UNI_IS_WORD(ch) (SRE_UNI_IS_ALNUM(ch) || (ch) == '_')

static unsigned int sre_lower_unicode(unsigned int ch)
{
    return (unsigned int)Py_UNICODE_TOLOWER(ch);
}

static unsigned int sre_upper_unicode(unsigned int ch)
{
    return (unsigned int)Py_UNICODE_TOUPPER(ch);
}

LOCAL(int)
sre_category(SRE_CODE category, unsigned int ch)
{
    switch (category) {

    case SRE_CATEGORY_DIGIT:
        return SRE_IS_DIGIT(ch);
    case SRE_CATEGORY_NOT_DIGIT:
        return !SRE_IS_DIGIT(ch);
    case SRE_CATEGORY_SPACE:
        return SRE_IS_SPACE(ch);
    case SRE_CATEGORY_NOT_SPACE:
        return !SRE_IS_SPACE(ch);
    case SRE_CATEGORY_WORD:
        return SRE_IS_WORD(ch);
    case SRE_CATEGORY_NOT_WORD:
        return !SRE_IS_WORD(ch);
    case SRE_CATEGORY_LINEBREAK:
        return SRE_IS_LINEBREAK(ch);
    case SRE_CATEGORY_NOT_LINEBREAK:
        return !SRE_IS_LINEBREAK(ch);

    case SRE_CATEGORY_LOC_WORD:
        return SRE_LOC_IS_WORD(ch);
    case SRE_CATEGORY_LOC_NOT_WORD:
        return !SRE_LOC_IS_WORD(ch);

    case SRE_CATEGORY_UNI_DIGIT:
        return SRE_UNI_IS_DIGIT(ch);
    case SRE_CATEGORY_UNI_NOT_DIGIT:
        return !SRE_UNI_IS_DIGIT(ch);
    case SRE_CATEGORY_UNI_SPACE:
        return SRE_UNI_IS_SPACE(ch);
    case SRE_CATEGORY_UNI_NOT_SPACE:
        return !SRE_UNI_IS_SPACE(ch);
    case SRE_CATEGORY_UNI_WORD:
        return SRE_UNI_IS_WORD(ch);
    case SRE_CATEGORY_UNI_NOT_WORD:
        return !SRE_UNI_IS_WORD(ch);
    case SRE_CATEGORY_UNI_LINEBREAK:
        return SRE_UNI_IS_LINEBREAK(ch);
    case SRE_CATEGORY_UNI_NOT_LINEBREAK:
        return !SRE_UNI_IS_LINEBREAK(ch);
    }
    return 0;
}

LOCAL(int)
char_loc_ignore(SRE_CODE pattern, SRE_CODE ch)
{
    return ch == pattern || (SRE_CODE)sre_lower_locale(ch) == pattern ||
           (SRE_CODE)sre_upper_locale(ch) == pattern;
}

/* helpers */

static void data_stack_dealloc(SRE_STATE *state)
{
    if (state->data_stack) {
        PyMem_Free(state->data_stack);
        state->data_stack = NULL;
    }
    state->data_stack_size = state->data_stack_base = 0;
}

static int data_stack_grow(SRE_STATE *state, Py_ssize_t size)
{
    Py_ssize_t minsize, cursize;
    minsize = state->data_stack_base + size;
    cursize = state->data_stack_size;
    if (cursize < minsize) {
        void *stack;
        cursize = minsize + minsize / 4 + 1024;
        TRACE(("allocate/grow stack %zd\n", cursize));
        stack = PyMem_Realloc(state->data_stack, cursize);
        if (!stack) {
            data_stack_dealloc(state);
            return SRE_ERROR_MEMORY;
        }
        state->data_stack = (char *)stack;
        state->data_stack_size = cursize;
    }
    return 0;
}

/* memory pool functions for SRE_REPEAT, this can avoid memory
   leak when SRE(match) function terminates abruptly.
   state->repeat_pool_used is a doubly-linked list, so that we
   can remove a SRE_REPEAT node from it.
   state->repeat_pool_unused is a singly-linked list, we put/get
   node at the head. */
static SRE_REPEAT *repeat_pool_malloc(SRE_STATE *state)
{
    SRE_REPEAT *repeat;

    if (state->repeat_pool_unused) {
        /* remove from unused pool (singly-linked list) */
        repeat = state->repeat_pool_unused;
        state->repeat_pool_unused = repeat->pool_next;
    } else {
        repeat = PyObject_Malloc(sizeof(SRE_REPEAT));
        if (!repeat) {
            return NULL;
        }
    }

    /* add to used pool (doubly-linked list) */
    SRE_REPEAT *temp = state->repeat_pool_used;
    if (temp) {
        temp->pool_prev = repeat;
    }
    repeat->pool_prev = NULL;
    repeat->pool_next = temp;
    state->repeat_pool_used = repeat;

    return repeat;
}

static void repeat_pool_free(SRE_STATE *state, SRE_REPEAT *repeat)
{
    SRE_REPEAT *prev = repeat->pool_prev;
    SRE_REPEAT *next = repeat->pool_next;

    /* remove from used pool (doubly-linked list) */
    if (prev) {
        prev->pool_next = next;
    } else {
        state->repeat_pool_used = next;
    }
    if (next) {
        next->pool_prev = prev;
    }

    /* add to unused pool (singly-linked list) */
    repeat->pool_next = state->repeat_pool_unused;
    state->repeat_pool_unused = repeat;
}

static void repeat_pool_clear(SRE_STATE *state)
{
    /* clear used pool */
    SRE_REPEAT *next = state->repeat_pool_used;
    state->repeat_pool_used = NULL;
    while (next) {
        SRE_REPEAT *temp = next;
        next = temp->pool_next;
        PyObject_Free(temp);
    }

    /* clear unused pool */
    next = state->repeat_pool_unused;
    state->repeat_pool_unused = NULL;
    while (next) {
        SRE_REPEAT *temp = next;
        next = temp->pool_next;
        PyObject_Free(temp);
    }
}

#define SRE_CHAR uint32_t
#define SIZEOF_SRE_CHAR 4
#define SRE(F) sre_ucs4_##F
#include "sre_lib.h"

/* Defining this one enables tracing of the validator */
#undef VVERBOSE

/* Trace macro for the validator */
#if defined(VVERBOSE)
#define VTRACE(v) printf v
#else
#define VTRACE(v)                                                                                  \
    do {                                                                                           \
    } while (0) /* do nothing */
#endif

/* Report failure */
#define FAIL                                                                                       \
    do {                                                                                           \
        VTRACE(("FAIL: %d\n", __LINE__));                                                          \
        return -1;                                                                                 \
    } while (0)

/* Extract opcode, argument, or skip count from code array */
#define GET_OP                                                                                     \
    do {                                                                                           \
        VTRACE(("%p: ", code));                                                                    \
        if (code >= end)                                                                           \
            FAIL;                                                                                  \
        op = *code++;                                                                              \
        VTRACE(("%lu (op)\n", (unsigned long)op));                                                 \
    } while (0)
#define GET_ARG                                                                                    \
    do {                                                                                           \
        VTRACE(("%p= ", code));                                                                    \
        if (code >= end)                                                                           \
            FAIL;                                                                                  \
        arg = *code++;                                                                             \
        VTRACE(("%lu (arg)\n", (unsigned long)arg));                                               \
    } while (0)
#define GET_SKIP_ADJ(adj)                                                                          \
    do {                                                                                           \
        VTRACE(("%p= ", code));                                                                    \
        if (code >= end)                                                                           \
            FAIL;                                                                                  \
        skip = *code;                                                                              \
        VTRACE(("%lu (skip to %p)\n", (unsigned long)skip, code + skip));                          \
        if (skip - adj > (uintptr_t)(end - code))                                                  \
            FAIL;                                                                                  \
        code++;                                                                                    \
    } while (0)
#define GET_SKIP GET_SKIP_ADJ(0)

static int _validate_charset(SRE_CODE *code, SRE_CODE *end)
{
    /* Some variables are manipulated by the macros above */
    SRE_CODE op;
    SRE_CODE arg;
    SRE_CODE offset;
    int i;

    while (code < end) {
        GET_OP;
        switch (op) {

        case SRE_OP_NEGATE:
            break;

        case SRE_OP_LITERAL:
            GET_ARG;
            break;

        case SRE_OP_RANGE:
        case SRE_OP_RANGE_UNI_IGNORE:
            GET_ARG;
            GET_ARG;
            break;

        case SRE_OP_CHARSET:
            offset = 256 / SRE_CODE_BITS; /* 256-bit bitmap */
            if (offset > (uintptr_t)(end - code))
                FAIL;
            code += offset;
            break;

        case SRE_OP_BIGCHARSET:
            GET_ARG;                         /* Number of blocks */
            offset = 256 / sizeof(SRE_CODE); /* 256-byte table */
            if (offset > (uintptr_t)(end - code))
                FAIL;
            /* Make sure that each byte points to a valid block */
            for (i = 0; i < 256; i++) {
                if (((unsigned char *)code)[i] >= arg)
                    FAIL;
            }
            code += offset;
            offset = arg * (256 / SRE_CODE_BITS); /* 256-bit bitmap times arg */
            if (offset > (uintptr_t)(end - code))
                FAIL;
            code += offset;
            break;

        case SRE_OP_CATEGORY:
            GET_ARG;
            switch (arg) {
            case SRE_CATEGORY_DIGIT:
            case SRE_CATEGORY_NOT_DIGIT:
            case SRE_CATEGORY_SPACE:
            case SRE_CATEGORY_NOT_SPACE:
            case SRE_CATEGORY_WORD:
            case SRE_CATEGORY_NOT_WORD:
            case SRE_CATEGORY_LINEBREAK:
            case SRE_CATEGORY_NOT_LINEBREAK:
            case SRE_CATEGORY_LOC_WORD:
            case SRE_CATEGORY_LOC_NOT_WORD:
            case SRE_CATEGORY_UNI_DIGIT:
            case SRE_CATEGORY_UNI_NOT_DIGIT:
            case SRE_CATEGORY_UNI_SPACE:
            case SRE_CATEGORY_UNI_NOT_SPACE:
            case SRE_CATEGORY_UNI_WORD:
            case SRE_CATEGORY_UNI_NOT_WORD:
            case SRE_CATEGORY_UNI_LINEBREAK:
            case SRE_CATEGORY_UNI_NOT_LINEBREAK:
                break;
            default:
                FAIL;
            }
            break;

        default:
            FAIL;
        }
    }

    return 0;
}

/* Returns 0 on success, -1 on failure, and 1 if the last op is JUMP. */
#define _validate_inner(a, b, c) _validate_inner_depth((a), (b), (c), depth + 1)
static int _validate_inner_depth(SRE_CODE *code, SRE_CODE *end, Py_ssize_t groups, unsigned depth)
{
    if (depth > 1000)
        return -1;
    /* Some variables are manipulated by the macros above */
    SRE_CODE op;
    SRE_CODE arg;
    SRE_CODE skip;

    VTRACE(("code=%p, end=%p\n", code, end));

    if (code > end)
        FAIL;

    while (code < end) {
        GET_OP;
        switch (op) {

        case SRE_OP_MARK:
            /* We don't check whether marks are properly nested; the
               sre_match() code is robust even if they don't, and the worst
               you can get is nonsensical match results. */
            GET_ARG;
            if (arg >= 2 * (size_t)groups) {
                VTRACE(("arg=%d, groups=%d\n", (int)arg, (int)groups));
                FAIL;
            }
            break;

        case SRE_OP_LITERAL:
        case SRE_OP_NOT_LITERAL:
        case SRE_OP_LITERAL_IGNORE:
        case SRE_OP_NOT_LITERAL_IGNORE:
        case SRE_OP_LITERAL_UNI_IGNORE:
        case SRE_OP_NOT_LITERAL_UNI_IGNORE:
        case SRE_OP_LITERAL_LOC_IGNORE:
        case SRE_OP_NOT_LITERAL_LOC_IGNORE:
            GET_ARG;
            /* The arg is just a character, nothing to check */
            break;

        case SRE_OP_SUCCESS:
        case SRE_OP_FAILURE:
            /* Nothing to check; these normally end the matching process */
            break;

        case SRE_OP_AT:
            GET_ARG;
            switch (arg) {
            case SRE_AT_BEGINNING:
            case SRE_AT_BEGINNING_STRING:
            case SRE_AT_BEGINNING_LINE:
            case SRE_AT_END:
            case SRE_AT_END_LINE:
            case SRE_AT_END_STRING:
            case SRE_AT_BOUNDARY:
            case SRE_AT_NON_BOUNDARY:
            case SRE_AT_LOC_BOUNDARY:
            case SRE_AT_LOC_NON_BOUNDARY:
            case SRE_AT_UNI_BOUNDARY:
            case SRE_AT_UNI_NON_BOUNDARY:
                break;
            default:
                FAIL;
            }
            break;

        case SRE_OP_ANY:
        case SRE_OP_ANY_ALL:
            /* These have no operands */
            break;

        case SRE_OP_IN:
        case SRE_OP_IN_IGNORE:
        case SRE_OP_IN_UNI_IGNORE:
        case SRE_OP_IN_LOC_IGNORE:
            GET_SKIP;
            /* Stop 1 before the end; we check the FAILURE below */
            if (_validate_charset(code, code + skip - 2))
                FAIL;
            if (code[skip - 2] != SRE_OP_FAILURE)
                FAIL;
            code += skip - 1;
            break;

        case SRE_OP_INFO: {
            /* A minimal info field is
               <INFO> <1=skip> <2=flags> <3=min> <4=max>;
               If SRE_INFO_PREFIX or SRE_INFO_CHARSET is in the flags,
               more follows. */
            SRE_CODE flags, i;
            SRE_CODE *newcode;
            GET_SKIP;
            newcode = code + skip - 1;
            GET_ARG;
            flags = arg;
            GET_ARG;
            GET_ARG;
            /* Check that only valid flags are present */
            if ((flags & ~(SRE_INFO_PREFIX | SRE_INFO_LITERAL | SRE_INFO_CHARSET)) != 0)
                FAIL;
            /* PREFIX and CHARSET are mutually exclusive */
            if ((flags & SRE_INFO_PREFIX) && (flags & SRE_INFO_CHARSET))
                FAIL;
            /* LITERAL implies PREFIX */
            if ((flags & SRE_INFO_LITERAL) && !(flags & SRE_INFO_PREFIX))
                FAIL;
            /* Validate the prefix */
            if (flags & SRE_INFO_PREFIX) {
                SRE_CODE prefix_len;
                GET_ARG;
                prefix_len = arg;
                GET_ARG;
                /* Here comes the prefix string */
                if (prefix_len > (uintptr_t)(newcode - code))
                    FAIL;
                code += prefix_len;
                /* And here comes the overlap table */
                if (prefix_len > (uintptr_t)(newcode - code))
                    FAIL;
                /* Each overlap value should be < prefix_len */
                for (i = 0; i < prefix_len; i++) {
                    if (code[i] >= prefix_len)
                        FAIL;
                }
                code += prefix_len;
            }
            /* Validate the charset */
            if (flags & SRE_INFO_CHARSET) {
                if (_validate_charset(code, newcode - 1))
                    FAIL;
                if (newcode[-1] != SRE_OP_FAILURE)
                    FAIL;
                code = newcode;
            } else if (code != newcode) {
                VTRACE(("code=%p, newcode=%p\n", code, newcode));
                FAIL;
            }
        } break;

        case SRE_OP_BRANCH: {
            SRE_CODE *target = NULL;
            for (;;) {
                GET_SKIP;
                if (skip == 0)
                    break;
                /* Stop 2 before the end; we check the JUMP below */
                if (_validate_inner(code, code + skip - 3, groups))
                    FAIL;
                code += skip - 3;
                /* Check that it ends with a JUMP, and that each JUMP
                   has the same target */
                GET_OP;
                if (op != SRE_OP_JUMP)
                    FAIL;
                GET_SKIP;
                if (target == NULL)
                    target = code + skip - 1;
                else if (code + skip - 1 != target)
                    FAIL;
            }
            if (code != target)
                FAIL;
        } break;

        case SRE_OP_REPEAT_ONE:
        case SRE_OP_MIN_REPEAT_ONE:
        case SRE_OP_POSSESSIVE_REPEAT_ONE: {
            SRE_CODE min, max;
            GET_SKIP;
            GET_ARG;
            min = arg;
            GET_ARG;
            max = arg;
            if (min > max)
                FAIL;
            if (max > SRE_MAXREPEAT)
                FAIL;
            if (_validate_inner(code, code + skip - 4, groups))
                FAIL;
            code += skip - 4;
            GET_OP;
            if (op != SRE_OP_SUCCESS)
                FAIL;
        } break;

        case SRE_OP_REPEAT:
        case SRE_OP_POSSESSIVE_REPEAT: {
            SRE_CODE op1 = op, min, max;
            GET_SKIP;
            GET_ARG;
            min = arg;
            GET_ARG;
            max = arg;
            if (min > max)
                FAIL;
            if (max > SRE_MAXREPEAT)
                FAIL;
            if (_validate_inner(code, code + skip - 3, groups))
                FAIL;
            code += skip - 3;
            GET_OP;
            if (op1 == SRE_OP_POSSESSIVE_REPEAT) {
                if (op != SRE_OP_SUCCESS)
                    FAIL;
            } else {
                if (op != SRE_OP_MAX_UNTIL && op != SRE_OP_MIN_UNTIL)
                    FAIL;
            }
        } break;

        case SRE_OP_ATOMIC_GROUP: {
            GET_SKIP;
            if (_validate_inner(code, code + skip - 2, groups))
                FAIL;
            code += skip - 2;
            GET_OP;
            if (op != SRE_OP_SUCCESS)
                FAIL;
        } break;

        case SRE_OP_GROUPREF:
        case SRE_OP_GROUPREF_IGNORE:
        case SRE_OP_GROUPREF_UNI_IGNORE:
        case SRE_OP_GROUPREF_LOC_IGNORE:
            GET_ARG;
            if (arg >= (size_t)groups)
                FAIL;
            break;

        case SRE_OP_GROUPREF_EXISTS:
            /* The regex syntax for this is: '(?(group)then|else)', where
               'group' is either an integer group number or a group name,
               'then' and 'else' are sub-regexes, and 'else' is optional. */
            GET_ARG;
            if (arg >= (size_t)groups)
                FAIL;
            GET_SKIP_ADJ(1);
            code--; /* The skip is relative to the first arg! */
            /* There are two possibilities here: if there is both a 'then'
               part and an 'else' part, the generated code looks like:

               GROUPREF_EXISTS
               <group>
               <skipyes>
               ...then part...
               JUMP
               <skipno>
               (<skipyes> jumps here)
               ...else part...
               (<skipno> jumps here)

               If there is only a 'then' part, it looks like:

               GROUPREF_EXISTS
               <group>
               <skip>
               ...then part...
               (<skip> jumps here)

               There is no direct way to decide which it is, and we don't want
               to allow arbitrary jumps anywhere in the code; so we just look
               for a JUMP opcode preceding our skip target.
            */
            VTRACE(("then part:\n"));
            int rc = _validate_inner(code + 1, code + skip - 1, groups);
            if (rc == 1) {
                VTRACE(("else part:\n"));
                code += skip - 2; /* Position after JUMP, at <skipno> */
                GET_SKIP;
                rc = _validate_inner(code, code + skip - 1, groups);
            }
            if (rc)
                FAIL;
            code += skip - 1;
            break;

        case SRE_OP_ASSERT:
        case SRE_OP_ASSERT_NOT:
            GET_SKIP;
            GET_ARG; /* 0 for lookahead, width for lookbehind */
            code--;  /* Back up over arg to simplify math below */
            /* Stop 1 before the end; we check the SUCCESS below */
            if (_validate_inner(code + 1, code + skip - 2, groups))
                FAIL;
            code += skip - 2;
            GET_OP;
            if (op != SRE_OP_SUCCESS)
                FAIL;
            break;

        case SRE_OP_JUMP:
            if (code + 1 != end)
                FAIL;
            VTRACE(("JUMP: %d\n", __LINE__));
            return 1;

        default:
            FAIL;
        }
    }

    VTRACE(("okay\n"));
    return 0;
}

static int _validate_outer(SRE_CODE *code, SRE_CODE *end, Py_ssize_t groups)
{
    const unsigned depth = 0;
    if (groups < 0 || (size_t)groups > SRE_MAXGROUPS || code >= end || end[-1] != SRE_OP_SUCCESS)
        FAIL;
    return _validate_inner(code, end - 1, groups);
}

#undef _validate_inner
int cxx_sre_validate(const uint32_t *code, size_t count, size_t groups)
{
    if (!code || !count || groups > SRE_MAXGROUPS)
        return 0;
    return _validate_outer((SRE_CODE *)code, (SRE_CODE *)code + count, (Py_ssize_t)groups) == 0;
}
int cxx_sre_run(const uint32_t *code, size_t count, const uint32_t *text, size_t length,
                size_t groups, ptrdiff_t pos, ptrdiff_t endpos, int search, int fullmatch,
                int must_advance, ptrdiff_t *marks, int *lastindex, int (*poll)(void *),
                void *context)
{
    static const uint32_t empty = 0;
    SRE_STATE state = {0};
    Py_ssize_t status;
    if (!count || groups > SRE_MAXGROUPS || pos < 0 || endpos < 0 || (size_t)pos > length ||
        (size_t)endpos > length)
        return SRE_ERROR_STATE;
    if (!text)
        text = &empty;
    for (size_t i = 0; i < 2 * (groups + 1); ++i)
        marks[i] = -1;
    *lastindex = -1;
    state.beginning = text;
    state.start = state.ptr = text + pos;
    state.end = text + endpos;
    state.match_all = fullmatch;
    state.must_advance = must_advance;
    state.lastmark = state.lastindex = -1;
    state.poll = poll;
    state.context = context;
    state.mark = calloc(groups ? 2 * groups : 1, sizeof(void *));
    if (!state.mark)
        return SRE_ERROR_MEMORY;
    status = search ? sre_ucs4_search(&state, (SRE_CODE *)code) : sre_ucs4_match(&state, code, 1);
    if (status > 0) {
        marks[0] = (const uint32_t *)state.start - text;
        marks[1] = (const uint32_t *)state.ptr - text;
        *lastindex = state.lastindex;
        for (size_t i = 0; i < groups; ++i) {
            const size_t j = 2 * i;
            if ((int)j < state.lastmark && state.mark[j] && state.mark[j + 1]) {
                marks[j + 2] = (const uint32_t *)state.mark[j] - text;
                marks[j + 3] = (const uint32_t *)state.mark[j + 1] - text;
            }
        }
    }
    data_stack_dealloc(&state);
    repeat_pool_clear(&state);
    free(state.mark);
    return (int)status;
}
