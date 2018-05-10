
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

int mapper_signal_set_from_message(mapper_signal sig, mapper_message msg);

/**** Devices ****/

int mapper_device_set_from_message(mapper_device dev, mapper_message msg);

void mapper_device_manage_subscriber(mapper_device dev, lo_address address,
                                     int flags, int timeout_seconds,
                                     int revision);

/**** Networking ****/

mapper_database mapper_network_add_database(mapper_network net);

void mapper_network_remove_database(mapper_network net);

void mapper_network_add_device(mapper_network net, mapper_device dev);

void mapper_network_remove_device(mapper_network net, mapper_device dev);

void mapper_network_poll(mapper_network net);

int mapper_network_init(mapper_network net);

void mapper_network_set_dest_bus(mapper_network net);

void mapper_network_set_dest_mesh(mapper_network net, lo_address address);

void mapper_network_set_dest_subscribers(mapper_network net, int type);

void mapper_network_add_message(mapper_network net, const char *str,
                                network_message_t cmd, lo_message msg);

void mapper_network_send(mapper_network net);

void mapper_network_free_messages(mapper_network net);

/***** Device *****/

void init_device_prop_table(mapper_device dev);

void mapper_device_registered(mapper_device dev);

void mapper_device_add_signal_methods(mapper_device dev, mapper_signal sig);

void mapper_device_remove_signal_methods(mapper_device dev, mapper_signal sig);

void mapper_device_num_instances_changed(mapper_device dev, mapper_signal sig,
                                         int size);

void mapper_device_route_signal(mapper_device dev, mapper_signal sig,
                                int instance_index, const void *value,
                                int count, mapper_timetag_t tt);

int mapper_device_route_query(mapper_device dev, mapper_signal sig,
                              mapper_timetag_t tt);

void mapper_device_release_scope(mapper_device dev, const char *scope);

void mapper_device_start_servers(mapper_device dev, int port);

void mapper_device_on_id_and_ordinal(mapper_device dev,
                                     mapper_allocated_t *resource);

mapper_id_map mapper_device_add_instance_id_map(mapper_device dev,
                                                int group_index,
                                                mapper_id local_id,
                                                mapper_id global_id);

void mapper_device_remove_instance_id_map(mapper_device dev, int group_index,
                                          mapper_id_map map);

mapper_id_map mapper_device_find_instance_id_map_by_local(mapper_device dev,
                                                          int group_index,
                                                          mapper_id local_id);

mapper_id_map mapper_device_find_instance_id_map_by_global(mapper_device dev,
                                                           int group_index,
                                                           mapper_id global_id);

const char *mapper_device_name(mapper_device dev);

void mapper_device_send_state(mapper_device dev, network_message_t cmd);

/***** Router *****/

void mapper_router_remove_signal(mapper_router router, mapper_router_signal rs);

void mapper_router_num_instances_changed(mapper_router r,
                                         mapper_signal sig,
                                         int size);

/*! For a given signal instance, calculate mapping outputs and forward to
 *  destinations. */
void mapper_router_process_signal(mapper_router r, mapper_signal sig,
                                  int instance_index, const void *value,
                                  int count, mapper_timetag_t timetag);

int mapper_router_send_query(mapper_router router,
                             mapper_signal sig,
                             mapper_timetag_t tt);

void mapper_router_add_map(mapper_router router, mapper_map map);

void mapper_router_remove_link(mapper_router router, mapper_link link);

int mapper_router_remove_map(mapper_router router, mapper_map map);

/*! Find a mapping in a router by local signal and remote signal name. */
mapper_map mapper_router_outgoing_map(mapper_router router,
                                      mapper_signal local_src,
                                      int num_sources,
                                      const char **src_names,
                                      const char *dest_name);

mapper_map mapper_router_incoming_map(mapper_router router,
                                      mapper_signal local_dest,
                                      int num_sources,
                                      const char **src_names);

mapper_map mapper_router_map_by_id(mapper_router router, mapper_signal local_sig,
                                   mapper_id id, mapper_direction dir);

