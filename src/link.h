
#ifndef __MAPPER_LINK_H__
#define __MAPPER_LINK_H__

mpr_link mpr_link_new(mpr_local_dev local_dev, mpr_dev remote_dev);

/*! Return the list of maps associated with a given link.
 *  \param link         The link to check.
 *  \return             The list of results.  Use mpr_list_next() to iterate. */
mpr_list mpr_link_get_maps(mpr_link link);

void mpr_link_add_map(mpr_link link, int is_src);

void mpr_link_remove_map(mpr_link link, mpr_local_map rem);

void mpr_link_init(mpr_link link);

void mpr_link_connect(mpr_link link, const char *host, int admin_port,
                      int data_port);

void mpr_link_free(mpr_link link);

int mpr_link_process_bundles(mpr_link link, mpr_time t, int idx);

void mpr_link_add_msg(mpr_link link, mpr_sig dst, lo_message msg, mpr_time t,
                      mpr_proto proto, int idx);

int mpr_link_get_is_local(mpr_link link);

#endif /* __MAPPER_LINK_H__ */
