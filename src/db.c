
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

inline static const char *skip_slash(const char *string)
{
    return string + (string && string[0]=='/');
}

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

static int update_string_if_different(char **pdest_str,
                                      const char *src_str)
{
    if (!(*pdest_str) || strcmp((*pdest_str), src_str)) {
        char *str = (char*) realloc((void*)(*pdest_str), strlen(src_str)+1);
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
    const char *type = 0;
    lo_arg **a = mapper_msg_get_param(params, field, &type, 0);

    if (a && (*a) && (type[0]=='s' || type[0]=='S')
        && (!(*pdest_str) || strcmp((*pdest_str), &(*a)->s))) {
        char *str = (char*) realloc((void*)(*pdest_str), strlen(&(*a)->s)+1);
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
    const char *type = 0;
    lo_arg **a = mapper_msg_get_param(params, field, &type, 0);

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
    const char *type = 0;
    lo_arg **a = mapper_msg_get_param(params, field, &type, 0);

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
    union {
        int indirect;
        int alt_type;
    };
    int length;     // lengths stored as negatives, lookup lengths as offsets
    int offset;
} property_table_value_t;


#define SIG_OFFSET(x) offsetof(mapper_db_signal_t, x)
#define DEV_OFFSET(x) offsetof(mapper_db_device_t, x)
#define CON_OFFSET(x) offsetof(mapper_db_connection_t, x)
#define SLOT_OFFSET(x) offsetof(mapper_db_connection_slot_t, x)

#define SIG_TYPE        (SIG_OFFSET(type))
#define SIG_LENGTH      (SIG_OFFSET(length))

#define SLOT_TYPE       (SLOT_OFFSET(type))
#define SLOT_LENGTH     (SLOT_OFFSET(length))
#define NUM_SCOPES      (CON_OFFSET(scope.size))
#define NUM_SOURCES     (CON_OFFSET(num_sources))

#define LOCAL_TYPE      (CON_OFFSET(local_type))
#define REMOTE_TYPE     (CON_OFFSET(remote_type))
#define LOCAL_LENGTH    (CON_OFFSET(local_length))
#define REMOTE_LENGTH   (CON_OFFSET(remote_length))
#define NUM_SCOPES      (CON_OFFSET(scope.size))

/* Here type 'o', which is not an OSC type, was reserved to mean "same
 * type as the signal's type".  The lookup and index functions will
 * return the sig->type instead of the value's type. */
static property_table_value_t sig_values[] = {
    { 's', {1},        -1,         SIG_OFFSET(description) },
    { 'i', {0},        -1,         SIG_OFFSET(direction) },
    { 'i', {0},        -1,         SIG_OFFSET(length) },
    { 'o', {SIG_TYPE}, SIG_LENGTH, SIG_OFFSET(maximum) },
    { 'o', {SIG_TYPE}, SIG_LENGTH, SIG_OFFSET(minimum) },
    { 's', {1},        -1,         SIG_OFFSET(name) },
    { 'f', {0},        -1,         SIG_OFFSET(rate) },
    { 'c', {0},        -1,         SIG_OFFSET(type) },
    { 's', {1},        -1,         SIG_OFFSET(unit) },
    { 'i', {0},         0,         SIG_OFFSET(user_data) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t sig_strings[] = {
    { "description", &sig_values[0] },
    { "direction",   &sig_values[1] },
    { "length",      &sig_values[2] },
    { "max",         &sig_values[3] },
    { "min",         &sig_values[4] },
    { "name",        &sig_values[5] },
    { "rate",        &sig_values[6] },
    { "type",        &sig_values[7] },
    { "unit",        &sig_values[8] },
    { "user_data",   &sig_values[9] },
};

static mapper_string_table_t sig_table = { sig_strings, 10, 10 };

static property_table_value_t dev_values[] = {
    { 's', {1}, -1, DEV_OFFSET(description) },
    { 's', {1}, -1, DEV_OFFSET(host) },
    { 's', {1}, -1, DEV_OFFSET(lib_version) },
    { 's', {1}, -1, DEV_OFFSET(name) },
    { 'i', {0}, -1, DEV_OFFSET(num_connections_in) },
    { 'i', {0}, -1, DEV_OFFSET(num_connections_out) },
    { 'i', {0}, -1, DEV_OFFSET(num_inputs) },
    { 'i', {0}, -1, DEV_OFFSET(num_outputs) },
    { 'i', {0}, -1, DEV_OFFSET(port) },
    { 't', {0}, -1, DEV_OFFSET(synced) },
    { 'i', {0},  0, DEV_OFFSET(user_data) },
    { 'i', {0}, -1, DEV_OFFSET(version) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t dev_strings[] = {
    { "description",         &dev_values[0] },
    { "host",                &dev_values[1] },
    { "lib_version",         &dev_values[2] },
    { "name",                &dev_values[3] },
    { "num_connections_in",  &dev_values[4] },
    { "num_connections_out", &dev_values[5] },
    { "num_inputs",          &dev_values[6] },
    { "num_outputs",         &dev_values[7] },
    { "port",                &dev_values[8] },
    { "synced",              &dev_values[9] },
    { "user_data",           &dev_values[10] },
    { "version",             &dev_values[11] },
};

static mapper_string_table_t dev_table = { dev_strings, 12, 12 };

static property_table_value_t slot_values[] = {
    { 'i', {0},         -1,          SLOT_OFFSET(bound_max) },
    { 'i', {0},         -1,          SLOT_OFFSET(bound_min) },
    { 'i', {0},         -1,          SLOT_OFFSET(cause_update) },
    { 'i', {0},         -1,          SLOT_OFFSET(length) },
    { 'o', {SLOT_TYPE}, SLOT_LENGTH, SLOT_OFFSET(maximum) },
    { 'o', {SLOT_TYPE}, SLOT_LENGTH, SLOT_OFFSET(minimum) },
    { 'i', {0},         -1,          SLOT_OFFSET(send_as_instance) },
    { 'c', {0},         -1,          SLOT_OFFSET(type) },
};

static string_table_node_t slot_strings[] = {
    { "bound_max",        &slot_values[0] },
    { "bound_min",        &slot_values[1] },
    { "cause_update",     &slot_values[2] },
    { "length",           &slot_values[3] },
    { "maximum",          &slot_values[4] },
    { "minimum",          &slot_values[5] },
    { "send_as_instance", &slot_values[6] },
    { "type",             &slot_values[7] },
};

static mapper_string_table_t slot_table = { slot_strings, 8, 8 };

static property_table_value_t con_values[] = {
    { 's', {1}, -1,         CON_OFFSET(expression) },
    { 'i', {0}, -1,         CON_OFFSET(hash)},
    { 'i', {0}, -1,         CON_OFFSET(id) },
    { 'i', {0}, -1,         CON_OFFSET(mode) },
    { 'i', {0}, -1,         CON_OFFSET(muted) },
    { 'i', {0}, -1,         CON_OFFSET(scope.size) },
    { 'i', {0}, -1,         CON_OFFSET(num_sources) },
    { 'i', {0}, -1,         CON_OFFSET(process_location) },
    { 's', {1}, NUM_SCOPES, CON_OFFSET(scope.names) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t con_strings[] = {
    { "expression",         &con_values[0] },
    { "hash",               &con_values[1] },
    { "id",                 &con_values[2] },
    { "mode",               &con_values[3] },
    { "muted",              &con_values[4] },
    { "num_scopes",         &con_values[5] },
    { "num_sources",        &con_values[6] },
    { "process_at",         &con_values[7] },
    { "scope_names",        &con_values[8] },
};

static mapper_string_table_t con_table = { con_strings, 9, 9 };

/* Generic index and lookup functions to which the above tables would
 * be passed. These are called for specific types below. */

static int mapper_db_property_index(void *thestruct, table extra,
                                    unsigned int index, const char **property,
                                    char *type, const void **value,
                                    int *length, table proptable)
{
    die_unless(type!=0, "type parameter cannot be null.\n");
    die_unless(value!=0, "value parameter cannot be null.\n");
    die_unless(length!=0, "length parameter cannot be null.\n");

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
            void **pp = (void**)((char*)thestruct + prop->offset);
            if (*pp) {
                if (j==index) {
                    if (property)
                        *property = table_key_at_index(proptable, i);
                    if (prop->type == 'o')
                        *type = *((char*)thestruct + prop->alt_type);
                    else
                        *type = prop->type;
                    if (prop->length > 0)
                        *length = *(int*)((char*)thestruct + prop->length);
                    else
                        *length = prop->length * -1;
                    if (prop->type == 's' && prop->length > 0 && *length == 1) {
                        // In this case pass the char* rather than the array
                        char **temp = *pp;
                        *value = temp[0];
                    }
                    else
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
                if (prop->type == 'o')
                    *type = *((char*)thestruct + prop->alt_type);
                else
                    *type = prop->type;
                *value = (lo_arg*)((char*)thestruct + prop->offset);
                if (prop->length > 0)
                    *length = *(int*)((char*)thestruct + prop->length);
                else
                    *length = prop->length * -1;
                return 0;
            }
            j++;
        }
    }

    if (extra) {
        index -= j;
        mapper_prop_value_t *val;
        val = table_value_at_index_p(extra, index);
        if (val) {
            if (property)
                *property = table_key_at_index(extra, index);
            *type = val->type;
            *value = val->value;
            *length = val->length;
            return 0;
        }
    }

    return 1;
}

static int mapper_db_property_lookup(void *thestruct, table extra,
                                     const char *property, char *type,
                                     const void **value, int *length, table proptable)
{
    die_unless(type!=0, "type parameter cannot be null.\n");
    die_unless(value!=0, "value parameter cannot be null.\n");
    die_unless(length!=0, "length parameter cannot be null.\n");

    const mapper_prop_value_t *val;
    if (extra) {
        val = table_find_p(extra, property);
        if (val) {
            *type = val->type;
            *value = val->value;
            *length = val->length;
            return 0;
        }
    }

    property_table_value_t *prop;
    prop = table_find_p(proptable, property);
    if (prop) {
        if (prop->type == 'o')
            *type = *((char*)thestruct + prop->alt_type);
        else
            *type = prop->type;
        if (prop->length > 0)
            *length = *(int*)((char*)thestruct + prop->length);
        else
            *length = prop->length * -1;
        if (prop->indirect) {
            void **pp = (void**)((char*)thestruct + prop->offset);
            if (*pp) {
                if (prop->type == 's' && prop->length > 0 && *length == 1) {
                    // In this case pass the char* rather than the array
                    char **temp = *pp;
                    *value = temp[0];
                }
                else
                    *value = *pp;
            }
            else
                return 1;
        }
        else
            *value = (void*)((char*)thestruct + prop->offset);
        return 0;
    }
    return 1;
}

/**** Device records ****/

/*! Update information about a given device record based on message
 *  parameters. */
static int update_device_record_params(mapper_db_device reg,
                                       const char *name,
                                       mapper_message_t *params,
                                       mapper_timetag_t *current_time)
{
    int updated = 0;
    const char *no_slash = skip_slash(name);

    updated += update_string_if_different(&reg->name, no_slash);
    if (updated)
        reg->name_hash = crc32(0L, (const Bytef *)no_slash, strlen(no_slash));

    if (current_time)
        mapper_timetag_cpy(&reg->synced, *current_time);

    if (!params)
        return updated;

    updated += update_string_if_arg(&reg->host, params, AT_IP);

    updated += update_string_if_arg(&reg->lib_version, params, AT_LIB_VERSION);

    updated += update_int_if_arg(&reg->port, params, AT_PORT);

    updated += update_int_if_arg(&reg->num_inputs, params, AT_NUM_INPUTS);

    updated += update_int_if_arg(&reg->num_outputs, params, AT_NUM_OUTPUTS);

    updated += update_int_if_arg(&reg->num_connections_in, params,
                                 AT_NUM_CONNECTIONS_IN);

    updated += update_int_if_arg(&reg->num_connections_out, params,
                                 AT_NUM_CONNECTIONS_OUT);

    updated += update_int_if_arg(&reg->version, params, AT_REV);

    updated += mapper_msg_add_or_update_extra_params(reg->extra, params);

    return updated;
}

mapper_db_device mapper_db_add_or_update_device_params(mapper_db db,
                                                       const char *name,
                                                       mapper_message_t *params,
                                                       mapper_timetag_t *current_time)
{
    mapper_db_device dev = mapper_db_get_device_by_name(db, name);
    int rc = 0, updated = 0;

    if (!dev) {
        dev = (mapper_db_device) list_new_item(sizeof(*dev));
        dev->extra = table_new();
        rc = 1;

        list_prepend_item(dev, (void**)&db->registered_devices);
    }

    if (dev) {
        updated = update_device_record_params(dev, name, params, current_time);

        if (rc || updated) {
            fptr_list cb = db->device_callbacks;
            while (cb) {
                mapper_db_device_handler *h = cb->f;
                h(dev, rc ? MDB_NEW : MDB_MODIFY, cb->context);
                cb = cb->next;
            }
        }
    }

    return dev;
}

int mapper_db_device_property_index(mapper_db_device dev, unsigned int index,
                                    const char **property, char *type,
                                    const void **value, int *length)
{
    return mapper_db_property_index(dev, dev->extra, index, property, type,
                                    value, length, &dev_table);
}

int mapper_db_device_property_lookup(mapper_db_device dev, const char *property,
                                     char *type, const void **value, int *length)
{
    return mapper_db_property_lookup(dev, dev->extra, property, type,
                                     value, length, &dev_table);
}

static void db_remove_device_internal(mapper_db db, mapper_db_device dev,
                                      int quiet)
{
    if (!dev)
        return;

    mapper_db_remove_connections_by_query(db,
        mapper_db_get_connections_by_device_name(db, dev->name));

    mapper_db_remove_signals_by_query(db,
        mapper_db_get_signals_by_device_name(db, dev->name));

    if (!quiet) {
        fptr_list cb = db->device_callbacks;
        while (cb) {
            mapper_db_device_handler *h = cb->f;
            h(dev, MDB_REMOVE, cb->context);
            cb = cb->next;
        }
    }

    if (dev->name)
        free(dev->name);
    if (dev->description)
        free(dev->description);
    if (dev->host)
        free(dev->host);
    if (dev->lib_version)
        free(dev->lib_version);
    if (dev->extra)
        table_free(dev->extra, 1);
    list_remove_item(dev, (void**)&db->registered_devices);
}

void mapper_db_remove_device_by_name(mapper_db db, const char *name)
{
    mapper_db_device dev = mapper_db_get_device_by_name(db, name);
    if (!dev)
        return;
    db_remove_device_internal(db, dev, 0);
}

mapper_db_device mapper_db_get_device_by_name(mapper_db db,
                                              const char *name)
{
    const char *no_slash = skip_slash(name);
    mapper_db_device reg = db->registered_devices;
    while (reg) {
        if (strcmp(reg->name, no_slash)==0)
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

#ifdef DEBUG
static void print_connection_slot(const char *label, int index,
                                  mapper_db_connection_slot s)
{
    printf("%s", label);
    if (index > -1)
        printf(" %d", index);
    printf(": '%s':'%s'\n", s->signal->device->name, s->signal->name);
    printf("         bound_min=%s\n",
           mapper_get_boundary_action_string(s->bound_min));
    printf("         bound_max=%s\n",
           mapper_get_boundary_action_string(s->bound_max));
    if (s->minimum) {
        printf("         minimum=");
        mapper_prop_pp(s->type, s->length, s->minimum);
        printf("\n");
    }
    if (s->maximum) {
        printf("         maximum=");
        mapper_prop_pp(s->type, s->length, s->maximum);
        printf("\n");
    }
    printf("         cause_update=%s\n", s->cause_update ? "yes" : "no");
}
#endif

void mapper_db_dump(mapper_db db)
{
#ifdef DEBUG
    int i;

    mapper_db_device reg = db->registered_devices;
    printf("Registered devices:\n");
    while (reg) {
        printf("  name='%s', host='%s', port=%d\n", reg->name, reg->host, reg->port);
        reg = list_get_next(reg);
    }

    mapper_db_signal sig = db->registered_signals;
    printf("Registered signals:\n");
    while (sig) {
        printf("  name='%s':'%s' ", sig->device->name, sig->name);
        switch (sig->direction) {
            case DI_BOTH:
                printf("(input/output)\n");
                break;
            case DI_OUTGOING:
                printf("(output)\n");
                break;
            case DI_INCOMING:
                printf("(input)\n");
                break;
            default:
                printf("(unknown)\n");
                break;
        }
        sig = list_get_next(sig);
    }

    mapper_db_connection con = db->registered_connections;
    printf("Registered connections:\n");
    while (con) {
        printf("  hash=%d\n", con->hash);
        if (con->num_sources == 1)
            print_connection_slot("    source slot", -1, &con->sources[0]);
        else {
            for (i = 0; i < con->num_sources; i++)
                print_connection_slot("    source slot", i, &con->sources[i]);
        }
        print_connection_slot("    destination slot", -1, &con->destination);
        printf("    mode='%s'\n", mapper_get_mode_type_string(con->mode));
        printf("    expression='%s'\n", con->expression);
        printf("    muted='%s'\n", con->muted ? "yes" : "no");
        con = list_get_next(con);
    }
#endif
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

void mapper_db_check_device_status(mapper_db db, uint32_t thresh_time_sec)
{
    mapper_db_device reg = db->registered_devices;
    while (reg) {
        // check if device has "checked in" recently
        // this could be /sync ping or any sent metadata
        if (reg->synced.sec && (reg->synced.sec < thresh_time_sec)) {
            fptr_list cb = db->device_callbacks;
            while (cb) {
                mapper_db_device_handler *h = cb->f;
                h(reg, MDB_UNRESPONSIVE, cb->context);
                cb = cb->next;
            }
        }
        reg = list_get_next(reg);
    }
}

int mapper_db_flush(mapper_db db, uint32_t current_time,
                    uint32_t timeout, int quiet)
{
    mapper_db_device reg = db->registered_devices;
    int removed = 0;
    while (reg) {
        if (reg->synced.sec && (current_time - reg->synced.sec > timeout)) {
            db_remove_device_internal(db, reg, quiet);
            removed++;
        }
        reg = list_get_next(reg);
    }
    return removed;
}

/**** Signals ****/

/*! Update information about a given signal record based on message
 *  parameters. */
static int update_signal_record_params(mapper_db_signal sig,
                                       mapper_message_t *params)
{
    lo_arg **args;
    const char *types = 0;
    int length, updated = 0, result;

    if (!params)
        return updated;

    updated += update_int_if_arg(&sig->id, params, AT_ID);

    updated += update_char_if_arg(&sig->type, params, AT_TYPE);

    updated += update_int_if_arg(&sig->length, params, AT_LENGTH);

    updated += update_string_if_arg((char**)&sig->unit, params, AT_UNITS);

    /* @max */
    args = mapper_msg_get_param(params, AT_MAX, &types, &length);
    if (args && types) {
        if (length == sig->length) {
            if (!sig->maximum)
                sig->maximum = calloc(1, length * mapper_type_size(sig->type));
            int i;
            for (i=0; i<length; i++) {
                result = propval_set_from_lo_arg(sig->maximum, sig->type,
                                                 args[i], types[i], i);
                if (result == -1) {
                    free(sig->maximum);
                    sig->maximum = 0;
                    break;
                }
                else
                    updated += result;
            }
        }
    }

    /* @min */
    args = mapper_msg_get_param(params, AT_MIN, &types, &length);
    if (args && types) {
        if (length == sig->length) {
            if (!sig->minimum)
                sig->minimum = calloc(1, length * mapper_type_size(sig->type));
            int i;
            for (i=0; i<length; i++) {
                result = propval_set_from_lo_arg(sig->minimum, sig->type,
                                                 args[i], types[i], i);
                if (result == -1) {
                    free(sig->minimum);
                    sig->minimum = 0;
                    break;
                }
                else
                    updated += result;
            }
        }
    }

    int direction = mapper_msg_get_signal_direction(params);
    if (direction && direction != sig->direction) {
        sig->direction = direction;
        updated++;
    }

    updated += update_int_if_arg(&sig->num_instances, params, AT_INSTANCES);

    updated += mapper_msg_add_or_update_extra_params(sig->extra, params);

    return updated;
}

mapper_db_signal mapper_db_add_or_update_signal_params(mapper_db db,
                                                       const char *name,
                                                       const char *device_name,
                                                       mapper_message_t *params)
{
    mapper_db_signal sig;
    int rc = 0, updated = 0;

    sig = mapper_db_get_signal_by_device_and_signal_names(db, device_name, name);
    if (!sig) {
        sig = (mapper_db_signal) list_new_item(sizeof(mapper_db_signal_t));

        // also add device record if necessary
        sig->device = mapper_db_add_or_update_device_params(db, device_name, 0, 0);

        if (params) {
            int direction = mapper_msg_get_signal_direction(params);
            if (direction & DI_INCOMING)
                sig->device->num_inputs++;
            if (direction & DI_OUTGOING)
                sig->device->num_outputs++;
        }

        // Defaults (int, length=1)
        mapper_db_signal_init(sig, 'i', 1, name, 0);
        rc = 1;
    }

    if (sig) {
        update_signal_record_params(sig, params);

        if (rc) {
            list_prepend_item(sig, (void**)(&db->registered_signals));
        }

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
    return sig;
}

void mapper_db_signal_init(mapper_db_signal sig, char type, int length,
                           const char *name, const char *unit)
{
    sig->direction = 0;
    sig->type = type;
    sig->length = length;
    sig->unit = unit ? strdup(unit) : 0;
    sig->extra = table_new();
    sig->minimum = sig->maximum = 0;
    sig->description = 0;

    if (!name)
        return;
    name = skip_slash(name);
    int len = strlen(name)+2;
    sig->path = malloc(len);
    snprintf(sig->path, len, "/%s", name);
    sig->name = (char*)sig->path+1;
}

int mapper_db_signal_property_index(mapper_db_signal sig, unsigned int index,
                                    const char **property, char *type,
                                    const void **value, int *length)
{
    return mapper_db_property_index(sig, sig->extra, index, property, type,
                                    value, length, &sig_table);
}

int mapper_db_signal_property_lookup(mapper_db_signal sig, const char *property,
                                     char *type, const void **value, int *length)
{
    return mapper_db_property_lookup(sig, sig->extra, property, type,
                                     value, length, &sig_table);
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

static int cmp_query_signals(void *context_data, mapper_db_signal sig)
{
    const char direction = *((const char*)context_data);
    if (direction == 'i')
        return sig->direction & DI_INCOMING;
    else
        return sig->direction & DI_OUTGOING;
}

mapper_db_signal_t **mapper_db_get_all_inputs(mapper_db db)
{
    if (!db->registered_signals)
        return 0;

    mapper_db_signal sig = db->registered_signals;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_signals, "i", 0);

    lh->self = sig;

    if (cmp_query_signals(&lh->query_context->data, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal_t **mapper_db_get_all_outputs(mapper_db db)
{
    if (!db->registered_signals)
        return 0;

    mapper_db_signal sig = db->registered_signals;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_signals, "o", 0);

    lh->self = sig;

    if (cmp_query_signals(&lh->query_context->data, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal_t **mapper_db_get_all_signals(mapper_db db)
{
    if (!db->registered_signals)
        return 0;

    list_header_t *lh = list_get_header_by_data(db->registered_signals);

    die_unless(lh->self == &lh->data, "bad self pointer in list structure");

    return (mapper_db_signal*)&lh->self;
}

static int cmp_query_signal_exact_device_name(void *context_data,
                                              mapper_db_signal sig)
{
    const char *device_name = (const char*)context_data;
    const char direction = *((const char*)context_data
                             + strlen(device_name) + 1);
    if (direction == 'i')
        return ((sig->direction & DI_INCOMING)
                && strcmp(device_name, sig->device->name)==0);
    else if (direction == 'o')
        return ((sig->direction & DI_OUTGOING)
                && strcmp(device_name, sig->device->name)==0);
    else
        return strcmp(device_name, sig->device->name)==0;
}

mapper_db_signal_t **mapper_db_get_signals_by_device_name(
    mapper_db db, const char *device_name)
{
    mapper_db_signal sig = db->registered_signals;
    if (!sig)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_signal_exact_device_name,
        skip_slash(device_name), "a", 0);

    lh->self = sig;

    if (cmp_query_signal_exact_device_name(&lh->query_context->data, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal_t **mapper_db_get_inputs_by_device_name(
    mapper_db db, const char *device_name)
{
    mapper_db_signal sig = db->registered_signals;
    if (!sig)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_signal_exact_device_name,
        skip_slash(device_name), "i", 0);

    lh->self = sig;

    if (cmp_query_signal_exact_device_name(&lh->query_context->data, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal_t **mapper_db_get_outputs_by_device_name(
    mapper_db db, const char *device_name)
{
    mapper_db_signal sig = db->registered_signals;
    if (!sig)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_signal_exact_device_name,
        skip_slash(device_name), "o", 0);

    lh->self = sig;

    if (cmp_query_signal_exact_device_name(&lh->query_context->data, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal mapper_db_get_signal_by_device_and_signal_names(
    mapper_db db, const char *device_name, const char *signal_name)
{
    mapper_db_device dev = mapper_db_get_device_by_name(db, device_name);
    if (!dev)
        return 0;
    mapper_db_signal sig = db->registered_signals;
    if (!sig)
        return 0;

    while (sig) {
        if (sig->device == dev && strcmp(sig->name, skip_slash(signal_name))==0)
            return sig;
        sig = list_get_next(sig);
    }
    return 0;
}

mapper_db_signal mapper_db_get_input_by_device_and_signal_names(
    mapper_db db, const char *device_name, const char *signal_name)
{
    mapper_db_signal sig = db->registered_signals;
    if (!sig)
        return 0;

    while (sig) {
        if ((sig->direction & DI_INCOMING)
            && strcmp(sig->device->name, skip_slash(device_name))==0
            && strcmp(sig->name, skip_slash(signal_name))==0)
            return sig;
        sig = list_get_next(sig);
    }
    return 0;
}

mapper_db_signal mapper_db_get_output_by_device_and_signal_names(
    mapper_db db, const char *device_name, const char *signal_name)
{
    mapper_db_signal sig = db->registered_signals;
    if (!sig)
        return 0;

    while (sig) {
        if ((sig->direction & DI_OUTGOING)
            && strcmp(sig->device->name, skip_slash(device_name))==0
            && strcmp(sig->name, skip_slash(signal_name))==0)
            return sig;
        sig = list_get_next(sig);
    }
    return 0;
}

mapper_db_signal mapper_db_get_signal_by_full_name(mapper_db db,
                                                   const char *full_name)
{
    mapper_db_signal sig = db->registered_signals;
    if (!sig)
        return 0;

    const char *no_slash = skip_slash(full_name);

    while (sig) {
        int len = strlen(sig->device->name);
        if (len < strlen(no_slash) && full_name[len] == '/') {
            if (strncmp(sig->device->name, no_slash, len)==0
                && strcmp(sig->name, no_slash+len)==0)
                return sig;
        }
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
    const char direction = *((const char *)signal_pattern
                             + strlen(signal_pattern) + 1);
    if (direction == 'i')
        return ((sig->direction & DI_INCOMING)
                && strcmp(device_name, sig->device->name)==0
                && strstr(sig->name, signal_pattern));
    else if (direction == 'o')
        return ((sig->direction & DI_OUTGOING)
                && strcmp(device_name, sig->device->name)==0
                && strstr(sig->name, signal_pattern));
    else
        return (strcmp(device_name, sig->device->name)==0
                && strstr(sig->name, signal_pattern));
}

mapper_db_signal_t **mapper_db_match_inputs_by_device_name(
    mapper_db db, const char *device_name, const char *pattern)
{
    mapper_db_signal sig = db->registered_signals;
    if (!sig)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_match_signal_device_name,
        skip_slash(device_name), pattern, "i", 0);

    lh->self = sig;

    if (cmp_match_signal_device_name(&lh->query_context->data, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal_t **mapper_db_match_outputs_by_device_name(
    mapper_db db, const char *device_name, char const *pattern)
{
    mapper_db_signal sig = db->registered_signals;
    if (!sig)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_match_signal_device_name,
        skip_slash(device_name), pattern, "o", 0);

    lh->self = sig;

    if (cmp_match_signal_device_name(&lh->query_context->data, sig))
        return (mapper_db_signal*)&lh->self;

    return (mapper_db_signal*)dynamic_query_continuation(lh);
}

mapper_db_signal_t **mapper_db_match_signals_by_device_name(
    mapper_db db, const char *device_name, char const *pattern)
{
    mapper_db_signal sig = db->registered_signals;
    if (!sig)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_match_signal_device_name,
        skip_slash(device_name), pattern, "a", 0);

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

static void mapper_db_remove_signal(mapper_db db, mapper_db_signal sig)
{
    // remove any stored connection using this signal
    mapper_db_remove_connections_by_query(db,
        mapper_db_get_connections_by_device_and_signal_name(db, sig->device->name,
                                                            sig->name));

    fptr_list cb = db->signal_callbacks;
    while (cb) {
        mapper_db_signal_handler *h = cb->f;
        h(sig, MDB_REMOVE, cb->context);
        cb = cb->next;
    }

    if (sig->direction & DI_INCOMING)
        sig->device->num_inputs--;
    if (sig->direction & DI_OUTGOING)
        sig->device->num_outputs--;

    if (sig->path)
        free(sig->path);
    if (sig->description)
        free(sig->description);
    if (sig->unit)
        free(sig->unit);
    if (sig->minimum)
        free(sig->minimum);
    if (sig->maximum)
        free(sig->maximum);
    if (sig->extra)
        table_free(sig->extra, 1);

    list_remove_item(sig, (void**)&db->registered_signals);
}

void mapper_db_remove_signal_by_name(mapper_db db, const char *device_name,
                                     const char *signal_name)
{
    mapper_db_signal sig;
    sig = mapper_db_get_signal_by_device_and_signal_names(db, device_name,
                                                          signal_name);
    if (sig)
        mapper_db_remove_signal(db, sig);
}

void mapper_db_remove_signals_by_query(mapper_db db,
                                       mapper_db_signal_t **s)
{
    while (s) {
        mapper_db_signal sig = *s;
        s = mapper_db_signal_next(s);
        mapper_db_remove_signal(db, sig);
    }
}

/**** Connection records ****/

static uint32_t update_connection_hash(mapper_db_connection c)
{
    if (c->hash)
        return c->hash;
    c->hash = crc32(0L, (const Bytef *)c->destination.signal->device->name,
                    strlen(c->destination.signal->device->name));
    c->hash = crc32(0L, (const Bytef *)c->destination.signal->name,
                    strlen(c->destination.signal->name));
    int i;
    for (i = 0; i < c->num_sources; i++) {
        c->hash = crc32(c->hash, (const Bytef *)c->sources[i].signal->device->name,
                        strlen(c->sources[i].signal->device->name));
        c->hash = crc32(c->hash, (const Bytef *)c->sources[i].signal->name,
                        strlen(c->sources[i].signal->name));
    }
    return c->hash;
}

static int mapper_db_connection_add_scope(mapper_connection_scope scope,
                                          const char *name)
{
    int i;
    if (!scope || !name)
        return 1;

    // Check if scope is already stored for this connection
    uint32_t hash;
    if (strcmp(name, "all")==0)
        hash = 0;
    else
        hash = crc32(0L, (const Bytef *)name, strlen(name));
    for (i=0; i<scope->size; i++)
        if (scope->hashes[i] == hash)
            return 1;

    // not found - add a new scope
    i = ++scope->size;
    scope->names = realloc(scope->names, i * sizeof(char *));
    scope->names[i-1] = strdup(name);
    scope->hashes = realloc(scope->hashes, i * sizeof(uint32_t));
    scope->hashes[i-1] = hash;
    return 0;
}

static int mapper_db_connection_remove_scope(mapper_connection_scope scope,
                                             int index)
{
    int i;

    free(scope->names[index]);
    for (i = index+1; i < scope->size; i++) {
        scope->names[i-1] = scope->names[i];
        scope->hashes[i-1] = scope->hashes[i];
    }
    scope->size--;
    scope->names = realloc(scope->names, scope->size * sizeof(char *));
    scope->hashes = realloc(scope->hashes, scope->size * sizeof(uint32_t));
    return 0;
}

int mapper_db_connection_update_scope(mapper_connection_scope scope,
                                      lo_arg **scope_list, int num)
{
    int i, j, updated = 0;
    if (scope_list && *scope_list) {
        if (num == 1 && strcmp(&scope_list[0]->s, "none")==0)
            num = 0;

        // First remove old scopes that are missing
        for (i = 0; i < scope->size; i++) {
            int found = 0;
            for (j = 0; j < num; j++) {
                if (strcmp(scope->names[i], &scope_list[j]->s) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                mapper_db_connection_remove_scope(scope, i);
                updated++;
            }
        }
        // ...then add any new scopes
        for (i=0; i<num; i++)
            updated += (1 - mapper_db_connection_add_scope(scope, &scope_list[i]->s));
    }
    return updated;
}

static int compare_slot_names(const void *l, const void *r)
{
    int result = strcmp(((mapper_db_connection_slot)l)->signal->device->name,
                        ((mapper_db_connection_slot)r)->signal->device->name);
    if (result == 0)
        return strcmp(((mapper_db_connection_slot)l)->signal->name,
                      ((mapper_db_connection_slot)r)->signal->name);
    return result;
}

/*! Update information about a given connection record based on
 *  message parameters. */
static int update_connection_record_params(mapper_db db,
                                           mapper_db_connection con,
                                           const char *src_name,
                                           mapper_message_t *params)
{
    lo_arg **args;
    const char *types;
    int i, slot = -1, updated = 0, length, result;
    mapper_db_connection_slot s;

    if (!params)
        return updated;

    updated += update_int_if_arg(&con->id, params, AT_ID);

    if (con->num_sources == 1)
        slot = 0;
    else if (src_name) {
        // retrieve slot
        for (i = 0; i < con->num_sources; i++) {
            if (strcmp(con->sources[i].signal->name, src_name) == 0) {
                slot = i;
                break;
            }
        }
    }

    /* @slot */
    int slot_id;
    if (!mapper_msg_get_param_if_int(params, AT_SLOT, &slot_id)) {
        if (slot >= 0)
            con->sources[slot].slot_id = slot_id;
    }

    /* @srcType */
    args = mapper_msg_get_param(params, AT_SRC_TYPE, &types, &length);
    if (args && types && (types[0] == 'c' || types[0] == 's' || types[0] == 'S')) {
        char type;
        if (slot >= 0) {
            type = types[0] == 'c' ? (*args)->c : (&(*args)->s)[0];
            if (con->sources[slot].type != type) {
                con->sources[slot].type = type;
                updated++;
            }
        }
        else if (length == con->num_sources) {
            for (i = 0; i < length; i++) {
                type = types[0] == 'c' ? (args[i])->c : (&(args[i])->s)[0];
                if (con->sources[i].type != type) {
                    con->sources[i].type = type;
                    updated++;
                }
            }
        }
    }

    /* @destType */
    updated += update_char_if_arg(&con->destination.type, params, AT_DEST_TYPE);

    /* @srcLength */
    args = mapper_msg_get_param(params, AT_SRC_LENGTH, &types, &length);
    if (args && types && types[0] == 'i') {
        if (slot >= 0) {
            if (con->sources[slot].length != (*args)->i) {
                con->sources[slot].length = (*args)->i;
                updated++;
            }
        }
        else if (length == con->num_sources) {
            for (i = 0; i < length; i++) {
                if (con->sources[i].length != args[i]->i) {
                    con->sources[i].length = args[i]->i;
                    updated++;
                }
            }
        }
    }

    /* @destLength */
    updated += update_int_if_arg(&con->destination.length, params, AT_DEST_LENGTH);

    /* @srcMax */
    args = mapper_msg_get_param(params, AT_SRC_MAX, &types, &length);
    if (args && types) {
        if (slot >= 0) {
            s = &con->sources[slot];
            if (length == s->length) {
                if (!s->maximum)
                    s->maximum = calloc(1, length * mapper_type_size(s->type));
                for (i = 0; i < length; i++) {
                    result = propval_set_from_lo_arg(s->maximum, s->type,
                                                     args[i], types[i], i);
                }
            }
        }
        else if (con->num_sources == 1) {
            s = &con->sources[0];
            if (length == s->length) {
                if (!s->maximum)
                    s->maximum = calloc(1, length * mapper_type_size(s->type));
                for (i = 0; i < length; i++) {
                    result = propval_set_from_lo_arg(s->maximum, s->type,
                                                     args[i], types[i], i);
                }
            }
        }
        else {
            // TODO: passing multiple source ranges for convergent connections
        }
    }

    /* @srcMin */
    args = mapper_msg_get_param(params, AT_SRC_MIN, &types, &length);
    if (args && types) {
        if (slot >= 0) {
            s = &con->sources[slot];
            if (length == s->length) {
                if (!s->minimum)
                    s->minimum = calloc(1, length * mapper_type_size(s->type));
                for (i = 0; i < length; i++) {
                    result = propval_set_from_lo_arg(s->minimum, s->type,
                                                     args[i], types[i], i);
                }
            }
        }
        else if (con->num_sources == 1) {
            s = &con->sources[0];
            if (length == s->length) {
                if (!s->minimum)
                    s->minimum = calloc(1, length * mapper_type_size(s->type));
                for (i = 0; i < length; i++) {
                    result = propval_set_from_lo_arg(s->minimum, s->type,
                                                     args[i], types[i], i);
                }
            }
        }
        else {
            // TODO: passing multiple source ranges for convergent connections
        }
    }

    /* @destMax */
    args = mapper_msg_get_param(params, AT_DEST_MAX, &types, &length);
    if (args && types) {
        s = &con->destination;
        if (length == s->length) {
            if (!s->maximum)
                s->maximum = calloc(1, length * mapper_type_size(s->type));
            for (i=0; i<length; i++) {
                result = propval_set_from_lo_arg(s->maximum, s->type,
                                                 args[i], types[i], i);
                if (result == -1)
                    break;
                else
                    updated += result;
            }
        }
    }

    /* @destMin */
    args = mapper_msg_get_param(params, AT_DEST_MIN, &types, &length);
    if (args && types) {
        s = &con->destination;
        if (length == s->length) {
            if (!s->minimum)
                s->minimum = calloc(1, length * mapper_type_size(s->type));
            for (i=0; i<length; i++) {
                result = propval_set_from_lo_arg(s->minimum, s->type,
                                                 args[i], types[i], i);
                if (result == -1)
                    break;
                else
                    updated += result;
            }
        }
    }

    /* Range boundary actions */
    args = mapper_msg_get_param(params, AT_SRC_BOUND_MAX, &types, &length);
    if (args && types) {
        mapper_boundary_action bound_max;
        if (slot >= 0 && (types[0] == 's' || types[0] == 'S')) {
            bound_max = mapper_get_boundary_action_from_string(&args[0]->s);
            if (bound_max != BA_UNDEFINED
                && bound_max != con->sources[slot].bound_max) {
                con->sources[slot].bound_max = bound_max;
                updated++;
            }
        }
        else if (length == con->num_sources) {
            for (i = 0; i < con->num_sources; i++) {
                if (types[i] != 's' && types[i] != 'S')
                    continue;
                bound_max = mapper_get_boundary_action_from_string(&args[i]->s);
                if (bound_max != BA_UNDEFINED
                    && bound_max != con->sources[i].bound_max) {
                    con->sources[i].bound_max = bound_max;
                    updated++;
                }
            }
        }
    }
    args = mapper_msg_get_param(params, AT_SRC_BOUND_MIN, &types, &length);
    if (args && types) {
        mapper_boundary_action bound_min;
        if (slot >= 0 && (types[0] == 's' || types[0] == 'S')) {
            bound_min = mapper_get_boundary_action_from_string(&args[0]->s);
            if (bound_min != BA_UNDEFINED
                && bound_min != con->sources[slot].bound_min) {
                con->sources[slot].bound_min = bound_min;
                updated++;
            }
        }
        else if (length == con->num_sources) {
            for (i = 0; i < con->num_sources; i++) {
                if (types[i] != 's' && types[i] != 'S')
                    continue;
                bound_min = mapper_get_boundary_action_from_string(&args[i]->s);
                if (bound_min != BA_UNDEFINED
                    && bound_min != con->sources[i].bound_min) {
                    con->sources[i].bound_min = bound_min;
                    updated++;
                }
            }
        }
    }
    args = mapper_msg_get_param(params, AT_DEST_BOUND_MAX, &types, &length);
    if (args && types) {
        mapper_boundary_action bound_max;
        if (types[0] == 's' || types[0] == 'S') {
            bound_max = mapper_get_boundary_action_from_string(&args[0]->s);
            if (bound_max != BA_UNDEFINED
                && bound_max != con->destination.bound_max) {
                con->destination.bound_max = bound_max;
                updated++;
            }
        }
    }
    args = mapper_msg_get_param(params, AT_DEST_BOUND_MIN, &types, &length);
    if (args && types) {
        mapper_boundary_action bound_min;
        if (types[0] == 's' || types[0] == 'S') {
            bound_min = mapper_get_boundary_action_from_string(&args[0]->s);
            if (bound_min != BA_UNDEFINED
                && bound_min != con->destination.bound_min) {
                con->destination.bound_min = bound_min;
                updated++;
            }
        }
    }

    /* @causeUpdate */
    args = mapper_msg_get_param(params, AT_CAUSE_UPDATE, &types, &length);
    if (args && types && (length == con->num_sources)) {
        int cause_update;
        for (i = 0; i < con->num_sources; i++) {
            cause_update = (types[i] == 'T');
            if (con->sources[i].cause_update != cause_update) {
                con->sources[i].cause_update = cause_update;
                updated++;
            }
        }
    }

    /* @sendAsInstance */
    args = mapper_msg_get_param(params, AT_SEND_AS_INSTANCE, &types, &length);
    if (args && types && (length == con->num_sources)) {
        int send_as_instance;
        for (i = 0; i < con->num_sources; i++) {
            send_as_instance = (types[i] == 'T');
            if (con->sources[i].send_as_instance != send_as_instance) {
                con->sources[i].send_as_instance = send_as_instance;
                updated++;
            }
        }
    }

    /* Mode */
    args = mapper_msg_get_param(params, AT_MODE, &types, &length);
    if (args && types && (types[0] == 's' || types[0] == 'S')) {
        mapper_mode_type mode = mapper_get_mode_type_from_string(&args[0]->s);
        if (con->mode != mode) {
            con->mode = mode;
            updated++;
        }
    }

    /* Expression */
    args = mapper_msg_get_param(params, AT_EXPRESSION, &types, &length);
    if (args && types && (types[0] == 's' || types[0] == 'S')) {
        if (!con->expression || strcmp(con->expression, &args[0]->s)) {
            con->expression = realloc(con->expression, strlen(&args[0]->s)+1);
            strcpy(con->expression, &args[0]->s);
            updated++;
        }
    }

    /* Processing location */
    args = mapper_msg_get_param(params, AT_PROCESS, &types, &length);
    if (args && types && (types[0] == 's' || types[0] == 'S')) {
        int at_source = (strcmp(&args[0]->s, "source")==0);
        if (at_source && con->process_location != MAPPER_SOURCE) {
            con->process_location = MAPPER_SOURCE;
            updated++;
        }
        else if (!at_source && con->process_location != MAPPER_DESTINATION) {
            con->process_location = MAPPER_DESTINATION;
            updated++;
        }
    }

    int mute = mapper_msg_get_mute(params);
    if (mute != -1 && mute != con->muted) {
        con->muted = mute;
        updated++;
    }

    lo_arg **a_scopes = mapper_msg_get_param(params, AT_SCOPE, &types, &length);
    if (types && (types[0] == 's' || types[0] == 'S'))
        mapper_db_connection_update_scope(&con->scope, a_scopes, length);

    updated += mapper_msg_add_or_update_extra_params(con->extra, params);
    return updated;
}

mapper_db_connection mapper_db_add_or_update_connection_params(mapper_db db,
                                                               int num_sources,
                                                               const char **src_names,
                                                               const char *dest_name,
                                                               mapper_message_t *params)
{
    if (num_sources >= MAX_NUM_CONNECTION_SOURCES) {
        trace("error: maximum connection sources exceeded.\n");
        return 0;
    }

    mapper_db_connection con;
    int rc = 0, updated = 0, devnamelen, i, j;
    char *devnamep, *signame, devname[256];

    /* connection could be part of larger "convergent" connection, so we will
     * retrieve record by connection id instead of names. */
    int id;
    if (!mapper_msg_get_param_if_int(params, AT_ID, &id)) {
        con = mapper_db_get_connection_by_dest_device_and_id(db, dest_name, id);
    }
    else {
        con = mapper_db_get_connection_by_signal_full_names(db, num_sources,
                                                            src_names, dest_name);
    }

    if (!con) {
        con = (mapper_db_connection)
            list_new_item(sizeof(mapper_db_connection_t));
        con->num_sources = num_sources;
        con->sources = ((mapper_db_connection_slot)
                        calloc(1, sizeof(struct _mapper_db_connection_slot)
                               * num_sources));
        for (i = 0; i < num_sources; i++) {
            devnamelen = mapper_parse_names(src_names[i], &devnamep, &signame);
            if (!devnamelen || devnamelen >= 256) {
                trace("error extracting device name\n");
                // clean up partially-built record
                list_remove_item(con, (void**)&db->registered_connections);
                return 0;
            }
            strncpy(devname, devnamep, devnamelen);
            devname[devnamelen] = 0;

            // also add source signal if necessary
            con->sources[i].signal =
                mapper_db_add_or_update_signal_params(db, signame, devname, 0);
            con->sources[i].slot_id = i;
            con->sources[i].cause_update = 1;
            con->sources[i].minimum = con->sources[i].maximum = 0;
        }
        devnamelen = mapper_parse_names(dest_name, &devnamep, &signame);
        if (!devnamelen || devnamelen >= 256) {
            trace("error extracting device name\n");
            // clean up partially-built record
            list_remove_item(con, (void**)&db->registered_connections);
            return 0;
        }
        strncpy(devname, devnamep, devnamelen);
        devname[devnamelen] = 0;
        con->destination.minimum = con->destination.maximum = 0;

        // also add destination signal if necessary
        con->destination.signal =
            mapper_db_add_or_update_signal_params(db, signame, devname, 0);
        con->destination.cause_update = 1;

        con->extra = table_new();
        rc = 1;
        update_connection_hash(con);
    }
    else if (con->num_sources < num_sources) {
        // add one or more sources
        for (i = 0; i < num_sources; i++) {
            devnamelen = mapper_parse_names(src_names[i], &devnamep, &signame);
            if (!devnamelen || devnamelen >= 256) {
                trace("error extracting device name\n");
                return 0;
            }
            strncpy(devname, devnamep, devnamelen);
            devname[devnamelen] = 0;
            for (j = 0; j < con->num_sources; j++) {
                if (strlen(con->sources[i].signal->device->name) == devnamelen
                    && strcmp(devname, con->sources[j].signal->device->name)==0
                    && strcmp(signame, con->sources[j].signal->name)==0) {
                    con->sources[j].slot_id = i;
                    break;
                }
            }
            if (j == con->num_sources) {
                con->num_sources++;
                con->sources = realloc(con->sources,
                                       sizeof(struct _mapper_db_connection_slot)
                                       * con->num_sources);
                con->sources[j].signal =
                    mapper_db_add_or_update_signal_params(db, signame, devname, 0);
                con->sources[j].slot_id = i;
                con->sources[j].cause_update = 1;
                con->sources[j].minimum = con->sources[j].maximum = 0;
            }
        }
        // slots should be in alphabetical order
        qsort(con->sources, con->num_sources,
              sizeof(mapper_db_connection_slot_t), compare_slot_names);
        update_connection_hash(con);
    }

    if (con) {
        updated = update_connection_record_params(db, con, num_sources > 1
                                                  ? 0 : src_names[0], params);

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

    return con;
}

int mapper_db_connection_property_index(mapper_db_connection con,
                                        unsigned int index,
                                        const char **property, char *type,
                                        const void **value, int *length)
{
    return mapper_db_property_index(con, con->extra, index, property, type,
                                    value, length, &con_table);
}

int mapper_db_connection_property_lookup(mapper_db_connection con,
                                         const char *property, char *type,
                                         const void **value, int *length)
{
    return mapper_db_property_lookup(con, con->extra, property, type,
                                     value, length, &con_table);
}

int mapper_db_connection_slot_property_index(mapper_db_connection_slot slot,
                                             unsigned int index,
                                             const char **property, char *type,
                                             const void **value, int *length)
{
    return mapper_db_property_index(slot, 0, index, property, type,
                                    value, length, &slot_table);
}

int mapper_db_connection_slot_property_lookup(mapper_db_connection_slot slot,
                                              const char *property, char *type,
                                              const void **value, int *length)
{
    return mapper_db_property_lookup(slot, 0, property, type,
                                     value, length, &slot_table);
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

mapper_db_connection mapper_db_get_connection_by_hash(mapper_db db,
                                                      uint32_t hash)
{
    mapper_db_connection con = db->registered_connections;
    if (!con)
        return 0;
    while (con) {
        if (con->hash == hash)
            return con;
        con = list_get_next(con);
    }
    return 0;
}

mapper_db_connection mapper_db_get_connection_by_dest_device_and_id(
    mapper_db db, const char *name, int id)
{
    mapper_db_connection con = db->registered_connections;
    if (!con)
        return 0;

    char *devname;
    int devnamelen = mapper_parse_names(name, &devname, 0);

    while (con) {
        if (con->id == id
            && strlen(con->destination.signal->device->name) == devnamelen
            && strncmp(con->destination.signal->device->name,
                       devname, devnamelen)==0)
            return con;
        con = list_get_next(con);
    }
    return 0;
}

static int cmp_query_get_connections_by_device_name(void *context_data,
                                                    mapper_db_connection con)
{
    const char *devname = (const char*)context_data;
    unsigned int len = strlen(devname);
    int i;
    for (i = 0; i < con->num_sources; i++) {
        if (strlen(con->sources[i].signal->device->name) == len
            && strncmp(con->sources[i].signal->device->name, devname, len)==0) {
            return 1;
        }
    }

    if (strlen(con->destination.signal->device->name) == len
        && strncmp(con->destination.signal->device->name, devname, len)==0) {
        return 1;
    }
    return 0;
}

mapper_db_connection_t **mapper_db_get_connections_by_device_name(
    mapper_db db, const char *device_name)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_device_name,
         skip_slash(device_name), 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_device_name(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

static int cmp_query_get_connections_by_src_dest_device_names(
    void *context_data, mapper_db_connection con)
{
    int i, offset;
    const char *num_sources_str = (const char*)context_data;
    offset = strlen(num_sources_str) + 1;

    unsigned int num_sources = atoi(num_sources_str);
    if (con->num_sources != num_sources)
        return 0;

    const char *devname;
    unsigned int len;
    for (i = 0; i < num_sources; i++) {
        devname = (const char *)context_data + offset;
        len = strlen(devname);
        if (strlen(con->sources[i].signal->device->name) != len
            || strncmp(con->sources[i].signal->device->name, devname, len))
            return 0;
        offset += len + 1;
    }
    devname = (const char *)context_data + offset;
    len = strlen(devname);
    return (strlen(con->destination.signal->device->name) == len
            && strncmp(con->destination.signal->device->name, devname, len)==0);
}

mapper_db_connection_t **mapper_db_get_connections_by_src_dest_device_names(
    mapper_db db, int num_sources, const char **src_device_names,
    const char *dest_device_name)
{
    int i, srcdevnamelen = 0;
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    char num_sources_str[16];
    snprintf(num_sources_str, 16, "%i", num_sources);
    for (i = 0; i < num_sources; i++)
        srcdevnamelen += strlen(skip_slash(src_device_names[i])) + 1;
    char combined_src_names[srcdevnamelen], *t = combined_src_names;
    for (i = 0; i < num_sources; i++) {
        const char *s = skip_slash(src_device_names[i]);
        while (*s) { *t++ = *s++; }
        *t++ = 0;
    }

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_src_dest_device_names,
        num_sources_str, combined_src_names, skip_slash(dest_device_name), 0);

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
    int i;
    for (i = 0; i < con->num_sources; i++) {
        if (strcmp(con->sources[i].signal->name, src_name)==0)
            return 1;
    }
    return 0;
}

mapper_db_connection_t **mapper_db_get_connections_by_src_signal_name(
    mapper_db db, const char *src_signal)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_src_signal_name,
        skip_slash(src_signal), 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_src_signal_name(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

static int cmp_query_get_connections_by_src_device_and_signal_names(
    void *context_data, mapper_db_connection con)
{
    const char *fullname = (const char*) context_data;
    char *devname, *signame;
    int i, len = mapper_parse_names(fullname, &devname, &signame);

    for (i = 0; i < con->num_sources; i++) {
        if (strlen(con->sources[i].signal->device->name) == len
            && strncmp(con->sources[i].signal->device->name, devname, len)==0
            && strcmp(con->sources[i].signal->name, signame)==0)
            return 1;
    }
    return 0;
}

mapper_db_connection_t **mapper_db_get_connections_by_src_device_and_signal_names(
    mapper_db db, const char *src_device, const char *src_signal)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    char name[1024];
    snprintf(name, 1024, "%s/%s", skip_slash(src_device), skip_slash(src_signal));

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
    return strcmp(con->destination.signal->name, dest_name)==0;
}

mapper_db_connection_t **mapper_db_get_connections_by_dest_signal_name(
    mapper_db db, const char *dest_name)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_dest_signal_name,
        skip_slash(dest_name), 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_dest_signal_name(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

static int cmp_query_get_connections_by_dest_device_and_signal_names(
    void *context_data, mapper_db_connection con)
{
    const char *fullname = (const char*) context_data;
    char *devname, *signame;
    int len = mapper_parse_names(fullname, &devname, &signame);
    return (strlen(con->destination.signal->device->name) == len
            && strncmp(con->destination.signal->device->name, devname, len)==0
            && strcmp(con->destination.signal->name, signame)==0);
}

mapper_db_connection_t **mapper_db_get_connections_by_dest_device_and_signal_names(
    mapper_db db, const char *dest_device, const char *dest_signal)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    char name[1024];
    snprintf(name, 1024, "%s/%s", skip_slash(dest_device), skip_slash(dest_signal));
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
    mapper_db db, int num_sources, const char **src_names, const char *dest_name)
{
    int i, j;
    mapper_db_connection con = db->registered_connections;
    if (!con)
        return 0;

    char *devname, *signame;
    int devnamelen;
    while (con) {
        if (con->num_sources == num_sources) {
            int matched = 0;
            for (i=0; i<num_sources; i++) {
                devnamelen = mapper_parse_names(src_names[i], &devname, &signame);
                for (j=0; j<num_sources; j++) {
                    if (strlen(con->sources[j].signal->device->name) == devnamelen
                        && strncmp(con->sources[j].signal->device->name,
                                   devname, devnamelen)==0
                        && strcmp(con->sources[j].signal->name, signame)==0) {
                        matched++;
                        break;
                    }
                }
            }
            if (matched == num_sources) {
                devnamelen = mapper_parse_names(dest_name, &devname, &signame);
                if (strlen(con->destination.signal->device->name) == devnamelen
                    && strncmp(con->destination.signal->device->name,
                               devname, devnamelen)==0
                    && strcmp(con->destination.signal->name, signame)==0)
                    return con;
            }
        }
        con = list_get_next(con);
    }
    return 0;
}

static int cmp_query_get_connections_by_device_and_signal_name(
    void *context_data, mapper_db_connection con)
{
    const char *fullname = (const char*) context_data;
    char *devname, *signame;
    int i, len = mapper_parse_names(fullname, &devname, &signame);
    for (i = 0; i < con->num_sources; i++) {
        if (strlen(con->sources[i].signal->device->name) == len
            && strncmp(con->sources[i].signal->device->name, devname, len)==0
            && strcmp(con->sources[0].signal->name, signame)==0)
            return 1;
    }
    return (strlen(con->destination.signal->device->name) == len
            && strncmp(con->destination.signal->device->name, devname, len)==0
            && strcmp(con->destination.signal->name, signame)==0);
}

mapper_db_connection_t **mapper_db_get_connections_by_device_and_signal_name(
    mapper_db db, const char *device_name,  const char *signal_name)
{
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    char fullname[1024];
    snprintf(fullname, 1024, "%s/%s", skip_slash(device_name),
             skip_slash(signal_name));
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_device_and_signal_name,
        fullname, 0);

    lh->self = connection;

    if (cmp_query_get_connections_by_device_and_signal_name(
            &lh->query_context->data, connection))
        return (mapper_db_connection*)&lh->self;

    return (mapper_db_connection*)dynamic_query_continuation(lh);
}

static int cmp_query_get_connections_by_device_and_signal_names(
    void *context_data, mapper_db_connection con)
{
    int i, len, offset;
    const char *num_sources_str = (const char*)context_data;
    offset = strlen(num_sources_str) + 1;

    unsigned int num_sources = atoi(num_sources_str);
    if (con->num_sources != num_sources)
        return 0;

    const char *fullname;
    char *devname, *signame;
    for (i = 0; i < num_sources; i++) {
        fullname = (const char *)context_data + offset;
        // slash already skipped
        len = mapper_parse_names(fullname, &devname, &signame);
        if (strlen(con->sources[i].signal->device->name) != len
            || strncmp(con->sources[i].signal->device->name, devname, len)
            || strcmp(con->sources[i].signal->name, signame))
            return 0;
        offset += strlen(fullname) + 1;
    }
    fullname = (const char *)context_data + offset;
    // slash already skipped
    len = mapper_parse_names(fullname, &devname, &signame);
    return (strlen(con->destination.signal->device->name) == len
            && strncmp(con->destination.signal->device->name, devname, len)==0
            && strcmp(con->destination.signal->name, signame)==0);
}

mapper_db_connection_t **mapper_db_get_connections_by_device_and_signal_names(
    mapper_db db, int num_sources,
    const char **src_devices,  const char **src_signals,
    const char *dest_device, const char *dest_signal)
{
    int i, combined_src_names_len = 0;
    mapper_db_connection connection = db->registered_connections;
    if (!connection)
        return 0;

    char num_sources_str[16];
    snprintf(num_sources_str, 16, "%i", num_sources);
    for (i = 0; i < num_sources; i++) {
        combined_src_names_len += strlen(src_devices[i]);
        combined_src_names_len += strlen(src_signals[i])+1;
    }
    char combined_src_names[combined_src_names_len], *t = combined_src_names;
    for (i = 0; i < num_sources; i++) {
        const char *s = skip_slash(src_devices[i]);
        while (*s) { *t++ = *s++; }
        *t++ = '/';
        s = skip_slash(src_signals[i]);
        while (*s) { *t++ = *s++; }
        *t++ = 0;
    }

    char dest_name[1024];
    snprintf(dest_name, 1024, "%s/%s", skip_slash(dest_device),
             skip_slash(dest_signal));

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_connections_by_device_and_signal_names,
        num_sources_str, combined_src_names, dest_name, 0);

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
    unsigned int len = strlen((*srcsig)->device->name);
    while (srcsig && *srcsig) {
        int i, found = 0;
        for (i = 0; i < con->num_sources; i++) {
            if (strlen(con->sources[i].signal->device->name) == len
                && strncmp(con->sources[i].signal->device->name,
                           (*srcsig)->device->name, len)==0
                && strcmp((*srcsig)->name, con->sources[i].signal->name)==0) {
                found = 1;
                break;
            }
        }
        if (found)
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
    len = strlen((*destsig)->device->name);
    while (destsig && *destsig) {
        if (strlen(con->destination.signal->device->name) == len
            && strncmp(con->destination.signal->device->name,
                       (*destsig)->device->name, len)==0
            && strcmp((*destsig)->name, con->destination.signal->name)==0)
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
    mapper_db db, mapper_db_signal_t **src, mapper_db_signal_t **dest)
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

static void free_slot(mapper_db_connection_slot s)
{
    if (s->minimum)
        free(s->minimum);
    if (s->maximum)
        free(s->maximum);
}

void mapper_db_remove_connection(mapper_db db, mapper_db_connection con)
{
    int i;
    if (!con)
        return;

    fptr_list cb = db->connection_callbacks;
    while (cb) {
        mapper_db_connection_handler *h = cb->f;
        h(con, MDB_REMOVE, cb->context);
        cb = cb->next;
    }

    if (con->sources) {
        for (i = 0; i < con->num_sources; i++) {
            free_slot(&con->sources[i]);
        }
        free(con->sources);
    }
    free_slot(&con->destination);
    if (con->scope.size && con->scope.names) {
        for (i=0; i<con->scope.size; i++)
            free(con->scope.names[i]);
        free(con->scope.names);
        free(con->scope.hashes);
    }
    if (con->expression)
        free(con->expression);
    if (con->extra)
        table_free(con->extra, 1);
    list_remove_item(con, (void**)&db->registered_connections);
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
    while ((cb = db->connection_callbacks)) {
        db->connection_callbacks = db->connection_callbacks->next;
        free(cb);
    }
}
