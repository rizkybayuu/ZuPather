#include "undo_redo.h"
#include "zupather_state.h"
#include "image_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Forward declarations of external helper routines */
extern void sync_canvas_title_everywhere(const char *new_title, HWND sender_hwnd);
extern void sync_all_map_attr_edits_from_state(void);
extern void update_window_title(void);
extern void update_status_bar(void);
extern void load_frog_sprite(void);
extern void load_coin_sprite(void);
extern void free_cutout(LevelCutout *co);
extern void free_sprite(LevelSprite *sp);
extern void free_bgalpha(LevelBackgroundAlpha *ba);
extern void cutout_generate_masks(LevelCutout *co);
extern void bgalpha_generate_masks(LevelBackgroundAlpha *ba);
extern bool resolve_cutout_image_in_folder(const char *folder, const char *image_name, CanvasImage *out_img);
extern bool resolve_sprite_image_in_folder(const char *folder, const char *image_name, CanvasImage *out_img);
extern bool resolve_bgalpha_image_in_folder(const char *folder, const char *image_name, CanvasImage *out_img);
extern HWND g_hwnd_main;

/* Snapshot data structures in RAM (zero disk cache) */
typedef struct {
    char        name[64];
    int         node_count;
    BezierNode *nodes;
    int         span_count;
    CurveSpan  *spans;
    bool        is_closed;
    int         skullrot;
    uint32_t    next_node_id;
} UndoPathSnapshot;

typedef struct {
    int  x;
    int  y;
    int  pri;
    char image_name[64];
    char source_filepath[MAX_PATH];
} UndoCutoutSnapshot;

typedef struct {
    int  x;
    int  y;
    int  pri;
    char image_name[64];
    char source_filepath[MAX_PATH];
} UndoSpriteSnapshot;

typedef struct {
    int  x;
    int  y;
    int  pri;
    int  vx;
    int  vy;
    char image_name[64];
    char source_filepath[MAX_PATH];
} UndoBgAlphaSnapshot;

typedef struct {
    char action_name[64];

    /* Canvas Configuration */
    bool has_canvas;
    char canvas_name[128];
    char canvas_title[128];
    int  canvas_w;
    int  canvas_h;
    char canvas_folder[MAX_PATH];
    int  level_difficulty;
    bool map_has_skullrot;
    float map_skullrot;
    bool map_is_space;
    int  curve_submode;

    /* Background image descriptor */
    bool has_bg_image;
    char bg_filepath[MAX_PATH];
    char bg_filename[MAX_PATH];
    char bg_display_name[64];

    /* Frog Element */
    bool has_frog;
    int  frog_gx;
    int  frog_gy;

    /* Coins (Treasure Points) */
    int  treasure_point_count;
    TreasurePoint treasure_points[MAX_TREASURE_POINTS];

    /* Overlays */
    int  cutout_count;
    UndoCutoutSnapshot cutouts[MAX_CUTOUTS];
    int  sprite_count;
    UndoSpriteSnapshot sprites[MAX_SPRITES];
    int  bg_alpha_count;
    UndoBgAlphaSnapshot bg_alphas[MAX_BG_ALPHAS];

    /* Bezier Paths */
    int  path_count;
    int  active_path_idx;
    int  active_node_idx;
    UndoPathSnapshot paths[MAX_PATHS];
} UndoMemorySnapshot;

static UndoMemorySnapshot s_stack[MAX_UNDO_STACK];
static int  s_undo_count = 0;
static int  s_current_idx = -1;
static bool s_is_performing_undo_redo = false;

/* Helper to free dynamically allocated buffers in a snapshot */
static void free_snapshot_memory(UndoMemorySnapshot *snap) {
    if (!snap) return;
    for (int p = 0; p < snap->path_count; ++p) {
        if (snap->paths[p].nodes) {
            free(snap->paths[p].nodes);
            snap->paths[p].nodes = NULL;
        }
        if (snap->paths[p].spans) {
            free(snap->paths[p].spans);
            snap->paths[p].spans = NULL;
        }
    }
    snap->path_count = 0;
}

