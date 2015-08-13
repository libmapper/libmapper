
#ifndef __MAPPER_INTERNAL_H__
#define __MAPPER_INTERNAL_H__

#include "types_internal.h"
#include <mapper/mapper.h>

// Structs that refer to things defined in mapper.h are declared here
// instead of in types_internal.h

/**** Signals ****/

#define MAPPER_MAX_VECTOR_LEN 128

/*! Get the full OSC name of a signal, including device name
 *  prefix.
 *  \param sig  The signal value to query.
 *  \param name A string to accept the name.
 *  \param len  The length of string pointed to by name.
 *  \return The number of characters used, or 0 if error.  Note that
 *          in some cases the name may not be available. */
int mapper_signal_full_name(mapper_signal sig, char *name, int len);

int mapper_signal_set_from_message(mapper_signal sig, mapper_message_t *msg);

/**** Devices ****/

int mapper_device_set_from_message(mapper_device dev, mapper_message msg);

/**** Instances ****/

/*! A signal instance is defined as a vector of values, along with some
 *  metadata. */
typedef struct _mapper_signal_instance
{
    /*! User-assignable instance id. */
    int id;

    /*! Index for accessing associated value history */
    int index;

    /*! Status of this instance. */
    int is_active;

    /*! User data of this instance. */
    void *user_data;

    /*! The instance's creation timestamp. */
    mapper_timetag_t created;

    /*! Indicates whether this instance has a value. */
    int has_value;
    char *has_value_flags;

    /*! The current value of this signal instance. */
    void *value;

    /*! The timetag associated with the current value. */
    mapper_timetag_t timetag;
} mapper_signal_instance_t, *mapper_signal_instance;

/*! Bit flags for indicating signal instance status. */
#define MAPPER_RELEASED_LOCALLY  0x01
#define MAPPER_RELEASED_REMOTELY 0x02

typedef struct _mapper_signal_id_map
{
    /*! Pointer to id_map in use */
    struct _mapper_id_map *map;

    /*! Pointer to signal instance. */
    struct _mapper_signal_instance *instance;

    /*! Status of the id_map. Can be either 0 or a combination of
     *  MAPPER_RELEASED_LOCALLY and MAPPER_RELEASED_REMOTELY. */
    int status;
} mapper_signal_id_map_t;

// Mapper internal functions

/**** Networking ****/

void mapper_network_add_device(mapper_network net, mapper_device dev);

void mapper_network_add_admin(mapper_network net, mapper_admin adm);

void mapper_network_remove_device(mapper_network net, mapper_device dev);

void mapper_network_remove_admin(mapper_network net, mapper_admin adm);

int mapper_network_poll(mapper_network net);

void mapper_network_set_bundle_dest_bus(mapper_network net);

void mapper_network_set_bundle_dest_mesh(mapper_network net, lo_address address);

void mapper_network_set_bundle_dest_subscribers(mapper_network net, int type);

void mapper_network_send_bundle(mapper_network net);

void mapper_network_send_signal(mapper_network net, mapper_signal sig);

void mapper_network_send_signal_removed(mapper_network net, mapper_signal sig);

/***** Device *****/

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

void mapper_device_start_server(mapper_device dev, int port);

void mapper_device_on_id_and_ordinal(mapper_device dev,
                                     mapper_allocated_t *resource);

mapper_id_map mapper_device_add_instance_id_map(mapper_device dev, int local_id,
                                                uint64_t global_id);

void mapper_device_remove_instance_id_map(mapper_device dev, mapper_id_map map);

mapper_id_map mapper_device_find_instance_id_map_by_local(mapper_device dev,
                                                          int local_id);

mapper_id_map mapper_device_find_instance_id_map_by_global(mapper_device dev,
                                                           uint64_t global_id);

const char *mapper_device_name(mapper_device dev);

uint64_t mapper_device_unique_id(mapper_device dev);

void mapper_device_prepare_message(mapper_device dev, lo_message msg);

/***** Router *****/

mapper_link mapper_router_add_link(mapper_router router, const char *host,
                                   int admin_port, int data_port,
                                   const char *name);

void mapper_router_update_link(mapper_router router, mapper_link link,
                               const char *host, int admin_port, int data_port);

void mapper_router_remove_link(mapper_router router, mapper_link link);

void mapper_router_remove_signal(mapper_router router, mapper_router_signal rs);

void mapper_router_num_instances_changed(mapper_router r,
                                         mapper_signal sig,
                                         int size);

/*! For a given signal instance, calculate mapping outputs and
 *  forward to destinations. */
