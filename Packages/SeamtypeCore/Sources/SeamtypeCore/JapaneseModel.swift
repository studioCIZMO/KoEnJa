import Foundation

/// Kana trigram model for judging how Japanese a romaji string reads. Counts come
/// from Japanese sentences (Tatoeba, CC-BY 2.0 FR) turned into the romaji people
/// type and back into kana with `RomajiComposer` (tools/build_kana_model.py).
///
/// Clean romaji isn't enough: "hello" composes to へっぉ, which this model scores
/// as very un-Japanese (っぉ essentially never occurs).
public final class JapaneseModel: @unchecked Sendable {
    public struct Score: Sendable {
        /// Average log-probability per keystroke, comparable with the English and Korean models.
        public let averageLogProbability: Double
        public let composition: RomajiComposer.Result
    }

    private var trigrams: [String: Float] = [:]
    private var bigrams: [String: Float] = [:]
    private var unigrams: [String: Float] = [:]
    private var bigramContexts: [String: Float] = [:]
    private var trigramContexts: [String: Float] = [:]
    private var total: Float = 0
    private let vocabulary: Float

    /// Non-letters that belong inside a Japanese phrase: ん (n'), ー, and the full-width symbols
    /// (、。？！「」・…).
    public static let inPhraseSymbols: Set<Character> = Set(RomajiComposer.symbols.keys).union(["'"])

    static let strayLetterLogP = -13.0
    static let pendingLogP = -3.5

    public init?(contentsOf url: URL) {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return nil }
        var symbols = Set<Character>()
        for line in text.split(whereSeparator: \.isNewline) {
            let parts = line.split(separator: "\t")
            guard parts.count == 2, let count = Float(parts[1]) else { continue }
            let chars = Array(parts[0])
            guard chars.count == 3 else { continue }
            let (a, b, c) = (String(chars[0]), String(chars[1]), String(chars[2]))
            trigrams[a + b + c, default: 0] += count
            trigramContexts[a + b, default: 0] += count
            bigrams[b + c, default: 0] += count
            bigramContexts[b, default: 0] += count
            unigrams[c, default: 0] += count
            total += count
            symbols.formUnion(chars)
        }
        guard total > 0 else { return nil }
        vocabulary = Float(symbols.count)
    }

    public static let shared: JapaneseModel? = Resources.kanaModel.flatMap(JapaneseModel.init(contentsOf:))
    /// The same kind of model over written Japanese (kanji + kana), for reranking conversions.
    public static let text: JapaneseModel? = Resources.textModel.flatMap(JapaneseModel.init(contentsOf:))

    /// Interpolated trigram probability of `c` after `a b`.
    private func probability(_ a: String, _ b: String, _ c: String) -> Double {
        let p1 = ((unigrams[c] ?? 0) + 1) / (total + vocabulary)
        let p2 = (bigramContexts[b]).map { ((bigrams[b + c] ?? 0)) / $0 } ?? 0
        let p3 = (trigramContexts[a + b]).map { ((trigrams[a + b + c] ?? 0)) / $0 } ?? 0
        return Double(0.6 * p3 + 0.3 * p2 + 0.1 * p1)
    }

    /// Sum of log-probabilities of the kana (and the end of word unless `partial`).
    public func logProbability(kana: String, partial: Bool) -> Double {
        var seq = ["^", "^"] + kana.map { String($0) }
        if !partial { seq.append("$") }
        var sum = 0.0
        for i in 2..<seq.count {
            sum += log(probability(seq[i - 2], seq[i - 1], seq[i]))
        }
        return sum
    }

    public func score(keys: String, partial: Bool = false) -> Score? {
        guard !keys.isEmpty, keys.allSatisfy({ $0.isASCII && ($0.isLetter || Self.inPhraseSymbols.contains($0)) }) else { return nil }
        let composition = RomajiComposer.compose(keys, partial: partial)
        let kana = String(composition.text.dropLast(composition.pending.count))
            .filter { !$0.isASCII && !RomajiComposer.punctuation.contains($0) }
        var total = logProbability(kana: kana, partial: partial)
        total += Double(composition.strayLetters) * Self.strayLetterLogP
        if !composition.pending.isEmpty { total += Self.pendingLogP }
        return Score(averageLogProbability: total / Double(keys.count), composition: composition)
    }
}
