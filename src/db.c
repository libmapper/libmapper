#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <zlib.h>

#include "mapper_internal.h"

/* Some useful local list functions. */

/*
 *   Note on the trick used here: Presuming that we can have lists as the result
 * of a search query, we need to be able to return a linked list composed of
 * pointers to arbitrary database items.  However a very common operation will
 * be to walk through all the entries.  We prepend a header containing a pointer
 * to the next item and a pointer to the current item, and return the address of
 * the current pointer.
 *   In the normal case, the address of the self-pointer can therefore be used
 * to locate the actual database entry, and we can walk along the actual list
 * without needing to allocate any memory for a list header.  However, in the
 * case where we want to walk along the result of a query, we can allocate a
 * dynamic set of list headers, and still have 'self' point to the actual
 * database item.
 *   Both types of queries can use the returned double-pointer as context for
 * the search as well as for returning the desired value.  This allows us to
 * avoid requiring the user to manage a separate iterator object.
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

typedef struct {
    int num_queries;
    list_header_t *lh1;
    list_header_t *lh2;
    mapper_db_op op;
} multi_query_t;

inline static const char *skip_slash(const char *string)
{
    return string + (string && string[0]=='/');
}

#define LIST_HEADER_SIZE (sizeof(list_header_t)-sizeof(int[1]))

/*! Reserve memory for a list item.  Reserves an extra pointer at the
 *  beginning of the structure to allow for a list pointer. */
static list_header_t* list_alloc_item(size_t size)
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

/*! Get the list header for memory returned from list_add_new_item(). */
static list_header_t* list_get_header_by_data(void *data)
{
    return (list_header_t*)(data - LIST_HEADER_SIZE);
}

/*! Get the list header for memory returned from list_add_new_item(). */
static list_header_t* list_get_header_by_self(void *self)
{
    list_header_t *lh=0;
    return (list_header_t*)(self - ((void*)&lh->self - (void*)lh));
}

static void *list_from_data(void *data)
{
    if (!data)
        return 0;
    list_header_t* lh = list_get_header_by_data(data);
    die_unless(lh->self == &lh->data, "bad self pointer in list structure");
    return &lh->self;
}

/*! Set the next pointer in memory returned from list_add_new_item(). */
static void list_set_next(void *mem, void *next)
{
    list_get_header_by_data(mem)->next = next;
}

/*! Get the next pointer in memory returned from list_add_new_item(). */
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

static list_header_t* list_add_new_item(void **list, size_t size)
{
    list_header_t* lh = list_alloc_item(size);
    list_prepend_item(lh, list);
    return lh;
}

/*! Remove an item from a list but do not free its memory. */
static void list_remove_item(void *item, void **head)
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
}

/*! Free the memory used by a list item */
static void list_free_item(void *item)
{
    if (item)
        free(list_get_header_by_data(item));
}

/** Structures and functions for performing dynamic queries **/

/* Here are some generalized routines for dealing with typical context format
 * and query continuation. Functions specific to particular queries are defined
 * further down with their compare operation. */

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

static void list_done(void **data)
{
    if (!data)
        return;
    list_header_t *lh = list_get_header_by_data(*data);
    if (lh->query_type == QUERY_DYNAMIC && lh->query_context->query_free)
        lh->query_context->query_free(lh);
}

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

/* We need to be careful of memory alignment here - for now we will just ensure
 * that string arguments are always passed last. */
static void **construct_query_context(void *p, void *f, const char *types, ...)
{
    if (!p || !f || !types)
        return 0;
    list_header_t *lh = (list_header_t*)malloc(LIST_HEADER_SIZE);
    lh->next = dynamic_query_continuation;
    lh->query_type = QUERY_DYNAMIC;

    va_list aq;
    va_start(aq, types);

    int i = 0, j, size = 0, num_args;
    const char *s;
    while (types[i]) {
        switch (types[i]) {
            case 'i':
            case 'c': // store char as int to avoid alignment problems
                if (types[i+1] && isdigit(types[i+1])) {
                    num_args = atoi(types+i+1);
                    va_arg(aq, int*);
                    i++;
                }
                else {
                    num_args = 1;
                    va_arg(aq, int);
                }
                size += num_args * sizeof(int);
                break;
            case 'h':
                if (types[i+1] && isdigit(types[i+1])) {
                    num_args = atoi(types+i+1);
                    va_arg(aq, int64_t*);
                    i++;
                }
                else {
                    num_args = 1;
                    va_arg(aq, int64_t);
                }
                size += num_args * sizeof(int64_t);
                break;
            case 's':
                if (types[i+1] && isdigit(types[i+1])) {
                    num_args = atoi(types+i+1);
                    const char **val = va_arg(aq, const char**);
                    for (j = 0; j < num_args; j++)
                        size += strlen(val[j]) + 1;
                    i++;
                }
                else {
                    const char *val = va_arg(aq, const char*);
                    size += strlen(val) + 1;
                }
                break;
            case 'v':
                // void ptr
                if (types[i+1] && isdigit(types[i+1])) {
                    num_args = atoi(types+i+1);
                    va_arg(aq, void**);
                    i++;
                }
                else {
                    num_args = 1;
                    va_arg(aq, void**);
                }
                size += num_args * sizeof(void**);
                break;
            default:
                va_end(aq);
                free(lh);
                return 0;
        }
        i++;
    };
    va_end(aq);

    lh->query_context = (query_info_t*)malloc(sizeof(query_info_t)+size);

    char *d = (char*)&lh->query_context->data;
    int offset = 0;
    va_start(aq, types);
    i = 0;
    while (types[i]) {
        switch (types[i]) {
            case 'i':
            case 'c': // store char as int to avoid alignment problems
                if (types[i+1] && isdigit(types[i+1])) {
                    // is array
                    num_args = atoi(types+i+1);
                    int *val = (int*)va_arg(aq, int*);
                    memcpy(d+offset, val, sizeof(int) * num_args);
                    i++;
                }
                else {
                    num_args = 1;
                    int val = (int)va_arg(aq, int);
                    memcpy(d+offset, &val, sizeof(int));
                }
                offset += sizeof(int) * num_args;
                break;
            case 'h': {
                if (types[i+1] && isdigit(types[i+1])) {
                    // is array
                    num_args = atoi(types+i+1);
                    int64_t *val = (int64_t*)va_arg(aq, int64_t*);
                    memcpy(d+offset, val, sizeof(int64_t) * num_args);
                    i++;
                }
                else {
                    num_args = 1;
                    int64_t val = (int64_t)va_arg(aq, int64_t);
                    memcpy(d+offset, &val, sizeof(int64_t));
                }
                offset += sizeof(int64_t) * num_args;
                break;
            }
            case 's':
                if (types[i+1] && isdigit(types[i+1])) {
                    // is array
                    num_args = atoi(types+i+1);
                    const char **val = (const char**)va_arg(aq, const char**);
                    for (j = 0; j < num_args; j++) {
                        snprintf(d+offset, size-offset, "%s", val[j]);
                    }
                    offset += strlen(val[j]) + 1;
                    i++;
                }
                else {
                    const char *val = (const char*)va_arg(aq, const char*);
                    snprintf(d+offset, size-offset, "%s", val);
                    offset += strlen(val) + 1;
                }
                break;
            case 'v': {
                if (types[i+1] && isdigit(types[i+1])) {
                    // is array
                    num_args = atoi(types+i+1);
                    i++;
                }
                else {
                    num_args = 1;
                }
                void *val = va_arg(aq, void**);
                memcpy(d+offset, val, sizeof(void*) * num_args);
                offset += sizeof(void**) * num_args;
                break;
            }
            default:
                va_end(aq);
                free(lh->query_context);
                free(lh);
                return 0;
        }
        i++;
    }
    s = va_arg(aq, const char*);
    va_end(aq);

    lh->query_context->size = sizeof(query_info_t)+size;
    lh->query_context->query_compare = (query_compare_func_t*)f;
    lh->query_context->query_free = (query_free_func_t*)free_query_single_context;

    lh->self = p;

    // try evaluating the first item
    if (lh->query_context->query_compare(&lh->query_context->data, p))
        return &lh->self;
    return dynamic_query_continuation(lh);
}

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

    if (lh1->query_type == QUERY_STATIC) {
        return list_from_data(lh1->next);
    }
    else if (lh1->query_type == QUERY_DYNAMIC) {
        /* Here we treat next as a pointer to a continuation function, so we can
         * return items from the database computed lazily.  The context is
         * simply the string(s) to match.  In the future, it might point to the
         * results of a SQL query for example. */
        void **(*f) (list_header_t*) = lh1->next;
        return f(lh1);
    }
    return 0;
}

