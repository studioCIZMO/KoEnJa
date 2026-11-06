import Foundation

/// Character-trigram language model plus a word list with how common each word is, from
/// SCOWL (Data/english_words.txt: word<TAB>level, 10 = most common … 60).
public final class EnglishModel: @unchecked Sendable {
    // Symbols: 0 = word boundary, 1...26 = a...z
    private static let symbols = 27
    private var trigrams: [Float]
    private var contexts: [Float]
    /// SCOWL level of each word (lowercase, letters only). REA: sorted words beside their levels,
    /// as the Kotlin port keeps them, not a Dictionary (some 6 MB at the iOS keyboard's launch).
    private let words: [String]
    private let wordLevels: [UInt8]
    /// The levels a guessed inflection may take from its stem: the list's own, not `commonWords`
    /// (nil: `levels`). REA: only the words `commonWords` changed (nil inside: not listed).
    private let stemLevels: [String: Int?]?
    private let smoothing: Float = 0.5

    /// Words up to this level count as English words (a score floor, protection from kana).
    public static let knownLevel = 50
    /// Words up to this level are everyday English (you, sense, machine, for, got).
    public static let commonLevel = 20
    /// Level given to `commonWords` (tech terms and slang SCOWL lacks): known, not "common".
    static let extraLevel = 35
    /// Letter trigrams come from this list when it's there (macOS ships it): its 235k words
    /// separate English spelling from Korean keys better than SCOWL's 119k (measured).
    public static let spellingListPath = "/usr/share/dict/words"

    /// macOS's list, else the same list shipped with Araso (Data/english_spelling.txt, web2, which
    /// Windows doesn't have): without it English scores differently and Japanese loses words.
    public static var spellingList: String? {
        FileManager.default.fileExists(atPath: spellingListPath) ? spellingListPath : Resources.englishSpelling?.path
    }

    /// Loading the word list takes a moment; share one instance.
    public static let shared = EnglishModel(listURL: Resources.englishWords, spellingPath: spellingList)

    /// Acronyms people write (RNA, GMT, WYSIWYG; SCOWL, Data/english_acronyms.txt), in capitals.
    public static let acronyms: Set<String> = {
        guard let url = Resources.englishAcronyms, let text = try? String(contentsOf: url, encoding: .utf8) else {
            NSLog("Araso: English acronym list missing")
            return []
        }
        return Set(text.split(whereSeparator: \.isNewline).map(String.init))
    }()

    /// From a word<TAB>level file; a missing file leaves only `commonWords` (logged). Trigrams
    /// are trained on `spellingPath`'s words when it can be read, otherwise on the list.
    public convenience init(listURL: URL?, spellingPath: String? = nil, extraWords: [String] = EnglishModel.commonWords) {
        // REA: the list read mapped, line by line, into (word, level) pairs: a Dictionary of it and
        // the split text cost the iOS keyboard some 25 MB at launch that malloc kept afterwards.
        var levels: [(String, Int)] = []
        if let listURL, let data = try? Data(contentsOf: listURL, options: .alwaysMapped) {
            levels.reserveCapacity(Resources.lineCount(data))
            // ponytail: a word listed twice keeps its lowest level (upstream: the later line); the list has none.
            Resources.forEachPair(in: data) { word, level in
                if let level = Int(level) { levels.append((word, level)) }
            }
        }
        if levels.isEmpty { NSLog("Araso: English word list missing (%@)", listURL?.path ?? "nil") }
        // REA: the extra words go in in init(levels:), not into a copy of the 119k-word list kept as
        // its own levels (+7 MB at the iOS keyboard's launch; it is killed near 70 MB). The spelling
        // list is read mapped and streamed into the trigrams, not split into 236k strings.
        let spelling = spellingPath.flatMap { try? Data(contentsOf: URL(fileURLWithPath: $0), options: .alwaysMapped) }
        self.init(entries: levels, extraWords: extraWords, spelling: nil, spellingFile: spelling)
    }

    /// For tests: these words at a middle level.
    public convenience init(words list: [String]) {
        self.init(levels: Dictionary(list.map { ($0.lowercased(), Self.extraLevel) }, uniquingKeysWith: min))
    }

