**수정 후 커밋**을 권고하며, `main`의 HEAD `4f2c106d42c5e01012d70998fa2093a5eda535d2` 대비 추적 파일 34개와 신규 파일 7개를 이전 세 보고서와 대조하고 허용된 테스트 및 저장소 밖 probe로 검증했습니다. 이전 보고서의 추가 문제 7건은 **고침 5 / 부분 2 / 안 고침 0**이며, 일반 입력 골든은 세 엔진 모두 1,950개가 동일하지만 TIP의 다른 부분 실패 경로에서 현재 키 유실을 재현했고 Dictionary의 교정 병합도 이번 작업 설명과 차이가 있습니다. 남은 작업은 아래 1번의 생략된 현재 키 쓰기 처리와 4번의 교정 병합 구현·설명 일치이며, Windows 네이티브 TIP·설치/제거, Mac 등록·설치, iOS·Android 실기기는 실행하지 않았으므로 그 환경의 발생 여부는 대역 검증과 구분했습니다.

| 이전 보고서의 7개 문제 | 판정 | 현재 근거·검증 결과·남은 수정 |
| --- | --- | --- |
| 1. 기존 단어 확정만 성공한 부분 쓰기를 현재 키의 성공으로 취급 | **부분** | 기존 재현인 `hello` 확정 성공 → 새 키의 `SetText` 실패는 `Windows/tip/Tip.cpp:1393`, `:1492`의 `lost=true → handled=false`로 고침; 실제 `Client` 본문과 C++ 엔진을 연결한 대역 결과 `writes=2 failed=1 lost=1 handled=0`. 그러나 **높음 — 현재 키 유실이 남는 경로**: Caps off로 `hello` 조합 → Caps on으로 `x` → 기존 `hello`의 `SetText`는 성공하고 바로 다음 `GetStart`만 실패하도록 주입; `:1397`은 `failed=true`만 설정하고, 새 키의 `replace`는 `:1381`에서 그냥 반환, `:1492`는 키를 소비; 결과 `text=hello writes=1 failed=1 lost=0 handled=1 keys=`. 정상 대조군의 새 키 미리보기는 `helloㅌ`; 실제 Windows 호스트에서 해당 API 실패가 일어나는지는 **추정**, 소스 본문을 실행한 키 유실은 확인. 수정: 앞 단어 확정과 현재 키 효과를 구분하고, 실패 뒤 생략된 현재 키 쓰기도 미전달로 기록하거나 한 번의 교체로 합치기; 위 후처리 실패 회귀 검사 추가. `:1486` 주석의 `commit-then-show` 순서를 그대로 지킨 재현이며, 새로운 엔진 쓰기 순서를 가정한 설계 확장 요구가 아님 |
| 2. 실패한 `finish()` 뒤 composition 잔존 | **고침** | `Windows/tip/Tip.cpp:1558`, `:1564`, `:1567`, `:1568`: 원래 context callback 안에서 확정 결과와 무관하게 기존 composition 종료·분리; 실제 `finish()` 본문에 첫 `SetText` 실패를 주입한 대역 결과 `detached=1 ended=1 ec=123 keys=`. `hr != S_OK`의 즉시 `drop()`과 늦은 callback의 기존 composition 종료 분기도 유지(`:1570`, `:1577`); 실제 TSF 비동기 호스트 실행은 미검증 |
| 3. 잠금 획득 실패에도 무잠금 저장 | **고침** | `Windows/engine/src/FileLock.h:37`, `:43`, `:53`, `Windows/engine/src/UserDictionary.cpp:285`, `Windows/japanese/MozcConverter.cpp:389`: 실패 여부 노출 및 디스크 쓰기 중단. `.lock`만 `chmod 000`인 실제 파일 검사에서 양쪽 `locked=0`; Dictionary는 파일을 만들지 않고 메모리 변경 보존, Mozc는 기존 파일 byte-identical; 권한 복구 뒤 다음 저장에서 보류 항목까지 보존. Windows `LockFileEx` 실패는 코드 검토, 실행 검사는 macOS `flock` 분기 |
| 4. 같은 Dictionary 항목의 날짜 갱신이 승격·삭제를 되돌림 | **부분** | **원래 `used()`의 두 재현은 고침**: `Windows/engine/src/UserDictionary.cpp:302`의 사용 기록 분기, `:306`의 삭제 유지, `:307`의 날짜만 갱신으로 `after-promotion=sure`, `after-delete=absent` 확인. 다만 작업 설명의 **`corrections apply on the freshest state`는 미구현**: 교정은 잠금 전 로컬 `store`에서 계산(`:266`), 저장 때 최신 항목을 지우고 로컬 Entry로 교체(`:312`, `:313`). **중간 — 검증된 설명·동작 차이**: 빈 동일 파일을 읽은 A/B가 각각 `learn(so, KOREAN, ENGLISH)` 한 번씩 순차 저장하면 최종 `weak`; 한 인스턴스에서 같은 교정 두 번은 `sure`. `:281` 주석에는 snapshot 교체 정책이 명시돼 있으므로 그 정책 자체를 새 결함으로 분류하지 않음; 이번 수정 설명의 최신 상태 교정 적용 주장과 불일치하는 범위만 남김. 수정: 교정 연산을 보류·기록해 잠금 안의 최신 Dictionary에 한 번씩 적용하거나, 의도한 정책이 snapshot 교체라면 수정 설명을 그 범위로 정정 |
| 5. Mozc의 같은 후보 학습 횟수가 감소 | **고침** | `Windows/japanese/MozcConverter.cpp:377`, `:378`, `:391`, `:394`, `:410`: `added` 증가분 병합, 다른 surface 제거, 성공 후에만 증가분 해제. 빈 파일을 읽은 A/B에서 같은 `じこ → 事故`를 각각 4회/1회 저장한 실제 결과 `じこ\t事故\t5`; 잠금 실패·재시도 뒤에도 5 유지. 기존 두 프로세스 동시 저장 검사도 `0/5 trials lost a reading` |
| 6. heartbeat가 해제된 `.install`을 일반 파일로 재생성 | **고침** | `Mac/get.sh:76`, `:91`, `:92` 및 동일 함수인 `Mac/pkg/postinstall:39`, `:54`, `:55`: `touch -c`, keeper 종료·wait 뒤 삭제, 소유권 불일치 exit 1. 추출한 실제 함수의 격리 검사에서 해제 후 일반 파일 없음, 즉시 재획득 성공, 종료 직후 keeper 생존 없음, 고아 잠금 동시 복구 8회 중 중첩 진입 0회; 구버전 10분 cleanup에도 살아 있는 잠금 보존. postinstall 복사 중 잠금만 제거한 대역은 `:87`에서 자체 메시지와 exit 1, staging 보존; 획득 timeout도 exit 1 및 staging 보존 |
| 7. 근사 Unicode 범위가 서로 다른 grapheme까지 삭제 | **고침** | `Windows/engine/src/InputSession.cpp:27`, `:38`, `:62`, `Android/engine/src/main/kotlin/com/netmaru/rea/engine/InputSession.kt:1553`, `:1561`: 정식 속성표 조회와 GB9c 포함 경계 규칙. `Android/tools/grapheme/generate.py`를 저장소 밖에서 Unicode 16.0.0 자료로 재실행한 C++·Kotlin 표는 현재 파일과 byte-identical(656 break ranges, 403 InCB ranges); 양쪽 `GraphemeBreakTest.txt` 1,093개 사례 통과. 이전 `A↠‍↠`, `A\u200B\u0301`, `Aका` 오류와 반각 탁점·IVS·ZWJ·국기·NFD 한글 검사도 통과 |

