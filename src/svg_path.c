#include "svg_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SVG_MAX_GROUP_DEPTH 64

/* 2D Affine Transformation Matrix:
 * | a  c  e |
 * | b  d  f |
 * | 0  0  1 |
 * Transformed point: x' = a*x + c*y + e, y' = b*x + d*y + f
 */
typedef struct {
    float a, b, c, d, e, f;
} SvgMatrix;

static inline SvgMatrix svg_matrix_identity(void) {
    SvgMatrix m = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    return m;
}

static inline SvgMatrix svg_matrix_multiply(SvgMatrix m1, SvgMatrix m2) {
    SvgMatrix res;
    res.a = m1.a * m2.a + m1.c * m2.b;
    res.b = m1.b * m2.a + m1.d * m2.b;
    res.c = m1.a * m2.c + m1.c * m2.d;
    res.d = m1.b * m2.c + m1.d * m2.d;
    res.e = m1.a * m2.e + m1.c * m2.f + m1.e;
    res.f = m1.b * m2.e + m1.d * m2.f + m1.f;
    return res;
}

static inline void svg_matrix_transform_point(SvgMatrix m, float x, float y, float *ox, float *oy) {
    if (ox) *ox = m.a * x + m.c * y + m.e;
    if (oy) *oy = m.b * x + m.d * y + m.f;
}

static inline void svg_matrix_transform_vec(SvgMatrix m, float vx, float vy, float *ovx, float *ovy) {
    if (ovx) *ovx = m.a * vx + m.c * vy;
    if (ovy) *ovy = m.b * vx + m.d * vy;
}

/* Skip SVG delimiters: spaces, commas, newlines */
static void skip_svg_whitespace(const char **p) {
    while (**p && (isspace((unsigned char)**p) || **p == ',')) {
        (*p)++;
    }
}

/* Parse a single floating-point number from SVG path data stream */
static bool parse_svg_coord(const char **p, float *out_val) {
    skip_svg_whitespace(p);
    if (!**p) return false;

    char *endptr = NULL;
    float val = strtof(*p, &endptr);
    if (endptr == *p) {
        return false;
    }
    *out_val = val;
    *p = endptr;
    return true;
}

/* Parse SVG transform attribute list, chaining transforms:
 * matrix(a, b, c, d, e, f)
 * translate(tx [, ty])
 * scale(sx [, sy])
 * rotate(angle [, cx, cy])
 * skewX(angle)
 * skewY(angle)
 */
static SvgMatrix parse_svg_transform_list(const char *t_str) {
    SvgMatrix current = svg_matrix_identity();
    if (!t_str) return current;

    const char *p = t_str;
    while (*p) {
        skip_svg_whitespace(&p);
        if (!*p) break;

        char fname[32];
        int fidx = 0;
        while (*p && isalpha((unsigned char)*p) && fidx < (int)sizeof(fname) - 1) {
            fname[fidx++] = *p++;
        }
        fname[fidx] = '\0';

        skip_svg_whitespace(&p);
        if (*p != '(') {
            if (*p) p++;
            continue;
        }
        p++;

        float args[8];
        int arg_count = 0;
        while (*p && *p != ')' && arg_count < 8) {
            float v = 0.0f;
            if (parse_svg_coord(&p, &v)) {
                args[arg_count++] = v;
            } else {
                if (*p != ')') p++;
            }
            skip_svg_whitespace(&p);
        }

        if (*p == ')') p++;

        SvgMatrix fn_m = svg_matrix_identity();
        bool valid_fn = false;

        if (_stricmp(fname, "matrix") == 0 && arg_count >= 6) {
            fn_m.a = args[0];
            fn_m.b = args[1];
            fn_m.c = args[2];
            fn_m.d = args[3];
            fn_m.e = args[4];
            fn_m.f = args[5];
            valid_fn = true;
        } else if (_stricmp(fname, "translate") == 0 && arg_count >= 1) {
            float tx = args[0];
            float ty = (arg_count >= 2) ? args[1] : 0.0f;
            fn_m.e = tx;
            fn_m.f = ty;
            valid_fn = true;
        } else if (_stricmp(fname, "scale") == 0 && arg_count >= 1) {
            float sx = args[0];
            float sy = (arg_count >= 2) ? args[1] : sx;
            fn_m.a = sx;
            fn_m.d = sy;
            valid_fn = true;
        } else if (_stricmp(fname, "rotate") == 0 && arg_count >= 1) {
            float angle_deg = args[0];
            float rad = angle_deg * (float)(M_PI / 180.0);
            float cos_a = cosf(rad);
            float sin_a = sinf(rad);
            if (arg_count >= 3) {
                float cx = args[1];
                float cy = args[2];
                fn_m.a = cos_a;
                fn_m.b = sin_a;
                fn_m.c = -sin_a;
                fn_m.d = cos_a;
                fn_m.e = cx * (1.0f - cos_a) + cy * sin_a;
                fn_m.f = cy * (1.0f - cos_a) - cx * sin_a;
            } else {
                fn_m.a = cos_a;
                fn_m.b = sin_a;
                fn_m.c = -sin_a;
                fn_m.d = cos_a;
            }
            valid_fn = true;
        } else if (_stricmp(fname, "skewX") == 0 && arg_count >= 1) {
            float rad = args[0] * (float)(M_PI / 180.0);
            fn_m.c = tanf(rad);
            valid_fn = true;
        } else if (_stricmp(fname, "skewY") == 0 && arg_count >= 1) {
            float rad = args[0] * (float)(M_PI / 180.0);
            fn_m.b = tanf(rad);
            valid_fn = true;
        }

        if (valid_fn) {
            current = svg_matrix_multiply(current, fn_m);
        }
    }

    return current;
}

