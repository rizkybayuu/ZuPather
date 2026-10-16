# Zuma Deluxe - `levels.xml` Specification & Reverse Engineering Catalog

This comprehensive document specifies the schema, elements, attributes, and data types of the Zuma Deluxe level configuration file (`levels/levels.xml`).

---

## 1. Root Element: `<Levels>`
All level definitions in `levels.xml` are enclosed within a single root tag:
```xml
<?xml version="1.0"?>
<Levels>
    ...
</Levels>
```

---

## 2. Visual & Geometry Definitions: `<Graphics>`
The `<Graphics>` tag defines visual parameters, curve references, background images, and shooter (frog) coordinates for a level.

### Attributes of `<Graphics>`:
| Attribute | Data Type | Description & Example |
| :--- | :--- | :--- |
| `id` | String | Unique internal identifier for the level (e.g., `id="coaster"` or `id="claw"`). |
| `dispname` | String | Display name shown on the user interface (e.g., `dispname="Altar of Tlaloc"`). |
| `name` | String | *(Optional)* Internal alternative level name. |
| `image` | String | Background asset filename without extension (e.g., `image="coaster"`). |
| `dir` | String | *(Optional)* Subfolder path containing level assets (defaults to `levels/`). |
| `curve` | String | Primary binary curve trajectory file basename (e.g., `curve="coaster"` maps to `levels/coaster/coaster.dat`). |
| `curve1` .. `curve9` | String | Secondary through 10th curves for multi-track levels (e.g., `curve1="track2"`, `curve2="track3"`). ZuPather and Zuma engine support up to 10 tracks per level. |
| **`gx`** | Integer | **Gun X** - Horizontal coordinate of the frog shooter in screen pixels (defaults to `320`). |
| **`gy`** | Integer | **Gun Y** - Vertical coordinate of the frog shooter in screen pixels (defaults to `240`). |
| `skullrot` | Float / Int | *(Optional)* Explicit rotation angle of the skull pit in degrees (e.g., `skullrot="0"` in Mirror Serpent). When specified, it overrides and resets the curve endpoint tangent. |
| `space` | Boolean | *(Optional)* Flag activating procedural cosmic space arena mode (`space="true"`). |
| `drawcurve` | Boolean | *(Optional)* Controls whether the curve track rail line is drawn (`"true"` or `"false"`, default `"true"`). |
| `drawtunnels` | Boolean | *(Optional)* Controls whether cutout tunnel/bridge masks are drawn (`"true"` or `"false"`, default `"true"`). |
| `curvecolor` | Hex Integer | *(Optional)* Custom color for the curve rail path overlay line (e.g., `curvecolor="0x00FF00"`). |
| `bgcolor` | Hex Integer | *(Optional)* Custom background canvas fill color (e.g., `bgcolor="0x000000"`, default `0xFFFFFFFF`). |
| `destroyall` | Boolean | *(Optional)* Flag for instant ball destruction behavior on level clear. |
| `huerot` | Float / Int | *(Optional)* Hue rotation angle for real-time background color shifting. |
| `treasurefreq`| Integer | *(Optional)* Override for golden coin spawn frame interval. |
| `partime` | Integer | *(Optional)* Par time threshold in seconds for bonus coins/points (defaults to `300`). |

> [!NOTE]
> In `Zuma.exe`, `<Graphics>` also directly supports embedding gameplay difficulty parameters that normally appear in `<Settings>` (e.g., `speed`, `maxspeed`, `start`, `colors`, `balls`, `score`, `firespeed`, `reloaddelay`, `difficulty`, `powerfreq`, `accrate`, `bombfreq`, `slowfreq`, `accfreq`, `backfreq`, `repeat`, `single`, `mergespeed`, `danger`, `slowdistance`, `slowfactor`, `slowspeed`, `zumaslow`, `zumaback`). When present on `<Graphics>`, these override the progression settings.

### Child Elements inside `<Graphics>`:

#### A. `<TreasurePoint>` (Bonus Coin Spawns)
Defines spawn coordinates and trigger distance thresholds for golden coins.
- `x` (Integer): Horizontal screen position of the coin.
- `y` (Integer): Vertical screen position of the coin.
- `dist1` (Float / Int): Ball train distance progress threshold along curve 1 required for the coin to spawn (e.g., `dist1="35"`).
- `dist2` (Float / Int): *(Optional)* Progress threshold along curve 2 for multi-track levels (e.g., `dist2="40"`).

#### B. `<Cutout>` (Tunnel & Bridge Visual Overlays)
Defines overlay cutout sprites rendered **above** rolling balls to create bridges and tunnels.
- `image` (String): Cutout sprite asset name (e.g., `image="tunnel01"`, `image="serpentsM"`).
- `pri` (Integer): Rendering layer priority (`0` to `4`, `pri="1"` = rendered above rolling balls).
- `x` (Integer): X coordinate of cutout placement.
- `y` (Integer): Y coordinate of cutout placement.

#### C. `<BackgroundAlpha>` (Dynamic Ambient Animation Layers)
Defines looping ambient motion layers (water streams, earthquakes, volcanic smoke).
- `image` (String): Texture asset name (e.g., `image="riverbed01"`, `image="quake02"`).
- `x`, `y` (Integer): Initial position on screen.
- `vx` (Float / Int): Horizontal scrolling velocity (e.g., `vx="-1"` or `vx="1"`).
- `vy` (Float / Int): Vertical scrolling velocity (e.g., `vy="1"` or `vy="2"`).

