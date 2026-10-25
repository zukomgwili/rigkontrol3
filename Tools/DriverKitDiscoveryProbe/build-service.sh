#!/bin/sh
# Build ONLY the software discovery dext. No provisioning, signing or activation.
set -eu
probe_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
probe_out=${1:-$(mktemp -d /private/tmp/rk3-dk-service.XXXXXX)}
mkdir -p "$probe_out"
probe_out=$(CDPATH= cd -- "$probe_out" && pwd)
set -x
plutil -lint "$probe_root/Info.plist" "$probe_root/Entitlements.plist"
xcodebuild -project "$probe_root/DiscoveryProbe.xcodeproj" \
    -scheme DiscoveryProbeDriver -configuration Debug -derivedDataPath "$probe_out" \
    CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO build
probe_bundle="$probe_out/Build/Products/Debug-driverkit/org.rigkontrol3.ni-discovery-probe.driver.dext"
probe_binary="$probe_bundle/Contents/MacOS/org.rigkontrol3.ni-discovery-probe.driver"
if [ ! -f "$probe_binary" ]; then probe_binary="$probe_bundle/org.rigkontrol3.ni-discovery-probe.driver"; fi
[ -f "$probe_binary" ]
file "$probe_binary"
shasum -a 256 "$probe_binary"
