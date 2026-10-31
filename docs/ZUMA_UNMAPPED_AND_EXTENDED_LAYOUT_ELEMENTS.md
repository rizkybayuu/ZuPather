# ZuCompleluxe & ZuPather: Unmapped, Bundled, and Extended Layout Elements Specification

> **Document Status**: Active Technical Specification & Cross-Project Alignment Guide  
> **Target Audience**: AI Agents & Engineers maintaining `ZuCompleluxe` (`modules/zumareshandler.dll`, `modules/zumatestingkit.dll`) and `ZuPather` (`src/layout_editor.c`).  
> **Canonical References**:  
> - `Zuma.exe` (v1.0.0.1, PopCap Games 2003)  
> - `ZuCompleluxe/src/zumareshandler.dll.c`, `ZuCompleluxe/src/zrh_layout.c`, `ZuCompleluxe/src/zrh_layout.h`  
> - `ZuPather/src/layout_editor.c`, `ZuPather/src/layout_editor.h`  
> - `Zuma Compleluxe/properties/layout.xml`  

---

## 1. Executive Summary & Purpose

In the current architecture of **ZuCompleluxe v0.6+**, `properties/layout.xml` acts as the single source of truth for runtime UI coordinates. However, several interface components, text strings, and HUD elements in *Zuma Deluxe* are currently:
1. **Bundled into monolithic tags** (e.g. `<Menubar livesX="28" stageCX="200" scoreRX="360" zumaX="409" menuX="540" />` packs five independent UI widgets into one line without vertical coordinates, individual sizes, or font mappings).
2. **Hardcoded inside x86 memory patch routines** in `zumareshandler.dll.c` (e.g. `FloatingText` clamps, Main Menu user greeting text, High Score Y baselines and row line spacings).
3. **Hardcoded inside `Zuma.exe` engine routines** without explicit representation in `layout.xml` (e.g. Help Screen instructions text, checkbox tray, OK button, In-Game Level Banner dual-line text typography).

Because the AI working on **ZuPather** (visual interactive layout editor) and the AI working on **ZuCompleluxe** (runtime DLL injector & memory patcher) operate in different project workspaces, **this document bridges all gaps**. It details:
* Exactly what text and elements exist in each game screen.
* Their underlying vanilla coordinates, memory hooks, fonts, and colors in `Zuma.exe`.
* How they are currently handled in `zumareshandler.dll.c`.
* The **backward-compatible extended XML schema** recommended for adoption so modders and visual editors can position every single element independently.

---

## 2. Screen-by-Screen Exhaustive Audit

---

### Screen 4: In-Game HUD (`<InGameHUD>`)

Currently, `properties/layout.xml` defines:
```xml
<InGameHUD>
    <LevelBanner offsetX="40" offsetY="40" align="bottom-right" />
    <LivesBanner centerX="320" centerY="240" align="center" />
    <Menubar livesX="28" stageCX="200" scoreRX="360" zumaX="409" menuX="540" align="top-center" />
    <MenubarMargins top="50" bottom="100" left="200" right="150" />
</InGameHUD>
```

#### A. The Five Bundled Menubar Widgets

In `zumareshandler.dll.c` (lines 2443–2543), `<Menubar>` controls 5 discrete widgets rendered on top of `IMAGE_MENUBAR` (`menubar.jpg` / `menubar.gif`):

```
+------------------------------------------------------------------------------------------------------------------+
|  [LivesPos]          [StageName]                 [ScoreText]            [ProgressBar]            [MenuBtn]       |
|  (livesX, livesY)    (stageCX, stageY)           (scoreRX, scoreY)      (zumaX, zumaY)           (menuX, menuY)  |
|  toadlives x2..5     "RAB 1" / "STAGE 1-1"       "0" / "125,400"        gauge.gif 178x24         menubutton.gif  |
+------------------------------------------------------------------------------------------------------------------+
```

