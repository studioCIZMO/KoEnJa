수정 후 커밋을 권고합니다. 검토 기준은 `main`의 `4f2c106d42c5e01012d70998fa2093a5eda535d2`와 미커밋 27개 파일이며, `git status`에 새 파일은 없었습니다. 기존 20건은 **고침 8건 / 부분 12건 / 안 고침 0건**으로 판단합니다. Swift 562개·C++ 세션 183개 테스트와 Kotlin 검사는 통과했고, 현재 Swift 라이브러리로 다시 만든 골든 1,950줄도 Kotlin·clang의 기준 파일과 동일합니다. 그러나 Windows 재전송 오판정, Caps Lock 오입력, 학습 저장 손실, 설치 잠금의 상호 배제 실패를 실제 코드 또는 격리 대역으로 재현했습니다. Windows TIP·Windows 설치/제거·iOS 실기기는 실행하지 않았으며, 그 환경에서만 확인할 수 있는 발생 양상은 **추정**으로 구분했습니다. 확인된 치명 등급의 크래시·행은 없지만, 아래 높음 등급 문제를 남긴 채 커밋 승인하기는 어렵습니다.

| 기존 번호 | 판정 | 근거와 남은 범위 |
| --- | --- | --- |
| 1. 재전송 fallback | 부분 | `Windows/tip/Tip.cpp:1268`에서 토큰을 pending 키와 독립적으로 확인하는 점은 개선됐습니다. 그러나 토큰 경로는 `seen/seenAt`를 기록하지 않고, `OnTestKeyDown`도 상태를 남깁니다(`:742`). 그 뒤 같은 실제 키가 최초 fallback 메시지로 인정됩니다. 아래 1번의 추출 코드 검사로 확인했습니다. |
| 2. iOS 선택 영역 | 고침 | `iOS/Keyboard/ProxyClient.swift:59`가 `selectedText`의 UTF-16 길이를 전달하고, `:84`가 선택을 기존 조합에서 벗어난 것으로 처리합니다. `iOS/Keyboard/KeyboardViewController.swift:256`은 그때 세션을 초기화합니다. 실제 ProxyClient 소스와 프록시 대역에서 `가 + 선택된 X → 가ㄴ`을 확인했습니다. |
| 3. TSF context 변경 | 고침 | `Windows/tip/Tip.cpp:730`의 push/pop에서 `finish()`, `:1323`의 composition 소유 context 검사, `:1419`의 편집 전 검사로 원래 지적한 다른 context의 ACP 범위 재사용을 차단합니다. `:1541`은 원래 context에 확정을 요청하고 비동기/실패이면 `:1553`에서 상태를 분리합니다. 네이티브 호스트의 재진입/비동기 실행은 미검증입니다. |
| 4. F9/F10 숫자 유실 | 고침 | `Windows/engine/src/InputSession.cpp:906`이 kana form 변경 뒤 `numberPrefix`를 보존하고 `Windows/tip/Tip.cpp:1446`은 `numberPrefix + keys`를 폭 변환합니다. 숫자 보존 세션 테스트도 통과했습니다. 다만 엔진 수정이 C++에만 들어간 새 포트 차이는 아래 12번입니다. |
| 5. TSF 쓰기 실패 뒤 키 소비 | 부분 | `Windows/tip/Tip.cpp:1364`에서 실패를 기록하고 `:1469`에서 엔진을 비우는 경로는 추가됐습니다. 그러나 `:1488`이 실패를 `edited=false`로 처리해 `:1508`의 재전송을 막으므로 CUAS의 키 전달 문제는 남습니다. 일부 편집 성공 뒤 실패도 “아무것도 쓰지 않음”으로 취급하며, `StartComposition/SetSelection` 실패는 여전히 확인하지 않습니다. 아래 2번. |
| 6. 재전송 대상 확인 | 부분 | `Windows/tip/Tip.cpp:1230`·`:1247`은 원래 focus HWND, foreground HWND, document manager를 비교합니다. 그러나 `Target`에 원래 `ITfContext`/top context가 없어 같은 HWND·document 안의 context 교체를 구별하지 못합니다. 검사 뒤 `SendInput` 전달까지의 초점 변경도 남습니다. 같은 창 안에서 Enter/Tab이 다른 필드로 갈 가능성은 **추정**이며 Windows 재현은 하지 않았습니다. |
| 7. 데스크톱 민감 필드 | 부분 | `Mac/Sources/InputController.swift:137`·`:240`과 `AppleJapaneseConverter.swift:146`은 Secure Event Input에 학습을 연결합니다. Secure Event Input 없는 OTP 필드는 보호되지 않는다고 소스에 명시돼 있습니다. Windows는 `Tip.cpp:1048`·`:1089`에 scope/학습 연결이 생겼지만 private 로그와 context 단위 캐시 문제가 남습니다. 아래 8·9번. |
| 8. learning=false 사용 기록 | 고침 | Swift `Packages/SeamtypeCore/Sources/SeamtypeCore/UserDictionary.swift:463`·`:651`, Kotlin `Android/engine/src/main/kotlin/com/netmaru/rea/engine/UserDictionary.kt:235`, C++ `Windows/engine/src/UserDictionary.cpp:268`에 guard가 있습니다. 추가 테스트는 날짜·파일·메모리 상태 불변 및 다시 켰을 때의 갱신을 확인합니다. 읽기/사전 결정/메뉴의 기존 기록 조회는 막지 않습니다. |
| 9. iOS 민감 필드 터치 교정 | 고침 | `iOS/Keyboard/KeyboardViewController.swift:732`의 첫 guard가 `!sensitive`여서 이웃 키 점수 계산과 EDGE 로그 전에 반환합니다. 이 수정 경로를 직접 호출하는 민감 필드 테스트는 추가되지 않았고 실기기 터치는 미검증입니다. |
| 10. Windows Caps Lock | 부분 | `Windows/tip/Tip.cpp:1159`·`:1393`·`:1432`에서 Shift와 Caps를 분리해 `rjatk → 검사`는 고쳤습니다. 그러나 Swift의 낱자모 약어 규칙과 세벌식 printable 키에서의 Caps 상태 갱신은 빠졌습니다. 새 코드로 `ZZ/RST/RD/DDR`가 자모가 되는 회귀를 재현했습니다. 아래 3번. |
| 11. grapheme Delete | 부분 | `Windows/engine/src/InputSession.cpp:919`와 `Android/engine/src/main/kotlin/com/netmaru/rea/engine/InputSession.kt:986`은 반각 탁점·결합 탁점 등 지정 범위만 처리합니다. IVS, 피부색 modifier, ZWJ는 여전히 Swift `InputSession.swift:3024`와 다릅니다. 실제 C++에서 `葛 + U+E0100 → 葛`, `👩🏽 → 👩`을 확인했습니다. 아래 13번. |
| 12. 기존 slips.log 백업 제외 | 고침 | `iOS/Shared/SlipLog.swift:26`이 기존 파일에도 첫 성공한 append 때 속성을 적용하고, `:48`은 성공했을 때만 `excluded=true`로 만들어 실패 시 다음 쓰기에 재시도합니다. 신규 생성·원자적 trim 뒤에도 적용합니다. 기존 파일을 전혀 쓰지 않는 경우의 즉시 마이그레이션이나 실제 iCloud 백업은 검증하지 않았습니다. |
| 13. Windows 학습 영속 저장 | 부분 | `Windows/tip/Tip.cpp:210`이 `learning.txt`를 연결하고 `:1096`·`:1554`가 Mozc를 저장합니다. 단일 세션 재시작 보존은 개선됐지만 `UserDictionary.cpp:272`는 오래된 전체 snapshot 덮어쓰기·공유 `.tmp`, `MozcConverter.cpp:385`는 잠금 없는 read/merge/write입니다. 두 경로 모두 데이터 손실을 재현했습니다. 아래 4·5번. |
| 14. 키 로그 버퍼 초과 | 고침 | `Windows/tip/Tip.cpp:860`에서 `snprintf` 반환값을 `0...sizeof(line)-1`로 제한합니다. 원래의 필요한 길이를 실제 버퍼 길이로 사용하던 범위 밖 읽기가 제거됐습니다. |
| 15. 고아 설치 잠금 | 부분 | `Mac/pkg/postinstall:20`·`Mac/get.sh:57`의 PID 생존 확인은 단일 복구자의 고아 잠금을 복구합니다. 그러나 확인과 `rm -rf` 사이에 새 소유자의 잠금을 지우는 경쟁이 있습니다. 구버전 get.sh는 PID를 모르므로 여전히 10분 넘은 살아 있는 새 잠금을 삭제합니다. 아래 6번. |
| 16. 등록 실패 뒤 옛 앱 삭제 | 부분 | `Mac/pkg/postinstall:94`의 삭제 guard와 실패 시 보존/exit 1은 추가됐습니다. 다만 `:83`은 `--register` 실패를 무시하고 `:84`는 ID 존재만 확인합니다(`Mac/Sources/Registration.swift:37`). 또한 `Registration.run()`은 성공 전 다른 사본을 등록 해제합니다(`:11`·`:44`). 아래 11번. |
| 17. 다른 관리자 계정으로 제거 | 부분 | `Windows/installer/languages.ps1:28`이 다른 사용자 프로필까지 찾는 방향은 개선됐습니다. 그러나 hive 해제 보장, 실패 보고, 언어 삭제의 설치 이력 확인이 없고 `S-1-5-21-*` 외 SID는 대상에서 빠집니다. 아래 7·10번. |
| 18. try.cmd 무조건 종료 | 고침 | `Windows/tip/try.cmd:12`에서 등록 해제·메시지·exit가 같은 `if` 괄호 안에 들어갔습니다. 일반 실행은 `:14` 이후 등록 경로로 진행합니다. Windows cmd 실행은 하지 않았습니다. |
| 19. 길게 누르기·Fix typos 설명 | 부분 | 키 표/상세의 오른쪽 modifier 한정은 반영됐습니다(`README.md:165`·`:247`, `README.en.md:167`·`:263`, `README.ja.md:167`·`:258`). 그러나 세 README의 `:96`은 여전히 수동 길게 누르기 설명에 “Fix typos, 기본 꺼짐”을 붙여 수동 추측도 그 설정에 의존하는 것처럼 안내합니다. |
| 20. 골든 수·upstream 버전 | 부분 | 개요·분모 설명·Swift 행·비교표·upstream 0.6.2는 고쳤습니다. 그러나 `README.md:554`, `README.en.md:609`, `README.ja.md:583`부터 Kotlin/clang/MSVC 행은 아직 `1932/1932`입니다. 그 성능 수치를 과거 측정이라고 구분한 날짜/조건도 없습니다. |

