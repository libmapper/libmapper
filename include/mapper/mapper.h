#ifndef __MAPPER_H__
#define __MAPPER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <mapper/mapper_db.h>
#include <mapper/mapper_types.h>

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
       properties.  Signals can be mapped between linked devices
       anywhere on the network by creating connections through a
       GUI. */

struct _mapper_signal;
typedef struct _mapper_signal *mapper_signal;

/*! A signal handler function can be called whenever a signal value
 *  changes. */
typedef void mapper_signal_handler(struct _mapper_signal *msig, void *v);

/*! Set or remove the minimum of a signal.
 *  \param sig      The signal to operate on.
 *  \param minimum  Must be the same type as the signal, or 0 to remove
 *                  the minimum. */
void msig_set_minimum(mapper_signal sig, void *minimum);

/*! Set or remove the maximum of a signal.
 *  \param sig      The signal to operate on.
 *  \param maximum  Must be the same type as the signal, or 0 to remove
 *                  the maximum. */
void msig_set_maximum(mapper_signal sig, void *maximum);

/*! Get a signal's property structure.
 *  \param sig  The signal to operate on.
 *  \return     A structure containing the signal's properties. */
mapper_db_signal msig_properties(mapper_signal sig);

/*! Set a property of a signal.  Can be used to provide arbitrary
 *  metadata.  Value pointed to will be copied.
 *  \param sig       The signal to operate on.
 *  \param property  The name of the property to add.
 *  \param type      The property OSC type.
 *  \param value     The property OSC value. */
void msig_set_property(mapper_signal sig, const char *property,
                       lo_type type, lo_arg *value);

/*! Remove a property of a signal.
 *  \param sig       The signal to operate on.
 *  \param property  The name of the property to remove. */
void msig_remove_property(mapper_signal sig, const char *property);

/*! Update the value of a scalar signal of type int.
 *  This is a scalar equivalent to msig_update(), for when passing by
 *  value is more convenient than passing a pointer.
 *  The signal will be routed according to external requests.
 *  \param sig The signal to update.
 *  \param value A new scalar value for this signal. */
void msig_update_int(mapper_signal sig, int value);

/*! Update the value of a scalar signal of type float.
 *  This is a scalar equivalent to msig_update(), for when passing by
 *  value is more convenient than passing a pointer.
 *  The signal will be routed according to external requests.
 *  \param sig The signal to update.
 *  \param value A new scalar value for this signal. */
void msig_update_float(mapper_signal sig, float value);

/*! Update the value of a signal.
 *  The signal will be routed according to external requests.
 *  \param sig The signal to update.
 *  \param value A pointer to a new value for this signal.  If the
 *         signal type is 'i', this should be int*; if the signal type
 *         is 'f', this should be float*.  It should be an array at
 *         least as long as the signal's length property. */
void msig_update(mapper_signal sig, void *value);

/*! Get the full OSC name of a signal, including device name
 *  prefix.
 *  \param sig The signal value to query.
 *  \param name A string to accept the name.
 *  \param len The length of string pointed to by name.
 *  \return The number of characters used, or 0 if error.  Note that
 *          in some cases the name may not be available. */
int msig_full_name(mapper_signal sig, char *name, int len);

/*! Query the values of any signals connected via mapping connections. 
 *  \param sig      A local output signal. We will be querying the remote 
 *                  ends of this signal's mapping connections.
 *  \param receiver A local input signal for receiving query responses. */
int msig_query_remote(mapper_signal sig, void *receiver);

/* @} */

/*** Devices ***/

/*! @defgroup devices Devices

    @{ A device is an entity on the network which has input and/or
       output signals.  The mapper_device is the primary interface
       through which a program uses libmapper.  A device must have a
       name, to which a unique ordinal is subsequently appended.  It
       can also be given other user-specified metadata.  Devices must
       be linked before their signals can be connected, which is
       accomplished by requests from an external GUI. */

/*! Allocate and initialize a mapper device.
 * \param name_prefix   A short descriptive string to identify the device.
 * \param initial_port  An initial port to use to receive data, or 0.
 *                      Subsequently, a unique port will be selected.

 * \param admin         A previously allocated admin to use.  If 0, an
 *                      admin will be allocated for use with this device.
 * \return              A newly allocated mapper device.  Should be free
 *                      using mdev_free(). */
