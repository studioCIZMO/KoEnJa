#!/bin/sh
# Forgets every KoEnJa.app LaunchServices knows except the installed one. On a Mac that builds
# KoEnJa, Xcode registers each build product, archive and notarized copy under the same bundle
# identifier; imklaunchagent then may try to start one of those at login (LaunchInputMethod
# status=-50, "unrecognized InputMethodConnectionName") and 한영일 stays grey until a reboot with
# the duplicates gone. Run it after a build or archive, and before rebooting when 한영일 is grey.
set -u
keep="/Library/Input Methods/KoEnJa.app"
ls=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
"$ls" -dump 2>/dev/null | sed -n 's/^path: *\(.*KoEnJa\.app\) (0x[0-9a-f]*)$/\1/p' | sort -u |
while IFS= read -r path; do
  [ "$path" = "$keep" ] && continue
  "$ls" -u "$path" >/dev/null 2>&1 && echo "forgot $path"
done
[ -d "$keep" ] && "$ls" -f "$keep" >/dev/null 2>&1
echo "kept $keep"
