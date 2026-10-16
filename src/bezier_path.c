#include "bezier_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Initialize empty Bezier path */
/* Initialize empty PathCache */
void path_cache_init(PathCache *cache) {
    if (!cache) return;
    cache->x = NULL;
    cache->y = NULL;
    cache->S = NULL;
    cache->count = 0;
    cache->capacity = 0;
    cache->total_length = 0.0;
    cache->node_s = NULL;
    cache->node_count = 0;
    cache->baked_spans = NULL;
    cache->baked_span_count = 0;
    cache->baked_span_capacity = 0;
    cache->is_dirty = true;
}

/* Free PathCache memory */
void path_cache_free(PathCache *cache) {
    if (!cache) return;
    if (cache->x) free(cache->x);
    if (cache->y) free(cache->y);
    if (cache->S) free(cache->S);
    if (cache->node_s) free(cache->node_s);
    if (cache->baked_spans) free(cache->baked_spans);
    cache->x = NULL;
    cache->y = NULL;
    cache->S = NULL;
    cache->node_s = NULL;
    cache->baked_spans = NULL;
    cache->count = 0;
    cache->capacity = 0;
    cache->total_length = 0.0;
    cache->node_count = 0;
    cache->baked_span_count = 0;
    cache->baked_span_capacity = 0;
    cache->is_dirty = true;
}

/* Invalidate cache on geometry changes */
void path_cache_invalidate(BezierPath *path) {
    if (!path) return;
    path->cache.is_dirty = true;
}

/* Initialize empty Bezier path */
void bezier_path_init(BezierPath *path, const char *name) {
    if (!path) return;
    memset(path, 0, sizeof(BezierPath));
    if (name && name[0]) {
        snprintf(path->name, sizeof(path->name), "%s", name);
    } else {
        snprintf(path->name, sizeof(path->name), "curve");
    }
    path->next_node_id = 1;
    path->skullrot = -1;
    path->spans = NULL;
    path->span_count = 0;
    path->span_capacity = 0;
    path_cache_init(&path->cache);
}

/* Free path memory */
void bezier_path_free(BezierPath *path) {
    if (!path) return;
    if (path->nodes) {
        free(path->nodes);
        path->nodes = NULL;
    }
    if (path->spans) {
        free(path->spans);
        path->spans = NULL;
    }
    path->node_count = 0;
    path->node_capacity = 0;
    path->span_count = 0;
    path->span_capacity = 0;
    path_cache_free(&path->cache);
}

/* Clear nodes without deallocating buffer */
void bezier_path_clear(BezierPath *path) {
    if (!path) return;
    path->node_count = 0;
    path->span_count = 0;
    path->skullrot = -1;
    path_cache_invalidate(path);
}

/* Find node index by stable monotonic ID */
int bezier_path_find_node_by_id(const BezierPath *path, uint32_t id) {
    if (!path || id == 0) return -1;
    for (int i = 0; i < path->node_count; ++i) {
        if (path->nodes[i].id == id) return i;
    }
    return -1;
}

/* Add node to end of path */
bool bezier_path_add_node(BezierPath *path, const BezierNode *node) {
    if (!path || !node) return false;
    if (path->node_count >= path->node_capacity) {
        int new_cap = path->node_capacity == 0 ? 16 : path->node_capacity * 2;
        BezierNode *new_arr = (BezierNode*)realloc(path->nodes, new_cap * sizeof(BezierNode));
        if (!new_arr) return false;
        path->nodes = new_arr;
        path->node_capacity = new_cap;
    }
    BezierNode n = *node;
    if (n.id == 0) {
        n.id = path->next_node_id++;
    } else if (n.id >= path->next_node_id) {
        path->next_node_id = n.id + 1;
    }
    path->nodes[path->node_count++] = n;
    path_cache_invalidate(path);
    return true;
}

/* Insert node at specified index */
bool bezier_path_insert_node(BezierPath *path, int index, const BezierNode *node) {
    if (!path || !node || index < 0 || index > path->node_count) return false;
    if (path->node_count >= path->node_capacity) {
        int new_cap = path->node_capacity == 0 ? 16 : path->node_capacity * 2;
        BezierNode *new_arr = (BezierNode*)realloc(path->nodes, new_cap * sizeof(BezierNode));
        if (!new_arr) return false;
        path->nodes = new_arr;
        path->node_capacity = new_cap;
    }
    for (int i = path->node_count; i > index; --i) {
        path->nodes[i] = path->nodes[i - 1];
    }
    BezierNode n = *node;
    if (n.id == 0) {
        n.id = path->next_node_id++;
    } else if (n.id >= path->next_node_id) {
        path->next_node_id = n.id + 1;
    }
    path->nodes[index] = n;
    path->node_count++;
    path_cache_invalidate(path);
    return true;
}

/* Remove node at index with symmetrical re-anchoring of attribute spans */
bool bezier_path_remove_node(BezierPath *path, int index) {
    if (!path || index < 0 || index >= path->node_count) return false;

    /* Ensure cache is up-to-date to read node_s arc lengths before deletion */
    if (path->node_count >= 2) {
        path_cache_update(path);
    }

    uint32_t del_id = path->nodes[index].id;
    double del_s = (path->cache.node_s && index < path->cache.node_count) ? path->cache.node_s[index] : 0.0;

    /* Symmetrical Re-Anchoring: Spans referencing del_id are preserved */
    for (int s = 0; s < path->span_count; ++s) {
        CurveSpan *sp = &path->spans[s];
        if (sp->node_c_id == del_id) {
            if (index > 0) {
                uint32_t prev_id = path->nodes[index - 1].id;
                double prev_s = path->cache.node_s ? path->cache.node_s[index - 1] : 0.0;
                double seg_len = del_s - prev_s;
                sp->node_c_id = prev_id;
                sp->off_c += (float)seg_len;
            } else if (path->node_count > 1) {
                uint32_t next_id = path->nodes[1].id;
                double next_s = path->cache.node_s ? path->cache.node_s[1] : 0.0;
                double seg_len = next_s - del_s;
                sp->node_c_id = next_id;
                sp->off_c = (float)fmax(0.0, (double)sp->off_c - seg_len);
            }
        }
        if (sp->node_d_id == del_id) {
            if (index + 1 < path->node_count) {
                uint32_t next_id = path->nodes[index + 1].id;
                double next_s = path->cache.node_s ? path->cache.node_s[index + 1] : 0.0;
                double seg_len = next_s - del_s;
                sp->node_d_id = next_id;
                sp->off_d += (float)seg_len;
            } else if (index > 0) {
                uint32_t prev_id = path->nodes[index - 1].id;
                double prev_s = path->cache.node_s ? path->cache.node_s[index - 1] : 0.0;
                double seg_len = del_s - prev_s;
                sp->node_d_id = prev_id;
                sp->off_d = (float)fmax(0.0, (double)sp->off_d - seg_len);
            }
        }
    }

    for (int i = index; i < path->node_count - 1; ++i) {
        path->nodes[i] = path->nodes[i + 1];
    }
    path->node_count--;
    path_cache_invalidate(path);
    return true;
}

/* Split bezier segment seg_idx at parameter t using De Casteljau subdivision */
bool bezier_path_split_segment(BezierPath *path, int seg_idx, float t) {
    if (!path || seg_idx < 0 || seg_idx >= path->node_count - 1) return false;
    if (t <= 0.001f || t >= 0.999f) return false;

    BezierNode *n0 = &path->nodes[seg_idx];
    BezierNode *n1 = &path->nodes[seg_idx + 1];

    float p0x = n0->x;
    float p0y = n0->y;
    float p1x = n0->x + n0->hout_x;
    float p1y = n0->y + n0->hout_y;
    float p2x = n1->x + n1->hin_x;
    float p2y = n1->y + n1->hin_y;
    float p3x = n1->x;
    float p3y = n1->y;

    float it = 1.0f - t;
    float p01_x = it * p0x + t * p1x;
    float p01_y = it * p0y + t * p1y;
    float p12_x = it * p1x + t * p2x;
    float p12_y = it * p1y + t * p2y;
    float p23_x = it * p2x + t * p3x;
    float p23_y = it * p2y + t * p3y;

    float p012_x = it * p01_x + t * p12_x;
    float p012_y = it * p01_y + t * p12_y;
    float p123_x = it * p12_x + t * p23_x;
    float p123_y = it * p12_y + t * p23_y;

    float split_x = it * p012_x + t * p123_x;
    float split_y = it * p012_y + t * p123_y;

    /* Update n0 outgoing handle */
    n0->hout_x = p01_x - p0x;
    n0->hout_y = p01_y - p0y;

    /* New split node */
    BezierNode new_node;
    memset(&new_node, 0, sizeof(BezierNode));
    new_node.x = split_x;
    new_node.y = split_y;
    new_node.hin_x = p012_x - split_x;
    new_node.hin_y = p012_y - split_y;
    new_node.hout_x = p123_x - split_x;
    new_node.hout_y = p123_y - split_y;
    new_node.type = NODE_SMOOTH;

    /* Insert new node into path at index seg_idx + 1 */
    if (!bezier_path_insert_node(path, seg_idx + 1, &new_node)) {
        return false;
    }

    /* Update succeeding node incoming handle (now at seg_idx + 2) */
    path->nodes[seg_idx + 2].hin_x = p23_x - p3x;
    path->nodes[seg_idx + 2].hin_y = p23_y - p3y;

    path_cache_invalidate(path);
    return true;
}

