#include "zupather_state.h"
#include "dialog_modals.h"
#include "svg_path.h"
#include "undo_redo.h"

/* Global State Variable Definitions */
/* Font Handles */
HFONT g_font_primary_regular = NULL;
HFONT g_font_primary_bold    = NULL;
HFONT g_font_title_bold      = NULL;
HFONT g_font_mono_regular    = NULL;
HFONT g_font_mono_bold       = NULL;


const ThemeColors g_theme_light = {
    .workspace_bg           = RGB(142, 146, 155), /* Neutral gray workspace */
    .canvas_bg              = RGB(255, 255, 255), /* Pure white canvas sheet */
    .canvas_border          = RGB(40, 44, 52),    /* Crisp outline */
    .canvas_grid_major      = RGB(216, 222, 232),
    .canvas_grid_minor      = RGB(236, 240, 246),
    .workspace_grid_major   = RGB(156, 160, 170),
    .workspace_grid_minor   = RGB(149, 153, 162),
    .canvas_crosshair       = RGB(180, 185, 195),
    .bar_bg                 = RGB(238, 240, 244),
    .bar_border             = RGB(180, 185, 195),
    .btn_bg                 = RGB(255, 255, 255),
    .btn_bg_pressed         = RGB(218, 222, 230),
    .btn_border             = RGB(175, 182, 194),
    .btn_text               = RGB(32, 36, 44),
    .text_primary           = RGB(32, 36, 44),
    .text_secondary         = RGB(85, 92, 105),
    .text_accent_blue       = RGB(24, 90, 175),
    .text_accent_green      = RGB(22, 125, 50),
    .text_accent_red        = RGB(185, 45, 40),
    .text_dim               = RGB(115, 122, 135)
};

const ThemeColors g_theme_dark = {
    .workspace_bg           = RGB(28, 31, 37),    /* Neutral charcoal workspace */
    .canvas_bg              = RGB(42, 46, 54),    /* Dark slate canvas sheet */
    .canvas_border          = RGB(90, 96, 110),   /* Neutral border */
    .canvas_grid_major      = RGB(60, 66, 78),
    .canvas_grid_minor      = RGB(50, 55, 65),
    .workspace_grid_major   = RGB(40, 44, 54),
    .workspace_grid_minor   = RGB(34, 37, 45),
    .canvas_crosshair       = RGB(92, 100, 116),
    .bar_bg                 = RGB(22, 24, 28),
    .bar_border             = RGB(44, 48, 56),
    .btn_bg                 = RGB(36, 40, 48),
    .btn_bg_pressed         = RGB(20, 22, 27),
    .btn_border             = RGB(62, 69, 82),
    .btn_text               = RGB(222, 226, 235),
    .text_primary           = RGB(222, 226, 235),
    .text_secondary         = RGB(160, 168, 182),
    .text_accent_blue       = RGB(97, 175, 239),
    .text_accent_green      = RGB(152, 195, 121),
    .text_accent_red        = RGB(224, 108, 117),
    .text_dim               = RGB(110, 118, 132)
};

/* Active Application State */
AppThemePreference g_theme_preference = THEME_PREF_DARK;
AppTheme g_current_theme = THEME_DARK;

/* Canvas State (Initial launch has NO canvas) */
bool        g_has_canvas       = false;
char        g_canvas_name[128]  = "";
char        g_canvas_title[128] = "";
int         g_canvas_w          = DEFAULT_CANVAS_WIDTH;
int         g_canvas_h         = DEFAULT_CANVAS_HEIGHT;
bool        g_in_canvas_edit_sync = false;
CanvasDragSnapshot g_canvas_drag_snap;
bool        g_has_frog         = false;
int         g_frog_gx          = DEFAULT_CANVAS_WIDTH / 2;
int         g_frog_gy          = DEFAULT_CANVAS_HEIGHT / 2;
bool        g_placing_frog     = false;
bool        g_frog_hovered     = false;
bool        g_frog_selected    = false;
int         g_mouse_screen_x   = -1000;
int         g_mouse_screen_y   = -1000;
CanvasImage g_frog_img;
bool        g_has_frog_img     = false;
CanvasImage g_canvas_img;
bool        g_has_bg_image     = false;

TreasurePoint g_treasure_points[MAX_TREASURE_POINTS];
int           g_treasure_point_count = 0;
bool          g_placing_coin         = false;
bool          g_picked_up_coin       = false;
int           g_hovered_coin_idx     = -1;
CanvasImage   g_coin_img;
bool          g_has_coin_img         = false;

/* BackgroundAlpha elements state */
LevelBackgroundAlpha g_bg_alphas[MAX_BG_ALPHAS];
int           g_bg_alpha_count          = 0;
bool          g_placing_bgalpha         = false;
bool          g_picked_up_bgalpha       = false;
bool          g_placing_bgalpha_vector  = false;
int           g_hovered_bgalpha_idx     = -1;
int           g_selected_bgalpha_idx    = 0;
int           g_editing_bgalpha_idx     = -1;
int           g_bgalpha_anchor_x        = 0;
int           g_bgalpha_anchor_y        = 0;
int           g_bgalpha_pending_vx      = 0;
int           g_bgalpha_pending_vy      = 0;
CanvasImage   g_default_bgalpha_img;
bool          g_has_default_bgalpha_img = false;
HBITMAP       g_default_bgalpha_mask_light = NULL;
HDC           g_default_bgalpha_hdc_light  = NULL;
HBITMAP       g_default_bgalpha_mask_dark  = NULL;
HDC           g_default_bgalpha_hdc_dark   = NULL;
CanvasImage   g_pending_bgalpha_img;
bool          g_has_pending_bgalpha_img = false;
char          g_pending_bgalpha_filepath[MAX_PATH] = "";

/* Cutouts elements state */
LevelCutout   g_cutouts[MAX_CUTOUTS];
int           g_cutout_count            = 0;
bool          g_placing_cutout          = false;
bool          g_picked_up_cutout        = false;
int           g_hovered_cutout_idx      = -1;
int           g_selected_cutout_idx     = 0;
int           g_pending_cutout_pri      = 1;
CanvasImage   g_default_cutout_img;
bool          g_has_default_cutout_img  = false;
HBITMAP       g_default_cutout_mask_light = NULL;
HDC           g_default_cutout_hdc_light  = NULL;
HBITMAP       g_default_cutout_mask_dark  = NULL;
HDC           g_default_cutout_hdc_dark   = NULL;

/* Pending custom Cutout being placed */
CanvasImage   g_pending_cutout_img;
bool          g_has_pending_cutout_img    = false;
char          g_pending_cutout_filepath[MAX_PATH] = "";
HBITMAP       g_pending_cutout_mask_light = NULL;
HDC           g_pending_cutout_hdc_light  = NULL;
HBITMAP       g_pending_cutout_mask_dark  = NULL;
HDC           g_pending_cutout_hdc_dark   = NULL;

/* Sprites elements state */
LevelSprite   g_sprites[MAX_SPRITES];
int           g_sprite_count            = 0;
bool          g_placing_sprite          = false;
bool          g_picked_up_sprite        = false;
int           g_hovered_sprite_idx      = -1;
int           g_selected_sprite_idx     = 0;
int           g_pending_sprite_pri      = 1;
CanvasImage   g_default_sprite_img;
bool          g_has_default_sprite_img  = false;

/* Pending custom Sprite being placed */
CanvasImage   g_pending_sprite_img;
bool          g_has_pending_sprite_img    = false;
char          g_pending_sprite_filepath[MAX_PATH] = "";

/* Canvas Mode State */
bool          g_mode_canvas             = false;
CanvasFitMode g_canvas_fit_mode         = CANVAS_FIT_ORIGINAL;
CanvasAlignment g_canvas_alignment      = CANVAS_ALIGN_TOP_LEFT;
bool          g_hud_canvas_collapsed    = false;
bool          g_hud_canvas_guide_collapsed = false;
bool          g_cv_affect_element       = false;
bool          g_cv_affect_curve         = false;
bool          g_cv_affect_overlay       = false;

/* Preview Mode & Level Preview Simulation State */
bool          g_hud_preview_guide_collapsed = false;
bool          g_hud_preview_info_collapsed  = false;
bool          g_level_preview_active        = false;
bool          g_level_preview_stored_state  = false;
float         g_preview_anim_distance       = 0.0f;
MapAttrHoverItem g_hud_map_attr_hover       = MAP_ATTR_HOVER_NONE;
int           g_hud_map_attr_hover_y        = 0;

/* Overlay Mode Toolbar & Canvas State */
bool          g_mode_element            = false; /* Element Mode (Frog & Coins) */
bool          g_mode_overlay            = false;
int           g_overlay_current_prio    = 1; /* Default 1, min 0, max 99 */
HWND          g_btn_overlay             = NULL;
char          g_canvas_folder[MAX_PATH] = "";

/* Element HUDs State */
bool          g_hud_element_collapsed       = false; /* Top-Right Element Tools HUD (F2) */
bool          g_hud_element_guide_collapsed = false; /* Top-Left Element Guide HUD (F1) */

/* Overlay HUDs State */
bool          g_hud_overlay_collapsed       = false; /* Top-Right Tools HUD (F2) */
bool          g_hud_overlay_guide_collapsed = false; /* Top-Left Guide HUD (F1) */
int           g_overlay_active_tab          = 0; /* 0 = Cutout, 1 = Sprite, 2 = BgAlpha */
bool          g_adjusting_overlay_priority  = false;
int           g_editing_prio_type           = 0; /* 0 = Cutout, 1 = Sprite */
int           g_editing_prio_idx            = -1;
int           g_orig_overlay_priority       = 0;
int           g_pending_overlay_priority    = 0;

BezierPath   g_paths[MAX_PATHS];
int          g_path_count           = 0;
int          g_active_path_idx      = -1;
int          g_active_node_idx      = -1;
bool         g_mode_curve           = false;

CurveSubMode g_curve_submode           = CURVE_SUBMODE_DRAW;
bool         g_hud_mode_collapsed      = false;
bool         g_hud_shortcuts_collapsed = false;

/* Variable Mode Attributes & Dragging State */
bool         g_var_write_tunnel        = true;
uint8_t      g_var_tunnel_val          = 0; /* Default 0 (false / hittable) */
bool         g_var_write_prio          = true;
uint8_t      g_var_prio_val            = 0; /* Default 0 (surface / baseline) */
bool         g_var_dragging            = false;
double       g_var_drag_s_start        = 0.0;
double       g_var_drag_s_cur          = 0.0;
bool         g_var_hover_active        = false;
double       g_var_hover_s             = 0.0;
float        g_var_hover_x             = 0.0f;
float        g_var_hover_y             = 0.0f;
uint8_t      g_var_hover_tun           = 0;
uint8_t      g_var_hover_prio          = 1;

int          g_hovered_curve_seg_path  = -1;
int          g_hovered_curve_seg_idx   = -1;
float        g_hovered_curve_seg_t     = 0.0f;
float        g_hovered_curve_seg_cx    = 0.0f;
float        g_hovered_curve_seg_cy    = 0.0f;

PenDragState g_pen_drag_state       = PEN_DRAG_NONE;
int          g_drag_path_idx        = -1;
int          g_drag_node_idx        = -1;
float        g_drag_anchor_start_x  = 0.0f;
float        g_drag_anchor_start_y  = 0.0f;
float        g_drag_mouse_start_cx  = 0.0f;
float        g_drag_mouse_start_cy  = 0.0f;
int          g_marquee_start_x      = 0;
int          g_marquee_start_y      = 0;
int          g_marquee_cur_x        = 0;
int          g_marquee_cur_y        = 0;

MarqueeSelectMode g_marquee_mode = MARQUEE_MODE_REPLACE;
bool *g_marquee_snapshot[MAX_PATHS] = {0};
int   g_marquee_snapshot_count[MAX_PATHS] = {0};

ObjDragState      g_obj_drag_state      = OBJ_DRAG_NONE;
int               g_obj_marquee_start_x = 0;
int               g_obj_marquee_start_y = 0;
int               g_obj_marquee_cur_x   = 0;
int               g_obj_marquee_cur_y   = 0;
MarqueeSelectMode g_obj_marquee_mode    = MARQUEE_MODE_REPLACE;

bool g_snap_frog_selected = false;
bool g_snap_coin_selected[MAX_TREASURE_POINTS] = {0};
bool g_snap_cutout_selected[MAX_CUTOUTS] = {0};
bool g_snap_sprite_selected[MAX_SPRITES] = {0};
bool g_snap_bgalpha_selected[MAX_BG_ALPHAS] = {0};

/* Window & Control Handles */
HWND g_hwnd_main          = NULL;
HWND g_hwnd_toolbar_left  = NULL;
HWND g_hwnd_toolbar_right = NULL;
HWND g_hwnd_status        = NULL;
HMENU g_hmenu_action_history = NULL;
HWND g_btn_frog           = NULL;
HWND g_btn_coin           = NULL;
HWND g_btn_canvas         = NULL;
HWND g_btn_manage_maps    = NULL;
HWND g_btn_element        = NULL;
HWND g_btn_curve          = NULL;
HWND g_edit_w             = NULL;
HWND g_edit_h             = NULL;
HWND g_btn_apply_size     = NULL;
HWND g_edit_cv_w          = NULL;
HWND g_edit_cv_h          = NULL;
HWND g_edit_prio          = NULL;
bool g_in_prio_update     = false;

/* Toolbar Scroll & Dragging State */
int  g_tb_scroll_x              = 0;
bool g_is_dragging_thumb        = false;
int  g_drag_thumb_mouse_start_x = 0;
int  g_drag_thumb_scroll_start  = 0;
bool g_is_dragging_tb_bg        = false;
int  g_drag_bg_mouse_start_x    = 0;
int  g_drag_bg_scroll_start     = 0;

/* Floating HUD Scroll & Dragging State (Guide & Curve Attribute) */
int  g_hud_guide_scroll_y          = 0;
bool g_is_dragging_guide_thumb     = false;
int  g_drag_guide_mouse_start_y    = 0;
int  g_drag_guide_scroll_start     = 0;
bool g_is_dragging_guide_bg        = false;
int  g_drag_guide_bg_mouse_start_y = 0;
int  g_drag_guide_bg_scroll_start  = 0;

int  g_hud_attr_scroll_y           = 0;
bool g_is_dragging_attr_thumb      = false;
int  g_drag_attr_mouse_start_y     = 0;
int  g_drag_attr_scroll_start      = 0;
bool g_is_dragging_attr_bg         = false;
int  g_drag_attr_bg_mouse_start_y  = 0;
int  g_drag_attr_bg_scroll_start   = 0;

int  g_hud_overlay_guide_scroll_y          = 0;
bool g_is_dragging_ov_guide_thumb          = false;
int  g_drag_ov_guide_mouse_start_y         = 0;
int  g_drag_ov_guide_scroll_start          = 0;
bool g_is_dragging_ov_guide_bg             = false;
int  g_drag_ov_guide_bg_mouse_start_y      = 0;
int  g_drag_ov_guide_bg_scroll_start       = 0;

int  g_hud_overlay_tools_scroll_y          = 0;
bool g_is_dragging_ov_tools_thumb          = false;
int  g_drag_ov_tools_mouse_start_y         = 0;
int  g_drag_ov_tools_scroll_start          = 0;
bool g_is_dragging_ov_tools_bg             = false;
int  g_drag_ov_tools_bg_mouse_start_y      = 0;
int  g_drag_ov_tools_bg_scroll_start       = 0;

/* Element Mode Floating HUD Scroll & Dragging State */
int  g_hud_element_guide_scroll_y          = 0;
bool g_is_dragging_el_guide_thumb          = false;
int  g_drag_el_guide_mouse_start_y         = 0;
int  g_drag_el_guide_scroll_start          = 0;
bool g_is_dragging_el_guide_bg             = false;
int  g_drag_el_guide_bg_mouse_start_y      = 0;
int  g_drag_el_guide_bg_scroll_start       = 0;

int  g_hud_element_tools_scroll_y          = 0;
bool g_is_dragging_el_tools_thumb          = false;
int  g_drag_el_tools_mouse_start_y         = 0;
int  g_drag_el_tools_scroll_start          = 0;
bool g_is_dragging_el_tools_bg             = false;
int  g_drag_el_tools_bg_mouse_start_y      = 0;
int  g_drag_el_tools_bg_scroll_start       = 0;

/* Canvas Mode Floating HUD Scroll & Dragging State */
int  g_hud_canvas_guide_scroll_y          = 0;
bool g_is_dragging_cv_guide_thumb          = false;
int  g_drag_cv_guide_mouse_start_y         = 0;
int  g_drag_cv_guide_scroll_start          = 0;
bool g_is_dragging_cv_guide_bg             = false;
int  g_drag_cv_guide_bg_mouse_start_y      = 0;
int  g_drag_cv_guide_bg_scroll_start       = 0;

int  g_hud_canvas_tools_scroll_y          = 0;
bool g_is_dragging_cv_tools_thumb          = false;
int  g_drag_cv_tools_mouse_start_y         = 0;
int  g_drag_cv_tools_scroll_start          = 0;
bool g_is_dragging_cv_tools_bg             = false;
int  g_drag_cv_tools_bg_mouse_start_y      = 0;
int  g_drag_cv_tools_bg_scroll_start       = 0;

/* Preview Mode Floating HUD Scroll & Dragging State */
int  g_hud_preview_guide_scroll_y          = 0;
bool g_is_dragging_prev_guide_thumb        = false;
int  g_drag_prev_guide_mouse_start_y       = 0;
int  g_drag_prev_guide_scroll_start        = 0;
bool g_is_dragging_prev_guide_bg           = false;
int  g_drag_prev_guide_bg_mouse_start_y    = 0;
int  g_drag_prev_guide_bg_scroll_start     = 0;

int  g_hud_preview_info_scroll_y           = 0;
bool g_is_dragging_prev_info_thumb         = false;
int  g_drag_prev_info_mouse_start_y        = 0;
int  g_drag_prev_info_scroll_start         = 0;
bool g_is_dragging_prev_info_bg            = false;
int  g_drag_prev_info_bg_mouse_start_y     = 0;
int  g_drag_prev_info_bg_scroll_start      = 0;

/* Level Preview Assets (11 Balls & Skull) */
CanvasImage g_ball_imgs[LEVEL_PREVIEW_BALL_COLORS];
bool        g_has_ball_imgs[LEVEL_PREVIEW_BALL_COLORS] = { false };
CanvasImage g_skull_img;
bool        g_has_skull_img                        = false;
HWND        g_edit_preview_title                   = NULL;

/* Map / Graphics Attributes */
bool  g_map_has_skullrot        = false;
float g_map_skullrot            = 0.0f;
bool  g_map_is_space            = false;

