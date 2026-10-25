#!/bin/sh
# Builds the ordinary IOKit client and runs ONLY the injected offline fixtures.
# Never invokes discovery-client --check, signs, activates or installs a service.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mode=${1:-test}
case "$mode" in test|sanitize|build) ;; *) echo 'Usage: sh build-client.sh [test|sanitize|build] [output-directory]' >&2; exit 2 ;; esac
out=${2:-/private/tmp/rk3-discovery-client-$mode}
mkdir -p "$out"
flags='-std=c11 -arch arm64 -mmacosx-version-min=13.0 -Wall -Wextra -Werror -pedantic -g'
if [ "$mode" = sanitize ]; then
    flags="$flags -O1 -fno-omit-frame-pointer -fsanitize=address,undefined"
else
    flags="$flags -O2"
fi
# Flags are controlled option tokens; all filesystem paths remain quoted.
xcrun clang $flags "$root/discovery-client.c" "$root/discovery-result.c" \
    -framework IOKit -framework CoreFoundation -o "$out/discovery-client"
xcrun clang $flags "$root/discovery-client-tests.c" "$root/discovery-result.c" \
    -o "$out/discovery-client-tests"
xcrun lipo -verify_arch arm64 "$out/discovery-client"
if [ "$mode" != build ]; then "$out/discovery-client-tests" --fixture; fi
printf 'Offline client build: %s (no live registry query performed)\n' "$out"
