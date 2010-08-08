
#include <string.h>

#include "types_internal.h"
#include "mapper_internal.h"

const char* mapper_msg_param_strings[] =
{
    "@IP",         /* AT_IP */
    "@port",       /* AT_PORT */
    "@canAlias",   /* AT_CANALIAS */
    "@numInputs",  /* AT_NUMINPUTS */
    "@numOutputs", /* AT_NUMOUTPUTS */
    "@hash",       /* AT_HASH */
    "@type",       /* AT_TYPE */
    "@min",        /* AT_MIN */
    "@max",        /* AT_MAX */
    "@scaling",    /* AT_SCALING */
    "@expression", /* AT_EXPRESSION */
    "@clipMin",    /* AT_CLIPMIN */
    "@clipMax",    /* AT_CLIPMAX */
    "@range",      /* AT_RANGE */
};

int mapper_msg_parse_params(mapper_message_t *msg,
                            const char *path, const char *types,
                            int argc, lo_arg **argv)
{
    int i, j;

    /* Sanity check: complain loudly and quit string if number of
     * strings and params doesn't match up. */
    die_unless(sizeof(mapper_msg_param_strings)/sizeof(const char*)
               == N_AT_PARAMS,
               "libmapper ERROR: wrong number of known parameters\n");

    memset(msg, 0, sizeof(mapper_message_t));
    msg->path = path;

    for (i=0; i<argc; i++) {
        if (types[i]!='s') {
            /* parameter ID not a string */
#ifdef DEBUG
            trace("message %s, parameter '", path);
            lo_arg_pp(types[i], argv[i]);
            trace("' not a string.\n");
#endif
            return 1;
        }

        for (j=0; j<N_AT_PARAMS; j++)
            if (strcmp(&argv[i]->s, mapper_msg_param_strings[j])==0)
                break;

        if (j==N_AT_PARAMS) {
            /* unknown parameter */
            trace("message %s, unknown parameter '%s'\n",
                  path, &argv[i]->s);
            return 1;
        }

        /* special case: range has 4 float or int parameters */
        // TODO: handle 'invert' and '-'
        if (j==AT_RANGE) {
            int k;
            msg->values[j] = &argv[i+1];
            for (k=0; k<4; k++) {
                i++;
                if (i >= argc) {
                    trace("message %s, not enough parameters "
                          "for @range.\n", path);
                    return 1;
                }
                if (types[i] != 'i' && types[i] != 'f') {
                    /* range parameter bad type */
#ifdef DEBUG
                    trace("message %s, @range parameter ", path);
                    lo_arg_pp(types[i], argv[i]);
                    trace("not float or int\n");
#endif
                    return 1;
                }
            }
        }
        else {
            msg->values[j] = &argv[++i];
            if (i >= argc) {
                trace("message %s, not enough parameters for %s\n",
                      path, &argv[i-1]->s);
                return 1;
            }
        }
    }
    return 0;
}

lo_arg** mapper_msg_get_param(mapper_message_t *msg,
                              mapper_msg_param_t param)
{
    die_unless(param >= 0 && param < N_AT_PARAMS,
               "error, unknown parameter in mapper_msg_get_param()\n");
    return msg->values[param];
}
