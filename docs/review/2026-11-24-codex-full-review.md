릴리스 보류를 권고합니다. 기본 엔진 검사는 모두 통과했고 현재 Swift로 재생성한 골든도 Kotlin·C++가 통과한 1,950줄 기준 파일과 완전히 같지만, Windows 재전송 판정·조합 대상 관리, iOS 선택 영역 처리, 숫자가 포함된 일본어 변환에서 문자 손실 또는 오입력 경로가 남아 있습니다. 민감 필드의 학습 차단과 설치 실패 처리도 보완이 필요합니다. 검토 기준은 종료 시점 `main`의 `ca2807af25f9cb6b19f9b3457c009bde59962164`와 미커밋 README 3종·`Mac/vim` 등이며, 실제 Windows/LINE·iOS·Android 기기 및 설치 프로그램은 실행하지 않았으므로 해당 환경에서의 발생 여부를 확인하지 못한 시나리오는 아래에 **추정**으로 표시했습니다.

1. **높음 — 재전송의 1초 fallback이 다음 실제 키까지 재전송으로 판정합니다.**

   `Windows/tip/Tip.cpp:725`, `Windows/tip/Tip.cpp:1175`, `Windows/tip/Tip.cpp:1342`: `OnTestKeyDown`이 재전송에 `FALSE`를 반환하면서 `replayed`를 남기는데, 표준 TSF 메시지 루프는 이때 `OnKeyDown`을 호출하지 않으므로 그곳의 초기화가 실행되지 않습니다. 이후 같은 VK·Ctrl/Alt 상태의 실제 키도 1초 동안 엔진을 우회합니다. 세벌식 최종에서 `hello;` 뒤 1초 안에 다시 `;`와 `f`를 치는 경우, 정상 엔진 결과 `hello;바`가 fallback 우회 시 `hello;;f`가 되는 것을 엔진 재생으로 확인했습니다. 또한 다른 키가 먼저 도착하면 토큰까지 무시하게 되어 늦게 도착한 주입 키를 다시 처리할 수 있습니다(네이티브 이벤트 순서에 따른 재주입은 **추정**). 토큰은 pending VK·시간 조건과 독립적으로 검사하고, 토큰이 없는 fallback은 실제 메시지 하나의 식별 정보로 제한해 다음 물리 키와 구별해야 합니다. [TSF KeyDown 호출 조건](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-keydown).

2. **높음 — iOS에서 선택한 텍스트를 기존 조합의 일부로 오인해 삭제합니다.**

   `iOS/Keyboard/ProxyClient.swift:57`, `iOS/Keyboard/ProxyClient.swift:78`, `iOS/Keyboard/ProxyClient.swift:100`, `iOS/Keyboard/KeyboardViewController.swift:256`: `selectedRange()`가 항상 길이 0을 반환하고 `resetIfMovedAway()`는 선택 영역을 보지 않고 앞 텍스트의 suffix만 비교합니다. 실제 `ProxyClient`를 프록시 대역과 연결한 검사에서 `가X`의 `가`가 조합 상태일 때 뒤의 `X`를 선택하면 reset이 발생하지 않았고, 다음 조합 `간`을 쓰면서 `X`를 지워 결과가 `가간`이 됐습니다. `selectedText`가 있는 경우 기존 조합 상태를 버리고 선택 영역을 새 입력으로 교체하도록 처리하며, 프록시가 제공하는 선택 길이를 엔진에 전달해야 합니다.

