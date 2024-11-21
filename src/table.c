
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "list.h"
#include "mpr_type.h"
#include "path.h"
#include "property.h"
#include "object.h"
#include "util/mpr_debug.h"
#include "util/mpr_set_coerced.h"
#include "table.h"
#include <mapper/mapper.h>

#define MPR_TBL_MAX_RECORDS 128

/*! Used to hold look-up table records. */
typedef struct _mpr_tbl_record {
    const char *key;
    void **val;
    int len;
    mpr_prop prop;
    mpr_type type;
    char flags;
} mpr_tbl_record_t;

/*! Used to hold look-up tables. */
typedef struct _mpr_tbl {
    mpr_tbl_record rec;
    int count;
    int alloced;
    char dirty;
} mpr_tbl_t;

/* we will sort so that indexed records come before keyed records */
static int compare_rec(const void *l, const void *r)
{
    mpr_tbl_record rec_l = (mpr_tbl_record)l;
    mpr_tbl_record rec_r = (mpr_tbl_record)r;
    int idx_l = MASK_PROP_BITFLAGS(rec_l->prop);
    int idx_r = MASK_PROP_BITFLAGS(rec_r->prop);
    if ((idx_l == MPR_PROP_EXTRA) && (idx_r == MPR_PROP_EXTRA)) {
        const char *str_l = rec_l->key, *str_r = rec_r->key;
        if (str_l[0] == '@')
            ++str_l;
        if (str_r[0] == '@')
            ++str_r;
        return mpr_path_match(str_l, str_r);
    }
    if (idx_l == MPR_PROP_EXTRA)
        return 1;
    if (idx_r == MPR_PROP_EXTRA)
        return -1;
    return idx_l - idx_r;
}

mpr_tbl mpr_tbl_new(void)
{
    mpr_tbl t = (mpr_tbl)calloc(1, sizeof(mpr_tbl_t));
    RETURN_ARG_UNLESS(t, 0);
    t->count = 0;
    t->alloced = 1;
    t->rec = (mpr_tbl_record)calloc(1, sizeof(mpr_tbl_record_t));
    return t;
}

void mpr_tbl_clear(mpr_tbl t)
{
    int i, j, free_vals = 1;
    for (i = 0; i < t->count; i++) {
        mpr_tbl_record rec = &t->rec[i];
        if (!(rec->flags & PROP_OWNED))
            continue;
        if (rec->key)
            free((char*)rec->key);
        if (free_vals && rec->val) {
            void *val = (rec->flags & INDIRECT) ? *rec->val : rec->val;
            if (val) {
                if (MPR_LIST == rec->type)
                    mpr_list_free(val);
                else if (MPR_OBJ != rec->type && MPR_PTR != rec->type) {
                    if ((MPR_STR == rec->type) && rec->len > 1) {
                        char **vals = (char**)val;
                        for (j = 0; j < rec->len; j++)
                            FUNC_IF(free, vals[j]);
                    }
                    free(val);
                }
            }
            if (rec->flags & INDIRECT)
                *rec->val = 0;
        }
    }
    t->count = 0;
    t->rec = realloc(t->rec, sizeof(mpr_tbl_record_t));
    t->alloced = 1;
}

void mpr_tbl_free(mpr_tbl t)
{
    mpr_tbl_clear(t);
    free(t->rec);
    free(t);
}

static mpr_tbl_record add_record_internal(mpr_tbl t, mpr_prop prop, const char *key,
                                          int len, mpr_type type, void *val, int flags)
{
    mpr_tbl_record rec;
    RETURN_ARG_UNLESS(t->count < MPR_TBL_MAX_RECORDS, NULL);
    t->count += 1;
    if (t->count > t->alloced) {
        while (t->count > t->alloced)
            t->alloced *= 2;
        t->rec = realloc(t->rec, t->alloced * sizeof(mpr_tbl_record_t));
    }
    rec = &t->rec[t->count-1];
    if (MPR_PROP_EXTRA == prop)
        flags |= MOD_ANY;
    rec->key = key ? strdup(key) : 0;
    rec->prop = prop;
    rec->len = len;
    rec->type = type;
    rec->val = val;
    rec->flags = flags;
    return rec;
}

