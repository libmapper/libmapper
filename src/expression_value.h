#ifndef __MPR_EXPRESSION_VALUE_H__
#define __MPR_EXPRESSION_VALUE_H__

#include "expression_trace.h"

typedef union _expr_value {
    float f;
    double d;
    int i;
} expr_value_t, *expr_value;

MPR_INLINE static void expr_value_cpy(void *dst, const void *src, size_t size)
{
    memcpy(dst, src, size * sizeof(expr_value_t));
}

#if TRACE_EVAL
static void expr_value_print(expr_value val, mpr_type type, int len, int prefix)
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