mapper_device mdev_new(const char *name_prefix, int initial_port,
                       mapper_admin admin);

//! Free resources used by a mapper device.
void mdev_free(mapper_device dev);

/*! Add an input signal to a mapper device.  Values and strings
 *  pointed to by this call (except value and user_data) will be
 *  copied.  For minimum, maximum, and value, actual type must
 *  correspond to 'type'.  If type='i', then int*; if type='f', then
 *  float*.
 *  \param dev The device to add a signal to.
 *  \param name The name of the signal.
 *  \param length The length of the signal vector, or 1 for a scalar.
 *  \param type The type fo the signal value.
 *  \param unit The unit of the signal, or 0 for none.
 *  \param minimum Pointer to a minimum value, or 0 for none.
 *  \param maximum Pointer to a maximum value, or 0 for none.
 *  \param handler Function to be called when the value of the
 *                 signal is updated.
 *  \param user_data User context pointer to be passed to handler. */
mapper_signal mdev_add_input(mapper_device dev, const char *name,
                             int length, char type, const char *unit,
                             void *minimum, void *maximum,
                             mapper_signal_handler *handler,
                             void *user_data);

/*! Add a hidden input to a mapper device. Hidden inputs can receive
 *  signals but are not reported on the admin bus; they are intended
 *  as local receivers for remote value queries. Values and strings
 *  pointed to by this call (except value and user_data) will be
 *  copied.  For minimum, maximum, and value, actual type must
 *  correspond to 'type'.  If type='i', then int*; if type='f', then
 *  float*.
 *  \param dev The device to add a signal to.
 *  \param name The name of the signal.
 *  \param length The length of the signal vector, or 1 for a scalar.
 *  \param type The type fo the signal value.
 *  \param unit The unit of the signal, or 0 for none.
 *  \param minimum Pointer to a minimum value, or 0 for none.
 *  \param maximum Pointer to a maximum value, or 0 for none.
 *  \param handler Function to be called when the value of the
 *                 signal is updated.
 *  \param user_data User context pointer to be passed to handler. */
mapper_signal mdev_add_hidden_input(mapper_device dev, const char *name,
                                    int length, char type, const char *unit,
                                    void *minimum, void *maximum,
                                    mapper_signal_handler *handler,
                                    void *user_data);

/*! Add an output signal to a mapper device.  Values and strings
 *  pointed to by this call (except value and user_data) will be
 *  copied.  For minimum, maximum, and value, actual type must
 *  correspond to 'type'.  If type='i', then int*; if type='f', then
 *  float*.
 *  \param dev The device to add a signal to.
 *  \param name The name of the signal.
 *  \param length The length of the signal vector, or 1 for a scalar.
 *  \param type The type fo the signal value.
 *  \param unit The unit of the signal, or 0 for none.
 *  \param minimum Pointer to a minimum value, or 0 for none.
 *  \param maximum Pointer to a maximum value, or 0 for none. */
mapper_signal mdev_add_output(mapper_device dev, const char *name,
                              int length, char type, const char *unit,
                              void *minimum, void *maximum);

/* Remove a device's input signal.
 * \param dev The device to remove a signal from.
 * \param sig The signal to remove. */ 
void mdev_remove_input(mapper_device dev, mapper_signal sig);

/* Remove a device's output signal.
 * \param dev The device to remove a signal from.
 * \param sig The signal to remove. */
void mdev_remove_output(mapper_device dev, mapper_signal sig);

//! Return the number of inputs.
int mdev_num_inputs(mapper_device dev);

//! Return the number of outputs.
int mdev_num_outputs(mapper_device dev);

/*! Get an input signal with a given name.
 *  \param dev Device to search in.
 *  \param name Name of input signal to search for. It may optionally
 *              begin with a '/' character.
 *  \param index Optional place to receive the index of the matching signal.
 *  \return Pointer to the mapper_signal describing the signal, or zero
 *          if not found.
 */
mapper_signal mdev_get_input_by_name(mapper_device dev, const char *name,
                                     int *index);

