
#ifndef __MAPPER_INTERNAL_H__
#define __MAPPER_INTERNAL_H__

#include "types_internal.h"
#include <mapper/mapper.h>

/**** Signals ****/

/*! A signal is defined as a vector of values, along with some
 *  metadata. */

struct _mapper_signal
{
    /*! Properties of this signal. */
    mapper_db_signal_t props;

    /*! The device associated with this signal. */
    struct _mapper_device *device;

    /*! The current value of this signal. */
    void *value;

    /*! The timetag associated with the current value of this
     *  signal. */
    mapper_timetag_t value_tt;

    /*! An optional function to be called when the signal value
     *  changes. */
    mapper_signal_handler *handler;
};

// Mapper internal functions

/**** Admin ****/

void mapper_admin_add_device(mapper_admin admin, mapper_device dev,
                             const char *identifier, int initial_port);

void mapper_admin_add_monitor(mapper_admin admin, mapper_monitor mon);

int mapper_admin_poll(mapper_admin admin);

void mapper_admin_port_probe(mapper_admin admin);

void mapper_admin_name_probe(mapper_admin admin);

/* A macro allow tracing bad usage of this function. */
#define mapper_admin_name(admin)                        \
    _real_mapper_admin_name(admin, __FILE__, __LINE__)

/* The real function, don't call directly. */
const char *_real_mapper_admin_name(mapper_admin admin,
                                    const char *file, unsigned int line);

/*! Macro for calling message-sending function. */
#define mapper_admin_send_osc(...)                  \
    _real_mapper_admin_send_osc(__VA_ARGS__, N_AT_PARAMS)

/*! Message-sending function, not to be called directly. */
void _real_mapper_admin_send_osc(mapper_admin admin, const char *path,
                                 const char *types, ...);

/*! Message-sending function which appends a parameter list at the end. */
void _real_mapper_admin_send_osc_with_params(const char *file, int line,
                                             mapper_admin admin,
                                             mapper_message_t *params,
                                             mapper_string_table_t *extra,
                                             const char *path,
                                             const char *types, ...);

#define mapper_admin_send_osc_with_params(...)                          \
    _real_mapper_admin_send_osc_with_params(__FILE__, __LINE__,         \
                                            __VA_ARGS__,                \
                                            LO_MARKER_A, LO_MARKER_B)

/***** Device *****/

void mdev_add_signal_query_response_callback(mapper_device md,
                                             mapper_signal sig);

void mdev_remove_signal_query_response_callback(mapper_device md,
                                                mapper_signal sig);

void mdev_route_signal(mapper_device md, mapper_signal sig,
                       mapper_signal_value_t *value);

int mdev_route_query(mapper_device md, mapper_signal sig);

void mdev_add_router(mapper_device md, mapper_router rt);

void mdev_remove_router(mapper_device md, mapper_router rt);

void mdev_start_server(mapper_device mdev);

void mdev_on_port_and_ordinal(mapper_device md,
                              mapper_admin_allocated_t *resource);

const char *mdev_name(mapper_device md);

/***** Router *****/

mapper_router mapper_router_new(mapper_device device, const char *host,
                                int port, const char *name);

void mapper_router_free(mapper_router router);

/*! Set a router's properties based on message parameters. */
void mapper_router_set_from_message(mapper_router router,
                                    mapper_message_t *msg);

void mapper_router_send_signal(mapper_router router, mapper_signal sig,
                               mapper_signal_value_t *value);

void mapper_router_receive_signal(mapper_router router, mapper_signal sig,
                                  mapper_signal_value_t *value);

int mapper_router_send_query(mapper_router router, mapper_signal sig);

mapper_connection mapper_router_add_connection(mapper_router router,
                                               mapper_signal sig,
                                               const char *dest_name,
                                               char dest_type,
                                               int dest_length);

int mapper_router_remove_connection(mapper_router router,
                                    mapper_connection connection);

/*! Find a router by destination name in a linked list of routers. */
mapper_router mapper_router_find_by_dest_name(mapper_router routers,
                                                const char* dest_name);

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
 *  \param handler Function to be called when the value of the
 *                 signal is updated.
 *  \param user_data User context pointer to be passed to handler. */
mapper_signal msig_new(const char *name, int length, char type,
                       int is_output, const char *unit,
                       void *minimum, void *maximum,
                       mapper_signal_handler *handler, void *user_data);

/*! Free memory used by a mapper_signal. Call this only for signals
 *  that are not registered with a device. Registered signals will be
 *  freed by mdev_free().
 *  \param sig The signal to free. */