int mpr_tbl_get_num_records(mpr_tbl t)
{
    int i, count = 0;
    mpr_tbl_record rec;
    for (i = 0; i < t->count; i++) {
        rec = &t->rec[i];
        if (!rec->val)
            continue;
        if ((rec->flags & INDIRECT) && !(*rec->val))
            continue;
        ++count;
    }
    return count;
}

mpr_tbl_record mpr_tbl_get_record(mpr_tbl t, mpr_prop prop, const char *key)
{
    mpr_tbl_record_t tmp;
    mpr_tbl_record rec = 0;
    RETURN_ARG_UNLESS(key || (MPR_PROP_UNKNOWN != prop && MPR_PROP_EXTRA != prop), 0);
    tmp.prop = prop;
    tmp.key = key;
    rec = bsearch(&tmp, t->rec, t->count, sizeof(mpr_tbl_record_t), compare_rec);
    return rec;
}

mpr_prop mpr_tbl_get_record_by_key(mpr_tbl t, const char *key, int *len, mpr_type *type,
                                   const void **val, int *pub)
{
    int found = 1;
    mpr_prop prop = mpr_prop_from_str(key);
    mpr_tbl_record rec = mpr_tbl_get_record(t, prop, key);

    if (!rec || rec->prop & PROP_REMOVE)
        found = 0;
    if (len)
        *len = found ? rec->len : 0;
    if (type)
        *type = found ? rec->type : MPR_NULL;
    if (val) {
        *val = found ? (rec->flags & INDIRECT ? *rec->val : rec->val) : NULL;
        if (found && MPR_LIST == rec->type)
            *val = mpr_list_start(mpr_list_get_cpy((mpr_list)*val));
    }
    if (pub)
        *pub = found && !(rec->flags & LOCAL_ACCESS);

    return found ? MASK_PROP_BITFLAGS(rec->prop) : MPR_PROP_UNKNOWN;
}

mpr_prop mpr_tbl_get_record_by_idx(mpr_tbl t, int prop, const char **key, int *len,
                                   mpr_type *type, const void **val, int *pub)
{
    int found = 1;
    int i, j = 0;
    mpr_tbl_record rec = 0;

    if (MASK_PROP_BITFLAGS(prop)) {
        /* use as mpr_prop instead of numerical index */
        rec = mpr_tbl_get_record(t, prop, NULL);
    }
    else {
        prop &= 0xFF;
        if (prop < t->count && t->count > 0) {
            for (i = 0; i < t->count; i++) {
                rec = &t->rec[i];
                if (!rec->val || ((rec->flags & INDIRECT) && !(*rec->val)))
                    continue;
                if (j == prop)
                    break;
                ++j;
            }
            if (i == t->count)
                rec = 0;
        }
    }

    if (!rec || rec->prop & PROP_REMOVE)
        found = 0;
    if (key)
        *key = found ? (rec->key ? rec->key : mpr_prop_as_str(rec->prop, 1)) : NULL;
    if (len)
        *len = found ? rec->len : 0;
    if (type)
        *type = found ? rec->type : MPR_NULL;
    if (val) {
        *val = found ? (rec->flags & INDIRECT ? *rec->val : rec->val) : NULL;
        if (found && MPR_LIST == rec->type)
            *val = mpr_list_start(mpr_list_get_cpy((mpr_list)*val));
    }
    if (pub)
        *pub = found && !(rec->flags & LOCAL_ACCESS);

    return found ? MASK_PROP_BITFLAGS(rec->prop) : MPR_PROP_UNKNOWN;
}

int mpr_tbl_get_record_is_writable(mpr_tbl t, mpr_prop prop)
{
    if (MPR_PROP_EXTRA == prop)
        return 1;
    mpr_tbl_record rec = mpr_tbl_get_record(t, prop, NULL);
    return rec ? (rec->flags & MOD_ANY) : 1;
}

