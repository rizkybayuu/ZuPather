#include "ai_psd_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#if defined(__has_include)
  #if __has_include(<zstd.h>)
    #include <zstd.h>
    #define HAVE_ZSTD 1
  #endif
#else
  #include <zstd.h>
  #define HAVE_ZSTD 1
#endif

/* ============================================================================
 * 2D Affine Transformation Matrix
 * ============================================================================ */
typedef struct {
    float a, b, c, d, e, f;
} VectorMatrix;

static inline VectorMatrix vec_matrix_identity(void) {
    VectorMatrix m = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    return m;
}

static inline VectorMatrix vec_matrix_multiply(VectorMatrix m1, VectorMatrix m2) {
    VectorMatrix r;
    r.a = m1.a * m2.a + m1.c * m2.b;
    r.b = m1.b * m2.a + m1.d * m2.b;
    r.c = m1.a * m2.c + m1.c * m2.d;
    r.d = m1.b * m2.c + m1.d * m2.d;
    r.e = m1.a * m2.e + m1.c * m2.f + m1.e;
    r.f = m1.b * m2.e + m1.d * m2.f + m1.f;
    return r;
}

static inline void vec_matrix_transform_point(VectorMatrix m, float x, float y, float *ox, float *oy) {
    if (ox) *ox = m.a * x + m.c * y + m.e;
    if (oy) *oy = m.b * x + m.d * y + m.f;
}

static BezierNode* append_node(BezierPath *path, float x, float y, BezierNodeType type) {
    if (!path) return NULL;
    BezierNode n;
    memset(&n, 0, sizeof(BezierNode));
    n.x = x;
    n.y = y;
    n.type = type;
    if (bezier_path_add_node(path, &n)) {
        return &path->nodes[path->node_count - 1];
    }
    return NULL;
}

/* ============================================================================
 * Format Detection
 * ============================================================================ */
VectorFileFormat vector_path_detect_format(const char *filepath, const uint8_t *header, size_t header_len) {
    const char *ext = filepath ? strrchr(filepath, '.') : NULL;

    /* Check magic signatures first */
    if (header && header_len >= 4) {
        if (memcmp(header, "CURV", 4) == 0) return VECTOR_FMT_DAT;
        if (memcmp(header, "8BPS", 4) == 0) return VECTOR_FMT_PSD;
        if (memcmp(header, "%PDF", 4) == 0) return VECTOR_FMT_PDF;
        if (memcmp(header, "%!PS", 4) == 0) return VECTOR_FMT_AI;
        if (header[0] == 0x00 && header[1] == 0xFF && header[2] == 'K' && header[3] == 'A') return VECTOR_FMT_AFFINITY;
    }

    if (ext) {
        if (_stricmp(ext, ".dat") == 0) return VECTOR_FMT_DAT;
        if (_stricmp(ext, ".svg") == 0) return VECTOR_FMT_SVG;
        if (_stricmp(ext, ".ai") == 0 || _stricmp(ext, ".eps") == 0) return VECTOR_FMT_AI;
        if (_stricmp(ext, ".psd") == 0) return VECTOR_FMT_PSD;
        if (_stricmp(ext, ".pdf") == 0) return VECTOR_FMT_PDF;
        if (_stricmp(ext, ".af") == 0 || _stricmp(ext, ".afdesign") == 0 ||
            _stricmp(ext, ".afphoto") == 0 || _stricmp(ext, ".afpub") == 0) {
            return VECTOR_FMT_AFFINITY;
        }
    }

    /* Check text patterns */
    if (header && header_len > 10) {
        if (memchr(header, '<', header_len) != NULL) {
            const char *p = (const char*)header;
            if (strstr(p, "<svg") || strstr(p, "<SVG") || strstr(p, "xmlns")) return VECTOR_FMT_SVG;
        }
        if (strstr((const char*)header, "%%BoundingBox") || strstr((const char*)header, "%AI")) {
            return VECTOR_FMT_AI;
        }
    }

    return VECTOR_FMT_UNKNOWN;
}

/* ============================================================================
 * Adobe Illustrator / PostScript (.ai, .eps, PDF stream) Curve Parser
 * Handles Photoshop "Paths to Illustrator", Illustrator PostScript & PDF streams.
 * ============================================================================ */
static void extract_ps_bounding_box(const char *ps_text, size_t len, float *out_llx, float *out_lly, float *out_urx, float *out_ury) {
    *out_llx = 0.0f; *out_lly = 0.0f;
    *out_urx = 640.0f; *out_ury = 480.0f;

    /* 1. Try %%HiResBoundingBox: llx lly urx ury */
    const char *hb = strstr(ps_text, "%%HiResBoundingBox:");
    if (hb) {
        float x1, y1, x2, y2;
        if (sscanf(hb + 19, "%f %f %f %f", &x1, &y1, &x2, &y2) == 4 && (x2 > x1) && (y2 > y1)) {
            *out_llx = x1; *out_lly = y1; *out_urx = x2; *out_ury = y2;
            return;
        }
    }

    /* 2. Try %AI3_Cropmarks: llx lly urx ury */
    const char *cm = strstr(ps_text, "%AI3_Cropmarks:");
    if (cm) {
        float x1, y1, x2, y2;
        if (sscanf(cm + 15, "%f %f %f %f", &x1, &y1, &x2, &y2) == 4 && (x2 > x1) && (y2 > y1)) {
            *out_llx = x1; *out_lly = y1; *out_urx = x2; *out_ury = y2;
            return;
        }
    }

    /* 3. Try %%BoundingBox: llx lly urx ury */
    const char *bb = strstr(ps_text, "%%BoundingBox:");
    if (bb) {
        float x1, y1, x2, y2;
        if (sscanf(bb + 14, "%f %f %f %f", &x1, &y1, &x2, &y2) == 4 && (x2 > x1) && (y2 > y1)) {
            *out_llx = x1; *out_lly = y1; *out_urx = x2; *out_ury = y2;
            return;
        }
    }

    /* 4. Try PDF /MediaBox [llx lly urx ury] */
    const char *mb = strstr(ps_text, "/MediaBox");
    if (!mb) mb = strstr(ps_text, "/CropBox");
    if (mb) {
        const char *brk = strchr(mb, '[');
        if (brk) {
            float x1, y1, x2, y2;
            if (sscanf(brk + 1, "%f %f %f %f", &x1, &y1, &x2, &y2) == 4 && (x2 > x1) && (y2 > y1)) {
                *out_llx = x1; *out_lly = y1; *out_urx = x2; *out_ury = y2;
                return;
            }
        }
    }
}