/* Reverse track node order, swap handles, and transpose attribute spans */
bool bezier_path_reverse(BezierPath *path) {
    if (!path || path->node_count < 2) return false;

    path_cache_update(path);
    double total_L = path->cache.total_length;

    /* 1. Record existing spans before reversing */
    int num_spans = path->span_count;
    typedef struct {
        double s_c, s_d;
        uint8_t tun, prio;
        bool has_tun, has_prio;
    } TempSpan;
    TempSpan *saved = NULL;
    if (num_spans > 0 && path->cache.baked_spans) {
        saved = (TempSpan*)malloc(num_spans * sizeof(TempSpan));
        if (saved) {
            for (int k = 0; k < num_spans; ++k) {
                BakedSpan *b = &path->cache.baked_spans[k];
                saved[k].s_c = b->s_c;
                saved[k].s_d = b->s_d;
                saved[k].tun = b->tunnel;
                saved[k].prio = b->prio;
                saved[k].has_tun = b->has_tunnel;
                saved[k].has_prio = b->has_prio;
            }
        }
    }

    /* 2. Reverse nodes order: 0 <-> N-1 */
    int n = path->node_count;
    for (int i = 0; i < n / 2; ++i) {
        BezierNode tmp = path->nodes[i];
        path->nodes[i] = path->nodes[n - 1 - i];
        path->nodes[n - 1 - i] = tmp;
    }

    /* 3. For every node, swap hin and hout */
    for (int i = 0; i < n; ++i) {
        BezierNode *nd = &path->nodes[i];
        float tmp_x = nd->hin_x;
        float tmp_y = nd->hin_y;
        nd->hin_x = nd->hout_x;
        nd->hin_y = nd->hout_y;
        nd->hout_x = tmp_x;
        nd->hout_y = tmp_y;
    }

    /* 4. Invalidate and update cache to get new polyline & node_s */
    path_cache_invalidate(path);
    path_cache_update(path);

    /* 5. Re-map spans in reversed coordinates: s'_c = L - s_d, s'_d = L - s_c */
    path->span_count = 0;
    if (saved) {
        for (int k = 0; k < num_spans; ++k) {
            double rev_sc = total_L - saved[k].s_d;
            double rev_sd = total_L - saved[k].s_c;
            if (rev_sd > rev_sc + 0.5) {
                bezier_path_add_span_from_s(
                    path, rev_sc, rev_sd,
                    saved[k].has_tun, saved[k].tun,
                    saved[k].has_prio, saved[k].prio
                );
            }
        }
        free(saved);
    }

    path_cache_invalidate(path);
    path_cache_update(path);
    return true;
}

/* Split path at interior node into two separate continuous paths */
bool bezier_path_split_at_node(BezierPath *orig_path, int split_node_idx, BezierPath *out_new_path) {
    if (!orig_path || !out_new_path) return false;
    if (split_node_idx <= 0 || split_node_idx >= orig_path->node_count - 1) return false;

    path_cache_update(orig_path);
    double s_split = orig_path->cache.node_s[split_node_idx];

    /* 1. Record existing spans */
    int num_spans = orig_path->span_count;
    typedef struct {
        double s_c, s_d;
        uint8_t tun, prio;
        bool has_tun, has_prio;
    } TempSpan;
    TempSpan *saved = NULL;
    if (num_spans > 0 && orig_path->cache.baked_spans) {
        saved = (TempSpan*)malloc(num_spans * sizeof(TempSpan));
        if (saved) {
            for (int k = 0; k < num_spans; ++k) {
                BakedSpan *b = &orig_path->cache.baked_spans[k];
                saved[k].s_c = b->s_c;
                saved[k].s_d = b->s_d;
                saved[k].tun = b->tunnel;
                saved[k].prio = b->prio;
                saved[k].has_tun = b->has_tunnel;
                saved[k].has_prio = b->has_prio;
            }
        }
    }

    /* 2. Initialize new path */
    char new_name[64];
    snprintf(new_name, sizeof(new_name), "%.50s-split", orig_path->name[0] ? orig_path->name : "track");
    bezier_path_init(out_new_path, new_name);

    /* 3. Copy nodes from split_node_idx to end into out_new_path */
    int num_tail_nodes = orig_path->node_count - split_node_idx;
    for (int i = 0; i < num_tail_nodes; ++i) {
        BezierNode nd = orig_path->nodes[split_node_idx + i];
        if (i == 0) {
            nd.hin_x = 0.0f;
            nd.hin_y = 0.0f;
        }
        bezier_path_add_node(out_new_path, &nd);
    }

    /* 4. Truncate orig_path to split_node_idx */
    orig_path->node_count = split_node_idx + 1;
    orig_path->nodes[split_node_idx].hout_x = 0.0f;
    orig_path->nodes[split_node_idx].hout_y = 0.0f;

    /* 5. Invalidate and update caches */
    path_cache_invalidate(orig_path);
    path_cache_update(orig_path);
    path_cache_invalidate(out_new_path);
    path_cache_update(out_new_path);

    /* 6. Partition spans */
    orig_path->span_count = 0;
    out_new_path->span_count = 0;
    if (saved) {
        for (int k = 0; k < num_spans; ++k) {
            /* Portion on orig_path */
            if (saved[k].s_c < s_split) {
                double seg_sc = saved[k].s_c;
                double seg_sd = saved[k].s_d < s_split ? saved[k].s_d : s_split;
                if (seg_sd > seg_sc + 0.5) {
                    bezier_path_add_span_from_s(
                        orig_path, seg_sc, seg_sd,
                        saved[k].has_tun, saved[k].tun,
                        saved[k].has_prio, saved[k].prio
                    );
                }
            }
            /* Portion on out_new_path */
            if (saved[k].s_d > s_split) {
                double seg_sc = (saved[k].s_c > s_split ? saved[k].s_c : s_split) - s_split;
                double seg_sd = saved[k].s_d - s_split;
                if (seg_sd > seg_sc + 0.5) {
                    bezier_path_add_span_from_s(
                        out_new_path, seg_sc, seg_sd,
                        saved[k].has_tun, saved[k].tun,
                        saved[k].has_prio, saved[k].prio
                    );
                }
            }
        }
        free(saved);
    }

    path_cache_invalidate(orig_path);
    path_cache_update(orig_path);
    path_cache_invalidate(out_new_path);
    path_cache_update(out_new_path);
    return true;
}

/* Join two endpoints of paths, auto-aligning flow direction */
bool bezier_path_join_paths(BezierPath *path_a, int endpoint_a, BezierPath *path_b, int endpoint_b) {
    if (!path_a || path_a->node_count < 1) return false;

    /* Subcase 1: Closing loop on the same path */
    if (path_a == path_b) {
        int n = path_a->node_count;
        if ((endpoint_a == 0 && endpoint_b == n - 1) || (endpoint_a == n - 1 && endpoint_b == 0)) {
            path_a->is_closed = true;
            path_cache_invalidate(path_a);
            path_cache_update(path_a);
            return true;
        }
        return false;
    }

    if (!path_b || path_b->node_count < 1) return false;

    /* Subcase 2: Joining two different paths.
     * We want path_a to end at endpoint_a (so endpoint_a must be at index path_a->node_count - 1),
     * and path_b to start at endpoint_b (so endpoint_b must be at index 0).
     */
    if (endpoint_a == 0) {
        bezier_path_reverse(path_a);
    }
    if (endpoint_b != 0) {
        bezier_path_reverse(path_b);
    }

    path_cache_update(path_a);
    path_cache_update(path_b);
    double len_a = path_a->cache.total_length;

    /* Record spans of path_b */
    int num_spans_b = path_b->span_count;
    typedef struct {
        double s_c, s_d;
        uint8_t tun, prio;
        bool has_tun, has_prio;
    } TempSpan;
    TempSpan *saved_b = NULL;
    if (num_spans_b > 0 && path_b->cache.baked_spans) {
        saved_b = (TempSpan*)malloc(num_spans_b * sizeof(TempSpan));
        if (saved_b) {
            for (int k = 0; k < num_spans_b; ++k) {
                BakedSpan *b = &path_b->cache.baked_spans[k];
                saved_b[k].s_c = b->s_c;
                saved_b[k].s_d = b->s_d;
                saved_b[k].tun = b->tunnel;
                saved_b[k].prio = b->prio;
                saved_b[k].has_tun = b->has_tunnel;
                saved_b[k].has_prio = b->has_prio;
            }
        }
    }

    /* Merge or append nodes of path_b into path_a */
    BezierNode *last_a = &path_a->nodes[path_a->node_count - 1];
    BezierNode *first_b = &path_b->nodes[0];
    float dist = hypotf(last_a->x - first_b->x, last_a->y - first_b->y);

    int start_copy_idx = 0;
    if (dist <= 2.0f) {
        /* Merge overlapping endpoint */
        last_a->hout_x = first_b->hout_x;
        last_a->hout_y = first_b->hout_y;
        start_copy_idx = 1;
    }

    for (int i = start_copy_idx; i < path_b->node_count; ++i) {
        bezier_path_add_node(path_a, &path_b->nodes[i]);
    }

    path_cache_invalidate(path_a);
    path_cache_update(path_a);

    /* Append spans of path_b shifted by len_a */
    if (saved_b) {
        for (int k = 0; k < num_spans_b; ++k) {
            double shifted_sc = saved_b[k].s_c + len_a;
            double shifted_sd = saved_b[k].s_d + len_a;
            bezier_path_add_span_from_s(
                path_a, shifted_sc, shifted_sd,
                saved_b[k].has_tun, saved_b[k].tun,
                saved_b[k].has_prio, saved_b[k].prio
            );
        }
        free(saved_b);
    }

    /* Clear path_b */
    bezier_path_clear(path_b);
    return true;
}

/* Span Management Operations */
bool bezier_path_add_span(BezierPath *path, const CurveSpan *span) {
    if (!path || !span) return false;
    if (path->span_count >= path->span_capacity) {
        int new_cap = path->span_capacity == 0 ? 8 : path->span_capacity * 2;
        CurveSpan *new_arr = (CurveSpan*)realloc(path->spans, new_cap * sizeof(CurveSpan));
        if (!new_arr) return false;
        path->spans = new_arr;
        path->span_capacity = new_cap;
    }
    path->spans[path->span_count++] = *span;
    path_cache_invalidate(path);
    return true;
}

