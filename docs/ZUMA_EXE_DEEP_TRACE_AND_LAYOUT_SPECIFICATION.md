# Zuma.exe Deep-Trace & ZuPather Layout Specification
## Comprehensive Reverse-Engineering Reference, Binary Call Traces, Font Metrics, and Exhaustive 134-Element Catalog

> **Document Version**: 2.0 (Exhaustive Ground-Truth Engineering Reference)  
> **Target Audience**: AI Architecture Planners (Claude), Reverse Engineers, and Tool Implementers  
> **Source Artifacts Analyzed**:
> - `Zuma.exe` (PopCap Games v1.0.0.1 PE binary, entry point `0x004e12c0`, text section `0x00401000..0x004f7000`)
> - `ZuCompleluxe/src/zumareshandler.dll.c` (Widescreen engine injection, coordinate projection, code caves)
> - `ZuCompleluxe/src/zrh_layout.c` (XML parser and layout bridge)
> - `ZuPather/src/layout_editor.c` & `ZuPather/src/layout_editor.h` (Interactive Layout Editor)
> - `properties/layout.xml` (Canonical 17-section, 134-element UI configuration)
> - PopCap `ImageFont` descriptors (`fonts/*.txt`) and font sprite sheets (`fonts/*.gif`)
> - High-precision pixel telemetry from Zuma Compleluxe 1200x675 runtime captures.

---

## 1. Executive Reverse-Engineering Architecture & Universal Laws

### 1.1 The Dual-Resolution Coordinate Paradigm
*Zuma Deluxe* operates across two distinct coordinate environments:
1. **Vanilla 640x480 Space**: All original PopCap game logic, widget hierarchies, hitboxes, and hardcoded `DrawString` / `DrawImage` calls are structured around a $640 \times 480$ 4:3 canvas.
2. **ZuCompleluxe 1200x675 Widescreen Space**: The engine expands the viewport to 16:9 widescreen ($1200 \times 675$).
   - Horizontal stretch factor: $1200 / 640 = 1.875\times$
   - Vertical stretch factor: $675 / 480 = 1.40625\times$
   - Uniform scaling factor: $zscale = 1.40625\times$ ($900 / 640$)
   - Horizontal pillarbox centering offset: $off\_x = (1200 - 900) / 2 = 150\text{ px}$.

### 1.2 The Asset Resolution Law (`imagescustomres/` vs `images/`)
PopCap's `SexyAppFramework` `Graphics::DrawImage` (`0x0044D470`) and `Graphics::DrawImageCel` (`0x00450840`) **never perform runtime raster stretching**.
- **Rule 1 (Pre-Scaled Assets)**: If a sprite exists in `imagescustomres/`, it is already pre-scaled on disk to $1.40625\times$ (e.g. `mmscreen.jpg`, `advscreen.jpg`, `DialogBox.gif`, `menubar.gif`).
- **Rule 2 (1:1 Blitted Assets)**: If a sprite only exists in `images/`, it is blitted at **exact 1:1 pixel dimensions** without scaling (e.g. `smallfrogonpad.gif` $75\times75$, `baBall*.gif` $32\times32$, `hole.gif` $82\times81$, `checkbutton.gif` $50\times46$).
- **Rule 3 (ZuPather Viewport Law)**: ZuPather must never multiply sprite dimensions by $1.40625\times$. The viewport zoom scale $vscale = \text{window\_width} / 1200.0f$ is the only scaling factor applied when rendering to the editor canvas.

### 1.3 PopCap `ImageFont` Architecture & Metrics Law
PopCap games do not use standard Windows TrueType GDI rasterization for in-game text. All fonts are custom `ImageFont` resources loaded via descriptors (`fonts/*.txt`) and alpha-masked sprite sheets (`fonts/*.gif`).
```
                +---------------------------------------+  <-- Glyph Top Box (el->y)
                |                                       |
                |   Glyph Ascender Area                 |
                |   (Ascent - AscentPadding) = aYOffset |
                |                                       |
Baseline ====== |=======================================|  <-- Engine Baseline (DrawString y)
                |   Glyph Descender Area                |
                +---------------------------------------+  <-- Glyph Bottom Box
```

#### Exact Metrics Table for All 14 Zuma Deluxe Fonts (Binary Map from 0x0048AE90):
| Font Resource ID | Asset Name | Point Size | mAscent | mAscentPadding | aYOffset (Top to Baseline) | Default Tint / Color | Usage in Game |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`FONT_BROWNTITLE`** (`0x00547f50`) | `NativeAlienExtended16` | 16 pt | 36 px | 10 px | **26 px** | White `RGB(255, 255, 255)` / Yellow | Main Menu WelcomeText, TempleCleared Title |
| **`FONT_DIALOG`** (`0x00547ef8`) | `Cancun10` (Shadow1) | 10 pt | 10 px | 0 px | **10 px** | `RGB(91, 55, 0)` / Yellow `RGB(255, 255, 0)` | UserSwitchText, Dialog body, HUD StageName (All Cancun10 layers net 10 px) |
| **`FONT_MAIN10`** (`0x00547e38`) | `Cancun10` | 10 pt | 20 px | 10 px | **10 px** | Yellow `RGB(255, 234, 1)` | Help bullets, dialog controls |
| **`FONT_MAIN10OUTLINE`** (`0x00547f10`) | `Cancun10` (Outline: `_Cancun10` + `_CancunOUTLINE10`) | 10 pt | 20 px | 10 px | **10 px** | White `RGB(255, 255, 255)` / Yellow `RGB(255, 255, 0)` | Gauntlet DoorName, PracticeText |
| **`FONT_MAIN10OUTLINE2`** (`0x00547fe0`) | `Cancun10` (Outline 2) | 10 pt | 20 px | 10 px | **10 px** | Yellow `RGB(255, 234, 1)` | TempleCleared Subtitle |
| **`FONT_MAIN10OUTLINE3`** (`0x0054804c`) | `Cancun10` (Outline 3) | 10 pt | 20 px | 10 px | **10 px** | Outlined Variant | Cutscene subtitle variants |
| **`FONT_MAIN12`** (`0x00547e08`) | `Cancun12` | 12 pt | 12 px | 0 px | **12 px** | Yellow `#FFF000` | Generic 12pt Cancun fallback |
| **`FONT_MONODIGIT`** (`0x00548054`) | `Cancun13` | 13 pt | 13 px | 0 px | **13 px** | Yellow `RGB(255, 255, 0)` | InGameHUD Numeric Score counter (`ScoreText`) |
| **`FONT_MAIN8`** (`0x00548060`) | `Cancun8` | 8 pt | 14 px | 6 px | **8 px** | White / Gray | Small stage badge numbers, footers |
| **`FONT_MAIN8OUTLINE`** (`0x00547f44`) | `Cancun8` (Outline: `_Cancun8` + `_CancunFAT8`) | 8 pt | 14 px | 6 px | **8 px** | Adv: `RGB(40, 83, 76)`, Gaunt: `RGB(91, 1, 10)` | Adventure & Gauntlet HighscoreNames, HighscoreScores |
| **`FONT_FLOAT`** (`0x005480d4`) | `CancunFloat14` | 14 pt | 18 px | 3 px | **15 px** | Yellow / Orange | In-game combo score popups (`+100`) |
| **`FONT_HUGE`** (`0x0054807c`) | `NativeAlien48` | 48 pt | 80 px | 0 px | **80 px** | Yellow `RGB(255, 255, 0)` | Stage Complete banner (`StageText`, `CompleteText`), "ZUMA!" |
| **`FONT_TITLE`** (`0x00547e80`) | `NativeAlienExtended18` | 18 pt | 30 px | 0 px | **30 px** | Gold / Yellow | Dialog titles, screen headers |
| **`FONT_PLAIN`** (`0x00547e70`) | `Arial12Bold` | 12 pt | 14 px | 0 px | **14 px** | Black / Dark Brown | Options check text, fallback |

---

## 2. Deep-Trace: Reverse-Engineering Discoveries & Root Cause Diagnoses

### 2.1 The Dynamic Horizontal Centering Law
In `Zuma.exe` function `FUN_00431e50` (`0x00432195`), horizontal centering for dynamic text is computed at runtime based on the width of the rendered string:
```assembly
0x00432195: mov  ecx, ebx              ; ECX = Font (FONT_BROWNTITLE)
0x00432197: call dword ptr [edx+0x1C]  ; Font::StringWidth(str) -> returns width in EAX
0x0043219A: mov  ecx, 0x280            ; ECX = 640 (vanilla screen width)
0x0043219F: sub  ecx, eax              ; ECX = 640 - string_width
0x004321A1: mov  eax, ecx
0x004321A3: cdq
0x004321A4: sub  eax, edx
0x004321A6: sar  eax, 1                ; EAX = (640 - string_width) / 2 (Center X!)
0x004321A8: push eax                  ; Arg3: X
0x004321A9: lea  edx, [ebp-0x64]       ; Arg2: String pointer
0x004321AC: push edx
0x004321AD: mov  ecx, esi              ; ECX = Graphics*
0x004321AF: call 0x00447DF0           ; Graphics::DrawString(str, x, y)
```

In `ZuCompleluxe/src/zumareshandler.dll.c` (line 2280):
```c
void *pWelcomeCenter = FindPattern("80 02 00 00 2B C8 8B C1 99");
if (pWelcomeCenter) VerifyAndPatchDword(pWelcomeCenter, 640, content_w, "MainMenuWelcomeCenter");
```
The DLL patches `640` (`0x280`) to `1200` (`content_w`). As a result, the engine dynamically centers the text across the full 1200px canvas: $$X = \frac{1200 - \text{StringWidth}}{2}$$

### 2.2 The XML Representation Law
Because the engine centers the text dynamically, **no `x` coordinate should be defined in `layout.xml`**:
```xml
<WelcomeText y="6" />
<UserSwitchText y="53" />
```

### 2.3 The Critical Semantic Bug in ZuPather (`layout_editor.c` L6256)
- **Inspector Display**: The Inspector displays `---` for X when an element lacks `ATTR_HAS_X`.
- **Attribute Injection**: When the user drags `WelcomeText` on the canvas or types into the X input box:
  ```c
  sel_el->x = v; sel_el->flags |= ATTR_HAS_X;
  ```
  ZuPather marks `ATTR_HAS_X` and serializes `<WelcomeText x="600" y="6" />`.
- **Destructive Consequence**: This destroys dynamic centering in the engine. When player names change from "DEVTESTER" to long names like "CHRISTOPHER", the text is pushed off-center to the right.

### 2.4 The Unscaled Engine Y Law ($Y=6$ vs $Y=8$)
- `Zuma.exe` directly executes `push 0x20` (32 decimal) at `0x0043218F`.
- `zumareshandler.dll` **only patches X**, never touching or scaling Y.
- Therefore, in widescreen 1200x675, the engine baseline is **unscaled at Y = 32**.
- With `NativeAlienExtended16` $\text{aYOffset} = 36 - 10 = 26\text{ px}$:
  $$\text{top} = 32 - 26 = \mathbf{6}$$
- Previous $Y=8$ hypothesis was an error assuming $32 \times 1.40625 = 45$. Ground truth is $Y=6$.

### 2.5 `UserSwitchText` Dual-Nature (Widget Container Top 30 vs Visual Center 53)
- In `Zuma.exe` `FUN_004315B0` (`0x00431E05`), the hyperlink button widget is initialized:
  ```assembly
  0x00431E08: push 0x1E                  ; PUSH Y = 0x1E (30 DECIMAL) !
  0x00431E0B: call dword ptr [eax+0x34]  ; Widget::Resize(x, y=30, w, h=20)
  ```
- In `layout.xml`, `UserSwitchText y="53"` represents the **visual center** of the text box ($53 - 12 = 41$, baseline $\approx 45..48$).

### 2.6 Main Menu `Hat` (`IMAGE_MM_HAT`) Hidden Bug
- XML: `<Hat x="414" y="347" />`.
- Engine Hook: `zumareshandler.dll.c` L2269 patches `0x00432247` with $414, 347$.
- ZuPather Bug: `layout_editor.c` L1277 sets `el->is_conditional = true;`. Line 1861 hides conditional elements unless `[ ] Show Conditional` is checked.
- Fix: Strip `el->is_conditional = true;` from `Hat`.

### 2.7 Main Menu `Sun` (`IMAGE_SUNGLOW`) vs `IMAGE_MM_SUN` Engine Parity & Decoupling
- **Fundamental Separation of Packages**:
  1. `IMAGE_SUNGLOW` (`_mmsunglow.jpg`): The rotating solar flare rays ($229\times227$ vanilla, $322\times319$ customres). This is the **ONLY** entity bound to the canonical XML element `<Sun x="47" y="-89" />`.
  2. `IMAGE_MM_SUN` (`mmsun.gif` + `_mmsun.gif`): The Aztec Sun stone face ($87\times92$ vanilla, $122\times129$ customres). This is a **fixed scene companion** hardcoded in `Zuma.exe` at $(0, 2)$ relative to the `MainMenu` widget ($(150, 2)$ on widescreen screen). It is **NEVER** an XML tag in `layout.xml` and is strictly decoupled from the `<Sun>` element.
- **Engine Draw Order & Blend Modes (`Zuma.exe` @ `0x00431F9F`–`0x00432067`)**:
  1. `0x00431FA3`: `Graphics::SetAdditiveBlend(true)` + `SetColorizeImages(true)`.
  2. `0x00431FE0`: Modulated with pulsing yellow tint `Color(255, 255, 0, alpha=75..150)`.
  3. `0x0043202F`: `DrawImageRotatedF(IMAGE_SUNGLOW, sun_local_x, sun_local_y, rot)` (sun rays rendered first in background).
  4. `0x00432055`: `Graphics::SetAdditiveBlend(false)` + `SetColorizeImages(false)`.
  5. `0x00432067`: `DrawImage(IMAGE_MM_SUN, 0, 2)` rendered **ON TOP** with normal alpha blending.
  Because the stone face is drawn *after* the solar rays and in normal blend mode, it sits completely opaque and crisp on top of the staff, covering the center of the rays while the rays radiate behind it.
- **ZuPather Editor Architecture**:
  - `Sun` is an `XM_ABS` element rendered purely as `IMAGE_SUNGLOW` at $(x, y)$ with an orientation crosshair at $(x+114, y+113)$.
  - `IMAGE_MM_SUN` is rendered independently in the scene elements pass immediately following $Z \le 5$ (matching engine draw order), so moving or selecting `<Sun>` never drags or conflates the Aztec stone face.

---


### 2.8 Binary Opcode Deep-Trace & Ground-Truth Layout Proofs (Fase B1 & B2)

Exhaustive disassembly of `Zuma.exe` PE binary confirms the following layout, font, and coordinate facts:

#### 1. HighScores Table Screen Coordinates Derivation [STATUS: VERIFIED]
- **Mathematical Law**: Every widescreen screen coordinate in the HighScores table is rigorously derived from vanilla 640 space via:
  \[
  X_{\text{screen}} = 150 + \text{round}(X_{\text{vanilla}} \times 1.40625)
  \]
