import Foundation

/// Syllable-unigram model for Korean: enough to tell "안녕하세요" from the accidental
/// Hangul that English produces on a 2-set layout (e.g. "the" → "솓").
public struct KoreanModel: Sendable {
    public struct Score: Sendable {
        /// Average log-probability per keystroke.
        public let averageLogProbability: Double
        public let composition: HangulComposer.Result
    }

    private let tiers: [Character: Double]
    /// Words seen in Korean sentences (Data/korean_words.txt, Tatoeba): 나는, 무슨, 내가…
    private let words: Set<String>

    static let tier1LogP = -5.0
    static let tier2LogP = -6.5
    static let tier3LogP = -8.0
    static let unknownSyllableLogP = -11.0
    static let strayJamoLogP = -13.0
    /// Typing the first consonant of the next syllable is normal mid-word.
    static let pendingConsonantLogP = -3.5
    /// The same for a consonant that joined the syllable before as its final (갷 of 소개ㅎ),
    /// a little less sure: alr is 미국 on its way and also already (tuned on preview traces of
    /// the top 10,000 English and Korean words).
    static let pendingFinalPenalty = 1.0
    /// ㅋㅋ, ㅎㅎ, ㅠㅠ and friends.
    static let chatJamoLogP = -4.0
    static let chatJamo: Set<Character> = Set("ㅋㅎㅠㅜㄷㅇㄱㅅㅂㄴ")

    /// Where syllable probabilities come from.
    public enum Source: String, Sendable {
        /// The hand-curated frequency tiers in KoreanSyllables.swift.
        case tiers
        /// Counts from Korean sentences (Data/korean_syllables.txt, Tatoeba, CC-BY 2.0 FR).
        case corpus
        /// Whichever of the two rates a syllable higher.
        case max
    }

    public init(source: Source = .max) {
        var map: [Character: Double] = [:]
        if source != .corpus {
            for (syllables, logP) in [(KoreanSyllables.tier3, Self.tier3LogP),
                                      (KoreanSyllables.tier2, Self.tier2LogP),
                                      (KoreanSyllables.tier1, Self.tier1LogP)] {
                for s in syllables where ("가"..."힣").contains(s) {
                    map[s] = logP // later (more frequent) tiers overwrite earlier ones
                }
            }
        }
        if source != .tiers, let counts = Self.corpusCounts() {
            let total = Double(counts.values.reduce(0, +))
            let smoothing = 0.5, possible = 11172.0
            for (s, n) in counts {
                let logP = log((Double(n) + smoothing) / (total + smoothing * possible))
                map[s] = Swift.max(map[s] ?? -.infinity, logP)
            }
        }
        tiers = map
        words = Self.corpusWords
    }

    /// A word that occurs in real Korean text (the composed Hangul, not the keys).
    public func isKnownWord(_ hangul: String) -> Bool { words.contains(hangul) }

    /// A word in the wider vocabulary (loanwords like 월드, 서버; 혜택), for English + Korean splits
    /// and the heuristic's Korean word floor.
    public func isVocabularyWord(_ hangul: String) -> Bool {
        words.contains(hangul) || Self.lists.vocabulary.contains(Self.hash(hangul)) // REA: hashes
    }

    /// A word the subtitles have fewer than `commonCount` times (촉매, twice): for the heuristic's
    /// Korean word floor only. The tail holds dialect and typos too (어뗘, 쓔벌, 꺼이), so it
    /// stays out of the splits and of `knowsSyllable`, which `EU정책` and `RJ이` rely on.
    public func isRareVocabularyWord(_ hangul: String) -> Bool { Self.lists.rare.contains(Self.hash(hangul)) } // REA: hashes

    /// A noun the lists only have with a case particle (혜택 from 혜택을, 혜택으로): they
    /// count words with their particles, so a bare noun can be missing from both.
    public func isKnownStem(_ hangul: String) -> Bool { Self.lists.stems.contains(hangul) } // REA: lists

    /// Where `korean_vocabulary.txt` stops being `vocabulary`: the old 50k list's last count.
    static let commonCount = 10

    /// Case particles, by what the noun must end in: a final consonant (을), none (를), or either.
    private static let particles: [(String, Bool?)] = [
        ("을", true), ("은", true), ("이", true), ("과", true), ("으로", true),
        ("를", false), ("는", false), ("가", false), ("와", false),
        ("에", nil), ("의", nil), ("도", nil), ("에서", nil), ("만", nil),
    ]

    // REA: korean_vocabulary.txt (245k lines) read once, straight from its bytes, into what the
    // lookups need, the words as 64-bit hashes: Sets of the words themselves, their counts and
    // the text cost the iOS keyboard (killed near 70 MB) some 20 MB. Upstream has `stems`,
    // `vocabulary`, `rareVocabulary`, `vocabularyCounts` and `listSyllables` instead.
    // ponytail: a word off the list hashing onto one on it is a 1 in 10^14 chance per lookup.
    private static let lists: (vocabulary: Set<UInt64>, rare: Set<UInt64>, stems: Set<String>, syllables: Set<Character>) = {
        var vocabulary = Set<UInt64>(), rare = Set<UInt64>(), stems = Set<String>(), syllables = Set<Character>()
        func addStems(_ word: String) {
            for (particle, final) in particles where word.hasSuffix(particle) {
                let stem = word.dropLast(particle.count)
                guard stem.count >= 2, let last = stem.last?.unicodeScalars.first?.value,
                      (0xAC00...0xD7A3).contains(last) else { continue }
                let hasFinal = (last - 0xAC00) % 28 != 0
                if final == nil || final == hasFinal { stems.insert(String(stem)) }
            }
        }
        for word in corpusWords { addStems(word); syllables.formUnion(word) }
        guard let url = Resources.koreanVocabulary, let data = try? Data(contentsOf: url) else { return (vocabulary, rare, stems, syllables) }
        data.withUnsafeBytes { bytes in
            var start = 0
            while start < bytes.count {
                let end = bytes[start...].firstIndex(of: 10) ?? bytes.count
                let line = bytes[start..<end]
                start = end + 1
                guard let tab = line.firstIndex(of: 9), tab > line.startIndex,
                      let n = Int(String(decoding: line[(tab + 1)...], as: UTF8.self)) else { continue }
                let word = String(decoding: line[..<tab], as: UTF8.self)
                addStems(word)
                if n >= commonCount {
                    vocabulary.insert(hash(word))
                    syllables.formUnion(word)
                } else {
                    rare.insert(hash(word))
                }
            }
        }
        return (vocabulary, rare, stems, syllables)
    }()