/*! Get an output signal with a given name.
 *  \param dev Device to search in.
 *  \param name Name of output signal to search for. It may optionally
 *              begin with a '/' character.
 *  \param index Optional place to receive the index of the matching signal.
 *  \return Pointer to the mapper_signal describing the signal, or zero
 *          if not found.
 */
mapper_signal mdev_get_output_by_name(mapper_device dev, const char *name,
                                      int *index);

/* Get an input signal with the given index.
 * \param dev The device to search in.
 * \param index Index of the signal to retrieve.
 * \return Pointer to the mapper_signal describing the signal, or zero
 *         if not found. */
mapper_signal mdev_get_input_by_index(mapper_device dev, int index);

/* Get an output signal with the given index.
 * \param dev The device to search in.
 * \param index Index of the signal to retrieve.
 * \return Pointer to the mapper_signal describing the signal, or zero
 *         if not found. */
mapper_signal mdev_get_output_by_index(mapper_device dev, int index);

/*! Set a property of a device.  Can be used to provide arbitrary
 *  metadata.  Value pointed to will be copied.
 *  \param dev       The device to operate on.
 *  \param property  The name of the property to add.
 *  \param type      The property OSC type.
 *  \param value     The property OSC value. */
void mdev_set_property(mapper_device dev, const char *property,
                       lo_type type, lo_arg *value);

/*! Remove a property of a device.
 *  \param dev       The device to operate on.
 *  \param property  The name of the property to remove. */
void mdev_remove_property(mapper_device dev, const char *property);

/*! Poll this device for new messages.
 *  Note, if you have multiple devices, the right thing to do is call
 *  this function for each of them with block_ms=0, and add your own
 *  sleep if necessary.
 *  \param dev The device to check messages for.
 *  \param block_ms Number of milliseconds to block waiting for
 *                  messages, or 0 for non-blocking behaviour.
 *  \return The number of handled messages. May be zero if there was
 *          nothing to do. */
int mdev_poll(mapper_device dev, int block_ms);

/*! Send the current value of a signal.
 *  This is called by msig_update(), so use that to change the value
 *  of a signal rather than calling this function directly.
 *  \param dev The device containing the signal to send.
 *  \param sig The signal to send.
 *  \return zero if the signal was sent, non-zero otherwise. */
int mdev_send_signal(mapper_device dev, mapper_signal sig);

/*! Detect whether a device is completely initialized.
 *  \return Non-zero if device is completely initialized, i.e., has an
 *  allocated receiving port and unique network name.  Zero
 *  otherwise. */
int mdev_ready(mapper_device dev);

/*! Return a string indicating the device's full name, if it is
 *  registered.  The returned string must not be externally modified.
 *  \param dev The device to query.
 *  \return String containing the device's full name, or zero if it is
 *  not available. */
const char *mdev_name(mapper_device dev);

/*! Return the port used by a device to receive signals, if available.
 *  \param dev The device to query.
 *  \return An integer indicating the device's port, or zero if it is
 *  not available. */
unsigned int mdev_port(mapper_device dev);

/*! Return the IPv4 address used by a device to receive signals, if available.
 *  \param dev The device to query.
 *  \return A pointer to an in_addr struct indicating the device's IP
 *          address, or zero if it is not available.  In general this
 *          will be the IPv4 address associated with the selected
 *          local network interface.
 */
const struct in_addr *mdev_ip4(mapper_device dev);

/*! Return a string indicating the name of the network interface this
 *  device is listening on.
 *  \param dev The device to query.
 *  \return A string containing the name of the network interface. */
const char *mdev_interface(mapper_device dev);

/*! Return an allocated ordinal which is appended to this device's
 *  network name.  In general the results of this function are not
 *  valid unless mdev_ready() returns non-zero.
 *  \param dev The device to query.
 *  \return A positive ordinal unique to this device (per name). */
unsigned int mdev_ordinal(mapper_device dev);

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
mapper_admin mapper_admin_new(const char *iface, const char *ip, int port);

/*! Free an admin created with mapper_admin_new(). */
void mapper_admin_free(mapper_admin admin);

/* @} */

/**** Device database ****/

