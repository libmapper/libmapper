
#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef POSIX
#include <time.h>
#endif

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

//! Allocate and initialize a mapper device.
mapper_device md_new(const char *name_prefix)
{
    mapper_device md =
        (mapper_device)calloc(1, sizeof(mapper_device));
    md->name_prefix = strdup(name_prefix);
    md->admin = mapper_admin_new(name_prefix,
                                 MAPPER_DEVICE_SYNTH,
                                 8000);
    return md;
}

//! Free resources used by a mapper device.
void md_free(mapper_device md)
{
    if (md) {
        mapper_admin_free(md->admin);
        free(md);
    }
}

//! Register a signal with a mapper device.
void md_register_input(mapper_device device,
                       mapper_signal signal)
{
}

//! Unregister a signal with a mapper device.
void md_register_output(mapper_device device,
                        mapper_signal signal)
{
}
