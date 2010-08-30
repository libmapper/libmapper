
#include <string.h>
#include <stdarg.h>

#include "mapper_internal.h"

/*! The global list of devices. */
mapper_db_device g_db_registered_devices = NULL;

/*! The global list of inputs. */
mapper_db_signal g_db_registered_inputs = NULL;

/*! The global list of outputs. */
mapper_db_signal g_db_registered_outputs = NULL;

/*! The global list of mappings. */
mapper_db_mapping g_db_registered_mappings = NULL;

/*! The global list of links. */
mapper_db_link g_db_registered_links = NULL;

/*! A list of function and context pointers. */
typedef struct _fptr_list {
    void *f;
    void *context;
    struct _fptr_list *next;
} *fptr_list;

/*! A global list of device record callbacks. */
fptr_list g_db_device_callbacks = NULL;

/*! A global list of signal record callbacks. */
fptr_list g_db_signal_callbacks = NULL;

/*! A global list of mapping record callbacks. */
fptr_list g_db_mapping_callbacks = NULL;

/*! A global list of link record callbacks. */
fptr_list g_db_link_callbacks = NULL;

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
    query_type_t query_type;
    struct _query_info *query_context;
    int data[0]; // stub
}  list_header_t;

/*! Reserve memory for a list item.  Reserves an extra pointer at the
 *  beginning of the structure to allow for a list pointer. */
static list_header_t* list_new_item(size_t size)
{
    // make sure the compiler is doing what we think it's doing with
    // that zero-length array
    die_unless(sizeof(list_header_t) == sizeof(void*)*3 + sizeof(query_type_t),
               "unexpected size for list_header_t");

    size += sizeof(list_header_t);
    list_header_t *lh = malloc(size);
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
    return (list_header_t*)(data - sizeof(list_header_t));
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
    list_header_t *lh = (list_header_t*)malloc(sizeof(list_header_t));
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
    int needed = sizeof(list_header_t);
    if (src->query_context)
        needed += src->query_context->size;
    else
        needed += sizeof(unsigned int);

    die_unless(size >= needed,
               "not enough memory provided to save query context.\n");

    memcpy(mem, src, sizeof(list_header_t));
    if (src->query_context)
        memcpy(mem+sizeof(list_header_t), src->query_context,
               src->query_context->size);
    else
        ((query_info_t*)(mem+sizeof(list_header_t)))->size = 0;
}

static void restore_query_single_context(list_header_t *dest, void *mem)
{
    query_info_t *qi = mem + sizeof(list_header_t);
    if (qi->size > 0)
        die_unless(dest->query_context
                   && dest->query_context->size >= qi->size,
                   "not enough memory provided to restore query context.\n");

    void *qc = dest->query_context;
    memcpy(dest, mem, sizeof(list_header_t));
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
    memcpy(result, lh, sizeof(list_header_t));

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
    memcpy(src, dest, sizeof(list_header_t));
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

static void update_signal_value_if_arg(mapper_message_t *params,
                                       mapper_msg_param_t field,
                                       char sigtype,
                                       mapper_signal_value_t **pv)
{
    lo_arg **a = mapper_msg_get_param(params, field);
    const char *type = mapper_msg_get_type(params, field);

    if (!a || !(*a))
        return;

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
        *pv = realloc(*pv, sizeof(mapper_signal_value_t));
        **pv = v;
    }
}

static void update_string_if_different(char **pdest_str,
                                       const char *src_str)
{
    if (!(*pdest_str) || strcmp((*pdest_str), src_str)) {
        char *str = (char*) realloc((void*)(*pdest_str),
                                    strlen(src_str)+1);
        strcpy(str, src_str);
        (*pdest_str) = str;
    }
}

static void update_string_if_arg(char **pdest_str,
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
    }
}

static void update_char_if_arg(char *pdest_char,
                               mapper_message_t *params,
                               mapper_msg_param_t field)
{
    lo_arg **a = mapper_msg_get_param(params, field);
    const char *type = mapper_msg_get_type(params, field);

    if (a && (*a) && (type[0]=='s' || type[0]=='S'))
        (*pdest_char) = (&(*a)->s)[0];
    else if (a && (*a) && type[0]=='c')
        (*pdest_char) = (*a)->c;
}

