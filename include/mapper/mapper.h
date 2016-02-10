#ifndef __MAPPER_H__
#define __MAPPER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <mapper/mapper_constants.h>
#include <mapper/mapper_types.h>

#define MAPPER_NOW ((mapper_timetag_t){0L,1L})

/*! \mainpage libmapper

This is the API documentation for libmapper, a network-based signal mapping
framework.  Please see the Modules section for detailed information, and be
sure to consult the tutorial to get started with libmapper concepts.

 */

/*** Signals ***/

/*! @defgroup signals Signals

    @{ Signals define inputs or outputs for devices.  A signal
       consists of a scalar or vector value of some integer or
       floating-point type.  A mapper_signal is created by adding an
       input or output to a device.  It can optionally be provided
       with some metadata such as a signal's range, unit, or other
       properties.  Signals can be mapped by creating maps through a
       GUI. */

/*! Update the value of a signal.  The signal will be routed according to
 *  external requests.
 *  \param sig      The signal to update.
 *  \param value    A pointer to a new value for this signal.  If the signal
 *                  type is 'i', this should be int*; if the signal type is 'f',
 *                  this should be float*.  It should be an array at least as
 *                  long as the signal's length property.
 *  \param count    The number of instances of the value that are being updated.
 *                  For non-periodic signals, this should be 0 or 1.  For
 *                  periodic signals, this may indicate that a block of values
 *                  should be accepted, where the last value is the current
 *                  value.
 *  \param tt       The time at which the value update was aquired. If the value
 *                  is MAPPER_NOW, libmapper will tag the value update with the
 *                  current time. See mapper_device_start_queue() for more
 *                  information on bundling multiple signal updates with the
 *                  same timetag. */
void mapper_signal_update(mapper_signal sig, const void *value, int count,
                          mapper_timetag_t tt);

/*! Update the value of a scalar signal of type int. This is a scalar
 *  equivalent to mapper_signal_update(), for when passing by value is more
 *  convenient than passing a pointer.
 *  The signal will be routed according to external requests.
 *  \param sig   The signal to update.
 *  \param value A new scalar value for this signal. */
void mapper_signal_update_int(mapper_signal sig, int value);

/*! Update the value of a scalar signal of type float. This is a scalar
 *  equivalent to mapper_signal_update(), for when passing by value is more
 *  convenient than passing a pointer.
 *  The signal will be routed according to external requests.
 *  \param sig The signal to update.
 *  \param value A new scalar value for this signal. */
void mapper_signal_update_float(mapper_signal sig, float value);

/*! Update the value of a scalar signal of type double. This is a scalar
 *  equivalent to mapper_signal_update(), for when passing by value is more
 *  convenient than passing a pointer.
 *  The signal will be routed according to external requests.
 *  \param sig      The signal to update.
 *  \param value    A new scalar value for this signal. */
void mapper_signal_update_double(mapper_signal sig, double value);

/*! Get a signal's value.
 *  \param sig      The signal to operate on.
 *  \param tt       A location to receive the value's time tag. May be 0.
 *  \return         A pointer to an array containing the value
 *                  of the signal, or 0 if the signal has no value. */
const void *mapper_signal_value(mapper_signal sig, mapper_timetag_t *tt);

/*! Query the values of any signals connected via mapping connections.
 *  \param sig      A local output signal. We will be querying the remote
 *                  ends of this signal's mapping connections.
 *  \param tt       A timetag to be attached to the outgoing query. Query
 *                  responses should also be tagged with this time.
 *  \return The number of queries sent, or -1 for error. */
int mapper_signal_query_remotes(mapper_signal sig, mapper_timetag_t tt);

/*! Return the list of maps associated with a given signal.
 *  \param sig      Signal record to query for maps.
 *  \param dir      The direction of the map relative to the given signal. Can
 *                  be MAPPER_INCOMING, MAPPER_OUTGOING, or MAPPER_DIR_ANY.
 *  \return         A double-pointer to the first item in the list of results
 *                  or zero if none.  Use mapper_map_query_next() to iterate. */
mapper_map *mapper_signal_maps(mapper_signal sig, mapper_direction dir);

/*! Associate a signal or signal instance with an arbitrary pointer.
 *  \param sig          The signal to operate on.
 *  \param user_data    A pointer to user data to be associated. */
void mapper_signal_set_user_data(mapper_signal sig, const void *user_data);

/*! Retrieve the arbitrary pointer associated with a signal instance.
 *  \param sig          The signal to operate on.
 *  \return             A pointer associated with this signal. */
void *mapper_signal_user_data(mapper_signal sig);

/*! A signal handler function can be called whenever a signal value changes. */
typedef void mapper_signal_update_handler(mapper_signal sig, mapper_id instance,
                                          const void *value, int count,
                                          mapper_timetag_t *tt);

/*! Set or unset the message handler for a signal.
 *  \param sig          The signal to operate on.
 *  \param handler      A pointer to a mapper_signal_update_handler function
 *                      for processing incoming messages.
 *  \param user_data    User context pointer to be passed to handler. */
void mapper_signal_set_callback(mapper_signal sig,
                                mapper_signal_update_handler *handler,
                                const void *user_data);

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

/*! Update the value of a specific signal instance.
 *  The signal will be routed according to external requests.
 *  \param sig          The signal to operate on.
 *  \param instance     The identifier of the instance to update.
 *  \param value        A pointer to a new value for this signal.  If the signal
 *                      type is 'i', this should be int*; if the signal type is
 *                      'f', this should be float* (etc).  It should be an array
 *                      at least as long as the signal's length property.
 *  \param count        The number of values being updated, or 0 for
 *                      non-periodic signals.
 *  \param tt           The time at which the value update was aquired. If NULL,
 *                      libmapper will tag the value update with the current
 *                      time.  See mapper_device_start_queue() for more
 *                      information on bundling multiple signal updates with the
 *                      same timetag. */
void mapper_signal_instance_update(mapper_signal sig, mapper_id instance,
                                   const void *value, int count,
                                   mapper_timetag_t tt);

/*! Release a specific instance of a signal by removing it from the list
 *  of active instances and adding it to the reserve list.
 *  \param sig          The signal to operate on.
 *  \param instance     The identifier of the instance to suspend.
 *  \param tt           The time at which the instance was released; if NULL,
 *                      will be tagged with the current time. See
 *                      mapper_device_start_queue() for more information on
 *                      bundling multiple signal updates with the same timetag. */
void mapper_signal_instance_release(mapper_signal sig, mapper_id instance,
                                    mapper_timetag_t tt);

/*! Remove a specific instance of a signal and free its memory.
 *  \param sig          The signal to operate on.
 *  \param instance     The identifier of the instance to suspend. */
void mapper_signal_remove_instance(mapper_signal sig, mapper_id instance);

/*! Return whether a given signal instance is currently active.
 *  \param sig          The signal to operate on.
 *  \param instance     The identifier of the instance to check.
 *  \return             Non-zero if the instance is active, zero otherwise. */
int mapper_signal_instance_is_active(mapper_signal sig, mapper_id instance);

/*! Activate a specific signal instance.
 *  \param sig          The signal to operate on.
 *  \param instance     The identifier of the instance to activate.
 *  \return             Non-zero if the instance is active, zero otherwise. */
int mapper_signal_instance_activate(mapper_signal sig, mapper_id id);

/*! Get the local id of the oldest active instance.
 *  \param sig          The signal to operate on.
 *  \return             The instance identifier, or zero if unsuccessful. */
mapper_id mapper_signal_oldest_active_instance(mapper_signal sig);

/*! Get the local id of the newest active instance.
 *  \param sig          The signal to operate on.
 *  \return             The instance identifier, or zero if unsuccessful. */
mapper_id mapper_signal_newest_active_instance(mapper_signal sig);

/*! Get a signal_instance's value.
 *  \param sig          The signal to operate on.
 *  \param instance     The identifier of the instance to operate on.
 *  \param tt           A location to receive the value's time tag. May be 0.
 *  \return             A pointer to an array containing the value of the signal
 *                      instance, or 0 if the signal instance has no value. */
const void *mapper_signal_instance_value(mapper_signal sig, mapper_id instance,
                                         mapper_timetag_t *tt);

/*! Return the number of active instances owned by a signal.
 *  \param  sig The signal to query.
 *  \return     The number of active instances. */
int mapper_signal_num_active_instances(mapper_signal sig);

/*! Return the number of reserved instances owned by a signal.
 *  \param  sig The signal to query.
 *  \return     The number of active instances. */
