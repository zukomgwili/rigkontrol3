#!/bin/sh
# Build the independent NI-derived portable core and native diagnostic on ARM.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mode=${1:-test}
case "$mode" in test|sanitize|build) ;; *) echo 'Usage: sh Tools/build-native.sh [test|sanitize|build]' >&2; exit 2 ;; esac
if [ "$(uname -m)" != arm64 ]; then
    echo 'Run on an Apple Silicon Mac to build and execute the native checks.' >&2
    exit 1
fi
out="$root/.build/native/$mode"
mkdir -p "$out"
# Each token below is a compiler option controlled by this script.
flags='-std=c11 -arch arm64 -mmacosx-version-min=13.0 -Wall -Wextra -Werror -pedantic -g'
if [ "$mode" = sanitize ]; then
    flags="$flags -O1 -fno-omit-frame-pointer -fsanitize=address,undefined"
else
    flags="$flags -O2"
fi
for unit in DeviceProtocol MIDIEvent AudioCodec AudioSchedule AudioRing AudioClock InputGain Controller; do
    xcrun clang $flags -I"$root/Driver" -c "$root/Driver/$unit.c" -o "$out/$unit.o"
done
xcrun ar rcs "$out/librk3-native.a" "$out/DeviceProtocol.o" "$out/MIDIEvent.o" "$out/AudioCodec.o" "$out/AudioSchedule.o" "$out/AudioRing.o" "$out/AudioClock.o" "$out/InputGain.o" "$out/Controller.o"
if [ "$mode" != build ]; then
    for unit in DeviceProtocol MIDIEvent AudioCodec AudioSchedule AudioRing AudioClock InputGain Controller; do
        xcrun clang $flags -I"$root/Driver" "$root/Driver/Tests/${unit}Tests.c" \
            "$out/librk3-native.a" -o "$out/${unit}Tests"
        "$out/${unit}Tests"
    done
fi
if [ -f "$root/Tools/rk3-device-probe.c" ]; then
    xcrun clang $flags -Wno-deprecated-declarations -I"$root/Driver" \
        "$root/Tools/rk3-device-probe.c" "$root/Driver/DeviceProtocol.c" \
        -framework IOKit -framework CoreFoundation -o "$out/rk3-device-probe"
    xcrun lipo -verify_arch arm64 "$out/rk3-device-probe"
fi
if [ "$mode" != build ]; then
    xcrun clang $flags -I"$root/Driver" -fblocks "$root/Driver/Tests/AudioDriverTests.c" \
        "$root/Driver/InputGain.c" \
        -framework CoreAudio -framework CoreFoundation -o "$out/AudioDriverTests"
    "$out/AudioDriverTests"
    for unit in NativeAudioISO NativeUSB; do
        if [ -f "$root/Driver/Tests/${unit}Tests.c" ]; then
            case "$unit" in
                NativeAudioISO) set -- ;;
                NativeUSB) set -- ;;
            esac
            xcrun clang $flags -fblocks -I"$root/Driver" "$root/Driver/Tests/${unit}Tests.c" "$@" \
                "$root/Driver/DeviceProtocol.c" "$root/Driver/AudioCodec.c" "$root/Driver/AudioSchedule.c" "$root/Driver/AudioRing.c" "$root/Driver/AudioClock.c" "$root/Driver/InputGain.c" \
                -framework IOKit -framework CoreFoundation -framework CoreAudio -o "$out/${unit}Tests"
            "$out/${unit}Tests"
        fi
    done
fi
if [ "$mode" != build ]; then
    xcrun clang $flags -I"$root/Driver" "$root/Driver/MIDI/MIDIStreamTests.c" \
        "$root/Driver/MIDI/MIDIStream.c" -o "$out/MIDIStreamTests"
    "$out/MIDIStreamTests"
    xcrun clang $flags -I"$root/Driver" "$root/Driver/MIDI/RK3MIDIDriverTests.c" \
        "$root/Driver/MIDI/MIDIStream.c" "$root/Driver/MIDIEvent.c" "$root/Driver/Controller.c" \
        -framework CoreMIDI -framework CoreAudio -framework CoreFoundation -o "$out/MIDIDriverTests"
    "$out/MIDIDriverTests"
    for variant in pedals calibrate; do
        case "$variant" in calibrate) set -- -DRK3_CALIBRATE_CLI ;; pedals) set -- ;; esac
        xcrun clang $flags "$@" "$root/Tools/rk3-pedal-tools-tests.c" \
            -framework CoreAudio -framework CoreMIDI -framework CoreFoundation -o "$out/$variant-tool-tests"
        "$out/$variant-tool-tests"
    done
fi
xcrun clang $flags -fobjc-arc -fblocks -I"$root/Driver" -c "$root/Driver/NativeHostUSB.m" -o "$out/NativeHostUSB.o"
if [ "$mode" != build ]; then
    xcrun clang $flags -fobjc-arc -fblocks -I"$root/Driver" "$root/Driver/Tests/NativeHostUSBTests.m" \
        -framework IOUSBHost -framework Foundation -framework IOKit -framework CoreFoundation -o "$out/NativeHostUSBTests"
    "$out/NativeHostUSBTests"
fi
xcrun clang $flags -I"$root/Driver" "$root/Tools/rk3-native-test.c" \
    "$root/Driver/NativeUSB.c" "$root/Driver/NativeAudioISO.c" "$root/Driver/AudioCodec.c" \
    "$root/Driver/AudioSchedule.c" "$root/Driver/AudioRing.c" "$root/Driver/AudioClock.c" "$root/Driver/InputGain.c" "$root/Driver/DeviceProtocol.c" "$out/NativeHostUSB.o" \
    -framework IOUSBHost -framework Foundation -framework IOKit -framework CoreFoundation -o "$out/rk3-native-test"
for unit in rk3-calibrate rk3-pedals rk3-midi-monitor rk3-midi-feedback; do
    xcrun clang $flags -I"$root/Driver" "$root/Tools/$unit.c" \
        -framework CoreAudio -framework CoreMIDI -framework CoreFoundation -o "$out/$unit"
done
xcrun clang $flags -I"$root/Driver" "$root/Tools/rk3-input-gain.c" \
    -framework CoreAudio -framework CoreFoundation -o "$out/rk3-input-gain"
xcrun clang $flags "$root/Tools/rk3-driver-smoke.c" -framework CoreAudio \
    -framework CoreFoundation -o "$out/rk3-driver-smoke"
xcrun clang $flags "$root/Tools/rk3-audio-client.c" -framework CoreAudio -framework CoreFoundation \
    -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker "$root/Tools/RK3AudioClient-Info.plist" \
    -o "$out/rk3-audio-client"
/usr/bin/codesign --force --sign - --timestamp=none "$out/rk3-audio-client"
printf 'ARM64 build: %s\n'  "$out"
