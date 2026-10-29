#!/bin/sh
# Original NI binaries and all reconstructed proprietary text stay local.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
reference=${1:-"$root/.build/ni/reference/2.8.0"}
output=${2:-"$root/.build/ni/decompiled/reproduced"}
ghidra=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home}
export JAVA_HOME
if [ ! -x "$ghidra/support/analyzeHeadless" ] || [ ! -x "$JAVA_HOME/bin/java" ]; then
    echo 'Set GHIDRA_HOME and JAVA_HOME to Ghidra12.2 and a compatible JDK.' >&2
    exit 1
fi
python3 - "$reference" "$output" "$ghidra" <<'CHECK'
import hashlib
from pathlib import Path
import sys
reference, output, ghidra = map(Path, sys.argv[1:])
if 'application.version=12.2' not in (ghidra/'Ghidra/application.properties').read_text().splitlines():
    raise SystemExit('This reconstruction is pinned to Ghidra12.2.')
expected={
 'NIUSBAudioDriver.x86_64':'41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08',
 'NIUSBMIDIDriver.x86_64':'84fa1a6d75c7e478af82fe4f7a38ddd633d60754d63d65d6090f0b2517152ae1'}
for name, digest in expected.items():
    if hashlib.sha256((reference/name).read_bytes()).hexdigest()!=digest:
        raise SystemExit('Unexpected NI input SHA-256: '+name)
if output.exists():
    raise SystemExit('Use a new output directory; refusing stale/mixed exports: '+str(output))
CHECK
# Ghidra project databases reject hidden path components such as .codex.
project=$(mktemp -d /private/tmp/rk3-ni-ghidra.XXXXXXXX)
trap 'rm -rf "$project"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$output"
reference=$(CDPATH= cd -- "$reference" && pwd)
output=$(CDPATH= cd -- "$output" && pwd)
for kind in Audio MIDI; do
    case "$kind" in
        Audio) label=audio280-text ;;
        MIDI) label=midi280-text ;;
    esac
    "$ghidra/support/analyzeHeadless" "$project" "$label" \
        -import "$reference/NIUSB${kind}Driver.x86_64" \
        -analysisTimeoutPerFile 600 -scriptPath "$root/Tools" \
        -postScript DecompileDriver.java "$output/$label" \
        >"$output/$label.log" 2>&1
    python3 - "$output/$label" <<'AUDIT'
from pathlib import Path
import sys
p=Path(sys.argv[1])
m=dict(line.split('=',1) for line in (p/'manifest.txt').read_text().splitlines())
if m['cancelled']!='false' or m['decompiled']!=m['functions']:
    raise SystemExit('Incomplete export: '+str(p))
if len(list(p.glob('*.c')))!=int(m['decompiled']):
    raise SystemExit('Export file count differs from manifest: '+str(p))
print(p.name+': '+m['decompiled']+' functions, SHA-256 '+m['input.sha256'])
AUDIT
done
printf 'Private exports: %s\n' "$output"
