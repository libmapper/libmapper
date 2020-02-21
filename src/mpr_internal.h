
#ifndef __MPR_INTERNAL_H__
#define __MPR_INTERNAL_H__

#include "types_internal.h"
#include <mpr/mpr.h>
#include <string.h>

/* Structs that refer to things defined in mpr.h are declared here instead
   of in types_internal.h */

#define RETURN_UNLESS(a, ...) { if (!(a)) { return __VA_ARGS__; }}
#define DONE_UNLESS(a) { if (!(a)) { goto done; }}
#define FUNC_IF(a, b) { if (b) { a(b); }}
#define PROP(NAME) MPR_PROP_##NAME

#if DEBUG
#define TRACE_RETURN_UNLESS(a, ret, ...) \
if (!(a)) { trace(__VA_ARGS__); return ret; }
#define TRACE_DEV_RETURN_UNLESS(a, ret, ...) \
if (!(a)) { trace_dev(dev, __VA_ARGS__); return ret; }
#else
#define TRACE_RETURN_UNLESS(a, ret, ...) if (!(a)) { return ret; }
#define TRACE_DEV_RETURN_UNLESS(a, ret, ...) if (!(a)) { return ret; }
#endif

/**** Subscriptions ****/
#ifdef DEBUG
void print_subscription_flags(int flags);
#endif

/**** Objects ****/
void mpr_obj_increment_version(mpr_obj obj);

#define MPR_LINK 0x20

/**** Signals ****/

#define MPR_MAX_VECTOR_LEN 128

/*! Get the full OSC name of a signal, including device name prefix.
 *  \param sig  The signal value to query.
 *  \param name A string to accept the name.
 *  \param len  The length of string pointed to by name.
 *  \return     The number of characters used, or 0 if error.  Note that in some
 *              cases the name may not be available. */
int mpr_sig_get_full_name(mpr_sig sig, char *name, int len);

int mpr_sig_set_from_msg(mpr_sig sig, mpr_msg msg);

void mpr_sig_update_timing_stats(mpr_sig sig, float diff);

/**** Devices ****/

int mpr_dev_set_from_msg(mpr_dev dev, mpr_msg msg);

void mpr_dev_manage_subscriber(mpr_dev dev, lo_address address, int flags,
                               int timeout_seconds, int revision);

/*! Return the list of inter-device links associated with a given device.
 *  \param dev          Device record query.
 *  \param dir          The direction of the link relative to the given device.
 *  \return             The list of results.  Use mpr_list_next() to iterate. */
mpr_list mpr_dev_get_links(mpr_dev dev, mpr_dir dir);

mpr_list mpr_dev_get_maps(mpr_dev dev, mpr_dir dir);

mpr_id mpr_dev_get_unused_sig_id(mpr_dev dev);

/**** Networking ****/

void mpr_net_add_dev(mpr_net n, mpr_dev d);

void mpr_net_remove_dev_methods(mpr_net n, mpr_dev d);

void mpr_net_poll(mpr_net n);

void mpr_net_init(mpr_net n, const char *iface, const char *group, int port);

void mpr_net_use_bus(mpr_net n);

void mpr_net_use_mesh(mpr_net n, lo_address addr);

void mpr_net_use_subscribers(mpr_net net, mpr_dev dev, int type);

void mpr_net_add_msg(mpr_net n, const char *str, net_msg_t cmd, lo_message msg);

void mpr_net_send(mpr_net n);

void mpr_net_free_msgs(mpr_net n);

void mpr_net_free(mpr_net n);

#define NEW_LO_MSG(VARNAME, FAIL)                   \
lo_message VARNAME = lo_message_new();              \
if (!VARNAME) {                                     \
    trace_net("couldn't allocate lo_message\n");    \
    FAIL;                                           \
}

/***** Device *****/

void init_dev_prop_tbl(mpr_dev dev);

void mpr_dev_on_registered(mpr_dev dev);

void mpr_dev_add_sig_methods(mpr_dev dev, mpr_sig sig);

void mpr_dev_remove_sig_methods(mpr_dev dev, mpr_sig sig);

int mpr_dev_route_query(mpr_dev dev, mpr_sig sig, mpr_time t);

void mpr_dev_release_scope(mpr_dev dev, const char *scope);

mpr_id_map mpr_dev_add_idmap(mpr_dev dev, int group, mpr_id LID, mpr_id GID);

void mpr_dev_remove_idmap(mpr_dev dev, int group, mpr_id_map map);

mpr_id_map mpr_dev_get_idmap_by_LID(mpr_dev dev, int group, mpr_id LID);