bool bezier_path_remove_span(BezierPath *path, int index) {
    if (!path || index < 0 || index >= path->span_count) return false;
    for (int i = index; i < path->span_count - 1; ++i) {
        path->spans[i] = path->spans[i + 1];
    }
    path->span_count--;
    path_cache_invalidate(path);
    return true;
}

void bezier_path_clear_spans(BezierPath *path) {
    if (!path) return;
    path->span_count = 0;
    path_cache_invalidate(path);
}

/* Permanently prune spans that have collapsed (w <= 0.0) or whose anchors were deleted */
void bezier_path_prune_collapsed_spans(BezierPath *path) {
    if (!path || path->span_count == 0) return;
    if (path->node_count < 2) {
        path->span_count = 0;
        path_cache_invalidate(path);
        return;
    }
    path_cache_update(path);

    int write_idx = 0;
    for (int i = 0; i < path->span_count; ++i) {
        bool keep = false;
        if (i < path->cache.baked_span_count) {
            keep = path->cache.baked_spans[i].active;
        }
        if (keep) {
            if (write_idx != i) {
                path->spans[write_idx] = path->spans[i];
            }
            write_idx++;
        }
    }
    if (write_idx != path->span_count) {
        path->span_count = write_idx;
        path_cache_invalidate(path);
        path_cache_update(path);
    }
}

/* Add attribute span by arc-length interval [s_start, s_end] with anchor referencing */
bool bezier_path_add_span_from_s(
    BezierPath *path,
    double s_start,
    double s_end,
    bool has_tunnel,
    uint8_t in_tunnel,
    bool has_prio,
    uint8_t priority
) {
    if (!path || path->node_count < 2) return false;
    if (!has_tunnel && !has_prio) return false;

    path_cache_update(path);
    if (!path->cache.node_s || path->cache.node_count < path->node_count) return false;

    if (s_start > s_end) {
        double tmp = s_start; s_start = s_end; s_end = tmp;
    }
    if (s_end - s_start < 1.0) return false;

    s_start = fmax(0.0, s_start);
    s_end = fmin(path->cache.total_length, s_end);
    if (s_end - s_start < 1.0) return false;

    /* Find start anchor: nearest anchor <= s_start */
    int ic = 0;
    for (int k = 0; k < path->node_count - 1; ++k) {
        if (path->cache.node_s[k] <= s_start + 1.0) ic = k;
        else break;
    }
    double off_c = s_start - path->cache.node_s[ic];
    if (fabs(off_c) <= 1.0) off_c = 0.0;
    if (off_c < 0.0) off_c = 0.0;

    /* Find end anchor: nearest anchor >= s_end, strictly > ic */
    int id = ic + 1;
    for (int k = path->node_count - 1; k > ic; --k) {
        if (path->cache.node_s[k] >= s_end - 1.0) id = k;
        else break;
    }
    double off_d = path->cache.node_s[id] - s_end;
    if (fabs(off_d) <= 1.0) off_d = 0.0;
    if (off_d < 0.0) off_d = 0.0;

    CurveSpan span;
    memset(&span, 0, sizeof(CurveSpan));
    span.node_c_id = path->nodes[ic].id;
    span.off_c = (float)off_c;
    span.node_d_id = path->nodes[id].id;
    span.off_d = (float)off_d;
    span.in_tunnel = in_tunnel;
    span.priority = priority;
    span.has_tunnel = has_tunnel;
    span.has_prio = has_prio;

    return bezier_path_add_span(path, &span);
}

/* Maintain handle symmetry & collinearity constraint */
void bezier_node_update_handles(BezierNode *node, bool out_was_dragged) {
    if (!node) return;
    if (node->type == NODE_CORNER || node->type == NODE_CUSP) {
        return;
    }

    if (node->type == NODE_SYMMETRIC) {
        if (out_was_dragged) {
            node->hin_x = -node->hout_x;
            node->hin_y = -node->hout_y;
        } else {
            node->hout_x = -node->hin_x;
            node->hout_y = -node->hin_y;
        }
    } else if (node->type == NODE_SMOOTH) {
        if (out_was_dragged) {
            float len_out = hypotf(node->hout_x, node->hout_y);
            float len_in  = hypotf(node->hin_x,  node->hin_y);
            if (len_out > 1e-6f && len_in > 1e-6f) {
                node->hin_x = -(node->hout_x / len_out) * len_in;
                node->hin_y = -(node->hout_y / len_out) * len_in;
            } else if (len_out > 1e-6f) {
                node->hin_x = -node->hout_x;
                node->hin_y = -node->hout_y;
            }
        } else {
            float len_in  = hypotf(node->hin_x,  node->hin_y);
            float len_out = hypotf(node->hout_x, node->hout_y);
            if (len_in > 1e-6f && len_out > 1e-6f) {
                node->hout_x = -(node->hin_x / len_in) * len_out;
                node->hout_y = -(node->hin_y / len_in) * len_out;
            } else if (len_in > 1e-6f) {
                node->hout_x = -node->hin_x;
                node->hout_y = -node->hin_y;
            }
        }
    }
}

/* Convert node to sharp corner (retract handles to zero) */
void bezier_node_make_corner(BezierNode *node) {
    if (!node) return;
    node->hin_x = 0.0f;
    node->hin_y = 0.0f;
    node->hout_x = 0.0f;
    node->hout_y = 0.0f;
    node->type = NODE_CORNER;
}

/* Convert node to sharp corner (retract handles) and invalidate cache */
void bezier_path_make_corner_node(BezierPath *path, int index) {
    if (!path || index < 0 || index >= path->node_count) return;
    bezier_node_make_corner(&path->nodes[index]);
    path_cache_invalidate(path);
}

/* Convert node to smooth with automatic collinear handles derived from adjacent neighbors */
void bezier_path_smooth_node(BezierPath *path, int index) {
    if (!path || index < 0 || index >= path->node_count) return;
    BezierNode *node = &path->nodes[index];
    if (path->node_count < 2) {
        bezier_node_make_corner(node);
        path_cache_invalidate(path);
        return;
    }

    float tx = 0.0f, ty = 0.0f;
    if (path->is_closed) {
        int prev_idx = (index - 1 + path->node_count) % path->node_count;
        int next_idx = (index + 1) % path->node_count;
        tx = (path->nodes[next_idx].x - path->nodes[prev_idx].x) / 6.0f;
        ty = (path->nodes[next_idx].y - path->nodes[prev_idx].y) / 6.0f;
        node->hout_x = tx;
        node->hout_y = ty;
        node->hin_x  = -tx;
        node->hin_y  = -ty;
        node->type   = NODE_SMOOTH;
    } else {
        if (index == 0) {
            tx = (path->nodes[1].x - node->x) / 6.0f;
            ty = (path->nodes[1].y - node->y) / 6.0f;
            node->hout_x = tx;
            node->hout_y = ty;
            node->hin_x  = 0.0f;
            node->hin_y  = 0.0f;
            node->type   = NODE_SMOOTH;
        } else if (index == path->node_count - 1) {
            tx = (node->x - path->nodes[index - 1].x) / 6.0f;
            ty = (node->y - path->nodes[index - 1].y) / 6.0f;
            node->hin_x  = -tx;
            node->hin_y  = -ty;
            node->hout_x = 0.0f;
            node->hout_y = 0.0f;
            node->type   = NODE_SMOOTH;
        } else {
            tx = (path->nodes[index + 1].x - path->nodes[index - 1].x) / 6.0f;
            ty = (path->nodes[index + 1].y - path->nodes[index - 1].y) / 6.0f;
            node->hout_x = tx;
            node->hout_y = ty;
            node->hin_x  = -tx;
            node->hin_y  = -ty;
            node->type   = NODE_SMOOTH;
        }
    }
    path_cache_invalidate(path);
}

/* Convert 4 Catmull-Rom waypoints to Cubic Bezier control handles */
void catmull_rom_to_bezier(
    float p0x, float p0y,
    float p1x, float p1y,
    float p2x, float p2y,
    float p3x, float p3y,
    float alpha,
    float *out_b1x, float *out_b1y,
    float *out_b2x, float *out_b2y
) {
    if (alpha <= 1e-4f) {
        /* Standard Uniform Catmull-Rom (alpha = 0.0) -> exact 1/6 tangent */
        *out_b1x = p1x + (p2x - p0x) / 6.0f;
        *out_b1y = p1y + (p2y - p0y) / 6.0f;
        *out_b2x = p2x - (p3x - p1x) / 6.0f;
        *out_b2y = p2y - (p3y - p1y) / 6.0f;
        return;
    }

    /* Parameterized Catmull-Rom (Centripetal alpha=0.5, Chordal alpha=1.0)
       Using numerically stable difference formulation to avoid catastrophic cancellation:
       B1 = P1 + [a1^2*(P2 - P1) + a2^2*(P1 - P0)] / [3*a1*(a1 + a2)]
       B2 = P2 - [a3^2*(P2 - P1) + a2^2*(P3 - P2)] / [3*a3*(a3 + a2)] */
    double d1 = hypot((double)(p1x - p0x), (double)(p1y - p0y));
    double d2 = hypot((double)(p2x - p1x), (double)(p2y - p1y));
    double d3 = hypot((double)(p3x - p2x), (double)(p3y - p2y));

    if (d1 < 1e-6) d1 = 1e-6;
    if (d2 < 1e-6) d2 = 1e-6;
    if (d3 < 1e-6) d3 = 1e-6;

    double a1 = pow(d1, (double)alpha);
    double a2 = pow(d2, (double)alpha);
    double a3 = pow(d3, (double)alpha);

    double k1 = 3.0 * a1 * (a1 + a2);
    double k2 = 3.0 * a3 * (a3 + a2);

    *out_b1x = (float)((double)p1x + (a1 * a1 * (double)(p2x - p1x) + a2 * a2 * (double)(p1x - p0x)) / k1);
    *out_b1y = (float)((double)p1y + (a1 * a1 * (double)(p2y - p1y) + a2 * a2 * (double)(p1y - p0y)) / k1);
    *out_b2x = (float)((double)p2x - (a3 * a3 * (double)(p2x - p1x) + a2 * a2 * (double)(p3x - p2x)) / k2);
    *out_b2y = (float)((double)p2y - (a3 * a3 * (double)(p2y - p1y) + a2 * a2 * (double)(p3y - p2y)) / k2);
}