3. **높음 — TSF context가 바뀌어도 이전 필드의 조합 범위를 새 필드에 적용할 수 있습니다.**

   `Windows/tip/Tip.cpp:714`, `Windows/tip/Tip.cpp:1218`, `Windows/tip/Tip.cpp:1251`: `OnPushContext`·`OnPopContext`가 아무 일도 하지 않고, `markedRange()`는 전역 `composition`에서 얻은 위치를 현재 `Client.context`의 범위인 것처럼 사용합니다. 같은 document manager에서 조합을 유지한 채 새 context를 push하고 동일한 ACP 위치에서 입력받는 호스트에서는 이전 단어가 이어지거나 새 필드의 문자가 덮어써질 수 있습니다(**추정**, 네이티브 호스트 미실행). context push/pop에서도 기존 조합을 원래 context에 확정하거나 분리하고, 모든 편집 전에 composition의 context와 대상 context의 동일성을 확인해야 합니다. [OnPushContext의 대상](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfthreadmgreventsink-onpushcontext).

4. **높음 — 일본어 숫자 구절에서 F9/F10을 누르면 앞 숫자가 사라집니다.**

   `Windows/tip/Tip.cpp:1303`, `Windows/engine/src/InputSession.cpp:764`, `Windows/engine/src/InputSession.cpp:834`: 숫자를 앞 필드에서 흡수한 변환은 `numberPrefix=3`, `keys=ji`, `kana=3じ`로 보관되지만, F9/F10이 먼저 호출하는 `Katakana` 처리에서 `numberPrefix`를 잃고 TIP은 `c->keys`만 폭 변환합니다. 일본어 단어 확정 후 `3ji<Space>`로 `3時`를 표시한 상태에서 F10은 `ji`, F9는 `ｊｉ`로 바뀌어 `3`이 유실되는 데이터 경로를 실제 C++ 엔진으로 확인했습니다. 문자 형태를 바꾸어도 숫자 prefix와 원래 로마자를 함께 보존하고 F9/F10 대상 문자열에 숫자를 포함해야 합니다.

5. **높음 — TSF의 실제 쓰기가 실패해도 키를 먹고 엔진 상태를 진행합니다.**

   `Windows/tip/Tip.cpp:1201`, `Windows/tip/Tip.cpp:1259`, `Windows/tip/Tip.cpp:1338`, `Windows/engine/src/InputSession.cpp:141`: `range()` 또는 `SetText()` 실패를 `Client.replace()`가 조용히 반환하며, 편집 잠금을 받았다는 사실만으로 `edited=true`가 됩니다. 엔진은 문자를 추가하고 `true`를 반환하므로, 예를 들어 `ITfRangeACP`를 제공하지 않는 anchor 기반 text store에서는 문자가 들어가지 않아도 원래 키가 소비됩니다(그러한 실제 앱의 발생 범위는 **추정**). adapter의 쓰기 실패를 `perform()`까지 전달해 키를 통과시키고 상태를 복구하거나, ACP가 없는 호스트에서는 조합을 시작하기 전에 우회해야 합니다.

6. **중간 — 재전송 대상이 원래 입력을 받은 창인지 확인하지 않습니다.**

   `Windows/tip/Tip.cpp:1154`, `Windows/tip/Tip.cpp:1357`: `replay()`는 전송 시점의 foreground PID만 현재 프로세스와 비교하고, 원래 HWND·thread·context는 보관하지 않습니다. 조합 확정의 `SetText` 콜백이 같은 프로세스의 다른 창이나 대화상자로 초점을 옮기면 원래 필드용 Enter/Tab이 새 창으로 주입될 수 있으며, PID 검사 뒤 다른 프로세스로 초점이 바뀌는 시간차도 남습니다(**추정**, 초점 변경 실험 미실행). 원래 대상과 키 상태를 보관하고 재전송 직전 context·HWND 변경 시 취소하도록 하며, 전역 입력 주입에서 대상 창을 보장할 수 없는 경로에는 별도 처리가 필요합니다. [SendInput은 전역 입력 스트림에 삽입](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).

