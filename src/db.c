
#include <string.h>

#include "mapper_internal.h"

/*! Initialization of the global list of local devices. */
mapper_db_device g_db_registered_devices = NULL;

/*! A list of function and context pointers. */
typedef struct _fptr_list {
    void *f;
    void *context;
    struct _fptr_list *next;
} *fptr_list;

/*! A global list of device record callbacks. */
fptr_list g_db_device_callbacks = NULL;

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
    void *query_context;
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

/*! Update information about a given device record based on message
 *  parameters. */
static void update_device_record_params(mapper_db_device reg,
                                        const char *name,
                                        mapper_message_t *params)
{
    lo_arg **a_host = mapper_msg_get_param(params, AT_IP);
    const char *t_host = mapper_msg_get_type(params, AT_IP);

    lo_arg **a_port = mapper_msg_get_param(params, AT_PORT);
    const char *t_port = mapper_msg_get_type(params, AT_PORT);

    lo_arg **a_canAlias = mapper_msg_get_param(params, AT_CANALIAS);
    const char *t_canAlias = mapper_msg_get_type(params, AT_CANALIAS);

    if (!reg->name || strcmp(reg->name, name)) {
        reg->name = (char*) realloc(reg->name, strlen(name)+1);
        strcpy(reg->name, name);
    }

    if (a_host && (*a_host) && t_host[0]=='s'
        && (!reg->host || strcmp(reg->host, &(*a_host)->s)))
    {
        reg->host = (char*) realloc(reg->host, strlen(&(*a_host)->s)+1);
        strcpy(reg->host, &(*a_host)->s);
    }

    if (a_port && t_port[0]=='i')
        reg->port = (*a_port)->i;

    if (a_canAlias && t_canAlias[0]=='s')
        reg->canAlias = strcmp("no", &(*a_canAlias)->s)!=0;

}

int mapper_db_add_or_update_params(const char *name,
                                   mapper_message_t *params)
{
    mapper_db_device reg = mapper_db_find_device_by_name(name);
    int rc = 0;

    if (!reg) {
        reg = (mapper_db_device) list_new_item(sizeof(*reg));
        rc = 1;

        list_set_next(reg, g_db_registered_devices);
        g_db_registered_devices = reg;
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

mapper_db_device mapper_db_find_device_by_name(const char *name)
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

mapper_db_device *mapper_db_get_devices_matching_continuation(list_header_t *lh)
{
    mapper_db_device reg = list_get_header_by_data(lh->self)->next;
    while (reg) {
        if (strstr(reg->name, lh->query_context))
            break;
        reg = list_get_header_by_data(reg)->next;
    }

    if (reg) {
        lh->self = reg;
        return (mapper_db_device*)&lh->self;
    }

    // Clean up
    free(lh->query_context);
    free(lh);
    return 0;
}

mapper_db_device *mapper_db_get_devices_matching(char *str)
{
    if (!g_db_registered_devices)
        return 0;

    mapper_db_device reg = g_db_registered_devices;
    while (reg) {
        if (strstr(reg->name, str))
            break;
        list_header_t *lh = list_get_header_by_data(reg);
        reg = lh->next;
    }

    if (!reg)
        return 0;  // No matches

    /* Here we treat next as a pointer to a continuation function, so
     * we can return items from the database computed lazily.  The
     * context is simply the string to match.  In the future, it might
     * point to the results of a SQL query for example. */

    list_header_t *result = (list_header_t*)malloc(sizeof(list_header_t));
    memset(result, 0, sizeof(list_header_t));
    result->self = reg;
    result->next = mapper_db_get_devices_matching_continuation;
    result->query_context = strdup(str);
    result->query_type = QUERY_DYNAMIC;

    return (mapper_db_device*)&result->self;
}

mapper_db_device *mapper_db_device_next(mapper_db_device* p)
{
    if (!p) {
        trace("bad pointer in mapper_db_device_next()\n");
        return 0;
    }

    if (!*p) {
        trace("pointer in mapper_db_device_next() points nowhere\n");
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

        return (mapper_db_device*)&lh2->self;
    }
    else if (lh1->query_type == QUERY_DYNAMIC)
    {
        // Call the continuation.
        mapper_db_device *(*f) (list_header_t*) = lh1->next;
        return f(lh1);
    }
    return 0;
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
}

void mapper_db_add_device_callback(device_callback_func *f, void *user)
{
    fptr_list cb = (fptr_list)malloc(sizeof(struct _fptr_list));
    cb->f = f;
    cb->context = user;
    cb->next = g_db_device_callbacks;
    g_db_device_callbacks = cb;
}

void mapper_db_remove_device_callback(device_callback_func *f, void *user)
{
    fptr_list cb = g_db_device_callbacks;
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
        g_db_device_callbacks = cb->next;

    free(cb);
}