/*! The set of possible actions on a database record, used
 *  to inform callbacks of what is happening to a record. */
typedef enum {
    MDB_MODIFY,
    MDB_NEW,
    MDB_REMOVE,
} mapper_db_action_t;

/***** Devices *****/

/*! @defgroup devicedb Device database

    @{ A monitor may query information about devices on the network
       through this interface. */

/*! A callback function prototype for when a device record is added or
 *  updated in the database. Such a function is passed in to
 *  mapper_db_add_device_callback().
 *  \param record  Pointer to the device record.
 *  \param action  A value of mapper_db_action_t indicating what
 *                 is happening to the database record.
 *  \param user    The user context pointer registered with this
 *                 callback. */
typedef void device_callback_func(mapper_db_device record,
                                  mapper_db_action_t action,
                                  void *user);

/*! Register a callback for when a device record is added or updated
 *  in the database.
 *  \param cb   Callback function.
 *  \param user A user-defined pointer to be passed to the callback
 *              for context . */
void mapper_db_add_device_callback(mapper_db db,
                                   device_callback_func *f, void *user);

/*! Remove a device record callback from the database service.
 *  \param cb   Callback function.
 *  \param user The user context pointer that was originally specified
 *              when adding the callback. */
void mapper_db_remove_device_callback(mapper_db db,
                                      device_callback_func *f, void *user);

/*! Return the whole list of devices.
 *  \return A double-pointer to the first item in the list of devices,
 *          or zero if none.  Use mapper_db_device_next() to
 *          iterate. */
mapper_db_device_t **mapper_db_get_all_devices(mapper_db db);

/*! Find information for a registered device.
 *  \param name  Name of the device to find in the database.
 *  \return      Information about the device, or zero if not found. */
mapper_db_device mapper_db_get_device_by_name(mapper_db db,
                                              const char *device_name);

/*! Return the list of devices with a substring in their name.
 *  \param str The substring to search for.
 *  \return    A double-pointer to the first item in a list of matching
 *             devices.  Use mapper_db_device_next() to iterate. */
mapper_db_device_t **mapper_db_match_devices_by_name(mapper_db db,
                                                     const char *device_pattern);

/*! Given a device record pointer returned from a previous
 *  mapper_db_return_*() call, get the next item in the list.
 *  \param  The previous device record pointer.
 *  \return A double-pointer to the next device record in the list, or
 *          zero if no more devices. */
mapper_db_device_t **mapper_db_device_next(mapper_db_device_t**);

/*! Given a device record pointer returned from a previous
 *  mapper_db_get_*() call, indicate that we are done iterating.
 *  \param The previous device record pointer. */
void mapper_db_device_done(mapper_db_device_t **);


/*! Look up a device property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param dev The device to look at.
 *  \param index Numerical index of a device property.
 *  \param property Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param type Address of a lo_type to receive the property value
 *              type.
 *  \param value Address of a lo_arg* to receive the property value.
 *  \return Zero if found, otherwise non-zero.
 *  @ingroup devicedb */
int mapper_db_device_property_index(mapper_db_device dev, unsigned int index,
                                    const char **property, lo_type *type,
                                    const lo_arg **value);

/*! Look up a device property by name.
 *  \param dev The device to look at.
 *  \param property The name of the property to retrive.
 *  \param type A pointer to a location to receive the type of the
 *              property value. (Required.)
 *  \param value A pointer a location to receive the address of the
 *               property's value. (Required.)
 *  \return Zero if found, otherwise non-zero.
 *  @ingroup devicedb */
int mapper_db_device_property_lookup(mapper_db_device dev,
                                     const char *property,
                                     lo_type *type,
                                     const lo_arg **value);

/* @} */

/***** Signals *****/

/*! @defgroup signaldb Signal database

    @{ A monitor may query information about signals on the network
       through this interface. It is also used by local signals to
       store property information. */

/*! A callback function prototype for when a signal record is added or
 *  updated in the database. Such a function is passed in to
 *  mapper_db_add_signal_callback().
 *  \param record  Pointer to the signal record.
 *  \param action  A value of mapper_db_action_t indicating what
 *                 is happening to the database record.
 *  \param user    The user context pointer registered with this
 *                 callback. */
