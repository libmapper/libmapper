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

typedef enum {
    OP_LOGICAL_NOT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_ADD,
    OP_SUBTRACT,
    OP_LEFT_BIT_SHIFT,
    OP_RIGHT_BIT_SHIFT,
    OP_IS_GREATER_THAN,
    OP_IS_GREATER_THAN_OR_EQUAL,
    OP_IS_LESS_THAN,
    OP_IS_LESS_THAN_OR_EQUAL,
    OP_IS_EQUAL,
    OP_IS_NOT_EQUAL,
    OP_BITWISE_AND,
    OP_BITWISE_XOR,
    OP_BITWISE_OR,
    OP_LOGICAL_AND,
    OP_LOGICAL_OR,
    OP_ASSIGNMENT,
} expr_op_t;

static struct {
    const char *name;
    unsigned int arity;
    unsigned int precedence;
} op_table[] = {
    { "!",  1,  11 },
    { "*",  2,  10 },
    { "/",  2,  10 },
    { "%",  2,  10 },
    { "+",  2,   9 },
    { "-",  2,   9 },
    { "<<", 2,   8 },
    { ">>", 2,   8 },
    { ">",  2,   7 },
    { ">=", 2,   7 },
    { "<",  2,   7 },
    { "<=", 2,   7 },
    { "==", 2,   6 },
    { "!=", 2,   6 },
    { "&",  2,   5 },
    { "^",  2,   4 },
    { "|",  2,   3 },
    { "&&", 2,   2 },
    { "||", 2,   1 },
    { "=",  2,   0 },
};

typedef float func_float_arity0();
typedef float func_float_arity1(float);
typedef float func_float_arity2(float,float);

