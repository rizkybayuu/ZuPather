#include "canvas_render.h"

static void get_adaptive_grid_steps(float zoom, int *out_major, int *out_minor) {
    if (zoom <= 0.40f) {
        *out_major = 160;
        *out_minor = 80;
    } else if (zoom <= 0.75f) {
        *out_major = 80;
        *out_minor = 40;
    } else if (zoom <= 1.50f) {
        *out_major = 40;
        *out_minor = 20;
    } else if (zoom <= 3.00f) {
        *out_major = 20;
        *out_minor = 10;
    } else if (zoom <= 5.50f) {
        *out_major = 10;
        *out_minor = 5;
    } else {
        *out_major = 5;
        *out_minor = 1;
    }
}

static inline BYTE get_round_rect_alpha(int x, int y, int w, int h, int r, BYTE max_alpha) {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    int dx = 0, dy = 0;
    if (x < r && y < r) {
        dx = r - x; dy = r - y;
    } else if (x >= w - r && y < r) {
        dx = x - (w - r - 1); dy = r - y;
    } else if (x < r && y >= h - r) {
        dx = r - x; dy = y - (h - r - 1);
    } else if (x >= w - r && y >= h - r) {
        dx = x - (w - r - 1); dy = y - (h - r - 1);
    } else {
        return max_alpha;
    }
    int dist_sq = dx * dx + dy * dy;
    if (dist_sq <= (r - 1) * (r - 1)) return max_alpha;
    if (dist_sq <= r * r) return (BYTE)(max_alpha * 2 / 3);
    if (dist_sq <= (r + 1) * (r + 1)) return (BYTE)(max_alpha / 3);
    return 0;
}

/* Floating coordinate badge for hovered entities rendered with translucent alpha */
static void render_coord_badge_alpha(HDC hdc, int center_x, int top_y, const char *label, const ThemeColors *th, COLORREF border_col, COLORREF text_col, BYTE alpha) {
    if (!label || !th) return;

    SelectObject(hdc, g_font_mono_bold);
    SIZE sz;
    GetTextExtentPoint32A(hdc, label, (int)strlen(label), &sz);

    int badge_w = sz.cx + 16;
    int badge_h = sz.cy + 6;
    if (badge_h < 22) badge_h = 22;
    int badge_left = center_x - badge_w / 2;
    int badge_top = top_y;
    int r = 8;

    /* 32-bit DIB section for translucent badge */
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = badge_w;
    bmi.bmiHeader.biHeight = -badge_h; /* Top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *pvBits = NULL;
    HBITMAP badge_bm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    if (!badge_bm || !pvBits) return;

    HDC badge_dc = CreateCompatibleDC(hdc);
    HBITMAP old_bbm = (HBITMAP)SelectObject(badge_dc, badge_bm);

    /* Draw pill background & border */
    HBRUSH bg_br = CreateSolidBrush(th->bar_bg);
    HPEN border_pen = CreatePen(PS_SOLID, 1, border_col);
    HBRUSH old_br = (HBRUSH)SelectObject(badge_dc, bg_br);
    HPEN old_pen = (HPEN)SelectObject(badge_dc, border_pen);
    RoundRect(badge_dc, 0, 0, badge_w, badge_h, r, r);
    SelectObject(badge_dc, old_br);
    SelectObject(badge_dc, old_pen);
    DeleteObject(bg_br);
    DeleteObject(border_pen);

    /* Draw text */
    SelectObject(badge_dc, g_font_mono_bold);
    SetBkMode(badge_dc, TRANSPARENT);
    SetTextColor(badge_dc, text_col);
    RECT rc_text = { 0, 0, badge_w, badge_h };
    DrawTextA(badge_dc, label, -1, &rc_text, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    /* Apply per-pixel alpha blending with smooth corner anti-aliasing */
    DWORD *pixels = (DWORD *)pvBits;
    for (int y = 0; y < badge_h; ++y) {
        for (int x = 0; x < badge_w; ++x) {
            int idx = y * badge_w + x;
            BYTE a = get_round_rect_alpha(x, y, badge_w, badge_h, r, alpha);
            DWORD px = pixels[idx];
            BYTE b = (BYTE)(px & 0xFF);
            BYTE g = (BYTE)((px >> 8) & 0xFF);
            BYTE r_val = (BYTE)((px >> 16) & 0xFF);
            BYTE pb = (BYTE)((b * a) / 255);
            BYTE pg = (BYTE)((g * a) / 255);
            BYTE pr = (BYTE)((r_val * a) / 255);
            pixels[idx] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | pb;
        }
    }

    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    GdiAlphaBlend(hdc, badge_left, badge_top, badge_w, badge_h,
                  badge_dc, 0, 0, badge_w, badge_h, bf);

    SelectObject(badge_dc, old_bbm);
    DeleteObject(badge_bm);
    DeleteDC(badge_dc);
}

static void render_coord_badge_ex(HDC hdc, int center_x, int top_y, const char *label, const ThemeColors *th, COLORREF border_col, COLORREF text_col) {
    render_coord_badge_alpha(hdc, center_x, top_y, label, th, border_col, text_col, 128); /* 50% opacity */
}

static void render_coord_badge(HDC hdc, int center_x, int top_y, const char *label, const ThemeColors *th) {
    render_coord_badge_alpha(hdc, center_x, top_y, label, th, th->text_accent_blue, th->text_primary, 128); /* 50% opacity */
}

static void draw_topleft_corner_brackets(HDC hdc, int x, int y, int w, int h, COLORREF col) {
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    int arm = 10;
    if (arm > w / 3) arm = w / 3;
    if (arm > h / 3) arm = h / 3;
    if (arm < 4) arm = 4;
    /* Top-Left */
    MoveToEx(hdc, x, y, NULL); LineTo(hdc, x + arm, y);
    MoveToEx(hdc, x, y, NULL); LineTo(hdc, x, y + arm);
    /* Top-Right */
    MoveToEx(hdc, x + w, y, NULL); LineTo(hdc, x + w - arm, y);
    MoveToEx(hdc, x + w, y, NULL); LineTo(hdc, x + w, y + arm);
    /* Bottom-Left */
    MoveToEx(hdc, x, y + h, NULL); LineTo(hdc, x + arm, y + h);
    MoveToEx(hdc, x, y + h, NULL); LineTo(hdc, x, y + h - arm);
    /* Bottom-Right */
    MoveToEx(hdc, x + w, y + h, NULL); LineTo(hdc, x + w - arm, y + h);
    MoveToEx(hdc, x + w, y + h, NULL); LineTo(hdc, x + w, y + h - arm);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

static void draw_object_selection_indicator(HDC hdc, int x, int y, int w, int h, COLORREF col) {
    HPEN pen = CreatePen(PS_SOLID, 2, col);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    int arm = 12;
    if (arm > w / 3) arm = w / 3;
    if (arm > h / 3) arm = h / 3;
    if (arm < 4) arm = 4;
    /* Top-Left */
    MoveToEx(hdc, x, y, NULL); LineTo(hdc, x + arm, y);
    MoveToEx(hdc, x, y, NULL); LineTo(hdc, x, y + arm);
    /* Top-Right */
    MoveToEx(hdc, x + w, y, NULL); LineTo(hdc, x + w - arm, y);
    MoveToEx(hdc, x + w, y, NULL); LineTo(hdc, x + w, y + arm);
    /* Bottom-Left */
    MoveToEx(hdc, x, y + h, NULL); LineTo(hdc, x + arm, y + h);
    MoveToEx(hdc, x, y + h, NULL); LineTo(hdc, x, y + h - arm);
    /* Bottom-Right */
    MoveToEx(hdc, x + w, y + h, NULL); LineTo(hdc, x + w - arm, y + h);
    MoveToEx(hdc, x + w, y + h, NULL); LineTo(hdc, x + w, y + h - arm);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

void update_object_marquee_selection(float min_x, float min_y, float max_x, float max_y) {
    if (g_mode_overlay) {
        /* Cutouts */
        for (int i = 0; i < g_cutout_count; ++i) {
            int cw = g_cutouts[i].has_img ? g_cutouts[i].img.width : (g_has_default_cutout_img ? g_default_cutout_img.width : 160);
            int ch = g_cutouts[i].has_img ? g_cutouts[i].img.height : (g_has_default_cutout_img ? g_default_cutout_img.height : 140);
            float ox1 = (float)g_cutouts[i].x;
            float oy1 = (float)g_cutouts[i].y;
            float ox2 = ox1 + (float)cw;
            float oy2 = oy1 + (float)ch;
            bool inside = !(ox2 < min_x || ox1 > max_x || oy2 < min_y || oy1 > max_y);
            bool base = g_snap_cutout_selected[i];
            switch (g_obj_marquee_mode) {
                case MARQUEE_MODE_REPLACE:  g_cutouts[i].selected = inside; break;
                case MARQUEE_MODE_ADD:      g_cutouts[i].selected = base || inside; break;
                case MARQUEE_MODE_SUBTRACT: g_cutouts[i].selected = base && !inside; break;
                case MARQUEE_MODE_TOGGLE:   g_cutouts[i].selected = inside ? !base : base; break;
            }
        }
        /* Sprites */
        for (int i = 0; i < g_sprite_count; ++i) {
            int sw = g_sprites[i].has_img ? g_sprites[i].img.width : (g_has_default_sprite_img ? g_default_sprite_img.width : 120);
            int sh = g_sprites[i].has_img ? g_sprites[i].img.height : (g_has_default_sprite_img ? g_default_sprite_img.height : 120);
            float ox1 = (float)g_sprites[i].x;
            float oy1 = (float)g_sprites[i].y;
            float ox2 = ox1 + (float)sw;
            float oy2 = oy1 + (float)sh;
            bool inside = !(ox2 < min_x || ox1 > max_x || oy2 < min_y || oy1 > max_y);
            bool base = g_snap_sprite_selected[i];
            switch (g_obj_marquee_mode) {
                case MARQUEE_MODE_REPLACE:  g_sprites[i].selected = inside; break;
                case MARQUEE_MODE_ADD:      g_sprites[i].selected = base || inside; break;
                case MARQUEE_MODE_SUBTRACT: g_sprites[i].selected = base && !inside; break;
                case MARQUEE_MODE_TOGGLE:   g_sprites[i].selected = inside ? !base : base; break;
            }
        }
        /* BackgroundAlphas */
        for (int i = 0; i < g_bg_alpha_count; ++i) {
            int bw = g_bg_alphas[i].has_img ? g_bg_alphas[i].img.width : (g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 160);
            int bh = g_bg_alphas[i].has_img ? g_bg_alphas[i].img.height : (g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 140);
            float ox1 = (float)g_bg_alphas[i].x;
            float oy1 = (float)g_bg_alphas[i].y;
            float ox2 = ox1 + (float)bw;
            float oy2 = oy1 + (float)bh;
            bool inside = !(ox2 < min_x || ox1 > max_x || oy2 < min_y || oy1 > max_y);
            bool base = g_snap_bgalpha_selected[i];
            switch (g_obj_marquee_mode) {
                case MARQUEE_MODE_REPLACE:  g_bg_alphas[i].selected = inside; break;
                case MARQUEE_MODE_ADD:      g_bg_alphas[i].selected = base || inside; break;
                case MARQUEE_MODE_SUBTRACT: g_bg_alphas[i].selected = base && !inside; break;
                case MARQUEE_MODE_TOGGLE:   g_bg_alphas[i].selected = inside ? !base : base; break;
            }
        }
    } else if (g_mode_element) {
        /* Frog */
        if (g_has_frog) {
            int half_w = g_has_frog_img ? (g_frog_img.width / 2) : 54;
            int half_h = g_has_frog_img ? (g_frog_img.height / 2) : 54;
            float ox1 = (float)(g_frog_gx - half_w);
            float oy1 = (float)(g_frog_gy - half_h);
            float ox2 = (float)(g_frog_gx + half_w);
            float oy2 = (float)(g_frog_gy + half_h);
            bool inside = !(ox2 < min_x || ox1 > max_x || oy2 < min_y || oy1 > max_y);
            bool base = g_snap_frog_selected;
            switch (g_obj_marquee_mode) {
                case MARQUEE_MODE_REPLACE:  g_frog_selected = inside; break;
                case MARQUEE_MODE_ADD:      g_frog_selected = base || inside; break;
                case MARQUEE_MODE_SUBTRACT: g_frog_selected = base && !inside; break;
                case MARQUEE_MODE_TOGGLE:   g_frog_selected = inside ? !base : base; break;
            }
        }
        /* Coins */
        int coin_half_w = g_has_coin_img ? (g_coin_img.width / 2) : 18;
        int coin_half_h = g_has_coin_img ? (g_coin_img.height / 2) : 18;
        for (int i = 0; i < g_treasure_point_count; ++i) {
            float ox1 = (float)(g_treasure_points[i].x - coin_half_w);
            float oy1 = (float)(g_treasure_points[i].y - coin_half_h);
            float ox2 = (float)(g_treasure_points[i].x + coin_half_w);
            float oy2 = (float)(g_treasure_points[i].y + coin_half_h);
            bool inside = !(ox2 < min_x || ox1 > max_x || oy2 < min_y || oy1 > max_y);
            bool base = g_snap_coin_selected[i];
            switch (g_obj_marquee_mode) {
                case MARQUEE_MODE_REPLACE:  g_treasure_points[i].selected = inside; break;
                case MARQUEE_MODE_ADD:      g_treasure_points[i].selected = base || inside; break;
                case MARQUEE_MODE_SUBTRACT: g_treasure_points[i].selected = base && !inside; break;
                case MARQUEE_MODE_TOGGLE:   g_treasure_points[i].selected = inside ? !base : base; break;
            }
        }
    }
}

static void render_object_marquee_box(HDC mem_dc, const ThemeColors *th) {
    if (g_obj_drag_state != OBJ_DRAG_MARQUEE) return;

    int left = g_obj_marquee_start_x < g_obj_marquee_cur_x ? g_obj_marquee_start_x : g_obj_marquee_cur_x;
    int right = g_obj_marquee_start_x > g_obj_marquee_cur_x ? g_obj_marquee_start_x : g_obj_marquee_cur_x;
    int top = g_obj_marquee_start_y < g_obj_marquee_cur_y ? g_obj_marquee_start_y : g_obj_marquee_cur_y;
    int bottom = g_obj_marquee_start_y > g_obj_marquee_cur_y ? g_obj_marquee_start_y : g_obj_marquee_cur_y;
    int mw = right - left;
    int mh = bottom - top;
    if (mw > 0 && mh > 0) {
        COLORREF mq_col;
        switch (g_obj_marquee_mode) {
            case MARQUEE_MODE_ADD:
                mq_col = RGB(35, 185, 80);  /* Green for Add */
                break;
            case MARQUEE_MODE_SUBTRACT:
                mq_col = RGB(225, 55, 55);  /* Red for Deselect */
                break;
            case MARQUEE_MODE_TOGGLE:
                mq_col = RGB(175, 65, 220); /* Purple for Toggle */
                break;
            case MARQUEE_MODE_REPLACE:
            default:
                mq_col = th->text_accent_blue; /* Blue for Standard Replace */
                break;
        }

        HDC fill_dc = CreateCompatibleDC(mem_dc);
        HBITMAP fill_bm = CreateCompatibleBitmap(mem_dc, mw, mh);
        HBITMAP old_fbm = (HBITMAP)SelectObject(fill_dc, fill_bm);
        HBRUSH fill_br = CreateSolidBrush(mq_col);
        RECT rc_fill = {0, 0, mw, mh};
        FillRect(fill_dc, &rc_fill, fill_br);
        DeleteObject(fill_br);

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 48, 0 };
        GdiAlphaBlend(mem_dc, left, top, mw, mh, fill_dc, 0, 0, mw, mh, bf);
        SelectObject(fill_dc, old_fbm);
        DeleteObject(fill_bm);
        DeleteDC(fill_dc);

        HPEN mq_pen = CreatePen(PS_SOLID, 1, mq_col);
        HPEN old_p = (HPEN)SelectObject(mem_dc, mq_pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        Rectangle(mem_dc, left, top, right, bottom);
        SelectObject(mem_dc, old_p);
        DeleteObject(mq_pen);
    }
}

/* Hit Test for Bezier Anchors and Control Handles */
bool hit_test_bezier(
    int mx, int my,
    const RECT *rc_canvas,
    int *out_path_idx,
    int *out_node_idx,
    int *out_type /* 0=anchor, 1=hout, 2=hin */
) {
    if (!g_has_canvas) return false;
    float handle_r_sq = 7.0f * 7.0f;
    float anchor_r_sq = 8.0f * 8.0f;

    /* 1. Test handles for active path SELECTED nodes only */
    if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
        BezierPath *bp = &g_paths[g_active_path_idx];
        for (int i = 0; i < bp->node_count; ++i) {
            BezierNode *n = &bp->nodes[i];
            if (!n->selected) continue;
            if (g_active_node_idx >= 0 && i != g_active_node_idx) continue; /* Strictly single active node */

            /* Test hout */
            if (hypotf(n->hout_x, n->hout_y) > 0.5f) {
                int hx = rc_canvas->left + (int)roundf((n->x + n->hout_x) * g_zoom);
                int hy = rc_canvas->top + (int)roundf((n->y + n->hout_y) * g_zoom);
                float dsq = (float)((mx - hx) * (mx - hx) + (my - hy) * (my - hy));
                if (dsq <= handle_r_sq) {
                    if (out_path_idx) *out_path_idx = g_active_path_idx;
                    if (out_node_idx) *out_node_idx = i;
                    if (out_type) *out_type = 1;
                    return true;
                }
            }
            /* Test hin */
            if (hypotf(n->hin_x, n->hin_y) > 0.5f) {
                int hx = rc_canvas->left + (int)roundf((n->x + n->hin_x) * g_zoom);
                int hy = rc_canvas->top + (int)roundf((n->y + n->hin_y) * g_zoom);
                float dsq = (float)((mx - hx) * (mx - hx) + (my - hy) * (my - hy));
                if (dsq <= handle_r_sq) {
                    if (out_path_idx) *out_path_idx = g_active_path_idx;
                    if (out_node_idx) *out_node_idx = i;
                    if (out_type) *out_type = 2;
                    return true;
                }
            }
        }
    }

    /* 2. Test anchors (active path first if valid, then all other paths) */
    if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
        BezierPath *bp = &g_paths[g_active_path_idx];
        for (int i = 0; i < bp->node_count; ++i) {
            int sx = rc_canvas->left + (int)roundf(bp->nodes[i].x * g_zoom);
            int sy = rc_canvas->top + (int)roundf(bp->nodes[i].y * g_zoom);
            float dsq = (float)((mx - sx) * (mx - sx) + (my - sy) * (my - sy));
            if (dsq <= anchor_r_sq) {
                if (out_path_idx) *out_path_idx = g_active_path_idx;
                if (out_node_idx) *out_node_idx = i;
                if (out_type) *out_type = 0;
                return true;
            }
        }
    }

    for (int p = 0; p < g_path_count; ++p) {
        if (p == g_active_path_idx) continue;
        BezierPath *bp = &g_paths[p];
        for (int i = 0; i < bp->node_count; ++i) {
            int sx = rc_canvas->left + (int)roundf(bp->nodes[i].x * g_zoom);
            int sy = rc_canvas->top + (int)roundf(bp->nodes[i].y * g_zoom);
            float dsq = (float)((mx - sx) * (mx - sx) + (my - sy) * (my - sy));
            if (dsq <= anchor_r_sq) {
                if (out_path_idx) *out_path_idx = p;
                if (out_node_idx) *out_node_idx = i;
                if (out_type) *out_type = 0;
                return true;
            }
        }
    }

    return false;
}


/* Helper to render an arc-length span along the cached polyline */
static void draw_curve_span_arc(HDC hdc, BezierPath *bp, const RECT *rc_canvas, double s_start, double s_end, HPEN pen) {
    if (!bp) return;
    if (bp->cache.is_dirty) path_cache_update(bp);
    if (bp->cache.count < 2 || !bp->cache.S || !bp->cache.x || !bp->cache.y) return;
    const PathCache *c = &bp->cache;
    if (s_start >= s_end) return;

    HPEN old_pen = (HPEN)SelectObject(hdc, pen);

    /* Start vertex */
    int i0 = 0;
    while (i0 < c->count - 1 && c->S[i0 + 1] < s_start) i0++;
    double t0 = (c->S[i0 + 1] > c->S[i0]) ? (s_start - c->S[i0]) / (c->S[i0 + 1] - c->S[i0]) : 0.0;
    if (t0 < 0.0) t0 = 0.0;
    if (t0 > 1.0) t0 = 1.0;
    float start_x = (float)((1.0 - t0) * c->x[i0] + t0 * c->x[i0 + 1]);
    float start_y = (float)((1.0 - t0) * c->y[i0] + t0 * c->y[i0 + 1]);

    int sx = rc_canvas->left + (int)roundf(start_x * g_zoom);
    int sy = rc_canvas->top + (int)roundf(start_y * g_zoom);
    MoveToEx(hdc, sx, sy, NULL);

    /* Intermediate vertices */
    for (int i = i0 + 1; i < c->count; ++i) {
        if (c->S[i] >= s_end) break;
        int vx = rc_canvas->left + (int)roundf(c->x[i] * g_zoom);
        int vy = rc_canvas->top + (int)roundf(c->y[i] * g_zoom);
        LineTo(hdc, vx, vy);
    }

    /* End vertex */
    int i1 = 0;
    while (i1 < c->count - 1 && c->S[i1 + 1] < s_end) i1++;
    double t1 = (c->S[i1 + 1] > c->S[i1]) ? (s_end - c->S[i1]) / (c->S[i1 + 1] - c->S[i1]) : 0.0;
    if (t1 < 0.0) t1 = 0.0;
    if (t1 > 1.0) t1 = 1.0;
    float end_x = (float)((1.0 - t1) * c->x[i1] + t1 * c->x[i1 + 1]);
    float end_y = (float)((1.0 - t1) * c->y[i1] + t1 * c->y[i1 + 1]);

    int ex = rc_canvas->left + (int)roundf(end_x * g_zoom);
    int ey = rc_canvas->top + (int)roundf(end_y * g_zoom);
    LineTo(hdc, ex, ey);

    SelectObject(hdc, old_pen);
}

/* Convert RGB to HSV (H: 0..360, S: 0..1, V: 0..1) */
static void rgb_to_hsv(COLORREF col, float *out_h, float *out_s, float *out_v) {
    float r = GetRValue(col) / 255.0f;
    float g = GetGValue(col) / 255.0f;
    float b = GetBValue(col) / 255.0f;
    float max_val = fmaxf(r, fmaxf(g, b));
    float min_val = fminf(r, fminf(g, b));
    float delta = max_val - min_val;

    *out_v = max_val;
    *out_s = (max_val > 1e-5f) ? (delta / max_val) : 0.0f;

    if (delta < 1e-5f) {
        *out_h = 50.0f; /* Neutral default yellow */
    } else if (max_val == r) {
        float h = 60.0f * fmodf(((g - b) / delta), 6.0f);
        if (h < 0.0f) h += 360.0f;
        *out_h = h;
    } else if (max_val == g) {
        *out_h = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        *out_h = 60.0f * (((r - g) / delta) + 4.0f);
    }
}

/* Convert HSV back to RGB COLORREF */
static __attribute__((unused)) COLORREF hsv_to_rgb(float h, float s, float v) {
    while (h < 0.0f) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;

    float c = v * s;
    float h_prime = h / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(h_prime, 2.0f) - 1.0f));
    float m = v - c;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h_prime >= 0.0f && h_prime < 1.0f) {
        r = c; g = x; b = 0.0f;
    } else if (h_prime >= 1.0f && h_prime < 2.0f) {
        r = x; g = c; b = 0.0f;
    } else if (h_prime >= 2.0f && h_prime < 3.0f) {
        r = 0.0f; g = c; b = x;
    } else if (h_prime >= 3.0f && h_prime < 4.0f) {
        r = 0.0f; g = x; b = c;
    } else if (h_prime >= 4.0f && h_prime < 5.0f) {
        r = x; g = 0.0f; b = c;
    } else if (h_prime >= 5.0f && h_prime < 6.0f) {
        r = c; g = 0.0f; b = x;
    }

    BYTE ir = (BYTE)roundf(fminf(255.0f, fmaxf(0.0f, (r + m) * 255.0f)));
    BYTE ig = (BYTE)roundf(fminf(255.0f, fmaxf(0.0f, (g + m) * 255.0f)));
    BYTE ib = (BYTE)roundf(fminf(255.0f, fmaxf(0.0f, (b + m) * 255.0f)));
    return RGB(ir, ig, ib);
}

