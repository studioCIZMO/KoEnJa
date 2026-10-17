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
        for c in keys {
            switch c {
            case " ": tap("space")
            case "⏎": tap("return")
            case "※": tap("한あA")
            default: tap(String(c))
            }
        }
    }

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
}
