<p align="center">
  <img src="iOS/App/Assets.xcassets/AppIcon.appiconset/icon.png" width="120" alt="KoEnJa のアイコン">
</p>

<h1 align="center">KoEnJa</h1>

<p align="center"><b>한 자판으로 한·영·일 · 一つの配列で韓・英・日 · Korean, English, Japanese on one keyboard</b></p>

<p align="center">KoEnJa（韓英日）は、韓国語・英語・日本語を切り替えなしで入力できるキーボードです。<br>
iOS キーボード、Android キーボード、Windows の入力メソッドとして開発しています。</p>

<p align="center">
  <a href="https://github.com/studioCIZMO/KoEnJa/releases/latest"><img src="https://img.shields.io/github/v/release/studioCIZMO/KoEnJa?style=flat-square&label=release" alt="最新リリース"></a>
  <a href="https://github.com/studioCIZMO/KoEnJa/actions/workflows/windows.yml"><img src="https://img.shields.io/github/actions/workflow/status/studioCIZMO/KoEnJa/windows.yml?branch=main&style=flat-square&label=windows" alt="Windows のビルドとゴールデンテスト"></a>
  <img src="https://img.shields.io/badge/Windows%2010%2F11-x64-555?style=flat-square" alt="Windows 10/11 x64">
  <img src="https://img.shields.io/badge/iOS-16%2B-555?style=flat-square" alt="iOS 16 以降">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue?style=flat-square" alt="MIT ライセンス"></a>
</p>

<p align="center"><a href="https://cizmo.net/KoEnJa">ウェブサイト</a> · <a href="README.md">한국어</a> · 日本語 · <a href="README.en.md">English</a></p>

<h3 align="center">한/영 キーも、日本語への切り替えも、もう要りません。</h3>

<p align="center">
すべて英字（QWERTY）配列で打ってください。<br>
KoEnJa が<b>単語ごとに</b>英語か、韓国語（2ボル式）か、日本語（ローマ字）かを見分けて、正しい文字で入力します。
</p>

```
dhsmf ghldmldptj GitHub PR flqbfmf Rmxsorh mergegoTdjdy.  →  오늘 회의에서 GitHub PR 리뷰를 끝내고 merge했어요.
Please run the tests again before you push.               →  Please run the tests again before you push.
iPhonedetesutoshimashita⏎                                 →  iPhoneでてすとしました
kinouemailwookutta⏎                                       →  きのうemailをおくった
```

<sub>上の例はエンジンのゴールデンテスト（`Android/engine/src/test/resources/golden.tsv`）からそのまま取ったものです。⏎（Return）はかなのまま確定し、Space で漢字に変換します。</sub>

<p align="center">
  <a href="https://github.com/studioCIZMO/KoEnJa/releases/latest"><img src="https://img.shields.io/badge/Windows%E7%89%88%E3%82%92%E3%83%80%E3%82%A6%E3%83%B3%E3%83%AD%E3%83%BC%E3%83%89-0078d4?style=for-the-badge&logo=windows&logoColor=white" alt="Windows 版をダウンロード"></a>
</p>

## ひと目でわかる KoEnJa