/* Priority Base Full Colors:
 * 0 = Yellow (RGB(255, 215, 0))
 * 1 = Hijau muda (RGB(65, 215, 75))
 * 2 = Hijau tua (RGB(20, 130, 40))
 * 3 = Biru muda (RGB(55, 180, 255))
 * >=4 = Biru tua (RGB(15, 60, 195))
 */
static COLORREF get_priority_full_color(int prio) {
    switch (prio) {
        case 1:  return RGB(65, 215, 75);   /* Hijau muda (vibrant light green) */
        case 2:  return RGB(20, 130, 40);   /* Hijau tua (rich dark green) */
        case 3:  return RGB(55, 180, 255);  /* Biru muda (sky light blue) */
        default: return RGB(15, 60, 195);   /* Biru tua (deep dark blue, prio >= 4) */
    }
}

/* Priority Colors across 3 Selection States:
 * - Active: 100% full vibrant priority color
 * - Selected: ~0.76x scaled (half-vibrant, matching prio=0 selected amber)
 * - Deselected: 1/2 grey (base_col) + 1/2 priority color (c_full) for prio >= 1;
 *               prio == 0 stays base_col (neutral grey)
 */
static COLORREF get_priority_mixed_color(COLORREF base_col, int prio) {
    if (prio <= 0) return base_col;

    COLORREF c_full = get_priority_full_color(prio);

    float h, s, v;
    rgb_to_hsv(base_col, &h, &s, &v);

    if (s < 0.20f) {
        /* Deselected: 1/2 grey + 1/2 priority color */
        int r = (GetRValue(base_col) + GetRValue(c_full)) / 2;
        int g = (GetGValue(base_col) + GetGValue(c_full)) / 2;
        int b = (GetBValue(base_col) + GetBValue(c_full)) / 2;
        return RGB(r, g, b);
    } else if (v < 0.88f) {
        /* Selected: half-vibrant priority color (analogous to selected amber) */
        int r = (int)roundf(GetRValue(c_full) * 0.76f);
        int g = (int)roundf(GetGValue(c_full) * 0.76f);
        int b = (int)roundf(GetBValue(c_full) * 0.76f);
        return RGB(r, g, b);
    } else {
        /* Active: 100% full vibrant priority color */
        return c_full;
    }
}

/* Render genuine circular dotted line (titik-titik bukan putus) for unhittable tunnel sections */
static void draw_curve_span_dots(HDC hdc, BezierPath *bp, const RECT *rc_canvas, double s_start, double s_end, COLORREF col, int pen_width) {
    if (!bp || s_start >= s_end) return;
    if (bp->cache.is_dirty) path_cache_update(bp);
    const PathCache *c = &bp->cache;
    if (c->count < 2 || !c->S || !c->x || !c->y) return;

    /* Radius strictly matches regular path stroke radius on screen (always constant regardless of zoom) */
    int r = (pen_width >= 2) ? 1 : 1;

    double screen_spacing = 6.0; /* Constant screen pixels interval between dots */
    double spacing = screen_spacing / (g_zoom > 0.01f ? g_zoom : 1.0f);

    HBRUSH dot_br = CreateSolidBrush(col);
    HPEN dot_pen = CreatePen(PS_SOLID, 1, col);
    HBRUSH old_br = (HBRUSH)SelectObject(hdc, dot_br);
    HPEN old_pen = (HPEN)SelectObject(hdc, dot_pen);

    double s = s_start;
    while (s <= s_end) {
        int i0 = 0;
        while (i0 < c->count - 1 && c->S[i0 + 1] < s) i0++;
        double t0 = (c->S[i0 + 1] > c->S[i0]) ? (s - c->S[i0]) / (c->S[i0 + 1] - c->S[i0]) : 0.0;
        if (t0 < 0.0) t0 = 0.0;
        if (t0 > 1.0) t0 = 1.0;
        float px = (float)((1.0 - t0) * c->x[i0] + t0 * c->x[i0 + 1]);
        float py = (float)((1.0 - t0) * c->y[i0] + t0 * c->y[i0 + 1]);

        int sx = rc_canvas->left + (int)roundf(px * g_zoom);
        int sy = rc_canvas->top + (int)roundf(py * g_zoom);

        Ellipse(hdc, sx - r, sy - r, sx + r + 1, sy + r + 1);

        if (s >= s_end) break;
        s += spacing;
        if (s > s_end && (s - spacing) < s_end - (spacing * 0.4)) {
            s = s_end;
        }
    }

    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(dot_br);
    DeleteObject(dot_pen);
}

/* Render full trajectory partitioned by attribute spans:
 * - Tunnel (Unhittable): Genuine Dotted Line (titik-titik)
 * - Hittable: Solid Line
 * - Priority: Blended with pure blue (0..10 -> 0%..100% blue)
 */
static void render_path_trajectory_with_spans(HDC hdc, BezierPath *bp, const RECT *rc_canvas, COLORREF curve_col, int pen_width) {
    if (!bp || bp->node_count < 2) return;
    if (bp->cache.is_dirty) path_cache_update(bp);
    const PathCache *c = &bp->cache;
    if (c->count < 2 || c->total_length <= 0.0) return;

    /* If no active spans exist, render base solid line directly */
    bool has_active_spans = false;
    if (c->baked_spans && c->baked_span_count > 0) {
        for (int i = 0; i < c->baked_span_count; ++i) {
            if (c->baked_spans[i].active) {
                has_active_spans = true;
                break;
            }
        }
    }

    if (!has_active_spans) {
        HPEN base_pen = CreatePen(PS_SOLID, pen_width, curve_col);
        draw_curve_span_arc(hdc, bp, rc_canvas, 0.0, c->total_length, base_pen);
        DeleteObject(base_pen);
        return;
    }

    /* Collect all boundary points along the arc */
    double boundaries[128];
    int b_count = 0;
    boundaries[b_count++] = 0.0;
    boundaries[b_count++] = c->total_length;

    for (int i = 0; i < c->baked_span_count && b_count < 120; ++i) {
        const BakedSpan *b = &c->baked_spans[i];
        if (!b->active) continue;
        double sc = fmax(0.0, fmin(c->total_length, b->s_c));
        double sd = fmax(0.0, fmin(c->total_length, b->s_d));
        if (sd > sc) {
            boundaries[b_count++] = sc;
            boundaries[b_count++] = sd;
        }
    }

    /* Sort boundaries ascending */
    for (int i = 0; i < b_count - 1; ++i) {
        for (int j = i + 1; j < b_count; ++j) {
            if (boundaries[j] < boundaries[i]) {
                double tmp = boundaries[i];
                boundaries[i] = boundaries[j];
                boundaries[j] = tmp;
            }
        }
    }

    /* Deduplicate boundaries closer than 0.1 px */
    int unique_count = 0;
    for (int i = 0; i < b_count; ++i) {
        if (unique_count == 0 || (boundaries[i] - boundaries[unique_count - 1]) > 0.1) {
            boundaries[unique_count++] = boundaries[i];
        }
    }

    /* Render each partition */
    for (int i = 0; i < unique_count - 1; ++i) {
        double s_a = boundaries[i];
        double s_b = boundaries[i + 1];
        if (s_b - s_a < 0.1) continue;

        double s_mid = (s_a + s_b) * 0.5;
        uint8_t tun = 0, prio = 0;
        bezier_path_cache_attrs_at(c, s_mid, &tun, &prio);

        COLORREF seg_col = get_priority_mixed_color(curve_col, prio);

        if (tun == 1) {
            /* Tunnel (Unhittable): Genuine Dotted Points */
            draw_curve_span_dots(hdc, bp, rc_canvas, s_a, s_b, seg_col, pen_width);
        } else {
            /* Hittable Track: Solid Line */
            HPEN seg_pen = CreatePen(PS_SOLID, pen_width, seg_col);
            draw_curve_span_arc(hdc, bp, rc_canvas, s_a, s_b, seg_pen);
            DeleteObject(seg_pen);
        }
    }
}

/* Helper to render sharp directional chevron arrows along the curve trajectory:
 * - Hittable: Filled triangle
 * - Unhittable (Tunnel): Open V-shaped chevron line
 * - Color: Priority mixed HSV color
 */
static void draw_curve_directional_arrows(HDC hdc, BezierPath *bp, const RECT *rc_canvas, COLORREF arrow_col) {
    if (!bp || bp->node_count < 2) return;
    if (bp->cache.is_dirty) path_cache_update(bp);
    const PathCache *c = &bp->cache;
    if (c->count < 2 || c->total_length < 25.0 || !c->S || !c->x || !c->y) return;

    double arrow_spacing = 65.0; /* px arc-length interval */
    double s = 35.0;
    while (s < c->total_length - 20.0) {
        int i0 = 0;
        while (i0 < c->count - 1 && c->S[i0 + 1] < s) i0++;
        double t0 = (c->S[i0 + 1] > c->S[i0]) ? (s - c->S[i0]) / (c->S[i0 + 1] - c->S[i0]) : 0.0;
        if (t0 < 0.0) t0 = 0.0;
        if (t0 > 1.0) t0 = 1.0;
        float px = (float)((1.0 - t0) * c->x[i0] + t0 * c->x[i0 + 1]);
        float py = (float)((1.0 - t0) * c->y[i0] + t0 * c->y[i0 + 1]);

        double s_fwd = s + 3.0;
        int i1 = i0;
        while (i1 < c->count - 1 && c->S[i1 + 1] < s_fwd) i1++;
        double t1 = (c->S[i1 + 1] > c->S[i1]) ? (s_fwd - c->S[i1]) / (c->S[i1 + 1] - c->S[i1]) : 0.0;
        if (t1 < 0.0) t1 = 0.0;
        if (t1 > 1.0) t1 = 1.0;
        float fx = (float)((1.0 - t1) * c->x[i1] + t1 * c->x[i1 + 1]);
        float fy = (float)((1.0 - t1) * c->y[i1] + t1 * c->y[i1 + 1]);

        float dx = fx - px;
        float dy = fy - py;
        float len = hypotf(dx, dy);
        if (len > 1e-4f) {
            float tx = dx / len;
            float ty = dy / len;
            float nx = -ty;
            float ny = tx;

            float scx = (float)rc_canvas->left + px * g_zoom;
            float scy = (float)rc_canvas->top + py * g_zoom;

            float tip_len = 6.0f;
            float wing_w  = 4.5f;

            POINT pts[3];
            pts[0].x = (LONG)roundf(scx + tx * tip_len);
            pts[0].y = (LONG)roundf(scy + ty * tip_len);
            pts[1].x = (LONG)roundf(scx - tx * tip_len + nx * wing_w);
            pts[1].y = (LONG)roundf(scy - ty * tip_len + ny * wing_w);
            pts[2].x = (LONG)roundf(scx - tx * tip_len - nx * wing_w);
            pts[2].y = (LONG)roundf(scy - ty * tip_len - ny * wing_w);

            uint8_t tun = 0, prio = 0;
            bezier_path_cache_attrs_at(c, s, &tun, &prio);
            COLORREF cur_arrow_col = get_priority_mixed_color(arrow_col, prio);

            if (tun == 1) {
                /* Unhittable: Open V-shaped line arrow */
                HPEN v_pen = CreatePen(PS_SOLID, 2, cur_arrow_col);
                HPEN prev_p = (HPEN)SelectObject(hdc, v_pen);
                POINT v_pts[3] = { pts[1], pts[0], pts[2] };
                Polyline(hdc, v_pts, 3);
                SelectObject(hdc, prev_p);
                DeleteObject(v_pen);
            } else {
                /* Hittable: Filled triangular arrow */
                HPEN tri_pen = CreatePen(PS_SOLID, 1, cur_arrow_col);
                HBRUSH tri_br = CreateSolidBrush(cur_arrow_col);
                HPEN prev_p = (HPEN)SelectObject(hdc, tri_pen);
                HBRUSH prev_b = (HBRUSH)SelectObject(hdc, tri_br);
                Polygon(hdc, pts, 3);
                SelectObject(hdc, prev_b);
                SelectObject(hdc, prev_p);
                DeleteObject(tri_br);
                DeleteObject(tri_pen);
            }
        }

        s += arrow_spacing;
    }
}

/* Pixel-by-pixel spatial Cutout Alpha Masking ("IMAGE DENGAN IMAGE") */
static void apply_cutout_mask_to_buffer(
    uint32_t *bits, int stride, int bw, int bh,
    float canv_cx, float canv_cy,
    int prio,
    bool mask_on_equal_prio
) {
    if (!bits || bw <= 0 || bh <= 0) return;

    float cx_dst = (float)bw * 0.5f;
    float cy_dst = (float)bh * 0.5f;
    float ball_left   = canv_cx - cx_dst;
    float ball_right  = canv_cx + cx_dst;
    float ball_top    = canv_cy - cy_dst;
    float ball_bottom = canv_cy + cy_dst;

    for (int c = 0; c < g_cutout_count; ++c) {
        LevelCutout *co = &g_cutouts[c];
        int co_pri = (co->pri > 0) ? co->pri : 1;
        int eff_prio = prio; /* JANGAN di-clamp ke 1! Nilai prio=0 adalah prioritas dasar bola/skull */

        /* Aturan Hierarki Cutout Masking:
         * - Untuk Sprite (mask_on_equal_prio = true): Cutout dengan prio >= sprite akan menutup sprite.
         * - Untuk Bola & Skull (mask_on_equal_prio = false):
         *     Ketika prio SAMA (eff_prio >= co_pri), bola & skull diutamakan DI ATAS (unmasked).
         *     Cutout HANYA menutup bola/skull jika co_pri > eff_prio (misal Cutout prio 1 vs Bola prio 0)!
         */
        if (mask_on_equal_prio ? (eff_prio > co_pri) : (eff_prio >= co_pri)) {
            continue;
        }

        int cw = co->has_img ? co->img.width : (g_has_default_cutout_img ? g_default_cutout_img.width : 160);
        int ch = co->has_img ? co->img.height : (g_has_default_cutout_img ? g_default_cutout_img.height : 140);
        const uint32_t *c_bits = co->has_img ? (const uint32_t *)co->img.bits :
                                 (g_has_default_cutout_img ? (const uint32_t *)g_default_cutout_img.bits : NULL);
        if (cw <= 0 || ch <= 0) continue;

        float co_left   = (float)co->x;
        float co_right  = (float)co->x + (float)cw;
        float co_top    = (float)co->y;
        float co_bottom = (float)co->y + (float)ch;

        /* Bounding box overlap test */
        if (ball_right <= co_left || ball_left >= co_right ||
            ball_bottom <= co_top || ball_top >= co_bottom) {
            continue;
        }

        /* Spatial pixel-by-pixel alpha masking */
        for (int y = 0; y < bh; ++y) {
            float py_canv = canv_cy - cy_dst + (float)y;
            float rel_y = py_canv - co_top;
            if (rel_y < 0.0f || rel_y >= (float)ch) continue;
            int iy = (int)floorf(rel_y);
            if (iy < 0 || iy >= ch) continue;

            uint32_t *row = bits + y * stride;
            const uint32_t *c_row = c_bits ? (c_bits + iy * cw) : NULL;

            for (int x = 0; x < bw; ++x) {
                uint32_t px = row[x];
                if (px == 0) continue;

                float px_canv = canv_cx - cx_dst + (float)x;
                float rel_x = px_canv - co_left;
                if (rel_x < 0.0f || rel_x >= (float)cw) continue;
                int ix = (int)floorf(rel_x);
                if (ix < 0 || ix >= cw) continue;

                float mask_strength = 0.0f;
                if (!c_row) {
                    /* Solid rectangular mask when no bitmap is available */
                    mask_strength = 1.0f;
                } else {
                    uint32_t c_px = c_row[ix];
                    BYTE c_a = (c_px >> 24) & 0xFF;
                    if (c_a == 0) continue;

                    BYTE c_b = c_px & 0xFF;
                    BYTE c_g = (c_px >> 8) & 0xFF;
                    BYTE c_r = (c_px >> 16) & 0xFF;
                    int lum = (c_r * 299 + c_g * 587 + c_b * 114) / 1000;

                    if (c_a < 255) {
                        /* PNG with alpha channel: mask coverage proportional to alpha */
                        mask_strength = (float)c_a / 255.0f;
                    } else if (lum > 15) {
                        /* PopCap grayscale mask: white (lum>=240) covers, black (lum<=15) is open */
                        mask_strength = (lum >= 240) ? 1.0f : ((float)(lum - 15) / 225.0f);
                    }
                }

                if (mask_strength > 0.005f) {
                    float factor = 1.0f - mask_strength;
                    if (factor <= 0.005f) {
                        row[x] = 0;
                    } else {
                        BYTE a = (BYTE)roundf((float)((px >> 24) & 0xFF) * factor);
                        BYTE r = (BYTE)roundf((float)((px >> 16) & 0xFF) * factor);
                        BYTE g = (BYTE)roundf((float)((px >> 8) & 0xFF) * factor);
                        BYTE b = (BYTE)roundf((float)(px & 0xFF) * factor);
                        row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                    }
                }
            }
        }
    }
}