/* Developer Attributes (Testing Kit) */
bool  g_dev_has_drawcurve       = false;
bool  g_dev_drawcurve           = false;
bool  g_dev_has_drawtunnels     = false;
bool  g_dev_drawtunnels         = false;
bool  g_dev_has_curvecolor      = false;
char  g_dev_curvecolor[32]      = "0x00FF00";
bool  g_dev_has_bgcolor         = false;
char  g_dev_bgcolor[32]         = "0x000000";
bool  g_dev_has_destroyall      = false;
bool  g_dev_destroyall          = false;
bool  g_dev_has_huerot          = false;
float g_dev_huerot              = 0.0f;
bool  g_dev_has_treasurefreq    = false;
int   g_dev_treasurefreq        = 1000;
bool  g_dev_has_partime         = false;
int   g_dev_partime             = 300;

HWND  g_edit_map_skullrot       = NULL;
HWND  g_edit_dev_curvecolor     = NULL;
HWND  g_edit_dev_bgcolor        = NULL;
HWND  g_edit_dev_huerot         = NULL;
HWND  g_edit_dev_treasurefreq   = NULL;
HWND  g_edit_dev_partime        = NULL;

void reset_map_and_dev_attributes(void) {
    g_map_has_skullrot = false;
    g_map_skullrot = 0.0f;
    g_map_is_space = false;
    g_dev_has_drawcurve = false;
    g_dev_drawcurve = false;
    g_dev_has_drawtunnels = false;
    g_dev_drawtunnels = false;
    g_dev_has_curvecolor = false;
    snprintf(g_dev_curvecolor, sizeof(g_dev_curvecolor), "0x00FF00");
    g_dev_has_bgcolor = false;
    snprintf(g_dev_bgcolor, sizeof(g_dev_bgcolor), "0x000000");
    g_dev_has_destroyall = false;
    g_dev_destroyall = false;
    g_dev_has_huerot = false;
    g_dev_huerot = 0.0f;
    g_dev_has_treasurefreq = false;
    g_dev_treasurefreq = 1000;
    g_dev_has_partime = false;
    g_dev_partime = 300;
}

CanvasHandleType g_hovered_canvas_handle = CANVAS_HANDLE_NONE;
CanvasHandleType g_drag_canvas_handle    = CANVAS_HANDLE_NONE;
int              g_drag_start_w          = 0;
int              g_drag_start_h          = 0;
int              g_drag_start_mouse_sx   = 0;
int              g_drag_start_mouse_sy   = 0;
float            g_drag_start_radius     = 0.0f;

bool g_canvas_handle_selected[10] = { false };
bool g_snap_canvas_handle_selected[10] = { false };

HWND    g_active_modal_dlg = NULL;
/* Viewport & Navigation State */
/* Performance & History Preferences */
bool g_perf_direct_blit = true;
bool g_perf_hud_cache = true;
bool g_perf_smooth_scroll = true;
int  g_undo_limit = 64;

bool  g_show_grid      = true;
float g_zoom           = 1.0f;
float g_pan_x          = 0.0f;
float g_pan_y          = 0.0f;
bool  g_is_panning     = false;
POINT g_last_mouse_pos = {0, 0};

float g_canvas_mouse_x       = -1.0f;
float g_canvas_mouse_y       = -1.0f;
bool  g_mouse_inside_canvas  = false;

void free_marquee_snapshot(void) {
    for (int p = 0; p < MAX_PATHS; ++p) {
        if (g_marquee_snapshot[p]) {
            free(g_marquee_snapshot[p]);
            g_marquee_snapshot[p] = NULL;
        }
        g_marquee_snapshot_count[p] = 0;
    }
}

void deselect_all_objects(void) {
    g_frog_selected = false;
    for (int i = 0; i < g_treasure_point_count; ++i) g_treasure_points[i].selected = false;
    for (int i = 0; i < g_cutout_count; ++i) g_cutouts[i].selected = false;
    for (int i = 0; i < g_sprite_count; ++i) g_sprites[i].selected = false;
    for (int i = 0; i < g_bg_alpha_count; ++i) g_bg_alphas[i].selected = false;
    if (g_editing_bgalpha_idx >= 0) {
        g_editing_bgalpha_idx = -1;
        if (!g_placing_bgalpha) g_placing_bgalpha_vector = false;
    }
    if (g_adjusting_overlay_priority) {
        g_adjusting_overlay_priority = false;
        g_editing_prio_idx = -1;
    }
}

int get_active_selected_bgalpha_idx(void) {
    if (!g_mode_overlay) return -1;
    if (g_selected_bgalpha_idx >= 0 && g_selected_bgalpha_idx < g_bg_alpha_count && g_bg_alphas[g_selected_bgalpha_idx].selected) {
        return g_selected_bgalpha_idx;
    }
    for (int i = 0; i < g_bg_alpha_count; ++i) {
        if (g_bg_alphas[i].selected) {
            g_selected_bgalpha_idx = i;
            return i;
        }
    }
    return -1;
}

int get_active_selected_cutout_idx(void) {
    if (!g_mode_overlay) return -1;
    if (g_selected_cutout_idx >= 0 && g_selected_cutout_idx < g_cutout_count && g_cutouts[g_selected_cutout_idx].selected) {
        return g_selected_cutout_idx;
    }
    for (int i = 0; i < g_cutout_count; ++i) {
        if (g_cutouts[i].selected) {
            g_selected_cutout_idx = i;
            return i;
        }
    }
    return -1;
}

int get_active_selected_sprite_idx(void) {
    if (!g_mode_overlay) return -1;
    if (g_selected_sprite_idx >= 0 && g_selected_sprite_idx < g_sprite_count && g_sprites[g_selected_sprite_idx].selected) {
        return g_selected_sprite_idx;
    }
    for (int i = 0; i < g_sprite_count; ++i) {
        if (g_sprites[i].selected) {
            g_selected_sprite_idx = i;
            return i;
        }
    }
    return -1;
}

void start_overlay_priority_adjustment(HWND hwnd, int type, int idx) {
    if (!g_mode_overlay) return;
    if (type == 0 && (idx < 0 || idx >= g_cutout_count)) return;
    if (type == 1 && (idx < 0 || idx >= g_sprite_count)) return;

    if (g_placing_bgalpha_vector) {
        g_placing_bgalpha_vector = false;
        g_editing_bgalpha_idx = -1;
    }

    g_hovered_cutout_idx = -1;
    g_hovered_sprite_idx = -1;
    g_hovered_bgalpha_idx = -1;

    g_adjusting_overlay_priority = true;
    g_editing_prio_type = type;
    g_editing_prio_idx = idx;

    if (type == 0) {
        g_orig_overlay_priority = g_cutouts[idx].pri;
        g_pending_overlay_priority = g_cutouts[idx].pri;
        g_overlay_active_tab = 0;
        g_selected_cutout_idx = idx;
    } else {
        g_orig_overlay_priority = g_sprites[idx].pri;
        g_pending_overlay_priority = g_sprites[idx].pri;
        g_overlay_active_tab = 1;
        g_selected_sprite_idx = idx;
    }
    g_overlay_current_prio = g_pending_overlay_priority;

    if (g_edit_prio) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", g_pending_overlay_priority);
        g_in_prio_update = true;
        SetWindowTextA(g_edit_prio, buf);
        g_in_prio_update = false;
    }
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
}

void confirm_overlay_priority_adjustment(HWND hwnd) {
    if (!g_adjusting_overlay_priority) return;
    bool changed = (g_pending_overlay_priority != g_orig_overlay_priority);
    if (changed) {
        undo_ensure_baseline();
    }
    if (g_editing_prio_type == 0 && g_editing_prio_idx >= 0 && g_editing_prio_idx < g_cutout_count) {
        g_cutouts[g_editing_prio_idx].pri = g_pending_overlay_priority;
    } else if (g_editing_prio_type == 1 && g_editing_prio_idx >= 0 && g_editing_prio_idx < g_sprite_count) {
        g_sprites[g_editing_prio_idx].pri = g_pending_overlay_priority;
    }
    g_overlay_current_prio = g_pending_overlay_priority;
    g_adjusting_overlay_priority = false;
    g_editing_prio_idx = -1;
    g_hovered_cutout_idx = -1;
    g_hovered_sprite_idx = -1;
    g_hovered_bgalpha_idx = -1;
    if (changed) {
        undo_snapshot_capture("Adjust Overlay Priority");
    }
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
}

typedef struct {
    int type; /* 0 = Cutout, 1 = Sprite, 2 = BgAlpha */
    int idx;
} OverlayHitItem;

bool cycle_overlay_selection_at(HWND hwnd, float cx, float cy) {
    if (!g_mode_overlay || !g_has_canvas) return false;

    OverlayHitItem hits[MAX_CUTOUTS + MAX_SPRITES + MAX_BG_ALPHAS];
    int hit_count = 0;

    /* 1. Check Cutouts in top-to-bottom visual rendering order (highest index to lowest) */
    for (int i = g_cutout_count - 1; i >= 0; --i) {
        int cw = g_cutouts[i].has_img ? g_cutouts[i].img.width : (g_has_default_cutout_img ? g_default_cutout_img.width : 160);
        int ch = g_cutouts[i].has_img ? g_cutouts[i].img.height : (g_has_default_cutout_img ? g_default_cutout_img.height : 140);
        if (cx >= (float)g_cutouts[i].x && cx <= (float)(g_cutouts[i].x + cw) &&
            cy >= (float)g_cutouts[i].y && cy <= (float)(g_cutouts[i].y + ch)) {
            hits[hit_count].type = 0;
            hits[hit_count].idx = i;
            hit_count++;
        }
    }

    /* 2. Check Sprites in top-to-bottom visual rendering order */
    for (int i = g_sprite_count - 1; i >= 0; --i) {
        int sw = g_sprites[i].has_img ? g_sprites[i].img.width : (g_has_default_sprite_img ? g_default_sprite_img.width : 120);
        int sh = g_sprites[i].has_img ? g_sprites[i].img.height : (g_has_default_sprite_img ? g_default_sprite_img.height : 120);
        if (cx >= (float)g_sprites[i].x && cx <= (float)(g_sprites[i].x + sw) &&
            cy >= (float)g_sprites[i].y && cy <= (float)(g_sprites[i].y + sh)) {
            hits[hit_count].type = 1;
            hits[hit_count].idx = i;
            hit_count++;
        }
    }

    /* 3. Check Background Alphas in top-to-bottom visual rendering order */
    for (int i = g_bg_alpha_count - 1; i >= 0; --i) {
        int bw = g_bg_alphas[i].has_img ? g_bg_alphas[i].img.width : (g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 160);
        int bh = g_bg_alphas[i].has_img ? g_bg_alphas[i].img.height : (g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 140);
        if (cx >= (float)g_bg_alphas[i].x && cx <= (float)(g_bg_alphas[i].x + bw) &&
            cy >= (float)g_bg_alphas[i].y && cy <= (float)(g_bg_alphas[i].y + bh)) {
            hits[hit_count].type = 2;
            hits[hit_count].idx = i;
            hit_count++;
        }
    }

    if (hit_count == 0) {
        deselect_all_objects();
        if (hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, NULL, FALSE);
        update_status_bar();
        return false;
    }

    /* Find currently selected item among hits under cursor */
    int cur_hit_idx = -1;
    for (int h = 0; h < hit_count; ++h) {
        bool is_sel = false;
        if (hits[h].type == 0 && g_cutouts[hits[h].idx].selected) is_sel = true;
        else if (hits[h].type == 1 && g_sprites[hits[h].idx].selected) is_sel = true;
        else if (hits[h].type == 2 && g_bg_alphas[hits[h].idx].selected) is_sel = true;
        if (is_sel) {
            cur_hit_idx = h;
            break;
        }
    }

    /* Select topmost (layer 5) if none selected, or cycle to next layer underneath (wraps to top) */
    int target_h = (cur_hit_idx == -1) ? 0 : ((cur_hit_idx + 1) % hit_count);

    deselect_all_objects();

    int t = hits[target_h].type;
    int idx = hits[target_h].idx;

    if (t == 0) {
        g_cutouts[idx].selected = true;
        g_selected_cutout_idx = idx;
        g_overlay_active_tab = 0;
        g_overlay_current_prio = g_cutouts[idx].pri;
        g_hovered_cutout_idx = idx;
        g_hovered_sprite_idx = -1;
        g_hovered_bgalpha_idx = -1;
    } else if (t == 1) {
        g_sprites[idx].selected = true;
        g_selected_sprite_idx = idx;
        g_overlay_active_tab = 1;
        g_overlay_current_prio = g_sprites[idx].pri;
        g_hovered_sprite_idx = idx;
        g_hovered_cutout_idx = -1;
        g_hovered_bgalpha_idx = -1;
    } else if (t == 2) {
        g_bg_alphas[idx].selected = true;
        g_selected_bgalpha_idx = idx;
        g_overlay_active_tab = 2;
        g_overlay_current_prio = g_bg_alphas[idx].pri;
        g_hovered_bgalpha_idx = idx;
        g_hovered_cutout_idx = -1;
        g_hovered_sprite_idx = -1;
    }

    if (g_edit_prio && IsWindow(g_edit_prio)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", g_overlay_current_prio);
        g_in_prio_update = true;
        SetWindowTextA(g_edit_prio, buf);
        g_in_prio_update = false;
    }

    if (hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
    return true;
}

void cancel_overlay_priority_adjustment(HWND hwnd) {
    if (!g_adjusting_overlay_priority) return;
    if (g_editing_prio_type == 0 && g_editing_prio_idx >= 0 && g_editing_prio_idx < g_cutout_count) {
        g_cutouts[g_editing_prio_idx].pri = g_orig_overlay_priority;
    } else if (g_editing_prio_type == 1 && g_editing_prio_idx >= 0 && g_editing_prio_idx < g_sprite_count) {
        g_sprites[g_editing_prio_idx].pri = g_orig_overlay_priority;
    }
    g_overlay_current_prio = g_orig_overlay_priority;
    if (g_edit_prio) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", g_orig_overlay_priority);
        g_in_prio_update = true;
        SetWindowTextA(g_edit_prio, buf);
        g_in_prio_update = false;
    }
    g_adjusting_overlay_priority = false;
    g_editing_prio_idx = -1;
    g_hovered_cutout_idx = -1;
    g_hovered_sprite_idx = -1;
    g_hovered_bgalpha_idx = -1;
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
}

void toggle_overlay_variable_mode(HWND hwnd) {
    if (!g_mode_overlay) return;

    if (g_adjusting_overlay_priority) {
        confirm_overlay_priority_adjustment(hwnd);
        return;
    }

    if (g_placing_bgalpha_vector) {
        g_placing_bgalpha_vector = false;
        g_editing_bgalpha_idx = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        update_status_bar();
        return;
    }

    int sel_ba = get_active_selected_bgalpha_idx();
    int sel_co = get_active_selected_cutout_idx();
    int sel_sp = get_active_selected_sprite_idx();

    if (sel_ba < 0 && sel_co < 0 && sel_sp < 0) {
        if (g_overlay_active_tab == 2 && g_bg_alpha_count == 1) {
            deselect_all_objects();
            g_bg_alphas[0].selected = true;
            g_selected_bgalpha_idx = 0;
            sel_ba = 0;
        } else if (g_overlay_active_tab == 0 && g_cutout_count == 1) {
            deselect_all_objects();
            g_cutouts[0].selected = true;
            g_selected_cutout_idx = 0;
            sel_co = 0;
        } else if (g_overlay_active_tab == 1 && g_sprite_count == 1) {
            deselect_all_objects();
            g_sprites[0].selected = true;
            g_selected_sprite_idx = 0;
            sel_sp = 0;
        } else if (g_bg_alpha_count == 1 && g_cutout_count == 0 && g_sprite_count == 0) {
            deselect_all_objects();
            g_bg_alphas[0].selected = true;
            g_selected_bgalpha_idx = 0;
            sel_ba = 0;
        } else if (g_cutout_count == 1 && g_bg_alpha_count == 0 && g_sprite_count == 0) {
            deselect_all_objects();
            g_cutouts[0].selected = true;
            g_selected_cutout_idx = 0;
            sel_co = 0;
        } else if (g_sprite_count == 1 && g_bg_alpha_count == 0 && g_cutout_count == 0) {
            deselect_all_objects();
            g_sprites[0].selected = true;
            g_selected_sprite_idx = 0;
            sel_sp = 0;
        }
    }

    if (sel_ba >= 0 && sel_ba < g_bg_alpha_count) {
        g_editing_bgalpha_idx = sel_ba;
        LevelBackgroundAlpha *ba = &g_bg_alphas[sel_ba];
        int img_w = ba->has_img ? ba->img.width : (g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 160);
        int img_h = ba->has_img ? ba->img.height : (g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 140);
        g_bgalpha_anchor_x = ba->x + img_w / 2;
        g_bgalpha_anchor_y = ba->y + img_h / 2;
        g_bgalpha_pending_vx = ba->vx;
        g_bgalpha_pending_vy = ba->vy;
        g_placing_bgalpha = false;
        g_placing_bgalpha_vector = true;
        InvalidateRect(hwnd, NULL, FALSE);
        update_status_bar();
    } else if (sel_co >= 0 && sel_co < g_cutout_count) {
        start_overlay_priority_adjustment(hwnd, 0, sel_co);
    } else if (sel_sp >= 0 && sel_sp < g_sprite_count) {
        start_overlay_priority_adjustment(hwnd, 1, sel_sp);
    }
}

int get_selected_object_count(void) {
    int cnt = 0;
    if (g_mode_overlay) {
        for (int i = 0; i < g_cutout_count; ++i) if (g_cutouts[i].selected) cnt++;
        for (int i = 0; i < g_sprite_count; ++i) if (g_sprites[i].selected) cnt++;
        for (int i = 0; i < g_bg_alpha_count; ++i) if (g_bg_alphas[i].selected) cnt++;
    } else if (!g_mode_curve) {
        if (g_has_frog && g_frog_selected) cnt++;
        for (int i = 0; i < g_treasure_point_count; ++i) if (g_treasure_points[i].selected) cnt++;
    }
    return cnt;
}

void snapshot_object_selection(void) {
    g_snap_frog_selected = g_frog_selected;
    for (int i = 0; i < g_treasure_point_count; ++i) g_snap_coin_selected[i] = g_treasure_points[i].selected;
    for (int i = 0; i < g_cutout_count; ++i) g_snap_cutout_selected[i] = g_cutouts[i].selected;
    for (int i = 0; i < g_sprite_count; ++i) g_snap_sprite_selected[i] = g_sprites[i].selected;
    for (int i = 0; i < g_bg_alpha_count; ++i) g_snap_bgalpha_selected[i] = g_bg_alphas[i].selected;
}

bool is_system_dark_mode(void) {
#if defined(_WIN32) && !defined(__APPLE__)
    HKEY hKey;
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
        RegCloseKey(hKey);
    }
    return (value == 0);
#else
    return false;
#endif
}

const ThemeColors *get_theme(void) {
    return (g_current_theme == THEME_DARK) ? &g_theme_dark : &g_theme_light;
}

