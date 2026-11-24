# KoEnJa(한영일) 인수인계 — 2026-11-16

다른 PC에서 이 프로젝트를 이어가기 위한 문서입니다. 코드로 알 수 있는 내용은 생략하고,
코드 밖의 상태·절차·규칙만 적습니다. 에이전트(Claude/Codex)용 규칙은 [CLAUDE.md](CLAUDE.md)에 있습니다.

## 1. 지금 상태 (main 093e48d + 이 커밋)

| 플랫폼 | 공개 버전 | 이 커밋의 버전 | 상태 |
|---|---|---|---|
| Mac 입력기 | **0.3.14 (GitHub v0.3.14, Latest, 11/24)** | 0.3.14 (17) | 공개됨(zip ×2). **KoEnJa.pkg는 미서명이라 릴리스에 안 올림**(.build/mac0314/release에 있음): Developer ID Installer 인증서 + notarytool 프로필이 생기면 `KOENJA_PKG_SIGN`/`KOENJA_NOTARY_PROFILE`로 release.sh 다시 돌려 v0.3.14에 `gh release upload`. 그때까지 README·LP의 pkg 안내는 선행 서술 |
| iOS 키보드 | 1.0.5 (App Store), 1.0.6 (16) 심사 중, 1.0.7 (17) TestFlight | **1.0.8 (18)** | 11/23 ASC 업로드(비밀번호 칸 학습 차단, 개인 교정 원문 보존, 오타 기록 백업 제외). 1.0.7: 키 라벨, 여백 귀속, ⌫ 교정 터치 학습, 개인 교정, 오타 기록 |
| Android 키보드 | **0.2.7 (9) Play 공개(11/23)**, APK **0.2.8 (GitHub v0.3.14, 11/24)** | 0.2.8 (10) (.build/KoEnJa-Android-0.2.8.aab, Play 미제출) | 0.2.8: 비밀번호 칸 학습·추천·자동 고침 차단(Codex P1), 개인정보 안내+처리방침 링크(Play 정책, P1), 단축어 총량 한도 = 복원 한도. Play 업로드는 사용자 확인 후 |
| Windows 입력기 | **0.3.4 (GitHub v0.3.14, 11/24)** | 0.3.4 | 이슈 #1(LINE에서 한글 뒤 Space/Enter 사라짐) 수정: 커밋 뒤 통과 키를 SendInput으로 재전송(Tip.cpp replay). 11/24 이슈에 0.3.4 확인 요청 댓글 달았음(답 기다림) |

엔진·Mac 기준 = **Araso 0.6.2 = 97e0075** (11/24 확인: b19e054 뒤 커밋은 0.6.2 버전 올림(Info.plist)만이라 코드 변경 없음, UPSTREAM 표식만 갱신). b19e054 동기화 내용(0.6.1 → main 3커밋, 2026-11-14):
- 엔진(Swift): 전환 키로 고른 읽기를 그 글자들만 즉시 학습(`switchedKeys`/`switchLearned`/`learnSwitchBeforeMore`), ↑/↓는 커서가 단어 끝일 때만(`listApplies`), 전각 스페이스 뒤 `noteRun`, `rememberedLanguage(keys, app:)`, `vimSignal`.
- Mac 앱: InputController(vimSignal 재전송, learned 변경 시 syncStrip), Updater(Mac 빌드 없는 릴리스 = 최신 처리, 저장 키 삭제), Uninstaller(rm && pkgutil --forget).
- Kotlin/C++ 포트: 위 학습 변경만 손으로 포팅. 포트는 의도적으로 fixing, readings arrows, per-app rememberedLanguage, vimSignal 없음.
- 골든: `golden_input.txt`에 `so※on`/`so`/`soon` 3그룹 추가 → golden.tsv 1950줄.
- 검증 완료: Swift 559 테스트, Mac Debug 빌드, Kotlin 엔진 테스트, 골든 1950/1950 (Kotlin, clang), session_test 164/164.
- **Codex 리뷰 미완**: `codex exec`가 16분간 세션조차 못 열어 중단함. 새 PC에서 리뷰 1회 돌릴 것(§4 참고). 리뷰 프롬프트 요지는 §6.