typedef struct _token {
    enum {
        TOK_CONST,
        TOK_OP,
        TOK_VAR,
        TOK_FUNC,
        TOK_OPEN_PAREN,
        TOK_CLOSE_PAREN,
        TOK_OPEN_SQUARE,
        TOK_CLOSE_SQUARE,
        TOK_OPEN_CURLY,
        TOK_CLOSE_CURLY,
        TOK_COMMA,
        TOK_QUESTION,
        TOK_COLON,
        TOK_END,
    } toktype;
    union {
        float f;
        int i;
        double d;
        char var;
        expr_op_t op;
        expr_func_t func;
    };
    char datatype;
    char casttype;
    char history_index;
    char vector_index;
} mapper_token_t, *mapper_token;

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
    tok->casttype = 0;
    int n=0;
    char c = **str;
    const char *s = *str;
    int integer_found = 0;

    if (c==0) {
        tok->toktype = TOK_END;
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
            tok->toktype = TOK_CONST;
            tok->datatype = 'i';
            return 0;
        }
    }

    switch (c) {
    case '.':
        c = (*(++*str));
        if (!isdigit(c) && c!='e' && integer_found) {
            tok->toktype = TOK_CONST;
            tok->f = (float)n;
            tok->datatype = 'f';
            return 0;
        }
        if (!isdigit(c) && c!='e')
            break;
        do {
            c = (*(++*str));
        } while (c && isdigit(c));
        if (c!='e') {
            tok->f = atof(s);
            tok->toktype = TOK_CONST;
            tok->datatype = 'f';
            return 0;
        }
    case 'e':
        if (!integer_found) {
            s = *str;
            while (c && (isalpha(c) || isdigit(c)))
                c = (*(++*str));
            tok->toktype = TOK_FUNC;
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
        tok->toktype = TOK_CONST;
        tok->datatype = 'f';
        tok->f = atof(s);
        return 0;
    case '+':
        tok->toktype = TOK_OP;
        tok->op = OP_ADD;
        ++*str;
        return 0;
    case '-':
        tok->toktype = TOK_OP;
        tok->op = OP_SUBTRACT;
        ++*str;
        return 0;
    case '/':
        tok->toktype = TOK_OP;
        tok->op = OP_DIVIDE;
        ++*str;
        return 0;
    case '*':
        tok->toktype = TOK_OP;
        tok->op = OP_MULTIPLY;
        ++*str;
        return 0;
    case '%':
        tok->toktype = TOK_OP;
        tok->op = OP_MODULO;
        ++*str;
        return 0;
    case '=':
        // could be '=', '=='
        tok->toktype = TOK_OP;
        tok->op = OP_ASSIGNMENT;
        ++*str;
        c = **str;
        if (c == '=') {
            tok->op = OP_IS_EQUAL;
            ++*str;
        }
        return 0;
    case '<':
        // could be '<', '<=', '<<'
        tok->toktype = TOK_OP;
        tok->op = OP_IS_LESS_THAN;
        ++*str;
        c = **str;
        if (c == '=') {
            tok->op = OP_IS_LESS_THAN_OR_EQUAL;
            ++*str;
        }
        else if (c == '<') {
            tok->op = OP_LEFT_BIT_SHIFT;
            ++*str;
        }
        return 0;
    case '>':
        // could be '>', '>=', '>>'
        tok->toktype = TOK_OP;
        tok->op = OP_IS_GREATER_THAN;
        ++*str;
        c = **str;
        if (c == '=') {
            tok->op = OP_IS_GREATER_THAN_OR_EQUAL;
            ++*str;
        }
        else if (c == '>') {
            tok->op = OP_RIGHT_BIT_SHIFT;
            ++*str;
        }
        return 0;
    case '!':
        // could be '!', '!='
        // TODO: handle factorial and logical negation cases
        tok->toktype = TOK_OP;
        tok->op = OP_LOGICAL_NOT;
        ++*str;
        c = **str;
        if (c == '=') {
            tok->op = OP_IS_NOT_EQUAL;
            ++*str;
        }
        return 0;
    case '&':
        // could be '&', '&&'
        tok->toktype = TOK_OP;
        tok->op = OP_BITWISE_AND;
        ++*str;
        c = **str;
        if (c == '&') {
            tok->op = OP_LOGICAL_AND;
            ++*str;
        }
        return 0;
    case '|':
        // could be '|', '||'
        tok->toktype = TOK_OP;
        tok->op = OP_BITWISE_OR;
        ++*str;
        c = **str;
        if (c == '|') {
            tok->op = OP_LOGICAL_OR;
            ++*str;
        }
        return 0;
    case '^':
        // bitwise XOR
        tok->toktype = TOK_OP;
        tok->op = OP_BITWISE_XOR;
        ++*str;
        return 0;
    case '(':
        tok->toktype = TOK_OPEN_PAREN;
        ++*str;
        return 0;
    case ')':
        tok->toktype = TOK_CLOSE_PAREN;
        ++*str;
        return 0;
    case 'x':
    case 'y':
        tok->toktype = TOK_VAR;
        tok->var = c;
        ++*str;
        return 0;
    case '[':
        tok->toktype = TOK_OPEN_SQUARE;
        ++*str;
        return 0;
    case ']':
        tok->toktype = TOK_CLOSE_SQUARE;
        ++*str;
        return 0;
    case '{':
        tok->toktype = TOK_OPEN_CURLY;
        ++*str;
        return 0;
    case '}':
        tok->toktype = TOK_CLOSE_CURLY;
        ++*str;
        return 0;
    case ' ':
    case '\t':
    case '\r':
    case '\n':
        c = (*(++*str));
        goto again;
    case ',':
        tok->toktype = TOK_COMMA;
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
        tok->toktype = TOK_FUNC;
        tok->func = function_lookup(s, *str-s);
        return 0;
    }

    return 1;
}

struct _mapper_expr
{
    mapper_token start;
    int length;
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
    NOT,
    VAR_RIGHT,
    VAR_VECTINDEX,
    VAR_HISTINDEX,
    CLOSE_VECTINDEX,
    CLOSE_HISTINDEX,
    OPEN_PAREN,
    CLOSE_PAREN,
    COMMA,
    QUESTION,
    COLON,
    END,
} state_t;

void mapper_expr_free(mapper_expr expr)
{
    free(expr);
}

#ifdef DEBUG

void printtoken(mapper_token_t tok)
{
    switch (tok.toktype) {
    case TOK_CONST:
        switch (tok.datatype) {
            case 'f':      printf("%ff", tok.f);          break;
            case 'd':      printf("%fd", tok.d);          break;
            case 'i':      printf("%di", tok.i);          break;
        }                                                 break;
    case TOK_OP:           printf("%s%c",
                                  op_table[tok.op].name,
                                  tok.datatype);          break;
    case TOK_OPEN_PAREN:   printf("(");                   break;
    case TOK_CLOSE_PAREN:  printf(")");                   break;
    case TOK_VAR:          printf("VAR(%c%c){%d}[%d]",
                                  tok.var, tok.datatype,
                                  tok.history_index,
                                  tok.vector_index);      break;
    case TOK_FUNC:         printf("FUNC(%s%c)",
                                  function_table[tok.func].name,
                                  tok.datatype);
        break;
    case TOK_COMMA:        printf(",");                   break;
    case TOK_QUESTION:     printf("?");                   break;
    case TOK_COLON:        printf(":");                   break;
    case TOK_END:          printf("END");                 break;
    default:               printf("(unknown token)");     break;
    }
}

