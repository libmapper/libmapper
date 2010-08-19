
#ifndef __MAPPER_INTERNAL_H__
#define __MAPPER_INTERNAL_H__

#include "types_internal.h"
#include <mapper/mapper.h>

// Mapper internal functions

/**** Admin ****/

mapper_admin mapper_admin_new(const char *identifier,
                              mapper_device device, int initial_port);

void mapper_admin_free(mapper_admin admin);

void mapper_admin_poll(mapper_admin admin);

void mapper_admin_port_registered(mapper_admin admin);

void mapper_admin_name_registered(mapper_admin admin);

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
void mapper_admin_send_osc_with_params(mapper_admin admin,
                                       mapper_message_t *params,
                                       const char *path,
                                       const char *types, ...);

/***** Device *****/

void mdev_route_signal(mapper_device md, mapper_signal sig,
                       mapper_signal_value_t *value);

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

void mapper_router_send_signal(mapper_router router, mapper_signal sig,
                               mapper_signal_value_t *value);

void mapper_router_receive_signal(mapper_router router, mapper_signal sig,
                                  mapper_signal_value_t *value);

void mapper_router_add_mapping(mapper_router router, mapper_signal sig,
                               mapper_mapping mapping);

void mapper_router_remove_mapping(mapper_signal_mapping sm,
                                  mapper_mapping mapping);

mapper_mapping mapper_router_add_blank_mapping(mapper_router router,
                                               mapper_signal sig,
                                               const char *name);

mapper_mapping mapper_router_add_direct_mapping(mapper_router router,
                                      mapper_signal sig, const char *name);

void mapper_router_add_linear_range_mapping(mapper_router router,
                                            mapper_signal sig,
                                            const char *name,
                                            float src_min, float src_max,
                                            float dest_min,
                                            float dest_max);

void mapper_router_add_linear_scale_mapping(mapper_router router,
                                            mapper_signal sig,
                                            const char *name,
                                            float scale, float offset);

void mapper_router_add_calibrate_mapping(mapper_router router,
                                         mapper_signal sig,
                                         const char *name, float dest_min,
                                         float dest_max);

void mapper_router_add_expression_mapping(mapper_router router,
                                          mapper_signal sig,
                                          const char *name, char *expr);

/*! Find a router by target name in a linked list of routers. */
mapper_router mapper_router_find_by_target_name(mapper_router routers,
                                                const char* target_name);

int get_expr_Tree(Tree *T, const char *s);

/**** Signals ****/

void mval_add_to_message(lo_message m, mapper_signal sig,
                         mapper_signal_value_t *value);

/**** Mappings ****/

int mapper_mapping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value);
int mapper_clipping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value);

mapper_mapping mapper_mapping_find_by_names(mapper_device md,
                                                    const char* src_name,
                                                    const char* target_name);

/*! Set a mapping's properties based on message parameters. */
void mapper_mapping_set_from_message(mapper_mapping mapping,
                                     mapper_signal sig,
                                     mapper_message_t *msg);

void mapper_mapping_set_direct(mapper_mapping mapping);

void mapper_mapping_set_linear_range(mapper_mapping mapping,
                                     mapper_mapping_range_t *range);

void mapper_mapping_set_expression(mapper_mapping mapping,
                                   const char *expr);

void mapper_mapping_set_calibrate(mapper_mapping mapping,
                                  float dest_min, float dest_max);

/**** Local device database ****/

/*! Add or update an entry in the device database using parsed message
 *  parameters.
 *  \param name   The name of the device.
 *  \param params The parsed message parameters containing new device
 *                information.
 *  \return       Non-zero if device was added to the database, or
 *                zero if it was already present. */
int mapper_db_add_or_update_device_params(const char *name,
                                          mapper_message_t *params);

/*! Dump device information database to the screen.  Useful for
 *  debugging, only works when compiled in debug mode. */
void mapper_db_dump();

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
 *  function to ensure it only processes the apriori expected number
 *  of parameters.  (e.g., @range has 4 parameters.)
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

/*! Helper to return the scaling type from a message parameter.
 *  \param msg Structure containing parameter info.
 *  \return The scaling type, or -1 if not found. */
mapper_scaling_type mapper_msg_get_scaling(mapper_message_t *msg);

/*! Prepare a lo_message for sending based on a vararg list of
 *  parameter pairs. */
void mapper_msg_prepare_varargs(lo_message m, va_list aq);

/*! Prepare a lo_message for sending based on a set of parameters. */
void mapper_msg_prepare_params(lo_message m,
                               mapper_message_t *params);

/*! Prepare a lo_message for sending based on a mapping struct. */
void mapper_mapping_prepare_osc_message(lo_message m, mapper_mapping map);

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

#endif // __MAPPER_INTERNAL_H__