/* Internal polyline dynamic buffer */
typedef struct {
    float *x;
    float *y;
    int count;
    int capacity;
} PolylineBuffer;

static void polyline_init(PolylineBuffer *buf) {
    buf->x = NULL;
    buf->y = NULL;
    buf->count = 0;
    buf->capacity = 0;
}

static void polyline_free(PolylineBuffer *buf) {
    if (buf->x) free(buf->x);
    if (buf->y) free(buf->y);
    buf->count = 0;
    buf->capacity = 0;
}

static void polyline_append(PolylineBuffer *buf, float x, float y) {
    if (buf->count >= buf->capacity) {
        int new_cap = buf->capacity == 0 ? 256 : buf->capacity * 2;
        float *nx = (float*)realloc(buf->x, new_cap * sizeof(float));
        float *ny = (float*)realloc(buf->y, new_cap * sizeof(float));
        if (!nx || !ny) {
            if (nx) buf->x = nx;
            if (ny) buf->y = ny;
            return;
        }
        buf->x = nx;
        buf->y = ny;
        buf->capacity = new_cap;
    }
    buf->x[buf->count] = x;
    buf->y[buf->count] = y;
    buf->count++;
}

/* De Casteljau adaptive subdivision with loop degenerate-chord guard */
static void subdivide_cubic_bezier(
    float p0x, float p0y,
    float p1x, float p1y,
    float p2x, float p2y,
    float p3x, float p3y,
    float tol_sq, int depth,
    PolylineBuffer *buf
) {
    if (depth > 12) {
        polyline_append(buf, p3x, p3y);
        return;
    }

    float ux = p3x - p0x;
    float uy = p3y - p0y;
    float chord_len_sq = ux * ux + uy * uy;
    float d1_sq, d2_sq;

    if (chord_len_sq <= 1e-4f) {
        d1_sq = (p1x - p0x) * (p1x - p0x) + (p1y - p0y) * (p1y - p0y);
        d2_sq = (p2x - p0x) * (p2x - p0x) + (p2y - p0y) * (p2y - p0y);
    } else {
        float c1 = (p1x - p0x) * uy - (p1y - p0y) * ux;
        float c2 = (p2x - p0x) * uy - (p2y - p0y) * ux;
        d1_sq = (c1 * c1) / chord_len_sq;
        d2_sq = (c2 * c2) / chord_len_sq;
    }

    if (d1_sq <= tol_sq && d2_sq <= tol_sq) {
        polyline_append(buf, p3x, p3y);
        return;
    }

    float l0x = p0x, l0y = p0y;
    float l1x = (p0x + p1x) * 0.5f, l1y = (p0y + p1y) * 0.5f;
    float m_x = (p1x + p2x) * 0.5f, m_y = (p1y + p2y) * 0.5f;
    float r2x = (p2x + p3x) * 0.5f, r2y = (p2y + p3y) * 0.5f;
    float r3x = p3x, r3y = p3y;

    float l2x = (l1x + m_x) * 0.5f, l2y = (l1y + m_y) * 0.5f;
    float r1x = (m_x + r2x) * 0.5f, r1y = (m_y + r2y) * 0.5f;
    float l3x = (l2x + r1x) * 0.5f, l3y = (l2y + r1y) * 0.5f;

    subdivide_cubic_bezier(l0x, l0y, l1x, l1y, l2x, l2y, l3x, l3y, tol_sq, depth + 1, buf);
    subdivide_cubic_bezier(l3x, l3y, r1x, r1y, r2x, r2y, r3x, r3y, tol_sq, depth + 1, buf);
}

/* Instantaneous Attribute Query: Half-open interval [s_c, s_d) with Painter's Algorithm */
static inline void attrs_at(const BakedSpan *b, int n, double s, uint8_t *out_tunnel, uint8_t *out_prio) {
    *out_tunnel = 0;
    *out_prio = 0; /* Default baseline: tunnel=0 (hittable), priority=0 (surface layer) */
    for (int k = 0; k < n; ++k) {
        if (b[k].active && s >= (b[k].s_c - 1e-4) && s <= (b[k].s_d + 1e-4)) {
            if (b[k].has_tunnel) *out_tunnel = b[k].tunnel;
            if (b[k].has_prio)   *out_prio   = b[k].prio;
        }
    }
}

/* Public API to evaluate attributes directly from baked cache */
void bezier_path_cache_attrs_at(const PathCache *cache, double s, uint8_t *out_tunnel, uint8_t *out_priority) {
    uint8_t t = 0, p = 0;
    if (cache && cache->baked_spans) {
        attrs_at(cache->baked_spans, cache->baked_span_count, s, &t, &p);
    }
    if (out_tunnel) *out_tunnel = t;
    if (out_priority) *out_priority = p;
}

/* Public API for attribute evaluation (ensures cache is updated if dirty) */
void bezier_path_attrs_at(BezierPath *path, double s, uint8_t *out_tunnel, uint8_t *out_priority) {
    if (!path) {
        if (out_tunnel) *out_tunnel = 0;
        if (out_priority) *out_priority = 0;
        return;
    }
    if (path->cache.is_dirty) {
        path_cache_update(path);
    }
    bezier_path_cache_attrs_at(&path->cache, s, out_tunnel, out_priority);
}

/* Locality window projection for Variable Mode 1D dragging */
double project_s_window(
    BezierPath *path,
    float px, float py,
    double s_prev,
    double window_px,
    double *out_dist_sq
) {
    if (!path) return 0.0;
    if (path->cache.is_dirty || path->cache.count < 2) {
        if (!path_cache_update(path)) return 0.0;
    }

    const PathCache *c = &path->cache;
    int n = c->count;
    if (n < 2) return 0.0;

    int i_start = 0;
    int i_end = n - 2;

    bool use_window = (s_prev >= 0.0 && window_px > 0.0);
    if (use_window) {
        double s_min = s_prev - window_px;
        double s_max = s_prev + window_px;
        int low = 0, high = n - 1;
        while (low < high) {
            int mid = (low + high) / 2;
            if (c->S[mid] < s_min) low = mid + 1;
            else high = mid;
        }
        i_start = (low > 0) ? low - 1 : 0;

        low = 0; high = n - 1;
        while (low < high) {
            int mid = (low + high + 1) / 2;
            if (c->S[mid] > s_max) high = mid - 1;
            else low = mid;
        }
        i_end = (low < n - 2) ? low : n - 2;
    }

    double best_s = 0.0;
    double best_d2 = 1e18;

    for (int i = i_start; i <= i_end; ++i) {
        double x0 = c->x[i],     y0 = c->y[i];
        double x1 = c->x[i + 1], y1 = c->y[i + 1];
        double vx = x1 - x0,     vy = y1 - y0;
        double seg_len_sq = vx * vx + vy * vy;
        double s0 = c->S[i];
        double s1 = c->S[i + 1];

        double wx = (double)px - x0;
        double wy = (double)py - y0;
        double t = (seg_len_sq > 1e-12) ? ((wx * vx + wy * vy) / seg_len_sq) : 0.0;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;

        double proj_x = x0 + t * vx;
        double proj_y = y0 + t * vy;
        double d2 = ((double)px - proj_x) * ((double)px - proj_x) +
                    ((double)py - proj_y) * ((double)py - proj_y);

        if (d2 < best_d2 - 1e-6) {
            best_d2 = d2;
            best_s = s0 + t * (s1 - s0);
        } else if (fabs(d2 - best_d2) <= 1e-6) {
            double cand_s = s0 + t * (s1 - s0);
            if (cand_s < best_s) {
                best_s = cand_s;
            }
        }
    }

    /* Fallback to global search if window search diverged beyond 40 px */
    if (use_window && best_d2 > 40.0 * 40.0) {
        return project_s_window(path, px, py, -1.0, 0.0, out_dist_sq);
    }

    if (out_dist_sq) *out_dist_sq = best_d2;
    return round(best_s);
}

