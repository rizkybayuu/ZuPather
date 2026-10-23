# Changelog

All notable changes to the **ZuPather** level design suite will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.5] - 2026-10-23

Major maintenance and feature release introducing the **Hierarchical SVG Transform Engine** with full 2D affine matrix transformation support (`matrix`, `translate`, `scale`, `rotate`, `skewX`, `skewY`), nested group `<g transform="...">` tree propagation, sub-pixel floating-point curve ingestion, a dedicated **Settings & Preferences Suite** (`Ctrl+,`), **Viewport GPU Acceleration & 60+ FPS Surface Caching**, **Three-Way Color Theme Architecture** (`Light`, `Dark`, `Use System Default`), and configurable **Action History Limit**.

### Added
- **Settings & Preferences Architecture (`src/dialog_modals.c`, `src/zupather_ui.c`)**:
  - Added dedicated top-level `Settings` menu bar between `Edit` and `View` with shortcut `Ctrl+,`.
  - Added quick-toggle menu items for direct hardware blit, HUD surface caching, and smooth wheel throttling without opening the dialog modal.
  - Symmetrically balanced modal dialog design with flat container cards (`PERFORMANCE & GPU ACCELERATION`, `ACTION HISTORY`, `WORKSPACE & ENVIRONMENT`), eliminating redundant horizontal divider lines.
  - Enforced uniform 20px margins across all four boundaries (top, bottom, left, and right) for bottom action buttons (*Reset Defaults*, *Cancel*, *Save & Apply*).
- **Performance & Viewport GPU Acceleration (`src/canvas_render.c`, `src/zupather_state.c`)**:
  - **Direct Blit (Hardware Optimization)**: Fast-path GDI `BitBlt` block transfers bypassing CPU AlphaBlend loops on fully opaque HUD surfaces and background buffers.
  - **Offscreen HUD Surface Caching**: Pre-renders and caches HUD elements and overlays into offscreen DIB surfaces, eliminating redundant per-frame drawing for zero-lag 60+ FPS zoom, pan, and scroll operations.
  - **Smooth Mouse Wheel & Precision Event Coalescing**: Smooth handling of high-frequency mouse wheel ticks on gaming and precision mice, preventing event queue backlog during rapid zooming.
- **Three-Way Color Theme System (`src/zupather_state.c`, `src/zupather_ui.c`)**:
  - Implemented 3 theme choices: `Light`, `Dark`, and `Use System Default` (serving as default on initial launch when no configuration exists).
  - Implemented real-time bi-directional synchronization between the live application theme and the Settings modal, ensuring the dropdown always accurately displays the active theme.
  - Custom owner-drawn combobox with 150px width accommodating full text `Use System Default` without clipping and 24px height matching numeric inputs.
  - Persistent theme configuration saved to `zupathconf.inf` under `Theme=`.
- **Configurable Action History Capacity (`src/undo_redo.c`, `src/dialog_modals.c`)**:
  - Added configurable undo/redo snapshot limit (default: 64 actions) via a compact numeric input box with memory boundary validation.
  - Maintained zero disk cache overhead with pure in-memory state retention.

### Fixed
- **SVG Transform Hierarchy & Matrix Stacking (`src/svg_path.c`)**:
  - Re-architected SVG path ingestion from a flat `<path>` scanner into a stack-based XML tree walker (supporting up to 64 levels of nested `<g>` elements).
  - Implemented 2D affine transformation matrices ($3 \times 3$ homogeneous coordinates) with full parent-to-child matrix concatenation ($M_{\text{eff}} = M_{\text{parent}} \times M_{\text{child}}$).
  - Added support for all SVG 1.1/2.0 transform operations: `matrix(a,b,c,d,e,f)`, `translate(tx[, ty])`, `scale(sx[, sy])`, `rotate(angle[, cx, cy])` with custom rotation centers, `skewX(angle)`, and `skewY(angle)`, including multi-transform chains within a single attribute.
  - Resolved an issue where cloned, rotated, or translated paths exported from vector suites like **Affinity Designer** and **Adobe Illustrator** (such as radial pinwheels or multi-spiral tracks in `spiralof8.svg`) were stripped of their transforms and stacked directly on top of each other.
  - Resolved coordinate offset distortion in artboard-relative SVG exports (e.g. `longrangetest.svg`), ensuring artboard translations correctly map paths into project canvas world coordinates.
