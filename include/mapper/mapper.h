#ifndef __MAPPER_H__
#define __MAPPER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <mapper/mapper_constants.h>
#include <mapper/mapper_types.h>

/*! \mainpage libmapper

This is the API documentation for libmapper, a distributed signal mapping
framework.  Please see the Modules section for detailed information, and be
sure to consult the tutorial to get started with libmapper concepts.

 */

/*** Objects ***/

/*! @defgroup objects Objects

     @{ Objects provide a generic representation of Devices, Signals, and Maps. */

/*! Return the internal mapper_graph structure used by an object.
 *  \param obj          The object to query.
 *  \return             The mapper_graph used by this object. */
mapper_graph mapper_object_get_graph(mapper_object obj);

/*! Return the specific type of an object.
 *  \param obj          The object to query.
 *  \return             The object type. */
mapper_object_type mapper_object_get_type(mapper_object obj);

/*! Get an object's number of properties.
 *  \param obj          The object to check.
 *  \param staged       1 to include staged properties in the count, 0 otherwise.
 *  \return             The number of properties stored in the table. */
int mapper_object_get_num_props(mapper_object obj, int staged);

/*! Look up a property by index of symbolic identifier.
 *  \param obj          The object to check.
 *  \param prop         Symbolic identifier of the property to retrieve.
 *  \param name         A pointer to a location to receive the name of the
 *                      property value (Optional, pass 0 to ignore).
 *  \param length       A pointer to a location to receive the vector length of
 *                      the property value. (Required.)
 *  \param type         A pointer to a location to receive the type of the
 *                      property value. (Required.)
 *  \param value        A pointer to a location to receive the address of the
 *                      property's value. (Required.)
 *  \return             Symbolic identifier of the retrieved property, or
 *                      MAPPER_PROP_UNKNOWN if not found. */
mapper_property mapper_object_get_prop_by_index(mapper_object obj,
                                                mapper_property prop,
                                                const char **name,
                                                int *length,
                                                mapper_type *type,
                                                const void **value);

/*! Look up a property by name.
 *  \param obj          The object to check.
 *  \param name         The name of the property to retrieve.
 *  \param length       A pointer to a location to receive the vector length of
 *                      the property value. (Required.)
 *  \param type         A pointer to a location to receive the type of the
 *                      property value. (Required.)
 *  \param value        A pointer to a location to receive the address of the
 *                      property's value. (Required.)
 *  \return             Symbolic identifier of the retrieved property, or
 *                      MAPPER_PROP_UNKNOWN if not found. */
mapper_property mapper_object_get_prop_by_name(mapper_object obj,
                                               const char *name, int *length,
                                               mapper_type *type,
                                               const void **value);

/*! Set a property.  Can be used to provide arbitrary metadata. Value pointed
 *  to will be copied.
 *  \param obj          The object to operate on.
 *  \param prop         Symbolic identifier of the property to add.
 *  \param name         The name of the property to add.
 *  \param length       The length of value array.
 *  \param type         The property  datatype.
 *  \param value        An array of property values.
 *  \param publish      1 to publish to the distributed graph, 0 for local-only.
 *  \return             Symbolic identifier of the set property, or
 *                      MAPPER_PROP_UNKNOWN if not found. */
mapper_property mapper_object_set_prop(mapper_object obj, mapper_property prop,
                                       const char *name, int length,
                                       mapper_type type, const void *value,
                                       int publish);

/*! Remove a property from an object.
 *  \param obj          The object to operate on.
 *  \param prop         Symbolic identifier of the property to remove.
 *  \param name         The name of the property to remove.
 *  \return             1 if property has been removed, 0 otherwise. */
int mapper_object_remove_prop(mapper_object obj, mapper_property prop,
                              const char *name);

/*! Push any property changes out to the distributed graph.
 *  \param obj          The object to operate on. */
void mapper_object_push(mapper_object obj);

/*! Helper to print the properties of an object.
 *  \param obj          The object to print.
 *  \param staged       1 to print staged properties, 0 otherwise. */
void mapper_object_print(mapper_object obj, int staged);

