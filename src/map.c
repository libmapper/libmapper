#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "mpr_internal.h"
#include "types_internal.h"
#include <mpr/mpr.h>

#define MAX_LEN 512
#define MPR_STATUS_LENGTH_KNOWN 0x04
#define MPR_STATUS_TYPE_KNOWN   0x08
#define MPR_STATUS_LINK_KNOWN   0x10
#define METADATA_OK             0x1C

/*! Function prototypes. */
static void reallocate_map_histories(mpr_map m);

static int alphabetise_sigs(int num, mpr_sig *s, int *o)
{
    int i, j, res1 = 1, res2 = 1;
    for (i = 0; i < num; i++)
        o[i] = i;
    for (i = 1; i < num; i++) {
        j = i-1;
        while (j >= 0) {
            res1 = strcmp(s[o[j]]->dev->name, s[o[j+1]]->dev->name);
            if (res1 < 0)
                break;
            else if (0 == res1) {
                res2 = strcmp(s[o[j]]->name, s[o[j+1]]->name);
                if (0 == res2) {
                    // abort: identical signal names
                    return 1;
                }
                else if (res2 < 0)
                    break;
            }
            // swap
            int temp = o[j];
            o[j] = o[j+1];
            o[j+1] = temp;
            j--;
        }
    }
    return 0;
}

static int cmp_qry_map_scopes(const void *ctx, mpr_dev d)
{
    mpr_map m = *(mpr_map*)ctx;
    for (int i = 0; i < m->num_scopes; i++) {
        if (!m->scopes[i] || m->scopes[i]->obj.id == d->obj.id)
            return 1;
    }
    return 0;
}

