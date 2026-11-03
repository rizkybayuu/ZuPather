# ZuPather & ZuCompleluxe: `layout.xml` Configuration Engine Specification

## 1. Overview & Architectural Scope

In the original *Zuma Deluxe* (v1.0.0.1) binary released by PopCap Games in 2003, all user interface coordinates, button dimensions, temple doorway placements, and navigation waypoints were hardcoded as absolute immediate values embedded directly within compiled x86 machine instructions.

**ZuCompleluxe v0.6** completely decouples the entire presentation layer from the binary through `modules/zumareshandler.dll` and `modules/zumatestingkit.dll`. All interface elements across all 17 distinct game screens, dialogs, HUD panels, and DevTester features are exposed via an intuitive, human-readable configuration file: `properties/layout.xml`.

This architecture enables:
1. **Total Customization**: High-definition UI overhauls, custom menu themes, repositioned doorway portals, and modified HUD safe-zones.
2. **Secret Temple 5 ("The Void", Stage 13)**: Complete spatial and navigational control over Temple 5 and its hidden doorway arch.
3. **Widescreen & Ultrawide Support**: Seamless scaling and alignment across 4:3, 16:9, 16:10, and 21:9 displays without asset distortion.
4. **Editor Integration**: ZuPather features a dedicated **"Layout" Button** (`ID_BTN_MANAGE_LAYOUT`) positioned directly to the left of the "Map" button on the primary toolbar, launching an interactive Layout Manager dialog.

---

## 2. The Virtual 640x480 Canonical Coordinate System

All coordinates declared in `properties/layout.xml` are authored strictly in PopCap's standard **640x480 virtual canvas space**.

### 2.1 Dynamic Transformation Pipeline
At runtime, `zumareshandler.dll` parses `properties/layout.xml` and transforms virtual coordinates into target display coordinates using high-precision sub-pixel scaling:

$$\text{scale} = \min\left(\frac{W_{\text{target}}}{640}, \frac{H_{\text{target}}}{480}\right)$$

$$X_{\text{screen}} = \lfloor x_{\text{virtual}} \times \text{scale} + 0.5 \rfloor$$

$$Y_{\text{screen}} = \lfloor y_{\text{virtual}} \times \text{scale} + 0.5 \rfloor$$

### 2.2 Adaptive Resolution & Aspect-Ratio Math
ZuCompleluxe adapts dynamically to any display format:
* **4:3 Standard**: 640x480, 800x600, 1024x768, 1600x1200
* **3:2 Displays**: 1152x768, 1920x1280, 2160x1440 (Surface)
* **16:9 Widescreen**: 1200x675, 1280x720, 1920x1080, 2560x1440, 3840x2160 (4K)
* **16:10 Displays**: 1280x800, 1680x1050, 1920x1200 (Steam Deck)
* **21:9 Ultrawide**: 2560x1080, 3440x1440, 5120x2160

#### Centering and Pillarbox Offsets:
$$cw = \lfloor 640 \times \text{scale} + 0.5 \rfloor, \quad ch = \lfloor 480 \times \text{scale} + 0.5 \rfloor$$

$$xo = \frac{W_{\text{target}} - cw}{2}, \quad yo = \frac{H_{\text{target}} - ch}{2}$$

1. **Map Screens & Main Menus**:
   * Screens are centered on the display with offset $(xo, yo)$.
   * Interactive buttons, doorways, and labels use local parent offsets `PARENT_LOCAL_X(x) = x * scale` and `PARENT_LOCAL_Y(y) = y * scale`.
   * This guarantees that menus and buttons never warp, distort, or drift out of position on widescreen or ultrawide monitors.
2. **Universal Dialog Modals (Continue, Options, User Selection)**:
   * When on menus (`board == NULL`), dialogs center within the 4:3 canvas via $x = (cw - w) / 2$ and $y = (ch - h) / 2$.
   * When in-game (`board != NULL`), dialogs center across the full display $(W_{\text{target}} - w) / 2$ and $(H_{\text{target}} - h) / 2$.
   * Clamped by `minTop` from `layout.xml` to prevent collision with top headers.

