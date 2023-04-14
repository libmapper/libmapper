
#ifndef __MPR_LINK_H__
#define __MPR_LINK_H__

typedef struct _mpr_link *mpr_link;

#include "device.h"
#include "graph.h"
#include "list.h"
#include "map.h"

#define MPR_LINK 0x20

/* TODO: replace this with something better */
#define LINK_LOCAL_DEV   0
#define LINK_REMOTE_DEV  1

size_t mpr_link_get_struct_size(void);

mpr_link mpr_link_new(mpr_local_dev local_dev, mpr_dev remote_dev);

/*! Return the list of maps associated with a given link.
 *  \param link         The link to check.
 *  \return             The list of results.  Use mpr_list_next() to iterate. */
mpr_list mpr_link_get_maps(mpr_link link);

int mpr_link_get_has_maps(mpr_link link, mpr_dir dir);

mpr_dev mpr_link_get_dev(mpr_link link, int idx);

void mpr_link_add_map(mpr_link link, mpr_map map);

void mpr_link_remove_map(mpr_link link, mpr_map map);

void mpr_link_init(mpr_link link, mpr_graph graph, mpr_dev dev1, mpr_dev dev2);

void mpr_link_connect(mpr_link link, const char *host, int admin_port, int data_port);

void mpr_link_free(mpr_link link);

int mpr_link_process_bundles(mpr_link link, mpr_time t);

void mpr_link_add_msg(mpr_link link, const char *path, lo_message msg, mpr_time t, mpr_proto proto);

int mpr_link_get_is_ready(mpr_link link);

lo_address mpr_link_get_admin_addr(mpr_link link);

void mpr_link_update_clock(mpr_link link, mpr_time then, mpr_time now,
                           int msg_id, int sent_id, double elapsed_remote);

int mpr_link_housekeeping(mpr_link link, mpr_time now);

int mpr_link_get_dev_dir(mpr_link link, mpr_dev dev);

#endif /* __MPR_LINK_H__ */
