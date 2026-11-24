#!/bin/zsh
# Packages a notarized KoEnJa.app for a GitHub release (from Araso's scripts/release.sh, MIT):
#   KoEnJa-Mac-<version>.zip, KoEnJa-Mac.zip   the app, for Mac/get.sh and the in-app updater
#   KoEnJa-<version>.pkg, KoEnJa.pkg           an installer into /Library/Input Methods that
#                                              registers it for the logged-in user (Mac/pkg/postinstall)
# The app comes signed and notarized from Xcode (HANDOFF §4: archive, exportArchive, exportNotarizedApp,
# stapler); this script doesn't build it. The package is signed and notarized when given
#   KOENJA_PKG_SIGN="Developer ID Installer: CIZMO, K.K. (N723QM4R64)"
#   KOENJA_NOTARY_PROFILE=koenja   # once: xcrun notarytool store-credentials koenja …
# Without them it is unsigned: enough to try the installer on this Mac, not for a release.
#
#   zsh Mac/release.sh <path/to/notarized/KoEnJa.app> [out dir]   (zsh: not sh)
set -euo pipefail
app=${1:?notarized KoEnJa.app}
out=${2:-${app:h}/../release}
[[ -d $app/Contents ]] || { echo "no app at $app" >&2; exit 1 }
codesign --verify --strict --deep "$app"
# Not grep -q: with pipefail, grep closing the pipe early fails the pipeline.
[[ $(codesign -dv "$app" 2>&1) == *"TeamIdentifier=N723QM4R64"* ]] || { echo "$app isn't signed by CIZMO" >&2; exit 1 }
xcrun stapler validate "$app" >/dev/null || { echo "$app isn't notarized and stapled" >&2; exit 1 }
version=$(plutil -extract CFBundleShortVersionString raw "$app/Contents/Info.plist")
here=${0:A:h}
rm -rf "$out"; mkdir -p "$out/root"

quietly() {
  local log
  log=$("$@" 2>&1) || { print -r -- "$log" >&2; return 1 }
  print -r -- "$log" | grep -i 'warning' >&2 || true
}
# notarytool submit --wait can end without an error when Apple rejects the upload: check the
# status, and print Apple's log of what it found when it isn't Accepted.
notarize() {
  local result id notary_status
  result=$(xcrun notarytool submit "$1" --keychain-profile "$KOENJA_NOTARY_PROFILE" --wait --output-format json) || true
  id=$(plutil -extract id raw - <<< "$result" 2>/dev/null) || true
  notary_status=$(plutil -extract status raw - <<< "$result" 2>/dev/null) || true
  [[ $notary_status == Accepted ]] && return
  print -r -- "notarization of $1: ${notary_status:-failed}" >&2
  print -r -- "$result" >&2
  [[ -n $id ]] && xcrun notarytool log "$id" --keychain-profile "$KOENJA_NOTARY_PROFILE" >&2
  return 1
}

# The zip keeps the signature and its sealed resources intact (ditto, not zip).
ditto -c -k --keepParent "$app" "$out/KoEnJa-Mac-$version.zip"
cp "$out/KoEnJa-Mac-$version.zip" "$out/KoEnJa-Mac.zip"

# The payload goes to a staging folder and the postinstall syncs it into /Library/Input Methods.
# Not relocatable: the installer must not "upgrade" a copy it finds elsewhere in place.
cp -R "$app" "$out/root/"
quietly pkgbuild --analyze --root "$out/root" "$out/component.plist"
plutil -replace 0.BundleIsRelocatable -bool false "$out/component.plist"
quietly pkgbuild --root "$out/root" --component-plist "$out/component.plist" \
  --install-location "/Library/Application Support/KoEnJa/Install" \
  --scripts "$here/pkg" --identifier com.cizmo.inputmethod.KoEnJa.pkg --version "$version" \
  "$out/KoEnJa-component.pkg"
pkg=$out/KoEnJa-$version.pkg
pkgsign=()
[[ -n ${KOENJA_PKG_SIGN:-} ]] && pkgsign=(--sign "$KOENJA_PKG_SIGN" --timestamp)
sed "s/@VERSION@/$version/g" "$here/Distribution.xml" > "$out/Distribution.xml"
quietly productbuild --distribution "$out/Distribution.xml" --package-path "$out" $pkgsign "$pkg"
# pkgbuild registered the staged copy with LaunchServices; unregister it so macOS never picks it
# when it launches the input method (Mac/lsclean.sh does the same for build products).
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u "$out/root/KoEnJa.app" 2>/dev/null || true
rm -rf "$out/root" "$out/component.plist" "$out/KoEnJa-component.pkg" "$out/Distribution.xml"

if [[ -n ${KOENJA_NOTARY_PROFILE:-} ]]; then
  notarize "$pkg"
  xcrun stapler staple "$pkg"
fi
cp "$pkg" "$out/KoEnJa.pkg"
[[ -z ${KOENJA_PKG_SIGN:-} ]] && echo "warning: unsigned package: Gatekeeper refuses it when downloaded; for trying it on this Mac only" >&2
echo "built $out/KoEnJa-Mac-$version.zip, $out/KoEnJa-Mac.zip, $pkg, $out/KoEnJa.pkg"
echo "  (${KOENJA_PKG_SIGN:-unsigned pkg}${KOENJA_NOTARY_PROFILE:+, notarized})"
