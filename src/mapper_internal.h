
#ifndef __MAPPER_INTERNAL_H__
#define __MAPPER_INTERNAL_H__

#include "types_internal.h"
#include <mapper/mapper.h>
#include <string.h>

/* Structs that refer to things defined in mapper.h are declared here instead
   of in types_internal.h */

/**** Signals ****/

#define MAPPER_MAX_VECTOR_LEN 128

/*! Get the full OSC name of a signal, including device name prefix.
 *  \param sig  The signal value to query.
 *  \param name A string to accept the name.
 *  \param len  The length of string pointed to by name.
 *  \return     The number of characters used, or 0 if error.  Note that in some
 *              cases the name may not be available. */
int mapper_signal_full_name(mapper_signal sig, char *name, int len);

int mapper_signal_set_from_msg(mapper_signal sig, mapper_msg msg);

void mapper_signal_update_timing_stats(mapper_signal sig, float diff);

/**** Devices ****/

int mapper_device_set_from_msg(mapper_device dev, mapper_msg msg);

void mapper_device_manage_subscriber(mapper_device dev, lo_address address,
                                     int flags, int timeout_seconds,
                                     int revision);

/*! Return the list of inter-device links associated with a given device.
 *  \param dev          Device record query.
 *  \param dir          The direction of the link relative to the given device.
 *  \return             A double-pointer to the first item in the list of
 *                      results. Use mapper_link_query_next() to iterate. */
mapper_object *mapper_device_get_links(mapper_device dev, mapper_direction dir);

/**** Networking ****/

void mapper_network_add_device(mapper_network net, mapper_device dev);

void mapper_network_remove_device_methods(mapper_network net, mapper_device dev);

void mapper_network_poll(mapper_network net);

void mapper_network_init(mapper_network n, const char *iface, const char *group,
                         int port);

void mapper_network_bus(mapper_network net);

void mapper_network_mesh(mapper_network net, lo_address address);

void mapper_network_subscribers(mapper_network net, int type);

void mapper_network_add_msg(mapper_network net, const char *str,
                            network_msg_t cmd, lo_message msg);

void mapper_network_send(mapper_network net);

void mapper_network_free_msgs(mapper_network net);

void mapper_network_free(mapper_network n);

/***** Device *****/

void init_device_prop_table(mapper_device dev);

void mapper_device_on_registered(mapper_device dev);

void mapper_device_add_signal_methods(mapper_device dev, mapper_signal sig);

void mapper_device_remove_signal_methods(mapper_device dev, mapper_signal sig);

void mapper_device_on_num_inst_changed(mapper_device d, mapper_signal s, int size);

void mapper_device_route_signal(mapper_device dev, mapper_signal sig,
                                int inst_idx, const void *val, int count,
                                mapper_time_t t);

int mapper_device_route_query(mapper_device dev, mapper_signal sig,
                              mapper_time_t t);

void mapper_device_release_scope(mapper_device dev, const char *scope);

void mapper_device_on_id_and_ordinal(mapper_device dev,
                                     mapper_allocated_t *resource);

mapper_idmap mapper_device_add_idmap(mapper_device dev, int group,
                                     mapper_id local, mapper_id global);

void mapper_device_remove_idmap(mapper_device dev, int group, mapper_idmap map);

mapper_idmap mapper_device_idmap_by_local(mapper_device dev, int group,
                                          mapper_id local);

mapper_idmap mapper_device_idmap_by_global(mapper_device dev, int group,
                                           mapper_id global);

const char *mapper_device_get_name(mapper_device dev);

void mapper_device_send_state(mapper_device dev, network_msg_t cmd);

/*! Find information for a registered link.
 *  \param dev          Device record to query.
 *  \param remote_dev   Remote device.
 *  \return             Information about the link, or zero if not found. */
mapper_link mapper_device_get_link_by_remote_device(mapper_device dev,
                                                    mapper_device remote_dev);

/*! Look up information for a registered object using its unique id.
 *  \param g            The graph to query.
 *  \param type         The type of object to return.
 *  \param id           Unique id identifying the object to find in the graph.
 *  \return             Information about the object, or zero if not found. */
