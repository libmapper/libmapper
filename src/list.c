#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

#include "mapper_internal.h"

/*   Note on the trick used here: Presuming that we can have lists as the result
 * of a search query, we need to be able to return a linked list composed of
 * pointers to arbitrary items.  However a very common operation will be to walk
 * through all the entries.  We prepend a header containing a pointer to the
 * next item and a pointer to the current item, and return the address of the
 * current pointer.
 *   In the normal case, the address of the self-pointer can therefore be used
 * to locate the actual entry, and we can walk along the actual list without
 * needing to allocate any memory for a list header.  However, in the case where
 * we want to walk along the result of a query, we can allocate a dynamic set of
 * list headers, and still have 'self' point to the actual item.
 *   Both types of queries can use the returned double-pointer as context for
 * the search as well as for returning the desired value. This allows us to
 * avoid requiring the user to manage a separate iterator object. */

typedef enum {
    OP_UNION,
    OP_INTERSECTION,
    OP_DIFFERENCE
} binary_op_t;

typedef enum {
    QUERY_STATIC    = 1,
    QUERY_DYNAMIC   = 2
} query_type_t;

typedef struct {
    void *next;
    void *self;
    void **start;
    struct _query_info *query_ctx;
    query_type_t query_type;
    int data[1]; /* stub */
}  mpr_list_header_t;

/*! Function for query comparison */
typedef int query_compare_func_t(const void *ctx_data, const void *item);

/*! Function for freeing query context */
typedef void query_free_func_t(mpr_list_header_t *lh);

/*! Function for handling parallel queries. */
static int cmp_parallel_query(const void *ctx_data, const void *dev);

/*! Contains some function pointers and data for handling query context. */
typedef struct _query_info {
    unsigned int size;
    query_compare_func_t *query_compare;
    query_free_func_t *query_free;
    int *data; /* stub */
} query_info_t;

#define LIST_HEADER_SIZE (sizeof(mpr_list_header_t)-sizeof(int[1]))

/*! Reserve memory for a list item.  Reserves an extra pointer at the
 *  beginning of the structure to allow for a list pointer. */
static mpr_list_header_t* mpr_list_new_item(size_t size)
{
    mpr_list_header_t *lh=0;

    /* make sure the compiler is doing what we think it's doing with
     * the size of mpr_list_header_t and location of data */
    die_unless(LIST_HEADER_SIZE == sizeof(void*)*4 + sizeof(query_type_t),
               "unexpected size for mpr_list_header_t");
    die_unless(LIST_HEADER_SIZE == ((char*)&lh->data - (char*)lh),
               "unexpected offset for data in mpr_list_header_t");

    size += LIST_HEADER_SIZE;
    lh = calloc(1, size);
    RETURN_ARG_UNLESS(lh, 0);
    lh->self = &lh->data;
    lh->start = &lh->self;
    lh->query_type = QUERY_STATIC;

    return (mpr_list_header_t*)&lh->data;
}

/*! Get the list header for memory returned from mpr_list_new_item(). */
static mpr_list_header_t* mpr_list_header_by_data(const void *data)
{
    return (mpr_list_header_t*)((char*)data - LIST_HEADER_SIZE);
}

/*! Get the list header for memory returned from mpr_list_new_item(). */
static mpr_list_header_t* mpr_list_header_by_self(void *self)
{
    mpr_list_header_t *lh = 0;
    return (mpr_list_header_t*)((char*)self - ((char*)&lh->self - (char*)lh));
}

void *mpr_list_from_data(const void *data)
{
    mpr_list_header_t *lh;
    RETURN_ARG_UNLESS(data, 0);
    lh = mpr_list_header_by_data(data);
    die_unless(lh->self == &lh->data, "bad self pointer in list structure");
    return &lh->self;
}

/*! Set the next pointer in memory returned from mpr_list_new_item(). */
static void mpr_list_set_next(void *mem, void *next)
{
    mpr_list_header_by_data(mem)->next = next;
}