mapper_slot mapper_router_slot(mapper_router router, mapper_signal signal,
                               int slot_number);

int mapper_router_loop_check(mapper_router router, mapper_signal local_sig,
                             int num_remotes, const char **remotes);

/**** Signals ****/

/*! Create a signal structure and fill it with provided arguments.  Values and
 *  strings pointed to by this call (except user_data) will be copied. Signals
 *  should be freed by mapper_signal_free() only if they are not registered with
 *  a device.  For minimum, maximum, and value, if type='f', should be float*,
 *  if type='i', then should be int*, or if type='d', should be double*.
 *  \param name         The name of the signal, starting with '/'.
 *  \param length       The length of the signal vector, or 1 for a scalar.
 *  \param type         The type fo the signal value.
 *  \param is_output    The direction of the signal, 1 for output, 0 for input.
 *  \param unit         The unit of the signal, or 0 for none.
 *  \param minimum      Pointer to a minimum value, or 0 for none.
 *  \param maximum      Pointer to a maximum value, or 0 for none.
 *  \param handler      Function to be called when the signal value is updated.
 *  \param user_data    User context pointer to be passed to handler. */
mapper_signal mapper_signal_new(const char *name, int length, char type,
                                int is_output, const char *unit,
                                const void *minimum, const void *maximum,
                                mapper_signal_update_handler *handler,
                                const void *user_data);

/*! Free memory used by a mapper_signal. Call this only for signals
 *  that are not registered with a device. Registered signals will be
 *  freed by mapper_device_free().
 *  \param sig      The signal to free. */
void mapper_signal_free(mapper_signal sig);

/*! Coerce a signal instance value to a particular type and vector length and
 *  add it to a lo_message. */
void message_add_coerced_signal_instance_value(lo_message m, mapper_signal sig,
                                               mapper_signal_instance si,
                                               int length, char type);

void mapper_signal_send_state(mapper_signal sig, network_message_t cmd);

void mapper_signal_send_removed(mapper_signal sig);

/**** Instances ****/

/*! Find an active instance with the given instance ID.
 *  \param sig       The signal owning the desired instance.
 *  \param global_id Globally unique id of this instance.
 *  \param flags     Bitflags indicating if search should include released instances.
 *  \return          The index of the retrieved signal instance, or -1 if no active
 *                   instances match the specified instance ID map. */
int mapper_signal_find_instance_with_global_id(mapper_signal sig,
                                               mapper_id global_id, int flags);

/*! Fetch a reserved (preallocated) signal instance using an instance id,
 *  activating it if necessary.
 *  \param sig      The signal owning the desired instance.
 *  \param local_id The requested signal instance ID.
 *  \param flags    Bitflags indicating if search should include released
 *                  instances.
 *  \param tt       Timetag associated with this action.
 *  \return         The index of the retrieved signal instance, or -1 if no free
 *                  instances were available and allocation of a new instance
 *                  was unsuccessful according to the selected allocation
 *                  strategy. */
int mapper_signal_instance_with_local_id(mapper_signal sig, mapper_id local_id,
                                         int flags, mapper_timetag_t *tt);

/*! Fetch a reserved (preallocated) signal instance using instance id map,
 *  activating it if necessary.
 *  \param sig       The signal owning the desired instance.
 *  \param global_id Globally unique id of this instance.
 *  \param flags     Bitflags indicating if search should include released instances.
 *  \param tt        Timetag associated with this action.
 *  \return          The index of the retrieved signal instance, or NULL if no free
 *                   instances were available and allocation of a new instance
 *                   was unsuccessful according to the selected allocation
 *                   strategy. */
int mapper_signal_instance_with_global_id(mapper_signal sig, mapper_id global_id,
                                          int flags, mapper_timetag_t *tt);

/*! Release a specific signal instance. */
void mapper_signal_instance_release_internal(mapper_signal sig,
                                             int instance_index,
                                             mapper_timetag_t timetag);

/**** Links ****/

void mapper_link_init(mapper_link link, int is_local);
void mapper_link_connect(mapper_link link, const char *host, int admin_port,
                      int data_port);