/* Functions for handling compound queries: unions, intersections, etc. */
static int cmp_compound_query(void *context_data, void *dev)
{
    list_header_t *lh1 = *(list_header_t**)context_data;
    list_header_t *lh2 = *(list_header_t**)(context_data + sizeof(void*));
    mapper_db_op op = *(mapper_db_op*)(context_data + sizeof(void*) * 2);

    query_info_t *c1 = lh1->query_context, *c2 = lh2->query_context;

    switch (op) {
        case OP_UNION:
            return (    c1->query_compare(&c1->data, dev)
                    ||  c2->query_compare(&c2->data, dev));
        case OP_INTERSECTION:
            return (    c1->query_compare(&c1->data, dev)
                    &&  c2->query_compare(&c2->data, dev));
        case OP_DIFFERENCE:
            return (    c1->query_compare(&c1->data, dev)
                    && !c2->query_compare(&c2->data, dev));
        default:
            return 0;
    }
}

static void **mapper_db_query_union(void *resource, void **query1,
                                    void **query2)
{
    if (!resource)
        return 0;
    if (!query1)
        return query2;
    if (!query2)
        return query1;

    list_header_t *lh1 = list_get_header_by_self(query1);
    list_header_t *lh2 = list_get_header_by_self(query2);
    return (construct_query_context(resource, cmp_compound_query,
                                    "vvi", &lh1, &lh2, OP_UNION));
}

static void **mapper_db_query_intersection(void *resource, void **query1,
                                           void **query2)
{
    if (!resource)
        return 0;
    if (!query1 || !query2)
        return 0;

    list_header_t *lh1 = list_get_header_by_self(query1);
    list_header_t *lh2 = list_get_header_by_self(query2);
    return (construct_query_context(resource, cmp_compound_query, "vvi",
                                    &lh1, &lh2, OP_INTERSECTION));
}