/* Update PathCache polyline, cumulative chord S, and node_s[] at 0.05 px tolerance */
bool path_cache_update(BezierPath *path) {
    if (!path) return false;
    if (!path->cache.is_dirty && path->cache.count >= 2) return true;
    if (path->node_count < 2) {
        path->cache.count = 0;
        path->cache.total_length = 0.0;
        path->cache.baked_span_count = 0;
        path->cache.is_dirty = false;
        return false;
    }

    /* Ensure node IDs are assigned */
    for (int i = 0; i < path->node_count; ++i) {
        if (path->nodes[i].id == 0) {
            path->nodes[i].id = path->next_node_id++;
        }
    }

    PolylineBuffer poly;
    polyline_init(&poly);

    int *node_v_idx = (int*)malloc(path->node_count * sizeof(int));
    if (!node_v_idx) {
        polyline_free(&poly);
        return false;
    }

    /* Append first anchor */
    const BezierNode *first_node = &path->nodes[0];
    polyline_append(&poly, first_node->x, first_node->y);
    node_v_idx[0] = 0;

    float tol_sq = 0.05f * 0.05f;
    int num_segs = path->is_closed ? path->node_count : path->node_count - 1;
    for (int i = 0; i < num_segs; ++i) {
        const BezierNode *n0 = &path->nodes[i];
        const BezierNode *n1 = (i + 1 < path->node_count) ? &path->nodes[i + 1] : &path->nodes[0];

        float p0x = n0->x;
        float p0y = n0->y;
        float p1x = n0->x + n0->hout_x;
        float p1y = n0->y + n0->hout_y;
        float p2x = n1->x + n1->hin_x;
        float p2y = n1->y + n1->hin_y;
        float p3x = n1->x;
        float p3y = n1->y;

        subdivide_cubic_bezier(
            p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y,
            tol_sq, 0, &poly
        );

        if (i + 1 < path->node_count) {
            node_v_idx[i + 1] = poly.count - 1;
        }
    }

    if (poly.count < 2) {
        free(node_v_idx);
        polyline_free(&poly);
        return false;
    }

    PathCache *c = &path->cache;
    if (c->capacity < poly.count) {
        free(c->x);
        free(c->y);
        free(c->S);
        c->x = (float*)malloc(poly.count * sizeof(float));
        c->y = (float*)malloc(poly.count * sizeof(float));
        c->S = (double*)malloc(poly.count * sizeof(double));
        c->capacity = poly.count;
    }

    if (!c->x || !c->y || !c->S) {
        free(node_v_idx);
        polyline_free(&poly);
        return false;
    }

    c->count = poly.count;
    memcpy(c->x, poly.x, poly.count * sizeof(float));
    memcpy(c->y, poly.y, poly.count * sizeof(float));

    /* Calculate cumulative chord length S */
    c->S[0] = 0.0;
    for (int i = 1; i < poly.count; ++i) {
        double dx = (double)(poly.x[i] - poly.x[i - 1]);
        double dy = (double)(poly.y[i] - poly.y[i - 1]);
        c->S[i] = c->S[i - 1] + hypot(dx, dy);
    }
    c->total_length = c->S[poly.count - 1];

    /* Update node_s positions */
    if (c->node_count != path->node_count) {
        free(c->node_s);
        c->node_s = (double*)malloc(path->node_count * sizeof(double));
        c->node_count = path->node_count;
    }
    if (c->node_s) {
        for (int i = 0; i < path->node_count; ++i) {
            c->node_s[i] = c->S[node_v_idx[i]];
        }
    }

    /* Bake Spans */
    if (c->baked_span_capacity < path->span_count) {
        free(c->baked_spans);
        int bcap = path->span_count > 0 ? path->span_count : 1;
        c->baked_spans = (BakedSpan*)malloc(bcap * sizeof(BakedSpan));
        c->baked_span_capacity = bcap;
    }
    c->baked_span_count = path->span_count;

    for (int k = 0; k < path->span_count; ++k) {
        const CurveSpan *sp = &path->spans[k];
        BakedSpan *b = &c->baked_spans[k];
        int ic = bezier_path_find_node_by_id(path, sp->node_c_id);
        int id = bezier_path_find_node_by_id(path, sp->node_d_id);
        if (ic >= 0 && id >= 0 && c->node_s) {
            double base_sc = c->node_s[ic];
            double base_sd = c->node_s[id];
            double sc = base_sc + (double)sp->off_c;
            double sd = base_sd - (double)sp->off_d;
            double w = sd - sc;
            if (w <= 0.0) {
                b->active = false; /* Non-destructive temporary collapse */
            } else {
                b->active = true;
                b->s_c = sc;
                b->s_d = sd;
                b->tunnel = sp->in_tunnel;
                b->prio = sp->priority;
                b->has_tunnel = sp->has_tunnel;
                b->has_prio = sp->has_prio;
            }
        } else {
            b->active = false;
        }
    }

    free(node_v_idx);
    polyline_free(&poly);
    c->is_dirty = false;
    return true;
}

/* Resample Bezier curve at uniform arc-length distance (strictly step_size px) */
ResampledPathPoint* bezier_path_resample(
    BezierPath *path,
    double step_size,
    double flatten_tolerance,
    int *out_point_count
) {
    (void)flatten_tolerance;
    if (out_point_count) *out_point_count = 0;
    if (!path || path->node_count < 2) return NULL;
    if (step_size <= 0.001) step_size = 1.0;

    if (path->cache.is_dirty || path->cache.count < 2) {
        if (!path_cache_update(path)) return NULL;
    }

    PathCache *c = &path->cache;
    double total_length = c->total_length;
    int est_count = (int)floor(total_length / step_size) + 2;
    ResampledPathPoint *resampled = (ResampledPathPoint*)malloc(est_count * sizeof(ResampledPathPoint));
    if (!resampled) return NULL;

    int res_count = 0;
    int cur_poly_idx = 0;
    double cur_target_s = 0.0;

    while (cur_target_s <= total_length) {
        while (cur_poly_idx < c->count - 1 && c->S[cur_poly_idx + 1] < cur_target_s) {
            cur_poly_idx++;
        }

        if (cur_poly_idx >= c->count - 1) {
            break;
        }

        double s0 = c->S[cur_poly_idx];
        double s1 = c->S[cur_poly_idx + 1];
        double seg_len = s1 - s0;
        double t = (seg_len > 1e-9) ? ((cur_target_s - s0) / seg_len) : 0.0;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;

        float rx = (float)((1.0 - t) * (double)c->x[cur_poly_idx] + t * (double)c->x[cur_poly_idx + 1]);
        float ry = (float)((1.0 - t) * (double)c->y[cur_poly_idx] + t * (double)c->y[cur_poly_idx + 1]);

        uint8_t tun = 0, prio = 0;
        attrs_at(c->baked_spans, c->baked_span_count, cur_target_s, &tun, &prio);

        resampled[res_count].x = rx;
        resampled[res_count].y = ry;
        resampled[res_count].in_tunnel = (tun != 0);
        resampled[res_count].priority = prio;
        res_count++;

        cur_target_s += step_size;
    }

    /* Guarantee endpoint is included without jump */
    if (res_count > 0) {
        int last_idx = c->count - 1;
        float last_x = c->x[last_idx];
        float last_y = c->y[last_idx];
        double end_dist = hypot((double)(last_x - resampled[res_count - 1].x),
                                (double)(last_y - resampled[res_count - 1].y));
        if (end_dist > 0.05) {
            uint8_t tun = 0, prio = 0;
            attrs_at(c->baked_spans, c->baked_span_count, total_length - 1e-6, &tun, &prio);

            resampled[res_count].x = last_x;
            resampled[res_count].y = last_y;
            resampled[res_count].in_tunnel = (tun != 0);
            resampled[res_count].priority = prio;
            res_count++;
        }
    }

    if (out_point_count) *out_point_count = res_count;
    return resampled;
}