void mapper_router_process_signal(mapper_router r, mapper_signal sig,
                                  int instance_index, const void *value,
                                  int count, mapper_timetag_t timetag);

int mapper_router_send_query(mapper_router router,
                             mapper_signal sig,
                             mapper_timetag_t tt);

mapper_map mapper_router_add_map(mapper_router router, mapper_signal sig,
                                 int num_remote_signals,
                                 mapper_signal *remote_signals,
                                 const char **remote_signal_names,
                                 mapper_direction direction);

int mapper_router_remove_map(mapper_router router, mapper_map map);

/*! Find a mapping in a router by local signal and remote signal name. */
mapper_map mapper_router_find_outgoing_map(mapper_router router,
                                           mapper_signal local_src,
                                           int num_sources,
                                           const char **src_names,
                                           const char *dest_name);

mapper_map mapper_router_find_incoming_map(mapper_router router,
                                           mapper_signal local_dest,
                                           int num_sources,
                                           const char **src_names);

mapper_map mapper_router_find_incoming_map_by_id(mapper_router router,
                                                 mapper_signal local_dest,
                                                 uint64_t id);

mapper_map mapper_router_find_outgoing_map_by_id(mapper_router router,
                                                 mapper_signal local_src,
                                                 uint64_t id);

mapper_slot mapper_router_find_slot(mapper_router router, mapper_signal signal,
                                    int slot_number);

/*! Find a link by remote address in a linked list of links. */
mapper_link mapper_router_find_link_by_remote_address(mapper_router router,
                                                      const char *host,
                                                      int port);

/*! Find a link by remote device name in a linked list of links. */
mapper_link mapper_router_find_link_by_remote_name(mapper_router router,
                                                const char *name);

/*! Find a link by remote device id in a linked list of links. */
mapper_link mapper_router_find_link_by_remote_id(mapper_router router,
                                                 uint32_t id);

void mapper_router_start_queue(mapper_router router, mapper_timetag_t tt);

void mapper_router_send_queue(mapper_router router, mapper_timetag_t tt);

/**** Signals ****/

/*! Create a signal structure and fill it with provided
 *  arguments. Values and strings pointed to by this call (except
 *  user_data) will be copied. Signals should be freed by mapper_signal_free()
 *  only if they are not registered with a device.
 *  For minimum, maximum, and value, if type='f', should be float*, or
 *  if type='i', then should be int*.
 *  \param name The name of the signal, starting with '/'.
 *  \param length The length of the signal vector, or 1 for a scalar.
 *  \param type The type fo the signal value.
 *  \param is_output The direction of the signal, 1 for output, 0 for input.
 *  \param unit The unit of the signal, or 0 for none.
 *  \param minimum Pointer to a minimum value, or 0 for none.
 *  \param maximum Pointer to a maximum value, or 0 for none.
 *  \param handler Function to be called when the value of the
 *                 signal is updated.
 *  \param user_data User context pointer to be passed to handler. */
mapper_signal mapper_signal_new(const char *name, int length, char type,
                                int is_output, const char *unit,
                                const void *minimum, const void *maximum,
                                mapper_signal_update_handler *handler,
                                const void *user_data);

/*! Free memory used by a mapper_signal. Call this only for signals
 *  that are not registered with a device. Registered signals will be
 *  freed by mapper_device_free().
 *  \param sig The signal to free. */
void mapper_signal_free(mapper_signal sig);

/*! Coerce a signal instance value to a particular type and vector length
 *  and add it to a lo_message. */
void message_add_coerced_signal_instance_value(lo_message m, mapper_signal sig,
                                               mapper_signal_instance si,
                                               int length, char type);

void mapper_signal_prepare_message(mapper_signal sig, lo_message msg);

/**** Instances ****/

/*! Find an active instance with the given instance ID.
 *  \param sig       The signal owning the desired instance.
 *  \param global_id Globally unique id of this instance.
 *  \param flags     Bitflags indicating if search should include released instances.
 *  \return          The index of the retrieved signal instance, or -1 if no active
 *                   instances match the specified instance ID map. */
int mapper_signal_find_instance_with_global_id(mapper_signal sig,
                                               uint64_t global_id, int flags);

/*! Fetch a reserved (preallocated) signal instance using an instance id,
 *  activating it if necessary.
 *  \param sig   The signal owning the desired instance.
 *  \param id    The requested signal instance ID.
 *  \param flags Bitflags indicating if search should include released instances.
 *  \param tt    Timetag associated with this action.
 *  \return      The index of the retrieved signal instance, or -1 if no free
 *               instances were available and allocation of a new instance
 *               was unsuccessful according to the selected allocation
 *               strategy. */