/*! Get the next pointer in memory returned from mpr_list_new_item(). */
static void* mpr_list_get_next_internal(void *mem)
{
    return mpr_list_header_by_data(mem)->next;
}

/*! Prepend an item to the beginning of a list. */
static void *mpr_list_prepend_item(void *item, void **list)
{
    mpr_list_set_next(item, *list);
    *list = item;
    return item;
}

void *mpr_list_add_item(void **list, size_t size)
{
    mpr_list_header_t* lh = mpr_list_new_item(size);
    mpr_list_prepend_item(lh, list);
    return lh;
}

/*! Remove an item from a list but do not free its memory. */
void mpr_list_remove_item(void **head, void *item)
{
    void *prev_node = 0, *node = *head;
    while (node) {
        if (node == item)
            break;
        prev_node = node;
        node = mpr_list_get_next_internal(node);
    }
    RETURN_UNLESS(node);
    if (prev_node)
        mpr_list_set_next(prev_node, mpr_list_get_next_internal(node));
    else
        *head = mpr_list_get_next_internal(node);
}

/*! Free the memory used by a list item */
void mpr_list_free_item(void *item)
{
    if (item)
        free(mpr_list_header_by_data(item));
}

/** Structures and functions for performing dynamic queries **/

/* Here are some generalized routines for dealing with typical context
 * format and query continuation. Functions specific to particular
 * queries are defined further down with their compare operation. */

void **mpr_list_query_continuation(mpr_list_header_t *lh)
{
    void *item = mpr_list_header_by_data(lh->self)->next;
    while (item) {
        if (lh->query_ctx->query_compare(&lh->query_ctx->data, item))
            break;
        item = mpr_list_get_next_internal(item);
    }

    if (item) {
        lh->self = item;
        return &lh->self;
    }

    /* Clean up */
    if (lh->query_ctx->query_free)
        lh->query_ctx->query_free(lh);
    return 0;
}

static void free_query_single_ctx(mpr_list_header_t *lh)
{
    if (cmp_parallel_query == lh->query_ctx->query_compare) {
        /* this is a parallel query – we need to free components also */
        void *data = &lh->query_ctx->data;
        mpr_list_header_t *lh1 = *(mpr_list_header_t**)data;
        mpr_list_header_t *lh2 = *(mpr_list_header_t**)((char*)data + sizeof(void*));
        free_query_single_ctx(lh1);
        free_query_single_ctx(lh2);
    }
    free(lh->query_ctx);
    free(lh);
}

#define GET_TYPE_SIZE(TYPE)                 \
if (types[i+1] && isdigit(types[i+1])) {    \
    num_args = atoi(types+i+1);             \
    va_arg(aq, TYPE*);                      \
    ++i;                                    \
}                                           \
else {                                      \
    num_args = 1;                           \
    va_arg(aq, TYPE);                       \
}                                           \
size += num_args * sizeof(TYPE);

static int get_query_size(const char *types, va_list aq)
{
    int i = 0, j, size = 0, num_args;
    RETURN_ARG_UNLESS(types, 0);
    while (types[i]) {
        switch (types[i]) {
            case MPR_INT32:
            case MPR_TYPE: /* store as int to avoid alignment problems */
                GET_TYPE_SIZE(int);
                break;
            case MPR_INT64:
                GET_TYPE_SIZE(int64_t);
                break;
            case MPR_PTR:
                GET_TYPE_SIZE(void*);
                break;
            case MPR_STR: /* special case */
                if (types[i+1] && isdigit(types[i+1])) {
                    const char **val = va_arg(aq, const char**);
                    num_args = atoi(types+i+1);
                    for (j = 0; j < num_args; j++)
                        size += strlen(val[j]) + 1;
                    ++i;
                }
                else {
                    const char *val = va_arg(aq, const char*);
                    size += (val ? strlen(val) : 0) + 1;
                }
                break;
            default:
                return 0;
        }
        ++i;
    };
    return size;
}