/* Export path to official Zuma Deluxe CURV (.dat) binary file atomically */
bool bezier_path_export_dat(BezierPath *path, const char *out_filepath) {
    if (!path || path->node_count < 2 || !out_filepath) return false;

    /* Prune any permanently collapsed spans before export */
    bezier_path_prune_collapsed_spans(path);

    int res_count = 0;
    ResampledPathPoint *pts = bezier_path_resample(path, 1.0, 0.05, &res_count);
    if (!pts || res_count < 2) {
        if (pts) free(pts);
        return false;
    }

    char tmp_filepath[1024];
    snprintf(tmp_filepath, sizeof(tmp_filepath), "%s.tmp", out_filepath);

    FILE *f = fopen(tmp_filepath, "wb");
    if (!f) {
        free(pts);
        return false;
    }

    /* 1. Header */
    fwrite("CURV", 1, 4, f);
    uint32_t version = 2;
    fwrite(&version, 4, 1, f);
    uint32_t edit_type = 1;
    fwrite(&edit_type, 4, 1, f);

    /* 2. Control Points Payload: 4 bytes count + (count * 10 bytes) */
    uint32_t cp_count = (uint32_t)path->node_count;
    uint32_t edit_buf_size = 4 + (cp_count * 10);
    fwrite(&edit_buf_size, 4, 1, f);
    fwrite(&cp_count, 4, 1, f);

    for (int i = 0; i < path->node_count; ++i) {
        int32_t cx = (int32_t)roundf(path->nodes[i].x);
        int32_t cy = (int32_t)roundf(path->nodes[i].y);
        uint8_t c_tunnel = 0, c_prio = 1;
        if (path->cache.node_s) {
            attrs_at(path->cache.baked_spans, path->cache.baked_span_count, path->cache.node_s[i] + 1e-6, &c_tunnel, &c_prio);
        }
        fwrite(&cx, 4, 1, f);
        fwrite(&cy, 4, 1, f);
        fwrite(&c_tunnel, 1, 1, f);
        fwrite(&c_prio, 1, 1, f);
    }

    /* 3. Runtime Discrete Trajectory Points */
    uint32_t total_points = (uint32_t)res_count;
    fwrite(&total_points, 4, 1, f);

    /* Anchor point (Point #0) */
    float first_x = pts[0].x;
    float first_y = pts[0].y;
    uint8_t first_tunnel = pts[0].in_tunnel ? 1 : 0;
    uint8_t first_prio = pts[0].priority;
    fwrite(&first_x, 4, 1, f);
    fwrite(&first_y, 4, 1, f);
    fwrite(&first_tunnel, 1, 1, f);
    fwrite(&first_prio, 1, 1, f);

    /* Subsequent points with error-feedback drift prevention */
    double dec_x = (double)first_x;
    double dec_y = (double)first_y;

    for (int i = 1; i < res_count; ++i) {
        double ideal_x = (double)pts[i].x;
        double ideal_y = (double)pts[i].y;

        int idx = (int)round((ideal_x - dec_x) * 100.0);
        int idy = (int)round((ideal_y - dec_y) * 100.0);

        if (idx < -128) idx = -128;
        if (idx > 127)  idx = 127;
        if (idy < -128) idy = -128;
        if (idy > 127)  idy = 127;

        int8_t dx = (int8_t)idx;
        int8_t dy = (int8_t)idy;

        dec_x += (double)dx * 0.01;
        dec_y += (double)dy * 0.01;

        uint8_t pt_tunnel = pts[i].in_tunnel ? 1 : 0;
        uint8_t pt_prio = pts[i].priority;

        fwrite(&dx, 1, 1, f);
        fwrite(&dy, 1, 1, f);
        fwrite(&pt_tunnel, 1, 1, f);
        fwrite(&pt_prio, 1, 1, f);
    }

    free(pts);

    if (ferror(f) || fclose(f) != 0) {
        remove(tmp_filepath);
        return false;
    }

#ifdef _WIN32
    if (!MoveFileExA(tmp_filepath, out_filepath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        remove(tmp_filepath);
        return false;
    }
#else
    remove(out_filepath);
    rename(tmp_filepath, out_filepath);
#endif

    return true;
}

const char* dat_import_error_to_string(DatImportError err) {
    switch (err) {
        case DAT_ERR_OK: return "Success";
        case DAT_ERR_INVALID_PARAM: return "Invalid input arguments.";
        case DAT_ERR_FILE_NOT_FOUND: return "Could not open file (file not found or access denied).";
        case DAT_ERR_INVALID_MAGIC: return "Invalid binary signature (expected 'CURV'). Not a Zuma Deluxe path file.";
        case DAT_ERR_UNSUPPORTED_VERSION: return "Unsupported CURV format version (expected version 1 or 2).";
        case DAT_ERR_INVALID_EDIT_TYPE: return "Unsupported CURV edit type (expected type 1).";
        case DAT_ERR_INVALID_CP_COUNT: return "Corrupted curve header: invalid control point count or buffer size mismatch.";
        case DAT_ERR_READ_CP_FAILED: return "Unexpected end of file while reading control points.";
        case DAT_ERR_ALLOC_FAILED: return "Memory allocation failure while importing curve.";
        default: return "Unknown Zuma path import error.";
    }
}

/* Import official Zuma Deluxe CURV (.dat) binary file into BezierPath with detailed error reporting */
bool bezier_path_import_dat_ex(BezierPath *path, const char *in_filepath, float alpha, DatImportError *out_error) {
    if (out_error) *out_error = DAT_ERR_OK;
    if (!path || !in_filepath) {
        if (out_error) *out_error = DAT_ERR_INVALID_PARAM;
        return false;
    }
    FILE *f = fopen(in_filepath, "rb");
    if (!f) {
        if (out_error) *out_error = DAT_ERR_FILE_NOT_FOUND;
        return false;
    }

    char magic[5] = {0};
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "CURV", 4) != 0) {
        fclose(f);
        if (out_error) *out_error = DAT_ERR_INVALID_MAGIC;
        return false;
    }

    uint32_t version = 0, edit_type = 0, edit_size = 0;
    if (fread(&version, 4, 1, f) != 1 ||
        fread(&edit_type, 4, 1, f) != 1 ||
        fread(&edit_size, 4, 1, f) != 1) {
        fclose(f);
        if (out_error) *out_error = DAT_ERR_READ_CP_FAILED;
        return false;
    }

    if (version != 1 && version != 2) {
        fclose(f);
        if (out_error) *out_error = DAT_ERR_UNSUPPORTED_VERSION;
        return false;
    }

    if (edit_type != 1) {
        fclose(f);
        if (out_error) *out_error = DAT_ERR_INVALID_EDIT_TYPE;
        return false;
    }

    uint32_t cp_count = 0;
    if (edit_size < 4 || fread(&cp_count, 4, 1, f) != 1) {
        fclose(f);
        if (out_error) *out_error = DAT_ERR_READ_CP_FAILED;
        return false;
    }

    /* Validate header format & control point buffer size */
    if (cp_count < 2 || edit_size != 4 + (cp_count * 10)) {
        fclose(f);
        if (out_error) *out_error = DAT_ERR_INVALID_CP_COUNT;
        return false;
    }

    /* Read raw control points */
    int32_t *raw_x = (int32_t*)malloc(cp_count * sizeof(int32_t));
    int32_t *raw_y = (int32_t*)malloc(cp_count * sizeof(int32_t));
    uint8_t *raw_t = (uint8_t*)malloc(cp_count * sizeof(uint8_t));
    uint8_t *raw_p = (uint8_t*)malloc(cp_count * sizeof(uint8_t));

    if (!raw_x || !raw_y || !raw_t || !raw_p) {
        if (raw_x) free(raw_x);
        if (raw_y) free(raw_y);
        if (raw_t) free(raw_t);
        if (raw_p) free(raw_p);
        fclose(f);
        if (out_error) *out_error = DAT_ERR_ALLOC_FAILED;
        return false;
    }

    for (uint32_t i = 0; i < cp_count; ++i) {
        if (fread(&raw_x[i], 4, 1, f) != 1 ||
            fread(&raw_y[i], 4, 1, f) != 1 ||
            fread(&raw_t[i], 1, 1, f) != 1 ||
            fread(&raw_p[i], 1, 1, f) != 1) {
            free(raw_x); free(raw_y); free(raw_t); free(raw_p);
            fclose(f);
            if (out_error) *out_error = DAT_ERR_READ_CP_FAILED;
            return false;
        }
    }

    uint32_t pt_count = 0;
    bool has_points = (fread(&pt_count, 4, 1, f) == 1 && pt_count > 1);
    float *pt_x = NULL;
    float *pt_y = NULL;
    uint8_t *pt_tunnel = NULL;
    uint8_t *pt_priority = NULL;
    double *pt_s = NULL;

    if (has_points) {
        pt_x = (float*)malloc(pt_count * sizeof(float));
        pt_y = (float*)malloc(pt_count * sizeof(float));
        pt_tunnel = (uint8_t*)malloc(pt_count * sizeof(uint8_t));
        pt_priority = (uint8_t*)malloc(pt_count * sizeof(uint8_t));
        pt_s = (double*)malloc(pt_count * sizeof(double));

        if (!pt_x || !pt_y || !pt_tunnel || !pt_priority || !pt_s) {
            has_points = false;
            if (pt_x) free(pt_x);
            if (pt_y) free(pt_y);
            if (pt_tunnel) free(pt_tunnel);
            if (pt_priority) free(pt_priority);
            if (pt_s) free(pt_s);
            pt_x = NULL; pt_y = NULL; pt_tunnel = NULL; pt_priority = NULL; pt_s = NULL;
        } else {
            if (version == 1) {
                bool ok = true;
                for (uint32_t i = 0; i < pt_count; ++i) {
                    float px, py;
                    uint8_t pt_t, pt_p;
                    if (fread(&px, 4, 1, f) != 1 ||
                        fread(&py, 4, 1, f) != 1 ||
                        fread(&pt_t, 1, 1, f) != 1 ||
                        fread(&pt_p, 1, 1, f) != 1) {
                        ok = false;
                        break;
                    }
                    pt_x[i] = px;
                    pt_y[i] = py;
                    pt_tunnel[i] = pt_t;
                    pt_priority[i] = pt_p;
                    if (i == 0) {
                        pt_s[0] = 0.0;
                    } else {
                        double d = hypot((double)(pt_x[i] - pt_x[i - 1]), (double)(pt_y[i] - pt_y[i - 1]));
                        pt_s[i] = pt_s[i - 1] + d;
                    }
                }
                if (!ok) has_points = false;
            } else {
                /* version == 2 */
                float first_x, first_y;
                uint8_t first_t, first_p;
                if (fread(&first_x, 4, 1, f) == 1 &&
                    fread(&first_y, 4, 1, f) == 1 &&
                    fread(&first_t, 1, 1, f) == 1 &&
                    fread(&first_p, 1, 1, f) == 1) {
                    pt_x[0] = first_x;
                    pt_y[0] = first_y;
                    pt_tunnel[0] = first_t;
                    pt_priority[0] = first_p;
                    pt_s[0] = 0.0;
                    double cur_x = (double)first_x;
                    double cur_y = (double)first_y;
                    bool ok = true;
                    for (uint32_t i = 1; i < pt_count; ++i) {
                        int8_t dx, dy;
                        uint8_t pt_t, pt_p;
                        if (fread(&dx, 1, 1, f) != 1 ||
                            fread(&dy, 1, 1, f) != 1 ||
                            fread(&pt_t, 1, 1, f) != 1 ||
                            fread(&pt_p, 1, 1, f) != 1) {
                            ok = false;
                            break;
                        }
                        cur_x += (double)dx * 0.01;
                        cur_y += (double)dy * 0.01;
                        pt_x[i] = (float)cur_x;
                        pt_y[i] = (float)cur_y;
                        pt_tunnel[i] = pt_t;
                        pt_priority[i] = pt_p;
                        double step = hypot((double)dx * 0.01, (double)dy * 0.01);
                        pt_s[i] = pt_s[i - 1] + step;
                    }
                    if (!ok) has_points = false;
                } else {
                    has_points = false;
                }
            }

            if (!has_points) {
                free(pt_x); free(pt_y); free(pt_tunnel); free(pt_priority); free(pt_s);
                pt_x = NULL; pt_y = NULL; pt_tunnel = NULL; pt_priority = NULL; pt_s = NULL;
            }
        }
    }
    fclose(f);

    bezier_path_clear(path);

    uint32_t *cp_idx = (uint32_t*)malloc(cp_count * sizeof(uint32_t));
    if (has_points && cp_idx) {
        /* Map each control point raw_x[i], raw_y[i] to nearest discrete trajectory point */
        cp_idx[0] = 0;
        cp_idx[cp_count - 1] = pt_count - 1;
        uint32_t cur_start = 0;
        for (uint32_t i = 1; i + 1 < cp_count; ++i) {
            float cx = (float)raw_x[i];
            float cy = (float)raw_y[i];
            uint32_t best_k = cur_start;
            double best_d2 = 1e18;
            for (uint32_t k = cur_start; k < pt_count; ++k) {
                double d2 = (double)(pt_x[k] - cx) * (double)(pt_x[k] - cx) +
                            (double)(pt_y[k] - cy) * (double)(pt_y[k] - cy);
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best_k = k;
                } else if (best_d2 < 2.5 * 2.5 && d2 > best_d2 + 25.0) {
                    break;
                }
            }
            cp_idx[i] = best_k;
            cur_start = best_k;
        }

        /* Step 1: Add all nodes using exact sub-pixel trajectory coordinates */
        for (uint32_t i = 0; i < cp_count; ++i) {
            BezierNode node;
            memset(&node, 0, sizeof(BezierNode));
            node.x = pt_x[cp_idx[i]];
            node.y = pt_y[cp_idx[i]];
            node.type = (i == 0 || i == cp_count - 1) ? NODE_CORNER : NODE_SMOOTH;
            bezier_path_add_node(path, &node);
        }

        /* Step 2: Fit Bezier handles per segment (i -> i+1) to discrete trajectory points via least-squares */
        for (uint32_t i = 0; i + 1 < cp_count; ++i) {
            uint32_t k0 = cp_idx[i];
            uint32_t k1 = cp_idx[i + 1];
            uint32_t seg_len = (k1 >= k0) ? (k1 - k0 + 1) : 0;
            float P0x = path->nodes[i].x;
            float P0y = path->nodes[i].y;
            float P3x = path->nodes[i + 1].x;
            float P3y = path->nodes[i + 1].y;

            if (seg_len <= 2) {
                path->nodes[i].hout_x = (P3x - P0x) / 3.0f;
                path->nodes[i].hout_y = (P3y - P0y) / 3.0f;
                path->nodes[i + 1].hin_x = -(P3x - P0x) / 3.0f;
                path->nodes[i + 1].hin_y = -(P3y - P0y) / 3.0f;
            } else {
                double *seg_s = (double*)malloc(seg_len * sizeof(double));
                if (!seg_s) {
                    path->nodes[i].hout_x = (P3x - P0x) / 3.0f;
                    path->nodes[i].hout_y = (P3y - P0y) / 3.0f;
                    path->nodes[i + 1].hin_x = -(P3x - P0x) / 3.0f;
                    path->nodes[i + 1].hin_y = -(P3y - P0y) / 3.0f;
                } else {
                    seg_s[0] = 0.0;
                    for (uint32_t m = 1; m < seg_len; ++m) {
                        double dx = (double)(pt_x[k0 + m] - pt_x[k0 + m - 1]);
                        double dy = (double)(pt_y[k0 + m] - pt_y[k0 + m - 1]);
                        seg_s[m] = seg_s[m - 1] + hypot(dx, dy);
                    }
                    double tot_s = seg_s[seg_len - 1];
                    if (tot_s < 1e-6) {
                        path->nodes[i].hout_x = 0.0f;
                        path->nodes[i].hout_y = 0.0f;
                        path->nodes[i + 1].hin_x = 0.0f;
                        path->nodes[i + 1].hin_y = 0.0f;
                    } else {
                        double a11 = 0.0, a12 = 0.0, a22 = 0.0;
                        double bx1 = 0.0, bx2 = 0.0, by1 = 0.0, by2 = 0.0;
                        for (uint32_t m = 1; m + 1 < seg_len; ++m) {
                            double t = seg_s[m] / tot_s;
                            double u = 1.0 - t;
                            double c1 = 3.0 * u * u * t;
                            double c2 = 3.0 * u * t * t;
                            double c0 = u * u * u;
                            double c3 = t * t * t;
                            double rx = (double)pt_x[k0 + m] - c0 * (double)P0x - c3 * (double)P3x;
                            double ry = (double)pt_y[k0 + m] - c0 * (double)P0y - c3 * (double)P3y;
                            a11 += c1 * c1;
                            a12 += c1 * c2;
                            a22 += c2 * c2;
                            bx1 += c1 * rx;
                            bx2 += c2 * rx;
                            by1 += c1 * ry;
                            by2 += c2 * ry;
                        }
                        double det = a11 * a22 - a12 * a12;
                        if (fabs(det) < 1e-9) {
                            path->nodes[i].hout_x = (P3x - P0x) / 3.0f;
                            path->nodes[i].hout_y = (P3y - P0y) / 3.0f;
                            path->nodes[i + 1].hin_x = -(P3x - P0x) / 3.0f;
                            path->nodes[i + 1].hin_y = -(P3y - P0y) / 3.0f;
                        } else {
                            double b1x = (a22 * bx1 - a12 * bx2) / det;
                            double b2x = (a11 * bx2 - a12 * bx1) / det;
                            double b1y = (a22 * by1 - a12 * by2) / det;
                            double b2y = (a11 * by2 - a12 * by1) / det;
                            path->nodes[i].hout_x = (float)(b1x - (double)P0x);
                            path->nodes[i].hout_y = (float)(b1y - (double)P0y);
                            path->nodes[i + 1].hin_x = (float)(b2x - (double)P3x);
                            path->nodes[i + 1].hin_y = (float)(b2y - (double)P3y);
                        }
                    }
                    free(seg_s);
                }
            }
        }

        /* Determine smooth vs corner node types based on tangent alignment */
        for (uint32_t i = 1; i + 1 < cp_count; ++i) {
            float hix = path->nodes[i].hin_x;
            float hiy = path->nodes[i].hin_y;
            float hox = path->nodes[i].hout_x;
            float hoy = path->nodes[i].hout_y;
            float len_in = hypotf(hix, hiy);
            float len_out = hypotf(hox, hoy);
            if (len_in > 0.1f && len_out > 0.1f) {
                float dot = (-hix * hox + -hiy * hoy) / (len_in * len_out);
                if (dot > 0.95f) {
                    path->nodes[i].type = NODE_SMOOTH;
                } else {
                    path->nodes[i].type = NODE_CORNER;
                }
            } else {
                path->nodes[i].type = NODE_CORNER;
            }
        }
        path->nodes[0].type = NODE_CORNER;
        path->nodes[cp_count - 1].type = NODE_CORNER;

        free(cp_idx);
    } else {
        /* Fallback when no discrete points are present: Catmull-Rom */
        for (uint32_t i = 0; i < cp_count; ++i) {
            BezierNode node;
            memset(&node, 0, sizeof(BezierNode));
            node.x = (float)raw_x[i];
            node.y = (float)raw_y[i];
            node.type = (i == 0 || i == cp_count - 1) ? NODE_CORNER : NODE_SMOOTH;
            bezier_path_add_node(path, &node);
        }
        for (uint32_t i = 0; i + 1 < cp_count; ++i) {
            float p0x = (i > 0) ? (float)raw_x[i - 1] : (float)raw_x[0];
            float p0y = (i > 0) ? (float)raw_y[i - 1] : (float)raw_y[0];
            float p1x = (float)raw_x[i];
            float p1y = (float)raw_y[i];
            float p2x = (float)raw_x[i + 1];
            float p2y = (float)raw_y[i + 1];
            float p3x = (i + 2 < cp_count) ? (float)raw_x[i + 2] : (float)raw_x[cp_count - 1];
            float p3y = (i + 2 < cp_count) ? (float)raw_y[i + 2] : (float)raw_y[cp_count - 1];

            float b1x, b1y, b2x, b2y;
            catmull_rom_to_bezier(p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y, alpha, &b1x, &b1y, &b2x, &b2y);

            path->nodes[i].hout_x = b1x - p1x;
            path->nodes[i].hout_y = b1y - p1y;
            path->nodes[i + 1].hin_x = b2x - p2x;
            path->nodes[i + 1].hin_y = b2y - p2y;
        }
        if (cp_idx) free(cp_idx);
    }

    path_cache_update(path);

    /* Step 3: Extract attribute spans directly mapped to arc-length */
    if (has_points && path->cache.node_s && path->cache.node_count == path->node_count && pt_s) {
        double total_pt_s = pt_s[pt_count - 1];
        double total_bez_s = path->cache.total_length;
        double scale_s = (total_pt_s > 1e-4 && total_bez_s > 1e-4) ? (total_bez_s / total_pt_s) : 1.0;

        /* Tunnel runs from points */
        uint32_t t_start = 0;
        bool in_t = false;
        for (uint32_t i = 0; i < pt_count; ++i) {
            if (pt_tunnel[i] != 0 && !in_t) {
                in_t = true;
                t_start = i;
            } else if (pt_tunnel[i] == 0 && in_t) {
                in_t = false;
                double s_start = pt_s[t_start] * scale_s;
                double s_end   = pt_s[i] * scale_s;
                bezier_path_add_span_from_s(path, s_start, s_end, true, 1, false, 0);
            }
        }
        if (in_t) {
            double s_start = pt_s[t_start] * scale_s;
            double s_end   = total_bez_s;
            bezier_path_add_span_from_s(path, s_start, s_end, true, 1, false, 0);
        }

        /* Priority runs from points (pt_priority[i] != 0) */
        uint32_t p_start = 0;
        uint8_t cur_prio = 0;
        bool in_p = false;
        for (uint32_t i = 0; i < pt_count; ++i) {
            uint8_t p = pt_priority[i];
            if (p != 0 && (!in_p || p != cur_prio)) {
                if (in_p) {
                    double s_start = pt_s[p_start] * scale_s;
                    double s_end   = pt_s[i] * scale_s;
                    bezier_path_add_span_from_s(path, s_start, s_end, false, 0, true, cur_prio);
                }
                in_p = true;
                p_start = i;
                cur_prio = p;
            } else if (p == 0 && in_p) {
                in_p = false;
                double s_start = pt_s[p_start] * scale_s;
                double s_end   = pt_s[i] * scale_s;
                bezier_path_add_span_from_s(path, s_start, s_end, false, 0, true, cur_prio);
            }
        }
        if (in_p) {
            double s_start = pt_s[p_start] * scale_s;
            double s_end   = total_bez_s;
            bezier_path_add_span_from_s(path, s_start, s_end, false, 0, true, cur_prio);
        }

        free(pt_x);
        free(pt_y);
        free(pt_tunnel);
        free(pt_priority);
        free(pt_s);
    } else {
        /* Fallback: extract attribute spans from control points flags */
        uint32_t t_start = 0;
        bool in_t_run = false;
        for (uint32_t i = 0; i < cp_count; ++i) {
            if (raw_t[i] != 0 && !in_t_run) {
                in_t_run = true;
                t_start = i;
            } else if (raw_t[i] == 0 && in_t_run) {
                in_t_run = false;
                double s_start = path->cache.node_s ? path->cache.node_s[t_start] : 0.0;
                double s_end   = path->cache.node_s ? path->cache.node_s[i] : 0.0;
                bezier_path_add_span_from_s(path, s_start, s_end, true, 1, false, 0);
            }
        }
        if (in_t_run) {
            double s_start = path->cache.node_s ? path->cache.node_s[t_start] : 0.0;
            double s_end   = path->cache.total_length;
            bezier_path_add_span_from_s(path, s_start, s_end, true, 1, false, 0);
        }

        /* Priority runs from control points (where raw_p != 0) */
        uint32_t p_start = 0;
        uint8_t cur_prio = 0;
        bool in_p_run = false;
        for (uint32_t i = 0; i < cp_count; ++i) {
            uint8_t p = raw_p[i];
            if (p != 0 && (!in_p_run || p != cur_prio)) {
                if (in_p_run) {
                    double s_start = path->cache.node_s ? path->cache.node_s[p_start] : 0.0;
                    double s_end   = path->cache.node_s ? path->cache.node_s[i] : 0.0;
                    bezier_path_add_span_from_s(path, s_start, s_end, false, 0, true, cur_prio);
                }
                in_p_run = true;
                p_start = i;
                cur_prio = p;
            } else if (p == 0 && in_p_run) {
                in_p_run = false;
                double s_start = path->cache.node_s ? path->cache.node_s[p_start] : 0.0;
                double s_end   = path->cache.node_s ? path->cache.node_s[i] : 0.0;
                bezier_path_add_span_from_s(path, s_start, s_end, false, 0, true, cur_prio);
            }
        }
        if (in_p_run) {
            double s_start = path->cache.node_s ? path->cache.node_s[p_start] : 0.0;
            double s_end   = path->cache.total_length;
            bezier_path_add_span_from_s(path, s_start, s_end, false, 0, true, cur_prio);
        }
    }

    free(raw_x);
    free(raw_y);
    free(raw_t);
    free(raw_p);

    path_cache_invalidate(path);
    path_cache_update(path);

    return true;
}

