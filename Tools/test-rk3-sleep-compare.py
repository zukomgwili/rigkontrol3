#!/usr/bin/env python3
"""OFFLINE orchestration regression tests: every child and OS record is synthetic.

These fixtures exercise process lifetimes and evidence classification only. They
neither call CoreMIDI/HAL/USB nor establish any real hardware or sleep result.
"""

import contextlib
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest


MODULE_PATH = Path(__file__).with_name("rk3-sleep-compare.py")
SPEC = importlib.util.spec_from_file_location("rk3_sleep_compare", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


# A real subprocess, but only a synthetic text protocol. Its two executable
# copies receive no system-service, driver, hardware, or sleep capabilities.
FAKE_CHILD = r'''
import json
import os
from pathlib import Path
import signal
import sys
import time

root = Path(__file__).parent
config_path = root / "fixture.json"
role = Path(__file__).name
stopped = False
dump_requested = False

def config():
    return json.loads(config_path.read_text())

def emit(row):
    print(json.dumps(row), flush=True)

def handler(number, frame):
    global stopped, dump_requested
    if number == signal.SIGUSR1:
        dump_requested = True
    else:
        stopped = True

for number in (signal.SIGINT, signal.SIGTERM, signal.SIGUSR1):
    signal.signal(number, handler)

def footer():
    cfg = config()
    total = 0
    for cc, values in sorted(cfg["cc"].items(), key=lambda row: int(row[0])):
        count, minimum, maximum, last = values
        if count:
            total += count
            print("CC%s count=%s min=%s max=%s last=%s" %
                  (cc, count, minimum, maximum, last), flush=True)
    print("Total controller CC messages: %d" % total, flush=True)

if role == "paired":
    (root / "paired.pid").write_text(str(os.getpid()))
    emit({"event": "status", "operation": "own_source_get_private", "status": 0})
    emit({"event": "paired_route", "client": 10, "port": 11,
          "private_source": 12, "native_source": 13, "own_private_readback": 1})
    emit({"event": "native_identity_selection", "matches": 1, "source": 13,
          "entity": 14, "device": 15})
    emit({"event": "native_metadata", "kind": "source", "ref": 13,
          "unique_status": 0, "unique_id": 16, "offline_status": 0,
          "offline": 0, "private_status": 0, "private": 0,
          "protocol_status": 0, "protocol": 1})
    emit({"event": "control", "phase": "A", "sent_timestamp": 100,
          "exact_matches": 1, "valid": True})
    print("ACTION_READY paired native/private controlA verified", flush=True)
    while not stopped:
        if dump_requested:
            dump_requested = False
            if not config().get("suppress_footer", False):
                footer()
        time.sleep(.005)
    valid_b = config().get("control_b", True)
    emit({"event": "control", "phase": "B", "sent_timestamp": 200,
          "exact_matches": int(valid_b), "valid": valid_b})
    footer()
    exit_code = 0 if valid_b else 2
    emit_result = {"controlA": True, "controlB": valid_b,
                   "controls_valid": valid_b, "private_callbacks": 2,
                   "private_packets": 2, "native_callbacks": 1,
                   "native_packets": 1, "native_CC": 1,
                   "unknown_callbacks": 0, "cleanup": True,
                   "exit": exit_code, "native_writes": 0}
    print("PAIRED_RESULT " + json.dumps(emit_result), flush=True)
    sys.exit(exit_code)

count_path = root / "raw-start-count"
cohort = int(count_path.read_text()) + 1 if count_path.exists() else 1
count_path.write_text(str(cohort))
(root / ("raw-%d.pid" % cohort)).write_text(str(os.getpid()))
generation = 2
sequence = 100
for edge in ("watch_initial_before", "watch_initial_after"):
    emit({"event": "native_info", "edge": edge, "status": 0,
          "shape": True, "schema_identity_valid": True, "uid_match": True,
          "generation": generation, "error": 0, "digital_snapshot": 2147483648})
emit({"event": "watch_ready", "object": 126, "generation": generation,
      "cursor": sequence, "seconds_cap": 300, "record_cap": 100000})
revision = config()["raw_revision"]
started = time.monotonic()
reason = "signal_stop"
exit_code = 0
fault = False
while not stopped:
    cfg = config()
    if cfg.get("raw_cap_seconds") is not None and time.monotonic() - started >= cfg["raw_cap_seconds"]:
        reason, exit_code = "duration_cap", 3
        break
    if cfg["raw_revision"] != revision:
        revision = cfg["raw_revision"]
        if cfg.get("raw_fault", False):
            emit({"event": "watch_fault", "reason": "synthetic_property_failure"})
            fault = True
        reports = cfg["raw_reports"]
        emit({"event": "watch_query", "after_sequence": sequence, "status": 0,
              "shape": True, "bounded_cursor_schema": True, "count": len(reports),
              "next_sequence": sequence + len(reports),
              "latest_sequence": sequence + len(reports), "dropped": 0})
        for raw_hex in reports:
            sequence += 1
            emit({"event": "watch_report", "sequence": sequence,
                  "host_time_raw": sequence * 1000, "length": len(raw_hex) // 2,
                  "reserved": 0, "raw_hex": raw_hex})
    time.sleep(.005)
emit({"event": "watch_end", "reason": reason, "exit": exit_code,
      "cursor": sequence, "fresh_reports": sequence - 100,
      "gap_or_dropped": False,
      "exact_count_inference_valid": not fault and exit_code == 0})
sys.exit(exit_code)
'''


def digital(pressed):
    return (bytes([4]) + (16 if pressed else 0).to_bytes(4, "little") + b"\0" * 3).hex()


def analog(value):
    report = bytearray(33)
    report[0] = 3
    report[5:7] = value.to_bytes(2, "big")
    return report.hex()


class OfflineSleepComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="rk3-OFFLINE-fixture-")
        self.root = Path(self.temp.name)
        self.cfg = {"cc": {}, "raw_revision": 0, "raw_reports": [], "control_b": True}
        self.write_config()
        self.paired = self.root / "paired"
        self.raw = self.root / "raw"
        source = "#!" + sys.executable + "\n" + FAKE_CHILD
        for executable in (self.paired, self.raw):
            executable.write_text(source)
            executable.chmod(0o700)
        self.comparisons = []
        self.sleep_utc = None
        self.wake_utc = None
        self.power_calls = 0

    def tearDown(self):
        for comparison in self.comparisons:
            if comparison.state != "DONE":
                with contextlib.redirect_stdout(io.StringIO()):
                    comparison.abort("offline fixture cleanup")
        self.temp.cleanup()

    def write_config(self, **updates):
        self.cfg.update(updates)
        pending = self.root / "fixture.next"
        pending.write_text(json.dumps(self.cfg))
        os.replace(pending, self.root / "fixture.json")

    def power_reader(self, start_utc, end_utc, outputdir):
        self.power_calls += 1
        # OS-shaped synthetic fixture, including whitespace before timezone.
        # Wake Requests and out-of-window rows are deliberately not transitions.
        sleep = datetime.fromisoformat(self.sleep_utc).strftime("%Y-%m-%d %H:%M:%S.%f")
        wake = datetime.fromisoformat(self.wake_utc).strftime("%Y-%m-%d %H:%M:%S.%f")
        return "\n".join([
            "2000-01-01 00:00:00 +0000 Sleep Entering Sleep state due to 'fixture outside window'",
            sleep + "    +0000 Sleep Entering Sleep state due to 'Synthetic fixture'",
            wake + " +0000 Wake Requests [Synthetic request, not transition]",
            wake + "    +0000 Wake Wake from Normal Sleep [Synthetic fixture]",
        ])

    def make_comparison(self):
        comparison = MODULE.SleepComparison(
            self.root / ("output-%d" % len(self.comparisons)), self.paired, self.raw,
            budget_seconds=600,
            expected_paired_sha=hashlib.sha256(self.paired.read_bytes()).hexdigest(),
            expected_raw_sha=hashlib.sha256(self.raw.read_bytes()).hexdigest(),
            process_reader=lambda: [
                {"pid": 111, "uid": 501, "command": "SYNTHETIC Guitar Rig 6"},
                {"pid": 222, "uid": 501, "command": "SYNTHETIC MIDIServer"},
                {"pid": 333, "uid": 501, "command": "SYNTHETIC owner"},
            ], power_reader=self.power_reader)
        self.comparisons.append(comparison)
        return comparison

    def call(self, comparison, method, *args):
        with contextlib.redirect_stdout(io.StringIO()):
            return getattr(comparison, method)(*args)

    def wait_for(self, condition, timeout=3):
        deadline = time.monotonic() + timeout
        while not condition():
            if time.monotonic() >= deadline:
                self.fail("synthetic subprocess did not reach expected condition")
            time.sleep(.01)

    def press_on(self):
        self.write_config(cc={"80": [1, 127, 127, 127]}, raw_revision=1,
                          raw_reports=[digital(False), digital(True), digital(False)])
        time.sleep(.04)

    def reach_post(self, comparison):
        count_path = self.root / "raw-start-count"
        prior_cohorts = int(count_path.read_text()) if count_path.exists() else 0
        self.call(comparison, "start")
        self.assertEqual(comparison.state, "PRE")
        paired_pid = comparison.paired_proc.pid
        self.press_on()
        self.call(comparison, "before_sleep")
        self.assertEqual(comparison.state, "SLEEP")
        self.assertIsNotNone(comparison.owned[1].poll(), "pre raw cohort must be reaped before Sleep")
        self.assertIsNone(comparison.paired_proc.poll(), "paired MIDI collector must span Sleep")
        self.sleep_utc = datetime.now(timezone.utc).isoformat()
        time.sleep(.015)
        self.wake_utc = datetime.now(timezone.utc).isoformat()
        time.sleep(.015)
        self.call(comparison, "after_wake")
        self.assertEqual(comparison.state, "POST")
        self.assertEqual(comparison.paired_proc.pid, paired_pid)
        self.assertEqual(int(count_path.read_text()), prior_cohorts + 2)

    def post_actions(self, delivered=True, raw_fault=False):
        cc = {"80": [2, 0, 127, 0], "4": [3, 0, 127, 0]} if delivered else {"80": [1, 127, 127, 127]}
        self.write_config(cc=cc, raw_revision=2, raw_fault=raw_fault,
                          raw_reports=[digital(False), digital(True), digital(False),
                                       analog(1100), analog(4000), analog(1100)])
        time.sleep(.04)

    def assert_reaped(self, comparison):
        self.assertEqual(comparison.state, "DONE")
        for child in comparison.owned:
            self.assertIsNotNone(child.poll(), "owned child survived cleanup")
            with self.assertRaises(ChildProcessError):
                os.waitpid(child.pid, os.WNOHANG)

    def test_raw_cap_does_not_stop_continuous_paired_collector(self):
        self.write_config(raw_cap_seconds=.6)
        comparison = self.make_comparison()
        self.call(comparison, "start")
        raw_proc, paired_proc = comparison.raw_proc, comparison.paired_proc
        self.wait_for(lambda: raw_proc.poll() is not None)
        self.assertEqual(raw_proc.returncode, 3)
        self.call(comparison, "poll")
        self.assertIsNone(paired_proc.poll(), "raw expiry reproduced the prior coupled-lifetime bug")
        self.assertEqual(comparison.state, "PRE")
        result = self.call(comparison, "abort", "synthetic raw cap observed")
        self.assertEqual(result["verdict"], "INCONCLUSIVE")
        self.assert_reaped(comparison)

    def test_pre_gate_rejects_stale_count_and_missing_fresh_footer(self):
        for mode in ("missing_cc", "stale_positive_footer"):
            with self.subTest(mode=mode):
                self.write_config(cc={}, suppress_footer=False, raw_revision=0)
                comparison = self.make_comparison()
                self.call(comparison, "start")
                self.assertEqual(comparison.state, "PRE")
                if mode == "stale_positive_footer":
                    self.press_on()
                    # Earlier complete count=1 must never satisfy the next
                    # request after that child stops producing fresh footers.
                    os.killpg(comparison.paired_proc.pid, signal.SIGUSR1)
                    self.wait_for(lambda: len(comparison._footers()) == 2)
                    self.write_config(suppress_footer=True)
                else:
                    self.write_config(raw_revision=1,
                                      raw_reports=[digital(False), digital(True), digital(False)])
                    time.sleep(.04)
                result = self.call(comparison, "before_sleep")
                self.assertNotEqual(comparison.state, "SLEEP")
                self.assertEqual(result["verdict"], "INCONCLUSIVE")
                self.assert_reaped(comparison)

    def test_positive_pipeline_uses_distinct_raw_cohorts_and_one_paired_child(self):
        comparison = self.make_comparison()
        self.reach_post(comparison)
        self.post_actions()
        result = self.call(comparison, "finish")
        self.assertEqual(result["verdict"], "PASS", result)
        self.assertEqual(self.power_calls, 1)
        power = json.loads((comparison.output / "power.json").read_text())
        self.assertEqual([row["kind"] for row in power["transitions"]], ["Sleep", "Wake"])
        self.assertEqual(len(comparison.owned), 3)
        self.assert_reaped(comparison)

    def test_missing_post_midi_is_fail_despite_positive_pre_count(self):
        comparison = self.make_comparison()
        self.reach_post(comparison)
        self.post_actions(delivered=False)
        result = self.call(comparison, "finish")
        self.assertEqual(result["verdict"], "FAIL", result)
        self.assert_reaped(comparison)

    def test_invalid_private_b_or_raw_fault_is_inconclusive(self):
        for failure in ("private_b", "raw_fault"):
            with self.subTest(failure=failure):
                self.write_config(cc={}, raw_revision=0, suppress_footer=False,
                                  control_b=True, raw_fault=False)
                comparison = self.make_comparison()
                self.reach_post(comparison)
                self.post_actions(delivered=False, raw_fault=failure == "raw_fault")
                if failure == "private_b":
                    self.write_config(control_b=False)
                result = self.call(comparison, "finish")
                self.assertEqual(result["verdict"], "INCONCLUSIVE", result)
                self.assert_reaped(comparison)

    def run_command_process(self, reason):
        output = self.root / ("command-" + reason)
        wrapper = "\n".join([
            "import importlib.util, sys",
            "from pathlib import Path",
            "spec = importlib.util.spec_from_file_location('offline_compare', %r)" % str(MODULE_PATH),
            "module = importlib.util.module_from_spec(spec)",
            "spec.loader.exec_module(module)",
            "run = module.SleepComparison(%r, %r, %r, budget_seconds=%r," %
            (str(output), str(self.paired), str(self.raw), 1.5 if reason == "partial" else 600),
            "    expected_paired_sha=%r, expected_raw_sha=%r," %
            (hashlib.sha256(self.paired.read_bytes()).hexdigest(),
             hashlib.sha256(self.raw.read_bytes()).hexdigest()),
            "    process_reader=lambda: [], power_reader=lambda *args: {})",
            "sys.exit(module.run_commands(run))",
        ])
        with (self.root / (reason + ".stdout")).open("w") as stdout:
            parent = subprocess.Popen([sys.executable, "-c", wrapper], stdin=subprocess.PIPE,
                                      stdout=stdout, stderr=subprocess.PIPE,
                                      start_new_session=True, text=True)
            try:
                def ready():
                    events = output / "events.json"
                    try:
                        return events.exists() and any(
                            row["event"] == "READY_PRE_ACTION" for row in json.loads(events.read_text()))
                    except json.JSONDecodeError:
                        return False  # The child's next snapshot write is still in progress.
                self.wait_for(ready)
                if reason == "eof":
                    parent.stdin.close()
                elif reason == "partial":
                    # Keep the writer open: a blocking readline would miss the
                    # whole-run deadline while waiting for a newline forever.
                    parent.stdin.write("status")
                    parent.stdin.flush()
                else:
                    parent.send_signal(signal.SIGTERM)
                self.assertEqual(parent.wait(timeout=5), 2)
                result = json.loads((output / "result.json").read_text())
                self.assertEqual(result["verdict"], "INCONCLUSIVE")
                expected = {"eof": "stdin_EOF", "signal": "parent_signal_15",
                            "partial": "whole_run_budget_exceeded"}
                self.assertEqual(result["reason"], expected[reason])
                self.assertEqual(len(result["cleanup"]), 2)
                for record in result["cleanup"]:
                    self.assertTrue(record["reaped"], record)
                    with self.assertRaises(ProcessLookupError):
                        os.kill(record["pid"], 0)
            finally:
                if parent.poll() is None:
                    parent.terminate()
                    parent.wait(timeout=5)
                if parent.stdin and not parent.stdin.closed:
                    parent.stdin.close()
                parent.stderr.close()

    def test_deadlines_eof_and_signal_cleanup_only_owned_children(self):
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"],
                                     start_new_session=True, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
        try:
            for clock_name in ("started_wall", "started_monotonic"):
                with self.subTest(clock=clock_name):
                    comparison = self.make_comparison()
                    self.call(comparison, "start")
                    setattr(comparison, clock_name, getattr(comparison, clock_name) - 601)
                    result = self.call(comparison, "poll")
                    self.assertEqual(result["verdict"], "INCONCLUSIVE")
                    self.assert_reaped(comparison)
                    self.assertIsNone(unrelated.poll(), "cleanup touched an unrelated process")
            for reason in ("eof", "signal", "partial"):
                with self.subTest(parent_exit=reason):
                    self.run_command_process(reason)
                    self.assertIsNone(unrelated.poll(), "command loop touched an unrelated process")
        finally:
            unrelated.terminate()
            unrelated.wait(timeout=3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
