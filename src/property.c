#include <string.h>
#include <stdio.h>

#include "device.h"
#include "list.h"
#include "object.h"
#include "property.h"
#include "util/mpr_debug.h"
#include <mapper/mapper.h>

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

/* Warning! This table needs to be kept synchronised with mpr_prop enum
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

const char* mpr_dir_strings[] =
{
    NULL,           /* MPR_DIR_UNDEFINED */
    "input",        /* MPR_DIR_IN */
    "output",       /* MPR_DIR_OUT */
    "any",          /* MPR_DIR_ANY */
    "both",         /* MPR_DIR_BOTH */
};

const char* mpr_loc_strings[] =
{
    NULL,           /* MPR_LOC_UNDEFINED */
    "src",          /* MPR_LOC_SRC */
    "dst",          /* MPR_LOC_DST */
    "any",          /* MPR_LOC_ANY */
    "both",         /* MPR_LOC_ANY */
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

int mpr_prop_get_len(mpr_prop p)
{
    p = MASK_PROP_BITFLAGS(p);
    die_unless(p > MPR_PROP_UNKNOWN && p <= MPR_PROP_EXTRA,
               "called mpr_prop_get_len() with bad index %d.\n", p);
    return static_props[PROP_TO_INDEX(p)].len;
}

int mpr_prop_get_protocol_type(mpr_prop p)
{
    p = MASK_PROP_BITFLAGS(p);
    die_unless(p > MPR_PROP_UNKNOWN && p <= MPR_PROP_EXTRA,
               "called mpr_prop_get_len() with bad index %d.\n", p);
    return static_props[PROP_TO_INDEX(p)].protocol_type;
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

const char *mpr_dir_as_str(mpr_dir dir)
{
    switch (dir) {
        case MPR_DIR_IN:
        case MPR_DIR_OUT:
        case MPR_DIR_ANY:
            return mpr_dir_strings[dir];
        case MPR_DIR_BOTH:
            return mpr_dir_strings[4];
        default:
            return "unknown";
    }
}

mpr_dir mpr_dir_from_str(const char *str)
{
    int i;
    RETURN_ARG_UNLESS(str, MPR_DIR_UNDEFINED);
    for (i = MPR_DIR_UNDEFINED + 1; i < 3; i++) {
        if (strcmp(str, mpr_dir_strings[i]) == 0)
            return i;
    }
    if (strcmp(str, mpr_dir_strings[4]) == 0)
        return MPR_DIR_BOTH;
    return MPR_DIR_UNDEFINED;
}

const char *mpr_loc_as_str(mpr_loc loc)
{
    switch (loc) {
        case MPR_LOC_SRC:
        case MPR_LOC_DST:
        case MPR_LOC_ANY:
            return mpr_loc_strings[loc];
        case MPR_LOC_BOTH:
            return mpr_loc_strings[4];
        default:
            return "unknown";
    }
}

mpr_loc mpr_loc_from_str(const char *str)
{
    int i;
    RETURN_ARG_UNLESS(str, MPR_LOC_UNDEFINED);
    for (i = MPR_LOC_UNDEFINED + 1; i < 3; i++) {
        if (strcmp(str, mpr_loc_strings[i]) == 0)
            return i;
    }
    if (strcmp(str, mpr_loc_strings[4]) == 0)
        return MPR_LOC_BOTH;
    return MPR_LOC_UNDEFINED;
}

const char *mpr_proto_as_str(mpr_proto p)
{
    if (p <= 0 || p > MPR_NUM_PROTO)
        return "unknown";
    return mpr_protocol_strings[p];
}

mpr_proto mpr_proto_from_str(const char *str)
{
    int i;
    RETURN_ARG_UNLESS(str, MPR_PROTO_UNDEFINED);
    for (i = MPR_PROTO_UNDEFINED+1; i < MPR_NUM_PROTO; i++) {
        if (strcmp(str, mpr_protocol_strings[i])==0)
            return i;
    }
    return MPR_PROTO_UNDEFINED;
}

const char *mpr_steal_type_as_str(mpr_steal_type stl)
{
    if (stl < MPR_STEAL_NONE || stl > MPR_STEAL_NEWEST)
        return "unknown";
    return mpr_steal_strings[stl];
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
                mpr_dev d = (mpr_dev)val;
                printf("'%s%s', ", mpr_dev_get_name(d), mpr_obj_get_is_local((mpr_obj)d) ? "*" : "");
            }
            else {
                mpr_dev *d = (mpr_dev*)val;
                for (i = 0; i < len; i++)
                    printf("'%s%s', ", mpr_dev_get_name(d[i]),
                           mpr_obj_get_is_local((mpr_obj)d[i]) ? "*" : "");
            }
            break;
        case MPR_SIG: {
            /* just print signal name */
            if (1 == len) {
                mpr_sig s = (mpr_sig)val;
                printf("'%s:%s%s', ", mpr_dev_get_name(mpr_sig_get_dev(s)), mpr_sig_get_name(s),
                       mpr_obj_get_is_local((mpr_obj)s) ? "*" : "");
            }
            else {
                mpr_sig *s = (mpr_sig*)val;
                for (i = 0; i < len; i++)
                    printf("'%s:%s%s', ", mpr_dev_get_name(mpr_sig_get_dev(s[i])),
                           mpr_sig_get_name(s[i]), mpr_obj_get_is_local((mpr_obj)s[i]) ? "*" : "");
            }
            break;
        }
        case MPR_LINK: {
            mpr_link l = (mpr_link)val;
            if (1 != len)
                break;
            mpr_prop_print(1, MPR_DEV, mpr_link_get_dev(l, 0));
            printf(" <-> ");
            mpr_prop_print(1, MPR_DEV, mpr_link_get_dev(l, 1));
            break;
        }
        case MPR_MAP: {
            /* just print signal names */
            mpr_map m = (mpr_map)val;
            int num_src = mpr_map_get_num_src(m);
            if (1 != len)
                break;
            if (num_src > 1)
                printf("[");
            for (i = 0; i < num_src; i++) {
                mpr_prop_print(1, MPR_SIG, mpr_map_get_src_sig(m, i));
                printf(", ");
            }
            printf("\b\b");
            if (num_src > 1)
                printf("]");
            printf(" -> ");
            mpr_prop_print(1, MPR_SIG, mpr_map_get_dst_sig(m));
            printf(", ");
            break;
        }
        case MPR_LIST: {
            mpr_list list = (mpr_list)val;
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
            break;
        }
        default:
            break;
    }

    if (len > 1)
        printf("\b\b] \b");
    else
        printf("\b\b \b");
}
