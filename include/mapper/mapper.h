#ifndef __MAPPER_H__
#define __MAPPER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <mapper/mapper_db.h>
#include <mapper/mapper_types.h>

#define MAPPER_NOW ((mapper_timetag_t){0L,1L})

//struct _mapper_signal;
//typedef struct _mapper_signal *mapper_signal;

/*! \mainpage libmapper

This is the API documentation for libmapper, a network-based signal
mapping framework.  Please see the Modules section for detailed
information, and be sure to consult the tutorial to get started with
libmapper concepts.

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

//struct _mapper_signal;
//typedef struct _mapper_signal *mapper_signal;

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
void mapper_signal_update(mapper_signal sig, void *value, int count,
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
void *mapper_signal_value(mapper_signal sig, mapper_timetag_t *tt);

/*! Query the values of any signals connected via mapping connections.
 *  \param sig      A local output signal. We will be querying the remote
 *                  ends of this signal's mapping connections.
 *  \param tt       A timetag to be attached to the outgoing query. Query
 *                  responses should also be tagged with this time.
 *  \return The number of queries sent, or -1 for error. */
int mapper_signal_query_remotes(mapper_signal sig, mapper_timetag_t tt);

/**** Instances ****/

/*! Add new instances to the reserve list. Note that if instance ids are specified,
 *  libmapper will not add multiple instances with the same id.
 *  \param sig          The signal to which the instances will be added.
 *  \param num          The number of instances to add.
 *  \param ids          Array of integer ids, one for each new instance,
 *                      or 0 for automatically-generated instance ids.
 *  \param user_data    Array of user context pointers, one for each new instance,
 *                      or 0 if not needed.
 *  \return             Number of instances added. */
int mapper_signal_reserve_instances(mapper_signal sig, int num, int *ids,
                                    void **user_data);

/*! Update the value of a specific signal instance.
 *  The signal will be routed according to external requests.
 *  \param sig      The signal to operate on.
 *  \param instance The instance to update.
 *  \param value    A pointer to a new value for this signal.  If the signal
 *                  type is 'i', this should be int*; if the signal type is 'f',
 *                  this should be float*.  It should be an array at least as
 *                  long as the signal's length property.
 *  \param count    The number of values being updated, or 0 for non-periodic
 *                  signals.
 *  \param tt       The time at which the value update was aquired. If NULL,
 *                  libmapper will tag the value update with the current time.
 *                  See mapper_device_start_queue() for more information on
 *                  bundling multiple signal updates with the same timetag. */
void mapper_signal_update_instance(mapper_signal sig, int instance, void *value,
                                   int count, mapper_timetag_t tt);

/*! Release a specific instance of a signal by removing it from the list
 *  of active instances and adding it to the reserve list.
 *  \param sig      The signal to operate on.
 *  \param instance The instance to suspend.
 *  \param tt       The time at which the instance was released; if NULL, will
 *                  be tagged with the current time. See
 *                  mapper_device_start_queue() for more information on bundling
 *                  multiple signal updates with the same timetag. */
void mapper_signal_release_instance(mapper_signal sig, int instance,
                                    mapper_timetag_t tt);

/*! Remove a specific instance of a signal and free its memory.
 *  \param sig      The signal to operate on.
 *  \param instance The instance to suspend. */
void mapper_signal_remove_instance(mapper_signal sig, int instance);

/*! Get the local id of the oldest active instance.
 *  \param sig      The signal to operate on.
 *  \param instance A location to receive the instance id.
 *  \return         Zero if an instance id has been found, non-zero otherwise. */
int mapper_signal_oldest_active_instance(mapper_signal sig, int *instance);

/*! Get the local id of the newest active instance.
 *  \param sig      The signal to operate on.
 *  \param instance A location to receive the instance id.
 *  \return         Zero if an instance id has been found, non-zero otherwise. */
int mapper_signal_newest_active_instance(mapper_signal sig, int *instance);

/*! Get a signal_instance's value.
 *  \param sig      The signal to operate on.
 *  \param instance The instance to operate on.
 *  \param tt       A location to receive the value's time tag. May be 0.
 *  \return         A pointer to an array containing the value of the signal
 *                  instance, or 0 if the signal instance has no value. */
void *mapper_signal_instance_value(mapper_signal sig, int instance,
                                   mapper_timetag_t *tt);

/*! Return the number of active instances owned by a signal.
 *  \param  sig The signal to query.
 *  \return     The number of active instances. */
int mapper_signal_num_active_instances(mapper_signal sig);

/*! Return the number of reserved instances owned by a signal.
 *  \param  sig The signal to query.
 *  \return     The number of active instances. */
int mapper_signal_num_reserved_instances(mapper_signal sig);

/*! Get an active signal instance's ID by its index.  Intended to be
 * used for iterating over the active instances.
 *  \param sig    The signal to operate on.
 *  \param index  The numerical index of the ID to retrieve.  Shall be
 *                between zero and the return value of
 *                mapper_signal_num_active_instances().
 *  \return       The instance ID associated with the given index. */
int mapper_signal_active_instance_id(mapper_signal sig, int index);

/*! Set the allocation method to be used when a previously-unseen
 *  instance ID is received.
 *  \param sig  The signal to operate on.
 *  \param mode Method to use for adding or reallocating active instances
 *              if no reserved instances are available. */
void mapper_signal_set_instance_allocation_mode(mapper_signal sig,
                                                mapper_instance_allocation_type mode);

/*! Get the allocation method to be used when a previously-unseen
 *  instance ID is received.
 *  \param sig  The signal to operate on.
 *  \return     The allocation mode of the provided signal. */
mapper_instance_allocation_type mapper_instance_allocation_mode(mapper_signal sig);

/*! A handler function to be called whenever a signal instance management
 *  event occurs. */
typedef void mapper_instance_event_handler(mapper_signal sig, int instance,
                                           mapper_instance_event_t event,
                                           mapper_timetag_t *tt);

/*! Set the handler to be called on signal instance management events.
 *  \param sig          The signal to operate on.
 *  \param h            A handler function for processing instance managment events.
 *  \param flags        Bitflags for indicating the types of events which should
 *                      trigger the callback. Can be a combination of IN_NEW,
 *                      IN_UPSTREAM_RELEASE, IN_DOWNSTREAM_RELEASE, and IN_OVERFLOW.
 *  \param user_data    User context pointer to be passed to handler. */
void mapper_signal_set_instance_event_callback(mapper_signal sig,
                                               mapper_instance_event_handler h,
                                               int flags, void *user_data);

/*! Associate a signal instance with an arbitrary pointer.
 *  \param sig          The signal to operate on.
 *  \param instance     The instance to operate on.
 *  \param user_data    A pointer to user data to be associated
 *                      with this instance. */
