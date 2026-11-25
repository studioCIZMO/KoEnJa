**수정 후 커밋**을 권고합니다. 기준은 `main`, HEAD `4f2c106d42c5e01012d70998fa2093a5eda535d2`, 추적 파일 34개 수정과 지정된 신규 파일 2개이며, 이전 `codex-fix-review.md`의 13건은 **고침 7 / 부분 6 / 안 고침 0**으로 판단합니다. Swift 563개, C++ 세션 226개 검사, C++ 골든 1,950개, Mozc 검사는 통과했고 Kotlin의 Caps·일본어 세션 테스트 메서드 25개도 직접 실행해 통과했습니다. 정상 토큰 재전송과 서로 다른 항목의 정상 잠금하 병합은 개선됐지만, 부분 쓰기 실패의 키 유실, 실패한 `finish()`의 composition 잔존 경로, 잠금 실패 시 무잠금 저장, 같은 항목의 갱신 손실, heartbeat의 잠금 재생성 문제가 남습니다. Windows TIP·Windows Registry/설치·실제 Mac 등록은 실행하지 않았으며, 아래에서 대역으로 확인한 코드 경로와 실제 네이티브 호스트에서의 발생 여부(**추정**)를 구분했습니다.

| 이전 보고서 번호·문제 | 판정 | 현재 근거와 남은 범위 |
| --- | --- | --- |
| 1. 토큰 재전송 뒤 실제 키 우회 | 고침 | `Windows/tip/Tip.cpp:1287`, `:1291`에서 토큰 경로가 pending fallback을 비웁니다. 실제 `isReplayed` 본문을 추출한 검사에서 `injected=1 repeatedTest=1 nextPhysical=0 pending=0`; 무토큰의 같은 메시지는 반복 인정하고 다음 timestamp는 거절했습니다(`:1296`). 다른 키 뒤 늦게 온 토큰도 인정합니다. |
| 2. 쓰기 실패 뒤 CUAS에 키를 돌려주지 않음 | 부분 | `Windows/tip/Tip.cpp:1479`, `:1501`, `:1521`에서 쓰기 전 실패는 edit 실행 여부를 유지하여 Type 키의 재전송 경로에 도달합니다. 그러나 `:1488`은 기존 단어 확정만 성공해도 새 키를 소비합니다. `finish()`에는 같은 실패 처리가 없고(`:1557`), `SetSelection` 결과도 여전히 무시합니다(`:1417`). 추가 1·2번 참조. |
| 3. Caps Lock 약어가 자모가 됨 | 고침 | C++ `Windows/engine/src/InputSession.cpp:118`, `:1168`과 Kotlin `Android/engine/src/main/kotlin/com/netmaru/rea/engine/InputSession.kt:249`, `:1174`가 Caps 상태와 Swift의 약어·사전 우선 규칙을 함께 처리합니다. `RST/RD/DDR/ZZ`, 미리보기, 사전 우선, 실제 Shift, 단어 중 Caps 변경, 후보 확정 테스트를 통과했습니다. C++의 세벌식 printable 변경도 검사합니다(`Windows/engine/test/session_test.cpp:489`). TIP의 handle·choose·form·finish가 모두 cased 경로를 사용합니다(`Windows/tip/Tip.cpp:1442`, `:1444`, `:1459`, `:1557`). |
| 4. learning.txt의 오래된 전체 snapshot 덮어쓰기 | 부분 | `Windows/engine/src/UserDictionary.cpp:281`, `:284`, `:291`, `:295`, `:302`가 잠금하 변경 항목 병합·고유 임시 파일·rename을 수행합니다. 서로 다른 `so/go`와 다른 항목 저장 뒤 삭제 유지 검사는 통과했습니다. 다만 잠금 실패가 무시되고, 같은 항목의 날짜 갱신이 최신 승격·삭제를 덮습니다. 추가 3·4번 참조. |
| 5. Mozc read/merge/rename 경쟁 | 부분 | `Windows/japanese/MozcConverter.cpp:385`에서 획득한 잠금은 `:398`의 rename까지 유지되며, `:401`은 저장 실패 시 `touched`를 남깁니다. 정상 잠금의 동시 저장 검사는 통과했습니다. 잠금 실패 시에는 다시 항목을 잃고, 같은 후보의 사용 횟수도 오래된 값으로 덮습니다. 추가 3·5번 참조. |
| 6. 고아 설치 잠금 복구 경쟁·구버전 cleanup | 부분 | `Mac/get.sh:78`, `:79`, `:87` 및 같은 함수인 `Mac/pkg/postinstall:41`, `:42`, `:50`에 단일 복구자·죽은 PID 재확인·소유자 한정 해제가 있습니다. 동시 복구 8회에서 중첩 진입은 없었고 heartbeat가 구버전 `-mmin +10` 삭제도 막았습니다. 그러나 새 `touch` 루프가 해제된 `.install`을 일반 파일로 다시 만듭니다. 추가 6번 참조. |
| 7. 다른 사용자 hive handle/unload 실패 은폐 | 고침 | `Windows/installer/languages.ps1:37`, `:60`, `:62`의 `Dispose`와 `:66`의 `finally`가 편집 예외 뒤에도 unload로 이어집니다. unload 실패는 `:70`에서 경고와 실패 상태로 남기고 `:74`에서 exit 3입니다. 실제 Windows unload는 미실행이며 판정은 제어 흐름 검토에 근거합니다. |
| 8. IS_PRIVATE에서도 KeyLog 기록 | 고침 | `Windows/tip/Tip.cpp:837`이 `privacy(context) != 0`인 필드의 로그를 차단합니다. `Windows/tip/tip_test.cpp:818`, `:829`, `:833`도 KeyLog를 켠 채 private 입력 전후 파일 크기를 비교하도록 바뀌었습니다. 네이티브 테스트는 실행하지 않았습니다. |
| 9. input scope를 context 단위로 영구 캐시 | 부분 | `Windows/tip/Tip.cpp:1049`가 매번 재조회하며 테스트도 focus 변경 없이 scope를 바꿉니다(`Windows/tip/tip_test.cpp:822`, `:829`, `:835`). 그러나 `GetValue` 실패를 `out=0`으로 반환하고(`Tip.cpp:1070`), `GetInputScopes` 실패도 0으로 처리합니다(`:1077`, `:1090`). 따라서 private/password 판정 뒤 읽기만 실패하면 `:1051`이 민감 상태를 지우고 학습·로그가 켜질 수 있습니다. 읽기 실패는 `nullopt`로 반환해 마지막 민감 상태를 유지해야 합니다; 실제 호스트의 해당 실패 발생은 **추정**입니다. |
| 10. 다른 사용자의 선호 언어까지 삭제 | 고침 | `Windows/installer/languages.ps1:59`는 KoEnJa의 TIP 값만 삭제하고 언어 키·`Languages`는 보존합니다. `:35`는 `S-1-5-21-*`, `S-1-12-1-*`를 모두 포함합니다. 현재 사용자 처리 `:11`부터 `:26`까지는 HEAD와 같은 동작입니다. |
| 11. 새 사본 등록 실패를 성공으로 오인 | 고침 | `Mac/pkg/postinstall:104`가 `--register`와 `--registered`의 연속 성공 때만 `registered=1`로 만듭니다. `Mac/Sources/Registration.swift:20`, `:34`는 TIS 등록 성공 뒤 다른 사본을 잊고, `:46`은 입력 소스 존재와 LaunchServices의 이 bundle 경로를 함께 확인합니다. 실제 등록·설치는 실행하지 않았습니다. |
| 12. C++만 numberPrefix를 보존하는 포트 차이 | 고침 | `Windows/engine/src/InputSession.cpp:993`의 form 전환이 다시 Swift와 같은 규칙이며, 세 포트의 `3ji → Space → Katakana → Escape → Enter` 회귀 검사가 통과했습니다(`Windows/engine/test/session_test.cpp:174`, `Android/engine/src/test/kotlin/com/netmaru/rea/engine/JapaneseSessionTest.kt:156`, `Packages/SeamtypeCore/Tests/SeamtypeCoreTests/JapaneseSessionTests.swift:292`). TIP의 F9/F10은 `Windows/tip/Tip.cpp:1453`에서 kana와 keys의 reading 차이로 prefix를 구합니다. |
| 13. 일부 Unicode 범위만 처리하는 Delete | 부분 | IVS·피부색·ZWJ 가족·국기·CRLF·NFD 한글·줄바꿈 뒤 결합 문자에 대한 새 검사는 통과했습니다. 그러나 `Windows/engine/src/InputSession.cpp:25`, `:42`와 Kotlin `Android/engine/src/main/kotlin/com/netmaru/rea/engine/InputSession.kt:1537`, `:1552`의 속성 목록은 여전히 근사치입니다. 새 ZWJ 규칙이 서로 다른 grapheme까지 함께 지우는 경우도 재현됐습니다. 추가 7번 참조. |

