#ifndef CANVAS_RENDER_H
#define CANVAS_RENDER_H

#include "zupather_state.h"
#include "hud_overlay.h"

void render_scene(HWND hwnd, HDC hdc, int client_w, int client_h);
void update_object_marquee_selection(float min_x, float min_y, float max_x, float max_y);
bool hit_test_bezier(
    int mx, int my,
    const RECT *rc_canvas,
    int *out_path_idx,
    int *out_node_idx,
    int *out_type /* 0=anchor, 1=hout, 2=hin */
);

bool hit_test_bezier_segment(
    int mx, int my,
    const RECT *rc_canvas,
    float max_screen_dist,
    int *out_path_idx,
    int *out_seg_idx,
    float *out_t,
    float *out_canvas_x,
    float *out_canvas_y
);

CanvasHandleType hit_test_canvas_handles(int client_w, int work_h, int sx, int sy);
void render_canvas_transform_handles(HDC hdc, int client_w, int work_h, const ThemeColors *th);

void render_splash_elements(HDC mem_dc, int view_w, int view_h, const ThemeColors *th, int offset_y, bool is_workspace);

#endif /* CANVAS_RENDER_H */