- **Bézier Node & Handle Vector Transformation**:
  - Transformed both anchor positions $(x, y)$ as spatial points and tangent handles (`hin_x, hin_y`, `hout_x, hout_y`) as relative direction vectors under any affine mapping (including non-uniform scaling, reflection, and rotation).
  - Maintained full sub-pixel floating-point precision throughout parser stages without premature integer truncation.
- **Non-Renderable Block Isolation**:
  - Added parsing filters to automatically skip paths declared inside definition blocks (`<defs>`, `<clipPath>`, `<mask>`, `<style>`, and `<metadata>`), preventing masks or clip geometries from being erroneously imported as playable Zuma curve tracks.

### Changed
- **Version & Metadata Alignment**:
  - Updated in-app splash screen version indicator badge to `v0.1.5`.
  - Updated Windows PE resource metadata (`res/zupather.rc`) to File Version `0.1.5` and Product Version `0.1.5`.
  - Updated CMake project configuration to `0.1.5`.
  - Updated `README.md` documentation and version header to `v0.1.5`.

---

## [0.1.4.1] - 2026-10-20

Targeted maintenance hotfix aligning compatibility notices and dialog modals with the **ZuCompleluxe** engine patcher framework (`modules\zumaextrapath.dll`, `modules\zumawebp.dll`) and the upcoming **Zuma Compleluxe** total conversion mod, accurately reflecting vanilla Zuma Deluxe curve limits.

### Fixed
- **ZuCompleluxe Framework & Compatibility Dialog Alignment**:
  - Clarified the distinction between the **ZuCompleluxe** engine patcher framework (`modules\zumaextrapath.dll`, `modules\zumawebp.dll`) and the upcoming **Zuma Compleluxe** total conversion mod across all in-app dialog modals.
  - Corrected the multi-track level warning dialog (`show_extra_paths_warning_ex`): accurately noted that vanilla `Zuma.exe` natively only reads up to 3 curve paths (`curve`, `curve2`, `curve3`) and completely ignores any 4th curve or beyond, rather than crashing or failing to load.
  - Clarified that levels utilizing 4 to 10 curve tracks require Zuma Deluxe patched with the ZuCompleluxe framework (`modules\zumaextrapath.dll`) or running under the Zuma Compleluxe mod.
  - Updated the 10-path limit warning dialogs (`show_path_limit_warning_ex` and `show_import_path_limit_warning`) to reflect that the 10-path allocation ceiling is defined by the ZuCompleluxe engine framework.
  - Clarified the WebP and Video animation notice dialogs (`show_webp_patch_warning_ex` and `show_video_load_warning`) to specify that Animated WebP playback is powered by `modules\zumawebp.dll` under ZuCompleluxe / Zuma Compleluxe.

### Changed
- **Version & Metadata Alignment**:
  - Updated in-app splash screen version indicator badge to `v0.1.4.1`.
  - Updated Windows PE resource metadata (`res/zupather.rc`) to File Version `0.1.4.1` and Product Version `0.1.4.1`.
  - Updated CMake project configuration to `0.1.4.1`.

---

## [0.1.4] - 2026-10-19

Targeted engine release introducing the adaptive Zuma engine `.dat` curve importer with automatic least-squares Bézier curve reconstruction, full compatibility with custom level editors and mod formats (including *Zuma Glitched Remake*), multi-track XML level parser robustness, and sub-pixel curve fidelity.

