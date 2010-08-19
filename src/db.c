
#include <string.h>

#include "mapper_internal.h"

/*! Initialization of the global list of local devices. */
mapper_db_device g_db_registered_devices = NULL;
mapper_db_signal g_db_registered_inputs = NULL;
mapper_db_signal g_db_registered_outputs = NULL;

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

/*! Prepend an item to the beginning of a list. */
static void* list_prepend_item(void *item, void **list)
{
    list_set_next(item, *list);
    *list = item;
    return item;
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

mapper_db_device *mapper_db_match_device_by_name_continuation(
    list_header_t *lh)
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

mapper_db_device *mapper_db_match_device_by_name(char *str)
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
    result->next = mapper_db_match_device_by_name_continuation;
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

static void update_signal_value_if_arg(lo_arg **a, const char *type,
                                       char sigtype,
                                       mapper_signal_value_t **pv)
{
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

/*! Update information about a given signal record based on message
 *  parameters. */
static void update_signal_record_params(mapper_db_signal sig,
                                        const char *name,
                                        const char *device_name,
                                        mapper_message_t *params)
{
    lo_arg **a_type = mapper_msg_get_param(params, AT_TYPE);
    const char *t_type = mapper_msg_get_type(params, AT_TYPE);

    lo_arg **a_units = mapper_msg_get_param(params, AT_UNITS);
    const char *t_units = mapper_msg_get_type(params, AT_UNITS);

    lo_arg **a_minimum = mapper_msg_get_param(params, AT_MINIMUM);
    const char *t_minimum = mapper_msg_get_type(params, AT_MINIMUM);

    lo_arg **a_maximum = mapper_msg_get_param(params, AT_MAXIMUM);
    const char *t_maximum = mapper_msg_get_type(params, AT_MAXIMUM);

    if (!sig->name || strcmp(sig->name, name)) {
        char *str = (char*) realloc((void*)sig->name, strlen(name)+1);
        strcpy(str, name);
        sig->name = str;
    }

    if (!sig->device_name || strcmp(sig->device_name, device_name)) {
        char *str = (char*) realloc((void*)sig->device_name,
                                    strlen(device_name)+1);
        strcpy(str, device_name);
        sig->device_name = str;
    }

    if (a_type && (*a_type) && t_type[0]=='s')
        sig->type = (&(*a_type)->s)[0];
    else if (a_type && (*a_type) && t_type[0]=='c')
        sig->type = (*a_type)->c;

    if (a_units && (*a_units) && t_units[0]=='s'
        && (!sig->unit || strcmp(sig->unit, &(*a_units)->s)))
    {
        char *str = (char*) realloc((void*)sig->unit,
                                    strlen(&(*a_units)->s)+1);
        strcpy(str, &(*a_units)->s);
        sig->unit = str;
    }

    update_signal_value_if_arg(a_maximum, t_maximum,
                               sig->type, &sig->maximum);

    update_signal_value_if_arg(a_minimum, t_minimum,
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
    trace("mapper_db_add_signal_callback(signal_callback_func *f,"
          " void *user() not yet implemented.\n");
}

void mapper_db_remove_signal_callback(signal_callback_func *f, void *user)
{
    trace("mapper_db_remove_signal_callback(signal_callback_func *f,"
          " void *user() not yet implemented.\n");
}

static mapper_db_signal_t **
mapper_db_get_signals_by_device_name_continuation(list_header_t *lh)
{
    const char *device_name = (const char*)lh->query_context;

    mapper_db_signal sig = list_get_header_by_data(lh->self)->next;
    while (sig) {
        if (strcmp(device_name, sig->device_name)==0)
            break;
        sig = list_get_header_by_data(sig)->next;
    }

    if (sig) {
        lh->self = sig;
        return (mapper_db_signal*)&lh->self;
    }

    // Clean up
    free(lh->query_context);
    free(lh);
    return 0;
}

mapper_db_signal_t **mapper_db_get_inputs_by_device_name(
    const char *device_name)
{
    mapper_db_signal sig = g_db_registered_inputs;
    while (sig) {
        if (strcmp(device_name, sig->device_name)==0)
        {
            list_header_t *lh = (list_header_t*)malloc(sizeof(list_header_t));
            lh->next = mapper_db_get_signals_by_device_name_continuation;
            lh->self = sig;
            lh->query_type = QUERY_DYNAMIC;
            lh->query_context = strdup(device_name);
            return (mapper_db_signal*)&lh->self;
        }
        sig = list_get_next(sig);
    }
    return 0;
}

mapper_db_signal_t **mapper_db_get_outputs_by_device_name(
    const char *device_name)
{
    mapper_db_signal sig = g_db_registered_outputs;
    while (sig) {
        if (strcmp(device_name, sig->device_name)==0)
        {
            list_header_t *lh = (list_header_t*)malloc(sizeof(list_header_t));
            lh->next = mapper_db_get_signals_by_device_name_continuation;
            lh->self = sig;
            lh->query_type = QUERY_DYNAMIC;
            lh->query_context = strdup(device_name);
            return (mapper_db_signal*)&lh->self;
        }
        sig = list_get_next(sig);
    }
    return 0;
}

static mapper_db_signal_t **
mapper_db_match_signals_by_device_name_continuation(list_header_t *lh)
{
    const char *device_name = (const char*)lh->query_context;
    const char *signal_pattern = ((const char*)lh->query_context
                                  + strlen(device_name) + 1);

    mapper_db_signal sig = list_get_header_by_data(lh->self)->next;
    while (sig) {
        if (strcmp(device_name, sig->device_name)==0
            && strstr(sig->name, signal_pattern))
            break;
        sig = list_get_header_by_data(sig)->next;
    }

    if (sig) {
        lh->self = sig;
        return (mapper_db_signal*)&lh->self;
    }

    // Clean up
    free(lh->query_context);
    free(lh);
    return 0;
}

mapper_db_signal_t **mapper_db_match_inputs_by_device_name(
    const char *device_name, const char *input_pattern)
{
    mapper_db_signal sig = g_db_registered_inputs;
    while (sig) {
        if (strcmp(device_name, sig->device_name)==0
            && strstr(sig->name, input_pattern))
        {
            list_header_t *lh = (list_header_t*)malloc(sizeof(list_header_t));
            lh->next = mapper_db_match_signals_by_device_name_continuation;
            lh->self = sig;
            lh->query_type = QUERY_DYNAMIC;
            lh->query_context = malloc(strlen(device_name)
                                       + strlen(input_pattern) + 2);
            strcpy(lh->query_context, device_name);
            strcpy(lh->query_context+strlen(device_name)+1, input_pattern);
            return (mapper_db_signal*)&lh->self;
        }
        sig = list_get_next(sig);
    }
    return 0;
}

mapper_db_signal_t **mapper_db_match_outputs_by_device_name(
    const char *device_name, char const *output_pattern)
{
    mapper_db_signal sig = g_db_registered_outputs;
    while (sig) {
        if (strcmp(device_name, sig->device_name)==0
            && strstr(sig->name, output_pattern))
        {
            list_header_t *lh = (list_header_t*)malloc(sizeof(list_header_t));
            lh->next = mapper_db_match_signals_by_device_name_continuation;
            lh->self = sig;
            lh->query_type = QUERY_DYNAMIC;
            lh->query_context = malloc(strlen(device_name)
                                       + strlen(output_pattern) + 2);
            strcpy(lh->query_context, device_name);
            strcpy(lh->query_context+strlen(device_name)+1, output_pattern);
            return (mapper_db_signal*)&lh->self;
        }
        sig = list_get_next(sig);
    }
    return 0;
}

mapper_db_signal_t **mapper_db_signal_next(mapper_db_signal_t** p)
{
    if (!p) {
        trace("bad pointer in mapper_db_signal_next()\n");
        return 0;
    }

    if (!*p) {
        trace("pointer in mapper_db_signal_next() points nowhere\n");
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

        return (mapper_db_signal*)&lh2->self;
    }
    else if (lh1->query_type == QUERY_DYNAMIC)
    {
        // Call the continuation.
        mapper_db_signal *(*f) (list_header_t*) = lh1->next;
        return f(lh1);
    }
    return 0;
}

void mapper_db_signal_done(mapper_db_signal_t **s)
{
    if (!s) return;
    list_header_t *lh = list_get_header_by_data(*s);
    if (lh->query_type == QUERY_DYNAMIC) {
        if (lh->query_context)
            free(lh->query_context);
        free(lh);
    }
}

void mapper_db_add_mapping_callback(mapping_callback_func *f, void *user)
{
    trace("mapper_db_add_mapping_callback(mapping_callback_func *f,"
          " void *user() not yet implemented.\n");
}

void mapper_db_remove_mapping_callback(mapping_callback_func *f, void *user)
{
    trace("mapper_db_remove_mapping_callback(mapping_callback_func *f,"
          " void *user() not yet implemented.\n");
}

mapper_db_mapping_t **mapper_db_get_mappings_by_input_name(
    const char *input_name)
{
    trace("mapper_db_get_mappings_by_input_name()"
          " not yet implemented.\n");
    return 0;
}

mapper_db_mapping_t **mapper_db_get_mappings_by_device_and_input_name(
    const char *device_name, const char *input_name)
{
    trace("mapper_db_get_mappings_by_device_and_input_name()"
          " not yet implemented.\n");
    return 0;
}

mapper_db_mapping_t **mapper_db_get_mappings_by_output_name(
    const char *output_name)
{
    trace("mapper_db_get_mappings_by_output_name()"
          " not yet implemented.\n");
    return 0;
}

mapper_db_mapping_t **mapper_db_get_mappings_by_device_and_output_name(
    const char *device_name, const char *output_name)
{
    trace("mapper_db_get_mappings_by_device_and_output_name()"
          " not yet implemented.\n");
    return 0;
}

mapper_db_mapping_t **mapper_db_get_mappings_by_devices_and_signals(
    mapper_db_device_t **input_devices,  mapper_db_signal_t **inputs,
    mapper_db_device_t **output_devices, mapper_db_signal_t **outputs)
{
    trace("mapper_db_get_mappings_by_devices_and_signals()"
          " not yet implemented.\n");
    return 0;
}

mapper_db_mapping_t **mapper_db_mapping_next(mapper_db_mapping_t** m)
{
    trace("mapper_db_mapping_next(mapper_db_mapping_t** m()"
          " not yet implemented.\n");
    return 0;
}

void mapper_db_add_link_callback(link_callback_func *f, void *user)
{
    trace("mapper_db_add_link_callback(link_callback_func *f, void *user()"
          " not yet implemented.\n");
}

void mapper_db_remove_link_callback(link_callback_func *f, void *user)
{
    trace("mapper_db_remove_link_callback(link_callback_func *f, void *user()"
          " not yet implemented.\n");
}

mapper_db_link_t **mapper_db_get_links_by_input_device_name(
    const char *input_device_name)
{
    trace("mapper_db_get_links_by_input_device_name()"
          " not yet implemented.\n");
    return 0;
}

mapper_db_link_t **mapper_db_get_links_by_output_device_name(
    const char *output_device_name)
{
    trace("mapper_db_get_links_by_output_device_name()"
          " not yet implemented.\n");
    return 0;
}

mapper_db_link_t **mapper_db_get_links_by_input_output_devices(
    mapper_db_device_t **input_device_list,
    mapper_db_device_t **output_device_list)
{
    trace("mapper_db_get_links_by_input_output_devices()"
          " not yet implemented.\n");
    return 0;
}