/**** Device records ****/

/*! Update information about a given device record based on message
 *  parameters. */
static void update_device_record_params(mapper_db_device reg,
                                        const char *name,
                                        mapper_message_t *params)
{
    update_string_if_different(&reg->name, name);

    update_string_if_arg(&reg->host, params, AT_IP);

    lo_arg **a_port = mapper_msg_get_param(params, AT_PORT);
    const char *t_port = mapper_msg_get_type(params, AT_PORT);

    if (a_port && t_port[0]=='i')
        reg->port = (*a_port)->i;

    lo_arg **a_canAlias = mapper_msg_get_param(params, AT_CANALIAS);
    const char *t_canAlias = mapper_msg_get_type(params, AT_CANALIAS);

    if (a_canAlias && t_canAlias[0]=='s')
        reg->canAlias = strcmp("no", &(*a_canAlias)->s)!=0;

}

int mapper_db_add_or_update_device_params(const char *name,
                                          mapper_message_t *params)
{
    mapper_db_device reg = mapper_db_get_device_by_name(name);
    int rc = 0;

    if (!reg) {
        reg = (mapper_db_device) list_new_item(sizeof(*reg));
        rc = 1;

        list_prepend_item(reg, (void**)&g_db_registered_devices);
    }

    if (reg) {
        update_device_record_params(reg, name, params);
        
        fptr_list cb = g_db_device_callbacks;
        while (cb) {
            device_callback_func *f = cb->f;
            f(reg, rc ? MDB_NEW : MDB_MODIFY, cb->context);
            cb = cb->next;
        }
    }

    return rc;
}

void mapper_db_remove_device(const char *name)
{
    mapper_db_device dev = mapper_db_get_device_by_name(name);
    if (dev) {
        fptr_list cb = g_db_device_callbacks;
        while (cb) {
            device_callback_func *f = cb->f;
            f(dev, MDB_REMOVE, cb->context);
            cb = cb->next;
        }
    }

    list_remove_item(dev, (void**)&g_db_registered_devices);

    // TODO: also remove mappings and signals for this device
}

mapper_db_device mapper_db_get_device_by_name(const char *name)
{

    mapper_db_device reg = g_db_registered_devices;
    while (reg) {
        if (strcmp(reg->name, name)==0)
            return reg;
        reg = list_get_next(reg);
    }
    return 0;

}

mapper_db_device *mapper_db_get_all_devices()
{
    if (!g_db_registered_devices)
        return 0;

    list_header_t *lh = list_get_header_by_data(g_db_registered_devices);

    die_unless(lh->self == &lh->data,
               "bad self pointer in list structure");

    return (mapper_db_device*)&lh->self;
}

static int cmp_query_match_device_by_name(void *context_data,
                                          mapper_db_device dev)
{
    const char *device_pattern = (const char*)context_data;
    return strstr(dev->name, device_pattern)!=0;
}

mapper_db_device *mapper_db_match_device_by_name(char *str)
{
    mapper_db_device dev = g_db_registered_devices;
    if (!dev)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_match_device_by_name, str, 0);

    lh->self = dev;

    if (cmp_query_match_device_by_name((void*)str, dev))
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

