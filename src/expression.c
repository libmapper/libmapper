#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mapper_internal.h"

#define STACK_SIZE 256
#ifdef DEBUG
#define TRACING 0 /* Set non-zero to see trace during parse & eval. */
#else
#define TRACING 0
#endif

static float minf(float x, float y)
{
    if (y < x) return y;
    else return x;
}

static float maxf(float x, float y)
{
    if (y > x) return y;
    else return x;
}

static float pif()
{
    return M_PI;
}

static float midiToHz(float x)
{
    return 440. * pow(2.0, (x - 69) / 12.0);
}

static float hzToMidi(float x)
{
    return 69. + 12. * log2(x / 440.);
}

static float uniform(float x)
{
    return rand() / (RAND_MAX + 1.0) * x;
}

typedef enum {
    FUNC_UNKNOWN=-1,
    FUNC_ABS=0,
    FUNC_ACOS,
    FUNC_ACOSH,
    FUNC_ASIN,
    FUNC_ASINH,
    FUNC_ATAN,
    FUNC_ATAN2,
    FUNC_ATANH,
    FUNC_CBRT,
    FUNC_CEIL,
    FUNC_COS,
    FUNC_COSH,
    FUNC_EXP,
    FUNC_EXP2,
    FUNC_FLOOR,
    FUNC_HYPOT,
    FUNC_HZTOMIDI,
    FUNC_LOG,
    FUNC_LOG10,
    FUNC_LOG2,
    FUNC_LOGB,
    FUNC_MAX,
    FUNC_MIDITOHZ,
    FUNC_MIN,
    FUNC_PI,
    FUNC_POW,
    FUNC_ROUND,
    FUNC_SIN,
    FUNC_SINH,
    FUNC_SQRT,
    FUNC_TAN,
    FUNC_TANH,
    FUNC_TRUNC,
    FUNC_UNIFORM,
    N_FUNCS
} expr_func_t;

static struct {
    const char *name;
    unsigned int arity;
    void *func;
} function_table[] = {
    { "abs", 1, fabsf },
    { "acos", 1, acosf },
    { "acosh", 1, acoshf },
    { "asin", 1, asinf },
    { "asinh", 1, asinhf },
    { "atan", 1, atanf },
    { "atan2", 2, atan2f },
    { "atanh", 1, atanhf },
    { "cbrt", 1, cbrtf },
    { "ceil", 1, ceilf },
    { "cos", 1, cosf },
    { "cosh", 1, coshf },
    { "exp", 1, expf },
    { "exp2", 1, exp2f },
    { "floor", 1, floorf },
    { "hypot", 2, hypotf },
    { "hzToMidi", 1, hzToMidi },
    { "log", 1, logf },
    { "log10", 1, log10f },
    { "log2", 1, log2f },
    { "logb", 1, logbf },
    { "max", 2, maxf },
    { "midiToHz", 1, midiToHz },
    { "min", 2, minf },
    { "pi", 0, pif },
    { "pow", 2, powf },
    { "round", 1, roundf },
    { "sin", 1, sinf },
    { "sinh", 1, sinhf },
    { "sqrt", 1, sqrtf },
    { "tan", 1, tanf },
    { "tanh", 1, tanhf },
    { "trunc", 1, truncf },
    { "uniform", 1, uniform },
};

typedef float func_float_arity0();
typedef float func_float_arity1(float);
typedef float func_float_arity2(float,float);

typedef struct _token {
    enum {
        TOK_FLOAT,
        TOK_INT,
        TOK_OP,
        TOK_OPEN_PAREN,
        TOK_CLOSE_PAREN,
        TOK_VAR,
        TOK_OPEN_SQUARE,
        TOK_CLOSE_SQUARE,
        TOK_OPEN_CURLY,
        TOK_CLOSE_CURLY,
        TOK_FUNC,
        TOK_COMMA,
        TOK_END,
        TOK_TOFLOAT,
        TOK_TOINT32,
    } type;
    union {
        float f;
        int i;
        char var;
        char op;
        expr_func_t func;
    };
} mapper_token_t;

static expr_func_t function_lookup(const char *s, int len)
{
    int i;
    for (i=0; i<N_FUNCS; i++) {
        if (strncmp(s, function_table[i].name, len)==0)
            return i;
    }
    return FUNC_UNKNOWN;
}

