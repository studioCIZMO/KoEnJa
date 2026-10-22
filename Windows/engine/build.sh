#!/bin/sh
# Builds the engine and its golden test with the system clang++, then runs the test against
# the Swift engine's golden output. No cmake: one compiler call per file.
# -ffp-contract=off: no fused multiply-add, so float scores round as on the JVM and in Swift.
# MSVC (v143): /std:c++20 /utf-8 /fp:precise (no contraction since VS 2022; never /fp:fast or /fp:contract).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$root/.build/windows-engine"
mkdir -p "$out"
flags="-std=c++20 -O2 -Wall -ffp-contract=off"
objects=""
pids=""
for src in "$here"/src/*.cpp "$here"/test/golden_test.cpp; do
    obj="$out/$(basename "${src%.cpp}").o"
    clang++ $flags -c "$src" -o "$obj" &
    pids="$pids $!"
    objects="$objects $obj"
done
for pid in $pids; do wait "$pid"; done
clang++ $flags $objects -o "$out/golden_test"
"$out/golden_test" "$root/Packages/SeamtypeCore/Sources/SeamtypeCore/Data" "$root/Android/engine/src/test/resources"