void mapper_db_dump()
{
    mapper_db_device reg = g_db_registered_devices;
    trace("Registered devices:\n");
    while (reg) {
        trace("  name=%s, host=%s, port=%d, canAlias=%d\n",
              reg->name, reg->host,
              reg->port, reg->canAlias);
        reg = list_get_next(reg);
    }

    mapper_db_signal sig = g_db_registered_inputs;
    trace("Registered inputs:\n");
    while (sig) {
        trace("  name=%s%s\n",
              sig->device_name, sig->name);
        sig = list_get_next(sig);
    }

    sig = g_db_registered_outputs;
    trace("Registered outputs:\n");
    while (sig) {
        trace("  name=%s%s\n",
              sig->device_name, sig->name);
        sig = list_get_next(sig);
    }

    mapper_db_mapping map = g_db_registered_mappings;
    trace("Registered mappings:\n");
    while (map) {
        char r[1024] = "(";
        if (map->range.known & MAPPING_RANGE_SRC_MIN)
            sprintf(r+strlen(r), "%f, ", map->range.src_min);
        else
            strcat(r, "-, ");
        if (map->range.known & MAPPING_RANGE_SRC_MAX)
            sprintf(r+strlen(r), "%f, ", map->range.src_max);
        else
            strcat(r, "-, ");
        if (map->range.known & MAPPING_RANGE_DEST_MIN)
            sprintf(r+strlen(r), "%f, ", map->range.dest_min);
        else
            strcat(r, "-, ");
        if (map->range.known & MAPPING_RANGE_DEST_MAX)
            sprintf(r+strlen(r), "%f", map->range.dest_max);
        else
            strcat(r, "-");
        strcat(r, ")");
        trace("  src_name=%s, dest_name=%s,\n"
              "      src_type=%d, dest_type=%d,\n"
              "      clip_upper=%s, clip_lower=%s,\n"
              "      range=%s,\n"
              "      expression=%s, scaling=%s, muted=%d\n",
              map->src_name, map->dest_name, map->src_type,
              map->dest_type,
              mapper_get_clipping_type_string(map->clip_upper),
              mapper_get_clipping_type_string(map->clip_lower),
              r, map->expression,
              mapper_get_scaling_type_string(map->scaling),
              map->muted);
        map = list_get_next(map);
    }

    mapper_db_link link = g_db_registered_links;
    trace("Registered links:\n");
    while (link) {
        trace("  source=%s, dest=%s\n",
              link->src_name, link->dest_name);
        link = list_get_next(link);
    }
}

void mapper_db_add_device_callback(device_callback_func *f, void *user)
{
    add_callback(&g_db_device_callbacks, f, user);
}

void mapper_db_remove_device_callback(device_callback_func *f, void *user)
{
    remove_callback(&g_db_device_callbacks, f, user);
}

/**** Signals ****/

/*! Update information about a given signal record based on message
 *  parameters. */
static void update_signal_record_params(mapper_db_signal sig,
                                        const char *name,
                                        const char *device_name,
                                        mapper_message_t *params)
{
    update_string_if_different((char**)&sig->name, name);
    update_string_if_different((char**)&sig->device_name, device_name);

    update_char_if_arg(&sig->type, params, AT_TYPE);

    update_string_if_arg((char**)&sig->unit, params, AT_UNITS);

    update_signal_value_if_arg(params, AT_MAXIMUM,
                               sig->type, &sig->maximum);

    update_signal_value_if_arg(params, AT_MINIMUM,
                               sig->type, &sig->minimum);
}

int mapper_db_add_or_update_signal_params(const char *name,
                                          const char *device_name,
                                          int is_output,
                                          mapper_message_t *params)
{
    mapper_db_signal sig;
    mapper_db_signal *psig = 0;

    if (is_output)
        psig = mapper_db_match_outputs_by_device_name(device_name, name);
    else
        psig = mapper_db_match_inputs_by_device_name(device_name, name);
    // TODO: actually we want to find the exact signal

    if (psig)
        sig = *psig;
    else {
        sig = (mapper_db_signal) list_new_item(sizeof(mapper_db_signal_t));

        // Defaults
        sig->length = 1;
    }

    if (sig) {
        update_signal_record_params(sig, name, device_name, params);

        if (!psig)
            list_prepend_item(sig, (void**)(is_output
                                            ? &g_db_registered_outputs
                                            : &g_db_registered_inputs));

        fptr_list cb = g_db_signal_callbacks;
        while (cb) {
            signal_callback_func *f = cb->f;
            f(sig, psig ? MDB_MODIFY : MDB_NEW, cb->context);
            cb = cb->next;
        }
    }

    if (psig)
        mapper_db_signal_done(psig);

    return 1;
}

void mapper_db_add_signal_callback(signal_callback_func *f, void *user)
{
    add_callback(&g_db_signal_callbacks, f, user);
}

void mapper_db_remove_signal_callback(signal_callback_func *f, void *user)
{
    remove_callback(&g_db_signal_callbacks, f, user);
}

static int cmp_query_signal_exact_device_name(void *context_data,
                                              mapper_db_signal sig)
{
    const char *device_name = (const char*)context_data;
    return strcmp(device_name, sig->device_name)==0;
}

