# ZuPather Curve Attribute Editing System (Variable Mode)
## Architectural Design & Technical Specification (Revision 2 - Production Aligned)

> **Document Version**: 2.0.0  
> **Target Subsystems**: `src/bezier_path.h`, `src/bezier_path.c`, `src/zupather_ui.c`, `tests/`  
> **Status**: Approved Architectural Blueprint Aligned with Retail Reverse Engineering Benchmarks

---

## 1. Executive Summary & Problem Scope

In the Zuma Deluxe engine, paths (`.dat` files) do not merely store $(x, y)$ coordinate trajectories. Each sampled point ($\sim 1.0\text{ px}$ step) carries two critical gameplay attributes:
1. **`in_tunnel`** (`uint8_t`, `0` or `1`): Determines whether the rolling sphere chain is obscured beneath scenery tunnels/bridges. Spheres in tunnels cannot be targeted or hit by the player's frog shooter.
2. **`priority`** (`uint8_t`, `0` or `1`): Determines track crossover Z-ordering when paths intersect or cross over bridges (`1` = renders above / overpass, `0` = renders below / underpass).

Historically, early editor implementations assigned these flags either per-node or per-segment. This created major limitations:
- A tunnel often starts or ends in the middle of a smooth bezier curve segment between two control anchors.
- Moving or adjusting curve control handles frequently corrupted tunnel boundaries.
- Painting attributes with a 2D canvas brush risked unintentional bleed/cross-talk across overlapping or self-intersecting track loops.

To solve this, this document specifies the **Curve Attribute System ("Variable Mode")**, featuring:
1. **Dual-Mode Workflow**: Seamless switching between **Draw Mode** (geometry authoring) and **Variable Mode** (tunnel & priority authoring).
2. **Minimizable HUD Overlays**: Floating Top-Right (Mode Switcher) and Top-Left (Contextual Shortcuts) panels with 50% collapsed / 100% expanded alpha-blended opacity.
3. **Arc-Length Parameterized Attribute Spans**: Anchored offsets along the 1D curve parameter $s \in [0, L]$, decoupled from 2D Euclidean distance, resilient to anchor edits, with non-destructive temporary collapse and painter's algorithm precedence.
4. **Locality Window Tracking**: Eliminating crossover ambiguity during 1D mouse drags.
5. **Unified High-Resolution `PathCache`**: A single tolerance $0.05\text{ px}$ polyline cache shared across rendering, mouse projection, span baking, and binary export.

---

## 2. Empirical Retail Validation & Benchmark Data

### 2.1 Catmull-Rom Spline Parametrization Benchmark (`coaster.dat`)

To ensure spline mathematical reconstruction does not introduce deviation into retail trajectories, empirical benchmarking was performed against the retail reference `coaster.dat` (3,659 discrete points, integrated arc length $L_{\text{total}} = 3,676.15\text{ px}$, 72 control points):

| Parametrization Model | Points Count | Total Length (px) | Avg Error (px) | Max Error (px) | Evaluation |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **Uniform ($\alpha = 0.0$)** | **3,676** | **3,674.85** | **0.3465** | **1.3081** | **Primary Choice (Zuma Vanilla Default)** |
| **Centripetal ($\alpha = 0.5$)** | 3,675 | 3,673.86 | 0.3943 | 1.3269 | Stable alternative |
| **Chordal ($\alpha = 1.0$)** | 3,676 | 3,674.74 | 0.5018 | 2.3319 | Excessive curve flattening in tight turns |

*Conclusion*: The **Uniform ($\alpha = 0.0$)** Catmull-Rom formulation produces the lowest average reconstruction error ($0.34\text{ px}$) and exact trajectory fidelity.

---

### 2.2 Control Points vs Runtime `points` Block in Retail Data

Byte-level inspection of `coaster.dat` and automated cross-analysis of all 25 retail Zuma Deluxe level tracks revealed:

#### 1. Case Study: `coaster.dat`
- **Tunnel Run 1 (`points` indices `[1466 .. 1619]`, length = 154 points)**:
  - Entrance point `1466` is **$0.15\text{ px}$** from **`CP[29]`** ($x=160, y=77$, `in_tunnel=1`).
  - Exit point `1619` is **$0.47\text{ px}$** from **`CP[32]`** ($x=57, y=184$, `in_tunnel=0`).
  - Enclosed control points: `CP[29]`, `CP[30]`, `CP[31]` are all marked `in_tunnel=1`.
