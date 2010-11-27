#ifndef __MAPPER_H__
#define __MAPPER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <mapper/mapper_db.h>
#include <mapper/mapper_types.h>

/*** Signals ***/

/*! @defgroup signals Signals
    @{ */

struct _mapper_signal;
typedef struct _mapper_signal *mapper_signal;

/*! A signal handler function can be called whenever a signal value
 *  changes. */
typedef void mapper_signal_handler(struct _mapper_signal *msig,
                                   mapper_signal_value_t *v);

/*! Macro to take a float or int value and cast it to a
 *  mapper_signal_value_t. */
#define MSIGVAL(f) (*(mapper_signal_value_t*)&f)

/*! Macro to take a float or int pointer and cast it to a
 *  mapper_signal_value_t pointer. */
#define MSIGVALP(f) ((mapper_signal_value_t*)f)

/*! Set or remove the minimum of a signal.
 *  \param sig      The signal to operate on.
 *  \param minimum  Must be the same type as the signal, or 0 to remove
 *                  the minimum. */
void msig_set_minimum(mapper_signal sig, mapper_signal_value_t *minimum);

/*! Set or remove the maximum of a signal.
 *  \param sig      The signal to operate on.
 *  \param maximum  Must be the same type as the signal, or 0 to remove
 *                  the maximum. */
void msig_set_maximum(mapper_signal sig, mapper_signal_value_t *maximum);

/*! Get a signal's property structure.
 *  \param sig  The signal to operate on.
 *  \return     A structure containing the signal's properties. */
mapper_db_signal msig_get_properties(mapper_signal sig);

/*! Set a property of a signal.  Can be used to provide arbitrary
 *  metadata.  Value pointed to will be copied.
 *  \param sig       The signal to operate on.
 *  \param property  The name of the property to add.
 *  \param type      The property OSC type.
 *  \param value     The property OSC value. */
void msig_set_property(mapper_signal sig, const char *property,
                       char type, lo_arg *value);

/*! Remove a property of a signal.
 *  \param sig       The signal to operate on.
 *  \param property  The name of the property to remove. */
void msig_remove_property(mapper_signal sig, const char *property);

/*! Update the value of a signal.
 *  This is a scalar equivalent to msig_update(), for when passing by
 *  value is more convenient than passing a pointer.
 *  The signal will be routed according to external requests.
 *  \param sig The signal to update.
 *  \param value A new scalar value for this signal. */
void msig_update_scalar(mapper_signal sig, mapper_signal_value_t value);

/*! Update the value of a signal.
 *  The signal will be routed according to external requests.
 *  \param sig The signal to update.
 *  \param value A pointer to a new value for this signal. */
void msig_update(mapper_signal sig, mapper_signal_value_t *value);

/*! Get the full OSC name of a signal, including device name
 *  prefix.
 *  \param sig The signal value to query.
 *  \param name A string to accept the name.
 *  \param len The length of string pointed to by name.
 *  \return The number of characters used, or 0 if error.  Note that
 *          in some cases the name may not be available. */
int msig_full_name(mapper_signal sig, char *name, int len);

/* @} */

/*** Devices ***/

/*! @defgroup devices Devices
    @{ */

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
void mdev_free(mapper_device device);

//! Add an input signal to a mapper device.
mapper_signal mdev_add_input(mapper_device md, int length, const char *name,
                             const char *unit, char type,
                             mapper_signal_value_t *minimum,
                             mapper_signal_value_t *maximum,
                             mapper_signal_value_t *value,
                             mapper_signal_handler *handler,
                             void *user_data);
    
//! Add an integer (scalar) input signal to a mapper device.
mapper_signal mdev_add_int_input(mapper_device md, const char *name, 
                                 const char *unit, int *minimum, 
                                 int *maximum, int *value,
                                 mapper_signal_handler *handler, 
                                 void *user_data);
    
//! Add a float (scalar) input signal to a mapper device.
mapper_signal mdev_add_float_input(mapper_device md, const char *name, 
                                   const char *unit, float *minimum, 
                                   float *maximum, float *value,
                                   mapper_signal_handler *handler, 
                                   void *user_data);

//! Add an output signal to a mapper device.
mapper_signal mdev_add_output(mapper_device md, int length, const char *name,
                              const char *unit, char type,
                              mapper_signal_value_t *minimum,
                              mapper_signal_value_t *maximum,
                              mapper_signal_value_t *value,
                              mapper_signal_handler *handler,
                              void *user_data);
    
//! Add an integer (scalar) output signal to a mapper device.
mapper_signal mdev_add_int_output(mapper_device md, const char *name, 
                                  const char *unit, int *minimum, 
                                  int *maximum, int *value,
                                  mapper_signal_handler *handler, 
                                  void *user_data);
    