    public convenience init(levels raw: [String: Int], spelling: [String]? = nil, extraWords: [String] = []) {
        self.init(entries: Array(raw), extraWords: extraWords, spelling: spelling, spellingFile: nil)
    }

    /// REA: trigrams from `spelling`, else from `spellingFile`'s lines (lowercased, as an ASCII
    /// list like web2 is) and then `extraWords`, else from the levels' words.
    init(entries raw: [(String, Int)], extraWords: [String], spelling: [String]?, spellingFile: Data?) {
        let n = Self.symbols
        var tri = [Float](repeating: 0, count: n * n * n)
        var ctx = [Float](repeating: 0, count: n * n)
        // REA: sorted by word, each once at its lowest level (upstream: a Dictionary, min on a clash).
        func normalized(_ raw: [(String, Int)]) -> [(word: String, level: Int)] {
            var levels: [(word: String, level: Int)] = []
            levels.reserveCapacity(raw.count)
            for (raw, level) in raw {
                let word = raw.contains("'") ? raw.filter { $0 != "'" } : raw
                guard !word.isEmpty, word.allSatisfy({ $0.isASCII && $0.isLetter }) else { continue }
                // Many 1–2 letter list entries aren't words people type ("ra", "ko"); only real ones count.
                if word.count > 2 || Self.shortWords.contains(word) { levels.append((word, level)) }
            }
            levels.sort { $0.word != $1.word ? $0.word < $1.word : $0.level < $1.level }
            var last: String?
            levels.removeAll { defer { last = $0.word }; return $0.word == last }
            return levels
        }
        var levels = normalized(raw)
        func find(_ word: String) -> Int? {
            var low = 0, high = levels.count
            while low < high {
                let mid = (low + high) / 2
                if levels[mid].word < word { low = mid + 1 } else { high = mid }
            }
            return low < levels.count && levels[low].word == word ? low : nil
        }
        // REA: upstream normalizes the list with the extra words in and keeps a second copy without.
        var listed: [String: Int?] = [:]
        var added: [(word: String, level: Int)] = []
        for (word, _) in normalized(extraWords.map { ($0.lowercased(), Self.extraLevel) }) {
            if let i = find(word) {
                listed[word] = levels[i].level
                levels[i].level = min(levels[i].level, Self.extraLevel)
            } else {
                listed[word] = .some(nil)
                added.append((word, Self.extraLevel))
            }
        }
        if !added.isEmpty {
            levels += added
            levels.sort { $0.word < $1.word }
        }
        stemLevels = extraWords.isEmpty ? nil : listed
        func train(_ word: String) {
            guard !word.isEmpty, word.allSatisfy({ $0.isASCII && $0.isLetter }) else { return }
            let codes = Self.encode(word)
            for i in 2..<codes.count {
                tri[(codes[i - 2] * n + codes[i - 1]) * n + codes[i]] += 1
                ctx[codes[i - 2] * n + codes[i - 1]] += 1
            }
        }
        if let spelling {
            spelling.forEach(train)
        } else if let spellingFile {
            spellingFile.withUnsafeBytes { (bytes: UnsafeRawBufferPointer) in
                var start = 0
                for i in 0...bytes.count where i == bytes.count || bytes[i] == 10 || bytes[i] == 13 {
                    if i > start { train(String(decoding: UnsafeRawBufferPointer(rebasing: bytes[start..<i]), as: UTF8.self).lowercased()) }
                    start = i + 1
                }
            }
            extraWords.forEach(train)
        } else {
            levels.forEach { train($0.word) }
        }
        trigrams = tri
        contexts = ctx
        words = levels.map(\.word)
        wordLevels = levels.map { UInt8(clamping: $0.level) }
    }

    public var wordCount: Int { words.count }

    /// REA: the listed level, by binary search of `words`.
    private func listedLevel(_ word: String) -> Int? {
        var low = 0, high = words.count
        while low < high {
            let mid = (low + high) / 2
            if words[mid] < word { low = mid + 1 } else { high = mid }
        }
        return low < words.count && words[low] == word ? Int(wordLevels[low]) : nil
    }

    /// [^, ^, letters..., $] with ^ and $ both encoded as 0.
    private static func encode(_ word: String) -> [Int] {
        var codes = [0, 0]
        for scalar in word.unicodeScalars {
            let v = Int(scalar.value)
            if v >= 97 && v <= 122 { codes.append(v - 96) }
        }
        codes.append(0)
        return codes
    }

