#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "operations.h"
#include "expression.h"

void get_expr_Tree(Tree *T, /**/char *s/**/)
{



    /*char s[SIZE];*/
    char **parsed_expr=NULL;
    int expr_length;
    float result;
    error err=ERR_EMPTY_EXPR;


	while (err!=NO_ERR)
        {
			printf("ENTRE DANS WHILE GET...\n");
            err=NO_ERR;
            /*printf("\n----------------------------------------------------------\n");
            printf("Available operators, functions, variables and constants :\n+ - * / ^\nexp log log10 sqrt cos sin tan abs floor round ceil\nx, x[-1],... y, y[-1],... Maximum history order = %d \nPi\n", MAX_HISTORY_ORDER);
            printf("----------------------------------------------------------\n");
            printf("Expression : ");
            fflush(stdout);
            get_typed_string(s, &err);*/

            if (s!=NULL && err==NO_ERR)
                {
                    /*Removes spaces*/
                    remove_spaces(s);
					
					/**/
					if (s[0]=='y' && s[1]=='=')
						s=sub_string(s,2, strlen(s)-1);/**/
					
                    if (strlen(s) > 0)
                        {
                            /*Parses the expression*/
                            parsed_expr=parse_string(s, &expr_length);
                            if (parsed_expr!=NULL)
                                {
                                    /*Constructs the tree*/
                                    ConstructTree( T, parsed_expr, 0, expr_length-1, &err);
                                    free(*parsed_expr);
                                }

                            else
                                {
                                    err=ERR_PARSER;
                                    perror("Parser error\n");
                                }
                        }

                    else
                        {
                            err=ERR_EMPTY_EXPR;
                            perror("Empty expression\n");
                        }
                }

            else
                {
                    err=ERR_GET_STRING;
                    perror("Impossible to get the string\n");
                }

        }


	return;
}
