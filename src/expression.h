#ifndef __EXPRESSION_H__
#define __EXPRESSION_H__

typedef enum { NO_ERR, ERR_GET_STRING, ERR_EMPTY_EXPR,
    ERR_PARSER, ERR_PARENTHESIS, ERR_MISSING_ARG,
    ERR_INCORRECT_EXPRESSION, ERR_TOO_LONG, ERR_COMPUTATION,
    ERR_HISTORY_ARGUMENT, ERR_HISTORY_COMPUTATION
} mapper_expr_error;

#define MAX_HISTORY_ORDER 5

/*! Structure used by the LINEAR and EXPRESSION mapping type to
 *  represent the expression of the mapping. */
typedef struct _mapper_expr_tree {
    Operand num;                //!< Numerical node
    Operator oper;              /*!< Operator node : operator,
                                 *   function, parenthesis, variable,
                                 *   history value. */
    struct _mapper_expr_tree *left, *right;
} *mapper_expr_tree;

int mapper_expr_create_from_string(mapper_expr_tree T, const char *s);

mapper_expr_tree mapper_expr_new(void);

void mapper_expr_free(mapper_expr_tree e);

mapper_expr_tree mapper_expr_copy(mapper_expr_tree e);

void mapper_expr_construct(mapper_expr_tree e, char **expr,
                           int t1, int t2, mapper_expr_error *err);

float mapper_expr_eval(mapper_expr_tree e,
                       float *history_x, float *history_y,
                       int history_position, mapper_expr_error *err);

#endif // __EXPRESSION_H__
