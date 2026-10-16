#ifndef HUD_OVERLAY_H
#define HUD_OVERLAY_H

#include "zupather_state.h"

int get_guide_content_height(void);
int get_guide_hud_max_scroll(int work_h);
bool get_guide_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h);
int get_attr_content_height(void);
int get_attr_hud_max_scroll(int work_h);
bool get_attr_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h);
void get_top_right_hud_rect(int client_w, int work_h, RECT *rc);
void get_top_left_hud_rect(int work_h, RECT *rc);
void render_curve_submode_huds(HDC mem_dc, int client_w, int work_h, const ThemeColors *th);

int get_overlay_guide_content_height(void);
int get_overlay_guide_hud_max_scroll(int work_h);
bool get_overlay_guide_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h);
void get_top_left_overlay_guide_hud_rect(int work_h, RECT *rc);
int get_overlay_tools_content_height(void);
int get_overlay_tools_hud_max_scroll(int work_h);
bool get_overlay_tools_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h);
void get_top_right_overlay_hud_rect(int client_w, int work_h, RECT *rc);
void render_overlay_guide_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th);
void render_overlay_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th);

int get_element_guide_content_height(void);
int get_element_guide_hud_max_scroll(int work_h);
bool get_element_guide_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h);
void get_top_left_element_guide_hud_rect(int work_h, RECT *rc);
void render_element_guide_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th);
int get_element_tools_content_height(void);
int get_element_tools_hud_max_scroll(int work_h);
bool get_element_tools_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h);
void get_top_right_element_hud_rect(int client_w, int work_h, RECT *rc);
void render_element_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th);

int get_canvas_guide_content_height(void);
int get_canvas_guide_hud_max_scroll(int work_h);
bool get_canvas_guide_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h);
void get_top_left_canvas_guide_hud_rect(int work_h, RECT *rc);
void render_canvas_guide_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th);

int get_canvas_tools_content_height(void);
int get_canvas_tools_hud_max_scroll(int work_h);
bool get_canvas_tools_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h);
void get_top_right_canvas_hud_rect(int client_w, int work_h, RECT *rc);
void render_canvas_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th);

int get_preview_guide_content_height(void);
int get_preview_guide_hud_max_scroll(int work_h);
bool get_preview_guide_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h);
void get_top_left_preview_guide_hud_rect(int work_h, RECT *rc);
void render_preview_guide_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th);

int get_preview_hud_content_height(void);
int get_preview_hud_title_box_y(void);
int get_preview_hud_max_scroll(int work_h);
bool get_preview_hud_scroll_geometry(int work_h, int *out_track_y, int *out_track_h, int *out_thumb_y, int *out_thumb_h);
void get_top_right_preview_hud_rect(int client_w, int work_h, RECT *rc);
void render_preview_hud(HDC mem_dc, int client_w, int work_h, const ThemeColors *th);

typedef enum {
    MAP_ATTR_HIT_NONE = 0,
    MAP_ATTR_HIT_PLAY_BTN,
    MAP_ATTR_HIT_CHECK_SKULLROT,
    MAP_ATTR_HIT_CHECK_SPACE,
    MAP_ATTR_HIT_CHECK_DRAWCURVE,
    MAP_ATTR_HIT_CHECK_DRAWTUNNELS,
    MAP_ATTR_HIT_CHECK_CURVECOLOR,
    MAP_ATTR_HIT_CHECK_BGCOLOR,
    MAP_ATTR_HIT_CHECK_DESTROYALL,
    MAP_ATTR_HIT_CHECK_HUEROT,
    MAP_ATTR_HIT_CHECK_TREASUREFREQ,
    MAP_ATTR_HIT_CHECK_PARTIME
} MapAttrHitTarget;

MapAttrHitTarget hit_test_map_attr_hud(int rel_x, int rel_y, int tr_w);
void hide_all_map_attr_edits(void);
void sync_all_map_attr_edits_from_state(void);
MapAttrHoverItem hit_test_map_attr_hover(int rel_x, int rel_y, int tr_w, int *out_anchor_y);
void get_map_attr_hud_layout(int *out_box_y, int *out_skull_y, int *out_space_y, int *out_sep3_y, int *out_sum_y, int *out_sep4_y, int *out_dev_y, int out_dev_items_y[8], int *out_total_h);
void get_map_attr_item_info(MapAttrHoverItem item, const char **out_title, const char **out_tag, const char **out_desc, const char **out_status);
void render_map_attr_tooltip(HDC mem_dc, int client_w, int work_h, const ThemeColors *th, MapAttrHoverItem hover_item, int anchor_y);

#endif /* HUD_OVERLAY_H */
