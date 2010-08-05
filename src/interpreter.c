#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "operations.h"
#include "expression.h"

/*! Get the expression tree from the expression string. */
int get_expr_Tree(Tree *T, char *s)
{

    /* The commented parts are for debug use, to get the string from
     * the terminal */

    /*char s[SIZE]; */
    char **parsed_expr = NULL;
    int expr_length;
    error err = ERR_EMPTY_EXPR;
    while (err != NO_ERR) {
        err = NO_ERR;

        if (s != NULL && err == NO_ERR) {

            /*Remove spaces */
            remove_spaces(s);
            if (s[0] == 'y' && s[1] == '=')
                s = sub_string(s, 2, strlen(s) - 1);
            if (strlen(s) > 0) {

                /*Parse the expression */
                parsed_expr = parse_string(s, &expr_length);
                if (parsed_expr != NULL) {

                    /*Construct the tree */
                    ConstructTree(T, parsed_expr, 0, expr_length - 1,
                                  &err);
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
