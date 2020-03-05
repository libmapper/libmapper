#ifndef __MPR_H__
#define __MPR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <mpr/mpr_constants.h>
#include <mpr/mpr_types.h>

/*! \mainpage libmpr

This is the API documentation for libmpr, a distributed signal mapping
framework.  Please see the Modules section for detailed information, and be
sure to consult the tutorial to get started with libmpr concepts.

 */

/*** Objects ***/

/*! @defgroup objects Objects

     @{ Objects provide a generic representation of Devices, Signals, and Maps. */

/*! Return the internal mpr_graph structure used by an object.
 *  \param obj          The object to query.
 *  \return             The mpr_graph used by this object. */
mpr_graph mpr_obj_get_graph(mpr_obj obj);

/*! Return the specific type of an object.
 *  \param obj          The object to query.
 *  \return             The object type. */
mpr_type mpr_obj_get_type(mpr_obj obj);

/*! Get an object's number of properties.
 *  \param obj          The object to check.
 *  \param staged       1 to include staged properties in the count, 0 otherwise.
 *  \return             The number of properties stored in the table. */
int mpr_obj_get_num_props(mpr_obj obj, int staged);

/*! Look up a property by index of symbolic identifier.
 *  \param obj          The object to check.
 *  \param prop         Symbolic identifier of the property to retrieve.
 *  \param key          A pointer to a location to receive the name of the
 *                      property value (Optional, pass 0 to ignore).
 *  \param len          A pointer to a location to receive the vector length of
 *                      the property value. (Required.)
 *  \param type         A pointer to a location to receive the type of the
 *                      property value. (Required.)
 *  \param val          A pointer to a location to receive the address of the
 *                      property's value. (Required.)
 *  \param pub          1 if property is published, 0 otherwise.
 *  \return             Symbolic identifier of the retrieved property, or
 *                      MPR_PROP_UNKNOWN if not found. */
mpr_prop mpr_obj_get_prop_by_idx(mpr_obj obj, mpr_prop prop, const char **key,
                                 int *len, mpr_type *type, const void **val,
                                 int *pub);

/*! Look up a property by name.
 *  \param obj          The object to check.
 *  \param key          The name of the property to retrieve.
 *  \param length       A pointer to a location to receive the vector length of
 *                      the property value. (Required.)
 *  \param type         A pointer to a location to receive the type of the
 *                      property value. (Required.)
 *  \param value        A pointer to a location to receive the address of the
 *                      property's value. (Required.)
 *  \param pub          1 if property is published, 0 otherwise.
 *  \return             Symbolic identifier of the retrieved property, or
 *                      MPR_PROP_UNKNOWN if not found. */
mpr_prop mpr_obj_get_prop_by_key(mpr_obj obj, const char *key, int *length,
                                 mpr_type *type, const void **value, int *pub);

int mpr_obj_get_prop_as_int32(mpr_obj obj, mpr_prop prop, const char *key);

float mpr_obj_get_prop_as_flt(mpr_obj obj, mpr_prop prop, const char *key);

const char *mpr_obj_get_prop_as_str(mpr_obj obj, mpr_prop prop, const char *key);

const void *mpr_obj_get_prop_as_ptr(mpr_obj obj, mpr_prop prop, const char *key);

mpr_obj mpr_obj_get_prop_as_obj(mpr_obj obj, mpr_prop prop, const char *key);

mpr_list mpr_obj_get_prop_as_list(mpr_obj obj, mpr_prop prop, const char *key);

/*! Set a property.  Can be used to provide arbitrary metadata. Value pointed
 *  to will be copied.
 *  \param obj          The object to operate on.
 *  \param prop         Symbolic identifier of the property to add.
 *  \param key          The name of the property to add.
 *  \param len          The length of value array.
 *  \param type         The property  datatype.
 *  \param value        An array of property values.
 *  \param publish      1 to publish to the distributed graph, 0 for local-only.
 *  \return             Symbolic identifier of the set property, or
 *                      MPR_PROP_UNKNOWN if not found. */