/*! Filter a list of objects using the given property.
 *  \param list         The list og objects to filter.
 *  \param prop         Symbolic identifier of the property to look for.
 *  \param name         The name of the property to search for.
 *  \param length       The value length.
 *  \param type         The value type.
 *  \param value        The value.
 *  \param op           The comparison operator.
 *  \return             A double-pointer to the first item in a list of results.
 *                      Use mapper_object_list_next() to iterate. */
mapper_object *mapper_object_list_filter(mapper_object *list, mapper_property prop,
                                         const char *name, int length,
                                         mapper_type type, const void *value,
                                         mapper_op op);

/*! Get the union of two object lists (objects matching list1 OR list2).
 *  \param list1        The first object list.
 *  \param list2        The second object list.
 *  \return             A double-pointer to the first item in a list of results.
 *                      Use mapper_object_list_next() to iterate. */
mapper_object *mapper_object_list_union(mapper_object *list1,
                                        mapper_object *list2);

/*! Get the intersection of two object lists (objects matching list1 AND list2).
 *  \param list1        The first object list.
 *  \param list2        The second object list.
 *  \return             A double-pointer to the first item in a list of results.
 *                      Use mapper_object_list_next() to iterate. */
mapper_object *mapper_object_list_intersection(mapper_object *list1,
                                               mapper_object *list2);

/*! Get the difference between two object lists (objects in list1 but NOT list2).
 *  \param list1        The first object list.
 *  \param list2        The second object list.
 *  \return             A double-pointer to the first item in a list of results.
 *                      Use mapper_object_list_next() to iterate. */
mapper_object *mapper_object_list_difference(mapper_object *list1,
                                             mapper_object *list2);

/*! Get an indexed item in a list of objects.
 *  \param list         The previous object record pointer.
 *  \param index        The index of the list element to retrieve.
 *  \return             A pointer to the object record, or zero if it doesn't
 *                      exist. */
mapper_object mapper_object_list_get_index(mapper_object *list, int index);

/*! Given a object record pointer returned from a previous object query, get the
 *  next item in the list.
 *  \param list         The previous object record pointer.
 *  \return             A double-pointer to the next object record in the list,
 *                      or zero if no more objects. */
mapper_object *mapper_object_list_next(mapper_object *list);

/*! Copy a previously-constructed object list.
 *  \param list         The previous object record pointer.
 *  \return             A double-pointer to the copy of the list, or zero if
 *                      none.  Use mapper_object_list_next() to iterate. */
mapper_object *mapper_object_list_copy(mapper_object *list);

/*! Given a object record pointer returned from a previous object query,
 *  indicate that we are done iterating.
 *  \param list         The previous object record pointer. */
void mapper_object_list_free(mapper_object *list);

/*! Return the number of objects in a previous object query list.
 *  \param list         The previous object record pointer.
 *  \return             The number of objects in the list. */
int mapper_object_list_get_length(mapper_object *list);

/* @} */

/*** Signals ***/

/*! @defgroup signals Signals

    @{ Signals define inputs or outputs for devices.  A signal consists of a
       scalar or vector value of some integer or floating-point type.  A
       mapper_signal is created by adding an input or output to a device.  It
       can optionally be provided with some metadata such as a signal's range,
       unit, or other properties.  Signals can be mapped by creating maps
       through a GUI. */

/*! Update the value of a signal instance.  The signal will be routed according
 *  to external requests.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to update.
 *  \param length       Length of the value argument. Expected to be a multiple
 *                      of the signal length. A block of values can be accepted,
 *                      with the current value as the last value(s) in an array.
 *  \param type         Data type of the value argument.
 *  \param value        A pointer to a new value for this signal.  If the signal
 *                      type is MAPPER_INT32, this should be int*; if the signal
 *                      type is MAPPER_DOUBLE, this should be float* (etc).  It
 *                      should be an array at least as long as the signal's
 *                      length property.
 *  \param time         The time at which the value update was aquired. If NULL,
 *                      libmapper will tag the value update with the current
 *                      time.  See mapper_device_start_queue() for more
 *                      information on bundling multiple signal updates with the
 *                      same time. */
void mapper_signal_set_value(mapper_signal sig, mapper_id inst, int length,
                             mapper_type type, const void *value,
                             mapper_time_t time);

