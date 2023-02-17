#include <lo/lo_lowlevel.h>

#include <stdlib.h>
#include <string.h>
#include "message.h"
#include "property.h"
#include "util/mpr_debug.h"
#include "util/mpr_inline.h"

/**** Messages ****/

/*! Queriable representation of a parameterized message parsed from an incoming
 *  OSC message. Does not contain a copy of data, so only valid for the duration
 *  of the message handler. Also allows for a constant number of "extra"
 *  parameters; that is, unknown parameters that may be specified for a signal
 *  and used for metadata, which will be added to a general-purpose string table
 *  associated with the signal. */
typedef struct _mpr_msg_atom
{
    const char *key;
    lo_arg **vals;
    const mpr_type *types;
    int len;
    int prop;
} mpr_msg_atom_t, *mpr_msg_atom;

typedef struct _mpr_msg
{
    mpr_msg_atom_t *atoms;
    int num_atoms;
} *mpr_msg;

int mpr_msg_get_num_atoms(mpr_msg m)
{
    return m->num_atoms;
}

mpr_msg_atom mpr_msg_get_atom(mpr_msg m, int idx)
{
    return &m->atoms[idx];
}

int mpr_msg_atom_get_len(mpr_msg_atom a)
{
    return a->len;
}

int mpr_msg_atom_get_prop(mpr_msg_atom a)
{
    return a->prop;
}

void mpr_msg_atom_set_prop(mpr_msg_atom a, int prop)
{
    a->prop = prop;
}

const char *mpr_msg_atom_get_key(mpr_msg_atom a)
{
    return a->key;
}

const mpr_type *mpr_msg_atom_get_types(mpr_msg_atom a)
{
    return a->types;
}

lo_arg **mpr_msg_atom_get_values(mpr_msg_atom a)
{
    return a->vals;
}

/*! Helper to check if data type matches, but allowing 'T' and 'F' for bool. */
MPR_INLINE static int type_match(const mpr_type l, const mpr_type r)
{
    return (l == r) || (strchr("bTF", l) && strchr("bTF", r));
}