/* Software rotation & blitting for 32-bit PBGRA images (Balls & Skulls) with spatial Cutout Masking */
static void draw_rotated_image_fast(
    HDC dest_dc,
    const CanvasImage *img,
    float center_x, float center_y,
    float angle_rad,
    float zoom,
    BYTE alpha,
    float canv_cx, float canv_cy,
    int prio,
    bool enable_cutout_mask
) {
    if (!img || !img->hbm || !img->bits || img->width <= 0 || img->height <= 0 || alpha == 0) return;

    static HBITMAP s_rot_hbm = NULL;
    static HDC s_rot_dc = NULL;
    static uint32_t *s_rot_bits = NULL;
    static int s_rot_capacity = 0;

    float cos_a = fabsf(cosf(angle_rad));
    float sin_a = fabsf(sinf(angle_rad));
    int bw = (int)ceilf((float)img->width * cos_a + (float)img->height * sin_a) + 2;
    int bh = (int)ceilf((float)img->width * sin_a + (float)img->height * cos_a) + 2;
    if (bw < 4) bw = 4;
    if (bh < 4) bh = 4;

    int req_dim = (bw > bh) ? bw : bh;
    if (req_dim > s_rot_capacity || !s_rot_dc) {
        if (s_rot_dc) DeleteDC(s_rot_dc);
        if (s_rot_hbm) DeleteObject(s_rot_hbm);

        int new_dim = (req_dim > 128) ? req_dim + 32 : 128;
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = new_dim;
        bmi.bmiHeader.biHeight = -new_dim; /* Top-down */
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        s_rot_dc = CreateCompatibleDC(dest_dc);
        s_rot_hbm = CreateDIBSection(s_rot_dc, &bmi, DIB_RGB_COLORS, (void**)&s_rot_bits, NULL, 0);
        SelectObject(s_rot_dc, s_rot_hbm);
        s_rot_capacity = new_dim;
    }

    for (int y = 0; y < bh; ++y) {
        memset(s_rot_bits + y * s_rot_capacity, 0, bw * sizeof(uint32_t));
    }

    float cx_dst = (float)bw * 0.5f;
    float cy_dst = (float)bh * 0.5f;
    float cx_src = (float)img->width * 0.5f;
    float cy_src = (float)img->height * 0.5f;
    float cos_neg = cosf(-angle_rad);
    float sin_neg = sinf(-angle_rad);
    const uint32_t *src_bits = (const uint32_t *)img->bits;
    int src_w = img->width;
    int src_h = img->height;

    for (int y = 0; y < bh; ++y) {
        float dy = (float)y - cy_dst;
        uint32_t *row = s_rot_bits + y * s_rot_capacity;
        for (int x = 0; x < bw; ++x) {
            float dx = (float)x - cx_dst;
            float sx = cx_src + dx * cos_neg - dy * sin_neg;
            float sy = cy_src + dx * sin_neg + dy * cos_neg;

            int x0 = (int)floorf(sx);
            int y0 = (int)floorf(sy);

            if (x0 >= 0 && x0 < src_w - 1 && y0 >= 0 && y0 < src_h - 1) {
                float fx = sx - (float)x0;
                float fy = sy - (float)y0;
                float w00 = (1.0f - fx) * (1.0f - fy);
                float w10 = fx * (1.0f - fy);
                float w01 = (1.0f - fx) * fy;
                float w11 = fx * fy;

                uint32_t p00 = src_bits[y0 * src_w + x0];
                uint32_t p10 = src_bits[y0 * src_w + (x0 + 1)];
                uint32_t p01 = src_bits[(y0 + 1) * src_w + x0];
                uint32_t p11 = src_bits[(y0 + 1) * src_w + (x0 + 1)];

                uint8_t b = (uint8_t)(w00 * (p00 & 0xFF) + w10 * (p10 & 0xFF) + w01 * (p01 & 0xFF) + w11 * (p11 & 0xFF));
                uint8_t g = (uint8_t)(w00 * ((p00 >> 8) & 0xFF) + w10 * ((p10 >> 8) & 0xFF) + w01 * ((p01 >> 8) & 0xFF) + w11 * ((p11 >> 8) & 0xFF));
                uint8_t r = (uint8_t)(w00 * ((p00 >> 16) & 0xFF) + w10 * ((p10 >> 16) & 0xFF) + w01 * ((p01 >> 16) & 0xFF) + w11 * ((p11 >> 16) & 0xFF));
                uint8_t a = (uint8_t)(w00 * ((p00 >> 24) & 0xFF) + w10 * ((p10 >> 24) & 0xFF) + w01 * ((p01 >> 24) & 0xFF) + w11 * ((p11 >> 24) & 0xFF));

                row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            } else if (x0 >= 0 && x0 < src_w && y0 >= 0 && y0 < src_h) {
                row[x] = src_bits[y0 * src_w + x0];
            }
        }
    }

    if (enable_cutout_mask) {
        apply_cutout_mask_to_buffer(s_rot_bits, s_rot_capacity, bw, bh, canv_cx, canv_cy, prio, false);
    }

    int screen_w = (int)roundf((float)bw * zoom);
    int screen_h = (int)roundf((float)bh * zoom);
    int screen_x = (int)roundf(center_x - (float)screen_w * 0.5f);
    int screen_y = (int)roundf(center_y - (float)screen_h * 0.5f);

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    GdiAlphaBlend(dest_dc, screen_x, screen_y, screen_w, screen_h,
                  s_rot_dc, 0, 0, bw, bh, bf);
}

/* Draw Sprite with Cutout Alpha Masking ("IMAGE DENGAN IMAGE") */
static void draw_sprite_masked(
    HDC dest_dc,
    const RECT *rc_canvas,
    LevelSprite *sp,
    int prio,
    float zoom
) {
    if (!sp) return;
    HDC sp_hdc = sp->has_img ? sp->img.hdc_mem : (g_has_default_sprite_img ? g_default_sprite_img.hdc_mem : NULL);
    int img_w = sp->has_img ? sp->img.width : (g_has_default_sprite_img ? g_default_sprite_img.width : 120);
    int img_h = sp->has_img ? sp->img.height : (g_has_default_sprite_img ? g_default_sprite_img.height : 120);
    const uint32_t *sp_bits = sp->has_img ? (const uint32_t *)sp->img.bits :
                              (g_has_default_sprite_img ? (const uint32_t *)g_default_sprite_img.bits : NULL);
    if (!sp_hdc || img_w <= 0 || img_h <= 0) return;

    int sx = rc_canvas->left + (int)roundf((float)sp->x * zoom);
    int sy = rc_canvas->top + (int)roundf((float)sp->y * zoom);
    int sw = (int)roundf((float)img_w * zoom);
    int sh = (int)roundf((float)img_h * zoom);

    /* Fast check: does any cutout with >= priority overlap this sprite?
     * In Zuma.exe (FUN_00415b10), <Sprite> precedes <Cutout> in the same priority bucket,
     * so a Cutout with co_pri >= sp->pri is drawn on top of the Sprite and masks it.
     */
    bool needs_mask = false;
    for (int c = 0; c < g_cutout_count; ++c) {
        LevelCutout *co = &g_cutouts[c];
        int co_pri = (co->pri > 0) ? co->pri : 1;
        if (prio > co_pri) continue;

        int cw = co->has_img ? co->img.width : (g_has_default_cutout_img ? g_default_cutout_img.width : 160);
        int ch = co->has_img ? co->img.height : (g_has_default_cutout_img ? g_default_cutout_img.height : 140);
        if (cw <= 0 || ch <= 0) continue;

        if ((sp->x + img_w) > co->x && sp->x < (co->x + cw) &&
            (sp->y + img_h) > co->y && sp->y < (co->y + ch)) {
            needs_mask = true;
            break;
        }
    }

    if (!needs_mask || !sp_bits) {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        GdiAlphaBlend(dest_dc, sx, sy, sw, sh, sp_hdc, 0, 0, img_w, img_h, bf);
        return;
    }

    static HDC s_sp_dc = NULL;
    static HBITMAP s_sp_hbm = NULL;
    static uint32_t *s_sp_bits = NULL;
    static int s_sp_cap_w = 0, s_sp_cap_h = 0;

    if (img_w > s_sp_cap_w || img_h > s_sp_cap_h || !s_sp_dc) {
        if (s_sp_dc) DeleteDC(s_sp_dc);
        if (s_sp_hbm) DeleteObject(s_sp_hbm);

        int new_w = (img_w > s_sp_cap_w) ? img_w + 32 : s_sp_cap_w;
        int new_h = (img_h > s_sp_cap_h) ? img_h + 32 : s_sp_cap_h;
        if (new_w < 128) new_w = 128;
        if (new_h < 128) new_h = 128;

        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = new_w;
        bmi.bmiHeader.biHeight = -new_h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        s_sp_dc = CreateCompatibleDC(dest_dc);
        s_sp_hbm = CreateDIBSection(s_sp_dc, &bmi, DIB_RGB_COLORS, (void**)&s_sp_bits, NULL, 0);
        SelectObject(s_sp_dc, s_sp_hbm);
        s_sp_cap_w = new_w;
        s_sp_cap_h = new_h;
    }

    for (int y = 0; y < img_h; ++y) {
        memcpy(s_sp_bits + y * s_sp_cap_w, sp_bits + y * img_w, img_w * sizeof(uint32_t));
    }

    apply_cutout_mask_to_buffer(s_sp_bits, s_sp_cap_w, img_w, img_h,
                                (float)sp->x + (float)img_w * 0.5f,
                                (float)sp->y + (float)img_h * 0.5f,
                                prio, true);

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    GdiAlphaBlend(dest_dc, sx, sy, sw, sh, s_sp_dc, 0, 0, img_w, img_h, bf);
}

/* Sample arc-length point and tangent heading on curve */
static bool sample_path_point_and_tangent(const BezierPath *bp, double s, float *out_x, float *out_y, float *out_angle, uint8_t *out_tun, uint8_t *out_prio) {
    if (!bp || bp->node_count < 1) return false;
    const PathCache *c = &bp->cache;
    if (c->count < 2 || c->total_length < 1.0 || !c->S || !c->x || !c->y) {
        if (bp->node_count > 0) {
            if (out_x) *out_x = bp->nodes[0].x;
            if (out_y) *out_y = bp->nodes[0].y;
            if (out_angle) *out_angle = 0.0f;
            if (out_tun) *out_tun = 0;
            if (out_prio) *out_prio = 0;
            return true;
        }
        return false;
    }

    if (s < 0.0) s = 0.0;
    if (s > c->total_length) s = c->total_length;

    int i0 = 0;
    while (i0 < c->count - 1 && c->S[i0 + 1] < s) i0++;
    double t0 = (c->S[i0 + 1] > c->S[i0]) ? (s - c->S[i0]) / (c->S[i0 + 1] - c->S[i0]) : 0.0;
    if (t0 < 0.0) t0 = 0.0;
    if (t0 > 1.0) t0 = 1.0;
    float px = (float)((1.0 - t0) * c->x[i0] + t0 * c->x[i0 + 1]);
    float py = (float)((1.0 - t0) * c->y[i0] + t0 * c->y[i0 + 1]);

    double s_fwd = s + 3.0;
    if (s_fwd > c->total_length) s_fwd = c->total_length;
    double s_back = s;
    if (s_fwd <= s) {
        s_back = (s >= 3.0) ? (s - 3.0) : 0.0;
    }

    int i1 = 0;
    while (i1 < c->count - 1 && c->S[i1 + 1] < s_fwd) i1++;
    double t1 = (c->S[i1 + 1] > c->S[i1]) ? (s_fwd - c->S[i1]) / (c->S[i1 + 1] - c->S[i1]) : 0.0;
    if (t1 < 0.0) t1 = 0.0;
    if (t1 > 1.0) t1 = 1.0;
    float fx = (float)((1.0 - t1) * c->x[i1] + t1 * c->x[i1 + 1]);
    float fy = (float)((1.0 - t1) * c->y[i1] + t1 * c->y[i1 + 1]);

    int ib = 0;
    while (ib < c->count - 1 && c->S[ib + 1] < s_back) ib++;
    double tb = (c->S[ib + 1] > c->S[ib]) ? (s_back - c->S[ib]) / (c->S[ib + 1] - c->S[ib]) : 0.0;
    if (tb < 0.0) tb = 0.0;
    if (tb > 1.0) tb = 1.0;
    float bx = (float)((1.0 - tb) * c->x[ib] + tb * c->x[ib + 1]);
    float by = (float)((1.0 - tb) * c->y[ib] + tb * c->y[ib + 1]);

    float dx = fx - bx;
    float dy = fy - by;
    float angle = atan2f(dy, dx);

    if (out_x) *out_x = px;
    if (out_y) *out_y = py;
    if (out_angle) *out_angle = angle;
    if (out_tun || out_prio) {
        uint8_t tun = 0, prio = 0;
        bezier_path_cache_attrs_at(c, s, &tun, &prio);
        if (out_tun) *out_tun = tun;
        if (out_prio) *out_prio = prio;
    }
    return true;
}

/* Render Level Preview Simulation: Animated 11-ball colored trains & merged skulls */
static void render_level_preview_simulation(HDC mem_dc, const RECT *rc_canvas, const ThemeColors *th) {
    (void)th;
    if (!g_has_canvas) return;

    load_level_preview_assets();
    load_default_cutout_sprite();
    load_default_sprite_sprite();

    /* 1. Calculate & Merge Skulls at Endpoints of each curve (Zuma 20px merge threshold: dx*dx + dy*dy < 400.0f) */
    typedef struct {
        float x;
        float y;
        float angle;
        int prio;
    } MergedSkull;

    MergedSkull skulls[MAX_PATHS];
    int skull_count = 0;

    for (int p = 0; p < g_path_count; ++p) {
        const BezierPath *bp = &g_paths[p];
        if (bp->node_count < 1) continue;

        float end_x = 0.0f, end_y = 0.0f, end_angle = 0.0f;
        uint8_t end_tun = 0, end_prio = 0;
        double skull_eval_s = (bp->cache.total_length > 1.0) ? (bp->cache.total_length - 0.5) : bp->cache.total_length;
        if (bp->cache.count >= 2) {
            sample_path_point_and_tangent(bp, skull_eval_s, &end_x, &end_y, &end_angle, &end_tun, &end_prio);
            int last_idx = bp->cache.count - 1;
            end_x = bp->cache.x[last_idx];
            end_y = bp->cache.y[last_idx];
        } else {
            const BezierNode *last_n = &bp->nodes[bp->node_count - 1];
            end_x = last_n->x;
            end_y = last_n->y;
            bezier_path_cache_attrs_at(&bp->cache, skull_eval_s, &end_tun, &end_prio);
        }

        float skull_angle;
        if (g_map_has_skullrot) {
            skull_angle = g_map_skullrot * ((float)M_PI / 180.0f);
        } else if (bp->skullrot >= 0) {
            skull_angle = (float)bp->skullrot * ((float)M_PI / 180.0f);
        } else {
            /* Default (when inactive): align skull mouth with incoming track tangent (+90 deg CW offset) */
            skull_angle = end_angle + (float)(M_PI / 2.0);
        }

        int sprio = (int)end_prio;
        /* Check Zuma 20px merging threshold (dx*dx + dy*dy < 400.0f) */
        bool merged = false;
        for (int s = 0; s < skull_count; ++s) {
            float dx = skulls[s].x - end_x;
            float dy = skulls[s].y - end_y;
            if (dx * dx + dy * dy < 400.0f) {
                if (sprio > skulls[s].prio) {
                    skulls[s].prio = sprio;
                }
                merged = true;
                break;
            }
        }

        if (!merged && skull_count < MAX_PATHS) {
            skulls[skull_count].x = end_x;
            skulls[skull_count].y = end_y;
            skulls[skull_count].angle = skull_angle;
            skulls[skull_count].prio = sprio;
            skull_count++;
        }
    }

    /* 2. Collect Simulation Balls across all Curves */
    typedef struct {
        float x;
        float y;
        float angle;
        int color_idx;
        int prio;
        BYTE alpha;
    } SimBall;

    static SimBall s_sim_balls[2048];
    int sim_ball_count = 0;

    int min_pri = 0;
    int max_pri = 1;

    for (int s = 0; s < skull_count; ++s) {
        if (skulls[s].prio < min_pri) min_pri = skulls[s].prio;
        if (skulls[s].prio > max_pri) max_pri = skulls[s].prio;
    }

    for (int i = 0; i < g_sprite_count; ++i) {
        int p = (g_sprites[i].pri > 0) ? g_sprites[i].pri : 1;
        if (p < min_pri) min_pri = p;
        if (p > max_pri) max_pri = p;
    }

    const double ball_spacing = 32.0;
    const double wave_gap = 256.0;
    const double wave_period = (double)LEVEL_PREVIEW_BALL_COLORS * ball_spacing + wave_gap;

    for (int p = 0; p < g_path_count; ++p) {
        const BezierPath *bp = &g_paths[p];
        if (bp->node_count < 1 || bp->cache.total_length < 1.0) continue;

        double total_len = bp->cache.total_length;
        double max_sim_dist = (double)g_preview_anim_distance;

        int k_min = (int)floor((max_sim_dist - total_len - wave_period) / wave_period);
        if (k_min < 0) k_min = 0;
        int k_max = (int)ceil(max_sim_dist / wave_period) + 1;

        for (int k = k_min; k <= k_max; ++k) {
            double leader_s = max_sim_dist - (double)k * wave_period;

            /* Strict 11 ball colors:
             * 0: Blue, 1: Yellow, 2: Red, 3: Green, 4: Purple, 5: White,
             * 6: Orange, 7: Navy, 8: Cyan, 9: Black, 10: Cream
             */
            for (int b = 0; b < LEVEL_PREVIEW_BALL_COLORS; ++b) {
                double ball_s = leader_s - (double)b * ball_spacing;
                if (ball_s >= 0.0 && ball_s <= total_len) {
                    float bx, by, b_angle;
                    uint8_t tun = 0, prio = 0;
                    if (sample_path_point_and_tangent(bp, ball_s, &bx, &by, &b_angle, &tun, &prio)) {
                        if (sim_ball_count < 2048) {
                            int bprio = (int)prio;
                            if (bprio < min_pri) min_pri = bprio;
                            if (bprio > max_pri) max_pri = bprio;

                            /* 90 degrees CCW rotation: angle = b_angle - PI/2 */
                            float angle = b_angle - (float)(M_PI / 2.0);

                            s_sim_balls[sim_ball_count].x = bx;
                            s_sim_balls[sim_ball_count].y = by;
                            s_sim_balls[sim_ball_count].angle = angle;
                            s_sim_balls[sim_ball_count].color_idx = b;
                            s_sim_balls[sim_ball_count].prio = bprio;
                            s_sim_balls[sim_ball_count].alpha = 255;
                            sim_ball_count++;
                        }
                    }
                }
            }
        }
    }

    /* 3. Render Sprites, Skulls, and Balls interleaved by Priority:
     * Dalam tingkatan prioritas p yang sama, urutan render dari bawah ke atas:
     *   A. Sprites dengan prioritas p (paling bawah di antara p yang sama)
     *   B. Skulls dengan prioritas p (di atas sprites, tetapi di bawah bola)
     *   C. Balls dengan prioritas p (paling atas: BOLA -> SKULL -> CUTOUT/SPRITE)
     */
    for (int p = min_pri; p <= max_pri; ++p) {
        /* A. Sprites dengan prioritas p (Di-mask pixel-by-pixel jika overlap cutout co_pri >= s_pri) */
        for (int i = 0; i < g_sprite_count; ++i) {
            LevelSprite *sp = &g_sprites[i];
            int s_pri = (sp->pri > 0) ? sp->pri : 1;
            if (s_pri != p) continue;

            draw_sprite_masked(mem_dc, rc_canvas, sp, s_pri, g_zoom);
        }

        /* B. Skulls di ujung kurva dengan prioritas p (Di atas sprite, di bawah bola) */
        if (g_has_skull_img) {
            for (int s = 0; s < skull_count; ++s) {
                if (skulls[s].prio != p) continue;
                float scx = (float)rc_canvas->left + skulls[s].x * g_zoom;
                float scy = (float)rc_canvas->top + skulls[s].y * g_zoom;
                draw_rotated_image_fast(mem_dc, &g_skull_img, scx, scy,
                                        skulls[s].angle, g_zoom, 255,
                                        skulls[s].x, skulls[s].y,
                                        skulls[s].prio, true);
            }
        }

        /* C. Balls dengan prioritas p (Paling atas di antara p yang sama: di atas skull dan sprite) */
        for (int i = 0; i < sim_ball_count; ++i) {
            if (s_sim_balls[i].prio != p) continue;

            int b = s_sim_balls[i].color_idx;
            if (b >= 0 && b < LEVEL_PREVIEW_BALL_COLORS && g_has_ball_imgs[b]) {
                float scx = (float)rc_canvas->left + s_sim_balls[i].x * g_zoom;
                float scy = (float)rc_canvas->top + s_sim_balls[i].y * g_zoom;
                draw_rotated_image_fast(mem_dc, &g_ball_imgs[b], scx, scy,
                                        s_sim_balls[i].angle, g_zoom, 255,
                                        s_sim_balls[i].x, s_sim_balls[i].y,
                                        s_sim_balls[i].prio, true);
            }
        }
    }
}