void draw_menu_nc_bottom_line(HWND hWnd, COLORREF bg_color) {
    MENUBARINFO mbi;
    memset(&mbi, 0, sizeof(mbi));
    mbi.cbSize = sizeof(mbi);
    if (!GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi)) return;

    RECT rcClient = { 0 };
    GetClientRect(hWnd, &rcClient);
    MapWindowPoints(hWnd, NULL, (POINT*)&rcClient, 2);

    RECT rcWindow = { 0 };
    GetWindowRect(hWnd, &rcWindow);

    OffsetRect(&rcClient, -rcWindow.left, -rcWindow.top);

    RECT rcLine = rcClient;
    rcLine.bottom = rcLine.top;
    rcLine.top--;

    HDC hdc = GetWindowDC(hWnd);
    if (hdc) {
        HBRUSH br = CreateSolidBrush(bg_color);
        FillRect(hdc, &rcLine, br);
        DeleteObject(br);
        ReleaseDC(hWnd, hdc);
    }
}

#if defined(_WIN32) && !defined(__APPLE__)
void update_window_theme_attributes(HWND hwnd, bool is_dark) {
    BOOL dwm_dark = is_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dwm_dark, sizeof(dwm_dark));
    DwmSetWindowAttribute(hwnd, 19, &dwm_dark, sizeof(dwm_dark));

    HMODULE hUxTheme = GetModuleHandleA("uxtheme.dll");
    if (!hUxTheme) hUxTheme = LoadLibraryA("uxtheme.dll");
    if (hUxTheme) {
        typedef BOOL (WINAPI *pfnAllowDarkModeForWindow)(HWND, BOOL);
        pfnAllowDarkModeForWindow pAllowDarkModeForWindow = (pfnAllowDarkModeForWindow)(void*)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));
        if (pAllowDarkModeForWindow) {
            pAllowDarkModeForWindow(hwnd, dwm_dark);
        }

        pfnSetPreferredAppMode pSetPreferredAppMode = (pfnSetPreferredAppMode)(void*)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
        pfnFlushMenuThemes pFlushMenuThemes = (pfnFlushMenuThemes)(void*)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136));
        if (pSetPreferredAppMode) {
            pSetPreferredAppMode(is_dark ? AppMode_ForceDark : AppMode_ForceLight);
        }
        if (pFlushMenuThemes) {
            pFlushMenuThemes();
        }
    }

    DrawMenuBar(hwnd);
}

void apply_control_dark_mode(HWND hwnd_ctrl, bool is_dark) {
    if (!hwnd_ctrl) return;
    HMODULE hUxTheme = GetModuleHandleA("uxtheme.dll");
    if (!hUxTheme) hUxTheme = LoadLibraryA("uxtheme.dll");
    if (hUxTheme) {
        typedef BOOL (WINAPI *pfnAllowDarkModeForWindow)(HWND, BOOL);
        pfnAllowDarkModeForWindow pAllowDarkModeForWindow = (pfnAllowDarkModeForWindow)(void*)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));
        if (pAllowDarkModeForWindow) {
            pAllowDarkModeForWindow(hwnd_ctrl, is_dark ? TRUE : FALSE);
        }
    }
    SetWindowTheme(hwnd_ctrl, is_dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
}

void load_frog_sprite(void) {
    if (g_has_frog_img) return;

    /* 1. Try embedded resource (IDR_SPRITE_FROG) */
    if (image_load_from_resource(IDR_SPRITE_FROG, &g_frog_img)) {
        g_has_frog_img = true;
        return;
    }

    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *p1 = strrchr(exe_dir, '\\');
    char *p2 = strrchr(exe_dir, '/');
    char *last_slash = (p2 && (!p1 || p2 > p1)) ? p2 : p1;
    if (last_slash) *(last_slash + 1) = '\0';

    char frog_path[MAX_PATH];
    snprintf(frog_path, sizeof(frog_path), "%sassets\\media\\SMALLFROGonPAD.png", exe_dir);

    if (image_load_from_file(frog_path, &g_frog_img)) {
        g_has_frog_img = true;
        return;
    }

    if (image_load_from_file("assets\\media\\SMALLFROGonPAD.png", &g_frog_img)) {
        g_has_frog_img = true;
        return;
    }

    if (image_load_from_file("D:\\Files\\Project\\Reverse Engineer\\ZuPather - Zuma Deluxe Path Maker\\assets\\media\\SMALLFROGonPAD.png", &g_frog_img)) {
        g_has_frog_img = true;
        return;
    }
}

void load_coin_sprite(void) {
    if (g_has_coin_img) return;

    /* 1. Try embedded resource (IDR_SPRITE_COIN) */
    if (image_load_from_resource(IDR_SPRITE_COIN, &g_coin_img)) {
        g_has_coin_img = true;
        return;
    }

    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *p1 = strrchr(exe_dir, '\\');
    char *p2 = strrchr(exe_dir, '/');
    char *last_slash = (p2 && (!p1 || p2 > p1)) ? p2 : p1;
    if (last_slash) *(last_slash + 1) = '\0';

    char coin_path[MAX_PATH];
    snprintf(coin_path, sizeof(coin_path), "%sassets\\media\\COIN.png", exe_dir);

    if (image_load_from_file(coin_path, &g_coin_img)) {
        g_has_coin_img = true;
        return;
    }

    if (image_load_from_file("assets\\media\\COIN.png", &g_coin_img)) {
        g_has_coin_img = true;
        return;
    }

    if (image_load_from_file("D:\\Files\\Project\\Reverse Engineer\\ZuPather - Zuma Deluxe Path Maker\\assets\\media\\COIN.png", &g_coin_img)) {
        g_has_coin_img = true;
        return;
    }
}

void free_cutout(LevelCutout *co) {
    if (!co) return;
    if (co->has_img) {
        image_free(&co->img);
        co->has_img = false;
    }
    if (co->hdc_mask_light) { DeleteDC(co->hdc_mask_light); co->hdc_mask_light = NULL; }
    if (co->hbm_mask_light) { DeleteObject(co->hbm_mask_light); co->hbm_mask_light = NULL; }
    if (co->hdc_mask_dark)  { DeleteDC(co->hdc_mask_dark);  co->hdc_mask_dark = NULL; }
    if (co->hbm_mask_dark)  { DeleteObject(co->hbm_mask_dark);  co->hbm_mask_dark = NULL; }
}

void free_sprite(LevelSprite *sp) {
    if (!sp) return;
    if (sp->has_img) {
        image_free(&sp->img);
        sp->has_img = false;
    }
}

void free_bgalpha(LevelBackgroundAlpha *ba) {
    if (!ba) return;
    if (ba->hdc_mask_light) { DeleteDC(ba->hdc_mask_light); ba->hdc_mask_light = NULL; }
    if (ba->hbm_mask_light) { DeleteObject(ba->hbm_mask_light); ba->hbm_mask_light = NULL; }
    if (ba->hdc_mask_dark)  { DeleteDC(ba->hdc_mask_dark);  ba->hdc_mask_dark = NULL; }
    if (ba->hbm_mask_dark)  { DeleteObject(ba->hbm_mask_dark);  ba->hbm_mask_dark = NULL; }
    if (ba->has_img) {
        image_free(&ba->img);
        ba->has_img = false;
    }
}

void sync_cutout_image_names(void) {
    for (int i = 0; i < g_cutout_count; ++i) {
        if (g_cutout_count == 1) {
            snprintf(g_cutouts[i].image_name, sizeof(g_cutouts[i].image_name), "tunnel");
        } else {
            snprintf(g_cutouts[i].image_name, sizeof(g_cutouts[i].image_name), "tunnel%d", i + 1);
        }
    }
}

void sync_sprite_image_names(void) {
    for (int i = 0; i < g_sprite_count; ++i) {
        if (g_sprite_count == 1) {
            snprintf(g_sprites[i].image_name, sizeof(g_sprites[i].image_name), "sprite");
        } else {
            snprintf(g_sprites[i].image_name, sizeof(g_sprites[i].image_name), "sprite%d", i + 1);
        }
    }
}

void sync_bgalpha_image_names(void) {
    for (int i = 0; i < g_bg_alpha_count; ++i) {
        if (g_bg_alpha_count == 1) {
            snprintf(g_bg_alphas[i].image_name, sizeof(g_bg_alphas[i].image_name), "quake");
        } else {
            snprintf(g_bg_alphas[i].image_name, sizeof(g_bg_alphas[i].image_name), "quake%d", i + 1);
        }
    }
}

void free_pending_cutout(void) {
    if (g_has_pending_cutout_img) {
        image_free(&g_pending_cutout_img);
        g_has_pending_cutout_img = false;
    }
    if (g_pending_cutout_hdc_light)  { DeleteDC(g_pending_cutout_hdc_light); g_pending_cutout_hdc_light = NULL; }
    if (g_pending_cutout_mask_light) { DeleteObject(g_pending_cutout_mask_light); g_pending_cutout_mask_light = NULL; }
    if (g_pending_cutout_hdc_dark)   { DeleteDC(g_pending_cutout_hdc_dark); g_pending_cutout_hdc_dark = NULL; }
    if (g_pending_cutout_mask_dark)  { DeleteObject(g_pending_cutout_mask_dark); g_pending_cutout_mask_dark = NULL; }
    g_pending_cutout_filepath[0] = '\0';
}

void free_pending_sprite(void) {
    if (g_has_pending_sprite_img) {
        image_free(&g_pending_sprite_img);
        g_has_pending_sprite_img = false;
    }
    g_pending_sprite_filepath[0] = '\0';
}

void free_pending_bgalpha(void) {
    if (g_has_pending_bgalpha_img) {
        image_free(&g_pending_bgalpha_img);
        g_has_pending_bgalpha_img = false;
    }
    g_pending_bgalpha_filepath[0] = '\0';
}

void generate_cutout_masks_internal(
    HBITMAP src_hbm, int w, int h,
    HBITMAP *out_hbm_light, HDC *out_hdc_light,
    HBITMAP *out_hbm_dark, HDC *out_hdc_dark
) {
    if (!src_hbm || w <= 0 || h <= 0) return;

    HDC hdc_screen = GetDC(NULL);

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* Top-down DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    size_t num_pixels = (size_t)w * (size_t)h;
    uint32_t *src_pixels = (uint32_t *)malloc(num_pixels * sizeof(uint32_t));
    uint32_t *dst_light = (uint32_t *)malloc(num_pixels * sizeof(uint32_t));
    uint32_t *dst_dark = (uint32_t *)malloc(num_pixels * sizeof(uint32_t));

    if (!src_pixels || !dst_light || !dst_dark) {
        if (src_pixels) free(src_pixels);
        if (dst_light) free(dst_light);
        if (dst_dark) free(dst_dark);
        ReleaseDC(NULL, hdc_screen);
        return;
    }

    GetDIBits(hdc_screen, src_hbm, 0, h, src_pixels, &bmi, DIB_RGB_COLORS);

    for (size_t i = 0; i < num_pixels; ++i) {
        uint32_t px = src_pixels[i];
        BYTE b = px & 0xFF;
        BYTE g = (px >> 8) & 0xFF;
        BYTE r = (px >> 16) & 0xFF;
        BYTE a = (px >> 24) & 0xFF;

        if (a == 0) {
            dst_light[i] = 0;
            dst_dark[i] = 0;
            continue;
        }

        int lum = (r * 299 + g * 587 + b * 114) / 1000;
        if (lum <= 15) {
            /* Black color becomes 100% transparent / removed */
            dst_light[i] = 0;
            dst_dark[i] = 0;
        } else {
            /* Whiteness factor: 1.0 for pure white (lum=240..255), 0.0 for black (lum<=15) */
            float k = (float)(lum - 15) / 225.0f;
            if (k > 1.0f) k = 1.0f;
            /* 25% opacity (alpha=64) for pure white */
            BYTE out_a = (BYTE)roundf(64.0f * k * ((float)a / 255.0f));
            if (out_a == 0) {
                dst_light[i] = 0;
                dst_dark[i] = 0;
            } else {
                /* Light Mode: transformed to Black with 25% opacity (PBGRA premultiplied) */
                dst_light[i] = ((uint32_t)out_a << 24);
                /* Dark Mode: transformed to White with 25% opacity (PBGRA premultiplied) */
                dst_dark[i] = ((uint32_t)out_a << 24) | ((uint32_t)out_a << 16) | ((uint32_t)out_a << 8) | out_a;
            }
        }
    }

    void *bits_light = NULL;
    *out_hbm_light = CreateDIBSection(hdc_screen, &bmi, DIB_RGB_COLORS, &bits_light, NULL, 0);
    if (*out_hbm_light && bits_light) {
        memcpy(bits_light, dst_light, num_pixels * sizeof(uint32_t));
        *out_hdc_light = CreateCompatibleDC(hdc_screen);
        SelectObject(*out_hdc_light, *out_hbm_light);
    }

    void *bits_dark = NULL;
    *out_hbm_dark = CreateDIBSection(hdc_screen, &bmi, DIB_RGB_COLORS, &bits_dark, NULL, 0);
    if (*out_hbm_dark && bits_dark) {
        memcpy(bits_dark, dst_dark, num_pixels * sizeof(uint32_t));
        *out_hdc_dark = CreateCompatibleDC(hdc_screen);
        SelectObject(*out_hdc_dark, *out_hbm_dark);
    }

    free(src_pixels);
    free(dst_light);
    free(dst_dark);
    ReleaseDC(NULL, hdc_screen);
}

void cutout_generate_masks(LevelCutout *co) {
    if (!co) return;
    if (co->hdc_mask_light) { DeleteDC(co->hdc_mask_light); co->hdc_mask_light = NULL; }
    if (co->hbm_mask_light) { DeleteObject(co->hbm_mask_light); co->hbm_mask_light = NULL; }
    if (co->hdc_mask_dark)  { DeleteDC(co->hdc_mask_dark);  co->hdc_mask_dark = NULL; }
    if (co->hbm_mask_dark)  { DeleteObject(co->hbm_mask_dark);  co->hbm_mask_dark = NULL; }

    HBITMAP src_hbm = co->has_img ? co->img.hbm : (g_has_default_cutout_img ? g_default_cutout_img.hbm : NULL);
    int w = co->has_img ? co->img.width : (g_has_default_cutout_img ? g_default_cutout_img.width : 0);
    int h = co->has_img ? co->img.height : (g_has_default_cutout_img ? g_default_cutout_img.height : 0);

    generate_cutout_masks_internal(src_hbm, w, h,
                                   &co->hbm_mask_light, &co->hdc_mask_light,
                                   &co->hbm_mask_dark, &co->hdc_mask_dark);
}

void generate_bgalpha_masks_internal(
    HBITMAP src_hbm, int w, int h,
    HBITMAP *out_hbm_light, HDC *out_hdc_light,
    HBITMAP *out_hbm_dark, HDC *out_hdc_dark
) {
    if (!src_hbm || w <= 0 || h <= 0) return;

    HDC hdc_screen = GetDC(NULL);

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* Top-down DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    size_t num_pixels = (size_t)w * (size_t)h;
    uint32_t *src_pixels = (uint32_t *)malloc(num_pixels * sizeof(uint32_t));
    uint32_t *dst_light = (uint32_t *)malloc(num_pixels * sizeof(uint32_t));
    uint32_t *dst_dark = (uint32_t *)malloc(num_pixels * sizeof(uint32_t));

    if (!src_pixels || !dst_light || !dst_dark) {
        if (src_pixels) free(src_pixels);
        if (dst_light) free(dst_light);
        if (dst_dark) free(dst_dark);
        ReleaseDC(NULL, hdc_screen);
        return;
    }

    GetDIBits(hdc_screen, src_hbm, 0, h, src_pixels, &bmi, DIB_RGB_COLORS);

    for (size_t i = 0; i < num_pixels; ++i) {
        uint32_t px = src_pixels[i];
        BYTE b = px & 0xFF;
        BYTE g = (px >> 8) & 0xFF;
        BYTE r = (px >> 16) & 0xFF;
        BYTE a = (px >> 24) & 0xFF;

        if (a == 0) {
            dst_light[i] = 0;
            dst_dark[i] = 0;
            continue;
        }

        /* 25% opacity Red = roundf(255 * 0.25) = 64 (0x40) */
        int lum = (r * 299 + g * 587 + b * 114) / 1000;
        float factor = (float)a / 255.0f;
        if (a == 255 && lum < 250) {
            factor = (float)lum / 255.0f;
        }
        BYTE out_a = (BYTE)roundf(64.0f * factor);
        if (out_a == 0) {
            dst_light[i] = 0;
            dst_dark[i] = 0;
        } else {
            /* Red with 25% opacity in both modes (PBGRA premultiplied: A=out_a, R=out_a, G=0, B=0) */
            uint32_t red_px = ((uint32_t)out_a << 24) | ((uint32_t)out_a << 16);
            dst_light[i] = red_px;
            dst_dark[i]  = red_px;
        }
    }

    void *bits_light = NULL;
    *out_hbm_light = CreateDIBSection(hdc_screen, &bmi, DIB_RGB_COLORS, &bits_light, NULL, 0);
    if (*out_hbm_light && bits_light) {
        memcpy(bits_light, dst_light, num_pixels * sizeof(uint32_t));
        *out_hdc_light = CreateCompatibleDC(hdc_screen);
        SelectObject(*out_hdc_light, *out_hbm_light);
    }

    void *bits_dark = NULL;
    *out_hbm_dark = CreateDIBSection(hdc_screen, &bmi, DIB_RGB_COLORS, &bits_dark, NULL, 0);
    if (*out_hbm_dark && bits_dark) {
        memcpy(bits_dark, dst_dark, num_pixels * sizeof(uint32_t));
        *out_hdc_dark = CreateCompatibleDC(hdc_screen);
        SelectObject(*out_hdc_dark, *out_hbm_dark);
    }

    free(src_pixels);
    free(dst_light);
    free(dst_dark);
    ReleaseDC(NULL, hdc_screen);
}

void bgalpha_generate_masks(LevelBackgroundAlpha *ba) {
    if (!ba) return;
    if (ba->hdc_mask_light) { DeleteDC(ba->hdc_mask_light); ba->hdc_mask_light = NULL; }
    if (ba->hbm_mask_light) { DeleteObject(ba->hbm_mask_light); ba->hbm_mask_light = NULL; }
    if (ba->hdc_mask_dark)  { DeleteDC(ba->hdc_mask_dark);  ba->hdc_mask_dark = NULL; }
    if (ba->hbm_mask_dark)  { DeleteObject(ba->hbm_mask_dark);  ba->hbm_mask_dark = NULL; }

    HBITMAP src_hbm = ba->has_img ? ba->img.hbm : (g_has_default_bgalpha_img ? g_default_bgalpha_img.hbm : NULL);
    int w = ba->has_img ? ba->img.width : (g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 0);
    int h = ba->has_img ? ba->img.height : (g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 0);

    generate_bgalpha_masks_internal(src_hbm, w, h,
                                    &ba->hbm_mask_light, &ba->hdc_mask_light,
                                    &ba->hbm_mask_dark, &ba->hdc_mask_dark);
}

