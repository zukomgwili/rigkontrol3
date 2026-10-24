#!/bin/sh
# Reversible removal of this machine's observed RK3 driver/workaround bundles.
# This script never unloads services, edits firmware, or changes macOS security.
set -eu
exec /usr/bin/python3 - "$@" <<'PY'
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import stat
import subprocess
import sys
import uuid

VERSION = 1
BACKUP_BASE = Path('/Library/Application Support/RigKontrol3/Quarantine')
# Both the bundle identity and the exact observed Info.plist/executable must
# match. A later installation must be reviewed and added explicitly.
CANDIDATES = [
    {'path': '/Library/Audio/Plug-Ins/HAL/RigKontrol3.driver',
     'id': 'com.rigkontrol3.sharedhal', 'version': '4',
     'exe': 'RigKontrol3SharedHAL',
     'info_sha256': '63f1ec61ab8a0d2591117756214366cf661cf76e5a88980e3e0daeff08c248af',
     'exe_sha256': 'e50519e1d0184eb9bdbc095215af0996c44899b0248e020c7144824bd8d193b3'},
    {'path': '/Library/Extensions/NIUSBAudioDriver.kext',
     'id': 'com.caiaq.driver.NIUSBHardwareDriver', 'version': '2.8.0',
     'exe': 'NIUSBAudioDriver',
     'info_sha256': '0e6ba261d8d6884bdcd848c940025815a887547a1bc287f37f86ab9080e2fb44',
     'exe_sha256': '41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08'},
    {'path': '/Library/Audio/MIDI Drivers/NIUSBMIDIDriver.plugin',
     'id': None, 'version': '2.7.0', 'exe': 'NIUSBMIDIDriver',
     'info_sha256': '564c90db45dc22aa1ff3e93b26035368de872a223f6878763b79d0e43a652fa7',
     'exe_sha256': 'e8eea627e252287d4ae5b82e5789d3a9f789cdabb6b0700639726e48a0e99c1d'},
    {'path': '/Library/PreferencePanes/NIUSBAudio.prefPane',
     'id': 'com.caiaq.NIUSBAudioPreferencePane', 'version': '2.8.0 (R36)',
     'exe': 'NIUSBAudio',
     'info_sha256': 'a1a5f6664f19f1828d34e5d2ae1637fa5d90fc5201ab4ae5e8a41e354021943c',
     'exe_sha256': 'cae7605e94760e35da2eeb976721b82d2d3b4355687ed22bec37fe265b3860b3'},
    {'path': '/Users/zukomgwili/src/rigkontrol3/.build/Rig Kontrol 3.app',
     'id': 'com.rigkontrol3.controller', 'version': '4', 'exe': 'rk3controller',
     'info_sha256': 'eb403e950b508c43c58f2cda6e956329a85133c282d99c1eabd53022d33aa77a',
     'exe_sha256': '4760d4f2936220470914b60cd636abe5531ec28790dafbc31e82bd85091b447b'},
]


def fail(message):
    raise RuntimeError(message)


def sha256(file):
    digest = hashlib.sha256()
    with file.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def no_symlink_ancestors(path):
    for ancestor in (path, *path.parents):
        if ancestor.is_symlink():
            fail('Refusing symlink path: ' + str(ancestor))


def snapshot(bundle):
    """Record all bundle entries, content hashes, ownership and permissions."""
    entries = []
    for item in [bundle, *sorted(bundle.rglob('*'))]:
        metadata = item.lstat()
        record = {'path': str(item.relative_to(bundle)),
                  'mode': stat.S_IMODE(metadata.st_mode),
                  'uid': metadata.st_uid, 'gid': metadata.st_gid}
        if stat.S_ISLNK(metadata.st_mode):
            record.update(kind='link', target=os.readlink(item))
        elif stat.S_ISREG(metadata.st_mode):
            record.update(kind='file', size=metadata.st_size, sha256=sha256(item))
        elif stat.S_ISDIR(metadata.st_mode):
            record.update(kind='directory')
        else:
            fail('Refusing non-file bundle entry: ' + str(item))
        entries.append(record)
    return {'entries': entries, 'sha256': hashlib.sha256(canonical_json(entries)).hexdigest()}


