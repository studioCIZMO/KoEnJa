import XCTest

/// Where the system keyboard's touch areas end: taps between two keys' centers, halving the gap
/// until the boundary is within 1 pt, reading the field after each. iOS resizes its touch areas
/// by what is likely next, so each boundary is measured after nothing (a new word) and after a
/// prefix. Run on a simulator with an English (US) keyboard: prints HIT lines.
/// `xcodebuild test -only-testing:REAUITests/HitAreas`; skipped unless HIT_AREAS=1 (TEST_RUNNER_HIT_AREAS).
final class HitAreas: XCTestCase {
    func testSystemKeyboard() throws {
        guard ProcessInfo.processInfo.environment["HIT_AREAS"] == "1" else { throw XCTSkip("set TEST_RUNNER_HIT_AREAS=1") }
        let app = XCUIApplication()
        app.launchArguments = ["-focusTry"]
        app.launch()
        let field = app.textFields["tryField"]
        XCTAssertTrue(field.waitForExistence(timeout: 5))
        sleep(2)
        let keys = app.keyboards.keys
        func key(_ k: String) -> XCUIElement { keys[k].exists ? keys[k] : keys[k.uppercased()] }
        let next = app.buttons.matching(NSPredicate(format: "label IN %@", ["다음 키보드", "次のキーボード", "Next keyboard"])).firstMatch
        for _ in 0..<6 where !key("q").exists && next.exists {
            next.tap()
            sleep(2)
        }
        print("HIT keys \(keys.allElementsBoundByIndex.prefix(12).map(\.label))")
        XCTAssertTrue(key("q").exists, "no English keyboard")
        func frame(_ k: String) -> CGRect { key(k).frame }
        let delete = keys.matching(NSPredicate(format: "label IN %@", ["delete", "삭제", "削除"])).firstMatch
        var text: String { let v = field.value as? String ?? ""; return v == field.placeholderValue ? "" : v }
        func clear() { for _ in 0..<text.count { delete.tap() } }
        func tap(_ p: CGPoint) { app.coordinate(withNormalizedOffset: .zero).withOffset(CGVector(dx: p.x, dy: p.y)).tap() }
        func center(_ k: String) -> CGPoint { CGPoint(x: frame(k).midX, y: frame(k).midY) }
        /// What a tap at `p` types after `prefix` (typed at key centers).
        func typed(_ p: CGPoint, after prefix: String) -> String {
            clear()
            for c in prefix { tap(center(String(c))) }
            let before = text.count
            tap(p)
            return String(text.dropFirst(before)).lowercased()
        }
        func boundary(_ a: String, _ b: String, after prefix: String, vertical: Bool) -> Double {
            var lo = vertical ? frame(a).midY : frame(a).midX, hi = vertical ? frame(b).midY : frame(b).midX
            let other = vertical ? frame(a).midX : frame(a).midY
            while hi - lo > 1 {
                let m = (lo + hi) / 2
                if typed(vertical ? CGPoint(x: other, y: m) : CGPoint(x: m, y: other), after: prefix) == a { lo = m } else { hi = m }
            }
            return (lo + hi) / 2
        }
        for k in ["q", "a", "z", "n", "m"] { print("HIT frame \(k) \(frame(k))") }
        for prefix in ["", "th", "wh", "qu"] {
            for (a, b) in [("q", "w"), ("e", "r"), ("t", "y"), ("o", "p"), ("a", "s"), ("g", "h"), ("k", "l"), ("c", "v"), ("b", "n"), ("n", "m")] {
                let x = boundary(a, b, after: prefix, vertical: false)
                let gapMid = (frame(a).maxX + frame(b).minX) / 2
                print("HIT after '\(prefix)' \(a)|\(b) x=\(String(format: "%.1f", x)) gap middle \(String(format: "%.1f", gapMid)) shift \(String(format: "%+.1f", x - gapMid))")
            }
            for (a, b) in [("t", "g"), ("g", "b"), ("i", "k"), ("k", "m")] {
                let y = boundary(a, b, after: prefix, vertical: true)
                let gapMid = (frame(a).maxY + frame(b).minY) / 2
                print("HIT after '\(prefix)' \(a)/\(b) y=\(String(format: "%.1f", y)) gap middle \(String(format: "%.1f", gapMid)) shift \(String(format: "%+.1f", y - gapMid))")
            }
        }
        clear()
    }