static bool parse_ai_postscript_paths(
    const char *text, size_t length,
    BezierPath *out_paths, int max_paths, int *out_count,
    SvgDimensions *out_dims
) {
    if (!text || length == 0) return false;

    float llx = 0.0f, lly = 0.0f, urx = 640.0f, ury = 480.0f;
    extract_ps_bounding_box(text, length, &llx, &lly, &urx, &ury);

    if (out_dims) {
        out_dims->view_w = urx - llx;
        out_dims->view_h = ury - lly;
        out_dims->has_viewbox = true;
    }

    VectorMatrix matrix_stack[64];
    int matrix_sp = 0;
    VectorMatrix cur_matrix = vec_matrix_identity();

    BezierPath cur_path;
    bezier_path_init(&cur_path, "path");
    int initial_count = *out_count;

    float args[16];
    int argc = 0;

    const char *p = text;
    const char *end = text + length;

    while (p < end) {
        /* Skip whitespace */
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end) break;

        /* Skip PostScript comments % ... except bounding boxes */
        if (*p == '%') {
            while (p < end && *p != '\r' && *p != '\n') p++;
            continue;
        }

        /* Read token */
        const char *token_start = p;
        if (*p == '[' || *p == ']') {
            p++;
            continue;
        }

        while (p < end && !isspace((unsigned char)*p) && *p != '[' && *p != ']' && *p != '%') {
            p++;
        }
        size_t tlen = (size_t)(p - token_start);
        if (tlen == 0) continue;

        char tok[64];
        if (tlen >= sizeof(tok)) tlen = sizeof(tok) - 1;
        memcpy(tok, token_start, tlen);
        tok[tlen] = '\0';

        /* Check if number */
        char *endptr = NULL;
        float val = strtof(tok, &endptr);
        if (endptr && *endptr == '\0' && endptr != tok) {
            if (argc < 16) {
                args[argc++] = val;
            }
            continue;
        }

        /* Operator Dispatch */
        if (strcmp(tok, "cm") == 0 || strcmp(tok, "concat") == 0) {
            if (argc >= 6) {
                VectorMatrix m;
                m.a = args[argc-6]; m.b = args[argc-5];
                m.c = args[argc-4]; m.d = args[argc-3];
                m.e = args[argc-2]; m.f = args[argc-1];
                cur_matrix = vec_matrix_multiply(cur_matrix, m);
            }
            argc = 0;
        } else if (strcmp(tok, "q") == 0) {
            if (matrix_sp < 64) {
                matrix_stack[matrix_sp++] = cur_matrix;
            }
            argc = 0;
        } else if (strcmp(tok, "Q") == 0) {
            if (matrix_sp > 0) {
                cur_matrix = matrix_stack[--matrix_sp];
            }
            argc = 0;
        } else if (strcmp(tok, "m") == 0 || strcmp(tok, "moveto") == 0) {
            if (argc >= 2) {
                /* If current path has at least 2 nodes, commit it */
                if (cur_path.node_count >= 2) {
                    if (*out_count < max_paths) {
                        path_cache_invalidate(&cur_path);
                        path_cache_update(&cur_path);
                        out_paths[(*out_count)++] = cur_path;
                    }
                    bezier_path_init(&cur_path, "path");
                } else if (cur_path.node_count == 1) {
                    /* Reset single uncommitted node */
                    bezier_path_free(&cur_path);
                    bezier_path_init(&cur_path, "path");
                }

                float raw_x = args[argc-2];
                float raw_y = args[argc-1];
                float tx, ty;
                vec_matrix_transform_point(cur_matrix, raw_x, raw_y, &tx, &ty);

                /* PostScript Y is bottom-up: canvas_y = ury - ty */
                float cx = tx - llx;
                float cy = ury - ty;

                append_node(&cur_path, cx, cy, NODE_CORNER);
            }
            argc = 0;
        } else if (strcmp(tok, "l") == 0 || strcmp(tok, "lineto") == 0) {
            if (argc >= 2 && cur_path.node_count > 0) {
                float raw_x = args[argc-2];
                float raw_y = args[argc-1];
                float tx, ty;
                vec_matrix_transform_point(cur_matrix, raw_x, raw_y, &tx, &ty);

                float cx = tx - llx;
                float cy = ury - ty;

                /* Ensure previous node has no outgoing handle */
                cur_path.nodes[cur_path.node_count - 1].hout_x = 0.0f;
                cur_path.nodes[cur_path.node_count - 1].hout_y = 0.0f;

                BezierNode *n = append_node(&cur_path, cx, cy, NODE_CORNER);
                if (n) {
                    n->hin_x = 0.0f;
                    n->hin_y = 0.0f;
                }
            }
            argc = 0;
        } else if (strcmp(tok, "c") == 0 || strcmp(tok, "curveto") == 0) {
            if (argc >= 6 && cur_path.node_count > 0) {
                float c1_rx = args[argc-6], c1_ry = args[argc-5];
                float c2_rx = args[argc-4], c2_ry = args[argc-3];
                float p3_rx = args[argc-2], p3_ry = args[argc-1];

                float c1_tx, c1_ty, c2_tx, c2_ty, p3_tx, p3_ty;
                vec_matrix_transform_point(cur_matrix, c1_rx, c1_ry, &c1_tx, &c1_ty);
                vec_matrix_transform_point(cur_matrix, c2_rx, c2_ry, &c2_tx, &c2_ty);
                vec_matrix_transform_point(cur_matrix, p3_rx, p3_ry, &p3_tx, &p3_ty);

                float c1_cx = c1_tx - llx, c1_cy = ury - c1_ty;
                float c2_cx = c2_tx - llx, c2_cy = ury - c2_ty;
                float p3_cx = p3_tx - llx, p3_cy = ury - p3_ty;

                /* Connect outgoing handle on previous anchor */
                BezierNode *prev = &cur_path.nodes[cur_path.node_count - 1];
                prev->hout_x = c1_cx - prev->x;
                prev->hout_y = c1_cy - prev->y;
                if (prev->type == NODE_CORNER && (prev->hin_x != 0.0f || prev->hin_y != 0.0f)) {
                    prev->type = NODE_SMOOTH;
                }

                /* Add new end anchor with incoming handle */
                BezierNode *n = append_node(&cur_path, p3_cx, p3_cy, NODE_SMOOTH);
                if (n) {
                    n->hin_x = c2_cx - p3_cx;
                    n->hin_y = c2_cy - p3_cy;
                    n->hout_x = 0.0f;
                    n->hout_y = 0.0f;
                }
            }
            argc = 0;
        } else if (strcmp(tok, "v") == 0) {
            /* First control point coincides with current point */
            if (argc >= 4 && cur_path.node_count > 0) {
                float c2_rx = args[argc-4], c2_ry = args[argc-3];
                float p3_rx = args[argc-2], p3_ry = args[argc-1];

                float c2_tx, c2_ty, p3_tx, p3_ty;
                vec_matrix_transform_point(cur_matrix, c2_rx, c2_ry, &c2_tx, &c2_ty);
                vec_matrix_transform_point(cur_matrix, p3_rx, p3_ry, &p3_tx, &p3_ty);

                float c2_cx = c2_tx - llx, c2_cy = ury - c2_ty;
                float p3_cx = p3_tx - llx, p3_cy = ury - p3_ty;

                BezierNode *prev = &cur_path.nodes[cur_path.node_count - 1];
                prev->hout_x = 0.0f;
                prev->hout_y = 0.0f;

                BezierNode *n = append_node(&cur_path, p3_cx, p3_cy, NODE_SMOOTH);
                if (n) {
                    n->hin_x = c2_cx - p3_cx;
                    n->hin_y = c2_cy - p3_cy;
                }
            }
            argc = 0;
        } else if (strcmp(tok, "y") == 0) {
            /* Second control point coincides with endpoint */
            if (argc >= 4 && cur_path.node_count > 0) {
                float c1_rx = args[argc-4], c1_ry = args[argc-3];
                float p3_rx = args[argc-2], p3_ry = args[argc-1];

                float c1_tx, c1_ty, p3_tx, p3_ty;
                vec_matrix_transform_point(cur_matrix, c1_rx, c1_ry, &c1_tx, &c1_ty);
                vec_matrix_transform_point(cur_matrix, p3_rx, p3_ry, &p3_tx, &p3_ty);

                float c1_cx = c1_tx - llx, c1_cy = ury - c1_ty;
                float p3_cx = p3_tx - llx, p3_cy = ury - p3_ty;

                BezierNode *prev = &cur_path.nodes[cur_path.node_count - 1];
                prev->hout_x = c1_cx - prev->x;
                prev->hout_y = c1_cy - prev->y;

                BezierNode *n = append_node(&cur_path, p3_cx, p3_cy, NODE_CORNER);
                if (n) {
                    n->hin_x = 0.0f;
                    n->hin_y = 0.0f;
                }
            }
            argc = 0;
        } else if (strcmp(tok, "re") == 0) {
            /* PDF rectangle operator: x y w h re */
            if (argc >= 4) {
                float rx = args[argc-4], ry = args[argc-3], rw = args[argc-2], rh = args[argc-1];
                if (cur_path.node_count >= 2) {
                    if (*out_count < max_paths) {
                        path_cache_invalidate(&cur_path);
                        path_cache_update(&cur_path);
                        out_paths[(*out_count)++] = cur_path;
                    }
                    bezier_path_init(&cur_path, "rect");
                }
                float x1, y1, x2, y2, x3, y3, x4, y4;
                vec_matrix_transform_point(cur_matrix, rx, ry, &x1, &y1);
                vec_matrix_transform_point(cur_matrix, rx + rw, ry, &x2, &y2);
                vec_matrix_transform_point(cur_matrix, rx + rw, ry + rh, &x3, &y3);
                vec_matrix_transform_point(cur_matrix, rx, ry + rh, &x4, &y4);

                append_node(&cur_path, x1 - llx, ury - y1, NODE_CORNER);
                append_node(&cur_path, x2 - llx, ury - y2, NODE_CORNER);
                append_node(&cur_path, x3 - llx, ury - y3, NODE_CORNER);
                append_node(&cur_path, x4 - llx, ury - y4, NODE_CORNER);
            }
            argc = 0;
        } else if (strcmp(tok, "h") == 0 || strcmp(tok, "closepath") == 0) {
            if (cur_path.node_count >= 2) {
                /* If endpoint isn't close to start node, connect to close loop */
                BezierNode *first = &cur_path.nodes[0];
                BezierNode *last = &cur_path.nodes[cur_path.node_count - 1];
                float dx = first->x - last->x;
                float dy = first->y - last->y;
                if (sqrtf(dx*dx + dy*dy) > 0.5f) {
                    append_node(&cur_path, first->x, first->y, NODE_CORNER);
                }
            }
            argc = 0;
        } else if (strcmp(tok, "S") == 0 || strcmp(tok, "s") == 0 ||
                   strcmp(tok, "F") == 0 || strcmp(tok, "f") == 0 || strcmp(tok, "f*") == 0 ||
                   strcmp(tok, "B") == 0 || strcmp(tok, "b") == 0 || strcmp(tok, "b*") == 0 ||
                   strcmp(tok, "N") == 0 || strcmp(tok, "n") == 0) {
            /* Path stroke/fill/terminator */
            if (cur_path.node_count >= 2) {
                if (*out_count < max_paths) {
                    path_cache_invalidate(&cur_path);
                    path_cache_update(&cur_path);
                    out_paths[(*out_count)++] = cur_path;
                }
                bezier_path_init(&cur_path, "path");
            } else if (cur_path.node_count > 0) {
                bezier_path_free(&cur_path);
                bezier_path_init(&cur_path, "path");
            }
            argc = 0;
        } else {
            /* Unknown operator or state setting, reset argument stack */
            argc = 0;
        }
    }

    if (cur_path.node_count >= 2) {
        if (*out_count < max_paths) {
            path_cache_invalidate(&cur_path);
            path_cache_update(&cur_path);
            out_paths[(*out_count)++] = cur_path;
        }
    } else if (cur_path.node_count > 0) {
        bezier_path_free(&cur_path);
    }

    return (*out_count > initial_count);
}