#define COPY_TYPE(TYPE)                                 \
if (types[i+1] && isdigit(types[i + 1])) {              \
    TYPE *val = (TYPE*)va_arg(aq, TYPE*);               \
    num_args = atoi(types + i + 1);                     \
    memcpy(data + offset, val, sizeof(TYPE) * num_args);\
    ++i;                                                \
}                                                       \
else {                                                  \
    TYPE val = (TYPE)va_arg(aq, TYPE);                  \
    num_args = 1;                                       \
    memcpy(data + offset, &val, sizeof(TYPE));          \
}                                                       \
offset += sizeof(TYPE) * num_args;

/* We need to be careful of memory alignment here - for now we will just ensure
 * that string arguments are always passed last. */
static void **new_query_internal(const void **list, int size, const void *func,
                                 const char *types, va_list aq)
{
    mpr_list_header_t *lh;
    int offset = 0, i = 0, j, num_args;
    char *data;
    RETURN_ARG_UNLESS(list && size && func && types, 0);
    lh = (mpr_list_header_t*)malloc(LIST_HEADER_SIZE);
    lh->next = (void*)mpr_list_query_continuation;
    lh->query_type = QUERY_DYNAMIC;
    lh->query_ctx = (query_info_t*)malloc(sizeof(query_info_t)+size);

    data = (char*)&lh->query_ctx->data;
    while (types[i]) {
        switch (types[i]) {
            case MPR_INT32:
            case MPR_TYPE: /* store char as int to avoid alignment problems */
                COPY_TYPE(int);
                break;
            case MPR_INT64:
                COPY_TYPE(int64_t);
                break;
            case MPR_PTR: {
                void *val = va_arg(aq, void**);
                if (types[i+1] && isdigit(types[i + 1])) {
                    /* is array */
                    num_args = atoi(types + i + 1);
                    ++i;
                }
                else
                    num_args = 1;
                memcpy(data + offset, val, sizeof(void*) * num_args);
                offset += sizeof(void**) * num_args;
                break;
            }
            case MPR_STR: /* special case */
                if (types[i+1] && isdigit(types[i + 1])) {
                    /* is array */
                    const char **val = (const char**)va_arg(aq, const char**);
                    num_args = atoi(types + i + 1);
                    for (j = 0; j < num_args; j++)
                        snprintf(data + offset, size - offset, "%s", val[j]);
                    offset += strlen(val[j]) + 1;
                    ++i;
                }
                else {
                    const char *val = (const char*)va_arg(aq, const char*);
                    snprintf(data + offset, size - offset, "%s", val);
                    offset += (val ? strlen(val) : 0) + 1;
                }
                break;
            default:
                free(lh->query_ctx);
                free(lh);
                return 0;
        }
        ++i;
    }

    lh->query_ctx->size = sizeof(query_info_t) + size;
    lh->query_ctx->query_compare = (query_compare_func_t*)func;
    lh->query_ctx->query_free = (query_free_func_t*)free_query_single_ctx;
    lh->start = (void**)list;
    lh->self = *lh->start;
    return &lh->self;
}

mpr_list mpr_list_new_query(const void **list, const void *func,
                            const char *types, ...)
{
    int size;
    va_list aq;
    mpr_list qry;
    va_start(aq, types);
    size = get_query_size(types, aq);
    va_end(aq);

    va_start(aq, types);
    qry = (mpr_list)new_query_internal(list, size, func, types, aq);
    va_end(aq);
    return qry;
}

mpr_list mpr_list_start(mpr_list list)
{
    mpr_list_header_t *lh;
    RETURN_ARG_UNLESS(list, 0);
    lh = mpr_list_header_by_self(list);
    lh->self = *lh->start;
    if (QUERY_DYNAMIC == lh->query_type) {
        if (!*list)
            return 0;
        if (lh->query_ctx->query_compare(&lh->query_ctx->data, *list))
            return (mpr_list)&lh->self;
        return (mpr_list)mpr_list_query_continuation(lh);
    }
    else
        return (mpr_list)&lh->self;
}

