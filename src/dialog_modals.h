#ifndef DIALOG_MODALS_H
#define DIALOG_MODALS_H

#include "zupather_state.h"
#include "level_io.h"

void show_new_canvas_dialog(HWND parent_hwnd);
void show_save_canvas_dialog(HWND parent_hwnd);
void show_open_canvas_dialog(HWND parent_hwnd);
void show_manage_levels_dialog(HWND parent_hwnd);
void prompt_change_background_image(HWND hwnd);
bool show_themed_notice_dialog_ex(HWND parent_hwnd, const char *title, const char *header, const char *body, int dlg_w, int dlg_h, bool has_cancel);
void show_themed_notice_dialog(HWND parent_hwnd, const char *title, const char *header, const char *body, int dlg_w, int dlg_h);
bool show_themed_confirm_dialog(HWND parent_hwnd, const char *title, const char *header, const char *body, bool is_danger);
int show_themed_choice_dialog(HWND parent_hwnd, const char *title, const char *header, const char *body, const char *btn1_label, const char *btn2_label, const char *btn3_label);
int show_themed_choice_dialog_ex(HWND parent_hwnd, const char *title, const char *header, const char *body, const char *btn1_label, const char *btn2_label, const char *btn3_label, int dlg_w, int dlg_h);
void show_saving_canvas_modal(HWND parent_hwnd, const char *canvas_name, const char *canvas_title, const SaveCanvasAssetPlan *plan);
void show_opening_canvas_modal(HWND parent_hwnd, const char *folder_path);
typedef struct {
    int plan_index;
    SaveAssetType type;
    char label[64];
    char id_name[64];
    char filename[128];
    int target_w;
    int target_h;
    int orig_fps;
    int orig_frames;
    double orig_duration_sec;
    uint64_t ram_per_frame;
    uint64_t orig_ram_bytes;
    int capped_frames;
    double capped_duration_sec;
    uint64_t capped_ram_bytes;
    bool is_capped;
} CanvasAnimationAssetStat;

typedef struct {
    int count;
    CanvasAnimationAssetStat assets[MAX_SAVE_CANVAS_ASSETS];
    uint64_t total_orig_ram_bytes;
    uint64_t total_capped_ram_bytes;
    uint64_t ram_budget_bytes;
    bool exceeds_budget;
} CanvasAnimationBudgetReport;

void calculate_canvas_animation_budget(const SaveCanvasAssetPlan *plan, CanvasAnimationBudgetReport *report);
bool check_and_show_webp_limit_warning(HWND hwnd_parent, SaveCanvasAssetPlan *plan, bool allow_cancel);

/* Show Extra Paths / ZuCompleluxe compatibility notice dialog (when saving 4 or more curve paths) */
bool show_extra_paths_warning_ex(HWND hwnd_parent, int path_count, bool allow_cancel);
void show_extra_paths_warning(HWND hwnd_parent, int path_count);

/* Show 10-path limit warning notice dialog (drawing or importing) */
bool show_path_limit_warning_ex(HWND hwnd_parent, int total_count, bool is_import);
void show_path_limit_warning(HWND hwnd_parent);
void show_import_path_limit_warning(HWND hwnd_parent, int canvas_count, int imported_count, bool replace);

/* Bidirectional Canvas Title Synchronization */
void sync_save_dialog_title_if_open(const char *title, HWND sender_hwnd);
void show_splash_screen_dialog(HWND parent_hwnd);

#endif /* DIALOG_MODALS_H */
