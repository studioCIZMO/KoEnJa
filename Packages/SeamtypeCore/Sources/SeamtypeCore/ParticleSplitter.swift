import Foundation

/// An English word with a particle typed straight after it (`githubdp` → `github에`,
/// `githubde` → `githubで`) is one more reading of the keys, scored like the whole-token
/// readings it competes with: average log-probability per keystroke.
///
/// - The front scores as English: a known word gets the dictionary floors `HeuristicEngine`
///   uses, capitals typed on purpose (API, GitHub) a higher one, anything else its spelling.
/// - The particle scores by how common it is, and switching language costs a little.
/// - The whole token scores as Korean (a Korean word, or one with a particle, gets a floor),
///   as unknown English, and with Japanese on as kana.
///
/// The split wins only by `margin`. `form에서` splits because 래그 is no Korean word and form
/// is everyday English; `핸들을` doesn't, because 핸들 is a word.
public struct ParticleSplitter: Sendable {
    public let english: EnglishModel
    public let korean: KoreanModel
    public let japanese: JapaneseModel?
    public var weights: Weights

    /// Log-probabilities for a whole word or particle, not per key, so a longer front or
    /// particle isn't favored for its length.
    public struct Weights: Sendable {
        /// A known English word: `knownBase` minus `perLevel` per SCOWL level (10 = everyday).
        public var knownBase = -4.0
        public var perLevel = 0.15
        /// An acronym (API, UI, PR) or a name with a capital inside (GitHub, VSCode, macOS).
        public var acronym = -6.0
        public var name = -7.0
        /// Taken off once from the spelling score of a front (or token) that isn't a known word.
        public var unknownWord = 3.0
        /// Taken off once more when the front isn't a known word: an unknown word with a
        /// particle after it is rarer than an unknown word (names: adriano, porto, chandi).
        public var unknownFront = 3.0
        /// A common particle (에, 를, で…) and the other endings (처럼, 했어, まで…).
        public var coreParticle = -2.5
        public var otherParticle = -4.5
        /// Once per split, for switching language inside a token.
        public var switchCost = 1.0
        /// Per key, the least a Korean word (or a word with a particle) scores.
        public var koreanWordFloor = -1.4
        /// Added per key to the kana reading's score, as `HeuristicEngine.japaneseOffset`.
        public var japaneseOffset = 1.4
        /// How far (per key) the split has to lead the best whole-token reading.
        public var margin = 0.3

        public init() {}
    }

    public enum Split: Equatable, Sendable {
        case korean(MixedWord)
        case japanese(english: String, particle: String)

        public var text: String {
            switch self {
            case .korean(let mixed): mixed.text
            case .japanese(let english, let particle): english + particle
            }
        }
    }

    /// Japanese particles that score `coreParticle` (から, まで, より score `otherParticle`).
    static let coreJapanese: Set<String> = ["で", "に", "を", "は", "が", "の", "と", "も"]
    /// Particles that follow a noun most of the time: they score `coreParticle`.
    static let coreKorean: Set<String> = Set("에 에서 에게 을 를 은 는 이 가 도 로 으로 와 과 의 만 까지 부터 랑 이랑 한테 처럼 보다".split(separator: " ").map(String.init))

    public init(english: EnglishModel, korean: KoreanModel = KoreanModel(), japanese: JapaneseModel? = nil,
                weights: Weights = Weights()) {
        self.english = english
        self.korean = korean
        self.japanese = japanese
        self.weights = weights
    }

    /// The best split of `keys`, if it beats reading the whole token as one language.
    /// Japanese particles only with `japaneseOn`.
    public func split(_ keys: String, japaneseOn: Bool) -> Split? {
        scored(keys, japaneseOn: japaneseOn).flatMap { $0.lead >= weights.margin ? $0.split : nil }
    }

