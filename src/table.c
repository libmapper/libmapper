
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
    int i;
    for (i=0; i<t->len; i++) {
        if (t->store[i].key)
            free((char*)t->store[i].key);
        if (free_values && t->store[i].value)
            free(t->store[i].value);
    }
    free(t->store);
    free(t);
}

void table_add(table t, const char *key, void *value)
{
    t->len += 1;
    if (t->len > t->alloced) {
        while (t->len > t->alloced)
            t->alloced *= 2;
        t->store = realloc(t->store, t->alloced*sizeof(string_table_node_t));
    }
    t->store[t->len-1].key = strdup(key);
    t->store[t->len-1].value = value;
}

void table_sort(table t)
{
    qsort(t->store, t->len, sizeof(string_table_node_t), compare_node_keys);
}

int table_find(table t, const char *key, void **value)
{
    string_table_node_t tmp;
    tmp.key = key;
    string_table_node_t *n = bsearch(&tmp, t->store, t->len,
                        sizeof(string_table_node_t), compare_node_keys);
    if (n) {
        *value = n->value;
        return 0;
    }
    else
        return 1;
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
        if (free_value)
            free(n->value);

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
        table_add(t, key, value);
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

/* Higher-level interface, where table stores arbitrary OSC arguments
 * along with their type. */

int mapper_table_add_or_update_osc_value(table t, const char *key,
                                         lo_type type, lo_arg *arg)
{
    mapper_osc_value_t **pval =
        (mapper_osc_value_t**)table_find_pp(t, key);

    if (pval) {
        die_unless((*pval)!=0, "parameter value already in "
                   "table cannot be null.\n");

        /* If destination is a string, reallocate and copy the new
         * string, otherwise just copy over the old value. */
        if (type == 's' || type == 'S')
        {
            if (((*pval)->type == 's' || (*pval)->type == 'S')
                && strcmp(&arg->s, &(*pval)->value.s)==0)
                return 0;

            int n = strlen(&arg->s);
            *pval = realloc(*pval, sizeof(mapper_osc_value_t) + n + 1);
            (*pval)->type = type;

            // For unknown reasons, strcpy crashes here with -O2, so
            // we'll use memcpy instead, which does not crash.
            memcpy(&(*pval)->value.s, &arg->s, n+1);
            return 1;
        } else {
            if ((*pval)->type == type
                && memcmp(&(*pval)->value, arg, sizeof(lo_arg))==0)
                return 0;

            (*pval)->type = type;
            if (arg)
                (*pval)->value = *arg;
            else
                (*pval)->value.h = 0;
            return 1;
        }
    }
    else {
        /* Need to add a new entry. */
        mapper_osc_value_t *val = 0;
        if (type == 's' || type == 'S') {
            int n = strlen(&arg->s);
            val = malloc(sizeof(mapper_osc_value_t) + n + 1);

            // For unknown reasons, strcpy crashes here with -O2, so
            // we'll use memcpy instead, which does not crash.
            memcpy(&val->value.s, &arg->s, n+1);
        }
        else {
            val = malloc(sizeof(mapper_osc_value_t));
            if (arg)
                val->value = *arg;
            else
                val->value.h = 0;
        }
        val->type = type;
        table_add(t, key, val);
        table_sort(t);
        return 1;
    }
    return 0;
}

#ifdef DEBUG
void table_dump_osc_values(table t)
{
    string_table_node_t *n = t->store;
    int i;
    for (i=0; i<t->len; i++) {
        printf("%s: ", n->key);
        mapper_osc_value_t *v = n->value;
        lo_arg_pp(v->type, &v->value);
        printf("\n");
        n++;
    }
}
#endif