static int expr_lex(const char **str, mapper_token_t *tok)
{
    int n=0;
    char c = **str;
    const char *s = *str;
    int integer_found = 0;

    if (c==0) {
        tok->type = TOK_END;
        return 0;
    }

  again:

    if (isdigit(c)) {
        do {
            c = (*(++*str));
        } while (c && isdigit(c));
        n = atoi(s);
        integer_found = 1;
        if (c!='.' && c!='e') {
            tok->i = n;
            tok->type = TOK_INT;
            return 0;
        }
    }

    switch (c) {
    case '.':
        c = (*(++*str));
        if (!isdigit(c) && c!='e' && integer_found) {
            tok->type = TOK_FLOAT;
            tok->f = (float)n;
            return 0;
        }
        if (!isdigit(c) && c!='e')
            break;
        do {
            c = (*(++*str));
        } while (c && isdigit(c));
        if (c!='e') {
            tok->f = atof(s);
            tok->type = TOK_FLOAT;
            return 0;
        }
    case 'e':
        if (!integer_found) {
            s = *str;
            while (c && (isalpha(c) || isdigit(c)))
                c = (*(++*str));
            tok->type = TOK_FUNC;
            tok->func = function_lookup(s, *str-s);
            if (tok->func == FUNC_UNKNOWN) {
                printf("unexpected `e' outside float\n");
                break;
            }
            else
                return 0;
        }
        c = (*(++*str));
        if (c!='-' && c!='+' && !isdigit(c)) {
            printf("Incomplete scientific notation `%s'.\n",s);
            break;
        }
        if (c=='-' || c=='+')
            c = (*(++*str));
        while (c && isdigit(c))
            c = (*(++*str));
        tok->type = TOK_FLOAT;
        tok->f = atof(s);
        return 0;
    case '+':
    case '-':
    case '/':
    case '*':
    case '%':
    case '=':
        tok->type = TOK_OP;
        tok->op = c;
        ++*str;
        return 0;
    case '(':
        tok->type = TOK_OPEN_PAREN;
        ++*str;
        return 0;
    case ')':
        tok->type = TOK_CLOSE_PAREN;
        ++*str;
        return 0;
    case 'x':
    case 'y':
        tok->type = TOK_VAR;
        tok->var = c;
        ++*str;
        return 0;
    case '[':
        tok->type = TOK_OPEN_SQUARE;
        ++*str;
        return 0;
    case ']':
        tok->type = TOK_CLOSE_SQUARE;
        ++*str;
        return 0;
    case '{':
        tok->type = TOK_OPEN_CURLY;
        ++*str;
        return 0;
    case '}':
        tok->type = TOK_CLOSE_CURLY;
        ++*str;
        return 0;
    case ' ':
    case '\t':
    case '\r':
    case '\n':
        c = (*(++*str));
        goto again;
    case ',':
        tok->type = TOK_COMMA;
        ++*str;
        return 0;
    default:
        if (!isalpha(c)) {
            printf("unknown character '%c' in lexer\n", c);
            break;
        }
        s = *str;
        while (c && (isalpha(c) || isdigit(c)))
            c = (*(++*str));
        tok->type = TOK_FUNC;
        tok->func = function_lookup(s, *str-s);
        return 0;
    }

    return 1;
}

typedef struct _exprnode
{
    mapper_token_t tok;
    int is_float;
    int history_index;  // when tok.type==TOK_VAR
    int vector_index;   // when tok.type==TOK_VAR
    struct _exprnode *next;
} *exprnode;

struct _mapper_expr
{
    exprnode node;
    int vector_size;
    int input_history_size;
    int output_history_size;
};

typedef enum {
    YEQUAL_Y,
    YEQUAL_EQ,
    EXPR,
    EXPR_RIGHT,
    TERM,
    TERM_RIGHT,
    VALUE,
    NEGATE,
    VAR_RIGHT,
    VAR_VECTINDEX,
    VAR_HISTINDEX,
    CLOSE_VECTINDEX,
    CLOSE_HISTINDEX,
    OPEN_PAREN,
    CLOSE_PAREN,
    COMMA,
    END,
} state_t;

