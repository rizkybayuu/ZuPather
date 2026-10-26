#!/bin/bash
# ==============================================================================
# ZuPather - macOS Native Build Script (Apple Silicon M1/M2/M3/M4 & Intel x86_64)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "======================================================="
echo "   ZuPather - Zuma Path Editor - macOS Native Builder"
echo "   Target: ./ZuPather.app (Universal / Native Bundle)"
echo "======================================================="

# 1. Detect Architecture and Homebrew paths
ARCH="$(uname -m)"
echo "[INFO] Detected CPU Architecture: $ARCH"

BREW_PREFIX=""
if [ -d "/opt/homebrew" ]; then
    BREW_PREFIX="/opt/homebrew"
elif [ -d "/usr/local" ]; then
    BREW_PREFIX="/usr/local"
elif command -v brew &> /dev/null; then
    BREW_PREFIX="$(brew --prefix)"
fi

BREW_CFLAGS=""
BREW_LDFLAGS=""
if [ -n "$BREW_PREFIX" ]; then
    echo "[INFO] Using Homebrew prefix: $BREW_PREFIX"
    if [ -d "$BREW_PREFIX/include" ]; then
        BREW_CFLAGS="-I$BREW_PREFIX/include"
    fi
    if [ -d "$BREW_PREFIX/lib" ]; then
        BREW_LDFLAGS="-L$BREW_PREFIX/lib"
    fi
fi

# 2. Check dependencies (libwebp, zstd)
HAS_WEBP=0
HAS_ZSTD=0
if brew list webp &>/dev/null || [ -f "$BREW_PREFIX/include/webp/decode.h" ]; then HAS_WEBP=1; fi
if brew list zstd &>/dev/null || [ -f "$BREW_PREFIX/include/zstd.h" ]; then HAS_ZSTD=1; fi

if [ $HAS_WEBP -eq 0 ] || [ $HAS_ZSTD -eq 0 ]; then
    echo "[WARN] Optional dependencies missing. To install: brew install webp zstd"
fi

# 3. Create .app Bundle Directory Structure
APP_DIR="$SCRIPT_DIR/ZuPather.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RESOURCES_DIR="$CONTENTS_DIR/Resources"

echo "[SETUP] Creating macOS Application Bundle layout..."
mkdir -p "$MACOS_DIR"
mkdir -p "$RESOURCES_DIR"

# 4. Generate Info.plist
cat << 'EOF' > "$CONTENTS_DIR/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>ZuPather</string>
    <key>CFBundleIdentifier</key>
    <string>com.rizkybayuu.zupather</string>
    <key>CFBundleName</key>
    <string>ZuPather</string>
    <key>CFBundleDisplayName</key>
    <string>ZuPather</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>0.2.0</string>
    <key>CFBundleVersion</key>
    <string>0.2.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
    <key>CFBundleIconFile</key>
    <string>ZuPather</string>
</dict>
</plist>
EOF

# Copy app resources if present (both structured and flattened for FindResourceA)
if [ -d "$SCRIPT_DIR/assets" ]; then
    cp -R "$SCRIPT_DIR/assets" "$RESOURCES_DIR/"
    if [ -d "$SCRIPT_DIR/assets/media" ]; then
        cp -f "$SCRIPT_DIR/assets/media/"* "$RESOURCES_DIR/" 2>/dev/null || true
    fi
    if [ -d "$SCRIPT_DIR/assets/fonts" ]; then
        cp -f "$SCRIPT_DIR/assets/fonts/"*.ttf "$RESOURCES_DIR/" 2>/dev/null || true
    fi
fi

# 5. Compile directly via Apple Clang
echo "[COMPILING] Building native binary with Apple Clang..."

clang -O2 \
    -D__APPLE__ \
    -D_stricmp=strcasecmp \
    -D_strnicmp=strncasecmp \
    -D_strdup=strdup \
    -I"$SCRIPT_DIR/src" \
    -I"$SCRIPT_DIR/src/compat" \
    $BREW_CFLAGS \
    "$SCRIPT_DIR/src/zupather_ui.c" \
    "$SCRIPT_DIR/src/zupather_state.c" \
    "$SCRIPT_DIR/src/undo_redo.c" \
    "$SCRIPT_DIR/src/level_io.c" \
    "$SCRIPT_DIR/src/dialog_modals.c" \
    "$SCRIPT_DIR/src/hud_overlay.c" \
    "$SCRIPT_DIR/src/canvas_render.c" \
    "$SCRIPT_DIR/src/image_loader.c" \
    "$SCRIPT_DIR/src/bezier_path.c" \
    "$SCRIPT_DIR/src/svg_path.c" \
    "$SCRIPT_DIR/src/level_progression.c" \
    "$SCRIPT_DIR/src/ai_psd_path.c" \
    "$SCRIPT_DIR/src/compat/win32_macos_user.m" \
    "$SCRIPT_DIR/src/compat/win32_macos_gdi.m" \
    "$SCRIPT_DIR/src/compat/win32_macos_io.m" \
    -framework Cocoa \
    -framework Carbon \
    -framework CoreGraphics \
    -framework CoreText \
    -framework ImageIO \
    -framework UniformTypeIdentifiers \
    $BREW_LDFLAGS \
    -lwebp -lwebpmux -lwebpdemux -lsharpyuv -lzstd -lm -lpthread \
    -o "$MACOS_DIR/ZuPather"

# 6. Ad-hoc codesign for immediate execution on macOS Gatekeeper
if command -v codesign &> /dev/null; then
    echo "[CODESIGN] Applying ad-hoc local signature..."
    codesign --force --deep --sign - "$APP_DIR" 2>/dev/null || true
fi

echo "======================================================="
echo "[SUCCESS] ZuPather.app successfully built!"
echo "Bundle Location: $APP_DIR"
echo "Launch with    : open \"$APP_DIR\""
echo "======================================================="

# Pause if running interactively
if [ "$1" != "--no-pause" ]; then
    echo ""
    echo "Press Enter to exit..."
    read -r
fi