### Added
- **Adaptive Zuma Engine `.dat` Curve Importer (`src/bezier_path.c`)**:
  - Re-engineered `bezier_path_import_dat_ex` based on decompiled `Zuma.exe` engine runtime logic (`CurveMgr::LoadCurve`), decoupling discrete waypoint ingestion from rigid PopCap editor control-point structures.
  - Added support for `.dat` files authored with non-standard, modified, or custom tool headers (such as "DeathKnight Zuma Bessel Generator", "GScienceStudio", or custom mod signatures found in *Zuma Glitched Remake*), eliminating erroneous `DAT_ERR_INVALID_CP_COUNT` rejections.
  - Implemented analytical least-squares cubic Bézier curve fitting (`fit_bezier_segment_points`, `extract_adaptive_control_points`) with recursive subdivision when standard PopCap control points are absent or custom, guaranteeing sub-pixel curve deviation ($< 1.0\text{ px}$) while yielding clean, intuitive, and fully editable Bézier anchors.
  - Maintained 100% bitwise parity and control point integrity for official vanilla PopCap `.dat` curve files.
- **Multi-Track Zuma Level Importer Robustness (`src/dialog_modals.c`)**:
  - Enhanced Zuma level XML parser in `import_zuma_level_to_canvas` and `show_zuma_level_attributes_dialog` to support both `curve` and `curve1` attribute keys in `<Graphics>` tags.
  - Fixed an issue where custom mod levels declaring track 1 as `curve1` (e.g. `betawings`, `final`, `longpit`) failed to load their first curve.

### Fixed
- **Attribute Span Half-Open Interval Boundary**:
  - Corrected `attrs_at` boundary evaluation (`s < (b[k].s_d - 1e-5)`) to strictly conform to half-open interval $[s_c, s_d)$, preventing false attribute overlaps at segment transition boundaries.

### Changed
- **Version & Metadata Alignment**:
  - Updated in-app splash screen version indicator badge to `v0.1.4`.
  - Updated Windows PE resource metadata (`res/zupather.rc`) to File Version `0.1.4.0` and Product Version `0.1.4`.
  - Updated CMake project configuration to `0.1.4`.

---

## [0.1.3] - 2026-10-17

Major feature release introducing Canvas Affect Transform, enabling proportional, synchronized transformation of level elements, curves, and overlay assets during canvas resizing and panning, backed by non-destructive image resampling and full undo/redo dimensions tracking.

### Added
- **Canvas Mode "Affect Transform" Subsystem**:
  - Added new dedicated controls section directly below **Image Alignment** in the Top-Right Canvas Tools HUD.
  - Provided three toggleable boolean options:
    1. `[x] Include Element (Frog & Coin)`: Proportionally scales Frog shooter anchor and Treasure Point coin waypoints with the canvas.
    2. `[x] Include Curve (Paths & Attrs)`: Proportionally scales all path anchor coordinates $(x, y)$, tangent handles $(h_{\text{in}}, h_{\text{out}})$, and curve attribute spans (`CurveSpan` tunnel and priority offsets $off_c, off_d$) according to exact arc-length stretch ratios.
    3. `[x] Include Overlay (Cutout/Sprite)`: Proportionally transforms both placement coordinates $(x, y)$ and image dimensions $(w, h)$ of Cutouts, Sprites, and Background Alpha masks.
  - Integrated with all canvas resize pathways: 8 interactive drag handles, Center Move handle, HUD Apply button, HUD/Menu preset buttons (640x480, 800x600, 1024x768, 1280x720), and keyboard arrow key handle nudging.
  - Documented Affect Transform shortcuts and behavior in the in-app Canvas Guide HUD (<kbd>F1</kbd>).
- **Non-Destructive Image Resampling (`orig_bits`)**:
  - Implemented pristine original bitmap retention in `CanvasImage` (`orig_bits`, `orig_width`, `orig_height`).
  - Added `image_resize_in_place()` which performs bilinear resampling directly from pristine unscaled source pixels, preventing generational quality degradation or blurring during continuous 60 FPS viewport resizing.