static void **mapper_db_query_difference(void *resource, void **query1,
                                         void **query2)
{
    if (!resource)
        return 0;
    if (!query1)
        return 0;
    if (!query2)
        return query1;

    list_header_t *lh1 = list_get_header_by_self(query1);
    list_header_t *lh2 = list_get_header_by_self(query2);
    return (construct_query_context(resource, cmp_compound_query, "vvi",
                                    &lh1, &lh2, OP_DIFFERENCE));
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

static int update_int64_if_arg(int64_t *pdest_int64,
                               mapper_message_t *params,
                               mapper_msg_param_t field)
{
    const char *type = 0;
    lo_arg **a = mapper_msg_get_param(params, field, &type, 0);

    if (a && (*a) && (type[0]=='h') && (*pdest_int64 != (&(*a)->i64)[0])) {
        (*pdest_int64) = (&(*a)->i64)[0];
        return 1;
    }
    return 0;
}

/* Static data for property tables embedded in the db data structures.
 *
 * Signals and devices have properties that can be indexed by name, and can have
 * extra properties attached to them if these appear in the OSC message
 * describing the resource.  The utility of this is to allow for attaching of
 * arbitrary metadata to objects on the network.  For example, signals can have
 * 'x' and 'y' values to indicate their physical position in the room.
 *
 * It is also useful to be able to look up standard properties like vector
 * length, name, or unit by specifying these as a string.
 *
 * The following data provides a string table (as usable by the implementation
 * in table.c) for indexing the static data existing in the mapper_db_* data
 * structures.  Some of these static properties may not actually exist, such as
 * 'minimum' and 'maximum' which are optional properties of signals.  Therefore
 * an 'indirect' form is available where the table points to a pointer to the
 * value, which may be null.
 *
 * A property lookup consists of looking through the 'extra' properties of a
 * structure.  If the requested property is not found, then the 'static'
 * properties are searched---in the worst case, an unsuccessful lookup may
 * therefore take twice as long.
 *
 * To iterate through all available properties, the caller must request by
 * index, starting at 0, and incrementing until failure.  They are not
 * guaranteed to be in a particular order.
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
#define MAP_OFFSET(x) offsetof(mapper_db_map_t, x)
#define SLOT_OFFSET(x) offsetof(mapper_db_map_slot_t, x)

#define SIG_TYPE        (SIG_OFFSET(type))
#define SIG_LENGTH      (SIG_OFFSET(length))

#define SLOT_TYPE       (SLOT_OFFSET(type))
#define SLOT_LENGTH     (SLOT_OFFSET(length))
#define NUM_SCOPES      (MAP_OFFSET(scope.size))
#define NUM_SOURCES     (MAP_OFFSET(num_sources))

#define LOCAL_TYPE      (MAP_OFFSET(local_type))
#define REMOTE_TYPE     (MAP_OFFSET(remote_type))
#define LOCAL_LENGTH    (MAP_OFFSET(local_length))
#define REMOTE_LENGTH   (MAP_OFFSET(remote_length))
#define NUM_SCOPES      (MAP_OFFSET(scope.size))

/* Here type 'o', which is not an OSC type, was reserved to mean "same type as
 * the signal's type".  The lookup and index functions will return the sig->type
 * instead of the value's type. */
static property_table_value_t sig_values[] = {
    { 's', {1},        -1,         SIG_OFFSET(description) },
    { 'i', {0},        -1,         SIG_OFFSET(direction) },
    { 'h', {0},        -1,         SIG_OFFSET(id) },
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
    { "id",          &sig_values[2] },
    { "length",      &sig_values[3] },
    { "max",         &sig_values[4] },
    { "min",         &sig_values[5] },
    { "name",        &sig_values[6] },
    { "rate",        &sig_values[7] },
    { "type",        &sig_values[8] },
    { "unit",        &sig_values[9] },
    { "user_data",   &sig_values[10] },
};

const int N_SIG_STRINGS = sizeof(sig_strings)/sizeof(sig_strings[0]);
static mapper_string_table_t sig_table =
    { sig_strings, N_SIG_STRINGS, N_SIG_STRINGS };

static property_table_value_t dev_values[] = {
    { 's', {1}, -1, DEV_OFFSET(description) },
    { 's', {1}, -1, DEV_OFFSET(host) },
    { 'h', {0}, -1, DEV_OFFSET(id) },
    { 's', {1}, -1, DEV_OFFSET(lib_version) },
    { 's', {1}, -1, DEV_OFFSET(name) },
    { 'i', {0}, -1, DEV_OFFSET(num_incoming_maps) },
    { 'i', {0}, -1, DEV_OFFSET(num_outgoing_maps) },
    { 'i', {0}, -1, DEV_OFFSET(num_inputs) },
    { 'i', {0}, -1, DEV_OFFSET(num_outputs) },
    { 'i', {0}, -1, DEV_OFFSET(port) },
    { 't', {0}, -1, DEV_OFFSET(synced) },
    { 'i', {0},  0, DEV_OFFSET(user_data) },
    { 'i', {0}, -1, DEV_OFFSET(version) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t dev_strings[] = {
    { "description",        &dev_values[0] },
    { "host",               &dev_values[1] },
    { "id",                 &dev_values[2] },
    { "lib_version",        &dev_values[3] },
    { "name",               &dev_values[4] },
    { "num_incoming_maps",  &dev_values[5] },
    { "num_inputs",         &dev_values[6] },
    { "num_outgoing_maps",  &dev_values[7] },
    { "num_outputs",        &dev_values[8] },
    { "port",               &dev_values[9] },
    { "synced",             &dev_values[10] },
    { "user_data",          &dev_values[11] },
    { "version",            &dev_values[12] },
};

const int N_DEV_STRINGS = sizeof(dev_strings)/sizeof(dev_strings[0]);
static mapper_string_table_t dev_table =
    { dev_strings, N_DEV_STRINGS, N_DEV_STRINGS };

static property_table_value_t slot_values[] = {
    { 'i', {0},         -1,          SLOT_OFFSET(bound_max) },
    { 'i', {0},         -1,          SLOT_OFFSET(bound_min) },
    { 'i', {0},         -1,          SLOT_OFFSET(calibrating) },
    { 'i', {0},         -1,          SLOT_OFFSET(cause_update) },
    { 'i', {0},         -1,          SLOT_OFFSET(direction) },
    { 'i', {0},         -1,          SLOT_OFFSET(length) },
    { 'o', {SLOT_TYPE}, SLOT_LENGTH, SLOT_OFFSET(maximum) },
    { 'o', {SLOT_TYPE}, SLOT_LENGTH, SLOT_OFFSET(minimum) },
    { 'i', {0},         -1,          SLOT_OFFSET(send_as_instance) },
    { 'c', {0},         -1,          SLOT_OFFSET(type) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t slot_strings[] = {
    { "bound_max",        &slot_values[0] },
    { "bound_min",        &slot_values[1] },
    { "calibrating",      &slot_values[2] },
    { "cause_update",     &slot_values[3] },
    { "direction",        &slot_values[4] },
    { "length",           &slot_values[5] },
    { "maximum",          &slot_values[6] },
    { "minimum",          &slot_values[7] },
    { "send_as_instance", &slot_values[8] },
    { "type",             &slot_values[9] },
};

const int N_SLOT_STRINGS = sizeof(slot_strings)/sizeof(slot_strings[0]);
static mapper_string_table_t slot_table =
    { slot_strings, N_SLOT_STRINGS, N_SLOT_STRINGS };

static property_table_value_t map_values[] = {
    { 's', {1}, -1,         MAP_OFFSET(expression) },
    { 'h', {0}, -1,         MAP_OFFSET(id)},
    { 'i', {0}, -1,         MAP_OFFSET(mode) },
    { 'i', {0}, -1,         MAP_OFFSET(muted) },
    { 'i', {0}, -1,         MAP_OFFSET(scope.size) },
    { 'i', {0}, -1,         MAP_OFFSET(num_sources) },
    { 'i', {0}, -1,         MAP_OFFSET(process_location) },
    { 's', {1}, NUM_SCOPES, MAP_OFFSET(scope.names) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t map_strings[] = {
    { "expression",         &map_values[0] },
    { "id",                 &map_values[1] },
    { "mode",               &map_values[2] },
    { "muted",              &map_values[3] },
    { "num_scopes",         &map_values[4] },
    { "num_sources",        &map_values[5] },
    { "process_at",         &map_values[6] },
    { "scope_names",        &map_values[7] },
};

const int N_MAP_STRINGS = sizeof(map_strings)/sizeof(map_strings[0]);
static mapper_string_table_t map_table =
    { map_strings, N_MAP_STRINGS, N_MAP_STRINGS };

/* Generic index and lookup functions to which the above tables would be passed.
 * These are called for specific types below. */

static int mapper_db_property_index(void *thestruct, table extra,
                                    unsigned int index, const char **property,
                                    char *type, const void **value,
                                    int *length, table proptable)
{
    die_unless(type!=0, "type parameter cannot be null.\n");
    die_unless(value!=0, "value parameter cannot be null.\n");
    die_unless(length!=0, "length parameter cannot be null.\n");

    int i=0, j=0;

    /* Unfortunately due to "optional" properties like minimum/maximum, unit,
     * etc, we cannot use an O(1) lookup here--the index changes according to
     * availability of properties.  Thus, we have to search through properties
     * linearly, incrementing a counter along the way, so indexed lookup is
     * O(N).  Meaning iterating through all indexes is O(N^2).  A better way
     * would be to use an iterator-style interface if efficiency was important
     * for iteration. */

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
                                     const void **value, int *length,
                                     table proptable)
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

/*! Update information about a device record based on message parameters. */
static int update_device_record_params(mapper_db_device reg,
                                       const char *name,
                                       mapper_message_t *params,
                                       mapper_timetag_t *current_time)
{
    int updated = 0;
    const char *no_slash = skip_slash(name);

    updated += update_string_if_different(&reg->name, no_slash);
    if (updated)
        reg->id = crc32(0L, (const Bytef *)no_slash, strlen(no_slash)) << 32;

    if (current_time)
        mapper_timetag_cpy(&reg->synced, *current_time);

    if (!params)
        return updated;

    updated += update_string_if_arg(&reg->host, params, AT_HOST);

    updated += update_string_if_arg(&reg->lib_version, params, AT_LIB_VERSION);

    updated += update_int_if_arg(&reg->port, params, AT_PORT);

    updated += update_int_if_arg(&reg->num_inputs, params, AT_NUM_INPUTS);

    updated += update_int_if_arg(&reg->num_outputs, params, AT_NUM_OUTPUTS);

    updated += update_int_if_arg(&reg->num_incoming_maps, params,
                                 AT_NUM_INCOMING_MAPS);

    updated += update_int_if_arg(&reg->num_outgoing_maps, params,
                                 AT_NUM_OUTGOING_MAPS);

    updated += update_int_if_arg(&reg->version, params, AT_REV);

    updated += mapper_msg_add_or_update_extra_params(reg->extra, params);

    return updated;
}

mapper_db_device mapper_db_add_or_update_device_params(mapper_db db,
                                                       const char *name,
                                                       mapper_message_t *params,
                                                       mapper_timetag_t *time)
{
    mapper_db_device dev = mapper_db_get_device_by_name(db, name);
    int rc = 0, updated = 0;

    if (!dev) {
        dev = (mapper_db_device) list_add_new_item((void**)&db->devices,
                                                   sizeof(*dev));
        dev->extra = table_new();
        rc = 1;
    }

    if (dev) {
        updated = update_device_record_params(dev, name, params, time);

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

// Internal function called by /logout protocol handler
void mapper_db_remove_device(mapper_db db, mapper_db_device dev, int quiet)
{
    if (!dev)
        return;

    mapper_db_remove_maps_by_query(db, mapper_db_get_device_maps(db, dev));

    mapper_db_remove_signals_by_query(db, mapper_db_get_device_signals(db, dev));

    list_remove_item(dev, (void**)&db->devices);

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
    list_free_item(dev);
}

mapper_db_device *mapper_db_get_devices(mapper_db db)
{
    return list_from_data(db->devices);
}

mapper_db_device mapper_db_get_device_by_name(mapper_db db, const char *name)
{
    const char *no_slash = skip_slash(name);
    mapper_db_device reg = db->devices;
    while (reg) {
        if (strcmp(reg->name, no_slash)==0)
            return reg;
        reg = list_get_next(reg);
    }
    return 0;
}

mapper_db_device mapper_db_get_device_by_id(mapper_db db, uint64_t id)
{
    mapper_db_device reg = db->devices;
    while (reg) {
        if (id == reg->id)
            return reg;
        reg = list_get_next(reg);
    }
    return 0;
}

static int cmp_query_devices_by_name_match(void *context_data,
                                           mapper_db_device dev)
{
    const char *pattern = (const char*)context_data;
    return strstr(dev->name, pattern)!=0;
}

mapper_db_device *mapper_db_get_devices_by_name_match(mapper_db db,
                                                      const char *pattern)
{
    return ((mapper_db_device *)
            construct_query_context(db->devices, cmp_query_devices_by_name_match,
                                    "s", pattern));
}

static inline int check_type(char type)
{
    return strchr("ifdsct", type) != 0;
}

static const char *op_strings[] =
{
    "==",   /* OP_EQUAL */
    ">",    /* OP_GREATER_THAN */
    ">=",   /* OP_GREATER_THAN_OR_EQUAL */
    "<",    /* OP_LESS_THAN */
    "<=",   /* OP_LESS_THAN_OR_EQUAL */
    "!=",   /* OP_NOT_EQUAL */
    "&&",   /* OP_AND */
    "||"    /* OP_OR */
};
static int num_op_strings = sizeof(op_strings) / sizeof(op_strings[0]);

static mapper_db_op parse_op(const char *s)
{
    if (!s)
        return OP_EQUAL;
    for (int i = 0; i < num_op_strings; i++) {
        if (strcmp(s, op_strings[i])==0)
            return i;
    }
    return OP_UNDEFINED;
}

static int compare_value(mapper_db_op op, int type, int length,
                         const void *val1, const void *val2)
{
    int i, compare = 0, difference = 0;
    switch (type) {
        case 's':
            if (length == 1)
                compare = strcmp((const char*)val1, (const char*)val2);
            else {
                for (i = 0; i < length; i++) {
                    compare += strcmp(((const char**)val1)[i],
                                      ((const char**)val2)[i]);
                    difference += abs(compare);
                }
            }
            break;
        case 'i':
            for (i = 0; i < length; i++) {
                compare += ((int*)val1)[i] > ((int*)val2)[i];
                compare -= ((int*)val1)[i] < ((int*)val2)[i];
                difference += abs(compare);
            }
            break;
        case 'f':
            for (i = 0; i < length; i++) {
                compare += ((float*)val1)[i] > ((float*)val2)[i];
                compare -= ((float*)val1)[i] < ((float*)val2)[i];
                difference += abs(compare);
            }
            break;
        case 'd':
            for (i = 0; i < length; i++) {
                compare += ((double*)val1)[i] > ((double*)val2)[i];
                compare -= ((double*)val1)[i] < ((double*)val2)[i];
                difference += abs(compare);
            }
            break;
        case 'c':
            for (i = 0; i < length; i++) {
                compare += ((char*)val1)[i] > ((char*)val2)[i];
                compare -= ((char*)val1)[i] < ((char*)val2)[i];
                difference += abs(compare);
            }
            break;
        case 't':
            for (i = 0; i < length; i++) {
                compare += ((uint64_t*)val1)[i] > ((uint64_t*)val2)[i];
                compare -= ((uint64_t*)val1)[i] < ((uint64_t*)val2)[i];
                difference += abs(compare);
            }
            break;
        case 'h':
            for (i = 0; i < length; i++) {
                compare += ((char*)val1)[i] > ((char*)val2)[i];
                compare -= ((char*)val1)[i] < ((char*)val2)[i];
                difference += abs(compare);
            }
            break;
        default:
            return 0;
    }
    switch (op) {
        case OP_EQUAL:
            return compare == 0 && !difference;
        case OP_GREATER_THAN:
            return compare > 0;
        case OP_GREATER_THAN_OR_EQUAL:
            return compare >= 0;
        case OP_LESS_THAN:
            return compare < 0;
        case OP_LESS_THAN_OR_EQUAL:
            return compare <= 0;
        case OP_NOT_EQUAL:
            return compare != 0 || difference;
        default:
            return 0;
    }
}

static int cmp_query_devices_by_property(void *context_data, mapper_db_device dev)
{
    int op = *(int*)context_data;
    int length = *(int*)(context_data + sizeof(int));
    char type = *(char*)(context_data + sizeof(int) * 2);
    void *value = *(void**)(context_data + sizeof(int) * 3);
    const char *property = (const char*)(context_data+sizeof(int)*3+sizeof(void*));
    int _length;
    char _type;
    const void *_value;
    if (mapper_db_device_property_lookup(dev, property, &_type, &_value, &_length))
        return 0;
    if (_type != type || _length != length)
        return 0;
    return compare_value(op, type, length, _value, value);
}

mapper_db_device *mapper_db_get_devices_by_property(mapper_db db,
                                                    const char *property,
                                                    char type, int length,
                                                    const void *value,
                                                    const char *operator)
{
    if (!property || !check_type(type) || length < 1)
        return 0;
    mapper_db_op op = parse_op(operator);
    if (op == OP_UNDEFINED || op > OP_NOT_EQUAL)
        return 0;
    return ((mapper_db_device *)
            construct_query_context(db->devices, cmp_query_devices_by_property,
                                    "iicvs", op, length, type, &value, property));
}

mapper_db_device *mapper_db_device_query_union(mapper_db db,
                                               mapper_db_device *query1,
                                               mapper_db_device *query2)
{
    return ((mapper_db_device *)
            mapper_db_query_union(db->devices, (void**)query1, (void**)query2));
}

mapper_db_device *mapper_db_device_query_intersection(mapper_db db,
                                                      mapper_db_device *query1,
                                                      mapper_db_device *query2)
{
    return ((mapper_db_device *)
            mapper_db_query_intersection(db->devices, (void**)query1,
                                         (void**)query2));
}

mapper_db_device *mapper_db_device_query_difference(mapper_db db,
                                                    mapper_db_device *query1,
                                                    mapper_db_device *query2)
{
    return ((mapper_db_device *)
            mapper_db_query_difference(db->devices, (void**)query1,
                                       (void**)query2));
}

mapper_db_device *mapper_db_device_next(mapper_db_device* dev)
{
    return (mapper_db_device*) iterator_next((void**)dev);
}

void mapper_db_device_done(mapper_db_device *dev)
{
    list_done((void**)dev);
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
    mapper_db_device reg = db->devices;
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

mapper_db_device mapper_db_get_expired_device(mapper_db db, uint32_t last_ping)
{
    mapper_db_device reg = db->devices;
    while (reg) {
        if (reg->synced.sec && (reg->synced.sec < last_ping)) {
            return reg;
        }
        reg = list_get_next(reg);
    }
    return 0;
}

/**** Signals ****/

/*! Update information about a signal record based on message parameters. */
static int update_signal_record_params(mapper_db_signal sig,
                                       mapper_message_t *params)
{
    lo_arg **args;
    const char *types = 0;
    int length, updated = 0, result;

    if (!params)
        return updated;

    updated += update_int64_if_arg((int64_t*)&sig->id, params, AT_ID);

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
    mapper_db_signal sig = 0;
    int rc = 0, updated = 0;

    mapper_db_device dev = mapper_db_get_device_by_name(db, device_name);
    if (dev)
        sig = mapper_db_get_device_signal_by_name(db, dev, name);
    else
        dev = mapper_db_add_or_update_device_params(db, device_name, 0, 0);

    if (!sig) {
        sig = (mapper_db_signal) list_add_new_item((void**)&db->signals,
                                                   sizeof(mapper_db_signal_t));

        // also add device record if necessary
        sig->device = dev;

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
        updated = update_signal_record_params(sig, params);

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
    int direction = *(int*)context_data;
    return !direction || (sig->direction & direction);
}

mapper_db_signal *mapper_db_get_signals(mapper_db db)
{
    return list_from_data(db->signals);
}

mapper_db_signal *mapper_db_get_inputs(mapper_db db)
{
    return ((mapper_db_signal *)
            construct_query_context(db->signals, cmp_query_signals,
                                    "i", DI_INCOMING));
}

mapper_db_signal *mapper_db_get_outputs(mapper_db db)
{
    return ((mapper_db_signal *)
            construct_query_context(db->signals, cmp_query_signals,
                                    "i", DI_OUTGOING));
}

static int cmp_query_signals_by_name(void *context_data, mapper_db_signal sig)
{
    int direction = *(int*)context_data;
    const char *name = (const char*)(context_data + sizeof(int));
    return ((!direction || (sig->direction & direction))
            && (strcmp(sig->name, name)==0));
}

mapper_db_signal *mapper_db_get_signals_by_name(mapper_db db, const char *name)
{
    return ((mapper_db_signal *)
            construct_query_context(db->signals, cmp_query_signals_by_name,
                                    "is", DI_ANY, name));
}

mapper_db_signal *mapper_db_get_inputs_by_name(mapper_db db, const char *name)
{
    return ((mapper_db_signal *)
            construct_query_context(db->signals, cmp_query_signals_by_name,
                                    "is", DI_INCOMING, name));
}

mapper_db_signal *mapper_db_get_outputs_by_name(mapper_db db, const char *name)
{
    return ((mapper_db_signal *)
            construct_query_context(db->signals, cmp_query_signals_by_name,
                                    "is", DI_OUTGOING, name));
}

static int cmp_query_signals_by_name_match(void *context_data,
                                           mapper_db_signal sig)
{
    int direction = *(int*)context_data;
    const char *pattern = (const char*)(context_data + sizeof(int));
    return ((!direction || (sig->direction & direction))
            && (strstr(sig->name, pattern)!=0));
}

mapper_db_signal *mapper_db_get_signals_by_name_match(mapper_db db,
                                                      const char *pattern)
{
    return ((mapper_db_signal *)
            construct_query_context(db->devices, cmp_query_signals_by_name_match,
                                    "is", DI_ANY, pattern));
}

mapper_db_signal *mapper_db_get_inputs_by_name_match(mapper_db db,
                                                     const char *pattern)
{
    return ((mapper_db_signal *)
            construct_query_context(db->devices, cmp_query_signals_by_name_match,
                                    "is", DI_INCOMING, pattern));
}

mapper_db_signal *mapper_db_get_outputs_by_name_match(mapper_db db,
                                                      const char *pattern)
{
    return ((mapper_db_signal *)
            construct_query_context(db->devices, cmp_query_signals_by_name_match,
                                    "is", DI_OUTGOING, pattern));
}

static int cmp_query_signals_by_property(void *context_data, mapper_db_signal sig)
{
    int op = *(int*)context_data;
    int length = *(int*)(context_data + sizeof(int));
    char type = *(char*)(context_data + sizeof(int) * 2);
    void *value = *(void**)(context_data + sizeof(int) * 3);
    const char *property = (const char*)(context_data+sizeof(int)*3+sizeof(void*));
    int _length;
    char _type;
    const void *_value;
    if (mapper_db_signal_property_lookup(sig, property, &_type, &_value, &_length))
        return 0;
    if (_type != type || _length != length)
        return 0;
    return compare_value(op, type, length, _value, value);
}

mapper_db_signal *mapper_db_get_signals_by_property(mapper_db db,
                                                    const char *property,
                                                    char type, int length,
                                                    const void *value,
                                                    const char *operator)
{
    if (!property || !check_type(type) || length < 1)
        return 0;
    mapper_db_op op = parse_op(operator);
    if (op == OP_UNDEFINED || op > OP_NOT_EQUAL)
        return 0;
    return ((mapper_db_signal *)
            construct_query_context(db->signals, cmp_query_signals_by_property,
                                    "iicvs", op, length, type, &value, property));
}

static int cmp_query_device_signals(void *context_data, mapper_db_signal sig)
{
    uint64_t dev_id = *(int64_t*)context_data;
    int direction = *(int*)(context_data + sizeof(uint64_t));
    return ((!direction || (sig->direction & direction))
            && (dev_id == sig->device->id));
}

mapper_db_signal *mapper_db_get_device_signals(mapper_db db,
                                               mapper_db_device dev)
{
    if (!dev)
        return 0;
    return ((mapper_db_signal *)
            construct_query_context(db->signals, cmp_query_device_signals,
                                    "hi", dev->id, DI_ANY));
}

mapper_db_signal *mapper_db_get_device_inputs(mapper_db db,
                                              mapper_db_device dev)
{
    if (!dev)
        return 0;
    return ((mapper_db_signal *)
            construct_query_context(db->signals, cmp_query_device_signals,
                                    "hi", dev->id, DI_INCOMING));
}

mapper_db_signal *mapper_db_get_device_outputs(mapper_db db,
                                               mapper_db_device dev)
{
    if (!dev)
        return 0;
    return ((mapper_db_signal *)
            construct_query_context(db->signals, cmp_query_device_signals,
                                    "hi", dev->id, DI_OUTGOING));
}

static mapper_db_signal get_device_signal_by_name_internal(mapper_db db,
                                                           mapper_db_device dev,
                                                           const char *sig_name,
                                                           int direction)
{
    if (!dev)
        return 0;
    mapper_db_signal sig = db->signals;
    if (!sig)
        return 0;

    while (sig) {
        if ((sig->device == dev) && (!direction || (sig->direction & direction))
            && strcmp(sig->name, skip_slash(sig_name))==0)
            return sig;
        sig = list_get_next(sig);
    }
    return 0;
}

mapper_db_signal mapper_db_get_device_signal_by_name(mapper_db db,
                                                     mapper_db_device dev,
                                                     const char *sig_name)
{
    return get_device_signal_by_name_internal(db, dev, sig_name, DI_ANY);
}

mapper_db_signal mapper_db_get_device_input_by_name(mapper_db db,
                                                    mapper_db_device dev,
                                                    const char *sig_name)
{
    return get_device_signal_by_name_internal(db, dev, sig_name, DI_INCOMING);
}

mapper_db_signal mapper_db_get_device_output_by_name(mapper_db db,
                                                     mapper_db_device dev,
                                                     const char *sig_name)
{
    return get_device_signal_by_name_internal(db, dev, sig_name, DI_OUTGOING);
}

static mapper_db_signal get_device_signal_by_index_internal(mapper_db db,
                                                            mapper_db_device dev,
                                                            int index,
                                                            int direction)
{
    if (!dev || index < 0)
        return 0;
    mapper_db_signal sig = db->signals;
    if (!sig)
        return 0;

    int count = -1;
    while (sig && count < index) {
        if ((sig->device == dev) && (!direction || (sig->direction & direction))) {
            if (++count == index)
                return sig;
        }
        sig = list_get_next(sig);
    }
    return 0;
}

mapper_db_signal mapper_db_get_device_signal_by_index(mapper_db db,
                                                      mapper_db_device dev,
                                                      int index)
{
    return get_device_signal_by_index_internal(db, dev, index, DI_ANY);
}

mapper_db_signal mapper_db_get_device_input_by_index(mapper_db db,
                                                     mapper_db_device dev,
                                                     int index)
{
    return get_device_signal_by_index_internal(db, dev, index, DI_INCOMING);
}

mapper_db_signal mapper_db_get_device_output_by_index(mapper_db db,
                                                      mapper_db_device dev,
                                                      int index)
{
    return get_device_signal_by_index_internal(db, dev, index, DI_OUTGOING);
}

static int cmp_query_device_signals_match_name(void *context_data,
                                               mapper_db_signal sig)
{
    uint64_t dev_id = *(int64_t*)context_data;
    int direction = *(int*)(context_data + sizeof(uint64_t));
    const char *pattern = (const char*)(context_data+sizeof(int64_t)+sizeof(int));

    return ((!direction || (sig->direction & direction))
            && (sig->device->id == dev_id)
            && strstr(sig->name, pattern));
}

mapper_db_signal *mapper_db_get_device_signals_by_name_match(mapper_db db,
                                                             mapper_db_device dev,
                                                             char const *pattern)
{
    if (!dev)
        return 0;
    return ((mapper_db_signal *)
            construct_query_context(db->signals,
                                    cmp_query_device_signals_match_name,
                                    "his", dev->id, DI_ANY, pattern));
}

mapper_db_signal *mapper_db_get_device_inputs_by_name_match(mapper_db db,
                                                            mapper_db_device dev,
                                                            const char *pattern)
{
    if (!dev)
        return 0;
    return ((mapper_db_signal *)
            construct_query_context(db->signals,
                                    cmp_query_device_signals_match_name,
                                    "his", dev->id, DI_INCOMING, pattern));
}

mapper_db_signal *mapper_db_get_device_outputs_by_name_match(mapper_db db,
                                                             mapper_db_device dev,
                                                             char const *pattern)
{
    if (!dev)
        return 0;
    return ((mapper_db_signal *)
            construct_query_context(db->signals,
                                    cmp_query_device_signals_match_name,
                                    "his", dev->id, DI_OUTGOING, pattern));
}

mapper_db_signal *mapper_db_signal_query_union(mapper_db db,
                                               mapper_db_signal *query1,
                                               mapper_db_signal *query2)
{
    return ((mapper_db_signal *)
            mapper_db_query_union(db->signals, (void**)query1, (void**)query2));
}

mapper_db_signal *mapper_db_signal_query_intersection(mapper_db db,
                                                      mapper_db_signal *query1,
                                                      mapper_db_signal *query2)
{
    return ((mapper_db_signal *)
            mapper_db_query_intersection(db->signals, (void**)query1,
                                         (void**)query2));
}

mapper_db_signal *mapper_db_signal_query_difference(mapper_db db,
                                                    mapper_db_signal *query1,
                                                    mapper_db_signal *query2)
{
    return ((mapper_db_signal *)
            mapper_db_query_difference(db->signals, (void**)query1,
                                       (void**)query2));
}

mapper_db_signal *mapper_db_signal_next(mapper_db_signal *sig)
{
    return (mapper_db_signal*) iterator_next((void**)sig);
}

void mapper_db_signal_done(mapper_db_signal *sig)
{
    list_done((void**)sig);
}

static void mapper_db_remove_signal(mapper_db db, mapper_db_signal sig)
{
    // remove any stored maps using this signal
    mapper_db_remove_maps_by_query(db, mapper_db_get_signal_maps(db, sig));

    list_remove_item(sig, (void**)&db->signals);

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

    list_free_item(sig);
}

// Internal function called by /logout protocol handler.
void mapper_db_remove_signal_by_name(mapper_db db, const char *device_name,
                                     const char *signal_name)
{
    mapper_db_device dev = mapper_db_get_device_by_name(db, device_name);
    if (!dev)
        return;
    mapper_db_signal sig = mapper_db_get_device_signal_by_name(db, dev,
                                                               signal_name);
    if (sig)
        mapper_db_remove_signal(db, sig);
}

void mapper_db_remove_signals_by_query(mapper_db db, mapper_db_signal_t **s)
{
    while (s) {
        mapper_db_signal sig = *s;
        s = mapper_db_signal_next(s);
        mapper_db_remove_signal(db, sig);
    }
}

/**** Map records ****/

static int mapper_db_map_add_scope(mapper_map_scope scope, const char *name)
{
    int i;
    if (!scope || !name)
        return 1;

    // Check if scope is already stored for this map
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

static int mapper_db_map_remove_scope(mapper_map_scope scope, int index)
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

int mapper_db_map_update_scope(mapper_map_scope scope, lo_arg **scope_list,
                               int num)
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
                mapper_db_map_remove_scope(scope, i);
                updated++;
            }
        }
        // ...then add any new scopes
        for (i=0; i<num; i++)
            updated += (1 - mapper_db_map_add_scope(scope, &scope_list[i]->s));
    }
    return updated;
}

static int compare_slot_names(const void *l, const void *r)
{
    int result = strcmp(((mapper_db_map_slot)l)->signal->device->name,
                        ((mapper_db_map_slot)r)->signal->device->name);
    if (result == 0)
        return strcmp(((mapper_db_map_slot)l)->signal->name,
                      ((mapper_db_map_slot)r)->signal->name);
    return result;
}

/*! Update information about a map record based on message parameters. */
static int update_map_record_params(mapper_db db, mapper_db_map map,
                                    const char *src_name,
                                    mapper_message_t *params)
{
    lo_arg **args;
    const char *types;
    int i, slot = -1, updated = 0, length, result;
    mapper_db_map_slot s;

    if (!params)
        return updated;

    updated += update_int64_if_arg((int64_t*)&map->id, params, AT_ID);

    if (map->num_sources == 1)
        slot = 0;
    else if (src_name) {
        // retrieve slot
        for (i = 0; i < map->num_sources; i++) {
            if (strcmp(map->sources[i].signal->name, src_name) == 0) {
                slot = i;
                break;
            }
        }
    }

    /* @slot */
    int slot_id;
    if (!mapper_msg_get_param_if_int(params, AT_SLOT, &slot_id)) {
        if (slot >= 0)
            map->sources[slot].slot_id = slot_id;
    }

    /* @srcType */
    args = mapper_msg_get_param(params, AT_SRC_TYPE, &types, &length);
    if (args && types && (types[0] == 'c' || types[0] == 's' || types[0] == 'S')) {
        char type;
        if (slot >= 0) {
            type = types[0] == 'c' ? (*args)->c : (&(*args)->s)[0];
            if (map->sources[slot].type != type) {
                map->sources[slot].type = type;
                updated++;
            }
        }
        else if (length == map->num_sources) {
            for (i = 0; i < length; i++) {
                type = types[0] == 'c' ? (args[i])->c : (&(args[i])->s)[0];
                if (map->sources[i].type != type) {
                    map->sources[i].type = type;
                    updated++;
                }
            }
        }
    }

    /* @destType */
    updated += update_char_if_arg(&map->destination.type, params, AT_DEST_TYPE);

    /* @srcLength */
    args = mapper_msg_get_param(params, AT_SRC_LENGTH, &types, &length);
    if (args && types && types[0] == 'i') {
        if (slot >= 0) {
            if (map->sources[slot].length != (*args)->i) {
                map->sources[slot].length = (*args)->i;
                updated++;
            }
        }
        else if (length == map->num_sources) {
            for (i = 0; i < length; i++) {
                if (map->sources[i].length != args[i]->i) {
                    map->sources[i].length = args[i]->i;
                    updated++;
                }
            }
        }
    }

    /* @destLength */
    updated += update_int_if_arg(&map->destination.length, params, AT_DEST_LENGTH);

    /* @srcMax */
    args = mapper_msg_get_param(params, AT_SRC_MAX, &types, &length);
    if (args && types) {
        if (slot >= 0) {
            s = &map->sources[slot];
            if (length == s->length) {
                if (!s->maximum)
                    s->maximum = calloc(1, length * mapper_type_size(s->type));
                for (i = 0; i < length; i++) {
                    result = propval_set_from_lo_arg(s->maximum, s->type,
                                                     args[i], types[i], i);
                }
            }
        }
        else if (map->num_sources == 1) {
            s = &map->sources[0];
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
            // TODO: passing multiple source ranges for convergent mappings
        }
    }

    /* @srcMin */
    args = mapper_msg_get_param(params, AT_SRC_MIN, &types, &length);
    if (args && types) {
        if (slot >= 0) {
            s = &map->sources[slot];
            if (length == s->length) {
                if (!s->minimum)
                    s->minimum = calloc(1, length * mapper_type_size(s->type));
                for (i = 0; i < length; i++) {
                    result = propval_set_from_lo_arg(s->minimum, s->type,
                                                     args[i], types[i], i);
                }
            }
        }
        else if (map->num_sources == 1) {
            s = &map->sources[0];
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
            // TODO: passing multiple source ranges for convergent mappings
        }
    }

    /* @destMax */
    args = mapper_msg_get_param(params, AT_DEST_MAX, &types, &length);
    if (args && types) {
        s = &map->destination;
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
        s = &map->destination;
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
                && bound_max != map->sources[slot].bound_max) {
                map->sources[slot].bound_max = bound_max;
                updated++;
            }
        }
        else if (length == map->num_sources) {
            for (i = 0; i < map->num_sources; i++) {
                if (types[i] != 's' && types[i] != 'S')
                    continue;
                bound_max = mapper_get_boundary_action_from_string(&args[i]->s);
                if (bound_max != BA_UNDEFINED
                    && bound_max != map->sources[i].bound_max) {
                    map->sources[i].bound_max = bound_max;
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
                && bound_min != map->sources[slot].bound_min) {
                map->sources[slot].bound_min = bound_min;
                updated++;
            }
        }
        else if (length == map->num_sources) {
            for (i = 0; i < map->num_sources; i++) {
                if (types[i] != 's' && types[i] != 'S')
                    continue;
                bound_min = mapper_get_boundary_action_from_string(&args[i]->s);
                if (bound_min != BA_UNDEFINED
                    && bound_min != map->sources[i].bound_min) {
                    map->sources[i].bound_min = bound_min;
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
                && bound_max != map->destination.bound_max) {
                map->destination.bound_max = bound_max;
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
                && bound_min != map->destination.bound_min) {
                map->destination.bound_min = bound_min;
                updated++;
            }
        }
    }

    /* @causeUpdate */
    args = mapper_msg_get_param(params, AT_CAUSE_UPDATE, &types, &length);
    if (args && types && (length == map->num_sources)) {
        int cause_update;
        for (i = 0; i < map->num_sources; i++) {
            cause_update = (types[i] == 'T');
            if (map->sources[i].cause_update != cause_update) {
                map->sources[i].cause_update = cause_update;
                updated++;
            }
        }
    }

    /* @sendAsInstance */
    args = mapper_msg_get_param(params, AT_SEND_AS_INSTANCE, &types, &length);
    if (args && types && (length == map->num_sources)) {
        int send_as_instance;
        for (i = 0; i < map->num_sources; i++) {
            send_as_instance = (types[i] == 'T');
            if (map->sources[i].send_as_instance != send_as_instance) {
                map->sources[i].send_as_instance = send_as_instance;
                updated++;
            }
        }
    }

    /* Mode */
    args = mapper_msg_get_param(params, AT_MODE, &types, &length);
    if (args && types && (types[0] == 's' || types[0] == 'S')) {
        mapper_mode_type mode = mapper_get_mode_type_from_string(&args[0]->s);
        if (map->mode != mode) {
            map->mode = mode;
            updated++;
        }
    }

    /* Expression */
    args = mapper_msg_get_param(params, AT_EXPRESSION, &types, &length);
    if (args && types && (types[0] == 's' || types[0] == 'S')) {
        if (!map->expression || strcmp(map->expression, &args[0]->s)) {
            map->expression = realloc(map->expression, strlen(&args[0]->s)+1);
            strcpy(map->expression, &args[0]->s);
            updated++;
        }
    }

    /* Processing location */
    args = mapper_msg_get_param(params, AT_PROCESS, &types, &length);
    if (args && types && (types[0] == 's' || types[0] == 'S')) {
        int at_source = (strcmp(&args[0]->s, "source")==0);
        if (at_source && map->process_location != MAPPER_SOURCE) {
            map->process_location = MAPPER_SOURCE;
            updated++;
        }
        else if (!at_source && map->process_location != MAPPER_DESTINATION) {
            map->process_location = MAPPER_DESTINATION;
            updated++;
        }
    }

    int mute = mapper_msg_get_mute(params);
    if (mute != -1 && mute != map->muted) {
        map->muted = mute;
        updated++;
    }

    lo_arg **a_scopes = mapper_msg_get_param(params, AT_SCOPE, &types, &length);
    if (types && (types[0] == 's' || types[0] == 'S'))
        mapper_db_map_update_scope(&map->scope, a_scopes, length);

    updated += mapper_msg_add_or_update_extra_params(map->extra, params);
    return updated;
}

mapper_db_map mapper_db_add_or_update_map_params(mapper_db db, int num_sources,
                                                 const char **src_names,
                                                 const char *dest_name,
                                                 mapper_message_t *params)
{
    if (num_sources >= MAX_NUM_MAP_SOURCES) {
        trace("error: maximum mapping sources exceeded.\n");
        return 0;
    }

    mapper_db_map map;
    int rc = 0, updated = 0, devnamelen, i, j;
    char *devnamep, *signame, devname[256];

    /* We could be part of larger "convergent" mapping, so we will retrieve
     * record by mapping id instead of names. */
    int64_t id;
    if (mapper_msg_get_param_if_int64(params, AT_ID, &id)) {
        trace("error: no 'id' property for updating map.");
        return 0;
    }
    map = mapper_db_get_map_by_id(db, id);

    if (!map) {
        map = (mapper_db_map) list_add_new_item((void**)&db->maps,
                                                sizeof(mapper_db_map_t));
        map->num_sources = num_sources;
        map->sources = ((mapper_db_map_slot)
                        calloc(1, sizeof(struct _mapper_db_map_slot)
                               * num_sources));
        for (i = 0; i < num_sources; i++) {
            devnamelen = mapper_parse_names(src_names[i], &devnamep, &signame);
            if (!devnamelen || devnamelen >= 256) {
                trace("error extracting device name\n");
                // clean up partially-built record
                list_remove_item(map, (void**)&db->maps);
                list_free_item(map);
                return 0;
            }
            strncpy(devname, devnamep, devnamelen);
            devname[devnamelen] = 0;

            // also add source signal if necessary
            map->sources[i].signal =
                mapper_db_add_or_update_signal_params(db, signame, devname, 0);
            map->sources[i].slot_id = i;
            map->sources[i].cause_update = 1;
            map->sources[i].minimum = map->sources[i].maximum = 0;
        }
        devnamelen = mapper_parse_names(dest_name, &devnamep, &signame);
        if (!devnamelen || devnamelen >= 256) {
            trace("error extracting device name\n");
            // clean up partially-built record
            list_remove_item(map, (void**)&db->maps);
            list_free_item(map);
            return 0;
        }
        strncpy(devname, devnamep, devnamelen);
        devname[devnamelen] = 0;
        map->destination.minimum = map->destination.maximum = 0;

        // also add destination signal if necessary
        map->destination.signal =
            mapper_db_add_or_update_signal_params(db, signame, devname, 0);
        map->destination.cause_update = 1;

        map->extra = table_new();
        rc = 1;
    }
    else if (map->num_sources < num_sources) {
        // add one or more sources
        for (i = 0; i < num_sources; i++) {
            devnamelen = mapper_parse_names(src_names[i], &devnamep, &signame);
            if (!devnamelen || devnamelen >= 256) {
                trace("error extracting device name\n");
                return 0;
            }
            strncpy(devname, devnamep, devnamelen);
            devname[devnamelen] = 0;
            for (j = 0; j < map->num_sources; j++) {
                if (strlen(map->sources[j].signal->device->name) == devnamelen
                    && strcmp(devname, map->sources[j].signal->device->name)==0
                    && strcmp(signame, map->sources[j].signal->name)==0) {
                    map->sources[j].slot_id = i;
                    break;
                }
            }
            if (j == map->num_sources) {
                map->num_sources++;
                map->sources = realloc(map->sources,
                                       sizeof(struct _mapper_db_map_slot)
                                       * map->num_sources);
                map->sources[j].signal =
                    mapper_db_add_or_update_signal_params(db, signame, devname, 0);
                map->sources[j].slot_id = i;
                map->sources[j].cause_update = 1;
                map->sources[j].minimum = map->sources[j].maximum = 0;
            }
        }
        // slots should be in alphabetical order
        qsort(map->sources, map->num_sources,
              sizeof(mapper_db_map_slot_t), compare_slot_names);
    }

    if (map) {
        updated = update_map_record_params(db, map, num_sources > 1
                                           ? 0 : src_names[0], params);

        if (rc || updated) {
            fptr_list cb = db->map_callbacks;
            while (cb) {
                mapper_db_map_handler *h = cb->f;
                h(map, rc ? MDB_NEW : MDB_MODIFY, cb->context);
                cb = cb->next;
            }
        }
    }

    return map;
}

int mapper_db_map_property_index(mapper_db_map map, unsigned int index,
                                 const char **property, char *type,
                                 const void **value, int *length)
{
    return mapper_db_property_index(map, map->extra, index, property, type,
                                    value, length, &map_table);
}

int mapper_db_map_property_lookup(mapper_db_map map, const char *property,
                                  char *type, const void **value, int *length)
{
    return mapper_db_property_lookup(map, map->extra, property, type,
                                     value, length, &map_table);
}

int mapper_db_map_slot_property_index(mapper_db_map_slot slot, unsigned int index,
                                      const char **property, char *type,
                                      const void **value, int *length)
{
    return mapper_db_property_index(slot, 0, index, property, type,
                                    value, length, &slot_table);
}

int mapper_db_map_slot_property_lookup(mapper_db_map_slot slot,
                                       const char *property, char *type,
                                       const void **value, int *length)
{
    return mapper_db_property_lookup(slot, 0, property, type,
                                     value, length, &slot_table);
}

void mapper_db_add_map_callback(mapper_db db, mapper_db_map_handler *h,
                                void *user)
{
    add_callback(&db->map_callbacks, h, user);
}

void mapper_db_remove_map_callback(mapper_db db, mapper_db_map_handler *h,
                                   void *user)
{
    remove_callback(&db->map_callbacks, h, user);
}

mapper_db_map *mapper_db_get_maps(mapper_db db)
{
    return list_from_data(db->maps);
}

mapper_db_map mapper_db_get_map_by_id(mapper_db db, uint64_t id)
{
    mapper_db_map map = db->maps;
    if (!map)
        return 0;
    while (map) {
        if (map->id == id)
            return map;
        map = list_get_next(map);
    }
    return 0;
}

static int cmp_query_maps_by_property(void *context_data, mapper_db_map map)
{
    int op = *(int*)context_data;
    int length = *(int*)(context_data + sizeof(int));
    char type = *(char*)(context_data + sizeof(int) * 2);
    void *value = *(void**)(context_data + sizeof(int) * 3);
    const char *property = (const char*)(context_data+sizeof(int)*3+sizeof(void*));
    int _length;
    char _type;
    const void *_value;
    if (mapper_db_map_property_lookup(map, property, &_type, &_value, &_length))
        return 0;
    if (_type != type || _length != length)
        return 0;
    return compare_value(op, type, length, _value, value);
}

mapper_db_map *mapper_db_get_maps_by_property(mapper_db db,
                                              const char *property,
                                              char type, int length,
                                              const void *value,
                                              const char *operator)
{
    if (!property || !check_type(type) || length < 1)
        return 0;
    mapper_db_op op = parse_op(operator);
    if (op == OP_UNDEFINED || op > OP_NOT_EQUAL)
        return 0;
    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_maps_by_property,
                                    "iicvs", op, length, type, &value, property));
}

static int cmp_query_maps_by_slot_property(void *context_data,
                                           mapper_db_map map)
{
    int i, direction = *(int*)context_data;
    int op = *(int*)(context_data + sizeof(int));
    int length2, length1 = *(int*)(context_data + sizeof(int) * 2);
    char type2, type1 = *(char*)(context_data + sizeof(int) * 3);
    const void *value2, *value1 = *(void**)(context_data + sizeof(int) * 4);
    const char *property = (const char*)(context_data + sizeof(int) * 4
                                         + sizeof(void*));
    if (!direction || direction & DI_INCOMING) {
        if (!mapper_db_map_slot_property_lookup(&map->destination, property,
                                                &type2, &value2, &length2)
            && type1 == type2 && length1 == length2
            && compare_value(op, type1, length1, value2, value1))
            return 1;
    }
    if (!direction || direction & DI_OUTGOING) {
        for (i = 0; i < map->num_sources; i++) {
            if (!mapper_db_map_slot_property_lookup(&map->sources[i], property,
                                                    &type2, &value2, &length2)
                && type1 == type2 && length1 == length2
                && compare_value(op, type1, length1, value2, value1))
                return 1;
        }
    }
    return 0;
}

mapper_db_map *mapper_db_get_maps_by_slot_property(mapper_db db,
                                                   const char *property,
                                                   char type, int length,
                                                   const void *value,
                                                   const char *operator)
{
    if (!property || !check_type(type) || length < 1)
        return 0;
    mapper_db_op op = parse_op(operator);
    if (op == OP_UNDEFINED || op > OP_NOT_EQUAL)
        return 0;
    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_maps_by_slot_property,
                                    "iiicvs", DI_ANY, op, length, type,
                                    &value, property));
}

mapper_db_map *mapper_db_get_maps_by_src_slot_property(mapper_db db,
                                                       const char *property,
                                                       char type, int length,
                                                       const void *value,
                                                       const char *operator)
{
    if (!property || !check_type(type) || length < 1)
        return 0;
    mapper_db_op op = parse_op(operator);
    if (op == OP_UNDEFINED || op > OP_NOT_EQUAL)
        return 0;
    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_maps_by_slot_property,
                                    "iiicvs", DI_OUTGOING, op, length, type,
                                    &value, property));
}

mapper_db_map *mapper_db_get_maps_by_dest_slot_property(mapper_db db,
                                                        const char *property,
                                                        char type, int length,
                                                        const void *value,
                                                        const char *operator)
{
    if (!property || !check_type(type) || length < 1)
        return 0;
    mapper_db_op op = parse_op(operator);
    if (op == OP_UNDEFINED || op > OP_NOT_EQUAL)
        return 0;
    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_maps_by_slot_property,
                                    "iiicvs", DI_INCOMING, op, length, type,
                                    &value, property));
}

