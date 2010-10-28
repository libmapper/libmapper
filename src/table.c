
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *key;
    void *value;
} node_t;

typedef struct {
    node_t *store;
    int len;
    int alloced;
} table_t, *table;

int compare_node_keys(const void *l, const void *r)
{
    return strcmp(((node_t*)l)->key, ((node_t*)r)->key);
}

table table_new()
{
    table t = (table)malloc(sizeof(table_t));
    if (!t) return 0;
    t->len = 0;
    t->alloced = 1;
    t->store = (node_t*)malloc(sizeof(node_t));
    return t;
}

void table_free(table t)
{
    int i;
    for (i=0; i<t->len; i++)
        if (t->store[i].key)
            free((char*)t->store[i].key);
    free(t);
}

void table_add(table t, const char *key, void *value)
{
    t->len += 1;
    if (t->len > t->alloced) {
        while (t->len > t->alloced)
            t->alloced *= 2;
        t->store = realloc(t->store, t->alloced*sizeof(node_t));
    }
    t->store[t->len-1].key = strdup(key);
    t->store[t->len-1].value = value;
}

void table_sort(table t)
{
    qsort(t->store, t->len, sizeof(node_t), compare_node_keys);
}

int table_find(table t, const char *key, void **value)
{
    node_t tmp;
    tmp.key = key;
    node_t *n = bsearch(&tmp, t->store, t->len,
                        sizeof(node_t), compare_node_keys);
    if (n) {
        *value = n->value;
        return 1;
    }
    else
        return 0;
}

void *table_find_p(table t, const char *key)
{
    node_t tmp;
    tmp.key = key;
    node_t *n = bsearch(&tmp, t->store, t->len,
                        sizeof(node_t), compare_node_keys);
    return n ? n->value : 0;
}