---

## 3. Adventure Map Doors: `doorX, doorY` vs `frogX, frogY`

### 3.1 Resolving the Legacy `x1, y1, x2, y2` Confusion
In vanilla *Zuma Deluxe*, each door on the Adventure Map was tracked internally inside PopCap's `AdventureDoor` struct:

```cpp
struct AdventureDoor {
    int mX1; // Offset +0x00
    int mY1; // Offset +0x04
    int mX2; // Offset +0x08
    int mY2; // Offset +0x0C
};
```

When early reverse-engineering tools extracted this structure, they dumped the members as `<Door id="0" x1="226" y1="300" x2="256" y2="361" />`.

This caused widespread confusion among modders:
* **Misconception**: Modders assumed `(x1, y1)` and `(x2, y2)` formed a rectangular bounding box (e.g. top-left to bottom-right, or position + dimensions).
* **Reality**: It is **NOT** a rectangle or bounding box.

### 3.2 The True Functional Roles

```
        +-------------------------+
        |   IMAGE_DOOR_...        |
        |   (doorX, doorY)        |
        |      [===DOOR===]       |
        +-------------------------+
                     |
                     |  (Path distance)
                     v
               [GOLDEN FROG]
               (frogX, frogY)
```

1. **`doorX`, `doorY` (Door Graphic Position)**:
   * The exact pixel coordinate on the temple map where the entrance doorway sprite (`IMAGE_DOOR_...` or `IMAGE_ADVDOOR5`) is rendered.
   * Modifying these coordinates physically moves the stone doorway arch graphic.
2. **`frogX`, `frogY` (Player Frog Waypoint)**:
   * The waypoint coordinate on the ground path where the golden frog pawn stands/sits when this door is selected.
   * When moving between levels, the frog hops to `(frogX, frogY)`.
   * Modifying these coordinates adjusts where the frog sits without altering the door graphic.

### 3.3 Full Backwards Compatibility
`zumareshandler.dll` supports **both** naming conventions. Modders are strongly advised to use the new explicit attribute names:

```xml
<!-- RECOMMENDED (Intuitive & Explicit) -->
<Door id="0" doorX="226" doorY="300" frogX="256" frogY="361" />

<!-- SUPPORTED (Legacy Backward Compatibility) -->
<Door id="0" x1="226" y1="300" x2="256" y2="361" />
```

### 3.4 Adventure Map Doors Reference Table

| Door ID | Temple | Stage Numbers | `doorX` (`x1`) | `doorY` (`y1`) | `frogX` (`x2`) | `frogY` (`y2`) | Notes |
|:---:|:---|:---:|:---:|:---:|:---:|:---:|:---|
| `0` | Temple 1 (Quetzalcoatl) | Stage 1 | `226` | `300` | `256` | `361` | Starting Aztec portal |
| `1` | Temple 1 (Quetzalcoatl) | Stage 2 | `296` | `270` | `327` | `364` | Mid-tier stair portal |
| `2` | Temple 1 (Quetzalcoatl) | Stage 3 | `366` | `300` | `398` | `360` | Upper plaza portal |
| `3` | Temple 2 (Popocatepetl) | Stage 4 | `0` | `164` | `56` | `218` | Left magma portal |
| `4` | Temple 2 (Popocatepetl) | Stage 5 | `69` | `159` | `116` | `201` | Mid magma portal |
| `5` | Temple 2 (Popocatepetl) | Stage 6 | `120` | `155` | `167` | `188` | Inner caldera portal |
| `6` | Temple 3 (Centeotl) | Stage 7 | `452` | `154` | `462` | `188` | Low marsh portal |
| `7` | Temple 3 (Centeotl) | Stage 8 | `509` | `154` | `521` | `192` | Mid swamp portal |
| `8` | Temple 3 (Centeotl) | Stage 9 | `568` | `154` | `584` | `196` | High altar portal |
| `9` | Temple 4 (Sun God) | Stage 10 | `220` | `35` | `248` | `86` | Left cosmic doorway |
| `10` | Temple 4 (Sun God) | Stage 11 | `291` | `35` | `324` | `86` | Center celestial doorway |
| `11` | Temple 4 (Sun God) | Stage 12 | `369` | `35` | `408` | `85` | Right apex doorway |
| `12` | Temple 5 (The Void) | Stage 13 | `291` | `176` | `323` | `201` | Secret Temple 5 portal |