mpr_id_map mpr_dev_get_idmap_by_GID(mpr_dev dev, int group, mpr_id GID);

const char *mpr_dev_get_name(mpr_dev dev);

void mpr_dev_send_state(mpr_dev dev, net_msg_t cmd);

/*! Find information for a registered link.
 *  \param dev          Device record to query.
 *  \param remote       Remote device.
 *  \return             Information about the link, or zero if not found. */
mpr_link mpr_dev_get_link_by_remote(mpr_dev dev, mpr_dev remote);

/*! Look up information for a registered object using its unique id.
 *  \param g            The graph to query.
 *  \param type         The type of object to return.
 *  \param id           Unique id identifying the object to find in the graph.
 *  \return             Information about the object, or zero if not found. */
mpr_obj mpr_graph_get_obj(mpr_graph g, mpr_type type, mpr_id id);

/*! Find information for a registered device.
 *  \param g            The graph to query.
 *  \param name         Name of the device to find in the graph.
 *  \return             Information about the device, or zero if not found. */
mpr_dev mpr_graph_get_dev_by_name(mpr_graph g, const char *name);

mpr_map mpr_graph_get_map_by_names(mpr_graph g, int num_src, const char **srcs,
                                   const char *dst);

void mpr_graph_cleanup(mpr_graph g);

/*! Find information for a registered signal.
 *  \param dev          The device to query.
 *  \param sig_name     Name of the signal to find in the graph.
 *  \return             Information about the signal, or zero if not found. */
mpr_sig mpr_dev_get_sig_by_name(mpr_dev dev, const char *sig_name);

/***** Router *****/

void mpr_rtr_remove_sig(mpr_rtr r, mpr_rtr_sig rs);

void mpr_rtr_num_inst_changed(mpr_rtr r, mpr_sig s, int size);

/*! For a given signal instance, calculate mapping outputs and forward to
 *  destinations. */
void mpr_rtr_process_sig(mpr_rtr r, mpr_sig s, int inst_idx, const void *val,
                         mpr_time t);

int mpr_rtr_send_query(mpr_rtr r, mpr_sig s, mpr_time t);

void mpr_rtr_add_map(mpr_rtr r, mpr_map m);

void mpr_rtr_remove_link(mpr_rtr r, mpr_link l);

int mpr_rtr_remove_map(mpr_rtr r, mpr_map m);

/*! Find a mapping in a router by local signal and remote signal name. */
mpr_map mpr_rtr_get_map_out(mpr_rtr r, mpr_sig s, int num_src, const char **src,
                            const char *dst);

mpr_map mpr_rtr_get_map_in(mpr_rtr r, mpr_sig s, int num_src, const char **src);

mpr_map mpr_rtr_get_map_by_id(mpr_rtr r, mpr_sig s, mpr_id id, mpr_dir dir);

mpr_slot mpr_rtr_get_slot(mpr_rtr r, mpr_sig s, int slot_num);

int mpr_rtr_loop_check(mpr_rtr r, mpr_sig s, int n_remote, const char **remote);

/**** Signals ****/

/*! Free memory used by a mpr_sig. Call this only for signals that are not
 *  registered with a device. Registered signals will be freed by mpr_sig_free().
 *  \param s        The signal to free. */
void mpr_sig_free_internal(mpr_sig s);

void mpr_sig_send_state(mpr_sig s, net_msg_t cmd);

void mpr_sig_send_removed(mpr_sig s);

/**** Instances ****/

/*! Find an active instance with the given instance id.
 *  \param s        The signal owning the desired instance.
 *  \param GID      Globally unique id of this instance.
 *  \param flags    Bitflags indicating if search should include released instances.
 *  \return         The index of the retrieved signal instance, or -1 if no active
 *                  instances match the specified instance id map. */
int mpr_sig_find_inst_with_GID(mpr_sig s, mpr_id GID, int flags);

/*! Fetch a reserved (preallocated) signal instance using an instance id,
 *  activating it if necessary.
 *  \param s        The signal owning the desired instance.
 *  \param LID      The requested signal instance id.
 *  \param flags    Bitflags indicating if search should include released
 *                  instances.
 *  \param t        Time associated with this action.
 *  \return         The index of the retrieved signal instance, or -1 if no free
 *                  instances were available and allocation of a new instance
 *                  was unsuccessful according to the selected allocation
 *                  strategy. */
int mpr_sig_get_inst_with_LID(mpr_sig s, mpr_id LID, int flags, mpr_time *t);

