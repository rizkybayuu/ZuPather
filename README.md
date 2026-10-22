# ZuPather (v0.1.4.2)

> **Version:** 0.1.4.2 (Hierarchical SVG Transform Engine & Multi-Track Vector Ingestion Release)  
> **Target Game:** *Zuma Deluxe* v1.0.0.1 & *ZuCompleluxe* Multi-Curve Framework  
> **Project Scope:** High-Precision Vector Path Editor, Asset Composer & Direct-to-Engine Exporter  
> **Core Language:** Pure C (C99 / Win32 API)  
> **Changelog:** [CHANGELOG.md](CHANGELOG.md)  
> **License:** [LICENSE.md](LICENSE.md) (MIT License)

**ZuPather** is an open-source, high-precision visual vector path creation suite, level asset composer, and binary curve compiler engineered specifically for *Zuma Deluxe* (v1.0.0.1) and the extended *ZuCompleluxe* multi-curve engine. It bridges professional vector graphic design suites (such as **Affinity Designer**, **Canva**, and **Adobe Illustrator**) with the proprietary binary curve format (`.dat`, `CURV`) required by the 2003 PopCap engine.

Built entirely in standalone C99 using native Win32 GDI/User32 APIs, ZuPather operates as a single unified executable with zero external runtime DLL dependencies, providing sub-pixel accuracy, real-time physics roll simulation, multi-layer visual composition, and direct-to-game level synchronization.

---

## Core Architecture & Engineering Philosophy

ZuPather is built around four foundational engineering principles:

### 1. Analytical Arc-Length Parameterization & Drift-Free Delta Quantization
In the *Zuma Deluxe* engine, rolling balls must traverse paths at a strictly uniform physical velocity. Standard parametric Bézier curves $B(t)$ evaluated uniformly across parameter $t \in [0, 1]$ suffer from velocity distortions ($\|B'(t)\| \neq \text{const}$), causing balls to accelerate on long spans and stall in sharp hairpin turns.
- **Continuous Numerical Integration**: ZuPather calculates cumulative curve arc length $S = \int_0^1 \|B'(t)\| dt$ using adaptive numerical subdivision.
- **Equidistant Sub-Pixel Resampling**: Paths are resampled at exactly **$\Delta s = 1.0\text{ pixel}$** spacing across the entire trajectory.
- **Drift-Free Delta Accumulation**: Coordinates in Zuma's binary `.dat` files are serialized as signed 8-bit relative deltas:
  $$\Delta x_i = \text{round}\left((x_i - x_{\text{prev}}) \times 100\right), \quad \Delta y_i = \text{round}\left((y_i - y_{\text{prev}}) \times 100\right)$$
  ZuPather maintains an internal fractional accumulator during rasterization, guaranteeing **zero cumulative floating-point rounding drift** across thousands of waypoints.

### 2. Standalone Single-Binary Architecture (Zero External Dependencies)
- **Native Win32 Subsystem**: ZuPather compiles directly into a standalone 2.3 MB binary with zero third-party GUI framework overhead (no Electron, no Qt, no .NET runtime).
- **Embedded Asset Pipeline**: All required UI fonts (Atkinson Hyperlegible, JetBrains Mono), graphical controls, and application icons are compiled directly into the executable via native Windows resource scripts (`res/zupather.rc`).
- **High-Performance Double Buffering**: Smooth, flicker-free rendering on Windows 7, 8, 10, and 11 with instantaneous pan, zoom, and coordinate transform calculations.

### 3. Non-Destructive Canvas Project Model (`.zupath`)
- **Lossless Project State**: Working designs are stored as structured JSON projects (`.zupath`), preserving original Bézier control anchors, tangent handles, Frog shooter anchors, Treasure Point coin waypoints, Cutout overlays, Sprites, and background Alpha masks.
- **Multi-Curve Layout Support**: Native multi-track level definitions supporting from 1 path up to 10 independent concurrent active curves per level (`curve` through `curve10`), designed specifically to interface with the *ZuCompleluxe* extended engine.

### 4. Safe Engine Synchronizer & Core Game File Protection
- **Levels.xml Synchronization**: Automatically parses, generates, and injects `<Graphics>` declarations, track counts, and progression assignments directly into Zuma's `levels.xml`.
- **Automatic Safety Backups**: Every write or export operation to the game directory automatically generates a timestamped `levels.xml.bak` fallback.
- **Vanilla Level Integrity Protection**: Official original Zuma Deluxe levels registered in `StageProgression` are strictly locked and protected against inadvertent renaming, deletion, or overwriting.

---

## Key Features

- **Vector Bézier Path Engine**:
  - Analytical cubic Bézier curve calculation with exact arc-length parameterization.
  - Uniform equidistant resampling ($\Delta s = 1.0\text{ px}$) eliminating ball rolling speed distortion.
  - Drift-free delta quantization preserving sub-pixel fractional accuracy in binary `.dat` (`CURV`) serialization.
  - Multi-curve architecture supporting up to 10 independent active tracks per level (`curve` through `curve10`) for *ZuCompleluxe*.
- **Interactive WYSIWYG Level Authoring Suite**:
  - **Vector Pen Tool**: Dynamic Bézier node placement, continuous tangent handle manipulation, node translation, and path closing.
  - **Canvas Affect Transform**: Selectively scale or shift Frog/Coins, Paths & Curve Attributes, and Cutout/Sprite/Alpha Overlays proportionally with non-destructive bilinear resampling from pristine original buffers (`orig_bits`).
  - **Frog Shooter Placement**: Interactive positioning of the central frog anchor (`gx`, `gy`) with real-time pad visualization.
  - **Treasure Point Coin Placer**: Interactive coin waypoint positioning with automatic curve proximity indexing.
  - **Attribute Spans Subsystem**: Tag curve segments with in-tunnel occlusion flags, speed modifiers, and depth priority tiers (`0..4`).
  - **Visual Asset Composition**: Layer background artwork, foreground tunnel cutouts (`_tunnel*.gif`), dynamic sprites, and alpha masks with live alpha blending.
- **Real-Time Dynamic Ball Roll Simulation**:
  - Live physics roll simulation executing at authentic Zuma Deluxe physical speed.
  - Dynamic 11-color ball visualization with exact ball spacing, tunnel occlusion, and skull terminus deceleration.
- **Zuma Deluxe Map Manager & Direct Engine Exporter**:
  - Dual-pane manager synchronizing local Canvas Projects (`.zupath`) with detected Zuma Deluxe game installations.
  - Tri-state level categorization with core vanilla level protection (prevents accidental overwriting or deletion of official progression maps).
  - Direct 1-click exporter: updates `<Graphics>` definitions in `levels.xml`, writes `.dat` binaries, registers progression, and creates timestamped safety backups (`levels.xml.bak`).
- **Bi-Directional Vector Exchange & Headless CLI**:
  - Full SVG import/export compatibility with vector design suites (Affinity Designer, Canva, Adobe Illustrator).
  - **Hierarchical SVG Transform Engine**: 2D affine matrix stack parsing (`matrix`, `translate`, `scale`, `rotate`, `skewX`, `skewY`) supporting deeply nested `<g transform="...">` groups, artboard offsets, and multi-track rotational patterns without curve stacking.
  - Headless CLI suite for batch compilation, conversion, and inspection (`svg2dat`, `dat2svg`, `genxml`, `info`).
- **Zero-Dependency Win32 Native Executable**:
  - Pure C99 compiled into a standalone 2.3 MB binary with embedded assets and fonts, requiring no external runtimes or DLLs.
  - Custom owner-drawn Dark and Light themes with instant runtime switching.

---

## Repository Structure

```
ZuPather/
├── assets/                       # Embedded application assets & design resources
│   ├── fonts/                    # High-legibility UI typography (SIL Open Font License)
│   │   ├── AtkinsonHyperlegible-Bold.ttf
│   │   ├── AtkinsonHyperlegible-Regular.ttf
│   │   ├── JetBrainsMono-Bold.ttf
│   │   ├── JetBrainsMono-Regular.ttf
│   │   └── OFL.txt               # SIL Open Font License v1.1 & copyright statements
│   └── media/                    # Ball sprites, icons, and UI visual indicators
│       ├── baBall*.png           # 11 distinct ball color sprite previews
│       ├── COIN.png              # Treasure point coin marker
│       ├── SKULL.png             # Path endpoint skull marker
│       ├── SMALLFROGonPAD.png    # Frog shooter indicator
│       ├── ZUpather.ico          # Application multi-resolution icon
│       └── ZuSPLASHscreen.jpg    # Application splash screen visual
├── docs/                         # Reverse-engineered format specifications & architecture notes
│   ├── ZUMA_CURV_PATH_SPECIFICATION.md
│   ├── ZUMA_LEVELS_XML_SPECIFICATION.md
│   └── ZUMA_CURV_ATTRIBUTE_MODE_DESIGN.md
├── res/                          # Win32 resource definitions & compiler scripts
│   └── zupather.rc               # Resource script (icons, fonts, manifests)
├── src/                          # Modular C99 source files & headers
│   ├── bezier_path.c / .h        # Bézier math, arc-length integration & delta quantization
│   ├── canvas_render.c / .h      # High-performance GDI canvas renderer & double-buffer
│   ├── dialog_modals.c / .h      # Themed modal dialogues, map manager & export workflows
│   ├── hud_overlay.c / .h        # Viewport navigation bar, tools, zoom & status indicators
│   ├── image_loader.c / .h       # Asset decoding pipeline (stb_image integration)
│   ├── level_io.c / .h           # Binary CURV (.dat) serializer & XML parser
│   ├── level_progression.c / .h  # Zuma Deluxe stage progression resolver & mapper
│   ├── svg_path.c / .h           # SVG vector command tokenizer & path generator
│   ├── undo_redo.c / .h          # Transactional history & undo/redo command stack
│   ├── zupather_state.c / .h     # Global application state management & configuration
│   └── zupather_ui.c             # Win32 window procedure, message dispatcher & CLI entry point
├── .gitignore                    # Comprehensive Git exclusion rules
├── build.bat                     # Unified 1-click build script
├── CHANGELOG.md                  # Detailed version release notes
├── LICENSE.md                    # Legal notices, disclaimers & MIT license
└── README.md                     # Project documentation
```

---

## Build Instructions

### Prerequisites
- **Operating System**: Windows 7 / 8 / 10 / 11 (32-bit or 64-bit).
- **C Compiler**: MinGW-w64 (`gcc.exe`), Clang, or MSVC (`cl.exe`).

### 1-Click Master Build (`build.bat`)
Open a command prompt in the repository root and execute:

```cmd
build.bat
```
*(To build non-interactively in automated pipelines, use `build.bat --no-pause`)*.

The build script will:
1. Detect installed compilers adaptively (MinGW-w64, Clang, or MSVC).
2. Compile application resources via `windres.exe` (`res/zupather.rc`).
3. Compile all C source modules into the single unified binary: **`ZuPather.exe`** (~2.3 MB).
4. Perform binary verification and report compiled artifact size.

---

## Technical Specifications: Binary Curve Format (`.dat`)

Zuma's native `.dat` path format is a structured binary file containing a 16-byte header followed by dense point records:

```
[Offset]  Type       Field            Description
+0x00     uint32_t   magic            0x56525543 ("CURV" in ASCII)
+0x04     int32_t    version          Format version (Always 2 in retail Zuma Deluxe)
+0x08     int32_t    edit_type        Editing mode flag (0 = Raw Waypoints, 1 = Dense Resampled)
+0x0C     int32_t    num_points       Total count of point records (N)

[Point Stream: Offset +0x10 onwards, N * 4 bytes]
Byte 0:   int8_t     delta_x          Relative X step * 100 (-128..127)
Byte 1:   int8_t     delta_y          Relative Y step * 100 (-128..127)
Byte 2:   uint8_t    flags            Bit 0: In-Tunnel flag (1 = underneath bridge/cutout)
Byte 3:   uint8_t    priority         Rendering depth layer (0 = Default, 1..4 = Elevated)
```

The starting coordinate $(x_0, y_0)$ is reconstructed from the first delta record:
$$x_0 = \frac{\Delta x_0}{100.0}, \quad y_0 = \frac{\Delta y_0}{100.0}$$
Subsequent coordinates are calculated iteratively:
$$x_i = x_{i-1} + \frac{\Delta x_i}{100.0}, \quad y_i = y_{i-1} + \frac{\Delta y_i}{100.0}$$

---

## Build Instructions

ZuPather can be built on Windows using either the native automated batch script or cross-toolchain CMake.

### 1. Automated Script (`build.bat`)
Run the self-contained build script which automatically scans for available GCC, Clang, or MSVC toolchains and embeds resources:
```cmd
build.bat
```
*(Append `--no-pause` for CI/unattended script execution).*

### 2. CMake Build (Visual Studio / MSVC)
```cmd
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

### 3. CMake Build (MinGW / MSYS2)
```bash
mkdir build
cd build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

---

## Command-Line Interface (CLI)

In addition to the interactive GUI, `ZuPather.exe` provides a comprehensive headless CLI:

### 1. Compile SVG Path to Zuma Binary `.dat`
```cmd
ZuPather.exe svg2dat track.svg track.dat --step 1.0
```

### 2. Decompile Zuma Binary `.dat` to SVG Vector Path
```cmd
ZuPather.exe dat2svg track.dat track.svg --res 640x480
```

### 3. Inspect Path Characteristics & Tunnel Density
```cmd
ZuPather.exe info "levels/coaster/coaster.dat"
```

### 4. Generate `levels.xml` Snippet from Project
```cmd
ZuPather.exe genxml my_project.zupath
```

---

## License & Legal Disclaimer

- **Trademarks & Copyrights**: *Zuma* and *Zuma Deluxe* are registered trademarks and copyrights of **PopCap Games, Inc.** (a subsidiary of **Electronic Arts Inc.**). Retail distributions analyzed represent releases through **GameHouse, Inc.** and **Steam**.
- **Non-Commercial Tooling & Research**: This repository is an independent reverse engineering and level tooling project conducted strictly for educational, preservation, and interoperability purposes under statutory exemptions (e.g., Title 17 U.S.C. § 1201(f)). It is not affiliated with or endorsed by PopCap Games, Electronic Arts, or GameHouse.
- **No Proprietary Assets Distributed**: This repository does not distribute proprietary game archives (`.pak`), vanilla background artwork, game music/soundtracks, or retail executables. Users must own an authentic retail copy of *Zuma Deluxe* to test and run exported levels in the game.
- **Source Code License**: All original code, tools, resources, and documentation in this repository are licensed under the [MIT License](LICENSE.md).

---

## Third-Party Credits & Typography

ZuPather embeds and utilizes the following open-source libraries and typefaces:

- **[Atkinson Hyperlegible](https://fonts.google.com/specimen/Atkinson+Hyperlegible)**: Copyright (c) 2020-2021, Braille Institute of America, Inc., with Reserved Font Name "Atkinson Hyperlegible". Licensed under the [SIL Open Font License 1.1](assets/fonts/OFL.txt). Engineered to provide maximum character distinction for UI labels, menus, and viewport controls.
- **[JetBrains Mono](https://www.jetbrains.com/lp/mono/)**: Copyright (c) 2020 JetBrains s.r.o., with Reserved Font Name "JetBrains Mono". Licensed under the [SIL Open Font License 1.1](assets/fonts/OFL.txt). Utilized for monospace coordinate readouts, attribute inspector tables, and numerical telemetry.
- **[stb_image & stb_image_write](https://github.com/nothings/stb)**: Authored by Sean T. Barrett (nothings) and contributors. Released into the Public Domain / MIT Dual License. Provides C99 image raster ingestion.

