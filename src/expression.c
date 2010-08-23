#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "operations.h"
#include "expression.h"

/*! Get the expression string from the terminal, only for debug use. */
void get_typed_string(char *s, mapper_error *err)
{
    int i = 0;
    char buffer[SIZE];
    char *buffer2 = NULL;
    buffer2 = fgets(buffer, SIZE, stdin);
    while (buffer[i] != '\n' && i < SIZE)
        i++;
    if (i < SIZE) {
        buffer[i] = '\0';
        strcpy(s, buffer);
    }

    else {
        *err = ERR_TOO_LONG;
        perror("Too long expression\n");
        return;
    }
}


/*! Remove spaces in a string. */
void remove_spaces(char *s)
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


/*! Get the substring between two positions in a string */
char *sub_string(const char *s, unsigned int start, unsigned int end)
{
    char *new_s = NULL;
    if (s != NULL && start < end) {
        new_s = (char*) malloc(sizeof(char) * (end - start + 2));
        if (new_s != NULL) {
            int i;
            for (i = start; i <= end; i++) {
                new_s[i - start] = s[i];
            }
            new_s[i - start] = '\0';
        }

        else {
            fprintf(stderr, "Allocation error\n");
            exit(EXIT_FAILURE);
        }
    }

    else if (s != NULL && start == end) {
        new_s = (char*) malloc(2 * sizeof(char));
        if (new_s != NULL) {
            new_s[0] = s[start];
            new_s[1] = '\0';
        }

        else {
            fprintf(stderr, "Allocation error\n");
            exit(EXIT_FAILURE);
        }
    }
    return new_s;
}


/*! Tell if a character has to be seen as a separator by the
 *  parser. */
int is_separator(char c)
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
char **parse_string(char *s, int *l)
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
                tab[i_tab] = sub_string(s, i_prev_separator + 1, i_s - 1);
                i_tab++;
            }

            /* If this separator is different from '\0', it has to be
             * inserted into the array */
            if (is_separator(s[i_s]) == 1) {
                tab[i_tab] = (char *) malloc(2 * sizeof(char));
                tab[i_tab] = sub_string(s, i_s, i_s);
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

Tree *NewTree(void)
{
    Tree *T;
    T = (Tree *) calloc(1, sizeof(Tree));
    T->left = NULL;
    T->right = NULL;
    return T;
}

void DeleteTree(Tree *T)
{
    if (T != NULL) {
        DeleteTree(T->left);
        DeleteTree(T->right);
        free(T);
    }
}

Tree *CopyTree(Tree *T)
{
    Tree *new_T = NULL;
    if (T != NULL) {
        new_T = NewTree();
        new_T->num = T->num;
        new_T->oper = T->oper;
        new_T->left = CopyTree(T->left);
        new_T->right = CopyTree(T->right);
    }
    return new_T;
}


/*! Convert a string to an operator_type object. */
Operator string_to_operator(char *s, operator_type type, mapper_error *err)
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
int find_history_order(char *s, mapper_error *err)
{
    int o = 0;
    char *buffer = NULL;
    buffer = (char*) malloc(strlen(s) * sizeof(char));
    if (s[1] == '[' && s[strlen(s) - 1] == ']') {
        buffer = sub_string(s, 2, strlen(s) - 1);
        o = atoi(buffer);
    }

    else {
        perror("Invalid history argument\n");
        *err = ERR_HISTORY_ARGUMENT;
        return 0;
    }
    free(buffer);
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
void ConstructTree(Tree *T, char **expr, int t1, int t2, mapper_error *err)
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
            T->num = _Pi;
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
        T->left = NewTree();
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
        ConstructTree(T->left, expr, position + 1,
                      t2 - prev_parenth_order, err);
    }

    else if (operat.id == OPP.id) {
        T->oper = MINUS;
        T->left = NewTree();
        T->right = NewTree();
        T->left->oper = EVAL;
        T->left->num = (float) 0;
        if ((t2 - prev_parenth_order - position - 1) < 0) {
            *err = ERR_MISSING_ARG;
            perror("Missing argument\n");
            return;
        }
        ConstructTree(T->right, expr, position + 1,
                      t2 - prev_parenth_order, err);
    }

    else {
        T->oper = operat;
        T->left = NewTree();
        T->right = NewTree();
        if ((position - 1 - t1 - prev_parenth_order) < 0) {
            *err = ERR_MISSING_ARG;
            perror("Missing argument\n");
            return;
        }
        ConstructTree(T->left, expr, t1 + prev_parenth_order,
                      position - 1, err);
        if ((t2 - prev_parenth_order - position - 1) < 0) {
            *err = ERR_MISSING_ARG;
            perror("Missing argument\n");
            return;
        }
        ConstructTree(T->right, expr, position + 1,
                      t2 - prev_parenth_order, err);
    }
}
