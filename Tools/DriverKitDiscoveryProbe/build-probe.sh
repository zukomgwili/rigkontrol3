#!/bin/sh
# Normal unsigned Xcode build only. No profile/account updates, signing, launch,
# extension activation/install, USB access or security changes are performed.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:-/private/tmp/rk3-discovery-probe-xcode}
exec xcodebuild -project "$root/DiscoveryProbe.xcodeproj" -scheme DiscoveryProbe \
    -configuration Debug -derivedDataPath "$out" \
    CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO build
