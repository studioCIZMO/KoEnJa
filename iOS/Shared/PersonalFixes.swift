import Foundation

/// The corrections this person makes, learned from their own retyping: a word taken back and
/// retyped with one or two keys different, then committed (rlfk, ⌫⌫, la, Space → rlla) is a
/// pair wrong→right. From the third time, the right form is the fix Space puts in and the bar
/// shows first, as a learned autocorrection (two could be a change of mind: cat → car). Keys (2-set for Korean), not text, so the fix
/// follows the language. In the App Group ("personalFixes": lines wrong TAB right TAB count).
/// ponytail: unlearning is only the app's list; add a veto (the wrong form committed as is, or
/// the fix taken back right after) when field logs show pairs that should not have stuck.
enum PersonalFixes {
    static let key = "personalFixes", minCount = 3, maxPairs = 500

    static func load() -> [(wrong: String, right: String, count: Int)] {
        (LanguagePreferences.defaults?.string(forKey: key) ?? "").split(separator: "\n").compactMap { line in
            let p = line.split(separator: "\t")
            return p.count == 3 ? Int(p[2]).map { (String(p[0]), String(p[1]), $0) } : nil
        }
    }

    static func save(_ list: [(wrong: String, right: String, count: Int)]) {
        LanguagePreferences.defaults?.set(list.map { "\($0.wrong)\t\($0.right)\t\($0.count)" }.joined(separator: "\n"), forKey: key)
    }

    /// One more time `wrong` was retyped as `right`. Returns the pair's count.
    @discardableResult
    static func record(wrong: String, right: String) -> Int {
        var list = load()
        if let i = list.firstIndex(where: { $0.wrong == wrong }) {
            list[i] = (wrong, right, right == list[i].right ? list[i].count + 1 : 1)
        } else {
            list.append((wrong, right, 1))
            if list.count > maxPairs { list.removeFirst(list.count - maxPairs) }
        }
        save(list)
        return list.first { $0.wrong == wrong }?.count ?? 0
    }

    /// The keys to put in for `keys`, once the pair has been seen `minCount` times.
    static func fix(for keys: String) -> String? {
        load().first { $0.wrong == keys && $0.count >= minCount }?.right
    }

    static func remove(wrong: String) { save(load().filter { $0.wrong != wrong }) }

    /// Whether going from the longest form typed to the committed one is a correction: both at
    /// least 3 keys, neither a prefix of the other (typing on, or an overshoot taken back), and
    /// at most two edits apart (a slip or two, not another word).
    static func isCorrection(longest: String, final: String) -> Bool {
        guard longest.count >= 3, final.count >= 3, longest != final,
              !final.hasPrefix(longest), !longest.hasPrefix(final) else { return false }
        return editDistance(longest, final) <= 2
    }

    static func editDistance(_ a: String, _ b: String) -> Int {
        let a = Array(a), b = Array(b)
        var prev = Array(0...b.count), cur = [Int](repeating: 0, count: b.count + 1)
        for i in 1...max(a.count, 1) where i <= a.count {
            cur[0] = i
            for j in 1...max(b.count, 1) where j <= b.count {
                cur[j] = min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1))
            }
            swap(&prev, &cur)
        }
        return prev[b.count]
    }
}
