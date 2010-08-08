
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
                                int port, char *name);

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

void mapper_router_add_direct_mapping(mapper_router router,
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

int get_expr_Tree(Tree *T, char *s);

/**** Signals ****/

void mval_add_to_message(lo_message m, mapper_signal sig,
                         mapper_signal_value_t *value);

/**** Mappings ****/

void mapper_mapping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value);

/**** Local device database ****/

/*! Add a device information instance to the local database. If it is
 *  already in the database, it is not added.
 *  \param full_name  The name of the device.
 *  \param host       The device's network host.
 *  \param port       The device's network port.
 *  \param canAlias   Boolean indicating the device's ability to alias
 *                    addresses.
 *  \return           Non-zero if device was added to the database, or
 *                    zero if it was already present. */
int mapper_db_add(char *full_name, char *host, int port, char *canAlias);

/*! Dump device information database to the screen.  Useful for
 *  debugging, only works when compiled in debug mode. */
void mapper_db_dump();

/*! Find information for a registered device.
 *  \param name Name of the device to find in the database.
 *  \return Information about the device, or zero if not found. */
mapper_db_registered mapper_db_find(char *name);

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

/*! Look up the value of a message parameter by symbolic idenfier.
 *  \param msg    Structure containing parameter info.
 *  \return       Pointer to lo_arg, or zero if not found. */
lo_arg** mapper_msg_get_param(mapper_message_t *msg,
                              mapper_msg_param_t param);

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
