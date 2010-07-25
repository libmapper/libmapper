
#ifndef __MAPPER_H__
#define __MAPPER_H__

#include <mapper/mapper_types.h>


/*** Global list that keeps information about the devices on the network ***/

/*! A structure that keeps information sent by /registered*/
typedef struct _mapper_admin_registered_info {
    char *full_name;
	char *host;
	int port;
	char *canAlias;
} mapper_admin_registered_info;

/*! Linked list of regist_info. Only one object : the global list REGIST_DEVICES_INFO2*/
typedef struct mapper_registered_infos mapper_registered_infos;
struct mapper_registered_infos
	{
 		mapper_admin_registered_info *regist_info;
		struct mapper_registered_infos *next;
	};
typedef mapper_registered_infos *list_regist_info;
/*! A global list that contains the regist_info of all the registered devices*/
extern list_regist_info REGIST_DEVICES_INFO2;




/*** Signals ***/

/*! A signal value may be one of several different types, so we use a
 union to represent this.  The appropriate selection from this union
 is determined by the mapper_signal::type variable. */

typedef union _mapper_signal_value
{
    float f;
    double d;
    int i32;
} mapper_signal_value_t, mval;

/*! A signal handler function can be called whenever a signal value
 *  changes. */
typedef void mapper_signal_handler(mapper_device mdev,
                                   mapper_signal_value_t *v);

/*! A signal is defined as a vector of values, along with some
 *  metadata. */

typedef struct _mapper_signal
{
    char type;  //!< The type of this signal, specified as an OSC type character.
    int length; //!< Length of the signal vector, or 1 for scalars.
    const char *name; //!< The name of this signal, an OSC path.  Must start with '/'.
    const char *unit; //!< The unit of this signal, or NULL for N/A.
    mapper_signal_value_t *minimum; //!< The minimum of this signal, or NULL for no minimum.
    mapper_signal_value_t *maximum; //!< The maximum of this signal, or NULL for no maximum.
    mapper_signal_value_t *value;   //!< An optional pointer to a C variable containing the actual vector.
    mapper_device device; //!< The device associated with this signal.
    mapper_signal_handler *handler; //!< An optional function to be called when the signal value changes.
    void *user_data;
} *mapper_signal;

/*! Fill out a signal structure for a floating point scalar. */
/*! \param name The name of the signal, starting with '/'.
 *  \param length The length of the signal vector, or 1 for a scalar.
 *  \param unit The unit of the signal, or 0 for none.
 *  \param minimum The minimum possible value, or INFINITY for none.
 *  \param maximum The maximum possible value, or INFINITY for none.
 *  \param handler the function to be called when the value af the signel is updated.
 *  \param value The address of a float value (or array) this signal
 *               implicitly reflects, or 0 for none.
 *  
 */
mapper_signal msig_float(int length, const char *name,
                         const char *unit, float minimum,
                         float maximum, float *value,
                         mapper_signal_handler *handler,
                         void *user_data);

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
void mdev_register_input(mapper_device device,
                       mapper_signal signal);

//! Unregister a signal with a mapper device.
void mdev_register_output(mapper_device device,
                        mapper_signal signal);

//! Return the number of inputs.
int mdev_num_inputs(mapper_device device);

//! Return the number of outputs.
int mdev_num_outputs(mapper_device device);

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

#endif // __MAPPER_H__