- **単語ごとに自動で**：英語・韓国語・日本語のうち合う言語を単語ごとに選びます。文の途中で言語が変わっても大丈夫です。
- **混ざった単語も**：`githubdp` → `github에`、`googlede` → `googleで`、`beautywo` → `beautyを`
- **間違えたらキーひとつ**：切り替えキー（iOS は `한あA` キー）を押すと、いまの単語が次の言語に変わります。Space で確定した直後でも使えます（`go` → `해`）。
- **優先言語**：あいまいな単語をどちらで読むかを、한국어 우선 と 日本語優先 から選べます。
- **漢字変換**：Windows では Microsoft IME と同じように、Space・変換、F6–F10、Shift+←/→ で文節を扱えます。
- **3 つのプラットフォームで同じ判定**：Swift（iOS）、Kotlin（Android）、C++（Windows）のエンジンが、同じゴールデンテスト 1,884 行を 1 行も違えずに通ります。
- **端末の中だけで**：入力した内容は端末の外に出ません。
- **正解率 99.4%**：Araso の評価ツールで測った値です（学習に使っていない単語・フレーズ 5,998 件、日本語オン、[評価結果](#評価結果)）。

## 目次

- [作った理由](#作った理由)
- [インストール](#インストール)：[Windows](#windows) · [iOS](#ios) · [Android](#android)
- [キー](#キー)
- [判定のしくみ](#判定のしくみ) · [評価結果](#評価結果)
- [既知の制限](#既知の制限)
- [開発](#開発)
- [プライバシー](#プライバシー)
- [KoEnJa と Araso](#koenja-と-araso)
- [問題の報告](#問題の報告) · [コード署名ポリシー](#コード署名ポリシー) · [ライセンス](#ライセンス)

## 作った理由

韓国語・英語・日本語を毎日まぜて書くと入力ソースが 3 つになり、切り替えキーを押す手間も、違う言語で打ってしまう
ことも増えます。macOS では [Araso](https://github.com/nyttastudio/araso) が、単語ごとに言語を見分けるという方法で
これを解決しました。KoEnJa はその判定エンジンを MIT ライセンスに基づいて使い、Araso が動かない iPhone・Android・
Windows でも同じように使えるようにした別製品です。

## インストール

### Windows

1. [Releases](https://github.com/studioCIZMO/KoEnJa/releases/latest) から `KoEnJa-Setup-0.1.0.exe` をダウンロードして実行します。
2. 0.1.0 はまだコード署名がないため、SmartScreen の「**Windows によって PC が保護されました**」が表示されます。
   「**詳細情報**」→「**実行**」を押し、続く管理者の確認で「**はい**」を押してください。
3. 最後の画面で「**KoEnJa の設定を開く**」にチェックしたまま完了すると、キーボードの種類、使う言語、切り替えキーを
   選べます。あとからはスタートメニューの「**KoEnJa 설정**」で変更できます。
4. **Win+Space** で、またはタスクバー右側の言語表示（あ/한/ENG）をクリックして **KoEnJa** を選びます。

64 ビット版 Windows 10/11（x64）専用で、ARM64 版 Windows にはまだ対応していません。日本語・韓国語の入力方式の
一覧に KoEnJa が追加されます（どちらもなければ韓国語に追加します）。

<details>
<summary>アンインストール</summary>

設定 → アプリ → インストールされているアプリ →「**KoEnJa**」→ アンインストール。入力方式の一覧からも外れます。
使用中のアプリがあれば、一部のファイルは再起動時に削除されます。設定と学習した単語（`%LOCALAPPDATA%\KoEnJa`）は
残すので、不要ならそのフォルダーを削除してください。

</details>

### iOS

App Store の審査中です。公開したらここと[ウェブサイト](https://cizmo.net/KoEnJa)でお知らせします。
インストール後、アプリで使う言語の順番を選び、設定 → 一般 → キーボードで KoEnJa を追加します。
「**フルアクセスを許可**」は、アプリで選んだ言語の順番と設定をキーボードが読み取り、触覚フィードバックを使うために
必要です（[プライバシー](#プライバシー)）。

### Android

Google Play での公開を準備中です。それまでは[リリース](https://github.com/studioCIZMO/KoEnJa/releases/latest)から `KoEnJa-Android-0.2.1.apk`（Android 8.0 以上）をインストールできます。
インストール後、アプリの「키보드 설정 열기」で 한영일 をオンにし、「입력 방법 고르기」で選んでください（アプリの画面はまだ韓国語のみ）。
Play 版は Google が署名するため、あとで Play 版に移るときは APK 版を削除してから Play でインストールしてください。ソースは `Android/` にあります。

## キー

### Windows

| キー | 動作 |
| --- | --- |
| 切り替えキー（入力中） | いま打っている単語を次の言語に変えます。 |
| 切り替えキー（入力していないとき） | 3 言語とも使うときは優先言語を切り替えます（**한국어 우선 ↔ 日本語優先**）。한국어 + English か 日本語 + English だけなら KoEnJa のオン・オフです（オフは英語そのまま）。 |
| Space・変換 | 日本語を漢字に変換します。もう一度押すと候補一覧が開きます。 |
| Space・Tab・↓ / Shift+Space・↑ | 候補一覧で次 / 前の候補。PageDown・PageUp はページ単位。 |
| 1–9 | 一覧の候補を選びます。 |
| Return | すべて確定します。変換前ならかなのまま確定します。 |
| Esc・Backspace | 変換をやめてかなに戻ります。 |
| Shift+← / Shift+→ | 最初の文節を短く / 長くします。→ は最初の文節を確定して残りを変換します。 |
| F6・F7・無変換 | ひらがな・カタカナ・その切り替え |
| F8・F9・F10 | 半角カタカナ・全角英字・半角英字 |
| 半角/全角（日本語キーボード） | KoEnJa のオン・オフ |

**切り替えキーの既定**は、韓国語 / 英語（US）キーボードでは右 Alt の単独押し（= 한/영）、日本語（JIS）キーボードでは
無変換です。KoEnJa の設定で、左 Ctrl の単独押し、한/영、変換、無変換、Ctrl+Shift、Shift+Space、Ctrl+Space から
選べます。Ctrl+Shift を使うには、Windows の Ctrl+Shift によるキーボード レイアウト切り替えをオフにしてください
（設定 > 時刻と言語 > 入力 > キーボードの詳細設定 > 入力言語のホットキー）。

### iOS

- **`한あA` キー**：いまの単語を次の言語に変えます。Space で確定した直後に押すと、いま入れた単語を変えます。
- 使う言語とその順番はアプリで選びます。先にある言語が、あいまいな単語を取ります。
- 日本語を打っている間は候補バーに変換候補が出て、確定したあとは続きそうな言葉を表示します。

## 判定のしくみ

KoEnJa の判定は [Araso](https://github.com/nyttastudio/araso) のエンジンによるものです。詳しくは
[Araso の判定方法（韓国語）](https://github.com/nyttastudio/araso#판단-방식)をご覧ください。要点は次のとおりです。

- 同じキーを英語、韓国語（2ボル式）、日本語（ローマ字 → かな）として読み、それぞれ点数をつけます。
  英語は文字 3-gram と SCOWL の単語リスト、韓国語は音節の頻度と単語リスト、日本語はかなの 3-gram を使います。
- 大文字、数字、URL・パスといった規則と、直前の単語の言語もあわせて見ます。前の単語と違う言語に切り替えて打つ場合も考慮します。
- 英単語 + 韓国語の助詞（`githubdp` → `github에`）、英単語 + 日本語（`googlede` → `googleで`）のような混ざった単語は分けて読みます。
- 優先言語は、あいまいな単語でその言語に少し多く点数を与えます。

KoEnJa での変更は `Packages/SeamtypeCore/UPSTREAM` に記載し、コードに `// REA:` で印をつけています。韓国語の単語
リストをハッシュで持ってメモリを減らしたこと、画面に表示したとおりに確定すること、大文字で始まる英単語に続く日本語、
アドレスの後の助詞、URL 内のドットファイルなどの規則です。

## 評価結果

Araso の評価ツール（`tools/prepare.sh`、`eval_convert.py`、`eval_context.py`）をそのまま使い、同じ Mac・同じデータで
Araso 本家（22e7d3d）と KoEnJa のエンジンを並べて測りました（2026-10-23、Apple M4、macOS 26.7.2）。すべての表、
コマンド、データのハッシュは [docs/evaluation.md](docs/evaluation.md)（英語）にあります。エンジンを Araso 0.3.1（eee3cbd）に
更新したあと（2026-10-26）同じ方法で測り直し、下の言語判定と漢字変換の数値は Araso 0.3.1・KoEnJa ともに変わりませんでした。

**学習に使っていないデータ**（英単語 2,011 件、韓国語の単語 1,987 件、日本語のフレーズ 2,000 件）

| | Araso README（公表値） | Araso 22e7d3d（この Mac） | KoEnJa エンジン（この Mac） |
| --- | --- | --- | --- |
| 日本語オフ（英/韓） | 99.82% | 99.77% | 99.77% |
| 日本語オン（英/韓/日） | 99.50% | 99.40% | 99.40% |

2 つのエンジンは 5,998 件すべてで同じ答えを出しました。日本語オンで英語 99.25%、韓国語 99.80%、日本語 99.15% です。

**漢字変換**（第 1 候補が原文と完全に一致する割合、学習に使っていない Tatoeba の文 400 件ずつ）

| | AzooKey + 並べ替え（iOS が使う経路） | 候補一覧の中に正解 |
| --- | --- | --- |
| 2–3 語のフレーズ | 79.8% | 94.8% |
| 文全体 | 68.5% | 84.2% |

Araso 本家と KoEnJa のエンジンで同じ値です。Android と Windows は Mozc 辞書を使う独自の変換器のため、このツールでは
測れていません。

**文中のよく使う単語**（`eval_context.py`：よく使う英語・韓国語の単語 10,000 語を各言語の後に置いたとき、上位 1,000 語 / 10,000 語のうち誤りの数）

| | 前に何もないとき | 同じ言語の後 | 別の言語の後 | 日本語の後 |
| --- | --- | --- | --- | --- |
| 韓国語の単語 | 9 / 33 | 2 / 15 | 13 / 62 | 9 / 34 |
| 英単語 | 2 / 48 | 0 / 23 | 8 / 71 | 2 / 86 |

どちらのエンジンも同じ値です。Araso の公表値と少し違うのは、Tatoeba のデータをその日に取り直したことと、公表値が
その後のエンジン変更より前に測られたためと考えられます（本家でも同じ差が出ます）。

**KoEnJa のエンジンで違う点**

- メモリ：同じ判定処理で最大メモリが 58.0 MB → 39.2 MB（約 19 MB 減、`araso-cli`、macOS）。
- 上の評価は単語を 1 つずつ判定する経路なので、KoEnJa の入力セッションの変更は通りません。同じ 5,998 件を入力
  セッションで打つと（`araso-cli --type`、参考値）13 件が変わり、KoEnJa が 6 件多く正解し、4 件多く誤りました。
  [詳細](docs/evaluation.md#supplementary-through-inputsession-not-one-of-arasos-published-evaluations)

**3 つのエンジンが同じか**（ゴールデンテスト：942 通りのキー入力 × 言語の順番 2 通り）

| エンジン | 結果 |
| --- | --- |
| Swift（iOS） | `golden.tsv` を作り直すと 1,884 行すべて一致 |
| Kotlin（Android） | 1884/1884 |
| C++ clang（macOS） | 1884/1884、1 キーあたり 9.3 µs（Apple M4） |
| C++ MSVC（CI、GitHub windows-2022） | 1884/1884、1 キーあたり 25.7–26.1 µs（直近 3 回） |

MSVC のゴールデンテストは `main` への push のたびに [CI](.github/workflows/windows.yml) で実行されます。

## 既知の制限

- Windows 0.1.0 はコード署名がありません（[コード署名ポリシー](#コード署名ポリシー)）。x64 専用で、ARM64 版 Windows には対応していません。
- iOS は App Store の審査中、Android は Google Play 公開前で APK のみです。Android アプリの画面はまだ韓国語だけです。
- 2 つの言語のどちらでも意味が通るキー（`go`/해、`so`/내）は間違えることがあります。切り替えキーで直してください。
- 英単語に日本語の助詞をつけると、日本語として読まれることがあります（`sitede` → `してで`、`tokenwo` → `とけんを`）。
- 漢字変換の第 1 候補が原文と一致するのは、フレーズで 5 件に 4 件、文で 3 件に 2 件ほどです（iOS の経路）。残りは候補一覧から選んでください。
- 評価中に見つけた不具合：入力セッションで `woforo-shi` が `재래개-し` になります（Araso 本家は `をふぉろーし`）。まだ直していません。

## 開発

- エンジン：`cd Packages/SeamtypeCore && swift test`
- iOS：`cd iOS && xcodegen && open REA.xcodeproj`
- Android：`cd Android && ./gradlew :engine:test :app:assembleDebug`
- Windows のエンジン（macOS）：`bash Windows/engine/build.sh`、Windows（VS 2022 Build Tools）：`Windows\installer\build.cmd`

| パス | 内容 |
| --- | --- |
| `Packages/SeamtypeCore` | 判定エンジン（Swift、Araso の `ArasoCore` に KoEnJa の変更） |
| `iOS/` | iOS アプリとキーボード |
| `Android/engine`、`Android/japanese`、`Android/app` | Kotlin のエンジン、Mozc 辞書の変換器、キーボード |
| `Windows/engine`、`Windows/japanese`、`Windows/tip`、`Windows/installer` | C++20 のエンジン、変換器、TSF 入力メソッド、インストーラー |
| `Android/engine/src/test/resources/golden.tsv` | 3 つのエンジンが一致すべきゴールデン出力（Swift のエンジンが `Android/tools/golden` で生成） |

エンジンを変えるときは Swift・Kotlin・C++ をあわせて直し、ゴールデンファイルを作り直します。CI
（`.github/workflows/windows.yml`）は MSVC でゴールデンテストを実行してからインストーラーを作り、`v*` タグでリリースを公開します。

## プライバシー

入力した内容は端末の中だけで処理され、外には送りません。キーボードと入力メソッドにはネットワーク機能がありません。
Windows の「**問題を報告…**」は、クリックしたときだけブラウザーで GitHub のページを開きます。iOS アプリの
プライバシーポリシーは [docs/privacy-policy.md](docs/privacy-policy.md) にあります。

## KoEnJa と Araso

KoEnJa は [Araso](https://github.com/nyttastudio/araso)（© aimdevlee、MIT ライセンス）の**エンジン**を使っています。
Araso は macOS 用の入力メソッドで、エンジンはそのうち単語の言語を判定する部分です。KoEnJa は Araso のコピーでは
ありません。Araso は iOS・Android・Windows では動かず、KoEnJa のアプリ・キーボード・入力メソッドはこのプロジェクトの
ために新たに作ったものです。

| 部分 | 出どころ |
| --- | --- |
| 言語エンジン、iOS（`Packages/SeamtypeCore`） | Araso のエンジンのソース（`ArasoCore`、0.3.1）を MIT ライセンスに基づいて使い、KoEnJa の変更を加えています。変更は `// REA:` で印をつけ、`Packages/SeamtypeCore/UPSTREAM` に記載しています。韓国語の単語リストのメモリ、画面に表示したとおりの確定、混在言語の規則です。 |
| 言語エンジン、Android（`Android/engine`、Kotlin）と Windows（`Windows/engine`、C++20） | Araso のエンジンに倣って KoEnJa 用に書き、照合しました。Swift・Kotlin・C++ のエンジンは同じゴールデンファイル（`Android/engine/src/test/resources/golden.tsv`）を再生し、1 行も違ってはいけません。 |
| iOS のキーボードとアプリ（`iOS/`） | KoEnJa 用に作成：キー配置、候補バー、切り替えキー、設定 |
| Android のキーボード（`Android/app`、`Android/japanese`） | KoEnJa 用に作成：Mozc 辞書を使う日本語変換器を含む |
| Windows の入力メソッド（`Windows/tip`、`Windows/japanese`、`Windows/installer`） | KoEnJa 用に作成：TSF テキスト サービス、Mozc 辞書による MS-IME 風の変換、キーボードの種類、切り替えキー、優先言語、設定、インストーラー |

Araso の macOS 入力メソッド、アプリ、インストーラーは KoEnJa に含まれていません。エンジンを MIT ライセンスで公開して
くださった Araso の開発者に感謝します。おかげでこのプロジェクトが実現しました。エンジンの元の著作権表示は
`THIRD_PARTY_NOTICES.txt`（と iOS アプリのライセンス画面）にそのまま残しています。

## 問題の報告

[Issue を作成](https://github.com/studioCIZMO/KoEnJa/issues/new?template=bug.yml)してください。Windows では、KoEnJa の
設定の「**問題を報告…**」が、バージョン、Windows のビルド、設定を記入した状態で同じフォームを開きます（入力した内容は
決して含めません）。Issue は公開されるので、個人的な文章は入れないでください。

## コード署名ポリシー

無償のコード署名は [SignPath.io](https://about.signpath.io) が、証明書は [SignPath Foundation](https://signpath.org) が
提供しています。（0.1.0 は未署名です。SignPath Foundation にプロジェクトが承認され次第、署名を始めます。）

> Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org).

- コミッターとレビュアー：[studioCIZMO](https://github.com/studioCIZMO)
- 承認者：[studioCIZMO](https://github.com/studioCIZMO)

署名するのは Windows インストーラーと、それがインストールするファイルだけです。いずれもこのリポジトリのソースから
GitHub Actions（`.github/workflows/windows.yml`）でビルドし、リリースごとに署名の前に人の手で承認します。

プライバシー：このプログラムは、ユーザーまたはインストール・運用する人が特に求めない限り、ほかのネットワーク
システムに情報を送りません。「*問題を報告…*」はクリックしたときだけブラウザーで GitHub のページを開きます。

## ライセンス

MIT（`LICENSE` を参照）。サードパーティのコードとデータ：`THIRD_PARTY_NOTICES.txt`（Araso MIT、Mozc 辞書、SCOWL、
Tatoeba、FrequencyWords など）。iOS アプリにはさらに `iOS/App/Licenses.txt`（AzooKeyKanaKanjiConverter、MIT、
その辞書、Apache-2.0）が加わります。

© 2026 CIZMO, K.K. · https://cizmo.net
