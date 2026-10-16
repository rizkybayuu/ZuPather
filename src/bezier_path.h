#ifndef BEZIER_PATH_H
#define BEZIER_PATH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Node constraint type for vector editing */
typedef enum {
    NODE_CORNER = 0,    /* Sharp corner / no active handles */
    NODE_SMOOTH,        /* Collinear handles, independent lengths */
    NODE_SYMMETRIC,     /* Collinear handles, strictly equal lengths */
    NODE_CUSP           /* Independent / broken handles (Alt-drag) */
} BezierNodeType;

/* Single anchor node with unique monotonic ID (attributes removed) */
typedef struct {
    uint32_t id;            /* Monotonic stable node identifier */
    float x, y;             /* Anchor position in canvas world coordinates */
    float hin_x, hin_y;     /* Incoming handle offset relative to anchor (0,0 = inactive) */
    float hout_x, hout_y;   /* Outgoing handle offset relative to anchor (0,0 = inactive) */
    BezierNodeType type;    /* Handle constraint rule */
    bool selected;          /* UI selection state */
} BezierNode;

/* Attribute span defined parametrically along curve arc length */
typedef struct {
    uint32_t node_c_id;     /* Anchor ID for start reference */
    float off_c;            /* Distance forward from anchor C, >= 0 */
    uint32_t node_d_id;     /* Anchor ID for end reference */
    float off_d;            /* Distance backward from anchor D, >= 0 */
    uint8_t in_tunnel;      /* 0 or 1 */
    uint8_t priority;       /* 0 (underpass) or 1 (overpass) */
    bool has_tunnel;        /* True if this span sets in_tunnel */
    bool has_prio;          /* True if this span sets priority */
} CurveSpan;

/* Baked span cache for instantaneous O(1) sampling */
typedef struct {
    double s_c;
    double s_d;
    uint8_t tunnel;
    uint8_t prio;
    bool has_tunnel;
    bool has_prio;
    bool active;            /* True if valid and width > 0 */
} BakedSpan;

/* Single unified high-resolution path cache (tolerance 0.05 px) */
typedef struct {
    float *x;
    float *y;
    double *S;              /* Cumulative chord length at each vertex */
    int count;
    int capacity;
    double total_length;

    /* Anchor arc-length coordinates */
    double *node_s;         /* Arc-length position s of each anchor node */
    int node_count;

    /* Pre-baked span boundaries */
    BakedSpan *baked_spans;
    int baked_span_count;
    int baked_span_capacity;

    bool is_dirty;
} PathCache;

/* Complete Bezier path definition */
typedef struct {
    char name[64];          /* Curve identifier (e.g. "coaster", "curve2") */
    BezierNode *nodes;
    int node_count;
    int node_capacity;
    bool is_closed;
    int skullrot;           /* Skull rotation in degrees (-1 = auto from path end tangent) */

    /* Monotonic ID generator for nodes */
    uint32_t next_node_id;

    /* Dynamic Attribute Spans (Painter's Algorithm Precedence) */
    CurveSpan *spans;
    int span_count;
    int span_capacity;

    /* Unified High-Resolution Polyline Cache */
    PathCache cache;
} BezierPath;

/* Resampled point ready for runtime game engine or .dat serialization */
typedef struct {
    float x, y;
    bool in_tunnel;
    uint8_t priority;
} ResampledPathPoint;

/* Path management functions */
void bezier_path_init(BezierPath *path, const char *name);
void bezier_path_free(BezierPath *path);
void bezier_path_clear(BezierPath *path);
bool bezier_path_add_node(BezierPath *path, const BezierNode *node);
bool bezier_path_insert_node(BezierPath *path, int index, const BezierNode *node);
bool bezier_path_remove_node(BezierPath *path, int index);
bool bezier_path_split_segment(BezierPath *path, int seg_idx, float t);
int  bezier_path_find_node_by_id(const BezierPath *path, uint32_t id);

/* Track transformation and topology operations */
bool bezier_path_reverse(BezierPath *path);
bool bezier_path_split_at_node(BezierPath *orig_path, int split_node_idx, BezierPath *out_new_path);
bool bezier_path_join_paths(BezierPath *path_a, int endpoint_a, BezierPath *path_b, int endpoint_b);

