# Zuma Deluxe — `CURV` Binary Path File Specification (`.dat`)

This document provides the binary specification, mathematical model, and runtime engine mechanics of the curve trajectory files (`.dat`) used in PopCap Games' *Zuma Deluxe*.

---

## 1. Overview & File Context

In *Zuma Deluxe*, ball paths are defined in binary files located in `levels/<level_name>/<curve_name>.dat` (e.g., `levels/coaster/coaster.dat`). In `levels/levels.xml`, a level references its path through the `<Graphics>` tag:
```xml
<Graphics id="coaster" curve="coaster" image="coaster" dispname="Altar of Tlaloc" gx="325" gy="240">
    ...
</Graphics>
```
The attribute `curve="coaster"` instructs the engine to load `levels/coaster/coaster.dat`.

Multi-track levels reference additional curves using `curve2`, `curve3`, etc. (e.g., `curve="blackswirley-1" curve2="blackswirley-2"`).

---

## 2. Complete Binary File Layout

A standard Zuma Deluxe `.dat` file consists of three contiguous binary sections:

```
+-------------------------------------------------------------------+
| 1. Header (16 bytes)                                              |
|    - Magic: "CURV" (4 bytes)                                      |
|    - Version: uint32 (4 bytes)                                    |
|    - Edit Type: uint32 (4 bytes)                                  |
|    - Edit Buffer Size: uint32 (4 bytes)                           |
+-------------------------------------------------------------------+
| 2. Editor Control Points Payload (`edit_data`: N bytes)           |
|    - Control Point Count: uint32 (4 bytes)                        |
|    - Array of Control Points (10 bytes each):                     |
|        * x: int32 (4 bytes)                                       |
|        * y: int32 (4 bytes)                                       |
|        * in_tunnel: uint8 (1 byte)                                |
|        * priority: uint8 (1 byte)                                 |
+-------------------------------------------------------------------+
| 3. Runtime Discrete Trajectory Points (`points`: M bytes)         |
|    - Total Points: uint32 (4 bytes)                               |
|    - Point #0 (Anchor point, 10 bytes):                           |
|        * x: float32 (4 bytes)                                     |
|        * y: float32 (4 bytes)                                     |
|        * in_tunnel: uint8 (1 byte)                                |
|        * priority: uint8 (1 byte)                                 |
|    - Points #1 to #TotalPoints-1 (Delta compressed, 4 bytes each):|
|        * dx: int8 signed char (1 byte)                            |
|        * dy: int8 signed char (1 byte)                            |
|        * in_tunnel: uint8 (1 byte)                                |
|        * priority: uint8 (1 byte)                                 |
+-------------------------------------------------------------------+
```

---

## 3. Section Field Specifications

### 3.1 Header (Offset `0x00` - `0x0F`, 16 bytes)