int mpr_tbl_remove_record(mpr_tbl t, mpr_prop prop, const char *key, int flags)
{
    int i, ret = 0;

    do {
        mpr_tbl_record rec = mpr_tbl_get_record(t, prop, key);
        RETURN_ARG_UNLESS(rec && (rec->flags & MOD_ANY) && rec->val, ret);
        prop = MASK_PROP_BITFLAGS(prop);
        if (   prop != MPR_PROP_EXTRA && prop != MPR_PROP_LINKED
            && prop != MPR_PROP_MAX && prop != MPR_PROP_MIN) {
            /* set value to null rather than removing */
            if (rec->flags & INDIRECT) {
                if (rec->val && *rec->val && rec->type != MPR_PTR) {
                    free(*rec->val);
                    *rec->val = 0;
                }
                rec->prop |= PROP_REMOVE;
                return 1;
            }
            else {
                trace("Cannot remove static property [%d] '%s'\n", prop,
                      key ? key : mpr_prop_as_str(prop, 1));
            }
            return 0;
        }

        /* Calculate its key in the records. */
        if (rec->val && rec->type != MPR_PTR) {
            if ((rec->type == MPR_STR) && rec->len > 1) {
                char **vals = (char**)rec->val;
                for (i = 0; i < rec->len; i++)
                    FUNC_IF(free, vals[i]);
            }
            free(rec->val);
            rec->val = 0;
        }
        rec->prop |= PROP_REMOVE;
        ret = 1;
    } while (prop == MPR_PROP_EXTRA && strchr(key, '*'));
    return ret;
}

void mpr_tbl_clear_empty_records(mpr_tbl t)
{
    int i, j;
    mpr_tbl_record rec;
    for (i = 0; i < t->count; i++) {
        rec = &t->rec[i];
        if (rec->val || !(rec->prop & PROP_REMOVE))
            continue;
        rec->prop &= ~PROP_REMOVE;
        if (MASK_PROP_BITFLAGS(rec->prop) != MPR_PROP_EXTRA)
            continue;
        free((char*)rec->key);
        for (j = rec - t->rec + 1; j < t->count; j++)
            t->rec[j-1] = t->rec[j];
        --t->count;
    }
}

/* For unknown reasons, strcpy crashes here with -O2, so we'll use memcpy
 * instead, which does not crash. */
static int update_elements(mpr_tbl_record rec, unsigned int len, mpr_type type, const void *val)
{
    int i, updated = 0;
    void *old_val, *new_val, *coerced = 0;
    RETURN_ARG_UNLESS(len && (rec->val || !(rec->flags & INDIRECT)), 0);
    old_val = (rec->flags & INDIRECT) ? *rec->val : rec->val;
    new_val = old_val;
    if (old_val && (type != rec->type || len != rec->len)) {
        if (rec->flags & INDIRECT || rec->prop != MPR_PROP_EXTRA) {
            coerced = calloc(1, mpr_type_get_size(rec->type) * rec->len);
            if (-1 == mpr_set_coerced(len, type, val, rec->len, rec->type, coerced)) {
                trace("could not coerce %c%d -> %c%d\n", type, len, rec->type, rec->len);
                free(coerced);
                return 0;
            }
            trace("coerced %c%d -> %c%d\n", type, len, rec->type, rec->len);
            val = coerced;
            type = rec->type;
            len = rec->len;
        }
        else {
            /* free old values */
            if (MPR_STR == rec->type && rec->len > 1) {
                for (i = 0; i < rec->len; i++)
                    free(((char**)old_val)[i]);
            }
            if (rec->len > 1 || (MPR_PTR != rec->type && MPR_GRAPH < rec->type))
                free(old_val);
            old_val = 0;
            updated = 1;
        }
    }
    switch (type) {
        case MPR_STR:
            if (1 == len) {
                if (old_val) {
                    if (strcmp((char*)old_val, (char*)val)) {
                        free((char*)old_val);
                        old_val = 0;
                        updated = 1;
                    }
                    else
                        goto done;
                }
                new_val = val ? (void*)strdup((char*)val) : 0;
            }
            else {
                const char **from = (const char**)val;
                char **to;
                if (!old_val)
                    new_val = calloc(1, sizeof(char*) * len);
                to = (char**)new_val;
                for (i = 0; i < len; i++) {
                    if (!to[i] || strcmp(from[i], to[i])) {
                        int n = strlen(from[i]);
                        to[i] = realloc(to[i], n+1);
                        memcpy(to[i], from[i], n+1);
                        updated = 1;
                    }
                }
            }
            break;
        case MPR_PTR:
        case MPR_OBJ:
        case MPR_LIST:
            if (1 == len && (!old_val || old_val != val)) {
                new_val = (void*)val;
                updated = 1;
                break;
            }
        default: {
            if (!old_val) {
                new_val = malloc(mpr_type_get_size(type) * len);
                memcpy(new_val, val, mpr_type_get_size(type) * len);
                updated = 1;
            }
            else if ((updated = memcmp(old_val, val, mpr_type_get_size(type) * len)))
                memcpy(new_val, val, mpr_type_get_size(type) * len);
        }
    }
    if (rec->flags & INDIRECT)
        *rec->val = new_val;
    else
        rec->val = new_val;
    rec->len = len;
    rec->type = type;
    rec->flags |= PROP_SET;

done:
    FUNC_IF(free, coerced);
    return updated != 0;
}

