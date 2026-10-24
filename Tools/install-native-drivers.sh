#!/bin/sh
# Exact, reversible installer for the two fresh native driver namespaces.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec /usr/bin/python3 - "$script_dir/../.build/native/drivers" "$@" <<'PY'
import argparse
from contextlib import contextmanager
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import pwd
import re
import shutil
import stat
import subprocess
import sys
import uuid

BASE = Path('/Library/Application Support/RigKontrol3/NativeDriverBackups')
BUNDLES = [
    {'name': 'RigKontrol3Audio.driver', 'id': 'org.rigkontrol3.ni-arm.audio',
     'exe': 'RK3AudioDriver', 'parent': '/Library/Audio/Plug-Ins/HAL'},
    {'name': 'RigKontrol3MIDI.plugin', 'id': 'org.rigkontrol3.ni-arm.midi',
     'exe': 'RK3MIDIDriver', 'parent': '/Library/Audio/MIDI Drivers'},
]


def fail(message):
    raise RuntimeError(message)


def no_links(path):
    for item in (path, *path.parents):
        if item.is_symlink():
            fail('Symlink refused: ' + str(item))


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail('Duplicate manifest key: ' + str(key))
        result[key] = value
    return result


def read_json(path):
    no_links(path)
    return json.loads(path.read_bytes(), object_pairs_hook=unique_object)


def entries(bundle):
    no_links(bundle)
    if not bundle.is_dir():
        fail('Bundle directory absent: ' + str(bundle))
    result = []
    for item in [bundle, *sorted(bundle.rglob('*'))]:
        no_links(item)
        metadata = item.lstat()
        record = {'path': str(item.relative_to(bundle)),
                  'uid': metadata.st_uid, 'gid': metadata.st_gid,
                  'mode': stat.S_IMODE(metadata.st_mode)}
        if stat.S_ISDIR(metadata.st_mode):
            record['kind'] = 'directory'
        elif stat.S_ISREG(metadata.st_mode):
            record.update(kind='file', size=metadata.st_size, sha256=digest(item))
        else:
            fail('Special file refused: ' + str(item))
        result.append(record)
    return result


def snapshot(bundle):
    records = entries(bundle)
    return {'entries': records, 'sha256': hashlib.sha256(canonical(records)).hexdigest()}


def run(command):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30)
    if result.returncode != 0:
        fail(command[0] + ' failed: ' + result.stderr.strip()[:1000])
    return result.stdout + result.stderr


def check_bundle(bundle, definition):
    entries(bundle)  # Refuse links/special files before passing paths to tools.
    info_file = bundle / 'Contents/Info.plist'
    info = plistlib.loads(info_file.read_bytes())
    for key, expected in [('CFBundleIdentifier', definition['id']),
                          ('CFBundleExecutable', definition['exe']),
                          ('CFBundlePackageType', 'BNDL')]:
        if info.get(key) != expected:
            fail('Bundle identity mismatch: ' + str(bundle) + ' / ' + key)
    if not isinstance(info.get('CFBundleVersion'), str) or not info['CFBundleVersion']:
        fail('Bundle version absent: ' + str(bundle))
    executable = bundle / 'Contents/MacOS' / definition['exe']
    architecture = run(['/usr/bin/lipo', '-archs', str(executable)]).strip()
    if architecture != 'arm64':
        fail('Expected arm64-only executable: ' + str(executable))
    run(['/usr/bin/codesign', '--verify', '--strict', '--deep', str(bundle)])
    signing = run(['/usr/bin/codesign', '--display', '--verbose=4', str(bundle)])
    identifiers = [line.removeprefix('Identifier=') for line in signing.splitlines() if line.startswith('Identifier=')]
    if identifiers != [definition['id']]:
        fail('Code-signing identifier mismatch: ' + str(bundle))