void mapper_signal_set_instance_data(mapper_signal sig, int instance,
                                     void *user_data);

/*! Retrieve the arbitrary pointer associated with a signal instance.
 *  \param sig          The signal to operate on.
 *  \param instance     The instance to operate on.
 *  \return             A pointer associated with this instance. */
void *mapper_signal_instance_data(mapper_signal sig, int instance);

/*! A signal handler function can be called whenever a signal value
 *  changes. */
typedef void mapper_signal_update_handler(mapper_signal sig, int instance,
                                          void *value, int count,
                                          mapper_timetag_t *tt);

/*! Set or unset the message handler for a signal.
 *  \param sig       The signal to operate on.
 *  \param handler   A pointer to a mapper_signal_update_handler
 *                   function for processing incoming messages.
 *  \param user_data User context pointer to be passed to handler. */
void mapper_signal_set_callback(mapper_signal sig,
                                mapper_signal_update_handler *handler,
                                void *user_data);

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
 *  \return         The signal direction: DI_INCOMING or DI_OUTGOING. */
mapper_direction_t mapper_signal_direction(mapper_signal sig);

/*! Get the unique id for a specific signal.
 *  \param sig      The signal to check.
 *  \return         The signal id. */
uint64_t mapper_signal_id(mapper_signal sig);

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
    
/*! Get the number of maps attatched to a specific signal.
 *  \param sig      The signal to check.
 *  \return         The number of attached maps. */
int mapper_signal_num_maps(mapper_signal sig);

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

/*! Look up a signal property by name.
 *  \param sig      The signal to check.
 *  \param property The name of the property to retrieve.
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \return Zero if found, otherwise non-zero. */
int mapper_signal_property(mapper_signal sig, const char *property,
                           char *type, const void **value, int *length);

/*! Look up a signal property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param sig      The signal to check.
 *  \param index    Numerical index of a signal property.
 *  \param property Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \return Zero if found, otherwise non-zero. */
int mapper_signal_property_index(mapper_signal sig, unsigned int index,
                                 const char **property, char *type,
                                 const void **value, int *length);

/*! Set the description property for a specific signal.
 *  \param sig          The signal to modify.
 *  \param description  The description value to set. */
void mapper_signal_set_description(mapper_signal sig, const char *description);

/*! Set or remove the maximum of a signal.
 *  \param sig      The signal to modify.
 *  \param maximum  Must be the same type as the signal, or 0 to remove
 *                  the maximum. */
void mapper_signal_set_maximum(mapper_signal sig, void *maximum);

/*! Set or remove the minimum of a signal.
 *  \param sig      The signal to modify.
 *  \param minimum  Must be the same type as the signal, or 0 to remove
 *                  the minimum. */
void mapper_signal_set_minimum(mapper_signal sig, void *minimum);

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
 *  \param property The name of the property to add.
 *  \param type     The property  datatype.
 *  \param value    An array of property values.
 *  \param length   The length of value array. */
void mapper_signal_set_property(mapper_signal sig, const char *property,
                                char type, void *value, int length);

/*! Remove a property of a signal.
 *  \param sig      The signal to operate on.
 *  \param property The name of the property to remove. */
void mapper_signal_remove_property(mapper_signal sig, const char *property);

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
 *  \param map      The device record.
 *  \param action   A value of mapper_action_t indicating what is happening
 *                  to the map.
 *  \param user     The user context pointer registered with this callback. */
typedef void mapper_device_map_handler(mapper_map map, mapper_action_t action,
                                       void *user);

/*! Set a function to be called when a map involving a device's local signals is
 *  established, modified or destroyed, indicated by the action parameter to the
 *  provided function. */
void mapper_device_set_map_handler(mapper_device dev,
                                    mapper_device_map_handler *h,
                                    void *user);

/*! Allocate and initialize a mapper device.
 *  \param name_prefix  A short descriptive string to identify the device.
 *  \param port         An optional port for starting the port allocation
 *                      scheme.
 *  \param admin        A previously allocated admin to use.  If 0, an
 *                      admin will be allocated for use with this device.
 *  \return             A newly allocated mapper device.  Should be free
 *                      using mapper_device_free(). */
mapper_device mapper_device_new(const char *name_prefix, int port,
                                mapper_admin admin);

//! Free resources used by a mapper device.
void mapper_device_free(mapper_device dev);

/*! Add an input signal to a mapper device.  Values and strings
 *  pointed to by this call (except user_data) will be copied.
 *  For minimum and maximum, actual type must correspond to 'type'.
 *  If type='i', then int*; if type='f', then float*.
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
mapper_signal mapper_device_add_input(mapper_device dev, const char *name,
                                      int length, char type, const char *unit,
                                      void *minimum, void *maximum,
                                      mapper_signal_update_handler *handler,
                                      void *user_data);

/*! Add an output signal to a mapper device.  Values and strings
 *  pointed to by this call will be copied.
 *  For minimum and maximum, actual type must correspond to 'type'.
 *  If type='i', then int*; if type='f', then float*.
 *  \param dev          The device to add a signal to.
 *  \param name         The name of the signal.
 *  \param length       The length of the signal vector, or 1 for a scalar.
 *  \param type         The type fo the signal value.
 *  \param unit     	The unit of the signal, or 0 for none.
 *  \param minimum      Pointer to a minimum value, or 0 for none.
 *  \param maximum      Pointer to a maximum value, or 0 for none. */
mapper_signal mapper_device_add_output(mapper_device dev, const char *name,
                                       int length, char type, const char *unit,
                                       void *minimum, void *maximum);

/* Remove a device's signal.
 * \param dev The device to remove a signal from.
 * \param sig The signal to remove. */
void mapper_device_remove_signal(mapper_device dev, mapper_signal sig);

/* Remove a device's input signal.
 * \param dev The device to remove a signal from.
 * \param sig The signal to remove. */
void mapper_device_remove_input(mapper_device dev, mapper_signal sig);

/* Remove a device's output signal.
 * \param dev The device to remove a signal from.
 * \param sig The signal to remove. */
void mapper_device_remove_output(mapper_device dev, mapper_signal sig);

//! Return the number of inputs.
int mapper_device_num_inputs(mapper_device dev);

//! Return the number of outputs.
int mapper_device_num_outputs(mapper_device dev);

//! Return the number of incoming maps.
int mapper_device_num_incoming_maps(mapper_device dev);

//! Return the number of outgoing maps.
int mapper_device_num_outgoing_maps(mapper_device dev);

