#!/bin/sh
# Build two native system driver bundles from the reconstructed NI protocol.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out="$root/.build/native/drivers"
mkdir -p "$out"
identity=${RK3_CODE_SIGN_IDENTITY:-}
if [ -z "$identity" ]; then
    identity=$(/usr/bin/security find-identity -v -p codesigning | python3 -c '
import re,sys
items=re.findall(r"[0-9]+\) ([0-9A-F]{40}) \"([^\"]+)\"",sys.stdin.read())
for prefix in ("Developer ID Application:","Apple Development:"):
    candidates=[digest for digest,name in items if name.startswith(prefix)]
    if len(candidates)==1:
        print(candidates[0]);break
    if len(candidates)>1:
        raise SystemExit("Set RK3_CODE_SIGN_IDENTITY to the desired Apple-issued signing identity.")
else:
    raise SystemExit("An Apple-issued code-signing identity is required for the CoreMIDI plug-in.")
')
fi
if [ "$identity" = '-' ] && [ "${RK3_ALLOW_ADHOC_TEST_BUILD:-0}" != 1 ]; then
    echo 'Ad-hoc MIDI plug-ins are rejected by MIDIServer on this Mac.' >&2
    exit 1
fi
flags='-std=c11 -arch arm64 -mmacosx-version-min=13.0 -O2 -g -Wall -Wextra -Werror -pedantic -fvisibility=hidden'
audio="$out/RigKontrol3Audio.driver"
midi="$out/RigKontrol3MIDI.plugin"
mkdir -p "$audio/Contents/MacOS" "$midi/Contents/MacOS"
cp -f "$root/Driver/Audio/Info.plist" "$audio/Contents/Info.plist"
cp -f "$root/Driver/MIDI/Info.plist" "$midi/Contents/Info.plist"
xcrun clang $flags -fobjc-arc -fblocks -I"$root/Driver" -c "$root/Driver/NativeHostUSB.m" -o "$out/NativeHostUSB.o"
xcrun clang $flags -fblocks -bundle -I"$root/Driver" \
    "$root/Driver/Audio/RK3AudioDriver.c" "$root/Driver/NativeUSB.c" \
    "$root/Driver/NativeAudioISO.c" "$root/Driver/DeviceProtocol.c" \
    "$root/Driver/AudioCodec.c" "$root/Driver/AudioSchedule.c" "$root/Driver/AudioRing.c" "$root/Driver/AudioClock.c" "$root/Driver/InputGain.c" "$out/NativeHostUSB.o" \
    -framework CoreAudio -framework CoreFoundation -framework IOKit -framework IOUSBHost -framework Foundation \
    -o "$audio/Contents/MacOS/RK3AudioDriver"
xcrun clang $flags -bundle -I"$root/Driver" "$root/Driver/MIDI/RK3MIDIDriver.c" \
    "$root/Driver/MIDIEvent.c" "$root/Driver/MIDI/MIDIStream.c" "$root/Driver/Controller.c" -framework CoreAudio -framework CoreMIDI \
    -framework CoreFoundation -o "$midi/Contents/MacOS/RK3MIDIDriver"
for bundle in "$audio" "$midi"; do
    /usr/bin/codesign --force --sign "$identity" --timestamp=none "$bundle"
    /usr/bin/codesign --verify --strict "$bundle"
done
xcrun lipo -verify_arch arm64 "$audio/Contents/MacOS/RK3AudioDriver"
xcrun lipo -verify_arch arm64 "$midi/Contents/MacOS/RK3MIDIDriver"
python3 - "$out" <<'MANIFEST'
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(sys.argv[1])
files={}
for name in ['RigKontrol3Audio.driver','RigKontrol3MIDI.plugin']:
    for file in sorted((root/name).rglob('*')):
        if file.is_file(): files[str(file.relative_to(root))]=hashlib.sha256(file.read_bytes()).hexdigest()
(root/'manifest.json').write_text(json.dumps({'schema':1,'architecture':'arm64','files':files},indent=2)+'\n')
MANIFEST
printf 'Native system driver bundles: %s\n' "$out"