void printstack(mapper_token_t *stack, int top)
{
    int i;
    printf("Stack: ");
    for (i=0; i<=top; i++) {
        printtoken(stack[i]);
        printf(" ");
    }
    printf("\n");
}

void printexpr(const char *s, mapper_expr e)
{
    printf("%s\n", s);
    printstack(e->start, e->length-1);
}

#endif


void promote_datatype(mapper_token_t *tok, char type)
{
    if (tok->datatype == type)
        return;

    if (tok->toktype == TOK_CONST) {
        // constants can be cast immediately
        if (tok->datatype == 'i') {
            if (type == 'f') {
                tok->f = (float)tok->i;
                tok->datatype = 'f';
            }
            else if (type == 'd') {
                tok->d = (double)tok->i;
                tok->datatype = 'd';
            }
        }
        else if (tok->datatype == 'f') {
            if (type == 'd') {
                tok->d = (double)tok->f;
                tok->datatype = 'd';
            }
        }
    }
    else if (tok->toktype == TOK_VAR) {
        // we may need to cast variable at runtime
        if (tok->datatype == 'i' || type == 'd')
            tok->casttype = type;
    }
    else if (tok->datatype == 'i' || type == 'd') {
        tok->datatype = type;
    }
}

int add_typecast(mapper_token_t *stack, int top)
{
    int i, arity, depth, can_precompute = 1;
    if (top < 1) return top;

    // walk down stack distance of arity
    if (stack[top].toktype == TOK_OP) {
        if (stack[top].op == OP_LOGICAL_NOT)
            arity = 1;
        else
            arity = 2;
    }
    else if (stack[top].toktype == TOK_FUNC) {
        arity = function_table[stack[top].func].arity;
    }
    else
        return top;

    if (arity) {
        // find operator or function inputs
        stack[top].datatype = stack[top-1].datatype;
        i = top;
        depth = arity;
        while (--i >= 0) {
            depth--;
            if (stack[i].toktype != TOK_CONST)
                can_precompute = 0;
            if (depth == 0) {
                promote_datatype(&stack[top], stack[i].datatype);
                promote_datatype(&stack[i], stack[top].datatype);
                break;
            }
            if (stack[i].toktype == TOK_OP)
                depth += op_table[stack[i].op].arity;
            else if (stack[i].toktype == TOK_FUNC)
                depth += function_table[stack[i].func].arity;
        }
        promote_datatype(&stack[top-1], stack[top].datatype);

        if (depth) {
            printf("error: malformed expression\n");
            return top;
        }
    }
    else {
        stack[top].datatype = 'f';
    }

    // if stack within bounds of arity was only constants, we're ok to compute
    if (!can_precompute)
        return top;

    struct _mapper_expr e;
    e.start = &stack[top-arity];
    e.length = arity+1;
    mapper_signal_history_t h;
    mapper_signal_value_t v;
    h.type = stack[top].datatype;
    h.value = &v;
    h.position = -1;
    h.length = 1;
    h.size = 1;
    if (!mapper_expr_evaluate(&e, 0, &h))
        return top;

    switch (stack[top].datatype) {
        case 'f':
            stack[top-arity].f = v.f;
            break;
        case 'd':
            stack[top-arity].d = v.d;
            break;
        case 'i':
            stack[top-arity].i = v.i32;
            break;
        default:
            printf("error: no datatype!\n");
            return 0;
            break;
    }
    stack[top-arity].toktype = TOK_CONST;
    return top-arity;
}

/* Macros to help express stack operations in parser. */
#define PUSH_TO_OUTPUT(x) ( memcpy(outstack + (++outstack_index), &x, sizeof(mapper_token_t)) )
#define PUSH_TO_OPERATOR(x) ( memcpy(opstack + (++opstack_index), &x, sizeof(mapper_token_t)) )
#define POP_OPERATOR() ( opstack_index-- )
#define POP_OPERATOR_TO_OUTPUT()                                                                \
{                                                                                               \
    memcpy(outstack + (++outstack_index), opstack + opstack_index, sizeof(mapper_token_t));     \
    outstack_index = add_typecast(outstack, outstack_index);                                    \
    opstack_index--;                                                                            \
}
#define FAIL(msg) { printf("%s\n", msg); return 0; }

