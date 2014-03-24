
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mapper_internal.h"

int compare_node_keys(const void *l, const void *r)
{
    return strcmp(((string_table_node_t*)l)->key,
                  ((string_table_node_t*)r)->key);
}

table table_new()
{
    table t = (table)malloc(sizeof(mapper_string_table_t));
    if (!t) return 0;
    t->len = 0;
    t->alloced = 1;
    t->store = (string_table_node_t*)malloc(sizeof(string_table_node_t));
    return t;
}

void table_free(table t, int free_values)
{
    int i, j;
    for (i=0; i<t->len; i++) {
        if (t->store[i].key)
            free((char*)t->store[i].key);
        if (free_values && t->store[i].value) {
            if (t->store[i].is_prop) {
                mapper_prop_value_t *prop = t->store[i].value;
                if ((prop->type == 's' || prop->type == 'S')
                    && prop->length > 1) {
                    char **vals = (char**)prop->value;
                    for (j = 0; j < prop->length; j++) {
                        if (vals[j])
                            free(vals[j]);
                    }
                }
                free(prop->value);
            }
            free(t->store[i].value);
        }
    }
    free(t->store);
    free(t);
}

void table_add(table t, const char *key, void *value, int is_prop)
{
    t->len += 1;
    if (t->len > t->alloced) {
        while (t->len > t->alloced)
            t->alloced *= 2;
        t->store = realloc(t->store, t->alloced * sizeof(string_table_node_t));
    }
    t->store[t->len-1].key = strdup(key);
    t->store[t->len-1].value = value;
    t->store[t->len-1].is_prop = is_prop;
}

void table_sort(table t)
{
    qsort(t->store, t->len, sizeof(string_table_node_t), compare_node_keys);
}

string_table_node_t *table_find_node(table t, const char *key)
{
    string_table_node_t tmp;
    tmp.key = key;
    string_table_node_t *n = 0;
    n = bsearch(&tmp, t->store, t->len,
                sizeof(string_table_node_t), compare_node_keys);
    return n;
}

void *table_find_p(table t, const char *key)
{
    string_table_node_t tmp;
    tmp.key = key;
    string_table_node_t *n = bsearch(&tmp, t->store, t->len,
                        sizeof(string_table_node_t), compare_node_keys);
    return n ? n->value : 0;
}

void **table_find_pp(table t, const char *key)
{
    string_table_node_t tmp;
    tmp.key = key;
    string_table_node_t *n = bsearch(&tmp, t->store, t->len,
                        sizeof(string_table_node_t), compare_node_keys);
    return n ? &n->value : 0;
}

void table_remove_key(table t, const char *key, int free_value)
{
    void **v = table_find_pp(t, key);
    if (v) {
        /* Some pointer magic to jump back to the beginning of the table node. */
        string_table_node_t *n = 0;
        n = (string_table_node_t*)((char*)v-((char*)&n->value - (char*)n));

        /* Calcuate its index in the store. */
        int i, index = n - t->store;

        free((char*)n->key);
        if (free_value) {
            if (n->is_prop) {
                mapper_prop_value_t *prop = n->value;
                if (prop->value) {
                    if ((prop->type == 's' || prop->type == 'S')
                        && prop->length > 1) {
                        char **vals = (char**)prop->value;
                        for (i = 0; i < prop->length; i++) {
                            if (vals[i])
                                free(vals[i]);
                        }
                    }
                    free(prop->value);
                }
            }
            free(n->value);
        }

        for (i=index+1; i < t->len; i++)
            t->store[i-1] = t->store[i];
        t->len --;
    }
}

int table_add_or_update(table t, const char *key, void *value)
{
    void **val = table_find_pp(t, key);
    if (val) {
        *val = value;
        return 0;
    } else {
        table_add(t, key, value, 0);
        table_sort(t);
        return 1;
    }
}

void *table_value_at_index_p(table t, unsigned int index)
{
    if (index >= t->len)
        return 0;
    return t->store[index].value;
}

const char *table_key_at_index(table t, unsigned int index)
{
    if (index >= t->len)
        return 0;
    return t->store[index].key;
}

int table_size(table t)
{
    return t->len;
}

