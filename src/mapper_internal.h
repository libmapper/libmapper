
#ifndef __MAPPER_INTERNAL_H__
#define __MAPPER_INTERNAL_H__

#include "types_internal.h"
#include <mapper/mapper.h>

// Structs that refer to things defined in mapper.h are declared here
// instead of in types_internal.h

/**** Signals ****/

struct _mapper_signal_id_map_t;

/*! A signal is defined as a vector of values, along with some
 *  metadata. */
struct _mapper_signal
{
    /*! Properties of this signal. */
    mapper_db_signal_t props;

    /*! The device associated with this signal. */
    struct _mapper_device *device;

    /*! ID maps and active instances. */
    struct _mapper_signal_id_map *id_maps;
    int id_map_length;

    /*! Array of pointers to the signal instances. */
    struct _mapper_signal_instance **instances;

    /*! Bitflag value when entire signal vector is known. */
    char *has_complete_value;

    /*! Type of voice stealing to perform on instances. */
    mapper_instance_allocation_type instance_allocation_type;

    /*! An optional function to be called when the signal value
     *  changes. */
    mapper_signal_update_handler *handler;

    /*! An optional function to be called when the signal instance management
     *  events occur. */
    mapper_signal_instance_event_handler *instance_event_handler;

    /*! Flags for deciding when to call the instance event handler. */
    int instance_event_flags;
};

/**** Devices ****/

struct _mapper_device {
    mapper_db_device_t props;           //!< Properties.
    mapper_admin_allocated_t ordinal;   /*!< A unique ordinal for this
                                         *   device instance. */
    int registered;                     /*!< Non-zero if this device has
                                         *   been registered. */

    /*! Non-zero if this device is the sole owner of this admin, i.e.,
     *  it was created during mdev_new() and should be freed during
     *  mdev_free(). */
    int own_admin;

    mapper_admin admin;
    struct _mapper_signal **inputs;
    struct _mapper_signal **outputs;
    int n_alloc_inputs;
    int n_alloc_outputs;
    int n_output_callbacks;
    int version;
    int flags;    /*!< Bitflags indicating if information has already been
                   *   sent in a given polling step. */
    mapper_router routers;
    mapper_receiver receivers;

    /*! Function to call for custom link handling. */
    mapper_device_link_handler *link_cb;
    void *link_cb_userdata;

    /*! Function to call for custom connection handling. */
    mapper_device_connection_handler *connection_cb;
    void *connection_cb_userdata;

    /*! The list of active instance id mappings. */
    struct _mapper_id_map *active_id_map;

    /*! The list of reserve instance id mappings. */
    struct _mapper_id_map *reserve_id_map;

    uint32_t id_counter;
    int link_timeout_sec;   /* Number of seconds after which unresponsive
                             * links will be removed, or 0 for never. */

    /*! Server used to handle incoming messages. */
    lo_server server;
};

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
    mapper_timetag_t creation_time;

    /*! Indicates whether this instance has a value. */
    int has_value;
    char *has_value_flags;

    /*! The current value of this signal instance. */
    void *value;

    /*! The timetag associated with the current value. */
    mapper_timetag_t timetag;
} mapper_signal_instance_t, *mapper_signal_instance;

/*! Bit flags for indicating signal instance status. */
#define IN_RELEASED_LOCALLY  0x01
#define IN_RELEASED_REMOTELY 0x02

typedef struct _mapper_signal_id_map
{
    /*! Pointer to id_map in use */
    struct _mapper_id_map *map;

    /*! Pointer to signal instance. */
    struct _mapper_signal_instance *instance;

    /*! Status of the id_map. Can be either 0 or a combination of
     *  IN_RELEASED_LOCALLY and IN_RELEASED_REMOTELY. */
    int status;
} mapper_signal_id_map_t;

// Mapper internal functions

/**** Admin ****/

void mapper_admin_add_device(mapper_admin admin, mapper_device dev);

void mapper_admin_add_monitor(mapper_admin admin, mapper_monitor mon);

void mapper_admin_remove_monitor(mapper_admin admin, mapper_monitor mon);

int mapper_admin_poll(mapper_admin admin);

