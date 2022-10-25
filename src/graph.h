
#ifndef __MAPPER_GRAPH_H__
#define __MAPPER_GRAPH_H__

#include "types_internal.h"

typedef struct _mpr_graph {
    mpr_obj_t obj;                  /* always first */
    mpr_net_t net;
    mpr_list devs;                  /*!< List of devices. */
    mpr_list sigs;                  /*!< List of signals. */
    mpr_list maps;                  /*!< List of maps. */
    mpr_list links;                 /*!< List of links. */
    fptr_list callbacks;            /*!< List of object record callbacks. */

    /*! Linked-list of autorenewing device subscriptions. */
    mpr_subscription subscriptions;

    mpr_thread_data thread_data;

    /*! Flags indicating whether information on signals and mappings should
     *  be automatically subscribed to when a new device is seen.*/
    int autosub;

    int own;
    int staged_maps;

    uint32_t resource_counter;
} mpr_graph_t, *mpr_graph;

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
 *  \return             Pointer to the device. */
mpr_dev mpr_graph_add_dev(mpr_graph g, const char *dev_name, mpr_msg msg);

/*! Add or update a signal entry in the graph using parsed message parameters.
 *  \param g            The graph to operate on.
 *  \param sig_name     The name of the signal.
 *  \param dev_name     The name of the device associated with this signal.
 *  \param msg          The parsed message parameters containing new metadata.
 *  \return             Pointer to the signal. */
mpr_sig mpr_graph_add_sig(mpr_graph g, const char *sig_name,
                          const char *dev_name, mpr_msg msg);

/*! Add or update a map entry in the graph using parsed message parameters.
 *  \param g            The graph to operate on.
 *  \param num_src      The number of source slots for this map
 *  \param src_names    The full names of the source signals.
 *  \param dst_name     The full name of the destination signal.
 *  \return             Pointer to the map. */
mpr_map mpr_graph_add_map(mpr_graph g, mpr_id id, int num_src, const char **src_names,
                          const char *dst_name);

/*! Remove a device from the graph. */
void mpr_graph_remove_dev(mpr_graph g, mpr_dev dev, mpr_graph_evt evt, int quiet);

/*! Remove a signal from the graph. */
void mpr_graph_remove_sig(mpr_graph g, mpr_sig sig, mpr_graph_evt evt);

/*! Remove a link from the graph. */
void mpr_graph_remove_link(mpr_graph g, mpr_link link, mpr_graph_evt evt);

/*! Remove a map from the graph. */
void mpr_graph_remove_map(mpr_graph g, mpr_map map, mpr_graph_evt evt);

/*! Print graph contents to the screen.  Useful for debugging, only works when
 *  compiled in debug mode. */
void mpr_graph_print(mpr_graph g);

int mpr_graph_subscribed_by_dev(mpr_graph g, const char *name);

int mpr_graph_subscribed_by_sig(mpr_graph g, const char *name);

#endif /* __MAPPER_GRAPH_H__ */
