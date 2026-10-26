<p align="center">
  <img src="iOS/App/Assets.xcassets/AppIcon.appiconset/icon.png" width="120" alt="KoEnJa 아이콘">
</p>

<h1 align="center">KoEnJa</h1>

<p align="center"><b>한 자판으로 한·영·일 · 一つの配列で韓・英・日 · Korean, English, Japanese on one keyboard</b></p>

<p align="center">한영일(KoEnJa)은 한국어·영어·일본어를 언어 전환 없이 입력하는 키보드입니다.<br>
iOS 키보드, Android 키보드, Windows 입력기로 만들고 있습니다.</p>

<p align="center">
  <a href="https://github.com/studioCIZMO/KoEnJa/releases/latest"><img src="https://img.shields.io/github/v/release/studioCIZMO/KoEnJa?style=flat-square&label=release" alt="최신 릴리스"></a>
  <a href="https://github.com/studioCIZMO/KoEnJa/actions/workflows/windows.yml"><img src="https://img.shields.io/github/actions/workflow/status/studioCIZMO/KoEnJa/windows.yml?branch=main&style=flat-square&label=windows" alt="Windows 빌드와 골든 테스트"></a>
  <img src="https://img.shields.io/badge/Windows%2010%2F11-x64-555?style=flat-square" alt="Windows 10/11 x64">
  <img src="https://img.shields.io/badge/iOS-16%2B-555?style=flat-square" alt="iOS 16 이상">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue?style=flat-square" alt="MIT 라이선스"></a>
</p>

<p align="center"><a href="https://cizmo.net/KoEnJa/ko">웹사이트</a> · 한국어 · <a href="README.ja.md">日本語</a> · <a href="README.en.md">English</a></p>

<h3 align="center">한/영 키도, 일본어 전환도 누르지 않아도 됩니다.</h3>

<p align="center">
모든 글을 영문(QWERTY) 자판으로 치세요.<br>
KoEnJa가 <b>단어마다</b> 영어인지, 한국어(두벌식)인지, 일본어(로마자)인지 알아보고 맞는 글자로 넣습니다.
</p>

```
dhsmf ghldmldptj GitHub PR flqbfmf Rmxsorh mergegoTdjdy.  →  오늘 회의에서 GitHub PR 리뷰를 끝내고 merge했어요.
Please run the tests again before you push.               →  Please run the tests again before you push.
iPhonedetesutoshimashita⏎                                 →  iPhoneでてすとしました
kinouemailwookutta⏎                                       →  きのうemailをおくった
```

<sub>위 예는 엔진의 골든 테스트(`Android/engine/src/test/resources/golden.tsv`)에서 그대로 가져왔습니다. ⏎(Return)는 가나를 그대로 확정하고, Space는 한자로 변환합니다.</sub>

<p align="center">
  <a href="https://github.com/studioCIZMO/KoEnJa/releases/latest"><img src="https://img.shields.io/badge/Windows%EC%9A%A9%20%EB%B0%9B%EA%B8%B0-0078d4?style=for-the-badge&logo=windows&logoColor=white" alt="Windows용 받기"></a>
</p>

## 한눈에 보기

