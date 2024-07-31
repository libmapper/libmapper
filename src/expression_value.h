#ifndef __MPR_EXPRESSION_VALUE_H__
#define __MPR_EXPRESSION_VALUE_H__

#include "expression_trace.h"

typedef union _mpr_expr_value {
    float f;
    double d;
    int i;
} mpr_expr_value_t, *mpr_expr_value;

#if TRACE_EVAL
static void mpr_expr_value_print(mpr_expr_value val, mpr_type type, int len, int prefix)
{
    int i;
    printf("%d|", prefix);
    if (!len) {
        printf("[]%c\n", type);
        return;
    }
    if (len > 1)
        printf("[");
    switch (type) {
#define TYPED_CASE(MTYPE, STR, T)           \
        case MTYPE:                         \
            for (i = 0; i < len; i++)       \
                printf(STR, val[i].T);      \
            break;
        TYPED_CASE(MPR_INT32, "%d, ", i)
        TYPED_CASE(MPR_FLT, "%g, ", f)
        TYPED_CASE(MPR_DBL, "%g, ", d)
#undef TYPED_CASE
        default:
            break;
    }
    if (len > 1)
        printf("\b\b]%c\n", type);
    else
        printf("\b\b%c\n", type);
}
#endif

#endif /* __MPR_EXPRESSION_VALUE_H__ */