7. **중간 — 데스크톱의 민감 필드 보호가 학습·로그까지 연결되지 않습니다.**

   `Mac/Sources/InputController.swift:162`, `Mac/Sources/InputController.swift:235`, `Packages/SeamtypeCore/Sources/SeamtypeCore/AppleJapaneseConverter.swift:145`, `Windows/tip/Tip.cpp:1009`, `Windows/tip/Tip.cpp:820`: Mac은 Secure Event Input·caret 조건과 터미널 비밀번호 프롬프트만 우회하고, 일반 caret를 제공하는 일회용 코드 필드에는 학습을 끄는 처리가 없으며 Apple converter에도 학습 차단 플래그가 없습니다. Windows 역시 keyboard-disabled/empty-context만 검사하며 엔진·Mozc의 `learning`을 끄지 않고, `KeyLog`가 켜져 있으면 `disabled()` 때문에 입력을 통과시키는 경우에도 VK를 파일에 씁니다. 따라서 일반 텍스트 형태의 인증 코드, 또는 disabled compartment 없이 민감 input scope만 알리는 호스트에는 무학습 보장이 없습니다(각 호스트의 실제 노출 조건은 **추정**; Windows 학습은 13번 문제로 현재 대부분 메모리에만 남습니다). 플랫폼에서 얻을 수 있는 민감 필드 정보를 하나의 상태로 전달해 모든 학습·후보 기록·진단 로그를 함께 차단하고, 식별할 수 없는 필드의 정책도 명시해야 합니다.

8. **중간 — 세 엔진 모두 `learning=false`에서도 기존 학습 사전을 수정합니다.**

   `Packages/SeamtypeCore/Sources/SeamtypeCore/UserDictionary.swift:649`, `Android/engine/src/main/kotlin/com/netmaru/rea/engine/UserDictionary.kt:234`, `Windows/engine/src/UserDictionary.cpp:267`: `learn()`에는 차단 조건이 있지만 `used()`에는 없어 기존 weak 항목의 사용 날짜 갱신 등을 그대로 저장합니다. 임시 사전을 사용한 C++ 검사에서 `learning=false` 후 `used(so, KOREAN)`만 호출해도 `so\tkorean\tlearned\tweak\t2026-11-23`이 `2026-11-24`로 바뀌었으며, iOS·Android의 민감 필드 설정도 이 경로를 막지 못합니다. `used()`와 Swift의 `noteApplied()` 등 사용 기록을 남기는 경로에도 차단 조건을 적용하고, 무학습 상태에서는 메모리와 파일 모두 바뀌지 않는지 확인해야 합니다.

9. **중간 — iOS 민감 필드에서도 터치 위치에 따른 글자 자동 변경이 실행됩니다.**

   `iOS/Keyboard/KeyboardViewController.swift:580`, `iOS/Keyboard/KeyboardViewController.swift:730`: `autocorrect()`·추천·터치 학습에는 `sensitive` 검사가 있지만, 모든 문자 키가 통과하는 `corrected()`의 guard에는 없습니다. 영문 인증 코드를 키 가장자리에서 입력하면 확률이 높은 이웃 키로 바뀔 수 있으며, 코드에 예시로 든 `fgem`→`fhem` 교정도 같은 경로입니다(실기기 터치 재현 미실행). `corrected()`에서 `!sensitive`를 먼저 확인해 민감 필드에는 눌린 문자 그대로 전달해야 합니다.

10. **중간 — Windows Caps Lock 입력이 Swift의 같은 물리 키 입력과 다른 언어가 됩니다.**

   `Windows/tip/Tip.cpp:1081`, `Mac/Sources/InputController.swift:540`, `Packages/SeamtypeCore/Tests/SeamtypeCoreTests/CapsLockTests.swift:43`: Windows는 Shift XOR Caps Lock으로 만든 대문자를 그대로 엔진에 넣지만 Swift는 Caps Lock과 Shift를 분리해 판단 후 영어 표시에만 대문자를 적용합니다. C++ 검사에서 `rjatk `는 `검사 `, TIP의 Caps Lock 경로에 해당하는 `RJATK `는 그대로 `RJATK `였고 `dkssudgktpdy `도 같은 차이가 났습니다. Caps Lock 상태를 별도로 전달하는 Swift의 처리를 포팅하되, 실제 Shift로 입력한 대문자와 구별하고 두 상태를 골든 또는 adapter 검사에 추가해야 합니다.

