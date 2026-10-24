#!/usr/bin/env python3
"""Check a captured host-feedback/cache snapshot; never access MIDI or USB."""

import argparse
import json
from pathlib import Path
import sys


def integer(text):
    try:
        return int(text, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected an integer, such as 0 or 0x81") from error


def switches(indices):
    return ", ".join(f"{index} (CC{index + 19})" for index in sorted(indices))


def mask(states):
    return sum(1 << (index - 1) for index, is_on in states.items() if is_on)


def reject_constant(value):
    raise ValueError(f"invalid JSON constant {value}")


def check(data, after_line, expected_mask):
    lines = data.splitlines(keepends=True)
    errors = []
    feedback = {}
    final_cache = None
    if after_line > len(lines):
        errors.append(f"Boundary {after_line} exceeds the log's {len(lines)} lines.")
    for line_number, raw in enumerate(lines, 1):
        if line_number <= after_line:
            continue
        if not raw.endswith(b"\n"):
            errors.append(f"Line {line_number} is not newline-terminated; retry after the log write completes.")
            continue
        try:
            record = json.loads(raw.decode("utf-8"), parse_constant=reject_constant)
            if not isinstance(record, dict) or not isinstance(record.get("type"), str):
                raise ValueError("expected an object with a string type")
            if record["type"] == "host-midi":
                values = record.get("bytes")
                if not isinstance(values, list) or len(values) not in (2, 3):
                    raise ValueError("host-midi bytes must be a MIDI channel-voice message")
                if any(type(value) is not int or not 0 <= value <= 255 for value in values):
                    raise ValueError("host-midi bytes must be integer bytes")
                status = values[0]
                if not 0x80 <= status <= 0xEF or any(value > 127 for value in values[1:]):
                    raise ValueError("invalid MIDI channel-voice bytes")
                required_length = 2 if status & 0xF0 in (0xC0, 0xD0) else 3
                if len(values) != required_length:
                    raise ValueError("incorrect MIDI channel-voice message length")
                if status == 0xBF and 20 <= values[1] <= 27 and values[2] in (0, 127):
                    feedback[values[1] - 19] = values[2] == 127
            elif record["type"] == "host-state":
                known = record.get("knownSwitches")
                if not isinstance(known, dict) or any(
                    key not in {str(index) for index in range(1, 9)} or type(value) is not bool
                    for key, value in known.items()
                ):
                    raise ValueError("host-state knownSwitches must map switch keys 1..8 to booleans")
                final_cache = {int(key): value for key, value in known.items()}
            # Outgoing `midi` records never establish host state.
        except (UnicodeDecodeError, ValueError) as error:
            errors.append(f"Malformed record at line {line_number}: {error}")

    required = set(range(1, 9))
    missing = required - feedback.keys()
    if missing:
        errors.append(f"Missing fresh binary host feedback for switches: {switches(missing)}.")
    elif mask(feedback) != expected_mask:
        wrong = [index for index in required if feedback[index] != bool(expected_mask & (1 << (index - 1)))]
        errors.append(f"Last fresh feedback mask is 0x{mask(feedback):02X}, expected 0x{expected_mask:02X}; wrong switches: {switches(wrong)}.")
    if final_cache is None:
        errors.append("No host-state cache record after the boundary.")
    elif required - final_cache.keys():
        errors.append(f"Final host-state cache is incomplete; missing switches: {switches(required - final_cache.keys())}.")
    elif mask(final_cache) != expected_mask:
        errors.append(f"Final host-state cache mask is 0x{mask(final_cache):02X}, expected 0x{expected_mask:02X}.")
    return errors, len(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--after-line", required=True, type=integer, help="ignore physical log lines 1 through N")
    parser.add_argument("--expect-mask", required=True, type=integer, help="actual host-effect mask; bit0 is switch1")
    args = parser.parse_args()
    if args.after_line < 0:
        parser.error("--after-line must be nonnegative")
    if not 0 <= args.expect_mask <= 0xFF:
        parser.error("--expect-mask must be between 0x00 and 0xFF")
    print("This checks host feedback/cache, not physical LEDs.")
    try:
        data = args.log.read_bytes()
    except OSError as error:
        print(f"FAIL: cannot read {args.log}: {error}", file=sys.stderr)
        return 1
    errors, final_line = check(data, args.after_line, args.expect_mask)
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print(f"PASS: all eight switches have fresh matching feedback and a complete matching cache (0x{args.expect_mask:02X}); checked lines {args.after_line + 1}..{final_line}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
