
#ifndef __MPR_TABLE_H__
#define __MPR_TABLE_H__

#include "message.h"

/* bit flags for tracking permissions for modifying properties */
#define NON_MODIFIABLE      0x00
#define LOCAL_MODIFY        0x01
#define REMOTE_MODIFY       0x02
#define MODIFIABLE          0x03
#define LOCAL_ACCESS_ONLY   0x04
#define MUTABLE_TYPE        0x08
#define MUTABLE_LENGTH      0x10
#define INDIRECT            0x20
#define PROP_OWNED          0x40
#define PROP_DIRTY          0x80

/*! Used to hold look-up table records. */
typedef struct {
    const char *key;
    void **val;
    int len;
    mpr_prop prop;
    mpr_type type;
    char flags;
} mpr_tbl_record_t, *mpr_tbl_record;

/*! Used to hold look-up tables. */
typedef struct _mpr_tbl {
    mpr_tbl_record rec;
    int count;
    int alloced;
    char dirty;
} mpr_tbl_t, *mpr_tbl;

/*! Create a new string table. */
mpr_tbl mpr_tbl_new(void);

/*! Clear the contents of a string table.
 * \param tab Table to free. */
void mpr_tbl_clear(mpr_tbl tab);

/*! Free a string table.
 * \param tab Table to free. */
void mpr_tbl_free(mpr_tbl tab);

/*! Get the number of records stored in a table. */
int mpr_tbl_get_size(mpr_tbl tab);

/*! Look up a value in a table.  Returns 0 if found, 1 if not found,
 *  and fills in value if found. */
mpr_tbl_record mpr_tbl_get(mpr_tbl tab, mpr_prop prop, const char *key);

mpr_prop mpr_tbl_get_prop_by_key(mpr_tbl tab, const char *key, int *len,
                                 mpr_type *type, const void **val, int *pub);

mpr_prop mpr_tbl_get_prop_by_idx(mpr_tbl tab, int prop, const char **key, int *len,
                                 mpr_type *type, const void **val, int *pub);

/*! Remove a key-value pair from a table (by index or name). */
int mpr_tbl_remove(mpr_tbl tab, mpr_prop prop, const char *key, int flags);

/*! Update a value in a table if the key already exists, or add it otherwise.
 *  Returns 0 if no add took place.  Sorts the table before exiting.
 *  \param tab          Table to update.
 *  \param prop         Index to store.
 *  \param key          Key to store if not already indexed.
 *  \param type         OSC type of value to add.
 *  \param args         Value(s) to add
 *  \param len          Number of OSC argument in array
 *  \param flags        LOCAL_MODIFY, REMOTE_MODIFY, NON_MODIFABLE.
 *  \return             The number of table values added or modified. */
int mpr_tbl_set(mpr_tbl tab, int prop, const char *key, int len,
                mpr_type type, const void *args, int flags);

/*! Sync an existing value with a table. Records added using this method must
 *  be added in alphabetical order since table_sort() will not be called.
 *  Key and value will not be copied by the table, and will not be freed when
 *  the table is cleared or deleted. */
void mpr_tbl_link(mpr_tbl tab, mpr_prop prop, int length, mpr_type type,
                  void *val, int flags);

/*! Add a typed OSC argument from a mpr_msg to a string table.
 *  \param tab      Table to update.
 *  \param atom     Message atom containing pointers to message key and value.
 *  \return         The number of table values added or modified. */
int mpr_tbl_set_from_atom(mpr_tbl tab, mpr_msg_atom atom, int flags);

#ifdef DEBUG
/*! Print a table of OSC values. */
void mpr_tbl_print_record(mpr_tbl_record rec);
void mpr_tbl_print(mpr_tbl tab);
#endif

/*! Add arguments contained in a string table to a lo_message */
void mpr_tbl_add_to_msg(mpr_tbl tab, mpr_tbl updates, lo_message msg);

/*! Clears and frees memory for removed records. This is not performed
 *  automatically by mpr_tbl_remove() in order to allow record
 *  removal to propagate to subscribed graph instances and peer devices. */
void mpr_tbl_clear_empty(mpr_tbl tab);

#endif /* __MPR_TABLE_H__ */