| Offset | Field Name | Data Type | Value / Range | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0x00` | `magic` | char[4] | `"CURV"` (`0x43 0x55 0x52 0x56`) | ASCII magic identifier verifying file format. |
| `0x04` | `version` | uint32 (LE) | `2` | File format version. Zuma Deluxe retail levels use Version 2. |
| `0x08` | `edit_type` | uint32 (LE) | `1` | Path type flag. `1` indicates standard spline control point mode. |
| `0x0C` | `edit_data_size` | uint32 (LE) | Variable (e.g., `724`) | Byte size of the following Editor Control Points payload. |

---

### 3.2 Editor Control Points Block (`edit_data`)

Directly following the header at offset `0x10`, this block contains the original design waypoints created during level authoring.

- **`num_control_points`** (`uint32`, 4 bytes): Number of control points $K$.
- **Control Point Array**: $K$ records, each exactly **10 bytes**:

| Offset within Record | Field Name | Data Type | Range | Description |
| :--- | :--- | :--- | :--- | :--- |
| `+0x00` | `x` | int32 (LE) | $-2048$ to $+2048$ | Control waypoint horizontal coordinate on the level canvas. |
| `+0x04` | `y` | int32 (LE) | $-2048$ to $+2048$ | Control waypoint vertical coordinate on the level canvas. |
| `+0x08` | `in_tunnel` | uint8 | `0` or `1` | `0` = normal open track; `1` = inside tunnel / bridge underpass. |
| `+0x09` | `priority` | uint8 | `0` to `255` | Z-order layer priority for track crossovers (`1` = over, `0` = under). |

Total size of this block:
$$\text{edit\_data\_size} = 4 + (K \times 10) \text{ bytes}$$
*Example from `coaster.dat`: $K = 72$, $\text{edit\_data\_size} = 4 + 720 = 724\text{ bytes}$.*

---

### 3.3 Runtime Discrete Trajectory Points Block (`points`)

This block contains the high-resolution, arc-length parameterized curve points sampled at ~1 pixel increments. This is the primary data structure read into memory by the Zuma game engine (`FUN_00413e70` in `Zuma.exe`).

- **`total_points`** (`uint32`, 4 bytes): Number of discrete trajectory steps $N$ (e.g., $N = 3659$ in `coaster.dat`).
- **Anchor Point (Point #0, 10 bytes)**:
  - `x`: `float32` (IEEE 754 single-precision float, 4 bytes). Entrance coordinate $X_0$.
  - `y`: `float32` (IEEE 754 single-precision float, 4 bytes). Entrance coordinate $Y_0$.
  - `in_tunnel`: `uint8` (1 byte).
  - `priority`: `uint8` (1 byte).
- **Subsequent Points (Point #1 through Point #$N-1$, 4 bytes each)**:
  Each point stores incremental positional deltas encoded as signed bytes:
  - `dx`: `int8` (signed char, range $-128$ to $+127$, 1 byte).
  - `dy`: `int8` (signed char, range $-128$ to $+127$, 1 byte).
  - `in_tunnel`: `uint8` (`0` or `1`, 1 byte).
  - `priority`: `uint8` (unsigned integer, 1 byte).

Total size of trajectory block:
$$\text{trajectory\_size} = 4 + 10 + ((N - 1) \times 4) \text{ bytes}$$

---

## 4. Mathematical Reconstruction & Coordinate Decoding

### 4.1 Coordinate Expansion Formula
During level load, the Zuma Deluxe engine reconstructs absolute floating-point coordinates $(X_n, Y_n)$ iteratively:

$$\begin{aligned}
X_0 &= \text{read\_float32}() \\
Y_0 &= \text{read\_float32}() \\
X_n &= X_{n-1} + (dx_n \times 0.01) \\
Y_n &= Y_{n-1} + (dy_n \times 0.01)
\end{aligned}$$

The scale multiplier is strictly $0.01\text{ px}$. Therefore, each signed byte step represents a delta between $-1.28\text{ px}$ and $+1.27\text{ px}$.

### 4.2 Encoding Formula (Drift-Free Error-Feedback Loop)
Naive delta calculation using adjacent ideal points $(X_n - X_{n-1})$ suffers from cumulative quantization drift over ~3600 points. To prevent this, the encoder must compute deltas against the **accumulated decoded position**:

$$\begin{aligned}
dx_n &= \operatorname{clamp}\left(\operatorname{round}\left((X_{n,\text{ideal}} - X_{n-1,\text{decoded}}) \times 100\right), -128, 127\right) \\
X_{n,\text{decoded}} &= X_{n-1,\text{decoded}} + (dx_n \times 0.01) \\
dy_n &= \operatorname{clamp}\left(\operatorname{round}\left((Y_{n,\text{ideal}} - Y_{n-1,\text{decoded}}) \times 100\right), -128, 127\right) \\
Y_{n,\text{decoded}} &= Y_{n-1,\text{decoded}} + (dy_n \times 0.01)
\end{aligned}$$

This error-feedback loop guarantees that floating-point quantization error never accumulates and remains strictly $\le 0.005\text{ px}$ across the entire trajectory.

### 4.3 Exact Catmull-Rom to Cubic Bezier Conversion
Zuma's original level editor generated spline trajectories from control points. A uniform Catmull-Rom spline segment between $P_i$ and $P_{i+1}$ is mathematically equivalent to a cubic Bezier curve with control handles:

$$\begin{aligned}
B_0 &= P_i \\
B_1 &= P_i + \frac{P_{i+1} - P_{i-1}}{6} \\
B_2 &= P_{i+1} - \frac{P_{i+2} - P_i}{6} \\
B_3 &= P_{i+1}
\end{aligned}$$

For parameterized Catmull-Rom splines ($\alpha = 0.0$ Uniform, $\alpha = 0.5$ Centripetal, $\alpha = 1.0$ Chordal), with chord distances $d_1 = \|P_1 - P_0\|$, $d_2 = \|P_2 - P_1\|$, $d_3 = \|P_3 - P_2\|$, and $a_k = d_k^\alpha$:

$$\begin{aligned}
B_1 &= P_1 + \frac{a_1^2 (P_2 - P_1) + a_2^2 (P_1 - P_0)}{3 a_1 (a_1 + a_2)} \\
B_2 &= P_2 - \frac{a_3^2 (P_2 - P_1) + a_2^2 (P_3 - P_2)}{3 a_3 (a_3 + a_2)}
\end{aligned}$$

This difference-based algebraic formulation avoids catastrophic floating-point cancellation.

#### Empirical Parametrization Benchmark on `coaster.dat` (72 control points, 71 segments):
Evaluating all 71 segments against the retail reference trajectory:

| Parametrization Model | Point Count | Integrated Length | Average Distance Error | Maximum Distance Error |
| :--- | :---: | :---: | :---: | :---: |
| **Uniform ($\alpha = 0.0$)** | **3,676** | **3,674.85 px** | **0.3465 px** | **1.3081 px** |
| **Centripetal ($\alpha = 0.5$)** | 3,675 | 3,673.86 px | 0.3943 px | 1.3269 px |
| **Chordal ($\alpha = 1.0$)** | 3,676 | 3,674.74 px | 0.5018 px | 2.3319 px |

*Conclusion*: Standard **Uniform Catmull-Rom ($\alpha = 0.0$)** achieves the closest match to vanilla Zuma Deluxe retail curves, matching PopCap's standard engine implementation with an average deviation of merely $\approx 0.34\text{ px}$.

---

## 5. Runtime Engine Mechanics

### 5.1 Arc-Length Parameterization
The discrete points stored in `.dat` files are sampled uniformly at constant arc-length distance intervals of strictly $1.0\text{ px}$ ($\Delta s = 1.0\text{ px}$). The final trajectory point is clamped directly onto the Golden Skull pit center even if the residual distance is $< 1.0\text{ px}$.

#### Why Uniform Arc-Length Sampling is Critical:
1. **Constant-Time Ball Advancement ($\mathcal{O}(1)$)**:
   In Zuma Deluxe, a ball train advances at speed $v$ (in pixels per frame, defined in `<Settings speed="..." />`). Because each point index corresponds to $\approx 1$ pixel of arc distance, moving a ball along the curve is simply:
   $$\text{position} \mathrel{+}= v$$
   The ball's Cartesian coordinate $(x, y)$ is found by indexing the points array at $i = \lfloor \text{position} \rfloor$ and interpolating with fraction $t = \text{position} - i$:
   $$(x, y) = (1 - t) \cdot P_i + t \cdot P_{i+1}$$
2. **Fixed Ball Spacing**:
   Zuma ball sprites have a constant diameter (approximately 32 pixels). Two touching balls in a chain simply maintain a distance of 32 indices along the path:
   $$\text{index}_{\text{ball}_{k}} = \text{index}_{\text{ball}_{k-1}} - 32$$
   No real-time polynomial integration or root-finding is required during gameplay.

### 5.2 Rolling Heading & Sprite Orientation
As balls roll along the track, their rotation angles ($\theta$) follow the path tangent:
$$\theta_n = \operatorname{atan2}(Y_{n+1} - Y_{n-1}, X_{n+1} - X_{n-1})$$
The ball sprite rotates by $\theta_n$ relative to its rolling axis.

### 5.3 Tunnel Flag (`in_tunnel`) Mechanics
When `in_tunnel == 1` at a point:
1. **Invulnerability**: The ball cannot collide with projectiles fired from the player's frog shooter. Shots pass over the tunnel track without hitting balls inside.
2. **Visual Depth**: The ball is sorted behind overlay cutout sprites defined in `levels.xml`:
   ```xml
   <Cutout image="tunnel01" pri="1" x="0" y="0" />
   ```

### 5.4 Track Priority (`priority`) Mechanics
On levels where the curve loops over itself (crossovers / overpasses):
- Crossover tracks have `priority = 1` (overpass).
- Lower tracks have `priority = 0` (underpass).
- Balls at positions with higher priority values are rendered above balls with lower priority values, preventing visual clipping artifacts.

### 5.5 Path Boundaries
- **Index $0$**: The track entrance, typically positioned off-screen (e.g., $X = -16$, $Y = 64$).
- **Index $N-1$**: The track destination, terminating exactly at the center of the Golden Skull Pit (e.g., $X = 325$, $Y = 103$). When the leading ball reaches index $N-1$, it drops into the pit, triggering life loss.

---

## 6. Case Study: `coaster.dat` Reference Metrics

Measurements extracted directly from `levels/coaster/coaster.dat`:
- **Total file size**: $15,386$ bytes.
- **Control points**: $72$ points.
- **Discrete curve points**: $3,659$ points.
- **Total path length**: $3,676.15$ pixels.
- **Average step size**: $1.0050$ pixels.
- **Tunnel Section 1**: Indices $1466$ to $1619$ ($154$ points), between $(159.9, 77.1)$ and $(57.1, 183.5)$, matching `tunnel01`.
- **Tunnel Section 2**: Indices $2687$ to $2840$ ($154$ points), between $(574.9, 93.9)$ and $(464.0, 122.4)$, matching `tunnel02`.
- **Start coordinate**: $(-15.066, 63.635)$.
- **End coordinate**: $(324.994, 102.995)$, matching the Golden Skull position in `levels.xml`.

---

## 7. ZuPather Vector Curve Editor Mechanics (Pen Tool)

ZuPather implements a professional vector pen tool workflow (mirroring Affinity Designer and Adobe Illustrator) for designing Zuma Deluxe paths:

### 7.1 Interactive Path Preview on Hover (Rubber-banding)
- When the Curve Tool is active (`Ctrl+U`) and an active path has at least one node, moving the mouse over the canvas renders a real-time prospective cubic Bezier segment from the path's trailing node to the cursor position.
- A prospective anchor reticle and coordinate badge (`+Node #N [X, Y]`) indicate exactly where the next waypoint will land before clicking.
- Left-clicking on empty canvas confirms waypoint placement and begins dragging tangent handles (`PEN_DRAG_NEW_HANDLE`).