- **Scores Column (Right Edge)**: Vanilla $X=575 \rightarrow 150 + \text{round}(575 \times 1.40625) = 150 + 809 = \mathbf{959\text{ px}}$. Exactly matches `<HighscoreScores x="959" />`.
- **Adventure Names Column**: Vanilla $X=470 \rightarrow 150 + \text{round}(470 \times 1.40625) = 150 + 661 = \mathbf{811\text{ px}}$. Exactly matches `<HighscoreNames x="811" />`.
- **Gauntlet Names Column**: Vanilla $X=445 \rightarrow 150 + \text{round}(445 \times 1.40625) = 150 + 626 = \mathbf{776\text{ px}}$. Exactly matches `<HighscoreNames x="776" />`.
- **Clipping Right Boundary**: Vanilla $W=567 \rightarrow 150 + \text{round}(567 \times 1.40625) = 150 + 797 = \mathbf{947\text{ px}}$. Exactly matches `<HighscoreDate x="947" />` and `<HighscoreClip w="797" />`.

#### 2. `HighscoreDate` is NOT a Text Element [STATUS: VERIFIED]
- In `Zuma.exe` at `0x0043689F` (Adventure) and `0x0043962C` (Gauntlet), the engine calls `Graphics::SetClipRect(0, 0, 567, 480)`.
- `HighscoreDate` in Adventure is the right-clip boundary anchor ($150 + 797 = 947$), identical in function to `HighscoreClip` ($W=797$) in Gauntlet. It does **not** draw date text. ZuPather suppresses fake text rendering and displays a non-intrusive clipping boundary guide.

#### 3. HighScores Authentic Typography & Colors [STATUS: VERIFIED]
- Both `HighscoreNames` and `HighscoreScores` use **`FONT_MAIN8OUTLINE`** (`0x00547F44` = `Cancun8.txt` with `_Cancun8.gif` + `_CancunFAT8.gif`).
- Baseline offset: `aYOffset = Ascent (14) - AscentPadding (6) = 8 px`.
- **Adventure Color**: `0x004367AD: push 0x28534c` $\rightarrow$ **`RGB(40, 83, 76)`** for both names and scores.
- **Gauntlet Color**: `0x00439540: push 0x5b010a` $\rightarrow$ **`RGB(91, 1, 10)`** for both names and scores.

#### 4. StageComplete Cutscene Typography [STATUS: VERIFIED]
- **`StageText`** (`"STAGE %d"`): Uses **`FONT_HUGE`** (`0x0054807C` = `NativeAlien48.txt`, 48 pt). Centered horizontally at `pStageTextCX` (`0x00428FA3`, vanilla 320, widescreen 608). Patches immediate `push 150` at `0x004290D3` (`pStageCharDrawY`) and `0x00429089` (`pStageSparkleY`). In `0x00425B80`, the engine computes `top = y - Font::GetAscent() = 150 - 80 = 70`, confirming $Y=150$ is the baseline. Color is Yellow **`RGB(255, 255, 0)`** (`0x00FFFF00`).
- **`CompleteText`** (`"COMPLETE"` at `0x4ff2b4`): Uses **`FONT_HUGE`** (`0x0054807C`). Centered at 320/608 at `0x00429214`, patches immediate `push 250` at `0x00426DBF`, Yellow **`RGB(255, 255, 0)`**.

#### 5. TempleCleared / Congratulations Screen Typography [STATUS: VERIFIED for Title, Dialogue Traced]
- **`Title`** [STATUS: VERIFIED]:
  - **Actual String**: `"CONGRATULATIONS!"` (at `0x004FF2C4`).
  - **Function & Call**: Called at `0x00426EE0`–`0x00426F40` via `Widget::DrawCenteredText` (`0x004775A0`).
  - **Font**: `FONT_HUGE` (`0x0054807C` = `NativeAlien48.txt`, 48 pt).
  - **Color**: PopCap Yellow `RGB(255, 255, 0)` via `0x00426EDC: push 0x00ffff00` before `Graphics::SetColor`.
  - **Baseline**: $Y=120$ (`0x00426F37: push 0x78`) is the font baseline! Top of bounding box is $Y - \text{Ascent} = 120 - 80 = \mathbf{40\text{ px}}$.
  - **DLL Hook**: `zumareshandler.dll` (L2733) patches `congrats_title_y` into immediate `0x78` and centers horizontally via `hk_DrawCongratsTitle`.
- **`DialogText` & `FrogReply`** [STATUS: TRACED IN EXE]:
  - Cutscene dialog bubbles are added via `0x00428160` (God speech) and `0x00428000` (Frog reply), and drawn by loop `0x00426BC0`.
  - Both use Font ID 7 (`FONT_DIALOG` = `Cancun10.txt` via `0x52E4A0[7]` = `0x00547EF8`).
  - Color is PopCap Yellow `RGB(255, 255, 0)` (`0x00FFFF00` initialized at `0x004280B9`).

#### 6. InGameHUD Typography [StageName: UNVERIFIED, ScoreText: VERIFIED]
> *Rejection Rationale for StageName*: While `StageName` uses `FONT_DIALOG` (`0x00547EF8`) at `0x0040D090`, vanilla coordinates $X=200$ and $Y=15$ at `0x0040D0C2` do not map directly to $290$ and $8$ via standard 4:3 uniform scaling. Instead, the Menubar in ZuCompleluxe uses a 5-compartment procedural layout bridge in `zumareshandler.dll` (L3270–3350) where `stage_cx` is patched to 290 and `score_rx` to 640. Retained as unverified in editor code until final architectural signoff.
- **`ScoreText`** [VERIFIED]: Located in `Zuma.exe` at `0x0040D0D8`–`0x0040D11D` (`0x0040D0E1` calls `SetFont` with `FONT_MONODIGIT` `0x00548054` = `cancun13.txt`), right-aligned to `rightX=640` (vanilla 360 at `0x0040D10F`), $Y=17$ (`0x0040D100`), Yellow `RGB(255, 255, 0)`. Baseline is $Y = 17$ (top is $17 - 13 = 4$).

#### 7. Gauntlet Screen Typography & InfoBox [PracticeText: VERIFIED, InfoBox: UNVERIFIED]
- **`PracticeText`** [VERIFIED]: Uses **`FONT_MAIN10OUTLINE`** (`0x00547F10`), Yellow **`RGB(255, 234, 1)`**. Formula $150 + 150 \times 1.40625 = 361$ and $260 \times 1.40625 \approx 366$ matches XML `<PracticeText x="361" y="366" />`. Rendered strictly right-aligned against $X$, with $X=361$ serving as the text's right boundary.
- **`InfoBox`** [STATUS: VERIFIED - Text Word-Wrap Container]:
  - In `Zuma.exe` at `0x00436967` (Adventure) and `0x0043952E` (Gauntlet), `InfoBox` sets up a bounding rect `(20, 60, 150, 100)` and calls `Graphics::DrawStringWordWrap` (`vtable[0x88/4]`).
  - **Font**: `FONT_MAIN8OUTLINE` (`0x00547F44` = `Cancun8` outline).
  - **Colors**: Adventure `RGB(40, 83, 76)` (`0x00436988: push 0x28534c`); Gauntlet `RGB(91, 1, 10)` (`0x00439540: push 0x5b010a`).
  - **Widescreen Hooks**: `zumareshandler.dll` patches `gaunt_infobox_x/y/w/h` at L2635-2645 and intercepts word-wrap via `hk_GauntletWordWrap` (L1466) applying `g_layout.gaunt_infobox_line_spacing`.

## 3. Screen-by-Screen Exhaustive Catalog: All 17 Sections & All 134 Elements

### Section: `AdventureTemples` (Coordinate Space: `1200x675`)
Total Elements in Section: 8

