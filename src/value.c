#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "mpr_internal.h"
#include "types_internal.h"
#include <mpr/mpr.h>

void mpr_value_realloc(mpr_value v, int mem, int samp_size, int is_input)
{
    RETURN_UNLESS(v && mem && samp_size);
    RETURN_UNLESS(mem != v->mem || samp_size != v->len * mpr_type_get_size(v->type));
    if (!is_input || (mem > v->mem) || (0 == v->pos)) {
        // realloc in place
        v->samps = realloc(v->samps, mem * samp_size);
        v->times = realloc(v->times, mem * sizeof(mpr_time));
        if (!is_input || samp_size != v->len * mpr_type_get_size(v->type)) {
            // Initialize entire value to 0
            memset(v->samps, 0, mem * samp_size);
            v->pos = -1;
        }
        else if (0 == v->pos) {
            memset(v->samps + samp_size * v->mem, 0, samp_size * (mem - v->mem));
        }
        else {
            int new_pos = mem - v->mem + v->pos;
            memcpy(v->samps + samp_size * new_pos, v->samps + samp_size * v->pos,
                   samp_size * (v->mem - v->pos));
            memcpy(&v->times[new_pos], &v->times[(int)v->pos], sizeof(mpr_time) * (v->mem - v->pos));
            memset(v->samps + samp_size * v->pos, 0, samp_size * (mem - v->mem));
        }
    }
    else {
        // copying into smaller array
        if (v->pos >= mem * 2) {
            // no overlap - memcpy ok
            int new_pos = mem - v->mem + v->pos;
            memcpy(v->samps, v->samps + samp_size * (new_pos - mem), samp_size * mem);
            memcpy(&v->times, &v->times[v->pos - mem], sizeof(mpr_time) * mem);
            v->samps = realloc(v->samps, mem * samp_size);
            v->times = realloc(v->times, mem * sizeof(mpr_time));
        }
        else {
            // there is overlap between new and old arrays - need to allocate new memory
            mpr_value_t tmp;
            tmp.samps = malloc(samp_size * mem);
            tmp.times = malloc(sizeof(mpr_time) * mem);
            if (v->pos < mem) {
                memcpy(tmp.samps, v->samps, samp_size * v->pos);
                memcpy(tmp.samps + samp_size * v->pos, v->samps + samp_size * (v->mem - mem + v->pos),
                       samp_size * (mem - v->pos));
                memcpy(tmp.times, v->times, sizeof(mpr_time) * v->pos);
                memcpy(&tmp.times[(int)v->pos], &v->times[v->mem - mem + v->pos],
                       sizeof(mpr_time) * (mem - v->pos));
            }
            else {
                memcpy(tmp.samps, v->samps + samp_size * (v->pos - mem), samp_size * mem);
                memcpy(tmp.times, &v->times[v->pos - mem], sizeof(mpr_time) * mem);
                v->pos = mem - 1;
            }
            free(v->samps);
            free(v->times);
            v->samps = tmp.samps;
            v->times = tmp.times;
        }
    }
    v->mem = mem;
}
