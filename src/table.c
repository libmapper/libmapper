
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mapper_internal.h"

// we will sort so that indexed records come before keyed records
static int compare_records(const void *l, const void *r)
{
    mapper_table_record_t *rec_l = (mapper_table_record_t*)l;
    mapper_table_record_t *rec_r = (mapper_table_record_t*)r;
    int idx_l = MASK_PROP_BITFLAGS(rec_l->prop);
    int idx_r = MASK_PROP_BITFLAGS(rec_r->prop);
    if ((idx_l == MAPPER_PROP_EXTRA) && (idx_r == MAPPER_PROP_EXTRA)) {
        const char *str_l = rec_l->name, *str_r = rec_r->name;
        if (str_l[0] == '@')
            ++str_l;
        if (str_r[0] == '@')
            ++str_r;
        return strcmp(str_l, str_r);
    }
    if (idx_l == MAPPER_PROP_EXTRA)
        return 1;
    if (idx_r == MAPPER_PROP_EXTRA)
        return -1;
    return idx_l - idx_r;
}

mapper_table mapper_table_new()
{
    mapper_table tab = (mapper_table)calloc(1, sizeof(mapper_table_t));
    if (!tab) return 0;
    tab->num_records = 0;
    tab->alloced = 1;
    tab->records = (mapper_table_record_t*)calloc(1, sizeof(mapper_table_record_t));
    return tab;
}

void mapper_table_clear(mapper_table tab)
{
    int i, j, free_vals = 1;
    for (i = 0; i < tab->num_records; i++) {
        mapper_table_record_t *rec = &tab->records[i];
        if (!(rec->flags & PROP_OWNED))
            continue;
        if (rec->name)
            free((char*)rec->name);
        if (free_vals && rec->val) {
            void *val = (rec->flags & INDIRECT) ? *rec->val : rec->val;
            if (val) {
                if ((rec->type == MAPPER_STRING) && rec->len > 1) {
                    char **vals = (char**)val;
                    for (j = 0; j < rec->len; j++) {
                        if (vals[j])
                            free(vals[j]);
                    }
                }
                free(val);
            }
            if (rec->flags & INDIRECT)
                *rec->val = 0;
        }
    }
    tab->num_records = 0;
    tab->records = realloc(tab->records, sizeof(mapper_table_record_t));
    tab->alloced = 1;
}

void mapper_table_free(mapper_table tab)
{
    mapper_table_clear(tab);
    free(tab->records);
    free(tab);
}

static mapper_table_record_t *mapper_table_add(mapper_table tab,
                                               mapper_property prop,
                                               const char *name, int len,
                                               mapper_type type, void *val,
                                               int flags)
{
    tab->num_records += 1;
    if (tab->num_records > tab->alloced) {
        while (tab->num_records > tab->alloced)
            tab->alloced *= 2;
        tab->records = realloc(tab->records,
                               tab->alloced * sizeof(mapper_table_record_t));
    }
    mapper_table_record_t *rec = &tab->records[tab->num_records-1];
    rec->name = name ? strdup(name) : 0;
    rec->prop = prop;
    rec->len = len;
    rec->type = type;
    rec->val = val;
    rec->flags = flags;
    return rec;
}

static void table_sort(mapper_table tab)
{
    qsort(tab->records, tab->num_records, sizeof(mapper_table_record_t),
          compare_records);
}

int mapper_table_num_records(mapper_table tab)
{
    int i, count = 0;
    mapper_table_record_t *rec;
    for (i = 0; i < tab->num_records; i++) {
        rec = &tab->records[i];
        if (!rec->val)
            continue;
        if ((rec->flags & INDIRECT) && !(*rec->val))
            continue;
        ++count;
    }
    return count;
}

mapper_table_record_t *mapper_table_record(mapper_table tab, mapper_property prop,
                                           const char *name)
{
    mapper_table_record_t tmp;
    tmp.prop = prop;
    tmp.name = name;
    mapper_table_record_t *rec = 0;
    rec = bsearch(&tmp, tab->records, tab->num_records,
                  sizeof(mapper_table_record_t), compare_records);
    return rec;
}

