<p align="center">
  <img src="iOS/App/Assets.xcassets/AppIcon.appiconset/icon.png" width="120" alt="KoEnJa icon">
</p>

<h1 align="center">KoEnJa</h1>

<p align="center"><b>한 자판으로 한·영·일 · 一つの配列で韓・英・日 · Korean, English, Japanese on one keyboard</b></p>

<p align="center">KoEnJa (한영일 · 韓英日) is a keyboard for typing Korean, English and Japanese without switching languages.<br>
It comes as an iOS keyboard, an Android keyboard and a Windows input method.</p>

<p align="center">
  <a href="https://github.com/studioCIZMO/KoEnJa/releases/latest"><img src="https://img.shields.io/github/v/release/studioCIZMO/KoEnJa?style=flat-square&label=release" alt="Latest release"></a>
  <a href="https://github.com/studioCIZMO/KoEnJa/actions/workflows/windows.yml"><img src="https://img.shields.io/github/actions/workflow/status/studioCIZMO/KoEnJa/windows.yml?branch=main&style=flat-square&label=windows" alt="Windows build and golden test"></a>
  <img src="https://img.shields.io/badge/Windows%2010%2F11-x64-555?style=flat-square" alt="Windows 10/11 x64">
  <img src="https://img.shields.io/badge/iOS-16%2B-555?style=flat-square" alt="iOS 16 or later">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue?style=flat-square" alt="MIT License"></a>
</p>

<p align="center"><a href="https://cizmo.net/KoEnJa/en">Website</a> · <a href="README.md">한국어</a> · <a href="README.ja.md">日本語</a> · English</p>

<h3 align="center">No more 한/영 key, no more switching to Japanese.</h3>

<p align="center">
Type everything on the QWERTY layout.<br>
KoEnJa tells, <b>word by word</b>, whether you're typing English, Korean (Dubeolsik) or Japanese (romaji), and puts in the right letters.
</p>

```
dhsmf ghldmldptj GitHub PR flqbfmf Rmxsorh mergegoTdjdy.  →  오늘 회의에서 GitHub PR 리뷰를 끝내고 merge했어요.
Please run the tests again before you push.               →  Please run the tests again before you push.
iPhonedetesutoshimashita⏎                                 →  iPhoneでてすとしました
kinouemailwookutta⏎                                       →  きのうemailをおくった
```

<sub>These examples are taken as they are from the engine's golden test (`Android/engine/src/test/resources/golden.tsv`). ⏎ (Return) commits the kana as typed; Space converts to kanji.</sub>

<p align="center">
  <a href="https://github.com/studioCIZMO/KoEnJa/releases/latest"><img src="https://img.shields.io/badge/Download%20for%20Windows-0078d4?style=for-the-badge&logo=windows&logoColor=white" alt="Download for Windows"></a>
</p>

## At a glance

- **Word by word**: it picks English, Korean or Japanese for each word, also when the language changes mid-sentence.
- **Mixed words too**: `githubdp` → `github에`, `googlede` → `googleで`, `beautywo` → `beautyを`
- **One key when it's wrong**: the switch key (the `한あA` key on iOS) puts the word in its next language. It also works right after Space committed it (`go` → `해`).
- **Language priority**: choose whether an ambiguous word goes Korean or Japanese, 한국어 우선 or 日本語優先.
- **Kanji conversion**: on Windows it works like Microsoft IME, with Space and 変換, F6–F10, and segments with Shift+←/→.
- **Three platforms, the same decisions**: the Swift (iOS), Kotlin (Android) and C++ (Windows) engines pass the same 1,884-line golden test without a single line different.
- **On the device only**: nothing you type leaves it.
- **99.4% accurate**: measured with Araso's own evaluation tools on 5,998 held-out words and phrases, Japanese on ([evaluation](#evaluation)).

## Contents