/*! Fetch a reserved (preallocated) signal instance using instance id map,
 *  activating it if necessary.
 *  \param s        The signal owning the desired instance.
 *  \param GID      Globally unique id of this instance.
 *  \param flags    Bitflags indicating if search should include released instances.
 *  \param t        Time associated with this action.
 *  \return         The index of the retrieved signal instance, or NULL if no free
 *                  instances were available and allocation of a new instance
 *                  was unsuccessful according to the selected allocation
 *                  strategy. */
int mpr_sig_get_inst_with_GID(mpr_sig s, mpr_id GID, int flags, mpr_time *t);

/*! Release a specific signal instance. */
void mpr_sig_release_inst_internal(mpr_sig s, int inst_idx, mpr_time time);

/**** Links ****/

/*! Return the list of maps associated with a given link.
 *  \param link         The link to check.
 *  \return             The list of results.  Use mpr_list_next() to iterate. */
mpr_list mpr_link_get_maps(mpr_link link);


void mpr_link_init(mpr_link link);
void mpr_link_connect(mpr_link link, const char *host, int admin_port,
                      int data_port);
void mpr_link_free(mpr_link link);
void mpr_link_send(mpr_link link, net_msg_t cmd);
void mpr_link_start_queue(mpr_link link, mpr_time t);
void mpr_link_send_queue(mpr_link link, mpr_time t);

mpr_link mpr_graph_add_link(mpr_graph g, mpr_dev dev1, mpr_dev dev2);

int mpr_link_get_is_local(mpr_link link);

/**** Maps ****/

void mpr_hist_realloc(mpr_hist hist, int hist_size, int samp_size, int is_output);

mpr_slot mpr_map_get_slot_by_sig(mpr_map map, mpr_sig sig);

/*! Process the signal instance value according to mapping properties.
 *  The result of this operation should be sent to the destination.
 *  \param map          The mapping process to perform.
 *  \param typestring   Pointer to a string to receive types.
 *  \param time         Timestamp for this update.
 *  \param instance     Index of the signal instance to process.
 *  \return             Zero if the operation was muted, one if performed. */
int mpr_map_perform(mpr_map map, mpr_type *typestring, mpr_time *time, int inst);

lo_message mpr_map_build_msg(mpr_map map, mpr_slot slot, const void *val,
                             mpr_type *types, mpr_id_map idmap);

/*! Set a mapping's properties based on message parameters. */
int mpr_map_set_from_msg(mpr_map map, mpr_msg msg, int override);

/*! Helper for printing typed values.
 *  \param len          The vector length of the value.
 *  \param type         The value type.
 *  \param value        A pointer to the property value to print. */
void mpr_prop_print(int len, mpr_type type, const void *val);

mpr_prop mpr_prop_from_str(const char *str);
const char *mpr_prop_as_str(mpr_prop prop, int skip_slash);

const char *mpr_loc_as_str(mpr_loc loc);
mpr_loc mpr_loc_from_str(const char *string);

const char *mpr_protocol_as_str(mpr_proto pro);
mpr_proto mpr_protocol_from_str(const char *string);

const char *mpr_steal_as_str(mpr_steal_type stl);

int mpr_map_send_state(mpr_map map, int slot, net_msg_t cmd);

void mpr_map_init(mpr_map map);

void mpr_map_free(mpr_map map);

/**** Slot ****/

void mpr_slot_init(mpr_slot slot);

void mpr_slot_free(mpr_slot slot);

int mpr_slot_set_from_msg(mpr_slot slot, mpr_msg msg, int *status);

void mpr_slot_add_props_to_msg(lo_message msg, mpr_slot slot, int is_dest,
                               int staged);

void mpr_slot_upgrade_extrema_memory(mpr_slot slot);

int mpr_slot_match_full_name(mpr_slot slot, const char *full_name);

/**** Graph ****/

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

/*! Initialize an already-allocated mpr_sig structure. */
void mpr_sig_init(mpr_sig s, mpr_dir dir, const char *name, int len,
                  mpr_type type, const char *unit, const void *min,
                  const void *max, int *num_inst);

/*! Add or update a map entry in the graph using parsed message parameters.
 *  \param g            The graph to operate on.
 *  \param num_src      The number of source slots for this map
 *  \param src          The full names of the source signals.
 *  \param dst          The full name of the destination signal.
 *  \param msg          The parsed message parameters containing new metadata.
 *  \return             Pointer to the map. */
mpr_map mpr_graph_add_map(mpr_graph g, int num_src, const char **src,
                          const char *dst_name, mpr_msg msg);

/*! Remove objects in the provided query. */
void mpr_graph_remove_by_qry(mpr_graph g, mpr_list l, mpr_graph_evt e);

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

