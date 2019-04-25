
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mpr_internal.h"

static int match_pattern(const char* s, const char* p)
{
    RETURN_UNLESS(s && p, 1);
    RETURN_UNLESS(strchr(p, '*'), strcmp(s, p));

        // 1) tokenize pattern using strtok() with delimiter character '*'
        // 2) use strstr() to check if token exists in offset string
    char *str = (char*)s, *tok;
    char dup[strlen(p)+1], *pat = dup;
    strcpy(pat, p);
    int ends_wild = ('*' == p[strlen(p)-1]);
    while (str && *str) {
        tok = strtok(pat, "*");
        RETURN_UNLESS(tok, !ends_wild);
        str = strstr(str, tok);
        if (str && *str)
            str += strlen(tok);
        else
            return 1;
            // subsequent calls to strtok() need first argument to be NULL
        pat = NULL;
    }
    return 0;
}

// we will sort so that indexed records come before keyed records
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
        return match_pattern(str_l, str_r);
    }
    if (idx_l == MPR_PROP_EXTRA)
        return 1;
    if (idx_r == MPR_PROP_EXTRA)
        return -1;
    return idx_l - idx_r;
}

mpr_tbl mpr_tbl_new()
{
    mpr_tbl t = (mpr_tbl)calloc(1, sizeof(mpr_tbl_t));
    RETURN_UNLESS(t, 0);
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
                else {
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

static mpr_tbl_record mpr_tbl_add(mpr_tbl t, mpr_prop prop, const char *key,
                                  int len, mpr_type type, void *val, int flags)
{
    t->count += 1;
    if (t->count > t->alloced) {
        while (t->count > t->alloced)
            t->alloced *= 2;
        t->rec = realloc(t->rec, t->alloced * sizeof(mpr_tbl_record_t));
    }
    mpr_tbl_record rec = &t->rec[t->count-1];
    if (MPR_PROP_EXTRA == prop)
        flags |= MODIFIABLE;
    rec->key = key ? strdup(key) : 0;
    rec->prop = prop;
    rec->len = len;
    rec->type = type;
    rec->val = val;
    rec->flags = flags;
    return rec;
}

int mpr_tbl_get_size(mpr_tbl t)
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

mpr_tbl_record mpr_tbl_get(mpr_tbl t, mpr_prop prop, const char *key)
{
    RETURN_UNLESS(key || (MPR_PROP_UNKNOWN != prop && MPR_PROP_EXTRA != prop), 0);
    mpr_tbl_record_t tmp;
    tmp.prop = prop;
    tmp.key = key;
    mpr_tbl_record rec = 0;
    rec = bsearch(&tmp, t->rec, t->count, sizeof(mpr_tbl_record_t),
                  compare_rec);
    return rec;
}

int mpr_tbl_get_prop_by_key(mpr_tbl t, const char *key, int *len, mpr_type *type,
                            const void **val, int *pub)
{
    int found = 1;
    mpr_prop prop = mpr_prop_from_str(key);
    mpr_tbl_record rec = mpr_tbl_get(t, prop, key);

    if (!rec || !rec->val || ((rec->flags & INDIRECT) && !(*rec->val)))
        found = 0;
    if (len)
        *len = found ? rec->len : 0;
    if (type)
        *type = found ? rec->type : MPR_NULL;
    if (val)
        *val = found ? (rec->flags & INDIRECT ? *rec->val : rec->val) : NULL;
    if (pub)
        *pub = rec->flags ^ LOCAL_ACCESS_ONLY;

    return found ? rec->prop : MPR_PROP_UNKNOWN;
}

int mpr_tbl_get_prop_by_idx(mpr_tbl t, mpr_prop prop, const char **key, int *len,
                            mpr_type *type, const void **val, int *pub)
{
    int found = 1;
    int i, j = 0;
    mpr_tbl_record rec = 0;

    if (MASK_PROP_BITFLAGS(prop)) {
        // use as mpr_prop instead of numerical index
        rec = mpr_tbl_get(t, MASK_PROP_BITFLAGS(prop), NULL);
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

    if (!rec || !rec->val || ((rec->flags & INDIRECT) && !(*rec->val)))
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
            *val = mpr_list_get_cpy((mpr_list)*val);
    }
    if (pub)
        *pub = rec->flags ^ LOCAL_ACCESS_ONLY;

    return found ? rec->prop : MPR_PROP_UNKNOWN;
}

int mpr_tbl_remove(mpr_tbl t, mpr_prop prop, const char *key, int flags)
{
    int ret = 0;

    do {
        mpr_tbl_record rec = mpr_tbl_get(t, prop, key);
        RETURN_UNLESS(rec && (rec->flags & MODIFIABLE) && rec->val, ret);
        prop = MASK_PROP_BITFLAGS(prop);
        if (prop != MPR_PROP_EXTRA && prop != MPR_PROP_LINKED) {
            // set value to null rather than removing
            if (rec->flags & INDIRECT) {
                if (rec->val && *rec->val) {
                    free(*rec->val);
                    *rec->val = 0;
                }
                rec->prop |= PROP_REMOVE;
                return 1;
            }
            else {
                trace("Cannot remove static property [%d] '%s'\n", prop,
                      key ?: mpr_prop_as_str(prop, 1));
            }
            return 0;
        }

        /* Calculate its key in the records. */
        int i;
        if (rec->val) {
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

void mpr_tbl_clear_empty(mpr_tbl t)
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
static int update_elements(mpr_tbl_record rec, unsigned int len, mpr_type type,
                           const void *val)
{
    RETURN_UNLESS(len, 0);
    int i, updated = 0;
    void *old_val = (rec->flags & INDIRECT) ? *rec->val : rec->val;
    void *new_val = old_val;

    if (old_val && (len != rec->len || type != rec->type)) {
        // free old values
        if (MPR_STR == rec->type && rec->len > 1) {
            for (i = 0; i < rec->len; i++)
                free(((char**)old_val)[i]);
        }
        free(old_val);
        old_val = 0;
        updated = 1;
    }

    if (MPR_STR == type) {
        if (1 == len) {
            if (old_val) {
                if (strcmp((char*)old_val, (char*)val)) {
                    free((char*)old_val);
                    old_val = 0;
                    updated = 1;
                }
                else
                    return 0;
            }
            new_val = val ? (void*)strdup((char*)val) : 0;
        }
        else {
            const char **from = (const char**)val;
            if (!old_val)
                new_val = calloc(1, sizeof(char*) * len);
            char **to = (char**)new_val;
            for (i = 0; i < len; i++) {
                if (!to[i] || strcmp(from[i], to[i])) {
                    int n = strlen(from[i]);
                    to[i] = realloc(to[i], n+1);
                    memcpy(to[i], from[i], n+1);
                    updated = 1;
                }
            }
        }
    }
    else {
        if (!old_val) {
            new_val = malloc(mpr_type_get_size(type) * len);
            memcpy(new_val, val, mpr_type_get_size(type) * len);
        }
        else if ((updated = memcmp(old_val, val, mpr_type_get_size(type) * len)))
            memcpy(new_val, val, mpr_type_get_size(type) * len);
    }

    if (rec->flags & INDIRECT)
        *rec->val = new_val;
    else
        rec->val = new_val;
    rec->len = len;
    rec->type = type;
    return updated != 0;
}

int set_internal(mpr_tbl t, mpr_prop prop, const char *key, int len,
                 mpr_type type, const void *val, int flags)
{
    int updated = 0;
    mpr_tbl_record rec = mpr_tbl_get(t, prop, key);
    if (rec) {
        RETURN_UNLESS(rec->flags & MODIFIABLE, 0);
        if (prop & PROP_REMOVE)
            return mpr_tbl_remove(t, prop, key, flags);
        if (type != rec->type && (rec->flags & INDIRECT)) {
            void *coerced = alloca(mpr_type_get_size(rec->type) * rec->len);
            set_coerced_val(len, type, val, rec->len, rec->type, coerced);
            updated = t->dirty = update_elements(rec, rec->len, rec->type, coerced);
        }
        else
            updated = t->dirty = update_elements(rec, len, type, val);
    }
    else {
        /* Need to add a new entry. */
        rec = mpr_tbl_add(t, prop, key, 0, type, 0, flags | PROP_OWNED);
        if (val)
            update_elements(rec, len, type, val);
        else
            rec->prop |= PROP_REMOVE;
        qsort(t->rec, t->count, sizeof(mpr_tbl_record_t), compare_rec);
        updated = t->dirty = 1;
    }
    return updated;
}

/* Higher-level interface, where table stores arbitrary arguments along
 * with their type. */
int mpr_tbl_set(mpr_tbl t, mpr_prop prop, const char *key, int len,
                mpr_type type, const void *args, int flags)
{
    if (!args && !(flags & REMOTE_MODIFY))
        return mpr_tbl_remove(t, prop, key, flags);
    return set_internal(t, prop, key, len, type, args, flags);
}

void mpr_tbl_link(mpr_tbl t, mpr_prop prop, int len, mpr_type type, void *val,
                  int flags)
{
    mpr_tbl_add(t, prop, NULL, len, type, val, flags);
}

static int update_elements_osc(mpr_tbl_record rec, unsigned int len,
                               const mpr_type *types, lo_arg **args)
{
    RETURN_UNLESS(len, 0);
    if (MPR_STR == types[0] && 1 == len)
        return update_elements(rec, 1, MPR_STR, &args[0]->s);

    mpr_type type = types[0];
    int i, size = mpr_type_get_size(types[0]) * len;
    void *val = malloc(size);

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
            break;
    }

    int updated = update_elements(rec, len, type, val);
    free(val);
    return updated;
}

/* Higher-level interface, where table stores arbitrary OSC arguments
 * parsed from a mpr_msg along with their type. */
int mpr_tbl_set_from_atom(mpr_tbl t, mpr_msg_atom atom, int flags)
{
    int updated = 0;
    mpr_tbl_record rec = mpr_tbl_get(t, atom->prop, atom->key);

    if (atom->prop & PROP_REMOVE)
        return mpr_tbl_remove(t, atom->prop, atom->key, flags);

    if (rec) {
        if (atom->prop & PROP_REMOVE)
            return mpr_tbl_remove(t, atom->prop, atom->key, flags);
        updated = t->dirty = update_elements_osc(rec, atom->len, atom->types,
                                                 atom->vals);
    }
    else {
        /* Need to add a new entry. */
        rec = mpr_tbl_add(t, atom->prop, atom->key, 0, atom->types[0], 0,
                          flags | PROP_OWNED);
        rec->val = 0;
        update_elements_osc(rec, atom->len, atom->types, atom->vals);
        qsort(t->rec, t->count, sizeof(mpr_tbl_record_t), compare_rec);
        updated = t->dirty = 1;
        return 1;
    }
    return updated;
}

static void mpr_record_add_to_msg(mpr_tbl_record rec, lo_message msg)
{
    RETURN_UNLESS(!(rec->flags & LOCAL_ACCESS_ONLY));
    int len = 0,
        indirect = rec->flags & INDIRECT,
        masked = MASK_PROP_BITFLAGS(rec->prop);
    char temp[256];
    void *val = rec->val ? (indirect ? *rec->val : rec->val) : NULL;

    mpr_list list = NULL;
    if (MPR_LIST == rec->type) {
        // use a copy of the list
        list = mpr_list_get_cpy((mpr_list)val);
        if (!list) {
            trace("skipping empty list property '%s'\n",
                  rec->key ?: mpr_prop_as_str(masked, 1));
            return;
        }
        list = mpr_list_start(list);
    }

    RETURN_UNLESS(val || masked == MPR_PROP_EXTRA);

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
        return;
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
        // can use static string
        lo_message_add_string(msg, mpr_prop_as_str(masked, 0));
    }
    if (rec->prop & PROP_REMOVE || !val || !rec->len)
        return;
    // add value
    switch (masked) {
        case MPR_PROP_DIR: {
            int dir = *(int*)rec->val;
            lo_message_add_string(msg, dir == MPR_DIR_OUT ? "output" : "input");
            break;
        }
        case MPR_PROP_PROCESS_LOC:
            lo_message_add_string(msg, mpr_loc_as_str(*(int*)rec->val));
            break;
        case MPR_PROP_PROTOCOL:
            lo_message_add_string(msg, mpr_protocol_as_str(*(int*)rec->val));
            break;
        case MPR_PROP_STEAL_MODE:
            lo_message_add_string(msg, mpr_steal_as_str(*(int*)rec->val));
            break;
        case MPR_PROP_DEV:
        case MPR_PROP_SIG:
        case MPR_PROP_SLOT:
            // do nothing
            break;
        case MPR_PROP_SCOPE:
        case MPR_PROP_LINKED: {
            if (!list) {
                lo_message_add_string(msg, "none");
                break;
            }
            while (list) {
                const char *key = mpr_obj_get_prop_as_str((mpr_obj)*list,
                                                          MPR_PROP_NAME, NULL);
                lo_message_add_string(msg, key);
                list = mpr_list_get_next(list);
            }
            break;
        }
        default:
            mpr_msg_add_typed_val(msg, rec->len, rec->type,
                                  indirect ? *rec->val : rec->val);
            break;
    }
    if (list)
        mpr_list_free(list);
}

void mpr_tbl_add_to_msg(mpr_tbl tbl, mpr_tbl new, lo_message msg)
{
    int i;
    // add all the updates
    if (new) {
        for (i = 0; i < new->count; i++)
            mpr_record_add_to_msg(&new->rec[i], msg);
    }
    RETURN_UNLESS(tbl);
    // add remaining records
    for (i = 0; i < tbl->count; i++) {
        // check if updated version exists
        if (!new || !mpr_tbl_get(new, tbl->rec[i].prop, tbl->rec[i].key))
            mpr_record_add_to_msg(&tbl->rec[i], msg);
    }
}

#ifdef DEBUG
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
    if (rec->flags & (INDIRECT | PROP_OWNED)) {
        printf("[");
        if (rec->flags & INDIRECT)
            printf("INDIRECT%s", rec->flags & PROP_OWNED ? ", " : "");
        if (rec->flags & PROP_OWNED)
            printf("OWNED");
        printf("]");
    }
    printf(": %c%d : ", rec->type, rec->len);
    void *val = (rec->flags & INDIRECT) ? *rec->val : rec->val;
    mpr_prop_print(rec->len, rec->type, val);
}

void mpr_tbl_print(mpr_tbl t)
{
    mpr_tbl_record rec = t->rec;
    int i;
    for (i = 0; i < t->count; i++) {
        mpr_tbl_print_record(rec);
        printf("\n");
        ++rec;
    }
}
#endif