/*! Look up a device property by name.
 *  \param dev      The device record to check.
 *  \param property The name of the property to retrieve.
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \return Zero if found, otherwise non-zero. */
int mapper_device_property(mapper_device dev, const char *property,
                           char *type, const void **value, int *length);

/*! Look up a device property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param dev      The device record to check.
 *  \param index    Numerical index of a device property.
 *  \param property Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \return Zero if found, otherwise non-zero. */
int mapper_device_property_index(mapper_device dev, unsigned int index,
                                 const char **property, char *type,
                                 const void **value, int *length);

/*! Set a property of a device.  Can be used to provide arbitrary
 *  metadata.  Value pointed to will be copied.
 *  \param dev      The device to operate on.
 *  \param property The name of the property to add.
 *  \param type     The property  datatype.
 *  \param value    An array of property values.
 *  \param length   The length of value array. */
void mapper_device_set_property(mapper_device dev, const char *property,
                                char type, void *value, int length);

/*! Remove a property of a device.
 *  \param dev      The device to operate on.
 *  \param property The name of the property to remove. */
void mapper_device_remove_property(mapper_device dev, const char *property);

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
 *  \param md   The device to count file descriptors for.
 *  \return     The number of file descriptors needed for the indicated
 *              device. */
int mapper_device_num_fds(mapper_device dev);

/*! Write the list of file descriptors for this device to the provided
 *  array.  Up to num file descriptors will be written.  These file
 *  descriptors can be used as input for the read array of select or
 *  poll, for example.
 *  \param md   The device to get file descriptors for.
 *  \param fds  Memory to receive file descriptors.
 *  \param num  The number of file descriptors pointed to by fds.
 *  \return     The number of file descriptors actually written to fds. */
int mapper_device_fds(mapper_device md, int *fds, int num);

/*! If an external event indicates that a file descriptor for this
 *  device needs servicing, this function should be called.
 *  \param md   The device that needs servicing.
 *  \param fd   The file descriptor that needs servicing. */
void mapper_device_service_fd(mapper_device md, int fd);

/*! Detect whether a device is completely initialized.
 *  \return     Non-zero if device is completely initialized, i.e., has an
 *              allocated receiving port and unique network name.  Zero
 *              otherwise. */
int mapper_device_ready(mapper_device dev);

/*! Return a string indicating the device's full name, if it is
 *  registered.  The returned string must not be externally modified.
 *  \param dev  The device to query.
 *  \return     String containing the device's full name, or zero if it is
 *              not available. */
const char *mapper_device_name(mapper_device dev);

/*! Return the unique id allocated to this device by the mapper network.
 *  \param  dev The device to query.
 *  \return     An integer indicating the device's id, or zero if it is
 *              not available. */
uint64_t mapper_device_id(mapper_device dev);

/*! Return the port used by a device to receive signals, if available.
 *  \param dev  The device to query.
 *  \return     An integer indicating the device's port, or zero if it is
 *              not available. */
unsigned int mapper_device_port(mapper_device dev);

/*! Return the IPv4 address used by a device to receive signals, if available.
 *  \param      dev The device to query.
 *  \return     A pointer to an in_addr struct indicating the device's IP
 *              address, or zero if it is not available.  In general this
 *              will be the IPv4 address associated with the selected
 *              local network interface. */
const struct in_addr *mapper_device_ip4(mapper_device dev);

/*! Return a string indicating the name of the network interface this
 *  device is listening on.
 *  \param dev  The device to query.
 *  \return     A string containing the name of the network interface. */
const char *mapper_device_interface(mapper_device dev);

/*! Return an allocated ordinal which is appended to this device's
 *  network name.  In general the results of this function are not
 *  valid unless mapper_device_ready() returns non-zero.
 *  \param dev  The device to query.
 *  \return     A positive ordinal unique to this device (per name). */
unsigned int mapper_device_ordinal(mapper_device dev);

/*! Start a time-tagged mapper queue. */
void mapper_device_start_queue(mapper_device md, mapper_timetag_t tt);

/*! Dispatch a time-tagged mapper queue. */
void mapper_device_send_queue(mapper_device md, mapper_timetag_t tt);

/*! Get access to the device's underlying lo_server. */
lo_server mapper_device_lo_server(mapper_device dev);

/*! Get the device's synchronization clock offset. */
double mapper_device_clock_offset(mapper_device dev);

mapper_db mapper_device_db(mapper_device dev);

/* @} */

/*** Admins ***/

/*! @defgroup admins Admins

    @{ Admins handle the traffic on the multicast admin bus.  In
       general, you do not need to worry about this interface, as an
       admin will be created automatically when allocating a device.
       An admin only needs to be explicitly created if you plan to
       override default settings for the admin bus.  */

/*! Create an admin with custom parameters.  Creating an admin object
 *  manually is only required if you wish to specify custom network
 *  parameters.  Creating a device or monitor without specifying an
 *  admin will give you an object working on the "standard"
 *  configuration.
 * \param iface  If non-zero, a string identifying a preferred network
 *               interface.  This string can be enumerated e.g. using
 *               if_nameindex(). If zero, an interface will be
 *               selected automatically.
 * \param group  If non-zero, specify a multicast group address to use.
 *               Zero indicates that the standard group 224.0.1.3 should
 *               be used.
 * \param port   If non-zero, specify a multicast port to use.  Zero
 *               indicates that the standard port 7570 should be used.
 * \return       A newly allocated admin.  Should be freed using
 *               mapper_admin_free() */
mapper_admin mapper_admin_new(const char *iface, const char *group, int port);

/*! Free an admin created with mapper_admin_new(). */
void mapper_admin_free(mapper_admin admin);

/*! Get the version of libmapper */
const char *mapper_admin_libversion(mapper_admin admin);

/* @} */

/**** Device database ****/

/*! @defgroup devicedb Device database

    @{ A monitor may query information about devices on the network
       through this interface. */

/*! Set the timeout in seconds after which a database will declare a device
 *  "unresponsive". Defaults to ADMIN_TIMEOUT_SEC.
 *  \param db       The database to use.
 *  \param timeout  The timeout in seconds. */
void mapper_db_set_timeout(mapper_db db, int timeout);

/*! Get the timeout in seconds after which a database will declare a device
 *  "unresponsive". Defaults to ADMIN_TIMEOUT_SEC.
 *  \param db       The database to use.
 *  \return         The current timeout in seconds. */
int mapper_db_timeout(mapper_db db);

/*! Remove unresponsive devices from the database.
 *  \param db       The database to flush.
 *  \param timeout  The number of seconds a device must have been unresponsive
 *                  before removal.
 *  \param quiet    1 to disable callbacks during db flush, 0 otherwise. */
void mapper_db_flush(mapper_db db, int timeout, int quiet);

