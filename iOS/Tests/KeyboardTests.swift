import SeamtypeCore
import UIKit
import XCTest

/// A text field as the keyboard sees it: text before the cursor, plus marked text.
final class FakeProxy: NSObject, UITextDocumentProxy {
    var text = "", marked = ""
    var documentContextBeforeInput: String? { text + marked }
    var documentContextAfterInput: String? { nil }
    var selectedText: String? { nil }
    var documentInputMode: UITextInputMode? { nil }
    var documentIdentifier: UUID { UUID() }
    var hasText: Bool { !(text + marked).isEmpty }
    func adjustTextPosition(byCharacterOffset offset: Int) {}
    func setMarkedText(_ markedText: String, selectedRange: NSRange) { marked = markedText }
    func unmarkText() { text += marked; marked = "" }
    func insertText(_ s: String) { text += marked + s; marked = "" } // ponytail: real fields replace marked; ours never insert while marked
    func deleteBackward() { if !marked.isEmpty { marked.removeLast() } else if !text.isEmpty { text.removeLast() } }
}

@MainActor
final class KeyboardTests: XCTestCase {
    func type(_ keys: String, into session: InputSession, _ client: ProxyClient) {
        for c in keys {
            let key: InputSession.Key = c.isLetter ? .letter(c) : .printable(c)
            if !session.handle(key, client: client) { client.proxy.insertText(String(c)) }
        }
    }

    func testTypesThreeLanguagesOnOneLayout() {
        let start = footprintMB()
        let session = KeyboardViewController.makeSession()
        let proxy = FakeProxy()
        let client = ProxyClient(proxy: proxy)
        session.activate(app: nil)

        type("dkssudgktpdy hello ", into: session, client)
        XCTAssertEqual(proxy.text, "안녕하세요 hello ")

        type("watashiha", into: session, client)
        session.commit(client: client)
        XCTAssertTrue(proxy.text.hasSuffix("わたしは"), proxy.text)

        // The switch key turns the word just committed into the next language.
        let p2 = FakeProxy(), c2 = ProxyClient(proxy: p2)
        type("gksrmf ", into: session, c2)
        XCTAssertEqual(p2.text, "한글 ")
        XCTAssertTrue(session.handle(.cycle, client: c2))
        XCTAssertNotEqual(p2.text, "한글 ", "switch key should change the committed word")

        print("REA memory: engine + models ≈ \(footprintMB() - start) MB (process total \(footprintMB()) MB)")
    }

    private func footprintMB() -> Int {
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<natural_t>.size)
        let kr = withUnsafeMutablePointer(to: &info) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count) }
        }
        return kr == KERN_SUCCESS ? Int(info.phys_footprint) / 1_048_576 : -1
    }
}