void load_default_cutout_sprite(void) {
    if (g_has_default_cutout_img) return;

    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *p1 = strrchr(exe_dir, '\\');
    char *p2 = strrchr(exe_dir, '/');
    char *last_slash = (p2 && (!p1 || p2 > p1)) ? p2 : p1;
    if (last_slash) *(last_slash + 1) = '\0';

    char cutout_path[MAX_PATH];
    snprintf(cutout_path, sizeof(cutout_path), "%sassets\\media\\CUTOUT.png", exe_dir);

    bool loaded = image_load_from_file(cutout_path, &g_default_cutout_img);
    if (!loaded) {
        loaded = image_load_from_file("assets\\media\\CUTOUT.png", &g_default_cutout_img);
    }
    if (!loaded) {
        loaded = image_load_from_file("D:\\Files\\Project\\Reverse Engineer\\ZuPather - Zuma Deluxe Path Maker\\assets\\media\\CUTOUT.png", &g_default_cutout_img);
    }

    if (loaded) {
        g_has_default_cutout_img = true;
        generate_cutout_masks_internal(
            g_default_cutout_img.hbm, g_default_cutout_img.width, g_default_cutout_img.height,
            &g_default_cutout_mask_light, &g_default_cutout_hdc_light,
            &g_default_cutout_mask_dark, &g_default_cutout_hdc_dark
        );
    }
}

void load_default_sprite_sprite(void) {
    if (g_has_default_sprite_img) return;

    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *p1 = strrchr(exe_dir, '\\');
    char *p2 = strrchr(exe_dir, '/');
    char *last_slash = (p2 && (!p1 || p2 > p1)) ? p2 : p1;
    if (last_slash) *(last_slash + 1) = '\0';

    char sprite_path[MAX_PATH];
    snprintf(sprite_path, sizeof(sprite_path), "%sassets\\media\\SPRITE.png", exe_dir);

    if (image_load_from_file(sprite_path, &g_default_sprite_img)) {
        g_has_default_sprite_img = true;
        return;
    }
    if (image_load_from_file("assets\\media\\SPRITE.png", &g_default_sprite_img)) {
        g_has_default_sprite_img = true;
        return;
    }
    if (image_load_from_file("D:\\Files\\Project\\Reverse Engineer\\ZuPather - Zuma Deluxe Path Maker\\assets\\media\\SPRITE.png", &g_default_sprite_img)) {
        g_has_default_sprite_img = true;
        return;
    }
}

void load_default_bgalpha_sprite(void) {
    if (g_has_default_bgalpha_img) return;

    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *p1 = strrchr(exe_dir, '\\');
    char *p2 = strrchr(exe_dir, '/');
    char *last_slash = (p2 && (!p1 || p2 > p1)) ? p2 : p1;
    if (last_slash) *(last_slash + 1) = '\0';

    char bgalpha_path[MAX_PATH];
    snprintf(bgalpha_path, sizeof(bgalpha_path), "%sassets\\media\\BGALPHA.png", exe_dir);

    bool loaded = false;
    if (image_load_from_file(bgalpha_path, &g_default_bgalpha_img)) {
        g_has_default_bgalpha_img = true;
        loaded = true;
    } else if (image_load_from_file("assets\\media\\BGALPHA.png", &g_default_bgalpha_img)) {
        g_has_default_bgalpha_img = true;
        loaded = true;
    }
    if (loaded && g_default_bgalpha_img.hbm && !g_default_bgalpha_hdc_light) {
        generate_bgalpha_masks_internal(
            g_default_bgalpha_img.hbm, g_default_bgalpha_img.width, g_default_bgalpha_img.height,
            &g_default_bgalpha_mask_light, &g_default_bgalpha_hdc_light,
            &g_default_bgalpha_mask_dark, &g_default_bgalpha_hdc_dark
        );
    }
}

void load_level_preview_assets(void) {
    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *p1 = strrchr(exe_dir, '\\');
    char *p2 = strrchr(exe_dir, '/');
    char *last_slash = (p2 && (!p1 || p2 > p1)) ? p2 : p1;
    if (last_slash) *(last_slash + 1) = '\0';

    static const char *s_ball_files[LEVEL_PREVIEW_BALL_COLORS] = {
        "baBallBlue.png",
        "baBallYellow.png",
        "baBallRed.png",
        "baBallGreen.png",
        "baBallPurple.png",
        "baBallWhite.png",
        "baBallOrange.png",
        "baBallNavy.png",
        "baBallCyan.png",
        "baBallBlack.png",
        "baBallCream.png"
    };

    for (int i = 0; i < LEVEL_PREVIEW_BALL_COLORS; ++i) {
        if (!g_has_ball_imgs[i]) {
            /* 1. Try embedded resource (IDR_SPRITE_BALL_BASE + i) */
            if (image_load_from_resource(IDR_SPRITE_BALL_BASE + i, &g_ball_imgs[i])) {
                g_has_ball_imgs[i] = true;
                continue;
            }

            char path_buf[MAX_PATH];
            snprintf(path_buf, sizeof(path_buf), "%sassets\\media\\%s", exe_dir, s_ball_files[i]);
            if (image_load_from_file(path_buf, &g_ball_imgs[i])) {
                g_has_ball_imgs[i] = true;
            } else {
                snprintf(path_buf, sizeof(path_buf), "assets\\media\\%s", s_ball_files[i]);
                if (image_load_from_file(path_buf, &g_ball_imgs[i])) {
                    g_has_ball_imgs[i] = true;
                } else {
                    snprintf(path_buf, sizeof(path_buf), "D:\\Files\\Project\\Reverse Engineer\\ZuPather - Zuma Deluxe Path Maker\\assets\\media\\%s", s_ball_files[i]);
                    if (image_load_from_file(path_buf, &g_ball_imgs[i])) {
                        g_has_ball_imgs[i] = true;
                    }
                }
            }
        }
    }

    if (!g_has_skull_img) {
        /* 1. Try embedded resource (IDR_SPRITE_SKULL) */
        if (image_load_from_resource(IDR_SPRITE_SKULL, &g_skull_img)) {
            g_has_skull_img = true;
        } else {
            char skull_path[MAX_PATH];
            snprintf(skull_path, sizeof(skull_path), "%sassets\\media\\SKULL.png", exe_dir);
            if (image_load_from_file(skull_path, &g_skull_img)) {
                g_has_skull_img = true;
            } else if (image_load_from_file("assets\\media\\SKULL.png", &g_skull_img)) {
                g_has_skull_img = true;
            } else if (image_load_from_file("D:\\Files\\Project\\Reverse Engineer\\ZuPather - Zuma Deluxe Path Maker\\assets\\media\\SKULL.png", &g_skull_img)) {
                g_has_skull_img = true;
            }
        }
    }
}

void enter_preview_mode(HWND hwnd) {
    g_mode_canvas = false;
    g_mode_element = false;
    g_mode_curve = false;
    g_mode_overlay = false;
    g_placing_frog = false;
    g_placing_coin = false;
    g_placing_cutout = false;
    g_placing_sprite = false;
    g_placing_bgalpha = false;
    g_placing_bgalpha_vector = false;
    g_picked_up_cutout = false;
    g_picked_up_sprite = false;
    g_picked_up_bgalpha = false;
    g_picked_up_coin = false;
    g_adjusting_overlay_priority = false;

    if (g_edit_cv_w) ShowWindow(g_edit_cv_w, SW_HIDE);
    if (g_edit_cv_h) ShowWindow(g_edit_cv_h, SW_HIDE);
    if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);

    deselect_all_objects();
    deselect_all_canvas_handles();

    HMENU hmenu = GetMenu(hwnd);
    if (hmenu) {
        CheckMenuItem(hmenu, ID_MENU_CANVAS_MODE, MF_UNCHECKED);
        CheckMenuItem(hmenu, ID_MENU_CANVAS_ELEMENT, MF_UNCHECKED);
        CheckMenuItem(hmenu, ID_MENU_CANVAS_CURVE, MF_UNCHECKED);
        CheckMenuItem(hmenu, ID_MENU_CANVAS_OVERLAY, MF_UNCHECKED);
    }
    refresh_toolbar_buttons();
    sync_level_preview_state();
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
}

void sync_level_preview_state(void) {
    bool is_preview_mode = (!g_mode_canvas && !g_mode_element && !g_mode_curve && !g_mode_overlay);
    if (is_preview_mode) {
        g_level_preview_active = g_level_preview_stored_state;
    } else {
        g_level_preview_active = false;
    }
}

void toggle_level_preview(HWND hwnd) {
    if (!g_has_canvas) return;
    g_level_preview_stored_state = !g_level_preview_stored_state;
    bool is_preview_mode = (!g_mode_canvas && !g_mode_element && !g_mode_curve && !g_mode_overlay);
    if (g_level_preview_stored_state && !is_preview_mode) {
        enter_preview_mode(hwnd);
    }
    sync_level_preview_state();
    if (g_level_preview_active) {
        g_preview_anim_distance = 0.0f;
    }
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
}

void set_canvas_title_sanitized(const char *raw_title) {
    if (!raw_title) return;
    char clean[MAX_PATH] = {0};
    int j = 0;
    for (int i = 0; raw_title[i] != '\0' && j < 63; ++i) {
        char ch = raw_title[i];
        if (ch == '"' || ch == '<' || ch == '>' || ch == '&') continue;
        clean[j++] = ch;
    }
    clean[j] = '\0';

    char *start = clean;
    while (*start == ' ' || *start == '\t') start++;
    char *end = start + strlen(start);
    while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t')) end--;
    *end = '\0';

    if (start[0] == '\0') {
        if (g_canvas_name[0] != '\0') {
            snprintf(g_canvas_title, sizeof(g_canvas_title), "%s", g_canvas_name);
        } else {
            snprintf(g_canvas_title, sizeof(g_canvas_title), "Level");
        }
    } else {
        snprintf(g_canvas_title, sizeof(g_canvas_title), "%s", start);
    }
}

void sync_canvas_title_everywhere(const char *new_title, HWND sender_hwnd) {
    static bool s_in_title_sync = false;
    if (s_in_title_sync) return;
    s_in_title_sync = true;

    set_canvas_title_sanitized(new_title);

    /* 1. Sync to HUD Map Attribute edit box (Level Title) */
    if (g_edit_preview_title && IsWindow(g_edit_preview_title) && sender_hwnd != g_edit_preview_title) {
        char cur_hud[256] = {0};
        GetWindowTextA(g_edit_preview_title, cur_hud, sizeof(cur_hud));
        if (strcmp(cur_hud, g_canvas_title) != 0) {
            SetWindowTextA(g_edit_preview_title, g_canvas_title);
        }
    }

    /* 2. Sync to Save Canvas dialog if open (Canvas Title) */
    sync_save_dialog_title_if_open(g_canvas_title, sender_hwnd);

    /* 3. Update Window Title and Status Bar */
    update_window_title();
    update_status_bar();

    if (g_hwnd_main && IsWindow(g_hwnd_main)) {
        InvalidateRect(g_hwnd_main, NULL, FALSE);
    }

    s_in_title_sync = false;
}

bool resolve_cutout_image_in_folder(const char *folder, const char *image_name, CanvasImage *out_img) {
    if (!folder || !folder[0] || !image_name || !image_name[0]) return false;
    const char *prefixes[] = {"_", ""};
    const char *exts[] = {"gif", "png", "jpg", "jpeg", "webp", "bmp"};
    char path[MAX_PATH];
    for (int p = 0; p < 2; ++p) {
        for (int e = 0; e < 6; ++e) {
            snprintf(path, sizeof(path), "%s\\%s%s.%s", folder, prefixes[p], image_name, exts[e]);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
                if (image_load_from_file(path, out_img)) return true;
            }
        }
    }
    return false;
}

bool resolve_sprite_image_in_folder(const char *folder, const char *image_name, CanvasImage *out_img) {
    if (!folder || !folder[0] || !image_name || !image_name[0]) return false;
    const char *prefixes[] = {"", "_"};
    const char *exts[] = {"png", "jpg", "jpeg", "gif", "webp", "bmp"};
    char path[MAX_PATH];
    for (int p = 0; p < 2; ++p) {
        for (int e = 0; e < 6; ++e) {
            snprintf(path, sizeof(path), "%s\\%s%s.%s", folder, prefixes[p], image_name, exts[e]);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
                if (image_load_from_file(path, out_img)) return true;
            }
        }
    }
    return false;
}

bool resolve_bgalpha_image_in_folder(const char *folder, const char *image_name, CanvasImage *out_img) {
    if (!folder || !folder[0] || !image_name || !image_name[0]) return false;
    const char *clean_name = (image_name[0] == '_') ? (image_name + 1) : image_name;
    const char *prefixes[] = {"_", ""}; /* BackgroundAlpha in folder strictly uses leading underscore */
    const char *exts[] = {"png", "jpg", "jpeg", "gif", "webp", "bmp"};
    char path[MAX_PATH];
    for (int p = 0; p < 2; ++p) {
        for (int e = 0; e < 6; ++e) {
            snprintf(path, sizeof(path), "%s\\%s%s.%s", folder, prefixes[p], clean_name, exts[e]);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
                if (image_load_from_file(path, out_img)) return true;
            }
        }
    }
    return false;
}

