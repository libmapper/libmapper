
#ifndef __MAPPER_INTERNAL_H__
#define __MAPPER_INTERNAL_H__

#include "types_internal.h"

// Mapper internal functions

/**** Admin ****/

mapper_admin mapper_admin_new(const char *identifier,
                              mapper_device_type_t type,
                              int initial_port);

void mapper_admin_free(mapper_admin admin);

void mapper_admin_poll(mapper_admin admin);

void mapper_admin_port_announce(mapper_admin admin);

void mapper_admin_name_announce(mapper_admin admin);

#endif // __MAPPER_INTERNAL_H__
