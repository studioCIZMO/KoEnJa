import Foundation

/// Finds data files both inside the installed app (Seamtype.app/Contents/Resources)
/// and in the source tree during development and tests.
///
/// SPM resource bundles are avoided on purpose: their accessor looks for the bundle
/// at the .app root, which is outside the signed Contents and breaks code signing.
public enum Resources {
    /// Source tree root, from this file's location (Sources/SeamtypeCore/Resources.swift).
    static let packageRoot = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()

    static func url(_ name: String, development: URL) -> URL? {
        var candidates: [URL] = []
        if let resources = Bundle.main.resourceURL { candidates.append(resources.appendingPathComponent(name)) }
        if let dir = ProcessInfo.processInfo.environment["SEAMTYPE_RESOURCES"] {
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

    /// Korean words seen in sentences, with counts (Tatoeba; build_korean_words.py).
    public static var koreanWords: URL? {
        url("korean_words.txt", development: packageRoot.appendingPathComponent("Sources/SeamtypeCore/Data/korean_words.txt"))
    }

    /// Korean words of two or more syllables, loanwords included (FrequencyWords;
    /// build_korean_vocabulary.py). Only for splitting `hello월드`.
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