/* Render Bezier Paths, Anchors, Handles, and Marquee Box */
static void render_bezier_curves(HDC mem_dc, const RECT *rc_canvas, int client_w, int work_h, const ThemeColors *th) {
    if (!g_has_canvas) return;

    for (int p = 0; p < g_path_count; ++p) {
        BezierPath *bp = &g_paths[p];
        if (bp->node_count < 1) continue;

        bool is_active_path = (p == g_active_path_idx);
        bool has_selected_node = false;
        for (int i = 0; i < bp->node_count; ++i) {
            if (bp->nodes[i].selected) {
                has_selected_node = true;
                break;
            }
        }
        bool is_selected_path = !is_active_path && has_selected_node;

        /* 3 Sifat Status Path:
         * 1. Deselected: Warna abu-abu (Grey)
         * 2. Selected:   Warna kuning setengah (Muted Amber Yellow)
         * 3. Active:     Warna kuning penuh (Vibrant Gold Yellow)
         */
        COLORREF curve_col;
        int pen_width;
        COLORREF arrow_col;

        if (is_active_path) {
            curve_col = RGB(255, 215, 0);  /* Active: Kuning Penuh */
            pen_width = 2;
            arrow_col = RGB(255, 220, 25);
        } else if (is_selected_path) {
            curve_col = RGB(195, 160, 35); /* Selected: Kuning Setengah */
            pen_width = 2;
            arrow_col = RGB(195, 160, 35);
        } else {
            curve_col = (g_current_theme == THEME_DARK) ? RGB(115, 122, 134) : RGB(150, 156, 166); /* Deselected: Abu-abu */
            pen_width = 1;
            arrow_col = (g_current_theme == THEME_DARK) ? RGB(95, 102, 114) : RGB(135, 142, 152);
        }

        HPEN handle_pen = CreatePen(PS_SOLID, 1, is_active_path ? RGB(230, 195, 20) : (is_selected_path ? RGB(180, 150, 30) : RGB(150, 155, 165)));
        HPEN start_pen  = CreatePen(PS_SOLID, 1, RGB(40, 180, 80));
        HPEN end_pen    = CreatePen(PS_SOLID, 1, RGB(220, 60, 60));
        HBRUSH anchor_act_br = CreateSolidBrush(RGB(255, 215, 0));  /* Active node: Kuning Penuh */
        HBRUSH anchor_sel_br = CreateSolidBrush(RGB(195, 160, 35)); /* Selected node: Kuning Setengah / Amber */
        HBRUSH anchor_def_br = CreateSolidBrush(is_active_path ? RGB(255, 255, 255) : (is_selected_path ? RGB(210, 205, 180) : RGB(170, 175, 185)));
        HBRUSH handle_br = CreateSolidBrush(RGB(255, 215, 0));
        HPEN anchor_pen = CreatePen(PS_SOLID, 1, RGB(30, 35, 45));

        /* A. Render curve segments with attribute spans & priority color mixing */
        render_path_trajectory_with_spans(mem_dc, bp, rc_canvas, curve_col, pen_width);

        /* A1. Render Real-time Directional Flow Arrows */
        draw_curve_directional_arrows(mem_dc, bp, rc_canvas, arrow_col);

        /* A2. Render Active Variable Drag Preview */
        if (is_active_path && g_var_dragging && g_curve_submode == CURVE_SUBMODE_VARIABLE) {
            double s_min = (g_var_drag_s_start < g_var_drag_s_cur) ? g_var_drag_s_start : g_var_drag_s_cur;
            double s_max = (g_var_drag_s_start > g_var_drag_s_cur) ? g_var_drag_s_start : g_var_drag_s_cur;
            if (s_max > s_min + 0.5) {
                COLORREF drag_col = get_priority_mixed_color(RGB(255, 230, 40), g_var_prio_val);
                if (g_var_tunnel_val == 1) {
                    draw_curve_span_dots(mem_dc, bp, rc_canvas, s_min, s_max, drag_col, pen_width);
                } else {
                    HPEN drag_pen = CreatePen(PS_SOLID, 3, drag_col);
                    draw_curve_span_arc(mem_dc, bp, rc_canvas, s_min, s_max, drag_pen);
                    DeleteObject(drag_pen);
                }
            }
        }

        /* B. Render tangent handles strictly for the single active node */
        if (is_active_path) {
            HPEN old_p = (HPEN)SelectObject(mem_dc, handle_pen);
            HBRUSH old_b = (HBRUSH)SelectObject(mem_dc, handle_br);

            for (int i = 0; i < bp->node_count; ++i) {
                const BezierNode *n = &bp->nodes[i];
                if (!n->selected) continue;
                if (g_active_node_idx >= 0 && i != g_active_node_idx) continue; /* Strictly single active node */

                int ax = rc_canvas->left + (int)roundf(n->x * g_zoom);
                int ay = rc_canvas->top + (int)roundf(n->y * g_zoom);

                /* Outgoing handle */
                if (hypotf(n->hout_x, n->hout_y) > 0.5f) {
                    int hx = rc_canvas->left + (int)roundf((n->x + n->hout_x) * g_zoom);
                    int hy = rc_canvas->top + (int)roundf((n->y + n->hout_y) * g_zoom);
                    MoveToEx(mem_dc, ax, ay, NULL);
                    LineTo(mem_dc, hx, hy);
                    Ellipse(mem_dc, hx - 3, hy - 3, hx + 4, hy + 4);
                }

                /* Incoming handle */
                if (hypotf(n->hin_x, n->hin_y) > 0.5f) {
                    int hx = rc_canvas->left + (int)roundf((n->x + n->hin_x) * g_zoom);
                    int hy = rc_canvas->top + (int)roundf((n->y + n->hin_y) * g_zoom);
                    MoveToEx(mem_dc, ax, ay, NULL);
                    LineTo(mem_dc, hx, hy);
                    Ellipse(mem_dc, hx - 3, hy - 3, hx + 4, hy + 4);
                }
            }
            SelectObject(mem_dc, old_b);
            SelectObject(mem_dc, old_p);
        }

        /* C. Render anchors */
        HPEN old_p = (HPEN)SelectObject(mem_dc, anchor_pen);
        for (int i = 0; i < bp->node_count; ++i) {
            const BezierNode *n = &bp->nodes[i];
            int ax = rc_canvas->left + (int)roundf(n->x * g_zoom);
            int ay = rc_canvas->top + (int)roundf(n->y * g_zoom);

            bool is_act_node = (is_active_path && i == g_active_node_idx && n->selected);
            bool is_sel_node = n->selected && !is_act_node;

            HBRUSH br = is_act_node ? anchor_act_br : (is_sel_node ? anchor_sel_br : anchor_def_br);
            RECT rc_anchor = { ax - 3, ay - 3, ax + 4, ay + 4 };
            FillRect(mem_dc, &rc_anchor, br);
            FrameRect(mem_dc, &rc_anchor, (HBRUSH)GetStockObject(BLACK_BRUSH));

            /* First node: green start indicator; Last node: red end indicator */
            if (i == 0 && bp->node_count > 1) {
                HPEN op = (HPEN)SelectObject(mem_dc, start_pen);
                SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
                Ellipse(mem_dc, ax - 7, ay - 7, ax + 8, ay + 8);
                SelectObject(mem_dc, op);
            } else if (i == bp->node_count - 1 && bp->node_count > 1) {
                HPEN op = (HPEN)SelectObject(mem_dc, end_pen);
                SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
                Ellipse(mem_dc, ax - 7, ay - 7, ax + 8, ay + 8);
                SelectObject(mem_dc, op);
            }
        }
        SelectObject(mem_dc, old_p);

        DeleteObject(handle_pen);
        DeleteObject(start_pen);
        DeleteObject(end_pen);
        DeleteObject(anchor_act_br);
        DeleteObject(anchor_sel_br);
        DeleteObject(anchor_def_br);
        DeleteObject(handle_br);
        DeleteObject(anchor_pen);
    }

    /* D. Marquee rectangle */
    if (g_pen_drag_state == PEN_DRAG_MARQUEE) {
        int left = g_marquee_start_x < g_marquee_cur_x ? g_marquee_start_x : g_marquee_cur_x;
        int right = g_marquee_start_x > g_marquee_cur_x ? g_marquee_start_x : g_marquee_cur_x;
        int top = g_marquee_start_y < g_marquee_cur_y ? g_marquee_start_y : g_marquee_cur_y;
        int bottom = g_marquee_start_y > g_marquee_cur_y ? g_marquee_start_y : g_marquee_cur_y;
        int mw = right - left;
        int mh = bottom - top;
        if (mw > 0 && mh > 0) {
            COLORREF mq_col;
            switch (g_marquee_mode) {
                case MARQUEE_MODE_ADD:
                    mq_col = RGB(35, 185, 80);  /* Green for Add */
                    break;
                case MARQUEE_MODE_SUBTRACT:
                    mq_col = RGB(225, 55, 55);  /* Red for Deselect */
                    break;
                case MARQUEE_MODE_TOGGLE:
                    mq_col = RGB(175, 65, 220); /* Purple for Toggle */
                    break;
                case MARQUEE_MODE_REPLACE:
                default:
                    mq_col = th->text_accent_blue; /* Blue for Standard Replace */
                    break;
            }

            HDC fill_dc = CreateCompatibleDC(mem_dc);
            HBITMAP fill_bm = CreateCompatibleBitmap(mem_dc, mw, mh);
            HBITMAP old_fbm = (HBITMAP)SelectObject(fill_dc, fill_bm);
            HBRUSH fill_br = CreateSolidBrush(mq_col);
            RECT rc_fill = {0, 0, mw, mh};
            FillRect(fill_dc, &rc_fill, fill_br);
            DeleteObject(fill_br);

            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 48, 0 };
            GdiAlphaBlend(mem_dc, left, top, mw, mh, fill_dc, 0, 0, mw, mh, bf);
            SelectObject(fill_dc, old_fbm);
            DeleteObject(fill_bm);
            DeleteDC(fill_dc);

            HPEN mq_pen = CreatePen(PS_SOLID, 1, mq_col);
            HPEN old_p = (HPEN)SelectObject(mem_dc, mq_pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, left, top, right, bottom);
            SelectObject(mem_dc, old_p);
            DeleteObject(mq_pen);
        }
    }

    /* E. Prospective Path Segment Preview on Hover (Rubber-banding / Next Node Preview) - Draw Mode only */
    if (g_mode_curve && g_curve_submode == CURVE_SUBMODE_DRAW && g_has_canvas && g_mouse_screen_y >= TOOLBAR_HEIGHT && g_pen_drag_state == PEN_DRAG_NONE) {
        int hit_p = -1, hit_n = -1, hit_type = -1;
        bool is_hover_item = hit_test_bezier(g_mouse_screen_x, g_mouse_screen_y, rc_canvas, &hit_p, &hit_n, &hit_type);

        if (!is_hover_item) {
            bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

            if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
                BezierPath *bp = &g_paths[g_active_path_idx];
                if (!bp->is_closed && bp->node_count > 0) {
                    const BezierNode *last_node = &bp->nodes[bp->node_count - 1];

                    float cand_x = g_canvas_mouse_x;
                    float cand_y = g_canvas_mouse_y;
                    int snap_deg = -1;

                    if (is_shift) {
                        float dx = cand_x - last_node->x;
                        float dy = cand_y - last_node->y;
                        float snap_dx, snap_dy;
                        snap_vector_45_deg(dx, dy, &snap_dx, &snap_dy);
                        cand_x = last_node->x + snap_dx;
                        cand_y = last_node->y + snap_dy;
                        double ang = atan2((double)snap_dy, (double)snap_dx) * 180.0 / 3.14159265358979323846;
                        if (ang < 0.0) ang += 360.0;
                        snap_deg = ((int)round(ang) % 360);
                    }

                    bool is_outside = (cand_x < 0.0f || cand_x > (float)g_canvas_w ||
                                       cand_y < 0.0f || cand_y > (float)g_canvas_h);
                    COLORREF prev_col;
                    COLORREF badge_border_col;
                    COLORREF badge_text_col;

                    if (is_outside) {
                        prev_col = th->text_accent_red;
                        badge_border_col = th->text_accent_red;
                        badge_text_col = th->text_accent_red;
                    } else if (is_shift) {
                        prev_col = th->text_accent_green;
                        badge_border_col = th->text_accent_green;
                        badge_text_col = th->text_primary;
                    } else {
                        prev_col = th->text_accent_blue;
                        badge_border_col = th->text_accent_blue;
                        badge_text_col = th->text_primary;
                    }

                    /* 1. Prospective Cubic Bezier Curve from last node to candidate */
                    float p0x = last_node->x, p0y = last_node->y;
                    float p1x = last_node->x + last_node->hout_x, p1y = last_node->y + last_node->hout_y;
                    float p2x = cand_x, p2y = cand_y;
                    float p3x = cand_x, p3y = cand_y;

                    HPEN prev_pen = CreatePen(PS_DOT, 1, prev_col);
                    HPEN old_p = (HPEN)SelectObject(mem_dc, prev_pen);

                    int start_sx = rc_canvas->left + (int)roundf(p0x * g_zoom);
                    int start_sy = rc_canvas->top + (int)roundf(p0y * g_zoom);
                    MoveToEx(mem_dc, start_sx, start_sy, NULL);

                    float seg_dx = p3x - p0x;
                    float seg_dy = p3y - p0y;
                    float screen_chord = hypotf(seg_dx, seg_dy) * g_zoom;
                    int steps = (int)(screen_chord / 4.0f);
                    if (steps < 12) steps = 12;
                    if (steps > 60) steps = 60;

                    for (int s = 1; s <= steps; ++s) {
                        float t = (float)s / (float)steps;
                        float it = 1.0f - t;
                        float bx = it*it*it*p0x + 3.0f*it*it*t*p1x + 3.0f*it*t*t*p2x + t*t*t*p3x;
                        float by = it*it*it*p0y + 3.0f*it*it*t*p1y + 3.0f*it*t*t*p2y + t*t*t*p3y;
                        int sx = rc_canvas->left + (int)roundf(bx * g_zoom);
                        int sy = rc_canvas->top + (int)roundf(by * g_zoom);
                        LineTo(mem_dc, sx, sy);
                    }
                    SelectObject(mem_dc, old_p);
                    DeleteObject(prev_pen);

                    /* 2. Candidate Anchor Crosshair & Square Reticle */
                    int cand_sx = rc_canvas->left + (int)roundf(cand_x * g_zoom);
                    int cand_sy = rc_canvas->top + (int)roundf(cand_y * g_zoom);

                    HPEN reticle_pen = CreatePen(PS_SOLID, 1, prev_col);
                    old_p = (HPEN)SelectObject(mem_dc, reticle_pen);
                    SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
                    Rectangle(mem_dc, cand_sx - 4, cand_sy - 4, cand_sx + 5, cand_sy + 5);
                    MoveToEx(mem_dc, cand_sx - 8, cand_sy, NULL);
                    LineTo(mem_dc, cand_sx + 9, cand_sy);
                    MoveToEx(mem_dc, cand_sx, cand_sy - 8, NULL);
                    LineTo(mem_dc, cand_sx, cand_sy + 9);
                    SelectObject(mem_dc, old_p);
                    DeleteObject(reticle_pen);

                    /* 3. Floating Coordinate & Snap Badge */
                    char badge_str[128];
                    if (is_outside) {
                        if (is_shift && snap_deg >= 0) {
                            snprintf(badge_str, sizeof(badge_str), "+Node #%d [%.0f, %.0f] %d deg (Off-Canvas)", bp->node_count + 1, cand_x, cand_y, snap_deg);
                        } else {
                            snprintf(badge_str, sizeof(badge_str), "+Node #%d [%.0f, %.0f] (Off-Canvas)", bp->node_count + 1, cand_x, cand_y);
                        }
                    } else {
                        if (is_shift && snap_deg >= 0) {
                            snprintf(badge_str, sizeof(badge_str), "+Node #%d [%.0f, %.0f] %d deg", bp->node_count + 1, cand_x, cand_y, snap_deg);
                        } else {
                            snprintf(badge_str, sizeof(badge_str), "+Node #%d [%.0f, %.0f]", bp->node_count + 1, cand_x, cand_y);
                        }
                    }
                    int badge_y = (cand_sy - 28 >= TOOLBAR_HEIGHT + 4) ? (cand_sy - 28) : (cand_sy + 12);
                    render_coord_badge_ex(mem_dc, cand_sx, badge_y, badge_str, th, badge_border_col, badge_text_col);
                } else if (bp->node_count == 0) {
                    float cand_x = g_canvas_mouse_x;
                    float cand_y = g_canvas_mouse_y;
                    bool is_outside = (cand_x < 0.0f || cand_x > (float)g_canvas_w ||
                                       cand_y < 0.0f || cand_y > (float)g_canvas_h);
                    COLORREF prev_col = is_outside ? th->text_accent_red : (is_shift ? th->text_accent_green : th->text_accent_blue);
                    COLORREF badge_border_col = prev_col;
                    COLORREF badge_text_col = is_outside ? th->text_accent_red : th->text_primary;

                    int cand_sx = rc_canvas->left + (int)roundf(cand_x * g_zoom);
                    int cand_sy = rc_canvas->top + (int)roundf(cand_y * g_zoom);

                    HPEN reticle_pen = CreatePen(PS_SOLID, 1, prev_col);
                    HPEN old_p = (HPEN)SelectObject(mem_dc, reticle_pen);
                    SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
                    Rectangle(mem_dc, cand_sx - 4, cand_sy - 4, cand_sx + 5, cand_sy + 5);
                    MoveToEx(mem_dc, cand_sx - 8, cand_sy, NULL);
                    LineTo(mem_dc, cand_sx + 9, cand_sy);
                    MoveToEx(mem_dc, cand_sx, cand_sy - 8, NULL);
                    LineTo(mem_dc, cand_sx, cand_sy + 9);
                    SelectObject(mem_dc, old_p);
                    DeleteObject(reticle_pen);

                    char badge_str[96];
                    if (is_outside) {
                        snprintf(badge_str, sizeof(badge_str), "Start Path #%d [%.0f, %.0f] (Off-Canvas)", g_active_path_idx + 1, cand_x, cand_y);
                    } else {
                        snprintf(badge_str, sizeof(badge_str), "Start Path #%d [%.0f, %.0f]", g_active_path_idx + 1, cand_x, cand_y);
                    }
                    int badge_y = (cand_sy - 28 >= TOOLBAR_HEIGHT + 4) ? (cand_sy - 28) : (cand_sy + 12);
                    render_coord_badge_ex(mem_dc, cand_sx, badge_y, badge_str, th, badge_border_col, badge_text_col);
                }
            } else {
                float cand_x = g_canvas_mouse_x;
                float cand_y = g_canvas_mouse_y;
                bool is_outside = (cand_x < 0.0f || cand_x > (float)g_canvas_w ||
                                   cand_y < 0.0f || cand_y > (float)g_canvas_h);
                COLORREF prev_col = is_outside ? th->text_accent_red : (is_shift ? th->text_accent_green : th->text_accent_blue);
                COLORREF badge_border_col = prev_col;
                COLORREF badge_text_col = is_outside ? th->text_accent_red : th->text_primary;

                int cand_sx = rc_canvas->left + (int)roundf(cand_x * g_zoom);
                int cand_sy = rc_canvas->top + (int)roundf(cand_y * g_zoom);

                HPEN reticle_pen = CreatePen(PS_SOLID, 1, prev_col);
                HPEN old_p = (HPEN)SelectObject(mem_dc, reticle_pen);
                SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
                Rectangle(mem_dc, cand_sx - 4, cand_sy - 4, cand_sx + 5, cand_sy + 5);
                MoveToEx(mem_dc, cand_sx - 8, cand_sy, NULL);
                LineTo(mem_dc, cand_sx + 9, cand_sy);
                MoveToEx(mem_dc, cand_sx, cand_sy - 8, NULL);
                LineTo(mem_dc, cand_sx, cand_sy + 9);
                SelectObject(mem_dc, old_p);
                DeleteObject(reticle_pen);

                char badge_str[96];
                if (is_outside) {
                    snprintf(badge_str, sizeof(badge_str), "New Path #%d [%.0f, %.0f] (Off-Canvas)", g_path_count + 1, cand_x, cand_y);
                } else {
                    snprintf(badge_str, sizeof(badge_str), "New Path #%d [%.0f, %.0f]", g_path_count + 1, cand_x, cand_y);
                }
                int badge_y = (cand_sy - 28 >= TOOLBAR_HEIGHT + 4) ? (cand_sy - 28) : (cand_sy + 12);
                render_coord_badge_ex(mem_dc, cand_sx, badge_y, badge_str, th, badge_border_col, badge_text_col);
            }
        }
    }

    /* F. Real-time Curve Attribute Tooltip Badge (Variable Mode) */
    if (g_mode_curve && g_curve_submode == CURVE_SUBMODE_VARIABLE && !g_var_dragging) {
        if (g_var_hover_active) {
            char badge_txt[128];
            const char *tun_str = g_var_hover_tun ? "Unhittable" : "Hittable";
            snprintf(badge_txt, sizeof(badge_txt), "Path #%d [s: %.0f px] | %s | Prio: %u",
                     g_active_path_idx + 1, g_var_hover_s, tun_str, g_var_hover_prio);

            int center_sx = rc_canvas->left + (int)roundf(g_var_hover_x * g_zoom);
            int badge_y = rc_canvas->top + (int)roundf(g_var_hover_y * g_zoom) - 28;
            if (badge_y < rc_canvas->top + 4) badge_y = rc_canvas->top + (int)roundf(g_var_hover_y * g_zoom) + 14;

            COLORREF badge_border = g_var_hover_tun ? RGB(255, 140, 40) : (g_var_hover_prio > 0 ? get_priority_mixed_color(RGB(255, 215, 0), g_var_hover_prio) : th->text_accent_blue);
            render_coord_badge_alpha(mem_dc, center_sx, badge_y, badge_txt, th, badge_border, th->text_primary, 128); /* 50% opacity */

            /* Highlight point on curve */
            int cx_pt = center_sx;
            int cy_pt = rc_canvas->top + (int)roundf(g_var_hover_y * g_zoom);
            COLORREF pt_col = g_var_hover_tun ? RGB(255, 140, 40) : (g_var_hover_prio > 0 ? get_priority_mixed_color(RGB(255, 215, 0), g_var_hover_prio) : RGB(255, 230, 40));
            HPEN pt_pen = CreatePen(PS_SOLID, 1, pt_col);
            HPEN old_pt_pen = (HPEN)SelectObject(mem_dc, pt_pen);
            HBRUSH pt_br = CreateSolidBrush(pt_col);
            HBRUSH old_pt_br = (HBRUSH)SelectObject(mem_dc, pt_br);
            Ellipse(mem_dc, cx_pt - 4, cy_pt - 4, cx_pt + 5, cy_pt + 5);
            SelectObject(mem_dc, old_pt_br);
            SelectObject(mem_dc, old_pt_pen);
            DeleteObject(pt_br);
            DeleteObject(pt_pen);
        } else if (!g_is_panning && g_mouse_screen_y >= TOOLBAR_HEIGHT &&
                   g_canvas_mouse_x >= 0.0f && g_canvas_mouse_x <= (float)g_canvas_w &&
                   g_canvas_mouse_y >= 0.0f && g_canvas_mouse_y <= (float)g_canvas_h) {
            /* Hover on Canvas directly (Non-path) */
            RECT rc_tr, rc_tl;
            get_top_right_hud_rect(client_w, work_h, &rc_tr);
            get_top_left_hud_rect(work_h, &rc_tl);
            bool over_hud = (g_mouse_screen_x >= rc_tr.left && g_mouse_screen_x <= rc_tr.right &&
                             g_mouse_screen_y >= rc_tr.top && g_mouse_screen_y <= rc_tr.bottom) ||
                            (g_mouse_screen_x >= rc_tl.left && g_mouse_screen_x <= rc_tl.right &&
                             g_mouse_screen_y >= rc_tl.top && g_mouse_screen_y <= rc_tl.bottom);
            if (!over_hud) {
                char badge_txt[128];
                const char *tun_str = (g_var_tunnel_val == 1) ? "Unhittable" : "Hittable";
                snprintf(badge_txt, sizeof(badge_txt), "Brush | %s | Prio: %u", tun_str, g_var_prio_val);

                int center_sx = rc_canvas->left + (int)roundf(g_canvas_mouse_x * g_zoom);
                int badge_y = rc_canvas->top + (int)roundf(g_canvas_mouse_y * g_zoom) - 28;
                if (badge_y < rc_canvas->top + 4) badge_y = rc_canvas->top + (int)roundf(g_canvas_mouse_y * g_zoom) + 14;

                COLORREF badge_border = (g_var_tunnel_val == 1) ? RGB(255, 140, 40) : (g_var_prio_val > 0 ? get_priority_mixed_color(RGB(255, 215, 0), g_var_prio_val) : th->text_accent_blue);
                render_coord_badge_alpha(mem_dc, center_sx, badge_y, badge_txt, th, badge_border, th->text_primary, 128); /* 50% opacity */
            }
        }
    }
}