int mapper_signal_instance_with_local_id(mapper_signal sig, int id, int flags,
                                         mapper_timetag_t *tt);

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
int mapper_signal_instance_with_global_id(mapper_signal sig, uint64_t global_id,
                                          int flags, mapper_timetag_t *tt);

/*! Release a specific signal instance. */
void mapper_signal_release_instance_internal(mapper_signal sig,
                                             int instance_index,
                                             mapper_timetag_t timetag);

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
int mapper_map_set_from_message(mapper_map map, mapper_message_t *msg,
                                int override);

const char *mapper_param_string(mapper_message_param_t param);

const char *mapper_boundary_action_string(mapper_boundary_action bound);

mapper_boundary_action mapper_boundary_action_from_string(const char *string);

const char *mapper_mode_string(mapper_mode mode);

mapper_mode mapper_mode_from_string(const char *string);

/**** Database ****/

int mapper_db_property_index(const void *thestruct, table extra,
                             unsigned int index, const char **property,
                             int *length, char *type, const void **value,
                             table proptable);

int mapper_db_property(const void *thestruct, table extra, const char *property,
                       int *length, char *type, const void **value,
                       table proptable);

/**** Local device database ****/

/*! Add or update an entry in the device database using parsed message
 *  parameters.
 *  \param db           The database to operate on.
 *  \param device_name  The name of the device.
 *  \param params       The parsed message parameters containing new device
 *                      information.
 *  \return             Pointer to the device database entry. */
mapper_device mapper_db_add_or_update_device_params(mapper_db db,
                                                    const char *device_name,
                                                    mapper_message_t *params);

/*! Add or update an entry in the signal database using parsed message
 *  parameters.
 *  \param db          The database to operate on.
 *  \param signal_name The name of the signal.
 *  \param device_name The name of the device associated with this signal.
 *  \param params      The parsed message parameters containing new signal
 *                     information.
 *  \return            Pointer to the signal database entry. */
mapper_signal mapper_db_add_or_update_signal_params(mapper_db db,
                                                    const char *signal_name,
                                                    const char *device_name,
                                                    mapper_message_t *params);

/*! Initialize an already-allocated mapper_signal structure. */
void mapper_signal_init(mapper_signal sig, const char *name, int length,
                        char type, mapper_direction direction, const char *unit,
                        const void *minimum, const void *maximum,
                        mapper_signal_update_handler *handler,
                        const void *user_data);

/*! Add or update an entry in the map database using parsed
 *  message parameters.
 *  \param db           The database to operate on.
 *  \param num_srcs     The number of source slots for this map
 *  \param src_names    The full name of the source signal.
 *  \param dest_name    The full name of the destination signal.
 *  \param params       The parsed message parameters containing new
 *                      map information.
 *  \return             Pointer to the map database entry. */
mapper_map mapper_db_add_or_update_map_params(mapper_db db, int num_srcs,
                                              const char **src_names,
                                              const char *dest_name,
                                              mapper_message_t *params);

/*! Remove a device from the database. */
void mapper_db_remove_device(mapper_db db, mapper_device dev, int quiet);

void mapper_db_remove_signal(mapper_db db, mapper_signal sig);

/*! Remove a named signal from the database if it exists. */
void mapper_db_remove_signal_by_name(mapper_db db, const char *dev_name,
                                     const char *sig_name);

/*! Remove signals in the provided query. */
void mapper_db_remove_signals_by_query(mapper_db db, mapper_signal *sigs);

/*! Remove maps in the provided query. */
void mapper_db_remove_maps_by_query(mapper_db db, mapper_map *maps);

/*! Remove a specific map from the database. */
void mapper_db_remove_map(mapper_db db, mapper_map map);

/*! Dump device information database to the screen.  Useful for
 *  debugging, only works when compiled in debug mode. */
void mapper_db_dump(mapper_db db);

void mapper_db_remove_all_callbacks(mapper_db db);

/*! Check device records for unresponsive devices. */
void mapper_db_check_device_status(mapper_db db, uint32_t now_sec);

/*! Flush device records for unresponsive devices. */
mapper_device mapper_db_expired_device(mapper_db db, uint32_t last_ping);

/**** Messages ****/
/*! Parse the device and signal names from an OSC path. */
int mapper_parse_names(const char *string, char **devnameptr, char **signameptr);

/*! Parse a message based on an OSC path and parameters.
 *  \param argc     Number of arguments in the argv array.
 *  \param types    String containing message parameter types.
 *  \param argv     Vector of lo_arg structures.
 *  \return         A mapper_message structure. Should be freed when done using
 *                  mapper_message_free. */