mapper_object mapper_graph_get_object(mapper_graph g, mapper_object_type type,
                                      mapper_id id);

/*! Find information for a registered device.
 *  \param g            The graph to query.
 *  \param name         Name of the device to find in the graph.
 *  \return             Information about the device, or zero if not found. */
mapper_device mapper_graph_get_device_by_name(mapper_graph g, const char *name);

/*! Find information for a registered signal.
 *  \param dev          The device to query.
 *  \param sig_name     Name of the signal to find in the graph.
 *  \return             Information about the signal, or zero if not found. */
mapper_signal mapper_device_get_signal_by_name(mapper_device dev,
                                               const char *sig_name);

/***** Router *****/

void mapper_router_remove_sig(mapper_router r, mapper_router_sig rs);

void mapper_router_num_inst_changed(mapper_router r, mapper_signal s, int size);

/*! For a given signal instance, calculate mapping outputs and forward to
 *  destinations. */
void mapper_router_process_sig(mapper_router r, mapper_signal s, int inst_idx,
                               const void *val, int count, mapper_time_t t);

int mapper_router_send_query(mapper_router r, mapper_signal s, mapper_time_t t);

void mapper_router_add_map(mapper_router r, mapper_map m);

void mapper_router_remove_link(mapper_router r, mapper_link l);

int mapper_router_remove_map(mapper_router r, mapper_map m);

/*! Find a mapping in a router by local signal and remote signal name. */
mapper_map mapper_router_map_out(mapper_router r, mapper_signal local_src,
                                 int num_src, const char **src_names,
                                 const char *dst_name);

mapper_map mapper_router_map_in(mapper_router r, mapper_signal local_dst,
                                int num_src, const char **src_names);

mapper_map mapper_router_map_by_id(mapper_router r, mapper_signal local_sig,
                                   mapper_id id, mapper_direction dir);

mapper_slot mapper_router_slot(mapper_router r, mapper_signal s, int slot_num);

int mapper_router_loop_check(mapper_router r, mapper_signal local_sig,
                             int num_remotes, const char **remotes);

/**** Signals ****/

/*! Create a signal structure and fill it with provided arguments.  Values and
 *  strings pointed to by this call will be copied. Signals should be freed by
 *  mapper_signal_free() only if they are not registered with a device.  For
 *  min, max, and value, if type=MAPPER_FLOAT, should be float*, if
 *  type=MAPPER_INT32, then should be int*, or if type=MAPPER_DOUBLE, should be
 *  double*.
 *  \param name         The name of the signal, starting with '/'.
 *  \param length       The length of the signal vector, or 1 for a scalar.
 *  \param type         The type fo the signal value.
 *  \param is_output    The direction of the signal, 1 for output, 0 for input.
 *  \param unit         The unit of the signal, or 0 for none.
 *  \param min          Pointer to a min value, or 0 for none.
 *  \param max          Pointer to a max value, or 0 for none.
 *  \param handler      Function to be called when the signal value is updated.
 */
mapper_signal mapper_signal_new(const char *name, int length, mapper_type type,
                                int is_output, const char *unit,
                                const void *min, const void *max,
                                mapper_signal_update_handler *handler);

/*! Free memory used by a mapper_signal. Call this only for signals
 *  that are not registered with a device. Registered signals will be
 *  freed by mapper_device_free().
 *  \param sig      The signal to free. */
void mapper_signal_free(mapper_signal sig);

/*! Coerce a signal instance value to a particular type and vector length and
 *  add it to a lo_message. */
void msg_add_coerced_signal_inst_val(lo_message m, mapper_signal sig,
                                     mapper_signal_inst si, int length,
                                     mapper_type type);

void mapper_signal_send_state(mapper_signal sig, network_msg_t cmd);

void mapper_signal_send_removed(mapper_signal sig);

/**** Instances ****/

