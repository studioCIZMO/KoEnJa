# KoEnJa (한영일 · 韓英日)

Type Korean, English and Japanese on one QWERTY layout. KoEnJa reads each word as you type and puts it in the language it belongs to: `dkssudgktpdy` → 안녕하세요, `hello` → hello, `kyouha` → 今日は. A switch key changes the word's language or the language that comes first.

한 자판으로 한국어·영어·일본어를 전환 없이 입력하는 키보드입니다. / 一つの配列で韓国語・英語・日本語を切り替えなしで入力できるキーボードです。

| Platform | Folder | State |
|---|---|---|
| iOS keyboard | `iOS/` | App Store review prep |
| Android keyboard | `Android/` | testing |
| Windows input method (TSF) | `Windows/` | testing (`Windows/installer`) |

## What KoEnJa is

KoEnJa is a keyboard for people who write in Korean, English and Japanese every day. You type everything on one QWERTY (romaji) layout, without a language switch key, and each word comes out in the right language as you type. When it guesses wrong, one switch key fixes the word. KoEnJa ships as three separate products: an iOS keyboard, an Android keyboard and a Windows input method. All of them are by CIZMO, K.K.

- **Per-word language choice.** It works inside a sentence and across mixed words: `githubdp` → github에, `kinouemailwookutta` → 昨日emailを送った.
- **Kanji conversion.** Japanese converts as you type. On Windows it works like Microsoft IME: Space and the conversion keys, F6–F10, and segment editing with Shift+←/→.
- **Your keyboard, your priority.** It supports the Korean (US) and Japanese (JIS) hardware layouts. You choose the switch key, and whether Korean or Japanese comes first.
- **On the device only.** Nothing you type leaves it.

## KoEnJa and Araso

KoEnJa uses the **engine** of [Araso](https://github.com/nyttastudio/araso) (© aimdevlee, MIT License). Araso is a macOS input method. The engine is the part of it that decides which language a word is in. KoEnJa is not a copy of Araso: Araso does not run on iOS, Android or Windows, and KoEnJa's apps, keyboards and input methods were written for this project.

| Part | Where it comes from |
|---|---|
| Language engine, iOS (`Packages/SeamtypeCore`) | Araso's engine source (`ArasoCore`, 0.2.2), used under the MIT License, with our changes. They are marked `// REA:` and listed in `Packages/SeamtypeCore/UPSTREAM`. They cover the memory use of the Korean vocabulary, committing exactly what the screen shows, and mixed-language rules. |
| Language engine, Android (`Android/engine`, Kotlin) and Windows (`Windows/engine`, C++20) | Written for KoEnJa to follow Araso's engine, then checked against it. The Swift, Kotlin and C++ engines replay the same golden file (`Android/engine/src/test/resources/golden.tsv`) and must match line for line. |
| iOS keyboard and app (`iOS/`) | Written for KoEnJa: the key layout, candidate bar, switch key and settings. |
| Android keyboard (`Android/app`, `Android/japanese`) | Written for KoEnJa, including a Japanese converter over the Mozc dictionary. |
| Windows input method (`Windows/tip`, `Windows/japanese`, `Windows/installer`) | Written for KoEnJa: the TSF text service, MS-IME-style conversion over the Mozc dictionary, keyboard types, switch key, language priority, settings and installer. |

Araso's macOS input method, its app and its installer are not part of KoEnJa. We are grateful to Araso's author for publishing the engine under the MIT License, which made this project possible. The engine's original copyright notice is kept in `THIRD_PARTY_NOTICES.txt` (and in the iOS app's license screen).

**한국어:** KoEnJa는 [Araso](https://github.com/nyttastudio/araso)(MIT)의 **언어 판별 엔진**을 MIT 라이선스에 따라 사용합니다. iOS는 그 엔진 소스에 저희 수정을 더해 쓰고, Android와 Windows용 엔진은 그 엔진을 따라 Kotlin과 C++로 새로 작성했습니다. 앱, 키보드, 입력기, 일본어 변환기, 설치 프로그램은 모두 KoEnJa를 위해 직접 만들었습니다. Araso는 macOS 입력기이고 KoEnJa는 iOS·Android·Windows용 별도 제품입니다. 엔진을 공개해 주신 Araso 개발자께 감사드립니다.

**日本語：** KoEnJa は [Araso](https://github.com/nyttastudio/araso)（MIT）の**言語判定エンジン**を MIT ライセンスに基づいて使用しています。iOS ではそのエンジンのソースに当社の変更を加えて使い、Android と Windows 用のエンジンはそれに倣って Kotlin と C++ で新たに書きました。アプリ、キーボード、入力メソッド、日本語変換、インストーラーはすべて KoEnJa のために独自に開発したものです。Araso は macOS 用の入力メソッドで、KoEnJa は iOS・Android・Windows 向けの別製品です。エンジンを公開してくださった Araso の開発者に感謝します。

## Build

- Engine: `cd Packages/SeamtypeCore && swift test`
- iOS: `cd iOS && xcodegen && open REA.xcodeproj`
- Android: `cd Android && ./gradlew :engine:test :app:assembleDebug`
- Windows engine on macOS: `bash Windows/engine/build.sh`; on Windows (VS 2022 Build Tools): `Windows\installer\build.cmd`

## Privacy

What you type stays on the device. The keyboards have no networking (the Windows version will only check for updates).

## Report a problem · 문제 보고 · 問題の報告

[Open an issue](https://github.com/studioCIZMO/KoEnJa/issues/new?template=bug.yml). On Windows, **Report a problem…** in KoEnJa Settings opens the same form with your version, Windows build and settings filled in (never what you typed). Issues are public, so leave out private text.

## License

MIT — see `LICENSE`. Third-party code and data: `THIRD_PARTY_NOTICES.txt` (Araso MIT, Mozc dictionary, SCOWL, Tatoeba, FrequencyWords and others); the iOS app adds `iOS/App/Licenses.txt` (AzooKeyKanaKanjiConverter, MIT, and its dictionary, Apache-2.0).

© 2026 CIZMO, K.K. · https://cizmo.net