| Element | Tag (Extended) | Current Attribute | Vanilla Position (640x480) | Widescreen Formula (`1200x675`) | Font / Asset | Color / Alignment | PE Hook / Memory Address |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Frog Lives** | `<LivesPos>` | `livesX="28"` | `x=28, y=6` | `tx + 28 * vscale` | `images/toadlives.gif` (26x24) | Drawn in a row: `x + i * 26` | `InGameLivesStartX` (`0x4189AB`, `C7 45 EC 1C 00 00 00`) |
| **Stage Name** | `<StageName>` | `stageCX="200"` | `centerX=200, y=8` | Center of red oval (`x=290`) | `Cancun12` | `RGB(255, 240, 0)`, `DT_CENTER` | `InGameStageTextCX` (`0x418961`, `B9 C8 00 00 00 2B C8 51`) |
| **Score Text** | `<ScoreText>` | `scoreRX="360"` | `rightX=360, y=8` | Right edge of score box (`x=640`) | `Cancun12` / `Cancun13` | `RGB(255, 240, 0)`, `DT_RIGHT` | `InGameScoreTextRX` (`0x418987`, `B9 68 01 00 00 2B C8 51`) |
| **Zuma Progress Bar** | `<ProgressBar>` | `zumaX="409"` | `x=409, y=2` | Curved recess slot (`x=879`) | `images/gauge.gif` (178x24, rows=1..3) | Top-Left blit | `InGameZumaBarDrawX` (`0x418915`, `6A 02 68 99 01 00 00 33 DB`), `InGameZumaBarResizeX` (`0x41893D`) |
| **Menu Button** | `<MenuBtn>` | `menuX="540"` | `x=540, y=4` | Right stone slot (`x=1100`) | `images/menubutton.gif` (3 columns: 67x27 cell) | 3-state push button | `InGameMenuBtnX` (`0x4188EB`, `6A 03 68 1C 02 00 00`) |

> [!IMPORTANT]
> **Why this caused confusion**: In `properties/layout.xml`, modders and tools only saw the `<Menubar>` tag. There were no explicit tags named `<ProgressBar>`, `<ScoreText>`, or `<StageName>`, giving the impression they could not be edited or did not exist in the layout system.

#### B. The Level Banner Dual-Line Text Elements
In `Zuma.exe` (`0x4177F8` and `0x417850`), when entering a level or playing Gauntlet Mode, two lines of text are rendered at bottom-right:
* **Line 1 (Difficulty / Level Code)**: e.g. `"RABBIT 1"` or `"LEVEL 1-1"`.
  * **Font**: PopCap `NativeAlien48` (large golden embossed glyphs).
  * **Color**: `RGB(255, 255, 255)` with internal orange bevel (`RGB(255, 140, 20)`).
  * **Vanilla Pos**: Clamped at `X <= 600`, `Y <= 440`.
  * **Widescreen Anchor**: `X = TARGET_W - offsetX`, `Y = TARGET_H - offsetY - 40`.
* **Line 2 (Level Display Name)**: e.g. `"COMPLEX TALON"` or `"SPIRAL OF DOOM"` (derived from `dispname="..."` in `levels.xml`).
  * **Font**: PopCap `NativeAlienExtended16` or `Cancun10`.
  * **Color**: Pure white `RGB(255, 255, 255)`.
  * **Widescreen Anchor**: `X = TARGET_W - offsetX`, `Y = TARGET_H - offsetY`.
* **Current XML representation**: Only `<LevelBanner offsetX="40" offsetY="40" align="bottom-right" />`.

#### C. Floating Score & Floating Combat Text
When matching marble chains, inserting balls, or triggering combos, floating floating point scores pop up and float upwards (e.g. `+100`, `+500`, `COMBO x2`, `CHAIN BONUS x4`):
* **Font**: `Cancun10` or `Cancun12` with black drop shadow.
* **Colors**: Color-coded by combo tier (`RGB(255, 255, 0)`, `RGB(56, 189, 248)`, `RGB(255, 100, 100)`).
* **Engine Boundaries**: In `zumareshandler.dll.c` (lines 1958–1973), these are dynamically clamped to `TARGET_W - 40` and `TARGET_H - 40` via `FloatingTextClamp` (`0x4223A0`) and `FloatingTextUpdate` (`0x422410`). Not currently exposed in `layout.xml`.

#### D. Dynamic Map Gameplay Elements (Level Background, Pedestal & Skull Pit)
* **Background Map**: Loaded dynamically from `levels/<level_id>/<level_id>.jpg`.
* **Skull Pit Hole (`Hole.gif` 82x81) & `pitcover.gif`**:
  * Position is **NOT static**; it is the final endpoint coordinate `(x, y)` of the curve track parsed from `levels/<level_id>/<curve_name>.dat` or `skullrot` tangent in `levels.xml`.
* **Shooting Frog Pedestal (`SMALLFROGonPAD.gif` 108x108)**:
  * Position is parsed from `gx="..." gy="..."` inside `levels/levels.xml`.

---

### Screen 1: Main Menu (`<MainMenu>`)

