# 개인화 로드맵: Araso와의 차별점, MyTypeless와의 통합

KoEnJa의 엔진은 Araso(MIT)에서 왔다. 차별점은 **개인 메모리**다. 타이핑과 음성(MyTypeless)이 한 사전을 쓰고, 둘 다 기기 밖으로 아무것도 보내지 않는다.
참고한 것:
- Typeless: 사전 수동·자동 추가, 일괄 가져오기, 편집 추적 학습, 기록/인사이트. 클라우드 기반이다.
- MyTypeless: `PersonalDictionary`(manual/auto 출처), `AutoLearner`(삽입 후 사용자가 고친 고유명사 학습), Insights. 전부 로컬이다.

Araso가 이미 가진 것: 전환 키로 고친 단어를 학습한다. 한 번 고치면 기울고 30일 뒤 잊으며, 두 번 고치면 확정한다. 앱별 항목과 조사 붙이기도 있다.

## 1단계 — 완료 (38388fd)

| 기능 | 내용 |
|---|---|
| 한글로 단어 추가 | 설정 › 사전 › + 에서 `깃허브`처럼 쓰면 키(`rltgjqm`)를 계산한다. Araso는 영문 키로만 넣을 수 있다. |
| MyTypeless 사전 공유 (Mac) | `~/Library/Application Support/MyTypeless/dictionary.json`의 단어를 **읽기 전용**으로 쓴다. 한글은 2벌식 키로, 라틴 문자는 쓴 그대로와 소문자로 확정 단어처럼 이긴다. 사용자가 직접 넣은 항목이 우선한다. 파일이 바뀌면 다음 입력 전환 때 다시 읽는다. dictionary.json에는 쓰지 않는다. |

알려진 한계:
- 한글 역변환은 2벌식 QWERTY만 된다. 3벌식이나 Colemak에서는 계산하지 않는다.
- 가나·한자 용어는 아직 쓰지 않는다.
- 입력 메뉴의 '잊기'로는 MyTypeless 단어가 지워지지 않는다. MyTypeless에서 지워야 한다.

## 2단계 — 다음 차용 목록 (우선순위 순)

1. **사전 화면에 출처 표시**: 직접 / 학습 / MyTypeless 필터. Typeless와 MyTypeless의 All·Auto·Manual에 해당한다.
2. **반대 방향 공유**: KoEnJa가 `~/Library/Application Support/KoEnJa/terms.json`에 `[{"term","language","source"}]`를 쓴다. MyTypeless는 이를 SpeechAnalyzer `contextualStrings`의 편향 단어로 읽는다. 쓰는 단어는 직접 넣은 것과 두 번 고친 것뿐이다. 계약 파일만 정하고, MyTypeless 쪽 구현은 그 프로젝트에서 한다.
3. **교정 기반 고유명사 제안**: MyTypeless `AutoLearner`의 정렬(diff) 방식을 쓴다. 단어가 들어간 직후 사용자가 Backspace로 그 단어를 지우고 다시 쓰면, 그 결과를 사전 **후보**로 올린다. 자동 등록하지 않고 사용자 승인 후 등록한다(competitors.md의 동의 원칙). 비밀번호 칸과 마스킹된 칸에서는 하지 않는다.
4. **CSV 가져오기/내보내기**: Typeless의 bulk-import에 해당한다. 로컬 파일로만 한다.
5. **인사이트**: 언어별 입력 비율, 자동 판정 횟수, 전환 키를 아낀 횟수. 텍스트는 저장하지 않고 숫자만 센다.
6. **일본어 용어**: 가나·한자 용어와 읽기를 AzooKey 사용자 사전에 넣는다.
7. **3벌식·Colemak 역변환**, 그리고 Android(Kotlin)와 Windows(C++) 엔진에 같은 기능 포팅.

## 3단계 — 장기: Mac·Windows에서 KoEnJa + MyTypeless 통합

목표는 **"치든 말하든 언어를 자동으로 알아듣는 하나의 입력기"**다.

- **공통 개인 메모리**: 지금의 파일 계약을 SwiftPM 패키지 `PersonalMemory`로 옮긴다.
  - 스키마(competitors.md 권고): `entries(normalized_term, display_term, locale_tags, keys, variants, source, use_count, enabled)`와 `correction_events`.
  - 로컬에만 저장하고 동기화는 꺼 둔다.
- **언어 판단 공유**: 앱별·직전 문맥의 타이핑 언어를 음성 인식 locale 힌트로 쓴다(ko-KR/ja-JP/en-US 자동 선택). 받아쓴 단어와 언어는 타이핑 엔진의 학습으로 돌아간다.
- **Mac 단일 앱**: 입력기(InputMethodKit) 안에 Fn 길게 누르기 받아쓰기를 넣는다. 입력기는 이미 포커스를 가진 프로세스라 붙여넣기 대신 `insertText`로 바로 넣을 수 있고, 손쉬운 사용 권한이 필요 없다.
- **Windows**: TSF IME(이미 있음)에 받아쓰기를 더한다. 로컬 음성 인식(Windows 음성 또는 whisper.cpp)과 같은 메모리 스키마를 C++로 옮긴다.
- **순서**: 2단계 2번(양방향 파일 계약) → `PersonalMemory` 패키지 → Mac 단일 앱 → Windows.