    /// REA: FNV-1a of the UTF-8, the same on every launch (`Hasher` isn't).
    private static func hash(_ word: String) -> UInt64 {
        word.utf8.reduce(0xcbf2_9ce4_8422_2325) { ($0 ^ UInt64($1)) &* 0x100_0000_01b3 }
    }

    private static let corpusWords: Set<String> = {
        guard let url = Resources.koreanWords, let text = try? String(contentsOf: url, encoding: .utf8) else { return [] }
        return Set(text.split(whereSeparator: \.isNewline).compactMap { $0.split(separator: "\t").first.map(String.init) })
    }()

    private static func corpusCounts() -> [Character: Int]? {
        guard let url = Resources.koreanSyllables, let text = try? String(contentsOf: url, encoding: .utf8) else { return nil }
        var counts: [Character: Int] = [:]
        for line in text.split(whereSeparator: \.isNewline) {
            let parts = line.split(separator: "\t")
            if parts.count == 2, let s = parts[0].first, let n = Int(parts[1]) { counts[s] = n }
        }
        return counts
    }

    public func syllableLogProbability(_ syllable: Character) -> Double {
        tiers[syllable] ?? Self.unknownSyllableLogP
    }

    /// A syllable seen at all: in the tiers, the corpus or a word of the lists, however rarely
    /// (삐, 쪄, 뾰, 쭈 are; 뗘, 쎠, 쓔 aren't). Rarer than `unknownSyllableLogP` still means
    /// someone typed it.
    public func knowsSyllable(_ syllable: Character) -> Bool {
        tiers[syllable] != nil || Self.lists.syllables.contains(syllable) // REA: lists
    }

    /// - Parameter partial: the token is still being typed, so a trailing lone
    ///   consonant is expected rather than penalized.
    public func score(keys: String, partial: Bool = false) -> Score? {
        guard let composition = HangulComposer.compose(keys: keys), !keys.isEmpty else { return nil }
        var total = logProbability(composition, partial: partial)
        // The consonant typed last may have joined the syllable before as its final (개 + ㅎ
        // shows 갷) while it is the next syllable's first (소개하는): score that reading too. Only
        // on 2-set: 3-set has its own keys for finals.
        if partial, KoreanLayout.current == .dubeolsik, !composition.isChat, let last = keys.last, DubeolsikLayout.jamo(for: last).map(Jamo.isConsonant) == true,
           case .syllable = composition.units.last,
           let before = HangulComposer.compose(keys: String(keys.dropLast())), before.units.count == composition.units.count,
           case .syllable = before.units.last {
            total = max(total, logProbability(before, partial: false) + Self.pendingConsonantLogP - Self.pendingFinalPenalty)
        }
        return Score(averageLogProbability: total / Double(keys.count), composition: composition)
    }

    private func logProbability(_ composition: HangulComposer.Result, partial: Bool) -> Double {
        let units = composition.units
        let allChat = composition.isChat
        var total = 0.0
        for (i, unit) in units.enumerated() {
            switch unit {
            case .syllable(let s):
                total += syllableLogProbability(s)
            case .jamo(let j):
                if allChat {
                    total += Self.chatJamoLogP
                } else if partial && i == units.count - 1 && Jamo.isConsonant(j) {
                    total += Self.pendingConsonantLogP
                } else {
                    total += Self.strayJamoLogP
                }
            }
        }
        return total
    }
}

extension HangulComposer.Result {
    /// ㅋㅋ, ㅠㅠ, ㅜㅜ: two or more of the jamo typed on their own in chat. Korean as it is, and
    /// already complete: they don't go on to make syllables.
    /// On 3-set only the same jamo again (ㅋㅋ, ㅎㅎ): its consonants sit on the right hand's
    /// letters, so a mix of them is English as often as not (jun is ㅇㄷㅅ, uhh ㄷㄴㄴ).
    public var isChat: Bool {
        units.count >= 2 && units.allSatisfy {
            if case .jamo(let j) = $0 { return KoreanModel.chatJamo.contains(j) }
            return false
        } && (KoreanLayout.current == .dubeolsik || Set(units.map(\.character)).count == 1)
    }

    /// Two or more loose consonants, or ㅜ/ㅠ: what chat abbreviates to (ㅁㅊ, ㅈㅅ, ㄹㅇ, ㅜㅜ).
    /// A reading worth switching to, though only the common ones (`isChat`) score as Korean.
    public var isJamoAbbreviation: Bool {
        units.count >= 2 && units.allSatisfy {
            if case .jamo(let j) = $0 { return Jamo.isConsonant(j) || j == "ㅜ" || j == "ㅠ" }
            return false
        }
    }
}
