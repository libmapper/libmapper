
#ifndef __MAPPER_H__
#define __MAPPER_H__

#include <mapper/mapper_types.h>

/*** Signals ***/

/*! A signal value may be one of several different types, so we use a
 union to represent this.  The appropriate selection from this union
 is determined by the mapper_signal::type variable. */

typedef union _mapper_signal_value
{
    float f;
    double d;
    int i32;
} mapper_signal_value_t;

/*! A signal is defined as a vector of values, along with some
 *  metadata. */

typedef struct _mapper_signal
{
    char type;  //!< The type of this signal, specified as an OSC type character.
    int length; //!< Length of the signal vector, or 1 for scalars.
    char *name; //!< The name of this signal, an OSC path.  Must start with '/'.
    char *unit; //!< The unit of this signal, or NULL for N/A.
    mapper_signal_value_t *minimum; //!< The minimum of this signal, or NULL for no minimum.
    mapper_signal_value_t *maximum; //!< The maximum of this signal, or NULL for no maximum.
    mapper_signal_value_t *value;   //!< An optional pointer to a C variable containing the actual vector.
} *mapper_signal;

/*! Fill out a signal structure for a floating point scalar. */
/*! \param sig The _mapper_signal struct to fill out.
 *  \param name The name of the signal, starting with '/'.
 *  \param length The length of the signal vector, or 1 for a scalar.
 *  \param unit The unit of the signal, or 0 for none.
 *  \param minimum The minimum possible value, or INFINITY for none.
 *  \param maximum The maximum possible value, or INFINITY for none.
 *  \param value The address of a float value (or array) this signal
 *               implicitly reflects, or 0 for none.
 *  \return Pointer to a newly allocated signal structure.
 */
mapper_signal msig_float(int length, const char *name,
                         const char *unit, float minimum,
                         float maximum, float *value);

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

#endif // __MAPPER_H__