/*! A callback function prototype for when a device record is added or updated.
 *  Such a function is passed in to mapper_db_add_device_callback().
 *  \param dev      The device record.
 *  \param action   A value of mapper_action_t indicating what is happening
 *                  to the database record.
 *  \param user     The user context pointer registered with this callback. */
typedef void mapper_db_device_handler(mapper_device dev, mapper_action_t action,
                                      void *user);

/*! Register a callback for when a device record is added or updated
 *  in the database.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     A user-defined pointer to be passed to the callback
 *                  for context . */
void mapper_db_add_device_callback(mapper_db db,
                                   mapper_db_device_handler *h,
                                   void *user);

/*! Remove a device record callback from the database service.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     The user context pointer that was originally specified
 *                  when adding the callback. */
void mapper_db_remove_device_callback(mapper_db db,
                                      mapper_db_device_handler *h,
                                      void *user);

/*! Return the whole list of devices.
 *  \param db       The database to query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_device_query_next() to iterate. */
mapper_device *mapper_db_devices(mapper_db db);

/*! Find information for a registered device.
 *  \param db       The database to query.
 *  \param name     Name of the device to find in the database.
 *  \return         Information about the device, or zero if not found. */
mapper_device mapper_db_device_by_name(mapper_db db, const char *name);

/*! Look up information for a registered device using its unique id.
 *  \param db       The database to query.
 *  \param id       Unique id identifying the device to find in the database.
 *  \return         Information about the device, or zero if not found. */
mapper_device mapper_db_device_by_id(mapper_db db, uint64_t id);

/*! Return the list of devices with a substring in their name.
 *  \param db       The database to query.
 *  \param pattern  The substring to search for.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_device_query_next() to iterate. */
mapper_device *mapper_db_devices_by_name_match(mapper_db db, const char *pattern);

/*! Return the list of devices matching the given property.
 *  \param db       The database to query.
 *  \param property The name of the property to search for.
 *  \param type     The value type.
 *  \param length   The value length.
 *  \param value    The value.
 *  \param op       A string specifying the comparison operator, can be one of
 *                  OP_EQUAL, OP_NOT_EQUAL, OP_GREATER_THAN, OP_LESS_THAN,
 *                  OP_GREATER_THAN_OR_EQUAL, OP_LESS_THAN_OR_EQUAL, OP_EXISTS,
 *                  or OP_DOES_NOT_EXIST.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_device_query_next() to iterate. */
mapper_device *mapper_db_devices_by_property(mapper_db db, const char *property,
                                             char type, int length,
                                             const void *value,
                                             mapper_query_op op);

/*! Get the union of two device queries (devices matching query1 OR query2).
 *  \param query1   The first device query.
 *  \param query2   The second device query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_device_query_next() to iterate. */
mapper_device *mapper_device_query_union(mapper_device *query1,
                                         mapper_device *query2);

/*! Get the intersection of two device queries (devices matching query1 AND
 *  query2).
 *  \param query1   The first device query.
 *  \param query2   The second device query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_device_query_next() to iterate. */
mapper_device *mapper_device_query_intersection(mapper_device *query1,
                                                mapper_device *query2);

/*! Get the difference between two device queries (devices matching query1 but
 *  NOT query2).
 *  \param query1   The first device query.
 *  \param query2   The second device query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_device_query_next() to iterate. */
mapper_device *mapper_device_query_difference(mapper_device *query1,
                                              mapper_device *query2);

/*! Given a device record pointer returned from a previous mapper_db_device*()
 *  call, get an indexed item in the list.
 *  \param query    The previous device record pointer.
 *  \param index    The index of the list element to retrieve.
 *  \return         A pointer to the device record, or zero if it doesn't
 *                  exist. */
mapper_device mapper_device_query_index(mapper_device *query, int index);

/*! Given a device record pointer returned from a previous mapper_db_device*()
 *  call, get the next item in the list.
 *  \param query    The previous device record pointer.
 *  \return         A double-pointer to the next device record in the list, or
 *                  zero if no more devices. */
mapper_device *mapper_device_query_next(mapper_device *query);

/*! Given a device record pointer returned from a previous mapper_db_device*()
 *  call, indicate that we are done iterating.
 *  \param query    The previous device record pointer. */
void mapper_device_query_done(mapper_device *query);

/*! Helper for printing typed mapper_prop values.
 *  \param type     The value type.
 *  \param length   The vector length of the value.
 *  \param value    A pointer to the property value to print. */
void mapper_prop_pp(char type, int length, const void *value);

/* @} */

/***** Signals *****/

/*! @defgroup signaldb Signal database

    @{ A monitor may query information about signals on the network
       through this interface. It is also used by local signals to
       store property information. */

/*! A callback function prototype for when a signal record is added or updated.
 *  Such a function is passed in to mapper_db_add_signal_callback().
 *  \param sig      The signal record.
 *  \param action   A value of mapper_action_t indicating what is happening
 *                  to the database record.
 *  \param user     The user context pointer registered with this callback. */
typedef void mapper_db_signal_handler(mapper_signal sig, mapper_action_t action,
                                      void *user);

/*! Register a callback for when a signal record is added or updated
 *  in the database.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     A user-defined pointer to be passed to the callback
 *                  for context . */
void mapper_db_add_signal_callback(mapper_db db,
                                   mapper_db_signal_handler *h,
                                   void *user);

/*! Remove a signal record callback from the database service.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     The user context pointer that was originally specified
 *                  when adding the callback. */
void mapper_db_remove_signal_callback(mapper_db db,
                                      mapper_db_signal_handler *h,
                                      void *user);