def check_identity(bundle, candidate):
    no_symlink_ancestors(bundle)
    if not bundle.is_dir():
        fail('Expected directory: ' + str(bundle))
    info = bundle / 'Contents/Info.plist'
    executable = bundle / 'Contents/MacOS' / candidate['exe']
    no_symlink_ancestors(info)
    no_symlink_ancestors(executable)
    data = plistlib.loads(info.read_bytes())
    for key, expected in [('CFBundleIdentifier', candidate['id']),
                          ('CFBundleVersion', candidate['version']),
                          ('CFBundleExecutable', candidate['exe'])]:
        if data.get(key) != expected:
            fail('Identity mismatch in ' + str(bundle) + ': ' + key)
    if sha256(info) != candidate['info_sha256'] or sha256(executable) != candidate['exe_sha256']:
        fail('Observed baseline SHA-256 mismatch: ' + str(bundle))


def write_manifest(directory, manifest):
    payload = canonical_json(manifest) + b'\n'
    temporary = directory / 'manifest.json.tmp'
    with temporary.open('xb') as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(directory / 'manifest.json')
    (directory / 'manifest.sha256').write_text(hashlib.sha256(payload).hexdigest() + '\n')


parser = argparse.ArgumentParser(description=__doc__)
actions = parser.add_mutually_exclusive_group()
actions.add_argument('--inventory', action='store_true', help='read-only inventory (default)')
actions.add_argument('--apply', action='store_true', help='quarantine exactly the observed bundles')
actions.add_argument('--restore', metavar='SESSION', type=Path, help='restore one verified quarantine session')
actions.add_argument('--verify', metavar='SESSION', type=Path, help='verify a quarantined session without moving it')
parser.add_argument('--staging-root', type=Path,
                    help='prefix every source/backup path with an existing fixture root; no services')
args = parser.parse_args()


def mapped(path):
    return args.staging_root / str(path).lstrip('/') if args.staging_root else Path(path)


def inventory():
    for candidate in CANDIDATES:
        bundle = mapped(candidate['path'])
        if not bundle.exists() and not bundle.is_symlink():
            print('ABSENT ' + candidate['path'])
            continue
        try:
            check_identity(bundle, candidate)
            print('MATCH  ' + candidate['path'])
        except Exception as error:
            print('REFUSE ' + candidate['path'] + ': ' + str(error))
    print('PRESERVE shared NIHardwareAgent, Controller Editor, Native Access and licensing services.')
    print('PRESERVE archived .build backups/extracted installers; they are not active drivers.')
    if args.staging_root:
        print('STAGING: no process, login-item or service inspection/actions.')
        return
    folders = ['/Library/LaunchAgents', '/Library/LaunchDaemons',
               '/Users/zukomgwili/Library/LaunchAgents']
    for folder in folders:
        for file in sorted(Path(folder).glob('*.plist')):
            try:
                contents = plistlib.loads(file.read_bytes())
                if any(token in str(contents).lower() for token in
                       ('rigkontrol', 'rig kontrol', 'niusb', 'nihardware')):
                    print('SHARED/UNKNOWN launch plist (not moved): ' + str(file))
            except (OSError, plistlib.InvalidFileException):
                if any(token in file.name.lower() for token in
                       ('rigkontrol', 'niusb', 'nihardware')):
                    print('UNKNOWN unreadable related launch plist: ' + str(file))
    print('UNKNOWN login items and loaded service state: this script never edits or unloads them.')
    print('Shared NIUSB drivers may support other NI devices. Quit the old RK3 app and audio clients before applying.')


def assert_admin():
    if not args.staging_root and os.geteuid() != 0:
        fail('Real-system changes require sudo; use --inventory for a read-only preview.')