typedef struct _stack_obj
{
    union {
        state_t state;
        exprnode node;
    };
    enum {
        ST_STATE,
        ST_NODE,
    } type;
} stack_obj_t;

static exprnode exprnode_new(mapper_token_t *tok, int is_float)
{
    exprnode t = (exprnode)
        malloc(sizeof(struct _exprnode));
    t->tok = *tok;
    t->is_float = is_float;
    t->history_index = 0;
    t->vector_index = 0;
    t->next = 0;
    return t;
}

static void exprnode_free(exprnode e)
{
    while (e) {
        exprnode tmp = e;
        e = e->next;
        free(tmp);
    }
}

void mapper_expr_free(mapper_expr expr)
{
    exprnode_free(expr->node);
    free(expr);
}

#ifdef DEBUG
void printtoken(mapper_token_t *tok)
{
    switch (tok->type) {
    case TOK_FLOAT:        printf("%f", tok->f);          break;
    case TOK_INT:          printf("%d", tok->i);          break;
    case TOK_OP:           printf("%c", tok->op);         break;
    case TOK_OPEN_PAREN:   printf("(");                   break;
    case TOK_CLOSE_PAREN:  printf(")");                   break;
    case TOK_VAR:          printf("VAR(%c)", tok->var);   break;
    case TOK_OPEN_SQUARE:  printf("[");                   break;
    case TOK_CLOSE_SQUARE: printf("]");                   break;
    case TOK_OPEN_CURLY:   printf("{");                   break;
    case TOK_CLOSE_CURLY:  printf("}");                   break;
    case TOK_FUNC:         printf("FUNC(%s)",
                                  function_table[tok->func].name);
        break;
    case TOK_COMMA:        printf(",");                   break;
    case TOK_END:          printf("END");                 break;
    case TOK_TOFLOAT:      printf("(float)");             break;
    case TOK_TOINT32:      printf("(int32)");             break;
    default:               printf("(unknown token)");     break;
    }
}

void printexprnode(const char *s, exprnode list)
{
    printf("%s", s);
    while (list) {
        if (list->is_float
            && list->tok.type != TOK_FLOAT
            && list->tok.type != TOK_TOFLOAT)
            printf(".");
        printtoken(&list->tok);
        if (list->tok.type == TOK_VAR) {
            if (list->history_index < 0)
                printf("{%d}", list->history_index);
            if (list->vector_index > -1)
                printf("[%d]", list->vector_index);
        }
        list = list->next;
        if (list) printf(" ");
    }
}

void printexpr(const char *s, mapper_expr e)
{
    printexprnode(s, e->node);
}

void printstack(stack_obj_t *stack, int top)
{
    const char *state_name[] = {
        "YEQUAL_Y", "YEQUAL_EQ", "EXPR", "EXPR_RIGHT", "TERM",
        "TERM_RIGHT", "VALUE", "NEGATE", "VAR_RIGHT",
        "VAR_VECTINDEX", "VAR_HISTINDEX", "CLOSE_VECTINDEX",
        "CLOSE_HISTINDEX", "OPEN_PAREN", "CLOSE_PAREN",
        "COMMA", "END" };

    int i;
    printf("Stack: ");
    for (i=0; i<=top; i++) {
        if (stack[i].type == ST_NODE) {
            printf("[");
            printexprnode("", stack[i].node);
            printf("] ");
            continue;
        }
        printf("%s ", state_name[stack[i].state]);
    }
    printf("\n");
}
#endif