static int cmp_query_device_maps(void *context_data, mapper_db_map map)
{
    uint64_t dev_id = *(uint64_t*)context_data;
    int direction = *(int*)(context_data + sizeof(uint64_t));
    if (!direction || (direction & DI_OUTGOING)) {
        int i;
        for (i = 0; i < map->num_sources; i++) {
            if (map->sources[i].signal->device->id == dev_id)
                return 1;
        }
    }
    if (!direction || (direction & DI_INCOMING)) {
        if (map->destination.signal->device->id == dev_id)
            return 1;
    }
    return 0;
}

mapper_db_map *mapper_db_get_device_maps(mapper_db db, mapper_db_device dev)
{
    if (!dev)
        return 0;
    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_device_maps,
                                    "hi", dev->id, DI_ANY));
}

mapper_db_map *mapper_db_get_device_outgoing_maps(mapper_db db,
                                                  mapper_db_device dev)
{
    if (!dev)
        return 0;
    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_device_maps,
                                    "hi", dev->id, DI_OUTGOING));
}

mapper_db_map *mapper_db_get_device_incoming_maps(mapper_db db,
                                                  mapper_db_device dev)
{
    if (!dev)
        return 0;
    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_device_maps,
                                    "hi", dev->id, DI_INCOMING));
}