void mapper_link_free(mapper_link link);
int mapper_link_set_from_message(mapper_link link, mapper_message msg, int rev);
void mapper_link_send_state(mapper_link link, network_message_t cmd, int staged);
void mapper_link_start_queue(mapper_link link, mapper_timetag_t tt);
void mapper_link_send_queue(mapper_link link, mapper_timetag_t tt);

mapper_link mapper_database_add_or_update_link(mapper_database db,
                                               mapper_device dev1,
                                               mapper_device dev2,
                                               mapper_message msg);

int mapper_database_update_link(mapper_database db, mapper_link link,
                                mapper_device reporting_dev, mapper_message msg);

/**** Maps ****/

void mhist_realloc(mapper_history history, int history_size,
                   int sample_size, int is_output);

/*! Process the signal instance value according to mapping properties.
 *  The result of this operation should be sent to the destination.
 *  \param map          The mapping process to perform.
 *  \param slot         The source slot being updated.
 *  \param instance     Index of the signal instance to process.
 *  \param typestring   Pointer to a string to receive types.
 *  \return             Zero if the operation was muted, one if performed. */
int mapper_map_perform(mapper_map map, mapper_slot slot, int instance,
                       char *typestring);

int mapper_boundary_perform(mapper_history history, mapper_slot slot,
                            char *typestring);

lo_message mapper_map_build_message(mapper_map map, mapper_slot slot,
                                    const void *value, int length,
                                    char *typestring, mapper_id_map id_map);

/*! Set a mapping's properties based on message parameters. */
int mapper_map_set_from_message(mapper_map map, mapper_message msg,
                                int override);

/*! Helper for printing typed values.
 *  \param length       The vector length of the value.
 *  \param type         The value type.
 *  \param value        A pointer to the property value to print. */
void mapper_property_print(int length, char type, const void *value);

mapper_property_t mapper_property_from_string(const char *str);
const char *mapper_property_string(mapper_property_t prop);

const char *mapper_property_protocol_string(mapper_property_t prop);

const char *mapper_boundary_action_string(mapper_boundary_action bound);

mapper_boundary_action mapper_boundary_action_from_string(const char *string);

const char *mapper_location_string(mapper_location loc);

mapper_location mapper_location_from_string(const char *string);

const char *mapper_mode_string(mapper_mode mode);

mapper_mode mapper_mode_from_string(const char *string);

const char *mapper_protocol_string(mapper_protocol pro);

mapper_protocol mapper_protocol_from_string(const char *string);

int mapper_map_send_state(mapper_map map, int slot, network_message_t cmd);

void mapper_map_init(mapper_map map);

void mapper_map_free(mapper_map map);

/**** Slot ****/

void mapper_slot_init(mapper_slot slot);

void mapper_slot_free(mapper_slot slot);

int mapper_slot_set_from_message(mapper_slot slot, mapper_message msg,
                                 int *status);

void mapper_slot_add_props_to_message(lo_message msg, mapper_slot slot,
                                      int is_dest, int staged);

void mapper_slot_upgrade_extrema_memory(mapper_slot slot);

int mapper_slot_match_full_name(mapper_slot slot, const char *full_name);

/**** Database ****/

/**** Local device database ****/

/*! Add or update an entry in the device database using parsed message
 *  parameters.
 *  \param db           The database to operate on.
 *  \param device_name  The name of the device.
 *  \param msg          The parsed message parameters containing new device
 *                      information.
 *  \return             Pointer to the device database entry. */
mapper_device mapper_database_add_or_update_device(mapper_database db,
                                                   const char *device_name,
                                                   mapper_message msg);

/*! Add or update an entry in the signal database using parsed message
 *  parameters.
 *  \param db          The database to operate on.
 *  \param signal_name The name of the signal.
 *  \param device_name The name of the device associated with this signal.
 *  \param msg         The parsed message parameters containing new signal
 *                     information.
 *  \return            Pointer to the signal database entry. */
mapper_signal mapper_database_add_or_update_signal(mapper_database db,
                                                   const char *signal_name,
                                                   const char *device_name,
                                                   mapper_message msg);