### Fixed
- **Dimension-Aware Level Export**:
  - Updated asset exporter in `level_io.c` to detect dimension modifications (`is_resized`) on Cutouts, Sprites, and Background Alphas.
  - Resized assets are cleanly re-encoded and exported to target directories rather than bypassing resizing via raw file copies.
- **Undo / Redo Overlay Dimension Synchronization**:
  - Extended memory undo snapshots (`UndoCutoutSnapshot`, `UndoSpriteSnapshot`, `UndoBgAlphaSnapshot`) with image width and height tracking.
  - Restoring state via <kbd>Ctrl</kbd>+<kbd>Z</kbd> / <kbd>Ctrl</kbd>+<kbd>Y</kbd> now accurately restores previous asset dimensions via non-destructive resampling.
- **Keyboard Arrow Key Canvas Nudging**:
  - Connected keyboard arrow keys in Canvas Mode to `apply_canvas_transform()`.
  - Added baseline protection and automatic `"Nudge Canvas Resize"` / `"Nudge Canvas Move"` undo snapshot recording.

### Changed
- **Version & Metadata Alignment**:
  - Updated in-app splash screen version indicator badge to `v0.1.3`.
  - Updated Windows PE resource metadata to File Version `0.1.3.0` and Product Version `0.1.3`.

---

## [0.1.2] - 2026-10-17

Major feature and stability release introducing cross-toolchain CMake and GitHub Actions CI, curve segment subdivision and node insertion in Draw Mode, stacked overlay layer cycling, and complete Undo/Redo hardening across all editor modules.

### Added
- **CMake Build System & GitHub Actions CI**:
  - Added root `CMakeLists.txt` and explicit source manifest `cmake/sources.cmake` supporting cross-toolchain compilation with MSVC (Visual Studio 2022) and MinGW-w64 (MSYS2 / GCC).
  - Added multi-platform GitHub Actions CI workflow (`.github/workflows/build.yml`) with matrix testing for MSVC (x64) and MinGW (x64) and automatic binary artifact publishing.
  - Embedded Windows PE resource script (`res/zupather.rc`) automatically across both MSVC (`rc.exe`) and MinGW (`windres.exe`).
  - Added comprehensive build instructions in `README.md` covering automated `build.bat` and CMake commands.
- **Draw Mode Bézier Segment Subdivision & Insertion**:
  - Added segment insertion: split any curve segment at arbitrary parameter $t$ using De Casteljau subdivision while mathematically preserving 100% of the curve's curvature geometry.
  - Enforced strict modifier condition: holding <kbd>Alt</kbd> (or <kbd>Ctrl</kbd>+<kbd>Alt</kbd>) over a curve segment displays an interactive cyan target reticle and floating badge `+Insert Node (Alt+LMB) [X, Y]`.
  - Pure Left Click (LMB) remains strictly reserved for drawing curves and adding new anchor nodes without any interruption or hijacking.
  - Enforced strict <kbd>Ctrl</kbd> requirement for selecting anchor nodes (`Ctrl + Click` / `Ctrl + Drag Marquee`).
  - Added <kbd>Insert</kbd> keyboard shortcut to subdivide the segment between the active selected node ($k$) and the previous node on the path ($k-1$) at midpoint ($t = 0.5$).
- **Overlay Mode Stacked Layer Cycling**:
  - Added <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+Left Click to cycle selections through overlapping/stacked overlay objects (Cutouts, Sprites, Alpha Masks) from top layer to bottom layer with circular wrap-around.

### Fixed
- **Comprehensive Undo / Redo Hardening**:
  - Implemented `undo_ensure_baseline()` across all editing operations to guarantee an initial reference snapshot before the first mutation.
  - Fixed Cutout and Sprite deletion undo: deleting an overlay cutout now cleanly records an undo snapshot and restores properly on <kbd>Ctrl</kbd>+<kbd>Z</kbd>.
  - Fixed Coin deletion and Frog position edit undo tracking.
  - Fixed Canvas resize undo capture upon drag handle release.
  - Fully integrated curve node insertion into the undo/redo stack (`"Insert Curve Node"`).