**이번 라운드에서 새로 도입됐다고 확인한 별도 문제: 없음** — 위 두 ‘부분’은 기존 부분 쓰기 복구 및 같은 항목 병합의 남은 범위이며, 새 문제 목록으로 중복 계상하지 않음.

이전 보고서의 13행 표에서 ‘부분’이던 여섯 항목도 대조 완료: 쓰기 실패 복구는 위 1번, Dictionary는 위 3·4번, Mozc는 위 3·5번, 설치 잠금은 위 6번, grapheme은 위 7번에 반영; 나머지 scope 읽기 실패는 `Windows/tip/Tip.cpp:1071`, `:1076`의 `nullopt`와 `:1054`의 같은 context 마지막 상태 유지로 기존 지적 범위에서 고침(정적 검토; `tip_test.cpp:850`의 실패 주입 네이티브 테스트는 미실행).

**전체 미커밋 diff의 일반 입력 회귀 판정: 정상적으로 읽고 쓸 수 있는 일반 필드에서는 네 플랫폼 모두 HEAD 대비 새 회귀 미검출** — 위 Windows 실패 경로와 학습 병합 잔여 범위를 제외한 판정이며, 실제 앱에서의 무회귀 보증 범위는 아님.

| 플랫폼 | HEAD 대비 동작과 확인 범위 |
| --- | --- |
| Mac | 공유 Swift 엔진의 일반 학습·조합 경로 유지; `Mac/Sources/InputController.swift:137`, `:240`의 추가 학습 차단은 Secure Event Input 상태에 한정. Caps Lock·세벌식·일본어·수정 동작을 포함한 Swift 테스트 통과; 설치 잠금·등록 변경은 일반 키 처리 밖. 설치된 입력기와 실제 등록은 실행하지 않음 |
| iOS | 동일 Swift 골든 유지; 변경은 선택 영역 길이·조합 초기화(`iOS/Keyboard/ProxyClient.swift:59`, `:84`), 민감 필드 터치 교정 차단(`iOS/Keyboard/KeyboardViewController.swift:732`), 로그 백업 제외 재시도(`iOS/Shared/SlipLog.swift:26`) 및 build 번호. 선택 없는 일반 필드의 처리 분기에는 추가 출력 변경 없음; UIKit·실기기 테스트는 미실행 |
| Android | Kotlin 골든 1,950개 동일; 새 Caps Lock API는 기존 일반 `handle(key, client)` 호출을 유지하는 `Android/app/src/main/kotlin/com/netmaru/rea/Typist.kt:214`의 동작을 바꾸지 않음. 일본어 후보 Delete의 grapheme 단위 변경과 `learning=false` 차단은 의도된 수정; 새 Caps·일본어 세션·사전 검사 직접 실행 통과 |
| Windows | C++ 골든 1,950개 동일, 세벌식·숫자 form·Caps 약어·dictionary 우선·실제 Shift·Caps 전환 검사 통과; Caps Lock과 Shift 분리, F9/F10 숫자 보존, 학습 영속 저장은 의도된 동작 변경. 정상 쓰기 경로의 추가 키 유실·오출력은 미검출; 위 1번의 API 실패 경로는 별도. TIP 전체·실제 CUAS·MSVC·Windows 설치는 미실행 |

