#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include <assert.h>

#include "util/mpr_set_coerced.h"
#include "value.h"

typedef struct _mpr_value_buffer
{
    void *samps;                /*!< Value for each sample of stored history. */
    mpr_time *times;            /*!< Time for each sample of stored history. */
    int16_t pos;                /*!< Current position in the circular buffer. */
    uint8_t full;               /*!< Indicates whether complete buffer contains valid data. */
} mpr_value_buffer_t, *mpr_value_buffer;

typedef struct _mpr_value
{
    mpr_value_buffer inst;      /*!< Array of value histories for each signal instance. */
    uint8_t vlen;               /*!< Vector length. */
    uint8_t num_inst;           /*!< Number of instances. */
    uint8_t num_active_inst;    /*!< Number of active instances. */
    mpr_type type;              /*!< The type of this signal. */
    uint16_t mlen;              /*!< History size of the buffer. */
} mpr_value_t;

MPR_INLINE static int _min(int a, int b) { return a < b ? a : b; }

mpr_value mpr_value_new(unsigned int vlen, mpr_type type, unsigned int mlen, unsigned int num_inst)
{
    mpr_value v = (mpr_value) calloc(1, sizeof(mpr_value_t));
    mpr_value_realloc(v, vlen, type, mlen, num_inst, 0);
    return v;
}

void mpr_value_free(mpr_value v) {
    int i;
    RETURN_UNLESS(v && v->inst);
    for (i = 0; i < v->num_inst; i++) {
        FUNC_IF(free, v->inst[i].samps);
        FUNC_IF(free, v->inst[i].times);
    }
    free(v->inst);
    free(v);
}

void mpr_value_realloc(mpr_value v, unsigned int vlen, mpr_type type, unsigned int mlen,
                       unsigned int num_inst, int reset)
{
    int i, samp_size;
    mpr_value_buffer_t *b, tmp;
    RETURN_UNLESS(v);
    if (vlen <= 0)
        vlen = v->vlen;
    if (mlen <= 0)
        mlen = v->mlen;
    samp_size = vlen * mpr_type_get_size(type);

    if (!v->inst || num_inst > v->num_inst) {
        if (v->inst)
            v->inst = realloc(v->inst, sizeof(mpr_value_buffer_t) * num_inst);
        else {
            v->inst = malloc(sizeof(mpr_value_buffer_t) * num_inst);
            v->num_inst = 0;
        }
        /* initialize new instances */
        for (i = v->num_inst; i < num_inst; i++) {
            b = &v->inst[i];
            b->samps = calloc(1, mlen * samp_size);
            b->times = calloc(1, mlen * sizeof(mpr_time));
            b->pos = -1;
            b->full = 0;
        }
    }

    if (reset || vlen != v->vlen || type != v->type) {
        /* reallocate old instances (v->num_inst has not yet been updated) */
        for (i = 0; i < v->num_inst; i++) {
            b = &v->inst[i];
            b->samps = realloc(b->samps, mlen * samp_size);
            b->times = realloc(b->times, mlen * sizeof(mpr_time));
            /* Initialize entire value to 0 */
            memset(b->samps, 0, mlen * samp_size);
            memset(b->times, 0, mlen * sizeof(mpr_time));
            b->pos = -1;
            b->full = 0;
        }
        goto done;
    }

    if (!v->mlen || mlen == v->mlen)
        goto done;

    /* only the memory size is different */

    for (i = 0; i < v->num_inst; i++) {
        b = &v->inst[i];
        tmp.samps = malloc(samp_size * mlen);
        tmp.times = malloc(sizeof(mpr_time) * mlen);

        /* TODO: don't bother copying memory if pos is -1 */
        if (mlen > v->mlen) {
            int opos = b->pos < 0 ? 0 : b->pos;
            int npos = v->mlen - opos;
            /* copy from [v->pos, v->mlen] to [0, v->mlen - v->pos] */
            memcpy(tmp.samps, (char*)b->samps + opos * samp_size, npos * samp_size);
            memcpy(tmp.times, &b->times[opos], npos * sizeof(mpr_time));
            /* copy from [0, v->pos] to [v->mlen - v->pos, v->mlen] */
            memcpy((char*)tmp.samps + npos * samp_size, b->samps, opos * samp_size);
            memcpy(&tmp.times[npos], b->times, opos * sizeof(mpr_time));
            /* zero remainder */
            memset((char*)tmp.samps + v->mlen * samp_size, 0, (mlen - v->mlen) * samp_size);
            memset(&tmp.times[v->mlen], 0, (mlen - v->mlen) * sizeof(mpr_time));
            b->pos = b->pos < 0 ? -1 : v->mlen;
            b->full = 0;
        }
        else {
            int opos = b->pos < 0 ? 0 : b->pos;
            int len = _min(v->mlen - opos, mlen);
            memcpy(tmp.samps, (char*)b->samps + opos * samp_size, len * samp_size);
            memcpy(tmp.times, &b->times[opos], len * sizeof(mpr_time));
            if (mlen > len) {
                memcpy((char*)tmp.samps + len * samp_size, b->samps, (mlen - len) * samp_size);
                memcpy(&tmp.times[len], b->times, (mlen - len) * sizeof(mpr_time));
            }
            b->pos = b->pos < 0 ? -1 : len;
            b->full = (b->pos > mlen);
        }

        free(b->samps);
        free(b->times);
        b->samps = tmp.samps;
        b->times = tmp.times;
    }

done:
    v->vlen = vlen;
    v->type = type;
    v->mlen = mlen;
    v->num_inst = num_inst;
}