아래는 새 회귀와 수정 과정에서 남은 주요 결함이며, 기존 문제의 단순 재진술인 경우 표시했습니다.

1. **높음 — 토큰이 정상 보존된 재전송 뒤 다음 실제 키도 우회합니다.** (기존 #1 미완료)

   `Windows/tip/Tip.cpp:742`, `:1268`, `:1272`: 토큰 분기의 조기 반환은 `replayed.seen=false`를 그대로 둡니다. 표준 TSF 루프는 Test가 false이면 Down을 호출하지 않으므로 `:1494`의 초기화도 실행되지 않습니다. 1초 안에 동일 VK·modifier의 실제 키가 오면 이를 첫 fallback으로 수락합니다. 실제 `isReplayed` 본문과 시간/extra-info 대역으로 `injected=1 nextPhysical=1`을 재현했습니다. 세벌식에서 영어 단어 뒤 `;`를 확정용으로 전달하고 바로 다시 `;`를 누르면 다음 `ㅂ` 키까지 엔진을 우회하는 경로입니다. 토큰 경로에서도 해당 메시지의 소비 사실을 기록하거나 pending fallback을 종료하고, 반복 Test와 다음 메시지를 각각 검사해야 합니다. [Microsoft의 TSF 메시지 루프](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-keydown).

2. **높음 — 쓰기 실패를 알면서도 CUAS에 원래 키를 돌려주는 경로는 없습니다.** (기존 #5 미완료, 네이티브 발생은 **추정**)

   `Windows/tip/Tip.cpp:1469`·`:1488`·`:1508`: Test에서 true를 반환한 뒤 `SetText`가 실패하면 `refused=true → edited=false`가 되어 재전송 조건에서 제외됩니다. 이 파일의 `:1223` 주석이 이미 설명하는 IMM32/CUAS의 `VK_PROCESSKEY` 경로에서는 Down의 false만으로 원래 키가 복구되지 않습니다. 따라서 읽을 수 있는 ACP 필드가 TIP의 쓰기만 거절할 때 원래 `d` 입력을 잃는 시나리오가 남습니다. 추가 테스트는 Down이 false이면 직접 `store.type(c)`를 호출해 이 호스트 동작을 대체합니다(`tip_test.cpp:525`·`:530`). “편집 세션 미실행 / 쓰기 전 실패 / 일부 쓰기 후 실패”를 분리하고, 쓰기 전 실패는 상태를 분리한 뒤 원래 대상에 안전하게 키를 전달해야 합니다. 일부 쓰기 뒤에는 무조건 원문 키를 전달하거나 상태만 비우지 말고 성공한 편집까지 고려해야 합니다. Windows 실행 없이 이 경로가 실제 LINE 등에서 발생했다고 단정하지는 않습니다.

3. **높음 — CapsClient만 포팅해 Caps Lock 약어가 한글 자모로 바뀝니다.** (새 회귀)

   `Windows/tip/Tip.cpp:1159`·`:1393`·`:1432`, 비교 `Packages/SeamtypeCore/Sources/SeamtypeCore/InputSession.swift:3216`: TIP은 엔진에 소문자를 보내지만 C++ 엔진에는 Swift의 “Caps 상태에서 낱자모 두 개 이상은 약어” 규칙이 없습니다. 실제 새 CapsClient와 C++ 세션에서 `zz → ㅋㅋ`, `rst rd ddr → ㄱㄴㅅ ㄱㅇ ㅇㅇㄱ`을 재현했고 `rjatk → 검사`만 정상입니다. 같은 엔진에 이전 TIP처럼 대문자를 전달하면 `ZZ RST RD DDR`가 그대로 유지되는 것도 확인했습니다. 따라서 Caps Lock으로 `RST` 같은 식별자를 쓰는 입력이 기존 대문자 처리보다 나빠집니다. Caps 상태와 해당 결정 규칙을 함께 이식하고, 사전 우선·실제 Shift·세벌식 printable에서 Caps가 바뀌는 경우도 Swift의 CapsLockTests와 맞춰야 합니다. 추가 TIP 검사는 `rjatk/hello/dkssud`만 다룹니다(`tip_test.cpp:799`).

4. **높음 — learning.txt는 동시 쓰기가 없어도 다른 앱에서 배운 항목을 잃습니다.** (새 영속 저장 경로의 결함)

   `Windows/tip/Tip.cpp:210`, `Windows/engine/src/UserDictionary.cpp:154`·`:272`: A와 B가 같은 파일을 읽은 뒤 A가 `so`, B가 `go`를 학습하면 B는 자신이 보유한 오래된 전체 사전으로 파일을 덮어씁니다. 실제 두 DictionaryEngine으로 순서대로 저장해 최종 `so=0 go=1`을 확인했습니다. 겹치는 저장에서는 둘 다 `learning.txt.tmp`를 truncate/write/rename하므로 임시 파일도 충돌합니다. **Windows에서의 바이트 단위 손상은 미재현(추정)**이지만, 순차 저장의 항목 유실만으로도 수정이 필요합니다. 파일별 프로세스 간 잠금 안에서 최신 내용을 읽고 이 인스턴스가 변경한 항목/삭제만 병합한 뒤 고유 임시 파일로 교체해야 합니다. mutex만 추가하고 오래된 전체 snapshot을 쓰는 방식으로는 순차 손실을 고칠 수 없습니다.

5. **높음 — Mozc의 merge도 하나의 트랜잭션이 아니어서 한쪽 학습이 사라집니다.** (새 저장 경로의 결함)

   `Windows/japanese/MozcConverter.cpp:385`·`:391`·`:401`·`:404`: 두 프로세스가 같은 이전 파일을 읽으면 각자 다른 reading을 병합해도 마지막 rename이 앞 저장을 지웁니다. 저장에 성공한 양쪽 모두 `touched.clear()`하여 사라진 항목을 다시 저장할 기회도 없어집니다. 실제 MozcConverter를 두 프로세스에서 같은 시점에 저장한 검사에서, 30,000개 기존 항목이 있는 파일에 `じこ`와 `きしゃ`를 각각 학습했을 때 **12회 모두 한쪽 reading이 유실**됐습니다. PID별 임시 이름은 프로세스끼리의 임시 파일 충돌만 줄이며, 같은 프로세스의 여러 UI thread/TIP 인스턴스는 여전히 같은 이름을 사용합니다. read/merge/rename 전체를 파일별 프로세스 간 잠금으로 묶고 임시 이름은 저장 호출별로 고유하게 만들어야 합니다. 추가 테스트의 `one.save(); two.save();`는 순차 실행입니다(`Windows/japanese/test/mozc_converter_test.cpp:197`).

6. **높음 — 고아 잠금 복구가 살아 있는 새 잠금을 지우고 두 설치를 동시에 진행시킵니다.** (새 경쟁 조건)

   `Mac/get.sh:59`·`:62`·`:68`, `Mac/pkg/postinstall:24`·`:28`·`:42`: A/B가 둘 다 죽은 PID를 확인한 뒤 A가 복구·획득하면, 잠시 늦게 진행한 B의 `rm -rf`는 A의 새 잠금을 지웁니다. B도 획득하므로 둘이 `Contents` 교체/rsync를 함께 실행할 수 있고, A의 EXIT trap은 B의 잠금까지 삭제합니다. 실제 두 `take_lock` 함수에 스케줄링 barrier만 둔 격리 검사에서 `both_acquired=True`, `A_exit_removed_B_lock=True`, `B_alive=True`를 확인했습니다. PID 소유자 확인과 삭제·재획득을 안전한 소유권 프로토콜로 묶고 cleanup은 자신의 소유권을 확인해야 합니다.

   구버전 호환도 시간 제한이 있습니다. 현재 HEAD의 구형 get.sh가 쓰는 `find ... -mmin +10 -exec rm -rf`만 격리 실행해, 살아 있는 PID가 들어 있는 새 잠금도 10분 경과 시 삭제됨을 확인했습니다. `Mac/Sources/Updater.swift:214`의 번들 get.sh를 실행하는 옛 앱이 참여할 수 있으므로, 새 PID 파일만으로 “살아 있는 설치는 절대 빼앗지 않음”을 보장할 수 없습니다. 구형 updater가 사용하는 잠금의 mtime 유지 또는 구형 실행을 제한하는 이행 규칙이 필요합니다. `postinstall:22`의 디렉터리 chown은 일반 사용자의 정리에 도움을 주지만 위 상호 배제 문제를 해결하지 않습니다. 실제 앱/설치는 건드리지 않았습니다.

7. **높음 — 다른 사용자 hive를 열어 둔 채 unload 실패를 숨길 수 있습니다.** (새 결함, Windows 실행 결과는 **추정**)

   `Windows/installer/languages.ps1:35`·`:42`·`:54`: `Get-ChildItem`의 마지막 `RegistryKey`가 `$key` 변수에 살아 있는데 `[gc]::Collect()`만 호출합니다. 이 참조는 수집 대상이 아니며 `Close()/Dispose()`도 없습니다. 예를 들어 오프라인 사용자 hive에 `en-US`가 남아 있으면, KoEnJa와 관계없는 마지막 키를 잡은 상태로 unload할 수 있습니다. 열린 하위 키 handle은 unload 거절 사유인데, 스크립트는 `reg.exe unload` 출력과 exit code를 모두 무시하며 `try/finally`도 없습니다. 결과적으로 `HKU\KoEnJa-<SID>`가 남고 후속 프로필 로드에 영향을 줄 가능성이 있습니다. **NTUSER.DAT 바이트 손상이나 실제 로그인 실패를 확인한 것은 아닙니다.** 모든 RegistryKey를 명시적으로 닫고 반드시 finally에서 unload하며 실패를 보고해야 합니다. [PowerShell RegistryProvider의 객체 반환](https://raw.githubusercontent.com/PowerShell/PowerShell/master/src/System.Management.Automation/namespaces/RegistryProvider.cs), [Microsoft의 열린 handle과 unload 조건](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rrp/bd8bb640-ce36-4194-93e1-1ce6b55bf775).

8. **중간 — IS_PRIVATE 필드의 학습은 꺼도 키 로그는 계속 씁니다.** (기존 #7 미완료)

   `Windows/tip/Tip.cpp:839`·`:1032`·`:1070`: `log()`는 `disabled()`만 보는데, `IS_PRIVATE`는 `privacy=1`이라 disabled가 아닙니다. `KeyLog=1`인 상태로 private 필드에 입력하면 VK와 modifier를 파일에 기록하는 경로가 그대로 열려 있습니다. 이는 코드 조건으로 확인했으며 Windows 로그 파일 생성은 실행하지 않았습니다. `privacy(context) != 0`이면 진단 키 로그도 금지하도록 같은 민감 상태를 적용해야 합니다. 새 private 테스트는 바로 앞에서 KeyLog를 0으로 바꾸므로 이 문제를 검사하지 않습니다(`tip_test.cpp:820`).

9. **중간 — input scope를 context 포인터만으로 영구 캐시합니다.** (새 보호 로직의 결함, 호스트 발생은 **추정**)

   `Windows/tip/Tip.cpp:1050`·`:1083`, 캐시 해제 `:723`·`:730`: scope는 현재 selection의 property로 읽는데 결과는 context 전체에 캐시합니다. 같은 context에서 앱이 일반 필드를 private/password로 바꾸거나 범위별 input scope가 달라져도 document focus/push/pop이 없으면 다음 키부터 다시 읽지 않습니다. 최초 정상 필드에서 얻은 0이 유지되면 이후 보호를 우회하고, 최초 password의 2가 유지되면 정상 필드에서도 입력기를 계속 우회합니다. 성공한 edit session 안에서 property 조회가 실패해도 `out=0`을 성공 결과처럼 캐시합니다. property/selection 변경에 캐시를 무효화하거나 키 처리 시 유효한 scope를 갱신하고, “scope 없음”과 “읽기 실패”를 구별해야 합니다. 새 테스트는 scope를 바꿀 때마다 `\f`로 focus를 강제 변경하여 이 경우를 건너뜁니다(`tip_test.cpp:815`·`:824`). 같은 HWND의 private scope를 동적으로 바꾸는 사용법은 [Microsoft 문서](https://learn.microsoft.com/en-us/windows/apps/develop/windows-integration/recall/recall-web-browsers)에도 있습니다.

10. **중간 — 제거기가 설치하지 않은 다른 사용자의 선호 언어까지 삭제합니다.** (새 영향 범위)

   `Windows/installer/languages.ps1:47`·`:49`·`:50`: 남은 TIP 값이 없다는 이유만으로 `Languages`와 언어 하위 키 전체를 제거하며, 그 언어를 KoEnJa 설치가 추가했다는 기록을 확인하지 않습니다. 기존에 한국어를 선호 언어로 쓰던 다른 사용자가 한국어 키보드로 KoEnJa만 남겨 두고 영어도 등록했다면, 프로그램 제거가 그 사용자의 한국어 언어 설정까지 삭제합니다. 코드의 삭제 조건은 확인했으며 실제 Windows 표시 언어/언어팩에 미치는 추가 영향은 **추정**입니다. 이 프로그램의 GUID 값만 제거하고, 언어 자체를 없애려면 설치 시 SID·추가한 언어·이후 변경 여부를 추적해야 합니다.

11. **중간 — pkg의 “등록 성공”이 새 사본의 등록 성공을 뜻하지 않습니다.** (기존 #16 미완료)

   `Mac/pkg/postinstall:83`·`:84`·`:94`, `Mac/Sources/Registration.swift:11`·`:19`·`:37`: `--register`가 실패해도 `--registered`가 같은 input-source ID를 하나 찾으면 `registered=1`입니다. 그 ID가 아직 남아 있는 기존 사본의 등록이라면 옛 파일 삭제 guard를 통과합니다. 또 `Registration.run()`은 새 등록의 성공을 알기 전에 `forgetOtherCopies()`로 기존 사본을 등록 해제하므로 “파일을 남김”만으로 기존 입력 소스가 보존되는 것도 아닙니다. 실제 LaunchServices/TIS의 목록 잔존 시간과 재현 여부는 **추정**이며 설치 명령을 실행하지 않았습니다. 새 등록 명령의 성공과 새 bundle의 등록을 함께 확인하고 기존 사본 등록 해제·파일 삭제는 그 이후로 옮겨야 합니다.

12. **중간 — numberPrefix 수정이 C++에만 들어가 Enter 동작이 포트별로 달라집니다.** (새 회귀)

   `Windows/engine/src/InputSession.cpp:906`, 미반영 `Packages/SeamtypeCore/Sources/SeamtypeCore/InputSession.swift:3008`·`Android/engine/src/main/kotlin/com/netmaru/rea/engine/InputSession.kt:973`: 동일한 `henkan → Space → Enter → 3ji → Space → Katakana → Escape` 뒤 C++는 `keys=ji`와 marked range를 유지하지만 Swift는 `keys=""`인 확정 상태입니다. 현재 두 실제 엔진에 같은 고정 후보 converter를 연결해 다음 Enter가 C++에서는 소비되고 Swift에서는 줄바꿈으로 전달되는 것을 확인했습니다. Kotlin에도 보존 대입이 빠진 것은 정적으로 확인했으나 이 보조 시퀀스 자체는 Kotlin에서 실행하지 않았습니다. 세 포트에 같은 상태 보존 규칙을 반영하고 form/Escape/Enter까지 포함한 공통 회귀 입력을 추가해야 합니다. 기존 1,950줄 골든 통과는 이 경로를 검사한다는 뜻이 아닙니다.

13. **중간 — grapheme 삭제가 일부 Unicode 범위 목록에 머물러 있습니다.** (기존 #11 미완료)

   `Windows/engine/src/InputSession.cpp:919`, `Android/engine/src/main/kotlin/com/netmaru/rea/engine/InputSession.kt:986`: 일본어 이름에도 쓰이는 supplementary variation selector `U+E0100`을 포함하지 않아 `葛 + IVS`에서 Delete가 글자 전체가 아니라 selector만 지웁니다. C++의 `👩🏽 → 👩`, `👨‍👩‍👧 → 👨‍👩‍`도 재현했으며 Swift는 세 경우 모두 한 grapheme 전체를 삭제했습니다. 새 범위 루프는 줄바꿈 뒤 독립 결합 문자에서도 앞 줄바꿈까지 함께 지웁니다(`A\n\u0301 → A`, Swift는 `A\n`). 일반적인 extended grapheme 경계 규칙을 사용하고 UTF-16 offset으로 변환해야 합니다. 추가 테스트의 `ｶﾞ/ｶﾞﾅ` 두 예만으로 Swift와의 일치를 보장할 수 없습니다.

검증 범위도 구분해야 합니다. 새 `tip_test.cpp`의 Host는 foreground 창이 없고 `GetFocus`도 `E_NOTIMPL`입니다(`:225`·`:277`). 따라서 replay 대상 검사, 실제 `SendInput` 메시지의 token/time, CUAS 전달, context push/pop 중 callback, 부분 편집 실패는 검증하지 않습니다. 반면 추가한 세 엔진의 `learning=false` 테스트와 반각 탁점 테스트는 바꾼 분기를 직접 통과합니다. iOS 선택 테스트도 수정 경로를 통과하지만 `resetIfMovedAway()`를 직접 호출하므로 UIKit callback 순서까지 검증한 것은 아닙니다. `corrected()`의 민감 guard, Apple converter의 새 learning flag, 설치/제거 경로에는 직접 회귀 테스트가 추가되지 않았습니다.

Windows `privacy()`의 동기 read-only edit session 자체는 문서상 허용되는 요청이고, `TF_ES_SYNC`는 실패 시 비동기로 그 참조 캡처를 실행시키는 모드가 아닙니다. 따라서 이것만으로 크래시/교착이라고 판단하지 않았습니다. [RequestEditSession 규칙](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession). Mac `noteSensitive()`는 키마다 secure-input 조회·플래그 대입·상태 변경 시 로그를 추가하며 파일 I/O나 사전 스캔은 없습니다. 실제 입력기에서 비용을 측정하지 않았으므로 성능 회귀로 보고하지 않습니다. 전역 Secure Event Input을 전역 engine/converter에 반영한다는 한계는 있지만, 이번 diff가 정상 사전 읽기나 메뉴 명령을 막는 결함은 확인하지 못했습니다. iOS 선택 변경과 민감 guard에서도 추가로 확인된 회귀는 없습니다.

실행한 명령과 핵심 결과는 다음과 같습니다. 모든 보조 probe와 그 데이터는 저장소 밖 임시 디렉터리 `/var/folders/wj/gc8m8jmd7ds34cgzj355ypfr0000gn/T/koenja-fix-review-_qk8l7ys`에만 만들었습니다. 아래에서 `<probe>`는 이 디렉터리입니다.

- `git status --short`, `git diff --stat`, `git diff`, `git diff --check`: 수정 27개, 새 파일 없음, diff 공백 검사 exit 0. 기준 HEAD는 위의 `4f2c106...`입니다.
- `cd Packages/SeamtypeCore && swift test`: exit 0. `Test run with 562 tests in 49 suites passed`. `PreviewTraceTests`, `DictionarySplitCheckTests`는 기존 조건으로 skipped.
- `bash Windows/engine/build.sh`: exit 0. `ko-en-ja: 975/975`, `ko-ja-en: 975/975`, `golden: 1950/1950`; 21,802 keys, 약 8.7 µs/key.
- `bash Windows/japanese/build.sh`: exit 0. `all passed`, `top-1 accuracy: 29/30 = 97%`. `よろしくおねがいします` 1건은 기대 top-1과 달랐지만 기존 정확도 기준을 통과했습니다.
- `cd Android && ./gradlew :engine:test`: exit 0, 최초에는 `UP-TO-DATE`. 이어 `./gradlew :engine:test --rerun-tasks`: exit 0, `BUILD SUCCESSFUL`, `golden: 1950/1950`. XML 합계 5 suites / 31 tests / failures 0 / errors 0.
- `clang++ -std=c++20 -O2 -ffp-contract=off Windows/engine/test/session_test.cpp Windows/engine/src/*.cpp -o <임시 session_test>`, 이어 Data 경로를 인자로 실행: exit 0, `session: 183/183`.
- 현재 `swift test`가 만든 `Packages/SeamtypeCore/.build/out/Products/Debug/libSeamtypeCore.a`와 `Android/tools/golden/Sources/golden/main.swift`를 `swiftc -I <Products/Debug> -I <_NumericsShims/include> ... -o <probe>/golden`으로 연결; `<probe>/golden Android/engine/src/test/resources/golden_input.txt <probe>/golden.tsv`; `cmp Android/engine/src/test/resources/golden.tsv <probe>/golden.tsv`: 모두 exit 0. 현재 Swift 출력 1,950줄 byte-identical; 저장소의 골든 파일은 변경하지 않았습니다.
- `clang++ -std=c++20 -O2 -ffp-contract=off <probe>/probe.cpp Windows/japanese/MozcConverter.cpp Windows/engine/src/*.cpp -o <probe>/probe`, 이어 Data·mozc.dic·임시 디렉터리를 인자로 실행: exit 0. `REPLAY injected=1 nextPhysical=1`, `CAPS zz => ㅋㅋ`, `DICTIONARY sequential so=0 go=1`, `MOZC concurrent lost readings in 12/12 trials`. TIP 전체를 컴파일한 것이 아니라 그 순수 함수/adapter 본문을 추출하고 나머지는 실제 엔진을 연결했습니다.
- `python3 <probe>/lock_probe.py`: exit 0. `both_acquired=True`, `A_exit_removed_B_lock=True`, `B_alive=True`, `deleted_new_pid_lock=True`. 실제 설치 스크립트 전체를 실행하지 않고 `take_lock` 및 구버전 stale-cleanup 명령만 임시 앱 디렉터리에서 실행했습니다.
- `swiftc -parse-as-library <probe>/proxy_probe.swift -o <probe>/proxy_probe` 후 실행: exit 0. `IOS selection length=1 reset=true result=가ㄴ`, emoji 선택 UTF-16 길이 2. 실제 ProxyClient 소스에 UIKit/TraceLog 대역만 연결한 검사입니다.
- Swift/C++ 엔진 보조 probe 연결·실행: exit 0. `SWIFT PREFIX afterEscape ... keys= ... marked=NSNotFound`와 `CPP PREFIX ... keys=ji marked=3,1`, C++ `EnterHandled=1`, Swift는 다음 Enter에 줄바꿈. IVS/emoji/결합 문자 Delete 차이도 확인했습니다. 초기 보조 링크의 `_NumericsShims` 모듈 경로 누락과 존재하지 않는 object 이름은 올바른 include/실제 source별 object 목록으로 수정해 재실행했으며 제품 빌드 실패가 아닙니다.
- `sh -n Mac/get.sh`, `sh -n Mac/pkg/postinstall`, 마지막 `git diff --check`: 모두 exit 0.
- Windows TIP/MSVC·설치/제거·iOS XCTest/실기기·시뮬레이터·설치된 Mac 입력기 실행은 하지 않았습니다. 설치/도구 추가/commit/push도 하지 않았습니다. 허용된 빌드 산출물과 이 보고서 외에는 저장소 파일을 변경하지 않았습니다.