추가로 확인한 수정 경로의 결함은 다음과 같습니다. 실제 Windows 실행 없이 확인한 항목은 검증 수준을 함께 적었습니다.

1. **높음 — 기존 단어 확정만 성공한 부분 쓰기를 새 키의 성공으로 취급합니다.**

   `Windows/tip/Tip.cpp:1393`, `:1488`, `Windows/engine/src/InputSession.cpp:122`, `:125`. Caps off로 `hello`를 조합한 뒤 Caps on으로 `x`를 입력하면 엔진은 기존 `hello` 확정과 새 `X` 표시를 각각 요청합니다. 첫 쓰기는 성공하고 두 번째만 거절하는 실제 C++ 엔진·TextClient 대역에서 `text=hello written=1 failed=1 tipEaten=1 keys=`를 확인했습니다. 새 `X`는 전혀 기록되지 않았지만 키는 소비되고 세션도 비워집니다. Windows에서 이 거절 순서를 보이는 호스트의 발생 여부는 **추정**이나, 쓰기 횟수만으로 새 키의 전달을 판정하는 오류는 재현했습니다. 한 키의 편집을 가능한 한 한 번의 교체로 합치거나, 기존 단어 확정과 현재 키의 효과를 구별하여 현재 키가 기록되지 않은 경우만 안전하게 재처리해야 합니다; 부분 쓰기 전체를 무조건 replay하는 방식도 안전하지 않습니다.

