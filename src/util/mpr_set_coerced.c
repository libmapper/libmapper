#include <string.h>
#include <mapper/mapper.h>
#include "../mpr_type.h"

/* Helper for setting property value from different data types */
int mpr_set_coerced(int src_len, mpr_type src_type, const void *src_val,
                    int dst_len, mpr_type dst_type, void *dst_val)
{
    int i, j, min_len = src_len < dst_len ? src_len : dst_len;

    if (src_type == dst_type) {
        int size = mpr_type_get_size(src_type);
        do {
            memcpy(dst_val, src_val, size * min_len);
            dst_len -= min_len;
            dst_val = (void*)((char*)dst_val + size * min_len);
            if (dst_len < min_len)
                min_len = dst_len;
        } while (dst_len > 0);
        return 0;
    }

    switch (dst_type) {
        case MPR_FLT:{
            float *dstf = (float*)dst_val;
            switch (src_type) {
                case MPR_INT32: {
                    int *srci = (int*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstf[i] = (float)srci[j];
                    }
                    break;
                }
                case MPR_DBL: {
                    double *srcd = (double*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstf[i] = (float)srcd[j];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case MPR_INT32:{
            int *dsti = (int*)dst_val;
            switch (src_type) {
                case MPR_FLT: {
                    float *srcf = (float*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dsti[i] = (int)srcf[j];
                    }
                    break;
                }
                case MPR_DBL: {
                    double *srcd = (double*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dsti[i] = (int)srcd[j];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case MPR_DBL:{
            double *dstd = (double*)dst_val;
            switch (src_type) {
                case MPR_INT32: {
                    int *srci = (int*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstd[i] = (float)srci[j];
                    }
                    break;
                }
                case MPR_FLT: {
                    float *srcf = (float*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstd[i] = (double)srcf[j];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        default:
            return -1;
    }
    return 0;
}