/*! Create a new expression by parsing the given string. */
mapper_expr mapper_expr_new_from_string(const char *str,
                                        char input_type,
                                        char output_type,
                                        int vector_size,
                                        int *input_history_size,
                                        int *output_history_size)
{
    const char *s = str;
    if (!str) return 0;

    // all expressions must start with "y=" (ignoring spaces)
    if (s[0] != 'y') return 0;
    s++;
    while (s[0] == ' ') s++;
    if (s[0] != '=') return 0;
    s++;

    mapper_token_t outstack[STACK_SIZE];
    int outstack_index = -1;
    mapper_token_t opstack[STACK_SIZE];
    int opstack_index = -1;
    int oldest_input = 0, oldest_output = 0;
    mapper_token_t tok;

    while (*s && !expr_lex(&s, &tok)) {
        switch (tok.toktype) {
            case TOK_CONST:
                // push to output stack
                memcpy(outstack + (++outstack_index), &tok, sizeof(mapper_token_t));
                break;
            case TOK_VAR:
                // set datatype
                tok.datatype = tok.var == 'x' ? input_type : output_type;
                tok.history_index = 0;
                tok.vector_index = 0;
                PUSH_TO_OUTPUT(tok);
                break;
            case TOK_FUNC:
            case TOK_OPEN_PAREN:
                tok.datatype = 'f';
                PUSH_TO_OPERATOR(tok);
                break;
            case TOK_COMMA:
            case TOK_CLOSE_PAREN:
                // pop from operator stack to output until left parenthesis found
                while (opstack_index >= 0 && opstack[opstack_index].toktype != TOK_OPEN_PAREN) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                if (opstack_index < 0)
                    {FAIL("Unmatched parentheses or misplaces comma.");}
                // remove left parenthesis from operator stack
                if (tok.toktype == TOK_CLOSE_PAREN) {
                    POP_OPERATOR();
                    if (opstack[opstack_index].toktype == TOK_FUNC) {
                        // if stack[top] is tok_func, pop to output
                        POP_OPERATOR_TO_OUTPUT();
                    }
                }
                break;
            case TOK_OP:
                // check precedence of operators on stack
                while (opstack_index >= 0 && opstack[opstack_index].toktype == TOK_OP
                       && op_table[opstack[opstack_index].op].precedence >=
                       op_table[tok.op].precedence) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                PUSH_TO_OPERATOR(tok);
                break;
            case TOK_OPEN_SQUARE:
                if (outstack[outstack_index].toktype != TOK_VAR)
                    {FAIL("Misplaced brackets.");}
                if (expr_lex(&s, &tok))
                    return 0;
                if (tok.toktype != TOK_CONST || tok.datatype != 'i')
                    {FAIL("Non-integer vector index.");}
                else if (tok.i > 0)
                    {FAIL("Vector indexing disabled for now.");}
                outstack[outstack_index].vector_index = tok.i;
                if (expr_lex(&s, &tok) || tok.toktype != TOK_CLOSE_SQUARE)
                    {FAIL("Unmatched bracket.");}
                break;
            case TOK_OPEN_CURLY:
                if (outstack[outstack_index].toktype != TOK_VAR)
                    {FAIL("Misplaced brackets.");}
                if (expr_lex(&s, &tok))
                    return 0;
                if (tok.toktype == TOK_OP && tok.op == OP_SUBTRACT) {
                    // if negative sign found, get next token
                    outstack[outstack_index].history_index = -1;
                    if (expr_lex(&s, &tok))
                        return 0;
                }
                if (tok.toktype != TOK_CONST || tok.datatype != 'i')
                    {FAIL("Non-integer history index.");}
                outstack[outstack_index].history_index *= tok.i;
                if (outstack[outstack_index].var == 'x') {
                    if (outstack[outstack_index].history_index > 0)
                        {FAIL("Input history index cannot be > 0.");}
                }
                else if (outstack[outstack_index].history_index > -1)
                    {FAIL("Output history index cannot be > -1.");}

                if (outstack[outstack_index].var == 'x' && tok.i < oldest_input)
                    oldest_input = tok.i;
                else if (tok.i < oldest_output)
                    oldest_output = tok.i;
                if (expr_lex(&s, &tok) || tok.toktype != TOK_CLOSE_CURLY)
                    {FAIL("Unmatched brace.");}
                break;
            default:
                {FAIL("Unknown token type.");}
                break;
        }
#if TRACING
        printf("OUTPUT:");
        printstack(outstack, outstack_index);
        printf("OPERATOR:");
        printstack(opstack, opstack_index);
#endif
    }

    // finish popping operators to output, check for unbalanced parentheses
    while (opstack_index >= 0) {
        if (opstack[opstack_index].toktype == TOK_OPEN_PAREN)
            {FAIL("Unmatched parentheses or misplaces comma.");}
        POP_OPERATOR_TO_OUTPUT();
    }

    mapper_expr expr = malloc(sizeof(struct _mapper_expr));
    expr->length = outstack_index + 1;
    expr->start = malloc(sizeof(struct _token)*expr->length);
    memcpy(expr->start, &outstack, sizeof(struct _token)*expr->length);
    expr->vector_size = vector_size;
    expr->input_history_size = *input_history_size = -oldest_input+1;
    expr->output_history_size = *output_history_size = -oldest_output+1;
    return expr;
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
    mapper_signal_value_t stack[to->length][expr->length];

    int i = 0, top = -1, count = 0;
    mapper_token_t *tok = expr->start;
    /* Increment index position of output data structure. */
    to->position = (to->position + 1) % to->size;
    while (count < expr->length && tok->toktype != TOK_END) {
        switch (tok->toktype) {
        case TOK_CONST:
            ++top;
            for (i = 0; i < to->length; i++)
                stack[i][top].d = tok->d;
            break;
        case TOK_VAR:
            {
                int idx;
                switch (tok->var) {
                case 'x':
                    ++top;
                    idx = ((tok->history_index + from->position
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
                    idx = ((tok->history_index + to->position
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
        case TOK_OP:
            --top;
            // promote types as necessary
            for (i = 0; i < to->length; i++) {
                if (tok->datatype == 'f') {
                    trace_eval("%f %c %f = ", stack[i][top].f,
                               op_table[tok->op].name, stack[i][top + 1].f);
                    switch (tok->op) {
                        case OP_ADD:
                                stack[i][top].f = stack[i][top].f + stack[i][top + 1].f;
                                break;
                        case OP_SUBTRACT:
                                stack[i][top].f = stack[i][top].f - stack[i][top + 1].f;
                                break;
                        case OP_MULTIPLY:
                                stack[i][top].f = stack[i][top].f * stack[i][top + 1].f;
                                break;
                        case OP_DIVIDE:
                                stack[i][top].f = stack[i][top].f / stack[i][top + 1].f;
                                break;
                        case OP_MODULO:
                                stack[i][top].f = fmod(stack[i][top].f, stack[i][top + 1].f);
                                break;
                        case OP_IS_EQUAL:
                                stack[i][top].f = stack[i][top].f == stack[i][top + 1].f;
                                break;
                        case OP_IS_NOT_EQUAL:
                                stack[i][top].f = stack[i][top].f != stack[i][top + 1].f;
                                break;
                        case OP_IS_LESS_THAN:
                                stack[i][top].f = stack[i][top].f < stack[i][top + 1].f;
                                break;
                        case OP_IS_LESS_THAN_OR_EQUAL:
                                stack[i][top].f = stack[i][top].f <= stack[i][top + 1].f;
                                break;
                        case OP_IS_GREATER_THAN:
                                stack[i][top].f = stack[i][top].f > stack[i][top + 1].f;
                                break;
                        case OP_IS_GREATER_THAN_OR_EQUAL:
                                stack[i][top].f = stack[i][top].f >= stack[i][top + 1].f;
                                break;
                        case OP_LOGICAL_AND:
                                stack[i][top].f = stack[i][top].f && stack[i][top + 1].f;
                                break;
                        case OP_LOGICAL_OR:
                                stack[i][top].f = stack[i][top].f || stack[i][top + 1].f;
                                break;
                        case OP_LOGICAL_NOT:
                                stack[i][top].f = !stack[i][top].f;
                                break;
                        default: goto error;
                    }
                    trace_eval("%f\n", stack[i][top].f);
                } else {
                    trace_eval("%d %c %d = ", stack[i][top].i32,
                               op_table[tok->op].name, stack[i][top + 1].i32);
                    switch (tok->op) {
                        case OP_ADD:
                            stack[i][top].i32 = stack[i][top].i32 + stack[i][top + 1].i32;
                            break;
                        case OP_SUBTRACT:
                            stack[i][top].i32 = stack[i][top].i32 - stack[i][top + 1].i32;
                            break;
                        case OP_MULTIPLY:
                            stack[i][top].i32 = stack[i][top].i32 * stack[i][top + 1].i32;
                            break;
                        case OP_DIVIDE:
                            stack[i][top].i32 = stack[i][top].i32 / stack[i][top + 1].i32;
                            break;
                        case OP_MODULO:
                            stack[i][top].i32 = stack[i][top].i32 % stack[i][top + 1].i32;
                            break;
                        case OP_IS_EQUAL:
                            stack[i][top].i32 = stack[i][top].i32 == stack[i][top + 1].i32;
                            break;
                        case OP_IS_NOT_EQUAL:
                            stack[i][top].i32 = stack[i][top].i32 != stack[i][top + 1].i32;
                            break;
                        case OP_IS_LESS_THAN:
                            stack[i][top].i32 = stack[i][top].i32 < stack[i][top + 1].i32;
                            break;
                        case OP_IS_LESS_THAN_OR_EQUAL:
                            stack[i][top].i32 = stack[i][top].i32 <= stack[i][top + 1].i32;
                            break;
                        case OP_IS_GREATER_THAN:
                            stack[i][top].i32 = stack[i][top].i32 > stack[i][top + 1].i32;
                            break;
                        case OP_IS_GREATER_THAN_OR_EQUAL:
                            stack[i][top].i32 = stack[i][top].i32 >= stack[i][top + 1].i32;
                            break;
                        case OP_LEFT_BIT_SHIFT:
                            stack[i][top].i32 = stack[i][top].i32 << stack[i][top + 1].i32;
                            break;
                        case OP_RIGHT_BIT_SHIFT:
                            stack[i][top].i32 = stack[i][top].i32 >> stack[i][top + 1].i32;
                            break;
                        case OP_BITWISE_AND:
                            stack[i][top].i32 = stack[i][top].i32 & stack[i][top + 1].i32;
                            break;
                        case OP_BITWISE_OR:
                            stack[i][top].i32 = stack[i][top].i32 | stack[i][top + 1].i32;
                            break;
                        case OP_BITWISE_XOR:
                            stack[i][top].i32 = stack[i][top].i32 ^ stack[i][top + 1].i32;
                            break;
                        case OP_LOGICAL_AND:
                            stack[i][top].i32 = stack[i][top].i32 && stack[i][top + 1].i32;
                            break;
                        case OP_LOGICAL_OR:
                            stack[i][top].i32 = stack[i][top].i32 || stack[i][top + 1].i32;
                            break;
                        case OP_LOGICAL_NOT:
                            ++top;
                            stack[i][top].i32 = !stack[i][top].i32;
                            break;
                        default: goto error;
                    }
                    trace_eval("%d\n", stack[i][top].i32);
                }
            }
            break;
        case TOK_FUNC:
            switch (function_table[tok->func].arity) {
            case 0:
                ++top;
                for (i = 0; i < to->length; i++) {
                    stack[i][top].f = ((func_float_arity0*)function_table[tok->func].func)();
                    trace_eval("%s = %f\n", function_table[tok->func].name,
                               stack[i][top].f);
                }
                break;
            case 1:
                for (i = 0; i < to->length; i++) {
                    trace_eval("%s(%f) = ", function_table[tok->func].name, stack[i][top].f);
                    stack[i][top].f = ((func_float_arity1*)function_table[tok->func].func)(stack[i][top].f);
                    trace_eval("%f\n", stack[i][top].f);
                }
                break;
            case 2:
                --top;
                for (i = 0; i < to->length; i++) {
                    trace_eval("%s(%f,%f) = ", function_table[tok->func].name,
                               stack[i][top].f, stack[i][top + 1].f);
                    stack[i][top].f = ((func_float_arity2*)function_table[tok->func].func)(stack[i][top].f, stack[i][top + 1].f);
                    trace_eval("%f\n", stack[i][top].f);
                }
                break;
            default: goto error;
            }
            break;
        default: goto error;
        }
        if (tok->casttype) {
            // need to cast to a different type
            if (tok->datatype == 'i') {
                if (tok->casttype == 'f') {
                    for (i = 0; i < to->length; i++) {
                        stack[i][top].f = (float)stack[i][top].i32;
                    }
                }
                else if (tok->casttype == 'd') {
                    for (i = 0; i < to->length; i++) {
                        stack[i][top].d = (double)stack[i][top].i32;
                    }
                }
            }
            else if (tok->datatype == 'f') {
                for (i = 0; i < to->length; i++) {
                    stack[i][top].d = (double)stack[i][top].f;
                }
            }
        }
        tok++;
        count++;
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
