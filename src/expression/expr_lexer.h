#ifndef __MPR_EXPR_LEXER_H__
#define __MPR_EXPR_LEXER_H__

#include <ctype.h>
#include "expr_token.h"

/* TODO: provide feedback to user */
#define lex_error trace

static expr_op_t op_lookup(etoken t, const char *s)
{
    int i;
    for (i = 0; i < OP_IF_THEN_ELSE; i++) {
        const char *name = op_tbl[i].name;
        int len = strlen(name);
        if (strncmp(s, name, len) == 0) {
            /* check for augmented assignment operator */
            if (op_tbl[i].assignment && ('=' == s[len])) {
                t->toktype = TOK_ASSIGN_OP;
                t->var.op_idx = i;
                return len + 1;
            }
            else {
                t->toktype = op_tbl[i].arity > 1 ? TOK_OP : TOK_OP_UNARY;
                t->op.idx = i;
                return len;
            }
        }
    }
    return 0;
}

/* TODO: move to expr_variable.h */
static int var_lookup(etoken tok, const char *s, int len)
{
    if ('t' != *s || '_' != *(s+1))
        tok->toktype = TOK_VAR;
    else if (len > 2) {
        tok->toktype = TOK_TT;
        s += 2;
        len -= 2;
    }
    if (3 == len && 0 == strncmp(s, "now", 3)) {
        tok->var.idx = VAR_NOW;
        tok->toktype = TOK_TT;
        return 0;
    }
    if (4 == len && 0 == strncmp(s, "next", 4)) {
        tok->var.idx = VAR_NEXT;
        tok->toktype = TOK_TT;
        return 0;
    }
    tok->var.idx = VAR_UNKNOWN;
    if (1 != len)
        return 0;
    if (*s == 'y')
        tok->var.idx = VAR_Y;
    else if ('x' == *s) {
        if ('$' == *(s+1)) {
            if ('$' == *(s+2)) {
                tok->var.idx = VAR_X_NEWEST;
                return 2;
            }
            else if (isdigit(*(s+2))) {
                /* literal input signal index */
                int num_digits = 1;
                while (isdigit(*s+1+num_digits))
                    ++num_digits;
                tok->var.idx = VAR_X + atoi(s+2);
                return num_digits + 1;
            }
            else
                tok->var.idx = VAR_X;
        }
        else
            tok->var.idx = VAR_X;
    }
    return 0;
}

