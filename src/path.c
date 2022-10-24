#include <stdlib.h>
#include <string.h>
#include "path.h"
#include "util/mpr_debug.h"

#ifdef _MSC_VER
#include <malloc.h>
#endif

int mpr_path_parse(const char *string, char **devnameptr, char **signameptr)
{
    char *devname, *signame;
    RETURN_ARG_UNLESS(string, 0);
    devname = (char*)mpr_path_skip_slash(string);
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

int mpr_path_match(const char* s, const char* p)
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