static int set_internal(mpr_tbl t, mpr_prop prop, const char *key, int len,
                        mpr_type type, const void *val, int flags)
{
    int updated = 0;
    mpr_tbl_record rec = mpr_tbl_get_record(t, prop, key);
    if (rec) {
        if (!(rec->flags & MOD_ANY)) {
            trace("Property [%d] '%s' is not writable.\n", MASK_PROP_BITFLAGS(rec->prop),
                  rec->key ? rec->key : mpr_prop_as_str(MASK_PROP_BITFLAGS(rec->prop), 1));
            return 0;

        }
        if (prop & PROP_REMOVE) {
            if (!val)
                return mpr_tbl_remove_record(t, prop, key, flags);
        }
        else
            rec->prop &= ~PROP_REMOVE;
        updated = t->dirty = update_elements(rec, len, type, val);
    }
    else {
        /* Need to add a new entry. */
        if (!(rec = add_record_internal(t, prop, key, 0, type, 0, flags | PROP_OWNED)))
            return 0;
        if (val)
            update_elements(rec, len, type, val);
        else
            rec->prop |= PROP_REMOVE;
        qsort(t->rec, t->count, sizeof(mpr_tbl_record_t), compare_rec);
        updated = t->dirty = 1;
    }
    return updated;
}

/* Higher-level interface, where table stores arbitrary arguments along with their type. */
int mpr_tbl_add_record(mpr_tbl t, int prop, const char *key, int len,
                       mpr_type type, const void *args, int flags)
{
    if (!args && !(flags & MOD_REMOTE))
        return mpr_tbl_remove_record(t, prop, key, flags);
    return set_internal(t, prop, key, len, type, args, flags);
}

void mpr_tbl_link_value(mpr_tbl t, mpr_prop prop, int len, mpr_type type, void *val, int flags)
{
    add_record_internal(t, prop, NULL, len, type, val, flags | PROP_SET);
}

void mpr_tbl_link_value_no_default(mpr_tbl t, mpr_prop prop, int len, mpr_type type, void *val,
                                   int flags)
{
    add_record_internal(t, prop, NULL, len, type, val, flags);
}