mpr_prop mpr_obj_set_prop(mpr_obj obj, mpr_prop prop, const char *key, int len,
                          mpr_type type, const void *value, int publish);

/*! Remove a property from an object.
 *  \param obj          The object to operate on.
 *  \param prop         Symbolic identifier of the property to remove.
 *  \param key          The name of the property to remove.
 *  \return             1 if property has been removed, 0 otherwise. */
int mpr_obj_remove_prop(mpr_obj obj, mpr_prop prop, const char *key);

/*! Push any property changes out to the distributed graph.
 *  \param obj          The object to operate on. */
void mpr_obj_push(mpr_obj obj);

/*! Helper to print the properties of an object.
 *  \param obj          The object to print.
 *  \param staged       1 to print staged properties, 0 otherwise. */
void mpr_obj_print(mpr_obj obj, int staged);

/*! Filter a list of objects using the given property.
 *  \param list         The list of objects to filter.
 *  \param prop         Symbolic identifier of the property to look for.
 *  \param key          The name of the property to search for.
 *  \param len          The value length.
 *  \param type         The value type.
 *  \param value        The value.
 *  \param op           The comparison operator.
 *  \return             A list of results.  Use mpr_list_get_next() to iterate. */
mpr_list mpr_list_filter(mpr_list list, mpr_prop prop, const char *key, int len,
                         mpr_type type, const void *value, mpr_op op);

/*! Get the union of two object lists (objects matching list1 OR list2).
 *  \param list1        The first object list.
 *  \param list2        The second object list.
 *  \return             A list of results.  Use mpr_list_get_next() to iterate. */
mpr_list mpr_list_get_union(mpr_list list1, mpr_list list2);

/*! Get the intersection of two object lists (objects matching list1 AND list2).
 *  \param list1        The first object list.
 *  \param list2        The second object list.
 *  \return             A list of results.  Use mpr_list_get_next() to iterate. */
mpr_list mpr_list_get_isect(mpr_list list1, mpr_list list2);

/*! Get the difference between two object lists (objects in list1 but NOT list2).
 *  \param list1        The first object list.
 *  \param list2        The second object list.
 *  \return             A list of results.  Use mpr_list_get_next() to iterate. */
mpr_list mpr_list_get_diff(mpr_list list1, mpr_list list2);

/*! Get an indexed item in a list of objects.
 *  \param list         The previous object record pointer.
 *  \param idx          The index of the list element to retrieve.
 *  \return             A pointer to the object record, or zero if it doesn't
 *                      exist. */
mpr_obj mpr_list_get_idx(mpr_list list, unsigned int idx);

/*! Given a object record pointer returned from a previous object query, get the
 *  next item in the list.
 *  \param list         The previous object record pointer.
 *  \return             A list of results.  Use mpr_list_get_next() to iterate. */
mpr_list mpr_list_get_next(mpr_list list);

/*! Copy a previously-constructed object list.
 *  \param list         The previous object record pointer.
 *  \return             A list of results.  Use mpr_list_get_next() to iterate. */
mpr_list mpr_list_get_cpy(mpr_list list);

/*! Given a object record pointer returned from a previous object query,
 *  indicate that we are done iterating.
 *  \param list         The previous object record pointer. */
void mpr_list_free(mpr_list list);

/*! Return the number of objects in a previous object query list.
 *  \param list         The previous object record pointer.
 *  \return             The number of objects in the list. */
int mpr_list_get_size(mpr_list list);

/* @} */

/*** Signals ***/

/*! @defgroup signals Signals

    @{ Signals define inputs or outputs for devices.  A signal consists of a
       scalar or vector value of some integer or floating-point type.  A signal
       is created by adding an input or output to a device.  It can optionally
       be provided with some metadata such as a signal's range, unit, or other
       properties.  Signals can be mapped by creating maps through a GUI. */

