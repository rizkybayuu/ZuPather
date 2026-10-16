# Changelog

All notable changes to the **ZuPather** level design suite will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