릴리스 노트 초안: [docs/release/0.3.13-notes.md](docs/release/0.3.13-notes.md)(GitHub), [docs/release/0.3.13-store-notes.md](docs/release/0.3.13-store-notes.md)(iOS whatsNew / Play).

## 2. 계정·자격 (비밀값은 이 저장소에 없음)

- GitHub: 코드 `studioCIZMO/KoEnJa`(공개, MIT). 커밋 author **CIZMO <cizmode@gmail.com>**(repo-local config). push는 `gh auth token -u studioCIZMO`로(아래 명령). `netmaru/REA`는 비공개 아카이브, 절대 push 금지(히스토리 filter-repo로 정리됨).
- 웹사이트: `cizmo.net/KoEnJa`(ko/en/ja LP), repo `netmaru/cizmo-renewal`(Vercel 자동 배포). push는 `gh auth token -u netmaru`.
- Apple: 팀 **CIZMO, K.K. (N723QM4R64)**. ASC 앱 KoEnJa id 6824875642(com.cizmo.koenja / .keyboard, group.com.cizmo.koenja). Mac 번들 com.cizmo.inputmethod.KoEnJa. Xcode는 **릴리스판만**(beta SDK 업로드는 ASC가 거절). TestFlight 내부 그룹 "CIZMO"(cizmode@gmail.com, netmaru@gmail.com). iPad는 팀에 기기 등록 불가 → TestFlight로 테스트.
- Google Play: CIZMO 개발자 계정, 앱 id 4972872594954085273, 패키지 com.cizmo.koenja(Kotlin 패키지는 com.netmaru.rea). 관리형 게시. **업로드 키 `~/.koenja/upload.jks` + `upload.properties`는 이 Mac에만 있음 → 새 PC로 안전하게 옮길 것(커밋·출력 금지).** AAB 업로드는 사용자가 Play Console에 직접 드래그(10MB 넘어 브라우저 도구 불가).
- Windows 서명: 미서명(SmartScreen 경고). SignPath Foundation 신청 계획, LP에 Code signing policy 섹션 있음.
- Windows 테스트 PC로 소스 보내기: `COPYFILE_DISABLE=1 tar czf … --exclude='._*'`(AppleDouble `._*.cpp`가 섞이면 MSVC가 C2018로 실패) → scp → `tar -xzf` → `Windows\tip\build.cmd`.
- 클라우드 루틴(claude.ai/code/routines): `trig_017ohTsEDwDRCaas6G3ycafQ` 월·목 10:00 JST Araso 새 커밋 검토 → `araso-sync/<sha>` 브랜치에 DRAFT PR; `trig_01EHTadrwBwMTnnDPEkrMrKb` 월 09:00 JST 이슈·다운로드 집계 보고(읽기 전용).

## 3. 개발 환경

