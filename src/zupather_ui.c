#include "zupather_state.h"
#include "hud_overlay.h"
#include "canvas_render.h"
#include "level_io.h"
#include "dialog_modals.h"
#include "undo_redo.h"
#include <mmsystem.h>
#include <timeapi.h>

static WNDPROC g_orig_edit_proc = NULL;
static WNDPROC g_orig_status_proc = NULL;
static char s_status_part_text[4][256] = {
    "No active canvas | Ctrl+N: New Canvas | Ctrl+O: Open Canvas | Ctrl+Shift+M: Map Manager",
    "---",
    "X: ---",
    "Y: ---"
};

static HWND g_hwnd_toolbar_scroll = NULL;
static LRESULT CALLBACK ToolbarLeftProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK ToolbarScrollProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK ToolbarRightProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK StatusBarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void apply_theme(HWND hwnd, AppTheme new_theme);
static void apply_menu_dark_mode_to_hwnd(HWND hwnd);


/* Calculate uniform, dynamically fitted toolbar button widths and positions.
 * Ensures the padding from text to outer border is mathematically identical across all buttons. */
static void calculate_toolbar_button_layout(int xs[4], int ws[4]) {
    HWND btns[] = { g_btn_canvas, g_btn_element, g_btn_curve, g_btn_overlay };
    const char *fallback_texts[] = {
        "Canvas",
        "Element",
        "Curve",
        "Overlay"
    };
    const int PADDING_X = 18; /* Exactly 9px left padding + 9px right padding for all buttons */
    const int GAP = 4;        /* 4px gap between buttons */

    HDC hdc = g_hwnd_toolbar_left ? GetDC(g_hwnd_toolbar_left) : NULL;
    HFONT old_f = NULL;
    if (hdc && g_font_primary_regular) {
        old_f = (HFONT)SelectObject(hdc, g_font_primary_regular);
    }

    int cur_x = 8;
    for (int i = 0; i < 4; ++i) {
        int text_w = 0;
        wchar_t wtext[64] = {0};
        if (btns[i]) {
            GetWindowTextW(btns[i], wtext, (int)(sizeof(wtext) / sizeof(wtext[0])));
        }
        if (hdc && wtext[0]) {
            SIZE sz;
            if (GetTextExtentPoint32W(hdc, wtext, (int)wcslen(wtext), &sz)) {
                text_w = sz.cx;
            }
        }
        if (text_w <= 0 && hdc) {
            SIZE sz;
            if (GetTextExtentPoint32A(hdc, fallback_texts[i], (int)strlen(fallback_texts[i]), &sz)) {
                text_w = sz.cx;
            }
        }
        if (text_w <= 0) {
            text_w = (int)strlen(fallback_texts[i]) * 7;
        }

        ws[i] = text_w + PADDING_X;
        xs[i] = cur_x;
        cur_x += ws[i] + GAP;
    }

    if (hdc) {
        if (old_f) SelectObject(hdc, old_f);
        ReleaseDC(g_hwnd_toolbar_left, hdc);
    }
}

static int get_toolbar_needed_width(void) {
    int xs[4], ws[4];
    calculate_toolbar_button_layout(xs, ws);
    return xs[3] + ws[3] + 8;
}

static void get_map_button_geometry(int tb_w, int *out_mm_x, int *out_mm_w) {
    int mm_w = 48;
    HDC hdc = g_hwnd_toolbar_left ? GetDC(g_hwnd_toolbar_left) : NULL;
    if (hdc) {
        HFONT old_f = g_font_primary_bold ? (HFONT)SelectObject(hdc, g_font_primary_bold) : NULL;
        SIZE sz;
        if (GetTextExtentPoint32A(hdc, "Map", 3, &sz)) {
            mm_w = sz.cx + 18;
        }
        if (old_f) SelectObject(hdc, old_f);
        ReleaseDC(g_hwnd_toolbar_left, hdc);
    }
    int mm_x = tb_w - mm_w - 8;
    if (mm_x < 8) mm_x = 8;
    if (out_mm_x) *out_mm_x = mm_x;
    if (out_mm_w) *out_mm_w = mm_w;
}

static int get_toolbar_avail_width(void) {
    if (!g_hwnd_toolbar_left) return 0;
    RECT rc;
    GetClientRect(g_hwnd_toolbar_left, &rc);
    int tb_w = rc.right - rc.left;
    int mm_x, mm_w;
    get_map_button_geometry(tb_w, &mm_x, &mm_w);
    int avail_w = mm_x - 4; /* Tepat di sebelah kiri tombol Map */
    return avail_w > 0 ? avail_w : 0;
}

/* Grab Scroll Geometry calculation */
static int get_toolbar_max_scroll(void) {
    int avail_w = get_toolbar_avail_width();
    int needed = get_toolbar_needed_width();
    int max_s = needed - avail_w;
    return max_s > 0 ? max_s : 0;
}

static bool get_grab_scroll_geometry(int *out_track_x, int *out_track_w, int *out_thumb_x, int *out_thumb_w) {
    int avail_w = get_toolbar_avail_width();
    int needed = get_toolbar_needed_width();
    int max_s = needed - avail_w;
    if (max_s <= 0) return false;

    int track_x = 8;
    int track_w = avail_w - 16;
    if (track_w <= 30) return false;

    float vis_ratio = (float)avail_w / (float)needed;
    int thumb_w = (int)roundf((float)track_w * vis_ratio);
    if (thumb_w < 35) thumb_w = 35;
    if (thumb_w > track_w) thumb_w = track_w;

    float scroll_ratio = (max_s > 0) ? ((float)g_tb_scroll_x / (float)max_s) : 0.0f;
    if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;

    int thumb_x = track_x + (int)roundf(scroll_ratio * (float)(track_w - thumb_w));

    if (out_track_x) *out_track_x = track_x;
    if (out_track_w) *out_track_w = track_w;
    if (out_thumb_x) *out_thumb_x = thumb_x;
    if (out_thumb_w) *out_thumb_w = thumb_w;
    return true;
}

static void update_toolbar_buttons_layout(void) {
    if (!g_hwnd_toolbar_left) return;
    RECT rc_tb;
    GetClientRect(g_hwnd_toolbar_left, &rc_tb);
    int tb_w = rc_tb.right - rc_tb.left;

    int mm_x, mm_w;
    get_map_button_geometry(tb_w, &mm_x, &mm_w);

    int avail_w = mm_x - 4; /* Tepat di sebelah kiri tombol "Map" */
    if (avail_w < 0) avail_w = 0;

    if (g_hwnd_toolbar_scroll) {
        SetWindowPos(g_hwnd_toolbar_scroll, NULL, 0, 0, avail_w, TOOLBAR_HEIGHT,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (g_btn_manage_maps) {
        SetWindowPos(g_btn_manage_maps, NULL, mm_x, 5, mm_w, 26,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    int max_s = get_toolbar_max_scroll();
    if (g_tb_scroll_x > max_s) g_tb_scroll_x = max_s;
    if (g_tb_scroll_x < 0) g_tb_scroll_x = 0;

    int xs[4], ws[4];
    calculate_toolbar_button_layout(xs, ws);
    HWND btns[] = { g_btn_canvas, g_btn_element, g_btn_curve, g_btn_overlay };

    HDWP hdwp = BeginDeferWindowPos(4);
    for (int i = 0; i < 4; ++i) {
        if (btns[i]) {
            hdwp = DeferWindowPos(hdwp, btns[i], NULL, xs[i] - g_tb_scroll_x, 5, ws[i], 26,
                                  SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    EndDeferWindowPos(hdwp);

    refresh_toolbar_buttons();
}

/* Force instant synchronous redraw on all toolbar buttons without message queue lag */
void refresh_toolbar_buttons(void) {
    if (!g_hwnd_toolbar_left) return;
    HWND btns[] = { g_btn_canvas, g_btn_element, g_btn_curve, g_btn_overlay, g_btn_manage_maps };
    for (int i = 0; i < 5; ++i) {
        if (btns[i]) {
            RedrawWindow(btns[i], NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
        }
    }
    if (g_hwnd_toolbar_scroll) {
        RedrawWindow(g_hwnd_toolbar_scroll, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    }
    RedrawWindow(g_hwnd_toolbar_left, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
}

/* Helper to draw owner-drawn buttons cleanly across all toolbars and views */
static void draw_custom_button(LPDRAWITEMSTRUCT dis) {
    if (!dis) return;
    const ThemeColors *th = get_theme();
    bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;

    COLORREF bg_color = is_pressed ? th->btn_bg_pressed : th->btn_bg;
    if (dis->CtlID == ID_BTN_MANAGE_MAPS) {
        bg_color = is_pressed ? RGB(18, 70, 140) : (g_current_theme == THEME_DARK ? RGB(30, 42, 60) : RGB(232, 240, 252));
    }
    HBRUSH bg_brush = CreateSolidBrush(bg_color);
    FillRect(dis->hDC, &dis->rcItem, bg_brush);
    DeleteObject(bg_brush);

    COLORREF border_color = th->btn_border;
    if (dis->CtlID == ID_BTN_MANAGE_MAPS) {
        border_color = th->text_accent_blue;
    } else if (dis->CtlID == ID_BTN_CANVAS && g_mode_canvas) {
        border_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_ELEMENT && g_mode_element) {
        border_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_CURVE && g_mode_curve) {
        border_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_OVERLAY && g_mode_overlay) {
        border_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_FROG && g_placing_frog) {
        border_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_COIN && g_placing_coin) {
        border_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_CUTOUT && g_placing_cutout) {
        border_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_SPRITE && g_placing_sprite) {
        border_color = th->text_accent_green;
    } else if (is_pressed) {
        border_color = th->text_accent_blue;
    }

    HPEN border_pen = CreatePen(PS_SOLID, 1, border_color);
    HPEN old_pen = (HPEN)SelectObject(dis->hDC, border_pen);
    SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
    SelectObject(dis->hDC, old_pen);
    DeleteObject(border_pen);

    SetBkMode(dis->hDC, TRANSPARENT);
    COLORREF text_color = is_pressed ? th->text_accent_blue : th->btn_text;
    if (dis->CtlID == ID_BTN_MANAGE_MAPS) {
        text_color = is_pressed ? RGB(255, 255, 255) : th->text_accent_blue;
    } else if (dis->CtlID == ID_BTN_CANVAS && g_mode_canvas && !is_pressed) {
        text_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_ELEMENT && g_mode_element && !is_pressed) {
        text_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_CURVE && g_mode_curve && !is_pressed) {
        text_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_OVERLAY && g_mode_overlay && !is_pressed) {
        text_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_FROG && g_placing_frog && !is_pressed) {
        text_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_COIN && g_placing_coin && !is_pressed) {
        text_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_CUTOUT && g_placing_cutout && !is_pressed) {
        text_color = th->text_accent_green;
    } else if (dis->CtlID == ID_BTN_SPRITE && g_placing_sprite && !is_pressed) {
        text_color = th->text_accent_green;
    }
    SetTextColor(dis->hDC, text_color);
    HFONT btn_font = (dis->CtlID == ID_BTN_CANVAS_APPLY) ? g_font_mono_bold :
                     ((dis->CtlID == ID_BTN_MANAGE_MAPS && g_font_primary_bold) ? g_font_primary_bold : g_font_primary_regular);
    SelectObject(dis->hDC, btn_font);

    wchar_t wtext[64];
    GetWindowTextW(dis->hwndItem, wtext, (int)(sizeof(wtext) / sizeof(wtext[0])));

    RECT rc_text = dis->rcItem;
    if (is_pressed) {
        rc_text.left += 1;
        rc_text.top += 1;
    }
    DrawTextW(dis->hDC, wtext, -1, &rc_text, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

/* Viewport Procedure for the scrollable container wrapping the 4 authoring buttons:
 * Canvas, Element, Curve, and Overlay.
 * Hardware-clipped to avail_w so it NEVER bleeds or draws over the Map button area! */
static LRESULT CALLBACK ToolbarScrollProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            return SendMessage(GetAncestor(hwnd, GA_ROOT), WM_COMMAND, wParam, lParam);

        case WM_CTLCOLORBTN: {
            const ThemeColors *th = get_theme();
            static HBRUSH s_btn_bg_brush = NULL;
            static COLORREF s_last_btn_bg = 0xFFFFFFFF;
            if (!s_btn_bg_brush || s_last_btn_bg != th->btn_bg) {
                if (s_btn_bg_brush) DeleteObject(s_btn_bg_brush);
                s_btn_bg_brush = CreateSolidBrush(th->btn_bg);
                s_last_btn_bg = th->btn_bg;
            }
            return (LRESULT)s_btn_bg_brush;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis && dis->CtlType == ODT_BUTTON) {
                draw_custom_button(dis);
                return TRUE;
            }
            return SendMessage(GetAncestor(hwnd, GA_ROOT), WM_DRAWITEM, wParam, lParam);
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const ThemeColors *th = get_theme();

            /* Solid background */
            HBRUSH brush = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);

            /* Bottom border */
            HPEN pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, 0, rc.bottom - 1, NULL);
            LineTo(hdc, rc.right, rc.bottom - 1);

            /* Grab Scroll Bar if overflowing (strictly bounded within the scrollable viewport) */
            int track_x, track_w, thumb_x, thumb_w;
            if (get_grab_scroll_geometry(&track_x, &track_w, &thumb_x, &thumb_w)) {
                int track_y = TOOLBAR_HEIGHT - 6;
                int track_h = 3;

                COLORREF track_col = (g_current_theme == THEME_DARK) ? RGB(38, 42, 50) : RGB(220, 224, 230);
                HBRUSH tr_br = CreateSolidBrush(track_col);
                RECT rc_tr = {track_x, track_y, track_x + track_w, track_y + track_h};
                FillRect(hdc, &rc_tr, tr_br);
                DeleteObject(tr_br);

                COLORREF thumb_col;
                if (g_current_theme == THEME_DARK) {
                    thumb_col = (g_is_dragging_thumb || g_is_dragging_tb_bg) ? RGB(170, 178, 195) : RGB(90, 96, 110);
                } else {
                    thumb_col = (g_is_dragging_thumb || g_is_dragging_tb_bg) ? RGB(95, 102, 115) : RGB(175, 180, 190);
                }

                HBRUSH th_br = CreateSolidBrush(thumb_col);
                HPEN th_pen = CreatePen(PS_SOLID, 1, thumb_col);
                SelectObject(hdc, th_pen);
                SelectObject(hdc, th_br);

                RoundRect(hdc, thumb_x, track_y - 1, thumb_x + thumb_w, track_y + track_h + 1, 4, 4);

                DeleteObject(th_pen);
                DeleteObject(th_br);
            }

            SelectObject(hdc, old_pen);
            DeleteObject(pen);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                if (g_is_dragging_thumb || g_is_dragging_tb_bg) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
                int track_x, track_w, thumb_x, thumb_w;
                if (get_grab_scroll_geometry(&track_x, &track_w, &thumb_x, &thumb_w)) {
                    POINT pt;
                    GetCursorPos(&pt);
                    ScreenToClient(hwnd, &pt);
                    if (pt.y >= TOOLBAR_HEIGHT - 10 && pt.y <= TOOLBAR_HEIGHT &&
                        pt.x >= track_x && pt.x <= track_x + track_w) {
                        SetCursor(LoadCursor(NULL, IDC_HAND));
                        return TRUE;
                    }
                }
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            SetFocus(GetAncestor(hwnd, GA_ROOT));

            int track_x, track_w, thumb_x, thumb_w;
            if (get_grab_scroll_geometry(&track_x, &track_w, &thumb_x, &thumb_w)) {
                int max_s = get_toolbar_max_scroll();
                if (my >= TOOLBAR_HEIGHT - 10 && mx >= track_x && mx <= track_x + track_w) {
                    if (mx >= thumb_x && mx <= thumb_x + thumb_w) {
                        g_is_dragging_thumb = true;
                        g_drag_thumb_mouse_start_x = mx;
                        g_drag_thumb_scroll_start = g_tb_scroll_x;
                        SetCapture(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    } else {
                        int travel = track_w - thumb_w;
                        if (travel > 0 && max_s > 0) {
                            float ratio = (float)(mx - track_x - thumb_w / 2) / (float)travel;
                            if (ratio < 0.0f) ratio = 0.0f;
                            if (ratio > 1.0f) ratio = 1.0f;
                            g_tb_scroll_x = (int)roundf(ratio * (float)max_s);
                            g_is_dragging_thumb = true;
                            g_drag_thumb_mouse_start_x = mx;
                            g_drag_thumb_scroll_start = g_tb_scroll_x;
                            update_toolbar_buttons_layout();
                            SetCapture(hwnd);
                            return 0;
                        }
                    }
                } else {
                    g_is_dragging_tb_bg = true;
                    g_drag_bg_mouse_start_x = mx;
                    g_drag_bg_scroll_start = g_tb_scroll_x;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lParam);
            if (g_is_dragging_thumb) {
                int track_x, track_w, thumb_x, thumb_w;
                if (get_grab_scroll_geometry(&track_x, &track_w, &thumb_x, &thumb_w)) {
                    int max_s = get_toolbar_max_scroll();
                    int travel = track_w - thumb_w;
                    if (travel > 0 && max_s > 0) {
                        int dx = mx - g_drag_thumb_mouse_start_x;
                        int new_s = g_drag_thumb_scroll_start + (int)roundf((float)dx * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_tb_scroll_x) {
                            g_tb_scroll_x = new_s;
                            update_toolbar_buttons_layout();
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_tb_bg) {
                int max_s = get_toolbar_max_scroll();
                if (max_s > 0) {
                    int dx = mx - g_drag_bg_mouse_start_x;
                    int new_s = g_drag_bg_scroll_start - dx;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_tb_scroll_x) {
                        g_tb_scroll_x = new_s;
                        update_toolbar_buttons_layout();
                    }
                }
                return 0;
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_is_dragging_thumb || g_is_dragging_tb_bg) {
                g_is_dragging_thumb = false;
                g_is_dragging_tb_bg = false;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_CAPTURECHANGED: {
            g_is_dragging_thumb = false;
            g_is_dragging_tb_bg = false;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            int max_s = get_toolbar_max_scroll();
            if (max_s > 0) {
                g_tb_scroll_x -= (zDelta > 0 ? 50 : -50);
                if (g_tb_scroll_x < 0) g_tb_scroll_x = 0;
                if (g_tb_scroll_x > max_s) g_tb_scroll_x = max_s;
                update_toolbar_buttons_layout();
            }
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* Master Toolbar Procedure: Hosts the scrollable buttons container and the fixed Map button */
static LRESULT CALLBACK ToolbarLeftProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            return SendMessage(GetAncestor(hwnd, GA_ROOT), WM_COMMAND, wParam, lParam);

        case WM_CTLCOLORBTN: {
            const ThemeColors *th = get_theme();
            static HBRUSH s_btn_bg_brush = NULL;
            static COLORREF s_last_btn_bg = 0xFFFFFFFF;
            if (!s_btn_bg_brush || s_last_btn_bg != th->btn_bg) {
                if (s_btn_bg_brush) DeleteObject(s_btn_bg_brush);
                s_btn_bg_brush = CreateSolidBrush(th->btn_bg);
                s_last_btn_bg = th->btn_bg;
            }
            return (LRESULT)s_btn_bg_brush;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis && dis->CtlType == ODT_BUTTON) {
                draw_custom_button(dis);
                return TRUE;
            }
            return SendMessage(GetAncestor(hwnd, GA_ROOT), WM_DRAWITEM, wParam, lParam);
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const ThemeColors *th = get_theme();

            /* Solid background */
            HBRUSH brush = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);

            /* Bottom border */
            HPEN pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, 0, rc.bottom - 1, NULL);
            LineTo(hdc, rc.right, rc.bottom - 1);
            SelectObject(hdc, old_pen);
            DeleteObject(pen);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE: {
            update_toolbar_buttons_layout();
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            int max_s = get_toolbar_max_scroll();
            if (max_s > 0) {
                g_tb_scroll_x -= (zDelta > 0 ? 50 : -50);
                if (g_tb_scroll_x < 0) g_tb_scroll_x = 0;
                if (g_tb_scroll_x > max_s) g_tb_scroll_x = max_s;
                update_toolbar_buttons_layout();
            }
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* Viewport Procedure for the solid Right Panel */
static LRESULT CALLBACK ToolbarRightProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            return SendMessage(GetAncestor(hwnd, GA_ROOT), WM_COMMAND, wParam, lParam);

        case WM_DRAWITEM:
            return SendMessage(GetAncestor(hwnd, GA_ROOT), WM_DRAWITEM, wParam, lParam);

        case WM_CTLCOLOREDIT:
            return SendMessage(GetAncestor(hwnd, GA_ROOT), WM_CTLCOLOREDIT, wParam, lParam);

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const ThemeColors *th = get_theme();

            /* Solid background */
            HBRUSH brush = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);

            /* Left vertical border separating from toolbar buttons */
            HPEN pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, 0, 0, NULL);
            LineTo(hdc, 0, rc.bottom);

            /* Bottom border */
            MoveToEx(hdc, 0, rc.bottom - 1, NULL);
            LineTo(hdc, rc.right, rc.bottom - 1);
            SelectObject(hdc, old_pen);
            DeleteObject(pen);

            /* Labels */
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, th->text_secondary);
            SelectObject(hdc, g_font_primary_regular);
            RECT rc_lbl_canvas = {8, 0, 63, TOOLBAR_HEIGHT};
            DrawTextA(hdc, "Canvas:", -1, &rc_lbl_canvas, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);

            SelectObject(hdc, g_font_mono_bold);
            RECT rc_lbl_x = {128, 0, 144, TOOLBAR_HEIGHT};
            DrawTextA(hdc, "x", -1, &rc_lbl_x, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* Subclass procedure for Edit controls to catch Enter and Tab keys */
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            HWND main_wnd = GetAncestor(hwnd, GA_ROOT);
            if (hwnd == g_edit_preview_title) {
                char buf[256];
                GetWindowTextA(g_edit_preview_title, buf, sizeof(buf));
                sync_canvas_title_everywhere(buf, g_edit_preview_title);
                SetFocus(main_wnd);
                InvalidateRect(main_wnd, NULL, FALSE);
                return 0;
            }
            if (hwnd == g_edit_map_skullrot || hwnd == g_edit_dev_curvecolor ||
                hwnd == g_edit_dev_bgcolor || hwnd == g_edit_dev_huerot ||
                hwnd == g_edit_dev_treasurefreq || hwnd == g_edit_dev_partime) {
                SetFocus(main_wnd);
                InvalidateRect(main_wnd, NULL, FALSE);
                return 0;
            }
            if (hwnd == g_edit_prio) {
                SetFocus(main_wnd);
                return 0;
            }
            if (hwnd == g_edit_cv_w || hwnd == g_edit_cv_h) {
                SendMessage(main_wnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CV_APPLY, BN_CLICKED), (LPARAM)hwnd);
                SetFocus(main_wnd);
                return 0;
            }
            SendMessage(main_wnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CANVAS_APPLY, BN_CLICKED), (LPARAM)hwnd);
            SetFocus(main_wnd);
            return 0;
        } else if (wParam == VK_ESCAPE) {
            HWND main_wnd = GetAncestor(hwnd, GA_ROOT);
            if (hwnd == g_edit_preview_title) {
                SetWindowTextA(g_edit_preview_title, g_canvas_title);
                SetFocus(main_wnd);
                return 0;
            }
            if (hwnd == g_edit_map_skullrot || hwnd == g_edit_dev_curvecolor ||
                hwnd == g_edit_dev_bgcolor || hwnd == g_edit_dev_huerot ||
                hwnd == g_edit_dev_treasurefreq || hwnd == g_edit_dev_partime) {
                sync_all_map_attr_edits_from_state();
                SetFocus(main_wnd);
                InvalidateRect(main_wnd, NULL, FALSE);
                return 0;
            }
            if (hwnd == g_edit_cv_w || hwnd == g_edit_cv_h) {
                char buf_w[32], buf_h[32];
                snprintf(buf_w, sizeof(buf_w), "%d", g_canvas_w);
                snprintf(buf_h, sizeof(buf_h), "%d", g_canvas_h);
                if (g_edit_cv_w) SetWindowTextA(g_edit_cv_w, buf_w);
                if (g_edit_cv_h) SetWindowTextA(g_edit_cv_h, buf_h);
            }
            SetFocus(main_wnd);
            return 0;
        } else if (wParam == VK_TAB) {
            if (hwnd == g_edit_preview_title || hwnd == g_edit_map_skullrot ||
                hwnd == g_edit_dev_curvecolor || hwnd == g_edit_dev_bgcolor ||
                hwnd == g_edit_dev_huerot || hwnd == g_edit_dev_treasurefreq ||
                hwnd == g_edit_dev_partime) {
                HWND main_wnd = GetAncestor(hwnd, GA_ROOT);
                SetFocus(main_wnd);
                return 0;
            }
            if (hwnd == g_edit_cv_w) {
                if (g_edit_cv_h) {
                    SetFocus(g_edit_cv_h);
                    SendMessage(g_edit_cv_h, EM_SETSEL, 0, -1);
                }
                return 0;
            } else if (hwnd == g_edit_cv_h) {
                HWND main_wnd = GetAncestor(hwnd, GA_ROOT);
                SetFocus(main_wnd);
                return 0;
            }
            if (hwnd == g_edit_prio) {
                HWND root = GetAncestor(hwnd, GA_ROOT);
                SetFocus(root);
                return 0;
            }
            if (hwnd == g_edit_w) {
                SetFocus(g_edit_h);
                SendMessage(g_edit_h, EM_SETSEL, 0, -1);
            } else if (hwnd == g_edit_h) {
                SetFocus(g_btn_apply_size);
            }
            return 0;
        }
    } else if (msg == WM_MOUSEWHEEL && hwnd == g_edit_prio) {
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (g_mode_overlay) {
            if (zDelta > 0) {
                if (g_overlay_current_prio < 99) g_overlay_current_prio++;
            } else if (g_overlay_current_prio > 0) {
                g_overlay_current_prio--;
            }
            g_pending_cutout_pri = g_overlay_current_prio;
            g_pending_sprite_pri = g_overlay_current_prio;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", g_overlay_current_prio);
            g_in_prio_update = true;
            SetWindowTextA(g_edit_prio, buf);
            g_in_prio_update = false;
        } else {
            if (zDelta > 0) {
                g_var_prio_val++;
            } else if (g_var_prio_val > 0) {
                g_var_prio_val--;
            }
            g_var_write_prio = true;
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", g_var_prio_val);
            g_in_prio_update = true;
            SetWindowTextA(g_edit_prio, buf);
            g_in_prio_update = false;
        }
        HWND root = GetAncestor(hwnd, GA_ROOT);
        InvalidateRect(root, NULL, FALSE);
        return 0;
    } else if (msg == WM_MOUSEMOVE) {
        HWND main_wnd = GetAncestor(hwnd, GA_ROOT);
        MapAttrHoverItem h_item = MAP_ATTR_HOVER_NONE;
        int box_y, skull_y, space_y, sep3_y, sum_y, sep4_y, dev_y, dev_items[8];
        get_map_attr_hud_layout(&box_y, &skull_y, &space_y, &sep3_y, &sum_y, &sep4_y, &dev_y, dev_items, NULL);
        int sy = -g_hud_preview_info_scroll_y;
        int anchor_y = 0;

        if (hwnd == g_edit_preview_title) {
            h_item = MAP_ATTR_HOVER_TITLE;
            anchor_y = box_y + 12 + sy;
        } else if (hwnd == g_edit_map_skullrot) {
            h_item = MAP_ATTR_HOVER_SKULLROT;
            anchor_y = skull_y + 11 + sy;
        } else if (hwnd == g_edit_dev_curvecolor) {
            h_item = MAP_ATTR_HOVER_CURVECOLOR;
            anchor_y = dev_items[2] + 11 + sy;
        } else if (hwnd == g_edit_dev_bgcolor) {
            h_item = MAP_ATTR_HOVER_BGCOLOR;
            anchor_y = dev_items[3] + 11 + sy;
        } else if (hwnd == g_edit_dev_huerot) {
            h_item = MAP_ATTR_HOVER_HUEROT;
            anchor_y = dev_items[5] + 11 + sy;
        } else if (hwnd == g_edit_dev_treasurefreq) {
            h_item = MAP_ATTR_HOVER_TREASUREFREQ;
            anchor_y = dev_items[6] + 11 + sy;
        } else if (hwnd == g_edit_dev_partime) {
            h_item = MAP_ATTR_HOVER_PARTIME;
            anchor_y = dev_items[7] + 11 + sy;
        }

        if (h_item != MAP_ATTR_HOVER_NONE) {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);

            if (g_hud_map_attr_hover != h_item || g_hud_map_attr_hover_y != anchor_y) {
                g_hud_map_attr_hover = h_item;
                g_hud_map_attr_hover_y = anchor_y;
                InvalidateRect(main_wnd, NULL, FALSE);
                update_status_bar();
            }
        }
    } else if (msg == WM_MOUSELEAVE) {
        HWND main_wnd = GetAncestor(hwnd, GA_ROOT);
        if (g_hud_map_attr_hover != MAP_ATTR_HOVER_NONE) {
            g_hud_map_attr_hover = MAP_ATTR_HOVER_NONE;
            InvalidateRect(main_wnd, NULL, FALSE);
            update_status_bar();
        }
    }
    return CallWindowProcA(g_orig_edit_proc, hwnd, msg, wParam, lParam);
}

/* Custom dark-mode-aware subclass for status bar (eliminates Win32 white borders, gaps, and size grip) */
static LRESULT CALLBACK StatusBarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_NCPAINT:
            return 0;

        case WM_SETCURSOR: {
            HWND root_wnd = GetAncestor(hwnd, GA_ROOT);
            if (root_wnd && !IsZoomed(root_wnd)) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                RECT rc;
                GetClientRect(hwnd, &rc);
                if (pt.x >= rc.right - 18 && pt.y >= rc.bottom - 18) {
                    SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
                    return TRUE;
                }
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            HWND root_wnd = GetAncestor(hwnd, GA_ROOT);
            if (root_wnd && !IsZoomed(root_wnd)) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                RECT rc;
                GetClientRect(hwnd, &rc);
                if (pt.x >= rc.right - 18 && pt.y >= rc.bottom - 18) {
                    ReleaseCapture();
                    SendMessage(root_wnd, WM_SYSCOMMAND, (WPARAM)(SC_SIZE + 8), 0);
                    return 0;
                }
            }
            break;
        }

        case SB_SETTEXTA: {
            int part = (int)(wParam & 0xFF);
            if (part >= 0 && part < 4 && lParam) {
                strncpy(s_status_part_text[part], (const char*)lParam, sizeof(s_status_part_text[part]) - 1);
                s_status_part_text[part][sizeof(s_status_part_text[part]) - 1] = '\0';
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return CallWindowProcA(g_orig_status_proc, hwnd, msg, wParam, lParam);
        }

        case SB_SETTEXTW: {
            int part = (int)(wParam & 0xFF);
            if (part >= 0 && part < 4 && lParam) {
                WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)lParam, -1, s_status_part_text[part], sizeof(s_status_part_text[part]), NULL, NULL);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return CallWindowProcA(g_orig_status_proc, hwnd, msg, wParam, lParam);
        }

        case SB_GETTEXTA: {
            int part = (int)(wParam & 0xFF);
            if (part >= 0 && part < 4 && lParam) {
                strcpy((char*)lParam, s_status_part_text[part]);
                return (LRESULT)strlen(s_status_part_text[part]);
            }
            return 0;
        }

        case SB_GETTEXTLENGTHA: {
            int part = (int)(wParam & 0xFF);
            if (part >= 0 && part < 4) {
                return (LRESULT)strlen(s_status_part_text[part]);
            }
            return 0;
        }

        case WM_SIZE: {
            LRESULT res = CallWindowProcA(g_orig_status_proc, hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, NULL, FALSE);
            return res;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (!hdc) return 0;

            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            if (w <= 0 || h <= 0) {
                EndPaint(hwnd, &ps);
                return 0;
            }

            /* Off-screen buffer to eliminate all flicker and eliminate native white borders */
            HDC mem_dc = CreateCompatibleDC(hdc);
            HBITMAP mem_bm = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP old_bm = (HBITMAP)SelectObject(mem_dc, mem_bm);

            const ThemeColors *th = get_theme();

            /* 1. Fill entire bar background with theme bar_bg (ZERO white pixels!) */
            HBRUSH bg_br = CreateSolidBrush(th->bar_bg);
            FillRect(mem_dc, &rc, bg_br);
            DeleteObject(bg_br);

            /* 2. Crisp 1px top border with theme bar_border */
            HPEN border_pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(mem_dc, border_pen);
            MoveToEx(mem_dc, 0, 0, NULL);
            LineTo(mem_dc, w, 0);

            /* 3. Compute part rectangles */
            HWND root_wnd = GetAncestor(hwnd, GA_ROOT);
            bool show_grip = (root_wnd && !IsZoomed(root_wnd));
            int grip_w = show_grip ? 18 : 0;

            int p0_right = (w - 285 > 100) ? (w - 285) : (w / 2);
            int p1_right = p0_right + 95;
            int p2_right = p1_right + 95;
            int p3_right = w - grip_w;
            if (p3_right < p2_right) p3_right = w;

            RECT part_rc[4];
            part_rc[0].left = 0;        part_rc[0].right = p0_right;
            part_rc[1].left = p0_right;  part_rc[1].right = p1_right;
            part_rc[2].left = p1_right;  part_rc[2].right = p2_right;
            part_rc[3].left = p2_right;  part_rc[3].right = p3_right;

            for (int i = 0; i < 4; ++i) {
                part_rc[i].top = 1;
                part_rc[i].bottom = h;
            }

            /* 4. Draw vertical divider lines between panels */
            for (int i = 1; i < 4; ++i) {
                int div_x = part_rc[i].left;
                if (div_x > 0 && div_x < w) {
                    MoveToEx(mem_dc, div_x, 3, NULL);
                    LineTo(mem_dc, div_x, h - 3);
                }
            }

            /* 5. Draw text for each panel */
            SetBkMode(mem_dc, TRANSPARENT);
            for (int i = 0; i < 4; ++i) {
                HFONT font = (i == 0) ? g_font_primary_regular : g_font_mono_bold;
                bool x_out = g_has_canvas ? (g_canvas_mouse_x < 0.0f || g_canvas_mouse_x > (float)g_canvas_w) : false;
                bool y_out = g_has_canvas ? (g_canvas_mouse_y < 0.0f || g_canvas_mouse_y > (float)g_canvas_h) : false;

                COLORREF color = (i == 0) ? th->text_primary :
                                 (i == 1) ? (g_has_canvas ? th->text_accent_green : th->text_dim) :
                                 (i == 2) ? (!g_has_canvas ? th->text_dim : (x_out ? th->text_accent_red : th->text_primary)) :
                                            (!g_has_canvas ? th->text_dim : (y_out ? th->text_accent_red : th->text_primary));

                SelectObject(mem_dc, font);
                SetTextColor(mem_dc, color);

                RECT text_rc = part_rc[i];
                if (i == 0) {
                    text_rc.left += 8;
                    text_rc.right -= 6;
                    DrawTextA(mem_dc, s_status_part_text[0], -1, &text_rc, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                } else {
                    DrawTextA(mem_dc, s_status_part_text[i], -1, &text_rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                }
            }

            /* 6. Draw clean themed size grip dots (NO white background box!) */
            if (show_grip) {
                COLORREF grip_dot = (g_current_theme == THEME_DARK) ? RGB(90, 96, 110) : RGB(140, 145, 155);
                COLORREF grip_shadow = (g_current_theme == THEME_DARK) ? RGB(16, 18, 22) : RGB(255, 255, 255);
                for (int row = 0; row < 3; ++row) {
                    for (int col = 0; col <= row; ++col) {
                        int gx = w - 6 - (row * 4) + (col * 4);
                        int gy = h - 6 - (col * 4);
                        if (gx >= 0 && gy >= 0 && gx < w && gy < h) {
                            SetPixel(mem_dc, gx, gy, grip_dot);
                            if (gx + 1 < w && gy + 1 < h) {
                                SetPixel(mem_dc, gx + 1, gy + 1, grip_shadow);
                            }
                        }
                    }
                }
            }

            SelectObject(mem_dc, old_pen);
            DeleteObject(border_pen);

            /* Blit to screen atomically */
            BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);

            SelectObject(mem_dc, old_bm);
            DeleteObject(mem_bm);
            DeleteDC(mem_dc);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return CallWindowProcA(g_orig_status_proc, hwnd, msg, wParam, lParam);
}


/* Keyboard Accelerator Table (Consistent Ctrl+ Required) */
static HACCEL create_app_accelerators(void) {
    ACCEL accels[] = {
        { FCONTROL | FVIRTKEY, 'Z', ID_MENU_EDIT_UNDO },
        { FCONTROL | FVIRTKEY, 'Y', ID_MENU_EDIT_REDO },
        { FCONTROL | FVIRTKEY, 'N', ID_MENU_FILE_NEW },
        { FCONTROL | FVIRTKEY, 'O', ID_MENU_FILE_OPEN },
        { FCONTROL | FVIRTKEY, 'S', ID_MENU_FILE_SAVE },
        { FCONTROL | FVIRTKEY, 'B', ID_MENU_CANVAS_CHANGE_IMG },
        { FCONTROL | FALT | FVIRTKEY, 'B', ID_MENU_CANVAS_REMOVE_IMG },
        { FCONTROL | FVIRTKEY, 'M', ID_BTN_CANVAS },
        { FCONTROL | FSHIFT | FVIRTKEY, 'M', ID_MENU_FILE_MANAGE_LEVELS },
        { FCONTROL | FSHIFT | FVIRTKEY, 'C', ID_MENU_CANVAS_ADD_CUTOUT },
        { FCONTROL | FVIRTKEY, 'E', ID_BTN_ELEMENT },
        { FCONTROL | FVIRTKEY, 'P', ID_BTN_FROG },
        { FCONTROL | FVIRTKEY, 'K', ID_BTN_COIN },
        { FCONTROL | FVIRTKEY, 'U', ID_BTN_CURVE },
        { FCONTROL | FSHIFT | FVIRTKEY, 'O', ID_BTN_OVERLAY },
        { FCONTROL | FSHIFT | FVIRTKEY, 'B', ID_OVERLAY_ADD_BGALPHA },
        { FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_MENU_CANVAS_ADD_SPRITE },
        { FCONTROL | FVIRTKEY, 'J', ID_OVERLAY_SET_VARIABLE },
        { FCONTROL | FVIRTKEY, 'A', ID_MENU_SELECT_ALL },
        { FCONTROL | FVIRTKEY, 'I', ID_MENU_INVERT_SEL },
        { FCONTROL | FVIRTKEY, 'D', ID_MENU_DESELECT_ALL },
        { FCONTROL | FSHIFT | FVIRTKEY, 'I', ID_MENU_CURVE_IMPORT_SVG },
        { FCONTROL | FSHIFT | FVIRTKEY, 'E', ID_MENU_CURVE_EXPORT_SVG },
        { FCONTROL | FVIRTKEY, '0', ID_MENU_VIEW_FIT },
        { FCONTROL | FVIRTKEY, '1', ID_MENU_VIEW_100 },
        { FCONTROL | FVIRTKEY, 'R', ID_MENU_VIEW_RESET },
        { FCONTROL | FVIRTKEY, 'T', ID_MENU_THEME_TOGGLE },
        { FCONTROL | FVIRTKEY, 'G', ID_MENU_VIEW_GRID },
        { FCONTROL | FVIRTKEY, 'L', ID_MENU_LEVEL_PREVIEW },
        { FCONTROL | FVIRTKEY, VK_OEM_PLUS, ID_MENU_VIEW_ZOOMIN },
        { FCONTROL | FVIRTKEY, VK_OEM_MINUS, ID_MENU_VIEW_ZOOMOUT }
    };
    return CreateAcceleratorTableA(accels, (int)(sizeof(accels) / sizeof(accels[0])));
}

/* Create Themed Toolbar Viewports and Controls */
static void create_toolbar_buttons(HWND hwnd) {
    HINSTANCE hinst = GetModuleHandle(NULL);

    WNDCLASSA wc_left;
    memset(&wc_left, 0, sizeof(wc_left));
    wc_left.lpfnWndProc = ToolbarLeftProc;
    wc_left.hInstance = hinst;
    wc_left.lpszClassName = "ZuPatherToolbarLeft";
    wc_left.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc_left);

    RECT rc_client;
    GetClientRect(hwnd, &rc_client);
    int client_w = rc_client.right - rc_client.left;

    WNDCLASSA wc_scroll;
    memset(&wc_scroll, 0, sizeof(wc_scroll));
    wc_scroll.lpfnWndProc = ToolbarScrollProc;
    wc_scroll.hInstance = hinst;
    wc_scroll.lpszClassName = "ZuPatherToolbarScroll";
    wc_scroll.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc_scroll);

    g_hwnd_toolbar_left = CreateWindowExA(
        0, "ZuPatherToolbarLeft", NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, client_w, TOOLBAR_HEIGHT,
        hwnd, (HMENU)991, hinst, NULL
    );

    int mm_x, mm_w;
    get_map_button_geometry(client_w, &mm_x, &mm_w);
    int avail_w = mm_x - 4; /* Tepat di sebelah kiri tombol Map */
    if (avail_w < 0) avail_w = 0;

    /* Scrollable viewport container strictly masked right before the Map button */
    g_hwnd_toolbar_scroll = CreateWindowExA(
        0, "ZuPatherToolbarScroll", NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, avail_w, TOOLBAR_HEIGHT,
        g_hwnd_toolbar_left, (HMENU)992, hinst, NULL
    );

    g_hwnd_toolbar_right = NULL;
    g_edit_w = NULL;
    g_edit_h = NULL;
    g_btn_apply_size = NULL;

    /* In-Level Attribute Authoring Buttons: Canvas, Element, Curve, and Overlay */
    /* Created inside g_hwnd_toolbar_scroll so they are hardware-masked from the Map button area */
    g_btn_canvas  = CreateWindowA("BUTTON", "Canvas", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                                  8, 5, 80, 26, g_hwnd_toolbar_scroll, (HMENU)ID_BTN_CANVAS, hinst, NULL);
    g_btn_element = CreateWindowA("BUTTON", "Element", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                                  94, 5, 85, 26, g_hwnd_toolbar_scroll, (HMENU)ID_BTN_ELEMENT, hinst, NULL);
    g_btn_curve   = CreateWindowA("BUTTON", "Curve",   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                                  185, 5, 75, 26, g_hwnd_toolbar_scroll, (HMENU)ID_BTN_CURVE, hinst, NULL);
    g_btn_overlay = CreateWindowA("BUTTON", "Overlay", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                                  266, 5, 85, 26, g_hwnd_toolbar_scroll, (HMENU)ID_BTN_OVERLAY, hinst, NULL);

    /* Map Action Button - positioned on far right with distinct style directly on g_hwnd_toolbar_left */
    g_btn_manage_maps = CreateWindowA("BUTTON", "Map", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                                      mm_x, 5, mm_w, 26, g_hwnd_toolbar_left, (HMENU)ID_BTN_MANAGE_MAPS, hinst, NULL);

    update_toolbar_buttons_layout();

    g_edit_cv_w = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "---",
                                  WS_CHILD | WS_CLIPSIBLINGS | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                                  0, 0, 62, 24, hwnd, (HMENU)ID_EDIT_CV_W, hinst, NULL);
    g_edit_cv_h = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "---",
                                  WS_CHILD | WS_CLIPSIBLINGS | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                                  0, 0, 62, 24, hwnd, (HMENU)ID_EDIT_CV_H, hinst, NULL);

    SendMessage(g_edit_cv_w, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);
    SendMessage(g_edit_cv_h, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);

    g_orig_edit_proc = (WNDPROC)SetWindowLongPtrA(g_edit_cv_w, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    SetWindowLongPtrA(g_edit_cv_h, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

    g_edit_prio = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0",
                                  WS_CHILD | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                                  0, 0, 52, 22, hwnd, (HMENU)ID_EDIT_PRIO, hinst, NULL);
    SendMessage(g_edit_prio, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);
    SetWindowLongPtrA(g_edit_prio, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

    g_edit_preview_title = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                           WS_CHILD | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | WS_TABSTOP,
                                           0, 0, 100, 24, hwnd, (HMENU)ID_EDIT_PREVIEW_TITLE, hinst, NULL);
    if (g_edit_preview_title) {
        SendMessage(g_edit_preview_title, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);
        SetWindowLongPtrA(g_edit_preview_title, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    }

    g_edit_map_skullrot = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0.0",
                                          WS_CHILD | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | ES_CENTER | WS_TABSTOP,
                                          0, 0, 60, 20, hwnd, (HMENU)ID_EDIT_MAP_SKULLROT, hinst, NULL);
    if (g_edit_map_skullrot) {
        SendMessage(g_edit_map_skullrot, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);
        SetWindowLongPtrA(g_edit_map_skullrot, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    }

    g_edit_dev_curvecolor = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0x000000",
                                            WS_CHILD | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | ES_CENTER | WS_TABSTOP,
                                            0, 0, 72, 20, hwnd, (HMENU)ID_EDIT_DEV_CURVECOLOR, hinst, NULL);
    if (g_edit_dev_curvecolor) {
        SendMessage(g_edit_dev_curvecolor, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);
        SetWindowLongPtrA(g_edit_dev_curvecolor, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    }

    g_edit_dev_bgcolor = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0x000000",
                                         WS_CHILD | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | ES_CENTER | WS_TABSTOP,
                                         0, 0, 72, 20, hwnd, (HMENU)ID_EDIT_DEV_BGCOLOR, hinst, NULL);
    if (g_edit_dev_bgcolor) {
        SendMessage(g_edit_dev_bgcolor, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);
        SetWindowLongPtrA(g_edit_dev_bgcolor, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    }

    g_edit_dev_huerot = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0.0",
                                        WS_CHILD | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | ES_CENTER | WS_TABSTOP,
                                        0, 0, 60, 20, hwnd, (HMENU)ID_EDIT_DEV_HUEROT, hinst, NULL);
    if (g_edit_dev_huerot) {
        SendMessage(g_edit_dev_huerot, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);
        SetWindowLongPtrA(g_edit_dev_huerot, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    }

    g_edit_dev_treasurefreq = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0",
                                              WS_CHILD | WS_CLIPSIBLINGS | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                                              0, 0, 60, 20, hwnd, (HMENU)ID_EDIT_DEV_TREASUREFREQ, hinst, NULL);
    if (g_edit_dev_treasurefreq) {
        SendMessage(g_edit_dev_treasurefreq, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);
        SetWindowLongPtrA(g_edit_dev_treasurefreq, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    }

    g_edit_dev_partime = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0",
                                         WS_CHILD | WS_CLIPSIBLINGS | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                                         0, 0, 60, 20, hwnd, (HMENU)ID_EDIT_DEV_PARTIME, hinst, NULL);
    if (g_edit_dev_partime) {
        SendMessage(g_edit_dev_partime, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);
        SetWindowLongPtrA(g_edit_dev_partime, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    }
}

static void apply_theme(HWND hwnd, AppTheme new_theme) {
    g_current_theme = new_theme;
    save_zuma_config();

    HMENU hmenu = GetMenu(hwnd);
    if (hmenu) {
        CheckMenuItem(hmenu, ID_MENU_THEME_LIGHT, (g_current_theme == THEME_LIGHT) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hmenu, ID_MENU_THEME_DARK,  (g_current_theme == THEME_DARK)  ? MF_CHECKED : MF_UNCHECKED);
    }

    update_status_bar();
    if (g_hwnd_status) {
        apply_control_dark_mode(g_hwnd_status, g_current_theme == THEME_DARK);
        InvalidateRect(g_hwnd_status, NULL, TRUE);
    }

    refresh_toolbar_buttons();
    if (g_hwnd_toolbar_right) { InvalidateRect(g_hwnd_toolbar_right, NULL, TRUE); UpdateWindow(g_hwnd_toolbar_right); }
    if (g_btn_frog)        { InvalidateRect(g_btn_frog, NULL, TRUE);        UpdateWindow(g_btn_frog); }
    if (g_btn_coin)        { InvalidateRect(g_btn_coin, NULL, TRUE);        UpdateWindow(g_btn_coin); }
    if (g_edit_cv_w)       { RedrawWindow(g_edit_cv_w, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE); }
    if (g_edit_cv_h)       { RedrawWindow(g_edit_cv_h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE); }
    if (g_edit_prio)       { RedrawWindow(g_edit_prio, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE); }
    if (g_edit_preview_title) { RedrawWindow(g_edit_preview_title, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE); }
    if (g_edit_map_skullrot)  { RedrawWindow(g_edit_map_skullrot, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE); }
    if (g_edit_dev_curvecolor){ RedrawWindow(g_edit_dev_curvecolor, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE); }
    if (g_edit_dev_bgcolor)   { RedrawWindow(g_edit_dev_bgcolor, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE); }
    if (g_edit_dev_huerot)    { RedrawWindow(g_edit_dev_huerot, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE); }
    if (g_edit_dev_treasurefreq) { RedrawWindow(g_edit_dev_treasurefreq, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE); }
    if (g_edit_dev_partime)   { RedrawWindow(g_edit_dev_partime, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE); }

    update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);

    HWND hMenuWnd = NULL;
    while ((hMenuWnd = FindWindowExA(NULL, hMenuWnd, "#32768", NULL)) != NULL) {
        apply_menu_dark_mode_to_hwnd(hMenuWnd);
        InvalidateRect(hMenuWnd, NULL, TRUE);
    }

    if (g_active_modal_dlg && IsWindow(g_active_modal_dlg)) {
        update_window_theme_attributes(g_active_modal_dlg, g_current_theme == THEME_DARK);
        InvalidateRect(g_active_modal_dlg, NULL, TRUE);
    }

    InvalidateRect(hwnd, NULL, FALSE);
    UpdateWindow(hwnd);
}

/* ========================================================================= */
/* Custom Owner-Drawn Menu Typography & Styling System                       */
/* - Left: Atkinson Hyperlegible (g_font_primary_regular / bold)             */
/* - Right: JetBrains Mono (g_font_mono_regular)                             */
/* - Full support for Dark & Light Themes with zero white flashes             */
/* ========================================================================= */

#define MAX_STATIC_MENU_ITEMS 128
static MenuItemVisual s_static_menu_items[MAX_STATIC_MENU_ITEMS];
static int s_static_menu_item_count = 0;

static MenuItemVisual* alloc_static_menu_visual(const char *raw_text, UINT id, HMENU hsub, bool is_sep) {
    if (s_static_menu_item_count >= MAX_STATIC_MENU_ITEMS) return NULL;
    MenuItemVisual *v = &s_static_menu_items[s_static_menu_item_count++];
    memset(v, 0, sizeof(*v));
    v->id = id;
    v->hsubmenu = hsub;
    v->is_separator = is_sep;
    v->is_bold = false;
    if (raw_text) {
        const char *tab = strchr(raw_text, '\t');
        if (tab) {
            size_t nlen = (size_t)(tab - raw_text);
            if (nlen >= sizeof(v->name)) nlen = sizeof(v->name) - 1;
            strncpy(v->name, raw_text, nlen);
            v->name[nlen] = '\0';
            strncpy(v->shortcut, tab + 1, sizeof(v->shortcut) - 1);
        } else {
            strncpy(v->name, raw_text, sizeof(v->name) - 1);
            v->shortcut[0] = '\0';
        }
    }
    return v;
}

static void add_custom_menu_item(HMENU hmenu, UINT flags, UINT_PTR id_or_sub, const char *raw_text) {
    bool is_sep = (flags & MF_SEPARATOR) != 0;
    bool is_sub = (flags & MF_POPUP) != 0;
    MenuItemVisual *v = alloc_static_menu_visual(raw_text, is_sub ? 0 : (UINT)id_or_sub, is_sub ? (HMENU)id_or_sub : NULL, is_sep);
    UINT final_flags = (flags & ~(MF_STRING)) | MF_OWNERDRAW;
    AppendMenuA(hmenu, final_flags, id_or_sub, (LPCSTR)v);
}

static void strip_menu_mnemonics(const char *src, char *dst, size_t dst_sz) {
    if (!src || !dst || dst_sz == 0) return;
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 1 < dst_sz; ++si) {
        if (src[si] == '&') {
            if (src[si + 1] == '&') {
                dst[di++] = '&';
                si++;
            }
            /* Skip single '&' mnemonic */
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

/* Forward declarations for popup menu theming */
static LRESULT CALLBACK MenuSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void apply_menu_dark_mode_to_hwnd(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;

    bool is_dark = (g_current_theme == THEME_DARK);
    BOOL dwm_dark = is_dark ? TRUE : FALSE;

    /* 1. DWM Immersive Dark Mode */
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dwm_dark, sizeof(dwm_dark));
    DwmSetWindowAttribute(hwnd, 19 /* DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 */, &dwm_dark, sizeof(dwm_dark));

    /* 2. DWM Border Color (Windows 11): RGB(55, 65, 82) in dark mode, RGB(180, 190, 205) in light mode */
    COLORREF border_col = is_dark ? RGB(55, 65, 82) : RGB(180, 190, 205);
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &border_col, sizeof(border_col));

    /* 3. Small clean rounded corners */
    DWORD corner_pref = 3; /* DWMWCP_ROUNDSMALL */
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner_pref, sizeof(corner_pref));

    /* 4. UxTheme: Allow dark mode for the popup menu window */
    HMODULE hUxTheme = GetModuleHandleA("uxtheme.dll");
    if (!hUxTheme) hUxTheme = LoadLibraryA("uxtheme.dll");
    if (hUxTheme) {
        typedef BOOL (WINAPI *pfnAllowDarkModeForWindow)(HWND, BOOL);
        pfnAllowDarkModeForWindow pAllowDarkModeForWindow = (pfnAllowDarkModeForWindow)(void*)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));
        if (pAllowDarkModeForWindow) {
            pAllowDarkModeForWindow(hwnd, dwm_dark);
        }
    }

    /* 5. Subclass window procedure to intercept WM_NCPAINT, WM_PRINT, and WM_ERASEBKGND */
    WNDPROC old_proc = (WNDPROC)GetWindowLongPtrA(hwnd, GWLP_WNDPROC);
    if (old_proc && old_proc != MenuSubclassProc) {
        SetPropA(hwnd, "ZuPMenuOrigProc", (HANDLE)old_proc);
        SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)MenuSubclassProc);
    }
}