### Changed
- **Version & Metadata Alignment**:
  - Updated in-app splash screen version indicator badge to `v0.1.2`.
  - Updated Windows PE resource metadata to File Version `0.1.2.0` and Product Version `0.1.2`.

---

## [0.1.1.1] - 2026-10-16

Minor maintenance patch resolving layout clipping in the Attribute Mode guide panel and updating the splash screen version badge.

### Fixed
- **HUD Guide Layout**:
  - Fixed an issue where shortcut rows in Attribute Mode were truncated at the bottom of the floating guide panel.
  - Updated `get_guide_content_height()` to dynamically calculate the panel height from shortcut entries, ensuring all items remain fully visible.

### Changed
- **Splash Screen Version Badge**:
  - Updated version indicator to `v0.1.1.1`.
  - Added adaptive styling for Dark and Light themes.

---

## [0.1.1] - 2026-10-16

Comprehensive portability update embedding all typography fonts and gameplay sprites directly into the unified executable, alongside Space Arena attribute safeguards and dialog interaction fixes.

### Added
- **Embedded Asset Pipeline (100% Standalone Self-Contained Binary)**:
  - Embedded all 4 application font files (`AtkinsonHyperlegible-Regular.ttf` ~53 KB, `AtkinsonHyperlegible-Bold.ttf`, `JetBrainsMono-Regular.ttf`, `JetBrainsMono-Bold.ttf`) directly as Win32 PE resources (`IDR_FONT_*`). ZuPather dynamically mounts them in memory via `AddFontMemResourceEx`, ensuring custom UI typography renders on any Windows system with zero external font files or installation.
  - Embedded all 14 gameplay and preview sprites (`SMALLFROGonPAD.png`, `COIN.png`, `SKULL.png`, and 11 `baBall*.png` colored ball textures) as Win32 binary resources (`IDR_SPRITE_*`) stream-loaded into GDI+ via `SHCreateMemStream` with disk fallback.
  - Embedded application splash image (`ZuSPLASHscreen.jpg`), vector GitHub brand logo (`github.svg`), and high-resolution icon (`ZUpather.ico`).
  - Integrated official Windows PE `VERSIONINFO` metadata resource directly into the executable (File Version 0.1.1.0, Product Version 0.1.1).
- **Space Arena Safeguards & Explanatory Feedback**:
  - Integrated dedicated `[SPACE ARENA]` badge beside the dialog title and `[Overridden by Space]` asset header indicators when `space="true"` (`g_map_is_space`).
  - Added an informative confirmation notice when saving a Space map or toggling the Space checkbox, detailing the 6 visual layers and engine attributes overridden by Zuma's procedural starfield.

### Changed
- **Save Canvas Modal Refinement**:
  - Maintained clean, non-intrusive subtitle `Saves canvas folder and settings.xml to ./canvas/<name>/` across all modes, perfectly harmonized with live animation RAM budget telemetry.
  - Polished themed confirmation modals by removing unnecessary horizontal divider lines for a cleaner modern aesthetic.
- **Splash Screen Version Alignment**:
  - Updated in-app splash screen version indicator badge to `v0.1.1`.

### Fixed
- **State & XML Persistence**:
  - Fixed `load_canvas_settings_xml()` attribute reset behavior during folder scans to ensure `g_map_is_space` state is never inadvertently cleared.
  - Preserved `map_is_space` and `map_has_skullrot` in `UndoSnapshot` during undo/redo history operations.
- **Progression Difficulty Combobox Freezing**:
  - Resolved an interaction freeze where clicking the Progression Difficulty dropdown on levels with zero cutout/sprite assets became unresponsive by providing an independent window subclass procedure (`g_orig_save_combo_diff_proc`).

---

## [0.1.0.1] - 2026-10-16

Maintenance update establishing pure portable configuration isolation and visual splash version alignment.

