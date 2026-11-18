#!/bin/sh
# Installs or updates KoEnJa for Mac into /Library/Input Methods (from Araso's scripts/get.sh, MIT):
#   curl -fsSL https://raw.githubusercontent.com/studioCIZMO/KoEnJa/main/Mac/get.sh | sh
#   … | sh -s -- --uninstall [--delete-data]
# KOENJA_ZIP=<path> installs a local zip instead of the latest release's.
# The bundle folder is made once by an administrator (sudo asks for the password) and owned by
# you, so updates (this script, the input method's own updater) replace its Contents without one.
# Run it as yourself, not with sudo: the input source is registered for the user running it.
set -eu
url="https://github.com/studioCIZMO/KoEnJa/releases/latest/download/KoEnJa-Mac.zip"
dest="/Library/Input Methods"
app="$dest/KoEnJa.app"
legacy="$HOME/Library/Input Methods/KoEnJa.app"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ "${1:-}" = --uninstall ]; then
  for copy in "$app" "$legacy"; do
    [ -d "$copy" ] || continue
    "$copy/Contents/MacOS/KoEnJa" --uninstall ${2:-}
    exit 0
  done
  echo "KoEnJa isn't installed."; exit 0
fi
[ "$(id -u)" != 0 ] || { echo "Run get.sh as yourself, not with sudo; it asks for the password when it needs it." >&2; exit 1; }

if [ -n "${KOENJA_ZIP:-}" ]; then cp "$KOENJA_ZIP" "$work/KoEnJa.zip"
else echo "Downloading KoEnJa…"; curl -fsSL -o "$work/KoEnJa.zip" "$url"; fi
ditto -x -k "$work/KoEnJa.zip" "$work"
[ -d "$work/KoEnJa.app" ] || { echo "The download has no KoEnJa.app." >&2; exit 1; }
codesign --verify --deep --strict "$work/KoEnJa.app" 2>/dev/null &&
  codesign -dv "$work/KoEnJa.app" 2>&1 | grep -qx 'TeamIdentifier=N723QM4R64' &&
  spctl --assess --type execute "$work/KoEnJa.app" 2>/dev/null ||
  { echo "The downloaded KoEnJa.app isn't signed by CIZMO and notarized; nothing was installed." >&2; exit 1; }
xattr -dr com.apple.quarantine "$work/KoEnJa.app" 2>/dev/null || true

# The bundle folder in /Library: made once (or the ~/Library copy of earlier versions moved there)
# by an administrator and owned by you. One sudo; only the updater's run is without a terminal.
if [ ! -d "$app" ]; then
  if [ -d "$legacy" ]; then
    echo "Moving KoEnJa from ~/Library/Input Methods to $dest (administrator password)."
    sudo sh -c 'mkdir -p "$1" && mv "$2" "$3" && chown -R "$4" "$3"' sh "$dest" "$legacy" "$app" "$(id -un)"
  else
    echo "Making $app (administrator password)."
    sudo sh -c 'mkdir -p "$1" && mkdir "$2" && chown "$3" "$2"' sh "$dest" "$app" "$(id -un)"
  fi
fi
# macOS lets only the owner, with App Management for the terminal, change an app in /Library.
[ -w "$app" ] && mkdir "$app/.probe.$$" 2>/dev/null && rmdir "$app/.probe.$$" ||
  { echo "$app can't be changed from here. If it is yours, allow this terminal under System Settings → Privacy & Security → App Management and run this again; if not, remove it as an administrator first." >&2; exit 1; }

# One install at a time (the input method and the settings window can both start one).
lock="$app/.install"
find "$app" -maxdepth 1 -name '.install' -mmin +10 -exec rm -rf {} + 2>/dev/null || true  # one left by a crash
mkdir "$lock" 2>/dev/null || { echo "Another KoEnJa install is running." >&2; exit 1; }
# The old Contents stays until the new one is in place, and comes back if the move fails or
# this script is stopped between the two moves.
old=""; staged=""
trap '[ -n "$old" ] && [ ! -d "$app/Contents" ] && mv "$old" "$app/Contents"; rm -rf "$work" "$lock" "$staged"' EXIT
trap 'exit 1' HUP INT TERM
# Staging left by a crash: its Contents.old is the install if the bundle has none.
for left in "$app"/.new.*; do
  [ -d "$left" ] || continue
  [ -d "$app/Contents" ] || { [ -d "$left/Contents.old" ] && mv "$left/Contents.old" "$app/Contents"; true; }
  rm -rf "$left"
done
staged=$(mktemp -d "$app/.new.XXXXXX")
ditto "$work/KoEnJa.app/Contents" "$staged/Contents"
if [ -d "$app/Contents" ]; then old="$staged/Contents.old"; mv "$app/Contents" "$old"; fi
mv "$staged/Contents" "$app/Contents" || exit 1
# A copy of earlier versions left in ~/Library would be a second input method to macOS.
if [ -d "$legacy" ]; then rm -rf "$legacy" || echo "Couldn't remove the old copy $legacy; remove it yourself." >&2; fi
# The running copy is the old one: macOS starts the new one on the next key.
pkill -x KoEnJa 2>/dev/null || true
"$app/Contents/MacOS/KoEnJa" --register ||
  echo "KoEnJa is installed but couldn't register; log out and back in once." >&2
killall TextInputMenuAgent 2>/dev/null || true
cat <<'MSG'
KoEnJa is installed. If it isn't in the input menu yet: System Settings → Keyboard → Input Sources →
Edit… → + → Korean → KoEnJa (log out and back in once if it isn't listed). If it is selected but
typing does nothing, remove it there (−) and add it again (+), or log out and back in once.
한영일이 설치되었습니다. 입력 메뉴에 없으면: 시스템 설정 → 키보드 → 입력 소스 → 편집… → + → 한국어 → 한영일
(목록에 없으면 한 번 로그아웃 후 다시 로그인). 골랐는데 글자가 안 들어가면 거기서 한영일을 빼고(−) 다시
추가하거나(+), 한 번 로그아웃 후 다시 로그인하세요.
MSG
