#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 0 ]]; then
    echo "Usage: $0" >&2
    exit 2
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="$repo_root/SharedHAL"
output_dir="$repo_root/.build/RigKontrol3SharedHAL"
bundle_dir="$output_dir/RigKontrol3.driver"
mkdir -p "$output_dir"
staging_dir="$(mktemp -d "$output_dir/staging.XXXXXX")"
trap 'rm -rf "$staging_dir"' EXIT
staged_bundle="$staging_dir/RigKontrol3.driver"
contents_dir="$staged_bundle/Contents"
macos_dir="$contents_dir/MacOS"
resources_dir="$contents_dir/Resources"
mkdir -p "$macos_dir" "$resources_dir"

# Include the exact driver sources and build/install scripts used for this
# artifact, including local adaptations beyond the pinned upstream revision.
snapshot_dir="$resources_dir/SharedHALSource"
mkdir -p "$snapshot_dir/SharedHAL" "$snapshot_dir/Scripts"
while IFS= read -r -d '' source_file; do
    relative_file="${source_file#"$source_dir/"}"
    mkdir -p "$snapshot_dir/SharedHAL/$(dirname "$relative_file")"
    cp -f "$source_file" "$snapshot_dir/SharedHAL/$relative_file"
done < <(find "$source_dir" -type f \( -name '*.c' -o -name '*.h' -o -name '*.plist' -o -name '*.md' -o -name LICENSE \) -print0)
for script_name in build-rk3sharedhal.sh install-rk3sharedhal.sh check-rk3sharedhal.sh; do
    cp -f "$repo_root/Scripts/$script_name" "$snapshot_dir/Scripts/"
done
revision="$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || echo unknown)"
cat > "$snapshot_dir/BUILD-PROVENANCE.txt" <<PROVENANCE
RigKontrol3 shared HAL 0.4.0 (build 4)
Repository: https://github.com/zukomgwili/rigkontrol3/tree/codex/rk3-effects-controller
Checkout base revision: $revision
The bundled snapshot contains the exact local source files used for this build;
the checkout may contain modifications beyond the base revision.
Upstream: https://github.com/mihaimetal/rk3-coreaudio/tree/d0679238ad06842f382ef89b995823e978caec9b
License: GPL-2.0; see SharedHAL/LICENSE and SharedHAL/NOTICE.md.
PROVENANCE
(cd "$snapshot_dir" && find SharedHAL Scripts -type f -exec shasum -a 256 {} \;) > "$snapshot_dir/SHA256SUMS.txt"
build_source_dir="$snapshot_dir/SharedHAL"
cp -f "$build_source_dir/Info.plist" "$contents_dir/Info.plist"
cp -f "$build_source_dir/LICENSE" "$resources_dir/LICENSE"
cp -f "$build_source_dir/NOTICE.md" "$resources_dir/NOTICE.md"

# Compile the captured snapshot itself so the bundled sources correspond to
# the binary even if the working checkout changes during compilation.
native_sources=(
    "$build_source_dir/RigKontrol3SharedHAL.c"
    "$build_source_dir/Control/rk3_control.c"
    "$build_source_dir/Native/usb_iokit.c"
    "$build_source_dir/Native/caiaq_protocol.c"
    "$build_source_dir/Native/codec_mode2.c"
    "$build_source_dir/Native/rk3_engine.c"
)
for source_file in "${native_sources[@]}"; do
    [[ -f "$source_file" ]] || { echo "Missing source: $source_file" >&2; exit 1; }
done
xcrun clang -arch arm64 -mmacosx-version-min=13.0 -std=c11 \
    -Wall -Wextra -Werror -O2 -g -fdebug-prefix-map="$staging_dir"=. -bundle \
    -I "$build_source_dir" -I "$build_source_dir/Native" -I "$build_source_dir/Control" \
    "${native_sources[@]}" \
    -framework CoreAudio -framework CoreFoundation -framework IOKit -lpthread \
    -Wl,-exported_symbol,_RK3DeviceFactory \
    -o "$macos_dir/RigKontrol3SharedHAL"
xcrun clang -arch arm64 -mmacosx-version-min=13.0 -std=c11 \
    -Wall -Wextra -Werror -O2 "$build_source_dir/Tools/rk3sharedprobe.c" \
    -framework CoreAudio -framework CoreFoundation \
    -o "$staging_dir/rk3sharedprobe"
lipo -verify_arch arm64 "$staging_dir/rk3sharedprobe"
codesign --force --sign - --timestamp=none "$staging_dir/rk3sharedprobe"
codesign --verify --strict "$staging_dir/rk3sharedprobe"

plutil -lint "$contents_dir/Info.plist"
[[ "$(plutil -extract CFBundleIdentifier raw "$contents_dir/Info.plist")" == com.rigkontrol3.sharedhal ]]
[[ "$(plutil -extract CFBundleExecutable raw "$contents_dir/Info.plist")" == RigKontrol3SharedHAL ]]
lipo -verify_arch arm64 "$macos_dir/RigKontrol3SharedHAL"
nm -gj "$macos_dir/RigKontrol3SharedHAL" | /usr/bin/grep -x _RK3DeviceFactory >/dev/null
while IFS= read -r dependency; do
    case "$dependency" in
        /System/Library/*|/usr/lib/*) ;;
        *) echo "Non-system shared HAL dependency: $dependency" >&2; exit 1 ;;
    esac
done < <(otool -L "$macos_dir/RigKontrol3SharedHAL" | awk 'NR > 1 {print $1}')
codesign --force --sign - --timestamp=none "$staged_bundle"
codesign --verify --deep --strict "$staged_bundle"
rm -rf "$bundle_dir"
mv -f "$staged_bundle" "$bundle_dir"
mv -f "$staging_dir/rk3sharedprobe" "$output_dir/rk3sharedprobe"
echo "Built $bundle_dir"
echo "Built $output_dir/rk3sharedprobe (not run)"
echo "No driver was installed and no audio service was restarted."