/* Extract named attribute value from XML element tag string */
static char *extract_xml_attribute(const char *tag_start, const char *tag_end, const char *attr_name) {
    if (!tag_start || !tag_end || !attr_name) return NULL;
    size_t attr_len = strlen(attr_name);

    const char *p = tag_start;
    while (p < tag_end) {
        const char *found = strstr(p, attr_name);
        if (!found || found >= tag_end) return NULL;

        if (found > tag_start && !isspace((unsigned char)*(found - 1))) {
            p = found + attr_len;
            continue;
        }

        const char *eq = found + attr_len;
        while (eq < tag_end && isspace((unsigned char)*eq)) eq++;
        if (eq >= tag_end || *eq != '=') {
            p = found + attr_len;
            continue;
        }
        eq++;
        while (eq < tag_end && isspace((unsigned char)*eq)) eq++;
        if (eq >= tag_end) return NULL;

        char quote = *eq;
        if (quote != '"' && quote != '\'') {
            p = eq;
            continue;
        }
        eq++;

        const char *val_end = memchr(eq, quote, (size_t)(tag_end - eq));
        if (!val_end) return NULL;

        size_t val_len = (size_t)(val_end - eq);
        char *val = (char *)malloc(val_len + 1);
        if (!val) return NULL;
        memcpy(val, eq, val_len);
        val[val_len] = '\0';
        return val;
    }
    return NULL;
}

/* Apply 2D affine transformation matrix to all nodes & handles in a BezierPath */
static void apply_matrix_to_bezier_path(BezierPath *path, SvgMatrix m) {
    if (!path || path->node_count <= 0) return;

    for (int i = 0; i < path->node_count; ++i) {
        BezierNode *n = &path->nodes[i];
        float old_x = n->x;
        float old_y = n->y;

        svg_matrix_transform_point(m, old_x, old_y, &n->x, &n->y);

        if (n->hin_x != 0.0f || n->hin_y != 0.0f) {
            float hx, hy;
            svg_matrix_transform_vec(m, n->hin_x, n->hin_y, &hx, &hy);
            n->hin_x = hx;
            n->hin_y = hy;
        }

        if (n->hout_x != 0.0f || n->hout_y != 0.0f) {
            float hx, hy;
            svg_matrix_transform_vec(m, n->hout_x, n->hout_y, &hx, &hy);
            n->hout_x = hx;
            n->hout_y = hy;
        }
    }

    path_cache_invalidate(path);
    path_cache_update(path);
}