/* Span management functions */
bool bezier_path_add_span(BezierPath *path, const CurveSpan *span);
bool bezier_path_remove_span(BezierPath *path, int index);
void bezier_path_clear_spans(BezierPath *path);
void bezier_path_prune_collapsed_spans(BezierPath *path);
bool bezier_path_add_span_from_s(
    BezierPath *path,
    double s_start,
    double s_end,
    bool has_tunnel,
    uint8_t in_tunnel,
    bool has_prio,
    uint8_t priority
);

/* Attribute evaluation */
void bezier_path_attrs_at(BezierPath *path, double s, uint8_t *out_tunnel, uint8_t *out_priority);
void bezier_path_cache_attrs_at(const PathCache *cache, double s, uint8_t *out_tunnel, uint8_t *out_priority);

/* Locality window projection for Variable Mode 1D drag */
double project_s_window(
    BezierPath *path,
    float px, float py,
    double s_prev,
    double window_px,
    double *out_dist_sq
);

/* Path cache operations */
void path_cache_init(PathCache *cache);
void path_cache_free(PathCache *cache);
void path_cache_invalidate(BezierPath *path);
bool path_cache_update(BezierPath *path);

/* Handle alignment & conversion helpers */
void bezier_node_update_handles(BezierNode *node, bool out_was_dragged);
void bezier_node_make_corner(BezierNode *node);
void bezier_path_make_corner_node(BezierPath *path, int index);
void bezier_path_smooth_node(BezierPath *path, int index);

/* Conversion between Catmull-Rom control points and Cubic Bezier handles */
/* alpha: 0.0 = Uniform (Zuma Deluxe default), 0.5 = Centripetal, 1.0 = Chordal */
void catmull_rom_to_bezier(
    float p0x, float p0y,
    float p1x, float p1y,
    float p2x, float p2y,
    float p3x, float p3y,
    float alpha,
    float *out_b1x, float *out_b1y,
    float *out_b2x, float *out_b2y
);

/* Curve flattening & 1.0 px arc-length resampling */
ResampledPathPoint* bezier_path_resample(
    BezierPath *path,
    double step_size,          /* 1.0 px for standard Zuma Deluxe engine */
    double flatten_tolerance,  /* 0.05 px for export, ~0.3 / zoom for preview */
    int *out_point_count
);

/* File I/O: Zuma Deluxe Binary CURV (.dat) */
typedef enum {
    DAT_ERR_OK = 0,
    DAT_ERR_INVALID_PARAM,
    DAT_ERR_FILE_NOT_FOUND,
    DAT_ERR_INVALID_MAGIC,          /* Not "CURV" magic */
    DAT_ERR_UNSUPPORTED_VERSION,    /* Version is not 1 or 2 */
    DAT_ERR_INVALID_EDIT_TYPE,      /* Edit type is not 1 */
    DAT_ERR_INVALID_CP_COUNT,       /* Control points < 2 or buffer size mismatch */
    DAT_ERR_READ_CP_FAILED,         /* Corrupted control points payload */
    DAT_ERR_ALLOC_FAILED
} DatImportError;

const char* dat_import_error_to_string(DatImportError err);

bool bezier_path_export_dat(
    BezierPath *path,
    const char *out_filepath
);

bool bezier_path_import_dat_ex(
    BezierPath *path,
    const char *in_filepath,
    float alpha, /* 0.0 = uniform */
    DatImportError *out_error
);

bool bezier_path_import_dat(
    BezierPath *path,
    const char *in_filepath,
    float alpha /* 0.0 = uniform */
);

/* File I/O: ZuPather Sidecar (.zupath) */
bool bezier_path_save_zupath(
    const BezierPath *paths,
    int path_count,
    const char *out_filepath
);

bool bezier_path_load_zupath(
    BezierPath **out_paths,
    int *out_path_count,
    const char *in_filepath
);

#ifdef __cplusplus
}
#endif

#endif /* BEZIER_PATH_H */
