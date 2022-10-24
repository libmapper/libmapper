
#ifndef __MAPPER_DEVICE_H__
#define __MAPPER_DEVICE_H__

#include "types_internal.h"
#include "util/mpr_inline.h"

/**** Debug macros ****/

/*! Debug tracer */
#ifdef __GNUC__
#ifdef DEBUG
#define trace_dev(DEV, ...)                                                         \
{                                                                                   \
    if (!DEV)                                                                       \
        printf("\x1B[32m-- <device>\x1B[0m ");                                      \
    else if (DEV->is_local && ((mpr_local_dev)DEV)->registered)                     \
        printf("\x1B[32m-- <device '%s'>\x1B[0m ", mpr_dev_get_name((mpr_dev)DEV)); \
    else                                                                            \
        printf("\x1B[32m-- <device '%s.?'::%p>\x1B[0m ", DEV->prefix, DEV);         \
    printf(__VA_ARGS__);                                                            \
}
#else /* !DEBUG */
#define trace_dev(...) {}
#endif /* DEBUG */
#else /* !__GNUC__ */
#define trace_dev(...) {};
#endif /* __GNUC__ */

int mpr_dev_set_from_msg(mpr_dev dev, mpr_msg msg);

void mpr_dev_manage_subscriber(mpr_local_dev dev, lo_address address, int flags,
                               int timeout_seconds, int revision);

/*! Return the list of inter-device links associated with a given device.
 *  \param dev          Device record query.
 *  \param dir          The direction of the link relative to the given device.
 *  \return             The list of results.  Use mpr_list_next() to iterate. */
mpr_list mpr_dev_get_links(mpr_dev dev, mpr_dir dir);

mpr_list mpr_dev_get_maps(mpr_dev dev, mpr_dir dir);

/*! Find information for a registered signal.
 *  \param dev          The device to query.
 *  \param sig_name     Name of the signal to find in the graph.
 *  \return             Information about the signal, or zero if not found. */
mpr_sig mpr_dev_get_sig_by_name(mpr_dev dev, const char *sig_name);

mpr_id mpr_dev_get_unused_sig_id(mpr_local_dev dev);

int mpr_dev_add_link(mpr_dev dev, mpr_dev rem);
void mpr_dev_remove_link(mpr_dev dev, mpr_dev rem);

int mpr_dev_handler(const char *path, const char *types, lo_arg **argv, int argc,
                    lo_message msg, void *data);

int mpr_dev_bundle_start(lo_timetag t, void *data);

MPR_INLINE static void mpr_dev_LID_incref(mpr_local_dev dev, mpr_id_map map)
{
    ++map->LID_refcount;
}

MPR_INLINE static void mpr_dev_GID_incref(mpr_local_dev dev, mpr_id_map map)
{
    ++map->GID_refcount;
}

int mpr_dev_LID_decref(mpr_local_dev dev, int group, mpr_id_map map);

int mpr_dev_GID_decref(mpr_local_dev dev, int group, mpr_id_map map);

void init_dev_prop_tbl(mpr_dev dev);

void mpr_dev_on_registered(mpr_local_dev dev);

void mpr_dev_add_sig_methods(mpr_local_dev dev, mpr_local_sig sig);

void mpr_dev_remove_sig_methods(mpr_local_dev dev, mpr_local_sig sig);

mpr_id_map mpr_dev_add_idmap(mpr_local_dev dev, int group, mpr_id LID, mpr_id GID);

mpr_id_map mpr_dev_get_idmap_by_LID(mpr_local_dev dev, int group, mpr_id LID);

mpr_id_map mpr_dev_get_idmap_by_GID(mpr_local_dev dev, int group, mpr_id GID);

const char *mpr_dev_get_name(mpr_dev dev);

void mpr_dev_send_state(mpr_dev dev, net_msg_t cmd);

int mpr_dev_send_maps(mpr_local_dev dev, mpr_dir dir, int msg);

/*! Find information for a registered link.
 *  \param dev          Device record to query.
 *  \param remote       Remote device.
 *  \return             Information about the link, or zero if not found. */
mpr_link mpr_dev_get_link_by_remote(mpr_local_dev dev, mpr_dev remote);


#endif /* __MAPPER_DEVICE_H__ */