#### D. `<Sprite>` (Static / Decorative Level Overlays)
Defines decorative sprite overlays placed at specific coordinates.
- `image` (String): Sprite image asset name.
- `pri` (Integer): Priority / layer index.
- `x` (Integer): Horizontal placement coordinate.
- `y` (Integer): Vertical placement coordinate.

---

## 2.1 The Space Level Animation Engine (`space="true"`)
When `space="true"` is set on `<Graphics>` (used in Stage 13 / Level 13-1 `space`):
1. **Background Image Bypass**: The standard level background image (`levels/space/space.jpg`) is completely ignored and not required.
2. **Nebula Texture (`images/nebula1`)**:
   - The engine loads the shared cosmic cloud texture `images/nebula1` from the main `images/` directory.
   - It scrolls vertically downwards at **0.25 pixels per frame** (`(update_count >> 2) % nebula_height`).
   - It performs 3 tiled vertical blits (above, center, below) so the looping transition is seamless.
   - If the nebula texture fails to load, it falls back to a solid black clear (`0x000000`).
3. **Procedural Starfield (1,000 Particles)**:
   - Zuma maintains an active pool of **1,000 star particles** across a 640x480 resolution.
   - Each star is assigned to one of 3 parallax depth layers upon spawn (`rand() % 3`):
     - **Layer 0 (Far / Dim / Slow)**: Velocity = `0.20 px/frame` (`0x3e4ccccd`), Color = Dark Gray `0x404040` (RGB: 64, 64, 64).
     - **Layer 1 (Mid / Medium)**: Velocity = `0.60 px/frame` (`0x3f19999a`), Color = Silver Gray `0x909090` (RGB: 144, 144, 144).
     - **Layer 2 (Near / Bright / Fast)**: Velocity = `0.90 px/frame` (`0x3f666666`), Color = Pure White `0xFFFFFF` (RGB: 255, 255, 255).
   - Rendered as 1x1 pixel rectangles (`FillRect((int)star.x, (int)star.y, 1, 1)`).
   - When a star reaches `y >= 480.0f`, it is deleted and re-spawned at `y = 0` with a new randomized `x` (`rand() % 640`) and random depth layer.
4. **Interactive Secret**:
   - On the level summary / victory screen, the game displays the hint: `"Press the 's' key to toggle star scrolling."`
   - In-game, pressing `'s'` toggles star scrolling motion on and off.

---

## 3. Gameplay Mechanics & Physics Profiles: `<Settings>`
The `<Settings>` tag defines ball train movement physics, difficulty scaling, and scoring thresholds.

| Parameter | Data Type | Description | Typical Values |
| :--- | :--- | :--- | :--- |
| `id` | String | Profile identifier (e.g., `level1`, `lvl11`, `lvl35`). | `"level1"`, `"lvl35"` |
| `speed` | Float | Base movement velocity of the ball train (pixels/tick). | `0.45` to `1.50` |
| `start` | Integer | Initial number of balls spawned when train enters. | `20` to `80` |
| `score` | Integer | Score threshold required to trigger "ZUMA" state and stop ball feeding. | `1000` to `10000` |
| `repeat` | Integer | Probability weight for consecutive identical ball colors. | `40` to `50` |
| `colors` | Integer | Number of active distinct ball colors. | `4`, `5`, or `6` |
| `reloaddelay`| Integer | Delay in ticks before frog reloads next ball. | `0` |
| `mergespeed` | Float | Magnetic attraction acceleration when two matching gaps close. | `0.05` |
| `firespeed` | Float | Velocity of projectiles launched by frog. | `6` |
| `partime` | Integer | Par time threshold in seconds for bonus coins/points. | `25` to `240` |
| `slowfactor` | Float | Speed divisor when Slow Ball power-up is active (`speed / slowfactor`). | `1.0` to `4.2` |
| `powerfreq` | Integer | Frame interval for spawning power-up balls (bomb, slow, accuracy, reverse). | `1000` to `2900` |
| `single` | Integer | Maximum consecutive sequence limit of a single color. | `7`, `8`, `9` |
| `zumaslow` | Integer | Deceleration duration in ticks upon reaching ZUMA state. | `500` |
| `zumaback` | Integer | Reverse displacement in pixels when ZUMA state is reached. | `200` |

---

## 4. Game Progression Schemas: `<Level>`, `<LevelProgression>`, `<StageProgression>`

### A. `<LevelProgression>`
Maps settings profiles to difficulty levels:
```xml
<LevelProgression 
    id="standard" 
    settings="level1,level2,level3,..."
    difficulty="1,2,3,..." />
```

### B. `<Level>`
Maps graphic entries to progression profiles:
```xml
<Level graphics="spiral" progression="standard" />
```

### C. `<StageProgression>`
Defines level sequences across Adventure Mode temple stages 1 through 13:
```xml
<StageProgression
    stage1 = "spiral,claw,riverbed,targetglyph,blackswirley"
    diffi1 = "lvl11,lvl12,lvl13,lvl14,lvl15"
    ...
    stage13 = "space"
    diffi13 = "lvl131" />
```
