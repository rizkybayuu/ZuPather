#include "svg_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

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
                        cur_path = &out_paths[cur_idx];
                        char name[64];
                        snprintf(name, sizeof(name), "curve-%d", cur_idx + 1);
                        bezier_path_init(cur_path, name);
                    }
                } else if (!cur_path) {
                    if (cur_idx < max_paths) {
                        cur_path = &out_paths[cur_idx];
                        char name[64];
                        snprintf(name, sizeof(name), "curve-%d", cur_idx + 1);
                        bezier_path_init(cur_path, name);
                    } else {
                        return; /* reached max paths */
                    }
                }

                if (cur_path) {
                    BezierNode n;
                    memset(&n, 0, sizeof(BezierNode));
                    n.x = roundf(cur_x);
                    n.y = roundf(cur_y);
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
                    prev_n->hout_x = roundf(cp1x - prev_n->x);
                    prev_n->hout_y = roundf(cp1y - prev_n->y);

                    BezierNode new_n;
                    memset(&new_n, 0, sizeof(BezierNode));
                    new_n.x = roundf(endx);
                    new_n.y = roundf(endy);
                    new_n.hin_x = roundf(cp2x - new_n.x);
                    new_n.hin_y = roundf(cp2y - new_n.y);
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
                    prev_n->hout_x = roundf(cp1x - prev_n->x);
                    prev_n->hout_y = roundf(cp1y - prev_n->y);

                    BezierNode new_n;
                    memset(&new_n, 0, sizeof(BezierNode));
                    new_n.x = roundf(endx);
                    new_n.y = roundf(endy);
                    new_n.hin_x = roundf(cp2x - new_n.x);
                    new_n.hin_y = roundf(cp2y - new_n.y);
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
                    prev_n->hout_x = roundf(cp1x - prev_n->x);
                    prev_n->hout_y = roundf(cp1y - prev_n->y);

                    BezierNode new_n;
                    memset(&new_n, 0, sizeof(BezierNode));
                    new_n.x = roundf(endx);
                    new_n.y = roundf(endy);
                    new_n.hin_x = roundf(cp2x - new_n.x);
                    new_n.hin_y = roundf(cp2y - new_n.y);
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
                    prev_n->hout_x = roundf(cp1x - prev_n->x);
                    prev_n->hout_y = roundf(cp1y - prev_n->y);

                    BezierNode new_n;
                    memset(&new_n, 0, sizeof(BezierNode));
                    new_n.x = roundf(endx);
                    new_n.y = roundf(endy);
                    new_n.hin_x = roundf(cp2x - new_n.x);
                    new_n.hin_y = roundf(cp2y - new_n.y);
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
                    n.x = roundf(cur_x);
                    n.y = roundf(cur_y);
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
                    n.x = roundf(cur_x);
                    n.y = roundf(cur_y);
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
                    n.x = roundf(cur_x);
                    n.y = roundf(cur_y);
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

/* Parse all <path d="..."> elements across an entire SVG string */
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

    const char *p = svg_content;
    while (*p && *out_count < max_paths) {
        /* Find next '<path' tag */
        const char *tag_start = strstr(p, "<path");
        if (!tag_start) tag_start = strstr(p, "<PATH");
        if (!tag_start) break;

        /* Find closing tag or '>' */
        const char *tag_end = strchr(tag_start, '>');
        if (!tag_end) break;

        /* Find d="..." or d='...' inside this tag */
        const char *d_attr = strstr(tag_start, " d=");
        if (!d_attr || d_attr > tag_end) {
            d_attr = strstr(tag_start, "\td=");
        }
        if (!d_attr || d_attr > tag_end) {
            d_attr = strstr(tag_start, "\nd=");
        }

        if (d_attr && d_attr < tag_end) {
            d_attr += 3;
            skip_svg_whitespace(&d_attr);
            char quote = *d_attr;
            if (quote == '"' || quote == '\'') {
                d_attr++;
                const char *val_end = strchr(d_attr, quote);
                if (val_end && val_end <= tag_end + 1) {
                    size_t val_len = (size_t)(val_end - d_attr);
                    char *d_buf = (char *)malloc(val_len + 1);
                    if (d_buf) {
                        memcpy(d_buf, d_attr, val_len);
                        d_buf[val_len] = '\0';
                        parse_d_string(d_buf, out_paths, max_paths, out_count);
                        free(d_buf);
                    }
                }
            }
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
