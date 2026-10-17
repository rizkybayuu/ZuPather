#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#define ZUPATHER_MEDIA_FILTER "All Supported Media (*.png;*.jpg;*.jpeg;*.bmp;*.dib;*.webp;*.gif;*.tif;*.tiff;*.ico;*.cur;*.img;*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm;*.flv;*.m4v;*.mpg;*.mpeg;*.3gp;*.ts;*.m2ts;*.asf)\0*.png;*.jpg;*.jpeg;*.bmp;*.dib;*.webp;*.gif;*.tif;*.tiff;*.ico;*.cur;*.img;*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm;*.flv;*.m4v;*.mpg;*.mpeg;*.3gp;*.ts;*.m2ts;*.asf\0Supported Images (*.png;*.jpg;*.jpeg;*.bmp;*.dib;*.webp;*.gif;*.tif;*.tiff;*.ico;*.cur;*.img)\0*.png;*.jpg;*.jpeg;*.bmp;*.dib;*.webp;*.gif;*.tif;*.tiff;*.ico;*.cur;*.img\0Supported Videos (*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm;*.flv;*.m4v;*.mpg;*.mpeg;*.3gp;*.ts;*.m2ts;*.asf)\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm;*.flv;*.m4v;*.mpg;*.mpeg;*.3gp;*.ts;*.m2ts;*.asf\0All Files (*.*)\0*.*\0"

typedef enum {
    MEDIA_TYPE_STATIC = 0,
    MEDIA_TYPE_ANIMATED_WEBP,
    MEDIA_TYPE_ANIMATED_GIF,
    MEDIA_TYPE_VIDEO
} MediaType;

typedef struct {
    HBITMAP hbm;        /* 32-bit PBGRA DIBSection */
    HDC     hdc_mem;    /* Compatible DC */
    void   *bits;       /* Raw pixel bits */
    int     duration_ms;/* Duration in milliseconds */
} ImageAnimationFrame;

typedef struct VideoPlaybackCtx VideoPlaybackCtx;
typedef struct WebPPlaybackCtx WebPPlaybackCtx;

typedef struct {
    HBITMAP hbm;                /* 32-bit PBGRA DIBSection currently displayed */
    HDC     hdc_mem;            /* Compatible memory DC with hbm selected */
    void   *bits;               /* Pixel bits pointer of hbm */
    int     width;              /* Image width in pixels */
    int     height;             /* Image height in pixels */
    char    filepath[MAX_PATH]; /* Full path to image file */
    char    filename[MAX_PATH]; /* Filename with extension (e.g. "spiral.png") */
    char    display_name[MAX_PATH]; /* Filename without extension (e.g. "spiral") */

    /* Animation and Video Playback metadata */
    MediaType media_type;
    bool      is_animated;
    bool      is_video;
    int       frame_count;
    int       cur_frame_idx;
    DWORD     last_frame_tick;

    /* Multi-frame buffer for animated GIF */
    ImageAnimationFrame *frames;

    /* WebP streaming playback context for Animated WebP */
    WebPPlaybackCtx     *webp_ctx;

    /* Video context for Media Foundation video streaming */
    VideoPlaybackCtx    *video_ctx;

    /* Frame 0 still frame copy for fallback / static export */
    HBITMAP   first_frame_hbm;
    HDC       first_frame_hdc;
    void     *first_frame_bits;

    /* Pristine unscaled 32-bit PBGRA pixels for non-destructive resizing */
    void     *orig_bits;
    int       orig_width;
    int       orig_height;
} CanvasImage;

typedef enum {
    IMG_EXPORT_KEEP_ORIGINAL = 0,
    IMG_EXPORT_PNG,
    IMG_EXPORT_WEBP,
    IMG_EXPORT_JPG,
    IMG_EXPORT_GIF,
    IMG_EXPORT_BMP
} ImageExportFormat;

/* Canvas Image Placement & Alignment Enums */
typedef enum {
    CANVAS_FIT_ORIGINAL = 0,
    CANVAS_FIT_FIT,      /* Letterbox / contain */
    CANVAS_FIT_FILL,     /* Crop / cover */
    CANVAS_FIT_STRETCH   /* Stretch to canvas dimensions */
} CanvasFitMode;

