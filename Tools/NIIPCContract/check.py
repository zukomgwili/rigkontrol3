#!/usr/bin/env python3
"""Build and run the isolated NI IPC contract proof; never register a port."""

from __future__ import annotations

import argparse
import datetime as dt
import difflib
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import uuid


PAIRS = {
    "transport": ("transport.c", "transport-tests.c", "transport.h"),
    "lifecycle": ("lifecycle.c", "lifecycle-tests.c", "lifecycle.h"),
}
# Exact anchors supplied by the proof owners after their RED/GREEN slices.
# Each mutant must compile, then fail a behavioral assertion; build errors fail the run.
MUTATIONS: tuple[tuple[str, str, str, str], ...] = (
    ("wire-prefix", "transport",
     "return ni_ipc_proof_send(s,message+12,count,reply,capacity);",
     "return ni_ipc_proof_send(s,message,count,reply,capacity);"),
    ("masked-id", "transport",
     "SInt32 msgid; memcpy(&msgid,bytes,sizeof(msgid));",
     "SInt32 msgid; memcpy(&msgid,bytes,sizeof(msgid)); msgid &= 0x00ffffff;"),
    ("changed-timeout", "transport",
     "s->port,msgid,data,2.0,2.0,mode,&returned",
     "s->port,msgid,data,0.5,2.0,mode,&returned"),
    ("receive-equality", "transport",
     "length<signed_count(r->capacity)",
     "length<=signed_count(r->capacity)"),
    ("unsigned-reply-capacity", "transport",
     "length<=signed_count(capacity)",
     "(uint64_t)length<=capacity"),
    ("zero-extended-name-byte", "transport",
     "(0xff00U|byte)", "byte"),
    ("null-callback-reply", "transport",
     "return ops->create_data(ops->context,NULL,0);", "return NULL;"),
    ("stop-without-join", "lifecycle",
     "    emit(impl, sink, NI_IPC_WAIT_TERMINATION, 0, 0);",
     "    /* Deliberate negative control: omit termination wait. */"),
    ("close-while-active", "lifecycle",
     "    if (impl->active != 0 || impl->port == 0) return;",
     "    if (impl->port == 0) return;"),
)

CF_ALLOWED = {
    "_CFRelease", "_CFRetain", "_CFEqual", "_CFGetTypeID",
    "_kCFRunLoopDefaultMode", "_kCFRunLoopCommonModes",
}
FORBIDDEN_PREFIXES = (
    "_IO", "_Audio", "_MIDI", "_Sec", "_OSSystemExtension",
    "_pthread_create", "_pthread_kill", "_dispatch_async", "_dispatch_after",
    "_dispatch_group_async", "_dispatch_source_create", "_exec", "_posix_spawn",
    "_sysctl", "_mach_port_", "_task_for_pid",
)
FORBIDDEN_EXACT = {
    "_socket", "_socketpair", "_connect", "_listen", "_accept", "_bind",
    "_send", "_sendto", "_sendmsg", "_recv", "_recvfrom", "_recvmsg",
    "_getaddrinfo", "_dlopen", "_dlsym", "_system", "_popen", "_fork",
    "_vfork", "_kill",
}