static void mapper_table_update_value_elements(mapper_prop_value_t *prop,
                                              int length, char type,
                                              void *args)
{
    /* For unknown reasons, strcpy crashes here with -O2, so
     * we'll use memcpy instead, which does not crash. */

    int i;
    if (length < 1)
        return;

    /* If destination is a string, reallocate and copy the new
     * string, otherwise just copy over the old value. */
    if (type == 's' || type == 'S')
    {
        char **from = (char**)args;
        if (length == 1) {
            free(prop->value);
            prop->value = strdup(*from);
        }
        else {
            char **to = (char**)prop->value;
            for (i = 0; i < length; i++) {
                int n = strlen(from[i]);
                to[i] = malloc(n+1);
                memcpy(to[i], from[i], n+1);
            }
        }
    } else {
        switch (type) {
            case 'f':
            {
                float *from = (float*)args;
                float *to = (float*)prop->value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 'i': {
                int32_t *from = (int32_t*)args;
                int32_t *to = (int32_t*)prop->value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 'd':
            {
                double *from = (double*)args;
                double *to = (double*)prop->value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 'h':
            {
                int64_t *from = (int64_t*)args;
                int64_t *to = (int64_t*)prop->value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 't':
            {
                mapper_timetag_t *from = (mapper_timetag_t*)args;
                mapper_timetag_t *to = (mapper_timetag_t*)prop->value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            case 'c':
            {
                char *from = (char*)args;
                char *to = (char*)prop->value;
                for (i = 0; i < length; i++)
                    to[i] = from[i];
                break;
            }
            default:
                break;
        }
    }
}

/* Higher-level interface, where table stores arbitrary arguments along
 * with their type. */
int mapper_table_add_or_update_typed_value(table t, const char *key, char type,
                                           void *args, int length)
{
    int i;
    string_table_node_t *node = table_find_node(t, key);
    
    if (node) {
        die_unless(node->value!=0, "parameter value already in "
                   "table cannot be null.\n");
        
        mapper_prop_value_t *prop = node->value;
        if ((prop->type == 's' || prop->type == 'S') && prop->length > 1) {
            char **vals = prop->value;
            for (i = 0; i < prop->length; i++) {
                free(vals[i]);
            }
        }
        prop->value = realloc(prop->value, mapper_type_size(type) * length);

        mapper_table_update_value_elements(prop, length, type, args);
        prop->length = length;
        prop->type = type;
    }
    else {
        /* Need to add a new entry. */
        mapper_prop_value_t *prop = malloc(sizeof(mapper_prop_value_t));
        prop->value = malloc(mapper_type_size(type) * length);
        prop->length = 0;
        mapper_table_update_value_elements(prop, length, type, args);
        prop->length = length;
        prop->type = type;
        
        table_add(t, key, prop, 1);
        table_sort(t);
        return 1;
    }
    return 0;
}

static void mapper_table_update_value_elements_osc(mapper_prop_value_t *prop,
                                                   int length, char type,
                                                   lo_arg **args)
{
    /* For unknown reasons, strcpy crashes here with -O2, so
     * we'll use memcpy instead, which does not crash. */

    int i;
    /* If destination is a string, reallocate and copy the new
     * string, otherwise just copy over the old value. */
    if (type == 's' || type == 'S')
    {
        if (length == 1) {
            free(prop->value);
            prop->value = strdup((char*)&args[0]->s);

        }
        else if (length > 1) {
            char **to = (char**)prop->value;
            char **from = (char**)args;
            for (i = 0; i < length; i++) {
                int n = strlen(from[i]);
                to[i] = malloc(n+1);
                memcpy(to[i], from[i], n+1);
            }
        }
    } else {
        switch (type) {
            case 'f':
            {
                float *vals = (float*)prop->value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->f;
                break;
            }
            case 'i':
            {
                int32_t *vals = (int32_t*)prop->value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->i32;
                break;
            }
            case 'd':
            {
                double *vals = (double*)prop->value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->d;
                break;
            }
            case 'h':
            {
                int64_t *vals = (int64_t*)prop->value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->h;
                break;
            }
            case 't':
            {
                mapper_timetag_t *vals = (mapper_timetag_t*)prop->value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->t;
                break;
            }
            case 'c':
            {
                char *vals = (char*)prop->value;
                for (i = 0; i < length; i++)
                    vals[i] = args[i]->c;
                break;
            }
            default:
                break;
        }
    }
}

/* Higher-level interface, where table stores arbitrary OSC arguments
 * parsed from a mapper_msg along with their type. */
int mapper_table_add_or_update_msg_value(table t, const char *key, lo_type type,
                                         lo_arg **args, int length)
{
    int i;
    string_table_node_t *node = table_find_node(t, key);

    if (node) {
        die_unless(node->value!=0, "parameter value already in "
                   "table cannot be null.\n");

        mapper_prop_value_t *prop = node->value;
        if ((prop->type == 's' || prop->type == 'S') && prop->length > 1) {
            char **vals = prop->value;
            for (i = 0; i < prop->length; i++) {
                free(vals[i]);
            }
        }
        prop->value = realloc(prop->value, mapper_type_size(type) * length);

        mapper_table_update_value_elements_osc(prop, length, type, args);
        prop->length = length;
        prop->type = type;
    }
    else {
        /* Need to add a new entry. */
        mapper_prop_value_t *prop = malloc(sizeof(mapper_prop_value_t));
        prop->value = malloc(mapper_type_size(type) * length);
        prop->length = 0;
        mapper_table_update_value_elements_osc(prop, length, type, args);
        prop->length = length;
        prop->type = type;

        table_add(t, key, prop, 1);
        table_sort(t);
        return 1;
    }
    return 0;
}

#ifdef DEBUG
void table_dump_prop_values(table t)
{
    string_table_node_t *n = t->store;
    int i;
    for (i=0; i<t->len; i++) {
        printf("%s: ", n->key);
        mapper_prop_value_t *prop = n->value;
        mapper_prop_pp(prop->type, prop->length, prop->value);
        printf("\n");
        n++;
    }
}
#endif