/*! Get the value of a signal instance.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to operate on.
 *  \param time         A location to receive the value's time tag. May be 0.
 *  \return             A pointer to an array containing the value of the signal
 *                      instance, or 0 if the signal instance has no value. */
const void *mapper_signal_get_value(mapper_signal sig, mapper_id inst,
                                    mapper_time_t *time);

/*! Return the list of maps associated with a given signal.
 *  \param sig          Signal record to query for maps.
 *  \param dir          The direction of the map relative to the given signal.
 *  \return             A double-pointer to the first item in the list of
 *                      results or zero if none.  Use mapper_object_list_next()
 *                      to iterate. */
mapper_object *mapper_signal_get_maps(mapper_signal sig, mapper_direction dir);

/*! Retrieve the signal group from a given signal.
 *  Signals in the same group will have instance ids automatically coordinated.
 *  By default all signals are in the same (default) group.
 *  \param sig          The signal to add.
 *  \return             The signal group used by this signal. */
mapper_signal_group mapper_signal_get_group(mapper_signal sig);

/*! Add a signal to a predefined signal group created using
 *  mapper_device_add_signal_group.
 *  Signals in the same group will have instance ids automatically coordinated.
 *  By default all signals are in the same (default) group.
 *  \param sig          The signal to add.
 *  \param group        A signal group to associate with this signal, or 0
 *                      to reset to the default group. */
void mapper_signal_set_group(mapper_signal sig, mapper_signal_group group);

/*! A signal handler function can be called whenever a signal value changes. */
typedef void mapper_signal_update_handler(mapper_signal sig, mapper_id inst,
                                          int length, mapper_type type,
                                          const void *value,
                                          mapper_time_t *time);

/*! Set or unset the message handler for a signal.
 *  \param sig          The signal to operate on.
 *  \param handler      A pointer to a mapper_signal_update_handler function
 *                      for processing incoming messages. */
void mapper_signal_set_callback(mapper_signal sig,
                                mapper_signal_update_handler *handler);

/**** Signal Instances ****/

/*! Add new instances to the reserve list. Note that if instance ids are
 *  specified, libmapper will not add multiple instances with the same id.
 *  \param sig          The signal to which the instances will be added.
 *  \param num          The number of instances to add.
 *  \param ids          Array of integer ids, one for each new instance,
 *                      or 0 for automatically-generated instance ids.
 *  \param user_data    Array of user context pointers, one for each new instance,
 *                      or 0 if not needed.
 *  \return             Number of instances added. */
int mapper_signal_reserve_instances(mapper_signal sig, int num, mapper_id *ids,
                                    void **user_data);

/*! Release a specific instance of a signal by removing it from the list of
 *  active instances and adding it to the reserve list.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to suspend.
 *  \param time         The time at which the instance was released; if NULL,
 *                      will be tagged with the current time. See
 *                      mapper_device_start_queue() for more information on
 *                      bundling multiple signal updates with the same time. */
void mapper_signal_release_instance(mapper_signal sig, mapper_id inst,
                                    mapper_time_t time);

/*! Remove a specific instance of a signal and free its memory.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to suspend. */
void mapper_signal_remove_instance(mapper_signal sig, mapper_id inst);

/*! Return whether a given signal instance is currently active.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to check.
 *  \return             Non-zero if the instance is active, zero otherwise. */
int mapper_signal_get_instance_is_active(mapper_signal sig, mapper_id inst);

/*! Activate a specific signal instance.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to activate.
 *  \return             Non-zero if the instance is active, zero otherwise. */
int mapper_signal_activate_instance(mapper_signal sig, mapper_id inst);

/*! Get the local id of the oldest active instance.
 *  \param sig          The signal to operate on.
 *  \return             The instance identifier, or zero if unsuccessful. */
mapper_id mapper_signal_get_oldest_instance_id(mapper_signal sig);

/*! Get the local id of the newest active instance.
 *  \param sig          The signal to operate on.
 *  \return             The instance identifier, or zero if unsuccessful. */
mapper_id mapper_signal_get_newest_instance_id(mapper_signal sig);

/*! Return the number of active instances owned by a signal.
 *  \param  sig         The signal to query.
 *  \return             The number of active instances. */
