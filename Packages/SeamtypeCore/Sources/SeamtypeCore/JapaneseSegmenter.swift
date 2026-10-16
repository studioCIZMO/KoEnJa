import Foundation

/// Finds English words typed inside a Japanese phrase, the way Japanese is written:
/// `kyouhagoogledekensaku` → きょうは + google + でけんさく.
///
/// Only used when the whole phrase doesn't compose to clean kana (google → ごおgぇ leaves
/// a stray "g"). Splits the romaji into clean Japanese spans and known English words
/// (3+ letters), maximizing the Japanese model's score plus a fixed score per English
/// word, with a cost per switch so ordinary Japanese is never carved up.
///
/// The input method asks on every keystroke, so the search only cuts where an English
/// word could start or end (no English word anywhere → nothing to do), and the last
/// answer is remembered.
public struct JapaneseSegmenter: Sendable {
    public enum Segment: Equatable, Sendable {
        case japanese(keys: String, kana: String)
        case english(String)
    }

    public let english: EnglishModel
    public let japanese: JapaneseModel
    /// Log-probability credited per letter of a known English word.
    public var englishLetterLogP = -1.6
    /// Cost of each switch into English.
    public var switchCost = 4.0
    /// Longest English word looked for inside a phrase.
    static let maxEnglishLength = 20

    static let particles: Set<String> = ["で", "に", "を", "は", "が", "の", "と", "も", "へ", "や", "か", "ね", "よ"]
    /// Case particles that may follow an English noun on their own: githubで, slackから.
    /// Not ね/よ/か/や/へ/って: names end in them far more often (jayne, rebecka, claudette).
    static let trailingParticles: Set<String> = ["で", "に", "を", "は", "が", "の", "と", "も", "から", "まで", "より"]

    private let cache = Cache()
    private let particleSplitter: ParticleSplitter

    public init(english: EnglishModel, japanese: JapaneseModel) {
        self.english = english
        self.japanese = japanese
        particleSplitter = ParticleSplitter(english: english, japanese: japanese)
    }

    /// The segments, or nil when the keys compose cleanly (nothing to split) or no
    /// split with both Japanese and English beats leaving stray letters.
    public func segment(_ keys: String) -> [Segment]? {
        scoredSegments(keys)?.segments
    }

    /// Segments with their total log-probability (Japanese spans + English credit − switches).
    public func scoredSegments(_ keys: String) -> (segments: [Segment], score: Double)? {
        if let hit = cache.get(keys) { return hit }
        let result = search(keys)
        cache.set(keys, result)
        return result
    }

    /// `githubde` → github + で, when that reading beats the whole token as kana, Korean or
    /// English (`ParticleSplitter`).
    public func englishWithParticle(_ keys: String) -> (english: String, particle: String)? {
        guard case let .japanese(word, particle)? = particleSplitter.split(keys, japaneseOn: true) else { return nil }
        return (word, particle)
    }

    // MARK: - Search

    private struct State {
        var score: Double
        var back: (position: Int, kana: Int, english: Bool)?
        var segment: Segment?
    }