mapper_db_signal_t **mapper_db_get_inputs_by_device_name(
    const char *device_name)
{
    mapper_db_signal sig = g_db_registered_inputs;
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
    const char *device_name)
{
    mapper_db_signal sig = g_db_registered_outputs;
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
    const char *device_name, const char *input_pattern)
{
    mapper_db_signal sig = g_db_registered_inputs;
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
    const char *device_name, char const *output_pattern)
{
    mapper_db_signal sig = g_db_registered_outputs;
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

/**** Mapping records ****/

/*! Update information about a given mapping record based on message
 *  parameters. */
static void update_mapping_record_params(mapper_db_mapping map,
                                        const char *src_name,
                                        const char *dest_name,
                                        mapper_message_t *params)
{
    update_string_if_different(&map->src_name, src_name);
    update_string_if_different(&map->dest_name, dest_name);

    // TODO: Unhandled fields --
    /* char src_type; */
    /* char dest_type; */

    mapper_clipping_type clip;
    clip = mapper_msg_get_clipping(params, AT_CLIPMAX);
    if (clip!=-1)
        map->clip_upper = clip;

    clip = mapper_msg_get_clipping(params, AT_CLIPMIN);
    if (clip!=-1)
        map->clip_lower = clip;

    lo_arg **a_range = mapper_msg_get_param(params, AT_RANGE);
    const char *t_range = mapper_msg_get_type(params, AT_RANGE);

    // TODO: currently ignoring strings such as 'invert', '-'
    if (a_range && (*a_range)) {
        if (t_range[0] == 'f') {
            map->range.src_min = (*a_range)->f;
            map->range.known |= MAPPING_RANGE_SRC_MIN;
        } else if (t_range[0] == 'i') {
            map->range.src_min = (float)(*a_range)->i;
            map->range.known |= MAPPING_RANGE_SRC_MIN;
        }
        if (t_range[1] == 'f') {
            map->range.src_max = (*a_range)->f;
            map->range.known |= MAPPING_RANGE_SRC_MAX;
        } else if (t_range[1] == 'i') {
            map->range.src_min = (float)(*a_range)->i;
            map->range.known |= MAPPING_RANGE_SRC_MAX;
        }
        if (t_range[2] == 'f') {
            map->range.dest_min = (*a_range)->f;
            map->range.known |= MAPPING_RANGE_DEST_MIN;
        } else if (t_range[2] == 'i') {
            map->range.src_min = (float)(*a_range)->i;
            map->range.known |= MAPPING_RANGE_DEST_MIN;
        }
        if (t_range[3] == 'f') {
            map->range.dest_max = (*a_range)->f;
            map->range.known |= MAPPING_RANGE_DEST_MAX;
        } else if (t_range[3] == 'i') {
            map->range.src_min = (float)(*a_range)->i;
            map->range.known |= MAPPING_RANGE_DEST_MAX;
        }
    }

    update_string_if_arg(&map->expression, params, AT_EXPRESSION);

    mapper_scaling_type scaling = mapper_msg_get_scaling(params);
    if (scaling!=-1)
        map->scaling = scaling;

    // TODO
    /* int muted; */
}

int mapper_db_add_or_update_mapping_params(const char *src_name,
                                           const char *dest_name,
                                           mapper_message_t *params)
{
    mapper_db_mapping map;
    int found = 0;

    // TODO: get device by names

    if (!found)
        map = (mapper_db_mapping) list_new_item(sizeof(mapper_db_mapping_t));

    if (map) {
        update_mapping_record_params(map, src_name, dest_name, params);

        if (!found)
            list_prepend_item(map, (void**)&g_db_registered_mappings);

        fptr_list cb = g_db_mapping_callbacks;
        while (cb) {
            mapping_callback_func *f = cb->f;
            f(map, found ? MDB_MODIFY : MDB_NEW, cb->context);
            cb = cb->next;
        }
    }

    return found;
}

mapper_db_mapping_t **mapper_db_get_all_mappings()
{
    if (!g_db_registered_mappings)
        return 0;

    list_header_t *lh = list_get_header_by_data(g_db_registered_mappings);

    die_unless(lh->self == &lh->data,
               "bad self pointer in list structure");

    return (mapper_db_mapping*)&lh->self;
}

void mapper_db_add_mapping_callback(mapping_callback_func *f, void *user)
{
    add_callback(&g_db_mapping_callbacks, f, user);
}

void mapper_db_remove_mapping_callback(mapping_callback_func *f, void *user)
{
    remove_callback(&g_db_mapping_callbacks, f, user);
}

static int cmp_query_get_mappings_by_input_name(void *context_data,
                                                mapper_db_mapping map)
{
    const char *inputname = (const char*)context_data;
    const char *mapinputname = map->src_name+1;
    while (*mapinputname && *mapinputname != '/')  // find the signal name
        mapinputname++;
    mapinputname++;
    return strcmp(mapinputname, inputname)==0;
}

mapper_db_mapping_t **mapper_db_get_mappings_by_input_name(
    const char *input_name)
{
    mapper_db_mapping mapping = g_db_registered_mappings;
    if (!mapping)
        return 0;

    // query skips first '/' in the name if it is provided
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_mappings_by_input_name,
        input_name[0]=='/' ? input_name+1 : input_name, 0);

    lh->self = mapping;

    if (cmp_query_get_mappings_by_input_name(
            &lh->query_context->data, mapping))
        return (mapper_db_mapping*)&lh->self;

    return (mapper_db_mapping*)dynamic_query_continuation(lh);
}

