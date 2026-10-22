import Foundation
import Testing
@testable import SeamtypeCore

/// A right ⌘ tap pauses Araso in the text field (`togglePause`).
@MainActor @Suite struct PauseTests {
    let field = FakeTextField()
    let session: InputSession

    init() {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("araso-pause-\(UUID().uuidString).json")
        session = InputSession(engine: DictionaryEngine(base: sharedEngine, storeURL: url))
        session.activate(app: "test.app")
    }

    /// Types letters and spaces; keys the session passes on are typed by the field.
    func type(_ s: String) {
        for ch in s {
            let key: InputSession.Key = ch.isLetter ? .letter(ch) : .printable(ch)
            if !session.handle(key, client: field) { field.insertText(String(ch), replacing: nil) }
        }
    }

    @Test func pausedKeysGoThroughAsTyped() {
        session.togglePause(client: field)
        type("jjkkhl dkssud")
        #expect(field.text == "jjkkhl dkssud")
        #expect(field.marked.location == NSNotFound)
    }

    @Test func pausingCommitsTheWordInProgress() {
        type("dkssud")
        session.togglePause(client: field)
        #expect(field.text == "안녕")
        #expect(field.marked.location == NSNotFound)
    }

    @Test func secondTapResumes() {
        session.togglePause(client: field)
        type("jk ")
        session.togglePause(client: field)
        type("dkssud ")
        #expect(field.text == "jk 안녕 ")
    }

    @Test func anotherTextFieldResumes() {
        session.togglePause(client: field)
        session.activate(app: "test.other")
        #expect(!session.paused)
    }
}

/// An app set to Strict keeps short commands as typed but still splits mixed words.
@MainActor @Suite struct StrictSplitTests {
    @Test func strictStillSplitsMixedWords() {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("araso-strict-\(UUID().uuidString).json")
        let policies = AppPolicies(overrides: ["test.terminal": .init(conversionThreshold: AppPolicy.strictThreshold)])
        let session = InputSession(engine: DictionaryEngine(base: splitting, storeURL: url), policies: policies)
        session.activate(app: "test.terminal")
        let field = FakeTextField()
        for ch in "ciw gst githubdp xdp hellodnjfem " {
            let key: InputSession.Key = ch.isLetter ? .letter(ch) : .printable(ch)
            if !session.handle(key, client: field) { field.insertText(String(ch), replacing: nil) }
        }
        #expect(field.text == "ciw gst github에 x에 hello월드 ")
    }
}