    /// Average natural-log probability per symbol (letters + end marker).
    /// Real English words land around -1.8…-2.4; keyboard mash around -3.5 or lower.
    public func averageLogProbability(_ token: String) -> Double {
        let n = Self.symbols
        let codes = Self.encode(token.lowercased())
        guard codes.count > 3 else { return -10 }
        var total: Double = 0
        for i in 2..<codes.count {
            let c = trigrams[(codes[i - 2] * n + codes[i - 1]) * n + codes[i]]
            let t = contexts[codes[i - 2] * n + codes[i - 1]]
            total += Double(log((c + smoothing) / (t + smoothing * Float(n))))
        }
        return total / Double(codes.count - 2)
    }

    /// The word's level, also for regular inflections of a listed word (nil: not a word).
    public func level(_ token: String) -> Int? {
        let w = token.lowercased().filter { $0 != "'" }
        if let level = listedLevel(w) { return level }
        var best: Int?
        // SCOWL lists the inflections of its words, so a guessed one counts only for a stem the
        // list itself ranks above its rarest known level: thru (50, and 35 only as one of
        // `commonWords`) + s is no word, and thrus is 소견.
        func consider(_ stem: String) {
            // REA: stemLevels holds only the words `commonWords` changed.
            if let l = stemLevels?[stem] ?? listedLevel(stem), l < Self.knownLevel { best = min(best ?? l, l) }
        }
        // Stems of 3+ letters: ths isn't the+s, ehs isn't eh+s.
        for suffix in ["s", "es", "ed", "d", "ing", "ly", "er", "ers", "est"] where w.count > suffix.count + 2 && w.hasSuffix(suffix) {
            let stem = String(w.dropLast(suffix.count))
            consider(stem)
            consider(stem + "e")
            // running → run, stopped → stop
            if let last = stem.last, stem.dropLast().last == last { consider(String(stem.dropLast())) }
        }
        // stories → story, denied → deny, heavier → heavy
        for suffix in ["ies", "ied", "ier", "iest", "iers"] where w.count > suffix.count + 1 && w.hasSuffix(suffix) {
            consider(String(w.dropLast(suffix.count)) + "y")
        }
        return best
    }

    /// A word people use (SCOWL level ≤ 50, or one of `commonWords`).
    public func isKnownWord(_ token: String) -> Bool {
        level(token).map { $0 <= Self.knownLevel } ?? false
    }

    /// Everyday English (level ≤ 20): kept English even right after Korean or Japanese.
    public func isCommonWord(_ token: String) -> Bool {
        level(token).map { $0 <= Self.commonLevel } ?? false
    }

    static let shortWords: Set<String> = Set("""
    a i ad ah am an as at aw ax be by do eh go ha he hi hm if in is it lo ma me mo my no of oh ok on or ow ox pa
    pc so to tv uh um up us we ya ye yo id ex vs ai ui ux os js ts
    """.split(whereSeparator: \.isWhitespace).map(String.init))

    /// Everyday/technical words SCOWL is missing or ranks rare.
    public static let commonWords: [String] = """
    ok okay hi hey yeah yep nope lol omg btw thx thanks pls please email emails app apps api apis url urls
    http https www github git commit push pull merge branch repo json yaml xml html css js ts swift python java
    kotlin rust golang npm yarn pnpm node react vue svelte docker kubernetes ios macos android iphone ipad mac wifi bluetooth usb
    login logout signup username password online offline website blog vlog youtube google slack notion figma
    debug deploy refactor config backend frontend database cache server client async await todo todos ai llm gpt
    claude chatgpt laptop smartphone selfie emoji internet podcast startup dev devs devops ui ux
    wanna gonna gotta kinda sorta outta lemme gimme dunno yeah yup nah ain cuz tho thru hiya kiddo sayin
    goin doin nothin somethin y'all ya yo hey oi aye
    wouldn couldn shouldn didn doesn isn wasn weren hasn haven hadn needn mustn aren mightn
    makeup brooklyn shakespeare einstein techno pesto americano cosmo promo garde kanji kana romaji ramen
    """.split(whereSeparator: { $0.isWhitespace }).map(String.init)
}