int mapper_signal_num_reserved_instances(mapper_signal sig);

/*! Get a signal instance's identifier by its index.  Intended to be used for
 *  iterating over the active instances.
 *  \param sig      The signal to operate on.
 *  \param index    The numerical index of the ID to retrieve.  Shall be between
 *                  zero and the return value of mapper_signal_num_instances().
 *  \return         The instance ID associated with the given index, or zero if
 *                  unsuccessful. */
mapper_id mapper_signal_instance_id(mapper_signal sig, int index);

/*! Get an active signal instance's ID by its index.  Intended to be used for
 *  iterating over the active instances.
 *  \param sig      The signal to operate on.
 *  \param index    The numerical index of the ID to retrieve.  Shall be between
 *                  zero and the return value of
 *                  mapper_signal_num_active_instances().
 *  \return         The instance ID associated with the given index, or zero if
 *                  unsuccessful. */
mapper_id mapper_signal_active_instance_id(mapper_signal sig, int index);

/*! Get a reserved signal instance's ID by its index.  Intended to be used for
 *  iterating over the reserved instances.
 *  \param sig      The signal to operate on.
 *  \param index    The numerical index of the ID to retrieve.  Shall be between
 *                  zero and the return value of
 *                  mapper_signal_num_active_instances().
 *  \return         The instance ID associated with the given index, or zero if
 *                  unsuccessful. */
mapper_id mapper_signal_reserved_instance_id(mapper_signal sig, int index);

/*! Set the stealing method to be used when a previously-unseen instance ID is
 *  received and no instances are available.
 *  \param sig      The signal to operate on.
 *  \param mode     Method to use for adding or reallocating active instances
 *                  if no reserved instances are available. */
void mapper_signal_set_instance_stealing_mode(mapper_signal sig,
                                              mapper_instance_stealing_type mode);

/*! Get the allocation method to be used when a previously-unseen
 *  instance ID is received.
 *  \param sig  The signal to operate on.
 *  \return     The allocation mode of the provided signal. */
mapper_instance_stealing_type mapper_signal_instance_stealing_mode(mapper_signal sig);

/*! A handler function to be called whenever a signal instance management
 *  event occurs. */
typedef void mapper_instance_event_handler(mapper_signal sig, mapper_id instance,
                                           int event, mapper_timetag_t *tt);

/*! Set the handler to be called on signal instance management events.
 *  \param sig          The signal to operate on.
 *  \param h            A handler function for instance management events.
 *  \param flags        Bitflags for indicating the types of events which should
 *                      trigger the callback. Can be a combination of MAPPER_NEW,
 *                      MAPPER_UPSTREAM_RELEASE, MAPPER_DOWNSTREAM_RELEASE, and
 *                      MAPPER_INSTANCE_OVERFLOW.
 *  \param user_data    User context pointer to be passed to handler. */
void mapper_signal_set_instance_event_callback(mapper_signal sig,
                                               mapper_instance_event_handler h,
                                               int flags, const void *user_data);

/*! Associate a signal instance with an arbitrary pointer.
 *  \param sig          The signal to operate on.
 *  \param instance     The identifier of the instance to operate on.
 *  \param user_data    A pointer to user data to be associated
 *                      with this instance. */
void mapper_signal_instance_set_user_data(mapper_signal sig, mapper_id instance,
                                          const void *user_data);

/*! Retrieve the arbitrary pointer associated with a signal instance.
 *  \param sig          The signal to operate on.
 *  \param instance     The identifier of the instance to operate on.
 *  \return             A pointer associated with this instance. */
void *mapper_signal_instance_user_data(mapper_signal sig, mapper_id instance);

/**** Signal properties ****/

/*! Get the description for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal description if it is defined, or NULL. */
const char *mapper_signal_description(mapper_signal sig);

/*! Get the parent mapper_device for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal's parent device. */
mapper_device mapper_signal_device(mapper_signal sig);

/*! Get the direction for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal direction: MAPPER_INCOMING or MAPPER_OUTGOING. */
mapper_direction mapper_signal_direction(mapper_signal sig);

/*! Get the unique id for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal id. */
mapper_id mapper_signal_id(mapper_signal sig);

/*! Indicate whether this signal is local.
 *  \param sig      The signal to check.
 *  \return         1 if the signal is local, 0 otherwise. */
int mapper_signal_is_local(mapper_signal sig);

/*! Get the vector length for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal vector length. */
int mapper_signal_length(mapper_signal sig);

/*! Get the maximum for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal maximum if it is defined, or NULL. */
void *mapper_signal_maximum(mapper_signal sig);

/*! Get the minimum for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal minimum if it is defined, or NULL. */
void *mapper_signal_minimum(mapper_signal sig);

/*! Get the name for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal name. */
const char *mapper_signal_name(mapper_signal sig);

/*! Get the number of instances for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The number of allocated signal instances. */
int mapper_signal_num_instances(mapper_signal sig);

/*! Get the number of maps associated with a specific signal.
 *  \param sig      The signal to check.
 *  \param dir      The direction of the maps relative to the given signal. Can
 *                  be MAPPER_INCOMING, MAPPER_OUTGOING, or MAPPER_DIR_ANY.
 *  \return         The number of associated maps. */
int mapper_signal_num_maps(mapper_signal sig, mapper_direction dir);

/*! Get the update rate for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The rate for this signal in samples/second, or zero
 *                  for non-periodic signals. */
float mapper_signal_rate(mapper_signal sig);

/*! Get the data type for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal date type. */
char mapper_signal_type(mapper_signal sig);

/*! Get the unit for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal unit if it is defined, or NULL. */
const char *mapper_signal_unit(mapper_signal sig);

/*! Get the total number of properties for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The number of properties. */
int mapper_signal_num_properties(mapper_signal sig);

/*! Look up a signal property by name.
 *  \param sig      The signal to check.
 *  \param name     The name of the property to retrieve.
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \return Zero if found, otherwise non-zero. */
int mapper_signal_property(mapper_signal sig, const char *name, int *length,
                           char *type, const void **value);

/*! Look up a signal property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param sig      The signal to check.
 *  \param index    Numerical index of a signal property.
 *  \param name     Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \return Zero if found, otherwise non-zero. */
int mapper_signal_property_index(mapper_signal sig, unsigned int index,
                                 const char **name, int *length, char *type,
                                 const void **value);

/*! Set the description property for a specific signal.
 *  \param sig          The signal to modify.
 *  \param description  The description value to set. */
void mapper_signal_set_description(mapper_signal sig, const char *description);

/*! Set or remove the maximum of a signal.
 *  \param sig      The signal to modify.
 *  \param maximum  Must be the same type as the signal, or 0 to remove
 *                  the maximum. */
void mapper_signal_set_maximum(mapper_signal sig, const void *maximum);

/*! Set or remove the minimum of a signal.
 *  \param sig      The signal to modify.
 *  \param minimum  Must be the same type as the signal, or 0 to remove
 *                  the minimum. */
void mapper_signal_set_minimum(mapper_signal sig, const void *minimum);

/*! Set the rate of a signal.
 *  \param sig      The signal to modify.
 *  \param rate     A rate for this signal in samples/second, or zero
 *                  for non-periodic signals. */
void mapper_signal_set_rate(mapper_signal sig, float rate);

/*! Set the unit of a signal.
 *  \param sig      The signal to operate on.
 *  \param unit     The unit value to set. */
void mapper_signal_set_unit(mapper_signal sig, const char *unit);

/*! Set a property of a signal.  Can be used to provide arbitrary
 *  metadata.  Value pointed to will be copied.
 *  \param sig      The signal to operate on.
 *  \param name     The name of the property to add.
 *  \param length   The length of value array.
 *  \param type     The property  datatype.
 *  \param value    An array of property values.
 *  \return         1 if property has been changed, 0 otherwise. */
int mapper_signal_set_property(mapper_signal sig, const char *name,
                               int length, char type, const void *value);

/*! Push any property changes out to the network.
 *  \param sig      The signal to operate on. */
void mapper_signal_push(mapper_signal sig);

/*! Remove a property of a signal.
 *  \param sig      The signal to operate on.
 *  \param name     The name of the property to remove.
 *  \return         1 if property has been removed, 0 otherwise. */
int mapper_signal_remove_property(mapper_signal sig, const char *name);

/*! Get the union of two signal queries (signals matching query1 OR query2).
 *  \param query1   The first signal query.
 *  \param query2   The second signal query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_signal_query_next() to iterate. */
