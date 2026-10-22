import Foundation
import Testing
@testable import SeamtypeCore

/// Typing English and romaji on Colemak: keys are the layout's letters, and 2-set Korean stays on
/// its physical keys (Colemak's s r e l… where QWERTY has d s k u…).
@MainActor @Suite struct LatinLayoutTests {
    /// What a QWERTY 2-set typist's fingers type on `layout`: the same keys, that layout's letters.
    static func keys(_ qwerty: String, on layout: LatinLayout) -> String {
        String(qwerty.map(layout.character(onSeat:)))
    }

    @Test func colemakMovesLettersAndKeepsSeats() {
        let c = LatinLayout.colemak
        #expect(Self.keys("dkssud", on: c) == "serrls")
        #expect(c.character(onSeat: ";") == "o")
        #expect(c.character(onSeat: "p") == ";")
        #expect(c.character(onSeat: "P") == ":")
        #expect(c.character(onSeat: "E") == "F")
        for seat in "qwertyuiopasdfghjkl;zxcvbnmQWERTYUIOPASDFGHJKL:ZXCVBNM" {
            #expect(c.seat(of: c.character(onSeat: seat)) == seat)
        }
        #expect(LatinLayout.colemakDH.character(onSeat: "g") == "g")
        #expect(LatinLayout.colemakDH.character(onSeat: "h") == "m")
        #expect(LatinLayout.colemakDH.character(onSeat: "v") == "d")
        #expect(LatinLayout.qwerty.character(onSeat: ";") == ";")
    }

    @Test func keyCodesTypeTheLayoutsLetters() {
        let c = LatinLayout.colemak
        #expect(c.character(keyCode: 14, shift: false, capsLock: false) == "f") // the e key
        #expect(c.character(keyCode: 41, shift: true, capsLock: false) == "O")  // the ; key
        #expect(c.character(keyCode: 35, shift: false, capsLock: true) == ";")  // Caps Lock leaves ; alone
        #expect(c.character(keyCode: 49, shift: false, capsLock: false) == nil) // Space: not a letter key
        #expect(c.isLetterSeat(";"))
        #expect(!LatinLayout.qwerty.isLetterSeat(";"))
    }

    @Test(arguments: LatinLayout.allCases)
    func decidesTheSameOnEveryLayout(layout: LatinLayout) {
        LatinLayout.$override.withValue(layout) {
            let engine = SplittingEngine(base: sharedEngine, english: sharedEngine.english)
            func decide(_ keys: String) -> Decision { engine.decide(keys, context: DecisionContext()) }
            // Korean on its keys, double consonants with Shift (쌀을), a particle on ; (에, Colemak's p key).
            #expect(decide(Self.keys("dkssudgktpdy", on: layout)).output == "안녕하세요")
            #expect(decide(Self.keys("Tkfdmf", on: layout)).output == "쌀을")
            #expect(decide(Self.keys("dlqfur", on: layout)).output == "입력")
            // English as the layout spells it, also with a particle typed on Korean's keys.
            #expect(decide("hello").output == "hello")
            #expect(decide("github" + Self.keys("dp", on: layout)).output == "github에")
            #expect(decide("QA" + Self.keys("fmf", on: layout)).output == "QA를")
        }
    }

    @Test func japaneseRomajiIsTheLayoutsLetters() {
        LatinLayout.$override.withValue(.colemak) {
            let d = trilingual.decide("kyouhaiitenkidesune", context: DecisionContext())
            #expect(d.language == .japanese, "\(d.explanation)")
        }
    }

    /// English on Colemak, romaji on QWERTY: the keys are Colemak's letters, and Japanese reads
    /// each through QWERTY, so a QWERTY romaji typist's fingers still make kana.
    @Test func romajiHasItsOwnLayout() {
        LatinLayout.$override.withValue(.colemak) {
            LatinLayout.$romajiOverride.withValue(.qwerty) {
                let keys = Self.keys("kyouhaiitenkidesune", on: .colemak)
                #expect(LatinLayout.romajiKeys(keys) == "kyouhaiitenkidesune")
                #expect(RomajiComposer.compose(keys).text == "きょうはいいてんきですね")
                let d = trilingual.decide(keys, context: DecisionContext())
                #expect(d.language == .japanese, "\(d.explanation)")
                #expect(trilingual.decide("hello", context: DecisionContext()).output == "hello")
            }
        }
        // English on QWERTY, romaji on Colemak: QWERTY's ; key is romaji's o.
        LatinLayout.$romajiOverride.withValue(.colemak) {
            #expect(LatinLayout.qwerty.isLetterSeat(";"))
            let keys = String("kyouha".map(LatinLayout.colemak.seat(of:))) // the keys Colemak's kyouha is on
            #expect(keys == "no;iha")
            #expect(RomajiComposer.compose(keys).text == "きょうは")
        }
        #expect(!LatinLayout.qwerty.isLetterSeat(";"))
    }

    @Test func aSessionTypesKoreanAndEnglishOnColemak() {
        LatinLayout.$override.withValue(.colemak) {
            let field = FakeTextField()
            let store = FileManager.default.temporaryDirectory.appendingPathComponent("araso-layout-\(UUID().uuidString).json")
            let engine = DictionaryEngine(base: SplittingEngine(base: sharedEngine, english: sharedEngine.english), storeURL: store)
            let session = InputSession(engine: engine, policies: AppPolicies(overrides: [:]))
            for ch in Self.keys("dkssud", on: .colemak) + " hello " + "github" + Self.keys("dp", on: .colemak) + " " {
                let key: InputSession.Key = ch == " " ? .printable(ch) : .letter(ch)
                if !session.handle(key, client: field) { field.insertText(String(ch), replacing: nil) }
            }
            #expect(field.text == "안녕 hello github에 ")
        }
    }
}
