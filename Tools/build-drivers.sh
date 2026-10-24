#!/bin/sh
# Build two native system driver bundles from the reconstructed NI protocol.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out="$root/.build/native/drivers"
mkdir -p "$out"
flags='-std=c11 -arch arm64 -mmacosx-version-min=13.0 -O2 -g -Wall -Wextra -Werror -pedantic -fvisibility=hidden'
audio="$out/RigKontrol3Audio.driver"
midi="$out/RigKontrol3MIDI.plugin"
mkdir -p "$audio/Contents/MacOS" "$midi/Contents/MacOS"
cp -f "$root/Driver/Audio/Info.plist" "$audio/Contents/Info.plist"
cp -f "$root/Driver/MIDI/Info.plist" "$midi/Contents/Info.plist"
xcrun clang $flags -fblocks -bundle -I"$root/Driver" \
    "$root/Driver/Audio/RK3AudioDriver.c" "$root/Driver/NativeUSB.c" \
    "$root/Driver/NativeAudioISO.c" "$root/Driver/DeviceProtocol.c" \
    "$root/Driver/AudioCodec.c" "$root/Driver/AudioSchedule.c" \
    -framework CoreAudio -framework CoreFoundation -framework IOKit \
    -o "$audio/Contents/MacOS/RK3AudioDriver"
xcrun clang $flags -bundle -I"$root/Driver" "$root/Driver/MIDI/RK3MIDIDriver.c" \
    "$root/Driver/MIDIEvent.c" "$root/Driver/MIDI/MIDIStream.c" "$root/Driver/Controller.c" -framework CoreAudio -framework CoreMIDI \
    -framework CoreFoundation -o "$midi/Contents/MacOS/RK3MIDIDriver"
for bundle in "$audio" "$midi"; do
    /usr/bin/codesign --force --sign - --timestamp=none "$bundle"
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