/*! Initialize an already-allocated mapper_signal structure. */
void mapper_signal_init(mapper_signal sig, mapper_direction dir,
                        int num_instances, const char *name, int length,
                        char type, const char *unit,
                        const void *minimum, const void *maximum,
                        mapper_signal_update_handler *handler,
                        const void *user_data);

/*! Add or update an entry in the map database using parsed message parameters.
 *  \param db           The database to operate on.
 *  \param num_srcs     The number of source slots for this map
 *  \param src_names    The full name of the source signal.
 *  \param dest_name    The full name of the destination signal.
 *  \param msg          The parsed message parameters containing new
 *                      map information.
 *  \return             Pointer to the map database entry. */
mapper_map mapper_database_add_or_update_map(mapper_database db, int num_srcs,
                                             const char **src_names,
                                             const char *dest_name,
                                             mapper_message msg);

/*! Remove a device from the database. */
void mapper_database_remove_device(mapper_database db, mapper_device dev,
                                   mapper_record_event event, int quiet);

void mapper_database_remove_signal(mapper_database db, mapper_signal sig,
                                   mapper_record_event event);

/*! Remove signals in the provided query. */
void mapper_database_remove_signals_by_query(mapper_database db,
                                             mapper_signal *sigs,
                                             mapper_record_event event);

/*! Remove links in the provided query. */
void mapper_database_remove_links_by_query(mapper_database db,
                                           mapper_link *links,
                                           mapper_record_event event);

/*! Remove a specific link from the database. */
void mapper_database_remove_link(mapper_database db, mapper_link link,
                                 mapper_record_event event);

/*! Remove maps in the provided query. */
void mapper_database_remove_maps_by_query(mapper_database db,
                                          mapper_map *maps,
                                          mapper_record_event event);

/*! Remove a specific map from the database. */
void mapper_database_remove_map(mapper_database db, mapper_map map,
                                mapper_record_event event);

/*! Print device information database to the screen.  Useful for debugging, only
 *  works when compiled in debug mode. */
void mapper_database_print(mapper_database db);

void mapper_database_remove_all_callbacks(mapper_database db);

/*! Check device records for unresponsive devices. */
void mapper_database_check_device_status(mapper_database db, uint32_t now_sec);

/*! Flush device records for unresponsive devices. */
mapper_device mapper_database_expired_device(mapper_database db,
                                             uint32_t last_ping);

int mapper_database_subscribed_by_device_name(mapper_database db,
                                              const char *name);

int mapper_database_subscribed_by_signal_name(mapper_database db,
                                              const char *name);

/**** Messages ****/
/*! Parse the device and signal names from an OSC path. */
int mapper_parse_names(const char *string, char **devnameptr, char **signameptr);

/*! Parse a message based on an OSC path and named properties.
 *  \param argc     Number of arguments in the argv array.
 *  \param types    String containing message parameter types.
 *  \param argv     Vector of lo_arg structures.
 *  \return         A mapper_message structure. Should be freed when done using
 *                  mapper_message_free. */
mapper_message mapper_message_parse_properties(int argc, const char *types,
                                               lo_arg **argv);

void mapper_message_free(mapper_message msg);

/*! Look up the value of a message parameter by symbolic identifier.
 *  Note that it's possible the 'types' string will be longer
 *  than the actual contents pointed to; it is up to the usage of this
 *  function to ensure it only processes the number of parameters indicated
 *  by the 'length' property.
 *  \param msg      Structure containing parameter info.
 *  \param prop     Symbolic identifier of the property to look for.
 *  \return         Pointer to mapper_message_atom, or zero if not found. */
mapper_message_atom mapper_message_property(mapper_message msg,
                                            mapper_property_t prop);

/*! Helper to return the boundary action from a message property.
 *  \param msg      Structure containing parameter info.
 *  \param prop     Either MAPPER_BOUND_MIN or MAPPER_BOUND_MAX.
 *  \return         The boundary action, or -1 if not found. */
