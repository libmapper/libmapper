
#ifndef __MPR_LIST_H__
#define __MPR_LIST_H__
#define __MPR_TYPES_H__

typedef struct _mpr_obj **mpr_list;

#include "mpr_type.h"

void *mpr_list_from_data(const void *data);

void *mpr_list_add_item(void **list, size_t size, int prepend);

void mpr_list_remove_item(void **list, void *item);

void mpr_list_free_item(void *item);

mpr_list mpr_list_new_query(const void **list, const void *func,
                            const mpr_type *types, ...);

mpr_list vmpr_list_new_query(const void **list, const void *func,
                             const mpr_type *types, va_list aq);

mpr_list mpr_list_start(mpr_list list);

#endif /* __MPR_LIST_H__ */