void mapper_admin_set_bundle_dest_bus(mapper_admin admin);

void mapper_admin_set_bundle_dest_mesh(mapper_admin admin, lo_address address);

void mapper_admin_set_bundle_dest_subscribers(mapper_admin admin, int type);

void mapper_admin_send_bundle(mapper_admin admin);

void mapper_admin_send_signal(mapper_admin admin, mapper_device md,
                              mapper_signal sig);

void mapper_admin_send_signal_removed(mapper_admin admin, mapper_device md,
                                      mapper_signal sig);

/*! Macro for calling message-sending function. */
#define mapper_admin_bundle_message(...)                                    \
    _real_mapper_admin_bundle_message(__VA_ARGS__, N_AT_PARAMS)

/*! Message-sending function, not to be called directly. */
void _real_mapper_admin_bundle_message(mapper_admin admin,
                                       int msg_index,
                                       const char *path,
                                       const char *types, ...);

/*! Message-sending function which appends a parameter list at the end. */
void _real_mapper_admin_bundle_message_with_params(mapper_admin admin,
                                                   mapper_message_t *params,
                                                   mapper_string_table_t *extra,
                                                   int msg_index,
                                                   const char *path,
                                                   const char *types, ...);

#define mapper_admin_bundle_message_with_params(...)                        \
    _real_mapper_admin_bundle_message_with_params(__VA_ARGS__, N_AT_PARAMS)

/***** Device *****/

void mdev_registered(mapper_device md);

void mdev_add_signal_methods(mapper_device md, mapper_signal sig);

void mdev_remove_signal_methods(mapper_device md, mapper_signal sig);

void mdev_add_instance_release_request_callback(mapper_device md,
                                                mapper_signal sig);

void mdev_remove_instance_release_request_callback(mapper_device md,
                                                   mapper_signal sig);

void mdev_num_instances_changed(mapper_device md,
                                mapper_signal sig,
                                int size);

void mdev_route_signal(mapper_device md,
                       mapper_signal sig,
                       int instance_index,
                       void *value,
                       int count,
                       mapper_timetag_t tt);

int mdev_route_query(mapper_device md, mapper_signal sig,
                     mapper_timetag_t tt);

void mdev_route_released(mapper_device md,
                         mapper_signal sig,
                         int instance_index,
                         mapper_timetag_t tt);

void mdev_add_router(mapper_device md, mapper_router rt);

void mdev_remove_router(mapper_device md, mapper_router rt);

void mdev_add_receiver(mapper_device md, mapper_receiver r);

void mdev_remove_receiver(mapper_device md, mapper_receiver r);

void mdev_receive_update(mapper_device md, mapper_signal sig,
                         int instance_index, mapper_timetag_t tt);

void mdev_release_scope(mapper_device md, const char *scope);

void mdev_start_server(mapper_device mdev, int port);

void mdev_on_id_and_ordinal(mapper_device md,
                            mapper_admin_allocated_t *resource);

mapper_id_map mdev_add_instance_id_map(mapper_device device, int local_id,
                                       int origin, int public_id);

void mdev_remove_instance_id_map(mapper_device device, mapper_id_map map);

mapper_id_map mdev_find_instance_id_map_by_local(mapper_device device,
                                                 int local_id);

mapper_id_map mdev_find_instance_id_map_by_remote(mapper_device device,
                                                  int origin, int public_id);

const char *mdev_name(mapper_device md);

/***** Router *****/

mapper_router mapper_router_new(mapper_device device, const char *host,
                                int admin_port, int data_port,
                                const char *name);

void mapper_router_free(mapper_router router);

/*! Set a router's properties based on message parameters. */
int mapper_router_set_from_message(mapper_router router,
                                   mapper_message_t *msg);

void mapper_router_num_instances_changed(mapper_router r,
                                         mapper_signal sig,
                                         int size);

/*! For a given connection instance, construct a mapped signal and
 *  send it on to the destination. */
void mapper_router_process_signal(mapper_router r,
                                  mapper_signal sig,
                                  int instance_index,
                                  void *value,
                                  int count,
                                  mapper_timetag_t timetag);

