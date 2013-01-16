#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mapper_internal.h"

#define STACK_SIZE 256
#ifdef DEBUG
#define TRACING 1 /* Set non-zero to see trace during parse & eval. */
#else
#define TRACING 1
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
} op_t;

static unsigned int mapper_op_precedence[] = {
    11, // OP_LOGICAL_NOT,
    10, // OP_MULTIPLY,
    10, // OP_DIVIDE,
    10, // OP_MODULO,
    9,  // OP_ADD,
    9,  // OP_SUBTRACT,
    8,  // OP_LEFT_BIT_SHIFT,
    8,  // OP_RIGHT_BIT_SHIFT,
    7,  // OP_IS_GREATER_THAN,
    7,  // OP_IS_GREATER_THAN_OR_EQUAL,
    7,  // OP_IS_LESS_THAN,
    7,  // OP_IS_LESS_THAN_OR_EQUAL,
    6,  // OP_IS_EQUAL,
    6,  // OP_IS_NOT_EQUAL,
    5,  // OP_BITWISE_AND,
    4,  // OP_BITWISE_XOR,
    3,  // OP_BITWISE_OR,
    2,  // OP_LOGICAL_AND,
    1,  // OP_LOGICAL_OR,
    0,  // OP_ASSIGNMENT,
};

typedef float func_float_arity0();
typedef float func_float_arity1(float);
typedef float func_float_arity2(float,float);

