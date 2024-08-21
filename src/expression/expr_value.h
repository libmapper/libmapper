#ifndef __MPR_EXPR_VALUE_H__
#define __MPR_EXPR_VALUE_H__

#include "expr_trace.h"

typedef union _evalue {
    float f;
    double d;
    int i;
} evalue_t, *evalue;

MPR_INLINE static void evalue_cpy(void *dst, const void *src, size_t size)
{
    memcpy(dst, src, size * sizeof(evalue_t));
}

#if TRACE_EVAL
static void evalue_print(evalue val, mpr_type type, int len, int prefix)
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

#endif /* __MPR_EXPR_VALUE_H__ */