static void collapse_expr_to_left(exprnode* plhs, exprnode rhs,
                                  int constant_folding)
{
    // track whether any variable references
    int refvar = 0;
    int refrand = 0;
    int is_float = 0;

    // find trailing operator on right hand side
    exprnode rhs_last = rhs;
    if (rhs->tok.type == TOK_VAR)
        refvar = 1;
    else if (rhs->tok.type == TOK_FUNC && rhs->tok.func == FUNC_UNIFORM)
        refrand = 1;
    while (rhs_last->next) {
        rhs_last = rhs_last->next;
        if (rhs_last->tok.type == TOK_VAR)
            refvar = 1;
        else if (rhs_last->tok.type == TOK_FUNC && rhs_last->tok.func == FUNC_UNIFORM)
            refrand = 1;
    }

    // find pointer to insertion place:
    // - could be a function that needs args,
    // - otherwise assume it's before the trailing operator
    exprnode *plhs_last = plhs;
    if ((*plhs_last)->tok.type == TOK_VAR)
        refvar = 1;
    else if ((*plhs_last)->tok.type == TOK_FUNC && (*plhs_last)->tok.func == FUNC_UNIFORM)
        refrand = 1;
    while ((*plhs_last)->next) {
        if ((*plhs_last)->tok.type == TOK_VAR)
            refvar = 1;
        else if ((*plhs_last)->tok.type == TOK_FUNC && (*plhs_last)->tok.func == FUNC_UNIFORM)
            refrand = 1;
        plhs_last = &(*plhs_last)->next;
    }

    // insert float coercion if sides disagree on type
    mapper_token_t coerce;
    coerce.type = TOK_TOFLOAT;
    is_float = (*plhs_last)->is_float || rhs_last->is_float;
    if ((*plhs_last)->is_float && !rhs_last->is_float) {
        rhs_last = rhs_last->next = exprnode_new(&coerce, 1);
    } else if (!(*plhs_last)->is_float && rhs_last->is_float) {
        exprnode e = exprnode_new(&coerce, 1);
        e->next = (*plhs_last);
        (*plhs_last) = e;
        plhs_last = &e->next;
        e->next->is_float = 1;
    }

    // insert the list before the trailing op of left hand side
    rhs_last->next = (*plhs_last);
    (*plhs_last) = rhs;

    // if there were no variable references, then expression is
    // constant, so evaluate it immediately
    if (constant_folding && !refvar && !refrand) {
        struct _mapper_expr e;
        e.node = *plhs;
        mapper_signal_history_t h;
        mapper_signal_value_t v;
        h.type = is_float ? 'f' : 'i';
        h.value = &v;
        h.position = -1;
        h.length = 1;
        h.size = 1;
        mapper_expr_evaluate(&e, 0, &h);

        exprnode_free((*plhs)->next);
        (*plhs)->next = 0;
        (*plhs)->is_float = is_float;

        if (is_float) {
            (*plhs)->tok.type = TOK_FLOAT;
            (*plhs)->tok.f = v.f;
        }
        else {
            (*plhs)->tok.type = TOK_INT;
            (*plhs)->tok.i = v.i32;
        }
    }
}

/* Macros to help express stack operations in parser. */
#define PUSHSTATE(x) { top++; stack[top].state = x; stack[top].type = ST_STATE; }
#define PUSHEXPR(x, f) { top++; stack[top].node = exprnode_new(&x, f); stack[top].type = ST_NODE; }
#define POP() (top--)
#define TOPSTATE_IS(x) (stack[top].type == ST_STATE && stack[top].state == x)
#define APPEND_OP(x)                                                    \
    if (stack[top].type == ST_NODE) {                                   \
        exprnode e = stack[top].node;                                   \
        while (e->next) e = e->next;                                    \
        e->next = exprnode_new(&tok, 0);                                \
        e->next->is_float = e->is_float;                                \
    }
#define SUCCESS(x) { result = x; goto done; }
#define FAIL(msg) { error_message = msg; result = 0; goto done; }