def check_package(directory, manifest=None):
    no_links(directory)
    if manifest is None:
        manifest = read_json(directory / 'manifest.json')
    if set(manifest) != {'schema', 'architecture', 'files'} or manifest['schema'] != 1 or manifest['architecture'] != 'arm64':
        fail('Unsupported build manifest')
    expected = manifest['files']
    if not isinstance(expected, dict) or not expected:
        fail('Build manifest has no files')
    names = {item['name'] for item in BUNDLES}
    for relative, checksum in expected.items():
        relative_path = PurePosixPath(relative)
        if (relative_path.is_absolute() or '..' in relative_path.parts or
                str(relative_path) != relative or not relative_path.parts or relative_path.parts[0] not in names or
                not isinstance(checksum, str) or re.fullmatch('[0-9a-f]{64}', checksum) is None):
            fail('Non-allowlisted manifest entry: ' + str(relative))
    actual = {}
    for definition in BUNDLES:
        bundle = directory / definition['name']
        check_bundle(bundle, definition)
        for record in entries(bundle):
            if record['kind'] == 'file':
                actual[definition['name'] + '/' + record['path']] = record['sha256']
    if actual != expected:
        fail('Build manifest file set or SHA-256 mismatch')
    return manifest


parser = argparse.ArgumentParser(description='Install/restore exactly the two native RK3 drivers; legacy quarantine is preserved.')
action = parser.add_mutually_exclusive_group()
action.add_argument('--check', action='store_true', help='read-only artifact check (default)')
action.add_argument('--install', action='store_true', help='install both checked bundles, backing up same-namespace versions')
action.add_argument('--restore', type=Path, metavar='SESSION', help='restore previous state; quarantine newly installed bundles')
action.add_argument('--verify-session', type=Path, metavar='SESSION', help='verify an install/restore journal without moving anything')
parser.add_argument('--source', type=Path, default=Path(sys.argv[1]), help='directory containing the build manifest and both bundles')
parser.add_argument('--staging-root', type=Path, help='existing absolute fixture directory; no privileged/service actions')
parser.add_argument('--restart-services', action='store_true', help='with explicit real install, restart CoreAudio and console-user MIDIServer')
args = parser.parse_args(sys.argv[2:])


def mapped(path):
    return args.staging_root / str(path).lstrip('/') if args.staging_root else Path(path)


def admin():
    if not args.staging_root and os.geteuid() != 0:
        fail('Real installation/restore requires sudo; --check is read-only.')


def trusted_directory(path):
    no_links(path)
    expected_uid = os.getuid() if args.staging_root else 0
    for directory in (path, *path.parents):
        if args.staging_root and directory != args.staging_root and args.staging_root not in directory.parents:
            continue  # /private/tmp is outside the isolated fixture trust root.
        if not directory.exists():
            continue
        metadata = directory.stat()
        if not stat.S_ISDIR(metadata.st_mode) or metadata.st_uid != expected_uid or metadata.st_mode & 0o022:
            fail('Unsafe protected directory ancestor: ' + str(directory))