mpr_list mpr_list_get_next(mpr_list list)
{
    mpr_list_header_t *lh;
    if (!list) {
        trace("bad pointer in mpr_list_get_next()\n");
        return 0;
    }
    if (!*list) {
        trace("pointer in mpr_list_get_next() points nowhere\n");
        return 0;
    }

    lh = mpr_list_header_by_self(list);
    RETURN_ARG_UNLESS(lh->next, 0);

    if (QUERY_STATIC == lh->query_type)
        return (mpr_list)mpr_list_from_data(lh->next);
    else {
        /* Here we treat next as a pointer to a continuation function, so we can
         * return items from the graph computed lazily.  The context is
         * simply the string(s) to match.  In the future, it might point to the
         * results of a SQL query for example. */
        void **(*f) (mpr_list_header_t*) = (void **(*) (mpr_list_header_t*))lh->next;
        return (mpr_list)f(lh);
    }
    return 0;
}

void mpr_list_free(mpr_list list)
{
    mpr_list_header_t *lh;
    RETURN_UNLESS(list);
    lh = mpr_list_header_by_self(list);
    if (QUERY_DYNAMIC == lh->query_type && lh->query_ctx->query_free)
        lh->query_ctx->query_free(lh);
}

mpr_obj mpr_list_get_idx(mpr_list list, unsigned int idx)
{
    int i = 0;
    mpr_list_header_t *lh;
    RETURN_ARG_UNLESS(list && idx >= 0, 0);
    lh = mpr_list_header_by_self(list);

    /* Reset to beginning of list */
    lh->self = *lh->start;
    mpr_list_start(list);

    while (list) {
        if (i == idx)
            return *list;
        list = mpr_list_get_next(list);
        ++i;
    }
    return 0;
}

/* Functions for handling parallel queries: unions, intersections, etc. */
static int cmp_parallel_query(const void *ctx_data, const void *list)
{
    mpr_list_header_t *lh1 = *(mpr_list_header_t**)ctx_data;
    mpr_list_header_t *lh2 = *(mpr_list_header_t**)((char*)ctx_data + sizeof(void*));
    mpr_op op = *(mpr_op*)((char*)ctx_data + sizeof(void*) * 2);
    query_info_t *c1 = lh1->query_ctx, *c2 = lh2->query_ctx;

    int res1 = c1->query_compare(&c1->data, list);
    int res2 = c2->query_compare(&c2->data, list);
    switch (op) {
        case OP_UNION:          return res1 || res2;
        case OP_INTERSECTION:   return res1 && res2;
        case OP_DIFFERENCE:     return res1 && !res2;
        default:                return 0;
    }
}

static mpr_list_header_t *mpr_list_header_cpy(mpr_list_header_t *lh)
{
    mpr_list_header_t *cpy = (mpr_list_header_t*)malloc(LIST_HEADER_SIZE);
    memcpy(cpy, lh, LIST_HEADER_SIZE);
    RETURN_ARG_UNLESS(lh->query_ctx, cpy);

    cpy->query_ctx = (query_info_t*)malloc(lh->query_ctx->size);
    memcpy(cpy->query_ctx, lh->query_ctx, lh->query_ctx->size);

    if (cmp_parallel_query == cpy->query_ctx->query_compare) {
        /* this is a parallel query – we need to copy components */
        void *data = &cpy->query_ctx->data;
        mpr_list_header_t *lh1 = *(mpr_list_header_t**)data;
        mpr_list_header_t *lh2 = *(mpr_list_header_t**)((char*)data + sizeof(void*));
        lh1 = mpr_list_header_cpy(lh1);
        lh2 = mpr_list_header_cpy(lh2);
        memcpy(data, &lh1, sizeof(void*));
        memcpy((char*)data + sizeof(void*), &lh2, sizeof(void*));
        lh1 = *(mpr_list_header_t**)data;
        lh2 = *(mpr_list_header_t**)((char*)data + sizeof(void*));
    }
    return cpy;
}

