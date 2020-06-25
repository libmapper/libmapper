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

static inline int _max(int a, int b)
{
    return a > b ? a : b;
}

static inline int _min(int a, int b)
{
    return a < b ? a : b;
}

static int _sort_sigs(int num, mpr_sig *s, int *o)
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

static int _cmp_qry_scopes(const void *ctx, mpr_dev d)
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
                                    _cmp_qry_scopes, "v", &m);
    mpr_tbl_link(t, PROP(SCOPE), 1, MPR_LIST, q, NON_MODIFIABLE | PROP_OWNED);
    mpr_tbl_link(t, PROP(STATUS), 1, MPR_INT32, &m->status, NON_MODIFIABLE);
    mpr_tbl_link(t, PROP(USE_INST), 1, MPR_BOOL, &m->use_inst, REMOTE_MODIFY);
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
    if (_sort_sigs(num_src, src, order)) {
        trace("error in mpr_map_new(): multiple use of source signal.\n");
        return 0;
    }

    m = (mpr_map)mpr_list_add_item((void**)&g->maps, sizeof(mpr_map_t));
    m->obj.type = MPR_MAP;
    m->obj.graph = g;
    m->num_src = num_src;
    m->src = (mpr_slot*)malloc(sizeof(mpr_slot) * num_src);
    for (i = 0; i < num_src; i++) {
        m->src[i] = mpr_slot_new(m, NULL, 1);
        if (src[order[i]]->dev->obj.graph == g)
            o = (mpr_obj)src[order[i]];
        else if (!(o = mpr_graph_get_obj(g, MPR_SIG, src[order[i]]->obj.id))) {
            o = (mpr_obj)mpr_graph_add_sig(g, src[order[i]]->name, src[order[i]]->dev->name, 0);
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
        m->src[i]->obj.id = i;
    }
    m->dst = mpr_slot_new(m, *dst, 0);
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

static int _cmp_qry_sigs(const void *ctx, mpr_sig s)
{
    mpr_map m = *(mpr_map*)ctx;
    mpr_loc l = *(mpr_loc*)(ctx + sizeof(mpr_map*));
    int i;
    if (l & MPR_LOC_SRC) {
        for (i = 0; i < m->num_src; i++) {
            if (s == m->src[i]->sig)
                return 1;
        }
    }
    return (l & MPR_LOC_DST) ? (s == m->dst->sig) : 0;
}

mpr_list mpr_map_get_sigs(mpr_map m, mpr_loc l)
{
    RETURN_UNLESS(m && m->obj.graph->sigs, 0);
    mpr_list qry = mpr_list_new_query((const void**)&m->obj.graph->sigs, _cmp_qry_sigs, "vi", &m, l);
    return mpr_list_start(qry);
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

/* Here we do not edit the "scope" property directly – instead we stage a the
 * change with device name arguments and send to the distrubuted graph. */
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
        mpr_tbl_set(m->obj.props.staged, p, NULL, r->len + 1, MPR_STR, names, REMOTE_MODIFY);
    }
    else
        mpr_tbl_set(m->obj.props.staged, p, NULL, 1, MPR_STR, d->name, REMOTE_MODIFY);
}

/* Here we do not edit the "scope" property directly – instead we stage a the
 * change with device name arguments and send to the distrubuted graph. */
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

static int _add_scope(mpr_map m, const char *name)
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

static int _remove_scope(mpr_map m, const char *name)
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

