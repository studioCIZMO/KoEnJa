import Foundation
import Testing
@testable import SeamtypeCore

/// tools/data of this checkout, or `ARASO_TOOLS_DATA`; nil when it has no eval.tsv.
enum ToolsData {
    static let url: URL? = {
        let url = ProcessInfo.processInfo.environment["ARASO_TOOLS_DATA"].map { URL(fileURLWithPath: $0) }
            ?? URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
                .deletingLastPathComponent().appendingPathComponent("tools/data")
        return FileManager.default.fileExists(atPath: url.appendingPathComponent("eval.tsv").path) ? url : nil
    }()
}

/// The wrong-split check for a remembered word with a particle after it (`DictionaryEngine.withParticle`),
/// over held-out data: with every eval.tsv word, or the top 20k words of en_50k as English, in the
/// dictionary, no eval.tsv row may split off a particle. Before ParticleSplitter's guards, 28 did
/// (sha+들을 for 놈들을, toki+は for ときは).
///
/// Needs `tools/prepare.sh` (tools/data is gitignored); `ARASO_TOOLS_DATA` points a worktree at
/// another checkout's copy. Skipped without it.
@Suite(.disabled(if: ToolsData.url == nil, "tools/data missing: run tools/prepare.sh"))
struct DictionarySplitCheckTests {
    /// keys, previous word's language, expected language.
    let rows: [(keys: String, previous: Language?, expected: String)]
    let base = SplittingEngine(base: trilingual, english: trilingual.english, japanese: trilingual.segmenter)

    init() throws {
        let data = try #require(ToolsData.url)
        rows = try String(contentsOf: data.appendingPathComponent("eval.tsv"), encoding: .utf8)
            .split(separator: "\n").compactMap { line in
                let fields = line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
                return fields.count >= 3 ? (fields[0], Language(rawValue: fields[1]), fields[2]) : nil
            }
    }

    /// Rows read as a word of the dictionary with a particle after it.
    private func splits(with entries: [UserDictionary.Entry]) throws -> [String] {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("araso-split-\(UUID().uuidString).json")
        defer { try? FileManager.default.removeItem(at: url) }
        try #require(UserDictionary(entries).encoded()).write(to: url)
        let engine = DictionaryEngine(base: base, english: trilingual.english, korean: trilingual.korean,
                                      japaneseParticles: true, storeURL: url)
        return rows.compactMap { row in
            let d = engine.decide(row.keys, context: DecisionContext(previousLanguage: row.previous))
            guard d.explanation.hasSuffix("+particle") else { return nil }
            let plain = base.decide(row.keys, context: DecisionContext(previousLanguage: row.previous))
            return "\(row.keys) [\(row.expected)] \(plain.output) → \(d.output) (\(d.explanation))"
        }
    }

    @Test func evalWordsAsEntries() throws {
        var seen = Set<String>()
        let entries = rows.compactMap { row -> UserDictionary.Entry? in
            guard let language = Language(rawValue: row.expected), seen.insert(row.keys).inserted else { return nil }
            return .init(keys: row.keys, language: language, source: .user)
        }
        let wrong = try splits(with: entries)
        #expect(wrong.isEmpty, "\(wrong.count) rows split:\n\(wrong.prefix(30).joined(separator: "\n"))")
    }

    @Test func commonEnglishWordsAsEntries() throws {
        let data = try #require(ToolsData.url)
        let words = try String(contentsOf: data.appendingPathComponent("en_50k.txt"), encoding: .utf8)
            .split(separator: "\n").prefix(20_000).compactMap { $0.split(separator: " ").first.map(String.init) }
        let wrong = try splits(with: words.map { .init(keys: $0, language: .english, source: .user) })
        #expect(wrong.isEmpty, "\(wrong.count) rows split:\n\(wrong.prefix(30).joined(separator: "\n"))")
    }
}