/*! Find an active instance with the given instance id.
 *  \param sig       The signal owning the desired instance.
 *  \param global_id Globally unique id of this instance.
 *  \param flags     Bitflags indicating if search should include released instances.
 *  \return          The index of the retrieved signal instance, or -1 if no active
 *                   instances match the specified instance id map. */
int mapper_signal_find_inst_with_global_id(mapper_signal sig, mapper_id global_id,
                                           int flags);

/*! Fetch a reserved (preallocated) signal instance using an instance id,
 *  activating it if necessary.
 *  \param sig      The signal owning the desired instance.
 *  \param local_id The requested signal instance id.
 *  \param flags    Bitflags indicating if search should include released
 *                  instances.
 *  \param t        Time associated with this action.
 *  \return         The index of the retrieved signal instance, or -1 if no free
 *                  instances were available and allocation of a new instance
 *                  was unsuccessful according to the selected allocation
 *                  strategy. */
int mapper_signal_inst_with_local_id(mapper_signal sig, mapper_id local_id,
                                     int flags, mapper_time_t *t);

/*! Fetch a reserved (preallocated) signal instance using instance id map,
 *  activating it if necessary.
 *  \param sig       The signal owning the desired instance.
 *  \param global_id Globally unique id of this instance.
 *  \param flags     Bitflags indicating if search should include released instances.
 *  \param t         Time associated with this action.
 *  \return          The index of the retrieved signal instance, or NULL if no free
 *                   instances were available and allocation of a new instance
 *                   was unsuccessful according to the selected allocation
 *                   strategy. */
int mapper_signal_inst_with_global_id(mapper_signal sig, mapper_id global_id,
                                      int flags, mapper_time_t *t);

/*! Release a specific signal instance. */
void mapper_signal_release_inst_internal(mapper_signal sig, int inst_idx,
                                         mapper_time_t time);

/**** Links ****/

/*! Return the list of maps associated with a given link.
 *  \param link         The link to check.
 *  \return             A double-pointer to the first item in the list of
 *                      results. Use mapper_map_query_next() to iterate. */
mapper_object *mapper_link_get_maps(mapper_link link);


void mapper_link_init(mapper_link link);
void mapper_link_connect(mapper_link link, const char *host, int admin_port,
                      int data_port);
void mapper_link_free(mapper_link link);
void mapper_link_send(mapper_link link, network_msg_t cmd);
void mapper_link_start_queue(mapper_link link, mapper_time_t t);
void mapper_link_send_queue(mapper_link link, mapper_time_t t);

mapper_link mapper_graph_add_link(mapper_graph g, mapper_device dev1,
                                  mapper_device dev2);

/**** Maps ****/

void mapper_hist_realloc(mapper_hist hist, int hist_size, int samp_size,
                         int is_output);

mapper_slot mapper_map_get_slot_by_signal(mapper_map map, mapper_signal sig);

/*! Process the signal instance value according to mapping properties.
 *  The result of this operation should be sent to the destination.
 *  \param map          The mapping process to perform.
 *  \param slot         The source slot being updated.
 *  \param instance     Index of the signal instance to process.
 *  \param typestring   Pointer to a string to receive types.
 *  \return             Zero if the operation was muted, one if performed. */
int mapper_map_perform(mapper_map map, mapper_slot slot, int inst,
                       mapper_type *typestring);

lo_message mapper_map_build_msg(mapper_map map, mapper_slot slot,
                                const void *val, int length,
                                mapper_type *types, mapper_idmap idmap);

/*! Set a mapping's properties based on message parameters. */
int mapper_map_set_from_msg(mapper_map map, mapper_msg msg, int override);

/*! Helper for printing typed values.
 *  \param length       The vector length of the value.
 *  \param type         The value type.
 *  \param value        A pointer to the property value to print. */
void mapper_prop_print(int length, mapper_type type, const void *val);

mapper_property mapper_prop_from_string(const char *str);
const char *mapper_prop_string(mapper_property prop);

const char *mapper_prop_protocol_string(mapper_property prop);

const char *mapper_loc_string(mapper_location loc);

