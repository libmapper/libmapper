
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <zlib.h>

#include "mapper_internal.h"

/* Some useful local list functions. */

/*
 *   Note on the trick used here: Presuming that we can have lists as
 * the result of a search query, we need to be able to return a linked
 * list composed of pointers to arbitrary database items.  However a
 * very common operation will be to walk through all the entries.  We
 * prepend a header containing a pointer to the next item and a
 * pointer to the current item, and return the address of the current
 * pointer.
 *   In the normal case, the address of the self-pointer can therefore
 * be used to locate the actual database entry, and we can walk along
 * the actual list without needing to allocate any memory for a list
 * header.  However, in the case where we want to walk along the
 * result of a query, we can allocate a dynamic set of list headers,
 * and still have 'self' point to the actual database item.
 *   Both types of queries can use the returned double-pointer as
 * context for the search as well as for returning the desired value.
 * This allows us to avoid requiring the user to manage a separate
 * iterator object.
 */

typedef enum {
    QUERY_STATIC,
    QUERY_DYNAMIC
} query_type_t;

typedef struct {
    void *next;
    void *self;
    struct _query_info *query_context;
    query_type_t query_type;
    int data[1]; // stub
}  list_header_t;

#define LIST_HEADER_SIZE (sizeof(list_header_t)-sizeof(int[1]))

/*! Reserve memory for a list item.  Reserves an extra pointer at the
 *  beginning of the structure to allow for a list pointer. */
static list_header_t* list_new_item(size_t size)
{
    list_header_t *lh=0;

    // make sure the compiler is doing what we think it's doing with
    // the size of list_header_t and location of data
    die_unless(LIST_HEADER_SIZE == sizeof(void*)*3 + sizeof(query_type_t),
               "unexpected size for list_header_t");
    die_unless(((char*)&lh->data - (char*)lh) == LIST_HEADER_SIZE,
               "unexpected offset for data in list_header_t");

    size += LIST_HEADER_SIZE;
    lh = malloc(size);
    if (!lh)
        return 0;

    memset(lh, 0, size);
    lh->self = &lh->data;
    lh->query_type = QUERY_STATIC;

    return (list_header_t*)&lh->data;
}

/*! Get the list header for memory returned from list_new_item(). */
static list_header_t* list_get_header_by_data(void *data)
{
    return (list_header_t*)(data - LIST_HEADER_SIZE);
}

/*! Get the list header for memory returned from list_new_item(). */
static list_header_t* list_get_header_by_self(void *self)
{
    list_header_t *lh=0;
    return (list_header_t*)(self - ((void*)&lh->self - (void*)lh));
}

/*! Set the next pointer in memory returned from list_new_item(). */
static void list_set_next(void *mem, void *next)
{
    list_get_header_by_data(mem)->next = next;
}

/*! Get the next pointer in memory returned from list_new_item(). */
static void* list_get_next(void *mem)
{
    return list_get_header_by_data(mem)->next;
}

/*! Prepend an item to the beginning of a list. */
static void* list_prepend_item(void *item, void **list)
{
    list_set_next(item, *list);
    *list = item;
    return item;
}

/*! Remove an item from a list and free its memory. */
void list_remove_item(void *item, void **head)
{
    void *prev_node = 0, *node = *head;
    while (node) {
        if (node == item)
            break;
        prev_node = node;
        node = list_get_next(node);
    }

    if (!node)
        return;

    if (prev_node)
        list_set_next(prev_node, list_get_next(node));
    else
        *head = list_get_next(node);

    free(list_get_header_by_data(node));
}

/** Structures and functions for performing dynamic queries **/

/* Here are some generalized routines for dealing with typical context
 * format and query continuation. Functions specific to particular
 * queries are defined further down with their compare operation. */

/*! Function for query comparison */
typedef int query_compare_func_t(void *context_data, void *item);

/*! Function for freeing query context */
typedef void query_free_func_t(list_header_t *lh);

/*! Contains some function pointers and data for handling query context. */
typedef struct _query_info {
    unsigned int size;
    query_compare_func_t *query_compare;
    query_free_func_t *query_free;
    int data[0]; // stub
} query_info_t;

static void **dynamic_query_continuation(list_header_t *lh)
{
    void *item = list_get_header_by_data(lh->self)->next;
    while (item) {
        if (lh->query_context->query_compare(&lh->query_context->data, item))
            break;
        item = list_get_next(item);
    }

    if (item) {
        lh->self = item;
        return &lh->self;
    }

    // Clean up
    if (lh->query_context->query_free)
        lh->query_context->query_free(lh);
    return 0;
}

static void free_query_single_context(list_header_t *lh)
{
    free(lh->query_context);
    free(lh);
}

static list_header_t *construct_query_context_from_strings(
    query_compare_func_t *f, ...)
{
    list_header_t *lh = (list_header_t*)malloc(LIST_HEADER_SIZE);
    lh->next = dynamic_query_continuation;
    lh->query_type = QUERY_DYNAMIC;

    va_list aq;
    va_start(aq, f);

    int size = 0;
    const char *s = va_arg(aq, const char*);
    while (s) {
        size += strlen(s) + 1;
        s = va_arg(aq, const char*);
    };
    va_end(aq);

    lh->query_context = (query_info_t*)malloc(sizeof(query_info_t)+size);

    char *t = (char*)&lh->query_context->data;
    va_start(aq, f);
    s = va_arg(aq, const char*);
    while (s) {
        while (*s) { *t++ = *s++; }
        *t++ = 0;
        s = va_arg(aq, const char*);
    };
    va_end(aq);

    lh->query_context->size = sizeof(query_info_t)+size;
    lh->query_context->query_compare = f;
    lh->query_context->query_free =
        (query_free_func_t*)free_query_single_context;
    return lh;
}

static void save_query_single_context(list_header_t *src,
                                      void *mem, unsigned int size)
{
    int needed = LIST_HEADER_SIZE;
    if (src->query_context)
        needed += src->query_context->size;
    else
        needed += sizeof(unsigned int);

    die_unless(size >= needed,
               "not enough memory provided to save query context.\n");

    memcpy(mem, src, LIST_HEADER_SIZE);
    if (src->query_context)
        memcpy(mem+LIST_HEADER_SIZE, src->query_context,
               src->query_context->size);
    else
        ((query_info_t*)(mem+LIST_HEADER_SIZE))->size = 0;
}

static void restore_query_single_context(list_header_t *dest, void *mem)
{
    query_info_t *qi = mem + LIST_HEADER_SIZE;
    if (qi->size > 0)
        die_unless(dest->query_context
                   && dest->query_context->size >= qi->size,
                   "not enough memory provided to restore query context.\n");

    void *qc = dest->query_context;
    memcpy(dest, mem, LIST_HEADER_SIZE);
    if (qi->size == 0)
        dest->query_context = 0;
    else {
        memcpy(qc, qi, qi->size);
        dest->query_context = qc;
    }
}

// May be useful in the future.
#if 0
static list_header_t *dup_query_single_context(list_header_t *lh)
{
    list_header_t *result = (list_header_t*) malloc(sizeof(list_header_t*));
    memcpy(result, lh, LIST_HEADER_SIZE);

    if (lh->query_context) {
        result->query_context = malloc(lh->query_context->size);
        memcpy(result->query_context, lh->query_context,
               lh->query_context->size);
    }

    return result;
}

static void copy_query_single_context(list_header_t *src,
                                      list_header_t *dest)
{
    memcpy(src, dest, LIST_HEADER_SIZE);
    if (src->query_context) {
        die_unless(dest->query_context!=0,
                   "error, no destination query context to copy to.");
        die_unless(dest->query_context->size == src->query_context->size,
                   "error, query context sizes don't match on copy.");
        memcpy(dest->query_context, src->query_context,
               src->query_context->size);
    }
}
#endif

static void **iterator_next(void** p)
{
    if (!p) {
        trace("bad pointer in iterator_next()\n");
        return 0;
    }

    if (!*p) {
        trace("pointer in iterator_next() points nowhere\n");
        return 0;
    }

    list_header_t *lh1 = list_get_header_by_self(p);
    if (!lh1->next)
        return 0;

    if (lh1->query_type == QUERY_STATIC)
    {
        list_header_t *lh2 = list_get_header_by_data(lh1->next);

        die_unless(lh2->self == &lh2->data,
                   "bad self pointer in list structure");

        return (void**)&lh2->self;
    }
    else if (lh1->query_type == QUERY_DYNAMIC)
    {
        /* Here we treat next as a pointer to a continuation function,
         * so we can return items from the database computed lazily.
         * The context is simply the string(s) to match.  In the
         * future, it might point to the results of a SQL query for
         * example. */
        void **(*f) (list_header_t*) = lh1->next;
        return f(lh1);
    }
    return 0;
}

void add_callback(fptr_list *head, void *f, void *user)
{
    fptr_list cb = (fptr_list)malloc(sizeof(struct _fptr_list));
    cb->f = f;
    cb->context = user;
    cb->next = *head;
    *head = cb;
}