mapper_message mapper_message_parse_params(int argc, const char *types,
                                           lo_arg **argv);

void mapper_message_free(mapper_message msg);

/*! Look up the value of a message parameter by symbolic identifier.
 *  Note that it's possible the 'types' string will be longer
 *  than the actual contents pointed to; it is up to the usage of this
 *  function to ensure it only processes the number of parameters indicated
 *  by the 'length' property.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \return         Pointer to mapper_message_atom, or zero if not found. */
mapper_message_atom mapper_message_param(mapper_message_t *msg,
                                         mapper_message_param_t param);

/*! Helper to get a direct parameter value only if it's a string.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \return         A string containing the parameter value or zero if
 *                  not found. */
const char* mapper_message_param_if_string(mapper_message_t *msg,
                                           mapper_message_param_t param);

/*! Helper to get a direct parameter value only if it's a char type,
 *  or if it's a string of length one.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \return         A string containing the parameter value or zero if
 *                  not found. */
const char* mapper_message_param_if_char(mapper_message_t *msg,
                                         mapper_message_param_t param);


/*! Helper to get a direct parameter value only if it's an int or boolean.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \param value    Location of int to receive value.
 *  \return         Zero if not found, otherwise non-zero. */
int mapper_message_param_if_int(mapper_message_t *msg,
                                mapper_message_param_t param,
                                int *value);

/*! Helper to get a direct parameter value only if it's an int64.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \param value    Location of int64 to receive value.
 *  \return         Zero if not found, otherwise non-zero. */
int mapper_message_param_if_int64(mapper_message_t *msg,
                                  mapper_message_param_t param,
                                  int64_t *value);

/*! Helper to get a direct parameter value only if it's a float.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \param value    Location of float to receive value.
 *  \return         Zero if not found, otherwise non-zero. */
int mapper_message_param_if_float(mapper_message_t *msg,
                                  mapper_message_param_t param,
                                  float *value);

/*! Helper to update a direct parameter value only if it's a string.
 *  \param pdest    Pointer to receive the updated value.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \return         1 if the parameter has been found and updated. */
int mapper_update_string_if_arg(char **pdest, mapper_message_t *msg,
                                mapper_message_param_t param);

/*! Helper to update a direct parameter value only if it's a char type.
 *  \param pdest    Pointer to receive the updated value.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \return         1 if the parameter has been found and updated. */
int mapper_update_char_if_arg(char *pdest, mapper_message_t *msg,
                              mapper_message_param_t param);

/*! Helper to update a direct parameter value only if it's a boolean.
 *  \param pdest    Pointer to receive the updated value.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \return         1 if the parameter has been found and updated. */
int mapper_update_bool_if_arg(int *pdest, mapper_message_t *msg,
                              mapper_message_param_t param);

/*! Helper to update a direct parameter value only if it's an int.
 *  \param pdest    Pointer to receive the updated value.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \return         1 if the parameter has been found and updated. */
int mapper_update_int_if_arg(int *pdest, mapper_message_t *msg,
                             mapper_message_param_t param);

/*! Helper to update a direct parameter value only if it's an int64.
 *  \param pdest    Pointer to receive the updated value.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \return         1 if the parameter has been found and updated. */
int mapper_update_int64_if_arg(int64_t *pdest, mapper_message_t *msg,
                               mapper_message_param_t param);

/*! Helper to update a direct parameter value only if it's a float.
 *  \param pdest    Pointer to receive the updated value.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \return         1 if the parameter has been found and updated. */
int mapper_update_float_if_arg(float *pdest, mapper_message_t *msg,
                               mapper_message_param_t param);

/*! Helper to update a direct parameter value only if it's a double.
 *  \param pdest    Pointer to receive the updated value.
 *  \param msg      Structure containing parameter info.
 *  \param param    Symbolic identifier of the parameter to look for.
 *  \return         1 if the parameter has been found and updated. */
int mapper_update_double_if_arg(double *pdest, mapper_message_t *msg,
                                mapper_message_param_t param);

/*! Helper to return the boundary action from a message parameter.
 *  \param msg Structure containing parameter info.
 *  \param param Either AT_BOUND_MIN or AT_BOUND_MAX.
 *  \return The boundary action, or -1 if not found. */
mapper_boundary_action mapper_message_boundary_action(mapper_message_t *msg,
                                                      mapper_message_param_t param);

/*! Helper to return the signal direction from a message parameter.
 *  \param msg Structure containing parameter info.
 *  \return 0 for input, 1 for output, or -1 if not found. */