lo_message mapper_router_build_message(void *value,
                                       int length,
                                       char type,
                                       char *typestring,
                                       mapper_id_map id_map);

int mapper_router_send_query(mapper_router router,
                             mapper_signal sig,
                             mapper_timetag_t tt);

mapper_connection mapper_router_add_connection(mapper_router router,
                                               mapper_signal sig,
                                               const char *dest_name,
                                               char dest_type,
                                               int dest_length);

int mapper_router_remove_connection(mapper_router router,
                                    mapper_connection connection);

/*! Find a connection in a router by source and destination signal names. */
mapper_connection mapper_router_find_connection_by_names(mapper_router rt,
                                                         const char* src_name,
                                                         const char* dest_name);

/*! Find a router by destination address in a linked list of routers. */
mapper_router mapper_router_find_by_dest_address(mapper_router routers,
                                                 const char *host,
                                                 int port);

/*! Find a router by destination device name in a linked list of routers. */
mapper_router mapper_router_find_by_dest_name(mapper_router routers,
                                              const char *dest_name);

/*! Find a router by destination device hash in a linked list of routers. */
mapper_router mapper_router_find_by_dest_hash(mapper_router routers,
                                              uint32_t hash);

void mapper_router_start_queue(mapper_router router, mapper_timetag_t tt);

void mapper_router_send_queue(mapper_router router, mapper_timetag_t tt);

/***** Receiver *****/

mapper_receiver mapper_receiver_new(mapper_device device, const char *host,
                                    int admin_port, int data_port,
                                    const char *name);

void mapper_receiver_free(mapper_receiver receiver);

/*! Set a router's properties based on message parameters. */
int mapper_receiver_set_from_message(mapper_receiver receiver,
                                     mapper_message_t *msg);

void mapper_receiver_send_update(mapper_receiver r,
                                 mapper_signal sig,
                                 int instance_index,
                                 mapper_timetag_t tt);

void mapper_receiver_send_released(mapper_receiver r,
                                   mapper_signal sig,
                                   int instance_index,
                                   mapper_timetag_t tt);

mapper_connection mapper_receiver_add_connection(mapper_receiver receiver,
                                                 mapper_signal sig,
                                                 const char *src_name,
                                                 char src_type,
                                                 int src_length);

int mapper_receiver_remove_connection(mapper_receiver receiver,
                                      mapper_connection connection);

/*! Find a connection in a receiver by source and destination signal names. */
mapper_connection mapper_receiver_find_connection_by_names(mapper_receiver rc,
                                                           const char* src_name,
                                                           const char* dest_name);

/*! Find a receiver by source address in a linked list of receivers. */
mapper_receiver mapper_receiver_find_by_src_address(mapper_receiver receivers,
                                                    const char *host,
                                                    int port);

/*! Find a receiver by source device name in a linked list of receivers. */
mapper_receiver mapper_receiver_find_by_src_name(mapper_receiver receivers,
                                                 const char *src_name);

/*! Find a receiver by source device hash in a linked list of receivers. */
mapper_receiver mapper_receiver_find_by_src_hash(mapper_receiver receivers,
                                                 uint32_t hash);

/**** Signals ****/

/*! Create a signal structure and fill it with provided
 *  arguments. Values and strings pointed to by this call (except
 *  user_data) will be copied. Signals should be freed by msig_free()
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
 *  \param number of signal instances.
 *  \param handler Function to be called when the value of the
 *                 signal is updated.
 *  \param user_data User context pointer to be passed to handler. */
mapper_signal msig_new(const char *name, int length, char type,
                       int is_output, const char *unit,
                       void *minimum, void *maximum, int num_instances,
                       mapper_signal_update_handler *handler,
                       void *user_data);

/*! Free memory used by a mapper_signal. Call this only for signals
 *  that are not registered with a device. Registered signals will be
 *  freed by mdev_free().
 *  \param sig The signal to free. */
void msig_free(mapper_signal sig);

/*! Coerce a signal instance value to a particular type and vector length
 *  and add it to a lo_message. */