void remove_callback(fptr_list *head, void *f, void *user)
{
    fptr_list cb = *head;
    fptr_list prevcb = 0;
    while (cb) {
        if (cb->f == f && cb->context == user)
            break;
        prevcb = cb;
        cb = cb->next;
    }
    if (!cb)
        return;

    if (prevcb)
        prevcb->next = cb->next;
    else
        *head = cb->next;

    free(cb);
}

/* Helper functions for queries that take two queries as parameters. */

typedef struct {
    list_header_t *lh_src_head;
    list_header_t *lh_dest_head;
} src_dest_queries_t;

static void free_query_src_dest_queries(list_header_t *lh)
{
    query_info_t *qi = lh->query_context;
    src_dest_queries_t *d = (src_dest_queries_t*)&qi->data;

    qi = d->lh_src_head->query_context;
    if (d->lh_src_head->query_type == QUERY_DYNAMIC
        && qi->query_free)
        qi->query_free(d->lh_src_head);

    qi = d->lh_dest_head->query_context;
    if (d->lh_dest_head->query_type == QUERY_DYNAMIC
        && qi->query_free)
        qi->query_free(d->lh_dest_head);

    free_query_single_context(lh);
}

/* Helper functions for updating struct fields based on message
 * parameters. */

static int update_signal_value_if_arg(mapper_message_t *params,
                                      mapper_msg_param_t field,
                                      char sigtype,
                                      mapper_signal_value_t **pv)
{
    lo_arg **a = mapper_msg_get_param(params, field);
    const char *type = mapper_msg_get_type(params, field);

    if (!a || !(*a))
        return 0;

    mapper_signal_value_t v;
    int update = 0;
    if (sigtype == 'f') {
        if (type[0] == 'f') {
            v.f = (*a)->f;
            update = 1;
        }
        else if (type[0] == 'i') {
            v.f = (*a)->i;
            update = 1;
        }
    }
    else if (sigtype == 'i') {
        if (type[0] == 'f') {
            v.i32 = (int)(*a)->f;
            update = 1;
        }
        else if (type[0] == 'i') {
            v.i32 = (*a)->i;
            update = 1;
        }
    }
    if (update) {
        if (!*pv)
            *pv = realloc(*pv, sizeof(mapper_signal_value_t));
        if (memcmp(&v, pv, sizeof(mapper_signal_value_t))!=0) {
            **pv = v;
            return 1;
        }
    }
    return 0;
}

static int update_string_if_different(char **pdest_str,
                                      const char *src_str)
{
    if (!(*pdest_str) || strcmp((*pdest_str), src_str)) {
        char *str = (char*) realloc((void*)(*pdest_str),
                                    strlen(src_str)+1);
        strcpy(str, src_str);
        (*pdest_str) = str;
        return 1;
    }
    return 0;
}

static int update_string_if_arg(char **pdest_str,
                                mapper_message_t *params,
                                mapper_msg_param_t field)
{
    lo_arg **a = mapper_msg_get_param(params, field);
    const char *type = mapper_msg_get_type(params, field);

    if (a && (*a) && (type[0]=='s' || type[0]=='S')
        && (!(*pdest_str) || strcmp((*pdest_str), &(*a)->s)))
    {
        char *str = (char*) realloc((void*)(*pdest_str),
                                    strlen(&(*a)->s)+1);
        strcpy(str, &(*a)->s);
        (*pdest_str) = str;
        return 1;
    }
    return 0;
}

static int update_char_if_arg(char *pdest_char,
                              mapper_message_t *params,
                              mapper_msg_param_t field)
{
    lo_arg **a = mapper_msg_get_param(params, field);
    const char *type = mapper_msg_get_type(params, field);

    if (a && (*a) && (type[0]=='s' || type[0]=='S')) {
        if (*pdest_char != (&(*a)->s)[0]) {
            (*pdest_char) = (&(*a)->s)[0];
            return 1;
        }
    }
    else if (a && (*a) && type[0]=='c') {
        if (*pdest_char != (*a)->c) {
            (*pdest_char) = (*a)->c;
            return 1;
        }
    }
    return 0;
}

static int update_int_if_arg(int *pdest_int,
                             mapper_message_t *params,
                             mapper_msg_param_t field)
{
    lo_arg **a = mapper_msg_get_param(params, field);
    const char *type = mapper_msg_get_type(params, field);

    if (a && (*a) && (type[0]=='i') && (*pdest_int != (&(*a)->i)[0])) {
        (*pdest_int) = (&(*a)->i)[0];
        return 1;
    }
    return 0;
}

/* Static data for property tables embedded in the db data
 * structures.
 *
 * Signals and devices have properties that can be indexed by name,
 * and can have extra properties attached to them if these appear in
 * the OSC message describing the resource.  The utility of this is to
 * allow for attaching of arbitrary metadata to objects on the
 * network.  For example, signals can have 'x' and 'y' values to
 * indicate their physical position in the room.
 *
 * It is also useful to be able to look up standard properties like
 * vector length, name, or unit by specifying these as a string.
 *
 * The following data provides a string table (as usable by the
 * implementation in table.c) for indexing the static data existing in
 * the mapper_db_* data structures.  Some of these static properties
 * may not actually exist, such as 'minimum' and 'maximum' which are
 * optional properties of signals.  Therefore an 'indirect' form is
 * available where the table points to a pointer to the value, which
 * may be null.
 *
 * A property lookup consists of looking through the 'extra'
 * properties of a structure.  If the requested property is not found,
 * then the 'static' properties are searched---in the worst case, an
 * unsuccessful lookup may therefore take twice as long.
 *
 * To iterate through all available properties, the caller must
 * request by index, starting at 0, and incrementing until failure.
 * They are not guaranteed to be in a particular order.
 */

typedef struct {
    char type;
    char indirect;
    int  offset;
} property_table_value_t;

#define SIGDB_OFFSET(x) offsetof(mapper_db_signal_t, x)
#define DEVDB_OFFSET(x) offsetof(mapper_db_device_t, x)
#define LINKDB_OFFSET(x) offsetof(mapper_db_link_t, x)
#define CONDB_OFFSET(x) offsetof(mapper_db_connection_t, x)

/* Here type 'o', which is not an OSC type, was reserved to mean "same
 * type as the signal's type".  The lookup and index functions will
 * return the sig->type instead of the value's type. */
