# KoEnJa(한영일) 인수인계 — 2026-11-16

다른 PC에서 이 프로젝트를 이어가기 위한 문서입니다. 코드로 알 수 있는 내용은 생략하고,
코드 밖의 상태·절차·규칙만 적습니다. 에이전트(Claude/Codex)용 규칙은 [CLAUDE.md](CLAUDE.md)에 있습니다.

## 1. 지금 상태 (main 093e48d + 이 커밋)

| 플랫폼 | 공개 버전 | 이 커밋의 버전 | 상태 |
|---|---|---|---|
| Mac 입력기 | 0.3.12 (GitHub v0.3.12, Latest) | **0.3.13 (16)** | 빌드·공증·릴리스 **아직 안 함** |
| iOS 키보드 | 1.0.5 (App Store) | **1.0.6 (16)** | 아카이브·업로드 **아직 안 함** |
| Android 키보드 | 0.2.5 (Play), APK 0.2.5 | **0.2.6 (8)** | bundleRelease **아직 안 함** |
| Windows 입력기 | 0.3.1 (GitHub) | **0.3.2** | push 시 CI가 Setup exe 빌드 |

이 커밋 = **Araso main b19e054 동기화** (0.6.1 → main 3커밋, 2026-11-14):
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
- 클라우드 루틴(claude.ai/code/routines): `trig_017ohTsEDwDRCaas6G3ycafQ` 월·목 10:00 JST Araso 새 커밋 검토 → `araso-sync/<sha>` 브랜치에 DRAFT PR; `trig_01EHTadrwBwMTnnDPEkrMrKb` 월 09:00 JST 이슈·다운로드 집계 보고(읽기 전용).

## 3. 개발 환경

- Mac(현재): macOS 26, Xcode 27.2(/Applications/Xcode.app), Android Studio/SDK, XcodeGen. 테스트 AVD **REA_Pixel**(REA_Play는 경쟁앱 분석용, `checkame`는 다른 프로젝트 — 건드리지 말 것).
- Xcode 프로젝트는 생성물: `cd iOS && xcodegen`, `cd Mac && xcodegen`(project.yml). `.gitignore`에 있음.
- Android `local.properties`(sdk.dir)는 PC별로 새로 만듦.
- Windows 테스트 PC: Windows 11 x64, LAN 192.168.4.71(바뀔 수 있음), 사용자 `netma`, SSH 키 `~/.ssh/rea_windows`(이 Mac에만 있음). git 없음 → `git archive` tar를 scp. VS 2022 BuildTools, Inno Setup 6. 설치 빌드는 `C:\Program Files\KoEnJa\<ver>`. 바탕화면은 OneDrive 동기화 → 바이너리 복사 금지.
- Mac 입력기 로컬 설치: `KOENJA_ZIP=<zip> sh Mac/get.sh`. **입력 메뉴에서 회색으로 죽는 현상(11/02, 11/13, 11/17 재발)**: `/usr/bin/log show --last 10m --predicate 'process == "imklaunchagent" AND messageType == error'`에 `LaunchInputMethod() status=-50` / `Refusing connection name for bundle: unrecognized 'InputMethodConnectionName' value` / `Request for Endpoint Invalid`가 찍히면 그 상태. 앱 서명·공증·LS 등록·폴더 권한·Info.plist는 모두 정상이었고, 입력 소스 재추가·imklaunchagent 재시작(kill)·`open -b` 수동 실행으로는 안 풀림. **재부팅만 복구**. 11/17부터 실험으로 KoEnJa.app을 `/Library/Input Methods`(Araso와 같은 곳, 거기선 한 번도 안 죽음)로 옮겨 둠(`sudo mv`). 이 상태에서는 Updater가 자동 설치 대신 릴리스 페이지를 연다(`Updater.replaceable`). 재발하지 않으면 get.sh/문서를 /Library 설치로 바꿀 것. 바이너리를 직접 실행하거나 imklaunchagent를 죽이지 말 것; 정상 여부는 `process == "KoEnJa"`의 "Activate Server"로 확인.

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

### Mac 릴리스 (0.3.13)
```sh
cd Mac && xcodegen && M=$PWD/../.build/mac0313 && mkdir -p $M
xcodebuild archive -project KoEnJaMac.xcodeproj -scheme KoEnJa -configuration Release -archivePath $M/KoEnJa.xcarchive -allowProvisioningUpdates
xcodebuild -exportArchive -archivePath $M/KoEnJa.xcarchive -exportOptionsPlist export.plist -exportPath $M/export   # 공증 업로드
while ! xcodebuild -exportNotarizedApp -archivePath $M/KoEnJa.xcarchive -exportPath $M/notarized 2>/dev/null; do sleep 30; done
xcrun stapler validate $M/notarized/KoEnJa.app && spctl -a -vv $M/notarized/KoEnJa.app
(cd $M/notarized && ditto -c -k --keepParent KoEnJa.app ../KoEnJa-Mac-0.3.13.zip && cp ../KoEnJa-Mac-0.3.13.zip ../KoEnJa-Mac.zip)
```
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

1. Codex 리뷰 1회 → 지적 수정 → 테스트 재실행.
2. Mac 0.3.13 공증 빌드 → Android 0.2.6 AAB/APK → push → CI Setup 0.3.2 받기 → GitHub v0.3.13 릴리스(사용자 확인 후 공개).
3. iOS 1.0.6 아카이브·업로드 → ASC 버전·whatsNew·심사 제출(사용자 확인).
4. Android AAB Play 업로드(사용자) + Play 노트(store-notes.md ko/ja/en).
5. README 3종, cizmo-renewal 버전 줄.
6. 보류: GitHub 릴리스에 남아 있는 옛 APK(0.2.4) 교체 여부; Threads 홍보 글은 사용자가 올림; Mac 입력기 회색 현상 재발 추적.
7. 열린 품질 이슈: iOS 시뮬레이터 UI TypingTests 불안정(실기기/TestFlight로 판단), Mozc 변환 순위(kinou→機能 등), 포트에 없는 기능(fixing, readings 화살표, 앱별 기억). 경쟁앱 Kej 분석에서 가져올 것: 키보드 높이·위치, 키 간격, 근접키 보정, Space 스와이프 후보 선택, 설정 백업.

## 6. Codex 리뷰 프롬프트 요지 (이 커밋용)

읽기 전용으로 `git show HEAD`를 보고: (1) Kotlin/C++ `learnSwitchBeforeMore`의 Swift 대비 호출/리셋 지점(letter·Japanese-phrase·keepsInWord append 앞; new word, cancelComposition, startConversion, showKanaForms, finishWord의 리셋; Cycle 핸들러 dropNMarker 뒤 `switchedKeys = keys`; wordEnds에서 switchLearned면 record 생략), (2) Mac Updater `finishCheck(release, failed:, noMac:)`의 `guard state == .checking`/menuDone/키 삭제, Uninstaller receipt 로직(Predecessor.all 비어 있음), (3) 추가 골든 케이스가 새 학습 경로를 타는지, (4) 버전 번호. 업스트림 비교: `nyttastudio/araso` `git diff 9daba56 b19e054 -- Sources/ArasoCore/InputSession.swift`.