typedef void signal_callback_func(mapper_db_signal record,
                                  mapper_db_action_t action,
                                  void *user);

/*! Register a callback for when a signal record is added or updated
 *  in the database.
 *  \param cb   Callback function.
 *  \param user A user-defined pointer to be passed to the callback
 *              for context . */
void mapper_db_add_signal_callback(mapper_db db,
                                   signal_callback_func *f, void *user);

/*! Remove a signal record callback from the database service.
 *  \param cb   Callback function.
 *  \param user The user context pointer that was originally specified
 *              when adding the callback. */
void mapper_db_remove_signal_callback(mapper_db db,
                                      signal_callback_func *f, void *user);

/*! Return the list of all known inputs across all devices.
 *  \return A double-pointer to the first item in the list of results
 *          or zero if none.  Use mapper_db_signal_next() to iterate. */
mapper_db_signal_t **mapper_db_get_all_inputs(mapper_db db);

/*! Return the list of all known outputs across all devices.
 *  \return A double-pointer to the first item in the list of results
 *          or zero if none.  Use mapper_db_signal_next() to iterate. */ 
mapper_db_signal_t **mapper_db_get_all_outputs(mapper_db db);

/*! Return the list of inputs for a given device.
 *  \param dev_name Name of the device to match for outputs.  Must
 *                  be exact, including the leading '/'.
 *  \return A double-pointer to the first item in the list of input
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_get_inputs_by_device_name(
    mapper_db db, const char *device_name);

/*! Return the list of outputs for a given device.
 *  \param dev_name Name of the device to match for outputs.  Must
 *                  be exact, including the leading '/'.
 *  \return A double-pointer to the first item in the list of output
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_get_outputs_by_device_name(
    mapper_db db, const char *device_name);

/*! Return the list of inputs for a given device.
 *  \param dev_name Name of the device to match for inputs.
 *  \param input_pattern A substring to search for in the device inputs.
 *  \return A double-pointer to the first item in the list of input
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_match_inputs_by_device_name(
    mapper_db db, const char *device_name, const char *input_pattern);

/*! Return the list of outputs for a given device.
 *  \param dev_name Name of the device to match for outputs.
 *  \param output_pattern A substring to search for in the device outputs.
 *  \return A double-pointer to the first item in the list of output
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_match_outputs_by_device_name(
    mapper_db db, const char *device_name, char const *output_pattern);

/*! Given a signal record pointer returned from a previous
 *  mapper_db_get_*() call, get the next item in the list.
 *  \param  The previous signal record pointer.
 *  \return A double-pointer to the next signal record in the list, or
 *          zero if no more signals. */
mapper_db_signal_t **mapper_db_signal_next(mapper_db_signal_t**);

/*! Given a signal record pointer returned from a previous
 *  mapper_db_get_*() call, indicate that we are done iterating.
 *  \param The previous signal record pointer. */
void mapper_db_signal_done(mapper_db_signal_t **s);

/*! Look up a signal property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param sig The signal to look at.
 *  \param index Numerical index of a signal property.
 *  \param property Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param type Address of a lo_type to receive the property value type.
 *  \param value Address of a lo_arg* to receive the property value.
 *  \return Zero if found, otherwise non-zero.
 *  @ingroup signaldb */
int mapper_db_signal_property_index(mapper_db_signal sig, unsigned int index,
                                    const char **property, lo_type *type,
                                    const lo_arg **value);

/*! Look up a signal property by name.
 *  \param sig The signal to look at.
 *  \param property The name of the property to retrive.
 *  \param type A pointer to a location to receive the type of the
 *              property value. (Required.)
 *  \param value A pointer a location to receive the address of the
 *               property's value. (Required.)
 *  \return Zero if found, otherwise non-zero.
 *  @ingroup signaldb */
int mapper_db_signal_property_lookup(mapper_db_signal sig,
                                     const char *property,
                                     lo_type *type,
                                     const lo_arg **value);

/* @} */

/***** Mappings *****/

/*! @defgroup mappingdb Mappings database

    @{ A monitor may query information about mappings (connections)
       between signals on the network through this interface.  It is
       also used to specify mapping properties during connection
       requests. */