static int update_elements_osc(mpr_tbl_record rec, unsigned int len,
                               const mpr_type *types, lo_arg **args)
{
    int i, size, updated;
    mpr_type type;
    void *val;
    RETURN_ARG_UNLESS(len, 0);
    if (MPR_STR == types[0] && 1 == len)
        return update_elements(rec, 1, MPR_STR, &args[0]->s);

    type = types[0];
    size = mpr_type_get_size(types[0]) * len;
    val = malloc(size);

    switch (type) {
        case MPR_STR:
            for (i = 0; i < len; i++)
                ((char**)val)[i] = &args[i]->s;
            break;
        case MPR_FLT:
            for (i = 0; i < len; i++)
                ((float*)val)[i] = args[i]->f;
            break;
        case MPR_INT32:
            for (i = 0; i < len; i++)
                ((int32_t*)val)[i] = args[i]->i32;
            break;
        case MPR_DBL:
            for (i = 0; i < len; i++)
                ((double*)val)[i] = args[i]->d;
            break;
        case MPR_INT64:
            for (i = 0; i < len; i++)
                ((int64_t*)val)[i] = args[i]->h;
            break;
        case MPR_TIME:
            for (i = 0; i < len; i++)
                ((mpr_time*)val)[i] = args[i]->t;
            break;
        case MPR_TYPE:
            for (i = 0; i < len; i++)
                ((mpr_type*)val)[i] = args[i]->c;
            break;
        case 'T':
        case 'F':
            for (i = 0; i < len; i++)
                ((int32_t*)val)[i] = (types[i] == 'T');
            type = MPR_BOOL;
            break;
        default:
            free(val);
            return 0;
            break;
    }

    updated = update_elements(rec, len, type, val);
    free(val);
    return updated;
}

/* Higher-level interface, where table stores arbitrary OSC arguments
 * parsed from a mpr_msg along with their type. */
int mpr_tbl_add_record_from_msg_atom(mpr_tbl t, mpr_msg_atom atom, int flags)
{
    int updated = 0, prop = mpr_msg_atom_get_prop(atom), len = mpr_msg_atom_get_len(atom);
    const char *key = mpr_msg_atom_get_key(atom);
    mpr_tbl_record rec;

    if (prop & PROP_REMOVE)
        return mpr_tbl_remove_record(t, prop, key, flags);

    rec = mpr_tbl_get_record(t, prop, key);
    if (rec)
        updated = t->dirty = update_elements_osc(rec, len, mpr_msg_atom_get_types(atom),
                                                 mpr_msg_atom_get_values(atom));
    else {
        /* Need to add a new entry. */
        const mpr_type *types = mpr_msg_atom_get_types(atom);
        if (!(rec = add_record_internal(t, prop, key, 0, types[0], 0, flags | PROP_OWNED)))
            return 0;
        rec->val = 0;
        update_elements_osc(rec, len, types, mpr_msg_atom_get_values(atom));
        qsort(t->rec, t->count, sizeof(mpr_tbl_record_t), compare_rec);
        updated = t->dirty = 1;
    }
    return updated;
}

#define LO_MESSAGE_ADD_VEC(MSG, TYPE, CAST, VAL)    \
for (i = 0; i < len; i++)                           \
    lo_message_add_##TYPE(MSG, ((CAST*)VAL)[i]);    \

/* helper for mpr_msg_varargs() */
static void _add_typed_val(lo_message msg, int len, mpr_type type, const void *val)
{
    int i;
    if (type && len < 1)
        return;

    switch (type) {
        case MPR_STR:
            if (len == 1)   lo_message_add_string(msg, (char*)val);
            else            LO_MESSAGE_ADD_VEC(msg, string, char*, val);        break;
        case MPR_FLT:       LO_MESSAGE_ADD_VEC(msg, float, float, val);         break;
        case MPR_DBL:       LO_MESSAGE_ADD_VEC(msg, double, double, val);       break;
        case MPR_INT32:     LO_MESSAGE_ADD_VEC(msg, int32, int, val);           break;
        case MPR_INT64:     LO_MESSAGE_ADD_VEC(msg, int64, int64_t, val);       break;
        case MPR_TIME:      LO_MESSAGE_ADD_VEC(msg, timetag, mpr_time, val);    break;
        case MPR_TYPE:      LO_MESSAGE_ADD_VEC(msg, char, mpr_type, val);       break;
        case 0:             lo_message_add_nil(msg);                            break;
        case MPR_BOOL:
            for (i = 0; i < len; i++) {
                if (((int*)val)[i])
                    lo_message_add_true(msg);
                else
                    lo_message_add_false(msg);
            }
            break;
        default:
            break;
    }
}