//! Add a float (scalar) output signal to a mapper device.
mapper_signal mdev_add_float_output(mapper_device md, const char *name, 
                                    const char *unit, float *minimum, 
                                    float *maximum, float *value,
                                    mapper_signal_handler *handler, 
                                    void *user_data);

//! Return the number of inputs.
int mdev_num_inputs(mapper_device device);

//! Return the number of outputs.
int mdev_num_outputs(mapper_device device);

/*! Find an input signal by name.
 *  \param md Device to search in.
 *  \param name Name of output signal to search for. It may optionally
 *              begin with a '/' character.
 *  \param result Optional place to get the matching mapper_signal
 *                pointer. It will have a value of 0 if not found.
 *  \return Index of the signal in the device's input signal array, or
 *          -1 if not found.
 */
int mdev_find_input_by_name(mapper_device md, const char *name,
                            mapper_signal *result);

/*! Find an output signal by name.
 *  \param md Device to search in.
 *  \param name Name of output signal to search for. It may optionally
 *              begin with a '/' character.
 *  \param result Optional place to get the matching mapper_signal
 *                pointer. It will have a value of 0 if not found.
 *  \return Index of the signal in the device's output signal array,
 *          or -1 if not found.
 */
int mdev_find_output_by_name(mapper_device md, const char *name,
                             mapper_signal *result);

/*! Set a property of a device.  Can be used to provide arbitrary
 *  metadata.  Value pointed to will be copied.
 *  \param dev       The device to operate on.
 *  \param property  The name of the property to add.
 *  \param type      The property OSC type.
 *  \param value     The property OSC value. */
void mdev_set_property(mapper_device dev, const char *property,
                       char type, lo_arg *value);

/*! Remove a property of a device.
 *  \param dev       The device to operate on.
 *  \param property  The name of the property to remove. */
void mdev_remove_property(mapper_device dev, const char *property);

/*! Poll this device for new messages.
 *  Note, if you have multiple devices, the right thing to do is call
 *  this function for each of them with block_ms=0, and add your own
 *  sleep if necessary.
 *  \param md The device to check messages for.
 *  \param block_ms Number of milliseconds to block waiting for
 *                  messages, or 0 for non-blocking behaviour.
 *  \return The number of handled messages. May be zero if there was
 *          nothing to do. */
int mdev_poll(mapper_device md, int block_ms);

/*! Send the current value of a signal.
 *  This is called by msig_update(), so use that to change the value
 *  of a signal rather than calling this function directly.
 *  \param device The device containing the signal to send.
 *  \param sig The signal to send.
 *  \return zero if the signal was sent, non-zero otherwise. */
int mdev_send_signal(mapper_device device, mapper_signal sig);

/*! Detect whether a device is completely initialized.
 *  \return Non-zero if device is completely initialized, i.e., has an
 *  allocated receiving port and unique network name.  Zero
 *  otherwise. */
int mdev_ready(mapper_device device);

/*! Return a string indicating the device's full name, if it is
 *  registered.  The returned string must not be externally modified.
 *  \param device The device to query.
 *  \return String containing the device's full name, or zero if it is
 *  not available. */
const char *mdev_name(mapper_device device);

/*! Return the port used by a device to receive signals, if available.
 *  \param device The device to query.
 *  \return An integer indicating the device's port, or zero if it is
 *  not available. */
unsigned int mdev_port(mapper_device device);

/* @} */

/*** Admins ***/