/*! Find information for a registered input signal.
 *  \param db       The database to query.
 *  \param id       Unique id of the signal to find in the database.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_db_signal_by_id(mapper_db db, uint64_t id);

/*! Return the list of all known signals across all devices.
 *  \param db       The database to query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_signals(mapper_db db);

/*! Return the list of all known inputs across all devices.
 *  \param db       The database to query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_inputs(mapper_db db);

/*! Return the list of all known outputs across all devices.
 *  \param db       The database to query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_outputs(mapper_db db);

/*! Return the list of signals for a given device.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_device_signals(mapper_db db, mapper_device dev);

/*! Return the list of inputs for a given device.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_device_inputs(mapper_db db, mapper_device dev);

/*! Return the list of outputs for a given device.
 *  \param db       The database to query.
 *  \param dev      Device record query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_device_outputs(mapper_db db, mapper_device dev);

/*! Find information for registered signals.
 *  \param db       The database to query.
 *  \param sig_name Name of the signal to find in the database.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_signals_by_name(mapper_db db, const char *sig_name);

/*! Find information for registered input signals.
 *  \param db       The database to query.
 *  \param sig_name Name of the input signal to find in the database.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_inputs_by_name(mapper_db db, const char *sig_name);

/*! Find information for registered output signals.
 *  \param db       The database to query.
 *  \param sig_name Name of the output signal to find in the database.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_outputs_by_name(mapper_db db, char const *sig_name);

/*! Find information for registered signals.
 *  \param db       The database to query.
 *  \param pattern  The substring to search for.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_signals_by_name_match(mapper_db db, const char *pattern);

/*! Find information for registered input signals.
 *  \param db       The database to query.
 *  \param pattern  The substring to search for.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_inputs_by_name_match(mapper_db db, const char *pattern);

/*! Find information for registered output signals.
 *  \param db       The database to query.
 *  \param pattern  The substring to search for.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_outputs_by_name_match(mapper_db db, char const *pattern);

/*! Return the list of signals matching the given property.
 *  \param db       The database to query.
 *  \param property The name of the property to search for.
 *  \param type     The value type.
 *  \param length   The value length.
 *  \param value    The value.
 *  \param op       A string specifying the comparison operator, can be one of
 *                  OP_EQUAL, OP_NOT_EQUAL, OP_GREATER_THAN, OP_LESS_THAN,
 *                  OP_GREATER_THAN_OR_EQUAL, OP_LESS_THAN_OR_EQUAL, OP_EXISTS,
 *                  or OP_DOES_NOT_EXIST.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_signals_by_property(mapper_db db, const char *property,
                                             char type, int length,
                                             const void *value,
                                             mapper_query_op op);

/*! Find information for a registered input signal.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \param sig_name Name of the signal to find in the database.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_db_device_signal_by_name(mapper_db db, mapper_device dev,
                                              const char *sig_name);

/*! Find information for a registered input signal.
 *  \param db       The database to query.
 *  \param dev      Device recordto query.
 *  \param sig_name Name of the input signal to find in the database.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_db_device_input_by_name(mapper_db db, mapper_device dev,
                                             const char *sig_name);

/*! Find information for a registered output signal.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \param sig_name Name of the output signal to find in the database.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_db_device_output_by_name(mapper_db db, mapper_device dev,
                                              char const *sig_name);

/*! Find information for a registered input signal.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \param index    Index of the signal to find in the database.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_db_device_signal_by_index(mapper_db db, mapper_device dev,
                                               int index);

/*! Find information for a registered input signal.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \param index    Index of the input signal to find in the database.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_db_device_input_by_index(mapper_db db, mapper_device dev,
                                              int index);

/*! Find information for a registered output signal.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \param index    Index of the output signal to find in the database.
 *  \return         Information about the signal, or zero if not found. */
mapper_signal mapper_db_device_output_by_index(mapper_db db, mapper_device dev,
                                               int index);

/*! Return the list of signals for a given device.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \param pattern  A substring to search for in the device signals.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_device_signals_by_name_match(mapper_db db,
                                                      mapper_device dev,
                                                      char const *pattern);

/*! Return the list of inputs for a given device.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \param pattern  A substring to search for in the device inputs.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_device_inputs_by_name_match(mapper_db db,
                                                     mapper_device dev,
                                                     const char *pattern);

/*! Return the list of outputs for a given device.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \param pattern  A substring to search for in the device outputs.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_db_device_outputs_by_name_match(mapper_db db,
                                                      mapper_device dev,
                                                      char const *pattern);

/*! Get the union of two signal queries (signals matching query1 OR query2).
 *  \param query1   The first signal query.
 *  \param query2   The second signal query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_signal_query_union(mapper_signal *query1,
                                         mapper_signal *query2);

/*! Get the intersection of two signal queries (signals matching query1 AND
 *  query2).
 *  \param query1   The first signal query.
 *  \param query2   The second signal query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_signal_query_intersection(mapper_signal *query1,
                                                mapper_signal *query2);

/*! Get the difference between two signal queries (signals matching query1 but
 *  NOT query2).
 *  \param query1   The first signal query.
 *  \param query2   The second signal query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_signal_query_next() to iterate. */
mapper_signal *mapper_signal_query_difference(mapper_signal *query1,
                                              mapper_signal *query2);

/*! Given a signal record pointer returned from a previous mapper_db_signal*()
 *  call, get an indexed item in the list.
 *  \param query    The previous signal record pointer.
 *  \param index    The index of the list element to retrieve.
 *  \return         A pointer to the signal record, or zero if it doesn't
 *                  exist. */
mapper_signal mapper_signal_query_index(mapper_signal *query, int index);

/*! Given a signal record pointer returned from a previous mapper_db_signal*()
 *  call, get the next item in the list.
 *  \param query    The previous signal record pointer.
 *  \return         A double-pointer to the next signal record in the list, or
 *                  zero if no more signals. */
mapper_signal *mapper_signal_query_next(mapper_signal *query);

/*! Given a signal record pointer returned from a previous mapper_db_signal*()
 *  call, indicate that we are done iterating.
 *  \param query    The previous signal record pointer. */
void mapper_signal_query_done(mapper_signal *query);

/* @} */

/***** Maps *****/

/*! @defgroup mapdb Maps database

    @{ A monitor may query information about maps between signals on the network
       through this interface.  It is also used to specify properties during
       mapping requests. */

/*! A callback function prototype for when a map record is added or updated in
 *  the database. Such a function is passed in to mapper_db_add_map_callback().
 *  \param map      The map record.
 *  \param action   A value of mapper_action_t indicating what is happening
 *                  to the database record.
 *  \param user     The user context pointer registered with this callback. */
typedef void mapper_map_handler(mapper_map map, mapper_action_t action,
                                void *user);

/*! Register a callback for when a map record is added or updated.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     A user-defined pointer to be passed to the callback
 *                  for context . */
void mapper_db_add_map_callback(mapper_db db, mapper_map_handler *h,
                                void *user);

/*! Remove a map record callback from the database service.
 *  \param db       The database to query.
 *  \param h        Callback function.
 *  \param user     The user context pointer that was originally specified
 *                  when adding the callback. */
void mapper_db_remove_map_callback(mapper_db db, mapper_map_handler *h,
                                   void *user);

/*! Return a list of all registered maps.
 *  \param db       The database to query.
 *  \return         A double-pointer to the first item in the list of results,
 *                  or zero if none.  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_maps(mapper_db db);

/*! Return the map that match the given map id.
 *  \param db       The database to query.
 *  \param id       Unique id identifying the map.
 *  \return         A pointer to a structure containing information on the
 *                  found map, or 0 if not found. */
mapper_map mapper_db_map_by_id(mapper_db db, uint64_t id);