/*! A callback function prototype for when a mapping record is added or
 *  updated in the database. Such a function is passed in to
 *  mapper_db_add_mapping_callback().
 *  \param record  Pointer to the mapping record.
 *  \param action  A value of mapper_db_action_t indicating what
 *                 is happening to the database record.
 *  \param user    The user context pointer registered with this
 *                 callback. */
typedef void mapping_callback_func(mapper_db_mapping record,
                                   mapper_db_action_t action,
                                   void *user);

/*! Register a callback for when a mapping record is added or updated
 *  in the database.
 *  \param cb   Callback function.
 *  \param user A user-defined pointer to be passed to the callback
 *              for context . */
void mapper_db_add_mapping_callback(mapper_db db,
                                    mapping_callback_func *f, void *user);

/*! Remove a mapping record callback from the database service.
 *  \param cb   Callback function.
 *  \param user The user context pointer that was originally specified
 *              when adding the callback. */
void mapper_db_remove_mapping_callback(mapper_db db,
                                       mapping_callback_func *f, void *user);

/*! Return a list of all registered mappings.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_mapping_next() to iterate. */
mapper_db_mapping_t **mapper_db_get_all_mappings(mapper_db db);

/*! Return the list of mappings that touch the given device name.
 *  \param dev_name Name of the device to find.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_mapping_next() to iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_device_name(
    mapper_db db, const char *device_name);

/*! Return the list of mappings for a given input name.
 *  \param input_name Name of the input to find.
 *  \return A double-pointer to the first item in the list of results
 *          or zero if none.  Use mapper_db_mapping_next() to iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_input_name(
    mapper_db db, const char *input_name);

/*! Return the list of mappings for a given device and input name.
 *  \param dev_name Exact name of the device to find, including the
 *                  leading '/'.
 *  \param input_name Exact name of the input to find, including the
 *                    leading '/'.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_mapping_next() to iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_device_and_input_name(
    mapper_db db, const char *device_name, const char *input_name);

/*! Return the list of mappings for a given output name.
 *  \param output_name Name of the output to find.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_mapping_next() to iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_output_name(
    mapper_db db, const char *output_name);

/*! Return the list of mappings for a given device and output name.
 *  \param dev_name Exact name of the device to find, including the
 *                  leading '/'.
 *  \param output_name Exact name of the output to find, including the
 *                     leading '/'.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_mapping_next() to iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_device_and_output_name(
    mapper_db db, const char *device_name, const char *output_name);

/*! Return the list of mappings that touch any signals in the lists of
 *  inputs, outputs, and devices provided.
 *  \param input_devices Double-pointer to the first item in a list
 *                       returned from a previous database query.
 *  \param output_devices Double-pointer to the first item in a list
 *                        returned from a previous database query.
 *  \param inputs Double-pointer to the first item in a list
 *                returned from a previous database query.
 *  \param outputs Double-pointer to the first item in a list
 *                 returned from a previous database query.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_mapping_next() to iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_device_and_signal_names(
    mapper_db db,
    const char *input_device_name,  const char *input_name,
    const char *output_device_name, const char *output_name);

/*! Return the mapping that match the exact source and destination
 *  specified by their full names ("/<device>/<signal>").
 *  \param src_name  Full name of source signal.
 *  \param dest_name Full name of destination signal.
 *  \return A pointer to a structure containing information on the
 *          found mapping, or 0 if not found. */
mapper_db_mapping mapper_db_get_mapping_by_signal_full_names(
    mapper_db db, const char *src_name, const char *dest_name);

/*! Return mappings that have the specified source and destination
 *  devices.
 *  \param src_name  Name of source device.
 *  \param dest_name Name of destination device.
 *  \return A double-pointer to the first item in a list of results,
 *          or 0 if not found. */
mapper_db_mapping_t **mapper_db_get_mappings_by_src_dest_device_names(
    mapper_db db,
    const char *src_device_name, const char *dest_device_name);