11. **중간 — C++·Kotlin의 변환 중 Delete가 grapheme 대신 code point 하나만 지웁니다.**

   `Windows/engine/src/InputSession.cpp:915`, `Android/engine/src/main/kotlin/com/netmaru/rea/engine/InputSession.kt:984`, `Packages/SeamtypeCore/Sources/SeamtypeCore/InputSession.swift:3024`: C++·Kotlin은 surrogate pair만 고려하지만 Swift는 `String.dropLast()`로 grapheme 전체를 삭제합니다. Windows F8의 반각 변환 결과 `ｶﾞ`에서 Delete를 누르는 엔진 재현은 `ｶ`를 남겼고, Swift에서는 `ｶﾞ`가 한 Character라 전체가 지워졌습니다. NFD의 `か`+결합 탁점에도 같은 차이가 있으므로 grapheme 경계 기준으로 삭제하되 text-store 위치의 UTF-16 단위는 유지해야 합니다.

12. **중간 — iOS 1.0.7에서 남은 오타 로그는 업그레이드해도 백업 제외되지 않습니다.**

   `iOS/Shared/SlipLog.swift:22`, `iOS/Shared/SlipLog.swift:31`, `iOS/Shared/SlipLog.swift:35`: 백업 제외 속성은 파일 신규 생성 또는 256 KiB 초과 trim 때만 설정됩니다. `d780bea`의 1.0.7 구현이 만든 기존 `slips.log`에는 이 속성이 없음을 확인했으며, 1.0.8로 올린 뒤 계속 작은 로그에 append만 하면 교정 원문이 든 파일은 백업 제외되지 않은 채 남습니다. 파일을 처음 열거나 앱을 업그레이드할 때 기존 로그에도 제외 속성을 적용하고, 속성 설정 실패 시 다음 접근에서 재시도해야 합니다.

13. **중간 — Windows에서 배운 단어·한자 선택이 세션을 넘겨 저장되지 않습니다.**

   `Windows/tip/Tip.cpp:203`, `Windows/engine/src/Engines.h:34`, `Windows/japanese/MozcConverter.cpp:331`, `Windows/japanese/MozcConverter.cpp:373`: TIP이 `Engines::makeSession()`에 사전 경로를 넘기지 않아 단어 학습은 메모리에만 있고, `japanese.txt` 경로를 받은 Mozc converter도 production TIP에서 `save()`를 호출하는 곳이 없으며 destructor는 기본 구현입니다. 앱을 닫거나 언어 순서 변경으로 session을 교체하면 배운 결과가 사라집니다. 사용자 사전 경로를 연결하고 converter의 저장 수명을 정하되, 여러 앱 프로세스가 함께 저장하므로 읽기·병합·원자적 교체를 함께 설계해야 합니다.

14. **중간 — Windows 키 로그가 고정 버퍼 밖의 메모리를 파일에 씁니다.**

   `Windows/tip/Tip.cpp:833`, `Windows/tip/Tip.cpp:842`: `snprintf()`의 반환값은 잘리지 않았을 때 필요한 길이인데 이를 400바이트 버퍼의 실제 길이처럼 `WriteFile()`에 넘깁니다. 동일 포맷과 255자 실행 파일 basename으로 검사한 반환값은 412여서 `KeyLog`가 켜진 해당 조건에서 스택 버퍼 뒤 12바이트를 읽어 로그에 쓸 수 있습니다(실제 범위 밖 읽기는 실행하지 않음). 쓰기 길이를 `sizeof(line)-1` 이하로 제한하거나 필요한 길이만큼 동적 버퍼를 사용해야 합니다.