void mpr_map_init(mpr_map m)
{
    m->obj.props.mask = 0;
    m->obj.props.synced = mpr_tbl_new();
    m->obj.props.staged = mpr_tbl_new();
    mpr_tbl t = m->obj.props.synced;

    // these properties need to be added in alphabetical order
    mpr_tbl_link(t, PROP(DATA), 1, MPR_PTR, &m->obj.data,
                 MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(t, PROP(EXPR), 1, MPR_STR, &m->expr_str, MODIFIABLE | INDIRECT);
    mpr_tbl_link(t, PROP(ID), 1, MPR_INT64, &m->obj.id, NON_MODIFIABLE | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(t, PROP(MUTED), 1, MPR_BOOL, &m->muted, MODIFIABLE);
    mpr_tbl_link(t, PROP(NUM_SIGS_IN), 1, MPR_INT32, &m->num_src, NON_MODIFIABLE);
    mpr_tbl_link(t, PROP(PROCESS_LOC), 1, MPR_INT32, &m->process_loc, MODIFIABLE);
    mpr_tbl_link(t, PROP(PROTOCOL), 1, MPR_INT32, &m->protocol, REMOTE_MODIFY);
    mpr_list q = mpr_list_new_query((const void**)&m->obj.graph->devs,
                                    cmp_qry_map_scopes, "v", &m);
    mpr_tbl_link(t, PROP(SCOPE), 1, MPR_LIST, q, NON_MODIFIABLE | PROP_OWNED);
    mpr_tbl_link(t, PROP(STATUS), 1, MPR_INT32, &m->status, NON_MODIFIABLE);
    mpr_tbl_link(t, PROP(VERSION), 1, MPR_INT32, &m->obj.version, REMOTE_MODIFY);

    int i, is_local = 0;
    if (m->dst->sig->loc)
        is_local = 1;
    else {
        for (i = 0; i < m->num_src; i++) {
            if (m->src[i]->sig->loc) {
                is_local = 1;
                break;
            }
        }
    }
    mpr_tbl_set(t, PROP(IS_LOCAL), NULL, 1, MPR_BOOL, &is_local,
                LOCAL_ACCESS_ONLY | NON_MODIFIABLE);
    for (i = 0; i < m->num_src; i++)
        mpr_slot_init(m->src[i]);
    mpr_slot_init(m->dst);
}

mpr_map mpr_map_new(int num_src, mpr_sig *src, int num_dst, mpr_sig *dst)
{
    RETURN_UNLESS(src && *src && dst && *dst, 0);
    RETURN_UNLESS(num_src > 0 && num_src <= MAX_NUM_MAP_SRC, 0);
    int i, j;
    for (i = 0; i < num_src; i++) {
        for (j = 0; j < num_dst; j++) {
            if (   strcmp(src[i]->name, dst[j]->name)==0
                && strcmp(src[i]->dev->name, dst[j]->dev->name)==0) {
                trace("Cannot connect signal '%s:%s' to itself.\n",
                      mpr_dev_get_name(src[i]->dev), src[i]->name);
                return 0;
            }
        }
    }
    // Only 1 destination supported for now
    RETURN_UNLESS(1 == num_dst, 0);
    mpr_graph g = (*dst)->obj.graph;

    // check if record of map already exists
    mpr_map m;
    mpr_obj o;
    mpr_list temp, maps = mpr_sig_get_maps(*dst, MPR_DIR_IN);
    if (maps) {
        for (i = 0; i < num_src; i++) {
            o = mpr_graph_get_obj(g, MPR_SIG, src[i]->obj.id);
            if (o) {
                temp = mpr_sig_get_maps((mpr_sig)o, MPR_DIR_OUT);
                maps = mpr_list_get_isect(maps, temp);
            }
            else {
                mpr_list_free(maps);
                maps = 0;
                break;
            }
        }
        while (maps) {
            if (((mpr_map)*maps)->num_src == num_src) {
                m = (mpr_map)*maps;
                mpr_list_free(maps);
                return m;
            }
            maps = mpr_list_get_next(maps);
        }
    }

    int order[num_src];
    if (alphabetise_sigs(num_src, src, order)) {
        trace("error in mpr_map_new(): multiple use of source signal.\n");
        return 0;
    }

    m = (mpr_map)mpr_list_add_item((void**)&g->maps, sizeof(mpr_map_t));
    m->obj.type = MPR_MAP;
    m->obj.graph = g;
    m->num_src = num_src;
    m->src = (mpr_slot*)malloc(sizeof(mpr_slot) * num_src);
    for (i = 0; i < num_src; i++) {
        m->src[i] = (mpr_slot)calloc(1, sizeof(struct _mpr_slot));
        if (src[order[i]]->dev->obj.graph == g)
            o = (mpr_obj)src[order[i]];
        else if (!(o = mpr_graph_get_obj(g, MPR_SIG, src[order[i]]->obj.id))) {
            o = (mpr_obj)mpr_graph_add_sig(g, src[order[i]]->name,
                                           src[order[i]]->dev->name, 0);
            if (!o->id) {
                o->id = src[order[i]]->obj.id;
                ((mpr_sig)o)->dir = src[order[i]]->dir;
                ((mpr_sig)o)->len = src[order[i]]->len;
                ((mpr_sig)o)->type = src[order[i]]->type;
            }
            mpr_dev d = ((mpr_sig)o)->dev;
            if (!d->obj.id)
                d->obj.id = src[order[i]]->dev->obj.id;
        }
        m->src[i]->sig = (mpr_sig)o;
        m->src[i]->map = m;
        m->src[i]->obj.id = i;
    }
    m->dst = (mpr_slot)calloc(1, sizeof(struct _mpr_slot));
    m->dst->sig = *dst;
    m->dst->map = m;
    m->dst->dir = MPR_DIR_IN;

    // we need to give the map a temporary id – this may be overwritten later
    if ((*dst)->dev->loc)
        m->obj.id = mpr_dev_generate_unique_id((*dst)->dev);

    mpr_map_init(m);
    m->status = MPR_STATUS_STAGED;
    m->protocol = MPR_PROTO_UDP;
    ++g->staged_maps;
    return m;
}

void mpr_map_release(mpr_map m)
{
    mpr_net_use_bus(&m->obj.graph->net);
    mpr_map_send_state(m, -1, MSG_UNMAP);
}

void mpr_map_refresh(mpr_map m)
{
    RETURN_UNLESS(m);
    mpr_net_use_bus(&m->obj.graph->net);
    mpr_map_send_state(m, -1, m->loc ? MSG_MAP_TO : MSG_MAP);
}

void mpr_map_free(mpr_map m)
{
    int i;
    if (m->src) {
        for (i = 0; i < m->num_src; i++) {
            mpr_slot_free(m->src[i]);
            free(m->src[i]);
        }
        free(m->src);
    }
    if (m->dst) {
        mpr_slot_free(m->dst);
        free(m->dst);
    }
    if (m->num_scopes && m->scopes)
        free(m->scopes);
    FUNC_IF(mpr_tbl_free, m->obj.props.synced);
    FUNC_IF(mpr_tbl_free, m->obj.props.staged);
    FUNC_IF(free, m->expr_str);
}

int mpr_map_get_num_sigs(mpr_map m, mpr_loc l)
{
    return ((l & MPR_LOC_SRC) ? m->num_src : 0) + (l & MPR_LOC_DST) ? 1 : 0;
}

mpr_sig mpr_map_get_sig(mpr_map m, mpr_loc l, int i)
{
    if (l & MPR_LOC_SRC) {
        if (i < m->num_src)
            return m->src[i]->sig;
        i -= m->num_src;
    }
    return (l & MPR_LOC_DST) ? m->dst->sig : NULL;
}

mpr_slot mpr_map_get_slot_by_sig(mpr_map m, mpr_sig s)
{
    int i;
    if (m->dst->sig->obj.id == s->obj.id)
        return m->dst;
    for (i = 0; i < m->num_src; i++) {
        if (m->src[i]->sig->obj.id == s->obj.id)
            return m->src[i];
    }
    return 0;
}

int mpr_map_get_sig_idx(mpr_map m, mpr_sig s)
{
    int i;
    if (m->dst->sig->obj.id == s->obj.id)
        return 0;
    for (i = 0; i < m->num_src; i++) {
        if (m->src[i]->sig->obj.id == s->obj.id)
            return i;
    }
    return -1;
}

int mpr_map_get_is_ready(mpr_map m)
{
    return m ? (MPR_STATUS_ACTIVE == m->status) : 0;
}

void mpr_map_add_scope(mpr_map m, mpr_dev d)
{
    RETURN_UNLESS(m);
    mpr_prop p = PROP(SCOPE) | PROP_ADD;
    mpr_tbl_record r = mpr_tbl_get(m->obj.props.staged, p, NULL);
    if (r && MPR_STR == r->type) {
        const char *names[r->len+1];
        if (1 == r->len)
            names[0] = (const char*)r->val;
        for (int i = 0; i < r->len; i++)
            names[i] = ((const char**)r->val)[i];
        names[r->len] = d ? d->name : "all";
        mpr_tbl_set(m->obj.props.staged, p, NULL, r->len + 1, MPR_STR, names,
                    REMOTE_MODIFY);
    }
    else
        mpr_tbl_set(m->obj.props.staged, p, NULL, 1, MPR_STR, d->name, REMOTE_MODIFY);
}

void mpr_map_remove_scope(mpr_map m, mpr_dev d)
{
    RETURN_UNLESS(m && d);
    mpr_prop p = PROP(SCOPE) | PROP_REMOVE;
    mpr_tbl t = m->obj.props.staged;
    mpr_tbl_record r = mpr_tbl_get(t, p, NULL);
    if (r && MPR_STR == r->type) {
        const char *names[r->len];
        if (1 == r->len) {
            if (0 == strcmp((const char*)r->val, d->name))
                mpr_tbl_remove(t, p, NULL, REMOTE_MODIFY);
        }
        else {
            int i = 0, j = 0;
            for (; i < r->len; i++) {
                if (0 != strcmp(((const char**)r->val)[i], d->name))
                    names[j++] = ((const char**)r->val)[i];
            }
            if (j != i)
                mpr_tbl_set(t, p, NULL, j, MPR_STR, names, REMOTE_MODIFY);
        }
    }
    else
        mpr_tbl_set(t, p, NULL, 1, MPR_STR, d->name, REMOTE_MODIFY);
}

static int add_scope_internal(mpr_map m, const char *name)
{
    RETURN_UNLESS(m && name, 0);
    int i;
    mpr_dev d = 0;

    if (strcmp(name, "all")==0) {
        for (i = 0; i < m->num_scopes; i++) {
            if (!m->scopes[i])
                return 0;
        }
    }
    else {
        d = mpr_graph_add_dev(m->obj.graph, name, 0);
        for (i = 0; i < m->num_scopes; i++) {
            if (m->scopes[i] && m->scopes[i]->obj.id == d->obj.id)
                return 0;
        }
    }

    // not found - add a new scope
    i = ++m->num_scopes;
    m->scopes = realloc(m->scopes, i * sizeof(mpr_dev));
    m->scopes[i-1] = d;
    return 1;
}

static int remove_scope_internal(mpr_map m, const char *name)
{
    RETURN_UNLESS(m && name, 0);
    int i;
    if (strcmp(name, "all")==0)
        name = 0;
    for (i = 0; i < m->num_scopes; i++) {
        if (!m->scopes[i]) {
            if (!name)
                break;
        }
        else if (name && strcmp(m->scopes[i]->name, name) == 0)
            break;
    }
    if (i == m->num_scopes)
        return 0;
    // found - remove scope at index i
    for (++i; i < m->num_scopes - 1; i++)
        m->scopes[i] = m->scopes[i + 1];
    --m->num_scopes;
    m->scopes = realloc(m->scopes, m->num_scopes * sizeof(mpr_dev));
    return 1;
}

static int mpr_map_update_scope(mpr_map m, mpr_msg_atom a)
{
    int i, j, updated = 0, num = a->len;
    lo_arg **scope_list = a->vals;
    if (scope_list && *scope_list) {
        if (1 == num && strcmp(&scope_list[0]->s, "none")==0)
            num = 0;
        const char *name, *no_slash;

        // First remove old scopes that are missing
        for (i = 0; i < m->num_scopes; i++) {
            int found = 0;
            for (j = 0; j < num; j++) {
                name = &scope_list[j]->s;
                if (!m->scopes[i]) {
                    if (strcmp(name, "all") == 0) {
                        found = 1;
                        break;
                    }
                    break;
                }
                no_slash = ('/' == name[0]) ? name + 1 : name;
                if (strcmp(no_slash, m->scopes[i]->name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                remove_scope_internal(m, m->scopes[i]->name);
                ++updated;
            }
        }
        // ...then add any new scopes
        for (i = 0; i < num; i++)
            updated += add_scope_internal(m, &scope_list[i]->s);
    }
    return updated;
}

// only called for outgoing maps
int mpr_map_perform(mpr_map m, mpr_type *types, mpr_time *time, int inst)
{
    int i;

    RETURN_UNLESS(MPR_STATUS_ACTIVE == m->status && !m->muted, 0);

    if (!m->loc->expr) {
        trace("error: missing expression.\n");
        return 0;
    }

    mpr_hist src[m->num_src];
    for (i = 0; i < m->num_src; i++)
        src[i] = &m->src[i]->loc->hist[inst % m->src[i]->sig->num_inst];
    return (mpr_expr_eval(m->loc->expr, src, &m->loc->expr_var[inst],
                          &m->dst->loc->hist[inst], time, types));
}

/*! Build a value update message for a given map. */
lo_message mpr_map_build_msg(mpr_map m, mpr_slot slot, const void *val,
                             mpr_type *types, mpr_id_map idmap)
{
    int i;
    int len = ((MPR_LOC_SRC == m->process_loc) ? m->dst->sig->len : slot->sig->len);
    NEW_LO_MSG(msg, return 0);

    if (val && types) {
        for (i = 0; i < len; i++) {
            switch (types[i]) {
            case MPR_INT32: lo_message_add_int32(msg, ((int*)val)[i]);     break;
            case MPR_FLT:   lo_message_add_float(msg, ((float*)val)[i]);   break;
            case MPR_DBL:   lo_message_add_double(msg, ((double*)val)[i]); break;
            case MPR_NULL:  lo_message_add_nil(msg);                       break;
            default:                                                       break;
            }
        }
    }
    else if (idmap) {
        for (i = 0; i < len; i++)
            lo_message_add_nil(msg);
    }
    if (idmap) {
        lo_message_add_string(msg, "@instance");
        lo_message_add_int64(msg, idmap->GID);
    }
    if (MPR_LOC_DST == m->process_loc) {
        // add slot
        lo_message_add_string(msg, "@slot");
        lo_message_add_int32(msg, slot->obj.id);
    }
    return msg;
}

/* Helper to replace a map's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expr_str(mpr_map m, const char *expr_str)
{
    if (m->loc->expr && m->expr_str && strcmp(m->expr_str, expr_str)==0)
        return 1;

    int i, src_lens[m->num_src];
    char src_types[m->num_src];
    for (i = 0; i < m->num_src; i++) {
        src_types[i] = m->src[i]->sig->type;
        src_lens[i] = m->src[i]->sig->len;
    }
    mpr_expr expr = mpr_expr_new_from_str(expr_str, m->num_src, src_types,
                                          src_lens, m->dst->sig->type,
                                          m->dst->sig->len);
    RETURN_UNLESS(expr, 1);

    // expression update may force processing location to change
    // e.g. if expression combines signals from different devices
    // e.g. if expression refers to current/past value of destination
    int out_hist_size = mpr_expr_get_out_hist_size(expr);
    if (out_hist_size > 1 && MPR_LOC_SRC == m->process_loc) {
        m->process_loc = MPR_LOC_DST;
        if (!m->dst->sig->loc) {
            // copy expression string but do not execute it
            mpr_tbl_set(m->obj.props.synced, PROP(EXPR), NULL, 1, MPR_STR,
                        expr_str, REMOTE_MODIFY);
            mpr_expr_free(expr);
            return 1;
        }
    }

    FUNC_IF(mpr_expr_free, m->loc->expr);
    m->loc->expr = expr;

    if (m->expr_str == expr_str)
        return 0;

    mpr_tbl_set(m->obj.props.synced, PROP(EXPR), NULL, 1, MPR_STR, expr_str,
                REMOTE_MODIFY);
    return 0;
}

static int print_var(const char *varname, char *str, int max_len, int vec_len,
                     mpr_type type, const void *val)
{
    if (!str || strlen <= 0)
        return -1;
    snprintf(str, max_len, "%s=", varname);
    int i, str_len = strlen(str);
    if (vec_len > 1)
        snprintf(str+str_len, max_len-str_len, "[");
    switch (type) {
        case MPR_INT32:
            for (i = 0; i < vec_len; i++) {
                str_len = strlen(str);
                snprintf(str+str_len, max_len-str_len, "%d,", ((int*)val)[i]);
            }
            break;
        case MPR_FLT:
            for (i = 0; i < vec_len; i++) {
                str_len = strlen(str);
                snprintf(str+str_len, max_len-str_len, "%f,", ((float*)val)[i]);
            }
            break;
        case MPR_DBL:
            for (i = 0; i < vec_len; i++) {
                str_len = strlen(str);
                snprintf(str+str_len, max_len-str_len, "%f,", ((double*)val)[i]);
            }
            break;
        default:
            break;
    }
    str_len = strlen(str) - 1;
    if (vec_len > 1)
        snprintf(str+str_len, max_len-str_len, "];");
    else
        snprintf(str+str_len, max_len-str_len, ";");
    return strlen(str);
}

static inline int _min(int a, int b)
{
    return a < b ? a : b;
}

const char *mpr_map_set_linear(mpr_map m, int slen, mpr_type stype,
                               const void *smin, const void *smax, int dlen,
                               mpr_type dtype, const void *dmin, const void *dmax)
{
    int i, str_len;
    char expr[MAX_LEN] = "";
    const char *e = expr;

    int min_len = slen ?: m->src[0]->sig->len;
    min_len = _min(min_len, dlen ?: m->dst->sig->len);

    if (smin)
        str_len = print_var("srcMin", expr, MAX_LEN, min_len, stype, smin);
    else if (m->src[0]->sig->min)
        str_len = print_var("srcMin", expr, MAX_LEN, min_len,
                            m->src[0]->sig->type, m->src[0]->sig->min);
    else
        return NULL;

    if (smax)
        str_len += print_var("srcMax", expr+str_len, MAX_LEN-str_len, min_len,
                             stype, smax);
    else if (m->src[0]->sig->max)
        str_len += print_var("srcMax", expr+str_len, MAX_LEN-str_len, min_len,
                             m->src[0]->sig->type, m->src[0]->sig->max);
    else
        return NULL;

    if (dmin)
        str_len += print_var("dstMin", expr+str_len, MAX_LEN-str_len, min_len,
                             dtype, dmin);
    else if (m->dst->sig->min)
        str_len += print_var("dstMin", expr+str_len, MAX_LEN-str_len, min_len,
                             m->dst->sig->type, m->dst->sig->min);
    else
        return NULL;

    if (dmax)
        str_len += print_var("dstMax", expr+str_len, MAX_LEN-str_len, min_len,
                             dtype, dmax);
    else if (m->dst->sig->max)
        str_len += print_var("dstMax", expr+str_len, MAX_LEN-str_len, min_len,
                             m->dst->sig->type, m->dst->sig->max);
    else
        return NULL;

    snprintf(expr+str_len, MAX_LEN-str_len,
             "srcRange=srcMax-srcMin;"
             "m=srcRange?((dstMax-dstMin)/srcRange):0;"
             "b=srcRange?(dstMin*srcMax-dstMax*srcMin)/srcRange:dstMin;");

    str_len = strlen(expr);

    if (m->dst->sig->len == m->src[0]->sig->len)
        snprintf(expr+str_len, MAX_LEN-str_len, "y=m*x+b");
    else if (m->dst->sig->len > m->src[0]->sig->len) {
        if (min_len == 1)
            snprintf(expr+str_len, MAX_LEN-str_len, "y[0]=m*x+b");
        else
            snprintf(expr+str_len, MAX_LEN-str_len, "y[0:%i]=m*x+b", min_len-1);
    }
    else if (min_len == 1)
        snprintf(expr+str_len, MAX_LEN-str_len, "y=m*x[0]+b");
    else
        snprintf(expr+str_len, MAX_LEN-str_len, "y=m*x[0:%i]+b", min_len-1);

    trace("linear expression requires %d chars\n", (int)strlen(expr));

    // If everything is successful, replace the map's expression.
    if (e) {
        int should_compile = 0;
        if (m->loc) {
            if (m->loc->is_local_only)
                should_compile = 1;
            else if (MPR_LOC_DST == m->process_loc) {
                // check if destination is local
                if (m->dst->loc->rsig)
                    should_compile = 1;
            }
            else {
                for (i = 0; i < m->num_src; i++) {
                    if (m->src[i]->loc->rsig)
                        should_compile = 1;
                }
            }
        }
        if (should_compile) {
            if (!replace_expr_str(m, e)) {
                reallocate_map_histories(m);

                // evaluate expression to intialise literals
                mpr_time now;
                mpr_time_set(&now, MPR_NOW);
                mpr_hist *vars = m->loc->expr_var;
                mpr_hist to = m->dst->loc->hist;
                for (i = 0; i < m->loc->num_var_inst; i++)
                    mpr_expr_eval(m->loc->expr, 0, &vars[i], &to[i], &now, 0);
            }
        }
        else {
            mpr_tbl_set(m->obj.props.synced, PROP(EXPR), NULL, 1, MPR_STR, e,
                        REMOTE_MODIFY);
        }
    }
    return m->expr_str;
}

static void mpr_map_set_expr(mpr_map m, const char *expr)
{
    int i, should_compile = 0;
    if (m->loc->is_local_only)
        should_compile = 1;
    else if (MPR_LOC_DST == m->process_loc) {
        // check if destination is local
        if (m->dst->loc->rsig)
            should_compile = 1;
    }
    else {
        for (i = 0; i < m->num_src; i++) {
            if (m->src[i]->loc->rsig)
                should_compile = 1;
        }
    }
    if (should_compile) {
        if (!replace_expr_str(m, expr)) {
            reallocate_map_histories(m);
            // evaluate expression to intialise literals
            mpr_time now;
            mpr_time_set(&now, MPR_NOW);
            mpr_hist *vars = m->loc->expr_var;
            mpr_hist to = m->dst->loc->hist;
            for (i = 0; i < m->loc->num_var_inst; i++)
                mpr_expr_eval(m->loc->expr, 0, &vars[i], &to[i], &now, 0);
        }
        else
            return;
    }
    else {
        mpr_tbl_set(m->obj.props.synced, PROP(EXPR), NULL, 1, MPR_STR, expr,
                    REMOTE_MODIFY);
        return;
    }

    /* Special case: if we are the receiver and the new expression evaluates to
     * a constant we can update immediately. */
    /* TODO: should call handler for all instances updated through this map. */
    int use_inst = 0;
    for (i = 0; i < m->num_src; i++) {
        if (m->src[i]->use_inst) {
            use_inst = 1;
            break;
        }
    }
    use_inst += m->dst->use_inst;
    if (mpr_expr_get_num_input_slots(m->loc->expr) <= 0 && !use_inst) {
        mpr_time now;
        mpr_time_set(&now, MPR_NOW);

        // evaluate expression
        for (i = 0; i < m->loc->num_var_inst; i++) {
            mpr_hist *vars = m->loc->expr_var;
            mpr_hist to = m->dst->loc->hist;
            for (i = 0; i < m->loc->num_var_inst; i++)
                mpr_expr_eval(m->loc->expr, 0, &vars[i], &to[i], &now, 0);
        }

        // call handler if it exists
        if (m->dst->loc) {
            mpr_sig sig = m->dst->loc->rsig->sig;
            mpr_sig_handler *h = sig->loc->handler;
            if (h)
                h(sig, MPR_SIG_UPDATE, 0, sig->len, sig->type,
                  &m->dst->loc->hist[0].val, now);
        }
    }

    // check whether each source slot causes computation
    for (i = 0; i < m->num_src; i++)
        m->src[i]->causes_update = !mpr_expr_get_src_is_muted(m->loc->expr, i);
}

// TODO: move to slot.c?
static void init_slot_hist(mpr_slot slot)
{
    RETURN_UNLESS(!slot->loc->hist);
    slot->loc->hist = malloc(sizeof(struct _mpr_hist) * slot->num_inst);
    slot->loc->hist_size = 1;
    int i;
    for (i = 0; i < slot->num_inst; i++) {
        mpr_hist hist = &slot->loc->hist[i];
        hist->type = slot->sig->type;
        hist->len = slot->sig->len;
        hist->size = 1;
        hist->val = calloc(1, mpr_type_get_size(slot->sig->type) * slot->sig->len);
        hist->time = calloc(1, sizeof(mpr_time));
        hist->pos = -1;
    }
}

static void apply_expr(mpr_map m)
{
    if (MPR_LOC_DST == m->process_loc && !m->dst->loc)
        return;

    // try user-defined expression
    if (m->expr_str) {
        mpr_map_set_expr(m, m->expr_str);
        return;
    }

    // try linear scaling
    mpr_slot s = m->src[0], d = m->dst;
    if (mpr_map_set_linear(m, s->sig->len, s->sig->type, s->min, s->max,
                           d->sig->len, d->sig->type, d->min, d->max))
        return;

    // try linear combination of inputs
    char expr_str[MAX_LEN] = "";
    if (1 == m->num_src) {
        if (m->src[0]->sig->len == m->dst->sig->len)
            snprintf(expr_str, MAX_LEN, "y=x");
        else if (m->src[0]->sig->len > m->dst->sig->len) {
            // truncate source
            if (1 == m->dst->sig->len)
                snprintf(expr_str, MAX_LEN, "y=x[0]");
            else
                snprintf(expr_str, MAX_LEN, "y=x[0:%i]", m->dst->sig->len-1);
        }
        else if (1 == m->src[0]->sig->len) {
            // truncate dst
            snprintf(expr_str, MAX_LEN, "y[0]=x");
        }
        else
            snprintf(expr_str, MAX_LEN, "y[0:%i]=x", m->src[0]->sig->len-1);
    }
    else {
        // check vector lengths
        int i, j, max_vec_len = 0, min_vec_len = INT_MAX;
        for (i = 0; i < m->num_src; i++) {
            if (m->src[i]->sig->len > max_vec_len)
                max_vec_len = m->src[i]->sig->len;
            if (m->src[i]->sig->len < min_vec_len)
                min_vec_len = m->src[i]->sig->len;
        }
        int offset = 0, dst_vec_len;
        if (max_vec_len < m->dst->sig->len) {
            if (1 == max_vec_len)
                snprintf(expr_str, MAX_LEN, "y[0]=(");
            else
                snprintf(expr_str, MAX_LEN, "y[0:%d]=(", max_vec_len-1);
            offset = strlen(expr_str);
            dst_vec_len = max_vec_len;
        }
        else {
            snprintf(expr_str, MAX_LEN, "y=(");
            offset = 3;
            dst_vec_len = m->dst->sig->len;
        }
        for (i = 0; i < m->num_src; i++) {
            if (m->src[i]->sig->len > dst_vec_len) {
                if (1 == dst_vec_len)
                    snprintf(expr_str + offset, MAX_LEN - offset, "x%d[0]+", i);
                else
                    snprintf(expr_str + offset, MAX_LEN - offset, "x%d[0:%d]+",
                             i, dst_vec_len-1);
                offset = strlen(expr_str);
            }
            else if (m->src[i]->sig->len < dst_vec_len) {
                snprintf(expr_str + offset, MAX_LEN - offset,
                         "[x%d,0", i);
                offset = strlen(expr_str);
                for (j = 1; j < dst_vec_len - m->src[0]->sig->len; j++) {
                    snprintf(expr_str + offset, MAX_LEN - offset, ",0");
                    offset += 2;
                }
                snprintf(expr_str + offset, MAX_LEN - offset, "]+");
                offset += 2;
            }
            else {
                snprintf(expr_str + offset, MAX_LEN - offset, "x%d+", i);
                offset = strlen(expr_str);
            }
        }
        --offset;
        snprintf(expr_str + offset, MAX_LEN - offset, ")/%d", m->num_src);
    }
    mpr_map_set_expr(m, expr_str);
}

static void mpr_map_check_status(mpr_map m)
{
    RETURN_UNLESS(!bitmatch(m->status, MPR_STATUS_READY));

    m->status |= METADATA_OK;
    int i, mask = ~METADATA_OK;
    if (m->dst->sig->len)
        m->dst->loc->status |= MPR_STATUS_LENGTH_KNOWN;
    if (m->dst->sig->type)
        m->dst->loc->status |= MPR_STATUS_TYPE_KNOWN;
    if (m->dst->loc->rsig || (m->dst->link && m->dst->link && m->dst->link->addr.udp))
        m->dst->loc->status |= MPR_STATUS_LINK_KNOWN;
    m->status &= (m->dst->loc->status | mask);

    for (i = 0; i < m->num_src; i++) {
        if (m->src[i]->sig->len)
            m->src[i]->loc->status |= MPR_STATUS_LENGTH_KNOWN;
        if (m->src[i]->sig->type)
            m->src[i]->loc->status |= MPR_STATUS_TYPE_KNOWN;
        if (m->src[i]->loc->rsig || (m->src[i]->link && m->src[i]->link
                                     && m->src[i]->link->addr.udp))
            m->src[i]->loc->status |= MPR_STATUS_LINK_KNOWN;
        m->status &= (m->src[i]->loc->status | mask);
    }

    if ((m->status & METADATA_OK) == METADATA_OK) {
        // allocate memory for map history
        for (i = 0; i < m->num_src; i++)
            init_slot_hist(m->src[i]);
        init_slot_hist(m->dst);
        if (!m->loc->expr_var)
            m->loc->expr_var = calloc(1, sizeof(mpr_hist*) * m->loc->num_var_inst);
        m->status = MPR_STATUS_READY;
        // update in/out counts for link
        if (m->loc->is_local_only) {
            if (m->dst->link)
                ++m->dst->link->num_maps[0];
        }
        else {
            if (m->dst->link) {
                ++m->dst->link->num_maps[0];
                m->dst->link->obj.props.synced->dirty = 1;
            }
            mpr_link last = 0, link;
            for (i = 0; i < m->num_src; i++) {
                link = m->src[i]->link;
                if (link && link != last) {
                    ++m->src[i]->link->num_maps[1];
                    m->src[i]->link->obj.props.synced->dirty = 1;
                    last = link;
                }
            }
        }
        apply_expr(m);
    }
    return;
}

// if 'override' flag is not set, only remote properties can be set
int mpr_map_set_from_msg(mpr_map m, mpr_msg msg, int override)
{
    int i, j, updated = 0;
    mpr_tbl tbl;
    mpr_msg_atom a;
    if (!msg) {
        if (m->loc && m->status < MPR_STATUS_READY) {
            // check if mapping is now "ready"
            mpr_map_check_status(m);
        }
        return 0;
    }

    if (MPR_DIR_OUT == m->dst->dir) {
        // check if MPR_PROP_SLOT property is defined
        a = mpr_msg_get_prop(msg, PROP(SLOT));
        if (a && a->len == m->num_src) {
            mpr_tbl_record rec;
            for (i = 0; i < m->num_src; i++) {
                int id = (a->vals[i])->i32;
                m->src[i]->obj.id = id;
                // also need to correct slot table indices
                tbl = m->src[i]->obj.props.synced;
                for (j = 0; j < tbl->count; j++) {
                    rec = &tbl->rec[j];
                    rec->prop = MASK_PROP_BITFLAGS(rec->prop) | SRC_SLOT_PROP(id);
                }
                tbl = m->src[i]->obj.props.staged;
                for (j = 0; j < tbl->count; j++) {
                    rec = &tbl->rec[j];
                    rec->prop = MASK_PROP_BITFLAGS(rec->prop) | SRC_SLOT_PROP(id);
                }
            }
        }
    }

    // set destination slot properties
    int status = 0xFF;
    updated += mpr_slot_set_from_msg(m->dst, msg, &status);

    // set source slot properties
    for (i = 0; i < m->num_src; i++)
        updated += mpr_slot_set_from_msg(m->src[i], msg, &status);

    tbl = m->obj.props.synced;
    for (i = 0; i < msg->num_atoms; i++) {
        a = &msg->atoms[i];
        switch (MASK_PROP_BITFLAGS(a->prop)) {
            case PROP(NUM_SIGS_IN):
            case PROP(NUM_SIGS_OUT):
                // these properties will be set by signal args
                break;
            case PROP(STATUS):
                if (m->loc)
                    break;
                updated += mpr_tbl_set_from_atom(tbl, a, REMOTE_MODIFY);
                break;
            case PROP(PROCESS_LOC): {
                mpr_loc loc;
                loc = mpr_loc_from_str(&(a->vals[0])->s);
                if (m->loc) {
                    if (MPR_LOC_UNDEFINED == loc) {
                        trace("map process location is undefined!\n");
                        break;
                    }
                    if (MPR_LOC_ANY == loc) {
                        // no effect
                        break;
                    }
                    if (!m->loc->one_src) {
                        /* Processing must take place at destination if map
                         * includes source signals from different devices. */
                        loc = MPR_LOC_DST;
                    }
                }
                updated += mpr_tbl_set(tbl, PROP(PROCESS_LOC), NULL, 1,
                                       MPR_INT32, &loc, REMOTE_MODIFY);
                break;
            }
            case PROP(EXPR): {
                const char *expr_str = &a->vals[0]->s;
                mpr_loc orig_loc = m->process_loc;
                if (m->loc && bitmatch(m->status, MPR_STATUS_READY)) {
                    if (strstr(expr_str, "y{-"))
                        m->process_loc = MPR_LOC_DST;
                    int should_compile = 0;
                    if (m->loc->is_local_only)
                        should_compile = 1;
                    else if (MPR_LOC_DST == m->process_loc) {
                        // check if destination is local
                        if (m->dst->loc->rsig)
                            should_compile = 1;
                    }
                    else {
                        for (j = 0; j < m->num_src; j++) {
                            if (m->src[j]->loc->rsig)
                                should_compile = 1;
                        }
                    }
                    if (should_compile) {
                        if (!replace_expr_str(m, expr_str)) {
                            // TODO: don't increment updated counter twice
                            ++updated;
                            reallocate_map_histories(m);
                            // evaluate expression to intialise literals
                            mpr_time now;
                            mpr_time_set(&now, MPR_NOW);
                            mpr_hist *vars = m->loc->expr_var;
                            mpr_hist to = m->dst->loc->hist;
                            for (j = 0; j < m->loc->num_var_inst; j++)
                                mpr_expr_eval(m->loc->expr, 0, &vars[j], &to[j],
                                              &now, 0);
                        }
                        else {
                            // restore original process location
                            m->process_loc = orig_loc;
                            break;
                        }
                    }
                }
                if (orig_loc != m->process_loc) {
                    mpr_loc loc = m->process_loc;
                    m->process_loc = orig_loc;
                    updated += mpr_tbl_set(tbl, PROP(PROCESS_LOC), NULL, 1,
                                           MPR_INT32, &loc, REMOTE_MODIFY);
                }
                updated += mpr_tbl_set(tbl, PROP(EXPR), NULL, 1, MPR_STR,
                                       expr_str, REMOTE_MODIFY);
                if (!m->loc) {
                    // remove any cached expression variables from table
                    mpr_tbl_remove(tbl, MPR_PROP_EXTRA, "var@*", REMOTE_MODIFY);
                }
                break;
            }
            case PROP(SCOPE):
                if (mpr_type_get_is_str(a->types[0]))
                    updated += mpr_map_update_scope(m, a);
                break;
            case PROP(SCOPE) | PROP_ADD:
                for (j = 0; j < a->len; j++)
                    updated += add_scope_internal(m, &(a->vals[j])->s);
                break;
            case PROP(SCOPE) | PROP_REMOVE:
                for (j = 0; j < a->len; j++)
                    updated += remove_scope_internal(m, &(a->vals[j])->s);
                break;
            case PROP(PROTOCOL): {
                mpr_proto pro = mpr_protocol_from_str(&(a->vals[0])->s);
                updated += mpr_tbl_set(tbl, PROP(PROTOCOL), NULL, 1, MPR_INT32,
                                       &pro, REMOTE_MODIFY);
                break;
            }
            case PROP(EXTRA):
                if (strncmp(a->key, "var@", 4)==0) {
                    if (m->loc && m->loc->expr) {
                        for (j = 0; j < m->loc->num_expr_var; j++) {
                            if (!mpr_expr_get_var_is_public(m->loc->expr, j))
                                continue;
                            // check if matches existing varname
                            const char *name = mpr_expr_get_var_name(m->loc->expr, j);
                            if (strcmp(name, a->key+4)!=0)
                                continue;
                            // found variable
                            // TODO: handle multiple instances
                            int k = 0, l, var_len =  m->loc->expr_var[k][j].len;
                            double *v = m->loc->expr_var[k][j].val + m->loc->expr_var[k][j].pos;
                            // cast to double if necessary
                            switch (a->types[0])  {
                                case MPR_DBL:
                                    for (k = 0, l = 0; k < var_len; k++, l++) {
                                        if (l >= a->len)
                                            l = 0;
                                        v[k] = a->vals[l]->d;
                                    }
                                    break;
                                case MPR_INT32:
                                    for (k = 0, l = 0; k < var_len; k++, l++) {
                                        if (l >= a->len)
                                            l = 0;
                                        v[k] = (double)a->vals[l]->i32;
                                    }
                                    break;
                                case MPR_FLT:
                                    for (k = 0, l = 0; k < var_len; k++, l++) {
                                        if (l >= a->len)
                                            l = 0;
                                        v[k] = (float)a->vals[l]->f;
                                    }
                                    break;
                                default:
                                    break;
                            }
                            break;
                        }
                    }
                    if (m->loc)
                        break;
                }
            case PROP(ID):
            case PROP(MUTED):
            case PROP(VERSION):
                updated += mpr_tbl_set_from_atom(tbl, a, REMOTE_MODIFY);
                break;
            default:
                break;
        }
    }

    if (m->loc) {
        if (m->status < MPR_STATUS_READY) {
            // check if mapping is now "ready"
            mpr_map_check_status(m);
        }
        else if (updated)
            apply_expr(m);
    }
    return updated;
}

/* TODO: figuring out the correct number of instances for the user variables
 * is a bit tricky... for now we will use the maximum. */
void reallocate_map_histories(mpr_map m)
{
    int i, j, hist_size;
    mpr_slot slot;
    mpr_local_slot slot_loc;

    // If there is no expression, then no memory needs to be reallocated.
    RETURN_UNLESS(m->loc->expr);

    // Reallocate source histories
    for (i = 0; i < m->num_src; i++) {
        slot = m->src[i];
        slot_loc = slot->loc;

        hist_size = mpr_expr_get_in_hist_size(m->loc->expr, i);
        if (hist_size > slot_loc->hist_size) {
            size_t sample_size = mpr_type_get_size(slot->sig->type) * slot->sig->len;;
            for (j = 0; j < slot->num_inst; j++)
                mpr_hist_realloc(&slot_loc->hist[j], hist_size, sample_size, 1);
            slot_loc->hist_size = hist_size;
        }
        else if (hist_size < slot_loc->hist_size) {
            // Do nothing for now...
        }
    }

    hist_size = mpr_expr_get_out_hist_size(m->loc->expr);
    slot = m->dst;
    slot_loc = slot->loc;

    // reallocate output histories
    if (hist_size > slot_loc->hist_size) {
        int sample_size = mpr_type_get_size(slot->sig->type) * slot->sig->len;
        for (i = 0; i < slot->num_inst; i++)
            mpr_hist_realloc(&slot_loc->hist[i], hist_size, sample_size, 0);
        slot_loc->hist_size = hist_size;
    }
    else if (hist_size < slot_loc->hist_size) {
        // Do nothing for now...
    }

    // reallocate user variable histories
    int new_num_var = mpr_expr_get_num_vars(m->loc->expr);
    if (new_num_var > m->loc->num_expr_var) {
        for (i = 0; i < m->loc->num_var_inst; i++) {
            m->loc->expr_var[i] = realloc(m->loc->expr_var[i],
                                          new_num_var * sizeof(struct _mpr_hist));
            // initialize new variables...
            for (j = m->loc->num_expr_var; j < new_num_var; j++) {
                m->loc->expr_var[i][j].type = MPR_DBL;
                m->loc->expr_var[i][j].len = 0;
                m->loc->expr_var[i][j].size = 0;
                m->loc->expr_var[i][j].pos = -1;
                m->loc->expr_var[i][j].val = 0;
                m->loc->expr_var[i][j].time = 0;
            }
            for (j = 0; j < new_num_var; j++) {
                int hist_size = mpr_expr_get_var_hist_size(m->loc->expr, j);
                int vector_len = mpr_expr_get_var_vec_len(m->loc->expr, j);
                mpr_hist_realloc(m->loc->expr_var[i]+j, hist_size,
                                 vector_len * sizeof(double), 0);
                m->loc->expr_var[i][j].len = vector_len;
                m->loc->expr_var[i][j].size = hist_size;
                m->loc->expr_var[i][j].pos = -1;
            }
        }
        m->loc->num_expr_var = new_num_var;
    }
    else if (new_num_var < m->loc->num_expr_var) {
        // Do nothing for now...
    }
}

void mpr_hist_realloc(mpr_hist hist, int hist_size, int samp_size, int is_input)
{
    RETURN_UNLESS(hist && hist_size && samp_size);
    RETURN_UNLESS(hist_size != hist->size);
    if (!is_input || (hist_size > hist->size) || (0 == hist->pos)) {
        // realloc in place
        hist->val = realloc(hist->val, hist_size * samp_size);
        hist->time = realloc(hist->time, hist_size * sizeof(mpr_time));
        if (!is_input) {
            // Initialize entire history to 0
            memset(hist->val, 0, hist_size * samp_size);
            hist->pos = -1;
        }
        else if (0 == hist->pos) {
            memset(hist->val + samp_size * hist->size, 0,
                   samp_size * (hist_size - hist->size));
        }
        else {
            int new_pos = hist_size - hist->size + hist->pos;
            memcpy(hist->val + samp_size * new_pos,
                   hist->val + samp_size * hist->pos,
                   samp_size * (hist->size - hist->pos));
            memcpy(&hist->time[new_pos], &hist->time[(int)hist->pos],
                   sizeof(mpr_time) * (hist->size - hist->pos));
            memset(hist->val + samp_size * hist->pos, 0,
                   samp_size * (hist_size - hist->size));
        }
    }
    else {
        // copying into smaller array
        if (hist->pos >= hist_size * 2) {
            // no overlap - memcpy ok
            int new_pos = hist_size - hist->size + hist->pos;
            memcpy(hist->val, hist->val + samp_size * (new_pos - hist_size),
                   samp_size * hist_size);
            memcpy(&hist->time, &hist->time[hist->pos - hist_size],
                   sizeof(mpr_time) * hist_size);
            hist->val = realloc(hist->val, hist_size * samp_size);
            hist->time = realloc(hist->time, hist_size * sizeof(mpr_time));
        }
        else {
            // there is overlap between new and old arrays - need to allocate new memory
            mpr_hist_t temp;
            temp.val = malloc(samp_size * hist_size);
            temp.time = malloc(sizeof(mpr_time) * hist_size);
            if (hist->pos < hist_size) {
                memcpy(temp.val, hist->val, samp_size * hist->pos);
                memcpy(temp.val + samp_size * hist->pos,
                       hist->val + samp_size * (hist->size - hist_size + hist->pos),
                       samp_size * (hist_size - hist->pos));
                memcpy(temp.time, hist->time, sizeof(mpr_time) * hist->pos);
                memcpy(&temp.time[(int)hist->pos],
                       &hist->time[hist->size - hist_size + hist->pos],
                       sizeof(mpr_time) * (hist_size - hist->pos));
            }
            else {
                memcpy(temp.val, hist->val + samp_size * (hist->pos - hist_size),
                       samp_size * hist_size);
                memcpy(temp.time, &hist->time[hist->pos - hist_size],
                       sizeof(mpr_time) * hist_size);
                hist->pos = hist_size - 1;
            }
            free(hist->val);
            free(hist->time);
            hist->val = temp.val;
            hist->time = temp.time;
        }
    }
    hist->size = hist_size;
}

/* If the "slot_index" argument is >= 0, we can assume this message will be sent
 * to a peer device rather than an administrator. */
int mpr_map_send_state(mpr_map m, int slot, net_msg_t cmd)
{
    if (MSG_MAPPED == cmd && m->status < MPR_STATUS_READY)
        return slot;
    NEW_LO_MSG(msg, return slot);
    char dst_name[256], src_names[1024];
    snprintf(dst_name, 256, "%s%s", m->dst->sig->dev->name, m->dst->sig->path);

    if (MPR_DIR_IN == m->dst->dir) {
        // add mapping destination
        lo_message_add_string(msg, dst_name);
        lo_message_add_string(msg, "<-");
    }

    // add mapping sources
    int i = (slot >= 0) ? slot : 0;
    int len = 0, result;
    mpr_link link = m->src[i]->loc ? m->src[i]->link : 0;
    for (; i < m->num_src; i++) {
        if ((slot >= 0) && link && (link != m->src[i]->link))
            break;
        result = snprintf(&src_names[len], 1024-len, "%s%s",
                          m->src[i]->sig->dev->name, m->src[i]->sig->path);
        if (result < 0 || (len + result + 1) >= 1024) {
            trace("Error encoding sources for combined /mapped msg");
            lo_message_free(msg);
            return slot;
        }
        lo_message_add_string(msg, &src_names[len]);
        len += result + 1;
    }

    if (MPR_DIR_OUT == m->dst->dir || !m->dst->dir) {
        // add mapping destination
        lo_message_add_string(msg, "->");
        lo_message_add_string(msg, dst_name);
    }

    // Add unique id
    if (m->obj.id) {
        lo_message_add_string(msg, mpr_prop_as_str(PROP(ID), 0));
        lo_message_add_int64(msg, *((int64_t*)&m->obj.id));
    }

    if (MSG_UNMAP == cmd || MSG_UNMAPPED == cmd) {
        mpr_net_add_msg(&m->obj.graph->net, 0, cmd, msg);
        return i-1;
    }

    // add other properties
    int staged = (MSG_MAP == cmd) || (MSG_MAP_MOD == cmd);
    mpr_tbl_add_to_msg(0, staged ? m->obj.props.staged : m->obj.props.synced, msg);

    // add slot id
    if (MPR_DIR_IN == m->dst->dir && m->status <= MPR_STATUS_READY && !staged) {
        lo_message_add_string(msg, mpr_prop_as_str(PROP(SLOT), 0));
        i = (slot >= 0) ? slot : 0;
        link = m->src[i]->loc ? m->src[i]->link : 0;
        for (; i < m->num_src; i++) {
            if ((slot >= 0) && link && (link != m->src[i]->link))
                break;
            lo_message_add_int32(msg, m->src[i]->obj.id);
        }
    }

    /* source properties */
    i = (slot >= 0) ? slot : 0;
    link = m->src[i]->loc ? m->src[i]->link : 0;
    for (; i < m->num_src; i++) {
        if ((slot >= 0) && link && (link != m->src[i]->link))
            break;
        mpr_slot_add_props_to_msg(msg, m->src[i], 0, staged);
    }

    /* destination properties */
    mpr_slot_add_props_to_msg(msg, m->dst, 1, staged);

    // add public expression variables
    int j, k, l;
    char varname[32];
    if (m->loc && m->loc->expr) {
        for (j = 0; j < m->loc->num_expr_var; j++) {
            if (!mpr_expr_get_var_is_public(m->loc->expr, j))
                continue;
            // TODO: handle multiple instances
            k = 0;
            if (m->loc->expr_var[k][j].pos >= 0) {
                snprintf(varname, 32, "@var@%s", mpr_expr_get_var_name(m->loc->expr, j));
                lo_message_add_string(msg, varname);
                double *v = m->loc->expr_var[k][j].val + m->loc->expr_var[k][j].pos;
                for (l = 0; l < m->loc->expr_var[k][j].len; l++)
                    lo_message_add_double(msg, v[l]);
            }
            else {
                trace("public expression variable '%s' is not yet initialised.\n",
                      mpr_expr_get_var_name(m->loc->expr, j));
            }
        }
    }

    mpr_net_add_msg(&m->obj.graph->net, 0, cmd, msg);
    return i-1;
}
