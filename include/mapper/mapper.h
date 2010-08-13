#ifndef __MAPPER_H__
#define __MAPPER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <mapper/mapper_db.h>
#include <mapper/mapper_types.h>

/*** Signals ***/

/*! A signal handler function can be called whenever a signal value
 *  changes. */
typedef void mapper_signal_handler(mapper_device mdev,
                                   mapper_signal_value_t *v);

/*! A signal is defined as a vector of values, along with some
 *  metadata. */

typedef struct _mapper_signal
{
    /*! Properties of this signal. */
    mapper_db_signal_t props;

    /*! An optional pointer to a C variable containing the actual
     *  vector. */
    mapper_signal_value_t *value;

    /*! The device associated with this signal. */
    mapper_device device;

    /*! An optional function to be called when the signal value
     *  changes. */
    mapper_signal_handler *handler;

    /*! A pointer available for passing user context. */
    void *user_data;
} *mapper_signal;

/*! Fill out a signal structure for a floating point scalar. */
/*! \param name The name of the signal, starting with '/'.
 *  \param length The length of the signal vector, or 1 for a scalar.
 *  \param unit The unit of the signal, or 0 for none.
 *  \param minimum The minimum possible value, or INFINITY for none.
 *  \param maximum The maximum possible value, or INFINITY for none.
 *  \param handler the function to be called when the value of the
 *                 signel is updated.
 *  \param value The address of a float value (or array) this signal
 *               implicitly reflects, or 0 for none.
 *
 */
mapper_signal msig_float(int length, const char *name,
                         const char *unit, float minimum,
                         float maximum, float *value,
                         mapper_signal_handler *handler, void *user_data);

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

/*** Devices ***/

//! Allocate and initialize a mapper device.
mapper_device mdev_new(const char *name_prefix, int initial_port);

//! Free resources used by a mapper device.
void mdev_free(mapper_device device);

//! Register a signal with a mapper device.
void mdev_register_input(mapper_device device, mapper_signal signal);

//! Unregister a signal with a mapper device.
void mdev_register_output(mapper_device device, mapper_signal signal);

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

/*! Poll this device for new messages.
 *  \param block_ms Number of milliseconds to block waiting for
 *  messages, or 0 for non-blocking behaviour. */
void mdev_poll(mapper_device md, int block_ms);

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

/**** Local device database ****/

/*! The set of possible actions on a database record, used to inform
 *  callbacks of what is happening to a record. */
typedef enum {
    MDB_MODIFY,
    MDB_NEW,
    MDB_REMOVE,
} mapper_db_action_t;

/***** Devices *****/

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
void mapper_db_add_device_callback(device_callback_func *f, void *user);

/*! Remove a device record callback from the database service.
 *  \param cb   Callback function.
 *  \param user The user context pointer that was originally specified
 *              when adding the callback. */
void mapper_db_remove_device_callback(device_callback_func *f, void *user);

/*! Return the whole list of devices.
 *  \return A double-pointer to the first item in the list of devices,
 *          or zero if none.  Use mapper_db_device_next() to
 *          iterate. */
mapper_db_device_t **mapper_db_get_all_devices();

/*! Find information for a registered device.
 *  \param name  Name of the device to find in the database.
 *  \return      Information about the device, or zero if not found. */
mapper_db_device mapper_db_get_device_by_name(const char *device_name);

/*! Return the list of devices with a substring in their name.
 *  \param str The substring to search for.
 *  \return    A double-pointer to the first item in a list of matching
 *             devices.  Use mapper_db_device_next() to iterate. */
mapper_db_device_t **mapper_db_match_device_by_name(char *device_pattern);

/*! Given a device record pointer returned from a previous
 *  mapper_db_return_*() call, get the next item in the list.
 *  \param  The previous device record pointer.
 *  \return A double-pointer to the next device record in the list, or
 *          zero if no more devices. */
mapper_db_device_t **mapper_db_device_next(mapper_db_device_t**);

/***** Signals *****/

/*! A callback function prototype for when a signal record is added or
 *  updated in the database. Such a function is passed in to
 *  mapper_db_add_signal_callback().
 *  \param record  Pointer to the signal record.
 *  \param device  Pointer to the associated device record.
 *  \param action  A value of mapper_db_action_t indicating what
 *                 is happening to the database record.
 *  \param user    The user context pointer registered with this
 *                 callback. */
typedef void signal_callback_func(mapper_db_signal record,
                                  mapper_db_device device,
                                  mapper_db_action_t action,
                                  void *user);

/*! Register a callback for when a signal record is added or updated
 *  in the database.
 *  \param cb   Callback function.
 *  \param user A user-defined pointer to be passed to the callback
 *              for context . */
void mapper_db_add_signal_callback(signal_callback_func *f, void *user);

/*! Remove a signal record callback from the database service.
 *  \param cb   Callback function.
 *  \param user The user context pointer that was originally specified
 *              when adding the callback. */