void message_add_coerced_signal_instance_value(lo_message m,
                                               mapper_signal sig,
                                               mapper_signal_instance si,
                                               int length,
                                               const char type);

/**** Instances ****/

/*! Store an instance id_map record.
 *  \param sig   The signal owning the instance.
 *  \param si    A pointer to the signal instance structure.
 *  \param map   The id map matched to this instance.
 *  \return      The index at which the record was stored. */
int msig_add_id_map(mapper_signal sig, mapper_signal_instance si,
                    mapper_id_map map);

/*! Find an active instance with the given instance ID.
 *  \param sig   The signal owning the desired instance.
 *  \param id    The requested signal instance ID.
 *  \param flags Bitflags indicating if search should include released instances.
 *  \return      The index of the retrieved signal instance, or -1 if no active
 *               instances match the specified instance ID. */
int msig_find_instance_with_local_id(mapper_signal sig, int id, int flags);

/*! Find an active instance with the given instance ID.
 *  \param sig       The signal owning the desired instance.
 *  \param origin    Unique hash identifying the device that activated this instance.
 *  \param public_id The public id of this instance.
 *  \param flags     Bitflags indicating if search should include released instances.
 *  \return          The index of the retrieved signal instance, or -1 if no active
 *                   instances match the specified instance ID map. */
int msig_find_instance_with_remote_ids(mapper_signal sig, int origin,
                                       int public_id, int flags);

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
int msig_get_instance_with_local_id(mapper_signal sig, int id,
                                    int flags, mapper_timetag_t *tt);

/*! Fetch a reserved (preallocated) signal instance using instance id map,
 *  activating it if necessary.
 *  \param sig       The signal owning the desired instance.
 *  \param origin    Unique hash identifying the device that activated this instance.
 *  \param public_id The public id of this instance.
 *  \param flags     Bitflags indicating if search should include released instances.
 *  \param tt        Timetag associated with this action.
 *  \return          The index of the retrieved signal instance, or NULL if no free
 *                   instances were available and allocation of a new instance
 *                   was unsuccessful according to the selected allocation
 *                   strategy. */
int msig_get_instance_with_remote_ids(mapper_signal sig, int origin, int public_id,
                                      int flags, mapper_timetag_t *tt);

/*! Release a specific signal instance. */
void msig_release_instance_internal(mapper_signal sig,
                                    int instance_index,
                                    mapper_timetag_t timetag);

/**** connections ****/

/*! Perform the connection from a value vector to a scalar.  The
 *  result of this operation should be sent to the destination.
 *  \param connection The mapping process to perform.
 *  \param from_value Pointer to current value of the source signal.
 *  \param to_value   Pointer to a value to receive the result.
 *  \return Zero if the operation was muted, or one if it was performed. */
int mapper_connection_perform(mapper_connection connection,
                              mapper_signal_history_t *from_value,
                              mapper_signal_history_t **expr_vars,
                              mapper_signal_history_t *to_value,
                              char *typestring);

int mapper_boundary_perform(mapper_connection connection,
                            mapper_signal_history_t *from_value);

/*! Set a connection's properties based on message parameters. */
int mapper_connection_set_from_message(mapper_connection connection,
                                       mapper_message_t *msg);

void mapper_connection_set_mode_direct(mapper_connection connection);

void mapper_connection_set_mode_linear(mapper_connection connection);

void mapper_connection_set_mode_expression(mapper_connection connection,
                                           const char *expr);

void mapper_connection_set_mode_calibrate(mapper_connection connection);

const char *mapper_get_boundary_action_string(mapper_boundary_action bound);

const char *mapper_get_mode_type_string(mapper_mode_type mode);

/**** Local device database ****/

/*! Add or update an entry in the device database using parsed message
 *  parameters.
 *  \param db           The database to operate on.
 *  \param device_name  The name of the device.
 *  \param params       The parsed message parameters containing new device
 *                      information.
 *  \param current_time The current time.
 *  \return             Non-zero if device was added to the database, or
 *                      zero if it was already present. */
int mapper_db_add_or_update_device_params(mapper_db db,
                                          const char *device_name,
                                          mapper_message_t *params,
                                          mapper_timetag_t *current_time);