/*! @defgroup admins Admins
    @{ */

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
    @{ */

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
mapper_db_device_t **mapper_db_match_device_by_name(mapper_db db,
                                                    char *device_pattern);

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

/* @} */

/***** Signals *****/

/*! @defgroup signaldb Signal database
    @{ */

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
 *  \param device_name Name of the device to match for outputs.  Must
 *                     be exact, including the leading '/'.
 *  \return A double-pointer to the first item in the list of input
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_get_inputs_by_device_name(
    mapper_db db, const char *device_name);

/*! Return the list of outputs for a given device.
 *  \param device_name Name of the device to match for outputs.  Must
 *                     be exact, including the leading '/'.
 *  \return A double-pointer to the first item in the list of output
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_get_outputs_by_device_name(
    mapper_db db, const char *device_name);

/*! Return the list of inputs for a given device.
 *  \param device_name Name of the device to match for inputs.
 *  \param input_pattern A substring to search for in the device inputs.
 *  \return A double-pointer to the first item in the list of input
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_match_inputs_by_device_name(
    mapper_db db, const char *device_name, const char *input_pattern);

/*! Return the list of outputs for a given device.
 *  \param device_name Name of the device to match for outputs.
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

/* @} */

/***** Mappings *****/

/*! @defgroup mappingdb Mappings database
    @{ */

/*! Interface to add a mapping between two signals.
 *  \param source_signal_path Source signal name (OSC path).
 *  \param dest_signal_path   Destination signal name (OSC path). */
void mapper_db_create_mapping( const char* source_signal_path, 
		const char* dest_signal_path );

/*! Interface to remove a mapping between two signals.
 *  \param source_signal_path Source signal name (OSC path).
 *  \param dest_signal_path   Destination signal name (OSC path). */
void mapper_db_destroy_mapping( const char* source_signal_path, 
		const char* dest_signal_path );

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
 *  \param device_name Name of the device to find.
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
 *  \param device_name Exact name of the device to find, including the
 *                    leading '/'.
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
 *  \param device_name Exact name of the device to find, including the
 *                     leading '/'.
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
    @{ */

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
 *  \param cb   Callback function.
 *  \param user A user-defined pointer to be passed to the callback
 *              for context . */
void mapper_db_add_link_callback(mapper_db db,
                                 link_callback_func *f, void *user);

/*! Remove a link record callback from the database service.
 *  \param cb   Callback function.
 *  \param user The user context pointer that was originally specified
 *              when adding the callback. */
void mapper_db_remove_link_callback(mapper_db db,
                                    link_callback_func *f, void *user);

/*! Return the whole list of links.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_link_next() to iterate. */
mapper_db_link_t **mapper_db_get_all_links(mapper_db db);

/*! Return the list of links that touch the given device name.
 *  \param device_name Name of the device to find.
 *  \return A double-pointer to the first item in the list of results,
 *          or zero if none.  Use mapper_db_link_next() to iterate. */
mapper_db_link_t **mapper_db_get_links_by_device_name(
    mapper_db db, const char *device_name);

/*! Return the list of links for a given source name.
 *  \param src_device_name Name of the source device to find.
 *  \return A double-pointer to the first item in the list of source
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_link_t **mapper_db_get_links_by_src_device_name(
    mapper_db db, const char *src_device_name);

/*! Return the list of links for a given destination name.
 *  \param dest_device_name Name of the destination device to find.
 *  \return A double-pointer to the first item in the list of destination
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_link_t **mapper_db_get_links_by_dest_device_name(
    mapper_db db, const char *dest_device_name);

/*! Return the link structure associated with the exact devices specified.
 *  \param src_device_name Name of the source device to find.
 *  \param dest_device_name Name of the destination device to find.
 *  \return A structure containing information on the link, or 0 if
 *          not found. */
mapper_db_link mapper_db_get_link_by_src_dest_names(mapper_db db,
    const char *src_device_name, const char *dest_device_name);

/*! Return the list of links for a given source name.
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
    @{ */

/*! Create a network monitor. */
mapper_monitor mapper_monitor_new();

/*! Free a network monitor. */
void mapper_monitor_free(mapper_monitor mon);

/*! Poll a network monitor.
 *  \return The number of handled messages. */
int mapper_monitor_poll(mapper_monitor mon, int block_ms);

/*! Get the database associated with a monitor. This can be used as
 *  long as the monitor remains alive. */
mapper_db mapper_monitor_get_db(mapper_monitor mon);

/*! Request signals for specific device. */
int mapper_monitor_request_signals_by_name(
    mapper_monitor mon, const char* name);

/*! Request links for specific device. */
int mapper_monitor_request_links_by_name(
    mapper_monitor mon, const char* name);

/*! Request mappings for specific device. */
int mapper_monitor_request_mappings_by_name(
    mapper_monitor mon, const char* name);

/* @} */

/***** String tables *****/

/* For accessing named parameters of signals and devices. */

/*! Look up a signal property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param sig The signal to look at.
 *  \param index Numerical index of a signal property.
 *  \param property Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param type Address of a character to receive the property value
 *              type.
 *  \param value Address of a lo_arg* to receive the property value.
 *  \return Zero if found, otherwise non-zero.
 *  @ingroup signaldb */
int mapper_db_signal_property_index(mapper_db_signal sig, unsigned int index,
                                    const char **property, char *type,
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
                                     char *type,
                                     const lo_arg **value);

/*! Look up a device property by index. To iterate all properties,
 *  call this function from index=0, increasing until it returns zero.
 *  \param dev The device to look at.
 *  \param index Numerical index of a device property.
 *  \param property Address of a string pointer to receive the name of
 *                  indexed property.  May be zero.
 *  \param type Address of a character to receive the property value
 *              type.
 *  \param value Address of a lo_arg* to receive the property value.
 *  \return Zero if found, otherwise non-zero.
 *  @ingroup devicedb */
int mapper_db_device_property_index(mapper_db_device dev, unsigned int index,
                                    const char **property, char *type,
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
                                     char *type,
                                     const lo_arg **value);

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_H__
