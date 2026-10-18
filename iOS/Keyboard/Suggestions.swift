import SeamtypeCore
import UIKit

/// Korean and English suggestions for the bar, like the system keyboard's: corrections for a
/// typo, else completions. A typo with a fix found by one slip (a neighbor key, two keys swapped)
/// is corrected on Space; that fix comes first.
/// Korean: iOS's spell checker has no Korean (it only completes), so a typo is a word missing
/// from SeamtypeCore's word lists, and fixes come from re-typing its keys with one slip.
/// English: UITextChecker's guesses and completions.
/// ponytail: one slip per word, no learning of words the user keeps; add a user dictionary
/// when names get "corrected".
@MainActor
struct Suggestions: Equatable {
    var words: [String] = []
    var autocorrect: String?

    private static let checker = UITextChecker()

    /// Keys next to each key on QWERTY (same row and the rows above and below).
    static let neighbors: [Character: String] = {
        let rows = ["qwertyuiop", "asdfghjkl", "zxcvbnm"].map(Array.init)
        let offset: [Double] = [0, 0.5, 1.5] // each row's left edge, in keys
        var map: [Character: String] = [:]
        for (r, row) in rows.enumerated() {
            for (i, key) in row.enumerated() {
                let x = Double(i) + offset[r]
                map[key] = String(rows.indices.flatMap { r2 in
                    rows[r2].enumerated().filter { j, other in
                        other != key && abs(r2 - r) <= 1 && abs(Double(j) + offset[r2] - x) <= (r2 == r ? 1 : 0.5)
                    }.map(\.element)
                })
            }
        }
        return map
    }()

    /// The keys with one slip: each key swapped for a neighbor, or two keys in the wrong order.
    static func oneSlip(_ keys: String) -> [String] {
        // Avoid quadratic work for long tokens (URLs, identifiers, or a missing space).
        guard keys.count <= 64 else { return [] }
        let chars = Array(keys)
        var out: [String] = []
        for i in chars.indices {
            let lower = Character(chars[i].lowercased())
            for n in neighbors[lower] ?? "" {
                var c = chars
                c[i] = chars[i].isUppercase ? Character(n.uppercased()) : n
                out.append(String(c))
            }
            if i + 1 < chars.count, chars[i] != chars[i + 1] {
                var c = chars
                c.swapAt(i, i + 1)
                out.append(String(c))
            }
        }
        return out
    }

    static func korean(word: String, keys: String, model: KoreanModel) -> Suggestions {
        // iOS's lexicon knows words our lists miss (눌려 was "fixed" to 물려, 버그를 to 버스를):
        // a word it can complete, or completes to itself, is a word, not a typo.
        let range = NSRange(location: 0, length: (word as NSString).length)
        let known = checker.completions(forPartialWordRange: range, in: word, language: "ko_KR") ?? []
        let completions = Array(known.filter { $0 != word }.prefix(2))
        guard word.count >= 2, !model.isVocabularyWord(word), known.isEmpty,
              HangulComposer.compose(keys: keys)?.isBroken == false else { return Suggestions(words: completions) }
        // Fixes ranked: a word seen in real text first, then by how common its syllables are.
        let fixes = Set(oneSlip(keys).compactMap { HangulComposer.compose(keys: $0)?.text })
            .filter { $0.count == word.count && model.isVocabularyWord($0) }
            .sorted { a, b in
                let (ka, kb) = (model.isKnownWord(a), model.isKnownWord(b))
                if ka != kb { return ka }
                let (sa, sb) = (syllableScore(a, model), syllableScore(b, model))
                return sa != sb ? sa > sb : a < b
            }
        return Suggestions(words: Array(fixes.prefix(2)), autocorrect: fixes.first)
    }

    private static func syllableScore(_ word: String, _ model: KoreanModel) -> Double {
        word.reduce(0) { $0 + model.syllableLogProbability($1) }
    }

    static func english(word: String, model: EnglishModel) -> Suggestions {
        let range = NSRange(location: 0, length: (word as NSString).length)
        let typo = word.count >= 2 && !model.isKnownWord(word.lowercased())
            && checker.rangeOfMisspelledWord(in: word, range: range, startingAt: 0, wrap: false, language: "en_US").location != NSNotFound
        guard typo else { return Suggestions(words: complete(word, language: "en_US")) }
        let guesses = (checker.guesses(forWordRange: range, in: word, language: "en_US") ?? [])
            .filter { !$0.contains(" ") && !$0.contains("-") }
        // Only a guess one slip away is trusted for autocorrect; others are just offered.
        let slips = Set(oneSlip(word.lowercased()))
        let fix = guesses.first { slips.contains($0.lowercased()) }
        let words = fix.map { fix in [fix] + guesses.filter { $0 != fix } } ?? guesses
        return Suggestions(words: Array(words.prefix(2)), autocorrect: fix)
    }

    private static func complete(_ word: String, language: String) -> [String] {
        let range = NSRange(location: 0, length: (word as NSString).length)
        return (checker.completions(forPartialWordRange: range, in: word, language: language) ?? [])
            .filter { $0 != word }.prefix(2).map { $0 }
    }
}