static int cmp_query_get_mappings_by_device_and_input_name(
    void *context_data, mapper_db_mapping map)
{
    const char *name = (const char*) context_data;
    return strcmp(map->src_name, name)==0;
}

mapper_db_mapping_t **mapper_db_get_mappings_by_device_and_input_name(
    const char *device_name, const char *input_name)
{
    mapper_db_mapping mapping = g_db_registered_mappings;
    if (!mapping)
        return 0;

    // query skips first '/' in both names if it is provided
    char name[1024];
    snprintf(name, 1024, "/%s/%s",
             device_name[0]=='/' ? device_name+1 : device_name,
             input_name[0]=='/' ? input_name+1 : input_name);

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_mappings_by_device_and_input_name,
        name, 0);

    lh->self = mapping;

    if (cmp_query_get_mappings_by_device_and_input_name(
            &lh->query_context->data, mapping))
        return (mapper_db_mapping*)&lh->self;

    return (mapper_db_mapping*)dynamic_query_continuation(lh);
}

static int cmp_query_get_mappings_by_output_name(void *context_data,
                                                mapper_db_mapping map)
{
    const char *outputname = (const char*)context_data;
    const char *mapoutputname = map->dest_name+1;
    while (*mapoutputname && *mapoutputname != '/')  // find the signal name
        mapoutputname++;
    mapoutputname++;
    return strcmp(mapoutputname, outputname)==0;
}

mapper_db_mapping_t **mapper_db_get_mappings_by_output_name(
    const char *output_name)
{
    mapper_db_mapping mapping = g_db_registered_mappings;
    if (!mapping)
        return 0;

    // query skips first '/' in the name if it is provided
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_mappings_by_output_name,
        output_name[0]=='/' ? output_name+1 : output_name, 0);

    lh->self = mapping;

    if (cmp_query_get_mappings_by_output_name(
            &lh->query_context->data, mapping))
        return (mapper_db_mapping*)&lh->self;

    return (mapper_db_mapping*)dynamic_query_continuation(lh);
}

static int cmp_query_get_mappings_by_device_and_output_name(
    void *context_data, mapper_db_mapping map)
{
    const char *name = (const char*) context_data;
    return strcmp(map->dest_name, name)==0;
}

mapper_db_mapping_t **mapper_db_get_mappings_by_device_and_output_name(
    const char *device_name, const char *output_name)
{
    mapper_db_mapping mapping = g_db_registered_mappings;
    if (!mapping)
        return 0;

    char name[1024];
    snprintf(name, 1024, "/%s/%s",
             device_name[0]=='/' ? device_name+1 : device_name,
             output_name[0]=='/' ? output_name+1 : output_name);

    // query skips first '/' in both names if it is provided
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_mappings_by_device_and_output_name,
        name, 0);

    lh->self = mapping;

    if (cmp_query_get_mappings_by_device_and_output_name(
            &lh->query_context->data, mapping))
        return (mapper_db_mapping*)&lh->self;

    return (mapper_db_mapping*)dynamic_query_continuation(lh);
}