/*! A signal handler function can be called whenever a signal value changes.
 *  \param sig          The signal that has been updated.
 *  \param evt          The type of event that has occured, e.g. MPR_SIG_UPDATE
 *                      when the value has changed.
 *  \param inst         The id of the instance that has been changed, if
 *                      applicable.
 *  \param length       The array length of the update value.
 *  \param type         The data type of the update value.
 *  \param value        A pointer to the value update.
 *  \param time         The timetag associated with this event. */
typedef void mpr_sig_handler(mpr_sig sig, mpr_sig_evt evt, mpr_id inst, int length,
                             mpr_type type, const void *value, mpr_time time);

/*! Allocate and initialize a signal.  Values and strings pointed to by this
 *  call will be copied.  For min and max values, actual type must correspond
 *  to 'type' (if type=MPR_INT32, then int*, etc).
 *  \param parent       The object to add a signal to.
 *  \param dir          The signal direction.
 *  \param name         The name of the signal.
 *  \param len          The length of the signal vector, or 1 for a scalar.
 *  \param type         The type fo the signal value.
 *  \param unit         The unit of the signal, or 0 for none.
 *  \param min          Pointer to a minimum value, or 0 for none.
 *  \param max          Pointer to a maximum value, or 0 for none.
 *  \param num_inst     Pointer to the number of signal instances or 0 to
 *                      indicate that instances will not be used.
 *  \param handler      Function to be called when the value of the signal is
 *                      updated.
 *  \param events       Bitflags for types of events we are interested in.
 *  \return             The new signal. */
mpr_sig mpr_sig_new(mpr_dev parent, mpr_dir dir, const char *name, int len,
                    mpr_type type, const char *unit, const void *min,
                    const void *max, int *num_inst, mpr_sig_handler *handler,
                    int events);

/* Free resources used by a signal.
 * \param sig           The signal to free. */
void mpr_sig_free(mpr_sig sig);

/*! Update the value of a signal instance.  The signal will be routed according
 *  to external requests.
 *  \param sig          The signal to operate on.
 *  \param inst         A pointer to the identifier of the instance to update,
 *                      or 0 for the default instance.
 *  \param length       Length of the value argument. Expected to be a multiple
 *                      of the signal length. A block of values can be accepted,
 *                      with the current value as the last value(s) in an array.
 *  \param type         Data type of the value argument.
 *  \param value        A pointer to a new value for this signal.  If the signal
 *                      type is MPR_INT32, this should be int*; if the signal
 *                      type is MPR_FLOAT, this should be float* (etc).  It
 *                      should be an array at least as long as the signal's
 *                      length property.
 *  \param time         The time at which the value update was aquired. If NULL,
 *                      libmpr will tag the value update with the current
 *                      time.  See mpr_dev_start_queue() for more
 *                      information on bundling multiple signal updates with the
 *                      same time. */
void mpr_sig_set_value(mpr_sig sig, mpr_id inst, int length, mpr_type type,
                       const void *value, mpr_time time);

/*! Get the value of a signal instance.
 *  \param sig          The signal to operate on.
 *  \param inst         A pointer to the identifier of the instance to query,
 *                      or 0 for the default instance.
 *  \param time         A location to receive the value's time tag. May be 0.
 *  \return             A pointer to an array containing the value of the signal
 *                      instance, or 0 if the signal instance has no value. */
const void *mpr_sig_get_value(mpr_sig sig, mpr_id inst, mpr_time *time);

/*! Return the list of maps associated with a given signal.
 *  \param sig          Signal record to query for maps.
 *  \param dir          The direction of the map relative to the given signal.
 *  \return             A list of results.  Use mpr_list_get_next() to iterate. */
mpr_list mpr_sig_get_maps(mpr_sig sig, mpr_dir dir);

/*! Set or unset the message handler for a signal.
 *  \param sig          The signal to operate on.
 *  \param handler      A pointer to a mpr_sig_handler function
 *                      for processing incoming messages.
 *  \param events       Bitflags for types of events we are interested in. */
void mpr_sig_set_cb(mpr_sig sig, mpr_sig_handler *handler, int events);

/**** Signal Instances ****/

