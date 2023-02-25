
#ifndef __MPR_DEVICE_H__
#define __MPR_DEVICE_H__
#define __MPR_TYPES_H__

typedef struct _mpr_dev *mpr_dev;
typedef struct _mpr_local_dev *mpr_local_dev;

#include "object.h"
#include "expression.h"
#include "list.h"
#include "message.h"
#include "mpr_signal.h"
#include "mpr_time.h"
#include "network.h"
#include "id_map.h"
#include "util/mpr_inline.h"

typedef enum {
    SERVER_UDP = 0,
    SERVER_TCP = 1
} dev_server_t;

/**** Debug macros ****/

/*! Debug tracer */
#if defined(__GNUC__) || defined(WIN32)
#ifdef DEBUG
#define trace_dev(DEV, ...)                                                                   \
{                                                                                             \
    if (!DEV)                                                                                 \
        printf("\x1B[32m-- <device>\x1B[0m ");                                                \
    else if (mpr_dev_get_is_registered((mpr_dev)DEV))                                         \
        printf("\x1B[32m-- <device '%s'>\x1B[0m ", mpr_dev_get_name((mpr_dev)DEV));           \
    else                                                                                      \
        printf("\x1B[32m-- <device '%s?'::%p>\x1B[0m ", mpr_dev_get_name((mpr_dev)DEV), DEV); \
    printf(__VA_ARGS__);                                                                      \
}
#else /* !DEBUG */
#define trace_dev(...) {}
#endif /* DEBUG */
#else /* !__GNUC__ */
#define trace_dev(...) {};
#endif /* __GNUC__ */

void mpr_dev_free_mem(mpr_dev dev);

int mpr_dev_set_from_msg(mpr_dev dev, mpr_msg msg);

int mpr_dev_get_is_subscribed(mpr_dev dev);
void mpr_dev_set_is_subscribed(mpr_dev dev, int subscribed);

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

void mpr_dev_init(mpr_dev dev, int is_local, const char *name, mpr_id id);

void mpr_dev_on_registered(mpr_local_dev dev);

void mpr_local_dev_add_server_method(mpr_local_dev dev, const char *path,
                                     lo_method_handler h, void *data);

void mpr_dev_remove_sig(mpr_dev dev, mpr_sig sig);

void mpr_local_dev_remove_server_method(mpr_local_dev dev, const char *path);

const char *mpr_dev_get_name(mpr_dev dev);

void mpr_dev_send_state(mpr_dev dev, net_msg_t cmd);

int mpr_dev_send_maps(mpr_local_dev dev, mpr_dir dir, int msg);

/*! Find information for a registered link.
 *  \param dev          Device record to query.
 *  \param remote       Remote device.
 *  \return             Information about the link, or zero if not found. */
struct _mpr_link* mpr_dev_get_link_by_remote(mpr_local_dev dev, mpr_dev remote);

void mpr_dev_set_synced(mpr_dev dev, mpr_time time);

int mpr_dev_has_local_link(mpr_dev dev);

int mpr_dev_check_synced(mpr_dev dev, mpr_time time);

size_t mpr_dev_get_struct_size();

int mpr_dev_get_is_registered(mpr_dev dev);

lo_server mpr_local_dev_get_server(mpr_local_dev dev, dev_server_t idx);

int mpr_local_dev_get_bundle_idx(mpr_local_dev dev);

void mpr_local_dev_set_sending(mpr_local_dev dev);

void mpr_local_dev_set_receiving(mpr_local_dev dev);

int mpr_local_dev_has_subscribers(mpr_local_dev dev);

void mpr_local_dev_send_to_subscribers(mpr_local_dev dev, lo_bundle bundle, int msg_type,
                                       lo_server from);

void mpr_local_dev_handler_name(mpr_local_dev dev, const char *name,
                                int temp_id, int random_id, int hint);

void mpr_local_dev_probe_name(mpr_local_dev dev, mpr_net net);

void mpr_local_dev_handler_name_probe(mpr_local_dev dev, char *name, int temp_id,
                                     int random_id, mpr_id id);

void mpr_local_dev_restart_registration(mpr_local_dev dev, int start_ordinal);

int mpr_local_dev_check_registration(mpr_local_dev dev);

void mpr_local_dev_handler_logout(mpr_local_dev dev, mpr_dev remote, const char *prefix_str,
                                 int ordinal);

void mpr_local_dev_copy_net_servers(mpr_local_dev dev, lo_server *servers);

void mpr_dev_set_num_maps(mpr_dev dev, int num_maps_in, int num_maps_out);

void mpr_local_dev_add_sig(mpr_local_dev dev, mpr_local_sig sig, mpr_dir dir);

mpr_id_map mpr_dev_add_id_map(mpr_local_dev dev, int group, mpr_id LID, mpr_id GID);

mpr_id_map mpr_dev_get_id_map_by_LID(mpr_local_dev dev, int group, mpr_id LID);

mpr_id_map mpr_dev_get_id_map_by_GID(mpr_local_dev dev, int group, mpr_id GID);

int mpr_local_dev_get_num_id_maps(mpr_local_dev dev, int active);

#ifdef DEBUG
void mpr_local_dev_print_id_maps(mpr_local_dev dev);
#endif

#endif /* __MPR_DEVICE_H__ */
