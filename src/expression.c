#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "operations.h"
#include "expression.h"

/*! Provided because strndup is not standard on all supported platforms. */
static char* strndup_(const char* s, size_t n)
{
    if (n < 0) return 0;
    char *r = (char*)malloc(n+1);
    strncpy(r, s, n);
    r[n] = 0;
    return r;
}

/*! Remove spaces in a string. */
static void remove_spaces(char *s)
{
    char *buffer = NULL;
    int i, j = 0;
    char c;
    buffer = (char*) malloc(strlen(s) + 1);
    for (i = 0; (c = s[i]) != '\0'; i++)
        if (!isspace(c))
            buffer[j++] = c;
    buffer[j] = '\0';
    strcpy(s, buffer);
    free(buffer);
}

/*! Tell if a character has to be seen as a separator by the
 *  parser. */
static int is_separator(char c)
{
    int ret;
    switch (c) {
    case '+':
    case '-':
    case '*':
    case '/':
    case '^':
    case '(':
    case ')':
        ret = 1;
        break;
    case '\0':
    case '\n':
        ret = 2;
        break;
    default:
        ret = 0;
    }
    return ret;
}


/*! Parse the string and separate the terms in an array. */
static char **parse_string(char *s, int *l)
{
    char **tab = (char **) malloc((strlen(s)+1) * sizeof(char*));
    int i_s = 0;
    int i_prev_separator = -1;
    int i_tab = 0;
    while (i_s <= strlen(s)) {
        if (is_separator(s[i_s]) != 0
            && ((i_s == 0) || ((i_s != 0) && (s[i_s - 1] != '['))))

            /* '-' must not be seen as a separator when it is used for
             * history values : x[-1]... */
        {

            /* If the previous separator is not the previous
             * character, a new word has to be inserted into the
             * array */
            if (i_prev_separator != (i_s - 1)) {
                tab[i_tab] =
                    (char *) malloc((i_s - i_prev_separator) *
                                    sizeof(char));
                tab[i_tab] = strndup_(s + i_prev_separator + 1,
                                      i_s - i_prev_separator - 1);
                i_tab++;
            }

            /* If this separator is different from '\0', it has to be
             * inserted into the array */
            if (is_separator(s[i_s]) == 1) {
                tab[i_tab] = (char *) malloc(2 * sizeof(char));
                tab[i_tab] = strndup_(s+i_s, 1);
                i_tab++;
                i_prev_separator = i_s;
                i_s++;
            }

            /* If the separator is '\0', parsing is ended */
            else if (is_separator(s[i_s]) == 2) {
                tab[i_tab] = (char *) malloc(4 * sizeof(char));
                tab[i_tab] = "end";
                i_s = strlen(s) * 2;
            }
        }

        else
            i_s++;
    }
    *l = i_tab;
    return tab;
}

mapper_expr_tree mapper_expr_new(void)
{
    mapper_expr_tree T;
    T = (mapper_expr_tree) calloc(1, sizeof(struct _mapper_expr_tree));
    T->left = NULL;
    T->right = NULL;
    return T;
}

void mapper_expr_free(mapper_expr_tree T)
{
    if (T != NULL) {
        mapper_expr_free(T->left);
        mapper_expr_free(T->right);
        free(T);
    }
}

mapper_expr_tree mapper_expr_copy(mapper_expr_tree T)
{
    mapper_expr_tree new_T = NULL;
    if (T != NULL) {
        new_T = mapper_expr_new();
        new_T->num = T->num;
        new_T->oper = T->oper;
        new_T->left = mapper_expr_copy(T->left);
        new_T->right = mapper_expr_copy(T->right);
    }
    return new_T;
}


/*! Convert a string to an operator_type object. */
static Operator string_to_operator(char *s, operator_type type,
                                   mapper_expr_error *err)
{
    Operator oper = EVAL;
    if (type == arity_1) {
        if (strcmp(s, "sin") == 0)
            oper = SIN;

        else if (strcmp(s, "cos") == 0)
            oper = COS;

        else if (strcmp(s, "tan") == 0)
            oper = TAN;

        else if (strcmp(s, "exp") == 0)
            oper = EXP;

        else if (strcmp(s, "log") == 0)
            oper = LOG;

        else if (strcmp(s, "log10") == 0)
            oper = LOG10;

        else if (strcmp(s, "abs") == 0)
            oper = ABS;

        else if (strcmp(s, "sqrt") == 0)
            oper = SQUARE_ROOT;

        else if (strcmp(s, "floor") == 0)
            oper = FLOOR;

        else if (strcmp(s, "round") == 0)
            oper = ROUND;

        else if (strcmp(s, "ceil") == 0)
            oper = CEIL;

        else if (strcmp(s, "-") == 0)
            oper = OPP;

        else {
            *err = ERR_INCORRECT_EXPRESSION;
            fprintf(stderr, "Incorrect expression : %s\n", s);
        }
    }

    else if (type == arity_2) {
        switch (s[0]) {
        case '+':
            oper = PLUS;
            break;
        case '-':
            oper = MINUS;
            break;
        case '*':
            oper = TIMES;
            break;
        case '/':
            oper = DIV;
            break;
        case '^':
            oper = POW;
            break;
        default:
            *err = ERR_INCORRECT_EXPRESSION;
            fprintf(stderr, "Incorrect expression : %s\n", s);
        }
    }

    else if (type == eval) {
        switch (s[0]) {
        case 'x':
            oper = VARX;
            break;
        case 'y':
            oper = VARY;
            break;
        default:
            *err = ERR_INCORRECT_EXPRESSION;
            fprintf(stderr, "Incorrect expression : %s\n", s);
        }
    }
    return oper;
}


