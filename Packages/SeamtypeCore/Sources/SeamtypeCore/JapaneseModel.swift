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

    // REA: counts in sorted arrays keyed by packed symbol ids (21 bits each), as the Kotlin port
    // does: String-keyed Dictionaries cost the iOS keyboard some 13 MB for the text model alone.
    private struct Counts {
        let keys: [UInt64], values: [Float]
        init(keys: [UInt64], values: [Float]) { (self.keys, self.values) = (keys, values) }
        init(_ map: [UInt64: Float]) {
            let sorted = map.sorted { $0.key < $1.key }
            keys = sorted.map(\.key)
            values = sorted.map(\.value)
        }
        subscript(key: UInt64?) -> Float? {
            guard let key else { return nil }
            var low = 0, high = keys.count
            while low < high {
                let mid = (low + high) / 2
                if keys[mid] < key { low = mid + 1 } else { high = mid }
            }
            return low < keys.count && keys[low] == key ? values[low] : nil
        }
    }
    private let ids: [Character: UInt64]
    private let trigrams: Counts, bigrams: Counts, unigrams: Counts, bigramContexts: Counts, trigramContexts: Counts
    private let total: Float
    private let vocabulary: Float

    /// Non-letters that belong inside a Japanese phrase: ん (n'), ー, and the full-width symbols
    /// (、。？！「」・…).
    public static let inPhraseSymbols: Set<Character> = Set(RomajiComposer.symbols.keys).union(["'"])

    static let strayLetterLogP = -13.0
    static let pendingLogP = -3.5

    public init?(contentsOf url: URL) {
        // REA: read mapped, line by line; the trigrams (one line each) go straight into a sorted
        // array. The counts are whole numbers summing below 2^24, so Float adds them exactly in any order.
        guard let data = try? Data(contentsOf: url, options: .alwaysMapped) else { return nil }
        var ids: [Character: UInt64] = [:]
        func id(_ c: Character) -> UInt64 {
            if let i = ids[c] { return i }
            let i = UInt64(ids.count)
            ids[c] = i
            return i
        }
        var trigrams: [(key: UInt64, count: Float)] = []
        var bigrams: [UInt64: Float] = [:], unigrams: [UInt64: Float] = [:]
        var bigramContexts: [UInt64: Float] = [:], trigramContexts: [UInt64: Float] = [:]
        var total: Float = 0
        trigrams.reserveCapacity(Resources.lineCount(data))
        Resources.forEachPair(in: data) { key, count in
            guard let count = Float(count) else { return }
            let chars = Array(key)
            guard chars.count == 3 else { return }
            let (a, b, c) = (id(chars[0]), id(chars[1]), id(chars[2]))
            trigrams.append((a << 42 | b << 21 | c, count))
            trigramContexts[a << 21 | b, default: 0] += count
            bigrams[b << 21 | c, default: 0] += count
            bigramContexts[b, default: 0] += count
            unigrams[c, default: 0] += count
            total += count
        }
        guard total > 0 else { return nil }
        trigrams.sort { $0.key < $1.key }
        var merged: [(key: UInt64, count: Float)] = [] // the same trigram twice adds up, as upstream's Dictionary did
        merged.reserveCapacity(trigrams.count)
        for t in trigrams {
            if merged.last?.key == t.key { merged[merged.count - 1].count += t.count } else { merged.append(t) }
        }
        vocabulary = Float(ids.count)
        self.ids = ids
        self.total = total
        self.trigrams = Counts(keys: merged.map(\.key), values: merged.map(\.count))
        self.bigrams = Counts(bigrams)
        self.unigrams = Counts(unigrams)
        self.bigramContexts = Counts(bigramContexts)
        self.trigramContexts = Counts(trigramContexts)
    }

    public static let shared: JapaneseModel? = Resources.kanaModel.flatMap(JapaneseModel.init(contentsOf:))
    /// The same kind of model over written Japanese (kanji + kana), for reranking conversions.
    public static let text: JapaneseModel? = Resources.textModel.flatMap(JapaneseModel.init(contentsOf:))

    /// Interpolated trigram probability of `c` after `a b`.
    private func probability(_ a: Character, _ b: Character, _ c: Character) -> Double {
        let (a, b, c) = (ids[a], ids[b], ids[c])
        let ab = a.flatMap { a in b.map { a << 21 | $0 } }
        let bc = b.flatMap { b in c.map { b << 21 | $0 } }
        let abc = ab.flatMap { ab in c.map { ab << 21 | $0 } }
        let p1 = ((unigrams[c] ?? 0) + 1) / (total + vocabulary)
        let p2 = (bigramContexts[b]).map { ((bigrams[bc] ?? 0)) / $0 } ?? 0
        let p3 = (trigramContexts[ab]).map { ((trigrams[abc] ?? 0)) / $0 } ?? 0
        return Double(0.6 * p3 + 0.3 * p2 + 0.1 * p1)
    }

    /// Sum of log-probabilities of the kana (and the end of word unless `partial`).
    public func logProbability(kana: String, partial: Bool) -> Double {
        var seq: [Character] = ["^", "^"] + Array(kana)
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
