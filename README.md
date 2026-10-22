# KoEnJa (한영일 · 韓英日)

Type Korean, English and Japanese on one QWERTY layout. KoEnJa reads each word as you type and puts it in the language it belongs to: `dkssudgktpdy` → 안녕하세요, `hello` → hello, `kyouha` → 今日は. A switch key changes the word's language or the language that comes first.

한 자판으로 한국어·영어·일본어를 전환 없이 입력하는 키보드입니다. / 一つの配列で韓国語・英語・日本語を切り替えなしで入力できるキーボードです。

| Platform | Folder | State |
|---|---|---|
| iOS keyboard | `iOS/` | App Store review prep |
| Android keyboard | `Android/` | testing |
| Windows input method (TSF) | `Windows/` | testing (`Windows/installer`) |

The engine is [Araso](https://github.com/nyttastudio/araso) (formerly Seamtype, MIT) in `Packages/SeamtypeCore`, with REA's changes marked `// REA:`. Android (`Android/engine`, Kotlin) and Windows (`Windows/engine`, C++20) carry ports of it; all three replay the same golden file (`Android/engine/src/test/resources/golden.tsv`) and must match it line for line.

## Build

- Engine: `cd Packages/SeamtypeCore && swift test`
- iOS: `cd iOS && xcodegen && open REA.xcodeproj`
- Android: `cd Android && ./gradlew :engine:test :app:assembleDebug`
- Windows engine on macOS: `bash Windows/engine/build.sh`; on Windows (VS 2022 Build Tools): `Windows\installer\build.cmd`

## Privacy

What you type stays on the device. The keyboards have no networking (the Windows version will only check for updates).

## License

MIT — see `LICENSE`. Third-party code and data: `THIRD_PARTY_NOTICES.txt` (Araso MIT, Mozc dictionary, SCOWL, Tatoeba, FrequencyWords and others).

© 2026 CIZMO, K.K. · https://cizmo.net
