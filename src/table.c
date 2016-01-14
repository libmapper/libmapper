
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mapper_internal.h"

// we will sort so that indexed records come before keyed records
static int compare_records(const void *l, const void *r)
{
    mapper_table_record_t *rec_l = (mapper_table_record_t*)l;
    mapper_table_record_t *rec_r = (mapper_table_record_t*)r;
    if ((rec_l->index == AT_EXTRA) && (rec_r->index == AT_EXTRA)) {
        const char *str_l = rec_l->key, *str_r = rec_r->key;
        if (str_l[0] == '@')
            ++str_l;
        if (str_r[0] == '@')
            ++str_r;
        return strcmp(str_l, str_r);
    }
    if (rec_l->index == AT_EXTRA)
        return 1;
    if (rec_r->index == AT_EXTRA)
        return -1;
    return rec_l->index - rec_r->index;
}

mapper_table mapper_table_new()
{
    mapper_table tab = (mapper_table)malloc(sizeof(mapper_table_t));
    if (!tab) return 0;
    tab->num_records = 0;
    tab->alloced = 1;
    tab->records = (mapper_table_record_t*)malloc(sizeof(mapper_table_record_t));
    return tab;
}

void table_clear(mapper_table tab)
{
    int i, j, free_values = 1;
    for (i = 0; i < tab->num_records; i++) {
        mapper_table_record_t *rec = &tab->records[i];
        if (!(rec->flags & PROP_OWNED))
            continue;
        if (rec->key)
            free((char*)rec->key);
        if (free_values && rec->value) {
            void *value = (rec->flags & INDIRECT) ? *rec->value : rec->value;
            if (value) {
                if ((rec->type == 's' || rec->type == 'S') && rec->length > 1) {
                    char **vals = (char**)value;
                    for (j = 0; j < rec->length; j++) {
                        if (vals[j])
                            free(vals[j]);
                    }
                }
                free(value);
            }
            if (rec->flags & INDIRECT)
                *rec->value = 0;
        }
    }
    tab->num_records = 0;
    tab->records = realloc(tab->records, sizeof(mapper_table_record_t));
    tab->alloced = 1;
}

void mapper_table_free(mapper_table tab)
{
    table_clear(tab);
    free(tab->records);
    free(tab);
}

static mapper_table_record_t *mapper_table_add(mapper_table tab,
                                               mapper_property_t index,
                                               const char *key, int length,
                                               char type, void *value,
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
    if (key) {
        size_t len = strlen(key) + 2;
        rec->key = malloc(len);
        snprintf((char*)rec->key, len, "@%s", key[0] == '@' ? key + 1 : key);
    }
    else
        rec->key = 0;
    rec->index = index;
    rec->length = length;
    rec->type = type;
    rec->value = value;
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
        if (!rec->value)
            continue;
        if ((rec->flags & INDIRECT) && !(*rec->value))
            continue;
        ++count;
    }
    return count;
}

mapper_table_record_t *mapper_table_record(mapper_table tab,
                                           mapper_property_t index,
                                           const char *key)
{
    mapper_table_record_t tmp;
    tmp.index = index;
    tmp.key = key;
    mapper_table_record_t *rec = 0;
    rec = bsearch(&tmp, tab->records, tab->num_records,
                  sizeof(mapper_table_record_t), compare_records);
    return rec;
}

int mapper_table_property(mapper_table tab, const char *name, int *length,
                          char *type, const void **value)
{
    mapper_property_t prop = mapper_property_from_string(name);
    mapper_table_record_t *rec = mapper_table_record(tab, prop, name);
    if (!rec || !rec->value)
        return -1;
    if ((rec->flags & INDIRECT) && !(*rec->value))
        return -1;
    if (length)
        *length = rec->length;
    if (type)
        *type = rec->type;
    if (value)
        *value = rec->flags & INDIRECT ? *rec->value : rec->value;
    return 0;
}

int mapper_table_property_index(mapper_table tab, unsigned int index,
                                const char **name, int *length, char *type,
                                const void **value)
{
    if (index >= tab->num_records)
        return -1;
    int i, j = 0;
    mapper_table_record_t *rec;
    for (i = 0; i < tab->num_records; i++) {
        rec = &tab->records[i];
        if (!rec->value)
            continue;
        if ((rec->flags & INDIRECT) && !(*rec->value))
            continue;
        if (j == index)
            break;
        ++j;
    }
    if (i == tab->num_records)
        return -1;

    if (name)
        *name = rec->key ? rec->key + 1 : mapper_property_string(rec->index);
    if (length)
        *length = rec->length;
    if (type)
        *type = rec->type;
    if (value)
        *value = rec->flags & INDIRECT ? *rec->value : rec->value;
    return 0;
}