/* ============================================================================
 * Adobe Photoshop Document (.psd) Vector Path Extractor
 * Reads Pen Tool WorkPaths & Vector Mask knots directly from Image Resources.
 * ============================================================================ */
static bool parse_psd_paths(
    const uint8_t *data, size_t len,
    BezierPath *out_paths, int max_paths, int *out_count,
    SvgDimensions *out_dims
) {
    if (len < 30 || memcmp(data, "8BPS", 4) != 0) return false;

    uint32_t height = ((uint32_t)data[14] << 24) | ((uint32_t)data[15] << 16) | ((uint32_t)data[16] << 8) | (uint32_t)data[17];
    uint32_t width  = ((uint32_t)data[18] << 24) | ((uint32_t)data[19] << 16) | ((uint32_t)data[20] << 8) | (uint32_t)data[21];
    if (width == 0 || height == 0) return false;

    if (out_dims) {
        out_dims->view_w = (float)width;
        out_dims->view_h = (float)height;
        out_dims->has_viewbox = true;
    }

    size_t pos = 26;
    if (pos + 4 > len) return false;
    uint32_t color_mode_len = ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos+1] << 16) | ((uint32_t)data[pos+2] << 8) | (uint32_t)data[pos+3];
    pos += 4 + color_mode_len;

    if (pos + 4 > len) return false;
    uint32_t res_len = ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos+1] << 16) | ((uint32_t)data[pos+2] << 8) | (uint32_t)data[pos+3];
    pos += 4;
    size_t res_end = pos + res_len;
    if (res_end > len) res_end = len;

    int initial_count = *out_count;

    while (pos + 12 <= res_end) {
        if (memcmp(data + pos, "8BIM", 4) != 0) {
            pos++;
            continue;
        }
        pos += 4;

        uint16_t id = ((uint16_t)data[pos] << 8) | (uint16_t)data[pos+1];
        pos += 2;

        /* Pascal string name */
        uint8_t name_len = data[pos++];
        char res_name[64] = {0};
        size_t copy_name_len = (name_len < sizeof(res_name) - 1) ? name_len : (sizeof(res_name) - 1);
        memcpy(res_name, data + pos, copy_name_len);
        pos += name_len;
        if ((name_len + 1) % 2 != 0) pos++; /* 2-byte word padding */

        if (pos + 4 > res_end) break;
        uint32_t block_size = ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos+1] << 16) | ((uint32_t)data[pos+2] << 8) | (uint32_t)data[pos+3];
        pos += 4;
        size_t block_end = pos + block_size;
        if (block_end > res_end) block_end = res_end;

        /* Resource ID 2000 is WorkPath, 2001-2998 are saved vector paths */
        if (id >= 2000 && id <= 2998 && block_size >= 26) {
            BezierPath cur_path;
            const char *pname = res_name[0] ? res_name : (id == 2000 ? "WorkPath" : "Path");
            bezier_path_init(&cur_path, pname);
            bool in_subpath = false;

            size_t rec_pos = pos;
            while (rec_pos + 26 <= block_end) {
                int16_t selector = (int16_t)(((uint16_t)data[rec_pos] << 8) | (uint16_t)data[rec_pos+1]);

                if (selector == 0 || selector == 3) {
                    /* Subpath length record (0=closed, 3=open) */
                    if (in_subpath && cur_path.node_count >= 2) {
                        if (*out_count < max_paths) {
                            path_cache_invalidate(&cur_path);
                            path_cache_update(&cur_path);
                            out_paths[(*out_count)++] = cur_path;
                        }
                        bezier_path_init(&cur_path, pname);
                    }
                    in_subpath = true;
                } else if (selector == 1 || selector == 2 || selector == 4 || selector == 5) {
                    /* Knot record: 24 bytes following selector */
                    int32_t prec_y = (int32_t)(((uint32_t)data[rec_pos+2] << 24) | ((uint32_t)data[rec_pos+3] << 16) | ((uint32_t)data[rec_pos+4] << 8) | (uint32_t)data[rec_pos+5]);
                    int32_t prec_x = (int32_t)(((uint32_t)data[rec_pos+6] << 24) | ((uint32_t)data[rec_pos+7] << 16) | ((uint32_t)data[rec_pos+8] << 8) | (uint32_t)data[rec_pos+9]);
                    int32_t anch_y = (int32_t)(((uint32_t)data[rec_pos+10] << 24) | ((uint32_t)data[rec_pos+11] << 16) | ((uint32_t)data[rec_pos+12] << 8) | (uint32_t)data[rec_pos+13]);
                    int32_t anch_x = (int32_t)(((uint32_t)data[rec_pos+14] << 24) | ((uint32_t)data[rec_pos+15] << 16) | ((uint32_t)data[rec_pos+16] << 8) | (uint32_t)data[rec_pos+17]);
                    int32_t leav_y = (int32_t)(((uint32_t)data[rec_pos+18] << 24) | ((uint32_t)data[rec_pos+19] << 16) | ((uint32_t)data[rec_pos+20] << 8) | (uint32_t)data[rec_pos+21]);
                    int32_t leav_x = (int32_t)(((uint32_t)data[rec_pos+22] << 24) | ((uint32_t)data[rec_pos+23] << 16) | ((uint32_t)data[rec_pos+24] << 8) | (uint32_t)data[rec_pos+25]);

                    /* Coordinates are 32-bit fixed point with 24-bit fraction, normalized to image dimensions */
                    double k_norm = 1.0 / 16777216.0;
                    float ax = (float)((double)anch_x * k_norm * (double)width);
                    float ay = (float)((double)anch_y * k_norm * (double)height);
                    float px = (float)((double)prec_x * k_norm * (double)width);
                    float py = (float)((double)prec_y * k_norm * (double)height);
                    float lx = (float)((double)leav_x * k_norm * (double)width);
                    float ly = (float)((double)leav_y * k_norm * (double)height);

                    BezierNodeType ntype = (selector == 1 || selector == 4) ? NODE_SMOOTH : NODE_CUSP;
                    float hin_x = px - ax;
                    float hin_y = py - ay;
                    float hout_x = lx - ax;
                    float hout_y = ly - ay;

                    if (fabsf(hin_x) < 0.001f && fabsf(hin_y) < 0.001f &&
                        fabsf(hout_x) < 0.001f && fabsf(hout_y) < 0.001f) {
                        ntype = NODE_CORNER;
                        hin_x = 0.0f; hin_y = 0.0f; hout_x = 0.0f; hout_y = 0.0f;
                    }

                    BezierNode *n = append_node(&cur_path, ax, ay, ntype);
                    if (n) {
                        n->hin_x = hin_x;
                        n->hin_y = hin_y;
                        n->hout_x = hout_x;
                        n->hout_y = hout_y;
                    }
                }

                rec_pos += 26;
            }

            if (cur_path.node_count >= 2) {
                if (*out_count < max_paths) {
                    path_cache_invalidate(&cur_path);
                    path_cache_update(&cur_path);
                    out_paths[(*out_count)++] = cur_path;
                }
            } else {
                bezier_path_free(&cur_path);
            }
        }

        pos = block_end + (block_size % 2);
    }

    return (*out_count > initial_count);
}