mapper_signal *mapper_signal_query_union(mapper_signal *query1,
                                         mapper_signal *query2);

/*! Get the intersection of two signal queries (signals matching query1 AND
 *  query2).
 *  \param query1   The first signal query.
 *  \param query2   The second signal query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_signal_query_next() to iterate. */
mapper_signal *mapper_signal_query_intersection(mapper_signal *query1,
                                                mapper_signal *query2);

/*! Get the difference between two signal queries (signals matching query1 but
 *  NOT query2).
 *  \param query1   The first signal query.
 *  \param query2   The second signal query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_signal_query_next() to iterate. */
mapper_signal *mapper_signal_query_difference(mapper_signal *query1,
                                              mapper_signal *query2);

/*! Given a signal record pointer returned from a previous signal query, get an
 *  indexed item in the list.
 *  \param query    The previous signal record pointer.
 *  \param index    The index of the list element to retrieve.
 *  \return         A pointer to the signal record, or zero if it doesn't
 *                  exist. */
mapper_signal mapper_signal_query_index(mapper_signal *query, int index);

/*! Given a signal record pointer returned from a previous signal query, get the
 *  next item in the list.
 *  \param query    The previous signal record pointer.
 *  \return         A double-pointer to the next signal record in the list, or
 *                  zero if no more signals. */
mapper_signal *mapper_signal_query_next(mapper_signal *query);

/*! Copy a previously-constructed signal query.
 *  \param query    The previous signal record pointer.
 *  \return         A double-pointer to the copy of the list, or zero if none.
 *                  Use mapper_signal_query_next() to iterate. */
mapper_signal *mapper_signal_query_copy(mapper_signal *query);

/*! Given a signal record pointer returned from a previous signal query,
 *  indicate that we are done iterating.
 *  \param query    The previous signal record pointer. */
void mapper_signal_query_done(mapper_signal *query);

/*! Helper to print the properties of a specific signal.
 *  \param sig                  The signal to print.
 *  \param include_device_name  1 to include the parent device name, 0
 *                              otherwise. */
void mapper_signal_print(mapper_signal sig, int include_device_name);

/* @} */

/*** Devices ***/

/*! @defgroup devices Devices

    @{ A device is an entity on the network which has input and/or output
       signals.  The mapper_device is the primary interface through which a
       program uses libmapper.  A device must have a name, to which a unique
       ordinal is subsequently appended.  It can also be given other
       user-specified metadata.  Devices signals can be connected, which is
       accomplished by requests from an external GUI. */

/*! A callback function prototype for when a map is added, updated, or removed
 *  from a given device.  Such a function is passed in to
 *  mapper_device_set_map_handler().
 *  \param map      The map record.
 *  \param action   A value of mapper_record_action indicating what is
 *                  happening to the map record.
 *  \param user     The user context pointer registered with this callback. */
typedef void mapper_device_map_handler(mapper_map map,
                                       mapper_record_action action,
                                       const void *user);

/*! Set a function to be called when a map involving a device's local signals is
 *  established, modified or destroyed, indicated by the action parameter to the
 *  provided function. */
void mapper_device_set_map_handler(mapper_device dev,
                                   mapper_device_map_handler *h,
                                   const void *user);

/*! Allocate and initialize a mapper device.
 *  \param name_prefix  A short descriptive string to identify the device.
 *  \param port         An optional port for starting the port allocation
 *                      scheme.
 *  \param net          A previously allocated network structure to use.
 *                      If 0, one will be allocated for use with this device.
 *  \return             A newly allocated mapper device.  Should be free
 *                      using mapper_device_free(). */
mapper_device mapper_device_new(const char *name_prefix, int port,
                                mapper_network net);

//! Free resources used by a mapper device.
void mapper_device_free(mapper_device dev);

/*! Return a unique id associated with a given device. */
mapper_id mapper_device_unique_id(mapper_device dev);

/*! Retrieve the networking structure from a device.
 *  \param dev          The device to use.
 *  \return             The device network data structure. */
mapper_network mapper_device_network(mapper_device dev);

/*! Associate a device with an arbitrary pointer.
 *  \param dev          The device to operate on.
 *  \param user_data    A pointer to user data to be associated. */
void mapper_device_set_user_data(mapper_device dev, const void *user_data);

/*! Retrieve the arbitrary pointer associated with a device.
 *  \param dev          The device to operate on.
 *  \return             A pointer associated with this device. */
void *mapper_device_user_data(mapper_device dev);

/*! Add a signal to a mapper device.  Values and strings pointed to by
 *  this call (except user_data) will be copied.  For minimum and maximum,
 *  actual type must correspond to 'type' (if type='i', then int*, etc).
 *  \param dev          The device to add a signal to.
 *  \param dir          The signal direction: MAPPER_DIR_INCOMING for inputs or
 *                      MAPPER_DIR_OUTGOING for outputs.
 *  \param name         The name of the signal.
 *  \param length   	The length of the signal vector, or 1 for a scalar.
 *  \param type         The type fo the signal value.
 *  \param unit         The unit of the signal, or 0 for none.
 *  \param minimum      Pointer to a minimum value, or 0 for none.
 *  \param maximum      Pointer to a maximum value, or 0 for none.
 *  \param handler      Function to be called when the value of the
 *                      signal is updated.
 *  \param user_data    User context pointer to be passed to handler. */
mapper_signal mapper_device_add_signal(mapper_device dev,
                                       mapper_direction dir, const char *name,
                                       int length, char type, const char *unit,
                                       const void *minimum, const void *maximum,
                                       mapper_signal_update_handler *handler,
                                       const void *user_data);

/*! Add an input signal to a mapper device.  Values and strings pointed to by
 *  this call (except user_data) will be copied.  For minimum and maximum,
 *  actual type must correspond to 'type' (if type='i', then int*, etc).
 *  \param dev          The device to add a signal to.
 *  \param name         The name of the signal.
 *  \param length   	The length of the signal vector, or 1 for a scalar.
 *  \param type         The type fo the signal value.
 *  \param unit         The unit of the signal, or 0 for none.
 *  \param minimum      Pointer to a minimum value, or 0 for none.
 *  \param maximum      Pointer to a maximum value, or 0 for none.
 *  \param handler      Function to be called when the value of the
 *                      signal is updated.
 *  \param user_data    User context pointer to be passed to handler. */
mapper_signal mapper_device_add_input_signal(mapper_device dev, const char *name,
                                             int length, char type, const char *unit,
                                             const void *minimum, const void *maximum,
                                             mapper_signal_update_handler *handler,
                                             const void *user_data);

/*! Add an output signal to a mapper device.  Values and strings pointed to by
 *  this call (except user_data) will be copied.  For minimum and maximum,
 *  actual type must correspond to 'type' (if type='i', then int*, etc).
 *  \param dev          The device to add a signal to.
 *  \param name         The name of the signal.
 *  \param length   	The length of the signal vector, or 1 for a scalar.
 *  \param type         The type fo the signal value.
 *  \param unit         The unit of the signal, or 0 for none.
 *  \param minimum      Pointer to a minimum value, or 0 for none.
 *  \param maximum      Pointer to a maximum value, or 0 for none. */
mapper_signal mapper_device_add_output_signal(mapper_device dev, const char *name,
                                              int length, char type, const char *unit,
                                              const void *minimum, const void *maximum);

/* Remove a device's signal.
 * \param dev The device to remove a signal from.
 * \param sig The signal to remove. */
void mapper_device_remove_signal(mapper_device dev, mapper_signal sig);

//! Return the number of signals.
int mapper_device_num_signals(mapper_device dev, mapper_direction dir);

/*! Return the list of signals for a given device.
 *  \param dev      Device record to query.
 *  \param dir      The direction of the signals to return. Can be
 *                  MAPPER_INCOMING, MAPPER_OUTGOING, or MAPPER_DIR_ANY.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_signal_query_next() to iterate. */
mapper_signal *mapper_device_signals(mapper_device dev, mapper_direction dir);

/*! Find information for a registered signal.
 *  \param dev      Device record to query.
 *  \param id       Id of the signal to find in the database.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_device_signal_by_id(mapper_device dev, mapper_id id);

/*! Find information for a registered signal.
 *  \param dev      Device record to query.
 *  \param sig_name Name of the signal to find in the database.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_device_signal_by_name(mapper_device dev,
                                           const char *sig_name);

/*! Find information for a registered signal.
 *  \param dev      Device record to query.
 *  \param index    Index of the signal to find in the database.
 *  \param dir      The direction of the signal to return. Can be
 *                  MAPPER_INCOMING, MAPPER_OUTGOING, or MAPPER_DIR_ANY.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_device_signal_by_index(mapper_device dev, int index,
                                            mapper_direction dir);

/*! Get the description for a specific device.
 *  \param dev      The device to check.
 *  \return         The device description if it is defined, or NULL. */
