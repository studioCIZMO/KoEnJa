import Foundation
import Testing
@testable import SeamtypeCore

/// Korean on 3-set final (세벌식 최종): jamo on their own keys for each position, digits and
/// punctuation included, read through the same `HangulComposer.compose(keys:)` as 2-set.
@MainActor @Suite struct SebeolsikTests {
    static func compose(_ keys: String) -> String? {
        KoreanLayout.$override.withValue(.sebeolsikFinal) { HangulComposer.compose(keys: keys)?.text }
    }

    @Test func composesByPosition() {
        #expect(Self.compose("jfsheamfncj4") == "안녕하세요")
        #expect(Self.compose("kf2uf") == "갔다")      // ㅆ final on 2
        #expect(Self.compose("kkd") == "끼")          // ㄱㄱ is ㄲ
        #expect(Self.compose("j/f") == "와")          // ㅗ on / for compound vowels
        #expect(Self.compose("i9tmr") == "뭐해")
        #expect(Self.compose("m8") == "희")
        #expect(Self.compose("jtXjt") == "없어")      // ㅄ on ⇧X
        #expect(Self.compose("jd@uf") == "읽다")      // ㄺ on ⇧2
        #expect(Self.compose("k/rsofSjf") == "괜찮아")
        #expect(Self.compose("kfxx") == "갂")         // ㄱㄱ final is ㄲ
        // No 도깨비불: a vowel after a final starts its own (broken) syllable.
        #expect(Self.compose("kfxf") == "각ㅏ")
        #expect(Self.compose("jfshea!") == "안녕ㄲ")
    }

    @Test func numbersAndSymbolsArentKorean() {
        #expect(Self.compose("2026") == nil) // a number stays a number
        #expect(Self.compose("00") == nil)
        #expect(Self.compose("Hello") == nil) // ⇧H is 0 in Korean: no jamo
        #expect(Self.compose("kf?") == nil)
    }

    @Test func twoSetIsUnchanged() {
        #expect(HangulComposer.compose(keys: "dkssud")?.text == "안녕")
        #expect(HangulComposer.compose(keys: "2026") == nil)
    }

    @Test func decides() {
        KoreanLayout.$override.withValue(.sebeolsikFinal) {
            let engine = SplittingEngine(base: sharedEngine, english: sharedEngine.english)
            func decide(_ keys: String) -> Decision { engine.decide(keys, context: DecisionContext()) }
            #expect(decide("jfsheamfncj4").output == "안녕하세요")
            #expect(decide("nnfwjgw").output == "쌀을")
            #expect(decide("jd3yex").output == "입력")
            #expect(decide("0tpd").output == "커피")
            #expect(decide("hello").output == "hello")
            #expect(decide("world").output == "world")
            #expect(decide("github" + "jc").output == "github에")
            #expect(decide("2026").output == "2026")
        }
    }

    /// Typing through a session, digits and symbols arriving as the app's printable keys.
    static func type(_ text: String) -> String {
        KoreanLayout.$override.withValue(.sebeolsikFinal) {
            let field = FakeTextField()
            let store = FileManager.default.temporaryDirectory.appendingPathComponent("araso-3set-\(UUID().uuidString).json")
            let engine = DictionaryEngine(base: SplittingEngine(base: sharedEngine, english: sharedEngine.english), storeURL: store)
            let session = InputSession(engine: engine, policies: AppPolicies(overrides: [:]))
            for ch in text {
                let key: InputSession.Key = ch.isLetter ? .letter(ch) : .printable(ch)
                if !session.handle(key, client: field) { field.insertText(String(ch), replacing: nil) }
            }
            return field.text
        }
    }

    @Test func digitsAndSymbolsTypeJamoInsideAWord() {
        #expect(Self.type("jfshea hello ") == "안녕 hello ")
        #expect(Self.type("kf2uf ") == "갔다 ")       // 2 is the final ㅆ
        #expect(Self.type(";f3 ") == "밥 ")           // a word starts on ; (ㅂ)
        #expect(Self.type("0tpd ") == "커피 ")        // and on 0 (ㅋ)
        #expect(Self.type("mr2uf. ") == "했다. ")
    }

    @Test func deleteTakesBackTheLastJamo() {
        KoreanLayout.$override.withValue(.sebeolsikFinal) {
            let field = FakeTextField()
            let store = FileManager.default.temporaryDirectory.appendingPathComponent("araso-3set-\(UUID().uuidString).json")
            let engine = DictionaryEngine(base: SplittingEngine(base: sharedEngine, english: sharedEngine.english), storeURL: store)
            let session = InputSession(engine: engine, policies: AppPolicies(overrides: [:]))
            for ch in "kf2uf" {
                let key: InputSession.Key = ch.isLetter ? .letter(ch) : .printable(ch)
                if !session.handle(key, client: field) { field.insertText(String(ch), replacing: nil) }
            }
            _ = session.handle(.delete, client: field)
            #expect(field.text == "갔ㄷ")
            _ = session.handle(.delete, client: field)
            _ = session.handle(.delete, client: field)
            #expect(field.text == "가")
        }
    }

    @Test func symbolsThatDontFitEndTheWord() {
        #expect(Self.type("jfshea! ") == "안녕! ")    // ㄲ can't follow 녕's ㅇ: it is the !
        #expect(Self.type("jfsheamfncj4! ") == "안녕하세요! ") // 욖 isn't a syllable anyone types
        #expect(Self.type(";f! ") == "밖 ")             // 밖 is
        #expect(Self.type("hello! ") == "hello! ")
        #expect(Self.type("100 ") == "100 ")          // a number stays a number
        #expect(Self.type("2026 ") == "2026 ")
        #expect(Self.type("2026 0tpd ") == "2026 커피 ") // after a space 0 starts a word again
        #expect(Self.type("it's ") == "it's ")
    }
}