### 7.2 45° Angle Snapping (`Shift`)
Holding `Shift` activates 8-directional ($0^\circ, 45^\circ, 90^\circ, 135^\circ, 180^\circ, 225^\circ, 270^\circ, 315^\circ$) geometric constraint snapping:
1. **Next Waypoint Placement**: Snaps the prospective segment along horizontal ($y = y_{\text{last}}$), vertical ($x = x_{\text{last}}$), or diagonal ($|\Delta x| = |\Delta y|$) axes relative to the previous anchor. The badge dynamically reports the snap angle (e.g., `+Node #4 [320, 240] 45 deg`).
2. **Handle Manipulation**: Snaps incoming and outgoing handles to the nearest $45^\circ$ angle while strictly preserving tangent length.
3. **Anchor Translation**: Snaps node movements to orthogonal/diagonal axes.

### 7.3 Anchor Constraint & Handle Conversion (Affinity / Illustrator Style)
- **Sharp Corner Retraction (`NODE_CORNER`)**:
  - `Alt` + Left-Click directly on an anchor retracts its handles to zero $(0, 0)$, converting the waypoint to a sharp corner.
  - Pressing `C` when anchor(s) are selected immediately converts them to sharp corners with zero handles.
- **Smooth Tangent Restoration (`NODE_SMOOTH`)**:
  - `Alt` + Drag from a sharp anchor pulls out new symmetric handles.
  - Double-Clicking on an anchor point toggles between Sharp Corner (zero handles) and Auto Smooth (recalculates collinear tangents $(P_{i+1} - P_{i-1})/6$).
  - Pressing `S` when anchor(s) are selected auto-smooths them using adjacent neighbor waypoints.
