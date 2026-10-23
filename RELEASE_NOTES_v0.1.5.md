# ZuPather v0.1.5 Release Notes

**Release Date:** October 23, 2026  
**Binary Target:** `ZuPather.exe` (Single Unified Win32 Executable, Zero External DLLs)  
**Compatibility:** Windows 7 / 8 / 10 / 11 (x86 & x64)

---

## Highlights of Version 0.1.5

ZuPather **v0.1.5** is a major milestone release delivering significant enhancements to the vector graphics engine, viewport performance, application customization, and user experience:

1. **Hierarchical SVG Transform Engine**: Full 2D affine transformation matrix stack traversal (`matrix`, `translate`, `scale`, `rotate`, `skewX`, `skewY`), supporting deeply nested `<g transform="...">` groups, artboard translations, and radial track patterns from Affinity Designer and Adobe Illustrator without curve stacking.
2. **Dedicated Settings & Preferences Architecture (`Ctrl+,`)**: A comprehensive top-level **Settings** menu and modal dialog providing fine-grained control over viewport optimizations, memory limits, and workspace defaults.
3. **Viewport GPU Acceleration & 60+ FPS Surface Caching**:
   - **Direct Blit**: GDI `BitBlt` hardware transfer bypassing CPU AlphaBlend loops on fully opaque HUDs and backgrounds.
   - **Offscreen HUD Surface Caching**: Pre-renders and caches HUD overlays into offscreen DIB surfaces, delivering seamless, zero-lag 60+ FPS zoom, pan, and scroll operations.
   - **Smooth Mouse Wheel Throttling**: Precision event coalescing preventing message queue backlog on high-frequency gaming mice.
4. **Three-Way Color Theme System**:
   - Select between **Light**, **Dark**, and **Use System Default** (with System Default serving as the initial launch fallback).
   - Real-time bi-directional synchronization ensures the Settings dialog always accurately reflects the active theme.
   - Custom owner-drawn combobox with 150px width accommodating full text without clipping and 24px height matching numeric inputs.
5. **Configurable Action History Capacity**: Customizable undo/redo limit via a compact numeric input box with zero disk cache overhead.
6. **Symmetrically Balanced Dialog UI**: Clean flat card-based container layout with uniform 20px margins across all four boundaries (top, bottom, left, right).

---

## Detailed Changelog

### Added
- **Settings & Preferences Suite (`src/dialog_modals.c`, `src/zupather_ui.c`)**:
  - Top-level `Settings` menu bar between `Edit` and `View` with shortcut `Ctrl+,`.
  - Quick-toggle menu items for direct hardware blit, HUD surface caching, and smooth wheel throttling.
  - Symmetrically balanced modal dialog design with flat container cards (`PERFORMANCE & GPU ACCELERATION`, `ACTION HISTORY`, `WORKSPACE & ENVIRONMENT`), eliminating redundant horizontal divider lines.
  - Enforced uniform 20px margins across all four boundaries (top, bottom, left, and right) for bottom action buttons (*Reset Defaults*, *Cancel*, *Save & Apply*).
- **Performance & Viewport GPU Acceleration (`src/canvas_render.c`, `src/zupather_state.c`)**:
  - **Direct Blit (Hardware Optimization)**: Fast-path GDI `BitBlt` block transfers bypassing CPU AlphaBlend loops on fully opaque HUD surfaces and background buffers.
  - **Offscreen HUD Surface Caching**: Pre-renders and caches HUD elements and overlays into offscreen DIB surfaces, eliminating redundant per-frame drawing for zero-lag 60+ FPS zoom, pan, and scroll operations.
  - **Smooth Mouse Wheel & Precision Event Coalescing**: Smooth handling of high-frequency mouse wheel ticks on gaming and precision mice, preventing event queue backlog during rapid zooming.
- **Three-Way Color Theme System (`src/zupather_state.c`, `src/zupather_ui.c`)**:
  - Implemented 3 theme choices: `Light`, `Dark`, and `Use System Default` (serving as default on initial launch when no configuration exists).
  - Implemented real-time bi-directional synchronization between the live application theme and the Settings modal.
  - Custom owner-drawn combobox with 150px width accommodating full text `Use System Default` without clipping and 24px height matching numeric inputs.
  - Persistent theme configuration saved to `zupathconf.inf` under `Theme=`.
- **Configurable Action History Capacity (`src/undo_redo.c`, `src/dialog_modals.c`)**:
  - Added configurable undo/redo snapshot limit (default: 64 actions) via a compact numeric input box with memory boundary validation.
  - Maintained zero disk cache overhead with pure in-memory state retention.

### Fixed
- **SVG Transform Hierarchy & Matrix Stacking (`src/svg_path.c`)**:
  - Stack-based XML tree walker (supporting up to 64 levels of nested `<g>` elements) with 2D affine transformation matrices ($3 \times 3$ homogeneous coordinates) and full parent-to-child matrix concatenation ($M_{\text{eff}} = M_{\text{parent}} \times M_{\text{child}}$).
  - Full support for `matrix`, `translate`, `scale`, `rotate`, `skewX`, and `skewY`.
  - Resolved curve stacking issues on radial pinwheels and multi-spiral paths exported from Affinity Designer and Adobe Illustrator.
  - Corrected artboard coordinate offsets, mapping paths accurately into project canvas world coordinates.
- **Bézier Node & Handle Vector Transformation**:
  - Transformed both anchor positions $(x, y)$ as spatial points and tangent handles as relative direction vectors under any affine mapping with full sub-pixel floating-point precision.
- **Non-Renderable Block Isolation**:
  - Automatically skipped definition blocks (`<defs>`, `<clipPath>`, `<mask`, `<style>`, `<metadata>`), preventing masks and clip geometries from being imported as playable Zuma curve tracks.

---

## Verification & Build Details
- **Binary**: `ZuPather.exe`
- **Size**: ~2.7 MB
- **Compiler**: GCC / MinGW-w64 (Pure C99, Win32 API)
- **CI Status**: Multi-compiler matrix (MSVC x64 & MinGW-w64 x64) via GitHub Actions