/* ============================================================================
 * Serif Affinity (.af, .afdesign, .afphoto, .afpub) Native Curve Parser
 * Parses proprietary Zstandard-compressed compound document archives, extracting
 * PolyCurve ("PCrv" / "Crvs") 18-byte knot records and 2D transformation matrices.
 * ============================================================================ */
#ifdef HAVE_ZSTD
static bool parse_affinity_paths(
    const uint8_t *data, size_t length,
    BezierPath *out_paths, int max_paths, int *out_count,
    SvgDimensions *out_dims
) {
    if (!data || length < 64 || !out_paths || !out_count) return false;
    int initial_count = *out_count;

    /* Verify Affinity compound document magic: 00 FF 4B 41 ("\0\xFFKA") */
    if (data[0] != 0x00 || data[1] != 0xFF || data[2] != 'K' || data[3] != 'A') {
        return false;
    }

    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    if (!dctx) return false;

    uint8_t *latest_dom = NULL;
    size_t latest_dom_len = 0;

    /* Scan for Zstandard frames, retaining the latest active DOM frame (starts with "sreP" / Persona) */
    for (size_t i = 0; i + 4 < length; ++i) {
        if (data[i] == 0x28 && data[i+1] == 0xB5 && data[i+2] == 0x2F && data[i+3] == 0xFD) {
            ZSTD_inBuffer in = { data + i, length - i, 0 };
            size_t out_cap = 4 * 1024 * 1024;
            uint8_t *out_buf = (uint8_t*)malloc(out_cap);
            if (!out_buf) continue;
            ZSTD_outBuffer out = { out_buf, out_cap, 0 };

            ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only);
            size_t ret = ZSTD_decompressStream(dctx, &out, &in);
            if (!ZSTD_isError(ret) && out.pos > 100) {
                bool is_dom = false;
                for (size_t k = 0; k < 64 && k + 4 < out.pos; ++k) {
                    if (memcmp(out_buf + k, "sreP", 4) == 0) {
                        is_dom = true;
                        break;
                    }
                }

                if (is_dom) {
                    if (latest_dom) free(latest_dom);
                    latest_dom = out_buf;
                    latest_dom_len = out.pos;
                    out_buf = NULL;
                }
            }
            if (out_buf) free(out_buf);
        }
    }

    ZSTD_freeDCtx(dctx);

    if (!latest_dom || latest_dom_len == 0) {
        if (latest_dom) free(latest_dom);
        return false;
    }

    /* Detect Artboard / Canvas dimensions */
    float doc_w = 640.0f, doc_h = 480.0f;
    bool found_dim = false;
    for (size_t i = 0; i + 32 < latest_dom_len; ++i) {
        if (memcmp(latest_dom + i, "nArT", 4) == 0 || memcmp(latest_dom + i, "+cseD", 5) == 0) {
            size_t start_scan = (i > 128 ? i - 128 : 0);
            for (size_t p = start_scan; p + 16 < i + 256 && p + 16 < latest_dom_len; ++p) {
                double w_val, h_val;
                memcpy(&w_val, latest_dom + p, 8);
                memcpy(&h_val, latest_dom + p + 8, 8);
                if (w_val >= 100.0 && w_val <= 8000.0 && h_val >= 100.0 && h_val <= 8000.0) {
                    if (w_val == (double)(int)w_val && h_val == (double)(int)h_val) {
                        doc_w = (float)w_val;
                        doc_h = (float)h_val;
                        found_dim = true;
                        break;
                    }
                }
            }
            if (found_dim) break;
        }
    }

    if (out_dims) {
        out_dims->view_w = doc_w;
        out_dims->view_h = doc_h;
        out_dims->has_viewbox = found_dim;
    }

    /* Parse all curve objects: search for "svrC\x01DvCP" ("Crvs \x01 PCvD") */
    for (size_t i = 0; i + 32 < latest_dom_len; ++i) {
        if (memcmp(latest_dom + i, "svrC", 4) == 0 && latest_dom[i+4] == 0x01 && memcmp(latest_dom + i + 5, "DvCP", 4) == 0) {
            if (*out_count >= max_paths) break;

            /* 1. Nearest name before this curve */
            char name[64] = "curve";
            size_t back_limit = i > 4096 ? i - 4096 : 0;
            for (size_t b = i; b > back_limit; --b) {
                if (memcmp(latest_dom + b, "+cseD", 5) == 0) {
                    uint32_t nlen = latest_dom[b+5] | (latest_dom[b+6] << 8) | (latest_dom[b+7] << 16) | (latest_dom[b+8] << 24);
                    if (nlen > 0 && nlen < 60 && b + 9 + nlen <= latest_dom_len) {
                        memcpy(name, latest_dom + b + 9, nlen);
                        name[nlen] = '\0';
                        break;
                    }
                }
            }

            /* 2. Nearest 2D transform matrix before this curve: "(mrfX" ("Xfrm(") */
            VectorMatrix cur_mat = vec_matrix_identity();
            size_t xf_limit = i > 2048 ? i - 2048 : 0;
            for (size_t b = i; b > xf_limit; --b) {
                if (memcmp(latest_dom + b, "(mrfX", 5) == 0) {
                    size_t mp = b + 5;
                    if (mp + 48 <= i) {
                        double a, c, tx, b_val, d, ty;
                        memcpy(&a, latest_dom + mp, 8);
                        memcpy(&c, latest_dom + mp + 8, 8);
                        memcpy(&tx, latest_dom + mp + 16, 8);
                        memcpy(&b_val, latest_dom + mp + 24, 8);
                        memcpy(&d, latest_dom + mp + 32, 8);
                        memcpy(&ty, latest_dom + mp + 40, 8);
                        if (!isnan(a) && !isnan(b_val) && !isnan(c) && !isnan(d) && !isnan(tx) && !isnan(ty)) {
                            cur_mat.a = (float)a;
                            cur_mat.b = (float)b_val;
                            cur_mat.c = (float)c;
                            cur_mat.d = (float)d;
                            cur_mat.e = (float)tx;
                            cur_mat.f = (float)ty;
                            break;
                        }
                    }
                }
            }

            /* 3. Locate points: look for "&BsvC" (end of subpath) */
            size_t end_p = i;
            while (end_p + 5 < latest_dom_len && memcmp(latest_dom + end_p, "&BsvC", 5) != 0) end_p++;
            if (end_p + 5 >= latest_dom_len) continue;

            size_t pt_end = end_p - 2;
            size_t start_p = i;
            while (start_p + 16 < end_p && memcmp(latest_dom + start_p, "0ataD", 5) != 0) start_p++;
            if (start_p + 16 >= end_p) continue;
            start_p += 5;

            /* Locate the first valid point */
            size_t p = start_p;
            while (p + 18 <= pt_end) {
                uint16_t flag = latest_dom[p+16] | (latest_dom[p+17] << 8);
                if (flag == 0x0200 || flag == 0x0002) {
                    double x, y;
                    memcpy(&x, latest_dom + p, 8);
                    memcpy(&y, latest_dom + p + 8, 8);
                    if (!isnan(x) && !isnan(y) && fabs(x) < 50000.0 && fabs(y) < 50000.0) {
                        break;
                    }
                }
                p++;
            }

            if (p + 18 > pt_end) continue;

            /* Initialize new BezierPath */
            BezierPath path;
            char path_name[64];
            snprintf(path_name, sizeof(path_name), "%s-%d", name[0] ? name : "curve", *out_count + 1);
            bezier_path_init(&path, path_name);

            /* Parse knots in triplets of 18 bytes each:
             * Point 0: flag 0x0200 = Control point 1 (incoming handle in world space)
             * Point 1: flag 0x0002 / 0x0001 = Anchor point
             * Point 2: flag 0x0100 = Control point 2 (outgoing handle in world space)
             */
            for (size_t cur = p; cur + 18 <= pt_end; cur += 54) {
                if (cur + 18 > pt_end) break;

                double raw_in_x = 0, raw_in_y = 0;
                double raw_ax = 0, raw_ay = 0;
                double raw_out_x = 0, raw_out_y = 0;
                uint16_t in_flag = 0, a_flag = 0, out_flag = 0;

                /* Point 0: InHandle */
                memcpy(&raw_in_x, latest_dom + cur, 8);
                memcpy(&raw_in_y, latest_dom + cur + 8, 8);
                in_flag = latest_dom[cur+16] | (latest_dom[cur+17] << 8);

                /* Point 1: Anchor */
                if (cur + 36 <= pt_end) {
                    memcpy(&raw_ax, latest_dom + cur + 18, 8);
                    memcpy(&raw_ay, latest_dom + cur + 26, 8);
                    a_flag = latest_dom[cur+34] | (latest_dom[cur+35] << 8);
                } else {
                    raw_ax = raw_in_x; raw_ay = raw_in_y;
                    a_flag = in_flag;
                }

                /* Point 2: OutHandle */
                if (cur + 54 <= pt_end) {
                    memcpy(&raw_out_x, latest_dom + cur + 36, 8);
                    memcpy(&raw_out_y, latest_dom + cur + 44, 8);
                    out_flag = latest_dom[cur+52] | (latest_dom[cur+53] << 8);
                }

                /* Apply affine transform matrix */
                float ax, ay, in_x, in_y, out_x, out_y;
                vec_matrix_transform_point(cur_mat, (float)raw_ax, (float)raw_ay, &ax, &ay);
                vec_matrix_transform_point(cur_mat, (float)raw_in_x, (float)raw_in_y, &in_x, &in_y);
                vec_matrix_transform_point(cur_mat, (float)raw_out_x, (float)raw_out_y, &out_x, &out_y);

                BezierNodeType ntype = (a_flag == 0x0002) ? NODE_SMOOTH : NODE_CORNER;
                BezierNode *n = append_node(&path, ax, ay, ntype);
                if (n) {
                    if (path.node_count > 1 && in_flag == 0x0200) {
                        n->hin_x = in_x - ax;
                        n->hin_y = in_y - ay;
                    } else {
                        n->hin_x = 0.0f;
                        n->hin_y = 0.0f;
                    }

                    if (out_flag == 0x0100) {
                        n->hout_x = out_x - ax;
                        n->hout_y = out_y - ay;
                    } else {
                        n->hout_x = 0.0f;
                        n->hout_y = 0.0f;
                    }
                }
            }

            if (path.node_count >= 2) {
                path_cache_invalidate(&path);
                path_cache_update(&path);
                out_paths[(*out_count)++] = path;
            } else {
                bezier_path_free(&path);
            }
        }
    }

    free(latest_dom);
    return (*out_count > initial_count);
}
#endif

