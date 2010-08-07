
#include <string.h>

#include "mapper_internal.h"

/*! Initialization of the global list of local devices. */
mapper_db_registered g_db_registered = NULL;

/*! Add the information got by the /registered message to the global
 *  list of registered devices information */
static void mdev_add_registered(char *full_name, char *host, int port,
                                char *canAlias)
{
    mapper_db_registered reg =
        (mapper_db_registered)malloc(sizeof(mapper_db_registered*));

    reg->full_name = strdup(full_name);
    reg->host = strdup(host);
    reg->port = port;
    reg->canAlias = strdup(canAlias);
    reg->next = g_db_registered;
    g_db_registered = reg;

    trace("new registered device: name=%s, host=%s, port=%d, "
          "canAlias=%s\n", full_name, host, port, canAlias);
}

int mapper_db_add(char *full_name, char *host, int port, char *canAlias)
{
    mapper_db_registered reg = mapper_db_find(full_name);
    if (!reg)
        mdev_add_registered(full_name, host, port, canAlias);
    return 1;
}

mapper_db_registered mapper_db_find(char *name)
{
    mapper_db_registered reg = g_db_registered;
    while (reg) {
        if (strcmp(reg->full_name, name)==0)
            return reg;
    }
    return 0;
}

void mapper_db_dump()
{
    mapper_db_registered reg = g_db_registered;
    trace("Registered devices:\n");
    while (reg) {
        trace("  name=%s, host=%s, port=%d, canAlias=%s\n",
              reg->full_name, reg->host,
              reg->port, reg->canAlias);
        reg = reg->next;
    }
}
