import XCTest

/// Types on the 한영일 keyboard the way a person does, by tapping the screen where the keys are,
/// then reads the field. The keyboard must be the one up (on a simulator: in AppleKeyboards,
/// switched to once). Keys of another app's keyboard aren't in the accessibility tree, so the
/// positions are the layout's (KeyboardViewController), measured on an iPhone 17 Pro screen.
final class TypingTests: XCTestCase {
    var app: XCUIApplication!

    override func setUp() {
        continueAfterFailure = true
        app = XCUIApplication()
        app.launchArguments = ["-focusTry"]
        app.launch()
        let preset = app.buttons["한국어 > 일본어 > 영어"]
        if preset.waitForExistence(timeout: 2) { preset.tap() }
        XCTAssertTrue(app.textFields["tryField"].waitForExistence(timeout: 5))
        sleep(2) // the keyboard comes up
        // System keyboards show their keys to accessibility, 한영일 (another app's) doesn't.
        for _ in 0..<4 where app.keyboards.firstMatch.exists {
            app.buttons["다음 키보드"].tap()
            sleep(2)
        }
        XCTAssertFalse(app.keyboards.firstMatch.exists, "한영일 isn't up")
        sleep(2) // taps right after it comes up were lost
    }

    // Layout, in points: rows start 598⅓ and are 54 apart; a pitch is a tenth of the width
    // inside 6⅔ margins; the bottom row is 123, 한あA, space, ., return (1.25, 1.25, 4.5, 1, 2).
    static let margin = 20.0 / 3, top = 598.0 + 1 / 3, pitch = (402 - 2 * margin + 6) / 10

    static func center(_ key: String) -> CGPoint {
        let rows = ["qwertyuiop", "asdfghjkl", "zxcvbnm"]
        for (r, row) in rows.enumerated() {
            if key.count == 1, let i = row.firstIndex(of: Character(key.lowercased())) {
                let left = margin + [0, 0.5, 1.5][r] * pitch + Double(row.distance(from: row.startIndex, to: i)) * pitch
                return CGPoint(x: left + (pitch - 6) / 2, y: top + Double(r) * 54 + 127.0 / 6)
            }
        }
        let side = (1.3 * pitch - 6) / 2
        switch key {
        case "⇧": return CGPoint(x: margin + side, y: top + 108 + 127.0 / 6)
        case "⌫": return CGPoint(x: 402 - margin - side, y: top + 108 + 127.0 / 6)
        default:
            let bottom: [(String, Double)] = [("123", 1.25), ("한あA", 1.25), ("space", 4.5), (".", 1), ("return", 2)]
            var left = margin
            for (name, units) in bottom {
                if name == key { return CGPoint(x: left + (units * pitch - 6) / 2, y: top + 162 + 19.3) }
                left += units * pitch
            }
            fatalError("no key \(key)")
        }
    }

    func tap(_ key: String) {
        let p = Self.center(key)
        app.coordinate(withNormalizedOffset: .zero).withOffset(CGVector(dx: p.x, dy: p.y)).tap()
    }

    /// Letters as keys; " " is space, "⏎" return, "⌫" delete, "⇧" shift, "※" the 한あA key.
    func type(_ keys: String) {
        var numbers = false
        for c in keys {
            // Digits and symbols are on the letter keys in 123 mode, which stays until 123 again.
            if let key = Self.symbolKeys[c] {
                if !numbers { tap("123"); numbers = true }
                tap(String(key))
                continue
            }
            if numbers, c.isLetter { tap("123"); numbers = false }
            switch c {
            case " ": tap("space")
            case "⏎": tap("return")
            case "※": tap("한あA")
            case _ where c.isUppercase: tap("⇧"); tap(c.lowercased())
            default: tap(String(c))
            }
        }
        if numbers { tap("123") }
    }

    static let symbolKeys = Dictionary(uniqueKeysWithValues: zip("1234567890-/:;()₩&@,?!'\"~", "qwertyuiopasdfghjklxcvbnm"))