int mapper_table_get_prop_by_name(mapper_table tab, const char *name, int *len,
                                  mapper_type *type, const void **val)
{
    int found = 1;
    mapper_property prop = mapper_prop_from_string(name);
    mapper_table_record_t *rec = mapper_table_record(tab, prop, name);

    if (!rec || !rec->val || ((rec->flags & INDIRECT) && !(*rec->val)))
        found = 0;

    if (len)
        *len = found ? rec->len : 0;
    if (type)
        *type = found ? rec->type : MAPPER_NULL;
    if (val)
        *val = found ? (rec->flags & INDIRECT ? *rec->val : rec->val) : NULL;

    return found ? rec->prop : MAPPER_PROP_UNKNOWN;
}

int mapper_table_get_prop_by_index(mapper_table tab, mapper_property prop,
                                   const char **name, int *len,
                                   mapper_type *type, const void **val)
{
    int found = 1;
    int i, j = 0;
    mapper_table_record_t *rec = 0;

    if (MASK_PROP_BITFLAGS(prop)) {
        // use as mapper_property instead of numerical index
        rec = mapper_table_record(tab, MASK_PROP_BITFLAGS(prop), NULL);
    }
    else {
        prop &= 0xFF;
        if (prop < tab->num_records && tab->num_records > 0) {
            for (i = 0; i < tab->num_records; i++) {
                rec = &tab->records[i];
                if (!rec->val || ((rec->flags & INDIRECT) && !(*rec->val)))
                    continue;
                if (j == prop)
                    break;
                ++j;
            }
            if (i == tab->num_records)
                rec = 0;
        }
    }

    if (!rec || !rec->val || ((rec->flags & INDIRECT) && !(*rec->val)))
        found = 0;

    if (name)
        *name = found ? (rec->name ? rec->name : mapper_prop_string(rec->prop)) : NULL;
    if (len)
        *len = found ? rec->len : 0;
    if (type)
        *type = found ? rec->type : MAPPER_NULL;
    if (val)
        *val = found ? (rec->flags & INDIRECT ? *rec->val : rec->val) : NULL;

    return found ? rec->prop : MAPPER_PROP_UNKNOWN;
}

int mapper_table_remove_record(mapper_table tab, mapper_property prop,
                               const char *name, int flags)
{
    mapper_table_record_t *rec = mapper_table_record(tab, prop, name);
    if (!rec || !(rec->flags & MODIFIABLE) || !rec->val)
        return 0;

    if (MASK_PROP_BITFLAGS(prop) != MAPPER_PROP_EXTRA) {
        // set value to null rather than removing name
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
                  name ?: mapper_prop_string(prop));
        }
        return 0;
    }

    /* Calculate its key in the records. */
    int i;
    if (rec->val) {
        if ((rec->type == MAPPER_STRING) && rec->len > 1) {
            char **vals = (char**)rec->val;
            for (i = 0; i < rec->len; i++) {
                if (vals[i])
                    free(vals[i]);
            }
        }
        free(rec->val);
        rec->val = 0;
    }

    rec->prop |= PROP_REMOVE;
    return 1;
}

void mapper_table_clear_empty_records(mapper_table tab)
{
    int i, j;
    mapper_table_record_t *rec;
    for (i = 0; i < tab->num_records; i++) {
        rec = &tab->records[i];
        if (rec->val || !(rec->prop & PROP_REMOVE))
            continue;
        rec->prop &= ~PROP_REMOVE;
        if (MASK_PROP_BITFLAGS(rec->prop) != MAPPER_PROP_EXTRA)
            continue;
        free((char*)rec->name);
        for (j = rec - tab->records + 1; j < tab->num_records; j++)
            tab->records[j-1] = tab->records[j];
        --tab->num_records;
    }
}