int mapper_table_remove_record(mapper_table tab, mapper_property_t index,
                               const char *key, int flags, int free_value)
{
    mapper_table_record_t *rec = mapper_table_record(tab, index, key);
    if (!rec || !(rec->flags & MODIFIABLE) || !rec->value)
        return 0;

    if (index != AT_EXTRA) {
        // set value to null rather than removing key
        if (rec->flags & INDIRECT) {
            if (rec->value && *rec->value) {
                free(*rec->value);
                *rec->value = 0;
            }
        }
        else {
            trace("Cannot remove static property [%d] '%s'\n", index,
                  key ?: mapper_property_string(index));
        }
        return 0;
    }

    /* Calculate its index in the records. */
    int i;
    index = rec - tab->records;

    free((char*)rec->key);
    if (free_value && rec->value) {
        if ((rec->type == 's' || rec->type == 'S') && rec->length > 1) {
            char **vals = (char**)rec->value;
            int i;
            for (i = 0; i < rec->length; i++) {
                if (vals[i])
                    free(vals[i]);
            }
        }
        free(rec->value);
    }

    for (i = index + 1; i < tab->num_records; i++)
        tab->records[i-1] = tab->records[i];
    --tab->num_records;

    return 0;
}

static int is_value_different(mapper_table_record_t *rec, int length, char type,
                              const void *value)
{
    int i;
    void *recval;
    if (rec->length != length || rec->type != type)
        return 1;

    recval = (rec->flags & INDIRECT) ? *rec->value : rec->value;
    if (!recval ^ !value)
        return 1;

    switch (type) {
        case 's':
            if (length == 1)
                return strcmp((char*)recval, (char*)value);
            else {
                char **l = (char**)recval;
                char **r = (char**)value;
                for (i = 0; i < length; i++) {
                    if (strcmp(l[i], r[i]))
                        return 1;
                }
            }
            break;
        case 'i':
        case 'b': {
            int32_t *l = (int32_t*)recval;
            int32_t *r = (int32_t*)value;
            for (i = 0; i < length; i++) {
                if (l[i] != r[i])
                    return 1;
            }
            break;
        }
        case 'f': {
            float *l = (float*)recval;
            float *r = (float*)value;
            for (i = 0; i < length; i++) {
                if (l[i] != r[i])
                    return 1;
            }
            break;
        }
        case 'd': {
            double *l = (double*)recval;
            double *r = (double*)value;
            for (i = 0; i < length; i++) {
                if (l[i] != r[i])
                    return 1;
            }
            break;
        }
        case 'h':
        case 't': {
            int64_t *l = (int64_t*)recval;
            int64_t *r = (int64_t*)value;
            for (i = 0; i < length; i++) {
                if (l[i] != r[i])
                    return 1;
            }
            break;
        }
        case 'c': {
            char *l = (char*)recval;
            char *r = (char*)value;
            for (i = 0; i < length; i++) {
                if (l[i] != r[i])
                    return 1;
            }
            break;
        }
        case 'v':
            if (length == 1)
                return recval != value;
            else {
                void **l = (void**)recval;
                void **r = (void**)value;
                for (i = 0; i < length; i++) {
                    if (l[i] != r[i])
                        return 1;
                }
            }
            break;
    }
    return 0;
}