void toggle_overlay_mode(HWND hwnd) {
    if (!g_has_canvas) return;
    g_mode_overlay = !g_mode_overlay;
    if (g_mode_overlay) {
        if (g_mode_canvas) {
            g_mode_canvas = false;
            if (g_btn_canvas) InvalidateRect(g_btn_canvas, NULL, TRUE);
            if (g_edit_cv_w) ShowWindow(g_edit_cv_w, SW_HIDE);
            if (g_edit_cv_h) ShowWindow(g_edit_cv_h, SW_HIDE);
        }
        g_mode_curve = false;
        g_mode_element = false;
        g_placing_frog = false;
        g_placing_coin = false;
        g_frog_hovered = false;
        g_hovered_coin_idx = -1;
        g_hud_overlay_guide_scroll_y = 0;
        g_hud_overlay_tools_scroll_y = 0;
        if (g_btn_canvas)  InvalidateRect(g_btn_canvas, NULL, TRUE);
        if (g_btn_element) InvalidateRect(g_btn_element, NULL, TRUE);
        if (g_btn_curve) InvalidateRect(g_btn_curve, NULL, TRUE);
    } else {
        g_placing_cutout = false;
        g_placing_sprite = false;
        g_placing_bgalpha = false;
        g_placing_bgalpha_vector = false;
        g_editing_bgalpha_idx = -1;
        g_hovered_cutout_idx = -1;
        g_hovered_sprite_idx = -1;
        g_hovered_bgalpha_idx = -1;
        g_hud_overlay_guide_scroll_y = 0;
        g_hud_overlay_tools_scroll_y = 0;
        free_pending_cutout();
        free_pending_sprite();
        free_pending_bgalpha();
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
}

void trigger_add_cutout(HWND hwnd) {
    if (!g_has_canvas) {
        show_new_canvas_dialog(hwnd);
        return;
    }
    if (g_placing_cutout) {
        bool was_picked = g_picked_up_cutout;
        g_picked_up_cutout = false;
        g_placing_cutout = false;
        free_pending_cutout();
        if (was_picked) {
            undo_snapshot_capture("Delete Cutout");
        }
        InvalidateRect(hwnd, NULL, FALSE);
        update_status_bar();
        return;
    }

    char filepath[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = ZUPATHER_MEDIA_FILTER;
    ofn.lpstrFile   = filepath;
    ofn.nMaxFile    = sizeof(filepath);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = "Select Cutout Image (Overlay Mask)";

    if (!GetOpenFileNameA(&ofn)) {
        return;
    }

    CanvasImage temp_img;
    memset(&temp_img, 0, sizeof(temp_img));
    if (!image_load_from_file(filepath, &temp_img)) {
        show_themed_notice_dialog(
            hwnd,
            "Load Failed - ZuPather",
            "Cutout Media Error",
            "Failed to load selected media as cutout overlay.\n\nPlease ensure the file format is supported and not corrupted.",
            480, 240
        );
        return;
    }

    if (temp_img.is_video) {
        show_video_load_warning(hwnd);
    } else {
        char ext[16] = {0};
        image_get_extension(filepath, ext, sizeof(ext));
        if (_stricmp(ext, "webp") == 0) {
            show_webp_patch_warning(hwnd);
        }
    }

    if (g_placing_frog) {
        g_placing_frog = false;
        if (g_btn_frog) InvalidateRect(g_btn_frog, NULL, TRUE);
    }
    if (g_placing_coin) {
        g_placing_coin = false;
        if (g_btn_coin) InvalidateRect(g_btn_coin, NULL, TRUE);
    }
    if (g_mode_curve) {
        g_mode_curve = false;
        if (g_btn_curve) InvalidateRect(g_btn_curve, NULL, TRUE);
    }
    if (g_placing_sprite) {
        g_placing_sprite = false;
        free_pending_sprite();
    }
    if (g_placing_bgalpha) {
        g_placing_bgalpha = false;
        g_placing_bgalpha_vector = false;
        g_editing_bgalpha_idx = -1;
        free_pending_bgalpha();
    }

    g_mode_overlay = true;

    free_pending_cutout();
    g_pending_cutout_img = temp_img;
    g_has_pending_cutout_img = true;
    strncpy(g_pending_cutout_filepath, filepath, sizeof(g_pending_cutout_filepath) - 1);

    generate_cutout_masks_internal(
        g_pending_cutout_img.hbm, g_pending_cutout_img.width, g_pending_cutout_img.height,
        &g_pending_cutout_mask_light, &g_pending_cutout_hdc_light,
        &g_pending_cutout_mask_dark, &g_pending_cutout_hdc_dark
    );

    g_placing_cutout = true;
    g_picked_up_cutout = false;
    g_hovered_cutout_idx = -1;

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    g_mouse_screen_x = pt.x;
    g_mouse_screen_y = pt.y;

    if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
    SetFocus(hwnd);
}

void trigger_add_sprite(HWND hwnd) {
    if (!g_has_canvas) {
        show_new_canvas_dialog(hwnd);
        return;
    }
    if (g_placing_sprite) {
        bool was_picked = g_picked_up_sprite;
        g_picked_up_sprite = false;
        g_placing_sprite = false;
        free_pending_sprite();
        if (was_picked) {
            undo_snapshot_capture("Delete Sprite");
        }
        InvalidateRect(hwnd, NULL, FALSE);
        update_status_bar();
        return;
    }

    char filepath[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = ZUPATHER_MEDIA_FILTER;
    ofn.lpstrFile   = filepath;
    ofn.nMaxFile    = sizeof(filepath);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = "Select Sprite Image (Decoration)";

    if (!GetOpenFileNameA(&ofn)) {
        return;
    }

    CanvasImage temp_img;
    memset(&temp_img, 0, sizeof(temp_img));
    if (!image_load_from_file(filepath, &temp_img)) {
        show_themed_notice_dialog(
            hwnd,
            "Load Failed - ZuPather",
            "Sprite Media Error",
            "Failed to load selected media as sprite overlay.\n\nPlease ensure the file format is supported and not corrupted.",
            480, 240
        );
        return;
    }

    if (temp_img.is_video) {
        show_video_load_warning(hwnd);
    } else {
        char ext[16] = {0};
        image_get_extension(filepath, ext, sizeof(ext));
        if (_stricmp(ext, "webp") == 0) {
            show_webp_patch_warning(hwnd);
        }
    }

    if (g_placing_frog) {
        g_placing_frog = false;
        if (g_btn_frog) InvalidateRect(g_btn_frog, NULL, TRUE);
    }
    if (g_placing_coin) {
        g_placing_coin = false;
        if (g_btn_coin) InvalidateRect(g_btn_coin, NULL, TRUE);
    }
    if (g_mode_curve) {
        g_mode_curve = false;
        if (g_btn_curve) InvalidateRect(g_btn_curve, NULL, TRUE);
    }
    if (g_placing_cutout) {
        g_placing_cutout = false;
        free_pending_cutout();
    }
    if (g_placing_bgalpha) {
        g_placing_bgalpha = false;
        g_placing_bgalpha_vector = false;
        g_editing_bgalpha_idx = -1;
        free_pending_bgalpha();
    }

    g_mode_overlay = true;

    free_pending_sprite();
    g_pending_sprite_img = temp_img;
    g_has_pending_sprite_img = true;
    strncpy(g_pending_sprite_filepath, filepath, sizeof(g_pending_sprite_filepath) - 1);

    g_placing_sprite = true;
    g_picked_up_sprite = false;
    g_hovered_sprite_idx = -1;

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    g_mouse_screen_x = pt.x;
    g_mouse_screen_y = pt.y;

    if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
    SetFocus(hwnd);
}

void trigger_add_bgalpha(HWND hwnd) {
    if (!g_has_canvas) {
        show_new_canvas_dialog(hwnd);
        return;
    }
    if (g_placing_bgalpha) {
        bool was_picked = g_picked_up_bgalpha;
        g_picked_up_bgalpha = false;
        g_placing_bgalpha = false;
        g_placing_bgalpha_vector = false;
        g_editing_bgalpha_idx = -1;
        free_pending_bgalpha();
        if (was_picked) {
            undo_snapshot_capture("Delete Background Alpha");
        }
        InvalidateRect(hwnd, NULL, FALSE);
        update_status_bar();
        return;
    }

    char filepath[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = ZUPATHER_MEDIA_FILTER;
    ofn.lpstrFile   = filepath;
    ofn.nMaxFile    = sizeof(filepath);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = "Select BackgroundAlpha Image (Level Transition)";

    if (!GetOpenFileNameA(&ofn)) {
        return;
    }

    CanvasImage temp_img;
    memset(&temp_img, 0, sizeof(temp_img));
    if (!image_load_from_file(filepath, &temp_img)) {
        show_themed_notice_dialog(
            hwnd,
            "Load Failed - ZuPather",
            "BackgroundAlpha Media Error",
            "Failed to load selected media as BackgroundAlpha overlay.\n\nPlease ensure the file format is supported and not corrupted.",
            480, 240
        );
        return;
    }

    if (temp_img.is_video) {
        show_video_load_warning(hwnd);
    } else {
        char ext[16] = {0};
        image_get_extension(filepath, ext, sizeof(ext));
        if (_stricmp(ext, "webp") == 0) {
            show_webp_patch_warning(hwnd);
        }
    }

    if (g_placing_frog) {
        g_placing_frog = false;
        if (g_btn_frog) InvalidateRect(g_btn_frog, NULL, TRUE);
    }
    if (g_placing_coin) {
        g_placing_coin = false;
        if (g_btn_coin) InvalidateRect(g_btn_coin, NULL, TRUE);
    }
    if (g_mode_curve) {
        g_mode_curve = false;
        if (g_btn_curve) InvalidateRect(g_btn_curve, NULL, TRUE);
    }
    if (g_placing_cutout) {
        g_placing_cutout = false;
        free_pending_cutout();
    }
    if (g_placing_sprite) {
        g_placing_sprite = false;
        free_pending_sprite();
    }

    g_mode_overlay = true;

    free_pending_bgalpha();
    g_pending_bgalpha_img = temp_img;
    g_has_pending_bgalpha_img = true;
    strncpy(g_pending_bgalpha_filepath, filepath, sizeof(g_pending_bgalpha_filepath) - 1);

    g_placing_bgalpha = true;
    g_picked_up_bgalpha = false;
    g_placing_bgalpha_vector = false;
    g_editing_bgalpha_idx = -1;
    g_hovered_bgalpha_idx = -1;
    g_bgalpha_pending_vx = 0;
    g_bgalpha_pending_vy = 0;

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    g_mouse_screen_x = pt.x;
    g_mouse_screen_y = pt.y;

    if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
    SetFocus(hwnd);
}

static HANDLE s_hfont_mem[4] = {NULL, NULL, NULL, NULL};

static void load_embedded_font_res(int res_id, int slot) {
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(res_id), (LPCSTR)RT_RCDATA);
    if (!hRes) return;
    HGLOBAL hMem = LoadResource(NULL, hRes);
    if (!hMem) return;
    DWORD sz = SizeofResource(NULL, hRes);
    const void *ptr = LockResource(hMem);
    if (!ptr || sz == 0) return;
    DWORD num_fonts = 0;
    HANDLE h = AddFontMemResourceEx((PVOID)ptr, sz, NULL, &num_fonts);
    if (h) {
        s_hfont_mem[slot] = h;
    }
}

void init_fonts(void) {
    /* 1. Register process-private embedded fonts directly from RAM (100% portable) */
    load_embedded_font_res(IDR_FONT_ATKINSON_REGULAR, 0);
    load_embedded_font_res(IDR_FONT_ATKINSON_BOLD, 1);
    load_embedded_font_res(IDR_FONT_JETBRAINS_REGULAR, 2);
    load_embedded_font_res(IDR_FONT_JETBRAINS_BOLD, 3);

    g_font_primary_regular = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, FONT_PRIMARY_NAME);
    g_font_primary_bold = CreateFontA(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, FONT_PRIMARY_NAME);
    g_font_title_bold = CreateFontA(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, FONT_PRIMARY_NAME);
    g_font_mono_regular = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, FONT_SECONDARY_NAME);
    g_font_mono_bold = CreateFontA(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, FONT_SECONDARY_NAME);

    if (!g_font_primary_regular) g_font_primary_regular = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!g_font_primary_bold)    g_font_primary_bold    = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!g_font_title_bold)      g_font_title_bold      = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!g_font_mono_regular)    g_font_mono_regular    = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);
    if (!g_font_mono_bold)       g_font_mono_bold       = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);
}

void destroy_fonts(void) {
    if (g_font_primary_regular) DeleteObject(g_font_primary_regular);
    if (g_font_primary_bold)    DeleteObject(g_font_primary_bold);
    if (g_font_title_bold)      DeleteObject(g_font_title_bold);
    if (g_font_mono_regular)    DeleteObject(g_font_mono_regular);
    if (g_font_mono_bold)       DeleteObject(g_font_mono_bold);

    for (int i = 0; i < 4; ++i) {
        if (s_hfont_mem[i]) {
            RemoveFontMemResourceEx(s_hfont_mem[i]);
            s_hfont_mem[i] = NULL;
        }
    }
}

int get_workspace_height(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int client_h = rc.bottom - rc.top;
    int status_h = 24;
    if (g_hwnd_status) {
        RECT rcs;
        GetWindowRect(g_hwnd_status, &rcs);
        status_h = rcs.bottom - rcs.top;
    }
    int work_h = client_h - TOOLBAR_HEIGHT - status_h;
    return work_h > 0 ? work_h : 0;
}

void get_canvas_screen_rect(int client_w, int work_h, RECT *out_rc) {
    if (!g_has_canvas) {
        SetRect(out_rc, 0, 0, 0, 0);
        return;
    }
    float scaled_w = (float)g_canvas_w * g_zoom;
    float scaled_h = (float)g_canvas_h * g_zoom;

    float center_x = (float)client_w * 0.5f + g_pan_x;
    float center_y = (float)TOOLBAR_HEIGHT + (float)work_h * 0.5f + g_pan_y;

    out_rc->left   = (int)roundf(center_x - scaled_w * 0.5f);
    out_rc->top    = (int)roundf(center_y - scaled_h * 0.5f);
    out_rc->right  = (int)roundf(center_x + scaled_w * 0.5f);
    out_rc->bottom = (int)roundf(center_y + scaled_h * 0.5f);
}

void screen_to_canvas(int client_w, int work_h, int sx, int sy, float *out_cx, float *out_cy) {
    if (!g_has_canvas) {
        *out_cx = -1.0f;
        *out_cy = -1.0f;
        return;
    }
    RECT rc;
    get_canvas_screen_rect(client_w, work_h, &rc);
    *out_cx = ((float)sx - (float)rc.left) / g_zoom;
    *out_cy = ((float)sy - (float)rc.top) / g_zoom;
}

void update_status_bar(void) {
    if (!g_hwnd_status) return;

    if (!g_has_canvas) {
        SendMessageA(g_hwnd_status, SB_SETTEXTA, 0 | SBT_OWNERDRAW | SBT_NOBORDERS, (LPARAM)"No active canvas | Ctrl+N: New Canvas | Ctrl+O: Open Canvas | Ctrl+Shift+M: Map Manager");
        SendMessageA(g_hwnd_status, SB_SETTEXTA, 1 | SBT_OWNERDRAW | SBT_NOBORDERS, (LPARAM)"---");
        SendMessageA(g_hwnd_status, SB_SETTEXTA, 2 | SBT_OWNERDRAW | SBT_NOBORDERS, (LPARAM)"X: ---");
        SendMessageA(g_hwnd_status, SB_SETTEXTA, 3 | SBT_OWNERDRAW | SBT_NOBORDERS, (LPARAM)"Y: ---");
        return;
    }

    static char buf_info[256];
    if (g_mode_curve) {
        const char *sub_name = (g_curve_submode == CURVE_SUBMODE_DRAW) ? "Draw Mode" : "Attribute Mode";
        int sel_node_cnt = 0;
        for (int p = 0; p < g_path_count; ++p) {
            for (int i = 0; i < g_paths[p].node_count; ++i) {
                if (g_paths[p].nodes[i].selected) sel_node_cnt++;
            }
        }
        if (g_curve_submode == CURVE_SUBMODE_DRAW && g_hovered_curve_seg_path >= 0 && g_hovered_curve_seg_path < g_path_count) {
            snprintf(buf_info, sizeof(buf_info),
                     "Curve [Draw Mode] | Path #%d (Segment %d-%d) | Alt+LMB: Insert Node Between Points | Drag: Adjust",
                     g_hovered_curve_seg_path + 1, g_hovered_curve_seg_idx + 1, g_hovered_curve_seg_idx + 2);
        } else if (sel_node_cnt > 0) {
            snprintf(buf_info, sizeof(buf_info),
                     "Curve [%s] | %d Selected Node%s | Delete: Remove | Arrows: Nudge 1px | Ctrl+D: Deselect",
                     sub_name, sel_node_cnt, sel_node_cnt > 1 ? "s" : "");
        } else if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
            if (g_curve_submode == CURVE_SUBMODE_DRAW) {
                snprintf(buf_info, sizeof(buf_info),
                         "Curve [%s] | Path #%d [%s]: %d nodes | LMB: Continue Path | RMB: Finish Curve | Ctrl+Click: Select Point",
                         sub_name, g_active_path_idx + 1, g_paths[g_active_path_idx].name, g_paths[g_active_path_idx].node_count);
            } else {
                snprintf(buf_info, sizeof(buf_info),
                         "Curve [%s] | Path #%d [%s]: %d nodes | LMB: Paint Attribute Span | Ctrl+Click: Select Point",
                         sub_name, g_active_path_idx + 1, g_paths[g_active_path_idx].name, g_paths[g_active_path_idx].node_count);
            }
        } else {
            snprintf(buf_info, sizeof(buf_info),
                     "Curve [%s] | No active path | LMB: New Path #%d",
                     sub_name, g_path_count + 1);
        }
    } else if (g_placing_frog) {
        if (g_mouse_inside_canvas) {
            snprintf(buf_info, sizeof(buf_info), "Set Frog Position: [gx=%d, gy=%d] | LMB: Confirm | Esc: Cancel",
                     (int)roundf(g_canvas_mouse_x), (int)roundf(g_canvas_mouse_y));
        } else {
            snprintf(buf_info, sizeof(buf_info), "Set Frog Position: Hover cursor over desired position | LMB: Confirm | Esc: Cancel");
        }
    } else if (g_placing_coin) {
        if (g_mouse_inside_canvas) {
            snprintf(buf_info, sizeof(buf_info), "Add Coin (Treasure Point #%d): [x=%d, y=%d] | LMB: Place | Esc: Cancel",
                     g_treasure_point_count + 1, (int)roundf(g_canvas_mouse_x), (int)roundf(g_canvas_mouse_y));
        } else {
            snprintf(buf_info, sizeof(buf_info), "Add Coin (Treasure Point #%d): Hover cursor over desired position | LMB: Place | Esc: Cancel",
                     g_treasure_point_count + 1);
        }
    } else if (g_placing_bgalpha_vector && g_editing_bgalpha_idx >= 0) {
        snprintf(buf_info, sizeof(buf_info), "Adjust Motion: Aim cursor around donut to set Vx/Vy [%d, %d] | LMB: Confirm | RMB / Esc: Cancel",
                 g_bgalpha_pending_vx, g_bgalpha_pending_vy);
    } else if (g_placing_bgalpha) {
        if (g_placing_bgalpha_vector) {
            snprintf(buf_info, sizeof(buf_info), "BackgroundAlpha Step 2: Aim cursor around donut to set Vx/Vy [%d, %d] | LMB: Confirm | RMB / Esc: Cancel",
                     g_bgalpha_pending_vx, g_bgalpha_pending_vy);
        } else if (g_mouse_inside_canvas) {
            snprintf(buf_info, sizeof(buf_info), "BackgroundAlpha Step 1: Anchor [x=%d, y=%d] | LMB: Anchor | Esc: Cancel",
                     (int)roundf(g_canvas_mouse_x), (int)roundf(g_canvas_mouse_y));
        } else {
            snprintf(buf_info, sizeof(buf_info), "BackgroundAlpha Step 1: Hover cursor over desired position | LMB: Anchor | Esc: Cancel");
        }
    } else if (g_placing_cutout) {
        if (g_mouse_inside_canvas) {
            snprintf(buf_info, sizeof(buf_info), "Place Cutout (Tunnel #%d): [x=%d, y=%d] | Pri: %d (Wheel: Pri) | LMB: Confirm | Esc: Cancel",
                     g_cutout_count + 1, (int)roundf(g_canvas_mouse_x), (int)roundf(g_canvas_mouse_y), g_overlay_current_prio);
        } else {
            snprintf(buf_info, sizeof(buf_info), "Place Cutout (Tunnel #%d): Hover cursor over desired position | Pri: %d (Wheel: Pri) | LMB: Confirm | Esc: Cancel",
                     g_cutout_count + 1, g_overlay_current_prio);
        }
    } else if (g_placing_sprite) {
        if (g_mouse_inside_canvas) {
            snprintf(buf_info, sizeof(buf_info), "Place Sprite (Sprite #%d): [x=%d, y=%d] | Pri: %d (Wheel: Pri) | LMB: Confirm | Esc: Cancel",
                     g_sprite_count + 1, (int)roundf(g_canvas_mouse_x), (int)roundf(g_canvas_mouse_y), g_overlay_current_prio);
        } else {
            snprintf(buf_info, sizeof(buf_info), "Place Sprite (Sprite #%d): Hover cursor over desired position | Pri: %d (Wheel: Pri) | LMB: Confirm | Esc: Cancel",
                     g_sprite_count + 1, g_overlay_current_prio);
        }
    } else if (g_frog_hovered && g_mode_element) {
        snprintf(buf_info, sizeof(buf_info), "Canvas: %s | Frog: [gx=%d, gy=%d] (LMB: Reposition / Follow cursor)",
                 g_canvas_name, g_frog_gx, g_frog_gy);
    } else if (g_mode_element && g_hovered_coin_idx >= 0 && g_hovered_coin_idx < g_treasure_point_count) {
        snprintf(buf_info, sizeof(buf_info), "Canvas: %s | Coin #%d: [x=%d, y=%d] (LMB: Reposition / Follow cursor | RMB: Delete)",
                 g_canvas_name, g_hovered_coin_idx + 1,
                 g_treasure_points[g_hovered_coin_idx].x, g_treasure_points[g_hovered_coin_idx].y);
    } else if (g_mode_overlay && g_hovered_bgalpha_idx >= 0 && g_hovered_bgalpha_idx < g_bg_alpha_count) {
        LevelBackgroundAlpha *ba = &g_bg_alphas[g_hovered_bgalpha_idx];
        const char *b_id = ba->image_name[0] ? ba->image_name : ((g_bg_alpha_count == 1) ? "bgalpha" : "bgalpha1");
        snprintf(buf_info, sizeof(buf_info), "Canvas: %s | BgAlpha ID: %s (#%d) | Vx: %d, Vy: %d | [x=%d, y=%d] (LMB: Move | RMB: Delete)",
                 g_canvas_name, b_id, g_hovered_bgalpha_idx + 1,
                 ba->vx, ba->vy, ba->x, ba->y);
    } else if (g_mode_overlay && g_hovered_cutout_idx >= 0 && g_hovered_cutout_idx < g_cutout_count) {
        LevelCutout *co = &g_cutouts[g_hovered_cutout_idx];
        const char *c_id = co->image_name[0] ? co->image_name : ((g_cutout_count == 1) ? "tunnel" : "tunnel1");
        snprintf(buf_info, sizeof(buf_info), "Canvas: %s | Cutout ID: %s (#%d) | Priority: %d | [x=%d, y=%d] (LMB: Move | RMB: Delete)",
                 g_canvas_name, c_id, g_hovered_cutout_idx + 1,
                 co->pri, co->x, co->y);
    } else if (g_mode_overlay && g_hovered_sprite_idx >= 0 && g_hovered_sprite_idx < g_sprite_count) {
        LevelSprite *sp = &g_sprites[g_hovered_sprite_idx];
        const char *s_id = sp->image_name[0] ? sp->image_name : ((g_sprite_count == 1) ? "sprite" : "sprite1");
        snprintf(buf_info, sizeof(buf_info), "Canvas: %s | Sprite ID: %s (#%d) | Priority: %d | [x=%d, y=%d] (LMB: Move | RMB: Delete)",
                 g_canvas_name, s_id, g_hovered_sprite_idx + 1,
                 sp->pri, sp->x, sp->y);
    } else if (g_mode_canvas && is_any_canvas_handle_selected()) {
        int sel_cnt = get_selected_canvas_handle_count();
        snprintf(buf_info, sizeof(buf_info), "Canvas: %s | %d Selected Handle%s | Arrows: Nudge 1px | Ctrl+D: Deselect | Esc: Cancel",
                 g_canvas_name, sel_cnt, sel_cnt > 1 ? "s" : "");
    } else if (get_selected_object_count() > 0) {
        int sel_cnt = get_selected_object_count();
        snprintf(buf_info, sizeof(buf_info), "Canvas: %s | %d Selected Object%s | Delete: Remove | Arrows: Nudge 1px | Ctrl+D: Deselect",
                 g_canvas_name, sel_cnt, sel_cnt > 1 ? "s" : "");
    } else if (g_mode_element) {
        snprintf(buf_info, sizeof(buf_info), "Canvas: %s | Element Mode | Ctrl+P: Frog (%s) | Ctrl+K: Add Coin (%d placed) | Ctrl+E: Exit",
                 g_canvas_name, g_has_frog ? "Set" : "Not Set", g_treasure_point_count);
    } else if (g_mode_overlay) {
        snprintf(buf_info, sizeof(buf_info), "Canvas: %s | Overlay Mode | Cutouts: %d | Sprites: %d | BgAlpha: %d | Pri: %d | Ctrl+Shift+O: Exit",
                 g_canvas_name, g_cutout_count, g_sprite_count, g_bg_alpha_count, g_overlay_current_prio);
    } else {
        if (g_level_preview_active) {
            snprintf(buf_info, sizeof(buf_info), "Canvas: %s | Level Preview Active (Simulating Ball Trains & Skulls) | Click [Stop Level Preview] to exit",
                     g_canvas_name);
        } else if (g_hud_map_attr_hover != MAP_ATTR_HOVER_NONE) {
            const char *h_status = get_map_attr_hover_status(g_hud_map_attr_hover);
            snprintf(buf_info, sizeof(buf_info), "Canvas: %s | %s", g_canvas_name, h_status ? h_status : "");
        } else {
            snprintf(buf_info, sizeof(buf_info), "Canvas: %s (%dx%d) | Preview Mode | 1: Canvas | 2: Curve | 3: Element | 4: Overlay | M: Pan",
                     g_canvas_name, g_canvas_w, g_canvas_h);
        }
    }

    static char buf_zoom[64];
    snprintf(buf_zoom, sizeof(buf_zoom), "Zoom: %d%%", (int)roundf(g_zoom * 100.0f));

    static char buf_x[64];
    static char buf_y[64];
    snprintf(buf_x, sizeof(buf_x), "X: %.1f px", g_canvas_mouse_x);
    snprintf(buf_y, sizeof(buf_y), "Y: %.1f px", g_canvas_mouse_y);

    SendMessageA(g_hwnd_status, SB_SETTEXTA, 0 | SBT_OWNERDRAW | SBT_NOBORDERS, (LPARAM)buf_info);
    SendMessageA(g_hwnd_status, SB_SETTEXTA, 1 | SBT_OWNERDRAW | SBT_NOBORDERS, (LPARAM)buf_zoom);
    SendMessageA(g_hwnd_status, SB_SETTEXTA, 2 | SBT_OWNERDRAW | SBT_NOBORDERS, (LPARAM)buf_x);
    SendMessageA(g_hwnd_status, SB_SETTEXTA, 3 | SBT_OWNERDRAW | SBT_NOBORDERS, (LPARAM)buf_y);
}