/*! Return the list of mappings that touch any signals in the lists of
 *  inputs and outputs provided.
 *  \param inputs Double-pointer to the first item in a list
 *                returned from a previous database query.
 *  \param outputs Double-pointer to the first item in a list
 *                 returned from a previous database query.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_mapping_next() to iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_signal_queries(
    mapper_db db,
    mapper_db_signal_t **inputs, mapper_db_signal_t **outputs);

/*! Given a mapping record pointer returned from a previous
 *  mapper_db_get_mappings*() call, get the next item in the list.
 *  \param  The previous mapping record pointer.
 *  \return A double-pointer to the next mapping record in the list, or
 *          zero if no more mappings. */
mapper_db_mapping_t **mapper_db_mapping_next(mapper_db_mapping_t**);

/*! Given a mapping record pointer returned from a previous
 *  mapper_db_get_*() call, indicate that we are done iterating.
 *  \param The previous mapping record pointer. */
void mapper_db_mapping_done(mapper_db_mapping_t **);

/* @} */

/***** Links *****/

/*! @defgroup linkdb Links database
 
    @{ A monitor may query information about links betweend devices on
       the network through this interface. */

/*! A callback function prototype for when a link record is added or
 *  updated in the database. Such a function is passed in to
 *  mapper_db_add_link_callback().
 *  \param record  Pointer to the link record.
 *  \param action  A value of mapper_db_action_t indicating what
 *                 is happening to the database record.
 *  \param user    The user context pointer registered with this
 *                 callback. */
typedef void link_callback_func(mapper_db_link record,
                                mapper_db_action_t action,
                                void *user);

/*! Register a callback for when a link record is added or updated
 *  in the database.
 *  \param db   The database to query.
 *  \param cb   Callback function.
 *  \param user A user-defined pointer to be passed to the callback
 *              for context . */
void mapper_db_add_link_callback(mapper_db db,
                                 link_callback_func *f, void *user);

/*! Remove a link record callback from the database service.
 *  \param db   The database to query.
 *  \param cb   Callback function.
 *  \param user The user context pointer that was originally specified
 *              when adding the callback. */
void mapper_db_remove_link_callback(mapper_db db,
                                    link_callback_func *f, void *user);

/*! Return the whole list of links.
 *  \param db The database to query.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_link_next() to iterate. */
mapper_db_link_t **mapper_db_get_all_links(mapper_db db);

/*! Return the list of links that touch the given device name.
 *  \param db The database to query.
 *  \param dev_name Name of the device to find.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_link_next() to iterate. */
mapper_db_link_t **mapper_db_get_links_by_device_name(
    mapper_db db, const char *dev_name);

/*! Return the list of links for a given source name.
 *  \param db The database to query.
 *  \param src_device_name Name of the source device to find.
 *  \return A double-pointer to the first item in the list of source
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_link_t **mapper_db_get_links_by_src_device_name(
    mapper_db db, const char *src_device_name);

/*! Return the list of links for a given destination name.
 *  \param db The database to query.
 *  \param dest_device_name Name of the destination device to find.
 *  \return A double-pointer to the first item in the list of destination
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_link_t **mapper_db_get_links_by_dest_device_name(
    mapper_db db, const char *dest_device_name);

/*! Return the link structure associated with the exact devices specified.
 *  \param db The database to query.
 *  \param src_device_name Name of the source device to find.
 *  \param dest_device_name Name of the destination device to find.
 *  \return A structure containing information on the link, or 0 if
 *          not found. */
mapper_db_link mapper_db_get_link_by_src_dest_names(mapper_db db,
    const char *src_device_name, const char *dest_device_name);

/*! Return the list of links for a given source name.
 *  \param db The database to query.
 *  \param src_device_name Name of the source device to find.
 *  \return A double-pointer to the first item in the list of source
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_link_t **mapper_db_get_links_by_src_dest_devices(
    mapper_db db,
    mapper_db_device_t **src_device_list,
    mapper_db_device_t **dest_device_list);

/*! Given a link record double-pointer returned from a previous
 *  mapper_db_get_links*() call, get the next item in the list.
 *  \param  The previous link record double-pointer.
 *  \return A double-pointer to the next link record in the list, or
 *          zero if no more links. */
mapper_db_link_t **mapper_db_link_next(mapper_db_link_t**);