int mpr_value_remove_inst(mpr_value v, unsigned int idx)
{
    int i;
    RETURN_ARG_UNLESS(idx >= 0 && idx < v->num_inst, v->num_inst);
    free(v->inst[idx].samps);
    free(v->inst[idx].times);
    if (v->inst[idx].pos >= 0)
        --v->num_active_inst;
    for (i = idx + 1; i < v->num_inst; i++) {
        /* shift values down */
        memcpy(&(v->inst[i-1]), &(v->inst[i]), sizeof(mpr_value_buffer_t));
    }
    --v->num_inst;
    assert(v->num_inst >= 0);
    v->inst = realloc(v->inst, sizeof(mpr_value_buffer_t) * v->num_inst);
    return v->num_inst;
}

void mpr_value_reset_inst(mpr_value v, unsigned int idx, mpr_time t)
{
    mpr_value_buffer b;
    RETURN_UNLESS(v->inst && idx < v->num_inst);
    b = &v->inst[idx];
    memset(b->samps, 0, v->mlen * v->vlen * mpr_type_get_size(v->type));
    /* store the reset time at idx 0 */
    memset(b->times, 0, v->mlen * sizeof(mpr_time));
    memcpy(b->times, &t, sizeof(mpr_time));
    if (b->pos >= 0)
        --v->num_active_inst;
    b->pos = -1;
    b->full = 0;
}

void* mpr_value_get_value(mpr_value v, unsigned int inst_idx, int hist_idx)
{
    mpr_value_buffer b = &v->inst[inst_idx % v->num_inst];
    int idx = (b->pos + v->mlen + hist_idx) % v->mlen;
    RETURN_ARG_UNLESS(b->pos >= 0, NULL);
    if (idx < 0)
        idx += v->mlen;
    return (char*)b->samps + idx * v->vlen * mpr_type_get_size(v->type);
}

void mpr_value_set_next(mpr_value v, unsigned int inst_idx, const void *s, mpr_time *t)
{
    mpr_value_incr_idx(v, inst_idx);
    if (s)
        memcpy(mpr_value_get_value(v, inst_idx, 0), s, v->vlen * mpr_type_get_size(v->type));
    if (t)
        memcpy(mpr_value_get_time(v, inst_idx, 0), t, sizeof(mpr_time));
}

/* returns 1 if the element was updated */
int mpr_value_set_element(mpr_value v, unsigned int inst_idx, int el_idx, void *new)
{
    void *old = mpr_value_get_value(v, inst_idx, 0);
    size_t size = mpr_type_get_size(v->type);
    RETURN_ARG_UNLESS(old, 0);
    el_idx = el_idx % v->vlen;
    if (el_idx < 0)
        el_idx += v->vlen;
    if (memcmp(old + el_idx * size, new, size)) {
        memcpy(old + el_idx * size, new, size);
        return 1;
    }
    return 0;
}

int mpr_value_set_next_coerced(mpr_value v, unsigned int inst_idx, unsigned int len,
                               mpr_type type, const void *s, mpr_time *t)
{
    mpr_value_incr_idx(v, inst_idx);
    if (t)
        memcpy(mpr_value_get_time(v, inst_idx, 0), t, sizeof(mpr_time));
    return s ? mpr_set_coerced(len, type, s, v->vlen, v->type, mpr_value_get_value(v, inst_idx, 0)) : 0;
}

/* here we return the time at idx 0 even if the value has been reset */
mpr_time* mpr_value_get_time(mpr_value v, unsigned int inst_idx, int hist_idx)
{
    mpr_value_buffer b = &v->inst[inst_idx % v->num_inst];
    int idx = 0;
    if (b->pos >= 0) {
        idx = (b->pos + v->mlen + hist_idx) % v->mlen;
        if (idx < 0)
            idx += v->mlen;
    }
    return &b->times[idx];
}