static int cmp_query_maps_by_all_src_devices(void *context_data,
                                             mapper_db_map map)
{
    int num_sources = *(int*)context_data;
    if (map->num_sources != num_sources)
        return 0;
    int i, j, found;
    uint64_t *ids = (uint64_t*)(context_data + sizeof(int));
    for (i = 0; i < num_sources; i++) {
        found = 0;
        for (j = 0; j < num_sources; j++) {
            if (map->sources[j].signal->device->id == ids[i]) {
                found = 1;
                break;
            }
        }
        if (!found)
            return 0;
    }
    return 1;
}

mapper_db_map *mapper_db_get_maps_by_src_devices(mapper_db db, int num_sources,
                                                 mapper_db_device *devices)
{
    if (!db->maps || !devices || num_sources > MAX_NUM_MAP_SOURCES)
        return 0;
    int i;
    uint64_t ids[num_sources];
    for (i = 0; i < num_sources; i++) {
        if (!devices[i])
            return 0;
        ids[i] = devices[i]->id;
    }

    // TODO: expand to more sources if necessary
    die_unless(num_sources<10, "can't currently handle more than 10 sources.\n");
    char types[4];
    snprintf(types, 4, "ih%d", num_sources);

    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_maps_by_all_src_devices,
                                    types, num_sources, ids));
}