int mapper_signal_get_num_active_instances(mapper_signal sig);

/*! Return the number of reserved instances owned by a signal.
 *  \param  sig         The signal to query.
 *  \return             The number of active instances. */
int mapper_signal_get_num_reserved_instances(mapper_signal sig);

/*! Get a signal instance's identifier by its index.  Intended to be used for
 *  iterating over the active instances.
 *  \param sig          The signal to operate on.
 *  \param index        The numerical index of the ID to retrieve.  Should be
 *                      between zero and the number of instances.
 *  \return             The instance ID associated with the given index, or zero
 *                      if unsuccessful. */
mapper_id mapper_signal_get_instance_id(mapper_signal sig, int index);

/*! Get an active signal instance's ID by its index.  Intended to be used for
 *  iterating over the active instances.
 *  \param sig          The signal to operate on.
 *  \param index        The numerical index of the ID to retrieve.  Should be
 *                      between zero and the number of instances.
 *  \return             The instance ID associated with the given index, or zero
 *                      if unsuccessful. */
mapper_id mapper_signal_get_active_instance_id(mapper_signal sig, int index);

/*! Get a reserved signal instance's ID by its index.  Intended to be used for
 *  iterating over the reserved instances.
 *  \param sig          The signal to operate on.
 *  \param index        The numerical index of the ID to retrieve.  Should be
 *                      between zero and the number of instances.
 *  \return             The instance ID associated with the given index, or zero
 *                      if unsuccessful. */
mapper_id mapper_signal_get_reserved_instance_id(mapper_signal sig, int index);

/*! Set the stealing method to be used when a previously-unseen instance ID is
 *  received and no instances are available.
 *  \param sig          The signal to operate on.
 *  \param mode         Method to use for reallocating active instances if no
 *                      reserved instances are available. */
void mapper_signal_set_stealing_mode(mapper_signal sig, mapper_stealing_type mode);

/*! Get the stealing method to be used when a previously-unseen instance ID is
 *  received.
 *  \param sig          The signal to operate on.
 *  \return             The stealing mode of the provided signal. */
mapper_stealing_type mapper_signal_get_stealing_mode(mapper_signal sig);

/*! A handler function to be called whenever a signal instance management event
 *  occurs. */
typedef void mapper_instance_event_handler(mapper_signal sig, mapper_id inst,
                                           mapper_instance_event event,
                                           mapper_time_t *time);

/*! Set the handler to be called on signal instance management events.
 *  \param sig          The signal to operate on.
 *  \param h            A handler function for instance management events.
 *  \param events       Bitflags for indicating the types of events which should
 *                      trigger the callback. Can be a combination of values
 *                      from the enum mapper_instance_event. */
void mapper_signal_set_instance_event_callback(mapper_signal sig,
                                               mapper_instance_event_handler h,
                                               int events);

/*! Associate a signal instance with an arbitrary pointer.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to operate on.
 *  \param user_data    A pointer to user data to be associated with this
 *                      instance. */
void mapper_signal_set_instance_user_data(mapper_signal sig, mapper_id inst,
                                          const void *user_data);

/*! Retrieve the arbitrary pointer associated with a signal instance.
 *  \param sig          The signal to operate on.
 *  \param inst         The identifier of the instance to operate on.
 *  \return             A pointer associated with this instance. */
void *mapper_signal_get_instance_user_data(mapper_signal sig, mapper_id inst);

/**** Signal properties ****/

/*! Get the parent mapper_device for a specific signal.
 *  \param sig          The signal to check.
 *  \return         	The signal's parent device. */
mapper_device mapper_signal_get_device(mapper_signal sig);

/*! Get the number of instances for a specific signal.
 *  \param sig          The signal to check.
 *  \return         	The number of allocated signal instances. */
int mapper_signal_get_num_instances(mapper_signal sig);

/*! Get the number of maps associated with a specific signal.
 *  \param sig          The signal to check.
 *  \param dir          The direction of the maps relative to the given signal.
 *  \return             The number of associated maps. */
int mapper_signal_get_num_maps(mapper_signal sig, mapper_direction dir);

/* @} */

/*** Devices ***/

