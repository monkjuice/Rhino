#!/bin/bash
# Packs a macOS build of Rhino Forge into one installer: the app into
# /Applications, the VST3 and the AU into /Library/Audio/Plug-Ins.
#
#   build-pkg.sh <artefacts folder> <output .pkg>
#
# The artefacts folder is the one holding Standalone/, VST3/ and AU/, such as
# build/RhinoForge_artefacts/Release. .github/workflows/forge-macos.yml runs
# this; on a Mac it runs the same way after a build.
#
# Each bundle carries the factory presets in Contents/Resources/Factory, which
# is where presetBank::factorySource looks when no source tree is above it, and
# is then signed ad hoc: Apple Silicon runs nothing unsigned, and adding the
# presets breaks the signature the linker made. Ad hoc is not a Developer ID,
# so macOS asks whoever opens the package to allow it once, under System
# Settings > Privacy & Security.
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <artefacts folder> <output .pkg>" >&2
    exit 2
fi
artefacts=$1
output=$2
here=$(cd "$(dirname "$0")" && pwd)
forge=$(cd "$here/../.." && pwd)
version=$(sed -n 's/^project(RhinoForge VERSION \([0-9.]*\).*/\1/p' "$forge/CMakeLists.txt")
if [[ -z $version ]]; then
    echo "no project version in $forge/CMakeLists.txt" >&2
    exit 1
fi

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

# Copies a bundle to where the package installs it, puts the factory set in it
# and signs it.
place() {
    local bundle=$1 into=$stage/root$2
    if [[ ! -d $bundle ]]; then
        echo "missing $bundle: build the Standalone, VST3 and AU targets first" >&2
        exit 1
    fi
    local placed
    placed="$into/$(basename "$bundle")"
    mkdir -p "$into"
    ditto "$bundle" "$placed"
    mkdir -p "$placed/Contents/Resources"
    ditto "$forge/presets/Factory" "$placed/Contents/Resources/Factory"
    codesign --force --sign - "$placed"
    codesign --verify --strict "$placed"
    echo "$(basename "$bundle"): $(lipo -archs "$placed/Contents/MacOS/"*)"
}
place "$artefacts/Standalone/Rhino Forge.app" /Applications
place "$artefacts/VST3/Rhino Forge.vst3" /Library/Audio/Plug-Ins/VST3
place "$artefacts/AU/Rhino Forge.component" /Library/Audio/Plug-Ins/Components

# Installer relocates a bundle: finding one with the same identifier elsewhere
# on the disk, it updates that copy instead of installing to the path given.
# The app and both plugins carry one identifier, so relocation is turned off
# and every bundle lands where it says.
components=$stage/components.plist
pkgbuild --analyze --root "$stage/root" "$components"
index=0
while /usr/libexec/PlistBuddy -c "Print :$index" "$components" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$index:BundleIsRelocatable false" "$components"
    index=$((index + 1))
done

mkdir -p "$stage/scripts"
cp "$here/scripts/postinstall" "$stage/scripts/postinstall"
chmod +x "$stage/scripts/postinstall"

pkgbuild --root "$stage/root" \
    --component-plist "$components" \
    --scripts "$stage/scripts" \
    --identifier com.rhino.forge.installer \
    --version "$version" \
    --install-location / \
    "$output"
echo "wrote $output (Rhino Forge $version)"