typedef enum {
    CANVAS_ALIGN_TOP_LEFT = 0,
    CANVAS_ALIGN_TOP_CENTER,
    CANVAS_ALIGN_TOP_RIGHT,
    CANVAS_ALIGN_MIDDLE_LEFT,
    CANVAS_ALIGN_MIDDLE_CENTER,
    CANVAS_ALIGN_MIDDLE_RIGHT,
    CANVAS_ALIGN_BOTTOM_LEFT,
    CANVAS_ALIGN_BOTTOM_CENTER,
    CANVAS_ALIGN_BOTTOM_RIGHT
} CanvasAlignment;

#define CANVAS_ALIGN_MID_LEFT    CANVAS_ALIGN_MIDDLE_LEFT
#define CANVAS_ALIGN_CENTER      CANVAS_ALIGN_MIDDLE_CENTER
#define CANVAS_ALIGN_MID_RIGHT   CANVAS_ALIGN_MIDDLE_RIGHT

void get_canvas_image_dest_rect(
    int canvas_w, int canvas_h,
    int img_src_w, int img_src_h,
    CanvasFitMode fit_mode,
    CanvasAlignment alignment,
    int *out_x, int *out_y,
    int *out_w, int *out_h
);

/* Initialize COM & Media Foundation subsystem */
void image_subsystem_init(void);

/* Shutdown COM & Media Foundation subsystem */
void image_subsystem_shutdown(void);

/* Load any image or video into 32-bit PBGRA DIBSection */
bool image_load_from_file(const char *filepath, CanvasImage *out_img);

/* Load image from raw in-memory buffer (PNG, JPG, BMP, etc.) */
bool image_load_from_memory(const void *data, size_t size, CanvasImage *out_img);

/* Load image from compiled executable RCDATA resource */
bool image_load_from_resource(int res_id, CanvasImage *out_img);

/* Advance animation frame by checking elapsed time (returns true if canvas redraw is needed) */
bool image_advance_animation(CanvasImage *img);

/* Free image and media playback resources */
void image_free(CanvasImage *img);

/* Resize image in-place by resampling pristine orig_bits into a new DIBSection */
bool image_resize_in_place(CanvasImage *img, int new_w, int new_h);

/* Extract filename without extension */
void image_get_basename_no_ext(const char *filepath, char *out_basename, size_t max_len);

/* Extract extension without dot in lowercase */
void image_get_extension(const char *filepath, char *out_ext, size_t max_len);

/* Helper checking if extension is one of 4 Zuma native formats (.png, .webp, .jpg, .gif) */
bool image_is_zuma_native_ext(const char *ext_or_filepath);

/* Helper checking if extension is a video format (.mp4, .mkv, .avi, .mov, .wmv, .webm, etc.) */
bool image_is_video_ext(const char *ext_or_filepath);

/* Show video load warning dialog (Audio omitted, Zuma Deluxe vanilla does not support video) */
void show_video_load_warning(HWND hwnd_parent);

/* Show WebP / ZuCompleluxe patch notice dialog */
bool show_webp_patch_warning_ex(HWND hwnd_parent, bool allow_cancel);
void show_webp_patch_warning(HWND hwnd_parent);

/* Show warning dialog when video / animated media is converted to static format (*.png, *.jpg, *.gif) */
bool show_animation_flatten_warning(HWND hwnd_parent, const char *asset_names, bool allow_cancel);

/* Maximum combined RAM budget for all decoded animated WebP frames in Zuma Deluxe (32-bit process) */
#define CANVAS_MAX_SAFE_ANIMATION_RAM_BYTES (512ULL * 1024ULL * 1024ULL) /* 512 MB */

/* Legacy fallback frame limit */
#define MAX_SAFE_WEBP_FRAMES 900

/* Probe video stream to retrieve true duration (seconds), FPS, and total frame count */
bool get_video_stream_info(const char *filepath, double *out_duration_sec, int *out_fps, int *out_total_frames);

/* Export or copy image to destination path (with optional frame limit) */
bool image_export_to_file_ex(
    const CanvasImage *img,
    const char *dest_filepath,
    ImageExportFormat format,
    bool resize,
    int target_w,
    int target_h,
    int max_frames
);

bool image_export_to_file(
    const CanvasImage *img,
    const char *dest_filepath,
    ImageExportFormat format,
    bool resize,
    int target_w,
    int target_h
);

/* Forward declaration for save progress notifications */
void update_save_progress(const char *status_msg, int pct);

#endif /* IMAGE_LOADER_H */