void zoom_to_fit(HWND hwnd) {
    if (!g_has_canvas) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int client_w = rc.right - rc.left;
    int work_h = get_workspace_height(hwnd);

    int pad_x = 40;
    int pad_y = 40;
    int avail_w = client_w - pad_x;
    int avail_h = work_h - pad_y;
    if (avail_w < 50) avail_w = 50;
    if (avail_h < 50) avail_h = 50;

    float zx = (float)avail_w / (float)g_canvas_w;
    float zy = (float)avail_h / (float)g_canvas_h;
    float fit_zoom = (zx < zy) ? zx : zy;

    if (fit_zoom > 4.0f)  fit_zoom = 4.0f;
    if (fit_zoom < 0.25f) fit_zoom = 0.25f;

    g_zoom = fit_zoom;
    g_pan_x = 0.0f;
    g_pan_y = 0.0f;

    update_status_bar();
    InvalidateRect(hwnd, NULL, FALSE);
}

void set_canvas_size(HWND hwnd, int new_w, int new_h) {
    if (new_w <= 0 || new_h <= 0) return;
    g_canvas_w = new_w;
    g_canvas_h = new_h;
    g_has_canvas = true;

    if (g_canvas_name[0] == '\0') {
        strncpy(g_canvas_name, "Untitled Canvas", sizeof(g_canvas_name) - 1);
    }
    if (g_canvas_title[0] == '\0') {
        strncpy(g_canvas_title, g_canvas_name, sizeof(g_canvas_title) - 1);
    }
    update_window_title();

    if (g_edit_w && g_edit_h) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", g_canvas_w);
        SetWindowTextA(g_edit_w, buf);
        snprintf(buf, sizeof(buf), "%d", g_canvas_h);
        SetWindowTextA(g_edit_h, buf);
    }
    if (g_edit_cv_w && g_edit_cv_h) {
        g_in_canvas_edit_sync = true;
        char buf[32];
        if (GetFocus() != g_edit_cv_w) {
            snprintf(buf, sizeof(buf), "%d", g_canvas_w);
            SetWindowTextA(g_edit_cv_w, buf);
        }
        if (GetFocus() != g_edit_cv_h) {
            snprintf(buf, sizeof(buf), "%d", g_canvas_h);
            SetWindowTextA(g_edit_cv_h, buf);
        }
        g_in_canvas_edit_sync = false;
    }

    /* Update Menu checkmarks */
    HMENU hmenu = GetMenu(hwnd);
    if (hmenu) {
        CheckMenuItem(hmenu, ID_MENU_CANVAS_640_480,  (g_canvas_w == 640 && g_canvas_h == 480)   ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hmenu, ID_MENU_CANVAS_800_600,  (g_canvas_w == 800 && g_canvas_h == 600)   ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hmenu, ID_MENU_CANVAS_1024_768, (g_canvas_w == 1024 && g_canvas_h == 768)  ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hmenu, ID_MENU_CANVAS_1280_720, (g_canvas_w == 1280 && g_canvas_h == 720)  ? MF_CHECKED : MF_UNCHECKED);
    }

    update_status_bar();
    InvalidateRect(hwnd, NULL, FALSE);
}

/* 45-degree angle snapping helper (Affinity Designer / Illustrator style) */
void snap_vector_45_deg(float dx, float dy, float *out_dx, float *out_dy) {
    double len = hypot((double)dx, (double)dy);
    if (len < 1e-4) {
        *out_dx = dx;
        *out_dy = dy;
        return;
    }
    double angle = atan2((double)dy, (double)dx);
    const double SNAP_STEP = 3.14159265358979323846 / 4.0; /* 45 degrees */
    int step_idx = (int)round(angle / SNAP_STEP);
    step_idx = ((step_idx % 8) + 8) % 8;

    switch (step_idx) {
        case 0: /* 0 deg: East (+X, horizontal right) */
            *out_dx = (float)len;
            *out_dy = 0.0f;
            break;
        case 1: /* 45 deg: South-East (+X, +Y) */
            *out_dx = (float)(len * 0.7071067811865475);
            *out_dy = (float)(len * 0.7071067811865475);
            break;
        case 2: /* 90 deg: South (+Y, vertical down) */
            *out_dx = 0.0f;
            *out_dy = (float)len;
            break;
        case 3: /* 135 deg: South-West (-X, +Y) */
            *out_dx = (float)(-len * 0.7071067811865475);
            *out_dy = (float)(len * 0.7071067811865475);
            break;
        case 4: /* 180 deg: West (-X, horizontal left) */
            *out_dx = (float)(-len);
            *out_dy = 0.0f;
            break;
        case 5: /* 225 deg: North-West (-X, -Y) */
            *out_dx = (float)(-len * 0.7071067811865475);
            *out_dy = (float)(-len * 0.7071067811865475);
            break;
        case 6: /* 270 deg: North (-Y, vertical up) */
            *out_dx = 0.0f;
            *out_dy = (float)(-len);
            break;
        case 7: /* 315 deg: North-East (+X, -Y) */
            *out_dx = (float)(len * 0.7071067811865475);
            *out_dy = (float)(-len * 0.7071067811865475);
            break;
        default:
            *out_dx = dx;
            *out_dy = dy;
            break;
    }
}

/* Unified Selection Handlers (Canvas Mode, Curve Mode, Element Mode, Overlay Mode) */
void handle_select_all(HWND hwnd) {
    if (!g_has_canvas) return;
    if (g_mode_canvas) {
        select_all_canvas_handles();
    } else if (g_mode_curve) {
        for (int p = 0; p < g_path_count; ++p) {
            for (int i = 0; i < g_paths[p].node_count; ++i) {
                g_paths[p].nodes[i].selected = true;
            }
        }
        if (g_active_path_idx < 0 && g_path_count > 0) {
            g_active_path_idx = 0;
        }
    } else if (g_mode_element) {
        if (g_has_frog) g_frog_selected = true;
        for (int i = 0; i < g_treasure_point_count; ++i) {
            g_treasure_points[i].selected = true;
        }
    } else if (g_mode_overlay) {
        for (int i = 0; i < g_cutout_count; ++i) g_cutouts[i].selected = true;
        for (int i = 0; i < g_sprite_count; ++i) g_sprites[i].selected = true;
        for (int i = 0; i < g_bg_alpha_count; ++i) g_bg_alphas[i].selected = true;
    }
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
}

void handle_invert_selection(HWND hwnd) {
    if (!g_has_canvas) return;
    if (g_mode_canvas) {
        invert_canvas_handle_selection();
    } else if (g_mode_curve) {
        int first_sel_path = -1;
        for (int p = 0; p < g_path_count; ++p) {
            for (int i = 0; i < g_paths[p].node_count; ++i) {
                g_paths[p].nodes[i].selected = !g_paths[p].nodes[i].selected;
                if (g_paths[p].nodes[i].selected && first_sel_path < 0) {
                    first_sel_path = p;
                }
            }
        }
        if (first_sel_path >= 0) {
            g_active_path_idx = first_sel_path;
        }
    } else if (g_mode_element) {
        if (g_has_frog) g_frog_selected = !g_frog_selected;
        for (int i = 0; i < g_treasure_point_count; ++i) {
            g_treasure_points[i].selected = !g_treasure_points[i].selected;
        }
    } else if (g_mode_overlay) {
        for (int i = 0; i < g_cutout_count; ++i) g_cutouts[i].selected = !g_cutouts[i].selected;
        for (int i = 0; i < g_sprite_count; ++i) g_sprites[i].selected = !g_sprites[i].selected;
        for (int i = 0; i < g_bg_alpha_count; ++i) g_bg_alphas[i].selected = !g_bg_alphas[i].selected;
    }
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
}

void handle_deselect_all(HWND hwnd) {
    if (!g_has_canvas) return;
    if (g_mode_canvas) {
        deselect_all_canvas_handles();
    } else if (g_mode_curve) {
        g_active_node_idx = -1;
        if (g_pen_drag_state == PEN_DRAG_MARQUEE) {
            free_marquee_snapshot();
        }
        g_pen_drag_state = PEN_DRAG_NONE;
        for (int p = 0; p < g_path_count; ++p) {
            for (int i = 0; i < g_paths[p].node_count; ++i) {
                g_paths[p].nodes[i].selected = false;
            }
        }
    }
    deselect_all_objects();
    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
}

void deselect_all_canvas_handles(void) {
    for (int i = 1; i <= 9; ++i) {
        g_canvas_handle_selected[i] = false;
    }
}

void select_all_canvas_handles(void) {
    for (int i = 1; i <= 9; ++i) {
        g_canvas_handle_selected[i] = true;
    }
}

void invert_canvas_handle_selection(void) {
    for (int i = 1; i <= 9; ++i) {
        g_canvas_handle_selected[i] = !g_canvas_handle_selected[i];
    }
}

bool is_any_canvas_handle_selected(void) {
    for (int i = 1; i <= 9; ++i) {
        if (g_canvas_handle_selected[i]) return true;
    }
    return false;
}

int get_selected_canvas_handle_count(void) {
    int cnt = 0;
    for (int i = 1; i <= 9; ++i) {
        if (g_canvas_handle_selected[i]) cnt++;
    }
    return cnt;
}

void snapshot_canvas_handle_selection(void) {
    for (int i = 1; i <= 9; ++i) {
        g_snap_canvas_handle_selected[i] = g_canvas_handle_selected[i];
    }
}

void get_canvas_handle_screen_point(int client_w, int work_h, CanvasHandleType ht, int *out_x, int *out_y) {
    RECT rc;
    get_canvas_screen_rect(client_w, work_h, &rc);
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;
    int hx = cx, hy = cy;

    switch (ht) {
        case CANVAS_HANDLE_TOP:          hx = cx; hy = rc.top - 8; break;
        case CANVAS_HANDLE_BOTTOM:       hx = cx; hy = rc.bottom + 8; break;
        case CANVAS_HANDLE_LEFT:         hx = rc.left - 8; hy = cy; break;
        case CANVAS_HANDLE_RIGHT:        hx = rc.right + 8; hy = cy; break;
        case CANVAS_HANDLE_TOP_LEFT:     hx = rc.left - 8; hy = rc.top - 8; break;
        case CANVAS_HANDLE_TOP_RIGHT:    hx = rc.right + 8; hy = rc.top - 8; break;
        case CANVAS_HANDLE_BOTTOM_LEFT:  hx = rc.left - 8; hy = rc.bottom + 8; break;
        case CANVAS_HANDLE_BOTTOM_RIGHT: hx = rc.right + 8; hy = rc.bottom + 8; break;
        case CANVAS_HANDLE_CENTER_MOVE:  hx = cx; hy = cy; break;
        default: break;
    }

    if (out_x) *out_x = hx;
    if (out_y) *out_y = hy;
}

void update_canvas_handle_marquee_selection(int min_sx, int min_sy, int max_sx, int max_sy, int client_w, int work_h) {
    for (int ht = 1; ht <= 9; ++ht) {
        int hx = 0, hy = 0;
        get_canvas_handle_screen_point(client_w, work_h, (CanvasHandleType)ht, &hx, &hy);

        int r = 8;
        bool inside = !(hx + r < min_sx || hx - r > max_sx || hy + r < min_sy || hy - r > max_sy);
        bool base = g_snap_canvas_handle_selected[ht];

        switch (g_obj_marquee_mode) {
            case MARQUEE_MODE_REPLACE:  g_canvas_handle_selected[ht] = inside; break;
            case MARQUEE_MODE_ADD:      g_canvas_handle_selected[ht] = base || inside; break;
            case MARQUEE_MODE_SUBTRACT: g_canvas_handle_selected[ht] = base && !inside; break;
            case MARQUEE_MODE_TOGGLE:   g_canvas_handle_selected[ht] = inside ? !base : base; break;
        }
    }
}


void shift_all_canvas_objects(int dx, int dy) {
    if (dx == 0 && dy == 0) return;

    /* 1. Frog */
    if (g_has_frog) {
        g_frog_gx += dx;
        g_frog_gy += dy;
    }

    /* 2. Coins (Treasure Points) */
    for (int i = 0; i < g_treasure_point_count; ++i) {
        g_treasure_points[i].x += dx;
        g_treasure_points[i].y += dy;
    }

    /* 3. Cutouts */
    for (int i = 0; i < g_cutout_count; ++i) {
        g_cutouts[i].x += dx;
        g_cutouts[i].y += dy;
    }

    /* 4. Sprites */
    for (int i = 0; i < g_sprite_count; ++i) {
        g_sprites[i].x += dx;
        g_sprites[i].y += dy;
    }

    /* 5. BackgroundAlphas */
    for (int i = 0; i < g_bg_alpha_count; ++i) {
        g_bg_alphas[i].x += dx;
        g_bg_alphas[i].y += dy;
    }

    /* 6. Vector Curves */
    for (int p = 0; p < g_path_count; ++p) {
        for (int n = 0; n < g_paths[p].node_count; ++n) {
            BezierNode *nd = &g_paths[p].nodes[n];
            nd->x += (float)dx;
            nd->y += (float)dy;
        }
        path_cache_invalidate(&g_paths[p]);
    }
}

void snapshot_canvas_drag_state(void) {
    g_canvas_drag_snap.canvas_w = g_canvas_w;
    g_canvas_drag_snap.canvas_h = g_canvas_h;
    g_canvas_drag_snap.frog_x = g_frog_gx;
    g_canvas_drag_snap.frog_y = g_frog_gy;
    for (int i = 0; i < g_treasure_point_count; ++i) {
        g_canvas_drag_snap.coin_x[i] = g_treasure_points[i].x;
        g_canvas_drag_snap.coin_y[i] = g_treasure_points[i].y;
    }
    for (int i = 0; i < g_cutout_count; ++i) {
        g_canvas_drag_snap.cutout_x[i] = g_cutouts[i].x;
        g_canvas_drag_snap.cutout_y[i] = g_cutouts[i].y;
        g_canvas_drag_snap.cutout_w[i] = g_cutouts[i].has_img ? g_cutouts[i].img.width : 0;
        g_canvas_drag_snap.cutout_h[i] = g_cutouts[i].has_img ? g_cutouts[i].img.height : 0;
    }
    for (int i = 0; i < g_sprite_count; ++i) {
        g_canvas_drag_snap.sprite_x[i] = g_sprites[i].x;
        g_canvas_drag_snap.sprite_y[i] = g_sprites[i].y;
        g_canvas_drag_snap.sprite_w[i] = g_sprites[i].has_img ? g_sprites[i].img.width : 0;
        g_canvas_drag_snap.sprite_h[i] = g_sprites[i].has_img ? g_sprites[i].img.height : 0;
    }
    for (int i = 0; i < g_bg_alpha_count; ++i) {
        g_canvas_drag_snap.bgalpha_x[i] = g_bg_alphas[i].x;
        g_canvas_drag_snap.bgalpha_y[i] = g_bg_alphas[i].y;
        g_canvas_drag_snap.bgalpha_w[i] = g_bg_alphas[i].has_img ? g_bg_alphas[i].img.width : 0;
        g_canvas_drag_snap.bgalpha_h[i] = g_bg_alphas[i].has_img ? g_bg_alphas[i].img.height : 0;
    }
    g_canvas_drag_snap.pan_x = g_pan_x;
    g_canvas_drag_snap.pan_y = g_pan_y;
    int pc = g_path_count;
    if (pc > MAX_PATHS) pc = MAX_PATHS;
    g_canvas_drag_snap.path_count = pc;
    for (int p = 0; p < pc; ++p) {
        path_cache_update(&g_paths[p]);
        int nc = g_paths[p].node_count;
        if (nc > MAX_BEZIER_NODES) nc = MAX_BEZIER_NODES;
        g_canvas_drag_snap.paths[p].node_count = nc;
        for (int n = 0; n < nc; ++n) {
            g_canvas_drag_snap.paths[p].node_x[n] = g_paths[p].nodes[n].x;
            g_canvas_drag_snap.paths[p].node_y[n] = g_paths[p].nodes[n].y;
            g_canvas_drag_snap.paths[p].hin_x[n] = g_paths[p].nodes[n].hin_x;
            g_canvas_drag_snap.paths[p].hin_y[n] = g_paths[p].nodes[n].hin_y;
            g_canvas_drag_snap.paths[p].hout_x[n] = g_paths[p].nodes[n].hout_x;
            g_canvas_drag_snap.paths[p].hout_y[n] = g_paths[p].nodes[n].hout_y;
            if (g_paths[p].cache.node_s) {
                g_canvas_drag_snap.paths[p].node_s[n] = g_paths[p].cache.node_s[n];
            } else {
                g_canvas_drag_snap.paths[p].node_s[n] = 0.0;
            }
        }
        int sc = g_paths[p].span_count;
        if (sc > 256) sc = 256;
        g_canvas_drag_snap.paths[p].span_count = sc;
        for (int s = 0; s < sc; ++s) {
            g_canvas_drag_snap.paths[p].span_off_c[s] = g_paths[p].spans[s].off_c;
            g_canvas_drag_snap.paths[p].span_off_d[s] = g_paths[p].spans[s].off_d;
        }
    }
}