static int cmp_query_get_mappings_by_device_and_signal_names(
    void *context_data, mapper_db_mapping map)
{
    const char *src_name = (const char*) context_data;
    if (strcmp(map->src_name, src_name)!=0)
        return 0;
    const char *dest_name = src_name + strlen(src_name) + 1;
    return strcmp(map->dest_name, dest_name)==0;
}

mapper_db_mapping_t **mapper_db_get_mappings_by_device_and_signal_names(
    const char *input_device_name,  const char *input_name,
    const char *output_device_name, const char *output_name)
{
    mapper_db_mapping mapping = g_db_registered_mappings;
    if (!mapping)
        return 0;

    char inname[1024];
    snprintf(inname, 1024, "/%s/%s",
             (input_device_name[0]=='/'
              ? input_device_name+1
              : input_device_name),
             input_name[0]=='/' ? input_name+1 : input_name);

    char outname[1024];
    snprintf(outname, 1024, "/%s/%s",
             (output_device_name[0]=='/'
              ? output_device_name+1
              : output_device_name),
             output_name[0]=='/' ? output_name+1 : output_name);

    // query skips first '/' in both names if it is provided
    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_mappings_by_device_and_signal_names,
        inname, outname, 0);

    lh->self = mapping;

    if (cmp_query_get_mappings_by_device_and_signal_names(
            &lh->query_context->data, mapping))
        return (mapper_db_mapping*)&lh->self;

    return (mapper_db_mapping*)dynamic_query_continuation(lh);
}

static int cmp_get_mappings_by_signal_queries(void *context_data,
                                              mapper_db_mapping map)
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
        if (strncmp((*srcsig)->device_name, map->src_name, devnamelen)==0
            && strcmp((*srcsig)->name, map->src_name+devnamelen)==0)
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
        if (strncmp((*destsig)->device_name, map->dest_name, devnamelen)==0
            && strcmp((*destsig)->name, map->dest_name+devnamelen)==0)
            break;
        destsig = mapper_db_signal_next(destsig);
    }
    restore_query_single_context(qsig->lh_dest_head, ctx_backup);
    mapper_db_signal_done(destsig);
    if (!destsig)
        return 0;

    return 1;
}

mapper_db_mapping_t **mapper_db_get_mappings_by_signal_queries(
    mapper_db_signal_t **inputs, mapper_db_signal_t **outputs)
{
    mapper_db_mapping maps = g_db_registered_mappings;
    if (!maps)
        return 0;

    if (!(inputs && outputs))
        return 0;

    query_info_t *qi = (query_info_t*)
        malloc(sizeof(query_info_t) + sizeof(src_dest_queries_t));

    qi->size = sizeof(query_info_t) + sizeof(src_dest_queries_t);
    qi->query_compare =
        (query_compare_func_t*) cmp_get_mappings_by_signal_queries;
    qi->query_free = free_query_src_dest_queries;

    src_dest_queries_t *qdata = (src_dest_queries_t*)&qi->data;

    qdata->lh_src_head = list_get_header_by_self(inputs);
    qdata->lh_dest_head = list_get_header_by_self(outputs);

    list_header_t *lh = (list_header_t*) malloc(sizeof(list_header_t));
    lh->self = maps;
    lh->next = dynamic_query_continuation;
    lh->query_type = QUERY_DYNAMIC;
    lh->query_context = qi;

    if (cmp_get_mappings_by_signal_queries(
            &lh->query_context->data, maps))
        return (mapper_db_mapping*)&lh->self;

    return (mapper_db_mapping*)dynamic_query_continuation(lh);
}

mapper_db_mapping_t **mapper_db_mapping_next(mapper_db_mapping_t** m)
{
    return (mapper_db_mapping*) iterator_next((void**)m);
}

void mapper_db_mapping_done(mapper_db_mapping_t **d)
{
    if (!d) return;
    list_header_t *lh = list_get_header_by_data(*d);
    if (lh->query_type == QUERY_DYNAMIC
        && lh->query_context->query_free)
        lh->query_context->query_free(lh);
}

/**** Link records ****/

