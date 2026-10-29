#!/usr/bin/env python3
"""Coordinate ONE human Sleep comparison using the two frozen observers.

No device, application, server, sleep or recovery command is issued here. Raw
cohorts have separate lifetimes; the paired MIDI client spans the entire run.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import select
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
PAIRED_SHA = "74f22f99beec4ec8e7eb7d91d3d19723de1ff2aea4b605d2d46d848113de9c1f"
RAW_SHA = "3899b149b230f352edcf198f41321b24fe85adfc639a774d787252a06d1ce21e"
FOOTER = re.compile(r"^Total controller CC messages: (\d+)\n", re.M)
CC = re.compile(r"^CC(\d+) count=(\d+) min=(\d+) max=(\d+) last=(\d+)$", re.M)


def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def instant(value):
    return dt.datetime.fromisoformat(value.replace("Z", "+00:00"))


def process_snapshot():
    result = []
    text = subprocess.check_output(["/bin/ps", "-axo", "pid=,uid=,comm="], text=True, timeout=3)
    for line in text.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) == 3 and ("Contents/MacOS/Guitar Rig 6" in fields[2]
                                or Path(fields[2]).name == "MIDIServer"
                                or "RigKontrol3Audio.driver" in fields[2]):
            result.append(dict(pid=int(fields[0]), uid=int(fields[1]), command=fields[2]))
    return result


def parse_power_log(text, start_utc, end_utc):
    """Select actual transition columns, excluding e.g. 'Wake Requests'."""
    start, end = instant(start_utc), instant(end_utc)
    rows, warnings = [], []
    pattern = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d(?:\.\d+)?)\s+([+-]\d{4})\s+(Sleep|Wake|DarkWake)\s+(.*)$")
    for line in text.splitlines():
        match = pattern.match(line)
        if match:
            stamp, zone, kind, message = match.groups()
            when = dt.datetime.fromisoformat(stamp + zone)
            if start <= when <= end and ((kind == "Sleep" and message.startswith("Entering Sleep state"))
                                        or (kind in ("Wake", "DarkWake") and message.startswith(kind + " from"))):
                rows.append(dict(utc=when.isoformat(), kind=kind, message=message))
        if "Guitar Rig 6" in line and ("Acks" in line or "timed out" in line):
            stamp = re.match(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\s+([+-]\d{4})", line)
            if stamp and start <= dt.datetime.fromisoformat(stamp[1] + stamp[2]) <= end:
                warnings.append(line)
    return dict(read_exit=0, transitions=rows, gr6_ack_warnings=warnings)


def power_snapshot(start_utc, end_utc, outputdir):
    with (outputdir / "pmset.log").open("w") as out, (outputdir / "pmset.stderr").open("w") as err:
        try:
            child = subprocess.run(["/usr/bin/pmset", "-g", "log"], stdout=out, stderr=err, timeout=10)
        except subprocess.TimeoutExpired:
            return dict(read_exit=None, error="pmset_timeout", transitions=[])
    result = parse_power_log((outputdir / "pmset.log").read_text(), start_utc, end_utc)
    result["read_exit"] = child.returncode
    return result


class SleepComparison:
    def __init__(self, output, paired, raw, budget_seconds=1800, expected_paired_sha=None,
                 expected_raw_sha=None, process_reader=None, power_reader=None):
        self.output, self.paired, self.raw = map(Path, (output, paired, raw))
        self.budget_seconds = budget_seconds
        self.expected_hashes = (expected_paired_sha or PAIRED_SHA, expected_raw_sha or RAW_SHA)
        self.process_reader, self.power_reader = process_reader or process_snapshot, power_reader or power_snapshot
        self.state, self.result, self.snapshots = "PRE", None, {}
        self.owned, self.files, self.raw_faults = [], [], []
        self.paired_proc = self.raw_proc = None
        self.started_wall = self.started_monotonic = None
        self.created = False
        self.events = []

    def _save(self, name, value):
        (self.output / (name + ".json")).write_text(json.dumps(value, indent=2) + "\n")

    def _event(self, name, **fields):
        value = dict(event=name, utc=utc(), state=self.state)
        value.update(fields)
        self.events.append(value)
        self._save("events", self.events)
        print(json.dumps(value), flush=True)
        return value

    def _elapsed(self):
        return dict(wall_seconds=time.time() - self.started_wall,
                    monotonic_seconds=time.monotonic() - self.started_monotonic)

    def _budget(self):
        elapsed = self._elapsed()
        if max(elapsed.values()) >= self.budget_seconds:
            raise RuntimeError("whole_run_budget_exceeded")
        return self.budget_seconds - max(elapsed.values())

    def _spawn(self, name, args):
        out, err = (self.output / (name + ".log")).open("w"), (self.output / (name + ".stderr")).open("w")
        self.files.extend((out, err))
        child = subprocess.Popen(args, stdout=out, stderr=err, start_new_session=True)
        self.owned.append(child)
        self._event("spawn", observer=name, pid=child.pid)
        return child

    def _signal(self, child, value):
        if child is not None and child in self.owned and child.poll() is None:
            try:
                os.killpg(child.pid, value)
            except ProcessLookupError:
                pass

    def _stop(self, child, first=signal.SIGINT):
        if child is None:
            return None
        for value, seconds in ((first, 4), (signal.SIGTERM, 2), (signal.SIGKILL, 2)):
            self._signal(child, value)
            try:
                return child.wait(timeout=seconds)
            except subprocess.TimeoutExpired:
                pass
        raise RuntimeError("owned_child_not_reaped")

    def _text(self, name):
        return (self.output / (name + ".log")).read_text(errors="replace")

    def _rows(self, name):
        rows = []
        for line in self._text(name).splitlines(keepends=True):
            if line.endswith("\n") and line.startswith("{"):
                rows.append(json.loads(line))
        return rows

    def _wait(self, predicate, seconds=4):
        deadline = time.monotonic() + seconds
        while True:
            self._budget()
            if self.paired_proc.poll() is not None:
                raise RuntimeError("continuous_paired_observer_exited")
            if predicate():
                return
            if time.monotonic() >= deadline:
                raise RuntimeError("fresh_observation_timeout")
            time.sleep(.05)

    def _footers(self):
        text, result, start = self._text("midi"), [], 0
        for match in FOOTER.finditer(text):
            counts = {cc: dict(zip(("count", "min", "max", "last"), map(int, values)))
                      for cc, *values in CC.findall(text[start:match.start()])}
            total = int(match[1])
            if sum(row["count"] for row in counts.values()) != total:
                raise RuntimeError("inconsistent_complete_midi_footer")
            result.append(dict(counts=counts, total_CC=total, footer_number=len(result) + 1))
            start = match.end()
        return result

    def checkpoint(self, name):
        old = len(self._footers())
        self._signal(self.paired_proc, signal.SIGUSR1)
        self._wait(lambda: len(self._footers()) > old)
        point = dict(self._footers()[-1], utc=utc(), fresh_footer=True, processes=self.process_reader(),
                     paired_pid=self.paired_proc.pid, elapsed=self._elapsed())
        self.snapshots[name] = point
        self._save(name, point)
        return point

    @staticmethod
    def _cc(point, number):
        return point["counts"].get(str(number), dict(count=0, min=127, max=0, last=None))

    def _raw_summary(self, name, complete=False):
        rows = self._rows(name)
        ready = [r for r in rows if r.get("event") == "watch_ready"]
        ends = [r for r in rows if r.get("event") == "watch_end"]
        infos = [r for r in rows if r.get("event") in ("watch_info", "native_info")]
        errors = []
        if len(ready) != 1:
            errors.append("raw_ready_missing_or_duplicate")
        generation = ready[0].get("generation") if ready else None
        if not isinstance(generation, int) or generation <= 0:
            errors.append("raw_generation_missing_or_invalid")
        if not infos:
            errors.append("raw_info_missing")
        for row in infos:
            if (row.get("status") != 0 or row.get("shape") is not True
                    or row.get("schema_identity_valid") is not True or row.get("error") != 0
                    or row.get("generation") != generation):
                errors.append("raw_property_schema_error_or_generation_change")
        if any(r.get("event") == "watch_fault" for r in rows):
            errors.append("raw_watch_fault")
        queries = [r for r in rows if r.get("event") == "watch_query"]
        if any(r.get("status") != 0 or r.get("shape") is not True
               or r.get("bounded_cursor_schema") is not True or r.get("dropped") != 0 for r in queries):
            errors.append("raw_query_invalid_or_dropped")
        reports = [r for r in rows if r.get("event") == "watch_report"]
        cursor = ready[0]["cursor"] if ready else 0
        data, sequences = [], []
        for row in reports:
            value = bytes.fromhex(row["raw_hex"])
            if (row["sequence"] != cursor + 1 or len(value) != row.get("length", len(value))
                    or row.get("reserved") != 0 or not 0 < len(value) <= 64):
                errors.append("raw_sequence_gap_or_length")
            cursor = row["sequence"]
            sequences.append(cursor)
            data.append(value)
        initial = infos[-1].get("digital_snapshot", 0) & 16 if infos else 0
        # Initial native_info is the cohort boundary; later info may already show a press.
        if infos:
            initial = infos[0].get("digital_snapshot", 0) & 16
        digital = [b[1] | b[2] << 8 for b in data if len(b) >= 3 and b[0] == 4]
        previous, rises, releases = bool(initial), 0, 0
        for value in digital:
            bit = bool(value & 16)
            rises += bit and not previous
            releases += previous and not bit
            previous = bit
        analog = [b[5] << 8 | b[6] for b in data if len(b) >= 7 and b[0] == 3]
        if ends and (len(ends) != 1 or ends[0].get("exit") != 0
                     or ends[0].get("reason") != "signal_stop"
                     or ends[0].get("gap_or_dropped") is not False
                     or ends[0].get("exact_count_inference_valid") is not True):
            errors.append("raw_cap_or_uncertain_end")
        if complete and len(ends) != 1:
            errors.append("raw_end_missing")
        summary = dict(valid=not errors, errors=sorted(set(errors)), generation=generation,
                       initial_bit4=bool(initial), final_bit4=previous, bit4_rises=rises, bit4_releases=releases,
                       sequence_range=[sequences[0], sequences[-1]] if sequences else [], fresh_reports=len(data),
                       axis2_range=[min(analog), max(analog)] if analog else None,
                       last_info=infos[-1] if infos else None, last_query=queries[-1] if queries else None,
                       watch_end=ends, witness=rises == releases == 1 and not initial and not previous)
        self._save(name + "-summary", summary)
        return summary

    def _raw_start(self, name):
        self.raw_name = name
        self.raw_proc = self._spawn(name, [str(self.raw)])
        self._wait(lambda: any(r.get("event") == "watch_ready" for r in self._rows(name)))
        summary = self._raw_summary(name)
        if not summary["valid"] or summary["initial_bit4"]:
            raise RuntimeError("raw_cohort_not_ready_or_initial_switch_held")

    def start(self):
        try:
            if self.created or self.started_wall is not None:
                raise RuntimeError("start_already_called")
            if not 0 < self.budget_seconds <= 1800:
                raise RuntimeError("budget_out_of_bounds")
            for path, expected in zip((self.paired, self.raw), self.expected_hashes):
                if not path.is_file() or not os.access(path, os.X_OK) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                    raise RuntimeError("frozen_binary_validation_failed: " + str(path))
            self.output.mkdir(exist_ok=False)
            self.created = True
            self.started_wall, self.started_monotonic, self.started_utc = time.time(), time.monotonic(), utc()
            self.paired_proc = self._spawn("midi", [str(self.paired), "--live"])
            self._raw_start("raw-pre")
            self._wait(lambda: "ACTION_READY paired native/private controlA verified" in self._text("midi"))
            rows = self._rows("midi")
            if not any(r.get("event") == "control" and r.get("phase") == "A" and r.get("valid") is True
                       and r.get("exact_matches") == 1 for r in rows):
                raise RuntimeError("private_control_A_invalid")
            if not any(r.get("event") == "paired_route" and r.get("own_private_readback") == 1 for r in rows):
                raise RuntimeError("private_source_readback_not_verified")
            initial = self.checkpoint("initial")
            if self._cc(initial, 80)["count"] or self._cc(initial, 4)["max"] > 0:
                raise RuntimeError("initial_OFF_heel_baseline_invalid")
            return self._event("READY_PRE_ACTION", instruction="One ON press/release; keep pedal at heel, then pre")
        except Exception as error:
            return self.abort(str(error))

    def before_sleep(self):
        try:
            if self.state != "PRE" or not self.created:
                raise RuntimeError("pre_wrong_state")
            self._budget()
            point = self.checkpoint("pre")
            cc = self._cc(point, 80)
            raw = self._raw_summary("raw-pre")
            if (cc["count"] - self._cc(self.snapshots["initial"], 80)["count"] != 1 or cc["last"] != 127
                    or self._cc(point, 4)["max"] > 0 or not raw["valid"] or not raw["witness"]
                    or self.raw_proc.poll() is not None):
                raise RuntimeError("fresh_pre_baseline_or_raw_witness_invalid")
            code = self._stop(self.raw_proc)
            raw = self._raw_summary("raw-pre", complete=True)
            if code != 0 or not raw["valid"] or not raw["witness"]:
                raise RuntimeError("pre_raw_finalization_invalid")
            if self._budget() < 180:
                raise RuntimeError("insufficient_budget_before_sleep")
            self.state = "SLEEP"
            self._save("pre-raw", raw)
            return self._event("READY_SLEEP", instruction="Apple menu Sleep once; 60s then wake and leave untouched 10s, then wake")
        except Exception as error:
            return self.abort(str(error))

    def after_wake(self):
        try:
            if self.state != "SLEEP":
                raise RuntimeError("wake_wrong_state")
            self._budget()
            point = self.checkpoint("quiet")
            pre = self.snapshots["pre"]
            if (self._cc(point, 80) != self._cc(pre, 80) or self._cc(point, 4)["max"] > 0):
                raise RuntimeError("quiet_interval_midi_changed")
            self._raw_start("raw-post")
            self.state = "POST"
            return self._event("READY_POST_ACTION", instruction="One OFF press/release; heel-to-toe-to-heel sweep, then finish")
        except Exception as error:
            return self.abort(str(error))

    def poll(self):
        if self.result is not None:
            return self.result
        if self.started_wall is None:
            return dict(state=self.state, started=False)
        try:
            remaining = self._budget()
            if self.paired_proc.poll() is not None:
                raise RuntimeError("continuous_paired_observer_exited")
            if self.raw_proc is not None and self.state in ("PRE", "POST") and self.raw_proc.poll() is not None:
                if self.raw_name not in self.raw_faults:
                    self.raw_faults.append(self.raw_name)
                    self._event("RAW_COHORT_EXITED", observer=self.raw_name, exit=self.raw_proc.returncode,
                                consequence="Sleep admission/final verdict invalid; paired observer continues")
            return dict(state=self.state, remaining_seconds=remaining, paired_pid=self.paired_proc.pid,
                        raw_pid=self.raw_proc.pid, raw_faults=self.raw_faults)
        except Exception as error:
            return self.abort(str(error))

    def finish(self):
        try:
            if self.state != "POST":
                raise RuntimeError("finish_wrong_state")
            self._budget()
            if self.raw_proc.poll() is not None:
                raise RuntimeError("post_raw_exited_before_finish")
            post = self.checkpoint("post")
            raw_code = self._stop(self.raw_proc)
            raw = self._raw_summary("raw-post", complete=True)
            paired_code = self._stop(self.paired_proc)
            results = [json.loads(line[14:]) for line in self._text("midi").splitlines() if line.startswith("PAIRED_RESULT ")]
            paired = results[0] if len(results) == 1 else {}
            self._save("paired-result", paired)
            power = self.power_reader(self.started_utc, utc(), self.output)
            if isinstance(power, str):
                power = parse_power_log(power, self.started_utc, utc())
            self._save("power", power)
            self._budget()
            pre, quiet = self.snapshots["pre"], self.snapshots["quiet"]
            transitions = power.get("transitions", [])
            sleep = [r for r in transitions if r.get("kind") == "Sleep" and instant(pre["utc"]) <= instant(r["utc"]) <= instant(quiet["utc"])]
            wakes = [r for r in transitions if r.get("kind") == "Wake" and sleep
                     and instant(sleep[0]["utc"]) < instant(r["utc"]) <= instant(quiet["utc"])]
            controls = (paired_code in (0, 1) and paired.get("exit") == paired_code
                        and all(paired.get(key) is True for key in ("controlA", "controlB", "controls_valid", "cleanup"))
                        and paired.get("unknown_callbacks") == 0 and paired.get("private_callbacks", 0) >= 2
                        and paired.get("private_packets", 0) >= 2 and paired.get("native_writes") == 0)
            processes_stable = all(p["processes"] == self.snapshots["initial"]["processes"] for p in (pre, quiet, post))
            analog = raw["axis2_range"]
            valid = (controls and raw_code == 0 and raw["valid"] and raw["witness"]
                     and analog is not None and analog[1] - analog[0] >= 100 and not self.raw_faults
                     and power.get("read_exit") == 0 and bool(sleep) and bool(wakes) and processes_stable)
            cc80, cc4 = self._cc(post, 80), self._cc(post, 4)
            delivered = (cc80["count"] - self._cc(quiet, 80)["count"] == 1 and cc80["last"] == 0
                         and cc4["count"] - self._cc(quiet, 4)["count"] > 1
                         and cc4["min"] == 0 and cc4["max"] == 127 and cc4["last"] == 0)
            generation_pre = json.loads((self.output / "pre-raw.json").read_text())["generation"]
            details = dict(controls_valid=controls, paired_exit=paired_code, raw_exit=raw_code,
                           os_sleep_confirmed=bool(sleep and wakes), processes_stable=processes_stable,
                           process_changes=[name for name, point in self.snapshots.items()
                                            if point["processes"] != self.snapshots["initial"]["processes"]],
                           generation_pre=generation_pre, generation_post=raw["generation"],
                           generation_changed_between_cohorts=generation_pre != raw["generation"],
                           post_CC80_delta=cc80["count"] - self._cc(quiet, 80)["count"],
                           post_CC4_delta=cc4["count"] - self._cc(quiet, 4)["count"],
                           wake_to_quiet_seconds=(instant(quiet["utc"]) - instant(wakes[-1]["utc"])).total_seconds() if wakes else None,
                           wake_to_post_seconds=(instant(post["utc"]) - instant(wakes[-1]["utc"])).total_seconds() if wakes else None,
                           limits=["Separate generation-qualified raw cohorts; no raw observation during Sleep",
                                   "Unchanged target PIDs do not prove application subscription continuity",
                                   "Native paired-client comparison only; no audio/all-eight/USB qualification",
                                   "Human UI/LED observations require separate record"])
            return self._complete("PASS" if valid and delivered else "FAIL" if valid else "INCONCLUSIVE",
                                  "postwake_midi_delivered" if valid and delivered else "observed_postwake_midi_missing" if valid else "comparison_evidence_invalid", **details)
        except Exception as error:
            return self.abort(str(error))

    def _complete(self, verdict, reason, **details):
        cleanup = []
        for child in reversed(self.owned):
            try:
                self._stop(child)
                cleanup.append(dict(pid=child.pid, exit=child.returncode, reaped=child.poll() is not None))
            except Exception as error:
                cleanup.append(dict(pid=child.pid, error=str(error), reaped=False))
                verdict = "INCONCLUSIVE"
        for file in self.files:
            file.close()
        self.state = "DONE"
        self.result = dict(verdict=verdict, reason=reason, state=self.state, utc=utc(), cleanup=cleanup, **details)
        if self.started_wall is not None:
            self.result["elapsed"] = self._elapsed()
        if self.created:
            self._save("result", self.result)
            self._event("RESULT", **self.result)
        return self.result

    def abort(self, reason="operator_abort"):
        return self.result if self.result is not None else self._complete("INCONCLUSIVE", reason)


def run_commands(run, input_stream=sys.stdin):
    """Production command loop; exposed for subprocess EOF/signal tests."""
    def interrupted(number, frame):
        raise InterruptedError("parent_signal_" + str(number))
    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGTERM, interrupted)
    try:
        run.start()
        pending = b""
        input_fd = input_stream.fileno()
        while run.state != "DONE":
            run.poll()
            if run.state == "DONE":
                break
            if b"\n" not in pending:
                if not select.select([input_fd], [], [], .25)[0]:
                    continue
                chunk = os.read(input_fd, 4096)
                if not chunk:
                    run.abort("stdin_EOF_partial_command" if pending else "stdin_EOF")
                    break
                pending += chunk
                if b"\n" not in pending:
                    if len(pending) > 1024:
                        run.abort("stdin_command_too_long")
                    continue
            line, _, pending = pending.partition(b"\n")
            if len(line) > 1024:
                run.abort("stdin_command_too_long")
                break
            command = line.decode("ascii").strip().lower()
            actions = {"pre": run.before_sleep, "wake": run.after_wake, "finish": run.finish,
                       "abort": run.abort, "status": run.poll}
            if command in actions:
                value = actions[command]()
                if command == "status":
                    print(json.dumps(value), flush=True)
            else:
                run.abort("unknown_command")
    except BaseException as error:
        # Ignore repeat termination during bounded cleanup of our own children.
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        run.abort(str(error))
    if run.result and not run.created:
        print(json.dumps(run.result), flush=True)
    return {"PASS": 0, "FAIL": 1}.get((run.result or {}).get("verdict"), 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="New evidence directory; existing paths are refused")
    parser.add_argument("--paired", type=Path, default=ROOT / ".build/native/live-midi-delivery/paired-consumer")
    parser.add_argument("--raw", type=Path, default=ROOT / ".build/native/postsleep-investigation/tools/rawwatch")
    parser.add_argument("--paired-sha256", default=PAIRED_SHA)
    parser.add_argument("--raw-sha256", default=RAW_SHA)
    args = parser.parse_args()
    run = SleepComparison(args.output, args.paired, args.raw,
                          expected_paired_sha=args.paired_sha256, expected_raw_sha=args.raw_sha256)
    return run_commands(run)


if __name__ == "__main__":
    sys.exit(main())