/*! Get the history order from a string, x[..] or y[..]*/
static int find_history_order(char *s, mapper_expr_error *err)
{
    int o = 0;
    if (s[1] == '[' && s[strlen(s) - 1] == ']')
        o = atoi(s+2);

    if (o >= 0 || (int) fabs(o) > MAX_HISTORY_ORDER - 1) {
        perror("Invalid history argument\n");
        *err = ERR_HISTORY_ARGUMENT;
        return 0;
    }
    else
        return o;
}

/*! Construct the expression tree using the different priorities of
 *  the operators and functions */
void mapper_expr_construct(mapper_expr_tree T, char **expr,
                           int t1, int t2, mapper_expr_error *err)
{
    int i = t1;

/* positionv : position of the last operand in the expression */
    int positionv = t1;

/* position : position of the operator with the lowest priority */
    int position = t1;

/* parenth_order : "depth" of the term at the position i */
    int parenth_order = 0;

/* prev_parenth_order : "depth" of the term at the position i-1 */
    int prev_parenth_order = 100;
    int history_order = 0;

/* Initialization with the operator of highest priority. */
    operator_priority curr_prior = PRIOR_VAR;
    Operator operat = VARX;
    Operator tmp_oper = VARX;
    if (t1 > t2) {
        *err = ERR_EMPTY_EXPR;
        perror("Empty expression\n");
        return;
    }

/* Research of the operator with the lowest priority. */
    while (i < t2 + 1) {

        /* Analysis of expr[i] */

        /*If expr[i] represents a parenthesis */
        if (expr[i][0] == '(') {
            if (i == (t2 - 1) || expr[i + 1][0] == ')') {
                *err = ERR_PARENTHESIS;
                perror("Parenthesis error\n");
                return;
            }

            else
                parenth_order++;
        }

        else if (expr[i][0] == ')') {
            if (i == t1 || ((i != t2 - 1) && expr[i + 1][0] == '(')) {
                *err = ERR_PARENTHESIS;
                perror("Parenthesis error\n");
                return;
            }

            else
                parenth_order--;
        }

        /*If expr[i] represents a float */
        else if (isdigit(expr[i][0]) != 0 || (strcmp(expr[i], "pi") == 0)
                 || (strcmp(expr[i], "Pi") == 0)) {
            if (curr_prior > PRIOR_NUMBER) {
                curr_prior = PRIOR_NUMBER;
                positionv = i;
            }
        }

        else {

            /*If expr[i] represents a function or an history value */
            if (strlen(expr[i]) != 1) {
                if (strchr(expr[i], '[') == NULL) {
                    tmp_oper = string_to_operator(expr[i], arity_1, err);
                }

                else {
                    if (expr[i][0] == 'x') {
                        tmp_oper = VARX_HISTORY;
                        history_order = find_history_order(expr[i], err);
                    }

                    else if (expr[i][0] == 'y') {
                        tmp_oper = VARY_HISTORY;
                        history_order = find_history_order(expr[i], err);
                    }

                    else {
                        *err = ERR_HISTORY_ARGUMENT;
                        perror("Invalid history argument\n");
                        return;
                    }
                }
            }

            /*If expr[i] represents an operator or a variable */
            else if (strlen(expr[i]) == 1) {
                switch (expr[i][0]) {

                /* '-' represents both operators MINUS (arity_2) and
                 * OPP (arity_1) */
                case '-':
                    if ((i == t1)
                        || ((i != t1) && (expr[i - 1][0] == '('))
                        || ((i != t1) && (expr[i - 1][0] == '*'))
                        || ((i != t1) && (expr[i - 1][0] == '/'))
                        || ((i != t1) && (expr[i - 1][0] == '^')))
                        tmp_oper = string_to_operator("-", arity_1, err);

                    else
                        tmp_oper = string_to_operator("-", arity_2, err);
                    break;
                case 'x':
                case 'y':
                    tmp_oper = string_to_operator(expr[i], eval, err);
                    break;
                default:
                    tmp_oper = string_to_operator(expr[i], arity_2, err);
                    break;
                }
            }

            /* If the "parenthesis depth" has decreased or if the
             * priority of tmp_oper is lower than the current lowest
             * one, operat=tmp_oper */
            if ((parenth_order < prev_parenth_order)
                || ((parenth_order == prev_parenth_order)
                    && (curr_prior > tmp_oper.prior))) {
                position = i;
                operat = tmp_oper;
                curr_prior = operat.prior;
                prev_parenth_order = parenth_order;
            }
        }
        i++;
    }
    if (parenth_order != 0) {
        *err = ERR_PARENTHESIS;
        perror("Parenthesis error\n");
        return;
    }

    else if (*err == ERR_INCORRECT_EXPRESSION) {
        return;
    }

/* Construction of the tree. */
    if (curr_prior == PRIOR_VAR) {

        /* In this case T->num gives the history order, and not a
         * nuveric value */
        T->oper = operat;
        if (history_order != 0)
            T->num = history_order;
    }

    else if (curr_prior == PRIOR_NUMBER) {
        T->oper = EVAL;
        if (isdigit(expr[positionv][0]) != 0) {
            T->num = (float) atof(expr[positionv]);
        }

        else if ((strcmp(expr[positionv], "pi") == 0)
                 || (strcmp(expr[positionv], "Pi") == 0)) {
            T->num = M_PI;
        }

        else {
            *err = ERR_INCORRECT_EXPRESSION;
            fprintf(stderr, "Incorrect expression : %s\n",
                    expr[positionv]);
            return;
        }
    }

    else if (operat.type == arity_1) {
        T->oper = operat;
        T->left = mapper_expr_new();
        T->right = NULL;
        if ((t2 - prev_parenth_order - position - 1) < 0
            || (expr[position + 1][0] == '('
                && expr[position + 2][0] == ')')) {
            *err = ERR_MISSING_ARG;
            perror("Missing argument\n");
            return;
        }

        else if (expr[position + 1][0] != '(') {
            *err = ERR_PARENTHESIS;
            perror("Arguments must be between ()\n");
            return;
        }
        mapper_expr_construct(T->left, expr, position + 1,
                              t2 - prev_parenth_order, err);
    }

    else if (operat.id == OPP.id) {
        T->oper = MINUS;
        T->left = mapper_expr_new();
        T->right = mapper_expr_new();
        T->left->oper = EVAL;
        T->left->num = (float) 0;
        if ((t2 - prev_parenth_order - position - 1) < 0) {
            *err = ERR_MISSING_ARG;
            perror("Missing argument\n");
            return;
        }
        mapper_expr_construct(T->right, expr, position + 1,
                              t2 - prev_parenth_order, err);
    }

    else {
        T->oper = operat;
        T->left = mapper_expr_new();
        T->right = mapper_expr_new();
        if ((position - 1 - t1 - prev_parenth_order) < 0) {
            *err = ERR_MISSING_ARG;
            perror("Missing argument\n");
            return;
        }
        mapper_expr_construct(T->left, expr, t1 + prev_parenth_order,
                              position - 1, err);
        if ((t2 - prev_parenth_order - position - 1) < 0) {
            *err = ERR_MISSING_ARG;
            perror("Missing argument\n");
            return;
        }
        mapper_expr_construct(T->right, expr, position + 1,
                              t2 - prev_parenth_order, err);
    }
}