/*! Add new instances to the reserve list. Note that if instance ids are
 *  specified, libmpr will not add multiple instances with the same id.
 *  \param sig          The signal to which the instances will be added.
 *  \param num          The number of instances to add.
 *  \param ids          Array of integer ids, one for each new instance,
 *                      or 0 for automatically-generated instance ids.
 *  \param data         Array of user context pointers, one for each new instance,
 *                      or 0 if not needed.
 *  \return             Number of instances added. */
int mpr_sig_reserve_inst(mpr_sig sig, int num, mpr_id *ids, void **data);

/*! Release a specific instance of a signal by removing it from the list of
 *  active instances and adding it to the reserve list.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to suspend.
 *  \param time         The time at which the instance was released; if NULL,
 *                      will be tagged with the current time. See
 *                      mpr_dev_start_queue() for more information on
 *                      bundling multiple signal updates with the same time. */
void mpr_sig_release_inst(mpr_sig sig, mpr_id inst, mpr_time time);

/*! Remove a specific instance of a signal and free its memory.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to suspend. */
void mpr_sig_remove_inst(mpr_sig sig, mpr_id inst);

/*! Return whether a given signal instance is currently active.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to check.
 *  \return             Non-zero if the instance is active, zero otherwise. */
int mpr_sig_get_inst_is_active(mpr_sig sig, mpr_id inst);

/*! Activate a specific signal instance.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to activate.
 *  \return             Non-zero if the instance is active, zero otherwise. */
int mpr_sig_activate_inst(mpr_sig sig, mpr_id inst);

/*! Get the local id of the oldest active instance.
 *  \param sig          The signal to operate on.
 *  \return             The instance identifier, or zero if unsuccessful. */
mpr_id mpr_sig_get_oldest_inst_id(mpr_sig sig);

/*! Get the local id of the newest active instance.
 *  \param sig          The signal to operate on.
 *  \return             The instance identifier, or zero if unsuccessful. */
mpr_id mpr_sig_get_newest_inst_id(mpr_sig sig);

/*! Get a signal instance's identifier by its index.  Intended to be used for
 *  iterating over the active instances.
 *  \param sig          The signal to operate on.
 *  \param idx          The numerical index of the ID to retrieve.  Should be
 *                      between zero and the number of instances.
 *  \param status       The status of the instances to searchl should be set to
 *                      MPR_STATUS_ACTIVE, MPR_STATUS_RESERVED, or both
 *                      (MPR_STATUS_ACTIVE | MPR_STATUS_RESERVED).
 *  \return             The instance ID associated with the given index, or zero
 *                      if unsuccessful. */
mpr_id mpr_sig_get_inst_id(mpr_sig sig, int idx, mpr_status status);

/*! Associate a signal instance with an arbitrary pointer.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to operate on.
 *  \param data         A pointer to user data to be associated with this
 *                      instance. */
void mpr_sig_set_inst_data(mpr_sig sig, mpr_id inst, const void *data);

/*! Retrieve the arbitrary pointer associated with a signal instance.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to operate on.
 *  \return             A pointer associated with this instance. */
void *mpr_sig_get_inst_data(mpr_sig sig, mpr_id inst);

/**** Signal properties ****/

/*! Get the parent mpr_dev for a specific signal.
 *  \param sig          The signal to check.
 *  \return             The signal's parent device. */
mpr_dev mpr_sig_get_dev(mpr_sig sig);

/*! Get the number of instances for a specific signal.
 *  \param sig          The signal to check.
 *  \param status       The status of the instances to searchl should be set to
 *                      MPR_STATUS_ACTIVE, MPR_STATUS_RESERVED, or both
 *                      (MPR_STATUS_ACTIVE | MPR_STATUS_RESERVED).
 *  \return             The number of allocated signal instances. */
int mpr_sig_get_num_inst(mpr_sig sig, mpr_status status);

/* @} */

/*** Devices ***/