const char *mapper_device_description(mapper_device dev);

/*! Return the number of maps associated with a specific device.
 *  \param dev      The device to check.
 *  \param dir      The direction of the maps relative to the given device.  Can
 *                  be MAPPER_INCOMING, MAPPER_OUTGOING, or MAPPER_DIR_ANY.
 *  \return         The number of associated maps. */
int mapper_device_num_maps(mapper_device dev, mapper_direction dir);

/*! Return the list of maps associated with a given device.
 *  \param dev      Device record query.
 *  \param dir      The direction of the map relative to the given device. Can
 *                  be MAPPER_INCOMING, MAPPER_OUTGOING, or MAPPER_DIR_ANY.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_map_query_next() to iterate. */
mapper_map *mapper_device_maps(mapper_device dev, mapper_direction dir);

/*! Get the total number of properties for a specific device.
 *  \param dev      The device to check.
 *  \return         The number of properties. */
int mapper_device_num_properties(mapper_device dev);

/*! Look up a device property by name.
 *  \param dev      The device record to check.
 *  \param name     The name of the property to retrieve.
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \return         Zero if found, otherwise non-zero. */
int mapper_device_property(mapper_device dev, const char *name, int *length,
                           char *type, const void **value);

/*! Look up a device property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param dev      The device record to check.
 *  \param index    Numerical index of a device property.
 *  \param name     Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \return Zero if found, otherwise non-zero. */
int mapper_device_property_index(mapper_device dev, unsigned int index,
                                 const char **name, int *length, char *type,
                                 const void **value);

/*! Set the description property for a specific local device.
 *  \param dev          The device to modify.
 *  \param description  The description value to set. */
void mapper_device_set_description(mapper_device dev, const char *description);

/*! Set a property of a device.  Can be used to provide arbitrary
 *  metadata.  Value pointed to will be copied.
 *  \param dev      The device to operate on.
 *  \param name     The name of the property to add.
 *  \param length   The length of value array.
 *  \param type     The property  datatype.
 *  \param value    An array of property values.
 *  \return         1 if property has been changed, 0 otherwise. */
int mapper_device_set_property(mapper_device dev, const char *name,
                               int length, char type, const void *value);

/*! Push any property changes out to the network.
 *  \param dev      The device to operate on. */
void mapper_device_push(mapper_device dev);

/*! Remove a property of a device.
 *  \param dev      The device to operate on.
 *  \param name     The name of the property to remove.
 *  \return         1 if property has been removed, 0 otherwise. */
int mapper_device_remove_property(mapper_device dev, const char *name);

/*! Poll this device for new messages.
 *  Note, if you have multiple devices, the right thing to do is call
 *  this function for each of them with block_ms=0, and add your own
 *  sleep if necessary.
 *  \param dev      The device to check messages for.
 *  \param block_ms Number of milliseconds to block waiting for
 *                  messages, or 0 for non-blocking behaviour.
 *  \return         The number of handled messages. May be zero if there was
 *                  nothing to do. */
int mapper_device_poll(mapper_device dev, int block_ms);

/*! Return the number of file descriptors needed for this device.
 *  This can be used to allocated an appropriately-sized list for
 *  called to mapper_device_fds.  Note that the number of descriptors
 *  needed can change throughout the life of a device, therefore this
 *  function should be called whenever the list of file descriptors is
 *  needed.
 *  \param dev  The device to count file descriptors for.
 *  \return     The number of file descriptors needed for the indicated
 *              device. */
int mapper_device_num_fds(mapper_device dev);

/*! Write the list of file descriptors for this device to the provided
 *  array.  Up to num file descriptors will be written.  These file
 *  descriptors can be used as input for the read array of select or
 *  poll, for example.
 *  \param dev  The device to get file descriptors for.
 *  \param fds  Memory to receive file descriptors.
 *  \param num  The number of file descriptors pointed to by fds.
 *  \return     The number of file descriptors actually written to fds. */
int mapper_device_fds(mapper_device dev, int *fds, int num);

/*! If an external event indicates that a file descriptor for this
 *  device needs servicing, this function should be called.
 *  \param dev  The device that needs servicing.
 *  \param fd   The file descriptor that needs servicing. */
void mapper_device_service_fd(mapper_device dev, int fd);

/*! Detect whether a device is completely initialized.
 *  \param dev  The device to query.
 *  \return     Non-zero if device is completely initialized, i.e., has an
 *              allocated receiving port and unique network name.  Zero
 *              otherwise. */
int mapper_device_ready(mapper_device dev);

/*! Recover a timetag specifying the last time a device made contact.  If the
 *  device is local this will be the current time.
 *  \param dev  The device whose time we are asking for.
 *  \param tt   A previously allocated timetag to initialize. */
void mapper_device_synced(mapper_device dev, mapper_timetag_t *tt);

/*! Return the current version number for a device.
 *  \param dev  The device to query.
 *  \return     The version number of the device. */
int mapper_device_version(mapper_device dev);

/*! Return a string indicating the device's full name, if it is registered.
 *  The returned string must not be externally modified.
 *  \param dev  The device to query.
 *  \return     String containing the device's full name, or zero if it is
 *              not available. */
const char *mapper_device_name(mapper_device dev);

/*! Return a string indicating the device's host, if it is registered.
 *  The returned string must not be externally modified.
 *  \param dev  The device to query.
 *  \return     String containing the device's host, or zero if it is
 *              not available. */
const char *mapper_device_host(mapper_device dev);

/*! Return the unique id allocated to this device by the mapper network.
 *  \param dev  The device to query.
 *  \return     The device's id, or zero if it is not available. */
mapper_id mapper_device_id(mapper_device dev);

/*! Indicate whether this device is local.
 *  \param dev  The device to query.
 *  \return     1 if the device is local, 0 otherwise. */
int mapper_device_is_local(mapper_device dev);

/*! Return the port used by a device to receive signals, if available.
 *  \param dev  The device to query.
 *  \return     An integer indicating the device's port, or zero if it is
 *              not available. */
unsigned int mapper_device_port(mapper_device dev);

/*! Return an allocated ordinal which is appended to this device's
 *  network name.  In general the results of this function are not
 *  valid unless mapper_device_ready() returns non-zero.
 *  \param dev  The device to query.
 *  \return     A positive ordinal unique to this device (per name). */
unsigned int mapper_device_ordinal(mapper_device dev);

/*! Start a time-tagged mapper queue.
 *  \param dev  The device to use.
 *  \param tt   A timetag to use for the updates bundled by this queue. */
void mapper_device_start_queue(mapper_device dev, mapper_timetag_t tt);

/*! Dispatch a time-tagged mapper queue.
 *  \param dev  The device to use.
 *  \param tt   The timetag for an existing queue created with
 *              mapper_device_start_queue(). */
void mapper_device_send_queue(mapper_device dev, mapper_timetag_t tt);

/*! Get access to the device's underlying lo_server. */
lo_server mapper_device_lo_server(mapper_device dev);

mapper_database mapper_device_database(mapper_device dev);

/*! Get the union of two device queries (devices matching query1 OR query2).
 *  \param query1   The first device query.
 *  \param query2   The second device query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_device_query_next() to iterate. */
mapper_device *mapper_device_query_union(mapper_device *query1,
                                         mapper_device *query2);

/*! Get the intersection of two device queries (devices matching query1 AND
 *  query2).
 *  \param query1   The first device query.
 *  \param query2   The second device query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_device_query_next() to iterate. */
mapper_device *mapper_device_query_intersection(mapper_device *query1,
                                                mapper_device *query2);

/*! Get the difference between two device queries (devices matching query1 but
 *  NOT query2).
 *  \param query1   The first device query.
 *  \param query2   The second device query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_device_query_next() to iterate. */
mapper_device *mapper_device_query_difference(mapper_device *query1,
                                              mapper_device *query2);

/*! Given a device record pointer returned from a previous device query, get an
 *  indexed item in the list.
 *  \param query    The previous device record pointer.
 *  \param index    The index of the list element to retrieve.
 *  \return         A pointer to the device record, or zero if it doesn't
 *                  exist. */
mapper_device mapper_device_query_index(mapper_device *query, int index);