Currently defined in `properties/layout.xml`:
```xml
<MainMenu>
    <AdventureBtn x="452" y="64" />
    <GauntletBtn x="436" y="153" />
    <OptionsBtn x="418" y="236" />
    <MoreGamesBtn x="394" y="305" />
    <QuitBtn x="496" y="315" />
    <FrogEyes x="191" y="331" />
    <EyeBlink x="188" y="247" />
    <Sun1 x="-63" y="-69" />
    <Sun2 x="-63" y="-69" />
    <EyeRay1 x="29" y="25" />
    <EyeRay2 x="87" y="16" />
    <EyeIdle1 x="219" y="368" />
    <EyeIdle2 x="276" y="355" />
</MainMenu>
```

#### Unmentioned & Partially Exposed Main Menu Elements:

1. **User Welcome Greeting Title (`WelcomeText`)**:
   * **Text**: `"WELCOME TO ZUMA, DEVTESTER!"` (or active player profile name).
   * **Font**: PopCap `NativeAlienExtended16`.
   * **Color**: Golden orange `RGB(255, 234, 1)`.
   * **Vanilla Position**: Centered horizontally across 640px, top Y = `-4` to `36`.
   * **Engine Patch**: Patched in `zumareshandler.dll.c` (line 1661) at `MainMenuWelcomeCenter` (`0x4296B2`).
2. **User Switch Profile Hint (`UserSwitchText`)**:
   * **Text**: `"(IF THIS IS NOT YOU, CLICK HERE.)"`.
   * **Font**: PopCap `Cancun10` (un-tinted, dark Aztec brown `RGB(91, 55, 0)`).
   * **Decoration**: Underline drawn with 1px solid line `RGB(91, 55, 0)` from `X = centerX - textWidth/2` to `X = centerX + textWidth/2` at `Y = 51`.
   * **Vanilla Position**: Centered horizontally across 640px at `Y = 38`.
   * **Engine Patch**: Patched in `zumareshandler.dll.c` (line 1658) at `MainMenuUserCenter` (`0x42967A`).
3. **Festive Santa Hat (`Hat`)**:
   * **Sprite**: `images/mmhat.gif` (seasonal Easter egg / holiday patch).
   * **Vanilla Position**: `x="215" y="170"` on the frog statue's head.
4. **Sun Flare Hotspot Clarification (`Sun1`, `Sun2`)**:
   * In `layout.xml`, `<Sun1 x="-63" y="-69" />` and `<Sun2 x="-63" y="-69" />` use **`HOTSPOT_TOPLEFT`**.
   * In custom resolution assets (`imagescustomres/mmsun.png`), the image size is `309x309` (`220 * 1.40625`).
   * When placed at top-left `(-63, -69)` in 640x480 space, the center of the sun sits at `(-63 + 110, -69 + 110) = (47, 41)`, resting precisely on the tip of the Tiki warrior's staff.

---

### Screen 2: Adventure Map (`<AdventureLabels>`, `<AdventureTemples>`, `<AdventureDoors>`)

Currently defined in `properties/layout.xml`:
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

#### Unmentioned & Hardcoded Properties:

1. **Highscore Ledger Vertical Baseline & Row Spacing**:
   * `<HighscoreNames x="470" />`, `<HighscoreScores x="575" />`, and `<HighscoreDate x="567" />` only define horizontal X coordinates.
   * **Vanilla Baseline Y**: `y = 35` (starts directly below `advhighscore.gif`).
   * **Row Spacing**: `18px` per row (for 5 ranking rows).
   * **Font**: PopCap `Cancun10` with real outline `_CancunOUTLINE10.gif`.
   * **Color**: `RGB(255, 234, 1)`.
2. **Adventure InfoBox Text Content**:
   * Currently defined as `<InfoBox x="20" y="60" w="150" h="100" />`.
   * **Rendered Text**: `"UNCOVER THE SECRET TEMPLES OF ZUMA!"`.
   * **Font**: PopCap `Cancun10`.
   * **Color**: Light peach `RGB(254, 209, 127)`.
3. **Layer Ordering of `advback`**:
   * In `Zuma.exe`, the waterfall and lower-left giant stone face (`advback.jpg` + mask `_advback.gif`) is layered **above** `Temple1` (Quetzalcoatl) but **below** `BottomBox` (`advstage.gif`) and buttons (`MainMenu`, `Play`).

---

### Screen 3: Gauntlet Screen (`<GauntletLabels>`, `<GauntletDoors>`, `<GauntletButtons>`)