/*! Create a new expression by parsing the given string. */
mapper_expr mapper_expr_new_from_string(const char *str,
                                        int input_is_float,
                                        int output_is_float,
                                        int vector_size,
                                        int *input_history_size,
                                        int *output_history_size)
{
    const char *s = str;
    if (!str) return 0;
    stack_obj_t stack[STACK_SIZE];
    int top = -1;
    exprnode result = 0;
    const char *error_message = 0;

    mapper_token_t tok;
    int i, next_token = 1;

    int var_allowed = 1;
    int oldest_input = 0, oldest_output = 0;

    PUSHSTATE(EXPR);
    PUSHSTATE(YEQUAL_EQ);
    PUSHSTATE(YEQUAL_Y);

    while (top >= 0) {
        if (next_token && expr_lex(&s, &tok))
            {FAIL("Error in lexical analysis.");}
        next_token = 0;

        if (stack[top].type == ST_NODE) {
            if (top==0)
                SUCCESS(stack[top].node);
            if (stack[top-1].type == ST_STATE) {
                if (top >= 2 && stack[top-2].type == ST_NODE) {
                    if (stack[top-1].type == ST_STATE
                        && (stack[top-1].state == EXPR_RIGHT
                            || stack[top-1].state == TERM_RIGHT
                            || stack[top-1].state == CLOSE_PAREN))
                    {
                        collapse_expr_to_left(&stack[top-2].node,
                                              stack[top].node, 1);
                        POP();
                    }
                    else if (stack[top-1].type == ST_STATE
                             && stack[top-1].state == CLOSE_HISTINDEX)
                    {
                        die_unless(stack[top-2].node->tok.type == TOK_VAR,
                                   "expected VAR two-down on the stack.\n");
                        die_unless(!stack[top].node->next && (stack[top].node->tok.type == TOK_INT
                                                              || stack[top].node->tok.type == TOK_FLOAT),
                                   "expected lonely INT or FLOAT expression on the stack.\n");
                        if (stack[top].node->tok.type == TOK_FLOAT)
                            stack[top-2].node->history_index = (int)stack[top].node->tok.f;
                        else
                            stack[top-2].node->history_index = stack[top].node->tok.i;

                        /* Track the oldest history reference in order
                         * to know how much buffer needs to be
                         * allocated for this expression. */
                        if (stack[top-2].node->tok.var == 'x'
                            && oldest_input > stack[top-2].node->history_index) {
                            oldest_input = stack[top-2].node->history_index;
                        }
                        else if (stack[top-2].node->tok.var == 'y'
                                 && oldest_output > stack[top-2].node->history_index) {
                            oldest_output = stack[top-2].node->history_index;
                        }

                        exprnode_free(stack[top].node);
                        POP();
                    }
                    else if (stack[top-1].type == ST_STATE
                             && stack[top-1].state == CLOSE_VECTINDEX)
                    {
                        die_unless(stack[top-2].node->tok.type == TOK_VAR,
                                   "expected VAR two-down on the stack.\n");
                        die_unless(!stack[top].node->next && (stack[top].node->tok.type == TOK_INT
                                                              || stack[top].node->tok.type == TOK_FLOAT),
                                   "expected lonely INT or FLOAT expression on the stack.\n");
                        if (stack[top].node->tok.type == TOK_FLOAT)
                            stack[top-2].node->vector_index = (int)stack[top].node->tok.f;
                        else
                            stack[top-2].node->vector_index = stack[top].node->tok.i;
                        if (stack[top-2].node->vector_index > 0)
                            {FAIL("Vector indexing not yet implemented.");}
                        if (stack[top-2].node->vector_index < 0
                            || stack[top-2].node->vector_index >= vector_size)
                            {FAIL("Vector index outside input size.");}

                        exprnode_free(stack[top].node);
                        POP();
                    }
                }
                else {
                    // swap the expression down the stack
                    stack_obj_t tmp = stack[top-1];
                    stack[top-1] = stack[top];
                    stack[top] = tmp;
                }
            }

#if TRACING
            printstack(&stack[0], top);
#endif
            continue;
        }

        switch (stack[top].state) {
        case YEQUAL_Y:
            if (tok.type == TOK_VAR && tok.var == 'y')
                POP();
            else
                {FAIL("Error in y= prefix.");}
            next_token = 1;
            break;
        case YEQUAL_EQ:
            if (tok.type == TOK_OP && tok.op == '=') {
                POP();
            } else {
                {FAIL("Error in y= prefix.");}
            }
            next_token = 1;
            break;
        case EXPR:
            POP();
            PUSHSTATE(EXPR_RIGHT);
            PUSHSTATE(TERM);
            break;
        case EXPR_RIGHT:
            if (tok.type == TOK_OP) {
                POP();
                if (tok.op == '+' || tok.op == '-') {
                    APPEND_OP(tok);
                    PUSHSTATE(EXPR);
                    next_token = 1;
                }
            }
            else POP();
            break;
        case TERM:
            POP();
            PUSHSTATE(TERM_RIGHT);
            PUSHSTATE(VALUE);
            break;
        case TERM_RIGHT:
            if (tok.type == TOK_OP) {
                POP();
                if (tok.op == '*' || tok.op == '/' || tok.op == '%') {
                    APPEND_OP(tok);
                    PUSHSTATE(TERM);
                    next_token = 1;
                }
            }
            else POP();
            break;
        case VALUE:
            if (tok.type == TOK_INT) {
                POP();
                PUSHEXPR(tok, 0);
                next_token = 1;
            } else if (tok.type == TOK_FLOAT) {
                POP();
                PUSHEXPR(tok, 1);
                next_token = 1;
            } else if (tok.type == TOK_VAR) {
                if (var_allowed) {
                    POP();
                    PUSHEXPR(tok, input_is_float);
                    PUSHSTATE(VAR_RIGHT);
                    next_token = 1;
                }
                else
                    {FAIL("Unexpected variable reference.");}
            } else if (tok.type == TOK_OPEN_PAREN) {
                POP();
                PUSHSTATE(CLOSE_PAREN);
                PUSHSTATE(EXPR);
                next_token = 1;
            } else if (tok.type == TOK_FUNC) {
                POP();
                if (tok.func == FUNC_UNKNOWN)
                    {FAIL("Unknown function.");}
                else {
                    PUSHEXPR(tok, 1);
                    int arity = function_table[tok.func].arity;
                    if (arity > 0) {
                        PUSHSTATE(CLOSE_PAREN);
                        PUSHSTATE(EXPR);
                        for (i=1; i < arity; i++) {
                            PUSHSTATE(COMMA);
                            PUSHSTATE(EXPR);
                        }
                        PUSHSTATE(OPEN_PAREN);
                    }
                    next_token = 1;
                }
            } else if (tok.type == TOK_OP && tok.op == '-') {
                POP();
                PUSHSTATE(NEGATE);
                PUSHSTATE(VALUE);
                next_token = 1;
            } else
                {FAIL("Expected value.");}
            break;
        case NEGATE:
            POP();
            // insert '0' before, and '-' after the expression.
            // set is_float according to trailing operator.
            if (stack[top].type == ST_NODE) {
                mapper_token_t t;
                t.type = TOK_INT;
                t.i = 0;
                exprnode e = exprnode_new(&t, 0);
                t.type = TOK_OP;
                t.op = '-';
                e->next = exprnode_new(&t, 0);
                collapse_expr_to_left(&e, stack[top].node, 1);
                stack[top].node = e;
            }
            else
                {FAIL("Expected to negate an expression.");}
            break;
        case VAR_RIGHT:
            if (tok.type == TOK_OPEN_SQUARE) {
                POP();
                PUSHSTATE(VAR_VECTINDEX);
            }
            else if (tok.type == TOK_OPEN_CURLY) {
                POP();
                PUSHSTATE(VAR_HISTINDEX);
            }
            else
                POP();
            break;
        case VAR_VECTINDEX:
            POP();
            if (tok.type == TOK_OPEN_SQUARE) {
                var_allowed = 0;
                PUSHSTATE(CLOSE_VECTINDEX);
                PUSHSTATE(EXPR);
                next_token = 1;
            }
            break;
        case VAR_HISTINDEX:
            POP();
            if (tok.type == TOK_OPEN_CURLY) {
                var_allowed = 0;
                PUSHSTATE(CLOSE_HISTINDEX);
                PUSHSTATE(EXPR);
                next_token = 1;
            }
            break;
        case CLOSE_VECTINDEX:
            if (tok.type == TOK_CLOSE_SQUARE) {
                var_allowed = 1;
                POP();
                PUSHSTATE(VAR_HISTINDEX);
                next_token = 1;
            }
            else
                {FAIL("Expected ']'.");}
            break;
        case CLOSE_HISTINDEX:
            if (tok.type == TOK_CLOSE_CURLY) {
                var_allowed = 1;
                POP();
                PUSHSTATE(VAR_VECTINDEX);
                next_token = 1;
            }
            else
                {FAIL("Expected '}'.");}
            break;
        case CLOSE_PAREN:
            if (tok.type == TOK_CLOSE_PAREN) {
                POP();
                next_token = 1;
                break;
            } else
                {FAIL("Expected ')'.");}
            break;
        case COMMA:
            if (tok.type == TOK_COMMA) {
                POP();
                // find previous expression on the stack
                for (i=top-1; i>=0 && stack[i].type!=ST_NODE; --i) {};
                if (i>=0) {
                    collapse_expr_to_left(&stack[i].node,
                                          stack[top].node, 0);
                    POP();
                }
                next_token = 1;
            } else
                {FAIL("Expected ','.");}
            break;
        case OPEN_PAREN:
            if (tok.type == TOK_OPEN_PAREN) {
                POP();
                next_token = 1;
            } else
                {FAIL("Expected '('.");}
            break;
        case END:
            if (tok.type == TOK_END) {
                POP();
                break;
            } else
                {FAIL("Expected END.");}
        default:
            FAIL("Unexpected parser state.");
            break;
        }

#if TRACING
        printstack(&stack[0], top);
#endif
    }

  done:
    if (!result) {
        if (error_message)
            printf("%s\n", error_message);
        goto cleanup;
    }

    // We need a limit, but this is a bit arbitrary.
    if (oldest_input < -100) {
        trace("Expression contains history references of %i\n", oldest_input);
        goto cleanup;
    }
    if (oldest_output < -100) {
        trace("Expression contains history references of %i\n", oldest_output);
        goto cleanup;
    }

    // Coerce the final output if necessary
    exprnode e = result;
    while (e->next) e = e->next;
    if (e->is_float && !output_is_float) {
        mapper_token_t coerce;
        coerce.type = TOK_TOINT32;
        e->next = exprnode_new(&coerce, 0);
        e->next->is_float = 0;
    }
    else if (!e->is_float && output_is_float) {
        mapper_token_t coerce;
        coerce.type = TOK_TOFLOAT;
        e->next = exprnode_new(&coerce, 0);
        e->next->is_float = 1;
    }

    /* Special case: if this is a vector expression, we currently do
     * not completely support it.  However, we can "fake" vector
     * operations by performing the entire expression element-wise.
     * In this case, we have to disallow any vector indexing, so here
     * we scan the compiled expression and check for it.  This will be
     * removed in a future version. */
    if (vector_size > 1) {
        e = result;
        while (e) {
            if (e->tok.type == TOK_VAR && e->vector_index > 0) {
                trace("vector indexing not yet implemented\n");
                goto cleanup;
            }
            e = e->next;
        }
    }

    mapper_expr expr = malloc(sizeof(struct _mapper_expr));
    expr->node = result;
    expr->vector_size = vector_size;
    *input_history_size = (int)ceilf(-oldest_input)+1;
    *output_history_size = (int)ceilf(-oldest_output)+1;
    return expr;

  cleanup:
    for (i=0; i<top; i++) {
        if (stack[i].type == ST_NODE)
            exprnode_free(stack[i].node);
    }
    return 0;
}