mapper_boundary_action mapper_message_boundary_action(mapper_message msg,
                                                      mapper_property_t prop);

void mapper_message_add_typed_value(lo_message msg, int length, char type,
                                    const void *value);

/*! Prepare a lo_message for sending based on a map struct. */
const char *mapper_map_prepare_message(mapper_map map, lo_message msg,
                                       int slot_index);

/*! Helper for setting property value from different lo_arg types. */
int set_coerced_value(void *dst, const void *src, int length, char dest_type,
                      char src_type);

/*! Helper for setting property value from different double type. */
void propval_set_double(void *to, char type, int index, double from);

/*! Helper for getting a double from different property value types. */
double propval_double(const void *value, char type, int index);

/**** Expression parser/evaluator ****/

mapper_expr mapper_expr_new_from_string(const char *str, int num_inputs,
                                        const char *input_types,
                                        const int *input_vector_lengths,
                                        char output_type,
                                        int output_vector_length);

int mapper_expr_input_history_size(mapper_expr expr, int index);

int mapper_expr_output_history_size(mapper_expr expr);

int mapper_expr_num_variables(mapper_expr expr);

int mapper_expr_variable_history_size(mapper_expr expr, int index);

int mapper_expr_variable_vector_length(mapper_expr expr, int index);

#ifdef DEBUG
void printexpr(const char*, mapper_expr);
#endif

int mapper_expr_evaluate(mapper_expr expr, mapper_history *sources,
                         mapper_history *expr_vars, mapper_history result,
                         mapper_timetag_t *tt, char *typestring);

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
                                           mapper_property_t index,
                                           const char *key);

int mapper_table_property(mapper_table tab, const char *name, int *length,
                          char *type, const void **value);

int mapper_table_property_index(mapper_table tab, unsigned int index,
                                const char **name, int *length, char *type,
                                const void **value);

/*! Remove a key-value pair from a table (by index or key). */
int mapper_table_remove_record(mapper_table tab, mapper_property_t index,
                               const char *key, int flags);

/*! Update a value in a table if the key already exists, or add it otherwise.
 *  Returns 0 if no add took place.  Sorts the table before exiting.
 *  \param tab          Table to update.
 *  \param index        Index to store.
 *  \param key          Key to store if not already indexed.
 *  \param type         OSC type of value to add.
 *  \param args         Value(s) to add
 *  \param length       Number of OSC argument in array
 *  \param flags        MAPPER_LOCAL_MODIFY or MAPPER_REMOTE_MODIFY,
 *                      or MAPPER_NON_MODIFABLE.
 *  \return             The number of table values added or modified. */
int mapper_table_set_record(mapper_table tab, mapper_property_t index,
                            const char *key, int length, char type,
                            const void *args, int flags);

/*! Sync an existing value with a table. Records added using this method must
 *  be added in alphabetical order since table_sort() will not be called.
 *  Key and value will not be copied by the table, and will not be freed when
 *  the table is cleared or deleted. */
void mapper_table_link_value(mapper_table tab, mapper_property_t index,
                             int length, char type, void *value, int flags);

/*! Add a typed OSC argument from a mapper_message to a string table.
 *  \param tab      Table to update.
 *  \param atom     Message atom containing pointers to message key and value.
 *  \return         The number of table values added or modified. */
int mapper_table_set_record_from_atom(mapper_table tab, mapper_message_atom atom,
                                      int flags);

int mapper_table_set_from_message(mapper_table tab, mapper_message msg,
                                  int flags);

#ifdef DEBUG
/*! Print a table of OSC values. */
void mapper_table_print_record(mapper_table_record_t *tab);
void mapper_table_print(mapper_table tab);
#endif

/*! Add arguments contained in a string table to a lo_message */
void mapper_table_add_to_message(mapper_table tab, mapper_table updates,
                                 lo_message msg);

/*! Clears and frees memory for removed records. This is not performed
 *  automatically by mapper_table_remove_record() in order to allow record
 *  removal to propagate to subscribed databases and peer devices. */
void mapper_table_clear_empty_records(mapper_table tab);