static int cmp_query_signal_maps(void *context_data, mapper_db_map map)
{
    mapper_db_signal sig = *(mapper_db_signal*)context_data;
    int direction = *(int*)(context_data + sizeof(int64_t));
    if (!direction || (direction & DI_OUTGOING)) {
        int i;
        for (i = 0; i < map->num_sources; i++) {
            if (map->sources[i].signal == sig)
                return 1;
        }
    }
    if (!direction || (direction & DI_INCOMING)) {
        if (map->destination.signal == sig)
            return 1;
    }
    return 0;
}

mapper_db_map *mapper_db_get_signal_maps(mapper_db db, mapper_db_signal sig)
{
    if (!sig)
        return 0;
    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_signal_maps,
                                    "vi", &sig, DI_ANY));
}

mapper_db_map *mapper_db_get_signal_outgoing_maps(mapper_db db,
                                                  mapper_db_signal sig)
{
    if (!sig)
        return 0;
    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_signal_maps,
                                    "vi", &sig, DI_OUTGOING));
}

mapper_db_map *mapper_db_get_signal_incoming_maps(mapper_db db,
                                                  mapper_db_signal sig)
{
    if (!sig)
        return 0;
    return ((mapper_db_map *)
            construct_query_context(db->maps, cmp_query_signal_maps,
                                    "vi", &sig, DI_INCOMING));
}

