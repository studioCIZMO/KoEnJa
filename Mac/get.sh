#!/bin/sh
# Installs or updates KoEnJa for Mac into ~/Library/Input Methods (from Araso's scripts/get.sh, MIT):
#   curl -fsSL https://raw.githubusercontent.com/studioCIZMO/KoEnJa/main/Mac/get.sh | sh
#   … | sh -s -- --uninstall [--delete-data]
# KOENJA_ZIP=<path> installs a local zip instead of the latest release's.
set -eu
url="https://github.com/studioCIZMO/KoEnJa/releases/latest/download/KoEnJa-Mac.zip"
dest="$HOME/Library/Input Methods"
app="$dest/KoEnJa.app"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ "${1:-}" = --uninstall ]; then
  [ -d "$app" ] || { echo "KoEnJa isn't installed."; exit 0; }
  "$app/Contents/MacOS/KoEnJa" --uninstall ${2:-}
  exit 0
fi

if [ -n "${KOENJA_ZIP:-}" ]; then cp "$KOENJA_ZIP" "$work/KoEnJa.zip"
else echo "Downloading KoEnJa…"; curl -fsSL -o "$work/KoEnJa.zip" "$url"; fi
ditto -x -k "$work/KoEnJa.zip" "$work"
[ -d "$work/KoEnJa.app" ] || { echo "The download has no KoEnJa.app." >&2; exit 1; }
codesign --verify --deep --strict "$work/KoEnJa.app" 2>/dev/null &&
  codesign -dv "$work/KoEnJa.app" 2>&1 | grep -qx 'TeamIdentifier=N723QM4R64' &&
  spctl --assess --type execute "$work/KoEnJa.app" 2>/dev/null ||
  { echo "The downloaded KoEnJa.app isn't signed by CIZMO and notarized; nothing was installed." >&2; exit 1; }
xattr -dr com.apple.quarantine "$work/KoEnJa.app" 2>/dev/null || true

mkdir -p "$dest"
rm -rf "$dest/.KoEnJa.app.new"
ditto "$work/KoEnJa.app" "$dest/.KoEnJa.app.new"
rm -rf "$app"
mv "$dest/.KoEnJa.app.new" "$app"
# The running copy is the old one: macOS starts the new one on the next key.
pkill -x KoEnJa 2>/dev/null || true
"$app/Contents/MacOS/KoEnJa" --register
killall TextInputMenuAgent 2>/dev/null || true
cat <<'MSG'
KoEnJa is installed. If it isn't in the input menu yet: System Settings → Keyboard → Input Sources →
Edit… → + → Korean → KoEnJa (log out and back in once if it isn't listed).
한영일이 설치되었습니다. 입력 메뉴에 없으면: 시스템 설정 → 키보드 → 입력 소스 → 편집… → + → 한국어 → 한영일
(목록에 없으면 한 번 로그아웃 후 다시 로그인).
MSG