---

## 4. Complete XML Configuration Directory (All 16 Sections)

### 1. `<AdventureTemples>`
Controls the rendering positions of background temple structures on the Adventure Map.
```xml
<AdventureTemples>
    <Temple1 x="0" y="188" />
    <Temple2 x="0" y="112" />
    <Temple2V x="0" y="112" />
    <Temple3 x="395" y="125" />
    <Temple3V x="395" y="125" />
    <Temple4 x="174" y="1" />
    <Temple5 x="126" y="148" />
    <Middle x="0" y="72" />
</AdventureTemples>
```
* `Temple1` through `Temple4`: Active temple stone facades.
* `Temple2V`, `Temple3V`: Vegetation / overgrown variants drawn before unlocking subsequent temples.
* `Temple5`: Secret Temple 5 (`IMAGE_ADVTEMPLE5`, 391x100) leading to Stage 13 ("The Void").
* `Middle`: Central decorative plateau plate (`IMAGE_ADVMIDDLE`).

### 2. `<AdventureDoors>`
Defines the 13 temple doors (0..12) and their corresponding frog waypoint stations.
```xml
<AdventureDoors>
    <Door id="0" doorX="226" doorY="300" frogX="256" frogY="361" />
    <Door id="1" doorX="296" doorY="270" frogX="327" frogY="364" />
    <Door id="2" doorX="366" doorY="300" frogX="398" frogY="360" />
    <Door id="3" doorX="0" doorY="164" frogX="56" frogY="218" />
    <Door id="4" doorX="69" doorY="159" frogX="116" frogY="201" />
    <Door id="5" doorX="120" doorY="155" frogX="167" frogY="188" />
    <Door id="6" doorX="452" doorY="154" frogX="462" frogY="188" />
    <Door id="7" doorX="509" doorY="154" frogX="521" frogY="192" />
    <Door id="8" doorX="568" doorY="154" frogX="584" frogY="196" />
    <Door id="9" doorX="220" doorY="35" frogX="248" frogY="86" />
    <Door id="10" doorX="291" doorY="35" frogX="324" frogY="86" />
    <Door id="11" doorX="369" doorY="35" frogX="408" frogY="85" />
    <Door id="12" doorX="291" doorY="176" frogX="323" frogY="201" />
</AdventureDoors>
```

### 3. `<AdventureButtons>`
Defines navigation pushbuttons on the Adventure Map.
```xml
<AdventureButtons>
    <MainMenu x="0" y="438" />
    <Play x="542" y="441" />
</AdventureButtons>
```
* `MainMenu`: "MAIN MENU" stone button (bottom-left).
* `Play`: "PLAY" stone button (bottom-right).

### 4. `<AdventureLabels>`
Controls text labels, level titles, high score tables, stage info box, and the Stage 13 Banner.
```xml
<AdventureLabels>
    <HighscoreTitle x="456" />
    <BottomBox x="289" y="382" />
    <StageNum x="344" y="396" />
    <StageText y="422" centerX="326" />
    <Subtitle y="456" centerX="326" />
    <HighscoreScores x="575" />
    <HighscoreDate x="567" />
    <HighscoreNames x="470" />
    <InfoBox x="20" y="60" w="150" h="100" />
    <Stage13Badge x="289" y="382" />
    <Stage13Num x="344" y="396" />
    <Stage13Title x="326" y="422" />
    <Stage13Subtitle x="326" y="456" />
</AdventureLabels>
```