/*! @defgroup devices Devices

    @{ A device is an entity on the distributed graph which has input and/or
       output signals.  The mapper_device is the primary interface through which
       a program uses libmapper.  A device must have a name, to which a unique
       ordinal is subsequently appended.  It can also be given other
       user-specified metadata.  Devices signals can be connected, which is
       accomplished by requests from an external GUI. */

/*! Allocate and initialize a mapper device.
 *  \param name         A short descriptive string to identify the device.
 *                      Must not contain spaces or the slash character '/'.
 *  \param graph        A previously allocated graph structure to use.
 *                      If 0, one will be allocated for use with this device.
 *  \return             A newly allocated mapper device.  Should be free
 *                      using mapper_device_free(). */
mapper_device mapper_device_new(const char *name, mapper_graph graph);

/*! Free resources used by a mapper device.
 *  \param dev          The device to use. */
void mapper_device_free(mapper_device dev);

/*! Return a unique id associated with a given device.
 *  \param dev          The device to use.
 *  \return             A new unique id. */
mapper_id mapper_device_generate_unique_id(mapper_device dev);

/*! Add a signal to a mapper device.  Values and strings pointed to by this call
 *  (except user_data) will be copied.  For minimum and maximum, actual type
 *  must correspond to 'type' (if type=MAPPER_INT32, then int*, etc).
 *  \param dev          The device to add a signal to.
 *  \param dir          The signal direction.
 *  \param num_insts    The number of signal instances.
 *  \param name         The name of the signal.
 *  \param length   	The length of the signal vector, or 1 for a scalar.
 *  \param type         The type fo the signal value.
 *  \param unit         The unit of the signal, or 0 for none.
 *  \param minimum      Pointer to a minimum value, or 0 for none.
 *  \param maximum      Pointer to a maximum value, or 0 for none.
 *  \param handler      Function to be called when the value of the signal is
 *                      updated.
 *  \return             The new signal. */
mapper_signal mapper_device_add_signal(mapper_device dev, mapper_direction dir,
                                       int num_insts, const char *name,
                                       int length, mapper_type type,
                                       const char *unit,
                                       const void *minimum, const void *maximum,
                                       mapper_signal_update_handler *handler);

/* Remove a device's signal.
 * \param dev           The device owning the signal to be removed.
 * \param sig           The signal to remove. */
void mapper_device_remove_signal(mapper_device dev, mapper_signal sig);

/*! Return the number of signals.
 *  \param dev          The device to query.
 *  \param dir          The direction of the signals to count.
 *  \return             The number of signals. */
int mapper_device_get_num_signals(mapper_device dev, mapper_direction dir);

/*! Return the list of signals for a given device.
 *  \param dev          The device to query.
 *  \param dir          The direction of the signals to return.
 *  \return             A double-pointer to the first item in the list of
 *                      results. Use mapper_object_list_next() to iterate. */
mapper_object *mapper_device_get_signals(mapper_device dev,
                                         mapper_direction dir);

/*! Create a new signal group, used for coordinating signal instances across
 *  related signals.
 *  \param dev          The device.
 *  \return             A new signal group identifier. Use the function
 *                      mapper_signal_set_group() to add or remove signals from
 *                      a group. */
mapper_signal_group mapper_device_add_signal_group(mapper_device dev);

/*! Remove a previously-defined multi-signal group.
 *  \param dev          The device containing the signal group to be removed.
 *  \param group        The signal group identifier to be removed. */
void mapper_device_remove_signal_group(mapper_device dev,
                                       mapper_signal_group group);

/*! Return the number of maps associated with a specific device.
 *  \param dev          The device to check.
 *  \param dir          The direction of the maps relative to the given device.
 *  \return             The number of associated maps. */
int mapper_device_get_num_maps(mapper_device dev, mapper_direction dir);

/*! Return the list of maps associated with a given device.
 *  \param dev          Device record query.
 *  \param dir          The direction of the map relative to the given device.
 *  \return             A double-pointer to the first item in the list of
 *                      results. Use mapper_object_list_next() to iterate. */
mapper_object *mapper_device_get_maps(mapper_device dev, mapper_direction dir);