/**** Lists ****/

void *mapper_list_from_data(const void *data);

void *mapper_list_add_item(void **list, size_t size);

void mapper_list_remove_item(void **list, void *item);

void mapper_list_free_item(void *item);

void **mapper_list_new_query(const void *list, const void *f,
                             const char *types, ...);

void **mapper_list_query_union(void **query1, void **query2);

void **mapper_list_query_intersection(void **query1, void **query2);

void **mapper_list_query_difference(void **query1, void **query2);

void *mapper_list_next(void *mem);

void *mapper_list_query_index(void **query, int index);

void **mapper_list_query_next(void **query);

void **mapper_list_query_copy(void **query);

void mapper_list_query_done(void **query);

/**** Time ****/

/*! Get the current time. */
double mapper_get_current_time(void);

/**** Debug macros ****/

/*! Debug tracer */
#ifdef DEBUG
#ifdef __GNUC__
#include <stdio.h>
#include <assert.h>
#define trace(...) { printf("-- " __VA_ARGS__); }
#define trace_db(...)  { printf("\x1B[31m-- <database>\x1B[0m " __VA_ARGS__);}
#define trace_dev(DEV, ...)                                             \
{                                                                       \
    if (DEV->local && DEV->local->registered)                           \
        printf("\x1B[32m-- <%s>\x1B[0m ", mapper_device_name(DEV));     \
    else                                                                \
        printf("\x1B[32m-- <%s.?::%p>\x1B[0m ", DEV->identifier, DEV);  \
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
#define trace_db(...) {}
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
inline static int mapper_type_size(char type)
{
    switch (type) {
        case 'i': return sizeof(int);
        case 'b':
        case 'T':
        case 'F': return sizeof(int);
        case 'f': return sizeof(float);
        case 'd': return sizeof(double);
        case 's':
        case 'S': return sizeof(char*);
        case 'h': return sizeof(int64_t);
        case 't': return sizeof(mapper_timetag_t);
        case 'c': return sizeof(char);
        default:
            die_unless(0, "Unknown type '%c' in mapper_type_size().\n", type);
            return 0;
    }
}

/*! Helper to find the size in bytes of a signal's full vector. */
inline static size_t mapper_signal_vector_bytes(mapper_signal sig)
{
    return mapper_type_size(sig->type) * sig->length;
}

/*! Helper to find the pointer to the current value in a mapper_history_t. */
inline static void* mapper_history_value_ptr(mapper_history_t h)
{
    return h.value + h.position * h.length * mapper_type_size(h.type);
}

/*! Helper to find the pointer to the current timetag in a mapper_history_t. */
inline static void* mapper_history_tt_ptr(mapper_history_t h)
{
    return &h.timetag[h.position];
}

/*! Helper to check if a type character is valid. */
inline static int check_signal_type(char type)
{
    return (type != 'i' && type != 'f' && type != 'd');
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
inline static int is_number_type(char type)
{
    switch (type) {
        case 'i':
        case 'f':
        case 'd':   return 1;
        default:    return 0;
    }
}

/*! Helper to check if type is a boolean. */
inline static int is_boolean_type(char type)
{
    switch (type) {
        case 'T':
        case 'F':   return 1;
        default:    return 0;
    }
}

/*! Helper to check if type is a string. */
inline static int is_string_type(char type)
{
    switch (type) {
        case 's':
        case 'S':   return 1;
        default:    return 0;
    }
}

/*! Helper to check if type is a string or void* */
inline static int is_ptr_type(char type)
{
    switch (type) {
        case 's':
        case 'S':
        case 'v':   return 1;
        default:    return 0;
    }
}

/*! Helper to check if data type matches, but allowing 'T' and 'F' for bool. */
inline static int type_match(const char l, const char r)
{
    return (l == r) || (strchr("bTF", l) && strchr("bTF", r));
}

/*! Helper to remove a leading slash '/' from a string. */
inline static const char *skip_slash(const char *string)
{
    return string + (string && string[0]=='/');
}

#endif // __MAPPER_INTERNAL_H__
