import Foundation
import Testing
@testable import SeamtypeCore

/// so switched to Japanese (そ), then me: the pick was learned for so when m came. Space converts the
/// phrase, Esc puts the kana back as the forced word, Return sends it in: some isn't learned as
/// Japanese on top (the Kotlin and C++ ports once forgot the pick when a conversion started).
@MainActor @Suite struct ConversionAfterSwitchTests {
    let field = FakeTextField()
    let converter = FakeConverter()
    let session: InputSession
    let engine: DictionaryEngine

    init() {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("araso-conversion-switch-\(UUID().uuidString).json")
        engine = DictionaryEngine(base: SplittingEngine(base: trilingual, english: trilingual.english, japanese: trilingual.segmenter), storeURL: url)
        session = InputSession(engine: engine, converter: converter, japaneseEnabled: true)
        session.activate(app: "com.apple.TextEdit")
    }

    func type(_ keys: String) {
        for ch in keys {
            let handled = ch.isLetter ? session.handle(.letter(ch), client: field) : session.handle(.printable(ch), client: field)
            if !handled, ch == " " { field.insertText(" ", replacing: nil) }
        }
    }

    var dictionary: UserDictionary { (session.engine as! DictionaryEngine).dictionary }

    @Test func aConversionCancelledBackToTheSwitchedWordDoesNotLearnIt() throws {
        type("so")
        while session.readings.map({ $0.languages[$0.selected] }) != .japanese {
            _ = session.handle(.cycle, client: field)
        }
        #expect(field.text == "そ")
        type("me")
        #expect(dictionary.languageEntry(for: "so")?.language == .japanese)
        type(" ")
        #expect(session.conversion != nil)
        _ = session.handle(.escape, client: field)
        #expect(session.conversion == nil)
        _ = session.handle(.enter, client: field)
        #expect(field.text == "そめ")
        #expect(dictionary.languageEntry(for: "some") == nil)
        #expect(dictionary.languageEntry(for: "so")?.language == .japanese)
    }

    /// Learning off (a password field): a correction teaches nothing; on again, it does.
    @Test func learningOffTeachesNothing() {
        engine.learning = false
        engine.learn("dkssud", as: .english, engineChoice: .korean)
        engine.learn("dkssud", as: .english, engineChoice: .korean)
        #expect(engine.learned["dkssud"] == nil)
        engine.learning = true
        engine.learn("dkssud", as: .english, engineChoice: .korean)
        engine.learn("dkssud", as: .english, engineChoice: .korean)
        #expect(engine.learned["dkssud"] == .english)
    }
}