/*! @defgroup devices Devices

    @{ A device is an entity on the distributed graph which has input and/or
       output signals.  The mpr_dev is the primary interface through which
       a program uses libmpr.  A device must have a name, to which a unique
       ordinal is subsequently appended.  It can also be given other
       user-specified metadata.  Devices signals can be connected, which is
       accomplished by requests from an external GUI. */

/*! Allocate and initialize a device.
 *  \param name         A short descriptive string to identify the device.
 *                      Must not contain spaces or the slash character '/'.
 *  \param graph        A previously allocated graph structure to use.
 *                      If 0, one will be allocated for use with this device.
 *  \return             A newly allocated device.  Should be freed
 *                      using mpr_dev_free(). */
mpr_dev mpr_dev_new(const char *name, mpr_graph graph);

/*! Free resources used by a device.
 *  \param dev          The device to free. */
void mpr_dev_free(mpr_dev dev);

/*! Return a unique id associated with a given device.
 *  \param dev          The device to use.
 *  \return             A new unique id. */
mpr_id mpr_dev_generate_unique_id(mpr_dev dev);

/*! Return the list of signals for a given device.
 *  \param dev          The device to query.
 *  \param dir          The direction of the signals to return, should be
 *                      MPR_DIR_IN, MPR_DIR_OUT, or MPR_DIR_ANY.
 *  \return             A list of results.  Use mpr_list_get_next() to iterate. */
mpr_list mpr_dev_get_sigs(mpr_dev dev, mpr_dir dir);

/*! Poll this device for new messages.  Note, if you have multiple devices, the
 *  right thing to do is call this function for each of them with block_ms=0,
 *  and add your own sleep if necessary.
 *  \param dev          The device to check messages for.
 *  \param block_ms     Number of milliseconds to block waiting for messages, or
 *                      0 for non-blocking behaviour.
 *  \return             The number of handled messages. May be zero if there was
 *                      nothing to do. */
int mpr_dev_poll(mpr_dev dev, int block_ms);

/*! Detect whether a device is completely initialized.
 *  \param dev          The device to query.
 *  \return             Non-zero if device is completely initialized, i.e., has
 *                      an allocated receiving port and unique identifier.
 *                      Zero otherwise. */
int mpr_dev_get_is_ready(mpr_dev dev);

/*! Start a time-tagged queue.
 *  \param dev          The device to use.
 *  \param time         A time to use for the updates bundled by this queue,
 *                      or MPR_NOW to use the current local time.
 *  \return             The time used to identify this queue. */
mpr_time mpr_dev_start_queue(mpr_dev dev, mpr_time time);

/*! Dispatch a time-tagged queue.
 *  \param dev          The device to use.
 *  \param time         The time for an existing queue created with
 *                      mpr_dev_start_queue(). */
void mpr_dev_send_queue(mpr_dev dev, mpr_time time);

/* @} */

/***** Maps *****/

/*! @defgroup maps Maps

    @{ Maps define dataflow connections between sets of signals. A map consists
       of one or more sources, one destination, and properties which determine
       how the source data is processed. */

/*! Create a map between a set of signals.
 *  \param num_srcs     The number of source signals in this map.
 *  \param srcs         Array of source signal data structures.
 *  \param num_dsts     The number of destination signals in this map.
 *                      Currently restricted to 1.
 *  \param dsts         Array of destination signal data structures.
 *  \return             A map data structure – either loaded from the graph
 *                      (if the map already existed) or newly created. In the
 *                      latter case the map will not take effect until it has
 *                      been added to the distributed graph using
 *                      mpr_obj_push(). */
mpr_map mpr_map_new(int num_srcs, mpr_sig *srcs, int num_dsts, mpr_sig *dsts);

/*! Remove a map between a set of signals.
 *  \param map          The map to destroy. */
void mpr_map_release(mpr_map map);

/*! Retrieve a list of connected signals for a specific map.
 *  \param map          The map to check.
 *  \param loc          The map endpoint, must be MPR_LOC_SRC, MPR_LOC_DST, or
 *                      MPR_LOC_ANY.
 *  \return             A list of results.  Use mpr_list_get_next() to iterate. */