/*! Given a device record pointer returned from a previous device query, get the
 *  next item in the list.
 *  \param query    The previous device record pointer.
 *  \return         A double-pointer to the next device record in the list, or
 *                  zero if no more devices. */
mapper_device *mapper_device_query_next(mapper_device *query);

/*! Copy a previously-constructed device query.
 *  \param query    The previous device record pointer.
 *  \return         A double-pointer to the copy of the list, or zero if none.
 *                  Use mapper_device_query_next() to iterate. */
mapper_device *mapper_device_query_copy(mapper_device *query);

/*! Given a device record pointer returned from a previous device query,
 *  indicate that we are done iterating.
 *  \param query    The previous device record pointer. */
void mapper_device_query_done(mapper_device *query);

/*! Helper to print the properties of a specific device.
 *  \param dev      The device to print. */
void mapper_device_print(mapper_device dev);

/* @} */

/*** Networking ***/

/*! @defgroup networks Networks

    @{ Networks handle the traffic on the multicast bus.  In general, you do not
       need to worry about this interface, as a network structure will be
       created automatically when allocating a device.  A network structure only
       needs to be explicitly created if you plan to override default settings
       for the multicast bus.  */

/*! Create a network with custom parameters.  Creating a network object
 *  manually is only required if you wish to specify custom network
 *  parameters.  Creating a device or admin without specifying an
 *  network will give you an object working on the "standard"
 *  configuration.
 * \param interface If non-zero, a string identifying a preferred network
 *                  interface.  This string can be enumerated e.g. using
 *                  if_nameindex(). If zero, an interface will be
 *                  selected automatically.
 * \param group     If non-zero, specify a multicast group address to use.
 *                  Zero indicates that the standard group 224.0.1.3 should
 *                  be used.
 * \param port      If non-zero, specify a multicast port to use.  Zero
 *                  indicates that the standard port 7570 should be used.
 * \return          A newly allocated network structure.  Should be freed using
 *                  mapper_network_free() */
mapper_network mapper_network_new(const char *interface, const char *group,
                                  int port);

/*! Free a network structure created with mapper_network_new().
 *  \param net      The network structure to free. */
void mapper_network_free(mapper_network net);

/*! Return the mapper_database structure used for tracking the network.
 *  \param net      The network structure to query.
 *  \return         The mapper_database used by this network structure. */
mapper_database mapper_network_database(mapper_network net);

/*! Return a string indicating the name of the network interface in use.
 *  \param net      The network structure to query.
 *  \return         A string containing the name of the network interface. */
const char *mapper_network_interface(mapper_network net);

/*! Return the IPv4 address used by a device to receive signals, if available.
 *  \param net      The network structure to query.
 *  \return         A pointer to an in_addr struct indicating the network's IP
 *                  address, or zero if it is not available.  In general this
 *                  will be the IPv4 address associated with the selected
 *                  local network interface. */
const struct in_addr *mapper_network_ip4(mapper_network net);

/*! Retrieve the name of the multicast group currently in use.
 *  \param net      The network structure to query. */
const char *mapper_network_group(mapper_network net);

/*! Retrieve the name of the multicast port currently in use.
 *  \param net      The network structure to query. */
int mapper_network_port(mapper_network net);

/*! Interface to send an arbitrary OSC message to the administrative bus.
 *  \param net      The networking structure to use for sending the message.
 *  \param path     The path for the OSC message.
 *  \param types    A string specifying the types of subsequent arguments.
 *  \param ...      A list of arguments. */
void mapper_network_send_message(mapper_network net, const char *path,
                                 const char *types, ...);

/* @} */

/***** Maps *****/

/*! @defgroup maps Maps

    @{ Maps define dataflow connections between sets of signals. A map consists
       of one or more source slots, one destination slot, and properties which
       determine how the source data is processed. */

/*! Create a map between a set of signals.
 *  \param num_sources  The number of source signals in this map.
 *  \param sources      Array of source signal data structures.
 *  \param destination  Destination signal data structure.
 *  \return             A map data structure – either loaded from the database
 *                      (if the map already existed) or newly created. In the
 *                      latter case the map will not take effect until it has
 *                      been added to the network using mapper_map_push(). */
mapper_map mapper_map_new(int num_sources, mapper_signal *sources,
                          mapper_signal destination);

/*! Associate a map with an arbitrary pointer.
 *  \param map          The map to operate on.
 *  \param user_data    A pointer to user data to be associated. */
void mapper_map_set_user_data(mapper_map map, const void *user_data);

/*! Retrieve the arbitrary pointer associated with a map.
 *  \param map          The map to operate on.
 *  \return             A pointer associated with this map. */
void *mapper_map_user_data(mapper_map map);

/*! Remove a map between a set of signals.
 *  \param map      The map to destroy. */
void mapper_map_release(mapper_map map);

/*! Get the description for a specific signal.
 *  \param map      The map to check.
 *  \return         The map description. */
const char *mapper_map_description(mapper_map map);

/*! Get the number of sources for to a specific map.
 *  \param map      The map to check.
 *  \return         The number of sources. */
int mapper_map_num_sources(mapper_map map);

/*! Get a source slot for a specific map.
 *  \param map      The map to check.
 *  \param index    The source slot index.
 *  \return         The indexed source slot, or NULL if not available. */
mapper_slot mapper_map_source_slot(mapper_map map, int index);

/*! Get the destination slot for a specific map.
 *  \param map      The map to check.
 *  \return         The destination slot. */
mapper_slot mapper_map_destination_slot(mapper_map map);

/*! Get the map slot matching a specific signal.
 *  \param map      The map to check.
 *  \param sig      The signal corresponding to the desired slot.
 *  \return         The slot, or NULL if not found. */
mapper_slot mapper_map_slot_by_signal(mapper_map map, mapper_signal sig);

/*! Get the unique id for a specific map.
 *  \param map      The map to check.
 *  \return         The unique id assigned to this map. */
mapper_id mapper_map_id(mapper_map map);

/*! Get the mode property for a specific map.
 *  \param map      The map to check.
 *  \return         The mode parameter for this map. */
mapper_mode mapper_map_mode(mapper_map map);

/*! Get the expression property for a specific map.
 *  \param map      The map to check.
 *  \return         The expression evaulated by this map. */
const char *mapper_map_expression(mapper_map map);

/*! Get the muted property for a specific map.
 *  \param map      The map to check.
 *  \return         One if this map is muted, 0 otherwise. */
int mapper_map_muted(mapper_map map);

/*! Get the process location for a specific map.
 *  \param map      The map to check.
 *  \return         MAPPER_LOC_SOURCE if processing is evaluated at the source
 *                  device, MAPPER_LOC_DESTINATION otherwise. */
mapper_location mapper_map_process_location(mapper_map map);

/*! Get the scopes property for a specific map.
 *  \param map      The map to check.
 *  \return         A double-pointer to the first item in the list of results
 *                  or zero if none.  Use mapper_map_query_next() to iterate. */
mapper_device *mapper_map_scopes(mapper_map map);

/*! Detect whether a map is completely initialized.
 *  \param map  The device to query.
 *  \return     Non-zero if map is completely initialized, zero otherwise. */
int mapper_map_ready(mapper_map map);

/*! Set the description property for a specific map. Changes to remote maps will
 *  not take effect until synchronized with the network using mapper_map_push().
 *  \param map          The map to modify.
 *  \param description  The description value to set. */
void mapper_map_set_description(mapper_map map, const char *description);

/*! Set the mode property for a specific map. Changes to remote maps will not
 *  take effect until synchronized with the network using mapper_map_push().
 *  \param map      The map to modify.
 *  \param mode     The mode value to set, can be one of MAPPER_MODE_EXPRESSION,
 *                  MAPPER_MODE_LINEAR, or MAPPER_MODE_RAW. */
void mapper_map_set_mode(mapper_map map, mapper_mode mode);

/*! Set the expression property for a specific map. Changes to remote maps will not
 *  take effect until synchronized with the network using mapper_map_push().
 *  \param map          The map to modify.
 *  \param expression   A string specifying an expression to be evaluated by
 *                      the map. */
void mapper_map_set_expression(mapper_map map, const char *expression);

/*! Set the muted property for a specific map. Changes to remote maps will not
 *  take effect until synchronized with the network using mapper_map_push().
 *  \param map      The map to modify.
 *  \param muted    1 to mute this map, or 0 unmute. */
void mapper_map_set_muted(mapper_map map, int muted);

