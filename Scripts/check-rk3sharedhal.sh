#!/usr/bin/env bash
set -euo pipefail
[[ $# -eq 0 ]] || { echo "Usage: $0" >&2; exit 2; }
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
test_output="$repo_root/.build/shared-audio/tests"
mkdir -p "$test_output"
flags=(-arch arm64 -mmacosx-version-min=13.0 -std=c11 -fblocks
       -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
       -fno-omit-frame-pointer)
frameworks=(-framework CoreFoundation -framework CoreAudio -framework IOKit)
xcrun clang "${flags[@]}" "$repo_root/SharedHAL/Tests/control_properties_test.c" \
    "$repo_root/SharedHAL/Control/rk3_control.c" "${frameworks[@]}" \
    -o "$test_output/control_properties_test"
xcrun clang "${flags[@]}" "$repo_root/SharedHAL/Native/Tests/engine_lifecycle_test.c" \
    "$repo_root/SharedHAL/Native/codec_mode2.c" "${frameworks[@]}" \
    -o "$test_output/engine_lifecycle_test"
xcrun clang "${flags[@]}" -I "$repo_root/SharedHAL/Native" \
    "$repo_root/SharedHAL/Tests/codec_test.c" "$repo_root/SharedHAL/Native/codec_mode2.c" \
    -o "$test_output/codec_test"
"$test_output/control_properties_test"
"$test_output/engine_lifecycle_test"
"$test_output/codec_test"
echo 'PASS: hardware-free shared HAL properties, lifecycle and codec tests.'
