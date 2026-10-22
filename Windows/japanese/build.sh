#!/bin/sh
# Builds the converter and its test with the system clang++, unpacks the dictionary once, runs the test.
# mozc.dic is mapped, not read: the unpacked file (~53 MB) is what the Windows data folder ships.
# MSVC (v143): cl /std:c++20 /utf-8 /fp:precise /O2 /EHsc MozcConverter.cpp test\mozc_converter_test.cpp psapi.lib
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$root/.build/windows-japanese"
gz="$root/Android/japanese/src/main/resources/mozc.dic.gz" # built by Android/tools/japanese-dict/build.py; license beside it
mkdir -p "$out"
if [ ! -f "$out/mozc.dic" ] || [ "$gz" -nt "$out/mozc.dic" ]; then
    gunzip -c "$gz" > "$out/mozc.dic.tmp" && mv "$out/mozc.dic.tmp" "$out/mozc.dic"
fi
clang++ -std=c++20 -O2 -Wall -ffp-contract=off "$here/MozcConverter.cpp" "$here/test/mozc_converter_test.cpp" -o "$out/mozc_converter_test"
"$out/mozc_converter_test" "$out/mozc.dic" "$@"