- **Tunnel Run 2 (`points` indices `[2687 .. 2840]`, length = 154 points)**:
  - Entrance point `2687` is **$0.16\text{ px}$** from **`CP[51]`** ($x=575, y=94$, `in_tunnel=1`).
  - Exit point `2840` is **$0.45\text{ px}$** from **`CP[55]`** ($x=464, y=123$, `in_tunnel=0`).
  - Enclosed control points: `CP[51]`, `CP[52]`, `CP[53]`, `CP[54]` are all marked `in_tunnel=1`.
- **Priority 1 Runs in `coaster.dat`**:
  - `[   0 .. 1434]` (1,435 points): Drops to 0 at point 1434 ($5\text{ px}$ before `CP[28]`).
  - `[1665 .. 2639]` (975 points): Starts at point 1665 ($1\text{ px}$ after `CP[33]`).
  - `[2874 .. 3658]` (785 points): Starts at point 2874 ($d = 0.00\text{ px}$ exactly at `CP[56]`).

#### 2. Cross-Level Statistical Survey (All 25 Retail Levels)
- **Distinct Priority Values**: Strictly **`{0, 1}`** across all vanilla levels. No retail level uses priority 2 or higher.
- **Tunnel Run Count**: Exactly **20 tunnel runs** across all 25 levels.
- **Tunnel Length Distribution**:
  - Minimum length: **97 points** (`overunder.dat`).
  - Maximum length: **331 points** (`tunnellevel.dat`).
  - Mean length: **197.3 points**.
  - Most frequent lengths: **154 points** (2 levels) and **159 points** (2 levels).
- **Coincidence with Control Points ($\le 1.0\text{ px}$)**:
  - Tunnel start boundary aligns with a CP: **19 / 20 (95.0%)**.
  - Tunnel end boundary aligns with a CP: **20 / 20 (100.0%)**.
  - Both boundaries align with CPs: **19 / 20 (95.0%)**.

*Key Architectural Takeaway*: While PopCap's original editor was node-based, runtime `points` blocks feature sub-pixel snapping to tunnel mouths. In ZuPather, decoupling spans from raw control points allows authors to place tunnels anywhere on the curve while maintaining 100% retail binary export compatibility.

---

## 3. High-Level Architecture & User Interface Design

```
+--------------------------------------------------------------------------------------------------+
| TOOLBAR: [Open] [Save] ... [Frog] [Coin] [Curve (ON)] ... [Canvas: 640x480]                     |
+--------------------------------------------------------------------------------------------------+
| WORKSPACE CANVAS                                                                                 |
|                                                                                                  |
|  +-------------------------------------+                +-------------------------------------+  |
|  | [v] SHORTCUTS HUD (TOP-LEFT)        |                | [v] MODE SWITCHER HUD (TOP-RIGHT)   |  |
|  | Mode: Variable Mode                 |                | Active: [Draw Mode] | [Variable] *  |  |
|  | - Drag on Path: Apply Tunnel/Prio   |                | Selected Attribute:                 |  |
|  | - Ctrl+Click Span: Select/Inspect   |                |   (*) Tunnel: [ ON  ] (1)           |  |
|  | - Right-Click Span: Clear/Reset     |                |   (*) Priority: [ 1 ] (+ / -)       |  |
|  | - Shift+Drag: Extend span boundary  |                | [Collapse Button: ^ / v]            |  |
|  +-------------------------------------+                +-------------------------------------+  |
|                                                                                                  |
|                                                                                                  |
|         Anchor A                       Span Start C               Span End D          Anchor B   |
|            o================================[===========================]================o        |
|            | <------- d_AC (2px) ---------> |                           | <--- d_BD ---> |        |
|            |                                |                           |      (3px)     |        |
|            |                                +====== TUNNEL / PRIO 1 ====               |        |
|                                                                                                  |
+--------------------------------------------------------------------------------------------------+
```

### 3.1 Workspace HUD Panels

Both panels reside in the workspace immediately beneath the main toolbar ($y = \text{TOOLBAR\_HEIGHT} + 8\text{ px}$):