/*! Add or update an entry in the signal database using parsed message
 *  parameters.
 *  \param db          The database to operate on.
 *  \param signal_name The name of the signal.
 *  \param device_name The name of the device associated with this signal.
 *  \param params      The parsed message parameters containing new signal
 *                     information.
 *  \return            Non-zero if signal was added to the database, or
 *                     zero if it was already present. */
int mapper_db_add_or_update_signal_params(mapper_db db,
                                          const char *signal_name,
                                          const char *device_name,
                                          mapper_message_t *params);

/*! Initialize an already-allocated mapper_db_signal structure. */
void mapper_db_signal_init(mapper_db_signal sig, int is_output,
                           char type, int length,
                           const char *name, const char *unit);

/*! Add or update an entry in the connection database using parsed
 *  message parameters.
 *  \param db        The database to operate on.
 *  \param src_name  The full name of the source signal.
 *  \param dest_name The full name of the destination signal.
 *  \param params    The parsed message parameters containing new
 *                   connection information.
 *  \return          Non-zero if connection was added to the database,
 *                   or zero if it was already present. */
int mapper_db_add_or_update_connection_params(mapper_db db,
                                              const char *src_name,
                                              const char *dest_name,
                                              mapper_message_t *params);

/*! Remove a named device from the database if it exists. */
void mapper_db_remove_device_by_name(mapper_db db, const char *name);

/*! Remove a named input signal from the database if it exists. */
void mapper_db_remove_input_by_name(mapper_db db, const char *dev_name,
                                    const char *sig_name);

/*! Remove a named output signal from the database if it exists. */
void mapper_db_remove_output_by_name(mapper_db db, const char *dev_name,
                                    const char *sig_name);

/*! Remove signals in the provided query. */
void mapper_db_remove_inputs_by_query(mapper_db db,
                                      mapper_db_signal_t **s);

/*! Remove signals in the provided query. */
void mapper_db_remove_outputs_by_query(mapper_db db,
                                       mapper_db_signal_t **s);

/*! Remove connections in the provided query. */
void mapper_db_remove_connections_by_query(mapper_db db,
                                           mapper_db_connection_t **c);

/*! Remove a specific connection from the database. */
void mapper_db_remove_connection(mapper_db db,
                                 mapper_db_connection map);

/*! Remove links in the provided query. */
void mapper_db_remove_links_by_query(mapper_db db,
                                     mapper_db_link_t **s);

/*! Remove a specific link from the database. */
void mapper_db_remove_link(mapper_db db,
                           mapper_db_link map);

/*! Dump device information database to the screen.  Useful for
 *  debugging, only works when compiled in debug mode. */
void mapper_db_dump(mapper_db db);

void mapper_db_remove_all_callbacks(mapper_db db);

/*! Check device records for unresponsive devices. */
int mapper_db_check_device_status(mapper_db db, uint32_t now_sec);

/*! Flush device records for unresponsive devices. */
int mapper_db_flush(mapper_db db, uint32_t current_time,
                    uint32_t timeout, int quiet);

/**** Links ****/

/*! Add or update an entry in the link database using parsed message
 *  parameters.
 *  \param db     The database to operate on.
 *  \param src_name  The name of the source device.
 *  \param dest_name The name of the destination device.
 *  \param params The parsed message parameters containing new link
 *                information.
 *  \return       Non-zero if link was added to the database, or
 *                zero if it was already present. */
int mapper_db_add_or_update_link_params(mapper_db db,
                                        const char *src_name,
                                        const char *dest_name,
                                        mapper_message_t *params);

/**** Connections ****/

void mhist_realloc(mapper_signal_history_t *history, int history_size,
                   int sample_size, int is_output);

/*! Update a scope identifiers to a given connection record. */
int mapper_db_connection_update_scope(mapper_connection_scope scope,
                                      lo_arg **scope_list, int num);

/**** Messages ****/

/*! Parse a message based on an OSC path and parameters.
 *  \param msg    Structure to receive parameter info.
 *  \param path   String containing message address.
 *  \param types  String containing message parameter types.
 *  \param argc   Number of arguments in the argv array.
 *  \param argv   Vector of lo_arg structures.
 *  \return       Non-zero indicates error in parsing. */