- **Independent Tangents (`NODE_CUSP`)**:
  - `Alt` + Dragging a handle breaks handle collinearity, allowing sharp direction changes.

### 7.4 Multi-Selection & Canvas Multi-Track Export
- **Rectangular Selection (Marquee)**: Holding `Ctrl` and dragging across empty workspace renders a selection box. Clicking empty space with `Ctrl` deselects all paths and nodes.
- **Curve Deletion & Automatic Compaction**:
  - Selecting all nodes of a curve and pressing `Delete` / `Backspace` (or `Shift+Delete` / `Ctrl+Delete` with no nodes selected) removes that curve.
  - Active curves are compacted immediately in memory, closing index gaps and updating path names (`[canvas]-1`, `[canvas]-2`, etc.).
  - When reducing to 1 curve, it cleanly transitions back to single-track format (`[canvas].dat`), deleting stale `[canvas]-*.dat` files and updating `settings.xml` to `curve="[canvas]"`.
  - When saving with multiple curves, stale `.dat` files with index $> N$ are purged from disk, keeping the canvas folder strictly in sync with the project.
- **Translucent Floating Badges (25% Opacity)**:
  - Coordinate info badges (`+Node #N [X, Y]`, `Start Path #N`, `New Path #N`) are rendered via per-pixel 32-bit DIB section alpha blending at 25% opacity (`alpha = 64`).
  - This translucent glass effect prevents the badge from obstructing the user's view of the background image and tracks while drawing or inspecting curves.
- **Theme Synchronization**:
  - Switching between Light Mode and Dark Mode immediately repaints all active toolbar toggle buttons (including the Curve tool button) without color caching or state sticking.
