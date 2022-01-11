#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "types_internal.h"
#include "mapper_internal.h"

#ifdef DEBUG
#define TRACING 0 /* Set non-zero to see parsed properties. */
#else
#define TRACING 0
#endif

/* length = 0 indicates variable length. */
typedef struct {
    const char *key;
    int len;
    mpr_type store_type;
    mpr_type protocol_type;
} static_prop_t;

/* Warning! This table needs to be kept synchonised with mpr_prop enum
 * found in mpr_constants.h */
const static_prop_t static_props[] = {
    { 0,                0, 0,         0 },         /* MPR_PROP_UNKNOWN */
    { "@bundle",        1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_BUNDLE */
    { "@data",          1, MPR_PTR,   0  },        /* MPR_PROP_DATA */
    { "@device",        1, MPR_DEV,   MPR_STR },   /* MPR_PROP_DEVICE */
    { "@direction",     1, MPR_INT32, MPR_STR },   /* MPR_PROP_DIR */
    { "@ephemeral",     1, MPR_BOOL,  MPR_BOOL },  /* MPR_PROP_EPHEM */
    { "@expr",          1, MPR_STR,   MPR_STR },   /* MPR_PROP_EXPR */
    { "@host",          1, MPR_STR,   MPR_STR },   /* MPR_PROP_HOST */
    { "@id",            1, MPR_INT64, MPR_INT64 }, /* MPR_PROP_ID */
    { "@is_local",      1, MPR_BOOL,  MPR_BOOL },  /* MPR_PROP_IS_LOCAL */
    { "@jitter",        1, MPR_FLT,   MPR_FLT },   /* MPR_PROP_JITTER */
    { "@length",        1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_LEN */
    { "@lib_version",   1, MPR_STR,   MPR_STR },   /* MPR_PROP_LIBVER */
    { "@linked",        0, MPR_DEV,   MPR_STR },   /* MPR_PROP_LINKED */
    { "@max",           0, 'n',       'n' },       /* MPR_PROP_MAX */
    { "@min",           0, 'n',       'n' },       /* MPR_PROP_MIN */
    { "@muted",         1, MPR_BOOL,  MPR_BOOL },  /* MPR_PROP_MUTED */
    { "@name",          1, MPR_STR,   MPR_STR },   /* MPR_PROP_NAME */
    { "@num_inst",      1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_NUM_INST */
    { "@num_maps",      2, MPR_INT32, MPR_INT32 }, /* MPR_PROP_NUM_MAPS */
    { "@num_maps_in",   1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_NUM_MAPS_IN */
    { "@num_maps_out",  1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_NUM_MAPS_OUT */
    { "@num_sigs_in",   1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_NUM_SIGS_IN */
    { "@num_sigs_out",  1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_NUM_SIGS_OUT */
    { "@ordinal",       1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_ORDINAL */
    { "@period",        1, MPR_FLT,   MPR_FLT },   /* MPR_PROP_PERIOD */
    { "@port",          1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_PORT */
    { "@process_loc",   1, MPR_INT32, MPR_STR },   /* MPR_PROP_PROCESS_LOC */
    { "@protocol",      1, MPR_INT32, MPR_STR },   /* MPR_PROP_PROTOCOL */
    { "@rate",          1, MPR_FLT,   MPR_FLT },   /* MPR_PROP_RATE */
    { "@scope",         0, MPR_DEV,   MPR_STR },   /* MPR_PROP_SCOPE */
    { "@signal",        0, MPR_SIG,   MPR_STR },   /* MPR_PROP_SIGNAL */
    { "@slot",          0, MPR_INT32, MPR_INT32 }, /* MPR_PROP_SLOT */
    { "@status",        1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_STATUS */
    { "@steal",         1, MPR_INT32, MPR_STR },   /* MPR_PROP_STEAL_MODE */
    { "@synced",        1, MPR_TIME,  MPR_TIME },  /* MPR_PROP_SYNCED */
    { "@type",          1, MPR_TYPE,  MPR_TYPE },  /* MPR_PROP_TYPE */
    { "@unit",          1, MPR_STR,   MPR_STR },   /* MPR_PROP_UNIT */
    { "@use_inst",      1, MPR_BOOL,  MPR_BOOL },  /* MPR_PROP_USE_INST */
    { "@version",       1, MPR_INT32, MPR_INT32 }, /* MPR_PROP_VERSION */
    { "@extra",         0, 'a', 'a' }, /* MPR_PROP_EXTRA (special case, does not
                                           * represent a specific property name) */
};

const char* mpr_loc_strings[] =
{
    NULL,           /* MPR_LOC_UNDEFINED */
    "src",          /* MPR_LOC_SRC */
    "dst",          /* MPR_LOC_DST */
    "any",          /* MPR_LOC_ANY */
};

const char* mpr_protocol_strings[] =
{
    NULL,           /* MPR_PROTO_UNDEFINED */
    "osc.udp",      /* MPR_PROTO_UDP */
    "osc.tcp",      /* MPR_PROTO_TCP */
};

const char *mpr_steal_strings[] =
{
    "none",         /* MPR_STEAL_NONE */
    "oldest",       /* MPR_STEAL_OLDEST */
    "newest",       /* MPR_STEAL_NEWEST */
};

int mpr_parse_names(const char *string, char **devnameptr, char **signameptr)
{
    char *devname, *signame;
    RETURN_ARG_UNLESS(string, 0);
    devname = (char*)skip_slash(string);
    RETURN_ARG_UNLESS(devname && devname[0] != '/', 0);
    if (devnameptr)
        *devnameptr = (char*) devname;
    signame = strchr(devname+1, '/');
    if (!signame) {
        if (signameptr)
            *signameptr = 0;
        return strlen(devname);
    }
    if (!++signame) {
        if (signameptr)
            *signameptr = 0;
        return strlen(devname)-1;
    }
    if (signameptr)
        *signameptr = signame;
    return (signame - devname - 1);
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
            static_prop_t prop;
            prop = static_props[PROP_TO_INDEX(a->prop)];
            if (prop.len) {
                if (prop.len != a->len) {
                    trace("Static property '%s' cannot have length %d.\n",
                          static_props[PROP_TO_INDEX(a->prop)].key, a->len);
                    a->len = a->prop = 0;
                    a->types = 0;
                    continue;
                }
                if (a->prop & (PROP_ADD | PROP_REMOVE)) {
                    trace("Cannot add or remove values from static property '%s'.\n",
                          static_props[PROP_TO_INDEX(a->prop)].key);
                    a->len = a->prop = 0;
                    a->types = 0;
                    continue;
                }
            }
            if (!prop.protocol_type) {
                trace("Static property '%s' cannot be set by message.\n",
                      static_props[PROP_TO_INDEX(a->prop)].key);
                a->len = a->prop = 0;
                a->types = 0;
                continue;
            }
            if (prop.protocol_type == 'n') {
                if (!mpr_type_get_is_num(a->types[0])) {
                    trace("Static property '%s' cannot have type '%c' (3).\n",
                          static_props[PROP_TO_INDEX(a->prop)].key, a->types[0]);
                    a->len = a->prop = 0;
                    a->types = 0;
                    continue;
                }
            }
            else if (prop.protocol_type == MPR_BOOL) {
                if (!mpr_type_get_is_bool(a->types[0])) {
                    trace("Static property '%s' cannot have type '%c' (2).\n",
                          static_props[PROP_TO_INDEX(a->prop)].key, a->types[0]);
                    a->len = a->prop = 0;
                    a->types = 0;
                    continue;
                }
            }
            else if (prop.protocol_type != a->types[0]) {
                trace("Static property '%s' cannot have type '%c' (1).\n",
                      static_props[PROP_TO_INDEX(a->prop)].key, a->types[0]);
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

#define LO_MESSAGE_ADD_VEC(MSG, TYPE, CAST, VAL)    \
for (i = 0; i < len; i++)                           \
    lo_message_add_##TYPE(MSG, ((CAST*)VAL)[i]);    \

/* helper for mpr_msg_varargs() */
void mpr_msg_add_typed_val(lo_message msg, int len, mpr_type type, const void *val)
{
    int i;
    if (type && len < 1)
        return;

    switch (type) {
        case MPR_STR:
            if (len == 1)   lo_message_add_string(msg, (char*)val);
            else            LO_MESSAGE_ADD_VEC(msg, string, char*, val);    	break;
        case MPR_FLT:       LO_MESSAGE_ADD_VEC(msg, float, float, val);         break;
        case MPR_DBL:       LO_MESSAGE_ADD_VEC(msg, double, double, val);       break;
        case MPR_INT32:     LO_MESSAGE_ADD_VEC(msg, int32, int, val);           break;
        case MPR_INT64:     LO_MESSAGE_ADD_VEC(msg, int64, int64_t, val);       break;
        case MPR_TIME:      LO_MESSAGE_ADD_VEC(msg, timetag, mpr_time, val);    break;
        case MPR_TYPE:      LO_MESSAGE_ADD_VEC(msg, char, mpr_type, val);       break;
        case 0:             lo_message_add_nil(msg);                            break;
        case MPR_BOOL:
            for (i = 0; i < len; i++) {
                if (((int*)val)[i])
                    lo_message_add_true(msg);
                else
                    lo_message_add_false(msg);
            }
            break;
        default:
            break;
    }
}

const char *mpr_prop_as_str(mpr_prop p, int skip_slash)
{
    const char *s;
    p = MASK_PROP_BITFLAGS(p);
    die_unless(p > MPR_PROP_UNKNOWN && p <= MPR_PROP_EXTRA,
               "called mpr_prop_as_str() with bad index %d.\n", p);
    s = static_props[PROP_TO_INDEX(p)].key;
    return skip_slash ? s + 1 : s;
}

mpr_prop mpr_prop_from_str(const char *string)
{
    /* property keys are stored alphabetically so we can use a binary search */
    int beg = PROP_TO_INDEX(MPR_PROP_UNKNOWN) + 1;
    int end = PROP_TO_INDEX(MPR_PROP_EXTRA) - 1;
    int mid = (beg + end) * 0.5, cmp;
    while (beg <= end) {
        cmp = strcmp(string, static_props[mid].key + 1);
        if (cmp > 0)
            beg = mid + 1;
        else if (cmp == 0)
            return INDEX_TO_PROP(mid);
        else
            end = mid - 1;
        mid = (beg + end) * 0.5;
    }
    if (strcmp(string, "expression")==0)
        return MPR_PROP_EXPR;
    if (strcmp(string, "maximum")==0)
        return MPR_PROP_MAX;
    if (strcmp(string, "minimum")==0)
        return MPR_PROP_MIN;
    return MPR_PROP_EXTRA;
}

const char *mpr_loc_as_str(mpr_loc loc)
{
    if (loc <= 0 || loc > MPR_LOC_ANY)
        return "unknown";
    return mpr_loc_strings[loc];
}

mpr_loc mpr_loc_from_str(const char *str)
{
    int i;
    RETURN_ARG_UNLESS(str, MPR_LOC_UNDEFINED);
    for (i = MPR_LOC_UNDEFINED+1; i < 3; i++) {
        if (strcmp(str, mpr_loc_strings[i])==0)
            return i;
    }
    return MPR_LOC_UNDEFINED;
}

const char *mpr_protocol_as_str(mpr_proto p)
{
    if (p <= 0 || p > MPR_NUM_PROTO)
        return "unknown";
    return mpr_protocol_strings[p];
}

mpr_proto mpr_protocol_from_str(const char *str)
{
    int i;
    RETURN_ARG_UNLESS(str, MPR_PROTO_UNDEFINED);
    for (i = MPR_PROTO_UNDEFINED+1; i < MPR_NUM_PROTO; i++) {
        if (strcmp(str, mpr_protocol_strings[i])==0)
            return i;
    }
    return MPR_PROTO_UNDEFINED;
}

const char *mpr_steal_as_str(mpr_steal_type stl)
{
    if (stl < MPR_STEAL_NONE || stl > MPR_STEAL_NEWEST)
        return "unknown";
    return mpr_steal_strings[stl];
}

/* Helper for setting property value from different data types */
int set_coerced_val(int src_len, mpr_type src_type, const void *src_val,
                    int dst_len, mpr_type dst_type, void *dst_val)
{
    int i, j, min_len = src_len < dst_len ? src_len : dst_len;

    if (src_type == dst_type) {
        int size = mpr_type_get_size(src_type);
        do {
            memcpy(dst_val, src_val, size * min_len);
            dst_len -= min_len;
            dst_val = (void*)((char*)dst_val + size * min_len);
            if (dst_len < min_len)
                min_len = dst_len;
        } while (dst_len > 0);
        return 0;
    }

    switch (dst_type) {
        case MPR_FLT:{
            float *dstf = (float*)dst_val;
            switch (src_type) {
                case MPR_INT32: {
                    int *srci = (int*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstf[i] = (float)srci[j];
                    }
                    break;
                }
                case MPR_DBL: {
                    double *srcd = (double*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstf[i] = (float)srcd[j];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case MPR_INT32:{
            int *dsti = (int*)dst_val;
            switch (src_type) {
                case MPR_FLT: {
                    float *srcf = (float*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dsti[i] = (int)srcf[j];
                    }
                    break;
                }
                case MPR_DBL: {
                    double *srcd = (double*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dsti[i] = (int)srcd[j];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case MPR_DBL:{
            double *dstd = (double*)dst_val;
            switch (src_type) {
                case MPR_INT32: {
                    int *srci = (int*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstd[i] = (float)srci[j];
                    }
                    break;
                }
                case MPR_FLT: {
                    float *srcf = (float*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstd[i] = (double)srcf[j];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        default:
            return -1;
    }
    return 0;
}

int match_pattern(const char* s, const char* p)
{
    int ends_wild;
    char *str, *tok, *pat;
    RETURN_ARG_UNLESS(s && p, 1);
    RETURN_ARG_UNLESS(strchr(p, '*'), strcmp(s, p));

    /* 1) tokenize pattern using strtok() with delimiter character '*'
     * 2) use strstr() to check if token exists in offset string */
    str = (char*)s;
    pat = alloca((strlen(p) + 1) * sizeof(char));
    strcpy(pat, p);
    ends_wild = ('*' == p[strlen(p)-1]);
    while (str && *str) {
        tok = strtok(pat, "*");
        RETURN_ARG_UNLESS(tok, !ends_wild);
        str = strstr(str, tok);
        if (str && *str)
            str += strlen(tok);
        else
            return 1;
            /* subsequent calls to strtok() need first argument to be NULL */
        pat = NULL;
    }
    return 0;
}

void mpr_prop_print(int len, mpr_type type, const void *val)
{
    int i;
    if (!val || len < 1) {
        printf("NULL");
        return;
    }

    if (len > 1)
        printf("[");

    switch (type) {
        case MPR_STR:
            if (len == 1)
                printf("'%s', ", (char*)val);
            else {
                for (i = 0; i < len; i++)
                    printf("'%s', ", ((char**)val)[i]);
            }
            break;
        case MPR_FLT:
            for (i = 0; i < len; i++)
                printf("%f, ", ((float*)val)[i]);
            break;
        case MPR_INT32:
            for (i = 0; i < len; i++)
                printf("%d, ", ((int*)val)[i]);
            break;
        case MPR_BOOL:
            for (i = 0; i < len; i++)
                printf("%c, ", ((int*)val)[i] ? 'T' : 'F');
            break;
        case MPR_DBL:
            for (i = 0; i < len; i++)
                printf("%f, ", ((double*)val)[i]);
            break;
        case MPR_INT64:
            for (i = 0; i < len; i++)
                printf("%" PR_MPR_INT64 ", ", ((int64_t*)val)[i]);
            break;
        case MPR_TIME:
            for (i = 0; i < len; i++)
                printf("%f, ", mpr_time_as_dbl(((mpr_time*)val)[i]));
            break;
        case MPR_TYPE:
            for (i = 0; i < len; i++)
                printf("%c, ", ((mpr_type*)val)[i]);
            break;
        case MPR_PTR:
            if (len == 1)
                printf("%p, ", val);
            else {
                for (i = 0; i < len; i++)
                    printf("%p, ", ((void**)val)[i]);
            }
            break;
        case MPR_DEV:
            /* just print device name */
            if (1 == len) {
                mpr_dev dev = (mpr_dev)val;
                printf("'%s%s', ", mpr_dev_get_name(dev), dev->is_local ? "*" : "");
            }
            else {
                mpr_dev *dev = (mpr_dev*)val;
                for (i = 0; i < len; i++)
                    printf("'%s%s', ", mpr_dev_get_name(dev[i]), dev[i]->is_local ? "*" : "");
            }
            break;
        case MPR_SIG: {
            /* just print signal name */
            if (1 == len) {
                mpr_sig sig = (mpr_sig)val;
                printf("'%s:%s%s', ", mpr_dev_get_name(sig->dev), sig->name,
                       sig->is_local ? "*" : "");
            }
            else {
                mpr_sig *sig = (mpr_sig*)val;
                for (i = 0; i < len; i++)
                    printf("'%s:%s%s', ", mpr_dev_get_name(sig[i]->dev), sig[i]->name,
                           sig[i]->is_local ? "*" : "");
            }
            break;
        }
        case MPR_LIST: {
            mpr_list list = mpr_list_start(mpr_list_get_cpy((mpr_list)val));
            if (!list || !*list || !(len = mpr_list_get_size(list))) {
                printf("[], ");
                break;
            }
            if (len > 1)
                printf("[");
            while (list) {
                if (!*list)
                    printf("null");
                else
                    mpr_prop_print(1, (*list)->type, *list);
                printf(", ");
                list = mpr_list_get_next(list);
            }
        }
        default:
            break;
    }

    if (len > 1)
        printf("\b\b] \b");
    else
        printf("\b\b \b");
}
