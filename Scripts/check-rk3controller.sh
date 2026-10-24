#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
app_binary="$repo_root/.build/Rig Kontrol 3.app/Contents/MacOS/rk3controller"
if [[ ! -x "$app_binary" ]]; then
    echo "Build the app first with Scripts/build-rk3controller.sh" >&2
    exit 2
fi

# Run with the RK3 attached, its audio idle, and other controller instances quit.
# This exercises the real native USB/CoreMIDI path, including clean disposal.
event_log="$(mktemp "$repo_root/.build/controller-smoke.XXXXXX")"
python3 - "$app_binary" "$event_log" <<'PY'
import json
import subprocess
import sys

app, path = sys.argv[1:]
timed_out = False
try:
    result = subprocess.run(
        [app, '--smoke-test-seconds', '3', '--event-log', path],
        timeout=10, check=False,
    )
    exit_code = result.returncode
except subprocess.TimeoutExpired:
    timed_out = True
    exit_code = -1

with open(path) as event_file:
    records = [json.loads(line) for line in event_file if line.strip()]
states = [record['status'] for record in records if record['type'] == 'status']
native_backend = any(
    record['type'] == 'backend' and record.get('name') == 'rk3-coreaudio IOKit'
    and record.get('upstreamRevision') == 'd0679238ad06842f382ef89b995823e978caec9b'
    for record in records
)
ready = 'Connected — MIDI ready' in states
clean = any(record['type'] == 'shutdown' and record.get('clean') for record in records)
switch_messages = [
    record for record in records
    if record['type'] == 'midi' and 20 <= record['bytes'][1] <= 27
]
for state in states:
    print(state)
print(f'Event log: {path}')
if timed_out:
    print('FAIL: app did not exit within ten seconds; its test process was stopped.')
elif not records:
    print('FAIL: no test session started. Quit any existing controller app and retry.')
elif not native_backend:
    print('FAIL: expected the pinned rk3-coreaudio IOKit backend.')
elif exit_code or not ready or not clean:
    print('FAIL: expected device readiness and a clean shutdown.')
elif switch_messages:
    print('FAIL: switch MIDI was emitted during the idle check. Leave switches untouched and retry.')
else:
    print('PASS: rk3-coreaudio backend ready, no idle switch commands, clean shutdown.')
    sys.exit(0)
sys.exit(1)
PY