mapper_location mapper_loc_from_string(const char *string);

const char *mapper_protocol_string(mapper_protocol pro);

mapper_protocol mapper_protocol_from_string(const char *string);

int mapper_map_send_state(mapper_map map, int slot, network_msg_t cmd);

void mapper_map_init(mapper_map map);

void mapper_map_free(mapper_map map);

/**** Slot ****/

void mapper_slot_init(mapper_slot slot);

void mapper_slot_free(mapper_slot slot);

int mapper_slot_set_from_msg(mapper_slot slot, mapper_msg msg, int *status);

void mapper_slot_add_props_to_msg(lo_message msg, mapper_slot slot, int is_dest,
                                  int staged);

void mapper_slot_upgrade_extrema_memory(mapper_slot slot);

int mapper_slot_match_full_name(mapper_slot slot, const char *full_name);

/**** Graph ****/

/*! Add or update a device entry in the graph using parsed message parameters.
 *  \param g            The graph to operate on.
 *  \param dev_name     The name of the device.
 *  \param msg          The parsed message parameters containing new device
 *                      information.
 *  \return             Pointer to the device. */
mapper_device mapper_graph_add_or_update_device(mapper_graph g,
                                                const char *dev_name,
                                                mapper_msg msg);

/*! Add or update a signal entry in the graph using parsed message parameters.
 *  \param g            The graph to operate on.
 *  \param sig_name     The name of the signal.
 *  \param dev_name     The name of the device associated with this signal.
 *  \param msg          The parsed message parameters containing new signal
 *                      information.
 *  \return             Pointer to the signal. */
mapper_signal mapper_graph_add_or_update_signal(mapper_graph g,
                                                const char *sig_name,
                                                const char *dev_name,
                                                mapper_msg msg);

/*! Initialize an already-allocated mapper_signal structure. */
void mapper_signal_init(mapper_signal sig, mapper_direction dir, int num_inst,
                        const char *name, int length, mapper_type type,
                        const char *unit, const void *min, const void *max,
                        mapper_signal_update_handler *handler);

/*! Add or update a map entry in the graph using parsed message parameters.
 *  \param g            The graph to operate on.
 *  \param num_src      The number of source slots for this map
 *  \param src_names    The full name of the source signal.
 *  \param dst_name     The full name of the destination signal.
 *  \param msg          The parsed message parameters containing new
 *                      map information.
 *  \return             Pointer to the map. */
mapper_map mapper_graph_add_or_update_map(mapper_graph g, int num_src,
                                          const char **src_names,
                                          const char *dst_name,
                                          mapper_msg msg);

/*! Remove a device from the graph. */
void mapper_graph_remove_device(mapper_graph g, mapper_device dev,
                                mapper_record_event event, int quiet);

void mapper_graph_remove_signal(mapper_graph g, mapper_signal sig,
                                mapper_record_event event);

/*! Remove signals in the provided query. */
void mapper_graph_remove_signals_by_query(mapper_graph g, mapper_object *sigs,
                                          mapper_record_event event);

/*! Remove links in the provided query. */
void mapper_graph_remove_links_by_query(mapper_graph g, mapper_object *links,
                                        mapper_record_event event);

/*! Remove a specific link from the graph. */
void mapper_graph_remove_link(mapper_graph g, mapper_link link,
                              mapper_record_event event);

/*! Remove maps in the provided query. */
void mapper_graph_remove_maps_by_query(mapper_graph g, mapper_object *maps,
                                       mapper_record_event event);

/*! Remove a specific map from the graph. */
void mapper_graph_remove_map(mapper_graph g, mapper_map map,
                             mapper_record_event event);

/*! Print graph contents to the screen.  Useful for debugging, only works when
 *  compiled in debug mode. */
void mapper_graph_print(mapper_graph g);

void mapper_graph_remove_all_callbacks(mapper_graph g);

/*! Check device records for unresponsive devices. */
void mapper_graph_check_device_status(mapper_graph g, uint32_t now_sec);

/*! Flush device records for unresponsive devices. */
mapper_device mapper_graph_expired_device(mapper_graph g, uint32_t last_ping);