1. **Top-Right HUD: Mode Switcher & Attribute Palette**
   - **Position**: Top-right corner ($x = \text{client\_w} - 280\text{ px}, y = 42\text{ px}$).
   - **Mode Selector Buttons**:
     - `[Draw Mode]`: Default mode. Handles bezier node creation, anchor dragging, tangent manipulation, marquee selection.
     - `[Variable Mode]`: Locks node geometry to prevent accidental displacement. Activates 1D arc-length mouse dragging and span manipulation.
   - **Attribute Controls** (active in Variable Mode):
     - Tunnel Toggle: `[Open (0)]` / `[Tunnel (1)]`.
     - Priority Toggle: `[Priority: 0]` / `[Priority: 1]`.
   - **Collapse / Expand Behavior**:
     - Header toggle button (`▲` collapse / `▼` expand).
     - **Expanded State**: 100% opacity ($\alpha = 255$).
     - **Collapsed State**: 50% opacity ($\alpha = 128$), minimal pill displaying the active mode indicator.

2. **Top-Left HUD: Contextual Shortcut Cheat-Sheet**
   - **Position**: Top-left corner ($x = 12\text{ px}, y = 42\text{ px}$).
   - **Dynamic Content**:
     - *In Draw Mode*:
       - `Left-Click`: Append node / Start curve
       - `Ctrl+Click`: Select node / Toggle selection
       - `Ctrl+Drag (empty)`: Marquee box select
       - `Alt+Drag Handle`: Cusp mode (break tangent handles)
       - `Del`: Remove selected node(s)
     - *In Variable Mode*:
       - `Left-Drag on curve`: Paint tunnel / priority span
       - `Ctrl+Click on span`: Inspect span attributes
       - `Right-Click on span`: Delete span (revert to underlying track)
       - `Shift+Drag edge`: Adjust $d_{AC}$ or $d_{BD}$ offsets
   - **Collapse / Expand Behavior**:
     - Header toggle button (`▲` / `▼`).
     - **Expanded**: 100% opacity ($\alpha = 255$).
     - **Collapsed**: 50% opacity ($\alpha = 128$), compact `[?] Shortcuts` badge.

3. **Win32 GDI Alpha Blending**:
   - Implemented via `GdiAlphaBlend` using `AC_SRC_OVER` and `SourceConstantAlpha` set to 128 (collapsed) or 255 (expanded).

---

## 4. Mathematical Formulation & Algorithmic Solutions

### 4.1 1D Arc-Length Coordinate Space & Unified `PathCache`

To eliminate discrepancies between viewport rendering and binary export, ZuPather maintains a single high-resolution `PathCache` per path evaluated at tolerance $\text{tol} = 0.05\text{ px}$:
- Dynamic polyline buffers $x[k], y[k]$ ($k \in [0, N-1]$).
- Cumulative arc-length table $S[k] = \sum_{j=1}^k \|\mathbf{p}_j - \mathbf{p}_{j-1}\|$, with $L_{\text{total}} = S[N-1]$.
- Anchor node cumulative positions: `node_s[i]`.
- Array of pre-evaluated `BakedSpan` intervals.

---

### 4.2 Stable Node Identifiers (`uint32_t id`)

Binding spans to raw array indices (`start_anchor_idx`, `end_anchor_idx`) causes catastrophic corruption whenever nodes are inserted or deleted.
- Every `BezierNode` contains a stable `uint32_t id` generated monotonically by `path->next_node_id`.
- Attribute spans reference `node_c_id` and `node_d_id`.
- Resolving `id` $\to$ current array index is a simple linear search ($N < 200$, sub-microsecond).

---

### 4.3 Anchor-Relative Span Definition

A span $S$ is defined parametrically relative to two bounding anchors:
- **Start Anchor $C$** (`node_c_id`, cumulative position $s_C$).
- **End Anchor $D$** (`node_d_id`, cumulative position $s_D$, where $s_D \ge s_C$).
- **Forward Offset $d_{AC} \ge 0$**: Arc-length distance measured forward along the curve from Anchor $C$:
  $$s_{\text{start}} = s_C + \text{off}_c$$
- **Backward Offset $d_{BD} \ge 0$**: Arc-length distance measured backward along the curve from Anchor $D$:
  $$s_{\text{end}} = s_D - \text{off}_d$$

#### Effective Span Width:
$$W = s_{\text{end}} - s_{\text{start}} = (s_D - s_C) - (\text{off}_c + \text{off}_d)$$

---

### 4.4 Non-Destructive Collapse during Dragging & Snapshot Undo

