
#ifndef __MPR_STRING_H__
#define __MPR_STRING_H__

#include "util/mpr_inline.h"

int mpr_path_match(const char* s, const char* p);

/*! Parse the device and signal names from an OSC path. */
int mpr_path_parse(const char *string, char **devnameptr, char **signameptr);

/*! Helper to remove a leading slash '/' from a string. */
MPR_INLINE static const char *mpr_path_skip_slash(const char *string)
{
    return string + (string && string[0]=='/');
}

#endif /* __MAPPER_STRING_H__ */