    /// The best split and how far it leads the best whole-token reading (for tuning).
    public func scored(_ keys: String, japaneseOn: Bool) -> (split: Split, lead: Double, detail: String)? {
        let chars = Array(keys)
        let n = chars.count
        guard n >= 3, !english.isKnownWord(keys) else { return nil }
        var best: (split: Split, score: Double)?
        func offer(_ split: Split, front: String, particle: Double) {
            guard let w = frontScore(front) else { return }
            let score = (w + particle - weights.switchCost) / Double(n)
            if best.map({ score > $0.score }) ?? true { best = (split, score) }
        }
        for start in 2..<n {
            let tail = String(chars[start...])
            guard tail.first.map(DubeolsikLayout.startsSyllable) == true,
                  let composed = HangulComposer.compose(keys: tail), composed.strayJamo.isEmpty,
                  MixedWord.suffixes.contains(composed.text) else { continue }
            let front = String(chars[..<start])
            offer(.korean(MixedWord(english: front, koreanKeys: tail, korean: composed.text)), front: front,
                  particle: Self.coreKorean.contains(composed.text) ? weights.coreParticle : weights.otherParticle)
        }
        if japaneseOn, japanese != nil {
            for start in max(2, n - 4)..<n {
                let tail = RomajiComposer.compose(String(chars[start...]))
                guard tail.isClean, JapaneseSegmenter.trailingParticles.contains(tail.text) else { continue }
                offer(.japanese(english: String(chars[..<start]), particle: tail.text), front: String(chars[..<start]),
                      particle: Self.coreJapanese.contains(tail.text) ? weights.coreParticle : weights.otherParticle)
            }
        }
        guard let best else { return nil }
        let k = koreanScore(keys)
        let e = english.averageLogProbability(keys) - weights.unknownWord / Double(n)
        let j = japaneseOn ? japaneseScore(keys) : -.infinity
        let whole = max(k, e, j)
        let detail = String(format: "split=%.2f ko=%.2f en=%.2f ja=%.2f", best.score, k, e, j)
        return (best.split, best.score - whole, detail)
    }

    /// The front as English (log-probability of the word); nil when it can't be one.
    func frontScore(_ front: String) -> Double? {
        guard front.count >= 2, front.allSatisfy({ $0.isASCII && $0.isLetter }) else { return nil }
        // Capitals that are all shifted jamo in keys that spell clean Hangul are Korean typed
        // with shift (akffkRod: 말라깽, tkrlRns: 사기꾼), not an acronym or a name.
        let korean = front.allSatisfy { !$0.isUppercase || DubeolsikLayout.isKoreanShiftKey($0) }
            && HangulComposer.compose(keys: front).map { $0.strayJamo.isEmpty } == true
        if !korean, Self.isAcronym(front) { return weights.acronym }
        if !korean, isName(front) { return weights.name }
        // Lowercase (or a capital at the start: a name, or the start of a sentence) needs
        // 3 letters: so + が, me + で are Japanese.
        guard front.count >= 3 else { return nil }
        let spelling = english.averageLogProbability(front) * Double(front.count)
        if let level = english.level(front), level <= EnglishModel.knownLevel {
            return max(spelling, weights.knownBase - weights.perLevel * Double(level))
        }
        return spelling - weights.unknownWord - weights.unknownFront
    }

    /// GitHub, VSCode, iPhone, macOS: a capital inside, and the part from the last capital a
    /// known word (Hub, Code) or capitals (OS). Not APIdm.
    func isName(_ front: String) -> Bool {
        let chars = Array(front)
        guard chars.dropFirst().contains(where: \.isUppercase), chars.contains(where: \.isLowercase),
              let last = chars.lastIndex(where: \.isUppercase) else { return false }
        if last == chars.count - 1 {
            // Ends in capitals (macOS): the run of them is the last part.
            var start = last
            while start > 0, chars[start - 1].isUppercase { start -= 1 }
            return chars.count - start >= 2
        }
        // The last part starts at the last capital (Code of VSCode, Idm of APIdm).
        let part = String(chars[last...])
        return part.count >= 3 && english.isKnownWord(part)
    }

    /// API, UI, PR: 2 to 6 capitals.
    static func isAcronym(_ front: String) -> Bool {
        (2...6).contains(front.count) && front.allSatisfy(\.isUppercase)
    }

    /// The whole token as Korean, per key. A capital with no shifted jamo (the S of Slack)
    /// can't be Korean.
    func koreanScore(_ keys: String) -> Double {
        guard !SplittingEngine.hasEnglishOnlyCapital(keys), let score = korean.score(keys: keys) else { return -.infinity }
        let composition = score.composition
        if composition.strayJamo.isEmpty, isKoreanWordOrWordWithParticle(composition.text) {
            return max(score.averageLogProbability, weights.koreanWordFloor)
        }
        return score.averageLogProbability
    }

    /// The whole token as kana, per key. Capitals after the first letter aren't romaji
    /// (UIde is UI + で); one at the start may begin a sentence (Kyouha).
    func japaneseScore(_ keys: String) -> Double {
        guard !keys.dropFirst().contains(where: \.isUppercase), let score = japanese?.score(keys: keys) else { return -.infinity }
        return score.averageLogProbability + weights.japaneseOffset
    }

    /// 핸들, 소문은, 행하는: a word of the wider vocabulary, or one with a particle or ending.
    func isKoreanWordOrWordWithParticle(_ hangul: String) -> Bool {
        if korean.isVocabularyWord(hangul) { return true }
        let chars = Array(hangul)
        return chars.count > 1 && (1..<chars.count).contains { cut in
            korean.isVocabularyWord(String(chars[..<cut])) && MixedWord.suffixes.contains(String(chars[cut...]))
        }
    }
}