int mapper_msg_parse_params(mapper_message_t *msg,
                            const char *path, const char *types,
                            int argc, lo_arg **argv);

/*! Look up the value of a message parameter by symbolic identifier.
 *  \param msg    Structure containing parameter info.
 *  \param param  Symbolic identifier of the parameter to look for.
 *  \return       Pointer to lo_arg, or zero if not found. */
lo_arg** mapper_msg_get_param(mapper_message_t *msg,
                              mapper_msg_param_t param);

/*! Look up the type of a message parameter by symbolic identifier.
 *  Note that it's possible the returned type string will be longer
 *  than the actual contents pointed to; it is up to the usage of this
 *  function to ensure it only processes the a priori expected number
 *  of parameters. The number of parameter elements can be retrieved
 *  using the function mapper_msg_get_length().
 *  \param msg    Structure containing parameter info.
 *  \param param  Symbolic identifier of the parameter to look for.
 *  \return       String containing type of each parameter argument. */
const char* mapper_msg_get_type(mapper_message_t *msg,
                                mapper_msg_param_t param);

/*! Look up the vector length of a message parameter by symbolic identifier.
 *  \param msg    Structure containing parameter info.
 *  \param param  Symbolic identifier of the parameter to look for.
 *  \return       Integer containing the length of the parameter vector. */
int mapper_msg_get_length(mapper_message_t *msg,
                          mapper_msg_param_t param);

/*! Helper to get a direct parameter value only if it's a string.
 *  \param msg    Structure containing parameter info.
 *  \param param  Symbolic identifier of the parameter to look for.
 *  \return       A string containing the parameter value or zero if
 *                not found. */
const char* mapper_msg_get_param_if_string(mapper_message_t *msg,
                                           mapper_msg_param_t param);

/*! Helper to get a direct parameter value only if it's a char type,
 *  or if it's a string of length one.
 *  \param msg    Structure containing parameter info.
 *  \param param  Symbolic identifier of the parameter to look for.
 *  \return       A string containing the parameter value or zero if
 *                not found. */
const char* mapper_msg_get_param_if_char(mapper_message_t *msg,
                                         mapper_msg_param_t param);

/*! Helper to get a direct parameter value only if it's an int.
 *  \param msg    Structure containing parameter info.
 *  \param param  Symbolic identifier of the parameter to look for.
 *  \param value  Location of int to receive value.
 *  \return       Zero if not found, otherwise non-zero. */
int mapper_msg_get_param_if_int(mapper_message_t *msg,
                                mapper_msg_param_t param,
                                int *value);

/*! Helper to get a direct parameter value only if it's a float.
 *  \param msg    Structure containing parameter info.
 *  \param param  Symbolic identifier of the parameter to look for.
 *  \param value  Location of float to receive value.
 *  \return       Zero if not found, otherwise non-zero. */
int mapper_msg_get_param_if_float(mapper_message_t *msg,
                                  mapper_msg_param_t param,
                                  float *value);

/*! Helper to return the boundary action from a message parameter.
 *  \param msg Structure containing parameter info.
 *  \param param Either AT_BOUND_MIN or AT_BOUND_MAX.
 *  \return The boundary action, or -1 if not found. */
mapper_boundary_action mapper_msg_get_boundary_action(mapper_message_t *msg,
                                                      mapper_msg_param_t param);

/*! Helper to return the signal direction from a message parameter.
 *  \param msg Structure containing parameter info.
 *  \return 0 for input, 1 for output, or -1 if not found. */
mapper_mode_type mapper_msg_get_direction(mapper_message_t *msg);

/*! Helper to return the mode type from a message parameter.
 *  \param msg Structure containing parameter info.
 *  \return The mode type, or -1 if not found. */
mapper_mode_type mapper_msg_get_mode(mapper_message_t *msg);

/*! Helper to return the 'mute' state from a message parameter.
 *  \param msg Structure containing parameter info.
 *  \return The muted state (0 or 1), or -1 if not found. */
int mapper_msg_get_mute(mapper_message_t *msg);