15. **중간 — 패키지 설치 중 강제 종료가 이후 모든 pkg 설치를 막습니다.**

   `Mac/pkg/postinstall:21`, `Mac/pkg/postinstall:25`: `.install` 디렉터리를 만든 뒤 SIGKILL·전원 종료가 발생하면 EXIT trap이 실행되지 않습니다. 다음 pkg 설치는 소유 프로세스가 없는 잠금도 매번 60초 기다린 후 실패하며, `get.sh`에만 있는 오래된 잠금 복구를 pkg에서는 하지 않습니다. 두 설치 경로에서 잠금 소유 PID·생존 여부를 공유해 고아 잠금만 복구하고, 살아 있는 설치의 잠금을 시간만으로 지우지 않도록 해야 합니다.

16. **중간 — 새 Mac 입력기 등록 실패 뒤에도 이전 사용자 사본을 삭제합니다.**

   `Mac/pkg/postinstall:65`, `Mac/pkg/postinstall:76`, `Mac/pkg/postinstall:84`: 30번의 `--register`·`--registered`가 모두 실패해도 성공 여부를 확인하지 않고 `~/Library/Input Methods/KoEnJa.app`을 등록 해제·삭제한 뒤 0으로 종료합니다. 새 사본의 등록이 실패하는 시스템에서는 기존 사본과 입력 소스 선택까지 잃을 수 있습니다(**추정**, 실제 Installer 미실행); 이는 바로 위의 “Only after registering this copy” 조건도 지키지 않습니다. 등록 성공을 명시적으로 기록해 확인된 경우에만 기존 사본을 삭제하고, 실패하면 복구 가능한 사본과 명확한 설치 결과를 남겨야 합니다.