/*! Check device records for unresponsive devices. */
void mpr_graph_check_dev_status(mpr_graph g, uint32_t now_sec);

/*! Flush device records for unresponsive devices. */
mpr_dev mpr_graph_expired_dev(mpr_graph g, uint32_t last_ping);

int mpr_graph_subscribed_by_dev(mpr_graph g, const char *name);

int mpr_graph_subscribed_by_sig(mpr_graph g, const char *name);

/**** Messages ****/
/*! Parse the device and signal names from an OSC path. */
int mpr_parse_names(const char *string, char **devnameptr, char **signameptr);

/*! Parse a message based on an OSC path and named properties.
 *  \param argc     Number of arguments in the argv array.
 *  \param types    String containing message parameter types.
 *  \param argv     Vector of lo_arg structures.
 *  \return         A mpr_msg structure. Free when done using mpr_msg_free. */
mpr_msg mpr_msg_parse_props(int argc, const mpr_type *types, lo_arg **argv);

void mpr_msg_free(mpr_msg msg);

/*! Look up the value of a message parameter by symbolic identifier.
 *  \param msg      Structure containing parameter info.
 *  \param prop     Symbolic identifier of the property to look for.
 *  \return         Pointer to mpr_msg_atom, or zero if not found. */
mpr_msg_atom mpr_msg_get_prop(mpr_msg msg, mpr_prop prop);

void mpr_msg_add_typed_val(lo_message msg, int len, mpr_type type, const void *val);

/*! Prepare a lo_message for sending based on a map struct. */
const char *mpr_map_prepare_msg(mpr_map map, lo_message msg, int slot_idx);

/*! Helper for setting property value from different lo_arg types. */
int set_coerced_val(int src_len, mpr_type src_type, const void *src_val,
                    int dst_len, mpr_type dst_type, void *dst_val);

/**** Expression parser/evaluator ****/

mpr_expr mpr_expr_new_from_str(const char *str, int num_in, const mpr_type *in_types,
                               const int *in_vec_lens, mpr_type out_type, int out_vec_len);

int mpr_expr_get_in_hist_size(mpr_expr expr, int idx);

int mpr_expr_get_out_hist_size(mpr_expr expr);

int mpr_expr_get_num_vars(mpr_expr expr);

int mpr_expr_get_var_hist_size(mpr_expr expr, int idx);

int mpr_expr_get_var_vec_len(mpr_expr expr, int idx);

int mpr_expr_get_src_is_muted(mpr_expr expr, int idx);

const char *mpr_expr_get_var_name(mpr_expr expr, int idx);

int mpr_expr_get_var_is_public(mpr_expr expr, int idx);

#ifdef DEBUG
void printexpr(const char*, mpr_expr);
#endif

int mpr_expr_eval(mpr_expr expr, mpr_hist *srcs, mpr_hist *expr_vars,
                  mpr_hist result, mpr_time *t, mpr_type *types);

int mpr_expr_get_num_input_slots(mpr_expr expr);

void mpr_expr_free(mpr_expr expr);

/**** String tables ****/

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

int mpr_tbl_get_prop_by_key(mpr_tbl tab, const char *key, int *len,
                            mpr_type *type, const void **val, int *pub);

int mpr_tbl_get_prop_by_idx(mpr_tbl tab, mpr_prop prop, const char **key,
                            int *len, mpr_type *type, const void **val, int *pub);

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
 *  \param flags        MPR_LOCAL_MODIFY, MPR_REMOTE_MODIFY, MPR_NON_MODIFABLE.
 *  \return             The number of table values added or modified. */