Currently defined in `properties/layout.xml`:
```xml
<GauntletLabels>
    <DoorFrame x="170" y="220" />
    <HighscoreTitle x="434" />
    <DoorName x="170" y="427" />
    <PracticeText x="150" y="260" w="320" />
    <CustomMapTag x="150" y="260" />
    <Gem x="560" y="220" />
    <HighscoreScores x="575" />
    <HighscoreNames x="445" />
    <HighscoreClip w="567" />
    <InfoBox x="20" y="60" w="150" h="100" />
</GauntletLabels>
```

#### Unmentioned & Hardcoded Properties:

1. **`DoorName` Text Content & Typography**:
   * At runtime, `DoorName` (`x="170" y="427"`) renders the stage title or `"RANDOM"` inside the dark bottom recess bar of the temple doorway.
   * **Font**: `Cancun12` (or `Cancun14`).
   * **Color**: Yellow `RGB(255, 255, 0)`.
   * **Box Dimensions**: Width `320px` (`GauntDoorNameW = SDIM_VAL(320)` in `zumareshandler.dll.c`), Height `20px`.
2. **`PracticeText` / Mode Indicator**:
   * Renders `"PRACTICE"` or `"SURVIVAL"` at the top-right of the preview doorway.
   * **Font**: `Cancun10`.
   * **Color**: Rust orange `RGB(210, 110, 35)`.
3. **`InfoBox` Text Content**:
   * **Rendered Text**: `"PLAY THROUGH ALL THE LEVELS YOU HAVE UNLOCKED IN ADVENTURE MODE IN RANDOM ORDER!"`.
   * **Font**: `Cancun10`.
   * **Color**: Warm amber `RGB(255, 185, 45)`.
4. **Difficulty Gem Badge Hotspot (`Gem`)**:
   * `<Gem x="560" y="220" />` uses **`HOTSPOT_BOTTOM_CENTER`** in `Zuma.exe` (lines 1835–1849 of `zumareshandler.dll.c`), meaning drawing coordinates are calculated as `(x - width/2, y - height)` = `(498, 126)` for `gauntsungem.gif`.

---

### Screen 8: Help / Instructions Screen (`<HelpScreen>`)

Currently defined in `properties/layout.xml`:
```xml
<HelpScreen>
    <HelpBack x="11" y="51" />
    <Skull x="65" y="20" />
    <Coin x="282" y="45" />
    <Frog x="500" y="65" />
    <TopText1 x="30" y="40" />
    <TopText2 x="8" y="40" />
    <SwapClick x="560" y="55" />
    <SwapMouse x="580" y="60" />
    <BallTarget x="240" y="160" />
    <DevTemple5 x="225" y="114" />
    <DevMouseM x="465" y="145" />
</HelpScreen>
```

#### Completely Unmentioned Text & UI Elements:

1. **Instruction Texts for All 5 Compartments**:
   * **Compartment 1 (Top-Left)**:
     - Header: `"SHOOT BALLS BY CLICKING ON SCREEN WHERE YOU WANT THE BALL TO GO."` (`Cancun10`, `RGB(255, 234, 1)`).
     - Subtitle: `"THE GEM IN THE BACK OF THE FROG SHOWS THE COLOR OF THE NEXT BALL."` (`Cancun10`, `RGB(255, 185, 45)`).
   * **Compartment 2 (Top-Right)**:
     - Header: `"CLEAR BALLS BY GETTING THREE OR MORE OF THE SAME COLOR IN A ROW."` (`Cancun10`, `RGB(255, 234, 1)`).
     - Subtitle: `"LIKE COLORED BALLS ATTRACT AND CAN CREATE CHAIN REACTIONS!"` (`Cancun10`, `RGB(255, 185, 45)`).
   * **Compartment 3 (Bottom-Left: Skull Pit)**:
     - Text: `"DON'T LET THE BALLS REACH THE SKULL!"` (`Cancun10`, `RGB(255, 234, 1)`).
   * **Compartment 4 (Bottom-Center: Gold Coin)**:
     - Text: `"SHOOT COINS FOR EXTRA POINTS!"` (`Cancun10`, `RGB(255, 234, 1)`).
   * **Compartment 5 (Bottom-Right: Marble Swap)**:
     - Text: `"RIGHT MOUSE BUTTON WILL SWAP THE BALLS."` (`Cancun10`, `RGB(255, 234, 1)`).