/*! Set the process location property for a specific map. Depending on the map
 *  topology and expression specified it may not be possible to set the process
 *  location to MAPPER_LOC_SOURCE for all maps. Changes to remote maps will not
 *  take effect until synchronized with the network using mapper_map_push().
 *  \param map      The map to modify.
 *  \param location MAPPER_LOC_SOURCE to indicate processing should be handled
 *                  by the source device, MAPPER_LOC_DESTINATION for the
 *                  destination. */
void mapper_map_set_process_location(mapper_map map, mapper_location location);

/*! Set an arbitrary property for a specific map.  Changes to remote maps will
 *  not take effect until synchronized with the network using mapper_map_push().
 *  \param map      The map to modify.
 *  \param name     The name of the property to add.
 *  \param length   The length of value array.
 *  \param type     The property  datatype.
 *  \param value    An array of property values.
 *  \return         1 if property has been changed, 0 otherwise. */
int mapper_map_set_property(mapper_map map, const char *name, int length,
                            char type, const void *value);

/*! Push any property changes out to the network.
 *  \param map      The map to operate on. */
void mapper_map_push(mapper_map map);

/*! Remove a property of a map.
 *  \param map      The map to operate on.
 *  \param name     The name of the property to remove.
 *  \return         1 if property has been removed, 0 otherwise. */
int mapper_map_remove_property(mapper_map map, const char *name);

/*! Add a scope to this map. Map scopes configure the propagation of signal
 *  instance updates across the map. Changes to remote maps will not take effect
 *  until synchronized with the network using mapper_map_push().
 *  \param map      The map to modify.
 *  \param dev      Device to add as a scope for this map. After taking effect,
 *                  this setting will cause instance updates originating at this
 *                  device to be propagated across the map. */
void mapper_map_add_scope(mapper_map map, mapper_device dev);

/*! Remove a scope from this map. Map scopes configure the propagation of signal
 *  instance updates across the map. Changes to remote maps will not take effect
 *  until synchronized with the network using mapper_map_push().
 *  \param map      The map to modify.
 *  \param dev      Device to remove as a scope for this map. After taking effect,
 *                  this setting will cause instance updates originating at this
 *                  device to be blocked from propagating across the map. */
void mapper_map_remove_scope(mapper_map map, mapper_device dev);

/*! Get the total number of properties for a specific map.
 *  \param map      The map to check.
 *  \return         The number of properties. */
int mapper_map_num_properties(mapper_map map);

/*! Look up a map property by name.
 *  \param map      The map to check.
 *  \param name     The name of the property to retrieve.
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \return         Zero if found, otherwise non-zero. */
int mapper_map_property(mapper_map map, const char *name, int *length,
                        char *type, const void **value);

/*! Look up a map property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param map      The map to check.
 *  \param index    Numerical index of a map property.
 *  \param name     Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \return Zero if found, otherwise non-zero. */
int mapper_map_property_index(mapper_map map, unsigned int index,
                              const char **name, int *length, char *type,
                              const void **value);

/*! Get the union of two map queries (maps matching query1 OR query2).
 *  \param query1   The first map query.
 *  \param query2   The second map query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_map_query_next() to iterate. */
mapper_map *mapper_map_query_union(mapper_map *query1, mapper_map *query2);

/*! Get the intersection of two map queries (maps matching query1 AND query2).
 *  \param query1   The first map query.
 *  \param query2   The second map query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_map_query_next() to iterate. */
mapper_map *mapper_map_query_intersection(mapper_map *query1, mapper_map *query2);

/*! Get the difference between two map queries (maps matching query1 but NOT
 *  query2).
 *  \param query1   The first map query.
 *  \param query2   The second map query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_map_query_next() to iterate. */
mapper_map *mapper_map_query_difference(mapper_map *query1, mapper_map *query2);

/*! Given a map record pointer returned from a previous map query, get an
 *  indexed item in the list.
 *  \param query    The previous map record pointer.
 *  \param index    The index of the list element to retrieve.
 *  \return         A pointer to the map record, or zero if it doesn't exist. */
mapper_map mapper_map_query_index(mapper_map *query, int index);

/*! Given a map record pointer returned from a previous map query, get the next
 *  item in the list.
 *  \param query    The previous map record pointer.
 *  \return         A double-pointer to the next map record in the list, or
 *                  zero if no more maps. */
mapper_map *mapper_map_query_next(mapper_map *query);

/*! Copy a previously-constructed map query.
 *  \param query    The previous map record pointer.
 *  \return         A double-pointer to the copy of the list, or zero if none.
 *                  Use mapper_map_query_next() to iterate. */
mapper_map *mapper_map_query_copy(mapper_map *query);

/*! Given a map record pointer returned from a previous map*() query, indicate
 *  that we are done iterating.
 *  \param query    The previous map record pointer. */
void mapper_map_query_done(mapper_map *query);

/*! Helper to print the properties of a specific map.
 *  \param map      The map to print. */
void mapper_map_print(mapper_map map);

/* @} */

/***** Slots *****/

/*! @defgroup slots Slots

    @{ Slots define the endpoints of a map.  Each slot links to a signal
       structure and handles properties of the map that are specific to an
       endpoint such as range extrema. */

/*! Get the boundary maximum property for a specific map slot. This property
 *  controls behaviour when a value exceeds the specified slot maximum value.
 *  \param slot     The slot to check.
 *  \return         The boundary maximum setting. */
mapper_boundary_action mapper_slot_bound_max(mapper_slot slot);

/*! Get the boundary minimum property for a specific map slot. This property
 *  controls behaviour when a value is less than the specified slot minimum value.
 *  \param slot     The slot to check.
 *  \return         The boundary minimum setting. */
mapper_boundary_action mapper_slot_bound_min(mapper_slot slot);

/*! Get the calibrating property for a specific map slot. When enabled, the
 *  slot minimum and maximum values will be updated based on processed data.
 *  \param slot     The slot to check.
 *  \return         One if calibration is enabled, 0 otherwise. */
int mapper_slot_calibrating(mapper_slot slot);

/*! Get the "causes update" property for a specific map slot. When enabled,
 *  updates to this slot will cause computation of a new map output.
 *  \param slot     The slot to check.
 *  \return         One if causes map update, 0 otherwise. */
int mapper_slot_causes_update(mapper_slot slot);

/*! Get the "maximum" property for a specific map slot.
 *  \param slot     The slot to check.
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.). */
void mapper_slot_maximum(mapper_slot slot, int *length, char *type, void **value);

/*! Get the "minimum" property for a specific map slot.
 *  \param slot     The slot to check.
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.). */
void mapper_slot_minimum(mapper_slot slot, int *length, char *type, void **value);

/*! Get the total number of properties for a specific slot.
 *  \param slot     The slot to check.
 *  \return         The number of properties. */
int mapper_slot_num_properties(mapper_slot slot);

/*! Look up a map property by name.
 *  \param slot     The map slot to check.
 *  \param name     The name of the property to retrieve.
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \return         Zero if found, otherwise non-zero. */
int mapper_slot_property(mapper_slot slot, const char *name, int *length,
                         char *type, const void **value);

/*! Look up a map slot property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param slot     The map slot to check.
 *  \param index    Numerical index of a map slot property.
 *  \param name     Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \return         Zero if found, otherwise non-zero. */
int mapper_slot_property_index(mapper_slot slot, unsigned int index,
                               const char **name, int *length, char *type,
                               const void **value);

/*! Get the "send as instance" property for a specific map slot. If enabled,
 *  updates to this slot will be treated as updates to a specific instance.
 *  \param slot     The slot to check.
 *  \return         One to send as instance, 0 otherwise. */
int mapper_slot_use_as_instance(mapper_slot slot);

/*! Get the index for a specific map slot.
 *  \param slot     The slot to check.
 *  \return         The index of the slot in its parent map. */
int mapper_slot_index(mapper_slot slot);

/*! Get the parent signal for a specific map slot.
 *  \param slot     The slot to check.
 *  \return         The slot's parent signal. */
mapper_signal mapper_slot_signal(mapper_slot slot);

/*! Set the boundary maximum property for a specific map slot. This property
 *  controls behaviour when a value exceeds the specified slot maximum value.
 *  Changes to remote maps will not take effect until synchronized with the
 *  network using mapper_map_push().
 *  \param slot     The slot to modify.
 *  \param action   The boundary maximum setting. */
void mapper_slot_set_bound_max(mapper_slot slot, mapper_boundary_action action);

/*! Set the boundary minimum property for a specific map slot. This property
 *  controls behaviour when a value is less than the slot minimum value.
 *  Changes to remote maps will not take effect until synchronized with the
 *  network using mapper_map_push().
 *  \param slot     The slot to modify.
 *  \param action   The boundary minimum setting. */