int mapper_expr_input_history_size(mapper_expr expr)
{
    return expr->input_history_size;
}

int mapper_expr_output_history_size(mapper_expr expr)
{
    return expr->output_history_size;
}

#if TRACING
#define trace_eval printf
#else
static void trace_eval(const char *s,...) {}
#endif

int mapper_expr_evaluate(mapper_expr expr,
                         mapper_signal_history_t *from,
                         mapper_signal_history_t *to)
{
    mapper_signal_value_t stack[to->length][STACK_SIZE];

    int i = 0, top = -1;
    exprnode node = expr->node;
    /* Increment index position of output data structure. */
    to->position = (to->position + 1) % to->size;
    while (node) {
            switch (node->tok.type) {
        case TOK_INT:
            ++top;
            for (i = 0; i < to->length; i++)
                stack[i][top].i32 = node->tok.i;
            break;
        case TOK_FLOAT:
            ++top;
            for (i = 0; i < to->length; i++)
                stack[i][top].f = node->tok.f;
            break;
        case TOK_VAR:
            {
                int idx;
                switch (node->tok.var) {
                case 'x':
                    ++top;
                    idx = ((node->history_index + from->position
                            + from->size) % from->size);
                    if (from->type == 'f') {
                        float *v = from->value + idx * from->length * mapper_type_size(from->type);
                        for (i = 0; i < from->length; i++)
                            stack[i][top].f = v[i];
                    }
                    else if (from->type == 'i') {
                        int *v = from->value + idx * from->length * mapper_type_size(from->type);
                        for (i = 0; i < from->length; i++)
                            stack[i][top].i32 = v[i];
                    }
                    break;
                case 'y':
                    ++top;
                    idx = ((node->history_index + to->position
                            + to->size) % to->size);
                    if (to->type == 'f') {
                        float *v = to->value + idx * to->length * mapper_type_size(to->type);
                        for (i = 0; i < to->length; i++)
                            stack[i][top].f = v[i];
                    }
                    else if (to->type == 'i') {
                        int *v = to->value + idx * to->length * mapper_type_size(to->type);
                        for (i = 0; i < to->length; i++)
                            stack[i][top].i32 = v[i];
                    }
                    break;
                default: goto error;
                }
            }
            break;
        case TOK_TOFLOAT:
            for (i = 0; i < to->length; i++)
                stack[i][top].f = (float)stack[i][top].i32;
            break;
        case TOK_TOINT32:
            for (i = 0; i < to->length; i++)
                stack[i][top].i32 = (int)stack[i][top].f;
            break;
        case TOK_OP:
            --top;
            for (i = 0; i < to->length; i++) {
                if (node->is_float) {
                    trace_eval("%f %c %f = ", stack[i][top].f, node->tok.op, stack[i][top + 1].f);
                    switch (node->tok.op) {
                    case '+': stack[i][top].f = stack[i][top].f + stack[i][top + 1].f; break;
                    case '-': stack[i][top].f = stack[i][top].f - stack[i][top + 1].f; break;
                    case '*': stack[i][top].f = stack[i][top].f * stack[i][top + 1].f; break;
                    case '/': stack[i][top].f = stack[i][top].f / stack[i][top + 1].f; break;
                    case '%': stack[i][top].f = fmod(stack[i][top].f, stack[i][top + 1].f); break;
                    default: goto error;
                    }
                    trace_eval("%f\n", stack[i][top].f);
                } else {
                    trace_eval("%d %c %d = ", stack[i][top].i32, node->tok.op, stack[i][top + 1].i32);
                    switch (node->tok.op) {
                    case '+': stack[i][top].i32 = stack[i][top].i32 + stack[i][top + 1].i32; break;
                    case '-': stack[i][top].i32 = stack[i][top].i32 - stack[i][top + 1].i32; break;
                    case '*': stack[i][top].i32 = stack[i][top].i32 * stack[i][top + 1].i32; break;
                    case '/': stack[i][top].i32 = stack[i][top].i32 / stack[i][top + 1].i32; break;
                    case '%': stack[i][top].i32 = stack[i][top].i32 % stack[i][top + 1].i32; break;
                    default: goto error;
                    }
                    trace_eval("%d\n", stack[i][top].i32);
                }
            }
            break;
        case TOK_FUNC:
            switch (function_table[node->tok.func].arity) {
            case 0:
                ++top;
                for (i = 0; i < to->length; i++) {
                    stack[i][top].f = ((func_float_arity0*)function_table[node->tok.func].func)();
                    trace_eval("%s = %f\n", function_table[node->tok.func].name,
                               stack[i][top].f);
                }
                break;
            case 1:
                for (i = 0; i < to->length; i++) {
                    trace_eval("%s(%f) = ", function_table[node->tok.func].name, stack[i][top].f);
                    stack[i][top].f = ((func_float_arity1*)function_table[node->tok.func].func)(stack[i][top].f);
                    trace_eval("%f\n", stack[i][top].f);
                }
                break;
            case 2:
                --top;
                for (i = 0; i < to->length; i++) {
                    trace_eval("%s(%f,%f) = ", function_table[node->tok.func].name, stack[i][top].f, stack[i][top + 1].f);
                    stack[i][top].f = ((func_float_arity2*)function_table[node->tok.func].func)(stack[i][top].f, stack[i][top + 1].f);
                    trace_eval("%f\n", stack[i][top].f);
                }
                break;
            default: goto error;
            }
            break;
        default: goto error;
        }
        node = node->next;
    }

    if (to->type == 'f') {
        float *v = msig_history_value_pointer(*to);
        for (i = 0; i < to->length; i++)
            v[i] = stack[i][top].f;
    }
    else if (to->type == 'i') {
        int *v = msig_history_value_pointer(*to);
        for (i = 0; i < to->length; i++)
            v[i] = stack[i][top].i32;
    }
    return 1;

  error:
    trace("Unexpected token in expression.");
    return 0;
}
