#ifndef __MAPPER_TYPES_H__
#define __MAPPER_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

/*! This file defines opaque types that are used internally in libmapper. */

/*! Abstraction for accessing any mapper object type. */
typedef void *mapper_object;

/*! An internal structure defining a mapper device. */
typedef void *mapper_device;

//! An internal structure defining a mapper signal.
typedef void *mapper_signal;

//! An internal structure defining a mapping between a set of signals.
typedef void *mapper_map;

//! An internal structure to handle network database.
//! This can be retrieved by calling mapper_object_graph().
typedef void *mapper_graph;

//! An internal structure defining a grouping of mapper signals.
typedef int mapper_signal_group;

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_TYPES_H__
