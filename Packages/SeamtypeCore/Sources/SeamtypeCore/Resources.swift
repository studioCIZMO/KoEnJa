import Foundation

/// Finds data files both inside the installed app (Araso.app/Contents/Resources)
/// and in the source tree during development and tests.
///
/// SPM resource bundles are avoided on purpose: their accessor looks for the bundle
/// at the .app root, which is outside the signed Contents and breaks code signing.
public enum Resources {
    /// REA: each line of `data` with exactly two tab-separated fields (empty ones skipped), read
    /// in place: what `split(whereSeparator: \.isNewline)` and `split(separator: "\t")` of the
    /// whole text gave, without holding it and its 200k substrings (memory at the iOS keyboard's launch).
    static func forEachPair(in data: Data, _ body: (String, String) -> Void) {
        data.withUnsafeBytes { (bytes: UnsafeRawBufferPointer) in
            var start = 0
            for i in 0...bytes.count where i == bytes.count || bytes[i] == 10 || bytes[i] == 13 {
                defer { start = i + 1 }
                guard i > start else { continue }
                let fields = bytes[start..<i].split(separator: 9)
                guard fields.count == 2 else { continue }
                body(String(decoding: fields[0], as: UTF8.self), String(decoding: fields[1], as: UTF8.self))
            }
        }
    }

    /// REA: the number of lines, to size an array before reading them.
    static func lineCount(_ data: Data) -> Int { data.reduce(0) { $1 == 10 ? $0 + 1 : $0 } + 1 }

    /// Set by a host that keeps the data files elsewhere (Windows: `Resources` beside the host).
    nonisolated(unsafe) public static var directory: URL?

    /// Source tree root, from this file's location (Sources/SeamtypeCore/Resources.swift).
    static let packageRoot = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()

    static func url(_ name: String, development: URL) -> URL? {
        var candidates: [URL] = []
        if let directory { candidates.append(directory.appendingPathComponent(name)) }
        if let resources = Bundle.main.resourceURL { candidates.append(resources.appendingPathComponent(name)) }
        if let dir = ProcessInfo.processInfo.environment["ARASO_RESOURCES"] {
            candidates.append(URL(fileURLWithPath: dir).appendingPathComponent(name))
        }
        candidates.append(development)
        return candidates.first { FileManager.default.fileExists(atPath: $0.path) }
    }

    /// Kana trigram counts (tools/build_kana_model.py).
    public static var kanaModel: URL? {
        url("kana_trigrams.txt", development: packageRoot.appendingPathComponent("Sources/SeamtypeCore/Data/kana_trigrams.txt"))
    }

    /// Character trigrams of written Japanese, for reranking conversions (build_text_model.py).
    public static var textModel: URL? {
        url("text_trigrams.txt", development: packageRoot.appendingPathComponent("Sources/SeamtypeCore/Data/text_trigrams.txt"))
    }

    /// English words with their SCOWL level, 10 (most common) to 60 (build_english_words.py).
    public static var englishWords: URL? {
        url("english_words.txt", development: packageRoot.appendingPathComponent("Sources/SeamtypeCore/Data/english_words.txt"))
    }

    /// English acronyms of SCOWL up to size 70 (build_english_acronyms.py).
    public static var englishAcronyms: URL? {
        url("english_acronyms.txt", development: packageRoot.appendingPathComponent("Sources/SeamtypeCore/Data/english_acronyms.txt"))
    }

    /// Webster's Second International (web2, public domain): the letter trigrams' spellings where
    /// the system has no /usr/share/dict/words (Windows, Linux).
    public static var englishSpelling: URL? {
        url("english_spelling.txt", development: packageRoot.appendingPathComponent("Sources/SeamtypeCore/Data/english_spelling.txt"))
    }

    /// Korean words seen in sentences, with counts (Tatoeba; build_korean_words.py).
    public static var koreanWords: URL? {
        url("korean_words.txt", development: packageRoot.appendingPathComponent("Sources/SeamtypeCore/Data/korean_words.txt"))
    }

    /// Korean words of two or more syllables, loanwords included (FrequencyWords;
    /// build_korean_vocabulary.py): splitting `hello월드`, and the heuristic's Korean word floor.
    public static var koreanVocabulary: URL? {
        url("korean_vocabulary.txt", development: packageRoot.appendingPathComponent("Sources/SeamtypeCore/Data/korean_vocabulary.txt"))
    }

    /// Korean syllable counts from sentences (Tatoeba).
    public static var koreanSyllables: URL? {
        url("korean_syllables.txt", development: packageRoot.appendingPathComponent("Sources/SeamtypeCore/Data/korean_syllables.txt"))
    }

    /// AzooKey's dictionary directory (Apache-2.0).
    public static var japaneseDictionary: URL? {
        url("Dictionary", development: packageRoot.appendingPathComponent(
            ".build/checkouts/AzooKeyKanaKanjiConverter/Sources/KanaKanjiConverterModuleWithDefaultDictionary/azooKey_dictionary_storage/Dictionary"))
    }
}