/* Extract SVG dimensions from <svg ... width="..." height="..." viewBox="..."> */
static void extract_svg_dimensions(const char *svg, SvgDimensions *out_dims) {
    if (!out_dims) return;
    out_dims->view_w = 640.0f;
    out_dims->view_h = 480.0f;
    out_dims->has_viewbox = false;

    const char *tag = strstr(svg, "<svg");
    if (!tag) tag = strstr(svg, "<SVG");
    if (!tag) return;

    /* Look for viewBox="min_x min_y width height" */
    const char *vb = strstr(tag, "viewBox=");
    if (!vb) vb = strstr(tag, "viewbox=");
    if (vb) {
        vb += 8;
        if (*vb == '"' || *vb == '\'') vb++;
        float min_x = 0.0f, min_y = 0.0f, w = 0.0f, h = 0.0f;
        if (parse_svg_coord(&vb, &min_x) && parse_svg_coord(&vb, &min_y) &&
            parse_svg_coord(&vb, &w) && parse_svg_coord(&vb, &h)) {
            if (w > 1.0f && h > 1.0f) {
                out_dims->view_w = w;
                out_dims->view_h = h;
                out_dims->has_viewbox = true;
                return;
            }
        }
    }

    /* Fallback: look for width and height attributes */
    const char *w_attr = strstr(tag, "width=");
    const char *h_attr = strstr(tag, "height=");
    if (w_attr && h_attr) {
        w_attr += 6;
        if (*w_attr == '"' || *w_attr == '\'') w_attr++;
        h_attr += 7;
        if (*h_attr == '"' || *h_attr == '\'') h_attr++;

        float w = 0.0f, h = 0.0f;
        if (parse_svg_coord(&w_attr, &w) && parse_svg_coord(&h_attr, &h)) {
            if (w > 1.0f && h > 1.0f) {
                out_dims->view_w = w;
                out_dims->view_h = h;
                out_dims->has_viewbox = true;
            }
        }
    }
}

