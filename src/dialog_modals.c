#include "dialog_modals.h"
#include "level_io.h"
#include "hud_overlay.h"
#include "undo_redo.h"
#include <shellapi.h>
#include "canvas_render.h"

/* ========================================================================= */
/* Custom Floating Warning Popup Dialog Box (Rendered right below caret)     */
/* ========================================================================= */
typedef struct {
    char header[128];
    char detail[512];
    char forbidden[512];
    char allow_lbl[32];
    char allowed[512];
    char rules[512];
    HWND hwnd_anchor_edit;
} FloatingWarnData;

static HWND s_hwnd_warn_popup = NULL;
static FloatingWarnData s_warn_popup_data;
static WNDPROC g_orig_canvas_name_proc = NULL;
static WNDPROC g_orig_canvas_title_proc = NULL;

static LRESULT CALLBACK WarnPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, 9922, 6500, NULL); /* 6.5s auto dismiss */
            return 0;

        case WM_TIMER:
            if (wParam == 9922) {
                KillTimer(hwnd, 9922);
                DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_LBUTTONDOWN:
            DestroyWindow(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HDC mem_dc = CreateCompatibleDC(hdc);
            HBITMAP mem_bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP old_bm = (HBITMAP)SelectObject(mem_dc, mem_bm);

            bool is_dark = (g_current_theme == THEME_DARK);

            COLORREF bg_col        = is_dark ? RGB(36, 18, 20)  : RGB(255, 243, 244);
            COLORREF border_col    = is_dark ? RGB(220, 60, 72) : RGB(215, 45, 58);
            COLORREF title_col     = is_dark ? RGB(255, 120, 130): RGB(175, 20, 30);
            COLORREF detail_col    = is_dark ? RGB(235, 215, 220): RGB(55, 20, 25);
            COLORREF sep_col       = is_dark ? RGB(75, 36, 42)  : RGB(238, 200, 206);
            COLORREF forb_lbl_col  = is_dark ? RGB(255, 95, 105) : RGB(185, 20, 30);
            COLORREF forb_txt_col  = is_dark ? RGB(255, 190, 195): RGB(110, 20, 28);
            COLORREF allow_lbl_col = is_dark ? RGB(70, 215, 135) : RGB(20, 135, 60);
            COLORREF allow_txt_col = is_dark ? RGB(185, 245, 210): RGB(25, 95, 45);
            COLORREF rule_lbl_col  = is_dark ? RGB(180, 185, 195): RGB(85, 90, 100);
            COLORREF rule_txt_col  = is_dark ? RGB(160, 165, 175): RGB(95, 100, 110);

            /* Fill Background */
            HBRUSH bg_br = CreateSolidBrush(bg_col);
            FillRect(mem_dc, &rc, bg_br);
            DeleteObject(bg_br);

            /* Draw Outer Border */
            HPEN border_pen = CreatePen(PS_SOLID, 2, border_col);
            HPEN old_pen = (HPEN)SelectObject(mem_dc, border_pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, 0, 0, rc.right, rc.bottom);
            SelectObject(mem_dc, old_pen);
            DeleteObject(border_pen);

            SetBkMode(mem_dc, TRANSPARENT);

            /* 1. Badge [ ! ] */
            RECT rc_badge = { 14, 12, 42, 34 };
            HBRUSH badge_br = CreateSolidBrush(border_col);
            HPEN badge_pen = CreatePen(PS_SOLID, 1, border_col);
            old_pen = (HPEN)SelectObject(mem_dc, badge_pen);
            HBRUSH old_b = (HBRUSH)SelectObject(mem_dc, badge_br);
            RoundRect(mem_dc, rc_badge.left, rc_badge.top, rc_badge.right, rc_badge.bottom, 6, 6);
            SelectObject(mem_dc, old_b);
            SelectObject(mem_dc, old_pen);
            DeleteObject(badge_br);
            DeleteObject(badge_pen);

            HFONT hf_bold = g_font_primary_bold ? g_font_primary_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT hf_reg = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT old_f = (HFONT)SelectObject(mem_dc, hf_bold);
            SetTextColor(mem_dc, RGB(255, 255, 255));
            DrawTextA(mem_dc, "!", 1, &rc_badge, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

            /* Close hint [x] in top-right */
            RECT rc_close = { rc.right - 28, 8, rc.right - 8, 28 };
            SetTextColor(mem_dc, rule_lbl_col);
            DrawTextA(mem_dc, "x", 1, &rc_close, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

            /* 2. Header Text */
            SetTextColor(mem_dc, title_col);
            RECT rc_title = { 50, 13, rc.right - 32, 33 };
            DrawTextA(mem_dc, s_warn_popup_data.header, -1, &rc_title, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

            /* 3. Detail Message */
            SelectObject(mem_dc, hf_reg);
            SetTextColor(mem_dc, detail_col);
            RECT rc_detail_calc = { 14, 38, rc.right - 14, 600 };
            DrawTextA(mem_dc, s_warn_popup_data.detail, -1, &rc_detail_calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
            int d_h = rc_detail_calc.bottom - rc_detail_calc.top;
            if (d_h < 18) d_h = 18;
            RECT rc_detail = { 14, 38, rc.right - 14, 38 + d_h };
            DrawTextA(mem_dc, s_warn_popup_data.detail, -1, &rc_detail, DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);

            /* Separator Line */
            int sep_y = 38 + d_h + 8;
            HPEN sep_pen = CreatePen(PS_SOLID, 1, sep_col);
            old_pen = (HPEN)SelectObject(mem_dc, sep_pen);
            MoveToEx(mem_dc, 14, sep_y, NULL);
            LineTo(mem_dc, rc.right - 14, sep_y);
            SelectObject(mem_dc, old_pen);
            DeleteObject(sep_pen);

            int row_y = sep_y + 8;
            int text_x = 96;

            /* 4. FORBIDDEN row */
            if (s_warn_popup_data.forbidden[0] != '\0') {
                SelectObject(mem_dc, hf_bold);
                SetTextColor(mem_dc, forb_lbl_col);
                RECT rc_f_lbl = { 14, row_y, text_x - 6, row_y + 18 };
                DrawTextA(mem_dc, "FORBIDDEN:", -1, &rc_f_lbl, DT_SINGLELINE | DT_LEFT | DT_TOP);

                SelectObject(mem_dc, hf_reg);
                SetTextColor(mem_dc, forb_txt_col);
                RECT rc_f_calc = { text_x, row_y, rc.right - 14, row_y + 300 };
                DrawTextA(mem_dc, s_warn_popup_data.forbidden, -1, &rc_f_calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
                int f_h = rc_f_calc.bottom - rc_f_calc.top;
                if (f_h < 18) f_h = 18;
                RECT rc_f_txt = { text_x, row_y, rc.right - 14, row_y + f_h };
                DrawTextA(mem_dc, s_warn_popup_data.forbidden, -1, &rc_f_txt, DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
                row_y += f_h + 6;
            }

            /* 5. ALLOWED / STATUS row */
            if (s_warn_popup_data.allowed[0] != '\0') {
                const char *lbl = s_warn_popup_data.allow_lbl[0] ? s_warn_popup_data.allow_lbl : "ALLOWED:";
                SelectObject(mem_dc, hf_bold);
                SetTextColor(mem_dc, allow_lbl_col);
                RECT rc_a_lbl = { 14, row_y, text_x - 6, row_y + 18 };
                DrawTextA(mem_dc, lbl, -1, &rc_a_lbl, DT_SINGLELINE | DT_LEFT | DT_TOP);

                SelectObject(mem_dc, hf_reg);
                SetTextColor(mem_dc, allow_txt_col);
                RECT rc_a_calc = { text_x, row_y, rc.right - 14, row_y + 300 };
                DrawTextA(mem_dc, s_warn_popup_data.allowed, -1, &rc_a_calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
                int a_h = rc_a_calc.bottom - rc_a_calc.top;
                if (a_h < 18) a_h = 18;
                RECT rc_a_txt = { text_x, row_y, rc.right - 14, row_y + a_h };
                DrawTextA(mem_dc, s_warn_popup_data.allowed, -1, &rc_a_txt, DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
                row_y += a_h + 6;
            }

            /* 6. RULES row */
            if (s_warn_popup_data.rules[0] != '\0') {
                SelectObject(mem_dc, hf_bold);
                SetTextColor(mem_dc, rule_lbl_col);
                RECT rc_r_lbl = { 14, row_y, text_x - 6, row_y + 18 };
                DrawTextA(mem_dc, "RULES:", -1, &rc_r_lbl, DT_SINGLELINE | DT_LEFT | DT_TOP);

                SelectObject(mem_dc, hf_reg);
                SetTextColor(mem_dc, rule_txt_col);
                RECT rc_r_calc = { text_x, row_y, rc.right - 14, row_y + 300 };
                DrawTextA(mem_dc, s_warn_popup_data.rules, -1, &rc_r_calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
                int r_h = rc_r_calc.bottom - rc_r_calc.top;
                if (r_h < 18) r_h = 18;
                RECT rc_r_txt = { text_x, row_y, rc.right - 14, row_y + r_h };
                DrawTextA(mem_dc, s_warn_popup_data.rules, -1, &rc_r_txt, DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
            }

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem_dc, 0, 0, SRCCOPY);

            SelectObject(mem_dc, old_f);
            SelectObject(mem_dc, old_bm);
            DeleteObject(mem_bm);
            DeleteDC(mem_dc);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            s_hwnd_warn_popup = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void hide_edit_warning_popup(void) {
    if (s_hwnd_warn_popup && IsWindow(s_hwnd_warn_popup)) {
        DestroyWindow(s_hwnd_warn_popup);
        s_hwnd_warn_popup = NULL;
    }
}

static void hide_edit_warning_tooltip(void) {
    hide_edit_warning_popup();
}

static void destroy_edit_warning_tooltip(void) {
    hide_edit_warning_popup();
}

static void show_floating_warning_popup_at_point(
    HWND hwnd_parent,
    int screen_x,
    int screen_y,
    const char *header,
    const char *detail,
    const char *forbidden,
    const char *allow_lbl,
    const char *allowed,
    const char *rules
) {
    HINSTANCE hinst = GetModuleHandle(NULL);
    static bool s_popup_class_registered = false;
    if (!s_popup_class_registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = WarnPopupWndProc;
        wc.hInstance     = hinst;
        wc.lpszClassName = "ZuPatherFloatingWarnPopup";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        s_popup_class_registered = true;
    }

    if (s_hwnd_warn_popup && IsWindow(s_hwnd_warn_popup)) {
        DestroyWindow(s_hwnd_warn_popup);
        s_hwnd_warn_popup = NULL;
    }

    memset(&s_warn_popup_data, 0, sizeof(s_warn_popup_data));
    strncpy(s_warn_popup_data.header, header ? header : "Warning", sizeof(s_warn_popup_data.header) - 1);
    strncpy(s_warn_popup_data.detail, detail ? detail : "", sizeof(s_warn_popup_data.detail) - 1);
    strncpy(s_warn_popup_data.forbidden, forbidden ? forbidden : "", sizeof(s_warn_popup_data.forbidden) - 1);
    strncpy(s_warn_popup_data.allow_lbl, (allow_lbl && allow_lbl[0]) ? allow_lbl : "ALLOWED:", sizeof(s_warn_popup_data.allow_lbl) - 1);
    strncpy(s_warn_popup_data.allowed, allowed ? allowed : "", sizeof(s_warn_popup_data.allowed) - 1);
    strncpy(s_warn_popup_data.rules, rules ? rules : "", sizeof(s_warn_popup_data.rules) - 1);
    s_warn_popup_data.hwnd_anchor_edit = NULL;

    int popup_w = 510;
    int pad_x = 14;
    int text_x = 96;
    int content_w = popup_w - text_x - pad_x;

    /* Measure dynamic heights using screen DC */
    HDC hdc_screen = GetDC(NULL);
    HFONT hf_reg = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old_f = (HFONT)SelectObject(hdc_screen, hf_reg);

    /* Detail height */
    RECT rc_d_calc = { 0, 0, popup_w - 28, 600 };
    DrawTextA(hdc_screen, s_warn_popup_data.detail, -1, &rc_d_calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    int d_h = rc_d_calc.bottom - rc_d_calc.top;
    if (d_h < 18) d_h = 18;

    /* FORBIDDEN height */
    int f_h = 0;
    if (s_warn_popup_data.forbidden[0] != '\0') {
        RECT rc_f_calc = { 0, 0, content_w, 600 };
        DrawTextA(hdc_screen, s_warn_popup_data.forbidden, -1, &rc_f_calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        f_h = rc_f_calc.bottom - rc_f_calc.top;
        if (f_h < 18) f_h = 18;
    }

    /* ALLOWED / STATUS height */
    int a_h = 0;
    if (s_warn_popup_data.allowed[0] != '\0') {
        RECT rc_a_calc = { 0, 0, content_w, 600 };
        DrawTextA(hdc_screen, s_warn_popup_data.allowed, -1, &rc_a_calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        a_h = rc_a_calc.bottom - rc_a_calc.top;
        if (a_h < 18) a_h = 18;
    }

    /* RULES height */
    int r_h = 0;
    if (s_warn_popup_data.rules[0] != '\0') {
        RECT rc_r_calc = { 0, 0, content_w, 600 };
        DrawTextA(hdc_screen, s_warn_popup_data.rules, -1, &rc_r_calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        r_h = rc_r_calc.bottom - rc_r_calc.top;
        if (r_h < 18) r_h = 18;
    }

    SelectObject(hdc_screen, old_f);
    ReleaseDC(NULL, hdc_screen);

    int sep_y = 38 + d_h + 8;
    int cur_y = sep_y + 8;
    if (f_h > 0) cur_y += f_h + 6;
    if (a_h > 0) cur_y += a_h + 6;
    if (r_h > 0) cur_y += r_h + 6;
    int popup_h = cur_y + 8;

    int popup_x = screen_x;
    int popup_y = screen_y;

    /* Clamp inside current monitor work area */
    POINT pt_pt = { screen_x, screen_y };
    HMONITOR hMon = MonitorFromPoint(pt_pt, MONITOR_DEFAULTTONEAREST);
    if (hMon) {
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoA(hMon, &mi)) {
            RECT rc_work = mi.rcWork;
            if (popup_x + popup_w > rc_work.right - 12) {
                popup_x = rc_work.right - popup_w - 12;
            }
            if (popup_x < rc_work.left + 12) {
                popup_x = rc_work.left + 12;
            }
            if (popup_y + popup_h > rc_work.bottom - 12) {
                popup_y = screen_y - popup_h - 6;
                if (popup_y < rc_work.top + 12) {
                    popup_y = rc_work.top + 12;
                }
            }
        }
    }

    s_hwnd_warn_popup = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "ZuPatherFloatingWarnPopup", "",
        WS_POPUP | WS_VISIBLE,
        popup_x, popup_y, popup_w, popup_h,
        hwnd_parent, NULL, hinst, NULL
    );

    if (s_hwnd_warn_popup) {
        ShowWindow(s_hwnd_warn_popup, SW_SHOWNOACTIVATE);
        UpdateWindow(s_hwnd_warn_popup);
    }
}

static void show_edit_warning_popup(
    HWND hwnd_edit,
    const char *header,
    const char *detail,
    const char *forbidden,
    const char *allowed,
    const char *rules
) {
    if (!hwnd_edit || !IsWindow(hwnd_edit)) return;

    RECT rc_edit;
    GetWindowRect(hwnd_edit, &rc_edit);

    POINT pt_caret = {0, 0};
    DWORD sel = (DWORD)SendMessage(hwnd_edit, EM_GETSEL, 0, 0);
    int char_idx = LOWORD(sel);
    LRESULT lpos = SendMessage(hwnd_edit, EM_POSFROMCHAR, (WPARAM)char_idx, 0);
    if (lpos != -1) {
        pt_caret.x = (short)LOWORD(lpos);
        pt_caret.y = (short)HIWORD(lpos);
        ClientToScreen(hwnd_edit, &pt_caret);
    } else {
        pt_caret.x = rc_edit.left + 10;
        pt_caret.y = rc_edit.top;
    }

    int popup_x = pt_caret.x - 20;
    int popup_y = rc_edit.bottom + 4;

    show_floating_warning_popup_at_point(
        GetParent(hwnd_edit),
        popup_x, popup_y,
        header, detail, forbidden, "ALLOWED:", allowed, rules
    );
    if (s_hwnd_warn_popup) {
        s_warn_popup_data.hwnd_anchor_edit = hwnd_edit;
    }
}

static void show_original_map_protected_popup_at_point(HWND parent_hwnd, int x, int y, const char *level_id, const char *level_title) {
    char header[128];
    snprintf(header, sizeof(header), "Original Zuma Deluxe Map Protected");

    char detail[512];
    if (level_title && level_title[0] != '\0') {
        snprintf(detail, sizeof(detail),
            "Map '%s' (%s) is an official original Zuma Deluxe map registered in StageProgression.",
            level_id ? level_id : "", level_title);
    } else {
        snprintf(detail, sizeof(detail),
            "Map '%s' is an official original Zuma Deluxe map registered in StageProgression.",
            level_id ? level_id : "");
    }

    const char *forb = "Renaming, deleting, or modifying official Zuma Deluxe original maps via ZuPather.";
    const char *stat = "Protected Core Game File - Verified in levels.xml <StageProgression>. Integrity enforced.";
    const char *rule = "Original vanilla Zuma Deluxe maps cannot be altered by ZuPather to prevent game crashes and broken stages. If you wish to customize vanilla maps, edit them directly in your Zuma Deluxe installation directory.";

    MessageBeep(MB_ICONWARNING);
    show_floating_warning_popup_at_point(
        parent_hwnd,
        x + 4, y + 4,
        header,
        detail,
        forb,
        "STATUS:",
        stat,
        rule
    );
}

static void show_original_map_protected_dialog(HWND parent_hwnd, const char *level_id, const char *level_title) {
    POINT pt;
    GetCursorPos(&pt);
    show_original_map_protected_popup_at_point(parent_hwnd, pt.x, pt.y, level_id, level_title);
}

static LRESULT CALLBACK CanvasNameEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            HWND parent = GetParent(hwnd);
            if (parent) {
                HWND btn = GetDlgItem(parent, ID_SAVE_BTN_SAVE);
                if (btn) SendMessage(parent, WM_COMMAND, MAKEWPARAM(ID_SAVE_BTN_SAVE, BN_CLICKED), (LPARAM)btn);
                else if ((btn = GetDlgItem(parent, ID_DLG_BTN_CREATE)) != NULL) SendMessage(parent, WM_COMMAND, MAKEWPARAM(ID_DLG_BTN_CREATE, BN_CLICKED), (LPARAM)btn);
                else if ((btn = GetDlgItem(parent, ID_RENAME_BTN_OK)) != NULL) SendMessage(parent, WM_COMMAND, MAKEWPARAM(ID_RENAME_BTN_OK, BN_CLICKED), (LPARAM)btn);
                else if ((btn = GetDlgItem(parent, ID_RENAME_ZUMA_BTN_OK)) != NULL) SendMessage(parent, WM_COMMAND, MAKEWPARAM(ID_RENAME_ZUMA_BTN_OK, BN_CLICKED), (LPARAM)btn);
                else if ((btn = GetDlgItem(parent, ID_RENAME_EXP_BTN_OK)) != NULL) SendMessage(parent, WM_COMMAND, MAKEWPARAM(ID_RENAME_EXP_BTN_OK, BN_CLICKED), (LPARAM)btn);
            }
            return 0;
        } else if (wParam == VK_ESCAPE) {
            hide_edit_warning_popup();
            HWND parent = GetParent(hwnd);
            if (parent) PostMessage(parent, WM_CLOSE, 0, 0);
            return 0;
        }
    }

    if (msg == WM_KILLFOCUS) {
        hide_edit_warning_popup();
        return CallWindowProcA(g_orig_canvas_name_proc ? g_orig_canvas_name_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
    }

    if (msg == WM_CHAR) {
        char ch = (char)wParam;
        if (wParam < 32) {
            return CallWindowProcA(g_orig_canvas_name_proc ? g_orig_canvas_name_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
        }

        /* 1. Force uppercase characters to lowercase automatically */
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch + ('a' - 'A'));
            wParam = (WPARAM)ch;
        }

        DWORD sel = (DWORD)SendMessage(hwnd, EM_GETSEL, 0, 0);
        int start_sel = LOWORD(sel);

        /* 2. Check first character rule: Must start with a lowercase letter (a-z) */
        if (start_sel == 0 && !(ch >= 'a' && ch <= 'z')) {
            MessageBeep(MB_ICONWARNING);
            if (ch >= '0' && ch <= '9') {
                show_edit_warning_popup(
                    hwnd,
                    "Forbidden Start Character: Cannot Start with a Number",
                    "Zuma Deluxe requires Level IDs to start with a letter (a-z).\nStarting with a digit causes Zuma's difficulty parser (atol) to confuse the level with a numerical difficulty index and crash.",
                    "Digits (0-9) as the first character",
                    "Lowercase letters (a-z) only as the first character",
                    "Must start with a-z (cannot start with a number or symbol), max 32 chars."
                );
            } else if (ch == '_' || ch == '-') {
                show_edit_warning_popup(
                    hwnd,
                    "Forbidden Start Character: Cannot Start with Symbol",
                    "Zuma Deluxe requires Level IDs to start with a letter (a-z).\nStarting with an underscore or hyphen causes XML attribute parsing and asset loading errors in Zuma Deluxe.",
                    "Symbols (_ or -) as the first character",
                    "Lowercase letters (a-z) only as the first character",
                    "Must start with a-z (cannot start with a number or symbol), max 32 chars."
                );
            } else {
                goto check_forbidden_chars;
            }
            return 0;
        }

        /* 3. Check Zuma Deluxe valid ID characters: [a-z0-9_-] */
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
            char text[128] = {0};
            GetWindowTextA(hwnd, text, sizeof(text));
            int sel_len = HIWORD(sel) - LOWORD(sel);
            if ((int)strlen(text) - sel_len >= 32) {
                MessageBeep(MB_ICONWARNING);
                show_edit_warning_popup(
                    hwnd,
                    "Maximum Level ID Length (32 chars)",
                    "Zuma Deluxe allocates fixed 1024-byte stack buffers for stage parsing. IDs are limited to 32 characters to prevent buffer overflow.",
                    "Character inputs beyond 32 characters are blocked",
                    "Lowercase letters (a-z), digits (0-9), _ (underscore), - (hyphen)",
                    "Must start with a-z (not a number), max 32 chars."
                );
                return 0;
            }
            hide_edit_warning_popup();
            return CallWindowProcA(g_orig_canvas_name_proc ? g_orig_canvas_name_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
        }

check_forbidden_chars:
        /* 4. Forbidden character typed: Block + Show Custom Floating Dialog Box */
        MessageBeep(MB_ICONWARNING);
        char hdr[128];
        const char *desc = "";
        if (ch == ' ') {
            snprintf(hdr, sizeof(hdr), "Forbidden Character: ' ' (Space)");
            desc = "Spaces are not allowed in Level IDs! Zuma Deluxe splits levels by comma without trimming, causing a fatal 'Level not found' crash.";
        } else if (ch == ',') {
            snprintf(hdr, sizeof(hdr), "Forbidden Character: ',' (Comma)");
            desc = "Commas are not allowed! Zuma Deluxe uses commas as stage progression delimiters in levels.xml.";
        } else if (ch == '"' || ch == '\'') {
            snprintf(hdr, sizeof(hdr), "Forbidden Character: Quotes (\" or ')");
            desc = "Quotes are not allowed! They corrupt XML attribute and file parsing in Zuma Deluxe.";
        } else if (ch == '<' || ch == '>' || ch == '&') {
            snprintf(hdr, sizeof(hdr), "Forbidden Character: XML Symbol ('%c')", ch);
            desc = "Angle brackets and ampersands are not allowed! They break XML tags and entities in levels.xml.";
        } else if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' || ch == '|' || ch == '.') {
            snprintf(hdr, sizeof(hdr), "Forbidden Character: Filesystem Symbol ('%c')", ch);
            desc = "Filesystem symbols (/ \\ : * ? | .) are not allowed in folder and file names on Windows.";
        } else {
            snprintf(hdr, sizeof(hdr), "Forbidden Character: '%c'", ch);
            desc = "Special punctuation and symbols are not supported in Zuma Deluxe level IDs.";
        }

        show_edit_warning_popup(
            hwnd,
            hdr,
            desc,
            "Space, Comma (,), Quotes (\", '), / \\ : * ? < > | & . ! @ # $ % ^ + = [ ] { } ; ` ~",
            "Lowercase letters (a-z), digits (0-9), underscore (_), and hyphen (-)",
            "Must start with a-z (cannot start with a number), max 32 chars."
        );
        return 0;
    }

    if (msg == WM_PASTE) {
        if (OpenClipboard(hwnd)) {
            HANDLE hData = GetClipboardData(CF_TEXT);
            if (hData) {
                char *clip_text = (char*)GlobalLock(hData);
                if (clip_text) {
                    char clean[64] = {0};
                    int dst = 0;
                    bool modified = false;
                    DWORD sel = (DWORD)SendMessage(hwnd, EM_GETSEL, 0, 0);
                    int start_sel = LOWORD(sel);

                    for (int s = 0; clip_text[s] && dst < 32; ++s) {
                        char c = clip_text[s];
                        if (c >= 'A' && c <= 'Z') {
                            c = (char)(c + ('a' - 'A'));
                            modified = true;
                        }
                        if (start_sel == 0 && dst == 0) {
                            if (c >= 'a' && c <= 'z') {
                                clean[dst++] = c;
                            } else {
                                modified = true;
                            }
                        } else {
                            if (c == ' ') {
                                clean[dst++] = '_';
                                modified = true;
                            } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
                                clean[dst++] = c;
                            } else {
                                modified = true;
                            }
                        }
                    }
                    GlobalUnlock(hData);
                    CloseClipboard();

                    if (clean[0] != '\0') {
                        SendMessageA(hwnd, EM_REPLACESEL, TRUE, (LPARAM)clean);
                    }
                    if (modified) {
                        MessageBeep(MB_ICONINFORMATION);
                        show_edit_warning_popup(
                            hwnd,
                            "Pasted Level ID Sanitized",
                            "Pasted text was automatically converted to lowercase and cleaned for Zuma Deluxe compatibility.",
                            "Spaces, commas, quotes, and invalid symbols were cleaned",
                            "Must start with a-z; allowed: [a-z0-9_-]",
                            "Max 32 characters limit."
                        );
                    }
                    return 0;
                }
            }
            CloseClipboard();
        }
    }

    return CallWindowProcA(g_orig_canvas_name_proc ? g_orig_canvas_name_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK CanvasTitleEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            HWND parent = GetParent(hwnd);
            if (parent) {
                HWND btn = GetDlgItem(parent, ID_SAVE_BTN_SAVE);
                if (btn) SendMessage(parent, WM_COMMAND, MAKEWPARAM(ID_SAVE_BTN_SAVE, BN_CLICKED), (LPARAM)btn);
                else {
                    btn = GetDlgItem(parent, ID_DLG_BTN_CREATE);
                    if (btn) SendMessage(parent, WM_COMMAND, MAKEWPARAM(ID_DLG_BTN_CREATE, BN_CLICKED), (LPARAM)btn);
                    else {
                        btn = GetDlgItem(parent, ID_RENAME_BTN_OK);
                        if (btn) SendMessage(parent, WM_COMMAND, MAKEWPARAM(ID_RENAME_BTN_OK, BN_CLICKED), (LPARAM)btn);
                    }
                }
            }
            return 0;
        } else if (wParam == VK_ESCAPE) {
            hide_edit_warning_popup();
            HWND parent = GetParent(hwnd);
            if (parent) PostMessage(parent, WM_CLOSE, 0, 0);
            return 0;
        }
    }

    if (msg == WM_KILLFOCUS) {
        hide_edit_warning_popup();
        return CallWindowProcA(g_orig_canvas_title_proc ? g_orig_canvas_title_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
    }

    if (msg == WM_CHAR) {
        char ch = (char)wParam;
        if (wParam < 32) {
            return CallWindowProcA(g_orig_canvas_title_proc ? g_orig_canvas_title_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
        }

        /* Forbidden in Title / dispname:
           1. " (double quote, breaks dispname="..." XML attribute)
           2. < and > (breaks XML tags)
           3. & (breaks XML entity parsing)
        */
        if (ch == '"' || ch == '<' || ch == '>' || ch == '&') {
            MessageBeep(MB_ICONWARNING);
            char hdr[128];
            const char *desc = "";
            if (ch == '"') {
                snprintf(hdr, sizeof(hdr), "Forbidden in Title: '\"' (Double Quote)");
                desc = "Double quotes are forbidden because they prematurely close the dispname=\"...\" XML attribute.";
            } else if (ch == '&') {
                snprintf(hdr, sizeof(hdr), "Forbidden in Title: '&' (Ampersand)");
                desc = "Ampersands are forbidden because they corrupt Zuma Deluxe's XML entity parser.";
            } else {
                snprintf(hdr, sizeof(hdr), "Forbidden in Title: '%c' (Angle Bracket)", ch);
                desc = "Angle brackets ('<' and '>') are forbidden because they break XML tags in levels.xml.";
            }

            show_edit_warning_popup(
                hwnd,
                hdr,
                desc,
                "\" (double quote), < (less than), > (greater than), & (ampersand)",
                "Uppercase & lowercase letters, numbers, spaces, and punctuation (' - . ! , : ( ) /)",
                "Max 64 characters (fits Zuma Deluxe in-game intro banner)."
            );
            return 0;
        }

        /* Length limit: max 64 characters */
        char text[128] = {0};
        GetWindowTextA(hwnd, text, sizeof(text));
        DWORD sel = (DWORD)SendMessage(hwnd, EM_GETSEL, 0, 0);
        int sel_len = HIWORD(sel) - LOWORD(sel);
        if ((int)strlen(text) - sel_len >= 64) {
            MessageBeep(MB_ICONWARNING);
            show_edit_warning_popup(
                hwnd,
                "Maximum Title Length (64 chars)",
                "The in-game level intro banner in Zuma Deluxe fits up to 64 characters.",
                "Character inputs beyond 64 characters are blocked",
                "Uppercase & lowercase letters, numbers, spaces, and punctuation (' - . ! , : ( ) /)",
                "Max 64 characters limit."
            );
            return 0;
        }

        /* All other characters (spaces, uppercase, lowercase, punctuation) are allowed! */
        hide_edit_warning_popup();
        return CallWindowProcA(g_orig_canvas_title_proc ? g_orig_canvas_title_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
    }

    if (msg == WM_PASTE) {
        if (OpenClipboard(hwnd)) {
            HANDLE hData = GetClipboardData(CF_TEXT);
            if (hData) {
                char *clip_text = (char*)GlobalLock(hData);
                if (clip_text) {
                    char clean[128] = {0};
                    int dst = 0;
                    bool modified = false;
                    for (int s = 0; clip_text[s] && dst < 64; ++s) {
                        char c = clip_text[s];
                        if (c == '"' || c == '<' || c == '>' || c == '&' || c == '\r' || c == '\n') {
                            modified = true;
                        } else {
                            clean[dst++] = c;
                        }
                    }
                    GlobalUnlock(hData);
                    CloseClipboard();

                    if (clean[0] != '\0') {
                        SendMessageA(hwnd, EM_REPLACESEL, TRUE, (LPARAM)clean);
                    }
                    if (modified) {
                        MessageBeep(MB_ICONINFORMATION);
                        show_edit_warning_popup(
                            hwnd,
                            "Pasted Title Sanitized",
                            "Pasted text was automatically cleaned of XML breaking symbols.",
                            "Double quotes (\"), angle brackets (< >), and ampersands (&) were removed",
                            "Uppercase & lowercase letters, numbers, spaces, and punctuation",
                            "Max 64 characters limit."
                        );
                    }
                    return 0;
                }
            }
            CloseClipboard();
        }
    }

    return CallWindowProcA(g_orig_canvas_title_proc ? g_orig_canvas_title_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
}

/* ========================================================================= */
/* New Canvas Modal Dialog (Centered, Themed, Image Auto-detection)          */
/* ========================================================================= */
typedef struct {
    HWND        hwnd_dlg;
    HWND        hwnd_name;
    HWND        hwnd_title;
    HWND        hwnd_w;
    HWND        hwnd_h;
    HWND        hwnd_imgpath;
    HWND        hwnd_btn_browse;
    HWND        hwnd_btn_clear;
    HWND        hwnd_btn_create;
    HWND        hwnd_btn_cancel;
    CanvasImage temp_img;
    bool        has_temp_img;
    char        selected_path[MAX_PATH];
} NewCanvasDlgData;

static NewCanvasDlgData *g_dlg_data = NULL;
static WNDPROC g_orig_imgpath_edit_proc = NULL;

static LRESULT CALLBACK ImgPathEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SETFOCUS: {
            LRESULT res = CallWindowProcA(g_orig_imgpath_edit_proc, hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, NULL, TRUE);
            return res;
        }

        case WM_KILLFOCUS: {
            LRESULT res = CallWindowProcA(g_orig_imgpath_edit_proc, hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, NULL, TRUE);
            return res;
        }

        case WM_PAINT: {
            LRESULT res = CallWindowProcA(g_orig_imgpath_edit_proc, hwnd, msg, wParam, lParam);
            int len = GetWindowTextLengthA(hwnd);
            if (len == 0) {
                bool is_active = (GetFocus() == hwnd);
                if (!is_active) {
                    HDC hdc = wParam ? (HDC)wParam : GetDC(hwnd);
                    if (hdc) {
                        RECT rc;
                        GetClientRect(hwnd, &rc);
                        rc.left += 6;
                        SetBkMode(hdc, TRANSPARENT);
                        COLORREF placeholder_col = (g_current_theme == THEME_DARK)
                            ? RGB(130, 138, 150)
                            : RGB(150, 155, 165);
                        SetTextColor(hdc, placeholder_col);
                        SelectObject(hdc, g_font_primary_regular);
                        DrawTextA(hdc, "No image selected", -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
                        if (!wParam) ReleaseDC(hwnd, hdc);
                    }
                }
            }
            return res;
        }
    }
    return CallWindowProcA(g_orig_imgpath_edit_proc, hwnd, msg, wParam, lParam);
}

static void sanitize_canvas_names_from_image(
    const char *raw_display_name,
    char *out_safe_name, size_t max_name,
    char *out_safe_title, size_t max_title,
    bool *out_violation_found,
    char *out_folder_issues, size_t max_folder_iss,
    char *out_title_issues, size_t max_title_iss
) {
    if (!raw_display_name || raw_display_name[0] == '\0') {
        raw_display_name = "level1";
    }

    /* 1. Folder / Level ID Sanitization */
    bool name_has_forbidden = false;
    bool name_starts_with_num = false;
    bool name_too_long = false;

    char raw_trimmed[128] = {0};
    strncpy(raw_trimmed, raw_display_name, sizeof(raw_trimmed) - 1);

    if (raw_trimmed[0] >= '0' && raw_trimmed[0] <= '9') {
        name_starts_with_num = true;
    }

    char temp_name[128] = {0};
    int dst_n = 0;
    if (name_starts_with_num) {
        strcpy(temp_name, "lvl_");
        dst_n = 4;
    }

    for (const char *p = raw_trimmed; *p && dst_n < 120; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') {
            name_has_forbidden = true;
            temp_name[dst_n++] = (char)(c + ('a' - 'A'));
        } else if (c == ' ') {
            name_has_forbidden = true;
            temp_name[dst_n++] = '_';
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            temp_name[dst_n++] = c;
        } else {
            name_has_forbidden = true;
            temp_name[dst_n++] = '_';
        }
    }
    temp_name[dst_n] = '\0';

    /* Collapse consecutive underscores */
    char collapsed_name[128] = {0};
    int cd = 0;
    bool prev_us = false;
    for (int i = 0; temp_name[i]; ++i) {
        if (temp_name[i] == '_') {
            if (!prev_us) {
                collapsed_name[cd++] = '_';
                prev_us = true;
            }
        } else {
            collapsed_name[cd++] = temp_name[i];
            prev_us = false;
        }
    }
    collapsed_name[cd] = '\0';

    /* Trim leading and trailing underscores */
    char *sn = collapsed_name;
    while (*sn == '_' && !name_starts_with_num) sn++;
    char *en = sn + strlen(sn);
    while (en > sn && *(en - 1) == '_') {
        en--;
        *en = '\0';
    }

    if (*sn == '\0') {
        strncpy(out_safe_name, "level1", max_name - 1);
        out_safe_name[max_name - 1] = '\0';
    } else {
        if (strlen(sn) > 32) {
            name_too_long = true;
            sn[32] = '\0';
        }
        strncpy(out_safe_name, sn, max_name - 1);
        out_safe_name[max_name - 1] = '\0';
    }

    /* 2. Level Title Sanitization */
    bool title_has_forbidden = false;
    bool title_too_long = false;

    char temp_title[128] = {0};
    int dst_t = 0;
    for (const char *p = raw_display_name; *p && dst_t < 120; ++p) {
        char c = *p;
        if (c == '"') {
            title_has_forbidden = true;
            temp_title[dst_t++] = '\'';
        } else if (c == '<' || c == '>') {
            title_has_forbidden = true;
            temp_title[dst_t++] = ' ';
        } else if (c == '&') {
            title_has_forbidden = true;
            temp_title[dst_t++] = ' ';
        } else if (c == '\r' || c == '\n' || c == '\t') {
            title_has_forbidden = true;
            temp_title[dst_t++] = ' ';
        } else {
            temp_title[dst_t++] = c;
        }
    }
    temp_title[dst_t] = '\0';

    /* Collapse consecutive spaces */
    char collapsed_title[128] = {0};
    int ctd = 0;
    bool prev_sp = false;
    for (int i = 0; temp_title[i]; ++i) {
        if (temp_title[i] == ' ') {
            if (!prev_sp) {
                collapsed_title[ctd++] = ' ';
                prev_sp = true;
            }
        } else {
            collapsed_title[ctd++] = temp_title[i];
            prev_sp = false;
        }
    }
    collapsed_title[ctd] = '\0';

    char *st = collapsed_title;
    while (*st == ' ') st++;
    char *et = st + strlen(st);
    while (et > st && *(et - 1) == ' ') {
        et--;
        *et = '\0';
    }

    if (*st == '\0') {
        strncpy(out_safe_title, out_safe_name, max_title - 1);
        out_safe_title[max_title - 1] = '\0';
    } else {
        if (strlen(st) > 64) {
            title_too_long = true;
            st[64] = '\0';
        }
        strncpy(out_safe_title, st, max_title - 1);
        out_safe_title[max_title - 1] = '\0';
    }

    /* 3. Build Issue Notes */
    if (out_folder_issues) {
        out_folder_issues[0] = '\0';
        if (name_starts_with_num) {
            strncat(out_folder_issues, "  - Started with a number (automatically prefixed with 'lvl_').\n", max_folder_iss - strlen(out_folder_issues) - 1);
        }
        if (name_has_forbidden) {
            strncat(out_folder_issues, "  - Contained uppercase, spaces, or special symbols (converted to lowercase / underscores).\n", max_folder_iss - strlen(out_folder_issues) - 1);
        }
        if (name_too_long) {
            strncat(out_folder_issues, "  - Exceeded 32-character limit (truncated to 32 characters).\n", max_folder_iss - strlen(out_folder_issues) - 1);
        }
        if (out_folder_issues[0] == '\0') {
            strncat(out_folder_issues, "  - Valid.\n", max_folder_iss - strlen(out_folder_issues) - 1);
        }
    }

    if (out_title_issues) {
        out_title_issues[0] = '\0';
        if (title_has_forbidden) {
            strncat(out_title_issues, "  - Contained forbidden XML characters (\", <, >, &) (filtered to safe characters).\n", max_title_iss - strlen(out_title_issues) - 1);
        }
        if (title_too_long) {
            strncat(out_title_issues, "  - Exceeded 64-character limit (truncated to 64 characters).\n", max_title_iss - strlen(out_title_issues) - 1);
        }
        if (out_title_issues[0] == '\0') {
            strncat(out_title_issues, "  - Valid.\n", max_title_iss - strlen(out_title_issues) - 1);
        }
    }

    bool has_viol = name_starts_with_num || name_has_forbidden || name_too_long || title_has_forbidden || title_too_long;
    if (out_violation_found) *out_violation_found = has_viol;
}

static LRESULT CALLBACK NewCanvasModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();

    switch (msg) {
        case WM_CREATE:
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH bg_brush = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc, bg_brush);
            DeleteObject(bg_brush);

            /* Outer subtle border */
            HPEN border_pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(hdc, border_pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom);

            /* Title Header */
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, th->text_primary);
            SelectObject(hdc, g_font_primary_bold);
            RECT rc_title = {24, 18, rc.right - 24, 42};
            DrawTextA(hdc, "New Canvas Setup", -1, &rc_title, DT_SINGLELINE | DT_LEFT | DT_TOP);

            SetTextColor(hdc, th->text_secondary);
            SelectObject(hdc, g_font_primary_regular);
            RECT rc_sub = {24, 44, rc.right - 24, 64};
            DrawTextA(hdc, "Configure canvas size or auto-detect from reference image", -1, &rc_sub, DT_SINGLELINE | DT_LEFT | DT_TOP);

            /* Field Labels */
            SelectObject(hdc, g_font_primary_bold);
            SetTextColor(hdc, th->text_primary);

            RECT rc_lbl1 = {24, 76, 218, 94};
            DrawTextA(hdc, "Canvas Name (Folder):", -1, &rc_lbl1, DT_SINGLELINE | DT_LEFT | DT_TOP);

            RECT rc_lbl_title = {230, 76, rc.right - 24, 94};
            DrawTextA(hdc, "Canvas Title (dispname):", -1, &rc_lbl_title, DT_SINGLELINE | DT_LEFT | DT_TOP);

            RECT rc_lbl2 = {24, 140, rc.right - 24, 158};
            DrawTextA(hdc, "Dimensions (Width x Height in px):", -1, &rc_lbl2, DT_SINGLELINE | DT_LEFT | DT_TOP);

            RECT rc_lbl_x = {136, 164, 154, 188};
            SelectObject(hdc, g_font_mono_bold);
            DrawTextA(hdc, "x", -1, &rc_lbl_x, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

            SelectObject(hdc, g_font_primary_bold);
            RECT rc_lbl3 = {24, 204, rc.right - 24, 222};
            DrawTextA(hdc, "Reference Image (PNG, JPG, BMP, WebP) - Optional:", -1, &rc_lbl3, DT_SINGLELINE | DT_LEFT | DT_TOP);

            SelectObject(hdc, old_pen);
            DeleteObject(border_pen);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdc_ctl = (HDC)wParam;
            HWND hwnd_ctl = (HWND)lParam;
            if (g_dlg_data && (hwnd_ctl == g_dlg_data->hwnd_name || hwnd_ctl == g_dlg_data->hwnd_title ||
                               hwnd_ctl == g_dlg_data->hwnd_w || hwnd_ctl == g_dlg_data->hwnd_h ||
                               hwnd_ctl == g_dlg_data->hwnd_imgpath)) {
                COLORREF edit_bg = (g_current_theme == THEME_DARK) ? RGB(36, 40, 48) : RGB(255, 255, 255);
                SetTextColor(hdc_ctl, th->text_primary);
                SetBkColor(hdc_ctl, edit_bg);
                static HBRUSH s_edit_br_light = NULL;
                static HBRUSH s_edit_br_dark = NULL;
                if (!s_edit_br_light) s_edit_br_light = CreateSolidBrush(RGB(255, 255, 255));
                if (!s_edit_br_dark)  s_edit_br_dark  = CreateSolidBrush(RGB(36, 40, 48));
                return (LRESULT)((g_current_theme == THEME_DARK) ? s_edit_br_dark : s_edit_br_light);
            }
            break;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool is_create = (dis->CtlID == ID_DLG_BTN_CREATE);

                COLORREF bg_color = is_create ? (is_pressed ? RGB(18, 70, 140) : th->text_accent_blue)
                                              : (is_pressed ? th->btn_bg_pressed : th->btn_bg);
                HBRUSH bg_brush = CreateSolidBrush(bg_color);
                FillRect(dis->hDC, &dis->rcItem, bg_brush);
                DeleteObject(bg_brush);

                COLORREF border_col = is_create ? th->text_accent_blue : th->btn_border;
                HPEN pen = CreatePen(PS_SOLID, 1, border_col);
                HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, old_pen);
                DeleteObject(pen);

                char text[64] = {0};
                GetWindowTextA(dis->hwndItem, text, sizeof(text));

                SetBkMode(dis->hDC, TRANSPARENT);
                COLORREF text_col = is_create ? RGB(255, 255, 255) : th->btn_text;
                SetTextColor(dis->hDC, text_col);
                SelectObject(dis->hDC, is_create ? g_font_primary_bold : g_font_primary_regular);

                RECT rc_t = dis->rcItem;
                if (is_pressed) { rc_t.left += 1; rc_t.top += 1; }
                DrawTextA(dis->hDC, text, -1, &rc_t, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (!g_dlg_data) break;

            if (cmd == ID_DLG_BTN_BROWSE) {
                OPENFILENAMEA ofn;
                char filepath[MAX_PATH] = {0};
                memset(&ofn, 0, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner   = hwnd;
                ofn.lpstrFilter = ZUPATHER_MEDIA_FILTER;
                ofn.lpstrFile   = filepath;
                ofn.nMaxFile    = sizeof(filepath);
                ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

                if (GetOpenFileNameA(&ofn)) {
                    CanvasImage temp;
                    if (image_load_from_file(filepath, &temp)) {
                        if (g_dlg_data->has_temp_img) {
                            image_free(&g_dlg_data->temp_img);
                        }
                        g_dlg_data->temp_img = temp;
                        g_dlg_data->has_temp_img = true;
                        strncpy(g_dlg_data->selected_path, filepath, sizeof(g_dlg_data->selected_path) - 1);

                        /* Auto-fill Name & Title with Zuma Deluxe sanitization and single warning */
                        char safe_name[64] = {0};
                        char safe_title[128] = {0};
                        bool has_violations = false;
                        char folder_issues[512] = {0};
                        char title_issues[512] = {0};

                        sanitize_canvas_names_from_image(
                            temp.display_name,
                            safe_name, sizeof(safe_name),
                            safe_title, sizeof(safe_title),
                            &has_violations,
                            folder_issues, sizeof(folder_issues),
                            title_issues, sizeof(title_issues)
                        );

                        SetWindowTextA(g_dlg_data->hwnd_name, safe_name);
                        SetWindowTextA(g_dlg_data->hwnd_title, safe_title);

                        char buf_dim[32];
                        snprintf(buf_dim, sizeof(buf_dim), "%d", temp.width);
                        SetWindowTextA(g_dlg_data->hwnd_w, buf_dim);
                        snprintf(buf_dim, sizeof(buf_dim), "%d", temp.height);
                        SetWindowTextA(g_dlg_data->hwnd_h, buf_dim);

                        SetWindowTextA(g_dlg_data->hwnd_imgpath, temp.filename);
                        InvalidateRect(g_dlg_data->hwnd_imgpath, NULL, TRUE);

                        if (has_violations) {
                            char body[1536];
                            snprintf(body, sizeof(body),
                                "The selected image filename contains formatting or characters that do not conform to Zuma Deluxe rules:\n\n"
                                "1. Canvas Folder / Level ID:\n%s\n"
                                "2. Level Title:\n%s\n"
                                "To ensure your project is valid and typing is not blocked, both names have been automatically modified and sanitized by the system:\n"
                                "- Folder Name: %s\n"
                                "- Level Title: %s",
                                folder_issues, title_issues, safe_name, safe_title
                            );
                            show_themed_notice_dialog(hwnd, "Canvas Name Sanitization Notice", "Image Filename Sanitized for Zuma Compatibility", body, 600, 440);
                        }

                        if (temp.is_video) {
                            show_video_load_warning(hwnd);
                        } else {
                            char ext[16] = {0};
                            image_get_extension(temp.filepath, ext, sizeof(ext));
                            if (_stricmp(ext, "webp") == 0) {
                                show_webp_patch_warning(hwnd);
                            }
                        }
                    } else {
                        show_themed_notice_dialog(hwnd, "Media Load Error", "Failed to Load Media", "Failed to load selected media format.", 460, 240);
                    }
                }
            } else if (cmd == ID_DLG_BTN_CLEAR_IMG) {
                if (g_dlg_data->has_temp_img) {
                    image_free(&g_dlg_data->temp_img);
                    g_dlg_data->has_temp_img = false;
                }
                g_dlg_data->selected_path[0] = '\0';
                SetWindowTextA(g_dlg_data->hwnd_imgpath, "");
                InvalidateRect(g_dlg_data->hwnd_imgpath, NULL, TRUE);
            } else if (cmd == ID_DLG_BTN_CREATE) {
                char raw_name[128] = {0};
                char raw_title[128] = {0};
                char w_buf[32] = {0};
                char h_buf[32] = {0};

                GetWindowTextA(g_dlg_data->hwnd_name, raw_name, sizeof(raw_name));
                GetWindowTextA(g_dlg_data->hwnd_title, raw_title, sizeof(raw_title));
                GetWindowTextA(g_dlg_data->hwnd_w, w_buf, sizeof(w_buf));
                GetWindowTextA(g_dlg_data->hwnd_h, h_buf, sizeof(h_buf));

                int nw = atoi(w_buf);
                int nh = atoi(h_buf);
                if (nw <= 0) nw = DEFAULT_CANVAS_WIDTH;
                if (nh <= 0) nh = DEFAULT_CANVAS_HEIGHT;

                /* Trim whitespace for name */
                char *s_name = raw_name;
                while (*s_name == ' ' || *s_name == '\t' || *s_name == '\r' || *s_name == '\n') s_name++;
                char *e_name = s_name + strlen(s_name);
                while (e_name > s_name && (*(e_name - 1) == ' ' || *(e_name - 1) == '\t' || *(e_name - 1) == '\r' || *(e_name - 1) == '\n')) e_name--;
                *e_name = '\0';

                if (*s_name == '\0') {
                    show_themed_notice_dialog(hwnd, "Validation Error", "Invalid Canvas Name",
                        "Canvas Name (Folder / Level ID) cannot be empty.\nPlease enter a valid name using lowercase letters (a-z), numbers, '_' or '-'.", 480, 250);
                    SetFocus(g_dlg_data->hwnd_name);
                    break;
                }

                if (*s_name >= '0' && *s_name <= '9') {
                    show_themed_notice_dialog(hwnd, "Zuma Deluxe Warning", "Level Name Cannot Start with a Number",
                        "Zuma Deluxe level IDs must start with a letter (a-z), not a number.\n\n"
                        "Starting with a number causes Zuma's difficulty parser (atol) to confuse the level with a numerical difficulty index and crash.\n\n"
                        "Please prefix your level name with letters (e.g. 'lvl_' or 'stage_').", 540, 310);
                    SetFocus(g_dlg_data->hwnd_name);
                    break;
                }

                char name_buf[128] = {0};
                strncpy(name_buf, s_name, sizeof(name_buf) - 1);

                /* Trim whitespace for title */
                char *s_title = raw_title;
                while (*s_title == ' ' || *s_title == '\t' || *s_title == '\r' || *s_title == '\n') s_title++;
                char *e_title = s_title + strlen(s_title);
                while (e_title > s_title && (*(e_title - 1) == ' ' || *(e_title - 1) == '\t' || *(e_title - 1) == '\r' || *(e_title - 1) == '\n')) e_title--;
                *e_title = '\0';

                char title_buf[128] = {0};
                if (*s_title != '\0') {
                    strncpy(title_buf, s_title, sizeof(title_buf) - 1);
                } else {
                    strncpy(title_buf, name_buf, sizeof(title_buf) - 1);
                }

                /* Apply new canvas */
                g_has_canvas = true;
                strncpy(g_canvas_name, name_buf, sizeof(g_canvas_name) - 1);
                sync_canvas_title_everywhere(title_buf, NULL);
                sync_all_map_attr_edits_from_state();
                g_has_frog = true;
                g_frog_gx = nw / 2;
                g_frog_gy = nh / 2;
                g_placing_frog = false;
                g_frog_hovered = false;
                load_frog_sprite();
                set_canvas_size(g_hwnd_main, nw, nh);

                /* Reset vector curve paths */
                for (int p = 0; p < g_path_count; ++p) {
                    bezier_path_free(&g_paths[p]);
                }
                g_path_count = 0;
                g_active_path_idx = -1;
                g_mode_curve = false;
                if (g_btn_curve) InvalidateRect(g_btn_curve, NULL, TRUE);

                /* Reset coins, cutouts, sprites, and background alphas */
                g_treasure_point_count = 0;
                g_placing_coin = false;
                g_hovered_coin_idx = -1;
                if (g_btn_coin) InvalidateRect(g_btn_coin, NULL, TRUE);

                for (int i = 0; i < g_bg_alpha_count; ++i) free_bgalpha(&g_bg_alphas[i]);
                g_bg_alpha_count = 0;
                g_placing_bgalpha = false;
                g_placing_bgalpha_vector = false;
                g_hovered_bgalpha_idx = -1;

                for (int i = 0; i < g_cutout_count; ++i) free_cutout(&g_cutouts[i]);
                g_cutout_count = 0;
                g_placing_cutout = false;
                g_hovered_cutout_idx = -1;

                for (int i = 0; i < g_sprite_count; ++i) free_sprite(&g_sprites[i]);
                g_sprite_count = 0;
                g_placing_sprite = false;
                g_hovered_sprite_idx = -1;
                if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);

                g_canvas_folder[0] = '\0';

                if (g_has_bg_image) {
                    image_free(&g_canvas_img);
                    g_has_bg_image = false;
                }

                if (g_dlg_data->has_temp_img) {
                    g_canvas_img = g_dlg_data->temp_img;
                    g_has_bg_image = true;
                    memset(&g_dlg_data->temp_img, 0, sizeof(CanvasImage));
                    g_dlg_data->has_temp_img = false;
                }

                /* Update main window title */
                update_window_title();

                zoom_to_fit(g_hwnd_main);
                undo_reset();
                undo_ensure_baseline();
                if (g_hwnd_main && IsWindow(g_hwnd_main)) {
                    undo_redo_update_menu(g_hwnd_main);
                }
                DestroyWindow(hwnd);
            } else if (cmd == ID_DLG_BTN_CANCEL) {
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_TIMER:
            if (wParam == 9921) {
                KillTimer(hwnd, 9921);
                hide_edit_warning_tooltip();
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            destroy_edit_warning_tooltip();
            if (g_active_modal_dlg == hwnd) g_active_modal_dlg = NULL;
            if (g_dlg_data) {
                if (g_dlg_data->has_temp_img) {
                    image_free(&g_dlg_data->temp_img);
                }
                free(g_dlg_data);
                g_dlg_data = NULL;
            }
            EnableWindow(g_hwnd_main, TRUE);
            SetFocus(g_hwnd_main);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void show_new_canvas_dialog(HWND parent_hwnd) {
    if (g_dlg_data) {
        SetFocus(g_dlg_data->hwnd_dlg);
        return;
    }

    HINSTANCE hinst = GetModuleHandle(NULL);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = NewCanvasModalProc;
    wc.hInstance     = hinst;
    wc.lpszClassName = "ZuPatherNewCanvasModal";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    int dlg_w = 460;
    int dlg_h = 350;

    RECT rc_parent;
    GetWindowRect(parent_hwnd, &rc_parent);
    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;

    g_dlg_data = (NewCanvasDlgData*)calloc(1, sizeof(NewCanvasDlgData));

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherNewCanvasModal", "New Canvas Setup",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, NULL
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    g_dlg_data->hwnd_dlg = hwnd_dlg;
    EnableWindow(parent_hwnd, FALSE);

    /* Canvas Name Edit (forced lowercase, no space default) */
    char init_canvas_name[128] = "level1";
    if (g_has_canvas && g_canvas_name[0]) {
        strncpy(init_canvas_name, g_canvas_name, sizeof(init_canvas_name) - 1);
        for (char *p = init_canvas_name; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
            else if (*p == ' ') *p = '_';
        }
    }
    g_dlg_data->hwnd_name = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", init_canvas_name,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        24, 98, 194, 26, hwnd_dlg, (HMENU)ID_DLG_EDIT_NAME, hinst, NULL
    );
    SendMessage(g_dlg_data->hwnd_name, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);

    /* Canvas Title Edit */
    const char *init_title = g_has_canvas ? (g_canvas_title[0] ? g_canvas_title : g_canvas_name) : "Level 1";
    g_dlg_data->hwnd_title = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", init_title,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        230, 98, 194, 26, hwnd_dlg, (HMENU)ID_DLG_EDIT_TITLE, hinst, NULL
    );
    SendMessage(g_dlg_data->hwnd_title, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);

    g_orig_canvas_name_proc = (WNDPROC)SetWindowLongPtrA(
        g_dlg_data->hwnd_name, GWLP_WNDPROC, (LONG_PTR)CanvasNameEditSubclassProc
    );
    g_orig_canvas_title_proc = (WNDPROC)SetWindowLongPtrA(
        g_dlg_data->hwnd_title, GWLP_WNDPROC, (LONG_PTR)CanvasTitleEditSubclassProc
    );

    /* Dimensions W x H */
    char buf_w[32], buf_h[32];
    snprintf(buf_w, sizeof(buf_w), "%d", g_has_canvas ? g_canvas_w : DEFAULT_CANVAS_WIDTH);
    snprintf(buf_h, sizeof(buf_h), "%d", g_has_canvas ? g_canvas_h : DEFAULT_CANVAS_HEIGHT);

    g_dlg_data->hwnd_w = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", buf_w,
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP,
        24, 164, 106, 26, hwnd_dlg, (HMENU)ID_DLG_EDIT_W, hinst, NULL
    );
    g_dlg_data->hwnd_h = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", buf_h,
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP,
        158, 164, 106, 26, hwnd_dlg, (HMENU)ID_DLG_EDIT_H, hinst, NULL
    );
    SendMessage(g_dlg_data->hwnd_w, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);
    SendMessage(g_dlg_data->hwnd_h, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);

    /* Image Path / Name Display (starts EMPTY; displays 'No image selected' placeholder when inactive) */
    g_dlg_data->hwnd_imgpath = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
        24, 228, 250, 26, hwnd_dlg, (HMENU)ID_DLG_EDIT_IMGPATH, hinst, NULL
    );
    SendMessage(g_dlg_data->hwnd_imgpath, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);
    g_orig_imgpath_edit_proc = (WNDPROC)SetWindowLongPtrA(
        g_dlg_data->hwnd_imgpath, GWLP_WNDPROC, (LONG_PTR)ImgPathEditSubclassProc
    );

    /* Browse & Clear Image Buttons */
    g_dlg_data->hwnd_btn_browse = CreateWindowA(
        "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        282, 228, 80, 26, hwnd_dlg, (HMENU)ID_DLG_BTN_BROWSE, hinst, NULL
    );
    g_dlg_data->hwnd_btn_clear = CreateWindowA(
        "BUTTON", "Clear", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        368, 228, 56, 26, hwnd_dlg, (HMENU)ID_DLG_BTN_CLEAR_IMG, hinst, NULL
    );

    /* Bottom Action Buttons */
    g_dlg_data->hwnd_btn_create = CreateWindowA(
        "BUTTON", "Create Canvas", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        200, 274, 130, 28, hwnd_dlg, (HMENU)ID_DLG_BTN_CREATE, hinst, NULL
    );
    g_dlg_data->hwnd_btn_cancel = CreateWindowA(
        "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        338, 274, 86, 28, hwnd_dlg, (HMENU)ID_DLG_BTN_CANCEL, hinst, NULL
    );

    SetFocus(g_dlg_data->hwnd_name);
}


/* ========================================================================= */
/* Save Canvas Modal Dialog                                                  */
/* ========================================================================= */
typedef struct {
    HWND hwnd_dlg;
    HWND hwnd_name;
    HWND hwnd_title;
    HWND hwnd_chk_orig;
    HWND hwnd_lbl_orig_ext;
    HWND hwnd_combo_fmt;
    HWND hwnd_combo_diff;
    HWND hwnd_lbl_prog_id;
    LevelDifficulty selected_diff;
    HWND hwnd_btn_save;
    HWND hwnd_btn_cancel;
    HWND hwnd_tooltip;
    bool can_keep_orig;
    bool chk_orig_checked;

    /* Multi-asset support */
    SaveCanvasAssetPlan plan;
    int selected_asset_idx;
    int hovered_asset_idx;
    int asset_scroll_x;
    bool is_dragging_scroll;
    int drag_start_mx;
    int drag_start_scroll_x;
    bool is_hovering_thumb;
} SaveCanvasDlgData;

static SaveCanvasDlgData *g_save_dlg_data = NULL;

void sync_save_dialog_title_if_open(const char *title, HWND sender_hwnd) {
    if (g_save_dlg_data && g_save_dlg_data->hwnd_title && IsWindow(g_save_dlg_data->hwnd_title)) {
        if (sender_hwnd != g_save_dlg_data->hwnd_title) {
            char cur[128] = {0};
            GetWindowTextA(g_save_dlg_data->hwnd_title, cur, sizeof(cur));
            if (strcmp(cur, title ? title : "") != 0) {
                SetWindowTextA(g_save_dlg_data->hwnd_title, title ? title : "");
            }
        }
    }
}

static WNDPROC g_orig_save_chk_proc = NULL;
static WNDPROC g_orig_save_combo_proc = NULL;
static WNDPROC g_orig_save_combo_diff_proc = NULL;
static void format_calc_ram_str(uint64_t bytes, char *out_buf, size_t sz);

static void sync_save_dlg_asset_controls(HWND hwnd) {
    if (!g_save_dlg_data) return;
    if (g_save_dlg_data->selected_asset_idx < 0 || g_save_dlg_data->selected_asset_idx >= g_save_dlg_data->plan.count) {
        if (g_save_dlg_data->hwnd_chk_orig) ShowWindow(g_save_dlg_data->hwnd_chk_orig, SW_HIDE);
        if (g_save_dlg_data->hwnd_lbl_orig_ext) ShowWindow(g_save_dlg_data->hwnd_lbl_orig_ext, SW_HIDE);
        if (g_save_dlg_data->hwnd_combo_fmt) ShowWindow(g_save_dlg_data->hwnd_combo_fmt, SW_HIDE);
        return;
    }

    SaveCanvasAssetItem *item = &g_save_dlg_data->plan.items[g_save_dlg_data->selected_asset_idx];
    ShowWindow(g_save_dlg_data->hwnd_chk_orig, SW_SHOW);

    /* Keep original format checkbox */
    g_save_dlg_data->can_keep_orig = item->can_keep_orig;
    /* Keep HWND enabled so SaveThemedChkSubclassProc and Tooltip receive hover messages even when uneditable */
    EnableWindow(g_save_dlg_data->hwnd_chk_orig, TRUE);
    CheckDlgButton(hwnd, ID_SAVE_CHK_ORIG, item->keep_orig ? BST_CHECKED : BST_UNCHECKED);
    g_save_dlg_data->chk_orig_checked = item->keep_orig;
    InvalidateRect(g_save_dlg_data->hwnd_chk_orig, NULL, TRUE);

    /* Tooltip trigger logic:
     * ONLY show format not supported warning when checkbox is UNEDITABLE (!item->can_keep_orig).
     * When EDITABLE (item->can_keep_orig == true), the format is fully supported, so DEACTIVATE tooltip. */
    if (g_save_dlg_data->hwnd_tooltip) {
        TOOLINFOA ti;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
        ti.hwnd = g_save_dlg_data->hwnd_dlg;
        ti.uId = (UINT_PTR)g_save_dlg_data->hwnd_chk_orig;

        if (!item->can_keep_orig) {
            ti.lpszText = "Original format is not supported by Zuma Deluxe.\nPlease select an export format (.webp, .png, .jpg, .gif) from the dropdown.";
            SendMessageA(g_save_dlg_data->hwnd_tooltip, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
            SendMessage(g_save_dlg_data->hwnd_tooltip, TTM_ACTIVATE, TRUE, 0);
        } else {
            SendMessage(g_save_dlg_data->hwnd_tooltip, TTM_POP, 0, 0);
            SendMessage(g_save_dlg_data->hwnd_tooltip, TTM_ACTIVATE, FALSE, 0);
            ti.lpszText = NULL;
            SendMessageA(g_save_dlg_data->hwnd_tooltip, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
        }
    }

    /* Label extension vs ComboBox */
    if (item->keep_orig) {
        ShowWindow(g_save_dlg_data->hwnd_combo_fmt, SW_HIDE);
        EnableWindow(g_save_dlg_data->hwnd_combo_fmt, FALSE);
        ShowWindow(g_save_dlg_data->hwnd_lbl_orig_ext, SW_SHOW);
        char ext_lbl[64];
        snprintf(ext_lbl, sizeof(ext_lbl), "(format: .%s)", item->orig_ext[0] ? item->orig_ext : "png");
        SetWindowTextA(g_save_dlg_data->hwnd_lbl_orig_ext, ext_lbl);
    } else {
        ShowWindow(g_save_dlg_data->hwnd_combo_fmt, SW_SHOW);
        EnableWindow(g_save_dlg_data->hwnd_combo_fmt, TRUE);
        ShowWindow(g_save_dlg_data->hwnd_lbl_orig_ext, SW_HIDE);
        int sel = 1; /* PNG default */
        if (item->selected_format == IMG_EXPORT_WEBP) sel = 0;
        else if (item->selected_format == IMG_EXPORT_PNG) sel = 1;
        else if (item->selected_format == IMG_EXPORT_JPG) sel = 2;
        else if (item->selected_format == IMG_EXPORT_GIF) sel = 3;
        SendMessage(g_save_dlg_data->hwnd_combo_fmt, CB_SETCURSEL, sel, 0);
    }
}

static void draw_dialog_themed_checkbox(HWND hwnd, HDC hdc, bool is_checked, bool is_disabled) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const ThemeColors *th = get_theme();
    bool is_dark = (g_current_theme == THEME_DARK);

    COLORREF bg_col = th->bar_bg;
    COLORREF box_bg = is_dark ? RGB(36, 40, 48) : RGB(255, 255, 255);
    COLORREF box_bd = is_dark ? RGB(70, 78, 92) : RGB(175, 182, 194);
    COLORREF text_col = th->text_primary;
    COLORREF check_col = is_dark ? RGB(97, 175, 239) : RGB(24, 90, 175);

    if (is_disabled) {
        BYTE bg_r = GetRValue(bg_col), bg_g = GetGValue(bg_col), bg_b = GetBValue(bg_col);
        BYTE tx_r = GetRValue(text_col), tx_g = GetGValue(text_col), tx_b = GetBValue(text_col);
        BYTE bd_r = GetRValue(box_bd), bd_g = GetGValue(box_bd), bd_b = GetBValue(box_bd);
        text_col = RGB((tx_r + bg_r) / 2, (tx_g + bg_g) / 2, (tx_b + bg_b) / 2);
        box_bd = RGB((bd_r + bg_r) / 2, (bd_g + bg_g) / 2, (bd_b + bg_b) / 2);
        box_bg = is_dark ? RGB(bg_r + 10, bg_g + 10, bg_b + 10) : RGB(bg_r - 8, bg_g - 8, bg_b - 8);
    }

    HBRUSH bg_br = CreateSolidBrush(bg_col);
    FillRect(hdc, &rc, bg_br);
    DeleteObject(bg_br);

    int box_sz = 13;
    int box_x = 1;
    int box_y = (rc.bottom - rc.top - box_sz) / 2;
    RECT rc_box = { box_x, box_y, box_x + box_sz, box_y + box_sz };

    HBRUSH in_br = CreateSolidBrush(box_bg);
    FillRect(hdc, &rc_box, in_br);
    DeleteObject(in_br);

    HPEN pen = CreatePen(PS_SOLID, 1, box_bd);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc_box.left, rc_box.top, rc_box.right, rc_box.bottom);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    if (is_checked) {
        HPEN chk_pen = CreatePen(PS_SOLID, 2, check_col);
        old_pen = (HPEN)SelectObject(hdc, chk_pen);
        MoveToEx(hdc, box_x + 3, box_y + 6, NULL);
        LineTo(hdc, box_x + 5, box_y + 10);
        LineTo(hdc, box_x + 10, box_y + 3);
        SelectObject(hdc, old_pen);
        DeleteObject(chk_pen);
    }

    char text[128] = {0};
    GetWindowTextA(hwnd, text, sizeof(text));
    RECT rc_text = { box_x + box_sz + 6, 0, rc.right, rc.bottom };
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_col);
    HFONT font = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old_font = (HFONT)SelectObject(hdc, font);
    DrawTextA(hdc, text, -1, &rc_text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    SelectObject(hdc, old_font);
}

static LRESULT CALLBACK SaveThemedChkSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!g_save_dlg_data) return DefWindowProcA(hwnd, msg, wParam, lParam);
    bool is_disabled = !g_save_dlg_data->can_keep_orig;
    bool *p_chk = &g_save_dlg_data->chk_orig_checked;

    switch (msg) {
        case WM_LBUTTONDOWN:
            if (!is_disabled) {
                SetFocus(hwnd);
                *p_chk = !(*p_chk);
                InvalidateRect(hwnd, NULL, TRUE);
                SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
            } else {
                if (g_save_dlg_data && g_save_dlg_data->hwnd_tooltip) {
                    SendMessage(g_save_dlg_data->hwnd_tooltip, TTM_POPUP, 0, 0);
                }
            }
            return 0;

        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            return 0;

        case WM_KEYDOWN:
            if (!is_disabled && wParam == VK_SPACE) {
                *p_chk = !(*p_chk);
                InvalidateRect(hwnd, NULL, TRUE);
                SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
                return 0;
            }
            if (is_disabled) return 0;
            break;

        case WM_KEYUP:
        case WM_CHAR:
            if (is_disabled) return 0;
            break;

        case WM_SETCURSOR:
            if (is_disabled) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }
            break;

        case BM_SETCHECK:
            *p_chk = (wParam == BST_CHECKED);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;

        case BM_GETCHECK:
            return *p_chk ? BST_CHECKED : BST_UNCHECKED;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            draw_dialog_themed_checkbox(hwnd, hdc, *p_chk, is_disabled);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return CallWindowProcA(g_orig_save_chk_proc ? g_orig_save_chk_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
}

static WNDPROC g_orig_save_combo_diff_list_proc = NULL;
static int g_last_diff_hover_idx = -1;

static LRESULT CALLBACK SaveComboDiffListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_MOUSEMOVE) {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        LRESULT item = CallWindowProcA(g_orig_save_combo_diff_list_proc, hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        if (HIWORD(item) == 0) { /* 0 means cursor is inside listbox rect */
            int idx = LOWORD(item);
            if (idx != g_last_diff_hover_idx && g_save_dlg_data && g_save_dlg_data->hwnd_tooltip) {
                g_last_diff_hover_idx = idx;
                LevelDifficulty hov_diff = LEVEL_DIFF_MEDIUM;
                if (idx == 0) hov_diff = LEVEL_DIFF_DEV_TEST;
                else if (idx == 1) hov_diff = LEVEL_DIFF_EASY;
                else if (idx == 2) hov_diff = LEVEL_DIFF_MEDIUM;
                else if (idx == 3) hov_diff = LEVEL_DIFF_HARD;
                else if (idx == 4) hov_diff = LEVEL_DIFF_EXTREME;

                TOOLINFOA ti;
                memset(&ti, 0, sizeof(ti));
                ti.cbSize = sizeof(ti);
                ti.hwnd = hwnd;
                ti.uId = (UINT_PTR)hwnd;
                ti.lpszText = (LPSTR)level_difficulty_get_description(hov_diff);
                SendMessageA(g_save_dlg_data->hwnd_tooltip, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
            }
        }
    }
    return CallWindowProcA(g_orig_save_combo_diff_list_proc ? g_orig_save_combo_diff_list_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK SaveThemedComboSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WNDPROC orig_proc = (g_save_dlg_data && hwnd == g_save_dlg_data->hwnd_combo_diff)
                        ? g_orig_save_combo_diff_proc
                        : g_orig_save_combo_proc;
    if (!orig_proc) orig_proc = DefWindowProcA;

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            bool is_dark = (g_current_theme == THEME_DARK);

            COLORREF bg_col = is_dark ? RGB(36, 40, 48) : RGB(255, 255, 255);
            COLORREF border_col = is_dark ? RGB(70, 78, 92) : RGB(175, 182, 194);
            COLORREF text_col = is_dark ? RGB(222, 226, 235) : RGB(32, 36, 44);
            COLORREF arrow_col = is_dark ? RGB(160, 168, 182) : RGB(85, 92, 105);

            HBRUSH bg_br = CreateSolidBrush(bg_col);
            FillRect(hdc, &rc, bg_br);
            DeleteObject(bg_br);

            HPEN pen = CreatePen(PS_SOLID, 1, border_col);
            HPEN old_pen = (HPEN)SelectObject(hdc, pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, old_pen);
            DeleteObject(pen);

            int btn_w = 20;
            int arrow_cx = rc.right - btn_w / 2 - 2;
            int arrow_cy = (rc.bottom - rc.top) / 2;

            HPEN sep_pen = CreatePen(PS_SOLID, 1, border_col);
            old_pen = (HPEN)SelectObject(hdc, sep_pen);
            MoveToEx(hdc, rc.right - btn_w, rc.top + 3, NULL);
            LineTo(hdc, rc.right - btn_w, rc.bottom - 3);
            SelectObject(hdc, old_pen);
            DeleteObject(sep_pen);

            POINT pts[3];
            pts[0].x = arrow_cx - 4; pts[0].y = arrow_cy - 2;
            pts[1].x = arrow_cx + 4; pts[1].y = arrow_cy - 2;
            pts[2].x = arrow_cx;     pts[2].y = arrow_cy + 2;

            HBRUSH ar_br = CreateSolidBrush(arrow_col);
            HPEN ar_pen = CreatePen(PS_SOLID, 1, arrow_col);
            old_pen = (HPEN)SelectObject(hdc, ar_pen);
            HBRUSH old_br = (HBRUSH)SelectObject(hdc, ar_br);
            Polygon(hdc, pts, 3);
            SelectObject(hdc, old_br);
            SelectObject(hdc, old_pen);
            DeleteObject(ar_br);
            DeleteObject(ar_pen);

            int sel = (int)CallWindowProcA(orig_proc, hwnd, CB_GETCURSEL, 0, 0);
            char text[64] = {0};
            if (sel >= 0) {
                CallWindowProcA(orig_proc, hwnd, CB_GETLBTEXT, sel, (LPARAM)text);
            }

            bool is_dev = (strstr(text, "Dev-Test") != NULL);
            if (is_dev) {
                text_col = is_dark ? RGB(255, 175, 45) : RGB(210, 100, 0);
            }

            RECT rc_text = { rc.left + 8, rc.top, rc.right - btn_w - 4, rc.bottom };
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, text_col);
            HFONT hfont = is_dev ? (g_font_primary_bold ? g_font_primary_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT))
                                 : (g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            HFONT old_font = (HFONT)SelectObject(hdc, hfont);
            DrawTextA(hdc, text, -1, &rc_text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            SelectObject(hdc, old_font);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;
    }
    return CallWindowProcA(orig_proc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK SaveCanvasModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();

    switch (msg) {
        case WM_CREATE:
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            /* Double buffering */
            HDC mem_dc = CreateCompatibleDC(hdc);
            HBITMAP mem_bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP old_bm = (HBITMAP)SelectObject(mem_dc, mem_bm);

            HBRUSH bg_brush = CreateSolidBrush(th->bar_bg);
            FillRect(mem_dc, &rc, bg_brush);
            DeleteObject(bg_brush);

            HPEN border_pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(mem_dc, border_pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, 0, 0, rc.right, rc.bottom);

            SetBkMode(mem_dc, TRANSPARENT);
            SetTextColor(mem_dc, th->text_primary);
            SelectObject(mem_dc, g_font_primary_bold);
            RECT rc_title = {24, 16, rc.right - 24, 38};
            DrawTextA(mem_dc, "Save Canvas Project", -1, &rc_title, DT_SINGLELINE | DT_LEFT | DT_TOP);

            if (g_map_is_space) {
                RECT rc_calc = {0, 0, 0, 0};
                DrawTextA(mem_dc, "Save Canvas Project", -1, &rc_calc, DT_CALCRECT | DT_SINGLELINE);
                int badge_x = 24 + (rc_calc.right - rc_calc.left) + 8;
                int badge_y = 17;
                RECT rc_sp_badge = {badge_x, badge_y, badge_x + 90, badge_y + 18};
                HBRUSH sp_br = CreateSolidBrush(RGB(110, 45, 175));
                HPEN sp_p = CreatePen(PS_SOLID, 1, RGB(145, 75, 220));
                SelectObject(mem_dc, sp_br);
                SelectObject(mem_dc, sp_p);
                RoundRect(mem_dc, rc_sp_badge.left, rc_sp_badge.top, rc_sp_badge.right, rc_sp_badge.bottom, 4, 4);
                DeleteObject(sp_br);
                DeleteObject(sp_p);
                SetTextColor(mem_dc, RGB(255, 255, 255));
                SelectObject(mem_dc, g_font_primary_bold);
                DrawTextA(mem_dc, "SPACE ARENA", -1, &rc_sp_badge, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }

            SetTextColor(mem_dc, th->text_secondary);
            SelectObject(mem_dc, g_font_primary_regular);
            RECT rc_sub = {24, 38, rc.right - 24, 56};
            DrawTextA(mem_dc, "Saves canvas folder and settings.xml to ./canvas/<name>/", -1, &rc_sub, DT_SINGLELINE | DT_LEFT | DT_TOP);

            /* Calculate live multi-asset animation RAM budget */
            CanvasAnimationBudgetReport anim_report;
            memset(&anim_report, 0, sizeof(anim_report));
            if (g_save_dlg_data) {
                calculate_canvas_animation_budget(&g_save_dlg_data->plan, &anim_report);
            }

            if (anim_report.count > 0) {
                char ram_str[96];
                char used_str[32], max_str[32];
                format_calc_ram_str(anim_report.total_capped_ram_bytes, used_str, sizeof(used_str));
                format_calc_ram_str(anim_report.ram_budget_bytes, max_str, sizeof(max_str));

                if (anim_report.exceeds_budget) {
                    char orig_str[32];
                    format_calc_ram_str(anim_report.total_orig_ram_bytes, orig_str, sizeof(orig_str));
                    snprintf(ram_str, sizeof(ram_str), "RAM: %s -> %s (Balanced)", orig_str, used_str);
                } else {
                    snprintf(ram_str, sizeof(ram_str), "Animation RAM: %s / %s Pool", used_str, max_str);
                }

                RECT rc_ram_txt = {rc.right - 220, 16, rc.right - 24, 38};
                SetTextColor(mem_dc, anim_report.exceeds_budget ? (anim_report.total_capped_ram_bytes >= anim_report.ram_budget_bytes ? RGB(235, 140, 40) : th->text_accent_blue) : ((g_current_theme == THEME_DARK) ? RGB(80, 200, 120) : RGB(25, 140, 60)));
                SelectObject(mem_dc, g_font_primary_bold);
                DrawTextA(mem_dc, ram_str, -1, &rc_ram_txt, DT_SINGLELINE | DT_RIGHT | DT_TOP);
            }

            /* 1. Horizontal Asset Carousel Strip (Between Subtitle and Canvas Name) */
            int strip_x = 24;
            int strip_y = 58;
            int strip_w = rc.right - 48;
            int strip_h = 86;
            RECT rc_strip = {strip_x, strip_y, strip_x + strip_w, strip_y + strip_h};

            COLORREF strip_bg = (g_current_theme == THEME_DARK) ? RGB(24, 28, 36) : RGB(236, 240, 246);
            HBRUSH strip_br = CreateSolidBrush(strip_bg);
            HPEN strip_pen = CreatePen(PS_SOLID, 1, th->bar_border);
            SelectObject(mem_dc, strip_br);
            SelectObject(mem_dc, strip_pen);
            RoundRect(mem_dc, rc_strip.left, rc_strip.top, rc_strip.right, rc_strip.bottom, 6, 6);
            DeleteObject(strip_br);
            DeleteObject(strip_pen);

            int total_cards_w = 0;
            if (g_save_dlg_data && g_save_dlg_data->plan.count > 0) {
                int card_w = 68;
                int card_h = 66;
                int card_gap = 6;
                total_cards_w = g_save_dlg_data->plan.count * (card_w + card_gap) - card_gap + 8;

                HRGN clip_rgn = CreateRectRgn(rc_strip.left + 2, rc_strip.top + 2, rc_strip.right - 2, strip_y + 71);
                SelectClipRgn(mem_dc, clip_rgn);

                for (int k = 0; k < g_save_dlg_data->plan.count; ++k) {
                    SaveCanvasAssetItem *item = &g_save_dlg_data->plan.items[k];
                    int card_x = rc_strip.left + 4 + k * (card_w + card_gap) - g_save_dlg_data->asset_scroll_x;
                    int card_y = rc_strip.top + 4;

                    if (card_x + card_w < rc_strip.left || card_x > rc_strip.right) continue;

                    bool is_sel = (k == g_save_dlg_data->selected_asset_idx);
                    bool is_hov = (k == g_save_dlg_data->hovered_asset_idx);

                    COLORREF card_bg = is_sel ? ((g_current_theme == THEME_DARK) ? RGB(34, 46, 66) : RGB(225, 236, 252))
                                              : ((g_current_theme == THEME_DARK) ? RGB(32, 36, 44) : RGB(248, 250, 252));
                    COLORREF card_bd = is_sel ? th->text_accent_blue : (is_hov ? th->text_secondary : th->bar_border);

                    HBRUSH card_br = CreateSolidBrush(card_bg);
                    HPEN card_pen = CreatePen(PS_SOLID, is_sel ? 2 : 1, card_bd);
                    SelectObject(mem_dc, card_br);
                    SelectObject(mem_dc, card_pen);
                    RoundRect(mem_dc, card_x, card_y, card_x + card_w, card_y + card_h, 4, 4);
                    DeleteObject(card_br);
                    DeleteObject(card_pen);

                    /* Thumbnail preview */
                    int box_x = card_x + 4;
                    int box_y = card_y + 4;
                    int box_w = card_w - 8;
                    int box_h = 36;

                    if (item->img_ref && (item->img_ref->first_frame_hdc || item->img_ref->hdc_mem) && item->width > 0 && item->height > 0) {
                        HDC src_hdc = item->img_ref->first_frame_hdc ? item->img_ref->first_frame_hdc : item->img_ref->hdc_mem;
                        float aspect_src = (float)item->width / (float)item->height;
                        float aspect_box = (float)box_w / (float)box_h;
                        int dw = box_w, dh = box_h;
                        if (aspect_src > aspect_box) {
                            dh = (int)((float)box_w / aspect_src);
                            if (dh < 1) dh = 1;
                        } else {
                            dw = (int)((float)box_h * aspect_src);
                            if (dw < 1) dw = 1;
                        }
                        int dx = box_x + (box_w - dw) / 2;
                        int dy = box_y + (box_h - dh) / 2;

                        SetStretchBltMode(mem_dc, HALFTONE);
                        SetBrushOrgEx(mem_dc, 0, 0, NULL);
                        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                        GdiAlphaBlend(mem_dc, dx, dy, dw, dh, src_hdc, 0, 0, item->width, item->height, bf);
                    } else {
                        RECT rc_ph = {box_x, box_y, box_x + box_w, box_y + box_h};
                        SetTextColor(mem_dc, th->text_dim);
                        SelectObject(mem_dc, g_font_primary_regular);
                        DrawTextA(mem_dc, item->orig_ext, -1, &rc_ph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                    }

                    /* Format Pill Badge (top-right of thumbnail) - Reflects CURRENT ORIGINAL FILE format */
                    const char *badge_fmt = item->orig_ext[0] ? item->orig_ext : (item->is_video ? "mp4" : "png");
                    char fmt_str[16];
                    snprintf(fmt_str, sizeof(fmt_str), ".%s", badge_fmt);
                    for (char *p = fmt_str; *p; p++) {
                        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - ('a' - 'A'));
                    }

                    /* Dynamically calculate badge width from font metrics so text is never truncated */
                    RECT rc_calc = {0, 0, 0, 0};
                    HFONT badge_font = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                    HFONT old_bf = (HFONT)SelectObject(mem_dc, badge_font);
                    DrawTextA(mem_dc, fmt_str, -1, &rc_calc, DT_CALCRECT | DT_SINGLELINE);
                    int tw = rc_calc.right - rc_calc.left;
                    int bw = tw + 8;
                    if (bw < 28) bw = 28;
                    int bh = 13;
                    int bx = card_x + card_w - bw - 3;
                    int by = card_y + 3;
                    RECT rc_badge = {bx, by, bx + bw, by + bh};

                    COLORREF badge_bg = item->is_video ? RGB(200, 50, 50) : (image_is_zuma_native_ext(badge_fmt) ? RGB(35, 130, 65) : th->text_accent_blue);
                    HBRUSH b_br = CreateSolidBrush(badge_bg);
                    FillRect(mem_dc, &rc_badge, b_br);
                    DeleteObject(b_br);

                    SetBkMode(mem_dc, TRANSPARENT);
                    SetTextColor(mem_dc, RGB(255, 255, 255));
                    DrawTextA(mem_dc, fmt_str, -1, &rc_badge, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                    SelectObject(mem_dc, old_bf);

                    /* Card label below thumbnail */
                    RECT rc_lbl = {card_x + 2, card_y + 43, card_x + card_w - 2, card_y + card_h - 2};
                    SetTextColor(mem_dc, is_sel ? th->text_accent_blue : th->text_primary);
                    SelectObject(mem_dc, is_sel ? g_font_primary_bold : g_font_primary_regular);
                    DrawTextA(mem_dc, item->label, -1, &rc_lbl, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
                }

                SelectClipRgn(mem_dc, NULL);
                DeleteObject(clip_rgn);

                /* Horizontal Drag Scrollbar at the bottom of the strip */
                if (total_cards_w > strip_w) {
                    int track_x = strip_x + 8;
                    int track_y = strip_y + 74;
                    int track_w = strip_w - 16;
                    int track_h = 6;
                    int max_scroll = total_cards_w - strip_w;

                    float view_ratio = (float)strip_w / (float)total_cards_w;
                    int thumb_w = (int)(track_w * view_ratio);
                    if (thumb_w < 36) thumb_w = 36;
                    if (thumb_w > track_w) thumb_w = track_w;
                    int scrollable_track_w = track_w - thumb_w;

                    int thumb_x = track_x;
                    if (max_scroll > 0 && scrollable_track_w > 0) {
                        thumb_x = track_x + (int)((float)g_save_dlg_data->asset_scroll_x / (float)max_scroll * scrollable_track_w);
                    }
                    if (thumb_x < track_x) thumb_x = track_x;
                    if (thumb_x + thumb_w > track_x + track_w) thumb_x = track_x + track_w - thumb_w;

                    bool is_dark = (g_current_theme == THEME_DARK);

                    /* Track colors */
                    COLORREF tr_bg = is_dark ? RGB(16, 20, 26) : RGB(220, 226, 235);
                    COLORREF tr_bd = is_dark ? RGB(38, 44, 54) : RGB(200, 206, 216);

                    /* Thumb colors (Adaptive Dark / Light) */
                    COLORREF th_bg, th_bd;
                    if (g_save_dlg_data->is_dragging_scroll) {
                        th_bg = th->text_accent_blue;
                        th_bd = is_dark ? RGB(160, 210, 255) : RGB(14, 60, 125);
                    } else if (g_save_dlg_data->is_hovering_thumb) {
                        th_bg = is_dark ? RGB(88, 98, 116) : RGB(140, 150, 166);
                        th_bd = is_dark ? RGB(110, 122, 142) : RGB(115, 125, 140);
                    } else {
                        th_bg = is_dark ? RGB(60, 68, 82) : RGB(172, 180, 194);
                        th_bd = is_dark ? RGB(78, 88, 104) : RGB(150, 158, 172);
                    }

                    /* Draw track capsule */
                    HBRUSH tr_br = CreateSolidBrush(tr_bg);
                    HPEN tr_pen = CreatePen(PS_SOLID, 1, tr_bd);
                    SelectObject(mem_dc, tr_br);
                    SelectObject(mem_dc, tr_pen);
                    RoundRect(mem_dc, track_x, track_y, track_x + track_w, track_y + track_h, 4, 4);
                    DeleteObject(tr_br);
                    DeleteObject(tr_pen);

                    /* Draw thumb capsule */
                    HBRUSH th_br = CreateSolidBrush(th_bg);
                    HPEN th_pen = CreatePen(PS_SOLID, 1, th_bd);
                    SelectObject(mem_dc, th_br);
                    SelectObject(mem_dc, th_pen);
                    RoundRect(mem_dc, thumb_x, track_y, thumb_x + thumb_w, track_y + track_h, 4, 4);
                    DeleteObject(th_br);
                    DeleteObject(th_pen);
                }
            } else {
                RECT rc_empty = rc_strip;
                if (g_map_is_space) {
                    COLORREF sp_card_bg = (g_current_theme == THEME_DARK) ? RGB(38, 24, 56) : RGB(244, 236, 252);
                    COLORREF sp_card_bd = (g_current_theme == THEME_DARK) ? RGB(140, 70, 220) : RGB(160, 90, 230);
                    HBRUSH sp_cbr = CreateSolidBrush(sp_card_bg);
                    HPEN sp_cpen = CreatePen(PS_SOLID, 1, sp_card_bd);
                    SelectObject(mem_dc, sp_cbr);
                    SelectObject(mem_dc, sp_cpen);
                    RoundRect(mem_dc, rc_empty.left + 8, rc_empty.top + 6, rc_empty.right - 8, rc_empty.bottom - 6, 6, 6);
                    DeleteObject(sp_cbr);
                    DeleteObject(sp_cpen);

                    RECT rc_sp_title = {rc_empty.left + 16, rc_empty.top + 12, rc_empty.right - 16, rc_empty.top + 28};
                    SetTextColor(mem_dc, (g_current_theme == THEME_DARK) ? RGB(230, 185, 255) : RGB(120, 30, 180));
                    SelectObject(mem_dc, g_font_primary_bold);
                    DrawTextA(mem_dc, "SPACE ARENA (space=\"true\")", -1, &rc_sp_title, DT_SINGLELINE | DT_LEFT | DT_TOP);

                    RECT rc_sp_desc = {rc_empty.left + 16, rc_empty.top + 32, rc_empty.right - 16, rc_empty.bottom - 8};
                    SetTextColor(mem_dc, (g_current_theme == THEME_DARK) ? RGB(200, 180, 220) : RGB(90, 60, 120));
                    SelectObject(mem_dc, g_font_primary_regular);
                    DrawTextA(mem_dc, "Procedural 3D starfield active in Zuma Deluxe.\r\nBackground, cutouts, sprites, and bgcolor are overridden.", -1, &rc_sp_desc, DT_LEFT | DT_WORDBREAK);
                } else {
                    SetTextColor(mem_dc, th->text_dim);
                    SelectObject(mem_dc, g_font_primary_regular);
                    DrawTextA(mem_dc, "No images or overlay assets detected in canvas", -1, &rc_empty, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                }
            }

            /* Detail hover / selected status text below carousel */
            int active_idx = (g_save_dlg_data && g_save_dlg_data->hovered_asset_idx >= 0)
                             ? g_save_dlg_data->hovered_asset_idx
                             : (g_save_dlg_data ? g_save_dlg_data->selected_asset_idx : -1);
            if (g_save_dlg_data && active_idx >= 0 && active_idx < g_save_dlg_data->plan.count) {
                SaveCanvasAssetItem *it = &g_save_dlg_data->plan.items[active_idx];
                char detail_str[384];
                const char *exp_txt = it->keep_orig ? "Keep original format"
                                                    : (it->selected_format == IMG_EXPORT_WEBP ? "Convert to WebP" :
                                                      (it->selected_format == IMG_EXPORT_PNG ? "Convert to PNG" :
                                                      (it->selected_format == IMG_EXPORT_JPG ? "Convert to JPG" : "Convert to GIF")));

                /* Match with anim_report if available */
                const CanvasAnimationAssetStat *matched_st = NULL;
                for (int s = 0; s < anim_report.count; ++s) {
                    if (anim_report.assets[s].plan_index == active_idx) {
                        matched_st = &anim_report.assets[s];
                        break;
                    }
                }

                if (matched_st) {
                    char ram_buf[32];
                    format_calc_ram_str(matched_st->capped_ram_bytes, ram_buf, sizeof(ram_buf));
                    if (matched_st->is_capped) {
                        char orig_ram_buf[32];
                        format_calc_ram_str(matched_st->orig_ram_bytes, orig_ram_buf, sizeof(orig_ram_buf));
                        snprintf(detail_str, sizeof(detail_str), "%s (%s) | %dx%d px | %s%s -> %s | RAM: ~%s (Balanced from ~%s, %d fr)",
                                 it->label, it->id_name, matched_st->target_w, matched_st->target_h,
                                 it->orig_ext, it->is_video ? " (Video)" : "", exp_txt,
                                 ram_buf, orig_ram_buf, matched_st->capped_frames);
                    } else {
                        snprintf(detail_str, sizeof(detail_str), "%s (%s) | %dx%d px | %s%s -> %s | RAM: ~%s (100%% Full Duration, %d fr)",
                                 it->label, it->id_name, matched_st->target_w, matched_st->target_h,
                                 it->orig_ext, it->is_video ? " (Video)" : "", exp_txt,
                                 ram_buf, matched_st->capped_frames);
                    }
                } else {
                    snprintf(detail_str, sizeof(detail_str), "%s (%s) | %dx%d px | %s%s -> %s",
                             it->label, it->id_name, it->width, it->height,
                             it->orig_ext, it->is_video ? " (Video)" : "", exp_txt);
                }
                RECT rc_detail = {24, 148, rc.right - 24, 166};
                SetTextColor(mem_dc, th->text_secondary);
                SelectObject(mem_dc, g_font_primary_regular);
                DrawTextA(mem_dc, detail_str, -1, &rc_detail, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);
            }

            /* 2. Canvas Name & Title Form */
            SelectObject(mem_dc, g_font_primary_bold);
            SetTextColor(mem_dc, th->text_primary);
            RECT rc_lbl1 = {24, 170, 220, 188};
            DrawTextA(mem_dc, "Canvas Name (Folder):", -1, &rc_lbl1, DT_SINGLELINE | DT_LEFT | DT_TOP);

            RECT rc_lbl_title = {236, 170, rc.right - 24, 188};
            DrawTextA(mem_dc, "Canvas Title (dispname):", -1, &rc_lbl_title, DT_SINGLELINE | DT_LEFT | DT_TOP);

            SelectObject(mem_dc, g_font_primary_regular);
            SetTextColor(mem_dc, th->text_secondary);
            char info_buf[128];
            snprintf(info_buf, sizeof(info_buf), "Canvas Size: %d x %d px", g_canvas_w, g_canvas_h);
            RECT rc_dim = {24, 224, rc.right - 24, 240};
            DrawTextA(mem_dc, info_buf, -1, &rc_dim, DT_SINGLELINE | DT_LEFT | DT_TOP);

            /* 3. Asset Export Options Header */
            if (g_save_dlg_data && g_save_dlg_data->selected_asset_idx >= 0 && g_save_dlg_data->selected_asset_idx < g_save_dlg_data->plan.count) {
                SaveCanvasAssetItem *sel_it = &g_save_dlg_data->plan.items[g_save_dlg_data->selected_asset_idx];
                char opt_hdr[160];
                if (g_map_is_space) {
                    snprintf(opt_hdr, sizeof(opt_hdr), "Export Options: %s (%s) [Overridden by Space]:", sel_it->label, sel_it->id_name);
                } else {
                    snprintf(opt_hdr, sizeof(opt_hdr), "Export Options for %s (%s):", sel_it->label, sel_it->id_name);
                }
                SelectObject(mem_dc, g_font_primary_bold);
                SetTextColor(mem_dc, g_map_is_space ? ((g_current_theme == THEME_DARK) ? RGB(215, 160, 255) : RGB(130, 40, 190)) : th->text_primary);
                RECT rc_lbl_opt = {24, 246, rc.right - 24, 264};
                DrawTextA(mem_dc, opt_hdr, -1, &rc_lbl_opt, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);
            } else {
                SelectObject(mem_dc, g_font_primary_regular);
                SetTextColor(mem_dc, th->text_dim);
                RECT rc_no_opt = {24, 246, rc.right - 24, 264};
                DrawTextA(mem_dc, "No assets selected to configure.", -1, &rc_no_opt, DT_SINGLELINE | DT_LEFT | DT_TOP);
            }

            /* 4. Level Progression & Difficulty Section */
            SelectObject(mem_dc, g_font_primary_bold);
            SetTextColor(mem_dc, th->text_primary);
            RECT rc_lbl_diff = {24, 302, 220, 320};
            DrawTextA(mem_dc, "Progression Difficulty:", -1, &rc_lbl_diff, DT_SINGLELINE | DT_LEFT | DT_TOP);

            RECT rc_lbl_prog = {236, 302, rc.right - 24, 320};
            DrawTextA(mem_dc, "Mapped Zuma Progression:", -1, &rc_lbl_prog, DT_SINGLELINE | DT_LEFT | DT_TOP);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem_dc, 0, 0, SRCCOPY);

            SelectObject(mem_dc, old_pen);
            DeleteObject(border_pen);
            SelectObject(mem_dc, old_bm);
            DeleteObject(mem_bm);
            DeleteDC(mem_dc);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int strip_x = 24;
            int strip_y = 58;
            int strip_w = rc.right - 48;
            int strip_h = 86;

            if (g_save_dlg_data && g_save_dlg_data->plan.count > 0) {
                int card_w = 68;
                int card_gap = 6;
                int total_cards_w = g_save_dlg_data->plan.count * (card_w + card_gap) - card_gap + 8;
                bool has_overflow = (total_cards_w > strip_w);

                /* 1. Check Scrollbar Hit & Drag */
                if (has_overflow) {
                    int track_x = strip_x + 8;
                    int track_y = strip_y + 74;
                    int track_w = strip_w - 16;
                    int track_h = 6;
                    int max_scroll = total_cards_w - strip_w;

                    if (my >= track_y - 4 && my <= track_y + track_h + 4 && mx >= track_x && mx <= track_x + track_w) {
                        float view_ratio = (float)strip_w / (float)total_cards_w;
                        int thumb_w = (int)(track_w * view_ratio);
                        if (thumb_w < 36) thumb_w = 36;
                        if (thumb_w > track_w) thumb_w = track_w;
                        int scrollable_track_w = track_w - thumb_w;

                        int thumb_x = track_x;
                        if (max_scroll > 0 && scrollable_track_w > 0) {
                            thumb_x = track_x + (int)((float)g_save_dlg_data->asset_scroll_x / (float)max_scroll * scrollable_track_w);
                        }
                        if (thumb_x < track_x) thumb_x = track_x;
                        if (thumb_x + thumb_w > track_x + track_w) thumb_x = track_x + track_w - thumb_w;

                        if (mx >= thumb_x && mx <= thumb_x + thumb_w) {
                            /* Clicked directly inside thumb */
                            g_save_dlg_data->is_dragging_scroll = true;
                            g_save_dlg_data->drag_start_mx = mx;
                            g_save_dlg_data->drag_start_scroll_x = g_save_dlg_data->asset_scroll_x;
                            SetCapture(hwnd);
                            InvalidateRect(hwnd, NULL, FALSE);
                            return 0;
                        } else {
                            /* Clicked on track: jump thumb center to click position */
                            int click_rel = mx - track_x - thumb_w / 2;
                            float pct = (scrollable_track_w > 0) ? ((float)click_rel / (float)scrollable_track_w) : 0.0f;
                            if (pct < 0.0f) pct = 0.0f;
                            if (pct > 1.0f) pct = 1.0f;
                            g_save_dlg_data->asset_scroll_x = (int)(pct * max_scroll);
                            g_save_dlg_data->is_dragging_scroll = true;
                            g_save_dlg_data->drag_start_mx = mx;
                            g_save_dlg_data->drag_start_scroll_x = g_save_dlg_data->asset_scroll_x;
                            SetCapture(hwnd);
                            InvalidateRect(hwnd, NULL, FALSE);
                            return 0;
                        }
                    }
                }

                /* 2. Check Card Selection (inside card area, above scrollbar) */
                if (my >= strip_y && my < strip_y + 72 && mx >= strip_x && mx <= strip_x + strip_w) {
                    int rel_x = mx - strip_x - 4 + g_save_dlg_data->asset_scroll_x;
                    int hit = rel_x / (card_w + card_gap);
                    if (hit >= 0 && hit < g_save_dlg_data->plan.count) {
                        g_save_dlg_data->selected_asset_idx = hit;
                        sync_save_dlg_asset_controls(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
            }
            break;
        }

        case WM_LBUTTONUP: {
            if (g_save_dlg_data && g_save_dlg_data->is_dragging_scroll) {
                g_save_dlg_data->is_dragging_scroll = false;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;
        }

        case WM_CAPTURECHANGED: {
            if (g_save_dlg_data && g_save_dlg_data->is_dragging_scroll) {
                g_save_dlg_data->is_dragging_scroll = false;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_SETCURSOR: {
            if (g_save_dlg_data && (g_save_dlg_data->is_dragging_scroll || g_save_dlg_data->is_hovering_thumb)) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return TRUE;
            }
            break;
        }

        case WM_MOUSEMOVE: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int strip_x = 24;
            int strip_y = 58;
            int strip_w = rc.right - 48;
            int strip_h = 86;

            if (g_save_dlg_data && g_save_dlg_data->plan.count > 0) {
                int card_w = 68;
                int card_gap = 6;
                int total_cards_w = g_save_dlg_data->plan.count * (card_w + card_gap) - card_gap + 8;
                bool has_overflow = (total_cards_w > strip_w);

                /* Handle scroll dragging */
                if (g_save_dlg_data->is_dragging_scroll && has_overflow) {
                    int track_w = strip_w - 16;
                    int max_scroll = total_cards_w - strip_w;
                    float view_ratio = (float)strip_w / (float)total_cards_w;
                    int thumb_w = (int)(track_w * view_ratio);
                    if (thumb_w < 36) thumb_w = 36;
                    if (thumb_w > track_w) thumb_w = track_w;
                    int scrollable_track_w = track_w - thumb_w;

                    if (scrollable_track_w > 0) {
                        int dx = mx - g_save_dlg_data->drag_start_mx;
                        float scroll_per_pixel = (float)max_scroll / (float)scrollable_track_w;
                        int new_scroll = g_save_dlg_data->drag_start_scroll_x + (int)(dx * scroll_per_pixel);
                        if (new_scroll < 0) new_scroll = 0;
                        if (new_scroll > max_scroll) new_scroll = max_scroll;
                        if (g_save_dlg_data->asset_scroll_x != new_scroll) {
                            g_save_dlg_data->asset_scroll_x = new_scroll;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                    return 0;
                }

                /* Check thumb hover */
                bool new_hover_thumb = false;
                if (has_overflow) {
                    int track_x = strip_x + 8;
                    int track_y = strip_y + 74;
                    int track_w = strip_w - 16;
                    int track_h = 6;
                    int max_scroll = total_cards_w - strip_w;
                    float view_ratio = (float)strip_w / (float)total_cards_w;
                    int thumb_w = (int)(track_w * view_ratio);
                    if (thumb_w < 36) thumb_w = 36;
                    if (thumb_w > track_w) thumb_w = track_w;
                    int scrollable_track_w = track_w - thumb_w;

                    int thumb_x = track_x;
                    if (max_scroll > 0 && scrollable_track_w > 0) {
                        thumb_x = track_x + (int)((float)g_save_dlg_data->asset_scroll_x / (float)max_scroll * scrollable_track_w);
                    }
                    if (thumb_x < track_x) thumb_x = track_x;
                    if (thumb_x + thumb_w > track_x + track_w) thumb_x = track_x + track_w - thumb_w;

                    if (my >= track_y - 3 && my <= track_y + track_h + 3 && mx >= thumb_x && mx <= thumb_x + thumb_w) {
                        new_hover_thumb = true;
                    }
                }
                if (g_save_dlg_data->is_hovering_thumb != new_hover_thumb) {
                    g_save_dlg_data->is_hovering_thumb = new_hover_thumb;
                    InvalidateRect(hwnd, NULL, FALSE);
                }

                /* Check card hover */
                int new_hover = -1;
                if (my >= strip_y && my < strip_y + 72 && mx >= strip_x && mx <= strip_x + strip_w) {
                    int rel_x = mx - strip_x - 4 + g_save_dlg_data->asset_scroll_x;
                    int hit = rel_x / (card_w + card_gap);
                    if (hit >= 0 && hit < g_save_dlg_data->plan.count) {
                        new_hover = hit;
                    }
                }
                if (g_save_dlg_data->hovered_asset_idx != new_hover) {
                    g_save_dlg_data->hovered_asset_idx = new_hover;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            break;
        }

        case WM_MOUSEWHEEL: {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            ScreenToClient(hwnd, &pt);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int strip_x = 24;
            int strip_y = 58;
            int strip_w = rc.right - 48;
            int strip_h = 86;

            if (pt.y >= strip_y && pt.y <= strip_y + strip_h && pt.x >= strip_x && pt.x <= strip_x + strip_w) {
                if (g_save_dlg_data && g_save_dlg_data->plan.count > 0) {
                    int card_w = 68;
                    int card_gap = 6;
                    int total_cards_w = g_save_dlg_data->plan.count * (card_w + card_gap) - card_gap + 8;
                    int max_s = (total_cards_w > strip_w) ? (total_cards_w - strip_w) : 0;
                    short delta = (short)HIWORD(wParam);
                    g_save_dlg_data->asset_scroll_x -= (delta / WHEEL_DELTA) * (card_w + card_gap);
                    if (g_save_dlg_data->asset_scroll_x < 0) g_save_dlg_data->asset_scroll_x = 0;
                    if (g_save_dlg_data->asset_scroll_x > max_s) g_save_dlg_data->asset_scroll_x = max_s;
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            break;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC hdc_ctl = (HDC)wParam;
            HWND hwnd_ctl = (HWND)lParam;
            if (g_save_dlg_data && g_save_dlg_data->hwnd_lbl_orig_ext && hwnd_ctl == g_save_dlg_data->hwnd_lbl_orig_ext) {
                SetTextColor(hdc_ctl, th->text_secondary);
                SetBkColor(hdc_ctl, th->bar_bg);
                static HBRUSH s_lbl_br_light = NULL;
                static HBRUSH s_lbl_br_dark = NULL;
                if (!s_lbl_br_light) s_lbl_br_light = CreateSolidBrush(g_theme_light.bar_bg);
                if (!s_lbl_br_dark)  s_lbl_br_dark  = CreateSolidBrush(g_theme_dark.bar_bg);
                return (LRESULT)((g_current_theme == THEME_DARK) ? s_lbl_br_dark : s_lbl_br_light);
            }
            if (g_save_dlg_data && g_save_dlg_data->hwnd_lbl_prog_id && hwnd_ctl == g_save_dlg_data->hwnd_lbl_prog_id) {
                SetTextColor(hdc_ctl, th->text_accent_blue);
                SetBkColor(hdc_ctl, th->bar_bg);
                static HBRUSH s_prog_br_light = NULL;
                static HBRUSH s_prog_br_dark = NULL;
                if (!s_prog_br_light) s_prog_br_light = CreateSolidBrush(g_theme_light.bar_bg);
                if (!s_prog_br_dark)  s_prog_br_dark  = CreateSolidBrush(g_theme_dark.bar_bg);
                return (LRESULT)((g_current_theme == THEME_DARK) ? s_prog_br_dark : s_prog_br_light);
            }
            SetTextColor(hdc_ctl, th->text_primary);
            SetBkColor(hdc_ctl, th->bar_bg);
            static HBRUSH s_bar_br_light = NULL;
            static HBRUSH s_bar_br_dark = NULL;
            if (!s_bar_br_light) s_bar_br_light = CreateSolidBrush(g_theme_light.bar_bg);
            if (!s_bar_br_dark)  s_bar_br_dark  = CreateSolidBrush(g_theme_dark.bar_bg);
            return (LRESULT)((g_current_theme == THEME_DARK) ? s_bar_br_dark : s_bar_br_light);
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdc_ctl = (HDC)wParam;
            COLORREF edit_bg = (g_current_theme == THEME_DARK) ? RGB(36, 40, 48) : RGB(255, 255, 255);
            SetTextColor(hdc_ctl, th->text_primary);
            SetBkColor(hdc_ctl, edit_bg);
            static HBRUSH s_combo_br_light = NULL;
            static HBRUSH s_combo_br_dark = NULL;
            if (!s_combo_br_light) s_combo_br_light = CreateSolidBrush(RGB(255, 255, 255));
            if (!s_combo_br_dark)  s_combo_br_dark  = CreateSolidBrush(RGB(36, 40, 48));
            return (LRESULT)((g_current_theme == THEME_DARK) ? s_combo_br_dark : s_combo_br_light);
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc_ctl = (HDC)wParam;
            HWND hwnd_ctl = (HWND)lParam;
            if (g_save_dlg_data && (hwnd_ctl == g_save_dlg_data->hwnd_name || hwnd_ctl == g_save_dlg_data->hwnd_title)) {
                COLORREF edit_bg = (g_current_theme == THEME_DARK) ? RGB(36, 40, 48) : RGB(255, 255, 255);
                SetTextColor(hdc_ctl, th->text_primary);
                SetBkColor(hdc_ctl, edit_bg);
                static HBRUSH s_edit_br_light = NULL;
                static HBRUSH s_edit_br_dark = NULL;
                if (!s_edit_br_light) s_edit_br_light = CreateSolidBrush(RGB(255, 255, 255));
                if (!s_edit_br_dark)  s_edit_br_dark  = CreateSolidBrush(RGB(36, 40, 48));
                return (LRESULT)((g_current_theme == THEME_DARK) ? s_edit_br_dark : s_edit_br_light);
            }
            break;
        }

        case WM_MEASUREITEM: {
            LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lParam;
            if (mis->CtlType == ODT_COMBOBOX && mis->CtlID == ID_SAVE_COMBO_DIFF) {
                mis->itemHeight = 24;
                return TRUE;
            }
            break;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_COMBOBOX && dis->CtlID == ID_SAVE_COMBO_DIFF) {
                if (dis->itemID == (UINT)-1) return TRUE;
                bool is_dark = (g_current_theme == THEME_DARK);
                bool is_sel = (dis->itemState & ODS_SELECTED) != 0;

                COLORREF bg_col;
                if (is_sel) {
                    bg_col = is_dark ? RGB(45, 60, 85) : RGB(215, 230, 252);
                } else {
                    bg_col = is_dark ? RGB(32, 36, 44) : RGB(255, 255, 255);
                }
                HBRUSH br = CreateSolidBrush(bg_col);
                FillRect(dis->hDC, &dis->rcItem, br);
                DeleteObject(br);

                char item_text[64] = {0};
                SendMessageA(dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LPARAM)item_text);

                bool is_dev = (dis->itemID == 0 || strstr(item_text, "Dev-Test") != NULL);
                COLORREF text_col;
                if (is_dev) {
                    text_col = is_dark ? RGB(255, 175, 45) : RGB(210, 100, 0);
                } else {
                    text_col = is_dark ? RGB(225, 230, 240) : RGB(30, 35, 45);
                }

                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, text_col);
                HFONT f = is_dev ? (g_font_primary_bold ? g_font_primary_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT))
                                 : (g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                HFONT old_f = (HFONT)SelectObject(dis->hDC, f);

                RECT rc_t = dis->rcItem;
                rc_t.left += 8;
                DrawTextA(dis->hDC, item_text, -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                SelectObject(dis->hDC, old_f);
                return TRUE;
            }
            if (dis->CtlType == ODT_BUTTON) {
                bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool is_save = (dis->CtlID == ID_SAVE_BTN_SAVE);

                COLORREF bg_color = is_save ? (is_pressed ? RGB(18, 70, 140) : th->text_accent_blue)
                                            : (is_pressed ? th->btn_bg_pressed : th->btn_bg);
                HBRUSH bg_brush = CreateSolidBrush(bg_color);
                FillRect(dis->hDC, &dis->rcItem, bg_brush);
                DeleteObject(bg_brush);

                COLORREF border_col = is_save ? th->text_accent_blue : th->btn_border;
                HPEN pen = CreatePen(PS_SOLID, 1, border_col);
                HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, old_pen);
                DeleteObject(pen);

                char text[64] = {0};
                GetWindowTextA(dis->hwndItem, text, sizeof(text));

                SetBkMode(dis->hDC, TRANSPARENT);
                COLORREF text_col = is_save ? RGB(255, 255, 255) : th->btn_text;
                SetTextColor(dis->hDC, text_col);
                SelectObject(dis->hDC, is_save ? g_font_primary_bold : g_font_primary_regular);

                RECT rc_t = dis->rcItem;
                if (is_pressed) { rc_t.left += 1; rc_t.top += 1; }
                DrawTextA(dis->hDC, text, -1, &rc_t, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (!g_save_dlg_data) break;

            if (cmd == ID_SAVE_CHK_ORIG) {
                if (g_save_dlg_data->selected_asset_idx >= 0 && g_save_dlg_data->selected_asset_idx < g_save_dlg_data->plan.count) {
                    SaveCanvasAssetItem *item = &g_save_dlg_data->plan.items[g_save_dlg_data->selected_asset_idx];
                    if (!item->can_keep_orig) {
                        /* Uneditable: force unchecked */
                        item->keep_orig = false;
                        g_save_dlg_data->chk_orig_checked = false;
                        CheckDlgButton(hwnd, ID_SAVE_CHK_ORIG, BST_UNCHECKED);
                        sync_save_dlg_asset_controls(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    bool is_chk = (IsDlgButtonChecked(hwnd, ID_SAVE_CHK_ORIG) == BST_CHECKED);
                    g_save_dlg_data->chk_orig_checked = is_chk;
                    item->keep_orig = is_chk;
                    if (is_chk) {
                        if (_stricmp(item->orig_ext, "webp") == 0) {
                            show_webp_patch_warning(hwnd);
                        }
                    } else {
                        if (item->selected_format == IMG_EXPORT_WEBP) {
                            show_webp_patch_warning(hwnd);
                        }
                    }
                }
                sync_save_dlg_asset_controls(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (cmd == ID_SAVE_COMBO_FORMAT && HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = (int)SendMessage(g_save_dlg_data->hwnd_combo_fmt, CB_GETCURSEL, 0, 0);
                if (g_save_dlg_data->selected_asset_idx >= 0 && g_save_dlg_data->selected_asset_idx < g_save_dlg_data->plan.count) {
                    SaveCanvasAssetItem *item = &g_save_dlg_data->plan.items[g_save_dlg_data->selected_asset_idx];
                    if (sel == 0) item->selected_format = IMG_EXPORT_WEBP;
                    else if (sel == 1) item->selected_format = IMG_EXPORT_PNG;
                    else if (sel == 2) item->selected_format = IMG_EXPORT_JPG;
                    else if (sel == 3) item->selected_format = IMG_EXPORT_GIF;

                    if (item->selected_format == IMG_EXPORT_WEBP) {
                        show_webp_patch_warning(hwnd);
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (cmd == ID_SAVE_COMBO_DIFF && HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = (int)SendMessage(g_save_dlg_data->hwnd_combo_diff, CB_GETCURSEL, 0, 0);
                LevelDifficulty new_diff = LEVEL_DIFF_MEDIUM;
                if (sel == 0) new_diff = LEVEL_DIFF_DEV_TEST;
                else if (sel == 1) new_diff = LEVEL_DIFF_EASY;
                else if (sel == 2) new_diff = LEVEL_DIFF_MEDIUM;
                else if (sel == 3) new_diff = LEVEL_DIFF_HARD;
                else if (sel == 4) new_diff = LEVEL_DIFF_EXTREME;
                g_save_dlg_data->selected_diff = new_diff;

                int actual_paths = (g_path_count > 0 && g_path_count <= 10) ? g_path_count : 1;
                char p_id[64];
                get_level_progression_id(actual_paths, new_diff, p_id, sizeof(p_id));
                SetWindowTextA(g_save_dlg_data->hwnd_lbl_prog_id, p_id);

                if (g_save_dlg_data->hwnd_tooltip) {
                    TOOLINFOA ti;
                    memset(&ti, 0, sizeof(ti));
                    ti.cbSize = sizeof(ti);
                    ti.hwnd = hwnd;
                    ti.uId = (UINT_PTR)g_save_dlg_data->hwnd_combo_diff;
                    ti.lpszText = (LPSTR)level_difficulty_get_description(new_diff);
                    SendMessageA(g_save_dlg_data->hwnd_tooltip, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
                }
            } else if (cmd == ID_SAVE_EDIT_TITLE && HIWORD(wParam) == EN_CHANGE) {
                static bool s_in_save_title_sync = false;
                if (!s_in_save_title_sync && g_save_dlg_data && g_save_dlg_data->hwnd_title) {
                    s_in_save_title_sync = true;
                    char buf[128] = {0};
                    GetWindowTextA(g_save_dlg_data->hwnd_title, buf, sizeof(buf));
                    sync_canvas_title_everywhere(buf, g_save_dlg_data->hwnd_title);
                    s_in_save_title_sync = false;
                }
            } else if (cmd == ID_SAVE_BTN_SAVE) {
                g_level_difficulty = g_save_dlg_data->selected_diff;
                char raw_name[128] = {0};
                char raw_title[128] = {0};
                GetWindowTextA(g_save_dlg_data->hwnd_name, raw_name, sizeof(raw_name));
                GetWindowTextA(g_save_dlg_data->hwnd_title, raw_title, sizeof(raw_title));

                /* Trim whitespace for name */
                char *s_name = raw_name;
                while (*s_name == ' ' || *s_name == '\t' || *s_name == '\r' || *s_name == '\n') s_name++;
                char *e_name = s_name + strlen(s_name);
                while (e_name > s_name && (*(e_name - 1) == ' ' || *(e_name - 1) == '\t' || *(e_name - 1) == '\r' || *(e_name - 1) == '\n')) e_name--;
                *e_name = '\0';

                if (*s_name == '\0') {
                    show_themed_notice_dialog(hwnd, "Validation Error", "Invalid Canvas Name",
                        "Canvas Name (Folder / Level ID) cannot be empty.\nPlease enter a valid name using lowercase letters (a-z), numbers, '_' or '-'.", 480, 250);
                    SetFocus(g_save_dlg_data->hwnd_name);
                    break;
                }

                if (*s_name >= '0' && *s_name <= '9') {
                    show_themed_notice_dialog(hwnd, "Zuma Deluxe Warning", "Level Name Cannot Start with a Number",
                        "Zuma Deluxe level IDs must start with a letter (a-z), not a number.\n\n"
                        "Starting with a number causes Zuma's difficulty parser (atol) to confuse the level with a numerical difficulty index and crash.\n\n"
                        "Please prefix your level name with letters (e.g. 'lvl_' or 'stage_').", 540, 310);
                    SetFocus(g_save_dlg_data->hwnd_name);
                    break;
                }

                char name_buf[128] = {0};
                strncpy(name_buf, s_name, sizeof(name_buf) - 1);

                /* Trim whitespace for title */
                char *s_title = raw_title;
                while (*s_title == ' ' || *s_title == '\t' || *s_title == '\r' || *s_title == '\n') s_title++;
                char *e_title = s_title + strlen(s_title);
                while (e_title > s_title && (*(e_title - 1) == ' ' || *(e_title - 1) == '\t' || *(e_title - 1) == '\r' || *(e_title - 1) == '\n')) e_title--;
                *e_title = '\0';

                char title_buf[128] = {0};
                if (*s_title != '\0') {
                    strncpy(title_buf, s_title, sizeof(title_buf) - 1);
                } else {
                    strncpy(title_buf, name_buf, sizeof(title_buf) - 1);
                }

                /* Multi-asset validation across all detected canvas assets */
                bool has_webp = false;
                char flatten_assets[1024] = {0};

                for (int k = 0; k < g_save_dlg_data->plan.count; ++k) {
                    SaveCanvasAssetItem *it = &g_save_dlg_data->plan.items[k];
                    ImageExportFormat it_fmt = it->keep_orig ? IMG_EXPORT_KEEP_ORIGINAL : it->selected_format;
                    if (it_fmt == IMG_EXPORT_WEBP || (it_fmt == IMG_EXPORT_KEEP_ORIGINAL && _stricmp(it->orig_ext, "webp") == 0)) {
                        has_webp = true;
                    }
                    bool is_multi = it->is_video || (it->is_animated && it->frame_count > 1);
                    bool is_static = (it_fmt == IMG_EXPORT_PNG || it_fmt == IMG_EXPORT_JPG || it_fmt == IMG_EXPORT_BMP || (it_fmt == IMG_EXPORT_GIF && it->is_video));
                    if (is_multi && is_static) {
                        char line[128];
                        snprintf(line, sizeof(line), "- %s (%s)\n", it->label, it->id_name);
                        strncat(flatten_assets, line, sizeof(flatten_assets) - strlen(flatten_assets) - 1);
                    }
                }

                /* Trigger warnings if needed */
                if (flatten_assets[0] != '\0') {
                    if (!show_animation_flatten_warning(hwnd, flatten_assets, true)) {
                        break;
                    }
                }
                if (has_webp) {
                    if (!show_webp_patch_warning_ex(hwnd, true)) {
                        break;
                    }
                    if (!check_and_show_webp_limit_warning(hwnd, &g_save_dlg_data->plan, true)) {
                        break;
                    }
                }

                compact_empty_paths();
                if (g_path_count >= 4) {
                    if (!show_extra_paths_warning_ex(hwnd, g_path_count, true)) {
                        break;
                    }
                }

                if (g_map_is_space) {
                    const char *space_note =
                        "Enabling space=\"true\" converts this level into a procedural space arena in Zuma Deluxe.\r\n\r\n"
                        "CRITICAL: The following attributes and visual layers will be IGNORED or overridden by the Zuma game engine:\r\n\r\n"
                        " * Background Graphic (image): Completely ignored. Zuma replaces it with a procedural field of 1,000 3D stars and a scrolling nebula.\r\n"
                        " * Background Color (bgcolor): Ignored. Zuma forces the cosmic space void.\r\n"
                        " * Hue Rotation (huerot): Ignored for background rendering.\r\n"
                        " * BackgroundAlpha Cutouts: Static atmospheric and earthquake layers will not render against the space starfield.\r\n"
                        " * Cutout Layers: May float awkwardly over dynamic space particles.\r\n"
                        " * Sprite Layers: Ignored or may float awkwardly over dynamic space particles.\r\n\r\n"
                        "Do you want to proceed with saving this Space canvas project?";
                    int res = show_themed_choice_dialog_ex(hwnd, "Space Mode Warning", "Procedural Space Arena Activated", space_note, "Save Level", "Cancel", NULL, 600, 420);
                    if (res != 1) {
                        break;
                    }
                }

                SaveCanvasAssetPlan final_plan = g_save_dlg_data->plan;
                DestroyWindow(hwnd);
                show_saving_canvas_modal(g_hwnd_main, name_buf, title_buf, &final_plan);
            } else if (cmd == ID_SAVE_BTN_CANCEL) {
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_TIMER:
            if (wParam == 9921) {
                KillTimer(hwnd, 9921);
                hide_edit_warning_tooltip();
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            destroy_edit_warning_tooltip();
            if (g_active_modal_dlg == hwnd) g_active_modal_dlg = NULL;
            if (g_save_dlg_data) {
                if (g_save_dlg_data->hwnd_tooltip && IsWindow(g_save_dlg_data->hwnd_tooltip)) {
                    DestroyWindow(g_save_dlg_data->hwnd_tooltip);
                }
                free(g_save_dlg_data);
                g_save_dlg_data = NULL;
            }
            g_orig_save_chk_proc = NULL;
            g_orig_save_combo_proc = NULL;
            g_orig_save_combo_diff_proc = NULL;
            g_orig_save_combo_diff_list_proc = NULL;
            g_last_diff_hover_idx = -1;
            EnableWindow(g_hwnd_main, TRUE);
            SetFocus(g_hwnd_main);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ========================================================================= */
/* Saving Canvas Progress Modal Dialog                                       */
/* ========================================================================= */
#define WM_SAVE_PROGRESS (WM_USER + 101)
#define WM_SAVE_DONE     (WM_USER + 102)

typedef struct {
    char name[128];
    char title[128];
    SaveCanvasAssetPlan plan;
    HWND hwnd_dlg;
    HWND hwnd_parent;
} SaveWorkerParams;

typedef struct {
    HWND hwnd_dlg;
    HWND hwnd_parent;
    char status_text[256];
    int progress_pct; /* 0..100, or -1 for indeterminate marquee */
    int anim_tick;
    bool is_done;
    bool is_dark;
    bool save_success;
} SaveProgressModalState;

static SaveProgressModalState *g_saving_modal_state = NULL;

static void on_save_progress_callback(const char *status_msg, int pct) {
    if (g_saving_modal_state && g_saving_modal_state->hwnd_dlg && IsWindow(g_saving_modal_state->hwnd_dlg)) {
        SendMessageA(g_saving_modal_state->hwnd_dlg, WM_SAVE_PROGRESS, (WPARAM)pct, (LPARAM)status_msg);
    }
}

static DWORD WINAPI SaveWorkerThread(LPVOID lpParam) {
    SaveWorkerParams *params = (SaveWorkerParams*)lpParam;
    if (!params) return 0;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    set_save_progress_handler(on_save_progress_callback);

    bool ok = execute_save_canvas_with_plan(params->hwnd_parent, params->name, params->title, &params->plan);

    set_save_progress_handler(NULL);
    CoUninitialize();

    if (params->hwnd_dlg && IsWindow(params->hwnd_dlg)) {
        PostMessageA(params->hwnd_dlg, WM_SAVE_DONE, ok ? 100 : 0, 0);
    }

    free(params);
    return 0;
}

static LRESULT CALLBACK SaveProgressModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();

    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, 1, 30, NULL); /* 30ms animation ticker */
            return 0;

        case WM_TIMER:
            if (wParam == 1) {
                if (g_saving_modal_state) {
                    g_saving_modal_state->anim_tick++;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else if (wParam == 2) {
                KillTimer(hwnd, 2);
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_SAVE_PROGRESS: {
            if (g_saving_modal_state) {
                if (lParam) {
                    strncpy(g_saving_modal_state->status_text, (const char*)lParam, sizeof(g_saving_modal_state->status_text) - 1);
                    g_saving_modal_state->status_text[sizeof(g_saving_modal_state->status_text) - 1] = '\0';
                }
                g_saving_modal_state->progress_pct = (int)wParam;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_SAVE_DONE: {
            if (g_saving_modal_state) {
                g_saving_modal_state->is_done = true;
                g_saving_modal_state->save_success = (wParam == 100);
                if (g_saving_modal_state->save_success) {
                    g_saving_modal_state->progress_pct = 100;
                    strncpy(g_saving_modal_state->status_text, "Canvas Project saved successfully!", sizeof(g_saving_modal_state->status_text) - 1);
                    SetTimer(hwnd, 2, 750, NULL); /* Auto close after 750ms */
                } else {
                    strncpy(g_saving_modal_state->status_text, "Error saving canvas project.", sizeof(g_saving_modal_state->status_text) - 1);
                    SetTimer(hwnd, 2, 2000, NULL);
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            bool is_dark = (g_current_theme == THEME_DARK);
            COLORREF bg_col = th->bar_bg;
            COLORREF border_col = th->bar_border;
            COLORREF text_pri = th->text_primary;
            COLORREF text_sec = th->text_secondary;
            COLORREF bar_track = is_dark ? RGB(36, 40, 48) : RGB(218, 222, 230);
            COLORREF bar_fill  = is_dark ? RGB(97, 175, 239) : RGB(24, 90, 175);
            COLORREF done_fill = is_dark ? RGB(152, 195, 121) : RGB(40, 167, 69);

            /* Double buffering */
            HDC mem_dc = CreateCompatibleDC(hdc);
            HBITMAP mem_bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP old_bm = (HBITMAP)SelectObject(mem_dc, mem_bm);

            HBRUSH bg_br = CreateSolidBrush(bg_col);
            FillRect(mem_dc, &rc, bg_br);
            DeleteObject(bg_br);

            HPEN bpen = CreatePen(PS_SOLID, 1, border_col);
            HPEN old_pen = (HPEN)SelectObject(mem_dc, bpen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, 0, 0, rc.right, rc.bottom);
            SelectObject(mem_dc, old_pen);
            DeleteObject(bpen);

            /* Header Title */
            SetBkMode(mem_dc, TRANSPARENT);
            SetTextColor(mem_dc, text_pri);
            HFONT hfont_hdr = g_font_primary_bold ? g_font_primary_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT old_font = (HFONT)SelectObject(mem_dc, hfont_hdr);

            RECT rc_hdr = { 28, 18, rc.right - 28, 42 };
            const char *hdr_text = (g_saving_modal_state && g_saving_modal_state->is_done)
                ? (g_saving_modal_state->save_success ? "Save Completed" : "Save Error")
                : "Saving Canvas Project...";
            DrawTextA(mem_dc, hdr_text, -1, &rc_hdr, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

            /* Status Text */
            SetTextColor(mem_dc, text_sec);
            HFONT hfont_reg = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            SelectObject(mem_dc, hfont_reg);
            RECT rc_msg = { 28, 46, rc.right - 28, 76 };
            const char *msg_str = (g_saving_modal_state && g_saving_modal_state->status_text[0])
                ? g_saving_modal_state->status_text
                : "Please wait while assets and paths are being saved...";
            DrawTextA(mem_dc, msg_str, -1, &rc_msg, DT_WORDBREAK | DT_LEFT);

            /* Progress Bar */
            RECT rc_bar = { 28, 86, rc.right - 28, 106 };
            HBRUSH tr_br = CreateSolidBrush(bar_track);
            FillRect(mem_dc, &rc_bar, tr_br);
            DeleteObject(tr_br);

            HPEN tr_pen = CreatePen(PS_SOLID, 1, border_col);
            old_pen = (HPEN)SelectObject(mem_dc, tr_pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, rc_bar.left, rc_bar.top, rc_bar.right, rc_bar.bottom);
            SelectObject(mem_dc, old_pen);
            DeleteObject(tr_pen);

            int track_w = rc_bar.right - rc_bar.left - 2;

            if (g_saving_modal_state && g_saving_modal_state->is_done) {
                if (g_saving_modal_state->save_success) {
                    HBRUSH d_br = CreateSolidBrush(done_fill);
                    RECT rc_full = { rc_bar.left + 1, rc_bar.top + 1, rc_bar.right - 1, rc_bar.bottom - 1 };
                    FillRect(mem_dc, &rc_full, d_br);
                    DeleteObject(d_br);
                }
            } else if (g_saving_modal_state && g_saving_modal_state->progress_pct >= 0) {
                int fill_w = (track_w * g_saving_modal_state->progress_pct) / 100;
                if (fill_w > 0) {
                    HBRUSH f_br = CreateSolidBrush(bar_fill);
                    RECT rc_f = { rc_bar.left + 1, rc_bar.top + 1, rc_bar.left + 1 + fill_w, rc_bar.bottom - 1 };
                    FillRect(mem_dc, &rc_f, f_br);
                    DeleteObject(f_br);
                }
                char pct_str[16];
                snprintf(pct_str, sizeof(pct_str), "%d%%", g_saving_modal_state->progress_pct);
                SetTextColor(mem_dc, is_dark ? RGB(255, 255, 255) : RGB(20, 20, 20));
                HFONT old_f = (HFONT)SelectObject(mem_dc, g_font_mono_bold ? g_font_mono_bold : hfont_reg);
                DrawTextA(mem_dc, pct_str, -1, &rc_bar, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                SelectObject(mem_dc, old_f);
            } else {
                int block_w = 80;
                int tick = g_saving_modal_state ? g_saving_modal_state->anim_tick : 0;
                int span = track_w + block_w;
                int pos = (tick * 4) % span - block_w;

                int x1 = rc_bar.left + 1 + pos;
                int x2 = x1 + block_w;
                if (x1 < rc_bar.left + 1) x1 = rc_bar.left + 1;
                if (x2 > rc_bar.right - 1) x2 = rc_bar.right - 1;

                if (x2 > x1) {
                    HBRUSH f_br = CreateSolidBrush(bar_fill);
                    RECT rc_ind = { x1, rc_bar.top + 1, x2, rc_bar.bottom - 1 };
                    FillRect(mem_dc, &rc_ind, f_br);
                    DeleteObject(f_br);
                }
            }

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem_dc, 0, 0, SRCCOPY);
            SelectObject(mem_dc, old_font);
            SelectObject(mem_dc, old_bm);
            DeleteObject(mem_bm);
            DeleteDC(mem_dc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            if (g_saving_modal_state && !g_saving_modal_state->is_done) return 0;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            KillTimer(hwnd, 2);
            PostMessageA(hwnd, WM_NULL, 0, 0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void show_saving_canvas_modal(HWND parent_hwnd, const char *canvas_name, const char *canvas_title, const SaveCanvasAssetPlan *plan) {
    HINSTANCE hinst = GetModuleHandle(NULL);
    static bool s_class_registered = false;
    if (!s_class_registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = SaveProgressModalProc;
        wc.hInstance     = hinst;
        wc.lpszClassName = "ZuPatherSaveProgressModal";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        s_class_registered = true;
    }

    int dlg_w = 460;
    int dlg_h = 150;

    RECT rc_parent = {0};
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        GetWindowRect(parent_hwnd, &rc_parent);
    } else {
        GetWindowRect(GetDesktopWindow(), &rc_parent);
    }
    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;

    SaveProgressModalState state;
    memset(&state, 0, sizeof(state));
    state.progress_pct = 0;
    state.is_done = false;
    state.save_success = false;
    strncpy(state.status_text, "Initializing canvas export...", sizeof(state.status_text) - 1);
    state.hwnd_parent = parent_hwnd;
    g_saving_modal_state = &state;

    HWND prev_modal = g_active_modal_dlg;
    BOOL parent_was_enabled = TRUE;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        parent_was_enabled = IsWindowEnabled(parent_hwnd);
        if (parent_was_enabled) {
            EnableWindow(parent_hwnd, FALSE);
        }
    }

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherSaveProgressModal", "Saving Canvas Project",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, NULL
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    state.hwnd_dlg = hwnd_dlg;

    SaveWorkerParams *params = (SaveWorkerParams*)calloc(1, sizeof(SaveWorkerParams));
    if (params) {
        strncpy(params->name, canvas_name ? canvas_name : "Untitled", sizeof(params->name) - 1);
        strncpy(params->title, canvas_title ? canvas_title : params->name, sizeof(params->title) - 1);
        if (plan) {
            params->plan = *plan;
        }
        params->hwnd_dlg = hwnd_dlg;
        params->hwnd_parent = parent_hwnd;
    }

    undo_ensure_baseline();

    HANDLE h_thread = CreateThread(NULL, 0, SaveWorkerThread, params, 0, NULL);

    MSG msg;
    while (IsWindow(hwnd_dlg) && GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (h_thread) {
        WaitForSingleObject(h_thread, INFINITE);
        CloseHandle(h_thread);
    }

    if (state.save_success) {
        undo_snapshot_capture("Save Canvas");
    }

    g_saving_modal_state = NULL;
    g_active_modal_dlg = prev_modal;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        if (parent_was_enabled) {
            EnableWindow(parent_hwnd, TRUE);
        }
        SetForegroundWindow(parent_hwnd);
        SetFocus(parent_hwnd);
        undo_redo_update_menu(parent_hwnd);
        InvalidateRect(parent_hwnd, NULL, FALSE);
    }
}

/* ========================================================================= */
/* Opening Canvas Progress Modal Dialog                                      */
/* ========================================================================= */
#define WM_OPEN_PROGRESS (WM_USER + 103)
#define WM_OPEN_DONE     (WM_USER + 104)

typedef struct {
    char folder_path[MAX_PATH];
    HWND hwnd_dlg;
    HWND hwnd_parent;
} OpenWorkerParams;

typedef struct {
    HWND hwnd_dlg;
    HWND hwnd_parent;
    char status_text[256];
    int progress_pct; /* 0..100 */
    int anim_tick;
    bool is_done;
    bool is_dark;
    bool open_success;
} OpenProgressModalState;

static OpenProgressModalState *g_opening_modal_state = NULL;

static void on_open_progress_callback(const char *status_msg, int pct) {
    if (g_opening_modal_state && g_opening_modal_state->hwnd_dlg && IsWindow(g_opening_modal_state->hwnd_dlg)) {
        SendMessageA(g_opening_modal_state->hwnd_dlg, WM_OPEN_PROGRESS, (WPARAM)pct, (LPARAM)status_msg);
    }
}

static DWORD WINAPI OpenWorkerThread(LPVOID lpParam) {
    OpenWorkerParams *params = (OpenWorkerParams*)lpParam;
    if (!params) return 0;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    set_open_progress_handler(on_open_progress_callback);

    bool ok = execute_open_canvas_folder(params->hwnd_parent, params->folder_path);

    set_open_progress_handler(NULL);
    CoUninitialize();

    if (params->hwnd_dlg && IsWindow(params->hwnd_dlg)) {
        PostMessageA(params->hwnd_dlg, WM_OPEN_DONE, ok ? 100 : 0, 0);
    }

    free(params);
    return 0;
}

static LRESULT CALLBACK OpenProgressModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();

    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, 1, 30, NULL); /* 30ms animation ticker */
            return 0;

        case WM_TIMER:
            if (wParam == 1) {
                if (g_opening_modal_state) {
                    g_opening_modal_state->anim_tick++;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else if (wParam == 2) {
                KillTimer(hwnd, 2);
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_OPEN_PROGRESS: {
            if (g_opening_modal_state) {
                if (lParam) {
                    strncpy(g_opening_modal_state->status_text, (const char*)lParam, sizeof(g_opening_modal_state->status_text) - 1);
                    g_opening_modal_state->status_text[sizeof(g_opening_modal_state->status_text) - 1] = '\0';
                }
                g_opening_modal_state->progress_pct = (int)wParam;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_OPEN_DONE: {
            if (g_opening_modal_state) {
                g_opening_modal_state->is_done = true;
                g_opening_modal_state->open_success = (wParam == 100);
                if (g_opening_modal_state->open_success) {
                    g_opening_modal_state->progress_pct = 100;
                    strncpy(g_opening_modal_state->status_text, "Canvas Project loaded successfully!", sizeof(g_opening_modal_state->status_text) - 1);
                    SetTimer(hwnd, 2, 600, NULL); /* Auto close after 600ms */
                } else {
                    strncpy(g_opening_modal_state->status_text, "Error loading canvas project.", sizeof(g_opening_modal_state->status_text) - 1);
                    SetTimer(hwnd, 2, 2000, NULL);
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            bool is_dark = (g_current_theme == THEME_DARK);
            COLORREF bg_col = th->bar_bg;
            COLORREF border_col = th->bar_border;
            COLORREF text_pri = th->text_primary;
            COLORREF text_sec = th->text_secondary;
            COLORREF bar_track = is_dark ? RGB(36, 40, 48) : RGB(218, 222, 230);
            COLORREF bar_fill  = is_dark ? RGB(97, 175, 239) : RGB(24, 90, 175);
            COLORREF done_fill = is_dark ? RGB(152, 195, 121) : RGB(40, 167, 69);

            /* Double buffering */
            HDC mem_dc = CreateCompatibleDC(hdc);
            HBITMAP mem_bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP old_bm = (HBITMAP)SelectObject(mem_dc, mem_bm);

            HBRUSH bg_br = CreateSolidBrush(bg_col);
            FillRect(mem_dc, &rc, bg_br);
            DeleteObject(bg_br);

            HPEN bpen = CreatePen(PS_SOLID, 1, border_col);
            HPEN old_pen = (HPEN)SelectObject(mem_dc, bpen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, 0, 0, rc.right, rc.bottom);
            SelectObject(mem_dc, old_pen);
            DeleteObject(bpen);

            /* Header Title */
            SetBkMode(mem_dc, TRANSPARENT);
            SetTextColor(mem_dc, text_pri);
            HFONT hfont_hdr = g_font_primary_bold ? g_font_primary_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT old_font = (HFONT)SelectObject(mem_dc, hfont_hdr);

            RECT rc_hdr = { 28, 18, rc.right - 28, 42 };
            const char *hdr_text = (g_opening_modal_state && g_opening_modal_state->is_done)
                ? (g_opening_modal_state->open_success ? "Open Completed" : "Open Error")
                : "Opening Canvas Project...";
            DrawTextA(mem_dc, hdr_text, -1, &rc_hdr, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

            /* Status Text */
            SetTextColor(mem_dc, text_sec);
            HFONT hfont_reg = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            SelectObject(mem_dc, hfont_reg);
            RECT rc_msg = { 28, 46, rc.right - 28, 76 };
            const char *msg_str = (g_opening_modal_state && g_opening_modal_state->status_text[0])
                ? g_opening_modal_state->status_text
                : "Please wait while canvas assets are loading...";
            DrawTextA(mem_dc, msg_str, -1, &rc_msg, DT_WORDBREAK | DT_LEFT);

            /* Progress Bar */
            RECT rc_bar = { 28, 86, rc.right - 28, 106 };
            HBRUSH tr_br = CreateSolidBrush(bar_track);
            FillRect(mem_dc, &rc_bar, tr_br);
            DeleteObject(tr_br);

            HPEN tr_pen = CreatePen(PS_SOLID, 1, border_col);
            old_pen = (HPEN)SelectObject(mem_dc, tr_pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, rc_bar.left, rc_bar.top, rc_bar.right, rc_bar.bottom);
            SelectObject(mem_dc, old_pen);
            DeleteObject(tr_pen);

            int track_w = rc_bar.right - rc_bar.left - 2;

            if (g_opening_modal_state && g_opening_modal_state->is_done) {
                if (g_opening_modal_state->open_success) {
                    HBRUSH d_br = CreateSolidBrush(done_fill);
                    RECT rc_full = { rc_bar.left + 1, rc_bar.top + 1, rc_bar.right - 1, rc_bar.bottom - 1 };
                    FillRect(mem_dc, &rc_full, d_br);
                    DeleteObject(d_br);
                }
            } else if (g_opening_modal_state && g_opening_modal_state->progress_pct >= 0) {
                int fill_w = (track_w * g_opening_modal_state->progress_pct) / 100;
                if (fill_w > 0) {
                    HBRUSH f_br = CreateSolidBrush(bar_fill);
                    RECT rc_f = { rc_bar.left + 1, rc_bar.top + 1, rc_bar.left + 1 + fill_w, rc_bar.bottom - 1 };
                    FillRect(mem_dc, &rc_f, f_br);
                    DeleteObject(f_br);
                }
                char pct_str[16];
                snprintf(pct_str, sizeof(pct_str), "%d%%", g_opening_modal_state->progress_pct);
                SetTextColor(mem_dc, is_dark ? RGB(255, 255, 255) : RGB(20, 20, 20));
                HFONT old_f = (HFONT)SelectObject(mem_dc, g_font_mono_bold ? g_font_mono_bold : hfont_reg);
                DrawTextA(mem_dc, pct_str, -1, &rc_bar, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                SelectObject(mem_dc, old_f);
            } else {
                int block_w = 80;
                int tick = g_opening_modal_state ? g_opening_modal_state->anim_tick : 0;
                int span = track_w + block_w;
                int pos = (tick * 4) % span - block_w;

                int x1 = rc_bar.left + 1 + pos;
                int x2 = x1 + block_w;
                if (x1 < rc_bar.left + 1) x1 = rc_bar.left + 1;
                if (x2 > rc_bar.right - 1) x2 = rc_bar.right - 1;

                if (x2 > x1) {
                    HBRUSH f_br = CreateSolidBrush(bar_fill);
                    RECT rc_ind = { x1, rc_bar.top + 1, x2, rc_bar.bottom - 1 };
                    FillRect(mem_dc, &rc_ind, f_br);
                    DeleteObject(f_br);
                }
            }

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem_dc, 0, 0, SRCCOPY);
            SelectObject(mem_dc, old_font);
            SelectObject(mem_dc, old_bm);
            DeleteObject(mem_bm);
            DeleteDC(mem_dc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            if (g_opening_modal_state && !g_opening_modal_state->is_done) return 0;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            KillTimer(hwnd, 2);
            PostMessageA(hwnd, WM_NULL, 0, 0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void show_opening_canvas_modal(HWND parent_hwnd, const char *folder_path) {
    HINSTANCE hinst = GetModuleHandle(NULL);
    static bool s_open_class_registered = false;
    if (!s_open_class_registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = OpenProgressModalProc;
        wc.hInstance     = hinst;
        wc.lpszClassName = "ZuPatherOpenProgressModal";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        s_open_class_registered = true;
    }

    int dlg_w = 460;
    int dlg_h = 150;

    RECT rc_parent = {0};
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        GetWindowRect(parent_hwnd, &rc_parent);
    } else {
        GetWindowRect(GetDesktopWindow(), &rc_parent);
    }
    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;

    OpenProgressModalState state;
    memset(&state, 0, sizeof(state));
    state.progress_pct = 0;
    state.is_done = false;
    state.open_success = false;
    strncpy(state.status_text, "Initializing canvas loader...", sizeof(state.status_text) - 1);
    state.hwnd_parent = parent_hwnd;
    g_opening_modal_state = &state;

    HWND prev_modal = g_active_modal_dlg;
    BOOL parent_was_enabled = TRUE;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        parent_was_enabled = IsWindowEnabled(parent_hwnd);
        if (parent_was_enabled) {
            EnableWindow(parent_hwnd, FALSE);
        }
    }

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherOpenProgressModal", "Opening Canvas Project",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, NULL
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    state.hwnd_dlg = hwnd_dlg;

    OpenWorkerParams *params = (OpenWorkerParams*)calloc(1, sizeof(OpenWorkerParams));
    if (params) {
        strncpy(params->folder_path, folder_path ? folder_path : "", sizeof(params->folder_path) - 1);
        params->hwnd_dlg = hwnd_dlg;
        params->hwnd_parent = parent_hwnd;
    }

    HANDLE h_thread = CreateThread(NULL, 0, OpenWorkerThread, params, 0, NULL);

    MSG msg;
    while (IsWindow(hwnd_dlg) && GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (h_thread) {
        WaitForSingleObject(h_thread, INFINITE);
        CloseHandle(h_thread);
    }

    if (state.open_success) {
        undo_reset();
        undo_ensure_baseline();
    }

    g_opening_modal_state = NULL;
    g_active_modal_dlg = prev_modal;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        if (parent_was_enabled) {
            EnableWindow(parent_hwnd, TRUE);
        }
        SetForegroundWindow(parent_hwnd);
        SetFocus(parent_hwnd);
        zoom_to_fit(parent_hwnd);
        update_status_bar();
        undo_redo_update_menu(parent_hwnd);
        InvalidateRect(parent_hwnd, NULL, TRUE);
    }
}

void show_save_canvas_dialog(HWND parent_hwnd) {
    if (!g_has_canvas) {
        show_themed_notice_dialog(parent_hwnd, "Notice", "No Active Canvas", "No active canvas to save. Please create or open a canvas first.", 460, 240);
        return;
    }

    if (g_save_dlg_data) {
        SetFocus(g_save_dlg_data->hwnd_dlg);
        return;
    }

    HINSTANCE hinst = GetModuleHandle(NULL);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = SaveCanvasModalProc;
    wc.hInstance     = hinst;
    wc.lpszClassName = "ZuPatherSaveCanvasModal";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    int client_w = 480;
    int client_h = 422;
    RECT rc_dlg = {0, 0, client_w, client_h};
    AdjustWindowRectEx(&rc_dlg, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int dlg_w = rc_dlg.right - rc_dlg.left;
    int dlg_h = rc_dlg.bottom - rc_dlg.top;

    RECT rc_parent;
    GetWindowRect(parent_hwnd, &rc_parent);
    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;

    g_save_dlg_data = (SaveCanvasDlgData*)calloc(1, sizeof(SaveCanvasDlgData));
    if (!g_save_dlg_data) return;

    /* Build multi-asset plan across all detected canvas images */
    build_save_canvas_asset_plan(&g_save_dlg_data->plan);
    g_save_dlg_data->selected_asset_idx = (g_save_dlg_data->plan.count > 0) ? 0 : -1;
    g_save_dlg_data->hovered_asset_idx = -1;
    g_save_dlg_data->asset_scroll_x = 0;

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherSaveCanvasModal", "Save Canvas Project",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, NULL
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    g_save_dlg_data->hwnd_dlg = hwnd_dlg;
    EnableWindow(parent_hwnd, FALSE);

    /* Canvas Name (sanitized to lowercase, no spaces default) */
    char init_save_name[128] = "level1";
    if (g_canvas_name[0]) {
        strncpy(init_save_name, g_canvas_name, sizeof(init_save_name) - 1);
        for (char *p = init_save_name; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
            else if (*p == ' ') *p = '_';
            else if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) *p = '_';
        }
    }
    g_save_dlg_data->hwnd_name = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", init_save_name,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        24, 190, 194, 26, hwnd_dlg, (HMENU)ID_SAVE_EDIT_NAME, hinst, NULL
    );
    SendMessage(g_save_dlg_data->hwnd_name, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);

    /* Ensure HUD Level Title and state are aligned first */
    if (g_edit_preview_title && IsWindow(g_edit_preview_title)) {
        char hud_buf[256] = {0};
        GetWindowTextA(g_edit_preview_title, hud_buf, sizeof(hud_buf));
        if (hud_buf[0] != '\0') {
            set_canvas_title_sanitized(hud_buf);
        }
    }

    /* Canvas Title (sanitized from XML breaking characters) */
    char initial_title[128] = "Level 1";
    const char *src_title = g_canvas_title[0] ? g_canvas_title : (g_canvas_name[0] ? g_canvas_name : "Level 1");
    strncpy(initial_title, src_title, sizeof(initial_title) - 1);
    for (char *p = initial_title; *p; ++p) {
        if (*p == '"' || *p == '<' || *p == '>' || *p == '&') *p = ' ';
    }
    g_save_dlg_data->hwnd_title = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", initial_title,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        236, 190, 220, 26, hwnd_dlg, (HMENU)ID_SAVE_EDIT_TITLE, hinst, NULL
    );
    SendMessage(g_save_dlg_data->hwnd_title, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);

    g_orig_canvas_name_proc = (WNDPROC)SetWindowLongPtrA(
        g_save_dlg_data->hwnd_name, GWLP_WNDPROC, (LONG_PTR)CanvasNameEditSubclassProc
    );
    g_orig_canvas_title_proc = (WNDPROC)SetWindowLongPtrA(
        g_save_dlg_data->hwnd_title, GWLP_WNDPROC, (LONG_PTR)CanvasTitleEditSubclassProc
    );

    if (g_save_dlg_data->plan.count > 0) {
        g_save_dlg_data->hwnd_chk_orig = CreateWindowA(
            "BUTTON", "Keep original format",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            24, 268, 160, 24, hwnd_dlg, (HMENU)ID_SAVE_CHK_ORIG, hinst, NULL
        );
        SendMessage(g_save_dlg_data->hwnd_chk_orig, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);

        g_orig_save_chk_proc = (WNDPROC)SetWindowLongPtrA(
            g_save_dlg_data->hwnd_chk_orig, GWLP_WNDPROC, (LONG_PTR)SaveThemedChkSubclassProc
        );

        /* Static gray label showing original file extension when Keep original format is checked */
        g_save_dlg_data->hwnd_lbl_orig_ext = CreateWindowA(
            "STATIC", "(format: .png)",
            WS_CHILD | SS_LEFT | SS_CENTERIMAGE | WS_VISIBLE,
            190, 268, 150, 24, hwnd_dlg, (HMENU)ID_SAVE_LBL_ORIG_EXT, hinst, NULL
        );
        SendMessage(g_save_dlg_data->hwnd_lbl_orig_ext, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);

        /* ComboBox to the right of Keep original format */
        g_save_dlg_data->hwnd_combo_fmt = CreateWindowA(
            "COMBOBOX", "",
            WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            190, 268, 100, 160, hwnd_dlg, (HMENU)ID_SAVE_COMBO_FORMAT, hinst, NULL
        );
        SendMessage(g_save_dlg_data->hwnd_combo_fmt, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);

        g_orig_save_combo_proc = (WNDPROC)SetWindowLongPtrA(
            g_save_dlg_data->hwnd_combo_fmt, GWLP_WNDPROC, (LONG_PTR)SaveThemedComboSubclassProc
        );

        /* Ordered: .webp first, followed by .png, .jpg, .gif */
        SendMessageA(g_save_dlg_data->hwnd_combo_fmt, CB_ADDSTRING, 0, (LPARAM)".webp");
        SendMessageA(g_save_dlg_data->hwnd_combo_fmt, CB_ADDSTRING, 0, (LPARAM)".png");
        SendMessageA(g_save_dlg_data->hwnd_combo_fmt, CB_ADDSTRING, 0, (LPARAM)".jpg");
        SendMessageA(g_save_dlg_data->hwnd_combo_fmt, CB_ADDSTRING, 0, (LPARAM)".gif");

        /* Themed tooltip for blocked checkbox */
        HWND hwnd_tt = CreateWindowExA(
            WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd_dlg, NULL, hinst, NULL
        );
        if (hwnd_tt) {
            SetWindowPos(hwnd_tt, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowTheme(hwnd_tt, L"", L"");

            COLORREF tt_bg = (g_current_theme == THEME_DARK) ? RGB(36, 40, 48) : RGB(255, 255, 255);
            COLORREF tt_fg = (g_current_theme == THEME_DARK) ? RGB(222, 226, 235) : RGB(32, 36, 44);
            SendMessage(hwnd_tt, TTM_SETTIPBKCOLOR, (WPARAM)tt_bg, 0);
            SendMessage(hwnd_tt, TTM_SETTIPTEXTCOLOR, (WPARAM)tt_fg, 0);
            SendMessage(hwnd_tt, TTM_SETMAXTIPWIDTH, 0, (LPARAM)320);
            SendMessage(hwnd_tt, TTM_SETDELAYTIME, TTDT_INITIAL, 200);
            SendMessage(hwnd_tt, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);
            SendMessage(hwnd_tt, TTM_SETDELAYTIME, TTDT_RESHOW, 100);
            if (g_font_primary_regular) {
                SendMessage(hwnd_tt, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);
            }

            TOOLINFOA ti;
            memset(&ti, 0, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd = hwnd_dlg;
            ti.uId = (UINT_PTR)g_save_dlg_data->hwnd_chk_orig;
            ti.lpszText = NULL;
            SendMessageA(hwnd_tt, TTM_ADDTOOLA, 0, (LPARAM)&ti);
            SendMessage(hwnd_tt, TTM_ACTIVATE, FALSE, 0);

            g_save_dlg_data->hwnd_tooltip = hwnd_tt;
        }

        /* Initial sync of asset controls with the first asset item */
        sync_save_dlg_asset_controls(hwnd_dlg);
    }

    if (!g_save_dlg_data->hwnd_tooltip) {
        HWND hwnd_tt = CreateWindowExA(
            WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd_dlg, NULL, hinst, NULL
        );
        if (hwnd_tt) {
            SetWindowPos(hwnd_tt, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowTheme(hwnd_tt, L"", L"");
            COLORREF tt_bg = (g_current_theme == THEME_DARK) ? RGB(36, 40, 48) : RGB(255, 255, 255);
            COLORREF tt_fg = (g_current_theme == THEME_DARK) ? RGB(222, 226, 235) : RGB(32, 36, 44);
            SendMessage(hwnd_tt, TTM_SETTIPBKCOLOR, (WPARAM)tt_bg, 0);
            SendMessage(hwnd_tt, TTM_SETTIPTEXTCOLOR, (WPARAM)tt_fg, 0);
            SendMessage(hwnd_tt, TTM_SETMAXTIPWIDTH, 0, (LPARAM)360);
            SendMessage(hwnd_tt, TTM_SETDELAYTIME, TTDT_INITIAL, 200);
            SendMessage(hwnd_tt, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);
            SendMessage(hwnd_tt, TTM_SETDELAYTIME, TTDT_RESHOW, 100);
            if (g_font_primary_regular) {
                SendMessage(hwnd_tt, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);
            }
            g_save_dlg_data->hwnd_tooltip = hwnd_tt;
        }
    }

    /* Level Difficulty ComboBox */
    g_save_dlg_data->selected_diff = g_level_difficulty;
    g_save_dlg_data->hwnd_combo_diff = CreateWindowA(
        "COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        24, 324, 194, 160, hwnd_dlg, (HMENU)ID_SAVE_COMBO_DIFF, hinst, NULL
    );
    SendMessage(g_save_dlg_data->hwnd_combo_diff, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);
    g_orig_save_combo_diff_proc = (WNDPROC)SetWindowLongPtrA(
        g_save_dlg_data->hwnd_combo_diff, GWLP_WNDPROC, (LONG_PTR)SaveThemedComboSubclassProc
    );

    SendMessageA(g_save_dlg_data->hwnd_combo_diff, CB_ADDSTRING, 0, (LPARAM)"Dev-Test (Testing Kit)");
    SendMessageA(g_save_dlg_data->hwnd_combo_diff, CB_ADDSTRING, 0, (LPARAM)"Easy (Relaxed)");
    SendMessageA(g_save_dlg_data->hwnd_combo_diff, CB_ADDSTRING, 0, (LPARAM)"Medium (Standard Default)");
    SendMessageA(g_save_dlg_data->hwnd_combo_diff, CB_ADDSTRING, 0, (LPARAM)"Hard (Challenging)");
    SendMessageA(g_save_dlg_data->hwnd_combo_diff, CB_ADDSTRING, 0, (LPARAM)"Extreme (Intense)");

    int diff_cb_idx = 2; /* Default: Medium is index 2 (below Easy) */
    if (g_save_dlg_data->selected_diff == LEVEL_DIFF_DEV_TEST) diff_cb_idx = 0;
    else if (g_save_dlg_data->selected_diff == LEVEL_DIFF_EASY) diff_cb_idx = 1;
    else if (g_save_dlg_data->selected_diff == LEVEL_DIFF_MEDIUM) diff_cb_idx = 2;
    else if (g_save_dlg_data->selected_diff == LEVEL_DIFF_HARD) diff_cb_idx = 3;
    else if (g_save_dlg_data->selected_diff == LEVEL_DIFF_EXTREME) diff_cb_idx = 4;
    SendMessage(g_save_dlg_data->hwnd_combo_diff, CB_SETCURSEL, diff_cb_idx, 0);

    /* Progression ID Label */
    int actual_paths = (g_path_count > 0 && g_path_count <= 10) ? g_path_count : 1;
    char init_prog_id[64];
    get_level_progression_id(actual_paths, g_save_dlg_data->selected_diff, init_prog_id, sizeof(init_prog_id));

    g_save_dlg_data->hwnd_lbl_prog_id = CreateWindowA(
        "STATIC", init_prog_id,
        WS_CHILD | SS_LEFT | SS_CENTERIMAGE | WS_VISIBLE,
        236, 324, 226, 26, hwnd_dlg, (HMENU)ID_SAVE_LBL_PROG_ID, hinst, NULL
    );
    SendMessage(g_save_dlg_data->hwnd_lbl_prog_id, WM_SETFONT, (WPARAM)g_font_primary_bold, TRUE);

    /* Tooltip for difficulty combobox */
    if (g_save_dlg_data->hwnd_tooltip) {
        TOOLINFOA ti_diff;
        memset(&ti_diff, 0, sizeof(ti_diff));
        ti_diff.cbSize = sizeof(ti_diff);
        ti_diff.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
        ti_diff.hwnd = hwnd_dlg;
        ti_diff.uId = (UINT_PTR)g_save_dlg_data->hwnd_combo_diff;
        ti_diff.lpszText = (LPSTR)level_difficulty_get_description(g_save_dlg_data->selected_diff);
        SendMessageA(g_save_dlg_data->hwnd_tooltip, TTM_ADDTOOLA, 0, (LPARAM)&ti_diff);

        /* Subclass dropdown listbox to support live hover tooltips for all difficulty items */
        COMBOBOXINFO cbi;
        memset(&cbi, 0, sizeof(cbi));
        cbi.cbSize = sizeof(COMBOBOXINFO);
        if (GetComboBoxInfo(g_save_dlg_data->hwnd_combo_diff, &cbi) && cbi.hwndList) {
            g_orig_save_combo_diff_list_proc = (WNDPROC)SetWindowLongPtrA(cbi.hwndList, GWLP_WNDPROC, (LONG_PTR)SaveComboDiffListSubclassProc);
            TOOLINFOA ti_list;
            memset(&ti_list, 0, sizeof(ti_list));
            ti_list.cbSize = sizeof(ti_list);
            ti_list.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti_list.hwnd = hwnd_dlg;
            ti_list.uId = (UINT_PTR)cbi.hwndList;
            ti_list.lpszText = (LPSTR)level_difficulty_get_description(g_save_dlg_data->selected_diff);
            SendMessageA(g_save_dlg_data->hwnd_tooltip, TTM_ADDTOOLA, 0, (LPARAM)&ti_list);
        }
    }

    /* Buttons (perfect 24px right and bottom margins) */
    int btn_y = 370;
    int cancel_w = 84;
    int save_w = 124;
    int cancel_x = client_w - 24 - cancel_w;
    int save_x = cancel_x - 12 - save_w;

    g_save_dlg_data->hwnd_btn_save = CreateWindowA(
        "BUTTON", "Save Canvas", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        save_x, btn_y, save_w, 28, hwnd_dlg, (HMENU)ID_SAVE_BTN_SAVE, hinst, NULL
    );
    g_save_dlg_data->hwnd_btn_cancel = CreateWindowA(
        "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        cancel_x, btn_y, cancel_w, 28, hwnd_dlg, (HMENU)ID_SAVE_BTN_CANCEL, hinst, NULL
    );

    SetFocus(g_save_dlg_data->hwnd_name);
    SendMessage(g_save_dlg_data->hwnd_name, EM_SETSEL, 0, -1);
}

/* ========================================================================= */
/* Open Canvas Modal Dialog                                                  */
/* ========================================================================= */
typedef struct {
    char name[64];
    char title[128];
} OpenCanvasEntry;

typedef struct {
    HWND hwnd_dlg;
    HWND hwnd_list;
    HWND hwnd_btn_select;
    HWND hwnd_btn_rename;
    HWND hwnd_btn_delete;
    HWND hwnd_btn_refresh;
    HWND hwnd_btn_cancel;
    char base_dir[MAX_PATH];
    char selected_folder[MAX_PATH];
    char selected_name[128];
    char selected_title[128];
    int  selected_w;
    int  selected_h;
    char selected_img[MAX_PATH];
    bool selected_has_xml;
    int  selected_coins_count;
    bool has_selection;

    int entry_count;
    OpenCanvasEntry entries[256];
} OpenCanvasDlgData;

static OpenCanvasDlgData *g_open_dlg_data = NULL;

static void update_open_canvas_selection_info(HWND hwnd_dlg) {
    if (!g_open_dlg_data) return;
    int idx = (int)SendMessage(g_open_dlg_data->hwnd_list, LB_GETCURSEL, 0, 0);
    if (idx != LB_ERR) {
        char item_text[MAX_PATH] = {0};
        SendMessageA(g_open_dlg_data->hwnd_list, LB_GETTEXT, idx, (LPARAM)item_text);
        snprintf(g_open_dlg_data->selected_folder, sizeof(g_open_dlg_data->selected_folder),
                 "%s\\%s", g_open_dlg_data->base_dir, item_text);

        g_open_dlg_data->selected_has_xml = load_canvas_settings_xml(
            g_open_dlg_data->selected_folder,
            g_open_dlg_data->selected_name, sizeof(g_open_dlg_data->selected_name),
            g_open_dlg_data->selected_title, sizeof(g_open_dlg_data->selected_title),
            &g_open_dlg_data->selected_w, &g_open_dlg_data->selected_h,
            g_open_dlg_data->selected_img, sizeof(g_open_dlg_data->selected_img),
            NULL, NULL, NULL,
            NULL, NULL,
            NULL, &g_open_dlg_data->selected_coins_count, 0,
            NULL, NULL, 0,
            NULL, NULL, 0,
            NULL, NULL, 0
        );

        if (g_open_dlg_data->selected_name[0] == '\0') {
            strncpy(g_open_dlg_data->selected_name, item_text, sizeof(g_open_dlg_data->selected_name) - 1);
        }
        if (g_open_dlg_data->selected_title[0] == '\0') {
            strncpy(g_open_dlg_data->selected_title, g_open_dlg_data->selected_name, sizeof(g_open_dlg_data->selected_title) - 1);
        }
        if (g_open_dlg_data->selected_img[0] == '\0') {
            find_image_in_folder(g_open_dlg_data->selected_folder, g_open_dlg_data->selected_name,
                                 g_open_dlg_data->selected_img, sizeof(g_open_dlg_data->selected_img));
        }
        if (g_open_dlg_data->selected_w <= 0) g_open_dlg_data->selected_w = DEFAULT_CANVAS_WIDTH;
        if (g_open_dlg_data->selected_h <= 0) g_open_dlg_data->selected_h = DEFAULT_CANVAS_HEIGHT;

        g_open_dlg_data->has_selection = true;
    }
    InvalidateRect(hwnd_dlg, NULL, FALSE);
}

static void populate_open_canvas_list(HWND hwnd_dlg) {
    if (!g_open_dlg_data || !g_open_dlg_data->hwnd_list) return;
    SendMessage(g_open_dlg_data->hwnd_list, LB_RESETCONTENT, 0, 0);
    g_open_dlg_data->entry_count = 0;

    get_canvas_base_directory(g_open_dlg_data->base_dir, sizeof(g_open_dlg_data->base_dir));

    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.*", g_open_dlg_data->base_dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    int count = 0;
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {

                char c_name[128] = {0};
                char c_title[128] = {0};
                char folder_path[MAX_PATH];
                snprintf(folder_path, sizeof(folder_path), "%s\\%s", g_open_dlg_data->base_dir, fd.cFileName);
                load_canvas_settings_xml(
                    folder_path,
                    c_name, sizeof(c_name),
                    c_title, sizeof(c_title),
                    NULL, NULL, NULL, 0,
                    NULL, NULL, NULL,
                    NULL, NULL,
                    NULL, NULL, 0,
                    NULL, NULL, 0,
                    NULL, NULL, 0,
                    NULL, NULL, 0
                );
                const char *disp_name = (c_name[0] != '\0') ? c_name : fd.cFileName;
                const char *disp_title = (c_title[0] != '\0') ? c_title : disp_name;

                if (count < 256) {
                    strncpy(g_open_dlg_data->entries[count].name, disp_name, 63);
                    g_open_dlg_data->entries[count].name[63] = '\0';
                    strncpy(g_open_dlg_data->entries[count].title, disp_title, 127);
                    g_open_dlg_data->entries[count].title[127] = '\0';
                    SendMessageA(g_open_dlg_data->hwnd_list, LB_ADDSTRING, 0, (LPARAM)fd.cFileName);
                    count++;
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    g_open_dlg_data->entry_count = count;

    if (count > 0) {
        SendMessage(g_open_dlg_data->hwnd_list, LB_SETCURSEL, 0, 0);
        update_open_canvas_selection_info(hwnd_dlg);
    } else {
        g_open_dlg_data->has_selection = false;
        InvalidateRect(hwnd_dlg, NULL, FALSE);
    }
}

/* Helper to replace all occurrences of a substring */
static char *replace_string_all(const char *src, const char *old_str, const char *new_str) {
    if (!src || !old_str || !new_str || old_str[0] == '\0') return NULL;

    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);

    int count = 0;
    const char *p = src;
    while ((p = strstr(p, old_str)) != NULL) {
        count++;
        p += old_len;
    }

    if (count == 0) {
        return _strdup(src);
    }

    size_t src_len = strlen(src);
    size_t new_total_len = src_len + count * (new_len - old_len);
    char *result = (char *)malloc(new_total_len + 1);
    if (!result) return NULL;

    char *dest = result;
    p = src;
    const char *match;
    while ((match = strstr(p, old_str)) != NULL) {
        size_t chunk_len = match - p;
        memcpy(dest, p, chunk_len);
        dest += chunk_len;
        memcpy(dest, new_str, new_len);
        dest += new_len;
        p = match + old_len;
    }
    strcpy(dest, p);
    return result;
}

static char *replace_range(const char *src, const char *range_start, const char *range_end, const char *repl) {
    if (!src || !range_start || !range_end || range_end < range_start) return NULL;
    size_t prefix_len = range_start - src;
    size_t repl_len = strlen(repl);
    size_t suffix_len = strlen(range_end);
    char *out = (char *)malloc(prefix_len + repl_len + suffix_len + 1);
    if (!out) return NULL;
    memcpy(out, src, prefix_len);
    memcpy(out + prefix_len, repl, repl_len);
    memcpy(out + prefix_len + repl_len, range_end, suffix_len + 1);
    return out;
}

static char *update_xml_canvas_title(char *xml_buf, const char *new_title) {
    if (!xml_buf || !new_title) return xml_buf;

    /* 1. Update CanvasTitle: in comments */
    char *ct = strstr(xml_buf, "CanvasTitle:");
    if (ct) {
        char *val = ct + 12;
        while (*val == ' ' || *val == '\t') val++;
        char *eol = strpbrk(val, "\r\n");
        if (!eol) eol = val + strlen(val);
        char *next_xml = replace_range(xml_buf, val, eol, new_title);
        if (next_xml) {
            free(xml_buf);
            xml_buf = next_xml;
        }
    } else {
        char *cn = strstr(xml_buf, "CanvasName:");
        if (cn) {
            char *eol = strpbrk(cn, "\r\n");
            if (eol) {
                if (*eol == '\r' && *(eol + 1) == '\n') eol += 2;
                else eol += 1;
                char ins[256];
                snprintf(ins, sizeof(ins), "    CanvasTitle: %s\n", new_title);
                char *next_xml = replace_range(xml_buf, eol, eol, ins);
                if (next_xml) {
                    free(xml_buf);
                    xml_buf = next_xml;
                }
            }
        }
    }

    /* 2. Update dispname="..." in <Graphics */
    char *gfx = strstr(xml_buf, "<Graphics");
    if (gfx) {
        char *tag_end = strchr(gfx, '>');
        if (tag_end) {
            char *dn = strstr(gfx, "dispname=\"");
            if (dn && dn < tag_end) {
                char *val = dn + 10;
                char *q = strchr(val, '"');
                if (q && q < tag_end) {
                    char *next_xml = replace_range(xml_buf, val, q, new_title);
                    if (next_xml) {
                        free(xml_buf);
                        xml_buf = next_xml;
                    }
                }
            } else {
                char ins[256];
                snprintf(ins, sizeof(ins), " dispname=\"%s\"", new_title);
                char *next_xml = replace_range(xml_buf, tag_end, tag_end, ins);
                if (next_xml) {
                    free(xml_buf);
                    xml_buf = next_xml;
                }
            }
        }
    }

    return xml_buf;
}

static void notify_manage_levels_canvas_renamed(const char *new_name);

/* ========================================================================= */
/* Rename Canvas Modal Dialog                                                */
/* ========================================================================= */
typedef struct {
    HWND hwnd_dlg;
    HWND hwnd_edit;
    HWND hwnd_edit_title;
    HWND hwnd_btn_ok;
    HWND hwnd_btn_cancel;
    HWND hwnd_open_parent;
    char old_name[MAX_PATH];
    char old_title[128];
    char old_folder[MAX_PATH];
    char base_dir[MAX_PATH];
} RenameCanvasDlgData;

static RenameCanvasDlgData *g_rename_dlg_data = NULL;

static LRESULT CALLBACK RenameCanvasModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();

    switch (msg) {
        case WM_CREATE:
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH bg_brush = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc, bg_brush);
            DeleteObject(bg_brush);

            HPEN border_pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(hdc, border_pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, th->text_primary);
            SelectObject(hdc, g_font_primary_bold);
            RECT rc_title = {24, 16, rc.right - 24, 38};
            DrawTextA(hdc, "Rename Canvas Project", -1, &rc_title, DT_SINGLELINE | DT_LEFT | DT_TOP);

            SetTextColor(hdc, th->text_secondary);
            SelectObject(hdc, g_font_primary_regular);
            RECT rc_sub = {24, 40, rc.right - 24, 60};
            DrawTextA(hdc, "Update canvas folder name/ID and display title:", -1, &rc_sub, DT_SINGLELINE | DT_LEFT | DT_TOP);

            SelectObject(hdc, g_font_primary_bold);
            SetTextColor(hdc, th->text_primary);
            RECT rc_lbl1 = {24, 64, rc.right - 24, 82};
            DrawTextA(hdc, "Canvas Name (Folder/ID):", -1, &rc_lbl1, DT_SINGLELINE | DT_LEFT | DT_TOP);

            RECT rc_lbl2 = {24, 118, rc.right - 24, 136};
            DrawTextA(hdc, "Canvas Title (dispname):", -1, &rc_lbl2, DT_SINGLELINE | DT_LEFT | DT_TOP);

            SelectObject(hdc, old_pen);
            DeleteObject(border_pen);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc_ctl = (HDC)wParam;
            HWND hwnd_ctl = (HWND)lParam;
            if (g_rename_dlg_data && (hwnd_ctl == g_rename_dlg_data->hwnd_edit ||
                                      hwnd_ctl == g_rename_dlg_data->hwnd_edit_title)) {
                COLORREF edit_bg = (g_current_theme == THEME_DARK) ? RGB(36, 40, 48) : RGB(255, 255, 255);
                SetTextColor(hdc_ctl, th->text_primary);
                SetBkColor(hdc_ctl, edit_bg);
                static HBRUSH s_edit_br_light = NULL;
                static HBRUSH s_edit_br_dark = NULL;
                if (!s_edit_br_light) s_edit_br_light = CreateSolidBrush(RGB(255, 255, 255));
                if (!s_edit_br_dark)  s_edit_br_dark  = CreateSolidBrush(RGB(36, 40, 48));
                return (LRESULT)((g_current_theme == THEME_DARK) ? s_edit_br_dark : s_edit_br_light);
            }
            break;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool is_ok = (dis->CtlID == ID_RENAME_BTN_OK);

                COLORREF bg_color = is_ok ? (is_pressed ? RGB(18, 70, 140) : th->text_accent_blue)
                                          : (is_pressed ? th->btn_bg_pressed : th->btn_bg);
                HBRUSH bg_brush = CreateSolidBrush(bg_color);
                FillRect(dis->hDC, &dis->rcItem, bg_brush);
                DeleteObject(bg_brush);

                COLORREF border_col = is_ok ? th->text_accent_blue : th->btn_border;
                HPEN pen = CreatePen(PS_SOLID, 1, border_col);
                HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, old_pen);
                DeleteObject(pen);

                char text[64] = {0};
                GetWindowTextA(dis->hwndItem, text, sizeof(text));

                SetBkMode(dis->hDC, TRANSPARENT);
                COLORREF text_col = is_ok ? RGB(255, 255, 255) : th->btn_text;
                SetTextColor(dis->hDC, text_col);
                SelectObject(dis->hDC, is_ok ? g_font_primary_bold : g_font_primary_regular);

                RECT rc_t = dis->rcItem;
                if (is_pressed) { rc_t.left += 1; rc_t.top += 1; }
                DrawTextA(dis->hDC, text, -1, &rc_t, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (!g_rename_dlg_data) break;

            if (cmd == ID_RENAME_BTN_OK) {
                char raw_name[128] = {0};
                char raw_title[128] = {0};
                GetWindowTextA(g_rename_dlg_data->hwnd_edit, raw_name, sizeof(raw_name));
                GetWindowTextA(g_rename_dlg_data->hwnd_edit_title, raw_title, sizeof(raw_title));

                /* Trim whitespace for name */
                char *s_name = raw_name;
                while (*s_name == ' ' || *s_name == '\t' || *s_name == '\r' || *s_name == '\n') s_name++;
                char *e_name = s_name + strlen(s_name);
                while (e_name > s_name && (*(e_name - 1) == ' ' || *(e_name - 1) == '\t' || *(e_name - 1) == '\r' || *(e_name - 1) == '\n')) {
                    e_name--;
                }
                *e_name = '\0';

                if (*s_name == '\0') {
                    show_themed_notice_dialog(hwnd, "Invalid Name", "Canvas Name Required", "Canvas name cannot be empty.", 460, 240);
                    SetFocus(g_rename_dlg_data->hwnd_edit);
                    return 0;
                }

                if (*s_name >= '0' && *s_name <= '9') {
                    show_themed_notice_dialog(hwnd, "Zuma Deluxe Warning", "Level Name Cannot Start with a Number",
                        "Zuma Deluxe level IDs must start with a letter (a-z), not a number.\n\n"
                        "Starting with a number causes Zuma's difficulty parser (atol) to confuse the level with a numerical difficulty index and crash.\n\n"
                        "Please prefix your level name with letters (e.g. 'lvl_' or 'stage_').", 540, 310);
                    SetFocus(g_rename_dlg_data->hwnd_edit);
                    return 0;
                }

                /* Trim whitespace for title */
                char *s_title = raw_title;
                while (*s_title == ' ' || *s_title == '\t' || *s_title == '\r' || *s_title == '\n') s_title++;
                char *e_title = s_title + strlen(s_title);
                while (e_title > s_title && (*(e_title - 1) == ' ' || *(e_title - 1) == '\t' || *(e_title - 1) == '\r' || *(e_title - 1) == '\n')) {
                    e_title--;
                }
                *e_title = '\0';

                char new_name[128];
                strncpy(new_name, s_name, sizeof(new_name) - 1);
                new_name[sizeof(new_name) - 1] = '\0';

                char new_title[128];
                if (*s_title != '\0') {
                    strncpy(new_title, s_title, sizeof(new_title) - 1);
                } else {
                    strncpy(new_title, new_name, sizeof(new_title) - 1);
                }
                new_title[sizeof(new_title) - 1] = '\0';

                bool name_changed = (strcmp(new_name, g_rename_dlg_data->old_name) != 0);
                bool title_changed = (strcmp(new_title, g_rename_dlg_data->old_title) != 0);

                /* If neither changed, simply close */
                if (!name_changed && !title_changed) {
                    DestroyWindow(hwnd);
                    return 0;
                }

                char target_folder[MAX_PATH];
                if (name_changed) {
                    /* Check invalid characters: \ / : * ? " < > | */
                    const char *invalid_chars = "\\/:*?\"<>|";
                    for (const char *c = new_name; *c; ++c) {
                        if (strchr(invalid_chars, *c) != NULL) {
                            show_themed_notice_dialog(hwnd, "Invalid Characters", "Invalid Characters in Canvas Name",
                                "A canvas name cannot contain any of the following characters:\n\\ / : * ? \" < > |", 480, 250);
                            SetFocus(g_rename_dlg_data->hwnd_edit);
                            return 0;
                        }
                    }

                    /* Check if target folder already exists */
                    snprintf(target_folder, sizeof(target_folder), "%s\\%s", g_rename_dlg_data->base_dir, new_name);

                    DWORD attr = GetFileAttributesA(target_folder);
                    if (attr != INVALID_FILE_ATTRIBUTES) {
                        show_themed_notice_dialog(hwnd, "Canvas Already Exists", "Canvas Folder Already Exists",
                            "A canvas project with this name already exists in ./canvas/.\nPlease choose a different name.", 480, 250);
                        SetFocus(g_rename_dlg_data->hwnd_edit);
                        return 0;
                    }

                    /* Step 1: Rename the folder first */
                    if (!MoveFileA(g_rename_dlg_data->old_folder, target_folder)) {
                        DWORD err = GetLastError();
                        char err_msg[256];
                        snprintf(err_msg, sizeof(err_msg), "Failed to rename canvas folder (Error %lu).\nPlease check if the folder is open in another program.", err);
                        show_themed_notice_dialog(hwnd, "Rename Failed", "Folder In Use", err_msg, 480, 250);
                        return 0;
                    }

                    /* Step 2: Rename internal files inside target_folder */
                    char search_spec[MAX_PATH];
                    snprintf(search_spec, sizeof(search_spec), "%s\\*.*", target_folder);

                    WIN32_FIND_DATAA fd;
                    HANDLE hFind = FindFirstFileA(search_spec, &fd);
                    if (hFind != INVALID_HANDLE_VALUE) {
                        size_t old_len = strlen(g_rename_dlg_data->old_name);
                        do {
                            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                                continue;
                            }

                            if (strncmp(fd.cFileName, g_rename_dlg_data->old_name, old_len) == 0) {
                                char next_ch = fd.cFileName[old_len];
                                if (next_ch == '.' || next_ch == '-') {
                                    const char *suffix = fd.cFileName + old_len;
                                    char new_file_name[MAX_PATH];
                                    snprintf(new_file_name, sizeof(new_file_name), "%s%s", new_name, suffix);

                                    char old_file_path[MAX_PATH];
                                    char new_file_path[MAX_PATH];
                                    snprintf(old_file_path, sizeof(old_file_path), "%s\\%s", target_folder, fd.cFileName);
                                    snprintf(new_file_path, sizeof(new_file_path), "%s\\%s", target_folder, new_file_name);

                                    MoveFileA(old_file_path, new_file_path);
                                }
                            }
                        } while (FindNextFileA(hFind, &fd));
                        FindClose(hFind);
                    }
                } else {
                    strncpy(target_folder, g_rename_dlg_data->old_folder, sizeof(target_folder) - 1);
                    target_folder[sizeof(target_folder) - 1] = '\0';
                }

                /* Step 3: Update settings.xml */
                char xml_path[MAX_PATH];
                snprintf(xml_path, sizeof(xml_path), "%s\\settings.xml", target_folder);

                FILE *fx = fopen(xml_path, "rb");
                if (fx) {
                    fseek(fx, 0, SEEK_END);
                    long sz = ftell(fx);
                    fseek(fx, 0, SEEK_SET);
                    if (sz > 0 && sz < 4 * 1024 * 1024) {
                        char *xml_buf = (char *)malloc(sz + 1);
                        if (xml_buf) {
                            fread(xml_buf, 1, sz, fx);
                            xml_buf[sz] = '\0';
                            fclose(fx);
                            fx = NULL;

                            char *processed_xml = NULL;
                            if (name_changed) {
                                processed_xml = replace_string_all(xml_buf, g_rename_dlg_data->old_name, new_name);
                                free(xml_buf);
                            } else {
                                processed_xml = xml_buf;
                            }

                            if (processed_xml) {
                                processed_xml = update_xml_canvas_title(processed_xml, new_title);
                                FILE *fw = fopen(xml_path, "wb");
                                if (fw) {
                                    fwrite(processed_xml, 1, strlen(processed_xml), fw);
                                    fclose(fw);
                                }
                                free(processed_xml);
                            }
                        }
                    }
                    if (fx) fclose(fx);
                }

                /* Step 4: If active canvas was renamed or title updated, sync memory state */
                bool is_active_canvas = (g_has_canvas &&
                    (_stricmp(g_canvas_name, g_rename_dlg_data->old_name) == 0 ||
                     _stricmp(g_canvas_folder, g_rename_dlg_data->old_folder) == 0));

                if (is_active_canvas) {
                    if (name_changed) {
                        size_t old_len = strlen(g_rename_dlg_data->old_name);
                        strncpy(g_canvas_name, new_name, sizeof(g_canvas_name) - 1);
                        g_canvas_name[sizeof(g_canvas_name) - 1] = '\0';
                        strncpy(g_canvas_folder, target_folder, sizeof(g_canvas_folder) - 1);
                        g_canvas_folder[sizeof(g_canvas_folder) - 1] = '\0';

                        if (g_has_bg_image) {
                            if (strncmp(g_canvas_img.filename, g_rename_dlg_data->old_name, old_len) == 0) {
                                char next_ch = g_canvas_img.filename[old_len];
                                if (next_ch == '.' || next_ch == '-') {
                                    char new_img_name[MAX_PATH];
                                    snprintf(new_img_name, sizeof(new_img_name), "%s%s", new_name, g_canvas_img.filename + old_len);
                                    strncpy(g_canvas_img.filename, new_img_name, sizeof(g_canvas_img.filename) - 1);
                                    snprintf(g_canvas_img.filepath, sizeof(g_canvas_img.filepath), "%s\\%s", target_folder, new_img_name);
                                    image_get_basename_no_ext(g_canvas_img.filepath, g_canvas_img.display_name, sizeof(g_canvas_img.display_name));
                                }
                            } else {
                                snprintf(g_canvas_img.filepath, sizeof(g_canvas_img.filepath), "%s\\%s", target_folder, g_canvas_img.filename);
                            }
                        }

                        if (g_path_count == 1) {
                            snprintf(g_paths[0].name, sizeof(g_paths[0].name), "%s", new_name);
                        } else {
                            for (int p = 0; p < g_path_count; ++p) {
                                snprintf(g_paths[p].name, sizeof(g_paths[p].name), "%s-%d", new_name, p + 1);
                            }
                        }
                    }

                    sync_canvas_title_everywhere(new_title, NULL);
                    sync_all_map_attr_edits_from_state();
                    InvalidateRect(g_hwnd_main, NULL, FALSE);
                }

                /* Step 5: Refresh parent list and select the newly renamed folder */
                HWND parent = g_rename_dlg_data->hwnd_open_parent;
                if (g_open_dlg_data && parent == g_open_dlg_data->hwnd_dlg) {
                    populate_open_canvas_list(parent);
                    if (g_open_dlg_data->hwnd_list) {
                        int found_idx = (int)SendMessageA(g_open_dlg_data->hwnd_list, LB_FINDSTRINGEXACT, -1, (LPARAM)new_name);
                        if (found_idx != LB_ERR) {
                            SendMessage(g_open_dlg_data->hwnd_list, LB_SETCURSEL, found_idx, 0);
                            update_open_canvas_selection_info(parent);
                        }
                    }
                }
                notify_manage_levels_canvas_renamed(new_name);

                DestroyWindow(hwnd);
                return 0;
            } else if (cmd == ID_RENAME_BTN_CANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_TIMER:
            if (wParam == 9921) {
                KillTimer(hwnd, 9921);
                hide_edit_warning_tooltip();
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            destroy_edit_warning_tooltip();
            HWND parent = g_rename_dlg_data ? g_rename_dlg_data->hwnd_open_parent : NULL;
            if (g_active_modal_dlg == hwnd) {
                g_active_modal_dlg = parent;
            }
            if (g_rename_dlg_data) {
                free(g_rename_dlg_data);
                g_rename_dlg_data = NULL;
            }
            if (parent && IsWindow(parent)) {
                EnableWindow(parent, TRUE);
                SetFocus(parent);
            }
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void show_rename_canvas_dialog(HWND parent_hwnd, const char *old_name, const char *old_title, const char *old_folder, const char *base_dir) {
    if (!old_name || !old_folder || !base_dir || old_name[0] == '\0') return;

    if (g_rename_dlg_data) {
        SetFocus(g_rename_dlg_data->hwnd_dlg);
        return;
    }

    HINSTANCE hinst = GetModuleHandle(NULL);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = RenameCanvasModalProc;
    wc.hInstance     = hinst;
    wc.lpszClassName = "ZuPatherRenameCanvasModal";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    RECT rc_dlg = { 0, 0, 440, 226 };
    AdjustWindowRectEx(&rc_dlg, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int dlg_w = rc_dlg.right - rc_dlg.left;
    int dlg_h = rc_dlg.bottom - rc_dlg.top;

    RECT rc_parent;
    GetWindowRect(parent_hwnd, &rc_parent);
    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;

    g_rename_dlg_data = (RenameCanvasDlgData*)calloc(1, sizeof(RenameCanvasDlgData));
    g_rename_dlg_data->hwnd_open_parent = parent_hwnd;
    strncpy(g_rename_dlg_data->old_name, old_name, sizeof(g_rename_dlg_data->old_name) - 1);
    const char *safe_old_title = (old_title && old_title[0]) ? old_title : old_name;
    strncpy(g_rename_dlg_data->old_title, safe_old_title, sizeof(g_rename_dlg_data->old_title) - 1);
    strncpy(g_rename_dlg_data->old_folder, old_folder, sizeof(g_rename_dlg_data->old_folder) - 1);
    strncpy(g_rename_dlg_data->base_dir, base_dir, sizeof(g_rename_dlg_data->base_dir) - 1);

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherRenameCanvasModal", "Rename Canvas Project",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, NULL
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    g_rename_dlg_data->hwnd_dlg = hwnd_dlg;
    EnableWindow(parent_hwnd, FALSE);

    /* Edit Box 1: Canvas Name */
    g_rename_dlg_data->hwnd_edit = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", old_name,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        24, 84, 390, 26, hwnd_dlg, (HMENU)ID_RENAME_EDIT_NAME, hinst, NULL
    );
    SendMessage(g_rename_dlg_data->hwnd_edit, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);
    g_orig_canvas_name_proc = (WNDPROC)SetWindowLongPtrA(
        g_rename_dlg_data->hwnd_edit, GWLP_WNDPROC, (LONG_PTR)CanvasNameEditSubclassProc
    );

    /* Edit Box 2: Canvas Title */
    g_rename_dlg_data->hwnd_edit_title = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", safe_old_title,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        24, 138, 390, 26, hwnd_dlg, (HMENU)ID_RENAME_EDIT_TITLE, hinst, NULL
    );
    SendMessage(g_rename_dlg_data->hwnd_edit_title, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);
    g_orig_canvas_title_proc = (WNDPROC)SetWindowLongPtrA(
        g_rename_dlg_data->hwnd_edit_title, GWLP_WNDPROC, (LONG_PTR)CanvasTitleEditSubclassProc
    );

    /* Buttons */
    g_rename_dlg_data->hwnd_btn_ok = CreateWindowA(
        "BUTTON", "Rename", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        226, 176, 90, 28, hwnd_dlg, (HMENU)ID_RENAME_BTN_OK, hinst, NULL
    );
    g_rename_dlg_data->hwnd_btn_cancel = CreateWindowA(
        "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        324, 176, 90, 28, hwnd_dlg, (HMENU)ID_RENAME_BTN_CANCEL, hinst, NULL
    );

    ShowWindow(hwnd_dlg, SW_SHOW);
    UpdateWindow(hwnd_dlg);

    SetFocus(g_rename_dlg_data->hwnd_edit);
    SendMessage(g_rename_dlg_data->hwnd_edit, EM_SETSEL, 0, -1);
}

static LRESULT CALLBACK OpenCanvasModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();

    switch (msg) {
        case WM_CREATE:
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);
            return 0;

        case WM_THEMECHANGED:
            if (g_open_dlg_data && g_open_dlg_data->hwnd_list) {
                apply_control_dark_mode(g_open_dlg_data->hwnd_list, g_current_theme == THEME_DARK);
            }
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH bg_brush = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc, bg_brush);
            DeleteObject(bg_brush);

            HPEN border_pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(hdc, border_pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom);

            /* Title Header */
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, th->text_primary);
            SelectObject(hdc, g_font_primary_bold);
            RECT rc_title = {24, 18, rc.right - 24, 42};
            DrawTextA(hdc, "Open Canvas Project", -1, &rc_title, DT_SINGLELINE | DT_LEFT | DT_TOP);

            SetTextColor(hdc, th->text_secondary);
            SelectObject(hdc, g_font_primary_regular);
            RECT rc_sub = {24, 44, rc.right - 24, 64};
            DrawTextA(hdc, "Select a canvas folder from ./canvas/ to open, rename, or delete", -1, &rc_sub, DT_SINGLELINE | DT_LEFT | DT_TOP);

            /* Column Labels */
            SelectObject(hdc, g_font_primary_bold);
            SetTextColor(hdc, th->text_primary);
            RECT rc_lbl_list = {24, 68, 334, 88};
            DrawTextA(hdc, "Folders in ./canvas/:", -1, &rc_lbl_list, DT_SINGLELINE | DT_LEFT | DT_TOP);

            RECT rc_lbl_prev = {348, 68, rc.right - 24, 88};
            DrawTextA(hdc, "Canvas Details:", -1, &rc_lbl_prev, DT_SINGLELINE | DT_LEFT | DT_TOP);

            /* 1px themed border around listbox (matching Map Manager) */
            Rectangle(hdc, 23, 91, 24 + 310 + 1, 92 + 260 + 1);

            /* Details Card */
            RECT rc_card = {348, 92, rc.right - 24, 92 + 260};
            HBRUSH card_br = CreateSolidBrush(th->canvas_bg);
            FillRect(hdc, &rc_card, card_br);
            DeleteObject(card_br);

            Rectangle(hdc, rc_card.left, rc_card.top, rc_card.right, rc_card.bottom);

            if (g_open_dlg_data && g_open_dlg_data->has_selection) {
                SetTextColor(hdc, th->text_primary);
                SelectObject(hdc, g_font_primary_bold);

                char buf_name[160];
                snprintf(buf_name, sizeof(buf_name), "Canvas: %s", g_open_dlg_data->selected_name);
                RECT rc_t1 = {rc_card.left + 16, rc_card.top + 14, rc_card.right - 16, rc_card.top + 34};
                DrawTextA(hdc, buf_name, -1, &rc_t1, DT_SINGLELINE | DT_LEFT | DT_TOP);

                SelectObject(hdc, g_font_primary_regular);
                SetTextColor(hdc, th->text_primary);
                char buf_title[160];
                snprintf(buf_title, sizeof(buf_title), "Title: %s", g_open_dlg_data->selected_title[0] ? g_open_dlg_data->selected_title : g_open_dlg_data->selected_name);
                RECT rc_t_title = {rc_card.left + 16, rc_card.top + 36, rc_card.right - 16, rc_card.top + 56};
                DrawTextA(hdc, buf_title, -1, &rc_t_title, DT_SINGLELINE | DT_LEFT | DT_TOP);

                SetTextColor(hdc, th->text_secondary);

                char buf_dim[160];
                snprintf(buf_dim, sizeof(buf_dim), "Dimensions: %d x %d px", g_open_dlg_data->selected_w, g_open_dlg_data->selected_h);
                RECT rc_t2 = {rc_card.left + 16, rc_card.top + 58, rc_card.right - 16, rc_card.top + 78};
                DrawTextA(hdc, buf_dim, -1, &rc_t2, DT_SINGLELINE | DT_LEFT | DT_TOP);

                char buf_img[256];
                if (g_open_dlg_data->selected_img[0]) {
                    snprintf(buf_img, sizeof(buf_img), "Image: %s", g_open_dlg_data->selected_img);
                } else {
                    snprintf(buf_img, sizeof(buf_img), "Image: None (Blank Canvas)");
                }
                RECT rc_t3 = {rc_card.left + 16, rc_card.top + 80, rc_card.right - 16, rc_card.top + 100};
                DrawTextA(hdc, buf_img, -1, &rc_t3, DT_SINGLELINE | DT_LEFT | DT_TOP);

                char buf_xml[160];
                if (g_open_dlg_data->selected_has_xml) {
                    snprintf(buf_xml, sizeof(buf_xml), "Config: settings.xml verified");
                    SetTextColor(hdc, th->text_accent_green);
                } else {
                    snprintf(buf_xml, sizeof(buf_xml), "Config: Scanned from folder files");
                    SetTextColor(hdc, th->text_accent_blue);
                }
                RECT rc_t4 = {rc_card.left + 16, rc_card.top + 102, rc_card.right - 16, rc_card.top + 122};
                DrawTextA(hdc, buf_xml, -1, &rc_t4, DT_SINGLELINE | DT_LEFT | DT_TOP);

                char buf_coins[160];
                if (g_open_dlg_data->selected_coins_count > 0) {
                    snprintf(buf_coins, sizeof(buf_coins), "Coins: %d placed", g_open_dlg_data->selected_coins_count);
                } else {
                    snprintf(buf_coins, sizeof(buf_coins), "Coins: None");
                }
                SetTextColor(hdc, th->text_secondary);
                RECT rc_t_coins = {rc_card.left + 16, rc_card.top + 124, rc_card.right - 16, rc_card.top + 144};
                DrawTextA(hdc, buf_coins, -1, &rc_t_coins, DT_SINGLELINE | DT_LEFT | DT_TOP);

                SetTextColor(hdc, th->text_dim);
                RECT rc_t5_lbl = {rc_card.left + 16, rc_card.top + 148, rc_card.right - 16, rc_card.top + 166};
                DrawTextA(hdc, "Folder Location:", -1, &rc_t5_lbl, DT_SINGLELINE | DT_LEFT | DT_TOP);

                SelectObject(hdc, g_font_mono_regular);
                RECT rc_t5 = {rc_card.left + 16, rc_card.top + 168, rc_card.right - 16, rc_card.bottom - 16};
                DrawTextA(hdc, g_open_dlg_data->selected_folder, -1, &rc_t5, DT_WORDBREAK | DT_LEFT | DT_TOP);
            } else {
                SetTextColor(hdc, th->text_dim);
                SelectObject(hdc, g_font_primary_regular);
                RECT rc_empty = {rc_card.left + 20, rc_card.top + 80, rc_card.right - 20, rc_card.bottom - 20};
                DrawTextA(hdc, "No canvas folder selected.\n\nPick a folder from the list to open, rename, or delete.",
                          -1, &rc_empty, DT_CENTER | DT_WORDBREAK);
            }

            SelectObject(hdc, old_pen);
            DeleteObject(border_pen);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MEASUREITEM: {
            LPMEASUREITEMSTRUCT lpmi = (LPMEASUREITEMSTRUCT)lParam;
            if (lpmi->CtlType == ODT_LISTBOX) {
                lpmi->itemHeight = 20;
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdc_lb = (HDC)wParam;
            SetTextColor(hdc_lb, th->text_primary);
            SetBkColor(hdc_lb, th->canvas_bg);
            static HBRUSH s_lb_br_light = NULL;
            static HBRUSH s_lb_br_dark = NULL;
            if (!s_lb_br_light) s_lb_br_light = CreateSolidBrush(g_theme_light.canvas_bg);
            if (!s_lb_br_dark)  s_lb_br_dark  = CreateSolidBrush(g_theme_dark.canvas_bg);
            return (LRESULT)((g_current_theme == THEME_DARK) ? s_lb_br_dark : s_lb_br_light);
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_LISTBOX) {
                if (dis->itemID == (UINT)-1) return TRUE;
                bool is_sel = (dis->itemState & ODS_SELECTED) != 0;
                bool is_dark = (g_current_theme == THEME_DARK);

                COLORREF bg_item;
                COLORREF text_id_color;
                COLORREF text_title_color;

                if (is_sel) {
                    bg_item = is_dark ? RGB(28, 70, 120) : RGB(218, 234, 252);
                    text_id_color = is_dark ? RGB(255, 255, 255) : RGB(10, 35, 75);
                    text_title_color = is_dark ? RGB(180, 215, 255) : RGB(40, 75, 130);
                } else {
                    bg_item = th->canvas_bg;
                    text_id_color = th->text_primary;
                    text_title_color = th->text_secondary;
                }

                HBRUSH br = CreateSolidBrush(bg_item);
                FillRect(dis->hDC, &dis->rcItem, br);
                DeleteObject(br);

                /* Selection accent bar on left edge */
                if (is_sel) {
                    RECT rc_bar = dis->rcItem;
                    rc_bar.right = rc_bar.left + 3;
                    HBRUSH bar_br = CreateSolidBrush(th->text_accent_blue);
                    FillRect(dis->hDC, &rc_bar, bar_br);
                    DeleteObject(bar_br);
                }

                const char *id_str = "";
                const char *title_str = "";
                if (g_open_dlg_data && dis->itemID < (UINT)g_open_dlg_data->entry_count) {
                    id_str = g_open_dlg_data->entries[dis->itemID].name;
                    title_str = g_open_dlg_data->entries[dis->itemID].title;
                }

                SetBkMode(dis->hDC, TRANSPARENT);

                /* 1. Left Text: Canvas ID (Bold Monospace / Primary) */
                SelectObject(dis->hDC, g_font_mono_bold ? g_font_mono_bold : g_font_primary_bold);
                SetTextColor(dis->hDC, text_id_color);
                RECT rc_left = dis->rcItem;
                rc_left.left += 10;
                rc_left.right = dis->rcItem.left + 140;
                DrawTextA(dis->hDC, id_str, -1, &rc_left, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

                /* 2. Right Text: Display Title (Regular Secondary Font) */
                SelectObject(dis->hDC, g_font_primary_regular);
                SetTextColor(dis->hDC, text_title_color);
                RECT rc_right = dis->rcItem;
                rc_right.left = dis->rcItem.left + 145;
                rc_right.right = dis->rcItem.right - 10;
                DrawTextA(dis->hDC, title_str, -1, &rc_right, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);

                return TRUE;
            }

            if (dis->CtlType == ODT_BUTTON) {
                bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool is_select = (dis->CtlID == ID_OPEN_BTN_SELECT);

                COLORREF bg_color = is_select ? (is_pressed ? RGB(18, 70, 140) : th->text_accent_blue)
                                              : (is_pressed ? th->btn_bg_pressed : th->btn_bg);
                HBRUSH bg_brush = CreateSolidBrush(bg_color);
                FillRect(dis->hDC, &dis->rcItem, bg_brush);
                DeleteObject(bg_brush);

                COLORREF border_col = is_select ? th->text_accent_blue : th->btn_border;
                HPEN pen = CreatePen(PS_SOLID, 1, border_col);
                HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, old_pen);
                DeleteObject(pen);

                char text[64] = {0};
                GetWindowTextA(dis->hwndItem, text, sizeof(text));

                SetBkMode(dis->hDC, TRANSPARENT);
                COLORREF text_col = is_select ? RGB(255, 255, 255) : th->btn_text;
                SetTextColor(dis->hDC, text_col);
                SelectObject(dis->hDC, is_select ? g_font_primary_bold : g_font_primary_regular);

                RECT rc_t = dis->rcItem;
                if (is_pressed) { rc_t.left += 1; rc_t.top += 1; }
                DrawTextA(dis->hDC, text, -1, &rc_t, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (!g_open_dlg_data) break;

            if (cmd == ID_OPEN_LIST_FOLDERS) {
                if (HIWORD(wParam) == LBN_SELCHANGE) {
                    update_open_canvas_selection_info(hwnd);
                } else if (HIWORD(wParam) == LBN_DBLCLK) {
                    if (g_open_dlg_data->has_selection) {
                        char sel_folder[MAX_PATH];
                        strncpy(sel_folder, g_open_dlg_data->selected_folder, sizeof(sel_folder) - 1);
                        sel_folder[sizeof(sel_folder) - 1] = '\0';
                        DestroyWindow(hwnd);
                        show_opening_canvas_modal(g_hwnd_main, sel_folder);
                    }
                }
            } else if (cmd == ID_OPEN_BTN_REFRESH) {
                populate_open_canvas_list(hwnd);
            } else if (cmd == ID_OPEN_BTN_RENAME) {
                if (g_open_dlg_data->has_selection && g_open_dlg_data->selected_name[0] != '\0') {
                    show_rename_canvas_dialog(hwnd, g_open_dlg_data->selected_name,
                                              g_open_dlg_data->selected_title,
                                              g_open_dlg_data->selected_folder,
                                              g_open_dlg_data->base_dir);
                } else {
                    show_themed_notice_dialog(hwnd, "Notice", "No Canvas Selected", "Please select a canvas folder from the list first.", 460, 240);
                }
            } else if (cmd == ID_OPEN_BTN_DELETE) {
                if (g_open_dlg_data->has_selection && g_open_dlg_data->selected_name[0] != '\0') {
                    char header[128];
                    snprintf(header, sizeof(header), "Permanently Delete '%s'?", g_open_dlg_data->selected_name);
                    char body[512];
                    snprintf(body, sizeof(body),
                             "Are you sure you want to permanently delete canvas '%s'?\n\n"
                             "This will delete the entire folder and all its files:\n%s\n\n"
                             "This action cannot be undone.",
                             g_open_dlg_data->selected_name, g_open_dlg_data->selected_folder);
                    if (show_themed_confirm_dialog(hwnd, "Confirm Canvas Deletion", header, body, true)) {
                        bool is_active_canvas = (g_has_canvas &&
                            (_stricmp(g_canvas_name, g_open_dlg_data->selected_name) == 0 ||
                             _stricmp(g_canvas_folder, g_open_dlg_data->selected_folder) == 0));

                        if (is_active_canvas) {
                            for (int p = 0; p < MAX_PATHS; ++p) {
                                bezier_path_free(&g_paths[p]);
                            }
                            g_path_count = 0;
                            g_active_path_idx = -1;
                            g_active_node_idx = -1;
                            if (g_has_bg_image) {
                                image_free(&g_canvas_img);
                                g_has_bg_image = false;
                            }
                            g_canvas_name[0] = '\0';
                            g_canvas_folder[0] = '\0';
                            g_has_canvas = false;
                            g_canvas_w = DEFAULT_CANVAS_WIDTH;
                            g_canvas_h = DEFAULT_CANVAS_HEIGHT;
                            g_has_frog = false;
                            g_frog_gx = DEFAULT_CANVAS_WIDTH / 2;
                            g_frog_gy = DEFAULT_CANVAS_HEIGHT / 2;
                            g_treasure_point_count = 0;
                            for (int i = 0; i < g_cutout_count; ++i) free_cutout(&g_cutouts[i]);
                            g_cutout_count = 0;
                            for (int i = 0; i < g_sprite_count; ++i) free_sprite(&g_sprites[i]);
                            g_sprite_count = 0;
                            for (int i = 0; i < g_bg_alpha_count; ++i) free_bgalpha(&g_bg_alphas[i]);
                            g_bg_alpha_count = 0;
                            SetWindowTextA(g_hwnd_main, "ZuPather - Zuma Path Editor");
                            update_status_bar();
                            InvalidateRect(g_hwnd_main, NULL, FALSE);
                        }

                        char double_null_path[MAX_PATH + 2];
                        memset(double_null_path, 0, sizeof(double_null_path));
                        strncpy(double_null_path, g_open_dlg_data->selected_folder, MAX_PATH);

                        SHFILEOPSTRUCTA sfop;
                        memset(&sfop, 0, sizeof(sfop));
                        sfop.hwnd = hwnd;
                        sfop.wFunc = FO_DELETE;
                        sfop.pFrom = double_null_path;
                        sfop.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
                        int res = SHFileOperationA(&sfop);

                        if (res == 0 && !sfop.fAnyOperationsAborted) {
                            populate_open_canvas_list(hwnd);
                        } else {
                            show_themed_notice_dialog(hwnd, "Delete Failed", "Folder Deletion Error",
                                "Failed to delete canvas folder.\nMake sure the folder is not in use by another program.", 480, 250);
                        }
                    }
                } else {
                    show_themed_notice_dialog(hwnd, "Notice", "No Canvas Selected", "Please select a canvas folder from the list first.", 460, 240);
                }
            } else if (cmd == ID_OPEN_BTN_SELECT) {
                if (g_open_dlg_data->has_selection) {
                    char sel_folder[MAX_PATH];
                    strncpy(sel_folder, g_open_dlg_data->selected_folder, sizeof(sel_folder) - 1);
                    sel_folder[sizeof(sel_folder) - 1] = '\0';
                    DestroyWindow(hwnd);
                    show_opening_canvas_modal(g_hwnd_main, sel_folder);
                } else {
                    show_themed_notice_dialog(hwnd, "Notice", "No Canvas Selected", "Please select a canvas folder from the list first.", 460, 240);
                }
            } else if (cmd == ID_OPEN_BTN_CANCEL) {
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_active_modal_dlg == hwnd) g_active_modal_dlg = NULL;
            if (g_open_dlg_data) {
                free(g_open_dlg_data);
                g_open_dlg_data = NULL;
            }
            EnableWindow(g_hwnd_main, TRUE);
            SetFocus(g_hwnd_main);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void show_open_canvas_dialog(HWND parent_hwnd) {
    if (g_open_dlg_data) {
        SetFocus(g_open_dlg_data->hwnd_dlg);
        return;
    }

    HINSTANCE hinst = GetModuleHandle(NULL);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = OpenCanvasModalProc;
    wc.hInstance     = hinst;
    wc.lpszClassName = "ZuPatherOpenCanvasModal";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    int dlg_w = 680;
    int dlg_h = 445;

    RECT rc_parent;
    GetWindowRect(parent_hwnd, &rc_parent);
    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;

    g_open_dlg_data = (OpenCanvasDlgData*)calloc(1, sizeof(OpenCanvasDlgData));

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherOpenCanvasModal", "Open Canvas Project",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, NULL
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    g_open_dlg_data->hwnd_dlg = hwnd_dlg;
    EnableWindow(parent_hwnd, FALSE);

    /* ListBox */
    g_open_dlg_data->hwnd_list = CreateWindowExA(
        0, "LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        24, 92, 310, 260, hwnd_dlg, (HMENU)ID_OPEN_LIST_FOLDERS, hinst, NULL
    );
    SendMessage(g_open_dlg_data->hwnd_list, LB_SETITEMHEIGHT, 0, 20);
    apply_control_dark_mode(g_open_dlg_data->hwnd_list, g_current_theme == THEME_DARK);

    /* Bottom action buttons */
    g_open_dlg_data->hwnd_btn_select = CreateWindowA(
        "BUTTON", "Select as Canvas", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        24, 365, 145, 28, hwnd_dlg, (HMENU)ID_OPEN_BTN_SELECT, hinst, NULL
    );
    g_open_dlg_data->hwnd_btn_rename = CreateWindowA(
        "BUTTON", "Rename", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        179, 365, 85, 28, hwnd_dlg, (HMENU)ID_OPEN_BTN_RENAME, hinst, NULL
    );
    g_open_dlg_data->hwnd_btn_delete = CreateWindowA(
        "BUTTON", "Delete", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        274, 365, 85, 28, hwnd_dlg, (HMENU)ID_OPEN_BTN_DELETE, hinst, NULL
    );
    g_open_dlg_data->hwnd_btn_refresh = CreateWindowA(
        "BUTTON", "Refresh", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        369, 365, 85, 28, hwnd_dlg, (HMENU)ID_OPEN_BTN_REFRESH, hinst, NULL
    );
    g_open_dlg_data->hwnd_btn_cancel = CreateWindowA(
        "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        571, 365, 85, 28, hwnd_dlg, (HMENU)ID_OPEN_BTN_CANCEL, hinst, NULL
    );

    populate_open_canvas_list(hwnd_dlg);
    SetFocus(g_open_dlg_data->hwnd_list);
}

void prompt_change_background_image(HWND hwnd) {
    if (!g_has_canvas) {
        show_new_canvas_dialog(hwnd);
        return;
    }

    OPENFILENAMEA ofn;
    char filepath[MAX_PATH] = {0};
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = ZUPATHER_MEDIA_FILTER;
    ofn.lpstrFile   = filepath;
    ofn.nMaxFile    = sizeof(filepath);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        CanvasImage new_img;
        if (image_load_from_file(filepath, &new_img)) {
            undo_ensure_baseline();
            if (g_has_bg_image) {
                image_free(&g_canvas_img);
            }
            g_canvas_img = new_img;
            g_has_bg_image = true;
            update_status_bar();
            InvalidateRect(hwnd, NULL, FALSE);
            undo_snapshot_capture("Change Background");

            if (new_img.is_video) {
                show_video_load_warning(hwnd);
            } else {
                char ext[16] = {0};
                image_get_extension(filepath, ext, sizeof(ext));
                if (_stricmp(ext, "webp") == 0) {
                    show_webp_patch_warning(hwnd);
                }
            }
        } else {
            show_themed_notice_dialog(hwnd, "Error", "Failed to Load Media", "Failed to load the selected media file.", 460, 240);
        }
    }
}

/* ========================================================================= */
/* Custom Themed Notice & Warning Modal Dialog                               */
/* Supports Dark / Light Theme, English Text, and DWM Title Bar Attributes   */
/* ========================================================================= */
#define ID_NOTICE_BTN_OK     101
#define ID_NOTICE_BTN_CANCEL 102

typedef struct {
    HWND        hwnd_dlg;
    HWND        hwnd_btn_ok;
    HWND        hwnd_btn_cancel;
    const char *title;
    const char *header;
    const char *body;
    int         dlg_w;
    int         dlg_h;
    bool        has_cancel;
    bool        result;
    bool        is_done;
} ThemedNoticeData;

static LRESULT CALLBACK ThemedNoticeModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();
    ThemedNoticeData *data = (ThemedNoticeData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTA *cs = (CREATESTRUCTA*)lParam;
            data = (ThemedNoticeData*)cs->lpCreateParams;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)data);
            if (data) data->hwnd_dlg = hwnd;

            HINSTANCE hinst = GetModuleHandle(NULL);
            HWND btn_ok = CreateWindowA(
                "BUTTON", "OK",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                0, 0, 90, 30, hwnd, (HMENU)ID_NOTICE_BTN_OK, hinst, NULL
            );
            if (data) data->hwnd_btn_ok = btn_ok;

            if (data && data->has_cancel) {
                HWND btn_cancel = CreateWindowA(
                    "BUTTON", "Cancel",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    0, 0, 90, 30, hwnd, (HMENU)ID_NOTICE_BTN_CANCEL, hinst, NULL
                );
                if (data) data->hwnd_btn_cancel = btn_cancel;
            }
            return 0;
        }

        case WM_SIZE: {
            if (data && data->hwnd_btn_ok) {
                int cw = LOWORD(lParam);
                int ch = HIWORD(lParam);
                int btn_w = 90;
                int btn_h = 28;
                int btn_y = ch - 24 - btn_h;
                if (data->has_cancel && data->hwnd_btn_cancel) {
                    int cancel_x = cw - 24 - btn_w;
                    int ok_x = cancel_x - 12 - btn_w;
                    SetWindowPos(data->hwnd_btn_ok, NULL, ok_x, btn_y, btn_w, btn_h, SWP_NOZORDER);
                    SetWindowPos(data->hwnd_btn_cancel, NULL, cancel_x, btn_y, btn_w, btn_h, SWP_NOZORDER);
                } else {
                    int btn_x = cw - 24 - btn_w;
                    SetWindowPos(data->hwnd_btn_ok, NULL, btn_x, btn_y, btn_w, btn_h, SWP_NOZORDER);
                }
            }
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc_client;
            GetClientRect(hwnd, &rc_client);

            bool is_dark = (g_current_theme == THEME_DARK);

            /* Fill solid background matching active theme */
            HBRUSH bg_brush = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc_client, bg_brush);
            DeleteObject(bg_brush);

            /* Accent circular info / warning badge on the left */
            int icon_x = 22;
            int icon_y = 20;
            int icon_d = 36;
            bool is_warn = (data && data->has_cancel);
            COLORREF badge_col = is_warn ? (is_dark ? RGB(229, 140, 30) : RGB(210, 120, 20)) : th->text_accent_blue;
            const char *badge_symbol = is_warn ? "!" : "i";

            HBRUSH badge_brush = CreateSolidBrush(badge_col);
            HPEN null_pen = (HPEN)GetStockObject(NULL_PEN);
            HPEN old_pen = (HPEN)SelectObject(hdc, null_pen);
            HBRUSH old_brush = (HBRUSH)SelectObject(hdc, badge_brush);
            Ellipse(hdc, icon_x, icon_y, icon_x + icon_d, icon_y + icon_d);
            SelectObject(hdc, old_brush);
            SelectObject(hdc, old_pen);
            DeleteObject(badge_brush);

            /* Glyph centered inside the badge */
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            HFONT hfont_icon = g_font_title_bold ? g_font_title_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT old_font = (HFONT)SelectObject(hdc, hfont_icon);
            RECT rc_icon = { icon_x, icon_y, icon_x + icon_d, icon_y + icon_d - 2 };
            DrawTextA(hdc, badge_symbol, -1, &rc_icon, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

            if (data) {
                int text_x = icon_x + icon_d + 16;
                int text_r = rc_client.right - 24;

                /* Header Title */
                SetTextColor(hdc, th->text_primary);
                HFONT hfont_hdr = g_font_title_bold ? g_font_title_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                SelectObject(hdc, hfont_hdr);
                RECT rc_hdr = { text_x, icon_y + 2, text_r, icon_y + 28 };
                if (data->header) {
                    DrawTextA(hdc, data->header, -1, &rc_hdr, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);
                }

                /* Multi-line Body text */
                SetTextColor(hdc, th->text_secondary);
                HFONT hfont_body = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                SelectObject(hdc, hfont_body);
                RECT rc_body = { text_x, icon_y + 36, text_r, rc_client.bottom - 24 - 28 - 18 };
                if (data->body) {
                    DrawTextA(hdc, data->body, -1, &rc_body, DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
                }
            }

            SelectObject(hdc, old_font);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool is_cancel = (dis->CtlID == ID_NOTICE_BTN_CANCEL);

                COLORREF bg_color;
                COLORREF border_color;
                COLORREF text_color;

                if (is_cancel) {
                    bg_color = is_pressed ? th->btn_bg_pressed : th->btn_bg;
                    border_color = th->btn_border;
                    text_color = th->btn_text;
                } else {
                    bg_color = is_pressed ? RGB(18, 70, 140) : th->text_accent_blue;
                    border_color = bg_color;
                    text_color = RGB(255, 255, 255);
                }

                HBRUSH bg_brush = CreateSolidBrush(bg_color);
                FillRect(dis->hDC, &dis->rcItem, bg_brush);
                DeleteObject(bg_brush);

                HPEN pen = CreatePen(PS_SOLID, 1, border_color);
                HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, old_pen);
                DeleteObject(pen);

                char text[32] = {0};
                GetWindowTextA(dis->hwndItem, text, sizeof(text));

                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, text_color);
                HFONT hfont_btn = (!is_cancel && g_font_primary_bold) ? g_font_primary_bold : (g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                HFONT old_font = (HFONT)SelectObject(dis->hDC, hfont_btn);

                RECT rc_t = dis->rcItem;
                if (is_pressed) { rc_t.left += 1; rc_t.top += 1; }
                DrawTextA(dis->hDC, text, -1, &rc_t, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                SelectObject(dis->hDC, old_font);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (cmd == ID_NOTICE_BTN_OK || cmd == IDOK) {
                if (data) {
                    data->result = true;
                    data->is_done = true;
                }
                DestroyWindow(hwnd);
                return 0;
            } else if (cmd == ID_NOTICE_BTN_CANCEL || cmd == IDCANCEL) {
                if (data) {
                    data->result = false;
                    data->is_done = true;
                }
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            if (data) {
                data->result = false;
                data->is_done = true;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (data) data->is_done = true;
            PostMessageA(hwnd, WM_NULL, 0, 0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

bool show_themed_notice_dialog_ex(
    HWND parent_hwnd,
    const char *title,
    const char *header,
    const char *body,
    int dlg_w,
    int dlg_h,
    bool has_cancel
) {
    HINSTANCE hinst = GetModuleHandle(NULL);
    static bool s_class_registered = false;
    if (!s_class_registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = ThemedNoticeModalProc;
        wc.hInstance     = hinst;
        wc.lpszClassName = "ZuPatherNoticeModal";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        s_class_registered = true;
    }

    if (dlg_w <= 0) dlg_w = 560;

    /* Measure header and body to fit dialog height perfectly with balanced button distance */
    int client_w = dlg_w;
    int icon_x = 22;
    int icon_y = 20;
    int icon_d = 36;
    int text_x = icon_x + icon_d + 16;
    int text_r = client_w - 24;
    int text_w = text_r - text_x;
    if (text_w < 120) text_w = 120;

    HDC screen_dc = GetDC(NULL);
    HFONT hfont_hdr = g_font_title_bold ? g_font_title_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old_f = (HFONT)SelectObject(screen_dc, hfont_hdr);
    RECT rc_calc_hdr = {0, 0, text_w, 0};
    DrawTextA(screen_dc, (header && header[0]) ? header : "", -1, &rc_calc_hdr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int hdr_h = rc_calc_hdr.bottom - rc_calc_hdr.top;
    if (hdr_h < 24) hdr_h = 24;

    HFONT hfont_body = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SelectObject(screen_dc, hfont_body);
    RECT rc_calc_b = {0, 0, text_w, 0};
    DrawTextA(screen_dc, (body && body[0]) ? body : "", -1, &rc_calc_b, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int body_h = rc_calc_b.bottom - rc_calc_b.top;
    SelectObject(screen_dc, old_f);
    ReleaseDC(NULL, screen_dc);

    int content_bottom = icon_y + hdr_h + 10 + body_h;
    if (content_bottom < icon_y + icon_d) content_bottom = icon_y + icon_d;

    /* Distance from last text line to button: 22px; button height: 28px; bottom margin: 24px */
    int client_h = content_bottom + 22 + 28 + 24;

    RECT rc_dlg = {0, 0, client_w, client_h};
    AdjustWindowRectEx(&rc_dlg, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int final_dlg_w = rc_dlg.right - rc_dlg.left;
    int final_dlg_h = rc_dlg.bottom - rc_dlg.top;

    RECT rc_parent = {0};
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        GetWindowRect(parent_hwnd, &rc_parent);
    } else {
        GetWindowRect(GetDesktopWindow(), &rc_parent);
    }
    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - final_dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - final_dlg_h) / 2;

    ThemedNoticeData data;
    memset(&data, 0, sizeof(data));
    data.title = title;
    data.header = header;
    data.body = body;
    data.dlg_w = final_dlg_w;
    data.dlg_h = final_dlg_h;
    data.has_cancel = has_cancel;
    data.result = false;
    data.is_done = false;

    HWND prev_modal = g_active_modal_dlg;
    BOOL parent_was_enabled = TRUE;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        parent_was_enabled = IsWindowEnabled(parent_hwnd);
        if (parent_was_enabled) {
            EnableWindow(parent_hwnd, FALSE);
        }
    }

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherNoticeModal",
        (title && title[0]) ? title : "Notice",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, final_dlg_w, final_dlg_h,
        parent_hwnd, NULL, hinst, &data
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    if (data.hwnd_btn_ok) {
        SetFocus(data.hwnd_btn_ok);
    }

    MSG msg;
    while (!data.is_done && IsWindow(hwnd_dlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (data.is_done) break;
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) {
                data.result = true;
                data.is_done = true;
                DestroyWindow(hwnd_dlg);
                break;
            } else if (msg.wParam == VK_ESCAPE) {
                data.result = false;
                data.is_done = true;
                DestroyWindow(hwnd_dlg);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_active_modal_dlg = prev_modal;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        if (parent_was_enabled) {
            EnableWindow(parent_hwnd, TRUE);
        }
        SetForegroundWindow(parent_hwnd);
        SetFocus(parent_hwnd);
    }

    return data.result;
}

void show_themed_notice_dialog(
    HWND parent_hwnd,
    const char *title,
    const char *header,
    const char *body,
    int dlg_w,
    int dlg_h
) {
    show_themed_notice_dialog_ex(parent_hwnd, title, header, body, dlg_w, dlg_h, false);
}

void show_video_load_warning(HWND hwnd_parent) {
    const char *title = "Video Media Notice - ZuPather";
    const char *header = "Video Animation File Loaded";
    const char *body =
        "1. Audio in video files is NOT loaded or saved (only visual video frames are processed).\n\n"
        "2. Vanilla Zuma Deluxe does NOT support video or WebP files. In-game animation playback is exclusively supported in Zuma.exe patched with \"ZuCompleluxe\" (modules\\zumawebp.dll) or in the \"Zuma Compleluxe\" mod.\n\n"
        "3. ZuCompleluxe is hosted on GitHub and will be released to the public alongside the official release of the \"Zuma Compleluxe\" mod, which is currently in active development.\n\n"
        "4. When saving the canvas project, video media will be exported as Animated WebP (.webp).\n\n"
        "5. The video will play in a continuous loop on the canvas.";

    show_themed_notice_dialog_ex(hwnd_parent, title, header, body, 580, 410, false);
}

bool show_webp_patch_warning_ex(HWND hwnd_parent, bool allow_cancel) {
    const char *title = "WebP Compatibility - ZuCompleluxe";
    const char *header = "Animated WebP Compatibility Notice";
    const char *body =
        "WebP format (specifically Animated WebP) is supported in Zuma.exe patched with the \"ZuCompleluxe\" framework (modules\\zumawebp.dll) and in the upcoming \"Zuma Compleluxe\" mod.\n\n"
        "Original vanilla Zuma Deluxe only supports standard formats (.jpg, .png, .gif) and does not natively read WebP images.\n\n"
        "To run WebP assets in-game, please ensure your Zuma Deluxe game executable is patched with ZuCompleluxe. ZuCompleluxe will be released on GitHub alongside the official launch of the Zuma Compleluxe mod.";

    return show_themed_notice_dialog_ex(hwnd_parent, title, header, body, 560, 350, allow_cancel);
}

void show_webp_patch_warning(HWND hwnd_parent) {
    show_webp_patch_warning_ex(hwnd_parent, false);
}

bool show_animation_flatten_warning(HWND hwnd_parent, const char *asset_names, bool allow_cancel) {
    char body[1536];
    snprintf(
        body, sizeof(body),
        "The following media assets contain multiple animation frames or video streams, but the selected export format is a static image (*.png / *.jpg / *.gif):\n\n"
        "%s\n"
        "Animation will NOT play in-game, and only the first still frame will be exported as the static replacement image.\n\n"
        "If you wish to preserve in-game animation, select the *.webp format and use Zuma.exe patched with ZuCompleluxe (modules\\zumawebp.dll) or the Zuma Compleluxe mod.",
        (asset_names && asset_names[0]) ? asset_names : "- Background Image\n"
    );

    const char *title = "Static Image Conversion Warning";
    const char *header = "Animated Media Flattened to Still Image";
    return show_themed_notice_dialog_ex(hwnd_parent, title, header, body, 580, 390, allow_cancel);
}

bool show_extra_paths_warning_ex(HWND hwnd_parent, int path_count, bool allow_cancel) {
    char body[1024];
    snprintf(
        body, sizeof(body),
        "This level contains %d curve paths (tracks).\n\n"
        "1. Vanilla Zuma Deluxe natively only supports up to 3 curve paths per level (curve, curve2, curve3). Any 4th curve or beyond is completely ignored by vanilla Zuma.exe.\n\n"
        "2. Having 4 to 10 curve paths (extra tracks) is supported when using Zuma.exe patched with the \"ZuCompleluxe\" framework (modules\\zumaextrapath.dll) or playing on the upcoming \"Zuma Compleluxe\" mod.\n\n"
        "3. ZuCompleluxe is hosted on GitHub and will be released to the public alongside the official release of the \"Zuma Compleluxe\" mod, which is currently in active development.\n\n"
        "Please ensure your target game is patched with ZuCompleluxe to play all %d curve tracks in-game.",
        path_count,
        path_count
    );

    const char *title = "Extra Paths Compatibility - ZuCompleluxe";
    const char *header = "Multi-Track Level Compatibility (4 to 10 Paths)";
    return show_themed_notice_dialog_ex(hwnd_parent, title, header, body, 580, 390, allow_cancel);
}

void show_extra_paths_warning(HWND hwnd_parent, int path_count) {
    show_extra_paths_warning_ex(hwnd_parent, path_count, false);
}

bool show_path_limit_warning_ex(HWND hwnd_parent, int total_count, bool is_import) {
    char body[1024];
    if (is_import) {
        int excess = total_count - 10;
        snprintf(
            body, sizeof(body),
            "The imported file contains %d curve paths (tracks).\n\n"
            "1. The ZuCompleluxe framework (modules\\zumaextrapath.dll) and Zuma Compleluxe support a maximum limit of 10 curve tracks per level.\n\n"
            "2. Vanilla Zuma Deluxe natively only reads up to 3 paths, while ZuCompleluxe expands capacity up to 10 paths.\n\n"
            "3. Only the first 10 paths will be preserved. Paths #11 onwards (%d excess path%s) will be automatically removed.\n\n"
            "Click OK to proceed with the first 10 paths.",
            total_count,
            excess,
            excess > 1 ? "s" : ""
        );
    } else {
        snprintf(
            body, sizeof(body),
            "You have reached the maximum limit of 10 curve paths (tracks).\n\n"
            "1. The ZuCompleluxe framework (modules\\zumaextrapath.dll) and Zuma Compleluxe support a maximum of 10 curve tracks per level (curve, curve2..curve10).\n\n"
            "2. Vanilla Zuma Deluxe natively only supports up to 3 paths, ignoring any 4th track onwards.\n\n"
            "3. Adding an 11th path or beyond exceeds the engine allocation limit and has been blocked.\n\n"
            "To create or split another path, please delete one of your existing paths first."
        );
    }

    const char *title = "Curve Path Limit - ZuCompleluxe";
    const char *header = "Maximum 10 Curve Paths Limit";
    return show_themed_notice_dialog_ex(hwnd_parent, title, header, body, 580, 360, false);
}

void show_path_limit_warning(HWND hwnd_parent) {
    show_path_limit_warning_ex(hwnd_parent, 11, false);
}

void show_import_path_limit_warning(HWND hwnd_parent, int canvas_count, int imported_count, bool replace) {
    char body[1024];

    if (replace) {
        int excess = imported_count - 10;
        if (canvas_count > 0) {
            snprintf(
                body, sizeof(body),
                "The imported SVG file contains %d curve paths (tracks):\n\n"
                "- Canvas project paths: Replaced (%d existing track%s cleared)\n"
                "- Imported SVG tracks: %d paths\n"
                "- Total resulting tracks: %d paths (exceeds limit by %d)\n\n"
                "Path Handling & Resolution:\n"
                "The ZuCompleluxe framework and Zuma Compleluxe support up to 10 curve tracks per level (vanilla Zuma Deluxe natively only reads up to 3). Only the first 10 paths from the imported SVG (tracks #1 to #10) will be kept. The remaining %d excess path%s (tracks #11 to #%d) will be automatically discarded.\n\n"
                "Click OK to proceed with the preserved 10 tracks.",
                imported_count,
                canvas_count, canvas_count > 1 ? "s" : "",
                imported_count,
                imported_count, excess,
                excess, excess > 1 ? "s" : "", imported_count
            );
        } else {
            snprintf(
                body, sizeof(body),
                "The imported SVG file contains %d curve paths (tracks):\n\n"
                "- Canvas project paths: None (0 existing tracks)\n"
                "- Imported SVG tracks: %d paths\n"
                "- Total resulting tracks: %d paths (exceeds limit by %d)\n\n"
                "Path Handling & Resolution:\n"
                "The ZuCompleluxe framework and Zuma Compleluxe support up to 10 curve tracks per level (vanilla Zuma Deluxe natively only reads up to 3). Only the first 10 paths from the imported SVG (tracks #1 to #10) will be kept. The remaining %d excess path%s (tracks #11 to #%d) will be automatically discarded.\n\n"
                "Click OK to proceed with the preserved 10 tracks.",
                imported_count,
                imported_count,
                imported_count, excess,
                excess, excess > 1 ? "s" : "", imported_count
            );
        }
    } else {
        /* Append mode */
        int total = canvas_count + imported_count;
        int can_add = 10 - canvas_count;
        if (can_add < 0) can_add = 0;
        int excess = imported_count - can_add;

        if (can_add > 0) {
            snprintf(
                body, sizeof(body),
                "The combined curve tracks exceed the maximum limit of 10 paths:\n\n"
                "- Canvas project currently has: %d path%s\n"
                "- Imported SVG file contains: %d path%s\n"
                "- Total combined tracks: %d paths (exceeds limit by %d)\n\n"
                "Path Handling & Resolution:\n"
                "All %d existing path%s in the canvas will be kept. To comply with the 10-path limit, only the first %d path%s from the imported SVG will be added (SVG tracks #1 to #%d). The remaining %d path%s from the SVG (SVG tracks #%d to #%d) will be automatically discarded.\n\n"
                "Click OK to proceed with the preserved 10 tracks.",
                canvas_count, canvas_count > 1 ? "s" : "",
                imported_count, imported_count > 1 ? "s" : "",
                total, excess,
                canvas_count, canvas_count > 1 ? "s" : "",
                can_add, can_add > 1 ? "s" : "", can_add,
                excess, excess > 1 ? "s" : "", can_add + 1, imported_count
            );
        } else {
            /* Canvas already had 10 paths */
            snprintf(
                body, sizeof(body),
                "The canvas project already has %d curve paths (the maximum limit):\n\n"
                "- Canvas project currently has: %d paths\n"
                "- Imported SVG file contains: %d path%s\n"
                "- Total combined tracks: %d paths\n\n"
                "Path Handling & Resolution:\n"
                "All %d existing paths in the canvas will be kept. None of the %d path%s from the imported SVG can be added, and all imported tracks will be discarded.\n\n"
                "To import new tracks from SVG, please delete existing paths in the canvas first.",
                canvas_count,
                canvas_count,
                imported_count, imported_count > 1 ? "s" : "",
                total,
                canvas_count,
                imported_count, imported_count > 1 ? "s" : ""
            );
        }
    }

    const char *title = "Curve Path Limit - ZuCompleluxe";
    const char *header = "Maximum 10 Curve Paths Limit";
    show_themed_notice_dialog_ex(hwnd_parent, title, header, body, 620, 440, false);
}

static void format_calc_time_str(double sec, char *out_buf, size_t sz) {
    if (sec < 0.0) sec = 0.0;
    int total_s = (int)sec;
    int mins = total_s / 60;
    double rem_s = sec - (mins * 60);
    if (mins > 0) {
        snprintf(out_buf, sz, "%02d:%04.1f (%0.1fs)", mins, rem_s, sec);
    } else {
        snprintf(out_buf, sz, "%04.1fs", sec);
    }
}

static void format_calc_number_str(int num, char *out_buf, size_t sz) {
    if (num >= 1000000) {
        snprintf(out_buf, sz, "%d,%03d,%03d", num / 1000000, (num % 1000000) / 1000, num % 1000);
    } else if (num >= 1000) {
        snprintf(out_buf, sz, "%d,%03d", num / 1000, num % 1000);
    } else {
        snprintf(out_buf, sz, "%d", num);
    }
}

static void format_calc_ram_str(uint64_t bytes, char *out_buf, size_t sz) {
    double mb = (double)bytes / (1024.0 * 1024.0);
    if (mb >= 1000.0) {
        double gb = mb / 1024.0;
        snprintf(out_buf, sz, "%.2f GB", gb);
    } else if (mb >= 1.0) {
        snprintf(out_buf, sz, "%.1f MB", mb);
    } else {
        double kb = (double)bytes / 1024.0;
        snprintf(out_buf, sz, "%.1f KB", kb);
    }
}

void calculate_canvas_animation_budget(const SaveCanvasAssetPlan *plan, CanvasAnimationBudgetReport *report) {
    if (!report) return;
    memset(report, 0, sizeof(*report));
    report->ram_budget_bytes = CANVAS_MAX_SAFE_ANIMATION_RAM_BYTES;
    if (!plan || plan->count <= 0) return;

    int bg_index_in_report = -1;

    for (int i = 0; i < plan->count; ++i) {
        const SaveCanvasAssetItem *it = &plan->items[i];
        ImageExportFormat it_fmt = it->keep_orig ? IMG_EXPORT_KEEP_ORIGINAL : it->selected_format;
        bool is_target_webp = (it_fmt == IMG_EXPORT_WEBP ||
                               (it_fmt == IMG_EXPORT_KEEP_ORIGINAL && _stricmp(it->orig_ext, "webp") == 0));
        if (!is_target_webp) continue;

        bool is_video = it->is_video || image_is_video_ext(it->source_path);
        bool is_anim = it->is_animated || (it->frame_count > 1);

        if (!is_video && !is_anim) continue;

        double dur_sec = 0.0;
        int fps = 30;
        int total_frames = 0;

        if (is_video && it->source_path[0] != '\0') {
            if (!get_video_stream_info(it->source_path, &dur_sec, &fps, &total_frames)) {
                total_frames = it->frame_count > 0 ? it->frame_count : 30;
                fps = 30;
                dur_sec = (double)total_frames / (double)fps;
            }
        } else {
            total_frames = it->frame_count > 0 ? it->frame_count : 1;
            fps = 30;
            dur_sec = (double)total_frames / (double)fps;
        }

        if (total_frames <= 1) continue;

        int target_w = it->width;
        int target_h = it->height;
        if (it->type == SAVE_ASSET_BACKGROUND) {
            target_w = (g_canvas_w > 0) ? g_canvas_w : it->width;
            target_h = (g_canvas_h > 0) ? g_canvas_h : it->height;
            if (target_w <= 0) target_w = g_canvas_w;
            if (target_h <= 0) target_h = g_canvas_h;
        }
        if (target_w <= 0) target_w = 640;
        if (target_h <= 0) target_h = 480;

        if (report->count < MAX_SAVE_CANVAS_ASSETS) {
            CanvasAnimationAssetStat *st = &report->assets[report->count];
            st->plan_index = i;
            st->type = it->type;
            strncpy(st->label, it->label, sizeof(st->label) - 1);
            strncpy(st->id_name, it->id_name, sizeof(st->id_name) - 1);

            const char *slash = strrchr(it->source_path, '\\');
            if (!slash) slash = strrchr(it->source_path, '/');
            const char *fname = slash ? (slash + 1) : it->source_path;
            if (fname[0] == '\0') fname = it->id_name;
            strncpy(st->filename, fname, sizeof(st->filename) - 1);

            st->target_w = target_w;
            st->target_h = target_h;
            st->orig_fps = (fps > 0) ? fps : 30;
            st->orig_frames = total_frames;
            st->orig_duration_sec = dur_sec;
            st->ram_per_frame = (uint64_t)target_w * (uint64_t)target_h * 4ULL;
            st->orig_ram_bytes = st->ram_per_frame * (uint64_t)total_frames;
            st->capped_frames = total_frames;
            st->capped_duration_sec = dur_sec;
            st->capped_ram_bytes = st->orig_ram_bytes;
            st->is_capped = false;

            if (it->type == SAVE_ASSET_BACKGROUND) {
                bg_index_in_report = report->count;
            }

            report->total_orig_ram_bytes += st->orig_ram_bytes;
            report->total_capped_ram_bytes += st->capped_ram_bytes;
            report->count++;
        }
    }

    if (report->count <= 0) return;

    /* 2. Check if total memory fits within safe pool */
    if (report->total_orig_ram_bytes <= report->ram_budget_bytes) {
        report->exceeds_budget = false;
        return;
    }

    report->exceeds_budget = true;

    /* 3. Weighted Proportional Allocation */
    uint64_t bg_budget = 0;
    uint64_t overlays_budget = 0;

    if (bg_index_in_report >= 0) {
        if (report->count == 1) {
            bg_budget = report->ram_budget_bytes;
            overlays_budget = 0;
        } else {
            bg_budget = (report->ram_budget_bytes * 55ULL) / 100ULL; /* 55% for Background */
            overlays_budget = report->ram_budget_bytes - bg_budget;  /* 45% for Overlays */
        }
    } else {
        bg_budget = 0;
        overlays_budget = report->ram_budget_bytes; /* 100% for Overlays */
    }

    /* Cap Background if needed */
    if (bg_index_in_report >= 0) {
        CanvasAnimationAssetStat *bg_st = &report->assets[bg_index_in_report];
        if (bg_st->orig_ram_bytes > bg_budget && bg_st->ram_per_frame > 0) {
            int max_f = (int)(bg_budget / bg_st->ram_per_frame);
            int min_f = (bg_st->orig_fps * 3); /* Guarantee at least 3 seconds loop */
            if (min_f < 60) min_f = 60;
            if (max_f < min_f) max_f = min_f;
            if (max_f > bg_st->orig_frames) max_f = bg_st->orig_frames;

            bg_st->capped_frames = max_f;
            bg_st->capped_ram_bytes = (uint64_t)max_f * bg_st->ram_per_frame;
            bg_st->capped_duration_sec = (double)max_f / (double)bg_st->orig_fps;
            bg_st->is_capped = (bg_st->capped_frames < bg_st->orig_frames);
        }
    }

    /* Cap Overlays: Preserve small sprites (< 20 MB RAM) at 100% full duration */
    if (overlays_budget > 0) {
        uint64_t preserved_overlays_ram = 0;
        uint64_t heavy_overlays_orig_ram = 0;
        int heavy_count = 0;

        for (int i = 0; i < report->count; ++i) {
            if (i == bg_index_in_report) continue;
            CanvasAnimationAssetStat *st = &report->assets[i];
            /* If overlay is light (< 20 MB), keep 100% */
            if (st->orig_ram_bytes <= 20ULL * 1024ULL * 1024ULL) {
                preserved_overlays_ram += st->orig_ram_bytes;
            } else {
                heavy_overlays_orig_ram += st->orig_ram_bytes;
                heavy_count++;
            }
        }

        uint64_t remaining_overlays_budget = (overlays_budget > preserved_overlays_ram)
                                             ? (overlays_budget - preserved_overlays_ram) : (20ULL * 1024ULL * 1024ULL);

        if (heavy_count > 0 && heavy_overlays_orig_ram > 0) {
            for (int i = 0; i < report->count; ++i) {
                if (i == bg_index_in_report) continue;
                CanvasAnimationAssetStat *st = &report->assets[i];
                if (st->orig_ram_bytes > 20ULL * 1024ULL * 1024ULL) {
                    /* Allocate share proportionally */
                    double ratio = (double)st->orig_ram_bytes / (double)heavy_overlays_orig_ram;
                    uint64_t item_budget = (uint64_t)(ratio * (double)remaining_overlays_budget);
                    if (st->ram_per_frame > 0) {
                        int max_f = (int)(item_budget / st->ram_per_frame);
                        int min_f = (st->orig_fps * 2); /* Guarantee at least 2 seconds loop */
                        if (min_f < 60) min_f = 60;
                        if (max_f < min_f) max_f = min_f;
                        if (max_f > st->orig_frames) max_f = st->orig_frames;

                        st->capped_frames = max_f;
                        st->capped_ram_bytes = (uint64_t)max_f * st->ram_per_frame;
                        st->capped_duration_sec = (double)max_f / (double)st->orig_fps;
                        st->is_capped = (st->capped_frames < st->orig_frames);
                    }
                }
            }
        }
    }

    /* Recalculate total capped RAM */
    report->total_capped_ram_bytes = 0;
    for (int i = 0; i < report->count; ++i) {
        report->total_capped_ram_bytes += report->assets[i].capped_ram_bytes;
    }
}

bool check_and_show_webp_limit_warning(HWND hwnd_parent, SaveCanvasAssetPlan *plan, bool allow_cancel) {
    if (!plan || plan->count <= 0) return true;

    CanvasAnimationBudgetReport report;
    calculate_canvas_animation_budget(plan, &report);

    if (!report.exceeds_budget) {
        /* No capping needed - set max_export_frames to 0 (uncapped) for all */
        for (int i = 0; i < plan->count; ++i) {
            plan->items[i].max_export_frames = 0;
        }
        return true;
    }

    char orig_total_str[32], capped_total_str[32], budget_str[32];
    format_calc_ram_str(report.total_orig_ram_bytes, orig_total_str, sizeof(orig_total_str));
    format_calc_ram_str(report.total_capped_ram_bytes, capped_total_str, sizeof(capped_total_str));
    format_calc_ram_str(report.ram_budget_bytes, budget_str, sizeof(budget_str));

    char body[3584];
    char list_buf[2560] = {0};

    for (int k = 0; k < report.count; ++k) {
        CanvasAnimationAssetStat *st = &report.assets[k];
        char orig_dur_str[32], cap_dur_str[32];
        char orig_frames_str[32], cap_frames_str[32];
        char orig_ram_str[32], cap_ram_str[32];

        format_calc_time_str(st->orig_duration_sec, orig_dur_str, sizeof(orig_dur_str));
        format_calc_time_str(st->capped_duration_sec, cap_dur_str, sizeof(cap_dur_str));
        format_calc_number_str(st->orig_frames, orig_frames_str, sizeof(orig_frames_str));
        format_calc_number_str(st->capped_frames, cap_frames_str, sizeof(cap_frames_str));
        format_calc_ram_str(st->orig_ram_bytes, orig_ram_str, sizeof(orig_ram_str));
        format_calc_ram_str(st->capped_ram_bytes, cap_ram_str, sizeof(cap_ram_str));

        char item_line[384];
        if (st->is_capped) {
            snprintf(item_line, sizeof(item_line),
                "- %s (%s, %dx%d px) - %s:\n"
                "   - Original : %s @ %d FPS (~%s frames, %s RAM)\n"
                "   - Capped   : %s @ %d FPS (%s frames looping, %s RAM)\n\n",
                st->label, st->id_name, st->target_w, st->target_h, st->filename,
                orig_dur_str, st->orig_fps, orig_frames_str, orig_ram_str,
                cap_dur_str, st->orig_fps, cap_frames_str, cap_ram_str
            );
        } else {
            snprintf(item_line, sizeof(item_line),
                "- %s (%s, %dx%d px) - %s:\n"
                "   - 100%% Full Duration Preserved: %s @ %d FPS (%s frames, %s RAM)\n\n",
                st->label, st->id_name, st->target_w, st->target_h, st->filename,
                orig_dur_str, st->orig_fps, orig_frames_str, orig_ram_str
            );
        }
        strncat(list_buf, item_line, sizeof(list_buf) - strlen(list_buf) - 1);
    }

    snprintf(body, sizeof(body),
        "The combined animation assets in this canvas require ~%s of RAM, which exceeds the safe budget for Zuma Deluxe's 32-bit engine (%s max pool).\n\n"
        "Multi-Asset Safe Balancing:\n"
        "%s"
        "Total Optimized RAM: %s / %s Safe Pool.\n\n"
        "Technical Notice:\n"
        "Zuma Deluxe is a 32-bit application with a strict 2 GB address space. WebP animation frames are decoded uncompressed in system memory. ZuPather dynamically balances memory across all active animations to prevent Out of Memory crashes while preserving small sprites at full duration.",
        orig_total_str, budget_str, list_buf, capped_total_str, budget_str
    );

    int dlg_w = 660;
    int dlg_h = 420 + report.count * 44;
    if (dlg_h > 620) dlg_h = 620;

    bool proceed = show_themed_notice_dialog_ex(
        hwnd_parent,
        "WebP Memory Budget Limit Notice",
        "Canvas Animation Memory Limit Exceeded",
        body,
        dlg_w,
        dlg_h,
        allow_cancel
    );

    if (proceed) {
        /* User agreed to apply safe caps! Write capped_frames to plan */
        for (int k = 0; k < report.count; ++k) {
            CanvasAnimationAssetStat *st = &report.assets[k];
            if (st->plan_index >= 0 && st->plan_index < plan->count) {
                plan->items[st->plan_index].max_export_frames = st->capped_frames;
            }
        }
        return true;
    }

    return false;
}

/* ========================================================================= */
/* Custom Themed Confirmation Modal Dialog                                   */
/* Supports Dark / Light Theme, Danger Action, and DWM Title Bar Attributes  */
/* ========================================================================= */
#define ID_CONFIRM_BTN_YES    101
#define ID_CONFIRM_BTN_NO     102
#define ID_CONFIRM_BTN_CANCEL 103

typedef struct {
    HWND        hwnd_dlg;
    HWND        hwnd_btn_yes;
    HWND        hwnd_btn_no;
    HWND        hwnd_btn_cancel;
    const char *title;
    const char *header;
    const char *body;
    const char *btn1_label;
    const char *btn2_label;
    const char *btn3_label;
    bool        is_danger;
    int         choice_result; /* 1 = btn1, 2 = btn2, 0 = cancel / closed */
    bool        result;
    bool        is_done;
} ThemedConfirmData;

static LRESULT CALLBACK ThemedConfirmModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();
    ThemedConfirmData *data = (ThemedConfirmData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTA *cs = (CREATESTRUCTA*)lParam;
            data = (ThemedConfirmData*)cs->lpCreateParams;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)data);
            if (data) data->hwnd_dlg = hwnd;

            HINSTANCE hinst = GetModuleHandle(NULL);
            const char *label1 = (data && data->btn1_label) ? data->btn1_label : ((data && data->is_danger) ? "Delete" : "Yes");
            const char *label2 = (data && data->btn2_label) ? data->btn2_label : "No";
            const char *label3 = (data && data->btn3_label) ? data->btn3_label : NULL;

            HWND btn_yes = CreateWindowA(
                "BUTTON", label1,
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                0, 0, 88, 28, hwnd, (HMENU)ID_CONFIRM_BTN_YES, hinst, NULL
            );
            HWND btn_no = CreateWindowA(
                "BUTTON", label2,
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                0, 0, 88, 28, hwnd, (HMENU)ID_CONFIRM_BTN_NO, hinst, NULL
            );
            HWND btn_cancel = NULL;
            if (label3 && label3[0]) {
                btn_cancel = CreateWindowA(
                    "BUTTON", label3,
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    0, 0, 88, 28, hwnd, (HMENU)ID_CONFIRM_BTN_CANCEL, hinst, NULL
                );
            }

            if (data) {
                data->hwnd_btn_yes = btn_yes;
                data->hwnd_btn_no = btn_no;
                data->hwnd_btn_cancel = btn_cancel;
            }
            SetFocus(btn_no); /* Default focus to No/Secondary button for safety */
            return 0;
        }

        case WM_SIZE: {
            if (data && data->hwnd_btn_yes && data->hwnd_btn_no) {
                int cw = LOWORD(lParam);
                int ch = HIWORD(lParam);
                int btn_w = 100;
                int btn_h = 28;
                int btn_y = ch - 14 - btn_h;
                if (data->hwnd_btn_cancel) {
                    int c_x = cw - 20 - btn_w;
                    int no_x = c_x - 10 - btn_w;
                    int yes_x = no_x - 10 - btn_w;
                    SetWindowPos(data->hwnd_btn_yes, NULL, yes_x, btn_y, btn_w, btn_h, SWP_NOZORDER);
                    SetWindowPos(data->hwnd_btn_no, NULL, no_x, btn_y, btn_w, btn_h, SWP_NOZORDER);
                    SetWindowPos(data->hwnd_btn_cancel, NULL, c_x, btn_y, btn_w, btn_h, SWP_NOZORDER);
                } else {
                    int no_x = cw - 20 - btn_w;
                    int yes_x = no_x - 10 - btn_w;
                    SetWindowPos(data->hwnd_btn_yes, NULL, yes_x, btn_y, btn_w, btn_h, SWP_NOZORDER);
                    SetWindowPos(data->hwnd_btn_no, NULL, no_x, btn_y, btn_w, btn_h, SWP_NOZORDER);
                }
            }
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc_client;
            GetClientRect(hwnd, &rc_client);

            bool is_dark = (g_current_theme == THEME_DARK);
            COLORREF warn_col = is_dark ? RGB(229, 140, 30) : RGB(210, 120, 20);

            HBRUSH bg_brush = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc_client, bg_brush);
            DeleteObject(bg_brush);

            int sep_y = rc_client.bottom - 50;

            int icon_x = 22;
            int icon_y = 20;
            int icon_d = 38;

            HBRUSH badge_brush = CreateSolidBrush(warn_col);
            HPEN null_pen = (HPEN)GetStockObject(NULL_PEN);
            HPEN old_pen = (HPEN)SelectObject(hdc, null_pen);
            HBRUSH old_brush = (HBRUSH)SelectObject(hdc, badge_brush);
            Ellipse(hdc, icon_x, icon_y, icon_x + icon_d, icon_y + icon_d);
            SelectObject(hdc, old_brush);
            SelectObject(hdc, old_pen);
            DeleteObject(badge_brush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            HFONT hfont_icon = g_font_primary_bold ? g_font_primary_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT old_font = (HFONT)SelectObject(hdc, hfont_icon);
            RECT rc_icon = { icon_x, icon_y, icon_x + icon_d, icon_y + icon_d - 2 };
            DrawTextA(hdc, "!", -1, &rc_icon, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

            if (data) {
                int text_x = icon_x + icon_d + 16;
                int text_r = rc_client.right - 24;

                SetTextColor(hdc, th->text_primary);
                SelectObject(hdc, g_font_primary_bold ? g_font_primary_bold : hfont_icon);
                RECT rc_hdr = { text_x, icon_y + 2, text_r, icon_y + 28 };
                if (data->header) {
                    DrawTextA(hdc, data->header, -1, &rc_hdr, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);
                }

                SetTextColor(hdc, th->text_secondary);
                SelectObject(hdc, g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                RECT rc_body = { text_x, icon_y + 34, text_r, sep_y - 8 };
                if (data->body) {
                    DrawTextA(hdc, data->body, -1, &rc_body, DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
                }
            }

            SelectObject(hdc, old_font);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool is_yes = (dis->CtlID == ID_CONFIRM_BTN_YES);

                COLORREF bg_col;
                COLORREF border_col;
                COLORREF text_col;

                if (is_yes) {
                    if (data && data->is_danger) {
                        bg_col = is_pressed ? RGB(160, 35, 30) : RGB(210, 48, 40);
                        border_col = bg_col;
                    } else {
                        bg_col = is_pressed ? RGB(18, 70, 140) : th->text_accent_blue;
                        border_col = bg_col;
                    }
                    text_col = RGB(255, 255, 255);
                } else {
                    bg_col = is_pressed ? th->btn_bg_pressed : th->btn_bg;
                    border_col = th->btn_border;
                    text_col = th->btn_text;
                }

                HBRUSH bg_brush = CreateSolidBrush(bg_col);
                FillRect(dis->hDC, &dis->rcItem, bg_brush);
                DeleteObject(bg_brush);

                HPEN pen = CreatePen(PS_SOLID, 1, border_col);
                HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, old_pen);
                DeleteObject(pen);

                char text[32] = {0};
                GetWindowTextA(dis->hwndItem, text, sizeof(text));

                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, text_col);
                HFONT hfont_btn = (is_yes && data && data->is_danger) ? g_font_primary_bold : g_font_primary_regular;
                if (!hfont_btn) hfont_btn = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HFONT old_font = (HFONT)SelectObject(dis->hDC, hfont_btn);

                RECT rc_t = dis->rcItem;
                if (is_pressed) { rc_t.left += 1; rc_t.top += 1; }
                DrawTextA(dis->hDC, text, -1, &rc_t, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                SelectObject(dis->hDC, old_font);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (cmd == ID_CONFIRM_BTN_YES) {
                if (data) {
                    data->result = true;
                    data->choice_result = 1;
                    data->is_done = true;
                }
                DestroyWindow(hwnd);
                return 0;
            } else if (cmd == ID_CONFIRM_BTN_NO) {
                if (data) {
                    data->result = false;
                    data->choice_result = 2;
                    data->is_done = true;
                }
                DestroyWindow(hwnd);
                return 0;
            } else if (cmd == ID_CONFIRM_BTN_CANCEL || cmd == IDCANCEL) {
                if (data) {
                    data->result = false;
                    data->choice_result = 0;
                    data->is_done = true;
                }
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            if (data) {
                data->result = false;
                data->choice_result = 0;
                data->is_done = true;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (data) data->is_done = true;
            PostMessageA(hwnd, WM_NULL, 0, 0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int show_themed_choice_dialog_ex(
    HWND parent_hwnd,
    const char *title,
    const char *header,
    const char *body,
    const char *btn1_label,
    const char *btn2_label,
    const char *btn3_label,
    int dlg_w,
    int dlg_h
) {
    HINSTANCE hinst = GetModuleHandle(NULL);
    static bool s_class_registered = false;
    if (!s_class_registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = ThemedConfirmModalProc;
        wc.hInstance     = hinst;
        wc.lpszClassName = "ZuPatherConfirmModal";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        s_class_registered = true;
    }

    if (dlg_w <= 0) dlg_w = (btn3_label && btn3_label[0]) ? 560 : 520;
    if (dlg_h <= 0) dlg_h = 240;

    RECT rc_parent = {0};
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        GetWindowRect(parent_hwnd, &rc_parent);
    } else {
        GetWindowRect(GetDesktopWindow(), &rc_parent);
    }
    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;

    ThemedConfirmData data;
    memset(&data, 0, sizeof(data));
    data.title = title;
    data.header = header;
    data.body = body;
    data.btn1_label = btn1_label;
    data.btn2_label = btn2_label;
    data.btn3_label = btn3_label;
    data.is_danger = false;
    data.choice_result = 0;
    data.result = false;
    data.is_done = false;

    HWND prev_modal = g_active_modal_dlg;
    BOOL parent_was_enabled = TRUE;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        parent_was_enabled = IsWindowEnabled(parent_hwnd);
        if (parent_was_enabled) {
            EnableWindow(parent_hwnd, FALSE);
        }
    }

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherConfirmModal",
        (title && title[0]) ? title : "Confirmation",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, &data
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    if (data.hwnd_btn_no) {
        SetFocus(data.hwnd_btn_no);
    }

    MSG msg;
    while (!data.is_done && IsWindow(hwnd_dlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (data.is_done) break;
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_ESCAPE) {
                data.choice_result = 0;
                data.result = false;
                data.is_done = true;
                DestroyWindow(hwnd_dlg);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_active_modal_dlg = prev_modal;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        if (parent_was_enabled) {
            EnableWindow(parent_hwnd, TRUE);
        }
        SetForegroundWindow(parent_hwnd);
        SetFocus(parent_hwnd);
    }
    return data.choice_result;
}

int show_themed_choice_dialog(
    HWND parent_hwnd,
    const char *title,
    const char *header,
    const char *body,
    const char *btn1_label,
    const char *btn2_label,
    const char *btn3_label
) {
    return show_themed_choice_dialog_ex(parent_hwnd, title, header, body, btn1_label, btn2_label, btn3_label, 0, 0);
}

bool show_themed_confirm_dialog(
    HWND parent_hwnd,
    const char *title,
    const char *header,
    const char *body,
    bool is_danger
) {
    int res = show_themed_choice_dialog(
        parent_hwnd,
        title,
        header,
        body,
        is_danger ? "Delete" : "Yes",
        "No",
        NULL
    );
    return (res == 1);
}

/* ========================================================================= */
/* ========================================================================= */
/* Manage Levels Modal (Zuma Deluxe Levels & Canvas Projects Bridge)        */
/* ========================================================================= */

typedef enum {
    ZUMA_MAP_ORIGINAL = 0, /* Putih (White): Terdaftar di StageProgression resmi Zuma (Protected) */
    ZUMA_MAP_MISC     = 1, /* Oranye (Orange): Punya <Graphics> di levels.xml tapi tidak di StageProgression & bukan zupath */
    ZUMA_MAP_CUSTOM   = 2  /* Hijau (Green): Level progression adalah zupath[n]-[diff] (dibuat/diekspor oleh ZuPather) */
} ZumaMapType;

typedef struct {
    char id[64];
    char dispname[128];
    ZumaMapType type;
    char prog_id[64];
} ZumaLevelEntry;

typedef struct {
    char name[64];
    char title[128];
} CanvasLevelEntry;

typedef struct {
    HWND hwnd_dlg;
    HWND hwnd_btn_set_folder;
    HWND hwnd_list_zuma;
    HWND hwnd_btn_import;
    HWND hwnd_list_canvases;
    HWND hwnd_btn_export;
    HWND hwnd_btn_close;

    HWND hwnd_tooltip;
    WNDPROC orig_zuma_list_proc;
    WNDPROC orig_canvas_list_proc;
    int last_hover_zuma_idx;

    int zuma_level_count;
    ZumaLevelEntry zuma_levels[128];
    int selected_zuma_idx;

    int canvas_count;
    CanvasLevelEntry canvas_entries[128];
    int selected_canvas_idx;
} ManageLevelsDlgData;

static bool get_loose_xml_attr_str(const char *text, const char *attr_name, char *out_val, size_t max_len);
static int find_index_in_comma_list(const char *list, const char *target);
static bool export_canvas_to_zuma_map(HWND parent_hwnd, const char *canvas_name);
static void populate_manage_canvas_list(HWND hwnd_dlg);
static void populate_manage_zuma_levels(HWND hwnd_dlg);
static void show_rename_canvas_and_export_dialog(HWND parent_hwnd, const char *canvas_name);
static void show_rename_zuma_map_dialog(HWND parent_hwnd, const char *old_id);
static void handle_delete_zuma_map(HWND parent_hwnd, const char *level_id);
static void show_zuma_map_context_menu(HWND parent_hwnd, int idx, int x, int y);
static void show_canvas_list_context_menu(HWND parent_hwnd, int idx, int x, int y);
static LRESULT CALLBACK ManageZumaListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK ManageCanvasListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static ManageLevelsDlgData *g_manage_dlg_data = NULL;

static bool is_valid_zuma_level_id(const char *name) {
    if (!name || name[0] == '\0') return false;
    size_t len = strlen(name);
    if (len > 32) return false;
    /* Must start with lowercase letter [a-z] (strictly no uppercase, digits, or symbols at start) */
    if (name[0] < 'a' || name[0] > 'z') return false;
    for (size_t i = 0; i < len; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

static bool get_xml_attr_str(const char *tag, const char *attr_name, char *out_val, size_t max_len) {
    if (!tag || !attr_name || !out_val || max_len == 0) return false;
    out_val[0] = '\0';
    size_t nlen = strlen(attr_name);
    const char *p = tag;
    while ((p = strstr(p, attr_name)) != NULL) {
        if (p > tag && (isalnum((unsigned char)*(p - 1)) || *(p - 1) == '_')) {
            p += nlen;
            continue;
        }
        const char *after = p + nlen;
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') after++;
        if (*after == '=') {
            after++;
            while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') after++;
            if (*after == '"' || *after == '\'') {
                char quote = *after;
                after++;
                const char *end = strchr(after, quote);
                if (!end) return false;
                size_t len = (size_t)(end - after);
                if (len >= max_len) len = max_len - 1;
                strncpy(out_val, after, len);
                out_val[len] = '\0';
                return true;
            } else {
                const char *end = after;
                while (*end && *end != ' ' && *end != '\t' && *end != '>' && *end != '/' && *end != '\r' && *end != '\n') end++;
                size_t len = (size_t)(end - after);
                if (len >= max_len) len = max_len - 1;
                strncpy(out_val, after, len);
                out_val[len] = '\0';
                return (len > 0);
            }
        }
        p += nlen;
    }
    return false;
}

static bool get_xml_attr_int(const char *tag, const char *attr_name, int *out_val) {
    char val[64];
    if (get_xml_attr_str(tag, attr_name, val, sizeof(val))) {
        if (out_val) *out_val = atoi(val);
        return true;
    }
    return false;
}

/* Helper to check if an id is registered in StageProgression stageX lists */
static bool is_id_in_stage_progression(const char *xml_buf, const char *level_id) {
    if (!xml_buf || !level_id || !level_id[0]) return false;
    const char *sp = strstr(xml_buf, "<StageProgression");
    if (!sp) return false;
    const char *sp_end = strstr(sp, "/>");
    if (!sp_end) sp_end = strstr(sp, "</StageProgression>");
    if (!sp_end) sp_end = sp + 16384;

    for (int s = 1; s <= 25; ++s) {
        char stage_key[32];
        snprintf(stage_key, sizeof(stage_key), "stage%d", s);
        const char *st_ptr = strstr(sp, stage_key);
        if (st_ptr && st_ptr < sp_end) {
            char stage_list[1024] = {0};
            get_loose_xml_attr_str(st_ptr, stage_key, stage_list, sizeof(stage_list));
            if (find_index_in_comma_list(stage_list, level_id) >= 0) {
                return true;
            }
        }
    }
    return false;
}

/* Helper to extract level progression attribute for a map */
static bool get_zuma_level_progression(const char *xml_buf, const char *level_id, char *out_prog, size_t out_sz) {
    if (!xml_buf || !level_id || !level_id[0] || !out_prog || out_sz == 0) return false;
    out_prog[0] = '\0';

    const char *p = xml_buf;
    while ((p = strstr(p, "<Level")) != NULL) {
        char next_c = p[6];
        if (next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c == '\n') {
            const char *tag_end = strchr(p, '>');
            if (tag_end) {
                size_t tlen = (size_t)(tag_end - p + 1);
                char tag_buf[512] = {0};
                if (tlen >= sizeof(tag_buf)) tlen = sizeof(tag_buf) - 1;
                strncpy(tag_buf, p, tlen);
                tag_buf[tlen] = '\0';

                char gfx[64] = {0}, lvl_id[64] = {0}, prog[64] = {0};
                get_loose_xml_attr_str(tag_buf, "graphics", gfx, sizeof(gfx));
                get_loose_xml_attr_str(tag_buf, "id", lvl_id, sizeof(lvl_id));
                get_loose_xml_attr_str(tag_buf, "progression", prog, sizeof(prog));

                if ((gfx[0] && _stricmp(gfx, level_id) == 0) ||
                    (lvl_id[0] && _stricmp(lvl_id, level_id) == 0)) {
                    strncpy(out_prog, prog, out_sz - 1);
                    out_prog[out_sz - 1] = '\0';
                    return true;
                }
            }
        }
        p += 6;
    }
    return false;
}

/* Classify Zuma map into:
 * 1. ZUMA_MAP_ORIGINAL (White): In StageProgression and not zupath
 * 2. ZUMA_MAP_CUSTOM (Green): Has zupath[n]-[diff] progression
 * 3. ZUMA_MAP_MISC (Orange): Has <Graphics> but not in StageProgression & not zupath */
static ZumaMapType classify_zuma_map(const char *xml_buf, const char *level_id, char *out_prog, size_t out_sz) {
    char prog[64] = {0};
    bool has_prog = get_zuma_level_progression(xml_buf, level_id, prog, sizeof(prog));
    if (out_prog && out_sz > 0) {
        strncpy(out_prog, prog, out_sz - 1);
        out_prog[out_sz - 1] = '\0';
    }

    /* 1. Custom ZuPather Map: progression matches zupath[n]-[diff] */
    if (has_prog && _strnicmp(prog, "zupath", 6) == 0) {
        return ZUMA_MAP_CUSTOM;
    }

    /* 2. Authentic Original Zuma Deluxe Map: registered in StageProgression */
    if (is_id_in_stage_progression(xml_buf, level_id)) {
        return ZUMA_MAP_ORIGINAL;
    }

    /* 3. Misc Map: defined in <Graphics> but not in StageProgression & not zupath */
    return ZUMA_MAP_MISC;
}

static bool find_zuma_graphics_node(const char *xml, const char *level_id, char *out_dispname, size_t max_disp) {
    if (!xml || !level_id || !level_id[0]) return false;
    char search[128];
    snprintf(search, sizeof(search), "id=\"%s\"", level_id);
    const char *p = xml;
    while ((p = strstr(p, search)) != NULL) {
        const char *tag_start = p;
        while (tag_start > xml && *tag_start != '<' && *tag_start != '>') tag_start--;
        if (tag_start >= xml && *tag_start == '<' && strncasecmp(tag_start, "<Graphics", 9) == 0) {
            const char *tag_end = strchr(p, '>');
            if (tag_end) {
                char tag[512] = {0};
                size_t tlen = (size_t)(tag_end - tag_start);
                if (tlen >= sizeof(tag)) tlen = sizeof(tag) - 1;
                strncpy(tag, tag_start, tlen);
                tag[tlen] = '\0';
                if (out_dispname && max_disp > 0) {
                    if (!get_xml_attr_str(tag, "dispname", out_dispname, max_disp) || out_dispname[0] == '\0') {
                        strncpy(out_dispname, level_id, max_disp - 1);
                        out_dispname[max_disp - 1] = '\0';
                    }
                }
                return true;
            }
        }
        p += strlen(search);
    }
    return false;
}

static void populate_manage_zuma_levels(HWND hwnd_dlg) {
    if (!g_manage_dlg_data) return;
    g_manage_dlg_data->zuma_level_count = 0;
    g_manage_dlg_data->selected_zuma_idx = -1;

    if (g_manage_dlg_data->hwnd_list_zuma) {
        SendMessage(g_manage_dlg_data->hwnd_list_zuma, LB_RESETCONTENT, 0, 0);
    }

    if (!g_has_zuma_dir || g_zuma_levels_dir[0] == '\0') {
        if (g_manage_dlg_data->hwnd_list_zuma) {
            ShowWindow(g_manage_dlg_data->hwnd_list_zuma, SW_HIDE);
            EnableWindow(g_manage_dlg_data->hwnd_list_zuma, FALSE);
        }
        if (g_manage_dlg_data->hwnd_btn_import) {
            EnableWindow(g_manage_dlg_data->hwnd_btn_import, FALSE);
        }
        InvalidateRect(hwnd_dlg, NULL, TRUE);
        return;
    }

    char xml_path[MAX_PATH];
    snprintf(xml_path, sizeof(xml_path), "%s\\levels.xml", g_zuma_levels_dir);

    FILE *f = fopen(xml_path, "rb");
    if (!f) {
        if (g_manage_dlg_data->hwnd_list_zuma) {
            ShowWindow(g_manage_dlg_data->hwnd_list_zuma, SW_HIDE);
            EnableWindow(g_manage_dlg_data->hwnd_list_zuma, FALSE);
        }
        if (g_manage_dlg_data->hwnd_btn_import) {
            EnableWindow(g_manage_dlg_data->hwnd_btn_import, FALSE);
        }
        InvalidateRect(hwnd_dlg, NULL, TRUE);
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) {
        fclose(f);
        return;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    fread(buf, 1, sz, f);
    fclose(f);
    buf[sz] = '\0';

    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.*", g_zuma_levels_dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    int count = 0;
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {

                /* 1. Subfolder must conform to lowercase Zuma ID rules */
                if (!is_valid_zuma_level_id(fd.cFileName)) {
                    continue;
                }

                /* 2. Subfolder must match <Graphics id="..."> in levels.xml */
                char disp[128] = {0};
                if (find_zuma_graphics_node(buf, fd.cFileName, disp, sizeof(disp))) {
                    if (count < 128) {
                        strncpy(g_manage_dlg_data->zuma_levels[count].id, fd.cFileName, 63);
                        g_manage_dlg_data->zuma_levels[count].id[63] = '\0';
                        strncpy(g_manage_dlg_data->zuma_levels[count].dispname, disp, 127);
                        g_manage_dlg_data->zuma_levels[count].dispname[127] = '\0';

                        char prog_buf[64] = {0};
                        ZumaMapType mtype = classify_zuma_map(buf, fd.cFileName, prog_buf, sizeof(prog_buf));
                        g_manage_dlg_data->zuma_levels[count].type = mtype;
                        strncpy(g_manage_dlg_data->zuma_levels[count].prog_id, prog_buf, 63);
                        g_manage_dlg_data->zuma_levels[count].prog_id[63] = '\0';

                        if (g_manage_dlg_data->hwnd_list_zuma) {
                            SendMessageA(g_manage_dlg_data->hwnd_list_zuma, LB_ADDSTRING, 0, (LPARAM)fd.cFileName);
                        }
                        count++;
                    }
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    free(buf);

    g_manage_dlg_data->zuma_level_count = count;
    if (g_manage_dlg_data->hwnd_list_zuma) {
        ShowWindow(g_manage_dlg_data->hwnd_list_zuma, SW_SHOW);
        EnableWindow(g_manage_dlg_data->hwnd_list_zuma, TRUE);
        if (count > 0) {
            SendMessage(g_manage_dlg_data->hwnd_list_zuma, LB_SETCURSEL, 0, 0);
            g_manage_dlg_data->selected_zuma_idx = 0;
            if (g_manage_dlg_data->hwnd_btn_import) {
                EnableWindow(g_manage_dlg_data->hwnd_btn_import, TRUE);
            }
        } else {
            if (g_manage_dlg_data->hwnd_btn_import) {
                EnableWindow(g_manage_dlg_data->hwnd_btn_import, FALSE);
            }
        }
    }
    InvalidateRect(hwnd_dlg, NULL, TRUE);
}

static void populate_manage_canvas_list(HWND hwnd_dlg) {
    if (!g_manage_dlg_data) return;
    g_manage_dlg_data->canvas_count = 0;
    g_manage_dlg_data->selected_canvas_idx = -1;

    if (g_manage_dlg_data->hwnd_list_canvases) {
        SendMessage(g_manage_dlg_data->hwnd_list_canvases, LB_RESETCONTENT, 0, 0);
    }

    char base_dir[MAX_PATH];
    get_canvas_base_directory(base_dir, sizeof(base_dir));

    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.*", base_dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    int count = 0;
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {

                char folder_path[MAX_PATH];
                snprintf(folder_path, sizeof(folder_path), "%s\\%s", base_dir, fd.cFileName);

                char xml_file[MAX_PATH];
                snprintf(xml_file, sizeof(xml_file), "%s\\settings.xml", folder_path);

                if (GetFileAttributesA(xml_file) == INVALID_FILE_ATTRIBUTES) {
                    continue; /* Skip folders without settings.xml */
                }

                char cv_name[64] = {0};
                char cv_title[128] = {0};
                if (!load_canvas_settings_xml(
                    folder_path,
                    cv_name, sizeof(cv_name),
                    cv_title, sizeof(cv_title),
                    NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL,
                    NULL, NULL, 0,
                    NULL, NULL, 0,
                    NULL, NULL, 0,
                    NULL, NULL, 0
                )) {
                    continue; /* Skip invalid settings.xml */
                }

                if (count < 128) {
                    const char *final_name = (cv_name[0] != '\0') ? cv_name : fd.cFileName;
                    const char *final_title = (cv_title[0] != '\0') ? cv_title : final_name;

                    strncpy(g_manage_dlg_data->canvas_entries[count].name, final_name, 63);
                    g_manage_dlg_data->canvas_entries[count].name[63] = '\0';
                    strncpy(g_manage_dlg_data->canvas_entries[count].title, final_title, 127);
                    g_manage_dlg_data->canvas_entries[count].title[127] = '\0';

                    if (g_manage_dlg_data->hwnd_list_canvases) {
                        SendMessageA(g_manage_dlg_data->hwnd_list_canvases, LB_ADDSTRING, 0, (LPARAM)final_name);
                    }
                    count++;
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    g_manage_dlg_data->canvas_count = count;
    if (g_manage_dlg_data->hwnd_list_canvases) {
        if (count > 0) {
            SendMessage(g_manage_dlg_data->hwnd_list_canvases, LB_SETCURSEL, 0, 0);
            g_manage_dlg_data->selected_canvas_idx = 0;
            if (g_manage_dlg_data->hwnd_btn_export) {
                EnableWindow(g_manage_dlg_data->hwnd_btn_export, g_has_zuma_dir ? TRUE : FALSE);
            }
        } else {
            if (g_manage_dlg_data->hwnd_btn_export) {
                EnableWindow(g_manage_dlg_data->hwnd_btn_export, FALSE);
            }
        }
    }
    InvalidateRect(hwnd_dlg, NULL, TRUE);
}

/* ========================================================================= */
/* Zuma Deluxe levels.xml to ZuPather settings.xml Level Import Engine       */
/* ========================================================================= */

static bool copy_zuma_overlay_asset(
    const char *src_folder,
    const char *orig_name,
    const char *dest_folder,
    const char *dest_prefix,
    const char *dest_name,
    char *out_copied_filename,
    size_t max_out
) {
    if (!src_folder || !orig_name || !orig_name[0] || !dest_folder || !dest_name || !dest_name[0]) return false;

    const char *clean_orig = (orig_name[0] == '_') ? (orig_name + 1) : orig_name;
    const char *exts[] = {"gif", "png", "jpg", "jpeg", "webp", "bmp"};
    const char *prefixes[] = {"_", ""};

    char src_file[MAX_PATH];
    for (int p = 0; p < 2; ++p) {
        for (int e = 0; e < 6; ++e) {
            snprintf(src_file, sizeof(src_file), "%s\\%s%s.%s", src_folder, prefixes[p], clean_orig, exts[e]);
            if (GetFileAttributesA(src_file) != INVALID_FILE_ATTRIBUTES) {
                char dest_file[MAX_PATH];
                snprintf(dest_file, sizeof(dest_file), "%s\\%s%s.%s", dest_folder, dest_prefix ? dest_prefix : "", dest_name, exts[e]);
                if (CopyFileA(src_file, dest_file, FALSE)) {
                    if (out_copied_filename && max_out > 0) {
                        snprintf(out_copied_filename, max_out, "%s%s.%s", dest_prefix ? dest_prefix : "", dest_name, exts[e]);
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

static bool copy_zuma_background_image(
    const char *zuma_levels_dir,
    const char *level_id,
    const char *orig_image_name,
    const char *dest_folder,
    char *out_bg_filename,
    size_t max_bg
) {
    if (!zuma_levels_dir || !level_id || !dest_folder) return false;

    const char *exts[] = {"jpg", "png", "jpeg", "gif", "webp", "bmp"};
    const char *candidates[2];
    candidates[0] = (orig_image_name && orig_image_name[0]) ? orig_image_name : level_id;
    candidates[1] = level_id;

    char src_folder[MAX_PATH];
    snprintf(src_folder, sizeof(src_folder), "%s\\%s", zuma_levels_dir, level_id);

    char src_path[MAX_PATH];
    for (int c = 0; c < 2; ++c) {
        for (int e = 0; e < 6; ++e) {
            snprintf(src_path, sizeof(src_path), "%s\\%s.%s", src_folder, candidates[c], exts[e]);
            if (GetFileAttributesA(src_path) != INVALID_FILE_ATTRIBUTES) {
                char dest_path[MAX_PATH];
                snprintf(dest_path, sizeof(dest_path), "%s\\%s.%s", dest_folder, level_id, exts[e]);
                if (CopyFileA(src_path, dest_path, FALSE)) {
                    if (out_bg_filename && max_bg > 0) {
                        snprintf(out_bg_filename, max_bg, "%s.%s", level_id, exts[e]);
                    }
                    return true;
                }
            }
        }
    }

    /* Fallback: search in Zuma's parent images/ directory */
    char parent_images[MAX_PATH];
    snprintf(parent_images, sizeof(parent_images), "%s\\..\\images", zuma_levels_dir);
    for (int c = 0; c < 2; ++c) {
        for (int e = 0; e < 6; ++e) {
            snprintf(src_path, sizeof(src_path), "%s\\%s.%s", parent_images, candidates[c], exts[e]);
            if (GetFileAttributesA(src_path) != INVALID_FILE_ATTRIBUTES) {
                char dest_path[MAX_PATH];
                snprintf(dest_path, sizeof(dest_path), "%s\\%s.%s", dest_folder, level_id, exts[e]);
                if (CopyFileA(src_path, dest_path, FALSE)) {
                    if (out_bg_filename && max_bg > 0) {
                        snprintf(out_bg_filename, max_bg, "%s.%s", level_id, exts[e]);
                    }
                    return true;
                }
            }
        }
    }

    return false;
}

static bool copy_zuma_curves_and_generate_zupath(
    const char *zuma_levels_dir,
    const char *level_id,
    char curves[4][64],
    int curve_count,
    const char *dest_folder,
    char *out_error_buf,
    size_t error_buf_size
) {
    if (out_error_buf && error_buf_size > 0) out_error_buf[0] = '\0';
    if (!zuma_levels_dir || !level_id || !dest_folder) {
        if (out_error_buf && error_buf_size > 0) {
            snprintf(out_error_buf, error_buf_size, "Invalid parameter (missing directory or level ID).");
        }
        return false;
    }

    char src_folder[MAX_PATH];
    snprintf(src_folder, sizeof(src_folder), "%s\\%s", zuma_levels_dir, level_id);

    int actual_count = (curve_count > 0 && curve_count <= 4) ? curve_count : 1;
    BezierPath bpaths[4];
    memset(bpaths, 0, sizeof(bpaths));

    for (int k = 0; k < actual_count; ++k) {
        const char *cname = (curves[k][0] != '\0') ? curves[k] : level_id;
        bezier_path_init(&bpaths[k], cname);

        char src_dat[MAX_PATH];
        /* 1. Try inside level subfolder (e.g. levels\level_id\cname.dat) */
        snprintf(src_dat, sizeof(src_dat), "%s\\%s.dat", src_folder, cname);
        if (GetFileAttributesA(src_dat) == INVALID_FILE_ATTRIBUTES) {
            if (actual_count > 1) {
                snprintf(src_dat, sizeof(src_dat), "%s\\%s-%d.dat", src_folder, level_id, k + 1);
            } else {
                snprintf(src_dat, sizeof(src_dat), "%s\\%s.dat", src_folder, level_id);
            }
        }
        /* 2. Fallback: try directly in zuma_levels_dir root (e.g. levels\cname.dat) */
        if (GetFileAttributesA(src_dat) == INVALID_FILE_ATTRIBUTES) {
            snprintf(src_dat, sizeof(src_dat), "%s\\%s.dat", zuma_levels_dir, cname);
            if (GetFileAttributesA(src_dat) == INVALID_FILE_ATTRIBUTES) {
                if (actual_count > 1) {
                    snprintf(src_dat, sizeof(src_dat), "%s\\%s-%d.dat", zuma_levels_dir, level_id, k + 1);
                } else {
                    snprintf(src_dat, sizeof(src_dat), "%s\\%s.dat", zuma_levels_dir, level_id);
                }
            }
        }

        char dest_dat[MAX_PATH];
        if (actual_count > 1) {
            snprintf(dest_dat, sizeof(dest_dat), "%s\\%s-%d.dat", dest_folder, level_id, k + 1);
        } else {
            snprintf(dest_dat, sizeof(dest_dat), "%s\\%s.dat", dest_folder, level_id);
        }

        if (GetFileAttributesA(src_dat) == INVALID_FILE_ATTRIBUTES) {
            if (out_error_buf && error_buf_size > 0) {
                snprintf(out_error_buf, error_buf_size,
                    "Zuma curve file '%s.dat' not found in:\n  - %s\n  - %s",
                    cname, src_folder, zuma_levels_dir);
            }
            for (int i = 0; i <= k; ++i) bezier_path_free(&bpaths[i]);
            return false;
        }

        CopyFileA(src_dat, dest_dat, FALSE);
        DatImportError dat_err = DAT_ERR_OK;
        if (!bezier_path_import_dat_ex(&bpaths[k], dest_dat, 0.0f, &dat_err)) {
            if (out_error_buf && error_buf_size > 0) {
                snprintf(out_error_buf, error_buf_size,
                    "Failed to parse Zuma curve data for track %d (%s):\nReason: %s",
                    k + 1, dest_dat, dat_import_error_to_string(dat_err));
            }
            for (int i = 0; i <= k; ++i) bezier_path_free(&bpaths[i]);
            return false;
        }

        path_cache_update(&bpaths[k]);
    }

    /* Save .zupath vector sidecar */
    char zupath_file[MAX_PATH];
    snprintf(zupath_file, sizeof(zupath_file), "%s\\%s.zupath", dest_folder, level_id);
    bezier_path_save_zupath(bpaths, actual_count, zupath_file);

    for (int k = 0; k < actual_count; ++k) {
        bezier_path_free(&bpaths[k]);
    }

    return true;
}

static bool generate_zuma_settings_xml(
    const char *dest_folder,
    const char *level_id,
    const char *dispname,
    int frog_gx, int frog_gy,
    const char *bg_filename,
    int img_w, int img_h,
    int curve_count,
    const LevelCutout *cutouts, int cutout_count,
    const LevelBackgroundAlpha *bgalphas, int bgalpha_count,
    const LevelSprite *sprites, int sprite_count,
    const TreasurePoint *coins, int coin_count,
    const char *extra_attrs
) {
    char xml_path[MAX_PATH];
    snprintf(xml_path, sizeof(xml_path), "%s\\settings.xml", dest_folder);

    FILE *f = fopen(xml_path, "w");
    if (!f) return false;

    const char *safe_name = (level_id && level_id[0]) ? level_id : "level";
    const char *safe_title = (dispname && dispname[0]) ? dispname : safe_name;
    int cv_w = (img_w > 0) ? img_w : 640;
    int cv_h = (img_h > 0) ? img_h : 480;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<!--\n");
    fprintf(f, "    ZuPather Canvas Configuration\n");
    fprintf(f, "    CanvasName: %s\n", safe_name);
    fprintf(f, "    CanvasTitle: %s\n", safe_title);
    fprintf(f, "    CanvasWidth: %d\n", cv_w);
    fprintf(f, "    CanvasHeight: %d\n", cv_h);
    if (bg_filename && bg_filename[0] && _stricmp(bg_filename, "none") != 0) {
        fprintf(f, "    BackgroundImage: %s\n", bg_filename);
        fprintf(f, "    ImageWidth: %d\n", cv_w);
        fprintf(f, "    ImageHeight: %d\n", cv_h);
        fprintf(f, "    ImageResized: true\n");
    } else {
        fprintf(f, "    BackgroundImage: none\n");
        fprintf(f, "    ImageWidth: 0\n");
        fprintf(f, "    ImageHeight: 0\n");
        fprintf(f, "    ImageResized: false\n");
    }
    fprintf(f, "    FrogGX: %d\n", frog_gx);
    fprintf(f, "    FrogGY: %d\n", frog_gy);
    fprintf(f, "    TreasurePointCount: %d\n", coin_count);
    fprintf(f, "    CutoutCount: %d\n", cutout_count);
    fprintf(f, "    SpriteCount: %d\n", sprite_count);
    fprintf(f, "    BackgroundAlphaCount: %d\n", bgalpha_count);
    int actual_paths = (curve_count > 0 && curve_count <= 10) ? curve_count : 1;
    char prog_id[64];
    get_level_progression_id(actual_paths, g_level_difficulty, prog_id, sizeof(prog_id));
    const char *diff_str = level_difficulty_to_string(g_level_difficulty);
    fprintf(f, "    Progression: %s\n", prog_id);
    fprintf(f, "    Difficulty: %s\n", diff_str);
    fprintf(f, "-->\n");
    fprintf(f, "<Levels>\n");

    /* Graphics tag */
    fprintf(f, "    <Graphics id=\"%s\" image=\"%s\"", safe_name, safe_name);
    if (curve_count <= 1) {
        fprintf(f, " curve=\"%s\"", safe_name);
    } else {
        for (int p = 0; p < curve_count; ++p) {
            char cname[128];
            snprintf(cname, sizeof(cname), "%s-%d", safe_name, p + 1);
            if (p == 0) {
                fprintf(f, " curve=\"%s\"", cname);
            } else {
                fprintf(f, " curve%d=\"%s\"", p + 1, cname);
            }
        }
    }
    fprintf(f, " dispname=\"%s\" gx=\"%d\" gy=\"%d\"", safe_title, frog_gx, frog_gy);
    if (extra_attrs && extra_attrs[0]) {
        fprintf(f, "%s", extra_attrs);
    }
    fprintf(f, ">\n");
    fprintf(f, "        <!-- Zuma Deluxe Visual Elements (Child tags ordered: BackgroundAlpha -> Sprite -> Cutout -> TreasurePoint) -->\n");

    /* BackgroundAlpha elements */
    if (bgalpha_count > 0) {
        for (int i = 0; i < bgalpha_count; ++i) {
            fprintf(f, "        <BackgroundAlpha image=\"%s\" x=\"%d\" y=\"%d\" vx=\"%d\" vy=\"%d\" />\n",
                    bgalphas[i].image_name, bgalphas[i].x, bgalphas[i].y, bgalphas[i].vx, bgalphas[i].vy);
        }
    } else {
        fprintf(f, "        <!-- <BackgroundAlpha image=\"water\" x=\"0\" y=\"0\" vx=\"0\" vy=\"1\" /> -->\n");
    }

    /* Sprite elements */
    if (sprite_count > 0) {
        for (int i = 0; i < sprite_count; ++i) {
            int pri = (sprites[i].pri >= 0 && sprites[i].pri <= 99) ? sprites[i].pri : 1;
            fprintf(f, "        <Sprite pri=\"%d\" image=\"%s\" x=\"%d\" y=\"%d\" />\n",
                    pri, sprites[i].image_name, sprites[i].x, sprites[i].y);
        }
    } else {
        fprintf(f, "        <!-- <Sprite image=\"tree\" x=\"100\" y=\"200\" /> -->\n");
    }

    /* Cutout elements */
    if (cutout_count > 0) {
        for (int i = 0; i < cutout_count; ++i) {
            int pri = (cutouts[i].pri >= 0 && cutouts[i].pri <= 99) ? cutouts[i].pri : 1;
            fprintf(f, "        <Cutout pri=\"%d\" image=\"%s\" x=\"%d\" y=\"%d\" />\n",
                    pri, cutouts[i].image_name, cutouts[i].x, cutouts[i].y);
        }
    } else {
        fprintf(f, "        <!-- <Cutout pri=\"1\" image=\"tunnel\" x=\"0\" y=\"0\" /> -->\n");
    }

    /* TreasurePoint elements */
    if (coin_count > 0) {
        for (int i = 0; i < coin_count; ++i) {
            int d = coins[i].dist1 > 0 ? coins[i].dist1 : 35;
            fprintf(f, "        <TreasurePoint x=\"%d\" y=\"%d\" dist1=\"%d\" />\n",
                    coins[i].x, coins[i].y, d);
        }
    } else {
        fprintf(f, "        <!-- <TreasurePoint x=\"100\" y=\"100\" dist1=\"35\" /> -->\n");
    }

    fprintf(f, "    </Graphics>\n");
    fprintf(f, "</Levels>\n");

    fclose(f);
    return true;
}

static int find_index_in_comma_list(const char *list, const char *target) {
    if (!list || !target || !target[0]) return -1;
    int idx = 0;
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        const char *next = strchr(p, ',');
        size_t len = next ? (size_t)(next - p) : strlen(p);
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r' || p[len - 1] == '\n')) {
            len--;
        }
        if (len == strlen(target) && _strnicmp(p, target, len) == 0) {
            return idx;
        }
        idx++;
        if (!next) break;
        p = next + 1;
    }
    return -1;
}

static bool get_index_from_comma_list(const char *list, int target_idx, char *out_val, size_t out_sz) {
    if (!list || target_idx < 0 || !out_val || out_sz == 0) return false;
    out_val[0] = '\0';
    int idx = 0;
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        const char *next = strchr(p, ',');
        size_t len = next ? (size_t)(next - p) : strlen(p);
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r' || p[len - 1] == '\n')) {
            len--;
        }
        if (idx == target_idx) {
            if (len >= out_sz) len = out_sz - 1;
            strncpy(out_val, p, len);
            out_val[len] = '\0';
            return true;
        }
        idx++;
        if (!next) break;
        p = next + 1;
    }
    return false;
}

static bool get_loose_xml_attr_str(const char *text, const char *attr_name, char *out_val, size_t max_len) {
    if (!text || !attr_name || !out_val || max_len == 0) return false;
    out_val[0] = '\0';
    const char *p = text;
    size_t nlen = strlen(attr_name);
    while ((p = strstr(p, attr_name)) != NULL) {
        if (p > text && (isalnum((unsigned char)*(p - 1)) || *(p - 1) == '_')) {
            p += nlen;
            continue;
        }
        const char *after = p + nlen;
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') after++;
        if (*after == '=') {
            after++;
            while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') after++;
            if (*after == '"' || *after == '\'') {
                char quote = *after;
                after++;
                const char *end = strchr(after, quote);
                if (!end) return false;
                size_t len = (size_t)(end - after);
                if (len >= max_len) len = max_len - 1;
                strncpy(out_val, after, len);
                out_val[len] = '\0';
                return true;
            } else {
                const char *end = after;
                while (*end && *end != ' ' && *end != '\t' && *end != '>' && *end != '/' && *end != '\r' && *end != '\n') end++;
                size_t len = (size_t)(end - after);
                if (len >= max_len) len = max_len - 1;
                strncpy(out_val, after, len);
                out_val[len] = '\0';
                return (len > 0);
            }
        }
        p += nlen;
    }
    return false;
}

static bool find_zuma_settings_value(const char *xml_buf, const char *level_id, const char *attr_name, char *out_val, size_t out_sz) {
    if (!xml_buf || !level_id || !level_id[0] || !attr_name || !out_val || out_sz == 0) return false;
    out_val[0] = '\0';

    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "<Settings id=\"%s\"", level_id);
    const char *p = strstr(xml_buf, search_pattern);
    if (!p) {
        snprintf(search_pattern, sizeof(search_pattern), "<Settings id='%s'", level_id);
        p = strstr(xml_buf, search_pattern);
    }
    if (!p) {
        snprintf(search_pattern, sizeof(search_pattern), "<Settings id=\"%s_settings\"", level_id);
        p = strstr(xml_buf, search_pattern);
    }

    if (!p) {
        const char *sp = strstr(xml_buf, "<StageProgression");
        if (sp) {
            const char *sp_end = strstr(sp, "/>");
            if (!sp_end) sp_end = strstr(sp, "</StageProgression>");
            if (!sp_end) sp_end = sp + 8192;

            for (int s = 1; s <= 15; ++s) {
                char stage_key[32], diffi_key[32];
                snprintf(stage_key, sizeof(stage_key), "stage%d", s);
                snprintf(diffi_key, sizeof(diffi_key), "diffi%d", s);

                char stage_list[512] = {0};
                char diffi_list[512] = {0};

                const char *st_ptr = strstr(sp, stage_key);
                if (st_ptr && st_ptr < sp_end) {
                    get_loose_xml_attr_str(st_ptr, stage_key, stage_list, sizeof(stage_list));
                }

                int target_idx = find_index_in_comma_list(stage_list, level_id);
                if (target_idx >= 0) {
                    const char *df_ptr = strstr(sp, diffi_key);
                    if (df_ptr && df_ptr < sp_end) {
                        get_loose_xml_attr_str(df_ptr, diffi_key, diffi_list, sizeof(diffi_list));
                        char matched_diffi[64] = {0};
                        if (get_index_from_comma_list(diffi_list, target_idx, matched_diffi, sizeof(matched_diffi)) && matched_diffi[0]) {
                            snprintf(search_pattern, sizeof(search_pattern), "<Settings id=\"%s\"", matched_diffi);
                            p = strstr(xml_buf, search_pattern);
                            if (!p) {
                                snprintf(search_pattern, sizeof(search_pattern), "<Settings id='%s'", matched_diffi);
                                p = strstr(xml_buf, search_pattern);
                            }
                            if (p) break;
                        }
                    }
                }
            }
        }
    }

    if (p) {
        const char *tag_end = strchr(p, '>');
        if (tag_end) {
            char tag_buf[1024] = {0};
            size_t tlen = (size_t)(tag_end - p + 1);
            if (tlen >= sizeof(tag_buf)) tlen = sizeof(tag_buf) - 1;
            strncpy(tag_buf, p, tlen);
            tag_buf[tlen] = '\0';

            return get_loose_xml_attr_str(tag_buf, attr_name, out_val, out_sz);
        }
    }

    return false;
}

static bool import_zuma_level_to_canvas(HWND hwnd_dlg, const char *level_id) {
    if (!g_has_zuma_dir || g_zuma_levels_dir[0] == '\0' || !level_id || !level_id[0]) {
        show_themed_notice_dialog(hwnd_dlg, "Notice", "Zuma Folder Required", "Please configure the Zuma Deluxe folder first.", 460, 240);
        return false;
    }

    g_level_difficulty = LEVEL_DIFF_MEDIUM;

    char xml_path[MAX_PATH];
    snprintf(xml_path, sizeof(xml_path), "%s\\levels.xml", g_zuma_levels_dir);

    FILE *f = fopen(xml_path, "rb");
    if (!f) {
        show_themed_notice_dialog(hwnd_dlg, "Import Error", "Missing levels.xml", "Could not open levels.xml in the Zuma Deluxe directory.", 480, 240);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) {
        fclose(f);
        return false;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    fread(buf, 1, sz, f);
    fclose(f);
    buf[sz] = '\0';

    /* Locate the <Graphics id="level_id" ...> node */
    char search_id[128];
    snprintf(search_id, sizeof(search_id), "id=\"%s\"", level_id);

    const char *p = buf;
    const char *found_gfx = NULL;
    while ((p = strstr(p, search_id)) != NULL) {
        const char *tag_start = p;
        while (tag_start > buf && *tag_start != '<' && *tag_start != '>') tag_start--;
        if (tag_start >= buf && *tag_start == '<' && strncasecmp(tag_start, "<Graphics", 9) == 0) {
            found_gfx = tag_start;
            break;
        }
        p += strlen(search_id);
    }

    if (!found_gfx) {
        free(buf);
        show_themed_notice_dialog(hwnd_dlg, "Import Error", "Level Definition Missing", "Could not locate graphics definition for the selected level in levels.xml.", 500, 240);
        return false;
    }

    /* Extract the opening <Graphics ...> tag */
    const char *gfx_end = strchr(found_gfx, '>');
    if (!gfx_end) {
        free(buf);
        return false;
    }

    char gfx_tag[1024] = {0};
    size_t tag_len = (size_t)(gfx_end - found_gfx);
    if (tag_len >= sizeof(gfx_tag)) tag_len = sizeof(gfx_tag) - 1;
    strncpy(gfx_tag, found_gfx, tag_len);
    gfx_tag[tag_len] = '\0';

    char dispname[128] = {0};
    char image_attr[64] = {0};
    char curve_names[4][64] = {{0}};
    int curve_count = 0;
    int frog_gx = 320;
    int frog_gy = 240;

    get_xml_attr_str(gfx_tag, "dispname", dispname, sizeof(dispname));
    if (dispname[0] == '\0') strncpy(dispname, level_id, sizeof(dispname) - 1);

    get_xml_attr_str(gfx_tag, "image", image_attr, sizeof(image_attr));
    if (image_attr[0] == '\0') strncpy(image_attr, level_id, sizeof(image_attr) - 1);

    get_xml_attr_int(gfx_tag, "gx", &frog_gx);
    get_xml_attr_int(gfx_tag, "gy", &frog_gy);

    char c1[64] = {0};
    if (get_xml_attr_str(gfx_tag, "curve", c1, sizeof(c1)) && c1[0]) {
        strncpy(curve_names[0], c1, 63);
        curve_count = 1;
    } else if (get_xml_attr_str(gfx_tag, "curve1", c1, sizeof(c1)) && c1[0]) {
        strncpy(curve_names[0], c1, 63);
        curve_count = 1;
    }
    for (int k = 2; k <= 4; ++k) {
        char attr[16];
        snprintf(attr, sizeof(attr), "curve%d", k);
        char ck[64] = {0};
        if (get_xml_attr_str(gfx_tag, attr, ck, sizeof(ck)) && ck[0]) {
            strncpy(curve_names[curve_count], ck, 63);
            curve_count++;
        }
    }
    if (curve_count == 0) {
        strncpy(curve_names[0], level_id, 63);
        curve_count = 1;
    }

    /* Extract extra Zuma Deluxe attributes (skullrot, space, drawcurve, drawtunnels, curvecolor, bgcolor, destroyall, huerot, treasurefreq, partime) */
    char extra_gfx_attrs[1024] = {0};
    char extra_val[64] = {0};
    if (get_xml_attr_str(gfx_tag, "skullrot", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(extra_gfx_attrs + strlen(extra_gfx_attrs), sizeof(extra_gfx_attrs) - strlen(extra_gfx_attrs), " skullrot=\"%s\"", extra_val);
    }
    if (get_xml_attr_str(gfx_tag, "space", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(extra_gfx_attrs + strlen(extra_gfx_attrs), sizeof(extra_gfx_attrs) - strlen(extra_gfx_attrs), " space=\"%s\"", extra_val);
    }
    if (get_xml_attr_str(gfx_tag, "drawcurve", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(extra_gfx_attrs + strlen(extra_gfx_attrs), sizeof(extra_gfx_attrs) - strlen(extra_gfx_attrs), " drawcurve=\"%s\"", extra_val);
    }
    if (get_xml_attr_str(gfx_tag, "drawtunnels", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(extra_gfx_attrs + strlen(extra_gfx_attrs), sizeof(extra_gfx_attrs) - strlen(extra_gfx_attrs), " drawtunnels=\"%s\"", extra_val);
    }
    if (get_xml_attr_str(gfx_tag, "curvecolor", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(extra_gfx_attrs + strlen(extra_gfx_attrs), sizeof(extra_gfx_attrs) - strlen(extra_gfx_attrs), " curvecolor=\"%s\"", extra_val);
    }
    if (get_xml_attr_str(gfx_tag, "bgcolor", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(extra_gfx_attrs + strlen(extra_gfx_attrs), sizeof(extra_gfx_attrs) - strlen(extra_gfx_attrs), " bgcolor=\"%s\"", extra_val);
    }
    if (get_xml_attr_str(gfx_tag, "huerot", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(extra_gfx_attrs + strlen(extra_gfx_attrs), sizeof(extra_gfx_attrs) - strlen(extra_gfx_attrs), " huerot=\"%s\"", extra_val);
    }

    /* Find the closing </Graphics> */
    const char *block_end = strstr(gfx_end, "</Graphics>");
    if (!block_end) {
        block_end = strstr(gfx_end, "<Graphics");
        if (!block_end) block_end = strstr(gfx_end, "<Settings");
        if (!block_end) block_end = buf + sz;
    }

    /* Parse child tags inside block */
    LevelBackgroundAlpha bgalphas[MAX_BG_ALPHAS];
    char orig_bg_images[MAX_BG_ALPHAS][64];
    int bgalpha_count = 0;

    LevelSprite sprites[MAX_SPRITES];
    char orig_sprite_images[MAX_SPRITES][64];
    int sprite_count = 0;

    LevelCutout cutouts[MAX_CUTOUTS];
    char orig_cutout_images[MAX_CUTOUTS][64];
    int cutout_count = 0;

    TreasurePoint coins[MAX_TREASURE_POINTS];
    int coin_count = 0;

    const char *scan = gfx_end;
    while (scan < block_end) {
        const char *tag_s = strchr(scan, '<');
        if (!tag_s || tag_s >= block_end) break;
        const char *tag_e = strchr(tag_s, '>');
        if (!tag_e || tag_e > block_end) break;

        char elem[512] = {0};
        size_t elen = (size_t)(tag_e - tag_s + 1);
        if (elen >= sizeof(elem)) elen = sizeof(elem) - 1;
        strncpy(elem, tag_s, elen);
        elem[elen] = '\0';

        if (strncasecmp(elem, "<BackgroundAlpha", 16) == 0 && bgalpha_count < MAX_BG_ALPHAS) {
            LevelBackgroundAlpha *ba = &bgalphas[bgalpha_count];
            memset(ba, 0, sizeof(LevelBackgroundAlpha));
            get_xml_attr_int(elem, "x", &ba->x);
            get_xml_attr_int(elem, "y", &ba->y);
            get_xml_attr_int(elem, "vx", &ba->vx);
            get_xml_attr_int(elem, "vy", &ba->vy);
            ba->pri = 1;
            get_xml_attr_int(elem, "pri", &ba->pri);
            char bimg[64] = {0};
            get_xml_attr_str(elem, "image", bimg, sizeof(bimg));
            strncpy(orig_bg_images[bgalpha_count], bimg, 63);
            bgalpha_count++;
        } else if (strncasecmp(elem, "<Sprite", 7) == 0 && sprite_count < MAX_SPRITES) {
            LevelSprite *sp = &sprites[sprite_count];
            memset(sp, 0, sizeof(LevelSprite));
            get_xml_attr_int(elem, "x", &sp->x);
            get_xml_attr_int(elem, "y", &sp->y);
            sp->pri = 1;
            get_xml_attr_int(elem, "pri", &sp->pri);
            char simg[64] = {0};
            get_xml_attr_str(elem, "image", simg, sizeof(simg));
            strncpy(orig_sprite_images[sprite_count], simg, 63);
            sprite_count++;
        } else if (strncasecmp(elem, "<Cutout", 7) == 0 && cutout_count < MAX_CUTOUTS) {
            LevelCutout *co = &cutouts[cutout_count];
            memset(co, 0, sizeof(LevelCutout));
            get_xml_attr_int(elem, "x", &co->x);
            get_xml_attr_int(elem, "y", &co->y);
            co->pri = 1;
            get_xml_attr_int(elem, "pri", &co->pri);
            char cimg[64] = {0};
            get_xml_attr_str(elem, "image", cimg, sizeof(cimg));
            strncpy(orig_cutout_images[cutout_count], cimg, 63);
            cutout_count++;
        } else if (strncasecmp(elem, "<TreasurePoint", 14) == 0 && coin_count < MAX_TREASURE_POINTS) {
            TreasurePoint *tp = &coins[coin_count];
            memset(tp, 0, sizeof(TreasurePoint));
            get_xml_attr_int(elem, "x", &tp->x);
            get_xml_attr_int(elem, "y", &tp->y);
            tp->dist1 = 35;
            if (!get_xml_attr_int(elem, "dist1", &tp->dist1)) {
                get_xml_attr_int(elem, "dist2", &tp->dist1);
            }
            if (tp->dist1 <= 0) tp->dist1 = 35;
            coin_count++;
        }

        scan = tag_e + 1;
    }
    free(buf);

    /* Target directory: ./canvas/<level_id> */
    char base_dir[MAX_PATH];
    get_canvas_base_directory(base_dir, sizeof(base_dir));

    char target_canvas_dir[MAX_PATH];
    snprintf(target_canvas_dir, sizeof(target_canvas_dir), "%s\\%s", base_dir, level_id);

    if (GetFileAttributesA(target_canvas_dir) != INVALID_FILE_ATTRIBUTES) {
        char confirm_msg[512];
        snprintf(confirm_msg, sizeof(confirm_msg),
            "A canvas project named '%s' already exists in ./canvas/.\n\n"
            "Do you want to overwrite it with the imported level from Zuma Deluxe?",
            level_id);
        if (!show_themed_confirm_dialog(hwnd_dlg, "Canvas Already Exists", "Canvas Conflict", confirm_msg, true)) {
            return false;
        }
    } else {
        CreateDirectoryA(target_canvas_dir, NULL);
    }

    char zuma_level_folder[MAX_PATH];
    snprintf(zuma_level_folder, sizeof(zuma_level_folder), "%s\\%s", g_zuma_levels_dir, level_id);

    /* A. Copy Background Image */
    char bg_copied_filename[MAX_PATH] = "none";
    int bg_w = 640;
    int bg_h = 480;
    if (copy_zuma_background_image(g_zuma_levels_dir, level_id, image_attr, target_canvas_dir, bg_copied_filename, sizeof(bg_copied_filename))) {
        char full_bg_path[MAX_PATH];
        snprintf(full_bg_path, sizeof(full_bg_path), "%s\\%s", target_canvas_dir, bg_copied_filename);
        CanvasImage tmp_img;
        memset(&tmp_img, 0, sizeof(CanvasImage));
        if (image_load_from_file(full_bg_path, &tmp_img)) {
            if (tmp_img.width > 0) bg_w = tmp_img.width;
            if (tmp_img.height > 0) bg_h = tmp_img.height;
            image_free(&tmp_img);
        }
    }

    /* B. Copy Curves (.dat) and Generate Vector Sidecar (.zupath) */
    char curve_err[512] = {0};
    if (!copy_zuma_curves_and_generate_zupath(g_zuma_levels_dir, level_id, curve_names, curve_count, target_canvas_dir, curve_err, sizeof(curve_err))) {
        char err_msg[768];
        snprintf(err_msg, sizeof(err_msg),
            "Failed to import curve path(s) for map '%s':\n\n%s\n\n"
            "The canvas project directory was created, but curve paths could not be fully imported.",
            level_id, curve_err[0] ? curve_err : "Unknown error while reading curve .dat file.");
        show_themed_notice_dialog(
            hwnd_dlg,
            "Curve Import Notice - ZuPather",
            "Curve Import Incomplete",
            err_msg,
            560, 280
        );
    }

    /* C. Standardize and Copy Cutouts */
    for (int i = 0; i < cutout_count; ++i) {
        char std_name[64];
        if (cutout_count == 1) {
            snprintf(std_name, sizeof(std_name), "tunnel");
        } else {
            snprintf(std_name, sizeof(std_name), "tunnel%d", i + 1);
        }
        strncpy(cutouts[i].image_name, std_name, sizeof(cutouts[i].image_name) - 1);
        copy_zuma_overlay_asset(zuma_level_folder, orig_cutout_images[i], target_canvas_dir, "_", std_name, NULL, 0);
    }

    /* D. Standardize and Copy BackgroundAlphas */
    for (int i = 0; i < bgalpha_count; ++i) {
        char std_name[64];
        if (bgalpha_count == 1) {
            snprintf(std_name, sizeof(std_name), "quake");
        } else {
            snprintf(std_name, sizeof(std_name), "quake%d", i + 1);
        }
        strncpy(bgalphas[i].image_name, std_name, sizeof(bgalphas[i].image_name) - 1);
        copy_zuma_overlay_asset(zuma_level_folder, orig_bg_images[i], target_canvas_dir, "_", std_name, NULL, 0);
    }

    /* E. Standardize and Copy Sprites */
    for (int i = 0; i < sprite_count; ++i) {
        char std_name[64];
        if (sprite_count == 1) {
            snprintf(std_name, sizeof(std_name), "sprite");
        } else {
            snprintf(std_name, sizeof(std_name), "sprite%d", i + 1);
        }
        strncpy(sprites[i].image_name, std_name, sizeof(sprites[i].image_name) - 1);
        copy_zuma_overlay_asset(zuma_level_folder, orig_sprite_images[i], target_canvas_dir, "", std_name, NULL, 0);
    }

    /* F. Generate Structured settings.xml */
    generate_zuma_settings_xml(
        target_canvas_dir,
        level_id,
        dispname,
        frog_gx, frog_gy,
        bg_copied_filename,
        bg_w, bg_h,
        curve_count,
        cutouts, cutout_count,
        bgalphas, bgalpha_count,
        sprites, sprite_count,
        coins, coin_count,
        extra_gfx_attrs
    );

    /* Clean up any obsolete/stale files in target folder */
    prune_unregistered_canvas_files(target_canvas_dir, level_id);

    /* G. Refresh Canvas list and present completion */
    populate_manage_canvas_list(hwnd_dlg);

    /* Select the newly imported canvas in right listbox */
    for (int i = 0; i < g_manage_dlg_data->canvas_count; ++i) {
        if (_stricmp(g_manage_dlg_data->canvas_entries[i].name, level_id) == 0) {
            SendMessage(g_manage_dlg_data->hwnd_list_canvases, LB_SETCURSEL, i, 0);
            g_manage_dlg_data->selected_canvas_idx = i;
            break;
        }
    }

    char prompt[512];
    snprintf(prompt, sizeof(prompt),
        "Map '%s' (%s) was successfully imported to:\n%s\n\n"
        "All visual overlay assets (cutouts, background alpha layers) and curve paths have been converted into ZuPather standard format.\n\n"
        "Would you like to open and edit this map in ZuPather now, or stay in Map Manager?",
        level_id, dispname, target_canvas_dir);

    int choice = show_themed_choice_dialog_ex(
        hwnd_dlg,
        "Import Successful",
        "Map Imported Successfully",
        prompt,
        "Open Map",
        "Stay Here",
        NULL,
        560,
        280
    );

    if (choice == 1) {
        DestroyWindow(hwnd_dlg);
        execute_open_canvas_folder(g_hwnd_main, target_canvas_dir);
    } else {
        InvalidateRect(hwnd_dlg, NULL, TRUE);
    }

    return true;
}

static void show_zuma_level_attributes_dialog(HWND parent_hwnd, const char *level_id) {
    if (!parent_hwnd || !level_id || !level_id[0] || !g_has_zuma_dir || g_zuma_levels_dir[0] == '\0') {
        show_themed_notice_dialog(parent_hwnd, "Notice", "Zuma Folder Required", "Please configure the Zuma Deluxe folder first.", 460, 240);
        return;
    }

    char xml_path[MAX_PATH];
    snprintf(xml_path, sizeof(xml_path), "%s\\levels.xml", g_zuma_levels_dir);

    FILE *f = fopen(xml_path, "rb");
    if (!f) {
        show_themed_notice_dialog(parent_hwnd, "Notice", "Missing levels.xml", "Could not open levels.xml in the Zuma Deluxe directory.", 480, 240);
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) {
        fclose(f);
        return;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    fread(buf, 1, sz, f);
    fclose(f);
    buf[sz] = '\0';

    char search_id[128];
    snprintf(search_id, sizeof(search_id), "id=\"%s\"", level_id);

    const char *p = buf;
    const char *found_gfx = NULL;
    while ((p = strstr(p, search_id)) != NULL) {
        const char *tag_start = p;
        while (tag_start > buf && *tag_start != '<' && *tag_start != '>') tag_start--;
        if (tag_start >= buf && *tag_start == '<' && strncasecmp(tag_start, "<Graphics", 9) == 0) {
            found_gfx = tag_start;
            break;
        }
        p += strlen(search_id);
    }

    if (!found_gfx) {
        free(buf);
        show_themed_notice_dialog(parent_hwnd, "Notice", "Level Not Found", "Could not locate graphics definition for this level in levels.xml.", 500, 240);
        return;
    }

    const char *gfx_end = strchr(found_gfx, '>');
    if (!gfx_end) {
        free(buf);
        return;
    }

    char gfx_tag[1024] = {0};
    size_t tag_len = (size_t)(gfx_end - found_gfx);
    if (tag_len >= sizeof(gfx_tag)) tag_len = sizeof(gfx_tag) - 1;
    strncpy(gfx_tag, found_gfx, tag_len);
    gfx_tag[tag_len] = '\0';

    char dispname[128] = {0};
    char image_attr[64] = {0};
    char curve_names[4][64] = {{0}};
    int curve_count = 0;
    int frog_gx = 320;
    int frog_gy = 240;

    get_xml_attr_str(gfx_tag, "dispname", dispname, sizeof(dispname));
    if (dispname[0] == '\0') strncpy(dispname, level_id, sizeof(dispname) - 1);

    get_xml_attr_str(gfx_tag, "image", image_attr, sizeof(image_attr));
    if (image_attr[0] == '\0') strncpy(image_attr, level_id, sizeof(image_attr) - 1);

    get_xml_attr_int(gfx_tag, "gx", &frog_gx);
    get_xml_attr_int(gfx_tag, "gy", &frog_gy);

    char c1[64] = {0};
    if (get_xml_attr_str(gfx_tag, "curve", c1, sizeof(c1)) && c1[0]) {
        strncpy(curve_names[0], c1, 63);
        curve_count = 1;
    } else if (get_xml_attr_str(gfx_tag, "curve1", c1, sizeof(c1)) && c1[0]) {
        strncpy(curve_names[0], c1, 63);
        curve_count = 1;
    }
    for (int k = 2; k <= 4; ++k) {
        char attr[16];
        snprintf(attr, sizeof(attr), "curve%d", k);
        char ck[64] = {0};
        if (get_xml_attr_str(gfx_tag, attr, ck, sizeof(ck)) && ck[0]) {
            strncpy(curve_names[curve_count], ck, 63);
            curve_count++;
        }
    }
    if (curve_count == 0) {
        strncpy(curve_names[0], level_id, 63);
        curve_count = 1;
    }

    char skullrot[48] = "Tangential (Auto)";
    char extra_val[64] = {0};
    if (get_xml_attr_str(gfx_tag, "skullrot", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(skullrot, sizeof(skullrot), "%s deg", extra_val);
    }

    char space_val[48] = "Disabled (Standard Arena)";
    if (get_xml_attr_str(gfx_tag, "space", extra_val, sizeof(extra_val)) && extra_val[0]) {
        if (_stricmp(extra_val, "true") == 0) {
            strncpy(space_val, "Enabled (Cosmic Arena)", sizeof(space_val) - 1);
        } else {
            snprintf(space_val, sizeof(space_val), "%s", extra_val);
        }
    }

    char zuma_prog_id[64] = "standard";
    char search_lvl[128];
    snprintf(search_lvl, sizeof(search_lvl), "graphics=\"%s\"", level_id);
    const char *lvl_node = strstr(buf, search_lvl);
    if (lvl_node) {
        const char *t_start = lvl_node;
        while (t_start > buf && *t_start != '<') t_start--;
        if (t_start >= buf && strncasecmp(t_start, "<Level", 6) == 0) {
            get_xml_attr_str(t_start, "progression", zuma_prog_id, sizeof(zuma_prog_id));
        }
    }
    if (zuma_prog_id[0] == '\0') {
        strncpy(zuma_prog_id, (curve_count > 1) ? "dual" : "standard", sizeof(zuma_prog_id) - 1);
    }

    char drawcurve[32] = "Default (false)";
    if (get_xml_attr_str(gfx_tag, "drawcurve", extra_val, sizeof(extra_val)) && extra_val[0]) {
        strncpy(drawcurve, extra_val, sizeof(drawcurve) - 1);
    }

    char drawtunnels[32] = "Default (false)";
    if (get_xml_attr_str(gfx_tag, "drawtunnels", extra_val, sizeof(extra_val)) && extra_val[0]) {
        strncpy(drawtunnels, extra_val, sizeof(drawtunnels) - 1);
    }

    char curvecolor[32] = "Default (White)";
    if (get_xml_attr_str(gfx_tag, "curvecolor", extra_val, sizeof(extra_val)) && extra_val[0]) {
        strncpy(curvecolor, extra_val, sizeof(curvecolor) - 1);
    }

    char bgcolor[32] = "Default (0x000000)";
    if (get_xml_attr_str(gfx_tag, "bgcolor", extra_val, sizeof(extra_val)) && extra_val[0]) {
        strncpy(bgcolor, extra_val, sizeof(bgcolor) - 1);
    }

    char destroyall[32] = "Default (false)";
    if (get_xml_attr_str(gfx_tag, "destroyall", extra_val, sizeof(extra_val)) && extra_val[0]) {
        strncpy(destroyall, extra_val, sizeof(destroyall) - 1);
    } else if (find_zuma_settings_value(buf, level_id, "destroyall", extra_val, sizeof(extra_val)) && extra_val[0]) {
        strncpy(destroyall, extra_val, sizeof(destroyall) - 1);
    }

    char huerot[32] = "Default (0.0 deg)";
    if (get_xml_attr_str(gfx_tag, "huerot", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(huerot, sizeof(huerot), "%s deg", extra_val);
    }

    char treasurefreq[32] = "Default (1000 frames)";
    if (find_zuma_settings_value(buf, level_id, "treasurefreq", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(treasurefreq, sizeof(treasurefreq), "%s frames", extra_val);
    }

    char partime[32] = "Default (None / Untimed)";
    if (find_zuma_settings_value(buf, level_id, "partime", extra_val, sizeof(extra_val)) && extra_val[0]) {
        snprintf(partime, sizeof(partime), "%s sec", extra_val);
    }

    /* Count child tags inside block */
    const char *block_end = strstr(gfx_end, "</Graphics>");
    if (!block_end) {
        block_end = strstr(gfx_end, "<Graphics");
        if (!block_end) block_end = strstr(gfx_end, "<Settings");
        if (!block_end) block_end = buf + sz;
    }

    int coins_cnt = 0;
    int cutouts_cnt = 0;
    int bgalphas_cnt = 0;
    int sprites_cnt = 0;

    const char *scan = gfx_end;
    while (scan < block_end) {
        const char *tag_s = strchr(scan, '<');
        if (!tag_s || tag_s >= block_end) break;
        const char *tag_e = strchr(tag_s, '>');
        if (!tag_e || tag_e > block_end) break;

        char elem[512] = {0};
        size_t elen = (size_t)(tag_e - tag_s + 1);
        if (elen >= sizeof(elem)) elen = sizeof(elem) - 1;
        strncpy(elem, tag_s, elen);
        elem[elen] = '\0';

        if (strncasecmp(elem, "<TreasurePoint", 14) == 0) coins_cnt++;
        else if (strncasecmp(elem, "<Cutout", 7) == 0) cutouts_cnt++;
        else if (strncasecmp(elem, "<BackgroundAlpha", 16) == 0) bgalphas_cnt++;
        else if (strncasecmp(elem, "<Sprite", 7) == 0) sprites_cnt++;

        scan = tag_e + 1;
    }
    free(buf);

    char curves_str[256] = {0};
    for (int i = 0; i < curve_count; ++i) {
        if (i > 0) strncat(curves_str, ", ", sizeof(curves_str) - strlen(curves_str) - 1);
        strncat(curves_str, curve_names[i], sizeof(curves_str) - strlen(curves_str) - 1);
        strncat(curves_str, ".dat", sizeof(curves_str) - strlen(curves_str) - 1);
    }

    /* Check disk assets in Zuma folder */
    char zuma_folder[MAX_PATH];
    snprintf(zuma_folder, sizeof(zuma_folder), "%s\\%s", g_zuma_levels_dir, level_id);

    char bg_disk_test[MAX_PATH];
    snprintf(bg_disk_test, sizeof(bg_disk_test), "%s\\%s.jpg", zuma_folder, image_attr);
    bool bg_found = (GetFileAttributesA(bg_disk_test) != INVALID_FILE_ATTRIBUTES);
    if (!bg_found) {
        snprintf(bg_disk_test, sizeof(bg_disk_test), "%s\\%s.png", zuma_folder, image_attr);
        bg_found = (GetFileAttributesA(bg_disk_test) != INVALID_FILE_ATTRIBUTES);
    }

    int curves_found_cnt = 0;
    for (int i = 0; i < curve_count; ++i) {
        char dat_test[MAX_PATH];
        snprintf(dat_test, sizeof(dat_test), "%s\\%s.dat", zuma_folder, curve_names[i]);
        if (GetFileAttributesA(dat_test) != INVALID_FILE_ATTRIBUTES) curves_found_cnt++;
    }

    char frog_str[64];
    if (frog_gx >= 0 && frog_gy >= 0) {
        snprintf(frog_str, sizeof(frog_str), "X: %d, Y: %d", frog_gx, frog_gy);
    } else {
        snprintf(frog_str, sizeof(frog_str), "Not Placed");
    }

    char header[256];
    snprintf(header, sizeof(header), "Zuma Deluxe Map: %s (%s)", dispname, level_id);

    char body[1536];
    snprintf(body, sizeof(body),
        "General Properties:\n"
        "  - Map ID: %s\n"
        "  - Location Path: levels/%s/\n"
        "  - Display Title: %s\n"
        "  - Level Progression: %s\n"
        "  - Skull Rotation: %s\n"
        "  - Space Level: %s\n\n"
        "Level Objects & Elements:\n"
        "  - Frog Shooter Center: %s\n"
        "  - Treasure Coins: %d spawn point(s)\n"
        "  - Curve Tracks: %d track(s) / max 10 [%s]\n"
        "  - Bridge / Cutout Masks: %d tunnel(s)\n"
        "  - Decorative Sprites: %d sprite(s)\n"
        "  - Ambient BG Alphas: %d layer(s)\n"
        "  - Canvas Dimensions: 640 x 480 px (Standard)\n"
        "  - Background Graphic: %s\n\n"
        "Developer Attributes (Testing Kit):\n"
        "  - drawcurve: %s  |  drawtunnels: %s\n"
        "  - curvecolor: %s  |  bgcolor: %s\n"
        "  - destroyall: %s  |  huerot: %s\n"
        "  - treasurefreq: %s  |  partime: %s\n\n"
        "Additional Information:\n"
        "  - Data Source: levels/levels.xml (Zuma Deluxe Directory)\n"
        "  - Asset Status: Backdrop: %s | Rails: %d/%d Verified",
        level_id,
        level_id,
        dispname,
        zuma_prog_id,
        skullrot,
        space_val,
        frog_str,
        coins_cnt,
        curve_count, curves_str,
        cutouts_cnt,
        sprites_cnt,
        bgalphas_cnt,
        image_attr,
        drawcurve, drawtunnels,
        curvecolor, bgcolor,
        destroyall, huerot,
        treasurefreq, partime,
        bg_found ? "Verified" : "Missing / Non-standard",
        curves_found_cnt, curve_count
    );

    show_themed_notice_dialog(parent_hwnd, "Zuma Deluxe Map Attributes", header, body, 600, 560);
}

static void show_canvas_project_attributes_dialog(HWND parent_hwnd, const char *canvas_name) {
    if (!parent_hwnd || !canvas_name || !canvas_name[0]) return;

    char base_dir[MAX_PATH];
    get_canvas_base_directory(base_dir, sizeof(base_dir));

    char folder_path[MAX_PATH];
    snprintf(folder_path, sizeof(folder_path), "%s\\%s", base_dir, canvas_name);

    char cv_name[128] = {0};
    char cv_title[128] = {0};
    int cv_w = DEFAULT_CANVAS_WIDTH;
    int cv_h = DEFAULT_CANVAS_HEIGHT;
    char cv_img[MAX_PATH] = {0};
    int frog_x = 320, frog_y = 240;
    int coins_cnt = 0;
    int cutouts_cnt = 0;
    int sprites_cnt = 0;
    int bgalphas_cnt = 0;

    char skullrot[48] = "Tangential (Auto)";
    char space_val[48] = "Disabled (Standard Arena)";
    char drawcurve[32] = "Default (false)";
    char drawtunnels[32] = "Default (false)";
    char curvecolor[32] = "Default (White)";
    char bgcolor[32] = "Default (0x000000)";
    char destroyall[32] = "Default (false)";
    char huerot[32] = "Default (0.0 deg)";
    char treasurefreq[32] = "Default (1000 frames)";
    char partime[32] = "Default (None / Untimed)";
    char cv_prog_id[64] = {0};
    bool has_xml = false;

    char xml_path[MAX_PATH];
    snprintf(xml_path, sizeof(xml_path), "%s\\settings.xml", folder_path);

    FILE *f = fopen(xml_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz <= 16 * 1024 * 1024) {
            char *buf = (char *)malloc(sz + 1);
            if (buf) {
                fread(buf, 1, sz, f);
                buf[sz] = '\0';
                has_xml = true;

                /* Parse Graphics tag */
                const char *gfx = strstr(buf, "<Graphics");
                if (gfx) {
                    const char *gfx_end = strchr(gfx, '>');
                    if (gfx_end) {
                        char gfx_tag[1024] = {0};
                        size_t tlen = (size_t)(gfx_end - gfx);
                        if (tlen >= sizeof(gfx_tag)) tlen = sizeof(gfx_tag) - 1;
                        strncpy(gfx_tag, gfx, tlen);
                        gfx_tag[tlen] = '\0';

                        get_xml_attr_str(gfx_tag, "id", cv_name, sizeof(cv_name));
                        get_xml_attr_str(gfx_tag, "dispname", cv_title, sizeof(cv_title));
                        get_xml_attr_str(gfx_tag, "image", cv_img, sizeof(cv_img));
                        get_xml_attr_int(gfx_tag, "gx", &frog_x);
                        get_xml_attr_int(gfx_tag, "gy", &frog_y);

                        char extra_val[64] = {0};
                        if (get_xml_attr_str(gfx_tag, "skullrot", extra_val, sizeof(extra_val)) && extra_val[0]) {
                            snprintf(skullrot, sizeof(skullrot), "%s deg", extra_val);
                        }
                        if (get_xml_attr_str(gfx_tag, "space", extra_val, sizeof(extra_val)) && extra_val[0]) {
                            if (_stricmp(extra_val, "true") == 0) {
                                strncpy(space_val, "Enabled (Cosmic Arena)", sizeof(space_val) - 1);
                            } else {
                                snprintf(space_val, sizeof(space_val), "%s", extra_val);
                            }
                        }
                        if (get_xml_attr_str(gfx_tag, "drawcurve", extra_val, sizeof(extra_val)) && extra_val[0]) {
                            strncpy(drawcurve, extra_val, sizeof(drawcurve) - 1);
                        }
                        if (get_xml_attr_str(gfx_tag, "drawtunnels", extra_val, sizeof(extra_val)) && extra_val[0]) {
                            strncpy(drawtunnels, extra_val, sizeof(drawtunnels) - 1);
                        }
                        if (get_xml_attr_str(gfx_tag, "curvecolor", extra_val, sizeof(extra_val)) && extra_val[0]) {
                            strncpy(curvecolor, extra_val, sizeof(curvecolor) - 1);
                        }
                        if (get_xml_attr_str(gfx_tag, "bgcolor", extra_val, sizeof(extra_val)) && extra_val[0]) {
                            strncpy(bgcolor, extra_val, sizeof(bgcolor) - 1);
                        }
                        if (get_xml_attr_str(gfx_tag, "destroyall", extra_val, sizeof(extra_val)) && extra_val[0]) {
                            strncpy(destroyall, extra_val, sizeof(destroyall) - 1);
                        }
                        if (get_xml_attr_str(gfx_tag, "huerot", extra_val, sizeof(extra_val)) && extra_val[0]) {
                            snprintf(huerot, sizeof(huerot), "%s deg", extra_val);
                        }
                    }

                    /* Check Settings tag and comments for partime, treasurefreq, destroyall */
                    char *pt_c = strstr(buf, "ParTime:");
                    if (pt_c) {
                        snprintf(partime, sizeof(partime), "%d sec", atoi(pt_c + 8));
                    }
                    char *tf_c = strstr(buf, "TreasureFreq:");
                    if (tf_c) {
                        snprintf(treasurefreq, sizeof(treasurefreq), "%d frames", atoi(tf_c + 13));
                    }
                    char *da_c = strstr(buf, "DestroyAll:");
                    if (da_c) {
                        char *q = da_c + 11;
                        while (*q == ' ' || *q == '\t') q++;
                        if (strncmp(q, "true", 4) == 0) strncpy(destroyall, "true", sizeof(destroyall) - 1);
                    }
                    const char *st_tag = strstr(buf, "<Settings");
                    if (st_tag) {
                        char extra_s[64] = {0};
                        if (get_xml_attr_str(st_tag, "partime", extra_s, sizeof(extra_s)) && extra_s[0]) {
                            snprintf(partime, sizeof(partime), "%s sec", extra_s);
                        }
                        if (get_xml_attr_str(st_tag, "treasurefreq", extra_s, sizeof(extra_s)) && extra_s[0]) {
                            snprintf(treasurefreq, sizeof(treasurefreq), "%s frames", extra_s);
                        }
                        if (get_xml_attr_str(st_tag, "destroyall", extra_s, sizeof(extra_s)) && extra_s[0]) {
                            strncpy(destroyall, extra_s, sizeof(destroyall) - 1);
                        }
                    }

                    /* Extract Progression */
                    char *prog_c = strstr(buf, "Progression:");
                    if (prog_c) {
                        char *q = prog_c + 12;
                        while (*q == ' ' || *q == '\t') q++;
                        char *endl = strpbrk(q, "\r\n");
                        size_t pl = endl ? (size_t)(endl - q) : strlen(q);
                        if (pl >= sizeof(cv_prog_id)) pl = sizeof(cv_prog_id) - 1;
                        strncpy(cv_prog_id, q, pl);
                        cv_prog_id[pl] = '\0';
                    }
                    if (cv_prog_id[0] == '\0') {
                        const char *lvl_p = strstr(buf, "<Level");
                        if (lvl_p) {
                            get_xml_attr_str(lvl_p, "progression", cv_prog_id, sizeof(cv_prog_id));
                        }
                    }

                    /* Count child elements */
                    const char *scan = gfx;
                    const char *block_end = strstr(gfx, "</Graphics>");
                    if (!block_end) block_end = buf + sz;

                    while (scan < block_end) {
                        const char *tag_s = strchr(scan, '<');
                        if (!tag_s || tag_s >= block_end) break;
                        const char *tag_e = strchr(tag_s, '>');
                        if (!tag_e || tag_e > block_end) break;

                        char elem[512] = {0};
                        size_t elen = (size_t)(tag_e - tag_s + 1);
                        if (elen >= sizeof(elem)) elen = sizeof(elem) - 1;
                        strncpy(elem, tag_s, elen);
                        elem[elen] = '\0';

                        if (strncasecmp(elem, "<TreasurePoint", 14) == 0) coins_cnt++;
                        else if (strncasecmp(elem, "<Cutout", 7) == 0) cutouts_cnt++;
                        else if (strncasecmp(elem, "<BackgroundAlpha", 16) == 0) bgalphas_cnt++;
                        else if (strncasecmp(elem, "<Sprite", 7) == 0) sprites_cnt++;

                        scan = tag_e + 1;
                    }
                }

                /* Also check comments for CanvasWidth / CanvasHeight */
                char *cw = strstr(buf, "CanvasWidth:");
                if (cw) cv_w = atoi(cw + 12);
                char *ch = strstr(buf, "CanvasHeight:");
                if (ch) cv_h = atoi(ch + 13);
                char *bg = strstr(buf, "BackgroundImage:");
                if (bg && cv_img[0] == '\0') {
                    char *q = bg + 16;
                    while (*q == ' ' || *q == '\t') q++;
                    char *endl = strpbrk(q, "\r\n");
                    if (endl) {
                        size_t len = (size_t)(endl - q);
                        if (len >= sizeof(cv_img)) len = sizeof(cv_img) - 1;
                        strncpy(cv_img, q, len);
                        cv_img[len] = '\0';
                    }
                }

                free(buf);
            }
        }
        fclose(f);
    }

    if (cv_name[0] == '\0') strncpy(cv_name, canvas_name, sizeof(cv_name) - 1);
    if (cv_title[0] == '\0') strncpy(cv_title, cv_name, sizeof(cv_title) - 1);
    if (cv_w <= 0) cv_w = DEFAULT_CANVAS_WIDTH;
    if (cv_h <= 0) cv_h = DEFAULT_CANVAS_HEIGHT;

    int path_cnt = 0;
    char path_files[256] = {0};
    for (int p = 0; p < MAX_PATHS; ++p) {
        char test_file[MAX_PATH];
        if (p == 0) {
            snprintf(test_file, sizeof(test_file), "%s\\%s.zupath", folder_path, cv_name);
        } else {
            snprintf(test_file, sizeof(test_file), "%s\\%s_%d.zupath", folder_path, cv_name, p + 1);
        }
        if (GetFileAttributesA(test_file) != INVALID_FILE_ATTRIBUTES) {
            if (path_cnt > 0) strncat(path_files, ", ", sizeof(path_files) - strlen(path_files) - 1);
            char fname[64];
            snprintf(fname, sizeof(fname), (p == 0) ? "%s.zupath" : "%s_%d.zupath", cv_name, p + 1);
            strncat(path_files, fname, sizeof(path_files) - strlen(path_files) - 1);
            path_cnt++;
        }
    }
    if (path_cnt == 0) {
        char test_dat[MAX_PATH];
        snprintf(test_dat, sizeof(test_dat), "%s\\%s.dat", folder_path, cv_name);
        if (GetFileAttributesA(test_dat) != INVALID_FILE_ATTRIBUTES) {
            path_cnt = 1;
            snprintf(path_files, sizeof(path_files), "%s.dat", cv_name);
        }
    }

    char frog_str[64];
    if (frog_x >= 0 && frog_y >= 0) {
        snprintf(frog_str, sizeof(frog_str), "X: %d, Y: %d", frog_x, frog_y);
    } else {
        snprintf(frog_str, sizeof(frog_str), "Not Placed");
    }

    char header[256];
    snprintf(header, sizeof(header), "Canvas Project: %s (%s)", cv_title, cv_name);

    if (cv_prog_id[0] == '\0') {
        snprintf(cv_prog_id, sizeof(cv_prog_id), "zupath%d-medium", path_cnt > 0 ? path_cnt : 1);
    }

    char body[1536];
    snprintf(body, sizeof(body),
        "General Properties:\n"
        "  - Map ID: %s\n"
        "  - Location Path: ./canvas/%s/\n"
        "  - Display Title: %s\n"
        "  - Level Progression: %s\n"
        "  - Skull Rotation: %s\n"
        "  - Space Level: %s\n\n"
        "Level Objects & Elements:\n"
        "  - Frog Shooter Center: %s\n"
        "  - Treasure Coins: %d spawn point(s)\n"
        "  - Curve Tracks: %d track(s) / max 10 [%s]\n"
        "  - Bridge / Cutout Masks: %d tunnel(s)\n"
        "  - Decorative Sprites: %d sprite(s)\n"
        "  - Ambient BG Alphas: %d layer(s)\n"
        "  - Canvas Dimensions: %d x %d px\n"
        "  - Background Graphic: %s\n\n"
        "Developer Attributes (Testing Kit):\n"
        "  - drawcurve: %s  |  drawtunnels: %s\n"
        "  - curvecolor: %s  |  bgcolor: %s\n"
        "  - destroyall: %s  |  huerot: %s\n"
        "  - treasurefreq: %s  |  partime: %s\n\n"
        "Additional Information:\n"
        "  - Data Source: ./canvas/%s/settings.xml (ZuPather Canvas Project)\n"
        "  - Asset Status: Configuration: %s | Vector Tracks: %s",
        cv_name,
        cv_name,
        cv_title,
        cv_prog_id,
        skullrot,
        space_val,
        frog_str,
        coins_cnt,
        path_cnt,
        path_cnt > 0 ? path_files : "No paths drawn yet",
        cutouts_cnt,
        sprites_cnt,
        bgalphas_cnt,
        cv_w, cv_h,
        cv_img[0] ? cv_img : "None (Blank Canvas)",
        drawcurve, drawtunnels,
        curvecolor, bgcolor,
        destroyall, huerot,
        treasurefreq, partime,
        cv_name,
        has_xml ? "settings.xml OK" : "Auto-detected from directory",
        path_cnt > 0 ? "Sidecar OK" : "None"
    );

    show_themed_notice_dialog(parent_hwnd, "Canvas Project Attributes", header, body, 600, 560);
}

/* ========================================================================= */
/* DynBuffer: Lightweight Dynamic String Buffer for Safe XML Operations      */
/* ========================================================================= */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} DynBuffer;

static void dyn_init(DynBuffer *b, size_t initial_cap) {
    if (initial_cap < 512) initial_cap = 512;
    b->data = (char *)malloc(initial_cap);
    b->len = 0;
    b->cap = initial_cap;
    if (b->data) b->data[0] = '\0';
}

static void dyn_free(DynBuffer *b) {
    if (b->data) {
        free(b->data);
        b->data = NULL;
    }
    b->len = 0;
    b->cap = 0;
}

static bool dyn_ensure(DynBuffer *b, size_t add_len) {
    if (b->len + add_len + 1 > b->cap) {
        size_t new_cap = (b->cap * 3) / 2 + add_len + 2048;
        char *p = (char *)realloc(b->data, new_cap);
        if (!p) return false;
        b->data = p;
        b->cap = new_cap;
    }
    return true;
}

static bool dyn_append_str(DynBuffer *b, const char *str) {
    if (!str) return true;
    size_t slen = strlen(str);
    if (!dyn_ensure(b, slen)) return false;
    memcpy(b->data + b->len, str, slen);
    b->len += slen;
    b->data[b->len] = '\0';
    return true;
}

static bool dyn_insert_str(DynBuffer *b, size_t pos, const char *str) {
    if (!str) return true;
    if (pos > b->len) pos = b->len;
    size_t slen = strlen(str);
    if (!dyn_ensure(b, slen)) return false;
    memmove(b->data + pos + slen, b->data + pos, b->len - pos + 1);
    memcpy(b->data + pos, str, slen);
    b->len += slen;
    return true;
}

static bool dyn_replace_range(DynBuffer *b, size_t start_pos, size_t end_pos, const char *replacement) {
    if (start_pos > b->len) start_pos = b->len;
    if (end_pos > b->len) end_pos = b->len;
    if (end_pos < start_pos) end_pos = start_pos;
    size_t old_len = end_pos - start_pos;
    size_t rep_len = replacement ? strlen(replacement) : 0;

    if (rep_len > old_len) {
        if (!dyn_ensure(b, rep_len - old_len)) return false;
    }
    memmove(b->data + start_pos + rep_len, b->data + end_pos, b->len - end_pos + 1);
    if (rep_len > 0) {
        memcpy(b->data + start_pos, replacement, rep_len);
    }
    b->len = b->len - old_len + rep_len;
    b->data[b->len] = '\0';
    return true;
}

static void format_progression_settings_string(const ZumaLevelProgressionDef *prog, DynBuffer *out_buf) {
    if (!prog || !out_buf) return;
    char line[512];
    for (int i = 0; i < prog->step_count; ++i) {
        const ZumaProgressionSetting *s = &prog->steps[i];
        snprintf(line, sizeof(line), "<Settings id=\"%s\" speed=\"%.2f\" start=\"%d\" score=\"%d\"",
                 s->id, s->speed, s->start, s->score);
        dyn_append_str(out_buf, line);

        if (prog->difficulty == LEVEL_DIFF_DEV_TEST && s->repeat > 0) {
            snprintf(line, sizeof(line), " repeat=\"%d\"", s->repeat);
            dyn_append_str(out_buf, line);
        }
        if (s->slowfactor > 0.05f) {
            snprintf(line, sizeof(line), " slowfactor=\"%.1f\"", s->slowfactor);
            dyn_append_str(out_buf, line);
        }
        if (s->powerfreq > 0) {
            snprintf(line, sizeof(line), " powerfreq=\"%d\"", s->powerfreq);
            dyn_append_str(out_buf, line);
        }
        if (s->destroyall) {
            dyn_append_str(out_buf, " destroyall=\"true\"");
        }
        if (s->firespeed != 6 && s->firespeed > 0) {
            snprintf(line, sizeof(line), " firespeed=\"%d\"", s->firespeed);
            dyn_append_str(out_buf, line);
        }
        if (s->partime > 0) {
            snprintf(line, sizeof(line), " partime=\"%d\"", s->partime);
            dyn_append_str(out_buf, line);
        }

        dyn_append_str(out_buf, " />\r\n");
    }
}

static void format_progression_def_string(const ZumaLevelProgressionDef *prog, DynBuffer *out_buf) {
    if (!prog || !out_buf) return;
    char line[1024];
    snprintf(line, sizeof(line), "<LevelProgression\r\n\tid=\"%s\"\r\n\tsettings=\"", prog->id);
    dyn_append_str(out_buf, line);

    for (int i = 0; i < prog->step_count; ++i) {
        snprintf(line, sizeof(line), "%s%s", prog->steps[i].id, (i + 1 < prog->step_count) ? "," : "");
        dyn_append_str(out_buf, line);
    }
    dyn_append_str(out_buf, "\"\r\n\tdifficulty=\"");

    for (int i = 0; i < prog->step_count; ++i) {
        snprintf(line, sizeof(line), "%d%s", i + 1, (i + 1 < prog->step_count) ? "," : "");
        dyn_append_str(out_buf, line);
    }
    dyn_append_str(out_buf, "\"\r\n\t/>\r\n");
}

static bool extract_cleaned_graphics_block(const char *cv_xml, const char *canvas_name, DynBuffer *out_gfx) {
    if (!cv_xml || !canvas_name || !out_gfx) return false;
    const char *gfx_start = strstr(cv_xml, "<Graphics");
    if (!gfx_start) return false;
    const char *gfx_end = strstr(gfx_start, "</Graphics>");
    if (!gfx_end) return false;
    gfx_end += strlen("</Graphics>");

    /* Extract lines, filtering out placeholder template comments */
    const char *line_p = gfx_start;
    while (line_p < gfx_end) {
        const char *next_line = strchr(line_p, '\n');
        if (!next_line || next_line > gfx_end) next_line = gfx_end;
        else next_line++;

        size_t line_len = (size_t)(next_line - line_p);
        char line_buf[1024];
        if (line_len >= sizeof(line_buf)) line_len = sizeof(line_buf) - 1;
        strncpy(line_buf, line_p, line_len);
        line_buf[line_len] = '\0';

        if (!strstr(line_buf, "<!-- <") && !strstr(line_buf, "<!-- Zuma Deluxe Visual Elements")) {
            dyn_append_str(out_gfx, line_buf);
        }

        line_p = next_line;
    }

    if (out_gfx->len > 0 && out_gfx->data[out_gfx->len - 1] != '\n') {
        dyn_append_str(out_gfx, "\r\n");
    }

    return (out_gfx->len > 0);
}

/* (Original Zuma Deluxe Map Protection is rendered as a floating popup card via show_original_map_protected_popup_at_point) */

/* ========================================================================= */
/* Export to Maps: Full-featured Canvas Project -> Zuma Deluxe Exporter      */
/* ========================================================================= */
static bool export_canvas_to_zuma_map(HWND parent_hwnd, const char *canvas_name) {
    if (!g_has_zuma_dir || g_zuma_levels_dir[0] == '\0') {
        show_themed_notice_dialog(parent_hwnd, "Notice", "Zuma Folder Required",
                                  "Please configure the Zuma Deluxe folder first.", 460, 240);
        return false;
    }

    if (!canvas_name || canvas_name[0] == '\0') {
        show_themed_notice_dialog(parent_hwnd, "Notice", "No Canvas Selected",
                                  "Please select a canvas project from the list to export.", 460, 240);
        return false;
    }

    if (!is_valid_zuma_level_id(canvas_name)) {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg),
            "Canvas project name '%s' cannot be exported to Zuma Deluxe.\n\n"
            "Zuma Deluxe requires level identifiers to:\n"
            "- Start with a lowercase letter [a-z]\n"
            "- Contain only lowercase letters, digits, underscores, or dashes\n"
            "- Be 32 characters or fewer\n\n"
            "Please rename this canvas project before exporting.",
            canvas_name);
        show_themed_notice_dialog(parent_hwnd, "Invalid Level ID - ZuPather", "Level ID Incompatible", err_msg, 520, 280);
        return false;
    }

    /* 1. Locate Source Canvas Project Directory & settings.xml */
    char base_canvas_dir[MAX_PATH];
    get_canvas_base_directory(base_canvas_dir, sizeof(base_canvas_dir));

    char src_canvas_dir[MAX_PATH];
    snprintf(src_canvas_dir, sizeof(src_canvas_dir), "%s\\%s", base_canvas_dir, canvas_name);

    char settings_xml_path[MAX_PATH];
    snprintf(settings_xml_path, sizeof(settings_xml_path), "%s\\settings.xml", src_canvas_dir);

    FILE *f_cv = fopen(settings_xml_path, "rb");
    if (!f_cv) {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg),
            "Could not open 'settings.xml' in:\n%s\n\n"
            "Please open and save this canvas in ZuPather before exporting.",
            src_canvas_dir);
        show_themed_notice_dialog(parent_hwnd, "Export Error - ZuPather", "Missing settings.xml", err_msg, 520, 260);
        return false;
    }

    fseek(f_cv, 0, SEEK_END);
    long cv_sz = ftell(f_cv);
    fseek(f_cv, 0, SEEK_SET);
    if (cv_sz <= 0 || cv_sz > 8 * 1024 * 1024) {
        fclose(f_cv);
        show_themed_notice_dialog(parent_hwnd, "Export Error - ZuPather", "Corrupted settings.xml",
                                  "Canvas settings.xml is empty or exceeds allowable size limit.", 480, 240);
        return false;
    }

    char *cv_xml = (char *)malloc(cv_sz + 1);
    if (!cv_xml) {
        fclose(f_cv);
        return false;
    }
    fread(cv_xml, 1, cv_sz, f_cv);
    fclose(f_cv);
    cv_xml[cv_sz] = '\0';

    /* 2. Extract Cleaned <Graphics> Block */
    DynBuffer cleaned_gfx;
    dyn_init(&cleaned_gfx, 2048);
    if (!extract_cleaned_graphics_block(cv_xml, canvas_name, &cleaned_gfx)) {
        free(cv_xml);
        dyn_free(&cleaned_gfx);
        show_themed_notice_dialog(parent_hwnd, "Export Error - ZuPather", "Malformed settings.xml",
                                  "Could not find a valid <Graphics> block in canvas settings.xml.", 500, 240);
        return false;
    }

    /* 3. Parse Track Count, Curve Names, and Difficulty */
    const char *gfx_tag = strstr(cv_xml, "<Graphics");
    int curve_count = 0;
    char curve_names[10][64];
    memset(curve_names, 0, sizeof(curve_names));

    if (gfx_tag) {
        const char *tag_end = strchr(gfx_tag, '>');
        if (tag_end) {
            size_t tlen = tag_end - gfx_tag + 1;
            char tag_hdr[1024];
            if (tlen >= sizeof(tag_hdr)) tlen = sizeof(tag_hdr) - 1;
            strncpy(tag_hdr, gfx_tag, tlen);
            tag_hdr[tlen] = '\0';

            char ck[64] = {0};
            if (get_loose_xml_attr_str(tag_hdr, "curve", ck, sizeof(ck)) && ck[0]) {
                strncpy(curve_names[0], ck, 63);
                curve_count = 1;
            }
            for (int k = 2; k <= 10; ++k) {
                char attr[16];
                snprintf(attr, sizeof(attr), "curve%d", k);
                if (get_loose_xml_attr_str(tag_hdr, attr, ck, sizeof(ck)) && ck[0]) {
                    strncpy(curve_names[curve_count], ck, 63);
                    curve_count++;
                }
            }
        }
    }
    if (curve_count == 0) {
        strncpy(curve_names[0], canvas_name, 63);
        curve_count = 1;
    }

    LevelDifficulty diff = LEVEL_DIFF_MEDIUM;
    const char *p_diff = strstr(cv_xml, "Difficulty:");
    if (p_diff) {
        p_diff += 11;
        while (*p_diff == ' ' || *p_diff == '\t') p_diff++;
        char diff_str[32] = {0};
        const char *eol = strpbrk(p_diff, "\r\n");
        size_t dl = eol ? (size_t)(eol - p_diff) : strlen(p_diff);
        if (dl >= sizeof(diff_str)) dl = sizeof(diff_str) - 1;
        strncpy(diff_str, p_diff, dl);
        diff_str[dl] = '\0';
        diff = level_difficulty_from_string(diff_str);
    } else {
        const char *p_prog = strstr(cv_xml, "Progression:");
        if (p_prog) {
            p_prog += 12;
            while (*p_prog == ' ' || *p_prog == '\t') p_prog++;
            const char *dash = strchr(p_prog, '-');
            if (dash) {
                char diff_str[32] = {0};
                const char *eol = strpbrk(dash + 1, "\r\n");
                size_t dl = eol ? (size_t)(eol - (dash + 1)) : strlen(dash + 1);
                if (dl >= sizeof(diff_str)) dl = sizeof(diff_str) - 1;
                strncpy(diff_str, dash + 1, dl);
                diff_str[dl] = '\0';
                diff = level_difficulty_from_string(diff_str);
            }
        }
    }

    char prog_id[64];
    get_level_progression_id(curve_count, diff, prog_id, sizeof(prog_id));

    /* 4. Check if Level Already Exists in Zuma Deluxe */
    char zuma_xml_path[MAX_PATH];
    snprintf(zuma_xml_path, sizeof(zuma_xml_path), "%s\\levels.xml", g_zuma_levels_dir);

    char target_zuma_dir[MAX_PATH];
    snprintf(target_zuma_dir, sizeof(target_zuma_dir), "%s\\%s", g_zuma_levels_dir, canvas_name);

    bool folder_exists = (GetFileAttributesA(target_zuma_dir) != INVALID_FILE_ATTRIBUTES);

    FILE *f_zx = fopen(zuma_xml_path, "rb");
    if (!f_zx) {
        free(cv_xml);
        dyn_free(&cleaned_gfx);
        show_themed_notice_dialog(parent_hwnd, "Export Error - ZuPather", "Missing levels.xml",
                                  "Could not open levels.xml in the configured Zuma Deluxe directory.", 500, 240);
        return false;
    }

    fseek(f_zx, 0, SEEK_END);
    long zx_sz = ftell(f_zx);
    fseek(f_zx, 0, SEEK_SET);
    if (zx_sz <= 0 || zx_sz > 16 * 1024 * 1024) {
        fclose(f_zx);
        free(cv_xml);
        dyn_free(&cleaned_gfx);
        show_themed_notice_dialog(parent_hwnd, "Export Error - ZuPather", "Corrupted levels.xml",
                                  "Zuma levels.xml is empty or exceeds allowable size limit.", 480, 240);
        return false;
    }

    DynBuffer dyn_xml;
    dyn_init(&dyn_xml, zx_sz + 16384);
    fread(dyn_xml.data, 1, zx_sz, f_zx);
    fclose(f_zx);
    dyn_xml.data[zx_sz] = '\0';
    dyn_xml.len = zx_sz;

    /* Search for existing <Graphics ... id="canvas_name" ...> */
    const char *existing_gfx_ptr = NULL;
    const char *p_scan = dyn_xml.data;
    while ((p_scan = strstr(p_scan, "<Graphics")) != NULL) {
        const char *tag_close = strchr(p_scan, '>');
        if (tag_close) {
            size_t tlen = (size_t)(tag_close - p_scan);
            char tag_hdr[1024];
            if (tlen >= sizeof(tag_hdr)) tlen = sizeof(tag_hdr) - 1;
            strncpy(tag_hdr, p_scan, tlen);
            tag_hdr[tlen] = '\0';

            char found_id[64] = {0};
            if (get_loose_xml_attr_str(tag_hdr, "id", found_id, sizeof(found_id)) &&
                _stricmp(found_id, canvas_name) == 0) {
                existing_gfx_ptr = p_scan;
                break;
            }
        }
        p_scan += 9;
    }

    ZumaMapType mtype = classify_zuma_map(dyn_xml.data, canvas_name, NULL, 0);
    bool is_replace = (existing_gfx_ptr != NULL) || folder_exists;

    /* 5. Confirmation Prompt & Protection Check */
    if (mtype == ZUMA_MAP_ORIGINAL) {
        /* 100% BLOCKED from overwriting original Zuma Deluxe map! */
        free(cv_xml);
        dyn_free(&cleaned_gfx);
        dyn_free(&dyn_xml);

        show_rename_canvas_and_export_dialog(parent_hwnd, canvas_name);
        return false;
    }

    if (is_replace) {
        const char *type_desc = (mtype == ZUMA_MAP_CUSTOM) ? "ZuPather Custom Map" : "Zuma Misc / Unregistered Map";
        char confirm_msg[600];
        snprintf(confirm_msg, sizeof(confirm_msg),
            "Map '%s' (%s) already exists in Zuma Deluxe.\n\n"
            "Exporting will update the <Graphics> definition in levels.xml, set progression (%s), and overwrite track curves and assets in:\n"
            "%s\\%s\n\n"
            "A safety backup file (levels.xml.bak) will be created automatically.\n\n"
            "Are you sure you want to overwrite this map in Zuma Deluxe?",
            canvas_name, type_desc, prog_id, g_zuma_levels_dir, canvas_name);
        if (!show_themed_confirm_dialog(parent_hwnd, "Confirm Map Overwrite - ZuPather", "Overwrite Map in Zuma Deluxe?", confirm_msg, false)) {
            free(cv_xml);
            dyn_free(&cleaned_gfx);
            dyn_free(&dyn_xml);
            return false;
        }
    } else {
        char confirm_msg[600];
        snprintf(confirm_msg, sizeof(confirm_msg),
            "Export canvas project '%s' to Zuma Deluxe?\n\n"
            "This will add <Graphics>, progression settings (%s), and <Level> mapping to levels.xml, and copy all level assets to:\n"
            "%s\\%s\n\n"
            "A safety backup of levels.xml will be created as levels.xml.bak.\n\n"
            "Do you want to proceed?",
            canvas_name, prog_id, g_zuma_levels_dir, canvas_name);
        if (!show_themed_confirm_dialog(parent_hwnd, "Export to Maps - ZuPather", "Export Map to Zuma Deluxe", confirm_msg, false)) {
            free(cv_xml);
            dyn_free(&cleaned_gfx);
            dyn_free(&dyn_xml);
            return false;
        }
    }

    /* 6. Modify levels.xml: Step A - <Graphics> */
    if (existing_gfx_ptr) {
        const char *existing_gfx_close = strstr(existing_gfx_ptr, "</Graphics>");
        if (existing_gfx_close) {
            existing_gfx_close += strlen("</Graphics>");
            while (*existing_gfx_close == ' ' || *existing_gfx_close == '\t' ||
                   *existing_gfx_close == '\r' || *existing_gfx_close == '\n') {
                existing_gfx_close++;
            }
            size_t start_off = existing_gfx_ptr - dyn_xml.data;
            size_t end_off = existing_gfx_close - dyn_xml.data;
            dyn_replace_range(&dyn_xml, start_off, end_off, cleaned_gfx.data);
        }
    } else {
        /* Find last </Graphics> in levels.xml */
        const char *last_gfx = NULL;
        const char *s_pos = dyn_xml.data;
        while ((s_pos = strstr(s_pos, "</Graphics>")) != NULL) {
            last_gfx = s_pos;
            s_pos += 11;
        }

        if (last_gfx) {
            last_gfx += 11;
            while (*last_gfx == ' ' || *last_gfx == '\t') last_gfx++;
            if (*last_gfx == '\r') last_gfx++;
            if (*last_gfx == '\n') last_gfx++;

            size_t ins_off = last_gfx - dyn_xml.data;
            if (strstr(dyn_xml.data, "<!-- ZuPather Exported Graphics -->") == NULL) {
                DynBuffer block;
                dyn_init(&block, cleaned_gfx.len + 64);
                dyn_append_str(&block, "\r\n<!-- ZuPather Exported Graphics -->\r\n");
                dyn_append_str(&block, cleaned_gfx.data);
                dyn_insert_str(&dyn_xml, ins_off, block.data);
                dyn_free(&block);
            } else {
                dyn_insert_str(&dyn_xml, ins_off, cleaned_gfx.data);
            }
        } else {
            const char *end_lvl = strstr(dyn_xml.data, "</Levels>");
            size_t ins_off = end_lvl ? (size_t)(end_lvl - dyn_xml.data) : dyn_xml.len;
            dyn_insert_str(&dyn_xml, ins_off, cleaned_gfx.data);
        }
    }
    dyn_free(&cleaned_gfx);

    /* 7. Modify levels.xml: Step B - <Settings> */
    ZumaLevelProgressionDef prog_def;
    generate_zuma_level_progression(curve_count, diff, &prog_def);

    if (strstr(dyn_xml.data, prog_def.steps[0].id) == NULL) {
        DynBuffer st_buf;
        dyn_init(&st_buf, 4096);
        format_progression_settings_string(&prog_def, &st_buf);

        const char *last_st = NULL;
        const char *st_scan = dyn_xml.data;
        while ((st_scan = strstr(st_scan, "<Settings")) != NULL) {
            last_st = st_scan;
            st_scan += 9;
        }

        if (last_st) {
            const char *st_end = strchr(last_st, '>');
            if (st_end) {
                st_end++;
                while (*st_end == ' ' || *st_end == '\t') st_end++;
                if (*st_end == '\r') st_end++;
                if (*st_end == '\n') st_end++;

                size_t st_off = st_end - dyn_xml.data;
                if (strstr(dyn_xml.data, "<!-- ZuPather Exported Settings -->") == NULL) {
                    DynBuffer st_comment;
                    dyn_init(&st_comment, st_buf.len + 64);
                    dyn_append_str(&st_comment, "\r\n<!-- ZuPather Exported Settings -->\r\n");
                    dyn_append_str(&st_comment, st_buf.data);
                    dyn_insert_str(&dyn_xml, st_off, st_comment.data);
                    dyn_free(&st_comment);
                } else {
                    dyn_insert_str(&dyn_xml, st_off, st_buf.data);
                }
            }
        }
        dyn_free(&st_buf);
    }

    /* 8. Modify levels.xml: Step C - <LevelProgression> */
    char lp_search[128];
    snprintf(lp_search, sizeof(lp_search), "id=\"%s\"", prog_def.id);
    bool lp_exists = (strstr(dyn_xml.data, lp_search) != NULL);
    if (!lp_exists) {
        snprintf(lp_search, sizeof(lp_search), "id='%s'", prog_def.id);
        lp_exists = (strstr(dyn_xml.data, lp_search) != NULL);
    }

    if (!lp_exists) {
        DynBuffer lp_buf;
        dyn_init(&lp_buf, 1024);
        format_progression_def_string(&prog_def, &lp_buf);

        const char *last_lp = NULL;
        const char *lp_scan = dyn_xml.data;
        while ((lp_scan = strstr(lp_scan, "<LevelProgression")) != NULL) {
            last_lp = lp_scan;
            lp_scan += 17;
        }

        if (last_lp) {
            const char *lp_end = strstr(last_lp, "/>");
            if (!lp_end) lp_end = strstr(last_lp, "</LevelProgression>");
            if (lp_end) {
                lp_end += (*lp_end == '/') ? 2 : strlen("</LevelProgression>");
                while (*lp_end == ' ' || *lp_end == '\t') lp_end++;
                if (*lp_end == '\r') lp_end++;
                if (*lp_end == '\n') lp_end++;

                size_t lp_off = lp_end - dyn_xml.data;
                if (strstr(dyn_xml.data, "<!-- ZuPather Exported LevelProgression -->") == NULL) {
                    DynBuffer lp_comment;
                    dyn_init(&lp_comment, lp_buf.len + 64);
                    dyn_append_str(&lp_comment, "\r\n<!-- ZuPather Exported LevelProgression -->\r\n");
                    dyn_append_str(&lp_comment, lp_buf.data);
                    dyn_insert_str(&dyn_xml, lp_off, lp_comment.data);
                    dyn_free(&lp_comment);
                } else {
                    dyn_insert_str(&dyn_xml, lp_off, lp_buf.data);
                }
            }
        }
        dyn_free(&lp_buf);
    }

    /* 9. Modify levels.xml: Step D - <Level> */
    const char *existing_lvl = NULL;
    const char *lvl_scan = dyn_xml.data;
    while ((lvl_scan = strstr(lvl_scan, "<Level")) != NULL) {
        char next_c = lvl_scan[6];
        if (next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c == '\n') {
            const char *tag_end = strchr(lvl_scan, '>');
            if (tag_end) {
                size_t tlen = (size_t)(tag_end - lvl_scan + 1);
                char tag_buf[512];
                if (tlen >= sizeof(tag_buf)) tlen = sizeof(tag_buf) - 1;
                strncpy(tag_buf, lvl_scan, tlen);
                tag_buf[tlen] = '\0';

                char gfx_attr[64] = {0};
                if (get_loose_xml_attr_str(tag_buf, "graphics", gfx_attr, sizeof(gfx_attr)) &&
                    _stricmp(gfx_attr, canvas_name) == 0) {
                    existing_lvl = lvl_scan;
                    break;
                }
            }
        }
        lvl_scan += 6;
    }

    char new_lvl_tag[256];
    snprintf(new_lvl_tag, sizeof(new_lvl_tag), "<Level graphics=\"%s\" progression=\"%s\" />\r\n",
             canvas_name, prog_id);

    if (existing_lvl) {
        const char *lvl_close = strchr(existing_lvl, '>');
        if (lvl_close) {
            lvl_close++;
            while (*lvl_close == ' ' || *lvl_close == '\t' || *lvl_close == '\r' || *lvl_close == '\n') lvl_close++;
            size_t start_off = existing_lvl - dyn_xml.data;
            size_t end_off = lvl_close - dyn_xml.data;
            dyn_replace_range(&dyn_xml, start_off, end_off, new_lvl_tag);
        }
    } else {
        const char *last_lvl = NULL;
        const char *scan2 = dyn_xml.data;
        while ((scan2 = strstr(scan2, "<Level")) != NULL) {
            char next_c = scan2[6];
            if (next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c == '\n') {
                last_lvl = scan2;
            }
            scan2 += 6;
        }

        if (last_lvl) {
            const char *lvl_end = strchr(last_lvl, '>');
            if (lvl_end) {
                lvl_end++;
                while (*lvl_end == ' ' || *lvl_end == '\t') lvl_end++;
                if (*lvl_end == '\r') lvl_end++;
                if (*lvl_end == '\n') lvl_end++;

                size_t lvl_off = lvl_end - dyn_xml.data;
                if (strstr(dyn_xml.data, "<!-- ZuPather Exported Level -->") == NULL) {
                    DynBuffer lvl_comment;
                    dyn_init(&lvl_comment, sizeof(new_lvl_tag) + 64);
                    dyn_append_str(&lvl_comment, "\r\n<!-- ZuPather Exported Level -->\r\n");
                    dyn_append_str(&lvl_comment, new_lvl_tag);
                    dyn_insert_str(&dyn_xml, lvl_off, lvl_comment.data);
                    dyn_free(&lvl_comment);
                } else {
                    dyn_insert_str(&dyn_xml, lvl_off, new_lvl_tag);
                }
            }
        }
    }

    /* 10. Atomic Save levels.xml with Backup */
    char bak_path[MAX_PATH];
    snprintf(bak_path, sizeof(bak_path), "%s\\levels.xml.bak", g_zuma_levels_dir);
    CopyFileA(zuma_xml_path, bak_path, FALSE);

    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s\\levels.xml.tmp", g_zuma_levels_dir);
    FILE *fout = fopen(tmp_path, "wb");
    if (!fout) {
        dyn_free(&dyn_xml);
        free(cv_xml);
        show_themed_notice_dialog(parent_hwnd, "Export Error - ZuPather", "Failed to Write levels.xml.tmp",
                                  "Could not create temporary XML file. Check folder permissions.", 520, 260);
        return false;
    }
    fwrite(dyn_xml.data, 1, dyn_xml.len, fout);
    fclose(fout);

    if (!CopyFileA(tmp_path, zuma_xml_path, FALSE)) {
        dyn_free(&dyn_xml);
        free(cv_xml);
        show_themed_notice_dialog(parent_hwnd, "Export Error - ZuPather", "Failed to Update levels.xml",
                                  "Failed to replace levels.xml. The backup levels.xml.bak is preserved.", 520, 260);
        return false;
    }
    DeleteFileA(tmp_path);
    dyn_free(&dyn_xml);

    /* 11. Copy and Export Assets to Zuma Deluxe levels/[canvas_name]/ */
    CreateDirectoryA(target_zuma_dir, NULL);

    int curves_exported = 0;
    int curves_failed = 0;

    for (int k = 0; k < curve_count; ++k) {
        const char *cname = curve_names[k];
        char dat_fn[MAX_PATH];
        snprintf(dat_fn, sizeof(dat_fn), "%s.dat", cname);

        char src_dat[MAX_PATH], dst_dat[MAX_PATH];
        snprintf(src_dat, sizeof(src_dat), "%s\\%s", src_canvas_dir, dat_fn);
        snprintf(dst_dat, sizeof(dst_dat), "%s\\%s", target_zuma_dir, dat_fn);

        bool dat_ok = false;
        if (GetFileAttributesA(src_dat) != INVALID_FILE_ATTRIBUTES) {
            dat_ok = CopyFileA(src_dat, dst_dat, FALSE);
        }

        if (!dat_ok) {
            /* Fallback: compile directly from .zupath vector sidecar */
            char zupath_fp[MAX_PATH];
            snprintf(zupath_fp, sizeof(zupath_fp), "%s\\%s.zupath", src_canvas_dir, canvas_name);
            BezierPath *loaded_paths = NULL;
            int loaded_cnt = 0;
            if (bezier_path_load_zupath(&loaded_paths, &loaded_cnt, zupath_fp) && loaded_paths) {
                if (k < loaded_cnt && loaded_paths[k].node_count >= 2) {
                    dat_ok = bezier_path_export_dat(&loaded_paths[k], dst_dat);
                }
                for (int p = 0; p < loaded_cnt; ++p) bezier_path_free(&loaded_paths[p]);
                free(loaded_paths);
            }
        }

        if (dat_ok) {
            curves_exported++;
        } else {
            curves_failed++;
        }
    }

    /* Prune obsolete .dat files in target_zuma_dir */
    if (curve_count == 1) {
        for (int p = 1; p <= 64; ++p) {
            char stale[MAX_PATH];
            snprintf(stale, sizeof(stale), "%s\\%s-%d.dat", target_zuma_dir, canvas_name, p);
            DeleteFileA(stale);
        }
    } else {
        char stale_single[MAX_PATH];
        snprintf(stale_single, sizeof(stale_single), "%s\\%s.dat", target_zuma_dir, canvas_name);
        DeleteFileA(stale_single);
        for (int p = curve_count + 1; p <= 64; ++p) {
            char stale_multi[MAX_PATH];
            snprintf(stale_multi, sizeof(stale_multi), "%s\\%s-%d.dat", target_zuma_dir, canvas_name, p);
            DeleteFileA(stale_multi);
        }
    }

    /* Background image export / copy */
    char bg_src_filename[MAX_PATH] = {0};
    const char *p_bg = strstr(cv_xml, "BackgroundImage:");
    if (p_bg) {
        p_bg += 16;
        while (*p_bg == ' ' || *p_bg == '\t') p_bg++;
        const char *eol = strpbrk(p_bg, "\r\n");
        if (eol) {
            size_t l = (size_t)(eol - p_bg);
            if (l < sizeof(bg_src_filename)) {
                strncpy(bg_src_filename, p_bg, l);
                bg_src_filename[l] = '\0';
                if (_stricmp(bg_src_filename, "none") == 0) bg_src_filename[0] = '\0';
            }
        }
    }

    const char *bg_exts[] = {"jpg", "jpeg", "png", "webp", "gif", "bmp"};
    if (bg_src_filename[0] == '\0') {
        for (int e = 0; e < 6; ++e) {
            char t_fn[MAX_PATH], t_fp[MAX_PATH];
            snprintf(t_fn, sizeof(t_fn), "%s.%s", canvas_name, bg_exts[e]);
            snprintf(t_fp, sizeof(t_fp), "%s\\%s", src_canvas_dir, t_fn);
            if (GetFileAttributesA(t_fp) != INVALID_FILE_ATTRIBUTES) {
                strncpy(bg_src_filename, t_fn, sizeof(bg_src_filename) - 1);
                break;
            }
        }
    }

    char bg_status[128] = "None (Blank Canvas)";
    if (bg_src_filename[0] != '\0') {
        char bg_src_full[MAX_PATH];
        snprintf(bg_src_full, sizeof(bg_src_full), "%s\\%s", src_canvas_dir, bg_src_filename);
        char ext[16] = {0};
        image_get_extension(bg_src_filename, ext, sizeof(ext));

        if (_stricmp(ext, "jpg") == 0 || _stricmp(ext, "jpeg") == 0 || _stricmp(ext, "png") == 0) {
            char bg_dst_full[MAX_PATH];
            snprintf(bg_dst_full, sizeof(bg_dst_full), "%s\\%s.%s", target_zuma_dir, canvas_name, ext);
            if (CopyFileA(bg_src_full, bg_dst_full, FALSE)) {
                snprintf(bg_status, sizeof(bg_status), "%s.%s", canvas_name, ext);
            }
        } else {
            CanvasImage cimg;
            memset(&cimg, 0, sizeof(CanvasImage));
            if (image_load_from_file(bg_src_full, &cimg)) {
                char bg_dst_png[MAX_PATH];
                snprintf(bg_dst_png, sizeof(bg_dst_png), "%s\\%s.png", target_zuma_dir, canvas_name);
                if (image_export_to_file(&cimg, bg_dst_png, IMG_EXPORT_PNG, false, 0, 0)) {
                    snprintf(bg_status, sizeof(bg_status), "%s.png (Converted to PNG)", canvas_name);
                }
                image_free(&cimg);
            }
            char bg_dst_orig[MAX_PATH];
            snprintf(bg_dst_orig, sizeof(bg_dst_orig), "%s\\%s", target_zuma_dir, bg_src_filename);
            CopyFileA(bg_src_full, bg_dst_orig, FALSE);
        }
    }

    /* Copy Overlays (Cutouts, BackgroundAlphas, Sprites) */
    int overlays_copied = 0;
    char search_pat[MAX_PATH];
    snprintf(search_pat, sizeof(search_pat), "%s\\*.*", src_canvas_dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pat, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                if (_stricmp(fd.cFileName, "settings.xml") == 0) continue;
                if (strstr(fd.cFileName, ".zupath") != NULL) continue;
                if (strstr(fd.cFileName, ".dat") != NULL) continue;
                if (bg_src_filename[0] && _stricmp(fd.cFileName, bg_src_filename) == 0) continue;

                char fext[16] = {0};
                image_get_extension(fd.cFileName, fext, sizeof(fext));
                if (_stricmp(fext, "png") == 0 || _stricmp(fext, "gif") == 0 ||
                    _stricmp(fext, "jpg") == 0 || _stricmp(fext, "jpeg") == 0 ||
                    _stricmp(fext, "webp") == 0 || _stricmp(fext, "bmp") == 0) {

                    char src_f[MAX_PATH], dst_f[MAX_PATH];
                    snprintf(src_f, sizeof(src_f), "%s\\%s", src_canvas_dir, fd.cFileName);
                    snprintf(dst_f, sizeof(dst_f), "%s\\%s", target_zuma_dir, fd.cFileName);
                    if (CopyFileA(src_f, dst_f, FALSE)) {
                        overlays_copied++;
                    }

                    if (_stricmp(fext, "webp") == 0 || _stricmp(fext, "bmp") == 0) {
                        CanvasImage oimg;
                        memset(&oimg, 0, sizeof(CanvasImage));
                        if (image_load_from_file(src_f, &oimg)) {
                            char base_no_ext[MAX_PATH];
                            strncpy(base_no_ext, fd.cFileName, sizeof(base_no_ext) - 1);
                            char *dot = strrchr(base_no_ext, '.');
                            if (dot) *dot = '\0';
                            char dst_png[MAX_PATH];
                            snprintf(dst_png, sizeof(dst_png), "%s\\%s.png", target_zuma_dir, base_no_ext);
                            image_export_to_file(&oimg, dst_png, IMG_EXPORT_PNG, false, 0, 0);
                            image_free(&oimg);
                        }
                    }
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    /* Prune any stale files in target_zuma_dir that do not exist in src_canvas_dir */
    if (is_replace) {
        char tgt_pat[MAX_PATH];
        snprintf(tgt_pat, sizeof(tgt_pat), "%s\\*.*", target_zuma_dir);
        WIN32_FIND_DATAA tfd;
        HANDLE htFind = FindFirstFileA(tgt_pat, &tfd);
        if (htFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(tfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    char src_check[MAX_PATH];
                    snprintf(src_check, sizeof(src_check), "%s\\%s", src_canvas_dir, tfd.cFileName);
                    bool is_active_dat = false;
                    if (strstr(tfd.cFileName, ".dat") != NULL) {
                        for (int p = 1; p <= curve_count; ++p) {
                            char expected_dat[MAX_PATH];
                            if (curve_count == 1) {
                                snprintf(expected_dat, sizeof(expected_dat), "%s.dat", canvas_name);
                            } else {
                                snprintf(expected_dat, sizeof(expected_dat), "%s-%d.dat", canvas_name, p);
                            }
                            if (_stricmp(tfd.cFileName, expected_dat) == 0) {
                                is_active_dat = true;
                                break;
                            }
                        }
                    }
                    bool is_active_bg = false;
                    if (bg_src_filename[0]) {
                        char bg_name_only[MAX_PATH];
                        char ext[16] = {0};
                        image_get_extension(bg_src_filename, ext, sizeof(ext));
                        snprintf(bg_name_only, sizeof(bg_name_only), "%s.%s", canvas_name, ext);
                        if (_stricmp(tfd.cFileName, bg_name_only) == 0 ||
                            _stricmp(tfd.cFileName, bg_src_filename) == 0) {
                            is_active_bg = true;
                        }
                    }
                    if (!is_active_dat && !is_active_bg && GetFileAttributesA(src_check) == INVALID_FILE_ATTRIBUTES) {
                        char to_del[MAX_PATH];
                        snprintf(to_del, sizeof(to_del), "%s\\%s", target_zuma_dir, tfd.cFileName);
                        DeleteFileA(to_del);
                    }
                }
            } while (FindNextFileA(htFind, &tfd));
            FindClose(htFind);
        }
    }

    free(cv_xml);

    /* 12. Refresh Zuma Deluxe Map List in Modal and Select Exported Map */
    populate_manage_zuma_levels(parent_hwnd);

    if (g_manage_dlg_data && g_manage_dlg_data->hwnd_list_zuma) {
        for (int i = 0; i < g_manage_dlg_data->zuma_level_count; ++i) {
            if (_stricmp(g_manage_dlg_data->zuma_levels[i].id, canvas_name) == 0) {
                SendMessage(g_manage_dlg_data->hwnd_list_zuma, LB_SETCURSEL, i, 0);
                g_manage_dlg_data->selected_zuma_idx = i;
                break;
            }
        }
    }

    /* 13. Present Formatted Completion Summary */
    char body[1024];
    snprintf(body, sizeof(body),
        "Canvas project '%s' was successfully exported to Zuma Deluxe!\n\n"
        "Target Directory:\n %s\\%s\n\n"
        "Export Summary:\n"
        "- Graphics: %s in levels.xml\n"
        "- Level Progression: %s (%s)\n"
        "- Curve Tracks: %d track(s) exported (.dat)%s\n"
        "- Background Image: %s\n"
        "- Overlay Assets: %d file(s) copied\n"
        "- XML Backup: levels.xml.bak created\n\n"
        "The map is now ready to play in Zuma Deluxe!",
        canvas_name,
        g_zuma_levels_dir, canvas_name,
        is_replace ? "Updated" : "Added",
        prog_id, level_difficulty_get_display_name(diff),
        curves_exported, (curves_failed > 0) ? " (some failed!)" : "",
        bg_status,
        overlays_copied);

    if (g_manage_dlg_data && g_manage_dlg_data->hwnd_dlg) {
        populate_manage_zuma_levels(g_manage_dlg_data->hwnd_dlg);
    }

    show_themed_notice_dialog(parent_hwnd, "Export to Maps - ZuPather", "Export Successful", body, 580, 420);
    return true;
}

/* ========================================================================= */
/* Manage Levels Context Menus, Rename Sub-Modals & List Subclasses         */
/* ========================================================================= */

static void notify_manage_levels_canvas_renamed(const char *new_name) {
    if (g_manage_dlg_data && g_manage_dlg_data->hwnd_dlg) {
        populate_manage_canvas_list(g_manage_dlg_data->hwnd_dlg);
        if (g_manage_dlg_data->hwnd_list_canvases && new_name && new_name[0]) {
            int found_idx = (int)SendMessageA(g_manage_dlg_data->hwnd_list_canvases, LB_FINDSTRINGEXACT, -1, (LPARAM)new_name);
            if (found_idx != LB_ERR) {
                SendMessage(g_manage_dlg_data->hwnd_list_canvases, LB_SETCURSEL, found_idx, 0);
                g_manage_dlg_data->selected_canvas_idx = found_idx;
            }
        }
    }
}

static bool rename_zuma_map_in_xml(const char *old_id, const char *new_id) {
    if (!g_has_zuma_dir || g_zuma_levels_dir[0] == '\0' || !old_id || !new_id) return false;

    char xml_path[MAX_PATH];
    snprintf(xml_path, sizeof(xml_path), "%s\\levels.xml", g_zuma_levels_dir);

    char bak_path[MAX_PATH];
    snprintf(bak_path, sizeof(bak_path), "%s\\levels.xml.bak", g_zuma_levels_dir);
    CopyFileA(xml_path, bak_path, FALSE);

    FILE *f = fopen(xml_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) { fclose(f); return false; }

    DynBuffer dyn;
    dyn_init(&dyn, sz + 8192);
    fread(dyn.data, 1, sz, f);
    fclose(f);
    dyn.data[sz] = '\0';
    dyn.len = sz;

    char old_id_attr[128], new_id_attr[128];
    snprintf(old_id_attr, sizeof(old_id_attr), "id=\"%s\"", old_id);
    snprintf(new_id_attr, sizeof(new_id_attr), "id=\"%s\"", new_id);

    char old_gfx_attr[128], new_gfx_attr[128];
    snprintf(old_gfx_attr, sizeof(old_gfx_attr), "graphics=\"%s\"", old_id);
    snprintf(new_gfx_attr, sizeof(new_gfx_attr), "graphics=\"%s\"", new_id);

    /* 1. In <Graphics ...>, update id="..." */
    const char *p = dyn.data;
    while ((p = strstr(p, "<Graphics")) != NULL) {
        const char *tag_end = strchr(p, '>');
        if (tag_end) {
            const char *attr_pos = strstr(p, old_id_attr);
            if (attr_pos && attr_pos < tag_end) {
                size_t off = attr_pos - dyn.data;
                dyn_replace_range(&dyn, off, off + strlen(old_id_attr), new_id_attr);
                p = dyn.data + off + strlen(new_id_attr);
                continue;
            }
        }
        p += 9;
    }

    /* 2. In <Level ...>, update id="..." and graphics="..." */
    p = dyn.data;
    while ((p = strstr(p, "<Level")) != NULL) {
        char next_c = p[6];
        if (next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c == '\n') {
            const char *tag_end = strchr(p, '>');
            if (tag_end) {
                const char *attr_pos = strstr(p, old_id_attr);
                if (attr_pos && attr_pos < tag_end) {
                    size_t off = attr_pos - dyn.data;
                    dyn_replace_range(&dyn, off, off + strlen(old_id_attr), new_id_attr);
                    p = dyn.data + off + strlen(new_id_attr);
                    tag_end = strchr(p, '>');
                }
                if (tag_end) {
                    const char *gfx_pos = strstr(p, old_gfx_attr);
                    if (gfx_pos && gfx_pos < tag_end) {
                        size_t off = gfx_pos - dyn.data;
                        dyn_replace_range(&dyn, off, off + strlen(old_gfx_attr), new_gfx_attr);
                        p = dyn.data + off + strlen(new_gfx_attr);
                        continue;
                    }
                }
            }
        }
        p += 6;
    }

    /* 3. In <Settings ...>, update id="..." */
    p = dyn.data;
    while ((p = strstr(p, "<Settings")) != NULL) {
        const char *tag_end = strchr(p, '>');
        if (tag_end) {
            const char *attr_pos = strstr(p, old_id_attr);
            if (attr_pos && attr_pos < tag_end) {
                size_t off = attr_pos - dyn.data;
                dyn_replace_range(&dyn, off, off + strlen(old_id_attr), new_id_attr);
                p = dyn.data + off + strlen(new_id_attr);
                continue;
            }
        }
        p += 9;
    }

    /* Write updated levels.xml safely */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s\\levels.xml.tmp", g_zuma_levels_dir);
    FILE *fw = fopen(tmp_path, "wb");
    if (!fw) {
        dyn_free(&dyn);
        return false;
    }
    fwrite(dyn.data, 1, dyn.len, fw);
    fclose(fw);
    dyn_free(&dyn);

    if (!MoveFileExA(tmp_path, xml_path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(xml_path);
        MoveFileA(tmp_path, xml_path);
    }
    return true;
}

static bool delete_zuma_map_from_xml(const char *level_id) {
    if (!g_has_zuma_dir || g_zuma_levels_dir[0] == '\0' || !level_id || !level_id[0]) return false;

    char xml_path[MAX_PATH];
    snprintf(xml_path, sizeof(xml_path), "%s\\levels.xml", g_zuma_levels_dir);

    char bak_path[MAX_PATH];
    snprintf(bak_path, sizeof(bak_path), "%s\\levels.xml.bak", g_zuma_levels_dir);
    CopyFileA(xml_path, bak_path, FALSE);

    FILE *f = fopen(xml_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) { fclose(f); return false; }

    DynBuffer dyn;
    dyn_init(&dyn, sz + 1024);
    fread(dyn.data, 1, sz, f);
    fclose(f);
    dyn.data[sz] = '\0';
    dyn.len = sz;

    char id_attr[128], gfx_attr[128];
    snprintf(id_attr, sizeof(id_attr), "id=\"%s\"", level_id);
    snprintf(gfx_attr, sizeof(gfx_attr), "graphics=\"%s\"", level_id);

    /* 1. Remove <Graphics ... id="level_id" ...> ... </Graphics> */
    const char *p = dyn.data;
    while ((p = strstr(p, "<Graphics")) != NULL) {
        const char *tag_end = strchr(p, '>');
        if (tag_end) {
            const char *attr_pos = strstr(p, id_attr);
            if (attr_pos && attr_pos < tag_end) {
                const char *block_start = p;
                while (block_start > dyn.data && (*(block_start - 1) == ' ' || *(block_start - 1) == '\t')) block_start--;
                if (block_start > dyn.data && *(block_start - 1) == '\n') {
                    block_start--;
                    if (block_start > dyn.data && *(block_start - 1) == '\r') block_start--;
                }

                const char *block_end = NULL;
                if (*(tag_end - 1) == '/') {
                    block_end = tag_end + 1;
                } else {
                    const char *close_tag = strstr(tag_end, "</Graphics>");
                    if (close_tag) {
                        block_end = close_tag + strlen("</Graphics>");
                    } else {
                        block_end = tag_end + 1;
                    }
                }
                while (*block_end == '\r' || *block_end == '\n') block_end++;

                size_t s_off = block_start - dyn.data;
                size_t e_off = block_end - dyn.data;
                dyn_replace_range(&dyn, s_off, e_off, "");
                p = dyn.data + s_off;
                continue;
            }
        }
        p += 9;
    }

    /* 2. Remove <Level ... id="level_id" ...> or graphics="level_id" */
    p = dyn.data;
    while ((p = strstr(p, "<Level")) != NULL) {
        char next_c = p[6];
        if (next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c == '\n') {
            const char *tag_end = strchr(p, '>');
            if (tag_end) {
                const char *id_pos = strstr(p, id_attr);
                const char *gfx_pos = strstr(p, gfx_attr);
                if ((id_pos && id_pos < tag_end) || (gfx_pos && gfx_pos < tag_end)) {
                    const char *block_start = p;
                    while (block_start > dyn.data && (*(block_start - 1) == ' ' || *(block_start - 1) == '\t')) block_start--;
                    if (block_start > dyn.data && *(block_start - 1) == '\n') {
                        block_start--;
                        if (block_start > dyn.data && *(block_start - 1) == '\r') block_start--;
                    }

                    const char *block_end = NULL;
                    if (*(tag_end - 1) == '/') {
                        block_end = tag_end + 1;
                    } else {
                        const char *close_tag = strstr(tag_end, "</Level>");
                        if (close_tag) {
                            block_end = close_tag + strlen("</Level>");
                        } else {
                            block_end = tag_end + 1;
                        }
                    }
                    while (*block_end == '\r' || *block_end == '\n') block_end++;

                    size_t s_off = block_start - dyn.data;
                    size_t e_off = block_end - dyn.data;
                    dyn_replace_range(&dyn, s_off, e_off, "");
                    p = dyn.data + s_off;
                    continue;
                }
            }
        }
        p += 6;
    }

    /* Write updated levels.xml safely */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s\\levels.xml.tmp", g_zuma_levels_dir);
    FILE *fw = fopen(tmp_path, "wb");
    if (!fw) {
        dyn_free(&dyn);
        return false;
    }
    fwrite(dyn.data, 1, dyn.len, fw);
    fclose(fw);
    dyn_free(&dyn);

    if (!MoveFileExA(tmp_path, xml_path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(xml_path);
        MoveFileA(tmp_path, xml_path);
    }
    return true;
}

static void handle_delete_zuma_map(HWND parent_hwnd, const char *level_id) {
    if (!g_has_zuma_dir || g_zuma_levels_dir[0] == '\0' || !level_id || !level_id[0]) return;

    /* Guard: original map is 100% protected from deletion */
    char xml_path_chk[MAX_PATH];
    snprintf(xml_path_chk, sizeof(xml_path_chk), "%s\\levels.xml", g_zuma_levels_dir);
    FILE *fx_chk = fopen(xml_path_chk, "rb");
    if (fx_chk) {
        fseek(fx_chk, 0, SEEK_END);
        long sz = ftell(fx_chk);
        fseek(fx_chk, 0, SEEK_SET);
        if (sz > 0 && sz < 16*1024*1024) {
            char *buf = (char *)malloc(sz + 1);
            if (buf) {
                fread(buf, 1, sz, fx_chk);
                buf[sz] = '\0';
                if (is_id_in_stage_progression(buf, level_id)) {
                    free(buf);
                    fclose(fx_chk);
                    show_original_map_protected_dialog(parent_hwnd, level_id, NULL);
                    return;
                }
                free(buf);
            }
        }
        fclose(fx_chk);
    }

    char header[128];
    snprintf(header, sizeof(header), "Delete Map '%s' from Zuma Deluxe?", level_id);
    char body[600];
    snprintf(body, sizeof(body),
        "Are you sure you want to delete Zuma Deluxe map '%s'?\n\n"
        "This action will:\n"
        "- Delete folder: %s\\%s\n"
        "- Remove associated <Graphics> and <Level> definitions from levels.xml\n\n"
        "A backup file (levels.xml.bak) will be created automatically.\n\n"
        "This action cannot be undone.",
        level_id, g_zuma_levels_dir, level_id);

    if (!show_themed_confirm_dialog(parent_hwnd, "Confirm Zuma Map Deletion - ZuPather", header, body, true)) {
        return;
    }

    /* 1. Clean from levels.xml */
    delete_zuma_map_from_xml(level_id);

    /* 2. Delete folder levels\<level_id> */
    char folder_path[MAX_PATH];
    snprintf(folder_path, sizeof(folder_path), "%s\\%s", g_zuma_levels_dir, level_id);

    char double_null_path[MAX_PATH + 2];
    memset(double_null_path, 0, sizeof(double_null_path));
    strncpy(double_null_path, folder_path, MAX_PATH);

    SHFILEOPSTRUCTA sfop;
    memset(&sfop, 0, sizeof(sfop));
    sfop.hwnd = parent_hwnd;
    sfop.wFunc = FO_DELETE;
    sfop.pFrom = double_null_path;
    sfop.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationA(&sfop);

    /* 3. Refresh Zuma maps list */
    populate_manage_zuma_levels(parent_hwnd);

    char succ_msg[512];
    snprintf(succ_msg, sizeof(succ_msg),
        "Zuma map '%s' was successfully deleted from Zuma Deluxe.\n\n"
        "The level folder and corresponding XML nodes have been cleaned up.",
        level_id);
    show_themed_notice_dialog(parent_hwnd, "Map Deleted - ZuPather", "Deletion Successful", succ_msg, 500, 260);
}

typedef struct {
    HWND hwnd_dlg;
    HWND hwnd_parent;
    HWND hwnd_edit;
    HWND hwnd_btn_ok;
    HWND hwnd_btn_cancel;
    char old_id[64];
    bool is_done;
} RenameZumaMapDlgData;

static LRESULT CALLBACK RenameZumaMapModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();
    RenameZumaMapDlgData *data = (RenameZumaMapDlgData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTA *cs = (CREATESTRUCTA*)lParam;
            data = (RenameZumaMapDlgData*)cs->lpCreateParams;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)data);
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);

            HINSTANCE hinst = GetModuleHandle(NULL);
            data->hwnd_edit = CreateWindowExA(
                WS_EX_CLIENTEDGE, "EDIT", data->old_id,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                24, 116, 412, 26, hwnd, (HMENU)ID_RENAME_ZUMA_EDIT_ID, hinst, NULL
            );
            SendMessage(data->hwnd_edit, WM_SETFONT, (WPARAM)(g_font_mono_bold ? g_font_mono_bold : g_font_primary_regular), TRUE);
            SendMessage(data->hwnd_edit, EM_SETSEL, 0, -1);
            g_orig_canvas_name_proc = (WNDPROC)SetWindowLongPtrA(
                data->hwnd_edit, GWLP_WNDPROC, (LONG_PTR)CanvasNameEditSubclassProc
            );

            data->hwnd_btn_ok = CreateWindowA(
                "BUTTON", "Rename Map", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                236, 208, 115, 32, hwnd, (HMENU)ID_RENAME_ZUMA_BTN_OK, hinst, NULL
            );

            data->hwnd_btn_cancel = CreateWindowA(
                "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                361, 208, 75, 32, hwnd, (HMENU)ID_RENAME_ZUMA_BTN_CANCEL, hinst, NULL
            );
            SetFocus(data->hwnd_edit);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH bg_br = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc, bg_br);
            DeleteObject(bg_br);

            HPEN pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(hdc, pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom);
            SelectObject(hdc, old_pen);
            DeleteObject(pen);

            SetBkMode(hdc, TRANSPARENT);

            /* Title & Subtitle */
            SelectObject(hdc, g_font_primary_bold);
            SetTextColor(hdc, th->text_primary);
            RECT rc_t = {24, 16, rc.right - 24, 38};
            DrawTextA(hdc, "Rename Zuma Deluxe Map", -1, &rc_t, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

            SelectObject(hdc, g_font_primary_regular);
            SetTextColor(hdc, th->text_secondary);
            RECT rc_sub = {24, 40, rc.right - 24, 58};
            DrawTextA(hdc, "Rename map ID in levels.xml and level folder in Zuma Deluxe", -1, &rc_sub, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

            /* Current ID label & value */
            SelectObject(hdc, g_font_primary_bold);
            SetTextColor(hdc, th->text_dim);
            RECT rc_l1 = {24, 66, 130, 84};
            DrawTextA(hdc, "Current Map ID:", -1, &rc_l1, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

            SelectObject(hdc, g_font_mono_bold ? g_font_mono_bold : g_font_primary_bold);
            SetTextColor(hdc, (g_current_theme == THEME_DARK) ? RGB(255, 175, 55) : RGB(215, 110, 10));
            RECT rc_v1 = {136, 66, rc.right - 24, 84};
            DrawTextA(hdc, data ? data->old_id : "", -1, &rc_v1, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

            /* New ID label */
            SelectObject(hdc, g_font_primary_bold);
            SetTextColor(hdc, th->text_primary);
            RECT rc_l2 = {24, 94, rc.right - 24, 112};
            DrawTextA(hdc, "New Map ID (lowercase [a-z]):", -1, &rc_l2, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

            /* Note */
            SelectObject(hdc, g_font_primary_regular);
            SetTextColor(hdc, th->text_dim);
            RECT rc_note = {24, 150, rc.right - 24, 196};
            DrawTextA(hdc, "Note: Only the folder name and ID in levels.xml will be updated.\nAll background graphics, curve tracks, and assets remain unchanged.", -1, &rc_note, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc_ctl = (HDC)wParam;
            COLORREF edit_bg = (g_current_theme == THEME_DARK) ? RGB(36, 40, 48) : RGB(255, 255, 255);
            SetTextColor(hdc_ctl, th->text_primary);
            SetBkColor(hdc_ctl, edit_bg);
            static HBRUSH s_edit_br_light = NULL;
            static HBRUSH s_edit_br_dark = NULL;
            if (!s_edit_br_light) s_edit_br_light = CreateSolidBrush(RGB(255, 255, 255));
            if (!s_edit_br_dark)  s_edit_br_dark  = CreateSolidBrush(RGB(36, 40, 48));
            return (LRESULT)((g_current_theme == THEME_DARK) ? s_edit_br_dark : s_edit_br_light);
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool is_ok = (dis->CtlID == ID_RENAME_ZUMA_BTN_OK);

                COLORREF bg_color = is_ok ? (is_pressed ? RGB(18, 70, 140) : th->text_accent_blue)
                                          : (is_pressed ? th->btn_bg_pressed : th->btn_bg);
                HBRUSH bg_brush = CreateSolidBrush(bg_color);
                FillRect(dis->hDC, &dis->rcItem, bg_brush);
                DeleteObject(bg_brush);

                COLORREF border_col = is_ok ? th->text_accent_blue : th->btn_border;
                HPEN pen = CreatePen(PS_SOLID, 1, border_col);
                HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, old_pen);
                DeleteObject(pen);

                char text[64] = {0};
                GetWindowTextA(dis->hwndItem, text, sizeof(text));

                SetBkMode(dis->hDC, TRANSPARENT);
                COLORREF text_col = is_ok ? RGB(255, 255, 255) : th->btn_text;
                SetTextColor(dis->hDC, text_col);
                SelectObject(dis->hDC, is_ok ? g_font_primary_bold : g_font_primary_regular);

                RECT rc_t = dis->rcItem;
                if (is_pressed) { rc_t.left += 1; rc_t.top += 1; }
                DrawTextA(dis->hDC, text, -1, &rc_t, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (!data) break;

            if (cmd == ID_RENAME_ZUMA_BTN_OK) {
                char raw_id[128] = {0};
                GetWindowTextA(data->hwnd_edit, raw_id, sizeof(raw_id));

                char *s = raw_id;
                while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
                char *e = s + strlen(s);
                while (e > s && (*(e - 1) == ' ' || *(e - 1) == '\t' || *(e - 1) == '\r' || *(e - 1) == '\n')) e--;
                *e = '\0';

                if (*s == '\0') {
                    show_themed_notice_dialog(hwnd, "Invalid ID", "Map ID Cannot Be Empty",
                        "Please enter a new ID for this Zuma Deluxe map.", 460, 240);
                    SetFocus(data->hwnd_edit);
                    return 0;
                }

                if (!is_valid_zuma_level_id(s)) {
                    show_themed_notice_dialog(hwnd, "Invalid Zuma ID", "Invalid Zuma ID Format",
                        "Map ID must start with a lowercase letter [a-z], maximum 32 characters, and contain only lowercase letters [a-z], digits [0-9], underscores (_), or hyphens (-).", 540, 260);
                    SetFocus(data->hwnd_edit);
                    return 0;
                }

                if (_stricmp(s, data->old_id) == 0) {
                    data->is_done = true;
                    DestroyWindow(hwnd);
                    return 0;
                }

                /* Check folder existence */
                char target_folder[MAX_PATH];
                snprintf(target_folder, sizeof(target_folder), "%s\\%s", g_zuma_levels_dir, s);
                if (GetFileAttributesA(target_folder) != INVALID_FILE_ATTRIBUTES) {
                    show_themed_notice_dialog(hwnd, "Folder Exists", "Level Folder Already Exists",
                        "A level folder with that name already exists in your Zuma Deluxe directory. Please choose a different ID.", 520, 240);
                    SetFocus(data->hwnd_edit);
                    return 0;
                }

                /* Check in levels.xml */
                char xml_path[MAX_PATH];
                snprintf(xml_path, sizeof(xml_path), "%s\\levels.xml", g_zuma_levels_dir);
                FILE *fx = fopen(xml_path, "rb");
                if (fx) {
                    fseek(fx, 0, SEEK_END);
                    long sz = ftell(fx);
                    fseek(fx, 0, SEEK_SET);
                    if (sz > 0 && sz < 16*1024*1024) {
                        char *buf = (char *)malloc(sz + 1);
                        if (buf) {
                            fread(buf, 1, sz, fx);
                            buf[sz] = '\0';
                            if (is_id_in_stage_progression(buf, s)) {
                                free(buf);
                                fclose(fx);
                                MessageBeep(MB_ICONWARNING);
                                show_edit_warning_popup(
                                    data->hwnd_edit,
                                    "Original Zuma Deluxe Map Protected",
                                    "Target ID is an official original Zuma Deluxe map registered in StageProgression.",
                                    "Renaming any level to match an official Zuma Deluxe map ID",
                                    "Custom unique identifiers not reserved by Zuma Deluxe core stages",
                                    "Vanilla Zuma Deluxe maps cannot be altered by ZuPather to prevent crashes and broken game stages."
                                );
                                SetFocus(data->hwnd_edit);
                                return 0;
                            }
                            if (find_zuma_graphics_node(buf, s, NULL, 0)) {
                                free(buf);
                                fclose(fx);
                                show_themed_notice_dialog(hwnd, "ID Conflicted", "Map ID Already in Use",
                                    "That ID is already registered in levels.xml. Please choose a different ID.", 520, 240);
                                SetFocus(data->hwnd_edit);
                                return 0;
                            }
                            free(buf);
                        }
                    }
                    fclose(fx);
                }

                /* Perform Folder Rename */
                char old_folder[MAX_PATH];
                snprintf(old_folder, sizeof(old_folder), "%s\\%s", g_zuma_levels_dir, data->old_id);
                if (!MoveFileA(old_folder, target_folder)) {
                    show_themed_notice_dialog(hwnd, "Rename Failed", "Failed to Rename Folder",
                        "Failed to rename the Zuma Deluxe level folder. Make sure files are not in use by another program.", 540, 240);
                    return 0;
                }

                /* Update levels.xml */
                rename_zuma_map_in_xml(data->old_id, s);

                /* Refresh parent list */
                HWND parent_dlg = data->hwnd_parent;
                populate_manage_zuma_levels(parent_dlg);
                if (g_manage_dlg_data && g_manage_dlg_data->hwnd_list_zuma) {
                    int found_idx = (int)SendMessageA(g_manage_dlg_data->hwnd_list_zuma, LB_FINDSTRINGEXACT, -1, (LPARAM)s);
                    if (found_idx != LB_ERR) {
                        SendMessage(g_manage_dlg_data->hwnd_list_zuma, LB_SETCURSEL, found_idx, 0);
                        g_manage_dlg_data->selected_zuma_idx = found_idx;
                    }
                }

                char succ_msg[512];
                snprintf(succ_msg, sizeof(succ_msg),
                    "Zuma map '%s' was successfully renamed to '%s'!\n\n"
                    "The folder name and ID in levels.xml have been updated. All curve tracks and graphics assets remain untouched.",
                    data->old_id, s);

                data->is_done = true;
                DestroyWindow(hwnd);
                show_themed_notice_dialog(parent_dlg, "Rename Successful - ZuPather", "Map Rename Successful", succ_msg, 540, 260);
                return 0;
            } else if (cmd == ID_RENAME_ZUMA_BTN_CANCEL) {
                hide_edit_warning_popup();
                data->is_done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            hide_edit_warning_popup();
            if (data) data->is_done = true;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            hide_edit_warning_popup();
            if (data) data->is_done = true;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void show_rename_zuma_map_dialog(HWND parent_hwnd, const char *old_id) {
    if (!old_id || old_id[0] == '\0') return;

    /* Guard: original map is 100% protected from renaming */
    char xml_path_chk[MAX_PATH];
    snprintf(xml_path_chk, sizeof(xml_path_chk), "%s\\levels.xml", g_zuma_levels_dir);
    FILE *fx_chk = fopen(xml_path_chk, "rb");
    if (fx_chk) {
        fseek(fx_chk, 0, SEEK_END);
        long sz = ftell(fx_chk);
        fseek(fx_chk, 0, SEEK_SET);
        if (sz > 0 && sz < 16*1024*1024) {
            char *buf = (char *)malloc(sz + 1);
            if (buf) {
                fread(buf, 1, sz, fx_chk);
                buf[sz] = '\0';
                if (is_id_in_stage_progression(buf, old_id)) {
                    free(buf);
                    fclose(fx_chk);
                    show_original_map_protected_dialog(parent_hwnd, old_id, NULL);
                    return;
                }
                free(buf);
            }
        }
        fclose(fx_chk);
    }

    HINSTANCE hinst = GetModuleHandle(NULL);
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = RenameZumaMapModalProc;
        wc.hInstance     = hinst;
        wc.lpszClassName = "ZuPatherRenameZumaModal";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        s_registered = true;
    }

    RECT rc_dlg = { 0, 0, 460, 256 };
    AdjustWindowRectEx(&rc_dlg, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int dlg_w = rc_dlg.right - rc_dlg.left;
    int dlg_h = rc_dlg.bottom - rc_dlg.top;

    RECT rc_p;
    GetWindowRect(parent_hwnd, &rc_p);
    int cx = rc_p.left + ((rc_p.right - rc_p.left) - dlg_w) / 2;
    int cy = rc_p.top + ((rc_p.bottom - rc_p.top) - dlg_h) / 2;

    RenameZumaMapDlgData data;
    memset(&data, 0, sizeof(data));
    data.hwnd_parent = parent_hwnd;
    strncpy(data.old_id, old_id, sizeof(data.old_id) - 1);

    HWND prev_modal = g_active_modal_dlg;
    BOOL was_enabled = IsWindowEnabled(parent_hwnd);
    if (was_enabled) EnableWindow(parent_hwnd, FALSE);

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherRenameZumaModal", "Rename Zuma Deluxe Map",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, &data
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;

    MSG msg;
    while (!data.is_done && IsWindow(hwnd_dlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (data.is_done) break;
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            data.is_done = true;
            DestroyWindow(hwnd_dlg);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_active_modal_dlg = prev_modal;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        if (was_enabled) EnableWindow(parent_hwnd, TRUE);
        SetForegroundWindow(parent_hwnd);
        SetFocus(parent_hwnd);
    }
}

typedef struct {
    HWND hwnd_dlg;
    HWND hwnd_parent;
    HWND hwnd_edit;
    HWND hwnd_btn_ok;
    HWND hwnd_btn_cancel;
    char old_canvas_name[64];
    bool is_done;
} RenameCanvasExportDlgData;

static LRESULT CALLBACK RenameCanvasExportModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();
    RenameCanvasExportDlgData *data = (RenameCanvasExportDlgData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTA *cs = (CREATESTRUCTA*)lParam;
            data = (RenameCanvasExportDlgData*)cs->lpCreateParams;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)data);
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);

            char def_name[64];
            snprintf(def_name, sizeof(def_name), "%s_custom", data->old_canvas_name);

            HINSTANCE hinst = GetModuleHandle(NULL);
            data->hwnd_edit = CreateWindowExA(
                WS_EX_CLIENTEDGE, "EDIT", def_name,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                24, 116, 432, 26, hwnd, (HMENU)ID_RENAME_EXP_EDIT_NAME, hinst, NULL
            );
            SendMessage(data->hwnd_edit, WM_SETFONT, (WPARAM)(g_font_mono_bold ? g_font_mono_bold : g_font_primary_regular), TRUE);
            SendMessage(data->hwnd_edit, EM_SETSEL, 0, -1);
            g_orig_canvas_name_proc = (WNDPROC)SetWindowLongPtrA(
                data->hwnd_edit, GWLP_WNDPROC, (LONG_PTR)CanvasNameEditSubclassProc
            );

            data->hwnd_btn_ok = CreateWindowA(
                "BUTTON", "Rename & Export", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                246, 208, 130, 32, hwnd, (HMENU)ID_RENAME_EXP_BTN_OK, hinst, NULL
            );

            data->hwnd_btn_cancel = CreateWindowA(
                "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                386, 208, 70, 32, hwnd, (HMENU)ID_RENAME_EXP_BTN_CANCEL, hinst, NULL
            );
            SetFocus(data->hwnd_edit);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH bg_br = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc, bg_br);
            DeleteObject(bg_br);

            HPEN pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(hdc, pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom);
            SelectObject(hdc, old_pen);
            DeleteObject(pen);

            SetBkMode(hdc, TRANSPARENT);

            /* Title & Subtitle */
            SelectObject(hdc, g_font_primary_bold);
            SetTextColor(hdc, th->text_primary);
            RECT rc_t = {24, 16, rc.right - 24, 38};
            DrawTextA(hdc, "Rename Canvas Project & Export", -1, &rc_t, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

            SelectObject(hdc, g_font_primary_regular);
            SetTextColor(hdc, (g_current_theme == THEME_DARK) ? RGB(245, 175, 45) : RGB(195, 105, 0));
            RECT rc_sub = {24, 40, rc.right - 24, 60};
            char sub_txt[256];
            snprintf(sub_txt, sizeof(sub_txt), "'%s' is an official Zuma Deluxe map name and is protected.", data ? data->old_canvas_name : "");
            DrawTextA(hdc, sub_txt, -1, &rc_sub, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

            /* Label */
            SelectObject(hdc, g_font_primary_bold);
            SetTextColor(hdc, th->text_primary);
            RECT rc_lbl = {24, 94, rc.right - 24, 112};
            DrawTextA(hdc, "New Canvas Name (lowercase [a-z]):", -1, &rc_lbl, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

            /* Note */
            SelectObject(hdc, g_font_primary_regular);
            SetTextColor(hdc, th->text_dim);
            RECT rc_note = {24, 150, rc.right - 24, 196};
            DrawTextA(hdc, "The canvas project folder in ./canvas/ will be renamed first,\nthen ZuPather will automatically export this project to Zuma Deluxe.", -1, &rc_note, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc_ctl = (HDC)wParam;
            COLORREF edit_bg = (g_current_theme == THEME_DARK) ? RGB(36, 40, 48) : RGB(255, 255, 255);
            SetTextColor(hdc_ctl, th->text_primary);
            SetBkColor(hdc_ctl, edit_bg);
            static HBRUSH s_edit_br_light = NULL;
            static HBRUSH s_edit_br_dark = NULL;
            if (!s_edit_br_light) s_edit_br_light = CreateSolidBrush(RGB(255, 255, 255));
            if (!s_edit_br_dark)  s_edit_br_dark  = CreateSolidBrush(RGB(36, 40, 48));
            return (LRESULT)((g_current_theme == THEME_DARK) ? s_edit_br_dark : s_edit_br_light);
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool is_ok = (dis->CtlID == ID_RENAME_EXP_BTN_OK);

                COLORREF bg_color = is_ok ? (is_pressed ? RGB(20, 105, 55) : RGB(35, 140, 75))
                                          : (is_pressed ? th->btn_bg_pressed : th->btn_bg);
                HBRUSH bg_brush = CreateSolidBrush(bg_color);
                FillRect(dis->hDC, &dis->rcItem, bg_brush);
                DeleteObject(bg_brush);

                COLORREF border_col = is_ok ? RGB(35, 140, 75) : th->btn_border;
                HPEN pen = CreatePen(PS_SOLID, 1, border_col);
                HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, old_pen);
                DeleteObject(pen);

                char text[64] = {0};
                GetWindowTextA(dis->hwndItem, text, sizeof(text));

                SetBkMode(dis->hDC, TRANSPARENT);
                COLORREF text_col = is_ok ? RGB(255, 255, 255) : th->btn_text;
                SetTextColor(dis->hDC, text_col);
                SelectObject(dis->hDC, is_ok ? g_font_primary_bold : g_font_primary_regular);

                RECT rc_t = dis->rcItem;
                if (is_pressed) { rc_t.left += 1; rc_t.top += 1; }
                DrawTextA(dis->hDC, text, -1, &rc_t, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (!data) break;

            if (cmd == ID_RENAME_EXP_BTN_OK) {
                char raw_name[128] = {0};
                GetWindowTextA(data->hwnd_edit, raw_name, sizeof(raw_name));

                char *s = raw_name;
                while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
                char *e = s + strlen(s);
                while (e > s && (*(e - 1) == ' ' || *(e - 1) == '\t' || *(e - 1) == '\r' || *(e - 1) == '\n')) e--;
                *e = '\0';

                if (*s == '\0') {
                    show_themed_notice_dialog(hwnd, "Invalid Name", "Canvas Name Cannot Be Empty",
                        "Please enter a new name for this canvas project.", 460, 240);
                    SetFocus(data->hwnd_edit);
                    return 0;
                }

                if (!is_valid_zuma_level_id(s)) {
                    show_themed_notice_dialog(hwnd, "Invalid Zuma ID", "Invalid Canvas Name Format",
                        "Canvas name must start with a lowercase letter [a-z], maximum 32 characters, and contain only lowercase letters [a-z], digits [0-9], underscores (_), or hyphens (-).", 540, 260);
                    SetFocus(data->hwnd_edit);
                    return 0;
                }

                /* Check if it's an original Zuma map */
                char zuma_xml[MAX_PATH];
                snprintf(zuma_xml, sizeof(zuma_xml), "%s\\levels.xml", g_zuma_levels_dir);
                FILE *fx = fopen(zuma_xml, "rb");
                if (fx) {
                    fseek(fx, 0, SEEK_END);
                    long sz = ftell(fx);
                    fseek(fx, 0, SEEK_SET);
                    if (sz > 0 && sz < 16*1024*1024) {
                        char *buf = (char *)malloc(sz + 1);
                        if (buf) {
                            fread(buf, 1, sz, fx);
                            buf[sz] = '\0';
                            if (is_id_in_stage_progression(buf, s)) {
                                free(buf);
                                fclose(fx);
                                MessageBeep(MB_ICONWARNING);
                                show_edit_warning_popup(
                                    data->hwnd_edit,
                                    "Original Zuma Deluxe Map Protected",
                                    "Target ID is an official original Zuma Deluxe map registered in StageProgression.",
                                    "Exporting with an official Zuma Deluxe map ID",
                                    "Custom unique identifiers not reserved by Zuma Deluxe core stages",
                                    "Vanilla Zuma Deluxe maps cannot be altered by ZuPather to prevent crashes and broken game stages."
                                );
                                SetFocus(data->hwnd_edit);
                                return 0;
                            }
                            free(buf);
                        }
                    }
                    fclose(fx);
                }

                /* Check if folder canvas/<new_name> already exists */
                char base_canvas_dir[MAX_PATH];
                get_canvas_base_directory(base_canvas_dir, sizeof(base_canvas_dir));

                char new_canvas_folder[MAX_PATH];
                snprintf(new_canvas_folder, sizeof(new_canvas_folder), "%s\\%s", base_canvas_dir, s);
                if (GetFileAttributesA(new_canvas_folder) != INVALID_FILE_ATTRIBUTES) {
                    show_themed_notice_dialog(hwnd, "Canvas Exists", "Canvas Folder Already Exists",
                        "A canvas folder with that name already exists in your ./canvas/ directory. Please choose a different name.", 520, 240);
                    SetFocus(data->hwnd_edit);
                    return 0;
                }

                /* Perform Canvas Rename on disk */
                char old_canvas_folder[MAX_PATH];
                snprintf(old_canvas_folder, sizeof(old_canvas_folder), "%s\\%s", base_canvas_dir, data->old_canvas_name);

                if (!MoveFileA(old_canvas_folder, new_canvas_folder)) {
                    show_themed_notice_dialog(hwnd, "Rename Failed", "Failed to Rename Canvas",
                        "Failed to rename canvas folder. Make sure files are not in use by another program.", 500, 240);
                    return 0;
                }

                /* Rename internal files */
                char search_spec[MAX_PATH];
                snprintf(search_spec, sizeof(search_spec), "%s\\*.*", new_canvas_folder);
                WIN32_FIND_DATAA fd;
                HANDLE hFind = FindFirstFileA(search_spec, &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    size_t old_len = strlen(data->old_canvas_name);
                    do {
                        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            if (strncmp(fd.cFileName, data->old_canvas_name, old_len) == 0) {
                                char next_ch = fd.cFileName[old_len];
                                if (next_ch == '.' || next_ch == '-') {
                                    const char *suffix = fd.cFileName + old_len;
                                    char new_fn[MAX_PATH], old_fp[MAX_PATH], new_fp[MAX_PATH];
                                    snprintf(new_fn, sizeof(new_fn), "%s%s", s, suffix);
                                    snprintf(old_fp, sizeof(old_fp), "%s\\%s", new_canvas_folder, fd.cFileName);
                                    snprintf(new_fp, sizeof(new_fp), "%s\\%s", new_canvas_folder, new_fn);
                                    MoveFileA(old_fp, new_fp);
                                }
                            }
                        }
                    } while (FindNextFileA(hFind, &fd));
                    FindClose(hFind);
                }

                /* Update settings.xml */
                char set_xml_path[MAX_PATH];
                snprintf(set_xml_path, sizeof(set_xml_path), "%s\\settings.xml", new_canvas_folder);
                FILE *f_st = fopen(set_xml_path, "rb");
                if (f_st) {
                    fseek(f_st, 0, SEEK_END);
                    long s_sz = ftell(f_st);
                    fseek(f_st, 0, SEEK_SET);
                    if (s_sz > 0 && s_sz < 4*1024*1024) {
                        char *s_buf = (char *)malloc(s_sz + 1);
                        if (s_buf) {
                            fread(s_buf, 1, s_sz, f_st);
                            s_buf[s_sz] = '\0';
                            fclose(f_st);
                            f_st = NULL;

                            char *proc_xml = replace_string_all(s_buf, data->old_canvas_name, s);
                            free(s_buf);
                            if (proc_xml) {
                                FILE *fw = fopen(set_xml_path, "wb");
                                if (fw) {
                                    fwrite(proc_xml, 1, strlen(proc_xml), fw);
                                    fclose(fw);
                                }
                                free(proc_xml);
                            }
                        }
                    }
                    if (f_st) fclose(f_st);
                }

                /* If active canvas is loaded in workspace, update in-memory state */
                if (g_has_canvas && (_stricmp(g_canvas_name, data->old_canvas_name) == 0 || _stricmp(g_canvas_folder, old_canvas_folder) == 0)) {
                    strncpy(g_canvas_name, s, sizeof(g_canvas_name) - 1);
                    strncpy(g_canvas_folder, new_canvas_folder, sizeof(g_canvas_folder) - 1);
                    char win_title[256];
                    snprintf(win_title, sizeof(win_title), "%s - ZuPather [Zuma Path Editor]", s);
                    SetWindowTextA(g_hwnd_main, win_title);
                    update_status_bar();
                    InvalidateRect(g_hwnd_main, NULL, FALSE);
                }

                /* Refresh canvas list in Map Manager */
                HWND parent_dlg = data->hwnd_parent;
                populate_manage_canvas_list(parent_dlg);
                if (g_manage_dlg_data && g_manage_dlg_data->hwnd_list_canvases) {
                    int found_idx = (int)SendMessageA(g_manage_dlg_data->hwnd_list_canvases, LB_FINDSTRINGEXACT, -1, (LPARAM)s);
                    if (found_idx != LB_ERR) {
                        SendMessage(g_manage_dlg_data->hwnd_list_canvases, LB_SETCURSEL, found_idx, 0);
                        g_manage_dlg_data->selected_canvas_idx = found_idx;
                    }
                }

                char newly_named_canvas[64];
                strncpy(newly_named_canvas, s, sizeof(newly_named_canvas) - 1);
                newly_named_canvas[sizeof(newly_named_canvas) - 1] = '\0';

                data->is_done = true;
                DestroyWindow(hwnd);

                /* Now automatically proceed to export under the new name! */
                export_canvas_to_zuma_map(parent_dlg, newly_named_canvas);
                return 0;
            } else if (cmd == ID_RENAME_EXP_BTN_CANCEL) {
                hide_edit_warning_popup();
                data->is_done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            hide_edit_warning_popup();
            if (data) data->is_done = true;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            hide_edit_warning_popup();
            if (data) data->is_done = true;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void show_rename_canvas_and_export_dialog(HWND parent_hwnd, const char *canvas_name) {
    if (!canvas_name || canvas_name[0] == '\0') return;

    HINSTANCE hinst = GetModuleHandle(NULL);
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = RenameCanvasExportModalProc;
        wc.hInstance     = hinst;
        wc.lpszClassName = "ZuPatherRenameExportModal";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        s_registered = true;
    }

    RECT rc_dlg = { 0, 0, 480, 256 };
    AdjustWindowRectEx(&rc_dlg, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int dlg_w = rc_dlg.right - rc_dlg.left;
    int dlg_h = rc_dlg.bottom - rc_dlg.top;

    RECT rc_p;
    GetWindowRect(parent_hwnd, &rc_p);
    int cx = rc_p.left + ((rc_p.right - rc_p.left) - dlg_w) / 2;
    int cy = rc_p.top + ((rc_p.bottom - rc_p.top) - dlg_h) / 2;

    RenameCanvasExportDlgData data;
    memset(&data, 0, sizeof(data));
    data.hwnd_parent = parent_hwnd;
    strncpy(data.old_canvas_name, canvas_name, sizeof(data.old_canvas_name) - 1);

    HWND prev_modal = g_active_modal_dlg;
    BOOL was_enabled = IsWindowEnabled(parent_hwnd);
    if (was_enabled) EnableWindow(parent_hwnd, FALSE);

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherRenameExportModal", "Rename Canvas & Export",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, &data
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;

    if (data.hwnd_edit) {
        show_edit_warning_popup(
            data.hwnd_edit,
            "Original Zuma Deluxe Map Protected",
            "Target level is an official original Zuma Deluxe map registered in StageProgression.",
            "Overwriting official Zuma Deluxe maps directly",
            "A new unique custom ID for your canvas project",
            "Vanilla Zuma Deluxe maps cannot be altered by ZuPather. Enter a new custom name to export."
        );
    }

    MSG msg;
    while (!data.is_done && IsWindow(hwnd_dlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (data.is_done) break;
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            data.is_done = true;
            DestroyWindow(hwnd_dlg);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_active_modal_dlg = prev_modal;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        if (was_enabled) EnableWindow(parent_hwnd, TRUE);
        SetForegroundWindow(parent_hwnd);
        SetFocus(parent_hwnd);
    }
}

static void show_zuma_map_context_menu(HWND parent_hwnd, int idx, int x, int y) {
    if (!g_manage_dlg_data || idx < 0 || idx >= g_manage_dlg_data->zuma_level_count) return;
    ZumaLevelEntry *ze = &g_manage_dlg_data->zuma_levels[idx];

    if (ze->type == ZUMA_MAP_ORIGINAL) {
        show_original_map_protected_popup_at_point(parent_hwnd, x, y, ze->id, ze->dispname);
        return;
    }

    HMENU hmenu = CreatePopupMenu();
    AppendMenuA(hmenu, MF_STRING, ID_ZUMA_CTX_RENAME, "Rename Zuma Map...");
    AppendMenuA(hmenu, MF_STRING, ID_ZUMA_CTX_DELETE, "Delete Zuma Map...");

    SetForegroundWindow(parent_hwnd);
    int cmd = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, x, y, 0, parent_hwnd, NULL);
    DestroyMenu(hmenu);

    if (cmd == ID_ZUMA_CTX_RENAME) {
        show_rename_zuma_map_dialog(parent_hwnd, ze->id);
    } else if (cmd == ID_ZUMA_CTX_DELETE) {
        handle_delete_zuma_map(parent_hwnd, ze->id);
    }
}

static void show_canvas_list_context_menu(HWND parent_hwnd, int idx, int x, int y) {
    if (!g_manage_dlg_data || idx < 0 || idx >= g_manage_dlg_data->canvas_count) return;
    CanvasLevelEntry *ce = &g_manage_dlg_data->canvas_entries[idx];

    HMENU hmenu = CreatePopupMenu();
    AppendMenuA(hmenu, MF_STRING, ID_CANVAS_CTX_OPEN,   "Open Canvas");
    AppendMenuA(hmenu, MF_STRING, ID_CANVAS_CTX_RENAME, "Rename Canvas...");
    AppendMenuA(hmenu, MF_STRING, ID_CANVAS_CTX_DELETE, "Delete Canvas...");

    SetForegroundWindow(parent_hwnd);
    int cmd = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, x, y, 0, parent_hwnd, NULL);
    DestroyMenu(hmenu);

    char base_dir[MAX_PATH];
    get_canvas_base_directory(base_dir, sizeof(base_dir));
    char folder_path[MAX_PATH];
    snprintf(folder_path, sizeof(folder_path), "%s\\%s", base_dir, ce->name);

    if (cmd == ID_CANVAS_CTX_OPEN) {
        DestroyWindow(parent_hwnd);
        show_opening_canvas_modal(g_hwnd_main, folder_path);
    } else if (cmd == ID_CANVAS_CTX_RENAME) {
        show_rename_canvas_dialog(parent_hwnd, ce->name, ce->title, folder_path, base_dir);
    } else if (cmd == ID_CANVAS_CTX_DELETE) {
        char header[128];
        snprintf(header, sizeof(header), "Permanently Delete '%s'?", ce->name);
        char body[512];
        snprintf(body, sizeof(body),
                 "Are you sure you want to permanently delete canvas '%s'?\n\n"
                 "This will delete the entire folder and all its files:\n%s\n\n"
                 "This action cannot be undone.",
                 ce->name, folder_path);

        if (show_themed_confirm_dialog(parent_hwnd, "Confirm Canvas Deletion", header, body, true)) {
            bool is_active_canvas = (g_has_canvas &&
                (_stricmp(g_canvas_name, ce->name) == 0 ||
                 _stricmp(g_canvas_folder, folder_path) == 0));

            if (is_active_canvas) {
                for (int p = 0; p < MAX_PATHS; ++p) {
                    bezier_path_free(&g_paths[p]);
                }
                g_path_count = 0;
                g_active_path_idx = -1;
                g_active_node_idx = -1;
                if (g_has_bg_image) {
                    image_free(&g_canvas_img);
                    g_has_bg_image = false;
                }
                g_canvas_name[0] = '\0';
                g_canvas_folder[0] = '\0';
                g_has_canvas = false;
                g_canvas_w = DEFAULT_CANVAS_WIDTH;
                g_canvas_h = DEFAULT_CANVAS_HEIGHT;
                g_has_frog = false;
                g_frog_gx = DEFAULT_CANVAS_WIDTH / 2;
                g_frog_gy = DEFAULT_CANVAS_HEIGHT / 2;
                g_treasure_point_count = 0;
                for (int i = 0; i < g_cutout_count; ++i) free_cutout(&g_cutouts[i]);
                g_cutout_count = 0;
                for (int i = 0; i < g_sprite_count; ++i) free_sprite(&g_sprites[i]);
                g_sprite_count = 0;
                for (int i = 0; i < g_bg_alpha_count; ++i) free_bgalpha(&g_bg_alphas[i]);
                g_bg_alpha_count = 0;
                SetWindowTextA(g_hwnd_main, "ZuPather - Zuma Path Editor");
                update_status_bar();
                InvalidateRect(g_hwnd_main, NULL, FALSE);
            }

            char double_null_path[MAX_PATH + 2];
            memset(double_null_path, 0, sizeof(double_null_path));
            strncpy(double_null_path, folder_path, MAX_PATH);

            SHFILEOPSTRUCTA sfop;
            memset(&sfop, 0, sizeof(sfop));
            sfop.hwnd = parent_hwnd;
            sfop.wFunc = FO_DELETE;
            sfop.pFrom = double_null_path;
            sfop.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
            int res = SHFileOperationA(&sfop);

            if (res == 0 && !sfop.fAnyOperationsAborted) {
                populate_manage_canvas_list(parent_hwnd);
            } else {
                show_themed_notice_dialog(parent_hwnd, "Delete Failed", "Folder Deletion Error",
                    "Failed to delete canvas folder.\nMake sure the folder is not in use by another program.", 480, 250);
            }
        }
    }
}

static LRESULT CALLBACK ManageZumaListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!g_manage_dlg_data) return DefWindowProcA(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_MOUSEMOVE: {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            LRESULT item = CallWindowProcA(g_manage_dlg_data->orig_zuma_list_proc, hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
            if (HIWORD(item) == 0) {
                int idx = LOWORD(item);
                if (idx != g_manage_dlg_data->last_hover_zuma_idx && idx >= 0 && idx < g_manage_dlg_data->zuma_level_count) {
                    g_manage_dlg_data->last_hover_zuma_idx = idx;
                    ZumaLevelEntry *ze = &g_manage_dlg_data->zuma_levels[idx];
                    const char *desc = "";
                    if (ze->type == ZUMA_MAP_ORIGINAL) {
                        desc = "Original Zuma Deluxe Map (Protected)\n"
                               "Official game map registered in StageProgression levels.xml.\n"
                               "Protected: cannot be renamed, modified, or deleted.";
                    } else if (ze->type == ZUMA_MAP_MISC) {
                        desc = "Zuma Misc Map (Unregistered)\n"
                               "Contains <Graphics> definition in levels.xml, but not in official StageProgression.\n"
                               "Right-click to Rename ID or Delete this map.";
                    } else if (ze->type == ZUMA_MAP_CUSTOM) {
                        desc = "ZuPather Custom Map\n"
                               "Custom level created and exported using ZuPather progression (zupath[n]-[diff]).\n"
                               "Right-click to Rename ID or Delete this map.";
                    }

                    if (g_manage_dlg_data->hwnd_tooltip) {
                        COLORREF tt_bg = (g_current_theme == THEME_DARK) ? RGB(26, 30, 38) : RGB(255, 255, 255);
                        COLORREF tt_fg = (g_current_theme == THEME_DARK) ? RGB(230, 235, 245) : RGB(30, 35, 45);
                        SendMessageA(g_manage_dlg_data->hwnd_tooltip, TTM_SETTIPBKCOLOR, (WPARAM)tt_bg, 0);
                        SendMessageA(g_manage_dlg_data->hwnd_tooltip, TTM_SETTIPTEXTCOLOR, (WPARAM)tt_fg, 0);

                        TOOLINFOA ti;
                        memset(&ti, 0, sizeof(ti));
                        ti.cbSize = sizeof(ti);
                        ti.hwnd = hwnd;
                        ti.uId = (UINT_PTR)hwnd;
                        ti.lpszText = (LPSTR)desc;
                        SendMessageA(g_manage_dlg_data->hwnd_tooltip, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
                    }
                }
            }
            TRACKMOUSEEVENT tme;
            memset(&tme, 0, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            break;
        }

        case WM_LBUTTONDOWN:
            hide_edit_warning_popup();
            break;

        case WM_MOUSELEAVE:
            if (g_manage_dlg_data) {
                g_manage_dlg_data->last_hover_zuma_idx = -1;
            }
            break;

        case WM_RBUTTONDOWN: {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            LRESULT item = CallWindowProcA(g_manage_dlg_data->orig_zuma_list_proc, hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
            if (HIWORD(item) == 0) {
                int idx = LOWORD(item);
                if (idx >= 0 && idx < g_manage_dlg_data->zuma_level_count) {
                    SendMessage(hwnd, LB_SETCURSEL, idx, 0);
                    g_manage_dlg_data->selected_zuma_idx = idx;
                }
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            LRESULT item = CallWindowProcA(g_manage_dlg_data->orig_zuma_list_proc, hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
            if (HIWORD(item) == 0) {
                int idx = LOWORD(item);
                if (idx >= 0 && idx < g_manage_dlg_data->zuma_level_count) {
                    SendMessage(hwnd, LB_SETCURSEL, idx, 0);
                    g_manage_dlg_data->selected_zuma_idx = idx;
                    POINT screen_pt = pt;
                    ClientToScreen(hwnd, &screen_pt);
                    show_zuma_map_context_menu(GetParent(hwnd), idx, screen_pt.x, screen_pt.y);
                }
            }
            return 0;
        }
    }
    return CallWindowProcA(g_manage_dlg_data->orig_zuma_list_proc ? g_manage_dlg_data->orig_zuma_list_proc : DefWindowProcA,
                          hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK ManageCanvasListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!g_manage_dlg_data) return DefWindowProcA(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_LBUTTONDOWN:
            hide_edit_warning_popup();
            break;

        case WM_RBUTTONDOWN: {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            LRESULT item = CallWindowProcA(g_manage_dlg_data->orig_canvas_list_proc, hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
            if (HIWORD(item) == 0) {
                int idx = LOWORD(item);
                if (idx >= 0 && idx < g_manage_dlg_data->canvas_count) {
                    SendMessage(hwnd, LB_SETCURSEL, idx, 0);
                    g_manage_dlg_data->selected_canvas_idx = idx;
                }
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            LRESULT item = CallWindowProcA(g_manage_dlg_data->orig_canvas_list_proc, hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
            if (HIWORD(item) == 0) {
                int idx = LOWORD(item);
                if (idx >= 0 && idx < g_manage_dlg_data->canvas_count) {
                    SendMessage(hwnd, LB_SETCURSEL, idx, 0);
                    g_manage_dlg_data->selected_canvas_idx = idx;
                    POINT screen_pt = pt;
                    ClientToScreen(hwnd, &screen_pt);
                    show_canvas_list_context_menu(GetParent(hwnd), idx, screen_pt.x, screen_pt.y);
                }
            }
            return 0;
        }
    }
    return CallWindowProcA(g_manage_dlg_data->orig_canvas_list_proc ? g_manage_dlg_data->orig_canvas_list_proc : DefWindowProcA,
                          hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK ManageLevelsModalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();

    switch (msg) {
        case WM_CREATE:
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);
            return 0;

        case WM_LBUTTONDOWN:
            hide_edit_warning_popup();
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_THEMECHANGED:
            if (g_manage_dlg_data) {
                apply_control_dark_mode(g_manage_dlg_data->hwnd_list_zuma, g_current_theme == THEME_DARK);
                apply_control_dark_mode(g_manage_dlg_data->hwnd_list_canvases, g_current_theme == THEME_DARK);
                if (g_manage_dlg_data->hwnd_tooltip) {
                    COLORREF tt_bg = (g_current_theme == THEME_DARK) ? RGB(26, 30, 38) : RGB(255, 255, 255);
                    COLORREF tt_fg = (g_current_theme == THEME_DARK) ? RGB(230, 235, 245) : RGB(30, 35, 45);
                    SendMessageA(g_manage_dlg_data->hwnd_tooltip, TTM_SETTIPBKCOLOR, (WPARAM)tt_bg, 0);
                    SendMessageA(g_manage_dlg_data->hwnd_tooltip, TTM_SETTIPTEXTCOLOR, (WPARAM)tt_fg, 0);
                }
            }
            InvalidateRect(hwnd, NULL, TRUE);
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            /* Main background */
            HBRUSH bg_brush = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc, bg_brush);
            DeleteObject(bg_brush);

            /* Outer window border */
            HPEN border_pen = CreatePen(PS_SOLID, 1, th->bar_border);
            HPEN old_pen = (HPEN)SelectObject(hdc, border_pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom);

            SetBkMode(hdc, TRANSPARENT);

            /* 1. Header Title & Subtitle */
            SetTextColor(hdc, th->text_primary);
            SelectObject(hdc, g_font_primary_bold);
            RECT rc_title = {24, 16, rc.right - 24, 40};
            DrawTextA(hdc, "Map Manager", -1, &rc_title, DT_SINGLELINE | DT_LEFT | DT_TOP);

            SetTextColor(hdc, th->text_secondary);
            SelectObject(hdc, g_font_primary_regular);
            RECT rc_sub = {24, 42, rc.right - 24, 62};
            DrawTextA(hdc, "Bridge Zuma Deluxe maps with ZuPather canvas projects (Import & Export)", -1, &rc_sub, DT_SINGLELINE | DT_LEFT | DT_TOP);

            /* 2. Top Card: Zuma Folder Configuration */
            RECT rc_folder_card = {24, 66, rc.right - 24, 142};
            COLORREF card_bg_col = (g_current_theme == THEME_DARK) ? RGB(30, 34, 42) : RGB(246, 248, 251);
            HBRUSH card_br = CreateSolidBrush(card_bg_col);
            FillRect(hdc, &rc_folder_card, card_br);
            DeleteObject(card_br);

            HPEN card_pen = CreatePen(PS_SOLID, 1, th->bar_border);
            SelectObject(hdc, card_pen);
            Rectangle(hdc, rc_folder_card.left, rc_folder_card.top, rc_folder_card.right, rc_folder_card.bottom);
            DeleteObject(card_pen);

            /* Card Content: Left side text */
            SelectObject(hdc, g_font_primary_bold);
            SetTextColor(hdc, th->text_primary);
            RECT rc_lbl_cfg = {rc_folder_card.left + 16, rc_folder_card.top + 10, rc_folder_card.right - 180, rc_folder_card.top + 28};
            DrawTextA(hdc, "Zuma Deluxe Folder:", -1, &rc_lbl_cfg, DT_SINGLELINE | DT_LEFT | DT_TOP);

            if (!g_has_zuma_dir || g_zuma_levels_dir[0] == '\0') {
                COLORREF warn_col = (g_current_theme == THEME_DARK) ? RGB(245, 175, 45) : RGB(195, 105, 0);
                SetTextColor(hdc, warn_col);
                SelectObject(hdc, g_font_primary_bold);
                RECT rc_status = {rc_folder_card.left + 16, rc_folder_card.top + 30, rc_folder_card.right - 180, rc_folder_card.top + 50};
                DrawTextA(hdc, "[!] Zuma folder is not set.", -1, &rc_status, DT_SINGLELINE | DT_LEFT | DT_TOP);

                SetTextColor(hdc, th->text_dim);
                SelectObject(hdc, g_font_primary_regular);
                RECT rc_hint = {rc_folder_card.left + 16, rc_folder_card.top + 50, rc_folder_card.right - 180, rc_folder_card.top + 70};
                DrawTextA(hdc, "Click 'Set Zuma Folder' to link your Zuma game directory and enable import/export.", -1, &rc_hint, DT_SINGLELINE | DT_LEFT | DT_TOP);
            } else {
                COLORREF ok_col = (g_current_theme == THEME_DARK) ? RGB(80, 215, 130) : RGB(20, 145, 60);
                SetTextColor(hdc, ok_col);
                SelectObject(hdc, g_font_primary_bold);
                RECT rc_status = {rc_folder_card.left + 16, rc_folder_card.top + 30, rc_folder_card.right - 180, rc_folder_card.top + 50};
                char buf_ok[512];
                snprintf(buf_ok, sizeof(buf_ok), "Linked: %s", g_zuma_levels_dir);
                DrawTextA(hdc, buf_ok, -1, &rc_status, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_END_ELLIPSIS);

                SetTextColor(hdc, th->text_secondary);
                SelectObject(hdc, g_font_primary_regular);
                RECT rc_hint = {rc_folder_card.left + 16, rc_folder_card.top + 50, rc_folder_card.right - 180, rc_folder_card.top + 70};
                char buf_cnt[128];
                snprintf(buf_cnt, sizeof(buf_cnt), "Verified levels.xml (%d map definitions active)", g_manage_dlg_data ? g_manage_dlg_data->zuma_level_count : 0);
                DrawTextA(hdc, buf_cnt, -1, &rc_hint, DT_SINGLELINE | DT_LEFT | DT_TOP);
            }

            /* 3. Column Headers */
            int col_w = 346;
            int left_x = 24;
            int right_x = rc.right - 24 - col_w;

            SelectObject(hdc, g_font_primary_bold);
            SetTextColor(hdc, th->text_primary);
            RECT rc_hdr_left = {left_x, 152, left_x + col_w, 172};
            char hdr_left_txt[128];
            if (g_has_zuma_dir && g_manage_dlg_data && g_manage_dlg_data->zuma_level_count > 0) {
                snprintf(hdr_left_txt, sizeof(hdr_left_txt), "Zuma Deluxe Maps (%d)", g_manage_dlg_data->zuma_level_count);
            } else {
                snprintf(hdr_left_txt, sizeof(hdr_left_txt), "Zuma Deluxe Maps");
            }
            DrawTextA(hdc, hdr_left_txt, -1, &rc_hdr_left, DT_SINGLELINE | DT_LEFT | DT_TOP);

            RECT rc_hdr_right = {right_x, 152, right_x + col_w, 172};
            char hdr_right_txt[128];
            snprintf(hdr_right_txt, sizeof(hdr_right_txt), "Canvas Projects (%d in ./canvas/)", g_manage_dlg_data ? g_manage_dlg_data->canvas_count : 0);
            DrawTextA(hdc, hdr_right_txt, -1, &rc_hdr_right, DT_SINGLELINE | DT_LEFT | DT_TOP);

            /* 4. Left Box 50% Opacity Card when Zuma Folder NOT set */
            if (!g_has_zuma_dir || g_zuma_levels_dir[0] == '\0') {
                RECT rc_dimmed = {left_x, 176, left_x + col_w, 416};

                COLORREF dim_bg = (g_current_theme == THEME_DARK) ? RGB(22, 25, 30) : RGB(236, 239, 243);
                HBRUSH dim_br = CreateSolidBrush(dim_bg);
                FillRect(hdc, &rc_dimmed, dim_br);
                DeleteObject(dim_br);

                COLORREF dim_border = (g_current_theme == THEME_DARK) ? RGB(45, 50, 60) : RGB(210, 215, 222);
                HPEN dim_pen = CreatePen(PS_SOLID, 1, dim_border);
                SelectObject(hdc, dim_pen);
                Rectangle(hdc, rc_dimmed.left, rc_dimmed.top, rc_dimmed.right, rc_dimmed.bottom);
                DeleteObject(dim_pen);

                /* Centered warning message inside 50% dimmed box */
                COLORREF dim_warn = (g_current_theme == THEME_DARK) ? RGB(210, 150, 40) : RGB(175, 100, 10);
                SetTextColor(hdc, dim_warn);
                SelectObject(hdc, g_font_primary_bold);
                RECT rc_t_w1 = {rc_dimmed.left + 16, rc_dimmed.top + 72, rc_dimmed.right - 16, rc_dimmed.top + 94};
                DrawTextA(hdc, "[!] Zuma Folder Not Set", -1, &rc_t_w1, DT_SINGLELINE | DT_CENTER | DT_TOP);

                COLORREF dim_sub = (g_current_theme == THEME_DARK) ? RGB(120, 126, 138) : RGB(140, 145, 155);
                SetTextColor(hdc, dim_sub);
                SelectObject(hdc, g_font_primary_regular);
                RECT rc_t_w2 = {rc_dimmed.left + 24, rc_dimmed.top + 100, rc_dimmed.right - 24, rc_dimmed.bottom - 40};
                DrawTextA(hdc, "Maps list is empty because Zuma Deluxe folder has not been linked yet.\n\nPlease click 'Set Zuma Folder' above to choose your game directory.", -1, &rc_t_w2, DT_CENTER | DT_WORDBREAK);
            } else {
                /* Border around Left ListBox when active */
                HPEN box_pen = CreatePen(PS_SOLID, 1, th->bar_border);
                HPEN old_p = (HPEN)SelectObject(hdc, box_pen);
                SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, left_x - 1, 175, left_x + col_w + 1, 176 + 240 + 1);
                SelectObject(hdc, old_p);
                DeleteObject(box_pen);
            }

            /* Border around Right ListBox */
            {
                HPEN box_pen = CreatePen(PS_SOLID, 1, th->bar_border);
                HPEN old_p = (HPEN)SelectObject(hdc, box_pen);
                SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, right_x - 1, 175, right_x + col_w + 1, 176 + 240 + 1);
                SelectObject(hdc, old_p);
                DeleteObject(box_pen);
            }

            /* 5. Bottom Separator and Footer */
            RECT rc_sep = {24, 468, rc.right - 24, 469};
            HBRUSH sep_br = CreateSolidBrush(th->bar_border);
            FillRect(hdc, &rc_sep, sep_br);
            DeleteObject(sep_br);

            SetTextColor(hdc, th->text_dim);
            SelectObject(hdc, g_font_primary_regular);
            RECT rc_tip = {24, 480, rc.right - 130, 506};
            DrawTextA(hdc, "Select a Zuma map on the left to import, or select a canvas project on the right to export.", -1, &rc_tip, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

            SelectObject(hdc, old_pen);
            DeleteObject(border_pen);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MEASUREITEM: {
            LPMEASUREITEMSTRUCT lpmi = (LPMEASUREITEMSTRUCT)lParam;
            if (lpmi->CtlType == ODT_LISTBOX) {
                lpmi->itemHeight = 20;
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdc_lb = (HDC)wParam;
            SetTextColor(hdc_lb, th->text_primary);
            SetBkColor(hdc_lb, (g_current_theme == THEME_DARK) ? RGB(24, 27, 33) : RGB(255, 255, 255));
            static HBRUSH s_manage_lb_br_light = NULL;
            static HBRUSH s_manage_lb_br_dark = NULL;
            if (!s_manage_lb_br_light) s_manage_lb_br_light = CreateSolidBrush(RGB(255, 255, 255));
            if (!s_manage_lb_br_dark)  s_manage_lb_br_dark  = CreateSolidBrush(RGB(24, 27, 33));
            return (LRESULT)((g_current_theme == THEME_DARK) ? s_manage_lb_br_dark : s_manage_lb_br_light);
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_LISTBOX) {
                if (dis->itemID == (UINT)-1) return TRUE;

                bool is_sel = (dis->itemState & ODS_SELECTED) != 0;
                int cid = dis->CtlID;

                if (cid == ID_MANAGE_LIST_ZUMA_LEVELS && g_manage_dlg_data) {
                    if (dis->itemID < (UINT)g_manage_dlg_data->zuma_level_count) {
                        ZumaLevelEntry *ze = &g_manage_dlg_data->zuma_levels[dis->itemID];
                        const char *id_str = ze->id;
                        const char *title_str = ze->dispname;

                        /* 3 Distinct Zuma Map Conditions:
                         * 1. ZUMA_MAP_ORIGINAL = Putih / White
                         * 2. ZUMA_MAP_MISC     = Oranye / Orange
                         * 3. ZUMA_MAP_CUSTOM   = Hijau / Green
                         */
                        COLORREF type_col;
                        COLORREF title_col;
                        if (ze->type == ZUMA_MAP_ORIGINAL) {
                            type_col = is_sel ? ((g_current_theme == THEME_DARK) ? RGB(255, 255, 255) : RGB(10, 35, 75))
                                              : ((g_current_theme == THEME_DARK) ? RGB(245, 248, 252) : RGB(35, 40, 50));
                            title_col = is_sel ? ((g_current_theme == THEME_DARK) ? RGB(235, 242, 255) : RGB(20, 45, 80))
                                               : th->text_secondary;
                        } else if (ze->type == ZUMA_MAP_MISC) {
                            type_col = is_sel ? ((g_current_theme == THEME_DARK) ? RGB(255, 195, 90) : RGB(180, 85, 5))
                                              : ((g_current_theme == THEME_DARK) ? RGB(255, 168, 50) : RGB(215, 110, 10));
                            title_col = type_col;
                        } else { /* ZUMA_MAP_CUSTOM */
                            type_col = is_sel ? ((g_current_theme == THEME_DARK) ? RGB(100, 250, 155) : RGB(10, 130, 45))
                                              : ((g_current_theme == THEME_DARK) ? RGB(65, 215, 120) : RGB(20, 150, 60));
                            title_col = type_col;
                        }

                        COLORREF bg_color = is_sel ? ((g_current_theme == THEME_DARK) ? RGB(28, 70, 120) : RGB(220, 235, 252))
                                                   : ((g_current_theme == THEME_DARK) ? RGB(24, 27, 33) : RGB(255, 255, 255));
                        HBRUSH bg_br = CreateSolidBrush(bg_color);
                        FillRect(dis->hDC, &dis->rcItem, bg_br);
                        DeleteObject(bg_br);

                        /* Left edge accent bar matching map type color when selected */
                        if (is_sel) {
                            RECT rc_bar = dis->rcItem;
                            rc_bar.right = rc_bar.left + 3;
                            HBRUSH bar_br = CreateSolidBrush(type_col);
                            FillRect(dis->hDC, &rc_bar, bar_br);
                            DeleteObject(bar_br);
                        }

                        SetBkMode(dis->hDC, TRANSPARENT);

                        /* 1. Left Text: Map ID (in designated map type color) */
                        SelectObject(dis->hDC, g_font_mono_bold ? g_font_mono_bold : g_font_primary_bold);
                        SetTextColor(dis->hDC, type_col);
                        RECT rc_left = dis->rcItem;
                        rc_left.left += 10;
                        rc_left.right = dis->rcItem.left + 140;
                        DrawTextA(dis->hDC, id_str, -1, &rc_left, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

                        /* 2. Right Text: Display Name (colored matching map type) */
                        SelectObject(dis->hDC, g_font_primary_regular);
                        SetTextColor(dis->hDC, title_col);
                        RECT rc_mid = dis->rcItem;
                        rc_mid.left = dis->rcItem.left + 145;
                        rc_mid.right = dis->rcItem.right - 10;
                        DrawTextA(dis->hDC, title_str, -1, &rc_mid, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

                        return TRUE;
                    }
                } else if (cid == ID_MANAGE_LIST_CANVASES && g_manage_dlg_data) {
                    if (dis->itemID < (UINT)g_manage_dlg_data->canvas_count) {
                        const char *id_str = g_manage_dlg_data->canvas_entries[dis->itemID].name;
                        const char *title_str = g_manage_dlg_data->canvas_entries[dis->itemID].title;

                        COLORREF bg_color = is_sel ? ((g_current_theme == THEME_DARK) ? RGB(28, 70, 120) : RGB(220, 235, 252))
                                                   : ((g_current_theme == THEME_DARK) ? RGB(24, 27, 33) : RGB(255, 255, 255));
                        HBRUSH bg_br = CreateSolidBrush(bg_color);
                        FillRect(dis->hDC, &dis->rcItem, bg_br);
                        DeleteObject(bg_br);

                        if (is_sel) {
                            RECT rc_bar = dis->rcItem;
                            rc_bar.right = rc_bar.left + 3;
                            HBRUSH bar_br = CreateSolidBrush(th->text_accent_blue);
                            FillRect(dis->hDC, &rc_bar, bar_br);
                            DeleteObject(bar_br);
                        }

                        SetBkMode(dis->hDC, TRANSPARENT);

                        /* 1. Left Text: Canvas ID */
                        SelectObject(dis->hDC, g_font_mono_bold ? g_font_mono_bold : g_font_primary_bold);
                        COLORREF id_col = is_sel ? ((g_current_theme == THEME_DARK) ? RGB(255, 255, 255) : RGB(10, 40, 95))
                                                 : th->text_primary;
                        SetTextColor(dis->hDC, id_col);
                        RECT rc_left = dis->rcItem;
                        rc_left.left += 10;
                        rc_left.right = dis->rcItem.left + 140;
                        DrawTextA(dis->hDC, id_str, -1, &rc_left, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

                        /* 2. Right Text: Canvas Title */
                        SelectObject(dis->hDC, g_font_primary_regular);
                        COLORREF title_col = is_sel ? ((g_current_theme == THEME_DARK) ? RGB(180, 215, 255) : RGB(45, 95, 165))
                                                    : th->text_secondary;
                        SetTextColor(dis->hDC, title_col);
                        RECT rc_right = dis->rcItem;
                        rc_right.left = dis->rcItem.left + 145;
                        rc_right.right = dis->rcItem.right - 10;
                        DrawTextA(dis->hDC, title_str, -1, &rc_right, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);

                        return TRUE;
                    }
                }
            }

            if (dis->CtlType == ODT_BUTTON) {
                bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool is_disabled = (dis->itemState & ODS_DISABLED) != 0;
                int cid = dis->CtlID;

                COLORREF bg_color;
                COLORREF text_color;
                COLORREF border_color;
                HFONT font_to_use = g_font_primary_bold;

                if (cid == ID_MANAGE_BTN_SET_FOLDER) {
                    bg_color = is_pressed ? th->btn_bg_pressed : th->btn_bg;
                    text_color = th->btn_text;
                    border_color = th->text_accent_blue;
                } else if (cid == ID_MANAGE_BTN_IMPORT) {
                    if (is_disabled) {
                        bg_color = (g_current_theme == THEME_DARK) ? RGB(32, 36, 44) : RGB(230, 233, 238);
                        text_color = th->text_dim;
                        border_color = th->btn_border;
                    } else {
                        bg_color = is_pressed ? RGB(18, 70, 140) : th->text_accent_blue;
                        text_color = RGB(255, 255, 255);
                        border_color = th->text_accent_blue;
                    }
                } else if (cid == ID_MANAGE_BTN_EXPORT) {
                    if (is_disabled) {
                        bg_color = (g_current_theme == THEME_DARK) ? RGB(32, 36, 44) : RGB(230, 233, 238);
                        text_color = th->text_dim;
                        border_color = th->btn_border;
                    } else {
                        bg_color = is_pressed ? RGB(20, 105, 55) : RGB(35, 140, 75);
                        text_color = RGB(255, 255, 255);
                        border_color = RGB(35, 140, 75);
                    }
                } else {
                    /* Close button */
                    bg_color = is_pressed ? th->btn_bg_pressed : th->btn_bg;
                    text_color = th->btn_text;
                    border_color = th->btn_border;
                    font_to_use = g_font_primary_regular;
                }

                HBRUSH bg_brush = CreateSolidBrush(bg_color);
                FillRect(dis->hDC, &dis->rcItem, bg_brush);
                DeleteObject(bg_brush);

                HPEN pen = CreatePen(PS_SOLID, 1, border_color);
                HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, old_pen);
                DeleteObject(pen);

                char text[64] = {0};
                GetWindowTextA(dis->hwndItem, text, sizeof(text));

                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, text_color);
                SelectObject(dis->hDC, font_to_use);

                RECT rc_t = dis->rcItem;
                if (is_pressed) { rc_t.left += 1; rc_t.top += 1; }
                DrawTextA(dis->hDC, text, -1, &rc_t, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (!g_manage_dlg_data) break;

            if (cmd == ID_MANAGE_BTN_SET_FOLDER) {
                handle_set_zuma_folder(hwnd);
                populate_manage_zuma_levels(hwnd);
                populate_manage_canvas_list(hwnd);
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (cmd == ID_MANAGE_LIST_ZUMA_LEVELS) {
                if (HIWORD(wParam) == LBN_SELCHANGE) {
                    int sel = (int)SendMessage(g_manage_dlg_data->hwnd_list_zuma, LB_GETCURSEL, 0, 0);
                    g_manage_dlg_data->selected_zuma_idx = sel;
                } else if (HIWORD(wParam) == LBN_DBLCLK) {
                    int sel = (int)SendMessage(g_manage_dlg_data->hwnd_list_zuma, LB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < g_manage_dlg_data->zuma_level_count) {
                        show_zuma_level_attributes_dialog(hwnd, g_manage_dlg_data->zuma_levels[sel].id);
                    }
                }
            } else if (cmd == ID_MANAGE_LIST_CANVASES) {
                if (HIWORD(wParam) == LBN_SELCHANGE) {
                    int sel = (int)SendMessage(g_manage_dlg_data->hwnd_list_canvases, LB_GETCURSEL, 0, 0);
                    g_manage_dlg_data->selected_canvas_idx = sel;
                } else if (HIWORD(wParam) == LBN_DBLCLK) {
                    int sel = (int)SendMessage(g_manage_dlg_data->hwnd_list_canvases, LB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < g_manage_dlg_data->canvas_count) {
                        show_canvas_project_attributes_dialog(hwnd, g_manage_dlg_data->canvas_entries[sel].name);
                    }
                }
            } else if (cmd == ID_MANAGE_BTN_IMPORT) {
                if (!g_has_zuma_dir) {
                    show_themed_notice_dialog(hwnd, "Notice", "Zuma Folder Required", "Please configure the Zuma Deluxe folder first.", 460, 240);
                    break;
                }
                int sel = (int)SendMessage(g_manage_dlg_data->hwnd_list_zuma, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < g_manage_dlg_data->zuma_level_count) {
                    const char *lvl_id = g_manage_dlg_data->zuma_levels[sel].id;
                    import_zuma_level_to_canvas(hwnd, lvl_id);
                } else {
                    show_themed_notice_dialog(hwnd, "Notice", "No Map Selected", "Please select a map from the Zuma Deluxe list to import.", 460, 240);
                }
            } else if (cmd == ID_MANAGE_BTN_EXPORT) {
                if (!g_has_zuma_dir) {
                    show_themed_notice_dialog(hwnd, "Notice", "Zuma Folder Required", "Please configure the Zuma Deluxe folder first.", 460, 240);
                    break;
                }
                int sel = (int)SendMessage(g_manage_dlg_data->hwnd_list_canvases, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < g_manage_dlg_data->canvas_count) {
                    const char *cv_name = g_manage_dlg_data->canvas_entries[sel].name;
                    export_canvas_to_zuma_map(hwnd, cv_name);
                } else {
                    show_themed_notice_dialog(hwnd, "Notice", "No Canvas Selected", "Please select a canvas project from the list to export.", 460, 240);
                }
            } else if (cmd == ID_MANAGE_BTN_CLOSE) {
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_active_modal_dlg == hwnd) g_active_modal_dlg = NULL;
            if (g_manage_dlg_data) {
                if (g_manage_dlg_data->hwnd_tooltip && IsWindow(g_manage_dlg_data->hwnd_tooltip)) {
                    DestroyWindow(g_manage_dlg_data->hwnd_tooltip);
                    g_manage_dlg_data->hwnd_tooltip = NULL;
                }
                free(g_manage_dlg_data);
                g_manage_dlg_data = NULL;
            }
            EnableWindow(g_hwnd_main, TRUE);
            SetFocus(g_hwnd_main);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void show_manage_levels_dialog(HWND parent_hwnd) {
    if (g_manage_dlg_data) {
        SetFocus(g_manage_dlg_data->hwnd_dlg);
        return;
    }

    HINSTANCE hinst = GetModuleHandle(NULL);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = ManageLevelsModalProc;
    wc.hInstance     = hinst;
    wc.lpszClassName = "ZuPatherManageLevelsModal";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    int dlg_w = 760;
    int dlg_h = 550;

    RECT rc_parent;
    GetWindowRect(parent_hwnd, &rc_parent);
    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;

    g_manage_dlg_data = (ManageLevelsDlgData*)calloc(1, sizeof(ManageLevelsDlgData));

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "ZuPatherManageLevelsModal", "Map Manager",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, NULL
    );

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    g_manage_dlg_data->hwnd_dlg = hwnd_dlg;
    EnableWindow(parent_hwnd, FALSE);

    /* 1. Button: Set Zuma Folder */
    g_manage_dlg_data->hwnd_btn_set_folder = CreateWindowA(
        "BUTTON", "Set Zuma Folder...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        556, 86, 164, 34, hwnd_dlg, (HMENU)ID_MANAGE_BTN_SET_FOLDER, hinst, NULL
    );

    int col_w = 346;
    int left_x = 24;
    int right_x = 390;

    /* 2. Left ListBox: Zuma Maps (0 extended style, ownerdrawn, themed dark scrollbar) */
    g_manage_dlg_data->hwnd_list_zuma = CreateWindowExA(
        0, "LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
        left_x, 176, col_w, 240, hwnd_dlg, (HMENU)ID_MANAGE_LIST_ZUMA_LEVELS, hinst, NULL
    );
    SendMessage(g_manage_dlg_data->hwnd_list_zuma, LB_SETITEMHEIGHT, 0, 20);
    apply_control_dark_mode(g_manage_dlg_data->hwnd_list_zuma, g_current_theme == THEME_DARK);

    /* 3. Left Action Button: Import to Canvas */
    g_manage_dlg_data->hwnd_btn_import = CreateWindowA(
        "BUTTON", "Import to Canvas", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        left_x, 426, col_w, 32, hwnd_dlg, (HMENU)ID_MANAGE_BTN_IMPORT, hinst, NULL
    );

    /* 4. Right ListBox: Canvas Projects (0 extended style, ownerdrawn, themed dark scrollbar) */
    g_manage_dlg_data->hwnd_list_canvases = CreateWindowExA(
        0, "LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
        right_x, 176, col_w, 240, hwnd_dlg, (HMENU)ID_MANAGE_LIST_CANVASES, hinst, NULL
    );
    SendMessage(g_manage_dlg_data->hwnd_list_canvases, LB_SETITEMHEIGHT, 0, 20);
    apply_control_dark_mode(g_manage_dlg_data->hwnd_list_canvases, g_current_theme == THEME_DARK);

    /* 5. Right Action Button: Export to Maps */
    g_manage_dlg_data->hwnd_btn_export = CreateWindowA(
        "BUTTON", "Export to Maps", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        right_x, 426, col_w, 32, hwnd_dlg, (HMENU)ID_MANAGE_BTN_EXPORT, hinst, NULL
    );

    /* 6. Close Button */
    g_manage_dlg_data->hwnd_btn_close = CreateWindowA(
        "BUTTON", "Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        dlg_w - 24 - 116, 476, 96, 30, hwnd_dlg, (HMENU)ID_MANAGE_BTN_CLOSE, hinst, NULL
    );

    /* Subclass both listboxes for context menus & hover tooltips */
    g_manage_dlg_data->orig_zuma_list_proc = (WNDPROC)SetWindowLongPtrA(
        g_manage_dlg_data->hwnd_list_zuma, GWLP_WNDPROC, (LONG_PTR)ManageZumaListSubclassProc
    );

    g_manage_dlg_data->orig_canvas_list_proc = (WNDPROC)SetWindowLongPtrA(
        g_manage_dlg_data->hwnd_list_canvases, GWLP_WNDPROC, (LONG_PTR)ManageCanvasListSubclassProc
    );

    /* Create and configure tooltip for Zuma Deluxe Maps list hover */
    HWND hwnd_tt = CreateWindowExA(
        WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd_dlg, NULL, hinst, NULL
    );
    SetWindowPos(hwnd_tt, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowTheme(hwnd_tt, L"", L"");
    COLORREF tt_bg = (g_current_theme == THEME_DARK) ? RGB(26, 30, 38) : RGB(255, 255, 255);
    COLORREF tt_fg = (g_current_theme == THEME_DARK) ? RGB(230, 235, 245) : RGB(30, 35, 45);
    SendMessageA(hwnd_tt, TTM_SETTIPBKCOLOR, (WPARAM)tt_bg, 0);
    SendMessageA(hwnd_tt, TTM_SETTIPTEXTCOLOR, (WPARAM)tt_fg, 0);
    SendMessageA(hwnd_tt, TTM_SETMAXTIPWIDTH, 0, 460);
    SendMessageA(hwnd_tt, TTM_SETDELAYTIME, TTDT_INITIAL, 100);
    SendMessageA(hwnd_tt, TTM_SETDELAYTIME, TTDT_RESHOW, 50);
    g_manage_dlg_data->hwnd_tooltip = hwnd_tt;
    g_manage_dlg_data->last_hover_zuma_idx = -1;

    TOOLINFOA ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd = g_manage_dlg_data->hwnd_list_zuma;
    ti.uId = (UINT_PTR)g_manage_dlg_data->hwnd_list_zuma;
    ti.lpszText = "";
    SendMessageA(hwnd_tt, TTM_ADDTOOLA, 0, (LPARAM)&ti);

    populate_manage_zuma_levels(hwnd_dlg);
    populate_manage_canvas_list(hwnd_dlg);

    SetFocus(g_has_zuma_dir ? g_manage_dlg_data->hwnd_list_zuma : g_manage_dlg_data->hwnd_btn_set_folder);
}

/* ============================================================================
 * Splash Screen Modal Dialog Implementation
 * ============================================================================ */
static enum {
    SPLASH_ACT_NONE = 0,
    SPLASH_ACT_NEW,
    SPLASH_ACT_OPEN,
    SPLASH_ACT_MAP
} s_splash_chosen_action = SPLASH_ACT_NONE;

static bool s_splash_dlg_done = false;

static LRESULT CALLBACK SplashModalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HDC mem_dc = CreateCompatibleDC(hdc);
            HBITMAP mem_bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP old_bm = (HBITMAP)SelectObject(mem_dc, mem_bm);

            const ThemeColors *th = get_theme();
            bool is_dark = (g_current_theme == THEME_DARK);

            /* Fill adaptive background */
            COLORREF bg_col = is_dark ? RGB(26, 29, 35) : RGB(242, 244, 248);
            HBRUSH bg_br = CreateSolidBrush(bg_col);
            FillRect(mem_dc, &rc, bg_br);
            DeleteObject(bg_br);

            /* Clean adaptive outer border */
            HPEN pen_bdr = CreatePen(PS_SOLID, 1, is_dark ? RGB(56, 62, 74) : RGB(180, 185, 195));
            HPEN old_pen = (HPEN)SelectObject(mem_dc, pen_bdr);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, 0, 0, rc.right, rc.bottom);
            SelectObject(mem_dc, old_pen);
            DeleteObject(pen_bdr);

            /* Render splash banner, github link, 3 action cards, and footer */
            render_splash_elements(mem_dc, rc.right, rc.bottom, th, 0, false);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem_dc, 0, 0, SRCCOPY);
            SelectObject(mem_dc, old_bm);
            DeleteObject(mem_bm);
            DeleteDC(mem_dc);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            int old_hover = g_splash_hover_item;
            int new_hover = 0;
            if (PtInRect(&g_splash_rc_github, pt)) new_hover = 1;
            else if (PtInRect(&g_splash_rc_new, pt)) new_hover = 2;
            else if (PtInRect(&g_splash_rc_open, pt)) new_hover = 3;
            else if (PtInRect(&g_splash_rc_map, pt)) new_hover = 4;

            if (new_hover != old_hover) {
                g_splash_hover_item = new_hover;
                InvalidateRect(hwnd, NULL, FALSE);
            }

            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (g_splash_hover_item != 0) {
                g_splash_hover_item = 0;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                if (g_splash_hover_item > 0) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            if (PtInRect(&g_splash_rc_github, pt)) {
                ShellExecuteA(NULL, "open", "https://github.com/rizkybayuu/ZuPather", NULL, NULL, SW_SHOWNORMAL);
                DestroyWindow(hwnd);
                return 0;
            } else if (PtInRect(&g_splash_rc_new, pt)) {
                s_splash_chosen_action = SPLASH_ACT_NEW;
                DestroyWindow(hwnd);
                return 0;
            } else if (PtInRect(&g_splash_rc_open, pt)) {
                s_splash_chosen_action = SPLASH_ACT_OPEN;
                DestroyWindow(hwnd);
                return 0;
            } else if (PtInRect(&g_splash_rc_map, pt)) {
                s_splash_chosen_action = SPLASH_ACT_MAP;
                DestroyWindow(hwnd);
                return 0;
            } else {
                /* User clicked anywhere on background or image -> dismiss immediately! */
                DestroyWindow(hwnd);
                return 0;
            }
        }

        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            /* Any mouse button click dismisses immediately */
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_KEYDOWN: {
            /* Any key press dismisses immediately */
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (wParam == 'N' || wParam == 'n') {
                    s_splash_chosen_action = SPLASH_ACT_NEW;
                } else if (wParam == 'O' || wParam == 'o') {
                    s_splash_chosen_action = SPLASH_ACT_OPEN;
                } else if (wParam == 'M' || wParam == 'm') {
                    s_splash_chosen_action = SPLASH_ACT_MAP;
                }
            }
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            s_splash_dlg_done = true;
            g_splash_hover_item = 0;
            PostMessageA(hwnd, WM_NULL, 0, 0);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void show_splash_screen_dialog(HWND parent_hwnd) {
    HINSTANCE hinst = GetModuleHandle(NULL);
    static bool s_class_registered = false;
    if (!s_class_registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.style         = CS_DROPSHADOW | CS_DBLCLKS;
        wc.lpfnWndProc   = SplashModalWndProc;
        wc.hInstance     = hinst;
        wc.lpszClassName = "ZuPatherSplashModal";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        s_class_registered = true;
    }

    int dlg_w = 620;
    int dlg_h = 466;

    RECT rc_parent = {0};
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        GetWindowRect(parent_hwnd, &rc_parent);
    } else {
        GetWindowRect(GetDesktopWindow(), &rc_parent);
    }

    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;

    s_splash_chosen_action = SPLASH_ACT_NONE;
    s_splash_dlg_done = false;
    g_splash_hover_item = 0;

    HWND prev_modal = g_active_modal_dlg;

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_TOPMOST,
        "ZuPatherSplashModal",
        "ZuPather - Zuma Path Editor",
        WS_POPUP | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, NULL
    );

    if (!hwnd_dlg) {
        return;
    }

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    SetFocus(hwnd_dlg);

    MSG msg;
    while (!s_splash_dlg_done && IsWindow(hwnd_dlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (s_splash_dlg_done) break;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_active_modal_dlg = prev_modal;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        SetForegroundWindow(parent_hwnd);
        SetFocus(parent_hwnd);
    }

    /* Trigger selected action after splash modal has cleanly closed */
    if (s_splash_chosen_action == SPLASH_ACT_NEW) {
        show_new_canvas_dialog(parent_hwnd);
    } else if (s_splash_chosen_action == SPLASH_ACT_OPEN) {
        show_open_canvas_dialog(parent_hwnd);
    } else if (s_splash_chosen_action == SPLASH_ACT_MAP) {
        show_manage_levels_dialog(parent_hwnd);
    }
}


/* ========================================================================= */
/* Settings & Preferences Modal Dialog Implementation                        */
/* ========================================================================= */

typedef struct {
    HWND hwnd_dlg;
    HWND hwnd_chk_direct_blit;
    HWND hwnd_chk_hud_cache;
    HWND hwnd_chk_smooth_scroll;
    HWND hwnd_edit_undo_limit;
    HWND hwnd_combo_theme;
    HWND hwnd_chk_show_grid;
    HWND hwnd_btn_change_zuma;
    HWND hwnd_btn_save;
    HWND hwnd_btn_cancel;
    HWND hwnd_btn_reset;

    bool chk_direct_blit;
    bool chk_hud_cache;
    bool chk_smooth_scroll;
    int  undo_limit_val;
    AppThemePreference theme_pref_val;
    bool show_grid;
    bool is_done;
} SettingsModalData;

static void draw_settings_card(HDC hdc, int x, int y, int w, int h, const char *title, const ThemeColors *th, bool is_dark) {
    COLORREF card_bg = is_dark ? RGB(28, 32, 40) : RGB(255, 255, 255);
    COLORREF card_bd = is_dark ? RGB(55, 62, 75) : RGB(215, 220, 230);

    HBRUSH br = CreateSolidBrush(card_bg);
    HPEN pen = CreatePen(PS_SOLID, 1, card_bd);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    HBRUSH old_br = (HBRUSH)SelectObject(hdc, br);
    RoundRect(hdc, x, y, x + w, y + h, 6, 6);
    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(br);
    DeleteObject(pen);

    if (title && title[0]) {
        RECT rc_t = { x + 14, y + 8, x + w - 14, y + 24 };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, th->text_accent_blue);
        HFONT old_f = (HFONT)SelectObject(hdc, g_font_primary_bold);
        DrawTextA(hdc, title, -1, &rc_t, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);
        SelectObject(hdc, old_f);
    }
}

static void draw_settings_themed_checkbox(HWND hwnd, HDC hdc, bool is_checked) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const ThemeColors *th = get_theme();
    bool is_dark = (g_current_theme == THEME_DARK);

    COLORREF bg_col = is_dark ? RGB(28, 32, 40) : RGB(255, 255, 255);
    COLORREF box_bg = is_dark ? RGB(36, 40, 48) : RGB(255, 255, 255);
    COLORREF box_bd = is_dark ? RGB(70, 78, 92) : RGB(175, 182, 194);
    COLORREF text_col = th->text_primary;
    COLORREF check_col = is_dark ? RGB(97, 175, 239) : RGB(24, 90, 175);

    HBRUSH bg_br = CreateSolidBrush(bg_col);
    FillRect(hdc, &rc, bg_br);
    DeleteObject(bg_br);

    int box_sz = 13;
    int box_x = 0;
    int box_y = (rc.bottom - rc.top - box_sz) / 2;
    RECT rc_box = { box_x, box_y, box_x + box_sz, box_y + box_sz };

    HBRUSH in_br = CreateSolidBrush(box_bg);
    FillRect(hdc, &rc_box, in_br);
    DeleteObject(in_br);

    HPEN pen = CreatePen(PS_SOLID, 1, box_bd);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc_box.left, rc_box.top, rc_box.right, rc_box.bottom);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    if (is_checked) {
        HPEN chk_pen = CreatePen(PS_SOLID, 2, check_col);
        old_pen = (HPEN)SelectObject(hdc, chk_pen);
        MoveToEx(hdc, box_x + 3, box_y + 6, NULL);
        LineTo(hdc, box_x + 5, box_y + 10);
        LineTo(hdc, box_x + 10, box_y + 3);
        SelectObject(hdc, old_pen);
        DeleteObject(chk_pen);
    }

    char text[128] = {0};
    GetWindowTextA(hwnd, text, sizeof(text));
    RECT rc_text = { box_x + box_sz + 8, 0, rc.right, rc.bottom };
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_col);
    HFONT font = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old_font = (HFONT)SelectObject(hdc, font);
    DrawTextA(hdc, text, -1, &rc_text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    SelectObject(hdc, old_font);
}

static WNDPROC s_orig_settings_chk_proc = NULL;
static LRESULT CALLBACK SettingsChkSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    bool *p_chk = (bool*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            if (p_chk) {
                *p_chk = !(*p_chk);
                InvalidateRect(hwnd, NULL, TRUE);
                SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_SPACE) {
                if (p_chk) {
                    *p_chk = !(*p_chk);
                    InvalidateRect(hwnd, NULL, TRUE);
                    SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
                }
                return 0;
            }
            break;
        case BM_SETCHECK:
            if (p_chk) {
                *p_chk = (wParam == BST_CHECKED);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        case BM_GETCHECK:
            return (p_chk && *p_chk) ? BST_CHECKED : BST_UNCHECKED;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            draw_settings_themed_checkbox(hwnd, hdc, p_chk ? *p_chk : false);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return CallWindowProcA(s_orig_settings_chk_proc ? s_orig_settings_chk_proc : DefWindowProcA, hwnd, msg, wParam, lParam);
}

static WNDPROC s_orig_settings_combo_proc = NULL;
static LRESULT CALLBACK SettingsThemedComboSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WNDPROC orig_proc = s_orig_settings_combo_proc ? s_orig_settings_combo_proc : DefWindowProcA;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        bool is_dark = (g_current_theme == THEME_DARK);

        COLORREF bg_col = is_dark ? RGB(36, 40, 48) : RGB(255, 255, 255);
        COLORREF border_col = is_dark ? RGB(70, 78, 92) : RGB(175, 182, 194);
        COLORREF text_col = is_dark ? RGB(222, 226, 235) : RGB(32, 36, 44);
        COLORREF arrow_col = is_dark ? RGB(160, 168, 182) : RGB(85, 92, 105);

        HBRUSH bg_br = CreateSolidBrush(bg_col);
        FillRect(hdc, &rc, bg_br);
        DeleteObject(bg_br);

        HPEN pen = CreatePen(PS_SOLID, 1, border_col);
        HPEN old_pen = (HPEN)SelectObject(hdc, pen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);

        int btn_w = 14;
        int arrow_cx = rc.right - btn_w / 2 - 1;
        int arrow_cy = (rc.bottom - rc.top) / 2;

        HPEN sep_pen = CreatePen(PS_SOLID, 1, border_col);
        old_pen = (HPEN)SelectObject(hdc, sep_pen);
        MoveToEx(hdc, rc.right - btn_w, rc.top + 3, NULL);
        LineTo(hdc, rc.right - btn_w, rc.bottom - 3);
        SelectObject(hdc, old_pen);
        DeleteObject(sep_pen);

        POINT pts[3];
        pts[0].x = arrow_cx - 3; pts[0].y = arrow_cy - 2;
        pts[1].x = arrow_cx + 3; pts[1].y = arrow_cy - 2;
        pts[2].x = arrow_cx;     pts[2].y = arrow_cy + 2;

        HBRUSH ar_br = CreateSolidBrush(arrow_col);
        HPEN ar_pen = CreatePen(PS_SOLID, 1, arrow_col);
        old_pen = (HPEN)SelectObject(hdc, ar_pen);
        HBRUSH old_br = (HBRUSH)SelectObject(hdc, ar_br);
        Polygon(hdc, pts, 3);
        SelectObject(hdc, old_br);
        SelectObject(hdc, old_pen);
        DeleteObject(ar_br);
        DeleteObject(ar_pen);

        int sel = (int)CallWindowProcA(orig_proc, hwnd, CB_GETCURSEL, 0, 0);
        const char *disp_text = "Dark";
        if (sel == 0) disp_text = "Light";
        else if (sel == 1) disp_text = "Dark";
        else if (sel == 2) disp_text = "System";

        RECT rc_text = { rc.left + 6, rc.top, rc.right - btn_w - 2, rc.bottom };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, text_col);
        HFONT font = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT old_font = (HFONT)SelectObject(hdc, font);
        DrawTextA(hdc, disp_text, -1, &rc_text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        SelectObject(hdc, old_font);

        EndPaint(hwnd, &ps);
        return 0;
    }
    return CallWindowProcA(orig_proc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK SettingsModalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const ThemeColors *th = get_theme();
    SettingsModalData *data = (SettingsModalData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    bool is_dark = (g_current_theme == THEME_DARK);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTA *cs = (CREATESTRUCTA*)lParam;
            data = (SettingsModalData*)cs->lpCreateParams;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)data);
            if (!data) return 0;
            data->hwnd_dlg = hwnd;
            HINSTANCE hinst = GetModuleHandle(NULL);

            #define MAKE_CHK(id, text, x, y, w, h, bool_ptr) \
                do { \
                    HWND chk = CreateWindowA("BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, \
                                            x, y, w, h, hwnd, (HMENU)(UINT_PTR)(id), hinst, NULL); \
                    SendMessage(chk, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE); \
                    SetWindowLongPtrA(chk, GWLP_USERDATA, (LONG_PTR)(bool_ptr)); \
                    s_orig_settings_chk_proc = (WNDPROC)SetWindowLongPtrA(chk, GWLP_WNDPROC, (LONG_PTR)SettingsChkSubclassProc); \
                } while (0)

            /* Section 1: Performance Checkboxes (inside Card 1: y=52, h=140) */
            MAKE_CHK(ID_SETTINGS_CHK_DIRECT_BLIT, "Direct Blit (Bypass CPU AlphaBlend on opaque HUDs)",
                     34, 80, 472, 17, &data->chk_direct_blit);
            MAKE_CHK(ID_SETTINGS_CHK_HUD_CACHE, "Offscreen HUD Surface Caching (60+ FPS Viewport)",
                     34, 116, 472, 17, &data->chk_hud_cache);
            MAKE_CHK(ID_SETTINGS_CHK_SMOOTH_SCROLL, "Smooth Mouse Wheel & Precision Event Coalescing",
                     34, 152, 472, 17, &data->chk_smooth_scroll);

            /* Section 2: Action History Edit (inside Card 2: y=200, h=76) */
            /* Control Box: exactly 60x24 px (compact numeric input) */
            data->hwnd_edit_undo_limit = CreateWindowExA(
                WS_EX_CLIENTEDGE, "EDIT", "64",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER,
                194, 227, 60, 24, hwnd, (HMENU)ID_SETTINGS_EDIT_UNDO_LIMIT, hinst, NULL
            );
            SendMessage(data->hwnd_edit_undo_limit, WM_SETFONT, (WPARAM)g_font_mono_bold, TRUE);
            char cur_limit_str[32];
            snprintf(cur_limit_str, sizeof(cur_limit_str), "%d", data->undo_limit_val);
            SetWindowTextA(data->hwnd_edit_undo_limit, cur_limit_str);

            /* Section 3: Workspace & Environment (inside Card 3: y=284, h=114) */
            /* Control Box: exactly 60x24 px (matching Action History width and height) */
            data->hwnd_combo_theme = CreateWindowA(
                "COMBOBOX", "",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
                122, 308, 60, 120, hwnd, (HMENU)ID_SETTINGS_COMBO_THEME, hinst, NULL
            );
            SendMessage(data->hwnd_combo_theme, WM_SETFONT, (WPARAM)g_font_primary_regular, TRUE);
            s_orig_settings_combo_proc = (WNDPROC)SetWindowLongPtrA(data->hwnd_combo_theme, GWLP_WNDPROC, (LONG_PTR)SettingsThemedComboSubclassProc);
            SendMessage(data->hwnd_combo_theme, CB_SETDROPPEDWIDTH, 150, 0);

            SendMessageA(data->hwnd_combo_theme, CB_ADDSTRING, 0, (LPARAM)"Light");
            SendMessageA(data->hwnd_combo_theme, CB_ADDSTRING, 0, (LPARAM)"Dark");
            SendMessageA(data->hwnd_combo_theme, CB_ADDSTRING, 0, (LPARAM)"Use System Default");
            int sel_th = (int)data->theme_pref_val;
            if (sel_th < 0 || sel_th > 2) {
                sel_th = (g_current_theme == THEME_DARK) ? 1 : 0;
            }
            SendMessage(data->hwnd_combo_theme, CB_SETCURSEL, sel_th, 0);

            MAKE_CHK(ID_SETTINGS_CHK_SHOW_GRID, "Show Canvas Coordinate Grid by Default",
                     34, 338, 472, 17, &data->show_grid);

            /* Control Box: 80x24 px */
            data->hwnd_btn_change_zuma = CreateWindowA(
                "BUTTON", "Change...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                424, 362, 80, 24, hwnd, (HMENU)ID_SETTINGS_BTN_CHANGE_ZUMA, hinst, NULL
            );

            /* Bottom Action Buttons: perfectly equal margins (20px top, 20px bottom, 20px left, 20px right) */
            data->hwnd_btn_reset = CreateWindowA(
                "BUTTON", "Reset Defaults", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                20, 418, 105, 28, hwnd, (HMENU)ID_SETTINGS_BTN_RESET, hinst, NULL
            );
            data->hwnd_btn_cancel = CreateWindowA(
                "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                320, 418, 80, 28, hwnd, (HMENU)ID_SETTINGS_BTN_CANCEL, hinst, NULL
            );
            data->hwnd_btn_save = CreateWindowA(
                "BUTTON", "Save & Apply", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                410, 418, 110, 28, hwnd, (HMENU)ID_SETTINGS_BTN_SAVE, hinst, NULL
            );

            #undef MAKE_CHK
            return 0;
        }

        case WM_MEASUREITEM: {
            LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lParam;
            if (mis->CtlType == ODT_COMBOBOX && mis->CtlID == ID_SETTINGS_COMBO_THEME) {
                mis->itemHeight = 18;
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            COLORREF bg = is_dark ? RGB(36, 40, 48) : RGB(255, 255, 255);
            COLORREF fg = is_dark ? RGB(220, 225, 235) : RGB(20, 24, 32);
            SetBkColor(hdc, bg);
            SetTextColor(hdc, fg);
            static HBRUSH s_edit_brush = NULL;
            if (s_edit_brush) DeleteObject(s_edit_brush);
            s_edit_brush = CreateSolidBrush(bg);
            return (LRESULT)s_edit_brush;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, th->text_primary);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            /* 1. Dialog Background */
            HBRUSH bg_br = CreateSolidBrush(th->bar_bg);
            FillRect(hdc, &rc, bg_br);
            DeleteObject(bg_br);

            SetBkMode(hdc, TRANSPARENT);

            /* 2. Header Banner */
            RECT rc_title = { 20, 12, rc.right - 20, 30 };
            SetTextColor(hdc, th->text_primary);
            SelectObject(hdc, g_font_primary_bold);
            DrawTextA(hdc, "Settings & Preferences", -1, &rc_title, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);

            RECT rc_sub = { 20, 30, rc.right - 20, 46 };
            SetTextColor(hdc, th->text_secondary);
            SelectObject(hdc, g_font_primary_regular);
            DrawTextA(hdc, "Configure viewport optimizations, action history, and workspace.", -1, &rc_sub, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);

            /* 3. Card 1: PERFORMANCE (y: 52 to 192 -> h = 140) */
            draw_settings_card(hdc, 20, 52, rc.right - 40, 140, "PERFORMANCE & GPU ACCELERATION", th, is_dark);
            SelectObject(hdc, g_font_primary_regular);
            SetTextColor(hdc, th->text_dim);
            RECT rc_d1 = { 54, 98, rc.right - 40, 112 };
            DrawTextA(hdc, "Hardware block transfer (BitBlt). Eliminates CPU blend loops on opaque HUDs.", -1, &rc_d1, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);
            RECT rc_d2 = { 54, 134, rc.right - 40, 148 };
            DrawTextA(hdc, "Caches rendered HUD surfaces for zero-lag 60+ FPS zoom & scroll.", -1, &rc_d2, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);
            RECT rc_d3 = { 54, 170, rc.right - 40, 184 };
            DrawTextA(hdc, "Smoothly handles high-frequency wheel ticks on gaming and precision mice.", -1, &rc_d3, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);

            /* 4. Card 2: ACTION HISTORY (y: 200 to 276 -> h = 76) */
            draw_settings_card(hdc, 20, 200, rc.right - 40, 76, "ACTION HISTORY", th, is_dark);
            RECT rc_lbl_hist = { 34, 227, 188, 251 };
            SetTextColor(hdc, th->text_primary);
            SelectObject(hdc, g_font_primary_regular);
            DrawTextA(hdc, "Snapshot History Limit:", -1, &rc_lbl_hist, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);

            RECT rc_unit_hist = { 262, 227, rc.right - 40, 251 };
            SetTextColor(hdc, th->text_secondary);
            DrawTextA(hdc, "actions", -1, &rc_unit_hist, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);

            RECT rc_sub_hist = { 34, 253, rc.right - 40, 268 };
            SetTextColor(hdc, th->text_dim);
            DrawTextA(hdc, "Maximum undo/redo states retained in memory (zero disk cache).", -1, &rc_sub_hist, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);

            /* 5. Card 3: WORKSPACE & ENVIRONMENT (y: 284 to 398 -> h = 114) */
            draw_settings_card(hdc, 20, 284, rc.right - 40, 114, "WORKSPACE & ENVIRONMENT", th, is_dark);
            RECT rc_lbl_theme = { 34, 308, 118, 332 };
            SetTextColor(hdc, th->text_primary);
            SelectObject(hdc, g_font_primary_regular);
            DrawTextA(hdc, "Color Theme:", -1, &rc_lbl_theme, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);

            RECT rc_lbl_zuma = { 34, 362, 118, 386 };
            DrawTextA(hdc, "Zuma Folder:", -1, &rc_lbl_zuma, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);

            char zuma_disp[MAX_PATH];
            if (g_has_zuma_dir && g_zuma_levels_dir[0]) {
                snprintf(zuma_disp, sizeof(zuma_disp), "%s", g_zuma_levels_dir);
            } else {
                snprintf(zuma_disp, sizeof(zuma_disp), "(No Zuma folder configured)");
            }
            SetTextColor(hdc, (g_has_zuma_dir && g_zuma_levels_dir[0]) ? th->text_accent_green : th->text_secondary);
            SelectObject(hdc, g_font_mono_regular);
            RECT rc_txt_zuma = { 122, 362, 416, 386 };
            DrawTextA(hdc, zuma_disp, -1, &rc_txt_zuma, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS);

            /* Notice: NO horizontal line above the bottom buttons! */

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_COMBOBOX && dis->CtlID == ID_SETTINGS_COMBO_THEME) {
                if (dis->itemID == (UINT)-1) return TRUE;
                bool is_dark = (g_current_theme == THEME_DARK);
                bool is_sel = (dis->itemState & ODS_SELECTED) != 0;

                COLORREF bg_col;
                if (is_sel) {
                    bg_col = is_dark ? RGB(45, 60, 85) : RGB(215, 230, 252);
                } else {
                    bg_col = is_dark ? RGB(32, 36, 44) : RGB(255, 255, 255);
                }
                HBRUSH br = CreateSolidBrush(bg_col);
                FillRect(dis->hDC, &dis->rcItem, br);
                DeleteObject(br);

                char item_text[64] = {0};
                SendMessageA(dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LPARAM)item_text);

                COLORREF text_col = is_dark ? RGB(225, 230, 240) : RGB(30, 35, 45);
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, text_col);
                HFONT old_f = (HFONT)SelectObject(dis->hDC, g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

                RECT rc_t = dis->rcItem;
                rc_t.left += 8;
                DrawTextA(dis->hDC, item_text, -1, &rc_t, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
                SelectObject(dis->hDC, old_f);
                return TRUE;
            }

            if (dis->CtlType == ODT_BUTTON) {
                bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool is_save = (dis->CtlID == ID_SETTINGS_BTN_SAVE);

                COLORREF bg_col;
                COLORREF border_col;
                COLORREF text_col;

                if (is_save) {
                    bg_col = is_pressed ? RGB(18, 70, 140) : th->text_accent_blue;
                    border_col = bg_col;
                    text_col = RGB(255, 255, 255);
                } else {
                    bg_col = is_pressed ? th->btn_bg_pressed : th->btn_bg;
                    border_col = th->btn_border;
                    text_col = th->btn_text;
                }

                HBRUSH bg_brush = CreateSolidBrush(bg_col);
                FillRect(dis->hDC, &dis->rcItem, bg_brush);
                DeleteObject(bg_brush);

                HPEN pen = CreatePen(PS_SOLID, 1, border_col);
                HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, old_pen);
                DeleteObject(pen);

                char text[48] = {0};
                GetWindowTextA(dis->hwndItem, text, sizeof(text));

                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, text_col);
                HFONT hfont_btn = is_save ? g_font_primary_bold : g_font_primary_regular;
                if (!hfont_btn) hfont_btn = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HFONT old_font = (HFONT)SelectObject(dis->hDC, hfont_btn);

                RECT rc_t = dis->rcItem;
                if (is_pressed) { rc_t.left += 1; rc_t.top += 1; }
                DrawTextA(dis->hDC, text, -1, &rc_t, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
                SelectObject(dis->hDC, old_font);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (cmd == ID_SETTINGS_BTN_CHANGE_ZUMA) {
                handle_set_zuma_folder(hwnd);
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (cmd == ID_SETTINGS_BTN_RESET) {
                if (data) {
                    data->chk_direct_blit = true;
                    data->chk_hud_cache = true;
                    data->chk_smooth_scroll = true;
                    data->show_grid = true;
                    data->theme_pref_val = THEME_PREF_SYSTEM;
                    if (data->hwnd_combo_theme) {
                        SendMessage(data->hwnd_combo_theme, CB_SETCURSEL, (WPARAM)THEME_PREF_SYSTEM, 0);
                        InvalidateRect(data->hwnd_combo_theme, NULL, TRUE);
                    }
                    if (data->hwnd_edit_undo_limit) SetWindowTextA(data->hwnd_edit_undo_limit, "64");
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            } else if (cmd == ID_SETTINGS_BTN_CANCEL) {
                if (data) data->is_done = true;
                DestroyWindow(hwnd);
            } else if (cmd == ID_SETTINGS_BTN_SAVE) {
                if (data) {
                    char buf[32] = {0};
                    if (data->hwnd_edit_undo_limit) {
                        GetWindowTextA(data->hwnd_edit_undo_limit, buf, sizeof(buf));
                    }
                    int lim = atoi(buf);
                    if (lim < 10) lim = 10;
                    if (lim > MAX_UNDO_STACK) lim = MAX_UNDO_STACK;

                    g_perf_direct_blit = data->chk_direct_blit;
                    g_perf_hud_cache = data->chk_hud_cache;
                    g_perf_smooth_scroll = data->chk_smooth_scroll;
                    g_show_grid = data->show_grid;

                    int sel_th = (int)SendMessage(data->hwnd_combo_theme, CB_GETCURSEL, 0, 0);
                    if (sel_th < 0 || sel_th > 2) sel_th = (int)THEME_PREF_DARK;
                    AppThemePreference new_pref = (AppThemePreference)sel_th;

                    HWND parent = GetParent(hwnd);
                    if (parent && IsWindow(parent)) {
                        apply_theme_preference(parent, new_pref);
                    } else {
                        g_theme_preference = new_pref;
                        g_current_theme = (new_pref == THEME_PREF_SYSTEM)
                            ? (is_system_dark_mode() ? THEME_DARK : THEME_LIGHT)
                            : (new_pref == THEME_PREF_DARK ? THEME_DARK : THEME_LIGHT);
                    }

                    undo_set_history_limit(lim);
                    invalidate_hud_cache();
                    save_zuma_config();

                    if (parent && IsWindow(parent)) {
                        create_main_menu(parent);
                        InvalidateRect(parent, NULL, TRUE);
                        UpdateWindow(parent);
                    }

                    data->is_done = true;
                }
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_CLOSE: {
            if (data) data->is_done = true;
            DestroyWindow(hwnd);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void show_settings_dialog(HWND parent_hwnd) {
    HINSTANCE hinst = GetModuleHandle(NULL);
    static bool s_settings_class_registered = false;
    if (!s_settings_class_registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.style         = CS_DROPSHADOW | CS_DBLCLKS;
        wc.lpfnWndProc   = SettingsModalWndProc;
        wc.hInstance     = hinst;
        wc.lpszClassName = "ZuPatherSettingsModal";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        s_settings_class_registered = true;
    }

    RECT rc_dlg = { 0, 0, 540, 466 };
    AdjustWindowRectEx(&rc_dlg, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int dlg_w = rc_dlg.right - rc_dlg.left;
    int dlg_h = rc_dlg.bottom - rc_dlg.top;

    RECT rc_parent = {0};
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        GetWindowRect(parent_hwnd, &rc_parent);
    } else {
        GetWindowRect(GetDesktopWindow(), &rc_parent);
    }

    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;

    SettingsModalData data;
    memset(&data, 0, sizeof(data));
    data.chk_direct_blit = g_perf_direct_blit;
    data.chk_hud_cache = g_perf_hud_cache;
    data.chk_smooth_scroll = g_perf_smooth_scroll;
    data.undo_limit_val = g_undo_limit;
    data.theme_pref_val = g_theme_preference;
    data.show_grid = g_show_grid;
    data.is_done = false;

    HWND prev_modal = g_active_modal_dlg;
    BOOL parent_was_enabled = TRUE;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        parent_was_enabled = IsWindowEnabled(parent_hwnd);
        if (parent_was_enabled) {
            EnableWindow(parent_hwnd, FALSE);
        }
    }

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        "ZuPatherSettingsModal",
        "Settings & Preferences",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, &data
    );

    if (!hwnd_dlg) {
        if (parent_hwnd && IsWindow(parent_hwnd) && parent_was_enabled) {
            EnableWindow(parent_hwnd, TRUE);
        }
        return;
    }

    update_window_theme_attributes(hwnd_dlg, g_current_theme == THEME_DARK);
    g_active_modal_dlg = hwnd_dlg;
    SetFocus(hwnd_dlg);

    MSG msg;
    while (!data.is_done && IsWindow(hwnd_dlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (data.is_done) break;
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_ESCAPE) {
                data.is_done = true;
                DestroyWindow(hwnd_dlg);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_active_modal_dlg = prev_modal;
    if (parent_hwnd && IsWindow(parent_hwnd)) {
        if (parent_was_enabled) {
            EnableWindow(parent_hwnd, TRUE);
        }
        SetForegroundWindow(parent_hwnd);
        SetFocus(parent_hwnd);
    }
}