- Mac(현재): macOS 26, Xcode 27.2(/Applications/Xcode.app), Android Studio/SDK, XcodeGen. 테스트 AVD **REA_Pixel**(REA_Play는 경쟁앱 분석용, `checkame`는 다른 프로젝트 — 건드리지 말 것).
- Xcode 프로젝트는 생성물: `cd iOS && xcodegen`, `cd Mac && xcodegen`(project.yml). `.gitignore`에 있음.
- Android `local.properties`(sdk.dir)는 PC별로 새로 만듦.
- Windows 테스트 PC: Windows 11 x64, LAN 192.168.4.71(바뀔 수 있음), 사용자 `netma`, SSH 키 `~/.ssh/rea_windows`(이 Mac에만 있음). git 없음 → `git archive` tar를 scp. VS 2022 BuildTools, Inno Setup 6. 설치 빌드는 `C:\Program Files\KoEnJa\<ver>`. 바탕화면은 OneDrive 동기화 → 바이너리 복사 금지.
- Mac 입력기 로컬 설치: `KOENJA_ZIP=<zip> sh Mac/get.sh`(0.3.13부터 /Library/Input Methods; 첫 설치만 sudo 암호). **입력 메뉴에서 회색으로 죽는 현상(11/02, 11/13, 11/17 재발)**: `/usr/bin/log show --last 10m --predicate 'process == "imklaunchagent" AND messageType == error'`에 `LaunchInputMethod() status=-50` / `Refusing connection name for bundle: unrecognized 'InputMethodConnectionName' value` / `Request for Endpoint Invalid`가 찍히면 그 상태. 앱 서명·공증·LS 등록·폴더 권한·Info.plist는 모두 정상이었고, 입력 소스 재추가·imklaunchagent 재시작(kill)·`open -b` 수동 실행으로는 안 풀림. **재부팅만 복구**. 11/17부터 실험으로 KoEnJa.app을 `/Library/Input Methods`(Araso와 같은 곳, 거기선 한 번도 안 죽음)로 옮겨 둠(`sudo mv`). /Library의 번들은 macOS 앱 관리(App Management, TCC `kTCCServiceSystemPolicyAppBundles`) 보호를 받아, 소유자라도 그 권한이 있는 터미널에서만 get.sh로 교체할 수 있다(Orca 셸은 불가 → 사용자 터미널에서 실행). 앱 안의 Updater는 같은 팀 서명이라 허용될 것으로 보이나 0.3.14 업데이트 때 실제 확인 필요. **11/18 확정에 가까운 원인: 이 Mac에서 KoEnJa를 빌드하면 Xcode가 빌드 산출물·아카이브·공증 사본을 같은 번들 ID로 LaunchServices에 등록하고, 런처가 로그인 때 그중 엉뚱한 것을 띄우려다 실패한다(중복 정리 후 부팅은 정상, 빌드 뒤 중복 7개인 채 부팅하면 실패; 일반 사용자 Mac에는 없는 현상).** 빌드·아카이브 뒤와 재부팅 전에 `sh Mac/lsclean.sh`(설치본만 남기고 등록 해제)를 돌릴 것. `.build/.metadata_never_index`로 Spotlight 색인도 막음. /Library 이동만으로는 안 풀렸음. 11/17 저녁 get.sh/Updater를 /Library 설치 기준으로 바꿈(번들 폴더만 sudo로 만들고 사용자 소유로 두어 Contents 교체는 암호 없이; `Updater.replaceable` = Input Methods 안의 쓸 수 있는 번들). 바이너리를 직접 실행하거나 imklaunchagent를 죽이지 말 것; 정상 여부는 `process == "KoEnJa"`의 "Activate Server"로 확인.

## 4. 반복 절차 (명령)

### 테스트
```sh
cd Packages/SeamtypeCore && swift test                                 # Swift 엔진(559)
cd Android && ./gradlew -q :engine:test --tests '*GoldenTest*'          # Kotlin 골든
sh Windows/engine/build.sh                                              # clang 골든
clang++ -std=c++20 -O2 -Wall -ffp-contract=off Windows/engine/src/*.cpp Windows/engine/test/session_test.cpp -o .build/windows-engine/session_test \
  && .build/windows-engine/session_test Packages/SeamtypeCore/Sources/SeamtypeCore/Data
```
골든 재생성(엔진 동작이 바뀌었을 때만): `cd Android/tools/golden && swift run -c release golden ../../engine/src/test/resources/golden_input.txt ../../engine/src/test/resources/golden.tsv`. golden_input은 **빈 줄 외 모든 줄을 키로 침**(주석 불가, `#reset`은 새 필드). Swift/Kotlin/clang/MSVC(CI) 넷이 한 줄도 다르면 안 됨.

### Codex 리뷰(모든 변경에 1회)
`codex exec --sandbox read-only -C <repo> --skip-git-repo-check -o out.md "<프롬프트>"`. codex는 nvm 아래(`~/.nvm/versions/node/*/bin/codex`) — 에이전트 셸에 PATH가 없으면 직접 경로로.

