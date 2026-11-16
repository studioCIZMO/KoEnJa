# KoEnJa — 에이전트 작업 규칙

인수인계 상세는 [HANDOFF.md](HANDOFF.md). 아래는 모든 세션에 적용되는 규칙.

- **보고는 한국어**(코드·명령·식별자는 원문).
- 커밋 author **CIZMO <cizmode@gmail.com>**, 메시지 끝에 `Co-Authored-By: Claude …` 줄. push는 `gh auth token -u studioCIZMO` 토큰으로(HANDOFF §4). `netmaru/REA`에 push 금지.
- **모든 변경은 Codex 리뷰 1회**(`codex exec --sandbox read-only …`).
- 릴리스 주체 CIZMO, K.K.(Apple 팀 N723QM4R64). Xcode는 릴리스판만.
- **공개 릴리스·심사 제출·스토어 게시는 사용자 확인 후.** Android AAB는 사용자가 업로드.
- Mac 업데이터 때문에 **Mac 빌드 없는 새 태그 금지**; 다른 플랫폼 에셋은 현재 릴리스에 추가.
- 엔진 변경은 Swift·Kotlin·C++ 셋 다 맞추고 골든(`Android/engine/src/test/resources/golden.tsv`) 4환경(Swift/Kotlin/clang/MSVC CI) 동일 유지. golden_input.txt에 주석 금지.
- Araso 동기화 시 `Packages/SeamtypeCore/UPSTREAM`·`Mac/UPSTREAM` 첫 줄 sha와 로컬 변경 목록 갱신.
- 전역 툴체인·시스템 설정 변경 금지(물어볼 것). 테스트 AVD는 REA_Pixel; `checkame` 에뮬레이터 금지.
- `~/.koenja/upload.jks`, `upload.properties`는 커밋·출력 금지. 비밀번호 입력 금지(ASC/Play/Xcode 로그인은 사용자).
- Mac 입력기: imklaunchagent 죽이지 말고 IME 바이너리를 직접 실행하지 말 것.
- README는 한국어 main, `README.ja.md`/`README.en.md` 함께 갱신.
