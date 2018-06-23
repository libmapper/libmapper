#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include <zlib.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

/*! Function prototypes. */
static void reallocate_map_histories(mapper_map map);

static int alphabetise_signals(int num, mapper_signal *sigs, int *order)
{
    int i, j, result1 = 1, result2 = 1;
    for (i = 0; i < num; i++)
        order[i] = i;
    for (i = 1; i < num; i++) {
        j = i-1;
        while (j >= 0
               && (((result1 = strcmp(sigs[order[j]]->dev->name,
                                      sigs[order[j+1]]->dev->name)) > 0)
                   || ((result2 = strcmp(sigs[order[j]]->name,
                                         sigs[order[j+1]]->name)) > 0))) {
                   int temp = order[j];
                   order[j] = order[j+1];
                   order[j+1] = temp;
                   --j;
        }
        if (result1 == 0 && result2 == 0)
            return 1;
    }
    return 0;
}

void mapper_map_init(mapper_map map)
{
    map->obj.props.mask = 0;
    map->obj.props.synced = mapper_table_new();
    map->obj.props.staged = mapper_table_new();

    mapper_table tab = map->obj.props.synced;

    // these properties need to be added in alphabetical order
    mapper_table_link(tab, MAPPER_PROP_EXPR, 1, MAPPER_STRING, &map->expr_str,
                      MODIFIABLE | INDIRECT);

    mapper_table_link(tab, MAPPER_PROP_ID, 1, MAPPER_INT64, &map->obj.id,
                      NON_MODIFIABLE | LOCAL_ACCESS_ONLY);

    mapper_table_link(tab, MAPPER_PROP_MUTED, 1, MAPPER_BOOL, &map->muted,
                      MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_NUM_INPUTS, 1, MAPPER_INT32,
                      &map->num_src, NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_PROCESS_LOC, 1, MAPPER_INT32,
                      &map->process_loc, MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_PROTOCOL, 1, MAPPER_INT32,
                      &map->protocol, REMOTE_MODIFY);

    mapper_table_link(tab, MAPPER_PROP_STATUS, 1, MAPPER_INT32, &map->status,
                      NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_USER_DATA, 1, MAPPER_PTR, &map->obj.user,
                      MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);

    mapper_table_link(tab, MAPPER_PROP_VERSION, 1, MAPPER_INT32,
                      &map->obj.version, REMOTE_MODIFY);

    int i, is_local = 0;
    if (map->dst->sig->local)
        is_local = 1;
    else {
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->sig->local) {
                is_local = 1;
                break;
            }
        }
    }
    mapper_table_set_record(tab, MAPPER_PROP_IS_LOCAL, NULL, 1, MAPPER_BOOL,
                            &is_local, LOCAL_ACCESS_ONLY | NON_MODIFIABLE);

    for (i = 0; i < map->num_src; i++)
        mapper_slot_init(map->src[i]);
    mapper_slot_init(map->dst);
}

mapper_map mapper_map_new(int num_src, mapper_signal *src,
                          int num_dst, mapper_signal *dst)
{
    int i, j;
    if (!src || !*src || !dst || !*dst)
        return 0;
    if (num_src <= 0 || num_src > MAX_NUM_MAP_SRC)
        return 0;

    for (i = 0; i < num_src; i++) {
        for (j = 0; j < num_dst; j++) {
            if (src[i]->obj.id == dst[j]->obj.id) {
                trace("Cannot connect signal '%s:%s' to itself.\n",
                      mapper_device_get_name(src[i]->dev), src[i]->name);
                return 0;
            }
        }
    }

    // Only 1 destination supported for now
    if (num_dst != 1)
        return 0;

    mapper_graph g = (*dst)->obj.graph;

    // check if record of map already exists
    mapper_map map;
    mapper_object obj, *maps, *temp;
    maps = mapper_signal_get_maps(*dst, MAPPER_DIR_IN);
    if (maps) {
        for (i = 0; i < num_src; i++) {
            obj = mapper_graph_get_object(g, MAPPER_OBJ_SIGNAL, src[i]->obj.id);
            if (obj) {
                temp = mapper_signal_get_maps((mapper_signal)obj, MAPPER_DIR_OUT);
                maps = mapper_object_list_intersection(maps, temp);
            }
            else {
                mapper_object_list_free(maps);
                maps = 0;
                break;
            }
        }
        while (maps) {
            if (((mapper_map)*maps)->num_src == num_src) {
                map = (mapper_map)*maps;
                mapper_object_list_free(maps);
                return map;
            }
            maps = mapper_object_list_next(maps);
        }
    }

    int order[num_src];
    if (alphabetise_signals(num_src, src, order)) {
        trace("error in mapper_map_new(): multiple use of source signal.\n");
        return 0;
    }

    map = (mapper_map)mapper_list_add_item((void**)&g->maps, sizeof(mapper_map_t));
    map->obj.type = MAPPER_OBJ_MAP;
    map->obj.graph = g;
    map->num_src = num_src;
    map->src = (mapper_slot*) malloc(sizeof(mapper_slot) * num_src);
    for (i = 0; i < num_src; i++) {
        map->src[i] = (mapper_slot)calloc(1, sizeof(struct _mapper_slot));
        obj = mapper_graph_get_object(g, MAPPER_OBJ_SIGNAL, src[order[i]]->obj.id);
        if (!obj) {
            obj = ((mapper_object)
                   mapper_graph_add_or_update_signal(g, src[order[i]]->name,
                                                     src[order[i]]->dev->name, 0));
            if (!obj->id) {
                obj->id = src[order[i]]->obj.id;
                ((mapper_signal)obj)->dir = src[order[i]]->dir;
                ((mapper_signal)obj)->len = src[order[i]]->len;
                ((mapper_signal)obj)->type = src[order[i]]->type;
            }
            mapper_device dev = ((mapper_signal)obj)->dev;
            if (!dev->obj.id) {
                dev->obj.id = src[order[i]]->dev->obj.id;
            }
        }
        map->src[i]->sig = (mapper_signal)obj;
        map->src[i]->map = map;
        map->src[i]->obj.id = i;
    }
    map->dst = (mapper_slot)calloc(1, sizeof(struct _mapper_slot));
    map->dst->sig = *dst;
    map->dst->map = map;
    map->dst->dir = MAPPER_DIR_IN;

    // we need to give the map a temporary id – this may be overwritten later
    if ((*dst)->dev->local)
        map->obj.id = mapper_device_generate_unique_id((*dst)->dev);

    mapper_map_init(map);

    map->status = STATUS_STAGED;
    map->protocol = MAPPER_PROTO_UDP;

    return map;
}

