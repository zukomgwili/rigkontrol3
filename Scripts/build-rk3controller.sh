#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 0 ]]; then
    echo "Usage: $0" >&2
    exit 2
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
bundle_dir="$repo_root/.build/Rig Kontrol 3.app"

# Keep compiler and package caches writable in restricted development sessions.
# Explicit caller-provided compiler cache locations still take precedence.
export CLANG_MODULE_CACHE_PATH="${CLANG_MODULE_CACHE_PATH:-$repo_root/.build/ModuleCache}"
export SWIFTPM_MODULECACHE_OVERRIDE="${SWIFTPM_MODULECACHE_OVERRIDE:-$repo_root/.build/ModuleCache}"
mkdir -p "$CLANG_MODULE_CACHE_PATH" "$SWIFTPM_MODULECACHE_OVERRIDE"
swift_flags=(
    --package-path "$repo_root"
    --cache-path "$repo_root/.build/SwiftPMCache"
    --config-path "$repo_root/.build/SwiftPMConfig"
    --security-path "$repo_root/.build/SwiftPMSecurity"
    --manifest-cache local
    --disable-sandbox
    --configuration release
    -debug-info-format none
    --arch arm64
)
swift build "${swift_flags[@]}" --product rk3controller
bin_dir="$(swift build "${swift_flags[@]}" --show-bin-path)"
source_binary="$bin_dir/rk3controller"

# Build into a staging bundle so a failed build never leaves a partly updated app.
staging_dir="$(mktemp -d "$repo_root/.build/rk3controller-bundle.XXXXXX")"
trap 'rm -rf "$staging_dir"' EXIT
staged_bundle="$staging_dir/Rig Kontrol 3.app"
contents_dir="$staged_bundle/Contents"
macos_dir="$contents_dir/MacOS"
frameworks_dir="$contents_dir/Frameworks"
resources_dir="$contents_dir/Resources"
mkdir -p "$macos_dir" "$frameworks_dir" "$resources_dir"
cp -f "$source_binary" "$macos_dir/rk3controller"
chmod u+w "$macos_dir/rk3controller"

cat > "$contents_dir/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleDisplayName</key><string>Rig Kontrol 3</string>
    <key>CFBundleExecutable</key><string>rk3controller</string>
    <key>CFBundleIdentifier</key><string>com.rigkontrol3.controller</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>Rig Kontrol 3</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>0.2.0</string>
    <key>CFBundleVersion</key><string>2</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>LSUIElement</key><true/>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# Replace the Homebrew libusb reference with a copy inside the app. Keep system
# frameworks and Swift runtime libraries supplied by macOS.
libusb_reference="$(otool -L "$macos_dir/rk3controller" | awk '$1 ~ /libusb[^[:space:]]*\.dylib$/ {print $1; exit}')"
if [[ -z "$libusb_reference" ]]; then
    echo "Could not find the executable's dynamic libusb dependency." >&2
    exit 1
fi
libusb_name="${libusb_reference##*/}"
case "$libusb_reference" in
    /*) libusb_source="$libusb_reference" ;;
    *) libusb_source="$(pkg-config --variable=libdir libusb-1.0)/$libusb_name" ;;
esac
if [[ ! -f "$libusb_source" ]]; then
    echo "Cannot locate libusb for bundling: $libusb_source" >&2
    exit 1
fi
cp -f "$libusb_source" "$frameworks_dir/$libusb_name"
chmod u+w "$frameworks_dir/$libusb_name"
install_name_tool -id "@rpath/$libusb_name" "$frameworks_dir/$libusb_name"
install_name_tool -change "$libusb_reference" "@executable_path/../Frameworks/$libusb_name" "$macos_dir/rk3controller"

# libusb on this Mac depends only on OS libraries. Fail clearly if a future
# dependency adds another external library instead of silently shipping a broken app.
while IFS= read -r dependency; do
    case "$dependency" in
        /System/Library/*|/usr/lib/*) ;;
        *) echo "Unbundled libusb dependency: $dependency" >&2; exit 1 ;;
    esac
done < <(otool -L "$frameworks_dir/$libusb_name" | awk 'NR > 2 {print $1}')

libusb_prefix="$(pkg-config --variable=prefix libusb-1.0)"
if [[ -f "$libusb_prefix/COPYING" ]]; then
    cp -f "$libusb_prefix/COPYING" "$resources_dir/libusb-COPYING.txt"
fi
if [[ -f "$repo_root/Documentation/Controller-Setup.md" ]]; then
    cp -f "$repo_root/Documentation/Controller-Setup.md" "$resources_dir/Controller-Setup.md"
fi

plutil -lint "$contents_dir/Info.plist"
lipo -verify_arch arm64 "$macos_dir/rk3controller"
lipo -verify_arch arm64 "$frameworks_dir/$libusb_name"
codesign --force --sign - --timestamp=none "$frameworks_dir/$libusb_name"
codesign --force --sign - --timestamp=none "$staged_bundle"
codesign --verify --deep --strict "$staged_bundle"

# Only the fixed build-output path is replaced; nothing is installed or launched.
rm -rf "$bundle_dir"
mv -f "$staged_bundle" "$bundle_dir"
echo "Built $bundle_dir"
echo "Open with: open \"$bundle_dir\""
