#ifndef ZUPATHER_STATE_H
#define ZUPATHER_STATE_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _MSC_VER
#define MAYBE_UNUSED
#define strncasecmp _strnicmp
#define strcasecmp  _stricmp
#else
#define MAYBE_UNUSED __attribute__((unused))
#endif

#include "image_loader.h"
#include "bezier_path.h"
#include "svg_path.h"
#include "level_progression.h"

/* Undocumented UAH (User32 Aero Hooks) Window Messages & Structures for Dark Menu Bar */
#ifndef WM_UAHDRAWMENU
#define WM_UAHDRAWMENU        0x0091
#endif
#ifndef WM_UAHDRAWMENUITEM
#define WM_UAHDRAWMENUITEM    0x0092
#endif
#ifndef WM_UAHMEASUREMENUITEM
#define WM_UAHMEASUREMENUITEM 0x0094
#endif

typedef union tagUAHMENUITEMMETRICS {
    struct {
        DWORD cx;
        DWORD cy;
    } rgsizeBar[2];
    struct {
        DWORD cx;
        DWORD cy;
    } rgsizePopup[4];
} UAHMENUITEMMETRICS;

typedef struct tagUAHMENUPOPUPMETRICS {
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2;
} UAHMENUPOPUPMETRICS;

typedef struct tagUAHMENU {
    HMENU hmenu;
    HDC hdc;
    DWORD dwFlags;
} UAHMENU;