int mpr_tbl_set(mpr_tbl tab, mpr_prop prop, const char *key, int len,
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

/**** Lists ****/

void *mpr_list_from_data(const void *data);

void *mpr_list_add_item(void **list, size_t size);

void mpr_list_remove_item(void **list, void *item);

void mpr_list_free_item(void *item);

mpr_list mpr_list_new_query(const void **list, const void *func,
                            const mpr_type *types, ...);

mpr_list mpr_list_start(mpr_list list);

/**** Time ****/

/*! Get the current time. */
double mpr_get_current_time(void);

/*! Return the difference in seconds between two mpr_times.
 *  \param minuend      The minuend.
 *  \param subtrahend   The subtrahend.
 *  \return             The difference a-b in seconds. */
double mpr_time_get_diff(mpr_time minuend, mpr_time subtrahend);

/**** Debug macros ****/

/*! Debug tracer */
#ifdef DEBUG
#ifdef __GNUC__
#include <stdio.h>
#include <assert.h>
#define trace(...) { printf("-- " __VA_ARGS__); }
#define trace_graph(...)  { printf("\x1B[31m-- <graph>\x1B[0m " __VA_ARGS__);}
#define trace_dev(DEV, ...)                                                 \
{                                                                           \
    if (!DEV)                                                               \
        printf("\x1B[32m-- <device>\x1B[0m ");                              \
    else if (DEV->loc && DEV->loc->registered)                              \
        printf("\x1B[32m-- <device '%s'>\x1B[0m ", mpr_dev_get_name(DEV));  \
    else                                                                    \
        printf("\x1B[32m-- <device '%s.?'::%p>\x1B[0m ", DEV->prefix, DEV); \
    printf(__VA_ARGS__);                                                    \
}
#define trace_net(...)  { printf("\x1B[33m-- <network>\x1B[0m  " __VA_ARGS__);}
#define die_unless(a, ...) { if (!(a)) { printf("-- " __VA_ARGS__); assert(a); } }
#else
static void trace(...)
{
};
static void die_unless(...) {};
#endif
#else
#ifdef __GNUC__
#define trace(...) {}
#define trace_graph(...) {}
#define trace_dev(...) {}
#define trace_net(...) {}
#define die_unless(...) {}
#else
static void trace(...)
{
};
static void die_unless(...) {};
#endif
#endif

/*! Helper to find size of signal value types. */
inline static int mpr_type_get_size(mpr_type type)
{
    if (type <= MPR_LIST)   return sizeof(void*);
    switch (type) {
        case MPR_INT32:
        case MPR_BOOL:
        case 'T':
        case 'F':           return sizeof(int);
        case MPR_FLT:       return sizeof(float);
        case MPR_DBL:       return sizeof(double);
        case MPR_PTR:       return sizeof(void*);
        case MPR_STR:       return sizeof(char*);
        case MPR_INT64:     return sizeof(int64_t);
        case MPR_TIME:      return sizeof(mpr_time);
        case MPR_TYPE:      return sizeof(mpr_type);
        default:
            die_unless(0, "Unknown type '%c' in mpr_type_get_size().\n", type);
            return 0;
    }
}

/*! Helper to find the size in bytes of a signal's full vector. */
inline static size_t mpr_sig_get_vector_bytes(mpr_sig sig)
{
    return mpr_type_get_size(sig->type) * sig->len;
}

/*! Helper to find the pointer to the current value in a mpr_hist_t. */
inline static void* mpr_hist_get_val_ptr(mpr_hist_t h)
{
    return h.val + h.pos * h.len * mpr_type_get_size(h.type);
}

/*! Helper to find the pointer to the current time in a mpr_hist_t. */
inline static void* mpr_hist_get_time_ptr(mpr_hist_t h)
{
    return h.time + h.pos * sizeof(mpr_time);
}

/*! Helper to check if a type character is valid. */
inline static int check_sig_length(int length)
{
    return (length < 1 || length > MPR_MAX_VECTOR_LEN);
}

/*! Helper to check if bitfields match completely. */
inline static int bitmatch(unsigned int a, unsigned int b)
{
    return (a & b) == b;
}

/*! Helper to check if type is a number. */
inline static int mpr_type_get_is_num(mpr_type type)
{
    switch (type) {
        case MPR_INT32:
        case MPR_FLT:
        case MPR_DBL:
            return 1;
        default:    return 0;
    }
}

/*! Helper to check if type is a boolean. */
inline static int mpr_type_get_is_bool(mpr_type type)
{
    return 'T' == type || 'F' == type;
}

/*! Helper to check if type is a string. */
inline static int mpr_type_get_is_str(mpr_type type)
{
    return MPR_STR == type;
}

/*! Helper to check if type is a string or void* */
inline static int mpr_type_get_is_ptr(mpr_type type)
{
    return MPR_PTR == type || MPR_STR == type;
}

/*! Helper to check if data type matches, but allowing 'T' and 'F' for bool. */
inline static int type_match(const mpr_type l, const mpr_type r)
{
    return (l == r) || (strchr("bTF", l) && strchr("bTF", r));
}

/*! Helper to remove a leading slash '/' from a string. */
inline static const char *skip_slash(const char *string)
{
    return string + (string && string[0]=='/');
}

/*! Helper to check if a time has the MPR_NOW value */
inline static int mpr_time_get_is_now(mpr_time *t)
{
    return (memcmp(t, &MPR_NOW, sizeof(mpr_time)) == 0);
}

#endif // __MPR_INTERNAL_H__