### 5. `<GauntletDoors>`
Defines the 4 practice temple door portals in Gauntlet Mode.
```xml
<GauntletDoors>
    <Door id="0" x="209" y="179" />
    <Door id="1" x="232" y="129" />
    <Door id="2" x="249" y="82" />
    <Door id="3" x="265" y="28" />
</GauntletDoors>
```

### 6. `<GauntletButtons>`
Defines navigation and difficulty selection buttons in Gauntlet Mode.
```xml
<GauntletButtons>
    <Practice x="177" y="444" />
    <Survival x="277" y="444" />
    <Back x="390" y="444" />
    <Stats x="532" y="388" />
    <HighScores x="532" y="432" />
</GauntletButtons>
```

### 7. `<GauntletLabels>`
Controls title text and rank achievement badges in Gauntlet Mode.
```xml
<GauntletLabels>
    <Title y="28" centerX="320" />
    <EagleBadge x="480" y="100" />
    <JaguarBadge x="480" y="150" />
    <SunGodBadge x="480" y="200" />
</GauntletLabels>
```

### 8. `<GauntletThumbnails>`
Controls the thumbnail grid layout for level selection in Gauntlet Mode.
```xml
<GauntletThumbnails>
    <Grid startX="75" startY="120" cols="4" rows="5" spacingX="120" spacingY="60" />
</GauntletThumbnails>
```

### 9. `<MainMenu>`
Controls the main menu pushbuttons and decorative frog eye glows.
```xml
<MainMenu>
    <AdventureButton x="382" y="124" />
    <GauntletButton x="367" y="176" />
    <OptionsButton x="350" y="228" />
    <RecordsButton x="335" y="280" />
    <QuitButton x="320" y="332" />
    <FrogEyeGlowLeft x="145" y="215" />
    <FrogEyeGlowRight x="210" y="215" />
</MainMenu>
```

### 10. `<InGameMenubar>`
Controls HUD widgets along the top stone header bar during active gameplay.
```xml
<InGameMenubar>
    <LivesWidget x="10" y="5" />
    <StageDisplay x="135" y="5" />
    <ScoreDisplay x="290" y="5" />
    <ZumaBar x="465" y="5" />
    <MenuButton x="585" y="5" />
    <LeftSafetyMargin px="0" />
    <RightSafetyMargin px="0" />
</InGameMenubar>
```

### 11. `<Dialogs>`
Specifies universal metrics, maximum boundaries, and padding for pop-up dialog modals.
```xml
<Dialogs>
    <Common minTop="30" borderPadding="15" />
    <ButtonSpacing x="15" y="10" />
</Dialogs>
```

### 12. `<OptionsDialog>`
Defines the layout of the in-game and main menu Options window.
```xml
<OptionsDialog>
    <MusicSlider x="220" y="120" w="180" />
    <SoundSlider x="220" y="160" w="180" />
    <FullscreenCheckbox x="150" y="210" />
    <CustomCursorsCheckbox x="150" y="240" />
    <OkButton x="270" y="300" />
</OptionsDialog>
```

### 13. `<StatsDialog>`
Controls text coordinate anchors in the player statistics summary screen.
```xml
<StatsDialog>
    <HeaderTitle y="40" centerX="320" />
    <StatRows startY="90" rowHeight="24" labelX="120" valueX="480" />
    <CloseButton x="270" y="380" />
</StatsDialog>
```

### 14. `<CreditsScreen>`
Controls the rolling staff credits presentation.
```xml
<CreditsScreen>
    <WhateverButton x="520" y="440" />
    <ScrollSpeed pixelsPerFrame="1.0" />
</CreditsScreen>
```

### 15. `<PracticeScreen>`
Controls the Practice / Survival temple difficulty selection buttons.
```xml
<PracticeScreen>
    <RabbitButton x="120" y="340" />
    <EagleButton x="230" y="340" />
    <JaguarButton x="340" y="340" />
    <SunGodButton x="450" y="340" />
</PracticeScreen>
```