/*! Poll this device for new messages.  Note, if you have multiple devices, the
 *  right thing to do is call this function for each of them with block_ms=0,
 *  and add your own sleep if necessary.
 *  \param dev          The device to check messages for.
 *  \param block_ms     Number of milliseconds to block waiting for messages, or
 *                      0 for non-blocking behaviour.
 *  \return             The number of handled messages. May be zero if there was
 *                      nothing to do. */
int mapper_device_poll(mapper_device dev, int block_ms);

/*! Detect whether a device is completely initialized.
 *  \param dev          The device to query.
 *  \return             Non-zero if device is completely initialized, i.e., has
 *                      an allocated receiving port and unique identifier.
 *                      Zero otherwise. */
int mapper_device_ready(mapper_device dev);

/*! Start a time-tagged mapper queue.
 *  \param dev          The device to use.
 *  \param time         A time to use for the updates bundled by this queue,
 *                      or MAPPER_NOW to use the current local time.
 *  \return             The time used to identify this queue. */
mapper_time_t mapper_device_start_queue(mapper_device dev, mapper_time_t time);

/*! Dispatch a time-tagged mapper queue.
 *  \param dev          The device to use.
 *  \param time         The time for an existing queue created with
 *                      mapper_device_start_queue(). */
void mapper_device_send_queue(mapper_device dev, mapper_time_t time);

/* @} */

/***** Maps *****/

/*! @defgroup maps Maps

    @{ Maps define dataflow connections between sets of signals. A map consists
       of one or more sources, one destination, and properties which determine
       how the source data is processed. */

/*! Create a map between a set of signals.
 *  \param num_sources  The number of source signals in this map.
 *  \param sources      Array of source signal data structures.
 *  \param num_destinations  The number of destination signals in this map.
 *                      Currently restricted to 1.
 *  \param destinations Array of destination signal data structures.
 *  \return             A map data structure – either loaded from the graph
 *                      (if the map already existed) or newly created. In the
 *                      latter case the map will not take effect until it has
 *                      been added to the distributed graph using
 *                      mapper_map_push(). */
mapper_map mapper_map_new(int num_sources, mapper_signal *sources,
                          int num_destinations, mapper_signal *destinations);

/*! Remove a map between a set of signals.
 *  \param map          The map to destroy. */
void mapper_map_release(mapper_map map);

/*! Get the number of signals for to a specific map.
 *  \param map          The map to check.
 *  \param loc          MAPPER_LOC_SOURCE, MAPPER_LOC_DESTINATION,
 *  \return             The number of signals. */
int mapper_map_get_num_signals(mapper_map map, mapper_location loc);

/*! Retrieve a signal for a specific map.
 *  \param map          The map to check.
 *  \param loc          The map endpoint, must be MAPPER_LOC_SOURCE or
 *                      MAPPER_LOC_DESTINATION.
 *  \param index        The signal index.
 *  \return             The signal, or NULL if not available. */
mapper_signal mapper_map_get_signal(mapper_map map, mapper_location loc,
                                    int index);

/*! Retrieve the index for a specific map signal.
 *  \param map          The map to check.
 *  \param sig          The signal to find.
 *  \return             The signal index. */
int mapper_map_get_signal_index(mapper_map map, mapper_signal sig);

/*! Get the scopes property for a specific map.
 *  \param map          The map to check.
 *  \return             A double-pointer to the first item in the list of
 *                      results or zero if none.  Use mapper_object_list_next() to
 *                      iterate. */
mapper_object *mapper_map_get_scopes(mapper_map map);

/*! Detect whether a map is completely initialized.
 *  \param map          The device to query.
 *  \return             Non-zero if map is completely initialized, zero
 *                      otherwise. */
int mapper_map_ready(mapper_map map);

/*! Re-create stale map if necessary.
 *  \param map          The map to operate on. */
void mapper_map_refresh(mapper_map map);

/*! Add a scope to this map. Map scopes configure the propagation of signal
 *  instance updates across the map. Changes to remote maps will not take effect
 *  until synchronized with the distributed graph using mapper_map_push().
 *  \param map          The map to modify.
 *  \param dev          Device to add as a scope for this map. After taking
 *                      effect, this setting will cause instance updates
 *                      originating at this device to be propagated across the
 *                      map. */