def apply():
    assert_admin()
    # Preflight every observed component before the first move.
    found = []
    for candidate in CANDIDATES:
        source = mapped(candidate['path'])
        if source.exists() or source.is_symlink():
            check_identity(source, candidate)
            found.append((candidate, source, snapshot(source)))
    if not found:
        print('No allowlisted bundles installed; nothing moved.')
        return
    base = mapped(BACKUP_BASE)
    no_symlink_ancestors(base)
    base.mkdir(parents=True, exist_ok=True, mode=0o700)
    session = base / (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8])
    session.mkdir(mode=0o700)
    (session / 'bundles').mkdir(mode=0o700)
    # Move with os.rename only: refusing cross-volume copies preserves metadata,
    # signatures, ACLs, xattrs and ownership without lossy reconstruction.
    # The manifest is immutable after the first move. Interrupted runs can be
    # recovered by inspecting which side of each rename currently exists.
    manifest = {'version': VERSION, 'staging_root': str(args.staging_root) if args.staging_root else None,
                'items': []}
    for index, (candidate, source, before) in enumerate(found):
        destination = session / 'bundles' / str(index)
        if source.stat().st_dev != session.stat().st_dev:
            fail('Backup must be on the same filesystem as every source: ' + str(source))
        manifest['items'].append({'source': candidate['path'], 'backup': 'bundles/' + str(index),
                                  'snapshot': before})
    write_manifest(session, manifest)
    try:
        for item in manifest['items']:
            source = mapped(item['source'])
            destination = session / item['backup']
            if snapshot(source) != item['snapshot']:
                fail('Source changed after preflight: ' + str(source))
            os.rename(source, destination)
            if snapshot(destination) != item['snapshot']:
                fail('Post-move verification failed: ' + str(destination))
            print('QUARANTINED ' + item['source'])
    except Exception:
        print('Interrupted quarantine. Recover with --restore ' + str(session), file=sys.stderr)
        raise
    print('SESSION ' + str(session))
    print('Verified SHA-256 manifest: ' + str(session / 'manifest.json'))
    print('No services were changed. Restart macOS before assessing the clean runtime baseline.')


def load_session(directory):
    base = mapped(BACKUP_BASE)
    no_symlink_ancestors(directory)
    if not directory.is_absolute() or directory.parent != base or not directory.is_dir():
        fail('Session must be a direct directory beneath ' + str(base))
    manifest_file = directory / 'manifest.json'
    checksum_file = directory / 'manifest.sha256'
    no_symlink_ancestors(manifest_file)
    no_symlink_ancestors(checksum_file)
    payload = manifest_file.read_bytes()
    if hashlib.sha256(payload).hexdigest() != checksum_file.read_text().strip():
        fail('Manifest SHA-256 mismatch')
    manifest = json.loads(payload)
    if manifest.get('version') != VERSION or manifest.get('staging_root') != (str(args.staging_root) if args.staging_root else None):
        fail('Manifest version/staging root mismatch')
    allowed = {candidate['path']: candidate for candidate in CANDIDATES}
    seen = set()
    for index, item in enumerate(manifest['items']):
        source = item['source']
        if source not in allowed or source in seen or item['backup'] != 'bundles/' + str(index):
            fail('Manifest contains a non-allowlisted or duplicate path')
        seen.add(source)
        backup = directory / item['backup']
        no_symlink_ancestors(backup)
        original = mapped(source)
        no_symlink_ancestors(original)
        if backup.exists() and original.exists():
            fail('Restore collision; refusing to overwrite ' + str(original))
        current = backup if backup.exists() else original
        if not current.exists():
            fail('Both original and backup are absent: ' + source)
        check_identity(current, allowed[source])
        if snapshot(current) != item['snapshot']:
            fail('Bundle SHA-256/metadata mismatch: ' + str(current))
    return manifest


def restore(directory, verify_only=False):
    if not verify_only:
        assert_admin()
    manifest = load_session(directory)
    if verify_only:
        print('VERIFIED ' + str(directory))
        return
    # All entries and destination collisions have been validated before moving.
    for item in manifest['items']:
        backup = directory / item['backup']
        original = mapped(item['source'])
        if backup.exists():
            if not original.parent.is_dir():
                fail('Restore parent is missing: ' + str(original.parent))
            os.rename(backup, original)
            if snapshot(original) != item['snapshot']:
                fail('Post-restore verification failed: ' + str(original))
            print('RESTORED ' + item['source'])
    print('Verified restore complete. No services were changed.')


try:
    if args.staging_root:
        if not args.staging_root.is_absolute():
            fail('--staging-root must be an absolute fixture directory other than /')
        no_symlink_ancestors(args.staging_root)
        if not args.staging_root.is_dir():
            fail('--staging-root must already exist')
        args.staging_root = args.staging_root.resolve(strict=True)
        if args.staging_root == Path('/'):
            fail('--staging-root must be a fixture directory other than /')
    if args.apply:
        apply()
    elif args.restore or args.verify:
        restore(args.restore or args.verify, verify_only=bool(args.verify))
    else:
        inventory()
except (RuntimeError, OSError, ValueError, KeyError, plistlib.InvalidFileException) as error:
    print('ERROR: ' + str(error), file=sys.stderr)
    sys.exit(1)
PY