/* Capture current application state into a snapshot */
static void capture_current_state(UndoMemorySnapshot *snap, const char *action_name) {
    memset(snap, 0, sizeof(UndoMemorySnapshot));
    strncpy(snap->action_name, action_name ? action_name : "Action", sizeof(snap->action_name) - 1);

    /* 1. Canvas settings */
    snap->has_canvas = g_has_canvas;
    strncpy(snap->canvas_name, g_canvas_name, sizeof(snap->canvas_name) - 1);
    strncpy(snap->canvas_title, g_canvas_title, sizeof(snap->canvas_title) - 1);
    snap->canvas_w = g_canvas_w;
    snap->canvas_h = g_canvas_h;
    strncpy(snap->canvas_folder, g_canvas_folder, sizeof(snap->canvas_folder) - 1);
    snap->level_difficulty = (int)g_level_difficulty;
    snap->map_has_skullrot = g_map_has_skullrot;
    snap->map_skullrot = g_map_skullrot;
    snap->map_is_space = g_map_is_space;
    snap->curve_submode = (int)g_curve_submode;

    /* 2. Background image */
    snap->has_bg_image = g_has_bg_image;
    if (g_has_bg_image) {
        strncpy(snap->bg_filepath, g_canvas_img.filepath, sizeof(snap->bg_filepath) - 1);
        strncpy(snap->bg_filename, g_canvas_img.filename, sizeof(snap->bg_filename) - 1);
        strncpy(snap->bg_display_name, g_canvas_img.display_name, sizeof(snap->bg_display_name) - 1);
    }

    /* 3. Frog Element */
    snap->has_frog = g_has_frog;
    snap->frog_gx  = g_frog_gx;
    snap->frog_gy  = g_frog_gy;

    /* 4. Coins (Treasure Points) */
    snap->treasure_point_count = g_treasure_point_count;
    for (int i = 0; i < g_treasure_point_count && i < MAX_TREASURE_POINTS; ++i) {
        snap->treasure_points[i] = g_treasure_points[i];
    }

    /* 5. Cutouts */
    snap->cutout_count = g_cutout_count;
    for (int i = 0; i < g_cutout_count && i < MAX_CUTOUTS; ++i) {
        snap->cutouts[i].x = g_cutouts[i].x;
        snap->cutouts[i].y = g_cutouts[i].y;
        snap->cutouts[i].pri = g_cutouts[i].pri;
        strncpy(snap->cutouts[i].image_name, g_cutouts[i].image_name, sizeof(snap->cutouts[i].image_name) - 1);
        strncpy(snap->cutouts[i].source_filepath, g_cutouts[i].source_filepath, sizeof(snap->cutouts[i].source_filepath) - 1);
    }

    /* 6. Sprites */
    snap->sprite_count = g_sprite_count;
    for (int i = 0; i < g_sprite_count && i < MAX_SPRITES; ++i) {
        snap->sprites[i].x = g_sprites[i].x;
        snap->sprites[i].y = g_sprites[i].y;
        snap->sprites[i].pri = g_sprites[i].pri;
        strncpy(snap->sprites[i].image_name, g_sprites[i].image_name, sizeof(snap->sprites[i].image_name) - 1);
        strncpy(snap->sprites[i].source_filepath, g_sprites[i].source_filepath, sizeof(snap->sprites[i].source_filepath) - 1);
    }

    /* 7. Background Alphas */
    snap->bg_alpha_count = g_bg_alpha_count;
    for (int i = 0; i < g_bg_alpha_count && i < MAX_BG_ALPHAS; ++i) {
        snap->bg_alphas[i].x = g_bg_alphas[i].x;
        snap->bg_alphas[i].y = g_bg_alphas[i].y;
        snap->bg_alphas[i].pri = g_bg_alphas[i].pri;
        snap->bg_alphas[i].vx = g_bg_alphas[i].vx;
        snap->bg_alphas[i].vy = g_bg_alphas[i].vy;
        strncpy(snap->bg_alphas[i].image_name, g_bg_alphas[i].image_name, sizeof(snap->bg_alphas[i].image_name) - 1);
        strncpy(snap->bg_alphas[i].source_filepath, g_bg_alphas[i].source_filepath, sizeof(snap->bg_alphas[i].source_filepath) - 1);
    }

    /* 8. Bezier Paths and Nodes (Deep Copy) */
    snap->path_count = g_path_count;
    snap->active_path_idx = g_active_path_idx;
    snap->active_node_idx = g_active_node_idx;
    for (int p = 0; p < g_path_count && p < MAX_PATHS; ++p) {
        const BezierPath *bp = &g_paths[p];
        UndoPathSnapshot *ups = &snap->paths[p];
        strncpy(ups->name, bp->name, sizeof(ups->name) - 1);
        ups->is_closed = bp->is_closed;
        ups->skullrot = bp->skullrot;
        ups->next_node_id = bp->next_node_id;

        ups->node_count = bp->node_count;
        if (bp->node_count > 0 && bp->nodes) {
            ups->nodes = (BezierNode*)malloc(bp->node_count * sizeof(BezierNode));
            if (ups->nodes) {
                memcpy(ups->nodes, bp->nodes, bp->node_count * sizeof(BezierNode));
            } else {
                ups->node_count = 0;
            }
        } else {
            ups->nodes = NULL;
        }

        ups->span_count = bp->span_count;
        if (bp->span_count > 0 && bp->spans) {
            ups->spans = (CurveSpan*)malloc(bp->span_count * sizeof(CurveSpan));
            if (ups->spans) {
                memcpy(ups->spans, bp->spans, bp->span_count * sizeof(CurveSpan));
            } else {
                ups->span_count = 0;
            }
        } else {
            ups->spans = NULL;
        }
    }
}

