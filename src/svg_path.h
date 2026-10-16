#ifndef SVG_PATH_H
#define SVG_PATH_H

#include "bezier_path.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float view_w;
    float view_h;
    bool has_viewbox;
} SvgDimensions;

/* Parse SVG path data from memory string or buffer.
 * Extracts all <path d="..."> elements and subpaths.
 * out_paths: array of BezierPath of capacity max_paths.
 * out_count: pointer to store number of successfully imported paths.
 * out_dims: optional struct to receive viewBox/width/height if present in <svg>.
 * Returns true if at least one valid path with >= 2 nodes was imported.
 */
bool svg_path_import_from_memory(
    const char *svg_content,
    size_t length,
    BezierPath *out_paths,
    int max_paths,
    int *out_count,
    SvgDimensions *out_dims
);

/* Load and parse an SVG file from disk.
 * Returns true if at least one path with >= 2 nodes was imported.
 */
bool svg_path_import_from_file(
    const char *filepath,
    BezierPath *out_paths,
    int max_paths,
    int *out_count,
    SvgDimensions *out_dims
);

/* Export BezierPath array to an SVG file with a transparent canvas.
 * paths: array of BezierPath to export.
 * count: number of paths.
 * canvas_w, canvas_h: canvas dimensions for width, height, and viewBox.
 * Returns true on success.
 */
bool svg_path_export_to_file(
    const char *filepath,
    const BezierPath *paths,
    int count,
    int canvas_w,
    int canvas_h
);

/* Export BezierPath array to a heap-allocated SVG XML string (caller must free()). */
char *svg_path_export_to_memory(
    const BezierPath *paths,
    int count,
    int canvas_w,
    int canvas_h
);

#ifdef __cplusplus
}
#endif

#endif /* SVG_PATH_H */