/* Render 2-Step Donut / Ring Direction Widget for BackgroundAlpha (Vx/Vy Animation Velocity) */
static void render_bgalpha_vector_widget(HDC mem_dc, const RECT *rc_canvas, const ThemeColors *th) {
    if (!g_placing_bgalpha_vector) return;

    int anchor_sx = rc_canvas->left + (int)roundf((float)g_bgalpha_anchor_x * g_zoom);
    int anchor_sy = rc_canvas->top + (int)roundf((float)g_bgalpha_anchor_y * g_zoom);

    int r_outer = 48;
    int r_dead = 16;

    /* Draw semi-transparent / dark filled background disc */
    HBRUSH ring_bg = CreateSolidBrush(RGB(20, 24, 34));
    HPEN ring_pen = CreatePen(PS_SOLID, 2, th->text_accent_blue);
    HBRUSH old_br = (HBRUSH)SelectObject(mem_dc, ring_bg);
    HPEN old_pen = (HPEN)SelectObject(mem_dc, ring_pen);

    Ellipse(mem_dc, anchor_sx - r_outer, anchor_sy - r_outer, anchor_sx + r_outer, anchor_sy + r_outer);

    /* Draw deadzone circle */
    HPEN dead_pen = CreatePen(PS_DOT, 1, RGB(120, 140, 160));
    SelectObject(mem_dc, dead_pen);
    SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
    Ellipse(mem_dc, anchor_sx - r_dead, anchor_sy - r_dead, anchor_sx + r_dead, anchor_sy + r_dead);
    DeleteObject(dead_pen);

    /* Draw 8 radial direction ticks */
    HPEN spoke_pen = CreatePen(PS_SOLID, 1, RGB(70, 80, 100));
    SelectObject(mem_dc, spoke_pen);
    for (int a = 0; a < 8; a++) {
        float rad = (float)a * 0.785398163f; /* PI / 4 */
        int x1 = anchor_sx + (int)roundf(cosf(rad) * (float)r_dead);
        int y1 = anchor_sy + (int)roundf(sinf(rad) * (float)r_dead);
        int x2 = anchor_sx + (int)roundf(cosf(rad) * (float)r_outer);
        int y2 = anchor_sy + (int)roundf(sinf(rad) * (float)r_outer);
        MoveToEx(mem_dc, x1, y1, NULL);
        LineTo(mem_dc, x2, y2);
    }
    DeleteObject(spoke_pen);

    /* Calculate mouse puck position */
    int dx = g_mouse_screen_x - anchor_sx;
    int dy = g_mouse_screen_y - anchor_sy;
    float dist = sqrtf((float)(dx * dx + dy * dy));

    int puck_x = anchor_sx;
    int puck_y = anchor_sy;
    if (dist >= (float)r_dead) {
        float angle = atan2f((float)dy, (float)dx);
        float p_dist = dist;
        if (p_dist > (float)(r_outer - 8)) p_dist = (float)(r_outer - 8);
        puck_x = anchor_sx + (int)roundf(cosf(angle) * p_dist);
        puck_y = anchor_sy + (int)roundf(sinf(angle) * p_dist);

        /* Draw active direction vector line */
        HPEN active_line_pen = CreatePen(PS_SOLID, 2, th->text_accent_green);
        SelectObject(mem_dc, active_line_pen);
        MoveToEx(mem_dc, anchor_sx, anchor_sy, NULL);
        LineTo(mem_dc, puck_x, puck_y);
        DeleteObject(active_line_pen);
    }

    /* Draw puck (circle) */
    HBRUSH puck_br = CreateSolidBrush(th->text_accent_green);
    HPEN puck_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    SelectObject(mem_dc, puck_br);
    SelectObject(mem_dc, puck_pen);
    Ellipse(mem_dc, puck_x - 6, puck_y - 6, puck_x + 7, puck_y + 7);
    DeleteObject(puck_pen);
    DeleteObject(puck_br);

    SelectObject(mem_dc, old_pen);
    SelectObject(mem_dc, old_br);
    DeleteObject(ring_pen);
    DeleteObject(ring_bg);

    /* Draw floating badge next to donut */
    char badge[128];
    const char *dir_name = "Stationary";
    if (g_bgalpha_pending_vx == 1 && g_bgalpha_pending_vy == 0) dir_name = "East";
    else if (g_bgalpha_pending_vx == 1 && g_bgalpha_pending_vy == 1) dir_name = "South-East";
    else if (g_bgalpha_pending_vx == 0 && g_bgalpha_pending_vy == 1) dir_name = "South";
    else if (g_bgalpha_pending_vx == -1 && g_bgalpha_pending_vy == 1) dir_name = "South-West";
    else if (g_bgalpha_pending_vx == -1 && g_bgalpha_pending_vy == 0) dir_name = "West";
    else if (g_bgalpha_pending_vx == -1 && g_bgalpha_pending_vy == -1) dir_name = "North-West";
    else if (g_bgalpha_pending_vx == 0 && g_bgalpha_pending_vy == -1) dir_name = "North";
    else if (g_bgalpha_pending_vx == 1 && g_bgalpha_pending_vy == -1) dir_name = "North-East";

    snprintf(badge, sizeof(badge), "Direction: %s | Vx: %d, Vy: %d (Click LMB to Confirm)",
             dir_name, g_bgalpha_pending_vx, g_bgalpha_pending_vy);
    render_coord_badge(mem_dc, anchor_sx, anchor_sy + r_outer + 18, badge, th);
}

/* Hit-testing for 8 Canvas Resize Handles + Center Move + Center Donut */
CanvasHandleType hit_test_canvas_handles(int client_w, int work_h, int sx, int sy) {
    if (!g_has_canvas) return CANVAS_HANDLE_NONE;

    RECT rc;
    get_canvas_screen_rect(client_w, work_h, &rc);
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;

    int tol = 12; /* tolerance pixel radius */

    /* 1. Corners (Priority over edges) */
    if (abs(sx - rc.left) <= tol && abs(sy - rc.top) <= tol) return CANVAS_HANDLE_TOP_LEFT;
    if (abs(sx - rc.right) <= tol && abs(sy - rc.top) <= tol) return CANVAS_HANDLE_TOP_RIGHT;
    if (abs(sx - rc.left) <= tol && abs(sy - rc.bottom) <= tol) return CANVAS_HANDLE_BOTTOM_LEFT;
    if (abs(sx - rc.right) <= tol && abs(sy - rc.bottom) <= tol) return CANVAS_HANDLE_BOTTOM_RIGHT;

    /* 2. Edges (Top, Bottom, Left, Right) */
    if (abs(sx - cx) <= tol * 2 && abs(sy - rc.top) <= tol) return CANVAS_HANDLE_TOP;
    if (abs(sx - cx) <= tol * 2 && abs(sy - rc.bottom) <= tol) return CANVAS_HANDLE_BOTTOM;
    if (abs(sx - rc.left) <= tol && abs(sy - cy) <= tol * 2) return CANVAS_HANDLE_LEFT;
    if (abs(sx - rc.right) <= tol && abs(sy - cy) <= tol * 2) return CANVAS_HANDLE_RIGHT;

    /* 3. Center Move Handle */
    float dist = hypotf((float)(sx - cx), (float)(sy - cy));
    if (dist <= 18.0f) {
        return CANVAS_HANDLE_CENTER_MOVE;
    }

    return CANVAS_HANDLE_NONE;
}

/* Helper to draw filled directional triangle with border */
static void draw_dir_triangle(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, COLORREF fill_col, COLORREF border_col) {
    POINT pts[3] = { {x1, y1}, {x2, y2}, {x3, y3} };
    HBRUSH br = CreateSolidBrush(fill_col);
    HPEN pen = CreatePen(PS_SOLID, 1, border_col);
    HBRUSH old_br = (HBRUSH)SelectObject(hdc, br);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);

    Polygon(hdc, pts, 3);

    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(br);
    DeleteObject(pen);
}

/* Visual rendering for Canvas transformation handles in Canvas Mode */
void render_canvas_transform_handles(HDC hdc, int client_w, int work_h, const ThemeColors *th) {
    if (!g_has_canvas) return;

    RECT rc;
    get_canvas_screen_rect(client_w, work_h, &rc);
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;

    /* 1. Highlight Outer Border Frame */
    HPEN frame_pen = CreatePen(PS_SOLID, 2, th->text_accent_blue);
    HPEN old_pen = (HPEN)SelectObject(hdc, frame_pen);
    HBRUSH old_br = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    Rectangle(hdc, rc.left - 1, rc.top - 1, rc.right + 2, rc.bottom + 2);

    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(frame_pen);

    /* 2. Floating Canvas Dimensions Badge above top edge */
    char sz_badge[64];
    snprintf(sz_badge, sizeof(sz_badge), "Canvas: %d x %d px", g_canvas_w, g_canvas_h);
    int badge_y = (rc.top - 24 >= TOOLBAR_HEIGHT + 4) ? (rc.top - 24) : (rc.top + 16);
    render_coord_badge(hdc, cx, badge_y, sz_badge, th);

    /* 3. Draw 8 Directional Arrow Triangle Handles */
    COLORREF norm_fill = th->text_accent_blue;
    COLORREF norm_border = RGB(20, 20, 25);
    COLORREF hot_fill = RGB(255, 215, 0);     /* Bright Yellow / Amber */
    COLORREF hot_border = RGB(40, 20, 0);
    COLORREF sel_fill = RGB(45, 220, 100);    /* Vibrant Emerald / Green for Active/Selected */
    COLORREF sel_border = RGB(15, 60, 25);

    /* Edge arrow pointers (triangle pointing outwards) */
    /* TOP */
    bool is_sel = g_canvas_handle_selected[CANVAS_HANDLE_TOP];
    bool is_hot = (g_hovered_canvas_handle == CANVAS_HANDLE_TOP || g_drag_canvas_handle == CANVAS_HANDLE_TOP);
    draw_dir_triangle(hdc, cx, rc.top - 14, cx - 8, rc.top - 2, cx + 8, rc.top - 2,
                      is_sel ? sel_fill : (is_hot ? hot_fill : norm_fill),
                      is_sel ? sel_border : (is_hot ? hot_border : norm_border));

    /* BOTTOM */
    is_sel = g_canvas_handle_selected[CANVAS_HANDLE_BOTTOM];
    is_hot = (g_hovered_canvas_handle == CANVAS_HANDLE_BOTTOM || g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM);
    draw_dir_triangle(hdc, cx, rc.bottom + 14, cx - 8, rc.bottom + 2, cx + 8, rc.bottom + 2,
                      is_sel ? sel_fill : (is_hot ? hot_fill : norm_fill),
                      is_sel ? sel_border : (is_hot ? hot_border : norm_border));

    /* LEFT */
    is_sel = g_canvas_handle_selected[CANVAS_HANDLE_LEFT];
    is_hot = (g_hovered_canvas_handle == CANVAS_HANDLE_LEFT || g_drag_canvas_handle == CANVAS_HANDLE_LEFT);
    draw_dir_triangle(hdc, rc.left - 14, cy, rc.left - 2, cy - 8, rc.left - 2, cy + 8,
                      is_sel ? sel_fill : (is_hot ? hot_fill : norm_fill),
                      is_sel ? sel_border : (is_hot ? hot_border : norm_border));

    /* RIGHT */
    is_sel = g_canvas_handle_selected[CANVAS_HANDLE_RIGHT];
    is_hot = (g_hovered_canvas_handle == CANVAS_HANDLE_RIGHT || g_drag_canvas_handle == CANVAS_HANDLE_RIGHT);
    draw_dir_triangle(hdc, rc.right + 14, cy, rc.right + 2, cy - 8, rc.right + 2, cy + 8,
                      is_sel ? sel_fill : (is_hot ? hot_fill : norm_fill),
                      is_sel ? sel_border : (is_hot ? hot_border : norm_border));

    /* Corner arrow pointers (pointing outward diagonally) */
    /* TOP_LEFT */
    is_sel = g_canvas_handle_selected[CANVAS_HANDLE_TOP_LEFT];
    is_hot = (g_hovered_canvas_handle == CANVAS_HANDLE_TOP_LEFT || g_drag_canvas_handle == CANVAS_HANDLE_TOP_LEFT);
    draw_dir_triangle(hdc, rc.left - 12, rc.top - 12, rc.left - 1, rc.top - 10, rc.left - 10, rc.top - 1,
                      is_sel ? sel_fill : (is_hot ? hot_fill : norm_fill),
                      is_sel ? sel_border : (is_hot ? hot_border : norm_border));

    /* TOP_RIGHT */
    is_sel = g_canvas_handle_selected[CANVAS_HANDLE_TOP_RIGHT];
    is_hot = (g_hovered_canvas_handle == CANVAS_HANDLE_TOP_RIGHT || g_drag_canvas_handle == CANVAS_HANDLE_TOP_RIGHT);
    draw_dir_triangle(hdc, rc.right + 12, rc.top - 12, rc.right + 1, rc.top - 10, rc.right + 10, rc.top - 1,
                      is_sel ? sel_fill : (is_hot ? hot_fill : norm_fill),
                      is_sel ? sel_border : (is_hot ? hot_border : norm_border));

    /* BOTTOM_LEFT */
    is_sel = g_canvas_handle_selected[CANVAS_HANDLE_BOTTOM_LEFT];
    is_hot = (g_hovered_canvas_handle == CANVAS_HANDLE_BOTTOM_LEFT || g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM_LEFT);
    draw_dir_triangle(hdc, rc.left - 12, rc.bottom + 12, rc.left - 1, rc.bottom + 10, rc.left - 10, rc.bottom + 1,
                      is_sel ? sel_fill : (is_hot ? hot_fill : norm_fill),
                      is_sel ? sel_border : (is_hot ? hot_border : norm_border));

    /* BOTTOM_RIGHT */
    is_sel = g_canvas_handle_selected[CANVAS_HANDLE_BOTTOM_RIGHT];
    is_hot = (g_hovered_canvas_handle == CANVAS_HANDLE_BOTTOM_RIGHT || g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM_RIGHT);
    draw_dir_triangle(hdc, rc.right + 12, rc.bottom + 12, rc.right + 1, rc.bottom + 10, rc.right + 10, rc.bottom + 1,
                      is_sel ? sel_fill : (is_hot ? hot_fill : norm_fill),
                      is_sel ? sel_border : (is_hot ? hot_border : norm_border));

    /* 4. Center Move 4-Way Cross Arrows Handle (Move frame over scene - Uniform Color Palette) */
    bool is_move_sel = g_canvas_handle_selected[CANVAS_HANDLE_CENTER_MOVE];
    bool is_move_hot = (g_hovered_canvas_handle == CANVAS_HANDLE_CENTER_MOVE || g_drag_canvas_handle == CANVAS_HANDLE_CENTER_MOVE);
    COLORREF move_fill = is_move_sel ? sel_fill : (is_move_hot ? hot_fill : norm_fill);
    COLORREF move_border = is_move_sel ? sel_border : (is_move_hot ? hot_border : norm_border);

    /* Central hub circle */
    HBRUSH move_br = CreateSolidBrush(move_fill);
    HPEN move_pen = CreatePen(PS_SOLID, 1, move_border);
    HPEN old_mp = (HPEN)SelectObject(hdc, move_pen);
    HBRUSH old_mb = (HBRUSH)SelectObject(hdc, move_br);

    Ellipse(hdc, cx - 4, cy - 4, cx + 5, cy + 5);

    /* 4-way cross lines */
    HPEN cross_pen = CreatePen(PS_SOLID, 2, move_fill);
    SelectObject(hdc, cross_pen);
    MoveToEx(hdc, cx - 13, cy, NULL); LineTo(hdc, cx + 14, cy);
    MoveToEx(hdc, cx, cy - 13, NULL); LineTo(hdc, cx, cy + 14);

    SelectObject(hdc, old_mb);
    SelectObject(hdc, old_mp);
    DeleteObject(cross_pen);
    DeleteObject(move_br);
    DeleteObject(move_pen);

    /* 4-way arrowheads */
    draw_dir_triangle(hdc, cx, cy - 16, cx - 3, cy - 11, cx + 3, cy - 11, move_fill, move_border);
    draw_dir_triangle(hdc, cx, cy + 16, cx - 3, cy + 11, cx + 3, cy + 11, move_fill, move_border);
    draw_dir_triangle(hdc, cx - 16, cy, cx - 11, cy - 3, cx - 11, cy + 3, move_fill, move_border);
    draw_dir_triangle(hdc, cx + 16, cy, cx + 11, cy - 3, cx + 11, cy + 3, move_fill, move_border);

    /* Tooltip badge if hovered or selected */
    if (is_move_hot || is_move_sel) {
        if (is_move_sel) {
            render_coord_badge(hdc, cx, cy + 48, "Center Move [ACTIVE] (Arrow keys to nudge 1px)", th);
        } else {
            render_coord_badge(hdc, cx, cy + 48, "Center Move (Shift to Snap 45 deg | Ctrl+Click to Select)", th);
        }
    }
}