/* ============================================================================
 * Unified Vector Curve Importer (AI, PSD, PDF, SVG, Affinity)
 * ============================================================================ */
bool vector_path_import_from_memory(
    const uint8_t *data,
    size_t length,
    const char *hint_ext,
    BezierPath *out_paths,
    int max_paths,
    int *out_count,
    SvgDimensions *out_dims
) {
    if (!data || length == 0 || !out_paths || !out_count) return false;

    VectorFileFormat fmt = vector_path_detect_format(hint_ext, data, length > 256 ? 256 : length);

    /* 1. Try Affinity Designer / Photo / Publisher Native File (.af, .afdesign, .afphoto, .afpub) */
    if (fmt == VECTOR_FMT_AFFINITY || (length >= 4 && data[0] == 0x00 && data[1] == 0xFF && data[2] == 'K' && data[3] == 'A')) {
#ifdef HAVE_ZSTD
        if (parse_affinity_paths(data, length, out_paths, max_paths, out_count, out_dims)) {
            return true;
        }
#endif
        /* Fallback: scan for embedded PDF or SVG streams if any */
        for (size_t i = 0; i + 1024 < length; ++i) {
            if (data[i] == '%' && memcmp(data + i, "%PDF-", 5) == 0) {
                if (parse_ai_postscript_paths((const char*)(data + i), length - i, out_paths, max_paths, out_count, out_dims)) {
                    return true;
                }
            }
        }
        for (size_t i = 0; i + 1024 < length; ++i) {
            if (data[i] == '<' && (memcmp(data + i, "<svg", 4) == 0 || memcmp(data + i, "<SVG", 4) == 0)) {
                if (svg_path_import_from_memory((const char*)(data + i), length - i, out_paths, max_paths, out_count, out_dims)) {
                    return true;
                }
            }
        }
    }

    /* 2. Try Adobe Photoshop PSD */
    if (fmt == VECTOR_FMT_PSD || (length >= 4 && memcmp(data, "8BPS", 4) == 0)) {
        if (parse_psd_paths(data, length, out_paths, max_paths, out_count, out_dims)) {
            return true;
        }
    }

    /* 3. Try SVG directly if SVG format */
    if (fmt == VECTOR_FMT_SVG) {
        if (svg_path_import_from_memory((const char*)data, length, out_paths, max_paths, out_count, out_dims)) {
            return true;
        }
    }

    /* 4. Try Adobe Illustrator PostScript / PDF Stream */
    if (fmt == VECTOR_FMT_AI || fmt == VECTOR_FMT_PDF ||
        memcmp(data, "%!PS", 4) == 0 || memcmp(data, "%PDF", 4) == 0 ||
        strstr((const char*)data, "%%BoundingBox") || strstr((const char*)data, "%AI")) {
        if (parse_ai_postscript_paths((const char*)data, length, out_paths, max_paths, out_count, out_dims)) {
            return true;
        }
    }

    /* 5. Fallback attempts: try SVG, then PostScript */
    if (svg_path_import_from_memory((const char*)data, length, out_paths, max_paths, out_count, out_dims)) {
        return true;
    }
    if (parse_ai_postscript_paths((const char*)data, length, out_paths, max_paths, out_count, out_dims)) {
        return true;
    }

    return false;
}