static int update_elements(mapper_table_record_t *rec, int len, mapper_type type,
                           const void *val)
{
    /* For unknown reasons, strcpy crashes here with -O2, so
     * we'll use memcpy instead, which does not crash. */

    int i, updated = 0;
    if (len < 1)
        return 0;

    void *old_val = (rec->flags & INDIRECT) ? *rec->val : rec->val;
    void *new_val = old_val;

    if (old_val && (len != rec->len || type != rec->type)) {
        // free old values
        if (MAPPER_STRING == rec->type && rec->len > 1) {
            for (i = 0; i < rec->len; i++)
                free(((char**)old_val)[i]);
        }
        free(old_val);
        old_val = 0;
        updated = 1;
    }

    if (MAPPER_STRING == type) {
        if (1 == len) {
            if (old_val && strcmp((char*)old_val, (char*)val)) {
                free((char*)old_val);
                old_val = 0;
                updated = 1;
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
        if (old_val) {
            if ((updated = memcmp(old_val, val, mapper_type_size(type) * len)))
                memcpy(new_val, val, mapper_type_size(type) * len);
        }
        else {
            new_val = malloc(mapper_type_size(type) * len);
            memcpy(new_val, val, mapper_type_size(type) * len);
        }
    }

    if (rec->flags & INDIRECT)
        *rec->val = new_val;
    else
        rec->val = new_val;
    rec->len = len;
    rec->type = type;
    return updated;
}

int set_record_internal(mapper_table tab, mapper_property prop,
                        const char *name, int len, mapper_type type,
                        const void *val, int flags)
{
    int updated = 0;
    if (!name && (MAPPER_PROP_UNKNOWN == prop || MAPPER_PROP_EXTRA == prop)) {
        return 0;
    }

    mapper_table_record_t *rec = mapper_table_record(tab, prop, name);

    if (rec) {
        if (!(rec->flags & MODIFIABLE))
            return 0;
        if (flags & PROP_REMOVE)
            return mapper_table_remove_record(tab, prop, name, flags);
        void *coerced = (void*)val;
        if (type != rec->type && (rec->flags & INDIRECT)) {
            coerced = alloca(mapper_type_size(rec->type) * rec->len);
            set_coerced_val(len, type, val, rec->len, rec->type, coerced);
        }
        updated = tab->dirty = update_elements(rec, len, type, coerced);
    }
    else {
        /* Need to add a new entry. */
        rec = mapper_table_add(tab, prop, name, 0, type, 0, flags | PROP_OWNED);
        update_elements(rec, len, type, val);
        table_sort(tab);
        updated = tab->dirty = 1;
    }
    return updated;
}

/* Higher-level interface, where table stores arbitrary arguments along
 * with their type. */
int mapper_table_set_record(mapper_table tab, mapper_property prop,
                            const char *name, int len, mapper_type type,
                            const void *args, int flags)
{
    if (!args && !(flags & REMOTE_MODIFY))
        return mapper_table_remove_record(tab, prop, name, flags);
    return set_record_internal(tab, prop, name, len, type, args, flags);
}

void mapper_table_link(mapper_table tab, mapper_property prop, int len,
                       mapper_type type, void *val, int flags)
{
    mapper_table_add(tab, prop, NULL, len, type, val, flags);
}

static int val_changed_osc(mapper_table_record_t *rec, int len,
                           const mapper_type *types, lo_arg **args)
{
    int i;
    void *recval;
    if (rec->len != len || !type_match(rec->type, types[0]))
        return 1;
    
    recval = (rec->flags & INDIRECT) ? *rec->val : rec->val;
    if (!recval)
        return 1;
    
    switch (types[0]) {
        case MAPPER_STRING:
            if (len == 1)
                return strcmp((char*)recval, &args[0]->s);
            else {
                char **l = (char**)recval;
                char **r = (char**)args;
                for (i = 0; i < len; i++) {
                    if (strcmp(l[i], r[i]))
                        return 1;
                }
            }
            break;
        case MAPPER_INT32: {
            int32_t *vals = (int32_t*)recval;
            for (i = 0; i < len; i++) {
                if (vals[i] != args[i]->i32)
                    return 1;
            }
            break;
        }
        case 'T':
        case 'F': {
            int32_t *vals = (int32_t*)recval;
            for (i = 0; i < len; i++) {
                if (vals[i] ^ (types[i] == 'T'))
                    return 1;
            }
            break;
        }
        case MAPPER_FLOAT: {
            float *vals = (float*)recval;
            for (i = 0; i < len; i++) {
                if (vals[i] != args[i]->f)
                    return 1;
            }
            break;
        }
        case MAPPER_DOUBLE: {
            double *vals = (double*)recval;
            for (i = 0; i < len; i++) {
                if (vals[i] != args[i]->d)
                    return 1;
            }
            break;
        }
        case MAPPER_INT64:
        case MAPPER_TIME: {
            int64_t *vals = (int64_t*)recval;
            for (i = 0; i < len; i++) {
                if (vals[i] != args[i]->h)
                    return 1;
            }
            break;
        }
        case MAPPER_CHAR: {
            char *vals = (char*)recval;
            for (i = 0; i < len; i++) {
                if (vals[i] != args[i]->c)
                    return 1;
            }
            break;
        }
    }
    return 0;
}

static void update_elements_osc(mapper_table_record_t *rec, int len,
                                const mapper_type *types, lo_arg **args,
                                int indirect)
{
    /* For unknown reasons, strcpy crashes here with -O2, so
     * we'll use memcpy instead, which does not crash. */

    int i, realloced = 0;
    if (len < 1)
        return;

    void *val;
    if (indirect || len > 1 || type_is_ptr(types[0])) {
        realloced = 1;
        val = malloc(mapper_type_size(types[0]) * len);
    }
    else {
        if (len != rec->len || !type_match(types[0], rec->type))
            rec->val = realloc(rec->val, mapper_type_size(types[0]) * len);
        val = rec->val;
    }

    /* If destination is a string, reallocate and copy the new
     * string, otherwise just copy over the old value. */
    if (types[0] == MAPPER_STRING) {
        if (len == 1) {
            if (val)
                free(val);
            val = strdup((char*)&args[0]->s);
        }
        else if (len > 1) {
            char **to = (char**)val;
            char **from = (char**)args;
            for (i = 0; i < len; i++) {
                int n = strlen(from[i]);
                to[i] = malloc(n+1);
                memcpy(to[i], from[i], n+1);
            }
        }
    } else {
        switch (types[0]) {
            case MAPPER_FLOAT: {
                float *vals = (float*)val;
                for (i = 0; i < len; i++)
                    vals[i] = args[i]->f;
                break;
            }
            case MAPPER_INT32: {
                int32_t *vals = (int32_t*)val;
                for (i = 0; i < len; i++)
                    vals[i] = args[i]->i32;
                break;
            }
            case MAPPER_DOUBLE: {
                double *vals = (double*)val;
                for (i = 0; i < len; i++)
                    vals[i] = args[i]->d;
                break;
            }
            case MAPPER_INT64: {
                int64_t *vals = (int64_t*)val;
                for (i = 0; i < len; i++)
                    vals[i] = args[i]->h;
                break;
            }
            case MAPPER_TIME: {
                mapper_time_t *vals = (mapper_time_t*)val;
                for (i = 0; i < len; i++)
                    vals[i] = args[i]->t;
                break;
            }
            case MAPPER_CHAR: {
                char *vals = (char*)val;
                for (i = 0; i < len; i++)
                    vals[i] = args[i]->c;
                break;
            }
            case 'T':
            case 'F': {
                int32_t *vals = (int32_t*)val;
                for (i = 0; i < len; i++) {
                    vals[i] = (types[i] == 'T');
                }
                break;
            }
            default:
                break;
        }
    }

    if (realloced) {
        void *old_val = indirect ? *rec->val : rec->val;
        if (indirect)
            *rec->val = val;
        else
            rec->val = val;
        if (old_val) {
            if (rec->type == MAPPER_STRING && rec->len > 1) {
                // free elements
                for (i = 0; i < rec->len; i++) {
                    if (((char**)old_val)[i])
                        free(((char**)old_val)[i]);
                }
            }
            free(old_val);
        }
    }

    rec->len = len;
    if (types[0] == 'T' || types[0] == 'F')
        rec->type = MAPPER_BOOL;
    else
        rec->type = types[0];
}

/* Higher-level interface, where table stores arbitrary OSC arguments
 * parsed from a mapper_msg along with their type. */
int mapper_table_set_record_from_atom(mapper_table tab, mapper_msg_atom atom,
                                      int flags)
{
    mapper_table_record_t *rec = mapper_table_record(tab, atom->prop, atom->name);

    if (atom->prop & PROP_REMOVE) {
        return mapper_table_remove_record(tab, atom->prop, atom->name, flags);
    }

    if (rec) {
        if (!val_changed_osc(rec, atom->len, atom->types, atom->vals))
            return 0;
        update_elements_osc(rec, atom->len, atom->types, atom->vals,
                            rec->flags & INDIRECT);
        tab->dirty = 1;
        return 1;
    }
    else {
        /* Need to add a new entry. */
        rec = mapper_table_add(tab, atom->prop, atom->name, 0, atom->types[0],
                               0, flags | PROP_OWNED);
        rec->val = 0;
        update_elements_osc(rec, atom->len, atom->types, atom->vals, 0);
        table_sort(tab);
        tab->dirty = 1;
        return 1;
    }
    return 0;
}

int mapper_table_set_from_msg(mapper_table tab, mapper_msg msg, int flags)
{
    int i, updated = 0;
    for (i = 0; i < msg->num_atoms; i++) {
        updated += mapper_table_set_record_from_atom(tab, &msg->atoms[i], flags);
    }
    return updated;
}

static void mapper_record_add_to_msg(mapper_table_record_t *rec, lo_message msg)
{
    int len, indirect, masked;
    char temp[256];

    if (!rec->val || !rec->len || rec->flags & LOCAL_ACCESS_ONLY)
        return;

    masked = MASK_PROP_BITFLAGS(rec->prop);
    indirect = rec->flags & INDIRECT;
    if (masked != MAPPER_PROP_EXTRA) {
        if (!rec->val || (indirect && !*rec->val))
            return;
    }

    len = 0;
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

    if (masked < 0 || masked > MAPPER_PROP_EXTRA) {
        trace("skipping malformed property.\n");
        return;
    }
    if (masked == MAPPER_PROP_EXTRA) {
        snprintf(temp + len, 256 - len, "@%s", rec->name);
        len = strlen(temp);
    }
    else {
        snprintf(temp + len, 256 - len, "%s",
                 mapper_prop_protocol_string(masked));
    }
    if (len) {
        lo_message_add_string(msg, temp);
    }
    else {
        // can use static string
        lo_message_add_string(msg, mapper_prop_protocol_string(masked));
    }
    if (rec->prop & PROP_REMOVE)
        return;
    // add value
    switch (masked) {
        case MAPPER_PROP_DIR: {
            int dir = *(int*)rec->val;
            lo_message_add_string(msg, dir == MAPPER_DIR_OUT ? "output" : "input");
            break;
        }
        case MAPPER_PROP_PROCESS_LOC: {
            int loc = *(int*)rec->val;
            lo_message_add_string(msg, mapper_loc_string(loc));
            break;
        }
        case MAPPER_PROP_PROTOCOL: {
            int pro = *(int*)rec->val;
            lo_message_add_string(msg, mapper_protocol_string(pro));
            break;
        }
        case MAPPER_PROP_DEVICE:
        case MAPPER_PROP_SIGNAL:
        case MAPPER_PROP_SLOT:
            // do nothing
            break;
        default:
            mapper_msg_add_typed_val(msg, rec->len, rec->type,
                                     indirect ? *rec->val : rec->val);
            break;
    }
}

void mapper_table_add_to_msg(mapper_table tab, mapper_table updates,
                             lo_message msg)
{
    int i;
    // add all the updates
    if (updates) {
        for (i = 0; i < updates->num_records; i++) {
            mapper_record_add_to_msg(&updates->records[i], msg);
        }
    }
    if (!tab)
        return;
    // add remaining records
    for (i = 0; i < tab->num_records; i++) {
        // check if updated version exists
        if (!updates || !mapper_table_record(updates, tab->records[i].prop,
                                             tab->records[i].name)) {
            mapper_record_add_to_msg(&tab->records[i], msg);
        }
    }
}

#ifdef DEBUG
void mapper_table_print_record(mapper_table_record_t *rec)
{
    if (!rec) {
        printf("error: no record found");
        return;
    }
    printf("%p : %d ", rec->val, rec->prop);
    if (rec->name)
        printf("'%s' ", rec->name);
    else
        printf("(%s) ", mapper_prop_string(rec->prop));
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
    mapper_prop_print(rec->len, rec->type, val);
}

void mapper_table_print(mapper_table tab)
{
    mapper_table_record_t *rec = tab->records;
    int i;
    for (i = 0; i < tab->num_records; i++) {
        mapper_table_print_record(rec);
        printf("\n");
        ++rec;
    }
}
#endif
