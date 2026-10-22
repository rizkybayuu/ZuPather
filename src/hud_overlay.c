#include "hud_overlay.h"

/* High-Performance HUD Rendering Helpers */
static void blit_hud_surface(HDC dst_dc, int x, int y, int w, int h, HDC src_dc, BYTE alpha) {
    if (alpha >= 255 || g_perf_direct_blit) {
        BitBlt(dst_dc, x, y, w, h, src_dc, 0, 0, SRCCOPY);
    } else {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
        GdiAlphaBlend(dst_dc, x, y, w, h, src_dc, 0, 0, w, h, bf);
    }
}

static bool s_hud_cache_dirty = true;
void invalidate_hud_cache(void) {
    s_hud_cache_dirty = true;
}


typedef struct {
    const char *key;
    const char *desc;
    bool is_primary;
} GuideRow;

static const GuideRow s_draw_guide[] = {
    /* Primary Navigation & Control Shortcuts (Highlighted at Top) */
    { "Tab",             "Toggle Draw / Attribute mode", true },
    { "Ctrl+Shift+I",    "Import curve path (.dat, .svg)", true },
    { "Ctrl+Shift+E",    "Export curve paths (.dat, .svg)", true },
    { "F1",              "Toggle Guide panel",            true },
    { "F2",              "Toggle Curve Tools panel",      true },

    /* Secondary Tool & Geometry Actions */
    { "LMB",             "Add node / Drag tangent handles", false },
    { "Alt+LMB on Curve","Insert new node between points", false },
    { "Insert",          "Subdivide before node at midpoint", false },
    { "RMB",             "Finish curve / Start new track", false },
    { "Enter / Esc",     "Finish or cancel curve drawing", false },
    { "Ctrl + LMB",      "Select active node / Drag move", false },
    { "Ctrl + Drag",     "Marquee replace selection",     false },
    { "Ctrl+Shift+LMB",  "Add node to selection",         false },
    { "Ctrl+Shift+Drag", "Marquee add to selection",      false },
    { "Alt+Shift+LMB",   "Deselect clicked node",         false },
    { "Alt+Shift+Drag",  "Marquee deselect nodes",        false },
    { "Ctrl+Alt+Shift",  "Toggle select (click or drag)", false },
    { "Ctrl+A",          "Select all curve nodes",        false },
    { "Ctrl+I",          "Invert selection",              false },
    { "Ctrl+D",          "Deselect all curve nodes",      false },
    { "Alt + Drag",      "Break handle into sharp cusp",  false },
    { "S",               "Smooth handles (collinear)",    false },
    { "C",               "Sharp corner (retract handles)", false },
    { "Arrows",          "Nudge selected node(s) 1px",    false },
    { "Shift + Arrows",  "Nudge selected path 1px",       false },
    { "Delete",          "Delete selected anchor point(s)", false },
    { "Shift + Del",     "Delete path of selected node",  false },
    { "Shift + RMB",     "Delete clicked path directly",  false },
    { "RMB Anchor",      "Quick delete clicked anchor (idle)", false },
    { "RMB Handle",      "Quick retract clicked handle (idle)", false },
    { "X",               "Split track at interior node",  false },
    { "R",               "Reverse track flow direction",  false },
    { "J",               "Join 2 endpoints (auto-flow)",  false },
    { "[",               "Select previous track",         false },
    { "]",               "Select next track",             false },
    { "N",               "Finish track / Start new track", false }
};

static const GuideRow s_attr_guide[] = {
    /* Primary Navigation & Control Shortcuts (Highlighted at Top) */
    { "Tab",             "Toggle Draw / Attribute mode",  true },
    { "Ctrl+Shift+I",    "Import curve path (.dat, .svg)", true },
    { "Ctrl+Shift+E",    "Export curve paths (.dat, .svg)", true },
    { "F1",              "Toggle Guide panel",            true },
    { "F2",              "Toggle Curve Tools panel",      true },

    /* Attribute Spans Painting & Quick Controls */
    { "LMB Drag",        "Paint attribute span on track", false },
    { "RMB Click",       "Toggle Unhittable (or press U)", false },
    { "Mouse Wheel",     "Scroll canvas: Priority +/- (min 0)", false },
    { "Ctrl + LMB",      "Switch active path under cursor", false },
    { "Ctrl+A",          "Select all curve nodes",        false },
    { "Ctrl+I",          "Invert selection",              false },
    { "Ctrl+D",          "Deselect all curve nodes",      false },
    { "Hover Track",     "Inspect Unhittable & Priority badge", false }
};

int get_guide_content_height(void) {
    if (g_hud_shortcuts_collapsed) return 26;

    const GuideRow *cur_guide = (g_curve_submode == CURVE_SUBMODE_DRAW) ? s_draw_guide : s_attr_guide;
    int count = (g_curve_submode == CURVE_SUBMODE_DRAW) ?
        (int)(sizeof(s_draw_guide) / sizeof(s_draw_guide[0])) :
        (int)(sizeof(s_attr_guide) / sizeof(s_attr_guide[0]));

    int total_h = 28; /* Header & top title bar */
    for (int i = 0; i < count; ++i) {
        total_h += 20; /* item_h */
        if (cur_guide[i].is_primary && (i + 1 < count) && !cur_guide[i + 1].is_primary) {
            total_h += 8; /* divider spacing between primary and secondary */
        }
    }
    total_h += 8; /* bottom margin */
    return total_h;
}

int get_guide_hud_max_scroll(int work_h) {
    if (g_hud_shortcuts_collapsed) return 0;
    int content_h = get_guide_content_height();
    int max_avail_h = work_h - 16;
    if (max_avail_h < 26) max_avail_h = 26;
    int max_s = content_h - max_avail_h;
    return max_s > 0 ? max_s : 0;
}

