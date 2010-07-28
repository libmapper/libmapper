
#ifndef __MAPPER_INTERNAL_H__
#define __MAPPER_INTERNAL_H__

#include "types_internal.h"
#include <mapper/mapper.h>

// Mapper internal functions

/**** Admin ****/

mapper_admin mapper_admin_new(const char *identifier,
                              mapper_device device,
                              mapper_device_type_t type,
                              int initial_port);

void mapper_admin_free(mapper_admin admin);

void mapper_admin_poll(mapper_admin admin);

void mapper_admin_port_announce(mapper_admin admin);

void mapper_admin_name_announce(mapper_admin admin);

/***** Device *****/
void mdev_add_REGIST_DEVICES_INFO( char *full_name, char *host,	int port, char *canAlias);

void mdev_route_signal(mapper_device md, mapper_signal sig,
                       mapper_signal_value_t *value);

void mdev_add_router(mapper_device md, mapper_router rt);

void mdev_remove_router(mapper_device md, mapper_router rt);

void mdev_start_server(mapper_device mdev);

void mdev_on_port_and_ordinal(mapper_device md,
                              mapper_admin_allocated_t *resource);

int mdev_name(mapper_device md, char *name, int len);

/***** Router *****/

mapper_router mapper_router_new(const char *host, int port, char *name);

void mapper_router_free(mapper_router router);

void mapper_router_send_signal(mapper_router router, mapper_signal sig,
                               mapper_signal_value_t *value);

void mapper_router_receive_signal(mapper_router router, mapper_signal sig,
                                  mapper_signal_value_t *value);

void mapper_router_add_mapping(mapper_router router, mapper_signal sig,
                               mapper_mapping mapping);

void mapper_router_remove_mapping(mapper_signal_mapping sm, mapper_mapping mapping);

void mapper_router_add_direct_mapping(mapper_router router, mapper_signal sig,
                                      const char *name,float src_min, float src_max, float dest_min, float dest_max);

void mapper_router_add_linear_mapping(mapper_router router, mapper_signal sig,
                                    const char *name, /*mapper_signal_value_t scale,*/ char * expr,float src_min, float src_max, float dest_min, float dest_max);

void mapper_router_add_calibrate_mapping(mapper_router router, mapper_signal sig,
									  const char *name, /*mapper_signal_value_t scale,*/ char * expr,float src_min, float src_max, float dest_min, float dest_max);

void mapper_router_add_expression_mapping(mapper_router router, mapper_signal sig,
					  const char *name,char * expr,float src_min, float src_max, float dest_min, float dest_max);

/**** Signals ****/

void mval_add_to_message(lo_message m, mapper_signal sig,
                         mapper_signal_value_t *value);

/**** Mappings ****/

void mapper_mapping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value);

/**** Debug macros ****/

/*! Debug tracer */
#ifdef DEBUG
#ifdef __GNUC__
#include <stdio.h>
#define trace(...) { printf("-- " __VA_ARGS__); }
#else
static void trace(...) {};
#endif
#else
#ifdef __GNUC__
#define trace(...) {}
#else
static void trace(...) {};
#endif
#endif

#endif // __MAPPER_INTERNAL_H__