/*! Given a link record double-pointer returned from a previous
 *  mapper_db_get_*() call, indicate that we are done iterating.
 *  \param The previous link record double-pointer. */
void mapper_db_link_done(mapper_db_link_t **s);

/* @} */

/***** Monitors *****/

/*! @defgroup monitor Monitors

    @{ Monitors are the primary interface through which a program may
       observe the network and store information about devices and
       signals that are present.  Each monitor has a database of
       devices, signals, mappings, and links, which can be queried.  A
       monitor can also make link and connection requests.  In
       general, the monitor interface is useful for building GUI
       applications to control the network. */

/*! Create a network monitor. */
mapper_monitor mapper_monitor_new(void);

/*! Free a network monitor. */
void mapper_monitor_free(mapper_monitor mon);

/*! Poll a network monitor.
 *  \param mon The monitor to poll.
 *  \param block_ms The number of milliseconds to block, or 0 for
 *                  non-blocking behaviour.
 *  \return The number of handled messages. */
int mapper_monitor_poll(mapper_monitor mon, int block_ms);

/*! Get the database associated with a monitor. This can be used as
 *  long as the monitor remains alive. */
mapper_db mapper_monitor_get_db(mapper_monitor mon);

/*! Request that all devices report in. */
int mapper_monitor_request_devices(mapper_monitor mon);

/*! Request signals for specific device. */
int mapper_monitor_request_signals_by_name(
    mapper_monitor mon, const char* name);

/*! Request links for specific device. */
int mapper_monitor_request_links_by_name(
    mapper_monitor mon, const char* name);

/*! Request mappings for specific device. */
int mapper_monitor_request_mappings_by_name(
    mapper_monitor mon, const char* name);

/*! When auto-request is enabled (enable=1), the monitor automatically
 *  makes requests for information on signals, links, and mappings
 *  when it encounters a previously-unseen device. By default,
 *  auto-request is enabled for monitors. */
void mapper_monitor_autorequest(mapper_monitor mon, int enable);

/*! Interface to add a link between two devices.
 *  \param mon The monitor to use for sending the message.
 *  \param source_device_path Source device name.
 *  \param dest_device_path   Destination device name. */
void mapper_monitor_link(mapper_monitor mon,
                         const char* source_device, 
                         const char* dest_device);

/*! Interface to remove a link between two devices.
 *  \param mon The monitor to use for sending the message.
 *  \param source_device Source device name.
 *  \param dest_device   Destination device name. */
void mapper_monitor_unlink(mapper_monitor mon,
                           const char* source_device, 
                           const char* dest_device);

/*! Interface to modify a mapping between two signals.
 *  \param mon The monitor to use for sending the message.
 *  \param source_signal Source signal name.
 *  \param dest_signal   Destination signal name.
 *  \param properties An optional data structure specifying the
 *                    requested properties of this mapping.
 *  \param property_flags Bit flags indicating which properties in the
 *                        provided mapper_db_mapping_t should be
 *                        applied to the new connection. See the flags
 *                        prefixed by MAPPING_ in mapper_db.h. */
void mapper_monitor_mapping_modify(mapper_monitor mon,
                            mapper_db_mapping_t *properties,
                            unsigned int property_flags);

/*! Interface to add a mapping between two signals.
 *  \param mon The monitor to use for sending the message.
 *  \param source_signal Source signal name.
 *  \param dest_signal   Destination signal name.
 *  \param properties An optional data structure specifying the
 *                    requested properties of this mapping.
 *  \param property_flags Bit flags indicating which properties in the
 *                        provided mapper_db_mapping_t should be
 *                        applied to the new connection. See the flags
 *                        prefixed by MAPPING_ in mapper_db.h. */
void mapper_monitor_connect(mapper_monitor mon,
                            const char* source_signal,
                            const char* dest_signal,
                            mapper_db_mapping_t *properties,
                            unsigned int property_flags);

/*! Interface to remove a mapping between two signals.
 *  \param mon The monitor to use for sending the message.
 *  \param source_signal Source signal name.
 *  \param dest_signal   Destination signal name. */
void mapper_monitor_disconnect(mapper_monitor mon,
                               const char* source_signal, 
                               const char* dest_signal);

/* @} */

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_H__