/* Placed Coins / Treasure Points Renderer */
static void render_placed_coins(HDC mem_dc, const RECT *rc_canvas, const ThemeColors *th) {
    load_coin_sprite();
    if (!g_has_coin_img || !g_coin_img.hbm || !g_coin_img.hdc_mem) return;
    int cw = (int)roundf((float)g_coin_img.width * g_zoom);
    int ch = (int)roundf((float)g_coin_img.height * g_zoom);

    for (int i = 0; i < g_treasure_point_count; ++i) {
        int center_sx = rc_canvas->left + (int)roundf((float)g_treasure_points[i].x * g_zoom);
        int center_sy = rc_canvas->top + (int)roundf((float)g_treasure_points[i].y * g_zoom);
        int cx = center_sx - cw / 2;
        int cy = center_sy - ch / 2;

        bool is_coin_hovered = (!g_level_preview_active && !g_placing_coin && !g_placing_frog && g_mode_element && g_hovered_coin_idx == i);
        BYTE alpha = is_coin_hovered ? 128 : 255;

        BLENDFUNCTION bf_coin = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
        GdiAlphaBlend(mem_dc, cx, cy, cw, ch,
                      g_coin_img.hdc_mem, 0, 0, g_coin_img.width, g_coin_img.height, bf_coin);

        if (!g_level_preview_active && g_treasure_points[i].selected) {
            draw_object_selection_indicator(mem_dc, cx, cy, cw, ch, th->text_accent_blue);
        }
    }
}

static void render_placed_coin_hover_badge(HDC mem_dc, const RECT *rc_canvas, const ThemeColors *th) {
    load_coin_sprite();
    if (!g_has_coin_img || g_level_preview_active || g_placing_coin || g_placing_frog || !g_mode_element) return;
    if (g_hovered_coin_idx < 0 || g_hovered_coin_idx >= g_treasure_point_count) return;

    int i = g_hovered_coin_idx;
    int ch = (int)roundf((float)g_coin_img.height * g_zoom);
    int center_sx = rc_canvas->left + (int)roundf((float)g_treasure_points[i].x * g_zoom);
    int center_sy = rc_canvas->top + (int)roundf((float)g_treasure_points[i].y * g_zoom);

    HPEN hover_pen = CreatePen(PS_SOLID, 1, th->text_accent_blue);
    HPEN old_pen_c = (HPEN)SelectObject(mem_dc, hover_pen);
    MoveToEx(mem_dc, center_sx - 6, center_sy, NULL);
    LineTo(mem_dc, center_sx + 7, center_sy);
    MoveToEx(mem_dc, center_sx, center_sy - 6, NULL);
    LineTo(mem_dc, center_sx, center_sy + 7);
    SelectObject(mem_dc, old_pen_c);
    DeleteObject(hover_pen);

    /* On-canvas coordinate badge on hover */
    char coin_badge[64];
    snprintf(coin_badge, sizeof(coin_badge), "Coin #%d [X: %d, Y: %d]", i + 1, g_treasure_points[i].x, g_treasure_points[i].y);
    int badge_y = (center_sy - ch / 2 - 24 >= rc_canvas->top + 4) ? (center_sy - ch / 2 - 24) : (center_sy + ch / 2 + 8);
    render_coord_badge(mem_dc, center_sx, badge_y, coin_badge, th);
}

/* Placed Frog Shooter Renderer */
static void render_placed_frog(HDC mem_dc, const RECT *rc_canvas, const ThemeColors *th) {
    load_frog_sprite();
    if (!g_has_frog || g_placing_frog || !g_has_frog_img || !g_frog_img.hbm || !g_frog_img.hdc_mem) return;
    int center_sx = rc_canvas->left + (int)roundf((float)g_frog_gx * g_zoom);
    int center_sy = rc_canvas->top + (int)roundf((float)g_frog_gy * g_zoom);
    int fw = (int)roundf((float)g_frog_img.width * g_zoom);
    int fh = (int)roundf((float)g_frog_img.height * g_zoom);
    int fx = center_sx - fw / 2;
    int fy = center_sy - fh / 2;

    bool is_frog_hovered = (!g_level_preview_active && g_frog_hovered && g_mode_element);
    BYTE alpha = is_frog_hovered ? 128 : 255;
    BLENDFUNCTION bf_frog = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    GdiAlphaBlend(mem_dc, fx, fy, fw, fh,
                  g_frog_img.hdc_mem, 0, 0, g_frog_img.width, g_frog_img.height, bf_frog);

    if (!g_level_preview_active && g_frog_selected) {
        draw_object_selection_indicator(mem_dc, fx, fy, fw, fh, th->text_accent_blue);
    }
}

static void render_placed_frog_hover_badge(HDC mem_dc, const RECT *rc_canvas, const ThemeColors *th) {
    load_frog_sprite();
    if (!g_has_frog || g_placing_frog || !g_has_frog_img || g_level_preview_active) return;
    bool is_frog_hovered = (g_frog_hovered && g_mode_element);
    if (!is_frog_hovered) return;

    int center_sx = rc_canvas->left + (int)roundf((float)g_frog_gx * g_zoom);
    int center_sy = rc_canvas->top + (int)roundf((float)g_frog_gy * g_zoom);
    int fh = (int)roundf((float)g_frog_img.height * g_zoom);

    HPEN hover_pen = CreatePen(PS_SOLID, 1, th->text_accent_blue);
    HPEN old_pen_h = (HPEN)SelectObject(mem_dc, hover_pen);
    MoveToEx(mem_dc, center_sx - 6, center_sy, NULL);
    LineTo(mem_dc, center_sx + 7, center_sy);
    MoveToEx(mem_dc, center_sx, center_sy - 6, NULL);
    LineTo(mem_dc, center_sx, center_sy + 7);
    SelectObject(mem_dc, old_pen_h);
    DeleteObject(hover_pen);

    /* On-canvas coordinate badge on hover */
    char frog_badge[64];
    snprintf(frog_badge, sizeof(frog_badge), "Frog [X: %d, Y: %d]", g_frog_gx, g_frog_gy);
    int badge_y = (center_sy - fh / 2 - 24 >= rc_canvas->top + 4) ? (center_sy - fh / 2 - 24) : (center_sy + fh / 2 + 8);
    render_coord_badge(mem_dc, center_sx, badge_y, frog_badge, th);
}

