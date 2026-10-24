#!/usr/bin/env python3
"""Signed ARM64 fixture tests; never installs into the real /Library."""
from pathlib import Path
import fcntl
import hashlib
import json
import os
import plistlib
import shutil
import stat
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
INSTALLER = ROOT / 'Tools/install-native-drivers.sh'
DEFINITIONS = [
    ('RigKontrol3Audio.driver', 'RK3AudioDriver', 'RK3AudioFactory', 'Audio', '/Library/Audio/Plug-Ins/HAL'),
    ('RigKontrol3MIDI.plugin', 'RK3MIDIDriver', 'RK3MIDIDriverFactory', 'MIDI', '/Library/Audio/MIDI Drivers'),
]


def command(arguments, **kwargs):
    return subprocess.run(arguments, check=True, capture_output=True, text=True, timeout=30, **kwargs)


def write_manifest(directory):
    files = {}
    for name, *_ in DEFINITIONS:
        for item in sorted((directory / name).rglob('*')):
            if item.is_file():
                files[str(item.relative_to(directory))] = hashlib.sha256(item.read_bytes()).hexdigest()
    (directory / 'manifest.json').write_text(json.dumps({'schema': 1, 'architecture': 'arm64', 'files': files}) + '\n')


def fixture_source(directory, version):
    directory.mkdir()
    for name, executable, factory, section, _ in DEFINITIONS:
        bundle = directory / name
        (bundle / 'Contents/MacOS').mkdir(parents=True)
        info = plistlib.loads((ROOT / 'Driver' / section / 'Info.plist').read_bytes())
        info['CFBundleVersion'] = str(version)
        (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
        source = 'void *' + factory + '(void *a, void *b) {(void)a;(void)b;return 0;}\nint RK3FixtureVersion(void){return ' + str(version) + ';}\n'
        command(['xcrun', 'clang', '-arch', 'arm64', '-bundle', '-x', 'c', '-', '-o', str(bundle / 'Contents/MacOS' / executable)], input=source)
        command(['/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', str(bundle)])
    write_manifest(directory)


def main():
    temporary = Path(tempfile.mkdtemp(prefix='rk3-native-installer-tests-', dir='/private/tmp'))
    try:
        first = temporary / 'source1'; second = temporary / 'source2'
        fixture_source(first, 1); fixture_source(second, 2)
        staging = temporary / 'fixture'; staging.mkdir()
        legacy = staging / 'Library/Application Support/RigKontrol3/Quarantine/existing/session.txt'
        legacy.parent.mkdir(parents=True); legacy.write_text('preserve legacy quarantine\n')

        def run(*arguments, source=first, ok=True):
            result = subprocess.run(['/bin/sh', str(INSTALLER), '--source', str(source), '--staging-root', str(staging), *map(str, arguments)],
                                    capture_output=True, text=True, timeout=30)
            assert (result.returncode == 0) == ok, (arguments, result.stdout, result.stderr)
            return result

        def install(source):
            result = run('--install', source=source)
            return Path(next(line.removeprefix('SESSION ') for line in result.stdout.splitlines() if line.startswith('SESSION ')))

        def target(definition):
            return staging / definition[4].lstrip('/') / definition[0]

        def versions(expected):
            for definition in DEFINITIONS:
                bundle = target(definition)
                if expected is None:
                    assert not bundle.exists()
                else:
                    data = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
                    assert data['CFBundleVersion'] == str(expected)
                    for item in [bundle, *bundle.rglob('*')]:
                        expected_mode = 0o755 if item.is_dir() or item == bundle / 'Contents/MacOS' / definition[1] else 0o644
                        assert stat.S_IMODE(item.stat().st_mode) == expected_mode
                        assert item.stat().st_uid == os.getuid()
                        assert item.stat().st_gid == os.getgid()

        run('--check'); versions(None)
        fresh = install(first); versions(1)
        run('--verify-session', fresh)
        run('--restore', fresh); versions(None)
        run('--restore', fresh); versions(None)
        assert all((fresh / 'removed' / definition[0]).exists() for definition in DEFINITIONS)
        print('PASS first install, normalized modes/ownership, verified fresh restore and repeat restore')

        baseline = install(first); upgraded = install(second); versions(2)
        run('--verify-session', upgraded)
        run('--restore', upgraded); versions(1)
        retained = upgraded / 'removed' / DEFINITIONS[0][0]
        displaced = temporary / 'temporarily-missing-new-driver'
        retained.rename(displaced)
        run('--verify-session', upgraded, ok=False); versions(1)
        displaced.rename(retained)
        run('--restore', upgraded); versions(1)
        run('--restore', baseline); versions(None)
        print('PASS same-namespace upgrade and reverse-order restore')

        baseline = install(first); interrupted = install(second)
        # Model an upgrade interrupted after the first old bundle moved but
        # before its replacement, while the second old bundle is untouched.
        os.rename(target(DEFINITIONS[0]), interrupted / 'package' / DEFINITIONS[0][0])
        os.rename(target(DEFINITIONS[1]), interrupted / 'package' / DEFINITIONS[1][0])
        os.rename(interrupted / 'previous' / DEFINITIONS[1][0], target(DEFINITIONS[1]))
        run('--restore', interrupted); versions(1)
        run('--restore', baseline); versions(None)
        print('PASS interrupted mixed-state upgrade recovery')

        corrupted = install(first)
        journal = corrupted / 'manifest.json'; original_journal = journal.read_bytes()
        journal.write_bytes(original_journal + b' ')
        run('--restore', corrupted, ok=False); versions(1)
        journal.write_bytes(original_journal); run('--restore', corrupted); versions(None)
        print('PASS journal tampering refuses all moves')

        lock_file = staging / 'Library/Application Support/RigKontrol3/NativeDriverBackups/.install.lock'
        with lock_file.open('r+') as locked:
            fcntl.flock(locked, fcntl.LOCK_EX | fcntl.LOCK_NB)
            run('--install', ok=False); versions(None)
        print('PASS concurrent mutation lock refusal')

        protected_parent = staging / 'Library/Application Support/RigKontrol3'
        original_mode = stat.S_IMODE(protected_parent.stat().st_mode)
        protected_parent.chmod(0o775); run('--install', ok=False); versions(None); protected_parent.chmod(original_mode)
        hal_parent = staging / DEFINITIONS[0][4].lstrip('/')
        original_mode = stat.S_IMODE(hal_parent.stat().st_mode)
        hal_parent.chmod(0o777); run('--install', ok=False); versions(None); hal_parent.chmod(original_mode)
        print('PASS writable backup/target ancestor refused before mutations')

        collision = install(first)
        changed = target(DEFINITIONS[0]) / 'Contents/Info.plist'
        original = changed.read_bytes(); changed.write_bytes(original + b'\n')
        run('--restore', collision, ok=False)
        assert all(target(definition).exists() for definition in DEFINITIONS)
        changed.write_bytes(original); run('--restore', collision); versions(None)
        print('PASS changed active driver refuses restore before any move')

        manifest = first / 'manifest.json'; original_manifest = manifest.read_bytes()
        broken = json.loads(original_manifest); key = next(iter(broken['files'])); broken['files'][key] = '0' * 64
        manifest.write_text(json.dumps(broken)); run('--install', ok=False); versions(None); manifest.write_bytes(original_manifest)
        print('PASS manifest SHA mismatch refuses install')

        signature = temporary / 'bad-signature'; shutil.copytree(first, signature)
        info_file = signature / DEFINITIONS[0][0] / 'Contents/Info.plist'
        info = plistlib.loads(info_file.read_bytes()); info['CFBundleName'] += ' changed'
        info_file.write_bytes(plistlib.dumps(info)); write_manifest(signature)
        run('--check', source=signature, ok=False)
        print('PASS signature mismatch refused even with refreshed content manifest')

        foreign = temporary / 'foreign'; shutil.copytree(first, foreign)
        bundle = foreign / DEFINITIONS[0][0]; info_file = bundle / 'Contents/Info.plist'
        info = plistlib.loads(info_file.read_bytes()); info['CFBundleIdentifier'] = 'org.unrelated.audio'
        info_file.write_bytes(plistlib.dumps(info))
        command(['/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', str(bundle)])
        write_manifest(foreign); run('--check', source=foreign, ok=False)
        print('PASS foreign signed bundle identity refused')

        intel = temporary / 'intel-fixture'; shutil.copytree(first, intel)
        bundle = intel / DEFINITIONS[0][0]
        command(['xcrun', 'clang', '-arch', 'x86_64', '-bundle', '-x', 'c', '-', '-o', str(bundle / 'Contents/MacOS' / DEFINITIONS[0][1])],
                input='void *RK3AudioFactory(void *a,void *b){(void)a;(void)b;return 0;}\n')
        command(['/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', str(bundle)])
        write_manifest(intel); run('--check', source=intel, ok=False)
        print('PASS signed Intel-only executable refused')

        linked = temporary / 'linked'; linked.symlink_to(first, target_is_directory=True)
        run('--check', source=linked, ok=False)
        hal = staging / DEFINITIONS[0][4].lstrip('/')
        saved_hal = hal.with_name('HAL-saved'); hal.rename(saved_hal)
        outside = temporary / 'outside'; outside.mkdir(); hal.symlink_to(outside, target_is_directory=True)
        run('--install', ok=False); assert list(outside.iterdir()) == []
        hal.unlink(); saved_hal.rename(hal)
        run('--install', '--restart-services', ok=False); versions(None)
        assert legacy.read_text() == 'preserve legacy quarantine\n'
        print('PASS symlink source/destination, fixture service-restart refusal, legacy quarantine preserved')
        print('All installer fixture checks passed; no real driver/service changes made')
    finally:
        shutil.rmtree(temporary)


if __name__ == '__main__':
    main()