### Changed
- **Pure Portable Architecture & Config Isolation**:
  - Eliminated Windows Registry fallback (`HKCU\Software\ZuPather`) in favor of strict, isolated file persistence via `canvas/zupathconf.inf`.
  - Deleting `zupathconf.inf` now completely and reliably resets ZuPather to a clean factory state with zero residual path caches.
  - Updated in-app splash screen version indicator badge to `v0.1.0.1`.

---

## [0.1.0] - 2026-10-16

Initial milestone release establishing the complete, standalone vector path editor, level asset composer, and binary curve compiler for *Zuma Deluxe* and the *ZuCompleluxe* engine.

### Added

- **Vector Bézier Core & Arc-Length Parameterization**:
  - High-precision cubic Bézier curve calculation with analytical tangent and curvature derivatives.
  - Numerical arc-length integration with uniform equidistant resampling ($\Delta s = 1.0\text{ pixel}$) guaranteeing uniform ball rolling speed in-game.
  - Drift-free delta quantization tracking cumulative fractional sub-pixel errors to eliminate positional drift across thousands of path waypoints.
  - Multi-track curve architecture supporting from 1 path up to 10 independent active curves per level (`curve` through `curve10`), fully compatible with the *ZuCompleluxe* multi-curve engine.

- **Visual Win32 WYSIWYG Editor**:
  - Standalone single-binary Win32 architecture with zero external runtime DLL dependencies.
  - Real-time double-buffered GDI canvas supporting smooth panning, zooming, coordinate grids, and pixel-precise layout measurements.
  - Adaptive Dark and Light theme engine with instant runtime switching, custom owner-drawn menus, title bar integration, and cohesive high-contrast UI palettes.

- **Interactive Level Authoring Tools**:
  - **Pen Tool**: Dynamic vector point addition, continuous tangent control handle manipulation, anchor node insertion, and path closing.
  - **Selection & Node Editing**: Click-and-drag node translation, tangent handle modification, snapping guides, and multi-node inspection.
  - **Frog Shooter Placement**: Interactive frog anchor placement (`gx`, `gy`) with real-time visual pad rendering and coordinate synchronization.
  - **Treasure Point Coin Placer**: Interactive coin waypoint positioning with automatic curve proximity indexing.
  - **Attribute Spans Subsystem**: Interactive segment tagging for In-Tunnel flags, speed/acceleration modifiers, and rendering priority layers (`0..4`).
  - **Cutout, Sprite & Alpha Mask Orchestrator**: Multi-layer visual composition previewing under-track and over-track geometry with transparency.

- **Live Physics & Ball Roll Simulation**:
  - Real-time interactive ball train simulation executing at authentic Zuma Deluxe physical speed.
  - Dynamic 11-color ball visualization with correct spacing, tunnel occlusion, and skull terminus deceleration.

- **Zuma Deluxe Map Manager & Direct Engine Exporter**:
  - Dual-pane level manager displaying local ZuPather Canvas Projects alongside detected Zuma Deluxe Game Maps.
  - Tri-state color-coded map classification:
    - **White**: Protected vanilla Zuma Deluxe original maps registered in official `StageProgression`. Modification, deletion, or overwriting is strictly blocked.
    - **Orange**: Unregistered / Misc maps present in `levels.xml` but unassigned in progression (rename and delete enabled).
    - **Green**: Custom ZuPather maps utilizing extended progression formats (rename and delete enabled).
  - Safe direct-to-engine exporter: automatically updates `<Graphics>` definitions, writes track `.dat` files, configures progression IDs (`zupath[n]-[diff]`), and creates automatic timestamped safety backups (`levels.xml.bak`).

- **Vector Exchange & Headless CLI Tooling**:
  - Bi-directional SVG vector import/export compatible with professional design suites (Affinity Designer, Canva, Adobe Illustrator).
  - Integrated command-line interface for batch conversion and headless pipelines (`svg2dat`, `dat2svg`, `genxml`, `info`).

- **Automated Build Pipeline**:
  - Unified one-click master builder (`build.bat`) supporting MinGW-w64 (GCC), Clang, and MSVC toolchains.
  - Automatic Windows resource compilation embedding icons, fonts, and manifest data.