/* Subclass procedure for Win32 popup menu window (#32768) */
static LRESULT CALLBACK MenuSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WNDPROC origProc = (WNDPROC)GetPropA(hwnd, "ZuPMenuOrigProc");
    if (!origProc) return DefWindowProcA(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_NCPAINT: {
            HDC hdc = GetWindowDC(hwnd);
            if (hdc) {
                RECT rc;
                GetWindowRect(hwnd, &rc);
                int w = rc.right - rc.left;
                int h = rc.bottom - rc.top;

                bool is_dark = (g_current_theme == THEME_DARK);
                COLORREF border_col = is_dark ? RGB(55, 65, 82) : RGB(180, 190, 205);
                COLORREF bg_col = is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250);

                /* 1. Outer crisp border (1px) */
                RECT rc_outer = { 0, 0, w, h };
                HBRUSH br_border = CreateSolidBrush(border_col);
                FrameRect(hdc, &rc_outer, br_border);
                DeleteObject(br_border);

                /* 2. Fill inner margin with theme background to eliminate any white bleed */
                HBRUSH br_inner = CreateSolidBrush(bg_col);
                for (int inset = 1; inset <= 3; ++inset) {
                    RECT rc_in = { inset, inset, w - inset, h - inset };
                    FrameRect(hdc, &rc_in, br_inner);
                }
                DeleteObject(br_inner);

                ReleaseDC(hwnd, hdc);
            }
            return 0; /* Fully intercept and block native white border */
        }

        case WM_PRINT: {
            LRESULT res = CallWindowProcA(origProc, hwnd, msg, wParam, lParam);
            if (lParam & PRF_NONCLIENT) {
                HDC hdc = (HDC)wParam;
                if (hdc) {
                    RECT rc;
                    GetWindowRect(hwnd, &rc);
                    int w = rc.right - rc.left;
                    int h = rc.bottom - rc.top;

                    bool is_dark = (g_current_theme == THEME_DARK);
                    COLORREF border_col = is_dark ? RGB(55, 65, 82) : RGB(180, 190, 205);
                    COLORREF bg_col = is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250);

                    RECT rc_outer = { 0, 0, w, h };
                    HBRUSH br_border = CreateSolidBrush(border_col);
                    FrameRect(hdc, &rc_outer, br_border);
                    DeleteObject(br_border);

                    HBRUSH br_inner = CreateSolidBrush(bg_col);
                    for (int inset = 1; inset <= 3; ++inset) {
                        RECT rc_in = { inset, inset, w - inset, h - inset };
                        FrameRect(hdc, &rc_in, br_inner);
                    }
                    DeleteObject(br_inner);
                }
            }
            return res;
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            if (hdc) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                bool is_dark = (g_current_theme == THEME_DARK);
                HBRUSH br = CreateSolidBrush(is_dark ? RGB(22, 26, 36) : RGB(242, 245, 250));
                FillRect(hdc, &rc, br);
                DeleteObject(br);
                return 1;
            }
            break;
        }

        case WM_NCDESTROY: {
            RemovePropA(hwnd, "ZuPMenuOrigProc");
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)origProc);
            break;
        }
    }
    return CallWindowProcA(origProc, hwnd, msg, wParam, lParam);
}

static HHOOK s_hMenuCbtHook = NULL;

static LRESULT CALLBACK MenuCbtHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HCBT_CREATEWND) {
        HWND hwnd = (HWND)wParam;
        LPCBT_CREATEWNDA cbt = (LPCBT_CREATEWNDA)lParam;
        bool is_menu = false;
        if (cbt && cbt->lpcs && cbt->lpcs->lpszClass) {
            if ((ULONG_PTR)cbt->lpcs->lpszClass <= 0xFFFF) {
                if (LOWORD((ULONG_PTR)cbt->lpcs->lpszClass) == 0x8000) {
                    is_menu = true;
                }
            } else if (strcmp(cbt->lpcs->lpszClass, "#32768") == 0) {
                is_menu = true;
            }
        }
        if (is_menu) {
            apply_menu_dark_mode_to_hwnd(hwnd);
        }
    } else if (nCode == HCBT_ACTIVATE || nCode == HCBT_SETFOCUS) {
        HWND hwnd = (HWND)wParam;
        char cls_name[32] = {0};
        if (GetClassNameA(hwnd, cls_name, sizeof(cls_name)) > 0) {
            if (strcmp(cls_name, "#32768") == 0) {
                apply_menu_dark_mode_to_hwnd(hwnd);
            }
        }
    }
    return CallNextHookEx(s_hMenuCbtHook, nCode, wParam, lParam);
}

static void measure_custom_menu_item(LPMEASUREITEMSTRUCT mis, HWND hwnd) {
    if (!mis || mis->CtlType != ODT_MENU) return;
    MenuItemVisual *v = (MenuItemVisual*)mis->itemData;
    if (!v) return;

    if (v->is_separator) {
        mis->itemWidth = 50;
        mis->itemHeight = 5; /* Compact 5px separator */
        return;
    }

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        mis->itemWidth = 160;
        mis->itemHeight = 20;
        return;
    }

    HFONT font_to_use = (v->is_bold && g_font_primary_bold) ? g_font_primary_bold : g_font_primary_regular;
    HFONT old_f = (HFONT)SelectObject(hdc, font_to_use);

    char clean_name[128];
    strip_menu_mnemonics(v->name, clean_name, sizeof(clean_name));

    SIZE sz_name = { 0, 0 };
    GetTextExtentPoint32A(hdc, clean_name, (int)strlen(clean_name), &sz_name);

    SIZE sz_sc = { 0, 0 };
    if (v->shortcut[0]) {
        SelectObject(hdc, g_font_mono_regular ? g_font_mono_regular : font_to_use);
        GetTextExtentPoint32A(hdc, v->shortcut, (int)strlen(v->shortcut), &sz_sc);
    }

    SelectObject(hdc, old_f);
    ReleaseDC(hwnd, hdc);

    int padding_left = 22; /* Compact checkmark column */
    int gap = v->shortcut[0] ? 20 : 8;
    int padding_right = v->hsubmenu ? 18 : 10;

    mis->itemWidth = padding_left + sz_name.cx + gap + sz_sc.cx + padding_right;
    mis->itemHeight = 20; /* Compact 20px item row height for tighter spacing */
}

static void draw_custom_menu_item(LPDRAWITEMSTRUCT dis) {
    if (!dis || dis->CtlType != ODT_MENU) return;
    MenuItemVisual *v = (MenuItemVisual*)dis->itemData;
    if (!v) return;

    const ThemeColors *th = get_theme();
    bool is_dark = (g_current_theme == THEME_DARK);
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    bool is_selected = (dis->itemState & ODS_SELECTED) != 0;
    bool is_disabled = (dis->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
    bool is_checked  = (dis->itemState & ODS_CHECKED) != 0;
    bool is_bold     = v->is_bold || is_checked;

    if (v->is_separator) {
        HBRUSH bg_br = CreateSolidBrush(th->bar_bg);
        FillRect(hdc, &rc, bg_br);
        DeleteObject(bg_br);

        HPEN pen = CreatePen(PS_SOLID, 1, th->bar_border);
        HPEN old_pen = (HPEN)SelectObject(hdc, pen);
        int mid_y = rc.top + (rc.bottom - rc.top) / 2;
        MoveToEx(hdc, rc.left + 20, mid_y, NULL);
        LineTo(hdc, rc.right - 6, mid_y);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
        return;
    }

    /* 1. Item Background */
    COLORREF bg_color = th->bar_bg;
    if (is_selected && !is_disabled) {
        bg_color = is_dark ? RGB(38, 54, 82) : RGB(220, 232, 250);
    }
    HBRUSH bg_br = CreateSolidBrush(bg_color);
    FillRect(hdc, &rc, bg_br);
    DeleteObject(bg_br);

    /* 2. Selection Border */
    if (is_selected && !is_disabled) {
        HPEN pen = CreatePen(PS_SOLID, 1, is_dark ? RGB(70, 110, 180) : RGB(140, 180, 235));
        HPEN old_pen = (HPEN)SelectObject(hdc, pen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left + 2, rc.top + 1, rc.right - 2, rc.bottom);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    /* 3. Checkmark */
    if (is_checked) {
        COLORREF chk_col = is_disabled ? th->text_dim : (is_dark ? RGB(90, 205, 255) : RGB(0, 102, 204));
        int chk_cx = rc.left + 11;
        int chk_cy = rc.top + (rc.bottom - rc.top) / 2;
        HPEN chk_pen = CreatePen(PS_SOLID, 2, chk_col);
        HPEN old_cp = (HPEN)SelectObject(hdc, chk_pen);
        MoveToEx(hdc, chk_cx - 4, chk_cy, NULL);
        LineTo(hdc, chk_cx - 1, chk_cy + 3);
        LineTo(hdc, chk_cx + 4, chk_cy - 4);
        SelectObject(hdc, old_cp);
        DeleteObject(chk_pen);
    }

    /* 4. Submenu Indicator Arrow (Consistent Theme Opacity) */
    if (v->hsubmenu != NULL) {
        COLORREF arrow_col;
        if (is_dark) {
            arrow_col = is_selected ? RGB(255, 255, 255) : RGB(138, 140, 148); /* 100% white on hover, 50% opacity on non-hover */
        } else {
            arrow_col = is_selected ? RGB(0, 0, 0) : RGB(135, 140, 150);       /* 100% black on hover, 50% opacity on non-hover */
        }

        int mid_y = rc.top + (rc.bottom - rc.top) / 2;
        int arr_tip_x = rc.right - 8;
        int arr_base_x = arr_tip_x - 4;
        POINT pts[3] = {
            { arr_base_x, mid_y - 4 },
            { arr_tip_x,  mid_y },
            { arr_base_x, mid_y + 4 }
        };

        HBRUSH arr_br = CreateSolidBrush(arrow_col);
        HPEN arr_pen = CreatePen(PS_SOLID, 1, arrow_col);
        HBRUSH old_br = (HBRUSH)SelectObject(hdc, arr_br);
        HPEN old_p = (HPEN)SelectObject(hdc, arr_pen);
        Polygon(hdc, pts, 3);
        SelectObject(hdc, old_br);
        SelectObject(hdc, old_p);
        DeleteObject(arr_br);
        DeleteObject(arr_pen);
    }

    SetBkMode(hdc, TRANSPARENT);

    /* 5. Item Label (Atkinson Hyperlegible) */
    HFONT font_name = (is_bold && g_font_primary_bold) ? g_font_primary_bold : g_font_primary_regular;
    HFONT old_f = (HFONT)SelectObject(hdc, font_name);

    COLORREF col_name;
    if (is_disabled) {
        col_name = th->text_dim;
    } else if (is_selected) {
        col_name = is_dark ? RGB(255, 255, 255) : RGB(10, 20, 40);
    } else if (is_checked) {
        col_name = is_dark ? RGB(100, 215, 255) : RGB(0, 102, 204);
    } else {
        col_name = th->text_primary;
    }
    SetTextColor(hdc, col_name);

    RECT rc_name = rc;
    rc_name.left += 22;
    rc_name.right -= (v->shortcut[0] ? 75 : (v->hsubmenu ? 18 : 10));
    DrawTextA(hdc, v->name, -1, &rc_name, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    /* 6. Item Shortcut (JetBrains Mono) */
    if (v->shortcut[0]) {
        SelectObject(hdc, g_font_mono_regular ? g_font_mono_regular : font_name);
        COLORREF col_sc;
        if (is_disabled) {
            col_sc = th->text_dim;
        } else if (is_selected) {
            col_sc = is_dark ? RGB(180, 205, 240) : RGB(40, 80, 140);
        } else {
            col_sc = th->text_secondary;
        }
        SetTextColor(hdc, col_sc);

        RECT rc_sc = rc;
        rc_sc.right -= (v->hsubmenu ? 18 : 10);
        DrawTextA(hdc, v->shortcut, -1, &rc_sc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    }

    SelectObject(hdc, old_f);

    /* 7. Suppress native Windows User32 default submenu arrow to prevent duplicate/stacked arrows */
    if (v->hsubmenu != NULL) {
        ExcludeClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
    }
}

static LRESULT handle_custom_menuchar(WPARAM wParam, LPARAM lParam) {
    char ch = (char)LOWORD(wParam);
    HMENU hmenu = (HMENU)lParam;
    if (!hmenu) return 0;
    int count = GetMenuItemCount(hmenu);
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOA mii;
        memset(&mii, 0, sizeof(mii));
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_DATA | MIIM_FTYPE;
        if (GetMenuItemInfoA(hmenu, i, TRUE, &mii) && (mii.fType & MFT_OWNERDRAW)) {
            MenuItemVisual *v = (MenuItemVisual*)mii.dwItemData;
            if (v && !v->is_separator) {
                const char *p = v->name;
                while (*p) {
                    if (*p == '&' && *(p + 1) != '&') {
                        char m = *(p + 1);
                        if (tolower((unsigned char)m) == tolower((unsigned char)ch)) {
                            return MAKELRESULT(i, MNC_EXECUTE);
                        }
                    }
                    p++;
                }
            }
        }
    }
    return 0;
}

/* Create Classic Windows Menu Bar */
static void create_main_menu(HWND hwnd) {
    s_static_menu_item_count = 0;
    HMENU hmenu = CreateMenu();

    HMENU hfile = CreatePopupMenu();
    add_custom_menu_item(hfile, MF_STRING, ID_MENU_FILE_NEW,  "&New Canvas...\tCtrl+N");
    add_custom_menu_item(hfile, MF_STRING, ID_MENU_FILE_OPEN, "&Open Canvas...\tCtrl+O");
    add_custom_menu_item(hfile, MF_STRING, ID_MENU_FILE_SAVE, "&Save Canvas...\tCtrl+S");
    add_custom_menu_item(hfile, MF_SEPARATOR, 0, NULL);
    add_custom_menu_item(hfile, MF_STRING, ID_MENU_FILE_MANAGE_LEVELS, "&Map Manager...\tCtrl+Shift+M");
    add_custom_menu_item(hfile, MF_SEPARATOR, 0, NULL);
    add_custom_menu_item(hfile, MF_STRING, ID_MENU_FILE_SPLASH, "&Splash Screen...");
    add_custom_menu_item(hfile, MF_STRING, ID_MENU_FILE_EXIT, "E&xit\tAlt+F4");
    AppendMenuA(hmenu, MF_POPUP, (UINT_PTR)hfile, "&File");

    HMENU hview = CreatePopupMenu();
    add_custom_menu_item(hview, MF_STRING, ID_MENU_VIEW_ZOOMIN, "Zoom &In\tCtrl++");
    add_custom_menu_item(hview, MF_STRING, ID_MENU_VIEW_ZOOMOUT, "Zoom &Out\tCtrl+-");
    add_custom_menu_item(hview, MF_STRING, ID_MENU_VIEW_100, "Zoom to &100%\tCtrl+1");
    add_custom_menu_item(hview, MF_STRING, ID_MENU_VIEW_RESET, "&Center Canvas\tCtrl+R");
    add_custom_menu_item(hview, MF_STRING, ID_MENU_VIEW_FIT, "Zoom to &Fit\tCtrl+0");
    add_custom_menu_item(hview, MF_SEPARATOR, 0, NULL);
    add_custom_menu_item(hview, MF_STRING | (g_show_grid ? MF_CHECKED : MF_UNCHECKED), ID_MENU_VIEW_GRID, "Show &Grid\tCtrl+G");
    add_custom_menu_item(hview, MF_SEPARATOR, 0, NULL);

    HMENU htheme = CreatePopupMenu();
    add_custom_menu_item(htheme, MF_STRING | (g_current_theme == THEME_LIGHT ? MF_CHECKED : MF_UNCHECKED), ID_MENU_THEME_LIGHT, "&Light Mode");
    add_custom_menu_item(htheme, MF_STRING | (g_current_theme == THEME_DARK  ? MF_CHECKED : MF_UNCHECKED), ID_MENU_THEME_DARK,  "&Dark Mode");
    add_custom_menu_item(htheme, MF_SEPARATOR, 0, NULL);
    add_custom_menu_item(htheme, MF_STRING, ID_MENU_THEME_TOGGLE, "&Toggle Theme\tCtrl+T");
    add_custom_menu_item(hview, MF_POPUP, (UINT_PTR)htheme, "&Theme");

    AppendMenuA(hmenu, MF_POPUP, (UINT_PTR)hview, "&View");

    /* Action History Submenu */
    g_hmenu_action_history = CreatePopupMenu();
    undo_redo_populate_history_menu(g_hmenu_action_history);

    /* Edit Menu */
    HMENU hcanvas = CreatePopupMenu();
    add_custom_menu_item(hcanvas, MF_POPUP, (UINT_PTR)g_hmenu_action_history, "Action &History");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_EDIT_UNDO, "&Undo\tCtrl+Z");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_EDIT_REDO, "&Redo\tCtrl+Y");
    add_custom_menu_item(hcanvas, MF_SEPARATOR, 0, NULL);

    /* 4 Level Authoring Modes: Canvas, Element, Curve, and Overlay (Satu Rumpun & Setara) */
    add_custom_menu_item(hcanvas, MF_STRING | (g_mode_canvas ? MF_CHECKED : MF_UNCHECKED), ID_MENU_CANVAS_MODE, "Canvas &Mode...\tCtrl+M");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_CANVAS_CHANGE_IMG, "   &Add/Change Background...\tCtrl+B");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_CANVAS_REMOVE_IMG, "   &Remove Background\tCtrl+Alt+B");
    add_custom_menu_item(hcanvas, MF_STRING | (g_canvas_w == 640 && g_canvas_h == 480 ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_CANVAS_640_480, "   &640 x 480 (Classic Zuma)");
    add_custom_menu_item(hcanvas, MF_STRING | (g_canvas_w == 800 && g_canvas_h == 600 ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_CANVAS_800_600, "   &800 x 600 (Zuma Compleluxe)");
    add_custom_menu_item(hcanvas, MF_STRING | (g_canvas_w == 1024 && g_canvas_h == 768 ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_CANVAS_1024_768, "   &1024 x 768 (XGA)");
    add_custom_menu_item(hcanvas, MF_STRING | (g_canvas_w == 1280 && g_canvas_h == 720 ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_CANVAS_1280_720, "   &1280 x 720 (HD 720p)");

    add_custom_menu_item(hcanvas, MF_STRING | (g_mode_element ? MF_CHECKED : MF_UNCHECKED), ID_MENU_CANVAS_ELEMENT, "&Element Mode...\tCtrl+E");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_CANVAS_SET_FROG,   "   Set &Frog Position...\tCtrl+P");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_CANVAS_ADD_COIN,   "   Add &Coin (Treasure Point)...\tCtrl+K");

    add_custom_menu_item(hcanvas, MF_STRING | (g_mode_curve ? MF_CHECKED : MF_UNCHECKED), ID_MENU_CANVAS_CURVE, "&Curve Pen Tool...\tCtrl+U");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_CURVE_IMPORT_SVG, "   &Import Path...\tCtrl+Shift+I");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_CURVE_EXPORT_SVG, "   &Export Path...\tCtrl+Shift+E");

    add_custom_menu_item(hcanvas, MF_STRING | (g_mode_overlay ? MF_CHECKED : MF_UNCHECKED), ID_MENU_CANVAS_OVERLAY, "&Overlay Mode...\tCtrl+Shift+O");
    add_custom_menu_item(hcanvas, MF_STRING, ID_OVERLAY_ADD_BGALPHA, "   Add &Background Alpha...\tCtrl+Shift+B");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_CANVAS_ADD_SPRITE, "   Add &Sprite Decoration...\tCtrl+Shift+S");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_CANVAS_ADD_CUTOUT, "   Add &Cutout Overlay...\tCtrl+Shift+C");
    add_custom_menu_item(hcanvas, MF_STRING, ID_OVERLAY_SET_VARIABLE, "   Set &Variable (Motion / Priority)...\tCtrl+J");

    add_custom_menu_item(hcanvas, MF_SEPARATOR, 0, NULL);
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_SELECT_ALL,   "Select &All\tCtrl+A");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_INVERT_SEL,   "&Invert Selection\tCtrl+I");
    add_custom_menu_item(hcanvas, MF_STRING, ID_MENU_DESELECT_ALL, "&Deselect All\tCtrl+D");
    AppendMenuA(hmenu, MF_POPUP, (UINT_PTR)hcanvas, "&Edit");

    SetMenu(hwnd, hmenu);
}