int mapper_graph_subscribed_by_dev_name(mapper_graph g, const char *name);

int mapper_graph_subscribed_by_sig_name(mapper_graph g, const char *name);

/**** Messages ****/
/*! Parse the device and signal names from an OSC path. */
int mapper_parse_names(const char *string, char **devnameptr, char **signameptr);

/*! Parse a message based on an OSC path and named properties.
 *  \param argc     Number of arguments in the argv array.
 *  \param types    String containing message parameter types.
 *  \param argv     Vector of lo_arg structures.
 *  \return         A mapper_msg structure. Should be freed when done using
 *                  mapper_msg_free. */
mapper_msg mapper_msg_parse_props(int argc, const mapper_type *types,
                                  lo_arg **argv);

void mapper_msg_free(mapper_msg msg);

/*! Look up the value of a message parameter by symbolic identifier.
 *  \param msg      Structure containing parameter info.
 *  \param prop     Symbolic identifier of the property to look for.
 *  \return         Pointer to mapper_msg_atom, or zero if not found. */
mapper_msg_atom mapper_msg_prop(mapper_msg msg, mapper_property prop);

void mapper_msg_add_typed_val(lo_message msg, int length, mapper_type type,
                              const void *val);

/*! Prepare a lo_message for sending based on a map struct. */
const char *mapper_map_prepare_msg(mapper_map map, lo_message msg, int slot_idx);

/*! Helper for setting property value from different lo_arg types. */
int set_coerced_val(int src_len, mapper_type src_type, const void *src_val,
                    int dst_len, mapper_type dst_type, void *dst_val);

/*! Helper for setting property value from different double type. */
void propval_set_double(void *to, mapper_type type, int idx, double from);

/*! Helper for getting a double from different property value types. */
double propval_get_double(const void *val, mapper_type type, int idx);

/**** Expression parser/evaluator ****/

mapper_expr mapper_expr_new_from_string(const char *str, int num_inputs,
                                        const mapper_type *input_types,
                                        const int *input_vector_lengths,
                                        mapper_type output_type,
                                        int output_vector_length);

int mapper_expr_in_hist_size(mapper_expr expr, int idx);

int mapper_expr_out_hist_size(mapper_expr expr);

int mapper_expr_num_vars(mapper_expr expr);

int mapper_expr_var_hist_size(mapper_expr expr, int idx);

int mapper_expr_var_vector_length(mapper_expr expr, int idx);

int mapper_expr_src_muted(mapper_expr expr, int idx);

#ifdef DEBUG
void printexpr(const char*, mapper_expr);
#endif

int mapper_expr_eval(mapper_expr expr, mapper_hist *srcs, mapper_hist *expr_vars,
                     mapper_hist result, mapper_time_t *t, mapper_type *types);

int mapper_expr_constant_output(mapper_expr expr);

int mapper_expr_num_input_slots(mapper_expr expr);

void mapper_expr_free(mapper_expr expr);

/**** String tables ****/

/*! Create a new string table. */
mapper_table mapper_table_new(void);

/*! Clear the contents of a string table.
 * \param tab Table to free. */
void mapper_table_clear(mapper_table tab);

/*! Free a string table.
 * \param tab Table to free. */
void mapper_table_free(mapper_table tab);

/*! Get the number of records stored in a table. */
int mapper_table_num_records(mapper_table tab);

/*! Look up a value in a table.  Returns 0 if found, 1 if not found,
 *  and fills in value if found. */
mapper_table_record_t *mapper_table_record(mapper_table tab,
                                           mapper_property prop,
                                           const char *name);

int mapper_table_get_prop_by_name(mapper_table tab, const char *name, int *len,
                                  mapper_type *type, const void **val);

int mapper_table_get_prop_by_index(mapper_table tab, mapper_property prop,
                                   const char **name, int *len,
                                   mapper_type *type, const void **val);

/*! Remove a key-value pair from a table (by index or name). */
int mapper_table_remove_record(mapper_table tab, mapper_property prop,
                               const char *name, int flags);