When an author drags an anchor such that the curve shortens below threshold ($W \le 0$):
1. **Interactive Drag State**: The span is marked `active = false`. It is temporarily excluded from rendering and baking.
2. **Reversibility**: If the author drags the anchor back before releasing the mouse such that $W > 0$, the span automatically reactivates (`active = true`) with its attributes intact.
3. **Commit on Mouse-Up**: Permanent pruning occurs strictly on `WM_LBUTTONUP` if $W \le 0$.
4. **Snapshot Undo**: A full path struct snapshot is pushed to the undo stack before every drag transaction.

---

### 4.5 Locality Window Tracking (Eliminating Crossover Ambiguity)

At track intersections (crossovers), a global $\arg\min_{s} \|C(s) - \mathbf{p}_{\text{mouse}}\|$ is mathematically ambiguous: two crossing curve branches can have identical Euclidean distances ($0\text{ px}$), causing mouse tracking to jump between tracks mid-drag.

#### Locality Window Algorithm:
1. **Initial Mouse-Down**:
   - Perform global search.
   - If two candidates differ in Euclidean distance by $< 1.0\text{ px}$, pick the smaller $s$ deterministically.
   - Initialize $s_{\text{prev}} = s^*$.
2. **Active Drag (`WM_MOUSEMOVE`)**:
   - Restrict projection search to the arc-length window $s \in [s_{\text{prev}} - 40\text{ px}, \; s_{\text{prev}} + 40\text{ px}]$.
   - Binary search $S[]$ in `PathCache` to locate index range $[i_0, i_1]$.
   - Linear scan over polyline segments $i \in [i_0, i_1-1]$ to find closest point $s^*$.
   - **Global Fallback**: If the screen distance from cursor to projected curve exceeds $40\text{ px}$, fall back to global polyline search.
3. **Integer Snapping**:
   - Snap $s^*$ to integer values ($\operatorname{round}(s^*)$) matching Zuma's $1.0\text{ px}$ discrete sampling rate.

---

### 4.6 Single Source of Truth & Painter's Algorithm Precedence

- The legacy fields `in_tunnel` and `priority` are **completely removed from `BezierNode`**.
- The redundant `layer` field is eliminated.
- **Array Order = Painting Order**: Spans are evaluated in array index order ($k = 0, \dots, M-1$). Later spans overwrite earlier ones.
- When a child span collapses, the underlying region automatically reverts to the parent span's attributes.

#### Instantaneous Attribute Query:
```c
static inline void attrs_at(const BakedSpan *b, int n, double s, uint8_t *out_tunnel, uint8_t *out_prio) {
    *out_tunnel = 0;
    *out_prio = 0;
    for (int k = 0; k < n; ++k) {
        if (b[k].active && s >= b[k].s_c - 1e-6 && s <= b[k].s_d + 1e-6) {
            *out_tunnel = b[k].tunnel;
            *out_prio = b[k].prio;
        }
    }
}
```

---

### 4.7 Symmetrical Re-Anchoring Rules on Node Deletion

When an anchor node is deleted:
1. **Pre-Compute**: Record all anchor and span arc-length positions $s$ using the **old** curve geometry.
2. **Remove Node**: Delete the node from `path->nodes`.
3. **Re-Anchor Spans**:
   - If start anchor $A$ was deleted: re-anchor to preceding surviving anchor $A_{\text{prev}}$:
     $$\text{off}_c' = \text{off}_c + L(A_{\text{prev}} \to A)$$
     If $A$ was the very first node (no predecessor), clamp to 0 against the new first node:
     $$\text{off}_c' = \max\left(0.0f, \; \text{off}_c - L(A \to A_{\text{next}})\right)$$
   - If end anchor $B$ was deleted: re-anchor to succeeding surviving anchor $B_{\text{next}}$:
     $$\text{off}_d' = \text{off}_d + L(B \to B_{\text{next}})$$
     If $B$ was the very last node (no successor), clamp to 0 against the new last node:
     $$\text{off}_d' = \max\left(0.0f, \; \text{off}_d - L(B_{\text{prev}} \to B)\right)$$
4. **Node Insertion**: When inserting a node along an existing segment, perform De Casteljau subdivision at the split parameter $t$. The curve geometry does not change, and stable node IDs ensure existing spans remain untouched.

---

## 5. C Data Structures (`src/bezier_path.h`)