mpr_list mpr_map_get_sigs(mpr_map map, mpr_loc loc);

/*! Retrieve the index for a specific map signal.
 *  \param map          The map to check.
 *  \param sig          The signal to find.
 *  \return             The signal index. */
int mpr_map_get_sig_idx(mpr_map map, mpr_sig sig);

/*! Detect whether a map is completely initialized.
 *  \param map          The device to query.
 *  \return             Non-zero if map is completely initialized, zero
 *                      otherwise. */
int mpr_map_get_is_ready(mpr_map map);

/*! Re-create stale map if necessary.
 *  \param map          The map to operate on. */
void mpr_map_refresh(mpr_map map);

/*! Add a scope to this map. Map scopes configure the propagation of signal
 *  instance updates across the map. Changes to remote maps will not take effect
 *  until synchronized with the distributed graph using mpr_obj_push().
 *  \param map          The map to modify.
 *  \param dev          Device to add as a scope for this map. After taking
 *                      effect, this setting will cause instance updates
 *                      originating at this device to be propagated across the
 *                      map. */
void mpr_map_add_scope(mpr_map map, mpr_dev dev);

/*! Remove a scope from this map. Map scopes configure the propagation of signal
 *  instance updates across the map. Changes to remote maps will not take effect
 *  until synchronized with the distributed graph using mpr_obj_push().
 *  \param map          The map to modify.
 *  \param dev          Device to remove as a scope for this map. After taking
 *                      effect, this setting will cause instance updates
 *                      originating at this device to be blocked from
 *                      propagating across the map. */
void mpr_map_remove_scope(mpr_map map, mpr_dev dev);

/* @} */

/***** Graph *****/

/*! @defgroup graphs Graphs

    @{ Graphs are the primary interface through which a program may observe
       the distributed graph and store information about devices and signals
       that are present.  Each Graph stores records of devices, signals, and
       maps, which can be queried. */

/*! Create a peer in the distributed graph.
 *  \param autosub_flags    Sets whether the graph should automatically
 *                          subscribe to information about signals and maps
 *                          when it encounters a previously-unseen device.
 *  \return                 The new graph. */
mpr_graph mpr_graph_new(int autosub_flags);

/*! Specify network interface to use.
 *  \param g            The graph structure to use.
 *  \param interface    The name of the network interface to use. */
void mpr_graph_set_interface(mpr_graph g, const char *interface);

/*! Return a string indicating the name of the network interface in use.
 *  \param g            The graph structure to query.
 *  \return             A string containing the name of the network interface. */
const char *mpr_graph_get_interface(mpr_graph g);

/*! Set the multicast group and port to use.
 *  \param g            The graph structure to query.
 *  \param group        A string specifying the multicast group for bus
 *                      communication with the distributed graph.
 *  \param port         The port to use for multicast communication. */
void mpr_graph_set_address(mpr_graph g, const char *group, int port);

/*! Retrieve the multicast group currently in use.
 *  \param g            The graph structure to query.
 *  \return             A string specifying the multicast url for bus
 *                      communication with the distributed graph. */
const char *mpr_graph_get_address(mpr_graph g);

/*! Synchonize a local graph copy with the distributed graph.
 *  \param g            The graph to update.
 *  \param block_ms     The number of milliseconds to block, or 0 for
 *                      non-blocking behaviour.
 *  \return             The number of handled messages. */
int mpr_graph_poll(mpr_graph g, int block_ms);

/*! Free a graph.
 *  \param g            The graph to free. */
void mpr_graph_free(mpr_graph g);

/*! Print the contents of a graph.
 *  \param g            The graph to print. */
void mpr_graph_print(mpr_graph g);

/*! Subscribe to information about a specific device.
 *  \param g            The graph to use.
 *  \param dev          The device of interest. If NULL the graph will
 *                      automatically subscribe to all discovered devices.
 *  \param types        Bitflags setting the type of information of interest.
 *                      Can be a combination of mpr_type values.
 *  \param timeout      The length in seconds for this subscription. If set to
 *                      -1, the graph will automatically renew the
 *                      subscription until it is freed or this function is
 *                      called again. */