void mapper_map_release(mapper_map map)
{
    mapper_network_bus(&map->obj.graph->net);
    mapper_map_send_state(map, -1, MSG_UNMAP);
}

void mapper_map_refresh(mapper_map map)
{
    if (!map)
        return;
    mapper_network_bus(&map->obj.graph->net);
    mapper_map_send_state(map, -1, map->local ? MSG_MAP_TO : MSG_MAP);
}

void mapper_map_free(mapper_map map)
{
    int i;
    if (map->src) {
        for (i = 0; i < map->num_src; i++) {
            mapper_slot_free(map->src[i]);
            free(map->src[i]);
        }
        free(map->src);
    }
    if (map->dst) {
        mapper_slot_free(map->dst);
        free(map->dst);
    }
    if (map->num_scopes && map->scopes) {
        free(map->scopes);
    }
    if (map->obj.props.synced)
        mapper_table_free(map->obj.props.synced);
    if (map->obj.props.staged)
        mapper_table_free(map->obj.props.staged);
    if (map->expr_str)
        free(map->expr_str);
}

int mapper_map_get_num_signals(mapper_map map, mapper_location loc)
{
    int count = 0;
    if (loc & MAPPER_LOC_SRC)
        count += map->num_src;
    if (loc & MAPPER_LOC_DST)
        count += 1;
    return count;
}

mapper_signal mapper_map_get_signal(mapper_map map, mapper_location loc, int idx)
{
    if (loc & MAPPER_LOC_SRC) {
        if (idx < map->num_src)
            return map->src[idx]->sig;
        idx -= map->num_src;
    }
    return (loc & MAPPER_LOC_DST) ? map->dst->sig : NULL;
}

mapper_slot mapper_map_get_slot_by_signal(mapper_map map, mapper_signal sig)
{
    int i;
    if (map->dst->sig->obj.id == sig->obj.id)
        return map->dst;
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->sig->obj.id == sig->obj.id)
            return map->src[i];
    }
    return 0;
}

int mapper_map_get_signal_index(mapper_map map, mapper_signal sig)
{
    int i;
    if (map->dst->sig->obj.id == sig->obj.id)
        return 0;
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->sig->obj.id == sig->obj.id)
            return i;
    }
    return -1;
}

static int cmp_query_map_scopes(const void *context_data, mapper_device dev)
{
    int num_scopes = *(int*)context_data;
    mapper_device *scopes = (mapper_device*)(context_data + sizeof(int));
    for (int i = 0; i < num_scopes; i++) {
        if (!scopes[i] || scopes[i]->obj.id == dev->obj.id)
            return 1;
    }
    return 0;
}

mapper_object *mapper_map_get_scopes(mapper_map map)
{
    if (!map || !map->num_scopes)
        return 0;
    return ((mapper_object *)
            mapper_list_new_query(map->obj.graph->devs, cmp_query_map_scopes,
                                  "iv", map->num_scopes, &map->scopes));
}

int mapper_map_ready(mapper_map map)
{
    return map ? (map->status == STATUS_ACTIVE) : 0;
}

void mapper_map_add_scope(mapper_map map, mapper_device device)
{
    if (!map)
        return;
    mapper_property prop = MAPPER_PROP_SCOPE | PROP_ADD;
    mapper_table_record_t *rec = mapper_table_record(map->obj.props.staged,
                                                     prop, NULL);
    if (rec && rec->type == MAPPER_STRING) {
        const char *names[rec->len+1];
        if (rec->len == 1) {
            names[0] = (const char*)rec->val;
        }
        for (int i = 0; i < rec->len; i++) {
            names[i] = ((const char**)rec->val)[i];
        }
        names[rec->len] = device ? device->name : "all";
        mapper_table_set_record(map->obj.props.staged, prop, NULL, rec->len + 1,
                                MAPPER_STRING, names, REMOTE_MODIFY);
    }
    else
        mapper_table_set_record(map->obj.props.staged, prop, NULL, 1,
                                MAPPER_STRING, device->name, REMOTE_MODIFY);
}

void mapper_map_remove_scope(mapper_map map, mapper_device device)
{
    if (!map || !device)
        return;
    mapper_property prop = MAPPER_PROP_SCOPE | PROP_REMOVE;
    mapper_table_record_t *rec = mapper_table_record(map->obj.props.staged,
                                                     prop, NULL);
    if (rec && rec->type == MAPPER_STRING) {
        const char *names[rec->len+1];
        if (rec->len == 1) {
            names[0] = (const char*)rec->val;
        }
        for (int i = 0; i < rec->len; i++) {
            names[i] = ((const char**)rec->val)[i];
        }
        names[rec->len] = device->name;
        mapper_table_set_record(map->obj.props.staged, prop, NULL, rec->len + 1,
                                MAPPER_STRING, names, REMOTE_MODIFY);
    }
    else
        mapper_table_set_record(map->obj.props.staged, prop, NULL, 1,
                                MAPPER_STRING, device->name, REMOTE_MODIFY);
}

static int add_scope_internal(mapper_map map, const char *name)
{
    int i;
    if (!map || !name)
        return 0;
    mapper_device dev = 0;

    if (strcmp(name, "all")==0) {
        for (i = 0; i < map->num_scopes; i++) {
            if (!map->scopes[i])
                return 0;
        }
    }
    else {
        dev = mapper_graph_add_or_update_device(map->obj.graph, name, 0);
        for (i = 0; i < map->num_scopes; i++) {
            if (map->scopes[i] && map->scopes[i]->obj.id == dev->obj.id)
                return 0;
        }
    }

    // not found - add a new scope
    i = ++map->num_scopes;
    map->scopes = realloc(map->scopes, i * sizeof(mapper_device));
    map->scopes[i-1] = dev;
    return 1;
}