void apply_canvas_transform(int old_w, int old_h, int new_w, int new_h, float unchk_shift_x, float unchk_shift_y, bool is_resize) {
    float sx = 1.0f, sy = 1.0f;
    if (is_resize && old_w > 0 && old_h > 0) {
        sx = (float)new_w / (float)old_w;
        sy = (float)new_h / (float)old_h;
    }

    /* 1. Elements (Frog & Coins) */
    if (g_cv_affect_element) {
        if (g_has_frog) {
            g_frog_gx = (int)roundf((float)g_canvas_drag_snap.frog_x * sx);
            g_frog_gy = (int)roundf((float)g_canvas_drag_snap.frog_y * sy);
        }
        for (int i = 0; i < g_treasure_point_count; ++i) {
            g_treasure_points[i].x = (int)roundf((float)g_canvas_drag_snap.coin_x[i] * sx);
            g_treasure_points[i].y = (int)roundf((float)g_canvas_drag_snap.coin_y[i] * sy);
        }
    } else {
        if (g_has_frog) {
            g_frog_gx = (int)roundf((float)g_canvas_drag_snap.frog_x + unchk_shift_x);
            g_frog_gy = (int)roundf((float)g_canvas_drag_snap.frog_y + unchk_shift_y);
        }
        for (int i = 0; i < g_treasure_point_count; ++i) {
            g_treasure_points[i].x = (int)roundf((float)g_canvas_drag_snap.coin_x[i] + unchk_shift_x);
            g_treasure_points[i].y = (int)roundf((float)g_canvas_drag_snap.coin_y[i] + unchk_shift_y);
        }
    }

    /* 2. Curves (Paths, Handles, Attributes) */
    if (g_cv_affect_curve) {
        for (int p = 0; p < g_path_count && p < g_canvas_drag_snap.path_count; ++p) {
            for (int n = 0; n < g_paths[p].node_count && n < g_canvas_drag_snap.paths[p].node_count; ++n) {
                g_paths[p].nodes[n].x = g_canvas_drag_snap.paths[p].node_x[n] * sx;
                g_paths[p].nodes[n].y = g_canvas_drag_snap.paths[p].node_y[n] * sy;
                g_paths[p].nodes[n].hin_x = g_canvas_drag_snap.paths[p].hin_x[n] * sx;
                g_paths[p].nodes[n].hin_y = g_canvas_drag_snap.paths[p].hin_y[n] * sy;
                g_paths[p].nodes[n].hout_x = g_canvas_drag_snap.paths[p].hout_x[n] * sx;
                g_paths[p].nodes[n].hout_y = g_canvas_drag_snap.paths[p].hout_y[n] * sy;
            }
            path_cache_invalidate(&g_paths[p]);

            if (is_resize && g_paths[p].span_count > 0 && g_canvas_drag_snap.paths[p].span_count > 0) {
                path_cache_update(&g_paths[p]);
                for (int s = 0; s < g_paths[p].span_count && s < g_canvas_drag_snap.paths[p].span_count; ++s) {
                    uint32_t cid = g_paths[p].spans[s].node_c_id;
                    uint32_t did = g_paths[p].spans[s].node_d_id;
                    int ic = bezier_path_find_node_by_id(&g_paths[p], cid);
                    int id = bezier_path_find_node_by_id(&g_paths[p], did);
                    if (ic >= 0 && id >= 0 && ic < g_canvas_drag_snap.paths[p].node_count && id < g_canvas_drag_snap.paths[p].node_count) {
                        double ds_old = g_canvas_drag_snap.paths[p].node_s[id] - g_canvas_drag_snap.paths[p].node_s[ic];
                        double ds_new = 0.0;
                        if (g_paths[p].cache.node_s) {
                            ds_new = g_paths[p].cache.node_s[id] - g_paths[p].cache.node_s[ic];
                        }
                        if (ds_old > 1e-4 && ds_new > 1e-4) {
                            float r = (float)(ds_new / ds_old);
                            g_paths[p].spans[s].off_c = g_canvas_drag_snap.paths[p].span_off_c[s] * r;
                            g_paths[p].spans[s].off_d = g_canvas_drag_snap.paths[p].span_off_d[s] * r;
                        }
                    }
                }
                path_cache_invalidate(&g_paths[p]);
            }
        }
    } else {
        for (int p = 0; p < g_path_count && p < g_canvas_drag_snap.path_count; ++p) {
            for (int n = 0; n < g_paths[p].node_count && n < g_canvas_drag_snap.paths[p].node_count; ++n) {
                g_paths[p].nodes[n].x = g_canvas_drag_snap.paths[p].node_x[n] + unchk_shift_x;
                g_paths[p].nodes[n].y = g_canvas_drag_snap.paths[p].node_y[n] + unchk_shift_y;
                g_paths[p].nodes[n].hin_x = g_canvas_drag_snap.paths[p].hin_x[n];
                g_paths[p].nodes[n].hin_y = g_canvas_drag_snap.paths[p].hin_y[n];
                g_paths[p].nodes[n].hout_x = g_canvas_drag_snap.paths[p].hout_x[n];
                g_paths[p].nodes[n].hout_y = g_canvas_drag_snap.paths[p].hout_y[n];
            }
            for (int s = 0; s < g_paths[p].span_count && s < g_canvas_drag_snap.paths[p].span_count; ++s) {
                g_paths[p].spans[s].off_c = g_canvas_drag_snap.paths[p].span_off_c[s];
                g_paths[p].spans[s].off_d = g_canvas_drag_snap.paths[p].span_off_d[s];
            }
            path_cache_invalidate(&g_paths[p]);
        }
    }

    /* 3. Overlays (Cutouts, Sprites, BackgroundAlphas) */
    if (g_cv_affect_overlay) {
        for (int i = 0; i < g_cutout_count; ++i) {
            g_cutouts[i].x = (int)roundf((float)g_canvas_drag_snap.cutout_x[i] * sx);
            g_cutouts[i].y = (int)roundf((float)g_canvas_drag_snap.cutout_y[i] * sy);
            if (is_resize && g_cutouts[i].has_img && g_canvas_drag_snap.cutout_w[i] > 0 && g_canvas_drag_snap.cutout_h[i] > 0) {
                int nw = (int)roundf((float)g_canvas_drag_snap.cutout_w[i] * sx);
                int nh = (int)roundf((float)g_canvas_drag_snap.cutout_h[i] * sy);
                if (nw < 1) nw = 1;
                if (nh < 1) nh = 1;
                if (g_cutouts[i].img.width != nw || g_cutouts[i].img.height != nh) {
                    image_resize_in_place(&g_cutouts[i].img, nw, nh);
                    cutout_generate_masks(&g_cutouts[i]);
                }
            }
        }
        for (int i = 0; i < g_sprite_count; ++i) {
            g_sprites[i].x = (int)roundf((float)g_canvas_drag_snap.sprite_x[i] * sx);
            g_sprites[i].y = (int)roundf((float)g_canvas_drag_snap.sprite_y[i] * sy);
            if (is_resize && g_sprites[i].has_img && g_canvas_drag_snap.sprite_w[i] > 0 && g_canvas_drag_snap.sprite_h[i] > 0) {
                int nw = (int)roundf((float)g_canvas_drag_snap.sprite_w[i] * sx);
                int nh = (int)roundf((float)g_canvas_drag_snap.sprite_h[i] * sy);
                if (nw < 1) nw = 1;
                if (nh < 1) nh = 1;
                if (g_sprites[i].img.width != nw || g_sprites[i].img.height != nh) {
                    image_resize_in_place(&g_sprites[i].img, nw, nh);
                }
            }
        }
        for (int i = 0; i < g_bg_alpha_count; ++i) {
            g_bg_alphas[i].x = (int)roundf((float)g_canvas_drag_snap.bgalpha_x[i] * sx);
            g_bg_alphas[i].y = (int)roundf((float)g_canvas_drag_snap.bgalpha_y[i] * sy);
            if (is_resize && g_bg_alphas[i].has_img && g_canvas_drag_snap.bgalpha_w[i] > 0 && g_canvas_drag_snap.bgalpha_h[i] > 0) {
                int nw = (int)roundf((float)g_canvas_drag_snap.bgalpha_w[i] * sx);
                int nh = (int)roundf((float)g_canvas_drag_snap.bgalpha_h[i] * sy);
                if (nw < 1) nw = 1;
                if (nh < 1) nh = 1;
                if (g_bg_alphas[i].img.width != nw || g_bg_alphas[i].img.height != nh) {
                    image_resize_in_place(&g_bg_alphas[i].img, nw, nh);
                    bgalpha_generate_masks(&g_bg_alphas[i]);
                }
            }
        }
    } else {
        for (int i = 0; i < g_cutout_count; ++i) {
            g_cutouts[i].x = (int)roundf((float)g_canvas_drag_snap.cutout_x[i] + unchk_shift_x);
            g_cutouts[i].y = (int)roundf((float)g_canvas_drag_snap.cutout_y[i] + unchk_shift_y);
            if (is_resize && g_cutouts[i].has_img && g_canvas_drag_snap.cutout_w[i] > 0 && g_canvas_drag_snap.cutout_h[i] > 0) {
                if (g_cutouts[i].img.width != g_canvas_drag_snap.cutout_w[i] || g_cutouts[i].img.height != g_canvas_drag_snap.cutout_h[i]) {
                    image_resize_in_place(&g_cutouts[i].img, g_canvas_drag_snap.cutout_w[i], g_canvas_drag_snap.cutout_h[i]);
                    cutout_generate_masks(&g_cutouts[i]);
                }
            }
        }
        for (int i = 0; i < g_sprite_count; ++i) {
            g_sprites[i].x = (int)roundf((float)g_canvas_drag_snap.sprite_x[i] + unchk_shift_x);
            g_sprites[i].y = (int)roundf((float)g_canvas_drag_snap.sprite_y[i] + unchk_shift_y);
            if (is_resize && g_sprites[i].has_img && g_canvas_drag_snap.sprite_w[i] > 0 && g_canvas_drag_snap.sprite_h[i] > 0) {
                if (g_sprites[i].img.width != g_canvas_drag_snap.sprite_w[i] || g_sprites[i].img.height != g_canvas_drag_snap.sprite_h[i]) {
                    image_resize_in_place(&g_sprites[i].img, g_canvas_drag_snap.sprite_w[i], g_canvas_drag_snap.sprite_h[i]);
                }
            }
        }
        for (int i = 0; i < g_bg_alpha_count; ++i) {
            g_bg_alphas[i].x = (int)roundf((float)g_canvas_drag_snap.bgalpha_x[i] + unchk_shift_x);
            g_bg_alphas[i].y = (int)roundf((float)g_canvas_drag_snap.bgalpha_y[i] + unchk_shift_y);
            if (is_resize && g_bg_alphas[i].has_img && g_canvas_drag_snap.bgalpha_w[i] > 0 && g_canvas_drag_snap.bgalpha_h[i] > 0) {
                if (g_bg_alphas[i].img.width != g_canvas_drag_snap.bgalpha_w[i] || g_bg_alphas[i].img.height != g_canvas_drag_snap.bgalpha_h[i]) {
                    image_resize_in_place(&g_bg_alphas[i].img, g_canvas_drag_snap.bgalpha_w[i], g_canvas_drag_snap.bgalpha_h[i]);
                    bgalpha_generate_masks(&g_bg_alphas[i]);
                }
            }
        }
    }
}

void apply_canvas_drag_shift(float shift_x, float shift_y) {
    apply_canvas_transform(g_canvas_w, g_canvas_h, g_canvas_w, g_canvas_h, shift_x, shift_y, false);
}

void resize_canvas_with_transform(HWND hwnd, int new_w, int new_h) {
    if (new_w <= 0 || new_h <= 0) return;
    int old_w = g_canvas_w;
    int old_h = g_canvas_h;
    if (old_w == new_w && old_h == new_h) return;

    snapshot_canvas_drag_state();
    set_canvas_size(hwnd, new_w, new_h);
    apply_canvas_transform(old_w, old_h, new_w, new_h, 0.0f, 0.0f, true);
    InvalidateRect(hwnd, NULL, FALSE);
}