/* Restore state from a memory snapshot */
static void restore_state(const UndoMemorySnapshot *snap, HWND hwnd_main) {
    if (!snap) return;

    /* 1. Restore Canvas Settings */
    g_has_canvas = snap->has_canvas;
    strncpy(g_canvas_name, snap->canvas_name, sizeof(g_canvas_name) - 1);
    strncpy(g_canvas_title, snap->canvas_title, sizeof(g_canvas_title) - 1);
    if (snap->has_canvas && snap->canvas_w > 0 && snap->canvas_h > 0) {
        set_canvas_size(hwnd_main, snap->canvas_w, snap->canvas_h);
    } else {
        g_canvas_w = snap->canvas_w;
        g_canvas_h = snap->canvas_h;
    }
    strncpy(g_canvas_folder, snap->canvas_folder, sizeof(g_canvas_folder) - 1);
    g_level_difficulty = (LevelDifficulty)snap->level_difficulty;
    g_map_has_skullrot = snap->map_has_skullrot;
    g_map_skullrot = snap->map_skullrot;
    g_map_is_space = snap->map_is_space;
    g_curve_submode = (CurveSubMode)snap->curve_submode;

    /* 2. Restore Background Image */
    if (snap->has_bg_image && snap->bg_filepath[0] != '\0') {
        if (!g_has_bg_image || strcmp(g_canvas_img.filepath, snap->bg_filepath) != 0) {
            if (g_has_bg_image) {
                image_free(&g_canvas_img);
                g_has_bg_image = false;
            }
            if (image_load_from_file(snap->bg_filepath, &g_canvas_img)) {
                g_has_bg_image = true;
                strncpy(g_canvas_img.filepath, snap->bg_filepath, sizeof(g_canvas_img.filepath) - 1);
                strncpy(g_canvas_img.filename, snap->bg_filename, sizeof(g_canvas_img.filename) - 1);
                strncpy(g_canvas_img.display_name, snap->bg_display_name, sizeof(g_canvas_img.display_name) - 1);
            }
        }
    } else {
        if (g_has_bg_image) {
            image_free(&g_canvas_img);
            g_has_bg_image = false;
        }
    }

    /* 3. Restore Frog Element */
    g_has_frog = snap->has_frog;
    g_frog_gx  = snap->frog_gx;
    g_frog_gy  = snap->frog_gy;
    if (g_has_frog && !g_has_frog_img) {
        load_frog_sprite();
    }

    /* 4. Restore Coins */
    g_treasure_point_count = snap->treasure_point_count;
    for (int i = 0; i < snap->treasure_point_count && i < MAX_TREASURE_POINTS; ++i) {
        g_treasure_points[i] = snap->treasure_points[i];
    }
    if (g_treasure_point_count > 0 && !g_has_coin_img) {
        load_coin_sprite();
    }

    /* 5. Restore Cutouts */
    for (int i = 0; i < g_cutout_count; ++i) free_cutout(&g_cutouts[i]);
    g_cutout_count = snap->cutout_count;
    for (int i = 0; i < snap->cutout_count && i < MAX_CUTOUTS; ++i) {
        LevelCutout *co = &g_cutouts[i];
        memset(co, 0, sizeof(LevelCutout));
        co->x = snap->cutouts[i].x;
        co->y = snap->cutouts[i].y;
        co->pri = snap->cutouts[i].pri;
        strncpy(co->image_name, snap->cutouts[i].image_name, sizeof(co->image_name) - 1);
        strncpy(co->source_filepath, snap->cutouts[i].source_filepath, sizeof(co->source_filepath) - 1);
        if (co->source_filepath[0] && GetFileAttributesA(co->source_filepath) != INVALID_FILE_ATTRIBUTES) {
            if (image_load_from_file(co->source_filepath, &co->img)) co->has_img = true;
        } else if (g_canvas_folder[0]) {
            resolve_cutout_image_in_folder(g_canvas_folder, co->image_name, &co->img);
        }
        cutout_generate_masks(co);
    }

    /* 6. Restore Sprites */
    for (int i = 0; i < g_sprite_count; ++i) free_sprite(&g_sprites[i]);
    g_sprite_count = snap->sprite_count;
    for (int i = 0; i < snap->sprite_count && i < MAX_SPRITES; ++i) {
        LevelSprite *sp = &g_sprites[i];
        memset(sp, 0, sizeof(LevelSprite));
        sp->x = snap->sprites[i].x;
        sp->y = snap->sprites[i].y;
        sp->pri = snap->sprites[i].pri;
        strncpy(sp->image_name, snap->sprites[i].image_name, sizeof(sp->image_name) - 1);
        strncpy(sp->source_filepath, snap->sprites[i].source_filepath, sizeof(sp->source_filepath) - 1);
        if (sp->source_filepath[0] && GetFileAttributesA(sp->source_filepath) != INVALID_FILE_ATTRIBUTES) {
            if (image_load_from_file(sp->source_filepath, &sp->img)) sp->has_img = true;
        } else if (g_canvas_folder[0]) {
            resolve_sprite_image_in_folder(g_canvas_folder, sp->image_name, &sp->img);
        }
    }

    /* 7. Restore Background Alphas */
    for (int i = 0; i < g_bg_alpha_count; ++i) free_bgalpha(&g_bg_alphas[i]);
    g_bg_alpha_count = snap->bg_alpha_count;
    for (int i = 0; i < snap->bg_alpha_count && i < MAX_BG_ALPHAS; ++i) {
        LevelBackgroundAlpha *ba = &g_bg_alphas[i];
        memset(ba, 0, sizeof(LevelBackgroundAlpha));
        ba->x = snap->bg_alphas[i].x;
        ba->y = snap->bg_alphas[i].y;
        ba->pri = snap->bg_alphas[i].pri;
        ba->vx = snap->bg_alphas[i].vx;
        ba->vy = snap->bg_alphas[i].vy;
        strncpy(ba->image_name, snap->bg_alphas[i].image_name, sizeof(ba->image_name) - 1);
        strncpy(ba->source_filepath, snap->bg_alphas[i].source_filepath, sizeof(ba->source_filepath) - 1);
        if (ba->source_filepath[0] && GetFileAttributesA(ba->source_filepath) != INVALID_FILE_ATTRIBUTES) {
            if (image_load_from_file(ba->source_filepath, &ba->img)) ba->has_img = true;
        } else if (g_canvas_folder[0]) {
            resolve_bgalpha_image_in_folder(g_canvas_folder, ba->image_name, &ba->img);
        }
        bgalpha_generate_masks(ba);
    }

    /* 8. Restore Bezier Paths and Nodes */
    for (int p = 0; p < g_path_count; ++p) {
        bezier_path_free(&g_paths[p]);
        memset(&g_paths[p], 0, sizeof(BezierPath));
    }
    g_path_count = snap->path_count;
    g_active_path_idx = snap->active_path_idx;
    g_active_node_idx = snap->active_node_idx;
    for (int p = 0; p < snap->path_count && p < MAX_PATHS; ++p) {
        const UndoPathSnapshot *src = &snap->paths[p];
        BezierPath *dst = &g_paths[p];
        bezier_path_init(dst, src->name);
        dst->is_closed = src->is_closed;
        dst->skullrot = src->skullrot;
        dst->next_node_id = src->next_node_id;

        if (src->node_count > 0 && src->nodes) {
            dst->nodes = (BezierNode*)malloc(src->node_count * sizeof(BezierNode));
            if (dst->nodes) {
                memcpy(dst->nodes, src->nodes, src->node_count * sizeof(BezierNode));
                dst->node_count = src->node_count;
            }
        }
        if (src->span_count > 0 && src->spans) {
            dst->spans = (CurveSpan*)malloc(src->span_count * sizeof(CurveSpan));
            if (dst->spans) {
                memcpy(dst->spans, src->spans, src->span_count * sizeof(CurveSpan));
                dst->span_count = src->span_count;
            }
        }
        path_cache_invalidate(dst);
        path_cache_update(dst);
    }

    /* 9. Synchronize Global UI Controls */
    sync_canvas_title_everywhere(g_canvas_title, NULL);
    sync_all_map_attr_edits_from_state();
    update_window_title();
    update_status_bar();

    if (hwnd_main && IsWindow(hwnd_main)) {
        InvalidateRect(hwnd_main, NULL, FALSE);
        UpdateWindow(hwnd_main);
    }
}

