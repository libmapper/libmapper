#ifndef __MPR_EXPR_STRUCT_H__
#define __MPR_EXPR_STRUCT_H__

#include "expr_stack.h"
#include "expr_variable.h"

struct _mpr_expr
{
    estack stack;
    expr_var_t *vars;
    mpr_time next;
    uint16_t *src_mlen;
    uint16_t max_src_mlen;
    uint16_t dst_mlen;
    uint8_t num_vars;
    int8_t inst_ctl;
    int8_t mute_ctl;
    int8_t next_ctl;
    int8_t num_src;
    int8_t flags;
};

#endif /* __MPR_EXPR_STRUCT_H__ */