2. **Startup Checkbox Tray (`DontShowCheckbox`)**:
   * **Checkbox**: `checkbutton.gif` (`cols=2`, 50x46 cell) at `x=14, y=430`.
   * **Label**: `"DON'T SHOW THIS SCREEN AT STARTUP."` (`Cancun10`, `RGB(255, 234, 1)`).
   * **Underline**: `checkboxsliver.gif` and `checkboxcap.gif`.
3. **Modal Dismiss Button (`DoneBtn`)**:
   * **Widget**: PopCap `Dialogbutton` (3-slice green stone button, 180x44 px) at `x=450, y=430`.
   * **Label**: `"OK"` (`Cancun14`, `RGB(255, 234, 1)`).

---

### Screen 11: DevTester Help Screen (`<DevHelpScreen>`)

Currently defined in `properties/layout.xml`:
```xml
<DevHelpScreen>
    <HeaderTitle x="320" y="28" align="top-middle" />
    <HeaderSubtitle x="320" y="78" align="top-middle" />
    <Columns col1X="162" col2X="478" />
    <AdvHeader y="108" align="top-middle" />
    <Temple5Preview x="67" y="114" align="top-left" />
    <AdvBullets x="30" y="206" spacing="18" align="top-left" />
    <GauntHeader y="108" align="top-middle" />
    <GauntBullets x="343" y="132" spacing="21" align="top-left" />
    <CheatHeader y="282" align="top-middle" />
    <CheatBullets x="30" y="304" spacing="18" align="top-left" />
    <DemoBalls x="478" y="295" spacing="32" align="center" />
    <DemoMouse x="508" y="350" align="center" />
    <DemoClick x="458" y="362" align="center" />
    <DemoClear x="478" y="303" align="center" />
    <DevDialog width="380" minTop="10" align="center" />
</DevHelpScreen>
```

#### Detailed Text Contents & Formats:
* **`HeaderTitle`**: `"DEVELOPER MODE"` rendered with Sandy Aztec Glow font at `y=28` (centered on top stone ribbon).
* **`HeaderSubtitle`**: `"SPECIAL TESTING PROFILE - UNRESTRICTED GAME ACCESS"` (`Cancun10`, `RGB(255, 234, 1)`).
* **`AdvBullets`** (4 bullet lines, spacing=18):
  1. `"* ALL 13 ADVENTURE TEMPLES UNLOCKED"`
  2. `"* ACCESS TO SECRET TEMPLE 5: THE VOID"`
  3. `"* SKIP DIRECTLY TO STAGE 13 FINAL BATTLE"`
  4. `"* UNLIMITED LIVES & CONTINUES ENABLED"`
* **`GauntBullets`** (5 bullet lines, spacing=21):
  1. `"* FULL GAUNTLET PRACTICE UNLOCKED"`
  2. `"* SUN GOD MAXIMUM RANK REACHED"`
  3. `"* ALL COURSE THUMBNAILS CACHED"`
  4. `"* SURVIVAL ENDLESS MODE READY"`
  5. `"* CUSTOM RESOLUTION COMPATIBLE"`
* **`CheatBullets`** (4 bullet lines, spacing=18):
  1. `"* CLICK MIDDLE MOUSE BUTTON IN GAME"`
  2. `"* INSTANTLY TRIGGER FULL MARBLE CLEAR"`
  3. `"* SPAWN DESTROYALL MARBLE EXPLOSIONS"`
  4. `"* AUTO COMPLETE LEVEL WITH ACETIME BONUS"`
* **`DemoClick`**: `"CLICK!"` (`Cancun10`, white `RGB(255, 255, 255)`).
* **`DemoClear`**: `"+100 +100 +100 +100 +100"` (`Cancun10`, `RGB(255, 234, 1)`).

---

### Screen 10: Loading Screen (`<LoadingScreen>`)

Currently defined in `properties/layout.xml`:
```xml
<LoadingScreen>
    <LoadingBar x="128" y="349" />
    <ClickToPlay y="430" />
</LoadingScreen>
```

#### Unmentioned & Hardcoded Properties:
1. **Loading Progress Bar Dimensions & Asset**:
   * Asset: `images/bar.gif` (dimensions: width 384px, height 30px, inner fill 268px).
   * Fill bar advances based on asset loading completion percentage (`0%` to `100%`).
2. **"LOADING..." Title / Hint Text**:
   * **Text**: `"LOADING..."`
   * **Font**: PopCap `Cancun14` or `NativeAlienExtended16`.
   * **Color**: Aztec Amber `RGB(255, 204, 0)`.
   * **Position**: Horizontally centered across 640px at `y = 315` (directly above loading bar).