    /// Does the system keyboard count the space beside its edge keys (the half-key margins next to
    /// a and l, the gaps beside shift and delete, the row gaps) as those keys? The Korean 2-set
    /// keyboard has the same geometry (ㅁ…ㅣ on the home row, ㅋ…ㅡ below), so the English one answers
    /// for ㅏ ㅣ ㅡ. Prints HIT EDGE lines; skipped unless HIT_AREAS=1.
    func testSystemKeyboardEdges() throws {
        guard ProcessInfo.processInfo.environment["HIT_AREAS"] == "1" else { throw XCTSkip("set TEST_RUNNER_HIT_AREAS=1") }
        let app = XCUIApplication()
        app.launchArguments = ["-focusTry"]
        app.launch()
        let field = app.textFields["tryField"]
        XCTAssertTrue(field.waitForExistence(timeout: 5))
        if !app.keyboards.firstMatch.waitForExistence(timeout: 3) { field.tap(); sleep(2) } // -focusTry didn't take
        let keys = app.keyboards.keys
        func key(_ k: String) -> XCUIElement { keys[k].exists ? keys[k] : keys[k.uppercased()] }
        let next = app.buttons.matching(NSPredicate(format: "label IN %@", ["다음 키보드", "次のキーボード", "Next keyboard"])).firstMatch
        for _ in 0..<6 where !key("q").exists && next.exists {
            next.tap()
            sleep(2)
        }
        print("HIT keys \(keys.allElementsBoundByIndex.prefix(12).map(\.label))")
        XCTAssertTrue(key("q").exists, "no English keyboard")
        let delete = keys.matching(NSPredicate(format: "label IN %@", ["delete", "삭제", "削除"])).firstMatch
        var text: String { let v = field.value as? String ?? ""; return v == field.placeholderValue ? "" : v }
        func clear() { for _ in 0..<text.count { delete.tap() } }
        func typed(_ p: CGPoint) -> String {
            clear()
            app.coordinate(withNormalizedOffset: .zero).withOffset(CGVector(dx: p.x, dy: p.y)).tap()
            return text.lowercased()
        }
        let width = app.frame.width
        let (a, l, m, q, z) = (key("a").frame, key("l").frame, key("m").frame, key("q").frame, key("z").frame)
        let shift = app.keyboards.buttons.matching(NSPredicate(format: "label CONTAINS[c] 'shift'")).firstMatch.frame
        print("HIT EDGE frames a=\(a) l=\(l) m=\(m) q=\(q) shift=\(shift) delete=\(delete.frame) width=\(width)")
        let probes: [(String, CGPoint)] = [
            ("left margin, home row (2pt from the edge)", CGPoint(x: 2, y: a.midY)),
            ("left margin, home row (a.minX - 8)", CGPoint(x: a.minX - 8, y: a.midY)),
            ("right margin, home row (width - 2)", CGPoint(x: width - 2, y: l.midY)),
            ("right margin, home row (l.maxX + 8)", CGPoint(x: l.maxX + 8, y: l.midY)),
            ("right margin, top row (width - 2)", CGPoint(x: width - 2, y: q.midY)),
            ("gap m|delete middle", CGPoint(x: (m.maxX + delete.frame.minX) / 2, y: m.midY)),
            ("gap z|shift middle", CGPoint(x: (shift.maxX + z.minX) / 2, y: z.midY)),
            ("row gap a/z middle", CGPoint(x: a.midX, y: (a.maxY + z.minY) / 2)),
            ("row gap l/m middle", CGPoint(x: l.midX, y: (l.maxY + m.minY) / 2)),
            ("above top row (q.minY - 3)", CGPoint(x: q.midX, y: q.minY - 3)),
        ]
        for (name, p) in probes {
            print("HIT EDGE \(name) at \(p) → '\(typed(p))'")
        }
        clear()
    }
}