mpr_list mpr_list_get_cpy(mpr_list list)
{
    mpr_list_header_t *lh, *cpy;
    RETURN_ARG_UNLESS(list, 0);
    lh = mpr_list_header_by_self(list);
    cpy = mpr_list_header_cpy(lh);
    return (mpr_list)&cpy->self;
}

mpr_list mpr_list_get_union(mpr_list list1, mpr_list list2)
{
    mpr_list_header_t *lh1, *lh2;
    RETURN_ARG_UNLESS(list1, list2);
    RETURN_ARG_UNLESS(list2, list1);
    lh1 = mpr_list_header_by_self(list1);
    lh2 = mpr_list_header_by_self(list2);
    return mpr_list_start(mpr_list_new_query((const void **)lh1->start, (void*)cmp_parallel_query,
                                             "vvi", &lh1, &lh2, OP_UNION));
}

mpr_list mpr_list_get_isect(mpr_list list1, mpr_list list2)
{
    mpr_list_header_t *lh1, *lh2;
    RETURN_ARG_UNLESS(list1 && list2, 0);
    lh1 = mpr_list_header_by_self(list1);
    lh2 = mpr_list_header_by_self(list2);
    return mpr_list_start(mpr_list_new_query((const void **)lh1->start, (void*)cmp_parallel_query,
                                             "vvi", &lh1, &lh2, OP_INTERSECTION));
}

static mpr_list mpr_list_filter_internal(mpr_list list, const void *func, const char *types, ...)
{
    int size;
    va_list aq;
    mpr_list_header_t *lh1, *lh2;
    void **filter;

    RETURN_ARG_UNLESS(list, 0);

    va_start(aq, types);
    size = get_query_size(types, aq);
    va_end(aq);

    lh1 = mpr_list_header_by_self(list);

    va_start(aq, types);
    filter = new_query_internal((const void **)lh1->start, size, func, types, aq);
    va_end(aq);

    if (QUERY_STATIC == lh1->query_type)
        return (mpr_list)filter;

    /* return intersection */
    lh2 = mpr_list_header_by_self(filter);
    return mpr_list_new_query((const void **)lh1->start, (void*)cmp_parallel_query,
                              "vvi", &lh1, &lh2, OP_INTERSECTION);
}

#define COMPARE_TYPE(TYPE)                      \
for (i = 0; i < len; i++) {                     \
    comp += ((TYPE*)v1)[i] > ((TYPE*)v2)[i];    \
    comp -= ((TYPE*)v1)[i] < ((TYPE*)v2)[i];    \
    diff += abs(comp);                          \
}

static int compare_val(mpr_op op, int len, mpr_type type,
                       const void *v1, const void *v2)
{
    int i, comp = 0, diff = 0;
    switch (type) {
        case MPR_STR:
            if (1 == len)
                comp = match_pattern((const char*)v1, (const char*)v2);
            else {
                for (i = 0; i < len; i++) {
                    comp += match_pattern(((const char**)v1)[i], ((const char**)v2)[i]);
                    diff += abs(comp);
                }
            }
            break;
        case MPR_INT32:
            COMPARE_TYPE(int);
            break;
        case MPR_FLT:
            COMPARE_TYPE(float);
            break;
        case MPR_DBL:
            COMPARE_TYPE(double);
            break;
        case MPR_TYPE:
            COMPARE_TYPE(mpr_type);
            break;
        case MPR_INT64:
        case MPR_TIME:
            COMPARE_TYPE(uint64_t);
            break;
        case MPR_PTR:
        case MPR_DEV:
        case MPR_SIG:
        case MPR_MAP:
        case MPR_OBJ:
            if (1 == len)
                comp = ((void*)v1 > (void*)v2) - ((void*)v1 < (void*)v2);
            else {
                COMPARE_TYPE(void*);
            }
            break;
        default:
            return 0;
    }
    switch (op) {
        case MPR_OP_EQ:     return (0 == comp) && !diff;
        case MPR_OP_GT:     return comp > 0;
        case MPR_OP_GTE:    return comp >= 0;
        case MPR_OP_LT:     return comp < 0;
        case MPR_OP_LTE:    return comp <= 0;
        case MPR_OP_NEQ:    return comp != 0 || diff;
        default:            return 0;
    }
}

