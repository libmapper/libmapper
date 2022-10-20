
#ifndef __MAPPER_INTERNAL_H__
#define __MAPPER_INTERNAL_H__

#include "types_internal.h"
#include <mapper/mapper.h>
#include <string.h>

#ifdef _MSC_VER
#include <malloc.h>
#endif

/* Structs that refer to things defined in mapper.h are declared here instead
   of in types_internal.h */

#define RETURN_UNLESS(condition) { if (!(condition)) { return; }}
#define RETURN_ARG_UNLESS(condition, arg) { if (!(condition)) { return arg; }}
#define DONE_UNLESS(condition) { if (!(condition)) { goto done; }}
#define FUNC_IF(func, arg) { if (arg) { func(arg); }}
#define PROP(NAME) MPR_PROP_##NAME

#if DEBUG
#define TRACE_RETURN_UNLESS(a, ret, ...) \
if (!(a)) { trace(__VA_ARGS__); return ret; }
#define TRACE_DEV_RETURN_UNLESS(a, ret, ...) \
if (!(a)) { trace_dev(dev, __VA_ARGS__); return ret; }
#define TRACE_NET_RETURN_UNLESS(a, ret, ...) \
if (!(a)) { trace_net(__VA_ARGS__); return ret; }
#else
#define TRACE_RETURN_UNLESS(a, ret, ...) if (!(a)) { return ret; }
#define TRACE_DEV_RETURN_UNLESS(a, ret, ...) if (!(a)) { return ret; }
#define TRACE_NET_RETURN_UNLESS(a, ret, ...) if (!(a)) { return ret; }
#endif

#if defined(WIN32) || defined(_MSC_VER)
#define MPR_INLINE __inline
#else
#define MPR_INLINE __inline
#endif

/**** Debug macros ****/

/*! Debug tracer */
#ifdef __GNUC__
#ifdef DEBUG
#include <stdio.h>
#include <assert.h>
#define trace(...) { printf("-- " __VA_ARGS__); }
#define trace_graph(...)  { printf("\x1B[31m-- <graph>\x1B[0m " __VA_ARGS__);}
#define trace_net(...)  { printf("\x1B[33m-- <network>\x1B[0m  " __VA_ARGS__);}
#define die_unless(a, ...) { if (!(a)) { printf("-- " __VA_ARGS__); assert(a); } }
#else /* !DEBUG */
#define trace(...) {}
#define trace_graph(...) {}
#define trace_net(...) {}
#define die_unless(...) {}
#endif /* DEBUG */
#else /* !__GNUC__ */
#define trace(...) {};
#define trace_graph(...) {};
#define trace_net(...) {};
#define die_unless(...) {};
#endif /* __GNUC__ */

/**** Subscriptions ****/
#ifdef DEBUG
void print_subscription_flags(int flags);
#endif

/**** Objects ****/
void mpr_obj_increment_version(mpr_obj obj);

#define MPR_LINK 0x20

/**** Messages ****/
/*! Parse the device and signal names from an OSC path. */
int mpr_parse_names(const char *string, char **devnameptr, char **signameptr);

/*! Parse a message based on an OSC path and named properties.
 *  \param argc     Number of arguments in the argv array.
 *  \param types    String containing message parameter types.
 *  \param argv     Vector of lo_arg structures.
 *  \return         A mpr_msg structure. Free when done using mpr_msg_free. */
mpr_msg mpr_msg_parse_props(int argc, const mpr_type *types, lo_arg **argv);

void mpr_msg_free(mpr_msg msg);

/*! Look up the value of a message parameter by symbolic identifier.
 *  \param msg      Structure containing parameter info.
 *  \param prop     Symbolic identifier of the property to look for.
 *  \return         Pointer to mpr_msg_atom, or zero if not found. */
mpr_msg_atom mpr_msg_get_prop(mpr_msg msg, int prop);

void mpr_msg_add_typed_val(lo_message msg, int len, mpr_type type, const void *val);

/*! Helper for setting property value from different lo_arg types. */
int set_coerced_val(int src_len, mpr_type src_type, const void *src_val,
                    int dst_len, mpr_type dst_type, void *dst_val);

int match_pattern(const char* s, const char* p);

/**** Time ****/

/*! Get the current time. */
double mpr_get_current_time(void);

/*! Return the difference in seconds between two mpr_times.
 *  \param minuend      The minuend.
 *  \param subtrahend   The subtrahend.
 *  \return             The difference a-b in seconds. */
double mpr_time_get_diff(mpr_time minuend, mpr_time subtrahend);

/**** Properties ****/

/*! Helper for printing typed values.
 *  \param len          The vector length of the value.
 *  \param type         The value type.
 *  \param val          A pointer to the property value to print. */
void mpr_prop_print(int len, mpr_type type, const void *val);

mpr_prop mpr_prop_from_str(const char *str);

const char *mpr_prop_as_str(mpr_prop prop, int skip_slash);

/**** Types ****/