### Mac 릴리스 (0.3.14)
```sh
cd Mac && xcodegen && M=$PWD/../.build/mac0314 && mkdir -p $M
xcodebuild archive -project KoEnJaMac.xcodeproj -scheme KoEnJa -configuration Release -archivePath $M/KoEnJa.xcarchive -allowProvisioningUpdates
xcodebuild -exportArchive -archivePath $M/KoEnJa.xcarchive -exportOptionsPlist export.plist -exportPath $M/export   # 공증 업로드
while ! xcodebuild -exportNotarizedApp -archivePath $M/KoEnJa.xcarchive -exportPath $M/notarized 2>/dev/null; do sleep 30; done
xcrun stapler validate $M/notarized/KoEnJa.app && spctl -a -vv $M/notarized/KoEnJa.app
sh Mac/lsclean.sh
# zip ×2 + KoEnJa{,-<ver>}.pkg (Mac/release.sh; Mac/pkg/postinstall, Mac/Distribution.xml). 서명·공증은 환경 변수가 있을 때만:
KOENJA_PKG_SIGN="Developer ID Installer: CIZMO, K.K. (N723QM4R64)" KOENJA_NOTARY_PROFILE=koenja zsh Mac/release.sh $M/notarized/KoEnJa.app $M/release
```
**pkg 서명 준비(1회, 사용자)**: Xcode → Settings → Accounts → CIZMO, K.K. → Manage Certificates → + → **Developer ID Installer**(Account Holder만 만들 수 있음) → `security find-identity -v -p basic`에 "Developer ID Installer: CIZMO, K.K." 확인. 공증 자격: ASC → Users and Access → Integrations → App Store Connect API → Team Keys → 생성(Developer) → .p8 내려받아 `xcrun notarytool store-credentials koenja --key <p8> --key-id <ID> --issuer <ISSUER>`(또는 Apple ID + 앱 암호). 둘이 없으면 pkg는 미서명(Gatekeeper가 거절) → 릴리스에 올리지 말 것. zip은 Xcode 경로로 이미 공증됨.
**설치 모델**: pkg도 get.sh도 `/Library/Input Methods/KoEnJa.app`을 **사용자 소유**로 둠(postinstall이 chown) → 앱 안의 Updater가 암호 없이 zip으로 교체. 교체가 안 되는 복사본(루트 소유, App Management 거부 = get.sh "can't be changed from here")은 Updater가 `KoEnJa.pkg`를 ~/Downloads에 받아 `pkgutil --check-signature`(Developer ID Installer + 공증, 팀 N723QM4R64) 확인 뒤 Installer로 연다(Araso 방식). 제거: 영수증(`com.cizmo.inputmethod.KoEnJa.pkg`)이 있으면 Uninstaller가 관리자 경로로 지우며 `pkgutil --forget`.
**규칙**: Mac 업데이터는 "최신 릴리스 태그"를 자기 버전과 비교함 → **Mac 빌드 없는 새 태그 금지**. 다른 플랫폼만 바뀌면 현재 릴리스에 에셋 추가.

### Windows
push하면 `.github/workflows/windows.yml`이 MSVC 골든 + `KoEnJa-Setup-<Version.rc>.exe` 아티팩트 생성. 받기: `gh run download -R studioCIZMO/KoEnJa -n KoEnJa-Setup`. v* 태그에 릴리스가 없을 때만 CI가 릴리스를 만듦(우리는 손으로 만들어 CI가 건너뜀).

### iOS
```sh
cd iOS && xcodegen
xcodebuild archive -project REA.xcodeproj -scheme REA -configuration Release -destination 'generic/platform=iOS' -archivePath ../.build/ios/REA.xcarchive -allowProvisioningUpdates
xcodebuild -exportArchive -archivePath ../.build/ios/REA.xcarchive -exportOptionsPlist export.plist -exportPath ../.build/ios/export -allowProvisioningUpdates   # ASC 업로드
```
이후 ASC(사용자 로그인 상태의 브라우저)에서 새 버전 1.0.6 만들기 → 빌드 선택 → whatsNew(ko/ja/en, store-notes.md) → 심사 제출. **제출 전 사용자 확인.**