typedef struct tagUAHMENUITEM {
    int iPosition;
    UAHMENUITEMMETRICS umim;
    UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;

typedef struct tagUAHDRAWMENUITEM {
    DRAWITEMSTRUCT dis;
    UAHMENU um;
    UAHMENUITEM umi;
} UAHDRAWMENUITEM;

typedef struct tagUAHMEASUREMENUITEM {
    MEASUREITEMSTRUCT mis;
    UAHMENU um;
    UAHMENUITEM umi;
} UAHMEASUREMENUITEM;

typedef enum {
    AppMode_Default = 0,
    AppMode_AllowDark = 1,
    AppMode_ForceDark = 2,
    AppMode_ForceLight = 3,
    AppMode_Max = 4
} PreferredAppMode;

typedef PreferredAppMode(WINAPI *pfnSetPreferredAppMode)(PreferredAppMode appMode);
typedef void(WINAPI *pfnFlushMenuThemes)(void);

#define DEFAULT_CANVAS_WIDTH   640
#define DEFAULT_CANVAS_HEIGHT  480
#define TOOLBAR_HEIGHT         40

/* Command & Control IDs */
#define ID_STATUSBAR              101
#define ID_MENU_FILE_NEW          201
#define ID_MENU_FILE_OPEN         202
#define ID_MENU_FILE_SAVE         203
#define ID_MENU_FILE_EXIT         204
#define ID_MENU_SET_ZUMA_FOLDER   205
#define ID_MENU_FILE_MANAGE_LEVELS 206
#define ID_MENU_FILE_SPLASH       207
#define ID_MENU_EDIT_UNDO         210
#define ID_MENU_EDIT_REDO         211
#define ID_MENU_ACTION_HISTORY_BASE 7000
#define ID_MENU_ACTION_HISTORY_MAX  (ID_MENU_ACTION_HISTORY_BASE + 64)
#define ID_MENU_VIEW_ZOOMIN       301
#define ID_MENU_VIEW_ZOOMOUT      302
#define ID_MENU_VIEW_100          303
#define ID_MENU_VIEW_RESET        304
#define ID_MENU_VIEW_FIT          305
#define ID_MENU_VIEW_GRID         306
#define ID_MENU_THEME_LIGHT       307
#define ID_MENU_THEME_DARK        308
#define ID_MENU_THEME_TOGGLE      309

#define ID_MENU_CANVAS_NEW        310
#define ID_MENU_CANVAS_OPEN       311
#define ID_MENU_CANVAS_SAVE       312
#define ID_MENU_CANVAS_CHANGE_IMG 313
#define ID_MENU_CANVAS_REMOVE_IMG 314
#define ID_MENU_CANVAS_640_480    315
#define ID_MENU_CANVAS_800_600    316
#define ID_MENU_CANVAS_1024_768   317
#define ID_MENU_CANVAS_1280_720   318
#define ID_MENU_CANVAS_CUSTOM     319
#define ID_MENU_CANVAS_SET_FROG   320
#define ID_MENU_CANVAS_ADD_COIN   321
#define ID_MENU_CANVAS_CURVE      322
#define ID_MENU_CURVE_SELECT_ALL  323
#define ID_MENU_CURVE_INVERT_SEL  324
#define ID_MENU_CURVE_IMPORT_SVG  325
#define ID_MENU_CURVE_EXPORT_SVG  326
#define ID_MENU_CANVAS_ADD_CUTOUT 327
#define ID_MENU_CANVAS_ADD_SPRITE 328
#define ID_MENU_CANVAS_OVERLAY    329
#define ID_OVERLAY_ADD_BGALPHA    330
#define ID_MENU_CANVAS_ELEMENT    331
#define ID_MENU_DESELECT_ALL      332
#define ID_OVERLAY_SET_VARIABLE   333
#define ID_MENU_CANVAS_MODE       334
#define ID_MENU_LEVEL_PREVIEW     335
#define ID_MENU_SELECT_ALL        ID_MENU_CURVE_SELECT_ALL
#define ID_MENU_INVERT_SEL        ID_MENU_CURVE_INVERT_SEL

#define ID_BTN_FROG               401
#define ID_BTN_COIN               402
#define ID_BTN_CURVE              403
#define ID_BTN_OVERLAY            404
#define ID_BTN_CUTOUT             405
#define ID_BTN_SPRITE             406
#define ID_BTN_ELEMENT            407
#define ID_BTN_CANVAS             408
#define ID_BTN_MANAGE_MAPS        409
#define ID_EDIT_CANVAS_W          411
#define ID_EDIT_CANVAS_H          412
#define ID_BTN_CANVAS_APPLY       413
#define ID_EDIT_PRIO              414

#define ID_EDIT_CV_W              415
#define ID_EDIT_CV_H              416
#define ID_BTN_CV_APPLY           417
#define ID_BTN_CV_PRESET_640      418
#define ID_BTN_CV_PRESET_800      419
#define ID_BTN_CV_PRESET_1024     420
#define ID_BTN_CV_PRESET_1280     421
#define ID_BTN_CV_FIT_ORIG        422
#define ID_BTN_CV_FIT_FIT         423
#define ID_BTN_CV_FIT_FILL        424
#define ID_BTN_CV_FIT_STRETCH     425
#define ID_BTN_CV_ALIGN_TL        426
#define ID_BTN_CV_ALIGN_TC        427
#define ID_BTN_CV_ALIGN_TR        428
#define ID_BTN_CV_ALIGN_ML        429
#define ID_BTN_CV_ALIGN_MC        430
#define ID_BTN_CV_ALIGN_MR        431
#define ID_BTN_CV_ALIGN_BL        432
#define ID_BTN_CV_ALIGN_BC        433
#define ID_BTN_CV_ALIGN_BR        434
#define ID_EDIT_PREVIEW_TITLE     435
#define ID_EDIT_MAP_SKULLROT      436
#define ID_EDIT_DEV_CURVECOLOR    437
#define ID_EDIT_DEV_BGCOLOR       438
#define ID_EDIT_DEV_HUEROT        439
#define ID_EDIT_DEV_TREASUREFREQ  440
#define ID_EDIT_DEV_PARTIME       441

/* New Canvas Modal Control IDs */
#define ID_DLG_EDIT_NAME          501
#define ID_DLG_EDIT_TITLE         509
#define ID_DLG_EDIT_W             502
#define ID_DLG_EDIT_H             503
#define ID_DLG_EDIT_IMGPATH       504
#define ID_DLG_BTN_BROWSE         505
#define ID_DLG_BTN_CLEAR_IMG      506
#define ID_DLG_BTN_CREATE         507
#define ID_DLG_BTN_CANCEL         508

/* Save Canvas Modal Control IDs */
#define ID_SAVE_EDIT_NAME         520
#define ID_SAVE_EDIT_TITLE        528
#define ID_SAVE_CHK_ORIG          521
#define ID_SAVE_COMBO_FORMAT      522
#define ID_SAVE_CHK_RESIZE        525
#define ID_SAVE_BTN_SAVE          526
#define ID_SAVE_BTN_CANCEL        527
#define ID_SAVE_LBL_ORIG_EXT      529
#define ID_SAVE_COMBO_DIFF        530
#define ID_SAVE_LBL_PROG_ID       531
#define IDT_MEDIA_ANIMATION       1001

/* Open Canvas Modal Control IDs */
#define ID_OPEN_LIST_FOLDERS      540
#define ID_OPEN_BTN_SELECT        541
#define ID_OPEN_BTN_RENAME        542
#define ID_OPEN_BTN_DELETE        543
#define ID_OPEN_BTN_REFRESH       544
#define ID_OPEN_BTN_CANCEL        545

/* Rename Canvas Modal Control IDs */
#define ID_RENAME_EDIT_NAME       550
#define ID_RENAME_EDIT_TITLE      553
#define ID_RENAME_BTN_OK          551
#define ID_RENAME_BTN_CANCEL      552

/* Manage Levels Modal Control IDs */
#define ID_MANAGE_BTN_SET_FOLDER   4801
#define ID_MANAGE_LIST_ZUMA_LEVELS 4802
#define ID_MANAGE_BTN_IMPORT       4803
#define ID_MANAGE_LIST_CANVASES    4804
#define ID_MANAGE_BTN_EXPORT       4805
#define ID_MANAGE_BTN_CLOSE        4806
#define ID_MANAGE_BTN_REFRESH      4807

/* Manage Levels Context Menus & Sub-Modals */
#define ID_ZUMA_CTX_RENAME         4811
#define ID_ZUMA_CTX_DELETE         4812
#define ID_CANVAS_CTX_OPEN         4821
#define ID_CANVAS_CTX_RENAME       4822
#define ID_CANVAS_CTX_DELETE       4823
#define ID_RENAME_ZUMA_EDIT_ID     4831
#define ID_RENAME_ZUMA_BTN_OK      4832
#define ID_RENAME_ZUMA_BTN_CANCEL  4833
#define ID_RENAME_EXP_EDIT_NAME    4841
#define ID_RENAME_EXP_BTN_OK       4842
#define ID_RENAME_EXP_BTN_CANCEL   4843

#define RIGHT_PANEL_WIDTH         254
#define TOOLBAR_NEEDED_WIDTH      620
#define ZUPATHER_VERSION_STR      "v0.1.4.1"

/* Embedded Resource IDs */
#define IDR_SPLASH_JPG              101
#define IDR_GITHUB_SVG              102
#define IDR_FONT_ATKINSON_REGULAR   103
#define IDR_FONT_ATKINSON_BOLD      104
#define IDR_FONT_JETBRAINS_REGULAR  105
#define IDR_FONT_JETBRAINS_BOLD     106
#define IDR_SPRITE_FROG             107
#define IDR_SPRITE_COIN             108
#define IDR_SPRITE_SKULL            109
#define IDR_SPRITE_BALL_BASE        110

/* Font Face Names */
#define FONT_PRIMARY_NAME   "Atkinson Hyperlegible"
#define FONT_SECONDARY_NAME "JetBrains Mono"

/* Menu Item Typography & Owner-Draw Structure */
typedef struct MenuItemVisual {
    char name[128];
    char shortcut[64];
    UINT id;
    HMENU hsubmenu;
    bool is_separator;
    bool is_bold;
} MenuItemVisual;

/* Theme Definition */
typedef enum {
    THEME_LIGHT = 0,
    THEME_DARK  = 1
} AppTheme;

typedef struct {
    COLORREF workspace_bg;
    COLORREF canvas_bg;
    COLORREF canvas_border;
    COLORREF canvas_grid_major;
    COLORREF canvas_grid_minor;
    COLORREF workspace_grid_major;
    COLORREF workspace_grid_minor;
    COLORREF canvas_crosshair;
    COLORREF bar_bg;
    COLORREF bar_border;
    COLORREF btn_bg;
    COLORREF btn_bg_pressed;
    COLORREF btn_border;
    COLORREF btn_text;
    COLORREF text_primary;
    COLORREF text_secondary;
    COLORREF text_accent_blue;
    COLORREF text_accent_green;
    COLORREF text_accent_red;
    COLORREF text_dim;
} ThemeColors;
/* Treasure Points (Coins) State */
#define MAX_TREASURE_POINTS 32
typedef struct {
    int x;
    int y;
    int dist1;
    bool selected;
} TreasurePoint;

/* Cutout, Sprite, and BackgroundAlpha Overlay Elements State */
#define MAX_CUTOUTS 16
#define MAX_SPRITES 16
#define MAX_BG_ALPHAS 16

typedef struct {
    int x;                          /* Top-Left X coordinate on canvas */
    int y;                          /* Top-Left Y coordinate on canvas */
    int pri;                        /* Layer priority (unused for Zuma BackgroundAlpha) */
    int vx;                         /* Horizontal animation velocity (-1, 0, 1) */
    int vy;                         /* Vertical animation velocity (-1, 0, 1) */
    char image_name[64];            /* Image basename without extension (e.g. "bgalpha", "water") */
    char source_filepath[MAX_PATH]; /* Original source image file on disk */
    CanvasImage img;                /* Loaded image asset */
    bool has_img;
    HBITMAP hbm_mask_light;         /* 12.5% black mask */
    HDC     hdc_mask_light;
    HBITMAP hbm_mask_dark;          /* 12.5% white mask */
    HDC     hdc_mask_dark;
    bool selected;
} LevelBackgroundAlpha;

typedef struct {
    int x;                          /* Top-Left X coordinate on canvas */
    int y;                          /* Top-Left Y coordinate on canvas */
    int pri;                        /* Rendering layer priority (default 1, min 0, max 99) */
    char image_name[64];            /* Image basename without extension (e.g. "tunnel01") */
    char source_filepath[MAX_PATH]; /* Original source image file on disk */
    CanvasImage img;                /* Loaded image asset */
    bool has_img;
    HBITMAP hbm_mask_light;         /* Transformed mask: White transparent, Black 25% black */
    HDC     hdc_mask_light;
    HBITMAP hbm_mask_dark;          /* Transformed mask: White transparent, Black 25% white */
    HDC     hdc_mask_dark;
    bool selected;
} LevelCutout;

typedef struct {
    int x;                          /* Top-Left X coordinate on canvas */
    int y;                          /* Top-Left Y coordinate on canvas */
    int pri;                        /* Rendering layer priority (default 1, min 0, max 99) */
    char image_name[64];            /* Image basename without extension (e.g. "sprite01") */
    char source_filepath[MAX_PATH]; /* Original source image file on disk */
    CanvasImage img;                /* Loaded image asset */
    bool has_img;
    bool selected;
} LevelSprite;

/* Vector Curve Path State */
#define MAX_PATHS 16
/* Curve Sub-modes: Draw Mode (Geometry) vs Variable Mode (Tunnel & Priority) */
typedef enum {
    CURVE_SUBMODE_DRAW = 0,
    CURVE_SUBMODE_VARIABLE
} CurveSubMode;

typedef enum {
    PEN_DRAG_NONE = 0,
    PEN_DRAG_NEW_HANDLE,       /* Creating new node, dragging outgoing handle */
    PEN_DRAG_ANCHOR,           /* Moving anchor */
    PEN_DRAG_HANDLE_IN,        /* Dragging incoming handle */
    PEN_DRAG_HANDLE_OUT,       /* Dragging outgoing handle */
    PEN_DRAG_MARQUEE,          /* Shift+drag marquee box */
    PEN_DRAG_INSERT_NODE       /* Inserting node on curve segment, optional immediate drag */
} PenDragState;

/* Marquee Selection Modes & Snapshot */
typedef enum {
    MARQUEE_MODE_REPLACE = 0,   /* Ctrl: replace selection */
    MARQUEE_MODE_ADD,           /* Ctrl + Shift: add to selection */
    MARQUEE_MODE_SUBTRACT,      /* Alt + Shift: deselect / subtract from selection */
    MARQUEE_MODE_TOGGLE         /* Ctrl + Alt + Shift: toggle / invert selection */
} MarqueeSelectMode;

/* Canvas Objects Selection & Dragging State */
typedef enum {
    OBJ_DRAG_NONE = 0,
    OBJ_DRAG_MARQUEE
} ObjDragState;

/* Canvas Mode Interactive Handles */
typedef enum {
    CANVAS_HANDLE_NONE = 0,
    CANVAS_HANDLE_TOP,
    CANVAS_HANDLE_BOTTOM,
    CANVAS_HANDLE_LEFT,
    CANVAS_HANDLE_RIGHT,
    CANVAS_HANDLE_TOP_LEFT,
    CANVAS_HANDLE_TOP_RIGHT,
    CANVAS_HANDLE_BOTTOM_LEFT,
    CANVAS_HANDLE_BOTTOM_RIGHT,
    CANVAS_HANDLE_CENTER_MOVE,
    CANVAS_HANDLE_CENTER_DONUT
} CanvasHandleType;


/* Global State Variables (Declared extern) */
extern HFONT g_font_primary_regular;
extern HFONT g_font_primary_bold;
extern HFONT g_font_title_bold;
extern HFONT g_font_mono_regular;
extern HFONT g_font_mono_bold;

extern const ThemeColors g_theme_light;
extern const ThemeColors g_theme_dark;
extern AppTheme g_current_theme;
extern HMENU g_hmenu_action_history;

extern bool        g_has_canvas;
extern char        g_canvas_name[128];
extern char        g_canvas_title[128];
extern int         g_canvas_w;
extern int         g_canvas_h;
extern bool        g_in_canvas_edit_sync;
extern bool        g_has_frog;
extern int         g_frog_gx;
extern int         g_frog_gy;
extern bool        g_placing_frog;
extern bool        g_frog_hovered;
extern bool        g_frog_selected;
extern int         g_mouse_screen_x;
extern int         g_mouse_screen_y;
extern CanvasImage g_frog_img;
extern bool        g_has_frog_img;
extern CanvasImage g_canvas_img;
extern bool        g_has_bg_image;

extern TreasurePoint g_treasure_points[MAX_TREASURE_POINTS];
extern int           g_treasure_point_count;
extern bool          g_placing_coin;
extern bool          g_picked_up_coin;
extern int           g_hovered_coin_idx;
extern CanvasImage   g_coin_img;
extern bool          g_has_coin_img;

extern LevelBackgroundAlpha g_bg_alphas[MAX_BG_ALPHAS];
extern int           g_bg_alpha_count;
extern bool          g_placing_bgalpha;
extern bool          g_picked_up_bgalpha;
extern bool          g_placing_bgalpha_vector;
extern int           g_hovered_bgalpha_idx;
extern int           g_selected_bgalpha_idx;
extern int           g_editing_bgalpha_idx;
extern int           g_bgalpha_anchor_x;
extern int           g_bgalpha_anchor_y;
extern int           g_bgalpha_pending_vx;
extern int           g_bgalpha_pending_vy;
extern CanvasImage   g_default_bgalpha_img;
extern bool          g_has_default_bgalpha_img;
extern HBITMAP       g_default_bgalpha_mask_light;
extern HDC           g_default_bgalpha_hdc_light;
extern HBITMAP       g_default_bgalpha_mask_dark;
extern HDC           g_default_bgalpha_hdc_dark;
extern CanvasImage   g_pending_bgalpha_img;
extern bool          g_has_pending_bgalpha_img;
extern char          g_pending_bgalpha_filepath[MAX_PATH];

extern LevelCutout   g_cutouts[MAX_CUTOUTS];
extern int           g_cutout_count;
extern bool          g_placing_cutout;
extern bool          g_picked_up_cutout;
extern int           g_hovered_cutout_idx;
extern int           g_selected_cutout_idx;
extern int           g_pending_cutout_pri;
extern CanvasImage   g_default_cutout_img;
extern bool          g_has_default_cutout_img;
extern HBITMAP       g_default_cutout_mask_light;
extern HDC           g_default_cutout_hdc_light;
extern HBITMAP       g_default_cutout_mask_dark;
extern HDC           g_default_cutout_hdc_dark;
extern CanvasImage   g_pending_cutout_img;
extern bool          g_has_pending_cutout_img;
extern char          g_pending_cutout_filepath[MAX_PATH];
extern HBITMAP       g_pending_cutout_mask_light;
extern HDC           g_pending_cutout_hdc_light;
extern HBITMAP       g_pending_cutout_mask_dark;
extern HDC           g_pending_cutout_hdc_dark;

extern LevelSprite   g_sprites[MAX_SPRITES];
extern int           g_sprite_count;
extern bool          g_placing_sprite;
extern bool          g_picked_up_sprite;
extern int           g_hovered_sprite_idx;
extern int           g_selected_sprite_idx;
extern int           g_pending_sprite_pri;
extern CanvasImage   g_default_sprite_img;
extern bool          g_has_default_sprite_img;
extern CanvasImage   g_pending_sprite_img;
extern bool          g_has_pending_sprite_img;
extern char          g_pending_sprite_filepath[MAX_PATH];

extern bool          g_mode_canvas;
extern CanvasFitMode g_canvas_fit_mode;
extern CanvasAlignment g_canvas_alignment;
extern bool          g_hud_canvas_collapsed;
extern bool          g_hud_canvas_guide_collapsed;
extern bool          g_cv_affect_element;
extern bool          g_cv_affect_curve;
extern bool          g_cv_affect_overlay;

extern bool          g_hud_preview_guide_collapsed;
extern bool          g_hud_preview_info_collapsed;
extern bool          g_level_preview_active;
extern bool          g_level_preview_stored_state;
extern float         g_preview_anim_distance;
void sync_level_preview_state(void);
void toggle_level_preview(HWND hwnd);

typedef enum {
    MAP_ATTR_HOVER_NONE = 0,
    MAP_ATTR_HOVER_TITLE,
    MAP_ATTR_HOVER_SKULLROT,
    MAP_ATTR_HOVER_SPACE,
    MAP_ATTR_HOVER_FROG,
    MAP_ATTR_HOVER_COIN,
    MAP_ATTR_HOVER_TRACKS,
    MAP_ATTR_HOVER_CUTOUTS,
    MAP_ATTR_HOVER_SPRITES,
    MAP_ATTR_HOVER_CANVAS_SIZE,
    MAP_ATTR_HOVER_BACKGROUND,
    MAP_ATTR_HOVER_PROGRESSION,
    MAP_ATTR_HOVER_DRAWCURVE,
    MAP_ATTR_HOVER_DRAWTUNNELS,
    MAP_ATTR_HOVER_CURVECOLOR,
    MAP_ATTR_HOVER_BGCOLOR,
    MAP_ATTR_HOVER_DESTROYALL,
    MAP_ATTR_HOVER_HUEROT,
    MAP_ATTR_HOVER_TREASUREFREQ,
    MAP_ATTR_HOVER_PARTIME
} MapAttrHoverItem;

extern MapAttrHoverItem g_hud_map_attr_hover;
extern int              g_hud_map_attr_hover_y;
const char *get_map_attr_hover_status(MapAttrHoverItem item);

extern bool          g_mode_element;
extern bool          g_mode_overlay;
extern int           g_overlay_current_prio;
extern HWND          g_btn_overlay;
extern char          g_canvas_folder[MAX_PATH];

extern bool          g_hud_element_collapsed;
extern bool          g_hud_element_guide_collapsed;
extern bool          g_hud_overlay_collapsed;
extern bool          g_hud_overlay_guide_collapsed;
extern int           g_overlay_active_tab;

extern bool          g_adjusting_overlay_priority;
extern int           g_editing_prio_type; /* 0 = Cutout, 1 = Sprite */
extern int           g_editing_prio_idx;
extern int           g_orig_overlay_priority;
extern int           g_pending_overlay_priority;

extern BezierPath   g_paths[MAX_PATHS];
extern int          g_path_count;
extern int          g_active_path_idx;
extern int          g_active_node_idx;
extern bool         g_mode_curve;
extern CurveSubMode g_curve_submode;
extern bool         g_hud_mode_collapsed;
extern bool         g_hud_shortcuts_collapsed;

extern bool         g_var_write_tunnel;
extern uint8_t      g_var_tunnel_val;
extern bool         g_var_write_prio;
extern uint8_t      g_var_prio_val;
extern bool         g_var_dragging;
extern double       g_var_drag_s_start;
extern double       g_var_drag_s_cur;
extern bool         g_var_hover_active;
extern double       g_var_hover_s;
extern float        g_var_hover_x;
extern float        g_var_hover_y;
extern uint8_t      g_var_hover_tun;
extern uint8_t      g_var_hover_prio;

extern int          g_hovered_curve_seg_path;
extern int          g_hovered_curve_seg_idx;
extern float        g_hovered_curve_seg_t;
extern float        g_hovered_curve_seg_cx;
extern float        g_hovered_curve_seg_cy;

extern PenDragState g_pen_drag_state;
extern int          g_drag_path_idx;
extern int          g_drag_node_idx;
extern float        g_drag_anchor_start_x;
extern float        g_drag_anchor_start_y;
extern float        g_drag_mouse_start_cx;
extern float        g_drag_mouse_start_cy;
extern int          g_marquee_start_x;
extern int          g_marquee_start_y;
extern int          g_marquee_cur_x;
extern int          g_marquee_cur_y;

extern MarqueeSelectMode g_marquee_mode;
extern bool        *g_marquee_snapshot[MAX_PATHS];
extern int          g_marquee_snapshot_count[MAX_PATHS];

extern ObjDragState      g_obj_drag_state;
extern int               g_obj_marquee_start_x;
extern int               g_obj_marquee_start_y;
extern int               g_obj_marquee_cur_x;
extern int               g_obj_marquee_cur_y;
extern MarqueeSelectMode g_obj_marquee_mode;

extern bool g_snap_frog_selected;
extern bool g_snap_coin_selected[MAX_TREASURE_POINTS];
extern bool g_snap_cutout_selected[MAX_CUTOUTS];
extern bool g_snap_sprite_selected[MAX_SPRITES];
extern bool g_snap_bgalpha_selected[MAX_BG_ALPHAS];

extern HWND g_hwnd_main;
extern HWND g_hwnd_toolbar_left;
extern HWND g_hwnd_toolbar_right;
extern HWND g_hwnd_status;
extern HWND g_btn_frog;
extern HWND g_btn_coin;
extern HWND g_btn_element;
extern HWND g_btn_curve;
extern HWND g_edit_w;
extern HWND g_edit_h;
extern HWND g_btn_apply_size;
extern HWND g_edit_prio;
extern bool g_in_prio_update;

extern int  g_tb_scroll_x;
extern bool g_is_dragging_thumb;
extern int  g_drag_thumb_mouse_start_x;
extern int  g_drag_thumb_scroll_start;
extern bool g_is_dragging_tb_bg;
extern int  g_drag_bg_mouse_start_x;
extern int  g_drag_bg_scroll_start;

extern int  g_hud_guide_scroll_y;
extern bool g_is_dragging_guide_thumb;
extern int  g_drag_guide_mouse_start_y;
extern int  g_drag_guide_scroll_start;
extern bool g_is_dragging_guide_bg;
extern int  g_drag_guide_bg_mouse_start_y;
extern int  g_drag_guide_bg_scroll_start;

extern int  g_hud_attr_scroll_y;
extern bool g_is_dragging_attr_thumb;
extern int  g_drag_attr_mouse_start_y;
extern int  g_drag_attr_scroll_start;
extern bool g_is_dragging_attr_bg;
extern int  g_drag_attr_bg_mouse_start_y;
extern int  g_drag_attr_bg_scroll_start;

extern int  g_hud_overlay_guide_scroll_y;
extern bool g_is_dragging_ov_guide_thumb;
extern int  g_drag_ov_guide_mouse_start_y;
extern int  g_drag_ov_guide_scroll_start;
extern bool g_is_dragging_ov_guide_bg;
extern int  g_drag_ov_guide_bg_mouse_start_y;
extern int  g_drag_ov_guide_bg_scroll_start;

extern int  g_hud_overlay_tools_scroll_y;
extern bool g_is_dragging_ov_tools_thumb;
extern int  g_drag_ov_tools_mouse_start_y;
extern int  g_drag_ov_tools_scroll_start;
extern bool g_is_dragging_ov_tools_bg;
extern int  g_drag_ov_tools_bg_mouse_start_y;
extern int  g_drag_ov_tools_bg_scroll_start;

extern int  g_hud_element_guide_scroll_y;
extern bool g_is_dragging_el_guide_thumb;
extern int  g_drag_el_guide_mouse_start_y;
extern int  g_drag_el_guide_scroll_start;
extern bool g_is_dragging_el_guide_bg;
extern int  g_drag_el_guide_bg_mouse_start_y;
extern int  g_drag_el_guide_bg_scroll_start;

extern int  g_hud_element_tools_scroll_y;
extern bool g_is_dragging_el_tools_thumb;
extern int  g_drag_el_tools_mouse_start_y;
extern int  g_drag_el_tools_scroll_start;
extern bool g_is_dragging_el_tools_bg;
extern int  g_drag_el_tools_bg_mouse_start_y;
extern int  g_drag_el_tools_bg_scroll_start;

extern int  g_hud_canvas_guide_scroll_y;
extern bool g_is_dragging_cv_guide_thumb;
extern int  g_drag_cv_guide_mouse_start_y;
extern int  g_drag_cv_guide_scroll_start;
extern bool g_is_dragging_cv_guide_bg;
extern int  g_drag_cv_guide_bg_mouse_start_y;
extern int  g_drag_cv_guide_bg_scroll_start;

extern int  g_hud_canvas_tools_scroll_y;
extern bool g_is_dragging_cv_tools_thumb;
extern int  g_drag_cv_tools_mouse_start_y;
extern int  g_drag_cv_tools_scroll_start;
extern bool g_is_dragging_cv_tools_bg;
extern int  g_drag_cv_tools_bg_mouse_start_y;
extern int  g_drag_cv_tools_bg_scroll_start;

extern int  g_hud_preview_guide_scroll_y;
extern bool g_is_dragging_prev_guide_thumb;
extern int  g_drag_prev_guide_mouse_start_y;
extern int  g_drag_prev_guide_scroll_start;
extern bool g_is_dragging_prev_guide_bg;
extern int  g_drag_prev_guide_bg_mouse_start_y;
extern int  g_drag_prev_guide_bg_scroll_start;

extern int  g_hud_preview_info_scroll_y;
extern bool g_is_dragging_prev_info_thumb;
extern int  g_drag_prev_info_mouse_start_y;
extern int  g_drag_prev_info_scroll_start;
extern bool g_is_dragging_prev_info_bg;
extern int  g_drag_prev_info_bg_mouse_start_y;
extern int  g_drag_prev_info_bg_scroll_start;

#define LEVEL_PREVIEW_BALL_COLORS 11
extern CanvasImage g_ball_imgs[LEVEL_PREVIEW_BALL_COLORS];
extern bool        g_has_ball_imgs[LEVEL_PREVIEW_BALL_COLORS];
extern CanvasImage g_skull_img;
extern bool        g_has_skull_img;

extern CanvasHandleType g_hovered_canvas_handle;
extern CanvasHandleType g_drag_canvas_handle;
extern int              g_drag_start_w;
extern int              g_drag_start_h;
extern int              g_drag_start_mouse_sx;
extern int              g_drag_start_mouse_sy;
extern float            g_drag_start_radius;

extern bool g_canvas_handle_selected[10];
extern bool g_snap_canvas_handle_selected[10];

extern HWND g_btn_canvas;
extern HWND g_btn_manage_maps;
extern HWND g_edit_cv_w;
extern HWND g_edit_cv_h;
extern HWND g_edit_preview_title;

/* Map / Graphics Attributes */
extern bool  g_map_has_skullrot;
extern float g_map_skullrot;
extern bool  g_map_is_space;

/* Developer Attributes (Testing Kit) */
extern bool  g_dev_has_drawcurve;
extern bool  g_dev_drawcurve;
extern bool  g_dev_has_drawtunnels;
extern bool  g_dev_drawtunnels;
extern bool  g_dev_has_curvecolor;
extern char  g_dev_curvecolor[32];
extern bool  g_dev_has_bgcolor;
extern char  g_dev_bgcolor[32];
extern bool  g_dev_has_destroyall;
extern bool  g_dev_destroyall;
extern bool  g_dev_has_huerot;
extern float g_dev_huerot;
extern bool  g_dev_has_treasurefreq;
extern int   g_dev_treasurefreq;
extern bool  g_dev_has_partime;
extern int   g_dev_partime;

extern HWND  g_edit_map_skullrot;
extern HWND  g_edit_dev_curvecolor;
extern HWND  g_edit_dev_bgcolor;
extern HWND  g_edit_dev_huerot;
extern HWND  g_edit_dev_treasurefreq;
extern HWND  g_edit_dev_partime;

void reset_map_and_dev_attributes(void);

extern HWND g_active_modal_dlg;

extern bool  g_show_grid;
extern float g_zoom;
extern float g_pan_x;
extern float g_pan_y;
extern bool  g_is_panning;
extern POINT g_last_mouse_pos;

extern float g_canvas_mouse_x;
extern float g_canvas_mouse_y;
extern bool  g_mouse_inside_canvas;

/* Functions in zupather_state.c */
const ThemeColors *get_theme(void);
void draw_menu_nc_bottom_line(HWND hWnd, COLORREF bg_color);
void update_window_theme_attributes(HWND hwnd, bool is_dark);
void apply_control_dark_mode(HWND hwnd_ctrl, bool is_dark);
void load_frog_sprite(void);
void load_coin_sprite(void);
void free_cutout(LevelCutout *co);
void free_sprite(LevelSprite *sp);
void free_bgalpha(LevelBackgroundAlpha *ba);
void sync_cutout_image_names(void);
void sync_sprite_image_names(void);
void sync_bgalpha_image_names(void);
void free_pending_cutout(void);
void free_pending_sprite(void);
void free_pending_bgalpha(void);
void generate_cutout_masks_internal(
    HBITMAP src_hbm, int w, int h,
    HBITMAP *out_hbm_light, HDC *out_hdc_light,
    HBITMAP *out_hbm_dark, HDC *out_hdc_dark
);
void cutout_generate_masks(LevelCutout *co);
void generate_bgalpha_masks_internal(
    HBITMAP src_hbm, int w, int h,
    HBITMAP *out_hbm_light, HDC *out_hdc_light,
    HBITMAP *out_hbm_dark, HDC *out_hdc_dark
);
void bgalpha_generate_masks(LevelBackgroundAlpha *ba);
void load_default_cutout_sprite(void);
void load_default_sprite_sprite(void);
void load_default_bgalpha_sprite(void);
void load_level_preview_assets(void);
void enter_preview_mode(HWND hwnd);
void set_canvas_title_sanitized(const char *raw_title);
void sync_canvas_title_everywhere(const char *new_title, HWND sender_hwnd);
bool resolve_cutout_image_in_folder(const char *folder, const char *image_name, CanvasImage *out_img);
bool resolve_sprite_image_in_folder(const char *folder, const char *image_name, CanvasImage *out_img);
bool resolve_bgalpha_image_in_folder(const char *folder, const char *image_name, CanvasImage *out_img);
void toggle_overlay_mode(HWND hwnd);
void trigger_add_cutout(HWND hwnd);
void trigger_add_sprite(HWND hwnd);
void trigger_add_bgalpha(HWND hwnd);
void init_fonts(void);
void destroy_fonts(void);
int get_workspace_height(HWND hwnd);
void get_canvas_screen_rect(int client_w, int work_h, RECT *out_rc);
void screen_to_canvas(int client_w, int work_h, int sx, int sy, float *out_cx, float *out_cy);
void update_status_bar(void);
void zoom_to_fit(HWND hwnd);
void set_canvas_size(HWND hwnd, int new_w, int new_h);
void free_marquee_snapshot(void);
void deselect_all_objects(void);
int get_selected_object_count(void);
int get_active_selected_bgalpha_idx(void);
int get_active_selected_cutout_idx(void);
int get_active_selected_sprite_idx(void);
void start_overlay_priority_adjustment(HWND hwnd, int type, int idx);
void confirm_overlay_priority_adjustment(HWND hwnd);
void cancel_overlay_priority_adjustment(HWND hwnd);
void toggle_overlay_variable_mode(HWND hwnd);
bool cycle_overlay_selection_at(HWND hwnd, float cx, float cy);
void snapshot_object_selection(void);
void snap_vector_45_deg(float dx, float dy, float *out_dx, float *out_dy);
void handle_select_all(HWND hwnd);
void handle_invert_selection(HWND hwnd);
void handle_deselect_all(HWND hwnd);
void toggle_canvas_mode(HWND hwnd);
void shift_all_canvas_objects(int dx, int dy);
void deselect_all_canvas_handles(void);
void select_all_canvas_handles(void);
void invert_canvas_handle_selection(void);
bool is_any_canvas_handle_selected(void);
int get_selected_canvas_handle_count(void);
void snapshot_canvas_handle_selection(void);
void get_canvas_handle_screen_point(int client_w, int work_h, CanvasHandleType ht, int *out_x, int *out_y);
void update_canvas_handle_marquee_selection(int min_sx, int min_sy, int max_sx, int max_sy, int client_w, int work_h);

#define MAX_BEZIER_NODES 1024

typedef struct {
    int canvas_w, canvas_h;
    int frog_x, frog_y;
    int coin_x[MAX_TREASURE_POINTS], coin_y[MAX_TREASURE_POINTS];
    int cutout_x[MAX_CUTOUTS], cutout_y[MAX_CUTOUTS];
    int cutout_w[MAX_CUTOUTS], cutout_h[MAX_CUTOUTS];
    int sprite_x[MAX_SPRITES], sprite_y[MAX_SPRITES];
    int sprite_w[MAX_SPRITES], sprite_h[MAX_SPRITES];
    int bgalpha_x[MAX_BG_ALPHAS], bgalpha_y[MAX_BG_ALPHAS];
    int bgalpha_w[MAX_BG_ALPHAS], bgalpha_h[MAX_BG_ALPHAS];
    float pan_x, pan_y;
    int path_count;
    struct {
        int node_count;
        float node_x[MAX_BEZIER_NODES];
        float node_y[MAX_BEZIER_NODES];
        float hin_x[MAX_BEZIER_NODES];
        float hin_y[MAX_BEZIER_NODES];
        float hout_x[MAX_BEZIER_NODES];
        float hout_y[MAX_BEZIER_NODES];
        double node_s[MAX_BEZIER_NODES];
        int span_count;
        float span_off_c[256];
        float span_off_d[256];
    } paths[MAX_PATHS];
} CanvasDragSnapshot;

extern CanvasDragSnapshot g_canvas_drag_snap;
void snapshot_canvas_drag_state(void);
void apply_canvas_drag_shift(float shift_x, float shift_y);
void apply_canvas_transform(int old_w, int old_h, int new_w, int new_h, float unchk_shift_x, float unchk_shift_y, bool is_resize);
void resize_canvas_with_transform(HWND hwnd, int new_w, int new_h);

void toggle_element_mode(HWND hwnd);
void refresh_toolbar_buttons(void);
void update_window_title(void);

/* Save & Open Progress Feedback System */
typedef void (*SaveProgressFn)(const char *status_msg, int pct);
void set_save_progress_handler(SaveProgressFn fn);
void update_save_progress(const char *status_msg, int pct);

typedef void (*OpenProgressFn)(const char *status_msg, int pct);
void set_open_progress_handler(OpenProgressFn fn);
void update_open_progress(const char *status_msg, int pct);

/* Zuma Deluxe Directory Configuration */
extern char g_zuma_levels_dir[MAX_PATH];
extern char g_zuma_root_dir[MAX_PATH];
extern bool g_has_zuma_dir;

void load_zuma_config(void);
void save_zuma_config(void);

/* Splash Screen State & Assets */
extern CanvasImage g_splash_img;
extern bool g_has_splash_img;
void load_splash_image(void);
void free_splash_image(void);
void draw_github_svg_logo(HDC hdc, int dest_x, int dest_y, int dest_size, COLORREF color);

/* Splash Screen Hit-testing (Shared for Workspace when !g_has_canvas) */
extern RECT g_splash_rc_github;
extern RECT g_splash_rc_new;
extern RECT g_splash_rc_open;
extern RECT g_splash_rc_map;
extern int g_splash_hover_item; /* 0=none, 1=github, 2=new, 3=open, 4=map */

#endif /* ZUPATHER_STATE_H */
