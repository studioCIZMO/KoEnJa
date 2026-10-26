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

    static let all = LanguagePreferences(order: [.korean, .english, .japanese])

    /// The same keys go to whichever language the user ranked first.
    func testLanguageOrderDecidesAmbiguousWords() {
        func typed(_ keys: String, _ order: [LanguagePreferences.Language]) -> String {
            let session = KeyboardViewController.makeSession(converter: nil, preferences: LanguagePreferences(order: order))
            let proxy = FakeProxy(), client = ProxyClient(proxy: proxy)
            session.activate(app: nil)
            type(keys, into: session, client)
            return proxy.text
        }
        for word in ["go ", "so ", "do "] {
            print("REA order: \(word) ko>en=\(typed(word, [.korean, .english])) en>ko=\(typed(word, [.english, .korean]))")
        }
        XCTAssertEqual(typed("go ", [.korean, .english]), "해 ")
        XCTAssertEqual(typed("go ", [.english, .korean]), "go ")
        // Japanese left out: romaji stays out of kana.
        XCTAssertFalse(typed("watashiha ", [.korean, .english]).contains("わ"))
        XCTAssertEqual(typed("watashiha ", [.japanese, .english]).prefix(4), "わたしは")
    }

    /// Seamtype's options reach the session: Return after a word only confirms it, or goes on.
    func testSeamtypeOptions() {
        for confirmsOnly in [true, false] {
            var preferences = Self.all
            preferences.returnConfirmsOnly = confirmsOnly
            let session = KeyboardViewController.makeSession(converter: nil, preferences: preferences)
            let client = ProxyClient(proxy: FakeProxy())
            session.activate(app: nil)
            type("hello", into: session, client)
            XCTAssertEqual(session.handle(.enter, client: client), confirmsOnly, "Return taken by the keyboard")
        }
    }

    func testTypesThreeLanguagesOnOneLayout() {
        let start = footprintMB()
        let session = KeyboardViewController.makeSession(converter: nil, preferences: Self.all)
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

    /// The word being composed is plain text: a web form moving to the next field keeps it,
    /// and the late commit doesn't type it again into the new field.
    func testComposingWordSurvivesFocusChange() {
        let session = KeyboardViewController.makeSession(converter: nil, preferences: Self.all)
        let proxy = FakeProxy(), client = ProxyClient(proxy: proxy)
        session.activate(app: nil)
        type("dlgusdn", into: session, client)
        XCTAssertEqual(proxy.text, "이현우")
        XCTAssertEqual(proxy.marked, "", "no marked text for the app to drop")
        proxy.text = "" // the next field
        session.commit(client: client)
        XCTAssertEqual(proxy.text, "")
    }

    /// The cursor moved away mid-word: the word stays, the next key starts a new one there
    /// (it doesn't delete what's now before the cursor).
    func testCursorMovedAwayMidWord() {
        let session = KeyboardViewController.makeSession(converter: nil, preferences: Self.all)
        let proxy = FakeProxy(), client = ProxyClient(proxy: proxy)
        session.activate(app: nil)
        type("rk", into: session, client)
        XCTAssertEqual(proxy.text, "가")
        proxy.text = "hello" // the text before the cursor after a tap elsewhere
        XCTAssertTrue(client.resetIfMovedAway())
        session.commitForNewContext(client: nil)
        type("s", into: session, client)
        XCTAssertEqual(proxy.text, "helloㄴ")
    }

        /// Typing correction: the neighbor key wins only when it makes a much likelier word.
    func testNeighborKeyPlausibility() {
        let heuristic = KeyboardViewController.makeHeuristic(preferences: Self.all)
        let p = { KeyboardViewController.plausibility($0, heuristic) }
        let margin = KeyboardViewController.correctionMargin
        XCTAssertGreaterThan(p("fh"), p("fg") + margin, "로 over ㄹㅎ")      // 로드스타 typed as fgem…
        // Not caught: 하능 for 하늘, both common syllables; that takes whole words.
        XCTAssertLessThan(p("gksmf"), p("gksmd") + margin)
        XCTAssertLessThan(p("gksk"), p("gksl") + margin, "하나 stays: both are words")
        for (word, typo) in [("fh", "fg"), ("gksmf", "gksmd"), ("gksk", "gksl")] {
            print("REA plausibility \(word)=\(p(word)) \(typo)=\(p(typo))")
        }
    }

    /// Suggestions: a one-slip typo is fixed on Space, a real word is left alone.
    func testSuggestions() {
        let korean = KeyboardViewController.makeHeuristic(preferences: Self.all).korean
        func ko(_ keys: String) -> Suggestions {
            Suggestions.korean(word: HangulComposer.compose(keys: keys)!.text, keys: keys, model: korean)
        }
        XCTAssertEqual(ko("wlagodgksek").autocorrect, "진행한다")   // 짐행한다
        XCTAssertEqual(ko("wlsgodgkske").autocorrect, "진행한다")   // 진행하낟
        XCTAssertNil(ko("xptmxmfmf").autocorrect)                   // 테스트를
        XCTAssertNil(ko("zlqhemrk").autocorrect)                    // 키보드가: unknown, no one-slip fix
        XCTAssertNil(ko("snffu").autocorrect)                       // 눌려: missing from our lists, iOS knows it
        XCTAssertTrue(ko("xptmxmfm").words.contains("테스트를"), "\(ko("xptmxmfm").words)") // 테스트르 completes
        let english = KeyboardViewController.makeHeuristic(preferences: Self.all).english
        XCTAssertEqual(Suggestions.english(word: "teh", model: english).autocorrect, "the")
        XCTAssertNil(Suggestions.english(word: "hello", model: english).autocorrect)
        // As the system keyboard: a key missing or pressed twice, an apostrophe left out, a lone i.
        XCTAssertEqual(Suggestions.english(word: "helo", model: english).autocorrect, "hello")
        XCTAssertEqual(Suggestions.english(word: "helllo", model: english).autocorrect, "hello")
        XCTAssertEqual(Suggestions.english(word: "dont", model: english).autocorrect, "don't")
        XCTAssertEqual(Suggestions.english(word: "Im", model: english).autocorrect, "I'm")
        XCTAssertEqual(Suggestions.english(word: "i", model: english).autocorrect, "I")
        XCTAssertNil(Suggestions.english(word: "lets", model: english).autocorrect)
        XCTAssertEqual(Set(Suggestions.neighbors["g"]!), Set("fhtyv"))
        XCTAssertTrue(Suggestions.oneSlip(String(repeating: "a", count: 65)).isEmpty)
        for word in ["teh", "wrold", "hellp"] {
            let result = Suggestions.english(word: word, model: english)
            if let fix = result.autocorrect { XCTAssertEqual(result.words.first, fix) }
        }
        let client = ProxyClient(proxy: FakeProxy())
        XCTAssertNil(client.text(in: NSRange(location: -1, length: 1)))
        XCTAssertNil(client.text(in: NSRange(location: 1, length: Int.max)))
    }

    func azooKey() throws -> AzooKeyConverter {
        // DerivedData/Build/Products/<config>/REAKeyboardTests.xctest → DerivedData/SourcePackages
        let derived = Bundle(for: Self.self).bundleURL.deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        let dictionary = derived.appendingPathComponent("SourcePackages/checkouts/AzooKeyKanaKanjiConverter/Sources/KanaKanjiConverterModuleWithDefaultDictionary/azooKey_dictionary_storage/Dictionary")
        let memory = FileManager.default.temporaryDirectory.appendingPathComponent("rea-azookey")
        return try XCTUnwrap(AzooKeyConverter(dictionary: dictionary, memory: memory))
    }

    /// Misses from the 1000-character run on the iPhone (UITests/Corpus.swift).
    func testCorpusMisses() throws {
        // The iPhone's order.
        let session = KeyboardViewController.makeSession(converter: try azooKey(), preferences: LanguagePreferences(order: [.korean, .japanese, .english]))
        let proxy = FakeProxy(), client = ProxyClient(proxy: proxy)
        session.activate(app: nil)
        for (keys, _) in corpus.prefix(6) { type(keys, into: session, client) }
        let before = proxy.text
        type("The keyboard ", into: session, client)
        XCTAssertEqual(String(proxy.text.dropFirst(before.count)), "The keyboard ", "a capitalized English word after Korean")
        // A number before a Japanese word goes in with it, once (3ji gave 33時).
        proxy.text = ""; session.commitForNewContext(client: nil)
        type("nihongo", into: session, client); session.commit(client: client)
        type("3ji ", into: session, client); session.commit(client: client)
        XCTAssertTrue(proxy.text.contains("3時") && !proxy.text.contains("33"), proxy.text)
        XCTAssertNil(Suggestions.korean(word: "버그를", keys: "qjrmfmf", model: KoreanModel()).autocorrect, "loanword + particle")
        XCTAssertEqual(Suggestions.korean(word: "짐행한다", keys: "wlagodgksek", model: KoreanModel()).autocorrect, "진행한다")
    }

    func testConvertsKanjiWithSpace() throws {
        let start = footprintMB()
        let converter = try azooKey()
        let session = KeyboardViewController.makeSession(converter: converter, preferences: Self.all)
        let proxy = FakeProxy(), client = ProxyClient(proxy: proxy)
        session.activate(app: nil)

        // While typing: kana on screen, candidates for the bar.
        type("nihongo", into: session, client)
        XCTAssertEqual(proxy.text, "にほんご")
        let typing = session.typingCandidates
        let nihongo = try XCTUnwrap(typing.first { $0.text == "日本語" }, "\(typing.prefix(5).map(\.text))")
        session.choose(typing: nihongo, client: client)
        XCTAssertEqual(proxy.text, "日本語")
        XCTAssertTrue(session.typingCandidates.isEmpty)
        proxy.text = ""

        // Return takes the first candidate, so it should be the whole phrase converted.
        type("nihonngonotesutoha", into: session, client)
        print("REA first candidates: \(session.typingCandidates.prefix(5).map(\.text))")
        XCTAssertEqual(session.typingCandidates.first?.text, "日本語のテストは")
        proxy.text = ""; session.commitForNewContext(client: nil)

        type("kanjihenkan ", into: session, client)
        let candidates = try XCTUnwrap(session.conversion?.candidates.map(\.text))
        XCTAssertTrue(candidates.contains("漢字変換"), "\(candidates.prefix(5))")
        session.choose(try XCTUnwrap(candidates.firstIndex(of: "漢字変換")), client: client)
        XCTAssertEqual(proxy.text, "漢字変換")
        // After a commit: what usually comes next.
        let next = converter.nextWords()
        XCTAssertFalse(next.isEmpty)
        print("REA next words after 漢字変換: \(next.prefix(8))")
        converter.forgetNextWords()
        XCTAssertTrue(converter.nextWords().isEmpty)
        print("REA memory: kanji converter ≈ \(footprintMB() - start) MB (process total \(footprintMB()) MB)")
    }

    /// Renders the keyboard at iPhone 17 Pro width; REA_SNAPSHOT=<path> saves it as PNG.
    func testLayoutKeysLineUp() throws {
        let keyboard = KeyboardViewController()
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 402, height: 237))
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
        // The system keyboard's measurements (pt, 402 wide): 33.5 keys, 6⅔ from the edge, 53 bar.
        func frame(_ name: String) -> CGRect { let b = buttons.first { $0.accessibilityLabel == name }!; return b.convert(b.bounds, to: nil) }
        XCTAssertEqual(frame("q").width, 33.5, accuracy: 0.3)
        XCTAssertEqual(frame("q").minX, 20 / 3, accuracy: 0.3)
        XCTAssertEqual(frame("q").minY, 36, accuracy: 0.5)
        XCTAssertEqual(frame("q").height, 127 / 3, accuracy: 0.3)
        XCTAssertEqual(frame("a").minY - frame("q").minY, 54, accuracy: 0.3)
        XCTAssertEqual(frame("shift").width, 45.3, accuracy: 0.5)
        XCTAssertEqual(frame("z").minX, frame("q").minX + (frame("w").minX - frame("q").minX) * 1.5, accuracy: 0.5, "z under the q/w gap")
        // A touch between q and w, or between the q and a rows, still lands on a key.
        let q = letters.first { $0.accessibilityLabel == "q" }!.convert(CGRect.zero, to: keyboard.view).origin
        let w = letters.first { $0.accessibilityLabel == "w" }!.convert(CGRect.zero, to: keyboard.view).origin
        let gapX = CGPoint(x: (q.x + w.x) / 2 + letters[0].bounds.width / 2, y: q.y + 20)
        XCTAssertEqual(keyboard.view.hitTest(gapX, with: nil)?.accessibilityLabel.map { "qw".contains($0) }, true)
        XCTAssertEqual(keyboard.view.hitTest(CGPoint(x: q.x + 10, y: q.y + 44 + 5), with: nil)?.accessibilityLabel.map { "qa".contains($0) }, true)
        // A touch in the gap goes to the nearer key: 3pt above the a key types a.
        XCTAssertEqual(keyboard.view.hitTest(CGPoint(x: frame("a").midX, y: frame("a").minY - 3), with: nil)?.accessibilityLabel, "a")
        // The gap between z and x (a row inside a row) lands on one of them.
        let z = frame("z"), x = frame("x")
        XCTAssertEqual(keyboard.view.hitTest(CGPoint(x: (z.maxX + x.minX) / 2, y: z.midY), with: nil)?.accessibilityLabel.map { "zx".contains($0) }, true)
        // Releasing the previous finger must not hide the next key's popup.
        let qButton = try XCTUnwrap(letters.first { $0.accessibilityLabel == "q" })
        let wButton = try XCTUnwrap(letters.first { $0.accessibilityLabel == "w" })
        qButton.sendActions(for: .touchDown)
        wButton.sendActions(for: .touchDown)
        qButton.sendActions(for: .touchUpInside)
        let popup = keyboard.view.subviews.compactMap { $0 as? UILabel }.first
        XCTAssertEqual(popup?.text, "w")
        XCTAssertEqual(popup?.isHidden, false)
        wButton.sendActions(for: .touchUpInside)
        XCTAssertEqual(popup?.isHidden, true)
        if let path = ProcessInfo.processInfo.environment["REA_SNAPSHOT"] {
            let image = UIGraphicsImageRenderer(bounds: keyboard.view.bounds).image { keyboard.view.layer.render(in: $0.cgContext) }
            try image.pngData()!.write(to: URL(fileURLWithPath: path))
        }
    }

    /// iOS makes a new controller each time the keyboard comes up and may keep the old ones:
    /// off screen, a controller drops its keys, and the engine is shared.
    func testControllerDropsKeysOffScreen() {
        let start = footprintMB()
        weak var button: UIView?
        weak var controller: KeyboardViewController?
        for _ in 0..<20 {
            autoreleasepool {
                let keyboard = KeyboardViewController()
                controller = keyboard
                let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 402, height: 237))
                window.rootViewController = keyboard
                window.makeKeyAndVisible()
                keyboard.view.layoutIfNeeded()
                button = keyboard.view.allSubviews.first { $0 is UIButton }
                keyboard.beginAppearanceTransition(false, animated: false)
                keyboard.endAppearanceTransition()
                window.rootViewController = nil
            }
        }
        XCTAssertNil(button, "keys kept off screen")
        XCTAssertNil(controller, "a closed keyboard is freed")
        print("REA memory: 20 keyboards ≈ \(footprintMB() - start) MB")
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
