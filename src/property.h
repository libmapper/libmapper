
#ifndef __MAPPER_PROPERTY_H__
#define __MAPPER_PROPERTY_H__

/*! Helper for printing typed values.
 *  \param len          The vector length of the value.
 *  \param type         The value type.
 *  \param val          A pointer to the property value to print. */
void mpr_prop_print(int len, mpr_type type, const void *val);

mpr_prop mpr_prop_from_str(const char *str);

const char *mpr_prop_as_str(mpr_prop prop, int skip_slash);

#endif /* __MAPPER_PROPERTY_H__ */