3. **"CLICK HERE TO PLAY!" Button Typography**:
   * **Text**: `"CLICK HERE TO PLAY!"`
   * **Font**: PopCap `Cancun14` (with pulsing alpha oscillation when ready).
   * **Color**: `RGB(255, 255, 0)` pulsating to `RGB(255, 200, 50)`.
   * **Position**: Centered horizontally at baseline `y = 430`.

---

### Screen 6: Stage Complete Banner & Score Tally (`<StageComplete>`)

Currently defined in `properties/layout.xml`:
```xml
<StageComplete>
    <StageText x="320" y="150" align="top-center" />
    <CompleteText x="320" y="250" align="top-center" />
    <FrogWriting y="250" align="top-center" />
    <FrogWaypoints bx="150" cx="500" dx="320" y="250" align="center" />
    <FrogTarget x="320" y="200" align="center" />
</StageComplete>
```

#### Completely Unmentioned Score Tally Text Elements:
When a level is cleared, Zuma Deluxe performs an animated tally sequence before transitioning back to the Adventure Map:
1. **`StageText` Typography**:
   * **Text**: `"STAGE %d"` (e.g. `"STAGE 1-1"`).
   * **Font**: PopCap `NativeAlien48`.
   * **Color**: Pure White with golden relief `RGB(255, 240, 10)`.
   * **Center**: `(320, 150)`.
2. **`CompleteText` Typography**:
   * **Text**: `"COMPLETE!"`
   * **Font**: PopCap `NativeAlien48`.
   * **Color**: Radiant Gold `RGB(255, 215, 0)`.
   * **Center**: `(320, 250)`.
3. **Bonus Score Breakdown Tally Rows (Unmapped in XML)**:
   * **Row 1 - Time Bonus**:
     - Label: `"TIME BONUS:"` (`x=200, y=310`, font `Cancun12`, `RGB(255, 234, 1)`).
     - Value: `"%d"` (`x=440, y=310`, right-aligned, font `Cancun12`, white `RGB(255, 255, 255)`).
   * **Row 2 - Ace Time Comparison**:
     - Label: `"ACE TIME:"` (`x=200, y=335`, font `Cancun10`, `RGB(200, 200, 200)`).
     - Value: `"%02d:%02d  YOUR TIME: %02d:%02d"` (`x=440, y=335`, right-aligned, font `Cancun10`, `RGB(255, 255, 0)`).
   * **Row 3 - Balls Cleared / Remaining**:
     - Label: `"BALLS REMAINING:"` (`x=200, y=360`, font `Cancun12`, `RGB(255, 234, 1)`).
     - Value: `"%d x 100 PTS"` (`x=440, y=360`, right-aligned, font `Cancun12`, white).
   * **Row 4 - Cumulative Total**:
     - Label: `"TOTAL SCORE:"` (`x=200, y=395`, font `Cancun14`, golden amber `RGB(255, 185, 45)`).
     - Value: `"%d"` (`x=440, y=395`, right-aligned, font `Cancun14`, `RGB(255, 255, 0)`).

---

### Screen 7: Temple Cleared Modal Dialog (`<TempleCleared>`)

Currently defined in `properties/layout.xml`:
```xml
<TempleCleared>
    <ContinueBtn x="410" y="450" align="center" />
    <Title x="320" y="120" align="center" />
    <TikiHead x="50" y="160" align="center" />
    <DialogText x="220" y="180" width="380" height="150" align="center" />
    <FrogMedallion x="120" y="380" align="center" />
    <FrogReply x="200" y="280" align="center" />
</TempleCleared>
```

#### Detailed Text Content & Typography:
1. **Header Title (`Title`)**:
   * **Text**: `"CONGRATULATIONS!"`
   * **Font**: PopCap `NativeAlien48`.
   * **Color**: Rich Gold `RGB(255, 215, 0)`.
   * **Position**: Centered at `(320, 120)`.
2. **Tiki God Speech Text (`DialogText`)**:
   * **Sample Script (Temple 1)**: `"MORTAL, YOU HAVE PROVEN YOUR WORTHINESS BY CLEANSING THE FIRST TEMPLE. BUT THE DEEPER MYSTERIES OF THE JUNGLE REMAIN TO TEST YOUR SPIRIT!"`
   * **Font**: PopCap `Cancun12` with word wrapping within `width=380, height=150`.
   * **Color**: Parchment Tan `RGB(250, 235, 200)`.