/*! Return the list of maps matching the given property.
 *  \param db       The database to query.
 *  \param property The name of the property to search for.
 *  \param type     The value type.
 *  \param length   The value length.
 *  \param value    The value.
 *  \param op       A string specifying the comparison operator, can be one of
 *                  OP_EQUAL, OP_NOT_EQUAL, OP_GREATER_THAN, OP_LESS_THAN,
 *                  OP_GREATER_THAN_OR_EQUAL, OP_LESS_THAN_OR_EQUAL, OP_EXISTS,
 *                  or OP_DOES_NOT_EXIST.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_maps_by_property(mapper_db db, const char *property,
                                       char type, int length, const void *value,
                                       mapper_query_op op);

/*! Return the list of maps matching the given slot property.
 *  \param db       The database to query.
 *  \param property The name of the property to search for.
 *  \param type     The value type.
 *  \param length   The value length.
 *  \param value    The value.
 *  \param op       A string specifying the comparison operator, can be one of
 *                  OP_EQUAL, OP_NOT_EQUAL, OP_GREATER_THAN, OP_LESS_THAN,
 *                  OP_GREATER_THAN_OR_EQUAL, OP_LESS_THAN_OR_EQUAL, OP_EXISTS,
 *                  or OP_DOES_NOT_EXIST.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_maps_by_slot_property(mapper_db db, const char *property,
                                            char type, int length,
                                            const void *value,
                                            mapper_query_op op);

/*! Return the list of maps matching the given source slot property.
 *  \param db       The database to query.
 *  \param property The name of the property to search for.
 *  \param type     The value type.
 *  \param length   The value length.
 *  \param value    The value.
 *  \param op       A string specifying the comparison operator, can be one of
 *                  OP_EQUAL, OP_NOT_EQUAL, OP_GREATER_THAN, OP_LESS_THAN,
 *                  OP_GREATER_THAN_OR_EQUAL, OP_LESS_THAN_OR_EQUAL, OP_EXISTS,
 *                  or OP_DOES_NOT_EXIST.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_maps_by_src_slot_property(mapper_db db,
                                                const char *property,
                                                char type, int length,
                                                const void *value,
                                                mapper_query_op op);

/*! Return the list of maps matching the given destination slot property.
 *  \param db       The database to query.
 *  \param property The name of the property to search for.
 *  \param type     The value type.
 *  \param length   The value length.
 *  \param value    The value.
 *  \param op       A string specifying the comparison operator, can be one of
 *                  OP_EQUAL, OP_NOT_EQUAL, OP_GREATER_THAN, OP_LESS_THAN,
 *                  OP_GREATER_THAN_OR_EQUAL, OP_LESS_THAN_OR_EQUAL, OP_EXISTS,
 *                  or OP_DOES_NOT_EXIST.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_maps_by_dest_slot_property(mapper_db db,
                                                 const char *property,
                                                 char type, int length,
                                                 const void *value,
                                                 mapper_query_op op);

/*! Return the list of maps that touch the given device name.
 *  \param db       The database to query.
 *  \param dev      Device record query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_device_maps(mapper_db db, mapper_device dev);

/*! Return the list of outgoing maps that touch the given device name.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_device_outgoing_maps(mapper_db db, mapper_device dev);

/*! Return the list of incoming maps that touch the given device name.
 *  \param db       The database to query.
 *  \param dev      Device record to query.
 *  \return         A double-pointer to the first item in the list of results.
 *                  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_device_incoming_maps(mapper_db db, mapper_device dev);

/*! Return the list of maps for a given signal name.
 *  \param db       The database to query.
 *  \param sig      Signal record to query for maps.
 *  \return         A double-pointer to the first item in the list of results
 *                  or zero if none.  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_signal_maps(mapper_db db, mapper_signal sig);

/*! Return the list of maps for a given source signal name.
 *  \param db       The database to query.
 *  \param sig      Signal record to query for outgoing maps.
 *  \return         A double-pointer to the first item in the list of results
 *                  or zero if none.  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_signal_outgoing_maps(mapper_db db, mapper_signal sig);

/*! Return the list of maps for a given destination signal name.
 *  \param db       The database to query.
 *  \param sig      Signal record to query for incoming maps.
 *  \return         A double-pointer to the first item in the list of results,
 *                  or zero if none.  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_db_signal_incoming_maps(mapper_db db, mapper_signal sig);

/*! Get the union of two map queries (maps matching query1 OR query2).
 *  \param query1   The first map query.
 *  \param query2   The second map query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_map_query_union(mapper_map *query1, mapper_map *query2);

/*! Get the intersection of two map queries (maps matching query1 AND query2).
 *  \param query1   The first map query.
 *  \param query2   The second map query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_map_query_intersection(mapper_map *query1,
                                          mapper_map *query2);

/*! Get the difference between two map queries (maps matching query1 but NOT
 *  query2).
 *  \param query1   The first map query.
 *  \param query2   The second map query.
 *  \return         A double-pointer to the first item in a list of results.
 *                  Use mapper_db_map_query_next() to iterate. */
mapper_map *mapper_map_query_difference(mapper_map *query1, mapper_map *query2);

/*! Given a map record pointer returned from a previous mapper_db_map*()
 *  call, get an indexed item in the list.
 *  \param query    The previous map record pointer.
 *  \param index    The index of the list element to retrieve.
 *  \return         A pointer to the map record, or zero if it doesn't exist. */
mapper_map mapper_map_query_index(mapper_map *query, int index);

/*! Given a map record pointer returned from a previous mapper_db_map*() call,
 *  get the next item in the list.
 *  \param query    The previous map record pointer.
 *  \return         A double-pointer to the next map record in the list, or
 *                  zero if no more maps. */
mapper_map *mapper_map_query_next(mapper_map *query);

/*! Given a map record pointer returned from a previous mapper_db_map*() call,
 *  indicate that we are done iterating.
 *  \param query    The previous map record pointer. */
void mapper_map_query_done(mapper_map *query);

void mapper_map_pp(mapper_map map);

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
uint64_t mapper_map_id(mapper_map map);

/*! Get the mode property for a specific map.
 *  \param map      The map to check.
 *  \return         The mode parameter for this map. */
mapper_mode_type mapper_map_mode(mapper_map map);

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
 *  \return         LOC_SOURCE if processing is evaluated at the source
 *                  device, LOC_DESTINATION otherwise. */
mapper_location mapper_map_process_location(mapper_map map);

/*! Get the scopes property for a specific map.
 *  \param map      The map to check.
 *  \return         A double-pointer to the first item in the list of results
 *                  or zero if none.  Use mapper_db_map_query_next() to iterate. */
mapper_device *mapper_map_scopes(mapper_map map);