/* Parse a single SVG path data string (contents of d="...") into BezierPath array */
static void parse_d_string(
    const char *d,
    const char *id_name,
    BezierPath *out_paths,
    int max_paths,
    int *inout_count
) {
    if (!d || !out_paths || max_paths <= 0 || !inout_count) return;

    const char *p = d;
    char cmd = 0;
    float cur_x = 0.0f, cur_y = 0.0f;
    float start_x = 0.0f, start_y = 0.0f;
    float last_cp2_x = 0.0f, last_cp2_y = 0.0f;
    float last_qp_x = 0.0f, last_qp_y = 0.0f;
    char prev_cmd = 0;

    BezierPath *cur_path = NULL;
    int cur_idx = *inout_count;
    int subpath_idx = 0;

    while (*p) {
        skip_svg_whitespace(&p);
        if (!*p) break;

        /* If next character is an alphabetic command */
        if (isalpha((unsigned char)*p)) {
            cmd = *p++;
        } else if (!cmd) {
            /* Unexpected token without preceding command */
            p++;
            continue;
        }

        switch (cmd) {
            case 'M':
            case 'm': {
                bool is_rel = (cmd == 'm');
                float mx = 0.0f, my = 0.0f;
                if (!parse_svg_coord(&p, &mx) || !parse_svg_coord(&p, &my)) break;

                if (is_rel && cur_path && cur_path->node_count > 0) {
                    cur_x += mx;
                    cur_y += my;
                } else {
                    cur_x = mx;
                    cur_y = my;
                }
                start_x = cur_x;
                start_y = cur_y;

                /* If the current path already has at least 2 nodes, start a new subpath */
                if (cur_path && cur_path->node_count >= 2) {
                    if (cur_idx + 1 < max_paths) {
                        path_cache_invalidate(cur_path);
                        path_cache_update(cur_path);
                        cur_idx++;
                        subpath_idx++;
                        cur_path = &out_paths[cur_idx];
                        char name[64];
                        if (id_name && id_name[0]) {
                            snprintf(name, sizeof(name), "%s-%d", id_name, subpath_idx + 1);
                        } else {
                            snprintf(name, sizeof(name), "curve-%d", cur_idx + 1);
                        }
                        bezier_path_init(cur_path, name);
                    }
                } else if (!cur_path) {
                    if (cur_idx < max_paths) {
                        cur_path = &out_paths[cur_idx];
                        char name[64];
                        if (id_name && id_name[0]) {
                            snprintf(name, sizeof(name), "%s", id_name);
                        } else {
                            snprintf(name, sizeof(name), "curve-%d", cur_idx + 1);
                        }
                        bezier_path_init(cur_path, name);
                    } else {
                        return; /* reached max paths */
                    }
                }

                if (cur_path) {
                    BezierNode n;
                    memset(&n, 0, sizeof(BezierNode));
                    n.x = cur_x;
                    n.y = cur_y;
                    n.type = NODE_CORNER;
                    bezier_path_add_node(cur_path, &n);
                }

                last_cp2_x = cur_x;
                last_cp2_y = cur_y;
                last_qp_x = cur_x;
                last_qp_y = cur_y;
                prev_cmd = cmd;

                /* In SVG: subsequent coordinates after M/m are treated as L/l */
                cmd = is_rel ? 'l' : 'L';
                break;
            }

            case 'C':
            case 'c': {
                bool is_rel = (cmd == 'c');
                float cp1x, cp1y, cp2x, cp2y, endx, endy;
                if (!parse_svg_coord(&p, &cp1x) || !parse_svg_coord(&p, &cp1y) ||
                    !parse_svg_coord(&p, &cp2x) || !parse_svg_coord(&p, &cp2y) ||
                    !parse_svg_coord(&p, &endx) || !parse_svg_coord(&p, &endy)) {
                    cmd = 0;
                    break;
                }

                if (is_rel) {
                    cp1x += cur_x; cp1y += cur_y;
                    cp2x += cur_x; cp2y += cur_y;
                    endx += cur_x; endy += cur_y;
                }

                if (cur_path && cur_path->node_count > 0) {
                    BezierNode *prev_n = &cur_path->nodes[cur_path->node_count - 1];
                    prev_n->hout_x = cp1x - prev_n->x;
                    prev_n->hout_y = cp1y - prev_n->y;

                    BezierNode new_n;
                    memset(&new_n, 0, sizeof(BezierNode));
                    new_n.x = endx;
                    new_n.y = endy;
                    new_n.hin_x = cp2x - new_n.x;
                    new_n.hin_y = cp2y - new_n.y;
                    new_n.type = NODE_SMOOTH;
                    bezier_path_add_node(cur_path, &new_n);
                }

                cur_x = endx;
                cur_y = endy;
                last_cp2_x = cp2x;
                last_cp2_y = cp2y;
                prev_cmd = cmd;
                break;
            }

            case 'S':
            case 's': {
                bool is_rel = (cmd == 's');
                float cp2x, cp2y, endx, endy;
                if (!parse_svg_coord(&p, &cp2x) || !parse_svg_coord(&p, &cp2y) ||
                    !parse_svg_coord(&p, &endx) || !parse_svg_coord(&p, &endy)) {
                    cmd = 0;
                    break;
                }

                if (is_rel) {
                    cp2x += cur_x; cp2y += cur_y;
                    endx += cur_x; endy += cur_y;
                }

                /* Calculate reflected control point 1 */
                float cp1x = cur_x;
                float cp1y = cur_y;
                if (prev_cmd == 'C' || prev_cmd == 'c' || prev_cmd == 'S' || prev_cmd == 's') {
                    cp1x = 2.0f * cur_x - last_cp2_x;
                    cp1y = 2.0f * cur_y - last_cp2_y;
                }

                if (cur_path && cur_path->node_count > 0) {
                    BezierNode *prev_n = &cur_path->nodes[cur_path->node_count - 1];
                    prev_n->hout_x = cp1x - prev_n->x;
                    prev_n->hout_y = cp1y - prev_n->y;

                    BezierNode new_n;
                    memset(&new_n, 0, sizeof(BezierNode));
                    new_n.x = endx;
                    new_n.y = endy;
                    new_n.hin_x = cp2x - new_n.x;
                    new_n.hin_y = cp2y - new_n.y;
                    new_n.type = NODE_SMOOTH;
                    bezier_path_add_node(cur_path, &new_n);
                }

                cur_x = endx;
                cur_y = endy;
                last_cp2_x = cp2x;
                last_cp2_y = cp2y;
                prev_cmd = cmd;
                break;
            }

            case 'Q':
            case 'q': {
                bool is_rel = (cmd == 'q');
                float q1x, q1y, endx, endy;
                if (!parse_svg_coord(&p, &q1x) || !parse_svg_coord(&p, &q1y) ||
                    !parse_svg_coord(&p, &endx) || !parse_svg_coord(&p, &endy)) {
                    cmd = 0;
                    break;
                }

                if (is_rel) {
                    q1x += cur_x; q1y += cur_y;
                    endx += cur_x; endy += cur_y;
                }

                /* Degree elevate Quadratic to Cubic Bezier control points */
                float cp1x = cur_x + (2.0f / 3.0f) * (q1x - cur_x);
                float cp1y = cur_y + (2.0f / 3.0f) * (q1y - cur_y);
                float cp2x = endx  + (2.0f / 3.0f) * (q1x - endx);
                float cp2y = endy  + (2.0f / 3.0f) * (q1y - endy);

                if (cur_path && cur_path->node_count > 0) {
                    BezierNode *prev_n = &cur_path->nodes[cur_path->node_count - 1];
                    prev_n->hout_x = cp1x - prev_n->x;
                    prev_n->hout_y = cp1y - prev_n->y;

                    BezierNode new_n;
                    memset(&new_n, 0, sizeof(BezierNode));
                    new_n.x = endx;
                    new_n.y = endy;
                    new_n.hin_x = cp2x - new_n.x;
                    new_n.hin_y = cp2y - new_n.y;
                    new_n.type = NODE_SMOOTH;
                    bezier_path_add_node(cur_path, &new_n);
                }

                cur_x = endx;
                cur_y = endy;
                last_qp_x = q1x;
                last_qp_y = q1y;
                prev_cmd = cmd;
                break;
            }

            case 'T':
            case 't': {
                bool is_rel = (cmd == 't');
                float endx, endy;
                if (!parse_svg_coord(&p, &endx) || !parse_svg_coord(&p, &endy)) {
                    cmd = 0;
                    break;
                }

                if (is_rel) {
                    endx += cur_x;
                    endy += cur_y;
                }

                float q1x = cur_x;
                float q1y = cur_y;
                if (prev_cmd == 'Q' || prev_cmd == 'q' || prev_cmd == 'T' || prev_cmd == 't') {
                    q1x = 2.0f * cur_x - last_qp_x;
                    q1y = 2.0f * cur_y - last_qp_y;
                }

                float cp1x = cur_x + (2.0f / 3.0f) * (q1x - cur_x);
                float cp1y = cur_y + (2.0f / 3.0f) * (q1y - cur_y);
                float cp2x = endx  + (2.0f / 3.0f) * (q1x - endx);
                float cp2y = endy  + (2.0f / 3.0f) * (q1y - endy);

                if (cur_path && cur_path->node_count > 0) {
                    BezierNode *prev_n = &cur_path->nodes[cur_path->node_count - 1];
                    prev_n->hout_x = cp1x - prev_n->x;
                    prev_n->hout_y = cp1y - prev_n->y;

                    BezierNode new_n;
                    memset(&new_n, 0, sizeof(BezierNode));
                    new_n.x = endx;
                    new_n.y = endy;
                    new_n.hin_x = cp2x - new_n.x;
                    new_n.hin_y = cp2y - new_n.y;
                    new_n.type = NODE_SMOOTH;
                    bezier_path_add_node(cur_path, &new_n);
                }

                cur_x = endx;
                cur_y = endy;
                last_qp_x = q1x;
                last_qp_y = q1y;
                prev_cmd = cmd;
                break;
            }

            case 'L':
            case 'l': {
                bool is_rel = (cmd == 'l');
                float lx, ly;
                if (!parse_svg_coord(&p, &lx) || !parse_svg_coord(&p, &ly)) {
                    cmd = 0;
                    break;
                }

                if (is_rel) {
                    cur_x += lx;
                    cur_y += ly;
                } else {
                    cur_x = lx;
                    cur_y = ly;
                }

                if (cur_path) {
                    BezierNode n;
                    memset(&n, 0, sizeof(BezierNode));
                    n.x = cur_x;
                    n.y = cur_y;
                    n.type = NODE_CORNER;
                    bezier_path_add_node(cur_path, &n);
                }

                last_cp2_x = cur_x;
                last_cp2_y = cur_y;
                last_qp_x = cur_x;
                last_qp_y = cur_y;
                prev_cmd = cmd;
                break;
            }

            case 'H':
            case 'h': {
                bool is_rel = (cmd == 'h');
                float hx;
                if (!parse_svg_coord(&p, &hx)) {
                    cmd = 0;
                    break;
                }

                if (is_rel) cur_x += hx;
                else cur_x = hx;

                if (cur_path) {
                    BezierNode n;
                    memset(&n, 0, sizeof(BezierNode));
                    n.x = cur_x;
                    n.y = cur_y;
                    n.type = NODE_CORNER;
                    bezier_path_add_node(cur_path, &n);
                }

                last_cp2_x = cur_x;
                last_cp2_y = cur_y;
                last_qp_x = cur_x;
                last_qp_y = cur_y;
                prev_cmd = cmd;
                break;
            }

            case 'V':
            case 'v': {
                bool is_rel = (cmd == 'v');
                float vy;
                if (!parse_svg_coord(&p, &vy)) {
                    cmd = 0;
                    break;
                }

                if (is_rel) cur_y += vy;
                else cur_y = vy;

                if (cur_path) {
                    BezierNode n;
                    memset(&n, 0, sizeof(BezierNode));
                    n.x = cur_x;
                    n.y = cur_y;
                    n.type = NODE_CORNER;
                    bezier_path_add_node(cur_path, &n);
                }

                last_cp2_x = cur_x;
                last_cp2_y = cur_y;
                last_qp_x = cur_x;
                last_qp_y = cur_y;
                prev_cmd = cmd;
                break;
            }

            case 'Z':
            case 'z': {
                if (cur_path && cur_path->node_count >= 2) {
                    cur_path->is_closed = true;
                    /* If the last node coincides with start node, merge */
                    BezierNode *last_n = &cur_path->nodes[cur_path->node_count - 1];
                    BezierNode *first_n = &cur_path->nodes[0];
                    if (fabsf(last_n->x - first_n->x) < 1.0f && fabsf(last_n->y - first_n->y) < 1.0f) {
                        first_n->hin_x = last_n->hin_x;
                        first_n->hin_y = last_n->hin_y;
                        cur_path->node_count--;
                    }
                }
                cur_x = start_x;
                cur_y = start_y;
                last_cp2_x = cur_x;
                last_cp2_y = cur_y;
                last_qp_x = cur_x;
                last_qp_y = cur_y;
                prev_cmd = cmd;
                cmd = 0;
                break;
            }

            default:
                /* Unsupported command character: advance */
                p++;
                break;
        }
    }

    if (cur_path && cur_path->node_count >= 2) {
        path_cache_invalidate(cur_path);
        path_cache_update(cur_path);
        cur_idx++;
    } else if (cur_path && cur_path->node_count < 2) {
        /* Incomplete single-point path: discard */
        bezier_path_free(cur_path);
        memset(cur_path, 0, sizeof(BezierPath));
    }

    *inout_count = cur_idx;
}

