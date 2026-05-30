# Project Instructions for AI Agents

This file provides instructions and context for AI coding agents working on this project.

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:7510c1e2 -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/SYNC_CONCEPTS.md for details and anti-patterns.

## Session Completion

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds
<!-- END BEADS INTEGRATION -->


## Build & Test

```bash
swift build
swift test
swift run rk3probe
```

## Architecture Overview

RigKontrol3 is a native macOS Apple Silicon MIDI bridge project for Native Instruments Rig Kontrol 3 pedal and switch control data.

The first milestone is controller-only: read Rig Kontrol 3 control events and publish them through CoreMIDI as a virtual MIDI source. Audio interface support is out of scope until the controller path is working.

Current structure:

- `RigKontrol3Core`: Swift library for device probes, mapping, calibration, and MIDI message construction.
- `rk3probe`: command-line probe for USB/HID/MIDI visibility checks.
- `Research/rig-kontrol-3.md`: verified local hardware and driver facts.

DriverKit is a documented fallback only if app-level USB access cannot read the control data reliably or cannot be shipped with acceptable permissions. Do not start DriverKit work unless a concrete app-level blocker is recorded; current libusb probes can open the device, claim interface 0 alt 0, and read bulk endpoint `0x81`.

## Conventions & Patterns

- Verify hardware facts from the attached device before relying on public samples.
- Keep hardware access, mapping/calibration, and MIDI output separate.
- Use Beads (`bd`) for task tracking.
- Do not add audio-interface work to the first milestone.