void mapper_slot_set_bound_min(mapper_slot slot, mapper_boundary_action action);

/*! Set the calibrating property for a specific map slot. When enabled, the
 *  slot minimum and maximum values will be updated based on processed data.
 *  Changes to remote maps will not take effect until synchronized with the
 *  network using mapper_map_push().
 *  \param slot         The slot to modify.
 *  \param calibrating  One to enable calibration, 0 otherwise. */
void mapper_slot_set_calibrating(mapper_slot slot, int calibrating);

/*! Set the "causes update" property for a specific map slot. When enabled,
 *  updates to this slot will cause computation of a new map output.
 *  Changes to remote maps will not take effect until synchronized with the
 *  network using mapper_map_push().
 *  \param slot             The slot to modify.
 *  \param causes_update    One to enable "causes update", 0 otherwise. */
void mapper_slot_set_causes_update(mapper_slot slot, int causes_update);

/*! Set the "maximum" property for a specific map slot.  Changes to remote maps
 *  will not take effect until synchronized with the network using
 *  mapper_map_push().
 *  \param slot     The slot to modify.
 *  \param type     The data type of the update.
 *  \param value    An array of values.
 *  \param length   Length of the update array. */
void mapper_slot_set_maximum(mapper_slot slot, int length, char type,
                             const void *value);

/*! Set the "minimum" property for a specific map slot.  Changes to remote maps
 *  will not take effect until synchronized with the network using
 *  mapper_map_push().
 *  \param slot     The slot to modify.
 *  \param type     The data type of the update.
 *  \param value    An array of values.
 *  \param length   Length of the update array. */
void mapper_slot_set_minimum(mapper_slot slot, int length, char type,
                             const void *value);

/*! Set the "use as instance" property for a specific map slot.  If enabled,
 *  updates to this slot will be treated as updates to a specific instance.
 *  Changes to remote maps will not take effect until synchronized with the
 *  network using mapper_map_push().
 *  \param slot             The slot to modify.
 *  \param use_as_instance  One to send as instance update, 0 otherwise. */
void mapper_slot_set_use_as_instance(mapper_slot slot, int use_as_instance);

/*! Set an arbitrary property for a specific map slot.  Changes to remote maps
 *  will not take effect until synchronized with the network using
 *  mapper_map_push().
 *  \param slot     The slot to modify.
 *  \param name     The name of the property to add.
 *  \param type     The property  datatype.
 *  \param value    An array of property values.
 *  \param length   The length of value array.
 *  \return         1 if property has been changed, 0 otherwise. */
int mapper_slot_set_property(mapper_slot slot, const char *name, int length,
                             char type, const void *value);

/*! Remove a property of a map slot.
 *  \param slot     The slot to operate on.
 *  \param name     The name of the property to remove.
 *  \return         1 if property has been removed, 0 otherwise. */
int mapper_slot_remove_property(mapper_slot slot, const char *name);

/*! Helper to print the properties of a specific slot.
 *  \param slot     The slot to print. */
void mapper_slot_print(mapper_slot slot);

/* @} */

/***** Database *****/

/*! @defgroup database Databases

    @{ Databases are the primary interface through which a program may observe
       the network and store information about devices and signals that are
       present.  Each Database has a database of devices, signals, and maps,
       which can be queried. */

/*! Create a peer in the libmapper distributed database.
 *  \param net                  A previously allocated network structure to use.
 *                              If 0, one will be allocated for use with this
 *                              database.
 *  \param autosubscribe_flags  Sets whether the database should automatically
 *                              subscribe to information about signals
 *                              and maps when it encounters a previously-unseen
 *                              device.
 *  \return                     The new database. */
mapper_database mapper_database_new(mapper_network net, int autosubscribe_flags);

/*! Update a database.
 *  \param db       The database to update.
 *  \param block_ms The number of milliseconds to block, or 0 for
 *                  non-blocking behaviour.
 *  \return         The number of handled messages. */
int mapper_database_poll(mapper_database db, int block_ms);

/*! Free a database.
 *  \param db       The database to free. */
void mapper_database_free(mapper_database db);

/*! Subscribe to information about a specific device.
 *  \param db       The database to use.
 *  \param dev      The device of interest. If NULL the admin will
 *                  automatically subscribe to all discovered devices.
 *  \param flags    Bitflags setting the type of information of interest.  Can
 *                  be a combination of SUB_DEVICE, SUB_DEVICE_INPUTS,
 *                  SUB_DEVICE_OUTPUTS, SUB_DEVICE_SIGNALS, SUB_DEVICE_MAPS_IN,
 *                  SUB_DEVICE_MAPS_OUT, SUB_DEVICE_MAPS, or simply
 *                  SUB_DEVICE_ALL for all information.
 *  \param timeout  The length in seconds for this subscription. If set to -1,
 *                  the admin will automatically renew the subscription until
 *                  it is freed or this function is called again. */
void mapper_database_subscribe(mapper_database db, mapper_device dev, int flags,
                               int timeout);

/*! Unsubscribe from information about a specific device.
 *  \param db       The database to use.
 *  \param dev      The device of interest. If NULL the admin will unsubscribe
 *                  from all devices. */
void mapper_database_unsubscribe(mapper_database db, mapper_device dev);

/*! Set the timeout in seconds after which a database will declare a device
 *  "unresponsive". Defaults to MAPPER_TIMEOUT_SEC.
 *  \param db       The database to use.
 *  \param timeout  The timeout in seconds. */
void mapper_database_set_timeout(mapper_database db, int timeout);

/*! Get the timeout in seconds after which a database will declare a device
 *  "unresponsive". Defaults to MAPPER_TIMEOUT_SEC.
 *  \param db       The database to use.
 *  \return         The current timeout in seconds. */
int mapper_database_timeout(mapper_database db);

/*! Retrieve the networking structure from a database.
 *  \param db       The database to use.
 *  \return         The database network data structure. */
mapper_network mapper_database_network(mapper_database db);

/*! Remove unresponsive devices from the database.
 *  \param db       The database to flush.
 *  \param timeout  The number of seconds a device must have been unresponsive
 *                  before removal.
 *  \param quiet    1 to disable callbacks during flush, 0 otherwise. */
void mapper_database_flush(mapper_database db, int timeout, int quiet);

/*! Send a request to the network for all active devices to report in.
 *  \param          The database to use. */
void mapper_database_request_devices(mapper_database db);

/*! A callback function prototype for when a device record is added or updated.
 *  Such a function is passed in to mapper_database_add_device_callback().
 *  \param dev      The device record.
 *  \param action   A value of mapper_record_action indicating what is
 *                  happening to the device record.
 *  \param user     The user context pointer registered with this callback. */
typedef void mapper_database_device_handler(mapper_device dev,
                                            mapper_record_action action,
                                            const void *user);

/*! Register a callback for when a device record is added or updated
 *  in the database.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     A user-defined pointer to be passed to the callback
 *                  for context . */
void mapper_database_add_device_callback(mapper_database db,
                                         mapper_database_device_handler *h,
                                         const void *user);

/*! Remove a device record callback from the database service.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     The user context pointer that was originally specified
 *                  when adding the callback. */
void mapper_database_remove_device_callback(mapper_database db,
                                            mapper_database_device_handler *h,
                                            const void *user);

/*! Return the number of devices stored in the database.
 *  \param db       The database to query.
 *  \return         The number of devices. */
int mapper_database_num_devices(mapper_database db);

/*! Return the whole list of devices.
 *  \param db       The database to query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_device_query_next() to iterate. */
mapper_device *mapper_database_devices(mapper_database db);

/*! Return the list of local devices.
 *  \param db       The database to query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_device_query_next() to iterate. */
mapper_device *mapper_database_local_devices(mapper_database db);

/*! Find information for a registered device.
 *  \param db       The database to query.
 *  \param name     Name of the device to find in the database.
 *  \return         Information about the device, or zero if not found. */
mapper_device mapper_database_device_by_name(mapper_database db,
                                             const char *name);

/*! Look up information for a registered device using its unique id.
 *  \param db       The database to query.
 *  \param id       Unique id identifying the device to find in the database.
 *  \return         Information about the device, or zero if not found. */
mapper_device mapper_database_device_by_id(mapper_database db, mapper_id id);

/*! Return the list of devices with a substring in their name.
 *  \param db       The database to query.
 *  \param pattern  The substring to search for.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_device_query_next() to iterate. */