@contextmanager
def mutation_lock():
    admin()
    base = mapped(BASE)
    trusted_directory(base)
    for definition in BUNDLES:
        trusted_directory(mapped(definition['parent']))
    if base.exists() and not args.staging_root and (base.stat().st_uid != 0 or base.stat().st_mode & 0o022):
        fail('Backup base must be root-owned and not group/world writable')
    base.mkdir(parents=True, exist_ok=True, mode=0o700)
    trusted_directory(base)
    lock_file = base / '.install.lock'
    no_links(lock_file)
    descriptor = os.open(lock_file, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        metadata = os.fstat(descriptor)
        expected_uid = os.getuid() if args.staging_root else 0
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1 or metadata.st_uid != expected_uid or metadata.st_mode & 0o022:
            fail('Unsafe installer lock file')
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            fail('Another native-driver install or restore is already running')
        yield
    finally:
        os.close(descriptor)


def normalize(bundle, definition):
    expected_uid = os.getuid() if args.staging_root else 0
    expected_gid = os.getgid() if args.staging_root else 0
    executable = bundle / 'Contents/MacOS' / definition['exe']
    for item in [bundle, *sorted(bundle.rglob('*'))]:
        no_links(item)
        os.chown(item, expected_uid, expected_gid)
        os.chmod(item, 0o755 if item.is_dir() or item == executable else 0o644)


def journal_write(directory, journal):
    payload = canonical(journal) + b'\n'
    with (directory / 'manifest.json').open('xb') as stream:
        stream.write(payload); stream.flush(); os.fsync(stream.fileno())
    with (directory / 'manifest.sha256').open('x') as stream:
        stream.write(hashlib.sha256(payload).hexdigest() + '\n'); stream.flush(); os.fsync(stream.fileno())


def install_locked():
    admin()
    manifest = check_package(args.source)
    old = []
    for definition in BUNDLES:
        target = mapped(Path(definition['parent']) / definition['name'])
        no_links(target)
        if target.exists():
            check_bundle(target, definition)
            old.append(snapshot(target))
        else:
            old.append(None)
    base = mapped(BASE)
    no_links(base)
    if base.exists() and not args.staging_root and (base.stat().st_uid != 0 or base.stat().st_mode & 0o022):
        fail('Backup base must be root-owned and not group/world writable')
    base.mkdir(parents=True, exist_ok=True, mode=0o700)
    session = base / (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8])
    session.mkdir(mode=0o700)
    package = session / 'package'; package.mkdir(mode=0o700)
    (session / 'previous').mkdir(mode=0o700)
    (session / 'removed').mkdir(mode=0o700)
    for definition in BUNDLES:
        shutil.copytree(args.source / definition['name'], package / definition['name'], symlinks=False)
        normalize(package / definition['name'], definition)
    # Verify the immutable manifest again against the private staged copy.
    check_package(package, manifest)
    items = []
    for definition, previous in zip(BUNDLES, old):
        target = mapped(Path(definition['parent']) / definition['name'])
        no_links(target)
        target.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
        trusted_directory(target.parent)
        no_links(target)
        if target.parent.stat().st_dev != session.stat().st_dev:
            fail('Atomic backup/install requires targets and backup on one filesystem')
        if (snapshot(target) if target.exists() else None) != previous:
            fail('Existing driver changed during staging: ' + str(target))
        items.append({'name': definition['name'], 'old': previous,
                      'new': snapshot(package / definition['name'])})
    journal = {'schema': 1, 'staging_root': str(args.staging_root) if args.staging_root else None,
               'build_manifest': manifest, 'items': items}
    journal_write(session, journal)  # Immutable before the first active-path move.
    print('SESSION ' + str(session), flush=True)
    try:
        for definition, item in zip(BUNDLES, items):
            target = mapped(Path(definition['parent']) / definition['name'])
            if item['old'] is not None:
                os.rename(target, session / 'previous' / definition['name'])
            if target.exists() or target.is_symlink():
                fail('Install destination unexpectedly exists: ' + str(target))
            os.rename(package / definition['name'], target)
            check_bundle(target, definition)
            if snapshot(target) != item['new']:
                fail('Post-install content/ownership/mode mismatch: ' + str(target))
            print('INSTALLED ' + str(target))
    except Exception:
        print('Interrupted install. Restore this SESSION before another install.', file=sys.stderr)
        raise
    print('Both native bundles verified; legacy quarantine is unchanged.')
    return session


def install():
    with mutation_lock():
        return install_locked()