static int remove_scope_internal(mapper_map map, const char *name)
{
    int i;
    if (!map || !name)
        return 0;
    if (strcmp(name, "all")==0)
        name = 0;
    for (i = 0; i < map->num_scopes; i++) {
        if (!map->scopes[i]) {
            if (!name)
                break;
        }
        else if (name && strcmp(map->scopes[i]->name, name) == 0)
            break;
    }
    if (i == map->num_scopes)
        return 0;

    // found - remove scope at index i
    for (; i < map->num_scopes; i++) {
        map->scopes[i-1] = map->scopes[i];
    }
    --map->num_scopes;
    map->scopes = realloc(map->scopes, map->num_scopes * sizeof(mapper_device));
    return 1;
}

static int mapper_map_update_scope(mapper_map map, mapper_msg_atom atom)
{
    int i, j, updated = 0, num = atom->len;
    lo_arg **scope_list = atom->vals;
    if (scope_list && *scope_list) {
        if (num == 1 && strcmp(&scope_list[0]->s, "none")==0)
            num = 0;
        const char *name, *no_slash;

        // First remove old scopes that are missing
        for (i = 0; i < map->num_scopes; i++) {
            int found = 0;
            for (j = 0; j < num; j++) {
                name = &scope_list[j]->s;
                if (!map->scopes[i]) {
                    if (strcmp(name, "all") == 0) {
                        found = 1;
                        break;
                    }
                    break;
                }
                no_slash = name[0] == '/' ? name + 1 : name;
                if (strcmp(no_slash, map->scopes[i]->name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                remove_scope_internal(map, &scope_list[i]->s);
                ++updated;
            }
        }
        // ...then add any new scopes
        for (i = 0; i < num; i++) {
            updated += add_scope_internal(map, &scope_list[i]->s);
        }
    }
    return updated;
}

// only called for outgoing maps
int mapper_map_perform(mapper_map map, mapper_slot slot, int inst,
                       mapper_type *types)
{
    int changed = 0, i;
    mapper_hist from = slot->local->hist;
    mapper_hist to = map->dst->local->hist;

    if (slot->calib) {
        if (!slot->min) {
            slot->min = malloc(slot->sig->len * mapper_type_size(slot->sig->type));
        }
        if (!slot->max) {
            slot->max = malloc(slot->sig->len * mapper_type_size(slot->sig->type));
        }

        /* If calibration has just taken effect, first data sample sets source
         * min and max. */
        switch (slot->sig->type) {
            case MAPPER_FLOAT: {
                float *v = mapper_hist_val_ptr(from[inst]);
                float *src_min = (float*)slot->min;
                float *src_max = (float*)slot->max;
                if (slot->calib == 1) {
                    for (i = 0; i < from->len; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    slot->calib = 2;
                    changed = 1;
                }
                else {
                    for (i = 0; i < from->len; i++) {
                        if (v[i] < src_min[i]) {
                            src_min[i] = v[i];
                            changed = 1;
                        }
                        if (v[i] > src_max[i]) {
                            src_max[i] = v[i];
                            changed = 1;
                        }
                    }
                }
                break;
            }
            case MAPPER_INT32: {
                int *v = mapper_hist_val_ptr(from[inst]);
                int *src_min = (int*)slot->min;
                int *src_max = (int*)slot->max;
                if (slot->calib == 1) {
                    for (i = 0; i < from->len; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    slot->calib = 2;
                    changed = 1;
                }
                else {
                    for (i = 0; i < from->len; i++) {
                        if (v[i] < src_min[i]) {
                            src_min[i] = v[i];
                            changed = 1;
                        }
                        if (v[i] > src_max[i]) {
                            src_max[i] = v[i];
                            changed = 1;
                        }
                    }
                }
                break;
            }
            case MAPPER_DOUBLE: {
                double *v = mapper_hist_val_ptr(from[inst]);
                double *src_min = (double*)slot->min;
                double *src_max = (double*)slot->max;
                if (slot->calib == 1) {
                    for (i = 0; i < from->len; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    slot->calib = 2;
                    changed = 1;
                }
                else {
                    for (i = 0; i < from->len; i++) {
                        if (v[i] < src_min[i]) {
                            src_min[i] = v[i];
                            changed = 1;
                        }
                        if (v[i] > src_max[i]) {
                            src_max[i] = v[i];
                            changed = 1;
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    if (map->status != STATUS_ACTIVE || map->muted) {
        return 0;
    }
    else if (map->process_loc == MAPPER_LOC_DST) {
        to[inst].pos = 0;
        // copy value without type coercion
        memcpy(mapper_hist_val_ptr(to[inst]),
               mapper_hist_val_ptr(from[inst]),
               mapper_signal_vector_bytes(slot->sig));
        // copy time
        memcpy(mapper_hist_time_ptr(to[inst]),
               mapper_hist_time_ptr(from[inst]),
               sizeof(mapper_time_t));
        for (i = 0; i < from->len; i++)
            types[i] = map->src[0]->sig->type;
        return 1;
    }

    if (!map->local->expr) {
        trace("error: missing expression.\n");
        return 0;
    }

    mapper_hist src[map->num_src];
    for (i = 0; i < map->num_src; i++)
        src[i] = &map->src[i]->local->hist[inst];
    return (mapper_expr_eval(map->local->expr, src, &map->local->expr_var[inst],
                             &to[inst], mapper_hist_time_ptr(from[inst]), types));
}

/*! Build a value update message for a given map. */
lo_message mapper_map_build_msg(mapper_map map, mapper_slot slot, const void *val,
                                int count, mapper_type *types, mapper_idmap idmap)
{
    int i;
    int len = ((map->process_loc == MAPPER_LOC_SRC)
               ? map->dst->sig->len * count : slot->sig->len * count);

    lo_message msg = lo_message_new();
    if (!msg)
        return 0;

    if (val && types) {
        for (i = 0; i < len; i++) {
            switch (types[i]) {
                case MAPPER_INT32:
                    lo_message_add_int32(msg, ((int*)val)[i]);
                    break;
                case MAPPER_FLOAT:
                    lo_message_add_float(msg, ((float*)val)[i]);
                    break;
                case MAPPER_DOUBLE:
                    lo_message_add_double(msg, ((double*)val)[i]);
                    break;
                case MAPPER_NULL:
                    lo_message_add_nil(msg);
                    break;
                default:
                    break;
            }
        }
    }
    else if (idmap) {
        for (i = 0; i < len; i++)
            lo_message_add_nil(msg);
    }

    if (idmap) {
        lo_message_add_string(msg, "@instance");
        lo_message_add_int64(msg, idmap->global);
    }

    if (map->process_loc == MAPPER_LOC_DST) {
        // add slot
        lo_message_add_string(msg, "@slot");
        lo_message_add_int32(msg, slot->obj.id);
    }

    return msg;
}

/* Helper to replace a map's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expr_string(mapper_map map, const char *expr_str)
{
    if (map->local->expr && map->expr_str && strcmp(map->expr_str, expr_str)==0)
        return 1;

    int i;
    char src_types[map->num_src];
    int src_lens[map->num_src];
    for (i = 0; i < map->num_src; i++) {
        src_types[i] = map->src[i]->sig->type;
        src_lens[i] = map->src[i]->sig->len;
    }
    mapper_expr expr = mapper_expr_new_from_string(expr_str, map->num_src,
                                                   src_types, src_lens,
                                                   map->dst->sig->type,
                                                   map->dst->sig->len);

    if (!expr)
        return 1;

    // expression update may force processing location to change
    // e.g. if expression combines signals from different devices
    // e.g. if expression refers to current/past value of destination
    int out_hist_size = mapper_expr_out_hist_size(expr);
    if (out_hist_size > 1 && map->process_loc == MAPPER_LOC_SRC) {
        map->process_loc = MAPPER_LOC_DST;
        if (!map->dst->sig->local) {
            // copy expression string but do not execute it
            mapper_table_set_record(map->obj.props.synced, MAPPER_PROP_EXPR,
                                    NULL, 1, MAPPER_STRING, expr_str,
                                    REMOTE_MODIFY);
            mapper_expr_free(expr);
            return 1;
        }
    }

    if (map->local->expr)
        mapper_expr_free(map->local->expr);

    map->local->expr = expr;

    if (map->expr_str == expr_str)
        return 0;

    mapper_table_set_record(map->obj.props.synced, MAPPER_PROP_EXPR, NULL, 1,
                            MAPPER_STRING, expr_str, REMOTE_MODIFY);

    return 0;
}

const char *mapper_map_set_linear(mapper_map map, int slen, mapper_type stype,
                                  const void *smin, const void *smax, int dlen,
                                  mapper_type dtype, const void *dmin,
                                  const void *dmax)
{
    int i, min_len, str_len;
    char expr[256] = "";
    const char *e = expr;

    if (smin)
        mapper_table_set_record(map->src[0]->obj.props.synced, MAPPER_PROP_MIN,
                                NULL, slen, stype, smin, REMOTE_MODIFY);
    if (smax)
        mapper_table_set_record(map->src[0]->obj.props.synced, MAPPER_PROP_MAX,
                                NULL, slen, stype, smax, REMOTE_MODIFY);
    if (dmin)
        mapper_table_set_record(map->dst->obj.props.synced, MAPPER_PROP_MIN,
                                NULL, dlen, dtype, dmin, REMOTE_MODIFY);
    if (dmax)
        mapper_table_set_record(map->dst->obj.props.synced, MAPPER_PROP_MAX,
                                NULL, dlen, dtype, dmax, REMOTE_MODIFY);

    slen = map->src[0]->sig->len;
    stype = map->src[0]->sig->type;
    smin = map->src[0]->min;
    smax = map->src[0]->max;

    dlen = map->dst->sig->len;
    dtype = map->dst->sig->type;
    dmin = map->dst->min;
    dmax = map->dst->max;

    if (!smin || !smax || !dmin || !dmax)
        return 0;

    min_len = (slen < dlen ? slen : dlen);
    double src_mind = 0, src_maxd = 0, dst_mind = 0, dst_maxd = 0;

    if (dlen == slen)
        snprintf(expr, 256, "y=x*");
    else if (dlen > slen) {
        if (min_len == 1)
            snprintf(expr, 256, "y[0]=x*");
        else
            snprintf(expr, 256, "y[0:%i]=x*", min_len-1);
    }
    else {
        if (min_len == 1)
            snprintf(expr, 256, "y=x[0]*");
        else
            snprintf(expr, 256, "y=x[0:%i]*", min_len-1);
    }

    if (min_len > 1) {
        str_len = strlen(expr);
        snprintf(expr+str_len, 256-str_len, "[");
    }

    // add multiplier
    for (i = 0; i < min_len; i++) {
        src_mind = propval_get_double(smin, stype, (i<slen) ? i : slen-1);
        src_maxd = propval_get_double(smax, stype, (i<slen) ? i : slen-1);
        str_len = strlen(expr);
        if (src_mind == src_maxd)
            snprintf(expr+str_len, 256-str_len, "0,");
        else {
            dst_mind = propval_get_double(dmin, dtype, (i<dlen) ? i : dlen-1);
            dst_maxd = propval_get_double(dmax, dtype, (i<dlen) ? i : dlen-1);
            if ((src_mind == dst_mind) && (src_maxd == dst_maxd)) {
                snprintf(expr+str_len, 256-str_len, "1,");
            }
            else {
                double scale = ((dst_mind - dst_maxd) / (src_mind - src_maxd));
                snprintf(expr+str_len, 256-str_len, "%g,", scale);
            }
        }
    }
    str_len = strlen(expr);
    if (min_len > 1)
        snprintf(expr+str_len-1, 256-str_len+1, "]+[");
    else
        snprintf(expr+str_len-1, 256-str_len+1, "+");

    // add offset
    for (i = 0; i < min_len; i++) {
        src_mind = propval_get_double(smin, stype, (i<slen) ? i : slen-1);
        src_maxd = propval_get_double(smax, stype, (i<slen) ? i : slen-1);
        str_len = strlen(expr);
        if (src_mind == src_maxd)
            snprintf(expr+str_len, 256-str_len, "%g,", dst_mind);
        else {
            dst_mind = propval_get_double(dmin, dtype, (i<dlen) ? i : dlen-1);
            dst_maxd = propval_get_double(dmax, dtype, (i<dlen) ? i : dlen-1);
            if ((src_mind == dst_mind) && (src_maxd == dst_maxd)) {
                snprintf(expr+str_len, 256-str_len, "0,");
            }
            else {
                double offset = ((dst_maxd * src_mind - dst_mind * src_maxd)
                                 / (src_mind - src_maxd));
                snprintf(expr+str_len, 256-str_len, "%g,", offset);
            }
        }
    }
    str_len = strlen(expr);
    if (min_len > 1)
        snprintf(expr+str_len-1, 256-str_len+1, "]");
    else
        expr[str_len-1] = '\0';

    // If everything is successful, replace the map's expression.
    if (e) {
        int should_compile = 0;
        if (map->local) {
            if (map->local->is_local_only)
                should_compile = 1;
            else if (map->process_loc == MAPPER_LOC_DST) {
                // check if destination is local
                if (map->dst->local->rsig)
                    should_compile = 1;
            }
            else {
                for (i = 0; i < map->num_src; i++) {
                    if (map->src[i]->local->rsig)
                        should_compile = 1;
                }
            }
        }
        if (should_compile) {
            if (!replace_expr_string(map, e))
                reallocate_map_histories(map);
        }
        else {
            mapper_table_set_record(map->obj.props.synced, MAPPER_PROP_EXPR,
                                    NULL, 1, MAPPER_STRING, e, REMOTE_MODIFY);
        }
    }
    return map->expr_str;
}

static void mapper_map_set_expr(mapper_map map, const char *expr)
{
    int i, should_compile = 0;
    if (map->local->is_local_only)
        should_compile = 1;
    else if (map->process_loc == MAPPER_LOC_DST) {
        // check if destination is local
        if (map->dst->local->rsig)
            should_compile = 1;
    }
    else {
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->local->rsig)
                should_compile = 1;
        }
    }
    if (should_compile) {
        if (!replace_expr_string(map, expr))
            reallocate_map_histories(map);
        else
            return;
    }
    else {
        mapper_table_set_record(map->obj.props.synced, MAPPER_PROP_EXPR, NULL,
                                1, MAPPER_STRING, expr, REMOTE_MODIFY);
        return;
    }

    /* Special case: if we are the receiver and the new expression evaluates to
     * a constant we can update immediately. */
    /* TODO: should call handler for all instances updated through this map. */
    int use_inst = 0;
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->use_inst) {
            use_inst = 1;
            break;
        }
    }
    use_inst += map->dst->use_inst;
    if (mapper_expr_constant_output(map->local->expr) && !use_inst) {
        mapper_time_t now;
        mapper_time_now(&now);

        // evaluate expression
        mapper_expr_eval(map->local->expr, 0, 0, map->dst->local->hist, &now, 0);

        // call handler if it exists
        if (map->dst->local) {
            mapper_signal sig = map->dst->local->rsig->sig;
            mapper_signal_update_handler *h = sig->local->update_handler;
            if (h)
                h(sig, 0, sig->len, sig->type,
                  &map->dst->local->hist[0].val, &now);
        }
    }

    // check whether each source slot causes computation
    for (i = 0; i < map->num_src; i++) {
        map->src[i]->causes_update = !mapper_expr_src_muted(map->local->expr, i);
    }
}

// TODO: move to slot.c?
static void init_slot_hist(mapper_slot slot)
{
    int i;
    if (slot->local->hist) {
        return;
    }
    slot->local->hist = malloc(sizeof(struct _mapper_hist) * slot->num_inst);
    slot->local->hist_size = 1;
    for (i = 0; i < slot->num_inst; i++) {
        slot->local->hist[i].type = slot->sig->type;
        slot->local->hist[i].len = slot->sig->len;
        slot->local->hist[i].size = 1;
        slot->local->hist[i].val = calloc(1, mapper_type_size(slot->sig->type)
                                          * slot->sig->len);
        slot->local->hist[i].time = calloc(1, sizeof(mapper_time_t));
        slot->local->hist[i].pos = -1;
    }
}

static void apply_expr(mapper_map map)
{
    if (map->process_loc == MAPPER_LOC_DST && !map->dst->local)
        return;

    // try user-defined expression
    if (map->expr_str) {
        mapper_map_set_expr(map, map->expr_str);
        return;
    }

    // try linear scaling
    mapper_signal s = map->src[0]->sig, d = map->dst->sig;
    if (mapper_map_set_linear(map, s->len, s->type, s->min, s->max,
                              d->len, d->type, d->min, d->max))
        return;

    // try linear combination of inputs
    char expr_str[256] = "";
    if (map->num_src == 1) {
        if (map->src[0]->sig->len == map->dst->sig->len)
            snprintf(expr_str, 256, "y=x");
        else {
            if (map->src[0]->sig->len > map->dst->sig->len) {
                // truncate source
                if (map->dst->sig->len == 1)
                    snprintf(expr_str, 256, "y=x[0]");
                else
                    snprintf(expr_str, 256, "y=x[0:%i]", map->dst->sig->len-1);
            }
            else {
                // truncate dst
                if (map->src[0]->sig->len == 1)
                    snprintf(expr_str, 256, "y[0]=x");
                else
                    snprintf(expr_str, 256, "y[0:%i]=x", map->src[0]->sig->len-1);
            }
        }
    }
    else {
        // check vector lengths
        int i, j, max_vec_len = 0, min_vec_len = INT_MAX;
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->sig->len > max_vec_len)
                max_vec_len = map->src[i]->sig->len;
            if (map->src[i]->sig->len < min_vec_len)
                min_vec_len = map->src[i]->sig->len;
        }
        int offset = 0, dst_vec_len;
        if (max_vec_len < map->dst->sig->len) {
            snprintf(expr_str, 256, "y[0:%d]=(", max_vec_len-1);
            offset = strlen(expr_str);
            dst_vec_len = max_vec_len;
        }
        else {
            snprintf(expr_str, 256, "y=(");
            offset = 3;
            dst_vec_len = map->dst->sig->len;
        }
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->sig->len > dst_vec_len) {
                snprintf(expr_str + offset, 256 - offset,
                         "x%d[0:%d]+", i, dst_vec_len-1);
                offset = strlen(expr_str);
            }
            else if (map->src[i]->sig->len < dst_vec_len) {
                snprintf(expr_str + offset, 256 - offset,
                         "[x%d,0", i);
                offset = strlen(expr_str);
                for (j = 1; j < dst_vec_len - map->src[0]->sig->len; j++) {
                    snprintf(expr_str + offset, 256 - offset, ",0");
                    offset += 2;
                }
                snprintf(expr_str + offset, 256 - offset, "]+");
                offset += 2;
            }
            else {
                snprintf(expr_str + offset, 256 - offset, "x%d+", i);
                offset = strlen(expr_str);
            }
        }
        --offset;
        snprintf(expr_str + offset, 256 - offset, ")/%d",
                 map->num_src);
    }
    mapper_map_set_expr(map, expr_str);
}

static int mapper_map_check_status(mapper_map map)
{
    if (bitmatch(map->status, STATUS_READY))
        return map->status;

    map->status |= STATUS_LINK_KNOWN;
    int mask = ~STATUS_LINK_KNOWN;
    if (map->dst->local->rsig || (map->dst->link && map->dst->link
                                  && map->dst->link->addr.udp))
        map->dst->local->status |= STATUS_LINK_KNOWN;
    map->status &= (map->dst->local->status | mask);

    int i;
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->local->rsig || (map->src[i]->link && map->src[i]->link
                                         && map->src[i]->link->addr.udp))
            map->src[i]->local->status |= STATUS_LINK_KNOWN;
        map->status &= (map->src[i]->local->status | mask);
    }

    if (map->status == STATUS_LINK_KNOWN) {
        // allocate memory for map history
        for (i = 0; i < map->num_src; i++) {
            init_slot_hist(map->src[i]);
        }
        init_slot_hist(map->dst);
        if (!map->local->expr_var) {
            map->local->expr_var = calloc(1, sizeof(mapper_hist*)
                                          * map->local->num_var_inst);
        }
        map->status = STATUS_READY;
        // update in/out counts for link
        if (map->local->is_local_only) {
            if (map->dst->link)
                ++map->dst->link->num_maps[0];
        }
        else {
            if (map->dst->link) {
                ++map->dst->link->num_maps[0];
                map->dst->link->obj.props.synced->dirty = 1;
            }
            mapper_link last = 0, link;
            for (i = 0; i < map->num_src; i++) {
                link = map->src[i]->link;
                if (link && link != last) {
                    ++map->src[i]->link->num_maps[1];
                    map->src[i]->link->obj.props.synced->dirty = 1;
                    last = link;
                }
            }
        }
        apply_expr(map);
    }
    return map->status;
}

// if 'override' flag is not set, only remote properties can be set
int mapper_map_set_from_msg(mapper_map map, mapper_msg msg, int override)
{
    int i, j, updated = 0;
    mapper_msg_atom atom;
    if (!msg) {
        if (map->local && map->status < STATUS_READY) {
            // check if mapping is now "ready"
            mapper_map_check_status(map);
        }
        return 0;
    }

    if (map->dst->dir == MAPPER_DIR_OUT) {
        // check if MAPPER_PROP_SLOT property is defined
        atom = mapper_msg_prop(msg, MAPPER_PROP_SLOT);
        if (atom && atom->len == map->num_src) {
            mapper_table tab;
            mapper_table_record_t *rec;
            for (i = 0; i < map->num_src; i++) {
                int id = (atom->vals[i])->i32;
                map->src[i]->obj.id = id;
                // also need to correct slot table indices
                tab = map->src[i]->obj.props.synced;
                for (j = 0; j < tab->num_records; j++) {
                    rec = &tab->records[j];
                    rec->prop = MASK_PROP_BITFLAGS(rec->prop) | SRC_SLOT_PROP(id);
                }
                tab = map->src[i]->obj.props.staged;
                for (j = 0; j < tab->num_records; j++) {
                    rec = &tab->records[j];
                    rec->prop = MASK_PROP_BITFLAGS(rec->prop) | SRC_SLOT_PROP(id);
                }
            }
        }
    }

    // set destination slot properties
    int status = 0xFF;
    updated += mapper_slot_set_from_msg(map->dst, msg, &status);

    // set source slot properties
    for (i = 0; i < map->num_src; i++) {
        updated += mapper_slot_set_from_msg(map->src[i], msg, &status);
    }

    for (i = 0; i < msg->num_atoms; i++) {
        atom = &msg->atoms[i];

        switch (MASK_PROP_BITFLAGS(atom->prop)) {
            case MAPPER_PROP_NUM_INPUTS:
            case MAPPER_PROP_NUM_OUTPUTS:
                // these properties will be set by signal args
                break;
            case MAPPER_PROP_STATUS:
                if (map->local)
                    break;
                updated += mapper_table_set_record_from_atom(map->obj.props.synced,
                                                             atom, REMOTE_MODIFY);
                break;
            case MAPPER_PROP_PROCESS_LOC: {
                mapper_location loc;
                loc = mapper_loc_from_string(&(atom->vals[0])->s);
                if (map->local) {
                    if (loc == MAPPER_LOC_UNDEFINED) {
                        trace("map process location is undefined!\n");
                        break;
                    }
                    if (loc == MAPPER_LOC_ANY) {
                        // no effect
                        break;
                    }
                    if (!map->local->one_src) {
                        /* Processing must take place at destination if map
                         * includes source signals from different devices. */
                        loc = MAPPER_LOC_DST;
                    }
                }
                updated += mapper_table_set_record(map->obj.props.synced,
                                                   MAPPER_PROP_PROCESS_LOC,
                                                   NULL, 1, MAPPER_INT32, &loc,
                                                   REMOTE_MODIFY);
                break;
            }
            case MAPPER_PROP_EXPR: {
                const char *expr_str = &atom->vals[0]->s;
                mapper_location orig_loc = map->process_loc;
                if (map->local && bitmatch(map->status, STATUS_READY)) {
                    if (strstr(expr_str, "y{-")) {
                        map->process_loc = MAPPER_LOC_DST;
                    }
                    int should_compile = 0;
                    if (map->local->is_local_only)
                        should_compile = 1;
                    else if (map->process_loc == MAPPER_LOC_DST) {
                        // check if destination is local
                        if (map->dst->local->rsig)
                            should_compile = 1;
                    }
                    else {
                        for (j = 0; j < map->num_src; j++) {
                            if (map->src[j]->local->rsig)
                                should_compile = 1;
                        }
                    }
                    if (should_compile) {
                        if (!replace_expr_string(map, expr_str)) {
                            // TODO: don't increment updated counter twice
                            ++updated;
                            reallocate_map_histories(map);
                        }
                        else {
                            // restore original process location
                            map->process_loc = orig_loc;
                            break;
                        }
                    }
                }
                if (orig_loc != map->process_loc) {
                    mapper_location loc = map->process_loc;
                    map->process_loc = orig_loc;
                    updated += mapper_table_set_record(map->obj.props.synced,
                                                       MAPPER_PROP_PROCESS_LOC,
                                                       NULL, 1, MAPPER_INT32,
                                                       &loc, REMOTE_MODIFY);
                }
                updated += mapper_table_set_record(map->obj.props.synced,
                                                   MAPPER_PROP_EXPR,
                                                   NULL, 1, MAPPER_STRING,
                                                   expr_str, REMOTE_MODIFY);
                break;
            }
            case MAPPER_PROP_SCOPE:
                if (type_is_str(atom->types[0])) {
                    updated += mapper_map_update_scope(map, atom);
                }
                break;
            case MAPPER_PROP_SCOPE | PROP_ADD:
                for (j = 0; j < atom->len; j++)
                    updated += add_scope_internal(map, &(atom->vals[j])->s);
                break;
            case MAPPER_PROP_SCOPE | PROP_REMOVE:
                for (j = 0; j < atom->len; j++)
                    updated += remove_scope_internal(map, &(atom->vals[j])->s);
                break;
            case MAPPER_PROP_PROTOCOL: {
                mapper_protocol pro;
                pro = mapper_protocol_from_string(&(atom->vals[0])->s);
                updated += mapper_table_set_record(map->obj.props.synced,
                                                   MAPPER_PROP_PROTOCOL, NULL,
                                                   1, MAPPER_INT32, &pro,
                                                   REMOTE_MODIFY);
                break;
            }
            case MAPPER_PROP_EXTRA:
                if (!atom->prop)
                    break;
            case MAPPER_PROP_ID:
            case MAPPER_PROP_MUTED:
            case MAPPER_PROP_VERSION:
                updated += mapper_table_set_record_from_atom(map->obj.props.synced,
                                                             atom, REMOTE_MODIFY);
                break;
            default:
                break;
        }
    }

    if (map->local) {
        if (map->status < STATUS_READY) {
            // check if mapping is now "ready"
            mapper_map_check_status(map);
        }
        else if (updated)
            apply_expr(map);
    }
    return updated;
}

/* TODO: figuring out the correct number of instances for the user variables
 * is a bit tricky... for now we will use the maximum. */
void reallocate_map_histories(mapper_map map)
{
    int i, j;
    mapper_slot slot;
    mapper_local_slot slot_loc;
    int hist_size;

    // If there is no expression, then no memory needs to be reallocated.
    if (!map->local->expr)
        return;

    // Reallocate source histories
    for (i = 0; i < map->num_src; i++) {
        slot = map->src[i];
        slot_loc = slot->local;

        hist_size = mapper_expr_in_hist_size(map->local->expr, i);
        if (hist_size > slot_loc->hist_size) {
            size_t sample_size = mapper_type_size(slot->sig->type) * slot->sig->len;;
            for (j = 0; j < slot->num_inst; j++) {
                mapper_hist_realloc(&slot_loc->hist[j], hist_size, sample_size, 1);
            }
            slot_loc->hist_size = hist_size;
        }
        else if (hist_size < slot_loc->hist_size) {
            // Do nothing for now...
        }
    }

    hist_size = mapper_expr_out_hist_size(map->local->expr);
    slot = map->dst;
    slot_loc = slot->local;

    // reallocate output histories
    if (hist_size > slot_loc->hist_size) {
        int sample_size = mapper_type_size(slot->sig->type) * slot->sig->len;
        for (i = 0; i < slot->num_inst; i++) {
            mapper_hist_realloc(&slot_loc->hist[i], hist_size, sample_size, 0);
        }
        slot_loc->hist_size = hist_size;
    }
    else if (hist_size < slot_loc->hist_size) {
        // Do nothing for now...
    }

    // reallocate user variable histories
    int new_num_var = mapper_expr_num_vars(map->local->expr);
    if (new_num_var > map->local->num_expr_var) {
        for (i = 0; i < map->local->num_var_inst; i++) {
            map->local->expr_var[i] = realloc(map->local->expr_var[i],
                                              new_num_var *
                                              sizeof(struct _mapper_hist));
            // initialize new variables...
            for (j = map->local->num_expr_var; j < new_num_var; j++) {
                map->local->expr_var[i][j].type = MAPPER_DOUBLE;
                map->local->expr_var[i][j].len = 0;
                map->local->expr_var[i][j].size = 0;
                map->local->expr_var[i][j].pos = -1;
            }
            for (j = 0; j < new_num_var; j++) {
                int hist_size = mapper_expr_var_hist_size(map->local->expr, j);
                int vector_len = mapper_expr_var_vector_length(map->local->expr, j);
                mapper_hist_realloc(map->local->expr_var[i]+j, hist_size,
                                    vector_len * sizeof(double), 0);
                (map->local->expr_var[i]+j)->len = vector_len;
                (map->local->expr_var[i]+j)->size = hist_size;
                (map->local->expr_var[i]+j)->pos = -1;
            }
        }
        map->local->num_expr_var = new_num_var;
    }
    else if (new_num_var < map->local->num_expr_var) {
        // Do nothing for now...
    }
}

void mapper_hist_realloc(mapper_hist hist, int hist_size, int samp_size,
                         int is_input)
{
    if (!hist || !hist_size || !samp_size)
        return;
    if (hist_size == hist->size)
        return;
    if (!is_input || (hist_size > hist->size) || (hist->pos == 0)) {
        // realloc in place
        hist->val = realloc(hist->val, hist_size * samp_size);
        hist->time = realloc(hist->time, hist_size * sizeof(mapper_time_t));
        if (!is_input) {
            // Initialize entire history to 0
            memset(hist->val, 0, hist_size * samp_size);
            hist->pos = -1;
        }
        else if (hist->pos == 0) {
            memset(hist->val + samp_size * hist->size, 0,
                   samp_size * (hist_size - hist->size));
        }
        else {
            int new_pos = hist_size - hist->size + hist->pos;
            memcpy(hist->val + samp_size * new_pos,
                   hist->val + samp_size * hist->pos,
                   samp_size * (hist->size - hist->pos));
            memcpy(&hist->time[new_pos],
                   &hist->time[hist->pos], sizeof(mapper_time_t)
                   * (hist->size - hist->pos));
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
                   sizeof(mapper_time_t) * hist_size);
            hist->val = realloc(hist->val, hist_size * samp_size);
            hist->time = realloc(hist->time, hist_size * sizeof(mapper_time_t));
        }
        else {
            // there is overlap between new and old arrays - need to allocate new memory
            mapper_hist_t temp;
            temp.val = malloc(samp_size * hist_size);
            temp.time = malloc(sizeof(mapper_time_t) * hist_size);
            if (hist->pos < hist_size) {
                memcpy(temp.val, hist->val, samp_size * hist->pos);
                memcpy(temp.val + samp_size * hist->pos,
                       hist->val + samp_size * (hist->size - hist_size + hist->pos),
                       samp_size * (hist_size - hist->pos));
                memcpy(temp.time, hist->time,
                       sizeof(mapper_time_t) * hist->pos);
                memcpy(&temp.time[hist->pos],
                       &hist->time[hist->size - hist_size + hist->pos],
                       sizeof(mapper_time_t) * (hist_size - hist->pos));
            }
            else {
                memcpy(temp.val, hist->val + samp_size * (hist->pos - hist_size),
                       samp_size * hist_size);
                memcpy(temp.time, &hist->time[hist->pos - hist_size],
                       sizeof(mapper_time_t) * hist_size);
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
int mapper_map_send_state(mapper_map map, int slot, network_msg_t cmd)
{
    if (cmd == MSG_MAPPED && map->status < STATUS_READY)
        return slot;
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return slot;
    }

    char dst_name[256], src_names[1024];
    snprintf(dst_name, 256, "%s%s", map->dst->sig->dev->name,
             map->dst->sig->path);

    if (map->dst->dir == MAPPER_DIR_IN) {
        // add mapping destination
        lo_message_add_string(msg, dst_name);
        lo_message_add_string(msg, "<-");
    }

    // add mapping sources
    int i = (slot >= 0) ? slot : 0;
    int len = 0, result;
    mapper_link link = map->src[i]->local ? map->src[i]->link : 0;
    for (; i < map->num_src; i++) {
        if ((slot >= 0) && link && (link != map->src[i]->link))
            break;
        result = snprintf(&src_names[len], 1024-len, "%s%s",
                          map->src[i]->sig->dev->name, map->src[i]->sig->path);
        if (result < 0 || (len + result + 1) >= 1024) {
            trace("Error encoding sources for combined /mapped msg");
            lo_message_free(msg);
            return slot;
        }
        lo_message_add_string(msg, &src_names[len]);
        len += result + 1;
    }

    if (map->dst->dir == MAPPER_DIR_OUT || !map->dst->dir) {
        // add mapping destination
        lo_message_add_string(msg, "->");
        lo_message_add_string(msg, dst_name);
    }

    // Add unique id
    if (map->obj.id) {
        lo_message_add_string(msg, mapper_prop_protocol_string(MAPPER_PROP_ID));
        lo_message_add_int64(msg, *((int64_t*)&map->obj.id));
    }

    if (cmd == MSG_UNMAP || cmd == MSG_UNMAPPED) {
        mapper_network_add_msg(&map->obj.graph->net, 0, cmd, msg);
        return i-1;
    }

    // add other properties
    int staged = (cmd == MSG_MAP) || (cmd == MSG_MAP_MODIFY);
    mapper_table_add_to_msg(0, (staged ? map->obj.props.staged
                                : map->obj.props.synced), msg);

    if (!staged) {
        // add scopes
        if (map->num_scopes) {
            lo_message_add_string(msg, mapper_prop_protocol_string(MAPPER_PROP_SCOPE));
            for (i = 0; i < map->num_scopes; i++) {
                if (map->scopes[i])
                    lo_message_add_string(msg, map->scopes[i]->name);
                else
                    lo_message_add_string(msg, "all");
            }
        }
    }

    // add slot id
    if (map->dst->dir == MAPPER_DIR_IN && map->status < STATUS_READY && !staged) {
        lo_message_add_string(msg, mapper_prop_protocol_string(MAPPER_PROP_SLOT));
        i = (slot >= 0) ? slot : 0;
        link = map->src[i]->local ? map->src[i]->link : 0;
        for (; i < map->num_src; i++) {
            if ((slot >= 0) && link && (link != map->src[i]->link))
                break;
            lo_message_add_int32(msg, map->src[i]->obj.id);
        }
    }

    /* source properties */
    i = (slot >= 0) ? slot : 0;
    link = map->src[i]->local ? map->src[i]->link : 0;
    for (; i < map->num_src; i++) {
        if ((slot >= 0) && link && (link != map->src[i]->link))
            break;
        mapper_slot_add_props_to_msg(msg, map->src[i], 0, staged);
    }

    /* destination properties */
    mapper_slot_add_props_to_msg(msg, map->dst, 1, staged);

    mapper_network_add_msg(&map->obj.graph->net, 0, cmd, msg);

    return i-1;
}