### 16. `<Stage13Banner>`
Specifies the presentation banner plate and title styling for Secret Temple 5 ("The Void").
```xml
<Stage13Banner>
    <BackgroundPlate x="126" y="148" image="IMAGE_ADVTEMPLE5" />
    <PortalDoor x="291" y="176" image="IMAGE_ADVDOOR5" />
    <HeadlineText y="422" text="THE VOID" />
    <SubText y="456" text="THE PLACE OF NO ENDING" />
</Stage13Banner>
```

---

## 5. ZuPather UI Integration Blueprint: The "Layout" Button & Editor Modal

To provide seamless visual layout authoring for modders, ZuPather integrates a dedicated **"Layout" Button** directly into its main toolbar.

### 5.1 Toolbar Button Placement (`g_hwnd_toolbar_left`)
In `src/zupather_ui.c`:
* Currently, the "Map" button (`g_btn_manage_maps`) is instantiated on `g_hwnd_toolbar_left` with identifier `ID_BTN_MANAGE_MAPS`.
* The new **"Layout" Button** (`g_btn_manage_layout`) is positioned **immediately to the left of the "Map" button**:

```
+------------------------------------------------------------------------------------+
| TOOLBAR LEFT: [Open] [Save] ... [Curve] [Layout] [Map] ... [Mode Switcher]        |
+------------------------------------------------------------------------------------+
```

#### Layout Button Constants & Instantiation:
```c
#define ID_BTN_MANAGE_LAYOUT 1145

// In zupather_ui.c:
int layout_btn_w = 58;
int layout_btn_x = mm_x - layout_btn_w - 4; // Positioned immediately left of mm_x ("Map")

g_btn_manage_layout = CreateWindowA(
    "BUTTON", "Layout",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    layout_btn_x, 5, layout_btn_w, 26,
    g_hwnd_toolbar_left, (HMENU)ID_BTN_MANAGE_LAYOUT,
    hinst, NULL
);
```

### 5.2 Layout Manager Interactive Modal (`dialog_modals.c`)
Clicking the "Layout" button launches the **Layout Manager Modal Window**:

1. **Category Navigation Tree (Left Pane)**:
   - Lists all 16 sections (`AdventureTemples`, `AdventureDoors`, `InGameMenubar`, `MainMenu`, etc.).
   - Selecting a section populates the property grid and switches the preview backdrop.
2. **Interactive Visual Viewport (Center Canvas)**:
   - Renders the actual PopCap background (e.g., Adventure Map `map.jpg`, Main Menu `mainmenu.jpg`, or in-game backdrop).
   - **Interactive Handles**:
     - *Door Sprites*: Draggable rectangular blue frame representing `(doorX, doorY)`.
     - *Frog Waypoints*: Draggable green target circle representing `(frogX, frogY)`.
     - *Connecting Path*: Dotted yellow line connecting each door arch to its waypoint pawn.
3. **Aspect-Ratio Simulation Toolbar (Top Bar)**:
   - Switchable aspect pills: `[ 4:3 ]`, `[ 16:9 ]`, `[ 16:10 ]`, `[ 21:9 ]`.
   - Simulates screen pillarboxing and validates that elements remain within safe bounds across widescreen and ultrawide monitors.
4. **Property Inspector (Right Pane)**:
   - Direct numerical text boxes for active attributes: `doorX`, `doorY`, `frogX`, `frogY`, `x`, `y`, `w`, `h`, `centerX`.
   - Real-time two-way synchronization: dragging handles on the canvas immediately updates the property textboxes, and typing coordinates snaps the handles.
5. **File I/O Controls**:
   - `[Load layout.xml]`: Reads existing layout configurations.
   - `[Save layout.xml]`: Exports clean, indented XML adhering to the schema.
   - `[Defaults]`: Restores vanilla 2003 coordinate parameters.