/*! Store 'extra' parameters specified in a mapper_message to a table.
 *  \param t      Table to edit.
 *  \param params Message containing parameters.
 *  \return The number of parameters added or modified. */
int mapper_msg_add_or_update_extra_params(table t,
                                          mapper_message_t *params);

/*! Prepare a lo_message for sending based on a vararg list of
 *  parameter pairs. */
void mapper_msg_prepare_varargs(lo_message m, va_list aq);

/*! Prepare a lo_message for sending based on a set of parameters. */
void mapper_msg_prepare_params(lo_message m,
                               mapper_message_t *params);

/*! Prepare a lo_message for sending based on a connection struct. */
void mapper_link_prepare_osc_message(lo_message m,
                                     mapper_router router);

/*! Prepare a lo_message for sending based on a connection struct. */
void mapper_connection_prepare_osc_message(lo_message m,
                                           mapper_connection c);

// Helper for setting property value from different lo_arg types
int propval_set_from_lo_arg(void *dest, const char dest_type,
                            lo_arg *src, const char src_type, int index);

/**** Expression parser/evaluator ****/

mapper_expr mapper_expr_new_from_string(const char *str,
                                        char input_type,
                                        char output_type,
                                        int input_vector_size,
                                        int output_vector_size);

int mapper_expr_input_history_size(mapper_expr expr);

int mapper_expr_output_history_size(mapper_expr expr);

int mapper_expr_num_variables(mapper_expr expr);

int mapper_expr_variable_history_size(mapper_expr expr, int index);

int mapper_expr_variable_vector_length(mapper_expr expr, int index);

#ifdef DEBUG
void printexpr(const char*, mapper_expr);
#endif

int mapper_expr_evaluate(mapper_expr expr,
                         mapper_signal_history_t *from_value,
                         mapper_signal_history_t **expr_vars,
                         mapper_signal_history_t *to_value,
                         char *typestring);

int mapper_expr_constant_output(mapper_expr expr);

void mapper_expr_free(mapper_expr expr);

/**** String tables ****/

/*! Create a new string table. */
table table_new();

/*! Free a string table.
 * \param t Table to free.
 * \param free_values Non-zero to free all values, otherwise zero. */
void table_free(table t, int free_values);

/*! Add a string to a table. */
void table_add(table t, const char *key, void *value, int is_mapper_prop);

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
int table_add_or_update(table t, const char *key, void *value);

#ifdef DEBUG
/*! Dump a table of OSC values. */
void table_dump_osc_values(table t);
#endif

/*! Add a typed OSC argument from a mapper_msg to a string table.
 *  \param t        Table to update.
 *  \param key      Key to store.
 *  \param type     OSC type of value to add.
 *  \param arg      Array of OSC values to add
 *  \param length   Number of OSC argument in array
 *  \return The number of table values added or modified. */
int mapper_table_add_or_update_msg_value(table t, const char *key,
                                         lo_type type, lo_arg **args,
                                         int length);

/*! Add a typed argument to a string table.
 *  \param t        Table to update.
 *  \param key      Key to store.
 *  \param type     OSC type of value to add.
 *  \param arg      Value(s) to add
 *  \param length   Number of OSC argument in array
 *  \return The number of table values added or modified. */
int mapper_table_add_or_update_typed_value(table t, const char *key,
                                           char type, void *args,
                                           int length);

/*! Add arguments contained in a string table to a lo_message */
void mapper_msg_add_value_table(lo_message m, table t);

/**** Clock synchronization ****/

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
inline static size_t msig_vector_bytes(mapper_signal sig)
{
    return mapper_type_size(sig->props.type) * sig->props.length;
}

/*! Helper to find the pointer to the current value in a mapper_signal_history_t. */
inline static void* msig_history_value_pointer(mapper_signal_history_t h)
{
    return h.value + h.position * h.length * mapper_type_size(h.type);
}

/*! Helper to find the pointer to the current timetag in a mapper_signal_history_t. */
inline static void* msig_history_tt_pointer(mapper_signal_history_t h)
{
    return &h.timetag[h.position];
}

#endif // __MAPPER_INTERNAL_H__
