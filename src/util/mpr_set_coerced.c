#include <string.h>
#include "../mpr_type.h"

/* Helper for setting property value from different data types */
int mpr_set_coerced(int src_len, mpr_type src_type, const void *src_val,
                    int dst_len, mpr_type dst_type, void *dst_val)
{
    int i, j, min_len = src_len < dst_len ? src_len : dst_len, modified = 0;

    if (src_type == dst_type) {
        int size = mpr_type_get_size(src_type);
        do {
            if (memcmp(dst_val, src_val, size * min_len)) {
                memcpy(dst_val, src_val, size * min_len);
                modified = 1;
            }
            dst_len -= min_len;
            dst_val = (void*)((char*)dst_val + size * min_len);
            if (dst_len < min_len)
                min_len = dst_len;
        } while (dst_len > 0);
        return !modified;
    }

    switch (dst_type) {
        case MPR_FLT:{
            float *dstf = (float*)dst_val, tempf;
            switch (src_type) {
                case MPR_BOOL:
                case MPR_INT32: {
                    int *srci = (int*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        tempf = (float)srci[j];
                        if (tempf != dstf[i]) {
                            modified = 1;
                            dstf[i] = tempf;
                        }
                    }
                    break;
                }
                case MPR_DBL: {
                    double *srcd = (double*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        tempf = (float)srcd[j];
                        if (tempf != dstf[i]) {
                            dstf[i] = tempf;
                            modified = 1;
                        }
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case MPR_INT32: {
            int *dsti = (int*)dst_val, tempi;
            switch (src_type) {
                case MPR_FLT: {
                    float *srcf = (float*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        tempi = (int)srcf[j];
                        if (tempi != dsti[i]) {
                            dsti[i] = tempi;
                            modified = 1;
                        }
                    }
                    break;
                }
                case MPR_DBL: {
                    double *srcd = (double*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        tempi = (int)srcd[j];
                        if (tempi != dsti[i]) {
                            dsti[i] = tempi;
                            modified = 1;
                        }
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case MPR_DBL: {
            double *dstd = (double*)dst_val, tempd;
            switch (src_type) {
                case MPR_INT32: {
                    int *srci = (int*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        tempd = (float)srci[j];
                        if (tempd != dstd[i]) {
                            dstd[i] = tempd;
                            modified = 1;
                        }
                    }
                    break;
                }
                case MPR_FLT: {
                    float *srcf = (float*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        tempd = (double)srcf[j];
                        if (tempd != dstd[i]) {
                            dstd[i] = tempd;
                            modified = 1;
                        }
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case MPR_BOOL: {
            int *dstb = (int*)dst_val, tempb;
            switch (src_type) {
                case MPR_INT32: {
                    int *srci = (int*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        tempb = srci[j] != 0;
                        if (tempb != dstb[i]) {
                            dstb[i] = tempb;
                            modified = 1;
                        }
                    }
                    break;
                }
                case MPR_FLT: {
                    float *srcf = (float*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        tempb = (int)srcf[j] != 0;
                        if (tempb != dstb[i]) {
                            dstb[i] = tempb;
                            modified = 1;
                        }
                    }
                    break;
                }
                case MPR_DBL: {
                    double *srcd = (double*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        tempb = (int)srcd[j] != 0;
                        if (tempb != dstb[i]) {
                            dstb[i] = tempb;
                            modified = 1;
                        }
                    }
                    break;
                }
                case MPR_STR: {
                    if (src_len == 1) {
                        const char *srcs = (const char*)src_val;
                        tempb = srcs[0] == 'T' || srcs[0] == 't';
                        for (i = 0; i < dst_len; i++) {
                            if (tempb != dstb[i]) {
                                dstb[i] = tempb;
                                modified = 1;
                            }
                        }
                    }
                    else {
                        const char **srcs = (const char**)src_val;
                        for (i = 0, j = 0; i < dst_len; i++, j++) {
                            if (j >= src_len)
                                j = 0;
                            tempb = (srcs[j][0] == 'T' || srcs[j][0] == 't');
                            if (tempb != dstb[i]) {
                                dstb[i] = tempb;
                                modified = 1;
                            }
                        }
                    }
                    break;
                }
            }
            break;
        }
        default:
            return -1;
    }
    return !modified;
}