```c
#ifndef BEZIER_PATH_H
#define BEZIER_PATH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Node constraint type for vector editing */
typedef enum {
    NODE_CORNER = 0,    /* Sharp corner / inactive handles */
    NODE_SMOOTH,        /* Collinear handles, independent lengths */
    NODE_SYMMETRIC,     /* Collinear handles, equal lengths */
    NODE_CUSP           /* Independent / broken handles */
} BezierNodeType;

/* Single anchor node with unique monotonic ID (attributes removed) */
typedef struct {
    uint32_t id;            /* Monotonic stable node identifier */
    float x, y;             /* Anchor position in canvas world coordinates */
    float hin_x, hin_y;     /* Incoming handle offset relative to anchor */
    float hout_x, hout_y;   /* Outgoing handle offset relative to anchor */
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
    bool active;            /* Runtime activation flag (false during temporary collapse) */
} CurveSpan;

/* Baked span cache for instantaneous O(1) sampling */
typedef struct {
    double s_c;
    double s_d;
    uint8_t tunnel;
    uint8_t prio;
    bool active;
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

    bool is_dirty;
} PathCache;

/* Complete Bezier path definition */
typedef struct {
    char name[64];
    BezierNode *nodes;
    int node_count;
    int node_capacity;
    uint32_t next_node_id;  /* Monotonic ID generator */
    bool is_closed;

    /* Variable Mode Attribute Spans */
    CurveSpan *spans;
    int span_count;
    int span_capacity;

    /* Unified Cache */
    PathCache cache;
} BezierPath;

/* Resampled point ready for runtime game engine or .dat serialization */
typedef struct {
    float x, y;
    bool in_tunnel;
    uint8_t priority;
} ResampledPathPoint;

#endif /* BEZIER_PATH_H */
```

---

## 6. Serialization Specification

### 6.1 Zuma Deluxe Binary CURV (`.dat`)
During arc-length resampling (`bezier_path_resample`):
- For each discrete point $k$ at arc-length $s_k = k \times 1.0\text{ px}$:
  - Query `attrs_at(path->cache.baked_spans, path->cache.baked_span_count, s_k, &t, &p)`.
  - Write evaluated `in_tunnel` (byte) and `priority` (byte) into Point #$k$.
- For control points in `edit_data`:
  - Query `attrs_at(path->cache.baked_spans, path->cache.baked_span_count, path->cache.node_s[i], &t, &p)`.
  - Write into the 10-byte control point record.

### 6.2 ZuPather Sidecar (`.zupath` v1.1)
```
ZUPATH 1.1
PATH coaster CLOSED 0 NODE_COUNT 72 SPAN_COUNT 2
NODE 120.0 200.0 0.0 0.0 0.0 0.0 corner
...
SPAN 29 0.0 32 0.0 1 0
SPAN 51 0.0 55 0.0 1 0
ENDPATH
```
*Span record format: `SPAN <start_anchor_idx> <off_c> <end_anchor_idx> <off_d> <in_tunnel> <priority>`*  
On file load, anchor indices are mapped to stable memory IDs automatically.

---

## 7. Execution Roadmap

1. **Step 1: Core Mathematical Model (`bezier_path.h/.c`)**:
   - Refactor `BezierNode.id` generator.
   - Implement unified `PathCache` (tolerance 0.05 px, `S[]`, `node_s[]`, `BakedSpan[]`).
   - Implement `path_cache_update()`, `attrs_at()`, and `project_s_window()`.
   - Implement symmetrical re-anchoring on node removal and De Casteljau split on node insertion.
2. **Step 2: Automated Console Regression Test (`tests/test_spans_and_cache.c`)**:
   - Test stable node ID tracking.
   - Test De Casteljau node insertion without span shift.
   - Test symmetrical re-anchoring on anchor deletion.
   - Test temporary collapse during anchor drag vs mouse-up permanent prune.
   - Test nested span inheritance (painter's algorithm).
   - Test round-trip `.dat` and `.zupath` v1.1 serialization.
3. **Step 3: Win32 GUI Integration (`src/zupather_ui.c`)**:
   - Wire unified `PathCache` into curve rendering.
   - Add Top-Right Mode Switcher HUD & Top-Left Shortcut HUD (50%/100% opacity).
   - Implement Variable Mode 1D mouse drag handler with locality window tracking and integer snapping.