2. **높음 — `finish()`에서 쓰기가 거절되면 이전 composition이 남습니다.**

   `Windows/tip/Tip.cpp:1392`, `:1554`, `:1557`, `:1565`. A context의 조합을 확정하는 edit session은 실행됐으나 `Client.replace()`의 `SetText`가 실패하면, `field.failed`만 설정되고 callback은 정상 종료합니다. `finish()`는 이 값을 검사하지 않고 `hr == S_OK`이면 `drop()`도 호출하지 않습니다. 실제 엔진과 거절 대역에서 `hello` 확정 후 `keys`만 비워지고 marked range `0,5`가 남았으며, TIP 소스상 `composition`을 끝내거나 reset하는 다른 경로도 없습니다. 이후 B context에서 다시 `finish()`를 불러도 빈 엔진은 쓰기를 요청하지 않아 기존 composition이 계속 남을 수 있습니다. 실제 TSF context 교체·pop 때의 외형적 증상은 **추정**입니다. 원래 context의 cookie 안에서 `field.failed`를 처리하고, 확정 성공 여부와 별개로 해당 composition을 종료·분리해야 합니다. `RequestEditSession`의 성공 결과는 callback 반환값이며 내부 `SetText`의 성공을 보장하지 않습니다. [Microsoft RequestEditSession 문서](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession).