- [Why we made it](#why-we-made-it)
- [Install](#install): [Windows](#windows) · [iOS](#ios) · [Android](#android)
- [Keys](#keys)
- [How it decides](#how-it-decides) · [Evaluation](#evaluation)
- [Known limitations](#known-limitations)
- [Development](#development)
- [Privacy](#privacy)
- [KoEnJa and Araso](#koenja-and-araso)
- [Report a problem](#report-a-problem) · [Code signing policy](#code-signing-policy) · [License](#license)

## Why we made it

If you write in Korean, English and Japanese every day, you have three input sources, and you keep pressing the
switch key or typing in the wrong language. On macOS, [Araso](https://github.com/nyttastudio/araso) solved this by
telling each word's language as you type. KoEnJa uses that engine under the MIT License and is a separate product
that brings the same way of typing to iPhone, Android and Windows, where Araso doesn't run.

## Install

### Windows

1. Download `KoEnJa-Setup-0.1.0.exe` from [Releases](https://github.com/studioCIZMO/KoEnJa/releases/latest) and run it.
2. 0.1.0 isn't code-signed yet, so SmartScreen shows **Windows protected your PC**. Click **More info** →
   **Run anyway**, then **Yes** at the administrator prompt.
3. Leave **Open KoEnJa settings** checked on the last page and finish: pick your keyboard, your languages and the
   switch key. You can change them later in **KoEnJa 설정** in the Start menu.
4. Pick **KoEnJa** with **Win+Space**, or click the language indicator (ENG/한/あ) on the taskbar.

For 64-bit Windows 10/11 (x64) only; ARM64 Windows isn't supported yet. KoEnJa is added to your Korean and Japanese
input lists (to Korean if you have neither).

<details>
<summary>Uninstall</summary>

Settings → Apps → Installed apps → **KoEnJa** → Uninstall. It leaves your input lists too. Files in use by an open
app are removed at the next restart. Your settings and learned words (`%LOCALAPPDATA%\KoEnJa`) are kept; delete
that folder if you don't need them.

</details>

### iOS

In App Store review. We'll announce it here and on the [website](https://cizmo.net/KoEnJa/en).
After installing, choose your languages and their order in the app, then add the KoEnJa keyboard in Settings →
General → Keyboard. **Allow Full Access** lets the keyboard read the language order and settings chosen in the app
and use haptics ([Privacy](#privacy)).

### Android

Coming to Google Play. Until then, install `KoEnJa-Android-0.2.1.apk` (Android 8.0+) from the [release](https://github.com/studioCIZMO/KoEnJa/releases/latest).
After installing, turn on 한영일 with 키보드 설정 열기 (open keyboard settings) in the app and pick it with 입력 방법 고르기 (choose input method); the app's screens are in Korean only for now.
The Play version is signed by Google, so to move to it later, uninstall the APK first. The source is in `Android/`.

## Keys

### Windows

| Key | What it does |
| --- | --- |
| Switch key (while typing) | Puts the word being typed in its next language. |
| Switch key (nothing being typed) | With all three languages, switches which goes first (**한국어 우선 ↔ 日本語優先**). With only 한국어 + English or 日本語 + English, turns KoEnJa off and on (off is plain English). |
| Space · 変換 | Converts Japanese to kanji. Press again to open the candidate list. |
| Space · Tab · ↓ / Shift+Space · ↑ | Next / previous candidate in the list; PageDown and PageUp by pages. |
| 1–9 | Picks a candidate from the list. |
| Return | Commits everything. Before converting, commits the kana as typed. |
| Esc · Backspace | Cancels the conversion, back to kana. |
| Shift+← / Shift+→ | Makes the first segment shorter or longer. → commits the first segment and converts the rest. |
| F6 · F7 · 無変換 | Hiragana · katakana · flip between them |
| F8 · F9 · F10 | Half-width katakana · full-width romaji · half-width romaji |
| 半角/全角 (Japanese keyboard) | KoEnJa off and on |

**The default switch key** is right Alt tapped alone (= 한/영) on Korean and US keyboards, and 無変換 on Japanese
(JIS) keyboards. In KoEnJa Settings you can choose left Ctrl tapped alone, 한/영, 変換, 無変換, Ctrl+Shift,
Shift+Space or Ctrl+Space instead. For Ctrl+Shift, turn off Windows' Ctrl+Shift keyboard layout hotkey
(Settings > Time & language > Typing > Advanced keyboard settings > Input language hot keys).

### iOS

- **The `한あA` key** puts the current word in its next language. Right after Space, it changes the word just committed.
- You choose your languages and their order in the app. The first one takes an ambiguous word.
- While you type Japanese, the candidate bar shows conversions; after a commit it shows what usually comes next.

## How it decides

KoEnJa's decisions come from [Araso](https://github.com/nyttastudio/araso)'s engine; see
[how Araso decides](https://github.com/nyttastudio/araso/blob/main/README.en.md) for the details. In short:

- The same keys are read as English, Korean (Dubeolsik) and Japanese (romaji → kana), and each reading is scored:
  letter trigrams and the SCOWL word list for English, syllable frequencies and word lists for Korean, kana
  trigrams for Japanese.
- Rules (capitals, digits, URLs and paths) and the previous word's language count too, including when you switch
  language from one word to the next.
- Mixed words are split: an English word + a Korean particle (`githubdp` → `github에`), an English word + Japanese
  (`googlede` → `googleで`).
- The language you put first gets a small edge on ambiguous words.

KoEnJa's changes are listed in `Packages/SeamtypeCore/UPSTREAM` and marked `// REA:` in the code: the Korean
vocabulary kept as hashes to save memory, committing exactly what the screen shows, Japanese after a capitalized
English word, a particle after an address, and dotfiles in URLs.

## Evaluation

We ran Araso's own evaluation tools (`tools/prepare.sh`, `eval_convert.py`, `eval_context.py`) unchanged on upstream
Araso (22e7d3d) and on KoEnJa's engine, side by side on the same Mac with the same data (2026-10-23, Apple M4,
macOS 26.7.2). All tables, commands and data hashes are in [docs/evaluation.md](docs/evaluation.md). After moving
the engine to Araso 0.3.1 (eee3cbd, 2026-10-26) we ran it again the same way: the language and kanji numbers below
came out the same for Araso 0.3.1 and for KoEnJa.

**Held-out data** (2,011 English words, 1,987 Korean words, 2,000 Japanese phrases)

| | Araso README (published) | Araso 22e7d3d (this Mac) | KoEnJa engine (this Mac) |
| --- | --- | --- | --- |
| Japanese off (en/ko) | 99.82% | 99.77% | 99.77% |
| Japanese on (en/ko/ja) | 99.50% | 99.40% | 99.40% |

The two engines gave the same answer on all 5,998 items. With Japanese on: English 99.25%, Korean 99.80%,
Japanese 99.15%.

**Kanji conversion** (the top candidate exactly equal to the original text, 400 held-out Tatoeba items each)

| | AzooKey + reranking (the path iOS uses) | correct one in the list |
| --- | --- | --- |
| Phrases of 2–3 words | 79.8% | 94.8% |
| Whole sentences | 68.5% | 84.2% |

The same for upstream Araso and KoEnJa's engine. Android and Windows use our own converter over the Mozc
dictionary, which this tool can't run, so they aren't measured here.

**Common words in a sentence** (`eval_context.py`: the 10,000 most frequent English and Korean words after each
language; wrong within the top 1,000 / top 10,000)

| | after nothing | after the same language | after the other language | after Japanese |
| --- | --- | --- | --- | --- |
| Korean words | 9 / 33 | 2 / 15 | 13 / 62 | 9 / 34 |
| English words | 2 / 48 | 0 / 23 | 8 / 71 | 2 / 86 |

Both engines gave the same. The small differences from Araso's published numbers appear for upstream Araso too;
they likely come from Tatoeba data downloaded on the day and from engine changes made after the published numbers.

**Where KoEnJa's engine differs**

- Memory: peak memory for the same decisions went from 58.0 MB to 39.2 MB (about 19 MB less; `araso-cli` on macOS).
- The evaluation above decides one word at a time, so it doesn't go through KoEnJa's typing-session changes.
  Typing the same 5,998 items through a session (`araso-cli --type`, for reference) changed 13 of them: KoEnJa got
  6 more right and 4 more wrong.
  [Details](docs/evaluation.md#supplementary-through-inputsession-not-one-of-arasos-published-evaluations)

**Are the three engines the same?** (golden test: 942 key sequences × 2 language orders)

| Engine | Result |
| --- | --- |
| Swift (iOS) | `golden.tsv` regenerated: all 1,884 lines identical |
| Kotlin (Android) | 1884/1884 |
| C++, clang (macOS) | 1884/1884, 9.3 µs per key (Apple M4) |
| C++, MSVC (CI, GitHub windows-2022) | 1884/1884, 25.7–26.1 µs per key (last three runs) |

The MSVC golden test runs in [CI](.github/workflows/windows.yml) on every push to `main`.

## Known limitations

- Windows 0.1.0 is not code-signed ([Code signing policy](#code-signing-policy)). It's x64 only; ARM64 Windows isn't supported.
- iOS is in App Store review; Android isn't on Google Play yet, only as an APK. The Android app's screens are in Korean only for now.
- Keys that are words in two languages (`go`/해, `so`/내) can come out wrong; fix them with the switch key.
- An English word with a Japanese particle can be read as Japanese (`sitede` → `してで`, `tokenwo` → `とけんを`).
- The top kanji candidate matches the original in about 4 of 5 phrases and 2 of 3 sentences (the iOS path); pick the others from the list.
- A regression found during the evaluation: in a typing session `woforo-shi` comes out as `재래개-し` (upstream Araso: `をふぉろーし`). Not fixed yet.

## Development

- Engine: `cd Packages/SeamtypeCore && swift test`
- iOS: `cd iOS && xcodegen && open REA.xcodeproj`
- Android: `cd Android && ./gradlew :engine:test :app:assembleDebug`
- Windows engine on macOS: `bash Windows/engine/build.sh`; on Windows (VS 2022 Build Tools): `Windows\installer\build.cmd`

| Path | What's there |
| --- | --- |
| `Packages/SeamtypeCore` | The language engine (Swift: Araso's `ArasoCore` with KoEnJa's changes) |
| `iOS/` | The iOS app and keyboard |
| `Android/engine`, `Android/japanese`, `Android/app` | The Kotlin engine, the Mozc-dictionary converter, the keyboard |
| `Windows/engine`, `Windows/japanese`, `Windows/tip`, `Windows/installer` | The C++20 engine, the converter, the TSF input method, the installer |
| `Android/engine/src/test/resources/golden.tsv` | The golden output all three engines must match (made by the Swift engine with `Android/tools/golden`) |

An engine change goes into Swift, Kotlin and C++ together, and the golden file is regenerated. CI
(`.github/workflows/windows.yml`) runs the golden test with MSVC, builds the installer, and publishes a release on a
`v*` tag.

## Privacy

What you type is processed on the device and never sent anywhere. The keyboards and the input method have no
networking. On Windows, **Report a problem…** opens a GitHub page in your browser only when you click it. The iOS
app's privacy policy is in [docs/privacy-policy.md](docs/privacy-policy.md).

## KoEnJa and Araso

KoEnJa uses the **engine** of [Araso](https://github.com/nyttastudio/araso) (© aimdevlee, MIT License). Araso is a macOS input method. The engine is the part of it that decides which language a word is in. KoEnJa is not a copy of Araso: Araso does not run on iOS, Android or Windows, and KoEnJa's apps, keyboards and input methods were written for this project.

| Part | Where it comes from |
|---|---|
| Language engine, iOS (`Packages/SeamtypeCore`) | Araso's engine source (`ArasoCore`, 0.4.1), used under the MIT License, with our changes. They are marked `// REA:` and listed in `Packages/SeamtypeCore/UPSTREAM`. They cover the memory use of the Korean vocabulary, committing exactly what the screen shows, and mixed-language rules. |
| Language engine, Android (`Android/engine`, Kotlin) and Windows (`Windows/engine`, C++20) | Written for KoEnJa to follow Araso's engine, then checked against it. The Swift, Kotlin and C++ engines replay the same golden file (`Android/engine/src/test/resources/golden.tsv`) and must match line for line. |
| iOS keyboard and app (`iOS/`) | Written for KoEnJa: the key layout, candidate bar, switch key and settings. |
| Android keyboard (`Android/app`, `Android/japanese`) | Written for KoEnJa, including a Japanese converter over the Mozc dictionary. |
| Windows input method (`Windows/tip`, `Windows/japanese`, `Windows/installer`) | Written for KoEnJa: the TSF text service, MS-IME-style conversion over the Mozc dictionary, keyboard types, switch key, language priority, settings and installer. |

Araso's macOS input method, its app and its installer are not part of KoEnJa. We are grateful to Araso's author for publishing the engine under the MIT License, which made this project possible. The engine's original copyright notice is kept in `THIRD_PARTY_NOTICES.txt` (and in the iOS app's license screen).

## Report a problem

[Open an issue](https://github.com/studioCIZMO/KoEnJa/issues/new?template=bug.yml). On Windows, **Report a problem…** in KoEnJa Settings opens the same form with your version, Windows build and settings filled in (never what you typed). Issues are public, so leave out private text.

## Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org). (0.1.0 is unsigned; signing starts once SignPath Foundation accepts the project.)

- Committers and reviewers: [studioCIZMO](https://github.com/studioCIZMO)
- Approvers: [studioCIZMO](https://github.com/studioCIZMO)

Only the Windows installer and the files it installs are signed. Each one is built by GitHub Actions from this repository's source (`.github/workflows/windows.yml`), and every release is approved by hand before signing.

Privacy: this program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it. *Report a problem…* opens a GitHub page in your browser only when you click it.

## License

MIT — see `LICENSE`. Third-party code and data: `THIRD_PARTY_NOTICES.txt` (Araso MIT, Mozc dictionary, SCOWL, Tatoeba, FrequencyWords and others); the iOS app adds `iOS/App/Licenses.txt` (AzooKeyKanaKanjiConverter, MIT, and its dictionary, Apache-2.0).

© 2026 CIZMO, K.K. · https://cizmo.net