/*! Get the expression tree from the expression string. */
int mapper_expr_create_from_string(mapper_expr_tree T, const char *str)
{
    char expr[1024], *s = expr;
    strncpy(s, str, 1024);

    /* The commented parts are for debug use, to get the string from
     * the terminal */

    /*char s[SIZE]; */
    char **parsed_expr = NULL;
    int expr_length;
    mapper_expr_error err = ERR_EMPTY_EXPR;
    while (err != NO_ERR) {
        err = NO_ERR;

        if (s != NULL && err == NO_ERR) {

            /*Remove spaces */
            remove_spaces(s);
            if (s[0] == 'y' && s[1] == '=')
                s = strndup_(s+2, strlen(s)-2);
            if (strlen(s) > 0) {

                /*Parse the expression */
                parsed_expr = parse_string(s, &expr_length);
                if (parsed_expr != NULL) {

                    /*Construct the tree */
                    mapper_expr_construct(T, parsed_expr, 0,
                                          expr_length - 1, &err);
                    free(*parsed_expr);
                    if (err != NO_ERR)
                        return 0;
                }

                else {
                    err = ERR_PARSER;
                    perror("Parser error\n");
                    return 0;
                }
            }

            else {
                err = ERR_EMPTY_EXPR;
                perror("Empty expression\n");
                return 0;
            }
        }

        else {
            err = ERR_GET_STRING;
            perror("Impossible to get the string\n");
            return 0;
        }
    }
    return 1 /**/;

}