    private func search(_ keys: String) -> (segments: [Segment], score: Double)? {
        guard RomajiComposer.compose(keys).strayLetters > 0 else { return nil }
        let chars = Array(keys)
        let n = chars.count
        // Where known English words (3+ letters) are: end positions per start.
        var englishEnds = [[Int]](repeating: [], count: n)
        var cuts: Set<Int> = [0, n]
        for i in 0..<n where chars[i].isLetter {
            var j = i
            while j < n, j - i < Self.maxEnglishLength, chars[j].isLetter {
                j += 1
                if j - i >= 3, english.isKnownWord(String(chars[i..<j])) {
                    englishEnds[i].append(j)
                    cuts.insert(i)
                    cuts.insert(j)
                }
            }
        }
        guard cuts.count > 2 || englishEnds[0].contains(n) else { return nil }
        let positions = cuts.sorted()
        let index = Dictionary(uniqueKeysWithValues: positions.enumerated().map { ($1, $0) })
        // One Japanese span runs from the start or an English word's end to the end or an
        // English word's start (two spans side by side would only score worse).
        let englishStarts = Set(englishEnds.indices.filter { !englishEnds[$0].isEmpty })
        let englishEndSet = Set(englishEnds.flatMap { $0 })
        let japaneseFrom = positions.map { $0 == 0 || englishEndSet.contains($0) }
        let japaneseTo = positions.map { $0 == n || englishStarts.contains($0) }

        // best[position][kana so far, capped at 3][used English]: the best path to there.
        var best = [[[State?]]](repeating: [[State?]](repeating: [nil, nil], count: 4), count: positions.count)
        best[0][0][0] = State(score: 0)
        for p in positions.indices {
            for kana in 0...3 {
                for used in 0...1 {
                    guard let state = best[p][kana][used] else { continue }
                    let from = positions[p]
                    func offer(_ q: Int, _ kanaAfter: Int, _ usedAfter: Int, _ score: Double, _ segment: Segment) {
                        let total = state.score + score
                        if best[q][kanaAfter][usedAfter].map({ total > $0.score }) ?? true {
                            best[q][kanaAfter][usedAfter] = State(score: total, back: (p, kana, used == 1), segment: segment)
                        }
                    }
                    if from < n {
                        for end in englishEnds[from] {
                            let word = String(chars[from..<end])
                            offer(index[end]!, kana, 1, englishLetterLogP * Double(word.count) - switchCost, .english(word))
                        }
                    }
                    for q in (p + 1)..<positions.count where japaneseFrom[p] {
                        guard japaneseTo[q] else { continue }
                        let piece = String(chars[from..<positions[q]])
                        // A letter no vowel can rescue (the g of "gl") fails every longer span too.
                        let reading = RomajiComposer.compose(piece, partial: true)
                        if reading.strayLetters > 0 { break }
                        let composed = reading.pending.isEmpty ? reading : RomajiComposer.compose(piece)
                        // A one-kana span must be a particle (googleで), not a leftover (would + ん).
                        guard composed.isClean, !composed.text.isEmpty,
                              composed.text.count >= 2 || Self.particles.contains(composed.text) else { continue }
                        // Each span scores as Japanese on its own (start/end markers included).
                        offer(q, min(3, kana + composed.text.count), used,
                              japanese.logProbability(kana: composed.text, partial: false),
                              .japanese(keys: piece, kana: composed.text))
                    }
                }
            }
        }

        // Needs real Japanese around the English: at least 3 kana (not mum + ばい), or exactly
        // an English word with a particle after it (github + で).
        var answer: (segments: [Segment], score: Double)?
        let last = positions.count - 1
        for kana in 0...3 {
            guard let state = best[last][kana][1] else { continue }
            let segments = path(to: (last, kana, true), in: best)
            guard kana >= 3 || endsInParticle(segments, keys: keys) else { continue }
            if answer.map({ state.score > $0.score }) ?? true { answer = (merged(segments), state.score) }
        }
        return answer
    }

    private func path(to end: (position: Int, kana: Int, english: Bool), in best: [[[State?]]]) -> [Segment] {
        var segments: [Segment] = []
        var at: (position: Int, kana: Int, english: Bool)? = end
        while let node = at, let state = best[node.position][node.kana][node.english ? 1 : 0], let segment = state.segment {
            segments.append(segment)
            at = state.back
        }
        return segments.reversed()
    }

    /// Exactly an English word plus one particle, where the whole isn't itself English.
    private func endsInParticle(_ segments: [Segment], keys: String) -> Bool {
        guard segments.count == 2, case .english = segments[0],
              case .japanese(_, let kana) = segments[1] else { return false }
        return Self.trailingParticles.contains(kana) && !english.isKnownWord(keys)
    }

    /// Adjacent Japanese spans join into one (きょうは + でけんさく stay separate only around English).
    private func merged(_ segments: [Segment]) -> [Segment] {
        var out: [Segment] = []
        for segment in segments {
            if case .japanese(let k2, let kana2) = segment, case .japanese(let k1, let kana1)? = out.last {
                out[out.count - 1] = .japanese(keys: k1 + k2, kana: kana1 + kana2)
            } else {
                out.append(segment)
            }
        }
        return out
    }

    /// The last few answers: the preview, the decision and the commit ask about the same keys.
    private final class Cache: @unchecked Sendable {
        private let lock = NSLock()
        private var entries: [(keys: String, result: (segments: [Segment], score: Double)?)] = []

        func get(_ keys: String) -> (segments: [Segment], score: Double)?? {
            lock.withLock { entries.first { $0.keys == keys }.map { $0.result } }
        }

        func set(_ keys: String, _ result: (segments: [Segment], score: Double)?) {
            lock.withLock {
                entries.insert((keys, result), at: 0)
                if entries.count > 8 { entries.removeLast() }
            }
        }
    }

    // MARK: - Readings

    /// The final reading of romaji keys: kana, with English words kept when a segmenter finds them.
    public static func reading(of keys: String, segmenter: JapaneseSegmenter?) -> String {
        if let segments = segmenter?.segment(keys) { return text(segments) }
        return RomajiComposer.compose(keys).text
    }

    /// Kana with the English words left in place: きょうはgoogleでけんさく.
    public static func text(_ segments: [Segment]) -> String {
        segments.map { segment in
            switch segment {
            case .japanese(_, let kana): return kana
            case .english(let word): return word
            }
        }.joined()
    }
}
