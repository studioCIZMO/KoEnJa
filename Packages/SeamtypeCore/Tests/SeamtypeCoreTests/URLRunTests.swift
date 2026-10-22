import Foundation
import Testing
@testable import SeamtypeCore

/// After a URL or path marker (`://`, `www.`, `~/`, `x@`…), segments go in as typed; a lone `.`
/// or `/` is no marker (docs/url-run.md).
@MainActor @Suite struct URLRunTests {
    let field = FakeTextField()
    let learning: DictionaryEngine
    let session: InputSession
    let japanese: InputSession

    init() {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("araso-url-\(UUID().uuidString).json")
        learning = DictionaryEngine(base: SplittingEngine(base: trilingual, english: trilingual.english, japanese: trilingual.segmenter),
                                    storeURL: url)
        session = InputSession(engine: learning, policies: .escSwitches)
        japanese = InputSession(engine: learning, policies: .escSwitches, japaneseEnabled: true)
    }

    /// Keys through `session` (⎋: Esc); the app types what is passed on.
    func type(_ s: String, into session: InputSession? = nil) {
        let session = session ?? self.session
        for ch in s {
            let key: InputSession.Key = ch == "⎋" ? .escape : ch.isASCII && ch.isLetter ? .letter(ch) : .printable(ch)
            if !session.handle(key, client: field) { field.insertText(String(ch), replacing: nil) }
        }
    }

    @Test(arguments: ["https://example.com/dl ", "https://a.io/?q=dl&p=sk ", "cd ~/src/rk ", "www.ak.com/bb ",
                      "/usr/lib/dl ", "./ak/sk ", "http://localhost:3000/dl ", "localhost:3000/dl "])
    func segmentsAfterAMarkerStayAsTyped(_ keys: String) {
        type(keys)
        #expect(field.text == keys)
    }

    @Test func emailAfterTheAt() {
        type("x@gmail.com ")
        #expect(field.text.hasSuffix("@gmail.com "))
    }

    @Test(arguments: [("@dlrjs ", "@이건 "), ("ska/du ", "남/여 "), ("TV/fkeldh ", "TV/라디오 "),
                      ("L.A.fh ", "L.A.로 "), ("1:1ansdml ", "1:1문의 "), ("/sk ", "/나 ")])
    func noMarkerNoChange(_ keys: String, _ text: String) {
        type(keys)
        #expect(field.text == text)
    }

    /// REA: a case particle glued to the end of an address is Korean, not a path's last part;
    /// a run that starts with one dot is a dotfile, an ellipsis isn't.
    @Test(arguments: [("x@gmail.comdmfh ", "x@gmail.com으로 "), ("www.a.comdp ", "www.a.com에 "),
                      ("https://x.io/dp ", "https://x.io/dp "), ("https://x.io/yorkfmf ", "https://x.io/yorkfmf "),
                      (".env ", ".env "), ("...dma ", "...음 ")])
    func reaAddressParticleAndDotfile(_ keys: String, _ text: String) {
        type(keys)
        #expect(field.text == text)
    }

    @Test func japaneseOnKeepsTheURLHalfWidth() {
        type("https://x.io/ho.da ", into: japanese)
        #expect(field.text == "https://x.io/ho.da ")
    }

    @Test func kanaBeforeTheAtKeepsTheRestHalfWidth() {
        type("ho@gmail.com ", into: japanese)
        #expect(field.text == "ほ@gmail.com ")
    }

    @Test func japanesePunctuationWithoutAMarker() {
        type("kyouha.", into: japanese)
        #expect(field.text == "きょうは。")
    }

    @Test func theDictionaryIsNotAsked() {
        type("so⎋ x so⎋ ")
        #expect(learning.learned["so"] == .korean)
        field.text = ""; field.cursor = 0
        session.commitForNewContext(client: field)
        type("https://x.io/so ")
        #expect(field.text == "https://x.io/so ")
    }

    @Test func aSwitchedSegmentIsNotLearned() {
        type("https://x.io/dkssud")
        for _ in 0..<4 where !field.text.hasSuffix("안녕") { type("⎋") }
        #expect(field.text == "https://x.io/안녕")
        type(" ")
        type("https://x.io/dkssud⎋ ")
        #expect(learning.learned["dkssud"] == nil)
        #expect(learning.pending["dkssud"] == nil)
    }

    @Test func spaceEndsTheRun() {
        type("https://x.io/a dkssud ")
        #expect(field.text == "https://x.io/a 안녕 ")
        #expect(session.lastTrace?.previous == .english)
    }

    @Test func aNewContextEndsTheRun() {
        type("https://x.io/")
        field.cursor = 0 // a click at the start of the field
        session.commitForNewContext(client: field)
        type("dkssud ")
        #expect(field.text.hasPrefix("안녕 "))
    }

    @Test func aPastedURLCounts() {
        _ = session.handle(.other, client: field) // ⌘V
        field.insertText("https://x.io/", replacing: nil)
        type("dl ")
        #expect(field.text == "https://x.io/dl ")
    }

    @Test func aFieldThatCantBeReadOnlyHasWhatWasTyped() {
        field.readable = false
        _ = session.handle(.other, client: field)
        field.insertText("https://x.io/", replacing: nil)
        type("dkssud ")
        #expect(field.text == "https://x.io/안녕 ")
    }
}
