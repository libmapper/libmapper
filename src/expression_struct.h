#ifndef __MPR_EXPRESSION_STRUCT_H__
#define __MPR_EXPRESSION_STRUCT_H__

#include "expression_stack.h"
#include "expression_variable.h"

struct _mpr_expr
{
    estack stack;
    expr_var_t *vars;
    uint16_t *src_mlen;
    uint16_t max_src_mlen;
    uint16_t dst_mlen;
    uint8_t num_vars;
    int8_t inst_ctl;
    int8_t mute_ctl;
    int8_t num_src;
    int8_t own_stack;
};

#endif /* __MPR_EXPRESSION_STRUCT_H__ */
