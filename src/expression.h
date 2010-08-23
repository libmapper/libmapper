#ifndef __EXPRESSION_H__
#define __EXPRESSION_H__

#define SIZE 100
#define _Pi 3.14159265358979323846
#define MAX_HISTORY_ORDER 5
typedef enum { NO_ERR, ERR_GET_STRING, ERR_EMPTY_EXPR,
    ERR_PARSER, ERR_PARENTHESIS, ERR_MISSING_ARG,
    ERR_INCORRECT_EXPRESSION, ERR_TOO_LONG, ERR_COMPUTATION,
    ERR_HISTORY_ARGUMENT, ERR_HISTORY_COMPUTATION
} mapper_error;
void get_typed_string(char *s, mapper_error *err);
void remove_spaces(char *s);
char *sub_string(const char *s, unsigned int start, unsigned int end);
int is_separator(char c);
char **parse_string(char *s, int *l);
Operator string_to_operator(char *s, operator_type type, mapper_error *err);
int find_history_order(char *s, mapper_error *err);

/*! Structure used by the LINEAR and EXPRESSION mapping type to
 *  represent the expression of the mapping. */
typedef struct Tree {
    Operand num;                //!< Numerical node
    Operator oper;              /*!< Operator node : operator,
                                 *   function, parenthesis, variable,
                                 *   history value. */
    struct Tree *left, *right;
} Tree;
Tree *NewTree(void);
void DeleteTree(Tree *T);
Tree *CopyTree(Tree *T);
void ConstructTree(Tree *T, char **expr, int t1, int t2, mapper_error *err);
float EvalTree(Tree *T, float *history_x, float *history_y,
               int history_position, mapper_error *err);

#endif // __EXPRESSION_H__