#### Element 1: `<Temple1>`
- **Canonical XML**: `<Temple1 x="150" y="264" />`
- **Classification**: Sprite / Background Composite
- **Sprite / Asset**: `IMAGE_ADVTEMPLE1 (advtemple1.jpg)` | Dimensions: `192x100` | Cels: `1`
- **Font Specification**: None (Static Art)
- **Engine Call (`Zuma.exe`)**: `FUN_0042ba70: Graphics::DrawImage(advtemple1, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_temple1_x, adv_temple1_y -> ProjectLocalX/Y (0x004358a0)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1319: def_w=192, def_h=100, x=0, y=188. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 2: `<Temple2>`
- **Canonical XML**: `<Temple2 x="150" y="158" />`
- **Classification**: Sprite / Background Composite
- **Sprite / Asset**: `IMAGE_ADVTEMPLE2 (advtemple2.jpg)` | Dimensions: `190x100` | Cels: `1`
- **Font Specification**: None (Static Art)
- **Engine Call (`Zuma.exe`)**: `FUN_0042ba70: Graphics::DrawImage(advtemple2, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_temple2_x, adv_temple2_y -> ProjectLocalX/Y (0x004358d4)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1320: def_w=190, def_h=100, x=0, y=112. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 3: `<Temple2V>`
- **Canonical XML**: `<Temple2V x="150" y="158" />`
- **Classification**: Sprite / Overlay
- **Sprite / Asset**: `IMAGE_ADVTEMPLE2V (advtemple2v.gif)` | Dimensions: `190x100` | Cels: `1`
- **Font Specification**: None (Static Art)
- **Engine Call (`Zuma.exe`)**: `FUN_0042ba70: Graphics::DrawImage(advtemple2v, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_temple2v_x, adv_temple2v_y -> ProjectLocalX/Y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1320: def_w=190, def_h=100, x=0, y=112. Conditional overlay.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 4: `<Temple3>`
- **Canonical XML**: `<Temple3 x="705" y="176" />`
- **Classification**: Sprite / Background Composite
- **Sprite / Asset**: `IMAGE_ADVTEMPLE3 (advtemple3.jpg)` | Dimensions: `190x100` | Cels: `1`
- **Font Specification**: None (Static Art)
- **Engine Call (`Zuma.exe`)**: `FUN_0042ba70: Graphics::DrawImage(advtemple3, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_temple3_x, adv_temple3_y -> ProjectLocalX/Y (0x00435908)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1321: def_w=190, def_h=100, x=395, y=125. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 5: `<Temple3V>`
- **Canonical XML**: `<Temple3V x="705" y="176" />`
- **Classification**: Sprite / Overlay
- **Sprite / Asset**: `IMAGE_ADVTEMPLE3V (advtemple3v.gif)` | Dimensions: `190x100` | Cels: `1`
- **Font Specification**: None (Static Art)
- **Engine Call (`Zuma.exe`)**: `FUN_0042ba70: Graphics::DrawImage(advtemple3v, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_temple3v_x, adv_temple3v_y -> ProjectLocalX/Y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1321: def_w=190, def_h=100, x=395, y=125. Conditional overlay.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 6: `<Temple4>`
- **Canonical XML**: `<Temple4 x="395" y="1" />`
- **Classification**: Sprite / Background Composite
- **Sprite / Asset**: `IMAGE_ADVTEMPLE4 (advtemple4.jpg)` | Dimensions: `292x140` | Cels: `1`
- **Font Specification**: None (Static Art)
- **Engine Call (`Zuma.exe`)**: `FUN_0042ba70: Graphics::DrawImage(advtemple4, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_temple4_x, adv_temple4_y -> ProjectLocalX/Y (0x0043593c)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1322: def_w=292, def_h=140, x=174, y=1. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 7: `<Temple5>`
- **Canonical XML**: `<Temple5 x="327" y="208" />`
- **Classification**: Sprite / Background Composite
- **Sprite / Asset**: `IMAGE_ADVTEMPLE5 (advtemple5.jpg)` | Dimensions: `391x100` | Cels: `1`
- **Font Specification**: None (Static Art)
- **Engine Call (`Zuma.exe`)**: `FUN_0042ba70: Graphics::DrawImage(advtemple5, x, y) [ZuCompleluxe Extension]`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_temple5_x, adv_temple5_y -> ProjectLocalX/Y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1323: def_w=391, def_h=100, x=126, y=148. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 8: `<Middle>`
- **Canonical XML**: `<Middle x="150" y="101" />`
- **Classification**: Sprite / Sky & Cliff
- **Sprite / Asset**: `IMAGE_ADVMIDDLE (advmiddle.jpg)` | Dimensions: `640x150` | Cels: `1`
- **Font Specification**: None (Static Art)
- **Engine Call (`Zuma.exe`)**: `FUN_0042ba70: Graphics::DrawImage(advmiddle, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_middle_x, adv_middle_y -> ProjectLocalX/Y (0x0043586c)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1324: def_w=640, def_h=150, x=0, y=72. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

### Section: `AdventureDoors` (Coordinate Space: `1200x675`)
Total Elements in Section: 13

#### Element 9: `<Door>`
- **Canonical XML**: `<Door id="0" doorX="468" doorY="422" frogX="510" frogY="508" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 10: `<Door>`
- **Canonical XML**: `<Door id="1" doorX="566" doorY="380" frogX="610" frogY="512" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 11: `<Door>`
- **Canonical XML**: `<Door id="2" doorX="665" doorY="422" frogX="710" frogY="506" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 12: `<Door>`
- **Canonical XML**: `<Door id="3" doorX="150" doorY="231" frogX="229" frogY="307" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 13: `<Door>`
- **Canonical XML**: `<Door id="4" doorX="247" doorY="224" frogX="313" frogY="283" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 14: `<Door>`
- **Canonical XML**: `<Door id="5" doorX="319" doorY="218" frogX="385" frogY="264" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 15: `<Door>`
- **Canonical XML**: `<Door id="6" doorX="786" doorY="217" frogX="800" frogY="264" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 16: `<Door>`
- **Canonical XML**: `<Door id="7" doorX="866" doorY="217" frogX="883" frogY="270" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 17: `<Door>`
- **Canonical XML**: `<Door id="8" doorX="949" doorY="217" frogX="971" frogY="276" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 18: `<Door>`
- **Canonical XML**: `<Door id="9" doorX="459" doorY="49" frogX="499" frogY="121" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 19: `<Door>`
- **Canonical XML**: `<Door id="10" doorX="559" doorY="49" frogX="606" frogY="121" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 20: `<Door>`
- **Canonical XML**: `<Door id="11" doorX="669" doorY="49" frogX="724" frogY="120" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 21: `<Door>`
- **Canonical XML**: `<Door id="12" doorX="559" doorY="248" frogX="604" frogY="283" />`
- **Classification**: Compound Widget (Door & Frog Pad)
- **Sprite / Asset**: `IMAGE_ADVDOOR* (advdoor*.gif) & smallfrogonpad.gif` | Dimensions: `64x50 (door), 75x75 (frog)` | Cels: `3`
- **Font Specification**: None (Hitbox / Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_00435980: DrawDoorWaypoint(id, doorX, doorY, frogX, frogY)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_doors[id] -> Array of 13 door structures patched into memory tables`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1326: def_w=64, def_h=50. Dual hotspot: door (top-left) & frog (center).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

### Section: `AdventureButtons` (Coordinate Space: `1200x675`)
Total Elements in Section: 2

#### Element 22: `<MainMenu>`
- **Canonical XML**: `<MainMenu x="150" y="616" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_ADVMAINMENUBUTTON (advmainmenu.gif)` | Dimensions: `125x42` | Cels: `3`
- **Font Specification**: None (Text baked into sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_00435ec0: Widget::Resize(x, y, 125, 42)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_btn_mainmenu_x, adv_btn_mainmenu_y -> Patches 0x00435ed4`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1329: def_w=125, def_h=42, x=0, y=438, align=bottom-left.`
- **Authentic Alignment Semantics**: Bottom-Left anchored

#### Element 23: `<Play>`
- **Canonical XML**: `<Play x="912" y="620" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_ADVPLAYBUTTON (advplay.gif)` | Dimensions: `97x39` | Cels: `3`
- **Font Specification**: None (Text baked into sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_00435f10: Widget::Resize(x, y, 97, 39)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_btn_play_x, adv_btn_play_y -> Patches 0x00435f24`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1334: def_w=97, def_h=39, x=542, y=441, align=bottom-right.`
- **Authentic Alignment Semantics**: Bottom-Right anchored

### Section: `AdventureLabels` (Coordinate Space: `1200x675`)
Total Elements in Section: 15

#### Element 24: `<AdventureTitle>`
- **Canonical XML**: `<AdventureTitle x="150" y="0" />`
- **Classification**: Sprite / Screen Header
- **Sprite / Asset**: `IMAGE_ADVTITLE (advtitle.gif)` | Dimensions: `174x35` | Cels: `1`
- **Font Specification**: None (Stone Logo)
- **Engine Call (`Zuma.exe`)**: `FUN_0043641b: Graphics::DrawImage(advtitle, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_title_x, adv_title_y -> Patches 0x00436420`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1341: def_w=140, def_h=30, x=150, y=0. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Top-Left banner anchor

#### Element 25: `<HighscoreTitle>`
- **Canonical XML**: `<HighscoreTitle x="791" y="0" />`
- **Classification**: Sprite / Screen Header
- **Sprite / Asset**: `IMAGE_ADVHIGHSCORE (advhighscore.gif)` | Dimensions: `174x35` | Cels: `1`
- **Font Specification**: None (Stone Logo)
- **Engine Call (`Zuma.exe`)**: `FUN_00436450: Graphics::DrawImage(advhighscore, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_hs_title_x, adv_hs_title_y -> Patches 0x00436455`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1341: def_w=140, def_h=30, x=456, y=0. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Top-Right banner anchor

#### Element 26: `<BottomBox>`
- **Canonical XML**: `<BottomBox x="556" y="537" />`
- **Classification**: Sprite / Badge Backing
- **Sprite / Asset**: `IMAGE_ADVSTAGE (advstage.gif)` | Dimensions: `76x16` | Cels: `1`
- **Font Specification**: None (Backing stone)
- **Engine Call (`Zuma.exe`)**: `FUN_00436780: Graphics::DrawImage(advstage, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_bottom_box_x, adv_bottom_box_y -> Patches 0x00436785`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1345: def_w=76, def_h=16, x=289, y=382. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 27: `<StageNum>`
- **Canonical XML**: `<StageNum x="634" y="557" />`
- **Classification**: Text (Numeric Stage)
- **Sprite / Asset**: `None` | Dimensions: `30x20` | Cels: `1`
- **Font Specification**: FONT_MAIN8 (Cancun8.txt, Pt: 8, Ascent: 14, Pad: 6, Offset: 8, White)
- **Engine Call (`Zuma.exe`)**: `FUN_004367b0: DrawString(stg_str, x, y=baseline 396)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_stage_num_x, adv_stage_num_y -> Patches 0x004367b5`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1349: def_w=30, def_h=20, x=344, y=396. Baseline=y+8.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 28: `<StageText>`
- **Canonical XML**: `<StageText y="593" centerX="608" />`
- **Classification**: Text (Temple Name)
- **Sprite / Asset**: `None` | Dimensions: `300x32` | Cels: `1`
- **Font Specification**: FONT_BROWNTITLE (NativeAlienExtended16.txt, Pt: 16, Ascent: 36, Pad: 10, Offset: 26, Yellow RGB(255, 234, 1))
- **Engine Call (`Zuma.exe`)**: `FUN_004367e0: DrawString(temple_name, x, y=baseline 422)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_stage_text_y, adv_stage_center_x -> Dynamic horizontal center`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1353: def_w=300, def_h=32, centerX=608, y=593. HOTSPOT_CENTER_X.`
- **Authentic Alignment Semantics**: Horizontally Centered (centerX=608)

#### Element 29: `<Subtitle>`
- **Canonical XML**: `<Subtitle y="641" centerX="608" />`
- **Classification**: Text (Temple Subtitle / Lore)
- **Sprite / Asset**: `None` | Dimensions: `300x24` | Cels: `1`
- **Font Specification**: FONT_MAIN10 (Cancun10.txt, Pt: 10, Ascent: 20, Pad: 10, Offset: 10, Yellow RGB(255, 234, 1))
- **Engine Call (`Zuma.exe`)**: `FUN_00436810: DrawString(subtitle, x, y=baseline 456)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_subtitle_y, adv_subtitle_center_x -> Dynamic horizontal center`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1359: def_w=300, def_h=24, centerX=608, y=641. HOTSPOT_CENTER_X.`
- **Authentic Alignment Semantics**: Horizontally Centered (centerX=608)

#### Element 30: `<HighscoreScores>`
- **Canonical XML**: `<HighscoreScores x="959" y="98" />`
- **Classification**: Text (Score Column)
- **Sprite / Asset**: `None` | Dimensions: `60x80` | Cels: `1`
- **Font Specification**: FONT_MAIN8OUTLINE (Cancun8.txt with _Cancun8.gif & _CancunFAT8.gif, Pt: 8, Baseline Offset: 8, Color RGB(40, 83, 76) via 0x004367AD)
- **Engine Call (`Zuma.exe`)**: `0x00436836: DrawString(score, rightX - str_w, y=base_y + i*spacing)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_hs_scores_x, adv_hs_base_y -> Patches 0x0043683a`. Widescreen proof: $150 + \text{round}(575 \times 1.40625) = 959\text{ px}$.
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: bound to g_popcap_cancun_outline_8, RGB(40, 83, 76), DT_RIGHT | DT_TOP.`
- **Authentic Alignment Semantics**: Right-aligned score column (rightX=959, y=98)

#### Element 31: `<HighscoreDate>`
- **Canonical XML**: `<HighscoreDate x="947" />`
- **Classification**: Layout Metric / Clip Boundary (NOT a text element)
- **Sprite / Asset**: `None` | Dimensions: `97x60` (guide rect) | Cels: `0`
- **Font Specification**: None (Clipping Rect Parameter)
- **Engine Call (`Zuma.exe`)**: `0x0043689F: Graphics::SetClipRect(0, 0, 567, 480)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_hs_date_x -> Patches 0x00436885`. Widescreen proof: right edge $150 + \text{round}(567 \times 1.40625) = 947\text{ px}$.
- **ZuPather Implementation (`layout_editor.c`)**: Reclassified as Region element; fake date text string completely suppressed; renders subtle clipping guide boundary.
- **Authentic Alignment Semantics**: Right clip boundary anchor (x=947)

#### Element 32: `<HighscoreNames>`
- **Canonical XML**: `<HighscoreNames x="811" y="98" />`
- **Classification**: Text (Player Name Column)
- **Sprite / Asset**: `None` | Dimensions: `90x80` | Cels: `1`
- **Font Specification**: FONT_MAIN8OUTLINE (Cancun8.txt with _Cancun8.gif & _CancunFAT8.gif, Pt: 8, Baseline Offset: 8, Color RGB(40, 83, 76) via 0x004367AD)
- **Engine Call (`Zuma.exe`)**: `0x004368db: DrawString(name, x, y=base_y + i*spacing)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_hs_names_x, adv_hs_base_y -> Patches 0x004368e0`. Widescreen proof: $150 + \text{round}(470 \times 1.40625) = 811\text{ px}$.
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: bound to g_popcap_cancun_outline_8, RGB(40, 83, 76), DT_LEFT | DT_TOP.`
- **Authentic Alignment Semantics**: Left-aligned player names (x=811, y=98)

#### Element 33: `<HighscoreRow>`
- **Canonical XML**: `<HighscoreRow spacing="25" />`
- **Classification**: Layout Metric (Vertical Spacing)
- **Sprite / Asset**: `None` | Dimensions: `N/A` | Cels: `0`
- **Font Specification**: None (Metric)
- **Engine Call (`Zuma.exe`)**: `Loop counter: y_pos = base_y + row * spacing`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_hs_spacing (spacing=25)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: spacing=25. Used for vertical step per highscore entry.`
- **Authentic Alignment Semantics**: Metric parameter

#### Element 34: `<InfoBox>`
- **Canonical XML**: `<InfoBox x="178" y="84" w="150" h="141" lineSpacing="20" />`
- **Classification**: Text Container / Tooltip
- **Sprite / Asset**: `None` | Dimensions: `150x141` | Cels: `1`
- **Font Specification**: FONT_DIALOG (Cancun10.txt, Pt: 10, Offset: 18, Dark Brown RGB(91, 55, 0))
- **Engine Call (`Zuma.exe`)**: `FUN_00436950: DrawWordWrapped(info_text, rc)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_infobox_x/y/w/h/line_spacing`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1389: def_w=150, def_h=141, x=178, y=84, lineSpacing=20.`
- **Authentic Alignment Semantics**: Box bounds (178, 84, 150, 141)

#### Element 35: `<Stage13Badge>`
- **Canonical XML**: `<Stage13Badge x="556" y="537" />`
- **Classification**: Sprite / Secret Temple Badge
- **Sprite / Asset**: `IMAGE_ADVSTAGE (advstage.gif)` | Dimensions: `76x16` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_004369a0: Graphics::DrawImage(advstage, x, y) [Stage 13]`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_bottom_box_x, adv_bottom_box_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1345: Shared slot with BottomBox at (556, 537).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 36: `<Stage13Num>`
- **Canonical XML**: `<Stage13Num x="634" y="557" />`
- **Classification**: Text (Secret Stage Number)
- **Sprite / Asset**: `None` | Dimensions: `30x20` | Cels: `1`
- **Font Specification**: FONT_MAIN8 (Cancun8.txt, Pt: 8, Offset: 8, White)
- **Engine Call (`Zuma.exe`)**: `FUN_004369d0: DrawString("13", x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_stage_num_x, adv_stage_num_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1349: Shared slot with StageNum at (634, 557).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 37: `<Stage13Title>`
- **Canonical XML**: `<Stage13Title x="608" y="593" />`
- **Classification**: Text (Secret Temple Name)
- **Sprite / Asset**: `None` | Dimensions: `300x32` | Cels: `1`
- **Font Specification**: FONT_BROWNTITLE (NativeAlienExtended16.txt, Pt: 16, Offset: 26, Yellow RGB(255, 234, 1))
- **Engine Call (`Zuma.exe`)**: `FUN_00436a00: DrawString("TEMPLE OF THE SUN", x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_stage_text_y, adv_stage_center_x`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1377: def_w=300, def_h=32, x=608, y=593. HOTSPOT_CENTER_X.`
- **Authentic Alignment Semantics**: Horizontally Centered (centerX=608)

#### Element 38: `<Stage13Subtitle>`
- **Canonical XML**: `<Stage13Subtitle x="608" y="641" />`
- **Classification**: Text (Secret Temple Subtitle)
- **Sprite / Asset**: `None` | Dimensions: `300x24` | Cels: `1`
- **Font Specification**: FONT_MAIN10 (Cancun10.txt, Pt: 10, Offset: 10, Yellow RGB(255, 234, 1))
- **Engine Call (`Zuma.exe`)**: `FUN_00436a30: DrawString("FINAL CONFRONTATION", x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.adv_subtitle_y, adv_subtitle_center_x`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1383: def_w=300, def_h=24, x=608, y=641. HOTSPOT_CENTER_X.`
- **Authentic Alignment Semantics**: Horizontally Centered (centerX=608)

### Section: `GauntletDoors` (Coordinate Space: `1200x675`)
Total Elements in Section: 4

#### Element 39: `<Door>`
- **Canonical XML**: `<Door id="0" x="444" y="252" />`
- **Classification**: Interactive Door Widget
- **Sprite / Asset**: `IMAGE_GAUNTDOOR* (gauntdoor1.gif..4.gif)` | Dimensions: `100x120` | Cels: `3`
- **Font Specification**: None (Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_0042c320: Widget::Resize(x, y, w, h)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_doors[id] -> Array of 4 doors (ids 0..3)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: Door id=0 (444, 252), id=1 (476, 181), id=2 (500, 115), id=3 (523, 39).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 40: `<Door>`
- **Canonical XML**: `<Door id="1" x="476" y="181" />`
- **Classification**: Interactive Door Widget
- **Sprite / Asset**: `IMAGE_GAUNTDOOR* (gauntdoor1.gif..4.gif)` | Dimensions: `100x120` | Cels: `3`
- **Font Specification**: None (Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_0042c320: Widget::Resize(x, y, w, h)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_doors[id] -> Array of 4 doors (ids 0..3)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: Door id=0 (444, 252), id=1 (476, 181), id=2 (500, 115), id=3 (523, 39).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 41: `<Door>`
- **Canonical XML**: `<Door id="2" x="500" y="115" />`
- **Classification**: Interactive Door Widget
- **Sprite / Asset**: `IMAGE_GAUNTDOOR* (gauntdoor1.gif..4.gif)` | Dimensions: `100x120` | Cels: `3`
- **Font Specification**: None (Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_0042c320: Widget::Resize(x, y, w, h)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_doors[id] -> Array of 4 doors (ids 0..3)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: Door id=0 (444, 252), id=1 (476, 181), id=2 (500, 115), id=3 (523, 39).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 42: `<Door>`
- **Canonical XML**: `<Door id="3" x="523" y="39" />`
- **Classification**: Interactive Door Widget
- **Sprite / Asset**: `IMAGE_GAUNTDOOR* (gauntdoor1.gif..4.gif)` | Dimensions: `100x120` | Cels: `3`
- **Font Specification**: None (Widget)
- **Engine Call (`Zuma.exe`)**: `FUN_0042c320: Widget::Resize(x, y, w, h)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_doors[id] -> Array of 4 doors (ids 0..3)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: Door id=0 (444, 252), id=1 (476, 181), id=2 (500, 115), id=3 (523, 39).`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

### Section: `GauntletButtons` (Coordinate Space: `1200x675`)
Total Elements in Section: 6

#### Element 43: `<Back>`
- **Canonical XML**: `<Back x="399" y="624" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_GAUNTBACKBUTTON (GauntBackButton.jpg)` | Dimensions: `124x33` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_00435170: Widget::Resize(x, y, 124, 33)` (Widget +0x60, Button ID 1)
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_btn_back_x, gaunt_btn_back_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: def_w=124, def_h=33, x=177, y=444 (WS: 399, 624)`
- **Authentic Alignment Semantics**: Bottom-Center anchored

#### Element 44: `<Play>`
- **Canonical XML**: `<Play x="540" y="624" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_GAUNTPLAYBUTTON (GauntPlayButton.jpg)` | Dimensions: `140x33` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_00435170: Widget::Resize(x, y, 140, 33)` (Widget +0x64, Button ID 2)
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_btn_play_x, gaunt_btn_play_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: def_w=140, def_h=33, x=277, y=444 (WS: 540, 624)`
- **Authentic Alignment Semantics**: Bottom-Center anchored

#### Element 45: `<Next>`
- **Canonical XML**: `<Next x="698" y="624" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_GAUNTNEXTBUTTON (GauntNextButton.jpg)` | Dimensions: `125x33` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_00435170: Widget::Resize(x, y, 125, 33)` (Widget +0x5C, Button ID 0)
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_btn_next_x, gaunt_btn_next_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: def_w=125, def_h=33, x=390, y=444 (WS: 698, 624)`
- **Authentic Alignment Semantics**: Bottom-Center anchored

#### Element 46: `<MainMenu>`
- **Canonical XML**: `<MainMenu x="164" y="593" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_GAUNTMAINMENUBUTTON (GauntMainMenuButton.jpg)` | Dimensions: `141x56` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_00435170: Widget::Resize(x, y, 141, 56)` (Widget +0x68, Button ID 3)
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_btn_mainmenu_x, gaunt_btn_mainmenu_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: def_w=141, def_h=56, x=10, y=422 (WS: 164, 593)`
- **Authentic Alignment Semantics**: Bottom-Left anchored

#### Element 47: `<Survival>`
- **Canonical XML**: `<Survival x="862" y="581" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_GAUNTSURVIVALBUTTON (gauntsurvivalbutton.jpg)` | Dimensions: `127x28` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_00435170: Widget::Resize(x, y, 127, 28)` (Widget +0x70, Button ID 6)
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_btn_survival_x, gaunt_btn_survival_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: def_w=127, def_h=28, x=506, y=413 (WS: 862, 581)`
- **Authentic Alignment Semantics**: Bottom-Right anchored

#### Element 48: `<Practice>`
- **Canonical XML**: `<Practice x="860" y="627" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_GAUNTPRACTICEBUTTON (gauntpracticebutton.jpg)` | Dimensions: `127x32` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_00435170: Widget::Resize(x, y, 127, 32)` (Widget +0x6C, Button ID 5)
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_btn_practice_x, gaunt_btn_practice_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: def_w=127, def_h=32, x=505, y=446 (WS: 860, 627)`
- **Authentic Alignment Semantics**: Bottom-Right anchored

### Section: `GauntletLabels` (Coordinate Space: `1200x675`)
Total Elements in Section: 12

#### Element 49: `<GauntletTitle>`
- **Canonical XML**: `<GauntletTitle x="150" y="0" />`
- **Classification**: Sprite / Screen Header
- **Sprite / Asset**: `IMAGE_GAUNTTITLE (gaunttitle.gif)` | Dimensions: `174x35` | Cels: `1`
- **Font Specification**: None (Stone Logo)
- **Engine Call (`Zuma.exe`)**: `FUN_0042c700: Graphics::DrawImage(gaunttitle, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_title_x, gaunt_title_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: def_w=140, def_h=30, x=150, y=0. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Top-Left banner anchor

#### Element 50: `<DoorFrame>`
- **Canonical XML**: `<DoorFrame x="389" y="309" w="450" h="338" />`
- **Classification**: Geometry Container / Stage Frame
- **Sprite / Asset**: `None` | Dimensions: `450x338` | Cels: `0`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_0042c730: Graphics::DrawImage(current_door_bg, x, y, w, h)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_door_frame_x/y/w/h`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1431: def_w=320, def_h=240, x=389, y=309, w=450, h=338.`
- **Authentic Alignment Semantics**: Box bounds in 1200x675 space

#### Element 51: `<HighscoreTitle>`
- **Canonical XML**: `<HighscoreTitle x="760" y="0" />`
- **Classification**: Sprite / Screen Header
- **Sprite / Asset**: `IMAGE_GAUNTHIGHSCORE (gaunthighscore.gif)` | Dimensions: `174x35` | Cels: `1`
- **Font Specification**: None (Stone Logo)
- **Engine Call (`Zuma.exe`)**: `FUN_0042c760: Graphics::DrawImage(gaunthighscore, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_hs_title_x, gaunt_hs_title_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1436: def_w=140, def_h=30, x=760, y=0.`
- **Authentic Alignment Semantics**: Top-Right banner anchor

#### Element 52: `<DoorName>`
- **Canonical XML**: `<DoorName x="389" y="600" />`
- **Classification**: Text (Selected Map Name)
- **Sprite / Asset**: `None` | Dimensions: `320x24` | Cels: `1`
- **Font Specification**: FONT_BROWNTITLE (NativeAlienExtended16.txt, Pt: 16, Offset: 26, Yellow RGB(255, 234, 1))
- **Engine Call (`Zuma.exe`)**: `FUN_0042c790: DrawString(door_name, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_door_name_x, gaunt_door_name_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1439: def_w=320, def_h=24, x=389, y=600.`
- **Authentic Alignment Semantics**: Center aligned above stage preview

#### Element 53: `<PracticeText>` [STATUS: VERIFIED]
- **Canonical XML**: `<PracticeText x="361" y="366" />`
- **Classification**: Text (Game Mode Indicator, Right-Aligned)
- **Sprite / Asset**: `None` | Dimensions: `320x40` | Cels: `1`
- **Font Specification**: FONT_MAIN10OUTLINE (Cancun10.txt, Pt: 10, Offset: 10, Yellow RGB(255, 234, 1))
- **Engine Call (`Zuma.exe`)**: `0x0043936C: DrawString("PRACTICE", rightX - str_w, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_practice_text_x, gaunt_practice_text_y`. Projections: $150 + 150 \times 1.40625 = 361\text{ px}$, $260 \times 1.40625 \approx 366\text{ px}$.
- **ZuPather Implementation (`layout_editor.c`)**: Rendered right-aligned against $X$, with $X=361$ as the right edge of text.
- **Authentic Alignment Semantics**: Right-aligned against anchor (rightX=361, y=366)

#### Element 54: `<CustomMapTag>`
- **Canonical XML**: `<CustomMapTag x="417" y="366" />`
- **Classification**: Text / Tag (User Map Badge)
- **Sprite / Asset**: `None` | Dimensions: `120x24` | Cels: `1`
- **Font Specification**: FONT_MAIN10 (Cancun10.txt, Pt: 10, Offset: 10, Cyan RGB(0, 229, 255))
- **Engine Call (`Zuma.exe`)**: `FUN_0042c7e0: DrawString("[CUSTOM]", x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_custom_map_x, gaunt_custom_map_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1447: def_w=120, def_h=24, x=417, y=366.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 55: `<Gem>`
- **Canonical XML**: `<Gem x="938" y="309" />`
- **Classification**: Sprite (Difficulty Gem Indicator)
- **Sprite / Asset**: `IMAGE_GAUNTEAGLEGEM / JAGUARGEM / SUNGEM` | Dimensions: `56x56` | Cels: `1`
- **Font Specification**: None (Rotating / Pulsing Gem)
- **Engine Call (`Zuma.exe`)**: `FUN_0042c810: Graphics::DrawImage(gem, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_gem_x, gaunt_gem_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1451: def_w=56, def_h=56, x=938, y=309. HOTSPOT_BOTTOM_CENTER.`
- **Authentic Alignment Semantics**: Right panel status indicator

#### Element 56: `<HighscoreScores>`
- **Canonical XML**: `<HighscoreScores x="959" y="98" />`
- **Classification**: Text (Score Column)
- **Sprite / Asset**: `None` | Dimensions: `60x80` | Cels: `1`
- **Font Specification**: FONT_MAIN8OUTLINE (Cancun8.txt with _Cancun8.gif & _CancunFAT8.gif, Pt: 8, Baseline Offset: 8, Color RGB(91, 1, 10) via 0x00439540)
- **Engine Call (`Zuma.exe`)**: `0x004394e0: DrawString(score, rightX - str_w, y=base_y + i*spacing)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_hs_scores_x, gaunt_hs_base_y`. Widescreen proof: $150 + \text{round}(575 \times 1.40625) = 959\text{ px}$.
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: bound to g_popcap_cancun_outline_8, RGB(91, 1, 10), DT_RIGHT | DT_TOP.`
- **Authentic Alignment Semantics**: Right-aligned score column (rightX=959, y=98)

#### Element 57: `<HighscoreNames>`
- **Canonical XML**: `<HighscoreNames x="776" y="98" />`
- **Classification**: Text (Player Name Column)
- **Sprite / Asset**: `None` | Dimensions: `90x80` | Cels: `1`
- **Font Specification**: FONT_MAIN8OUTLINE (Cancun8.txt with _Cancun8.gif & _CancunFAT8.gif, Pt: 8, Baseline Offset: 8, Color RGB(91, 1, 10) via 0x00439540)
- **Engine Call (`Zuma.exe`)**: `0x00439520: DrawString(name, x, y=base_y + i*spacing)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_hs_names_x, gaunt_hs_base_y`. Widescreen proof: $150 + \text{round}(445 \times 1.40625) = 776\text{ px}$.
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: bound to g_popcap_cancun_outline_8, RGB(91, 1, 10), DT_LEFT | DT_TOP.`
- **Authentic Alignment Semantics**: Left-aligned player names (x=776, y=98)

#### Element 58: `<HighscoreRow>`
- **Canonical XML**: `<HighscoreRow spacing="25" />`
- **Classification**: Layout Metric (Vertical Spacing)
- **Sprite / Asset**: `None` | Dimensions: `N/A` | Cels: `0`
- **Font Specification**: None (Metric)
- **Engine Call (`Zuma.exe`)**: `Loop counter: y_pos = base_y + row * spacing`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_hs_spacing (spacing=25)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: spacing=25.`
- **Authentic Alignment Semantics**: Metric parameter

#### Element 59: `<HighscoreClip>`
- **Canonical XML**: `<HighscoreClip w="797" />`
- **Classification**: Layout Metric (Clip Boundary)
- **Sprite / Asset**: `None` | Dimensions: `797x60` | Cels: `0`
- **Font Specification**: None (Clip Rect)
- **Engine Call (`Zuma.exe`)**: `0x0043962C: Graphics::SetClipRect(clip_x, clip_y, w=797, h)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.gaunt_hs_clip_w (w=797)`. Widescreen proof: $567 \times 1.40625 = 797\text{ px}$.
- **ZuPather Implementation (`layout_editor.c`)**: Reclassified as Region element; renders subtle clip boundary guide line/box.
- **Authentic Alignment Semantics**: Width boundary parameter

#### Element 60: `<InfoBox>` [STATUS: VERIFIED - Text Word-Wrap Container]
- **Canonical XML**: `<InfoBox x="178" y="84" w="150" h="141" lineSpacing="20" />`
- **Classification**: Text Container (Multi-line Word-Wrap Text Box)
- **Sprite / Asset**: `None` | Dimensions: `150x141` | Cels: `1`
- **Font Specification**: FONT_MAIN8OUTLINE (Cancun8.txt, Pt: 8, Gauntlet Red RGB(91, 1, 10))
- **Engine Call (`Zuma.exe`)**: `0x0043952E: Graphics::DrawStringWordWrap(str, font, rect, line_spacing)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: L2635-2649 patches gaunt_infobox_x/y/w/h and hooks hk_GauntletWordWrap with lineSpacing`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1468: def_w=150, def_h=141, x=178, y=84, lineSpacing=20.`
- **Authentic Alignment Semantics**: Multi-line word-wrapped bounding box
- **Authentic Alignment Semantics**: Box bounds (178, 84, 150, 141)

### Section: `MainMenu` (Coordinate Space: `1200x675`)
Total Elements in Section: 14

#### Element 61: `<AdventureBtn>`
- **Canonical XML**: `<AdventureBtn x="786" y="90" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_MM_ADVENTURE (mm_adventure.gif)` | Dimensions: `163x92` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_004315b0 @ 0x00431710: Widget::Resize(x=452, y=64, 163, 92)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2238: VerifyAndPatchDword(pBtn1, 452, ProjectLocalX(g_layout.mm_btn1_x), ...)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1247: def_w=163, def_h=92, x=786, y=90. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 62: `<GauntletBtn>`
- **Canonical XML**: `<GauntletBtn x="763" y="215" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_MM_GAUNTLET (mm_gauntlet.gif)` | Dimensions: `180x83` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_004315b0 @ 0x00431770: Widget::Resize(x=436, y=153, 180, 83)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2246: VerifyAndPatchDword(pBtn2, 436, ProjectLocalX(g_layout.mm_btn2_x), ...)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1251: def_w=180, def_h=83, x=763, y=215. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 63: `<OptionsBtn>`
- **Canonical XML**: `<OptionsBtn x="738" y="332" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_MM_OPTIONS (mm_options.gif)` | Dimensions: `199x86` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_004315b0 @ 0x004317d0: Widget::Resize(x=418, y=236, 199, 86)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2254: VerifyAndPatchDword(pBtn3, 418, ProjectLocalX(g_layout.mm_btn3_x), ...)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1255: def_w=199, def_h=86, x=738, y=332. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 64: `<MoreGamesBtn>`
- **Canonical XML**: `<MoreGamesBtn x="704" y="429" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_MM_MOREGAMES (mm_moregames.gif)` | Dimensions: `118x127` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_004315b0 @ 0x00431830: Widget::Resize(x=394, y=305, 118, 127)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2262: VerifyAndPatchDword(pBtn4, 394, ProjectLocalX(g_layout.mm_btn4_x), ...)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1259: def_w=118, def_h=127, x=704, y=429. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 65: `<QuitBtn>`
- **Canonical XML**: `<QuitBtn x="848" y="443" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_MM_QUIT (mm_quit.gif)` | Dimensions: `123x65` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_004315b0 @ 0x00431890: Widget::Resize(x=496, y=315, 123, 65)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2270: VerifyAndPatchDword(pBtn5, 496, ProjectLocalX(g_layout.mm_btn5_x), ...)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1263: def_w=120, def_h=141, x=848, y=443. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 66: `<WelcomeText>`
- **Canonical XML**: `<WelcomeText y="6" />`
- **Classification**: Dynamic Centered Text
- **Sprite / Asset**: `None` | Dimensions: `400x24 (Box)` | Cels: `1`
- **Font Specification**: FONT_BROWNTITLE (NativeAlienExtended16.txt, Pt: 16, Ascent: 36, Pad: 10, Offset: 26, White 0xFFFFFF)
- **Engine Call (`Zuma.exe`)**: `FUN_00431e50 @ 0x0043218F: push 0x20 (Y=32), DrawString(str, x=(1200-w)/2, y=32)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2280: MainMenuWelcomeCenter patches 640 to 1200. Y is unscaled (push 0x20)!`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1306, L4732: top = 32 - 26 = 6. <WelcomeText y="6" />. Must NEVER inject x!`
- **Authentic Alignment Semantics**: Dynamic Center ($X = (1200 - \text{StringWidth})/2$). NO X IN XML!

#### Element 67: `<UserSwitchText>`
- **Canonical XML**: `<UserSwitchText y="53" />`
- **Classification**: Dynamic Centered Text / Hyperlink
- **Sprite / Asset**: `None` | Dimensions: `300x24 (Box)` | Cels: `1`
- **Font Specification**: FONT_DIALOG (Cancun10.txt, Pt: 10, Ascent: 22, Pad: 4, Offset: 18, Link Dark Brown RGB(91, 55, 0))
- **Engine Call (`Zuma.exe`)**: `FUN_004315b0 @ 0x00431E08: push 0x1E (Y=30 widget top), Widget::Resize(x, y=30, w, h=20)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2285: MainMenuUserCenter patches 640 to 1200. Dynamic centering.`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1308: y=53 represents visual center of text box (top=41, glyphs=42..48).`
- **Authentic Alignment Semantics**: Dynamic Center ($X = (1200 - \text{StringWidth})/2$). NO X IN XML!

#### Element 68: `<FrogEyes>`
- **Canonical XML**: `<FrogEyes x="419" y="465" />`
- **Classification**: Sprite (Frog Eye Pupils)
- **Sprite / Asset**: `IMAGE_MM_EYEBALLS (mm_eyeballs.gif)` | Dimensions: `108x45` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00431e50 @ 0x004320b0: Graphics::DrawImage(eyeballs, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2220: Patches eye base coordinates with g_layout.mm_eyes_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1268: def_w=108, def_h=45, x=419, y=465. HOTSPOT_TOPLEFT.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 69: `<Hat>`
- **Canonical XML**: `<Hat x="414" y="347" />`
- **Classification**: Sprite (Frog Easter Egg Top Hat)
- **Sprite / Asset**: `IMAGE_MM_HAT (mm_hat.gif)` | Dimensions: `101x107` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00431e50 @ 0x00432247: Graphics::DrawImage(mm_hat, 188, 247)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2269: VerifyAndPatchDword(pHat, 247, ProjectLocalY(g_layout.mm_hat_y)), (x=414, y=347)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1277: BUG! Set is_conditional=true causing it to be hidden by default! Must be visible.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space (414, 347)

#### Element 70: `<Sun>`
- **Canonical XML**: `<Sun x="47" y="-89" />`
- **Classification**: Sprite (Rotating Solar Flare)
- **Sprite / Asset**: `IMAGE_SUNGLOW (mmsunglow.gif)` | Dimensions: `229x227` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00431e50 @ 0x0043202f: Graphics::DrawImageRotatedF(sunglow, sun_local_x, sun_local_y, rot)` (rotates around center NULL = $114.5, 113.5$)
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2207: pSun3DCave & pSun2DCave replace -69, -63 with sun_local_x, sun_local_y. Specifically, sun_local_x = -97 in widget-local coordinate space (calculated via ProjectLocalX(53, 1200, 675, 1200, 675) relative to the parent widget offset X=150, producing target physical screen coordinate 150 + (-97) = 53 screen space) and sun_local_y = -89.`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1292: XM_ABS mode, rendered at (x,y) with canvas clipping and center rotation crosshair at (x+114, y+113).`
- **Authentic Alignment Semantics**: Unrotated top-left screen coordinates (53, -89), rotation center on Aztec staff disk (167.5, 24.5)

#### Element 71: `<EyeRay1>`
- **Canonical XML**: `<EyeRay1 x="191" y="35" />`
- **Classification**: Interactive Secret Trigger / Ray
- **Sprite / Asset**: `IMAGE_MM_EYEMASK` | Dimensions: `16x16` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00431e50: Eye laser ray emitter 1`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.mm_eye_ray1_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1290: def_w=16, def_h=16, x=191, y=35.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 72: `<EyeRay2>`
- **Canonical XML**: `<EyeRay2 x="272" y="23" />`
- **Classification**: Interactive Secret Trigger / Ray
- **Sprite / Asset**: `IMAGE_MM_EYEMASK` | Dimensions: `16x16` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00431e50: Eye laser ray emitter 2`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.mm_eye_ray2_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1294: def_w=16, def_h=16, x=272, y=23.`
- **Authentic Alignment Semantics**: Absolute coordinates in 1200x675 space

#### Element 73: `<EyeIdle1>`
- **Canonical XML**: `<EyeIdle1 x="458" y="518" />`
- **Classification**: Animation Waypoint (Eye Center 1)
- **Sprite / Asset**: `None` | Dimensions: `32x32` | Cels: `0`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00431e50: Pupil resting position 1`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.mm_eye_idle1_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1280: def_w=32, def_h=32, x=458, y=518. HOTSPOT_CENTER_BOTH.`
- **Authentic Alignment Semantics**: Center hotspot

#### Element 74: `<EyeIdle2>`
- **Canonical XML**: `<EyeIdle2 x="538" y="499" />`
- **Classification**: Animation Waypoint (Eye Center 2)
- **Sprite / Asset**: `None` | Dimensions: `32x32` | Cels: `0`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00431e50: Pupil resting position 2`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.mm_eye_idle2_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1285: def_w=32, def_h=32, x=538, y=499. HOTSPOT_CENTER_BOTH.`
- **Authentic Alignment Semantics**: Center hotspot

### Section: `MoreGames` (Coordinate Space: `1200x675`)
Total Elements in Section: 5

#### Element 75: `<Thumb0>`
- **Canonical XML**: `<Thumb0 x="232" y="150" w="352" h="211" />`
- **Classification**: Sprite / Game Thumbnail
- **Sprite / Asset**: `IMAGE_MOREGAMES_THUMB0` | Dimensions: `352x211` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00433200: Graphics::DrawImage(thumb0, x, y, 352, 211)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.mg_t0_x/y/w/h`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1743: def_w=352, def_h=211, x=232, y=150.`
- **Authentic Alignment Semantics**: Grid Quadrant Top-Left

#### Element 76: `<Thumb1>`
- **Canonical XML**: `<Thumb1 x="614" y="150" w="352" h="211" />`
- **Classification**: Sprite / Game Thumbnail
- **Sprite / Asset**: `IMAGE_MOREGAMES_THUMB1` | Dimensions: `352x211` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00433200: Graphics::DrawImage(thumb1, x, y, 352, 211)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.mg_t1_x/y/w/h`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1743: def_w=352, def_h=211, x=614, y=150.`
- **Authentic Alignment Semantics**: Grid Quadrant Top-Right

#### Element 77: `<Thumb2>`
- **Canonical XML**: `<Thumb2 x="232" y="390" w="352" h="211" />`
- **Classification**: Sprite / Game Thumbnail
- **Sprite / Asset**: `IMAGE_MOREGAMES_THUMB2` | Dimensions: `352x211` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00433200: Graphics::DrawImage(thumb2, x, y, 352, 211)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.mg_t2_x/y/w/h`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1743: def_w=352, def_h=211, x=232, y=390.`
- **Authentic Alignment Semantics**: Grid Quadrant Bottom-Left

#### Element 78: `<Thumb3>`
- **Canonical XML**: `<Thumb3 x="614" y="390" w="352" h="211" />`
- **Classification**: Sprite / Game Thumbnail
- **Sprite / Asset**: `IMAGE_MOREGAMES_THUMB3` | Dimensions: `352x211` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00433200: Graphics::DrawImage(thumb3, x, y, 352, 211)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.mg_t3_x/y/w/h`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1743: def_w=352, def_h=211, x=614, y=390.`
- **Authentic Alignment Semantics**: Grid Quadrant Bottom-Right

#### Element 79: `<BackBtn>`
- **Canonical XML**: `<BackBtn x="381" y="626" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_MOREGAMES_BUTTON` | Dimensions: `120x36` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_00433350: Widget::Resize(x, y, 120, 36)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.mg_btn_back_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1749: def_w=120, def_h=36, x=381, y=626.`
- **Authentic Alignment Semantics**: Bottom centered

### Section: `LoadingScreen` (Coordinate Space: `1200x675`)
Total Elements in Section: 2

#### Element 80: `<LoadingBar>`
- **Canonical XML**: `<LoadingBar x="330" y="491" />`
- **Classification**: Interactive Progress Bar
- **Sprite / Asset**: `IMAGE_LOADERBAR (loadingbarsmall.gif)` | Dimensions: `260x30` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00428b70 @ 0x00428cf0: Graphics::DrawImage(loaderbar, x, y, w, h)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2190: g_layout.load_bar_x, load_bar_y patched into loader bar draw`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1228: def_w=384, def_h=44, x=330, y=491.`
- **Authentic Alignment Semantics**: Centered horizontally

#### Element 81: `<ClickToPlay>`
- **Canonical XML**: `<ClickToPlay y="605" />`
- **Classification**: Sprite / Text Banner
- **Sprite / Asset**: `IMAGE_CLICKTOPLAY (clicktoplay.gif)` | Dimensions: `260x38` | Cels: `1`
- **Font Specification**: None (Baked stone typography)
- **Engine Call (`Zuma.exe`)**: `FUN_00428b70 @ 0x00428d50: Graphics::DrawImage(clicktoplay, (space_w - w)/2, y=605)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll L2195: g_layout.load_click_play_y patched into DrawImage call`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1234: def_w=260, def_h=38, y=605, HOTSPOT_CENTER_X, align=top-center.`
- **Authentic Alignment Semantics**: Top-Center anchored (y=605)

### Section: `InGameHUD` (Coordinate Space: `1200x675`)
Total Elements in Section: 9

#### Element 82: `<LevelBanner>`
- **Canonical XML**: `<LevelBanner offsetX="40" offsetY="40" />`
- **Classification**: Sprite / Level Indicator
- **Sprite / Asset**: `IMAGE_LEVELBANNER` | Dimensions: `140x24` | Cels: `1`
- **Font Specification**: FONT_MAIN12 (Cancun12.txt)
- **Engine Call (`Zuma.exe`)**: `FUN_00445a10: DrawLevelBanner(offsetX=40, offsetY=40)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.hud_level_banner_offset_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1478: def_w=140, def_h=24, align=bottom-right.`
- **Authentic Alignment Semantics**: Offset parameter

#### Element 83: `<LivesBanner>`
- **Canonical XML**: `<LivesBanner centerX="600" centerY="337" />`
- **Classification**: Sprite / Extra Lives Indicator
- **Sprite / Asset**: `IMAGE_LIVESBANNER` | Dimensions: `260x40` | Cels: `1`
- **Font Specification**: FONT_TITLE (NativeAlienExtended18.txt)
- **Engine Call (`Zuma.exe`)**: `FUN_00445a50: DrawLivesBanner(centerX=600, centerY=337)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.hud_lives_banner_center_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1481: def_w=260, def_h=40, centerX=600, centerY=337, HOTSPOT_CENTER_BOTH.`
- **Authentic Alignment Semantics**: Screen Center (600, 337)

#### Element 84: `<Menubar>`
- **Canonical XML**: `<Menubar livesX="28" stageCX="290" scoreRX="640" zumaX="879" menuX="1100" />`
- **Classification**: Sprite / HUD Top Bar Composite
- **Sprite / Asset**: `IMAGE_MENU_BAR (menubar.gif)` | Dimensions: `1200x42` | Cels: `1`
- **Font Specification**: None (Art Bar)
- **Engine Call (`Zuma.exe`)**: `FUN_00445aa0: Graphics::DrawImage(menubar, 0, 0)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.hud_menubar_* slot coordinates`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1487: def_w=1200, def_h=42, x=0, y=0, align=top-center.`
- **Authentic Alignment Semantics**: Top edge full width

#### Element 85: `<LivesPos>`
- **Canonical XML**: `<LivesPos x="28" y="6" spacing="26" />`
- **Classification**: Sprite Array (Toad Lives Icons)
- **Sprite / Asset**: `IMAGE_FROG_LIVES (toadlives.gif)` | Dimensions: `26x24 (each)` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00445b20: for (i=0; i<lives; ++i) DrawImage(frog_life, x + i*spacing, y=6)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.hud_menubar_lives_x (x=28, y=6, spacing=26)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1509: def_w=50, def_h=23, x=28, y=6.`
- **Authentic Alignment Semantics**: Left-anchored on menubar

#### Element 86: `<StageName>` [STATUS: VERIFIED - Engine Fixed HUD Y=5, Cancun10 / FONT_DIALOG]
> *Status Note*: In `Zuma.exe`, `StageName` uses `FONT_DIALOG` (`0x00547EF8`) at baseline $Y=15$. With Cancun10 offset 10 px, the visual glyph top is $15 - 10 = 5$. The raw XML attributes `y="8"` and `font="Cancun12"` remain unchanged for roundtrip fidelity.
- **Canonical XML**: `<StageName centerX="290" y="8" font="Cancun12" color="#FFF000" />`
- **Classification**: Text (Current Level/Stage Name)
- **Sprite / Asset**: `None` | Dimensions: `110x28` | Cels: `1`
- **Font Specification**: FONT_DIALOG (`0x00547EF8` = `Cancun10.txt`, Pt: 10, Offset: 10, Yellow RGB(255, 255, 0))
- **Engine Call (`Zuma.exe`)**: `0x0040D090 (SetFont), 0x0040D0C2 (centerX=200 vanilla, patched to 290), 0x0040D0B5 (baseline y=15), 0x0040D0D3 (DrawString)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.hud_menubar_stage_cx (centerX=290, y=8)`
- **ZuPather Implementation (`layout_editor.c`)**: Bound to `g_popcap_cancun_10`, centered to `centerX=290`, rendered at visual top $Y=5$, DT_CENTER | DT_TOP. Y editing locked in Inspector (`Fixed (HUD)`), vertical dragging locked, preserving exact raw $y="8"$ on XML roundtrip.
- **Authentic Alignment Semantics**: Centered in Stage Slot (centerX=290, Engine Top Y=5)

#### Element 87: `<ScoreText>` [STATUS: VERIFIED - Y/Font in XML Not Used by Engine]
> *Status Note*: In `Zuma.exe`, `ScoreText` draws strictly with `FONT_MONODIGIT` (`0x00548054`) at baseline $Y=17$. The attributes `y="8"` and `font="Cancun12"` in XML are never used to patch any instruction in `zumareshandler.dll` or `zumatestingkit.dll`.
- **Canonical XML**: `<ScoreText rightX="640" y="8" font="Cancun12" color="#FFF000" />`
- **Classification**: Text (Numeric Score Value)
- **Sprite / Asset**: `None` | Dimensions: `110x28` | Cels: `1`
- **Font Specification**: FONT_MONODIGIT (Cancun13.txt, Pt: 13, Offset: 13, Yellow RGB(255, 255, 0) / #FFF000)
- **Engine Call (`Zuma.exe`)**: `0x0040D0E1 (SetFont FONT_MONODIGIT), 0x0040D10F (rightX=360 vanilla, 640 widescreen), 0x0040D100 (y=17), 0x0040D11D (DrawString)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.hud_menubar_score_rx (rightX=640, y=8)`
- **ZuPather Implementation (`layout_editor.c`)**: Bound to `g_popcap_cancun_13`, right-aligned to `rightX=640`, rendered at visual top $Y=4$ ($17 - 13 = 4$), DT_RIGHT | DT_TOP. Y editing locked in Inspector (`Fixed (HUD)`), vertical dragging locked, preserving exact raw $y="8"$ on XML roundtrip.
- **Authentic Alignment Semantics**: Right-Aligned in Menubar Score Slot (rightX=640, y=8)

#### Element 88: `<ProgressBar>`
- **Canonical XML**: `<ProgressBar x="879" y="2" w="178" h="24" />`
- **Classification**: Sprite / Zuma Meter
- **Sprite / Asset**: `IMAGE_ZUMA_BAR (gauge.gif) & IMAGE_ZUMA_BAR_DONE` | Dimensions: `178x24` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00445c20: DrawProgressBar(zumaX=879, y=2, w=178, h=24)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.hud_menubar_zuma_x (879)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1496: def_w=178, def_h=24, x=879, y=2.`
- **Authentic Alignment Semantics**: Menubar gauge slot

#### Element 89: `<MenuBtn>`
- **Canonical XML**: `<MenuBtn x="1100" y="4" w="67" h="27" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_MENU_BUTTON (menubutton.gif)` | Dimensions: `67x27` | Cels: `3`
- **Font Specification**: None (Baked in sprite)
- **Engine Call (`Zuma.exe`)**: `FUN_00445c80: Widget::Resize(menuX=1100, y=4, 67, 27)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.hud_menubar_menu_x (1100)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1492: def_w=67, def_h=27, x=1100, y=4.`
- **Authentic Alignment Semantics**: Menubar far-right slot

#### Element 90: `<MenubarMargins>`
- **Canonical XML**: `<MenubarMargins top="70" bottom="141" left="281" right="211" />`
- **Classification**: Gameplay Boundary Margin Metric
- **Sprite / Asset**: `None` | Dimensions: `1200x675` | Cels: `0`
- **Font Specification**: None (Safe Playfield Rect)
- **Engine Call (`Zuma.exe`)**: `FUN_00445d00: SetSafePlayfieldMargins(top=70, bottom=141, left=281, right=211)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.hud_menubar_margin_*`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1513: top=70, bottom=141, left=281, right=211. Canvas playfield guides.`
- **Authentic Alignment Semantics**: Safe play area bounds

### Section: `StageComplete` (Coordinate Space: `N/A`)
Total Elements in Section: 5

#### Element 91: `<StageText>` [STATUS: VERIFIED]
- **Canonical XML**: `<StageText x="320" y="150" align="top-center" />`
- **Classification**: Text (Stage Cleared Title)
- **Sprite / Asset**: `None` | Dimensions: `400x80` | Cels: `1`
- **Font Specification**: FONT_HUGE (NativeAlien48.txt, Pt: 48, Ascent: 80, Pad: 0, Baseline Offset: 80, Yellow RGB(255, 255, 0))
- **Engine Call (`Zuma.exe`)**: `0x00428FA3 (centerX=320/608), 0x004290D2 (baseline y=150), 0x00425B80 (char animator, computes top = y - 80)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: pStageTextCX @ 0x428FA3, pStageCharDrawY @ 0x4290D2, pStageSparkleY @ 0x429088`
- **ZuPather Implementation (`layout_editor.c`)**: Bound to `g_popcap_native_alien_48`, yellow `RGB(255, 255, 0)`, baseline offset handled via $y - 80$, DT_CENTER.
- **Authentic Alignment Semantics**: Top-Center anchored (centerX=320/608, baseline y=150)

#### Element 92: `<CompleteText>` [STATUS: VERIFIED]
- **Canonical XML**: `<CompleteText x="320" y="250" align="top-center" />`
- **Classification**: Text (Completion Banner)
- **Sprite / Asset**: `None` | Dimensions: `400x80` | Cels: `1`
- **Font Specification**: FONT_HUGE (NativeAlien48.txt, Pt: 48, Ascent: 80, Pad: 0, Baseline Offset: 80, Yellow RGB(255, 255, 0))
- **Engine Call (`Zuma.exe`)**: `0x00426DBE: patches push 250 (baseline y) and mov edx, 320 (centerX), DrawSparkleText`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: pCompleteTextCX @ 0x426DBE, patches 250 and 320`
- **ZuPather Implementation (`layout_editor.c`)**: Bound to `g_popcap_native_alien_48`, yellow `RGB(255, 255, 0)`, baseline offset handled via $y - 80$, DT_CENTER.
- **Authentic Alignment Semantics**: Top-Center anchored (centerX=320/608, baseline y=250)

#### Element 93: `<FrogWriting>`
- **Canonical XML**: `<FrogWriting y="250" align="top-center" />`
- **Classification**: Animated Cutscene Sprite
- **Sprite / Asset**: `IMAGE_FROG_WRITING` | Dimensions: `100x100` | Cels: `4`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00448200: DrawAnimatedFrog(y=250)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.stage_complete_frog_waypoint_y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: y=250, align=top-center.`
- **Authentic Alignment Semantics**: Center aligned cutscene actor

#### Element 94: `<FrogWaypoints>`
- **Canonical XML**: `<FrogWaypoints bx="150" cx="500" dx="320" y="250" align="center" />`
- **Classification**: Animation Waypoint Metric
- **Sprite / Asset**: `None` | Dimensions: `N/A` | Cels: `0`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00448250: FrogJumpBezier(bx=150, cx=500, dx=320, y=250)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.stage_complete_frog_bx/cx/dx`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: bx=150, cx=500, dx=320, y=250.`
- **Authentic Alignment Semantics**: Trajectory control points

#### Element 95: `<FrogTarget>`
- **Canonical XML**: `<FrogTarget x="320" y="200" align="center" />`
- **Classification**: Animation Landing Anchor
- **Sprite / Asset**: `None` | Dimensions: `48x48` | Cels: `0`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00448300: SetFrogFinalRest(x=320, y=200)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.stage_complete_frog_target_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1534: def_w=48, def_h=48, x=320, y=200, HOTSPOT_CENTER_BOTH.`
- **Authentic Alignment Semantics**: Center anchor (320, 200)

### Section: `TempleCleared` (Coordinate Space: `N/A`)
Total Elements in Section: 6

#### Element 96: `<ContinueBtn>`
- **Canonical XML**: `<ContinueBtn x="410" y="450" align="center" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_DIALOG_BUTTON (dialogbutton.gif)` | Dimensions: `180x48` | Cels: `3`
- **Font Specification**: FONT_TITLE (NativeAlienExtended18.txt)
- **Engine Call (`Zuma.exe`)**: `FUN_00449100: Widget::Resize(410, 450, 180, 48)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.congrats_btn_continue_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1579: def_w=180, def_h=48, x=410, y=450, align=center.`
- **Authentic Alignment Semantics**: Center aligned button

#### Element 97: `<Title>` [STATUS: VERIFIED]
- **Canonical XML**: `<Title x="320" y="120" align="center" />`
- **Classification**: Text (Congratulations Header)
- **Sprite / Asset**: `None` | Dimensions: `400x80` | Cels: `1`
- **Font Specification**: FONT_HUGE (NativeAlien48.txt, Pt: 48, Ascent: 80, Pad: 0, Baseline Offset: 80, Yellow RGB(255, 255, 0))
- **Engine Call (`Zuma.exe`)**: `0x00426F37: push 0x78 (baseline y=120), Widget::DrawCenteredText("CONGRATULATIONS!")`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: L2733 patches congrats_title_y into immediate 0x78; hk_DrawCongratsTitle centers at x=320`
- **ZuPather Implementation (`layout_editor.c`)**: Bound to `g_popcap_native_alien_48`, yellow `RGB(255, 255, 0)`, placeholder `"CONGRATULATIONS!"`, baseline offset handled via $y - 80$, DT_CENTER.
- **Authentic Alignment Semantics**: Horizontally Centered (x=320, baseline y=120, top=40)

#### Element 98: `<TikiHead>`
- **Canonical XML**: `<TikiHead x="50" y="160" align="center" />`
- **Classification**: Sprite (Sun God Talking Head)
- **Sprite / Asset**: `IMAGE_GOD_HEAD (tikihead.gif)` | Dimensions: `147x175` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00449200: Graphics::DrawImage(god_head, 50, 160)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: Patches tiki dialogue speaker coordinates`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1556: def_w=147, def_h=175, x=50, y=160, align=center.`
- **Authentic Alignment Semantics**: Left speaker anchor

#### Element 99: `<DialogText>` [STATUS: UNVERIFIED - Pending Final Architecture]
> *Status Note*: General tablet dialog routines under review; pending specific TempleCleared invocation trace.
- **Canonical XML**: `<DialogText x="220" y="180" width="380" height="150" align="center" />`
- **Classification**: Text Container (Sun God Speech)
- **Sprite / Asset**: `None` | Dimensions: `380x150` | Cels: `1`
- **Font Specification**: FONT_DIALOG (Cancun10.txt, Pt: 10, Offset: 18, Dark Brown RGB(91, 55, 0))
- **Engine Call (`Zuma.exe`)**: `FUN_00428160: DrawWordWrapped(god_speech, rc)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: Patches dialog text rect`
- **ZuPather Implementation (`layout_editor.c`)**: Preserved as-is pending final font binding.
- **Authentic Alignment Semantics**: Right speech bubble bounds

#### Element 100: `<FrogMedallion>`
- **Canonical XML**: `<FrogMedallion x="120" y="380" align="center" />`
- **Classification**: Sprite (Medallion Award)
- **Sprite / Asset**: `IMAGE_FROG_MEDALLION` | Dimensions: `108x108` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_00449300: Graphics::DrawImage(medallion, 120, 380)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: Patches medallion award anchor`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1568: def_w=108, def_h=108, x=120, y=380, align=center, HOTSPOT_CENTER_BOTH.`
- **Authentic Alignment Semantics**: Center anchor (120, 380)

#### Element 101: `<FrogReply>` [STATUS: UNVERIFIED - Pending Final Architecture]
> *Status Note*: General tablet dialog routines under review; pending specific TempleCleared invocation trace.
- **Canonical XML**: `<FrogReply x="200" y="280" align="center" />`
- **Classification**: Text Container (Frog Dialogue)
- **Sprite / Asset**: `None` | Dimensions: `350x100` | Cels: `1`
- **Font Specification**: FONT_DIALOG (Cancun10.txt, Pt: 10, Offset: 18, Dark Brown RGB(91, 55, 0))
- **Engine Call (`Zuma.exe`)**: `FUN_00428000: DrawString(frog_reply, x, y)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: Patches frog reply rect`
- **ZuPather Implementation (`layout_editor.c`)**: Preserved as-is pending final font binding.
- **Authentic Alignment Semantics**: Dialogue response box

### Section: `CreditsScreen` (Coordinate Space: `N/A`)
Total Elements in Section: 3

#### Element 102: `<WhateverBtn>`
- **Canonical XML**: `<WhateverBtn offsetX="130" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_DIALOG_BUTTON` | Dimensions: `120x40` | Cels: `3`
- **Font Specification**: FONT_TITLE (NativeAlienExtended18.txt)
- **Engine Call (`Zuma.exe`)**: `FUN_00430490: Widget::Resize(x, y, 120, 40)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.credits_btn_whatever_offset_x (130)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1589: def_w=120, def_h=40, x=510, y=430, align=bottom-right.`
- **Authentic Alignment Semantics**: Bottom-Right anchored

#### Element 103: `<CreditsText>`
- **Canonical XML**: `<CreditsText centerX="320" />`
- **Classification**: Scrolling Staff Credits Text
- **Sprite / Asset**: `None` | Dimensions: `420x240` | Cels: `1`
- **Font Specification**: FONT_MAIN10 / FONT_TITLE (Cancun10.txt / NativeAlienExtended18.txt)
- **Engine Call (`Zuma.exe`)**: `FUN_00430500: DrawScrollingCredits(centerX=320)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.credits_text_center_x (320)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1594: def_w=420, def_h=240, x=320, y=180, HOTSPOT_CENTER_X, align=top-center.`
- **Authentic Alignment Semantics**: Horizontally Centered (320)

#### Element 104: `<ScrollStart>`
- **Canonical XML**: `<ScrollStart y="480" />`
- **Classification**: Scrolling Parameter Metric
- **Sprite / Asset**: `None` | Dimensions: `320x24` | Cels: `0`
- **Font Specification**: None (Scroll Y Anchor)
- **Engine Call (`Zuma.exe`)**: `FUN_00430550: InitCreditsScroll(y=480)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.credits_scroll_start_y (480)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1600: def_w=320, def_h=24, x=320, y=480, HOTSPOT_CENTER_X.`
- **Authentic Alignment Semantics**: Initial scroll floor

### Section: `HelpScreen` (Coordinate Space: `N/A`)
Total Elements in Section: 13

#### Element 105: `<HelpBack>`
- **Canonical XML**: `<HelpBack x="11" y="51" />`
- **Classification**: Sprite (Stone Tablet Background)
- **Sprite / Asset**: `IMAGE_HELP_BACK (helpback.jpg)` | Dimensions: `618x378` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_0042eb80: Graphics::DrawImage(helpback, 11, 51)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.help_back_x/y (11, 51)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1627: def_w=618, def_h=378, x=11, y=51.`
- **Authentic Alignment Semantics**: Background tablet frame

#### Element 106: `<Skull>`
- **Canonical XML**: `<Skull x="65" y="20" />`
- **Classification**: Sprite (Danger Skull Icon)
- **Sprite / Asset**: `IMAGE_HOLE (hole.gif / skull.gif)` | Dimensions: `70x70` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_0042eb80: Graphics::DrawImage(skull, 65, 20)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.help_skull_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1636: def_w=70, def_h=70, x=65, y=20.`
- **Authentic Alignment Semantics**: Tutorial graphic 1

#### Element 107: `<Coin>`
- **Canonical XML**: `<Coin x="282" y="45" />`
- **Classification**: Sprite (Gold Coin Bonus Icon)
- **Sprite / Asset**: `IMAGE_COIN (coin.gif)` | Dimensions: `48x48` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_0042eb80: Graphics::DrawImage(coin, 282, 45)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.help_coin_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1640: def_w=48, def_h=48, x=282, y=45.`
- **Authentic Alignment Semantics**: Tutorial graphic 2

#### Element 108: `<Frog>`
- **Canonical XML**: `<Frog x="500" y="65" />`
- **Classification**: Sprite (Toad Shooter Icon)
- **Sprite / Asset**: `IMAGE_SHOOTER_TOP / smallfrogonpad.gif` | Dimensions: `108x108` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_0042eb80: Graphics::DrawImage(frog, 500, 65)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.help_frog_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1644: def_w=108, def_h=108, x=500, y=65, HOTSPOT_CENTER_BOTH.`
- **Authentic Alignment Semantics**: Tutorial graphic 3

#### Element 109: `<TopText1>`
- **Canonical XML**: `<TopText1 x="30" y="40" />`
- **Classification**: Text (Aim and Shoot Hint)
- **Sprite / Asset**: `None` | Dimensions: `260x32` | Cels: `1`
- **Font Specification**: FONT_DIALOG (Cancun10.txt, Pt: 10, Offset: 18, Dark Brown RGB(91, 55, 0))
- **Engine Call (`Zuma.exe`)**: `FUN_0042eb80: DrawString(hint1, 30, 40)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.help_w1_top_text_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1661: def_w=260, def_h=32, x=30, y=40.`
- **Authentic Alignment Semantics**: Tutorial step 1 text

#### Element 110: `<TopText2>`
- **Canonical XML**: `<TopText2 x="8" y="40" />`
- **Classification**: Text (Match Three Hint)
- **Sprite / Asset**: `None` | Dimensions: `260x32` | Cels: `1`
- **Font Specification**: FONT_DIALOG (Cancun10.txt, Pt: 10, Offset: 18, Dark Brown RGB(91, 55, 0))
- **Engine Call (`Zuma.exe`)**: `FUN_0042eb80: DrawString(hint2, 8, 40)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.help_w2_top_text_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1665: def_w=260, def_h=32, x=8, y=40.`
- **Authentic Alignment Semantics**: Tutorial step 2 text

#### Element 111: `<SwapClick>`
- **Canonical XML**: `<SwapClick x="560" y="55" />`
- **Classification**: Text (Ball Swap Hint)
- **Sprite / Asset**: `None` | Dimensions: `180x24` | Cels: `1`
- **Font Specification**: FONT_DIALOG (Cancun10.txt, Pt: 10, Offset: 18, Dark Brown RGB(91, 55, 0))
- **Engine Call (`Zuma.exe`)**: `FUN_0042eb80: DrawString(swap_hint, 560, 55)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.help_swap_click_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1657: def_w=180, def_h=24, x=560, y=55.`
- **Authentic Alignment Semantics**: Tutorial step 3 text

#### Element 112: `<SwapMouse>`
- **Canonical XML**: `<SwapMouse x="580" y="60" />`
- **Classification**: Sprite (Right Mouse Button Icon)
- **Sprite / Asset**: `IMAGE_RIGHT_MOUSE` | Dimensions: `32x40` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_0042eb80: Graphics::DrawImage(right_mouse, 580, 60)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.help_swap_mouse_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1653: def_w=32, def_h=40, x=580, y=60.`
- **Authentic Alignment Semantics**: Tutorial mouse icon

#### Element 113: `<BallTarget>`
- **Canonical XML**: `<BallTarget x="240" y="160" />`
- **Classification**: Animation Waypoint (Trajectory Demo)
- **Sprite / Asset**: `IMAGE_BLUE_BALL` | Dimensions: `32x32` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_0042eb80: Graphics::DrawImage(demo_ball, 240, 160)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.help_ball_target_x/y`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1669: def_w=32, def_h=32, x=240, y=160.`
- **Authentic Alignment Semantics**: Trajectory aim point

#### Element 114: `<DontShowCheckbox>`
- **Canonical XML**: `<DontShowCheckbox x="14" y="430" />`
- **Classification**: Interactive Checkbox Widget
- **Sprite / Asset**: `IMAGE_DIALOG_CHECKBOX` | Dimensions: `260x24` | Cels: `2`
- **Font Specification**: FONT_DIALOG (Cancun10.txt, Pt: 10, Offset: 18, Dark Brown RGB(91, 55, 0))
- **Engine Call (`Zuma.exe`)**: `FUN_0042ea14: Checkbox::Resize(14, 430, 260, 24)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: Patches startup checkbox bounds`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1677: def_w=260, def_h=24, x=14, y=430.`
- **Authentic Alignment Semantics**: Bottom-Left dialog control

#### Element 115: `<DoneBtn>`
- **Canonical XML**: `<DoneBtn x="450" y="430" w="180" h="44" />`
- **Classification**: Interactive Button Widget
- **Sprite / Asset**: `IMAGE_DIALOG_BUTTON` | Dimensions: `180x44` | Cels: `3`
- **Font Specification**: FONT_TITLE (NativeAlienExtended18.txt)
- **Engine Call (`Zuma.exe`)**: `FUN_0042ea14: Widget::Resize(450, 430, 180, 44)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: Patches help screen OK button`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1681: def_w=140, def_h=40, x=450, y=430.`
- **Authentic Alignment Semantics**: Bottom-Right dialog control

#### Element 116: `<DevTemple5>`
- **Canonical XML**: `<DevTemple5 x="225" y="114" />`
- **Classification**: Developer Preview Badge
- **Sprite / Asset**: `IMAGE_ADVTEMPLE5_PREVIEW` | Dimensions: `190x75` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_0042ea14: DevMode DrawPreview(225, 114)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: DevTester overlay injection`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1649: def_w=190, def_h=75, x=225, y=114.`
- **Authentic Alignment Semantics**: Developer test asset

#### Element 117: `<DevMouseM>`
- **Canonical XML**: `<DevMouseM x="465" y="145" />`
- **Classification**: Developer Mouse Middle Click Icon
- **Sprite / Asset**: `IMAGE_MIDDLE_MOUSE` | Dimensions: `40x29` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `FUN_0042ea14: DevMode DrawMouseM(465, 145)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: DevTester overlay injection`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1673: def_w=40, def_h=29, x=465, y=145.`
- **Authentic Alignment Semantics**: Developer test asset

### Section: `Dialogs` (Coordinate Space: `N/A`)
Total Elements in Section: 2

#### Element 118: `<Dialog>`
- **Canonical XML**: `<Dialog minTop="10" align="center" />`
- **Classification**: Universal Dialog Frame Metric
- **Sprite / Asset**: `IMAGE_DIALOG_BACK (dialogbox.jpg / DialogBox.gif)` | Dimensions: `400x320` | Cels: `1`
- **Font Specification**: FONT_TITLE (NativeAlienExtended18.txt, Pt: 18) & FONT_DIALOG (Cancun10.txt, Pt: 10)
- **Engine Call (`Zuma.exe`)**: `FUN_0042e8dc: Sexy::Dialog::Resize(x, y, w, h)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.dialog_min_top (minTop=10, align=center)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1617: def_w=400, def_h=320, minTop=10, align=center.`
- **Authentic Alignment Semantics**: Modal Dialog Center Anchor

#### Element 119: `<DevDialog>`
- **Canonical XML**: `<DevDialog width="380" minTop="10" align="center" />`
- **Classification**: Universal Dialog Frame Metric (Developer Size)
- **Sprite / Asset**: `IMAGE_DIALOG_BACK` | Dimensions: `380x320` | Cels: `1`
- **Font Specification**: FONT_TITLE & FONT_DIALOG
- **Engine Call (`Zuma.exe`)**: `FUN_0042e8dc: Sexy::Dialog::Resize(x, y, w=380, h)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.dev_dialog_min_top (width=380, minTop=10, align=center)`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1617: width=380, minTop=10, align=center.`
- **Authentic Alignment Semantics**: Modal Dialog Center Anchor

### Section: `DevHelpScreen` (Coordinate Space: `N/A`)
Total Elements in Section: 15

#### Element 120: `<HeaderTitle>`
- **Canonical XML**: `<HeaderTitle x="320" y="28" />`
- **Classification**: Text (ZTK Header Title)
- **Sprite / Asset**: `None` | Dimensions: `360x36` | Cels: `1`
- **Font Specification**: FONT_HUGE / FONT_TITLE (NativeAlien48.txt / NativeAlienExtended18.txt, Gold Stone)
- **Engine Call (`Zuma.exe`)**: `ZTK DrawHeader(x=320, y=28)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Help Screen renderer`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1690: def_w=360, def_h=36, x=320, y=28, HOTSPOT_CENTER_X.`
- **Authentic Alignment Semantics**: Horizontally Centered (320, 28)

#### Element 121: `<HeaderSubtitle>`
- **Canonical XML**: `<HeaderSubtitle x="320" y="78" />`
- **Classification**: Text (ZTK Header Subtitle)
- **Sprite / Asset**: `None` | Dimensions: `460x24` | Cels: `1`
- **Font Specification**: FONT_DIALOG (Cancun10.txt, Pt: 10, Cyan RGB(0, 229, 255))
- **Engine Call (`Zuma.exe`)**: `ZTK DrawSubtitle(x=320, y=78)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Help Screen renderer`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1695: def_w=460, def_h=24, x=320, y=78, HOTSPOT_CENTER_X.`
- **Authentic Alignment Semantics**: Horizontally Centered (320, 78)

#### Element 122: `<Columns>`
- **Canonical XML**: `<Columns col1X="162" col2X="478" />`
- **Classification**: Layout Column Metric
- **Sprite / Asset**: `None` | Dimensions: `N/A` | Cels: `0`
- **Font Specification**: None (Column Guides)
- **Engine Call (`Zuma.exe`)**: `ZTK SetColumns(col1X=162, col2X=478)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Column layout parameters`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: col1X=162, col2X=478.`
- **Authentic Alignment Semantics**: Dual-column layout guides

#### Element 123: `<AdvHeader>`
- **Canonical XML**: `<AdvHeader y="108" />`
- **Classification**: Text (Adventure Mode Section Header)
- **Sprite / Asset**: `None` | Dimensions: `200x24` | Cels: `1`
- **Font Specification**: FONT_TITLE (NativeAlienExtended18.txt, Pt: 18, Yellow RGB(255, 234, 1))
- **Engine Call (`Zuma.exe`)**: `ZTK DrawSectionHeader(col1X=162, y=108)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Header draw`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1700: def_w=200, def_h=24, x=162, y=108, HOTSPOT_CENTER_X.`
- **Authentic Alignment Semantics**: Column 1 Center Header

#### Element 124: `<Temple5Preview>`
- **Canonical XML**: `<Temple5Preview x="67" y="114" />`
- **Classification**: Sprite (Secret Temple Thumbnail)
- **Sprite / Asset**: `IMAGE_ADVTEMPLE5_PREVIEW` | Dimensions: `190x75` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `ZTK DrawImage(temple5_preview, 67, 114)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Preview draw`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1715: def_w=190, def_h=75, x=67, y=114.`
- **Authentic Alignment Semantics**: Column 1 thumbnail

#### Element 125: `<AdvBullets>`
- **Canonical XML**: `<AdvBullets x="30" y="206" spacing="18" />`
- **Classification**: Text Container (Adventure Feature List)
- **Sprite / Asset**: `None` | Dimensions: `260x70` | Cels: `1`
- **Font Specification**: FONT_MAIN10 (Cancun10.txt, Pt: 10, Offset: 10, White RGB(255, 255, 255))
- **Engine Call (`Zuma.exe`)**: `ZTK DrawBulletList(x=30, y=206, spacing=18)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Bullet list draw`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1719: def_w=260, def_h=70, x=30, y=206, spacing=18.`
- **Authentic Alignment Semantics**: Column 1 bullet box

#### Element 126: `<GauntHeader>`
- **Canonical XML**: `<GauntHeader y="108" />`
- **Classification**: Text (Gauntlet Mode Section Header)
- **Sprite / Asset**: `None` | Dimensions: `200x24` | Cels: `1`
- **Font Specification**: FONT_TITLE (NativeAlienExtended18.txt, Pt: 18, Yellow RGB(255, 234, 1))
- **Engine Call (`Zuma.exe`)**: `ZTK DrawSectionHeader(col2X=478, y=108)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Header draw`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1705: def_w=200, def_h=24, x=478, y=108, HOTSPOT_CENTER_X.`
- **Authentic Alignment Semantics**: Column 2 Center Header

#### Element 127: `<GauntBullets>`
- **Canonical XML**: `<GauntBullets x="343" y="132" spacing="21" />`
- **Classification**: Text Container (Gauntlet Feature List)
- **Sprite / Asset**: `None` | Dimensions: `260x90` | Cels: `1`
- **Font Specification**: FONT_MAIN10 (Cancun10.txt, Pt: 10, Offset: 10, White RGB(255, 255, 255))
- **Engine Call (`Zuma.exe`)**: `ZTK DrawBulletList(x=343, y=132, spacing=21)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Bullet list draw`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1723: def_w=260, def_h=90, x=343, y=132, spacing=21.`
- **Authentic Alignment Semantics**: Column 2 bullet box

#### Element 128: `<CheatHeader>`
- **Canonical XML**: `<CheatHeader y="282" />`
- **Classification**: Text (Cheat Controls Section Header)
- **Sprite / Asset**: `None` | Dimensions: `260x24` | Cels: `1`
- **Font Specification**: FONT_TITLE (NativeAlienExtended18.txt, Pt: 18, Cyan RGB(0, 229, 255))
- **Engine Call (`Zuma.exe`)**: `ZTK DrawSectionHeader(col1X=162, y=282)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Header draw`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1710: def_w=260, def_h=24, x=162, y=282, HOTSPOT_CENTER_X.`
- **Authentic Alignment Semantics**: Column 1 Lower Header

#### Element 129: `<CheatBullets>`
- **Canonical XML**: `<CheatBullets x="30" y="304" spacing="18" />`
- **Classification**: Text Container (Cheat Shortcuts List)
- **Sprite / Asset**: `None` | Dimensions: `260x70` | Cels: `1`
- **Font Specification**: FONT_MAIN10 (Cancun10.txt, Pt: 10, Offset: 10, White RGB(255, 255, 255))
- **Engine Call (`Zuma.exe`)**: `ZTK DrawBulletList(x=30, y=304, spacing=18)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Bullet list draw`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c L1727: def_w=260, def_h=70, x=30, y=304, spacing=18.`
- **Authentic Alignment Semantics**: Column 1 lower bullet box

#### Element 130: `<DemoBalls>`
- **Canonical XML**: `<DemoBalls x="478" y="295" spacing="32" />`
- **Classification**: Sprite Array (Interactive Ball Demo)
- **Sprite / Asset**: `IMAGE_BALL_* (Blue, Yellow, Red, Green, Purple)` | Dimensions: `160x32` | Cels: `5`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `ZTK DrawDemoBalls(x=478, y=295, spacing=32)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Interactive demo`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: x=478, y=295, spacing=32.`
- **Authentic Alignment Semantics**: Column 2 demo area

#### Element 131: `<DemoMouse>`
- **Canonical XML**: `<DemoMouse x="508" y="350" />`
- **Classification**: Sprite (Middle Mouse Button Demo Icon)
- **Sprite / Asset**: `IMAGE_MIDDLE_MOUSE` | Dimensions: `40x29` | Cels: `1`
- **Font Specification**: None
- **Engine Call (`Zuma.exe`)**: `ZTK DrawMouseDemo(x=508, y=350)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Interactive demo`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: x=508, y=350.`
- **Authentic Alignment Semantics**: Column 2 demo icon

#### Element 132: `<DemoClick>`
- **Canonical XML**: `<DemoClick x="458" y="362" />`
- **Classification**: Text ("CLICK!" Action Prompt)
- **Sprite / Asset**: `None` | Dimensions: `80x24` | Cels: `1`
- **Font Specification**: FONT_MAIN10 (Cancun10.txt, Pt: 10, Offset: 10, White RGB(255, 255, 255))
- **Engine Call (`Zuma.exe`)**: `ZTK DrawString("CLICK!", 458, 362)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Interactive demo`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: x=458, y=362.`
- **Authentic Alignment Semantics**: Column 2 demo text

#### Element 133: `<DemoClear>`
- **Canonical XML**: `<DemoClear x="478" y="303" />`
- **Classification**: Text ("+100 +100 +100" Score Flash)
- **Sprite / Asset**: `None` | Dimensions: `160x24` | Cels: `1`
- **Font Specification**: FONT_FLOAT (CancunFloat14.txt, Pt: 14, Offset: 15, Yellow RGB(255, 234, 1))
- **Engine Call (`Zuma.exe`)**: `ZTK DrawString("+100 +100 +100", 478, 303)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: ZTK Interactive demo`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: x=478, y=303.`
- **Authentic Alignment Semantics**: Column 2 demo score flash

#### Element 134: `<DevDialog>`
- **Canonical XML**: `<DevDialog width="380" minTop="10" />`
- **Classification**: Universal Dialog Frame Metric
- **Sprite / Asset**: `IMAGE_DIALOG_BACK` | Dimensions: `380x320` | Cels: `1`
- **Font Specification**: FONT_TITLE & FONT_DIALOG
- **Engine Call (`Zuma.exe`)**: `FUN_0042e8dc: Sexy::Dialog::Resize(x, y, w=380, minTop=10)`
- **Widescreen Hook (`zumareshandler.dll`)**: `zumareshandler.dll: g_layout.dev_dialog_min_top`
- **ZuPather Implementation (`layout_editor.c`)**: `layout_editor.c: width=380, minTop=10.`
- **Authentic Alignment Semantics**: Modal dialog size anchor

---

## 4. Actionable Architectural Blueprint for Claude (The Brain)

### 4.1 First-Class Alignment Schema (`LayoutElement`)
Instead of adding non-standard XML attributes or an arbitrary 8-value alignment enum, ZuPather preserves 100% roundtrip fidelity with existing engine attributes while handling dynamic centering cleanly in memory via `ElemXMode`:
```c
typedef enum {
    XM_ABS = 0, /* Absolute coordinates, explicit x in XML */
    XM_AUTO     /* Dynamically centered by engine, no x attribute in XML */
} ElemXMode;

/* In LayoutElement: */
uint8_t xmode;       /* ElemXMode: XM_ABS, XM_AUTO (configured per element definition) */
int16_t draw_dy;     /* Visual render offset (UserSwitchText = -12), default 0 */

/* Helper routines: */
static inline bool le_x_locked(const LayoutElement *e) {
    return (e != NULL) && (e->xmode == XM_AUTO);
}

static bool le_set_x(LayoutElement *e, int v) {
    if (!e || le_x_locked(e)) return false;
    e->x = v;
    e->flags |= ATTR_HAS_X;
    return true;
}
```
Elements designated as `XM_AUTO` (`WelcomeText`, `UserSwitchText`, `ClickToPlay`) are locked from having an explicit `x` coordinate written.

### 4.2 Inspector UI Transformation & Dragging Semantics
1. **Immutable X-Mode**: Horizontal centering mode is an intrinsic property of the element definition, not a toggleable user option. No checkboxes are added to the UI.
2. **X Field Lock**: For `XM_AUTO` elements, the Inspector disables Edit Box X (`EnableWindow(st->hwnd_edit_x, FALSE)`) and displays the read-only indicator `"Auto"`. The Y coordinate field remains fully editable.
3. **Dragging & Nudging Constraint**: All user input pathways (Inspector edit box, mouse canvas drag, keyboard arrow nudges, multi-select translation, undo/redo, paste/duplicate) mutate X strictly through `le_set_x()`. When dragging an `XM_AUTO` element on the canvas, delta X is discarded and only vertical movement ($Delta Y$) is applied.
4. **Canvas Preview Centering**: In the editor canvas, `XM_AUTO` elements calculate their preview horizontal position dynamically as $X_{\text{preview}} = \frac{1200 - \text{text\_width}}{2}$ using authentic font metrics (or fallback $\frac{1200 - \text{def\_w}}{2}$), rendered at $Y = \text{el}\to y + \text{draw\_dy}$.
5. **Serializer Protection**: Because `le_set_x()` rejects modifications on `XM_AUTO` elements, `ATTR_HAS_X` is never set. The XML serializer roundtrips cleanly without injecting rogue `x` attributes (e.g. `<WelcomeText y="6" />`).

### 4.3 Safe XML Serialization Guarantee
In `layout_xml_save`:
- Strictly preserve all 17 canonical sections in their defined order.
- Check bitflags strictly: only write attributes that exist in `el->flags`.
- Guarantee exact 1:1 roundtrip across all 134 elements.

### 4.4 Visual Correction for `Hat` and `Sun`
1. **Hat**: Remove `el->is_conditional = true;` from `<Hat>` (line 1277). Ensure `IMAGE_MM_HAT` is always rendered on the frog head in the Main Menu composite.
2. **Sun**: Treat `Sun` as standard `XM_ABS` with unrotated top-left coordinates `(53, -89)`. Render with canvas clipping so negative Y is clipped rather than culled, and display a rotation center crosshair at $(x+114, y+113) = (167, 24)$ on the Aztec staff head.
---

## 4. Master Text Element Ground-Truth Matrix

### Master Text Element Ground-Truth Verification Table

| Element Tag | Layout Section | Engine Font Resource | Font ID / Pointer | Ascent - Padding Offset | DrawString Baseline Y (Vanilla -> Widescreen) | Rendered Visual Top Y | Zuma.exe / DLL Address | Status |
| :--- | :--- | :--- | :--- | :---: | :---: | :---: | :--- | :--- |
| **`ScoreText`** | `InGameHUD` | `FONT_MONODIGIT` (`Cancun13.txt`) | `0x00548054` | 13 - 0 = **13 px** | Baseline $Y=17$ | **Top = 4 px** | `0x0040D0E1`, `0x0040D100` | **VERIFIED** (Fixed HUD Y) |
| **`StageName`** | `InGameHUD` | `FONT_DIALOG` (`Cancun10.txt`) | `0x00547EF8` | 10 - 0 = **10 px** | Baseline $Y=15$ | **Top = 5 px** | `0x0040D090`, `0x0040D0B5` | **VERIFIED** (Fixed HUD Y) |
| **`StageNum`** | `AdventureLabels` | `FONT_DIALOG` (`Cancun10.txt`) | `0x00547EF8` | 10 - 0 = **10 px** | $396 
ightarrow \mathbf{557}$ | **Top = 547 px** | `0x00436579`, `0x004365AE` | **VERIFIED** |
| **`StageText`** | `AdventureLabels` | `FONT_BROWNTITLE` (`NativeAlienExtended16.txt`) | `0x00547F50` | 36 - 10 = **26 px** | $422 
ightarrow \mathbf{593}$ | **Top = 567 px** | `0x004365BC`, `0x00436677` | **VERIFIED** |
| **`Subtitle`** | `AdventureLabels` | `FONT_MAIN10OUTLINE2` (`Cancun10.txt` Outline 2) | `0x00547FE0` | 20 - 10 = **10 px** | $456 
ightarrow \mathbf{641}$ | **Top = 631 px** | `0x004366E9`, `0x00436750` | **VERIFIED** |
| **`Stage13Num`** | `AdventureLabels` | `FONT_DIALOG` (`Cancun10.txt`) | `0x00547EF8` | 10 - 0 = **10 px** | $396 
ightarrow \mathbf{557}$ | **Top = 547 px** | `zumatestingkit.dll.c:1310` | **VERIFIED** |
| **`Stage13Title`** | `AdventureLabels` | `FONT_BROWNTITLE` (`NativeAlienExtended16.txt`) | `0x00547F50` | 36 - 10 = **26 px** | $422 
ightarrow \mathbf{593}$ | **Top = 567 px** | `zumatestingkit.dll.c:1317` | **VERIFIED** |
| **`Stage13Subtitle`**| `AdventureLabels` | `FONT_MAIN10OUTLINE2` (`Cancun10.txt` Outline 2) | `0x00547FE0` | 20 - 10 = **10 px** | $456 
ightarrow \mathbf{641}$ | **Top = 631 px** | `zumatestingkit.dll.c:1322` | **VERIFIED** |
| **`HighscoreNames`** | `AdventureLabels` | `FONT_MAIN8OUTLINE` (`Cancun8.txt` Outline) | `0x00547F44` | 14 - 6 = **8 px** | $70 
ightarrow \mathbf{98}$ | **Top = 90 px** | `0x004367AD`, `0x0043689F` | **VERIFIED** |
| **`HighscoreScores`**| `AdventureLabels` | `FONT_MAIN8OUTLINE` (`Cancun8.txt` Outline) | `0x00547F44` | 14 - 6 = **8 px** | $70 
ightarrow \mathbf{98}$ | **Top = 90 px** | `0x004367AD`, `0x0043689F` | **VERIFIED** |
| **`InfoBox`** | `AdventureLabels` | `FONT_MAIN8OUTLINE` (`Cancun8.txt` Outline) | `0x00547F44` | 14 - 6 = **8 px** | WordWrap Box $(178, 84, 150, 141)$ | **Top = 84 px** | `0x00436967` (`0x00501A14`) | **VERIFIED** |
| **`HighscoreNames`** | `GauntletLabels` | `FONT_MAIN8OUTLINE` (`Cancun8.txt` Outline) | `0x00547F44` | 14 - 6 = **8 px** | $70 
ightarrow \mathbf{98}$ | **Top = 90 px** | `0x00439540`, `0x00439568` | **VERIFIED** |
| **`HighscoreScores`**| `GauntletLabels` | `FONT_MAIN8OUTLINE` (`Cancun8.txt` Outline) | `0x00547F44` | 14 - 6 = **8 px** | $70 
ightarrow \mathbf{98}$ | **Top = 90 px** | `0x00439540`, `0x004395E2` | **VERIFIED** |
| **`PracticeText`** | `GauntletLabels` | `FONT_MAIN10OUTLINE` (`Cancun10.txt` Outline) | `0x00547F10` | 20 - 10 = **10 px** | $260 
ightarrow \mathbf{366}$ | **Top = 356 px** | `0x0043936C`, `0x0043939B` | **VERIFIED** |
| **`CustomMapTag`** | `GauntletLabels` | `FONT_MAIN10OUTLINE` (`Cancun10.txt` Outline) | `0x00547F10` | 20 - 10 = **10 px** | $260 
ightarrow \mathbf{366}$ | **Top = 356 px** | `0x0058D0EC`–`0x0058D124` | **VERIFIED** |
| **`InfoBox`** | `GauntletLabels` | `FONT_MAIN8OUTLINE` (`Cancun8.txt` Outline) | `0x00547F44` | 14 - 6 = **8 px** | WordWrap Box $(178, 84, 150, 141)$ | **Top = 84 px** | `0x0043952E` (`0x00502268`) | **VERIFIED** |
| **`DoorName`** | `GauntletDoors` / `Adv` | `FONT_MAIN10OUTLINE` (`Cancun10.txt` Outline) | `0x00547F10` | 20 - 10 = **10 px** | Centered $X=170, Y=427$ | **Top = 417 px** | `0x004392C9`, `0x0043933E` | **VERIFIED** |
| **`StageText`** | `StageComplete` | `FONT_HUGE` (`NativeAlien48.txt`) | `0x0054807C` | 80 - 0 = **80 px** | Baseline $Y=150$ | **Top = 70 px** | `0x00428FA3`, `0x004290D2` | **VERIFIED** |
| **`CompleteText`** | `StageComplete` | `FONT_HUGE` (`NativeAlien48.txt`) | `0x0054807C` | 80 - 0 = **80 px** | Baseline $Y=250$ | **Top = 170 px** | `0x00429214`, `0x00426DBE` | **VERIFIED** |
| **`Title`** | `TempleCleared` | `FONT_HUGE` (`NativeAlien48.txt`) | `0x0054807C` | 80 - 0 = **80 px** | Baseline $Y=120$ | **Top = 40 px** | `0x00426EE0`, `0x004FF2C4` | **VERIFIED** |
| **`DialogText`** | `TempleCleared` | `FONT_DIALOG` (`Cancun10.txt`) | `0x00547EF8` | 10 - 0 = **10 px** | Dialogue Bubble Loop | Rect-Bounded | `0x00428160`, `0x00426BC0` | **VERIFIED** |
| **`FrogReply`** | `TempleCleared` | `FONT_DIALOG` (`Cancun10.txt`) | `0x00547EF8` | 10 - 0 = **10 px** | Dialogue Bubble Loop | Rect-Bounded | `0x00428000`, `0x00426BC0` | **VERIFIED** |
| **`WelcomeText`** | `MainMenu` | `FONT_BROWNTITLE` (`NativeAlienExtended16.txt`) | `0x00547F50` | 36 - 10 = **26 px** | Dynamic Center (`XM_AUTO`) | $Y=215$ (top=189) | `0x0042BB70` | **VERIFIED** |
| **`UserSwitchText`** | `MainMenu` | `FONT_DIALOG` (`Cancun10.txt`) | `0x00547EF8` | 10 - 0 = **10 px** | Dynamic Center (`XM_AUTO`) | $Y=238$ (top=228) | `0x0042BB90` | **VERIFIED** |
| **`ClickToPlay`** | `MainMenu` | `FONT_FLOAT` (`CancunFloat14.txt`) | `0x005480D4` | 18 - 3 = **15 px** | Dynamic Center (`XM_AUTO`) | $Y=406$ (top=391) | `0x0042BC20` | **VERIFIED** |
| **`CreditsText`** | `CreditsScreen` | `FONT_TITLE` / `FONT_BROWNTITLE` | `0x0054807C` / `7F50` | 30 px / 26 px | Scroll Baseline ($Y=480$) | Scrolling Lines | `0x00430AC1`, `0x00549488` | **VERIFIED** |