3. **Frog Response Bubble (`FrogReply`)**:
   * **Text**: `"RIBBIT!"`
   * **Font**: PopCap `Cancun14` Bold.
   * **Color**: Leaf Green `RGB(100, 255, 100)`.
4. **Continue Button (`ContinueBtn`)**:
   * **Widget**: PopCap `Dialogbutton` (stone 3-slice, `180x44` px).
   * **Text**: `"CONTINUE"` (`Cancun14`, `RGB(255, 234, 1)`).

---

## 3. Proposed Backward-Compatible Extended XML Schema

To allow both **ZuCompleluxe** and **ZuPather** to access every single element with full backward compatibility, the following extended tags are formally specified:

```xml
<Layout>
    <!-- ================================================================= -->
    <!-- IN-GAME HUD & MENUBAR EXTENDED SPECIFICATION                      -->
    <!-- ================================================================= -->
    <InGameHUD>
        <!-- Banners -->
        <LevelBanner offsetX="40" offsetY="40" align="bottom-right" />
        <LivesBanner centerX="320" centerY="240" align="center" />
        
        <!-- Legacy Menubar tag preserved for full backward compatibility -->
        <Menubar livesX="28" stageCX="200" scoreRX="360" zumaX="409" menuX="540" align="top-center" />
        
        <!-- NEW: Granular independent Menubar elements -->
        <LivesPos x="28" y="6" spacing="26" />
        <StageName centerX="200" y="8" font="Cancun12" color="#FFF000" />
        <ScoreText rightX="360" y="8" font="Cancun12" color="#FFF000" />
        <ProgressBar x="409" y="2" w="178" h="24" />
        <MenuBtn x="540" y="4" w="67" h="27" />

        <!-- Viewport Playfield Margins -->
        <MenubarMargins top="50" bottom="100" left="200" right="150" />
    </InGameHUD>

    <!-- ================================================================= -->
    <!-- MAIN MENU USER PROFILE & GREETING SPECIFICATION                   -->
    <!-- ================================================================= -->
    <MainMenu>
        <AdventureBtn x="452" y="64" />
        <GauntletBtn x="436" y="153" />
        <OptionsBtn x="418" y="236" />
        <MoreGamesBtn x="394" y="305" />
        <QuitBtn x="496" y="315" />
        
        <!-- NEW: User profile greetings -->
        <WelcomeText y="-4" font="NativeAlienExtended16" color="#FFEA01" />
        <UserSwitchText y="38" font="Cancun10" color="#5B3700" underline="true" />
        
        <!-- Frog Face & Solar Rays -->
        <FrogEyes x="191" y="331" />
        <EyeBlink x="188" y="247" />
        <Sun1 x="-63" y="-69" />
        <Sun2 x="-63" y="-69" />
        <EyeRay1 x="29" y="25" />
        <EyeRay2 x="87" y="16" />
        <EyeIdle1 x="219" y="368" />
        <EyeIdle2 x="276" y="355" />
        <Hat x="215" y="170" />
    </MainMenu>

    <!-- ================================================================= -->
    <!-- INSTRUCTIONS TABLET EXTENDED SPECIFICATION                        -->
    <!-- ================================================================= -->
    <HelpScreen>
        <HelpBack x="11" y="51" />
        <Skull x="65" y="20" />
        <Coin x="282" y="45" />
        <Frog x="500" y="65" />
        <TopText1 x="30" y="40" />
        <TopText2 x="8" y="40" />
        <SwapClick x="560" y="55" />
        <SwapMouse x="580" y="60" />
        <BallTarget x="240" y="160" />
        
        <!-- NEW: Dialog Controls -->
        <DontShowCheckbox x="14" y="430" />
        <DoneBtn x="450" y="430" w="180" h="44" />
    </HelpScreen>

    <!-- ================================================================= -->
    <!-- LOADING SCREEN EXTENDED SPECIFICATION                             -->
    <!-- ================================================================= -->
    <LoadingScreen>
        <LoadingBar x="128" y="349" w="384" h="30" />
        <LoadingText y="315" font="Cancun14" color="#FFCC00" align="center" />
        <ClickToPlay y="430" font="Cancun14" color="#FFFF00" align="center" />
    </LoadingScreen>

    <!-- ================================================================= -->
    <!-- STAGE COMPLETE TALLY EXTENDED SPECIFICATION                       -->
    <!-- ================================================================= -->
    <StageComplete>
        <StageText x="320" y="150" font="NativeAlien48" color="#FFF00A" align="top-center" />
        <CompleteText x="320" y="250" font="NativeAlien48" color="#FFD700" align="top-center" />
        <TimeBonusLabel x="200" y="310" font="Cancun12" color="#FFEA01" />
        <TimeBonusValue x="440" y="310" font="Cancun12" color="#FFFFFF" align="right" />
        <AceTimeLabel x="200" y="335" font="Cancun10" color="#C8C8C8" />
        <AceTimeValue x="440" y="335" font="Cancun10" color="#FFFF00" align="right" />
        <BallsRemainingLabel x="200" y="360" font="Cancun12" color="#FFEA01" />
        <BallsRemainingValue x="440" y="360" font="Cancun12" color="#FFFFFF" align="right" />
        <TotalScoreLabel x="200" y="395" font="Cancun14" color="#FFB92D" />
        <TotalScoreValue x="440" y="395" font="Cancun14" color="#FFFF00" align="right" />
    </StageComplete>

    <!-- ================================================================= -->
    <!-- TEMPLE CLEARED MODAL DIALOG EXTENDED SPECIFICATION                -->
    <!-- ================================================================= -->
    <TempleCleared>
        <Title x="320" y="120" font="NativeAlien48" color="#FFD700" align="center" />
        <TikiHead x="50" y="160" align="center" />
        <DialogText x="220" y="180" width="380" height="150" font="Cancun12" color="#FAEBCA" align="center" />
        <FrogMedallion x="120" y="380" align="center" />
        <FrogReply x="200" y="280" font="Cancun14" color="#64FF64" align="center" />
        <ContinueBtn x="410" y="450" w="180" h="44" font="Cancun14" color="#FFEA01" align="center" />
    </TempleCleared>
</Layout>
```