static void mpr_record_add_to_msg(mpr_tbl_record rec, lo_message msg)
{
    char temp[256];
    int len = 0, indirect, masked;
    void *val;
    mpr_list list = NULL;
    RETURN_UNLESS(!(rec->flags & LOCAL_ACCESS));
    RETURN_UNLESS(rec->flags & PROP_SET);

    indirect = rec->flags & INDIRECT;
    masked = MASK_PROP_BITFLAGS(rec->prop);
    val = rec->val ? (indirect ? *rec->val : rec->val) : NULL;

    if (MPR_LIST == rec->type) {
        /* use a copy of the list */
        list = mpr_list_get_cpy((mpr_list)val);
        if (!list) {
            trace("skipping empty list property '%s'\n",
                  rec->key ? rec->key : mpr_prop_as_str(masked, 1));
            return;
        }
        list = mpr_list_start(list);
    }

    DONE_UNLESS(val || masked == MPR_PROP_EXTRA);
    DONE_UNLESS(masked != MPR_PROP_DEV && masked != MPR_PROP_SIG && masked != MPR_PROP_SLOT);

    if (rec->prop & PROP_ADD) {
        snprintf(temp, 256, "+");
        ++len;
    }
    else if (rec->prop & PROP_REMOVE) {
        snprintf(temp, 256, "-");
        ++len;
    }
    if (rec->prop & DST_SLOT_PROP) {
        snprintf(temp + len, 256 - len, "@dst");
        len += 4;
    }
    else if (rec->prop >> SRC_SLOT_PROP_BIT_OFFSET) {
        snprintf(temp + len, 256 - len, "@src.%d", SRC_SLOT(rec->prop));
        len = strlen(temp);
    }

    if (masked < 0 || masked > MPR_PROP_EXTRA) {
        trace("skipping malformed property.\n");
        goto done;
    }
    if (masked == MPR_PROP_EXTRA) {
        snprintf(temp + len, 256 - len, "@%s", rec->key);
        len = strlen(temp);
    }
    else
        snprintf(temp + len, 256 - len, "%s", mpr_prop_as_str(masked, 0));
    if (len)
        lo_message_add_string(msg, temp);
    else {
        /* can use static string */
        lo_message_add_string(msg, mpr_prop_as_str(masked, 0));
    }
    DONE_UNLESS(val && rec->len);
    /* add value */
    switch (masked) {
        case MPR_PROP_DIR: {
            if (rec->type == MPR_INT32) {
                int dir = *(int*)rec->val;
                lo_message_add_string(msg, dir == MPR_DIR_OUT ? "output" : "input");
            }
            else
                _add_typed_val(msg, rec->len, rec->type, indirect ? *rec->val : rec->val);
            break;
        }
        case MPR_PROP_PROCESS_LOC:
            if (rec->type == MPR_INT32)
                lo_message_add_string(msg, mpr_loc_as_str(*(int*)rec->val));
            else
                _add_typed_val(msg, rec->len, rec->type, indirect ? *rec->val : rec->val);
            break;
        case MPR_PROP_PROTOCOL:
            if (rec->type == MPR_INT32)
                lo_message_add_string(msg, mpr_proto_as_str(*(int*)rec->val));
            else
                _add_typed_val(msg, rec->len, rec->type, indirect ? *rec->val : rec->val);
            break;
        case MPR_PROP_STEAL_MODE:
            if (rec->type == MPR_INT32)
                lo_message_add_string(msg, mpr_steal_type_as_str(*(int*)rec->val));
            else
                _add_typed_val(msg, rec->len, rec->type, indirect ? *rec->val : rec->val);
            break;
        case MPR_PROP_DEV:
        case MPR_PROP_SIG:
        case MPR_PROP_SLOT:
            /* do nothing */
            break;
        case MPR_PROP_SCOPE:
        case MPR_PROP_LINKED: {
            if (MPR_LIST == rec->type) {
                if (!list) {
                    lo_message_add_string(msg, "none");
                    break;
                }
                while (list) {
                    const char *key = mpr_obj_get_prop_as_str((mpr_obj)*list, MPR_PROP_NAME, NULL);
                    lo_message_add_string(msg, key);
                    list = mpr_list_get_next(list);
                }
                break;
            }
            /* else continue to _add_typed_val */
        }
        default:
            _add_typed_val(msg, rec->len, rec->type, indirect ? *rec->val : rec->val);
            break;
    }
  done:
    if (list)
        mpr_list_free(list);
}