/*! Set the description property for a specific map. Changes to remote maps will
 *  not take effect until synchronized with the network using mmon_update_map().
 *  \param map          The map to modify.
 *  \param description  The description value to set. */
void mapper_map_set_description(mapper_map map, const char *description);

/*! Set the mode property for a specific map. Changes to remote maps will not
 *  take effect until synchronized with the network using mmon_update_map().
 *  \param map      The map to modify.
 *  \param mode     The mode value to set, can be one of MO_EXPRESSION,
 *                  MO_LINEAR, or MO_RAW. */
void mapper_map_set_mode(mapper_map map, mapper_mode_type mode);

/*! Set the expression property for a specific map. Changes to remote maps will not
 *  take effect until synchronized with the network using mmon_update_map().
 *  \param map          The map to modify.
 *  \param expression   A string specifying an expression to be evaluated by
 *                      the map. */
void mapper_map_set_expression(mapper_map map, const char *expression);

/*! Set the muted property for a specific map. Changes to remote maps will not
 *  take effect until synchronized with the network using mmon_update_map().
 *  \param map      The map to modify.
 *  \param muted    1 to mute this map, or 0 unmute. */
void mapper_map_set_muted(mapper_map map, int muted);

/*! Set the process location property for a specific map. Depending on the map
 *  topology and expression specified it may not be possible to set the process
 *  location to LO_SOURCE for all maps. Changes to remote maps will not take
 *  effect until synchronized with the network using mmon_update_map().
 *  \param map      The map to modify.
 *  \param location LOC_SOURCE to indicate processing should be handled by the
 *                  source device, LOC_DESTINATION for the destination device. */
void mapper_map_set_process_location(mapper_map map, mapper_location location);

/*! Set an arbitrary property for a specific map.  Changes to remote maps will
 *  not take effect until synchronized with the network using mmon_update_map().
 *  \param map      The map to modify.
 *  \param property The name of the property to add.
 *  \param type     The property  datatype.
 *  \param value    An array of property values.
 *  \param length   The length of value array. */
void mapper_map_set_property(mapper_map map, const char *property, char type,
                             void *value, int length);

/*! Add a scope to this map. Map scopes configure the propagation of signal
 *  instance updates across the map. Changes to remote maps will not take effect
 *  until synchronized with the network using mmon_update_map().
 *  \param map      The map to modify.
 *  \param device   Device to add as a scope for this map. After taking effect,
 *                  this setting will cause instance updates originating at this
 *                  device to be propagated across the map. */
void mapper_map_add_scope(mapper_map map, mapper_device dev);

/*! Remove a scope from this map. Map scopes configure the propagation of signal
 *  instance updates across the map. Changes to remote maps will not take effect
 *  until synchronized with the network using mmon_update_map().
 *  \param map      The map to modify.
 *  \param device   Device to remove as a scope for this map. After taking effect,
 *                  this setting will cause instance updates originating at this
 *                  device to be blocked from propagating across the map. */
void mapper_map_remove_scope(mapper_map map, mapper_device dev);

/*! Look up a map property by name.
 *  \param map      The map to check.
 *  \param property The name of the property to retrieve.
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \return         Zero if found, otherwise non-zero. */
int mapper_map_property(mapper_map map, const char *property, char *type,
                        const void **value, int *length);

/*! Look up a map property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param map      The map to check.
 *  \param index    Numerical index of a map property.
 *  \param property Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \return Zero if found, otherwise non-zero. */
int mapper_map_property_index(mapper_map map, unsigned int index,
                              const char **property, char *type,
                              const void **value, int *length);

void mapper_slot_pp(mapper_slot slot);

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

/*! Get the "cause update" property for a specific map slot. When enabled,
 *  updates to this slot will cause computation of a new map output.
 *  \param slot     The slot to check.
 *  \return         One to cause update, 0 otherwise. */
int mapper_slot_cause_update(mapper_slot slot);

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

/*! Look up a map slot property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param slot     The map slot to check.
 *  \param index    Numerical index of a map slot property.
 *  \param property Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \return         Zero if found, otherwise non-zero. */
int mapper_slot_property_index(mapper_slot slot, unsigned int index,
                               const char **property, char *type,
                               const void **value, int *length);

/*! Look up a map property by name.
 *  \param slot     The map slot to check.
 *  \param property The name of the property to retrieve.
 *  \param type     A pointer to a location to receive the type of the
 *                  property value. (Required.)
 *  \param value    A pointer to a location to receive the address of the
 *                  property's value. (Required.)
 *  \param length   A pointer to a location to receive the vector length of
 *                  the property value. (Required.)
 *  \return         Zero if found, otherwise non-zero. */
int mapper_slot_property(mapper_slot slot, const char *property,
                         char *type, const void **value, int *length);

/*! Get the "send as instance" property for a specific map slot. If enabled,
 *  updates to this slot will be treated as updates to a specific instance.
 *  \param slot     The slot to check.
 *  \return         One to send as instance, 0 otherwise. */
int mapper_slot_send_as_instance(mapper_slot slot);

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
 *  network using mmon_update_map().
 *  \param slot     The slot to modify.
 *  \param action   The boundary maximum setting. */
void mapper_slot_set_bound_max(mapper_slot slot, mapper_boundary_action action);

/*! Set the boundary minimum property for a specific map slot. This property
 *  controls behaviour when a value is less than the slot minimum value.
 *  Changes to remote maps will not take effect until synchronized with the
 *  network using mmon_update_map().
 *  \param slot     The slot to modify.
 *  \param action   The boundary minimum setting. */
void mapper_slot_set_bound_min(mapper_slot slot, mapper_boundary_action action);

/*! Set the calibrating property for a specific map slot. When enabled, the
 *  slot minimum and maximum values will be updated based on processed data.
 *  Changes to remote maps will not take effect until synchronized with the
 *  network using mmon_update_map().
 *  \param slot         The slot to modify.
 *  \param calibrating  One to enable calibration, 0 otherwise. */
void mapper_slot_set_calibrating(mapper_slot slot, int calibrating);

/*! Set the "cause update" property for a specific map slot. When enabled,
 *  updates to this slot will cause computation of a new map output.
 *  Changes to remote maps will not take effect until synchronized with the
 *  network using mmon_update_map().
 *  \param slot         The slot to modify.
 *  \param cause_update One to enable calibration, 0 otherwise. */
void mapper_slot_set_cause_update(mapper_slot slot, int cause_update);

/*! Set the "maximum" property for a specific map slot.  Changes to remote maps
 *  will not take effect until synchronized with the network using
 *  mmon_update_map().
 *  \param slot     The slot to modify.
 *  \param type     The data type of the update.
 *  \param value    An array of values.
 *  \param length   Length of the update array. */