3. **높음 — 잠금 파일만 열 수 없는 경우 저장이 무잠금으로 진행됩니다.**

   `Windows/engine/src/FileLock.h:33`, `:36`, `:41`, `Windows/engine/src/UserDictionary.cpp:281`, `Windows/japanese/MozcConverter.cpp:385`. `FileLock`은 획득 실패를 호출자에게 전달하지 않습니다. 디렉터리는 쓰기 가능하지만 기존 `.lock`만 쓰기 불가능한 상황을 `chmod 000`으로 만들자 `open`은 실제 `EACCES(13)`로 실패했고, 양쪽 writer는 그대로 read/merge/rename을 수행했습니다. 실제 C++ 두 프로세스 검사에서 Dictionary와 Mozc 모두 서로 다른 항목 하나가 **4/4회 유실**됐습니다; 정상 잠금에서는 각각 **0/4회**였습니다. Windows의 ACL·LockFileEx 실패에서도 진행하는 코드는 같지만 그 플랫폼에서의 실험은 하지 않았습니다. 잠금 성공 여부를 노출하고 실패하면 디스크 저장을 중단하면서 변경분·`touched`를 재시도용으로 유지해야 합니다. 디렉터리 전체가 읽기 전용인 경우는 별도로 검사했으며, Mozc는 기존 파일을 보존하고 권한 복구 뒤 다음 `save()`에 성공했습니다.

4. **중간 — `used()`의 날짜 갱신이 다른 프로세스의 승격·삭제를 되돌립니다.**

   `Windows/engine/src/UserDictionary.cpp:288`, `:291`, `:292`, `:293`. A/B가 어제 날짜의 같은 weak `so`를 읽은 상태에서 A가 두 번째 교정으로 sure를 저장하고 B가 `used(so, KOREAN)`을 호출하면, B의 변경은 날짜뿐인데도 전체 weak Entry가 최신 sure를 덮습니다. 실제 결과는 `promoted=sure after-other-used=weak`였습니다. A가 해당 학습을 삭제한 경우도 `forgotten=absent after-other-used=weak`로 삭제가 취소됐습니다. 잠금은 정상 획득했고 저장도 순차 실행했습니다. 같은 키의 변경을 Entry 단위 덮어쓰기로 병합하지 말고, 잠금 안에서 최신 상태에 사용 날짜 갱신·교정·삭제라는 연산을 적용해야 합니다; 사용 기록은 이미 삭제된 항목을 복원하거나 sure를 weak로 낮추면 안 됩니다. 다른 키를 저장할 때 기존 삭제를 복원하는 옛 문제는 재현되지 않았습니다.

5. **중간 — Mozc의 같은 후보 학습 횟수가 뒤로 줄어듭니다.**

   `Windows/japanese/MozcConverter.cpp:376`, `:387`, `:389`, 점수 적용 `:196`. 같은 파일을 빈 상태로 읽은 A/B 중 A가 `じこ → 事故`를 네 번 학습·저장한 뒤 B가 동일 후보를 한 번 학습·저장하면 파일 값은 `じこ\t事故\t4`에서 `じこ\t事故\t1`로 바뀝니다. 다른 후보를 고른 상황이 아니므로 “latest choice wins” 정책으로 후보를 교체할 필요가 없는데도, 같은 후보의 누적 학습 강도가 낮아집니다. 실제 MozcConverter 두 인스턴스로 재현했습니다. 저장 이후의 증가분을 보관해 같은 reading·surface의 최신 count에 더하고, surface가 실제로 바뀐 경우에만 기존 후보를 제거하는 규칙을 적용해야 합니다.

6. **중간 — heartbeat가 해제된 `.install`을 일반 파일로 재생성하여 다음 설치를 막습니다.**

   `Mac/get.sh:71`, `:72`, `:87`; 동일한 `Mac/pkg/postinstall:34`, `:35`, `:50`. 백그라운드 루프가 PID·생존 검사를 통과한 직후 부모가 `release_lock`으로 디렉터리를 지우고, 이어 루프의 `touch "$lock"`가 실행되면 같은 경로에 빈 일반 파일이 생깁니다. 원본 함수를 그대로 실행한 첫 격리 검사에서 자연 발생했고, `touch` 직전의 실행 순서만 제어한 재검사에서도 일반 파일 생성과 다음 `take_lock`의 exit 1을 확인했습니다. `.install/pid`를 읽을 수 없으므로 오래된 잠금으로 판정될 때까지 get.sh는 즉시 실패하고 postinstall은 대기 후 실패합니다. 최소한 `touch -c`처럼 없는 경로를 만들지 않는 갱신으로 바꾸고, 해제와 heartbeat 종료 순서도 관리해야 합니다.

   루프의 stdout/stderr는 비어 있었고 부모 종료 직후에는 살아 있었지만 31초 뒤 종료됐습니다; 지속적인 고아 루프나 `Terminated` 로그는 재현되지 않았습니다. postinstall의 **획득 timeout**은 여전히 exit 1이며 staging을 보존합니다. 반면 획득 뒤 복사 중 잠금 자체를 없애는 격리 대역에서는 exit 0이었습니다: `release_lock`의 소유자 불일치는 성공으로 반환되므로, “획득 실패”와 “획득 후 소유권 상실”의 결과를 구분해야 합니다.

