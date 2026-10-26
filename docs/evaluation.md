# Evaluation: KoEnJa's engine with Araso's own tools

KoEnJa's language engine is [Araso](https://github.com/nyttastudio/araso)'s `ArasoCore` (MIT) with our changes
(`Packages/SeamtypeCore`, listed in `Packages/SeamtypeCore/UPSTREAM`, marked `// REA:`). To see what those
changes do, we ran Araso's own evaluation twice on the same machine, with the same downloaded data and the same
commands: once on upstream Araso, once with Araso's engine replaced by ours. Nothing was tuned for this.

- Date: 2026-10-23
- Re-run 2026-10-26 after our engine moved to Araso 0.3.1 ([`eee3cbd`](https://github.com/nyttastudio/araso/commit/eee3cbd014ad9395bf673b7270011548f91b6f8e)), upstream 0.3.1 against KoEnJa at 0.3.1, same data and commands:
  language accuracy (Japanese off 99.77%, on 99.40%) and every AzooKey conversion number are unchanged, and the two
  engines still agree. The one difference from the tables below is macOS's engine without reranking on phrases,
  77.2% for both (67.2% at 22e7d3d). Memory was not measured again.
- Araso: [`22e7d3d`](https://github.com/nyttastudio/araso/commit/22e7d3d4a31c8dd0fb81add06be6b481d8b4927f) (0.2.2+), the commit our engine is based on
- KoEnJa engine: `Packages/SeamtypeCore/Sources/SeamtypeCore` at KoEnJa `99ba133`
- Machine: Apple M4, macOS 26.7.2 (25G330), Apple Swift 6.4 (swiftlang-6.4.0.33.1), python3 standard library only
- AzooKeyKanaKanjiConverter 0.8.5 (resolved by Araso's `Package.swift`, `.upToNextMinor(from: "0.8.0")`)

## Summary

**Every language decision in Araso's evaluation came out the same** for upstream Araso and for KoEnJa's
engine: every output line of `--decide` for all 5,998 held-out rows (Japanese on and off) and of the context eval's `--list`, and the conversion eval's whole printout (scores and the misses it lists).
Our changes in this harness are memory and on-screen behaviour, not scoring; KoEnJa's engine used about
19 MB less memory for the same work (below).

The numbers on this machine are not exactly Araso's published ones, for upstream Araso too. See
[Why the numbers differ from Araso's README](#why-the-numbers-differ-from-arasos-readme).

## Held-out accuracy (`tools/prepare.sh`)

English 2,011 words, Korean 1,987 words, Japanese 2,000 phrases, none of them used to build the models.
Japanese on: `tools/eval3.py` (offset +0.50), as `prepare.sh` runs it. Japanese off: Araso's tools don't print
it, so we ran the English and Korean rows of the same `eval.tsv` through `araso-cli --decide` (no `--japanese`)
and scored them as `eval3.py` does (`mixed` counts as Korean); the script is below.

| | Araso README (published) | Araso 22e7d3d, this machine | KoEnJa engine, this machine |
| --- | --- | --- | --- |
| Japanese off (en/ko, 3,998) | 99.82% | 99.77% (3,989) | 99.77% (3,989) |
| Japanese on (en/ko/ja, 5,998) | 99.50% | 99.40% (5,962) | 99.40% (5,962) |

Japanese on, by language (both engines):

| | → English | → Korean | → Japanese | correct |
| --- | --- | --- | --- | --- |
| English (2,011) | 1,996 | 5 | 10 | 99.25% |
| Korean (1,987) | 4 | 1,983 | 0 | 99.80% |
| Japanese (2,000) | 17 | 0 | 1,983 | 99.15% |

Japanese off: English 2,006/2,011, Korean 1,983/1,987 (both engines).

`prepare.sh` also runs `eval_overlap.py` (after Japanese: Japanese words left as English 202/300, English words
turned Japanese 1/300) and `eval_particles.py` (an English word + a Korean particle: 100.0% / 100.0% / 99.9% for
capitalized, common and tech words; + a Japanese particle: 99.5% / 94.9% / 94.1%); both engines printed the same.

## Kanji conversion top-1 (`tools/eval_convert.py`)

The top candidate exactly equal to the original Tatoeba text (400 held-out phrases of 2–3 words, 400 whole
sentences). `macOS engine` is Araso's `AppleJapaneseConverter` (macOS's own conversion engine), which KoEnJa
doesn't use. **iOS KoEnJa uses the AzooKey path** (`AzooKeyConverter` with Araso's reranking); Android and Windows
use KoEnJa's own `MozcConverter` over the Mozc dictionary, which this harness cannot run, so it isn't measured here.

| | macOS engine | + rerank | in the list (+ rerank) | AzooKey | AzooKey + rerank (iOS) | AzooKey, in the list |
| --- | --- | --- | --- | --- | --- | --- |
| Phrases, published | 74.5% | 81.0% | 96.2% | — | 81.0% | — |
| Phrases, Araso 22e7d3d | 67.2% | 83.8% | 98.2% | 70.5% | 79.8% | 94.8% |
| Phrases, KoEnJa engine | 67.2% | 83.8% | 98.2% | 70.5% | 79.8% | 94.8% |
| Sentences, published | 71.2% | 74.0% | 91.5% | — | 67.2% | — |
| Sentences, Araso 22e7d3d | 73.0% | 72.0% | 91.5% | 55.8% | 68.5% | 84.2% |
| Sentences, KoEnJa engine | 73.0% | 72.0% | 91.5% | 55.8% | 68.5% | 84.2% |

An exact match is required, so a kana/kanji spelling difference counts as wrong. The only change we made to the
converter (`nextWords()` for the iOS candidate bar) isn't used in this measurement.

## Common words after another language (`tools/eval_context.py`, Japanese on)

The 10,000 most frequent English and Korean words, wrong within the top 1,000 / top 10,000.

| | after nothing | after the same language | after the other language | after Japanese |
| --- | --- | --- | --- | --- |
| Korean words, published | 9 / 33 | 2 / 15 | 14 / 63 | 9 / 34 |
| Korean words, both engines here | 9 / 33 | 2 / 15 | 13 / 62 | 9 / 34 |
| English words, published | 2 / 47 | 0 / 21 | 8 / 71 | 2 / 86 |
| English words, both engines here | 2 / 48 | 0 / 23 | 8 / 71 | 2 / 86 |

Total wrong: 46 / 370 published, 45 / 372 here for both engines. Araso notes this list was used to tune the
rules, so it is a reference score. `--list` output (the words themselves) was identical too.

## Why the results are the same

The evaluation calls `araso-cli --decide`, which runs `HeuristicEngine` through `SplittingEngine`, one word at a
time. Our changes there:

- Korean vocabulary as sorted 64-bit hashes, and English `stemLevels` holding only the words that differ: memory
  only, the same answers (none of the 5,998 rows changed).
- `HeuristicEngine`'s capitalized-word floor and a capitalized English word leading into Japanese
  (`iPhonedetesuto`): only for keys with capitals; no row here flipped.
- Everything else (Space/Return commit what was shown, the short-prefix rule, `isWeakFlipAllowed`'s runner-up
  rule, a particle after an address, dotfiles in URL runs, iOS candidate bar APIs) is in `InputSession` or the
  converter, which `--decide` and `--convert-all` don't go through.

### Supplementary: through `InputSession` (not one of Araso's published evaluations)

To see the `InputSession` changes, we typed each `eval.tsv` row plus a Space into a fresh session with
`araso-cli --type --japanese` (no previous word, no converter) and scored the field: English as typed, Korean as
the Hangul reading, Japanese as the kana reading.

| | English (2,011) | Korean (1,987) | Japanese (2,000) | total (5,998) |
| --- | --- | --- | --- | --- |
| Araso 22e7d3d | 1,962 | 1,981 | 1,974 | 5,917 (98.65%) |
| KoEnJa engine | 1,967 | 1,980 | 1,972 | 5,919 (98.68%) |

13 rows differ. KoEnJa is right where Araso isn't on 6 (English `gus`, `ti`, `nein`, `ie`, `rai`, `aya` stay
letters: the short-prefix rule and "Space commits what was shown"); Araso is right where KoEnJa isn't on 4
(English `dui`, Korean `qur`, Japanese `inotte` and `woforo-shi`); 3 are wrong in both (Korean `zoa` and `gus`,
Japanese `nan'ni`).

| label | keys | Araso | KoEnJa |
| --- | --- | --- | --- |
| English | gus | ぐs | **gus** |
| English | ti | ち | **ti** |
| English | nein | ねいん | **nein** |
| English | ie | いえ | **ie** |
| English | rai | らい | **rai** |
| English | aya | あや | **aya** |
| English | dui | **dui** | づい |
| Korean | qur | **벽** | qur |
| Japanese | inotte | **いのって** | inotte |
| Japanese | woforo-shi | **をふぉろーし** | 재래개-し |
| Korean | zoa | ぞあ | zoa |
| Korean | gus | ぐs | gus |
| Japanese | nan'ni | nan'に | nan'ni |

(Bold: correct.) `woforo-shi` → `재래개-し` is a KoEnJa regression we found here and have not fixed yet.

## Memory

Peak memory of `araso-cli --decide --japanese 0.5` over the first 300 rows of `eval.tsv` (`/usr/bin/time -l`,
three runs each, the same every run):

| | maximum resident set size | peak memory footprint |
| --- | --- | --- |
| Araso 22e7d3d | 66.5 MB | 58.0 MB |
| KoEnJa engine | 47.7 MB | 39.2 MB |

This is the Korean vocabulary as hashes and the English stem levels, which we changed for the iOS keyboard's
memory limit. It is the command-line tool on macOS, not the keyboard on a phone.

## KoEnJa's own checks

The Swift engine (iOS), the Kotlin port (Android) and the C++20 port (Windows) type the same 942 key sequences
under two language orders (`Android/engine/src/test/resources/golden_input.txt`) and must produce exactly
`golden.tsv`, line for line. On 2026-10-23, on `99ba133`:

| | how | result |
| --- | --- | --- |
| Swift (`Packages/SeamtypeCore`) | `Android/tools/golden` regenerated `golden.tsv`, `diff` | identical, 1,884 lines |
| Kotlin (`Android/engine`) | `./gradlew :engine:test --tests '*GoldenTest*'` | 1884/1884 |
| C++, clang (macOS) | `bash Windows/engine/build.sh` | 1884/1884; replay 190.6 ms for 20,468 keys (9.3 µs/key, Apple M4) |
| C++, MSVC (CI) | `.github/workflows/windows.yml`, GitHub `windows-2022` runner | 1884/1884; replay 525–534 ms for 20,468 keys (25.7–26.1 µs/key) in the last three runs |

Replay time is the whole session (decisions, composing, the text field) per key, models already loaded.

## Why the numbers differ from Araso's README

Upstream Araso and KoEnJa's engine gave the same results, so the differences from the published numbers aren't
from our changes. Likely causes, which we didn't separate:

- The held-out Japanese phrases and conversion samples come from Tatoeba's current export, downloaded on the
  day; Tatoeba grows daily, so the samples are not the ones Araso's author used.
- Araso's README numbers were last updated at `a356c3d` (2026-10-16); the engine changed after that, up to 22e7d3d.
- The macOS conversion engine is the one in this Mac's macOS (26.7.2).

## How to reproduce

```sh
git clone https://github.com/nyttastudio/araso araso-up && git -C araso-up checkout 22e7d3d
git clone https://github.com/nyttastudio/araso araso-rea && git -C araso-rea checkout 22e7d3d

# KoEnJa's engine in place of Araso's, keeping the target name ArasoCore so araso-cli builds.
rm -rf araso-rea/Sources/ArasoCore
cp -R KoEnJa/Packages/SeamtypeCore/Sources/SeamtypeCore araso-rea/Sources/ArasoCore
sed -i '' 's#Sources/SeamtypeCore/#Sources/ArasoCore/#g' araso-rea/Sources/ArasoCore/Resources.swift

(cd araso-up && tools/prepare.sh)          # downloads into tools/data, builds araso-cli, runs the evals
cp -R araso-up/tools/data araso-rea/tools/  # the same data for both
(cd araso-rea && tools/prepare.sh)

for d in araso-up araso-rea; do (cd $d/tools
  ARASO_ENGINE=apple python3 eval_convert.py ../.build/release/araso-cli data/ja_tokens.jsonl 400 0 none,
  python3 eval_convert.py ../.build/release/araso-cli data/ja_tokens.jsonl 400 0 none,
  python3 eval_context.py ../.build/release/araso-cli data/ko_50k.txt data/en_50k.txt 10000 --list); done
```

The only change needed to build our engine in Araso's package was the development data path in
`Resources.swift` (`Sources/SeamtypeCore/Data` → `Sources/ArasoCore/Data`); the data files are byte-identical to
Araso's. In `eval_convert.py`, the weight `none` is without reranking and the empty weight is the default.

Japanese off (our script, python3 standard library):

```python
# ja_off.py <araso-cli> <eval.tsv>
import subprocess, sys
from collections import Counter
cli, path = sys.argv[1:]
rows = [l.rstrip("\n").split("\t") for l in open(path, encoding="utf-8")]
rows = [r for r in rows if r[2] in ("english", "korean")]
out = subprocess.run([cli, "--decide"], input="".join(f"{r[0]}\t{r[1]}\n" for r in rows),
                     capture_output=True, text=True, check=True).stdout.splitlines()
c, n = Counter(), Counter()
for r, o in zip(rows, out):
    got = o.split("\t")[1]
    got = "korean" if got == "mixed" else got
    n[r[2]] += 1
    c[r[2]] += got == r[2]
print(f"ja off: {sum(c.values())}/{sum(n.values())}", dict(c), dict(n))
```

Line-for-line comparison of the two engines:

```sh
for v in up rea; do
  cut -f1,2 araso-up/tools/data/eval.tsv | araso-$v/.build/release/araso-cli --decide --japanese 0.5 > $v-on.out
  cut -f1,2 araso-up/tools/data/eval.tsv | araso-$v/.build/release/araso-cli --decide > $v-off.out
  cut -f1 araso-up/tools/data/eval.tsv | araso-$v/.build/release/araso-cli --type --japanese > $v-type.out
done
diff up-on.out rea-on.out; diff up-off.out rea-off.out; diff up-type.out rea-type.out
head -300 araso-up/tools/data/eval.tsv | cut -f1,2 > first300.tsv
/usr/bin/time -l araso-up/.build/release/araso-cli --decide --japanese 0.5 < first300.tsv > /dev/null
```

Data used (SHA-256, first 16 hex digits): `eval.tsv` d280d1163ac1649b (5,998 rows, the same file for both),
`jpn_sentences.tsv` 2a59a095d5884bf1, `kor_sentences.tsv` 2467e8648aa23db1, `ko_50k.txt` 8d00401b6728c8d6,
`en_50k.txt` 5351ff405b1126ef.