### Android
```sh
cd Android && ./gradlew -q bundleRelease assembleRelease
# app/build/outputs/bundle/release/app-release.aab → 사용자가 Play Console(관리형 게시)에 업로드
# app/build/outputs/apk/release/app-release.apk → KoEnJa-Android-0.2.6.apk로 GitHub 릴리스에
```
서명 설정은 `~/.koenja/upload.properties`를 읽음(build.gradle.kts 참조).

### GitHub 릴리스 + 푸시
```sh
T=$(gh auth token -u studioCIZMO)
git -c credential.helper= -c "credential.helper=!f(){ echo username=x-access-token; echo password=$T; };f" push -q origin main
gh release create v0.3.13 -R studioCIZMO/KoEnJa --target main --title "KoEnJa 0.3.13 — 전환 키로 고른 읽기 학습 (Mac)" \
  --notes-file docs/release/0.3.13-notes.md KoEnJa-Mac.zip KoEnJa-Mac-0.3.13.zip KoEnJa-Android-0.2.6.apk KoEnJa-Setup-0.3.2.exe
```
릴리스 후: README(ko/ja/en)의 버전 줄, cizmo-renewal 사이트 버전 줄 갱신.

### Araso 재동기화
`Packages/SeamtypeCore/UPSTREAM`, `Mac/UPSTREAM` 첫 줄이 동기화된 sha. 파일별 `git merge-file ours base(upstream old) theirs(upstream new)`, 경로 `Sources/ArasoCore`→`SeamtypeCore`, `Sources/Araso`→`Mac/Sources`. 충돌은 보통 REA 표시(`REA:`) 부분(KoreanModel 해시 리스트, Resources, Updater noMac). 포트(Kotlin/C++)는 git 머지가 아니라 **골든이 바뀐 변경만 손으로** 포팅. UPSTREAM 파일에 로컬 변경 목록이 있음.

## 5. 남은 일 (우선순위순)