/* ========================================================================= */
/* Public API Implementation                                                 */
/* ========================================================================= */

void undo_redo_init(void) {
    undo_reset();
}

void undo_reset(void) {
    for (int i = 0; i < s_undo_count; ++i) {
        free_snapshot_memory(&s_stack[i]);
    }
    s_undo_count = 0;
    s_current_idx = -1;

    if (g_hwnd_main && IsWindow(g_hwnd_main)) {
        undo_redo_update_menu(g_hwnd_main);
    }
}

void undo_ensure_baseline(void) {
    if (s_is_performing_undo_redo) return;
    if (s_undo_count == 0 && g_has_canvas) {
        capture_current_state(&s_stack[0], "Initial State");
        s_undo_count = 1;
        s_current_idx = 0;
        if (g_hwnd_main && IsWindow(g_hwnd_main)) {
            undo_redo_update_menu(g_hwnd_main);
        }
    }
}

void undo_snapshot_capture(const char *action_name) {
    if (s_is_performing_undo_redo) return;

    /* Ensure baseline state 0 exists first */
    undo_ensure_baseline();

    /* If we rolled back with undo and then made a new edit, discard forward redo history */
    if (s_current_idx < s_undo_count - 1) {
        for (int i = s_current_idx + 1; i < s_undo_count; ++i) {
            free_snapshot_memory(&s_stack[i]);
        }
        s_undo_count = s_current_idx + 1;
    }

    /* If stack reached capacity (64 turns limit), slide oldest snapshot out */
    if (s_undo_count >= MAX_UNDO_STACK) {
        free_snapshot_memory(&s_stack[0]);
        memmove(&s_stack[0], &s_stack[1], (MAX_UNDO_STACK - 1) * sizeof(UndoMemorySnapshot));
        s_undo_count = MAX_UNDO_STACK - 1;
        s_current_idx = MAX_UNDO_STACK - 2;
    }

    /* Record new snapshot */
    capture_current_state(&s_stack[s_undo_count], action_name);
    s_current_idx = s_undo_count;
    s_undo_count++;

    if (g_hwnd_main && IsWindow(g_hwnd_main)) {
        undo_redo_update_menu(g_hwnd_main);
    }
}