void mpr_value_set_time_hist(mpr_value v, mpr_time t, unsigned int inst_idx, int hist_idx)
{
    mpr_value_buffer b = &v->inst[inst_idx % v->num_inst];
    int idx = (b->pos + v->mlen + hist_idx) % v->mlen;
    if (idx < 0)
        idx += v->mlen;
    memcpy(&b->times[idx], &t, sizeof(mpr_time));
}

int mpr_value_cmp(mpr_value v, unsigned int inst_idx, int hist_idx, const void *ptr)
{
    const void *s = mpr_value_get_value(v, inst_idx, hist_idx);
    return !s || memcmp(s, ptr, v->vlen * mpr_type_get_size(v->type));
}

void mpr_value_incr_idx(mpr_value v, unsigned int inst_idx)
{
    mpr_value_buffer b = &v->inst[inst_idx % v->num_inst];
    if (b->pos < 0)
        ++v->num_active_inst;
    if (++b->pos >= v->mlen) {
        b->pos = 0;
        b->full |= 1;
    }
}

void mpr_value_decr_idx(mpr_value v, unsigned int inst_idx)
{
    mpr_value_buffer b = &v->inst[inst_idx % v->num_inst];
    if (--b->pos < 0)
        b->pos = v->mlen - 1;
}

unsigned int mpr_value_get_num_samps(mpr_value v, unsigned int inst_idx)
{
    mpr_value_buffer b = &v->inst[inst_idx % v->num_inst];
    return b->full ? v->mlen : (b->pos + 1);
}

unsigned int mpr_value_get_vlen(mpr_value v)
{
    return v->vlen;
}

unsigned int mpr_value_get_mlen(mpr_value v)
{
    return v->mlen;
}

unsigned int mpr_value_get_num_inst(mpr_value v)
{
    return v->num_inst;
}

unsigned int mpr_value_get_num_active_inst(mpr_value v)
{
    return v->num_active_inst;
}

mpr_type mpr_value_get_type(mpr_value v)
{
    return v->type;
}

#ifdef DEBUG
static void _value_print(mpr_value v, unsigned int inst_idx, int hist_idx) {
    int i;
    void *s;
    mpr_time *t;
    if (v->inst[inst_idx].pos < 0) {
        printf("NULL\n");
        return;
    }
    s = mpr_value_get_value(v, inst_idx, hist_idx);
    t = mpr_value_get_time(v, inst_idx, hist_idx);
    printf("%08x.%08x | ", (*t).sec, (*t).frac);
    if (v->vlen > 1)
        printf("[");

    switch (v->type) {
#define TYPED_CASE(MTYPE, TYPE, STR)        \
        case MTYPE:                         \
            for (i = 0; i < v->vlen; i++)   \
                printf(STR, ((TYPE*)s)[i]); \
            break;
        TYPED_CASE(MPR_INT32, int, "%d, ");
        TYPED_CASE(MPR_FLT, float, "%f, ");
        TYPED_CASE(MPR_DBL, double, "%f, ");
#undef TYPED_CASE
    }

    if (v->vlen > 1)
        printf("\b\b] @%p -> %p\n", v->inst[inst_idx].samps, s);
    else
        printf("\b\b @%p -> %p\n", v->inst[inst_idx].samps, s);
}

int mpr_value_print_inst(mpr_value v, unsigned int inst_idx) {
    RETURN_ARG_UNLESS(inst_idx < v->num_inst && v->inst[inst_idx].pos >= 0, 1);
    _value_print(v, inst_idx, v->inst[inst_idx].pos);
    return 0;
}

void mpr_value_print(mpr_value v) {
    int i;
    for (i = 0; i < v->num_inst; i++) {
        printf("%d:\t", i);
        if (mpr_value_print_inst(v, i))
            printf("no value\n");
    }
}

int mpr_value_print_inst_hist(mpr_value v, unsigned int inst_idx) {
    int i, hidx;
    RETURN_ARG_UNLESS(inst_idx < v->num_inst && v->inst[inst_idx].pos >= 0, 1);

    /* if history is full, print from pos+1 -> pos, else print from 0 -> pos */
    hidx = v->inst[inst_idx].pos * -1;
    for (i = 0; i < v->mlen; i++) {
        printf("%s {%3d} ", hidx ? "  " : "->", hidx);
        _value_print(v, inst_idx, hidx);
        ++hidx;
        if (hidx > 0)
            hidx -= v->mlen;
    };
    return 0;
}

#endif