mapper_db_map *mapper_db_map_query_union(mapper_db db, mapper_db_map *query1,
                                         mapper_db_map *query2)
{
    return ((mapper_db_map *)
            mapper_db_query_union(db->maps, (void**)query1, (void**)query2));
}

mapper_db_map *mapper_db_map_query_intersection(mapper_db db,
                                                mapper_db_map *query1,
                                                mapper_db_map *query2)
{
    return ((mapper_db_map *)
            mapper_db_query_intersection(db->maps, (void**)query1,
                                         (void**)query2));
}

mapper_db_map *mapper_db_map_query_difference(mapper_db db,
                                              mapper_db_map *query1,
                                              mapper_db_map *query2)
{
    return ((mapper_db_map *)
            mapper_db_query_difference(db->maps, (void**)query1, (void**)query2));
}

mapper_db_map_t **mapper_db_map_next(mapper_db_map *maps)
{
    return (mapper_db_map*) iterator_next((void**)maps);
}

void mapper_db_map_done(mapper_db_map *map)
{
    list_done((void**)map);
}

void mapper_db_remove_maps_by_query(mapper_db db, mapper_db_map_t **maps)
{
    while (maps) {
        mapper_db_map map = *maps;
        maps = mapper_db_map_next(maps);
        mapper_db_remove_map(db, map);
    }
}