class ProofFailure(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def forbidden(symbol: str) -> bool:
    if symbol.startswith("_CF"):
        return not (symbol in CF_ALLOWED or symbol.startswith(("_CFData", "_CFString")))
    return symbol in FORBIDDEN_EXACT or symbol.startswith(FORBIDDEN_PREFIXES)


class Runner:
    def __init__(self, output: Path, source: Path):
        self.output = output
        self.source = source
        self.sequence = 0
        self.summary: dict = {
            "started_utc": utc_now(), "status": "RUNNING", "source": str(source),
            "output": str(output), "commands": [], "builds": [],
            "negative_controls": [], "scope": "offline_injected_contract_only",
        }

    def save(self) -> None:
        (self.output / "summary.json").write_text(
            json.dumps(self.summary, indent=2, sort_keys=True) + "\n"
        )

    def command(self, label: str, argv: list[str], *, env: dict | None = None,
                timeout: int = 60, expect_success: bool = True) -> subprocess.CompletedProcess:
        self.sequence += 1
        stem = f"{self.sequence:02d}-{label}"
        try:
            result = subprocess.run(argv, text=True, capture_output=True, timeout=timeout,
                                    env=env, check=False)
        except (OSError, subprocess.TimeoutExpired) as error:
            self.summary["commands"].append({"label": label, "argv": argv, "error": str(error)})
            self.save()
            raise ProofFailure(f"{label}: {error}") from error
        stdout = self.output / f"{stem}.stdout.txt"
        stderr = self.output / f"{stem}.stderr.txt"
        stdout.write_text(result.stdout)
        stderr.write_text(result.stderr)
        self.summary["commands"].append({
            "label": label, "argv": argv, "returncode": result.returncode,
            "stdout": stdout.name, "stderr": stderr.name,
            "stdout_sha256": sha256(stdout), "stderr_sha256": sha256(stderr),
        })
        self.save()
        if expect_success and result.returncode:
            raise ProofFailure(f"{label} failed ({result.returncode}); see {stderr}")
        return result

    def inventory(self, label: str, binary: Path) -> dict:
        archs = self.command(label + "-archs", ["/usr/bin/lipo", "-archs", str(binary)]).stdout.split()
        if archs != ["arm64"]:
            raise ProofFailure(f"{label}: expected only arm64, got {archs}")
        raw = self.command(label + "-undefined", ["/usr/bin/nm", "-u", str(binary)]).stdout
        symbols = sorted({line.split()[-1] for line in raw.splitlines() if line.strip()})
        denied = [symbol for symbol in symbols if forbidden(symbol)]
        if denied:
            raise ProofFailure(f"{label}: forbidden live-operation imports: {denied}")
        self.command(label + "-libraries", ["/usr/bin/otool", "-L", str(binary)])
        return {"architecture": archs, "sha256": sha256(binary),
                "undefined_symbols": symbols, "forbidden_symbols": denied}

    def build(self, pair: str, source: Path, binary: Path, *, sanitized: bool) -> None:
        args = [self.clang, "-arch", "arm64", "-isysroot", self.sdk, "-std=c11",
                "-Wall", "-Wextra", "-Werror", "-pedantic", "-O1", "-g"]
        if sanitized:
            args += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        args += [str(source / PAIRS[pair][0]), str(source / PAIRS[pair][1])]
        if pair == "transport":
            args += ["-framework", "CoreFoundation"]
        args += ["-o", str(binary)]
        self.command(binary.name + "-compile", args)

    def execute(self) -> None:
        if platform.system() != "Darwin" or platform.machine() != "arm64":
            raise ProofFailure("This proof requires a native Darwin arm64 host; no Rosetta fallback")
        self.summary["host"] = {"system": platform.system(), "machine": platform.machine()}
        self.clang = self.command("clang-path", ["/usr/bin/xcrun", "--find", "clang"]).stdout.strip()
        self.sdk = self.command("sdk-path", ["/usr/bin/xcrun", "--sdk", "macosx", "--show-sdk-path"]).stdout.strip()
        self.command("compiler-version", [self.clang, "--version"])
        self.command("os-version", ["/usr/bin/sw_vers"])
        snapshot = self.output / "sources"
        snapshot.mkdir()
        files = sorted({name for names in PAIRS.values() for name in names} | {"check.py"})
        source_hashes = {}
        for name in files:
            source = self.source / name
            if not source.is_file():
                raise ProofFailure(f"Source is not ready: {source}")
            source_hashes[name] = sha256(source)
            shutil.copyfile(source, snapshot / name)
            if sha256(snapshot / name) != source_hashes[name]:
                raise ProofFailure(f"Source changed while snapshotting: {source}")
        self.summary["source_sha256"] = source_hashes
        self.save()
        for sanitized in (False, True):
            mode = "sanitized" if sanitized else "strict"
            for pair in PAIRS:
                binary = self.output / f"{pair}-{mode}"
                self.build(pair, snapshot, binary, sanitized=sanitized)
                metadata = self.inventory(binary.name, binary)
                if sanitized:
                    symbols = metadata["undefined_symbols"]
                    if not ("___asan_init" in symbols and any("ubsan_handle" in symbol for symbol in symbols)):
                        raise ProofFailure(f"{binary.name}: expected ASan and UBSan instrumentation imports")
                env = os.environ.copy()
                if sanitized:
                    env["ASAN_OPTIONS"] = "abort_on_error=1"
                    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
                result = self.command(binary.name + "-test", [str(binary)], env=env, timeout=30)
                self.summary["builds"].append({"pair": pair, "mode": mode, **metadata,
                                               "test_output": result.stdout})
                print(result.stdout.rstrip())
                self.save()
        if self.negative_controls:
            self.mutations(snapshot)
        else:
            self.summary["negative_controls_status"] = "NOT_REQUESTED"
        current = {name: sha256(self.source / name) for name in files}
        self.summary["source_unchanged_during_run"] = current == source_hashes
        if current != source_hashes:
            raise ProofFailure("Source changed during the run; repeat on the final reviewed source")
        self.summary["status"] = "PASS"

    def mutations(self, snapshot: Path) -> None:
        if not MUTATIONS:
            raise ProofFailure("Negative-control anchors have not been admitted/implemented yet")
        for name, pair, before, after in MUTATIONS:
            source = self.output / ("negative-" + name)
            source.mkdir()
            for filename in PAIRS[pair]:
                shutil.copyfile(snapshot / filename, source / filename)
            target = source / PAIRS[pair][0]
            original = target.read_text()
            if original.count(before) != 1:
                raise ProofFailure(f"{name}: mutation anchor is not unique")
            changed = original.replace(before, after, 1)
            target.write_text(changed)
            (source / "mutation.diff").write_text("".join(difflib.unified_diff(
                original.splitlines(keepends=True), changed.splitlines(keepends=True),
                fromfile="original/" + target.name, tofile="mutant/" + target.name)))
            binary = self.output / ("mutant-" + name)
            # A compilation failure raises ProofFailure; it never counts as a detected mutant.
            self.build(pair, source, binary, sanitized=False)
            metadata = self.inventory(binary.name, binary)
            result = self.command(binary.name + "-test", [str(binary)], timeout=30, expect_success=False)
            # Require an ordinary test failure, not signal death or a missing executable.
            if result.returncode != 1 or "FAIL" not in result.stdout + result.stderr:
                raise ProofFailure(f"{name}: mutant was not rejected by a behavioral assertion")
            self.summary["negative_controls"].append({
                "name": name, "pair": pair, "status": "REJECTED",
                "returncode": result.returncode, "source_sha256": sha256(target),
                "diff_sha256": sha256(source / "mutation.diff"), **metadata,
            })
            print(f"PASS negative control: {name} rejected")
            self.save()
        self.summary["negative_controls_status"] = "PASS"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="New evidence directory; existing paths are never overwritten")
    parser.add_argument("--negative-controls", action="store_true", help="Compile temporary semantic mutants and require test failure")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    repo = source.parent.parent
    output = args.output or repo / ".build" / "ni" / "ipc-arm-contract" / (
        dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8])
    output = output.resolve()
    if (output == repo or output in repo.parents or source == output or source in output.parents
            or (repo in output.parents and repo / ".build" not in output.parents)):
        parser.error("Evidence output must be outside the repository or under its ignored .build directory")
    try:
        output.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        parser.error(str(error))
    runner = Runner(output, source)
    runner.negative_controls = args.negative_controls
    try:
        runner.execute()
    except (ProofFailure, OSError) as error:
        runner.summary["status"] = "FAIL"
        runner.summary["error"] = str(error)
        print(f"FAIL: {error}", file=sys.stderr)
    finally:
        runner.summary["finished_utc"] = utc_now()
        runner.save()
        print(f"Evidence: {output}")
    return 0 if runner.summary["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