static void update_value_elements(mapper_table_record_t *rec, int length,
                                  char type, const void *args)
{
    /* For unknown reasons, strcpy crashes here with -O2, so
     * we'll use memcpy instead, which does not crash. */

    int i, realloced = 0;
    if (length < 1)
        return;

    int indirect = rec->flags & INDIRECT;
    void *value;
    if (indirect || length > 1 || is_ptr_type(type)) {
        realloced = 1;
        value = malloc(mapper_type_size(type) * length);
    }
    else {
        if (length != rec->length || type != rec->type)
            rec->value = realloc(rec->value, mapper_type_size(type) * length);
        value = rec->value;
    }

    /* If destination is a string, reallocate and copy the new
     * string, otherwise just copy over the old value. */
    if (type == 's' || type == 'S') {
        if (length == 1) {
            if (value)
                free(value);
            value = args ? strdup((char*)args) : 0;
        }
        else {
            const char **from = (const char**)args;
            char **to = (char**)value;
            for (i = 0; i < length; i++) {
                int n = strlen(from[i]);
                to[i] = malloc(n+1);
                memcpy(to[i], from[i], n+1);
            }
        }
    } else {
        switch (type) {
            case 'f': {
                float *from = (float*)args;
                float *to = (float*)value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 'i':
            case 'b': {
                int32_t *from = (int32_t*)args;
                int32_t *to = (int32_t*)value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 'd': {
                double *from = (double*)args;
                double *to = (double*)value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 'h': {
                int64_t *from = (int64_t*)args;
                int64_t *to = (int64_t*)value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 't': {
                mapper_timetag_t *from = (mapper_timetag_t*)args;
                mapper_timetag_t *to = (mapper_timetag_t*)value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 'c': {
                char *from = (char*)args;
                char *to = (char*)value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 'v': {
                if (length == 1)
                    value = (void*)args;
                else {
                    void **from = (void**)args;
                    void **to = (void**)value;
                    for (i = 0; i < length; i++)
                        to[i] = from[i];
                }
                break;
            }
            default:
                break;
        }
    }

    if (realloced) {
        void *old_val = indirect ? *rec->value : rec->value;
        if (indirect)
            *rec->value = value;
        else
            rec->value = value;
        if (old_val) {
            if (rec->type == 's' && rec->length > 1) {
                // free elements
                for (i = 0; i < rec->length; i++) {
                    if (((char**)old_val)[i])
                        free(((char**)old_val)[i]);
                }
            }
            free(old_val);
        }
    }
    rec->length = length;
    rec->type = type;
}

int set_record_internal(mapper_table tab, mapper_property_t index,
                        const char *key, int length, char type,
                        const void *value, int flags)
{
    mapper_table_record_t *rec = mapper_table_record(tab, index, key);

    if (rec) {
        if (!is_value_different(rec, length, type, value))
            return 0;
        update_value_elements(rec, length, type, value);
        return 1;
    }
    else {
        /* Need to add a new entry. */
        rec = mapper_table_add(tab, index, key, 0, type, 0, flags | PROP_OWNED);
        update_value_elements(rec, length, type, value);
        table_sort(tab);
        return 1;
    }
    return 0;
}

/* Higher-level interface, where table stores arbitrary arguments along
 * with their type. */
int mapper_table_set_record(mapper_table tab, mapper_property_t index,
                            const char *key, int length, char type,
                            const void *args, int flags)
{
    if (!args)
        return mapper_table_remove_record(tab, index, key, flags, 1);
    return set_record_internal(tab, index, key, length, type, args, flags);
}

void mapper_table_link_value(mapper_table tab, mapper_property_t index,
                             int length, char type, void *value, int flags)
{
    mapper_table_add(tab, index, NULL, length, type, value, flags);
}

static int is_value_different_osc(mapper_table_record_t *rec, int length,
                                  const char *types, lo_arg **args)
{
    int i;
    void *recval;
    if (rec->length != length || !type_match(rec->type, types[0]))
        return 1;
    
    recval = (rec->flags & INDIRECT) ? *rec->value : rec->value;
    if (!recval)
        return 1;
    
    switch (types[0]) {
        case 's':
            if (length == 1)
                return strcmp((char*)recval, &args[0]->s);
            else {
                char **l = (char**)recval;
                char **r = (char**)args;
                for (i = 0; i < length; i++) {
                    if (strcmp(l[i], r[i]))
                        return 1;
                }
            }
            break;
        case 'i': {
            int32_t *vals = (int32_t*)recval;
            for (i = 0; i < length; i++) {
                if (vals[i] != args[i]->i32)
                    return 1;
            }
            break;
        }
        case 'T':
        case 'F': {
            int32_t *vals = (int32_t*)recval;
            for (i = 0; i < length; i++) {
                if (vals[i] ^ (types[i] == 'T'))
                    return 1;
            }
            break;
        }
        case 'f': {
            float *vals = (float*)recval;
            for (i = 0; i < length; i++) {
                if (vals[i] != args[i]->f)
                    return 1;
            }
            break;
        }
        case 'd': {
            double *vals = (double*)recval;
            for (i = 0; i < length; i++) {
                if (vals[i] != args[i]->d)
                    return 1;
            }
            break;
        }
        case 'h':
        case 't': {
            int64_t *vals = (int64_t*)recval;
            for (i = 0; i < length; i++) {
                if (vals[i] != args[i]->h)
                    return 1;
            }
            break;
        }
        case 'c': {
            char *vals = (char*)recval;
            for (i = 0; i < length; i++) {
                if (vals[i] != args[i]->c)
                    return 1;
            }
            break;
        }
    }
    return 0;
}

static void update_value_elements_osc(mapper_table_record_t *rec, int length,
                                      const char *types, lo_arg **args,
                                      int indirect)
{
    /* For unknown reasons, strcpy crashes here with -O2, so
     * we'll use memcpy instead, which does not crash. */

    int i, realloced = 0;
    if (length < 1)
        return;

    void *value;
    if (indirect || length > 1 || is_ptr_type(types[0])) {
        realloced = 1;
        value = malloc(mapper_type_size(types[0]) * length);
    }
    else {
        if (length != rec->length || !type_match(types[0], rec->type))
            rec->value = realloc(rec->value, mapper_type_size(types[0]) * length);
        value = rec->value;
    }

    /* If destination is a string, reallocate and copy the new
     * string, otherwise just copy over the old value. */
    if (types[0] == 's' || types[0] == 'S') {
        if (length == 1) {
            if (value)
                free(value);
            value = strdup((char*)&args[0]->s);
        }
        else if (length > 1) {
            char **to = (char**)value;
            char **from = (char**)args;
            for (i = 0; i < length; i++) {
                int n = strlen(from[i]);
                to[i] = malloc(n+1);
                memcpy(to[i], from[i], n+1);
            }
        }
    } else {
        switch (types[0]) {
            case 'f': {
                float *vals = (float*)value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->f;
                break;
            }
            case 'i': {
                int32_t *vals = (int32_t*)value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->i32;
                break;
            }
            case 'd': {
                double *vals = (double*)value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->d;
                break;
            }
            case 'h': {
                int64_t *vals = (int64_t*)value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->h;
                break;
            }
            case 't': {
                mapper_timetag_t *vals = (mapper_timetag_t*)value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->t;
                break;
            }
            case 'c': {
                char *vals = (char*)value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->c;
                break;
            }
            case 'T':
            case 'F': {
                int32_t *vals = (int32_t*)value;
                for (i = 0; i < length; i++) {
                    vals[i] = (types[i] == 'T');
                }
                break;
            }
            default:
                break;
        }
    }

    if (realloced) {
        void *old_val = indirect ? *rec->value : rec->value;
        if (indirect)
            *rec->value = value;
        else
            rec->value = value;
        if (old_val) {
            if (rec->type == 's' && rec->length > 1) {
                // free elements
                for (i = 0; i < rec->length; i++) {
                    if (((char**)old_val)[i])
                        free(((char**)old_val)[i]);
                }
            }
            free(old_val);
        }
    }

    rec->length = length;
    if (types[0] == 'T' || types[0] == 'F')
        rec->type = 'b';
    else
        rec->type = types[0];
}

/* Higher-level interface, where table stores arbitrary OSC arguments
 * parsed from a mapper_message along with their type. */
int mapper_table_set_record_from_atom(mapper_table tab,
                                      mapper_message_atom atom, int flags)
{
    mapper_table_record_t *rec = mapper_table_record(tab, atom->index,
                                                     atom->key);

    if (rec) {
        if (!is_value_different_osc(rec, atom->length, atom->types, atom->values))
            return 0;
        update_value_elements_osc(rec, atom->length, atom->types, atom->values,
                                  rec->flags & INDIRECT);
        return 1;
    }
    else if (atom->index == AT_EXTRA) {
        /* Need to add a new entry. */
        rec = mapper_table_add(tab, atom->index, atom->key, 0, atom->types[0],
                               0, flags | PROP_OWNED);
        rec->value = 0;
        update_value_elements_osc(rec, atom->length, atom->types,
                                  atom->values, 0);
        table_sort(tab);
        return 1;
    }
    return 0;
}

int mapper_table_set_from_message(mapper_table tab, mapper_message msg,
                                  int flags)
{
    int i, updated = 0;
    for (i = 0; i < msg->num_atoms; i++) {
        updated += mapper_table_set_record_from_atom(tab, &msg->atoms[i], flags);
    }
    return updated;
}

#ifdef DEBUG
void mapper_table_dump(mapper_table tab)
{
    mapper_table_record_t *rec = tab->records;
    void *value;
    int i;
    for (i = 0; i < tab->num_records; i++) {
        printf("%d (%s) '%s' ", rec->index, mapper_property_string(rec->index),
               rec->key);
        if (rec->flags & (INDIRECT | PROP_OWNED)) {
            printf("[");
            if (rec->flags & INDIRECT)
                printf("INDIRECT%s", rec->flags & PROP_OWNED ? ", " : "");
            if (rec->flags & PROP_OWNED)
                printf("OWNED");
            printf("]");
        }
        printf(": ");
        value = (rec->flags & INDIRECT) ? *rec->value : rec->value;
        mapper_property_pp(rec->length, rec->type, value);
        printf("\n");
        ++rec;
    }
}
#endif
