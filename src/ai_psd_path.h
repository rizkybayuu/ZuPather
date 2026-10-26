#ifndef AI_PSD_PATH_H
#define AI_PSD_PATH_H

#include "bezier_path.h"
#include "svg_path.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VECTOR_FMT_UNKNOWN = 0,
    VECTOR_FMT_DAT,
    VECTOR_FMT_SVG,
    VECTOR_FMT_AI,         /* Adobe Illustrator PostScript (.ai, .eps) */
    VECTOR_FMT_PDF,        /* PDF or PDF-based Illustrator file */
    VECTOR_FMT_PSD,        /* Adobe Photoshop Document with vector paths (.psd) */
    VECTOR_FMT_AFFINITY    /* Serif Affinity Designer / Photo (.af, .afdesign, .afphoto) */
} VectorFileFormat;

/* Detect vector file format from file path and/or header bytes */
VectorFileFormat vector_path_detect_format(const char *filepath, const uint8_t *header, size_t header_len);

/* Unified Vector Curve Importer:
 * Imports paths from .svg, .ai (Photoshop "Paths to Illustrator", Illustrator EPS/PostScript/PDF),
 * .psd (Photoshop Pen Tool workpaths and vector masks), and .af / .afdesign (Affinity vector files).
 * Extracts curve paths and applies transformations accurately.
 * Returns true if at least one valid path with >= 2 nodes was imported.
 */
bool vector_path_import_from_file(
    const char *filepath,
    BezierPath *out_paths,
    int max_paths,
    int *out_count,
    SvgDimensions *out_dims
);

/* Import curves from an in-memory buffer (AI/PostScript/PDF/PSD/SVG) */
bool vector_path_import_from_memory(
    const uint8_t *data,
    size_t length,
    const char *hint_ext,
    BezierPath *out_paths,
    int max_paths,
    int *out_count,
    SvgDimensions *out_dims
);

/* Export BezierPath array to an Adobe Illustrator PostScript (.ai) file.
 * Compatible with Adobe Illustrator, Adobe Photoshop, Affinity Designer, Inkscape.
 */
bool ai_path_export_to_file(
    const char *filepath,
    const BezierPath *paths,
    int count,
    int canvas_w,
    int canvas_h
);

#ifdef __cplusplus
}
#endif

#endif /* AI_PSD_PATH_H */
