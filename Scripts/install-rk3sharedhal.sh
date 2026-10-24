#!/usr/bin/env bash
set -Eeuo pipefail
umask 022
# The privileged installer uses only macOS system tools, never caller PATH tools.
export PATH=/usr/bin:/bin:/usr/sbin:/sbin

usage() {
    cat >&2 <<'USAGE'
Usage:
  install-rk3sharedhal.sh install [--source BUNDLE]
  install-rk3sharedhal.sh rollback --backup TRANSACTION_DIRECTORY

Temporary-directory testing only:
  Add --staging-root DIRECTORY --backup-root DIRECTORY to either command.
  Staging mode never changes ownership or restarts audio services.

System installation requires root. This script does not build or launch an app.
It preserves each replaced RK3 bundle outside HAL and prints its rollback command.
USAGE
    exit 2
}
fail() { echo "$*" >&2; exit 1; }
directory_is_within() {
    local child="$1" ancestor="$2"
    while :; do
        [[ "$child" -ef "$ancestor" ]] && return 0
        [[ "$child" == / ]] && return 1
        child="$(dirname "$child")"
    done
}
[[ $# -ge 1 ]] || usage
action="$1"
shift
[[ "$action" == install || "$action" == rollback ]] || usage
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
source_bundle="$repo_root/.build/RigKontrol3SharedHAL/RigKontrol3.driver"
system_root=/Library/Audio/Plug-Ins/HAL
install_root="$system_root"
backup_root='/Library/Application Support/RigKontrol3/DriverBackups'
requested_backup=''
staging=false
explicit_backup_root=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --source) [[ $# -ge 2 && "$action" == install ]] || usage; source_bundle="$2"; shift 2 ;;
        --backup) [[ $# -ge 2 && "$action" == rollback ]] || usage; requested_backup="$2"; shift 2 ;;
        --staging-root) [[ $# -ge 2 ]] || usage; install_root="$2"; staging=true; shift 2 ;;
        --backup-root) [[ $# -ge 2 ]] || usage; backup_root="$2"; explicit_backup_root=true; shift 2 ;;
        *) usage ;;
    esac
done
[[ -d "$install_root" && ! -L "$install_root" ]] || fail "Install directory must already exist and not be a symlink: $install_root"
install_root="$(cd "$install_root" && pwd -P)"
if $staging; then
    $explicit_backup_root || fail 'Staging mode requires --backup-root outside the staging install directory.'
    if directory_is_within "$install_root" "$system_root"; then
        fail 'Staging mode cannot target the system HAL directory.'
    fi
else
    [[ "$(id -u)" == 0 ]] || fail 'Run the reviewed installer with sudo for system installation.'
    if /usr/bin/pgrep -x rk3controller >/dev/null; then
        fail 'Quit Rig Kontrol 3 before changing its shared HAL. No files changed.'
    fi
fi
target_bundle="$install_root/RigKontrol3.driver"
[[ ! -L "$target_bundle" ]] || fail "Refusing a symlink at $target_bundle"

plist_value() { /usr/bin/plutil -extract "$2" raw "$1"; }
executable_path() {
    local executable_name
    executable_name="$(plist_value "$1/Contents/Info.plist" CFBundleExecutable)"
    case "$executable_name" in ''|.|..|*/*) fail 'Invalid bundle executable name.' ;; esac
    printf '%s/Contents/MacOS/%s\n' "$1" "$executable_name"
}
executable_hash() { shasum -a 256 "$(executable_path "$1")" | awk '{print $1}'; }
validate_bundle() {
    local bundle="$1" required_kind="$2" bundle_id executable dependency
    [[ -d "$bundle" && ! -L "$bundle" && -f "$bundle/Contents/Info.plist" && ! -L "$bundle/Contents/Info.plist" ]] || fail "Not a regular driver bundle: $bundle"
    /usr/bin/plutil -lint "$bundle/Contents/Info.plist" >/dev/null
    bundle_id="$(plist_value "$bundle/Contents/Info.plist" CFBundleIdentifier)"
    case "$bundle_id" in
        com.rigkontrol3.sharedhal|com.zukomgwili.RigKontrol3HAL|com.mihai.rk3.audio) ;;
        *) fail "Refusing unexpected driver identity: $bundle_id" ;;
    esac
    executable="$(executable_path "$bundle")"
    [[ -f "$executable" && ! -L "$executable" ]] || fail "Missing regular executable: $executable"
    /usr/bin/lipo -verify_arch arm64 "$executable"
    /usr/bin/codesign --verify --deep --strict "$bundle"
    if [[ "$required_kind" == shared ]]; then
        [[ "$bundle_id" == com.rigkontrol3.sharedhal ]] || fail 'The source must be the new shared HAL.'
        [[ "$(plist_value "$bundle/Contents/Info.plist" CFBundleExecutable)" == RigKontrol3SharedHAL ]] || fail 'Unexpected shared HAL executable.'
        while IFS= read -r dependency; do
            case "$dependency" in
                /System/Library/*|/usr/lib/*) ;;
                *) fail "Non-system shared HAL dependency: $dependency" ;;
            esac
        done < <(/usr/bin/otool -L "$executable" | awk 'NR > 1 {print $1}')
    fi
}

# Reject a second RK3 bundle, including a differently named copy. Other audio
# drivers are read for identity only and are never removed or modified.
for candidate in "$install_root"/*.driver; do
    [[ -d "$candidate" && "$candidate" != "$target_bundle" ]] || continue
    candidate_id="$(plist_value "$candidate/Contents/Info.plist" CFBundleIdentifier 2>/dev/null || true)"
    case "$candidate_id:${candidate##*/}" in
        com.rigkontrol3.sharedhal:*|com.zukomgwili.RigKontrol3HAL:*|com.mihai.rk3.audio:*|*:RigKontrol*.driver)
            fail "Another RK3 HAL must be moved outside HAL before proceeding: $candidate" ;;
    esac
done
if [[ -e "$target_bundle" ]]; then validate_bundle "$target_bundle" any; fi
if [[ "$action" == install ]]; then
    validate_bundle "$source_bundle" shared
else
    [[ -n "$requested_backup" && -d "$requested_backup" && ! -L "$requested_backup" ]] || usage
    requested_backup="$(cd "$requested_backup" && pwd -P)"
    backup_manifest="$requested_backup/Manifest.plist"
    [[ -f "$backup_manifest" && ! -L "$backup_manifest" ]] || fail 'Missing backup manifest.'
    [[ "$(plist_value "$backup_manifest" Version)" == 1 ]] || fail 'Unsupported backup manifest.'
    [[ "$(plist_value "$backup_manifest" InstalledPath)" == "$target_bundle" ]] || fail 'Backup belongs to a different install directory.'
    restore_original="$(plist_value "$backup_manifest" HadOriginalBundle)"
    [[ "$restore_original" == true || "$restore_original" == false ]] || fail 'Invalid backup manifest flag.'
    if [[ "$restore_original" == true ]]; then
        source_bundle="$requested_backup/RigKontrol3.driver"
        validate_bundle "$source_bundle" any
        [[ "$(executable_hash "$source_bundle")" == "$(plist_value "$backup_manifest" OriginalExecutableSHA256)" ]] || fail 'Backup executable hash does not match its manifest.'
    fi
fi

mkdir -p "$backup_root"
backup_root="$(cd "$backup_root" && pwd -P)"
if directory_is_within "$backup_root" "$install_root"; then
    fail 'Backups must be outside the HAL install directory.'
fi
# The lock belongs to the physical HAL destination, independent of the caller's
# backup location. Its directory is not a .driver bundle and is removed on exit.
lock_dir="$install_root/.rigkontrol3-shared-install.lock"
mkdir "$lock_dir" 2>/dev/null || fail "Another installation may be active. Inspect $lock_dir before removing a stale lock."
trap 'rmdir "$lock_dir" 2>/dev/null || true' EXIT
transaction_dir="$(mktemp -d "$backup_root/$(date -u +%Y%m%dT%H%M%SZ).XXXXXX")"
manifest="$transaction_dir/Manifest.plist"
/usr/bin/plutil -create xml1 "$manifest"
/usr/bin/plutil -insert Version -integer 1 "$manifest"
/usr/bin/plutil -insert Action -string "$action" "$manifest"
/usr/bin/plutil -insert InstalledPath -string "$target_bundle" "$manifest"
/usr/bin/plutil -insert CreatedUTC -string "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$manifest"
had_original=false
if [[ -d "$target_bundle" ]]; then
    had_original=true
    /usr/bin/ditto "$target_bundle" "$transaction_dir/RigKontrol3.driver"
    /usr/bin/diff -qr "$target_bundle" "$transaction_dir/RigKontrol3.driver" >/dev/null
    validate_bundle "$transaction_dir/RigKontrol3.driver" any
    /usr/bin/plutil -insert OriginalBundleIdentifier -string "$(plist_value "$target_bundle/Contents/Info.plist" CFBundleIdentifier)" "$manifest"
    /usr/bin/plutil -insert OriginalExecutableSHA256 -string "$(executable_hash "$target_bundle")" "$manifest"
fi
/usr/bin/plutil -insert HadOriginalBundle -bool "$had_original" "$manifest"
/usr/bin/plutil -insert Status -string prepared "$manifest"

replace_started=false
finish_transaction() {
    local failure_status="$1"
    trap - ERR EXIT
    trap '' INT TERM HUP
    set +e
    if [[ "$failure_status" != 0 ]] && $replace_started; then
        echo 'Installation failed; restoring the previous HAL bundle.' >&2
        rm -rf "$target_bundle"
        if $had_original; then
            /usr/bin/ditto "$transaction_dir/RigKontrol3.driver" "$target_bundle"
            /usr/bin/codesign --verify --deep --strict "$target_bundle"
            recovery_status=$?
        else
            recovery_status=0
        fi
        if [[ "$recovery_status" == 0 ]]; then
            /usr/bin/plutil -replace Status -string restored-after-failure "$manifest"
            echo "Previous installation restored. Backup: $transaction_dir" >&2
        else
            echo "Automatic restoration needs attention. Original backup: $transaction_dir" >&2
        fi
    fi
    rmdir "$lock_dir" 2>/dev/null || true
    exit "$failure_status"
}
trap 'finish_transaction "$?"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

if [[ "$action" == install || "${restore_original:-false}" == true ]]; then
    staged_bundle="$transaction_dir/staged.driver"
    /usr/bin/ditto "$source_bundle" "$staged_bundle"
    if ! $staging; then
        chown -R root:wheel "$staged_bundle"
        chmod -R a+rX,u+w,go-w "$staged_bundle"
    fi
    validate_bundle "$staged_bundle" "$([[ "$action" == install ]] && echo shared || echo any)"
fi
replace_started=true
rm -rf "$target_bundle"
if [[ "$action" == install || "${restore_original:-false}" == true ]]; then
    mv -f "$staged_bundle" "$target_bundle"
    if [[ "$action" == install ]]; then
        # Seal the fresh installed path, avoiding copying over a loaded bundle.
        /usr/bin/codesign --force --sign - --timestamp=none "$target_bundle"
    fi
    validate_bundle "$target_bundle" "$([[ "$action" == install ]] && echo shared || echo any)"
fi
/usr/bin/plutil -replace Status -string completed "$manifest"
replace_started=false
if ! $staging; then
    # A normal termination lets launchd reload the newly installed HAL. Never
    # SIGKILL coreaudiod, and never restart audio services in staging tests.
    /usr/bin/killall coreaudiod || {
        echo 'Bundle updated, but coreaudiod was not restarted. Restart audio before testing.' >&2
        exit 1
    }
fi
echo "$action completed at $target_bundle"
echo "Previous installation preserved at: $transaction_dir"
if $staging; then
    printf 'Rollback: %q rollback --backup %q --staging-root %q --backup-root %q\n' "$0" "$transaction_dir" "$install_root" "$backup_root"
else
    printf 'Rollback: sudo %q rollback --backup %q\n' "$0" "$transaction_dir"
fi
