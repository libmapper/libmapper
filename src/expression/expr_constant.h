#ifndef __MPR_EXPR_CONSTANT_H__
#define __MPR_EXPR_CONSTANT_H__

/* Const special flags */
#define CONST_MINVAL    0x0001
#define CONST_MAXVAL    0x0002
#define CONST_PI        0x0003
#define CONST_E         0x0004
#define CONST_SPECIAL   0x0007

static int const_lookup(const char *s, int len)
{
    if (len == 2 && 'p' == *s && 'i' == *(s+1))
        return CONST_PI;
    else if (len == 1 && *s == 'e')
        return CONST_E;
    else
        return 0;
}

#endif /* __MPR_EXPR_CONSTANT_H__ */
