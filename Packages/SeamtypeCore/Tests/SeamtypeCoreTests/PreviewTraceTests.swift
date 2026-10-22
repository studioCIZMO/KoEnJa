import Foundation
import Testing
@testable import SeamtypeCore

/// Prints what the underlined preview shows after every keystroke (run with --filter PreviewTrace).
@MainActor @Suite(.disabled(if: ProcessInfo.processInfo.environment["ARASO_TRACE"] == nil))
struct PreviewTraceTests {
    @Test(arguments: ["kyouhaiitenkidesune", "watashihagakuseidesu", "dkssudgktpdy", "keyboard", "githubdp", "arigatou"])
    func trace(keys: String) {
        let field = FakeTextField()
        let engine = DictionaryEngine(base: SplittingEngine(base: trilingual, english: trilingual.english, japanese: trilingual.segmenter), storeURL: nil)
        let session = InputSession(engine: engine, japaneseEnabled: true)
        var shown: [String] = []
        for ch in keys {
            _ = session.handle(.letter(ch), client: field)
            shown.append(field.text)
        }
        print("TRACE \(keys): " + shown.joined(separator: " → "))
    }
}