void mapper_map_add_scope(mapper_map map, mapper_device dev);

/*! Remove a scope from this map. Map scopes configure the propagation of signal
 *  instance updates across the map. Changes to remote maps will not take effect
 *  until synchronized with the distributed graph using mapper_map_push().
 *  \param map          The map to modify.
 *  \param dev          Device to remove as a scope for this map. After taking
 *                      effect, this setting will cause instance updates
 *                      originating at this device to be blocked from
 *                      propagating across the map. */
void mapper_map_remove_scope(mapper_map map, mapper_device dev);

//const char *mapper_map_set_expression(mapper_map map, const char *expr);
const char *mapper_map_set_linear(mapper_map map, int srclen, mapper_type srctype,
                                  const void *srcmin, const void *srcmax,
                                  int dstlen, mapper_type dsttype,
                                  const void *dstmin, const void *dstmax);

//const char *mapper_map_set_linear_calib(mapper_map map, int dstlen,
//                                        mapper_type dsttype, const void *dstmin,
//                                        const void *dstmax, int timeout)

/* @} */

/***** Graph *****/

/*! @defgroup graphs Graphs

    @{ Graphs are the primary interface through which a program may observe
       the distributed graph and store information about devices and signals
       that are present.  Each Graph stores records of devices, signals, and
       maps, which can be queried. */

/*! Create a peer in the libmapper distributed graph.
 *  \param autosubscribe_flags  Sets whether the graph should automatically
 *                              subscribe to information about signals and maps
 *                              when it encounters a previously-unseen device.
 *  \return                     The new graph. */
mapper_graph mapper_graph_new(int autosubscribe_flags);

/*! Specify network interface to use.
 *  \param g            The graph structure to use.
 *  \param interface    The name of the network interface to use. */
void mapper_graph_set_interface(mapper_graph g, const char *interface);

/*! Return a string indicating the name of the network interface in use.
 *  \param g            The graph structure to query.
 *  \return             A string containing the name of the network interface. */
const char *mapper_graph_get_interface(mapper_graph g);

/*! Set the multicast group and port to use.
 *  \param g            The graph structure to query.
 *  \param group        A string specifying the multicast group for bus
 *                      communication with the distributed graph.
 *  \param port         The port to use for multicast communication. */
void mapper_graph_set_multicast_addr(mapper_graph g, const char *group, int port);

/*! Retrieve the multicast group currently in use.
 *  \param g            The graph structure to query.
 *  \return             A string specifying the multicast url for bus
 *                      communication with the distributed graph. */
const char *mapper_graph_get_multicast_addr(mapper_graph g);

/*! Synchonize a local graph copy with the distributed graph.
 *  \param g            The graph to update.
 *  \param block_ms     The number of milliseconds to block, or 0 for
 *                      non-blocking behaviour.
 *  \return             The number of handled messages. */
int mapper_graph_poll(mapper_graph g, int block_ms);

/*! Free a graph.
 *  \param g            The graph to free. */
void mapper_graph_free(mapper_graph g);

/*! Print the contents of a graph.
 *  \param g            The graph to print. */
void mapper_graph_print(mapper_graph g);

/*! Subscribe to information about a specific device.
 *  \param g            The graph to use.
 *  \param dev          The device of interest. If NULL the graph will
 *                      automatically subscribe to all discovered devices.
 *  \param types        Bitflags setting the type of information of interest.
 *                      Can be a combination of mapper_object_type values.
 *  \param timeout      The length in seconds for this subscription. If set to
 *                      -1, the graph will automatically renew the
 *                      subscription until it is freed or this function is
 *                      called again. */
void mapper_graph_subscribe(mapper_graph g, mapper_device dev, int types,
                            int timeout);

/*! Unsubscribe from information about a specific device.
 *  \param g            The graph to use.
 *  \param dev          The device of interest. If NULL the graph will
 *                      unsubscribe from all devices. */
void mapper_graph_unsubscribe(mapper_graph g, mapper_device dev);

/*! Send a request to the distributed graph for all active devices to report in.
 *  \param g            The graph to use. */
void mapper_graph_request_devices(mapper_graph g);