---

## 4. Implementation Guidelines for ZuCompleluxe AI

When implementing support for these elements in `ZuCompleluxe`:

1. **Dual Parsing Support in `zrh_layout.c`**:
   * When reading `<InGameHUD>`, check if `<LivesPos>`, `<StageName>`, `<ScoreText>`, `<ProgressBar>`, or `<MenuBtn>` exist.
   * If they exist, their coordinates take precedence over the legacy `<Menubar livesX="..." ... />` attributes.
   * If only the legacy `<Menubar>` tag exists, assign its values to the internal config struct as before.
2. **C Data Structure Additions in `zrh_layout.h`**:
   ```c
   typedef struct {
       /* ... existing fields ... */

       /* Granular In-Game Menubar widgets */
       int hud_lives_y;
       int hud_lives_spacing;
       int hud_stage_y;
       int hud_score_y;
       int hud_zuma_y;
       int hud_menu_y;

       /* Main Menu greetings */
       int mm_welcome_y;
       int mm_userswitch_y;
       int mm_hat_x, mm_hat_y;

       /* Help screen dialog buttons */
       int help_checkbox_x, help_checkbox_y;
       int help_done_btn_x,  help_done_btn_y;
   } ZRH_LayoutConfig;
   ```
3. **Atomic Memory Patching in `zumareshandler.dll.c`**:
   * Always verify PE opcode bytes with `VerifyAndPatchDword` and `VerifyAndPatchByte`.
   * For widescreen calculation, use the dynamic formula based on `TARGET_W` and `extra_w = hud_w - 800` to prevent widgets from overflowing the stone menubar graphics.

---

## 5. Implementation Guidelines for ZuPather AI

When implementing visual dragging in `ZuPather` (`src/layout_editor.c`):

1. **Expose Individual Elements in Listbox & Inspector**:
   * Provide separate selectable entries in Screen 4 for `LivesPos`, `StageName`, `ScoreText`, `ProgressBar`, and `MenuBtn`.
   * When saving back to `layout.xml`, write **both** the combined `<Menubar ... />` line (for vanilla/legacy compatibility) and the explicit granular tags.
2. **Font Scaling Rules**:
   * PopCap bitmap fonts (`ImageFont`) must **NEVER** be scaled by `zscale` (`1.40625`).
   * Only apply `vscale` (viewport scale) to font rendering so glyphs remain 1:1 pixel-crisp.
3. **Random Map Selection on Screen 4**:
   * Whenever the Layout Editor modal dialog is opened (`show_layout_editor_dialog`), pick a random level from `levels/levels.xml`.
   * Load that level's background image, its true curve endpoint for `Hole` / `pitcover`, and its true `(gx, gy)` coordinates for `SMALLFROGonPAD`.