- **단어마다 알아서**: 영어, 한국어, 일본어 가운데 맞는 언어를 단어마다 고릅니다. 문장 중간에 언어가 바뀌어도 됩니다.
- **섞인 단어도**: `githubdp` → `github에`, `googlede` → `googleで`, `beautywo` → `beautyを`
- **틀리면 키 하나로**: 전환 키(iOS는 `한あA` 키)를 누르면 지금 단어가 다음 언어로 바뀝니다. Space로 확정한 직후에도 됩니다(`go` → `해`).
- **우선 언어**: 애매한 단어를 어느 언어로 읽을지, 한국어 우선과 日本語優先 가운데 고릅니다.
- **한자 변환**: Windows에서는 Microsoft IME처럼 Space·変換, F6–F10, Shift+←/→로 문절을 다룹니다.
- **세 플랫폼, 같은 판단**: Swift(iOS), Kotlin(Android), C++(Windows) 엔진이 같은 골든 테스트 1,884줄을 한 줄도 다르지 않게 통과합니다.
- **기기 안에서만**: 입력한 내용은 기기 밖으로 나가지 않습니다.
- **정확도 99.4%**: Araso의 평가 도구로 잰 값입니다(학습에 쓰지 않은 단어·구절 5,998개, 일본어 켬, [평가 결과](#평가-결과)).

## 목차

- [만든 이유](#만든-이유)
- [설치](#설치): [Windows](#windows) · [iOS](#ios) · [Android](#android)
- [키](#키)
- [판단 방식](#판단-방식) · [평가 결과](#평가-결과)
- [알려진 한계](#알려진-한계)
- [개발](#개발)
- [개인정보](#개인정보)
- [KoEnJa와 Araso](#koenja와-araso)
- [문제 보고](#문제-보고) · [코드 서명 정책](#코드-서명-정책) · [라이선스](#라이선스)

## 만든 이유

한국어·영어·일본어를 매일 섞어 쓰면 입력 소스가 셋이 되고, 전환 키를 누르는 일도 엉뚱한 언어로 쳐 버리는
일도 잦습니다. macOS에서는 [Araso](https://github.com/nyttastudio/araso)가 이 문제를 단어마다 언어를 알아보는
방식으로 풀었습니다. KoEnJa는 그 판단 엔진을 MIT 라이선스에 따라 빌려, Araso가 돌아가지 않는 iPhone, Android,
Windows에서 같은 방식으로 쓸 수 있게 만든 별도 제품입니다.

## 설치

### Windows

1. [Releases](https://github.com/studioCIZMO/KoEnJa/releases/latest)에서 `KoEnJa-Setup-0.1.0.exe`를 받아 실행하세요.
2. 0.1.0은 아직 코드 서명이 없어 SmartScreen의 **Windows의 PC 보호** 화면이 나옵니다.
   **추가 정보** → **실행**을 누르고, 이어지는 관리자 권한 확인에서 **예**를 누르세요.
3. 마지막 화면에서 **KoEnJa 설정 열기**를 체크한 채 마치면 키보드 종류, 쓰는 언어, 전환 키를 고를 수 있습니다.
   나중에는 시작 메뉴의 **KoEnJa 설정**에서 바꿉니다.
4. **Win+Space**로, 또는 작업 표시줄 오른쪽의 언어 표시(한/あ/ENG)를 눌러 **KoEnJa**를 고르세요.

64비트 Windows 10/11(x64) 전용이고, ARM64 Windows는 아직 지원하지 않습니다. 한국어·일본어 입력 목록에 KoEnJa가
추가됩니다(둘 다 없으면 한국어에 추가합니다).

<details>
<summary>제거</summary>

설정 → 앱 → 설치된 앱 → **KoEnJa** → 제거. 입력 목록에서도 빠집니다. 쓰는 중인 앱이 있으면 일부 파일은 다시
시작할 때 지워집니다. 설정과 학습한 단어(`%LOCALAPPDATA%\KoEnJa`)는 남겨 두므로, 필요 없으면 그 폴더를 지우세요.

</details>

### iOS

App Store 출시 준비 중입니다. 출시되면 이곳과 [웹사이트](https://cizmo.net/KoEnJa/ko)에 알리겠습니다.
설치한 뒤에는 앱에서 쓰는 언어의 순서를 고르고, 설정 → 일반 → 키보드에서 KoEnJa 키보드를 추가합니다.
**전체 접근 허용**은 앱에서 고른 언어 순서와 설정을 키보드가 읽고 햅틱을 쓰는 데 필요합니다([개인정보](#개인정보)).

### Android

테스트 중이며 아직 배포하지 않습니다. 소스는 `Android/`에 있습니다.

## 키

### Windows

| 키 | 하는 일 |
| --- | --- |
| 전환 키 (입력 중) | 지금 단어를 다음 언어로 바꿉니다. |
| 전환 키 (입력 중이 아닐 때) | 세 언어를 다 쓰면 우선 언어를 바꿉니다(**한국어 우선 ↔ 日本語優先**). 한국어+English나 日本語+English만 쓰면 KoEnJa를 켜고 끕니다(끄면 그냥 영어). |
| Space · 変換 | 일본어를 한자로 변환합니다. 한 번 더 누르면 후보 목록이 열립니다. |
| Space · Tab · ↓ / Shift+Space · ↑ | 후보 목록에서 다음 / 이전 후보. PageDown·PageUp은 페이지 단위. |
| 1–9 | 목록의 후보를 고릅니다. |
| Return | 전부 확정합니다. 변환 전이면 가나 그대로 확정합니다. |
| Esc · Backspace | 변환을 취소하고 가나로 돌아갑니다. |
| Shift+← / Shift+→ | 첫 문절을 줄이거나 늘립니다. →는 첫 문절을 확정하고 나머지를 변환합니다. |
| F6 · F7 · 無変換 | 히라가나 · 가타카나 · 둘 사이 바꾸기 |
| F8 · F9 · F10 | 반각 가타카나 · 전각 영문 · 반각 영문 |
| 半角/全角 (일본어 키보드) | KoEnJa 켜기·끄기 |

**전환 키 기본값**은 한국어/미국식 키보드에서 오른쪽 Alt 단독 탭(= 한/영), 일본어(JIS) 키보드에서 無変換입니다.
KoEnJa 설정에서 왼쪽 Ctrl 단독 탭, 한/영, 変換, 無変換, Ctrl+Shift 탭, Shift+Space, Ctrl+Space 가운데 고를 수
있습니다. Ctrl+Shift를 쓰려면 Windows의 Ctrl+Shift 키보드 레이아웃 전환 단축키를 꺼야 합니다
(설정 > 시간 및 언어 > 입력 > 고급 키보드 설정 > 입력 언어 바로 가기 키).

### iOS

- **`한あA` 키**: 지금 단어를 다음 언어로 바꿉니다. Space로 확정한 직후에 누르면 방금 넣은 단어를 바꿉니다.
- 쓰는 언어와 그 순서는 앱에서 고릅니다. 앞에 둔 언어가 애매한 단어를 가져갑니다.
- 일본어를 치는 동안 후보 막대에 변환 후보가 나오고, 확정한 뒤에는 이어질 말을 보여 줍니다.

## 판단 방식

KoEnJa의 판단은 [Araso](https://github.com/nyttastudio/araso)의 엔진에서 왔습니다. 자세한 원리는
[Araso의 판단 방식](https://github.com/nyttastudio/araso#판단-방식)을 보세요. 짧게 정리하면 이렇습니다.

- 같은 키를 영어, 한국어(두벌식), 일본어(로마자 → 가나)로 각각 읽고 점수를 매깁니다.
  영어는 글자 3-그램과 SCOWL 단어 목록, 한국어는 음절 빈도와 단어 목록, 일본어는 가나 3-그램을 씁니다.
- 대문자, 숫자, URL·경로 같은 규칙과 앞 단어의 언어를 함께 봅니다. 앞 단어와 다른 언어로 바꿔 치는 경우도 고려합니다.
- 영어 단어 + 한국어 조사(`githubdp` → `github에`), 영어 단어 + 일본어(`googlede` → `googleで`)처럼 섞인 단어는 나눠서 읽습니다.
- 우선 언어는 애매한 단어에서 그 언어 쪽으로 점수를 조금 더 줍니다.

KoEnJa가 바꾼 점은 `Packages/SeamtypeCore/UPSTREAM`에 적고 코드에 `// REA:`로 표시했습니다. 한국어 단어 목록을
해시로 들고 있어 메모리를 줄인 것, 화면에 보인 그대로 확정하는 것, 대문자로 시작하는 영어 단어 뒤 일본어,
주소 뒤 조사와 URL 속 점 파일 같은 규칙입니다.

## 평가 결과

Araso의 평가 도구(`tools/prepare.sh`, `eval_convert.py`, `eval_context.py`)를 그대로 써서, 같은 Mac에서 같은 데이터로
Araso 원본(22e7d3d)과 KoEnJa 엔진을 나란히 쟀습니다(2026-10-23, Apple M4, macOS 26.7.2). 전체 표, 명령, 데이터
해시는 [docs/evaluation.md](docs/evaluation.md)에 있습니다. 엔진을 Araso 0.3.1(eee3cbd)로 올린 뒤(2026-10-26) 같은 방법으로
다시 쟀고, 아래 언어 판단과 한자 변환 수치는 Araso 0.3.1과 KoEnJa 모두 그대로였습니다.

**학습에 쓰지 않은 데이터** (영어 단어 2,011개, 한국어 단어 1,987개, 일본어 구절 2,000개)

| | Araso README (공개값) | Araso 22e7d3d (이 Mac) | KoEnJa 엔진 (이 Mac) |
| --- | --- | --- | --- |
| 일본어 끔 (영/한) | 99.82% | 99.77% | 99.77% |
| 일본어 켬 (영/한/일) | 99.50% | 99.40% | 99.40% |

두 엔진은 5,998개 모두에서 같은 답을 냈습니다. 일본어 켬에서 영어 99.25%, 한국어 99.80%, 일본어 99.15%입니다.

**한자 변환** (맨 앞 후보가 원문과 정확히 같은 비율, 학습에 쓰지 않은 Tatoeba 문장 400개씩)

| | AzooKey + 순서 재조정 (iOS가 쓰는 경로) | 후보 목록 안에 정답 |
| --- | --- | --- |
| 2–3단어 구절 | 79.8% | 94.8% |
| 문장 전체 | 68.5% | 84.2% |

Araso 원본과 KoEnJa 엔진의 값이 같습니다. Android와 Windows는 Mozc 사전 위의 자체 변환기를 써서 이 도구로는
재지 못했습니다.

**문장 속 흔한 단어** (`eval_context.py`: 가장 흔한 영어·한국어 단어 10,000개를 각 언어 뒤에 둠, 상위 1,000개 / 10,000개 가운데 틀린 수)

| | 앞에 아무것도 없을 때 | 같은 언어 뒤 | 다른 언어 뒤 | 일본어 뒤 |
| --- | --- | --- | --- | --- |
| 한국어 단어 | 9 / 33 | 2 / 15 | 13 / 62 | 9 / 34 |
| 영어 단어 | 2 / 48 | 0 / 23 | 8 / 71 | 2 / 86 |

두 엔진 모두 같은 값입니다. Araso의 공개값과 조금 다른 것은 Tatoeba 데이터를 그날 새로 받았고, 공개값이 이후
엔진 변경 전에 잰 것이기 때문으로 보입니다(원본도 같은 차이를 보임).

**KoEnJa 엔진이 다른 점**

- 메모리: 같은 판단 작업에서 최대 메모리가 58.0 MB → 39.2 MB(약 19 MB 감소, `araso-cli`, macOS).
- 위 평가는 단어 하나씩 판단하는 경로라서 KoEnJa의 입력 세션 변경은 거치지 않습니다. 같은 5,998개를 입력
  세션으로 쳐 보면(`araso-cli --type`, 참고용) 13개가 달라졌고, KoEnJa가 6개를 더 맞히고 4개를 더 틀렸습니다.
  [자세히](docs/evaluation.md#supplementary-through-inputsession-not-one-of-arasos-published-evaluations)

**세 엔진이 같은지** (골든 테스트: 942개 키 입력 × 언어 순서 2가지)

| 엔진 | 결과 |
| --- | --- |
| Swift (iOS) | `golden.tsv`를 다시 만들면 1,884줄 모두 같음 |
| Kotlin (Android) | 1884/1884 |
| C++ clang (macOS) | 1884/1884, 키당 9.3 µs (Apple M4) |
| C++ MSVC (CI, GitHub windows-2022) | 1884/1884, 키당 25.7–26.1 µs (최근 3회) |

MSVC 골든 테스트는 `main`에 push할 때마다 [CI](.github/workflows/windows.yml)에서 돕니다.

## 알려진 한계

- Windows 0.1.0은 코드 서명이 없습니다([코드 서명 정책](#코드-서명-정책)). x64 전용이고 ARM64 Windows는 지원하지 않습니다.
- iOS는 App Store 출시 전이고, Android는 테스트 중입니다.
- 두 언어에서 모두 말이 되는 키(`go`/해, `so`/내)는 틀릴 수 있습니다. 전환 키로 고치세요.
- 영어 단어에 일본어 조사를 붙이면 일본어로 읽히는 경우가 있습니다(`sitede` → `してで`, `tokenwo` → `とけんを`).
- 한자 변환의 맨 앞 후보는 구절 5개 중 4개, 문장 3개 중 2개꼴로 원문과 같습니다(iOS 경로). 나머지는 후보 목록에서 고르세요.
- 평가 중에 찾은 회귀: 입력 세션에서 `woforo-shi`가 `재래개-し`로 들어갑니다(Araso 원본은 `をふぉろーし`). 아직 고치지 않았습니다.

## 개발

- 엔진: `cd Packages/SeamtypeCore && swift test`
- iOS: `cd iOS && xcodegen && open REA.xcodeproj`
- Android: `cd Android && ./gradlew :engine:test :app:assembleDebug`
- Windows 엔진(macOS): `bash Windows/engine/build.sh`, Windows(VS 2022 Build Tools): `Windows\installer\build.cmd`

| 경로 | 내용 |
| --- | --- |
| `Packages/SeamtypeCore` | 판단 엔진(Swift, Araso의 `ArasoCore`에 KoEnJa의 변경) |
| `iOS/` | iOS 앱과 키보드 |
| `Android/engine`, `Android/japanese`, `Android/app` | Kotlin 엔진, Mozc 사전 변환기, 키보드 |
| `Windows/engine`, `Windows/japanese`, `Windows/tip`, `Windows/installer` | C++20 엔진, 변환기, TSF 입력기, 설치 프로그램 |
| `Android/engine/src/test/resources/golden.tsv` | 세 엔진이 맞춰야 하는 골든 출력(Swift 엔진이 `Android/tools/golden`으로 생성) |

엔진을 바꾸면 Swift, Kotlin, C++를 함께 고치고 골든 파일을 다시 만듭니다. CI(`.github/workflows/windows.yml`)는
MSVC로 골든 테스트를 돌린 뒤 설치 프로그램을 만들고, `v*` 태그에서 릴리스를 올립니다.

## 개인정보

입력한 내용은 기기 안에서만 처리되고 밖으로 보내지 않습니다. 키보드와 입력기에는 네트워크 기능이 없습니다.
Windows의 **문제 보고…**는 누를 때만 브라우저에서 GitHub 페이지를 엽니다. iOS 앱의 개인정보처리방침은
[docs/privacy-policy.md](docs/privacy-policy.md)에 있습니다.

## KoEnJa와 Araso

KoEnJa는 [Araso](https://github.com/nyttastudio/araso)(© aimdevlee, MIT 라이선스)의 **엔진**을 씁니다. Araso는
macOS 입력기이고, 엔진은 그 가운데 단어의 언어를 판단하는 부분입니다. KoEnJa는 Araso의 사본이 아닙니다.
Araso는 iOS, Android, Windows에서 돌아가지 않고, KoEnJa의 앱·키보드·입력기는 이 프로젝트를 위해 새로 만들었습니다.

| 부분 | 출처 |
| --- | --- |
| 언어 엔진, iOS (`Packages/SeamtypeCore`) | Araso의 엔진 소스(`ArasoCore`, 0.3.1)를 MIT 라이선스에 따라 쓰고 KoEnJa의 변경을 더했습니다. 변경은 `// REA:`로 표시하고 `Packages/SeamtypeCore/UPSTREAM`에 적었습니다. 한국어 단어 목록의 메모리, 화면에 보인 그대로 확정하기, 섞인 언어 규칙입니다. |
| 언어 엔진, Android (`Android/engine`, Kotlin)와 Windows (`Windows/engine`, C++20) | Araso의 엔진을 따라 KoEnJa용으로 새로 작성하고 대조했습니다. Swift, Kotlin, C++ 엔진은 같은 골든 파일(`Android/engine/src/test/resources/golden.tsv`)을 재생해 한 줄도 다르지 않아야 합니다. |
| iOS 키보드와 앱 (`iOS/`) | KoEnJa용으로 작성: 키 배치, 후보 막대, 전환 키, 설정 |
| Android 키보드 (`Android/app`, `Android/japanese`) | KoEnJa용으로 작성: Mozc 사전 위의 일본어 변환기 포함 |
| Windows 입력기 (`Windows/tip`, `Windows/japanese`, `Windows/installer`) | KoEnJa용으로 작성: TSF 텍스트 서비스, Mozc 사전 위의 MS-IME식 변환, 키보드 종류, 전환 키, 우선 언어, 설정, 설치 프로그램 |

Araso의 macOS 입력기, 앱, 설치 프로그램은 KoEnJa에 들어 있지 않습니다. 엔진의 원래 저작권 고지는
`THIRD_PARTY_NOTICES.txt`(와 iOS 앱의 라이선스 화면)에 그대로 두었습니다.

**한국어:** KoEnJa는 [Araso](https://github.com/nyttastudio/araso)(MIT)의 **언어 판별 엔진**을 MIT 라이선스에 따라 사용합니다. iOS는 그 엔진 소스에 저희 수정을 더해 쓰고, Android와 Windows용 엔진은 그 엔진을 따라 Kotlin과 C++로 새로 작성했습니다. 앱, 키보드, 입력기, 일본어 변환기, 설치 프로그램은 모두 KoEnJa를 위해 직접 만들었습니다. Araso는 macOS 입력기이고 KoEnJa는 iOS·Android·Windows용 별도 제품입니다. 엔진을 공개해 주신 Araso 개발자께 감사드립니다.

**日本語：** KoEnJa は [Araso](https://github.com/nyttastudio/araso)（MIT）の**言語判定エンジン**を MIT ライセンスに基づいて使用しています。iOS ではそのエンジンのソースに当社の変更を加えて使い、Android と Windows 用のエンジンはそれに倣って Kotlin と C++ で新たに書きました。アプリ、キーボード、入力メソッド、日本語変換、インストーラーはすべて KoEnJa のために独自に開発したものです。Araso は macOS 用の入力メソッドで、KoEnJa は iOS・Android・Windows 向けの別製品です。エンジンを公開してくださった Araso の開発者に感謝します。

**English:** KoEnJa uses the **language engine** of [Araso](https://github.com/nyttastudio/araso) (MIT) under the MIT License. On iOS it uses that engine's source with our changes; the Android and Windows engines were written anew in Kotlin and C++ to follow it. The apps, keyboards, input method, Japanese converters and installer were all written for KoEnJa. Araso is a macOS input method; KoEnJa is a separate product for iOS, Android and Windows. We are grateful to Araso's author for publishing the engine under the MIT License, which made this project possible.

## 문제 보고

[이슈 열기](https://github.com/studioCIZMO/KoEnJa/issues/new?template=bug.yml). Windows에서는 KoEnJa 설정의
**문제 보고…**가 같은 양식을 버전, Windows 빌드, 설정을 채운 채로 엽니다(입력한 내용은 절대 넣지 않습니다).
이슈는 공개되므로 개인적인 글은 빼 주세요.

## 코드 서명 정책

**한국어:** 무료 코드 서명은 [SignPath.io](https://about.signpath.io)가, 인증서는 [SignPath Foundation](https://signpath.org)이 제공합니다. (0.1.0은 서명되지 않았으며, SignPath Foundation이 프로젝트를 승인하면 서명을 시작합니다.) 서명하는 것은 Windows 설치 프로그램과 그것이 설치하는 파일뿐입니다. 모두 이 저장소의 소스로 GitHub Actions(`.github/workflows/windows.yml`)에서 빌드하며, 릴리스마다 서명 전에 사람이 직접 승인합니다. 이 프로그램은 사용자나 설치·운영하는 사람이 특별히 요청하지 않는 한 다른 네트워크 시스템으로 어떤 정보도 보내지 않습니다. *문제 보고…*는 누를 때만 브라우저에서 GitHub 페이지를 엽니다.

**日本語：** 無償のコード署名は [SignPath.io](https://about.signpath.io) が、証明書は [SignPath Foundation](https://signpath.org) が提供しています。（0.1.0 は未署名です。SignPath Foundation にプロジェクトが承認され次第、署名を始めます。）署名するのは Windows インストーラーと、それがインストールするファイルだけです。いずれもこのリポジトリのソースから GitHub Actions（`.github/workflows/windows.yml`）でビルドし、リリースごとに署名の前に人の手で承認します。このプログラムは、ユーザーまたはインストール・運用する人が特に求めない限り、ほかのネットワーク システムに情報を送りません。*問題を報告…* はクリックしたときだけブラウザーで GitHub のページを開きます。

**English:**

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org). (0.1.0 is unsigned; signing starts once SignPath Foundation accepts the project.)

- Committers and reviewers: [studioCIZMO](https://github.com/studioCIZMO)
- Approvers: [studioCIZMO](https://github.com/studioCIZMO)

Only the Windows installer and the files it installs are signed. Each one is built by GitHub Actions from this repository's source (`.github/workflows/windows.yml`), and every release is approved by hand before signing.

Privacy: this program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it. *Report a problem…* opens a GitHub page in your browser only when you click it.

## 라이선스

MIT — `LICENSE`를 보세요. 서드파티 코드와 데이터: `THIRD_PARTY_NOTICES.txt`(Araso MIT, Mozc 사전, SCOWL, Tatoeba,
FrequencyWords 등). iOS 앱은 `iOS/App/Licenses.txt`(AzooKeyKanaKanjiConverter, MIT, 그 사전, Apache-2.0)가 더해집니다.

© 2026 CIZMO, K.K. · https://cizmo.net