/*! A callback function prototype for when an object record is added or updated.
 *  Such a function is passed in to mapper_graph_add_callback().
 *  \param g            The graph that registered this callback.
 *  \param obj          The object record.
 *  \param event        A value of mapper_record_event indicating what is
 *                      happening to the object record.
 *  \param user         The user context pointer registered with this callback. */
typedef void mapper_graph_handler(mapper_graph g, mapper_object obj,
                                  mapper_record_event event, const void *user);

/*! Register a callback for when an object record is added or updated in the
 *  graph.
 *  \param g            The graph to query.
 *  \param h            Callback function.
 *  \param types        Bitflags setting the type of information of interest.
 *                      Can be a combination of mapper_object_type values.
 *  \param user         A user-defined pointer to be passed to the callback
 *                      for context. */
void mapper_graph_add_callback(mapper_graph g, mapper_graph_handler *h,
                               int types, const void *user);

/*! Remove a device record callback from the graph service.
 *  \param g            The graph to query.
 *  \param h            Callback function.
 *  \param user         The user context pointer that was originally specified
 *                      when adding the callback. */
void mapper_graph_remove_callback(mapper_graph g, mapper_graph_handler *h,
                                  const void *user);

/*! Return the number of objects stored in the graph.
 *  \param g            The graph to query.
 *  \param types        Bitflags setting the type of information of interest.
 *                      Can be a combination of mapper_object_type values.
 *  \return             The number of devices. */
int mapper_graph_get_num_objects(mapper_graph g, int types);

/*! Return a list of objects.
 *  \param g            The graph to query.
 *  \param types        Bitflags setting the type of information of interest.
 *                      Can be a combination of mapper_object_type values.
 *  \return             A double-pointer to the first item in the list of
 *                      results.  Use mapper_object_list_next() to iterate. */
mapper_object *mapper_graph_get_objects(mapper_graph g, int types);

/*! Return the list of maps that use the given scope.
 *  \param g            The graph to query.
 *  \param dev          The device owning the scope to query.
 *  \return             A double-pointer to the first item in a list of results.
 *                      Use mapper_object_list_next() to iterate. */
mapper_object *mapper_graph_get_maps_by_scope(mapper_graph g, mapper_device dev);

/* @} */

/***** Time *****/

/*! @defgroup times Times

 @{ libmapper primarily uses NTP timetags for communication and
    synchronization. */

/*! Initialize a time structure to the current time.
 *  \param time         A previously allocated time structure to initialize. */
void mapper_time_now(mapper_time_t *time);

/*! Add a time to another given time.
 *  \param augend       A previously allocated time to augment.
 *  \param addend       A time to add. */
void mapper_time_add(mapper_time_t *augend, mapper_time_t addend);

/*! Add a double-precision floating point value to another given time.
 *  \param augend       A previously allocated time to augment.
 *  \param addend       A value in seconds to add. */
void mapper_time_add_double(mapper_time_t *augend, double addend);

/*! Subtract a time from another given time.
 *  \param minuend      A previously allocated time to augment.
 *  \param subtrahend   A time to add to subtract. */
void mapper_time_subtract(mapper_time_t *minuend, mapper_time_t subtrahend);

/*! Add a double-precision floating point value to another given time.
 *  \param time         A previously allocated time to multiply.
 *  \param multiplicand A value in seconds. */
void mapper_time_multiply(mapper_time_t *time, double multiplicand);

/*! Return value of mapper_time as a double-precision floating point value.
 *  \param time         The time to read.
 *  \return             Value of the time as a double-precision float. */
double mapper_time_get_double(mapper_time_t time);

/*! Set value of a mapper_time from a double-precision floating point value.
 *  \param time         A previously-allocated time to set.
 *  \param value        The value in seconds to set. */
void mapper_time_set_double(mapper_time_t *time, double value);

/*! Copy value of a mapper_time.
 *  \param timel        The target time for copying.
 *  \param timer        The source time. */
void mapper_time_copy(mapper_time_t *timel, mapper_time_t timer);

/* @} */

/*! Get the version of libmapper.
 *  \return             A string specifying the version of libmapper. */
const char *mapper_version(void);

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_H__