/* Parse all <path d="..."> elements across an entire SVG string, honoring
 * hierarchical <g transform="..."> groups and path transforms.
 */
bool svg_path_import_from_memory(
    const char *svg_content,
    size_t length,
    BezierPath *out_paths,
    int max_paths,
    int *out_count,
    SvgDimensions *out_dims
) {
    if (!svg_content || length == 0 || !out_paths || max_paths <= 0 || !out_count) {
        return false;
    }

    *out_count = 0;
    extract_svg_dimensions(svg_content, out_dims);

    SvgMatrix matrix_stack[SVG_MAX_GROUP_DEPTH];
    int stack_depth = 0;
    matrix_stack[0] = svg_matrix_identity();

    int ignore_depth = 0;

    const char *p = svg_content;
    const char *doc_end = svg_content + length;

    while (p < doc_end && *out_count < max_paths) {
        const char *tag_start = strchr(p, '<');
        if (!tag_start || tag_start >= doc_end) break;

        /* Skip XML comments: <!-- ... --> */
        if (strncmp(tag_start, "<!--", 4) == 0) {
            const char *comment_end = strstr(tag_start + 4, "-->");
            p = comment_end ? comment_end + 3 : doc_end;
            continue;
        }

        /* Skip CDATA: <![CDATA[ ... ]]> */
        if (strncmp(tag_start, "<![CDATA[", 9) == 0) {
            const char *cdata_end = strstr(tag_start + 9, "]]>");
            p = cdata_end ? cdata_end + 3 : doc_end;
            continue;
        }

        /* Skip processing instructions: <? ... ?> and doctype: <!DOCTYPE ... > */
        if (tag_start[1] == '?' || (tag_start[1] == '!' && strncmp(tag_start, "<!DOCTYPE", 9) == 0)) {
            const char *pi_end = strchr(tag_start, '>');
            p = pi_end ? pi_end + 1 : doc_end;
            continue;
        }

        /* Find closing '>' for this tag */
        const char *tag_end = strchr(tag_start, '>');
        if (!tag_end || tag_end >= doc_end) break;

        /* Check if closing tag: </tagname> */
        if (tag_start[1] == '/') {
            const char *cname_start = tag_start + 2;
            skip_svg_whitespace(&cname_start);
            char cname[64];
            int cidx = 0;
            while (cname_start < tag_end && !isspace((unsigned char)*cname_start) && *cname_start != '>' && cidx < (int)sizeof(cname) - 1) {
                cname[cidx++] = *cname_start++;
            }
            cname[cidx] = '\0';

            /* Strip namespace prefix if any (e.g. "svg:g" -> "g") */
            char *colon = strchr(cname, ':');
            const char *tag_name = colon ? colon + 1 : cname;

            if (_stricmp(tag_name, "defs") == 0 ||
                _stricmp(tag_name, "clipPath") == 0 ||
                _stricmp(tag_name, "mask") == 0 ||
                _stricmp(tag_name, "style") == 0 ||
                _stricmp(tag_name, "metadata") == 0) {
                if (ignore_depth > 0) ignore_depth--;
            } else if (ignore_depth > 0) {
                ignore_depth--;
            } else if (_stricmp(tag_name, "g") == 0) {
                if (stack_depth > 0) {
                    stack_depth--;
                }
            }

            p = tag_end + 1;
            continue;
        }

        /* Opening tag */
        bool is_self_closing = false;
        if (tag_end > tag_start && *(tag_end - 1) == '/') {
            is_self_closing = true;
        }

        /* Extract tag name */
        const char *tname_start = tag_start + 1;
        skip_svg_whitespace(&tname_start);
        char tname[64];
        int tidx = 0;
        while (tname_start < tag_end && !isspace((unsigned char)*tname_start) && *tname_start != '/' && *tname_start != '>' && tidx < (int)sizeof(tname) - 1) {
            tname[tidx++] = *tname_start++;
        }
        tname[tidx] = '\0';

        char *colon = strchr(tname, ':');
        const char *tag_name = colon ? colon + 1 : tname;

        /* If we are currently inside an ignored block (<defs>, <clipPath>, etc.) */
        if (ignore_depth > 0) {
            if (!is_self_closing) {
                ignore_depth++;
            }
            p = tag_end + 1;
            continue;
        }

        /* Check for tags that start an ignored block */
        if (_stricmp(tag_name, "defs") == 0 ||
            _stricmp(tag_name, "clipPath") == 0 ||
            _stricmp(tag_name, "mask") == 0 ||
            _stricmp(tag_name, "style") == 0 ||
            _stricmp(tag_name, "metadata") == 0) {
            if (!is_self_closing) {
                ignore_depth++;
            }
            p = tag_end + 1;
            continue;
        }

        /* Group element <g> */
        if (_stricmp(tag_name, "g") == 0) {
            if (!is_self_closing) {
                SvgMatrix cur_m = matrix_stack[stack_depth];
                char *t_attr = extract_xml_attribute(tag_start, tag_end, "transform");
                if (t_attr) {
                    SvgMatrix g_m = parse_svg_transform_list(t_attr);
                    cur_m = svg_matrix_multiply(cur_m, g_m);
                    free(t_attr);
                }
                if (stack_depth + 1 < SVG_MAX_GROUP_DEPTH) {
                    stack_depth++;
                    matrix_stack[stack_depth] = cur_m;
                }
            }
            p = tag_end + 1;
            continue;
        }

        /* Path element <path> */
        if (_stricmp(tag_name, "path") == 0) {
            char *d_attr = extract_xml_attribute(tag_start, tag_end, "d");
            if (d_attr) {
                SvgMatrix eff_m = matrix_stack[stack_depth];
                char *t_attr = extract_xml_attribute(tag_start, tag_end, "transform");
                if (t_attr) {
                    SvgMatrix p_m = parse_svg_transform_list(t_attr);
                    eff_m = svg_matrix_multiply(eff_m, p_m);
                    free(t_attr);
                }

                char *id_attr = extract_xml_attribute(tag_start, tag_end, "id");

                int prev_count = *out_count;
                parse_d_string(d_attr, id_attr, out_paths, max_paths, out_count);

                /* Apply effective hierarchical transform to all newly created subpaths */
                for (int i = prev_count; i < *out_count; ++i) {
                    apply_matrix_to_bezier_path(&out_paths[i], eff_m);
                }

                if (id_attr) free(id_attr);
                free(d_attr);
            }
            p = tag_end + 1;
            continue;
        }

        p = tag_end + 1;
    }

    return (*out_count > 0);
}