static int filter_by_prop(const void *ctx, mpr_obj o)
{
    mpr_prop p =      *(int*)       ((char*)ctx);
    mpr_op op =       *(int*)       ((char*)ctx + sizeof(int));
    int len =         *(int*)       ((char*)ctx + sizeof(int)*2);
    mpr_type type =   *(mpr_type*)  ((char*)ctx + sizeof(int)*3);
    void *val =       *(void**)     ((char*)ctx + sizeof(int)*4);
    const char *key =  (const char*)((char*)ctx + sizeof(int)*4 + sizeof(void*));
    int _len;
    mpr_type _type;
    const void *_val;
    if (key && key[0])
        p = mpr_obj_get_prop_by_key(o, key, &_len, &_type, &_val, 0);
    else
        p = mpr_obj_get_prop_by_idx(o, p, NULL, &_len, &_type, &_val, 0);
    if (MPR_PROP_UNKNOWN == p)
        return MPR_OP_NEX == op;
    if (MPR_OP_EX == op)
        return 1;
    if (MPR_LIST == _type) {
        mpr_list l;
        if (op < MPR_OP_ANY)
            return 0;
        /* use a copy of the list */
        l = mpr_list_get_cpy((mpr_list)_val);
        while (l) {
            if (mpr_obj_get_type((mpr_obj)*l) != type) {
                mpr_list_free(l);
                return 0;
            }
            else if (*l == val && MPR_OP_ANY == op) {
                mpr_list_free(l);
                return 1;
            }
            l = mpr_list_get_next(l);
        }
        return MPR_OP_NONE == op;
    }
    else if (_type != type || (op < MPR_OP_ALL && _len != len))
        return 0;
    return compare_val(op, len, type, _val, val);
}

mpr_list mpr_list_filter(mpr_list list, mpr_prop p, const char *key, int len,
                         mpr_type type, const void *val, mpr_op op)
{
    int mask = MPR_OP_ALL | MPR_OP_ANY;
    if (!list || op <= MPR_OP_UNDEFINED || (op | mask) > (MPR_OP_NEQ | mask))
        return list;
    return mpr_list_start(mpr_list_filter_internal(list, (void*)filter_by_prop, "iiicvs", p, op,
                                                   len, type, &val, key));
}

mpr_list mpr_list_get_diff(mpr_list list1, mpr_list list2)
{
    mpr_list_header_t *lh1, *lh2;
    RETURN_ARG_UNLESS(list1, 0);
    RETURN_ARG_UNLESS(list2, list1);
    lh1 = mpr_list_header_by_self(list1);
    lh2 = mpr_list_header_by_self(list2);
    return mpr_list_start(mpr_list_new_query((const void **)lh1->start, (void*)cmp_parallel_query,
                                             "vvi", &lh1, &lh2, OP_DIFFERENCE));
}

int mpr_list_get_size(mpr_list list)
{
    int count = 1;
    mpr_list_header_t *lh;
    RETURN_ARG_UNLESS(list, 0);
    lh = mpr_list_header_by_self(list);
    RETURN_ARG_UNLESS(lh->start && *lh->start, 0);
    if (QUERY_DYNAMIC == lh->query_type) {
        /* use a copy */
        list = mpr_list_get_cpy(list);
        while ((list = mpr_list_get_next(list)))
            ++count;
    }
    else {
        /* cache list position */
        void *idx = lh->self;
        lh->self = *lh->start;
        while ((list = mpr_list_get_next(list)))
            ++count;
        /* restore list position */
        lh->self = idx;
    }
    return count;
}
