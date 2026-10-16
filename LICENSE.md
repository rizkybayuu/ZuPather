# Legal Notice, Disclaimer & License

## 1. Intellectual Property & Trademark Acknowledgment

- **Zuma** and **Zuma Deluxe** are registered trademarks and copyrights of **PopCap Games, Inc.**, originally founded in 2000 and acquired in 2011 by **Electronic Arts Inc. (EA)**.
- Specific retail builds targeted and supported for level deployment represent the distribution release provided through **GameHouse, Inc.** (a subsidiary of **RealNetworks, Inc.**) and **Valve Corporation (Steam)**.
- All trademarks, service marks, trade names, product names, graphical assets, animations, sound effects, musical compositions, and characters referenced herein remain the exclusive intellectual property of their respective copyright and trademark holders: PopCap Games, Inc., Electronic Arts Inc., and GameHouse / RealNetworks, Inc.

Neither the authors of this repository nor the *ZuPather* project are affiliated with, endorsed by, sponsored by, or associated in any manner with PopCap Games, Inc., Electronic Arts Inc., GameHouse, Inc., or RealNetworks, Inc.

---

## 2. Non-Commercial Research, Tooling & Interoperability Purpose

This repository and the ZuPather software suite are engineered strictly for **non-commercial, educational, preservation, modding tooling, and interoperability purposes**, including:

1. **Software Interoperability**: Implementing analytical curve rasterization, arc-length resampling, and delta encoding to generate valid binary curve files (`.dat`, `CURV` format) and level descriptor blocks (`levels.xml`) compatible with the 2003 PopCap engine and the extended *ZuCompleluxe* multi-curve framework.
2. **Software Preservation & Community Modding**: Providing a modern, high-precision visual design toolchain on contemporary operating systems (Windows 7, 8, 10, and 11) to preserve and extend the creative lifecycle of legacy game architectures.
3. **Clean-Room Engineering & Reverse Engineering Protections**: Developed under statutory provisions recognizing reverse engineering for interoperability, including Title 17 U.S.C. § 1201(f) (Reverse Engineering exemption under the Digital Millennium Copyright Act) and Directive 2009/24/EC of the European Parliament on the legal protection of computer programs.

---

## 3. Strict Asset Non-Distribution Policy

- **No Proprietary Game Assets Distributed**: This repository does **NOT** host, mirror, repackage, or distribute any proprietary, copyrighted game assets belonging to PopCap Games, Electronic Arts, or GameHouse. This includes, without limitation:
  - Original PopCap resource archives (`.pak` files).
  - Original level path data (`.dat` CURV coordinate files from retail stages).
  - Original background art, textures, sprites, UI elements, and font sheets.
  - Original audio tracks, music files (`.mo3`, `.ogg`, `.wav`), and sound effects.
  - Original commercial game binaries (`Zuma.exe`, `bass.dll`, `zumares.dll`) or retail installers.
- **Requirement of Legitimate Copy**: To play or test levels authored in ZuPather, users **must independently own a legally acquired, authentic copy of *Zuma Deluxe* (v1.0.0.1)** obtained through authorized retail channels (e.g., PopCap, GameHouse, EA, or Steam).

---

## 4. License for Original Contributions

The original code, resources, build scripts, and documentation authored specifically as part of the *ZuPather* project—namely:
- Win32 UI framework, dark/light theme engine, and canvas compositor (`src/zupather_ui.c`, `src/canvas_render.c`, `src/dialog_modals.c`, `src/hud_overlay.c`)
- Bézier mathematics, arc-length numerical integration, and delta quantization engine (`src/bezier_path.c`, `src/bezier_path.h`)
- Binary CURV serializer, XML parser, and map management synchronizer (`src/level_io.c`, `src/level_progression.c`)
- SVG vector parser and path importer (`src/svg_path.c`)
- Undo/redo transactional history system (`src/undo_redo.c`)
- Resource definitions, application icons, and build pipeline (`res/zupather.rc`, `build.bat`)
- Technical specifications and memory maps in `docs/`

are distributed under the terms of the **MIT License**:

```text
Copyright (c) 2026 ZuPather Project Contributors / Rizky Bayuu

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 5. Third-Party Software, Fonts & Asset Notices

### Atkinson Hyperlegible Font
- **Copyright**: Copyright (c) 2020-2021, Braille Institute of America, Inc., with Reserved Font Name "Atkinson Hyperlegible".
- **License**: [SIL Open Font License (OFL), Version 1.1](assets/fonts/OFL.txt) (http://scripts.sil.org/OFL).
- **Official Source**: [Google Fonts: Atkinson Hyperlegible](https://fonts.google.com/specimen/Atkinson+Hyperlegible) / [Braille Institute](https://brailleinstitute.org/freefont/)
- **Role in ZuPather**: Primary user interface typography utilized across menus, dialogs, buttons, and HUD indicators. Specifically chosen for high character distinction and optimal legibility in low-contrast, dark/light modding environments.

### JetBrains Mono Font
- **Copyright**: Copyright (c) 2020 JetBrains s.r.o., with Reserved Font Name "JetBrains Mono".
- **License**: [SIL Open Font License (OFL), Version 1.1](assets/fonts/OFL.txt) (http://scripts.sil.org/OFL).
- **Official Source**: [JetBrains Mono](https://www.jetbrains.com/lp/mono/)
- **Role in ZuPather**: Fixed-pitch monospace typography employed for numeric coordinate inspection, delta telemetry, curve point metrics, and command-line interfaces.

### stb_image & stb_image_write
- **Author**: Sean T. Barrett (nothings) and community contributors.
- **License**: Public Domain / MIT License (Dual-licensed).
- **Official Source**: [stb single-file public domain libraries](https://github.com/nothings/stb)
- **Role in ZuPather**: Embedded C99 image parsing for level background art, tunnel cutouts, sprites, and alpha masks.

### Reverse Engineering Specifications
- Documents in `docs/` contain independent technical descriptions derived from clean-room static and dynamic analysis of binary formats (`.dat`, `CURV`, `levels.xml`) conducted strictly for the purpose of software interoperability.