void mpr_tbl_add_to_msg(mpr_tbl tbl, mpr_tbl new, lo_message msg)
{
    int i;
    /* add all the updates */
    if (new) {
        for (i = 0; i < new->count; i++)
            mpr_record_add_to_msg(&new->rec[i], msg);
    }
    RETURN_UNLESS(tbl);
    /* add remaining records */
    for (i = 0; i < tbl->count; i++) {
        /* check if updated version exists */
        if (!new || !mpr_tbl_get_record(new, tbl->rec[i].prop, tbl->rec[i].key))
            mpr_record_add_to_msg(&tbl->rec[i], msg);
    }
}

int mpr_tbl_get_is_dirty(mpr_tbl tbl)
{
    return tbl->dirty;
}

void mpr_tbl_set_is_dirty(mpr_tbl tbl, int dirty)
{
    tbl->dirty = dirty;
}

int mpr_tbl_get_prop_is_set(mpr_tbl tbl, mpr_prop prop)
{
    mpr_tbl_record rec = mpr_tbl_get_record(tbl, prop, NULL);
    return rec && (rec->flags & PROP_SET);
}

void mpr_tbl_set_prop_is_set(mpr_tbl tbl, mpr_prop prop)
{
    mpr_tbl_record rec = mpr_tbl_get_record(tbl, prop, NULL);
    rec->flags |= PROP_SET;
}

#ifdef DEBUG
static const char *type_name(mpr_type type)
{
    switch (type) {
        case MPR_DEV:       return "MPR_DEV";
        case MPR_SIG_IN:    return "MPR_SIG_IN";
        case MPR_SIG_OUT:   return "MPR_SIG_OUT";
        case MPR_SIG:       return "MPR_SIG";
        case MPR_MAP_IN:    return "MPR_MAP_IN";
        case MPR_MAP_OUT:   return "MPR_MAP_OUT";
        case MPR_MAP:       return "MPR_MAP";
        case MPR_OBJ:       return "MPR_OBJ";
        case MPR_LIST:      return "MPR_LIST";
        case MPR_BOOL:      return "MPR_BOOL";
        case MPR_TYPE:      return "MPR_TYPE";
        case MPR_DBL:       return "MPR_DBL";
        case MPR_FLT:       return "MPR_FLT";
        case MPR_INT64:     return "MPR_INT64";
        case MPR_INT32:     return "MPR_INT32";
        case MPR_STR:       return "MPR_STR";
        case MPR_TIME:      return "MPR_TIME";
        case MPR_PTR:       return "MPR_PTR";
        case MPR_NULL:      return "MPR_NULL";
        default:            return "unknown type!";
    }
}

void mpr_tbl_print_record(mpr_tbl_record rec)
{
    if (!rec) {
        printf("error: no record found");
        return;
    }
    printf("%p : %d ", rec->val, rec->prop);
    if (rec->key)
        printf("'%s' ", rec->key);
    else
        printf("(%s) ", mpr_prop_as_str(rec->prop, 1));
    if (rec->prop & PROP_REMOVE)
        printf("[REMOVED]");
    else if (rec->flags & (INDIRECT | PROP_OWNED)) {
        printf("[");
        if (rec->flags & INDIRECT)
            printf("INDIRECT%s", rec->flags & PROP_OWNED ? ", " : "");
        if (rec->flags & PROP_OWNED)
            printf("OWNED");
        printf("]");
    }
    printf(": %s[%d] : ", type_name(rec->type), rec->len);
    void *val = (rec->flags & INDIRECT) ? *rec->val : rec->val;
    mpr_prop_print(rec->len, rec->type, val);
}

void mpr_tbl_print(mpr_tbl t)
{
    printf("<table %p with %d records>\n", t, t->count);
    mpr_tbl_record rec = t->rec;
    int i;
    for (i = 0; i < t->count; i++) {
        printf("  ");
        mpr_tbl_print_record(rec);
        printf("\n");
        ++rec;
    }
}
#endif
