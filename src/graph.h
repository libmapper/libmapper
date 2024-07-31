
#ifndef __MPR_GRAPH_H__
#define __MPR_GRAPH_H__

typedef struct _mpr_graph *mpr_graph;

#include "device.h"
#include "list.h"
#include "map.h"
#include "message.h"
#include "mpr_signal.h"
#include "network.h"
#include "object.h"

#define TIMEOUT_SEC 10  /* timeout after 10 seconds without ping */

/*! Find information for a registered device.
 *  \param g            The graph to query.
 *  \param name         Name of the device to find in the graph.
 *  \return             Information about the device, or zero if not found. */
mpr_dev mpr_graph_get_dev_by_name(mpr_graph g, const char *name);

mpr_map mpr_graph_get_map_by_names(mpr_graph g, int num_src, const char **srcs, const char *dst);

/*! Call registered graph callbacks for a given object type.
 *  \param g            The graph to query.
 *  \param o            The object to pass to the callbacks.
 *  \param t            The object type.
 *  \param e            The graph event type. */
void mpr_graph_call_cbs(mpr_graph g, mpr_obj o, mpr_type t, mpr_graph_evt e);

void mpr_graph_cleanup(mpr_graph g);

void mpr_graph_housekeeping(mpr_graph g);

mpr_link mpr_graph_add_link(mpr_graph g, mpr_dev dev1, mpr_dev dev2);

/*! Add or update a device entry in the graph using parsed message parameters.
 *  \param g            The graph to operate on.
 *  \param dev_name     The name of the device.
 *  \param msg          The parsed message parameters containing new metadata.
 *  \param force        1 to force addition of device, 0 to follow autosubscribe flags
 *  \return             Pointer to the device. */
mpr_dev mpr_graph_add_dev(mpr_graph g, const char *dev_name, mpr_msg msg, int force);

/*! Add or update a signal entry in the graph using parsed message parameters.
 *  \param g            The graph to operate on.
 *  \param sig_name     The name of the signal.
 *  \param dev_name     The name of the device associated with this signal.
 *  \param msg          The parsed message parameters containing new metadata.
 *  \return             Pointer to the signal. */
mpr_sig mpr_graph_add_sig(mpr_graph g, const char *sig_name, const char *dev_name, mpr_msg msg);

/*! Add or update a map entry in the graph using parsed message parameters.
 *  \param g            The graph to operate on.
 *  \param num_src      The number of source slots for this map
 *  \param src_names    The full names of the source signals.
 *  \param dst_name     The full name of the destination signal.
 *  \return             Pointer to the map. */
mpr_map mpr_graph_add_map(mpr_graph g, mpr_id id, int num_src, const char **src_names,
                          const char *dst_name);

/* TODO: use mpr_graph_remove_obj() instead? */

/*! Remove a device from the graph. */
void mpr_graph_remove_dev(mpr_graph g, mpr_dev dev, mpr_graph_evt evt);

/*! Remove a signal from the graph. */
void mpr_graph_remove_sig(mpr_graph g, mpr_sig sig, mpr_graph_evt evt);

/*! Remove a link from the graph. */
void mpr_graph_remove_link(mpr_graph g, mpr_link link, mpr_graph_evt evt);

/*! Remove a map from the graph. */
void mpr_graph_remove_map(mpr_graph g, mpr_map map, mpr_graph_evt evt);

/*! Print graph contents to the screen.  Useful for debugging, only works when
 *  compiled in debug mode. */
void mpr_graph_print(mpr_graph g);

int mpr_graph_subscribed_by_sig(mpr_graph g, const char *name);

void mpr_graph_free_cbs(mpr_graph g);

mpr_list mpr_graph_new_query(mpr_graph g, int allow_empty, int obj_type,
                             const void *func, const char *types, ...);

void mpr_graph_set_owned(mpr_graph g, int own);

mpr_net mpr_graph_get_net(mpr_graph g);

int mpr_graph_get_owned(mpr_graph g);

mpr_obj mpr_graph_add_obj(mpr_graph g, int obj_type, int is_local);

int mpr_graph_generate_unique_id(mpr_graph g);

void mpr_graph_sync_dev(mpr_graph g, const char *name);

int mpr_graph_get_autosub(mpr_graph g);

mpr_expr_eval_buffer mpr_graph_get_expr_eval_buffer(mpr_graph g);

void mpr_graph_reset_obj_statuses(mpr_graph g);

#endif /* __MPR_GRAPH_H__ */