static void free_slot(mapper_db_map_slot s)
{
    if (s->minimum)
        free(s->minimum);
    if (s->maximum)
        free(s->maximum);
}

void mapper_db_remove_map(mapper_db db, mapper_db_map map)
{
    int i;
    if (!map)
        return;

    list_remove_item(map, (void**)&db->maps);

    fptr_list cb = db->map_callbacks;
    while (cb) {
        mapper_db_map_handler *h = cb->f;
        h(map, MDB_REMOVE, cb->context);
        cb = cb->next;
    }

    if (map->sources) {
        for (i = 0; i < map->num_sources; i++) {
            free_slot(&map->sources[i]);
        }
        free(map->sources);
    }
    free_slot(&map->destination);
    if (map->scope.size && map->scope.names) {
        for (i=0; i<map->scope.size; i++)
            free(map->scope.names[i]);
        free(map->scope.names);
        free(map->scope.hashes);
    }
    if (map->expression)
        free(map->expression);
    if (map->extra)
        table_free(map->extra, 1);
    list_free_item(map);
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
    while ((cb = db->map_callbacks)) {
        db->map_callbacks = db->map_callbacks->next;
        free(cb);
    }
}

#ifdef DEBUG
static void print_map_slot(const char *label, int index, mapper_db_map_slot s)
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

    mapper_db_device dev = db->devices;
    printf("Registered devices:\n");
    while (dev) {
        printf("  name='%s', host='%s', port=%d, id=%llu\n",
               dev->name, dev->host, dev->port, dev->id);
        dev = list_get_next(dev);
    }

    mapper_db_signal sig = db->signals;
    printf("Registered signals:\n");
    while (sig) {
        printf("  name='%s':'%s', id=%llu ", sig->device->name,
               sig->name, sig->id);
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

    mapper_db_map map = db->maps;
    printf("Registered maps:\n");
    while (map) {
        printf("  id=%llu\n", map->id);
        if (map->num_sources == 1)
            print_map_slot("    source slot", -1, &map->sources[0]);
        else {
            for (i = 0; i < map->num_sources; i++)
                print_map_slot("    source slot", i, &map->sources[i]);
        }
        print_map_slot("    destination slot", -1, &map->destination);
        printf("    mode='%s'\n", mapper_get_mode_type_string(map->mode));
        printf("    expression='%s'\n", map->expression);
        printf("    muted='%s'\n", map->muted ? "yes" : "no");
        map = list_get_next(map);
    }
#endif
}