/*! Update a value in a table if the key already exists, or add it otherwise.
 *  Returns 0 if no add took place.  Sorts the table before exiting.
 *  \param tab          Table to update.
 *  \param prop         Index to store.
 *  \param name         Key to store if not already indexed.
 *  \param type         OSC type of value to add.
 *  \param args         Value(s) to add
 *  \param length       Number of OSC argument in array
 *  \param flags        MAPPER_LOCAL_MODIFY or MAPPER_REMOTE_MODIFY,
 *                      or MAPPER_NON_MODIFABLE.
 *  \return             The number of table values added or modified. */
int mapper_table_set_record(mapper_table tab, mapper_property prop,
                            const char *name, int length, mapper_type type,
                            const void *args, int flags);

/*! Sync an existing value with a table. Records added using this method must
 *  be added in alphabetical order since table_sort() will not be called.
 *  Key and value will not be copied by the table, and will not be freed when
 *  the table is cleared or deleted. */
void mapper_table_link(mapper_table tab, mapper_property prop, int length,
                       mapper_type type, void *val, int flags);

/*! Add a typed OSC argument from a mapper_msg to a string table.
 *  \param tab      Table to update.
 *  \param atom     Message atom containing pointers to message key and value.
 *  \return         The number of table values added or modified. */
int mapper_table_set_record_from_atom(mapper_table tab, mapper_msg_atom atom,
                                      int flags);

int mapper_table_set_from_msg(mapper_table tab, mapper_msg msg, int flags);

#ifdef DEBUG
/*! Print a table of OSC values. */
void mapper_table_print_record(mapper_table_record_t *tab);
void mapper_table_print(mapper_table tab);
#endif

/*! Add arguments contained in a string table to a lo_message */
void mapper_table_add_to_msg(mapper_table tab, mapper_table updates,
                             lo_message msg);

/*! Clears and frees memory for removed records. This is not performed
 *  automatically by mapper_table_remove_record() in order to allow record
 *  removal to propagate to subscribed graph instances and peer devices. */
void mapper_table_clear_empty_records(mapper_table tab);

/**** Lists ****/

void *mapper_list_from_data(const void *data);

void *mapper_list_add_item(void **list, size_t size);

void mapper_list_remove_item(void **list, void *item);

void mapper_list_free_item(void *item);

void **mapper_list_new_query(const void *list, const void *f,
                             const mapper_type *types, ...);

void **mapper_list_filter(void **list, const void *compare_func,
                          const char *types, ...);

void **mapper_list_union(void **list1, void **list2);

void **mapper_list_intersection(void **list1, void **list2);

void **mapper_list_difference(void **list1, void **list2);

void *mapper_list_get_index(void **list, int idx);

void **mapper_list_next(void **list);

void **mapper_list_copy(void **list);

int mapper_list_length(void **list);

void mapper_list_free(void **list);

/**** Time ****/

/*! Get the current time. */
double mapper_get_current_time(void);

/*! Return the difference in seconds between two mapper_times.
 *  \param minuend      The minuend.
 *  \param subtrahend   The subtrahend.
 *  \return             The difference a-b in seconds. */
double mapper_time_difference(mapper_time_t minuend, mapper_time_t subtrahend);

/**** Debug macros ****/

/*! Debug tracer */
#ifdef DEBUG
#ifdef __GNUC__
#include <stdio.h>
#include <assert.h>
#define trace(...) { printf("-- " __VA_ARGS__); }
#define trace_graph(...)  { printf("\x1B[31m-- <graph>\x1B[0m " __VA_ARGS__);}
#define trace_dev(DEV, ...)                                             \
{                                                                       \
    if (DEV->local && DEV->local->registered)                           \
        printf("\x1B[32m-- <device '%s'>\x1B[0m ", mapper_device_get_name(DEV));     \
    else                                                                \
        printf("\x1B[32m-- <device '%s.?'::%p>\x1B[0m ", DEV->identifier, DEV);  \
    printf(__VA_ARGS__);                                                \
}
#define trace_net(...)  { printf("\x1B[33m-- <network>\x1B[0m  " __VA_ARGS__);}
#define die_unless(a, ...) { if (!(a)) { printf("-- " __VA_ARGS__); assert(a); } }
#else
static void trace(...)
{
};
static void die_unless(...) {};
#endif
#else
#ifdef __GNUC__
#define trace(...) {}
#define trace_graph(...) {}
#define trace_dev(...) {}
#define trace_net(...) {}
#define die_unless(...) {}
#else
static void trace(...)
{
};
static void die_unless(...) {};
#endif
#endif