def load_journal(session):
    base = mapped(BASE)
    trusted_directory(base)
    for definition in BUNDLES:
        trusted_directory(mapped(definition['parent']))
    no_links(session)
    if not session.is_absolute() or session.parent != base or not session.is_dir():
        fail('Session must be a direct directory beneath ' + str(base))
    journal_file = session / 'manifest.json'; checksum_file = session / 'manifest.sha256'
    no_links(journal_file); no_links(checksum_file)
    if digest(journal_file) != checksum_file.read_text().strip():
        fail('Install journal SHA-256 mismatch')
    journal = read_json(journal_file)
    if (journal.get('schema') != 1 or
            journal.get('staging_root') != (str(args.staging_root) if args.staging_root else None) or
            [item.get('name') for item in journal.get('items', [])] != [item['name'] for item in BUNDLES]):
        fail('Journal schema/root/allowlist mismatch')
    plans = []
    for definition, item in zip(BUNDLES, journal['items']):
        target = mapped(Path(definition['parent']) / definition['name'])
        previous = session / 'previous' / definition['name']
        removed = session / 'removed' / definition['name']
        staged = session / 'package' / definition['name']
        observed = {}
        for label, location in [('target', target), ('previous', previous), ('removed', removed), ('staged', staged)]:
            no_links(location)
            if location.exists():
                check_bundle(location, definition)
                observed[label] = snapshot(location)
        for label in ('removed', 'staged'):
            if label in observed and observed[label] != item['new']:
                fail('New-driver backup mismatch: ' + str(removed if label == 'removed' else staged))
        if 'previous' in observed and (item['old'] is None or observed['previous'] != item['old']):
            fail('Previous-driver backup mismatch: ' + str(previous))
        if not any(observed.get(label) == item['new'] for label in ('target', 'removed', 'staged')):
            fail('New driver absent from all journal locations: ' + definition['name'])
        if 'target' in observed:
            if 'previous' not in observed and item['old'] is not None and observed['target'] == item['old']:
                plans.append((definition, False, False)); continue  # Untouched/already restored.
            if observed['target'] != item['new'] or 'removed' in observed:
                fail('Restore destination changed/collides: ' + str(target))
            remove_current = True
        else:
            remove_current = False
        if item['old'] is not None and 'previous' not in observed:
            fail('Previous driver absent from both original and backup paths: ' + str(target))
        plans.append((definition, remove_current, item['old'] is not None))
    return plans


def restore_locked(session):
    admin()
    plans = load_journal(session)  # Validate every entry before moving anything.
    for definition, remove_current, restore_previous in plans:
        target = mapped(Path(definition['parent']) / definition['name'])
        if remove_current:
            os.rename(target, session / 'removed' / definition['name'])
        if restore_previous:
            if target.exists() or target.is_symlink():
                fail('Restore refuses to overwrite: ' + str(target))
            os.rename(session / 'previous' / definition['name'], target)
        print('RESTORED PREVIOUS STATE ' + str(target))
    load_journal(session)
    print('Restore verified. Newly installed bundles are retained inside this session.')


def restore(session):
    with mutation_lock():
        restore_locked(session)


def restart_services():
    # This is reachable only after an explicit real --install --restart-services.
    console_uid = os.stat('/dev/console').st_uid
    commands = [['/usr/bin/killall', '-TERM', 'coreaudiod']]
    if console_uid != 0:
        console_user = pwd.getpwuid(console_uid).pw_name
        commands.append(['/usr/bin/killall', '-TERM', '-u', console_user, 'MIDIServer'])
    for command in commands:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=15)
        if result.returncode not in (0, 1):
            fail('Drivers installed, but service restart failed: ' + result.stderr.strip())
    print('CoreAudio and the console user MIDI server were signalled to reload, or were already absent.')


try:
    if args.staging_root:
        if not args.staging_root.is_absolute(): fail('Staging root must be absolute')
        no_links(args.staging_root)
        args.staging_root = args.staging_root.resolve(strict=True)
        if args.staging_root == Path('/') or not args.staging_root.is_dir(): fail('Invalid fixture root')
    if args.restart_services and (not args.install or args.staging_root):
        fail('--restart-services requires an explicit real --install; fixtures never touch services')
    if args.restore:
        restore(args.restore)
    elif args.verify_session:
        load_journal(args.verify_session); print('VERIFIED SESSION ' + str(args.verify_session))
    else:
        no_links(args.source)
        args.source = args.source.resolve(strict=True)
        if args.install:
            install()
            if args.restart_services: restart_services()
            else: print('Reload CoreAudio and the user MIDI server, or restart macOS, before testing.')
        else:
            manifest = check_package(args.source)
            print('CHECKED exact native identities, arm64 signatures and ' + str(len(manifest['files'])) + ' SHA-256 file entries.')
except (RuntimeError, OSError, ValueError, KeyError, TypeError, plistlib.InvalidFileException, subprocess.TimeoutExpired) as error:
    print('ERROR: ' + str(error), file=sys.stderr)
    sys.exit(1)
PY
