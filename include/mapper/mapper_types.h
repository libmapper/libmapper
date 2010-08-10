#ifndef __MAPPER_TYPES_H__
#define __MAPPER_TYPES_H__

#ifdef _cplusplus
extern "C" {
#endif

/*! \file This file defines opaque types that are used internally in
 *        libmapper. */

//! An internal structure defining a mapper device.
typedef void *mapper_device;

//! An internal structure defining a mapper network monitor.
typedef void *mapper_monitor;

#ifdef _cplusplus
}
#endif

#endif // __MAPPER_TYPES_H__