int mapper_message_signal_direction(mapper_message_t *msg);

/*! Helper to return the mode type from a message parameter.
 *  \param msg Structure containing parameter info.
 *  \return The mode type, or -1 if not found. */
mapper_mode mapper_message_mode(mapper_message_t *msg);

/*! Helper to return the 'mute' state from a message parameter.
 *  \param msg Structure containing parameter info.
 *  \return The muted state (0 or 1), or -1 if not found. */
int mapper_message_mute(mapper_message_t *msg);

/*! Store 'extra' parameters specified in a mapper_message to a table.
 *  \param t    Table to edit.
 *  \param msg  Message containing parameters.
 *  \return The number of parameters added or modified. */
int mapper_message_add_or_update_extra_params(table t, mapper_message_t *msg);

void mapper_message_add_typed_value(lo_message m, int length, char type,
                                    const void *value);

/*! Prepare a lo_message for sending based on a map struct. */
const char *mapper_map_prepare_message(mapper_map map, lo_message msg,
                                       int slot_index);

/*! Helper for setting property value from different lo_arg types. */
int propval_set_from_lo_arg(void *dest, char dest_type,
                            lo_arg *src, char src_type, int index);

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
table table_new();

/*! Clear the contents of a string table.
 * \param t Table to free. */
void table_clear(table t);

/*! Free a string table.
 * \param t Table to free. */
void table_free(table t);

/*! Add a string to a table. */
void table_add(table t, const char *key, const void *value, int is_mapper_prop);

/*! Sort a table.  Call this after table_add and before table_find. */
void table_sort(table t);

/*! Look up a value in a table.  Returns 0 if found, 1 if not found,
 *  and fills in value if found. */
int table_find(table t, const char *key, void **value);

/*! Look up a value in a table.  Returns the value directly, which may
 *  be zero, but also returns 0 if not found. */
void *table_find_p(table t, const char *key);

/*! Look up a value in a table.  Returns a pointer to the value,
 *  allowing it to be modified in-place.  Returns 0 if not found. */
void **table_find_pp(table t, const char *key);

/*! Remove a key-value pair from a table (by key). free_key is
 *  non-zero to indicate that key should be free()'d. */
void table_remove_key(table t, const char *key, int free_key);

/*! Get the value at a particular index. */
void *table_value_at_index_p(table t, unsigned int index);

/*! Get the key at a particular index. */
const char *table_key_at_index(table t, unsigned int index);

/*! Update a value in a table if the key already exists, or add it
 *  otherwise.  Returns 0 if no add took place.  Sorts the table
 *  before exiting, so this should be considered a longer operation
 *  than table_add. */
int table_add_or_update(table t, const char *key, const void *value);

#ifdef DEBUG
/*! Dump a table of OSC values. */
void table_dump_osc_values(table t);
#endif

/*! Add a typed OSC argument from a mapper_message to a string table.
 *  \param t        Table to update.
 *  \param atom     Message atom containing pointers to message key and value.
 *  \return The number of table values added or modified. */
int mapper_table_add_or_update_message_atom(table t, mapper_message_atom atom);

/*! Add a typed argument to a string table.
 *  \param t        Table to update.
 *  \param key      Key to store.
 *  \param type     OSC type of value to add.
 *  \param args     Value(s) to add
 *  \param length   Number of OSC argument in array
 *  \return         The number of table values added or modified. */
int mapper_table_add_or_update_typed_value(table t, const char *key, int length,
                                           char type, const void *args);

/*! Add arguments contained in a string table to a lo_message */
void mapper_message_add_value_table(lo_message m, table t);

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

/*! Initialize a mapper_clock. */
void mapper_clock_init(mapper_clock clock);

/*! Get the current time from a mapper_clock. */
void mapper_clock_now(mapper_clock clock, mapper_timetag_t *timetag);

/**** Debug macros ****/

/*! Debug tracer */
#ifdef DEBUG
#ifdef __GNUC__
#include <stdio.h>
#include <assert.h>
#define trace(...) { printf("-- " __VA_ARGS__); }
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
        die_unless(0, "getting size of unknown type %c\n", type);
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

/*! Helper to check if type is a string. */
inline static int is_string_type(char type)
{
    switch (type) {
        case 's':
        case 'S':   return 1;
        default:    return 0;
    }
}

/*! Helper to remove a leading slash '/' from a string. */
inline static const char *skip_slash(const char *string)
{
    return string + (string && string[0]=='/');
}

int mapper_property_set_string(char **property, const char *string);

#endif // __MAPPER_INTERNAL_H__
