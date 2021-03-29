#ifndef __MPR_TYPES_H__
#define __MPR_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

/*! This file defines opaque types that are used internally in libmapper. */

/*! Abstraction for accessing any object type. */
typedef void *mpr_obj;

/*! An internal structure defining a device. */
typedef void *mpr_dev;

/*! An internal structure defining a signal. */
typedef void *mpr_sig;

/*! An internal structure defining a mapping between a set of signals. */
typedef void *mpr_map;

/*! An internal structure defining a list of objects. */
typedef void **mpr_list;

/*! An internal structure representing the distributed mapping graph. */
/*! This can be retrieved by calling mpr_obj_graph(). */
typedef void *mpr_graph;

/*! An internal structure defining a grouping of signals. */
typedef int mpr_sig_group;

#ifdef __cplusplus
}
#endif

#endif /* __MPR_TYPES_H__ */