/* Backward compatible wrapper */
bool bezier_path_import_dat(BezierPath *path, const char *in_filepath, float alpha) {
    return bezier_path_import_dat_ex(path, in_filepath, alpha, NULL);
}

/* Save ZuPather sidecar format (.zupath) atomically via temp file */
bool bezier_path_save_zupath(const BezierPath *paths, int path_count, const char *out_filepath) {
    if (!out_filepath) return false;

    char tmp_filepath[1024];
    snprintf(tmp_filepath, sizeof(tmp_filepath), "%s.tmp", out_filepath);

    if (!paths || path_count <= 0) {
        remove(out_filepath);
        remove(tmp_filepath);
        return true;
    }

    FILE *f = fopen(tmp_filepath, "w");
    if (!f) return false;

    fprintf(f, "# ZuPather Vector Path Metadata (Standard C locale with '.' decimal required)\n");
    fprintf(f, "ZUPATH 1.1\n");
    fprintf(f, "PATH_COUNT %d\n\n", path_count);

    for (int p = 0; p < path_count; ++p) {
        const BezierPath *bp = &paths[p];

        char safe_name[64];
        strncpy(safe_name, bp->name[0] ? bp->name : "curve", sizeof(safe_name) - 1);
        safe_name[sizeof(safe_name) - 1] = '\0';
        for (char *c = safe_name; *c; ++c) {
            if (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') *c = '_';
        }

        fprintf(f, "PATH %s CLOSED %d NODE_COUNT %d SPAN_COUNT %d\n",
                safe_name, bp->is_closed ? 1 : 0, bp->node_count, bp->span_count);

        for (int i = 0; i < bp->node_count; ++i) {
            const BezierNode *n = &bp->nodes[i];
            const char *type_str = "corner";
            if (n->type == NODE_SMOOTH) type_str = "smooth";
            else if (n->type == NODE_SYMMETRIC) type_str = "symmetric";
            else if (n->type == NODE_CUSP) type_str = "cusp";

            fprintf(f, "NODE %.3f %.3f %.3f %.3f %.3f %.3f %s\n",
                    n->x, n->y, n->hin_x, n->hin_y, n->hout_x, n->hout_y,
                    type_str);
        }

        for (int i = 0; i < bp->span_count; ++i) {
            const CurveSpan *sp = &bp->spans[i];
            int idx_c = bezier_path_find_node_by_id(bp, sp->node_c_id);
            int idx_d = bezier_path_find_node_by_id(bp, sp->node_d_id);
            fprintf(f, "SPAN %d %.3f %d %.3f %d %d %d %d\n",
                    idx_c, sp->off_c, idx_d, sp->off_d,
                    (int)sp->in_tunnel, (int)sp->priority,
                    sp->has_tunnel ? 1 : 0, sp->has_prio ? 1 : 0);
        }

        fprintf(f, "ENDPATH\n\n");
    }

    if (ferror(f) || fclose(f) != 0) {
        remove(tmp_filepath);
        return false;
    }

#ifdef _WIN32
    if (!MoveFileExA(tmp_filepath, out_filepath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        remove(tmp_filepath);
        return false;
    }
#else
    remove(out_filepath);
    rename(tmp_filepath, out_filepath);
#endif

    return true;
}

/* Load ZuPather sidecar format (.zupath) */
bool bezier_path_load_zupath(BezierPath **out_paths, int *out_path_count, const char *in_filepath) {
    if (out_path_count) *out_path_count = 0;
    if (out_paths) *out_paths = NULL;
    if (!in_filepath) return false;

    FILE *f = fopen(in_filepath, "r");
    if (!f) return false;

    char line[512];
    int capacity = 4;
    int count = 0;
    BezierPath *paths = (BezierPath*)malloc(capacity * sizeof(BezierPath));
    if (!paths) {
        fclose(f);
        return false;
    }

    BezierPath *cur_path = NULL;
    bool version_verified = false;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\r' || *p == '\n' || *p == '\0') continue;

        if (!version_verified) {
            if (strncmp(p, "ZUPATH 1.", 9) != 0) {
                fclose(f);
                free(paths);
                return false;
            }
            version_verified = true;
            continue;
        }

        if (strncmp(p, "PATH ", 5) == 0) {
            char name[64] = "curve";
            int is_closed = 0;
            int n_count = 0;
            int s_count = 0;
            sscanf(p + 5, "%63s CLOSED %d NODE_COUNT %d SPAN_COUNT %d", name, &is_closed, &n_count, &s_count);

            if (count >= capacity) {
                int new_cap = capacity * 2;
                BezierPath *new_paths = (BezierPath*)realloc(paths, new_cap * sizeof(BezierPath));
                if (!new_paths) {
                    for (int i = 0; i < count; ++i) bezier_path_free(&paths[i]);
                    free(paths);
                    fclose(f);
                    return false;
                }
                paths = new_paths;
                capacity = new_cap;
            }
            cur_path = &paths[count++];
            bezier_path_init(cur_path, name);
            cur_path->is_closed = (is_closed != 0);
        } else if (strncmp(p, "NODE ", 5) == 0 && cur_path) {
            float x, y, hin_x, hin_y, hout_x, hout_y;
            char type_str[32] = "corner";
            int legacy_tun = 0, legacy_prio = 0;

            int parsed = sscanf(p + 5, "%f %f %f %f %f %f %31s %d %d",
                                &x, &y, &hin_x, &hin_y, &hout_x, &hout_y,
                                type_str, &legacy_tun, &legacy_prio);
            if (parsed >= 6) {
                BezierNode node;
                memset(&node, 0, sizeof(BezierNode));
                node.x = x;
                node.y = y;
                node.hin_x = hin_x;
                node.hin_y = hin_y;
                node.hout_x = hout_x;
                node.hout_y = hout_y;

                if (strcmp(type_str, "smooth") == 0) node.type = NODE_SMOOTH;
                else if (strcmp(type_str, "symmetric") == 0) node.type = NODE_SYMMETRIC;
                else if (strcmp(type_str, "cusp") == 0) node.type = NODE_CUSP;
                else node.type = NODE_CORNER;

                bezier_path_add_node(cur_path, &node);
            }
        } else if (strncmp(p, "SPAN ", 5) == 0 && cur_path) {
            int idx_c = 0, idx_d = 0;
            float off_c = 0.0f, off_d = 0.0f;
            int tun = 0, prio = 0, has_tun = 1, has_p = 1;
            if (sscanf(p + 5, "%d %f %d %f %d %d %d %d",
                       &idx_c, &off_c, &idx_d, &off_d, &tun, &prio, &has_tun, &has_p) >= 6) {
                if (idx_c >= 0 && idx_c < cur_path->node_count &&
                    idx_d >= 0 && idx_d < cur_path->node_count) {
                    CurveSpan sp;
                    memset(&sp, 0, sizeof(CurveSpan));
                    sp.node_c_id = cur_path->nodes[idx_c].id;
                    sp.off_c = off_c;
                    sp.node_d_id = cur_path->nodes[idx_d].id;
                    sp.off_d = off_d;
                    sp.in_tunnel = (uint8_t)tun;
                    sp.priority = (uint8_t)prio;
                    sp.has_tunnel = (has_tun != 0);
                    sp.has_prio = (has_p != 0);
                    bezier_path_add_span(cur_path, &sp);
                }
            }
        } else if (strncmp(p, "ENDPATH", 7) == 0) {
            if (cur_path) {
                path_cache_update(cur_path);
            }
            cur_path = NULL;
        }
    }

    fclose(f);

    if (count == 0) {
        free(paths);
        return false;
    }

    if (out_paths) *out_paths = paths;
    if (out_path_count) *out_path_count = count;
    return true;
}