검증 결과와 재현 자료:

- `cd Packages/SeamtypeCore && swift test`: exit 0, **563 tests / 49 suites**; 조건부 실행 테스트의 기존 조건 유지.
- `bash Windows/engine/build.sh`: exit 0, **975/975 + 975/975 = 1,950/1,950**.
- `bash Windows/japanese/build.sh`: exit 0, **all passed**, 동시 저장 유실 **0/5**; 기존 `よろしくおねがいします` top-1 차이는 동일하게 남아 29/30, 이번 회귀로 계상하지 않음.
- `cd Android && ./gradlew :engine:test`: exit 0, **5 tasks UP-TO-DATE**; 저장소 밖 Java probe에서 현재 컴파일된 6개 suite의 테스트 메서드 **40개 직접 실행 통과**, Kotlin 골든 1,950개와 grapheme 1,093개 포함.
- 저장소 루트에서 지정된 `clang++ -std=c++20 -O2 -ffp-contract=off Windows/engine/test/session_test.cpp Windows/engine/src/*.cpp` 빌드 후 Data 인자로 실행: exit 0, **session: 1328/1328**.
- 이번 `swift test` 산출물 `libSeamtypeCore.a`에 기존 Swift golden 소스를 저장소 밖에서 연결·실행: **현재 Swift 출력 = 저장된 `golden.tsv` = HEAD의 `golden.tsv`**, 1,950줄 byte-identical; SHA-256 `656d0f02e4c83bf33fccfd4a08e6865abc1d5feef668a3ff5a5e68c7a26e56d5`.
- `/tmp/koenja-review3.QXAz8T/tip_probe.cpp`: 현재 `Client`와 `Tip::finish()` 본문을 그대로 추출, 실제 엔진에 COM 대역 연결; 두 부분 실패 경로·성공 대조군·실패한 finish 종료 검사, 결과 `tip-probe.log`.
- `/tmp/koenja-review3.QXAz8T/persistence_probe.cpp`: 실제 DictionaryEngine·MozcConverter의 동일 항목·증가분·잠금 실패·재시도 검사, 결과 `persistence.log`.
- `/tmp/koenja-review3.QXAz8T/lock_probe.py`: 실제 두 스크립트의 동일 잠금 함수와 postinstall 복사 분기를 scratch 경로·rsync 대역에서 실행, 결과 `lock.log`; 이미 기다린 keeper를 probe가 한 번 더 기다린 경고는 harness에서 제거하고 해당 race만 다시 검사하여 `lock-race.log`에서 stderr 없음 확인.
- `/tmp/koenja-review3.QXAz8T/check_tables.py`: 저장소 밖 표 재생성·byte 비교, 결과 `tables.log`; `git diff --check` exit 0.

보고서 외 저장소 소스·테스트·설정 파일 수정 없음; 허용된 테스트 산출물과 `/tmp/koenja-review3.QXAz8T`의 검토 자료만 생성, commit·push·도구 설치·에뮬레이터·시뮬레이터·Xcode·설치된 Mac 입력기 실행 없음.