bool undo_can_undo(void) {
    return (s_current_idx > 0);
}

bool undo_can_redo(void) {
    return (s_current_idx >= 0 && s_current_idx < s_undo_count - 1);
}

const char *undo_get_undo_name(void) {
    if (undo_can_undo()) {
        return s_stack[s_current_idx].action_name;
    }
    return NULL;
}

const char *undo_get_redo_name(void) {
    if (undo_can_redo()) {
        return s_stack[s_current_idx + 1].action_name;
    }
    return NULL;
}

bool undo_perform(HWND hwnd_main) {
    if (!undo_can_undo()) return false;

    s_is_performing_undo_redo = true;
    s_current_idx--;
    restore_state(&s_stack[s_current_idx], hwnd_main);
    s_is_performing_undo_redo = false;

    undo_redo_update_menu(hwnd_main);
    return true;
}

bool undo_redo_perform(HWND hwnd_main) {
    if (!undo_can_redo()) return false;

    s_is_performing_undo_redo = true;
    s_current_idx++;
    restore_state(&s_stack[s_current_idx], hwnd_main);
    s_is_performing_undo_redo = false;

    undo_redo_update_menu(hwnd_main);
    return true;
}

static MenuItemVisual s_history_visuals[MAX_UNDO_STACK + 4];