static property_table_value_t sigdb_values[] = {
    { 's', 1, SIGDB_OFFSET(device_name) },
    { 'i', 0, SIGDB_OFFSET(is_output) },
    { 'i', 0, SIGDB_OFFSET(length) },
    { 'o', 1, SIGDB_OFFSET(maximum) },
    { 'o', 1, SIGDB_OFFSET(minimum) },
    { 's', 1, SIGDB_OFFSET(name) },
    { 'f', 0, SIGDB_OFFSET(rate) },
    { 'c', 0, SIGDB_OFFSET(type) },
    { 's', 1, SIGDB_OFFSET(unit) },
    { 'i', 0, SIGDB_OFFSET(user_data) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t sigdb_nodes[] = {
    { "device_name", &sigdb_values[0] },
    { "direction",   &sigdb_values[1] },
    { "length",      &sigdb_values[2] },
    { "max",         &sigdb_values[3] },
    { "min",         &sigdb_values[4] },
    { "name",        &sigdb_values[5] },
    { "rate",        &sigdb_values[6] },
    { "type",        &sigdb_values[7] },
    { "unit",        &sigdb_values[8] },
    { "user_data",   &sigdb_values[9] },
};

static mapper_string_table_t sigdb_table =
  { sigdb_nodes, 10, 10 };

static property_table_value_t devdb_values[] = {
    { 's', 1, DEVDB_OFFSET(host) },
    { 'i', 0, DEVDB_OFFSET(n_connections_in) },
    { 'i', 0, DEVDB_OFFSET(n_connections_out) },
    { 'i', 0, DEVDB_OFFSET(n_inputs) },
    { 'i', 0, DEVDB_OFFSET(n_links_in) },
    { 'i', 0, DEVDB_OFFSET(n_links_out) },
    { 'i', 0, DEVDB_OFFSET(n_outputs) },
    { 's', 1, DEVDB_OFFSET(name) },
    { 'i', 0, DEVDB_OFFSET(port) },
    { 't', 0, DEVDB_OFFSET(synced) },
    { 'i', 0, DEVDB_OFFSET(user_data) },
    { 'i', 0, DEVDB_OFFSET(version) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t devdb_nodes[] = {
    { "host",               &devdb_values[0] },
    { "n_connections_in",   &devdb_values[1] },
    { "n_connections_out",  &devdb_values[2] },
    { "n_inputs",           &devdb_values[3] },
    { "n_links_in",         &devdb_values[4] },
    { "n_links_out",        &devdb_values[5] },
    { "n_outputs",          &devdb_values[6] },
    { "name",               &devdb_values[7] },
    { "port",               &devdb_values[8] },
    { "synced",             &devdb_values[9] },
    { "user_data",          &devdb_values[10] },
    { "version",            &devdb_values[11] },
};

static mapper_string_table_t devdb_table =
  { devdb_nodes, 12, 12 };

// scope names and hashes are handled separately
static property_table_value_t linkdb_values[] = {
    { 's', 1, LINKDB_OFFSET(dest_host) },
    { 's', 1, LINKDB_OFFSET(dest_name) },
    { 'i', 0, LINKDB_OFFSET(dest_port) },
    { 'i', 0, LINKDB_OFFSET(num_scopes) },
    { 's', 1, LINKDB_OFFSET(src_host) },
    { 's', 1, LINKDB_OFFSET(src_name) },
    { 'i', 0, LINKDB_OFFSET(src_port) },
};

/* This table must remain in alphabetical order. */
// scope names and hashes are handled separately
static string_table_node_t linkdb_nodes[] = {
    { "dest_host",    &linkdb_values[0] },
    { "dest_name",    &linkdb_values[1] },
    { "dest_port",    &linkdb_values[2] },
    { "num_scopes",   &linkdb_values[3] },
    { "src_host",     &linkdb_values[4] },
    { "src_name",     &linkdb_values[5] },
    { "src_port",     &linkdb_values[6] },
};

static mapper_string_table_t linkdb_table =
{ linkdb_nodes, 7, 7 };

// ranges are handled separately
static property_table_value_t condb_values[] = {
    { 'i', 0, CONDB_OFFSET(bound_min) },
    { 'i', 0, CONDB_OFFSET(bound_max) },
    { 'i', 0, CONDB_OFFSET(dest_length) },
    { 's', 1, CONDB_OFFSET(dest_name) },
    { 'c', 0, CONDB_OFFSET(dest_type) },
    { 's', 1, CONDB_OFFSET(expression) },
    { 'i', 0, CONDB_OFFSET(mode) },
    { 'i', 0, CONDB_OFFSET(muted) },
    { 'i', 0, CONDB_OFFSET(src_length) },
    { 's', 1, CONDB_OFFSET(src_name) },
    { 'c', 0, CONDB_OFFSET(src_type) },
};

/* This table must remain in alphabetical order. */
// ranges are handled separately
static string_table_node_t condb_nodes[] = {
    { "bound_min",   &condb_values[0] },
    { "bound_max",   &condb_values[1] },
    { "dest_length", &condb_values[2] },
    { "dest_name",   &condb_values[3] },
    { "dest_type",   &condb_values[4] },
    { "expression",  &condb_values[5] },
    { "mode",        &condb_values[6] },
    { "muted",       &condb_values[7] },
    { "src_length",  &condb_values[8] },
    { "src_name",    &condb_values[9] },
    { "src_type",    &condb_values[10] },
};

static mapper_string_table_t condb_table =
{ condb_nodes, 11, 11 };

/* Generic index and lookup functions to which the above tables would
 * be passed. These are called for specific types below. */

static
int mapper_db_property_index(void *thestruct, char o_type,
                             table extra, unsigned int index,
                             const char **property, lo_type *type,
                             const lo_arg **value, table proptable)
{
    die_unless(type!=0, "type parameter cannot be null.\n");
    die_unless(value!=0, "value parameter cannot be null.\n");

    int i=0, j=0;

    /* Unfortunately due to "optional" properties like
     * minimum/maximum, unit, etc, we cannot use an O(1) lookup here--
     * the index changes according to availability of properties.
     * Thus, we have to search through properties linearly,
     * incrementing a counter along the way, so indexed lookup is
     * O(N).  Meaning iterating through all indexes is O(N^2).  A
     * better way would be to use an iterator-style interface if
     * efficiency was important for iteration. */

    /* First search static properties */
    property_table_value_t *prop;
    for (i=0; i < proptable->len; i++)
    {
        prop = table_value_at_index_p(proptable, i);
        if (prop->indirect) {
            lo_arg **pp = (lo_arg**)((char*)thestruct + prop->offset);
            if (*pp) {
                if (j==index) {
                    if (property)
                        *property = table_key_at_index(proptable, i);
                    *type = prop->type == 'o' ? o_type : prop->type;
                    *value = *pp;
                    return 0;
                }
                j++;
            }
        }
        else {
            if (j==index) {
                if (property)
                    *property = table_key_at_index(proptable, i);
                *type = prop->type == 'o' ? o_type : prop->type;
                *value = (lo_arg*)((char*)thestruct + prop->offset);
                return 0;
            }
            j++;
        }
    }

    index -= j;
    mapper_osc_value_t *val;
    val = table_value_at_index_p(extra, index);
    if (val) {
        if (property)
            *property = table_key_at_index(extra, index);
        *type = val->type == 'o' ? o_type : val->type;
        *value = &val->value;
        return 0;
    }

    return 1;
}

static
int mapper_db_property_lookup(void *thestruct, char o_type,
                              table extra, const char *property,
                              lo_type *type, const lo_arg **value,
                              table proptable)
{
    die_unless(type!=0, "type parameter cannot be null.\n");
    die_unless(value!=0, "value parameter cannot be null.\n");

    const mapper_osc_value_t *val;
    val = table_find_p(extra, property);
    if (val) {
        *type = val->type == 'o' ? o_type : val->type;
        *value = &val->value;
        return 0;
    }

    property_table_value_t *prop;
    prop = table_find_p(proptable, property);
    if (prop) {
        *type = prop->type == 'o' ? o_type : prop->type;
        if (prop->indirect) {
            lo_arg **pp = (lo_arg**)((char*)thestruct + prop->offset);
            if (*pp)
                *value = *pp;
            else {
                return 1;
            }
        }
        else
            *value = (lo_arg*)((char*)thestruct + prop->offset);
        return 0;
    }
    return 1;
}

/**** Device records ****/

/*! Update information about a given device record based on message
 *  parameters. */
static int update_device_record_params(mapper_db_device reg,
                                       const char *name,
                                       mapper_message_t *params)
{
    int updated = 0;

    updated += update_string_if_different(&reg->name, name);
    if (updated)
        reg->name_hash = crc32(0L, (const Bytef *)name, strlen(name));

    updated += update_string_if_arg(&reg->host, params, AT_IP);

    updated += update_int_if_arg(&reg->port, params, AT_PORT);

    updated += update_int_if_arg(&reg->n_inputs, params, AT_NUM_INPUTS);

    updated += update_int_if_arg(&reg->n_outputs, params, AT_NUM_OUTPUTS);

    updated += update_int_if_arg(&reg->n_links_in, params, AT_NUM_LINKS_IN);

    updated += update_int_if_arg(&reg->n_links_out, params, AT_NUM_LINKS_OUT);

    updated += update_int_if_arg(&reg->n_connections_in, params, AT_NUM_CONNECTIONS_IN);

    updated += update_int_if_arg(&reg->n_connections_out, params, AT_NUM_CONNECTIONS_OUT);

    updated += update_int_if_arg(&reg->version, params, AT_REV);

    updated += mapper_msg_add_or_update_extra_params(reg->extra, params);

    return updated;
}

int mapper_db_add_or_update_device_params(mapper_db db,
                                          const char *name,
                                          mapper_message_t *params)
{
    mapper_db_device reg = mapper_db_get_device_by_name(db, name);
    int rc = 0, updated = 0;

    if (!reg) {
        reg = (mapper_db_device) list_new_item(sizeof(*reg));
        reg->extra = table_new();
        rc = 1;

        list_prepend_item(reg, (void**)&db->registered_devices);
    }

    if (reg) {
        updated = update_device_record_params(reg, name, params);

        if (rc || updated) {
            fptr_list cb = db->device_callbacks;
            while (cb) {
                mapper_db_device_handler *h = cb->f;
                h(reg, rc ? MDB_NEW : MDB_MODIFY, cb->context);
                cb = cb->next;
            }
        }
    }

    return rc;
}

int mapper_db_device_property_index(mapper_db_device dev, unsigned int index,
                                    const char **property, lo_type *type,
                                    const lo_arg **value)
{
    return mapper_db_property_index(dev, 0, dev->extra,
                                    index, property, type,
                                    value, &devdb_table);
}

int mapper_db_device_property_lookup(mapper_db_device dev,
                                     const char *property,
                                     lo_type *type,
                                     const lo_arg **value)
{
    return mapper_db_property_lookup(dev, 0, dev->extra,
                                     property, type, value,
                                     &devdb_table);
}

void mapper_db_remove_device_by_name(mapper_db db, const char *name)
{
    mapper_db_device dev = mapper_db_get_device_by_name(db, name);
    if (!dev)
        return;

    mapper_db_remove_connections_by_query(db,
        mapper_db_get_connections_by_device_name(db, name));

    mapper_db_remove_links_by_query(db,
        mapper_db_get_links_by_device_name(db, name));

    mapper_db_remove_inputs_by_query(db,
        mapper_db_get_inputs_by_device_name(db, name));

    mapper_db_remove_outputs_by_query(db,
        mapper_db_get_outputs_by_device_name(db, name));

    fptr_list cb = db->device_callbacks;
    while (cb) {
        mapper_db_device_handler *h = cb->f;
        h(dev, MDB_REMOVE, cb->context);
        cb = cb->next;
    }

    if (dev->name)
        free(dev->name);
    if (dev->host)
        free(dev->host);
    if (dev->extra)
        table_free(dev->extra, 1);
    list_remove_item(dev, (void**)&db->registered_devices);
}

mapper_db_device mapper_db_get_device_by_name(mapper_db db,
                                              const char *name)
{
    mapper_db_device reg = db->registered_devices;
    while (reg) {
        if (strcmp(reg->name, name)==0)
            return reg;
        reg = list_get_next(reg);
    }
    return 0;
}

mapper_db_device mapper_db_get_device_by_name_hash(mapper_db db,
                                                   uint32_t name_hash)
{
    mapper_db_device reg = db->registered_devices;
    while (reg) {
        if (name_hash == reg->name_hash)
            return reg;
        reg = list_get_next(reg);
    }
    return 0;
}

mapper_db_device *mapper_db_get_all_devices(mapper_db db)
{
    if (!db->registered_devices)
        return 0;

    list_header_t *lh = list_get_header_by_data(db->registered_devices);

    die_unless(lh->self == &lh->data,
               "bad self pointer in list structure");

    return (mapper_db_device*)&lh->self;
}

static int cmp_query_match_devices_by_name(void *context_data,
                                           mapper_db_device dev)
{
    const char *device_pattern = (const char*)context_data;
    return strstr(dev->name, device_pattern)!=0;
}

mapper_db_device *mapper_db_match_devices_by_name(mapper_db db,
                                                  const char *str)
{
    mapper_db_device dev = db->registered_devices;
    if (!dev)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_match_devices_by_name, str, 0);

    lh->self = dev;

    if (cmp_query_match_devices_by_name((void*)str, dev))
        return (mapper_db_device*)&lh->self;

    return (mapper_db_device*)dynamic_query_continuation(lh);
}

mapper_db_device *mapper_db_device_next(mapper_db_device* p)
{
    return (mapper_db_device*) iterator_next((void**)p);
}

void mapper_db_device_done(mapper_db_device_t **d)
{
    if (!d) return;
    list_header_t *lh = list_get_header_by_data(*d);
    if (lh->query_type == QUERY_DYNAMIC
        && lh->query_context->query_free)
        lh->query_context->query_free(lh);
}

void mapper_db_dump(mapper_db db)
{
    mapper_db_device reg = db->registered_devices;
    trace("Registered devices:\n");
    while (reg) {
        trace("  name=%s, host=%s, port=%d\n",
              reg->name, reg->host, reg->port);
        reg = list_get_next(reg);
    }

    mapper_db_signal sig = db->registered_inputs;
    trace("Registered inputs:\n");
    while (sig) {
        trace("  name=%s%s\n",
              sig->device_name, sig->name);
        sig = list_get_next(sig);
    }

    sig = db->registered_outputs;
    trace("Registered outputs:\n");
    while (sig) {
        trace("  name=%s%s\n",
              sig->device_name, sig->name);
        sig = list_get_next(sig);
    }

    mapper_db_connection con = db->registered_connections;
    trace("Registered connections:\n");
    while (con) {
        char r[1024] = "(";
        if (con->range.known & CONNECTION_RANGE_SRC_MIN)
            sprintf(r+strlen(r), "%f, ", con->range.src_min);
        else
            strcat(r, "-, ");
        if (con->range.known & CONNECTION_RANGE_SRC_MAX)
            sprintf(r+strlen(r), "%f, ", con->range.src_max);
        else
            strcat(r, "-, ");
        if (con->range.known & CONNECTION_RANGE_DEST_MIN)
            sprintf(r+strlen(r), "%f, ", con->range.dest_min);
        else
            strcat(r, "-, ");
        if (con->range.known & CONNECTION_RANGE_DEST_MAX)
            sprintf(r+strlen(r), "%f", con->range.dest_max);
        else
            strcat(r, "-");
        strcat(r, ")");
        trace("  src_name=%s, dest_name=%s,\n"
              "      src_type=%d, dest_type=%d,\n"
              "      bound_max=%s, bound_min=%s,\n"
              "      range=%s,\n"
              "      expression=%s, mode=%s, muted=%d\n",
              con->src_name, con->dest_name, con->src_type,
              con->dest_type,
              mapper_get_boundary_action_string(con->bound_max),
              mapper_get_boundary_action_string(con->bound_min),
              r, con->expression,
              mapper_get_mode_type_string(con->mode),
              con->muted);
        con = list_get_next(con);
    }

    mapper_db_link link = db->registered_links;
    trace("Registered links:\n");
    while (link) {
        trace("  source=%s, dest=%s\n",
              link->src_name, link->dest_name);
        link = list_get_next(link);
    }
}

void mapper_db_add_device_callback(mapper_db db,
                                   mapper_db_device_handler *h, void *user)
{
    add_callback(&db->device_callbacks, h, user);
}

void mapper_db_remove_device_callback(mapper_db db,
                                      mapper_db_device_handler *h, void *user)
{
    remove_callback(&db->device_callbacks, h, user);
}

/**** Signals ****/

/*! Update information about a given signal record based on message
 *  parameters. */
static int update_signal_record_params(mapper_db_signal sig,
                                       const char *name,
                                       const char *device_name,
                                       mapper_message_t *params)
{
    int updated = 0;
    updated += update_string_if_different((char**)&sig->name, name);
    updated += update_string_if_different((char**)&sig->device_name, device_name);

    updated += update_int_if_arg(&sig->id, params, AT_ID);

    updated += update_char_if_arg(&sig->type, params, AT_TYPE);

    updated += update_int_if_arg(&sig->length, params, AT_LENGTH);

    updated += update_string_if_arg((char**)&sig->unit, params, AT_UNITS);

    updated += update_signal_value_if_arg(params, AT_MAX,
                                          sig->type, &sig->maximum);

    updated += update_signal_value_if_arg(params, AT_MIN,
                                          sig->type, &sig->minimum);

    int is_output = mapper_msg_get_direction(params);
    if (is_output != -1 && is_output != sig->is_output) {
        sig->is_output = is_output;
        updated++;
    }

    updated += update_int_if_arg(&sig->num_instances, params, AT_INSTANCES);

    updated += mapper_msg_add_or_update_extra_params(sig->extra, params);

    return updated;
}

int mapper_db_add_or_update_signal_params(mapper_db db,
                                          const char *name,
                                          const char *device_name,
                                          mapper_message_t *params)
{
    mapper_db_signal sig;
    int rc = 0, updated = 0;

    //need to find out if signal is output from params
    int is_output = mapper_msg_get_direction(params);
    if (is_output == 1)
        sig = mapper_db_get_output_by_device_and_signal_names(db, device_name, name);
    else if (is_output == 0)
        sig = mapper_db_get_input_by_device_and_signal_names(db, device_name, name);
    else
        return 0;

    if (!sig) {
        sig = (mapper_db_signal) list_new_item(sizeof(mapper_db_signal_t));

        // Defaults (int, length=1)
        mapper_db_signal_init(sig, is_output, 'i', 1, 0, 0);
        rc = 1;
    }

    if (sig) {
        update_signal_record_params(sig, name, device_name, params);

        if (rc)
            list_prepend_item(sig, (void**)(is_output
                                            ? &db->registered_outputs
                                            : &db->registered_inputs));

        if (rc || updated) {
            // TODO: Should we really allow callbacks to free themselves?
            fptr_list cb = db->signal_callbacks, temp;
            while (cb) {
                temp = cb->next;
                mapper_db_signal_handler *h = cb->f;
                h(sig, rc ? MDB_NEW : MDB_MODIFY, cb->context);
                cb = temp;
            }
        }
    }

    return rc;
}

void mapper_db_signal_init(mapper_db_signal sig, int is_output,
                           char type, int length,
                           const char *name, const char *unit)
{
    sig->is_output = is_output;
    sig->type = type;
    sig->length = length;
    sig->unit = unit ? strdup(unit) : 0;
    sig->extra = table_new();

    if (!name)
        return;

    if (name[0]=='/')
        sig->name = strdup(name);
    else {
        char *str = malloc(strlen(name)+2);
        if (str) {
            str[0] = '/';
            str[1] = 0;
            strcat(str, name);
            sig->name = str;
        }
    }
}

int mapper_db_signal_property_index(mapper_db_signal sig, unsigned int index,
                                    const char **property, lo_type *type,
                                    const lo_arg **value)
{
    return mapper_db_property_index(sig, sig->type, sig->extra,
                                    index, property, type,
                                    value, &sigdb_table);
}

int mapper_db_signal_property_lookup(mapper_db_signal sig,
                                     const char *property,
                                     lo_type *type,
                                     const lo_arg **value)
{
    return mapper_db_property_lookup(sig, sig->type, sig->extra,
                                     property, type, value,
                                     &sigdb_table);
}

void mapper_db_add_signal_callback(mapper_db db,
                                   mapper_db_signal_handler *h, void *user)
{
    add_callback(&db->signal_callbacks, h, user);
}

void mapper_db_remove_signal_callback(mapper_db db,
                                      mapper_db_signal_handler *h, void *user)
{
    remove_callback(&db->signal_callbacks, h, user);
}

mapper_db_signal_t **mapper_db_get_all_inputs(mapper_db db)
{
    if (!db->registered_inputs)
        return 0;

    list_header_t *lh = list_get_header_by_data(db->registered_inputs);

    die_unless(lh->self == &lh->data,
               "bad self pointer in list structure");

    return (mapper_db_signal*)&lh->self;
}

mapper_db_signal_t **mapper_db_get_all_outputs(mapper_db db)
{
    if (!db->registered_outputs)
        return 0;

    list_header_t *lh = list_get_header_by_data(db->registered_outputs);

    die_unless(lh->self == &lh->data,
               "bad self pointer in list structure");

    return (mapper_db_signal*)&lh->self;
}

static int cmp_query_signal_exact_device_name(void *context_data,
                                              mapper_db_signal sig)
{
    const char *device_name = (const char*)context_data;
    return strcmp(device_name, sig->device_name)==0;
}

mapper_db_signal_t **mapper_db_get_inputs_by_device_name(
    mapper_db db, const char *device_name)
{
    mapper_db_signal sig = db->registered_inputs;
    if (!sig)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_signal_exact_device_name,
        device_name, 0);

    lh->self = sig;

    if (cmp_query_signal_exact_device_name((void*)device_name, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal_t **mapper_db_get_outputs_by_device_name(
    mapper_db db, const char *device_name)
{
    mapper_db_signal sig = db->registered_outputs;
    if (!sig)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_signal_exact_device_name,
        device_name, 0);

    lh->self = sig;

    if (cmp_query_signal_exact_device_name((void*)device_name, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal mapper_db_get_input_by_device_and_signal_names(
    mapper_db db, const char *device_name, const char *signal_name)
{
    mapper_db_signal sig = db->registered_inputs;
    if (!sig)
        return 0;

    while (sig) {
        if (strcmp(sig->device_name, device_name)==0
            && strcmp(sig->name, signal_name)==0)
            return sig;
        sig = list_get_next(sig);
    }
    return 0;
}

mapper_db_signal mapper_db_get_output_by_device_and_signal_names(
    mapper_db db, const char *device_name, const char *signal_name)
{
    mapper_db_signal sig = db->registered_outputs;
    if (!sig)
        return 0;

    while (sig) {
        if (strcmp(sig->device_name, device_name)==0
            && strcmp(sig->name, signal_name)==0)
            return sig;
        sig = list_get_next(sig);
    }
    return 0;
}

static int cmp_match_signal_device_name(void *context_data,
                                        mapper_db_signal sig)
{
    const char *device_name = (const char*)context_data;
    const char *signal_pattern = ((const char*)context_data
                                  + strlen(device_name) + 1);
    return strcmp(device_name, sig->device_name)==0
        && strstr(sig->name, signal_pattern);
}

mapper_db_signal_t **mapper_db_match_inputs_by_device_name(
    mapper_db db, const char *device_name, const char *input_pattern)
{
    mapper_db_signal sig = db->registered_inputs;
    if (!sig)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_match_signal_device_name,
        device_name, input_pattern, 0);

    lh->self = sig;

    if (cmp_match_signal_device_name(&lh->query_context->data, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal_t **mapper_db_match_outputs_by_device_name(
    mapper_db db, const char *device_name, char const *output_pattern)
{
    mapper_db_signal sig = db->registered_outputs;
    if (!sig)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_match_signal_device_name,
        device_name, output_pattern, 0);

    lh->self = sig;

    if (cmp_match_signal_device_name(&lh->query_context->data, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal_t **mapper_db_signal_next(mapper_db_signal_t** p)
{
    return (mapper_db_signal*) iterator_next((void**)p);
}

void mapper_db_signal_done(mapper_db_signal_t **s)
{
    if (!s) return;
    list_header_t *lh = list_get_header_by_data(*s);
    if (lh->query_type == QUERY_DYNAMIC
        && lh->query_context->query_free)
        lh->query_context->query_free(lh);
}

void mapper_db_remove_inputs_by_query(mapper_db db,
                                      mapper_db_signal_t **s)
{
    while (s) {
        mapper_db_signal sig = *s;
        s = mapper_db_signal_next(s);

        fptr_list cb = db->signal_callbacks;
        while (cb) {
            mapper_db_signal_handler *h = cb->f;
            h(sig, MDB_REMOVE, cb->context);
            cb = cb->next;
        }

        if (sig->name)
            free(sig->name);
        if (sig->device_name)
            free(sig->device_name);
        if (sig->unit)
            free(sig->unit);
        if (sig->minimum)
            free(sig->minimum);
        if (sig->maximum)
            free(sig->maximum);
        if (sig->extra)
            table_free(sig->extra, 1);
        list_remove_item(sig, (void**)&db->registered_inputs);
    }
}

void mapper_db_remove_outputs_by_query(mapper_db db,
                                       mapper_db_signal_t **s)
{
    while (s) {
        mapper_db_signal sig = *s;
        s = mapper_db_signal_next(s);

        fptr_list cb = db->signal_callbacks;
        while (cb) {
            mapper_db_signal_handler *h = cb->f;
            h(sig, MDB_REMOVE, cb->context);
            cb = cb->next;
        }

        if (sig->name)
            free(sig->name);
        if (sig->device_name)
            free(sig->device_name);
        if (sig->unit)
            free(sig->unit);
        if (sig->minimum)
            free(sig->minimum);
        if (sig->maximum)
            free(sig->maximum);
        if (sig->extra)
            table_free(sig->extra, 1);
        list_remove_item(sig, (void**)&db->registered_outputs);
    }
}

/**** Connection records ****/

/*! Update information about a given connection record based on
 *  message parameters. */
static int update_connection_record_params(mapper_db_connection con,
                                           const char *src_name,
                                           const char *dest_name,
                                           mapper_message_t *params)
{
    int updated = 0;
    updated += update_string_if_different(&con->src_name, src_name);
    updated += update_string_if_different(&con->dest_name, dest_name);
    updated += update_char_if_arg(&con->src_type, params, AT_SRC_TYPE);
    updated += update_char_if_arg(&con->dest_type, params, AT_DEST_TYPE);

    mapper_boundary_action bound;
    bound = mapper_msg_get_boundary_action(params, AT_BOUND_MAX);
    if (bound != -1 && bound != con->bound_max) {
        con->bound_max = bound;
        updated++;
    }

    bound = mapper_msg_get_boundary_action(params, AT_BOUND_MIN);
    if (bound != -1 && bound != con->bound_min) {
        con->bound_min = bound;
        updated++;
    }

    lo_arg **a_range = mapper_msg_get_param(params, AT_RANGE);
    const char *t_range = mapper_msg_get_type(params, AT_RANGE);

    if (a_range && (*a_range)) {
        if (t_range[0] == 'f') {
            if (!(con->range.known & CONNECTION_RANGE_SRC_MIN)
                || con->range.src_min != a_range[0]->f)
                updated++;
            con->range.src_min = a_range[0]->f;
            con->range.known |= CONNECTION_RANGE_SRC_MIN;
        } else if (t_range[0] == 'i') {
            if (!(con->range.known & CONNECTION_RANGE_SRC_MIN)
                || con->range.src_min != (float)a_range[0]->i)
                updated++;
            con->range.src_min = (float)a_range[0]->i;
            con->range.known |= CONNECTION_RANGE_SRC_MIN;
        }
        if (t_range[1] == 'f') {
            if (!(con->range.known & CONNECTION_RANGE_SRC_MAX)
                || con->range.src_max != a_range[1]->f)
                updated++;
            con->range.src_max = a_range[1]->f;
            con->range.known |= CONNECTION_RANGE_SRC_MAX;
        } else if (t_range[1] == 'i') {
            if (!(con->range.known & CONNECTION_RANGE_SRC_MAX)
                || con->range.src_max != a_range[1]->i)
                updated++;
            con->range.src_max = (float)a_range[1]->i;
            con->range.known |= CONNECTION_RANGE_SRC_MAX;
        }
        if (t_range[2] == 'f') {
            if (!(con->range.known & CONNECTION_RANGE_DEST_MIN)
                || con->range.dest_min != a_range[2]->f)
                updated++;
            con->range.dest_min = a_range[2]->f;
            con->range.known |= CONNECTION_RANGE_DEST_MIN;
        } else if (t_range[2] == 'i') {
            if (!(con->range.known & CONNECTION_RANGE_DEST_MIN)
                || con->range.dest_min != a_range[2]->i)
                updated++;
            con->range.dest_min = (float)a_range[2]->i;
            con->range.known |= CONNECTION_RANGE_DEST_MIN;
        }
        if (t_range[3] == 'f') {
            if (!(con->range.known & CONNECTION_RANGE_DEST_MAX)
                || con->range.dest_max != a_range[3]->f)
                updated++;
            con->range.dest_max = a_range[3]->f;
            con->range.known |= CONNECTION_RANGE_DEST_MAX;
        } else if (t_range[3] == 'i') {
            if (!(con->range.known & CONNECTION_RANGE_DEST_MAX)
                || con->range.dest_max != a_range[3]->i)
                updated++;
            con->range.dest_max = (float)a_range[3]->i;
            con->range.known |= CONNECTION_RANGE_DEST_MAX;
        }
    }

    updated += update_int_if_arg(&con->id, params, AT_ID);
    updated += update_string_if_arg(&con->expression, params, AT_EXPRESSION);
    updated += update_char_if_arg(&con->src_type, params, AT_SRC_TYPE);
    updated += update_char_if_arg(&con->dest_type, params, AT_DEST_TYPE);
    updated += update_int_if_arg(&con->src_length, params, AT_SRC_LENGTH);
    updated += update_int_if_arg(&con->dest_length, params, AT_DEST_LENGTH);

    mapper_mode_type mode = mapper_msg_get_mode(params);
    if (mode != -1 && mode != con->mode) {
        con->mode = mode;
        updated++;
    }

    int mute = mapper_msg_get_mute(params);
    if (mute != -1 && mute != con->muted) {
        con->muted = mute;
        updated++;
    }

    updated += mapper_msg_add_or_update_extra_params(con->extra, params);
    return updated;
}

int mapper_db_add_or_update_connection_params(mapper_db db,
                                              const char *src_name,
                                              const char *dest_name,
                                              mapper_message_t *params)
{
    mapper_db_connection con;
    int rc = 0, updated = 0;

    con = mapper_db_get_connection_by_signal_full_names(db, src_name,
                                                        dest_name);

    if (!con) {
        con = (mapper_db_connection)
            list_new_item(sizeof(mapper_db_connection_t));
        con->extra = table_new();
        rc = 1;
    }

    if (con) {
        updated = update_connection_record_params(con, src_name, dest_name, params);

        if (rc)
            list_prepend_item(con, (void**)&db->registered_connections);

        if (rc || updated) {
            fptr_list cb = db->connection_callbacks;
            while (cb) {
                mapper_db_connection_handler *h = cb->f;
                h(con, rc ? MDB_NEW : MDB_MODIFY, cb->context);
                cb = cb->next;
            }
        }
    }

    return rc;
}

int mapper_db_connection_property_index(mapper_db_connection con,
                                        unsigned int index,
                                        const char **property,
                                        lo_type *type,
                                        const lo_arg **value)
{
    return mapper_db_property_index(con, 0, con->extra,
                                    index, property, type,
                                    value, &condb_table);
}

int mapper_db_connection_property_lookup(mapper_db_connection con,
                                         const char *property,
                                         lo_type *type,
                                         const lo_arg **value)
{
    return mapper_db_property_lookup(con, 0, con->extra,
                                     property, type, value,
                                     &condb_table);
}

mapper_db_connection_t **mapper_db_get_all_connections(mapper_db db)
{
    if (!db->registered_connections)
        return 0;

    list_header_t *lh = list_get_header_by_data(db->registered_connections);

    die_unless(lh->self == &lh->data,
               "bad self pointer in list structure");

    return (mapper_db_connection*)&lh->self;
}

void mapper_db_add_connection_callback(mapper_db db,
                                       mapper_db_connection_handler *h,
                                       void *user)
{
    add_callback(&db->connection_callbacks, h, user);
}

void mapper_db_remove_connection_callback(mapper_db db,
                                          mapper_db_connection_handler *h,
                                          void *user)
{
    remove_callback(&db->connection_callbacks, h, user);
}

static int cmp_query_get_connections_by_device_name(void *context_data,
                                                    mapper_db_connection con)
{
    const char *devname = (const char*)context_data;
    unsigned int devnamelen = strlen(devname);
    return (   strncmp(con->src_name+1, devname, devnamelen)==0
            || strncmp(con->dest_name+1, devname, devnamelen)==0 );
}

mapper_db_connection_t **mapper_db_get_connections_by_device_name(
    mapper_db db, const char *device_name)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    // query skips first '/' in the name if it is provided
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_device_name,
        device_name[0]=='/' ? device_name+1 : device_name, 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_device_name(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

static int cmp_query_get_connections_by_src_dest_device_names(
    void *context_data, mapper_db_connection con)
{
    const char *srcdevname = (const char*)context_data;
    unsigned int srcdevnamelen = strlen(srcdevname);
    const char *destdevname = srcdevname + srcdevnamelen + 1;
    unsigned int destdevnamelen = strlen(destdevname);
    return (   strncmp(con->src_name+1, srcdevname, srcdevnamelen)==0
            && strncmp(con->dest_name+1, destdevname, destdevnamelen)==0 );
}

mapper_db_connection_t **mapper_db_get_connections_by_src_dest_device_names(
    mapper_db db, const char *src_device_name, const char *dest_device_name)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    // query skips first '/' in the name if it is provided
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_src_dest_device_names,
        src_device_name[0]=='/' ? src_device_name+1 : src_device_name,
        dest_device_name[0]=='/' ? dest_device_name+1 : dest_device_name, 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_src_dest_device_names(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

static int cmp_query_get_connections_by_src_signal_name(void *context_data,
                                                        mapper_db_connection con)
{
    const char *src_name = (const char*)context_data;
    const char *map_src_name = con->src_name+1;
    while (*map_src_name && *map_src_name != '/')  // find the signal name
        map_src_name++;
    map_src_name++;
    return strcmp(map_src_name, src_name)==0;
}

mapper_db_connection_t **mapper_db_get_connections_by_src_signal_name(
    mapper_db db, const char *src_signal)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    // query skips first '/' in the name if it is provided
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_src_signal_name,
        src_signal[0]=='/' ? src_signal+1 : src_signal, 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_src_signal_name(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

static int cmp_query_get_connections_by_src_device_and_signal_names(
    void *context_data, mapper_db_connection con)
{
    const char *name = (const char*) context_data;
    return strcmp(con->src_name, name)==0;
}

mapper_db_connection_t **mapper_db_get_connections_by_src_device_and_signal_names(
    mapper_db db, const char *src_device, const char *src_signal)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    // query skips first '/' in both names if it is provided
    char name[1024];
    snprintf(name, 1024, "/%s/%s",
             src_device[0]=='/' ? src_device+1 : src_device,
             src_signal[0]=='/' ? src_signal+1 : src_signal);

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_src_device_and_signal_names,
        name, 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_src_device_and_signal_names(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

static int cmp_query_get_connections_by_dest_signal_name(void *context_data,
                                                         mapper_db_connection con)
{
    const char *dest_name = (const char*)context_data;
    const char *map_dest_name = con->dest_name+1;
    while (*map_dest_name && *map_dest_name != '/')  // find the signal name
        map_dest_name++;
    map_dest_name++;
    return strcmp(map_dest_name, dest_name)==0;
}

mapper_db_connection_t **mapper_db_get_connections_by_dest_signal_name(
    mapper_db db, const char *dest_name)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    // query skips first '/' in the name if it is provided
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_dest_signal_name,
        dest_name[0]=='/' ? dest_name+1 : dest_name, 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_dest_signal_name(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

static int cmp_query_get_connections_by_dest_device_and_signal_names(
    void *context_data, mapper_db_connection con)
{
    const char *name = (const char*) context_data;
    return strcmp(con->dest_name, name)==0;
}

mapper_db_connection_t **mapper_db_get_connections_by_dest_device_and_signal_names(
    mapper_db db, const char *dest_device, const char *dest_signal)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    char name[1024];
    snprintf(name, 1024, "/%s/%s",
             dest_device[0]=='/' ? dest_device+1 : dest_device,
             dest_signal[0]=='/' ? dest_signal+1 : dest_signal);

    // query skips first '/' in both names if it is provided
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_dest_device_and_signal_names,
        name, 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_dest_device_and_signal_names(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

mapper_db_connection mapper_db_get_connection_by_signal_full_names(
    mapper_db db, const char *src_name, const char *dest_name)
{
    mapper_db_connection con = db->registered_connections;
    if (!con)
        return 0;

    while (con) {
        if (strcmp(con->src_name, src_name)==0
            && strcmp(con->dest_name, dest_name)==0)
            return con;
        con = list_get_next(con);
    }
    return 0;
}

static int cmp_query_get_connections_by_device_and_signal_names(
    void *context_data, mapper_db_connection con)
{
    const char *src_name = (const char*) context_data;
    if (strcmp(con->src_name, src_name)!=0)
        return 0;
    const char *dest_name = src_name + strlen(src_name) + 1;
    return strcmp(con->dest_name, dest_name)==0;
}

mapper_db_connection_t **mapper_db_get_connections_by_device_and_signal_names(
    mapper_db db,
    const char *src_device,  const char *src_signal,
    const char *dest_device, const char *dest_signal)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    char inname[1024];
    snprintf(inname, 1024, "/%s/%s",
             (src_device[0]=='/' ? src_device+1 : src_device),
              src_signal[0]=='/' ? src_signal+1 : src_signal);

    char outname[1024];
    snprintf(outname, 1024, "/%s/%s",
             (dest_device[0]=='/' ? dest_device+1 : dest_device),
              dest_signal[0]=='/' ? dest_signal+1 : dest_signal);

    // query skips first '/' in both names if it is provided
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_device_and_signal_names,
        inname, outname, 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_device_and_signal_names(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

static int cmp_get_connections_by_signal_queries(void *context_data,
                                                 mapper_db_connection con)
{
    src_dest_queries_t *qsig = (src_dest_queries_t*) context_data;
    char ctx_backup[1024];

    /* Save the source list context so we can restart the query on the
     * next pass. */
    save_query_single_context(qsig->lh_src_head, ctx_backup, 1024);

    /* Indicate not to free memory at the end of the pass. */
    if (qsig->lh_src_head->query_type == QUERY_DYNAMIC
        && qsig->lh_src_head->query_context)
        qsig->lh_src_head->query_context->query_free = 0;

    /* Find at least one signal in the source list that matches. */
    mapper_db_signal *srcsig = (mapper_db_signal*)&qsig->lh_src_head->self;
    unsigned int devnamelen = strlen((*srcsig)->device_name);
    while (srcsig && *srcsig) {
        if (strncmp((*srcsig)->device_name, con->src_name, devnamelen)==0
            && strcmp((*srcsig)->name, con->src_name+devnamelen)==0)
            break;
        srcsig = mapper_db_signal_next(srcsig);
    }
    mapper_db_signal_done(srcsig);
    restore_query_single_context(qsig->lh_src_head, ctx_backup);
    if (!srcsig)
        return 0;

    /* Save the destination list context so we can restart the query
     * on the next pass. */
    save_query_single_context(qsig->lh_dest_head, ctx_backup, 1024);

    /* Indicate not to free memory at the end of the pass. */
    if (qsig->lh_dest_head->query_type == QUERY_DYNAMIC
        && qsig->lh_dest_head->query_context)
        qsig->lh_dest_head->query_context->query_free = 0;

    /* Find at least one signal in the destination list that matches. */
    mapper_db_signal *destsig = (mapper_db_signal*)&qsig->lh_dest_head->self;
    devnamelen = strlen((*destsig)->device_name);
    while (destsig && *destsig) {
        if (strncmp((*destsig)->device_name, con->dest_name, devnamelen)==0
            && strcmp((*destsig)->name, con->dest_name+devnamelen)==0)
            break;
        destsig = mapper_db_signal_next(destsig);
    }
    restore_query_single_context(qsig->lh_dest_head, ctx_backup);
    mapper_db_signal_done(destsig);
    if (!destsig)
        return 0;

    return 1;
}

mapper_db_connection_t **mapper_db_get_connections_by_signal_queries(
    mapper_db db,
    mapper_db_signal_t **src, mapper_db_signal_t **dest)
{
    mapper_db_connection maps = db->registered_connections;
    if (!maps)
        return 0;

    if (!(src && dest))
        return 0;

    query_info_t *qi = (query_info_t*)
        malloc(sizeof(query_info_t) + sizeof(src_dest_queries_t));

    qi->size = sizeof(query_info_t) + sizeof(src_dest_queries_t);
    qi->query_compare =
        (query_compare_func_t*) cmp_get_connections_by_signal_queries;
    qi->query_free = free_query_src_dest_queries;

    src_dest_queries_t *qdata = (src_dest_queries_t*)&qi->data;

    qdata->lh_src_head = list_get_header_by_self(src);
    qdata->lh_dest_head = list_get_header_by_self(dest);

    list_header_t *lh = (list_header_t*) malloc(LIST_HEADER_SIZE);
    lh->self = maps;
    lh->next = dynamic_query_continuation;
    lh->query_type = QUERY_DYNAMIC;
    lh->query_context = qi;

    if (cmp_get_connections_by_signal_queries(
            &lh->query_context->data, maps))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

mapper_db_connection_t **mapper_db_connection_next(mapper_db_connection_t** c)
{
    return (mapper_db_connection*) iterator_next((void**)c);
}

void mapper_db_connection_done(mapper_db_connection_t **d)
{
    if (!d) return;
    list_header_t *lh = list_get_header_by_data(*d);
    if (lh->query_type == QUERY_DYNAMIC
        && lh->query_context->query_free)
        lh->query_context->query_free(lh);
}

void mapper_db_remove_connections_by_query(mapper_db db,
                                           mapper_db_connection_t **c)
{
    while (c) {
        mapper_db_connection con = *c;
        c = mapper_db_connection_next(c);
        mapper_db_remove_connection(db, con);
    }
}

void mapper_db_remove_connection(mapper_db db, mapper_db_connection con)
{
    if (!con)
        return;

    fptr_list cb = db->connection_callbacks;
    while (cb) {
        mapper_db_connection_handler *h = cb->f;
        h(con, MDB_REMOVE, cb->context);
        cb = cb->next;
    }

    if (con->src_name)
        free(con->src_name);
    if (con->dest_name)
        free(con->dest_name);
    if (con->expression)
        free(con->expression);
    if (con->extra)
        table_free(con->extra, 1);
    list_remove_item(con, (void**)&db->registered_connections);
}

/**** Link records ****/

int mapper_db_link_add_scope(mapper_db_link link,
                             const char *scope)
{
    int i;
    if (!link || !scope)
        return 1;

    // Check if scope is already stored for this link
    uint32_t hash;
    if (strcmp(scope, "all")==0)
        hash = 0;
    else
        hash = crc32(0L, (const Bytef *)scope, strlen(scope));
    for (i=0; i<link->num_scopes; i++)
        if (link->scope_hashes[i] == hash)
            return 1;

    // not found - add a new scope
    i = ++link->num_scopes;
    link->scope_names = realloc(link->scope_names, i * sizeof(char *));
    link->scope_names[i-1] = strdup(scope);
    link->scope_hashes = realloc(link->scope_hashes, i * sizeof(uint32_t));
    link->scope_hashes[i-1] = hash;
    return 0;
}

int mapper_db_link_remove_scope(mapper_db_link link,
                                const char *scope)
{
    int i, j;
    if (!link || !scope)
        return 1;

    uint32_t hash;
    if (strcmp(scope, "all")==0)
        hash = 0;
    else
        hash = crc32(0L, (const Bytef *)scope, strlen(scope));
    for (i=0; i<link->num_scopes; i++) {
        if (link->scope_hashes[i] == hash) {
            free(link->scope_names[i]);
            for (j=i+1; j<link->num_scopes; j++) {
                link->scope_names[j-1] = link->scope_names[j];
                link->scope_hashes[j-1] = link->scope_hashes[j];
            }
            link->num_scopes--;
            link->scope_names = realloc(link->scope_names,
                                        link->num_scopes * sizeof(char *));
            link->scope_hashes = realloc(link->scope_hashes,
                                         link->num_scopes * sizeof(uint32_t));
            return 0;
        }
    }
    return 1;
}

/*! Update information about a given link record based on message
 *  parameters. */
static int update_link_record_params(mapper_db_link link,
                                     const char *src_name,
                                     const char *dest_name,
                                     mapper_message_t *params)
{
    int i, j, num_scopes = 0, updated = 0;
    updated += update_string_if_different(&link->src_name, src_name);
    updated += update_string_if_different(&link->dest_name, dest_name);
    updated += update_int_if_arg(&link->src_port, params, AT_SRC_PORT);
    updated += update_int_if_arg(&link->dest_port, params, AT_DEST_PORT);

    lo_arg **a_scopes = mapper_msg_get_param(params, AT_SCOPE);
    num_scopes = mapper_msg_get_length(params, AT_SCOPE);

    // First remove old scopes that are missing
    for (i=0; i<link->num_scopes; i++) {
        int found = 0;
        for (j=0; j<num_scopes; j++) {
            if (strcmp(link->scope_names[i], &a_scopes[j]->s) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            mapper_db_link_remove_scope(link, link->scope_names[i]);
            updated++;
        }
    }
    // ...then add any new scopes
    for (i=0; i<num_scopes; i++)
        updated += (1 - mapper_db_link_add_scope(link, &a_scopes[i]->s));

    if (num_scopes != link->num_scopes) {
        link->num_scopes = num_scopes;
        updated++;
    }

    updated += mapper_msg_add_or_update_extra_params(link->extra, params);
    return updated;
}

int mapper_db_add_or_update_link_params(mapper_db db,
                                        const char *src_name,
                                        const char *dest_name,
                                        mapper_message_t *params)
{
    mapper_db_link link;
    int rc = 0, updated = 0;

    link = mapper_db_get_link_by_src_dest_names(db, src_name, dest_name);

    if (!link) {
        link = (mapper_db_link) list_new_item(sizeof(mapper_db_link_t));
        link->extra = table_new();
        rc = 1;
    }

    if (link) {
        updated = update_link_record_params(link, src_name, dest_name, params);

        if (rc)
            list_prepend_item(link, (void**)&db->registered_links);

        if (rc || updated) {
            fptr_list cb = db->link_callbacks;
            while (cb) {
                mapper_db_link_handler *h = cb->f;
                h(link, rc ? MDB_NEW : MDB_MODIFY, cb->context);
                cb = cb->next;
            }
        }
    }
    else {
        trace("couldn't find or create link in "
              "mapper_db_add_or_update_link_params()\n");
    }

    return rc;
}

int mapper_db_link_property_index(mapper_db_link link, unsigned int index,
                                  const char **property, lo_type *type,
                                  const lo_arg **value)
{
    return mapper_db_property_index(link, 0, link->extra,
                                    index, property, type,
                                    value, &linkdb_table);
}

int mapper_db_link_property_lookup(mapper_db_link link,
                                   const char *property,
                                   lo_type *type,
                                   const lo_arg **value)
{
    return mapper_db_property_lookup(link, 0, link->extra,
                                     property, type, value,
                                     &linkdb_table);
}

void mapper_db_add_link_callback(mapper_db db,
                                 mapper_db_link_handler *h, void *user)
{
    add_callback(&db->link_callbacks, h, user);
}

void mapper_db_remove_link_callback(mapper_db db,
                                    mapper_db_link_handler *h, void *user)
{
    remove_callback(&db->link_callbacks, h, user);
}

mapper_db_link_t **mapper_db_get_all_links(mapper_db db)
{
    if (!db->registered_links)
        return 0;

    list_header_t *lh = list_get_header_by_data(db->registered_links);

    die_unless(lh->self == &lh->data,
               "bad self pointer in list structure");

    return (mapper_db_link*)&lh->self;
}

mapper_db_link mapper_db_get_link_by_src_dest_names(
    mapper_db db,
    const char *src_device_name, const char *dest_device_name)
{
    mapper_db_link link = db->registered_links;
    while (link) {
        if (strcmp(link->src_name, src_device_name)==0
            && strcmp(link->dest_name, dest_device_name)==0)
            break;
        link = list_get_next(link);
    }
    return link;
}

static int cmp_query_get_links_by_device_name(void *context_data,
                                              mapper_db_link link)
{
    const char *devname = (const char*)context_data;
    return (   strcmp(link->src_name+1, devname)==0
            || strcmp(link->dest_name+1, devname)==0 );
}

mapper_db_link_t **mapper_db_get_links_by_device_name(
    mapper_db db, const char *device_name)
{
    mapper_db_link link = db->registered_links;
    if (!link)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_links_by_device_name,
        device_name[0]=='/' ? device_name+1 : device_name, 0);

    lh->self = link;

    if (cmp_query_get_links_by_device_name(
            &lh->query_context->data, link))
        return (mapper_db_link*)&lh->self;

    return (mapper_db_link*)dynamic_query_continuation(lh);
}

static int cmp_query_get_links_by_src_device_name(void *context_data,
                                                  mapper_db_link link)
{
    const char *src = (const char*)context_data;
    return strcmp(link->src_name, src)==0;
}

mapper_db_link_t **mapper_db_get_links_by_src_device_name(
    mapper_db db, const char *src_device_name)
{
    mapper_db_link link = db->registered_links;
    if (!link)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_links_by_src_device_name,
        src_device_name, 0);

    lh->self = link;

    if (cmp_query_get_links_by_src_device_name(
            &lh->query_context->data, link))
        return (mapper_db_link*)&lh->self;

    return (mapper_db_link*)dynamic_query_continuation(lh);
}

static int cmp_query_get_links_by_dest_device_name(void *context_data,
                                                   mapper_db_link link)
{
    const char *dest = (const char*)context_data;
    return strcmp(link->dest_name, dest)==0;
}

mapper_db_link_t **mapper_db_get_links_by_dest_device_name(
    mapper_db db, const char *dest_device_name)
{
    mapper_db_link link = db->registered_links;
    if (!link)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_links_by_dest_device_name,
        dest_device_name, 0);

    lh->self = link;

    if (cmp_query_get_links_by_dest_device_name(
            &lh->query_context->data, link))
        return (mapper_db_link*)&lh->self;

    return (mapper_db_link*)dynamic_query_continuation(lh);
}

static int cmp_get_links_by_src_dest_devices(void *context_data,
                                             mapper_db_link link)
{
    src_dest_queries_t *qdata = (src_dest_queries_t*)context_data;
    char ctx_backup[1024];

    /* Save the source list context so we can restart the query on the
     * next pass. */
    save_query_single_context(qdata->lh_src_head, ctx_backup, 1024);

    /* Indicate not to free memory at the end of the pass. */
    if (qdata->lh_src_head->query_type == QUERY_DYNAMIC
        && qdata->lh_src_head->query_context)
        qdata->lh_src_head->query_context->query_free = 0;

    /* Find at least one device in the source list that matches. */
    mapper_db_device *src = (mapper_db_device*)&qdata->lh_src_head->self;
    while (src && *src) {
        if (strcmp((*src)->name, link->src_name)==0)
            break;
        src = mapper_db_device_next(src);
    }
    mapper_db_device_done(src);
    restore_query_single_context(qdata->lh_src_head, ctx_backup);
    if (!src)
        return 0;

    /* Save the destination list context so we can restart the query
     * on the next pass. */
    save_query_single_context(qdata->lh_dest_head, ctx_backup, 1024);

    /* Indicate not to free memory at the end of the pass. */
    if (qdata->lh_dest_head->query_type == QUERY_DYNAMIC
        && qdata->lh_dest_head->query_context)
        qdata->lh_dest_head->query_context->query_free = 0;

    /* Find at least one device in the destination list that matches. */
    mapper_db_device *dest = (mapper_db_device*)&qdata->lh_dest_head->self;
    while (dest && *dest) {
        if (strcmp((*dest)->name, link->dest_name)==0)
            break;
        dest = mapper_db_device_next(dest);
    }
    restore_query_single_context(qdata->lh_dest_head, ctx_backup);
    mapper_db_device_done(dest);
    if (!dest)
        return 0;

    return 1;
}

mapper_db_link_t **mapper_db_get_links_by_src_dest_devices(
    mapper_db db,
    mapper_db_device_t **src_device_list,
    mapper_db_device_t **dest_device_list)
{
    mapper_db_link link = db->registered_links;
    if (!link)
        return 0;

    query_info_t *qi = (query_info_t*)
        malloc(sizeof(query_info_t) + sizeof(src_dest_queries_t));

    qi->size = sizeof(query_info_t) + sizeof(src_dest_queries_t);
    qi->query_compare =
        (query_compare_func_t*) cmp_get_links_by_src_dest_devices;
    qi->query_free = free_query_src_dest_queries;

    src_dest_queries_t *qdata = (src_dest_queries_t*)&qi->data;

    qdata->lh_src_head = list_get_header_by_self(src_device_list);
    qdata->lh_dest_head = list_get_header_by_self(dest_device_list);

    list_header_t *lh = (list_header_t*) malloc(LIST_HEADER_SIZE);
    lh->self = link;
    lh->next = dynamic_query_continuation;
    lh->query_type = QUERY_DYNAMIC;
    lh->query_context = qi;

    if (cmp_get_links_by_src_dest_devices(
            &lh->query_context->data, link))
        return (mapper_db_link*)&lh->self;

    return (mapper_db_link*)dynamic_query_continuation(lh);
}

mapper_db_link_t **mapper_db_link_next(mapper_db_link_t** p)
{
    return (mapper_db_link*) iterator_next((void**)p);
}

void mapper_db_link_done(mapper_db_link_t **s)
{
    if (!s) return;
    list_header_t *lh = list_get_header_by_data(*s);
    if (lh->query_type == QUERY_DYNAMIC
        && lh->query_context->query_free)
        lh->query_context->query_free(lh);
}

void mapper_db_remove_links_by_query(mapper_db db, mapper_db_link_t **s)
{
    while (s) {
        mapper_db_link link = *s;
        s = mapper_db_link_next(s);
        mapper_db_remove_link(db, link);
    }
}

void mapper_db_remove_link(mapper_db db, mapper_db_link link)
{
    int i;
    if (!link)
        return;

    fptr_list cb = db->link_callbacks;
    while (cb) {
        mapper_db_link_handler *h = cb->f;
        h(link, MDB_REMOVE, cb->context);
        cb = cb->next;
    }

    if (link->src_name)
        free(link->src_name);
    if (link->dest_name)
        free(link->dest_name);
    if (link->num_scopes && link->scope_names) {
        for (i=0; i<link->num_scopes; i++)
            free(link->scope_names[i]);
        free(link->scope_names);
        free(link->scope_hashes);
    }
    if (link->extra)
        table_free(link->extra, 1);
    list_remove_item(link, (void**)&db->registered_links);
}

void mapper_db_remove_all_callbacks(mapper_db db)
{
    fptr_list cb;
    while ((cb = db->device_callbacks)) {
        db->device_callbacks = db->device_callbacks->next;
        free(cb);
    }
    while ((cb = db->signal_callbacks)) {
        db->signal_callbacks = db->signal_callbacks->next;
        free(cb);
    }
    while ((cb = db->link_callbacks)) {
        db->link_callbacks = db->link_callbacks->next;
        free(cb);
    }
    while ((cb = db->connection_callbacks)) {
        db->connection_callbacks = db->connection_callbacks->next;
        free(cb);
    }
}