void render_splash_elements(HDC mem_dc, int view_w, int view_h, const ThemeColors *th, int offset_y, bool is_workspace) {
    load_splash_image();

    int banner_w = 580;
    int banner_h = 290;
    int pill_w = 340;
    int pill_h = 34;
    int card_w = 184;
    int card_h = 74;
    int card_gap = 14;
    int total_cards_w = 3 * card_w + 2 * card_gap;

    int total_splash_h = banner_h + 14 + pill_h + 14 + card_h;
    int start_y = offset_y + (view_h - total_splash_h) / 2;
    if (start_y < offset_y + 12) start_y = offset_y + 12;

    /* 1. Splash Banner Image (ZuSPLASHscreen.jpg) */
    int banner_x = (view_w - banner_w) / 2;
    int banner_y = start_y;

    if (g_has_splash_img && g_splash_img.hbm && g_splash_img.hdc_mem) {
        SetStretchBltMode(mem_dc, HALFTONE);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        GdiAlphaBlend(mem_dc, banner_x, banner_y, banner_w, banner_h,
                      g_splash_img.hdc_mem, 0, 0, g_splash_img.width, g_splash_img.height, bf);

        /* Subtle crisp border around splash banner */
        HPEN img_pen = CreatePen(PS_SOLID, 1, th->bar_border);
        HPEN old_pen = (HPEN)SelectObject(mem_dc, img_pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        RoundRect(mem_dc, banner_x - 1, banner_y - 1, banner_x + banner_w + 1, banner_y + banner_h + 1, 6, 6);
        SelectObject(mem_dc, old_pen);
        DeleteObject(img_pen);

        /* Version badge "v0.1.1" at bottom-right corner of splash banner image */
        int ver_w = 56;
        int ver_h = 20;
        int ver_x = banner_x + banner_w - ver_w - 8;
        int ver_y = banner_y + banner_h - ver_h - 8;
        RECT rc_ver = { ver_x, ver_y, ver_x + ver_w, ver_y + ver_h };

        HBRUSH br_ver = CreateSolidBrush(RGB(18, 20, 26));
        HPEN pen_ver = CreatePen(PS_SOLID, 1, RGB(70, 76, 88));
        HPEN old_p_ver = (HPEN)SelectObject(mem_dc, pen_ver);
        HBRUSH old_b_ver = (HBRUSH)SelectObject(mem_dc, br_ver);
        RoundRect(mem_dc, rc_ver.left, rc_ver.top, rc_ver.right, rc_ver.bottom, 6, 6);
        SelectObject(mem_dc, old_b_ver);
        SelectObject(mem_dc, old_p_ver);
        DeleteObject(br_ver);
        DeleteObject(pen_ver);

        SetBkMode(mem_dc, TRANSPARENT);
        SelectObject(mem_dc, g_font_mono_bold ? g_font_mono_bold : g_font_primary_bold);
        SetTextColor(mem_dc, RGB(245, 248, 252));
        DrawTextA(mem_dc, ZUPATHER_VERSION_STR, -1, &rc_ver, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }

    /* 2. GitHub Link Pill Button ("Developed by Rizky Bayuu" -> GitHub ↗) */
    int pill_x = (view_w - pill_w) / 2;
    int pill_y = banner_y + banner_h + 14;

    RECT rc_pill = { pill_x, pill_y, pill_x + pill_w, pill_y + pill_h };
    g_splash_rc_github = rc_pill;

    bool is_dark = (g_current_theme == THEME_DARK);

    bool is_gh_hover = (g_splash_hover_item == 1);
    COLORREF gh_bg = is_gh_hover ? (is_dark ? RGB(45, 52, 64) : RGB(225, 234, 246))
                                 : (is_dark ? RGB(32, 36, 44) : RGB(255, 255, 255));
    COLORREF gh_border = is_gh_hover ? th->text_accent_blue : th->bar_border;
    COLORREF gh_text = is_gh_hover ? th->text_accent_blue : th->text_primary;

    HBRUSH br_gh = CreateSolidBrush(gh_bg);
    HPEN pen_gh = CreatePen(PS_SOLID, 1, gh_border);
    HPEN old_pen_gh = (HPEN)SelectObject(mem_dc, pen_gh);
    HBRUSH old_br_gh = (HBRUSH)SelectObject(mem_dc, br_gh);
    RoundRect(mem_dc, rc_pill.left, rc_pill.top, rc_pill.right, rc_pill.bottom, 16, 16);
    SelectObject(mem_dc, old_br_gh);
    SelectObject(mem_dc, old_pen_gh);
    DeleteObject(br_gh);
    DeleteObject(pen_gh);

    /* Draw vector GitHub SVG logo */
    draw_github_svg_logo(mem_dc, pill_x + 14, pill_y + 8, 18, gh_text);

    /* Draw "Developed by Rizky Bayuu" text */
    SetBkMode(mem_dc, TRANSPARENT);
    SelectObject(mem_dc, g_font_primary_bold);
    SetTextColor(mem_dc, gh_text);
    RECT rc_gh_txt = { pill_x + 38, pill_y, pill_x + 242, pill_y + pill_h };
    DrawTextA(mem_dc, "Developed by Rizky Bayuu", -1, &rc_gh_txt, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    /* Divider */
    HPEN pen_div = CreatePen(PS_SOLID, 1, is_gh_hover ? th->text_accent_blue : th->bar_border);
    HPEN old_p_div = (HPEN)SelectObject(mem_dc, pen_div);
    MoveToEx(mem_dc, pill_x + 246, pill_y + 7, NULL);
    LineTo(mem_dc, pill_x + 246, pill_y + pill_h - 7);
    SelectObject(mem_dc, old_p_div);
    DeleteObject(pen_div);

    /* GitHub link mark */
    SelectObject(mem_dc, g_font_mono_bold ? g_font_mono_bold : g_font_primary_bold);
    RECT rc_gh_arr = { pill_x + 252, pill_y, pill_x + pill_w - 12, pill_y + pill_h };
    DrawTextA(mem_dc, "GitHub >", -1, &rc_gh_arr, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

    /* 3. Three Action Start Cards (New Canvas, Open Canvas, Map Manager) */
    int cards_start_x = (view_w - total_cards_w) / 2;
    int cards_y = pill_y + pill_h + 14;

    struct {
        int id;
        const char *title;
        const char *subtitle;
        const char *shortcut;
        RECT *out_rc;
    } actions[3] = {
        { 2, "New Canvas",   "Create new level map", "Ctrl+N",        &g_splash_rc_new },
        { 3, "Open Canvas",  "Open canvas project",  "Ctrl+O",        &g_splash_rc_open },
        { 4, "Map Manager",  "Browse & sync maps",   "Ctrl+Shift+M",  &g_splash_rc_map },
    };

    for (int i = 0; i < 3; ++i) {
        int cx = cards_start_x + i * (card_w + card_gap);
        RECT rc_card = { cx, cards_y, cx + card_w, cards_y + card_h };
        *(actions[i].out_rc) = rc_card;

        bool is_card_hover = (g_splash_hover_item == actions[i].id);
        COLORREF card_bg = is_card_hover ? (is_dark ? RGB(42, 49, 62) : RGB(236, 243, 254))
                                         : (is_dark ? RGB(32, 36, 44) : RGB(255, 255, 255));
        COLORREF card_border = is_card_hover ? th->text_accent_blue : th->bar_border;
        COLORREF title_col = is_card_hover ? th->text_accent_blue : th->text_primary;
        COLORREF sub_col = th->text_secondary;

        HBRUSH br_c = CreateSolidBrush(card_bg);
        HPEN pen_c = CreatePen(PS_SOLID, 1, card_border);
        HPEN old_p_c = (HPEN)SelectObject(mem_dc, pen_c);
        HBRUSH old_b_c = (HBRUSH)SelectObject(mem_dc, br_c);
        RoundRect(mem_dc, rc_card.left, rc_card.top, rc_card.right, rc_card.bottom, 8, 8);
        SelectObject(mem_dc, old_b_c);
        SelectObject(mem_dc, old_p_c);
        DeleteObject(br_c);
        DeleteObject(pen_c);

        /* Card Title */
        SelectObject(mem_dc, g_font_primary_bold);
        SetTextColor(mem_dc, title_col);
        RECT rc_t = { cx + 12, cards_y + 10, cx + card_w - 12, cards_y + 28 };
        DrawTextA(mem_dc, actions[i].title, -1, &rc_t, DT_SINGLELINE | DT_LEFT | DT_TOP);

        /* Card Subtitle */
        SelectObject(mem_dc, g_font_primary_regular);
        SetTextColor(mem_dc, sub_col);
        RECT rc_s = { cx + 12, cards_y + 30, cx + card_w - 12, cards_y + 46 };
        DrawTextA(mem_dc, actions[i].subtitle, -1, &rc_s, DT_SINGLELINE | DT_LEFT | DT_TOP);

        /* Shortcut Badge */
        SelectObject(mem_dc, g_font_mono_regular);
        SIZE sz_sc;
        GetTextExtentPoint32A(mem_dc, actions[i].shortcut, (int)strlen(actions[i].shortcut), &sz_sc);
        int badge_pad_x = 6;
        int badge_w = sz_sc.cx + badge_pad_x * 2;
        int badge_h = sz_sc.cy + 4;
        int badge_x = cx + 12;
        int badge_y = cards_y + card_h - badge_h - 8;
        RECT rc_badge = { badge_x, badge_y, badge_x + badge_w, badge_y + badge_h };

        COLORREF badge_bg = is_dark ? RGB(24, 27, 33) : RGB(240, 243, 248);
        HBRUSH br_bd = CreateSolidBrush(badge_bg);
        HPEN pen_bd = CreatePen(PS_SOLID, 1, th->btn_border);
        HPEN old_p_bd = (HPEN)SelectObject(mem_dc, pen_bd);
        HBRUSH old_b_bd = (HBRUSH)SelectObject(mem_dc, br_bd);
        RoundRect(mem_dc, rc_badge.left, rc_badge.top, rc_badge.right, rc_badge.bottom, 4, 4);
        SelectObject(mem_dc, old_b_bd);
        SelectObject(mem_dc, old_p_bd);
        DeleteObject(br_bd);
        DeleteObject(pen_bd);

        SetTextColor(mem_dc, th->text_secondary);
        DrawTextA(mem_dc, actions[i].shortcut, -1, &rc_badge, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
}

/* Render Workspace & Canvas Scene */
void render_scene(HWND hwnd, HDC hdc, int client_w, int client_h) {
    const ThemeColors *th = get_theme();
    int work_h = get_workspace_height(hwnd);

    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bm = CreateCompatibleBitmap(hdc, client_w, client_h);
    HBITMAP old_bm = (HBITMAP)SelectObject(mem_dc, mem_bm);

    /* 1. Workspace Background */
    RECT rc_work = {0, TOOLBAR_HEIGHT, client_w, TOOLBAR_HEIGHT + work_h};
    HBRUSH bg_brush = CreateSolidBrush(th->workspace_bg);
    FillRect(mem_dc, &rc_work, bg_brush);
    DeleteObject(bg_brush);

    if (!g_has_canvas) {
        SetBkMode(mem_dc, TRANSPARENT);
        HFONT hf_title = g_font_title_bold ? g_font_title_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT old_f = (HFONT)SelectObject(mem_dc, hf_title);
        SetTextColor(mem_dc, th->text_primary);

        int mid_y = TOOLBAR_HEIGHT + work_h / 2 - 28;
        RECT rc_text1 = { 0, mid_y, client_w, mid_y + 30 };
        DrawTextA(mem_dc, "No Active Canvas", -1, &rc_text1, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        HFONT hf_body = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SelectObject(mem_dc, hf_body);
        SetTextColor(mem_dc, th->text_secondary);

        RECT rc_text2 = { 0, mid_y + 32, client_w, mid_y + 58 };
        DrawTextA(mem_dc, "Press Ctrl+N to create a new canvas, Ctrl+O to open an existing project, or Ctrl+Shift+M for Map Manager", -1, &rc_text2, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        SelectObject(mem_dc, old_f);

        BitBlt(hdc, 0, TOOLBAR_HEIGHT, client_w, work_h, mem_dc, 0, TOOLBAR_HEIGHT, SRCCOPY);
        SelectObject(mem_dc, old_bm);
        DeleteObject(mem_bm);
        DeleteDC(mem_dc);
        return;
    }

    /* 2. Canvas Geometry */
    RECT rc_canvas;
    get_canvas_screen_rect(client_w, work_h, &rc_canvas);

    /* 3. Outside Background Image (Anchored by Fit/Alignment - 50% Opacity) */
    if (g_has_bg_image && g_canvas_img.hbm && g_canvas_img.hdc_mem) {
        int place_x = 0, place_y = 0, place_w = g_canvas_img.width, place_h = g_canvas_img.height;
        get_canvas_image_dest_rect(
            g_canvas_w, g_canvas_h,
            g_canvas_img.width, g_canvas_img.height,
            g_canvas_fit_mode, g_canvas_alignment,
            &place_x, &place_y, &place_w, &place_h
        );

        int img_sx = rc_canvas.left + (int)roundf((float)place_x * g_zoom);
        int img_sy = rc_canvas.top  + (int)roundf((float)place_y * g_zoom);
        int img_sw = (int)roundf((float)place_w * g_zoom);
        int img_sh = (int)roundf((float)place_h * g_zoom);

        BLENDFUNCTION bf_50 = { AC_SRC_OVER, 0, 128, AC_SRC_ALPHA };
        GdiAlphaBlend(mem_dc, img_sx, img_sy, img_sw, img_sh,
                      g_canvas_img.hdc_mem, 0, 0, g_canvas_img.width, g_canvas_img.height, bf_50);
    }

    /* 4. Canvas Body Sheet (Covers the outside 50% image inside canvas area) */
    HBRUSH canvas_brush = CreateSolidBrush(th->canvas_bg);
    FillRect(mem_dc, &rc_canvas, canvas_brush);
    DeleteObject(canvas_brush);

    /* 5. Inside Background Image (Anchored by Fit/Alignment - 100% Full Opacity, clipped to canvas) */
    if (g_has_bg_image && g_canvas_img.hbm && g_canvas_img.hdc_mem) {
        HRGN hrgn_cv = CreateRectRgn(rc_canvas.left, rc_canvas.top, rc_canvas.right, rc_canvas.bottom);
        SelectClipRgn(mem_dc, hrgn_cv);

        int place_x = 0, place_y = 0, place_w = g_canvas_img.width, place_h = g_canvas_img.height;
        get_canvas_image_dest_rect(
            g_canvas_w, g_canvas_h,
            g_canvas_img.width, g_canvas_img.height,
            g_canvas_fit_mode, g_canvas_alignment,
            &place_x, &place_y, &place_w, &place_h
        );

        int img_sx = rc_canvas.left + (int)roundf((float)place_x * g_zoom);
        int img_sy = rc_canvas.top  + (int)roundf((float)place_y * g_zoom);
        int img_sw = (int)roundf((float)place_w * g_zoom);
        int img_sh = (int)roundf((float)place_h * g_zoom);

        BLENDFUNCTION bf_100 = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        GdiAlphaBlend(mem_dc, img_sx, img_sy, img_sw, img_sh,
                      g_canvas_img.hdc_mem, 0, 0, g_canvas_img.width, g_canvas_img.height, bf_100);

        SelectClipRgn(mem_dc, NULL);
        DeleteObject(hrgn_cv);
    }

    /* 6. Placed BackgroundAlpha Elements (Bottom-most overlay layer) */
    int hovered_ba_idx = -1;
    int adjusting_ba_idx = -1;
    if (!g_level_preview_active) {
        load_default_bgalpha_sprite();
        for (int i = 0; i < g_bg_alpha_count; ++i) {
            LevelBackgroundAlpha *ba = &g_bg_alphas[i];
            HDC ba_hdc = ba->has_img ? ba->img.hdc_mem : (g_has_default_bgalpha_img ? g_default_bgalpha_img.hdc_mem : NULL);
            int img_w = ba->has_img ? ba->img.width : (g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 160);
            int img_h = ba->has_img ? ba->img.height : (g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 140);

            if (ba_hdc) {
                int sx = rc_canvas.left + (int)roundf((float)ba->x * g_zoom);
                int sy = rc_canvas.top + (int)roundf((float)ba->y * g_zoom);
                int sw = (int)roundf((float)img_w * g_zoom);
                int sh = (int)roundf((float)img_h * g_zoom);

                bool is_hovered = (g_mode_overlay && !g_placing_bgalpha && !g_placing_cutout && !g_placing_sprite &&
                                   !g_placing_bgalpha_vector && !g_adjusting_overlay_priority &&
                                   g_hovered_bgalpha_idx == i);
                bool is_adjusting = (g_placing_bgalpha_vector && g_editing_bgalpha_idx == i);
                if (is_hovered) hovered_ba_idx = i;
                if (is_adjusting) adjusting_ba_idx = i;

                if ((is_hovered || is_adjusting) && ba_hdc) {
                    /* Hovered state: 50% opacity normal appearance (clearly visible) */
                    BLENDFUNCTION bf_ba = { AC_SRC_OVER, 0, 128, AC_SRC_ALPHA };
                    GdiAlphaBlend(mem_dc, sx, sy, sw, sh,
                                  ba_hdc, 0, 0, img_w, img_h, bf_ba);
                } else {
                    /* Placed & Unhovered (both inside Overlay Mode and outside Overlay Mode):
                       25% opacity Red */
                    if (!ba->hdc_mask_light) {
                        bgalpha_generate_masks(ba);
                    }
                    HDC mask_light = ba->hdc_mask_light ? ba->hdc_mask_light : g_default_bgalpha_hdc_light;
                    HDC mask_dark = ba->hdc_mask_dark ? ba->hdc_mask_dark : g_default_bgalpha_hdc_dark;
                    HDC active_mask = (g_current_theme == THEME_DARK) ? mask_dark : mask_light;

                    if (active_mask) {
                        BLENDFUNCTION bf_mask = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                        GdiAlphaBlend(mem_dc, sx, sy, sw, sh,
                                      active_mask, 0, 0, img_w, img_h, bf_mask);
                    } else {
                        /* Fallback to 25% normal image (alpha=64) */
                        BLENDFUNCTION bf_ba = { AC_SRC_OVER, 0, 64, AC_SRC_ALPHA };
                        GdiAlphaBlend(mem_dc, sx, sy, sw, sh,
                                      ba_hdc, 0, 0, img_w, img_h, bf_ba);
                    }
                }

                if (ba->selected) {
                    draw_object_selection_indicator(mem_dc, sx, sy, sw, sh, th->text_accent_blue);
                }
            }
        }
    }

    /* 7. Placed Sprites (Top-Left aligned, 100% opacity normal, 50% opacity when hovered) */
    int hovered_sp_idx = -1;
    int adjusting_sp_idx = -1;
    if (!g_level_preview_active) {
        load_default_sprite_sprite();
        for (int i = 0; i < g_sprite_count; ++i) {
            LevelSprite *sp = &g_sprites[i];
            HDC sp_hdc = sp->has_img ? sp->img.hdc_mem : (g_has_default_sprite_img ? g_default_sprite_img.hdc_mem : NULL);
            int img_w = sp->has_img ? sp->img.width : (g_has_default_sprite_img ? g_default_sprite_img.width : 120);
            int img_h = sp->has_img ? sp->img.height : (g_has_default_sprite_img ? g_default_sprite_img.height : 120);

            if (sp_hdc) {
                int sx = rc_canvas.left + (int)roundf((float)sp->x * g_zoom);
                int sy = rc_canvas.top + (int)roundf((float)sp->y * g_zoom);
                int sw = (int)roundf((float)img_w * g_zoom);
                int sh = (int)roundf((float)img_h * g_zoom);

                bool is_hovered = (g_mode_overlay && !g_placing_sprite && !g_placing_cutout &&
                                   !g_adjusting_overlay_priority && !g_placing_bgalpha_vector &&
                                   g_hovered_sprite_idx == i);
                bool is_adjusting = (g_adjusting_overlay_priority && g_editing_prio_type == 1 && g_editing_prio_idx == i);
                if (is_hovered) hovered_sp_idx = i;
                if (is_adjusting) adjusting_sp_idx = i;
                BYTE alpha = (is_hovered || is_adjusting) ? 128 : 255;

                BLENDFUNCTION bf_sp = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
                GdiAlphaBlend(mem_dc, sx, sy, sw, sh,
                              sp_hdc, 0, 0, img_w, img_h, bf_sp);

                if (g_mode_overlay && !g_hud_overlay_collapsed && g_overlay_active_tab == 1 && g_selected_sprite_idx == i && !is_hovered && !is_adjusting) {
                    draw_topleft_corner_brackets(mem_dc, sx, sy, sw, sh, th->text_secondary);
                }

                if (sp->selected) {
                    draw_object_selection_indicator(mem_dc, sx, sy, sw, sh, th->text_accent_blue);
                }
            }
        }
    }

    /* 8. Placed Cutouts (Top-Left aligned: Black 100% transparent, White 25% black (Light) / 25% white (Dark); 50% hovered) */
    int hovered_co_idx = -1;
    int adjusting_co_idx = -1;
    if (!g_level_preview_active) {
        load_default_cutout_sprite();
        for (int i = 0; i < g_cutout_count; ++i) {
            LevelCutout *co = &g_cutouts[i];
            HDC co_hdc = co->has_img ? co->img.hdc_mem : (g_has_default_cutout_img ? g_default_cutout_img.hdc_mem : NULL);
            HDC mask_light = co->hdc_mask_light ? co->hdc_mask_light : g_default_cutout_hdc_light;
            HDC mask_dark = co->hdc_mask_dark ? co->hdc_mask_dark : g_default_cutout_hdc_dark;
            HDC active_mask = (g_current_theme == THEME_DARK) ? mask_dark : mask_light;

            int img_w = co->has_img ? co->img.width : (g_has_default_cutout_img ? g_default_cutout_img.width : 160);
            int img_h = co->has_img ? co->img.height : (g_has_default_cutout_img ? g_default_cutout_img.height : 140);

            int sx = rc_canvas.left + (int)roundf((float)co->x * g_zoom);
            int sy = rc_canvas.top + (int)roundf((float)co->y * g_zoom);
            int sw = (int)roundf((float)img_w * g_zoom);
            int sh = (int)roundf((float)img_h * g_zoom);

            bool is_hovered = (g_mode_overlay && !g_placing_cutout && !g_placing_sprite &&
                               !g_adjusting_overlay_priority && !g_placing_bgalpha_vector &&
                               g_hovered_cutout_idx == i);
            bool is_adjusting = (g_adjusting_overlay_priority && g_editing_prio_type == 0 && g_editing_prio_idx == i);
            if (is_hovered) hovered_co_idx = i;
            if (is_adjusting) adjusting_co_idx = i;

            if ((is_hovered || is_adjusting) && co_hdc) {
                /* Hovered state: 50% opacity normal appearance */
                BLENDFUNCTION bf_co = { AC_SRC_OVER, 0, 128, AC_SRC_ALPHA };
                GdiAlphaBlend(mem_dc, sx, sy, sw, sh,
                              co_hdc, 0, 0, img_w, img_h, bf_co);
            } else if (active_mask) {
                /* Placed & Unhovered: Black is 100% transparent, White is 25% black (Light Mode) or 25% white (Dark Mode) */
                BLENDFUNCTION bf_mask = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                GdiAlphaBlend(mem_dc, sx, sy, sw, sh,
                              active_mask, 0, 0, img_w, img_h, bf_mask);

                if (g_mode_overlay && !g_hud_overlay_collapsed && g_overlay_active_tab == 0 && g_selected_cutout_idx == i && !is_hovered && !is_adjusting) {
                    draw_topleft_corner_brackets(mem_dc, sx, sy, sw, sh, th->text_secondary);
                }
            }

            if (co->selected) {
                draw_object_selection_indicator(mem_dc, sx, sy, sw, sh, th->text_accent_blue);
            }
        }
    }

    /* 9. Placed Coins / Treasure Points (Edit Mode: under curves & hover tooltips) */
    if (!g_level_preview_active) {
        render_placed_coins(mem_dc, &rc_canvas, th);
    }

    /* 10. Placed Frog (Edit Mode: under curves & hover tooltips) */
    if (!g_level_preview_active) {
        render_placed_frog(mem_dc, &rc_canvas, th);
    }

    /* 11. Adaptive Grid Rendering (Rendered IN FRONT OF placed elements, strictly under hover badges and curves) */
    if (g_show_grid) {
        int step_major = 40;
        int step_minor = 20;
        get_adaptive_grid_steps(g_zoom, &step_major, &step_minor);

        HPEN pen_ws_major = CreatePen(PS_SOLID, 1, th->workspace_grid_major);
        HPEN pen_ws_minor = CreatePen(PS_SOLID, 1, th->workspace_grid_minor);
        HPEN pen_cv_major = CreatePen(PS_SOLID, 1, th->canvas_grid_major);
        HPEN pen_cv_minor = CreatePen(PS_SOLID, 1, th->canvas_grid_minor);
        HPEN old_pen = (HPEN)GetCurrentObject(mem_dc, OBJ_PEN);

        /* 11.1 Outside Grid (Strictly clipped to workspace excluding canvas) */
        HRGN hrgn_total = CreateRectRgn(0, TOOLBAR_HEIGHT, client_w, TOOLBAR_HEIGHT + work_h);
        HRGN hrgn_cv = CreateRectRgn(rc_canvas.left, rc_canvas.top, rc_canvas.right, rc_canvas.bottom);
        HRGN hrgn_outside = CreateRectRgn(0, 0, 0, 0);
        CombineRgn(hrgn_outside, hrgn_total, hrgn_cv, RGN_DIFF);
        SelectClipRgn(mem_dc, hrgn_outside);

        float min_cx = ((float)0 - (float)rc_canvas.left) / g_zoom;
        float max_cx = ((float)client_w - (float)rc_canvas.left) / g_zoom;
        int start_cx = (int)floorf(min_cx / (float)step_minor) * step_minor;
        int end_cx   = (int)ceilf(max_cx / (float)step_minor) * step_minor;

        float min_cy = ((float)TOOLBAR_HEIGHT - (float)rc_canvas.top) / g_zoom;
        float max_cy = ((float)(TOOLBAR_HEIGHT + work_h) - (float)rc_canvas.top) / g_zoom;
        int start_cy = (int)floorf(min_cy / (float)step_minor) * step_minor;
        int end_cy   = (int)ceilf(max_cy / (float)step_minor) * step_minor;

        for (int cx = start_cx; cx <= end_cx; cx += step_minor) {
            int sx = rc_canvas.left + (int)roundf((float)cx * g_zoom);
            if (sx < 0 || sx > client_w) continue;
            SelectObject(mem_dc, (cx % step_major == 0) ? pen_ws_major : pen_ws_minor);
            MoveToEx(mem_dc, sx, TOOLBAR_HEIGHT, NULL);
            LineTo(mem_dc, sx, TOOLBAR_HEIGHT + work_h);
        }

        for (int cy = start_cy; cy <= end_cy; cy += step_minor) {
            int sy = rc_canvas.top + (int)roundf((float)cy * g_zoom);
            if (sy < TOOLBAR_HEIGHT || sy > TOOLBAR_HEIGHT + work_h) continue;
            SelectObject(mem_dc, (cy % step_major == 0) ? pen_ws_major : pen_ws_minor);
            MoveToEx(mem_dc, 0, sy, NULL);
            LineTo(mem_dc, client_w, sy);
        }

        /* 11.2 Inside Grid (Strictly clipped inside canvas, drawn on top of canvas bg & elements) */
        SelectClipRgn(mem_dc, hrgn_cv);

        for (int cx = 0; cx <= g_canvas_w; cx += step_minor) {
            int sx = rc_canvas.left + (int)roundf((float)cx * g_zoom);
            SelectObject(mem_dc, (cx % step_major == 0) ? pen_cv_major : pen_cv_minor);
            MoveToEx(mem_dc, sx, rc_canvas.top, NULL);
            LineTo(mem_dc, sx, rc_canvas.bottom);
        }

        for (int cy = 0; cy <= g_canvas_h; cy += step_minor) {
            int sy = rc_canvas.top + (int)roundf((float)cy * g_zoom);
            SelectObject(mem_dc, (cy % step_major == 0) ? pen_cv_major : pen_cv_minor);
            MoveToEx(mem_dc, rc_canvas.left, sy, NULL);
            LineTo(mem_dc, rc_canvas.right, sy);
        }

        SelectClipRgn(mem_dc, NULL);
        SelectObject(mem_dc, old_pen);
        DeleteObject(pen_ws_major);
        DeleteObject(pen_ws_minor);
        DeleteObject(pen_cv_major);
        DeleteObject(pen_cv_minor);
        DeleteObject(hrgn_outside);
        DeleteObject(hrgn_total);
        DeleteObject(hrgn_cv);
    }

    /* 12. Center Crosshair */
    if (!g_level_preview_active) {
        HPEN center_pen = CreatePen(PS_SOLID, 1, th->canvas_crosshair);
        HPEN old_pen = (HPEN)SelectObject(mem_dc, center_pen);
        int center_sx = rc_canvas.left + (int)roundf((float)g_canvas_w * 0.5f * g_zoom);
        int center_sy = rc_canvas.top + (int)roundf((float)g_canvas_h * 0.5f * g_zoom);
        MoveToEx(mem_dc, center_sx - 12, center_sy, NULL);
        LineTo(mem_dc, center_sx + 13, center_sy);
        MoveToEx(mem_dc, center_sx, center_sy - 12, NULL);
        LineTo(mem_dc, center_sx, center_sy + 13);
        SelectObject(mem_dc, old_pen);
        DeleteObject(center_pen);
    }

    /* 13. Canvas Crisp Border */
    HPEN border_pen = CreatePen(PS_SOLID, 1, th->canvas_border);
    HPEN old_pen = (HPEN)SelectObject(mem_dc, border_pen);
    SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
    Rectangle(mem_dc, rc_canvas.left, rc_canvas.top, rc_canvas.right, rc_canvas.bottom);
    SelectObject(mem_dc, old_pen);
    DeleteObject(border_pen);

    /* 14. Bezier Curve Paths, Anchors, and Handles (Edit Mode) OR Simulation (Preview Mode) */
    if (!g_level_preview_active) {
        render_bezier_curves(mem_dc, &rc_canvas, client_w, work_h, th);
    } else {
        render_level_preview_simulation(mem_dc, &rc_canvas, th);
        /* 14.1 Level Preview Mode: Coins and Frog are on top of cutouts, sprites, skull, and balls */
        render_placed_coins(mem_dc, &rc_canvas, th);
        render_placed_frog(mem_dc, &rc_canvas, th);
    }

    /* 15. Placed Elements Hover Badges and Brackets (Always in front of grid) */
    if (!g_level_preview_active) {
        render_placed_frog_hover_badge(mem_dc, &rc_canvas, th);
        render_placed_coin_hover_badge(mem_dc, &rc_canvas, th);

        if (hovered_co_idx >= 0 || adjusting_co_idx >= 0) {
            int idx = (hovered_co_idx >= 0) ? hovered_co_idx : adjusting_co_idx;
            LevelCutout *co = &g_cutouts[idx];
            int img_w = co->has_img ? co->img.width : (g_has_default_cutout_img ? g_default_cutout_img.width : 160);
            int img_h = co->has_img ? co->img.height : (g_has_default_cutout_img ? g_default_cutout_img.height : 140);
            int sx = rc_canvas.left + (int)roundf((float)co->x * g_zoom);
            int sy = rc_canvas.top + (int)roundf((float)co->y * g_zoom);
            int sw = (int)roundf((float)img_w * g_zoom);
            int sh = (int)roundf((float)img_h * g_zoom);

            draw_topleft_corner_brackets(mem_dc, sx, sy, sw, sh, th->text_accent_blue);
            if (hovered_co_idx >= 0) {
                char co_badge[128];
                const char *c_id = co->image_name[0] ? co->image_name : ((g_cutout_count == 1) ? "tunnel" : "tunnel1");
                snprintf(co_badge, sizeof(co_badge), "Cutout ID: %s | Pri: %d (#%d) [X: %d, Y: %d]",
                         c_id, co->pri > 0 ? co->pri : 1, idx + 1, co->x, co->y);
                int badge_y = (sy - 24 >= rc_canvas.top + 4) ? (sy - 24) : (sy + sh + 8);
                render_coord_badge(mem_dc, sx + sw / 2, badge_y, co_badge, th);
            }
        }

        if (hovered_sp_idx >= 0 || adjusting_sp_idx >= 0) {
            int idx = (hovered_sp_idx >= 0) ? hovered_sp_idx : adjusting_sp_idx;
            LevelSprite *sp = &g_sprites[idx];
            int img_w = sp->has_img ? sp->img.width : (g_has_default_sprite_img ? g_default_sprite_img.width : 120);
            int img_h = sp->has_img ? sp->img.height : (g_has_default_sprite_img ? g_default_sprite_img.height : 120);
            int sx = rc_canvas.left + (int)roundf((float)sp->x * g_zoom);
            int sy = rc_canvas.top + (int)roundf((float)sp->y * g_zoom);
            int sw = (int)roundf((float)img_w * g_zoom);
            int sh = (int)roundf((float)img_h * g_zoom);

            draw_topleft_corner_brackets(mem_dc, sx, sy, sw, sh, th->text_accent_blue);
            if (hovered_sp_idx >= 0) {
                char sp_badge[128];
                const char *s_id = sp->image_name[0] ? sp->image_name : ((g_sprite_count == 1) ? "sprite" : "sprite1");
                snprintf(sp_badge, sizeof(sp_badge), "Sprite ID: %s | Pri: %d (#%d) [X: %d, Y: %d]",
                         s_id, sp->pri > 0 ? sp->pri : 1, idx + 1, sp->x, sp->y);
                int badge_y = (sy - 24 >= rc_canvas.top + 4) ? (sy - 24) : (sy + sh + 8);
                render_coord_badge(mem_dc, sx + sw / 2, badge_y, sp_badge, th);
            }
        }

        if (hovered_ba_idx >= 0 || adjusting_ba_idx >= 0) {
            int idx = (hovered_ba_idx >= 0) ? hovered_ba_idx : adjusting_ba_idx;
            LevelBackgroundAlpha *ba = &g_bg_alphas[idx];
            int img_w = ba->has_img ? ba->img.width : (g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 160);
            int img_h = ba->has_img ? ba->img.height : (g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 140);
            int sx = rc_canvas.left + (int)roundf((float)ba->x * g_zoom);
            int sy = rc_canvas.top + (int)roundf((float)ba->y * g_zoom);
            int sw = (int)roundf((float)img_w * g_zoom);
            int sh = (int)roundf((float)img_h * g_zoom);

            draw_topleft_corner_brackets(mem_dc, sx, sy, sw, sh, th->text_accent_blue);
            if (hovered_ba_idx >= 0) {
                char ba_badge[128];
                const char *b_id = ba->image_name[0] ? ba->image_name : ((g_bg_alpha_count == 1) ? "quake" : "quake1");
                snprintf(ba_badge, sizeof(ba_badge), "BgAlpha ID: %s | Vx: %d, Vy: %d (#%d) [X: %d, Y: %d]",
                         b_id, ba->vx, ba->vy, idx + 1, ba->x, ba->y);
                int badge_y = (sy - 24 >= rc_canvas.top + 4) ? (sy - 24) : (sy + sh + 8);
                render_coord_badge(mem_dc, sx + sw / 2, badge_y, ba_badge, th);
            }
        }
    }


    /* 9.1 Ghost Frog (Follows hover cursor with 25% opacity in workspace during placement mode) */
    if (g_placing_frog && g_has_frog_img && g_frog_img.hbm && g_frog_img.hdc_mem) {
        if (g_mouse_screen_y >= TOOLBAR_HEIGHT && g_mouse_screen_x >= 0) {
            int fw = (int)roundf((float)g_frog_img.width * g_zoom);
            int fh = (int)roundf((float)g_frog_img.height * g_zoom);
            int fx = g_mouse_screen_x - fw / 2;
            int fy = g_mouse_screen_y - fh / 2;

            BLENDFUNCTION bf_ghost = { AC_SRC_OVER, 0, 64, AC_SRC_ALPHA };
            GdiAlphaBlend(mem_dc, fx, fy, fw, fh,
                          g_frog_img.hdc_mem, 0, 0, g_frog_img.width, g_frog_img.height, bf_ghost);

            /* Center reticle at cursor position for pixel-perfect placement */
            HPEN ghost_pen = CreatePen(PS_SOLID, 1, th->text_accent_blue);
            HPEN old_pen_g = (HPEN)SelectObject(mem_dc, ghost_pen);
            MoveToEx(mem_dc, g_mouse_screen_x - 10, g_mouse_screen_y, NULL);
            LineTo(mem_dc, g_mouse_screen_x + 11, g_mouse_screen_y);
            MoveToEx(mem_dc, g_mouse_screen_x, g_mouse_screen_y - 10, NULL);
            LineTo(mem_dc, g_mouse_screen_x, g_mouse_screen_y + 11);
            SelectObject(mem_dc, old_pen_g);
            DeleteObject(ghost_pen);
        }
    }

    /* 9.2 Ghost Coin (Follows hover cursor with 25% opacity in workspace during placement mode) */
    if (g_placing_coin && g_has_coin_img && g_coin_img.hbm && g_coin_img.hdc_mem) {
        if (g_mouse_screen_y >= TOOLBAR_HEIGHT && g_mouse_screen_x >= 0) {
            int cw = (int)roundf((float)g_coin_img.width * g_zoom);
            int ch = (int)roundf((float)g_coin_img.height * g_zoom);
            int cx = g_mouse_screen_x - cw / 2;
            int cy = g_mouse_screen_y - ch / 2;

            BLENDFUNCTION bf_ghost = { AC_SRC_OVER, 0, 64, AC_SRC_ALPHA };
            GdiAlphaBlend(mem_dc, cx, cy, cw, ch,
                          g_coin_img.hdc_mem, 0, 0, g_coin_img.width, g_coin_img.height, bf_ghost);

            /* Center reticle at cursor position for pixel-perfect placement */
            HPEN ghost_pen = CreatePen(PS_SOLID, 1, th->text_accent_blue);
            HPEN old_pen_g = (HPEN)SelectObject(mem_dc, ghost_pen);
            MoveToEx(mem_dc, g_mouse_screen_x - 10, g_mouse_screen_y, NULL);
            LineTo(mem_dc, g_mouse_screen_x + 11, g_mouse_screen_y);
            MoveToEx(mem_dc, g_mouse_screen_x, g_mouse_screen_y - 10, NULL);
            LineTo(mem_dc, g_mouse_screen_x, g_mouse_screen_y + 11);
            SelectObject(mem_dc, old_pen_g);
            DeleteObject(ghost_pen);
        }
    }

    /* 9.3 Ghost Cutout (Top-Left aligned with mouse cursor, 25% opacity) */
    if (g_placing_cutout) {
        HDC co_hdc = g_has_pending_cutout_img ? g_pending_cutout_img.hdc_mem :
                     (g_has_default_cutout_img ? g_default_cutout_img.hdc_mem : NULL);
        int img_w = g_has_pending_cutout_img ? g_pending_cutout_img.width :
                    (g_has_default_cutout_img ? g_default_cutout_img.width : 160);
        int img_h = g_has_pending_cutout_img ? g_pending_cutout_img.height :
                    (g_has_default_cutout_img ? g_default_cutout_img.height : 140);

        if (!co_hdc) {
            load_default_cutout_sprite();
            co_hdc = g_has_default_cutout_img ? g_default_cutout_img.hdc_mem : NULL;
            img_w = g_has_default_cutout_img ? g_default_cutout_img.width : 160;
            img_h = g_has_default_cutout_img ? g_default_cutout_img.height : 140;
        }

        if (co_hdc && g_mouse_screen_y >= TOOLBAR_HEIGHT && g_mouse_screen_x >= 0) {
            int sw = (int)roundf((float)img_w * g_zoom);
            int sh = (int)roundf((float)img_h * g_zoom);
            int sx = g_mouse_screen_x;
            int sy = g_mouse_screen_y;

            BLENDFUNCTION bf_ghost = { AC_SRC_OVER, 0, 64, AC_SRC_ALPHA };
            GdiAlphaBlend(mem_dc, sx, sy, sw, sh,
                          co_hdc, 0, 0, img_w, img_h, bf_ghost);

            /* Top-left corner indicator for pixel-perfect placement */
            HPEN ghost_pen = CreatePen(PS_SOLID, 1, th->text_accent_blue);
            HPEN old_pen_g = (HPEN)SelectObject(mem_dc, ghost_pen);
            MoveToEx(mem_dc, sx - 1, sy - 1, NULL);
            LineTo(mem_dc, sx + 15, sy - 1);
            MoveToEx(mem_dc, sx - 1, sy - 1, NULL);
            LineTo(mem_dc, sx - 1, sy + 15);
            SelectObject(mem_dc, old_pen_g);
            DeleteObject(ghost_pen);

            char pri_badge[32];
            snprintf(pri_badge, sizeof(pri_badge), "Pri: %d", g_overlay_current_prio);
            render_coord_badge(mem_dc, sx + 24, sy - 12, pri_badge, th);
        }
    }

    /* 9.4 Ghost Sprite (Top-Left aligned with mouse cursor, 25% opacity) */
    if (g_placing_sprite) {
        HDC sp_hdc = g_has_pending_sprite_img ? g_pending_sprite_img.hdc_mem :
                     (g_has_default_sprite_img ? g_default_sprite_img.hdc_mem : NULL);
        int img_w = g_has_pending_sprite_img ? g_pending_sprite_img.width :
                    (g_has_default_sprite_img ? g_default_sprite_img.width : 120);
        int img_h = g_has_pending_sprite_img ? g_pending_sprite_img.height :
                    (g_has_default_sprite_img ? g_default_sprite_img.height : 120);

        if (!sp_hdc) {
            load_default_sprite_sprite();
            sp_hdc = g_has_default_sprite_img ? g_default_sprite_img.hdc_mem : NULL;
            img_w = g_has_default_sprite_img ? g_default_sprite_img.width : 120;
            img_h = g_has_default_sprite_img ? g_default_sprite_img.height : 120;
        }

        if (sp_hdc && g_mouse_screen_y >= TOOLBAR_HEIGHT && g_mouse_screen_x >= 0) {
            int sw = (int)roundf((float)img_w * g_zoom);
            int sh = (int)roundf((float)img_h * g_zoom);
            int sx = g_mouse_screen_x;
            int sy = g_mouse_screen_y;

            BLENDFUNCTION bf_ghost = { AC_SRC_OVER, 0, 64, AC_SRC_ALPHA };
            GdiAlphaBlend(mem_dc, sx, sy, sw, sh,
                          sp_hdc, 0, 0, img_w, img_h, bf_ghost);

            /* Top-left corner indicator for pixel-perfect placement */
            HPEN ghost_pen = CreatePen(PS_SOLID, 1, th->text_accent_blue);
            HPEN old_pen_g = (HPEN)SelectObject(mem_dc, ghost_pen);
            MoveToEx(mem_dc, sx - 1, sy - 1, NULL);
            LineTo(mem_dc, sx + 15, sy - 1);
            MoveToEx(mem_dc, sx - 1, sy - 1, NULL);
            LineTo(mem_dc, sx - 1, sy + 15);
            SelectObject(mem_dc, old_pen_g);
            DeleteObject(ghost_pen);

            char pri_badge[32];
            snprintf(pri_badge, sizeof(pri_badge), "Pri: %d", g_overlay_current_prio);
            render_coord_badge(mem_dc, sx + 24, sy - 12, pri_badge, th);
        }
    }

    /* 9.4.5 Ghost BackgroundAlpha (Step 1: Top-Left aligned with mouse cursor, 25% opacity) */
    if (g_placing_bgalpha && !g_placing_bgalpha_vector) {
        HDC ba_hdc = g_has_pending_bgalpha_img ? g_pending_bgalpha_img.hdc_mem :
                     (g_has_default_bgalpha_img ? g_default_bgalpha_img.hdc_mem : NULL);
        int img_w = g_has_pending_bgalpha_img ? g_pending_bgalpha_img.width :
                    (g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 160);
        int img_h = g_has_pending_bgalpha_img ? g_pending_bgalpha_img.height :
                    (g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 140);

        if (!ba_hdc) {
            load_default_bgalpha_sprite();
            ba_hdc = g_has_default_bgalpha_img ? g_default_bgalpha_img.hdc_mem : NULL;
            img_w = g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 160;
            img_h = g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 140;
        }

        if (ba_hdc && g_mouse_screen_y >= TOOLBAR_HEIGHT && g_mouse_screen_x >= 0) {
            int sw = (int)roundf((float)img_w * g_zoom);
            int sh = (int)roundf((float)img_h * g_zoom);
            int sx = g_mouse_screen_x;
            int sy = g_mouse_screen_y;

            BLENDFUNCTION bf_ghost = { AC_SRC_OVER, 0, 64, AC_SRC_ALPHA };
            GdiAlphaBlend(mem_dc, sx, sy, sw, sh,
                          ba_hdc, 0, 0, img_w, img_h, bf_ghost);

            /* Top-left corner indicator for pixel-perfect placement */
            HPEN ghost_pen = CreatePen(PS_SOLID, 1, th->text_accent_blue);
            HPEN old_pen_g = (HPEN)SelectObject(mem_dc, ghost_pen);
            MoveToEx(mem_dc, sx - 1, sy - 1, NULL);
            LineTo(mem_dc, sx + 15, sy - 1);
            MoveToEx(mem_dc, sx - 1, sy - 1, NULL);
            LineTo(mem_dc, sx - 1, sy + 15);
            SelectObject(mem_dc, old_pen_g);
            DeleteObject(ghost_pen);

            char pos_badge[32];
            snprintf(pos_badge, sizeof(pos_badge), "[X: %d, Y: %d]", (int)roundf(g_canvas_mouse_x), (int)roundf(g_canvas_mouse_y));
            render_coord_badge(mem_dc, sx + 36, sy - 12, pos_badge, th);
        }
    }

    /* 9.4.6 BackgroundAlpha 2-Step Donut / Ring Direction Widget (Vx/Vy Alignment) */
    if (g_placing_bgalpha_vector) {
        render_bgalpha_vector_widget(mem_dc, &rc_canvas, th);
    }

    /* 9.4.6.1 Overlay Priority Adjustment Widget (Cutout / Sprite) */
    if (g_adjusting_overlay_priority && g_editing_prio_idx >= 0) {
        int target_x = 0, target_y = 0, target_w = 0, target_h = 0;
        const char *kind_str = "Cutout";
        if (g_editing_prio_type == 0 && g_editing_prio_idx < g_cutout_count) {
            LevelCutout *co = &g_cutouts[g_editing_prio_idx];
            int img_w = co->has_img ? co->img.width : (g_has_default_cutout_img ? g_default_cutout_img.width : 160);
            int img_h = co->has_img ? co->img.height : (g_has_default_cutout_img ? g_default_cutout_img.height : 140);
            target_x = rc_canvas.left + (int)roundf((float)co->x * g_zoom);
            target_y = rc_canvas.top + (int)roundf((float)co->y * g_zoom);
            target_w = (int)roundf((float)img_w * g_zoom);
            target_h = (int)roundf((float)img_h * g_zoom);
            kind_str = "Cutout";
        } else if (g_editing_prio_type == 1 && g_editing_prio_idx < g_sprite_count) {
            LevelSprite *sp = &g_sprites[g_editing_prio_idx];
            int img_w = sp->has_img ? sp->img.width : (g_has_default_sprite_img ? g_default_sprite_img.width : 120);
            int img_h = sp->has_img ? sp->img.height : (g_has_default_sprite_img ? g_default_sprite_img.height : 120);
            target_x = rc_canvas.left + (int)roundf((float)sp->x * g_zoom);
            target_y = rc_canvas.top + (int)roundf((float)sp->y * g_zoom);
            target_w = (int)roundf((float)img_w * g_zoom);
            target_h = (int)roundf((float)img_h * g_zoom);
            kind_str = "Sprite";
        }

        if (target_w > 0 && target_h > 0) {
            draw_topleft_corner_brackets(mem_dc, target_x, target_y, target_w, target_h, th->text_accent_blue);

            char pri_badge[128];
            snprintf(pri_badge, sizeof(pri_badge), "%s Priority: %d (Scroll Wheel: Adjust | LMB: Apply | RMB: Cancel)",
                     kind_str, g_pending_overlay_priority);
            int badge_y = (target_y - 28 >= rc_canvas.top + 4) ? (target_y - 28) : (target_y + target_h + 8);
            render_coord_badge(mem_dc, target_x + target_w / 2, badge_y, pri_badge, th);
        }
    }

    /* 9.4.7 Canvas Objects Selection Marquee Box */
    if (!g_level_preview_active) {
        render_object_marquee_box(mem_dc, th);
    }

    /* 9.5 Floating Workspace HUD Panels (Canvas Mode vs Curve Mode vs Overlay Mode vs Element Mode vs Preview Mode) */
    bool need_prio_edit = (g_mode_curve && g_curve_submode == CURVE_SUBMODE_VARIABLE && !g_hud_mode_collapsed) ||
                          (g_mode_overlay && g_has_canvas && !g_hud_overlay_collapsed);
    if (!need_prio_edit && g_edit_prio && IsWindowVisible(g_edit_prio)) {
        ShowWindow(g_edit_prio, SW_HIDE);
    }

    bool need_cv_edit = (g_mode_canvas && g_has_canvas && !g_hud_canvas_collapsed);
    if (!need_cv_edit) {
        if (g_edit_cv_w && IsWindowVisible(g_edit_cv_w)) ShowWindow(g_edit_cv_w, SW_HIDE);
        if (g_edit_cv_h && IsWindowVisible(g_edit_cv_h)) ShowWindow(g_edit_cv_h, SW_HIDE);
    }

    bool need_preview_title_edit = (!g_mode_canvas && !g_mode_curve && !g_mode_overlay && !g_mode_element && !g_hud_preview_info_collapsed && g_has_canvas);
    if (!need_preview_title_edit) {
        hide_all_map_attr_edits();
    }

    if (g_mode_canvas && g_has_canvas) {
        render_canvas_transform_handles(mem_dc, client_w, work_h, th);
        render_canvas_guide_hud(mem_dc, client_w, work_h, th);
        render_canvas_hud(mem_dc, client_w, work_h, th);
    } else if (g_mode_curve) {
        render_curve_submode_huds(mem_dc, client_w, work_h, th);
    } else if (g_mode_overlay && g_has_canvas) {
        render_overlay_guide_hud(mem_dc, client_w, work_h, th);
        render_overlay_hud(mem_dc, client_w, work_h, th);
    } else if (g_mode_element && g_has_canvas) {
        render_element_guide_hud(mem_dc, client_w, work_h, th);
        render_element_hud(mem_dc, client_w, work_h, th);
    } else {
        /* PREVIEW MODE (all edit modes toggled off) */
        render_preview_guide_hud(mem_dc, client_w, work_h, th);
        render_preview_hud(mem_dc, client_w, work_h, th);
        if (!g_hud_preview_info_collapsed) {
            render_map_attr_tooltip(mem_dc, client_w, work_h, th, g_hud_map_attr_hover, g_hud_map_attr_hover_y);
        }
    }

    /* 10. BitBlt Workspace Only (Toolbar handled cleanly by child viewports) */
    BitBlt(hdc, 0, TOOLBAR_HEIGHT, client_w, work_h, mem_dc, 0, TOOLBAR_HEIGHT, SRCCOPY);

    SelectObject(mem_dc, old_bm);
    DeleteObject(mem_bm);
    DeleteDC(mem_dc);
}