/*! Helper to find size of signal value types. */
inline static int mapper_type_size(mapper_type type)
{
    switch (type) {
        case MAPPER_INT32: return sizeof(int);
        case MAPPER_BOOL:
        case 'T':
        case 'F': return sizeof(int);
        case MAPPER_FLOAT: return sizeof(float);
        case MAPPER_DOUBLE: return sizeof(double);
        case MAPPER_STRING: return sizeof(char*);
        case MAPPER_INT64: return sizeof(int64_t);
        case MAPPER_TIME: return sizeof(mapper_time_t);
        case MAPPER_CHAR: return sizeof(char);
        default:
            die_unless(0, "Unknown type '%c' in mapper_type_size().\n", type);
            return 0;
    }
}

/*! Helper to find the size in bytes of a signal's full vector. */
inline static size_t mapper_signal_vector_bytes(mapper_signal sig)
{
    return mapper_type_size(sig->type) * sig->len;
}

/*! Helper to find the pointer to the current value in a mapper_hist_t. */
inline static void* mapper_hist_val_ptr(mapper_hist_t h)
{
    return h.val + h.pos * h.len * mapper_type_size(h.type);
}

/*! Helper to find the pointer to the current time in a mapper_hist_t. */
inline static void* mapper_hist_time_ptr(mapper_hist_t h)
{
    return &h.time[h.pos];
}

/*! Helper to check if a type character is valid. */
inline static int check_signal_type(mapper_type type)
{
    return (type != MAPPER_INT32 && type != MAPPER_FLOAT && type != MAPPER_DOUBLE);
}

/*! Helper to check if a type character is valid. */
inline static int check_signal_length(int length)
{
    return (length < 1 || length > MAPPER_MAX_VECTOR_LEN);
}

/*! Helper to check if bitfields match completely. */
inline static int bitmatch(unsigned int a, unsigned int b)
{
    return (a & b) == b;
}

/*! Helper to check if type is a number. */
inline static int type_is_num(mapper_type type)
{
    switch (type) {
        case MAPPER_INT32:
        case MAPPER_FLOAT:
        case MAPPER_DOUBLE:
            return 1;
        default:    return 0;
    }
}

/*! Helper to check if type is a boolean. */
inline static int type_is_bool(mapper_type type)
{
    switch (type) {
        case 'T':
        case 'F':   return 1;
        default:    return 0;
    }
}

/*! Helper to check if type is a string. */
inline static int type_is_str(mapper_type type)
{
    return type == MAPPER_STRING;
}

/*! Helper to check if type is a string or void* */
inline static int type_is_ptr(mapper_type type)
{
    switch (type) {
        case MAPPER_STRING:
        case MAPPER_PTR:
            return 1;
        default:    return 0;
    }
}

/*! Helper to check if data type matches, but allowing 'T' and 'F' for bool. */
inline static int type_match(const mapper_type l, const mapper_type r)
{
    return (l == r) || (strchr("bTF", l) && strchr("bTF", r));
}

/*! Helper to remove a leading slash '/' from a string. */
inline static const char *skip_slash(const char *string)
{
    return string + (string && string[0]=='/');
}

/*! Helper to check if a time has the MAPPER_NOW value */
inline static int time_is_now(mapper_time t)
{
    return (memcmp(t, &MAPPER_NOW, sizeof(mapper_time_t)) == 0);
}

#endif // __MAPPER_INTERNAL_H__