static int expr_lex(const char *str, int idx, etoken tok)
{
    int n=idx, i=idx;
    char c = str[idx];
    int integer_found = 0;
    tok->gen.datatype = MPR_INT32;
    tok->gen.casttype = 0;
    tok->gen.vec_len = 1;
    tok->var.vec_idx = 0;
    tok->gen.flags = 0;

    if (c==0) {
        tok->toktype = TOK_END;
        return idx;
    }

#if TRACE_PARSE
        printf("________________________\nlexing string '%s'\n", str + idx);
#endif

  again:

    i = idx;
    if (isdigit(c)) {
        do {
            c = str[++idx];
        } while (c && isdigit(c));
        n = atoi(str+i);
        integer_found = 1;
        if (c!='.' && c!='e') {
            etoken_set_int32(tok, n);
            return idx;
        }
    }

    switch (c) {
    case '.':
        c = str[++idx];
        if (!isdigit(c) && (c != 'e' || (c && isalpha(str[idx+1])))) {
            if (integer_found) {
                etoken_set_flt(tok, (float)n);
                return idx;
            }
            while (c && (isalpha(c) || isdigit(c)))
                c = str[++idx];
            ++i;
            if ((tok->fn.idx = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN) {
                tok->toktype = TOK_VFN_DOT;
                return idx;
            }
            else if ((tok->fn.idx = rfn_lookup(str+i, idx-i)) != RFN_UNKNOWN) {
                tok->toktype = TOK_RFN;
                return idx;
            }
            else if ((tok->fn.idx = fn_lookup(str+i, idx-i)) != FN_UNKNOWN) {
                tok->toktype = TOK_FN_DOT;
                return idx;
            }
            else
                break;
        }
        do {
            c = str[++idx];
        } while (c && isdigit(c));
        if (c != 'e') {
            etoken_set_flt(tok, atof(str+i));
            return idx;
        }
        /* do not break, continue to next case 'e' */
    case 'e':
        if (!integer_found) {
            while (c && (isalpha(c) || isdigit(c) || c == '_'))
                c = str[++idx];
            if ((tok->fn.idx = fn_lookup(str+i, idx-i)) != FN_UNKNOWN)
                tok->toktype = TOK_FN;
            else if ((tok->fn.idx = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN)
                tok->toktype = TOK_VFN;
            else if ((tok->lit.flags = const_lookup(str+i, idx-i))) {
                tok->toktype = TOK_LITERAL;
                tok->gen.datatype = MPR_FLT;
            }
            else
                idx += var_lookup(tok, str+i, idx-i);
            return idx;
        }
        c = str[++idx];
        if (c!='-' && c!='+' && !isdigit(c)) {
            lex_error("Incomplete scientific notation `%s'.\n", str+i);
            break;
        }
        if (c=='-' || c=='+')
            c = str[++idx];
        while (c && isdigit(c))
            c = str[++idx];
        etoken_set_dbl(tok, atof(str+i));
        return idx;
    case '-':
        /* handle '-' separately (for now) since it could be part of an arrow */
        /* could be either subtraction, negation, or lambda */
        switch (str[++idx]) {
            case '>':
                tok->toktype = TOK_LAMBDA;
                return idx + 1;
            case '-':
                tok->toktype = TOK_OP_UNARY;
                tok->op.idx = OP_DECREMENT_PRE;
                return idx + 1;
            case '=':
                tok->toktype = TOK_ASSIGN_OP;
                tok->var.op_idx = OP_SUBTRACT;
                return idx + 1;
            default:
                break;
        }
        i = idx - 2;
        /* back up one character */
        while (i && strchr(" \t\r\n", str[i]))
            --i;
        if (isalpha(str[i]) || isdigit(str[i]) || strchr(")]}$", str[i])) {
            etoken_set_op(tok, OP_SUBTRACT);
        }
        else
            tok->toktype = TOK_NEGATE;
        return idx;
    case '(':
        tok->toktype = TOK_OPEN_PAREN;
        return ++idx;
    case ')':
        tok->toktype = TOK_CLOSE_PAREN;
        return ++idx;
    case '[':
        tok->toktype = TOK_OPEN_SQUARE;
        return ++idx;
    case ']':
        tok->toktype = TOK_CLOSE_SQUARE;
        return ++idx;
    case '{':
        tok->toktype = TOK_OPEN_CURLY;
        return ++idx;
    case '}':
        tok->toktype = TOK_CLOSE_CURLY;
        return ++idx;
    case '$':
        tok->toktype = TOK_DOLLAR;
        return ++idx;
    case '#':
        tok->toktype = TOK_HASH;
        return ++idx;
    case ' ':
    case '\t':
    case '\r':
    case '\n':
        c = str[++idx];
        goto again;
    case ',':
        tok->toktype = TOK_COMMA;
        return ++idx;
    case ':':
        tok->toktype = TOK_COLON;
        return ++idx;
    case ';':
        tok->toktype = TOK_SEMICOLON;
        return ++idx;
    case '_':
        tok->toktype = TOK_MUTED;
        return ++idx;
    default:
        if (!isalpha(c)) {
            int len = op_lookup(tok, str+i);
            if (len) {
                return idx + len;
            }
            else if ('=' == c) {
                tok->toktype = TOK_ASSIGN;
                return idx + 1;
            }
            else {
                lex_error("unknown character '%c' in lexer\n", c);
                break;
            }
        }
        while (c && (isalpha(c) || isdigit(c) || c == '_'))
            c = str[++idx];
        if ((tok->fn.idx = fn_lookup(str+i, idx-i)) != FN_UNKNOWN)
            tok->toktype = TOK_FN;
        else if ((tok->fn.idx = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN)
            tok->toktype = TOK_VFN;
        else if ((tok->lit.flags = const_lookup(str+i, idx-i))) {
            tok->toktype = TOK_LITERAL;
            tok->gen.datatype = MPR_FLT;
        }
        else
            idx += var_lookup(tok, str+i, idx-i);
        return idx;
    }
    return 0;
}

#endif /* #ifndef __MPR_EXPR_LEXER_H__ */