bool vector_path_import_from_file(
    const char *filepath,
    BezierPath *out_paths,
    int max_paths,
    int *out_count,
    SvgDimensions *out_dims
) {
    if (!filepath || !out_paths || !out_count) return false;
    *out_count = 0;

    FILE *f = fopen(filepath, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 4) {
        fclose(f);
        return false;
    }

    uint8_t *buf = (uint8_t*)malloc(fsize + 1);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t rd = fread(buf, 1, fsize, f);
    fclose(f);
    buf[rd] = '\0';

    const char *ext = strrchr(filepath, '.');
    bool ok = vector_path_import_from_memory(buf, rd, ext, out_paths, max_paths, out_count, out_dims);
    free(buf);
    return ok;
}

/* ============================================================================
 * Adobe Illustrator PostScript (.ai) Curve Exporter
 * Writes clean EPSF-3.0 Adobe Illustrator format compatible with Photoshop,
 * Illustrator, Affinity Designer, and Inkscape.
 * ============================================================================ */
bool ai_path_export_to_file(
    const char *filepath,
    const BezierPath *paths,
    int count,
    int canvas_w,
    int canvas_h
) {
    if (!filepath || !paths || count <= 0) return false;
    if (canvas_w <= 0) canvas_w = 640;
    if (canvas_h <= 0) canvas_h = 480;

    FILE *f = fopen(filepath, "w");
    if (!f) return false;

    const char *title = strrchr(filepath, '\\');
    if (!title) title = strrchr(filepath, '/');
    title = title ? title + 1 : filepath;

    fprintf(f, "%%!PS-Adobe-3.0 EPSF-3.0\n");
    fprintf(f, "%%%%Creator: ZuPather - Zuma Deluxe Path Editor\n");
    fprintf(f, "%%%%Title: (%s)\n", title);
    fprintf(f, "%%%%BoundingBox: 0 0 %d %d\n", canvas_w, canvas_h);
    fprintf(f, "%%%%HiResBoundingBox: 0.0000 0.0000 %.4f %.4f\n", (float)canvas_w, (float)canvas_h);
    fprintf(f, "%%AI3_Cropmarks: 0.0000 0.0000 %.4f %.4f\n", (float)canvas_w, (float)canvas_h);
    fprintf(f, "%%%%DocumentProcessColors: Black\n");
    fprintf(f, "%%%%EndComments\n");
    fprintf(f, "%%%%BeginProlog\n");
    fprintf(f, "%%%%EndProlog\n");
    fprintf(f, "%%%%BeginSetup\n");
    fprintf(f, "%%%%EndSetup\n");
    fprintf(f, "[1 0 0 1 0 0] cm\n");
    fprintf(f, "0 J 0 j 1 w 10 M []0 d\n");
    fprintf(f, "0 0 0 1 K\n"); /* Black stroke */

    for (int p = 0; p < count; ++p) {
        const BezierPath *path = &paths[p];
        if (!path || path->node_count < 2) continue;

        fprintf(f, "%%AI5_BeginLayer\n");
        fprintf(f, "1 1 1 1 0 0 -1 0 0 0 Lb\n");
        fprintf(f, "(%s) Ln\n", path->name[0] ? path->name : "Track");

        /* Node 0: moveto in PostScript bottom-up coordinates (Y = canvas_h - Y) */
        float start_x = path->nodes[0].x;
        float start_y = (float)canvas_h - path->nodes[0].y;
        fprintf(f, "%.4f %.4f m\n", start_x, start_y);

        for (int i = 0; i < path->node_count - 1; ++i) {
            const BezierNode *n0 = &path->nodes[i];
            const BezierNode *n1 = &path->nodes[i + 1];

            float c1_x = n0->x + n0->hout_x;
            float c1_y = (float)canvas_h - (n0->y + n0->hout_y);

            float c2_x = n1->x + n1->hin_x;
            float c2_y = (float)canvas_h - (n1->y + n1->hin_y);

            float p3_x = n1->x;
            float p3_y = (float)canvas_h - n1->y;

            fprintf(f, "%.4f %.4f %.4f %.4f %.4f %.4f c\n",
                    c1_x, c1_y, c2_x, c2_y, p3_x, p3_y);
        }

        fprintf(f, "S\n"); /* Stroke */
        fprintf(f, "%%AI5_EndLayer--\n");
    }

    fprintf(f, "%%%%Trailer\n");
    fprintf(f, "%%%%EOF\n");
    fclose(f);
    return true;
}
