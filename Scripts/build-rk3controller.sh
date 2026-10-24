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
    <key>CFBundleShortVersionString</key><string>0.3.0</string>
    <key>CFBundleVersion</key><string>3</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>LSUIElement</key><true/>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# Legacy diagnostic code may still link libusb even though the controller uses
# native IOKit. Bundle it only when present; keep OS frameworks and Swift runtime
# libraries supplied by macOS.
libusb_reference="$(otool -L "$macos_dir/rk3controller" | awk '$1 ~ /libusb[^[:space:]]*\.dylib$/ {print $1; exit}')"
if [[ -n "$libusb_reference" ]]; then
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

    # Fail if a future libusb adds an external dependency we do not bundle.
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
    lipo -verify_arch arm64 "$frameworks_dir/$libusb_name"
    codesign --force --sign - --timestamp=none "$frameworks_dir/$libusb_name"
else
    echo "Executable has no dynamic libusb dependency."
fi

# Ship the native backend's exact adapted sources, attribution and license.
native_source_dir="$resources_dir/NativeUSBSource"
mkdir -p "$native_source_dir"
for source_file in \
    "$repo_root/Sources/CRK3CoreAudioUSB/usb_iokit.c" \
    "$repo_root/Sources/CRK3CoreAudioUSB/include/usb_iokit.h" \
    "$repo_root/ThirdParty/rk3-coreaudio/LICENSE" \
    "$repo_root/ThirdParty/rk3-coreaudio/NOTICE.md"; do
    if [[ ! -f "$source_file" ]]; then
        echo "Missing required native USB source or notice: $source_file" >&2
        exit 1
    fi
    cp -f "$source_file" "$native_source_dir/"
done

# Include the Swift adapter and shared transport types/pump used by this build.
for source_file in \
    "$repo_root/Sources/RigKontrol3Core/Hardware/CoreAudioUSBEventReader.swift" \
    "$repo_root/Sources/RigKontrol3Core/Hardware/RigKontrolUSBTypes.swift" \
    "$repo_root/Sources/RigKontrol3Core/Hardware/LibUSBBulkInputPump.swift"; do
    if [[ ! -f "$source_file" ]]; then
        echo "Missing required native USB adapter source: $source_file" >&2
        exit 1
    fi
    cp -f "$source_file" "$native_source_dir/"
done

cat > "$native_source_dir/README.md" <<'SOURCE_INFO'
# Native USB transport source snapshot

This folder contains the adapted IOKit C transport, public header and the
Swift adapter, transport types and input pump used by this build. See NOTICE.md for the
upstream revision and modifications, and LICENSE for GPL-2.0 terms.

Full corresponding app source and build instructions, branch codex/rk3-effects-controller:
https://github.com/zukomgwili/rigkontrol3/tree/codex/rk3-effects-controller
Upstream: https://github.com/mihaimetal/rk3-coreaudio/tree/d0679238ad06842f382ef89b995823e978caec9b

This backend snapshot is only a subset of the app's source. These files alone
are neither a standalone app nor the full corresponding source; use the full
repository branch above to build the controller application.
SOURCE_INFO

if [[ -f "$repo_root/Documentation/Controller-Setup.md" ]]; then
    cp -f "$repo_root/Documentation/Controller-Setup.md" "$resources_dir/Controller-Setup.md"
fi

plutil -lint "$contents_dir/Info.plist"
lipo -verify_arch arm64 "$macos_dir/rk3controller"
codesign --force --sign - --timestamp=none "$staged_bundle"
codesign --verify --deep --strict "$staged_bundle"

# Only the fixed build-output path is replaced; nothing is installed or launched.
rm -rf "$bundle_dir"
mv -f "$staged_bundle" "$bundle_dir"
echo "Built $bundle_dir"
echo "Open with: open \"$bundle_dir\""