void undo_redo_populate_history_menu(HMENU hhistory) {
    if (!hhistory) return;

    /* 1. Clear all existing items in the submenu */
    while (GetMenuItemCount(hhistory) > 0) {
        DeleteMenu(hhistory, 0, MF_BYPOSITION);
    }

    /* 2. If no actions yet (only initial baseline state 0 or empty) */
    if (s_undo_count <= 1) {
        MenuItemVisual *v = &s_history_visuals[0];
        memset(v, 0, sizeof(*v));
        strncpy(v->name, "(No Action History)", sizeof(v->name) - 1);
        v->shortcut[0] = '\0';
        v->is_bold = false;
        v->is_separator = false;
        AppendMenuA(hhistory, MF_OWNERDRAW | MF_GRAYED, 0, (LPCSTR)v);
        return;
    }

    /* 3. Populate all actions from 1 to s_undo_count - 1 */
    int total_actions = s_undo_count - 1;
    for (int i = 1; i <= total_actions; ++i) {
        bool is_current_marker = (s_current_idx < s_undo_count - 1 && i == s_current_idx + 1);

        MenuItemVisual *v = &s_history_visuals[i];
        memset(v, 0, sizeof(*v));
        snprintf(v->name, sizeof(v->name), "%d. %s",
                 i,
                 s_stack[i].action_name[0] ? s_stack[i].action_name : "Action");
        v->shortcut[0] = '\0'; /* Removed Undo and Redo text on the right side */
        v->id = ID_MENU_ACTION_HISTORY_BASE + i;
        v->is_bold = is_current_marker; /* Distinct bold styling for checked current state */
        v->is_separator = false;
        v->hsubmenu = NULL;

        UINT flags = MF_OWNERDRAW;
        if (is_current_marker) {
            flags |= MF_CHECKED;
        } else {
            flags |= MF_UNCHECKED;
        }

        AppendMenuA(hhistory, flags, (UINT_PTR)v->id, (LPCSTR)v);
    }
}

bool undo_jump_to_index(int target_idx, HWND hwnd_main) {
    if (target_idx < 0 || target_idx >= s_undo_count) return false;
    if (target_idx == s_current_idx) return true;

    s_is_performing_undo_redo = true;
    s_current_idx = target_idx;
    restore_state(&s_stack[s_current_idx], hwnd_main);
    s_is_performing_undo_redo = false;

    undo_redo_update_menu(hwnd_main);
    return true;
}

void undo_redo_update_menu(HWND hwnd_main) {
    if (!hwnd_main || !IsWindow(hwnd_main)) return;
    HMENU hmenu = GetMenu(hwnd_main);
    if (!hmenu) return;

    UINT u_flags = undo_can_undo() ? MF_ENABLED : MF_GRAYED;
    UINT r_flags = undo_can_redo() ? MF_ENABLED : MF_GRAYED;

    /* Update on root menu bar */
    EnableMenuItem(hmenu, ID_MENU_EDIT_UNDO, MF_BYCOMMAND | u_flags);
    EnableMenuItem(hmenu, ID_MENU_EDIT_REDO, MF_BYCOMMAND | r_flags);

    /* Search through all popups to guarantee submenus are updated */
    int count = GetMenuItemCount(hmenu);
    for (int i = 0; i < count; ++i) {
        HMENU sub = GetSubMenu(hmenu, i);
        if (sub) {
            EnableMenuItem(sub, ID_MENU_EDIT_UNDO, MF_BYCOMMAND | u_flags);
            EnableMenuItem(sub, ID_MENU_EDIT_REDO, MF_BYCOMMAND | r_flags);
        }
    }

    /* Refresh Action History Submenu if active */
    if (g_hmenu_action_history && IsMenu(g_hmenu_action_history)) {
        undo_redo_populate_history_menu(g_hmenu_action_history);
    }

    DrawMenuBar(hwnd_main);
}