static int _update_scope(mpr_map m, mpr_msg_atom a)
{
    int i = 0, j, updated = 0, num = a->len;
    lo_arg **scope_list = a->vals;
    if (scope_list && *scope_list) {
        if (1 == num && strcmp(&scope_list[0]->s, "none")==0)
            num = 0;
        const char *name;

        // First remove old scopes that are missing
        while (i < m->num_scopes) {
            int found = 0;
            for (j = 0; j < num; j++) {
                name = skip_slash(&scope_list[j]->s);
                if (!m->scopes[i]) {
                    if (strcmp(name, "all") == 0) {
                        found = 1;
                        break;
                    }
                    break;
                }
                if (strcmp(name, m->scopes[i]->name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && m->scopes[i])
                updated += _remove_scope(m, m->scopes[i]->name);
            else
                ++i;
        }
        // ...then add any new scopes
        for (i = 0; i < num; i++)
            updated += _add_scope(m, &scope_list[i]->s);
    }
    return updated;
}

// only called for outgoing maps
int mpr_map_perform(mpr_map m, mpr_type *types, mpr_time *time, int inst_idx)
{
    int i;

    RETURN_UNLESS(MPR_STATUS_ACTIVE == m->status && !m->muted, 0);

    if (!m->loc->expr) {
        trace("error: missing expression.\n");
        return 0;
    }

    mpr_value src[m->num_src];
    for (i = 0; i < m->num_src; i++)
        src[i] = &m->src[i]->loc->val[inst_idx % m->src[i]->num_inst];
    return (mpr_expr_eval(m->loc->expr, src, &m->loc->vars[inst_idx],
                          &m->dst->loc->val[inst_idx], time, types));
}

/*! Build a value update message for a given map. */
lo_message mpr_map_build_msg(mpr_map m, mpr_slot slot, const void *val,
                             mpr_type *types, mpr_id_map idmap)
{
    int i;
    int len = ((MPR_LOC_SRC == m->process_loc) ? m->dst->sig->len : slot->sig->len);
    NEW_LO_MSG(msg, return 0);

    if (val && types) {
        // value of vector elements can be <type> or NULL
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
    else if (m->use_inst) {
        for (i = 0; i < len; i++)
            lo_message_add_nil(msg);
    }
    if (m->use_inst && idmap) {
        lo_message_add_string(msg, "@instance");
        lo_message_add_int64(msg, idmap->GID);
    }
    if (MPR_LOC_DST == m->process_loc && MPR_DIR_OUT == m->dst->dir) {
        // add slot
        lo_message_add_string(msg, "@slot");
        lo_message_add_int32(msg, slot->obj.id);
    }
    return msg;
}

static void init_vars(mpr_value v, int vec_len)
{
    v->type = MPR_DBL;
    v->len = vec_len;
    v->mem = 0;
    v->pos = 0;
    v->samps = calloc(1, sizeof(double) * vec_len);
    v->times = calloc(1, sizeof(mpr_time));
}

void mpr_map_alloc_values(mpr_map m)
{
    // If there is no expression or the processing is remote, then no memory needs to be (re)allocated.
    RETURN_UNLESS(m->loc->expr
                  && (m->loc->is_local_only
                      || !((MPR_DIR_OUT == m->dst->dir) ^ (MPR_LOC_SRC == m->process_loc))));

    // TODO: check if this filters non-local processing.
    // if so we can eliminate process_loc tests below
    // if not we are allocating variable memory when we don't need to

    int i, j, hist_size, num_var_inst;

    // HANDLE edge case: if the map is local, then src->dir is OUT and dst->dir is IN

    // check if slot values need to be reallocated
    if (MPR_DIR_OUT == m->dst->dir) {
        int max_num_inst = 0;
        for (i = 0; i < m->num_src; i++) {
            hist_size = mpr_expr_get_in_hist_size(m->loc->expr, i);
            max_num_inst = _max(m->src[i]->sig->num_inst, max_num_inst);
            mpr_slot_alloc_values(m->src[i], m->src[i]->sig->num_inst, hist_size);
        }
        hist_size = mpr_expr_get_out_hist_size(m->loc->expr);
        // allocate enough dst slot and variable instances for the most multitudinous source signal
        mpr_slot_alloc_values(m->dst, max_num_inst, hist_size);
        num_var_inst = max_num_inst;
    }
    else if (MPR_DIR_IN == m->dst->dir) {
        // allocate enough instances for destination signal
        for (i = 0; i < m->num_src; i++) {
            hist_size = mpr_expr_get_in_hist_size(m->loc->expr, i);
            mpr_slot_alloc_values(m->src[i], m->dst->sig->num_inst, hist_size);
        }
        hist_size = mpr_expr_get_out_hist_size(m->loc->expr);
        mpr_slot_alloc_values(m->dst, m->dst->sig->num_inst, hist_size);
        num_var_inst = m->dst->sig->num_inst;
    }

    mpr_local_map lm = m->loc;
    int num_vars = mpr_expr_get_num_vars(lm->expr);

    if (lm->num_vars >= num_vars &&  lm->num_var_inst >= num_var_inst)
        return;

    // TODO: only need to allocate this memory for processing location
    if (!lm->vars) {
        lm->vars = calloc(1, sizeof(mpr_value) * num_var_inst);
        lm->num_var_inst = 0;
    }
    else {
        lm->vars = realloc(lm->vars, sizeof(mpr_value) * num_var_inst);
    }

    if (num_vars > lm->num_vars) {
        // reallocate existing user variables
        for (i = 0; i < lm->num_var_inst; i++) {
            lm->vars[i] = realloc(lm->vars[i], num_vars * sizeof(struct _mpr_value));
            // initialize new variables
            for (j = lm->num_vars; j < num_vars; j++) {
                int vec_len = mpr_expr_get_var_vec_len(m->loc->expr, j);
                init_vars(&lm->vars[i][j], vec_len);
            }
        }
    }

    for (i = lm->num_var_inst; i < num_var_inst; i++) {
        lm->vars[i] = calloc(1, num_vars * sizeof(struct _mpr_value));
        for (j = 0; j < num_vars; j++) {
            int vec_len = mpr_expr_get_var_vec_len(m->loc->expr, j);
            init_vars(&lm->vars[i][j], vec_len);
        }
    }

    lm->num_vars = num_vars;
    lm->num_var_inst = num_var_inst;
}

/* Helper to replace a map's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int _replace_expr_str(mpr_map m, const char *expr_str)
{
    if (m->loc->expr && m->expr_str && strcmp(m->expr_str, expr_str)==0)
        return 1;

    int i, src_lens[m->num_src];
    char src_types[m->num_src];
    for (i = 0; i < m->num_src; i++) {
        src_types[i] = m->src[i]->sig->type;
        src_lens[i] = m->src[i]->sig->len;
    }
    mpr_expr expr = mpr_expr_new_from_str(expr_str, m->num_src, src_types, src_lens,
                                          m->dst->sig->type, m->dst->sig->len);
    RETURN_UNLESS(expr, 1);

    // expression update may force processing location to change
    // e.g. if expression combines signals from different devices
    // e.g. if expression refers to current/past value of destination
    int out_mem = mpr_expr_get_out_hist_size(expr);
    if (out_mem > 1 && MPR_LOC_SRC == m->process_loc) {
        m->process_loc = MPR_LOC_DST;
        if (!m->dst->sig->loc) {
            // copy expression string but do not execute it
            mpr_tbl_set(m->obj.props.synced, PROP(EXPR), NULL, 1, MPR_STR, expr_str, REMOTE_MODIFY);
            mpr_expr_free(expr);
            return 1;
        }
    }
    FUNC_IF(mpr_expr_free, m->loc->expr);
    m->loc->expr = expr;

    if (m->expr_str == expr_str)
        return 0;
    mpr_tbl_set(m->obj.props.synced, PROP(EXPR), NULL, 1, MPR_STR, expr_str, REMOTE_MODIFY);
    mpr_tbl_remove(m->obj.props.staged, PROP(EXPR), NULL, 0);
    return 0;
}

static inline int _trim_zeros(char *str, int len)
{
    if (!strchr(str, '.'))
        return len;
    while ('0' == str[len-1])
        --len;
    if ('.' == str[len-1])
        --len;
    str[len] = '\0';
    return len;
}

static int _snprint_var(const char *varname, char *str, int max_len, int vec_len,
                        mpr_type type, const void *val)
{
    if (!str || strlen <= 0)
        return -1;
    snprintf(str, max_len, "%s=", varname);
    int i, str_len = strlen(str), var_len;
    if (vec_len > 1)
        str_len += snprintf(str+str_len, max_len-str_len, "[");
    switch (type) {
        case MPR_INT32:
            for (i = 0; i < vec_len; i++) {
                var_len = snprintf(str+str_len, max_len-str_len, "%d", ((int*)val)[i]);
                str_len += _trim_zeros(str+str_len, var_len);
                str_len += snprintf(str+str_len, max_len-str_len, ",");
            }
            break;
        case MPR_FLT:
            for (i = 0; i < vec_len; i++) {
                var_len = snprintf(str+str_len, max_len-str_len, "%f", ((float*)val)[i]);
                str_len += _trim_zeros(str+str_len, var_len);
                str_len += snprintf(str+str_len, max_len-str_len, ",");
            }
            break;
        case MPR_DBL:
            for (i = 0; i < vec_len; i++) {
                var_len = snprintf(str+str_len, max_len-str_len, "%f", ((double*)val)[i]);
                str_len += _trim_zeros(str+str_len, var_len);
                str_len += snprintf(str+str_len, max_len-str_len, ",");
            }
            break;
        default:
            break;
    }
    --str_len;
    if (vec_len > 1)
        str_len += snprintf(str+str_len, max_len-str_len, "];");
    else
        str_len += snprintf(str+str_len, max_len-str_len, ";");
    return str_len;
}

#define INSERT_VAL(MINORMAX)                                        \
mpr_value *ev = m->loc->vars;                                       \
for (j = 0; j < m->loc->num_vars; j++) {                            \
    if (!mpr_expr_get_var_is_public(m->loc->expr, j))               \
        continue;                                                   \
    /* TODO: handle multiple instances */                           \
    k = 0;                                                          \
    if (strcmp(MINORMAX, mpr_expr_get_var_name(m->loc->expr, j)))   \
        continue;                                                   \
    if (ev[k][j].pos < 0) {                                         \
        trace("expr var '%s' is not yet initialised.\n", MINORMAX); \
        goto abort;                                                 \
    }                                                               \
    len += snprintf(expr+len, MAX_LEN-len, "%s=", MINORMAX);        \
    double *v = ev[k][j].samps + ev[k][j].pos;                      \
    if (ev[k][j].len > 1)                                           \
        len += snprintf(expr+len, MAX_LEN-len, "[");                \
    for (l = 0; l < ev[k][j].len; l++) {                            \
        val_len = snprintf(expr+len, MAX_LEN-len, "%f", v[l]);      \
        len += _trim_zeros(expr+len, val_len);                      \
        len += snprintf(expr+len, MAX_LEN-len, ",");                \
    }                                                               \
    --len;                                                          \
    if (ev[k][j].len > 1)                                           \
        len += snprintf(expr+len, MAX_LEN-len, "]");                \
    len += snprintf(expr+len, MAX_LEN-len, ";");                    \
    break;                                                          \
}                                                                   \
if (j == m->loc->num_vars) {                                        \
    trace("expr var '%s' is not found.\n", MINORMAX);               \
    goto abort;                                                     \
}

static const char *_set_linear(mpr_map m, const char *e)
{
    // if e is NULL, try to fill in ranges from map signals
    int j, k, l, len = 0, val_len;
    char expr[MAX_LEN] = "";
    char *var = "x";

    int min_len = _min(m->src[0]->sig->len, m->dst->sig->len);

    if (e) {
        // how many instances of 'linear' appear in the expression string?
        int num_inst = 0;
        char *offset = (char*)e;
        while ((offset = strstr(offset, "linear"))) {
            ++num_inst;
            offset += 6;
        }
        // for now we will only allow one instance of 'linear' function macro
        if (num_inst != 1) {
            trace("found %d instances of the string 'linear'\n", num_inst);
            return NULL;
        }
        // use a copy in case we fail after tokenisation
        e = strdup(e);
        offset = (char*)e;
        // for (i = 0; i < num_inst; i++) {
            // TODO: copy sections of expression before 'linear('
            offset = strstr(offset, "linear") + 6;
            // remove any spaces
            while (*offset && *offset == ' ')
                ++offset;
            // next character must be '('
            if (*offset != '(') {
                goto abort;
            }
            ++offset;
            char* arg_str = strtok(offset, ")");
            if (!arg_str) {
                trace("error: no arguments found for 'linear' function\n");
                goto abort;
            }
            char *args[5];
            for (j = 0; j < 5; j++) {
                args[j] = strtok(arg_str, ",");
                arg_str = NULL;
                if (!args[j])
                    goto abort;
            }
            // we won't check if ranges are numeric since they could be variables
            // but src extrema are allowed to be "?" to indicate calibration
            // or '-' to indicate they should not be changed
            if (0 == strcmp(args[1], "?"))
                len = snprintf(expr, MAX_LEN, "sMin{-1}=x;sMin=min(%s,sMin);", var);
            else if (0 == strcmp(args[1], "-")) {
                // try to load sMin variable from existing expression
                if (!m->loc || !m->loc->expr) {
                    trace("can't retrieve previous expr var since map is not local\n");
                    len += snprintf(expr+len, MAX_LEN-len, "sMin=0;");
                }
                else {
                    INSERT_VAL("sMin");
                }
            }
            else {
                val_len = snprintf(expr, MAX_LEN, "sMin=%s", args[1]);
                len += _trim_zeros(expr, val_len);
                len += snprintf(expr+len, MAX_LEN-len, ";");
            }

            if (0 == strcmp(args[2], "?"))
                len += snprintf(expr+len, MAX_LEN-len, "sMax{-1}=x;sMax=max(%s,sMax);", var);
            else if (0 == strcmp(args[2], "-")) {
                // try to load sMax variable from existing expression
                if (!m->loc || !m->loc->expr) {
                    // TODO: try using signal instead
                    trace("can't retrieve previous expr var, using default\n");
                    // TODO: test with vector signals
                    len += snprintf(expr+len, MAX_LEN-len, "sMax=1;");
                }
                else {
                    INSERT_VAL("sMax");
                }
            }
            else {
                val_len = snprintf(expr+len, MAX_LEN-len, "sMax=%s", args[2]);
                len += _trim_zeros(expr+len, val_len);
                len += snprintf(expr+len, MAX_LEN-len, ";");
            }

            if (0 == strcmp(args[3], "-")) {
                // try to load dMin variable from existing expression
                if (!m->loc || !m->loc->expr) {
                    trace("can't retrieve previous expr var since map is not local\n");
                    len += snprintf(expr+len, MAX_LEN-len, "dMin=0;");
                }
                else {
                    INSERT_VAL("dMin");
                }
            }
            else {
                val_len = snprintf(expr+len, MAX_LEN-len, "dMin=%s", args[3]);
                len += _trim_zeros(expr+len, val_len);
                len += snprintf(expr+len, MAX_LEN-len, ";");
            }

            if (0 == strcmp(args[4], "-")) {
                // try to load dMin variable from existing expression
                if (!m->loc || !m->loc->expr) {
                    trace("can't retrieve previous expr var since map is not local\n");
                    len += snprintf(expr+len, MAX_LEN-len, "dMax=1;");
                }
                else {
                    INSERT_VAL("dMax");
                }
            }
            else {
                val_len = snprintf(expr+len, MAX_LEN-len, "dMax=%s", args[4]);
                len += _trim_zeros(expr+len, val_len);
                len += snprintf(expr+len, MAX_LEN-len, ";");
            }

            var = args[0];
            // TODO: copy sections of expression after closing paren ')'
        // }

    }
    else if (m->src[0]->sig->min && m->src[0]->sig->max && m->dst->sig->min && m->dst->sig->max) {
        len = _snprint_var("sMin", expr, MAX_LEN, min_len, m->src[0]->sig->type,
                           m->src[0]->sig->min);
        len += _snprint_var("sMax", expr+len, MAX_LEN-len, min_len,
                            m->src[0]->sig->type, m->src[0]->sig->max);
        len += _snprint_var("dMin", expr+len, MAX_LEN-len, min_len,
                            m->dst->sig->type, m->dst->sig->min);
        len += _snprint_var("dMax", expr+len, MAX_LEN-len, min_len,
                            m->dst->sig->type, m->dst->sig->max);
    }
    else {
        // try linear combination of inputs
        if (1 == m->num_src) {
            if (m->src[0]->sig->len == m->dst->sig->len)
                snprintf(expr, MAX_LEN, "y=x");
            else if (m->src[0]->sig->len > m->dst->sig->len) {
                // truncate source
                if (1 == m->dst->sig->len)
                    snprintf(expr, MAX_LEN, "y=x[0]");
                else
                    snprintf(expr, MAX_LEN, "y=x[0:%i]", m->dst->sig->len-1);
            }
            else if (1 == m->src[0]->sig->len) {
                // truncate dst
                snprintf(expr, MAX_LEN, "y[0]=x");
            }
            else
                snprintf(expr, MAX_LEN, "y[0:%i]=x", m->src[0]->sig->len-1);
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
            int dst_vec_len;
            if (max_vec_len < m->dst->sig->len) {
                if (1 == max_vec_len)
                    len = snprintf(expr, MAX_LEN, "y[0]=(");
                else
                    len = snprintf(expr, MAX_LEN, "y[0:%d]=(", max_vec_len-1);
                dst_vec_len = max_vec_len;
            }
            else {
                len = snprintf(expr, MAX_LEN, "y=(");
                dst_vec_len = m->dst->sig->len;
            }
            for (i = 0; i < m->num_src; i++) {
                if (m->src[i]->sig->len > dst_vec_len) {
                    if (1 == dst_vec_len)
                        len += snprintf(expr+len, MAX_LEN-len, "x%d[0]+", i);
                    else
                        len += snprintf(expr+len, MAX_LEN-len, "x%d[0:%d]+",
                                        i, dst_vec_len-1);
                }
                else if (m->src[i]->sig->len < dst_vec_len) {
                    len += snprintf(expr+len, MAX_LEN-len, "[x%d,0", i);
                    for (j = 1; j < dst_vec_len - m->src[0]->sig->len; j++)
                        len += snprintf(expr+len, MAX_LEN-len, ",0");
                    len += snprintf(expr+len, MAX_LEN-len, "]+");
                }
                else
                    len += snprintf(expr+len, MAX_LEN-len, "x%d+", i);
            }
            --len;
            snprintf(expr+len, MAX_LEN-len, ")/%d", m->num_src);
        }
        return strdup(expr);
    }

    snprintf(expr+len, MAX_LEN-len,
             "sRange=sMax-sMin;"
             "m=sRange?((dMax-dMin)/sRange):0;"
             "b=sRange?(dMin*sMax-dMax*sMin)/sRange:dMin;");

    len = strlen(expr);

    if (m->dst->sig->len == m->src[0]->sig->len)
        snprintf(expr+len, MAX_LEN-len, "y=m*%s+b;", var);
    else if (m->dst->sig->len > m->src[0]->sig->len) {
        if (min_len == 1)
            snprintf(expr+len, MAX_LEN-len, "y[0]=m*%s+b;", var);
        else
            snprintf(expr+len, MAX_LEN-len, "y[0:%i]=m*%s+b;", min_len-1, var);
    }
    else if (min_len == 1)
        snprintf(expr+len, MAX_LEN-len, "y=m*%s[0]+b;", var);
    else
        snprintf(expr+len, MAX_LEN-len, "y=m*%s[0:%i]+b;", var, min_len-1);

    trace("linear expression %s requires %d chars\n", expr, (int)strlen(expr));
    return strdup(expr);

abort:
    FUNC_IF(free, (char*)e);
    return NULL;
}

static int _set_expr(mpr_map m, const char *expr)
{
    RETURN_UNLESS(m->loc && m->num_src > 0, 0);

    if (m->idmap)
        mpr_dev_LID_decref(m->loc->rtr->dev, 0, m->idmap);

    int i, should_compile = 0;
    const char *new_expr = 0;
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

    if (!should_compile) {
        if (expr)
            mpr_tbl_set(m->obj.props.synced, PROP(EXPR), NULL, 1, MPR_STR, expr, REMOTE_MODIFY);
        goto done;
    }
    if (!expr || strstr(expr, "linear"))
        expr = new_expr = _set_linear(m, expr);
    RETURN_UNLESS(expr, 1);
    if (!_replace_expr_str(m, expr)) {
        mpr_map_alloc_values(m);
        // evaluate expression to intialise literals
        mpr_time now;
        mpr_time_set(&now, MPR_NOW);
        mpr_value *vars = m->loc->vars;
        mpr_value to = m->dst->loc->val;
        for (i = 0; i < m->loc->num_var_inst; i++)
            mpr_expr_eval(m->loc->expr, 0, &vars[i], &to[i], &now, 0);
    }
    else {
        if (!m->loc->expr && (   (MPR_LOC_DST == m->process_loc && m->dst->sig->loc)
                              || (MPR_LOC_SRC == m->process_loc && m->src[0]->sig->loc))) {
            // no previous expression, abort map
            m->status = MPR_STATUS_EXPIRED;
        }
        goto done;
    }

    /* Special case: if we are the receiver and the new expression evaluates to
     * a constant we can update immediately. */
    /* TODO: should call handler for all instances updated through this map. */
    if (mpr_expr_get_num_input_slots(m->loc->expr) <= 0 && !m->use_inst && m->dst->loc) {
        mpr_time now;
        mpr_time_set(&now, MPR_NOW);

        // call handler if it exists
        mpr_sig sig = m->dst->loc->rsig->sig;
        mpr_sig_handler *h = sig->loc->handler;
        if (h)
            h(sig, MPR_SIG_UPDATE, 0, sig->len, sig->type, &m->dst->loc->val[0].samps, now);
    }

    // check whether each source slot causes computation
    for (i = 0; i < m->num_src; i++)
        m->src[i]->causes_update = !mpr_expr_get_src_is_muted(m->loc->expr, i);
done:
    if (new_expr)
        free((char*)new_expr);
    return 0;
}

static void _check_status(mpr_map m)
{
    RETURN_UNLESS(!bitmatch(m->status, MPR_STATUS_READY));
    m->status |= METADATA_OK;
    int i, mask = ~METADATA_OK;
    if (m->dst->sig->len)
        m->dst->loc->status |= MPR_STATUS_LENGTH_KNOWN;
    if (m->dst->sig->type)
        m->dst->loc->status |= MPR_STATUS_TYPE_KNOWN;
    if (m->dst->loc->rsig || (m->dst->link && m->dst->link->addr.udp))
        m->dst->loc->status |= MPR_STATUS_LINK_KNOWN;
    m->status &= (m->dst->loc->status | mask);

    for (i = 0; i < m->num_src; i++) {
        if (m->src[i]->sig->len)
            m->src[i]->loc->status |= MPR_STATUS_LENGTH_KNOWN;
        if (m->src[i]->sig->type)
            m->src[i]->loc->status |= MPR_STATUS_TYPE_KNOWN;
        if (m->src[i]->loc->rsig || (m->src[i]->link && m->src[i]->link->addr.udp))
            m->src[i]->loc->status |= MPR_STATUS_LINK_KNOWN;
        m->status &= (m->src[i]->loc->status | mask);
    }

    if ((m->status & METADATA_OK) == METADATA_OK) {
        trace("map metadata OK\n");
        mpr_map_alloc_values(m);
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
        _set_expr(m, m->expr_str);
    }
    return;
}

// if 'override' flag is not set, only remote properties can be set
int mpr_map_set_from_msg(mpr_map m, mpr_msg msg, int override)
{
    int i, j, updated = 0, should_compile = 0;
    mpr_tbl tbl;
    mpr_msg_atom a;
    if (!msg)
        goto done;

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
    updated += mpr_slot_set_from_msg(m->dst, msg);

    // set source slot properties
    for (i = 0; i < m->num_src; i++)
        updated += mpr_slot_set_from_msg(m->src[i], msg);

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
                mpr_loc loc = mpr_loc_from_str(&(a->vals[0])->s);
                if (loc == m->process_loc)
                    break;
                if (MPR_LOC_UNDEFINED == loc) {
                    trace("map process location is undefined!\n");
                    break;
                }
                if (m->loc) {
                    if (MPR_LOC_ANY == loc) {
                        // no effect
                        break;
                    }
                    if (!m->loc->one_src) {
                        /* Processing must take place at destination if map
                         * includes source signals from different devices. */
                        loc = MPR_LOC_DST;
                    }
                    if (MPR_LOC_DST == loc) {
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
                        if (_set_expr(m, m->expr_str)) {
                            // do not change process location
                            break;
                        }
                        ++updated;
                    }
                    updated += mpr_tbl_set(tbl, PROP(PROCESS_LOC), NULL, 1,
                                   MPR_INT32, &loc, REMOTE_MODIFY);
                }
                else
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
                        if (_set_expr(m, expr_str)) {
                            // restore original process location
                            m->process_loc = orig_loc;
                            break;
                        }
                        ++updated;
                    }
                    else {
                        updated += mpr_tbl_set(tbl, PROP(EXPR), NULL, 1, MPR_STR,
                                               expr_str, REMOTE_MODIFY);
                    }
                }
                else {
                    updated += mpr_tbl_set(tbl, PROP(EXPR), NULL, 1, MPR_STR,
                                           expr_str, REMOTE_MODIFY);
                }
                if (orig_loc != m->process_loc) {
                    mpr_loc loc = m->process_loc;
                    m->process_loc = orig_loc;
                    updated += mpr_tbl_set(tbl, PROP(PROCESS_LOC), NULL, 1,
                                           MPR_INT32, &loc, REMOTE_MODIFY);
                }
                if (!m->loc) {
                    // remove any cached expression variables from table
                    mpr_tbl_remove(tbl, MPR_PROP_EXTRA, "var@*", REMOTE_MODIFY);
                }
                break;
            }
            case PROP(SCOPE):
                if (mpr_type_get_is_str(a->types[0]))
                    updated += _update_scope(m, a);
                break;
            case PROP(SCOPE) | PROP_ADD:
                for (j = 0; j < a->len; j++)
                    updated += _add_scope(m, &(a->vals[j])->s);
                break;
            case PROP(SCOPE) | PROP_REMOVE:
                for (j = 0; j < a->len; j++)
                    updated += _remove_scope(m, &(a->vals[j])->s);
                break;
            case PROP(PROTOCOL): {
                mpr_proto pro = mpr_protocol_from_str(&(a->vals[0])->s);
                updated += mpr_tbl_set(tbl, PROP(PROTOCOL), NULL, 1, MPR_INT32,
                                       &pro, REMOTE_MODIFY);
                break;
            }
            case PROP(USE_INST): {
                int use_inst = a->types[0] == 'T';
                if (m->loc && m->use_inst && !use_inst) {
                    // TODO: release map instances
                }
                updated += mpr_tbl_set(tbl, PROP(USE_INST), NULL, 1, MPR_BOOL,
                                       &use_inst, REMOTE_MODIFY);
                break;
            }
            case PROP(EXTRA):
                if (strncmp(a->key, "var@", 4)==0) {
                    if (m->loc && m->loc->expr) {
                        for (j = 0; j < m->loc->num_vars; j++) {
                            if (!mpr_expr_get_var_is_public(m->loc->expr, j))
                                continue;
                            // check if matches existing varname
                            const char *name = mpr_expr_get_var_name(m->loc->expr, j);
                            if (strcmp(name, a->key+4)!=0)
                                continue;
                            // found variable
                            // TODO: handle multiple instances
                            int k = 0, l, var_len =  m->loc->vars[k][j].len;
                            double *v = m->loc->vars[k][j].samps + m->loc->vars[k][j].pos;
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
done:
    if (m->loc && m->status < MPR_STATUS_READY) {
        // check if mapping is now "ready"
        _check_status(m);
    }
    return updated;
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
        if (MSG_MAPPED == cmd || (MPR_DIR_OUT == m->dst->dir))
            mpr_slot_add_props_to_msg(msg, m->src[i], 0, staged);
    }

    /* destination properties */
    if (MSG_MAPPED == cmd || (MPR_DIR_IN == m->dst->dir))
        mpr_slot_add_props_to_msg(msg, m->dst, 1, staged);

    // add public expression variables
    int j, k, l;
    char varname[32];
    if (m->loc && m->loc->expr) {
        for (j = 0; j < m->loc->num_vars; j++) {
            if (!mpr_expr_get_var_is_public(m->loc->expr, j))
                continue;
            // TODO: handle multiple instances
            k = 0;
            if (m->loc->vars[k][j].pos >= 0) {
                snprintf(varname, 32, "@var@%s", mpr_expr_get_var_name(m->loc->expr, j));
                lo_message_add_string(msg, varname);
                double *v = m->loc->vars[k][j].samps + m->loc->vars[k][j].pos;
                for (l = 0; l < m->loc->vars[k][j].len; l++)
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