void mpr_graph_subscribe(mpr_graph g, mpr_dev dev, int types, int timeout);

/*! Unsubscribe from information about a specific device.
 *  \param g            The graph to use.
 *  \param dev          The device of interest. If NULL the graph will
 *                      unsubscribe from all devices. */
void mpr_graph_unsubscribe(mpr_graph g, mpr_dev dev);

/*! A callback function prototype for when an object record is added or updated.
 *  Such a function is passed in to mpr_graph_add_cb().
 *  \param g            The graph that registered this callback.
 *  \param obj          The object record.
 *  \param evt          A value of mpr_graph_evt indicating what is happening
 *                      to the object record.
 *  \param data         The user context pointer registered with this callback. */
typedef void mpr_graph_handler(mpr_graph g, mpr_obj obj, const mpr_graph_evt evt,
                               const void *data);

/*! Register a callback for when an object record is added or updated in the
 *  graph.
 *  \param g            The graph to query.
 *  \param h            Callback function.
 *  \param types        Bitflags setting the type of information of interest.
 *                      Can be a combination of mpr_type values.
 *  \param data         A user-defined pointer to be passed to the callback
 *                      for context.
 *  \return             One if a callback was added, otherwise zero. */
int mpr_graph_add_cb(mpr_graph g, mpr_graph_handler *h, int types,
                     const void *data);

/*! Remove a device record callback from the graph service.
 *  \param g            The graph to query.
 *  \param h            Callback function.
 *  \param data         The user context pointer that was originally specified
 *                      when adding the callback.
 *  \return             One if a callback was removed, otherwise zero. */
int mpr_graph_remove_cb(mpr_graph g, mpr_graph_handler *h, const void *data);

/*! Return a list of objects.
 *  \param g            The graph to query.
 *  \param types        Bitflags setting the type of information of interest.
 *                      Can be a combination of mpr_type values.
 *  \return             A list of results.  Use mpr_list_get_next() to iterate. */
mpr_list mpr_graph_get_objs(mpr_graph g, int types);

/* @} */

/***** Time *****/

/*! @defgroup times Times

 @{ libmpr primarily uses NTP timetags for communication and synchronization. */

/*! Add a time to another given time.
 *  \param augend       A previously allocated time to augment.
 *  \param addend       A time to add. */
void mpr_time_add(mpr_time *augend, mpr_time addend);

/*! Add a double-precision floating point value to another given time.
 *  \param augend       A previously allocated time to augment.
 *  \param addend       A value in seconds to add. */
void mpr_time_add_dbl(mpr_time *augend, double addend);

/*! Subtract a time from another given time.
 *  \param minuend      A previously allocated time to augment.
 *  \param subtrahend   A time to add to subtract. */
void mpr_time_sub(mpr_time *minuend, mpr_time subtrahend);

/*! Add a double-precision floating point value to another given time.
 *  \param time         A previously allocated time to multiply.
 *  \param multiplicand A value in seconds. */
void mpr_time_mul(mpr_time *time, double multiplicand);

/*! Return value of mpr_time as a double-precision floating point value.
 *  \param time         The time to read.
 *  \return             Value of the time as a double-precision float. */
double mpr_time_as_dbl(mpr_time time);

/*! Set value of a mpr_time from a double-precision floating point value.
 *  \param time         A previously-allocated time to set.
 *  \param value        The value in seconds to set. */
void mpr_time_set_dbl(mpr_time *time, double value);

/*! Copy value of a mpr_time.
 *  \param timel        The target time for copying.
 *  \param timer        The source time. */
void mpr_time_set(mpr_time *timel, mpr_time timer);

/* @} */

/*! Get the version of libmpr.
 *  \return             A string specifying the version of libmpr. */
const char *mpr_get_version(void);

#ifdef __cplusplus
}
#endif

#endif // __MPR_H__
