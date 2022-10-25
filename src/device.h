
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

#define MPR_DEV_STRUCT_ITEMS                                            \
    mpr_obj_t obj;      /* always first */                              \
    mpr_dev *linked;                                                    \
    char *prefix;       /*!< The identifier (prefix) for this device. */\
    char *name;         /*!< The full name for this device, or zero. */ \
    mpr_time synced;    /*!< Timestamp of last sync. */                 \
    int ordinal;                                                        \
    int num_inputs;     /*!< Number of associated input signals. */     \
    int num_outputs;    /*!< Number of associated output signals. */    \
    int num_maps_in;    /*!< Number of associated incoming maps. */     \
    int num_maps_out;   /*!< Number of associated outgoing maps. */     \
    int num_linked;     /*!< Number of linked devices. */               \
    int status;                                                         \
    uint8_t subscribed;                                                 \
    int is_local;

/*! A record that keeps information about a device. */
struct _mpr_dev {
    MPR_DEV_STRUCT_ITEMS
};

struct _mpr_local_dev {
    MPR_DEV_STRUCT_ITEMS

    lo_server servers[2];

    mpr_allocated_t ordinal_allocator;  /*!< A unique ordinal for this device instance. */
    int registered;                     /*!< Non-zero if this device has been registered. */

    int n_output_callbacks;

    mpr_subscriber subscribers;         /*!< Linked-list of subscribed peers. */

    struct {
        struct _mpr_id_map **active;    /*!< The list of active instance id maps. */
        struct _mpr_id_map *reserve;    /*!< The list of reserve instance id maps. */
    } idmaps;

    mpr_expr_stack expr_stack;
    mpr_thread_data thread_data;

    mpr_time time;
    int num_sig_groups;
    uint8_t time_is_stale;
    uint8_t polling;
    uint8_t bundle_idx;
    uint8_t sending;
    uint8_t receiving;
};

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