void mapper_slot_set_maximum(mapper_slot slot, int length, char type,
                             void *value);

/*! Set the "minimum" property for a specific map slot.  Changes to remote maps
 *  will not take effect until synchronized with the network using
 *  mmon_update_map().
 *  \param slot     The slot to modify.
 *  \param type     The data type of the update.
 *  \param value    An array of values.
 *  \param length   Length of the update array. */
void mapper_slot_set_minimum(mapper_slot slot, int length, char type,
                             void *value);

/*! Set the "send as instance" property for a specific map slot.  If enabled,
 *  updates to this slot will be treated as updates to a specific instance.
 *  Changes to remote maps will not take effect until synchronized with the
 *  network using mmon_update_map().
 *  \param slot             The slot to modify.
 *  \param send_as_instance One to send as instance update, 0 otherwise. */
void mapper_slot_set_send_as_instance(mapper_slot slot, int send_as_instance);

/*! Set an arbitrary property for a specific map slot.  Changes to remote maps
 *  will not take effect until synchronized with the network using
 *  mmon_update_map().
 *  \param slot     The slot to modify.
 *  \param property The name of the property to add.
 *  \param type     The property  datatype.
 *  \param value    An array of property values.
 *  \param length   The length of value array. */
void mapper_slot_set_property(mapper_slot slot, const char *property, char type,
                              void *value, int length);

/* @} */

/***** Monitors *****/

/*! @defgroup monitor Monitors

    @{ Monitors are the primary interface through which a program may
       observe the network and store information about devices and
       signals that are present.  Each monitor has a database of
       devices, signals, and maps, which can be queried.
       A monitor can also make map requests.  In
       general, the monitor interface is useful for building GUI
       applications to control the network. */

/*! Bit flags for coordinating monitor metadata subscriptions. Subsets of
 *  device information must also include SUB_DEVICE. */
#define SUBSCRIBE_NONE              0x00
#define SUBSCRIBE_DEVICE            0x01
#define SUBSCRIBE_DEVICE_INPUTS     0x02
#define SUBSCRIBE_DEVICE_OUTPUTS    0x04
#define SUBSCRIBE_DEVICE_SIGNALS    SUBSCRIBE_DEVICE_INPUTS | SUBSCRIBE_DEVICE_OUTPUTS
#define SUBSCRIBE_DEVICE_MAPS_IN    0x10
#define SUBSCRIBE_DEVICE_MAPS_OUT   0x20
#define SUBSCRIBE_DEVICE_MAPS       SUBSCRIBE_DEVICE_MAPS_IN | SUBSCRIBE_DEVICE_MAPS_OUT
#define SUBSCRIBE_ALL               0xFF

/*! Create a network monitor.
 *  \param admin               A previously allocated admin to use.  If 0, an
 *                             admin will be allocated for use with this monitor.
 *  \param autosubscribe_flags Sets whether the monitor should automatically
 *                             subscribe to information about signals
 *                             and maps when it encounters a
 *                             previously-unseen device.
 *  \return The new monitor. */
mapper_monitor mmon_new(mapper_admin admin, int autosubscribe_flags);

/*! Free a network monitor. */
void mmon_free(mapper_monitor mon);

/*! Poll a network monitor.
 *  \param mon      The monitor to poll.
 *  \param block_ms The number of milliseconds to block, or 0 for
 *                  non-blocking behaviour.
 *  \return The number of handled messages. */
int mmon_poll(mapper_monitor mon, int block_ms);

/*! Get the database associated with a monitor. This can be used as
 *  long as the monitor remains alive. */
mapper_db mmon_db(mapper_monitor mon);

/*! Request that all devices report in. */
void mmon_request_devices(mapper_monitor mon);

/*! Subscribe to information about a specific device.
 *  \param mon      The monitor to use.
 *  \param dev      The device of interest. If NULL the monitor will
 *                  automatically subscribe to all discovered devices.
 *  \param flags    Bitflags setting the type of information of interest.  Can
 *                  be a combination of SUB_DEVICE, SUB_DEVICE_INPUTS,
 *                  SUB_DEVICE_OUTPUTS, SUB_DEVICE_SIGNALS, SUB_DEVICE_MAPS_IN,
 *                  SUB_DEVICE_MAPS_OUT, SUB_DEVICE_MAPS, or simply
 *                  SUB_DEVICE_ALL for all information.
 *  \param timeout  The length in seconds for this subscription. If set to -1,
 *                  the monitor will automatically renew the subscription until
 *                  it is freed or this function is called again. */
void mmon_subscribe(mapper_monitor mon, mapper_device dev, int flags,
                    int timeout);

/*! Unsubscribe from information about a specific device.
 *  \param mon      The monitor to use.
 *  \param dev      The device of interest. If NULL the monitor will unsubscribe
 *                  from all devices. */
void mmon_unsubscribe(mapper_monitor mon, mapper_device dev);

/*! Interface to add a map between a set of signals.
 *  \param mon          The Monitor to use.
 *  \param num_sources  The number of source signals in this map.
 *  \param sources      Array of source signal data structures.
 *  \param destination  Destination signal data structure.
 *  \return             A map data structure – either loaded from the db
 *                      (if the map already existed) or newly created. In the
 *                      latter case the map will not take effect until it has
 *                      been added to the network using mmon_update_map(). */
mapper_map mmon_add_map(mapper_monitor mon, int num_sources,
                        mapper_signal *sources, mapper_signal destination);

/*! Interface to establish or modify a map between a set of signals.
 *  \param mon          The monitor to use for sending the message.
 *  \param map          A modified data structure specifying the map properties.
 *  \return             A pointer to the established map data structure if it
 *                      responds within the timeout period, or 0 othersise. */
void mmon_update_map(mapper_monitor mon, mapper_map map);

/*! Interface to remove a map between a set of signals.
 *  \param mon          The monitor to use for sending the message.
 *  \param map          Map data structure. */
void mmon_remove_map(mapper_monitor mon, mapper_map map);

/*! Interface to send an arbitrary OSC message to the administrative bus.
 *  \param mon      The monitor to use for sending the message.
 *  \param path     The path for the OSC message.
 *  \param types    A string specifying the types of subsequent arguments.
 *  \param ...      A list of arguments. */
void mmon_send(mapper_monitor mon, const char *path, const char *types, ...);

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
 *  \param mon          The device whose time we are asking for.
 *  \param tt           A previously allocated timetag to initialize. */
void mmon_now(mapper_monitor mon, mapper_timetag_t *tt);

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
void mapper_timetag_cpy(mapper_timetag_t *ttl, mapper_timetag_t ttr);

/* @} */

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_H__