mpr_msg mpr_msg_parse_props(int argc, const mpr_type *types, lo_arg **argv)
{
    int i, slot_idx, num_props=0;
    mpr_msg msg;
    mpr_msg_atom a;
    const char *key;

    /* get the number of props */
    for (i = 0; i < argc; i++) {
        if (types[i] != MPR_STR)
            continue;
        if (argv[i]->s == '@' || !strncmp(&argv[i]->s, "-@", 2) || !strncmp(&argv[i]->s, "+@", 2))
            ++num_props;
    }
    RETURN_ARG_UNLESS(num_props, 0);

    msg = (mpr_msg) calloc(1, sizeof(struct _mpr_msg));
    msg->atoms = ((mpr_msg_atom_t*) calloc(1, sizeof(struct _mpr_msg_atom) * num_props));
    a = &msg->atoms[0];

    for (i = 0; i < argc; i++) {
        if (!mpr_type_get_is_str(types[i])) {
            /* property ID not a string */
#if TRACING
            printf("message item '");
            lo_arg_pp(types[i], argv[i]);
            printf("' not a string.\n");
#endif
            continue;
        }
#if TRACING
        printf("parsing property key '%s'\n", &argv[i]->s);
#endif
        /* new property */
        if (a->types || (a->prop & PROP_REMOVE))
            ++msg->num_atoms;
        a = &msg->atoms[msg->num_atoms];

        key = &argv[i]->s;
        if (strncmp(key, "+@", 2)==0) {
            a->prop = PROP_ADD;
            ++key;
        }
        else if (strncmp(key, "-@", 2)==0) {
            a->prop = PROP_REMOVE;
            ++key;
        }
        if (key[0] != '@') /* not a property key */
            continue;
        a->key = key;

        /* try to find matching index for static props */
        if (strncmp(a->key, "@dst@", 5)==0) {
            a->prop |= DST_SLOT_PROP;
            a->key += 5;
        }
        else if (strncmp(a->key, "@src", 4)==0) {
            if (a->key[4] == '@') {
                a->prop |= SRC_SLOT_PROP(0);
                a->key += 5;
            }
            else if (a->key[4] == '.') {
                /* in form 'src.<ordinal>' */
                slot_idx = atoi(a->key + 5);
                a->key = strchr(a->key + 5, '@');
                if (!a->key || !(++a->key)) {
                    trace("No sub-property found in key '%s'.\n", a->key);
                    a->types = 0;
                    continue;
                }
                a->prop |= SRC_SLOT_PROP(slot_idx);
            }
        }
        else
            ++a->key;

        a->prop |= mpr_prop_from_str(a->key);

        if (msg->num_atoms < 0)
            continue;
        a->types = &types[i+1];
        a->vals = &argv[i+1];
        while (++i < argc) {
            if ((types[i] == MPR_STR) && strcspn(&argv[i]->s, "@") < 2) {
                /* Arrived at next property index. */
                --i;
                break;
            }
            else if (!type_match(types[i], a->types[0])) {
                trace("Value vector for key '%s' has heterogeneous types.\n", a->key);
                a->len = a->prop = 0;
                a->types = 0;
                break;
            }
            else
                ++a->len;
        }
        if (!a->len) {
            a->types = 0;
            if (!(a->prop & PROP_REMOVE)) {
                trace("Key '%s' has no values.\n", a->key);
                continue;
            }
        }
        /* check type against static props */
        else if (MASK_PROP_BITFLAGS(a->prop) < MPR_PROP_EXTRA) {
            int len = mpr_prop_get_len(a->prop);
            int protocol_type = mpr_prop_get_protocol_type(a->prop);
            if (len) {
                if (len != a->len) {
                    /* TODO: remove property string lookup after checking output */
                    trace("Static property '%s:%s' cannot have length %d.\n", a->key,
                          mpr_prop_as_str(a->prop, 1), a->len);
                    a->len = a->prop = 0;
                    a->types = 0;
                    continue;
                }
                if (a->prop & (PROP_ADD | PROP_REMOVE)) {
                    trace("Cannot add or remove values from static property '%s:%s'.\n", a->key,
                          mpr_prop_as_str(a->prop, 1));
                    a->len = a->prop = 0;
                    a->types = 0;
                    continue;
                }
            }
            if (!protocol_type) {
                trace("Static property '%s:%s' cannot be set by message.\n", a->key,
                      mpr_prop_as_str(a->prop, 1));
                a->len = a->prop = 0;
                a->types = 0;
                continue;
            }
            if (protocol_type == 'n') {
                if (!mpr_type_get_is_num(a->types[0])) {
                    trace("Static property '%s:%s' cannot have type '%c' (3).\n", a->key,
                          mpr_prop_as_str(a->prop, 1), a->types[0]);
                    a->len = a->prop = 0;
                    a->types = 0;
                    continue;
                }
            }
            else if (protocol_type == MPR_BOOL) {
                if (!mpr_type_get_is_bool(a->types[0])) {
                    trace("Static property '%s:%s' cannot have type '%c' (2).\n", a->key,
                          mpr_prop_as_str(a->prop, 1), a->types[0]);
                    a->len = a->prop = 0;
                    a->types = 0;
                    continue;
                }
            }
            else if (protocol_type != a->types[0]) {
                trace("Static property '%s:%s' cannot have type '%c' (1).\n", a->key,
                      mpr_prop_as_str(a->prop, 1), a->types[0]);
                a->len = a->prop = 0;
                a->types = 0;
                continue;
            }
        }
    }
    /* reset last atom if no types unless "remove" flag is set */
    if (a->types || a->prop & PROP_REMOVE)
        ++msg->num_atoms;
    else {
        a->key = 0;
        a->len = 0;
        a->vals = 0;
    }
#if TRACING
    /* print out parsed properties */
    printf("%d parsed mpr_msgs:\n", msg->num_atoms);
    for (i = 0; i < msg->num_atoms; i++) {
        a = &msg->atoms[i];
        if (a->prop & PROP_ADD)
            printf(" +");
        else if (a->prop & PROP_REMOVE)
            printf(" -");
        else
            printf("  ");
        if (a->prop & DST_SLOT_PROP)
            printf("'dst/%s' [%d]: ", a->key, a->prop);
        else if (a->prop >> SRC_SLOT_PROP_BIT_OFFSET)
            printf("'src%d/%s' [%d]: ", SRC_SLOT(a->prop), a->key, a->prop);
        else
            printf("'%s' [%d]: ", a->key, a->prop);
        int j;
        for (j = 0; j < a->len; j++) {
            lo_arg_pp(a->types[j], a->vals[j]);
            printf(", ");
        }
        printf("\b\b \n");
    }
#endif
    return msg;
}

void mpr_msg_free(mpr_msg msg)
{
    RETURN_UNLESS(msg);
    FUNC_IF(free, msg->atoms);
    free(msg);
}

mpr_msg_atom mpr_msg_get_prop(mpr_msg msg, int prop)
{
    int i;
    for (i = 0; i < msg->num_atoms; i++) {
        if (msg->atoms[i].prop == prop) {
            RETURN_ARG_UNLESS(msg->atoms[i].len && msg->atoms[i].types, 0);
            return &msg->atoms[i];
        }
    }
    return 0;
}
