#ifndef __MAPPER_TYPES_H__
#define __MAPPER_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

/*! \file This file defines opaque types that are used internally in
 *        libmapper. */

//! An internal structure defining a mapper device.
typedef void *mapper_device;

//! An internal structure defining a mapper network monitor.
typedef void *mapper_monitor;

//! An internal structure defining an object to handle the admin bus.
typedef void *mapper_admin;

//! An internal structure to handle network database.
//! This should be retrieved by calling mapper_monitor_get_db().
typedef void *mapper_db;

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_TYPES_H__