/* Load and parse SVG from file path */
bool svg_path_import_from_file(
    const char *filepath,
    BezierPath *out_paths,
    int max_paths,
    int *out_count,
    SvgDimensions *out_dims
) {
    if (!filepath || !out_paths || max_paths <= 0 || !out_count) return false;

    FILE *f = fopen(filepath, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 50 * 1024 * 1024) { /* 50MB sanity limit */
        fclose(f);
        return false;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = '\0';

    bool ok = svg_path_import_from_memory(buf, rd, out_paths, max_paths, out_count, out_dims);
    free(buf);
    return ok;
}

/* Export Bezier paths to memory as SVG XML */
char *svg_path_export_to_memory(
    const BezierPath *paths,
    int count,
    int canvas_w,
    int canvas_h
) {
    if (!paths || count <= 0) return NULL;
    if (canvas_w <= 0) canvas_w = 640;
    if (canvas_h <= 0) canvas_h = 480;

    size_t buf_cap = 4096;
    for (int p = 0; p < count; ++p) {
        buf_cap += paths[p].node_count * 128 + 256;
    }

    char *xml = (char *)malloc(buf_cap);
    if (!xml) return NULL;

    int offset = snprintf(xml, buf_cap,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n"
        "  <!-- Exported from ZuPather - Zuma Path Editor (Transparent Canvas) -->\n",
        canvas_w, canvas_h, canvas_w, canvas_h
    );

    for (int p = 0; p < count; ++p) {
        const BezierPath *bp = &paths[p];
        if (bp->node_count < 2) continue;

        const char *name = bp->name[0] ? bp->name : "curve";
        offset += snprintf(xml + offset, buf_cap - offset,
            "  <path id=\"%s\" d=\"M %d %d",
            name,
            (int)roundf(bp->nodes[0].x),
            (int)roundf(bp->nodes[0].y)
        );

        int num_segs = bp->is_closed ? bp->node_count : bp->node_count - 1;
        for (int i = 0; i < num_segs; ++i) {
            const BezierNode *n0 = &bp->nodes[i];
            const BezierNode *n1 = (i + 1 < bp->node_count) ? &bp->nodes[i + 1] : &bp->nodes[0];

            int cp1x = (int)roundf(n0->x + n0->hout_x);
            int cp1y = (int)roundf(n0->y + n0->hout_y);
            int cp2x = (int)roundf(n1->x + n1->hin_x);
            int cp2y = (int)roundf(n1->y + n1->hin_y);
            int endx = (int)roundf(n1->x);
            int endy = (int)roundf(n1->y);

            offset += snprintf(xml + offset, buf_cap - offset,
                " C %d %d %d %d %d %d",
                cp1x, cp1y, cp2x, cp2y, endx, endy
            );
        }

        if (bp->is_closed) {
            offset += snprintf(xml + offset, buf_cap - offset, " Z");
        }

        offset += snprintf(xml + offset, buf_cap - offset,
            "\" fill=\"none\" stroke=\"#FFD700\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n"
        );
    }

    offset += snprintf(xml + offset, buf_cap - offset, "</svg>\n");
    return xml;
}

/* Export Bezier paths to file */
bool svg_path_export_to_file(
    const char *filepath,
    const BezierPath *paths,
    int count,
    int canvas_w,
    int canvas_h
) {
    if (!filepath || !paths || count <= 0) return false;

    char *xml = svg_path_export_to_memory(paths, count, canvas_w, canvas_h);
    if (!xml) return false;

    FILE *f = fopen(filepath, "wb");
    if (!f) {
        free(xml);
        return false;
    }

    size_t len = strlen(xml);
    size_t written = fwrite(xml, 1, len, f);
    fclose(f);
    free(xml);

    return (written == len);
}