typedef struct _token {
    enum {
        TOK_FLOAT,
        TOK_INT32,
        TOK_DOUBLE,
        TOK_TT,
        TOK_OP,
        TOK_OP_INT32,
        TOK_OP_FLOAT,
        TOK_OP_DOUBLE,
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
    } type;
    union {
        float f;
        int i;
        double d;
        char var;
        op_t op;
        expr_func_t func;
    };
    char datatype;
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
    printf("expr_lex:%s\n", *str);
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
            tok->type = TOK_INT32;
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
        tok->type = TOK_OP;
        tok->op = OP_ADD;
        ++*str;
        return 0;
    case '-':
        tok->type = TOK_OP;
        tok->op = OP_SUBTRACT;
        ++*str;
        return 0;
    case '/':
        tok->type = TOK_OP;
        tok->op = OP_DIVIDE;
        ++*str;
        return 0;
    case '*':
        tok->type = TOK_OP;
        tok->op = OP_MULTIPLY;
        ++*str;
        return 0;
    case '%':
        tok->type = TOK_OP;
        tok->op = OP_MODULO;
        ++*str;
        return 0;
    case '=':
        // could be '=', '=='
        tok->type = TOK_OP;
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
        tok->type = TOK_OP;
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
        tok->type = TOK_OP;
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
        tok->type = TOK_OP;
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
        tok->type = TOK_OP;
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
        tok->type = TOK_OP;
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
        tok->type = TOK_OP;
        tok->op = OP_BITWISE_XOR;
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
void printop(op_t op)
{
    switch (op) {
    case OP_BITWISE_AND:              printf("&");            break;
    case OP_BITWISE_OR:               printf("|");            break;
    case OP_BITWISE_XOR:              printf("^");            break;
    case OP_DIVIDE:                   printf("/");            break;
    case OP_ASSIGNMENT:               printf("=");            break;
    case OP_IS_EQUAL:                 printf("==");           break;
    case OP_IS_GREATER_THAN:          printf(">");            break;
    case OP_IS_GREATER_THAN_OR_EQUAL: printf(">=");           break;
    case OP_IS_LESS_THAN:             printf("<");            break;
    case OP_IS_LESS_THAN_OR_EQUAL:    printf("<=");           break;
    case OP_IS_NOT_EQUAL:             printf("!=");           break;
    case OP_LEFT_BIT_SHIFT:           printf("<<");           break;
    case OP_LOGICAL_AND:              printf("&&");           break;
    case OP_LOGICAL_OR:               printf("||");           break;
    case OP_LOGICAL_NOT:              printf("!");            break;
    case OP_SUBTRACT:                 printf("-");            break;
    case OP_MODULO:                   printf("%%");           break;
    case OP_MULTIPLY:                 printf("*");            break;
    case OP_ADD:                      printf("+");            break;
    case OP_RIGHT_BIT_SHIFT:          printf(">>");           break;
    default:                          printf("(unknown op)"); break;
    }
}

void printtoken(mapper_token_t tok)
{
    switch (tok.type) {
    case TOK_FLOAT:        printf("%f", tok.f);          break;
    case TOK_DOUBLE:       printf("%f", tok.d);          break;
    case TOK_INT:          printf("%d", tok.i);          break;
    case TOK_OP:           printop(tok.op);              break;
    case TOK_OPEN_PAREN:   printf("(");                  break;
    case TOK_CLOSE_PAREN:  printf(")");                  break;
    case TOK_VAR:          printf("VAR(%c){%d}[%d]",
                                  tok.var,
                                  tok.history_index,
                                  tok.vector_index);    break;
    case TOK_FUNC:         printf("FUNC(%s)",
                                  function_table[tok.func].name);
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
    printstack(e->start, e->length);
}

#endif

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

   // int oldest_input = 0, oldest_output = 0;

   // const char *error_message = 0;

    mapper_token_t tok;

    while (*s && !expr_lex(&s, &tok)) {
        switch (tok.type) {
            case TOK_INT32:
            case TOK_FLOAT:
            case TOK_DOUBLE:
            case TOK_VAR:
                // push to output stack
                memcpy(outstack + (++outstack_index), &tok, sizeof(mapper_token_t));
                break;
            case TOK_FUNC:
            case TOK_OPEN_PAREN:
                // push to operator stack
                memcpy(opstack + (++opstack_index), &tok, sizeof(mapper_token_t));
                break;
            case TOK_COMMA:
            case TOK_CLOSE_PAREN:
                // pop from operator stack to output until left parenthesis found
                while (opstack_index >= 0 && opstack[opstack_index].type != TOK_OPEN_PAREN) {
                    memcpy(outstack + (++outstack_index),
                           opstack + opstack_index,
                           sizeof(mapper_token_t));
                    
                    opstack_index--;
                }
                if (opstack_index < 0) {
                    printf("error: unmatched parentheses or misplaced comma!\n");
                    return 0;
                }
                // remove left parenthesis from operator stack
                if (tok.type == TOK_CLOSE_PAREN) {
                    opstack_index--;
                    if (opstack[opstack_index].type == TOK_FUNC) {
                        // if stack[top] is tok_func, pop to output
                        memcpy(outstack + (++outstack_index),
                               opstack + opstack_index,
                               sizeof(mapper_token_t));
                        opstack_index--;
                    }
                }
                break;
            case TOK_OP:
                // check precedence of operators on stack
                while (opstack_index >= 0 && opstack[opstack_index].type == TOK_OP
                       && mapper_op_precedence[opstack[opstack_index].op] >=
                       mapper_op_precedence[tok.op]) {
                    memcpy(outstack + (++outstack_index),
                           opstack + opstack_index,
                           sizeof(mapper_token_t));
                    opstack_index--;
                }
                // push to operator stack
                memcpy(opstack + (++opstack_index), &tok, sizeof(mapper_token_t));
                break;
            case TOK_OPEN_SQUARE:
                if (outstack[outstack_index].type != TOK_VAR) {
                    printf("error: misplaced brackets\n");
                }
                if (expr_lex(&s, &tok))
                    return 0;
                if (tok.type != TOK_INT32) {
                    // we will only allow integer indices
                    printf("error: non-integer vector index.\n");
                    return 0;
                }
                else if (tok.i > 0) {
                    // for now we will disable vector indexing
                    printf("error: vector indexing disabled for now.\n");
                    return 0;
                }
                outstack[outstack_index].vector_index = tok.i;
                if (expr_lex(&s, &tok) || tok.type != TOK_CLOSE_SQUARE) {
                    printf("error: unmatched bracket.\n");
                    return 0;
                }
                break;
            case TOK_OPEN_CURLY:
                if (outstack[outstack_index].type != TOK_VAR) {
                    printf("error: misplaced brackets\n");
                }
                if (expr_lex(&s, &tok))
                    return 0;
                // we will only allow negative integer indices
                if (tok.type != TOK_OP || tok.op != OP_SUBTRACT) {
                    printf("error: non-negative history index.\n");
                    return 0;
                }
                if (expr_lex(&s, &tok) || tok.type != TOK_INT32) {
                    printf("error: non-integer history index.\n");
                    return 0;
                }
                outstack[outstack_index].history_index = tok.i * -1;
                if (expr_lex(&s, &tok) || tok.type != TOK_CLOSE_CURLY) {
                    printf("error: unmatched brace.\n");
                    return 0;
                }
                break;
            default:
                printf("unknown token!\n");
                return 0;
                break;
        }
        printf("OUTPUT:");
        printstack(outstack, outstack_index);
        printf("OPERATOR:");
        printstack(opstack, opstack_index);
    }

    // finish popping operators to output, check for unbalanced parentheses
    while (opstack_index >= 0) {
        if (opstack[opstack_index].type == TOK_OPEN_PAREN) {
            printf("error: unmatched parentheses or misplaced comma!\n");
            return 0;
        }
        memcpy(outstack + (++outstack_index),
               opstack + opstack_index,
               sizeof(mapper_token_t));
        opstack_index--;
    }
    outstack[++outstack_index].type = TOK_END;

    printf("OUTPUT:");
    printstack(outstack, outstack_index);
    
    // todo: allocate and return expression
    blarg
    
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
    mapper_signal_value_t stack[to->length][expr->length];

    int i = 0, top = -1;
    mapper_token_t *tok = expr->start;
    /* Increment index position of output data structure. */
    to->position = (to->position + 1) % to->size;
    while (tok->type != TOK_END) {
        switch (tok->type) {
        case TOK_INT32:
            ++top;
            for (i = 0; i < to->length; i++)
                stack[i][top].i32 = tok->i;
            break;
        case TOK_FLOAT:
            ++top;
            for (i = 0; i < to->length; i++)
                stack[i][top].f = tok->f;
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
        case TOK_OP_FLOAT:
            --top;
            for (i = 0; i < to->length; i++) {
                trace_eval("%f %c %f = ", stack[i][top].f, tok->op, stack[i][top + 1].f);
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
            }
            break;
        case TOK_OP_INT32:
            --top;
            for (i = 0; i < to->length; i++) {
                trace_eval("%d %c %d = ", stack[i][top].i32, tok->op, stack[i][top + 1].i32);
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
                    trace_eval("%s(%f,%f) = ", function_table[tok->func].name, stack[i][top].f, stack[i][top + 1].f);
                    stack[i][top].f = ((func_float_arity2*)function_table[tok->func].func)(stack[i][top].f, stack[i][top + 1].f);
                    trace_eval("%f\n", stack[i][top].f);
                }
                break;
            default: goto error;
            }
            break;
        default: goto error;
        }
        tok++;
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
