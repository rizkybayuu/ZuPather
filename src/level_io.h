#ifndef LEVEL_IO_H
#define LEVEL_IO_H

#include "zupather_state.h"

void handle_import_path(HWND hwnd);
void handle_export_path(HWND hwnd);
void handle_import_svg_path(HWND hwnd);
void handle_export_svg_path(HWND hwnd);
void get_canvas_base_directory(char *out_path, size_t max_len);
void compact_empty_paths(void);
bool save_canvas_settings_xml(
    const char *folder_path,
    const char *canvas_name,
    const char *canvas_title,
    int canvas_w, int canvas_h,
    const char *img_filename,
    int img_w, int img_h,
    bool img_resized
);
void prune_unregistered_canvas_files(const char *folder_path, const char *safe_name);
bool load_canvas_settings_xml(
    const char *folder_path,
    char *out_name, size_t max_name,
    char *out_title, size_t max_title,
    int *out_w, int *out_h,
    char *out_img_filename, size_t max_img,
    int *out_img_w, int *out_img_h,
    bool *out_img_resized,
    int *out_gx, int *out_gy,
    TreasurePoint *out_coins, int *out_coin_count, int max_coins,
    LevelCutout *out_cutouts, int *out_cutout_count, int max_cutouts,
    LevelSprite *out_sprites, int *out_sprite_count, int max_sprites,
    LevelBackgroundAlpha *out_bgalphas, int *out_bgalpha_count, int max_bgalphas
);
bool find_image_in_folder(const char *folder_path, const char *preferred_name, char *out_filename, size_t max_len);
bool execute_open_canvas_folder(HWND hwnd, const char *folder_path);
typedef enum {
    SAVE_ASSET_BACKGROUND = 0,
    SAVE_ASSET_CUTOUT,
    SAVE_ASSET_SPRITE,
    SAVE_ASSET_BGALPHA
} SaveAssetType;

typedef struct {
    SaveAssetType type;
    int index;                      /* Array index in level objects: 0..count-1 */
    char label[64];                 /* "Background", "Cutout #1", "Sprite #2", "BgAlpha #1" */
    char id_name[64];               /* "level1", "tunnel", "sprite", "quake" */
    char orig_ext[16];              /* "png", "webp", "mp4", "jpg", "gif", "bmp" */
    char source_path[MAX_PATH];
    int width;
    int height;
    bool is_video;
    bool is_animated;
    int frame_count;
    bool can_keep_orig;             /* false if video/bmp, true if zuma native */
    bool keep_orig;                 /* true if keeping original format */
    ImageExportFormat selected_format; /* Target format if !keep_orig */
    bool resize;                    /* Background resize toggle */
    const CanvasImage *img_ref;     /* Pointer to CanvasImage for thumbnail rendering */
    int max_export_frames;          /* Calculated safe export frame limit (<= 0 for uncapped) */
} SaveCanvasAssetItem;

#define MAX_SAVE_CANVAS_ASSETS 64

typedef struct {
    int count;
    SaveCanvasAssetItem items[MAX_SAVE_CANVAS_ASSETS];
} SaveCanvasAssetPlan;

void build_save_canvas_asset_plan(SaveCanvasAssetPlan *out_plan);
bool execute_save_canvas_with_plan(
    HWND hwnd,
    const char *canvas_name,
    const char *canvas_title,
    const SaveCanvasAssetPlan *plan
);
bool execute_save_canvas(HWND hwnd, const char *canvas_name, const char *canvas_title, ImageExportFormat format, bool resize);
bool browse_for_folder(HWND hwnd_owner, char *out_folder, size_t max_len);

/* Zuma Deluxe Game and Levels Directory Management */
bool browse_for_zuma_folder(HWND hwnd_owner, char *out_folder, size_t max_len);
bool validate_zuma_levels_xml(const char *file_path, char *out_error, size_t max_err_len, int *out_level_count);
void handle_set_zuma_folder(HWND hwnd);

#endif /* LEVEL_IO_H */