/*! Helper to find size of signal value types. */
MPR_INLINE static int mpr_type_get_size(mpr_type type)
{
    if (type <= MPR_LIST)   return sizeof(void*);
    switch (type) {
        case MPR_INT32:
        case MPR_BOOL:
        case 'T':
        case 'F':           return sizeof(int);
        case MPR_FLT:       return sizeof(float);
        case MPR_DBL:       return sizeof(double);
        case MPR_PTR:       return sizeof(void*);
        case MPR_STR:       return sizeof(char*);
        case MPR_INT64:     return sizeof(int64_t);
        case MPR_TIME:      return sizeof(mpr_time);
        case MPR_TYPE:      return sizeof(mpr_type);
        default:
            die_unless(0, "Unknown type '%c' in mpr_type_get_size().\n", type);
            return 0;
    }
}

/**** Values ****/

void mpr_value_realloc(mpr_value val, unsigned int vec_len, mpr_type type,
                       unsigned int mem_len, unsigned int num_inst, int is_output);

void mpr_value_reset_inst(mpr_value v, int idx);

int mpr_value_remove_inst(mpr_value v, int idx);

void mpr_value_set_samp(mpr_value v, int idx, void *s, mpr_time t);

/*! Helper to find the pointer to the current value in a mpr_value_t. */
MPR_INLINE static void* mpr_value_get_samp(mpr_value v, int idx)
{
    mpr_value_buffer b = &v->inst[idx % v->num_inst];
    return (char*)b->samps + b->pos * v->vlen * mpr_type_get_size(v->type);
}

MPR_INLINE static void* mpr_value_get_samp_hist(mpr_value v, int inst_idx, int hist_idx)
{
    mpr_value_buffer b = &v->inst[inst_idx % v->num_inst];
    int idx = (b->pos + v->mlen + hist_idx) % v->mlen;
    if (idx < 0)
        idx += v->mlen;
    return (char*)b->samps + idx * v->vlen * mpr_type_get_size(v->type);
}

/*! Helper to find the pointer to the current time in a mpr_value_t. */
MPR_INLINE static mpr_time* mpr_value_get_time(mpr_value v, int idx)
{
    mpr_value_buffer b = &v->inst[idx % v->num_inst];
    return &b->times[b->pos];
}

MPR_INLINE static mpr_time* mpr_value_get_time_hist(mpr_value v, int inst_idx, int hist_idx)
{
    mpr_value_buffer b = &v->inst[inst_idx % v->num_inst];
    int idx = (b->pos + v->mlen + hist_idx) % v->mlen;
    if (idx < 0)
        idx += v->mlen;
    return &b->times[idx];
}

void mpr_value_free(mpr_value v);

#ifdef DEBUG
void mpr_value_print(mpr_value v, int inst_idx);
void mpr_value_print_hist(mpr_value v, int inst_idx);
#endif

/*! Helper to check if bitfields match completely. */
MPR_INLINE static int bitmatch(unsigned int a, unsigned int b)
{
    return (a & b) == b;
}

/*! Helper to check if type is a number. */
MPR_INLINE static int mpr_type_get_is_num(mpr_type type)
{
    switch (type) {
        case MPR_INT32:
        case MPR_FLT:
        case MPR_DBL:
            return 1;
        default:    return 0;
    }
}

/*! Helper to check if type is a boolean. */
MPR_INLINE static int mpr_type_get_is_bool(mpr_type type)
{
    return 'T' == type || 'F' == type;
}

/*! Helper to check if type is a string. */
MPR_INLINE static int mpr_type_get_is_str(mpr_type type)
{
    return MPR_STR == type;
}

/*! Helper to check if type is a string or void* */
MPR_INLINE static int mpr_type_get_is_ptr(mpr_type type)
{
    return MPR_PTR == type || MPR_STR == type;
}

/*! Helper to check if data type matches, but allowing 'T' and 'F' for bool. */
MPR_INLINE static int type_match(const mpr_type l, const mpr_type r)
{
    return (l == r) || (strchr("bTF", l) && strchr("bTF", r));
}

/*! Helper to remove a leading slash '/' from a string. */
MPR_INLINE static const char *skip_slash(const char *string)
{
    return string + (string && string[0]=='/');
}

MPR_INLINE static void set_bitflag(char *bytearray, int idx)
{
    bytearray[idx / 8] |= 1 << (idx % 8);
}

MPR_INLINE static void unset_bitflag(char *bytearray, int idx)
{
    bytearray[idx / 8] &= (0xFF ^ (1 << (idx % 8)));
}

MPR_INLINE static int get_bitflag(char *bytearray, int idx)
{
    return bytearray[idx / 8] & 1 << (idx % 8);
}

MPR_INLINE static int compare_bitflags(char *l, char *r, int num_flags)
{
    return memcmp(l, r, num_flags / 8 + 1);
}

MPR_INLINE static void clear_bitflags(char *bytearray, int num_flags)
{
    memset(bytearray, 0, num_flags / 8 + 1);
}

#endif /* __MAPPER_INTERNAL_H__ */
