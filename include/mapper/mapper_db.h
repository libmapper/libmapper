#ifndef __MAPPER_DB_H__
#define __MAPPER_DB_H__

#ifdef __cplusplus
extern "C" {
#endif

/*! \file This file defines structs used to return information from
 *  the network database. */

/*! A record that keeps information about a device on the network. */
typedef struct _mapper_db_device {
    char *name;   //!< Device name.
    char *host;   //!< Device network host name.
    int port;     //!< Device network port.
    int canAlias; //!< True if the device can handle OSC aliasing.
    void* user_data; //!< User modifiable data.
} mapper_db_device_t, *mapper_db_device;

/* Bit flags to identify which range extremities are known. If the bit
 * field is equal to RANGE_KNOWN, then all four required extremities
 * are known, and a linear mapping can be calculated. */
#define MAPPING_RANGE_SRC_MIN  0x01
#define MAPPING_RANGE_SRC_MAX  0x02
#define MAPPING_RANGE_DEST_MIN 0x04
#define MAPPING_RANGE_DEST_MAX 0x08
#define MAPPING_RANGE_KNOWN    0x0F

typedef struct _mapper_mapping_range {
    float src_min;              //!< Source minimum.
    float src_max;              //!< Source maximum.
    float dest_min;             //!< Destination minimum.
    float dest_max;             //!< Destination maximum.
    int known;                  /*!< Bitfield identifying which range
                                 *   extremities are known. */
} mapper_mapping_range_t;

/*! Describes what happens when the clipping boundaries are
 *  exceeded. */
typedef enum _mapper_clipping_type {
    CT_NONE,    /*!< Value is passed through unchanged. This is the
                 *   default. */
    CT_MUTE,    //!< Value is muted.
    CT_CLAMP,   //!< Value is limited to the boundary.
    CT_FOLD,    //!< Value continues in opposite direction.
    CT_WRAP,    /*!< Value appears as modulus offset at the opposite
                 *   boundary. */
} mapper_clipping_type;

/*! Describes the scaling mode of the mapping. */
typedef enum _mapper_scaling_type {
    SC_BYPASS,       //!< Direct scaling
    SC_LINEAR,       //!< Linear scaling
    SC_EXPRESSION,   //!< Expression scaling
    SC_CALIBRATE,    //!< Calibrate to input
    SC_MUTE,         //!< Mute scaling
} mapper_scaling_type;

/*! A record that describes the properties of a connection mapping. */
typedef struct _mapper_db_mapping {
    char *src_name;                 //!< Source signal name (OSC path).
    char *dest_name;                //!< Destination signal name (OSC path).

    char src_type;              //!< Source signal type.
    char dest_type;             //!< Destination signal type.

    mapper_clipping_type clip_upper;  /*!< Operation for exceeded
                                       *   upper boundary. */
    mapper_clipping_type clip_lower;  /*!< Operation for exceeded
                                       *   lower boundary. */

    mapper_mapping_range_t range;     //!< Range information.
    char *expression;

    mapper_scaling_type scaling;   /*!< Bypass, linear, calibrate, or
                                    *   expression mapping */
    int muted;                  /*!< 1 to mute mapping connection, 0
                                 *   to unmute */
} mapper_db_mapping_t, *mapper_db_mapping;

/*! A signal value may be one of several different types, so we use a
 *  union to represent this.  The appropriate selection from this
 *  union is determined by the mapper_signal::type variable. */

typedef union _mapper_signal_value {
    float f;
    double d;
    int i32;
} mapper_signal_value_t, mval;

/*! A record that describes properties of a signal. */
typedef struct _mapper_db_signal
{
    /*! The type of this signal, specified as an OSC type
     *  character. */
    char type;

    /*! Length of the signal vector, or 1 for scalars. */
    int length;

    /*! The name of this signal, an OSC path.  Must start with '/'. */
    const char *name;

    /*! The unit of this signal, or NULL for N/A. */
    const char *unit;

    /*! The minimum of this signal, or NULL for no minimum. */
    mapper_signal_value_t *minimum;

    /*! The maximum of this signal, or NULL for no maximum. */
    mapper_signal_value_t *maximum;
} mapper_db_signal_t, *mapper_db_signal;

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_DB_H__