1. ~~Codex 리뷰 1회 → 지적 수정 → 테스트 재실행~~ 11/18 완료: 지적 1건(Kotlin/C++ `startConversion`이 `switchLearned`를 지워 변환 취소 후 전체 단어를 학습) 수정, 세 포트에 회귀 테스트(ConversionAfterSwitchTests / JapaneseSessionTest / session_test) 추가. Swift 560, Kotlin 전부, C++ 171/171, 골든 1950 통과.
2. ~~Mac 0.3.13 공증 빌드 → Android 0.2.6 AAB/APK → CI Setup 0.3.2 → GitHub v0.3.13 릴리스~~ 11/18 완료. 빌드 뒤 `sh Mac/lsclean.sh` 잊지 말 것.
3. ~~iOS 1.0.6~~ 11/18 심사 제출 완료(審査待ち): 1.0.6 (16), ko/ja/en 이름·부제·키워드·프로모션·설명·whatsNew를 docs/store/listing-appstore.md대로 갱신(스크린샷은 1.0.5 것 유지), 앱 컴플라이언스(중국 본토 보안 평가·AIGC) 둘 다 "No"로 신고(有効). 주의: ASC 로캘 메뉴에서 JS 클릭으로 항목을 고르면 엉뚱한 로캘이 추가될 수 있음(정확한 텍스트 매칭으로 클릭). Business → Agreements → Compliance → China Mainland 확인은 11/19 사용자가 완료.
4. ~~Android AAB Play 업로드 + Play 노트~~ 11/18 제출, **11/23 전체 공개**(0.2.7 (9) + ko/ja/en 등록정보 9건, 관리형 게시에서 한 번에). cizmo.net 카드·llms.txt·뉴스(11/23) 갱신, README 3종 APK 이름 0.2.7. 11/18 제출 내역: 프로덕션 8 (0.2.6)과 스토어 등록정보 개정(ko 문안 개정, ja/en 등록정보 신설, 스크린샷 7장 세트 ×3 언어)을 함께 심사에 보냄(관리형 게시). 문안 원본: docs/store/listing-play.md, App Store용 docs/store/listing-appstore.md.
5. ~~README 3종, cizmo-renewal 버전 줄~~ 11/18 완료(iOS 1.0.6·Play 0.2.6이 나오면 다시).
6. **오타 고치기(SlipGuesser) 모바일 이식 — 사용자 승인(11/18)**: 역할 분담은 "스페이스 자동 교정은 기존 `Suggestions`, 전환 키 길게 누름 후보는 SeamtypeCore `SlipGuesser`". iOS 먼저: 한あA 길게 누름(0.5 s) → `Key.guess`, `session.guesses`를 후보 바에 표시(바뀐 글자 강조)·탭으로 `pickGuess`, `fixSlips` 설정 토글(기본 꺼짐) + ko/ja/en 문구, `InputSession` 생성 시 `fixSlips` 전달. 그다음 Android: `SlipGuesser` Kotlin 이식(약 400줄 + `KoreanModel.vocabularyCount`/`knowsSyllableIncludingRare`, SlipTests 이식), Windows C++도 동일, 골든에 오타 케이스 추가, 앱의 길게 누름·후보 바·설정.
7. **학습 스위치(11/23, Codex 최종 리뷰 P1)**: `DictionaryEngine.learning`(Swift/Kotlin/C++)과 변환기 `learning`(AzooKeyConverter/MozcConverter, C++는 베이스 플래그만)을 비밀번호·OTP 칸에서 false로. iOS `applySensitive()`(세션 재생성 뒤에도), Android `Typist.sensitive`(서비스가 start 전에 설정) + 추천·자동 고침 생략. 골든 영향 없음(테스트: ConversionAfterSwitchTests / UserDictionaryTest / session_test). 남은 리뷰 지적: Windows TIP은 아직 비밀번호 필드(GUID_PROP_… / IS_PASSWORD)를 엔진에 전달하지 않음 → 다음 Windows 릴리스에서 `engine->learning` 연결.
8. **iOS 터치 오타(ㅜㅡㅏㅣ 쪽) 조정 — 11/23**: 사용자 아이폰은 스토어 빌드라 trace.log(DEBUG 전용)도 App Group 학습 데이터도 꺼낼 수 없음(devicectl은 개발자 서명 앱 컨테이너만). 대신 1.0.7에 ⌫ 교정 학습(`learnFromBackspace`: 글자 → ⌫ → 이웃 키, 첫 터치가 그 키 쪽으로 1/4 이상 치우친 경우만 `TouchCalibration` 샘플; `neighborReach` 60 pt×scale)을 넣음. 8샘플부터 키 영역이 이동(최대 30%). 수치 조정이 더 필요하면 Debug 빌드를 실기기에 깔아 `TOUCH`/`NEAR`/`LEARN-BS` 트레이스를 모아 `touchShift`·`correctionShift`를 맞출 것. 키 라벨은 영문·한글 17 pt semibold 둘 다(42 pt 키에 두 줄 한계). **오타 기록(SlipLog)**: 릴리스 빌드에서도 App Group `slips.log`에 SLIP(⌫ 재입력: 잘못 누른 키→맞는 키, 잘못 누른 키 중심 기준 dx dy, toward, learned 여부)·NEAR(틈 터치 재배정)·EDGE(가장자리 보정 적용)·PICK/FIX(후보·자동 고침)·LEARN을 기록(비밀 입력란 제외, 256 KB 넘으면 앞 절반 삭제). 앱 오타 도움 → 오타 기록에서 키 쌍 집계·공유(ShareLink)·비우기. **가장자리(11/23)**: 시스템 키보드 측정(UITests/HitAreas `testSystemKeyboardEdges`, TEST_RUNNER_HIT_AREAS=1): a·l·p는 화면 끝(x=2, 400)까지 자기 키, 틈 한가운데는 무입력. 우리 것도 `KeyStack.ownsMargin`으로 줄 끝 키가 옆 여백을 가짐(단위 테스트 `testMarginsBelongToEndKeys`). **개인 교정(PersonalFixes, 11/23)**: 단어의 최장 키열(`longestKeys`, `follow`가 ⌫ 동안 유지)과 Space 시점 키열이 편집거리 ≤2·서로 접두어 아님이면 기록, 3회부터 `personalFix()`가 Space 교정·후보 첫째(한국어는 compose, 일본어·연락처 이름 제외). App Group `personalFixes`, 앱 오타 기록 → 내가 고친 단어에서 삭제. 다음 단계 후보: 적용된 교정을 바로 ⌫하면 거부(veto) 학습, 교정 적용 횟수 기록. 사용자가 TestFlight로 쓰다 공유해 주면 SLIP 줄의 dx/dy 분포로 ㅏㅣㅜㅡ(k l n m) 쪽 `touchShift`·`correctionShift`·키별 기본 오프셋을 정할 것.
9. **GitHub 이슈 #1(11/18, Windows 0.3.2, LINE)**: 한국어 단어 뒤 Space/Enter가 앱에 안 들어가고 확정 상태로 남음. 원인: OnTestKeyDown이 eaten=TRUE(단어를 먼저 넣어야 하므로), OnKeyDown이 eaten=FALSE로 통과시키는데, IMM32로 TSF를 쓰는 앱(CUAS; LINE은 Qt)은 그 키를 VK_PROCESSKEY로 받아 잃음(Chromium/WinUI 앱은 괜찮음). 수정(0.3.4): Araso TIP처럼 통과 키를 eaten=TRUE로 먹고 커밋 뒤 SendInput으로 재전송(`Tip::replay`; 포그라운드 창이 이 프로세스일 때만 — tip_test 하네스는 창이 없어 예전 경로 유지), 되돌아온 키는 1초 안·입력 중 아님이면 통과(`isReplayed`, OnKeyDown에서 잊음). 실제 LINE에서는 미확인 → 11/24 v0.3.14 공개 후 이슈에 0.3.4 확인 요청 댓글 게시(사용자 승인). cizmo.net LP·llms.txt·뉴스 11/24 갱신(136be84). 보류: GitHub 릴리스에 남아 있는 옛 APK(0.2.4) 교체 여부; Threads 홍보 글은 사용자가 올림; Mac 입력기 회색 현상 재발 추적.
10. 열린 품질 이슈: iOS 시뮬레이터 UI TypingTests 불안정(실기기/TestFlight로 판단), Mozc 변환 순위(kinou→機能 등), 포트에 없는 기능(fixing, readings 화살표, 앱별 기억). 경쟁앱 Kej 분석에서 가져올 것: 키보드 높이·위치, 키 간격, 근접키 보정, Space 스와이프 후보 선택, 설정 백업.

## 6. Codex 리뷰 프롬프트 요지 (이 커밋용)

읽기 전용으로 `git show HEAD`를 보고: (1) Kotlin/C++ `learnSwitchBeforeMore`의 Swift 대비 호출/리셋 지점(letter·Japanese-phrase·keepsInWord append 앞; new word, cancelComposition, startConversion, showKanaForms, finishWord의 리셋; Cycle 핸들러 dropNMarker 뒤 `switchedKeys = keys`; wordEnds에서 switchLearned면 record 생략), (2) Mac Updater `finishCheck(release, failed:, noMac:)`의 `guard state == .checking`/menuDone/키 삭제, Uninstaller receipt 로직(Predecessor.all 비어 있음), (3) 추가 골든 케이스가 새 학습 경로를 타는지, (4) 버전 번호. 업스트림 비교: `nyttastudio/araso` `git diff 9daba56 b19e054 -- Sources/ArasoCore/InputSession.swift`.