    /// The field's text (an empty field reports its placeholder).
    var text: String {
        let field = app.textFields["tryField"]
        let value = field.value as? String ?? ""
        return value == field.placeholderValue ? "" : value
    }

    /// Types `keys` and checks what they added to the field.
    func check(_ keys: String, _ expected: String, _ note: String, file: StaticString = #filePath, line: UInt = #line) {
        let before = text
        type(keys)
        sleep(1)
        let after = text
        let added = after.hasPrefix(before) ? String(after.dropFirst(before.count)) : "(changed) " + after
        print("TYPING \(note): \(keys) → [\(added)]")
        XCTAssertEqual(added, expected, note, file: file, line: line)
    }

    func testKoreanEnglishJapanese() {
        check("dkssudgktpdy ", "안녕하세요 ", "Korean")
        check("hello world ", "hello world ", "English")
        check("githubdp ", "github에 ", "English + Korean particle")
        check("nihongo⏎", "日本語", "Japanese, return takes the first candidate")
    }

    func testAutocorrect() {
        check("wlagodgksek ", "진행한다 ", "Korean typo 짐행한다")
        check("snffu ", "눌려 ", "Korean word missing from our lists")
        check("teh ", "the ", "English typo")
        check("hellp ", "hello ", "English: p for o, one slip")
        check("dkssudgktpdu ", "안녕하세요 ", "Korean: ㅕ for ㅛ, one slip")
    }

    func testEditingKeys() {
        check("hello⌫⌫ ", "hel ", "delete")
        check("⇧hi ", "Hi ", "one-shot shift")
        // go: English (a dictionary word); the switch key turns it into 해 or ご (learned order).
        let before = text
        type("go ※")
        sleep(1)
        print("TYPING switch key: go ※ → [\(text.dropFirst(before.count))]")
        XCTAssertTrue(["해 ", "ご "].contains(String(text.dropFirst(before.count))), "switch key")
    }

    func testMixedSentences() {
        check("dhsmf meetingdptj designdmf reviewgoTek ", "오늘 meeting에서 design을 review했다 ", "Korean + English")
        check("sksms Swiftfh appdmf aksemsek ", "나는 Swift로 app을 만든다 ", "Korean + capitalized English")
        check("toukyouniikimasu⏎", "東京に行きます", "Japanese sentence")
        check("kinou email wo okutta⏎", "昨日email を送った", "Japanese + English, spaces typed")
        check("kinouemailwookutta⏎", "昨日emailを送った", "Japanese + English, no spaces")
    }

    /// Sentences of all kinds (UITests/Corpus.swift): reports, chat, code, URLs, Japanese
    /// with English. Each goes after the last; a miss doesn't stop the run.
    func testCorpus() { typeAll(corpus) }

    /// App Store screenshots: a sentence, then a word left in the bar (its candidates showing).
    func testScreenshotKorean() { shot("1-ko", "dhsmf meetingdptj designdmf reviewgoTek ", "go") }
    func testScreenshotJapanese() { shot("2-ja", "kyouhaiitenkidesune⏎", "kaiginoshiryou") }
    func testScreenshotEnglish() { shot("3-en", "The keyboard now matches the system layout. ", "dhsmf meetingdms ", "keyboa") }

    func shot(_ name: String, _ keys: String...) {
        type(String(repeating: "⌫", count: text.count)) // the field keeps the last run's text
        keys.forEach(type)
        sleep(2)
        let attachment = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    func typeAll(_ corpus: [(String, String)]) {
        var misses = 0, chars = 0
        for (keys, expected) in corpus {
            let before = text
            type(keys)
            sleep(1)
            let added = String(text.dropFirst(before.count))
            chars += expected.count
            if added != expected { misses += 1; print("TYPING MISS: \(keys) → [\(added)] expected [\(expected)]") }
            else { print("TYPING ok: [\(added)]") }
        }
        print("TYPING corpus: \(corpus.count - misses)/\(corpus.count) sentences, \(chars) characters, field \(text.count)")
        XCTAssertEqual(misses, 0)
    }
}
