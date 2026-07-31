#!/usr/bin/env bash
#
# Installs a downloaded Spectroscope build.
#
# Handles the two things that make a downloaded, unsigned macOS bundle refuse to
# open: the quarantine flag Gatekeeper attaches to anything from a browser, and
# the broken signature or missing executable bit left behind if the bundle went
# through a plain zip.
#
#   ./scripts/install-macos.sh ~/Downloads/spectroscope-macos.tar.gz
#
# With no argument it looks for the tarball in ~/Downloads.

set -euo pipefail

ARCHIVE="${1:-$HOME/Downloads/spectroscope-macos.tar.gz}"

AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
APP_DIR="/Applications"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "error: no archive at $ARCHIVE" >&2
    echo "Download the spectroscope-macos artefact from the repo's Actions tab." >&2
    exit 1
fi

STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

echo "==> Extracting $(basename "$ARCHIVE")"
tar xzf "$ARCHIVE" -C "$STAGING"

mkdir -p "$AU_DIR" "$VST3_DIR"

install_bundle() {
    local source="$1" destination="$2" name
    name="$(basename "$source")"

    if [[ ! -e "$source" ]]; then
        echo "    skipping $name (not in archive)"
        return
    fi

    echo "==> Installing $name"

    # Gatekeeper marks anything a browser wrote. Without this the plugin never
    # appears in Live and the app claims to be damaged.
    xattr -dr com.apple.quarantine "$source" 2>/dev/null || true

    # A plain zip round-trip loses the executable bit; macOS reads a bundle
    # whose binary is not executable as corrupt.
    if [[ -d "$source/Contents/MacOS" ]]; then
        chmod +x "$source/Contents/MacOS/"* 2>/dev/null || true
    fi

    # Re-establish an ad-hoc signature so the loader will accept it.
    codesign --force --deep --sign - "$source" >/dev/null 2>&1 || true

    rm -rf "${destination:?}/$name"
    cp -R "$source" "$destination/"
    xattr -dr com.apple.quarantine "$destination/$name" 2>/dev/null || true

    echo "    -> $destination/$name"
}

install_bundle "$STAGING/AU/Spectroscope.component"        "$AU_DIR"
install_bundle "$STAGING/VST3/Spectroscope.vst3"           "$VST3_DIR"
install_bundle "$STAGING/Standalone/Spectroscope.app"      "$APP_DIR"

echo
echo "==> Verifying the Audio Unit with Apple's validator"

killall -9 AudioComponentRegistrar 2>/dev/null || true

if auval -v aufx Spct Cwil >/dev/null 2>&1; then
    echo "    auval passed"
else
    echo "    auval did not pass — run 'auval -v aufx Spct Cwil' to see why" >&2
fi

echo
echo "Done. In Live: Settings -> Plug-Ins -> Rescan."
