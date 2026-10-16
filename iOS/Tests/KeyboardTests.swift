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
        let session = KeyboardViewController.makeSession(converter: nil)
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

    func testConvertsKanjiWithSpace() throws {
        // DerivedData/Build/Products/<config>/REAKeyboardTests.xctest → DerivedData/SourcePackages
        let derived = Bundle(for: Self.self).bundleURL.deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        let dictionary = derived.appendingPathComponent("SourcePackages/checkouts/AzooKeyKanaKanjiConverter/Sources/KanaKanjiConverterModuleWithDefaultDictionary/azooKey_dictionary_storage/Dictionary")
        let memory = FileManager.default.temporaryDirectory.appendingPathComponent("rea-azookey")
        let start = footprintMB()
        let converter = try XCTUnwrap(AzooKeyConverter(dictionary: dictionary, memory: memory))
        let session = KeyboardViewController.makeSession(converter: converter)
        let proxy = FakeProxy(), client = ProxyClient(proxy: proxy)
        session.activate(app: nil)

        type("kanjihenkan ", into: session, client)
        let candidates = try XCTUnwrap(session.conversion?.candidates.map(\.text))
        XCTAssertTrue(candidates.contains("漢字変換"), "\(candidates.prefix(5))")
        session.choose(try XCTUnwrap(candidates.firstIndex(of: "漢字変換")), client: client)
        XCTAssertEqual(proxy.text, "漢字変換")
        print("REA memory: kanji converter ≈ \(footprintMB() - start) MB (process total \(footprintMB()) MB)")
    }

    /// Renders the keyboard at iPhone 17 Pro width; REA_SNAPSHOT=<path> saves it as PNG.
    func testLayoutKeysLineUp() throws {
        let keyboard = KeyboardViewController()
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 402, height: 316))
        window.backgroundColor = UIColor(white: 0.82, alpha: 1)
        window.rootViewController = keyboard
        window.makeKeyAndVisible()
        keyboard.view.layoutIfNeeded()
        let buttons = keyboard.view.allSubviews.compactMap { $0 as? UIButton }
        let rows = Dictionary(grouping: buttons) { Int($0.convert($0.bounds, to: nil).minY) }.values
        for row in rows {
            // every full row spans the same width, so its edges line up
            let frames = row.map { $0.convert($0.bounds, to: nil) }.sorted { $0.minX < $1.minX }
            if row.count >= 9, row.count != 9 || frames.count == 9 && frames[0].width > 40 {
                XCTAssertEqual(frames.first!.minX, 402 - frames.last!.maxX, accuracy: 1, "row centered")
            }
        }
        let letters = buttons.filter { $0.configuration?.attributedSubtitle != nil }
        XCTAssertEqual(letters.count, 26)
        XCTAssertEqual(Set(letters.map { Int($0.bounds.width) }).count, 1, "all letter keys one width")
        if let path = ProcessInfo.processInfo.environment["REA_SNAPSHOT"] {
            let image = UIGraphicsImageRenderer(bounds: keyboard.view.bounds).image { keyboard.view.layer.render(in: $0.cgContext) }
            try image.pngData()!.write(to: URL(fileURLWithPath: path))
        }
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

extension UIView {
    var allSubviews: [UIView] { subviews + subviews.flatMap(\.allSubviews) }
}