bool get_guide_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h) {
    int max_s = get_guide_hud_max_scroll(work_h);
    if (max_s <= 0) return false;

    int content_h = get_guide_content_height();
    int avail_h = work_h - 16;
    if (avail_h < 60) avail_h = 60;

    int track_y = 28;
    int track_h = avail_h - 34;
    if (track_h <= 20) return false;

    float vis_ratio = (float)avail_h / (float)content_h;
    int thumb_h = (int)roundf((float)track_h * vis_ratio);
    if (thumb_h < 25) thumb_h = 25;
    if (thumb_h > track_h) thumb_h = track_h;

    float scroll_ratio = (max_s > 0) ? ((float)g_hud_guide_scroll_y / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_y = track_y + (int)roundf(scroll_ratio * (float)(track_h - thumb_h));

    if (out_track_y) *out_track_y = track_y;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return true;
}

int get_attr_content_height(void) {
    if (g_hud_mode_collapsed) return 26;
    return 104;
}

int get_attr_hud_max_scroll(int work_h) {
    if (g_hud_mode_collapsed) return 0;
    int content_h = get_attr_content_height();
    int max_avail_h = work_h - 16;
    if (max_avail_h < 26) max_avail_h = 26;
    int max_s = content_h - max_avail_h;
    return max_s > 0 ? max_s : 0;
}

bool get_attr_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h) {
    int max_s = get_attr_hud_max_scroll(work_h);
    if (max_s <= 0) return false;

    int content_h = get_attr_content_height();
    int avail_h = work_h - 16;
    if (avail_h < 50) avail_h = 50;

    int track_y = 26;
    int track_h = avail_h - 32;
    if (track_h <= 15) return false;

    float vis_ratio = (float)avail_h / (float)content_h;
    int thumb_h = (int)roundf((float)track_h * vis_ratio);
    if (thumb_h < 20) thumb_h = 20;
    if (thumb_h > track_h) thumb_h = track_h;

    float scroll_ratio = (max_s > 0) ? ((float)g_hud_attr_scroll_y / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_y = track_y + (int)roundf(scroll_ratio * (float)(track_h - thumb_h));

    if (out_track_y) *out_track_y = track_y;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return true;
}

/* Calculate Top-Right HUD bounding rect (Curve Attribute & Mode Switcher) */
void get_top_right_hud_rect(int client_w, int work_h, RECT *rc) {
    int tr_w = g_hud_mode_collapsed ? 180 : 236;
    int full_h = get_attr_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int tr_h = (full_h > max_avail_h) ? max_avail_h : full_h;

    rc->left = client_w - tr_w - 12;
    rc->top = TOOLBAR_HEIGHT + 8;
    rc->right = rc->left + tr_w;
    rc->bottom = rc->top + tr_h;
}

/* Calculate Top-Left HUD bounding rect (Guide & Help Panel) */
void get_top_left_hud_rect(int work_h, RECT *rc) {
    int tl_w = g_hud_shortcuts_collapsed ? 135 : 370;
    int full_h = get_guide_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int tl_h = (full_h > max_avail_h) ? max_avail_h : full_h;

    rc->left = 12;
    rc->top = TOOLBAR_HEIGHT + 8;
    rc->right = rc->left + tl_w;
    rc->bottom = rc->top + tl_h;
}

static const char *g_overlay_guide_lines[] = {
    "Sub-Tools: Click [+ BgAlpha], [+ Sprite], or [+ Cutout]",
    "Mouse Wheel: Scroll up/down anywhere to adjust Priority (0..99)",
    "LMB (Left Click): Place at cursor (Locks X, Y, and Priority)",
    "LMB on Placed Item: Pick up & carry to reposition / change Pri",
    "RMB (Right Click): Delete hovered overlay item (or cancel)",
    "Selection & Multi-Select:",
    "  * Ctrl + LMB: Select item (Replace selection)",
    "  * Ctrl + Alt + LMB: Select underneath (cycle layers)",
    "  * Ctrl + Shift + LMB / Drag: Add to Multi-Select",
    "  * Alt + Shift + LMB / Drag: Subtract from Multi-Select",
    "  * Ctrl + Alt + Shift + LMB / Drag: Toggle Multi-Select",
    "  * Ctrl + A: Select All | Ctrl + I: Invert",
    "  * Ctrl + D: Deselect All",
    "  * Delete: Delete multi-selected items",
    "  * Arrow Keys: Nudge selected items by 1px",
    "Overlay Variable Adjustment:",
    "  * Ctrl+J: Set Motion (BgAlpha) / Set Pri (Cutout/Sprite)",
    "  1. BgAlpha: Aim donut for Vx/Vy [-1, 0, 1]",
    "  2. Cutout/Sprite: Scroll wheel to adjust Priority (0..99)",
    "  3. LMB confirms / RMB or Esc cancels adjustment",
    "F1: Guide | F2: Tools"
};

int get_overlay_guide_content_height(void) {
    if (g_hud_overlay_guide_collapsed) return 26;

    int num_lines = (int)(sizeof(g_overlay_guide_lines) / sizeof(g_overlay_guide_lines[0]));
    int text_w = 370 - 20;

    HDC screen_dc = GetDC(NULL);
    int total_h = 28; /* Top title bar (24px) + margin (4px) */
    for (int i = 0; i < num_lines; ++i) {
        bool is_footer_shortcuts = (i == num_lines - 1);
        bool is_header = (i == 0 ||
                          strcmp(g_overlay_guide_lines[i], "Selection & Multi-Select:") == 0 ||
                          strcmp(g_overlay_guide_lines[i], "Overlay Variable Adjustment:") == 0);
        HFONT font = is_header ? g_font_primary_bold : (is_footer_shortcuts ? g_font_mono_bold : g_font_primary_regular);
        HFONT old_f = (HFONT)SelectObject(screen_dc, font);
        RECT rc = { 0, 0, text_w, 0 };
        DrawTextA(screen_dc, g_overlay_guide_lines[i], -1, &rc, DT_CALCRECT | DT_WORDBREAK);
        SelectObject(screen_dc, old_f);
        int row_h = rc.bottom - rc.top;
        if (row_h < 18) row_h = 18;
        if (!is_footer_shortcuts) total_h += row_h + 3;
        else total_h += row_h;
    }
    ReleaseDC(NULL, screen_dc);
    total_h += 6; /* clean, compact bottom padding */
    return total_h;
}

int get_overlay_guide_hud_max_scroll(int work_h) {
    if (g_hud_overlay_guide_collapsed) return 0;
    int content_h = get_overlay_guide_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int max_s = content_h - max_avail_h;
    return max_s > 0 ? max_s : 0;
}

bool get_overlay_guide_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h) {
    int max_s = get_overlay_guide_hud_max_scroll(work_h);
    if (max_s <= 0) return false;

    int content_h = get_overlay_guide_content_height();
    int avail_h = work_h - 16;
    if (avail_h < 60) avail_h = 60;

    int track_y = 28;
    int track_h = avail_h - 34;
    if (track_h <= 20) return false;

    float vis_ratio = (float)avail_h / (float)content_h;
    int thumb_h = (int)roundf((float)track_h * vis_ratio);
    if (thumb_h < 25) thumb_h = 25;
    if (thumb_h > track_h) thumb_h = track_h;

    float scroll_ratio = (max_s > 0) ? ((float)g_hud_overlay_guide_scroll_y / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_y = track_y + (int)roundf(scroll_ratio * (float)(track_h - thumb_h));

    if (out_track_y) *out_track_y = track_y;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return true;
}

/* Calculate Top-Left Overlay Guide HUD bounding rect */
void get_top_left_overlay_guide_hud_rect(int work_h, RECT *rc) {
    int tl_w = g_hud_overlay_guide_collapsed ? 135 : 370;
    int full_h = get_overlay_guide_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int tl_h = g_hud_overlay_guide_collapsed ? 26 : ((full_h > max_avail_h) ? max_avail_h : full_h);

    rc->left = 12;
    rc->top = TOOLBAR_HEIGHT + 8;
    rc->right = rc->left + tl_w;
    rc->bottom = rc->top + tl_h;
}

int get_overlay_tools_content_height(void) {
    if (g_hud_overlay_collapsed) return 26;
    return 104;
}

int get_overlay_tools_hud_max_scroll(int work_h) {
    if (g_hud_overlay_collapsed) return 0;
    int content_h = get_overlay_tools_content_height();
    int max_avail_h = work_h - 16;
    if (max_avail_h < 26) max_avail_h = 26;
    int max_s = content_h - max_avail_h;
    return max_s > 0 ? max_s : 0;
}

bool get_overlay_tools_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h) {
    int max_s = get_overlay_tools_hud_max_scroll(work_h);
    if (max_s <= 0) return false;

    int content_h = get_overlay_tools_content_height();
    int avail_h = work_h - 16;
    if (avail_h < 50) avail_h = 50;

    int track_y = 26;
    int track_h = avail_h - 32;
    if (track_h <= 15) return false;

    float vis_ratio = (float)avail_h / (float)content_h;
    int thumb_h = (int)roundf((float)track_h * vis_ratio);
    if (thumb_h < 20) thumb_h = 20;
    if (thumb_h > track_h) thumb_h = track_h;

    float scroll_ratio = (max_s > 0) ? ((float)g_hud_overlay_tools_scroll_y / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_y = track_y + (int)roundf(scroll_ratio * (float)(track_h - thumb_h));

    if (out_track_y) *out_track_y = track_y;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return true;
}

/* Calculate Top-Right Overlay Tools HUD bounding rect */
void get_top_right_overlay_hud_rect(int client_w, int work_h, RECT *rc) {
    int tr_w = g_hud_overlay_collapsed ? 180 : 236;
    int full_h = get_overlay_tools_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int tr_h = (full_h > max_avail_h) ? max_avail_h : full_h;

    rc->left = client_w - tr_w - 12;
    rc->top = TOOLBAR_HEIGHT + 8;
    rc->right = rc->left + tr_w;
    rc->bottom = rc->top + tr_h;
}

/* Render Floating Workspace HUD Panels (50% opacity collapsed, 100% expanded) */
void render_curve_submode_huds(HDC mem_dc, int client_w, int work_h, const ThemeColors *th) {
    if (!g_mode_curve || !g_has_canvas) {
        if (!g_mode_overlay && g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
        return;
    }

    int max_guide_s = get_guide_hud_max_scroll(work_h);
    if (g_hud_guide_scroll_y > max_guide_s) g_hud_guide_scroll_y = max_guide_s;
    if (g_hud_guide_scroll_y < 0) g_hud_guide_scroll_y = 0;

    int max_attr_s = get_attr_hud_max_scroll(work_h);
    if (g_hud_attr_scroll_y > max_attr_s) g_hud_attr_scroll_y = max_attr_s;
    if (g_hud_attr_scroll_y < 0) g_hud_attr_scroll_y = 0;

    bool is_dark = (g_current_theme == THEME_DARK);
    COLORREF panel_bg = is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250);
    COLORREF panel_border = is_dark ? RGB(60, 70, 92) : RGB(190, 198, 212);

    /* -------------------------------------------------------------
     * 1. Top-Right HUD: Mode Switcher (Draw Mode vs Variable Mode)
     * ------------------------------------------------------------- */
    RECT rc_tr;
    get_top_right_hud_rect(client_w, work_h, &rc_tr);
    int tr_w = rc_tr.right - rc_tr.left;
    int tr_h = rc_tr.bottom - rc_tr.top;

    HDC dc_tr = CreateCompatibleDC(mem_dc);
    HBITMAP bm_tr = CreateCompatibleBitmap(mem_dc, tr_w, tr_h);
    HBITMAP old_bm_tr = (HBITMAP)SelectObject(dc_tr, bm_tr);

    HBRUSH br_bg_tr = CreateSolidBrush(panel_bg);
    RECT rc_tr_local = { 0, 0, tr_w, tr_h };
    FillRect(dc_tr, &rc_tr_local, br_bg_tr);
    DeleteObject(br_bg_tr);

    HPEN pen_bdr_tr = CreatePen(PS_SOLID, 1, panel_border);
    HPEN old_pen_tr = (HPEN)SelectObject(dc_tr, pen_bdr_tr);
    SelectObject(dc_tr, GetStockObject(NULL_BRUSH));
    Rectangle(dc_tr, 0, 0, tr_w, tr_h);
    SelectObject(dc_tr, old_pen_tr);
    DeleteObject(pen_bdr_tr);

    SetBkMode(dc_tr, TRANSPARENT);

    if (g_hud_mode_collapsed) {
        if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_t = { 8, 0, tr_w - 52, tr_h };
        DrawTextA(dc_tr, "[+] Curve Tools", -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_hint = { tr_w - 52, 0, tr_w - 22, tr_h };
        DrawTextA(dc_tr, "[F2]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_chev = { tr_w - 20, 0, tr_w - 4, tr_h };
        DrawTextA(dc_tr, "v", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_title = { 8, 4, 126, 20 };
        DrawTextA(dc_tr, "Curve Tools", -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_hint = { tr_w - 110, 4, tr_w - 22, 20 };
        DrawTextA(dc_tr, "[F2 to hide]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_chev = { tr_w - 20, 4, tr_w - 4, 20 };
        DrawTextA(dc_tr, "^", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        /* Clip content area below header */
        HRGN clip_tr = CreateRectRgn(1, 23, tr_w - 1, tr_h - 1);
        SelectClipRgn(dc_tr, clip_tr);

        int sy = -g_hud_attr_scroll_y;

        /* Row 1: Import Path & Export Path buttons */
        RECT rc_btn_imp = { 8, 24 + sy, 114, 46 + sy };
        HBRUSH br_imp = CreateSolidBrush(is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236));
        FillRect(dc_tr, &rc_btn_imp, br_imp);
        DeleteObject(br_imp);
        FrameRect(dc_tr, &rc_btn_imp, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        DrawTextA(dc_tr, "Import Path", -1, &rc_btn_imp, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        RECT rc_btn_exp = { 120, 24 + sy, 228, 46 + sy };
        HBRUSH br_exp = CreateSolidBrush(is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236));
        FillRect(dc_tr, &rc_btn_exp, br_exp);
        DeleteObject(br_exp);
        FrameRect(dc_tr, &rc_btn_exp, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        DrawTextA(dc_tr, "Export Path", -1, &rc_btn_exp, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        /* Row 2: Segmented Switch Pill (Draw vs Attribute) */
        RECT rc_switch_box = { 8, 50 + sy, 228, 72 + sy };
        RECT rc_sw_draw = { 8, 50 + sy, 118, 72 + sy };
        RECT rc_sw_var  = { 118, 50 + sy, 228, 72 + sy };
        bool is_draw = (g_curve_submode == CURVE_SUBMODE_DRAW);

        HBRUSH br_sw_d = CreateSolidBrush(is_draw ? th->text_accent_blue : (is_dark ? RGB(30, 35, 46) : RGB(230, 234, 242)));
        FillRect(dc_tr, &rc_sw_draw, br_sw_d);
        DeleteObject(br_sw_d);

        HBRUSH br_sw_v = CreateSolidBrush(!is_draw ? th->text_accent_blue : (is_dark ? RGB(30, 35, 46) : RGB(230, 234, 242)));
        FillRect(dc_tr, &rc_sw_var, br_sw_v);
        DeleteObject(br_sw_v);

        FrameRect(dc_tr, &rc_switch_box, (HBRUSH)GetStockObject(BLACK_BRUSH));

        SelectObject(dc_tr, is_draw ? g_font_primary_bold : g_font_primary_regular);
        SetTextColor(dc_tr, is_draw ? RGB(255, 255, 255) : th->text_secondary);
        DrawTextA(dc_tr, "Draw", -1, &rc_sw_draw, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        SelectObject(dc_tr, !is_draw ? g_font_primary_bold : g_font_primary_regular);
        SetTextColor(dc_tr, !is_draw ? RGB(255, 255, 255) : th->text_secondary);
        DrawTextA(dc_tr, "Attribute", -1, &rc_sw_var, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        if (g_curve_submode == CURVE_SUBMODE_DRAW) {
            if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
            SelectObject(dc_tr, g_font_mono_regular);
            SetTextColor(dc_tr, is_dark ? RGB(160, 180, 210) : RGB(70, 90, 120));
            RECT rc_sub = { 8, 76 + sy, tr_w - 8, 98 + sy };
            DrawTextA(dc_tr, "Pen Tool: Geometry Active", -1, &rc_sub, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        } else {
            /* Interactive Attribute Controls for Attribute Submode */
            /* Row 3 Left: Boolean Box (Checkbox) for Unhittable */
            bool is_unhittable = (g_var_tunnel_val == 1);
            RECT rc_box = { 10, 80 + sy, 24, 94 + sy };
            COLORREF box_bg = is_unhittable ? RGB(220, 90, 40) : (is_dark ? RGB(30, 35, 46) : RGB(255, 255, 255));
            HBRUSH br_box = CreateSolidBrush(box_bg);
            FillRect(dc_tr, &rc_box, br_box);
            DeleteObject(br_box);

            HPEN pen_box = CreatePen(PS_SOLID, 1, is_unhittable ? RGB(255, 140, 50) : (is_dark ? RGB(80, 95, 120) : RGB(160, 175, 195)));
            HPEN old_pen = (HPEN)SelectObject(dc_tr, pen_box);
            SelectObject(dc_tr, GetStockObject(NULL_BRUSH));
            Rectangle(dc_tr, rc_box.left, rc_box.top, rc_box.right, rc_box.bottom);
            SelectObject(dc_tr, old_pen);
            DeleteObject(pen_box);

            if (is_unhittable) {
                HPEN chk_pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                HPEN prev_p = (HPEN)SelectObject(dc_tr, chk_pen);
                POINT pts[3] = {
                    { rc_box.left + 3, rc_box.top + 7 },
                    { rc_box.left + 6, rc_box.top + 10 },
                    { rc_box.left + 11, rc_box.top + 3 }
                };
                Polyline(dc_tr, pts, 3);
                SelectObject(dc_tr, prev_p);
                DeleteObject(chk_pen);
            }

            RECT rc_chk_lbl = { 28, 76 + sy, 110, 98 + sy };
            SelectObject(dc_tr, is_unhittable ? g_font_primary_bold : g_font_primary_regular);
            SetTextColor(dc_tr, is_unhittable ? RGB(255, 140, 50) : th->text_primary);
            DrawTextA(dc_tr, "Unhittable", -1, &rc_chk_lbl, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

            /* Row 3 Right: Numeric Input for Priority */
            RECT rc_prio_lbl = { 110, 76 + sy, 168, 98 + sy };
            SelectObject(dc_tr, g_font_primary_regular);
            SetTextColor(dc_tr, th->text_secondary);
            DrawTextA(dc_tr, "Priority:", -1, &rc_prio_lbl, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

            RECT rc_prio_frame = { 172, 76 + sy, 228, 98 + sy };
            HBRUSH br_prio_bg = CreateSolidBrush(is_dark ? RGB(36, 40, 48) : RGB(255, 255, 255));
            FillRect(dc_tr, &rc_prio_frame, br_prio_bg);
            DeleteObject(br_prio_bg);
            FrameRect(dc_tr, &rc_prio_frame, (HBRUSH)GetStockObject(BLACK_BRUSH));

            if (g_edit_prio) {
                int prio_x = rc_tr.left + 172;
                int prio_y = rc_tr.top + 76 + sy;
                int prio_w = 56;
                int prio_h = 22;
                if (prio_y >= rc_tr.top + 23 && prio_y + prio_h <= rc_tr.bottom) {
                    SetWindowPos(g_edit_prio, HWND_TOP, prio_x, prio_y, prio_w, prio_h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
                    if (!g_in_prio_update && GetFocus() != g_edit_prio) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%u", g_var_prio_val);
                        char cur_txt[16] = {0};
                        GetWindowTextA(g_edit_prio, cur_txt, sizeof(cur_txt));
                        if (strcmp(cur_txt, buf) != 0) {
                            g_in_prio_update = true;
                            SetWindowTextA(g_edit_prio, buf);
                            g_in_prio_update = false;
                        }
                    }
                } else {
                    ShowWindow(g_edit_prio, SW_HIDE);
                }
            }
        }

        SelectClipRgn(dc_tr, NULL);
        DeleteObject(clip_tr);

        /* Draw Vertical Scrollbar if overflowing */
        int track_y, track_h, thumb_y, thumb_h;
        if (get_attr_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
            int track_x = tr_w - 6;
            int track_w = 3;
            COLORREF track_col = is_dark ? RGB(38, 44, 58) : RGB(218, 224, 235);
            HBRUSH tr_br = CreateSolidBrush(track_col);
            RECT rc_sb = { track_x, track_y, track_x + track_w, track_y + track_h };
            FillRect(dc_tr, &rc_sb, tr_br);
            DeleteObject(tr_br);

            COLORREF thumb_col;
            if (is_dark) {
                thumb_col = (g_is_dragging_attr_thumb || g_is_dragging_attr_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
            } else {
                thumb_col = (g_is_dragging_attr_thumb || g_is_dragging_attr_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
            }
            HBRUSH th_br = CreateSolidBrush(thumb_col);
            HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
            HPEN old_th_p = (HPEN)SelectObject(dc_tr, th_pen);
            HBRUSH old_th_b = (HBRUSH)SelectObject(dc_tr, th_br);
            RoundRect(dc_tr, track_x, thumb_y, track_x + track_w, thumb_y + thumb_h, 3, 3);
            SelectObject(dc_tr, old_th_b);
            SelectObject(dc_tr, old_th_p);
            DeleteObject(th_br);
            DeleteObject(th_pen);
        }
    }

    BYTE alpha_tr = g_hud_mode_collapsed ? 128 : 255;
    blit_hud_surface(mem_dc, rc_tr.left, rc_tr.top, tr_w, tr_h, dc_tr, alpha_tr);

    SelectObject(dc_tr, old_bm_tr);
    DeleteObject(bm_tr);
    DeleteDC(dc_tr);

    /* -------------------------------------------------------------
     * 2. Top-Left HUD: Dynamic Shortcuts Cheat-Sheet
     * ------------------------------------------------------------- */
    RECT rc_tl;
    get_top_left_hud_rect(work_h, &rc_tl);
    int tl_w = rc_tl.right - rc_tl.left;
    int tl_h = rc_tl.bottom - rc_tl.top;

    HDC dc_tl = CreateCompatibleDC(mem_dc);
    HBITMAP bm_tl = CreateCompatibleBitmap(mem_dc, tl_w, tl_h);
    HBITMAP old_bm_tl = (HBITMAP)SelectObject(dc_tl, bm_tl);

    HBRUSH br_bg_tl = CreateSolidBrush(panel_bg);
    RECT rc_tl_local = { 0, 0, tl_w, tl_h };
    FillRect(dc_tl, &rc_tl_local, br_bg_tl);
    DeleteObject(br_bg_tl);

    HPEN pen_bdr_tl = CreatePen(PS_SOLID, 1, panel_border);
    HPEN old_pen_tl = (HPEN)SelectObject(dc_tl, pen_bdr_tl);
    SelectObject(dc_tl, GetStockObject(NULL_BRUSH));
    Rectangle(dc_tl, 0, 0, tl_w, tl_h);
    SelectObject(dc_tl, old_pen_tl);
    DeleteObject(pen_bdr_tl);

    SetBkMode(dc_tl, TRANSPARENT);

    if (g_hud_shortcuts_collapsed) {
        SelectObject(dc_tl, g_font_primary_bold);
        SetTextColor(dc_tl, th->text_accent_blue);
        RECT rc_t = { 8, 0, 75, tl_h };
        DrawTextA(dc_tl, "[?] Guide", -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        /* Clear small keyboard shortcut hint */
        SelectObject(dc_tl, g_font_mono_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_hint = { 76, 0, tl_w - 22, tl_h };
        DrawTextA(dc_tl, "[F1]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_primary_regular);
        SetTextColor(dc_tl, th->text_primary);
        RECT rc_chev = { tl_w - 20, 0, tl_w - 4, tl_h };
        DrawTextA(dc_tl, "v", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        SelectObject(dc_tl, g_font_primary_bold);
        SetTextColor(dc_tl, th->text_primary);
        RECT rc_title = { 8, 4, 160, 22 };
        const char *hdr = (g_curve_submode == CURVE_SUBMODE_DRAW) ? "Guide: Draw Mode" : "Guide: Attribute Mode";
        DrawTextA(dc_tl, hdr, -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        /* Clear small keyboard shortcut hint */
        SelectObject(dc_tl, g_font_mono_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_hint = { tl_w - 120, 4, tl_w - 24, 22 };
        DrawTextA(dc_tl, "[F1 to hide]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tl, g_font_primary_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_chev = { tl_w - 20, 4, tl_w - 4, 22 };
        DrawTextA(dc_tl, "^", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        /* Separator line below title */
        HPEN sep_pen = CreatePen(PS_SOLID, 1, is_dark ? RGB(45, 52, 68) : RGB(218, 224, 235));
        HPEN old_sep = (HPEN)SelectObject(dc_tl, sep_pen);
        MoveToEx(dc_tl, 8, 24, NULL);
        LineTo(dc_tl, tl_w - 8, 24);
        SelectObject(dc_tl, old_sep);
        DeleteObject(sep_pen);

        /* Clip content area below separator */
        HRGN clip_tl = CreateRectRgn(1, 25, tl_w - 1, tl_h - 1);
        SelectClipRgn(dc_tl, clip_tl);

        int cur_y = 28 - g_hud_guide_scroll_y;
        int item_h = 20;

        const GuideRow *cur_guide = (g_curve_submode == CURVE_SUBMODE_DRAW) ? s_draw_guide : s_attr_guide;
        int count = (g_curve_submode == CURVE_SUBMODE_DRAW) ?
            (int)(sizeof(s_draw_guide) / sizeof(s_draw_guide[0])) :
            (int)(sizeof(s_attr_guide) / sizeof(s_attr_guide[0]));

        for (int i = 0; i < count; ++i) {
            if (cur_guide[i].is_primary) {
                /* Distinct highlight background with subtle gold tint */
                RECT rc_row_bg = { 6, cur_y, tl_w - 6, cur_y + item_h - 2 };
                COLORREF bg_tint = is_dark ? RGB(45, 38, 18) : RGB(254, 247, 228);
                HBRUSH br_hl = CreateSolidBrush(bg_tint);
                FillRect(dc_tl, &rc_row_bg, br_hl);
                DeleteObject(br_hl);

                /* Left vertical accent bar in pure gold */
                RECT rc_accent_bar = { 6, cur_y, 9, cur_y + item_h - 2 };
                COLORREF bar_col = is_dark ? RGB(255, 215, 0) : RGB(210, 130, 0);
                HBRUSH br_bar = CreateSolidBrush(bar_col);
                FillRect(dc_tl, &rc_accent_bar, br_bar);
                DeleteObject(br_bar);

                /* Primary Key badge: Pure Golden Yellow */
                SelectObject(dc_tl, g_font_mono_bold);
                COLORREF key_gold = is_dark ? RGB(255, 215, 0) : RGB(190, 110, 0);
                SetTextColor(dc_tl, key_gold);
                RECT rc_badge = { 13, cur_y, 138, cur_y + item_h };
                DrawTextA(dc_tl, cur_guide[i].key, -1, &rc_badge, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

                /* Primary Description: Warm Gold font, distinctly specialized color */
                SelectObject(dc_tl, g_font_primary_bold);
                COLORREF desc_gold = is_dark ? RGB(255, 235, 120) : RGB(140, 75, 0);
                SetTextColor(dc_tl, desc_gold);
                RECT rc_desc = { 140, cur_y, tl_w - 8, cur_y + item_h };
                DrawTextA(dc_tl, cur_guide[i].desc, -1, &rc_desc, DT_WORDBREAK | DT_VCENTER | DT_LEFT);

                cur_y += item_h;

                /* Divider line separating primary navigation shortcuts from standard editing actions */
                if (i + 1 < count && !cur_guide[i + 1].is_primary) {
                    cur_y += 3;
                    HPEN div_pen = CreatePen(PS_SOLID, 1, is_dark ? RGB(65, 55, 30) : RGB(215, 195, 150));
                    HPEN old_div = (HPEN)SelectObject(dc_tl, div_pen);
                    MoveToEx(dc_tl, 8, cur_y, NULL);
                    LineTo(dc_tl, tl_w - 8, cur_y);
                    SelectObject(dc_tl, old_div);
                    DeleteObject(div_pen);
                    cur_y += 5;
                }
            } else {
                /* Standard tool action row */
                SelectObject(dc_tl, g_font_mono_bold);
                SetTextColor(dc_tl, th->text_accent_blue);
                RECT rc_badge = { 8, cur_y, 138, cur_y + item_h };
                DrawTextA(dc_tl, cur_guide[i].key, -1, &rc_badge, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

                SelectObject(dc_tl, g_font_primary_regular);
                SetTextColor(dc_tl, th->text_primary);
                RECT rc_desc = { 140, cur_y, tl_w - 8, cur_y + item_h };
                DrawTextA(dc_tl, cur_guide[i].desc, -1, &rc_desc, DT_WORDBREAK | DT_VCENTER | DT_LEFT);

                cur_y += item_h;
            }
        }

        SelectClipRgn(dc_tl, NULL);
        DeleteObject(clip_tl);

        /* Draw Vertical Scrollbar if overflowing */
        int track_y, track_h, thumb_y, thumb_h;
        if (get_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
            int track_x = tl_w - 6;
            int track_w = 3;
            COLORREF track_col = is_dark ? RGB(38, 44, 58) : RGB(218, 224, 235);
            HBRUSH tr_br = CreateSolidBrush(track_col);
            RECT rc_sb = { track_x, track_y, track_x + track_w, track_y + track_h };
            FillRect(dc_tl, &rc_sb, tr_br);
            DeleteObject(tr_br);

            COLORREF thumb_col;
            if (is_dark) {
                thumb_col = (g_is_dragging_guide_thumb || g_is_dragging_guide_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
            } else {
                thumb_col = (g_is_dragging_guide_thumb || g_is_dragging_guide_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
            }
            HBRUSH th_br = CreateSolidBrush(thumb_col);
            HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
            HPEN old_th_p = (HPEN)SelectObject(dc_tl, th_pen);
            HBRUSH old_th_b = (HBRUSH)SelectObject(dc_tl, th_br);
            RoundRect(dc_tl, track_x, thumb_y, track_x + track_w, thumb_y + thumb_h, 3, 3);
            SelectObject(dc_tl, old_th_b);
            SelectObject(dc_tl, old_th_p);
            DeleteObject(th_br);
            DeleteObject(th_pen);
        }
    }

    BYTE alpha_tl = g_hud_shortcuts_collapsed ? 128 : 255;
    blit_hud_surface(mem_dc, rc_tl.left, rc_tl.top, tl_w, tl_h, dc_tl, alpha_tl);

    SelectObject(dc_tl, old_bm_tl);
    DeleteObject(bm_tl);
    DeleteDC(dc_tl);
}

/* Render Top-Left Floating Overlay Guide HUD (Collapsible with F1) */
void render_overlay_guide_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th) {
    (void)client_w;
    if (!g_mode_overlay || !g_has_canvas) return;

    int max_guide_s = get_overlay_guide_hud_max_scroll(work_h);
    if (g_hud_overlay_guide_scroll_y > max_guide_s) g_hud_overlay_guide_scroll_y = max_guide_s;
    if (g_hud_overlay_guide_scroll_y < 0) g_hud_overlay_guide_scroll_y = 0;

    bool is_dark = (g_current_theme == THEME_DARK);
    COLORREF panel_bg = is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250);
    COLORREF panel_border = is_dark ? RGB(60, 70, 92) : RGB(190, 198, 212);

    RECT rc_tl;
    get_top_left_overlay_guide_hud_rect(work_h, &rc_tl);
    int tl_w = rc_tl.right - rc_tl.left;
    int tl_h = rc_tl.bottom - rc_tl.top;

    HDC dc_tl = CreateCompatibleDC(mem_dc);
    HBITMAP bm_tl = CreateCompatibleBitmap(mem_dc, tl_w, tl_h);
    HBITMAP old_bm_tl = (HBITMAP)SelectObject(dc_tl, bm_tl);

    HBRUSH br_bg_tl = CreateSolidBrush(panel_bg);
    RECT rc_tl_local = { 0, 0, tl_w, tl_h };
    FillRect(dc_tl, &rc_tl_local, br_bg_tl);
    DeleteObject(br_bg_tl);

    HPEN pen_bdr_tl = CreatePen(PS_SOLID, 1, panel_border);
    HPEN old_pen_tl = (HPEN)SelectObject(dc_tl, pen_bdr_tl);
    SelectObject(dc_tl, GetStockObject(NULL_BRUSH));
    Rectangle(dc_tl, 0, 0, tl_w, tl_h);
    SelectObject(dc_tl, old_pen_tl);
    DeleteObject(pen_bdr_tl);

    SetBkMode(dc_tl, TRANSPARENT);

    if (g_hud_overlay_guide_collapsed) {
        SelectObject(dc_tl, g_font_primary_bold);
        SetTextColor(dc_tl, th->text_accent_blue);
        RECT rc_t = { 8, 0, 75, tl_h };
        DrawTextA(dc_tl, "[?] Guide", -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_mono_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_hint = { 76, 0, tl_w - 22, tl_h };
        DrawTextA(dc_tl, "[F1]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_primary_regular);
        SetTextColor(dc_tl, th->text_primary);
        RECT rc_chev = { tl_w - 20, 0, tl_w - 4, tl_h };
        DrawTextA(dc_tl, "v", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        SelectObject(dc_tl, g_font_primary_bold);
        SetTextColor(dc_tl, th->text_primary);
        RECT rc_title = { 8, 4, 180, 22 };
        DrawTextA(dc_tl, "Guide: Overlay Mode", -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_mono_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_hint = { tl_w - 120, 4, tl_w - 24, 22 };
        DrawTextA(dc_tl, "[F1 to hide]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tl, g_font_primary_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_chev = { tl_w - 20, 4, tl_w - 4, 22 };
        DrawTextA(dc_tl, "^", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        /* Separator line below title */
        HPEN sep_pen = CreatePen(PS_SOLID, 1, is_dark ? RGB(45, 52, 68) : RGB(218, 224, 235));
        HPEN old_sep = (HPEN)SelectObject(dc_tl, sep_pen);
        MoveToEx(dc_tl, 8, 24, NULL);
        LineTo(dc_tl, tl_w - 8, 24);
        SelectObject(dc_tl, old_sep);
        DeleteObject(sep_pen);

        /* Clip content area below separator */
        HRGN clip_tl = CreateRectRgn(1, 25, tl_w - 1, tl_h - 1);
        SelectClipRgn(dc_tl, clip_tl);

        int cur_y = 28 - g_hud_overlay_guide_scroll_y;
        int track_y, track_h, thumb_y, thumb_h;
        bool has_scroll = get_overlay_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h);
        int text_right = has_scroll ? (tl_w - 14) : (tl_w - 10);

        int num_lines = (int)(sizeof(g_overlay_guide_lines) / sizeof(g_overlay_guide_lines[0]));
        for (int i = 0; i < num_lines; ++i) {
            bool is_header = (i == 0 ||
                              strcmp(g_overlay_guide_lines[i], "Selection & Multi-Select:") == 0 ||
                              strcmp(g_overlay_guide_lines[i], "Overlay Variable Adjustment:") == 0);
            bool is_footer_shortcuts = (i == num_lines - 1);

            if (is_header) {
                SelectObject(dc_tl, g_font_primary_bold);
                SetTextColor(dc_tl, th->text_accent_blue);
            } else if (is_footer_shortcuts) {
                SelectObject(dc_tl, g_font_mono_bold);
                COLORREF gold_col = is_dark ? RGB(255, 215, 0) : RGB(190, 110, 0);
                SetTextColor(dc_tl, gold_col);
            } else {
                SelectObject(dc_tl, g_font_primary_regular);
                SetTextColor(dc_tl, th->text_primary);
            }

            /* Calculate multi-line wrapped height */
            RECT rc_calc = { 10, 0, text_right, 0 };
            DrawTextA(dc_tl, g_overlay_guide_lines[i], -1, &rc_calc, DT_CALCRECT | DT_WORDBREAK);
            int row_h = rc_calc.bottom - rc_calc.top;
            if (row_h < 18) row_h = 18;

            RECT rc_draw = { 10, cur_y, text_right, cur_y + row_h };
            DrawTextA(dc_tl, g_overlay_guide_lines[i], -1, &rc_draw, DT_WORDBREAK | DT_LEFT);

            if (!is_footer_shortcuts) cur_y += row_h + 3;
            else cur_y += row_h;
        }

        SelectClipRgn(dc_tl, NULL);
        DeleteObject(clip_tl);

        /* Draw Vertical Scrollbar ONLY IF content overflows (menabrak bagian bawah) */
        if (has_scroll) {
            int track_x = tl_w - 6;
            int track_w = 3;
            COLORREF track_col = is_dark ? RGB(38, 44, 58) : RGB(218, 224, 235);
            HBRUSH tr_br = CreateSolidBrush(track_col);
            RECT rc_sb = { track_x, track_y, track_x + track_w, track_y + track_h };
            FillRect(dc_tl, &rc_sb, tr_br);
            DeleteObject(tr_br);

            COLORREF thumb_col;
            if (is_dark) {
                thumb_col = (g_is_dragging_ov_guide_thumb || g_is_dragging_ov_guide_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
            } else {
                thumb_col = (g_is_dragging_ov_guide_thumb || g_is_dragging_ov_guide_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
            }
            HBRUSH th_br = CreateSolidBrush(thumb_col);
            HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
            HPEN old_th_p = (HPEN)SelectObject(dc_tl, th_pen);
            HBRUSH old_th_b = (HBRUSH)SelectObject(dc_tl, th_br);
            RoundRect(dc_tl, track_x, thumb_y, track_x + track_w, thumb_y + thumb_h, 3, 3);
            SelectObject(dc_tl, old_th_b);
            SelectObject(dc_tl, old_th_p);
            DeleteObject(th_br);
            DeleteObject(th_pen);
        }
    }

    BYTE alpha_tl = g_hud_overlay_guide_collapsed ? 128 : 255;
    blit_hud_surface(mem_dc, rc_tl.left, rc_tl.top, tl_w, tl_h, dc_tl, alpha_tl);

    SelectObject(dc_tl, old_bm_tl);
    DeleteObject(bm_tl);
    DeleteDC(dc_tl);
}

/* Render Top-Right Floating Overlay Mode & Tools HUD (Collapsible with F2) */
void render_overlay_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th) {
    if (!g_mode_overlay || !g_has_canvas) return;

    bool is_dark = (g_current_theme == THEME_DARK);
    COLORREF panel_bg = is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250);
    COLORREF panel_border = is_dark ? RGB(60, 70, 92) : RGB(190, 198, 212);

    RECT rc_tr;
    get_top_right_overlay_hud_rect(client_w, work_h, &rc_tr);
    int tr_w = rc_tr.right - rc_tr.left;
    int tr_h = rc_tr.bottom - rc_tr.top;

    HDC dc_tr = CreateCompatibleDC(mem_dc);
    HBITMAP bm_tr = CreateCompatibleBitmap(mem_dc, tr_w, tr_h);
    HBITMAP old_bm_tr = (HBITMAP)SelectObject(dc_tr, bm_tr);

    HBRUSH br_bg_tr = CreateSolidBrush(panel_bg);
    RECT rc_tr_local = { 0, 0, tr_w, tr_h };
    FillRect(dc_tr, &rc_tr_local, br_bg_tr);
    DeleteObject(br_bg_tr);

    HPEN pen_bdr_tr = CreatePen(PS_SOLID, 1, panel_border);
    HPEN old_pen_tr = (HPEN)SelectObject(dc_tr, pen_bdr_tr);
    SelectObject(dc_tr, GetStockObject(NULL_BRUSH));
    Rectangle(dc_tr, 0, 0, tr_w, tr_h);
    SelectObject(dc_tr, old_pen_tr);
    DeleteObject(pen_bdr_tr);

    SetBkMode(dc_tr, TRANSPARENT);

    int max_ov_tools_s = get_overlay_tools_hud_max_scroll(work_h);
    if (g_hud_overlay_tools_scroll_y > max_ov_tools_s) g_hud_overlay_tools_scroll_y = max_ov_tools_s;
    if (g_hud_overlay_tools_scroll_y < 0) g_hud_overlay_tools_scroll_y = 0;

    if (g_hud_overlay_collapsed) {
        if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_t = { 8, 0, tr_w - 52, tr_h };
        DrawTextA(dc_tr, "[+] Overlay Tools", -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_hint = { tr_w - 52, 0, tr_w - 22, tr_h };
        DrawTextA(dc_tr, "[F2]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_chev = { tr_w - 20, 0, tr_w - 4, tr_h };
        DrawTextA(dc_tr, "v", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        /* Header */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_title = { 8, 4, 126, 20 };
        DrawTextA(dc_tr, "Overlay Tools", -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_hint = { tr_w - 110, 4, tr_w - 22, 20 };
        DrawTextA(dc_tr, "[F2 to hide]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_chev = { tr_w - 20, 4, tr_w - 4, 20 };
        DrawTextA(dc_tr, "^", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        /* Clip content area below header */
        HRGN clip_tr = CreateRectRgn(1, 23, tr_w - 1, tr_h - 1);
        SelectClipRgn(dc_tr, clip_tr);

        int sy = -g_hud_overlay_tools_scroll_y;

        /* Row 1 (atas, full width): [+ Background Alpha] */
        RECT rc_btn_bg = { 8, 24 + sy, 228, 46 + sy };
        COLORREF bg_col_bg = g_placing_bgalpha ? th->text_accent_blue : (is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236));
        HBRUSH br_btn_bg = CreateSolidBrush(bg_col_bg);
        FillRect(dc_tr, &rc_btn_bg, br_btn_bg);
        DeleteObject(br_btn_bg);
        FrameRect(dc_tr, &rc_btn_bg, (HBRUSH)GetStockObject(BLACK_BRUSH));

        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, g_placing_bgalpha ? RGB(255, 255, 255) : th->text_accent_blue);
        DrawTextA(dc_tr, "+ Background Alpha", -1, &rc_btn_bg, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        /* Row 2: Bawah kiri: [+ Sprite], Bawah kanan: [+ Cutout] */
        RECT rc_btn_sp = { 8, 50 + sy, 114, 72 + sy };
        RECT rc_btn_co = { 120, 50 + sy, 228, 72 + sy };

        COLORREF bg_col_sp = g_placing_sprite ? th->text_accent_blue : (is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236));
        HBRUSH br_btn_sp = CreateSolidBrush(bg_col_sp);
        FillRect(dc_tr, &rc_btn_sp, br_btn_sp);
        DeleteObject(br_btn_sp);
        FrameRect(dc_tr, &rc_btn_sp, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetTextColor(dc_tr, g_placing_sprite ? RGB(255, 255, 255) : th->text_accent_blue);
        DrawTextA(dc_tr, "+ Sprite", -1, &rc_btn_sp, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        COLORREF bg_col_co = g_placing_cutout ? th->text_accent_blue : (is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236));
        HBRUSH br_btn_co = CreateSolidBrush(bg_col_co);
        FillRect(dc_tr, &rc_btn_co, br_btn_co);
        DeleteObject(br_btn_co);
        FrameRect(dc_tr, &rc_btn_co, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetTextColor(dc_tr, g_placing_cutout ? RGB(255, 255, 255) : th->text_accent_blue);
        DrawTextA(dc_tr, "+ Cutout", -1, &rc_btn_co, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        /* Row 3: Dynamic Context Button [Set Variable / Set Motion / Set Priority] (kiri) dan Priority Edit (kanan) */
        RECT rc_btn_motion = { 8, 76 + sy, 104, 98 + sy };
        int sel_ba_idx = get_active_selected_bgalpha_idx();
        int sel_co_idx = get_active_selected_cutout_idx();
        int sel_sp_idx = get_active_selected_sprite_idx();
        bool has_sel_ba = (g_mode_overlay && sel_ba_idx >= 0 && sel_ba_idx < g_bg_alpha_count);
        bool has_sel_co = (g_mode_overlay && sel_co_idx >= 0 && sel_co_idx < g_cutout_count);
        bool has_sel_sp = (g_mode_overlay && sel_sp_idx >= 0 && sel_sp_idx < g_sprite_count);
        bool has_target = has_sel_ba || has_sel_co || has_sel_sp;

        bool is_active_mode = (has_sel_ba && g_placing_bgalpha_vector) ||
                              ((has_sel_co || has_sel_sp) && g_adjusting_overlay_priority);

        const char *btn_text = "Set Variable";
        if (has_sel_ba) {
            btn_text = "Set Motion";
        } else if (has_sel_co || has_sel_sp) {
            btn_text = "Set Priority";
        }

        COLORREF bg_col_motion;
        COLORREF text_col_motion;
        if (is_active_mode) {
            bg_col_motion = th->text_accent_blue;
            text_col_motion = RGB(255, 255, 255);
        } else if (has_target) {
            bg_col_motion = is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236);
            text_col_motion = th->text_accent_blue;
        } else {
            /* Disabled / 50% opacity */
            bg_col_motion = is_dark ? RGB(26, 30, 40) : RGB(236, 239, 244);
            text_col_motion = is_dark ? RGB(90, 98, 112) : RGB(165, 172, 182);
        }

        HBRUSH br_btn_motion = CreateSolidBrush(bg_col_motion);
        FillRect(dc_tr, &rc_btn_motion, br_btn_motion);
        DeleteObject(br_btn_motion);

        HBRUSH br_border_motion = CreateSolidBrush(has_target ? (is_dark ? RGB(60, 70, 90) : RGB(170, 180, 200)) : (is_dark ? RGB(40, 45, 55) : RGB(210, 215, 222)));
        FrameRect(dc_tr, &rc_btn_motion, br_border_motion);
        DeleteObject(br_border_motion);

        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, text_col_motion);
        DrawTextA(dc_tr, btn_text, -1, &rc_btn_motion, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        RECT rc_prio_lbl = { 108, 76 + sy, 168, 98 + sy };
        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);
        DrawTextA(dc_tr, "Priority:", -1, &rc_prio_lbl, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        RECT rc_prio_frame = { 172, 76 + sy, 228, 98 + sy };
        HBRUSH br_prio_bg = CreateSolidBrush(is_dark ? RGB(36, 40, 48) : RGB(255, 255, 255));
        FillRect(dc_tr, &rc_prio_frame, br_prio_bg);
        DeleteObject(br_prio_bg);
        FrameRect(dc_tr, &rc_prio_frame, (HBRUSH)GetStockObject(BLACK_BRUSH));

        if (g_edit_prio) {
            int prio_x = rc_tr.left + 172;
            int prio_y = rc_tr.top + 76 + sy;
            int prio_w = 56;
            int prio_h = 22;
            if (prio_y >= rc_tr.top + 23 && prio_y + prio_h <= rc_tr.bottom) {
                SetWindowPos(g_edit_prio, HWND_TOP, prio_x, prio_y, prio_w, prio_h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
                if (!g_in_prio_update && GetFocus() != g_edit_prio) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%d", g_overlay_current_prio);
                    char cur_txt[16] = {0};
                    GetWindowTextA(g_edit_prio, cur_txt, sizeof(cur_txt));
                    if (strcmp(cur_txt, buf) != 0) {
                        g_in_prio_update = true;
                        SetWindowTextA(g_edit_prio, buf);
                        g_in_prio_update = false;
                    }
                }
            } else {
                ShowWindow(g_edit_prio, SW_HIDE);
            }
        }

        SelectClipRgn(dc_tr, NULL);
        DeleteObject(clip_tr);

        /* Draw Vertical Scrollbar if overflowing (menabrak bagian bawah) */
        int track_y, track_h, thumb_y, thumb_h;
        if (get_overlay_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
            int track_x = tr_w - 6;
            int track_w = 3;
            COLORREF track_col = is_dark ? RGB(38, 44, 58) : RGB(218, 224, 235);
            HBRUSH tr_br = CreateSolidBrush(track_col);
            RECT rc_sb = { track_x, track_y, track_x + track_w, track_y + track_h };
            FillRect(dc_tr, &rc_sb, tr_br);
            DeleteObject(tr_br);

            COLORREF thumb_col;
            if (is_dark) {
                thumb_col = (g_is_dragging_ov_tools_thumb || g_is_dragging_ov_tools_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
            } else {
                thumb_col = (g_is_dragging_ov_tools_thumb || g_is_dragging_ov_tools_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
            }
            HBRUSH th_br = CreateSolidBrush(thumb_col);
            HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
            HPEN old_th_p = (HPEN)SelectObject(dc_tr, th_pen);
            HBRUSH old_th_b = (HBRUSH)SelectObject(dc_tr, th_br);
            RoundRect(dc_tr, track_x, thumb_y, track_x + track_w, thumb_y + thumb_h, 3, 3);
            SelectObject(dc_tr, old_th_b);
            SelectObject(dc_tr, old_th_p);
            DeleteObject(th_br);
            DeleteObject(th_pen);
        }
    }

    BYTE alpha_tr = g_hud_overlay_collapsed ? 128 : 255;
    blit_hud_surface(mem_dc, rc_tr.left, rc_tr.top, tr_w, tr_h, dc_tr, alpha_tr);
    SelectObject(dc_tr, old_bm_tr);
    DeleteObject(bm_tr);
    DeleteDC(dc_tr);
}

/* =========================================================================
 * ELEMENT MODE: TOP-LEFT GUIDE HUD & TOP-RIGHT TOOLS HUD
 * ========================================================================= */

static const char *g_element_guide_lines[] = {
    "Element Tools:",
    "  * Click [Edit Frog] or press Ctrl+P to place / reposition Frog",
    "  * Click [+ Coin] or press Ctrl+K to add / place Treasure Point",
    "  * LMB on Placed Item: Pick up & carry to reposition",
    "  * RMB (Right Click): Delete hovered Coin / cancel carry (Frog cannot be deleted)",
    "Selection & Multi-Select:",
    "  * Ctrl + LMB: Select item (Replace selection)",
    "  * Ctrl + Shift + LMB / Drag: Add to Multi-Select",
    "  * Alt + Shift + LMB / Drag: Subtract from Multi-Select",
    "  * Ctrl + Alt + Shift + LMB / Drag: Toggle Multi-Select",
    "  * Ctrl + A: Select All | Ctrl + I: Invert",
    "  * Ctrl + D: Deselect All",
    "  * Delete: Delete multi-selected Coin(s) (Frog cannot be deleted)",
    "  * Arrow Keys: Nudge selected items by 1px",
    "Shortcuts: Ctrl+P (Frog) | Ctrl+K (Coin) | F1 (Guide) | F2 (Tools)"
};

int get_element_guide_content_height(void) {
    if (g_hud_element_guide_collapsed) return 26;

    int num_lines = (int)(sizeof(g_element_guide_lines) / sizeof(g_element_guide_lines[0]));
    int text_w = 360 - 24;

    HDC screen_dc = GetDC(NULL);
    int total_h = 28; /* Top title bar (24px) + margin (4px) */
    for (int i = 0; i < num_lines; ++i) {
        bool is_footer_shortcuts = (i == num_lines - 1);
        bool is_header = (!is_footer_shortcuts && g_element_guide_lines[i][0] != ' ');
        HFONT font = is_header ? g_font_primary_bold : (is_footer_shortcuts ? g_font_mono_bold : g_font_primary_regular);
        HFONT old_f = (HFONT)SelectObject(screen_dc, font);
        RECT rc = { 0, 0, text_w, 0 };
        DrawTextA(screen_dc, g_element_guide_lines[i], -1, &rc, DT_CALCRECT | DT_WORDBREAK);
        SelectObject(screen_dc, old_f);
        int row_h = rc.bottom - rc.top;
        if (row_h < 18) row_h = 18;
        total_h += row_h + 3;
    }
    ReleaseDC(NULL, screen_dc);
    total_h += 8; /* bottom padding */
    return total_h;
}

int get_element_guide_hud_max_scroll(int work_h) {
    if (g_hud_element_guide_collapsed) return 0;
    int content_h = get_element_guide_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int max_s = content_h - max_avail_h;
    return max_s > 0 ? max_s : 0;
}

bool get_element_guide_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h) {
    int max_s = get_element_guide_hud_max_scroll(work_h);
    if (max_s <= 0) return false;

    int content_h = get_element_guide_content_height();
    int avail_h = work_h - 16;
    if (avail_h < 60) avail_h = 60;

    int track_y = 28;
    int track_h = avail_h - 34;
    if (track_h <= 20) return false;

    float vis_ratio = (float)avail_h / (float)content_h;
    int thumb_h = (int)roundf((float)track_h * vis_ratio);
    if (thumb_h < 25) thumb_h = 25;
    if (thumb_h > track_h) thumb_h = track_h;

    float scroll_ratio = (max_s > 0) ? ((float)g_hud_element_guide_scroll_y / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_y = track_y + (int)roundf(scroll_ratio * (float)(track_h - thumb_h));

    if (out_track_y) *out_track_y = track_y;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return true;
}

void get_top_left_element_guide_hud_rect(int work_h, RECT *rc) {
    int tl_w = g_hud_element_guide_collapsed ? 135 : 360;
    int full_h = get_element_guide_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int tl_h = g_hud_element_guide_collapsed ? 26 : ((full_h > max_avail_h) ? max_avail_h : full_h);

    rc->left = 12;
    rc->top = TOOLBAR_HEIGHT + 8;
    rc->right = rc->left + tl_w;
    rc->bottom = rc->top + tl_h;
}

void render_element_guide_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th) {
    (void)client_w;
    if (!g_mode_element || !g_has_canvas) return;

    int max_guide_s = get_element_guide_hud_max_scroll(work_h);
    if (g_hud_element_guide_scroll_y > max_guide_s) g_hud_element_guide_scroll_y = max_guide_s;
    if (g_hud_element_guide_scroll_y < 0) g_hud_element_guide_scroll_y = 0;

    bool is_dark = (g_current_theme == THEME_DARK);
    COLORREF panel_bg = is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250);
    COLORREF panel_border = is_dark ? RGB(60, 70, 92) : RGB(190, 198, 212);

    RECT rc_tl;
    get_top_left_element_guide_hud_rect(work_h, &rc_tl);
    int tl_w = rc_tl.right - rc_tl.left;
    int tl_h = rc_tl.bottom - rc_tl.top;

    HDC dc_tl = CreateCompatibleDC(mem_dc);
    HBITMAP bm_tl = CreateCompatibleBitmap(mem_dc, tl_w, tl_h);
    HBITMAP old_bm_tl = (HBITMAP)SelectObject(dc_tl, bm_tl);

    HBRUSH br_bg_tl = CreateSolidBrush(panel_bg);
    RECT rc_tl_local = { 0, 0, tl_w, tl_h };
    FillRect(dc_tl, &rc_tl_local, br_bg_tl);
    DeleteObject(br_bg_tl);

    HPEN pen_bdr_tl = CreatePen(PS_SOLID, 1, panel_border);
    HPEN old_pen_tl = (HPEN)SelectObject(dc_tl, pen_bdr_tl);
    SelectObject(dc_tl, GetStockObject(NULL_BRUSH));
    Rectangle(dc_tl, 0, 0, tl_w, tl_h);
    SelectObject(dc_tl, old_pen_tl);
    DeleteObject(pen_bdr_tl);

    SetBkMode(dc_tl, TRANSPARENT);

    if (g_hud_element_guide_collapsed) {
        SelectObject(dc_tl, g_font_primary_bold);
        SetTextColor(dc_tl, th->text_accent_blue);
        RECT rc_t = { 8, 0, 75, tl_h };
        DrawTextA(dc_tl, "[?] Guide", -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_mono_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_hint = { 76, 0, tl_w - 22, tl_h };
        DrawTextA(dc_tl, "[F1]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_primary_regular);
        SetTextColor(dc_tl, th->text_primary);
        RECT rc_chev = { tl_w - 20, 0, tl_w - 4, tl_h };
        DrawTextA(dc_tl, "v", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        SelectObject(dc_tl, g_font_primary_bold);
        SetTextColor(dc_tl, th->text_primary);
        RECT rc_title = { 8, 4, 180, 22 };
        DrawTextA(dc_tl, "Guide: Element Mode", -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_mono_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_hint = { tl_w - 120, 4, tl_w - 24, 22 };
        DrawTextA(dc_tl, "[F1 to hide]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tl, g_font_primary_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_chev = { tl_w - 20, 4, tl_w - 4, 22 };
        DrawTextA(dc_tl, "^", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        /* Separator line below title */
        HPEN sep_pen = CreatePen(PS_SOLID, 1, is_dark ? RGB(45, 52, 68) : RGB(218, 224, 235));
        HPEN old_sep = (HPEN)SelectObject(dc_tl, sep_pen);
        MoveToEx(dc_tl, 8, 24, NULL);
        LineTo(dc_tl, tl_w - 8, 24);
        SelectObject(dc_tl, old_sep);
        DeleteObject(sep_pen);

        /* Clip content area below separator */
        HRGN clip_tl = CreateRectRgn(1, 25, tl_w - 1, tl_h - 1);
        SelectClipRgn(dc_tl, clip_tl);

        int cur_y = 28 - g_hud_element_guide_scroll_y;
        int track_y, track_h, thumb_y, thumb_h;
        bool has_scroll = get_element_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h);
        int text_right = has_scroll ? (tl_w - 14) : (tl_w - 10);

        int num_lines = (int)(sizeof(g_element_guide_lines) / sizeof(g_element_guide_lines[0]));
        for (int i = 0; i < num_lines; ++i) {
            bool is_footer_shortcuts = (i == num_lines - 1);
            bool is_header = (!is_footer_shortcuts && g_element_guide_lines[i][0] != ' ');

            if (is_header) {
                SelectObject(dc_tl, g_font_primary_bold);
                SetTextColor(dc_tl, th->text_accent_blue);
            } else if (is_footer_shortcuts) {
                SelectObject(dc_tl, g_font_mono_bold);
                COLORREF gold_col = is_dark ? RGB(255, 215, 0) : RGB(190, 110, 0);
                SetTextColor(dc_tl, gold_col);
            } else {
                SelectObject(dc_tl, g_font_primary_regular);
                SetTextColor(dc_tl, th->text_primary);
            }

            /* Calculate multi-line wrapped height */
            RECT rc_calc = { 10, 0, text_right, 0 };
            DrawTextA(dc_tl, g_element_guide_lines[i], -1, &rc_calc, DT_CALCRECT | DT_WORDBREAK);
            int row_h = rc_calc.bottom - rc_calc.top;
            if (row_h < 18) row_h = 18;

            RECT rc_draw = { 10, cur_y, text_right, cur_y + row_h };
            DrawTextA(dc_tl, g_element_guide_lines[i], -1, &rc_draw, DT_WORDBREAK | DT_LEFT);

            cur_y += row_h + 3;
        }

        SelectClipRgn(dc_tl, NULL);
        DeleteObject(clip_tl);

        /* Draw Vertical Scrollbar ONLY IF content overflows (menabrak bagian bawah) */
        if (has_scroll) {
            int track_x = tl_w - 6;
            int track_w = 3;
            COLORREF track_col = is_dark ? RGB(38, 44, 58) : RGB(218, 224, 235);
            HBRUSH tr_br = CreateSolidBrush(track_col);
            RECT rc_sb = { track_x, track_y, track_x + track_w, track_y + track_h };
            FillRect(dc_tl, &rc_sb, tr_br);
            DeleteObject(tr_br);

            COLORREF thumb_col;
            if (is_dark) {
                thumb_col = (g_is_dragging_el_guide_thumb || g_is_dragging_el_guide_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
            } else {
                thumb_col = (g_is_dragging_el_guide_thumb || g_is_dragging_el_guide_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
            }
            HBRUSH th_br = CreateSolidBrush(thumb_col);
            HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
            HPEN old_th_p = (HPEN)SelectObject(dc_tl, th_pen);
            HBRUSH old_th_b = (HBRUSH)SelectObject(dc_tl, th_br);
            RoundRect(dc_tl, track_x, thumb_y, track_x + track_w, thumb_y + thumb_h, 3, 3);
            SelectObject(dc_tl, old_th_b);
            SelectObject(dc_tl, old_th_p);
            DeleteObject(th_br);
            DeleteObject(th_pen);
        }
    }

    BYTE alpha_tl = g_hud_element_guide_collapsed ? 128 : 255;
    blit_hud_surface(mem_dc, rc_tl.left, rc_tl.top, tl_w, tl_h, dc_tl, alpha_tl);

    SelectObject(dc_tl, old_bm_tl);
    DeleteObject(bm_tl);
    DeleteDC(dc_tl);
}

int get_element_tools_content_height(void) {
    if (g_hud_element_collapsed) return 26;
    return 80;
}

int get_element_tools_hud_max_scroll(int work_h) {
    if (g_hud_element_collapsed) return 0;
    int content_h = get_element_tools_content_height();
    int max_avail_h = work_h - 16;
    if (max_avail_h < 26) max_avail_h = 26;
    int max_s = content_h - max_avail_h;
    return max_s > 0 ? max_s : 0;
}

bool get_element_tools_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h) {
    int max_s = get_element_tools_hud_max_scroll(work_h);
    if (max_s <= 0) return false;

    int content_h = get_element_tools_content_height();
    int avail_h = work_h - 16;
    if (avail_h < 50) avail_h = 50;

    int track_y = 26;
    int track_h = avail_h - 32;
    if (track_h <= 15) return false;

    float vis_ratio = (float)avail_h / (float)content_h;
    int thumb_h = (int)roundf((float)track_h * vis_ratio);
    if (thumb_h < 20) thumb_h = 20;
    if (thumb_h > track_h) thumb_h = track_h;

    float scroll_ratio = (max_s > 0) ? ((float)g_hud_element_tools_scroll_y / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_y = track_y + (int)roundf(scroll_ratio * (float)(track_h - thumb_h));

    if (out_track_y) *out_track_y = track_y;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return true;
}

void get_top_right_element_hud_rect(int client_w, int work_h, RECT *rc) {
    int tr_w = g_hud_element_collapsed ? 180 : 236;
    int full_h = get_element_tools_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int tr_h = (full_h > max_avail_h) ? max_avail_h : full_h;

    rc->left = client_w - tr_w - 12;
    rc->top = TOOLBAR_HEIGHT + 8;
    rc->right = rc->left + tr_w;
    rc->bottom = rc->top + tr_h;
}

void render_element_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th) {
    if (!g_mode_element || !g_has_canvas) return;

    if (g_edit_prio && IsWindowVisible(g_edit_prio)) {
        ShowWindow(g_edit_prio, SW_HIDE);
    }

    bool is_dark = (g_current_theme == THEME_DARK);
    COLORREF panel_bg = is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250);
    COLORREF panel_border = is_dark ? RGB(60, 70, 92) : RGB(190, 198, 212);

    RECT rc_tr;
    get_top_right_element_hud_rect(client_w, work_h, &rc_tr);
    int tr_w = rc_tr.right - rc_tr.left;
    int tr_h = rc_tr.bottom - rc_tr.top;

    HDC dc_tr = CreateCompatibleDC(mem_dc);
    HBITMAP bm_tr = CreateCompatibleBitmap(mem_dc, tr_w, tr_h);
    HBITMAP old_bm_tr = (HBITMAP)SelectObject(dc_tr, bm_tr);

    HBRUSH br_bg_tr = CreateSolidBrush(panel_bg);
    RECT rc_tr_local = { 0, 0, tr_w, tr_h };
    FillRect(dc_tr, &rc_tr_local, br_bg_tr);
    DeleteObject(br_bg_tr);

    HPEN pen_bdr_tr = CreatePen(PS_SOLID, 1, panel_border);
    HPEN old_pen_tr = (HPEN)SelectObject(dc_tr, pen_bdr_tr);
    SelectObject(dc_tr, GetStockObject(NULL_BRUSH));
    Rectangle(dc_tr, 0, 0, tr_w, tr_h);
    SelectObject(dc_tr, old_pen_tr);
    DeleteObject(pen_bdr_tr);

    SetBkMode(dc_tr, TRANSPARENT);

    int max_el_tools_s = get_element_tools_hud_max_scroll(work_h);
    if (g_hud_element_tools_scroll_y > max_el_tools_s) g_hud_element_tools_scroll_y = max_el_tools_s;
    if (g_hud_element_tools_scroll_y < 0) g_hud_element_tools_scroll_y = 0;

    if (g_hud_element_collapsed) {
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_t = { 8, 0, tr_w - 52, tr_h };
        DrawTextA(dc_tr, "[+] Element Tools", -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_hint = { tr_w - 52, 0, tr_w - 22, tr_h };
        DrawTextA(dc_tr, "[F2]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_chev = { tr_w - 20, 0, tr_w - 4, tr_h };
        DrawTextA(dc_tr, "v", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        /* Header */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_title = { 8, 4, 130, 20 };
        DrawTextA(dc_tr, "Element Tools", -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_hint = { tr_w - 110, 4, tr_w - 22, 20 };
        DrawTextA(dc_tr, "[F2 to hide]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_chev = { tr_w - 20, 4, tr_w - 4, 20 };
        DrawTextA(dc_tr, "^", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        /* Clip content area below header */
        HRGN clip_tr = CreateRectRgn(1, 23, tr_w - 1, tr_h - 1);
        SelectClipRgn(dc_tr, clip_tr);

        int sy = -g_hud_element_tools_scroll_y;

        /* Row 1: Left: [+ Frog], Right: [+ Coin] */
        RECT rc_btn_fr = { 8, 24 + sy, 114, 46 + sy };
        RECT rc_btn_cn = { 120, 24 + sy, 228, 46 + sy };

        COLORREF bg_col_fr = g_placing_frog ? th->text_accent_blue : (is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236));
        HBRUSH br_btn_fr = CreateSolidBrush(bg_col_fr);
        FillRect(dc_tr, &rc_btn_fr, br_btn_fr);
        DeleteObject(br_btn_fr);
        FrameRect(dc_tr, &rc_btn_fr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, g_placing_frog ? RGB(255, 255, 255) : th->text_accent_blue);
        DrawTextA(dc_tr, "* Frog", -1, &rc_btn_fr, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        COLORREF bg_col_cn = g_placing_coin ? th->text_accent_blue : (is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236));
        HBRUSH br_btn_cn = CreateSolidBrush(bg_col_cn);
        FillRect(dc_tr, &rc_btn_cn, br_btn_cn);
        DeleteObject(br_btn_cn);
        FrameRect(dc_tr, &rc_btn_cn, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetTextColor(dc_tr, g_placing_coin ? RGB(255, 255, 255) : th->text_accent_blue);
        DrawTextA(dc_tr, "+ Coin", -1, &rc_btn_cn, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        /* Row 2: Status badges: Frog placed position & Coin count */
        RECT rc_stat_fr = { 8, 50 + sy, 114, 72 + sy };
        RECT rc_stat_cn = { 120, 50 + sy, 228, 72 + sy };

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);

        char fr_txt[32];
        if (g_has_frog) {
            snprintf(fr_txt, sizeof(fr_txt), "Frog: %d,%d", g_frog_gx, g_frog_gy);
        } else {
            snprintf(fr_txt, sizeof(fr_txt), "Frog: Not Set");
        }
        DrawTextA(dc_tr, fr_txt, -1, &rc_stat_fr, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        char cn_txt[32];
        snprintf(cn_txt, sizeof(cn_txt), "Coins: %d", g_treasure_point_count);
        DrawTextA(dc_tr, cn_txt, -1, &rc_stat_cn, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        SelectClipRgn(dc_tr, NULL);
        DeleteObject(clip_tr);

        /* Draw Vertical Scrollbar if overflowing (menabrak bagian bawah) */
        int track_y, track_h, thumb_y, thumb_h;
        if (get_element_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
            int track_x = tr_w - 6;
            int track_w = 3;
            COLORREF track_col = is_dark ? RGB(38, 44, 58) : RGB(218, 224, 235);
            HBRUSH tr_br = CreateSolidBrush(track_col);
            RECT rc_sb = { track_x, track_y, track_x + track_w, track_y + track_h };
            FillRect(dc_tr, &rc_sb, tr_br);
            DeleteObject(tr_br);

            COLORREF thumb_col;
            if (is_dark) {
                thumb_col = (g_is_dragging_el_tools_thumb || g_is_dragging_el_tools_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
            } else {
                thumb_col = (g_is_dragging_el_tools_thumb || g_is_dragging_el_tools_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
            }
            HBRUSH th_br = CreateSolidBrush(thumb_col);
            HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
            HPEN old_th_p = (HPEN)SelectObject(dc_tr, th_pen);
            HBRUSH old_th_b = (HBRUSH)SelectObject(dc_tr, th_br);
            RoundRect(dc_tr, track_x, thumb_y, track_x + track_w, thumb_y + thumb_h, 3, 3);
            SelectObject(dc_tr, old_th_b);
            SelectObject(dc_tr, old_th_p);
            DeleteObject(th_br);
            DeleteObject(th_pen);
        }
    }

    BYTE alpha_tr = g_hud_element_collapsed ? 128 : 255;
    blit_hud_surface(mem_dc, rc_tr.left, rc_tr.top, tr_w, tr_h, dc_tr, alpha_tr);
    SelectObject(dc_tr, old_bm_tr);
    DeleteObject(bm_tr);
    DeleteDC(dc_tr);
}

/* ========================================================================= */
/* Canvas Mode HUD: Guide Panel (Top-Left) & Tools Panel (Top-Right)         */
/* ========================================================================= */

static const char *g_canvas_guide_lines[] = {
    "Canvas Transformation & Controls:",
    "  * 8 Directional Arrow Pointers:",
    "    - Drag top, bottom, left, right edges to resize canvas.",
    "    - Drag 4 corners to resize width & height simultaneously.",
    "    - Top & Left resizes auto-shift scene objects (Zero Drift!).",
    "  * Center Move Cross Arrows (Center Anchor):",
    "    - Drag center 4-way cross to shift canvas frame over scene.",
    "    - Level objects stay pinned at world coordinates.",
    "  * Modifiers (Aspect Ratio & Symmetric Mirror):",
    "    - Shift + Drag Edge/Corner: Lock aspect ratio.",
    "    - Ctrl + Drag Edge/Corner: Mirror / symmetric resize.",
    "    - Ctrl + Shift + Drag: Mirror & lock aspect ratio.",
    "    - Shift + Drag Center: Snap movement to 45-degree axes.",
    "Background Image Placement & Sizing:",
    "  * Fit Modes (Image Behavior):",
    "    - Original: Background keeps raw original pixel size.",
    "    - Fit: Proportional scale to fit completely within canvas.",
    "    - Fill: Proportional scale to cover entire canvas (crops excess).",
    "    - Stretch: Scale non-proportionally to match canvas dimensions.",
    "  * Alignment (9 Anchor Points):",
    "    - Select Top-Left, Center, Bottom-Right, etc. to position background.",
    "Numeric Input & Presets:",
    "  * Enter exact W & H values in Canvas Tools HUD and click Set.",
    "  * Quick 1-click presets: 640x480, 800x600, 1024x768, 1280x720.",
    "Affect Transform Options (Synchronized Scaling):",
    "  * Include Element: Frog & Coins scale proportionally with canvas.",
    "  * Include Curve: Path anchor nodes, handles & attributes scale.",
    "  * Include Overlay: Cutouts, Sprites & Alpha masks resize & shift.",
    "F1: Guide | F2: Tools | Ctrl+M: Toggle Canvas Mode"
};

int get_canvas_guide_content_height(void) {
    if (g_hud_canvas_guide_collapsed) return 26;

    int num_lines = (int)(sizeof(g_canvas_guide_lines) / sizeof(g_canvas_guide_lines[0]));
    int text_w = 370 - 20;

    HDC screen_dc = GetDC(NULL);
    int total_h = 28; /* Top title bar (24px) + margin (4px) */
    for (int i = 0; i < num_lines; ++i) {
        bool is_footer_shortcuts = (i == num_lines - 1);
        bool is_header = (g_canvas_guide_lines[i][0] != ' ' && !is_footer_shortcuts);
        HFONT font = is_header ? g_font_primary_bold : (is_footer_shortcuts ? g_font_mono_bold : g_font_primary_regular);
        HFONT old_f = (HFONT)SelectObject(screen_dc, font);
        RECT rc = { 0, 0, text_w, 0 };
        DrawTextA(screen_dc, g_canvas_guide_lines[i], -1, &rc, DT_CALCRECT | DT_WORDBREAK);
        SelectObject(screen_dc, old_f);
        int row_h = rc.bottom - rc.top;
        if (row_h < 18) row_h = 18;
        if (!is_footer_shortcuts) total_h += row_h + 3;
        else total_h += row_h;
    }
    ReleaseDC(NULL, screen_dc);
    total_h += 6;
    return total_h;
}

int get_canvas_guide_hud_max_scroll(int work_h) {
    if (g_hud_canvas_guide_collapsed) return 0;
    int content_h = get_canvas_guide_content_height();
    int max_avail_h = work_h - 16;
    if (max_avail_h < 26) max_avail_h = 26;
    int max_s = content_h - max_avail_h;
    return max_s > 0 ? max_s : 0;
}

bool get_canvas_guide_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h) {
    int max_s = get_canvas_guide_hud_max_scroll(work_h);
    if (max_s <= 0) return false;

    int content_h = get_canvas_guide_content_height();
    int avail_h = work_h - 16;
    if (avail_h < 60) avail_h = 60;

    int track_y = 28;
    int track_h = avail_h - 34;
    if (track_h <= 20) return false;

    float vis_ratio = (float)avail_h / (float)content_h;
    int thumb_h = (int)roundf((float)track_h * vis_ratio);
    if (thumb_h < 25) thumb_h = 25;
    if (thumb_h > track_h) thumb_h = track_h;

    float scroll_ratio = (max_s > 0) ? ((float)g_hud_canvas_guide_scroll_y / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_y = track_y + (int)roundf(scroll_ratio * (float)(track_h - thumb_h));

    if (out_track_y) *out_track_y = track_y;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return true;
}

void get_top_left_canvas_guide_hud_rect(int work_h, RECT *rc) {
    int tl_w = g_hud_canvas_guide_collapsed ? 135 : 370;
    int full_h = get_canvas_guide_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int tl_h = (full_h > max_avail_h) ? max_avail_h : full_h;

    rc->left = 12;
    rc->top = TOOLBAR_HEIGHT + 8;
    rc->right = rc->left + tl_w;
    rc->bottom = rc->top + tl_h;
}

void render_canvas_guide_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th) {
    (void)client_w;
    RECT rc_tl;
    get_top_left_canvas_guide_hud_rect(work_h, &rc_tl);
    int tl_w = rc_tl.right - rc_tl.left;
    int tl_h = rc_tl.bottom - rc_tl.top;

    HDC dc_tl = CreateCompatibleDC(mem_dc);
    HBITMAP bm_tl = CreateCompatibleBitmap(mem_dc, tl_w, tl_h);
    HBITMAP old_bm_tl = (HBITMAP)SelectObject(dc_tl, bm_tl);

    bool is_dark = (g_current_theme == THEME_DARK);
    COLORREF panel_bg = is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250);
    COLORREF panel_border = is_dark ? RGB(60, 70, 92) : RGB(190, 198, 212);

    HBRUSH br_bg_tl = CreateSolidBrush(panel_bg);
    RECT rc_tl_local = { 0, 0, tl_w, tl_h };
    FillRect(dc_tl, &rc_tl_local, br_bg_tl);
    DeleteObject(br_bg_tl);

    HPEN pen_bdr_tl = CreatePen(PS_SOLID, 1, panel_border);
    HPEN old_pen_tl = (HPEN)SelectObject(dc_tl, pen_bdr_tl);
    SelectObject(dc_tl, GetStockObject(NULL_BRUSH));
    Rectangle(dc_tl, 0, 0, tl_w, tl_h);
    SelectObject(dc_tl, old_pen_tl);
    DeleteObject(pen_bdr_tl);

    SetBkMode(dc_tl, TRANSPARENT);

    if (g_hud_canvas_guide_collapsed) {
        SelectObject(dc_tl, g_font_primary_bold);
        SetTextColor(dc_tl, th->text_accent_blue);
        RECT rc_t = { 8, 0, 75, tl_h };
        DrawTextA(dc_tl, "[?] Guide", -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_mono_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_hint = { 76, 0, tl_w - 22, tl_h };
        DrawTextA(dc_tl, "[F1]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_primary_regular);
        SetTextColor(dc_tl, th->text_primary);
        RECT rc_chev = { tl_w - 20, 0, tl_w - 4, tl_h };
        DrawTextA(dc_tl, "v", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        SelectObject(dc_tl, g_font_primary_bold);
        SetTextColor(dc_tl, th->text_primary);
        RECT rc_title = { 8, 4, 180, 22 };
        DrawTextA(dc_tl, "Guide: Canvas Mode", -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_mono_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_hint = { tl_w - 120, 4, tl_w - 24, 22 };
        DrawTextA(dc_tl, "[F1 to hide]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tl, g_font_primary_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_chev = { tl_w - 20, 4, tl_w - 4, 22 };
        DrawTextA(dc_tl, "^", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        /* Separator line below title */
        HPEN sep_pen = CreatePen(PS_SOLID, 1, is_dark ? RGB(45, 52, 68) : RGB(218, 224, 235));
        HPEN old_sep = (HPEN)SelectObject(dc_tl, sep_pen);
        MoveToEx(dc_tl, 8, 24, NULL);
        LineTo(dc_tl, tl_w - 8, 24);
        SelectObject(dc_tl, old_sep);
        DeleteObject(sep_pen);

        HRGN clip_tl = CreateRectRgn(1, 25, tl_w - 1, tl_h - 1);
        SelectClipRgn(dc_tl, clip_tl);

        int max_s = get_canvas_guide_hud_max_scroll(work_h);
        if (g_hud_canvas_guide_scroll_y > max_s) g_hud_canvas_guide_scroll_y = max_s;
        if (g_hud_canvas_guide_scroll_y < 0) g_hud_canvas_guide_scroll_y = 0;

        int cur_y = 28 - g_hud_canvas_guide_scroll_y;
        int text_w = tl_w - 20;
        int num_lines = (int)(sizeof(g_canvas_guide_lines) / sizeof(g_canvas_guide_lines[0]));

        for (int i = 0; i < num_lines; ++i) {
            bool is_footer_shortcuts = (i == num_lines - 1);
            bool is_header = (g_canvas_guide_lines[i][0] != ' ' && !is_footer_shortcuts);

            if (is_header) {
                SelectObject(dc_tl, g_font_primary_bold);
                SetTextColor(dc_tl, th->text_accent_blue);
            } else if (is_footer_shortcuts) {
                SelectObject(dc_tl, g_font_mono_bold);
                SetTextColor(dc_tl, is_dark ? RGB(255, 200, 60) : RGB(190, 110, 0));
            } else {
                SelectObject(dc_tl, g_font_primary_regular);
                SetTextColor(dc_tl, th->text_secondary);
            }

            RECT rc_calc = { 0, 0, text_w, 0 };
            DrawTextA(dc_tl, g_canvas_guide_lines[i], -1, &rc_calc, DT_CALCRECT | DT_WORDBREAK);
            int row_h = rc_calc.bottom - rc_calc.top;
            if (row_h < 18) row_h = 18;

            if (cur_y + row_h >= 24 && cur_y <= tl_h) {
                RECT rc_draw = { 10, cur_y, 10 + text_w, cur_y + row_h };
                DrawTextA(dc_tl, g_canvas_guide_lines[i], -1, &rc_draw, DT_WORDBREAK);
            }

            if (!is_footer_shortcuts) cur_y += row_h + 3;
            else cur_y += row_h;
        }

        SelectClipRgn(dc_tl, NULL);
        DeleteObject(clip_tl);

        int track_y, track_h, thumb_y, thumb_h;
        if (get_canvas_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
            int track_x = tl_w - 6;
            int track_w = 3;
            COLORREF track_col = is_dark ? RGB(38, 44, 58) : RGB(218, 224, 235);
            HBRUSH tr_br = CreateSolidBrush(track_col);
            RECT rc_sb = { track_x, track_y, track_x + track_w, track_y + track_h };
            FillRect(dc_tl, &rc_sb, tr_br);
            DeleteObject(tr_br);

            COLORREF thumb_col;
            if (is_dark) {
                thumb_col = (g_is_dragging_cv_guide_thumb || g_is_dragging_cv_guide_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
            } else {
                thumb_col = (g_is_dragging_cv_guide_thumb || g_is_dragging_cv_guide_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
            }
            HBRUSH th_br = CreateSolidBrush(thumb_col);
            HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
            HPEN old_th_p = (HPEN)SelectObject(dc_tl, th_pen);
            HBRUSH old_th_b = (HBRUSH)SelectObject(dc_tl, th_br);
            RoundRect(dc_tl, track_x, thumb_y, track_x + track_w, thumb_y + thumb_h, 3, 3);
            SelectObject(dc_tl, old_th_b);
            SelectObject(dc_tl, old_th_p);
            DeleteObject(th_br);
            DeleteObject(th_pen);
        }
    }

    BYTE alpha_tl = g_hud_canvas_guide_collapsed ? 128 : 255;
    blit_hud_surface(mem_dc, rc_tl.left, rc_tl.top, tl_w, tl_h, dc_tl, alpha_tl);
    SelectObject(dc_tl, old_bm_tl);
    DeleteObject(bm_tl);
    DeleteDC(dc_tl);
}

int get_canvas_tools_content_height(void) {
    if (g_hud_canvas_collapsed) return 26;
    return 490; /* Accommodates Affect Transform section */
}

int get_canvas_tools_hud_max_scroll(int work_h) {
    if (g_hud_canvas_collapsed) return 0;
    int content_h = get_canvas_tools_content_height();
    int max_avail_h = work_h - 16;
    if (max_avail_h < 26) max_avail_h = 26;
    int max_s = content_h - max_avail_h;
    return max_s > 0 ? max_s : 0;
}

bool get_canvas_tools_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h) {
    int max_s = get_canvas_tools_hud_max_scroll(work_h);
    if (max_s <= 0) return false;

    int content_h = get_canvas_tools_content_height();
    int avail_h = work_h - 16;
    if (avail_h < 60) avail_h = 60;

    int track_y = 28;
    int track_h = avail_h - 34;
    if (track_h <= 20) return false;

    float vis_ratio = (float)avail_h / (float)content_h;
    int thumb_h = (int)roundf((float)track_h * vis_ratio);
    if (thumb_h < 25) thumb_h = 25;
    if (thumb_h > track_h) thumb_h = track_h;

    float scroll_ratio = (max_s > 0) ? ((float)g_hud_canvas_tools_scroll_y / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_y = track_y + (int)roundf(scroll_ratio * (float)(track_h - thumb_h));

    if (out_track_y) *out_track_y = track_y;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return true;
}

void get_top_right_canvas_hud_rect(int client_w, int work_h, RECT *rc) {
    int tr_w = g_hud_canvas_collapsed ? 180 : 236;
    int full_h = get_canvas_tools_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int tr_h = (full_h > max_avail_h) ? max_avail_h : full_h;

    rc->left = client_w - tr_w - 12;
    rc->top = TOOLBAR_HEIGHT + 8;
    rc->right = rc->left + tr_w;
    rc->bottom = rc->top + tr_h;
}

void render_canvas_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th) {
    RECT rc_tr;
    get_top_right_canvas_hud_rect(client_w, work_h, &rc_tr);
    int tr_w = rc_tr.right - rc_tr.left;
    int tr_h = rc_tr.bottom - rc_tr.top;

    HDC dc_tr = CreateCompatibleDC(mem_dc);
    HBITMAP bm_tr = CreateCompatibleBitmap(mem_dc, tr_w, tr_h);
    HBITMAP old_bm_tr = (HBITMAP)SelectObject(dc_tr, bm_tr);

    bool is_dark = (g_current_theme == THEME_DARK);
    COLORREF panel_bg = is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250);
    COLORREF panel_border = is_dark ? RGB(60, 70, 92) : RGB(190, 198, 212);

    HBRUSH br_bg_tr = CreateSolidBrush(panel_bg);
    RECT rc_tr_local = { 0, 0, tr_w, tr_h };
    FillRect(dc_tr, &rc_tr_local, br_bg_tr);
    DeleteObject(br_bg_tr);

    HPEN pen_bdr_tr = CreatePen(PS_SOLID, 1, panel_border);
    HPEN old_pen_tr = (HPEN)SelectObject(dc_tr, pen_bdr_tr);
    SelectObject(dc_tr, GetStockObject(NULL_BRUSH));
    Rectangle(dc_tr, 0, 0, tr_w, tr_h);
    SelectObject(dc_tr, old_pen_tr);
    DeleteObject(pen_bdr_tr);

    SetBkMode(dc_tr, TRANSPARENT);

    if (g_hud_canvas_collapsed) {
        if (g_edit_cv_w) ShowWindow(g_edit_cv_w, SW_HIDE);
        if (g_edit_cv_h) ShowWindow(g_edit_cv_h, SW_HIDE);

        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_t = { 8, 0, tr_w - 52, tr_h };
        DrawTextA(dc_tr, "[+] Canvas Tools", -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_hint = { tr_w - 52, 0, tr_w - 22, tr_h };
        DrawTextA(dc_tr, "[F2]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_chev = { tr_w - 20, 0, tr_w - 4, tr_h };
        DrawTextA(dc_tr, "v", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_title = { 8, 4, 130, 20 };
        DrawTextA(dc_tr, "Canvas Tools", -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_hint = { tr_w - 110, 4, tr_w - 22, 20 };
        DrawTextA(dc_tr, "[F2 to hide]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_chev = { tr_w - 20, 4, tr_w - 4, 20 };
        DrawTextA(dc_tr, "^", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        HRGN clip_tr = CreateRectRgn(1, 23, tr_w - 1, tr_h - 1);
        SelectClipRgn(dc_tr, clip_tr);

        int max_s = get_canvas_tools_hud_max_scroll(work_h);
        if (g_hud_canvas_tools_scroll_y > max_s) g_hud_canvas_tools_scroll_y = max_s;
        if (g_hud_canvas_tools_scroll_y < 0) g_hud_canvas_tools_scroll_y = 0;

        int sy = -g_hud_canvas_tools_scroll_y;

        /* Row 1: Section "Canvas Size" */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_lbl_sz = { 8, 26 + sy, 228, 44 + sy };
        DrawTextA(dc_tr, "Canvas Size (W x H):", -1, &rc_lbl_sz, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        /* Size Input Boxes & Set Button */
        RECT rc_w_box = { 8, 46 + sy, 70, 70 + sy };
        RECT rc_x_lbl = { 72, 46 + sy, 84, 70 + sy };
        RECT rc_h_box = { 86, 46 + sy, 148, 70 + sy };
        RECT rc_btn_set = { 154, 46 + sy, 228, 70 + sy };

        HBRUSH br_input = CreateSolidBrush(is_dark ? RGB(36, 40, 48) : RGB(255, 255, 255));
        FillRect(dc_tr, &rc_w_box, br_input);
        FillRect(dc_tr, &rc_h_box, br_input);
        DeleteObject(br_input);
        HBRUSH br_border = CreateSolidBrush(is_dark ? RGB(65, 75, 90) : RGB(180, 188, 200));
        FrameRect(dc_tr, &rc_w_box, br_border);
        FrameRect(dc_tr, &rc_h_box, br_border);
        DeleteObject(br_border);

        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, th->text_secondary);
        DrawTextA(dc_tr, "x", -1, &rc_x_lbl, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        /* Set Button */
        COLORREF bg_set = is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236);
        HBRUSH br_set = CreateSolidBrush(bg_set);
        FillRect(dc_tr, &rc_btn_set, br_set);
        DeleteObject(br_set);
        FrameRect(dc_tr, &rc_btn_set, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        DrawTextA(dc_tr, "Set", -1, &rc_btn_set, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        /* Position Edit Controls for W and H */
        if (g_edit_cv_w && g_edit_cv_h) {
            int edit_w_x = rc_tr.left + 8;
            int edit_w_y = rc_tr.top + 46 + sy;
            int edit_w_w = 62;
            int edit_w_h = 24;

            int edit_h_x = rc_tr.left + 86;
            int edit_h_y = rc_tr.top + 46 + sy;
            int edit_h_w = 62;
            int edit_h_h = 24;

            if (edit_w_y >= rc_tr.top + 23 && edit_w_y + edit_w_h <= rc_tr.bottom) {
                SetWindowPos(g_edit_cv_w, HWND_TOP, edit_w_x, edit_w_y, edit_w_w, edit_w_h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
                SetWindowPos(g_edit_cv_h, HWND_TOP, edit_h_x, edit_h_y, edit_h_w, edit_h_h, SWP_SHOWWINDOW | SWP_NOACTIVATE);

                if (GetFocus() != g_edit_cv_w) {
                    char cur_w[16] = {0};
                    GetWindowTextA(g_edit_cv_w, cur_w, sizeof(cur_w));
                    char exp_w[16];
                    snprintf(exp_w, sizeof(exp_w), "%d", g_canvas_w);
                    if (strcmp(cur_w, exp_w) != 0) {
                        g_in_canvas_edit_sync = true;
                        SetWindowTextA(g_edit_cv_w, exp_w);
                        g_in_canvas_edit_sync = false;
                    }
                }
                if (GetFocus() != g_edit_cv_h) {
                    char cur_h[16] = {0};
                    GetWindowTextA(g_edit_cv_h, cur_h, sizeof(cur_h));
                    char exp_h[16];
                    snprintf(exp_h, sizeof(exp_h), "%d", g_canvas_h);
                    if (strcmp(cur_h, exp_h) != 0) {
                        g_in_canvas_edit_sync = true;
                        SetWindowTextA(g_edit_cv_h, exp_h);
                        g_in_canvas_edit_sync = false;
                    }
                }
                RedrawWindow(g_edit_cv_w, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
                RedrawWindow(g_edit_cv_h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
            } else {
                ShowWindow(g_edit_cv_w, SW_HIDE);
                ShowWindow(g_edit_cv_h, SW_HIDE);
            }
        }

        /* Row 2: Presets (4 Pills) */
        RECT rc_pr_640  = { 8, 76 + sy, 114, 98 + sy };
        RECT rc_pr_800  = { 120, 76 + sy, 228, 98 + sy };
        RECT rc_pr_1024 = { 8, 102 + sy, 114, 124 + sy };
        RECT rc_pr_1280 = { 120, 102 + sy, 228, 124 + sy };

        RECT preset_rcs[4] = { rc_pr_640, rc_pr_800, rc_pr_1024, rc_pr_1280 };
        const char *preset_labels[4] = { "640 x 480", "800 x 600", "1024 x 768", "1280 x 720" };
        bool preset_active[4] = {
            (g_canvas_w == 640 && g_canvas_h == 480),
            (g_canvas_w == 800 && g_canvas_h == 600),
            (g_canvas_w == 1024 && g_canvas_h == 768),
            (g_canvas_w == 1280 && g_canvas_h == 720)
        };

        for (int p = 0; p < 4; ++p) {
            COLORREF bg_p = preset_active[p] ? th->text_accent_blue : (is_dark ? RGB(34, 39, 50) : RGB(225, 230, 240));
            HBRUSH br_p = CreateSolidBrush(bg_p);
            FillRect(dc_tr, &preset_rcs[p], br_p);
            DeleteObject(br_p);
            FrameRect(dc_tr, &preset_rcs[p], (HBRUSH)GetStockObject(BLACK_BRUSH));
            SelectObject(dc_tr, preset_active[p] ? g_font_primary_bold : g_font_primary_regular);
            SetTextColor(dc_tr, preset_active[p] ? RGB(255, 255, 255) : th->text_secondary);
            DrawTextA(dc_tr, preset_labels[p], -1, &preset_rcs[p], DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }

        /* Row 3: Section "Background Image" */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_lbl_bg = { 8, 130 + sy, 228, 148 + sy };
        DrawTextA(dc_tr, "Background Image:", -1, &rc_lbl_bg, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        RECT rc_btn_bg_add = { 8, 150 + sy, 114, 172 + sy };
        RECT rc_btn_bg_rem = { 120, 150 + sy, 228, 172 + sy };

        COLORREF bg_col_bga = is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236);
        HBRUSH br_btn_bga = CreateSolidBrush(bg_col_bga);
        FillRect(dc_tr, &rc_btn_bg_add, br_btn_bga);
        DeleteObject(br_btn_bga);
        FrameRect(dc_tr, &rc_btn_bg_add, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetTextColor(dc_tr, th->text_accent_blue);
        if (g_has_bg_image) {
            DrawTextA(dc_tr, "* Background", -1, &rc_btn_bg_add, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        } else {
            DrawTextA(dc_tr, "+ Background", -1, &rc_btn_bg_add, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }

        COLORREF bg_col_bgr = is_dark ? RGB(36, 42, 56) : RGB(220, 226, 236);
        HBRUSH br_btn_bgr = CreateSolidBrush(bg_col_bgr);
        FillRect(dc_tr, &rc_btn_bg_rem, br_btn_bgr);
        DeleteObject(br_btn_bgr);
        FrameRect(dc_tr, &rc_btn_bg_rem, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetTextColor(dc_tr, g_has_bg_image ? (is_dark ? RGB(255, 110, 110) : RGB(210, 40, 40)) : th->text_secondary);
        DrawTextA(dc_tr, "- Background", -1, &rc_btn_bg_rem, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        /* Row 4: Section "Image Fit Mode" */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_lbl_fit = { 8, 178 + sy, 228, 196 + sy };
        DrawTextA(dc_tr, "Image Fit Behavior:", -1, &rc_lbl_fit, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        RECT rc_fit_orig = { 8, 198 + sy, 114, 220 + sy };
        RECT rc_fit_fit  = { 120, 198 + sy, 228, 220 + sy };
        RECT rc_fit_fill = { 8, 224 + sy, 114, 246 + sy };
        RECT rc_fit_str  = { 120, 224 + sy, 228, 246 + sy };

        RECT fit_rcs[4] = { rc_fit_orig, rc_fit_fit, rc_fit_fill, rc_fit_str };
        const char *fit_labels[4] = { "Original", "Fit (Letterbox)", "Fill (Cover)", "Stretch" };
        CanvasFitMode fit_modes[4] = { CANVAS_FIT_ORIGINAL, CANVAS_FIT_FIT, CANVAS_FIT_FILL, CANVAS_FIT_STRETCH };

        for (int f = 0; f < 4; ++f) {
            bool is_act = (g_canvas_fit_mode == fit_modes[f]);
            COLORREF bg_f = is_act ? th->text_accent_blue : (is_dark ? RGB(34, 39, 50) : RGB(225, 230, 240));
            HBRUSH br_f = CreateSolidBrush(bg_f);
            FillRect(dc_tr, &fit_rcs[f], br_f);
            DeleteObject(br_f);
            FrameRect(dc_tr, &fit_rcs[f], (HBRUSH)GetStockObject(BLACK_BRUSH));
            SelectObject(dc_tr, is_act ? g_font_primary_bold : g_font_primary_regular);
            SetTextColor(dc_tr, is_act ? RGB(255, 255, 255) : th->text_primary);
            DrawTextA(dc_tr, fit_labels[f], -1, &fit_rcs[f], DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }

        /* Row 5: Section "Image Alignment (3x3)" */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_lbl_ali = { 8, 252 + sy, 228, 270 + sy };
        DrawTextA(dc_tr, "Image Alignment (Anchor):", -1, &rc_lbl_ali, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        const char *align_labels[9] = { "TL", "TC", "TR", "ML", "Center", "MR", "BL", "BC", "BR" };
        CanvasAlignment align_vals[9] = {
            CANVAS_ALIGN_TOP_LEFT,    CANVAS_ALIGN_TOP_CENTER,    CANVAS_ALIGN_TOP_RIGHT,
            CANVAS_ALIGN_MID_LEFT,    CANVAS_ALIGN_CENTER,        CANVAS_ALIGN_MID_RIGHT,
            CANVAS_ALIGN_BOTTOM_LEFT, CANVAS_ALIGN_BOTTOM_CENTER, CANVAS_ALIGN_BOTTOM_RIGHT
        };

        int a_btn_w = 70;
        int a_btn_h = 24;
        int a_start_x = 10;
        int a_start_y = 274 + sy;

        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                int idx = row * 3 + col;
                int ax = a_start_x + col * (a_btn_w + 5);
                int ay = a_start_y + row * (a_btn_h + 4);
                RECT rc_a = { ax, ay, ax + a_btn_w, ay + a_btn_h };

                bool is_act = (g_canvas_alignment == align_vals[idx]);
                COLORREF bg_a = is_act ? th->text_accent_blue : (is_dark ? RGB(34, 39, 50) : RGB(225, 230, 240));
                HBRUSH br_a = CreateSolidBrush(bg_a);
                FillRect(dc_tr, &rc_a, br_a);
                DeleteObject(br_a);
                FrameRect(dc_tr, &rc_a, (HBRUSH)GetStockObject(BLACK_BRUSH));

                SelectObject(dc_tr, is_act ? g_font_primary_bold : g_font_primary_regular);
                SetTextColor(dc_tr, is_act ? RGB(255, 255, 255) : th->text_primary);
                DrawTextA(dc_tr, align_labels[idx], -1, &rc_a, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
        }

        /* Current Alignment Summary badge */
        const char *cur_align_name = "Top-Left (Default)";
        switch (g_canvas_alignment) {
            case CANVAS_ALIGN_TOP_LEFT:      cur_align_name = "Top-Left (Default)"; break;
            case CANVAS_ALIGN_TOP_CENTER:    cur_align_name = "Top-Center"; break;
            case CANVAS_ALIGN_TOP_RIGHT:     cur_align_name = "Top-Right"; break;
            case CANVAS_ALIGN_MID_LEFT:      cur_align_name = "Middle-Left"; break;
            case CANVAS_ALIGN_CENTER:        cur_align_name = "Center / Middle"; break;
            case CANVAS_ALIGN_MID_RIGHT:     cur_align_name = "Middle-Right"; break;
            case CANVAS_ALIGN_BOTTOM_LEFT:   cur_align_name = "Bottom-Left"; break;
            case CANVAS_ALIGN_BOTTOM_CENTER: cur_align_name = "Bottom-Center"; break;
            case CANVAS_ALIGN_BOTTOM_RIGHT:  cur_align_name = "Bottom-Right"; break;
        }

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_cur_ali = { 8, 360 + sy, 228, 378 + sy };
        char ali_txt[64];
        snprintf(ali_txt, sizeof(ali_txt), "Anchor: %s", cur_align_name);
        DrawTextA(dc_tr, ali_txt, -1, &rc_cur_ali, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        /* Row 6: Section "Affect Transform" */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_lbl_aff = { 8, 384 + sy, 228, 402 + sy };
        DrawTextA(dc_tr, "Affect Transform:", -1, &rc_lbl_aff, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        struct {
            const char *label;
            bool active;
            int y_top;
            int y_bottom;
        } aff_items[3] = {
            { "Include Element (Frog & Coin)", g_cv_affect_element, 406 + sy, 428 + sy },
            { "Include Curve (Paths & Attrs)", g_cv_affect_curve,   432 + sy, 454 + sy },
            { "Include Overlay (Cutout/Sprite)", g_cv_affect_overlay, 458 + sy, 480 + sy }
        };

        for (int i = 0; i < 3; ++i) {
            RECT rc_row = { 8, aff_items[i].y_top, 228, aff_items[i].y_bottom };
            bool is_act = aff_items[i].active;

            COLORREF bg_row = is_act ? (is_dark ? RGB(30, 42, 60) : RGB(225, 235, 250))
                                     : (is_dark ? RGB(32, 36, 46) : RGB(236, 240, 246));
            COLORREF border_row = is_act ? th->text_accent_blue : th->btn_border;

            HBRUSH br_row = CreateSolidBrush(bg_row);
            HPEN pen_row = CreatePen(PS_SOLID, 1, border_row);
            HPEN old_p_r = (HPEN)SelectObject(dc_tr, pen_row);
            HBRUSH old_b_r = (HBRUSH)SelectObject(dc_tr, br_row);
            RoundRect(dc_tr, rc_row.left, rc_row.top, rc_row.right, rc_row.bottom, 4, 4);
            SelectObject(dc_tr, old_b_r);
            SelectObject(dc_tr, old_p_r);
            DeleteObject(br_row);
            DeleteObject(pen_row);

            /* Checkbox box (14x14) */
            int cb_x = rc_row.left + 6;
            int cb_y = rc_row.top + 4;
            RECT rc_cb = { cb_x, cb_y, cb_x + 14, cb_y + 14 };

            COLORREF cb_bg = is_act ? th->text_accent_blue : (is_dark ? RGB(20, 24, 32) : RGB(255, 255, 255));
            HBRUSH br_cb = CreateSolidBrush(cb_bg);
            FillRect(dc_tr, &rc_cb, br_cb);
            DeleteObject(br_cb);
            FrameRect(dc_tr, &rc_cb, (HBRUSH)GetStockObject(BLACK_BRUSH));

            if (is_act) {
                SelectObject(dc_tr, g_font_mono_bold);
                SetTextColor(dc_tr, RGB(255, 255, 255));
                DrawTextA(dc_tr, "v", -1, &rc_cb, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }

            /* Label */
            SelectObject(dc_tr, is_act ? g_font_primary_bold : g_font_primary_regular);
            SetTextColor(dc_tr, is_act ? (is_dark ? RGB(220, 235, 255) : RGB(20, 50, 110)) : th->text_secondary);
            RECT rc_txt = { cb_x + 20, rc_row.top, rc_row.right - 4, rc_row.bottom };
            DrawTextA(dc_tr, aff_items[i].label, -1, &rc_txt, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        }

        SelectClipRgn(dc_tr, NULL);
        DeleteObject(clip_tr);

        int track_y, track_h, thumb_y, thumb_h;
        if (get_canvas_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
            int track_x = tr_w - 6;
            int track_w = 3;
            COLORREF track_col = is_dark ? RGB(38, 44, 58) : RGB(218, 224, 235);
            HBRUSH tr_br = CreateSolidBrush(track_col);
            RECT rc_sb = { track_x, track_y, track_x + track_w, track_y + track_h };
            FillRect(dc_tr, &rc_sb, tr_br);
            DeleteObject(tr_br);

            COLORREF thumb_col;
            if (is_dark) {
                thumb_col = (g_is_dragging_cv_tools_thumb || g_is_dragging_cv_tools_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
            } else {
                thumb_col = (g_is_dragging_cv_tools_thumb || g_is_dragging_cv_tools_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
            }
            HBRUSH th_br = CreateSolidBrush(thumb_col);
            HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
            HPEN old_th_p = (HPEN)SelectObject(dc_tr, th_pen);
            HBRUSH old_th_b = (HBRUSH)SelectObject(dc_tr, th_br);
            RoundRect(dc_tr, track_x, thumb_y, track_x + track_w, thumb_y + thumb_h, 3, 3);
            SelectObject(dc_tr, old_th_b);
            SelectObject(dc_tr, old_th_p);
            DeleteObject(th_br);
            DeleteObject(th_pen);
        }
    }

    BYTE alpha_tr = g_hud_canvas_collapsed ? 128 : 255;
    blit_hud_surface(mem_dc, rc_tr.left, rc_tr.top, tr_w, tr_h, dc_tr, alpha_tr);
    SelectObject(dc_tr, old_bm_tr);
    DeleteObject(bm_tr);
    DeleteDC(dc_tr);
}

/* ========================================================================= */
/* PREVIEW MODE FLOATING HUDS (Guide HUD & Canvas Info HUD)                  */
/* ========================================================================= */

static const char *g_preview_guide_lines[] = {
    "Level Preview Shortcut: Press Ctrl+L to Play or Stop",
    "",
    "1. Project Setup (New / Open Canvas)",
    " - Create a new canvas (Ctrl+N) or open existing project (Ctrl+O).",
    " - Select your background artwork; ZuPather auto-creates the level directory.",
    " - Set display title (dispname) and unique level ID in the Canvas Info HUD.",
    "",
    "2. Canvas Mode (Ctrl+M)",
    " - Configure canvas resolution (640x480 Classic, 800x600 Zuma Compleluxe, custom).",
    " - Position and scale artwork: Original, Fit, Fill, Stretch, or 9-point Anchor.",
    " - Drag corner and edge transformation handles for interactive canvas resizing.",
    "",
    "3. Curve Mode (Ctrl+U)",
    " - Design Zuma track curves using vector bezier paths (up to 10 paths).",
    "   * Draw Submode (Pen Tool):",
    "     - LMB: Place anchor nodes and drag tangent handles to curve the path.",
    "     - Alt + LMB on Curve: Insert new anchor node between existing points.",
    "     - Keys: C:Corner, S:Smooth, R:Reverse, X:Split, J:Join, Insert:Subdivide before node, N:New.",
    "     - Endpoints automatically spawn Zuma skull targets (merged if < 20px).",
    "   * Attribute Submode (Tab Toggle):",
    "     - Press Tab to switch between Draw and Attribute submodes.",
    "     - Adjust track priority depth with [+] and [-] keys.",
    "     - Toggle tunnel status along track segments with [U] key.",
    "",
    "4. Element Mode (Ctrl+E)",
    " - Position interactive level gameplay elements:",
    "   * Frog Shooter: Set frog placement (gx, gy) rotation center via [Edit Frog] (Ctrl+P).",
    "   * Treasure Coins: Place bonus gold coins via [+ Coin] (Ctrl+K) along curves.",
    "   * Interactive coordinates badge displays exact placement offsets during drag.",
    "",
    "5. Overlay Mode (Ctrl+Shift+O)",
    " - Add multi-layer visual depth elements above or below curves:",
    "   * Cutouts: Tunnel alpha masks that cut out rolling ball trains (White = Mask, Black = Visible).",
    "   * Sprites: Decorative overlay PNGs with transparency placed above or below balls based on priority depth.",
    "   * BackgroundAlpha (bgAlpha): Background alpha elements with velocity vectors (vx, vy) for screen-quake stage transition events.",
    "   * Priority Tuning (Ctrl+J): Interactively adjust Z-order layering and motion vectors.",
    "",
    "6. Preview Mode & Level Simulation (Ctrl+L)",
    " - Baseline view with all edit modes toggled OFF.",
    " - Press Ctrl+L or click [Play Level Preview] on Canvas Info HUD.",
    " - Simulates animated 11-color ball waves rolling toward end skulls at 1.5x speed!",
    "",
    "Shortcuts: Ctrl+M:Canvas  Ctrl+U:Curve  Ctrl+E:Element  Ctrl+Shift+O:Overlay  F1:Guide  F2:Info"
};

int get_preview_guide_content_height(void) {
    if (g_hud_preview_guide_collapsed) return 26;

    int num_lines = (int)(sizeof(g_preview_guide_lines) / sizeof(g_preview_guide_lines[0]));
    int text_w = 370 - 24;

    HDC screen_dc = GetDC(NULL);
    int total_h = 28; /* Top title bar (24px) + margin (4px) */
    for (int i = 0; i < num_lines; ++i) {
        bool is_shortcut_banner = (i == 0);
        bool is_footer_shortcuts = (i == num_lines - 1);
        bool is_header = (g_preview_guide_lines[i][0] >= '1' && g_preview_guide_lines[i][0] <= '9');
        bool is_sub_header = (!is_header && !is_footer_shortcuts && !is_shortcut_banner &&
                              (strstr(g_preview_guide_lines[i], "Draw Submode") != NULL ||
                               strstr(g_preview_guide_lines[i], "Attribute Submode") != NULL ||
                               strstr(g_preview_guide_lines[i], "Frog Shooter:") != NULL ||
                               strstr(g_preview_guide_lines[i], "Treasure Coins:") != NULL ||
                               strstr(g_preview_guide_lines[i], "Cutouts:") != NULL ||
                               strstr(g_preview_guide_lines[i], "Sprites:") != NULL ||
                               strstr(g_preview_guide_lines[i], "BackgroundAlpha") != NULL ||
                               strstr(g_preview_guide_lines[i], "Priority Tuning") != NULL));

        HFONT font = (is_header || is_sub_header) ? g_font_primary_bold :
                     ((is_shortcut_banner || is_footer_shortcuts) ? g_font_mono_bold : g_font_primary_regular);
        HFONT old_f = (HFONT)SelectObject(screen_dc, font);
        RECT rc = { 0, 0, text_w, 0 };
        DrawTextA(screen_dc, g_preview_guide_lines[i], -1, &rc, DT_CALCRECT | DT_WORDBREAK);
        SelectObject(screen_dc, old_f);
        int row_h = rc.bottom - rc.top;
        if (row_h < 18) row_h = 18;
        total_h += row_h + 3;
    }
    ReleaseDC(NULL, screen_dc);
    total_h += 12; /* bottom padding */
    return total_h;
}

int get_preview_guide_hud_max_scroll(int work_h) {
    if (g_hud_preview_guide_collapsed) return 0;
    int content_h = get_preview_guide_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int max_s = content_h - max_avail_h;
    return max_s > 0 ? max_s : 0;
}

bool get_preview_guide_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h) {
    int max_s = get_preview_guide_hud_max_scroll(work_h);
    if (max_s <= 0) return false;

    int content_h = get_preview_guide_content_height();
    int avail_h = work_h - 16;
    if (avail_h < 60) avail_h = 60;

    int track_y = 28;
    int track_h = avail_h - 34;
    if (track_h <= 20) return false;

    float vis_ratio = (float)avail_h / (float)content_h;
    int thumb_h = (int)roundf((float)track_h * vis_ratio);
    if (thumb_h < 25) thumb_h = 25;
    if (thumb_h > track_h) thumb_h = track_h;

    float scroll_ratio = (max_s > 0) ? ((float)g_hud_preview_guide_scroll_y / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_y = track_y + (int)roundf(scroll_ratio * (float)(track_h - thumb_h));

    if (out_track_y) *out_track_y = track_y;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return true;
}

void get_top_left_preview_guide_hud_rect(int work_h, RECT *rc) {
    int tl_w = g_hud_preview_guide_collapsed ? 135 : 370;
    int full_h = get_preview_guide_content_height();
    int max_avail_h = (work_h > 26) ? (work_h - 16) : 26;
    if (max_avail_h < 26) max_avail_h = 26;
    int tl_h = g_hud_preview_guide_collapsed ? 26 : ((full_h > max_avail_h) ? max_avail_h : full_h);

    rc->left = 12;
    rc->top = TOOLBAR_HEIGHT + 8;
    rc->right = rc->left + tl_w;
    rc->bottom = rc->top + tl_h;
}

void render_preview_guide_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th) {
    (void)client_w;
    RECT rc_tl;
    get_top_left_preview_guide_hud_rect(work_h, &rc_tl);
    int tl_w = rc_tl.right - rc_tl.left;
    int tl_h = rc_tl.bottom - rc_tl.top;

    HDC dc_tl = CreateCompatibleDC(mem_dc);
    HBITMAP bm_tl = CreateCompatibleBitmap(mem_dc, tl_w, tl_h);
    HBITMAP old_bm_tl = (HBITMAP)SelectObject(dc_tl, bm_tl);

    bool is_dark = (g_current_theme == THEME_DARK);
    COLORREF panel_bg = is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250);
    COLORREF panel_border = is_dark ? RGB(60, 70, 92) : RGB(190, 198, 212);

    HBRUSH br_bg_tl = CreateSolidBrush(panel_bg);
    RECT rc_tl_local = { 0, 0, tl_w, tl_h };
    FillRect(dc_tl, &rc_tl_local, br_bg_tl);
    DeleteObject(br_bg_tl);

    HPEN pen_bdr_tl = CreatePen(PS_SOLID, 1, panel_border);
    HPEN old_pen_tl = (HPEN)SelectObject(dc_tl, pen_bdr_tl);
    SelectObject(dc_tl, GetStockObject(NULL_BRUSH));
    Rectangle(dc_tl, 0, 0, tl_w, tl_h);
    SelectObject(dc_tl, old_pen_tl);
    DeleteObject(pen_bdr_tl);

    SetBkMode(dc_tl, TRANSPARENT);

    if (g_hud_preview_guide_collapsed) {
        SelectObject(dc_tl, g_font_primary_bold);
        SetTextColor(dc_tl, th->text_accent_blue);
        RECT rc_t = { 8, 0, 75, tl_h };
        DrawTextA(dc_tl, "[?] Guide", -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_mono_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_hint = { 76, 0, tl_w - 22, tl_h };
        DrawTextA(dc_tl, "[F1]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_primary_regular);
        SetTextColor(dc_tl, th->text_primary);
        RECT rc_chev = { tl_w - 20, 0, tl_w - 4, tl_h };
        DrawTextA(dc_tl, "v", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        SelectObject(dc_tl, g_font_primary_bold);
        SetTextColor(dc_tl, th->text_primary);
        RECT rc_title = { 8, 4, 180, 22 };
        DrawTextA(dc_tl, "Guide: ZuPather Workflow", -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tl, g_font_mono_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_hint = { tl_w - 120, 4, tl_w - 24, 22 };
        DrawTextA(dc_tl, "[F1 to hide]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tl, g_font_primary_regular);
        SetTextColor(dc_tl, th->text_secondary);
        RECT rc_chev = { tl_w - 20, 4, tl_w - 4, 22 };
        DrawTextA(dc_tl, "^", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        /* Separator line below title */
        HPEN sep_pen = CreatePen(PS_SOLID, 1, is_dark ? RGB(45, 52, 68) : RGB(218, 224, 235));
        HPEN old_sep = (HPEN)SelectObject(dc_tl, sep_pen);
        MoveToEx(dc_tl, 8, 24, NULL);
        LineTo(dc_tl, tl_w - 8, 24);
        SelectObject(dc_tl, old_sep);
        DeleteObject(sep_pen);

        HRGN clip_tl = CreateRectRgn(1, 25, tl_w - 1, tl_h - 1);
        SelectClipRgn(dc_tl, clip_tl);

        int max_s = get_preview_guide_hud_max_scroll(work_h);
        if (g_hud_preview_guide_scroll_y > max_s) g_hud_preview_guide_scroll_y = max_s;
        int cur_y = 28 - g_hud_preview_guide_scroll_y;
        int track_y, track_h, thumb_y, thumb_h;
        bool has_scroll = get_preview_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h);
        int text_w = has_scroll ? (tl_w - 24) : (tl_w - 20);
        int num_lines = (int)(sizeof(g_preview_guide_lines) / sizeof(g_preview_guide_lines[0]));

        for (int i = 0; i < num_lines; ++i) {
            bool is_shortcut_banner = (i == 0);
            bool is_footer_shortcuts = (i == num_lines - 1);
            bool is_header = (g_preview_guide_lines[i][0] >= '1' && g_preview_guide_lines[i][0] <= '9');
            bool is_sub_header = (!is_header && !is_footer_shortcuts && !is_shortcut_banner &&
                                  (strstr(g_preview_guide_lines[i], "Draw Submode") != NULL ||
                                   strstr(g_preview_guide_lines[i], "Attribute Submode") != NULL ||
                                   strstr(g_preview_guide_lines[i], "Frog Shooter:") != NULL ||
                                   strstr(g_preview_guide_lines[i], "Treasure Coins:") != NULL ||
                                   strstr(g_preview_guide_lines[i], "Cutouts:") != NULL ||
                                   strstr(g_preview_guide_lines[i], "Sprites:") != NULL ||
                                   strstr(g_preview_guide_lines[i], "BackgroundAlpha") != NULL ||
                                   strstr(g_preview_guide_lines[i], "Priority Tuning") != NULL));

            if (is_shortcut_banner) {
                SelectObject(dc_tl, g_font_mono_bold);
                SetTextColor(dc_tl, is_dark ? RGB(255, 215, 0) : RGB(190, 110, 0));
            } else if (is_header) {
                SelectObject(dc_tl, g_font_primary_bold);
                SetTextColor(dc_tl, th->text_accent_blue);
            } else if (is_sub_header) {
                SelectObject(dc_tl, g_font_primary_bold);
                SetTextColor(dc_tl, is_dark ? RGB(160, 205, 255) : RGB(25, 75, 160));
            } else if (is_footer_shortcuts) {
                SelectObject(dc_tl, g_font_mono_bold);
                SetTextColor(dc_tl, is_dark ? RGB(255, 200, 60) : RGB(190, 110, 0));
            } else {
                SelectObject(dc_tl, g_font_primary_regular);
                SetTextColor(dc_tl, th->text_secondary);
            }

            RECT rc_calc = { 0, 0, text_w, 0 };
            DrawTextA(dc_tl, g_preview_guide_lines[i], -1, &rc_calc, DT_CALCRECT | DT_WORDBREAK);
            int row_h = rc_calc.bottom - rc_calc.top;
            if (row_h < 18) row_h = 18;

            if (cur_y + row_h >= 24 && cur_y <= tl_h) {
                RECT rc_draw = { 10, cur_y, 10 + text_w, cur_y + row_h };
                DrawTextA(dc_tl, g_preview_guide_lines[i], -1, &rc_draw, DT_WORDBREAK | DT_LEFT);
            }

            if (!is_footer_shortcuts) cur_y += row_h + 3;
            else cur_y += row_h;
        }

        SelectClipRgn(dc_tl, NULL);
        DeleteObject(clip_tl);

        if (has_scroll) {
            int track_x = tl_w - 6;
            int track_w = 3;
            COLORREF track_col = is_dark ? RGB(38, 44, 58) : RGB(218, 224, 235);
            HBRUSH tr_br = CreateSolidBrush(track_col);
            RECT rc_sb = { track_x, track_y, track_x + track_w, track_y + track_h };
            FillRect(dc_tl, &rc_sb, tr_br);
            DeleteObject(tr_br);

            COLORREF thumb_col;
            if (is_dark) {
                thumb_col = (g_is_dragging_prev_guide_thumb || g_is_dragging_prev_guide_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
            } else {
                thumb_col = (g_is_dragging_prev_guide_thumb || g_is_dragging_prev_guide_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
            }
            HBRUSH th_br = CreateSolidBrush(thumb_col);
            HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
            HPEN old_th_p = (HPEN)SelectObject(dc_tl, th_pen);
            HBRUSH old_th_b = (HBRUSH)SelectObject(dc_tl, th_br);
            RoundRect(dc_tl, track_x, thumb_y, track_x + track_w, thumb_y + thumb_h, 3, 3);
            SelectObject(dc_tl, old_th_b);
            SelectObject(dc_tl, old_th_p);
            DeleteObject(th_br);
            DeleteObject(th_pen);
        }
    }

    BYTE alpha_tl = g_hud_preview_guide_collapsed ? 128 : 255;
    blit_hud_surface(mem_dc, rc_tl.left, rc_tl.top, tl_w, tl_h, dc_tl, alpha_tl);
    SelectObject(dc_tl, old_bm_tl);
    DeleteObject(bm_tl);
    DeleteDC(dc_tl);
}

/* Map Attribute HUD (Preview Mode) Geometry & Layout Calculation */
int get_preview_hud_title_box_y(void) {
    const char *loc = g_canvas_folder[0] ? g_canvas_folder : "Unsaved / In-Memory";
    int text_w = 250 - 16;
    int loc_h = 18;
    HDC hdc_screen = GetDC(NULL);
    if (hdc_screen) {
        HFONT old_f = (HFONT)SelectObject(hdc_screen, g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        RECT rc = { 0, 0, text_w, 0 };
        DrawTextA(hdc_screen, loc, -1, &rc, DT_CALCRECT | DT_WORDBREAK);
        loc_h = rc.bottom - rc.top;
        if (loc_h < 18) loc_h = 18;
        SelectObject(hdc_screen, old_f);
        ReleaseDC(NULL, hdc_screen);
    }
    return 156 + loc_h;
}

void get_map_attr_hud_layout(
    int *out_box_y,
    int *out_skull_y,
    int *out_space_y,
    int *out_sep3_y,
    int *out_sum_y,
    int *out_sep4_y,
    int *out_dev_y,
    int out_dev_items_y[8],
    int *out_total_h
) {
    int box_y = get_preview_hud_title_box_y();
    int skull_y = box_y + 30;
    int space_y = skull_y + 26;
    int sep3_y = space_y + 28;
    int sum_y = sep3_y + 6;
    int cur_info_y = sum_y + 22;
    int info_end_y = cur_info_y + 8 * 20;
    int sep4_y = info_end_y + 4;
    int dev_y = sep4_y + 6;
    int cur_dev = dev_y + 34;

    int dev_items[8];
    dev_items[0] = cur_dev;         /* drawcurve */
    dev_items[1] = cur_dev + 24;    /* drawtunnels */
    dev_items[2] = cur_dev + 48;    /* curvecolor */
    dev_items[3] = cur_dev + 74;    /* bgcolor */
    dev_items[4] = cur_dev + 100;   /* destroyall */
    dev_items[5] = cur_dev + 124;   /* huerot */
    dev_items[6] = cur_dev + 150;   /* treasurefreq */
    dev_items[7] = cur_dev + 176;   /* partime */

    int total_h = dev_items[7] + 32;

    if (out_box_y) *out_box_y = box_y;
    if (out_skull_y) *out_skull_y = skull_y;
    if (out_space_y) *out_space_y = space_y;
    if (out_sep3_y) *out_sep3_y = sep3_y;
    if (out_sum_y) *out_sum_y = sum_y;
    if (out_sep4_y) *out_sep4_y = sep4_y;
    if (out_dev_y) *out_dev_y = dev_y;
    if (out_dev_items_y) {
        for (int i = 0; i < 8; ++i) out_dev_items_y[i] = dev_items[i];
    }
    if (out_total_h) *out_total_h = total_h;
}

int get_preview_hud_content_height(void) {
    if (g_hud_preview_info_collapsed) return 26;
    int total_h = 0;
    get_map_attr_hud_layout(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &total_h);
    return total_h;
}

int get_preview_hud_max_scroll(int work_h) {
    if (g_hud_preview_info_collapsed) return 0;
    int content_h = get_preview_hud_content_height();
    int max_avail_h = work_h - 16;
    if (max_avail_h < 26) max_avail_h = 26;
    int max_s = content_h - max_avail_h;
    return max_s > 0 ? max_s : 0;
}

bool get_preview_hud_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h) {
    int max_s = get_preview_hud_max_scroll(work_h);
    if (max_s <= 0) return false;

    int content_h = get_preview_hud_content_height();
    int avail_h = work_h - 16;
    if (avail_h < 50) avail_h = 50;

    int track_y = 26;
    int track_h = avail_h - 32;
    if (track_h <= 15) return false;

    float vis_ratio = (float)avail_h / (float)content_h;
    int thumb_h = (int)roundf((float)track_h * vis_ratio);
    if (thumb_h < 20) thumb_h = 20;
    if (thumb_h > track_h) thumb_h = track_h;

    float scroll_ratio = (max_s > 0) ? ((float)g_hud_preview_info_scroll_y / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_y = track_y + (int)roundf(scroll_ratio * (float)(track_h - thumb_h));

    if (out_track_y) *out_track_y = track_y;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    return true;
}

void get_top_right_preview_hud_rect(int client_w, int work_h, RECT *rc) {
    int tr_w = g_hud_preview_info_collapsed ? 180 : 250;
    int full_h = get_preview_hud_content_height();
    int max_avail_h = work_h - 16;
    if (max_avail_h < 26) max_avail_h = 26;
    int tr_h = g_hud_preview_info_collapsed ? 26 : ((full_h > max_avail_h) ? max_avail_h : full_h);

    rc->right = client_w - 12;
    rc->left = rc->right - tr_w;
    rc->top = TOOLBAR_HEIGHT + 8;
    rc->bottom = rc->top + tr_h;
}

static void draw_hud_checkbox(HDC hdc, int x, int y, bool checked, bool dark_mode, COLORREF accent_col) {
    RECT rc = { x, y, x + 14, y + 14 };
    COLORREF bg_col = checked ? accent_col : (dark_mode ? RGB(32, 38, 48) : RGB(242, 245, 250));
    COLORREF bdr_col = checked ? accent_col : (dark_mode ? RGB(75, 85, 102) : RGB(175, 185, 198));

    HBRUSH br = CreateSolidBrush(bg_col);
    HPEN pen = CreatePen(PS_SOLID, 1, bdr_col);
    HBRUSH old_b = (HBRUSH)SelectObject(hdc, br);
    HPEN old_p = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 3, 3);
    SelectObject(hdc, old_b);
    SelectObject(hdc, old_p);
    DeleteObject(br);
    DeleteObject(pen);

    if (checked) {
        HPEN check_pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HPEN old_cp = (HPEN)SelectObject(hdc, check_pen);
        MoveToEx(hdc, x + 3, y + 7, NULL);
        LineTo(hdc, x + 6, y + 10);
        LineTo(hdc, x + 11, y + 4);
        SelectObject(hdc, old_cp);
        DeleteObject(check_pen);
    }
}

static void position_hud_edit_control(HWND hwnd_edit, bool is_enabled, int edit_x, int edit_y, int edit_w, int edit_h, int top_bound, int bot_bound) {
    if (!hwnd_edit) return;
    bool should_show = is_enabled && (edit_y >= top_bound) && (edit_y + edit_h <= bot_bound);
    bool is_visible = (IsWindowVisible(hwnd_edit) != FALSE);
    if (should_show) {
        RECT cur_rc;
        GetWindowRect(hwnd_edit, &cur_rc);
        POINT pt = { cur_rc.left, cur_rc.top };
        HWND parent = GetParent(hwnd_edit);
        if (parent) ScreenToClient(parent, &pt);
        int cur_w = cur_rc.right - cur_rc.left;
        int cur_h = cur_rc.bottom - cur_rc.top;
        if (!is_visible || pt.x != edit_x || pt.y != edit_y || cur_w != edit_w || cur_h != edit_h) {
            SetWindowPos(hwnd_edit, HWND_TOP, edit_x, edit_y, edit_w, edit_h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
            InvalidateRect(hwnd_edit, NULL, FALSE);
        }
    } else {
        if (is_visible) {
            ShowWindow(hwnd_edit, SW_HIDE);
        }
    }
}

void hide_all_map_attr_edits(void) {
    if (g_edit_preview_title) ShowWindow(g_edit_preview_title, SW_HIDE);
    if (g_edit_map_skullrot) ShowWindow(g_edit_map_skullrot, SW_HIDE);
    if (g_edit_dev_curvecolor) ShowWindow(g_edit_dev_curvecolor, SW_HIDE);
    if (g_edit_dev_bgcolor) ShowWindow(g_edit_dev_bgcolor, SW_HIDE);
    if (g_edit_dev_huerot) ShowWindow(g_edit_dev_huerot, SW_HIDE);
    if (g_edit_dev_treasurefreq) ShowWindow(g_edit_dev_treasurefreq, SW_HIDE);
    if (g_edit_dev_partime) ShowWindow(g_edit_dev_partime, SW_HIDE);
}

void sync_all_map_attr_edits_from_state(void) {
    if (g_edit_preview_title && GetFocus() != g_edit_preview_title) {
        SetWindowTextA(g_edit_preview_title, g_canvas_title);
    }
    if (g_edit_map_skullrot && GetFocus() != g_edit_map_skullrot) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", g_map_skullrot);
        SetWindowTextA(g_edit_map_skullrot, buf);
    }
    if (g_edit_dev_curvecolor && GetFocus() != g_edit_dev_curvecolor) {
        SetWindowTextA(g_edit_dev_curvecolor, g_dev_curvecolor);
    }
    if (g_edit_dev_bgcolor && GetFocus() != g_edit_dev_bgcolor) {
        SetWindowTextA(g_edit_dev_bgcolor, g_dev_bgcolor);
    }
    if (g_edit_dev_huerot && GetFocus() != g_edit_dev_huerot) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", g_dev_huerot);
        SetWindowTextA(g_edit_dev_huerot, buf);
    }
    if (g_edit_dev_treasurefreq && GetFocus() != g_edit_dev_treasurefreq) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", g_dev_treasurefreq);
        SetWindowTextA(g_edit_dev_treasurefreq, buf);
    }
    if (g_edit_dev_partime && GetFocus() != g_edit_dev_partime) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", g_dev_partime);
        SetWindowTextA(g_edit_dev_partime, buf);
    }
}

MapAttrHitTarget hit_test_map_attr_hud(int rel_x, int rel_y, int tr_w) {
    int content_y = rel_y + g_hud_preview_info_scroll_y;

    /* Play / Stop Button: { 8, 28, tr_w - 8, 54 } */
    if (content_y >= 28 && content_y <= 54 && rel_x >= 8 && rel_x <= tr_w - 8) {
        return MAP_ATTR_HIT_PLAY_BTN;
    }

    int box_y, skull_y, space_y, sep3_y, sum_y, sep4_y, dev_y, dev_items[8];
    get_map_attr_hud_layout(&box_y, &skull_y, &space_y, &sep3_y, &sum_y, &sep4_y, &dev_y, dev_items, NULL);

    /* Skull Rotation Checkbox: { 6..26, skull_y..skull_y + 20 } */
    if (content_y >= skull_y && content_y <= skull_y + 22 && rel_x >= 6 && rel_x <= 26) {
        return MAP_ATTR_HIT_CHECK_SKULLROT;
    }

    /* Space Checkbox or Pill: { 6..tr_w - 8, space_y..space_y + 24 } */
    if (content_y >= space_y && content_y <= space_y + 24 && rel_x >= 6 && rel_x <= tr_w - 8) {
        return MAP_ATTR_HIT_CHECK_SPACE;
    }

    /* Developer Attributes Checkboxes */
    /* drawcurve */
    if (content_y >= dev_items[0] && content_y <= dev_items[0] + 22 && rel_x >= 6 && rel_x <= tr_w - 8) {
        return MAP_ATTR_HIT_CHECK_DRAWCURVE;
    }
    /* drawtunnels */
    if (content_y >= dev_items[1] && content_y <= dev_items[1] + 22 && rel_x >= 6 && rel_x <= tr_w - 8) {
        return MAP_ATTR_HIT_CHECK_DRAWTUNNELS;
    }
    /* curvecolor */
    if (content_y >= dev_items[2] && content_y <= dev_items[2] + 22 && rel_x >= 6 && rel_x <= 26) {
        return MAP_ATTR_HIT_CHECK_CURVECOLOR;
    }
    /* bgcolor */
    if (content_y >= dev_items[3] && content_y <= dev_items[3] + 22 && rel_x >= 6 && rel_x <= 26) {
        return MAP_ATTR_HIT_CHECK_BGCOLOR;
    }
    /* destroyall */
    if (content_y >= dev_items[4] && content_y <= dev_items[4] + 22 && rel_x >= 6 && rel_x <= tr_w - 8) {
        return MAP_ATTR_HIT_CHECK_DESTROYALL;
    }
    /* huerot */
    if (content_y >= dev_items[5] && content_y <= dev_items[5] + 22 && rel_x >= 6 && rel_x <= 26) {
        return MAP_ATTR_HIT_CHECK_HUEROT;
    }
    /* treasurefreq */
    if (content_y >= dev_items[6] && content_y <= dev_items[6] + 22 && rel_x >= 6 && rel_x <= 26) {
        return MAP_ATTR_HIT_CHECK_TREASUREFREQ;
    }
    /* partime */
    if (content_y >= dev_items[7] && content_y <= dev_items[7] + 22 && rel_x >= 6 && rel_x <= 26) {
        return MAP_ATTR_HIT_CHECK_PARTIME;
    }

    return MAP_ATTR_HIT_NONE;
}

void render_preview_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th) {
    RECT rc_tr;
    get_top_right_preview_hud_rect(client_w, work_h, &rc_tr);
    int tr_w = rc_tr.right - rc_tr.left;
    int tr_h = rc_tr.bottom - rc_tr.top;

    HDC dc_tr = CreateCompatibleDC(mem_dc);
    HBITMAP bm_tr = CreateCompatibleBitmap(mem_dc, tr_w, tr_h);
    HBITMAP old_bm_tr = (HBITMAP)SelectObject(dc_tr, bm_tr);

    bool is_dark = (g_current_theme == THEME_DARK);
    COLORREF panel_bg = is_dark ? RGB(24, 28, 36) : RGB(246, 248, 252);
    COLORREF panel_border = is_dark ? RGB(50, 58, 72) : RGB(210, 218, 230);

    RECT rc_tr_local = { 0, 0, tr_w, tr_h };
    HBRUSH br_bg_tr = CreateSolidBrush(panel_bg);
    FillRect(dc_tr, &rc_tr_local, br_bg_tr);
    DeleteObject(br_bg_tr);

    HPEN pen_bdr_tr = CreatePen(PS_SOLID, 1, panel_border);
    HPEN old_pen_tr = (HPEN)SelectObject(dc_tr, pen_bdr_tr);
    SelectObject(dc_tr, GetStockObject(NULL_BRUSH));
    Rectangle(dc_tr, 0, 0, tr_w, tr_h);
    SelectObject(dc_tr, old_pen_tr);
    DeleteObject(pen_bdr_tr);

    SetBkMode(dc_tr, TRANSPARENT);

    if (g_hud_preview_info_collapsed) {
        hide_all_map_attr_edits();

        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_t = { 8, 0, tr_w - 52, tr_h };
        DrawTextA(dc_tr, "[+] Map Attribute", -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_hint = { tr_w - 52, 0, tr_w - 22, tr_h };
        DrawTextA(dc_tr, "[F2]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_chev = { tr_w - 20, 0, tr_w - 4, tr_h };
        DrawTextA(dc_tr, "v", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_title = { 8, 4, 130, 20 };
        DrawTextA(dc_tr, "Map Attribute", -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_hint = { tr_w - 110, 4, tr_w - 22, 20 };
        DrawTextA(dc_tr, "[F2 to hide]", -1, &rc_hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_chev = { tr_w - 20, 4, tr_w - 4, 20 };
        DrawTextA(dc_tr, "^", -1, &rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        HRGN clip_tr = CreateRectRgn(1, 23, tr_w - 1, tr_h - 1);
        SelectClipRgn(dc_tr, clip_tr);

        int max_s = get_preview_hud_max_scroll(work_h);
        if (g_hud_preview_info_scroll_y > max_s) g_hud_preview_info_scroll_y = max_s;
        if (g_hud_preview_info_scroll_y < 0) g_hud_preview_info_scroll_y = 0;

        int sy = -g_hud_preview_info_scroll_y;

        /* Row 1: Level Preview Toggle Button (At top above title canvas) */
        RECT rc_btn_play = { 8, 28 + sy, tr_w - 8, 54 + sy };
        COLORREF bg_play;
        COLORREF bdr_play;
        COLORREF txt_play;

        if (g_level_preview_active) {
            bg_play  = is_dark ? RGB(26, 62, 36) : RGB(215, 245, 225);
            bdr_play = is_dark ? RGB(40, 180, 80) : RGB(30, 150, 60);
            txt_play = is_dark ? RGB(100, 240, 140) : RGB(20, 120, 45);
        } else {
            bg_play  = is_dark ? RGB(32, 42, 60) : RGB(225, 235, 248);
            bdr_play = th->text_accent_blue;
            txt_play = th->text_accent_blue;
        }

        HBRUSH br_play = CreateSolidBrush(bg_play);
        FillRect(dc_tr, &rc_btn_play, br_play);
        DeleteObject(br_play);

        HPEN pen_play = CreatePen(PS_SOLID, 1, bdr_play);
        HPEN old_pp = (HPEN)SelectObject(dc_tr, pen_play);
        SelectObject(dc_tr, GetStockObject(NULL_BRUSH));
        RoundRect(dc_tr, rc_btn_play.left, rc_btn_play.top, rc_btn_play.right, rc_btn_play.bottom, 4, 4);
        SelectObject(dc_tr, old_pp);
        DeleteObject(pen_play);

        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, txt_play);
        const char *play_txt = g_level_preview_active ? "[ || ] Stop Level Preview" : "[ > ] Play Level Preview";
        DrawTextA(dc_tr, play_txt, -1, &rc_btn_play, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        /* Separator 1 */
        HPEN sep_pen = CreatePen(PS_SOLID, 1, is_dark ? RGB(45, 52, 68) : RGB(218, 224, 235));
        HPEN old_sep = (HPEN)SelectObject(dc_tr, sep_pen);
        MoveToEx(dc_tr, 8, 62 + sy, NULL);
        LineTo(dc_tr, tr_w - 8, 62 + sy);

        /* Section: Canvas Name (ID) */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_lbl_name = { 8, 68 + sy, tr_w - 8, 84 + sy };
        DrawTextA(dc_tr, "Canvas Name (ID):", -1, &rc_lbl_name, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, th->text_primary);
        RECT rc_val_name = { 8, 86 + sy, tr_w - 8, 104 + sy };
        const char *name_str = g_canvas_name[0] ? g_canvas_name : "Unnamed";
        DrawTextA(dc_tr, name_str, -1, &rc_val_name, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        /* Section: Canvas Location (Path) */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_lbl_loc = { 8, 108 + sy, tr_w - 8, 124 + sy };
        DrawTextA(dc_tr, "Canvas Location (Path):", -1, &rc_lbl_loc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        const char *loc_str = g_canvas_folder[0] ? g_canvas_folder : "Unsaved / In-Memory";
        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);

        RECT rc_calc = { 8, 0, tr_w - 8, 0 };
        DrawTextA(dc_tr, loc_str, -1, &rc_calc, DT_CALCRECT | DT_WORDBREAK);
        int loc_h = rc_calc.bottom - rc_calc.top;
        if (loc_h < 18) loc_h = 18;

        RECT rc_val_loc = { 8, 126 + sy, tr_w - 8, 126 + loc_h + sy };
        DrawTextA(dc_tr, loc_str, -1, &rc_val_loc, DT_WORDBREAK | DT_LEFT);

        int sep2_y = 126 + loc_h + 6;
        MoveToEx(dc_tr, 8, sep2_y + sy, NULL);
        LineTo(dc_tr, tr_w - 8, sep2_y + sy);

        int box_y, skull_y, space_y, sep3_y, sum_y, sep4_y, dev_y, dev_items[8];
        get_map_attr_hud_layout(&box_y, &skull_y, &space_y, &sep3_y, &sum_y, &sep4_y, &dev_y, dev_items, NULL);

        /* Row 2: Level Title Section (dispname) - Full Width Realtime Edit */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_lbl_title = { 8, sep2_y + 4 + sy, tr_w - 8, sep2_y + 22 + sy };
        DrawTextA(dc_tr, "Level Title (dispname):", -1, &rc_lbl_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        RECT rc_title_box = { 8, box_y + sy, tr_w - 8, box_y + 24 + sy };
        HBRUSH br_input = CreateSolidBrush(is_dark ? RGB(36, 40, 48) : RGB(255, 255, 255));
        FillRect(dc_tr, &rc_title_box, br_input);
        HBRUSH br_border = CreateSolidBrush(is_dark ? RGB(65, 75, 90) : RGB(180, 188, 200));
        FrameRect(dc_tr, &rc_title_box, br_border);

        /* Position Title Edit Control */
        position_hud_edit_control(g_edit_preview_title, true,
            rc_tr.left + 9, rc_tr.top + box_y + 1 + sy, tr_w - 18, 22,
            rc_tr.top + 23, rc_tr.bottom);

        /* Row 3: Skull Rotation (skullrot) */
        draw_hud_checkbox(dc_tr, 8, skull_y + 3 + sy, g_map_has_skullrot, is_dark, th->text_accent_blue);

        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, g_map_has_skullrot ? th->text_primary : th->text_secondary);
        RECT rc_lbl_skull = { 26, skull_y + 2 + sy, tr_w - 76, skull_y + 20 + sy };
        DrawTextA(dc_tr, "Skull Rotation:", -1, &rc_lbl_skull, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        RECT rc_skull_box = { tr_w - 70, skull_y + sy, tr_w - 8, skull_y + 22 + sy };
        FillRect(dc_tr, &rc_skull_box, br_input);
        FrameRect(dc_tr, &rc_skull_box, br_border);

        if (!g_map_has_skullrot) {
            SelectObject(dc_tr, g_font_mono_regular);
            SetTextColor(dc_tr, th->text_secondary);
            RECT rc_sk_txt = { tr_w - 68, skull_y + 1 + sy, tr_w - 10, skull_y + 21 + sy };
            char sk_buf[32];
            snprintf(sk_buf, sizeof(sk_buf), "%.1f", g_map_skullrot);
            DrawTextA(dc_tr, sk_buf, -1, &rc_sk_txt, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        position_hud_edit_control(g_edit_map_skullrot, g_map_has_skullrot,
            rc_tr.left + tr_w - 69, rc_tr.top + skull_y + 1 + sy, 60, 20,
            rc_tr.top + 23, rc_tr.bottom);

        /* Row 4: Space (Cosmic Arena) - Special Distinct Styling */
        COLORREF space_accent = is_dark ? RGB(180, 110, 255) : RGB(130, 45, 215);
        draw_hud_checkbox(dc_tr, 8, space_y + 3 + sy, g_map_is_space, is_dark, space_accent);

        RECT rc_space_badge = { 26, space_y + sy, tr_w - 8, space_y + 22 + sy };
        COLORREF bg_space = is_dark ? (g_map_is_space ? RGB(56, 32, 78) : RGB(34, 26, 46))
                                    : (g_map_is_space ? RGB(232, 218, 255) : RGB(245, 240, 252));
        COLORREF bdr_space = is_dark ? (g_map_is_space ? RGB(200, 140, 255) : RGB(100, 70, 140))
                                     : (g_map_is_space ? RGB(150, 80, 230) : RGB(180, 160, 210));
        HBRUSH br_sp = CreateSolidBrush(bg_space);
        HPEN pen_sp = CreatePen(PS_SOLID, 1, bdr_space);
        HBRUSH old_sp_b = (HBRUSH)SelectObject(dc_tr, br_sp);
        HPEN old_sp_p = (HPEN)SelectObject(dc_tr, pen_sp);
        RoundRect(dc_tr, rc_space_badge.left, rc_space_badge.top, rc_space_badge.right, rc_space_badge.bottom, 4, 4);
        SelectObject(dc_tr, old_sp_b);
        SelectObject(dc_tr, old_sp_p);
        DeleteObject(br_sp);
        DeleteObject(pen_sp);

        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, is_dark ? (g_map_is_space ? RGB(230, 195, 255) : RGB(190, 160, 230))
                                    : (g_map_is_space ? RGB(100, 25, 175) : RGB(120, 60, 180)));
        DrawTextA(dc_tr, " * Space Level (Cosmic Arena)", -1, &rc_space_badge, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        /* Separator 3 */
        MoveToEx(dc_tr, 8, sep3_y + sy, NULL);
        LineTo(dc_tr, tr_w - 8, sep3_y + sy);

        /* Row 5: Level Summary & Objects Info */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, th->text_accent_blue);
        RECT rc_lbl_sum = { 8, sum_y + sy, tr_w - 8, sum_y + 18 + sy };
        DrawTextA(dc_tr, "Level Objects & Elements:", -1, &rc_lbl_sum, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        struct {
            const char *label;
            char val[64];
        } info_items[8];

        if (g_has_frog) {
            snprintf(info_items[0].val, sizeof(info_items[0].val), "X: %d, Y: %d", g_frog_gx, g_frog_gy);
        } else {
            snprintf(info_items[0].val, sizeof(info_items[0].val), "Not Placed");
        }
        info_items[0].label = "Frog Center:";

        snprintf(info_items[1].val, sizeof(info_items[1].val), "%d", g_treasure_point_count);
        info_items[1].label = "Treasure Coins:";

        snprintf(info_items[2].val, sizeof(info_items[2].val), "%d / 10 paths", g_path_count);
        info_items[2].label = "Curve Tracks:";

        snprintf(info_items[3].val, sizeof(info_items[3].val), "%d tunnels", g_cutout_count);
        info_items[3].label = "Cutouts:";

        snprintf(info_items[4].val, sizeof(info_items[4].val), "%d objects", g_sprite_count);
        info_items[4].label = "Sprites:";

        snprintf(info_items[5].val, sizeof(info_items[5].val), "%d x %d px", g_canvas_w, g_canvas_h);
        info_items[5].label = "Canvas Size:";

        if (g_canvas_img.filename[0]) {
            snprintf(info_items[6].val, sizeof(info_items[6].val), "%s", g_canvas_img.filename);
        } else {
            snprintf(info_items[6].val, sizeof(info_items[6].val), "None");
        }
        info_items[6].label = "Background:";

        {
            char prog_id[64];
            int eff_paths = (g_path_count > 0) ? g_path_count : 1;
            get_level_progression_id(eff_paths, g_level_difficulty, prog_id, sizeof(prog_id));
            snprintf(info_items[7].val, sizeof(info_items[7].val), "%s", prog_id);
            info_items[7].label = "Progression:";
        }

        int cur_info_y = sum_y + 22 + sy;
        for (int i = 0; i < 8; ++i) {
            SelectObject(dc_tr, g_font_primary_regular);
            SetTextColor(dc_tr, th->text_secondary);
            RECT rc_k = { 10, cur_info_y, 115, cur_info_y + 18 };
            DrawTextA(dc_tr, info_items[i].label, -1, &rc_k, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

            SelectObject(dc_tr, g_font_mono_bold);
            SetTextColor(dc_tr, th->text_primary);
            RECT rc_v = { 118, cur_info_y, tr_w - 10, cur_info_y + 18 };
            DrawTextA(dc_tr, info_items[i].val, -1, &rc_v, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_PATH_ELLIPSIS);

            cur_info_y += 20;
        }

        /* Separator 4 */
        MoveToEx(dc_tr, 8, sep4_y + sy, NULL);
        LineTo(dc_tr, tr_w - 8, sep4_y + sy);

        /* Section: Developer Attributes (Testing Kit) */
        SelectObject(dc_tr, g_font_primary_bold);
        SetTextColor(dc_tr, is_dark ? RGB(235, 160, 45) : RGB(195, 105, 15));
        RECT rc_lbl_dev = { 8, dev_y + sy, tr_w - 8, dev_y + 16 + sy };
        DrawTextA(dc_tr, "Developer Attributes (Testing Kit):", -1, &rc_lbl_dev, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_sub_dev = { 8, dev_y + 16 + sy, tr_w - 8, dev_y + 30 + sy };
        DrawTextA(dc_tr, "Optional variables for testing in Zuma", -1, &rc_sub_dev, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        /* Dev Item 0: drawcurve */
        int y0 = dev_items[0] + sy;
        draw_hud_checkbox(dc_tr, 8, y0 + 3, g_dev_has_drawcurve, is_dark, th->text_accent_blue);
        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, g_dev_has_drawcurve ? th->text_primary : th->text_secondary);
        RECT rc_lbl_dc = { 26, y0 + 2, 110, y0 + 18 };
        DrawTextA(dc_tr, "drawcurve", -1, &rc_lbl_dc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_sub_dc = { 112, y0 + 2, tr_w - 8, y0 + 18 };
        DrawTextA(dc_tr, "(draw curve rail)", -1, &rc_sub_dc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        /* Dev Item 1: drawtunnels */
        int y1 = dev_items[1] + sy;
        draw_hud_checkbox(dc_tr, 8, y1 + 3, g_dev_has_drawtunnels, is_dark, th->text_accent_blue);
        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, g_dev_has_drawtunnels ? th->text_primary : th->text_secondary);
        RECT rc_lbl_dt = { 26, y1 + 2, 110, y1 + 18 };
        DrawTextA(dc_tr, "drawtunnels", -1, &rc_lbl_dt, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_sub_dt = { 112, y1 + 2, tr_w - 8, y1 + 18 };
        DrawTextA(dc_tr, "(draw tunnels)", -1, &rc_sub_dt, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        /* Dev Item 2: curvecolor */
        int y2 = dev_items[2] + sy;
        draw_hud_checkbox(dc_tr, 8, y2 + 3, g_dev_has_curvecolor, is_dark, th->text_accent_blue);
        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, g_dev_has_curvecolor ? th->text_primary : th->text_secondary);
        RECT rc_lbl_cc = { 26, y2 + 2, tr_w - 88, y2 + 18 };
        DrawTextA(dc_tr, "curvecolor:", -1, &rc_lbl_cc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        RECT rc_cc_box = { tr_w - 82, y2, tr_w - 8, y2 + 22 };
        FillRect(dc_tr, &rc_cc_box, br_input);
        FrameRect(dc_tr, &rc_cc_box, br_border);
        if (!g_dev_has_curvecolor) {
            SelectObject(dc_tr, g_font_mono_regular);
            SetTextColor(dc_tr, th->text_secondary);
            RECT rc_cc_txt = { tr_w - 80, y2 + 1, tr_w - 10, y2 + 21 };
            DrawTextA(dc_tr, g_dev_curvecolor, -1, &rc_cc_txt, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        position_hud_edit_control(g_edit_dev_curvecolor, g_dev_has_curvecolor,
            rc_tr.left + tr_w - 81, rc_tr.top + y2 + 1, 72, 20,
            rc_tr.top + 23, rc_tr.bottom);

        /* Dev Item 3: bgcolor */
        int y3 = dev_items[3] + sy;
        draw_hud_checkbox(dc_tr, 8, y3 + 3, g_dev_has_bgcolor, is_dark, th->text_accent_blue);
        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, g_dev_has_bgcolor ? th->text_primary : th->text_secondary);
        RECT rc_lbl_bg = { 26, y3 + 2, tr_w - 88, y3 + 18 };
        DrawTextA(dc_tr, "bgcolor:", -1, &rc_lbl_bg, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        RECT rc_bg_box = { tr_w - 82, y3, tr_w - 8, y3 + 22 };
        FillRect(dc_tr, &rc_bg_box, br_input);
        FrameRect(dc_tr, &rc_bg_box, br_border);
        if (!g_dev_has_bgcolor) {
            SelectObject(dc_tr, g_font_mono_regular);
            SetTextColor(dc_tr, th->text_secondary);
            RECT rc_bg_txt = { tr_w - 80, y3 + 1, tr_w - 10, y3 + 21 };
            DrawTextA(dc_tr, g_dev_bgcolor, -1, &rc_bg_txt, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        position_hud_edit_control(g_edit_dev_bgcolor, g_dev_has_bgcolor,
            rc_tr.left + tr_w - 81, rc_tr.top + y3 + 1, 72, 20,
            rc_tr.top + 23, rc_tr.bottom);

        /* Dev Item 4: destroyall */
        int y4 = dev_items[4] + sy;
        draw_hud_checkbox(dc_tr, 8, y4 + 3, g_dev_has_destroyall, is_dark, th->text_accent_blue);
        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, g_dev_has_destroyall ? th->text_primary : th->text_secondary);
        RECT rc_lbl_da = { 26, y4 + 2, 110, y4 + 18 };
        DrawTextA(dc_tr, "destroyall", -1, &rc_lbl_da, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SelectObject(dc_tr, g_font_primary_regular);
        SetTextColor(dc_tr, th->text_secondary);
        RECT rc_sub_da = { 112, y4 + 2, tr_w - 8, y4 + 18 };
        DrawTextA(dc_tr, "(explode on clear)", -1, &rc_sub_da, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

        /* Dev Item 5: huerot */
        int y5 = dev_items[5] + sy;
        draw_hud_checkbox(dc_tr, 8, y5 + 3, g_dev_has_huerot, is_dark, th->text_accent_blue);
        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, g_dev_has_huerot ? th->text_primary : th->text_secondary);
        RECT rc_lbl_hr = { 26, y5 + 2, tr_w - 76, y5 + 18 };
        DrawTextA(dc_tr, "huerot (deg):", -1, &rc_lbl_hr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        RECT rc_hr_box = { tr_w - 70, y5, tr_w - 8, y5 + 22 };
        FillRect(dc_tr, &rc_hr_box, br_input);
        FrameRect(dc_tr, &rc_hr_box, br_border);
        if (!g_dev_has_huerot) {
            SelectObject(dc_tr, g_font_mono_regular);
            SetTextColor(dc_tr, th->text_secondary);
            RECT rc_hr_txt = { tr_w - 68, y5 + 1, tr_w - 10, y5 + 21 };
            char hr_buf[32];
            snprintf(hr_buf, sizeof(hr_buf), "%.1f", g_dev_huerot);
            DrawTextA(dc_tr, hr_buf, -1, &rc_hr_txt, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        position_hud_edit_control(g_edit_dev_huerot, g_dev_has_huerot,
            rc_tr.left + tr_w - 69, rc_tr.top + y5 + 1, 60, 20,
            rc_tr.top + 23, rc_tr.bottom);

        /* Dev Item 6: treasurefreq */
        int y6 = dev_items[6] + sy;
        draw_hud_checkbox(dc_tr, 8, y6 + 3, g_dev_has_treasurefreq, is_dark, th->text_accent_blue);
        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, g_dev_has_treasurefreq ? th->text_primary : th->text_secondary);
        RECT rc_lbl_tf = { 26, y6 + 2, tr_w - 76, y6 + 18 };
        DrawTextA(dc_tr, "treasurefreq:", -1, &rc_lbl_tf, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        RECT rc_tf_box = { tr_w - 70, y6, tr_w - 8, y6 + 22 };
        FillRect(dc_tr, &rc_tf_box, br_input);
        FrameRect(dc_tr, &rc_tf_box, br_border);
        if (!g_dev_has_treasurefreq) {
            SelectObject(dc_tr, g_font_mono_regular);
            SetTextColor(dc_tr, th->text_secondary);
            RECT rc_tf_txt = { tr_w - 68, y6 + 1, tr_w - 10, y6 + 21 };
            char tf_buf[32];
            snprintf(tf_buf, sizeof(tf_buf), "%d", g_dev_treasurefreq);
            DrawTextA(dc_tr, tf_buf, -1, &rc_tf_txt, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        position_hud_edit_control(g_edit_dev_treasurefreq, g_dev_has_treasurefreq,
            rc_tr.left + tr_w - 69, rc_tr.top + y6 + 1, 60, 20,
            rc_tr.top + 23, rc_tr.bottom);

        /* Dev Item 7: partime */
        int y7 = dev_items[7] + sy;
        draw_hud_checkbox(dc_tr, 8, y7 + 3, g_dev_has_partime, is_dark, th->text_accent_blue);
        SelectObject(dc_tr, g_font_mono_bold);
        SetTextColor(dc_tr, g_dev_has_partime ? th->text_primary : th->text_secondary);
        RECT rc_lbl_pt = { 26, y7 + 2, tr_w - 76, y7 + 18 };
        DrawTextA(dc_tr, "partime (sec):", -1, &rc_lbl_pt, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        RECT rc_pt_box = { tr_w - 70, y7, tr_w - 8, y7 + 22 };
        FillRect(dc_tr, &rc_pt_box, br_input);
        FrameRect(dc_tr, &rc_pt_box, br_border);
        if (!g_dev_has_partime) {
            SelectObject(dc_tr, g_font_mono_regular);
            SetTextColor(dc_tr, th->text_secondary);
            RECT rc_pt_txt = { tr_w - 68, y7 + 1, tr_w - 10, y7 + 21 };
            char pt_buf[32];
            snprintf(pt_buf, sizeof(pt_buf), "%d", g_dev_partime);
            DrawTextA(dc_tr, pt_buf, -1, &rc_pt_txt, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        position_hud_edit_control(g_edit_dev_partime, g_dev_has_partime,
            rc_tr.left + tr_w - 69, rc_tr.top + y7 + 1, 60, 20,
            rc_tr.top + 23, rc_tr.bottom);

        DeleteObject(br_input);
        DeleteObject(br_border);
        SelectObject(dc_tr, old_sep);
        DeleteObject(sep_pen);

        SelectClipRgn(dc_tr, NULL);
        DeleteObject(clip_tr);

        int track_y, track_h, thumb_y, thumb_h;
        if (get_preview_hud_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
            int track_x = tr_w - 6;
            int track_w = 3;
            COLORREF track_col = is_dark ? RGB(38, 44, 58) : RGB(218, 224, 235);
            HBRUSH tr_br = CreateSolidBrush(track_col);
            RECT rc_sb = { track_x, track_y, track_x + track_w, track_y + track_h };
            FillRect(dc_tr, &rc_sb, tr_br);
            DeleteObject(tr_br);

            COLORREF thumb_col;
            if (is_dark) {
                thumb_col = (g_is_dragging_prev_info_thumb || g_is_dragging_prev_info_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
            } else {
                thumb_col = (g_is_dragging_prev_info_thumb || g_is_dragging_prev_info_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
            }
            HBRUSH th_br = CreateSolidBrush(thumb_col);
            HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
            HPEN old_th_p = (HPEN)SelectObject(dc_tr, th_pen);
            HBRUSH old_th_b = (HBRUSH)SelectObject(dc_tr, th_br);
            RoundRect(dc_tr, track_x, thumb_y, track_x + track_w, thumb_y + thumb_h, 3, 3);
            SelectObject(dc_tr, old_th_b);
            SelectObject(dc_tr, old_th_p);
            DeleteObject(th_br);
            DeleteObject(th_pen);
        }
    }

    BYTE alpha_tr = g_hud_preview_info_collapsed ? 128 : 255;
    blit_hud_surface(mem_dc, rc_tr.left, rc_tr.top, tr_w, tr_h, dc_tr, alpha_tr);
    SelectObject(dc_tr, old_bm_tr);
    DeleteObject(bm_tr);
    DeleteDC(dc_tr);
}

void get_map_attr_item_info(MapAttrHoverItem item, const char **out_title, const char **out_tag, const char **out_desc, const char **out_status) {
    switch (item) {
        case MAP_ATTR_HOVER_TITLE:
            if (out_title) *out_title = "Level Title (dispname)";
            if (out_tag)   *out_tag   = "[Visual Display / UI]";
            if (out_desc)  *out_desc  = "Display name shown on the stage screen, level banner, and pause menu in Zuma Deluxe. Stored as dispname in levels.xml.";
            if (out_status)*out_status= "Level Title (dispname): Display name of the level in Zuma Deluxe UI.";
            break;
        case MAP_ATTR_HOVER_SKULLROT:
            if (out_title) *out_title = "Skull Rotation (skullrot)";
            if (out_tag)   *out_tag   = "[Map Geometry / Visual]";
            if (out_desc)  *out_desc  = "Explicit rotation angle in degrees (e.g. 0.0) for the danger skull pit at the end of the track. When active, it overrides and bypasses the curve endpoint tangent.";
            if (out_status)*out_status= "Skull Rotation (skullrot): Explicit rotation angle in degrees (overrides curve endpoint tangent).";
            break;
        case MAP_ATTR_HOVER_SPACE:
            if (out_title) *out_title = "Space Arena (space)";
            if (out_tag)   *out_tag   = "[Cosmic Mode - Warning]";
            if (out_desc)  *out_desc  = "Converts level to Zuma's procedural space arena. Renders 1,000 real-time stars and scrolling nebula. WARNING: Static background, bgcolor, and static cutout layers will be bypassed!";
            if (out_status)*out_status= "Space Arena (space): Procedural 1,000 stars & scrolling nebula arena (bypasses static background).";
            break;
        case MAP_ATTR_HOVER_FROG:
            if (out_title) *out_title = "Frog Shooter Center (gx, gy)";
            if (out_tag)   *out_tag   = "[Player Origin]";
            if (out_desc)  *out_desc  = "Coordinates of the rotating frog shooter placed on the canvas. Determines where the player aims and shoots balls.";
            if (out_status)*out_status= "Frog Center: Rotating frog shooter origin coordinates (gx, gy).";
            break;
        case MAP_ATTR_HOVER_COIN:
            if (out_title) *out_title = "Treasure Coins (<TreasurePoint>)";
            if (out_tag)   *out_tag   = "[Bonus Collectibles]";
            if (out_desc)  *out_desc  = "Spawn locations for bonus gold coins in Zuma Deluxe. Hit coins during gameplay for 500 bonus points and quick meter charge.";
            if (out_status)*out_status= "Treasure Coins: Bonus coin spawn locations for score and meter charge.";
            break;
        case MAP_ATTR_HOVER_TRACKS:
            if (out_title) *out_title = "Curve Tracks (curve, curve1..9)";
            if (out_tag)   *out_tag   = "[Ball Path Rails - 10 Tracks]";
            if (out_desc)  *out_desc  = "Defines the Bezier path rails traveled by ball chains. ZuPather supports 1 to 10 tracks per level (curve, curve1..curve9).";
            if (out_status)*out_status= "Curve Tracks: Bezier path rails traveled by ball chains (1 to 10 tracks, curve..curve9).";
            break;
        case MAP_ATTR_HOVER_CUTOUTS:
            if (out_title) *out_title = "Tunnels (<Cutout>)";
            if (out_tag)   *out_tag   = "[Visual Obstruction]";
            if (out_desc)  *out_desc  = "Tunnel overlays that balls roll beneath. Drawn with priority layering so balls pass invisibly underneath them.";
            if (out_status)*out_status= "Cutouts: Tunnel overlays where balls roll underneath.";
            break;
        case MAP_ATTR_HOVER_SPRITES:
            if (out_title) *out_title = "Sprites (<Sprite>)";
            if (out_tag)   *out_tag   = "[Decorative Graphic]";
            if (out_desc)  *out_desc  = "Static decorative backdrop objects placed above or below ball tracks using priority layering.";
            if (out_status)*out_status= "Sprites: Decorative objects placed in the level scene.";
            break;
        case MAP_ATTR_HOVER_CANVAS_SIZE:
            if (out_title) *out_title = "Canvas Dimensions";
            if (out_tag)   *out_tag   = "[Resolution]";
            if (out_desc)  *out_desc  = "Target resolution of the level map (standard 640x480 for Zuma Deluxe, or upscaled HD resolution).";
            if (out_status)*out_status= "Canvas Dimensions: Native resolution of this level layout.";
            break;
        case MAP_ATTR_HOVER_BACKGROUND:
            if (out_title) *out_title = "Background Graphic (image)";
            if (out_tag)   *out_tag   = "[Level Backdrop]";
            if (out_desc)  *out_desc  = "Primary backdrop image loaded from levels/<id>/<image> by Zuma Deluxe.";
            if (out_status)*out_status= "Background Graphic: Primary backdrop image for this level.";
            break;
        case MAP_ATTR_HOVER_PROGRESSION:
            if (out_title) *out_title = "Level Progression (<LevelProgression>)";
            if (out_tag)   *out_tag   = "[Speed & Difficulty Tuning]";
            if (out_desc)  *out_desc  = level_difficulty_get_description(g_level_difficulty);
            if (out_status)*out_status= "Level Progression: Speed, ball count, repeat, and difficulty progression curve.";
            break;
        case MAP_ATTR_HOVER_DRAWCURVE:
            if (out_title) *out_title = "Draw Curve (drawcurve)";
            if (out_tag)   *out_tag   = "[Developer Testing Kit]";
            if (out_desc)  *out_desc  = "When true, Zuma Deluxe renders debug lines along the curve track rails during gameplay.";
            if (out_status)*out_status= "drawcurve: Developer flag to draw debug rail lines along the track.";
            break;
        case MAP_ATTR_HOVER_DRAWTUNNELS:
            if (out_title) *out_title = "Draw Tunnels (drawtunnels)";
            if (out_tag)   *out_tag   = "[Developer Testing Kit]";
            if (out_desc)  *out_desc  = "When true, Zuma Deluxe renders debug wireframe outline boxes over all tunnel cutout areas.";
            if (out_status)*out_status= "drawtunnels: Developer flag to draw wireframe boxes over tunnel cutouts.";
            break;
        case MAP_ATTR_HOVER_CURVECOLOR:
            if (out_title) *out_title = "Curve Color (curvecolor)";
            if (out_tag)   *out_tag   = "[Developer Testing Kit]";
            if (out_desc)  *out_desc  = "Hex color code (e.g. 0xFFFFFF) used by Zuma Deluxe to tint debug curve rail lines when drawcurve is active.";
            if (out_status)*out_status= "curvecolor: Hex color (e.g. 0xFFFFFF) for debug curve rail lines.";
            break;
        case MAP_ATTR_HOVER_BGCOLOR:
            if (out_title) *out_title = "Background Color (bgcolor)";
            if (out_tag)   *out_tag   = "[Developer Testing Kit]";
            if (out_desc)  *out_desc  = "Fallback solid hex color (e.g. 0x000000) filled behind the level graphics in Zuma Deluxe.";
            if (out_status)*out_status= "bgcolor: Fallback background solid fill color (hex code).";
            break;
        case MAP_ATTR_HOVER_DESTROYALL:
            if (out_title) *out_title = "Destroy All Balls (destroyall)";
            if (out_tag)   *out_tag   = "[Developer Testing Kit]";
            if (out_desc)  *out_desc  = "When true, all remaining balls instantly explode in a celebration blast upon level clear instead of rolling out.";
            if (out_status)*out_status= "destroyall: Explodes all balls instantly upon level clear.";
            break;
        case MAP_ATTR_HOVER_HUEROT:
            if (out_title) *out_title = "Hue Rotation (huerot)";
            if (out_tag)   *out_tag   = "[Developer Testing Kit]";
            if (out_desc)  *out_desc  = "Rotates the color hue of the background graphic continuously in degrees during gameplay.";
            if (out_status)*out_status= "huerot: Continuous background hue rotation in degrees.";
            break;
        case MAP_ATTR_HOVER_TREASUREFREQ:
            if (out_title) *out_title = "Treasure Frequency (treasurefreq)";
            if (out_tag)   *out_tag   = "[Gameplay Tuning]";
            if (out_desc)  *out_desc  = "Interval in game frames between coin / treasure spawns on the level's designated treasure points.";
            if (out_status)*out_status= "treasurefreq: Interval in frames between treasure coin spawns.";
            break;
        case MAP_ATTR_HOVER_PARTIME:
            if (out_title) *out_title = "Target Par Time (partime)";
            if (out_tag)   *out_tag   = "[Target Objective / Ace Time]";
            if (out_desc)  *out_desc  = "Target completion time in seconds (e.g. 120 = 2 min). Beating the level under this time awards an 'Ace Time' bonus and achievements in Zuma Deluxe.";
            if (out_status)*out_status= "partime: Target par time in seconds; beat this time for Ace Time bonus.";
            break;
        default:
            if (out_title) *out_title = NULL;
            if (out_tag)   *out_tag   = NULL;
            if (out_desc)  *out_desc  = NULL;
            if (out_status)*out_status= NULL;
            break;
    }
}

const char *get_map_attr_hover_status(MapAttrHoverItem item) {
    const char *status = NULL;
    get_map_attr_item_info(item, NULL, NULL, NULL, &status);
    return status;
}

MapAttrHoverItem hit_test_map_attr_hover(int rel_x, int rel_y, int tr_w, int *out_anchor_y) {
    if (out_anchor_y) *out_anchor_y = rel_y;
    int content_y = rel_y + g_hud_preview_info_scroll_y;
    int sy = -g_hud_preview_info_scroll_y;

    if (rel_x < 6 || rel_x > tr_w - 6) return MAP_ATTR_HOVER_NONE;

    int box_y, skull_y, space_y, sep3_y, sum_y, sep4_y, dev_y, dev_items[8];
    get_map_attr_hud_layout(&box_y, &skull_y, &space_y, &sep3_y, &sum_y, &sep4_y, &dev_y, dev_items, NULL);

    /* Level Title: from (box_y - 20) to (box_y + 24) */
    if (content_y >= box_y - 20 && content_y <= box_y + 24) {
        if (out_anchor_y) *out_anchor_y = box_y + 12 + sy;
        return MAP_ATTR_HOVER_TITLE;
    }

    /* Skull Rotation: skull_y to skull_y + 22 */
    if (content_y >= skull_y && content_y <= skull_y + 22) {
        if (out_anchor_y) *out_anchor_y = skull_y + 11 + sy;
        return MAP_ATTR_HOVER_SKULLROT;
    }

    /* Space: space_y to space_y + 24 */
    if (content_y >= space_y && content_y <= space_y + 24) {
        if (out_anchor_y) *out_anchor_y = space_y + 12 + sy;
        return MAP_ATTR_HOVER_SPACE;
    }

    /* Summary items: sum_y + 22 onwards, 8 items of 20px each */
    int cur_info_y = sum_y + 22;
    for (int i = 0; i < 8; ++i) {
        if (content_y >= cur_info_y && content_y <= cur_info_y + 18) {
            if (out_anchor_y) *out_anchor_y = cur_info_y + 9 + sy;
            switch (i) {
                case 0: return MAP_ATTR_HOVER_FROG;
                case 1: return MAP_ATTR_HOVER_COIN;
                case 2: return MAP_ATTR_HOVER_TRACKS;
                case 3: return MAP_ATTR_HOVER_CUTOUTS;
                case 4: return MAP_ATTR_HOVER_SPRITES;
                case 5: return MAP_ATTR_HOVER_CANVAS_SIZE;
                case 6: return MAP_ATTR_HOVER_BACKGROUND;
                case 7: return MAP_ATTR_HOVER_PROGRESSION;
            }
        }
        cur_info_y += 20;
    }

    /* Dev items 0..7 */
    if (content_y >= dev_items[0] && content_y <= dev_items[0] + 20) {
        if (out_anchor_y) *out_anchor_y = dev_items[0] + 10 + sy;
        return MAP_ATTR_HOVER_DRAWCURVE;
    }
    if (content_y >= dev_items[1] && content_y <= dev_items[1] + 20) {
        if (out_anchor_y) *out_anchor_y = dev_items[1] + 10 + sy;
        return MAP_ATTR_HOVER_DRAWTUNNELS;
    }
    if (content_y >= dev_items[2] && content_y <= dev_items[2] + 22) {
        if (out_anchor_y) *out_anchor_y = dev_items[2] + 11 + sy;
        return MAP_ATTR_HOVER_CURVECOLOR;
    }
    if (content_y >= dev_items[3] && content_y <= dev_items[3] + 22) {
        if (out_anchor_y) *out_anchor_y = dev_items[3] + 11 + sy;
        return MAP_ATTR_HOVER_BGCOLOR;
    }
    if (content_y >= dev_items[4] && content_y <= dev_items[4] + 20) {
        if (out_anchor_y) *out_anchor_y = dev_items[4] + 10 + sy;
        return MAP_ATTR_HOVER_DESTROYALL;
    }
    if (content_y >= dev_items[5] && content_y <= dev_items[5] + 22) {
        if (out_anchor_y) *out_anchor_y = dev_items[5] + 11 + sy;
        return MAP_ATTR_HOVER_HUEROT;
    }
    if (content_y >= dev_items[6] && content_y <= dev_items[6] + 22) {
        if (out_anchor_y) *out_anchor_y = dev_items[6] + 11 + sy;
        return MAP_ATTR_HOVER_TREASUREFREQ;
    }
    if (content_y >= dev_items[7] && content_y <= dev_items[7] + 22) {
        if (out_anchor_y) *out_anchor_y = dev_items[7] + 11 + sy;
        return MAP_ATTR_HOVER_PARTIME;
    }

    return MAP_ATTR_HOVER_NONE;
}

void render_map_attr_tooltip(HDC mem_dc, int client_w, int work_h, const ThemeColors *th, MapAttrHoverItem hover_item, int anchor_y) {
    if (hover_item == MAP_ATTR_HOVER_NONE) return;
    const char *title = NULL, *tag = NULL, *desc = NULL, *status = NULL;
    get_map_attr_item_info(hover_item, &title, &tag, &desc, &status);
    if (!title || !desc) return;

    RECT rc_tr;
    get_top_right_preview_hud_rect(client_w, work_h, &rc_tr);

    int card_w = 260;
    int card_pad = 10;
    int text_w = card_w - (card_pad * 2);

    /* Measure description height */
    HFONT old_f = (HFONT)SelectObject(mem_dc, g_font_primary_regular);
    RECT rc_calc = { 0, 0, text_w, 1000 };
    DrawTextA(mem_dc, desc, -1, &rc_calc, DT_CALCRECT | DT_WORDBREAK);
    int desc_h = rc_calc.bottom - rc_calc.top;

    int card_h = card_pad + 18 + 4 + (tag ? 16 : 0) + desc_h + card_pad;

    /* Anchor position: left of the HUD panel */
    int card_x = rc_tr.left - card_w - 10;
    if (card_x < 10) card_x = 10;

    int screen_anchor_y = rc_tr.top + anchor_y;
    int card_y = screen_anchor_y - card_h / 2;
    int min_y = rc_tr.top + 6;
    int max_y = rc_tr.bottom - card_h - 6;
    if (card_y < min_y) card_y = min_y;
    if (card_y > max_y) card_y = max_y;

    /* Render card background and border */
    bool is_dark = (g_current_theme == THEME_DARK);
    COLORREF bg_col = is_dark ? RGB(26, 30, 40) : RGB(255, 255, 255);
    COLORREF bdr_col = is_dark ? RGB(80, 95, 120) : RGB(175, 185, 205);

    HBRUSH br_bg = CreateSolidBrush(bg_col);
    HPEN pen_bdr = CreatePen(PS_SOLID, 1, bdr_col);
    HBRUSH old_b = (HBRUSH)SelectObject(mem_dc, br_bg);
    HPEN old_p = (HPEN)SelectObject(mem_dc, pen_bdr);

    RoundRect(mem_dc, card_x, card_y, card_x + card_w, card_y + card_h, 6, 6);

    SelectObject(mem_dc, old_b);
    SelectObject(mem_dc, old_p);
    DeleteObject(br_bg);
    DeleteObject(pen_bdr);

    /* Draw Title */
    SetBkMode(mem_dc, TRANSPARENT);
    SelectObject(mem_dc, g_font_primary_bold);
    SetTextColor(mem_dc, is_dark ? RGB(245, 185, 60) : RGB(180, 95, 15));
    RECT rc_title = { card_x + card_pad, card_y + card_pad, card_x + card_w - card_pad, card_y + card_pad + 18 };
    DrawTextA(mem_dc, title, -1, &rc_title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    int cur_y = card_y + card_pad + 20;

    /* Draw Tag if present */
    if (tag) {
        SelectObject(mem_dc, g_font_mono_bold);
        SetTextColor(mem_dc, th->text_secondary);
        RECT rc_tag = { card_x + card_pad, cur_y, card_x + card_w - card_pad, cur_y + 14 };
        DrawTextA(mem_dc, tag, -1, &rc_tag, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        cur_y += 16;
    }

    /* Draw Description */
    SelectObject(mem_dc, g_font_primary_regular);
    SetTextColor(mem_dc, th->text_primary);
    RECT rc_desc = { card_x + card_pad, cur_y, card_x + card_w - card_pad, cur_y + desc_h };
    DrawTextA(mem_dc, desc, -1, &rc_desc, DT_WORDBREAK | DT_LEFT);

    SelectObject(mem_dc, old_f);
}

