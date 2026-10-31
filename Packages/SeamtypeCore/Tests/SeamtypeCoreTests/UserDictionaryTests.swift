import Foundation
import Testing
@testable import SeamtypeCore

/// dictionary.json: corrected once nudges, twice wins, weak entries expire, lexicon.json moves in.
@Suite struct UserDictionaryTests {
    let dir: URL
    let day = Date(timeIntervalSince1970: 1_792_000_000)

    init() throws {
        dir = FileManager.default.temporaryDirectory.appendingPathComponent("araso-dict-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    }

    private func days(_ n: Int) -> Date { day.addingTimeInterval(Double(n) * 86_400) }

    /// One correction only nudges: `so` (en −1.90, ko −2.00) turns, `hello` doesn't.
    @Test func onceNudgesACloseCallOnly() {
        let engine = DictionaryEngine(base: sharedEngine, storeURL: dir.appendingPathComponent("d.json"))
        #expect(engine.decide("so", context: DecisionContext()).language == .english)
        engine.learn("so", as: .korean, engineChoice: .english)
        #expect(engine.pending == ["so": .korean] && engine.learned.isEmpty)
        let so = engine.decide("so", context: DecisionContext())
        #expect(so.language == .korean && so.output == "내", "\(so.explanation)")
        // After an English word the previous word's pull and the nudge cancel out.
        #expect(engine.decide("so", context: DecisionContext(previousLanguage: .english)).language == .english)

        engine.learn("hello", as: .korean, engineChoice: .english)
        #expect(engine.decide("hello", context: DecisionContext()).language == .english)
        engine.learn("hello", as: .korean, engineChoice: .english)
        let hello = engine.decide("hello", context: DecisionContext())
        #expect(hello.language == .korean && hello.confidence == 1 && hello.explanation == "learned")
    }

    /// REA: MyTypeless's terms win as added words do: Hangul by its 2-set keys, Latin as written
    /// and in lower case (not keys that read as Korean), under the user's own entries, re-read
    /// when the file changes, and never written into dictionary.json.
    @Test func myTypelessTermsWinUnderOwnEntries() throws {
        #expect(DubeolsikLayout.keys(for: "깃허브") == "rltgjqm")
        #expect(DubeolsikLayout.keys(for: "왠지 맑은") == nil)
        #expect(DubeolsikLayout.keys(for: "왠지") == "dhoswl" && DubeolsikLayout.keys(for: "맑은") == "akfrdms")
        #expect(DubeolsikLayout.keys(for: "꽉") == "Rhkr" && DubeolsikLayout.keys(for: "ㄱ") == nil)
        let terms = dir.appendingPathComponent("terms.json"), store = dir.appendingPathComponent("d.json")
        try Data(#"[{"term":"깃허브","source":"manual"},{"term":"KoEnJa"},{"term":"rk"},{"term":"日本語"}]"#.utf8).write(to: terms)
        let engine = DictionaryEngine(base: sharedEngine, storeURL: store, sharedURL: terms)
        let git = engine.decide("rltgjqm", context: DecisionContext())
        #expect(git.language == .korean && git.output == "깃허브" && git.confidence == 1)
        #expect(engine.decide("koenja", context: DecisionContext()).output == "koenja")
        #expect(engine.decide("KoEnJa", context: DecisionContext()).language == .english)
        #expect(engine.decide("rk", context: DecisionContext()).output == "가")
        engine.add(UserDictionary.Entry(keys: "koenja", language: .korean, source: .user))
        #expect(engine.decide("koenja", context: DecisionContext()).language == .korean)
        #expect(engine.dictionary.entries.count == 1)

        try Data(#"[{"term":"시즈모"}]"#.utf8).write(to: terms)
        try FileManager.default.setAttributes([.modificationDate: Date().addingTimeInterval(60)], ofItemAtPath: terms.path)
        engine.refreshShared()
        #expect(engine.decide("tlwmah", context: DecisionContext()).output == "시즈모")
        #expect(engine.decide("rltgjqm", context: DecisionContext()).explanation != "added")
    }

    /// A remembered word switched once to another reading keeps its memory and notes the switch;
    /// the same switch again moves the memory, and typing the word as remembered drops the note.
    @Test func rememberedWordMovesOnTheSecondSwitch() {
        var dictionary = UserDictionary()
        dictionary.learn("tori", as: .english, shown: .japanese, today: day)
        dictionary.learn("tori", as: .english, shown: .japanese, today: day)
        let switched = dictionary.learn("tori", as: .korean, shown: .english, today: day)
        #expect(!switched)
        #expect(dictionary.languageEntry(for: "tori")?.language == .english)
        #expect(dictionary.languageEntry(for: "tori")?.next == .korean)
        let typed = dictionary.used("tori", as: .english, today: day)
        #expect(typed)
        #expect(dictionary.languageEntry(for: "tori")?.next == nil)
        dictionary.learn("tori", as: .korean, shown: .english, today: day)
        let moved = dictionary.learn("tori", as: .korean, shown: .english, today: day)
        #expect(moved)
        #expect(dictionary.languageEntry(for: "tori")?.language == .korean)
        #expect(dictionary.languageEntry(for: "tori")?.strength == .sure)
        #expect(dictionary.languageEntry(for: "tori")?.next == nil)
    }

    @Test func learningRules() {
        var dictionary = UserDictionary()
        let first = dictionary.learn("dlrjs", as: .korean, shown: .english, today: day)
        #expect(!first)
        #expect(dictionary.languageEntry(for: "dlrjs")?.strength == .weak)
        // Corrected to something else: weak for that one instead.
        dictionary.learn("dlrjs", as: .japanese, shown: .english, today: day)
        #expect(dictionary.languageEntry(for: "dlrjs")?.language == .japanese)
        #expect(dictionary.languageEntry(for: "dlrjs")?.strength == .weak)
        let second = dictionary.learn("dlrjs", as: .japanese, shown: .english, today: day)
        #expect(second)
        #expect(dictionary.languageEntry(for: "dlrjs")?.strength == .sure)
        // Back to what was shown without the entry: forgotten.
        dictionary.learn("dlrjs", as: .english, shown: .english, today: day)
        #expect(dictionary.entries.isEmpty)
        // Turned by a weak entry and switched away from it: forgotten.
        dictionary.learn("so", as: .korean, shown: .english, today: day)
        dictionary.learn("so", as: .english, shown: .korean, today: day)
        #expect(dictionary.entries.isEmpty)
        // A sentence switched as one token isn't learned.
        dictionary.learn("taxidnswjstndkwjTlkonnichiha", as: .korean, shown: .english, today: day)
        #expect(dictionary.entries.isEmpty)
    }

    @Test func wordsAddedByHandStay() {
        var dictionary = UserDictionary([
            .init(keys: "hoge", language: .japanese, source: .user),
            .init(keys: "rt", text: "감사합니다", source: .user),
        ])
        dictionary.learn("hoge", as: .english, shown: .japanese, today: day)
        dictionary.learn("hoge", as: .english, shown: .japanese, today: day)
        #expect(dictionary.languageEntry(for: "hoge")?.language == .japanese)
        dictionary.learn("rk", as: .korean, shown: .english, today: day)
        dictionary.forgetLearned()
        #expect(dictionary.entries.count == 2)
        // The text entry isn't a language: forgetting the keys leaves it.
        dictionary.forget("rt")
        dictionary.forget("hoge")
        #expect(dictionary.entries.map(\.keys) == ["rt"])
    }

    @Test func weakEntriesExpireUnlessUsed() {
        var dictionary = UserDictionary()
        dictionary.learn("so", as: .korean, shown: .english, today: day)
        dictionary.learn("rk", as: .korean, shown: .english, today: day)
        dictionary.learn("rk", as: .korean, shown: .english, today: day)
        dictionary.learn("gks", as: .korean, shown: .english, today: day)
        let used = dictionary.used("gks", as: .korean, today: days(20))
        let sameDay = dictionary.used("gks", as: .korean, today: days(20))
        let otherLanguage = dictionary.used("gks", as: .english, today: days(21))
        #expect(used && !sameDay && !otherLanguage)
        let early = dictionary.dropExpired(today: days(29))
        let month = dictionary.dropExpired(today: days(30))
        #expect(!early && month)
        // `so` went; the sure `rk` and the `gks` typed on day 20 stay.
        #expect(dictionary.entries.map(\.keys) == ["rk", "gks"])
    }

    @Test func savesAndReadsBack() throws {
        let url = dir.appendingPathComponent("dictionary.json")
        let engine = DictionaryEngine(base: sharedEngine, storeURL: url, today: { [day] in day })
        engine.learn("hello", as: .korean, engineChoice: .english)
        engine.learn("hello", as: .korean, engineChoice: .english)
        engine.learn("so", as: .korean, engineChoice: .english)
        let json = try String(contentsOf: url, encoding: .utf8)
        #expect(json.contains(#""as" : "korean""#) && json.contains(#""from" : "learned""#) && !json.contains("text"))
        let reloaded = DictionaryEngine(base: sharedEngine, storeURL: url, today: { [day] in day })
        #expect(reloaded.learned == ["hello": .korean] && reloaded.pending == ["so": .korean])
        // A month later the weak one is gone, also from the file.
        let later = DictionaryEngine(base: sharedEngine, storeURL: url, today: { [day] in day.addingTimeInterval(31 * 86_400) })
        #expect(later.pending.isEmpty && later.learned == ["hello": .korean])
        #expect(UserDictionary.decode(try Data(contentsOf: url))?.entries.count == 1)
    }

    @Test func lexiconMovesIn() throws {
        let legacy = dir.appendingPathComponent("lexicon.json")
        let url = dir.appendingPathComponent("dictionary.json")
        try Data(#"""
        {"learned": {"hoge": "japanese"},
         "once": {"chraork": "korean", "hoge": "english", "taxidnswjstndkwjTlkonnichiha": "korean"}}
        """#.utf8).write(to: legacy)
        let migrated = DictionaryEngine.migrate(from: legacy, to: url, today: day)
        #expect(migrated.entries.map(\.keys) == ["hoge", "chraork"])
        #expect(migrated.languageEntry(for: "hoge")?.strength == .sure)
        #expect(migrated.languageEntry(for: "chraork")?.strength == .weak)
        #expect(!FileManager.default.fileExists(atPath: legacy.path))
        #expect(FileManager.default.fileExists(atPath: legacy.appendingPathExtension("migrated").path))
        let engine = DictionaryEngine(base: sharedEngine, storeURL: url)
        #expect(engine.learned == ["hoge": .japanese])
    }

    struct English: DecisionEngine {
        func decide(_ keys: String, context: DecisionContext) -> Decision {
            Decision(language: .english, confidence: 0.6, output: keys, explanation: "fixed")
        }
    }

    private func engine(_ entries: [UserDictionary.Entry], japanese: Bool = false) throws -> DictionaryEngine {
        let url = dir.appendingPathComponent("p-\(UUID().uuidString).json")
        try #require(UserDictionary(entries).encoded()).write(to: url)
        return DictionaryEngine(base: English(), japaneseParticles: japanese, storeURL: url)
    }

    /// A word that wins carries over to the word with a particle after it.
    @Test func wordWithParticle() throws {
        let engine = try engine([
            .init(keys: "chrao", language: .korean, source: .user),
            .init(keys: "tkfka", language: .korean, source: .learned, strength: .sure),
            .init(keys: "ghostty", language: .english, source: .user),
            .init(keys: "hoge", language: .japanese, source: .user),
            .init(keys: "zig", language: .english, source: .learned, strength: .weak),
        ], japanese: true)
        func read(_ keys: String) -> String {
            let d = engine.decide(keys, context: DecisionContext())
            return "\(d.language.rawValue) \(d.output)"
        }
        #expect(read("chraofmf") == "korean 촉매를")
        #expect(read("chraork") == "korean 촉매가")
        #expect(read("tkfkadms") == "korean 사람은")
        // The form the word doesn't take (사람는) isn't the word with a particle.
        #expect(read("tkfkasms") == "english tkfkasms")
        #expect(read("ghosttydp") == "mixed ghostty에")
        #expect(read("ghosttydptj") == "mixed ghostty에서")
        #expect(read("ghosttyde") == "japanese ghosttyで")
        #expect(read("hogede") == "japanese ほげで")
        #expect(read("hogekara") == "japanese ほげから")
        // Not a particle, a weak word, or keys that are an English word as a whole.
        #expect(read("ghosttyx") == "english ghosttyx")
        #expect(read("zigdp") == "english zigdp")
        // Keys that read as a Korean word with a particle, or as clean romaji, stay whole;
        // a lowercase English word needs 3 letters (found by giving every en_50k word an entry).
        let short = try self.engine([.init(keys: "sha", language: .english, source: .user),
                                     .init(keys: "toki", language: .english, source: .user),
                                     .init(keys: "to", language: .english, source: .user),
                                     .init(keys: "PC", language: .english, source: .user)], japanese: true)
        #expect(short.decide("shaemfdmf", context: DecisionContext()).language == .english)  // 놈들을
        #expect(short.decide("tokiha", context: DecisionContext()).language == .english)     // ときは
        #expect(short.decide("tosms", context: DecisionContext()).language == .english)      // 새는
        #expect(short.decide("PCdp", context: DecisionContext()).output == "PC에")
        let off = try self.engine([.init(keys: "hoge", language: .japanese, source: .user)])
        #expect(off.decide("hogede", context: DecisionContext()).language == .english)
        let ghost = try self.engine([.init(keys: "ghos", language: .english, source: .user)])
        #expect(ghost.decide("ghost", context: DecisionContext()).output == "ghost")
    }

    /// Added from the settings window: one entry per keys and apps, an app's own one first.
    @Test func addPinRemoveAndApps() throws {
        let engine = try engine([.init(keys: "dlrjs", language: .korean, source: .learned, strength: .sure)])
        let slack = "com.tinyspeck.slackmacgap"
        engine.add(.init(keys: "gst", language: .korean, source: .user))
        engine.add(.init(keys: "gst", language: .english, source: .user, apps: [slack]))
        engine.add(.init(keys: "gst", language: .japanese, source: .user, apps: ["com.jetbrains."]))
        func read(_ keys: String, in app: String?) -> Language {
            engine.decide(keys, context: DecisionContext(app: app)).language
        }
        #expect(read("gst", in: nil) == .korean)
        #expect(read("gst", in: "com.apple.TextEdit") == .korean)
        #expect(read("gst", in: slack) == .english)
        #expect(read("gst", in: "com.jetbrains.intellij") == .japanese)
        // The app's own entry is for the keys as a whole: no particle split from it.
        #expect(engine.decide("gstdp", context: DecisionContext(app: slack)).output != "gst에")
        // Added again with the same keys and apps: changed, not a second entry.
        engine.add(.init(keys: "gst", language: .english, source: .user))
        #expect(read("gst", in: nil) == .english)
        #expect(engine.dictionary.entries.count == 4)

        // Pinned: the learned word is the user's, and corrections leave it alone.
        let id = try #require(engine.dictionary.languageEntry(for: "dlrjs")?.id)
        engine.pin(id)
        #expect(engine.dictionary.languageEntry(for: "dlrjs")?.source == .user)
        engine.learn("dlrjs", as: .english, engineChoice: .korean)
        #expect(engine.learned["dlrjs"] == .korean)
        // forgetAll keeps it; remove takes it.
        engine.forgetAll()
        #expect(engine.learned["dlrjs"] == .korean)
        engine.remove(id)
        #expect(engine.learned["dlrjs"] == nil)
    }

    /// The input method tells an open settings window when it saved a change, and only then.
    @Test func savedIsCalledAfterWrites() throws {
        final class Count: @unchecked Sendable { var value = 0 }
        let count = Count()
        let engine = try engine([])
        engine.saved = { count.value += 1 }
        engine.learn("so", as: .korean, engineChoice: .english)
        #expect(count.value == 1)
        engine.learn("so", as: .korean, engineChoice: .english)
        engine.reload()
        engine.used("xx", as: .korean)   // nothing to change: nothing written
        #expect(count.value == 2)
    }
}