/* Main Application Window Procedure */
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd_main = hwnd;
            load_zuma_config();
            init_fonts();
            image_subsystem_init();
            undo_redo_init();
            load_frog_sprite();
            create_main_menu(hwnd);
            create_toolbar_buttons(hwnd);

            InitCommonControls();
            g_hwnd_status = CreateWindowExA(
                0, STATUSCLASSNAME, NULL,
                WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0,
                hwnd, (HMENU)ID_STATUSBAR, GetModuleHandle(NULL), NULL
            );
            if (g_hwnd_status) {
                g_orig_status_proc = (WNDPROC)SetWindowLongPtrA(
                    g_hwnd_status, GWLP_WNDPROC, (LONG_PTR)StatusBarSubclassProc
                );
            }
            apply_control_dark_mode(g_hwnd_status, g_current_theme == THEME_DARK);

            update_status_bar();
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);
            if (g_current_theme == THEME_DARK) {
                apply_theme(hwnd, THEME_DARK);
            }
            s_hMenuCbtHook = SetWindowsHookExA(WH_CBT, MenuCbtHookProc, NULL, GetCurrentThreadId());
            SetTimer(hwnd, IDT_MEDIA_ANIMATION, 15, NULL);
            return 0;
        }

        case WM_TIMER: {
            if (wParam == IDT_MEDIA_ANIMATION) {
                bool redraw = false;
                static DWORD s_last_preview_tick = 0;
                if (g_level_preview_active) {
                    DWORD now = timeGetTime();
                    if (s_last_preview_tick == 0) s_last_preview_tick = now;
                    DWORD dt = now - s_last_preview_tick;
                    if (dt > 100) dt = 100;
                    s_last_preview_tick = now;
                    g_preview_anim_distance += (float)dt * 0.12f;
                    redraw = true;
                } else {
                    s_last_preview_tick = 0;
                }
                if (g_has_bg_image && g_canvas_img.is_animated) {
                    if (image_advance_animation(&g_canvas_img)) redraw = true;
                }
                for (int i = 0; i < g_cutout_count; ++i) {
                    if (g_cutouts[i].has_img && g_cutouts[i].img.is_animated) {
                        if (image_advance_animation(&g_cutouts[i].img)) {
                            cutout_generate_masks(&g_cutouts[i]);
                            redraw = true;
                        }
                    }
                }
                for (int i = 0; i < g_sprite_count; ++i) {
                    if (g_sprites[i].has_img && g_sprites[i].img.is_animated) {
                        if (image_advance_animation(&g_sprites[i].img)) redraw = true;
                    }
                }
                for (int i = 0; i < g_bg_alpha_count; ++i) {
                    if (g_bg_alphas[i].has_img && g_bg_alphas[i].img.is_animated) {
                        if (image_advance_animation(&g_bg_alphas[i].img)) {
                            bgalpha_generate_masks(&g_bg_alphas[i]);
                            redraw = true;
                        }
                    }
                }
                if (redraw) {
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_UAHDRAWMENU: {
            UAHMENU *pUDMenu = (UAHMENU *)lParam;
            RECT rcWindow;
            GetWindowRect(hwnd, &rcWindow);
            MENUBARINFO mbi;
            memset(&mbi, 0, sizeof(mbi));
            mbi.cbSize = sizeof(mbi);
            GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi);
            RECT rcBar = mbi.rcBar;
            OffsetRect(&rcBar, -rcWindow.left, -rcWindow.top);

            const ThemeColors *th = get_theme();
            HBRUSH br = CreateSolidBrush(th->bar_bg);
            FillRect(pUDMenu->hdc, &rcBar, br);
            DeleteObject(br);
            return 0;
        }

        case WM_UAHDRAWMENUITEM: {
            UAHDRAWMENUITEM *pUDMI = (UAHDRAWMENUITEM *)lParam;
            if (pUDMI) {
                char text[128] = {0};
                MENUITEMINFOA mii;
                memset(&mii, 0, sizeof(mii));
                mii.cbSize = sizeof(mii);
                mii.fMask = MIIM_STRING;
                mii.dwTypeData = text;
                mii.cch = sizeof(text);
                GetMenuItemInfoA(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);

                bool is_hot = (pUDMI->dis.itemState & ODS_HOTLIGHT) != 0;
                bool is_selected = (pUDMI->dis.itemState & ODS_SELECTED) != 0;
                const ThemeColors *th = get_theme();
                COLORREF bg_col;
                COLORREF text_col;
                if (g_current_theme == THEME_DARK) {
                    if (is_selected)      bg_col = RGB(55, 62, 76);
                    else if (is_hot)      bg_col = RGB(45, 50, 60);
                    else                  bg_col = th->bar_bg;

                    text_col = (is_hot || is_selected) ? th->text_accent_blue : th->text_primary;
                } else {
                    if (is_selected)      bg_col = RGB(200, 210, 225);
                    else if (is_hot)      bg_col = RGB(218, 222, 230);
                    else                  bg_col = th->bar_bg;

                    text_col = (is_hot || is_selected) ? th->text_accent_blue : th->text_primary;
                }

                HBRUSH br = CreateSolidBrush(bg_col);
                FillRect(pUDMI->um.hdc, &pUDMI->dis.rcItem, br);
                DeleteObject(br);

                SetBkMode(pUDMI->um.hdc, TRANSPARENT);
                SetTextColor(pUDMI->um.hdc, text_col);
                SelectObject(pUDMI->um.hdc, g_font_primary_regular ? g_font_primary_regular : GetStockObject(DEFAULT_GUI_FONT));
                DWORD draw_flags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
                if (pUDMI->dis.itemState & ODS_NOACCEL) {
                    draw_flags |= DT_HIDEPREFIX;
                }
                DrawTextA(pUDMI->um.hdc, text, -1, &pUDMI->dis.rcItem, draw_flags);
                return 0;
            }
            break;
        }

        case WM_UAHMEASUREMENUITEM: {
            UAHMEASUREMENUITEM *pMmi = (UAHMEASUREMENUITEM *)lParam;
            LRESULT lr = DefWindowProcA(hwnd, msg, wParam, lParam);
            if (pMmi) {
                pMmi->mis.itemWidth = (pMmi->mis.itemWidth * 5) / 4;
            }
            return lr;
        }

        case WM_NCPAINT:
        case WM_NCACTIVATE: {
            LRESULT lr = DefWindowProcA(hwnd, msg, wParam, lParam);
            const ThemeColors *th = get_theme();
            draw_menu_nc_bottom_line(hwnd, th->bar_bg);
            return lr;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE: {
            RECT rc_client;
            GetClientRect(hwnd, &rc_client);
            int client_w = rc_client.right - rc_client.left;

            if (g_hwnd_toolbar_left) {
                MoveWindow(g_hwnd_toolbar_left, 0, 0, client_w, TOOLBAR_HEIGHT, TRUE);
                update_toolbar_buttons_layout();
            }

            if (g_hwnd_status) {
                SendMessage(g_hwnd_status, WM_SIZE, 0, 0);

                int parts[4];
                parts[0] = client_w - 285;
                parts[1] = parts[0] + 95;
                parts[2] = parts[1] + 95;
                parts[3] = -1;
                SendMessageA(g_hwnd_status, SB_SETPARTS, 4, (LPARAM)parts);
                update_status_bar();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MEASUREITEM: {
            LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lParam;
            if (mis && mis->CtlType == ODT_MENU) {
                measure_custom_menu_item(mis, hwnd);
                return TRUE;
            }
            break;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlID == ID_STATUSBAR) {
                return TRUE;
            }
            if (dis->CtlType == ODT_MENU) {
                draw_custom_menu_item(dis);
                return TRUE;
            }
            if (dis->CtlType == ODT_BUTTON) {
                draw_custom_button(dis);
                return TRUE;
            }
            break;
        }

        case WM_MENUCHAR: {
            LRESULT res = handle_custom_menuchar(wParam, lParam);
            if (res != 0) return res;
            break;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdc_edit = (HDC)wParam;
            HWND hwnd_edit = (HWND)lParam;
            if (hwnd_edit == g_edit_w || hwnd_edit == g_edit_h || hwnd_edit == g_edit_prio ||
                hwnd_edit == g_edit_cv_w || hwnd_edit == g_edit_cv_h || hwnd_edit == g_edit_preview_title ||
                hwnd_edit == g_edit_map_skullrot || hwnd_edit == g_edit_dev_curvecolor ||
                hwnd_edit == g_edit_dev_bgcolor || hwnd_edit == g_edit_dev_huerot ||
                hwnd_edit == g_edit_dev_treasurefreq || hwnd_edit == g_edit_dev_partime) {
                const ThemeColors *th = get_theme();
                COLORREF bg_col = (g_current_theme == THEME_DARK) ? RGB(36, 40, 48) : RGB(255, 255, 255);
                COLORREF text_col = (g_current_theme == THEME_DARK) ? RGB(225, 230, 240) : RGB(20, 24, 30);
                SetTextColor(hdc_edit, text_col);
                SetBkColor(hdc_edit, bg_col);
                static HBRUSH s_edit_brush_light = NULL;
                static HBRUSH s_edit_brush_dark = NULL;
                if (!s_edit_brush_light) s_edit_brush_light = CreateSolidBrush(RGB(255, 255, 255));
                if (!s_edit_brush_dark)  s_edit_brush_dark  = CreateSolidBrush(RGB(36, 40, 48));
                return (LRESULT)((g_current_theme == THEME_DARK) ? s_edit_brush_dark : s_edit_brush_light);
            }
            break;
        }

        case WM_ENTERMENULOOP:
        case WM_INITMENUPOPUP: {
            if (msg == WM_INITMENUPOPUP) {
                undo_redo_update_menu(hwnd);
            }
            HWND hMenuWnd = NULL;
            while ((hMenuWnd = FindWindowExA(NULL, hMenuWnd, "#32768", NULL)) != NULL) {
                apply_menu_dark_mode_to_hwnd(hMenuWnd);
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId >= ID_MENU_ACTION_HISTORY_BASE + 1 && wmId <= ID_MENU_ACTION_HISTORY_BASE + MAX_UNDO_STACK) {
                int target_idx = wmId - ID_MENU_ACTION_HISTORY_BASE;
                undo_jump_to_index(target_idx, hwnd);
                break;
            }
            switch (wmId) {
                case ID_MENU_FILE_NEW:
                case ID_MENU_CANVAS_NEW:
                    show_new_canvas_dialog(hwnd);
                    break;

                case ID_MENU_FILE_OPEN:
                case ID_MENU_CANVAS_OPEN:
                    show_open_canvas_dialog(hwnd);
                    break;

                case ID_MENU_FILE_SAVE:
                case ID_MENU_CANVAS_SAVE:
                    show_save_canvas_dialog(hwnd);
                    break;

                case ID_MENU_EDIT_UNDO:
                    undo_perform(hwnd);
                    break;

                case ID_MENU_EDIT_REDO:
                    undo_redo_perform(hwnd);
                    break;

                case ID_BTN_MANAGE_MAPS:
                case ID_MENU_FILE_MANAGE_LEVELS:
                    show_manage_levels_dialog(hwnd);
                    break;

                case ID_MENU_FILE_SPLASH:
                    show_splash_screen_dialog(hwnd);
                    break;

                case ID_MENU_SET_ZUMA_FOLDER:
                    handle_set_zuma_folder(hwnd);
                    break;

                case ID_MENU_CANVAS_CHANGE_IMG:
                    prompt_change_background_image(hwnd);
                    break;

                case ID_MENU_CANVAS_REMOVE_IMG:
                    if (g_has_bg_image) {
                        image_free(&g_canvas_img);
                        g_has_bg_image = false;
                        undo_snapshot_capture("Remove Background");
                        update_status_bar();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    break;

                case ID_MENU_CANVAS_MODE:
                case ID_BTN_CANVAS: {
                    toggle_canvas_mode(hwnd);
                    break;
                }

                case ID_MENU_CANVAS_ELEMENT:
                case ID_BTN_ELEMENT: {
                    toggle_element_mode(hwnd);
                    break;
                }

                case ID_MENU_CANVAS_SET_FROG:
                case ID_BTN_FROG: {
                    if (!g_has_canvas) {
                        show_new_canvas_dialog(hwnd);
                        break;
                    }
                    if (g_mode_canvas) {
                        g_mode_canvas = false;
                        if (g_btn_canvas) InvalidateRect(g_btn_canvas, NULL, TRUE);
                        if (g_edit_cv_w) ShowWindow(g_edit_cv_w, SW_HIDE);
                        if (g_edit_cv_h) ShowWindow(g_edit_cv_h, SW_HIDE);
                    }
                    if (!g_mode_element) {
                        g_mode_element = true;
                        if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
                        g_hud_element_guide_scroll_y = 0;
                        g_hud_element_tools_scroll_y = 0;
                        if (g_btn_element) InvalidateRect(g_btn_element, NULL, TRUE);
                    }
                    if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
                    if (g_placing_coin) {
                        g_placing_coin = false;
                        if (g_btn_coin) InvalidateRect(g_btn_coin, NULL, TRUE);
                    }
                    if (g_mode_curve) {
                        g_mode_curve = false;
                        if (g_btn_curve) InvalidateRect(g_btn_curve, NULL, TRUE);
                    }
                    if (g_mode_overlay) {
                        g_mode_overlay = false;
                        g_hud_overlay_guide_scroll_y = 0;
                        g_hud_overlay_tools_scroll_y = 0;
                    }
                    if (g_placing_cutout) {
                        g_placing_cutout = false;
                        free_pending_cutout();
                    }
                    if (g_placing_sprite) {
                        g_placing_sprite = false;
                        free_pending_sprite();
                    }
                    if (g_placing_bgalpha) {
                        g_placing_bgalpha = false;
                        g_placing_bgalpha_vector = false;
                        free_pending_bgalpha();
                    }
                    if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
                    load_frog_sprite();
                    g_placing_frog = !g_placing_frog;
                    if (g_placing_frog) {
                        g_frog_hovered = false;
                        POINT pt;
                        GetCursorPos(&pt);
                        ScreenToClient(hwnd, &pt);
                        g_mouse_screen_x = pt.x;
                        g_mouse_screen_y = pt.y;
                    }
                    HMENU hmenu = GetMenu(hwnd);
                    if (hmenu) {
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_MODE, g_mode_canvas ? MF_CHECKED : MF_UNCHECKED);
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_ELEMENT, g_mode_element ? MF_CHECKED : MF_UNCHECKED);
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_CURVE, g_mode_curve ? MF_CHECKED : MF_UNCHECKED);
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_OVERLAY, g_mode_overlay ? MF_CHECKED : MF_UNCHECKED);
                    }
                    if (g_btn_frog) RedrawWindow(g_btn_frog, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
                    refresh_toolbar_buttons();
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateWindow(hwnd);
                    update_status_bar();
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_CANVAS_ADD_COIN:
                case ID_BTN_COIN: {
                    if (!g_has_canvas) {
                        show_new_canvas_dialog(hwnd);
                        break;
                    }
                    if (g_mode_canvas) {
                        g_mode_canvas = false;
                        if (g_edit_cv_w) ShowWindow(g_edit_cv_w, SW_HIDE);
                        if (g_edit_cv_h) ShowWindow(g_edit_cv_h, SW_HIDE);
                    }
                    if (!g_mode_element) {
                        g_mode_element = true;
                        if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
                        g_hud_element_guide_scroll_y = 0;
                        g_hud_element_tools_scroll_y = 0;
                        if (g_btn_element) InvalidateRect(g_btn_element, NULL, TRUE);
                    }
                    if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
                    if (g_placing_frog) {
                        g_placing_frog = false;
                        if (g_btn_frog) InvalidateRect(g_btn_frog, NULL, TRUE);
                    }
                    if (g_mode_curve) {
                        g_mode_curve = false;
                    }
                    if (g_mode_overlay) {
                        g_mode_overlay = false;
                        g_hud_overlay_guide_scroll_y = 0;
                        g_hud_overlay_tools_scroll_y = 0;
                    }
                    if (g_placing_cutout) {
                        g_placing_cutout = false;
                        free_pending_cutout();
                    }
                    if (g_placing_sprite) {
                        g_placing_sprite = false;
                        free_pending_sprite();
                    }
                    if (g_placing_bgalpha) {
                        g_placing_bgalpha = false;
                        g_placing_bgalpha_vector = false;
                        free_pending_bgalpha();
                    }
                    load_coin_sprite();
                    g_placing_coin = !g_placing_coin;
                    if (g_placing_coin) {
                        g_hovered_coin_idx = -1;
                        POINT pt;
                        GetCursorPos(&pt);
                        ScreenToClient(hwnd, &pt);
                        g_mouse_screen_x = pt.x;
                        g_mouse_screen_y = pt.y;
                    }
                    HMENU hmenu = GetMenu(hwnd);
                    if (hmenu) {
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_MODE, g_mode_canvas ? MF_CHECKED : MF_UNCHECKED);
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_ELEMENT, g_mode_element ? MF_CHECKED : MF_UNCHECKED);
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_CURVE, g_mode_curve ? MF_CHECKED : MF_UNCHECKED);
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_OVERLAY, g_mode_overlay ? MF_CHECKED : MF_UNCHECKED);
                    }
                    if (g_btn_coin) RedrawWindow(g_btn_coin, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
                    refresh_toolbar_buttons();
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateWindow(hwnd);
                    update_status_bar();
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_CANVAS_CURVE:
                case ID_BTN_CURVE: {
                    if (!g_has_canvas) {
                        show_new_canvas_dialog(hwnd);
                        break;
                    }
                    if (g_mode_canvas) {
                        g_mode_canvas = false;
                        if (g_edit_cv_w) ShowWindow(g_edit_cv_w, SW_HIDE);
                        if (g_edit_cv_h) ShowWindow(g_edit_cv_h, SW_HIDE);
                    }
                    if (g_mode_element) {
                        g_mode_element = false;
                        g_placing_frog = false;
                        g_placing_coin = false;
                    }
                    if (g_placing_frog) {
                        g_placing_frog = false;
                        if (g_btn_frog) InvalidateRect(g_btn_frog, NULL, TRUE);
                    }
                    if (g_placing_coin) {
                        g_placing_coin = false;
                        if (g_btn_coin) InvalidateRect(g_btn_coin, NULL, TRUE);
                    }
                    if (g_mode_overlay) {
                        g_mode_overlay = false;
                        g_placing_cutout = false;
                        g_placing_sprite = false;
                        g_placing_bgalpha = false;
                        g_placing_bgalpha_vector = false;
                        free_pending_cutout();
                        free_pending_sprite();
                        free_pending_bgalpha();
                    }
                    if (g_placing_cutout) {
                        g_placing_cutout = false;
                    }
                    if (g_placing_sprite) {
                        g_placing_sprite = false;
                    }
                    g_mode_curve = !g_mode_curve;
                    if (g_mode_curve) {
                        g_frog_hovered = false;
                        g_hovered_coin_idx = -1;
                        g_hovered_cutout_idx = -1;
                        g_hovered_sprite_idx = -1;
                        g_hovered_bgalpha_idx = -1;
                        if (g_path_count == 0) {
                            g_path_count = 1;
                            g_active_path_idx = 0;
                            char name[64];
                            snprintf(name, sizeof(name), "%s", g_canvas_name[0] ? g_canvas_name : "curve");
                            bezier_path_init(&g_paths[0], name);
                        } else if (g_active_path_idx < 0) {
                            g_active_path_idx = 0;
                        }
                    } else {
                        if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
                    }
                    HMENU hmenu = GetMenu(hwnd);
                    if (hmenu) {
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_MODE, g_mode_canvas ? MF_CHECKED : MF_UNCHECKED);
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_ELEMENT, g_mode_element ? MF_CHECKED : MF_UNCHECKED);
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_CURVE, g_mode_curve ? MF_CHECKED : MF_UNCHECKED);
                        CheckMenuItem(hmenu, ID_MENU_CANVAS_OVERLAY, g_mode_overlay ? MF_CHECKED : MF_UNCHECKED);
                    }
                    refresh_toolbar_buttons();
                    sync_level_preview_state();
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateWindow(hwnd);
                    update_status_bar();
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_LEVEL_PREVIEW: {
                    toggle_level_preview(hwnd);
                    break;
                }

                case ID_MENU_CANVAS_OVERLAY:
                case ID_BTN_OVERLAY: {
                    toggle_overlay_mode(hwnd);
                    break;
                }

                case ID_OVERLAY_ADD_BGALPHA: {
                    trigger_add_bgalpha(hwnd);
                    break;
                }

                case ID_MENU_CANVAS_ADD_CUTOUT:
                case ID_BTN_CUTOUT: {
                    trigger_add_cutout(hwnd);
                    break;
                }

                case ID_MENU_CANVAS_ADD_SPRITE:
                case ID_BTN_SPRITE: {
                    trigger_add_sprite(hwnd);
                    break;
                }

                case ID_OVERLAY_SET_VARIABLE: {
                    toggle_overlay_variable_mode(hwnd);
                    break;
                }

                case ID_MENU_SELECT_ALL: {
                    handle_select_all(hwnd);
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_INVERT_SEL: {
                    handle_invert_selection(hwnd);
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_DESELECT_ALL: {
                    handle_deselect_all(hwnd);
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_CURVE_IMPORT_SVG: {
                    handle_import_path(hwnd);
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_CURVE_EXPORT_SVG: {
                    handle_export_path(hwnd);
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_VIEW_100:
                    if (g_has_canvas) {
                        g_zoom = 1.0f;
                        update_status_bar();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    SetFocus(hwnd);
                    break;

                case ID_MENU_VIEW_RESET:
                    if (g_has_canvas) {
                        g_pan_x = 0.0f;
                        g_pan_y = 0.0f;
                        update_status_bar();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    SetFocus(hwnd);
                    break;

                case ID_MENU_VIEW_ZOOMIN: {
                    if (g_has_canvas) {
                        float new_zoom = g_zoom * 1.25f;
                        if (new_zoom <= 8.0f) {
                            g_zoom = new_zoom;
                            update_status_bar();
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_VIEW_ZOOMOUT: {
                    if (g_has_canvas) {
                        float new_zoom = g_zoom / 1.25f;
                        if (new_zoom >= 0.25f) {
                            g_zoom = new_zoom;
                            update_status_bar();
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_VIEW_FIT:
                    zoom_to_fit(hwnd);
                    SetFocus(hwnd);
                    break;

                case ID_MENU_VIEW_GRID: {
                    g_show_grid = !g_show_grid;
                    save_zuma_config();
                    HMENU hmenu = GetMenu(hwnd);
                    if (hmenu) {
                        CheckMenuItem(hmenu, ID_MENU_VIEW_GRID, g_show_grid ? MF_CHECKED : MF_UNCHECKED);
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    SetFocus(hwnd);
                    break;
                }

                case ID_MENU_THEME_LIGHT:
                    apply_theme(hwnd, THEME_LIGHT);
                    SetFocus(hwnd);
                    break;

                case ID_MENU_THEME_DARK:
                    apply_theme(hwnd, THEME_DARK);
                    SetFocus(hwnd);
                    break;

                case ID_MENU_THEME_TOGGLE:
                    apply_theme(hwnd, (g_current_theme == THEME_LIGHT) ? THEME_DARK : THEME_LIGHT);
                    SetFocus(hwnd);
                    break;

                case ID_EDIT_CV_W:
                case ID_EDIT_CV_H: {
                    static bool s_in_edit_sync = false;
                    if (HIWORD(wParam) == EN_CHANGE && !s_in_edit_sync && !g_in_canvas_edit_sync && g_has_canvas) {
                        char buf_w[32] = {0};
                        char buf_h[32] = {0};
                        if (g_edit_cv_w) GetWindowTextA(g_edit_cv_w, buf_w, sizeof(buf_w));
                        if (g_edit_cv_h) GetWindowTextA(g_edit_cv_h, buf_h, sizeof(buf_h));
                        int nw = atoi(buf_w);
                        int nh = atoi(buf_h);
                        if (nw >= 64 && nw <= 8192 && nh >= 64 && nh <= 8192) {
                            if (nw != g_canvas_w || nh != g_canvas_h) {
                                s_in_edit_sync = true;
                                g_in_canvas_edit_sync = true;
                                set_canvas_size(hwnd, nw, nh);
                                g_in_canvas_edit_sync = false;
                                s_in_edit_sync = false;
                            }
                        }
                    }
                    break;
                }

                case ID_BTN_CV_APPLY:
                case ID_BTN_CANVAS_APPLY: {
                    char buf_w[32] = {0};
                    char buf_h[32] = {0};
                    if (g_edit_cv_w) GetWindowTextA(g_edit_cv_w, buf_w, sizeof(buf_w));
                    if (g_edit_cv_h) GetWindowTextA(g_edit_cv_h, buf_h, sizeof(buf_h));
                    int nw = atoi(buf_w);
                    int nh = atoi(buf_h);
                    if (nw > 0 && nh > 0) {
                        set_canvas_size(hwnd, nw, nh);
                        undo_snapshot_capture("Resize Canvas");
                    }
                    SetFocus(hwnd);
                    break;
                }

                case ID_BTN_CV_PRESET_640:
                    set_canvas_size(hwnd, 640, 480);
                    undo_snapshot_capture("Resize Canvas 640x480");
                    SetFocus(hwnd);
                    break;
                case ID_BTN_CV_PRESET_800:
                    set_canvas_size(hwnd, 800, 600);
                    undo_snapshot_capture("Resize Canvas 800x600");
                    SetFocus(hwnd);
                    break;
                case ID_BTN_CV_PRESET_1024:
                    set_canvas_size(hwnd, 1024, 768);
                    undo_snapshot_capture("Resize Canvas 1024x768");
                    SetFocus(hwnd);
                    break;
                case ID_BTN_CV_PRESET_1280:
                    set_canvas_size(hwnd, 1280, 720);
                    undo_snapshot_capture("Resize Canvas 1280x720");
                    SetFocus(hwnd);
                    break;

                case ID_BTN_CV_FIT_ORIG:
                    g_canvas_fit_mode = CANVAS_FIT_ORIGINAL;
                    InvalidateRect(hwnd, NULL, FALSE);
                    SetFocus(hwnd);
                    break;
                case ID_BTN_CV_FIT_FIT:
                    g_canvas_fit_mode = CANVAS_FIT_FIT;
                    InvalidateRect(hwnd, NULL, FALSE);
                    SetFocus(hwnd);
                    break;
                case ID_BTN_CV_FIT_FILL:
                    g_canvas_fit_mode = CANVAS_FIT_FILL;
                    InvalidateRect(hwnd, NULL, FALSE);
                    SetFocus(hwnd);
                    break;
                case ID_BTN_CV_FIT_STRETCH:
                    g_canvas_fit_mode = CANVAS_FIT_STRETCH;
                    InvalidateRect(hwnd, NULL, FALSE);
                    SetFocus(hwnd);
                    break;

                case ID_BTN_CV_ALIGN_TL: g_canvas_alignment = CANVAS_ALIGN_TOP_LEFT; InvalidateRect(hwnd, NULL, FALSE); SetFocus(hwnd); break;
                case ID_BTN_CV_ALIGN_TC: g_canvas_alignment = CANVAS_ALIGN_TOP_CENTER; InvalidateRect(hwnd, NULL, FALSE); SetFocus(hwnd); break;
                case ID_BTN_CV_ALIGN_TR: g_canvas_alignment = CANVAS_ALIGN_TOP_RIGHT; InvalidateRect(hwnd, NULL, FALSE); SetFocus(hwnd); break;
                case ID_BTN_CV_ALIGN_ML: g_canvas_alignment = CANVAS_ALIGN_MID_LEFT; InvalidateRect(hwnd, NULL, FALSE); SetFocus(hwnd); break;
                case ID_BTN_CV_ALIGN_MC: g_canvas_alignment = CANVAS_ALIGN_CENTER; InvalidateRect(hwnd, NULL, FALSE); SetFocus(hwnd); break;
                case ID_BTN_CV_ALIGN_MR: g_canvas_alignment = CANVAS_ALIGN_MID_RIGHT; InvalidateRect(hwnd, NULL, FALSE); SetFocus(hwnd); break;
                case ID_BTN_CV_ALIGN_BL: g_canvas_alignment = CANVAS_ALIGN_BOTTOM_LEFT; InvalidateRect(hwnd, NULL, FALSE); SetFocus(hwnd); break;
                case ID_BTN_CV_ALIGN_BC: g_canvas_alignment = CANVAS_ALIGN_BOTTOM_CENTER; InvalidateRect(hwnd, NULL, FALSE); SetFocus(hwnd); break;
                case ID_BTN_CV_ALIGN_BR: g_canvas_alignment = CANVAS_ALIGN_BOTTOM_RIGHT; InvalidateRect(hwnd, NULL, FALSE); SetFocus(hwnd); break;

                case ID_EDIT_PRIO: {
                    if (HIWORD(wParam) == EN_CHANGE && !g_in_prio_update) {
                        char buf[32] = {0};
                        GetWindowTextA(g_edit_prio, buf, sizeof(buf));
                        int val = atoi(buf);
                        if (val < 0) val = 0;
                        if (g_mode_overlay) {
                            if (val > 99) val = 99;
                            g_overlay_current_prio = val;
                            g_pending_cutout_pri = val;
                            g_pending_sprite_pri = val;
                        } else {
                            if (val > 255) val = 255;
                            g_var_prio_val = (uint8_t)val;
                            g_var_write_prio = true;
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    break;
                }

                case ID_EDIT_PREVIEW_TITLE: {
                    if (HIWORD(wParam) == EN_CHANGE && g_has_canvas) {
                        static bool s_in_title_sync = false;
                        if (!s_in_title_sync && g_edit_preview_title) {
                            s_in_title_sync = true;
                            char buf[256] = {0};
                            GetWindowTextA(g_edit_preview_title, buf, sizeof(buf));
                            sync_canvas_title_everywhere(buf, g_edit_preview_title);
                            s_in_title_sync = false;
                        }
                    }
                    break;
                }

                case ID_EDIT_MAP_SKULLROT: {
                    if (HIWORD(wParam) == EN_CHANGE && g_has_canvas) {
                        char buf[32] = {0};
                        if (g_edit_map_skullrot) GetWindowTextA(g_edit_map_skullrot, buf, sizeof(buf));
                        g_map_skullrot = (float)atof(buf);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    break;
                }

                case ID_EDIT_DEV_CURVECOLOR: {
                    if (HIWORD(wParam) == EN_CHANGE && g_has_canvas) {
                        char buf[32] = {0};
                        if (g_edit_dev_curvecolor) GetWindowTextA(g_edit_dev_curvecolor, buf, sizeof(buf));
                        strncpy(g_dev_curvecolor, buf, sizeof(g_dev_curvecolor) - 1);
                        g_dev_curvecolor[sizeof(g_dev_curvecolor) - 1] = '\0';
                    }
                    break;
                }

                case ID_EDIT_DEV_BGCOLOR: {
                    if (HIWORD(wParam) == EN_CHANGE && g_has_canvas) {
                        char buf[32] = {0};
                        if (g_edit_dev_bgcolor) GetWindowTextA(g_edit_dev_bgcolor, buf, sizeof(buf));
                        strncpy(g_dev_bgcolor, buf, sizeof(g_dev_bgcolor) - 1);
                        g_dev_bgcolor[sizeof(g_dev_bgcolor) - 1] = '\0';
                    }
                    break;
                }

                case ID_EDIT_DEV_HUEROT: {
                    if (HIWORD(wParam) == EN_CHANGE && g_has_canvas) {
                        char buf[32] = {0};
                        if (g_edit_dev_huerot) GetWindowTextA(g_edit_dev_huerot, buf, sizeof(buf));
                        g_dev_huerot = (float)atof(buf);
                    }
                    break;
                }

                case ID_EDIT_DEV_TREASUREFREQ: {
                    if (HIWORD(wParam) == EN_CHANGE && g_has_canvas) {
                        char buf[32] = {0};
                        if (g_edit_dev_treasurefreq) GetWindowTextA(g_edit_dev_treasurefreq, buf, sizeof(buf));
                        g_dev_treasurefreq = atoi(buf);
                    }
                    break;
                }

                case ID_EDIT_DEV_PARTIME: {
                    if (HIWORD(wParam) == EN_CHANGE && g_has_canvas) {
                        char buf[32] = {0};
                        if (g_edit_dev_partime) GetWindowTextA(g_edit_dev_partime, buf, sizeof(buf));
                        g_dev_partime = atoi(buf);
                    }
                    break;
                }

                case ID_MENU_CANVAS_640_480:
                    set_canvas_size(hwnd, 640, 480);
                    undo_snapshot_capture("Resize Canvas 640x480");
                    SetFocus(hwnd);
                    break;

                case ID_MENU_CANVAS_800_600:
                    set_canvas_size(hwnd, 800, 600);
                    undo_snapshot_capture("Resize Canvas 800x600");
                    SetFocus(hwnd);
                    break;

                case ID_MENU_CANVAS_1024_768:
                    set_canvas_size(hwnd, 1024, 768);
                    undo_snapshot_capture("Resize Canvas 1024x768");
                    SetFocus(hwnd);
                    break;

                case ID_MENU_CANVAS_1280_720:
                    set_canvas_size(hwnd, 1280, 720);
                    undo_snapshot_capture("Resize Canvas 1280x720");
                    SetFocus(hwnd);
                    break;

                case ID_MENU_CANVAS_CUSTOM:
                    SetFocus(g_edit_w);
                    SendMessage(g_edit_w, EM_SETSEL, 0, -1);
                    break;

                case ID_MENU_FILE_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int fwKeys = GET_KEYSTATE_WPARAM(wParam);
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

            /* Intercept scroll wheel over Top-Right and Top-Left HUD panels */
            if (g_mode_curve && g_has_canvas) {
                POINT pt_hud;
                pt_hud.x = GET_X_LPARAM(lParam);
                pt_hud.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt_hud);

                RECT rc_c;
                GetClientRect(hwnd, &rc_c);
                int c_w = rc_c.right - rc_c.left;
                int w_h = get_workspace_height(hwnd);
                RECT rc_tr, rc_tl;
                get_top_right_hud_rect(c_w, w_h, &rc_tr);
                get_top_left_hud_rect(w_h, &rc_tl);

                if (pt_hud.x >= rc_tr.left && pt_hud.x <= rc_tr.right &&
                    pt_hud.y >= rc_tr.top && pt_hud.y <= rc_tr.bottom) {
                    int max_s = get_attr_hud_max_scroll(w_h);
                    if (max_s > 0) {
                        g_hud_attr_scroll_y -= (zDelta > 0 ? 30 : -30);
                        if (g_hud_attr_scroll_y < 0) g_hud_attr_scroll_y = 0;
                        if (g_hud_attr_scroll_y > max_s) g_hud_attr_scroll_y = max_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                } else if (pt_hud.x >= rc_tl.left && pt_hud.x <= rc_tl.right &&
                           pt_hud.y >= rc_tl.top && pt_hud.y <= rc_tl.bottom) {
                    int max_s = get_guide_hud_max_scroll(w_h);
                    if (max_s > 0) {
                        g_hud_guide_scroll_y -= (zDelta > 0 ? 30 : -30);
                        if (g_hud_guide_scroll_y < 0) g_hud_guide_scroll_y = 0;
                        if (g_hud_guide_scroll_y > max_s) g_hud_guide_scroll_y = max_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
            } else if (g_mode_overlay && g_has_canvas && !(fwKeys & MK_CONTROL)) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt);

                int w_h = get_workspace_height(hwnd);
                RECT rc_guide;
                get_top_left_overlay_guide_hud_rect(w_h, &rc_guide);
                if (pt.x >= rc_guide.left && pt.x <= rc_guide.right &&
                    pt.y >= rc_guide.top && pt.y <= rc_guide.bottom) {
                    int max_s = get_overlay_guide_hud_max_scroll(w_h);
                    if (max_s > 0) {
                        g_hud_overlay_guide_scroll_y -= (zDelta > 0 ? 30 : -30);
                        if (g_hud_overlay_guide_scroll_y < 0) g_hud_overlay_guide_scroll_y = 0;
                        if (g_hud_overlay_guide_scroll_y > max_s) g_hud_overlay_guide_scroll_y = max_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
                RECT rc_c;
                GetClientRect(hwnd, &rc_c);
                int c_w = rc_c.right - rc_c.left;
                RECT rc_ov;
                get_top_right_overlay_hud_rect(c_w, w_h, &rc_ov);
                if (pt.x >= rc_ov.left && pt.x <= rc_ov.right &&
                    pt.y >= rc_ov.top && pt.y <= rc_ov.bottom) {
                    int max_s = get_overlay_tools_hud_max_scroll(w_h);
                    if (max_s > 0) {
                        g_hud_overlay_tools_scroll_y -= (zDelta > 0 ? 30 : -30);
                        if (g_hud_overlay_tools_scroll_y < 0) g_hud_overlay_tools_scroll_y = 0;
                        if (g_hud_overlay_tools_scroll_y > max_s) g_hud_overlay_tools_scroll_y = max_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                } else if (g_adjusting_overlay_priority && pt.y >= TOOLBAR_HEIGHT) {
                    if (zDelta > 0) {
                        if (g_pending_overlay_priority < 99) g_pending_overlay_priority++;
                    } else {
                        if (g_pending_overlay_priority > 0) g_pending_overlay_priority--;
                    }
                    g_overlay_current_prio = g_pending_overlay_priority;
                    if (g_editing_prio_type == 0 && g_editing_prio_idx >= 0 && g_editing_prio_idx < g_cutout_count) {
                        g_cutouts[g_editing_prio_idx].pri = g_pending_overlay_priority;
                    } else if (g_editing_prio_type == 1 && g_editing_prio_idx >= 0 && g_editing_prio_idx < g_sprite_count) {
                        g_sprites[g_editing_prio_idx].pri = g_pending_overlay_priority;
                    }
                    if (g_edit_prio) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%d", g_pending_overlay_priority);
                        g_in_prio_update = true;
                        SetWindowTextA(g_edit_prio, buf);
                        g_in_prio_update = false;
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                } else if (!g_placing_bgalpha && pt.y >= TOOLBAR_HEIGHT) {
                    if (zDelta > 0) {
                        if (g_overlay_current_prio < 99) g_overlay_current_prio++;
                    } else {
                        if (g_overlay_current_prio > 0) g_overlay_current_prio--;
                    }
                    g_pending_cutout_pri = g_overlay_current_prio;
                    g_pending_sprite_pri = g_overlay_current_prio;
                    if (g_edit_prio) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%d", g_overlay_current_prio);
                        g_in_prio_update = true;
                        SetWindowTextA(g_edit_prio, buf);
                        g_in_prio_update = false;
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
            } else if (g_mode_element && g_has_canvas && !(fwKeys & MK_CONTROL)) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt);

                int work_h = get_workspace_height(hwnd);
                RECT rc_guide;
                get_top_left_element_guide_hud_rect(work_h, &rc_guide);
                if (pt.x >= rc_guide.left && pt.x <= rc_guide.right &&
                    pt.y >= rc_guide.top && pt.y <= rc_guide.bottom) {
                    int max_s = get_element_guide_hud_max_scroll(work_h);
                    if (max_s > 0) {
                        g_hud_element_guide_scroll_y -= (zDelta > 0 ? 30 : -30);
                        if (g_hud_element_guide_scroll_y < 0) g_hud_element_guide_scroll_y = 0;
                        if (g_hud_element_guide_scroll_y > max_s) g_hud_element_guide_scroll_y = max_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
                RECT rc_c;
                GetClientRect(hwnd, &rc_c);
                int c_w = rc_c.right - rc_c.left;
                RECT rc_el;
                get_top_right_element_hud_rect(c_w, work_h, &rc_el);
                if (pt.x >= rc_el.left && pt.x <= rc_el.right &&
                    pt.y >= rc_el.top && pt.y <= rc_el.bottom) {
                    int max_s = get_element_tools_hud_max_scroll(work_h);
                    if (max_s > 0) {
                        g_hud_element_tools_scroll_y -= (zDelta > 0 ? 30 : -30);
                        if (g_hud_element_tools_scroll_y < 0) g_hud_element_tools_scroll_y = 0;
                        if (g_hud_element_tools_scroll_y > max_s) g_hud_element_tools_scroll_y = max_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
            } else if (g_mode_canvas && g_has_canvas && !(fwKeys & MK_CONTROL)) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt);

                int work_h = get_workspace_height(hwnd);
                RECT rc_guide;
                get_top_left_canvas_guide_hud_rect(work_h, &rc_guide);
                if (pt.x >= rc_guide.left && pt.x <= rc_guide.right &&
                    pt.y >= rc_guide.top && pt.y <= rc_guide.bottom) {
                    int max_s = get_canvas_guide_hud_max_scroll(work_h);
                    if (max_s > 0) {
                        g_hud_canvas_guide_scroll_y -= (zDelta > 0 ? 30 : -30);
                        if (g_hud_canvas_guide_scroll_y < 0) g_hud_canvas_guide_scroll_y = 0;
                        if (g_hud_canvas_guide_scroll_y > max_s) g_hud_canvas_guide_scroll_y = max_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
                RECT rc_c;
                GetClientRect(hwnd, &rc_c);
                int c_w = rc_c.right - rc_c.left;
                RECT rc_cv;
                get_top_right_canvas_hud_rect(c_w, work_h, &rc_cv);
                if (pt.x >= rc_cv.left && pt.x <= rc_cv.right &&
                    pt.y >= rc_cv.top && pt.y <= rc_cv.bottom) {
                    int max_s = get_canvas_tools_hud_max_scroll(work_h);
                    if (max_s > 0) {
                        g_hud_canvas_tools_scroll_y -= (zDelta > 0 ? 30 : -30);
                        if (g_hud_canvas_tools_scroll_y < 0) g_hud_canvas_tools_scroll_y = 0;
                        if (g_hud_canvas_tools_scroll_y > max_s) g_hud_canvas_tools_scroll_y = max_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
            } else if (!g_mode_canvas && !g_mode_element && !g_mode_curve && !g_mode_overlay && !(fwKeys & MK_CONTROL)) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt);

                int work_h = get_workspace_height(hwnd);
                RECT rc_guide;
                get_top_left_preview_guide_hud_rect(work_h, &rc_guide);
                if (pt.x >= rc_guide.left && pt.x <= rc_guide.right &&
                    pt.y >= rc_guide.top && pt.y <= rc_guide.bottom) {
                    int max_s = get_preview_guide_hud_max_scroll(work_h);
                    if (max_s > 0) {
                        g_hud_preview_guide_scroll_y -= (zDelta > 0 ? 30 : -30);
                        if (g_hud_preview_guide_scroll_y < 0) g_hud_preview_guide_scroll_y = 0;
                        if (g_hud_preview_guide_scroll_y > max_s) g_hud_preview_guide_scroll_y = max_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
                RECT rc_c;
                GetClientRect(hwnd, &rc_c);
                int c_w = rc_c.right - rc_c.left;
                RECT rc_pv;
                get_top_right_preview_hud_rect(c_w, work_h, &rc_pv);
                if (pt.x >= rc_pv.left && pt.x <= rc_pv.right &&
                    pt.y >= rc_pv.top && pt.y <= rc_pv.bottom) {
                    int max_s = get_preview_hud_max_scroll(work_h);
                    if (max_s > 0) {
                        g_hud_preview_info_scroll_y -= (zDelta > 0 ? 30 : -30);
                        if (g_hud_preview_info_scroll_y < 0) g_hud_preview_info_scroll_y = 0;
                        if (g_hud_preview_info_scroll_y > max_s) g_hud_preview_info_scroll_y = max_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
            }

            /* In Curve Attribute Mode: scroll wheel anywhere in workspace adjusts Priority (min 0) */
            if (g_mode_curve && g_curve_submode == CURVE_SUBMODE_VARIABLE && !(fwKeys & MK_CONTROL)) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt);

                if (pt.y >= TOOLBAR_HEIGHT) {
                    if (zDelta > 0) {
                        g_var_prio_val++;
                    } else if (g_var_prio_val > 0) {
                        g_var_prio_val--;
                    }
                    g_var_write_prio = true;
                    if (g_edit_prio) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%u", g_var_prio_val);
                        g_in_prio_update = true;
                        SetWindowTextA(g_edit_prio, buf);
                        g_in_prio_update = false;
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
            }

            if (g_has_canvas && (fwKeys & MK_CONTROL)) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt);

                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);

                float before_cx, before_cy;
                screen_to_canvas(client_w, work_h, pt.x, pt.y, &before_cx, &before_cy);

                float old_zoom = g_zoom;
                float zoom_factor = (zDelta > 0) ? 1.15f : (1.0f / 1.15f);
                float new_zoom = old_zoom * zoom_factor;

                if (new_zoom < 0.25f) new_zoom = 0.25f;
                if (new_zoom > 8.0f)  new_zoom = 8.0f;

                float scaled_w = (float)g_canvas_w * new_zoom;
                float scaled_h = (float)g_canvas_h * new_zoom;
                g_pan_x = (float)pt.x - ((float)client_w - scaled_w) * 0.5f - (before_cx * new_zoom);
                g_pan_y = (float)pt.y - (float)TOOLBAR_HEIGHT - ((float)work_h - scaled_h) * 0.5f - (before_cy * new_zoom);
                g_zoom = new_zoom;

                update_status_bar();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;
        }

        case WM_MBUTTONDOWN: {
            if (g_has_canvas) {
                g_is_panning = true;
                g_last_mouse_pos.x = GET_X_LPARAM(lParam);
                g_last_mouse_pos.y = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_MBUTTONUP: {
            if (g_is_panning) {
                g_is_panning = false;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_SETCURSOR: {
            if (g_mode_curve && g_has_canvas && g_mouse_screen_y >= TOOLBAR_HEIGHT && LOWORD(lParam) == HTCLIENT) {
                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);
                RECT rc_tr, rc_tl;
                get_top_right_hud_rect(client_w, work_h, &rc_tr);
                get_top_left_hud_rect(work_h, &rc_tl);

                if (g_is_dragging_attr_thumb || g_is_dragging_guide_thumb ||
                    g_is_dragging_attr_bg || g_is_dragging_guide_bg) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }

                if (g_mouse_screen_x >= rc_tr.left && g_mouse_screen_x <= rc_tr.right &&
                    g_mouse_screen_y >= rc_tr.top && g_mouse_screen_y <= rc_tr.bottom) {
                    int track_y, track_h, thumb_y, thumb_h;
                    if (get_attr_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                        int tr_w = rc_tr.right - rc_tr.left;
                        int rel_x = g_mouse_screen_x - rc_tr.left;
                        int rel_y = g_mouse_screen_y - rc_tr.top;
                        if (rel_x >= tr_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            return TRUE;
                        }
                    }
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }
                if (g_mouse_screen_x >= rc_tl.left && g_mouse_screen_x <= rc_tl.right &&
                    g_mouse_screen_y >= rc_tl.top && g_mouse_screen_y <= rc_tl.bottom) {
                    int track_y, track_h, thumb_y, thumb_h;
                    if (get_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                        int tl_w = rc_tl.right - rc_tl.left;
                        int rel_x = g_mouse_screen_x - rc_tl.left;
                        int rel_y = g_mouse_screen_y - rc_tl.top;
                        if (rel_x >= tl_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            return TRUE;
                        }
                    }
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }
                if (g_curve_submode == CURVE_SUBMODE_VARIABLE && (g_var_hover_active || g_var_dragging)) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
                SetCursor(LoadCursor(NULL, IDC_CROSS));
                return TRUE;
            }
            if ((g_placing_frog || g_placing_coin || g_placing_cutout || g_placing_sprite || g_placing_bgalpha || g_placing_bgalpha_vector) && LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursor(NULL, IDC_CROSS));
                return TRUE;
            }
            if (g_mode_overlay && g_has_canvas && LOWORD(lParam) == HTCLIENT) {
                if (g_is_dragging_ov_guide_thumb || g_is_dragging_ov_guide_bg ||
                    g_is_dragging_ov_tools_thumb || g_is_dragging_ov_tools_bg) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
                RECT rc_c;
                GetClientRect(hwnd, &rc_c);
                int c_w = rc_c.right - rc_c.left;
                int w_h = get_workspace_height(hwnd);
                RECT rc_ov, rc_guide;
                get_top_right_overlay_hud_rect(c_w, w_h, &rc_ov);
                get_top_left_overlay_guide_hud_rect(w_h, &rc_guide);

                if (g_mouse_screen_x >= rc_guide.left && g_mouse_screen_x <= rc_guide.right &&
                    g_mouse_screen_y >= rc_guide.top && g_mouse_screen_y <= rc_guide.bottom) {
                    int track_y, track_h, thumb_y, thumb_h;
                    if (get_overlay_guide_scroll_geometry(w_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                        int tl_w = rc_guide.right - rc_guide.left;
                        int rel_x = g_mouse_screen_x - rc_guide.left;
                        int rel_y = g_mouse_screen_y - rc_guide.top;
                        if (rel_x >= tl_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            return TRUE;
                        }
                    }
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }
                if (g_mouse_screen_x >= rc_ov.left && g_mouse_screen_x <= rc_ov.right &&
                    g_mouse_screen_y >= rc_ov.top && g_mouse_screen_y <= rc_ov.bottom) {
                    int track_y, track_h, thumb_y, thumb_h;
                    if (get_overlay_tools_scroll_geometry(w_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                        int tr_w = rc_ov.right - rc_ov.left;
                        int rel_x = g_mouse_screen_x - rc_ov.left;
                        int rel_y = g_mouse_screen_y - rc_ov.top;
                        if (rel_x >= tr_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            return TRUE;
                        }
                    }
                    int tr_rel_x = g_mouse_screen_x - rc_ov.left;
                    int tr_content_y = (g_mouse_screen_y - rc_ov.top) + g_hud_overlay_tools_scroll_y;
                    if (tr_content_y >= 76 && tr_content_y <= 98) {
                        if (tr_rel_x >= 8 && tr_rel_x <= 104) {
                            int sel_ba = get_active_selected_bgalpha_idx();
                            if (sel_ba < 0 || sel_ba >= g_bg_alpha_count) {
                                SetCursor(LoadCursor(NULL, IDC_ARROW));
                                return TRUE;
                            }
                        } else if (tr_rel_x >= 108 && tr_rel_x < 172) {
                            SetCursor(LoadCursor(NULL, IDC_ARROW));
                            return TRUE;
                        } else if (tr_rel_x >= 172 && tr_rel_x <= 228) {
                            SetCursor(LoadCursor(NULL, IDC_IBEAM));
                            return TRUE;
                        }
                    }
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
                if (g_hovered_cutout_idx != -1 || g_hovered_sprite_idx != -1 || g_hovered_bgalpha_idx != -1) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
            } else if (g_mode_element && g_has_canvas && LOWORD(lParam) == HTCLIENT) {
                if (g_is_dragging_el_guide_thumb || g_is_dragging_el_guide_bg ||
                    g_is_dragging_el_tools_thumb || g_is_dragging_el_tools_bg) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
                RECT rc_c;
                GetClientRect(hwnd, &rc_c);
                int c_w = rc_c.right - rc_c.left;
                int work_h = get_workspace_height(hwnd);
                RECT rc_el, rc_guide;
                get_top_right_element_hud_rect(c_w, work_h, &rc_el);
                get_top_left_element_guide_hud_rect(work_h, &rc_guide);

                if (g_mouse_screen_x >= rc_guide.left && g_mouse_screen_x <= rc_guide.right &&
                    g_mouse_screen_y >= rc_guide.top && g_mouse_screen_y <= rc_guide.bottom) {
                    int track_y, track_h, thumb_y, thumb_h;
                    if (get_element_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                        int tl_w = rc_guide.right - rc_guide.left;
                        int rel_x = g_mouse_screen_x - rc_guide.left;
                        int rel_y = g_mouse_screen_y - rc_guide.top;
                        if (rel_x >= tl_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            return TRUE;
                        }
                    }
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }
                if (g_mouse_screen_x >= rc_el.left && g_mouse_screen_x <= rc_el.right &&
                    g_mouse_screen_y >= rc_el.top && g_mouse_screen_y <= rc_el.bottom) {
                    int track_y, track_h, thumb_y, thumb_h;
                    if (get_element_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                        int tr_w = rc_el.right - rc_el.left;
                        int rel_x = g_mouse_screen_x - rc_el.left;
                        int rel_y = g_mouse_screen_y - rc_el.top;
                        if (rel_x >= tr_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            return TRUE;
                        }
                    }
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
                if (g_frog_hovered || g_hovered_coin_idx != -1) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
            } else if (g_mode_canvas && g_has_canvas && LOWORD(lParam) == HTCLIENT) {
                if (g_is_dragging_cv_guide_thumb || g_is_dragging_cv_guide_bg ||
                    g_is_dragging_cv_tools_thumb || g_is_dragging_cv_tools_bg) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
                RECT rc_c;
                GetClientRect(hwnd, &rc_c);
                int c_w = rc_c.right - rc_c.left;
                int work_h = get_workspace_height(hwnd);
                RECT rc_cv, rc_guide;
                get_top_right_canvas_hud_rect(c_w, work_h, &rc_cv);
                get_top_left_canvas_guide_hud_rect(work_h, &rc_guide);

                if (g_mouse_screen_x >= rc_guide.left && g_mouse_screen_x <= rc_guide.right &&
                    g_mouse_screen_y >= rc_guide.top && g_mouse_screen_y <= rc_guide.bottom) {
                    int track_y, track_h, thumb_y, thumb_h;
                    if (get_canvas_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                        int tl_w = rc_guide.right - rc_guide.left;
                        int rel_x = g_mouse_screen_x - rc_guide.left;
                        int rel_y = g_mouse_screen_y - rc_guide.top;
                        if (rel_x >= tl_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            return TRUE;
                        }
                    }
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }
                if (g_mouse_screen_x >= rc_cv.left && g_mouse_screen_x <= rc_cv.right &&
                    g_mouse_screen_y >= rc_cv.top && g_mouse_screen_y <= rc_cv.bottom) {
                    int track_y, track_h, thumb_y, thumb_h;
                    if (get_canvas_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                        int tr_w = rc_cv.right - rc_cv.left;
                        int rel_x = g_mouse_screen_x - rc_cv.left;
                        int rel_y = g_mouse_screen_y - rc_cv.top;
                        if (rel_x >= tr_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            return TRUE;
                        }
                    }
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }

                CanvasHandleType ht = hit_test_canvas_handles(c_w, work_h, g_mouse_screen_x, g_mouse_screen_y);
                if (ht == CANVAS_HANDLE_TOP || ht == CANVAS_HANDLE_BOTTOM) {
                    SetCursor(LoadCursor(NULL, IDC_SIZENS));
                    return TRUE;
                } else if (ht == CANVAS_HANDLE_LEFT || ht == CANVAS_HANDLE_RIGHT) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    return TRUE;
                } else if (ht == CANVAS_HANDLE_TOP_LEFT || ht == CANVAS_HANDLE_BOTTOM_RIGHT) {
                    SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
                    return TRUE;
                } else if (ht == CANVAS_HANDLE_TOP_RIGHT || ht == CANVAS_HANDLE_BOTTOM_LEFT) {
                    SetCursor(LoadCursor(NULL, IDC_SIZENESW));
                    return TRUE;
                } else if (ht == CANVAS_HANDLE_CENTER_MOVE) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                    return TRUE;
                }
            } else if (!g_mode_canvas && !g_mode_element && !g_mode_curve && !g_mode_overlay && LOWORD(lParam) == HTCLIENT) {
                if (g_is_dragging_prev_guide_thumb || g_is_dragging_prev_guide_bg ||
                    g_is_dragging_prev_info_thumb || g_is_dragging_prev_info_bg) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
                RECT rc_c;
                GetClientRect(hwnd, &rc_c);
                int c_w = rc_c.right - rc_c.left;
                int work_h = get_workspace_height(hwnd);
                RECT rc_pv, rc_guide;
                get_top_right_preview_hud_rect(c_w, work_h, &rc_pv);
                get_top_left_preview_guide_hud_rect(work_h, &rc_guide);

                if (g_mouse_screen_x >= rc_guide.left && g_mouse_screen_x <= rc_guide.right &&
                    g_mouse_screen_y >= rc_guide.top && g_mouse_screen_y <= rc_guide.bottom) {
                    int track_y, track_h, thumb_y, thumb_h;
                    if (get_preview_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                        int tl_w = rc_guide.right - rc_guide.left;
                        int rel_x = g_mouse_screen_x - rc_guide.left;
                        int rel_y = g_mouse_screen_y - rc_guide.top;
                        if (rel_x >= tl_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            return TRUE;
                        }
                    }
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }
                if (g_mouse_screen_x >= rc_pv.left && g_mouse_screen_x <= rc_pv.right &&
                    g_mouse_screen_y >= rc_pv.top && g_mouse_screen_y <= rc_pv.bottom) {
                    int track_y, track_h, thumb_y, thumb_h;
                    if (get_preview_hud_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                        int tr_w = rc_pv.right - rc_pv.left;
                        int rel_x = g_mouse_screen_x - rc_pv.left;
                        int rel_y = g_mouse_screen_y - rc_pv.top;
                        if (rel_x >= tr_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            return TRUE;
                        }
                    }
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }
            }
            break;
        }

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            g_mouse_screen_x = mx;
            g_mouse_screen_y = my;

            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);

            if (g_is_dragging_guide_thumb) {
                int work_h = get_workspace_height(hwnd);
                int track_y, track_h, thumb_y, thumb_h;
                if (get_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                    int max_s = get_guide_hud_max_scroll(work_h);
                    int travel = track_h - thumb_h;
                    if (travel > 0 && max_s > 0) {
                        int dy = my - g_drag_guide_mouse_start_y;
                        int new_s = g_drag_guide_scroll_start + (int)roundf((float)dy * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_hud_guide_scroll_y) {
                            g_hud_guide_scroll_y = new_s;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_guide_bg) {
                int work_h = get_workspace_height(hwnd);
                int max_s = get_guide_hud_max_scroll(work_h);
                if (max_s > 0) {
                    int dy = my - g_drag_guide_bg_mouse_start_y;
                    int new_s = g_drag_guide_bg_scroll_start - dy;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_hud_guide_scroll_y) {
                        g_hud_guide_scroll_y = new_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }
            if (g_is_dragging_attr_thumb) {
                int work_h = get_workspace_height(hwnd);
                int track_y, track_h, thumb_y, thumb_h;
                if (get_attr_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                    int max_s = get_attr_hud_max_scroll(work_h);
                    int travel = track_h - thumb_h;
                    if (travel > 0 && max_s > 0) {
                        int dy = my - g_drag_attr_mouse_start_y;
                        int new_s = g_drag_attr_scroll_start + (int)roundf((float)dy * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_hud_attr_scroll_y) {
                            g_hud_attr_scroll_y = new_s;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_attr_bg) {
                int work_h = get_workspace_height(hwnd);
                int max_s = get_attr_hud_max_scroll(work_h);
                if (max_s > 0) {
                    int dy = my - g_drag_attr_bg_mouse_start_y;
                    int new_s = g_drag_attr_bg_scroll_start - dy;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_hud_attr_scroll_y) {
                        g_hud_attr_scroll_y = new_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }
            if (g_is_dragging_ov_guide_thumb) {
                int work_h = get_workspace_height(hwnd);
                int track_y, track_h, thumb_y, thumb_h;
                if (get_overlay_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                    int max_s = get_overlay_guide_hud_max_scroll(work_h);
                    int travel = track_h - thumb_h;
                    if (travel > 0 && max_s > 0) {
                        int dy = my - g_drag_ov_guide_mouse_start_y;
                        int new_s = g_drag_ov_guide_scroll_start + (int)roundf((float)dy * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_hud_overlay_guide_scroll_y) {
                            g_hud_overlay_guide_scroll_y = new_s;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_ov_guide_bg) {
                int work_h = get_workspace_height(hwnd);
                int max_s = get_overlay_guide_hud_max_scroll(work_h);
                if (max_s > 0) {
                    int dy = my - g_drag_ov_guide_bg_mouse_start_y;
                    int new_s = g_drag_ov_guide_bg_scroll_start - dy;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_hud_overlay_guide_scroll_y) {
                        g_hud_overlay_guide_scroll_y = new_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }
            if (g_is_dragging_ov_tools_thumb) {
                int work_h = get_workspace_height(hwnd);
                int track_y, track_h, thumb_y, thumb_h;
                if (get_overlay_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                    int max_s = get_overlay_tools_hud_max_scroll(work_h);
                    int travel = track_h - thumb_h;
                    if (travel > 0 && max_s > 0) {
                        int dy = my - g_drag_ov_tools_mouse_start_y;
                        int new_s = g_drag_ov_tools_scroll_start + (int)roundf((float)dy * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_hud_overlay_tools_scroll_y) {
                            g_hud_overlay_tools_scroll_y = new_s;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_ov_tools_bg) {
                int work_h = get_workspace_height(hwnd);
                int max_s = get_overlay_tools_hud_max_scroll(work_h);
                if (max_s > 0) {
                    int dy = my - g_drag_ov_tools_bg_mouse_start_y;
                    int new_s = g_drag_ov_tools_bg_scroll_start - dy;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_hud_overlay_tools_scroll_y) {
                        g_hud_overlay_tools_scroll_y = new_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }
            if (g_is_dragging_el_guide_thumb) {
                int work_h = get_workspace_height(hwnd);
                int track_y, track_h, thumb_y, thumb_h;
                if (get_element_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                    int max_s = get_element_guide_hud_max_scroll(work_h);
                    int travel = track_h - thumb_h;
                    if (travel > 0 && max_s > 0) {
                        int dy = my - g_drag_el_guide_mouse_start_y;
                        int new_s = g_drag_el_guide_scroll_start + (int)roundf((float)dy * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_hud_element_guide_scroll_y) {
                            g_hud_element_guide_scroll_y = new_s;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_el_guide_bg) {
                int work_h = get_workspace_height(hwnd);
                int max_s = get_element_guide_hud_max_scroll(work_h);
                if (max_s > 0) {
                    int dy = my - g_drag_el_guide_bg_mouse_start_y;
                    int new_s = g_drag_el_guide_bg_scroll_start - dy;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_hud_element_guide_scroll_y) {
                        g_hud_element_guide_scroll_y = new_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }
            if (g_is_dragging_el_tools_thumb) {
                int work_h = get_workspace_height(hwnd);
                int track_y, track_h, thumb_y, thumb_h;
                if (get_element_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                    int max_s = get_element_tools_hud_max_scroll(work_h);
                    int travel = track_h - thumb_h;
                    if (travel > 0 && max_s > 0) {
                        int dy = my - g_drag_el_tools_mouse_start_y;
                        int new_s = g_drag_el_tools_scroll_start + (int)roundf((float)dy * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_hud_element_tools_scroll_y) {
                            g_hud_element_tools_scroll_y = new_s;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_el_tools_bg) {
                int work_h = get_workspace_height(hwnd);
                int max_s = get_element_tools_hud_max_scroll(work_h);
                if (max_s > 0) {
                    int dy = my - g_drag_el_tools_bg_mouse_start_y;
                    int new_s = g_drag_el_tools_bg_scroll_start - dy;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_hud_element_tools_scroll_y) {
                        g_hud_element_tools_scroll_y = new_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }

            if (g_is_dragging_cv_guide_thumb) {
                int work_h = get_workspace_height(hwnd);
                int track_y, track_h, thumb_y, thumb_h;
                if (get_canvas_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                    int max_s = get_canvas_guide_hud_max_scroll(work_h);
                    int travel = track_h - thumb_h;
                    if (travel > 0 && max_s > 0) {
                        int dy = my - g_drag_cv_guide_mouse_start_y;
                        int new_s = g_drag_cv_guide_scroll_start + (int)roundf((float)dy * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_hud_canvas_guide_scroll_y) {
                            g_hud_canvas_guide_scroll_y = new_s;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_cv_guide_bg) {
                int work_h = get_workspace_height(hwnd);
                int max_s = get_canvas_guide_hud_max_scroll(work_h);
                if (max_s > 0) {
                    int dy = my - g_drag_cv_guide_bg_mouse_start_y;
                    int new_s = g_drag_cv_guide_bg_scroll_start - dy;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_hud_canvas_guide_scroll_y) {
                        g_hud_canvas_guide_scroll_y = new_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }
            if (g_is_dragging_cv_tools_thumb) {
                int work_h = get_workspace_height(hwnd);
                int track_y, track_h, thumb_y, thumb_h;
                if (get_canvas_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                    int max_s = get_canvas_tools_hud_max_scroll(work_h);
                    int travel = track_h - thumb_h;
                    if (travel > 0 && max_s > 0) {
                        int dy = my - g_drag_cv_tools_mouse_start_y;
                        int new_s = g_drag_cv_tools_scroll_start + (int)roundf((float)dy * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_hud_canvas_tools_scroll_y) {
                            g_hud_canvas_tools_scroll_y = new_s;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_cv_tools_bg) {
                int work_h = get_workspace_height(hwnd);
                int max_s = get_canvas_tools_hud_max_scroll(work_h);
                if (max_s > 0) {
                    int dy = my - g_drag_cv_tools_bg_mouse_start_y;
                    int new_s = g_drag_cv_tools_bg_scroll_start - dy;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_hud_canvas_tools_scroll_y) {
                        g_hud_canvas_tools_scroll_y = new_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }

            if (g_is_dragging_prev_guide_thumb) {
                int work_h = get_workspace_height(hwnd);
                int track_y, track_h, thumb_y, thumb_h;
                if (get_preview_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                    int max_s = get_preview_guide_hud_max_scroll(work_h);
                    int travel = track_h - thumb_h;
                    if (travel > 0 && max_s > 0) {
                        int dy = my - g_drag_prev_guide_mouse_start_y;
                        int new_s = g_drag_prev_guide_scroll_start + (int)roundf((float)dy * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_hud_preview_guide_scroll_y) {
                            g_hud_preview_guide_scroll_y = new_s;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_prev_guide_bg) {
                int work_h = get_workspace_height(hwnd);
                int max_s = get_preview_guide_hud_max_scroll(work_h);
                if (max_s > 0) {
                    int dy = my - g_drag_prev_guide_bg_mouse_start_y;
                    int new_s = g_drag_prev_guide_bg_scroll_start - dy;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_hud_preview_guide_scroll_y) {
                        g_hud_preview_guide_scroll_y = new_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }
            if (g_is_dragging_prev_info_thumb) {
                int work_h = get_workspace_height(hwnd);
                int track_y, track_h, thumb_y, thumb_h;
                if (get_preview_hud_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h)) {
                    int max_s = get_preview_hud_max_scroll(work_h);
                    int travel = track_h - thumb_h;
                    if (travel > 0 && max_s > 0) {
                        int dy = my - g_drag_prev_info_mouse_start_y;
                        int new_s = g_drag_prev_info_scroll_start + (int)roundf((float)dy * (float)max_s / (float)travel);
                        if (new_s < 0) new_s = 0;
                        if (new_s > max_s) new_s = max_s;
                        if (new_s != g_hud_preview_info_scroll_y) {
                            g_hud_preview_info_scroll_y = new_s;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                return 0;
            }
            if (g_is_dragging_prev_info_bg) {
                int work_h = get_workspace_height(hwnd);
                int max_s = get_preview_hud_max_scroll(work_h);
                if (max_s > 0) {
                    int dy = my - g_drag_prev_info_bg_mouse_start_y;
                    int new_s = g_drag_prev_info_bg_scroll_start - dy;
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    if (new_s != g_hud_preview_info_scroll_y) {
                        g_hud_preview_info_scroll_y = new_s;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }

            if (g_drag_canvas_handle != CANVAS_HANDLE_NONE && g_has_canvas) {
                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);

                int dx_screen = mx - g_drag_start_mouse_sx;
                int dy_screen = my - g_drag_start_mouse_sy;
                float zoom_val = (g_zoom > 0.01f ? g_zoom : 1.0f);
                float dx_canvas = (float)dx_screen / zoom_val;
                float dy_canvas = (float)dy_screen / zoom_val;

                bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool is_ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

                if (g_drag_canvas_handle == CANVAS_HANDLE_CENTER_MOVE) {
                    float move_dx = (float)dx_screen;
                    float move_dy = (float)dy_screen;
                    if (is_shift) {
                        snap_vector_45_deg(move_dx, move_dy, &move_dx, &move_dy);
                    }
                    g_pan_x = g_canvas_drag_snap.pan_x + move_dx;
                    g_pan_y = g_canvas_drag_snap.pan_y + move_dy;
                    float shift_x = -move_dx / zoom_val;
                    float shift_y = -move_dy / zoom_val;
                    apply_canvas_drag_shift(shift_x, shift_y);
                } else {
                    float delta_w = 0.0f;
                    float delta_h = 0.0f;
                    bool has_horiz = false;
                    bool has_vert  = false;

                    /* 1. Base directional delta based on handle */
                    if (g_drag_canvas_handle == CANVAS_HANDLE_RIGHT ||
                        g_drag_canvas_handle == CANVAS_HANDLE_TOP_RIGHT ||
                        g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM_RIGHT) {
                        delta_w = dx_canvas;
                        has_horiz = true;
                    } else if (g_drag_canvas_handle == CANVAS_HANDLE_LEFT ||
                               g_drag_canvas_handle == CANVAS_HANDLE_TOP_LEFT ||
                               g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM_LEFT) {
                        delta_w = -dx_canvas;
                        has_horiz = true;
                    }

                    if (g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM ||
                        g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM_LEFT ||
                        g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM_RIGHT) {
                        delta_h = dy_canvas;
                        has_vert = true;
                    } else if (g_drag_canvas_handle == CANVAS_HANDLE_TOP ||
                               g_drag_canvas_handle == CANVAS_HANDLE_TOP_LEFT ||
                               g_drag_canvas_handle == CANVAS_HANDLE_TOP_RIGHT) {
                        delta_h = -dy_canvas;
                        has_vert = true;
                    }

                    /* 2. Aspect Ratio Lock (Shift) */
                    float aspect = (g_drag_start_w > 0 && g_drag_start_h > 0) ? 
                                   ((float)g_drag_start_w / (float)g_drag_start_h) : 1.0f;

                    if (is_shift) {
                        if (has_horiz && has_vert) {
                            if (fabsf(delta_w) >= fabsf(delta_h) * aspect) {
                                delta_h = delta_w / aspect;
                            } else {
                                delta_w = delta_h * aspect;
                            }
                        } else if (has_horiz && !has_vert) {
                            delta_h = delta_w / aspect;
                        } else if (!has_horiz && has_vert) {
                            delta_w = delta_h * aspect;
                        }
                    }

                    /* 3. Mirroring from Center (Ctrl) vs Anchor Side */
                    int new_w = g_drag_start_w;
                    int new_h = g_drag_start_h;
                    float shift_x = 0.0f;
                    float shift_y = 0.0f;
                    float new_pan_x = g_canvas_drag_snap.pan_x;
                    float new_pan_y = g_canvas_drag_snap.pan_y;

                    if (is_ctrl) {
                        /* Sisi sebaliknya ikut kena resize (mirroring dari center) */
                        new_w = g_drag_start_w + (int)roundf(delta_w * 2.0f);
                        new_h = g_drag_start_h + (int)roundf(delta_h * 2.0f);
                        if (new_w < 64) new_w = 64;
                        if (new_h < 64) new_h = 64;

                        new_pan_x = g_canvas_drag_snap.pan_x;
                        new_pan_y = g_canvas_drag_snap.pan_y;

                        shift_x = (float)(new_w - g_drag_start_w) * 0.5f;
                        shift_y = (float)(new_h - g_drag_start_h) * 0.5f;
                    } else {
                        /* Normal Anchor: sisi/sudut sebaliknya diam di layar */
                        new_w = g_drag_start_w + (int)roundf(delta_w);
                        new_h = g_drag_start_h + (int)roundf(delta_h);
                        if (new_w < 64) new_w = 64;
                        if (new_h < 64) new_h = 64;

                        int diff_w = new_w - g_drag_start_w;
                        int diff_h = new_h - g_drag_start_h;

                        /* Horizontal Pan & Shift */
                        if (g_drag_canvas_handle == CANVAS_HANDLE_RIGHT ||
                            g_drag_canvas_handle == CANVAS_HANDLE_TOP_RIGHT ||
                            g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM_RIGHT) {
                            new_pan_x = g_canvas_drag_snap.pan_x + (float)diff_w * 0.5f * g_zoom;
                            shift_x = 0.0f;
                        } else if (g_drag_canvas_handle == CANVAS_HANDLE_LEFT ||
                                   g_drag_canvas_handle == CANVAS_HANDLE_TOP_LEFT ||
                                   g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM_LEFT) {
                            new_pan_x = g_canvas_drag_snap.pan_x - (float)diff_w * 0.5f * g_zoom;
                            shift_x = (float)diff_w;
                        } else {
                            new_pan_x = g_canvas_drag_snap.pan_x;
                            shift_x = (float)diff_w * 0.5f;
                        }

                        /* Vertical Pan & Shift */
                        if (g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM ||
                            g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM_LEFT ||
                            g_drag_canvas_handle == CANVAS_HANDLE_BOTTOM_RIGHT) {
                            new_pan_y = g_canvas_drag_snap.pan_y + (float)diff_h * 0.5f * g_zoom;
                            shift_y = 0.0f;
                        } else if (g_drag_canvas_handle == CANVAS_HANDLE_TOP ||
                                   g_drag_canvas_handle == CANVAS_HANDLE_TOP_LEFT ||
                                   g_drag_canvas_handle == CANVAS_HANDLE_TOP_RIGHT) {
                            new_pan_y = g_canvas_drag_snap.pan_y - (float)diff_h * 0.5f * g_zoom;
                            shift_y = (float)diff_h;
                        } else {
                            new_pan_y = g_canvas_drag_snap.pan_y;
                            shift_y = (float)diff_h * 0.5f;
                        }
                    }

                    g_canvas_w = new_w;
                    g_canvas_h = new_h;
                    g_pan_x = new_pan_x;
                    g_pan_y = new_pan_y;
                    apply_canvas_drag_shift(shift_x, shift_y);
                }

                if (g_edit_cv_w && g_edit_cv_h) {
                    g_in_canvas_edit_sync = true;
                    char buf_w[16], buf_h[16];
                    snprintf(buf_w, sizeof(buf_w), "%d", g_canvas_w);
                    snprintf(buf_h, sizeof(buf_h), "%d", g_canvas_h);
                    if (GetFocus() != g_edit_cv_w) SetWindowTextA(g_edit_cv_w, buf_w);
                    if (GetFocus() != g_edit_cv_h) SetWindowTextA(g_edit_cv_h, buf_h);
                    g_in_canvas_edit_sync = false;
                }

                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            if (g_mode_canvas && g_has_canvas) {
                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);
                CanvasHandleType ht = hit_test_canvas_handles(client_w, work_h, mx, my);
                if (ht != g_hovered_canvas_handle) {
                    g_hovered_canvas_handle = ht;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }

            if (g_is_panning) {
                int dx = mx - g_last_mouse_pos.x;
                int dy = my - g_last_mouse_pos.y;
                g_pan_x += (float)dx;
                g_pan_y += (float)dy;
                g_last_mouse_pos.x = mx;
                g_last_mouse_pos.y = my;
                InvalidateRect(hwnd, NULL, FALSE);
            }

            RECT rc_client;
            GetClientRect(hwnd, &rc_client);
            int client_w = rc_client.right - rc_client.left;
            int work_h = get_workspace_height(hwnd);

            if (g_has_canvas) {
                screen_to_canvas(client_w, work_h, mx, my, &g_canvas_mouse_x, &g_canvas_mouse_y);
                g_mouse_inside_canvas = (g_canvas_mouse_x >= 0.0f && g_canvas_mouse_x <= (float)g_canvas_w &&
                                         g_canvas_mouse_y >= 0.0f && g_canvas_mouse_y <= (float)g_canvas_h);
            } else {
                g_canvas_mouse_x = -1.0f;
                g_canvas_mouse_y = -1.0f;
                g_mouse_inside_canvas = false;
            }

            if (g_obj_drag_state == OBJ_DRAG_MARQUEE && g_has_canvas) {
                g_obj_marquee_cur_x = mx;
                g_obj_marquee_cur_y = my;

                int dx = abs(mx - g_obj_marquee_start_x);
                int dy = abs(my - g_obj_marquee_start_y);
                if (dx >= 4 || dy >= 4) {
                    if (g_mode_canvas) {
                        int min_sx = g_obj_marquee_start_x < g_obj_marquee_cur_x ? g_obj_marquee_start_x : g_obj_marquee_cur_x;
                        int max_sx = g_obj_marquee_start_x > g_obj_marquee_cur_x ? g_obj_marquee_start_x : g_obj_marquee_cur_x;
                        int min_sy = g_obj_marquee_start_y < g_obj_marquee_cur_y ? g_obj_marquee_start_y : g_obj_marquee_cur_y;
                        int max_sy = g_obj_marquee_start_y > g_obj_marquee_cur_y ? g_obj_marquee_start_y : g_obj_marquee_cur_y;
                        update_canvas_handle_marquee_selection(min_sx, min_sy, max_sx, max_sy, client_w, work_h);
                    } else {
                        float cx1, cy1, cx2, cy2;
                        screen_to_canvas(client_w, work_h, g_obj_marquee_start_x, g_obj_marquee_start_y, &cx1, &cy1);
                        screen_to_canvas(client_w, work_h, g_obj_marquee_cur_x, g_obj_marquee_cur_y, &cx2, &cy2);
                        float min_x = cx1 < cx2 ? cx1 : cx2;
                        float max_x = cx1 > cx2 ? cx1 : cx2;
                        float min_y = cy1 < cy2 ? cy1 : cy2;
                        float max_y = cy1 > cy2 ? cy1 : cy2;

                        update_object_marquee_selection(min_x, min_y, max_x, max_y);
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if (g_pen_drag_state != PEN_DRAG_NONE && g_has_canvas) {
                bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool is_alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;

                if (g_pen_drag_state == PEN_DRAG_NEW_HANDLE) {
                    if (g_drag_path_idx >= 0 && g_drag_path_idx < g_path_count) {
                        BezierPath *bp = &g_paths[g_drag_path_idx];
                        if (g_drag_node_idx >= 0 && g_drag_node_idx < bp->node_count) {
                            BezierNode *n = &bp->nodes[g_drag_node_idx];
                            float dx = g_canvas_mouse_x - n->x;
                            float dy = g_canvas_mouse_y - n->y;
                            if (is_shift) {
                                snap_vector_45_deg(dx, dy, &dx, &dy);
                            }
                            if (hypotf(dx, dy) > 2.0f) {
                                if (is_alt) {
                                    n->type = NODE_CUSP;
                                    n->hout_x = dx;
                                    n->hout_y = dy;
                                } else {
                                    n->type = NODE_SYMMETRIC;
                                    n->hout_x = dx;
                                    n->hout_y = dy;
                                    n->hin_x = -dx;
                                    n->hin_y = -dy;
                                }
                                path_cache_invalidate(bp);
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                } else if (g_pen_drag_state == PEN_DRAG_ANCHOR) {
                    if (g_drag_path_idx >= 0 && g_drag_path_idx < g_path_count) {
                        BezierPath *bp = &g_paths[g_drag_path_idx];
                        if (g_drag_node_idx >= 0 && g_drag_node_idx < bp->node_count) {
                            BezierNode *n = &bp->nodes[g_drag_node_idx];
                            float delta_x = g_canvas_mouse_x - g_drag_mouse_start_cx;
                            float delta_y = g_canvas_mouse_y - g_drag_mouse_start_cy;
                            if (is_shift) {
                                snap_vector_45_deg(delta_x, delta_y, &delta_x, &delta_y);
                            }
                            float target_x = g_drag_anchor_start_x + delta_x;
                            float target_y = g_drag_anchor_start_y + delta_y;
                            float shift_x = target_x - n->x;
                            float shift_y = target_y - n->y;
                            n->x = target_x;
                            n->y = target_y;
                            path_cache_invalidate(bp);
                            for (int p = 0; p < g_path_count; ++p) {
                                BezierPath *cur_bp = &g_paths[p];
                                bool any_moved = false;
                                for (int i = 0; i < cur_bp->node_count; ++i) {
                                    if (p == g_drag_path_idx && i == g_drag_node_idx) continue;
                                    if (cur_bp->nodes[i].selected) {
                                        cur_bp->nodes[i].x += shift_x;
                                        cur_bp->nodes[i].y += shift_y;
                                        any_moved = true;
                                    }
                                }
                                if (any_moved) {
                                    path_cache_invalidate(cur_bp);
                                }
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                } else if (g_pen_drag_state == PEN_DRAG_HANDLE_OUT) {
                    if (g_drag_path_idx >= 0 && g_drag_path_idx < g_path_count) {
                        BezierPath *bp = &g_paths[g_drag_path_idx];
                        if (g_drag_node_idx >= 0 && g_drag_node_idx < bp->node_count) {
                            BezierNode *n = &bp->nodes[g_drag_node_idx];
                            float dx = g_canvas_mouse_x - n->x;
                            float dy = g_canvas_mouse_y - n->y;
                            if (is_shift) {
                                snap_vector_45_deg(dx, dy, &dx, &dy);
                            }
                            n->hout_x = dx;
                            n->hout_y = dy;
                            if (is_alt) {
                                n->type = NODE_CUSP;
                            } else {
                                bezier_node_update_handles(n, true);
                            }
                            path_cache_invalidate(bp);
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                } else if (g_pen_drag_state == PEN_DRAG_HANDLE_IN) {
                    if (g_drag_path_idx >= 0 && g_drag_path_idx < g_path_count) {
                        BezierPath *bp = &g_paths[g_drag_path_idx];
                        if (g_drag_node_idx >= 0 && g_drag_node_idx < bp->node_count) {
                            BezierNode *n = &bp->nodes[g_drag_node_idx];
                            float dx = g_canvas_mouse_x - n->x;
                            float dy = g_canvas_mouse_y - n->y;
                            if (is_shift) {
                                snap_vector_45_deg(dx, dy, &dx, &dy);
                            }
                            n->hin_x = dx;
                            n->hin_y = dy;
                            if (is_alt) {
                                n->type = NODE_CUSP;
                            } else {
                                bezier_node_update_handles(n, false);
                            }
                            path_cache_invalidate(bp);
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                } else if (g_pen_drag_state == PEN_DRAG_MARQUEE) {
                    g_marquee_cur_x = mx;
                    g_marquee_cur_y = my;

                    int dx = abs(mx - g_marquee_start_x);
                    int dy = abs(my - g_marquee_start_y);
                    if (dx >= 4 || dy >= 4) {
                        float cx1, cy1, cx2, cy2;
                        screen_to_canvas(client_w, work_h, g_marquee_start_x, g_marquee_start_y, &cx1, &cy1);
                        screen_to_canvas(client_w, work_h, g_marquee_cur_x, g_marquee_cur_y, &cx2, &cy2);
                        float min_x = cx1 < cx2 ? cx1 : cx2;
                        float max_x = cx1 > cx2 ? cx1 : cx2;
                        float min_y = cy1 < cy2 ? cy1 : cy2;
                        float max_y = cy1 > cy2 ? cy1 : cy2;

                        for (int p = 0; p < g_path_count; ++p) {
                            BezierPath *bp = &g_paths[p];
                            for (int i = 0; i < bp->node_count; ++i) {
                                bool base = (g_marquee_snapshot[p] && i < g_marquee_snapshot_count[p]) ? g_marquee_snapshot[p][i] : false;
                                bool inside = (bp->nodes[i].x >= min_x && bp->nodes[i].x <= max_x &&
                                               bp->nodes[i].y >= min_y && bp->nodes[i].y <= max_y);
                                switch (g_marquee_mode) {
                                    case MARQUEE_MODE_REPLACE:
                                        bp->nodes[i].selected = inside;
                                        break;
                                    case MARQUEE_MODE_ADD:
                                        bp->nodes[i].selected = base || inside;
                                        break;
                                    case MARQUEE_MODE_SUBTRACT:
                                        bp->nodes[i].selected = base && !inside;
                                        break;
                                    case MARQUEE_MODE_TOGGLE:
                                        bp->nodes[i].selected = inside ? !base : base;
                                        break;
                                }
                            }
                        }

                        /* Maintain invariant: exactly 1 active path and active node */
                        bool active_still_valid = false;
                        if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
                            if (g_active_node_idx >= 0 && g_active_node_idx < g_paths[g_active_path_idx].node_count) {
                                if (g_paths[g_active_path_idx].nodes[g_active_node_idx].selected) {
                                    active_still_valid = true;
                                }
                            }
                        }
                        if (!active_still_valid) {
                            int new_p = -1;
                            int new_n = -1;
                            if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
                                for (int i = 0; i < g_paths[g_active_path_idx].node_count; ++i) {
                                    if (g_paths[g_active_path_idx].nodes[i].selected) {
                                        new_p = g_active_path_idx;
                                        new_n = i;
                                        break;
                                    }
                                }
                            }
                            if (new_p < 0) {
                                for (int p = 0; p < g_path_count; ++p) {
                                    for (int i = 0; i < g_paths[p].node_count; ++i) {
                                        if (g_paths[p].nodes[i].selected) {
                                            new_p = p;
                                            new_n = i;
                                            break;
                                        }
                                    }
                                    if (new_p >= 0) break;
                                }
                            }
                            g_active_path_idx = new_p;
                            g_active_node_idx = new_n;
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }

            if (g_placing_bgalpha_vector && g_has_canvas) {
                RECT rc_can;
                get_canvas_screen_rect(client_w, work_h, &rc_can);
                int anchor_sx = rc_can.left + (int)roundf((float)g_bgalpha_anchor_x * g_zoom);
                int anchor_sy = rc_can.top + (int)roundf((float)g_bgalpha_anchor_y * g_zoom);

                int dx = mx - anchor_sx;
                int dy = my - anchor_sy;
                float dist = sqrtf((float)(dx * dx + dy * dy));
                if (dist < 16.0f) {
                    g_bgalpha_pending_vx = 0;
                    g_bgalpha_pending_vy = 0;
                } else {
                    float deg = atan2f((float)dy, (float)dx) * 180.0f / 3.14159265f;
                    if (deg < 0.0f) deg += 360.0f;
                    if (deg >= 337.5f || deg < 22.5f) {
                        g_bgalpha_pending_vx = 1;  g_bgalpha_pending_vy = 0;  /* East */
                    } else if (deg >= 22.5f && deg < 67.5f) {
                        g_bgalpha_pending_vx = 1;  g_bgalpha_pending_vy = 1;  /* South-East */
                    } else if (deg >= 67.5f && deg < 112.5f) {
                        g_bgalpha_pending_vx = 0;  g_bgalpha_pending_vy = 1;  /* South */
                    } else if (deg >= 112.5f && deg < 157.5f) {
                        g_bgalpha_pending_vx = -1; g_bgalpha_pending_vy = 1;  /* South-West */
                    } else if (deg >= 157.5f && deg < 202.5f) {
                        g_bgalpha_pending_vx = -1; g_bgalpha_pending_vy = 0;  /* West */
                    } else if (deg >= 202.5f && deg < 247.5f) {
                        g_bgalpha_pending_vx = -1; g_bgalpha_pending_vy = -1; /* North-West */
                    } else if (deg >= 247.5f && deg < 292.5f) {
                        g_bgalpha_pending_vx = 0;  g_bgalpha_pending_vy = -1; /* North */
                    } else {
                        g_bgalpha_pending_vx = 1;  g_bgalpha_pending_vy = -1; /* North-East */
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }

            if (g_placing_frog || g_placing_coin || g_placing_cutout || g_placing_sprite || g_placing_bgalpha || g_placing_bgalpha_vector || g_adjusting_overlay_priority) {
                /* Ghost frog / coin / cutout / sprite / bgalpha / interactive adjustment tracks mouse smoothly */
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (g_mode_overlay) {
                /* In Overlay Mode: ONLY Overlays (Cutouts, Sprites, BackgroundAlphas) can be hovered and edited! */
                if (g_frog_hovered) {
                    g_frog_hovered = false;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                if (g_hovered_coin_idx != -1) {
                    g_hovered_coin_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }

                /* Check cutouts hover */
                if (g_has_canvas && g_cutout_count > 0) {
                    int hit_idx = -1;
                    for (int i = g_cutout_count - 1; i >= 0; i--) {
                        int cw = g_cutouts[i].has_img ? g_cutouts[i].img.width : (g_has_default_cutout_img ? g_default_cutout_img.width : 160);
                        int ch = g_cutouts[i].has_img ? g_cutouts[i].img.height : (g_has_default_cutout_img ? g_default_cutout_img.height : 140);
                        if (g_canvas_mouse_x >= (float)g_cutouts[i].x &&
                            g_canvas_mouse_x <= (float)(g_cutouts[i].x + cw) &&
                            g_canvas_mouse_y >= (float)g_cutouts[i].y &&
                            g_canvas_mouse_y <= (float)(g_cutouts[i].y + ch)) {
                            hit_idx = i;
                            break;
                        }
                    }
                    if (hit_idx != g_hovered_cutout_idx) {
                        g_hovered_cutout_idx = hit_idx;
                        if (hit_idx >= 0) {
                            g_selected_cutout_idx = hit_idx;
                            g_overlay_active_tab = 0;
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                } else if (g_hovered_cutout_idx != -1) {
                    g_hovered_cutout_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }

                /* Check sprites hover */
                if (g_has_canvas && g_sprite_count > 0) {
                    int hit_idx = -1;
                    for (int i = g_sprite_count - 1; i >= 0; i--) {
                        int sw = g_sprites[i].has_img ? g_sprites[i].img.width : (g_has_default_sprite_img ? g_default_sprite_img.width : 120);
                        int sh = g_sprites[i].has_img ? g_sprites[i].img.height : (g_has_default_sprite_img ? g_default_sprite_img.height : 120);
                        if (g_canvas_mouse_x >= (float)g_sprites[i].x &&
                            g_canvas_mouse_x <= (float)(g_sprites[i].x + sw) &&
                            g_canvas_mouse_y >= (float)g_sprites[i].y &&
                            g_canvas_mouse_y <= (float)(g_sprites[i].y + sh)) {
                            hit_idx = i;
                            break;
                        }
                    }
                    if (hit_idx != g_hovered_sprite_idx) {
                        g_hovered_sprite_idx = hit_idx;
                        if (hit_idx >= 0) {
                            g_selected_sprite_idx = hit_idx;
                            g_overlay_active_tab = 1;
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                } else if (g_hovered_sprite_idx != -1) {
                    g_hovered_sprite_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }

                /* Check bgalphas hover: ONLY IF cursor is NOT hovering Cutout or Sprite! */
                if (g_hovered_cutout_idx == -1 && g_hovered_sprite_idx == -1 && g_has_canvas && g_bg_alpha_count > 0) {
                    int hit_idx = -1;
                    for (int i = g_bg_alpha_count - 1; i >= 0; i--) {
                        int bw = g_bg_alphas[i].has_img ? g_bg_alphas[i].img.width : (g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 160);
                        int bh = g_bg_alphas[i].has_img ? g_bg_alphas[i].img.height : (g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 140);
                        if (g_canvas_mouse_x >= (float)g_bg_alphas[i].x &&
                            g_canvas_mouse_x <= (float)(g_bg_alphas[i].x + bw) &&
                            g_canvas_mouse_y >= (float)g_bg_alphas[i].y &&
                            g_canvas_mouse_y <= (float)(g_bg_alphas[i].y + bh)) {
                            hit_idx = i;
                            break;
                        }
                    }
                    if (hit_idx != g_hovered_bgalpha_idx) {
                        g_hovered_bgalpha_idx = hit_idx;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                } else if (g_hovered_bgalpha_idx != -1) {
                    g_hovered_bgalpha_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else if (g_mode_element) {
                /* In Element Mode: ONLY Frog and Coins can be hovered and edited! */
                if (g_hovered_cutout_idx != -1) {
                    g_hovered_cutout_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                if (g_hovered_sprite_idx != -1) {
                    g_hovered_sprite_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                if (g_hovered_bgalpha_idx != -1) {
                    g_hovered_bgalpha_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }

                /* Check frog hover (Element Mode only) */
                if (g_has_frog && g_has_canvas) {
                    int half_w = g_has_frog_img ? (g_frog_img.width / 2) : 54;
                    int half_h = g_has_frog_img ? (g_frog_img.height / 2) : 54;
                    bool is_over = (g_canvas_mouse_x >= (float)(g_frog_gx - half_w) &&
                                    g_canvas_mouse_x <= (float)(g_frog_gx + half_w) &&
                                    g_canvas_mouse_y >= (float)(g_frog_gy - half_h) &&
                                    g_canvas_mouse_y <= (float)(g_frog_gy + half_h));
                    if (is_over != g_frog_hovered) {
                        g_frog_hovered = is_over;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }

                /* Check coins hover (Element Mode only) */
                if (g_has_canvas && g_treasure_point_count > 0) {
                    int coin_half_w = g_has_coin_img ? (g_coin_img.width / 2) : 18;
                    int coin_half_h = g_has_coin_img ? (g_coin_img.height / 2) : 18;
                    int hit_idx = -1;
                    for (int i = g_treasure_point_count - 1; i >= 0; i--) {
                        if (g_canvas_mouse_x >= (float)(g_treasure_points[i].x - coin_half_w) &&
                            g_canvas_mouse_x <= (float)(g_treasure_points[i].x + coin_half_w) &&
                            g_canvas_mouse_y >= (float)(g_treasure_points[i].y - coin_half_h) &&
                            g_canvas_mouse_y <= (float)(g_treasure_points[i].y + coin_half_h)) {
                            hit_idx = i;
                            break;
                        }
                    }
                    if (hit_idx != g_hovered_coin_idx) {
                        g_hovered_coin_idx = hit_idx;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                } else if (g_hovered_coin_idx != -1) {
                    g_hovered_coin_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else {
                /* In Curve Mode or Default View-Only Mode: reset all hover states */
                if (g_frog_hovered) {
                    g_frog_hovered = false;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                if (g_hovered_coin_idx != -1) {
                    g_hovered_coin_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                if (g_hovered_cutout_idx != -1) {
                    g_hovered_cutout_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                if (g_hovered_sprite_idx != -1) {
                    g_hovered_sprite_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                if (g_hovered_bgalpha_idx != -1) {
                    g_hovered_bgalpha_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }

                /* Hit-test hover for Map Attribute HUD in Preview Mode */
                if (!g_mode_canvas && !g_mode_element && !g_mode_curve && !g_mode_overlay &&
                    !g_hud_preview_info_collapsed && g_has_canvas) {
                    RECT rc_pv;
                    RECT rc_c;
                    GetClientRect(hwnd, &rc_c);
                    int client_w = rc_c.right - rc_c.left;
                    int work_h = get_workspace_height(hwnd);
                    get_top_right_preview_hud_rect(client_w, work_h, &rc_pv);

                    if (mx >= rc_pv.left && mx <= rc_pv.right && my >= rc_pv.top && my <= rc_pv.bottom) {
                        int rel_x = mx - rc_pv.left;
                        int rel_y = my - rc_pv.top;
                        int tr_w = rc_pv.right - rc_pv.left;
                        int anchor_y = 0;
                        MapAttrHoverItem h_item = hit_test_map_attr_hover(rel_x, rel_y, tr_w, &anchor_y);
                        if (h_item != g_hud_map_attr_hover || anchor_y != g_hud_map_attr_hover_y) {
                            g_hud_map_attr_hover = h_item;
                            g_hud_map_attr_hover_y = anchor_y;
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                        }
                    } else if (g_hud_map_attr_hover != MAP_ATTR_HOVER_NONE) {
                        POINT pt_screen = { mx, my };
                        ClientToScreen(hwnd, &pt_screen);
                        HWND hwnd_pt = WindowFromPoint(pt_screen);
                        bool over_edit = (hwnd_pt == g_edit_preview_title ||
                                          hwnd_pt == g_edit_map_skullrot ||
                                          hwnd_pt == g_edit_dev_curvecolor ||
                                          hwnd_pt == g_edit_dev_bgcolor ||
                                          hwnd_pt == g_edit_dev_huerot ||
                                          hwnd_pt == g_edit_dev_treasurefreq ||
                                          hwnd_pt == g_edit_dev_partime);
                        if (!over_edit) {
                            g_hud_map_attr_hover = MAP_ATTR_HOVER_NONE;
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                        }
                    }
                } else if (g_hud_map_attr_hover != MAP_ATTR_HOVER_NONE) {
                    g_hud_map_attr_hover = MAP_ATTR_HOVER_NONE;
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                }
            }

            if (g_mode_curve && g_has_canvas) {
                if (g_curve_submode == CURVE_SUBMODE_VARIABLE) {
                    if (g_var_dragging) {
                        if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count && g_paths[g_active_path_idx].node_count >= 2) {
                            BezierPath *bp = &g_paths[g_active_path_idx];
                            double dist_sq = 0.0;
                            double s = project_s_window(bp, g_canvas_mouse_x, g_canvas_mouse_y, g_var_drag_s_cur, 40.0, &dist_sq);
                            g_var_drag_s_cur = s;

                            if (bp->cache.count >= 2 && bp->cache.S && bp->cache.x && bp->cache.y) {
                                const PathCache *c = &bp->cache;
                                int i0 = 0;
                                while (i0 < c->count - 1 && c->S[i0 + 1] < s) i0++;
                                double t0 = (c->S[i0 + 1] > c->S[i0]) ? (s - c->S[i0]) / (c->S[i0 + 1] - c->S[i0]) : 0.0;
                                if (t0 < 0.0) t0 = 0.0;
                                if (t0 > 1.0) t0 = 1.0;
                                g_var_hover_x = (float)((1.0 - t0) * c->x[i0] + t0 * c->x[i0 + 1]);
                                g_var_hover_y = (float)((1.0 - t0) * c->y[i0] + t0 * c->y[i0 + 1]);
                            } else {
                                g_var_hover_x = g_canvas_mouse_x;
                                g_var_hover_y = g_canvas_mouse_y;
                            }
                            g_var_hover_active = true;
                            g_var_hover_s = s;
                            bezier_path_cache_attrs_at(&bp->cache, s, &g_var_hover_tun, &g_var_hover_prio);
                        }
                    } else {
                        if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count && g_paths[g_active_path_idx].node_count >= 2) {
                            BezierPath *bp = &g_paths[g_active_path_idx];
                            double dist_sq = 0.0;
                            double s = project_s_window(bp, g_canvas_mouse_x, g_canvas_mouse_y, -1.0, 0.0, &dist_sq);
                            if (dist_sq <= 25.0 * 25.0) {
                                if (bp->cache.count >= 2 && bp->cache.S && bp->cache.x && bp->cache.y) {
                                    const PathCache *c = &bp->cache;
                                    int i0 = 0;
                                    while (i0 < c->count - 1 && c->S[i0 + 1] < s) i0++;
                                    double t0 = (c->S[i0 + 1] > c->S[i0]) ? (s - c->S[i0]) / (c->S[i0 + 1] - c->S[i0]) : 0.0;
                                    if (t0 < 0.0) t0 = 0.0;
                                    if (t0 > 1.0) t0 = 1.0;
                                    g_var_hover_x = (float)((1.0 - t0) * c->x[i0] + t0 * c->x[i0 + 1]);
                                    g_var_hover_y = (float)((1.0 - t0) * c->y[i0] + t0 * c->y[i0 + 1]);
                                } else {
                                    g_var_hover_x = g_canvas_mouse_x;
                                    g_var_hover_y = g_canvas_mouse_y;
                                }
                                g_var_hover_active = true;
                                g_var_hover_s = s;
                                bezier_path_cache_attrs_at(&bp->cache, s, &g_var_hover_tun, &g_var_hover_prio);
                            } else {
                                g_var_hover_active = false;
                            }
                        } else {
                            g_var_hover_active = false;
                        }
                    }
                } else {
                    g_var_hover_active = false;
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }

            update_status_bar();
            return 0;
        }

        case WM_MOUSELEAVE: {
            g_var_hover_active = false;
            if (g_frog_hovered) {
                g_frog_hovered = false;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (g_hovered_coin_idx != -1) {
                g_hovered_coin_idx = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (g_hovered_cutout_idx != -1) {
                g_hovered_cutout_idx = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (g_hovered_sprite_idx != -1) {
                g_hovered_sprite_idx = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (g_hovered_bgalpha_idx != -1) {
                g_hovered_bgalpha_idx = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (g_hud_map_attr_hover != MAP_ATTR_HOVER_NONE) {
                g_hud_map_attr_hover = MAP_ATTR_HOVER_NONE;
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
            }
            if (g_placing_frog || g_placing_coin || g_placing_cutout || g_placing_sprite || g_placing_bgalpha || g_placing_bgalpha_vector || g_mode_curve) {
                g_mouse_screen_x = -1000;
                g_mouse_screen_y = -1000;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            if (g_placing_frog && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);

                float cx, cy;
                screen_to_canvas(client_w, work_h, mx, my, &cx, &cy);
                undo_ensure_baseline();
                g_frog_gx = (int)roundf(cx);
                g_frog_gy = (int)roundf(cy);
                g_has_frog = true;
                g_placing_frog = false;
                g_frog_hovered = true;
                undo_snapshot_capture("Set Frog Position");

                if (g_btn_frog) InvalidateRect(g_btn_frog, NULL, TRUE);
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            if (g_placing_coin && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                if (g_treasure_point_count < MAX_TREASURE_POINTS) {
                    RECT rc_client;
                    GetClientRect(hwnd, &rc_client);
                    int client_w = rc_client.right - rc_client.left;
                    int work_h = get_workspace_height(hwnd);

                    float cx, cy;
                    screen_to_canvas(client_w, work_h, mx, my, &cx, &cy);
                    int coin_x = (int)roundf(cx);
                    int coin_y = (int)roundf(cy);
                    undo_ensure_baseline();
                    g_treasure_points[g_treasure_point_count].x = coin_x;
                    g_treasure_points[g_treasure_point_count].y = coin_y;
                    g_treasure_points[g_treasure_point_count].dist1 = 35;
                    g_hovered_coin_idx = g_treasure_point_count;
                    g_treasure_point_count++;
                    undo_snapshot_capture("Add Coin");
                }
                g_placing_coin = false;
                if (g_btn_coin) InvalidateRect(g_btn_coin, NULL, TRUE);
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            if (g_placing_bgalpha && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                if (!g_placing_bgalpha_vector) {
                    /* Step 1: Click sets (X, Y) anchor and locks position, then activates vector selection */
                    RECT rc_client;
                    GetClientRect(hwnd, &rc_client);
                    int client_w = rc_client.right - rc_client.left;
                    int work_h = get_workspace_height(hwnd);

                    float cx, cy;
                    screen_to_canvas(client_w, work_h, mx, my, &cx, &cy);
                    g_bgalpha_anchor_x = (int)roundf(cx);
                    g_bgalpha_anchor_y = (int)roundf(cy);
                    g_placing_bgalpha_vector = true;
                    g_bgalpha_pending_vx = 0;
                    g_bgalpha_pending_vy = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                } else {
                    /* Step 2: 2nd click confirms vector (vx, vy) and finalizes BackgroundAlpha */
                    if (g_bg_alpha_count < MAX_BG_ALPHAS) {
                        undo_ensure_baseline();
                        LevelBackgroundAlpha *ba = &g_bg_alphas[g_bg_alpha_count];
                        memset(ba, 0, sizeof(LevelBackgroundAlpha));
                        ba->x = g_bgalpha_anchor_x;
                        ba->y = g_bgalpha_anchor_y;
                        ba->vx = g_bgalpha_pending_vx;
                        ba->vy = g_bgalpha_pending_vy;
                        ba->pri = 0;
                        if (g_has_pending_bgalpha_img) {
                            ba->img = g_pending_bgalpha_img;
                            ba->has_img = true;
                            strncpy(ba->source_filepath, g_pending_bgalpha_filepath, sizeof(ba->source_filepath) - 1);
                            memset(&g_pending_bgalpha_img, 0, sizeof(CanvasImage));
                            g_has_pending_bgalpha_img = false;
                            g_pending_bgalpha_filepath[0] = '\0';
                        } else {
                            strncpy(ba->image_name, "quake", sizeof(ba->image_name) - 1);
                            if (g_canvas_folder[0] && resolve_bgalpha_image_in_folder(g_canvas_folder, ba->image_name, &ba->img)) {
                                ba->has_img = true;
                            }
                        }
                        bgalpha_generate_masks(ba);
                        g_hovered_bgalpha_idx = g_bg_alpha_count;
                        g_bg_alpha_count++;
                        sync_bgalpha_image_names();
                        deselect_all_objects();
                        g_bg_alphas[g_bg_alpha_count - 1].selected = true;
                        g_selected_bgalpha_idx = g_bg_alpha_count - 1;
                        undo_snapshot_capture("Add Background Alpha");
                    }
                    g_placing_bgalpha = false;
                    g_placing_bgalpha_vector = false;
                    free_pending_bgalpha();
                    if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
            }

            if (g_placing_cutout && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                if (g_cutout_count < MAX_CUTOUTS) {
                    RECT rc_client;
                    GetClientRect(hwnd, &rc_client);
                    int client_w = rc_client.right - rc_client.left;
                    int work_h = get_workspace_height(hwnd);

                    float cx, cy;
                    screen_to_canvas(client_w, work_h, mx, my, &cx, &cy);
                    int cut_x = (int)roundf(cx);
                    int cut_y = (int)roundf(cy);

                    undo_ensure_baseline();
                    LevelCutout *co = &g_cutouts[g_cutout_count];
                    memset(co, 0, sizeof(LevelCutout));
                    co->x = cut_x;
                    co->y = cut_y;
                    co->pri = g_overlay_current_prio;
                    if (g_has_pending_cutout_img) {
                        co->img = g_pending_cutout_img;
                        co->has_img = true;
                        strncpy(co->source_filepath, g_pending_cutout_filepath, sizeof(co->source_filepath) - 1);
                        co->hbm_mask_light = g_pending_cutout_mask_light;
                        co->hdc_mask_light = g_pending_cutout_hdc_light;
                        co->hbm_mask_dark  = g_pending_cutout_mask_dark;
                        co->hdc_mask_dark  = g_pending_cutout_hdc_dark;

                        memset(&g_pending_cutout_img, 0, sizeof(CanvasImage));
                        g_has_pending_cutout_img = false;
                        g_pending_cutout_mask_light = NULL;
                        g_pending_cutout_hdc_light  = NULL;
                        g_pending_cutout_mask_dark  = NULL;
                        g_pending_cutout_hdc_dark   = NULL;
                        g_pending_cutout_filepath[0] = '\0';
                    } else {
                        strncpy(co->image_name, "tunnel", sizeof(co->image_name) - 1);
                        if (g_canvas_folder[0] && resolve_cutout_image_in_folder(g_canvas_folder, co->image_name, &co->img)) {
                            co->has_img = true;
                        }
                        cutout_generate_masks(co);
                    }
                    g_hovered_cutout_idx = g_cutout_count;
                    g_cutout_count++;
                    sync_cutout_image_names();
                    g_selected_cutout_idx = g_cutout_count - 1;
                    g_overlay_active_tab = 0;
                    undo_snapshot_capture("Add Cutout Overlay");
                }
                g_placing_cutout = false;
                free_pending_cutout();
                if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            if (g_placing_sprite && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                if (g_sprite_count < MAX_SPRITES) {
                    RECT rc_client;
                    GetClientRect(hwnd, &rc_client);
                    int client_w = rc_client.right - rc_client.left;
                    int work_h = get_workspace_height(hwnd);

                    float cx, cy;
                    screen_to_canvas(client_w, work_h, mx, my, &cx, &cy);
                    int spr_x = (int)roundf(cx);
                    int spr_y = (int)roundf(cy);

                    undo_ensure_baseline();
                    LevelSprite *sp = &g_sprites[g_sprite_count];
                    memset(sp, 0, sizeof(LevelSprite));
                    sp->x = spr_x;
                    sp->y = spr_y;
                    sp->pri = g_overlay_current_prio;
                    if (g_has_pending_sprite_img) {
                        sp->img = g_pending_sprite_img;
                        sp->has_img = true;
                        strncpy(sp->source_filepath, g_pending_sprite_filepath, sizeof(sp->source_filepath) - 1);

                        memset(&g_pending_sprite_img, 0, sizeof(CanvasImage));
                        g_has_pending_sprite_img = false;
                        g_pending_sprite_filepath[0] = '\0';
                    } else {
                        strncpy(sp->image_name, "sprite", sizeof(sp->image_name) - 1);
                        if (g_canvas_folder[0] && resolve_sprite_image_in_folder(g_canvas_folder, sp->image_name, &sp->img)) {
                            sp->has_img = true;
                        }
                    }
                    g_hovered_sprite_idx = g_sprite_count;
                    g_sprite_count++;
                    sync_sprite_image_names();
                    g_selected_sprite_idx = g_sprite_count - 1;
                    g_overlay_active_tab = 1;
                    undo_snapshot_capture("Add Sprite Decoration");
                }
                g_placing_sprite = false;
                free_pending_sprite();
                if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            /* 1. Hit-test Top-Left Overlay Guide HUD & Top-Right Overlay Tools HUD when Overlay Mode is ON */
            if (g_mode_overlay && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);

                /* Hit-test Top-Left Overlay Guide HUD */
                RECT rc_guide;
                get_top_left_overlay_guide_hud_rect(work_h, &rc_guide);
                if (mx >= rc_guide.left && mx <= rc_guide.right && my >= rc_guide.top && my <= rc_guide.bottom) {
                    if (g_hud_overlay_guide_collapsed) {
                        g_hud_overlay_guide_collapsed = false;
                        g_hud_overlay_guide_scroll_y = 0;
                    } else {
                        int rel_x = mx - rc_guide.left;
                        int rel_y = my - rc_guide.top;
                        int tl_w = rc_guide.right - rc_guide.left;
                        if (rel_y <= 26 && rel_x >= tl_w - 24) {
                            g_hud_overlay_guide_collapsed = true;
                            g_hud_overlay_guide_scroll_y = 0;
                        } else {
                            int track_y, track_h, thumb_y, thumb_h;
                            int max_s = get_overlay_guide_hud_max_scroll(work_h);
                            if (max_s > 0 && get_overlay_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h) &&
                                rel_x >= tl_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                                if (rel_y >= thumb_y && rel_y <= thumb_y + thumb_h) {
                                    g_is_dragging_ov_guide_thumb = true;
                                    g_drag_ov_guide_mouse_start_y = my;
                                    g_drag_ov_guide_scroll_start = g_hud_overlay_guide_scroll_y;
                                    SetCapture(hwnd);
                                } else {
                                    int travel = track_h - thumb_h;
                                    if (travel > 0) {
                                        float ratio = (float)(rel_y - track_y - thumb_h / 2) / (float)travel;
                                        if (ratio < 0.0f) ratio = 0.0f;
                                        if (ratio > 1.0f) ratio = 1.0f;
                                        g_hud_overlay_guide_scroll_y = (int)roundf(ratio * (float)max_s);
                                        g_is_dragging_ov_guide_thumb = true;
                                        g_drag_ov_guide_mouse_start_y = my;
                                        g_drag_ov_guide_scroll_start = g_hud_overlay_guide_scroll_y;
                                        SetCapture(hwnd);
                                    }
                                }
                            } else if (max_s > 0) {
                                g_is_dragging_ov_guide_bg = true;
                                g_drag_ov_guide_bg_mouse_start_y = my;
                                g_drag_ov_guide_bg_scroll_start = g_hud_overlay_guide_scroll_y;
                                SetCapture(hwnd);
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }

                /* Hit-test Top-Right Overlay Tools HUD */
                RECT rc_ov;
                get_top_right_overlay_hud_rect(client_w, work_h, &rc_ov);
                if (mx >= rc_ov.left && mx <= rc_ov.right && my >= rc_ov.top && my <= rc_ov.bottom) {
                    if (g_hud_overlay_collapsed) {
                        g_hud_overlay_collapsed = false;
                        g_hud_overlay_tools_scroll_y = 0;
                    } else {
                        int rel_x = mx - rc_ov.left;
                        int rel_y = my - rc_ov.top;
                        int tr_w = rc_ov.right - rc_ov.left;

                        if (rel_x >= tr_w - 24 && rel_y <= 24) {
                            /* Collapse button [^] */
                            g_hud_overlay_collapsed = true;
                            g_hud_overlay_tools_scroll_y = 0;
                            if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
                        } else {
                            int track_y, track_h, thumb_y, thumb_h;
                            int max_s = get_overlay_tools_hud_max_scroll(work_h);
                            if (max_s > 0 && get_overlay_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h) &&
                                rel_x >= tr_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                                if (rel_y >= thumb_y && rel_y <= thumb_y + thumb_h) {
                                    g_is_dragging_ov_tools_thumb = true;
                                    g_drag_ov_tools_mouse_start_y = my;
                                    g_drag_ov_tools_scroll_start = g_hud_overlay_tools_scroll_y;
                                    SetCapture(hwnd);
                                } else {
                                    int travel = track_h - thumb_h;
                                    if (travel > 0) {
                                        float ratio = (float)(rel_y - track_y - thumb_h / 2) / (float)travel;
                                        if (ratio < 0.0f) ratio = 0.0f;
                                        if (ratio > 1.0f) ratio = 1.0f;
                                        g_hud_overlay_tools_scroll_y = (int)roundf(ratio * (float)max_s);
                                        g_is_dragging_ov_tools_thumb = true;
                                        g_drag_ov_tools_mouse_start_y = my;
                                        g_drag_ov_tools_scroll_start = g_hud_overlay_tools_scroll_y;
                                        SetCapture(hwnd);
                                    }
                                }
                            } else {
                                int content_y = rel_y + g_hud_overlay_tools_scroll_y;
                                bool hit_btn = false;
                                if (rel_x >= 8 && rel_x <= 228 && content_y >= 24 && content_y <= 46) {
                                    /* Row 1 (atas, full width): [+ Background Alpha] */
                                    hit_btn = true;
                                    trigger_add_bgalpha(hwnd);
                                    return 0;
                                } else if (content_y >= 50 && content_y <= 72) {
                                    /* Row 2: Bawah kiri: [+ Sprite], Bawah kanan: [+ Cutout] */
                                    if (rel_x >= 8 && rel_x <= 114) {
                                        hit_btn = true;
                                        trigger_add_sprite(hwnd);
                                        return 0;
                                    } else if (rel_x >= 120 && rel_x <= 228) {
                                        hit_btn = true;
                                        trigger_add_cutout(hwnd);
                                        return 0;
                                    }
                                } else if (content_y >= 76 && content_y <= 98) {
                                    if (rel_x >= 8 && rel_x <= 104) {
                                        /* Row 3 Kiri: Tombol [Set Variable / Set Motion / Set Priority] */
                                        hit_btn = true;
                                        toggle_overlay_variable_mode(hwnd);
                                        return 0;
                                    } else if (rel_x >= 172 && rel_x <= 228) {
                                        /* Row 3 Kanan: Priority Edit Box Focus */
                                        hit_btn = true;
                                        if (g_edit_prio) {
                                            SetFocus(g_edit_prio);
                                            SendMessage(g_edit_prio, EM_SETSEL, 0, -1);
                                        }
                                    }
                                }

                                if (!hit_btn && max_s > 0) {
                                    g_is_dragging_ov_tools_bg = true;
                                    g_drag_ov_tools_bg_mouse_start_y = my;
                                    g_drag_ov_tools_bg_scroll_start = g_hud_overlay_tools_scroll_y;
                                    SetCapture(hwnd);
                                }
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
            }

            /* 1b. Hit-test Top-Left Element Guide HUD & Top-Right Element Tools HUD when Element Mode is ON */
            if (g_mode_element && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);

                /* Hit-test Top-Left Element Guide HUD */
                RECT rc_guide;
                get_top_left_element_guide_hud_rect(work_h, &rc_guide);
                if (mx >= rc_guide.left && mx <= rc_guide.right && my >= rc_guide.top && my <= rc_guide.bottom) {
                    if (g_hud_element_guide_collapsed) {
                        g_hud_element_guide_collapsed = false;
                        g_hud_element_guide_scroll_y = 0;
                    } else {
                        int rel_x = mx - rc_guide.left;
                        int rel_y = my - rc_guide.top;
                        int tl_w = rc_guide.right - rc_guide.left;
                        if (rel_y <= 26 && rel_x >= tl_w - 24) {
                            g_hud_element_guide_collapsed = true;
                            g_hud_element_guide_scroll_y = 0;
                        } else {
                            int track_y, track_h, thumb_y, thumb_h;
                            int max_s = get_element_guide_hud_max_scroll(work_h);
                            if (max_s > 0 && get_element_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h) &&
                                rel_x >= tl_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                                if (rel_y >= thumb_y && rel_y <= thumb_y + thumb_h) {
                                    g_is_dragging_el_guide_thumb = true;
                                    g_drag_el_guide_mouse_start_y = my;
                                    g_drag_el_guide_scroll_start = g_hud_element_guide_scroll_y;
                                    SetCapture(hwnd);
                                } else {
                                    int travel = track_h - thumb_h;
                                    if (travel > 0) {
                                        float ratio = (float)(rel_y - track_y - thumb_h / 2) / (float)travel;
                                        if (ratio < 0.0f) ratio = 0.0f;
                                        if (ratio > 1.0f) ratio = 1.0f;
                                        g_hud_element_guide_scroll_y = (int)roundf(ratio * (float)max_s);
                                        g_is_dragging_el_guide_thumb = true;
                                        g_drag_el_guide_mouse_start_y = my;
                                        g_drag_el_guide_scroll_start = g_hud_element_guide_scroll_y;
                                        SetCapture(hwnd);
                                    }
                                }
                            } else if (max_s > 0) {
                                g_is_dragging_el_guide_bg = true;
                                g_drag_el_guide_bg_mouse_start_y = my;
                                g_drag_el_guide_bg_scroll_start = g_hud_element_guide_scroll_y;
                                SetCapture(hwnd);
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }

                /* Hit-test Top-Right Element Tools HUD */
                RECT rc_el;
                get_top_right_element_hud_rect(client_w, work_h, &rc_el);
                if (mx >= rc_el.left && mx <= rc_el.right && my >= rc_el.top && my <= rc_el.bottom) {
                    if (g_hud_element_collapsed) {
                        g_hud_element_collapsed = false;
                        g_hud_element_tools_scroll_y = 0;
                    } else {
                        int rel_x = mx - rc_el.left;
                        int rel_y = my - rc_el.top;
                        int tr_w = rc_el.right - rc_el.left;

                        if (rel_x >= tr_w - 24 && rel_y <= 24) {
                            /* Collapse button [^] */
                            g_hud_element_collapsed = true;
                            g_hud_element_tools_scroll_y = 0;
                        } else {
                            int track_y, track_h, thumb_y, thumb_h;
                            int max_s = get_element_tools_hud_max_scroll(work_h);
                            if (max_s > 0 && get_element_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h) &&
                                rel_x >= tr_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                                if (rel_y >= thumb_y && rel_y <= thumb_y + thumb_h) {
                                    g_is_dragging_el_tools_thumb = true;
                                    g_drag_el_tools_mouse_start_y = my;
                                    g_drag_el_tools_scroll_start = g_hud_element_tools_scroll_y;
                                    SetCapture(hwnd);
                                } else {
                                    int travel = track_h - thumb_h;
                                    if (travel > 0) {
                                        float ratio = (float)(rel_y - track_y - thumb_h / 2) / (float)travel;
                                        if (ratio < 0.0f) ratio = 0.0f;
                                        if (ratio > 1.0f) ratio = 1.0f;
                                        g_hud_element_tools_scroll_y = (int)roundf(ratio * (float)max_s);
                                        g_is_dragging_el_tools_thumb = true;
                                        g_drag_el_tools_mouse_start_y = my;
                                        g_drag_el_tools_scroll_start = g_hud_element_tools_scroll_y;
                                        SetCapture(hwnd);
                                    }
                                }
                            } else {
                                int content_y = rel_y + g_hud_element_tools_scroll_y;
                                bool hit_btn = false;
                                if (content_y >= 24 && content_y <= 46) {
                                    /* Row 1: Left: [+ Frog], Right: [+ Coin] */
                                    if (rel_x >= 8 && rel_x <= 114) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_FROG, BN_CLICKED), 0);
                                        return 0;
                                    } else if (rel_x >= 120 && rel_x <= 228) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_COIN, BN_CLICKED), 0);
                                        return 0;
                                    }
                                }

                                if (!hit_btn && max_s > 0) {
                                    g_is_dragging_el_tools_bg = true;
                                    g_drag_el_tools_bg_mouse_start_y = my;
                                    g_drag_el_tools_bg_scroll_start = g_hud_element_tools_scroll_y;
                                    SetCapture(hwnd);
                                }
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
            }

            /* Hit-test Top-Left Preview Guide HUD & Top-Right Canvas Info HUD when in Preview Mode (all edit modes OFF) */
            if (!g_mode_canvas && !g_mode_element && !g_mode_curve && !g_mode_overlay && my >= TOOLBAR_HEIGHT) {
                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);

                /* Hit-test Top-Left Preview Guide HUD */
                RECT rc_guide;
                get_top_left_preview_guide_hud_rect(work_h, &rc_guide);
                if (mx >= rc_guide.left && mx <= rc_guide.right && my >= rc_guide.top && my <= rc_guide.bottom) {
                    if (g_hud_preview_guide_collapsed) {
                        g_hud_preview_guide_collapsed = false;
                        g_hud_preview_guide_scroll_y = 0;
                    } else {
                        int rel_x = mx - rc_guide.left;
                        int rel_y = my - rc_guide.top;
                        int tl_w = rc_guide.right - rc_guide.left;
                        if (rel_y <= 26 && rel_x >= tl_w - 24) {
                            g_hud_preview_guide_collapsed = true;
                            g_hud_preview_guide_scroll_y = 0;
                        } else {
                            int track_y, track_h, thumb_y, thumb_h;
                            int max_s = get_preview_guide_hud_max_scroll(work_h);
                            if (max_s > 0 && get_preview_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h) &&
                                rel_x >= tl_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                                if (rel_y >= thumb_y && rel_y <= thumb_y + thumb_h) {
                                    g_is_dragging_prev_guide_thumb = true;
                                    g_drag_prev_guide_mouse_start_y = my;
                                    g_drag_prev_guide_scroll_start = g_hud_preview_guide_scroll_y;
                                    SetCapture(hwnd);
                                } else {
                                    int travel = track_h - thumb_h;
                                    if (travel > 0) {
                                        float ratio = (float)(rel_y - track_y - thumb_h / 2) / (float)travel;
                                        if (ratio < 0.0f) ratio = 0.0f;
                                        if (ratio > 1.0f) ratio = 1.0f;
                                        g_hud_preview_guide_scroll_y = (int)roundf(ratio * (float)max_s);
                                        g_is_dragging_prev_guide_thumb = true;
                                        g_drag_prev_guide_mouse_start_y = my;
                                        g_drag_prev_guide_scroll_start = g_hud_preview_guide_scroll_y;
                                        SetCapture(hwnd);
                                    }
                                }
                            } else if (max_s > 0) {
                                g_is_dragging_prev_guide_bg = true;
                                g_drag_prev_guide_bg_mouse_start_y = my;
                                g_drag_prev_guide_bg_scroll_start = g_hud_preview_guide_scroll_y;
                                SetCapture(hwnd);
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }

                /* Hit-test Top-Right Map Attribute HUD */
                RECT rc_pv;
                get_top_right_preview_hud_rect(client_w, work_h, &rc_pv);
                if (mx >= rc_pv.left && mx <= rc_pv.right && my >= rc_pv.top && my <= rc_pv.bottom) {
                    if (g_hud_preview_info_collapsed) {
                        g_hud_preview_info_collapsed = false;
                        g_hud_preview_info_scroll_y = 0;
                    } else {
                        int rel_x = mx - rc_pv.left;
                        int rel_y = my - rc_pv.top;
                        int tr_w = rc_pv.right - rc_pv.left;
                        if (rel_y <= 26 && rel_x >= tr_w - 24) {
                            g_hud_preview_info_collapsed = true;
                            g_hud_preview_info_scroll_y = 0;
                            hide_all_map_attr_edits();
                        } else {
                            int track_y, track_h, thumb_y, thumb_h;
                            int max_s = get_preview_hud_max_scroll(work_h);
                            if (max_s > 0 && get_preview_hud_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h) &&
                                rel_x >= tr_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                                if (rel_y >= thumb_y && rel_y <= thumb_y + thumb_h) {
                                    g_is_dragging_prev_info_thumb = true;
                                    g_drag_prev_info_mouse_start_y = my;
                                    g_drag_prev_info_scroll_start = g_hud_preview_info_scroll_y;
                                    SetCapture(hwnd);
                                } else {
                                    int travel = track_h - thumb_h;
                                    if (travel > 0) {
                                        float ratio = (float)(rel_y - track_y - thumb_h / 2) / (float)travel;
                                        if (ratio < 0.0f) ratio = 0.0f;
                                        if (ratio > 1.0f) ratio = 1.0f;
                                        g_hud_preview_info_scroll_y = (int)roundf(ratio * (float)max_s);
                                        g_is_dragging_prev_info_thumb = true;
                                        g_drag_prev_info_mouse_start_y = my;
                                        g_drag_prev_info_scroll_start = g_hud_preview_info_scroll_y;
                                        SetCapture(hwnd);
                                    }
                                }
                            } else {
                                MapAttrHitTarget hit = hit_test_map_attr_hud(rel_x, rel_y, tr_w);
                                bool hit_btn = false;

                                if (hit != MAP_ATTR_HIT_NONE) {
                                    hit_btn = true;
                                    switch (hit) {
                                        case MAP_ATTR_HIT_PLAY_BTN:
                                            toggle_level_preview(hwnd);
                                            return 0;

                                        case MAP_ATTR_HIT_CHECK_SKULLROT:
                                            g_map_has_skullrot = !g_map_has_skullrot;
                                            if (!g_map_has_skullrot && g_edit_map_skullrot) {
                                                ShowWindow(g_edit_map_skullrot, SW_HIDE);
                                            } else if (g_map_has_skullrot && g_edit_map_skullrot) {
                                                char buf[32];
                                                snprintf(buf, sizeof(buf), "%.1f", g_map_skullrot);
                                                SetWindowTextA(g_edit_map_skullrot, buf);
                                                ShowWindow(g_edit_map_skullrot, SW_SHOW);
                                            }
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            return 0;

                                        case MAP_ATTR_HIT_CHECK_SPACE:
                                            if (!g_map_is_space) {
                                                const char *space_warn =
                                                    "Enabling space=\"true\" converts this level into a procedural space arena in Zuma Deluxe.\r\n\r\n"
                                                    "CRITICAL: The following attributes and visual layers will be IGNORED or overridden by the Zuma game engine:\r\n\r\n"
                                                    " * Background Graphic (image): Completely ignored. Zuma replaces it with a procedural field of 1,000 3D stars and a scrolling nebula.\r\n"
                                                    " * Background Color (bgcolor): Ignored. Zuma forces the cosmic space void.\r\n"
                                                    " * Hue Rotation (huerot): Ignored for background rendering.\r\n"
                                                    " * BackgroundAlpha Cutouts: Static atmospheric and earthquake layers will not render against the space starfield.\r\n"
                                                    " * Cutout Layers: May float awkwardly over dynamic space particles.\r\n"
                                                    " * Sprite Layers: Ignored or may float awkwardly over dynamic space particles.\r\n\r\n"
                                                    "Do you want to enable Space mode for this map?";
                                                int res = show_themed_choice_dialog_ex(hwnd, "Space Mode Warning", "Procedural Space Arena Activated", space_warn, "Enable Space", "Cancel", NULL, 600, 420);
                                                if (res == 1) {
                                                    g_map_is_space = true;
                                                }
                                            } else {
                                                g_map_is_space = false;
                                            }
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            return 0;

                                        case MAP_ATTR_HIT_CHECK_DRAWCURVE:
                                            g_dev_has_drawcurve = !g_dev_has_drawcurve;
                                            g_dev_drawcurve = g_dev_has_drawcurve;
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            return 0;

                                        case MAP_ATTR_HIT_CHECK_DRAWTUNNELS:
                                            g_dev_has_drawtunnels = !g_dev_has_drawtunnels;
                                            g_dev_drawtunnels = g_dev_has_drawtunnels;
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            return 0;

                                        case MAP_ATTR_HIT_CHECK_CURVECOLOR:
                                            g_dev_has_curvecolor = !g_dev_has_curvecolor;
                                            if (!g_dev_has_curvecolor && g_edit_dev_curvecolor) ShowWindow(g_edit_dev_curvecolor, SW_HIDE);
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            return 0;

                                        case MAP_ATTR_HIT_CHECK_BGCOLOR:
                                            g_dev_has_bgcolor = !g_dev_has_bgcolor;
                                            if (!g_dev_has_bgcolor && g_edit_dev_bgcolor) ShowWindow(g_edit_dev_bgcolor, SW_HIDE);
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            return 0;

                                        case MAP_ATTR_HIT_CHECK_DESTROYALL:
                                            g_dev_has_destroyall = !g_dev_has_destroyall;
                                            g_dev_destroyall = g_dev_has_destroyall;
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            return 0;

                                        case MAP_ATTR_HIT_CHECK_HUEROT:
                                            g_dev_has_huerot = !g_dev_has_huerot;
                                            if (!g_dev_has_huerot && g_edit_dev_huerot) ShowWindow(g_edit_dev_huerot, SW_HIDE);
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            return 0;

                                        case MAP_ATTR_HIT_CHECK_TREASUREFREQ:
                                            g_dev_has_treasurefreq = !g_dev_has_treasurefreq;
                                            if (!g_dev_has_treasurefreq && g_edit_dev_treasurefreq) ShowWindow(g_edit_dev_treasurefreq, SW_HIDE);
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            return 0;

                                        case MAP_ATTR_HIT_CHECK_PARTIME:
                                            g_dev_has_partime = !g_dev_has_partime;
                                            if (!g_dev_has_partime && g_edit_dev_partime) ShowWindow(g_edit_dev_partime, SW_HIDE);
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            return 0;

                                        default:
                                            break;
                                    }
                                }

                                if (!hit_btn && max_s > 0) {
                                    g_is_dragging_prev_info_bg = true;
                                    g_drag_prev_info_bg_mouse_start_y = my;
                                    g_drag_prev_info_bg_scroll_start = g_hud_preview_info_scroll_y;
                                    SetCapture(hwnd);
                                }
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
            }

            /* 1b2. Hit-test Top-Left Canvas Guide HUD & Top-Right Canvas Tools HUD when Canvas Mode is ON */
            if (g_mode_canvas && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);

                /* Hit-test Top-Left Canvas Guide HUD */
                RECT rc_guide;
                get_top_left_canvas_guide_hud_rect(work_h, &rc_guide);
                if (mx >= rc_guide.left && mx <= rc_guide.right && my >= rc_guide.top && my <= rc_guide.bottom) {
                    if (g_hud_canvas_guide_collapsed) {
                        g_hud_canvas_guide_collapsed = false;
                        g_hud_canvas_guide_scroll_y = 0;
                    } else {
                        int rel_x = mx - rc_guide.left;
                        int rel_y = my - rc_guide.top;
                        int tl_w = rc_guide.right - rc_guide.left;
                        if (rel_y <= 26 && rel_x >= tl_w - 24) {
                            g_hud_canvas_guide_collapsed = true;
                            g_hud_canvas_guide_scroll_y = 0;
                        } else {
                            int track_y, track_h, thumb_y, thumb_h;
                            int max_s = get_canvas_guide_hud_max_scroll(work_h);
                            if (max_s > 0 && get_canvas_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h) &&
                                rel_x >= tl_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                                if (rel_y >= thumb_y && rel_y <= thumb_y + thumb_h) {
                                    g_is_dragging_cv_guide_thumb = true;
                                    g_drag_cv_guide_mouse_start_y = my;
                                    g_drag_cv_guide_scroll_start = g_hud_canvas_guide_scroll_y;
                                    SetCapture(hwnd);
                                } else {
                                    int travel = track_h - thumb_h;
                                    if (travel > 0) {
                                        float ratio = (float)(rel_y - track_y - thumb_h / 2) / (float)travel;
                                        if (ratio < 0.0f) ratio = 0.0f;
                                        if (ratio > 1.0f) ratio = 1.0f;
                                        g_hud_canvas_guide_scroll_y = (int)roundf(ratio * (float)max_s);
                                        g_is_dragging_cv_guide_thumb = true;
                                        g_drag_cv_guide_mouse_start_y = my;
                                        g_drag_cv_guide_scroll_start = g_hud_canvas_guide_scroll_y;
                                        SetCapture(hwnd);
                                    }
                                }
                            } else if (max_s > 0) {
                                g_is_dragging_cv_guide_bg = true;
                                g_drag_cv_guide_bg_mouse_start_y = my;
                                g_drag_cv_guide_bg_scroll_start = g_hud_canvas_guide_scroll_y;
                                SetCapture(hwnd);
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }

                /* Hit-test Top-Right Canvas Tools HUD */
                RECT rc_cv;
                get_top_right_canvas_hud_rect(client_w, work_h, &rc_cv);
                if (mx >= rc_cv.left && mx <= rc_cv.right && my >= rc_cv.top && my <= rc_cv.bottom) {
                    if (g_hud_canvas_collapsed) {
                        g_hud_canvas_collapsed = false;
                        g_hud_canvas_tools_scroll_y = 0;
                    } else {
                        int rel_x = mx - rc_cv.left;
                        int rel_y = my - rc_cv.top;
                        int tr_w = rc_cv.right - rc_cv.left;
                        if (rel_y <= 26 && rel_x >= tr_w - 24) {
                            g_hud_canvas_collapsed = true;
                            g_hud_canvas_tools_scroll_y = 0;
                            if (g_edit_cv_w) ShowWindow(g_edit_cv_w, SW_HIDE);
                            if (g_edit_cv_h) ShowWindow(g_edit_cv_h, SW_HIDE);
                        } else {
                            int track_y, track_h, thumb_y, thumb_h;
                            int max_s = get_canvas_tools_hud_max_scroll(work_h);
                            if (max_s > 0 && get_canvas_tools_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h) &&
                                rel_x >= tr_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                                if (rel_y >= thumb_y && rel_y <= thumb_y + thumb_h) {
                                    g_is_dragging_cv_tools_thumb = true;
                                    g_drag_cv_tools_mouse_start_y = my;
                                    g_drag_cv_tools_scroll_start = g_hud_canvas_tools_scroll_y;
                                    SetCapture(hwnd);
                                } else {
                                    int travel = track_h - thumb_h;
                                    if (travel > 0) {
                                        float ratio = (float)(rel_y - track_y - thumb_h / 2) / (float)travel;
                                        if (ratio < 0.0f) ratio = 0.0f;
                                        if (ratio > 1.0f) ratio = 1.0f;
                                        g_hud_canvas_tools_scroll_y = (int)roundf(ratio * (float)max_s);
                                        g_is_dragging_cv_tools_thumb = true;
                                        g_drag_cv_tools_mouse_start_y = my;
                                        g_drag_cv_tools_scroll_start = g_hud_canvas_tools_scroll_y;
                                        SetCapture(hwnd);
                                    }
                                }
                            } else {
                                int content_y = rel_y + g_hud_canvas_tools_scroll_y;
                                bool hit_btn = false;

                                /* Set Button */
                                if (content_y >= 46 && content_y <= 70 && rel_x >= 154 && rel_x <= 228) {
                                    hit_btn = true;
                                    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CV_APPLY, BN_CLICKED), 0);
                                    return 0;
                                }

                                /* Presets */
                                if (content_y >= 76 && content_y <= 98) {
                                    if (rel_x >= 8 && rel_x <= 114) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CV_PRESET_640, BN_CLICKED), 0);
                                        return 0;
                                    } else if (rel_x >= 120 && rel_x <= 228) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CV_PRESET_800, BN_CLICKED), 0);
                                        return 0;
                                    }
                                } else if (content_y >= 102 && content_y <= 124) {
                                    if (rel_x >= 8 && rel_x <= 114) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CV_PRESET_1024, BN_CLICKED), 0);
                                        return 0;
                                    } else if (rel_x >= 120 && rel_x <= 228) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CV_PRESET_1280, BN_CLICKED), 0);
                                        return 0;
                                    }
                                }

                                /* Background Image Buttons */
                                if (content_y >= 150 && content_y <= 172) {
                                    if (rel_x >= 8 && rel_x <= 114) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_MENU_CANVAS_CHANGE_IMG, 0), 0);
                                        return 0;
                                    } else if (rel_x >= 120 && rel_x <= 228) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_MENU_CANVAS_REMOVE_IMG, 0), 0);
                                        return 0;
                                    }
                                }

                                /* Fit Modes */
                                if (content_y >= 198 && content_y <= 220) {
                                    if (rel_x >= 8 && rel_x <= 114) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CV_FIT_ORIG, BN_CLICKED), 0);
                                        return 0;
                                    } else if (rel_x >= 120 && rel_x <= 228) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CV_FIT_FIT, BN_CLICKED), 0);
                                        return 0;
                                    }
                                } else if (content_y >= 224 && content_y <= 246) {
                                    if (rel_x >= 8 && rel_x <= 114) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CV_FIT_FILL, BN_CLICKED), 0);
                                        return 0;
                                    } else if (rel_x >= 120 && rel_x <= 228) {
                                        hit_btn = true;
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CV_FIT_STRETCH, BN_CLICKED), 0);
                                        return 0;
                                    }
                                }

                                /* Image Alignment 3x3 Grid */
                                if (content_y >= 274 && content_y <= 354) {
                                    int col = -1;
                                    if (rel_x >= 10 && rel_x <= 80) col = 0;
                                    else if (rel_x >= 83 && rel_x <= 153) col = 1;
                                    else if (rel_x >= 156 && rel_x <= 226) col = 2;

                                    int row = -1;
                                    if (content_y >= 274 && content_y <= 298) row = 0;
                                    else if (content_y >= 302 && content_y <= 326) row = 1;
                                    else if (content_y >= 330 && content_y <= 354) row = 2;

                                    if (col >= 0 && row >= 0) {
                                        hit_btn = true;
                                        int btn_id = ID_BTN_CV_ALIGN_TL + (row * 3 + col);
                                        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(btn_id, BN_CLICKED), 0);
                                        return 0;
                                    }
                                }

                                if (!hit_btn && max_s > 0) {
                                    g_is_dragging_cv_tools_bg = true;
                                    g_drag_cv_tools_bg_mouse_start_y = my;
                                    g_drag_cv_tools_bg_scroll_start = g_hud_canvas_tools_scroll_y;
                                    SetCapture(hwnd);
                                }
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }

                /* Hit-test Canvas Handles (8 arrow handles + Center Move) */
                bool is_ctrl_cv  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool is_shift_cv = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool is_alt_cv   = (GetKeyState(VK_MENU) & 0x8000) != 0;
                bool is_select_modifier_cv = is_ctrl_cv || (is_alt_cv && is_shift_cv);

                if (!is_select_modifier_cv) {
                    CanvasHandleType ht = hit_test_canvas_handles(client_w, work_h, mx, my);
                    if (ht != CANVAS_HANDLE_NONE) {
                        g_drag_canvas_handle = ht;
                        g_drag_start_w = g_canvas_w;
                        g_drag_start_h = g_canvas_h;
                        g_drag_start_mouse_sx = mx;
                        g_drag_start_mouse_sy = my;

                        snapshot_canvas_drag_state();
                        SetCapture(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    } else if (is_any_canvas_handle_selected()) {
                        deselect_all_canvas_handles();
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                    }
                }
            }

            /* 1c. Adjusting BackgroundAlpha Vector confirmation on canvas click */
            if (g_placing_bgalpha_vector && g_editing_bgalpha_idx >= 0 && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                if (g_editing_bgalpha_idx >= 0 && g_editing_bgalpha_idx < g_bg_alpha_count) {
                    LevelBackgroundAlpha *ba = &g_bg_alphas[g_editing_bgalpha_idx];
                    ba->vx = g_bgalpha_pending_vx;
                    ba->vy = g_bgalpha_pending_vy;
                }
                g_placing_bgalpha_vector = false;
                g_editing_bgalpha_idx = -1;
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            /* 1d. Adjusting Overlay Priority confirmation on canvas click */
            if (g_adjusting_overlay_priority && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                confirm_overlay_priority_adjustment(hwnd);
                return 0;
            }

            /* 2. Selection Mode (Ctrl / Alt+Shift / Ctrl+Shift / Ctrl+Alt+Shift + LMB / Drag) or Pick-Up existing Objects */
            bool is_ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool is_alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
            bool is_select_modifier = is_ctrl || (is_alt && is_shift);

            if ((g_mode_overlay || g_mode_element || g_mode_canvas) && g_has_canvas && my >= TOOLBAR_HEIGHT && is_select_modifier) {
                MarqueeSelectMode mode = MARQUEE_MODE_REPLACE;
                if (is_ctrl && is_alt && is_shift)       mode = MARQUEE_MODE_TOGGLE;
                else if (!is_ctrl && is_alt && is_shift) mode = MARQUEE_MODE_SUBTRACT;
                else if (is_ctrl && is_shift && !is_alt) mode = MARQUEE_MODE_ADD;
                else if (is_ctrl && !is_shift && !is_alt) mode = MARQUEE_MODE_REPLACE;

                if (g_mode_canvas) {
                    snapshot_canvas_handle_selection();
                    g_obj_drag_state = OBJ_DRAG_MARQUEE;
                    g_obj_marquee_start_x = mx;
                    g_obj_marquee_start_y = my;
                    g_obj_marquee_cur_x   = mx;
                    g_obj_marquee_cur_y   = my;
                    g_obj_marquee_mode    = mode;

                    RECT rc_client;
                    GetClientRect(hwnd, &rc_client);
                    int client_w = rc_client.right - rc_client.left;
                    int work_h = get_workspace_height(hwnd);

                    CanvasHandleType ht = hit_test_canvas_handles(client_w, work_h, mx, my);
                    if (ht != CANVAS_HANDLE_NONE) {
                        if (mode == MARQUEE_MODE_REPLACE) {
                            deselect_all_canvas_handles();
                            g_canvas_handle_selected[ht] = true;
                        } else if (mode == MARQUEE_MODE_ADD) {
                            g_canvas_handle_selected[ht] = true;
                        } else if (mode == MARQUEE_MODE_SUBTRACT) {
                            g_canvas_handle_selected[ht] = false;
                        } else if (mode == MARQUEE_MODE_TOGGLE) {
                            g_canvas_handle_selected[ht] = !g_snap_canvas_handle_selected[ht];
                        }
                    }
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }

                snapshot_object_selection();
                g_obj_drag_state = OBJ_DRAG_MARQUEE;
                g_obj_marquee_start_x = mx;
                g_obj_marquee_start_y = my;
                g_obj_marquee_cur_x   = mx;
                g_obj_marquee_cur_y   = my;
                g_obj_marquee_mode    = mode;

                if (g_mode_overlay) {
                    if (g_hovered_cutout_idx >= 0 && g_hovered_cutout_idx < g_cutout_count) {
                        int idx = g_hovered_cutout_idx;
                        if (mode == MARQUEE_MODE_REPLACE) {
                            bool was_already_sel = g_cutouts[idx].selected && (get_selected_object_count() == 1);
                            deselect_all_objects();
                            g_cutouts[idx].selected = true;
                            g_selected_cutout_idx = idx;
                            g_overlay_active_tab = 0;
                            g_overlay_current_prio = g_cutouts[idx].pri;
                            if (g_edit_prio) {
                                char buf[16];
                                snprintf(buf, sizeof(buf), "%d", g_overlay_current_prio);
                                g_in_prio_update = true;
                                SetWindowTextA(g_edit_prio, buf);
                                g_in_prio_update = false;
                            }
                            if (was_already_sel && is_ctrl && !is_shift && !is_alt) {
                                start_overlay_priority_adjustment(hwnd, 0, idx);
                            }
                        } else if (mode == MARQUEE_MODE_ADD) {
                            g_cutouts[idx].selected = true;
                        } else if (mode == MARQUEE_MODE_SUBTRACT) {
                            g_cutouts[idx].selected = false;
                        } else if (mode == MARQUEE_MODE_TOGGLE) {
                            g_cutouts[idx].selected = !g_snap_cutout_selected[idx];
                        }
                    } else if (g_hovered_sprite_idx >= 0 && g_hovered_sprite_idx < g_sprite_count) {
                        int idx = g_hovered_sprite_idx;
                        if (mode == MARQUEE_MODE_REPLACE) {
                            bool was_already_sel = g_sprites[idx].selected && (get_selected_object_count() == 1);
                            deselect_all_objects();
                            g_sprites[idx].selected = true;
                            g_selected_sprite_idx = idx;
                            g_overlay_active_tab = 1;
                            g_overlay_current_prio = g_sprites[idx].pri;
                            if (g_edit_prio) {
                                char buf[16];
                                snprintf(buf, sizeof(buf), "%d", g_overlay_current_prio);
                                g_in_prio_update = true;
                                SetWindowTextA(g_edit_prio, buf);
                                g_in_prio_update = false;
                            }
                            if (was_already_sel && is_ctrl && !is_shift && !is_alt) {
                                start_overlay_priority_adjustment(hwnd, 1, idx);
                            }
                        } else if (mode == MARQUEE_MODE_ADD) {
                            g_sprites[idx].selected = true;
                        } else if (mode == MARQUEE_MODE_SUBTRACT) {
                            g_sprites[idx].selected = false;
                        } else if (mode == MARQUEE_MODE_TOGGLE) {
                            g_sprites[idx].selected = !g_snap_sprite_selected[idx];
                        }
                    } else if (g_hovered_bgalpha_idx >= 0 && g_hovered_bgalpha_idx < g_bg_alpha_count) {
                        int idx = g_hovered_bgalpha_idx;
                        if (mode == MARQUEE_MODE_REPLACE) {
                            bool was_already_sel = g_bg_alphas[idx].selected && (get_selected_object_count() == 1);
                            deselect_all_objects();
                            g_bg_alphas[idx].selected = true;
                            g_selected_bgalpha_idx = idx;
                            g_overlay_active_tab = 2;
                            if (was_already_sel && is_ctrl && !is_shift && !is_alt) {
                                LevelBackgroundAlpha *ba = &g_bg_alphas[idx];
                                int img_w = ba->has_img ? ba->img.width : (g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 160);
                                int img_h = ba->has_img ? ba->img.height : (g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 140);
                                g_bgalpha_anchor_x = ba->x + img_w / 2;
                                g_bgalpha_anchor_y = ba->y + img_h / 2;
                                g_bgalpha_pending_vx = ba->vx;
                                g_bgalpha_pending_vy = ba->vy;
                                g_placing_bgalpha = false;
                                g_placing_bgalpha_vector = true;
                                g_editing_bgalpha_idx = idx;
                            }
                        } else if (mode == MARQUEE_MODE_ADD) {
                            g_bg_alphas[idx].selected = true;
                            g_selected_bgalpha_idx = idx;
                        } else if (mode == MARQUEE_MODE_SUBTRACT) {
                            g_bg_alphas[idx].selected = false;
                        } else if (mode == MARQUEE_MODE_TOGGLE) {
                            g_bg_alphas[idx].selected = !g_snap_bgalpha_selected[idx];
                            if (g_bg_alphas[idx].selected) g_selected_bgalpha_idx = idx;
                        }
                    }
                } else if (g_mode_element) {
                    /* Element Mode: Frog & Coins */
                    if (g_frog_hovered && g_has_frog) {
                        if (mode == MARQUEE_MODE_REPLACE) {
                            deselect_all_objects();
                            g_frog_selected = true;
                        } else if (mode == MARQUEE_MODE_ADD) {
                            g_frog_selected = true;
                        } else if (mode == MARQUEE_MODE_SUBTRACT) {
                            g_frog_selected = false;
                        } else if (mode == MARQUEE_MODE_TOGGLE) {
                            g_frog_selected = !g_snap_frog_selected;
                        }
                    } else if (g_hovered_coin_idx >= 0 && g_hovered_coin_idx < g_treasure_point_count) {
                        int idx = g_hovered_coin_idx;
                        if (mode == MARQUEE_MODE_REPLACE) {
                            deselect_all_objects();
                            g_treasure_points[idx].selected = true;
                        } else if (mode == MARQUEE_MODE_ADD) {
                            g_treasure_points[idx].selected = true;
                        } else if (mode == MARQUEE_MODE_SUBTRACT) {
                            g_treasure_points[idx].selected = false;
                        } else if (mode == MARQUEE_MODE_TOGGLE) {
                            g_treasure_points[idx].selected = !g_snap_coin_selected[idx];
                        }
                    }
                }
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            /* Regular Left-Click: Pick-Up existing Cutout, Sprite, or BackgroundAlpha (Hierarchy: Cutout -> Sprite -> BgAlpha) */
            if (g_mode_overlay && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                /* Priority 1: Cutout (Top-most) */
                if (g_hovered_cutout_idx >= 0 && g_hovered_cutout_idx < g_cutout_count) {
                    int idx = g_hovered_cutout_idx;
                    free_pending_cutout();
                    g_overlay_current_prio = g_cutouts[idx].pri;
                    g_pending_cutout_pri = g_overlay_current_prio;
                    g_pending_sprite_pri = g_overlay_current_prio;
                    if (g_edit_prio) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%d", g_overlay_current_prio);
                        g_in_prio_update = true;
                        SetWindowTextA(g_edit_prio, buf);
                        g_in_prio_update = false;
                    }
                    if (g_cutouts[idx].has_img) {
                        g_pending_cutout_img = g_cutouts[idx].img;
                        g_has_pending_cutout_img = true;
                        strncpy(g_pending_cutout_filepath, g_cutouts[idx].source_filepath, sizeof(g_pending_cutout_filepath) - 1);
                        g_pending_cutout_mask_light = g_cutouts[idx].hbm_mask_light;
                        g_pending_cutout_hdc_light  = g_cutouts[idx].hdc_mask_light;
                        g_pending_cutout_mask_dark  = g_cutouts[idx].hbm_mask_dark;
                        g_pending_cutout_hdc_dark   = g_cutouts[idx].hdc_mask_dark;

                        memset(&g_cutouts[idx].img, 0, sizeof(CanvasImage));
                        g_cutouts[idx].has_img = false;
                        g_cutouts[idx].hbm_mask_light = NULL;
                        g_cutouts[idx].hdc_mask_light = NULL;
                        g_cutouts[idx].hbm_mask_dark  = NULL;
                        g_cutouts[idx].hdc_mask_dark  = NULL;
                    }
                    free_cutout(&g_cutouts[idx]);
                    for (int i = idx; i < g_cutout_count - 1; i++) {
                        g_cutouts[i] = g_cutouts[i + 1];
                    }
                    g_cutout_count--;
                    g_hovered_cutout_idx = -1;
                    sync_cutout_image_names();
                    g_placing_cutout = true;
                    if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
                /* Priority 2: Sprite */
                if (g_hovered_sprite_idx >= 0 && g_hovered_sprite_idx < g_sprite_count) {
                    int idx = g_hovered_sprite_idx;
                    free_pending_sprite();
                    g_overlay_current_prio = g_sprites[idx].pri;
                    g_pending_cutout_pri = g_overlay_current_prio;
                    g_pending_sprite_pri = g_overlay_current_prio;
                    if (g_edit_prio) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%d", g_overlay_current_prio);
                        g_in_prio_update = true;
                        SetWindowTextA(g_edit_prio, buf);
                        g_in_prio_update = false;
                    }
                    if (g_sprites[idx].has_img) {
                        g_pending_sprite_img = g_sprites[idx].img;
                        g_has_pending_sprite_img = true;
                        strncpy(g_pending_sprite_filepath, g_sprites[idx].source_filepath, sizeof(g_pending_sprite_filepath) - 1);

                        memset(&g_sprites[idx].img, 0, sizeof(CanvasImage));
                        g_sprites[idx].has_img = false;
                    }
                    free_sprite(&g_sprites[idx]);
                    for (int i = idx; i < g_sprite_count - 1; i++) {
                        g_sprites[i] = g_sprites[i + 1];
                    }
                    g_sprite_count--;
                    g_hovered_sprite_idx = -1;
                    sync_sprite_image_names();
                    g_placing_sprite = true;
                    if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
                /* Priority 3: BackgroundAlpha (Bottom-most overlay layer, no Priority) */
                if (g_hovered_bgalpha_idx >= 0 && g_hovered_bgalpha_idx < g_bg_alpha_count) {
                    int idx = g_hovered_bgalpha_idx;
                    free_pending_bgalpha();
                    /* BackgroundAlpha has no Priority, do not change priority */
                    if (g_bg_alphas[idx].has_img) {
                        g_pending_bgalpha_img = g_bg_alphas[idx].img;
                        g_has_pending_bgalpha_img = true;
                        strncpy(g_pending_bgalpha_filepath, g_bg_alphas[idx].source_filepath, sizeof(g_pending_bgalpha_filepath) - 1);

                        memset(&g_bg_alphas[idx].img, 0, sizeof(CanvasImage));
                        g_bg_alphas[idx].has_img = false;
                    }
                    free_bgalpha(&g_bg_alphas[idx]);
                    for (int i = idx; i < g_bg_alpha_count - 1; i++) {
                        g_bg_alphas[i] = g_bg_alphas[i + 1];
                    }
                    g_bg_alpha_count--;
                    g_hovered_bgalpha_idx = -1;
                    sync_bgalpha_image_names();
                    g_placing_bgalpha = true;
                    g_placing_bgalpha_vector = false;
                    if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
                /* Click on empty canvas in Overlay Mode: deselect all objects */
                if (!g_placing_cutout && !g_placing_sprite && !g_placing_bgalpha && !g_placing_bgalpha_vector) {
                    deselect_all_objects();
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                }
            } else if (g_mode_element && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                if (g_frog_hovered && g_has_frog) {
                    undo_ensure_baseline();
                    g_placing_frog = true;
                    g_frog_hovered = false;
                    if (g_btn_frog) InvalidateRect(g_btn_frog, NULL, TRUE);
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
                if (g_hovered_coin_idx >= 0 && g_hovered_coin_idx < g_treasure_point_count) {
                    for (int i = g_hovered_coin_idx; i < g_treasure_point_count - 1; i++) {
                        g_treasure_points[i] = g_treasure_points[i + 1];
                    }
                    g_treasure_point_count--;
                    g_hovered_coin_idx = -1;
                    g_placing_coin = true;
                    if (g_btn_coin) InvalidateRect(g_btn_coin, NULL, TRUE);
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
                /* Click on empty canvas in Element Mode: deselect all objects */
                if (!g_placing_frog && !g_placing_coin) {
                    deselect_all_objects();
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                }
            }

            if (g_mode_curve && g_has_canvas && my >= TOOLBAR_HEIGHT) {
                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);

                RECT rc_tr, rc_tl;
                get_top_right_hud_rect(client_w, work_h, &rc_tr);
                get_top_left_hud_rect(work_h, &rc_tl);

                /* 1. Hit-test Top-Right HUD (Curve Attribute) */
                if (mx >= rc_tr.left && mx <= rc_tr.right && my >= rc_tr.top && my <= rc_tr.bottom) {
                    if (g_hud_mode_collapsed) {
                        g_hud_mode_collapsed = false;
                        g_hud_attr_scroll_y = 0;
                    } else {
                        int rel_x = mx - rc_tr.left;
                        int rel_y = my - rc_tr.top;
                        int tr_w = rc_tr.right - rc_tr.left;
                        if (rel_x >= tr_w - 24 && rel_y <= 22) {
                            g_hud_mode_collapsed = true;
                            g_hud_attr_scroll_y = 0;
                            if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
                        } else {
                            int track_y, track_h, thumb_y, thumb_h;
                            int max_s = get_attr_hud_max_scroll(work_h);
                            if (max_s > 0 && get_attr_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h) &&
                                rel_x >= tr_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                                if (rel_y >= thumb_y && rel_y <= thumb_y + thumb_h) {
                                    g_is_dragging_attr_thumb = true;
                                    g_drag_attr_mouse_start_y = my;
                                    g_drag_attr_scroll_start = g_hud_attr_scroll_y;
                                    SetCapture(hwnd);
                                } else {
                                    int travel = track_h - thumb_h;
                                    if (travel > 0) {
                                        float ratio = (float)(rel_y - track_y - thumb_h / 2) / (float)travel;
                                        if (ratio < 0.0f) ratio = 0.0f;
                                        if (ratio > 1.0f) ratio = 1.0f;
                                        g_hud_attr_scroll_y = (int)roundf(ratio * (float)max_s);
                                        g_is_dragging_attr_thumb = true;
                                        g_drag_attr_mouse_start_y = my;
                                        g_drag_attr_scroll_start = g_hud_attr_scroll_y;
                                        SetCapture(hwnd);
                                    }
                                }
                            } else {
                                int content_y = rel_y + g_hud_attr_scroll_y;
                                bool hit_btn = false;
                                if (content_y >= 24 && content_y <= 46) {
                                    /* Row 1: Import Path / Export Path */
                                    if (rel_x >= 8 && rel_x <= 114) {
                                        hit_btn = true;
                                        handle_import_svg_path(hwnd);
                                    } else if (rel_x >= 120 && rel_x <= 228) {
                                        hit_btn = true;
                                        handle_export_svg_path(hwnd);
                                    }
                                } else if (content_y >= 50 && content_y <= 72) {
                                    /* Row 2: Segmented Switch Pill (Draw vs Variable) */
                                    if (rel_x >= 8 && rel_x <= 118) {
                                        hit_btn = true;
                                        g_curve_submode = CURVE_SUBMODE_DRAW;
                                        if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
                                    } else if (rel_x > 118 && rel_x <= 228) {
                                        hit_btn = true;
                                        g_curve_submode = CURVE_SUBMODE_VARIABLE;
                                    }
                                } else if (g_curve_submode == CURVE_SUBMODE_VARIABLE && content_y >= 76 && content_y <= 98) {
                                    /* Row 3 Left: Toggle boolean Unhittable */
                                    if (rel_x >= 8 && rel_x <= 110) {
                                        hit_btn = true;
                                        g_var_tunnel_val = (g_var_tunnel_val == 1) ? 0 : 1;
                                        g_var_write_tunnel = true;
                                    } else if (rel_x >= 111 && rel_x <= 228) {
                                        /* Focus Priority numeric input box */
                                        hit_btn = true;
                                        if (g_edit_prio) {
                                            SetFocus(g_edit_prio);
                                            SendMessage(g_edit_prio, EM_SETSEL, 0, -1);
                                        }
                                    }
                                }

                                if (!hit_btn && max_s > 0) {
                                    g_is_dragging_attr_bg = true;
                                    g_drag_attr_bg_mouse_start_y = my;
                                    g_drag_attr_bg_scroll_start = g_hud_attr_scroll_y;
                                    SetCapture(hwnd);
                                }
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }

                /* 2. Hit-test Top-Left HUD (Guide) */
                if (mx >= rc_tl.left && mx <= rc_tl.right && my >= rc_tl.top && my <= rc_tl.bottom) {
                    if (g_hud_shortcuts_collapsed) {
                        g_hud_shortcuts_collapsed = false;
                        g_hud_guide_scroll_y = 0;
                    } else {
                        int rel_x = mx - rc_tl.left;
                        int rel_y = my - rc_tl.top;
                        int tl_w = rc_tl.right - rc_tl.left;
                        if (rel_y <= 26 && rel_x >= tl_w - 24) {
                            g_hud_shortcuts_collapsed = true;
                            g_hud_guide_scroll_y = 0;
                        } else {
                            int track_y, track_h, thumb_y, thumb_h;
                            int max_s = get_guide_hud_max_scroll(work_h);
                            if (max_s > 0 && get_guide_scroll_geometry(work_h, &track_y, &track_h, &thumb_y, &thumb_h) &&
                                rel_x >= tl_w - 12 && rel_y >= track_y && rel_y <= track_y + track_h) {
                                if (rel_y >= thumb_y && rel_y <= thumb_y + thumb_h) {
                                    g_is_dragging_guide_thumb = true;
                                    g_drag_guide_mouse_start_y = my;
                                    g_drag_guide_scroll_start = g_hud_guide_scroll_y;
                                    SetCapture(hwnd);
                                } else {
                                    int travel = track_h - thumb_h;
                                    if (travel > 0) {
                                        float ratio = (float)(rel_y - track_y - thumb_h / 2) / (float)travel;
                                        if (ratio < 0.0f) ratio = 0.0f;
                                        if (ratio > 1.0f) ratio = 1.0f;
                                        g_hud_guide_scroll_y = (int)roundf(ratio * (float)max_s);
                                        g_is_dragging_guide_thumb = true;
                                        g_drag_guide_mouse_start_y = my;
                                        g_drag_guide_scroll_start = g_hud_guide_scroll_y;
                                        SetCapture(hwnd);
                                    }
                                }
                            } else if (max_s > 0) {
                                g_is_dragging_guide_bg = true;
                                g_drag_guide_bg_mouse_start_y = my;
                                g_drag_guide_bg_scroll_start = g_hud_guide_scroll_y;
                                SetCapture(hwnd);
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                /* 3. In Variable Mode: Click & Drag to Author Attribute Spans */
                if (g_curve_submode == CURVE_SUBMODE_VARIABLE) {
                    if (g_path_count > 0) {
                        float cx, cy;
                        screen_to_canvas(client_w, work_h, mx, my, &cx, &cy);

                        bool is_ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                        if (is_ctrl) {
                            /* Hold Ctrl + LMB Click: Switch Active Path under cursor.
                             * Detects BOTH anchor nodes AND entire path curve line with generous screen tolerance */
                            int best_p = -1;
                            double min_screen_dist = 1e18;
                            const double max_screen_hit = 32.0; /* Generous 32px screen threshold */

                            RECT rc_canvas;
                            get_canvas_screen_rect(client_w, work_h, &rc_canvas);

                            /* 1. Test anchor/handle nodes across all paths first */
                            int hit_p = -1, hit_n = -1, hit_type = -1;
                            if (hit_test_bezier(mx, my, &rc_canvas, &hit_p, &hit_n, &hit_type)) {
                                if (hit_p >= 0 && hit_p < g_path_count) {
                                    best_p = hit_p;
                                    min_screen_dist = 0.0;
                                }
                            }

                            /* 2. Test entire path curve polyline for all paths */
                            for (int p = 0; p < g_path_count; ++p) {
                                if (g_paths[p].node_count >= 2) {
                                    double d2 = 0.0;
                                    project_s_window(&g_paths[p], cx, cy, -1.0, 0.0, &d2);
                                    double scr_dist = sqrt(d2) * (double)g_zoom;
                                    if (scr_dist <= max_screen_hit && scr_dist < min_screen_dist) {
                                        min_screen_dist = scr_dist;
                                        best_p = p;
                                    }
                                } else if (g_paths[p].node_count == 1) {
                                    int nx = rc_canvas.left + (int)roundf(g_paths[p].nodes[0].x * g_zoom);
                                    int ny = rc_canvas.top + (int)roundf(g_paths[p].nodes[0].y * g_zoom);
                                    double scr_dist = hypot((double)(mx - nx), (double)(my - ny));
                                    if (scr_dist <= max_screen_hit && scr_dist < min_screen_dist) {
                                        min_screen_dist = scr_dist;
                                        best_p = p;
                                    }
                                }
                            }

                            if (best_p >= 0) {
                                g_active_path_idx = best_p;
                                g_active_node_idx = -1;
                                InvalidateRect(hwnd, NULL, FALSE);
                                update_status_bar();
                            }
                            return 0; /* When Ctrl is held, never paint attribute spans */
                        }

                        int target_p = g_active_path_idx;
                        double best_dist_sq = 1e18;
                        double best_s = 0.0;
                        if (target_p < 0 || target_p >= g_path_count || g_paths[target_p].node_count < 2) {
                            for (int p = 0; p < g_path_count; ++p) {
                                if (g_paths[p].node_count >= 2) {
                                    double d2 = 0.0;
                                    double s = project_s_window(&g_paths[p], cx, cy, -1.0, 0.0, &d2);
                                    if (d2 < best_dist_sq) {
                                        best_dist_sq = d2;
                                        best_s = s;
                                        target_p = p;
                                    }
                                }
                            }
                        } else {
                            best_s = project_s_window(&g_paths[target_p], cx, cy, -1.0, 0.0, &best_dist_sq);
                        }

                        if (target_p >= 0 && target_p < g_path_count && best_dist_sq <= 25.0 * 25.0) {
                            g_active_path_idx = target_p;
                            g_var_dragging = true;
                            g_var_drag_s_start = best_s;
                            g_var_drag_s_cur = best_s;
                            SetCapture(hwnd);
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                            return 0;
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                RECT rc_canvas;
                get_canvas_screen_rect(client_w, work_h, &rc_canvas);

                float cx, cy;
                screen_to_canvas(client_w, work_h, mx, my, &cx, &cy);

                bool is_ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool is_alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
                bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                int hit_p = -1, hit_n = -1, hit_type = -1;
                bool hit = hit_test_bezier(mx, my, &rc_canvas, &hit_p, &hit_n, &hit_type);

                bool is_select_op = is_ctrl || (is_alt && is_shift);

                if (is_select_op) {
                    /* Selection Operations: Replace (Ctrl), Add (Ctrl+Shift), Deselect (Alt+Shift), Toggle (Ctrl+Alt+Shift) */
                    if (hit) {
                        if (hit_type == 1) { /* Outgoing handle */
                            g_pen_drag_state = PEN_DRAG_HANDLE_OUT;
                            g_drag_path_idx = hit_p;
                            g_drag_node_idx = hit_n;
                            g_active_path_idx = hit_p;
                            g_active_node_idx = hit_n;
                            if (is_alt) {
                                g_paths[hit_p].nodes[hit_n].type = NODE_CUSP;
                            }
                            SetCapture(hwnd);
                            InvalidateRect(hwnd, NULL, FALSE);
                            return 0;
                        } else if (hit_type == 2) { /* Incoming handle */
                            g_pen_drag_state = PEN_DRAG_HANDLE_IN;
                            g_drag_path_idx = hit_p;
                            g_drag_node_idx = hit_n;
                            g_active_path_idx = hit_p;
                            g_active_node_idx = hit_n;
                            if (is_alt) {
                                g_paths[hit_p].nodes[hit_n].type = NODE_CUSP;
                            }
                            SetCapture(hwnd);
                            InvalidateRect(hwnd, NULL, FALSE);
                            return 0;
                        } else if (hit_type == 0) { /* Anchor Point */
                            if (is_ctrl && is_alt && is_shift) {
                                /* 1. Toggle Selection (Ctrl + Alt + Shift): Invert state */
                                g_paths[hit_p].nodes[hit_n].selected = !g_paths[hit_p].nodes[hit_n].selected;
                                if (g_paths[hit_p].nodes[hit_n].selected) {
                                    g_active_path_idx = hit_p;
                                    g_active_node_idx = hit_n;
                                } else if (g_active_path_idx == hit_p && g_active_node_idx == hit_n) {
                                    g_active_node_idx = -1;
                                    for (int j = 0; j < g_paths[hit_p].node_count; ++j) {
                                        if (g_paths[hit_p].nodes[j].selected) {
                                            g_active_node_idx = j;
                                            break;
                                        }
                                    }
                                }
                            } else if (!is_ctrl && is_alt && is_shift) {
                                /* 2. Deselect (Alt + Shift): Remove from selection */
                                g_paths[hit_p].nodes[hit_n].selected = false;
                                if (g_active_path_idx == hit_p && g_active_node_idx == hit_n) {
                                    g_active_node_idx = -1;
                                    for (int j = 0; j < g_paths[hit_p].node_count; ++j) {
                                        if (g_paths[hit_p].nodes[j].selected) {
                                            g_active_node_idx = j;
                                            break;
                                        }
                                    }
                                }
                            } else if (is_ctrl && is_shift && !is_alt) {
                                /* 3. Multiple Selection Add (Ctrl + Shift): Add without clearing existing */
                                g_paths[hit_p].nodes[hit_n].selected = true;
                                g_active_path_idx = hit_p;
                                g_active_node_idx = hit_n;
                                g_pen_drag_state = PEN_DRAG_ANCHOR;
                                g_drag_path_idx = hit_p;
                                g_drag_node_idx = hit_n;
                                g_drag_anchor_start_x = g_paths[hit_p].nodes[hit_n].x;
                                g_drag_anchor_start_y = g_paths[hit_p].nodes[hit_n].y;
                                g_drag_mouse_start_cx = cx;
                                g_drag_mouse_start_cy = cy;
                                SetCapture(hwnd);
                            } else if (is_ctrl && !is_shift && !is_alt) {
                                /* 4. Single Selection / Replace (Ctrl): Clear previous, select this node, prepare to drag */
                                if (!g_paths[hit_p].nodes[hit_n].selected) {
                                    for (int p = 0; p < g_path_count; ++p) {
                                        for (int i = 0; i < g_paths[p].node_count; ++i) {
                                            g_paths[p].nodes[i].selected = false;
                                        }
                                    }
                                    g_paths[hit_p].nodes[hit_n].selected = true;
                                }
                                g_active_path_idx = hit_p;
                                g_active_node_idx = hit_n;
                                g_pen_drag_state = PEN_DRAG_ANCHOR;
                                g_drag_path_idx = hit_p;
                                g_drag_node_idx = hit_n;
                                g_drag_anchor_start_x = g_paths[hit_p].nodes[hit_n].x;
                                g_drag_anchor_start_y = g_paths[hit_p].nodes[hit_n].y;
                                g_drag_mouse_start_cx = cx;
                                g_drag_mouse_start_cy = cy;
                                SetCapture(hwnd);
                            }
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                            return 0;
                        }
                    } else {
                        /* Click/Drag empty area: Start Marquee Drag with modifier mode */
                        g_pen_drag_state = PEN_DRAG_MARQUEE;
                        g_marquee_start_x = mx;
                        g_marquee_start_y = my;
                        g_marquee_cur_x = mx;
                        g_marquee_cur_y = my;

                        if (is_ctrl && is_alt && is_shift) {
                            g_marquee_mode = MARQUEE_MODE_TOGGLE;
                        } else if (!is_ctrl && is_alt && is_shift) {
                            g_marquee_mode = MARQUEE_MODE_SUBTRACT;
                        } else if (is_ctrl && is_shift && !is_alt) {
                            g_marquee_mode = MARQUEE_MODE_ADD;
                        } else {
                            g_marquee_mode = MARQUEE_MODE_REPLACE;
                        }

                        free_marquee_snapshot();
                        for (int p = 0; p < g_path_count; ++p) {
                            int cnt = g_paths[p].node_count;
                            g_marquee_snapshot_count[p] = cnt;
                            if (cnt > 0) {
                                g_marquee_snapshot[p] = (bool *)malloc(cnt * sizeof(bool));
                                for (int i = 0; i < cnt; ++i) {
                                    g_marquee_snapshot[p][i] = g_paths[p].nodes[i].selected;
                                }
                            } else {
                                g_marquee_snapshot[p] = NULL;
                            }
                        }
                        SetCapture(hwnd);
                        return 0;
                    }
                } else {
                    /* WITHOUT SELECTION MODIFIERS: Pure Pen Tool Drawing Mode */

                    /* If clicking directly on an already-exposed handle: allow dragging the handle */
                    if (hit && (hit_type == 1 || hit_type == 2)) {
                        if (hit_type == 1) {
                            g_pen_drag_state = PEN_DRAG_HANDLE_OUT;
                            g_drag_path_idx = hit_p;
                            g_drag_node_idx = hit_n;
                            g_active_path_idx = hit_p;
                            g_active_node_idx = hit_n;
                            if (is_alt) g_paths[hit_p].nodes[hit_n].type = NODE_CUSP;
                            SetCapture(hwnd);
                            InvalidateRect(hwnd, NULL, FALSE);
                            return 0;
                        } else {
                            g_pen_drag_state = PEN_DRAG_HANDLE_IN;
                            g_drag_path_idx = hit_p;
                            g_drag_node_idx = hit_n;
                            g_active_path_idx = hit_p;
                            g_active_node_idx = hit_n;
                            if (is_alt) g_paths[hit_p].nodes[hit_n].type = NODE_CUSP;
                            SetCapture(hwnd);
                            InvalidateRect(hwnd, NULL, FALSE);
                            return 0;
                        }
                    }

                    /* If Alt is held on an anchor: convert corner / pull handles */
                    if (is_alt && hit && hit_type == 0) {
                        bezier_path_make_corner_node(&g_paths[hit_p], hit_n);
                        g_pen_drag_state = PEN_DRAG_NEW_HANDLE;
                        g_drag_path_idx = hit_p;
                        g_drag_node_idx = hit_n;
                        g_active_path_idx = hit_p;
                        g_active_node_idx = hit_n;
                        for (int p = 0; p < g_path_count; ++p) {
                            for (int i = 0; i < g_paths[p].node_count; ++i) {
                                g_paths[p].nodes[i].selected = (p == hit_p && i == hit_n);
                            }
                        }
                        SetCapture(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                        return 0;
                    }

                    /* Regular Left Click: Continue selected active path OR create a new path */
                    if (g_active_path_idx < 0 || g_active_path_idx >= g_path_count) {
                        compact_empty_paths();
                        if (g_path_count >= 10) {
                            show_path_limit_warning(hwnd);
                            return 0;
                        }
                        /* No path selected -> start new path */
                        if (g_path_count < MAX_PATHS) {
                            g_active_path_idx = g_path_count++;
                            const char *base = g_canvas_name[0] ? g_canvas_name : "curve";
                            char name[64];
                            if (g_path_count == 1) {
                                snprintf(name, sizeof(name), "%s", base);
                            } else {
                                if (g_path_count == 2) {
                                    snprintf(g_paths[0].name, sizeof(g_paths[0].name), "%s-1", base);
                                }
                                snprintf(name, sizeof(name), "%s-%d", base, g_path_count);
                            }
                            bezier_path_init(&g_paths[g_active_path_idx], name);
                        } else {
                            return 0;
                        }
                    }

                        BezierPath *active_bp = &g_paths[g_active_path_idx];
                        for (int i = 0; i < active_bp->node_count; ++i) {
                            active_bp->nodes[i].selected = false;
                        }

                        float place_x = cx;
                        float place_y = cy;
                        if (is_shift && active_bp->node_count > 0) {
                            const BezierNode *prev = &active_bp->nodes[active_bp->node_count - 1];
                            float dx = cx - prev->x;
                            float dy = cy - prev->y;
                            float sdx, sdy;
                            snap_vector_45_deg(dx, dy, &sdx, &sdy);
                            place_x = prev->x + sdx;
                            place_y = prev->y + sdy;
                        }

                        undo_ensure_baseline();
                        BezierNode new_node;
                        memset(&new_node, 0, sizeof(BezierNode));
                        new_node.x = place_x;
                        new_node.y = place_y;
                        new_node.type = NODE_CORNER;
                        new_node.selected = true;

                        bezier_path_add_node(active_bp, &new_node);
                        g_pen_drag_state = PEN_DRAG_NEW_HANDLE;
                        g_drag_path_idx = g_active_path_idx;
                        g_drag_node_idx = active_bp->node_count - 1;
                        g_active_node_idx = active_bp->node_count - 1;
                        SetCapture(hwnd);

                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                        return 0;
                    }
                }
                return 0;
            }

        case WM_LBUTTONUP: {
            if (g_is_dragging_guide_thumb || g_is_dragging_guide_bg ||
                g_is_dragging_attr_thumb || g_is_dragging_attr_bg ||
                g_is_dragging_ov_guide_thumb || g_is_dragging_ov_guide_bg ||
                g_is_dragging_ov_tools_thumb || g_is_dragging_ov_tools_bg ||
                g_is_dragging_el_guide_thumb || g_is_dragging_el_guide_bg ||
                g_is_dragging_el_tools_thumb || g_is_dragging_el_tools_bg ||
                g_is_dragging_cv_guide_thumb || g_is_dragging_cv_guide_bg ||
                g_is_dragging_cv_tools_thumb || g_is_dragging_cv_tools_bg ||
                g_is_dragging_prev_guide_thumb || g_is_dragging_prev_guide_bg ||
                g_is_dragging_prev_info_thumb || g_is_dragging_prev_info_bg) {
                g_is_dragging_guide_thumb = false;
                g_is_dragging_guide_bg = false;
                g_is_dragging_attr_thumb = false;
                g_is_dragging_attr_bg = false;
                g_is_dragging_ov_guide_thumb = false;
                g_is_dragging_ov_guide_bg = false;
                g_is_dragging_ov_tools_thumb = false;
                g_is_dragging_ov_tools_bg = false;
                g_is_dragging_el_guide_thumb = false;
                g_is_dragging_el_guide_bg = false;
                g_is_dragging_el_tools_thumb = false;
                g_is_dragging_el_tools_bg = false;
                g_is_dragging_cv_guide_thumb = false;
                g_is_dragging_cv_guide_bg = false;
                g_is_dragging_cv_tools_thumb = false;
                g_is_dragging_cv_tools_bg = false;
                g_is_dragging_prev_guide_thumb = false;
                g_is_dragging_prev_guide_bg = false;
                g_is_dragging_prev_info_thumb = false;
                g_is_dragging_prev_info_bg = false;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (g_drag_canvas_handle != CANVAS_HANDLE_NONE) {
                g_drag_canvas_handle = CANVAS_HANDLE_NONE;
                ReleaseCapture();
                if (g_edit_cv_w && g_edit_cv_h) {
                    g_in_canvas_edit_sync = true;
                    char buf_w[16], buf_h[16];
                    snprintf(buf_w, sizeof(buf_w), "%d", g_canvas_w);
                    snprintf(buf_h, sizeof(buf_h), "%d", g_canvas_h);
                    SetWindowTextA(g_edit_cv_w, buf_w);
                    SetWindowTextA(g_edit_cv_h, buf_h);
                    g_in_canvas_edit_sync = false;
                }
                undo_snapshot_capture("Resize Canvas");
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            if (g_mode_curve && g_curve_submode == CURVE_SUBMODE_VARIABLE && g_var_dragging) {
                g_var_dragging = false;
                ReleaseCapture();
                if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
                    BezierPath *bp = &g_paths[g_active_path_idx];
                    double s_a = g_var_drag_s_start;
                    double s_b = g_var_drag_s_cur;
                    if (fabs(s_b - s_a) >= 1.0 && (g_var_write_tunnel || g_var_write_prio)) {
                        bezier_path_add_span_from_s(
                            bp, s_a, s_b,
                            g_var_write_tunnel, (uint8_t)g_var_tunnel_val,
                            g_var_write_prio, (uint8_t)g_var_prio_val
                        );
                        undo_snapshot_capture("Set Curve Variable");
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            if (g_obj_drag_state == OBJ_DRAG_MARQUEE) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                int dx = abs(mx - g_obj_marquee_start_x);
                int dy = abs(my - g_obj_marquee_start_y);
                if (dx < 4 && dy < 4) {
                    if (g_obj_marquee_mode == MARQUEE_MODE_REPLACE) {
                        bool hit_any = false;
                        if (g_mode_overlay) {
                            if (g_hovered_cutout_idx >= 0 || g_hovered_sprite_idx >= 0 || g_hovered_bgalpha_idx >= 0) hit_any = true;
                        } else if (g_mode_element) {
                            if (g_frog_hovered || g_hovered_coin_idx >= 0) hit_any = true;
                        } else if (g_mode_canvas) {
                            RECT rc_client;
                            GetClientRect(hwnd, &rc_client);
                            int client_w = rc_client.right - rc_client.left;
                            int work_h = get_workspace_height(hwnd);
                            if (hit_test_canvas_handles(client_w, work_h, mx, my) != CANVAS_HANDLE_NONE) hit_any = true;
                        }
                        if (!hit_any) {
                            if (g_mode_canvas) deselect_all_canvas_handles();
                            else deselect_all_objects();
                        }
                    }
                }
                ReleaseCapture();
                g_obj_drag_state = OBJ_DRAG_NONE;
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            if (g_pen_drag_state == PEN_DRAG_MARQUEE) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                int dx = abs(mx - g_marquee_start_x);
                int dy = abs(my - g_marquee_start_y);
                if (dx < 4 && dy < 4) {
                    if (g_marquee_mode == MARQUEE_MODE_REPLACE) {
                        /* Single Ctrl-Click without dragging in empty space: deselect all paths & nodes */
                        g_active_path_idx = -1;
                        g_active_node_idx = -1;
                        for (int p = 0; p < g_path_count; ++p) {
                            for (int i = 0; i < g_paths[p].node_count; ++i) {
                                g_paths[p].nodes[i].selected = false;
                            }
                        }
                    }
                }
                free_marquee_snapshot();
                ReleaseCapture();
                g_pen_drag_state = PEN_DRAG_NONE;
                g_drag_path_idx = -1;
                g_drag_node_idx = -1;
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            if (g_pen_drag_state != PEN_DRAG_NONE) {
                bool was_new_node = (g_pen_drag_state == PEN_DRAG_NEW_HANDLE);
                if (g_pen_drag_state == PEN_DRAG_ANCHOR && g_drag_path_idx >= 0 && g_drag_path_idx < g_path_count) {
                    bezier_path_prune_collapsed_spans(&g_paths[g_drag_path_idx]);
                }
                ReleaseCapture();
                g_pen_drag_state = PEN_DRAG_NONE;
                g_drag_path_idx = -1;
                g_drag_node_idx = -1;
                undo_snapshot_capture(was_new_node ? "Add Curve Node" : "Edit Curve Node");
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            break;
        }

        case WM_LBUTTONDBLCLK: {
            if (g_mode_curve && g_has_canvas) {
                RECT rc_client;
                GetClientRect(hwnd, &rc_client);
                int client_w = rc_client.right - rc_client.left;
                int work_h = get_workspace_height(hwnd);
                RECT rc_canvas;
                get_canvas_screen_rect(client_w, work_h, &rc_canvas);

                int hit_p = -1, hit_n = -1, hit_type = -1;
                if (hit_test_bezier(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &rc_canvas, &hit_p, &hit_n, &hit_type) && hit_type == 0) {
                    BezierNode *n = &g_paths[hit_p].nodes[hit_n];
                    if (hypotf(n->hin_x, n->hin_y) > 0.5f || hypotf(n->hout_x, n->hout_y) > 0.5f) {
                        /* Has handles: Convert to Sharp Corner (retract handles) */
                        bezier_path_make_corner_node(&g_paths[hit_p], hit_n);
                    } else {
                        /* Sharp corner: Convert to Auto Smooth with collinear tangent */
                        bezier_path_smooth_node(&g_paths[hit_p], hit_n);
                    }
                    path_cache_invalidate(&g_paths[hit_p]);
                    g_active_path_idx = hit_p;
                    g_active_node_idx = hit_n;
                    undo_snapshot_capture("Toggle Node Smooth");
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
            }
            break;
        }

        case WM_RBUTTONDOWN: {
            /* 1. If currently adjusting BackgroundAlpha Motion, right-click MUST ONLY CANCEL motion adjustment!
                  NEVER delete the BackgroundAlpha or anything else! */
            if (g_placing_bgalpha_vector && g_editing_bgalpha_idx >= 0) {
                g_placing_bgalpha_vector = false;
                g_editing_bgalpha_idx = -1;
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            /* 1b. If currently adjusting Overlay Priority, right-click MUST ONLY CANCEL priority adjustment!
                  NEVER delete the Cutout or Sprite item! */
            if (g_adjusting_overlay_priority) {
                cancel_overlay_priority_adjustment(hwnd);
                return 0;
            }

            /* 2. If placing a new object / vector, right-click cancels placement */
            if (g_placing_frog || g_placing_coin || g_placing_cutout || g_placing_sprite || g_placing_bgalpha || g_placing_bgalpha_vector) {
                g_placing_frog = false;
                g_placing_coin = false;
                g_placing_cutout = false;
                g_placing_sprite = false;
                g_placing_bgalpha = false;
                g_placing_bgalpha_vector = false;
                g_editing_bgalpha_idx = -1;
                free_pending_cutout();
                free_pending_sprite();
                free_pending_bgalpha();
                if (g_btn_frog) InvalidateRect(g_btn_frog, NULL, TRUE);
                if (g_btn_coin) InvalidateRect(g_btn_coin, NULL, TRUE);
                if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            if (g_mode_curve && g_has_canvas) {
                if (g_curve_submode == CURVE_SUBMODE_VARIABLE) {
                    bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (is_shift) {
                        /* Shift + RMB: Remove attribute span at cursor */
                        RECT rc_client;
                        GetClientRect(hwnd, &rc_client);
                        int client_w = rc_client.right - rc_client.left;
                        int work_h = get_workspace_height(hwnd);
                        float cx, cy;
                        screen_to_canvas(client_w, work_h, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &cx, &cy);
                        if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
                            BezierPath *active_bp = &g_paths[g_active_path_idx];
                            double dist_sq = 0.0;
                            double s_hit = project_s_window(active_bp, cx, cy, -1.0, 0.0, &dist_sq);
                            if (dist_sq <= 15.0 * 15.0) {
                                for (int sp_idx = active_bp->span_count - 1; sp_idx >= 0; --sp_idx) {
                                    if (active_bp->cache.baked_spans && sp_idx < active_bp->cache.baked_span_count) {
                                        BakedSpan *b = &active_bp->cache.baked_spans[sp_idx];
                                        if (b->active && s_hit >= b->s_c && s_hit < b->s_d) {
                                            bezier_path_remove_span(active_bp, sp_idx);
                                            InvalidateRect(hwnd, NULL, FALSE);
                                            update_status_bar();
                                            return 0;
                                        }
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    /* Plain RMB Click: Shortcut to toggle Unhittable boolean setting */
                    g_var_tunnel_val = (g_var_tunnel_val == 1) ? 0 : 1;
                    g_var_write_tunnel = true;
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                } else if (g_curve_submode == CURVE_SUBMODE_DRAW) {
                    RECT rc_client;
                    GetClientRect(hwnd, &rc_client);
                    int client_w = rc_client.right - rc_client.left;
                    int work_h = get_workspace_height(hwnd);
                    RECT rc_canvas;
                    get_canvas_screen_rect(client_w, work_h, &rc_canvas);

                    bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (is_shift) {
                        /* Shift + RMB: Delete clicked path directly */
                        int hit_p = -1, hit_n = -1, hit_type = -1;
                        if (hit_test_bezier(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &rc_canvas, &hit_p, &hit_n, &hit_type)) {
                            /* Hit anchor or handle of hit_p */
                        } else {
                            /* Test proximity to curve geometry */
                            float cx, cy;
                            screen_to_canvas(client_w, work_h, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &cx, &cy);
                            for (int p = 0; p < g_path_count; ++p) {
                                double dist_sq = 0.0;
                                project_s_window(&g_paths[p], cx, cy, -1.0, 0.0, &dist_sq);
                                float max_d = 20.0f / g_zoom;
                                if (dist_sq <= (double)(max_d * max_d)) {
                                    hit_p = p;
                                    break;
                                }
                            }
                        }
                        if (hit_p >= 0 && hit_p < g_path_count) {
                            bezier_path_free(&g_paths[hit_p]);
                            memset(&g_paths[hit_p], 0, sizeof(BezierPath));
                            compact_empty_paths();
                            undo_snapshot_capture("Delete Curve Path");
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                            return 0;
                        }
                        return 0;
                    }

                    int hit_p = -1, hit_n = -1, hit_type = -1;
                    if (hit_test_bezier(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &rc_canvas, &hit_p, &hit_n, &hit_type)) {
                        BezierPath *bp = &g_paths[hit_p];
                        if (hit_type == 0) {
                            /* RMB on Anchor: Quick delete node */
                            bezier_path_remove_node(bp, hit_n);
                            bezier_path_prune_collapsed_spans(bp);
                            if (bp->node_count == 0) {
                                compact_empty_paths();
                            } else if (g_active_node_idx == hit_n) {
                                g_active_node_idx = (hit_n < bp->node_count) ? hit_n : bp->node_count - 1;
                            }
                            undo_snapshot_capture("Delete Curve Node");
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                            return 0;
                        } else if (hit_type == 1) {
                            /* RMB on Outgoing handle: Retract hout */
                            bp->nodes[hit_n].hout_x = 0.0f;
                            bp->nodes[hit_n].hout_y = 0.0f;
                            bp->nodes[hit_n].type = NODE_CUSP;
                            path_cache_invalidate(bp);
                            undo_snapshot_capture("Retract Handle");
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                            return 0;
                        } else if (hit_type == 2) {
                            /* RMB on Incoming handle: Retract hin */
                            bp->nodes[hit_n].hin_x = 0.0f;
                            bp->nodes[hit_n].hin_y = 0.0f;
                            bp->nodes[hit_n].type = NODE_CUSP;
                            path_cache_invalidate(bp);
                            undo_snapshot_capture("Retract Handle");
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                            return 0;
                        }
                    } else {
                        /* RMB on Empty Canvas: Finish/deselect current path */
                        g_active_path_idx = -1;
                        g_active_node_idx = -1;
                        g_pen_drag_state = PEN_DRAG_NONE;
                        for (int p = 0; p < g_path_count; ++p) {
                            for (int i = 0; i < g_paths[p].node_count; ++i) {
                                g_paths[p].nodes[i].selected = false;
                            }
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                        return 0;
                    }
                }
            }
            if (g_mode_element && g_frog_hovered && g_has_frog) {
                /* Frog is permanent in ZuPather and cannot be deleted, only repositioned */
                g_frog_selected = false;
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            if (g_mode_element && g_hovered_coin_idx >= 0 && g_hovered_coin_idx < g_treasure_point_count) {
                for (int i = g_hovered_coin_idx; i < g_treasure_point_count - 1; i++) {
                    g_treasure_points[i] = g_treasure_points[i + 1];
                }
                g_treasure_point_count--;
                g_hovered_coin_idx = -1;
                undo_snapshot_capture("Delete Coin");
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            if (g_mode_overlay && g_hovered_cutout_idx >= 0 && g_hovered_cutout_idx < g_cutout_count) {
                int idx = g_hovered_cutout_idx;
                free_cutout(&g_cutouts[idx]);
                for (int i = idx; i < g_cutout_count - 1; i++) {
                    g_cutouts[i] = g_cutouts[i + 1];
                }
                g_cutout_count--;
                g_hovered_cutout_idx = -1;
                sync_cutout_image_names();
                if (g_selected_cutout_idx >= g_cutout_count) {
                    g_selected_cutout_idx = g_cutout_count - 1;
                    if (g_selected_cutout_idx < 0) g_selected_cutout_idx = 0;
                }
                undo_snapshot_capture("Delete Cutout");
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            if (g_mode_overlay && g_hovered_sprite_idx >= 0 && g_hovered_sprite_idx < g_sprite_count) {
                int idx = g_hovered_sprite_idx;
                free_sprite(&g_sprites[idx]);
                for (int i = idx; i < g_sprite_count - 1; i++) {
                    g_sprites[i] = g_sprites[i + 1];
                }
                g_sprite_count--;
                g_hovered_sprite_idx = -1;
                sync_sprite_image_names();
                if (g_selected_sprite_idx >= g_sprite_count) {
                    g_selected_sprite_idx = g_sprite_count - 1;
                    if (g_selected_sprite_idx < 0) g_selected_sprite_idx = 0;
                }
                undo_snapshot_capture("Delete Sprite");
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            if (g_mode_overlay && g_hovered_bgalpha_idx >= 0 && g_hovered_bgalpha_idx < g_bg_alpha_count) {
                int idx = g_hovered_bgalpha_idx;
                free_bgalpha(&g_bg_alphas[idx]);
                for (int i = idx; i < g_bg_alpha_count - 1; i++) {
                    g_bg_alphas[i] = g_bg_alphas[i + 1];
                }
                g_bg_alpha_count--;
                g_hovered_bgalpha_idx = -1;
                sync_bgalpha_image_names();
                undo_snapshot_capture("Delete Background Alpha");
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            return 0;
        }

        case WM_CAPTURECHANGED: {
            g_is_dragging_guide_thumb = false;
            g_is_dragging_guide_bg = false;
            g_is_dragging_ov_guide_thumb = false;
            g_is_dragging_ov_guide_bg = false;
            g_is_dragging_attr_thumb = false;
            g_is_dragging_attr_bg = false;
            g_is_dragging_ov_tools_thumb = false;
            g_is_dragging_ov_tools_bg = false;
            g_is_dragging_el_guide_thumb = false;
            g_is_dragging_el_guide_bg = false;
            g_is_dragging_el_tools_thumb = false;
            g_is_dragging_el_tools_bg = false;
            g_is_dragging_cv_guide_thumb = false;
            g_is_dragging_cv_guide_bg = false;
            g_is_dragging_cv_tools_thumb = false;
            g_is_dragging_cv_tools_bg = false;
            g_is_dragging_prev_guide_thumb = false;
            g_is_dragging_prev_guide_bg = false;
            g_is_dragging_prev_info_thumb = false;
            g_is_dragging_prev_info_bg = false;
            g_drag_canvas_handle = CANVAS_HANDLE_NONE;
            g_is_panning = false;
            if (g_pen_drag_state == PEN_DRAG_MARQUEE) {
                free_marquee_snapshot();
            }
            g_pen_drag_state = PEN_DRAG_NONE;
            g_obj_drag_state = OBJ_DRAG_NONE;
            g_drag_path_idx = -1;
            g_drag_node_idx = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_KEYUP: {
            if (wParam == VK_SHIFT && g_mode_curve) {
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }
            break;
        }

        case WM_KEYDOWN: {
            bool ctrl_down  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt_down   = (GetKeyState(VK_MENU) & 0x8000) != 0;

            if (ctrl_down && !shift_down && !alt_down) {
                if (wParam == 'Z' || wParam == 'z') {
                    undo_perform(hwnd);
                    return 0;
                }
                if (wParam == 'Y' || wParam == 'y') {
                    undo_redo_perform(hwnd);
                    return 0;
                }
                if (wParam == 'B' || wParam == 'b') {
                    prompt_change_background_image(hwnd);
                    return 0;
                }
            }
            if (ctrl_down && shift_down && !alt_down) {
                if (wParam == 'Z' || wParam == 'z') {
                    undo_redo_perform(hwnd);
                    return 0;
                }
                if (wParam == 'M' || wParam == 'm') {
                    show_manage_levels_dialog(hwnd);
                    return 0;
                }
            }
            if (ctrl_down && !shift_down && alt_down) {
                if (wParam == 'B' || wParam == 'b') {
                    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_MENU_CANVAS_REMOVE_IMG, 0), 0);
                    return 0;
                }
            }

            if (wParam == VK_ESCAPE) {
                if (g_adjusting_overlay_priority) {
                    cancel_overlay_priority_adjustment(hwnd);
                    return 0;
                }
                if (g_mode_canvas && is_any_canvas_handle_selected()) {
                    deselect_all_canvas_handles();
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
                if (g_placing_frog || g_placing_coin || g_placing_cutout || g_placing_sprite || g_placing_bgalpha || g_placing_bgalpha_vector) {
                    g_placing_frog = false;
                    g_placing_coin = false;
                    g_placing_cutout = false;
                    g_placing_sprite = false;
                    g_placing_bgalpha = false;
                    g_placing_bgalpha_vector = false;
                    g_editing_bgalpha_idx = -1;
                    free_pending_cutout();
                    free_pending_sprite();
                    free_pending_bgalpha();
                    if (g_btn_frog) InvalidateRect(g_btn_frog, NULL, TRUE);
                    if (g_btn_coin) InvalidateRect(g_btn_coin, NULL, TRUE);
                    if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                }
                return 0;
            }

            if (wParam == VK_F1) {
                /* Global toggle for Guide panel collapse / expand */
                if (g_mode_canvas) {
                    g_hud_canvas_guide_collapsed = !g_hud_canvas_guide_collapsed;
                    g_hud_canvas_guide_scroll_y = 0;
                } else if (g_mode_overlay) {
                    g_hud_overlay_guide_collapsed = !g_hud_overlay_guide_collapsed;
                    g_hud_overlay_guide_scroll_y = 0;
                } else if (g_mode_element) {
                    g_hud_element_guide_collapsed = !g_hud_element_guide_collapsed;
                    g_hud_element_guide_scroll_y = 0;
                } else if (g_mode_curve) {
                    g_hud_shortcuts_collapsed = !g_hud_shortcuts_collapsed;
                    g_hud_guide_scroll_y = 0;
                } else {
                    g_hud_preview_guide_collapsed = !g_hud_preview_guide_collapsed;
                    g_hud_preview_guide_scroll_y = 0;
                }
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            if (wParam == VK_F2) {
                if (g_mode_canvas) {
                    /* Global toggle for Canvas Tools panel collapse / expand */
                    g_hud_canvas_collapsed = !g_hud_canvas_collapsed;
                    g_hud_canvas_tools_scroll_y = 0;
                    if (g_hud_canvas_collapsed) {
                        if (g_edit_cv_w) ShowWindow(g_edit_cv_w, SW_HIDE);
                        if (g_edit_cv_h) ShowWindow(g_edit_cv_h, SW_HIDE);
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                } else if (g_mode_curve) {
                    /* Global toggle for Curve Attribute panel collapse / expand */
                    g_hud_mode_collapsed = !g_hud_mode_collapsed;
                    g_hud_attr_scroll_y = 0;
                    if (g_hud_mode_collapsed && g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                } else if (g_mode_overlay) {
                    /* Global toggle for Overlay Tools panel collapse / expand */
                    g_hud_overlay_collapsed = !g_hud_overlay_collapsed;
                    g_hud_overlay_tools_scroll_y = 0;
                    if (g_hud_overlay_collapsed && g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                } else if (g_mode_element) {
                    /* Global toggle for Element Tools panel collapse / expand */
                    g_hud_element_collapsed = !g_hud_element_collapsed;
                    g_hud_element_tools_scroll_y = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                } else {
                    /* Preview Mode: Map Attribute HUD */
                    g_hud_preview_info_collapsed = !g_hud_preview_info_collapsed;
                    g_hud_preview_info_scroll_y = 0;
                    if (g_hud_preview_info_collapsed) {
                        hide_all_map_attr_edits();
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
            }

            if (g_mode_curve && wParam == VK_TAB) {
                /* Single shortcut toggle between Draw Mode and Attribute Mode */
                g_curve_submode = (g_curve_submode == CURVE_SUBMODE_DRAW) ? CURVE_SUBMODE_VARIABLE : CURVE_SUBMODE_DRAW;
                g_hud_guide_scroll_y = 0;
                g_hud_attr_scroll_y = 0;
                if (g_curve_submode == CURVE_SUBMODE_DRAW && g_edit_prio) {
                    ShowWindow(g_edit_prio, SW_HIDE);
                }
                InvalidateRect(hwnd, NULL, FALSE);
                update_status_bar();
                return 0;
            }

            if (ctrl_down && shift_down && (wParam == 'I' || wParam == 'i')) {
                handle_import_svg_path(hwnd);
                return 0;
            }

            if (ctrl_down && shift_down && (wParam == 'E' || wParam == 'e')) {
                handle_export_svg_path(hwnd);
                return 0;
            }

            if (ctrl_down && shift_down && (wParam == 'O' || wParam == 'o')) {
                toggle_overlay_mode(hwnd);
                return 0;
            }

            if (ctrl_down && shift_down && (wParam == 'B' || wParam == 'b')) {
                trigger_add_bgalpha(hwnd);
                return 0;
            }

            if (ctrl_down && shift_down && (wParam == 'S' || wParam == 's')) {
                trigger_add_sprite(hwnd);
                return 0;
            }

            if (ctrl_down && shift_down && (wParam == 'C' || wParam == 'c')) {
                trigger_add_cutout(hwnd);
                return 0;
            }

            if (ctrl_down && !shift_down && (wParam == 'M' || wParam == 'm')) {
                SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CANVAS, BN_CLICKED), 0);
                return 0;
            }

            if (ctrl_down && !shift_down && (wParam == 'E' || wParam == 'e')) {
                SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_ELEMENT, BN_CLICKED), 0);
                return 0;
            }

            if (ctrl_down && !shift_down && (wParam == 'L' || wParam == 'l')) {
                toggle_level_preview(hwnd);
                return 0;
            }

            if (g_mode_curve) {
                if (g_curve_submode == CURVE_SUBMODE_VARIABLE) {
                    if (!ctrl_down && !shift_down && (wParam == 'U' || wParam == 'u')) {
                        g_var_tunnel_val = (g_var_tunnel_val == 1) ? 0 : 1;
                        g_var_write_tunnel = true;
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                        return 0;
                    }
                    if (!ctrl_down && (wParam == VK_OEM_PLUS || wParam == VK_ADD || wParam == '=')) {
                        g_var_prio_val++;
                        g_var_write_prio = true;
                        if (g_edit_prio) {
                            char buf[16];
                            snprintf(buf, sizeof(buf), "%u", g_var_prio_val);
                            g_in_prio_update = true;
                            SetWindowTextA(g_edit_prio, buf);
                            g_in_prio_update = false;
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                        return 0;
                    }
                    if (!ctrl_down && (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT || wParam == '-')) {
                        if (g_var_prio_val > 0) g_var_prio_val--;
                        g_var_write_prio = true;
                        if (g_edit_prio) {
                            char buf[16];
                            snprintf(buf, sizeof(buf), "%u", g_var_prio_val);
                            g_in_prio_update = true;
                            SetWindowTextA(g_edit_prio, buf);
                            g_in_prio_update = false;
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                        return 0;
                    }
                    /* Attribute Mode locks all bezier geometry modifications */
                    return 0;
                }
                if (wParam == VK_DELETE || wParam == VK_BACK) {
                    if (shift_down || ctrl_down) {
                        /* Delete entire path(s) that contain selected node(s), or active path */
                        bool deleted_any = false;
                        for (int p = 0; p < g_path_count; ++p) {
                            bool path_has_selected = false;
                            for (int i = 0; i < g_paths[p].node_count; ++i) {
                                if (g_paths[p].nodes[i].selected) {
                                    path_has_selected = true;
                                    break;
                                }
                            }
                            if (path_has_selected) {
                                bezier_path_free(&g_paths[p]);
                                memset(&g_paths[p], 0, sizeof(BezierPath));
                                deleted_any = true;
                            }
                        }
                        if (!deleted_any && g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
                            /* Fallback: delete active path if no individual node was selected */
                            bezier_path_free(&g_paths[g_active_path_idx]);
                            memset(&g_paths[g_active_path_idx], 0, sizeof(BezierPath));
                            deleted_any = true;
                        }
                        g_active_node_idx = -1;
                        compact_empty_paths();
                        if (deleted_any) {
                            undo_snapshot_capture("Delete Curve Path");
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                        return 0;
                    }

                    /* Regular Delete: Delete individual selected node(s) without destroying the path */
                    bool any_node_deleted = false;
                    for (int p = 0; p < g_path_count; ++p) {
                        BezierPath *bp = &g_paths[p];
                        bool path_changed = false;
                        for (int i = bp->node_count - 1; i >= 0; --i) {
                            if (bp->nodes[i].selected) {
                                bezier_path_remove_node(bp, i);
                                path_changed = true;
                                any_node_deleted = true;
                            }
                        }
                        if (path_changed) {
                            bezier_path_prune_collapsed_spans(bp);
                        }
                    }
                    g_active_node_idx = -1;
                    compact_empty_paths();
                    if (any_node_deleted) {
                        undo_snapshot_capture("Delete Curve Node");
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
                if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN) {
                    /* Nudge coordinates: 1 pixel per keypress */
                    float dx = 0.0f;
                    float dy = 0.0f;
                    if (wParam == VK_LEFT)  dx = -1.0f;
                    if (wParam == VK_RIGHT) dx =  1.0f;
                    if (wParam == VK_UP)    dy = -1.0f;
                    if (wParam == VK_DOWN)  dy =  1.0f;

                    if (shift_down) {
                        /* Shift + Arrow Keys: Nudge entire selected path(s) by 1px */
                        bool any_nudged = false;
                        for (int p = 0; p < g_path_count; ++p) {
                            bool path_has_selected = (p == g_active_path_idx);
                            if (!path_has_selected) {
                                for (int i = 0; i < g_paths[p].node_count; ++i) {
                                    if (g_paths[p].nodes[i].selected) {
                                        path_has_selected = true;
                                        break;
                                    }
                                }
                            }
                            if (path_has_selected) {
                                BezierPath *bp = &g_paths[p];
                                for (int i = 0; i < bp->node_count; ++i) {
                                    bp->nodes[i].x += dx;
                                    bp->nodes[i].y += dy;
                                }
                                path_cache_invalidate(bp);
                                any_nudged = true;
                            }
                        }
                        if (any_nudged) {
                            undo_snapshot_capture("Nudge Curve Path");
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                        }
                        return 0;
                    } else {
                        /* Arrow Keys alone: Nudge selected anchor point(s) by 1px */
                        int nudge_cnt = 0;
                        for (int p = 0; p < g_path_count; ++p) {
                            BezierPath *bp = &g_paths[p];
                            bool path_changed = false;
                            for (int i = 0; i < bp->node_count; ++i) {
                                if (bp->nodes[i].selected) {
                                    bp->nodes[i].x += dx;
                                    bp->nodes[i].y += dy;
                                    path_changed = true;
                                    nudge_cnt++;
                                }
                            }
                            if (path_changed) {
                                bezier_path_prune_collapsed_spans(bp);
                                path_cache_invalidate(bp);
                            }
                        }
                        /* Fallback: if no node was marked selected, nudge active node */
                        if (nudge_cnt == 0 && g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
                            BezierPath *bp = &g_paths[g_active_path_idx];
                            if (g_active_node_idx >= 0 && g_active_node_idx < bp->node_count) {
                                bp->nodes[g_active_node_idx].x += dx;
                                bp->nodes[g_active_node_idx].y += dy;
                                bp->nodes[g_active_node_idx].selected = true;
                                bezier_path_prune_collapsed_spans(bp);
                                path_cache_invalidate(bp);
                                nudge_cnt++;
                            }
                        }
                        if (nudge_cnt > 0) {
                            undo_snapshot_capture("Nudge Curve Node");
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                        }
                        return 0;
                    }
                }
                if (!ctrl_down && (wParam == 'C' || wParam == 'c')) {
                    /* Convert selected nodes to Sharp Corner (retract handles) */
                    int cnt = 0;
                    for (int p = 0; p < g_path_count; ++p) {
                        bool path_changed = false;
                        for (int i = 0; i < g_paths[p].node_count; ++i) {
                            if (g_paths[p].nodes[i].selected) {
                                bezier_path_make_corner_node(&g_paths[p], i);
                                path_changed = true;
                                cnt++;
                            }
                        }
                        if (path_changed) {
                            path_cache_invalidate(&g_paths[p]);
                        }
                    }
                    if (cnt == 0 && g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
                        BezierPath *bp = &g_paths[g_active_path_idx];
                        if (g_active_node_idx >= 0 && g_active_node_idx < bp->node_count) {
                            bezier_path_make_corner_node(bp, g_active_node_idx);
                            bp->nodes[g_active_node_idx].selected = true;
                            path_cache_invalidate(bp);
                            cnt++;
                        }
                    }
                    if (cnt > 0) {
                        undo_snapshot_capture("Corner Node");
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                    }
                    return 0;
                }
                if (!ctrl_down && (wParam == 'S' || wParam == 's')) {
                    /* Convert selected nodes to Auto Smooth with collinear tangent */
                    int cnt = 0;
                    for (int p = 0; p < g_path_count; ++p) {
                        bool path_changed = false;
                        for (int i = 0; i < g_paths[p].node_count; ++i) {
                            if (g_paths[p].nodes[i].selected) {
                                bezier_path_smooth_node(&g_paths[p], i);
                                path_changed = true;
                                cnt++;
                            }
                        }
                        if (path_changed) {
                            path_cache_invalidate(&g_paths[p]);
                        }
                    }
                    if (cnt == 0 && g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
                        BezierPath *bp = &g_paths[g_active_path_idx];
                        if (g_active_node_idx >= 0 && g_active_node_idx < bp->node_count) {
                            bezier_path_smooth_node(bp, g_active_node_idx);
                            bp->nodes[g_active_node_idx].selected = true;
                            path_cache_invalidate(bp);
                            cnt++;
                        }
                    }
                    if (cnt > 0) {
                        undo_snapshot_capture("Smooth Node");
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                    }
                    return 0;
                }
                if (!ctrl_down && (wParam == 'R' || wParam == 'r')) {
                    /* Reverse active path direction and swap handles */
                    if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
                        bezier_path_reverse(&g_paths[g_active_path_idx]);
                        undo_snapshot_capture("Reverse Path");
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                    }
                    return 0;
                }
                if (!ctrl_down && (wParam == 'X' || wParam == 'x')) {
                    /* Split active path at selected interior node */
                    if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count && g_path_count < MAX_PATHS) {
                        BezierPath *bp = &g_paths[g_active_path_idx];
                        int split_idx = -1;
                        for (int i = 1; i < bp->node_count - 1; ++i) {
                            if (bp->nodes[i].selected) {
                                split_idx = i;
                                break;
                            }
                        }
                        if (split_idx > 0) {
                            compact_empty_paths();
                            if (g_path_count >= 10) {
                                show_path_limit_warning(hwnd);
                                return 0;
                            }
                            BezierPath *new_path = &g_paths[g_path_count];
                            const char *base = g_canvas_name[0] ? g_canvas_name : "curve";
                            char new_name[64];
                            snprintf(new_name, sizeof(new_name), "%s-%d", base, g_path_count + 1);
                            bezier_path_init(new_path, new_name);
                            if (bezier_path_split_at_node(bp, split_idx, new_path)) {
                                g_path_count++;
                                g_active_path_idx = g_path_count - 1;
                                g_active_node_idx = 0;
                                undo_snapshot_capture("Split Path");
                                InvalidateRect(hwnd, NULL, FALSE);
                                update_status_bar();
                            } else {
                                bezier_path_free(new_path);
                            }
                        }
                    }
                    return 0;
                }
                if (!ctrl_down && (wParam == 'J' || wParam == 'j')) {
                    /* Join two selected track endpoints with auto flow alignment */
                    struct { int p; int n; } sel_ends[2];
                    int sel_cnt = 0;
                    for (int p = 0; p < g_path_count && sel_cnt < 2; ++p) {
                        for (int i = 0; i < g_paths[p].node_count && sel_cnt < 2; ++i) {
                            if (g_paths[p].nodes[i].selected) {
                                if (i == 0 || i == g_paths[p].node_count - 1) {
                                    sel_ends[sel_cnt].p = p;
                                    sel_ends[sel_cnt].n = i;
                                    sel_cnt++;
                                }
                            }
                        }
                    }
                    if (sel_cnt == 2) {
                        if (sel_ends[0].p == sel_ends[1].p) {
                            /* Same path: close loop */
                            BezierPath *bp = &g_paths[sel_ends[0].p];
                            bezier_path_join_paths(bp, sel_ends[0].n, NULL, sel_ends[1].n);
                            undo_snapshot_capture("Join Paths");
                            InvalidateRect(hwnd, NULL, FALSE);
                            update_status_bar();
                        } else {
                            /* Two different paths: merge path_b into path_a */
                            int p_a = sel_ends[0].p;
                            int p_b = sel_ends[1].p;
                            BezierPath *path_a = &g_paths[p_a];
                            BezierPath *path_b = &g_paths[p_b];
                            if (bezier_path_join_paths(path_a, sel_ends[0].n, path_b, sel_ends[1].n)) {
                                bezier_path_free(path_b);
                                memset(path_b, 0, sizeof(BezierPath));
                                compact_empty_paths();
                                g_active_path_idx = p_a;
                                g_active_node_idx = path_a->node_count - 1;
                                undo_snapshot_capture("Join Paths");
                                InvalidateRect(hwnd, NULL, FALSE);
                                update_status_bar();
                            }
                        }
                    }
                    return 0;
                }
                if (!ctrl_down && (wParam == VK_OEM_4 || wParam == VK_OEM_6)) {
                    /* Cycle active track with '[' and ']' */
                    if (g_path_count > 0) {
                        if (wParam == VK_OEM_4) {
                            g_active_path_idx = (g_active_path_idx - 1 + g_path_count) % g_path_count;
                        } else {
                            g_active_path_idx = (g_active_path_idx + 1) % g_path_count;
                        }
                        g_active_node_idx = -1; /* Single active track, no active nodes initially */
                        for (int p = 0; p < g_path_count; ++p) {
                            for (int i = 0; i < g_paths[p].node_count; ++i) {
                                g_paths[p].nodes[i].selected = false;
                            }
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                    }
                    return 0;
                }
                if (!ctrl_down && !shift_down && (wParam == 'N' || wParam == 'n')) {
                    compact_empty_paths();
                    if (g_path_count >= 10) {
                        show_path_limit_warning(hwnd);
                        return 0;
                    }
                    /* Finish active track and prepare to start new track */
                    g_active_path_idx = -1;
                    g_active_node_idx = -1;
                    g_pen_drag_state = PEN_DRAG_NONE;
                    for (int p = 0; p < g_path_count; ++p) {
                        for (int i = 0; i < g_paths[p].node_count; ++i) {
                            g_paths[p].nodes[i].selected = false;
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                    return 0;
                }
                if (wParam == VK_SHIFT) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    update_status_bar();
                }
            } else if (g_mode_canvas && g_has_canvas) {
                if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN) {
                    if (is_any_canvas_handle_selected()) {
                        int ndx = 0, ndy = 0;
                        if (wParam == VK_LEFT)  ndx = -1;
                        if (wParam == VK_RIGHT) ndx =  1;
                        if (wParam == VK_UP)    ndy = -1;
                        if (wParam == VK_DOWN)  ndy =  1;

                        float zoom = (g_zoom > 0.01f) ? g_zoom : 1.0f;

                        bool sel_r = g_canvas_handle_selected[CANVAS_HANDLE_RIGHT] ||
                                     g_canvas_handle_selected[CANVAS_HANDLE_TOP_RIGHT] ||
                                     g_canvas_handle_selected[CANVAS_HANDLE_BOTTOM_RIGHT];
                        bool sel_l = g_canvas_handle_selected[CANVAS_HANDLE_LEFT] ||
                                     g_canvas_handle_selected[CANVAS_HANDLE_TOP_LEFT] ||
                                     g_canvas_handle_selected[CANVAS_HANDLE_BOTTOM_LEFT];
                        bool sel_b = g_canvas_handle_selected[CANVAS_HANDLE_BOTTOM] ||
                                     g_canvas_handle_selected[CANVAS_HANDLE_BOTTOM_LEFT] ||
                                     g_canvas_handle_selected[CANVAS_HANDLE_BOTTOM_RIGHT];
                        bool sel_t = g_canvas_handle_selected[CANVAS_HANDLE_TOP] ||
                                     g_canvas_handle_selected[CANVAS_HANDLE_TOP_LEFT] ||
                                     g_canvas_handle_selected[CANVAS_HANDLE_TOP_RIGHT];
                        bool sel_c = g_canvas_handle_selected[CANVAS_HANDLE_CENTER_MOVE];

                        int delta_w = 0, delta_h = 0;
                        float pan_dx = 0.0f, pan_dy = 0.0f;
                        float shift_x = 0.0f, shift_y = 0.0f;

                        /* Width resizing / horizontal movement */
                        if (sel_r && !sel_l) {
                            delta_w += ndx;
                        } else if (sel_l && !sel_r) {
                            delta_w -= ndx;
                        } else if (sel_r && sel_l) {
                            /* Both edges selected: moving canvas horizontally */
                            pan_dx += (float)ndx * zoom;
                            shift_x += (float)(-ndx);
                        }

                        /* Height resizing / vertical movement */
                        if (sel_b && !sel_t) {
                            delta_h += ndy;
                        } else if (sel_t && !sel_b) {
                            delta_h -= ndy;
                        } else if (sel_b && sel_t) {
                            /* Both edges selected: moving canvas vertically */
                            pan_dy += (float)ndy * zoom;
                            shift_y += (float)(-ndy);
                        }

                        /* Center move handle */
                        if (sel_c) {
                            pan_dx += (float)ndx * zoom;
                            pan_dy += (float)ndy * zoom;
                            shift_x += (float)(-ndx);
                            shift_y += (float)(-ndy);
                        }

                        if (delta_w != 0 || delta_h != 0) {
                            int new_w = g_canvas_w + delta_w;
                            int new_h = g_canvas_h + delta_h;
                            if (new_w < 64) new_w = 64;
                            if (new_h < 64) new_h = 64;

                            int actual_dw = new_w - g_canvas_w;
                            int actual_dh = new_h - g_canvas_h;

                            if (sel_r && !sel_l) {
                                pan_dx += (float)actual_dw * 0.5f * zoom;
                            } else if (sel_l && !sel_r) {
                                pan_dx -= (float)actual_dw * 0.5f * zoom;
                                shift_x += (float)actual_dw;
                            }

                            if (sel_b && !sel_t) {
                                pan_dy += (float)actual_dh * 0.5f * zoom;
                            } else if (sel_t && !sel_b) {
                                pan_dy -= (float)actual_dh * 0.5f * zoom;
                                shift_y += (float)actual_dh;
                            }

                            g_canvas_w = new_w;
                            g_canvas_h = new_h;
                        }

                        g_pan_x += pan_dx;
                        g_pan_y += pan_dy;

                        if (shift_x != 0.0f || shift_y != 0.0f) {
                            shift_all_canvas_objects((int)roundf(shift_x), (int)roundf(shift_y));
                        }

                        if (g_edit_cv_w && g_edit_cv_h) {
                            g_in_canvas_edit_sync = true;
                            char buf_w[16], buf_h[16];
                            snprintf(buf_w, sizeof(buf_w), "%d", g_canvas_w);
                            snprintf(buf_h, sizeof(buf_h), "%d", g_canvas_h);
                            if (GetFocus() != g_edit_cv_w) SetWindowTextA(g_edit_cv_w, buf_w);
                            if (GetFocus() != g_edit_cv_h) SetWindowTextA(g_edit_cv_h, buf_h);
                            g_in_canvas_edit_sync = false;
                        }
                        if (g_edit_w && g_edit_h) {
                            char buf_w[16], buf_h[16];
                            snprintf(buf_w, sizeof(buf_w), "%d", g_canvas_w);
                            snprintf(buf_h, sizeof(buf_h), "%d", g_canvas_h);
                            if (GetFocus() != g_edit_w) SetWindowTextA(g_edit_w, buf_w);
                            if (GetFocus() != g_edit_h) SetWindowTextA(g_edit_h, buf_h);
                        }

                        update_window_title();
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                        return 0;
                    }
                }
            } else if (g_mode_overlay || g_mode_element) {
                /* Non-Curve Modes: Overlay Mode and Element Mode */
                if (wParam == VK_DELETE || wParam == VK_BACK) {
                    int sel_count = get_selected_object_count();
                    if (sel_count > 0) {
                        bool any_deleted = false;
                        if (g_mode_overlay) {
                            bool co_deleted = false;
                            for (int i = g_cutout_count - 1; i >= 0; --i) {
                                if (g_cutouts[i].selected) {
                                    free_cutout(&g_cutouts[i]);
                                    for (int j = i; j < g_cutout_count - 1; ++j) {
                                        g_cutouts[j] = g_cutouts[j + 1];
                                    }
                                    g_cutout_count--;
                                    co_deleted = true;
                                    any_deleted = true;
                                }
                            }
                            if (co_deleted) {
                                sync_cutout_image_names();
                                if (g_selected_cutout_idx >= g_cutout_count) {
                                    g_selected_cutout_idx = g_cutout_count - 1;
                                    if (g_selected_cutout_idx < 0) g_selected_cutout_idx = 0;
                                }
                            }

                            bool sp_deleted = false;
                            for (int i = g_sprite_count - 1; i >= 0; --i) {
                                if (g_sprites[i].selected) {
                                    free_sprite(&g_sprites[i]);
                                    for (int j = i; j < g_sprite_count - 1; ++j) {
                                        g_sprites[j] = g_sprites[j + 1];
                                    }
                                    g_sprite_count--;
                                    sp_deleted = true;
                                    any_deleted = true;
                                }
                            }
                            if (sp_deleted) {
                                sync_sprite_image_names();
                                if (g_selected_sprite_idx >= g_sprite_count) {
                                    g_selected_sprite_idx = g_sprite_count - 1;
                                    if (g_selected_sprite_idx < 0) g_selected_sprite_idx = 0;
                                }
                            }

                            bool ba_deleted = false;
                            for (int i = g_bg_alpha_count - 1; i >= 0; --i) {
                                if (g_bg_alphas[i].selected) {
                                    free_bgalpha(&g_bg_alphas[i]);
                                    for (int j = i; j < g_bg_alpha_count - 1; ++j) {
                                        g_bg_alphas[j] = g_bg_alphas[j + 1];
                                    }
                                    g_bg_alpha_count--;
                                    ba_deleted = true;
                                    any_deleted = true;
                                }
                            }
                            if (ba_deleted) {
                                sync_bgalpha_image_names();
                                if (g_selected_bgalpha_idx >= g_bg_alpha_count) {
                                    g_selected_bgalpha_idx = g_bg_alpha_count - 1;
                                    if (g_selected_bgalpha_idx < 0) g_selected_bgalpha_idx = 0;
                                }
                            }
                        } else if (g_mode_element) {
                            /* Element Mode: Coins (Frog cannot be deleted, only repositioned) */
                            for (int i = g_treasure_point_count - 1; i >= 0; --i) {
                                if (g_treasure_points[i].selected) {
                                    for (int j = i; j < g_treasure_point_count - 1; ++j) {
                                        g_treasure_points[j] = g_treasure_points[j + 1];
                                    }
                                    g_treasure_point_count--;
                                    any_deleted = true;
                                }
                            }
                        }
                        if (any_deleted) {
                            undo_snapshot_capture("Delete Objects");
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                        return 0;
                    }
                }

                if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN) {
                    int sel_count = get_selected_object_count();
                    if (sel_count > 0) {
                        int dx = 0, dy = 0;
                        if (wParam == VK_LEFT)  dx = -1;
                        if (wParam == VK_RIGHT) dx =  1;
                        if (wParam == VK_UP)    dy = -1;
                        if (wParam == VK_DOWN)  dy =  1;

                        if (g_mode_overlay) {
                            for (int i = 0; i < g_cutout_count; ++i) {
                                if (g_cutouts[i].selected) {
                                    g_cutouts[i].x += dx;
                                    g_cutouts[i].y += dy;
                                }
                            }
                            for (int i = 0; i < g_sprite_count; ++i) {
                                if (g_sprites[i].selected) {
                                    g_sprites[i].x += dx;
                                    g_sprites[i].y += dy;
                                }
                            }
                            for (int i = 0; i < g_bg_alpha_count; ++i) {
                                if (g_bg_alphas[i].selected) {
                                    g_bg_alphas[i].x += dx;
                                    g_bg_alphas[i].y += dy;
                                }
                            }
                        } else if (g_mode_element) {
                            /* Element Mode: Frog & Coins */
                            if (g_frog_selected && g_has_frog) {
                                g_frog_gx += dx;
                                g_frog_gy += dy;
                            }
                            for (int i = 0; i < g_treasure_point_count; ++i) {
                                if (g_treasure_points[i].selected) {
                                    g_treasure_points[i].x += dx;
                                    g_treasure_points[i].y += dy;
                                }
                            }
                        }
                        undo_snapshot_capture("Move Object");
                        InvalidateRect(hwnd, NULL, FALSE);
                        update_status_bar();
                        return 0;
                    }
                }

                if (g_mode_overlay && GetFocus() != g_edit_prio) {
                    if (ctrl_down && !shift_down && (wParam == 'J' || wParam == 'j')) {
                        toggle_overlay_variable_mode(hwnd);
                        return 0;
                    }
                }
            }

            if (ctrl_down) {
                if (wParam == 'U' || wParam == 'u') {
                    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CURVE, BN_CLICKED), 0);
                    return 0;
                }
                else if (wParam == 'A' || wParam == 'a') {
                    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_MENU_SELECT_ALL, 0), 0);
                    return 0;
                }
                else if (wParam == 'I' || wParam == 'i') {
                    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_MENU_INVERT_SEL, 0), 0);
                    return 0;
                }
                else if (wParam == 'D' || wParam == 'd') {
                    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_MENU_DESELECT_ALL, 0), 0);
                    return 0;
                }
                else if (wParam == 'P' || wParam == 'p') {
                    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_FROG, BN_CLICKED), 0);
                    return 0;
                }
                else if (wParam == 'K' || wParam == 'k') {
                    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_COIN, BN_CLICKED), 0);
                    return 0;
                }
                else if (wParam == '1') {
                    if (g_has_canvas) {
                        g_zoom = 1.0f;
                        update_status_bar();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    return 0;
                }
                else if (wParam == 'R' || wParam == 'r') {
                    if (g_has_canvas) {
                        g_pan_x = 0.0f;
                        g_pan_y = 0.0f;
                        update_status_bar();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    return 0;
                }
                else if (wParam == '0') {
                    zoom_to_fit(hwnd);
                    return 0;
                }
                else if (wParam == 'T' || wParam == 't') {
                    apply_theme(hwnd, (g_current_theme == THEME_LIGHT) ? THEME_DARK : THEME_LIGHT);
                    return 0;
                }
                else if (wParam == 'G' || wParam == 'g') {
                    g_show_grid = !g_show_grid;
                    save_zuma_config();
                    HMENU hmenu = GetMenu(hwnd);
                    if (hmenu) {
                        CheckMenuItem(hmenu, ID_MENU_VIEW_GRID, g_show_grid ? MF_CHECKED : MF_UNCHECKED);
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                else if (!shift_down && (wParam == 'N' || wParam == 'n')) {
                    show_new_canvas_dialog(hwnd);
                    return 0;
                }
                else if (!shift_down && (wParam == 'O' || wParam == 'o')) {
                    show_open_canvas_dialog(hwnd);
                    return 0;
                }
                else if (!shift_down && (wParam == 'S' || wParam == 's')) {
                    show_save_canvas_dialog(hwnd);
                    return 0;
                }
                else if (wParam == VK_OEM_PLUS || wParam == VK_ADD || wParam == '=') {
                    if (g_has_canvas) {
                        float new_zoom = g_zoom * 1.25f;
                        if (new_zoom <= 8.0f) {
                            g_zoom = new_zoom;
                            update_status_bar();
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                    return 0;
                }
                else if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT || wParam == '-') {
                    if (g_has_canvas) {
                        float new_zoom = g_zoom / 1.25f;
                        if (new_zoom >= 0.25f) {
                            g_zoom = new_zoom;
                            update_status_bar();
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                    return 0;
                }
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc_client;
            GetClientRect(hwnd, &rc_client);
            int client_w = rc_client.right - rc_client.left;
            int client_h = rc_client.bottom - rc_client.top;

            render_scene(hwnd, hdc, client_w, client_h);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            save_zuma_config();
            if (s_hMenuCbtHook) {
                UnhookWindowsHookEx(s_hMenuCbtHook);
                s_hMenuCbtHook = NULL;
            }
            free_marquee_snapshot();
            free_pending_cutout();
            free_pending_sprite();
            free_pending_bgalpha();
            for (int i = 0; i < g_bg_alpha_count; ++i) {
                free_bgalpha(&g_bg_alphas[i]);
            }
            g_bg_alpha_count = 0;
            if (g_default_bgalpha_hdc_light) { DeleteDC(g_default_bgalpha_hdc_light); g_default_bgalpha_hdc_light = NULL; }
            if (g_default_bgalpha_mask_light) { DeleteObject(g_default_bgalpha_mask_light); g_default_bgalpha_mask_light = NULL; }
            if (g_default_bgalpha_hdc_dark) { DeleteDC(g_default_bgalpha_hdc_dark); g_default_bgalpha_hdc_dark = NULL; }
            if (g_default_bgalpha_mask_dark) { DeleteObject(g_default_bgalpha_mask_dark); g_default_bgalpha_mask_dark = NULL; }
            if (g_has_default_bgalpha_img) {
                image_free(&g_default_bgalpha_img);
                g_has_default_bgalpha_img = false;
            }
            for (int i = 0; i < g_cutout_count; ++i) {
                free_cutout(&g_cutouts[i]);
            }
            g_cutout_count = 0;
            if (g_default_cutout_hdc_light) { DeleteDC(g_default_cutout_hdc_light); g_default_cutout_hdc_light = NULL; }
            if (g_default_cutout_mask_light) { DeleteObject(g_default_cutout_mask_light); g_default_cutout_mask_light = NULL; }
            if (g_default_cutout_hdc_dark) { DeleteDC(g_default_cutout_hdc_dark); g_default_cutout_hdc_dark = NULL; }
            if (g_default_cutout_mask_dark) { DeleteObject(g_default_cutout_mask_dark); g_default_cutout_mask_dark = NULL; }
            if (g_has_default_cutout_img) {
                image_free(&g_default_cutout_img);
                g_has_default_cutout_img = false;
            }
            for (int i = 0; i < g_sprite_count; ++i) {
                free_sprite(&g_sprites[i]);
            }
            g_sprite_count = 0;
            if (g_has_default_sprite_img) {
                image_free(&g_default_sprite_img);
                g_has_default_sprite_img = false;
            }
            if (g_has_bg_image) {
                image_free(&g_canvas_img);
                g_has_bg_image = false;
            }
            if (g_has_frog_img) {
                image_free(&g_frog_img);
                g_has_frog_img = false;
            }
            if (g_has_coin_img) {
                image_free(&g_coin_img);
                g_has_coin_img = false;
            }
            if (s_hMenuCbtHook) {
                UnhookWindowsHookEx(s_hMenuCbtHook);
                s_hMenuCbtHook = NULL;
            }
            KillTimer(hwnd, IDT_MEDIA_ANIMATION);
            image_subsystem_shutdown();
            destroy_fonts();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    timeBeginPeriod(1);

    /* Resolve absolute path to assets\media\ZUpather.ico next to executable */
    char full_ico_path[MAX_PATH];
    GetModuleFileNameA(NULL, full_ico_path, MAX_PATH);
    char *p_slash = strrchr(full_ico_path, '\\');
    if (p_slash) {
        strcpy(p_slash + 1, "assets\\media\\ZUpather.ico");
    } else {
        strcpy(full_ico_path, "assets\\media\\ZUpather.ico");
    }

    /* Load directly from file first to guarantee latest modified icon without system caching */
    HICON h_ico_big = (HICON)LoadImageA(NULL, full_ico_path, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
    if (!h_ico_big) {
        h_ico_big = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    }
    HICON h_ico_small = (HICON)LoadImageA(NULL, full_ico_path, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
    if (!h_ico_small) {
        h_ico_small = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    }

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "ZuPatherMainWindow";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = h_ico_big;
    wc.hIconSm       = h_ico_small;
    wc.hbrBackground = NULL;
    RegisterClassExA(&wc);

    int initial_w = 980;
    int initial_h = 720;

    HWND hwnd = CreateWindowA(
        "ZuPatherMainWindow",
        "ZuPather - Zuma Path Editor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        initial_w, initial_h,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 1;

    if (h_ico_big) SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)h_ico_big);
    if (h_ico_small) SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)h_ico_small);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    /* Show Splash Screen on first launch */
    show_splash_screen_dialog(hwnd);
    undo_ensure_baseline();

    HACCEL hAccel = create_app_accelerators();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            char cls[32] = {0};
            HWND f = GetFocus();
            if (f) GetClassNameA(f, cls, sizeof(cls));
            bool is_edit = (_stricmp(cls, "Edit") == 0);
            if (!is_edit) {
                if ((msg.wParam == 'Z' || msg.wParam == 'z') && !(GetKeyState(VK_SHIFT) & 0x8000)) {
                    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_MENU_EDIT_UNDO, 0), 0);
                    continue;
                } else if ((msg.wParam == 'Y' || msg.wParam == 'y') || ((msg.wParam == 'Z' || msg.wParam == 'z') && (GetKeyState(VK_SHIFT) & 0x8000))) {
                    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_MENU_EDIT_REDO, 0), 0);
                    continue;
                }
            }
        }
        if (!hAccel || !TranslateAcceleratorA(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (hAccel) {
        DestroyAcceleratorTable(hAccel);
    }

    timeEndPeriod(1);
    return (int)msg.wParam;
}
