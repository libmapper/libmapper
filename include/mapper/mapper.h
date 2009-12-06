
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

/*** Devices ***/

//! Allocate and initialize a mapper device.
mapper_device md_new(const char *name_prefix);

//! Free resources used by a mapper device.
void md_free(mapper_device device);

//! Register a signal with a mapper device.
void md_register_input(mapper_device device,
                       mapper_signal signal);

//! Unregister a signal with a mapper device.
void md_register_output(mapper_device device,
                        mapper_signal signal);

#endif // __MAPPER_H__
