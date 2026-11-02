#include "layout_editor.h"
#include "bezier_path.h"
#include "zupather_state.h"
#include "dialog_modals.h"
#include "image_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#if defined(_WIN32) && !defined(__APPLE__)
#include <windowsx.h>
#endif

/* Maximum layout elements supported across all 17 sections */
#define MAX_LAYOUT_ELEMENTS 256

/* Attribute bit flags for lossless XML representation */
typedef enum {
    ATTR_HAS_X           = (1 << 0),
    ATTR_HAS_Y           = (1 << 1),
    ATTR_HAS_W           = (1 << 2),
    ATTR_HAS_H           = (1 << 3),
    ATTR_HAS_DOOR_X      = (1 << 4),
    ATTR_HAS_DOOR_Y      = (1 << 5),
    ATTR_HAS_FROG_X      = (1 << 6),
    ATTR_HAS_FROG_Y      = (1 << 7),
    ATTR_HAS_CENTER_X    = (1 << 8),
    ATTR_HAS_CENTER_Y    = (1 << 9),
    ATTR_HAS_OFFSET_X    = (1 << 10),
    ATTR_HAS_OFFSET_Y    = (1 << 11),
    ATTR_HAS_MIN_TOP     = (1 << 12),
    ATTR_HAS_SPACING     = (1 << 13),
    ATTR_HAS_ID          = (1 << 14),
    ATTR_HAS_ALIGN       = (1 << 15),
    ATTR_HAS_LIVES_X     = (1 << 16),
    ATTR_HAS_STAGE_CX    = (1 << 17),
    ATTR_HAS_SCORE_RX    = (1 << 18),
    ATTR_HAS_ZUMA_X      = (1 << 19),
    ATTR_HAS_MENU_X      = (1 << 20),
    ATTR_HAS_TOP         = (1 << 21),
    ATTR_HAS_BOTTOM      = (1 << 22),
    ATTR_HAS_LEFT        = (1 << 23),
    ATTR_HAS_RIGHT       = (1 << 24),
    ATTR_HAS_COL1_X      = (1 << 25),
    ATTR_HAS_COL2_X      = (1 << 26),
    ATTR_HAS_BX          = (1 << 27),
    ATTR_HAS_CX          = (1 << 28),
    ATTR_HAS_DX          = (1 << 29),
    ATTR_IS_DOOR_PAIR    = (1 << 30),
    ATTR_HAS_LINESPACING = 0x80000000U
} LayoutAttrFlags;

/* Element Hotspot Mode for anchor alignment (TopLeft default vs Center on specific axes vs BottomCenter) */
typedef enum {
    HOTSPOT_TOPLEFT       = 0,
    HOTSPOT_CENTER_X      = (1 << 0),
    HOTSPOT_CENTER_Y      = (1 << 1),
    HOTSPOT_CENTER_BOTH   = (HOTSPOT_CENTER_X | HOTSPOT_CENTER_Y),
    HOTSPOT_BOTTOM_CENTER = (1 << 2)
} ElementHotspotMode;

typedef struct {
    char section[48];       /* e.g. "AdventureDoors", "MainMenu" */
    char tag[48];           /* e.g. "Door", "AdventureBtn" */
    char id[16];            /* e.g. "0", "1", ... */
    char align[32];         /* e.g. "center", "top-center" */
    uint32_t flags;

    /* Canonical 640x480 coordinate attributes */
    int x, y, w, h;
    int doorX, doorY, frogX, frogY;
    int centerX, centerY;
    int offsetX, offsetY;
    int minTop, spacing, lineSpacing;
    bool use_width_height_names;
    int livesX, stageCX, scoreRX, zumaX, menuX;
    int top, bottom, left, right;
    int col1X, col2X;
    int bx, cx, dx;

    /* Visual default dimensions when w/h not defined */
    int def_w, def_h;
    bool has_explicit_size;  /* True if explicit width/height was in XML */
    ElementHotspotMode hotspot; /* Hotspot mode: default TOPLEFT */
    bool is_conditional;

    /* Editor State */
    bool selected;
    RECT canvas_rect;       /* Primary bounding box in canvas viewport */
    RECT frog_canvas_rect;  /* Secondary handle for frog waypoint in doors */
} LayoutElement;

/* Screen Category Definition */
typedef struct {
    const char *name;
    const char *filter_section; /* NULL means multiple or custom */
    const char *bg_image_name;
    const char *description;
} LayoutScreenCategory;

static const LayoutScreenCategory g_screens[] = {
    { "Loading Screen",                  "LoadingScreen",    "loadingscreen.jpg",   "Initial game bootstrap and loading bar" },
    { "Main Menu",                       "MainMenu",         "mmscreen.jpg",        "Title screen, menu buttons and eye glow" },
    { "Adventure Map (Temples & Doors)", NULL,               "advback.jpg",         "Temples 1-5, doors 0-12, buttons and labels" },
    { "Gauntlet Screen",                 NULL,               "gauntscreen.jpg",     "Practice doors 0-3, buttons and rank tags" },
    { "In-Game HUD & Menubar",           "InGameHUD",        "levels\\Blackswirley\\BlackSwirley.jpg", "Lives, stage, score, zuma bar & margins" },
    { "Stage Complete Cutscene",         "StageComplete",    "levels\\Blackswirley\\BlackSwirley.jpg", "Stage banner text, complete text & frog hop" },
    { "Temple Cleared (Congratulations)","TempleCleared",    "levels\\Blackswirley\\BlackSwirley.jpg", "Tiki god dialogue, continue button & frog" },
    { "More Games Screen",               "MoreGames",        "moregamesscreen.jpg", "Thumbnail quadrants and back button" },
    { "Help & Instructions Screen",      "HelpScreen",       "helpback.jpg",        "Stone tablet tutorial widgets and icons" },
    { "End Credits Screen",              "CreditsScreen",    "happyending.jpg",     "Rolling staff credits & whatever button" },
    { "Universal Dialog Windows",        "Dialogs",          "dialogbox.jpg",       "Modal dialog positioning and top bounds" },
    { "DevTester Help & Cheats",         "DevHelpScreen",    "helpback_dev.jpg",    "Cheats manual, columns & interactive demo" },
    { "Complete Layout XML (All)",       NULL,               NULL,                  "Full list of all elements across 17 sections" }
};
#define NUM_SCREENS ((int)(sizeof(g_screens) / sizeof(g_screens[0])))

/* Sprite Cache with True PopCap Alpha Blending & Sprite Strip Slicing */
typedef struct {
    char name[64];
    CanvasImage img;
    bool loaded;
    bool from_customres; /* True if loaded from imagescustomres\ (pre-scaled) */
    int cols;           /* Horizontal cell division (e.g. 4 for doors, 3 for buttons) */
    int rows;           /* Vertical cell division (e.g. 50 for balls, 12 for pitcover) */
    int default_cell;   /* Default cell index to render in preview */
} LayoutSprite;

#define MAX_CACHED_SPRITES 96
static LayoutSprite g_sprite_cache[MAX_CACHED_SPRITES];
static int g_sprite_cache_count = 0;

/* Global Editor Dialog State */
typedef struct {
    HWND hwnd_dlg;
    HWND hwnd_parent;
    HWND hwnd_canvas;

    /* Top Bar Controls */
    HWND hwnd_combo_screen;
    HWND hwnd_combo_filter;
    HWND hwnd_lbl_res_info;
    HWND hwnd_btn_save;
    HWND hwnd_btn_reload;
    HWND hwnd_btn_close;

    /* Right Inspector Controls */
    HWND hwnd_list_elements;
    HWND hwnd_edit_tag;
    HWND hwnd_edit_section;
    HWND hwnd_edit_x;
    HWND hwnd_edit_y;
    HWND hwnd_edit_frog_x;
    HWND hwnd_edit_frog_y;
    HWND hwnd_edit_w;
    HWND hwnd_edit_h;
    HWND hwnd_edit_center_x;
    HWND hwnd_edit_center_y;
    HWND hwnd_edit_offset_x;
    HWND hwnd_edit_offset_y;
    HWND hwnd_edit_align;
    HWND hwnd_edit_spacing;
    HWND hwnd_lbl_coords;

    /* Themed Brushes */
    HBRUSH hbr_dlg_bg;
    HBRUSH hbr_panel_bg;
    HBRUSH hbr_edit_bg;
    HBRUSH hbr_list_bg;

    /* Data Elements */
    char layout_filepath[MAX_PATH];
    LayoutElement elements[MAX_LAYOUT_ELEMENTS];
    int element_count;
    int cur_screen_idx;
    int cur_sub_filter_idx;
    int hud_space_w;
    int hud_space_h;
    int main_menu_space_w;
    int main_menu_space_h;
    int loading_space_w;
    int loading_space_h;
    int adventure_space_w;
    int adventure_space_h;
    int gauntlet_space_w;
    int gauntlet_space_h;
    char section_space[32][64];
    char xml_header[4096];
    bool show_conditional_sprites;
    HWND hwnd_chk_conditional;

    /* Active Display Resolution (Strictly locked to Zuma.exe) */
    int target_w;
    int target_h;
    int detected_w;
    int detected_h;
    bool has_detected_res;

    /* Background image loaded for current screen */
    CanvasImage bg_image;
    bool has_bg_image;
    char current_bg_name[64];

    /* Interaction State */
    LayoutElement *held_elem;
    int held_handle;        /* 0: primary rect (or door), 1: frog handle */
    bool is_picked_up;      /* Angkat mode (follows cursor without mouse holding) */
    bool is_mouse_dragging; /* Standard drag & drop mode */
    int drag_start_mx;
    int drag_start_my;
    int drag_orig_x;
    int drag_orig_y;
    int drag_orig_center_x;
    int drag_orig_center_y;
    int drag_orig_frog_x;
    int drag_orig_frog_y;

    /* Marquee Selection Box (Ctrl + Drag) */
    bool marquee_active;
    POINT marquee_start;
    POINT marquee_curr;

    /* Hover Badge */
    LayoutElement *hovered_elem;
    int hovered_handle;
    int hover_canvas_x;
    int hover_canvas_y;

    /* Sync & Updating guards */
    bool syncing_inspector;
    bool updating_controls;
} LayoutEditorState;

static LayoutEditorState *g_layout_editor = NULL;

/* Control IDs */
#define ID_LE_COMBO_SCREEN      2101
#define ID_LE_COMBO_FILTER      2102
#define ID_LE_CHK_CONDITIONAL   2105
#define ID_LE_BTN_SAVE          2106
#define ID_LE_BTN_RELOAD        2107
#define ID_LE_BTN_CLOSE         2108
#define ID_LE_LIST_ELEMENTS     2109

#define ID_LE_EDIT_X            2110
#define ID_LE_EDIT_Y            2111
#define ID_LE_EDIT_FROG_X       2112
#define ID_LE_EDIT_FROG_Y       2113
#define ID_LE_EDIT_W            2114
#define ID_LE_EDIT_H            2115
#define ID_LE_EDIT_CENTER_X     2116
#define ID_LE_EDIT_CENTER_Y     2117
#define ID_LE_EDIT_OFFSET_X     2118
#define ID_LE_EDIT_OFFSET_Y     2119
#define ID_LE_EDIT_ALIGN        2120
#define ID_LE_EDIT_SPACING      2121

/* Forward declarations */
static void layout_editor_sync_inspector_from_selection(LayoutEditorState *st);
static void layout_editor_update_screen_elements_list(LayoutEditorState *st);
static void layout_editor_update_sub_filter_options(LayoutEditorState *st);
static void layout_editor_load_screen_background(LayoutEditorState *st);
static bool layout_xml_load(LayoutEditorState *st, const char *filepath);
static bool layout_xml_save(LayoutEditorState *st, const char *filepath);
static bool detect_zuma_native_resolution(int *out_w, int *out_h);
static bool is_element_in_screen(const LayoutElement *el, int screen_idx);
static bool is_element_in_sub_filter(const LayoutElement *el, int screen_idx, int sub_idx);
static bool is_element_visible_in_editor(const LayoutElement *el, const LayoutEditorState *st);
static LRESULT CALLBACK LayoutEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK LayoutCanvasProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static int get_canonical_section_index(const char *name);

/* Helper function to find Zuma Deluxe game root directory by climbing up parent directories */
static bool get_zuma_game_root_dir(char *out_dir, size_t max_len) {
    if (!out_dir || max_len == 0) return false;

    /* 1. If g_zuma_root_dir is already set, verify it */
    if (g_zuma_root_dir[0] != '\0') {
        char test_exe[MAX_PATH];
        char test_res[MAX_PATH];
        char test_res_ex[MAX_PATH];
        snprintf(test_exe, sizeof(test_exe), "%s\\Zuma.exe", g_zuma_root_dir);
        snprintf(test_res, sizeof(test_res), "%s\\properties\\resources.xml", g_zuma_root_dir);
        snprintf(test_res_ex, sizeof(test_res_ex), "%s\\properties\\resources_extra.xml", g_zuma_root_dir);
        if (GetFileAttributesA(test_exe) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesA(test_res) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesA(test_res_ex) != INVALID_FILE_ATTRIBUTES) {
            strncpy(out_dir, g_zuma_root_dir, max_len - 1);
            out_dir[max_len - 1] = '\0';
            return true;
        }
    }

    /* 2. Start from g_zuma_levels_dir or current working directory and climb upwards */
    char start_path[MAX_PATH] = {0};
    if (g_zuma_levels_dir[0] != '\0') {
        strncpy(start_path, g_zuma_levels_dir, sizeof(start_path) - 1);
    } else {
        GetCurrentDirectoryA(sizeof(start_path), start_path);
    }

    char current[MAX_PATH];
    if (GetFullPathNameA(start_path, sizeof(current), current, NULL) == 0) {
        strncpy(current, start_path, sizeof(current) - 1);
    }

    for (int iter = 0; iter < 6; ++iter) {
        char test_exe[MAX_PATH];
        char test_res[MAX_PATH];
        char test_res_ex[MAX_PATH];
        snprintf(test_exe, sizeof(test_exe), "%s\\Zuma.exe", current);
        snprintf(test_res, sizeof(test_res), "%s\\properties\\resources.xml", current);
        snprintf(test_res_ex, sizeof(test_res_ex), "%s\\properties\\resources_extra.xml", current);

        if (GetFileAttributesA(test_exe) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesA(test_res) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesA(test_res_ex) != INVALID_FILE_ATTRIBUTES) {
            strncpy(out_dir, current, max_len - 1);
            out_dir[max_len - 1] = '\0';
            /* Cache into g_zuma_root_dir */
            strncpy(g_zuma_root_dir, current, sizeof(g_zuma_root_dir) - 1);
            g_zuma_root_dir[sizeof(g_zuma_root_dir) - 1] = '\0';
            return true;
        }

        /* Climb one level up */
        char parent[MAX_PATH];
        snprintf(parent, sizeof(parent), "%s\\..", current);
        char resolved[MAX_PATH];
        if (GetFullPathNameA(parent, sizeof(resolved), resolved, NULL) == 0 ||
            _stricmp(resolved, current) == 0) {
            break; /* Reached filesystem root */
        }
        strncpy(current, resolved, sizeof(current) - 1);
    }

    return false;
}

/* Check whether properties/layout.xml is available in the linked Zuma folder */
bool layout_editor_is_available(char *out_path, size_t max_len) {
    char root_dir[MAX_PATH] = {0};
    if (get_zuma_game_root_dir(root_dir, sizeof(root_dir))) {
        char candidate[MAX_PATH];
        snprintf(candidate, sizeof(candidate), "%s\\properties\\layout.xml", root_dir);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            if (out_path && max_len > 0) {
                strncpy(out_path, candidate, max_len - 1);
                out_path[max_len - 1] = '\0';
            }
            return true;
        }
        snprintf(candidate, sizeof(candidate), "%s\\layout.xml", root_dir);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            if (out_path && max_len > 0) {
                strncpy(out_path, candidate, max_len - 1);
                out_path[max_len - 1] = '\0';
            }
            return true;
        }
    }

    char prop_dir[MAX_PATH] = {0};
    if (get_zuma_properties_dir(prop_dir, sizeof(prop_dir))) {
        char candidate[MAX_PATH];
        snprintf(candidate, sizeof(candidate), "%s\\layout.xml", prop_dir);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            if (out_path && max_len > 0) {
                strncpy(out_path, candidate, max_len - 1);
                out_path[max_len - 1] = '\0';
            }
            return true;
        }
    }
    return false;
}

/* Static metadata table for PopCap sprite strips: cols, rows, default preview cell */
typedef struct {
    const char *name;
    int cols;
    int rows;
    int default_cell;
} SpriteMetadata;

static const SpriteMetadata g_sprite_metadata_table[] = {
    { "advdoor1a", 4, 1, 1 },
    { "advdoor1b", 4, 1, 1 },
    { "advdoor1c", 4, 1, 1 },
    { "advdoor2a", 4, 1, 1 },
    { "advdoor2b", 4, 1, 1 },
    { "advdoor2c", 4, 1, 1 },
    { "advdoor3a", 4, 1, 1 },
    { "advdoor3b", 4, 1, 1 },
    { "advdoor3c", 4, 1, 1 },
    { "advdoor4a", 4, 1, 1 },
    { "advdoor4b", 4, 1, 1 },
    { "advdoor4c", 4, 1, 1 },
    { "advdoor5",  3, 1, 0 },
    { "gauntdoor1", 4, 1, 1 },
    { "gauntdoor2", 4, 1, 1 },
    { "gauntdoor3", 4, 1, 1 },
    { "gauntdoor4", 4, 1, 1 },
    { "checkbutton", 2, 1, 0 },
    { "tut_mouse_m", 2, 1, 0 },
    { "tut_mouse_r", 2, 1, 0 },
    { "baDotz_extra", 11, 1, 0 },
    { "baDotz", 6, 1, 0 },
    { "sparkle", 14, 1, 0 },
    { "baBallBlue", 1, 47, 0 },
    { "baBallOrange", 1, 47, 0 },
    { "baBallPurple", 1, 51, 0 },
    { "baBallYellow", 1, 50, 0 },
    { "baBallRed", 1, 50, 0 },
    { "baBallGreen", 1, 50, 0 },
    { "baBallWhite", 1, 50, 0 },
    { "baBallNavy", 1, 50, 0 },
    { "baBallCyan", 1, 50, 0 },
    { "baBallBlack", 1, 50, 0 },
    { "baBallCream", 1, 50, 0 },
    { "pitcover", 1, 12, 0 },
    { "coin", 1, 30, 0 },
    { "explosion", 1, 17, 0 },
    { "advback", 1, 1, 0 },
    { "advtitle", 1, 1, 0 },
    { "advmainmenubutton", 3, 1, 0 },
    { "advplaybutton", 3, 1, 0 },
    { "gaunttitle", 1, 1, 0 },
    { "gauntbackbutton", 4, 1, 0 },
    { "gauntmainmenubutton", 3, 1, 0 },
    { "gauntnextbutton", 4, 1, 0 },
    { "gauntplaybutton", 4, 1, 0 },
    { "gauntpracticebutton", 3, 1, 0 },
    { "gauntsurvivalbutton", 3, 1, 0 },
    { "Dialogbutton", 3, 1, 0 },
    { "DialogButtonLocked", 3, 1, 0 },
    { "moregamesbutton", 3, 1, 0 },
    { "mmarcadebutton", 3, 1, 0 },
    { "mmgauntletbutton", 3, 1, 0 },
    { "mmoptionsbutton", 3, 1, 0 },
    { "mmmoregamesbutton", 3, 1, 0 },
    { "mmquitbutton", 3, 1, 0 },
    { "menubutton", 3, 1, 0 },
    { "gauge", 1, 3, 0 },
    { "toadlives", 1, 1, 0 }
};

/* Retrieve asset file path prioritizing imagescustomres\ first, then images\ and levels\ */
static bool layout_get_zuma_asset_path(const char *filename, char *out_path, size_t max_len) {
    if (!filename || filename[0] == '\0' || !out_path || max_len == 0) return false;

    char root_dir[MAX_PATH] = {0};
    get_zuma_game_root_dir(root_dir, sizeof(root_dir));

    char candidate[MAX_PATH];

    /* 1. Prioritize imagescustomres\ first */
    if (root_dir[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%s\\imagescustomres\\%s", root_dir, filename);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            strncpy(out_path, candidate, max_len - 1);
            out_path[max_len - 1] = '\0';
            return true;
        }
    }

    /* 2. Fallback to images\ second */
    if (root_dir[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%s\\images\\%s", root_dir, filename);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            strncpy(out_path, candidate, max_len - 1);
            out_path[max_len - 1] = '\0';
            return true;
        }
    }

    /* 3. Direct root path check (for levels\..., fonts\...) */
    if (root_dir[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%s\\%s", root_dir, filename);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            strncpy(out_path, candidate, max_len - 1);
            out_path[max_len - 1] = '\0';
            return true;
        }

        snprintf(candidate, sizeof(candidate), "%s\\fonts\\%s", root_dir, filename);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            strncpy(out_path, candidate, max_len - 1);
            out_path[max_len - 1] = '\0';
            return true;
        }

        snprintf(candidate, sizeof(candidate), "%s\\assets\\media\\%s", root_dir, filename);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            strncpy(out_path, candidate, max_len - 1);
            out_path[max_len - 1] = '\0';
            return true;
        }

        snprintf(candidate, sizeof(candidate), "%s\\assets\\%s", root_dir, filename);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            strncpy(out_path, candidate, max_len - 1);
            out_path[max_len - 1] = '\0';
            return true;
        }
    }

    /* 4. Local ZuPather assets\ folder (e.g. assets\outscreen.jpg) */
    snprintf(candidate, sizeof(candidate), "assets\\%s", filename);
    if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
        strncpy(out_path, candidate, max_len - 1);
        out_path[max_len - 1] = '\0';
        return true;
    }

    /* 5. Fallback to local images\ in ZuPather directory */
    snprintf(candidate, sizeof(candidate), "images\\%s", filename);
    if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
        strncpy(out_path, candidate, max_len - 1);
        out_path[max_len - 1] = '\0';
        return true;
    }

    return false;
}

/* XML attribute parsing forward declarations */
static bool extract_xml_attr_int(const char *tag_str, const char *attr_name, int *out_val);
static bool extract_xml_attr_str(const char *tag_str, const char *attr_name, char *out_str, size_t max_len);

/* Find Image element in resources_extra.xml (fallback to resources.xml) */
static bool layout_find_resource_manifest_image(
    const char *name,
    char *out_path, size_t max_path_len,
    char *out_alpha, size_t max_alpha_len,
    int *out_cols, int *out_rows
) {
    if (!name || name[0] == '\0') return false;

    char root_dir[MAX_PATH] = {0};
    get_zuma_game_root_dir(root_dir, sizeof(root_dir));

    const char *manifest_names[] = {
        "properties\\resources_extra.xml",
        "properties\\resources.xml"
    };

    for (int m = 0; m < 2; ++m) {
        char full_manifest_path[MAX_PATH];
        if (root_dir[0] != '\0') {
            snprintf(full_manifest_path, sizeof(full_manifest_path), "%s\\%s", root_dir, manifest_names[m]);
        } else {
            snprintf(full_manifest_path, sizeof(full_manifest_path), "%s", manifest_names[m]);
        }

        FILE *fp = fopen(full_manifest_path, "r");
        if (!fp) continue;

        char line[512];
        char cur_idprefix[32] = "IMAGE_";

        while (fgets(line, sizeof(line), fp)) {
            char *p = line;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '<') {
                if (_strnicmp(p + 1, "SetDefaults", 11) == 0) {
                    char pre[32] = {0};
                    if (extract_xml_attr_str(p, "idprefix", pre, sizeof(pre))) {
                        strncpy(cur_idprefix, pre, sizeof(cur_idprefix) - 1);
                    }
                    continue;
                }

                if (_strnicmp(p + 1, "Image", 5) == 0) {
                    char id_str[64] = {0};
                    char path_str[128] = {0};
                    char alpha_str[128] = {0};
                    int cols = 1;
                    int rows = 1;

                    extract_xml_attr_str(p, "id", id_str, sizeof(id_str));
                    extract_xml_attr_str(p, "path", path_str, sizeof(path_str));
                    if (!extract_xml_attr_str(p, "alphaimage", alpha_str, sizeof(alpha_str))) {
                        if (!extract_xml_attr_str(p, "alphapath", alpha_str, sizeof(alpha_str))) {
                            extract_xml_attr_str(p, "alphagrid", alpha_str, sizeof(alpha_str));
                        }
                    }
                    extract_xml_attr_int(p, "cols", &cols);
                    extract_xml_attr_int(p, "rows", &rows);

                    bool match = false;
                    if (_stricmp(path_str, name) == 0) match = true;
                    if (_stricmp(id_str, name) == 0) match = true;
                    char prefixed_id[64];
                    snprintf(prefixed_id, sizeof(prefixed_id), "%s%s", cur_idprefix, name);
                    if (_stricmp(id_str, prefixed_id) == 0) match = true;
                    if (strncmp(name, "IMAGE_", 6) == 0 && _stricmp(id_str, name + 6) == 0) match = true;

                    if (match) {
                        if (out_path && max_path_len > 0) {
                            strncpy(out_path, path_str[0] ? path_str : name, max_path_len - 1);
                            out_path[max_path_len - 1] = '\0';
                        }
                        if (out_alpha && max_alpha_len > 0) {
                            strncpy(out_alpha, alpha_str, max_alpha_len - 1);
                            out_alpha[max_alpha_len - 1] = '\0';
                        }
                        if (out_cols) *out_cols = (cols > 0) ? cols : 1;
                        if (out_rows) *out_rows = (rows > 0) ? rows : 1;
                        fclose(fp);
                        return true;
                    }
                }
            }
        }
        fclose(fp);
    }

    return false;
}

/* Helper to build PopCap mask filename preserving subdirectory prefix (e.g. "fonts\_Name.gif") */
static void build_mask_filename(const char *name, const char *prefix, const char *suffix, const char *ext, char *out, size_t max_len) {
    const char *last_slash = strrchr(name, '\\');
    const char *last_fslash = strrchr(name, '/');
    const char *sep = (last_slash > last_fslash) ? last_slash : last_fslash;
    if (sep) {
        int dir_len = (int)(sep - name + 1);
        snprintf(out, max_len, "%.*s%s%s%s%s", dir_len, name, prefix, sep + 1, suffix, ext);
    } else {
        snprintf(out, max_len, "%s%s%s%s", prefix, name, suffix, ext);
    }
}

/* Retrieve cached sprite with true PopCap alpha mask blending and sprite strip cell info */
static LayoutSprite* layout_get_cached_sprite(const char *name, int requested_cols) {
    if (!name || name[0] == '\0') return NULL;

    for (int i = 0; i < g_sprite_cache_count; ++i) {
        if (_stricmp(g_sprite_cache[i].name, name) == 0) {
            return &g_sprite_cache[i];
        }
    }

    if (g_sprite_cache_count >= MAX_CACHED_SPRITES) return NULL;

    LayoutSprite *sp = &g_sprite_cache[g_sprite_cache_count];
    memset(sp, 0, sizeof(LayoutSprite));
    strncpy(sp->name, name, sizeof(sp->name) - 1);
    sp->cols = (requested_cols > 0) ? requested_cols : 1;
    sp->rows = 1;
    sp->default_cell = 0;

    /* 1. Check Manifest for metadata */
    char manifest_path[128] = {0};
    char manifest_alpha[128] = {0};
    int m_cols = 1, m_rows = 1;
    bool found_in_manifest = layout_find_resource_manifest_image(name, manifest_path, sizeof(manifest_path),
                                                                 manifest_alpha, sizeof(manifest_alpha),
                                                                 &m_cols, &m_rows);
    if (found_in_manifest) {
        if (m_cols > 1) sp->cols = m_cols;
        if (m_rows > 1) sp->rows = m_rows;
    }

    /* 2. Check Static Sprite Metadata Table */
    for (size_t s = 0; s < sizeof(g_sprite_metadata_table) / sizeof(g_sprite_metadata_table[0]); ++s) {
        if (_stricmp(name, g_sprite_metadata_table[s].name) == 0 ||
            (manifest_path[0] && _stricmp(manifest_path, g_sprite_metadata_table[s].name) == 0)) {
            sp->cols = g_sprite_metadata_table[s].cols;
            sp->rows = g_sprite_metadata_table[s].rows;
            sp->default_cell = g_sprite_metadata_table[s].default_cell;
            break;
        }
    }

    /* Button convention */
    if (sp->cols == 1 && (strstr(name, "button") || strstr(name, "Button") || strstr(name, "Btn"))) {
        sp->cols = 3;
        sp->default_cell = 0;
    }

    /* 3. Locate Base Image */
    const char *lookup_name = manifest_path[0] ? manifest_path : name;
    const char *exts[] = { ".png", ".jpg", ".gif", ".webp", "" };
    char base_path[MAX_PATH] = {0};
    bool found_base = false;

    for (int e = 0; e < 5; ++e) {
        char test_name[MAX_PATH];
        if (exts[e][0] != '\0') snprintf(test_name, sizeof(test_name), "%s%s", lookup_name, exts[e]);
        else snprintf(test_name, sizeof(test_name), "%s", lookup_name);
        if (layout_get_zuma_asset_path(test_name, base_path, sizeof(base_path))) {
            found_base = true;
            break;
        }
    }

    if (!found_base && lookup_name != name) {
        for (int e = 0; e < 5; ++e) {
            char test_name[MAX_PATH];
            if (exts[e][0] != '\0') snprintf(test_name, sizeof(test_name), "%s%s", name, exts[e]);
            else snprintf(test_name, sizeof(test_name), "%s", name);
            if (layout_get_zuma_asset_path(test_name, base_path, sizeof(base_path))) {
                found_base = true;
                break;
            }
        }
    }

    if (!found_base) return NULL;

    if (!image_load_from_file(base_path, &sp->img)) {
        return NULL;
    }
    sp->from_customres = (strstr(base_path, "imagescustomres") != NULL);

    /* 4. Locate PopCap Alpha Mask */
    char mask_path[MAX_PATH] = {0};
    bool found_mask = false;

    if (manifest_alpha[0] != '\0') {
        for (int e = 0; e < 5; ++e) {
            char test_name[MAX_PATH];
            if (exts[e][0] != '\0') snprintf(test_name, sizeof(test_name), "%s%s", manifest_alpha, exts[e]);
            else snprintf(test_name, sizeof(test_name), "%s", manifest_alpha);
            if (layout_get_zuma_asset_path(test_name, mask_path, sizeof(mask_path))) {
                found_mask = true;
                break;
            }
        }
    }

    if (!found_mask) {
        for (int e = 0; e < 4; ++e) {
            char mask_name[MAX_PATH];
            build_mask_filename(lookup_name, "_", "", exts[e], mask_name, sizeof(mask_name));
            if (layout_get_zuma_asset_path(mask_name, mask_path, sizeof(mask_path))) {
                found_mask = true;
                break;
            }
            build_mask_filename(lookup_name, "", "_", exts[e], mask_name, sizeof(mask_name));
            if (layout_get_zuma_asset_path(mask_name, mask_path, sizeof(mask_path))) {
                found_mask = true;
                break;
            }
        }
    }

    if (!found_mask && lookup_name != name) {
        for (int e = 0; e < 4; ++e) {
            char mask_name[MAX_PATH];
            build_mask_filename(name, "_", "", exts[e], mask_name, sizeof(mask_name));
            if (layout_get_zuma_asset_path(mask_name, mask_path, sizeof(mask_path))) {
                found_mask = true;
                break;
            }
            build_mask_filename(name, "", "_", exts[e], mask_name, sizeof(mask_name));
            if (layout_get_zuma_asset_path(mask_name, mask_path, sizeof(mask_path))) {
                found_mask = true;
                break;
            }
        }
    }

    /* 5. Apply Alpha Mask or Additive Glow */
    if (found_mask) {
        CanvasImage mask_img;
        if (image_load_from_file(mask_path, &mask_img)) {
            if (mask_img.bits && sp->img.bits && mask_img.width > 0 && mask_img.height > 0) {
                int img_w = sp->img.width;
                int img_h = sp->img.height;
                int mask_w = mask_img.width;
                int mask_h = mask_img.height;
                uint8_t *src_px = (uint8_t*)sp->img.bits;
                uint8_t *msk_px = (uint8_t*)mask_img.bits;

                for (int y = 0; y < img_h; ++y) {
                    int my = y % mask_h;
                    for (int x = 0; x < img_w; ++x) {
                        int mx = x % mask_w;
                        int m_idx = (my * mask_w + mx) * 4;
                        int m_alpha = (msk_px[m_idx + 3] == 0) ? 0 : msk_px[m_idx + 2]; /* R channel or luminance */
                        int s_idx = (y * img_w + x) * 4;
                        int b = src_px[s_idx + 0];
                        int g = src_px[s_idx + 1];
                        int r = src_px[s_idx + 2];

                        src_px[s_idx + 0] = (uint8_t)((b * m_alpha) / 255);
                        src_px[s_idx + 1] = (uint8_t)((g * m_alpha) / 255);
                        src_px[s_idx + 2] = (uint8_t)((r * m_alpha) / 255);
                        src_px[s_idx + 3] = (uint8_t)m_alpha;
                    }
                }
            }
            image_free(&mask_img);
        }
    } else {
        if (_stricmp(name, "mmsunglow") == 0 || strstr(name, "sunglow") || strstr(name, "glow")) {
            int total_pixels = sp->img.width * sp->img.height;
            uint8_t *src_px = (uint8_t*)sp->img.bits;
            for (int p = 0; p < total_pixels; ++p) {
                int b = src_px[p * 4 + 0];
                int g = src_px[p * 4 + 1];
                int r = src_px[p * 4 + 2];
                if (r + g + b < 24) {
                    src_px[p * 4 + 0] = 0;
                    src_px[p * 4 + 1] = 0;
                    src_px[p * 4 + 2] = 0;
                    src_px[p * 4 + 3] = 0;
                } else {
                    int lum = (r * 299 + g * 587 + b * 114) / 1000;
                    if (lum > 255) lum = 255;
                    src_px[p * 4 + 0] = (uint8_t)((b * lum) / 255);
                    src_px[p * 4 + 1] = (uint8_t)((g * lum) / 255);
                    src_px[p * 4 + 2] = (uint8_t)((r * lum) / 255);
                    src_px[p * 4 + 3] = (uint8_t)lum;
                }
            }
        } else {
            int total_pixels = sp->img.width * sp->img.height;
            uint8_t *src_px = (uint8_t*)sp->img.bits;
            bool has_any_alpha = false;
            for (int p = 0; p < total_pixels; ++p) {
                if (src_px[p * 4 + 3] > 0 && src_px[p * 4 + 3] < 255) {
                    has_any_alpha = true;
                    break;
                }
            }
            if (has_any_alpha) {
                for (int p = 0; p < total_pixels; ++p) {
                    int a = src_px[p * 4 + 3];
                    src_px[p * 4 + 0] = (uint8_t)((src_px[p * 4 + 0] * a) / 255);
                    src_px[p * 4 + 1] = (uint8_t)((src_px[p * 4 + 1] * a) / 255);
                    src_px[p * 4 + 2] = (uint8_t)((src_px[p * 4 + 2] * a) / 255);
                }
            }
        }
    }

    sp->loaded = true;
    g_sprite_cache_count++;
    return sp;
}

static void layout_clear_sprite_cache(void) {
    for (int i = 0; i < g_sprite_cache_count; ++i) {
        if (g_sprite_cache[i].loaded) {
            image_free(&g_sprite_cache[i].img);
            g_sprite_cache[i].loaded = false;
        }
    }
    g_sprite_cache_count = 0;
}

/* Map layout element to PopCap sprite asset name and frame count */
static const char* get_element_sprite_name(const LayoutElement *el, int *out_cols, int *out_rows, int *out_cell) {
    if (out_cols) *out_cols = 1;
    if (out_rows) *out_rows = 1;
    if (out_cell) *out_cell = 0;
    if (!el) return NULL;

    /* 1. Adventure Map Doors & Temples */
    if (_stricmp(el->section, "AdventureDoors") == 0 || _stricmp(el->tag, "Door") == 0) {
        int did = atoi(el->id);
        if (_stricmp(el->section, "GauntletDoors") == 0) {
            if (out_cols) *out_cols = 4;
            if (out_cell) *out_cell = 1; /* open door state */
            switch (did) {
                case 0: return "gauntdoor1";
                case 1: return "gauntdoor2";
                case 2: return "gauntdoor3";
                case 3: return "gauntdoor4";
                default: return "gauntdoor1";
            }
        }
        if (did == 12) {
            if (out_cols) *out_cols = 3;
            if (out_cell) *out_cell = 0;
            return "advdoor5";
        }
        if (out_cols) *out_cols = 4;
        if (out_cell) *out_cell = (did == 11) ? 3 : 1; /* Cell 3 is flaming orange; Cell 1 is dark open */
        switch (did) {
            case 0: return "advdoor1a";
            case 1: return "advdoor1b";
            case 2: return "advdoor1c";
            case 3: return "advdoor2a";
            case 4: return "advdoor2b";
            case 5: return "advdoor2c";
            case 6: return "advdoor3a";
            case 7: return "advdoor3b";
            case 8: return "advdoor3c";
            case 9: return "advdoor4a";
            case 10: return "advdoor4b";
            case 11: return "advdoor4c";
            default: return "advdoor1a";
        }
    }

    if (_stricmp(el->section, "AdventureTemples") == 0) {
        if (_stricmp(el->tag, "AdvBack") == 0) return "advback";
        if (_stricmp(el->tag, "Temple1") == 0) return "advtemple1";
        if (_stricmp(el->tag, "Temple2") == 0) return "advtemple2";
        if (_stricmp(el->tag, "Temple2V") == 0) return "advtemple2v";
        if (_stricmp(el->tag, "Temple3") == 0) return "advtemple3";
        if (_stricmp(el->tag, "Temple3V") == 0) return "advtemple3v";
        if (_stricmp(el->tag, "Temple4") == 0) return "advtemple4";
        if (_stricmp(el->tag, "Temple5") == 0) return "advtemple5";
        if (_stricmp(el->tag, "Middle") == 0) return "advmiddle";
    }

    if (_stricmp(el->section, "AdventureButtons") == 0) {
        if (out_cols) *out_cols = 3;
        if (_stricmp(el->tag, "MainMenu") == 0) return "advmainmenubutton";
        if (_stricmp(el->tag, "Play") == 0) return "advplaybutton";
    }

    if (_stricmp(el->section, "AdventureLabels") == 0) {
        if (_stricmp(el->tag, "AdventureTitle") == 0) return "advtitle";
        if (_stricmp(el->tag, "BottomBox") == 0 || _stricmp(el->tag, "Stage13Badge") == 0) return "advstage";
        if (_stricmp(el->tag, "HighscoreTitle") == 0) return "advhighscore";
    }

    /* 2. Main Menu */
    if (_stricmp(el->section, "MainMenu") == 0) {
        if (_stricmp(el->tag, "AdventureBtn") == 0) { if (out_cols) *out_cols = 3; return "mmarcadebutton"; }
        if (_stricmp(el->tag, "GauntletBtn") == 0)  { if (out_cols) *out_cols = 3; return "mmgauntletbutton"; }
        if (_stricmp(el->tag, "OptionsBtn") == 0)   { if (out_cols) *out_cols = 3; return "mmoptionsbutton"; }
        if (_stricmp(el->tag, "MoreGamesBtn") == 0) { if (out_cols) *out_cols = 3; return "mmmoregamesbutton"; }
        if (_stricmp(el->tag, "QuitBtn") == 0)      { if (out_cols) *out_cols = 3; return "mmquitbutton"; }
        if (_stricmp(el->tag, "EyeIdle1") == 0)     return "mmeyeleft";
        if (_stricmp(el->tag, "EyeIdle2") == 0)     return "mmeyeright";
        if (_stricmp(el->tag, "Sun") == 0 || _stricmp(el->tag, "Sun1") == 0 || _stricmp(el->tag, "Sun2") == 0) return "mmsunglow";
        if (_stricmp(el->tag, "Hat") == 0)          return "mmhat";
    }

    /* 3. Gauntlet */
    if (_stricmp(el->section, "GauntletButtons") == 0) {
        if (_stricmp(el->tag, "Back") == 0 || _stricmp(el->tag, "Next") == 0 || _stricmp(el->tag, "Play") == 0) {
            if (out_cols) *out_cols = 4;
        } else {
            if (out_cols) *out_cols = 3;
        }
        if (_stricmp(el->tag, "MainMenu") == 0) return "gauntmainmenubutton";
        if (_stricmp(el->tag, "Practice") == 0) return "gauntpracticebutton";
        if (_stricmp(el->tag, "Survival") == 0) return "gauntsurvivalbutton";
        if (_stricmp(el->tag, "Back") == 0)     return "gauntbackbutton";
        if (_stricmp(el->tag, "Next") == 0)     return "gauntnextbutton";
        if (_stricmp(el->tag, "Play") == 0)     return "gauntplaybutton";
    }

    if (_stricmp(el->section, "GauntletLabels") == 0) {
        if (_stricmp(el->tag, "GauntletTitle") == 0) return "gaunttitle";
        if (_stricmp(el->tag, "HighscoreTitle") == 0) return "gaunthighscore";
        if (_stricmp(el->tag, "Gem") == 0) return "gaunteaglegem";
    }

    /* 4. Loading Screen */
    if (_stricmp(el->section, "LoadingScreen") == 0) {
        if (_stricmp(el->tag, "LoadingBar") == 0) return "loaderbar";
    }

    /* 5. In-Game HUD */
    if (_stricmp(el->section, "InGameHUD") == 0) {
        if (_stricmp(el->tag, "Menubar") == 0) return "menubar";
        if (_stricmp(el->tag, "MenuBtn") == 0) { if (out_cols) *out_cols = 3; return "menubutton"; }
        if (_stricmp(el->tag, "ProgressBar") == 0) { if (out_rows) *out_rows = 3; return "gauge"; }
    }

    /* 6. Stage Complete Cutscene */
    if (_stricmp(el->section, "StageComplete") == 0) {
        if (_stricmp(el->tag, "FrogTarget") == 0 || _stricmp(el->tag, "FrogWriting") == 0) return "SMALLFROGonPAD";
        if (_stricmp(el->tag, "ContinueBtn") == 0) { if (out_cols) *out_cols = 3; return "Dialogbutton"; }
    }

    /* 7. Temple Cleared */
    if (_stricmp(el->section, "TempleCleared") == 0) {
        if (_stricmp(el->tag, "TikiHead") == 0) return "godhead";
        if (_stricmp(el->tag, "FrogMedallion") == 0) return "SMALLFROGonPAD";
        if (_stricmp(el->tag, "ContinueBtn") == 0) { if (out_cols) *out_cols = 3; return "Dialogbutton"; }
    }

    /* 8. Dialogs */
    if (_stricmp(el->section, "Dialogs") == 0) {
        if (_stricmp(el->tag, "DialogHeader") == 0) return "godhead";
        return "dialogbox";
    }

    /* 9. Help Screen */
    if (_stricmp(el->section, "HelpScreen") == 0) {
        if (_stricmp(el->tag, "HelpBack") == 0) return NULL; /* Composited in Step 2 */
        if (_stricmp(el->tag, "Skull") == 0) return "Hole";
        if (_stricmp(el->tag, "Coin") == 0) { if (out_rows) *out_rows = 30; return "coin"; }
        if (_stricmp(el->tag, "Frog") == 0) return "SMALLFROGonPAD";
        if (_stricmp(el->tag, "DevTemple5") == 0) return "advtemple5_tut";
        if (_stricmp(el->tag, "SwapMouse") == 0) { if (out_cols) *out_cols = 2; return "tut_mouse_r"; }
        if (_stricmp(el->tag, "DevMouseM") == 0) { if (out_cols) *out_cols = 2; return "tut_mouse_m"; }
        if (_stricmp(el->tag, "DontShowCheckbox") == 0) { if (out_cols) *out_cols = 2; return "checkbutton"; }
        if (_stricmp(el->tag, "WhateverBtn") == 0) { if (out_cols) *out_cols = 3; return "Dialogbutton"; }
    }

    /* 10. DevHelpScreen */
    if (_stricmp(el->section, "DevHelpScreen") == 0) {
        if (_stricmp(el->tag, "DialogHeader") == 0) return "godhead";
        if (_stricmp(el->tag, "Temple5Preview") == 0) return "advtemple5_tut";
        if (_stricmp(el->tag, "DemoMouse") == 0) { if (out_cols) *out_cols = 2; return "tut_mouse_m"; }
    }

    /* 11. Credits Screen */
    if (_stricmp(el->section, "CreditsScreen") == 0) {
        if (_stricmp(el->tag, "Tiki1") == 0 || _stricmp(el->tag, "Tiki2") == 0) return "godhead";
        if (_stricmp(el->tag, "WhateverBtn") == 0) { if (out_cols) *out_cols = 3; return "Dialogbutton"; }
    }

    /* 12. More Games */
    if (_stricmp(el->section, "MoreGames") == 0) {
        if (_stricmp(el->tag, "MoreGamesBtn") == 0) { if (out_cols) *out_cols = 3; return "moregamesbutton"; }
        if (_stricmp(el->tag, "BackBtn") == 0) { if (out_cols) *out_cols = 3; return "Dialogbutton"; }
    }

    return NULL;
}

/* Detect Zuma's target display resolution by inspecting Zuma.exe PE header (.zres and .zdata sections),
   checking custom resolution images (imagescustomres\menubar.jpg), or scanning for images<W>x<H> directories */
static bool detect_zuma_native_resolution(int *out_w, int *out_h) {
    if (out_w) *out_w = 640;
    if (out_h) *out_h = 480;

    char root_dir[MAX_PATH] = {0};
    if (!get_zuma_game_root_dir(root_dir, sizeof(root_dir))) {
        return false;
    }

    char zuma_exe[MAX_PATH];
    snprintf(zuma_exe, sizeof(zuma_exe), "%s\\Zuma.exe", root_dir);

    /* 1. Inspect Zuma.exe PE header for .zres section and .zdata embedded table */
    if (GetFileAttributesA(zuma_exe) != INVALID_FILE_ATTRIBUTES) {
        FILE *fp = fopen(zuma_exe, "rb");
        if (fp) {
            IMAGE_DOS_HEADER dos_hdr;
            if (fread(&dos_hdr, sizeof(dos_hdr), 1, fp) == 1 && dos_hdr.e_magic == IMAGE_DOS_SIGNATURE) {
                if (fseek(fp, dos_hdr.e_lfanew, SEEK_SET) == 0) {
                    DWORD pe_sig = 0;
                    if (fread(&pe_sig, sizeof(pe_sig), 1, fp) == 1 && pe_sig == 0x00004550) {
                        IMAGE_FILE_HEADER file_hdr;
                        if (fread(&file_hdr, sizeof(file_hdr), 1, fp) == 1) {
                            long sec_table_offset = dos_hdr.e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + file_hdr.SizeOfOptionalHeader;
                            if (fseek(fp, sec_table_offset, SEEK_SET) == 0) {
                                for (WORD i = 0; i < file_hdr.NumberOfSections; ++i) {
                                    IMAGE_SECTION_HEADER sec_hdr;
                                    if (fread(&sec_hdr, sizeof(sec_hdr), 1, fp) != 1) break;

                                    /* A. Section .zres */
                                    if (strncmp((char*)sec_hdr.Name, ".zres", 5) == 0) {
                                        long zres_raw_offset = sec_hdr.PointerToRawData;
                                        if (fseek(fp, zres_raw_offset, SEEK_SET) == 0) {
                                            uint32_t magic = 0;
                                            if (fread(&magic, sizeof(magic), 1, fp) == 1 && magic == 0x53455A52) {
                                                if (fseek(fp, zres_raw_offset + 0x48, SEEK_SET) == 0) {
                                                    uint32_t rw = 0, rh = 0;
                                                    if (fread(&rw, sizeof(rw), 1, fp) == 1 &&
                                                        fread(&rh, sizeof(rh), 1, fp) == 1) {
                                                        if (rw >= 320 && rh >= 240 && rw <= 7680 && rh <= 4320) {
                                                            if (out_w) *out_w = (int)rw;
                                                            if (out_h) *out_h = (int)rh;
                                                            fclose(fp);
                                                            return true;
                                                        }
                                                    }
                                                }
                                                /* Legacy string at +0x50: "modules\\zumares<W>x<H>.dll" */
                                                if (fseek(fp, zres_raw_offset + 0x50, SEEK_SET) == 0) {
                                                    char mod_str[64] = {0};
                                                    if (fread(mod_str, 1, sizeof(mod_str) - 1, fp) > 0) {
                                                        int dw = 0, dh = 0;
                                                        char *p = strstr(mod_str, "zumares");
                                                        if (p && sscanf(p, "zumares%dx%d", &dw, &dh) == 2) {
                                                            if (dw >= 320 && dh >= 240) {
                                                                if (out_w) *out_w = dw;
                                                                if (out_h) *out_h = dh;
                                                                fclose(fp);
                                                                return true;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    /* B. Section .zdata (+0x1800 embedded zres table) */
                                    if (strncmp((char*)sec_hdr.Name, ".zdata", 6) == 0) {
                                        long zdata_raw_offset = sec_hdr.PointerToRawData + 0x1800;
                                        if (fseek(fp, zdata_raw_offset, SEEK_SET) == 0) {
                                            uint32_t magic = 0;
                                            if (fread(&magic, sizeof(magic), 1, fp) == 1 && magic == 0x53455A52) {
                                                if (fseek(fp, zdata_raw_offset + 0x48, SEEK_SET) == 0) {
                                                    uint32_t rw = 0, rh = 0;
                                                    if (fread(&rw, sizeof(rw), 1, fp) == 1 &&
                                                        fread(&rh, sizeof(rh), 1, fp) == 1) {
                                                        if (rw >= 320 && rh >= 240 && rw <= 7680 && rh <= 4320) {
                                                            if (out_w) *out_w = (int)rw;
                                                            if (out_h) *out_h = (int)rh;
                                                            fclose(fp);
                                                            return true;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            fclose(fp);
        }
    }

    /* 2. Check imagescustomres\menubar.jpg (Spans the full widescreen width W x H!) */
    char menubar_path[MAX_PATH];
    snprintf(menubar_path, sizeof(menubar_path), "%s\\imagescustomres\\menubar.jpg", root_dir);
    if (GetFileAttributesA(menubar_path) != INVALID_FILE_ATTRIBUTES) {
        CanvasImage ci;
        if (image_load_from_file(menubar_path, &ci)) {
            if (ci.width >= 320 && ci.height >= 240) {
                if (out_w) *out_w = ci.width;
                if (out_h) *out_h = ci.height;
                image_free(&ci);
                return true;
            }
            image_free(&ci);
        }
    }

    /* 3. Scan for images<W>x<H> directory */
    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\images*x*", root_dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                int dw = 0, dh = 0;
                if (sscanf(fd.cFileName, "images%dx%d", &dw, &dh) == 2) {
                    if (dw >= 320 && dh >= 240) {
                        if (out_w) *out_w = dw;
                        if (out_h) *out_h = dh;
                        FindClose(hFind);
                        return true;
                    }
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    return false;
}

/* Element Hotspot Rule Table: Centralized anchor behavior */
typedef struct {
    const char *section;
    const char *tag;
    ElementHotspotMode hotspot;
} ElementHotspotRule;

static const ElementHotspotRule g_hotspot_rules[] = {
    { "TempleCleared",   "Title",             HOTSPOT_CENTER_X },
    { "TempleCleared",   "TikiHead",          HOTSPOT_TOPLEFT },   /* PopCap DrawImage(50, 160) top-left confirmed by disassembly */
    { "TempleCleared",   "FrogMedallion",     HOTSPOT_CENTER_BOTH },/* Center (120, 380) keeps medallion inside 480 boundary */
    { "TempleCleared",   "ContinueBtn",       HOTSPOT_TOPLEFT },
    { "MainMenu",        "WelcomeText",       HOTSPOT_CENTER_X },
    { "MainMenu",        "UserSwitchText",    HOTSPOT_CENTER_X },
    { "MainMenu",        "EyeIdle1",          HOTSPOT_CENTER_BOTH },/* Pupil resting center */
    { "MainMenu",        "EyeIdle2",          HOTSPOT_CENTER_BOTH },/* Pupil resting center */
    { "GauntletLabels",  "Gem",               HOTSPOT_BOTTOM_CENTER },/* 560 horizontally centered, bottom aligned at 220 */
    { "LoadingScreen",   "ClickToPlay",       HOTSPOT_CENTER_X },
    { "InGameHUD",       "LivesBanner",       HOTSPOT_CENTER_BOTH },
    { "StageComplete",   "StageText",         HOTSPOT_CENTER_X },
    { "StageComplete",   "CompleteText",      HOTSPOT_CENTER_X },
    { "StageComplete",   "FrogWriting",       HOTSPOT_CENTER_X },
    { "StageComplete",   "FrogTarget",        HOTSPOT_CENTER_BOTH },
    { "AdventureLabels", "StageText",         HOTSPOT_CENTER_X },
    { "AdventureLabels", "Subtitle",          HOTSPOT_CENTER_X },
    { "AdventureLabels", "Stage13Title",      HOTSPOT_CENTER_X },
    { "AdventureLabels", "Stage13Subtitle",   HOTSPOT_CENTER_X },
    { "HelpScreen",      "Title",             HOTSPOT_CENTER_X },
    { "HelpScreen",      "Frog",              HOTSPOT_CENTER_BOTH },
    { "CreditsScreen",   "CreditsText",       HOTSPOT_CENTER_X },
    { "CreditsScreen",   "ScrollStart",       HOTSPOT_CENTER_X },
    { "DevHelpScreen",   "HeaderTitle",       HOTSPOT_CENTER_X },
    { "DevHelpScreen",   "HeaderSubtitle",    HOTSPOT_CENTER_X },
    { "DevHelpScreen",   "FooterHint",        HOTSPOT_CENTER_X },
};

/* Helper to assign canonical visual bounding box dimensions, hotspot modes and defaults across all 17 sections */
static void assign_element_default_bounds(LayoutElement *el, const LayoutEditorState *st) {
    if (!el) return;

    int space_w = 640;
    if (st) {
        int sec_idx = get_canonical_section_index(el->section);
        if (sec_idx >= 0 && st->section_space[sec_idx][0] != '\0') {
            int sw = 0, sh = 0;
            if (sscanf(st->section_space[sec_idx], "%dx%d", &sw, &sh) == 2 && sw > 0) space_w = sw;
        } else if (_stricmp(el->section, "LoadingScreen") == 0 && st->loading_space_w > 0) {
            space_w = st->loading_space_w;
        } else if (_stricmp(el->section, "MainMenu") == 0 && st->main_menu_space_w > 0) {
            space_w = st->main_menu_space_w;
        }
    }

    el->hotspot = HOTSPOT_TOPLEFT;
    el->has_explicit_size = ((el->flags & ATTR_HAS_W) != 0 && (el->flags & ATTR_HAS_H) != 0);

    /* Center hotspot flags from explicit attributes */
    if (el->flags & ATTR_HAS_CENTER_X) el->hotspot |= HOTSPOT_CENTER_X;
    if (el->flags & ATTR_HAS_CENTER_Y) el->hotspot |= HOTSPOT_CENTER_Y;

    /* Apply centralized hotspot overrides */
    for (size_t r = 0; r < sizeof(g_hotspot_rules) / sizeof(g_hotspot_rules[0]); ++r) {
        if ((g_hotspot_rules[r].section == NULL || _stricmp(el->section, g_hotspot_rules[r].section) == 0) &&
            _stricmp(el->tag, g_hotspot_rules[r].tag) == 0) {
            el->hotspot = g_hotspot_rules[r].hotspot;
            break;
        }
    }

    /* 1. Loading Screen */
    if (_stricmp(el->section, "LoadingScreen") == 0) {
        if (_stricmp(el->tag, "LoadingBar") == 0) {
            int bar_w = (space_w > 640) ? (int)roundf(384.0f * ((float)space_w / 640.0f)) : 384;
            int bar_h = (space_w > 640) ? (int)roundf(44.0f * ((float)space_w / 640.0f)) : 44;
            el->def_w = bar_w; el->def_h = bar_h;
            if (!(el->flags & ATTR_HAS_X)) { el->x = (space_w - bar_w) / 2; }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 349;  }
        } else if (_stricmp(el->tag, "ClickToPlay") == 0) {
            el->def_w = 260; el->def_h = 38;
            el->hotspot |= HOTSPOT_CENTER_X;
            if (!(el->flags & ATTR_HAS_X)) { el->x = space_w / 2;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 430;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "top-center", sizeof(el->align)-1);  }
        } else {
            el->def_w = 120; el->def_h = 32;
        }
    }

    /* 2. Main Menu */
    else if (_stricmp(el->section, "MainMenu") == 0) {
        if (_stricmp(el->tag, "AdventureBtn") == 0) {
            el->def_w = 163; el->def_h = 92;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 452;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 64;  }
        } else if (_stricmp(el->tag, "GauntletBtn") == 0) {
            el->def_w = 180; el->def_h = 83;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 436;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 153;  }
        } else if (_stricmp(el->tag, "OptionsBtn") == 0) {
            el->def_w = 199; el->def_h = 86;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 418;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 236;  }
        } else if (_stricmp(el->tag, "MoreGamesBtn") == 0) {
            el->def_w = 118; el->def_h = 127;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 394;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 305;  }
        } else if (_stricmp(el->tag, "QuitBtn") == 0) {
            el->def_w = 120; el->def_h = 141;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 496;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 315;  }
        } else if (_stricmp(el->tag, "FrogEyes") == 0) {
            el->def_w = 108; el->def_h = 45;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 191;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 331;  }
        } else if (_stricmp(el->tag, "Sun") == 0 || _stricmp(el->tag, "Sun1") == 0 || _stricmp(el->tag, "Sun2") == 0) {
            el->def_w = 229; el->def_h = 227;
            if (!(el->flags & ATTR_HAS_X)) { el->x = -69;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = -63;  }
        } else if (_stricmp(el->tag, "Hat") == 0) {
            el->def_w = 101; el->def_h = 107;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 188;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 247;  }
        } else if (_stricmp(el->tag, "EyeIdle1") == 0) {
            el->hotspot = HOTSPOT_CENTER_BOTH;
            el->def_w = 32; el->def_h = 32;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 219;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 368;  }
        } else if (_stricmp(el->tag, "EyeIdle2") == 0) {
            el->hotspot = HOTSPOT_CENTER_BOTH;
            el->def_w = 32; el->def_h = 32;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 276;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 355;  }
        } else if (_stricmp(el->tag, "EyeRay1") == 0) {
            el->def_w = 16; el->def_h = 16;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 29;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 25;  }
        } else if (_stricmp(el->tag, "EyeRay2") == 0) {
            el->def_w = 16; el->def_h = 16;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 87;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 16;  }
        } else if (_stricmp(el->tag, "Sky") == 0) {
            el->def_w = 640; el->def_h = 100;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 0;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 0;  }
        } else if (_stricmp(el->tag, "WelcomeText") == 0) {
            el->def_w = 400; el->def_h = 24;
            el->hotspot |= HOTSPOT_CENTER_X;
            if (!(el->flags & ATTR_HAS_X)) { el->x = space_w / 2; }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 6; /* top box = engine baseline 32 (unscaled) - (ascent 36 - padding 10) = 6 */ }
        } else if (_stricmp(el->tag, "UserSwitchText") == 0) {
            el->def_w = 300; el->def_h = 24;
            el->hotspot |= HOTSPOT_CENTER_X;
            if (!(el->flags & ATTR_HAS_X)) { el->x = space_w / 2; }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 53; }
        } else {
            el->def_w = 80; el->def_h = 32;
        }
    }

    /* 3. Adventure Map */
    else if (_stricmp(el->section, "AdventureTemples") == 0) {
        if (_stricmp(el->tag, "AdvBack") == 0) { el->def_w = 640; el->def_h = 250; if (!(el->flags & ATTR_HAS_X)) { el->x = 0; } if (!(el->flags & ATTR_HAS_Y)) { el->y = 0; } }
        else 
        if (_stricmp(el->tag, "Temple1") == 0) { el->def_w = 192; el->def_h = 100; if (!(el->flags & ATTR_HAS_X)) { el->x = 0;  } if (!(el->flags & ATTR_HAS_Y)) { el->y = 188;  } }
        else if (_stricmp(el->tag, "Temple2") == 0 || _stricmp(el->tag, "Temple2V") == 0) { el->def_w = 190; el->def_h = 100; if (!(el->flags & ATTR_HAS_X)) { el->x = 0;  } if (!(el->flags & ATTR_HAS_Y)) { el->y = 112;  } }
        else if (_stricmp(el->tag, "Temple3") == 0 || _stricmp(el->tag, "Temple3V") == 0) { el->def_w = 190; el->def_h = 100; if (!(el->flags & ATTR_HAS_X)) { el->x = 395;  } if (!(el->flags & ATTR_HAS_Y)) { el->y = 125;  } }
        else if (_stricmp(el->tag, "Temple4") == 0) { el->def_w = 292; el->def_h = 140; if (!(el->flags & ATTR_HAS_X)) { el->x = 174;  } if (!(el->flags & ATTR_HAS_Y)) { el->y = 1;  } }
        else if (_stricmp(el->tag, "Temple5") == 0) { el->def_w = 391; el->def_h = 100; if (!(el->flags & ATTR_HAS_X)) { el->x = 126;  } if (!(el->flags & ATTR_HAS_Y)) { el->y = 148;  } }
        else if (_stricmp(el->tag, "Middle") == 0) { el->def_w = 640; el->def_h = 150; if (!(el->flags & ATTR_HAS_X)) { el->x = 0;  } if (!(el->flags & ATTR_HAS_Y)) { el->y = 72;  } }
        else { el->def_w = 160; el->def_h = 90; }
    } else if (_stricmp(el->section, "AdventureDoors") == 0 || _stricmp(el->tag, "Door") == 0) {
        el->def_w = 64; el->def_h = 50;
    } else if (_stricmp(el->section, "AdventureButtons") == 0) {
        if (_stricmp(el->tag, "MainMenu") == 0) {
            el->def_w = 125; el->def_h = 42;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 0;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 438;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "bottom-left", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "Play") == 0) {
            el->def_w = 97; el->def_h = 39;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 542;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 441;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "bottom-right", sizeof(el->align)-1);  }
        } else { el->def_w = 120; el->def_h = 40; }
    } else if (_stricmp(el->section, "AdventureLabels") == 0) {
        if (_stricmp(el->tag, "AdventureTitle") == 0) {
            el->def_w = 184; el->def_h = 73;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 0; }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 0; }
        } else 
        if (_stricmp(el->tag, "HighscoreTitle") == 0) {
            el->def_w = 140; el->def_h = 30;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 456;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 0;  } /* Zuma.exe 0x43641B: y = 0 */
        } else if (_stricmp(el->tag, "BottomBox") == 0 || _stricmp(el->tag, "Stage13Badge") == 0) {
            el->def_w = 76; el->def_h = 16;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 289;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 382;  }
        } else if (_stricmp(el->tag, "StageNum") == 0 || _stricmp(el->tag, "Stage13Num") == 0) {
            el->def_w = 30; el->def_h = 20;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 344;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 396;  }
        } else if (_stricmp(el->tag, "StageText") == 0) {
            el->def_w = 300; el->def_h = 32;
            el->hotspot |= HOTSPOT_CENTER_X;
            if (!(el->flags & ATTR_HAS_X)) { el->x = (el->flags & ATTR_HAS_CENTER_X) ? el->centerX : 326;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 422;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "Subtitle") == 0) {
            el->def_w = 300; el->def_h = 24;
            el->hotspot |= HOTSPOT_CENTER_X;
            if (!(el->flags & ATTR_HAS_X)) { el->x = (el->flags & ATTR_HAS_CENTER_X) ? el->centerX : 326;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 456;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "HighscoreScores") == 0) {
            el->def_w = 60; el->def_h = 80;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 575;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 70;  } /* Zuma.exe 0x436836: y = 70 */
        } else if (_stricmp(el->tag, "HighscoreNames") == 0) {
            el->def_w = 90; el->def_h = 80;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 470;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 70;  } /* Zuma.exe 0x4368DB: y = 70 */
        } else if (_stricmp(el->tag, "HighscoreDate") == 0 || _stricmp(el->tag, "HighscoreClip") == 0) {
            el->def_w = 97; el->def_h = 60;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 567;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 70;  }
        } else if (_stricmp(el->tag, "Stage13Title") == 0) {
            el->def_w = 300; el->def_h = 32;
            el->hotspot |= HOTSPOT_CENTER_X;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 326;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 422;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "InfoBox") == 0) {
            el->def_w = (el->flags & ATTR_HAS_W && el->w > 0) ? el->w : 211;
            el->def_h = (el->flags & ATTR_HAS_H && el->h > 0) ? el->h : 141;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 178; }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 84; }
        } else if (_stricmp(el->tag, "Stage13Subtitle") == 0) {
            el->def_w = 300; el->def_h = 24;
            el->hotspot |= HOTSPOT_CENTER_X;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 326;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 456;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "InfoBox") == 0) {
            el->def_w = (el->flags & ATTR_HAS_W && el->w > 0) ? el->w : 150;
            el->def_h = (el->flags & ATTR_HAS_H && el->h > 0) ? el->h : 100;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 20;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 60;  }
        } else { el->def_w = 110; el->def_h = 32; }
    }

    /* 4. Gauntlet */
    else if (_stricmp(el->section, "GauntletButtons") == 0) {
        if (_stricmp(el->tag, "Practice") == 0) {
            el->def_w = 127; el->def_h = 32;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 177;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 444;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "bottom-middle", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "Survival") == 0) {
            el->def_w = 127; el->def_h = 28;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 277;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 444;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "bottom-middle", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "Back") == 0) {
            el->def_w = 93; el->def_h = 33;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 390;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 444;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "bottom-middle", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "MainMenu") == 0) {
            el->def_w = 141; el->def_h = 56;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 10;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 422;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "bottom-left", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "Next") == 0) {
            el->def_w = 94; el->def_h = 33;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 506;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 413;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "bottom-right", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "Play") == 0) {
            el->def_w = 105; el->def_h = 33;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 505;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 446;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "bottom-right", sizeof(el->align)-1);  }
        } else { el->def_w = 120; el->def_h = 36; }
    } else if (_stricmp(el->section, "GauntletLabels") == 0) {
        if (_stricmp(el->tag, "GauntletTitle") == 0) {
            el->def_w = 168; el->def_h = 68;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 150;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 0;  }
        } else if (_stricmp(el->tag, "DoorFrame") == 0) {
            el->def_w = (el->flags & ATTR_HAS_W && el->w > 0) ? el->w : 450;
            el->def_h = (el->flags & ATTR_HAS_H && el->h > 0) ? el->h : 338;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 389;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 309;  }
        } else if (_stricmp(el->tag, "HighscoreTitle") == 0) {
            el->def_w = 140; el->def_h = 30;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 434;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 0;  }
        } else if (_stricmp(el->tag, "DoorName") == 0) {
            el->def_w = (el->flags & ATTR_HAS_W && el->w > 0) ? el->w : 450;
            el->def_h = 24;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 389;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 600;  }
        } else if (_stricmp(el->tag, "PracticeText") == 0) {
            el->def_w = (el->flags & ATTR_HAS_W && el->w > 0) ? el->w : 450;
            el->def_h = 40;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 361;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 366;  }
        } else if (_stricmp(el->tag, "CustomMapTag") == 0) {
            el->def_w = 120; el->def_h = 24;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 417;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 366;  }
        } else if (_stricmp(el->tag, "Gem") == 0) {
            el->hotspot = HOTSPOT_BOTTOM_CENTER;
            el->def_w = 56; el->def_h = 56;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 938;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 309;  }
        } else if (_stricmp(el->tag, "HighscoreScores") == 0) {
            el->def_w = 60; el->def_h = 80;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 959;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 98;  }
        } else if (_stricmp(el->tag, "HighscoreNames") == 0) {
            el->def_w = 90; el->def_h = 80;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 776;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 98;  }
        } else if (_stricmp(el->tag, "HighscoreClip") == 0) {
            el->def_w = (el->flags & ATTR_HAS_W && el->w > 0) ? el->w : 947;
            el->def_h = 60;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 0;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 0;  }
        } else if (_stricmp(el->tag, "InfoBox") == 0) {
            el->def_w = (el->flags & ATTR_HAS_W && el->w > 0) ? el->w : 211;
            el->def_h = (el->flags & ATTR_HAS_H && el->h > 0) ? el->h : 141;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 178;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 84;  }
        } else { el->def_w = 110; el->def_h = 32; }
    }

    /* 5. In-Game HUD */
    else if (_stricmp(el->section, "InGameHUD") == 0) {
        if (_stricmp(el->tag, "LevelBanner") == 0) {
            el->def_w = 140; el->def_h = 24;
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "bottom-right", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "LivesBanner") == 0) {
            el->hotspot = HOTSPOT_CENTER_BOTH;
            el->def_w = 260; el->def_h = 40;
            if (!(el->flags & ATTR_HAS_CENTER_X)) { el->centerX = 600; el->flags |= ATTR_HAS_CENTER_X; }
            if (!(el->flags & ATTR_HAS_CENTER_Y)) { el->centerY = 337; el->flags |= ATTR_HAS_CENTER_Y; }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "Menubar") == 0) {
            el->def_w = 1200; el->def_h = 42;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 0;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 0;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "top-center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "MenuBtn") == 0) {
            el->def_w = 67; el->def_h = 27;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 1100;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 4;  }
        } else if (_stricmp(el->tag, "ProgressBar") == 0) {
            el->def_w = 178; el->def_h = 24;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 879;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 2;  }
        } else if (_stricmp(el->tag, "StageName") == 0) {
            el->hotspot = HOTSPOT_CENTER_X;
            el->def_w = 110; el->def_h = 28;
            if (!(el->flags & ATTR_HAS_CENTER_X)) { el->centerX = 290; el->flags |= ATTR_HAS_CENTER_X; }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 8;  }
        } else if (_stricmp(el->tag, "ScoreText") == 0) {
            el->def_w = 110; el->def_h = 28;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 640;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 8;  }
        } else if (_stricmp(el->tag, "LivesPos") == 0) {
            el->def_w = 50; el->def_h = 23;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 28;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 6;  }
        } else if (_stricmp(el->tag, "MenubarMargins") == 0) {
            el->def_w = 1200; el->def_h = 675;
        } else { el->def_w = 120; el->def_h = 36; }
    }

    /* 6. Stage Complete Cutscene */
    else if (_stricmp(el->section, "StageComplete") == 0) {
        if (_stricmp(el->tag, "StageText") == 0) {
            el->hotspot |= HOTSPOT_CENTER_X;
            el->def_w = 280; el->def_h = 44;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 320;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 150;  }
        } else if (_stricmp(el->tag, "CompleteText") == 0) {
            el->hotspot |= HOTSPOT_CENTER_X;
            el->def_w = 320; el->def_h = 48;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 320;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 250;  }
        } else if (_stricmp(el->tag, "BonusBar") == 0) {
            el->def_w = 218; el->def_h = 20;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 211;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 240;  }
        } else if (_stricmp(el->tag, "FrogTarget") == 0) {
            el->hotspot = HOTSPOT_CENTER_BOTH;
            el->def_w = 48; el->def_h = 48;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 320;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 200;  }
        } else if (_stricmp(el->tag, "ContinueBtn") == 0) {
            el->def_w = 180; el->def_h = 55;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 230;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 340;  }
        } else {
            el->def_w = 120; el->def_h = 32;
        }
    }

    /* 7. Temple Cleared (Congratulations Dialog) */
    else if (_stricmp(el->section, "TempleCleared") == 0) {
        if (_stricmp(el->tag, "Title") == 0) {
            el->def_w = 360; el->def_h = 40;
            el->hotspot |= HOTSPOT_CENTER_X;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 320;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 120;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "TikiHead") == 0) {
            el->hotspot = HOTSPOT_TOPLEFT;
            el->def_w = 147; el->def_h = 175;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 50;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 160;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "DialogText") == 0) {
            el->def_w = (el->flags & ATTR_HAS_W && el->w > 0) ? el->w : 380;
            el->def_h = (el->flags & ATTR_HAS_H && el->h > 0) ? el->h : 150;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 220;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 180;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "FrogMedallion") == 0) {
            el->hotspot = HOTSPOT_CENTER_BOTH;
            el->def_w = 108; el->def_h = 108;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 120;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 380;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "FrogReply") == 0) {
            el->def_w = 350; el->def_h = 100;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 200;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 280;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "ContinueBtn") == 0) {
            el->def_w = 180; el->def_h = 48;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 410;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 450;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
        } else { el->def_w = 120; el->def_h = 36; }
    }

    /* 8. Credits Screen */
    else if (_stricmp(el->section, "CreditsScreen") == 0) {
        if (_stricmp(el->tag, "WhateverBtn") == 0) {
            el->def_w = 120; el->def_h = 40;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 510;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 430;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "bottom-right", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "CreditsText") == 0) {
            el->def_w = 420; el->def_h = 240;
            el->hotspot |= HOTSPOT_CENTER_X;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 320;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 180;  }
            if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "top-center", sizeof(el->align)-1);  }
        } else if (_stricmp(el->tag, "ScrollStart") == 0) {
            el->def_w = 320; el->def_h = 24;
            el->hotspot |= HOTSPOT_CENTER_X;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 320;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 480;  }
        } else if (_stricmp(el->tag, "Tiki1") == 0) {
            el->def_w = 147; el->def_h = 175;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 30;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 150;  }
        } else if (_stricmp(el->tag, "Tiki2") == 0) {
            el->def_w = 147; el->def_h = 175;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 470;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 280;  }
        } else { el->def_w = 160; el->def_h = 32; }
    }

    /* 9. Universal Dialogs */
    else if (_stricmp(el->section, "Dialogs") == 0) {
        el->def_w = (el->flags & ATTR_HAS_W && el->w > 0) ? el->w : 400;
        el->def_h = (el->flags & ATTR_HAS_H && el->h > 0) ? el->h : 320;
        if (!(el->flags & ATTR_HAS_X)) { el->x = 120;  }
        if (!(el->flags & ATTR_HAS_Y)) { el->y = 80;  }
        if (!(el->flags & ATTR_HAS_ALIGN)) { strncpy(el->align, "center", sizeof(el->align)-1);  }
    }

    /* 10. Help Screen */
    else if (_stricmp(el->section, "HelpScreen") == 0) {
        if (_stricmp(el->tag, "HelpBack") == 0) {
            el->def_w = 618; el->def_h = 378;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 11;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 51;  }
        } else if (_stricmp(el->tag, "Title") == 0) {
            el->hotspot |= HOTSPOT_CENTER_X;
            el->def_w = 320; el->def_h = 36;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 320;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 55;  }
        } else if (_stricmp(el->tag, "Skull") == 0) {
            el->def_w = 70; el->def_h = 70;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 65;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 20;  }
        } else if (_stricmp(el->tag, "Coin") == 0) {
            el->def_w = 48; el->def_h = 48;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 282;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 45;  }
        } else if (_stricmp(el->tag, "Frog") == 0) {
            el->hotspot |= HOTSPOT_CENTER_BOTH;
            el->def_w = 108; el->def_h = 108;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 500;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 65;  }
        } else if (_stricmp(el->tag, "DevTemple5") == 0) {
            el->def_w = 190; el->def_h = 75;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 225;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 114;  }
        } else if (_stricmp(el->tag, "SwapMouse") == 0) {
            el->def_w = 32; el->def_h = 40;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 580;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 60;  }
        } else if (_stricmp(el->tag, "SwapClick") == 0) {
            el->def_w = 180; el->def_h = 24;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 560;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 55;  }
        } else if (_stricmp(el->tag, "TopText1") == 0) {
            el->def_w = 260; el->def_h = 32;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 30;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 40;  }
        } else if (_stricmp(el->tag, "TopText2") == 0) {
            el->def_w = 260; el->def_h = 32;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 8;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 40;  }
        } else if (_stricmp(el->tag, "BallTarget") == 0) {
            el->def_w = 32; el->def_h = 32;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 240;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 160;  }
        } else if (_stricmp(el->tag, "DevMouseM") == 0) {
            el->def_w = 40; el->def_h = 29;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 465;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 145;  }
        } else if (_stricmp(el->tag, "DontShowCheckbox") == 0) {
            el->def_w = 260; el->def_h = 24;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 60;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 430;  }
        } else if (_stricmp(el->tag, "WhateverBtn") == 0) {
            el->def_w = 140; el->def_h = 40;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 450;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 422;  }
        } else { el->def_w = 110; el->def_h = 32; }
    }

    /* 11. DevHelpScreen (ZTK Testing Kit) */
    else if (_stricmp(el->section, "DevHelpScreen") == 0) {
        if (_stricmp(el->tag, "HeaderTitle") == 0) {
            el->hotspot |= HOTSPOT_CENTER_X;
            el->def_w = 360; el->def_h = 36;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 320;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 28;  }
        } else if (_stricmp(el->tag, "HeaderSubtitle") == 0) {
            el->hotspot |= HOTSPOT_CENTER_X;
            el->def_w = 460; el->def_h = 24;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 320;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 78;  }
        } else if (_stricmp(el->tag, "AdvHeader") == 0) {
            el->hotspot |= HOTSPOT_CENTER_X;
            el->def_w = 200; el->def_h = 24;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 162;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 108;  }
        } else if (_stricmp(el->tag, "GauntHeader") == 0) {
            el->hotspot |= HOTSPOT_CENTER_X;
            el->def_w = 200; el->def_h = 24;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 478;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 108;  }
        } else if (_stricmp(el->tag, "CheatHeader") == 0) {
            el->hotspot |= HOTSPOT_CENTER_X;
            el->def_w = 260; el->def_h = 24;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 162;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 282;  }
        } else if (_stricmp(el->tag, "Temple5Preview") == 0) {
            el->def_w = 190; el->def_h = 75;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 67;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 114;  }
        } else if (_stricmp(el->tag, "AdvBullets") == 0) {
            el->def_w = 260; el->def_h = 70;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 30;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 206;  }
        } else if (_stricmp(el->tag, "GauntBullets") == 0) {
            el->def_w = 260; el->def_h = 90;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 343;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 132;  }
        } else if (_stricmp(el->tag, "CheatBullets") == 0) {
            el->def_w = 260; el->def_h = 70;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 30;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 304;  }
        } else if (_stricmp(el->tag, "FooterHint") == 0) {
            el->hotspot |= HOTSPOT_CENTER_X;
            el->def_w = 320; el->def_h = 24;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 320;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 420;  }
        } else {
            el->def_w = 120; el->def_h = 32;
        }
    }

    /* 12. More Games */
    else if (_stricmp(el->section, "MoreGames") == 0) {
        if (strstr(el->tag, "Thumb")) { el->def_w = 250; el->def_h = 150; }
        else if (_stricmp(el->tag, "MoreGamesBtn") == 0) {
            el->def_w = 118; el->def_h = 127;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 355;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 290;  }
        } else if (_stricmp(el->tag, "BackBtn") == 0) {
            el->def_w = 120; el->def_h = 36;
            if (!(el->flags & ATTR_HAS_X)) { el->x = 164;  }
            if (!(el->flags & ATTR_HAS_Y)) { el->y = 445;  }
        } else { el->def_w = 120; el->def_h = 36; }
    }

    else {
        el->def_w = 110;
        el->def_h = 32;
    }

    /* Respect explicit w/h if specified */
    if (el->flags & ATTR_HAS_W && el->w > 0) el->def_w = el->w;
    if (el->flags & ATTR_HAS_H && el->h > 0) el->def_h = el->h;
}

/* Check if an element belongs to the active screen filter */
static bool is_element_in_screen(const LayoutElement *el, int screen_idx) {
    if (screen_idx < 0 || screen_idx >= NUM_SCREENS) return true;
    if (screen_idx == 12) return true; /* Complete Layout XML (All) */

    const LayoutScreenCategory *sc = &g_screens[screen_idx];
    if (sc->filter_section) {
        return (_stricmp(el->section, sc->filter_section) == 0);
    }

    /* Screen 2: Adventure Map combines AdventureTemples, AdventureDoors, AdventureButtons, AdventureLabels */
    if (screen_idx == 2) {
        return (_stricmp(el->section, "AdventureTemples") == 0 ||
                _stricmp(el->section, "AdventureDoors") == 0 ||
                _stricmp(el->section, "AdventureButtons") == 0 ||
                _stricmp(el->section, "AdventureLabels") == 0);
    }

    /* Screen 3: Gauntlet Screen combines GauntletDoors, GauntletButtons, GauntletLabels */
    if (screen_idx == 3) {
        return (_stricmp(el->section, "GauntletDoors") == 0 ||
                _stricmp(el->section, "GauntletButtons") == 0 ||
                _stricmp(el->section, "GauntletLabels") == 0);
    }

    return true;
}

/* Check if an element belongs to the active sub-section filter */
static bool is_element_in_sub_filter(const LayoutElement *el, int screen_idx, int sub_idx) {
    if (sub_idx <= 0) return true;

    /* Screen 2: Adventure Map */
    if (screen_idx == 2) {
        if (sub_idx == 1) return (_stricmp(el->section, "AdventureDoors") == 0);
        if (sub_idx == 2) return (_stricmp(el->section, "AdventureTemples") == 0);
        if (sub_idx == 3) return (_stricmp(el->section, "AdventureButtons") == 0);
        if (sub_idx == 4) return (_stricmp(el->section, "AdventureLabels") == 0);
    }

    /* Screen 3: Gauntlet Screen */
    if (screen_idx == 3) {
        if (sub_idx == 1) return (_stricmp(el->section, "GauntletDoors") == 0);
        if (sub_idx == 2) return (_stricmp(el->section, "GauntletButtons") == 0);
        if (sub_idx == 3) return (_stricmp(el->section, "GauntletLabels") == 0);
    }

    /* Screen 4: In-Game HUD */
    if (screen_idx == 4) {
        if (sub_idx == 1) return (_stricmp(el->tag, "Menubar") == 0 ||
                                  _stricmp(el->tag, "MenuBtn") == 0 ||
                                  _stricmp(el->tag, "ProgressBar") == 0 ||
                                  _stricmp(el->tag, "StageName") == 0 ||
                                  _stricmp(el->tag, "ScoreText") == 0 ||
                                  _stricmp(el->tag, "LivesPos") == 0);
        if (sub_idx == 2) return (_stricmp(el->tag, "MenubarMargins") == 0);
        if (sub_idx == 3) return (strstr(el->tag, "Banner") != NULL);
    }

    /* Screen 5: Stage Complete */
    if (screen_idx == 5) {
        if (sub_idx == 1) return (_stricmp(el->tag, "StageText") == 0 || _stricmp(el->tag, "CompleteText") == 0);
        if (sub_idx == 2) return (strstr(el->tag, "Frog") != NULL);
    }

    /* Screen 6: Temple Cleared */
    if (screen_idx == 6) {
        if (sub_idx == 1) return (_stricmp(el->tag, "TikiHead") == 0 || _stricmp(el->tag, "FrogMedallion") == 0);
        if (sub_idx == 2) return (_stricmp(el->tag, "DialogText") == 0 || _stricmp(el->tag, "FrogReply") == 0);
        if (sub_idx == 3) return (_stricmp(el->tag, "Title") == 0 || _stricmp(el->tag, "ContinueBtn") == 0);
    }

    /* Screen 8: Help Screen */
    if (screen_idx == 8) {
        if (sub_idx == 1) return (_stricmp(el->tag, "HelpBack") == 0 || _stricmp(el->tag, "Skull") == 0 ||
                                  _stricmp(el->tag, "Coin") == 0 || _stricmp(el->tag, "Frog") == 0 ||
                                  _stricmp(el->tag, "TopText1") == 0 || _stricmp(el->tag, "TopText2") == 0);
        if (sub_idx == 2) return (_stricmp(el->tag, "SwapClick") == 0 || _stricmp(el->tag, "SwapMouse") == 0 ||
                                  _stricmp(el->tag, "BallTarget") == 0);
        if (sub_idx == 3) return (strncmp(el->tag, "Dev", 3) == 0);
    }

    /* Screen 11: DevTester Help */
    if (screen_idx == 11) {
        if (sub_idx == 1) return (strstr(el->tag, "Header") != NULL || _stricmp(el->tag, "Columns") == 0);
        if (sub_idx == 2) return (strstr(el->tag, "Bullets") != NULL);
        if (sub_idx == 3) return (strncmp(el->tag, "Demo", 4) == 0);
    }

    return true;
}

static bool is_element_visible_in_editor(const LayoutElement *el, const LayoutEditorState *st) {
    if (!el || !st) return false;
    if (!is_element_in_screen(el, st->cur_screen_idx)) return false;
    if (!is_element_in_sub_filter(el, st->cur_screen_idx, st->cur_sub_filter_idx)) return false;
    if (el->is_conditional && !st->show_conditional_sprites) return false;

    /* In Adventure Map "All Elements" view, Stage 13 labels share exact coords with Temples 1-4 labels.
       Default to showing Temples 1-4 labels; show Stage 13 labels when selected or hovered */
    if (st->cur_screen_idx == 2 && st->cur_sub_filter_idx == 0) {
        if (_strnicmp(el->tag, "Stage13", 7) == 0 && !el->selected && (st->hovered_elem != el)) {
            return false;
        }
    }


    /* In HelpScreen (Screen 8), hide Dev items (DevTemple5, DevMouseM) unless explicitly selected or hovered */
    if (st->cur_screen_idx == 8) {
        if (_strnicmp(el->tag, "Dev", 3) == 0 && !el->selected && (st->hovered_elem != el)) return false;
    }

    return true;
}

/* XML Attribute extraction helper */
static bool extract_xml_attr_int(const char *tag_str, const char *attr_name, int *out_val) {
    if (!tag_str || !attr_name) return false;
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s=\"", attr_name);
    const char *p = strstr(tag_str, pattern);
    while (p) {
        if (p == tag_str || isspace((unsigned char)*(p - 1))) {
            p += strlen(pattern);
            char *end = NULL;
            long val = strtol(p, &end, 10);
            if (end != p) {
                if (out_val) *out_val = (int)val;
                return true;
            }
            break;
        }
        p = strstr(p + 1, pattern);
    }

    snprintf(pattern, sizeof(pattern), "%s='", attr_name);
    p = strstr(tag_str, pattern);
    while (p) {
        if (p == tag_str || isspace((unsigned char)*(p - 1))) {
            p += strlen(pattern);
            char *end = NULL;
            long val = strtol(p, &end, 10);
            if (end != p) {
                if (out_val) *out_val = (int)val;
                return true;
            }
            break;
        }
        p = strstr(p + 1, pattern);
    }

    /* Fallback for unquoted attributes (e.g. rows=30) */
    snprintf(pattern, sizeof(pattern), "%s=", attr_name);
    p = strstr(tag_str, pattern);
    while (p) {
        if (p == tag_str || isspace((unsigned char)*(p - 1))) {
            p += strlen(pattern);
            if (*p == '\"' || *p == '\'') p++;
            char *end = NULL;
            long val = strtol(p, &end, 10);
            if (end != p) {
                if (out_val) *out_val = (int)val;
                return true;
            }
        }
        p = strstr(p + 1, pattern);
    }
    return false;
}

static bool extract_xml_attr_str(const char *tag_str, const char *attr_name, char *out_str, size_t max_len) {
    if (!tag_str || !attr_name || !out_str || max_len == 0) return false;
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s=\"", attr_name);
    const char *p = strstr(tag_str, pattern);
    char quote = '\"';
    while (p) {
        if (p == tag_str || isspace((unsigned char)*(p - 1))) {
            p += strlen(pattern);
            size_t i = 0;
            while (*p && *p != quote && i + 1 < max_len) {
                out_str[i++] = *p++;
            }
            out_str[i] = '\0';
            return (i > 0);
        }
        p = strstr(p + 1, pattern);
    }

    snprintf(pattern, sizeof(pattern), "%s='", attr_name);
    p = strstr(tag_str, pattern);
    quote = '\'';
    while (p) {
        if (p == tag_str || isspace((unsigned char)*(p - 1))) {
            p += strlen(pattern);
            size_t i = 0;
            while (*p && *p != quote && i + 1 < max_len) {
                out_str[i++] = *p++;
            }
            out_str[i] = '\0';
            return (i > 0);
        }
        p = strstr(p + 1, pattern);
    }
    return false;
}

/* Parse layout.xml into structured elements array */
/* Canonical section list */
static const char *g_canonical_sections[] = {
    "AdventureTemples", "AdventureDoors", "AdventureButtons", "AdventureLabels",
    "GauntletDoors", "GauntletButtons", "GauntletLabels",
    "MainMenu", "MoreGames", "LoadingScreen", "InGameHUD",
    "StageComplete", "TempleCleared", "CreditsScreen", "HelpScreen",
    "Dialogs", "DevHelpScreen"
};
static const int g_num_canonical_sections = (int)(sizeof(g_canonical_sections) / sizeof(g_canonical_sections[0]));

static int get_canonical_section_index(const char *name) {
    if (!name || !*name) return -1;
    for (int s = 0; s < g_num_canonical_sections; ++s) {
        if (_stricmp(name, g_canonical_sections[s]) == 0) return s;
    }
    return -1;
}

static bool is_known_section_name(const char *name) {
    return (get_canonical_section_index(name) >= 0);
}

/* Check if an element belongs to a canonical section whose space matches target display resolution */
static bool is_element_space_1to1(const LayoutElement *el, const LayoutEditorState *st) {
    if (!el || !st || st->target_w <= 0 || st->target_h <= 0) return false;
    int sec_idx = get_canonical_section_index(el->section);
    if (sec_idx >= 0 && st->section_space[sec_idx][0] != '\0') {
        int sw = 0, sh = 0;
        if (sscanf(st->section_space[sec_idx], "%dx%d", &sw, &sh) == 2) {
            return (sw == st->target_w && sh == st->target_h);
        }
    }
    if (_stricmp(el->section, "InGameHUD") == 0 && st->hud_space_w > 0) {
        return (st->hud_space_w == st->target_w && st->hud_space_h == st->target_h);
    }
    if (_stricmp(el->section, "LoadingScreen") == 0 && st->loading_space_w > 0) {
        return (st->loading_space_w == st->target_w && st->loading_space_h == st->target_h);
    }
    if (_stricmp(el->section, "MainMenu") == 0 && st->main_menu_space_w > 0) {
        return (st->main_menu_space_w == st->target_w && st->main_menu_space_h == st->target_h);
    }
    return false;
}

/* Check if current editor screen space matches target display resolution */
static bool is_current_screen_space_1to1(const LayoutEditorState *st) {
    if (!st || st->target_w <= 0 || st->target_h <= 0) return false;
    int scr = st->cur_screen_idx;
    if (scr == 4 && st->hud_space_w == st->target_w && st->hud_space_h == st->target_h) return true;
    if (scr == 1 && st->main_menu_space_w == st->target_w && st->main_menu_space_h == st->target_h) return true;
    if (scr == 0 && st->loading_space_w == st->target_w && st->loading_space_h == st->target_h) return true;
    if (scr == 2 && st->adventure_space_w == st->target_w && st->adventure_space_h == st->target_h) return true;
    if (scr == 3 && st->gauntlet_space_w == st->target_w && st->gauntlet_space_h == st->target_h) return true;

    const char *sec_name = (scr >= 0 && scr < NUM_SCREENS) ? g_screens[scr].filter_section : NULL;
    if (!sec_name && scr == 2) sec_name = "AdventureTemples";
    if (!sec_name && scr == 3) sec_name = "GauntletDoors";
    if (sec_name) {
        int sec_idx = get_canonical_section_index(sec_name);
        if (sec_idx >= 0 && st->section_space[sec_idx][0] != '\0') {
            int sw = 0, sh = 0;
            if (sscanf(st->section_space[sec_idx], "%dx%d", &sw, &sh) == 2) {
                return (sw == st->target_w && sh == st->target_h);
            }
        }
    }
    return false;
}

static bool layout_xml_load(LayoutEditorState *st, const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) return false;

    st->element_count = 0;
    st->hud_space_w = 0;
    st->hud_space_h = 0;
    st->adventure_space_w = 0;
    st->adventure_space_h = 0;
    st->gauntlet_space_w = 0;
    st->gauntlet_space_h = 0;
    memset(st->section_space, 0, sizeof(st->section_space));
    memset(st->xml_header, 0, sizeof(st->xml_header));
    char line[1024];
    char cur_section[48] = {0};
    bool in_header = true;

    while (fgets(line, sizeof(line), fp)) {
        if (in_header) {
            char *chk = line;
            while (*chk && isspace((unsigned char)*chk)) chk++;
            if (*chk == '<') {
                char tag[32] = {0};
                if (sscanf(chk + 1, "%31[^> \t\r\n/]", tag) == 1 && _stricmp(tag, "Layout") == 0) {
                    in_header = false;
                    continue;
                }
            }
            size_t clen = strlen(st->xml_header);
            size_t llen = strlen(line);
            if (clen + llen + 1 < sizeof(st->xml_header)) {
                memcpy(st->xml_header + clen, line, llen + 1);
            }
            continue;
        }

        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;

        /* Section open/close check: <AdventureDoors> or </AdventureDoors> */
        if (*p == '<' && p[1] != '?' && p[1] != '!') {
            if (p[1] == '/') {
                /* Section closing */
                char close_sec[48] = {0};
                sscanf(p + 2, "%47[^> \t\r\n]", close_sec);
                if (_stricmp(close_sec, cur_section) == 0) {
                    cur_section[0] = '\0';
                }
                continue;
            }

            /* Check if this is a section header or a self-closing/child tag */
            char tag_name[48] = {0};
            if (sscanf(p + 1, "%47[^> \t\r\n/]", tag_name) != 1) continue;

            if (_stricmp(tag_name, "Layout") == 0) continue;

            /* Check section headers with space attribute, e.g. space="1200x675" or any custom value */
            int sec_idx = get_canonical_section_index(tag_name);
            int is_sec_header = (sec_idx >= 0 && !strstr(p, "/>"));
            int is_hud_open = 0;
            if (is_sec_header) {
                if (_stricmp(tag_name, "InGameHUD") == 0) is_hud_open = 1;
                char space_val[64] = {0};
                if (extract_xml_attr_str(p, "space", space_val, sizeof(space_val))) {
                    strncpy(st->section_space[sec_idx], space_val, sizeof(st->section_space[sec_idx]) - 1);
                    st->section_space[sec_idx][sizeof(st->section_space[sec_idx]) - 1] = 0;
                    int sw = 0, sh = 0;
                    if (sscanf(space_val, "%dx%d", &sw, &sh) == 2) {
                        if (_stricmp(tag_name, "InGameHUD") == 0) {
                            st->hud_space_w = sw;
                            st->hud_space_h = sh;
                        } else if (_stricmp(tag_name, "MainMenu") == 0) {
                            st->main_menu_space_w = sw;
                            st->main_menu_space_h = sh;
                        } else if (_stricmp(tag_name, "LoadingScreen") == 0) {
                            st->loading_space_w = sw;
                            st->loading_space_h = sh;
                        } else if (_strnicmp(tag_name, "Adventure", 9) == 0) {
                            st->adventure_space_w = sw;
                            st->adventure_space_h = sh;
                        } else if (_strnicmp(tag_name, "Gauntlet", 8) == 0) {
                            st->gauntlet_space_w = sw;
                            st->gauntlet_space_h = sh;
                        }
                    }
                }
            }

            /* If line ends with '>' and has no attributes (or is InGameHUD section header), it's a section header */
            char *slash = strchr(p, '/');
            char *quote = strchr(p, '\"');
            bool is_known_sec = is_known_section_name(tag_name);
            if ((!slash && !quote) || is_hud_open || (is_known_sec && !strstr(p, "/>"))) {
                strncpy(cur_section, tag_name, sizeof(cur_section) - 1);
                cur_section[sizeof(cur_section) - 1] = '\0';
                continue;
            }

            /* It is an element tag! */
            if (st->element_count >= MAX_LAYOUT_ELEMENTS) break;
            LayoutElement *el = &st->elements[st->element_count];
            memset(el, 0, sizeof(LayoutElement));
            strncpy(el->section, cur_section[0] ? cur_section : "General", sizeof(el->section) - 1);
            if (_stricmp(tag_name, "EyeBlink") == 0) {
                strncpy(el->tag, "Hat", sizeof(el->tag) - 1);
            } else if (_stricmp(tag_name, "Sun1") == 0 || _stricmp(tag_name, "Sun2") == 0) {
                strncpy(el->tag, "Sun", sizeof(el->tag) - 1);
            } else {
                strncpy(el->tag, tag_name, sizeof(el->tag) - 1);
            }

            /* Extract id */
            if (extract_xml_attr_str(p, "id", el->id, sizeof(el->id))) {
                el->flags |= ATTR_HAS_ID;
            }

            /* Extract align */
            if (extract_xml_attr_str(p, "align", el->align, sizeof(el->align))) {
                el->flags |= ATTR_HAS_ALIGN;
            }

            /* Extract standard coordinates */
            if (extract_xml_attr_int(p, "x", &el->x)) el->flags |= ATTR_HAS_X;
            if (extract_xml_attr_int(p, "rightX", &el->x)) { el->flags |= ATTR_HAS_X; el->flags |= ATTR_HAS_SCORE_RX; }
            if (extract_xml_attr_int(p, "y", &el->y)) el->flags |= ATTR_HAS_Y;
            if (extract_xml_attr_int(p, "w", &el->w)) el->flags |= ATTR_HAS_W;
            if (extract_xml_attr_int(p, "h", &el->h)) el->flags |= ATTR_HAS_H;
            if (extract_xml_attr_int(p, "width", &el->w)) { el->flags |= ATTR_HAS_W; el->use_width_height_names = true; }
            if (extract_xml_attr_int(p, "height", &el->h)) { el->flags |= ATTR_HAS_H; el->use_width_height_names = true; }

            /* Extract AdventureDoor specific coordinates */
            if (extract_xml_attr_int(p, "doorX", &el->doorX)) { el->flags |= ATTR_HAS_DOOR_X; el->flags |= ATTR_IS_DOOR_PAIR; }
            if (extract_xml_attr_int(p, "doorY", &el->doorY)) { el->flags |= ATTR_HAS_DOOR_Y; el->flags |= ATTR_IS_DOOR_PAIR; }
            if (extract_xml_attr_int(p, "frogX", &el->frogX)) { el->flags |= ATTR_HAS_FROG_X; el->flags |= ATTR_IS_DOOR_PAIR; }
            if (extract_xml_attr_int(p, "frogY", &el->frogY)) { el->flags |= ATTR_HAS_FROG_Y; el->flags |= ATTR_IS_DOOR_PAIR; }

            /* Backward compatibility: x1/y1 and x2/y2 */
            if (!(el->flags & ATTR_HAS_DOOR_X) && extract_xml_attr_int(p, "x1", &el->doorX)) { el->flags |= ATTR_HAS_DOOR_X; el->flags |= ATTR_IS_DOOR_PAIR; }
            if (!(el->flags & ATTR_HAS_DOOR_Y) && extract_xml_attr_int(p, "y1", &el->doorY)) { el->flags |= ATTR_HAS_DOOR_Y; el->flags |= ATTR_IS_DOOR_PAIR; }
            if (!(el->flags & ATTR_HAS_FROG_X) && extract_xml_attr_int(p, "x2", &el->frogX)) { el->flags |= ATTR_HAS_FROG_X; el->flags |= ATTR_IS_DOOR_PAIR; }
            if (!(el->flags & ATTR_HAS_FROG_Y) && extract_xml_attr_int(p, "y2", &el->frogY)) { el->flags |= ATTR_HAS_FROG_Y; el->flags |= ATTR_IS_DOOR_PAIR; }

            /* Center & Offsets */
            if (extract_xml_attr_int(p, "centerX", &el->centerX)) el->flags |= ATTR_HAS_CENTER_X;
            if (extract_xml_attr_int(p, "centerY", &el->centerY)) el->flags |= ATTR_HAS_CENTER_Y;
            if (extract_xml_attr_int(p, "offsetX", &el->offsetX)) el->flags |= ATTR_HAS_OFFSET_X;
            if (extract_xml_attr_int(p, "offsetY", &el->offsetY)) el->flags |= ATTR_HAS_OFFSET_Y;

            /* Spacing and margins */
            if (extract_xml_attr_int(p, "minTop", &el->minTop)) el->flags |= ATTR_HAS_MIN_TOP;
            if (extract_xml_attr_int(p, "spacing", &el->spacing)) el->flags |= ATTR_HAS_SPACING;
            if (extract_xml_attr_int(p, "lineSpacing", &el->lineSpacing)) el->flags |= ATTR_HAS_LINESPACING;

            /* InGame Menubar HUD positions */
            if (extract_xml_attr_int(p, "livesX", &el->livesX)) el->flags |= ATTR_HAS_LIVES_X;
            if (extract_xml_attr_int(p, "stageCX", &el->stageCX)) el->flags |= ATTR_HAS_STAGE_CX;
            if (extract_xml_attr_int(p, "scoreRX", &el->scoreRX)) el->flags |= ATTR_HAS_SCORE_RX;
            if (extract_xml_attr_int(p, "zumaX", &el->zumaX)) el->flags |= ATTR_HAS_ZUMA_X;
            if (extract_xml_attr_int(p, "menuX", &el->menuX)) el->flags |= ATTR_HAS_MENU_X;

            /* Menubar margins */
            if (extract_xml_attr_int(p, "top", &el->top)) el->flags |= ATTR_HAS_TOP;
            if (extract_xml_attr_int(p, "bottom", &el->bottom)) el->flags |= ATTR_HAS_BOTTOM;
            if (extract_xml_attr_int(p, "left", &el->left)) el->flags |= ATTR_HAS_LEFT;
            if (extract_xml_attr_int(p, "right", &el->right)) el->flags |= ATTR_HAS_RIGHT;

            /* DevTester columns & waypoints */
            if (extract_xml_attr_int(p, "col1X", &el->col1X)) el->flags |= ATTR_HAS_COL1_X;
            if (extract_xml_attr_int(p, "col2X", &el->col2X)) el->flags |= ATTR_HAS_COL2_X;
            if (extract_xml_attr_int(p, "bx", &el->bx)) el->flags |= ATTR_HAS_BX;
            if (extract_xml_attr_int(p, "cx", &el->cx)) el->flags |= ATTR_HAS_CX;
            if (extract_xml_attr_int(p, "dx", &el->dx)) el->flags |= ATTR_HAS_DX;

            assign_element_default_bounds(el, st);
            st->element_count++;
        }
    }

    fclose(fp);
    return (st->element_count > 0);
}

/* Save all elements back to layout.xml adhering to canonical 17 sections */
static bool layout_xml_save(LayoutEditorState *st, const char *filepath) {
    FILE *fp = fopen(filepath, "wb");
    if (!fp) return false;

    if (st->xml_header[0] != '\0') {
        fputs(st->xml_header, fp);
        fprintf(fp, "<Layout>\n");
    } else {
        fprintf(fp, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
        fprintf(fp, "<!--\n");
        fprintf(fp, "  =============================================================================\n");
        fprintf(fp, "  ZuCompleluxe UI Layout Configuration (properties/layout.xml)\n");
        fprintf(fp, "  Exported cleanly by ZuPather Layout Editor\n");
        fprintf(fp, "  =============================================================================\n");
        fprintf(fp, "-->\n");
        fprintf(fp, "<Layout>\n");
    }

    /* Canonical section list */
    static const char *canonical_sections[] = {
        "AdventureTemples",
        "AdventureDoors",
        "AdventureButtons",
        "AdventureLabels",
        "GauntletDoors",
        "GauntletButtons",
        "GauntletLabels",
        "MainMenu",
        "MoreGames",
        "LoadingScreen",
        "InGameHUD",
        "StageComplete",
        "TempleCleared",
        "CreditsScreen",
        "HelpScreen",
        "Dialogs",
        "DevHelpScreen"
    };
    int num_sec = (int)(sizeof(canonical_sections) / sizeof(canonical_sections[0]));

    for (int s = 0; s < num_sec; ++s) {
        const char *sec_name = canonical_sections[s];

        /* Check if we have any elements in this section */
        bool has_sec = false;
        for (int i = 0; i < st->element_count; ++i) {
            if (_stricmp(st->elements[i].section, sec_name) == 0) {
                has_sec = true;
                break;
            }
        }
        if (!has_sec) continue;

        int sec_idx = get_canonical_section_index(sec_name);
        const char *sec_space = (sec_idx >= 0 && st->section_space[sec_idx][0]) ? st->section_space[sec_idx] : NULL;

        if (sec_space && *sec_space) {
            fprintf(fp, "    <%s space=\"%s\">\n", sec_name, sec_space);
        } else if (_stricmp(sec_name, "InGameHUD") == 0) {
            int sw = st->hud_space_w > 0 ? st->hud_space_w : 1200;
            int sh = st->hud_space_h > 0 ? st->hud_space_h : 675;
            fprintf(fp, "    <InGameHUD space=\"%dx%d\">\n", sw, sh);
        } else if (_stricmp(sec_name, "MainMenu") == 0 && st->main_menu_space_w > 0) {
            fprintf(fp, "    <MainMenu space=\"%dx%d\">\n", st->main_menu_space_w, st->main_menu_space_h);
        } else if (_stricmp(sec_name, "LoadingScreen") == 0 && st->loading_space_w > 0) {
            fprintf(fp, "    <LoadingScreen space=\"%dx%d\">\n", st->loading_space_w, st->loading_space_h);
        } else if ((_stricmp(sec_name, "AdventureTemples") == 0 || _stricmp(sec_name, "AdventureDoors") == 0 || _stricmp(sec_name, "AdventureButtons") == 0 || _stricmp(sec_name, "AdventureLabels") == 0) && st->adventure_space_w > 0) {
            fprintf(fp, "    <%s space=\"%dx%d\">\n", sec_name, st->adventure_space_w, st->adventure_space_h);
        } else {
            fprintf(fp, "    <%s>\n", sec_name);
        }

        /* Dual-write sync for InGameHUD: ensure Menubar bundled attributes mirror granular widgets */
        if (_stricmp(sec_name, "InGameHUD") == 0) {
            LayoutElement *mb = NULL;
            LayoutElement *mbtn = NULL, *pbar = NULL, *stg = NULL, *scr = NULL, *lp = NULL;
            for (int i = 0; i < st->element_count; ++i) {
                if (_stricmp(st->elements[i].section, "InGameHUD") != 0) continue;
                if (_stricmp(st->elements[i].tag, "Menubar") == 0) mb = &st->elements[i];
                else if (_stricmp(st->elements[i].tag, "MenuBtn") == 0) mbtn = &st->elements[i];
                else if (_stricmp(st->elements[i].tag, "ProgressBar") == 0) pbar = &st->elements[i];
                else if (_stricmp(st->elements[i].tag, "StageName") == 0) stg = &st->elements[i];
                else if (_stricmp(st->elements[i].tag, "ScoreText") == 0) scr = &st->elements[i];
                else if (_stricmp(st->elements[i].tag, "LivesPos") == 0) lp = &st->elements[i];
            }
            if (mb) {
                if (mbtn) { mb->menuX = mbtn->x; mb->flags |= ATTR_HAS_MENU_X; }
                if (pbar) { mb->zumaX = pbar->x; mb->flags |= ATTR_HAS_ZUMA_X; }
                if (scr)  { mb->scoreRX = scr->x; mb->flags |= ATTR_HAS_SCORE_RX; }
                if (stg)  { mb->stageCX = stg->centerX; mb->flags |= ATTR_HAS_STAGE_CX; }
                if (lp)   { mb->livesX = lp->x; mb->flags |= ATTR_HAS_LIVES_X; }
            }
        }

        for (int i = 0; i < st->element_count; ++i) {
            LayoutElement *el = &st->elements[i];
            if (_stricmp(el->section, sec_name) != 0) continue;

            fprintf(fp, "        <%s", el->tag);

            if (el->flags & ATTR_HAS_ID) fprintf(fp, " id=\"%s\"", el->id);

            if (_stricmp(el->tag, "ScoreText") == 0) {
                fprintf(fp, " rightX=\"%d\"", el->x);
                if (el->flags & ATTR_HAS_Y) fprintf(fp, " y=\"%d\"", el->y);
                fprintf(fp, " font=\"Cancun12\" color=\"#FFF000\"");
            } else if (_stricmp(el->tag, "StageName") == 0) {
                if (el->flags & ATTR_HAS_CENTER_X) fprintf(fp, " centerX=\"%d\"", el->centerX);
                if (el->flags & ATTR_HAS_Y) fprintf(fp, " y=\"%d\"", el->y);
                fprintf(fp, " font=\"Cancun12\" color=\"#FFF000\"");
            } else {
                /* Door pair priority */
                if (el->flags & ATTR_IS_DOOR_PAIR) {
                    fprintf(fp, " doorX=\"%d\" doorY=\"%d\" frogX=\"%d\" frogY=\"%d\"",
                            el->doorX, el->doorY, el->frogX, el->frogY);
                } else {
                    if (el->flags & ATTR_HAS_X) fprintf(fp, " x=\"%d\"", el->x);
                    if (el->flags & ATTR_HAS_Y) fprintf(fp, " y=\"%d\"", el->y);
                }

                if (el->flags & ATTR_HAS_W) {
                    if (el->use_width_height_names) fprintf(fp, " width=\"%d\"", el->w);
                    else fprintf(fp, " w=\"%d\"", el->w);
                }
                if (el->flags & ATTR_HAS_H) {
                    if (el->use_width_height_names) fprintf(fp, " height=\"%d\"", el->h);
                    else fprintf(fp, " h=\"%d\"", el->h);
                }
                if (el->flags & ATTR_HAS_CENTER_X) fprintf(fp, " centerX=\"%d\"", el->centerX);
                if (el->flags & ATTR_HAS_CENTER_Y) fprintf(fp, " centerY=\"%d\"", el->centerY);
                if (el->flags & ATTR_HAS_OFFSET_X) fprintf(fp, " offsetX=\"%d\"", el->offsetX);
                if (el->flags & ATTR_HAS_OFFSET_Y) fprintf(fp, " offsetY=\"%d\"", el->offsetY);
                if (el->flags & ATTR_HAS_MIN_TOP) fprintf(fp, " minTop=\"%d\"", el->minTop);
                if (el->flags & ATTR_HAS_SPACING) fprintf(fp, " spacing=\"%d\"", el->spacing);
                if (el->flags & ATTR_HAS_LINESPACING) fprintf(fp, " lineSpacing=\"%d\"", el->lineSpacing);

                /* Menubar */
                if (el->flags & ATTR_HAS_LIVES_X) fprintf(fp, " livesX=\"%d\"", el->livesX);
                if (el->flags & ATTR_HAS_STAGE_CX) fprintf(fp, " stageCX=\"%d\"", el->stageCX);
                if (el->flags & ATTR_HAS_SCORE_RX) fprintf(fp, " scoreRX=\"%d\"", el->scoreRX);
                if (el->flags & ATTR_HAS_ZUMA_X) fprintf(fp, " zumaX=\"%d\"", el->zumaX);
                if (el->flags & ATTR_HAS_MENU_X) fprintf(fp, " menuX=\"%d\"", el->menuX);

                /* Margins */
                if (el->flags & ATTR_HAS_TOP) fprintf(fp, " top=\"%d\"", el->top);
                if (el->flags & ATTR_HAS_BOTTOM) fprintf(fp, " bottom=\"%d\"", el->bottom);
                if (el->flags & ATTR_HAS_LEFT) fprintf(fp, " left=\"%d\"", el->left);
                if (el->flags & ATTR_HAS_RIGHT) fprintf(fp, " right=\"%d\"", el->right);

                /* Columns & Waypoints */
                if (el->flags & ATTR_HAS_COL1_X) fprintf(fp, " col1X=\"%d\"", el->col1X);
                if (el->flags & ATTR_HAS_COL2_X) fprintf(fp, " col2X=\"%d\"", el->col2X);
                if (el->flags & ATTR_HAS_BX) fprintf(fp, " bx=\"%d\"", el->bx);
                if (el->flags & ATTR_HAS_CX) fprintf(fp, " cx=\"%d\"", el->cx);
                if (el->flags & ATTR_HAS_DX) fprintf(fp, " dx=\"%d\"", el->dx);

                if (el->flags & ATTR_HAS_ALIGN && el->align[0]) fprintf(fp, " align=\"%s\"", el->align);
            }

            fprintf(fp, " />\n");
        }

        fprintf(fp, "    </%s>\n", sec_name);
    }

    fprintf(fp, "</Layout>\n");
    fclose(fp);
    return true;
}

/* Load background image corresponding to active screen prioritizing imagescustomres\ first */
static void layout_editor_load_screen_background(LayoutEditorState *st) {
    if (st->has_bg_image) {
        image_free(&st->bg_image);
        st->has_bg_image = false;
        st->current_bg_name[0] = '\0';
    }

    const LayoutScreenCategory *sc = &g_screens[st->cur_screen_idx];
    if (!sc->bg_image_name || sc->bg_image_name[0] == '\0') return;

    char img_path[MAX_PATH] = {0};
    if (st->cur_screen_idx == 4) {
        char root_dir[MAX_PATH] = {0};
        get_zuma_game_root_dir(root_dir, sizeof(root_dir));
        char candidate_clawc[MAX_PATH];
        snprintf(candidate_clawc, sizeof(candidate_clawc), "%s\\levels\\clawc\\clawc.jpg", root_dir);
        if (GetFileAttributesA(candidate_clawc) != INVALID_FILE_ATTRIBUTES) {
            strncpy(img_path, candidate_clawc, sizeof(img_path) - 1);
        } else if (g_zuma_levels_dir[0] != '\0') {
            char test_lvl[MAX_PATH];
            const char *slash = strrchr(g_zuma_levels_dir, '\\');
            const char *folder_name = slash ? slash + 1 : "clawc";
            snprintf(test_lvl, sizeof(test_lvl), "%s\\%s.jpg", g_zuma_levels_dir, folder_name);
            if (GetFileAttributesA(test_lvl) != INVALID_FILE_ATTRIBUTES) {
                strncpy(img_path, test_lvl, sizeof(img_path) - 1);
            }
        }
    }

    if (img_path[0] == '\0') {
        layout_get_zuma_asset_path(sc->bg_image_name, img_path, sizeof(img_path));
    }

    if (img_path[0] != '\0') {
        if (image_load_from_file(img_path, &st->bg_image)) {
            st->has_bg_image = true;
            strncpy(st->current_bg_name, sc->bg_image_name, sizeof(st->current_bg_name) - 1);
        }
    }
}

/* Parse align attribute into canonical integer ID (identical to ZuCompleluxe ZRH_ParseAlignAttr) */
static int ZRH_ParseAlignAttr(const char *val) {
    if (!val) return 0;
    while (*val == ' ' || *val == '\t' || *val == '"' || *val == '\'') val++;
    if (_strnicmp(val, "top-left", 8) == 0 || _strnicmp(val, "tl", 2) == 0) return 1;
    if (_strnicmp(val, "top-middle", 10) == 0 || _strnicmp(val, "top-center", 10) == 0 || _strnicmp(val, "tc", 2) == 0 || _strnicmp(val, "tm", 2) == 0) return 2;
    if (_strnicmp(val, "top-right", 9) == 0 || _strnicmp(val, "tr", 2) == 0) return 3;
    if (_strnicmp(val, "middle-left", 11) == 0 || _strnicmp(val, "center-left", 11) == 0 || _strnicmp(val, "ml", 2) == 0 || _strnicmp(val, "cl", 2) == 0) return 4;
    if (_strnicmp(val, "middle-center", 13) == 0 || _strnicmp(val, "center", 6) == 0 || _strnicmp(val, "middle", 6) == 0 || _strnicmp(val, "c", 1) == 0) return 5;
    if (_strnicmp(val, "middle-right", 12) == 0 || _strnicmp(val, "center-right", 12) == 0 || _strnicmp(val, "mr", 2) == 0 || _strnicmp(val, "cr", 2) == 0) return 6;
    if (_strnicmp(val, "bottom-left", 11) == 0 || _strnicmp(val, "bl", 2) == 0) return 7;
    if (_strnicmp(val, "bottom-middle", 13) == 0 || _strnicmp(val, "bottom-center", 13) == 0 || _strnicmp(val, "bc", 2) == 0 || _strnicmp(val, "bm", 2) == 0) return 8;
    if (_strnicmp(val, "bottom-right", 12) == 0 || _strnicmp(val, "br", 2) == 0) return 9;
    return 0;
}

/* Resolve anchor cluster position (identical to ZuCompleluxe ZRH_ResolveAnchorPos) */
static void ZRH_ResolveAnchorPos(int in_x, int in_y, int align, int W, int H, float scale, int *out_x, int *out_y) {
    switch (align) {
        case 1: /* top-left */
            if (out_x) *out_x = (int)((float)in_x * scale + 0.5f);
            if (out_y) *out_y = (int)((float)in_y * scale + 0.5f);
            break;
        case 2: /* top-middle / top-center */
            if (out_x) *out_x = W / 2 + (int)((float)(in_x - 320) * scale + 0.5f);
            if (out_y) *out_y = (int)((float)in_y * scale + 0.5f);
            break;
        case 3: /* top-right */
            if (out_x) *out_x = W - (int)((float)(640 - in_x) * scale + 0.5f);
            if (out_y) *out_y = (int)((float)in_y * scale + 0.5f);
            break;
        case 4: /* middle-left / center-left */
            if (out_x) *out_x = (int)((float)in_x * scale + 0.5f);
            if (out_y) *out_y = H / 2 + (int)((float)(in_y - 240) * scale + 0.5f);
            break;
        case 5: /* center / middle */
            if (out_x) *out_x = W / 2 + (int)((float)(in_x - 320) * scale + 0.5f);
            if (out_y) *out_y = H / 2 + (int)((float)(in_y - 240) * scale + 0.5f);
            break;
        case 6: /* middle-right / center-right */
            if (out_x) *out_x = W - (int)((float)(640 - in_x) * scale + 0.5f);
            if (out_y) *out_y = H / 2 + (int)((float)(in_y - 240) * scale + 0.5f);
            break;
        case 7: /* bottom-left */
            if (out_x) *out_x = (int)((float)in_x * scale + 0.5f);
            if (out_y) *out_y = H - (int)((float)(480 - in_y) * scale + 0.5f);
            break;
        case 8: /* bottom-middle / bottom-center */
            if (out_x) *out_x = W / 2 + (int)((float)(in_x - 320) * scale + 0.5f);
            if (out_y) *out_y = H - (int)((float)(480 - in_y) * scale + 0.5f);
            break;
        case 9: /* bottom-right */
            if (out_x) *out_x = W - (int)((float)(640 - in_x) * scale + 0.5f);
            if (out_y) *out_y = H - (int)((float)(480 - in_y) * scale + 0.5f);
            break;
        default: /* 0: standard proportional with letterbox/pillarbox offset */
            if (out_x) {
                int cw = (int)(640.0f * scale + 0.5f);
                int xo = (W - cw) / 2;
                *out_x = (int)((float)in_x * scale + 0.5f) + xo;
            }
            if (out_y) {
                int ch = (int)(480.0f * scale + 0.5f);
                int yo = (H - ch) / 2;
                *out_y = (int)((float)in_y * scale + 0.5f) + yo;
            }
            break;
    }
}

/* Coordinate conversion math: Target Resolution & Canvas Viewport */
static void calculate_viewport_transforms(
    LayoutEditorState *st, int cv_w, int cv_h,
    float *out_vscale, int *out_tx, int *out_ty, int *out_tw, int *out_th,
    float *out_zscale, int *out_xo, int *out_yo, int *out_cw, int *out_ch
) {
    int tw = st->target_w;
    int th = st->target_h;
    if (tw <= 0) tw = 640;
    if (th <= 0) th = 480;

    /* Viewport fit scale */
    float vscale_x = (float)(cv_w - 32) / (float)tw;
    float vscale_y = (float)(cv_h - 32) / (float)th;
    float vscale = (vscale_x < vscale_y) ? vscale_x : vscale_y;
    if (vscale > 1.25f) vscale = 1.25f;

    int render_tw = (int)roundf((float)tw * vscale);
    int render_th = (int)roundf((float)th * vscale);
    int tx = (cv_w - render_tw) / 2;
    int ty = (cv_h - render_th) / 2;

    /* Zuma Compleluxe canonical scaling inside target resolution */
    float zscale_x = (float)tw / 640.0f;
    float zscale_y = (float)th / 480.0f;
    float zscale = (zscale_x < zscale_y) ? zscale_x : zscale_y;

    int cw = (int)roundf(640.0f * zscale);
    int ch = (int)roundf(480.0f * zscale);
    int xo = (tw - cw) / 2;
    int yo = (th - ch) / 2;

    int scr = st ? st->cur_screen_idx : 0;
    if (scr == 4 || scr == 5 || scr == 6) {
        /* Modes B and C: In-Game full widescreen without pillars */
        cw = tw;
        ch = th;
        xo = 0;
        yo = 0;
    }

    if (out_vscale) *out_vscale = vscale;
    if (out_tx) *out_tx = tx;
    if (out_ty) *out_ty = ty;
    if (out_tw) *out_tw = render_tw;
    if (out_th) *out_th = render_th;
    if (out_zscale) *out_zscale = zscale;
    if (out_xo) *out_xo = xo;
    if (out_yo) *out_yo = yo;
    if (out_cw) *out_cw = cw;
    if (out_ch) *out_ch = ch;
}

/* Convert canonical coordinate to Canvas pixel coordinate */
static void canonical_to_canvas(
    LayoutEditorState *st, int cv_w, int cv_h,
    int can_x, int can_y, int *out_vx, int *out_vy
) {
    float vscale, zscale;
    int tx, ty, tw, th, xo, yo, cw, ch;
    calculate_viewport_transforms(st, cv_w, cv_h, &vscale, &tx, &ty, &tw, &th, &zscale, &xo, &yo, &cw, &ch);

    int scr = st ? st->cur_screen_idx : 0;
    float sx, sy;
    if (scr == 6) {
        int dx = (st->target_w - 640) / 2;
        int dy = (st->target_h - 480) / 2;
        sx = (float)(dx + can_x);
        sy = (float)(dy + can_y);
    } else if (scr == 5) {
        int dy = (st->target_h - 480) / 2;
        sx = (can_x == 320) ? ((float)st->target_w * 0.5f) : ((float)can_x * zscale);
        sy = (float)(can_y + dy);
    } else if (is_current_screen_space_1to1(st)) {
        sx = (float)can_x;
        sy = (float)can_y;
    } else {
        sx = (float)xo + (float)can_x * zscale;
        sy = (float)yo + (float)can_y * zscale;
    }

    if (out_vx) *out_vx = tx + (int)roundf(sx * vscale);
    if (out_vy) *out_vy = ty + (int)roundf(sy * vscale);
}

/* Convert Canvas pixel coordinate back to canonical coordinate */
static void canvas_to_canonical(
    LayoutEditorState *st, int cv_w, int cv_h,
    int vx, int vy, int *out_cx, int *out_cy
) {
    float vscale, zscale;
    int tx, ty, tw, th, xo, yo, cw, ch;
    calculate_viewport_transforms(st, cv_w, cv_h, &vscale, &tx, &ty, &tw, &th, &zscale, &xo, &yo, &cw, &ch);

    float sx = (float)(vx - tx) / vscale;
    float sy = (float)(vy - ty) / vscale;

    int scr = st ? st->cur_screen_idx : 0;
    float can_x, can_y;
    if (scr == 6) {
        int dx = (st->target_w - 640) / 2;
        int dy = (st->target_h - 480) / 2;
        can_x = sx - (float)dx;
        can_y = sy - (float)dy;
    } else if (scr == 5) {
        int dy = (st->target_h - 480) / 2;
        can_x = (zscale > 0.0f) ? (sx / zscale) : sx;
        can_y = sy - (float)dy;
    } else if (is_current_screen_space_1to1(st)) {
        can_x = sx;
        can_y = sy;
    } else {
        can_x = (zscale > 0.0f) ? ((sx - (float)xo) / zscale) : (sx - (float)xo);
        can_y = (zscale > 0.0f) ? ((sy - (float)yo) / zscale) : (sy - (float)yo);
    }

    if (out_cx) *out_cx = (int)roundf(can_x);
    if (out_cy) *out_cy = (int)roundf(can_y);
}

/* Element type classifier helpers */
static bool is_text_element(const LayoutElement *el) {
    if (!el) return false;
    const char *tag = el->tag ? el->tag : "";
    if (_stricmp(tag, "Title") == 0 ||
        _stricmp(tag, "DialogText") == 0 ||
        _stricmp(tag, "FrogReply") == 0 ||
        _stricmp(tag, "StageText") == 0 ||
        _stricmp(tag, "CompleteText") == 0 ||
        _stricmp(tag, "ClickToPlay") == 0 ||
        _stricmp(tag, "HighscoreScores") == 0 ||
        _stricmp(tag, "HighscoreNames") == 0 ||
        _stricmp(tag, "StageNum") == 0 ||
        _stricmp(tag, "Subtitle") == 0 ||
        _stricmp(tag, "Stage13Num") == 0 ||
        _stricmp(tag, "Stage13Title") == 0 ||
        _stricmp(tag, "Stage13Subtitle") == 0 ||
        _stricmp(tag, "DoorName") == 0 ||
        _stricmp(tag, "PracticeText") == 0 ||
        _stricmp(tag, "ScoreText") == 0 ||
        _stricmp(tag, "StageName") == 0 ||
        _stricmp(tag, "CustomMapTag") == 0 ||
        _stricmp(tag, "CreditsText") == 0 ||
        _stricmp(tag, "HeaderTitle") == 0 ||
        _stricmp(tag, "HeaderSubtitle") == 0 ||
        _stricmp(tag, "AdvHeader") == 0 ||
        _stricmp(tag, "AdvBullets") == 0 ||
        _stricmp(tag, "GauntHeader") == 0 ||
        _stricmp(tag, "GauntBullets") == 0 ||
        _stricmp(tag, "CheatHeader") == 0 ||
        _stricmp(tag, "CheatBullets") == 0 ||
        _stricmp(tag, "DemoClear") == 0 ||
        _stricmp(tag, "DemoClick") == 0 ||
        _stricmp(tag, "TopText1") == 0 ||
        _stricmp(tag, "TopText2") == 0 ||
        _stricmp(tag, "SwapClick") == 0 ||
        _stricmp(tag, "BallTarget") == 0 ||
        _stricmp(tag, "WelcomeText") == 0 ||
        _stricmp(tag, "UserSwitchText") == 0 ||
        _stricmp(tag, "InfoBox") == 0) {
        return true;
    }
    return false;
}

static bool is_region_element(const LayoutElement *el) {
    if (!el) return false;
    const char *tag = el->tag ? el->tag : "";
    if (_stricmp(tag, "MenubarMargins") == 0 ||
        _stricmp(tag, "HighscoreClip") == 0 ||
        _stricmp(tag, "HighscoreDate") == 0 ||
        _strnicmp(tag, "Thumb", 5) == 0 ||
        _stricmp(tag, "ScrollArea") == 0 ||
        _stricmp(tag, "Dialog") == 0 ||
        _stricmp(tag, "DevDialog") == 0) {
        return true;
    }
    if (el->has_explicit_size && !is_text_element(el)) {
        return true;
    }
    return false;
}

static bool is_point_element(const LayoutElement *el) {
    if (!el) return false;
    if (is_text_element(el) || is_region_element(el)) return false;
    int cols = 1, rows = 1, cell = 0;
    if (get_element_sprite_name(el, &cols, &rows, &cell) != NULL) return false;
    return true;
}

/* Z-order sorting priority (based on Zuma Deluxe engine disassembly) */
static int get_element_z_order(const LayoutElement *el, int screen_idx) {
    if (!el) return 0;
    const char *sec = el->section ? el->section : "";
    const char *tag = el->tag ? el->tag : "";

    /* Adventure Screen */
    if (_stricmp(sec, "AdventureTemples") == 0) {
        if (_stricmp(tag, "AdvBack") == 0) return 5;
        if (_stricmp(tag, "Temple4") == 0) return 10;
        if (_stricmp(tag, "Middle") == 0)  return 20;
        if (_stricmp(tag, "Temple5") == 0) return 30;
        if (_stricmp(tag, "Temple3") == 0 || _stricmp(tag, "Temple3V") == 0) return 40;
        if (_stricmp(tag, "Temple2") == 0 || _stricmp(tag, "Temple2V") == 0) return 50;
        if (_stricmp(tag, "Temple1") == 0) return 60;
        return 25;
    }
    if (_stricmp(sec, "AdventureDoors") == 0 || (_stricmp(tag, "Door") == 0 && screen_idx == 2)) {
        int did = atoi(el->id);
        if (did >= 9 && did <= 11) return 15; /* Doors 10..12 on Temple 4 */
        if (did == 12) return 35;             /* Door 13 on Temple 5 */
        if (did >= 6 && did <= 8) return 45;  /* Doors 7..9 on Temple 3 */
        if (did >= 3 && did <= 5) return 55;  /* Doors 4..6 on Temple 2 */
        if (did >= 0 && did <= 2) return 65;  /* Doors 1..3 on Temple 1 */
        return 50;
    }
    if (_stricmp(sec, "AdventureButtons") == 0) return 110;
    if (_stricmp(sec, "AdventureLabels") == 0) {
        if (_stricmp(tag, "BottomBox") == 0 || _stricmp(tag, "Stage13Badge") == 0) return 70;
        if (_stricmp(tag, "AdventureTitle") == 0) return 90;
        if (_stricmp(tag, "HighscoreTitle") == 0) return 95;
        return 100;
    }

    /* Gauntlet Screen */
    if (_stricmp(sec, "GauntletDoors") == 0) return 30;
    if (_stricmp(sec, "GauntletLabels") == 0) {
        if (_stricmp(tag, "HighscoreClip") == 0) return 5;
        if (_stricmp(tag, "DoorFrame") == 0) return 20;
        if (_stricmp(tag, "Gem") == 0) return 40;
        return 90;
    }
    if (_stricmp(sec, "GauntletButtons") == 0) return 100;

    /* Main Menu */
    if (_stricmp(sec, "MainMenu") == 0) {
    if (_stricmp(tag, "Sun") == 0 || _stricmp(tag, "Sun1") == 0 || _stricmp(tag, "Sun2") == 0) return 5;
        if (_stricmp(tag, "EyeRay1") == 0 || _stricmp(tag, "EyeRay2") == 0) return 6;
        if (_stricmp(tag, "FrogEyes") == 0) return 10;
        if (_stricmp(tag, "EyeIdle1") == 0 || _stricmp(tag, "EyeIdle2") == 0) return 15;
        if (_stricmp(tag, "Hat") == 0) return 25;
        return 50;
    }

    /* Temple Cleared & Stage Complete */
    if (_stricmp(sec, "TempleCleared") == 0 || _stricmp(sec, "StageComplete") == 0) {
        if (_stricmp(tag, "TikiHead") == 0) return 10;
        if (_stricmp(tag, "FrogMedallion") == 0 || _stricmp(tag, "FrogTarget") == 0) return 15;
        if (_stricmp(tag, "ContinueBtn") == 0) return 40;
        return 30;
    }

    /* Help Screens */
    if (_stricmp(sec, "HelpScreen") == 0 || _stricmp(sec, "DevHelpScreen") == 0) {
        if (_stricmp(tag, "HelpBack") == 0) return 5;
        if (_stricmp(tag, "Temple5Preview") == 0 || _stricmp(tag, "DevTemple5") == 0) return 20;
        return 40;
    }

    /* Dialogs */
    if (_stricmp(sec, "Dialogs") == 0) {
        if (_stricmp(tag, "DialogHeader") == 0) return 20;
        return 10;
    }

    return 50;
}

typedef struct {
    LayoutElement *el;
    int z_order;
    int original_index;
} RenderEntry;

static int compare_render_entries(const void *a, const void *b) {
    const RenderEntry *ea = (const RenderEntry *)a;
    const RenderEntry *eb = (const RenderEntry *)b;
    if (ea->z_order != eb->z_order) {
        return ea->z_order - eb->z_order;
    }
    return ea->original_index - eb->original_index;
}

/* Update visual bounding boxes on canvas for all active elements */
static void update_all_elements_canvas_rects(LayoutEditorState *st, int cv_w, int cv_h) {
    float vscale, zscale;
    int tx, ty, tw, th, xo, yo, cw, ch;
    calculate_viewport_transforms(st, cv_w, cv_h, &vscale, &tx, &ty, &tw, &th, &zscale, &xo, &yo, &cw, &ch);

    for (int i = 0; i < st->element_count; ++i) {
        LayoutElement *el = &st->elements[i];
        if (!is_element_visible_in_editor(el, st)) {
            SetRectEmpty(&el->canvas_rect);
            SetRectEmpty(&el->frog_canvas_rect);
            continue;
        }

        /* Determine sprite dimensions if available */
        int cols = 1, rows = 1, def_cell = 0;
        const char *sp_name = get_element_sprite_name(el, &cols, &rows, &def_cell);
        LayoutSprite *sp = sp_name ? layout_get_cached_sprite(sp_name, cols) : NULL;
        if (sp && sp->loaded && sp->img.width > 0) {
            el->def_w = sp->img.width / cols;
            el->def_h = sp->img.height / rows;
        } else if (el->has_explicit_size) {
            el->def_w = el->w;
            el->def_h = el->h;
        }

        int elem_w = el->def_w;
        int elem_h = el->def_h;

        /* MenubarMargins safety zone guide */
        if (_stricmp(el->tag, "MenubarMargins") == 0) {
            int ml = (int)roundf((float)el->left * zscale);
            int mt = (int)roundf((float)el->top * zscale);
            int mr = (int)roundf((float)el->right * zscale);
            int mb = (int)roundf((float)el->bottom * zscale);
            int sx = ml;
            int sy = mt;
            int sw = st->target_w - ml - mr;
            int sh = st->target_h - mt - mb;
            int cx = tx + (int)roundf((float)sx * vscale);
            int cy = ty + (int)roundf((float)sy * vscale);
            int rw = (int)roundf((float)sw * vscale);
            int rh = (int)roundf((float)sh * vscale);
            SetRect(&el->canvas_rect, cx, cy, cx + rw, cy + rh);
            SetRectEmpty(&el->frog_canvas_rect);
            continue;
        }

        /* LoadingBar visual boundary clamp */
        if (_stricmp(el->tag, "LoadingBar") == 0) {
            int bw = (st && st->loading_space_w > 640) ? (int)roundf(384.0f * ((float)st->loading_space_w / 640.0f)) : 384;
            int bh = (st && st->loading_space_w > 640) ? (int)roundf(44.0f * ((float)st->loading_space_w / 640.0f)) : 44;
            elem_w = bw;
            elem_h = bh;
            el->def_w = bw;
            el->def_h = bh;
        }

        /* DoorName & PracticeText full door frame width bounds */
        if (_stricmp(el->tag, "DoorName") == 0 || _stricmp(el->tag, "PracticeText") == 0) {
            if (el->def_w <= 0) {
                elem_w = 450;
                elem_h = 28;
                el->def_w = 450;
                el->def_h = 28;
            }
        }

        /* LevelBanner bottom-right clamp */
        if (_stricmp(el->tag, "LevelBanner") == 0 && (el->flags & ATTR_HAS_OFFSET_X)) {
            elem_w = 140;
            elem_h = 24;
            el->def_w = 140;
            el->def_h = 24;
        }

        int scr = st->cur_screen_idx;

        bool has_sprite = (sp && sp->loaded && sp->img.width > 0);
        int target_ew, target_eh;
        if (has_sprite) {
            target_ew = sp->img.width / cols;
            target_eh = sp->img.height / rows;
        } else if (is_element_space_1to1(el, st) || scr == 4 || scr == 5 || scr == 6) {
            target_ew = el->def_w;
            target_eh = el->def_h;
        } else {
            target_ew = (int)roundf((float)el->def_w * zscale);
            target_eh = (int)roundf((float)el->def_h * zscale);
        }

        int canvas_ew = (int)roundf((float)target_ew * vscale);
        int canvas_eh = (int)roundf((float)target_eh * vscale);
        if (canvas_ew < 12) canvas_ew = 12;
        if (canvas_eh < 12) canvas_eh = 12;

        if (el->flags & ATTR_IS_DOOR_PAIR) {
            int target_dx, target_dy, target_fx, target_fy;
            if (is_element_space_1to1(el, st) || scr == 4 || scr == 5 || scr == 6 || (scr == 2 && st && st->adventure_space_w == st->target_w && st->adventure_space_h == st->target_h) || (scr == 3 && st && st->gauntlet_space_w == st->target_w && st->gauntlet_space_h == st->target_h)) {
                target_dx = el->doorX; target_dy = el->doorY;
                target_fx = el->frogX; target_fy = el->frogY;
            } else {
                /* Mode A (Screen 2: Adventure, Screen 3: Gauntlet) */
                target_dx = xo + (int)roundf((float)el->doorX * zscale);
                target_dy = yo + (int)roundf((float)el->doorY * zscale);
                target_fx = xo + (int)roundf((float)el->frogX * zscale);
                target_fy = yo + (int)roundf((float)el->frogY * zscale);
            }

            int dcx = tx + (int)roundf((float)target_dx * vscale);
            int dcy = ty + (int)roundf((float)target_dy * vscale);
            SetRect(&el->canvas_rect, dcx, dcy, dcx + canvas_ew, dcy + canvas_eh);

            int fcx = tx + (int)roundf((float)target_fx * vscale);
            int fcy = ty + (int)roundf((float)target_fy * vscale);
            int f_size = (int)roundf(24.0f * vscale);
            if (f_size < 16) f_size = 16;
            SetRect(&el->frog_canvas_rect, fcx - f_size / 2, fcy - f_size / 2, fcx + f_size / 2, fcy + f_size / 2);
        } else {
            int in_x = (el->flags & ATTR_HAS_CENTER_X) ? el->centerX : el->x;
            int in_y = (el->flags & ATTR_HAS_CENTER_Y) ? el->centerY : el->y;

            int target_x = 0, target_y = 0;

            if (scr == 6) {
                /* Mode C: TempleCleared */
                int delta_x = (st->target_w - 640) / 2;
                int delta_y = (st->target_h - 480) / 2;
                if (_stricmp(el->tag, "Title") == 0) in_y -= 80;
                target_x = delta_x + in_x;
                target_y = delta_y + in_y;
            } else if (scr == 5) {
                /* Mode C: StageComplete */
                int delta_y = (st->target_h - 480) / 2;
                if (_stricmp(el->tag, "StageText") == 0 || _stricmp(el->tag, "CompleteText") == 0) in_y -= 80;
                target_x = (in_x == 320 || in_x == st->target_w / 2) ? (st->target_w / 2) : (int)roundf((float)in_x * zscale);
                target_y = delta_y + in_y;
            } else if (scr == 4) {
                /* Mode B: InGameHUD */
                bool hud_space_ok = (st->hud_space_w == st->target_w && st->hud_space_h == st->target_h);
                if (_stricmp(el->tag, "LevelBanner") == 0 && (el->flags & ATTR_HAS_OFFSET_X)) {
                    int off_x = (el->flags & ATTR_HAS_OFFSET_X) ? el->offsetX : 40;
                    int off_y = (el->flags & ATTR_HAS_OFFSET_Y) ? el->offsetY : 40;
                    target_x = st->target_w - off_x - target_ew;
                    target_y = st->target_h - off_y - target_eh;
                } else if (_stricmp(el->tag, "ScoreText") == 0) {
                    target_x = hud_space_ok ? (in_x - target_ew) : ((int)roundf((float)in_x * zscale) - target_ew);
                    target_y = 4;
                } else if (_stricmp(el->tag, "StageName") == 0) {
                    target_x = hud_space_ok ? in_x : (int)roundf((float)in_x * zscale);
                    target_y = 5;
                } else if (hud_space_ok) {
                    target_x = in_x;
                    target_y = in_y;
                } else {
                    target_x = (int)roundf((float)in_x * zscale);
                    target_y = (int)roundf((float)in_y * zscale);
                }
            } else {
                /* Mode A: Menu 4:3 with outscreen pillars */
                if (scr == 8) {
                    /* HelpScreen child widget offsets */
                    if (_stricmp(el->tag, "TopText1") == 0 || _stricmp(el->tag, "BallTarget") == 0) {
                        in_x += 11; in_y += 25;
                    } else if (_stricmp(el->tag, "TopText2") == 0) {
                        in_x += 325; in_y += 25;
                    } else if (_stricmp(el->tag, "Skull") == 0 || _stricmp(el->tag, "Coin") == 0 ||
                               _stricmp(el->tag, "Frog") == 0 || _stricmp(el->tag, "SwapClick") == 0 ||
                               _stricmp(el->tag, "SwapMouse") == 0) {
                        in_x += 11; in_y += 255;
                    }
                }
                if (scr == 2 && el->section && _stricmp(el->section, "AdventureLabels") == 0) {
                    if (_stricmp(el->tag, "HighscoreNames") == 0 || _stricmp(el->tag, "HighscoreScores") == 0) {
                        in_y -= 8;
                    } else if (_stricmp(el->tag, "StageText") == 0 || _stricmp(el->tag, "Stage13Title") == 0) {
                        in_y -= 26;
                    } else if (_stricmp(el->tag, "StageNum") == 0 || _stricmp(el->tag, "Stage13Num") == 0) {
                        in_y -= 10;
                    } else if (_stricmp(el->tag, "Subtitle") == 0 || _stricmp(el->tag, "Stage13Subtitle") == 0) {
                        in_y -= 10;
                    }
                }
                if (scr == 3 && el->section && _stricmp(el->section, "GauntletLabels") == 0) {
                    if (_stricmp(el->tag, "PracticeText") == 0 || _stricmp(el->tag, "CustomMapTag") == 0) {
                        in_y -= 10;
                    }
                }
                bool is_space_1to1 = is_element_space_1to1(el, st);
                if (scr == 1 && st && st->main_menu_space_w == st->target_w && st->main_menu_space_h == st->target_h) is_space_1to1 = true;
                if (scr == 0 && st && st->loading_space_w == st->target_w && st->loading_space_h == st->target_h) is_space_1to1 = true;
                if (scr == 2 && st && st->adventure_space_w == st->target_w && st->adventure_space_h == st->target_h) is_space_1to1 = true;
                if (scr == 3 && st && st->gauntlet_space_w == st->target_w && st->gauntlet_space_h == st->target_h) is_space_1to1 = true;

                if (is_space_1to1) {
                    target_x = in_x;
                    target_y = in_y;
                } else if (_stricmp(el->tag, "HighscoreScores") == 0) {
                    target_x = xo + (int)roundf((float)(in_x - el->def_w) * zscale);
                    target_y = yo + (int)roundf((float)in_y * zscale);
                } else {
                    target_x = xo + (int)roundf((float)in_x * zscale);
                    target_y = yo + (int)roundf((float)in_y * zscale);
                }
            }

            /* Hotspot offsets */
            if (el->hotspot & HOTSPOT_BOTTOM_CENTER) {
                target_x -= target_ew / 2;
                target_y -= target_eh;
            } else {
                if (el->hotspot & HOTSPOT_CENTER_X) {
                    target_x -= target_ew / 2;
                }
                if (el->hotspot & HOTSPOT_CENTER_Y) {
                    target_y -= target_eh / 2;
                }
            }

            int cx = tx + (int)roundf((float)target_x * vscale);
            int cy = ty + (int)roundf((float)target_y * vscale);
            SetRect(&el->canvas_rect, cx, cy, cx + canvas_ew, cy + canvas_eh);
            SetRectEmpty(&el->frog_canvas_rect);
        }
    }
}

/* Hit testing helper based on descending visual Z-order */
static LayoutElement* layout_editor_hit_test(LayoutEditorState *st, int mx, int my, int *out_handle) {
    if (out_handle) *out_handle = 0;
    POINT pt = { mx, my };

    RenderEntry entries[MAX_LAYOUT_ELEMENTS];
    int count = 0;
    for (int i = 0; i < st->element_count; ++i) {
        LayoutElement *el = &st->elements[i];
        if (!is_element_visible_in_editor(el, st)) continue;
        entries[count].el = el;
        entries[count].z_order = get_element_z_order(el, st->cur_screen_idx);
        entries[count].original_index = i;
        count++;
    }
    qsort(entries, count, sizeof(RenderEntry), compare_render_entries);

    /* Test top-to-bottom (highest Z-order first) */
    for (int i = count - 1; i >= 0; --i) {
        LayoutElement *el = entries[i].el;
        if (el->flags & ATTR_IS_DOOR_PAIR) {
            if (PtInRect(&el->frog_canvas_rect, pt)) {
                if (out_handle) *out_handle = 1;
                return el;
            }
            if (PtInRect(&el->canvas_rect, pt)) {
                if (out_handle) *out_handle = 0;
                return el;
            }
        } else {
            if (PtInRect(&el->canvas_rect, pt)) {
                if (out_handle) *out_handle = 0;
                return el;
            }
        }
    }
    return NULL;
}

/* Render Door Arch and Frog Waypoint */
static void render_layout_door_pair(
    HDC mem_dc, LayoutElement *el, LayoutSprite *sp,
    int cols, int rows, int cell,
    bool is_selected, bool is_hovered, bool is_held, int held_handle,
    HFONT font_bold, HFONT font_ui, bool is_dark
) {
    /* 1. Connecting path line between Door and Frog (only when active) */
    if (is_selected || is_hovered || is_held) {
        int door_center_x = (el->canvas_rect.left + el->canvas_rect.right) / 2;
        int door_center_y = (el->canvas_rect.top + el->canvas_rect.bottom) / 2;
        int frog_center_x = (el->frog_canvas_rect.left + el->frog_canvas_rect.right) / 2;
        int frog_center_y = (el->frog_canvas_rect.top + el->frog_canvas_rect.bottom) / 2;

        HPEN path_pen = CreatePen(PS_DOT, 1, is_selected ? RGB(250, 204, 21) : (is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139)));
        HPEN old_ppen = (HPEN)SelectObject(mem_dc, path_pen);
        MoveToEx(mem_dc, door_center_x, door_center_y, NULL);
        LineTo(mem_dc, frog_center_x, frog_center_y);
        SelectObject(mem_dc, old_ppen);
        DeleteObject(path_pen);
    }

    /* 2. Render Door Arch Sprite */
    int dst_x = el->canvas_rect.left;
    int dst_y = el->canvas_rect.top;
    int dst_w = el->canvas_rect.right - el->canvas_rect.left;
    int dst_h = el->canvas_rect.bottom - el->canvas_rect.top;

    if (sp && sp->loaded && sp->img.hbm) {
        int frame_w = sp->img.width / cols;
        int frame_h = sp->img.height / rows;
        int frame_idx = cell;
        if (cols >= 3) {
            if (is_held && held_handle == 0) frame_idx = (cols >= 4) ? 3 : 2;
            else if (is_hovered || is_selected) frame_idx = 2;
            else frame_idx = cell;
        }
        int src_x = (frame_idx % cols) * frame_w;
        int src_y = (frame_idx / cols) * frame_h;

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        BOOL ok = AlphaBlend(mem_dc, dst_x, dst_y, dst_w, dst_h,
                             sp->img.hdc_mem, src_x, src_y, frame_w, frame_h, bf);
        if (!ok) {
            DWORD err = GetLastError();
            char err_msg[64];
            snprintf(err_msg, sizeof(err_msg), "BlendErr:%lu", err);
            TextOutA(mem_dc, dst_x, dst_y, err_msg, (int)strlen(err_msg));
        }
    } else {
        /* Subtle outline if door sprite not found */
        HPEN door_pen = CreatePen(PS_DASH, 1, is_selected ? RGB(56, 189, 248) : RGB(100, 116, 139));
        HPEN old_pen = (HPEN)SelectObject(mem_dc, door_pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        Rectangle(mem_dc, dst_x, dst_y, dst_x + dst_w, dst_y + dst_h);
        SelectObject(mem_dc, old_pen);
        DeleteObject(door_pen);
    }

    /* Highlight Door Arch when active */
    if (is_selected || is_hovered || (is_held && held_handle == 0)) {
        COLORREF border_color = (is_held && held_handle == 0) ? RGB(245, 158, 11) :
                                (is_selected ? RGB(56, 189, 248) : RGB(125, 211, 252));
        HPEN border_pen = CreatePen(PS_SOLID, is_selected ? 2 : 1, border_color);
        HPEN old_pen = (HPEN)SelectObject(mem_dc, border_pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        Rectangle(mem_dc, dst_x, dst_y, dst_x + dst_w, dst_y + dst_h);
        SelectObject(mem_dc, old_pen);
        DeleteObject(border_pen);

        /* Door label tag badge with drop shadow */
        char door_lbl[32];
        snprintf(door_lbl, sizeof(door_lbl), "Door %s", el->id);
        SetBkMode(mem_dc, TRANSPARENT);
        SelectObject(mem_dc, font_bold);
        RECT rc_lbl = { dst_x, dst_y - 18, dst_x + dst_w, dst_y };
        RECT rc_sh = rc_lbl;
        OffsetRect(&rc_sh, 1, 1);
        SetTextColor(mem_dc, RGB(0, 0, 0));
        DrawTextA(mem_dc, door_lbl, -1, &rc_sh, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(mem_dc, RGB(255, 234, 1));
        DrawTextA(mem_dc, door_lbl, -1, &rc_lbl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 3. Render Frog Waypoint Handle */
    int f_left = el->frog_canvas_rect.left;
    int f_top = el->frog_canvas_rect.top;
    int f_right = el->frog_canvas_rect.right;
    int f_bottom = el->frog_canvas_rect.bottom;

    bool is_frog_active = is_selected || is_hovered || (is_held && held_handle == 1);

    if (is_frog_active) {
        /* Active Frog Waypoint: Glowing circle with label */
        COLORREF fill_col = (is_held && held_handle == 1) ? RGB(245, 158, 11) :
                            (is_selected ? RGB(16, 185, 129) : RGB(45, 212, 191));
        HBRUSH br = CreateSolidBrush(fill_col);
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HBRUSH old_br = (HBRUSH)SelectObject(mem_dc, br);
        HPEN old_pen = (HPEN)SelectObject(mem_dc, pen);
        Ellipse(mem_dc, f_left, f_top, f_right, f_bottom);
        SelectObject(mem_dc, old_br);
        SelectObject(mem_dc, old_pen);
        DeleteObject(br);
        DeleteObject(pen);

        char f_lbl[16];
        snprintf(f_lbl, sizeof(f_lbl), "F%s", el->id);
        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, RGB(0, 0, 0));
        SelectObject(mem_dc, font_bold);
        RECT rc_ftxt = el->frog_canvas_rect;
        DrawTextA(mem_dc, f_lbl, -1, &rc_ftxt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

/* Render PopCap Sprite Element */
static void render_layout_sprite_element(
    HDC mem_dc, LayoutElement *el, LayoutSprite *sp,
    int cols, int rows, int cell,
    bool is_selected, bool is_hovered, bool is_held,
    HFONT font_bold
) {
    if (sp) {
        if (cols <= 1 && sp->cols > 1) cols = sp->cols;
        if (rows <= 1 && sp->rows > 1) rows = sp->rows;
    }
    int frame_w = sp->img.width / cols;
    int frame_h = sp->img.height / rows;
    int frame_idx = cell;
    if (frame_idx == 0 && sp && sp->default_cell > 0) frame_idx = sp->default_cell;
    if ((cols == 3 || cols == 4) && cell == 0) {
        if (is_held) frame_idx = 2;
        else if (is_hovered) frame_idx = 1;
        else frame_idx = 0;
    }
    int src_x = (frame_idx % cols) * frame_w;
    int src_y = (frame_idx / cols) * frame_h;

    int dst_x = el->canvas_rect.left;
    int dst_y = el->canvas_rect.top;
    int dst_w = el->canvas_rect.right - el->canvas_rect.left;
    int dst_h = el->canvas_rect.bottom - el->canvas_rect.top;

    /* Suppress duplicate blits for elements already composited in Step 2 */
    if (el->tag && (_stricmp(el->tag, "Menubar") == 0 ||
                    _stricmp(el->tag, "LivesBanner") == 0 ||
                    _stricmp(el->tag, "LivesPos") == 0 ||
                    _stricmp(el->tag, "StageName") == 0 ||
                    _stricmp(el->tag, "ScoreText") == 0 ||
                    _stricmp(el->tag, "HelpBack") == 0 ||
                    _stricmp(el->tag, "DontShowCheckbox") == 0 ||
                    _stricmp(el->tag, "WhateverBtn") == 0 ||
                    _stricmp(el->tag, "FrogWriting") == 0 ||
                    (el->section && (_stricmp(el->section, "HelpScreen") == 0 || _stricmp(el->section, "DevHelpScreen") == 0) &&
                     (_stricmp(el->tag, "HelpFront") == 0 || _stricmp(el->tag, "OkButton") == 0 || _stricmp(el->tag, "Temple5Preview") == 0 ||
                      _stricmp(el->tag, "MouseIcon") == 0 || _stricmp(el->tag, "DemoBalls") == 0 ||
                      _stricmp(el->tag, "DemoMouse") == 0 || _stricmp(el->tag, "DevMouseM") == 0 ||
                      _stricmp(el->tag, "DialogHeader") == 0 ||
                      _stricmp(el->tag, "Frog") == 0 || _stricmp(el->tag, "Coin") == 0 || _stricmp(el->tag, "Skull") == 0 || _stricmp(el->tag, "SwapMouse") == 0)))) {
        if (is_selected || is_hovered || is_held) {
            COLORREF border_color = is_held ? RGB(245, 158, 11) :
                                    (is_selected ? RGB(56, 189, 248) : RGB(125, 211, 252));
            HPEN pen = CreatePen(PS_SOLID, is_selected ? 2 : 1, border_color);
            HPEN old_pen = (HPEN)SelectObject(mem_dc, pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, dst_x, dst_y, dst_x + dst_w, dst_y + dst_h);
            SelectObject(mem_dc, old_pen);
            DeleteObject(pen);
        }
        return;
    }

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    // Safe clipping for negative dst_x / dst_y coordinates (e.g. Sun glow at screen borders)
    int blit_dx = dst_x, blit_dy = dst_y, blit_dw = dst_w, blit_dh = dst_h;
    int blit_sx = src_x, blit_sy = src_y, blit_sw = frame_w, blit_sh = frame_h;
    float sc_x = (float)frame_w / (float)(dst_w > 0 ? dst_w : 1);
    float sc_y = (float)frame_h / (float)(dst_h > 0 ? dst_h : 1);
    if (blit_dx < 0) {
        int clip = -blit_dx;
        int s_clip = (int)roundf((float)clip * sc_x);
        blit_sx += s_clip; blit_sw -= s_clip;
        blit_dw -= clip; blit_dx = 0;
    }
    if (blit_dy < 0) {
        int clip = -blit_dy;
        int s_clip = (int)roundf((float)clip * sc_y);
        blit_sy += s_clip; blit_sh -= s_clip;
        blit_dh -= clip; blit_dy = 0;
    }
    if (blit_dw > 0 && blit_dh > 0 && blit_sw > 0 && blit_sh > 0) {
        AlphaBlend(mem_dc, blit_dx, blit_dy, blit_dw, blit_dh,
                   sp->img.hdc_mem, blit_sx, blit_sy, blit_sw, blit_sh, bf);
    }

    if (is_selected || is_hovered || is_held) {
        COLORREF border_color = is_held ? RGB(245, 158, 11) :
                                (is_selected ? RGB(56, 189, 248) : RGB(125, 211, 252));
        HPEN pen = CreatePen(PS_SOLID, is_selected ? 2 : 1, border_color);
        HPEN old_pen = (HPEN)SelectObject(mem_dc, pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        Rectangle(mem_dc, dst_x, dst_y, dst_x + dst_w, dst_y + dst_h);
        SelectObject(mem_dc, old_pen);
        DeleteObject(pen);
    }
}

/* =========================================================================
   PopCap ImageFont System (Authentic Bitmap Font Parser & Renderer)
   ========================================================================= */
typedef struct {
    char ch;
    int width;
    int src_x, src_y, src_w, src_h;
    int off_x, off_y;
    int out_x, out_y, out_w, out_h;
    int out_off_x, out_off_y;
} PopCapCharDef;

typedef struct {
    char name[64];
    bool loaded;
    bool has_outline;            /* True if Outline layer is present and loaded */
    LayoutSprite *sprite;        /* Colored sprite (e.g. NativeAlienExtended16, CancunFloat14) */
    CanvasImage mask_img;        /* Grayscale mask image (e.g. _Cancun10, _Arial12Bold) */
    CanvasImage outline_img;     /* Outline mask image (e.g. _CancunOUTLINE10) */
    CanvasImage outline_dib;     /* Black outline ARGB DIB */
    CanvasImage tinted_dib[32];   /* Pre-tinted ARGB DIBs for quick blitting */
    COLORREF tinted_color[32];
    int tinted_count;
    int ascent;                  /* Main layer ascent (e.g. 10 for Cancun10) */
    int ascent_padding;          /* Main layer ascent padding (e.g. 0 for Cancun10) */
    int outline_ascent;          /* Outline layer ascent (e.g. 20 for Cancun10 Outline) */
    int outline_ascent_padding;  /* Outline layer ascent padding (e.g. 10 for Cancun10 Outline) */
    int line_spacing_offset;     /* Main layer line spacing offset (e.g. 4 for Cancun10) */
    int space_width;
    int point_size;
    int char_count;
    PopCapCharDef chars[256];
    char map_from[32];
    char map_to[32];
    int map_count;
} PopCapFont;

static PopCapFont g_popcap_native_alien_48 = {0};
static PopCapFont g_popcap_native_alien_18 = {0};
static PopCapFont g_popcap_native_alien_16 = {0};
static PopCapFont g_popcap_cancun_14 = {0};
static PopCapFont g_popcap_cancun_12 = {0};
static PopCapFont g_popcap_cancun_13 = {0};
static PopCapFont g_popcap_cancun_10 = {0};
static PopCapFont g_popcap_cancun_outline_10 = {0};
static PopCapFont g_popcap_cancun_outline_8 = {0};
static PopCapFont g_popcap_arial_12 = {0};

/* Universal Font Baseline Offset (Ascent - AscentPadding) Helper.
   Computes exact pixel offset between DrawString baseline Y and visual glyph top. */
static inline int le_font_baseline_offset(const PopCapFont *font) {
    if (!font) return 0;
    int off = 0;
    if (font->has_outline && font->outline_ascent > 0) {
        off = font->outline_ascent - font->outline_ascent_padding;
    } else if (font->ascent > 0) {
        off = font->ascent - font->ascent_padding;
    }
    if (off > 0) return off;

    /* Fallback constants matching verified font descriptors */
    if (font == &g_popcap_native_alien_48) return 80;
    if (font == &g_popcap_native_alien_18) return 30;
    if (font == &g_popcap_native_alien_16) return 26;
    if (font == &g_popcap_cancun_outline_8) return 8;
    if (font == &g_popcap_cancun_outline_10) return 10;
    if (font == &g_popcap_cancun_10) return 10;
    if (font == &g_popcap_cancun_12) return 12;
    if (font == &g_popcap_cancun_13) return 13;
    if (font == &g_popcap_cancun_14) return 15;
    if (font == &g_popcap_arial_12) return 14;
    return 0;
}

static char* font_skip_ws(char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static bool create_argb_dib(int width, int height, CanvasImage *out_img) {
    if (width <= 0 || height <= 0 || !out_img) return false;
    memset(out_img, 0, sizeof(CanvasImage));
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; /* Top-down DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screen_dc = GetDC(NULL);
    out_img->hdc_mem = CreateCompatibleDC(screen_dc);
    out_img->hbm = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &out_img->bits, NULL, 0);
    ReleaseDC(NULL, screen_dc);

    if (!out_img->hbm || !out_img->bits) {
        if (out_img->hdc_mem) DeleteDC(out_img->hdc_mem);
        out_img->hdc_mem = NULL;
        return false;
    }
    SelectObject(out_img->hdc_mem, out_img->hbm);
    out_img->width = width;
    out_img->height = height;
    return true;
}

static bool parse_popcap_font_txt(const char *filepath, PopCapFont *font) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    memset(font, 0, sizeof(PopCapFont));
    font->space_width = 8;
    font->ascent = 12;

    /* 1. CharList */
    char *p = strstr(buf, "CharList");
    if (p) {
        p = strchr(p, '(');
        if (p) {
            p++;
            while (*p && *p != ')') {
                p = font_skip_ws(p);
                if (*p == '\'') {
                    p++;
                    char c = *p++;
                    if (c == '\\' && *p) c = *p++;
                    if (*p == '\'') p++;
                    if (font->char_count < 256) {
                        font->chars[font->char_count].ch = c;
                        font->char_count++;
                    }
                } else if (*p == '"') {
                    p++;
                    char c = *p++;
                    if (c == '\\' && *p) c = *p++;
                    if (*p == '"') p++;
                    if (font->char_count < 256) {
                        font->chars[font->char_count].ch = c;
                        font->char_count++;
                    }
                } else {
                    p++;
                }
            }
        }
    }

    /* 2. WidthList */
    p = strstr(buf, "WidthList");
    if (p) {
        p = strchr(p, '(');
        if (p) {
            p++;
            int idx = 0;
            while (*p && *p != ')' && idx < font->char_count) {
                p = font_skip_ws(p);
                if ((*p >= '0' && *p <= '9') || *p == '-') {
                    char *endp;
                    font->chars[idx++].width = (int)strtol(p, &endp, 10);
                    p = endp;
                } else {
                    p++;
                }
            }
        }
    }

    /* 3. RectList */
    p = strstr(buf, "RectList");
    if (p) {
        p = strchr(p, '(');
        if (p) {
            p++;
            int idx = 0;
            while (*p && *p != ';' && idx < font->char_count) {
                p = font_skip_ws(p);
                if (*p == '(') {
                    p++;
                    int vals[4] = {0};
                    for (int v = 0; v < 4; ++v) {
                        p = font_skip_ws(p);
                        char *endp;
                        vals[v] = (int)strtol(p, &endp, 10);
                        p = endp;
                        p = font_skip_ws(p);
                        if (*p == ',') p++;
                    }
                    font->chars[idx].src_x = vals[0];
                    font->chars[idx].src_y = vals[1];
                    font->chars[idx].src_w = vals[2];
                    font->chars[idx].src_h = vals[3];
                    idx++;
                    p = strchr(p, ')');
                    if (p) p++;
                } else {
                    p++;
                }
            }
        }
    }

    /* 4. OffsetList */
    p = strstr(buf, "OffsetList");
    if (p) {
        p = strchr(p, '(');
        if (p) {
            p++;
            int idx = 0;
            while (*p && *p != ';' && idx < font->char_count) {
                p = font_skip_ws(p);
                if (*p == '(') {
                    p++;
                    int vals[2] = {0};
                    for (int v = 0; v < 2; ++v) {
                        p = font_skip_ws(p);
                        char *endp;
                        vals[v] = (int)strtol(p, &endp, 10);
                        p = endp;
                        p = font_skip_ws(p);
                        if (*p == ',') p++;
                    }
                    font->chars[idx].off_x = vals[0];
                    font->chars[idx].off_y = vals[1];
                    idx++;
                    p = strchr(p, ')');
                    if (p) p++;
                } else {
                    p++;
                }
            }
        }
    }

    /* 5. OutlineRectList (optional) */
    p = strstr(buf, "OutlineRectList");
    if (p) {
        p = strchr(p, '(');
        if (p) {
            p++;
            int idx = 0;
            while (*p && *p != ';' && idx < font->char_count) {
                p = font_skip_ws(p);
                if (*p == '(') {
                    p++;
                    int vals[4] = {0};
                    for (int v = 0; v < 4; ++v) {
                        p = font_skip_ws(p);
                        char *endp;
                        vals[v] = (int)strtol(p, &endp, 10);
                        p = endp;
                        p = font_skip_ws(p);
                        if (*p == ',') p++;
                    }
                    font->chars[idx].out_x = vals[0];
                    font->chars[idx].out_y = vals[1];
                    font->chars[idx].out_w = vals[2];
                    font->chars[idx].out_h = vals[3];
                    idx++;
                    p = strchr(p, ')');
                    if (p) p++;
                } else {
                    p++;
                }
            }
        }
    }

    /* 6. OutlineOffsetList (optional) */
    p = strstr(buf, "OutlineOffsetList");
    if (p) {
        p = strchr(p, '(');
        if (p) {
            p++;
            int idx = 0;
            while (*p && *p != ';' && idx < font->char_count) {
                p = font_skip_ws(p);
                if (*p == '(') {
                    p++;
                    int vals[2] = {0};
                    for (int v = 0; v < 2; ++v) {
                        p = font_skip_ws(p);
                        char *endp;
                        vals[v] = (int)strtol(p, &endp, 10);
                        p = endp;
                        p = font_skip_ws(p);
                        if (*p == ',') p++;
                    }
                    font->chars[idx].out_off_x = vals[0];
                    font->chars[idx].out_off_y = vals[1];
                    idx++;
                    p = strchr(p, ')');
                    if (p) p++;
                } else {
                    p++;
                }
            }
        }
    }

    /* 7. LayerSetAscent / LayerSetAscentPadding / LayerSetLineSpacingOffset */
    font->ascent = 10;
    font->ascent_padding = 0;
    font->outline_ascent = 20;
    font->outline_ascent_padding = 10;
    font->line_spacing_offset = 4;

    p = strstr(buf, "LayerSetAscent ");
    while (p) {
        char *line_end = strchr(p, ';');
        char layer_name[64] = {0};
        int asc_val = 0;
        if (sscanf(p, "LayerSetAscent %63s %d", layer_name, &asc_val) == 2) {
            if (_stricmp(layer_name, "Main") == 0) font->ascent = asc_val;
            else if (_stricmp(layer_name, "Outline") == 0) font->outline_ascent = asc_val;
        }
        p = line_end ? strstr(line_end, "LayerSetAscent ") : NULL;
    }

    p = strstr(buf, "LayerSetAscentPadding");
    while (p) {
        char *line_end = strchr(p, ';');
        char layer_name[64] = {0};
        int pad_val = 0;
        if (sscanf(p, "LayerSetAscentPadding %63s %d", layer_name, &pad_val) == 2) {
            if (_stricmp(layer_name, "Main") == 0) font->ascent_padding = pad_val;
            else if (_stricmp(layer_name, "Outline") == 0) font->outline_ascent_padding = pad_val;
        }
        p = line_end ? strstr(line_end, "LayerSetAscentPadding") : NULL;
    }

    p = strstr(buf, "LayerSetLineSpacingOffset");
    while (p) {
        char *line_end = strchr(p, ';');
        char layer_name[64] = {0};
        int lso_val = 0;
        if (sscanf(p, "LayerSetLineSpacingOffset %63s %d", layer_name, &lso_val) == 2) {
            if (_stricmp(layer_name, "Main") == 0) font->line_spacing_offset = lso_val;
        }
        p = line_end ? strstr(line_end, "LayerSetLineSpacingOffset") : NULL;
    }

    /* 8. Space width: LayerSetCharWidths Main (' ') (12) */
    p = strstr(buf, "LayerSetCharWidths");
    while (p) {
        p += 18;
        p = font_skip_ws(p);
        if (strncmp(p, "Main", 4) == 0) p += 4;
        p = font_skip_ws(p);
        if (*p == '(' && (*(p+1) == ' ' || *(p+1) == '\'')) {
            char *sp_val = strchr(p, ')');
            if (sp_val) {
                char *sp_op = strchr(sp_val, '(');
                if (sp_op) {
                    font->space_width = (int)strtol(sp_op + 1, NULL, 10);
                    break;
                }
            }
        }
        p = strstr(p, "LayerSetCharWidths");
    }

    /* 9. MapFrom / MapTo */
    p = strstr(buf, "Define MapFrom");
    if (p) {
        char *from_op = strchr(p, '(');
        char *to_p = strstr(buf, "Define MapTo");
        char *to_op = to_p ? strchr(to_p, '(') : NULL;
        if (from_op && to_op) {
            from_op++; to_op++;
            while (*from_op && *from_op != ')' && *to_op && *to_op != ')' && font->map_count < 32) {
                from_op = font_skip_ws(from_op);
                to_op = font_skip_ws(to_op);
                if (*from_op == '\'') from_op++;
                char cf = *from_op++;
                if (*from_op == '\'') from_op++;
                if (*from_op == ',') from_op++;

                if (*to_op == '\'') to_op++;
                char ct = *to_op++;
                if (*to_op == '\'') to_op++;
                if (*to_op == ',') to_op++;

                font->map_from[font->map_count] = cf;
                font->map_to[font->map_count] = ct;
                font->map_count++;
            }
        }
    }

    free(buf);
    font->loaded = (font->char_count > 0);
    return font->loaded;
}

static HDC popcap_font_get_tinted_dc(PopCapFont *font, COLORREF cr) {
    if (!font || !font->mask_img.bits) return NULL;
    for (int i = 0; i < font->tinted_count; ++i) {
        if (font->tinted_color[i] == cr) return font->tinted_dib[i].hdc_mem;
    }

    int idx;
    if (font->tinted_count < 32) {
        idx = font->tinted_count;
        if (!create_argb_dib(font->mask_img.width, font->mask_img.height, &font->tinted_dib[idx])) {
            return NULL;
        }
        font->tinted_count++;
    } else {
        idx = 31;
    }
    font->tinted_color[idx] = cr;

    uint32_t *dst = (uint32_t*)font->tinted_dib[idx].bits;
    uint32_t *src = (uint32_t*)font->mask_img.bits;
    uint8_t r = GetRValue(cr);
    uint8_t g = GetGValue(cr);
    uint8_t b = GetBValue(cr);
    int total = font->mask_img.width * font->mask_img.height;

    if (font->sprite && font->sprite->loaded && font->sprite->img.bits &&
        font->sprite->img.width == font->mask_img.width && font->sprite->img.height == font->mask_img.height) {
        uint8_t *spr_px = (uint8_t*)font->sprite->img.bits;
        for (int i = 0; i < total; ++i) {
            uint8_t a = (uint8_t)(src[i] & 0xFF);
            uint8_t sb = spr_px[i * 4 + 0];
            uint8_t sg = spr_px[i * 4 + 1];
            uint8_t sr = spr_px[i * 4 + 2];

            uint8_t pr = (uint8_t)((sr * r) / 255);
            uint8_t pg = (uint8_t)((sg * g) / 255);
            uint8_t pb = (uint8_t)((sb * b) / 255);

            dst[i] = ((uint32_t)a << 24) | ((uint32_t)pr << 16) | ((uint32_t)pg << 8) | (uint32_t)pb;
        }
    } else {
        for (int i = 0; i < total; ++i) {
            uint8_t a = (uint8_t)(src[i] & 0xFF);
            uint8_t pr = (uint8_t)((r * a) / 255);
            uint8_t pg = (uint8_t)((g * a) / 255);
            uint8_t pb = (uint8_t)((b * a) / 255);
            dst[i] = ((uint32_t)a << 24) | ((uint32_t)pr << 16) | ((uint32_t)pg << 8) | (uint32_t)pb;
        }
    }
    return font->tinted_dib[idx].hdc_mem;
}

static bool popcap_font_load(
    PopCapFont *font, const char *font_name,
    const char *sprite_name, const char *mask_name, const char *outline_name
) {
    if (!font || !font_name) return false;
    if (font->loaded) return true;

    char root_dir[MAX_PATH] = {0};
    if (!get_zuma_game_root_dir(root_dir, sizeof(root_dir))) return false;

    char txt_path[MAX_PATH] = {0};
    if (root_dir[0] != '\0') {
        snprintf(txt_path, sizeof(txt_path), "%s\\fonts\\%s.txt", root_dir, font_name);
    }
    if (txt_path[0] == '\0' || GetFileAttributesA(txt_path) == INVALID_FILE_ATTRIBUTES) {
        snprintf(txt_path, sizeof(txt_path), "%s\\..\\Zuma Compleluxe\\fonts\\%s.txt", root_dir, font_name);
    }
    if (GetFileAttributesA(txt_path) == INVALID_FILE_ATTRIBUTES) {
        snprintf(txt_path, sizeof(txt_path), "D:\\Files\\Project\\Reverse Engineer\\Zuma Compleluxe\\fonts\\%s.txt", font_name);
    }
    if (GetFileAttributesA(txt_path) == INVALID_FILE_ATTRIBUTES) return false;

    if (!parse_popcap_font_txt(txt_path, font)) return false;
    strncpy(font->name, font_name, sizeof(font->name) - 1);

    /* 1. Base / Colored Sprite */
    if (sprite_name && sprite_name[0]) {
        font->sprite = layout_get_cached_sprite(sprite_name, 1);
    }

    /* 2. Grayscale Mask Image */
    if (mask_name && mask_name[0]) {
        char mask_path[MAX_PATH];
        if (!layout_get_zuma_asset_path(mask_name, mask_path, sizeof(mask_path))) {
            snprintf(mask_path, sizeof(mask_path), "%s\\fonts\\%s.gif", root_dir, mask_name);
            if (GetFileAttributesA(mask_path) == INVALID_FILE_ATTRIBUTES) {
                snprintf(mask_path, sizeof(mask_path), "D:\\Files\\Project\\Reverse Engineer\\Zuma Compleluxe\\fonts\\%s.gif", mask_name);
            }
        }
        if (GetFileAttributesA(mask_path) != INVALID_FILE_ATTRIBUTES) {
            image_load_from_file(mask_path, &font->mask_img);
        }
    }

    /* 3. Outline Mask Image & DIB */
    font->has_outline = false;
    if (outline_name && outline_name[0]) {
        char out_path[MAX_PATH];
        if (!layout_get_zuma_asset_path(outline_name, out_path, sizeof(out_path))) {
            snprintf(out_path, sizeof(out_path), "%s\\fonts\\%s.gif", root_dir, outline_name);
        }
        if (GetFileAttributesA(out_path) != INVALID_FILE_ATTRIBUTES) {
            if (image_load_from_file(out_path, &font->outline_img)) {
                if (create_argb_dib(font->outline_img.width, font->outline_img.height, &font->outline_dib)) {
                    uint32_t *dst = (uint32_t*)font->outline_dib.bits;
                    uint32_t *src = (uint32_t*)font->outline_img.bits;
                    int total = font->outline_img.width * font->outline_img.height;
                    for (int i = 0; i < total; ++i) {
                        uint8_t a = (uint8_t)(src[i] & 0xFF);
                        dst[i] = ((uint32_t)a << 24); /* Pure black with outline alpha */
                    }
                    font->has_outline = true;
                }
            }
        }
    }

    font->loaded = true;
    return true;
}

static void ensure_popcap_fonts(void) {
    popcap_font_load(&g_popcap_native_alien_48, "NativeAlien48", "fonts\\NativeAlien48", "_NativeAlien48", NULL);
    popcap_font_load(&g_popcap_native_alien_18, "NativeAlienExtended18", "fonts\\NativeAlienExtended18", "_NativeAlienExtended18", NULL);
    popcap_font_load(&g_popcap_native_alien_16, "NativeAlienExtended16", "fonts\\NativeAlienExtended16", "_NativeAlienExtended16", NULL);
    popcap_font_load(&g_popcap_cancun_14, "CancunFloat14", "fonts\\CancunFloat14", "_CancunFloat14", NULL);
    popcap_font_load(&g_popcap_cancun_12, "Cancun12", NULL, "_Cancun12", NULL);
    popcap_font_load(&g_popcap_cancun_13, "Cancun13", NULL, "_Cancun13", NULL);
    popcap_font_load(&g_popcap_cancun_10, "Cancun10", NULL, "_Cancun10", NULL);
    popcap_font_load(&g_popcap_cancun_outline_10, "Cancun10", NULL, "_Cancun10", "_CancunOUTLINE10");
    popcap_font_load(&g_popcap_cancun_outline_8, "Cancun8", NULL, "_Cancun8", "_CancunFAT8");
    popcap_font_load(&g_popcap_arial_12, "Arial12Bold", NULL, "_Arial12Bold", NULL);
}

static int popcap_font_get_line_width(const PopCapFont *font, const char *line, int len) {
    if (!font || !font->loaded || !line || len <= 0) return 0;
    int w = 0;
    for (int i = 0; i < len; ++i) {
        char c = line[i];
        for (int m = 0; m < font->map_count; ++m) {
            if (c == font->map_from[m]) { c = font->map_to[m]; break; }
        }
        if (c == ' ') {
            w += font->space_width;
            continue;
        }
        const PopCapCharDef *def = NULL;
        for (int k = 0; k < font->char_count; ++k) {
            if (font->chars[k].ch == c) { def = &font->chars[k]; break; }
        }
        if (!def && c >= 'a' && c <= 'z') {
            char uc = (char)toupper((unsigned char)c);
            for (int k = 0; k < font->char_count; ++k) {
                if (font->chars[k].ch == uc) { def = &font->chars[k]; break; }
            }
        }
        if (def) w += def->width;
        else w += font->space_width;
    }
    return w;
}

static void popcap_font_draw_line(
    HDC mem_dc, PopCapFont *font, const char *line, int len,
    int pen_x, int baseline_y, COLORREF color, float vscale
) {
    if (!font || !font->loaded || !line || len <= 0) return;

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC tint_dc = (font->mask_img.bits) ? popcap_font_get_tinted_dc(font, color) : NULL;
    HDC sprite_dc = (font->sprite && font->sprite->loaded && font->sprite->img.hbm) ? font->sprite->img.hdc_mem : NULL;

    /* Pass 1: Outline Layer (if font has outline) */
    if (font->has_outline && font->outline_dib.hdc_mem) {
        int cur_x = pen_x;
        for (int i = 0; i < len; ++i) {
            char c = line[i];
            for (int m = 0; m < font->map_count; ++m) {
                if (font->map_from[m] == c) { c = font->map_to[m]; break; }
            }
            if (c == ' ') {
                cur_x += (int)roundf((float)font->space_width * vscale);
                continue;
            }
            const PopCapCharDef *def = NULL;
            for (int k = 0; k < font->char_count; ++k) {
                if (font->chars[k].ch == c) { def = &font->chars[k]; break; }
            }
            if (!def && c >= 'a' && c <= 'z') {
                char uc = (char)toupper((unsigned char)c);
                for (int k = 0; k < font->char_count; ++k) {
                    if (font->chars[k].ch == uc) { def = &font->chars[k]; break; }
                }
            }
            if (!def) {
                cur_x += (int)roundf((float)font->space_width * vscale);
                continue;
            }
            if (def->out_w > 0 && def->out_h > 0) {
                int ox = cur_x + (int)roundf((float)def->out_off_x * vscale);
                int oy = baseline_y - (int)roundf((float)(font->outline_ascent - def->out_off_y) * vscale);
                int ow = (int)roundf((float)def->out_w * vscale);
                int oh = (int)roundf((float)def->out_h * vscale);
                AlphaBlend(mem_dc, ox, oy, ow, oh,
                           font->outline_dib.hdc_mem, def->out_x, def->out_y, def->out_w, def->out_h, bf);
            }
            cur_x += (int)roundf((float)def->width * vscale);
        }
    }

    /* Pass 2: Main Glyph Layer */
    int cur_x = pen_x;
    for (int i = 0; i < len; ++i) {
        char c = line[i];
        for (int m = 0; m < font->map_count; ++m) {
            if (font->map_from[m] == c) { c = font->map_to[m]; break; }
        }
        if (c == ' ') {
            cur_x += (int)roundf((float)font->space_width * vscale);
            continue;
        }
        const PopCapCharDef *def = NULL;
        for (int k = 0; k < font->char_count; ++k) {
            if (font->chars[k].ch == c) { def = &font->chars[k]; break; }
        }
        if (!def && c >= 'a' && c <= 'z') {
            char uc = (char)toupper((unsigned char)c);
            for (int k = 0; k < font->char_count; ++k) {
                if (font->chars[k].ch == uc) { def = &font->chars[k]; break; }
            }
        }
        if (!def) {
            cur_x += (int)roundf((float)font->space_width * vscale);
            continue;
        }
        if (def->src_w > 0 && def->src_h > 0) {
            int gx = cur_x + (int)roundf((float)def->off_x * vscale);
            int gy = baseline_y - (int)roundf((float)(font->ascent - def->off_y) * vscale);
            int gw = (int)roundf((float)def->src_w * vscale);
            int gh = (int)roundf((float)def->src_h * vscale);
            if (tint_dc) {
                AlphaBlend(mem_dc, gx, gy, gw, gh,
                           tint_dc, def->src_x, def->src_y, def->src_w, def->src_h, bf);
            } else if (sprite_dc) {
                AlphaBlend(mem_dc, gx, gy, gw, gh,
                           sprite_dc, def->src_x, def->src_y, def->src_w, def->src_h, bf);
            }
        }
        cur_x += (int)roundf((float)def->width * vscale);
    }
}

static void popcap_font_draw_text(
    HDC mem_dc, PopCapFont *font, const char *text,
    const RECT *rc, COLORREF color, float vscale, UINT draw_flags
);

/* Authentic Sandy Glow Text Renderer (Zuma Testing Kit ztk_draw_text_sandy_glow) */
static void draw_sandy_glow_text(HDC mem_dc, PopCapFont *font, const char *text, const RECT *rc, float vscale) {
    if (!font || !font->loaded || !text || !rc) return;
    int offsets[8][2] = {
        {-2, 0}, {2, 0}, {0, -2}, {0, 2},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
    };
    for (int o = 0; o < 8; ++o) {
        RECT rco = {
            rc->left + (int)roundf((float)offsets[o][0] * vscale),
            rc->top + (int)roundf((float)offsets[o][1] * vscale),
            rc->right + (int)roundf((float)offsets[o][0] * vscale),
            rc->bottom + (int)roundf((float)offsets[o][1] * vscale)
        };
        popcap_font_draw_text(mem_dc, font, text, &rco, RGB(212, 106, 0), vscale, DT_CENTER | DT_VCENTER);
    }
    popcap_font_draw_text(mem_dc, font, text, rc, RGB(0, 229, 255), vscale, DT_CENTER | DT_VCENTER);
}

static void popcap_font_draw_text_ex(
    HDC mem_dc, PopCapFont *font, const char *text,
    const RECT *rc, COLORREF color, float vscale, UINT draw_flags, int custom_line_spacing
) {
    if (!font || !font->loaded || !text || !rc) return;

    int line_spacing = (custom_line_spacing > 0) ? custom_line_spacing : (font->ascent + font->line_spacing_offset);
    if (line_spacing <= 0) line_spacing = 14;
    int line_h = (int)roundf((float)line_spacing * vscale);
    if (line_h <= 0) line_h = 16;

    const char *p = text;
    int total_lines = 1;
    while (*p) {
        if (*p == '\n') total_lines++;
        p++;
    }

    int rc_h = rc->bottom - rc->top;
    int rc_w = rc->right - rc->left;
    int total_text_h = total_lines * line_h;

    int top_y = rc->top;
    if (draw_flags & DT_VCENTER) {
        top_y = rc->top + (rc_h - total_text_h) / 2;
    }

    int aYOffset = font->ascent - font->ascent_padding;
    int baseline_y = top_y + (int)roundf((float)aYOffset * vscale);

    const char *line_start = text;
    int line_idx = 0;
    while (*line_start) {
        const char *line_end = strchr(line_start, '\n');
        int len = line_end ? (int)(line_end - line_start) : (int)strlen(line_start);

        int line_w = (int)roundf((float)popcap_font_get_line_width(font, line_start, len) * vscale);
        int cur_x = rc->left;
        if (draw_flags & DT_CENTER) {
            cur_x = rc->left + (rc_w - line_w) / 2;
        } else if (draw_flags & DT_RIGHT) {
            cur_x = rc->right - line_w;
        }

        popcap_font_draw_line(mem_dc, font, line_start, len, cur_x, baseline_y + line_idx * line_h, color, vscale);

        if (!line_end) break;
        line_start = line_end + 1;
        line_idx++;
    }
}

static void popcap_font_draw_text(
    HDC mem_dc, PopCapFont *font, const char *text,
    const RECT *rc, COLORREF color, float vscale, UINT draw_flags
) {
    popcap_font_draw_text_ex(mem_dc, font, text, rc, color, vscale, draw_flags, 0);
}

/* Word-wrap line counter for layout calculation and verification */
static int popcap_font_count_wrapped_lines(const PopCapFont *font, const char *text, int max_w, float vscale) {
    if (!font || !font->loaded || !text || max_w <= 0) return 0;
    int line_count = 0;
    const char *p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char *line_start = p;
        const char *last_space_end = NULL;
        const char *cur = p;
        int cur_w = 0;

        while (*cur && *cur != '\n') {
            const char *word_start = cur;
            while (*cur && *cur != ' ' && *cur != '\n') cur++;
            int word_len = (int)(cur - word_start);
            int word_w = (int)roundf((float)popcap_font_get_line_width(font, word_start, word_len) * vscale);

            int space_w = 0;
            if (*cur == ' ') {
                space_w = (int)roundf((float)font->space_width * vscale);
            }

            if (cur_w > 0 && cur_w + word_w > max_w) {
                break;
            }

            cur_w += word_w + space_w;
            last_space_end = cur;
            if (*cur == ' ') cur++;
        }

        int line_len = 0;
        if (last_space_end && last_space_end > line_start) {
            line_len = (int)(last_space_end - line_start);
            p = (*last_space_end == ' ') ? last_space_end + 1 : last_space_end;
        } else {
            line_len = (cur > line_start) ? (int)(cur - line_start) : 1;
            p = line_start + line_len;
            if (*p == ' ') p++;
        }

        line_count++;
        if (*p == '\n') p++;
    }
    return line_count;
}

/* Word-wrap text renderer replicating Zuma.exe Widget::DrawStringWordWrap (0x0047A770) */
static void popcap_font_draw_word_wrap(
    HDC mem_dc, PopCapFont *font, const char *text,
    const RECT *rc, COLORREF color, float vscale, int line_spacing
) {
    if (!font || !font->loaded || !text || !rc) return;
    int max_w = rc->right - rc->left;
    if (max_w <= 0) return;

    if (line_spacing <= 0) line_spacing = 14;
    int step_y = (int)roundf((float)line_spacing * vscale);
    if (step_y <= 0) step_y = 12;

    int base_offset = le_font_baseline_offset(font);
    int baseline_y = rc->top + (int)roundf((float)base_offset * vscale);

    const char *p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char *line_start = p;
        const char *last_space_end = NULL;
        const char *cur = p;
        int cur_w = 0;

        while (*cur && *cur != '\n') {
            const char *word_start = cur;
            while (*cur && *cur != ' ' && *cur != '\n') cur++;
            int word_len = (int)(cur - word_start);
            int word_w = (int)roundf((float)popcap_font_get_line_width(font, word_start, word_len) * vscale);

            int space_w = 0;
            if (*cur == ' ') {
                space_w = (int)roundf((float)font->space_width * vscale);
            }

            if (cur_w > 0 && cur_w + word_w > max_w) {
                break;
            }

            cur_w += word_w + space_w;
            last_space_end = cur;
            if (*cur == ' ') cur++;
        }

        int line_len = 0;
        if (last_space_end && last_space_end > line_start) {
            line_len = (int)(last_space_end - line_start);
            p = (*last_space_end == ' ') ? last_space_end + 1 : last_space_end;
        } else {
            line_len = (cur > line_start) ? (int)(cur - line_start) : 1;
            p = line_start + line_len;
            if (*p == ' ') p++;
        }

        if (baseline_y <= rc->bottom + step_y) {
            popcap_font_draw_line(mem_dc, font, line_start, line_len, rc->left, baseline_y, color, vscale);
        }
        baseline_y += step_y;
        if (*p == '\n') p++;
    }
}

/* Render Authentic Text Element */
static void render_layout_text_element(
    HDC mem_dc, LayoutElement *el, const LayoutEditorState *st,
    HFONT font_bold, HFONT font_ui,
    bool is_dark, bool is_selected, bool is_hovered,
    float vscale
) {
    const char *tag = el->tag ? el->tag : "";
    const char *disp_text = tag;
    COLORREF text_color = RGB(255, 234, 1); /* Authentic Zuma Gold default */
    UINT draw_flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
    PopCapFont *pfont = &g_popcap_cancun_10;
    bool is_multiline_custom = false;

    /* Suppress duplicate text for elements already rendered in composite pass (HelpScreen, DevHelpScreen, DevDemo, MainMenu welcome) */
    if ((el->section && (_stricmp(el->section, "HelpScreen") == 0 || _stricmp(el->section, "DevHelpScreen") == 0)) ||
        (_stricmp(tag, "DemoClear") == 0) || (_stricmp(tag, "DemoClick") == 0) ||
        (_stricmp(tag, "WelcomeText") == 0) || (_stricmp(tag, "UserSwitchText") == 0)) {
        if (is_selected || is_hovered) {
            COLORREF border_color = is_selected ? RGB(56, 189, 248) : RGB(125, 211, 252);
            HPEN pen = CreatePen(PS_DOT, 1, border_color);
            HPEN old_pen = (HPEN)SelectObject(mem_dc, pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, el->canvas_rect.left - 2, el->canvas_rect.top - 2,
                      el->canvas_rect.right + 2, el->canvas_rect.bottom + 2);
            SelectObject(mem_dc, old_pen);
            DeleteObject(pen);
        }
        return;
    }

    if (_stricmp(tag, "HeaderTitle") == 0) {
        disp_text = "DEVELOPER MODE";
        draw_sandy_glow_text(mem_dc, &g_popcap_native_alien_16, disp_text, &el->canvas_rect, vscale);
        if (is_selected || is_hovered) {
            COLORREF border_color = is_selected ? RGB(56, 189, 248) : RGB(125, 211, 252);
            HPEN pen = CreatePen(PS_DOT, 1, border_color);
            HPEN old_pen = (HPEN)SelectObject(mem_dc, pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, el->canvas_rect.left - 2, el->canvas_rect.top - 2,
                      el->canvas_rect.right + 2, el->canvas_rect.bottom + 2);
            SelectObject(mem_dc, old_pen);
            DeleteObject(pen);
        }
        return;
    } else if (_stricmp(tag, "HeaderSubtitle") == 0) {
        disp_text = "SPECIAL TESTING PROFILE  -  UNRESTRICTED GAME ACCESS";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_cancun_10;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "AdvHeader") == 0) {
        disp_text = "ADVENTURE MODE";
        text_color = RGB(255, 255, 0);
        pfont = &g_popcap_cancun_14;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "AdvBullets") == 0) {
        is_multiline_custom = true;
    } else if (_stricmp(tag, "GauntHeader") == 0) {
        disp_text = "GAUNTLET MODE";
        text_color = RGB(255, 255, 0);
        pfont = &g_popcap_cancun_14;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "GauntBullets") == 0) {
        is_multiline_custom = true;
    } else if (_stricmp(tag, "CheatHeader") == 0) {
        disp_text = "MIDDLE MOUSE BUTTON";
        text_color = RGB(0, 229, 255);
        pfont = &g_popcap_cancun_14;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "CheatBullets") == 0) {
        is_multiline_custom = true;
    } else if (_stricmp(tag, "DemoClick") == 0) {
        disp_text = "CLICK!";
        text_color = RGB(255, 255, 255);
        pfont = &g_popcap_arial_12;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "DemoClear") == 0) {
        disp_text = "+100 +100 +100 +100 +100";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_cancun_10;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "Title") == 0) {
        disp_text = "TEMPLE CLEARED!";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_native_alien_16;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "DialogText") == 0) {
        disp_text = "The Gods of Zuma smile upon you!\nYou have unlocked the ancient chamber.";
        text_color = RGB(255, 255, 255);
        pfont = &g_popcap_cancun_10;
        draw_flags = DT_LEFT | DT_TOP;
    } else if (_stricmp(tag, "FrogReply") == 0) {
        disp_text = "\"Ribbit! Onward to victory!\"";
        text_color = RGB(160, 255, 128);
        pfont = &g_popcap_cancun_10;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "StageText") == 0) {
        if (_stricmp(el->section, "StageComplete") == 0) {
            disp_text = "STAGE COMPLETE";
            pfont = &g_popcap_native_alien_16;
        } else {
            disp_text = "SECRET SHRINE OF ZUMA";
            pfont = &g_popcap_native_alien_16;
        }
        text_color = RGB(255, 234, 1);
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "CompleteText") == 0) {
        disp_text = "BONUS: 50,000";
        text_color = RGB(255, 255, 0);
        pfont = &g_popcap_cancun_14;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "ClickToPlay") == 0) {
        disp_text = "CLICK HERE TO PLAY!";
        text_color = RGB(255, 236, 145); /* PopCap authentic 0xffec91 */
        pfont = &g_popcap_cancun_14;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "HighscoreScores") == 0) {
        if (_stricmp(el->section, "GauntletLabels") == 0) {
            disp_text = "10000\n5000\n1000";
            text_color = RGB(91, 1, 10);
        } else {
            disp_text = "686500\n194100\n194100";
            text_color = RGB(40, 83, 76);
        }
        pfont = &g_popcap_cancun_outline_8;
        draw_flags = DT_LEFT | DT_TOP;
    } else if (_stricmp(tag, "HighscoreNames") == 0) {
        if (_stricmp(el->section, "GauntletLabels") == 0) {
            disp_text = "SUKHBIR\nTYSEN\nSHAWN";
            text_color = RGB(91, 1, 10);
        } else {
            disp_text = "DEVTESTER\nDEVTESTER\nDEVTESTER";
            text_color = RGB(40, 83, 76);
        }
        pfont = &g_popcap_cancun_outline_8;
        draw_flags = DT_LEFT | DT_TOP;
    } else if (_stricmp(tag, "HighscoreDate") == 0) {
        if (el->section && _stricmp(el->section, "AdventureLabels") == 0) return; /* Clipping width, not date */
        disp_text = "10/31/26";
        text_color = RGB(160, 255, 128);
        pfont = &g_popcap_cancun_10;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "HighscoreTitle") == 0) {
        disp_text = "HIGH SCORES";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_cancun_10;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "StageNum") == 0) {
        disp_text = "12";
        text_color = RGB(251, 241, 176);
        pfont = &g_popcap_cancun_14;
        draw_flags = DT_LEFT | DT_TOP;
    } else if (_stricmp(tag, "Subtitle") == 0) {
        disp_text = "YOU ENTER THE RUINS ONE LAST TIME...";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_cancun_outline_10;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "Stage13Badge") == 0 || _stricmp(tag, "Stage13Num") == 0) {
        disp_text = "STAGE 13";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_cancun_14;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "Stage13Title") == 0) {
        disp_text = "SPACE";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_native_alien_16;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "Stage13Subtitle") == 0) {
        disp_text = "THE FINAL BATTLE";
        text_color = RGB(0, 229, 255);
        pfont = &g_popcap_cancun_outline_10;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "DoorName") == 0) {
        disp_text = "RANDOM";
        text_color = RGB(255, 255, 255);
        pfont = &g_popcap_cancun_outline_10;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "PracticeText") == 0) {
        disp_text = "PRACTICE";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_cancun_outline_10;
        draw_flags = DT_RIGHT | DT_VCENTER;
    } else if (_stricmp(tag, "InfoBox") == 0) {
        if (_stricmp(el->section, "GauntletLabels") == 0) {
            disp_text = "PLAY THROUGH ALL THE LEVELS YOU HAVE UNLOCKED IN ADVENTURE MODE IN RANDOM ORDER!";
            text_color = RGB(91, 1, 10);
        } else {
            disp_text = "UNCOVER THE SECRET TEMPLES OF ZUMA!";
            text_color = RGB(40, 83, 76);
        }
        pfont = &g_popcap_cancun_outline_8;
        draw_flags = DT_LEFT | DT_TOP;
    } else if (_stricmp(tag, "CustomMapTag") == 0) {
        disp_text = "ORIGINAL MAPS";
        text_color = RGB(160, 255, 128);
        pfont = &g_popcap_cancun_10;
        draw_flags = DT_CENTER;
    } else if (_stricmp(tag, "TopText1") == 0) {
        disp_text = "MAKE MATCHES OF 3 OR MORE TO DESTROY BALLS.";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_cancun_10;
        draw_flags = DT_LEFT;
    } else if (_stricmp(tag, "TopText2") == 0) {
        disp_text = "DON'T LET THE BALLS REACH THE GOLDEN SKULL!";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_cancun_10;
        draw_flags = DT_LEFT;
    } else if (_stricmp(tag, "SwapClick") == 0) {
        disp_text = "RIGHT CLICK TO SWAP BALLS";
        text_color = RGB(255, 234, 1);
        pfont = &g_popcap_cancun_10;
        draw_flags = DT_LEFT;
    }

    if (is_multiline_custom) {
        const char *lines[6];
        COLORREF line_cols[6];
        int num_lines = 0;
        int spacing = 18;

        if (_stricmp(tag, "AdvBullets") == 0) {
            lines[0] = "* ALL 13 STAGES UNLOCKED FROM START"; line_cols[0] = RGB(255, 255, 255);
            lines[1] = "* DIRECT TEMPLE 5 ACCESS TO SECRET";  line_cols[1] = RGB(255, 255, 255);
            lines[2] = "  STAGE 13 (\"THE VOID\")";          line_cols[2] = RGB(0, 229, 255);
            num_lines = 3;
            spacing = 18;
        } else if (_stricmp(tag, "GauntBullets") == 0) {
            lines[0] = "* ALL LEVELS UNLOCKED IN GAUNTLET";   line_cols[0] = RGB(255, 255, 255);
            lines[1] = "* PRACTICE & SURVIVAL MODES ACTIVE";  line_cols[1] = RGB(255, 255, 255);
            lines[2] = "* FULL SUN GOD RANK ACCESS UNLOCKED"; line_cols[2] = RGB(255, 234, 1);
            lines[3] = "* JUMP TO ANY DIFFICULTY TIER OR MAP";line_cols[3] = RGB(255, 185, 45);
            num_lines = 4;
            spacing = 21;
        } else if (_stricmp(tag, "CheatBullets") == 0) {
            lines[0] = "JUST PLAY NORMALLY, OR CLICK MIDDLE MOUSE";     line_cols[0] = RGB(255, 255, 255);
            lines[1] = "BUTTON AT ANY TIME TO END IT ALL AND ADVANCE!"; line_cols[1] = RGB(255, 255, 255);
            lines[2] = "* INSTANTLY AWARDS FULL TARGET SCORE";          line_cols[2] = RGB(255, 234, 1);
            lines[3] = "* TRIGGERS ZUMA DESTROYALL & CLEARS ALL BALLS"; line_cols[3] = RGB(255, 234, 1);
            lines[4] = "* AUTOMATICALLY COMPLETES THE CURRENT STAGE";    line_cols[4] = RGB(255, 185, 45);
            num_lines = 5;
            spacing = 18;
        }

        int sp_px = (int)roundf((float)spacing * vscale);
        if (sp_px <= 0) sp_px = 16;
        for (int l = 0; l < num_lines; ++l) {
            RECT rc_l = { el->canvas_rect.left, el->canvas_rect.top + l * sp_px,
                          el->canvas_rect.right, el->canvas_rect.top + (l + 1) * sp_px };
            if (g_popcap_cancun_10.loaded) {
                int base_l = rc_l.top + (int)roundf((float)(g_popcap_cancun_10.ascent - g_popcap_cancun_10.ascent_padding) * vscale);
                popcap_font_draw_line(mem_dc, &g_popcap_cancun_10, lines[l], (int)strlen(lines[l]),
                                      rc_l.left, base_l, line_cols[l], vscale);
            } else {
                SetTextColor(mem_dc, line_cols[l]);
                DrawTextA(mem_dc, lines[l], -1, &rc_l, DT_LEFT | DT_TOP | DT_SINGLELINE);
            }
        }
    } else {
        RECT rc_txt = el->canvas_rect;
        if (pfont && pfont->loaded) {
            if (_stricmp(tag, "ClickToPlay") == 0) {
                int line_w = (int)roundf((float)popcap_font_get_line_width(pfont, disp_text, (int)strlen(disp_text)) * vscale);
                int rc_w = rc_txt.right - rc_txt.left;
                int cur_x = rc_txt.left + (rc_w - line_w) / 2;

                /* PopCap HyperlinkWidget button dimensions: btn_h = font_h + 20 */
                int font_h = pfont->ascent + pfont->line_spacing_offset;
                if (font_h <= 0) font_h = 18;
                int btn_h = font_h + 20;

                /* PopCap HyperlinkWidget::Draw baseline: widget_y + (GetAscent() + mHeight) / 2 - 1 */
                int aY = (pfont->ascent + btn_h) / 2 - 1;
                int baseline_y = rc_txt.top + (int)roundf((float)aY * vscale);

                popcap_font_draw_line(mem_dc, pfont, disp_text, (int)strlen(disp_text), cur_x, baseline_y, text_color, vscale);

                int uy = baseline_y + (int)roundf(3.0f * vscale);
                int uh = (int)roundf(2.0f * vscale);
                if (uh < 1) uh = 1;

                HBRUSH br = CreateSolidBrush(text_color);
                RECT rc_ul = { cur_x, uy, cur_x + line_w, uy + uh };
                FillRect(mem_dc, &rc_ul, br);
                DeleteObject(br);
            } else {
                if (_stricmp(tag, "InfoBox") == 0) {
                    int l_spacing = (el->flags & ATTR_HAS_LINESPACING && el->lineSpacing > 0) ? el->lineSpacing : 20;
                    popcap_font_draw_word_wrap(mem_dc, pfont, disp_text, &rc_txt, text_color, vscale, l_spacing);
                } else if (_stricmp(tag, "HighscoreScores") == 0 || _stricmp(tag, "HighscoreNames") == 0) {
                    int row_sp = 21; /* PopCap authentic HighscoreRow spacing */
                    if (st) {
                        for (int k = 0; k < st->element_count; ++k) {
                            if (_stricmp(st->elements[k].section, el->section) == 0 &&
                                _stricmp(st->elements[k].tag, "HighscoreRow") == 0) {
                                if (st->elements[k].flags & ATTR_HAS_SPACING && st->elements[k].spacing > 0) {
                                    row_sp = st->elements[k].spacing;
                                }
                                break;
                            }
                        }
                    }
                    popcap_font_draw_text_ex(mem_dc, pfont, disp_text, &rc_txt, text_color, vscale, draw_flags, row_sp);
                } else {
                    popcap_font_draw_text(mem_dc, pfont, disp_text, &rc_txt, text_color, vscale, draw_flags);
                }
            }
        } else {
            SetBkMode(mem_dc, TRANSPARENT);
            SelectObject(mem_dc, font_bold);
            if (_stricmp(tag, "ClickToPlay") == 0) {
                SIZE sz;
                GetTextExtentPoint32A(mem_dc, disp_text, (int)strlen(disp_text), &sz);
                int rc_w = rc_txt.right - rc_txt.left;
                int cur_x = rc_txt.left + (rc_w - sz.cx) / 2;
                int btn_h = 38;
                int aY = (18 + btn_h) / 2 - 1;
                int baseline_y = rc_txt.top + (int)roundf((float)aY * vscale);
                int top_y = baseline_y - sz.cy + (int)roundf(3.0f * vscale);
                RECT rc_draw = { cur_x, top_y, cur_x + sz.cx, top_y + sz.cy };
                RECT rc_sh = rc_draw;
                OffsetRect(&rc_sh, 1, 1);
                SetTextColor(mem_dc, RGB(0, 0, 0));
                DrawTextA(mem_dc, disp_text, -1, &rc_sh, DT_LEFT | DT_TOP | DT_SINGLELINE);
                SetTextColor(mem_dc, text_color);
                DrawTextA(mem_dc, disp_text, -1, &rc_draw, DT_LEFT | DT_TOP | DT_SINGLELINE);

                int uy = baseline_y + (int)roundf(3.0f * vscale);
                HBRUSH br = CreateSolidBrush(text_color);
                RECT rc_ul = { cur_x, uy, cur_x + sz.cx, uy + 2 };
                FillRect(mem_dc, &rc_ul, br);
                DeleteObject(br);
            } else {
                RECT rc_sh = rc_txt;
                OffsetRect(&rc_sh, 1, 1);
                SetTextColor(mem_dc, RGB(0, 0, 0));
                DrawTextA(mem_dc, disp_text, -1, &rc_sh, draw_flags);
                SetTextColor(mem_dc, text_color);
                DrawTextA(mem_dc, disp_text, -1, &rc_txt, draw_flags);
            }
        }
    }

    /* Selection / hover outline */
    if (is_selected || is_hovered) {
        COLORREF border_color = is_selected ? RGB(56, 189, 248) : RGB(125, 211, 252);
        HPEN pen = CreatePen(PS_DOT, 1, border_color);
        HPEN old_pen = (HPEN)SelectObject(mem_dc, pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        Rectangle(mem_dc, el->canvas_rect.left - 2, el->canvas_rect.top - 2,
                  el->canvas_rect.right + 2, el->canvas_rect.bottom + 2);
        SelectObject(mem_dc, old_pen);
        DeleteObject(pen);
    }
}

/* Render Region Outline Element */
static void render_layout_region_element(
    HDC mem_dc, LayoutElement *el,
    HFONT font_bold, HFONT font_ui,
    bool is_dark, bool is_selected, bool is_hovered
) {
    if (!is_selected && !is_hovered) return;

    COLORREF border_color = is_selected ? RGB(56, 189, 248) :
                            (is_hovered ? RGB(125, 211, 252) : (is_dark ? RGB(100, 116, 139) : RGB(148, 163, 184)));

    HPEN pen = CreatePen(PS_DASH, is_selected ? 2 : 1, border_color);
    HPEN old_pen = (HPEN)SelectObject(mem_dc, pen);
    SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
    Rectangle(mem_dc, el->canvas_rect.left, el->canvas_rect.top, el->canvas_rect.right, el->canvas_rect.bottom);
    SelectObject(mem_dc, old_pen);
    DeleteObject(pen);

    /* Subtle corner badge tag */
    char badge[64];
    if (el->flags & ATTR_HAS_ID) {
        snprintf(badge, sizeof(badge), "[%s: %s]", el->tag, el->id);
    } else {
        snprintf(badge, sizeof(badge), "[%s]", el->tag);
    }
    SetBkMode(mem_dc, TRANSPARENT);
    SetTextColor(mem_dc, is_selected ? RGB(56, 189, 248) : (is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139)));
    SelectObject(mem_dc, font_ui);
    RECT rc_b = { el->canvas_rect.left + 4, el->canvas_rect.top + 2, el->canvas_rect.right - 4, el->canvas_rect.top + 18 };
    DrawTextA(mem_dc, badge, -1, &rc_b, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* Render Point Anchor Element */
static void render_layout_point_element(
    HDC mem_dc, LayoutElement *el,
    HFONT font_bold, HFONT font_ui,
    bool is_dark, bool is_selected, bool is_hovered,
    float vscale
) {
    int cx = (el->canvas_rect.left + el->canvas_rect.right) / 2;
    int cy = (el->canvas_rect.top + el->canvas_rect.bottom) / 2;

    /* Interactive Demo Balls rendering in DevHelpScreen */
    if (_stricmp(el->tag, "DemoBalls") == 0) {
        if (g_layout_editor && g_layout_editor->cur_screen_idx == 11) {
            return; /* Handled in Screen 11 stone tablet composite */
        }
        const char *balls[] = { "baBallRed", "baBallYellow", "baBallGreen", "baBallBlue", "baBallPurple" };
        int ball_sz = (int)roundf(32.0f * vscale);
        int spacing = (int)roundf(32.0f * vscale);
        int total_w = 5 * spacing;
        int start_bx = cx - total_w / 2;
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

        for (int b = 0; b < 5; ++b) {
            LayoutSprite *sp_b = layout_get_cached_sprite(balls[b], 1);
            if (sp_b && sp_b->loaded && sp_b->img.hbm) {
                int src_cell_h = sp_b->img.height / sp_b->rows;
                if (src_cell_h <= 0) src_cell_h = 32;
                int bx = start_bx + b * spacing;
                int by = cy - ball_sz / 2;
                AlphaBlend(mem_dc, bx, by, ball_sz, ball_sz,
                           sp_b->img.hdc_mem, 0, 0, sp_b->img.width, src_cell_h, bf);

                /* Floating +100 text above each ball */
                RECT rc_plus = { bx - 5, by - (int)roundf(14.0f * vscale), bx + ball_sz + 5, by };
                if (g_popcap_cancun_10.loaded) {
                    popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "+100", &rc_plus, RGB(255, 234, 1), vscale, DT_CENTER);
                } else {
                    SetTextColor(mem_dc, RGB(255, 234, 1));
                    DrawTextA(mem_dc, "+100", -1, &rc_plus, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
        }
        return;
    }

    if (!is_selected && !is_hovered) return;

    COLORREF pt_color = is_selected ? RGB(250, 204, 21) :
                        (is_hovered ? RGB(56, 189, 248) : (is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139)));

    /* Crosshairs */
    HPEN pen = CreatePen(PS_SOLID, 1, pt_color);
    HPEN old_pen = (HPEN)SelectObject(mem_dc, pen);
    MoveToEx(mem_dc, cx - 8, cy, NULL);
    LineTo(mem_dc, cx + 9, cy);
    MoveToEx(mem_dc, cx, cy - 8, NULL);
    LineTo(mem_dc, cx, cy + 9);

    /* Small circle */
    HBRUSH br = CreateSolidBrush(pt_color);
    HBRUSH old_br = (HBRUSH)SelectObject(mem_dc, br);
    Ellipse(mem_dc, cx - 3, cy - 3, cx + 4, cy + 4);
    SelectObject(mem_dc, old_br);
    SelectObject(mem_dc, old_pen);
    DeleteObject(br);
    DeleteObject(pen);

    /* Label tag */
    if (is_selected || is_hovered) {
        char pt_lbl[64];
        snprintf(pt_lbl, sizeof(pt_lbl), "%s", el->tag);
        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, pt_color);
        SelectObject(mem_dc, font_ui);
        TextOutA(mem_dc, cx + 8, cy - 8, pt_lbl, (int)strlen(pt_lbl));
    }
}

/* Rotate 32-bit ARGB sprite and AlphaBlend to destination DC */
static void draw_rotated_sprite(
    HDC dst_dc, LayoutSprite *sp, int frame_x, int frame_y, int frame_w, int frame_h,
    int center_x, int center_y, float scale, float angle_deg
) {
    if (!sp || !sp->loaded || !sp->img.hbm || frame_w <= 0 || frame_h <= 0) return;

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    int dst_w = (int)roundf((float)frame_w * scale);
    int dst_h = (int)roundf((float)frame_h * scale);
    if (dst_w <= 0 || dst_h <= 0) return;

    if (fabsf(angle_deg) < 0.5f || !sp->img.bits) {
        AlphaBlend(dst_dc, center_x - dst_w / 2, center_y - dst_h / 2, dst_w, dst_h,
                   sp->img.hdc_mem, frame_x, frame_y, frame_w, frame_h, bf);
        return;
    }

    int out_sz = (int)(sqrtf((float)(frame_w * frame_w + frame_h * frame_h)) + 2.0f);
    if (out_sz % 2 != 0) out_sz++;

    CanvasImage temp_dib;
    if (!create_argb_dib(out_sz, out_sz, &temp_dib)) {
        AlphaBlend(dst_dc, center_x - dst_w / 2, center_y - dst_h / 2, dst_w, dst_h,
                   sp->img.hdc_mem, frame_x, frame_y, frame_w, frame_h, bf);
        return;
    }

    float rad = angle_deg * 3.14159265f / 180.0f;
    float cos_a = cosf(-rad);
    float sin_a = sinf(-rad);
    float src_cx = frame_w * 0.5f;
    float src_cy = frame_h * 0.5f;
    float out_cx = out_sz * 0.5f;
    float out_cy = out_sz * 0.5f;

    uint32_t *src_bits = (uint32_t*)sp->img.bits;
    uint32_t *dst_bits = (uint32_t*)temp_dib.bits;
    int src_pitch = sp->img.width;

    for (int y = 0; y < out_sz; ++y) {
        float dy = (float)y - out_cy;
        for (int x = 0; x < out_sz; ++x) {
            float dx = (float)x - out_cx;
            float su = src_cx + dx * cos_a - dy * sin_a;
            float sv = src_cy + dx * sin_a + dy * cos_a;
            int iu = (int)roundf(su);
            int iv = (int)roundf(sv);
            if (iu >= 0 && iu < frame_w && iv >= 0 && iv < frame_h) {
                dst_bits[y * out_sz + x] = src_bits[(frame_y + iv) * src_pitch + (frame_x + iu)];
            } else {
                dst_bits[y * out_sz + x] = 0;
            }
        }
    }

    int out_dw = (int)roundf((float)out_sz * scale);
    int out_dh = (int)roundf((float)out_sz * scale);
    AlphaBlend(dst_dc, center_x - out_dw / 2, center_y - out_dh / 2, out_dw, out_dh,
               temp_dib.hdc_mem, 0, 0, out_sz, out_sz, bf);

    if (temp_dib.hbm) DeleteObject(temp_dib.hbm);
    if (temp_dib.hdc_mem) DeleteDC(temp_dib.hdc_mem);
}

/* 9-Slice DialogBox rendering matching PopCap SexyAppFramework */
static void draw_popcap_dialogbox_frame(HDC mem_dc, int dst_x, int dst_y, int dst_w, int dst_h, float vscale) {
    LayoutSprite *sp = layout_get_cached_sprite("dialogbox", 1);
    if (!sp || !sp->loaded || !sp->img.hbm) {
        RECT rc = { dst_x, dst_y, dst_x + dst_w, dst_y + dst_h };
        HBRUSH br = CreateSolidBrush(RGB(35, 30, 25));
        FillRect(mem_dc, &rc, br);
        DeleteObject(br);
        return;
    }
    int sw = sp->img.width;
    int sh = sp->img.height;
    int corner_w = 80;
    int corner_h = 80;
    if (corner_w * 2 > sw) corner_w = sw / 3;
    if (corner_h * 2 > sh) corner_h = sh / 3;
    int mid_sw = sw - 2 * corner_w;
    int mid_sh = sh - 2 * corner_h;

    int cw = (int)roundf((float)corner_w * vscale);
    int ch = (int)roundf((float)corner_h * vscale);
    if (cw * 2 > dst_w) cw = dst_w / 2;
    if (ch * 2 > dst_h) ch = dst_h / 2;
    int mw = dst_w - 2 * cw;
    int mh = dst_h - 2 * ch;

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    /* 4 Corners */
    AlphaBlend(mem_dc, dst_x, dst_y, cw, ch, sp->img.hdc_mem, 0, 0, corner_w, corner_h, bf);
    AlphaBlend(mem_dc, dst_x + dst_w - cw, dst_y, cw, ch, sp->img.hdc_mem, sw - corner_w, 0, corner_w, corner_h, bf);
    AlphaBlend(mem_dc, dst_x, dst_y + dst_h - ch, cw, ch, sp->img.hdc_mem, 0, sh - corner_h, corner_w, corner_h, bf);
    AlphaBlend(mem_dc, dst_x + dst_w - cw, dst_y + dst_h - ch, cw, ch, sp->img.hdc_mem, sw - corner_w, sh - corner_h, corner_w, corner_h, bf);

    /* 4 Edges */
    if (mw > 0) {
        AlphaBlend(mem_dc, dst_x + cw, dst_y, mw, ch, sp->img.hdc_mem, corner_w, 0, mid_sw, corner_h, bf);
        AlphaBlend(mem_dc, dst_x + cw, dst_y + dst_h - ch, mw, ch, sp->img.hdc_mem, corner_w, sh - corner_h, mid_sw, corner_h, bf);
    }
    if (mh > 0) {
        AlphaBlend(mem_dc, dst_x, dst_y + ch, cw, mh, sp->img.hdc_mem, 0, corner_h, corner_w, mid_sh, bf);
        AlphaBlend(mem_dc, dst_x + dst_w - cw, dst_y + ch, cw, mh, sp->img.hdc_mem, sw - corner_w, corner_h, corner_w, mid_sh, bf);
    }

    /* Center Fill */
    if (mw > 0 && mh > 0) {
        AlphaBlend(mem_dc, dst_x + cw, dst_y + ch, mw, mh, sp->img.hdc_mem, corner_w, corner_h, mid_sw, mid_sh, bf);
    }
}

/* 3-Slice Dialog Button rendering matching PopCap DialogButton */
static void draw_popcap_dialogbutton(
    HDC mem_dc, int dst_x, int dst_y, int dst_w, int dst_h,
    const char *label, int state_cell, float vscale
) {
    LayoutSprite *sp = layout_get_cached_sprite("Dialogbutton", 3);
    if (!sp || !sp->loaded || !sp->img.hbm) sp = layout_get_cached_sprite("dialogbutton", 3);
    if (sp && sp->loaded && sp->img.hbm) {
        int cell_w = sp->img.width / 3;
        int cell_h = sp->img.height;
        int src_base_x = state_cell * cell_w;
        int cap_w = cell_w / 3;
        int mid_w = cell_w - 2 * cap_w;

        int d_cap = (int)roundf((float)cap_w * vscale);
        if (d_cap * 2 > dst_w) d_cap = dst_w / 2;
        int d_mid = dst_w - 2 * d_cap;

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        /* Left cap */
        AlphaBlend(mem_dc, dst_x, dst_y, d_cap, dst_h, sp->img.hdc_mem, src_base_x, 0, cap_w, cell_h, bf);
        /* Right cap */
        AlphaBlend(mem_dc, dst_x + dst_w - d_cap, dst_y, d_cap, dst_h, sp->img.hdc_mem, src_base_x + cell_w - cap_w, 0, cap_w, cell_h, bf);
        /* Middle */
        if (d_mid > 0) {
            AlphaBlend(mem_dc, dst_x + d_cap, dst_y, d_mid, dst_h, sp->img.hdc_mem, src_base_x + cap_w, 0, mid_w, cell_h, bf);
        }
    }
    if (label && label[0]) {
        RECT rc_lbl = { dst_x, dst_y, dst_x + dst_w, dst_y + dst_h };
        if (g_popcap_cancun_14.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_14, label, &rc_lbl, RGB(255, 234, 1), vscale, DT_CENTER | DT_VCENTER);
        } else {
            SetBkMode(mem_dc, TRANSPARENT);
            SetTextColor(mem_dc, RGB(255, 234, 1));
            DrawTextA(mem_dc, label, -1, &rc_lbl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

/* Bottom Checkbox Tray rendering (checkbutton + checkboxsliver underline + checkboxcap) */
static void draw_popcap_checkbox_tray(
    HDC mem_dc, int x, int y, const char *label, bool checked, float vscale
) {
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    /* 1. Checkbox icon (checkbutton.gif cols=2, 50x46 cell) */
    LayoutSprite *sp_cb = layout_get_cached_sprite("checkbutton", 2);
    int cb_w = (int)roundf(50.0f * vscale);
    int cb_h = (int)roundf(46.0f * vscale);
    if (sp_cb && sp_cb->loaded && sp_cb->img.hbm) {
        int cell = checked ? 1 : 0;
        int cell_src_w = sp_cb->img.width / 2;
        AlphaBlend(mem_dc, x, y, cb_w, cb_h,
                   sp_cb->img.hdc_mem, cell * cell_src_w, 0, cell_src_w, sp_cb->img.height, bf);
    }

    /* 2. Text Label */
    int txt_x = x + cb_w + (int)roundf(8.0f * vscale);
    int txt_w = 0;
    if (g_popcap_cancun_10.loaded) {
        txt_w = (int)roundf((float)popcap_font_get_line_width(&g_popcap_cancun_10, label, (int)strlen(label)) * vscale);
        RECT rc_lbl = { txt_x, y, txt_x + txt_w + 20, y + cb_h };
        popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, label, &rc_lbl, RGB(255, 234, 1), vscale, DT_LEFT | DT_VCENTER);
    } else {
        RECT rc_lbl = { txt_x, y, txt_x + 500, y + cb_h };
        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, RGB(255, 234, 1));
        DrawTextA(mem_dc, label, -1, &rc_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        txt_w = (int)roundf(300.0f * vscale);
    }

    /* 3. Checkbox Sliver underline + Cap */
    LayoutSprite *sp_slv = layout_get_cached_sprite("checkboxsliver", 1);
    LayoutSprite *sp_cap = layout_get_cached_sprite("checkboxcap", 1);
    int line_y = y + (int)roundf(35.0f * vscale);
    int line_start_x = txt_x;
    int line_end_x = txt_x + txt_w + (int)roundf(12.0f * vscale);
    int line_h = (int)roundf(14.0f * vscale);
    if (line_h <= 0) line_h = 1;

    if (sp_slv && sp_slv->loaded && sp_slv->img.hbm) {
        int slv_w = line_end_x - line_start_x;
        if (slv_w > 0) {
            AlphaBlend(mem_dc, line_start_x, line_y, slv_w, line_h,
                       sp_slv->img.hdc_mem, 0, 0, sp_slv->img.width, sp_slv->img.height, bf);
        }
    }
    if (sp_cap && sp_cap->loaded && sp_cap->img.hbm) {
        int cap_w = (int)roundf(5.0f * vscale);
        int cap_h = (int)roundf(17.0f * vscale);
        AlphaBlend(mem_dc, line_end_x, line_y - (int)roundf(1.0f * vscale), cap_w, cap_h,
                   sp_cap->img.hdc_mem, 0, 0, sp_cap->img.width, sp_cap->img.height, bf);
    }
}

/* Paint Canvas Viewport procedure */
static LayoutElement* find_element_by_tag(LayoutEditorState *st, const char *section, const char *tag) {
    if (!st || !tag) return NULL;
    for (int i = 0; i < st->element_count; ++i) {
        if (section && st->elements[i].section[0] && _stricmp(st->elements[i].section, section) != 0) continue;
        if (_stricmp(st->elements[i].tag, tag) == 0) return &st->elements[i];
    }
    return NULL;
}

static void layout_canvas_paint(LayoutEditorState *st, HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int cv_w = rc.right - rc.left;
    int cv_h = rc.bottom - rc.top;
    if (cv_w <= 0 || cv_h <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }

    ensure_popcap_fonts();

    const ThemeColors *th_cur = get_theme();
    bool is_dark = (g_current_theme == THEME_DARK);

    /* Double-buffering */
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bm = CreateCompatibleBitmap(hdc, cv_w, cv_h);
    HBITMAP old_bm = (HBITMAP)SelectObject(mem_dc, mem_bm);

    /* Fill background */
    HBRUSH bg_brush = CreateSolidBrush(is_dark ? RGB(16, 20, 27) : RGB(241, 245, 249));
    FillRect(mem_dc, &rc, bg_brush);
    DeleteObject(bg_brush);

    float vscale, zscale;
    int tx, ty, tw, th, xo, yo, cw, ch;
    calculate_viewport_transforms(st, cv_w, cv_h, &vscale, &tx, &ty, &tw, &th, &zscale, &xo, &yo, &cw, &ch);

    int cx = tx + (int)roundf((float)xo * vscale);
    int cy = ty + (int)roundf((float)yo * vscale);
    int rw = (int)roundf((float)cw * vscale);
    int rh = (int)roundf((float)ch * vscale);
    RECT rc_content = { cx, cy, cx + rw, cy + rh };
    RECT rc_target = { tx, ty, tx + tw, ty + th };

    /* 1. Draw Target Resolution Outer Area / Outscreen Pillars in Mode A */
    LayoutSprite *sp_out = layout_get_cached_sprite("outscreen", 1);
    if (sp_out && sp_out->loaded && sp_out->img.hbm) {
        int tile_w = (int)roundf((float)sp_out->img.width * vscale);
        int tile_h = (int)roundf((float)sp_out->img.height * vscale);
        if (tile_w > 0 && tile_h > 0) {
            /* Left pillar: [tx .. cx] */
            if (cx > tx) {
                for (int px = tx; px < cx; px += tile_w) {
                    int dw = (px + tile_w <= cx) ? tile_w : (cx - px);
                    for (int py = ty; py < ty + th; py += tile_h) {
                        int dh = (py + tile_h <= ty + th) ? tile_h : (ty + th - py);
                        StretchBlt(mem_dc, px, py, dw, dh,
                                   sp_out->img.hdc_mem, 0, 0,
                                   (int)((float)dw / vscale), (int)((float)dh / vscale), SRCCOPY);
                    }
                }
            }
            /* Right pillar: [cx + rw .. tx + tw] */
            if (tx + tw > cx + rw) {
                for (int px = cx + rw; px < tx + tw; px += tile_w) {
                    int dw = (px + tile_w <= tx + tw) ? tile_w : (tx + tw - px);
                    for (int py = ty; py < ty + th; py += tile_h) {
                        int dh = (py + tile_h <= ty + th) ? tile_h : (ty + th - py);
                        StretchBlt(mem_dc, px, py, dw, dh,
                                   sp_out->img.hdc_mem, 0, 0,
                                   (int)((float)dw / vscale), (int)((float)dh / vscale), SRCCOPY);
                    }
                }
            }
        }
    } else {
        HBRUSH frame_brush = CreateSolidBrush(is_dark ? RGB(10, 13, 18) : RGB(226, 232, 240));
        FillRect(mem_dc, &rc_target, frame_brush);
        DeleteObject(frame_brush);
    }

    HPEN frame_pen = CreatePen(PS_SOLID, 1, is_dark ? RGB(45, 55, 72) : RGB(203, 213, 225));
    HPEN old_pen = (HPEN)SelectObject(mem_dc, frame_pen);
    SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
    Rectangle(mem_dc, tx, ty, tx + tw, ty + th);
    SelectObject(mem_dc, old_pen);
    DeleteObject(frame_pen);

    /* 2. Draw Canonical Content Area / Screen Composites */
    SetStretchBltMode(mem_dc, HALFTONE);
    SetBrushOrgEx(mem_dc, 0, 0, NULL);

    if (st->cur_screen_idx == 1) {
        /* Screen 1: Main Menu Composite */
        /* Twilight sky at top */
        LayoutSprite *sp_sky = layout_get_cached_sprite("mmsky", 1);
        if (sp_sky && sp_sky->loaded && sp_sky->img.hbm) {
            float sky_s = vscale;
            int sky_w = rw;
            int sky_h = (int)roundf((float)sp_sky->img.height * sky_s);
            StretchBlt(mem_dc, cx, cy, sky_w, sky_h,
                       sp_sky->img.hdc_mem, 0, 0, sp_sky->img.width, sp_sky->img.height, SRCCOPY);
        }
        /* mmscreen.jpg with alpha mask _mmscreen.gif */
        LayoutSprite *sp_mm = layout_get_cached_sprite("mmscreen", 1);
        if (sp_mm && sp_mm->loaded && sp_mm->img.hbm) {
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(mem_dc, cx, cy, rw, rh,
                       sp_mm->img.hdc_mem, 0, 0, sp_mm->img.width, sp_mm->img.height, bf);
        } else if (st->has_bg_image && st->bg_image.hbm) {
            StretchBlt(mem_dc, cx, cy, rw, rh,
                       st->bg_image.hdc_mem, 0, 0, st->bg_image.width, st->bg_image.height, SRCCOPY);
        }
        /* Fixed companion: Sun Face (mmsun) at (0, 2) canonical */
        LayoutSprite *sp_mmsun = layout_get_cached_sprite("mmsun", 1);
        if (sp_mmsun && sp_mmsun->loaded && sp_mmsun->img.hbm) {
            int mmsun_px = cx + (int)roundf(0.0f * zscale * vscale);
            int mmsun_py = cy + (int)roundf(2.0f * zscale * vscale);
            int mmsun_pw = (int)roundf((float)sp_mmsun->img.width * vscale);
            int mmsun_ph = (int)roundf((float)sp_mmsun->img.height * vscale);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(mem_dc, mmsun_px, mmsun_py, mmsun_pw, mmsun_ph,
                       sp_mmsun->img.hdc_mem, 0, 0, sp_mmsun->img.width, sp_mmsun->img.height, bf);
        }
        /* Static Top Header Text */
        int wel_y = 6; /* top box = engine baseline 32 (unscaled) - (ascent 36 - padding 10) = 6 */
        int us_y = 53;
        for (int i = 0; i < st->element_count; ++i) {
            if (_stricmp(st->elements[i].tag, "WelcomeText") == 0 && (st->elements[i].flags & ATTR_HAS_Y)) {
                wel_y = st->elements[i].y;
            } else if (_stricmp(st->elements[i].tag, "UserSwitchText") == 0 && (st->elements[i].flags & ATTR_HAS_Y)) {
                us_y = st->elements[i].y;
            }
        }
        RECT rc_welcome = { cx, cy + (int)roundf((float)wel_y * vscale), cx + rw, cy + (int)roundf((float)(wel_y + 44) * vscale) };
        RECT rc_notyou  = { cx, cy + (int)roundf((float)(us_y - 12) * vscale), cx + rw, cy + (int)roundf((float)(us_y + 12) * vscale) };
        if (g_popcap_native_alien_16.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_native_alien_16, "Welcome to Zuma, DEVTESTER!", &rc_welcome, RGB(255, 255, 255), vscale, DT_CENTER);
        } else if (g_popcap_cancun_14.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_14, "Welcome to Zuma, DEVTESTER!", &rc_welcome, RGB(255, 255, 255), vscale, DT_CENTER);
        }
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "(If this is not you, click here.)", &rc_notyou, RGB(91, 55, 0), vscale, DT_CENTER);
        } else {
            SetTextColor(mem_dc, RGB(255, 255, 255));
            DrawTextA(mem_dc, "Welcome to Zuma, DEVTESTER!", -1, &rc_welcome, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(mem_dc, RGB(91, 55, 0));
            DrawTextA(mem_dc, "(If this is not you, click here.)", -1, &rc_notyou, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    } else if (st->cur_screen_idx == 2) {
        /* Screen 2: Adventure Map Composite */
        /* Tiled advsky across top down to top of pyramid (y = 180 * zscale) so ZERO black gap */
        LayoutSprite *sp_sky = layout_get_cached_sprite("advsky", 1);
        if (sp_sky && sp_sky->loaded && sp_sky->img.hbm) {
            float sky_s = vscale;
            int sky_w = (int)roundf((float)sp_sky->img.width * sky_s);
            int sky_h = (int)roundf(180.0f * zscale * vscale);
            if (sky_w > 0 && sky_h > 0) {
                for (int sx = cx; sx < cx + rw; sx += sky_w) {
                    int draw_w = (sx + sky_w <= cx + rw) ? sky_w : (cx + rw - sx);
                    StretchBlt(mem_dc, sx, cy, draw_w, sky_h,
                               sp_sky->img.hdc_mem, 0, 0, (int)((float)draw_w / sky_s), sp_sky->img.height, SRCCOPY);
                }
            }
        }
        /* Note: advback, advtitle, and infobox are dynamically drawn from st->elements in Step 3 */
    } else if (st->cur_screen_idx == 3) {
        /* Screen 3: Gauntlet Screen Composite */
        /* Top sky band */
        LayoutSprite *sp_gsky = layout_get_cached_sprite("gauntsky", 1);
        if (sp_gsky && sp_gsky->loaded && sp_gsky->img.hbm) {
            float gsky_s = vscale;
            int gsky_w = (int)roundf((float)sp_gsky->img.width * gsky_s);
            int gsky_h = (int)roundf((float)sp_gsky->img.height * gsky_s);
            if (gsky_w > 0 && gsky_h > 0) {
                for (int sx = cx; sx < cx + rw; sx += gsky_w) {
                    int draw_w = (sx + gsky_w <= cx + rw) ? gsky_w : (cx + rw - sx);
                    StretchBlt(mem_dc, sx, cy, draw_w, gsky_h,
                               sp_gsky->img.hdc_mem, 0, 0, (int)((float)draw_w / gsky_s), sp_gsky->img.height, SRCCOPY);
                }
            }
        }

        /* The '?' Locked Level Doorway Preview (BEHIND gauntscreen) */
        int df_x = 389, df_y = 309, df_w = 450, df_h = 338;
        for (int i = 0; i < st->element_count; ++i) {
            if (_stricmp(st->elements[i].tag, "DoorFrame") == 0) {
                df_x = st->elements[i].x;
                df_y = st->elements[i].y;
                if (st->elements[i].w > 0) df_w = st->elements[i].w;
                if (st->elements[i].h > 0) df_h = st->elements[i].h;
                break;
            }
        }
        int dx_box = tx + (int)roundf((float)df_x * vscale);
        int dy_box = ty + (int)roundf((float)df_y * vscale);
        int dw_box = (int)roundf((float)df_w * vscale);
        int dh_box = (int)roundf((float)df_h * vscale);

        /* Authentic vertical red gradient */
        for (int row = 0; row < dh_box; ++row) {
            float frac = (float)row / (float)(dh_box > 0 ? dh_box : 1);
            int r_val;
            if (frac < 0.5f) {
                r_val = (int)(20.0f + 240.0f * (frac * 2.0f));
            } else {
                r_val = (int)(140.0f - 100.0f * ((frac - 0.5f) * 2.0f));
            }
            if (r_val < 0) r_val = 0;
            if (r_val > 255) r_val = 255;
            HPEN gpen = CreatePen(PS_SOLID, 1, RGB(r_val, 0, 0));
            HPEN old_gpen = (HPEN)SelectObject(mem_dc, gpen);
            MoveToEx(mem_dc, dx_box, dy_box + row, NULL);
            LineTo(mem_dc, dx_box + dw_box, dy_box + row);
            SelectObject(mem_dc, old_gpen);
            DeleteObject(gpen);
        }
        RECT rc_glyph = { dx_box, dy_box + (int)roundf(20.0f * vscale), dx_box + dw_box, dy_box + dh_box - (int)roundf(20.0f * vscale) };
        if (g_popcap_native_alien_48.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_native_alien_48, "?", &rc_glyph, RGB(235, 215, 175), vscale, DT_CENTER | DT_VCENTER);
        }

        /* gauntscreen overlay with alpha mask _gauntscreen.gif */
        LayoutSprite *sp_gt_screen = layout_get_cached_sprite("gauntscreen", 1);
        if (sp_gt_screen && sp_gt_screen->loaded && sp_gt_screen->img.hbm) {
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(mem_dc, cx, cy, rw, rh,
                       sp_gt_screen->img.hdc_mem, 0, 0, sp_gt_screen->img.width, sp_gt_screen->img.height, bf);
        } else if (st->has_bg_image && st->bg_image.hbm) {
            StretchBlt(mem_dc, cx, cy, rw, rh,
                       st->bg_image.hdc_mem, 0, 0, st->bg_image.width, st->bg_image.height, SRCCOPY);
        }


        /* Difficulty Gem is rendered dynamically in Step 3 from st->elements */

        /* Dynamic door name and practice text are rendered in Step 3 from XML layout elements */
    } else if (st->cur_screen_idx == 4) {
        /* Screen 4: In-Game HUD Full Widescreen */
        /* Board background (clawc.jpg 1200x675) */
        if (st->has_bg_image && st->bg_image.hbm) {
            StretchBlt(mem_dc, tx, ty, tw, th,
                       st->bg_image.hdc_mem, 0, 0, st->bg_image.width, st->bg_image.height, SRCCOPY);
        } else {
            HBRUSH board_br = CreateSolidBrush(RGB(20, 25, 35));
            FillRect(mem_dc, &rc_target, board_br);
            DeleteObject(board_br);
        }

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

        /* Hole & Pitcover at track endpoint (1:1 native 82x81 size) */
        LayoutSprite *sp_hole = layout_get_cached_sprite("Hole", 1);
        LayoutSprite *sp_pcover = layout_get_cached_sprite("pitcover", 1);
        int hw = (int)roundf(82.0f * vscale);
        int hh = (int)roundf(81.0f * vscale);
        int hx = tx + (int)roundf(208.0f * vscale);
        int hy = ty + (int)roundf(329.0f * vscale);
        if (sp_hole && sp_hole->loaded && sp_hole->img.hbm) {
            AlphaBlend(mem_dc, hx, hy, hw, hh,
                       sp_hole->img.hdc_mem, 0, 0, sp_hole->img.width, sp_hole->img.height, bf);
        }
        if (sp_pcover && sp_pcover->loaded && sp_pcover->img.hbm) {
            int pch = sp_pcover->img.height / sp_pcover->rows;
            AlphaBlend(mem_dc, hx, hy, hw, hh,
                       sp_pcover->img.hdc_mem, 0, 0, sp_pcover->img.width, pch, bf);
        }

        /* Frog Shooter in Center Pedestal (1:1 native 108x108, rotated 45 deg matching ref) */
        LayoutSprite *sp_frog = layout_get_cached_sprite("SMALLFROGonPAD", 1);
        if (!sp_frog || !sp_frog->loaded) sp_frog = layout_get_cached_sprite("frog", 1);
        int fx = tx + tw / 2;
        int fy = ty + (int)roundf(342.0f * vscale);
        if (sp_frog && sp_frog->loaded && sp_frog->img.hbm) {
            draw_rotated_sprite(mem_dc, sp_frog, 0, 0, 108, 108, fx, fy, vscale, 45.0f);
        }

        /* Menubar spanning full width 1200px (menubar.jpg 1200x675 with outer frame) */
        LayoutSprite *sp_mb = layout_get_cached_sprite("menubar", 1);
        if (sp_mb && sp_mb->loaded && sp_mb->img.hbm) {
            AlphaBlend(mem_dc, tx, ty, tw, th,
                       sp_mb->img.hdc_mem, 0, 0, sp_mb->img.width, sp_mb->img.height, bf);
        }

        /* 2 Frog Lives Icons */
        LayoutElement *el_lives = find_element_by_tag(st, "InGameHUD", "LivesPos");
        float lx = el_lives ? (float)el_lives->x : 28.0f;
        float ly = el_lives ? (float)el_lives->y : 6.0f;
        float lsp = (el_lives && (el_lives->flags & ATTR_HAS_SPACING)) ? (float)el_lives->spacing : 26.0f;
        LayoutSprite *sp_ficon = layout_get_cached_sprite("toadlives", 1);
        if (!sp_ficon || !sp_ficon->loaded) sp_ficon = layout_get_cached_sprite("frogicon", 1);
        if (sp_ficon && sp_ficon->loaded && sp_ficon->img.hbm) {
            int fi_w = (int)roundf((float)sp_ficon->img.width * vscale);
            int fi_h = (int)roundf((float)sp_ficon->img.height * vscale);
            for (int l = 0; l < 2; ++l) {
                AlphaBlend(mem_dc, tx + (int)roundf((lx + (float)l * lsp) * vscale), ty + (int)roundf(ly * vscale),
                           fi_w, fi_h, sp_ficon->img.hdc_mem, 0, 0, sp_ficon->img.width, sp_ficon->img.height, bf);
            }
        }

        /* Stage text "RAB 1" & Score text "0" reading dynamically from layout elements */
        LayoutElement *el_stg = find_element_by_tag(st, "InGameHUD", "StageName");
        int stg_cx = el_stg ? el_stg->centerX : 290;
        int stg_y = el_stg ? el_stg->y : 8;
        RECT rc_stg = { tx + (int)roundf((float)(stg_cx - 55) * vscale), ty + (int)roundf((float)stg_y * vscale),
                        tx + (int)roundf((float)(stg_cx + 55) * vscale), ty + (int)roundf((float)(stg_y + 28) * vscale) };

        LayoutElement *el_scr = find_element_by_tag(st, "InGameHUD", "ScoreText");
        int scr_rx = el_scr ? el_scr->x : 640;
        int scr_y = el_scr ? el_scr->y : 8;
        RECT rc_scr = { tx + (int)roundf((float)(scr_rx - 110) * vscale), ty + (int)roundf((float)scr_y * vscale),
                        tx + (int)roundf((float)scr_rx * vscale), ty + (int)roundf((float)(scr_y + 28) * vscale) };

        if (g_popcap_cancun_12.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_12, "RAB 1", &rc_stg, RGB(255, 240, 0), vscale, DT_CENTER);
        } else if (g_popcap_cancun_14.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_14, "RAB 1", &rc_stg, RGB(255, 240, 0), vscale, DT_CENTER);
        }
        if (g_popcap_cancun_13.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_13, "0", &rc_scr, RGB(255, 240, 0), vscale, DT_RIGHT);
        } else if (g_popcap_cancun_14.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_14, "0", &rc_scr, RGB(255, 240, 0), vscale, DT_RIGHT);
        }

        /* Note: Zuma Bar gauge and Menubutton are rendered dynamically in Step 3 via st->elements */

        /* Level intro banner at bottom right: RABBIT 1 / COMPLEX TALON */
        RECT rc_lb_title = { tx + tw - (int)roundf(380.0f * vscale), ty + th - (int)roundf(110.0f * vscale),
                             tx + tw - (int)roundf(40.0f * vscale),  ty + th - (int)roundf(50.0f * vscale) };
        RECT rc_lb_sub   = { tx + tw - (int)roundf(380.0f * vscale), ty + th - (int)roundf(56.0f * vscale),
                             tx + tw - (int)roundf(42.0f * vscale),  ty + th - (int)roundf(24.0f * vscale) };
        if (g_popcap_native_alien_48.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_native_alien_48, "RABBIT 1", &rc_lb_title, RGB(255, 255, 255), vscale, DT_RIGHT);
        } else if (g_popcap_native_alien_16.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_native_alien_16, "RABBIT 1", &rc_lb_title, RGB(255, 140, 20), vscale, DT_RIGHT);
        }
        if (g_popcap_native_alien_16.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_native_alien_16, "COMPLEX TALON", &rc_lb_sub, RGB(255, 255, 255), vscale, DT_RIGHT);
        } else if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "COMPLEX TALON", &rc_lb_sub, RGB(255, 234, 1), vscale, DT_RIGHT);
        }
    } else if (st->cur_screen_idx == 8) {
        /* Screen 8: HelpScreen / Instructions Authentic Stone Tablet Dialog */
        LayoutSprite *sp_back = layout_get_cached_sprite("helpback", 1);
        if (sp_back && sp_back->loaded && sp_back->img.hbm) {
            float hb_s = vscale;
            int hb_w = (int)roundf((float)sp_back->img.width * hb_s);
            int hb_h = (int)roundf((float)sp_back->img.height * hb_s);
            int hbx = cx + (rw - hb_w) / 2;
            int hby = cy + (rh - hb_h) / 2;
            StretchBlt(mem_dc, hbx, hby, hb_w, hb_h,
                       sp_back->img.hdc_mem, 0, 0, sp_back->img.width, sp_back->img.height, SRCCOPY);
        } else {
            HBRUSH br_stone = CreateSolidBrush(RGB(40, 50, 35));
            RECT rc_tab = { cx, cy, cx + rw, cy + rh };
            FillRect(mem_dc, &rc_tab, br_stone);
            DeleteObject(br_stone);
        }

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        LayoutSprite *sp_front = layout_get_cached_sprite("helpfront", 1);
        if (sp_front && sp_front->loaded && sp_front->img.hbm) {
            AlphaBlend(mem_dc, cx, cy, rw, rh,
                       sp_front->img.hdc_mem, 0, 0, sp_front->img.width, sp_front->img.height, bf);
        }

        LayoutSprite *sp_frog = layout_get_cached_sprite("SMALLFROGonPAD", 1);
        if (!sp_frog || !sp_frog->loaded) sp_frog = layout_get_cached_sprite("frog", 1);

        /* Compartment 1: Aiming Frog + Ball + Text */
        RECT rc_c1_t1 = { cx + (int)roundf(25.0f * vscale), cy + (int)roundf(90.0f * vscale),
                          cx + (int)roundf(435.0f * vscale), cy + (int)roundf(130.0f * vscale) };
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10,
                "SHOOT BALLS BY CLICKING ON SCREEN WHERE YOU WANT THE\nBALL TO GO.",
                &rc_c1_t1, RGB(255, 234, 1), vscale, DT_CENTER);
        }
        int c1_fx = cx + (int)roundf(74.0f * vscale);
        int c1_fy = cy + (int)roundf(154.0f * vscale);
        if (sp_frog && sp_frog->loaded && sp_frog->img.hbm) {
            draw_rotated_sprite(mem_dc, sp_frog, 0, 0, 108, 108,
                                c1_fx, c1_fy, vscale, 125.0f);
        }
        /* Green ball in mouth (1:1 native 32x32) */
        LayoutSprite *sp_bgrn = layout_get_cached_sprite("baBallGreen", 1);
        int bsz = (int)roundf(32.0f * vscale);
        if (sp_bgrn && sp_bgrn->loaded && sp_bgrn->img.hbm) {
            int cell_h = sp_bgrn->img.height / sp_bgrn->rows;
            AlphaBlend(mem_dc, c1_fx + (int)roundf(28.0f * vscale), c1_fy + (int)roundf(20.0f * vscale),
                       bsz, bsz, sp_bgrn->img.hdc_mem, 0, 0, sp_bgrn->img.width, cell_h, bf);
        }
        /* Green gem on back */
        if (sp_bgrn && sp_bgrn->loaded && sp_bgrn->img.hbm) {
            int gem_sz = (int)roundf(14.0f * vscale);
            int cell_h = sp_bgrn->img.height / sp_bgrn->rows;
            AlphaBlend(mem_dc, c1_fx - (int)roundf(24.0f * vscale), c1_fy - (int)roundf(18.0f * vscale),
                       gem_sz, gem_sz, sp_bgrn->img.hdc_mem, 0, 0, sp_bgrn->img.width, cell_h, bf);
        }
        RECT rc_c1_t2 = { cx + (int)roundf(25.0f * vscale), cy + (int)roundf(260.0f * vscale),
                          cx + (int)roundf(435.0f * vscale), cy + (int)roundf(300.0f * vscale) };
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10,
                "THE GEM IN THE BACK OF THE FROG SHOWS THE COLOR OF THE\nNEXT BALL.",
                &rc_c1_t2, RGB(255, 185, 45), vscale, DT_CENTER);
        }

        /* Compartment 2: Clear Balls + Chain Reactions */
        RECT rc_c2_t1 = { cx + (int)roundf(465.0f * vscale), cy + (int)roundf(90.0f * vscale),
                          cx + (int)roundf(875.0f * vscale), cy + (int)roundf(130.0f * vscale) };
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10,
                "CLEAR BALLS BY GETTING THREE OR MORE OF THE SAME\nCOLOR IN A ROW.",
                &rc_c2_t1, RGB(255, 234, 1), vscale, DT_CENTER);
        }
        const char *c2_balls[] = { "baBallYellow", "baBallYellow", "baBallBlue", "baBallBlue", "baBallBlue", "baBallRed" };
        for (int b = 0; b < 6; ++b) {
            LayoutSprite *sp_b = layout_get_cached_sprite(c2_balls[b], 1);
            if (sp_b && sp_b->loaded && sp_b->img.hbm) {
                int bx = cx + (int)roundf((513.0f + b * 28.0f) * vscale);
                int by = cy + (int)roundf(170.0f * vscale);
                int cell_h = sp_b->img.height / sp_b->rows;
                AlphaBlend(mem_dc, bx, by, bsz, bsz, sp_b->img.hdc_mem, 0, 0, sp_b->img.width, cell_h, bf);
            }
        }
        RECT rc_c2_t2 = { cx + (int)roundf(465.0f * vscale), cy + (int)roundf(260.0f * vscale),
                          cx + (int)roundf(875.0f * vscale), cy + (int)roundf(300.0f * vscale) };
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10,
                "LIKE COLORED BALLS ATTRACT AND CAN CREATE CHAIN\nREACTIONS!",
                &rc_c2_t2, RGB(255, 185, 45), vscale, DT_CENTER);
        }

        /* Compartment 3: Sun Skull (Hole.gif 88x88 + pitcover frame 0) */
        LayoutSprite *sp_gh = layout_get_cached_sprite("Hole", 1);
        if (!sp_gh || !sp_gh->loaded) sp_gh = layout_get_cached_sprite("godhead", 1);
        int gh_sz = (int)roundf(88.0f * vscale);
        int gh_x = cx + (int)roundf(150.0f * vscale) - gh_sz / 2;
        int gh_y = cy + (int)roundf(432.0f * vscale) - gh_sz / 2;
        if (sp_gh && sp_gh->loaded && sp_gh->img.hbm) {
            AlphaBlend(mem_dc, gh_x, gh_y, gh_sz, gh_sz,
                       sp_gh->img.hdc_mem, 0, 0, sp_gh->img.width, sp_gh->img.height, bf);
        }
        LayoutSprite *sp_pcover = layout_get_cached_sprite("pitcover", 1);
        if (sp_pcover && sp_pcover->loaded && sp_pcover->img.hbm) {
            int pch = sp_pcover->img.height / (sp_pcover->rows > 0 ? sp_pcover->rows : 12);
            AlphaBlend(mem_dc, gh_x, gh_y, gh_sz, gh_sz,
                       sp_pcover->img.hdc_mem, 0, 0, sp_pcover->img.width, pch, bf);
        }
        RECT rc_c3_t = { cx + (int)roundf(25.0f * vscale), cy + (int)roundf(540.0f * vscale),
                         cx + (int)roundf(325.0f * vscale), cy + (int)roundf(580.0f * vscale) };
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10,
                "DON'T LET THE BALLS REACH THE\nSKULL!",
                &rc_c3_t, RGB(255, 234, 1), vscale, DT_CENTER);
        }

        /* Compartment 4: Gold Coin (coin.gif 36x36 frame 0) */
        LayoutSprite *sp_coin = layout_get_cached_sprite("coin", 1);
        if (sp_coin && sp_coin->loaded && sp_coin->img.hbm) {
            int c_sz = (int)roundf(36.0f * vscale);
            int c_x = cx + (int)roundf(428.0f * vscale) - c_sz / 2;
            int c_y = cy + (int)roundf(432.0f * vscale) - c_sz / 2;
            AlphaBlend(mem_dc, c_x, c_y, c_sz, c_sz,
                       sp_coin->img.hdc_mem, 0, 0, 36, 36, bf);
        }
        RECT rc_c4_t = { cx + (int)roundf(335.0f * vscale), cy + (int)roundf(540.0f * vscale),
                         cx + (int)roundf(535.0f * vscale), cy + (int)roundf(570.0f * vscale) };
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10,
                "SHOOT COINS FOR EXTRA POINTS!",
                &rc_c4_t, RGB(255, 234, 1), vscale, DT_CENTER);
        }

        /* Compartment 5: Right-Click Ball Swap */
        int c5_fx = cx + (int)roundf(713.0f * vscale);
        int c5_fy = cy + (int)roundf(456.0f * vscale);
        if (sp_frog && sp_frog->loaded && sp_frog->img.hbm) {
            draw_rotated_sprite(mem_dc, sp_frog, 0, 0, 108, 108,
                                c5_fx, c5_fy, vscale, 180.0f);
        }
        /* Green ball in mouth */
        if (sp_bgrn && sp_bgrn->loaded && sp_bgrn->img.hbm) {
            int cell_h = sp_bgrn->img.height / sp_bgrn->rows;
            AlphaBlend(mem_dc, c5_fx - bsz / 2, c5_fy + (int)roundf(38.0f * vscale) - bsz / 2,
                       bsz, bsz, sp_bgrn->img.hdc_mem, 0, 0, sp_bgrn->img.width, cell_h, bf);
        }
        /* Mouse icon */
        LayoutSprite *sp_mouser = layout_get_cached_sprite("tut_mouse_r", 2);
        if (sp_mouser && sp_mouser->loaded && sp_mouser->img.hbm) {
            int mw = (int)roundf(20.0f * vscale);
            int mh = (int)roundf(29.0f * vscale);
            int m_src_w = sp_mouser->img.width / 2;
            AlphaBlend(mem_dc, cx + (int)roundf(834.0f * vscale), cy + (int)roundf(445.0f * vscale),
                       mw, mh, sp_mouser->img.hdc_mem, 0, 0, m_src_w, sp_mouser->img.height, bf);
        }
        RECT rc_c5_t = { cx + (int)roundf(550.0f * vscale), cy + (int)roundf(540.0f * vscale),
                         cx + (int)roundf(875.0f * vscale), cy + (int)roundf(580.0f * vscale) };
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10,
                "RIGHT MOUSE BUTTON WILL SWAP\nTHE BALLS.",
                &rc_c5_t, RGB(255, 234, 1), vscale, DT_CENTER);
        }

        /* Checkbox Tray and OK Button at bottom matching reference */
        int btn_w = (int)roundf(180.0f * vscale);
        int btn_h = (int)roundf(44.0f * vscale);
        int btn_x = cx + rw - btn_w - (int)roundf(30.0f * vscale);
        int btn_y = cy + (int)roundf(612.0f * vscale);
        int cb_x = cx + (int)roundf(14.0f * vscale);
        int cb_y = cy + (int)roundf(606.0f * vscale);

        draw_popcap_checkbox_tray(mem_dc, cb_x, cb_y,
                                  "DON'T SHOW THIS SCREEN AT STARTUP.", true, vscale);
        draw_popcap_dialogbutton(mem_dc, btn_x, btn_y, btn_w, btn_h, "OK", 0, vscale);
    } else if (st->cur_screen_idx == 11) {
        /* Screen 11: DevHelpScreen Authentic Stone Tablet Dialog */
        LayoutSprite *sp_back = layout_get_cached_sprite("helpback_dev", 1);
        if (!sp_back || !sp_back->loaded) sp_back = layout_get_cached_sprite("helpback", 1);
        if (sp_back && sp_back->loaded && sp_back->img.hbm) {
            float hb_s = vscale;
            int hb_w = (int)roundf((float)sp_back->img.width * hb_s);
            int hb_h = (int)roundf((float)sp_back->img.height * hb_s);
            int hbx = cx + (rw - hb_w) / 2;
            int hby = cy + (rh - hb_h) / 2;
            StretchBlt(mem_dc, hbx, hby, hb_w, hb_h,
                       sp_back->img.hdc_mem, 0, 0, sp_back->img.width, sp_back->img.height, SRCCOPY);
        } else {
            HBRUSH br_stone = CreateSolidBrush(RGB(40, 50, 35));
            RECT rc_tab = { cx, cy, cx + rw, cy + rh };
            FillRect(mem_dc, &rc_tab, br_stone);
            DeleteObject(br_stone);
        }

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        LayoutSprite *sp_front = layout_get_cached_sprite("helpfront_dev", 1);
        if (!sp_front || !sp_front->loaded) sp_front = layout_get_cached_sprite("helpfront", 1);
        if (sp_front && sp_front->loaded && sp_front->img.hbm) {
            AlphaBlend(mem_dc, cx, cy, rw, rh,
                       sp_front->img.hdc_mem, 0, 0, sp_front->img.width, sp_front->img.height, bf);
        }

        /* Header Title "DEVELOPER MODE" */
        RECT rc_devtitle = { cx, cy + (int)roundf(36.0f * vscale), cx + rw, cy + (int)roundf(72.0f * vscale) };
        draw_sandy_glow_text(mem_dc, &g_popcap_native_alien_16, "DEVELOPER MODE", &rc_devtitle, vscale);

        /* Header Subtitle */
        RECT rc_devsub = { cx, cy + (int)roundf(74.0f * vscale), cx + rw, cy + (int)roundf(98.0f * vscale) };
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "SPECIAL TESTING PROFILE - UNRESTRICTED GAME ACCESS", &rc_devsub, RGB(255, 234, 1), vscale, DT_CENTER);
        }

        /* Left Column: ADVENTURE MODE */
        RECT rc_c1_hdr = { cx + (int)roundf(75.0f * vscale), cy + (int)roundf(106.0f * vscale),
                           cx + (int)roundf(445.0f * vscale), cy + (int)roundf(130.0f * vscale) };
        if (g_popcap_cancun_14.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_14, "ADVENTURE MODE", &rc_c1_hdr, RGB(255, 234, 1), vscale, DT_CENTER);
        }

        /* advtemple5_tut.jpg (190x75 1:1 native size) */
        LayoutSprite *sp_t5 = layout_get_cached_sprite("advtemple5_tut", 1);
        if (sp_t5 && sp_t5->loaded && sp_t5->img.hbm) {
            int tp_w = (int)roundf(190.0f * vscale);
            int tp_h = (int)roundf(75.0f * vscale);
            int tp_x = cx + (int)roundf(260.0f * vscale) - tp_w / 2;
            int tp_y = cy + (int)roundf(140.0f * vscale);
            AlphaBlend(mem_dc, tp_x, tp_y, tp_w, tp_h,
                       sp_t5->img.hdc_mem, 0, 0, sp_t5->img.width, sp_t5->img.height, bf);
            HPEN frame_p = CreatePen(PS_SOLID, 1, RGB(180, 150, 80));
            HPEN old_fp = (HPEN)SelectObject(mem_dc, frame_p);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, tp_x - 1, tp_y - 1, tp_x + tp_w + 1, tp_y + tp_h + 1);
            SelectObject(mem_dc, old_fp);
            DeleteObject(frame_p);
        }

        /* Left Column Bullet Points */
        if (g_popcap_cancun_10.loaded) {
            RECT rc_t;
            rc_t = (RECT){ cx + (int)roundf(120.0f * vscale), cy + (int)roundf(236.0f * vscale), cx + (int)roundf(460.0f * vscale), cy + (int)roundf(256.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "* ALL 13 STAGES UNLOCKED FROM START", &rc_t, RGB(255, 255, 255), vscale, DT_LEFT);

            rc_t = (RECT){ cx + (int)roundf(120.0f * vscale), cy + (int)roundf(264.0f * vscale), cx + (int)roundf(460.0f * vscale), cy + (int)roundf(284.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "* DIRECT TEMPLE 5 ACCESS TO SECRET", &rc_t, RGB(255, 255, 255), vscale, DT_LEFT);

            rc_t = (RECT){ cx + (int)roundf(138.0f * vscale), cy + (int)roundf(290.0f * vscale), cx + (int)roundf(460.0f * vscale), cy + (int)roundf(310.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "STAGE 13 (\"THE VOID\")", &rc_t, RGB(0, 229, 255), vscale, DT_LEFT);

            /* Middle Mouse Button Section */
            RECT rc_mmb_hdr = { cx + (int)roundf(75.0f * vscale), cy + (int)roundf(340.0f * vscale),
                                cx + (int)roundf(445.0f * vscale), cy + (int)roundf(364.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_14, "MIDDLE MOUSE BUTTON", &rc_mmb_hdr, RGB(0, 229, 255), vscale, DT_CENTER);

            rc_t = (RECT){ cx + (int)roundf(120.0f * vscale), cy + (int)roundf(380.0f * vscale), cx + (int)roundf(460.0f * vscale), cy + (int)roundf(418.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "JUST PLAY NORMALLY, OR CLICK MIDDLE MOUSE\nBUTTON AT ANY TIME TO END IT ALL AND ADVANCE!", &rc_t, RGB(255, 255, 255), vscale, DT_LEFT);

            rc_t = (RECT){ cx + (int)roundf(120.0f * vscale), cy + (int)roundf(432.0f * vscale), cx + (int)roundf(460.0f * vscale), cy + (int)roundf(452.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "* INSTANTLY AWARDS FULL TARGET SCORE", &rc_t, RGB(255, 215, 0), vscale, DT_LEFT);

            rc_t = (RECT){ cx + (int)roundf(120.0f * vscale), cy + (int)roundf(458.0f * vscale), cx + (int)roundf(460.0f * vscale), cy + (int)roundf(478.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "* TRIGGERS ZUMA DESTROYALL & CLEARS ALL BALLS", &rc_t, RGB(255, 215, 0), vscale, DT_LEFT);

            rc_t = (RECT){ cx + (int)roundf(120.0f * vscale), cy + (int)roundf(484.0f * vscale), cx + (int)roundf(460.0f * vscale), cy + (int)roundf(504.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "* AUTOMATICALLY COMPLETES THE CURRENT STAGE", &rc_t, RGB(255, 215, 0), vscale, DT_LEFT);
        }

        /* Right Column: GAUNTLET MODE */
        RECT rc_c2_hdr = { cx + (int)roundf(475.0f * vscale), cy + (int)roundf(106.0f * vscale),
                           cx + (int)roundf(845.0f * vscale), cy + (int)roundf(130.0f * vscale) };
        if (g_popcap_cancun_14.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_14, "GAUNTLET MODE", &rc_c2_hdr, RGB(255, 234, 1), vscale, DT_CENTER);
        }

        if (g_popcap_cancun_10.loaded) {
            RECT rc_t;
            rc_t = (RECT){ cx + (int)roundf(520.0f * vscale), cy + (int)roundf(146.0f * vscale), cx + (int)roundf(860.0f * vscale), cy + (int)roundf(166.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "* ALL LEVELS UNLOCKED IN GAUNTLET", &rc_t, RGB(255, 255, 255), vscale, DT_LEFT);

            rc_t = (RECT){ cx + (int)roundf(520.0f * vscale), cy + (int)roundf(176.0f * vscale), cx + (int)roundf(860.0f * vscale), cy + (int)roundf(196.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "* PRACTICE & SURVIVAL MODES ACTIVE", &rc_t, RGB(255, 255, 255), vscale, DT_LEFT);

            rc_t = (RECT){ cx + (int)roundf(520.0f * vscale), cy + (int)roundf(206.0f * vscale), cx + (int)roundf(860.0f * vscale), cy + (int)roundf(226.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "* FULL SUN GOD RANK ACCESS UNLOCKED", &rc_t, RGB(255, 215, 0), vscale, DT_LEFT);

            rc_t = (RECT){ cx + (int)roundf(520.0f * vscale), cy + (int)roundf(236.0f * vscale), cx + (int)roundf(860.0f * vscale), cy + (int)roundf(256.0f * vscale) };
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "* JUMP TO ANY DIFFICULTY TIER OR MAP", &rc_t, RGB(255, 215, 0), vscale, DT_LEFT);
        }

        /* 5 Exploding Fireballs + "+100" */
        LayoutSprite *sp_exp = layout_get_cached_sprite("explosion", 1);
        if (!sp_exp || !sp_exp->loaded) sp_exp = layout_get_cached_sprite("badotz", 1);
        int exp_sz = (int)roundf(44.0f * vscale);
        for (int e = 0; e < 5; ++e) {
            int ex = cx + (int)roundf((545.0f + e * 42.0f) * vscale);
            int ey = cy + (int)roundf(416.0f * vscale);
            if (sp_exp && sp_exp->loaded && sp_exp->img.hbm) {
                int cell_h = sp_exp->img.height / sp_exp->rows;
                AlphaBlend(mem_dc, ex, ey, exp_sz, exp_sz,
                           sp_exp->img.hdc_mem, 0, 0, sp_exp->img.width, cell_h, bf);
            }
        }
        RECT rc_scores = { cx + (int)roundf(540.0f * vscale), cy + (int)roundf(398.0f * vscale),
                           cx + (int)roundf(760.0f * vscale), cy + (int)roundf(418.0f * vscale) };
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "+100 +100 +100 +100 +100", &rc_scores, RGB(255, 234, 1), vscale, DT_CENTER);
        }

        /* CLICK! label & middle mouse icon */
        RECT rc_click = { cx + (int)roundf(540.0f * vscale), cy + (int)roundf(472.0f * vscale),
                          cx + (int)roundf(640.0f * vscale), cy + (int)roundf(502.0f * vscale) };
        if (g_popcap_cancun_10.loaded) {
            popcap_font_draw_text(mem_dc, &g_popcap_cancun_10, "CLICK!", &rc_click, RGB(255, 255, 255), vscale, DT_RIGHT | DT_VCENTER);
        }
        LayoutSprite *sp_mousem = layout_get_cached_sprite("tut_mouse_m", 2);
        if (sp_mousem && sp_mousem->loaded && sp_mousem->img.hbm) {
            int mw = (int)roundf(20.0f * vscale);
            int mh = (int)roundf(29.0f * vscale);
            int m_src_w = sp_mousem->img.width / 2;
            AlphaBlend(mem_dc, cx + (int)roundf(655.0f * vscale), cy + (int)roundf(472.0f * vscale),
                       mw, mh, sp_mousem->img.hdc_mem, 0, 0, m_src_w, sp_mousem->img.height, bf);
        }

        /* Checkbox Tray and OK Button at bottom */
        int btn_w = (int)roundf(180.0f * vscale);
        int btn_h = (int)roundf(44.0f * vscale);
        int btn_x = cx + rw - btn_w - (int)roundf(30.0f * vscale);
        int btn_y = cy + (int)roundf(612.0f * vscale);
        int cb_x = cx + (int)roundf(14.0f * vscale);
        int cb_y = cy + (int)roundf(606.0f * vscale);

        draw_popcap_checkbox_tray(mem_dc, cb_x, cb_y,
                                  "DON'T SHOW THIS SCREEN AT STARTUP.", true, vscale);
        draw_popcap_dialogbutton(mem_dc, btn_x, btn_y, btn_w, btn_h, "OK", 0, vscale);
    } else if (st->cur_screen_idx == 5 || st->cur_screen_idx == 6) {
        /* Screens 5 & 6: StageComplete & TempleCleared Full Widescreen */
        if (st->has_bg_image && st->bg_image.hbm) {
            StretchBlt(mem_dc, tx, ty, tw, th,
                       st->bg_image.hdc_mem, 0, 0, st->bg_image.width, st->bg_image.height, SRCCOPY);
        }
        HBRUSH dark_br = CreateSolidBrush(RGB(10, 15, 25));
        HBITMAP dark_bm = CreateCompatibleBitmap(mem_dc, tw, th);
        HDC dark_dc = CreateCompatibleDC(mem_dc);
        HBITMAP old_dbm = (HBITMAP)SelectObject(dark_dc, dark_bm);
        RECT rcd = { 0, 0, tw, th };
        FillRect(dark_dc, &rcd, dark_br);
        BLENDFUNCTION bf_dark = { AC_SRC_OVER, 0, 160, 0 };
        AlphaBlend(mem_dc, tx, ty, tw, th, dark_dc, 0, 0, tw, th, bf_dark);
        SelectObject(dark_dc, old_dbm);
        DeleteDC(dark_dc);
        DeleteObject(dark_bm);
        DeleteObject(dark_br);
    } else {
        /* Screen 0 (LoadingScreen) and default screens */
        if (st->has_bg_image && st->bg_image.hbm) {
            StretchBlt(mem_dc, cx, cy, rw, rh,
                       st->bg_image.hdc_mem, 0, 0, st->bg_image.width, st->bg_image.height, SRCCOPY);
        }
    }

    /* Safe zone outline */
    HPEN safe_pen = CreatePen(PS_DOT, 1, RGB(56, 189, 248));
    old_pen = (HPEN)SelectObject(mem_dc, safe_pen);
    SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
    Rectangle(mem_dc, cx, cy, cx + rw, cy + rh);
    SelectObject(mem_dc, old_pen);
    DeleteObject(safe_pen);

    /* Update element bounding boxes */
    update_all_elements_canvas_rects(st, cv_w, cv_h);

    /* 3. Render Elements sorted by PopCap engine Z-Order */
    HFONT font_ui = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT font_bold = g_font_primary_bold ? g_font_primary_bold : font_ui;
    HFONT font_mono = g_font_mono_bold ? g_font_mono_bold : font_ui;
    HFONT old_font = (HFONT)SelectObject(mem_dc, font_ui);
    SetBkMode(mem_dc, TRANSPARENT);

    RenderEntry entries[MAX_LAYOUT_ELEMENTS];
    int entry_count = 0;
    for (int i = 0; i < st->element_count; ++i) {
        LayoutElement *el = &st->elements[i];
        if (!is_element_visible_in_editor(el, st)) continue;
        entries[entry_count].el = el;
        entries[entry_count].z_order = get_element_z_order(el, st->cur_screen_idx);
        entries[entry_count].original_index = i;
        entry_count++;
    }
    qsort(entries, entry_count, sizeof(RenderEntry), compare_render_entries);

    for (int i = 0; i < entry_count; ++i) {
        LayoutElement *el = entries[i].el;
        int el_z = entries[i].z_order;

        bool is_hovered = (st->hovered_elem == el);
        bool is_selected = el->selected;
        bool is_held = (st->held_elem == el);

        int cols = 1, rows = 1, cell = 0;
        const char *sp_name = get_element_sprite_name(el, &cols, &rows, &cell);
        LayoutSprite *sp = sp_name ? layout_get_cached_sprite(sp_name, cols) : NULL;
        if (sp) {
            if (cols <= 1 && sp->cols > 1) cols = sp->cols;
            if (rows <= 1 && sp->rows > 1) rows = sp->rows;
        }

        float effective_scale = vscale;

        if (el->flags & ATTR_IS_DOOR_PAIR) {
            render_layout_door_pair(mem_dc, el, sp, cols, rows, cell, is_selected, is_hovered, is_held, st->held_handle, font_bold, font_ui, is_dark);
        } else if (sp && sp->loaded && sp->img.hbm) {
            render_layout_sprite_element(mem_dc, el, sp, cols, rows, cell, is_selected, is_hovered, is_held, font_bold);
        } else if (is_text_element(el)) {
            render_layout_text_element(mem_dc, el, st, font_bold, font_ui, is_dark, is_selected, is_hovered, effective_scale);
        } else if (is_region_element(el)) {
            render_layout_region_element(mem_dc, el, font_bold, font_ui, is_dark, is_selected, is_hovered);
        } else {
            render_layout_point_element(mem_dc, el, font_bold, font_ui, is_dark, is_selected, is_hovered, effective_scale);
        }
    }

    /* 4. Draw Marquee Selection Box (Ctrl + Drag) */
    if (st->marquee_active) {
        RECT mq;
        mq.left   = (st->marquee_start.x < st->marquee_curr.x) ? st->marquee_start.x : st->marquee_curr.x;
        mq.right  = (st->marquee_start.x > st->marquee_curr.x) ? st->marquee_start.x : st->marquee_curr.x;
        mq.top    = (st->marquee_start.y < st->marquee_curr.y) ? st->marquee_start.y : st->marquee_curr.y;
        mq.bottom = (st->marquee_start.y > st->marquee_curr.y) ? st->marquee_start.y : st->marquee_curr.y;

        HPEN mq_pen = CreatePen(PS_DOT, 1, RGB(56, 189, 248));
        old_pen = (HPEN)SelectObject(mem_dc, mq_pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        Rectangle(mem_dc, mq.left, mq.top, mq.right, mq.bottom);
        SelectObject(mem_dc, old_pen);
        DeleteObject(mq_pen);
    }

    /* 5. Draw Interactive Hover Badge / Tooltip */
    if (st->hovered_elem && !st->marquee_active) {
        LayoutElement *hel = st->hovered_elem;
        char badge_txt[128];

        int can_x = (hel->flags & ATTR_IS_DOOR_PAIR) ? ((st->hovered_handle == 1) ? hel->frogX : hel->doorX) :
                    ((hel->flags & ATTR_HAS_CENTER_X) ? hel->centerX : hel->x);
        int can_y = (hel->flags & ATTR_IS_DOOR_PAIR) ? ((st->hovered_handle == 1) ? hel->frogY : hel->doorY) :
                    ((hel->flags & ATTR_HAS_CENTER_Y) ? hel->centerY : hel->y);

        if (hel->flags & ATTR_IS_DOOR_PAIR) {
            if (st->hovered_handle == 1) {
                snprintf(badge_txt, sizeof(badge_txt), "%s (Frog Waypoint) [id: %s] | X: %d, Y: %d", hel->tag, hel->id, can_x, can_y);
            } else {
                snprintf(badge_txt, sizeof(badge_txt), "%s (Door Arch) [id: %s] | X: %d, Y: %d", hel->tag, hel->id, can_x, can_y);
            }
        } else {
            snprintf(badge_txt, sizeof(badge_txt), "%s | Canonical: %d, %d", hel->tag, can_x, can_y);
        }

        SelectObject(mem_dc, font_bold);
        SIZE txt_sz;
        GetTextExtentPoint32A(mem_dc, badge_txt, (int)strlen(badge_txt), &txt_sz);

        int bx = st->hover_canvas_x + 14;
        int by = st->hover_canvas_y - 28;
        if (bx + txt_sz.cx + 16 > cv_w) bx = cv_w - txt_sz.cx - 16;
        if (by < 8) by = st->hover_canvas_y + 16;

        RECT rc_badge = { bx, by, bx + txt_sz.cx + 14, by + txt_sz.cy + 8 };
        HBRUSH bd_brush = CreateSolidBrush(is_dark ? RGB(15, 23, 42) : RGB(255, 255, 255));
        HPEN bd_pen = CreatePen(PS_SOLID, 1, RGB(56, 189, 248));
        FillRect(mem_dc, &rc_badge, bd_brush);
        old_pen = (HPEN)SelectObject(mem_dc, bd_pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        Rectangle(mem_dc, rc_badge.left, rc_badge.top, rc_badge.right, rc_badge.bottom);
        SelectObject(mem_dc, old_pen);
        DeleteObject(bd_brush);
        DeleteObject(bd_pen);

        SetTextColor(mem_dc, is_dark ? RGB(56, 189, 248) : RGB(2, 132, 199));
        RECT rc_btxt = rc_badge;
        DrawTextA(mem_dc, badge_txt, -1, &rc_btxt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 6. Viewport Resolution & Status Bar Overlay (Bottom Left) */
    char status_str[128];
    snprintf(status_str, sizeof(status_str), "Resolution: %d x %d (Locked in Zuma.exe)  |  Screen: %s",
             st->target_w, st->target_h, g_screens[st->cur_screen_idx].name);
    SetTextColor(mem_dc, is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139));
    SelectObject(mem_dc, font_mono);
    TextOutA(mem_dc, 12, cv_h - 22, status_str, (int)strlen(status_str));

    SelectObject(mem_dc, old_font);

    /* Blit to screen */
    BitBlt(hdc, 0, 0, cv_w, cv_h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_bm);
    DeleteObject(mem_bm);
    DeleteDC(mem_dc);

    EndPaint(hwnd, &ps);
}

/* Sync Property Inspector values from the currently selected element */
static void layout_editor_sync_inspector_from_selection(LayoutEditorState *st) {
    if (!st || st->syncing_inspector) return;
    st->syncing_inspector = true;

    LayoutElement *sel = NULL;
    int sel_count = 0;
    for (int i = 0; i < st->element_count; ++i) {
        if (st->elements[i].selected && is_element_visible_in_editor(&st->elements[i], st)) {
            sel_count++;
            if (!sel) sel = &st->elements[i];
        }
    }

    if (!sel || sel_count == 0) {
        SetWindowTextA(st->hwnd_edit_tag, "(No selection)");
        SetWindowTextA(st->hwnd_edit_section, "");
        SetWindowTextA(st->hwnd_edit_x, "");
        SetWindowTextA(st->hwnd_edit_y, "");
        SetWindowTextA(st->hwnd_edit_frog_x, "");
        SetWindowTextA(st->hwnd_edit_frog_y, "");
        SetWindowTextA(st->hwnd_edit_w, "");
        SetWindowTextA(st->hwnd_edit_h, "");
        SetWindowTextA(st->hwnd_edit_center_x, "");
        SetWindowTextA(st->hwnd_edit_center_y, "");
        SetWindowTextA(st->hwnd_edit_offset_x, "");
        SetWindowTextA(st->hwnd_edit_offset_y, "");
        SetWindowTextA(st->hwnd_edit_align, "");
        SetWindowTextA(st->hwnd_edit_spacing, "");
        SetWindowTextA(st->hwnd_lbl_coords, "Click or Drag an element to edit coordinates.");
        st->syncing_inspector = false;
        return;
    }

    char buf[64];
    if (sel->flags & ATTR_HAS_ID) {
        snprintf(buf, sizeof(buf), "%s [id=\"%s\"]", sel->tag, sel->id);
    } else {
        snprintf(buf, sizeof(buf), "%s", sel->tag);
    }
    SetWindowTextA(st->hwnd_edit_tag, buf);
    SetWindowTextA(st->hwnd_edit_section, sel->section);

    if (sel->flags & ATTR_IS_DOOR_PAIR) {
        snprintf(buf, sizeof(buf), "%d", sel->doorX); SetWindowTextA(st->hwnd_edit_x, buf);
        snprintf(buf, sizeof(buf), "%d", sel->doorY); SetWindowTextA(st->hwnd_edit_y, buf);
        snprintf(buf, sizeof(buf), "%d", sel->frogX); SetWindowTextA(st->hwnd_edit_frog_x, buf);
        snprintf(buf, sizeof(buf), "%d", sel->frogY); SetWindowTextA(st->hwnd_edit_frog_y, buf);
    } else {
        if (sel->flags & ATTR_HAS_X) { snprintf(buf, sizeof(buf), "%d", sel->x); SetWindowTextA(st->hwnd_edit_x, buf); }
        else SetWindowTextA(st->hwnd_edit_x, "---");

        if (sel->flags & ATTR_HAS_Y) { snprintf(buf, sizeof(buf), "%d", sel->y); SetWindowTextA(st->hwnd_edit_y, buf); }
        else SetWindowTextA(st->hwnd_edit_y, "---");

        SetWindowTextA(st->hwnd_edit_frog_x, "---");
        SetWindowTextA(st->hwnd_edit_frog_y, "---");
    }

    if (sel->flags & ATTR_HAS_W) { snprintf(buf, sizeof(buf), "%d", sel->w); SetWindowTextA(st->hwnd_edit_w, buf); }
    else SetWindowTextA(st->hwnd_edit_w, "---");

    if (sel->flags & ATTR_HAS_H) { snprintf(buf, sizeof(buf), "%d", sel->h); SetWindowTextA(st->hwnd_edit_h, buf); }
    else SetWindowTextA(st->hwnd_edit_h, "---");

    if (sel->flags & ATTR_HAS_CENTER_X) { snprintf(buf, sizeof(buf), "%d", sel->centerX); SetWindowTextA(st->hwnd_edit_center_x, buf); }
    else SetWindowTextA(st->hwnd_edit_center_x, "---");

    if (sel->flags & ATTR_HAS_CENTER_Y) { snprintf(buf, sizeof(buf), "%d", sel->centerY); SetWindowTextA(st->hwnd_edit_center_y, buf); }
    else SetWindowTextA(st->hwnd_edit_center_y, "---");

    if (sel->flags & ATTR_HAS_OFFSET_X) { snprintf(buf, sizeof(buf), "%d", sel->offsetX); SetWindowTextA(st->hwnd_edit_offset_x, buf); }
    else SetWindowTextA(st->hwnd_edit_offset_x, "---");

    if (sel->flags & ATTR_HAS_OFFSET_Y) { snprintf(buf, sizeof(buf), "%d", sel->offsetY); SetWindowTextA(st->hwnd_edit_offset_y, buf); }
    else SetWindowTextA(st->hwnd_edit_offset_y, "---");

    SetWindowTextA(st->hwnd_edit_align, sel->align[0] ? sel->align : "---");

    if (sel->flags & ATTR_HAS_SPACING) { snprintf(buf, sizeof(buf), "%d", sel->spacing); SetWindowTextA(st->hwnd_edit_spacing, buf); }
    else SetWindowTextA(st->hwnd_edit_spacing, "---");

    char hint[128];
    snprintf(hint, sizeof(hint), "Selected: %s | %d element(s) active.", sel->tag, sel_count);
    SetWindowTextA(st->hwnd_lbl_coords, hint);

    st->syncing_inspector = false;
}

/* Populate element listbox with elements of current screen & filter */
static void layout_editor_update_screen_elements_list(LayoutEditorState *st) {
    if (!st || !st->hwnd_list_elements) return;

    SendMessage(st->hwnd_list_elements, LB_RESETCONTENT, 0, 0);

    for (int i = 0; i < st->element_count; ++i) {
        LayoutElement *el = &st->elements[i];
        if (!is_element_visible_in_editor(el, st)) continue;

        char item_txt[96];
        if (el->flags & ATTR_IS_DOOR_PAIR) {
            snprintf(item_txt, sizeof(item_txt), "Door %s (door: %d,%d | frog: %d,%d)",
                     el->id, el->doorX, el->doorY, el->frogX, el->frogY);
        } else if (el->flags & ATTR_HAS_CENTER_X) {
            if (el->flags & ATTR_HAS_CENTER_Y) {
                snprintf(item_txt, sizeof(item_txt), "%s (center: %d, %d)", el->tag, el->centerX, el->centerY);
            } else {
                snprintf(item_txt, sizeof(item_txt), "%s (centerX: %d, %d)", el->tag, el->centerX, el->y);
            }
        } else if (el->flags & ATTR_HAS_ID) {
            snprintf(item_txt, sizeof(item_txt), "%s [id=\"%s\"] (%d, %d)", el->tag, el->id, el->x, el->y);
        } else if (!(el->flags & ATTR_HAS_X) && (_stricmp(el->tag, "ClickToPlay") == 0 || _stricmp(el->tag, "WelcomeText") == 0 || _stricmp(el->tag, "UserSwitchText") == 0)) {
            snprintf(item_txt, sizeof(item_txt), "%s (Center, %d)", el->tag, el->y);
        } else {
            snprintf(item_txt, sizeof(item_txt), "%s (%d, %d)", el->tag, el->x, el->y);
        }

        int idx = (int)SendMessageA(st->hwnd_list_elements, LB_ADDSTRING, 0, (LPARAM)item_txt);
        SendMessage(st->hwnd_list_elements, LB_SETITEMDATA, idx, (LPARAM)i);

        if (el->selected) {
            SendMessage(st->hwnd_list_elements, LB_SETCURSEL, idx, 0);
        }
    }
}

/* Update sub-filter options combobox when screen view changes */
static void layout_editor_update_sub_filter_options(LayoutEditorState *st) {
    if (!st || !st->hwnd_combo_filter) return;

    SendMessage(st->hwnd_combo_filter, CB_RESETCONTENT, 0, 0);
    st->cur_sub_filter_idx = 0;

    if (st->cur_screen_idx == 2) {
        /* Adventure Map */
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"All Elements");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Doors & Waypoints (13)");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Temples & Center (8)");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Buttons (Play/Menu) (2)");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Labels & Highscores (13)");
    } else if (st->cur_screen_idx == 3) {
        /* Gauntlet Screen */
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"All Elements");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Practice Doors (4)");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Navigation Buttons (6)");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Labels, Frames & Gems (10)");
    } else if (st->cur_screen_idx == 4) {
        /* In-Game HUD */
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"All HUD Elements");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Menubar HUD (livesX, etc.)");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Menubar Margins (top, etc.)");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Banners (Level & Lives)");
    } else if (st->cur_screen_idx == 5) {
        /* Stage Complete */
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"All Cutscene Elements");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Stage & Complete Banner");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Frog Writing & Waypoints");
    } else if (st->cur_screen_idx == 6) {
        /* Temple Cleared */
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"All Ending Elements");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Tiki God Head & Medallion");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Dialogue Box & Replies");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Title & Continue Button");
    } else if (st->cur_screen_idx == 8) {
        /* Help Screen */
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"All Help Elements");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Stone Tablet & Icons");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Tutorial Controls");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"DevTester Instructions");
    } else if (st->cur_screen_idx == 11) {
        /* DevTester Help */
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"All DevTester Elements");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Headers & Columns");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Bullets & Lists");
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"Interactive 5-Ball Demo");
    } else {
        SendMessageA(st->hwnd_combo_filter, CB_ADDSTRING, 0, (LPARAM)"All Elements");
    }

    SendMessage(st->hwnd_combo_filter, CB_SETCURSEL, 0, 0);
}

/* Viewport Canvas Window Procedure (Handles Angkat & Taruh, Marquee, Hover & Nudge) */
static LRESULT CALLBACK LayoutCanvasProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LayoutEditorState *st = g_layout_editor;
    if (!st) return DefWindowProcA(hwnd, msg, wParam, lParam);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int cv_w = rc.right - rc.left;
    int cv_h = rc.bottom - rc.top;

    switch (msg) {
        case WM_PAINT:
            layout_canvas_paint(st, hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1; /* Double buffered in WM_PAINT */

        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, (st->held_elem || st->is_picked_up) ? IDC_SIZEALL : IDC_ARROW));
            return TRUE;

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            st->hover_canvas_x = mx;
            st->hover_canvas_y = my;

            /* Check if picking up / dragging an element */
            if (st->held_elem && (st->is_picked_up || st->is_mouse_dragging)) {
                float vscale, zscale;
                calculate_viewport_transforms(st, cv_w, cv_h, &vscale, NULL, NULL, NULL, NULL, &zscale, NULL, NULL, NULL, NULL);

                int delta_canvas_x = mx - st->drag_start_mx;
                int delta_canvas_y = my - st->drag_start_my;

                bool is_drag_1to1 = is_element_space_1to1(st->held_elem, st);
                float denom = is_drag_1to1 ? vscale : (vscale * zscale);
                int delta_canon_x = (denom > 0.0001f) ? (int)roundf((float)delta_canvas_x / denom) : 0;
                int delta_canon_y = (denom > 0.0001f) ? (int)roundf((float)delta_canvas_y / denom) : 0;

                if (st->held_elem->flags & ATTR_IS_DOOR_PAIR) {
                    if (st->held_handle == 1) {
                        /* Frog Waypoint handle */
                        st->held_elem->frogX = st->drag_orig_frog_x + delta_canon_x;
                        st->held_elem->frogY = st->drag_orig_frog_y + delta_canon_y;
                    } else {
                        /* Door Arch handle */
                        st->held_elem->doorX = st->drag_orig_x + delta_canon_x;
                        st->held_elem->doorY = st->drag_orig_y + delta_canon_y;
                    }
                } else {
                    if (st->held_elem->flags & ATTR_HAS_CENTER_X) {
                        st->held_elem->centerX = st->drag_orig_center_x + delta_canon_x;
                    }
                    if (st->held_elem->flags & ATTR_HAS_X) {
                        st->held_elem->x = st->drag_orig_x + delta_canon_x;
                    } else if (!(st->held_elem->flags & ATTR_HAS_CENTER_X)) {
                        st->held_elem->x = st->drag_orig_x + delta_canon_x;
                        st->held_elem->flags |= ATTR_HAS_X;
                    }

                    if (st->held_elem->flags & ATTR_HAS_CENTER_Y) {
                        st->held_elem->centerY = st->drag_orig_center_y + delta_canon_y;
                    }
                    if (st->held_elem->flags & ATTR_HAS_Y) {
                        st->held_elem->y = st->drag_orig_y + delta_canon_y;
                    } else if (!(st->held_elem->flags & ATTR_HAS_CENTER_Y)) {
                        st->held_elem->y = st->drag_orig_y + delta_canon_y;
                        st->held_elem->flags |= ATTR_HAS_Y;
                    }
                }

                layout_editor_sync_inspector_from_selection(st);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            /* Marquee Box Selection */
            if (st->marquee_active) {
                st->marquee_curr.x = mx;
                st->marquee_curr.y = my;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            /* Detect hover target (descending Z-order) */
            int new_handle = 0;
            LayoutElement *new_hover = layout_editor_hit_test(st, mx, my, &new_handle);
            if (new_hover != st->hovered_elem || new_handle != st->hovered_handle) {
                st->hovered_elem = new_hover;
                st->hovered_handle = new_handle;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            bool ctrl_down = ((GetKeyState(VK_CONTROL) & 0x8000) != 0) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) || ((wParam & MK_CONTROL) != 0);
            bool alt_down  = ((GetKeyState(VK_MENU) & 0x8000) != 0) || ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0);

            SetFocus(hwnd);

            /* If an element was already picked up via "Angkat dan Taruh", this click drops it! */
            if (st->is_picked_up && st->held_elem) {
                st->is_picked_up = false;
                st->held_elem = NULL;
                ReleaseCapture();
                layout_editor_update_screen_elements_list(st);
                layout_editor_sync_inspector_from_selection(st);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            /* Overlay Mode feature parity: Hold Ctrl + Alt + Left Click to recycle click on overlapping stacked elements */
            if (ctrl_down && alt_down) {
                typedef struct {
                    LayoutElement *elem;
                    int handle;
                } HitItem;
                HitItem hits[MAX_LAYOUT_ELEMENTS * 2];
                int hit_count = 0;
                POINT pt = { mx, my };

                for (int i = st->element_count - 1; i >= 0; --i) {
                    LayoutElement *el = &st->elements[i];
                    if (!is_element_visible_in_editor(el, st)) continue;

                    if (el->flags & ATTR_IS_DOOR_PAIR) {
                        if (PtInRect(&el->frog_canvas_rect, pt)) {
                            hits[hit_count].elem = el;
                            hits[hit_count].handle = 1;
                            hit_count++;
                        }
                        if (PtInRect(&el->canvas_rect, pt)) {
                            hits[hit_count].elem = el;
                            hits[hit_count].handle = 0;
                            hit_count++;
                        }
                    } else {
                        if (PtInRect(&el->canvas_rect, pt)) {
                            hits[hit_count].elem = el;
                            hits[hit_count].handle = 0;
                            hit_count++;
                        }
                    }
                }

                if (hit_count > 0) {
                    int cur_hit_idx = -1;
                    for (int h = 0; h < hit_count; ++h) {
                        if (hits[h].elem->selected) {
                            cur_hit_idx = h;
                            break;
                        }
                    }

                    int next_hit_idx = (cur_hit_idx == -1) ? 0 : ((cur_hit_idx + 1) % hit_count);

                    for (int i = 0; i < st->element_count; ++i) st->elements[i].selected = false;
                    hits[next_hit_idx].elem->selected = true;
                    st->held_elem = NULL;
                    st->is_picked_up = false;

                    layout_editor_update_screen_elements_list(st);
                    layout_editor_sync_inspector_from_selection(st);

                    char cycle_msg[128];
                    snprintf(cycle_msg, sizeof(cycle_msg), "Recycle Click: %s [%s] (Layer %d of %d)",
                             hits[next_hit_idx].elem->tag, hits[next_hit_idx].elem->id[0] ? hits[next_hit_idx].elem->id : "-",
                             next_hit_idx + 1, hit_count);
                    SetWindowTextA(st->hwnd_lbl_coords, cycle_msg);

                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }

            /* Standard Hit-test elements (descending Z-order) */
            int clicked_handle = 0;
            LayoutElement *clicked_elem = layout_editor_hit_test(st, mx, my, &clicked_handle);

            if (clicked_elem) {
                if (ctrl_down) {
                    /* Ctrl + Click: Toggle multi-selection */
                    clicked_elem->selected = !clicked_elem->selected;
                    st->held_elem = NULL;
                    st->is_picked_up = false;
                } else {
                    /* Single click: Pick up element */
                    if (!clicked_elem->selected) {
                        for (int i = 0; i < st->element_count; ++i) st->elements[i].selected = false;
                        clicked_elem->selected = true;
                    }

                    st->held_elem = clicked_elem;
                    st->held_handle = clicked_handle;
                    st->is_mouse_dragging = true;
                    st->drag_start_mx = mx;
                    st->drag_start_my = my;

                    if (clicked_elem->flags & ATTR_IS_DOOR_PAIR) {
                        st->drag_orig_x = clicked_elem->doorX;
                        st->drag_orig_y = clicked_elem->doorY;
                        st->drag_orig_frog_x = clicked_elem->frogX;
                        st->drag_orig_frog_y = clicked_elem->frogY;
                    } else {
                        st->drag_orig_x = clicked_elem->x;
                        st->drag_orig_y = clicked_elem->y;
                        st->drag_orig_center_x = clicked_elem->centerX;
                        st->drag_orig_center_y = clicked_elem->centerY;
                    }

                    SetCapture(hwnd);
                }

                layout_editor_update_screen_elements_list(st);
                layout_editor_sync_inspector_from_selection(st);
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                /* Clicked empty area */
                if (ctrl_down) {
                    /* Ctrl + Drag Marquee Selection Box */
                    st->marquee_active = true;
                    st->marquee_start.x = mx;
                    st->marquee_start.y = my;
                    st->marquee_curr.x = mx;
                    st->marquee_curr.y = my;
                    SetCapture(hwnd);
                } else {
                    /* Deselect all */
                    for (int i = 0; i < st->element_count; ++i) st->elements[i].selected = false;
                    st->held_elem = NULL;
                    st->is_picked_up = false;
                    layout_editor_update_screen_elements_list(st);
                    layout_editor_sync_inspector_from_selection(st);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            if (st->marquee_active) {
                st->marquee_active = false;
                ReleaseCapture();

                RECT mq;
                mq.left   = (st->marquee_start.x < mx) ? st->marquee_start.x : mx;
                mq.right  = (st->marquee_start.x > mx) ? st->marquee_start.x : mx;
                mq.top    = (st->marquee_start.y < my) ? st->marquee_start.y : my;
                mq.bottom = (st->marquee_start.y > my) ? st->marquee_start.y : my;

                /* Select elements overlapping marquee */
                for (int i = 0; i < st->element_count; ++i) {
                    LayoutElement *el = &st->elements[i];
                    if (!is_element_visible_in_editor(el, st)) continue;

                    RECT dummy;
                    if (IntersectRect(&dummy, &mq, &el->canvas_rect) ||
                        ((el->flags & ATTR_IS_DOOR_PAIR) && IntersectRect(&dummy, &mq, &el->frog_canvas_rect))) {
                        el->selected = true;
                    }
                }

                layout_editor_update_screen_elements_list(st);
                layout_editor_sync_inspector_from_selection(st);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if (st->is_mouse_dragging) {
                st->is_mouse_dragging = false;
                ReleaseCapture();

                int dist = abs(mx - st->drag_start_mx) + abs(my - st->drag_start_my);
                if (dist < 4) {
                    /* User clicked without dragging: enter Angkat Mode (follows cursor until next click) */
                    st->is_picked_up = true;
                } else {
                    /* Finished drag and drop */
                    st->held_elem = NULL;
                    st->is_picked_up = false;
                }

                layout_editor_update_screen_elements_list(st);
                layout_editor_sync_inspector_from_selection(st);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_KEYDOWN: {
            int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
            int dx = 0, dy = 0;

            if (wParam == VK_LEFT)  dx = -step;
            if (wParam == VK_RIGHT) dx = step;
            if (wParam == VK_UP)    dy = -step;
            if (wParam == VK_DOWN)  dy = step;

            if (dx != 0 || dy != 0) {
                for (int i = 0; i < st->element_count; ++i) {
                    LayoutElement *el = &st->elements[i];
                    if (!el->selected || !is_element_visible_in_editor(el, st)) continue;

                    if (el->flags & ATTR_IS_DOOR_PAIR) {
                        el->doorX += dx;
                        el->doorY += dy;
                        el->frogX += dx;
                        el->frogY += dy;
                    } else {
                        if (el->flags & ATTR_HAS_CENTER_X) el->centerX += dx;
                        if (el->flags & ATTR_HAS_X) el->x += dx;
                        else if (!(el->flags & ATTR_HAS_CENTER_X)) { el->x += dx; el->flags |= ATTR_HAS_X; }

                        if (el->flags & ATTR_HAS_CENTER_Y) el->centerY += dy;
                        if (el->flags & ATTR_HAS_Y) el->y += dy;
                        else if (!(el->flags & ATTR_HAS_CENTER_Y)) { el->y += dy; el->flags |= ATTR_HAS_Y; }
                    }
                }

                layout_editor_sync_inspector_from_selection(st);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* Modal Dialog Window Procedure */
static LRESULT CALLBACK LayoutEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LayoutEditorState *st = g_layout_editor;

    switch (msg) {
        case WM_CREATE: {
            if (st) {
                const ThemeColors *th = get_theme();
                COLORREF edit_bg = (g_current_theme == THEME_DARK) ? RGB(26, 34, 48) : RGB(255, 255, 255);
                COLORREF list_bg = (g_current_theme == THEME_DARK) ? RGB(22, 28, 40) : RGB(255, 255, 255);
                st->hbr_dlg_bg   = CreateSolidBrush(th->bar_bg);
                st->hbr_panel_bg = CreateSolidBrush(th->workspace_bg);
                st->hbr_edit_bg  = CreateSolidBrush(edit_bg);
                st->hbr_list_bg  = CreateSolidBrush(list_bg);
            }
            return 0;
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (st && st->hbr_dlg_bg) {
                FillRect(hdc, &rc, st->hbr_dlg_bg);
            } else {
                const ThemeColors *th = get_theme();
                HBRUSH br = CreateSolidBrush(th->bar_bg);
                FillRect(hdc, &rc, br);
                DeleteObject(br);
            }
            return 1;
        }

        case WM_CTLCOLORDLG: {
            if (st && st->hbr_dlg_bg) return (LRESULT)st->hbr_dlg_bg;
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc_ctl = (HDC)wParam;
            HWND hwnd_ctl = (HWND)lParam;
            const ThemeColors *th = get_theme();
            SetBkMode(hdc_ctl, TRANSPARENT);
            if (st && hwnd_ctl == st->hwnd_lbl_coords) {
                SetTextColor(hdc_ctl, th->text_accent_blue);
            } else if (st && hwnd_ctl == st->hwnd_lbl_res_info) {
                SetTextColor(hdc_ctl, th->text_accent_green);
            } else {
                SetTextColor(hdc_ctl, th->text_primary);
            }
            if (st && st->hbr_dlg_bg) return (LRESULT)st->hbr_dlg_bg;
            break;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc_ctl = (HDC)wParam;
            const ThemeColors *th = get_theme();
            COLORREF edit_bg = (g_current_theme == THEME_DARK) ? RGB(26, 34, 48) : RGB(255, 255, 255);
            SetTextColor(hdc_ctl, th->text_primary);
            SetBkColor(hdc_ctl, edit_bg);
            if (st && st->hbr_edit_bg) return (LRESULT)st->hbr_edit_bg;
            break;
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdc_ctl = (HDC)wParam;
            const ThemeColors *th = get_theme();
            COLORREF list_bg = (g_current_theme == THEME_DARK) ? RGB(22, 28, 40) : RGB(255, 255, 255);
            SetTextColor(hdc_ctl, th->text_primary);
            SetBkColor(hdc_ctl, list_bg);
            if (st && st->hbr_list_bg) return (LRESULT)st->hbr_list_bg;
            break;
        }

        case WM_CTLCOLORBTN: {
            if (st && st->hbr_dlg_bg) return (LRESULT)st->hbr_dlg_bg;
            break;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)lParam;
            if (!dis) break;
            const ThemeColors *th = get_theme();
            bool is_dark = (g_current_theme == THEME_DARK);
            bool is_pressed = (dis->itemState & ODS_SELECTED) != 0;

            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;

            if (dis->CtlID == ID_LE_BTN_SAVE) {
                /* Premium Accent Blue Save Button */
                COLORREF bg_col = is_pressed ? RGB(10, 80, 105) : (is_dark ? RGB(14, 116, 144) : RGB(14, 165, 233));
                COLORREF border_col = RGB(56, 189, 248);
                COLORREF txt_col = RGB(255, 255, 255);

                HBRUSH br = CreateSolidBrush(bg_col);
                HPEN pen = CreatePen(PS_SOLID, 1, border_col);
                HBRUSH old_br = (HBRUSH)SelectObject(hdc, br);
                HPEN old_pen = (HPEN)SelectObject(hdc, pen);

                RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

                SelectObject(hdc, old_br);
                SelectObject(hdc, old_pen);
                DeleteObject(br);
                DeleteObject(pen);

                HFONT font = g_font_primary_bold ? g_font_primary_bold : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HFONT old_font = (HFONT)SelectObject(hdc, font);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, txt_col);
                DrawTextA(hdc, "Save Layout", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, old_font);
                return TRUE;
            } else if (dis->CtlID == ID_LE_BTN_RELOAD || dis->CtlID == ID_LE_BTN_CLOSE) {
                COLORREF bg_col = is_pressed ? (is_dark ? RGB(40, 52, 70) : RGB(226, 232, 240)) :
                                               (is_dark ? th->btn_bg : RGB(241, 245, 249));
                COLORREF border_col = is_dark ? th->btn_border : RGB(203, 213, 225);
                COLORREF txt_col = is_dark ? th->text_primary : RGB(30, 41, 59);

                HBRUSH br = CreateSolidBrush(bg_col);
                HPEN pen = CreatePen(PS_SOLID, 1, border_col);
                HBRUSH old_br = (HBRUSH)SelectObject(hdc, br);
                HPEN old_pen = (HPEN)SelectObject(hdc, pen);

                RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

                SelectObject(hdc, old_br);
                SelectObject(hdc, old_pen);
                DeleteObject(br);
                DeleteObject(pen);

                const char *txt = (dis->CtlID == ID_LE_BTN_RELOAD) ? "Reload" : "Close";
                HFONT font = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HFONT old_font = (HFONT)SelectObject(hdc, font);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, txt_col);
                DrawTextA(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, old_font);
                return TRUE;
            }
            break;
        }

        case WM_SIZE: {
            if (!st) break;
            RECT rc;
            GetClientRect(hwnd, &rc);
            int client_w = rc.right - rc.left;
            int client_h = rc.bottom - rc.top;

            int cv_x = 16;
            int cv_y = 52;
            int cv_w = client_w - 336;
            int cv_h = client_h - 68;

            int pnl_x = cv_x + cv_w + 12;
            int pnl_w = client_w - pnl_x - 16;

            if (st->hwnd_canvas) {
                SetWindowPos(st->hwnd_canvas, NULL, cv_x, cv_y, cv_w, cv_h, SWP_NOZORDER);
                InvalidateRect(st->hwnd_canvas, NULL, FALSE);
            }

            if (st->hwnd_list_elements) {
                SetWindowPos(st->hwnd_list_elements, NULL, pnl_x, 80, pnl_w, 200, SWP_NOZORDER);
            }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id == ID_LE_COMBO_SCREEN && code == CBN_SELCHANGE) {
                int sel = (int)SendMessage(st->hwnd_combo_screen, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_SCREENS) {
                    st->cur_screen_idx = sel;
                    for (int i = 0; i < st->element_count; ++i) st->elements[i].selected = false;
                    st->held_elem = NULL;
                    st->is_picked_up = false;
                    layout_editor_update_sub_filter_options(st);
                    layout_editor_load_screen_background(st);
                    layout_editor_update_screen_elements_list(st);
                    layout_editor_sync_inspector_from_selection(st);
                    InvalidateRect(st->hwnd_canvas, NULL, FALSE);
                }
            } else if (id == ID_LE_CHK_CONDITIONAL && code == BN_CLICKED) {
                LRESULT chk = SendMessage(st->hwnd_chk_conditional, BM_GETCHECK, 0, 0);
                st->show_conditional_sprites = (chk == BST_CHECKED);
                layout_editor_update_screen_elements_list(st);
                InvalidateRect(st->hwnd_canvas, NULL, FALSE);
            } else if (id == ID_LE_COMBO_FILTER && code == CBN_SELCHANGE) {
                int sub = (int)SendMessage(st->hwnd_combo_filter, CB_GETCURSEL, 0, 0);
                if (sub >= 0) {
                    st->cur_sub_filter_idx = sub;
                    for (int i = 0; i < st->element_count; ++i) st->elements[i].selected = false;
                    st->held_elem = NULL;
                    st->is_picked_up = false;
                    layout_editor_update_screen_elements_list(st);
                    layout_editor_sync_inspector_from_selection(st);
                    InvalidateRect(st->hwnd_canvas, NULL, FALSE);
                }

            } else if (id == ID_LE_LIST_ELEMENTS && code == LBN_SELCHANGE) {
                int sel = (int)SendMessage(st->hwnd_list_elements, LB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    int elem_idx = (int)SendMessage(st->hwnd_list_elements, LB_GETITEMDATA, sel, 0);
                    if (elem_idx >= 0 && elem_idx < st->element_count) {
                        for (int i = 0; i < st->element_count; ++i) st->elements[i].selected = false;
                        st->elements[elem_idx].selected = true;
                        layout_editor_sync_inspector_from_selection(st);
                        InvalidateRect(st->hwnd_canvas, NULL, FALSE);
                    }
                }
            } else if (!st->syncing_inspector && code == EN_CHANGE &&
                       (id == ID_LE_EDIT_X || id == ID_LE_EDIT_Y ||
                        id == ID_LE_EDIT_FROG_X || id == ID_LE_EDIT_FROG_Y ||
                        id == ID_LE_EDIT_W || id == ID_LE_EDIT_H ||
                        id == ID_LE_EDIT_CENTER_X || id == ID_LE_EDIT_CENTER_Y ||
                        id == ID_LE_EDIT_OFFSET_X || id == ID_LE_EDIT_OFFSET_Y ||
                        id == ID_LE_EDIT_SPACING)) {
                /* Two-Way Real-Time Synchronization from Property Inspector to Canvas */
                LayoutElement *sel_el = NULL;
                for (int i = 0; i < st->element_count; ++i) {
                    if (st->elements[i].selected && is_element_visible_in_editor(&st->elements[i], st)) {
                        sel_el = &st->elements[i];
                        break;
                    }
                }

                if (sel_el) {
                    char val_buf[32];
                    HWND hwnd_src = (HWND)lParam;
                    GetWindowTextA(hwnd_src, val_buf, sizeof(val_buf));
                    int v = atoi(val_buf);

                    if (id == ID_LE_EDIT_X) {
                        if (sel_el->flags & ATTR_IS_DOOR_PAIR) { sel_el->doorX = v; sel_el->flags |= ATTR_HAS_DOOR_X; }
                        else { sel_el->x = v; sel_el->flags |= ATTR_HAS_X; }
                    } else if (id == ID_LE_EDIT_Y) {
                        if (sel_el->flags & ATTR_IS_DOOR_PAIR) { sel_el->doorY = v; sel_el->flags |= ATTR_HAS_DOOR_Y; }
                        else { sel_el->y = v; sel_el->flags |= ATTR_HAS_Y; }
                    } else if (id == ID_LE_EDIT_FROG_X) {
                        if (sel_el->flags & ATTR_IS_DOOR_PAIR) { sel_el->frogX = v; sel_el->flags |= ATTR_HAS_FROG_X; }
                    } else if (id == ID_LE_EDIT_FROG_Y) {
                        if (sel_el->flags & ATTR_IS_DOOR_PAIR) { sel_el->frogY = v; sel_el->flags |= ATTR_HAS_FROG_Y; }
                    } else if (id == ID_LE_EDIT_W) {
                        sel_el->w = v;
                        sel_el->flags |= ATTR_HAS_W;
                        assign_element_default_bounds(sel_el, st);
                    } else if (id == ID_LE_EDIT_H) {
                        sel_el->h = v;
                        sel_el->flags |= ATTR_HAS_H;
                        assign_element_default_bounds(sel_el, st);
                    } else if (id == ID_LE_EDIT_CENTER_X) {
                        sel_el->centerX = v;
                        sel_el->flags |= ATTR_HAS_CENTER_X;
                    } else if (id == ID_LE_EDIT_CENTER_Y) {
                        sel_el->centerY = v;
                        sel_el->flags |= ATTR_HAS_CENTER_Y;
                    } else if (id == ID_LE_EDIT_OFFSET_X) {
                        sel_el->offsetX = v;
                        sel_el->flags |= ATTR_HAS_OFFSET_X;
                    } else if (id == ID_LE_EDIT_OFFSET_Y) {
                        sel_el->offsetY = v;
                        sel_el->flags |= ATTR_HAS_OFFSET_Y;
                    } else if (id == ID_LE_EDIT_SPACING) {
                        sel_el->spacing = v;
                        sel_el->flags |= ATTR_HAS_SPACING;
                    }

                    InvalidateRect(st->hwnd_canvas, NULL, FALSE);
                }
            } else if (id == ID_LE_BTN_SAVE) {
                if (layout_xml_save(st, st->layout_filepath)) {
                    show_themed_notice_dialog(
                        hwnd,
                        "ZuPather - Layout Editor",
                        "File layout.xml Berhasil Disimpan",
                        "Seluruh perubahan posisi koordinat elemen layout berhasil disimpan ke:\n\n"
                        "properties\\layout.xml\n\n"
                        "Engine ZuCompleluxe akan langsung menerapkan tata letak baru ini saat dijalankan.",
                        560, 280
                    );
                } else {
                    show_themed_notice_dialog(
                        hwnd,
                        "ZuPather - Layout Editor",
                        "Gagal Menyimpan File",
                        "Terjadi kesalahan saat menulis perubahan ke properties\\layout.xml.\nPastikan file tidak terkunci oleh Zuma Deluxe.",
                        500, 240
                    );
                }
            } else if (id == ID_LE_BTN_RELOAD) {
                layout_clear_sprite_cache();
                layout_xml_load(st, st->layout_filepath);
                layout_editor_load_screen_background(st);
                layout_editor_update_sub_filter_options(st);
                layout_editor_update_screen_elements_list(st);
                layout_editor_sync_inspector_from_selection(st);
                InvalidateRect(st->hwnd_canvas, NULL, FALSE);
            } else if (id == ID_LE_BTN_CLOSE) {
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            layout_clear_sprite_cache();
            if (st) {
                if (st->has_bg_image) {
                    image_free(&st->bg_image);
                    st->has_bg_image = false;
                }
                if (st->hbr_dlg_bg)   DeleteObject(st->hbr_dlg_bg);
                if (st->hbr_panel_bg) DeleteObject(st->hbr_panel_bg);
                if (st->hbr_edit_bg)  DeleteObject(st->hbr_edit_bg);
                if (st->hbr_list_bg)  DeleteObject(st->hbr_list_bg);

                if (st->hwnd_parent) EnableWindow(st->hwnd_parent, TRUE);
                SetFocus(st->hwnd_parent);
                free(st);
                g_layout_editor = NULL;
            }
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* Public entry point: Display Layout Editor Modal Window */
void show_layout_editor_dialog(HWND parent_hwnd) {
    if (g_layout_editor) {
        SetFocus(g_layout_editor->hwnd_dlg);
        return;
    }

    char layout_path[MAX_PATH] = {0};
    if (!layout_editor_is_available(layout_path, sizeof(layout_path))) {
        show_themed_notice_dialog(
            parent_hwnd,
            "ZuPather - Layout Editor",
            "ZuCompleluxe v0.6+ Patch Diperlukan",
            "Zuma Deluxe perlu dipatch dengan ZuCompleluxe v0.6 ke atas untuk mengakses fitur edit layout ini.\n\n"
            "File properties\\layout.xml tidak ditemukan pada direktori Zuma yang tertaut.\n"
            "Silakan pastikan direktori Zuma Deluxe telah diatur melalui Settings & Preferences dan telah memiliki file properties\\layout.xml.",
            560, 290
        );
        return;
    }

    HINSTANCE hinst = GetModuleHandle(NULL);
    bool is_dark = (g_current_theme == THEME_DARK);

    /* Register Window Classes */
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = LayoutEditorWndProc;
    wc.hInstance     = hinst;
    wc.lpszClassName = "ZuPatherLayoutEditorModal";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    WNDCLASSA wcv;
    memset(&wcv, 0, sizeof(wcv));
    wcv.lpfnWndProc   = LayoutCanvasProc;
    wcv.hInstance     = hinst;
    wcv.lpszClassName = "ZuPatherLayoutCanvas";
    wcv.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wcv);

    /* Allocate state */
    LayoutEditorState *st = (LayoutEditorState*)calloc(1, sizeof(LayoutEditorState));
    g_layout_editor = st;
    st->hwnd_parent = parent_hwnd;
    strncpy(st->layout_filepath, layout_path, sizeof(st->layout_filepath) - 1);
    st->cur_screen_idx = 0; /* Loading Screen default */

    /* Auto-detect Zuma display resolution */
    int auto_w = 640, auto_h = 480;
    st->has_detected_res = detect_zuma_native_resolution(&auto_w, &auto_h);
    st->detected_w = auto_w;
    st->detected_h = auto_h;
    st->target_w = auto_w;
    st->target_h = auto_h;

    /* Load layout.xml */
    layout_xml_load(st, layout_path);

    /* Compute window dimensions */
    int dlg_w = 1200;
    int dlg_h = 800;
    RECT rc_parent;
    GetWindowRect(parent_hwnd, &rc_parent);
    int cx = rc_parent.left + ((rc_parent.right - rc_parent.left) - dlg_w) / 2;
    int cy = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - dlg_h) / 2;
    if (cx < 10) cx = 10;
    if (cy < 10) cy = 10;

    HWND hwnd_dlg = CreateWindowExA(
        WS_EX_APPWINDOW, "ZuPatherLayoutEditorModal",
        "ZuPather - Layout Editor [ZuCompleluxe Dynamic Engine]",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE,
        cx, cy, dlg_w, dlg_h,
        parent_hwnd, NULL, hinst, NULL
    );
    st->hwnd_dlg = hwnd_dlg;
    EnableWindow(parent_hwnd, FALSE);

    /* Set dark title bar */
    update_window_theme_attributes(hwnd_dlg, is_dark);

    HFONT font_ui = g_font_primary_regular ? g_font_primary_regular : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT font_bold = g_font_primary_bold ? g_font_primary_bold : font_ui;
    HFONT font_mono = g_font_mono_bold ? g_font_mono_bold : font_ui;

    /* Top Toolbar Controls Layout */
    int tx = 16;
    HWND lbl_screen = CreateWindowA("STATIC", "Screen:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                                    tx, 14, 52, 24, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_screen, WM_SETFONT, (WPARAM)font_bold, TRUE);
    tx += 54;

    st->hwnd_combo_screen = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                          tx, 13, 200, 300, hwnd_dlg, (HMENU)ID_LE_COMBO_SCREEN, hinst, NULL);
    SendMessage(st->hwnd_combo_screen, WM_SETFONT, (WPARAM)font_ui, TRUE);
    apply_control_dark_mode(st->hwnd_combo_screen, is_dark);
    for (int i = 0; i < NUM_SCREENS; ++i) {
        SendMessageA(st->hwnd_combo_screen, CB_ADDSTRING, 0, (LPARAM)g_screens[i].name);
    }
    SendMessage(st->hwnd_combo_screen, CB_SETCURSEL, st->cur_screen_idx, 0);
    tx += 206;

    HWND lbl_filter = CreateWindowA("STATIC", "Filter:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                                    tx, 14, 44, 24, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_filter, WM_SETFONT, (WPARAM)font_bold, TRUE);
    tx += 46;

    st->hwnd_combo_filter = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                          tx, 13, 160, 300, hwnd_dlg, (HMENU)ID_LE_COMBO_FILTER, hinst, NULL);
    SendMessage(st->hwnd_combo_filter, WM_SETFONT, (WPARAM)font_ui, TRUE);
    apply_control_dark_mode(st->hwnd_combo_filter, is_dark);
    layout_editor_update_sub_filter_options(st);
    tx += 166;

    /* Static Resolution Indicator strictly locked to Zuma.exe */
    char auto_res_lbl[80];
    if (st->has_detected_res) {
        snprintf(auto_res_lbl, sizeof(auto_res_lbl), "Resolution: %d x %d (Locked in Zuma.exe)", auto_w, auto_h);
    } else {
        snprintf(auto_res_lbl, sizeof(auto_res_lbl), "Resolution: 640 x 480 (Vanilla)");
    }
    st->hwnd_lbl_res_info = CreateWindowA("STATIC", auto_res_lbl, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                                          tx, 14, 250, 24, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(st->hwnd_lbl_res_info, WM_SETFONT, (WPARAM)font_mono, TRUE);
    tx += 260;

    st->hwnd_chk_conditional = CreateWindowA("BUTTON", "Show Conditional", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                             tx, 14, 130, 24, hwnd_dlg, (HMENU)ID_LE_CHK_CONDITIONAL, hinst, NULL);
    SendMessage(st->hwnd_chk_conditional, WM_SETFONT, (WPARAM)font_ui, TRUE);
    apply_control_dark_mode(st->hwnd_chk_conditional, is_dark);
    tx += 136;

    st->hwnd_btn_save = CreateWindowA("BUTTON", "Save Layout", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                      tx, 11, 100, 28, hwnd_dlg, (HMENU)ID_LE_BTN_SAVE, hinst, NULL);
    SendMessage(st->hwnd_btn_save, WM_SETFONT, (WPARAM)font_bold, TRUE);
    tx += 106;

    st->hwnd_btn_reload = CreateWindowA("BUTTON", "Reload", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        tx, 11, 70, 28, hwnd_dlg, (HMENU)ID_LE_BTN_RELOAD, hinst, NULL);
    SendMessage(st->hwnd_btn_reload, WM_SETFONT, (WPARAM)font_ui, TRUE);
    tx += 76;

    st->hwnd_btn_close = CreateWindowA("BUTTON", "Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                       tx, 11, 70, 28, hwnd_dlg, (HMENU)ID_LE_BTN_CLOSE, hinst, NULL);
    SendMessage(st->hwnd_btn_close, WM_SETFONT, (WPARAM)font_ui, TRUE);

    /* Canvas Viewport */
    int cv_w = dlg_w - 336;
    int cv_h = dlg_h - 68;
    st->hwnd_canvas = CreateWindowExA(
        WS_EX_CLIENTEDGE, "ZuPatherLayoutCanvas", "",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        16, 52, cv_w, cv_h,
        hwnd_dlg, NULL, hinst, NULL
    );

    /* Right Inspector Panel Controls */
    int pnl_x = cv_w + 28;
    int pnl_w = dlg_w - pnl_x - 24;

    HWND lbl_insp = CreateWindowA("STATIC", "ELEMENT PROPERTIES", WS_CHILD | WS_VISIBLE,
                                  pnl_x, 56, pnl_w, 20, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_insp, WM_SETFONT, (WPARAM)font_bold, TRUE);

    st->hwnd_list_elements = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                                             WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
                                             pnl_x, 80, pnl_w, 190, hwnd_dlg, (HMENU)ID_LE_LIST_ELEMENTS, hinst, NULL);
    SendMessage(st->hwnd_list_elements, WM_SETFONT, (WPARAM)font_ui, TRUE);
    apply_control_dark_mode(st->hwnd_list_elements, is_dark);

    int py = 280;
    HWND lbl_tag = CreateWindowA("STATIC", "Tag / ID:", WS_CHILD | WS_VISIBLE, pnl_x, py, 75, 20, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_tag, WM_SETFONT, (WPARAM)font_ui, TRUE);
    st->hwnd_edit_tag = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_READONLY,
                                        pnl_x + 80, py, pnl_w - 80, 22, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(st->hwnd_edit_tag, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_tag, is_dark);

    py += 26;
    HWND lbl_sec = CreateWindowA("STATIC", "Section:", WS_CHILD | WS_VISIBLE, pnl_x, py, 75, 20, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_sec, WM_SETFONT, (WPARAM)font_ui, TRUE);
    st->hwnd_edit_section = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_READONLY,
                                            pnl_x + 80, py, pnl_w - 80, 22, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(st->hwnd_edit_section, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_section, is_dark);

    py += 30;
    HWND lbl_xy = CreateWindowA("STATIC", "X / Y (or door):", WS_CHILD | WS_VISIBLE, pnl_x, py, 95, 20, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_xy, WM_SETFONT, (WPARAM)font_ui, TRUE);
    st->hwnd_edit_x = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                      pnl_x + 100, py, 60, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_X, hinst, NULL);
    SendMessage(st->hwnd_edit_x, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_x, is_dark);

    st->hwnd_edit_y = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                      pnl_x + 168, py, 60, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_Y, hinst, NULL);
    SendMessage(st->hwnd_edit_y, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_y, is_dark);

    py += 26;
    HWND lbl_frog = CreateWindowA("STATIC", "Frog Waypoint:", WS_CHILD | WS_VISIBLE, pnl_x, py, 95, 20, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_frog, WM_SETFONT, (WPARAM)font_ui, TRUE);
    st->hwnd_edit_frog_x = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                           pnl_x + 100, py, 60, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_FROG_X, hinst, NULL);
    SendMessage(st->hwnd_edit_frog_x, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_frog_x, is_dark);

    st->hwnd_edit_frog_y = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                           pnl_x + 168, py, 60, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_FROG_Y, hinst, NULL);
    SendMessage(st->hwnd_edit_frog_y, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_frog_y, is_dark);

    py += 30;
    HWND lbl_wh = CreateWindowA("STATIC", "Width / Height:", WS_CHILD | WS_VISIBLE, pnl_x, py, 95, 20, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_wh, WM_SETFONT, (WPARAM)font_ui, TRUE);
    st->hwnd_edit_w = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                      pnl_x + 100, py, 60, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_W, hinst, NULL);
    SendMessage(st->hwnd_edit_w, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_w, is_dark);

    st->hwnd_edit_h = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                      pnl_x + 168, py, 60, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_H, hinst, NULL);
    SendMessage(st->hwnd_edit_h, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_h, is_dark);

    py += 26;
    HWND lbl_center = CreateWindowA("STATIC", "Center X / Y:", WS_CHILD | WS_VISIBLE, pnl_x, py, 95, 20, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_center, WM_SETFONT, (WPARAM)font_ui, TRUE);
    st->hwnd_edit_center_x = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                            pnl_x + 100, py, 60, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_CENTER_X, hinst, NULL);
    SendMessage(st->hwnd_edit_center_x, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_center_x, is_dark);

    st->hwnd_edit_center_y = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                            pnl_x + 168, py, 60, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_CENTER_Y, hinst, NULL);
    SendMessage(st->hwnd_edit_center_y, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_center_y, is_dark);

    py += 26;
    HWND lbl_offset = CreateWindowA("STATIC", "Offset X / Y:", WS_CHILD | WS_VISIBLE, pnl_x, py, 95, 20, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_offset, WM_SETFONT, (WPARAM)font_ui, TRUE);
    st->hwnd_edit_offset_x = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                            pnl_x + 100, py, 60, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_OFFSET_X, hinst, NULL);
    SendMessage(st->hwnd_edit_offset_x, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_offset_x, is_dark);

    st->hwnd_edit_offset_y = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                            pnl_x + 168, py, 60, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_OFFSET_Y, hinst, NULL);
    SendMessage(st->hwnd_edit_offset_y, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_offset_y, is_dark);

    py += 26;
    HWND lbl_align = CreateWindowA("STATIC", "Align / Spacing:", WS_CHILD | WS_VISIBLE, pnl_x, py, 95, 20, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(lbl_align, WM_SETFONT, (WPARAM)font_ui, TRUE);
    st->hwnd_edit_align = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE,
                                          pnl_x + 100, py, 75, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_ALIGN, hinst, NULL);
    SendMessage(st->hwnd_edit_align, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_align, is_dark);

    st->hwnd_edit_spacing = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                            pnl_x + 180, py, 48, 22, hwnd_dlg, (HMENU)ID_LE_EDIT_SPACING, hinst, NULL);
    SendMessage(st->hwnd_edit_spacing, WM_SETFONT, (WPARAM)font_mono, TRUE);
    apply_control_dark_mode(st->hwnd_edit_spacing, is_dark);

    py += 36;
    st->hwnd_lbl_coords = CreateWindowA("STATIC", "Click or Drag an element to edit coordinates.",
                                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        pnl_x, py, pnl_w, 40, hwnd_dlg, NULL, hinst, NULL);
    SendMessage(st->hwnd_lbl_coords, WM_SETFONT, (WPARAM)font_ui, TRUE);

    /* Initial state load */
    layout_editor_load_screen_background(st);
    layout_editor_update_screen_elements_list(st);
    layout_editor_sync_inspector_from_selection(st);
}