/*! Update information about a given link record based on message
 *  parameters. */
static void update_link_record_params(mapper_db_link link,
                                      const char *src_name,
                                      const char *dest_name,
                                      mapper_message_t *params)
{
    update_string_if_different(&link->src_name, src_name);
    update_string_if_different(&link->dest_name, dest_name);
}

int mapper_db_add_or_update_link_params(const char *src_name,
                                        const char *dest_name,
                                        mapper_message_t *params)
{
    mapper_db_link link;
    int rc = 0;

    link = mapper_db_get_link_by_source_dest_names(src_name, dest_name);

    if (!link) {
        link = (mapper_db_link) list_new_item(sizeof(mapper_db_link_t));
        rc = 1;
    }

    if (link) {
        update_link_record_params(link, src_name, dest_name, params);

        if (rc)
            list_prepend_item(link, (void**)&g_db_registered_links);

        fptr_list cb = g_db_link_callbacks;
        while (cb) {
            link_callback_func *f = cb->f;
            f(link, rc ? MDB_NEW : MDB_MODIFY, cb->context);
            cb = cb->next;
        }
    }
    else {
        trace("couldn't find or create link in "
              "mapper_db_add_or_update_link_params()\n");
    }

    return rc;
}

void mapper_db_add_link_callback(link_callback_func *f, void *user)
{
    add_callback(&g_db_link_callbacks, f, user);
}

void mapper_db_remove_link_callback(link_callback_func *f, void *user)
{
    remove_callback(&g_db_link_callbacks, f, user);
}

mapper_db_link_t **mapper_db_get_all_links()
{
    if (!g_db_registered_links)
        return 0;

    list_header_t *lh = list_get_header_by_data(g_db_registered_links);

    die_unless(lh->self == &lh->data,
               "bad self pointer in list structure");

    return (mapper_db_link*)&lh->self;
}

mapper_db_link mapper_db_get_link_by_source_dest_names(
    const char *source_device_name, const char *dest_device_name)
{
    mapper_db_link link = g_db_registered_links;
    while (link) {
        if (strcmp(link->src_name, source_device_name)==0
            && strcmp(link->dest_name, dest_device_name)==0)
            break;
        link = list_get_next(link);
    }
    return link;
}

static int cmp_query_get_links_by_source_device_name(void *context_data,
                                                     mapper_db_link link)
{
    const char *src = (const char*)context_data;
    return strcmp(link->src_name, src)==0;
}

mapper_db_link_t **mapper_db_get_links_by_source_device_name(
    const char *source_device_name)
{
    mapper_db_link link = g_db_registered_links;
    if (!link)
        return 0;

    list_header_t *lh = construct_query_context_from_strings(
        (query_compare_func_t*)cmp_query_get_links_by_source_device_name,
        source_device_name, 0);

    lh->self = link;

    if (cmp_query_get_links_by_source_device_name(
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
    const char *dest_device_name)
{
    mapper_db_link link = g_db_registered_links;
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

static int cmp_get_links_by_source_dest_devices(void *context_data,
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

mapper_db_link_t **mapper_db_get_links_by_source_dest_devices(
    mapper_db_device_t **source_device_list,
    mapper_db_device_t **dest_device_list)
{
    mapper_db_link link = g_db_registered_links;
    if (!link)
        return 0;

    query_info_t *qi = (query_info_t*)
        malloc(sizeof(query_info_t) + sizeof(src_dest_queries_t));

    qi->size = sizeof(query_info_t) + sizeof(src_dest_queries_t);
    qi->query_compare =
        (query_compare_func_t*) cmp_get_links_by_source_dest_devices;
    qi->query_free = free_query_src_dest_queries;

    src_dest_queries_t *qdata = (src_dest_queries_t*)&qi->data;

    qdata->lh_src_head = list_get_header_by_self(source_device_list);
    qdata->lh_dest_head = list_get_header_by_self(dest_device_list);

    list_header_t *lh = (list_header_t*) malloc(sizeof(list_header_t));
    lh->self = link;
    lh->next = dynamic_query_continuation;
    lh->query_type = QUERY_DYNAMIC;
    lh->query_context = qi;

    if (cmp_get_links_by_source_dest_devices(
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