void toggle_canvas_mode(HWND hwnd) {
    if (!g_has_canvas) {
        show_new_canvas_dialog(hwnd);
        return;
    }

    g_mode_canvas = !g_mode_canvas;
    if (g_mode_canvas) {
        if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
        if (g_mode_element) {
            g_mode_element = false;
            g_placing_frog = false;
            g_placing_coin = false;
            if (g_btn_element) InvalidateRect(g_btn_element, NULL, TRUE);
        }
        if (g_mode_curve) {
            g_mode_curve = false;
            if (g_btn_curve) InvalidateRect(g_btn_curve, NULL, TRUE);
        }
        if (g_mode_overlay) {
            g_mode_overlay = false;
            g_placing_cutout = false;
            g_placing_sprite = false;
            g_placing_bgalpha = false;
            g_placing_bgalpha_vector = false;
            g_editing_bgalpha_idx = -1;
            free_pending_cutout();
            free_pending_sprite();
            free_pending_bgalpha();
            if (g_btn_overlay) InvalidateRect(g_btn_overlay, NULL, TRUE);
        }
        deselect_all_objects();
        deselect_all_canvas_handles();
        g_hud_canvas_guide_scroll_y = 0;
        g_hud_canvas_tools_scroll_y = 0;
        g_hovered_canvas_handle = CANVAS_HANDLE_NONE;
        g_drag_canvas_handle = CANVAS_HANDLE_NONE;
    } else {
        deselect_all_canvas_handles();
        g_hovered_canvas_handle = CANVAS_HANDLE_NONE;
        g_drag_canvas_handle = CANVAS_HANDLE_NONE;
        if (g_edit_cv_w) ShowWindow(g_edit_cv_w, SW_HIDE);
        if (g_edit_cv_h) ShowWindow(g_edit_cv_h, SW_HIDE);
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
}

void toggle_element_mode(HWND hwnd) {
    if (!g_has_canvas) {
        show_new_canvas_dialog(hwnd);
        return;
    }

    g_mode_element = !g_mode_element;
    if (g_mode_element) {
        if (g_mode_canvas) {
            g_mode_canvas = false;
            if (g_edit_cv_w) ShowWindow(g_edit_cv_w, SW_HIDE);
            if (g_edit_cv_h) ShowWindow(g_edit_cv_h, SW_HIDE);
        }
        if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
        if (g_mode_curve) {
            g_mode_curve = false;
        }
        if (g_mode_overlay) {
            g_mode_overlay = false;
            g_placing_cutout = false;
            g_placing_sprite = false;
            g_placing_bgalpha = false;
            g_placing_bgalpha_vector = false;
            g_editing_bgalpha_idx = -1;
            g_hud_overlay_guide_scroll_y = 0;
            g_hud_overlay_tools_scroll_y = 0;
            free_pending_cutout();
            free_pending_sprite();
            free_pending_bgalpha();
            if (g_edit_prio) ShowWindow(g_edit_prio, SW_HIDE);
        }
        g_hud_element_guide_scroll_y = 0;
        g_hud_element_tools_scroll_y = 0;
    } else {
        g_placing_frog = false;
        g_placing_coin = false;
        deselect_all_objects();
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
}

void update_window_title(void) {
    if (!g_hwnd_main) return;
    if (!g_has_canvas) {
        SetWindowTextA(g_hwnd_main, "ZuPather - Zuma Path Editor");
        return;
    }
    char title_buf[256];
    if (g_canvas_title[0] && _stricmp(g_canvas_title, g_canvas_name) != 0) {
        snprintf(title_buf, sizeof(title_buf), "ZuPather - Zuma Path Editor - [%s (%s)]", g_canvas_name, g_canvas_title);
    } else {
        snprintf(title_buf, sizeof(title_buf), "ZuPather - Zuma Path Editor - [%s]", g_canvas_name);
    }
    SetWindowTextA(g_hwnd_main, title_buf);
}

static SaveProgressFn g_save_progress_fn = NULL;

void set_save_progress_handler(SaveProgressFn fn) {
    g_save_progress_fn = fn;
}

void update_save_progress(const char *status_msg, int pct) {
    if (g_save_progress_fn) {
        g_save_progress_fn(status_msg, pct);
    }
}

static OpenProgressFn g_open_progress_fn = NULL;

void set_open_progress_handler(OpenProgressFn fn) {
    g_open_progress_fn = fn;
}

void update_open_progress(const char *status_msg, int pct) {
    if (g_open_progress_fn) {
        g_open_progress_fn(status_msg, pct);
    }
}

/* Zuma Deluxe & User Preferences Configuration */
char g_zuma_levels_dir[MAX_PATH] = {0};
char g_zuma_root_dir[MAX_PATH] = {0};
bool g_has_zuma_dir = false;

static void get_zupather_config_path(char *out_path, size_t max_len) {
    if (!out_path || max_len == 0) return;
    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *p1 = strrchr(exe_dir, '\\');
    char *p2 = strrchr(exe_dir, '/');
    char *s = (p2 && (!p1 || p2 > p1)) ? p2 : p1;
    if (s) *(s + 1) = '\0';
    char canvas_dir[MAX_PATH] = {0};
    snprintf(canvas_dir, sizeof(canvas_dir), "%scanvas", exe_dir);
    CreateDirectoryA(canvas_dir, NULL);
    snprintf(out_path, max_len, "%scanvas\\zupathconf.inf", exe_dir);
}

void load_zuma_config(void) {
    char ini_path[MAX_PATH] = {0};
    get_zupather_config_path(ini_path, sizeof(ini_path));

    char buf[MAX_PATH] = {0};
    GetPrivateProfileStringA("Zuma", "LevelsDir", "", buf, sizeof(buf), ini_path);

    if (buf[0] != '\0') {
        char xml_check[MAX_PATH];
        snprintf(xml_check, sizeof(xml_check), "%s\\levels.xml", buf);
        if (GetFileAttributesA(xml_check) != INVALID_FILE_ATTRIBUTES) {
            strncpy(g_zuma_levels_dir, buf, sizeof(g_zuma_levels_dir) - 1);
            g_has_zuma_dir = true;
            strncpy(g_zuma_root_dir, buf, sizeof(g_zuma_root_dir) - 1);
            char *r1 = strrchr(g_zuma_root_dir, '\\');
            char *r2 = strrchr(g_zuma_root_dir, '/');
            char *rs = (r2 && (!r1 || r2 > r1)) ? r2 : r1;
            if (rs) *rs = '\0';
        }
    }

    /* Load Theme (Dark or Light Mode) */
    char theme_buf[32] = {0};
    GetPrivateProfileStringA("Preferences", "Theme", "", theme_buf, sizeof(theme_buf), ini_path);
    if (theme_buf[0] != '\0') {
        if (_stricmp(theme_buf, "dark") == 0 || strcmp(theme_buf, "1") == 0) {
            g_current_theme = THEME_DARK;
        } else if (_stricmp(theme_buf, "light") == 0 || strcmp(theme_buf, "0") == 0) {
            g_current_theme = THEME_LIGHT;
        }
    }

    /* Load Grid Mode toggle */
    /* Load Performance & Undo preferences */
    int blit_val = GetPrivateProfileIntA("Preferences", "PerformanceDirectBlit", -1, ini_path);
    if (blit_val != -1) g_perf_direct_blit = (blit_val != 0);

    int cache_val = GetPrivateProfileIntA("Preferences", "PerformanceHudCache", -1, ini_path);
    if (cache_val != -1) g_perf_hud_cache = (cache_val != 0);

    int smooth_val = GetPrivateProfileIntA("Preferences", "PerformanceSmoothScroll", -1, ini_path);
    if (smooth_val != -1) g_perf_smooth_scroll = (smooth_val != 0);

    int hist_val = GetPrivateProfileIntA("Preferences", "ActionHistoryLimit", -1, ini_path);
    if (hist_val >= 10 && hist_val <= 256) g_undo_limit = hist_val;

    /* Load Grid Mode toggle */
    int grid_val = GetPrivateProfileIntA("Preferences", "Grid", -1, ini_path);
    if (grid_val == 0) {
        g_show_grid = false;
    } else if (grid_val == 1) {
        g_show_grid = true;
    }
}

void save_zuma_config(void) {
    char ini_path[MAX_PATH] = {0};
    get_zupather_config_path(ini_path, sizeof(ini_path));

    WritePrivateProfileStringA("Zuma", "LevelsDir", g_zuma_levels_dir, ini_path);
    WritePrivateProfileStringA("Zuma", "RootDir", g_zuma_root_dir, ini_path);
    const char *pref_str = "system";
    if (g_theme_preference == THEME_PREF_DARK) pref_str = "dark";
    else if (g_theme_preference == THEME_PREF_LIGHT) pref_str = "light";
    WritePrivateProfileStringA("Preferences", "Theme", pref_str, ini_path);
    WritePrivateProfileStringA("Preferences", "Grid", g_show_grid ? "1" : "0", ini_path);
    char num_buf[32];
    WritePrivateProfileStringA("Preferences", "PerformanceDirectBlit", g_perf_direct_blit ? "1" : "0", ini_path);
    WritePrivateProfileStringA("Preferences", "PerformanceHudCache", g_perf_hud_cache ? "1" : "0", ini_path);
    WritePrivateProfileStringA("Preferences", "PerformanceSmoothScroll", g_perf_smooth_scroll ? "1" : "0", ini_path);
    snprintf(num_buf, sizeof(num_buf), "%d", g_undo_limit);
    WritePrivateProfileStringA("Preferences", "ActionHistoryLimit", num_buf, ini_path);
    WritePrivateProfileStringA(NULL, NULL, NULL, ini_path); /* Flush cache to disk */
}

/* Splash Screen State & Assets */
CanvasImage g_splash_img = {0};
bool g_has_splash_img = false;

RECT g_splash_rc_github = {0};
RECT g_splash_rc_new = {0};
RECT g_splash_rc_open = {0};
RECT g_splash_rc_map = {0};
int g_splash_hover_item = 0;

void load_splash_image(void) {
    if (g_has_splash_img && g_splash_img.hbm) return;

    /* 1. Try loading from compiled-in executable resource (101) */
    if (image_load_from_resource(101, &g_splash_img)) {
        g_has_splash_img = true;
        return;
    }

    /* 2. Fallback to file assets/media/ZuSPLASHscreen.jpg */
    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *p1 = strrchr(exe_dir, '\\');
    char *p2 = strrchr(exe_dir, '/');
    char *s = (p2 && (!p1 || p2 > p1)) ? p2 : p1;
    if (s) *(s + 1) = '\0';

    char splash_path[MAX_PATH];
    snprintf(splash_path, sizeof(splash_path), "%sassets\\media\\ZuSPLASHscreen.jpg", exe_dir);
    if (image_load_from_file(splash_path, &g_splash_img)) {
        g_has_splash_img = true;
        return;
    }

    if (image_load_from_file("assets\\media\\ZuSPLASHscreen.jpg", &g_splash_img)) {
        g_has_splash_img = true;
        return;
    }

    if (image_load_from_file("D:\\Files\\Project\\Reverse Engineer\\ZuPather - Zuma Deluxe Path Maker\\assets\\media\\ZuSPLASHscreen.jpg", &g_splash_img)) {
        g_has_splash_img = true;
        return;
    }
}

void free_splash_image(void) {
    if (g_has_splash_img) {
        image_free(&g_splash_img);
        g_has_splash_img = false;
    }
}

void draw_github_svg_logo(HDC hdc, int dest_x, int dest_y, int dest_size, COLORREF color) {
    static const float s_gh_segs[25][6] = {
        { 5.370f, 0.000f, 0.000f, 5.370f, 0.000f, 12.000f },
        { 0.000f, 17.310f, 3.435f, 21.795f, 8.205f, 23.385f },
        { 8.805f, 23.490f, 9.030f, 23.130f, 9.030f, 22.815f },
        { 9.030f, 22.530f, 9.015f, 21.585f, 9.015f, 20.580f },
        { 6.000f, 21.135f, 5.220f, 19.845f, 4.980f, 19.170f },
        { 4.845f, 18.825f, 4.260f, 17.760f, 3.750f, 17.475f },
        { 3.330f, 17.250f, 2.730f, 16.695f, 3.735f, 16.680f },
        { 4.680f, 16.665f, 5.355f, 17.550f, 5.580f, 17.910f },
        { 6.660f, 19.725f, 8.385f, 19.215f, 9.075f, 18.900f },
        { 9.180f, 18.120f, 9.495f, 17.595f, 9.840f, 17.295f },
        { 7.170f, 16.995f, 4.380f, 15.960f, 4.380f, 11.370f },
        { 4.380f, 10.065f, 4.845f, 8.985f, 5.610f, 8.145f },
        { 5.490f, 7.845f, 5.070f, 6.615f, 5.730f, 4.965f },
        { 5.730f, 4.965f, 6.735f, 4.650f, 9.030f, 6.195f },
        { 9.990f, 5.925f, 11.010f, 5.790f, 12.030f, 5.790f },
        { 13.050f, 5.790f, 14.070f, 5.925f, 15.030f, 6.195f },
        { 17.325f, 4.635f, 18.330f, 4.965f, 18.330f, 4.965f },
        { 18.990f, 6.615f, 18.570f, 7.845f, 18.450f, 8.145f },
        { 19.215f, 8.985f, 19.680f, 10.065f, 19.680f, 11.370f },
        { 19.680f, 15.975f, 16.875f, 16.995f, 14.205f, 17.295f },
        { 14.640f, 17.670f, 15.015f, 18.390f, 15.015f, 19.515f },
        { 15.015f, 21.120f, 15.000f, 22.410f, 15.000f, 22.815f },
        { 15.000f, 23.130f, 15.225f, 23.505f, 15.825f, 23.385f },
        { 20.695f, 21.752f, 24.000f, 17.165f, 24.000f, 12.000f },
        { 24.000f, 5.370f, 18.630f, 0.000f, 12.000f, 0.000f }
    };

    float scale = (float)dest_size / 24.0f;
    HBRUSH br = CreateSolidBrush(color);
    HBRUSH old_br = (HBRUSH)SelectObject(hdc, br);
    HPEN old_pen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));

    BeginPath(hdc);
    MoveToEx(hdc, (int)roundf(dest_x + 12.0f * scale), (int)roundf(dest_y + 0.0f * scale), NULL);

    for (int i = 0; i < 25; ++i) {
        POINT pts[3];
        pts[0].x = (int)roundf(dest_x + s_gh_segs[i][0] * scale);
        pts[0].y = (int)roundf(dest_y + s_gh_segs[i][1] * scale);
        pts[1].x = (int)roundf(dest_x + s_gh_segs[i][2] * scale);
        pts[1].y = (int)roundf(dest_y + s_gh_segs[i][3] * scale);
        pts[2].x = (int)roundf(dest_x + s_gh_segs[i][4] * scale);
        pts[2].y = (int)roundf(dest_y + s_gh_segs[i][5] * scale);
        PolyBezierTo(hdc, pts, 3);
    }

    CloseFigure(hdc);
    EndPath(hdc);

    SetPolyFillMode(hdc, WINDING);
    FillPath(hdc);

    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_br);
    DeleteObject(br);
}





/* ============================================================================
 * Application Error Logging & Crash Handling Implementation
 * ============================================================================ */
static bool s_error_log_session_active = false;

static void get_error_log_path(char *out_path, size_t max_len) {
    char exe_path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    char *p_slash = strrchr(exe_path, '\\');
    if (!p_slash) p_slash = strrchr(exe_path, '/');
    if (p_slash) {
        *p_slash = '\0';
        snprintf(out_path, max_len, "%s\\error.txt", exe_path);
    } else {
        snprintf(out_path, max_len, "error.txt");
    }
}

static FILE* open_session_error_log(void) {
    char log_path[MAX_PATH];
    get_error_log_path(log_path, sizeof(log_path));

    const char *mode = s_error_log_session_active ? "a" : "w";
    FILE *f = fopen(log_path, mode);
    if (!f) return NULL;

    if (!s_error_log_session_active) {
        s_error_log_session_active = true;
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "=======================================================\n");
        fprintf(f, " ZuPather Error & Crash Diagnostic Log\n");
        fprintf(f, " Session Started : %04d-%02d-%02d %02d:%02d:%02d\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fprintf(f, " Version         : %s\n", ZUPATHER_VERSION_STR);
#if defined(_WIN64)
        fprintf(f, " Architecture    : 64-bit (x64)\n");
#else
        fprintf(f, " Architecture    : 32-bit (x86)\n");
#endif
        fprintf(f, " File Location   : %s\n", log_path);
        fprintf(f, "=======================================================\n\n");
    }
    return f;
}

void log_app_error(const char *category, const char *fmt, ...) {
    FILE *f = open_session_error_log();
    if (!f) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            category ? category : "ERROR");

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fflush(f);
    fclose(f);
}

#else
void update_window_theme_attributes(HWND hwnd, bool is_dark) {
    (void)hwnd; (void)is_dark;
    DrawMenuBar(hwnd);
}
void apply_control_dark_mode(HWND hwnd_ctrl, bool is_dark) {
    (void)hwnd_ctrl; (void)is_dark;
}
#endif

#if defined(_WIN32) && !defined(__APPLE__)
static LONG WINAPI crash_exception_filter(EXCEPTION_POINTERS *ep) {
    FILE *f = open_session_error_log();
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);

        DWORD code = ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
        void *addr = ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : NULL;

        fprintf(f, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(f, " FATAL CRASH / UNHANDLED EXCEPTION DETECTED\n");
        fprintf(f, " Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fprintf(f, " Exception Code: 0x%08lX", (unsigned long)code);

        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:
                fprintf(f, " (EXCEPTION_ACCESS_VIOLATION)");
                if (ep->ExceptionRecord && ep->ExceptionRecord->NumberParameters >= 2) {
                    ULONG_PTR access_type = ep->ExceptionRecord->ExceptionInformation[0];
                    ULONG_PTR target_addr = ep->ExceptionRecord->ExceptionInformation[1];
                    const char *op = (access_type == 0) ? "read" : (access_type == 1 ? "write" : "execute");
                    fprintf(f, "\n Access Violation Detail: Attempted to %s memory address 0x%p", op, (void*)target_addr);
                }
                break;
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: fprintf(f, " (EXCEPTION_ARRAY_BOUNDS_EXCEEDED)"); break;
            case EXCEPTION_DATATYPE_MISALIGNMENT: fprintf(f, " (EXCEPTION_DATATYPE_MISALIGNMENT)"); break;
            case EXCEPTION_FLT_DIVIDE_BY_ZERO: fprintf(f, " (EXCEPTION_FLT_DIVIDE_BY_ZERO)"); break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO: fprintf(f, " (EXCEPTION_INT_DIVIDE_BY_ZERO)"); break;
            case EXCEPTION_ILLEGAL_INSTRUCTION: fprintf(f, " (EXCEPTION_ILLEGAL_INSTRUCTION)"); break;
            case EXCEPTION_IN_PAGE_ERROR: fprintf(f, " (EXCEPTION_IN_PAGE_ERROR)"); break;
            case EXCEPTION_NONCONTINUABLE_EXCEPTION: fprintf(f, " (EXCEPTION_NONCONTINUABLE_EXCEPTION)"); break;
            case EXCEPTION_STACK_OVERFLOW: fprintf(f, " (EXCEPTION_STACK_OVERFLOW)"); break;
            default: break;
        }
        fprintf(f, "\n Faulting Instruction Address: 0x%p\n", addr);

        /* Context Record (CPU Registers) */
        if (ep->ContextRecord) {
            CONTEXT *ctx = ep->ContextRecord;
#if defined(_WIN64)
            fprintf(f, "\nCPU Registers (x64):\n");
            fprintf(f, " RAX: 0x%016llX  RBX: 0x%016llX  RCX: 0x%016llX  RDX: 0x%016llX\n",
                    (unsigned long long)ctx->Rax, (unsigned long long)ctx->Rbx,
                    (unsigned long long)ctx->Rcx, (unsigned long long)ctx->Rdx);
            fprintf(f, " RSI: 0x%016llX  RDI: 0x%016llX  RBP: 0x%016llX  RSP: 0x%016llX\n",
                    (unsigned long long)ctx->Rsi, (unsigned long long)ctx->Rdi,
                    (unsigned long long)ctx->Rbp, (unsigned long long)ctx->Rsp);
            fprintf(f, " RIP: 0x%016llX  EFLAGS: 0x%08X\n",
                    (unsigned long long)ctx->Rip, (unsigned int)ctx->EFlags);
#elif defined(_M_IX86) || defined(__i386__)
            fprintf(f, "\nCPU Registers (x86):\n");
            fprintf(f, " EAX: 0x%08lX  EBX: 0x%08lX  ECX: 0x%08lX  EDX: 0x%08lX\n",
                    (unsigned long)ctx->Eax, (unsigned long)ctx->Ebx,
                    (unsigned long)ctx->Ecx, (unsigned long)ctx->Edx);
            fprintf(f, " ESI: 0x%08lX  EDI: 0x%08lX  EBP: 0x%08lX  ESP: 0x%08lX\n",
                    (unsigned long)ctx->Esi, (unsigned long)ctx->Edi,
                    (unsigned long)ctx->Ebp, (unsigned long)ctx->Esp);
            fprintf(f, " EIP: 0x%08lX  EFLAGS: 0x%08lX\n",
                    (unsigned long)ctx->Eip, (unsigned long)ctx->EFlags);
#endif
        }

        /* Stack Backtrace */
        void *stack_addrs[32];
        WORD captured = CaptureStackBackTrace(0, 32, stack_addrs, NULL);
        if (captured > 0) {
            fprintf(f, "\nStack Backtrace (%u frames):\n", captured);
            for (WORD i = 0; i < captured; ++i) {
                fprintf(f, "  [%02u] 0x%p\n", i, stack_addrs[i]);
            }
        }

        /* Application Context Diagnostics */
        fprintf(f, "\nApplication State at Time of Crash:\n");
        fprintf(f, " Active Canvas Name  : %s\n", g_canvas_name[0] ? g_canvas_name : "(none)");
        fprintf(f, " Active Canvas Title : %s\n", g_canvas_title[0] ? g_canvas_title : "(none)");
        fprintf(f, " Canvas Dimensions   : %d x %d\n", g_canvas_w, g_canvas_h);
        fprintf(f, " Paths Count         : %d\n", g_path_count);
        fprintf(f, " Cutouts Count       : %d\n", g_cutout_count);
        fprintf(f, " Sprites Count       : %d\n", g_sprite_count);
        fprintf(f, " BG Alphas Count     : %d\n", g_bg_alpha_count);
        fprintf(f, " Mode: Canvas=%s, Overlay=%s, Element=%s, Curve=%s\n",
                g_mode_canvas ? "true" : "false",
                g_mode_overlay ? "true" : "false",
                g_mode_element ? "true" : "false",
                g_mode_curve ? "true" : "false");
        fprintf(f, " Affect Overlay      : %s\n", g_cv_affect_overlay ? "true" : "false");
        fprintf(f, " Zoom Level          : %.2f%%\n", g_zoom * 100.0f);
        fprintf(f, " Pan Coordinates     : (%.1f, %.1f)\n", g_pan_x, g_pan_y);
        fprintf(f, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n");
        fflush(f);
        fclose(f);
    }

    char log_path[MAX_PATH];
    get_error_log_path(log_path, sizeof(log_path));

    char alert_msg[600];
    snprintf(alert_msg, sizeof(alert_msg),
        "ZuPather encountered an unhandled exception (0x%08lX).\n\n"
        "A detailed error log has been generated at:\n"
        "%s\n\n"
        "The application will now safely terminate.",
        (unsigned long)(ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0),
        log_path
    );
    MessageBoxA(NULL, alert_msg, "ZuPather - Fatal Application Error", MB_ICONERROR | MB_OK | MB_TASKMODAL);

    return EXCEPTION_EXECUTE_HANDLER;
}

void init_error_handler(void) {
    SetUnhandledExceptionFilter(crash_exception_filter);
}
#elif defined(__APPLE__)
void init_error_handler(void) {
    zp_macos_install_crash_handler();
}
#endif