void mapper_db_remove_signal_callback(signal_callback_func *f, void *user);

/*! Return the list of inputs for a given device.
 *  \param device_name Name of the device to match for outputs.  Must
 *                     be exact, including the leading '/'.
 *  \return A double-pointer to the first item in the list of input
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_get_inputs_by_device_name(
    const char *device_name);

/*! Return the list of outputs for a given device.
 *  \param device_name Name of the device to match for outputs.  Must
 *                     be exact, including the leading '/'.
 *  \return A double-pointer to the first item in the list of output
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_get_outputs_by_device_name(
    const char *device_name);

/*! Return the list of inputs for a given device.
 *  \param device_name Name of the device to match for inputs.
 *  \param input_pattern A substring to search for in the device inputs.
 *  \return A double-pointer to the first item in the list of input
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_match_inputs_by_device_name(
    const char *device_name, const char *input_pattern);

/*! Return the list of outputs for a given device.
 *  \param device_name Name of the device to match for outputs.
 *  \param output_pattern A substring to search for in the device outputs.
 *  \return A double-pointer to the first item in the list of output
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_signal_t **mapper_db_match_outputs_by_device_name(
    const char *device_name, char const *output_pattern);

/*! Given a signal record pointer returned from a previous
 *  mapper_db_get_*() call, get the next item in the list.
 *  \param  The previous signal record pointer.
 *  \return A double-pointer to the next signal record in the list, or
 *          zero if no more signals. */
mapper_db_signal_t **mapper_db_signal_next(mapper_db_signal_t**);

/***** Mappings *****/

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
void mapper_db_add_mapping_callback(mapping_callback_func *f, void *user);

/*! Remove a mapping record callback from the database service.
 *  \param cb   Callback function.
 *  \param user The user context pointer that was originally specified
 *              when adding the callback. */
void mapper_db_remove_mapping_callback(mapping_callback_func *f, void *user);

/*! Return the list of mappings for a given input name.
 *  \param input_name Name of the input to find.
 *  \return A double-pointer to the first item in the list of input
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_input_name(
    const char *input_name);

/*! Return the list of mappings for a given device and input name.
 *  \param device_name Exact name of the device to find, including the
 *                    leading '/'.
 *  \param input_name Exact name of the input to find, including the
 *                    leading '/'.
 *  \return A double-pointer to the first item in the list of input
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_device_and_input_name(
    const char *device_name, const char *input_name);

/*! Return the list of mappings for a given output name.
 *  \param output_name Name of the output to find.
 *  \return A double-pointer to the first item in the list of output
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_output_name(
    const char *output_name);

/*! Return the list of mappings for a given device and output name.
 *  \param device_name Exact name of the device to find, including the
 *                     leading '/'.
 *  \param output_name Exact name of the output to find, including the
 *                     leading '/'.
 *  \return A double-pointer to the first item in the list of output
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_device_and_output_name(
    const char *device_name, const char *output_name);

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
 *  \return A double-pointer to the first item in the list of output
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_mapping_t **mapper_db_get_mappings_by_devices_and_signals(
    mapper_db_device_t **input_devices,  mapper_db_signal_t **inputs,
    mapper_db_device_t **output_devices, mapper_db_signal_t **outputs);

/*! Given a mapping record pointer returned from a previous
 *  mapper_db_get_mappings*() call, get the next item in the list.
 *  \param  The previous mapping record pointer.
 *  \return A double-pointer to the next mapping record in the list, or
 *          zero if no more mappings. */
mapper_db_mapping_t **mapper_db_mapping_next(mapper_db_mapping_t**);

/***** Links *****/

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
void mapper_db_add_link_callback(link_callback_func *f, void *user);

/*! Remove a link record callback from the database service.
 *  \param cb   Callback function.
 *  \param user The user context pointer that was originally specified
 *              when adding the callback. */
void mapper_db_remove_link_callback(link_callback_func *f, void *user);

/*! Return the list of links for a given input name.
 *  \param input_device_name Name of the input device to find.
 *  \return A double-pointer to the first item in the list of input
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_link_t **mapper_db_get_links_by_input_device_name(
    const char *input_device_name);

/*! Return the list of links for a given output name.
 *  \param output_device_name Name of the output device to find.
 *  \return A double-pointer to the first item in the list of output
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_link_t **mapper_db_get_links_by_output_device_name(
    const char *output_device_name);

/*! Return the list of links for a given input name.
 *  \param input_device_name Name of the input device to find.
 *  \return A double-pointer to the first item in the list of input
 *          signals, or zero if none.  Use mapper_db_signal_next() to
 *          iterate. */
mapper_db_link_t **mapper_db_get_links_by_input_output_devices(
    mapper_db_device_t **input_device_list,
    mapper_db_device_t **output_device_list);

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_H__