17. **중간 — Windows 제거 시 설치했던 사용자가 아니라 관리자 계정의 언어 목록을 수정합니다.**

   `Windows/installer/KoEnJa.iss:68`, `Windows/installer/KoEnJa.iss:102`, `Windows/installer/languages.ps1:6`: 설치는 `ExecAsOriginalUser`로 시작한 사용자의 목록을 바꾸지만, 제거는 elevated uninstaller의 `[UninstallRun]`에서 현재 계정의 `Get-WinUserLanguageList`를 사용합니다. 일반 사용자가 다른 관리자 자격 증명을 입력하는 경우 DLL은 제거되지만 일반 사용자 목록에는 KoEnJa가 남습니다(**추정**, Windows 실설치 미실행). 설치 대상 사용자를 추적하고 사용자 권한의 helper 등으로 그 사용자의 목록을 정리해야 하며, `[UninstallRun]`에 지원되지 않는 `runasoriginaluser`를 붙이는 것으로는 해결되지 않습니다. [Inno Setup 실행 계정 규칙](https://jrsoftware.org/inno-setup/documentation/topic_runsection.htm), [ExecAsOriginalUser의 uninstall 제한](https://jrsoftware.org/inno-setup/documentation/topic_isxfunc_execasoriginaluser.htm).

18. **낮음 — `try.cmd`는 일반 실행에서도 제거 메시지를 출력하고 종료합니다.**

   `Windows/tip/try.cmd:12`: `if ... regsvr32 ... & echo ... & exit /b`에서 IF의 조건은 첫 명령에만 걸리고 `echo`와 `exit /b`는 항상 실행됩니다. 빌드 폴더와 관리자 권한이 정상이어도 인수 없이 실행하면 14행 이후의 등록·Notepad 실행에 도달하지 않습니다. `/u` 분기를 괄호 블록이나 별도 label로 묶어 분기 밖의 무조건 종료를 없애야 합니다.

19. **낮음 — 세 README가 모든 언어 전환 키를 길게 눌러 추측을 여는 것으로 설명합니다.**

   `README.md:165`, `README.md:247`, `README.en.md:167`, `README.en.md:263`, `README.ja.md:167`, `README.ja.md:258`: 실제 길게 누르기 처리는 `Mac/Sources/InputController.swift:306`의 오른쪽 Option·오른쪽 Shift에만 있고, Esc·Shift-Esc·Option-Space에는 해당 동작이 없습니다. 따라서 기본 전환 키인 Shift-Esc를 안내대로 길게 눌러도 추측 목록이 열리지 않습니다. 세 언어 모두 지원되는 오른쪽 modifier를 명시하고, 각 README 96행의 `Fix typos` 안내도 수동 추측은 `fixSlips=false`에서 계속 가능하다는 `Packages/SeamtypeCore/Sources/SeamtypeCore/Settings.swift:23`과 구분해야 합니다.

20. **낮음 — 세 README의 골든 수와 upstream 버전이 현재 코드와 맞지 않습니다.**

   `README.md:549`, `README.md:553`, `README.md:600`, `README.en.md:604`, `README.en.md:608`, `README.en.md:654`, `README.ja.md:578`, `README.ja.md:582`, `README.ja.md:630`: `942 × 2`와 `1,932`를 혼용하지만 현재 파일·실행 결과는 `975 × 2 = 1,950`이며, upstream도 두 `UPSTREAM` 파일의 `0.6.2 @ 97e0075`와 달리 `0.6.1`로 설명합니다. 58행의 개요와 마지막 비교표까지 세 언어를 함께 고치고, 이전 실행에서 측정한 성능 수치를 유지한다면 그 당시 테스트 규모·시점을 명시해야 합니다.

문제가 없음을 확인한 범위는 다음과 같습니다.

- Swift·Kotlin·C++의 기본 QWERTY 골든 출력은 1,950줄 모두 일치했습니다. 기존 세벌식·숫자 포함 단어·일본어 세션 검사도 통과했습니다. 이 골든에는 native text-store, Caps Lock modifier, F8/F9/F10, 선택 영역 변경이 포함되지 않아 위 문제들과 모순되지 않습니다.
- C++ clang의 `-ffp-contract=off`와 Windows CI의 `windows-2022`·`/fp:precise`는 수축 연산을 끄는 방향이 일치합니다. VS 2022의 `/fp:precise`에서는 `/fp:contract`를 따로 켜지 않는 한 FMA contraction이 기본으로 꺼져 있습니다. MSVC CI 자체는 이번 Mac 검토에서 실행하지 않았습니다. [Microsoft /fp 문서](https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior?view=msvc-170).
- Mac `Updater.swift`의 pkg 검증은 `pkgutil --check-signature` 성공, Developer ID Installer leaf, 팀 `N723QM4R64`, 공증 상태를 검사합니다. zip/get.sh 경로도 설치 전에 strict codesign·팀·Gatekeeper 검사를 하고 sudo에는 인용한 위치 인수를 전달합니다. 실제 `set -eu`를 포함한 격리 실패 주입에서 rollback의 `mv`가 실패해도 기존 `Contents.old`는 삭제되지 않았습니다.
- Mac 설정·앱 정책·사전의 원자적 저장과 읽기 실패 파일 보존, uninstaller의 삭제 순서·명시적 데이터 삭제 옵션, pkg의 rsync 제외 규칙·console user 소유권 처리·교체 실패 종료를 확인했습니다. 위에 적은 잠금·등록 실패 문제는 별도입니다.
- Android의 password variation 및 `IME_FLAG_NO_PERSONALIZED_LEARNING` 검사, session 시작 시 학습 플래그 적용, 선택 영역 변경 시 조합 초기화, 설정 백업의 사전 검증·256 KiB 한도·단축어 총량 제한을 확인했습니다. iOS의 민감 필드 분류와 일반 추천·자동 고침 차단도 확인했으며, 기존 로그를 제외한 새 slip log 생성·trim 시 백업 제외와 줄 경계를 보존하는 trim이 있습니다.
- 입력 내용을 원격 서버로 전송하는 런타임 경로는 확인되지 않았습니다. Mac의 GitHub 업데이트 조회·다운로드와 사용자 동작으로 여는 외부 링크, 빌드 시 사전 다운로드 경로를 구분해 확인했으며 Android 앱 manifest에는 INTERNET 권한이 없습니다.
- 새 Mac 설명의 주요 기본값·JSON 키·파일 경로·한국어 메뉴 문자열은 구현과 대조했습니다. 일본어·live conversion·`fixSlips`는 기본 꺼짐, `returnConfirmsOnly`는 켜짐이며, vim 두 스크립트의 시스템/사용자 설치 경로와 `--vim insert|normal|off --app` 인수는 `main.swift`와 일치합니다. Vim 스크립트 source 검사는 통과했고 Lua는 정적으로 확인했으며 Neovim 런타임은 미설치라 실행하지 않았습니다.

실행한 검사와 결과입니다.

- `cd Packages/SeamtypeCore && swift test`: exit 0, 49개 suite·561개 test 통과. `PreviewTraceTests`와 `DictionarySplitCheckTests`는 실행되지 않았으며 후자는 `tools/data missing: run tools/prepare.sh` 조건 때문입니다.
- `cd Android/tools/golden`에서 `swift run --scratch-path /Users/vincent/orca/projects/REA/.build/review-golden -c release golden <golden_input.txt 절대 경로> <임시 디렉터리>/golden.tsv`: exit 0. 현재 Swift 출력 1,950줄이 저장된 `golden.tsv`와 byte-identical. 기준 파일은 수정하지 않았습니다.
- `bash Windows/engine/build.sh`: exit 0, 1,950/1,950 통과(두 언어 순서 각각 975). 이번 clang 실행은 21,802 keys, 약 9.4 µs/key였습니다.
- `cd Android && ./gradlew :engine:test`: exit 0, UP-TO-DATE. 이어 `./gradlew :engine:test --rerun-tasks`: exit 0, 5개 task 실행, GoldenTest 3개 통과·실패/오류 0, 1,950/1,950 일치.
- 기존 `Windows/engine/test/session_test.cpp`를 `clang++ -std=c++20 -O2 -ffp-contract=off`와 기존 엔진 object로 임시 실행 파일에 연결해 실행: exit 0, 173/173 통과.
- 저장소 밖 임시 probe: 실제 iOS `ProxyClient`+프록시 대역의 선택 손상, C++의 세벌식 replay 우회·Caps Lock·숫자 prefix·반각 탁점 삭제·무학습 사전 변경을 확인했습니다. Swift `String`의 `ｶﾞ`·NFD grapheme 삭제, `snprintf`의 412 반환, 실제 `set -eu` 아래의 get.sh rollback 보존도 확인했습니다. probe 실행은 exit 0이며, 이는 위 결함 재현 또는 보존 조건 확인에 성공했다는 뜻입니다.
- `vim -Nu NONE -i NONE -n -es -c 'source Mac/vim/koenja.vim' -c 'autocmd! KoEnJa' -c 'qa!'`: exit 0. 실행 중인 입력기에 신호를 보내지 않도록 autocmd를 해제했습니다.
- `zsh -n Mac/release.sh`, `sh -n Mac/get.sh`, `sh -n Mac/pkg/postinstall`, `sh -n Mac/lsclean.sh`, `bash -n Windows/engine/build.sh`, `bash -n Windows/japanese/build.sh`, `git diff --check`: 모두 exit 0.

검토 중 다른 작업이 `main`을 `f1bd08c`에서 `ca2807a`로 갱신했고 Mac 아이콘·버전 및 iOS build 번호도 바뀌었습니다. 해당 소스 차이와 iOS 초기화 가드를 확인했으며, 이 검토에서는 허용된 빌드 산출물과 이 보고서 외에 저장소 파일을 수정하지 않았습니다.
