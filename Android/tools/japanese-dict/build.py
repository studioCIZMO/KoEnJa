#!/usr/bin/env python3
"""Builds japanese/src/main/resources/mozc.dic.gz (read by MozcConverter) from Mozc's OSS dictionary.

Usage (from Android/): python3 tools/japanese-dict/build.py [download-dir]

Layout, big-endian, gzipped:
  int N; short[N*N] connection cost (rid * N + lid); byte[N] 1 = particle/auxiliary (助詞, 助動詞)
  int R, E, readingChars, surfaceChars
  char[readingChars]; int[R+1] reading starts; int[R+1] entry starts   (readings sorted by UTF-16 units)
  short[E] lid; short[E] rid; short[E] cost; int[E+1] surface starts; char[surfaceChars]
A surface of length 0 means the surface is the reading itself.
"""
import array, gzip, os, sys, tempfile, urllib.request

COMMIT = "0136c35782536cb43d710575c311cd4185982738"  # google/mozc master, 2026-10-16
BASE = f"https://raw.githubusercontent.com/google/mozc/{COMMIT}/"
DICT = "src/data/dictionary_oss/"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "../../japanese/src/main/resources")


def fetch(path, cache):
    local = os.path.join(cache, path.replace("/", "_"))
    if not os.path.exists(local):
        urllib.request.urlretrieve(BASE + path, local)
    return open(local, encoding="utf-8")


def write(out, typecode, values):
    a = array.array(typecode, values)
    if sys.byteorder == "little":
        a.byteswap()
    out.write(a.tobytes())


def chars(s):
    return s.encode("utf-16-be")


def main():
    cache = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp()
    with fetch(DICT + "connection_single_column.txt", cache) as f:
        n = int(next(f))
        matrix = [int(line) for line in f]
    assert len(matrix) == n * n
    with fetch(DICT + "id.def", cache) as f:
        pos = [line.split(" ", 1)[1] for line in f]
    functional = [1 if p.startswith(("助詞", "助動詞")) else 0 for p in pos]
    assert len(functional) == n

    entries = {}  # reading -> [(lid, rid, cost, surface)]
    for i in range(10):
        with fetch(f"{DICT}dictionary{i:02d}.txt", cache) as f:
            for line in f:
                reading, lid, rid, cost, surface = line.rstrip("\n").split("\t")[:5]
                # 化, 視, 式… are cheap enough to beat a following particle (正解か → 正解化): +500.
                cost = int(cost) + (500 if pos[int(lid)].startswith("名詞,接尾,サ変接続") else 0)
                entries.setdefault(reading, []).append(
                    (int(lid), int(rid), cost, "" if surface == reading else surface))
    readings = sorted(entries, key=chars)

    reading_starts, entry_starts, lids, rids, costs, surface_starts = [0], [0], [], [], [], [0]
    reading_chars, surface_chars = bytearray(), bytearray()
    for r in readings:
        reading_chars += chars(r)
        reading_starts.append(len(reading_chars) // 2)
        for lid, rid, cost, surface in sorted(entries[r], key=lambda e: e[2]):
            lids.append(lid); rids.append(rid); costs.append(cost)
            surface_chars += chars(surface)
            surface_starts.append(len(surface_chars) // 2)
        entry_starts.append(len(lids))

    os.makedirs(OUT, exist_ok=True)
    with gzip.GzipFile(os.path.join(OUT, "mozc.dic.gz"), "wb", 9, mtime=0) as out:
        write(out, "i", [n]); write(out, "h", matrix); out.write(bytes(functional))
        write(out, "i", [len(readings), len(lids), len(reading_chars) // 2, len(surface_chars) // 2])
        out.write(reading_chars); write(out, "i", reading_starts); write(out, "i", entry_starts)
        write(out, "h", lids); write(out, "h", rids); write(out, "h", costs)
        write(out, "i", surface_starts); out.write(surface_chars)

    with open(os.path.join(OUT, "mozc.dic.LICENSE.txt"), "w", encoding="utf-8") as out:
        out.write(f"mozc.dic.gz is built from google/mozc {DICT} at {COMMIT}.\n\n")
        with fetch("LICENSE", cache) as f:
            out.write(f.read())
        out.write("\n\n" + "=" * 79 + f"\n{DICT}README.txt\n\n")
        with fetch(DICT + "README.txt", cache) as f:
            out.write(f.read())


if __name__ == "__main__":
    main()
