
#ifndef __MPR_TABLE_H__
#define __MPR_TABLE_H__
#define __MPR_TYPES_H__

typedef struct _mpr_tbl *mpr_tbl;
typedef struct _mpr_tbl_record *mpr_tbl_record;

#include "message.h"

/* bit flags for tracking permissions for modifying properties */
#define MOD_NONE        0x00    /* 00000000 */
#define MOD_LOCAL       0x01    /* 00000001 */
#define MOD_REMOTE      0x02    /* 00000010 */
#define MOD_ANY         0x03    /* 00000011 */
#define LOCAL_ACCESS    0x04    /* 00000100 */
#define MUTABLE_TYPE    0x08    /* 00001000 */
#define MUTABLE_LENGTH  0x10    /* 00010000 */
#define INDIRECT        0x20    /* 00100000 */
#define PROP_OWNED      0x40    /* 01000000 */
#define PROP_SET        0x80    /* 10000000 */

/*! Create a new string table. */
mpr_tbl mpr_tbl_new(void);

/*! Clear the contents of a string table.
 * \param tbl Table to free. */
void mpr_tbl_clear(mpr_tbl tbl);

/*! Free a string table.
 * \param tbl Table to free. */
void mpr_tbl_free(mpr_tbl tbl);

/*! Get the number of records stored in a table. */
int mpr_tbl_get_num_records(mpr_tbl tbl);

/*! Look up a value in a table by key.  Returns the property id and fills in len, type,
 *  val, and pub if found. Returns `MPR_PROP_UNKNOWN` if not found.
 *  \param tbl          Table to query.
 *  \param key          The name of the property to retrieve.
 *  \param len          A pointer to a location to receive the vector length of
 *                      the property value (Optional, pass `0` to ignore).
 *  \param type         A pointer to a location to receive the type of the
 *                      property value (Optional, pass `0` to ignore).
 *  \param val          A pointer to a location to receive the address of the
 *                      property's value (Optional, pass `0` to ignore).
 *  \param pub          A pointer to a location to receive the public flag for this property
 *                      (Optional, pass `0` to ignore).
 *  \return             Symbolic identifier of the retrieved property, or
 *                      `MPR_PROP_UNKNOWN` if not found. */
mpr_prop mpr_tbl_get_record_by_key(mpr_tbl tbl, const char *key, int *len,
                                   mpr_type *type, const void **val, int *pub);

/*! Look up a property by index or one of the symbolic identifiers listed in `mpr_constants.h`.
 *  Returns the property id and fills in len, type, val, and pub if found.
 *  Returns `MPR_PROP_UNKNOWN` if not found.
 *  \param tbl          Table to query.
 *  \param prop         Index or symbolic identifier of the property to retrieve.
 *  \param key          A pointer to a location to receive the name of the
 *                      property value (Optional, pass `0` to ignore).
 *  \param len          A pointer to a location to receive the vector length of
 *                      the property value (Optional, pass `0` to ignore).
 *  \param type         A pointer to a location to receive the type of the
 *                      property value (Optional, pass `0` to ignore).
 *  \param val          A pointer to a location to receive the address of the
 *                      property's value (Optional, pass `0` to ignore).
 *  \param pub          A pointer to a location to receive the public flag for this property
 *                      (Optional, pass `0` to ignore).
 *  \return             Symbolic identifier of the retrieved property, or
 *                      `MPR_PROP_UNKNOWN` if not found. */
mpr_prop mpr_tbl_get_record_by_idx(mpr_tbl tbl, int prop, const char **key, int *len,
                                   mpr_type *type, const void **val, int *pub);

/*! Remove a key-value pair from a table (by index or name). */
int mpr_tbl_remove_record(mpr_tbl tbl, mpr_prop prop, const char *key, int flags);

/*! Update a value in a table if the key already exists, or add it otherwise.
 *  Returns 0 if no add took place.  Sorts the table before exiting.
 *  \param tbl          Table to update.
 *  \param prop         Index to store.
 *  \param key          Key to store if not already indexed.
 *  \param type         OSC type of value to add.
 *  \param args         Value(s) to add
 *  \param len          Number of OSC argument in array
 *  \param flags        `MOD_LOCAL`, `MOD_REMOTE`, `MOD_ANY`, or `MOD_NONE`.
 *  \return             The number of table values added or modified. */
int mpr_tbl_add_record(mpr_tbl tbl, int prop, const char *key, int len,
                       mpr_type type, const void *args, int flags);

/*! Sync an existing value with a table. Records added using this method must
 *  be added in alphabetical order since `table_sort()` will not be called.
 *  Key and value will not be copied by the table, and will not be freed when
 *  the table is cleared or deleted. */
void mpr_tbl_link_value(mpr_tbl tbl, mpr_prop prop, int length, mpr_type type,
                        void *val, int flags);

/*! Sync an existing value with a table but do not mark is as updated.
 *  Records added using this method must
 *  be added in alphabetical order since `table_sort()` will not be called.
 *  Key and value will not be copied by the table, and will not be freed when
 *  the table is cleared or deleted. */
void mpr_tbl_link_value_no_default(mpr_tbl t, mpr_prop prop, int len,
                                   mpr_type type, void *val, int flags);

/*! Add a typed OSC argument from a `mpr_msg` to a string table.
 *  \param tbl      Table to update.
 *  \param atom     Message atom containing pointers to message key and value.
 *  \return         The number of table values added or modified. */
int mpr_tbl_add_record_from_msg_atom(mpr_tbl tbl, mpr_msg_atom atom, int flags);

#ifdef DEBUG
/*! Print a table of OSC values. */
void mpr_tbl_print_record(mpr_tbl_record rec);
void mpr_tbl_print(mpr_tbl tbl);
#endif

/*! Add arguments contained in a string table to a lo_message */
void mpr_tbl_add_to_msg(mpr_tbl tbl, mpr_tbl updates, lo_message msg);

/*! Clears and frees memory for removed records. This is not performed
 *  automatically by `mpr_tbl_remove()` in order to allow record
 *  removal to propagate to subscribed graph instances and peer devices. */
void mpr_tbl_clear_empty_records(mpr_tbl tbl);

int mpr_tbl_get_is_dirty(mpr_tbl tbl);

void mpr_tbl_set_is_dirty(mpr_tbl tbl, int is_dirty);

int mpr_tbl_get_prop_is_set(mpr_tbl tbl, mpr_prop prop);

void mpr_tbl_set_prop_is_set(mpr_tbl tbl, mpr_prop prop);

#endif /* __MPR_TABLE_H__ */