7. **중간 — 근사 Extended_Pictographic 범위가 서로 다른 문자까지 묶어 삭제합니다.**

   `Windows/engine/src/InputSession.cpp:42`, `:73`, `:74`, Kotlin `Android/engine/src/main/kotlin/com/netmaru/rea/engine/InputSession.kt:1552`, `:1587`. 새 범위는 emoji가 아닌 `U+21A0 ↠`도 Pict로 분류합니다. C++와 실제 컴파일된 Kotlin의 `dropLastCharacter`에 `A\u21A0\u200D\u21A0`를 넣으면 모두 `A`만 남지만 Swift `String.dropLast()`는 `A\u21A0\u200D`를 남깁니다. 즉 변환 후보에 이 문자열이 있으면 Delete 한 번으로 마지막 화살표뿐 아니라 그 앞 문자도 지웁니다. `A\u200B\u0301`에서도 C++/Kotlin은 `A`, Swift는 `A\u200B`였고, SpacingMark 예 `Aका`는 C++/Kotlin이 `Aक`, Swift가 `A`였습니다. 이 검사는 함수의 실제 결과이며 기본 Mozc 사전이 해당 후보를 제공하는지는 확인하지 않았습니다. 정식 Grapheme_Cluster_Break·Extended_Pictographic 속성표와 필요한 규칙을 사용하고 Unicode의 경계 테스트를 추가해야 합니다. [Unicode UAX #29](https://www.unicode.org/reports/tr29/).

테스트가 검사하는 범위도 나눠 보아야 합니다. 추가 Caps 테스트는 단순 대문자 출력뿐 아니라 결정 규칙·dictionary 우선·Caps 전환을 통과하고, 숫자 form 회귀는 Escape 뒤 Enter의 소비 여부까지 검사합니다. Mozc에는 실제 두 프로세스 저장 검사가 생겼고(`Windows/japanese/test/mozc_converter_test.cpp:207`), 이번 실행에서도 `concurrent saves: 0/5 trials lost a reading`이었습니다. 반면 Dictionary의 새 저장 검사는 순차·서로 다른 키가 중심이며, 두 저장소 모두 잠금 실패·같은 항목 충돌을 다루지 않습니다. TIP 테스트의 `store.refuse`는 첫 쓰기부터 모두 거절하는 경우뿐이고(`Windows/tip/tip_test.cpp:807`), 부분 실패·기존 composition의 finish 실패·SendInput/CUAS 경로는 검사하지 않습니다. F9/F10 테스트 `:645`와 `:646`은 두벌식에서 `3`을 앱이 직접 입력하는 경우라 prefix가 조합 밖에 있습니다(`Windows/tip/Tip.cpp:1173`); 새로 만든 kana-minus-reading 분기의 숫자 흡수 경로를 직접 검증하는 테스트는 아닙니다. scope 테스트는 동적 변경과 private 로그 차단을 검사하지만 property 조회 실패는 주입하지 않습니다.

그 밖의 요청 사항에서는, 두 저장 경로가 서로의 저장을 호출하거나 두 `FileLock`을 중첩하여 잡는 경로는 발견하지 못했고 각 잠금은 rename까지 유지됩니다. `Target`에는 원래 `ITfContext`와 현재 top context 비교가 추가돼 있습니다(`Windows/tip/Tip.cpp:1246`, `:1256`, `:1266`); 실제 SendInput 전달 시점의 초점 동작까지 실행 검증한 것은 아닙니다. 세 README의 Fix typos 설명은 수동 길게 누르기와 자동 교정을 분리했고(`README.md:96`, `README.en.md:96`, `README.ja.md:96`), 1,950개 표와 과거 1,932개 성능 측정 조건도 반영됐습니다(`README.md:554`, `README.en.md:609`, `README.ja.md:583`).

실행 결과와 재현 자료는 다음과 같습니다. 보조 소스·데이터는 모두 `/tmp/koenja-review2.R2fQYN`에 있으며, 제품 소스·테스트·설정 파일은 수정하지 않았습니다.

- `cd Packages/SeamtypeCore && swift test`: exit 0, 563 tests / 49 suites 통과; 기존 `PreviewTraceTests`, `DictionarySplitCheckTests`의 조건부 skip 유지. 로그 `swift.log`.
- `bash Windows/engine/build.sh`: exit 0, `ko-en-ja: 975/975`, `ko-ja-en: 975/975`, `golden: 1950/1950`. 병렬 빌드 중 측정한 성능 수치는 회귀 판정에 사용하지 않았습니다. 로그 `cpp.log`.
- `bash Windows/japanese/build.sh`: exit 0, `all passed`, 동시 저장 0/5 유실, top-1 29/30. `よろしくおねがいします`의 기존 top-1 차이는 이번 회귀로 분류하지 않았습니다. 로그 `mozc.log`.
- `cd Android && ./gradlew :engine:test`: exit 0, UP-TO-DATE. 기존 XML은 6 suites / 39 tests / failures 0 / errors 0이며, 캐시 재사용과 실제 실행을 구분하기 위해 저장소 밖 `PortTestsProbe.java`로 현재 컴파일된 `CapsLockTest` 6개와 `JapaneseSessionTest` 19개 메서드를 새 인스턴스에서 직접 호출해 **25개 통과**를 확인했습니다. 로그 `kotlin.log`, `kotlin-direct.log`.
- 지정된 `clang++ -std=c++20 -O2 -ffp-contract=off Windows/engine/test/session_test.cpp Windows/engine/src/*.cpp` 빌드 후 Data 인자로 실행: exit 0, `session: 226/226`. 실행 파일과 `session.log`는 임시 디렉터리에 있습니다.
- 실제 C++ 엔진·MozcConverter에 연결한 `probe.cpp`: 부분 쓰기 키 유실, 거절된 확정 뒤 marked 상태, 동일 Entry 승격/삭제 취소, 잠금 실패 동시 저장 유실, 같은 Mozc 후보 count 감소, 읽기 전용 디렉터리에서 재시도 보존을 확인했습니다. 로그 `probe.log`.
- `replay_probe.cpp`: 현재 `isReplayed` 본문과 시각·extra-info 대역으로 token / 반복 Test / 다음 실제 메시지 / 늦은 토큰 검사를 통과했습니다. TIP 전체 또는 실제 Windows 메시지 루프를 실행한 것은 아닙니다.
- `python3 /tmp/koenja-review2.R2fQYN/lock_probe.py`: exit 0. 두 스크립트의 동일한 잠금 함수, 정상·고아·동시 복구, 구버전 cleanup, heartbeat 종료, 해제 직후 재생성, postinstall의 timeout/소유권 상실을 scratch 경로에서 검사했습니다. postinstall은 경로를 scratch로 바꾼 복사·정리 분기만 사용했고 등록·pkill 구간은 실행하지 않았습니다. 로그 `lock.log`.
- `GraphemeProbe.java`로 현재 Kotlin 구현을 직접 호출하고 Swift `String.dropLast()` 및 실제 C++ 결과와 비교했습니다. 위 7번의 코드포인트별 차이는 모두 일치하게 재현됐습니다.
- `sh -n Mac/get.sh`, `sh -n Mac/pkg/postinstall`, `git diff --check`: 모두 exit 0. Windows TIP 빌드, Windows Registry 조작, 실제 설치/등록, 에뮬레이터·시뮬레이터·Xcode·설치된 Mac 입력기 실행, 도구 설치, commit/push는 하지 않았습니다.