mapper_device *mapper_database_devices_by_name_match(mapper_database db,
                                                     const char *pattern);

/*! Return the list of devices matching the given property.
 *  \param db       The database to query.
 *  \param name     The name of the property to search for.
 *  \param length   The value length.
 *  \param type     The value type.
 *  \param value    The value.
 *  \param op       The comparison operator.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_device_query_next() to iterate. */
mapper_device *mapper_database_devices_by_property(mapper_database db,
                                                   const char *name, int length,
                                                   char type, const void *value,
                                                   mapper_op op);

/*! A callback function prototype for when a signal record is added or updated.
 *  Such a function is passed in to mapper_database_add_signal_callback().
 *  \param sig      The signal record.
 *  \param action   A value of mapper_record_action indicating what is
 *                  happening to the signal record.
 *  \param user     The user context pointer registered with this callback. */
typedef void mapper_database_signal_handler(mapper_signal sig,
                                            mapper_record_action action,
                                            const void *user);

/*! Register a callback for when a signal record is added or updated
 *  in the database.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     A user-defined pointer to be passed to the callback
 *                  for context . */
void mapper_database_add_signal_callback(mapper_database db,
                                         mapper_database_signal_handler *h,
                                         const void *user);

/*! Remove a signal record callback from the database service.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     The user context pointer that was originally specified
 *                  when adding the callback. */
void mapper_database_remove_signal_callback(mapper_database db,
                                            mapper_database_signal_handler *h,
                                            const void *user);

/*! Find information for a registered signal.
 *  \param db       The database to query.
 *  \param id       Unique id of the signal to find in the database.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_database_signal_by_id(mapper_database db, mapper_id id);

/*! Return the number of signals stored in the database.
 *  \param db       The database to query.
 *  \return         The number of signals. */
int mapper_database_num_signals(mapper_database db);

/*! Return the list of all known signals across all devices.
 *  \param db       The database to query.
 *  \param dir      The direction of the signals to return. Can be
 *                  MAPPER_INCOMING, MAPPER_OUTGOING, or MAPPER_DIR_ANY.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_signal_query_next() to iterate. */
mapper_signal *mapper_database_signals(mapper_database db, mapper_direction dir);

/*! Find information for registered signals.
 *  \param db       The database to query.
 *  \param sig_name Name of the signal to find in the database.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_signal_query_next() to iterate. */
mapper_signal *mapper_database_signals_by_name(mapper_database db,
                                               const char *sig_name);

/*! Find information for registered signals.
 *  \param db       The database to query.
 *  \param pattern  The substring to search for.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_signal_query_next() to iterate. */
mapper_signal *mapper_database_signals_by_name_match(mapper_database db,
                                                     const char *pattern);

/*! Return the list of signals matching the given property.
 *  \param db       The database to query.
 *  \param name     The name of the property to search for.
 *  \param length   The value length.
 *  \param type     The value type.
 *  \param value    The value.
 *  \param op       The comparison operator.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_signal_query_next() to iterate. */
mapper_signal *mapper_database_signals_by_property(mapper_database db,
                                                   const char *name, int length,
                                                   char type, const void *value,
                                                   mapper_op op);

/*! A callback function prototype for when a map record is added or updated in
 *  the database. Such a function is passed in to
 *  mapper_database_add_map_callback().
 *  \param map      The map record.
 *  \param action   A value of mapper_record_action indicating what is
 *                  happening to the map record.
 *  \param user     The user context pointer registered with this callback. */
typedef void mapper_map_handler(mapper_map map, mapper_record_action action,
                                const void *user);

/*! Register a callback for when a map record is added or updated.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     A user-defined pointer to be passed to the callback
 *                  for context . */
void mapper_database_add_map_callback(mapper_database db, mapper_map_handler *h,
                                      const void *user);

/*! Remove a map record callback from the database service.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     The user context pointer that was originally specified
 *                  when adding the callback. */
void mapper_database_remove_map_callback(mapper_database db,
                                         mapper_map_handler *h,
                                         const void *user);

/*! Return the number of maps stored in the database.
 *  \param db       The database to query.
 *  \return         The number of maps. */
int mapper_database_num_maps(mapper_database db);

/*! Return a list of all registered maps.
 *  \param db       The database to query.
 *  \return         A double-pointer to the first item in the list of results,
 *                  or zero if none.  Use mapper_map_query_next() to iterate. */
mapper_map *mapper_database_maps(mapper_database db);

/*! Return the map that match the given map id.
 *  \param db       The database to query.
 *  \param id       Unique id identifying the map.
 *  \return         A pointer to a structure containing information on the
 *                  found map, or 0 if not found. */
mapper_map mapper_database_map_by_id(mapper_database db, mapper_id id);

/*! Return the list of maps that use the given scope.
 *  \param db       The database to query.
 *  \param dev      The device owning the scope to query.
 *  \return         A souble-pointer to the first item in a list of results.
 *                  Use mapper_map_query_next() to iterate. */
mapper_map *mapper_database_maps_by_scope(mapper_database db, mapper_device dev);

/*! Return the list of maps matching the given property.
 *  \param db       The database to query.
 *  \param name     The name of the property to search for.
 *  \param length   The value length.
 *  \param type     The value type.
 *  \param value    The value.
 *  \param op       The comparison operator.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_map_query_next() to iterate. */
mapper_map *mapper_database_maps_by_property(mapper_database db,
                                             const char *name, int length,
                                             char type, const void *value,
                                             mapper_op op);

/*! Return the list of maps matching the given slot property.
 *  \param db       The database to query.
 *  \param name     The name of the property to search for.
 *  \param length   The value length.
 *  \param type     The value type.
 *  \param value    The value.
 *  \param op       The comparison operator.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_map_query_next() to iterate. */
mapper_map *mapper_database_maps_by_slot_property(mapper_database db,
                                                  const char *name, int length,
                                                  char type, const void *value,
                                                  mapper_op op);

/* @} */

/***** Time *****/

/*! @defgroup time Time

 @{ libmapper primarily uses NTP timetags for communication and
    synchronization. */

/*! Initialize a timetag to the current mapping network time.
 *  \param dev  The device whose time we are asking for.
 *  \param tt   A previously allocated timetag to initialize. */
void mapper_device_now(mapper_device dev, mapper_timetag_t *tt);

/*! Initialize a timetag to the current mapping network time.
 *  \param net  The network whose time we are asking for.
 *  \param tt   A previously allocated timetag to initialize. */
void mapper_network_now(mapper_network net, mapper_timetag_t *tt);

/*! Return the difference in seconds between two mapper_timetags.
 *  \param minuend      The minuend.
 *  \param subtrahend   The subtrahend.
 *  \return             The difference a-b in seconds. */
double mapper_timetag_difference(mapper_timetag_t minuend,
                                 mapper_timetag_t subtrahend);

/*! Add a timetag to another given timetag.
 *  \param augend       A previously allocated timetag to augment.
 *  \param addend       A timetag to add to add. */
void mapper_timetag_add(mapper_timetag_t *augend, mapper_timetag_t addend);

/*! Subtract a timetag from another given timetag.
 *  \param minuend      A previously allocated timetag to augment.
 *  \param subtrahend   A timetag to add to subtract. */
void mapper_timetag_subtract(mapper_timetag_t *minuend,
                             mapper_timetag_t subtrahend);

/*! Add seconds to a given timetag.
 *  \param augend       A previously allocated timetag to augment.
 *  \param addend       An amount in seconds to add. */
void mapper_timetag_add_double(mapper_timetag_t *augend, double addend);

/*! Return value of mapper_timetag as a double-precision floating point value. */
double mapper_timetag_double(mapper_timetag_t tt);

/*! Set value of a mapper_timetag from an integer value. */
void mapper_timetag_set_int(mapper_timetag_t *tt, int value);

/*! Set value of a mapper_timetag from a floating point value. */
void mapper_timetag_set_float(mapper_timetag_t *tt, float value);

/*! Set value of a mapper_timetag from a double-precision floating point value. */
void mapper_timetag_set_double(mapper_timetag_t *tt, double value);

/*! Copy value of a mapper_timetag. */
void mapper_timetag_copy(mapper_timetag_t *ttl, mapper_timetag_t ttr);

/* @} */

/*! Get the version of libmapper */
const char *mapper_libversion();

/*! Helper for printing typed values.
 *  \param length   The vector length of the value.
 *  \param type     The value type.
 *  \param value    A pointer to the property value to print. */
void mapper_property_print(int length, char type, const void *value);

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_H__
