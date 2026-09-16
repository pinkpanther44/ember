#!/bin/sh
# Puts a desktop entry in your home folder so that Ember shows its own icon in
# the dock and the application list, instead of a generic one.
#
# 8.236: the application does set _NET_WM_ICON correctly, but GNOME's dock does
# not read it - it looks for a .desktop file whose StartupWMClass matches the
# window, and uses the icon from there. With no match you get the generic
# "executable" picture, which is a cog.
#
# Works two ways:
#
#   - from an unpacked release, where Ember and ember.png sit next to this file
#   - from a build tree, where the binary is under build/PersonalDAW_artefacts
#     and the icon is at Resources/Icons/ember_icon.png
#
# Nothing outside ~/.local/share is touched. To undo:
#   rm ~/.local/share/applications/ember.desktop ~/.local/share/icons/ember.png
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
APPS="$HOME/.local/share/applications"
ICONS="$HOME/.local/share/icons"

# --- find the binary ---------------------------------------------------------
BINARY=""

for candidate in \
    "$HERE/Ember" \
    "$HERE/../../build/PersonalDAW_artefacts/Release/Ember" \
    "$HERE/../../build/PersonalDAW_artefacts/Ember" \
    "$HERE/../../out-ember/PersonalDAW_artefacts/Release/Ember"
do
    if [ -x "$candidate" ]; then
        BINARY="$(cd "$(dirname "$candidate")" && pwd)/$(basename "$candidate")"
        break
    fi
done

if [ -z "$BINARY" ]; then
    echo "Could not find the Ember binary." >&2
    echo "Run this from an unpacked release, or build first:" >&2
    echo "  cmake -S . -B build -DMANTA_BRAND=ember && cmake --build build" >&2
    exit 1
fi

# --- find the icon -----------------------------------------------------------
ICON_SOURCE=""

for candidate in "$HERE/ember.png" "$HERE/../../Resources/Icons/ember_icon.png"
do
    if [ -f "$candidate" ]; then
        ICON_SOURCE="$candidate"
        break
    fi
done

if [ -z "$ICON_SOURCE" ]; then
    echo "Could not find ember.png or Resources/Icons/ember_icon.png." >&2
    exit 1
fi

# --- install -----------------------------------------------------------------
mkdir -p "$APPS" "$ICONS"
cp "$ICON_SOURCE" "$ICONS/ember.png"

cat > "$APPS/ember.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Ember
Comment=Digital audio workstation
Exec=$BINARY
Icon=$ICONS/ember.png
StartupWMClass=Ember
Categories=AudioVideo;Audio;AudioVideoEditing;
Terminal=false
DESKTOP

update-desktop-database "$APPS" 2>/dev/null || true

echo "Installed $APPS/ember.desktop"
echo "  binary: $BINARY"
echo "  icon:   $ICONS/ember.png"
echo "If Ember is running, close and start it again to see the icon."