void msig_free(mapper_signal sig);

void mval_add_to_message(lo_message m, mapper_signal sig,
                         mapper_signal_value_t *value);

/**** connections ****/

/*! Perform the connection from a value vector to a scalar.  The
 *  result of this operation should be sent to the destination.
 *  \param connection The mapping process to perform.
 *  \param sig        The signal to operate on.
 *  \param from_value Pointer to first value in a vector of the
 *                    expected size.
 *  \param to_value   Pointer to a value to receive the scalar result.
 *  \return Zero if the operation was muted, or one if it was performed. */
int mapper_connection_perform(mapper_connection connection,
                              mapper_signal sig,
                              mapper_signal_value_t *from_value,
                              mapper_signal_value_t *to_value);

int mapper_clipping_perform(mapper_connection connection,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value);

mapper_connection mapper_connection_find_by_names(mapper_device md,
                                                  const char* src_name,
                                                  const char* dest_name);

/*! Set a connection's properties based on message parameters. */
void mapper_connection_set_from_message(mapper_connection connection,
                                        mapper_signal sig,
                                        mapper_message_t *msg);

void mapper_connection_set_direct(mapper_connection connection);

void mapper_connection_set_linear_range(mapper_connection connection,
                                        mapper_signal sig,
                                        mapper_connection_range_t *range);

void mapper_connection_set_expression(mapper_connection connection,
                                      mapper_signal sig,
                                      const char *expr);

void mapper_connection_set_calibrate(mapper_connection connection,
                                     mapper_signal sig,
                                     float dest_min, float dest_max);

const char *mapper_get_clipping_type_string(mapper_clipping_type clipping);

const char *mapper_get_mode_type_string(mapper_mode_type mode);

/**** Local device database ****/

/*! Add or update an entry in the device database using parsed message
 *  parameters.
 *  \param db          The database to operate on.
 *  \param device_name The name of the device.
 *  \param params      The parsed message parameters containing new device
 *                     information.
 *  \return            Non-zero if device was added to the database, or
 *                     zero if it was already present. */
int mapper_db_add_or_update_device_params(mapper_db db,
                                          const char *device_name,
                                          mapper_message_t *params);

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
void mapper_db_remove_device(mapper_db db, const char *name);

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
 *  of parameters.  (e.g., "@range" has 4 parameters.)
 *  \param msg    Structure containing parameter info.
 *  \param param  Symbolic identifier of the parameter to look for.
 *  \return       String containing type of each parameter argument.
 */
const char* mapper_msg_get_type(mapper_message_t *msg,
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

/*! Helper to return the clipping type from a message parameter.
 *  \param msg Structure containing parameter info.
 *  \param param Either AT_CLIPMIN or AT_CLIPMAX.
 *  \return The clipping type, or -1 if not found. */
mapper_clipping_type mapper_msg_get_clipping(mapper_message_t *msg,
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

void mapper_msg_add_or_update_extra_params(table t,
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

/**** Expression parser/evaluator ****/

mapper_expr mapper_expr_new_from_string(const char *str,
                                        int input_is_float,
                                        int output_is_float,
                                        int vector_size);

#ifdef DEBUG
void printexpr(const char*, mapper_expr);
#endif

mapper_signal_value_t mapper_expr_evaluate(
    mapper_expr expr, mapper_signal_value_t* input_vector);

void mapper_expr_free(mapper_expr expr);

/**** String tables ****/

/*! Create a new string table. */
table table_new();

/*! Free a string table.
 * \param t Table to free.
 * \param free_values Non-zero to free all values, otherwise zero. */
void table_free(table t, int free_values);

/*! Add a string to a table. */
void table_add(table t, const char *key, void *value);

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

/*! Add a typed OSC argument to a string table. */
void mapper_table_add_or_update_osc_value(table t, const char *key,
                                          lo_type type, lo_arg *arg);

/*! Add OSC arguments contained in a string table to a lo_message */
void mapper_msg_add_osc_value_table(lo_message m, table t);

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
    mapper_signal_value_t v;
    switch (type) {
    case 'i': return sizeof(v.i32);
    case 'f': return sizeof(v.f);
    case 'd': return sizeof(v.d);
    default:
        die_unless(0, "getting size of unknown type %c\n", type);
        return 0;
    }
}

/*! Helper to find the size in bytes of a signal's full vector. */
inline static int msig_vector_bytes(mapper_signal sig)
{
    return mapper_type_size(sig->props.type) * sig->props.length;
}

#endif // __MAPPER_INTERNAL_H__
