import Foundation
import Testing
@testable import SeamtypeCore

/// REA: the word and trigram lists are read in place (`Resources.forEachPair`), as the split text was:
/// CRLF and CR end lines, empty tab fields are skipped, a third field drops the line.
@Suite struct ListReadingTests {
    @Test func pairs() {
        var got: [String] = []
        Resources.forEachPair(in: Data("cat\t10\r\ndog\t\t20\rbad\t1\t2\n\nowl\t30".utf8)) { got.append("\($0)=\($1)") }
        #expect(got == ["cat=10", "dog=20", "owl=30"])
    }

    @Test func englishList() throws {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("english-\(UUID()).txt")
        try Data("cat\t10\r\ndon't\t20\r\n".utf8).write(to: url)
        defer { try? FileManager.default.removeItem(at: url) }
        let model = EnglishModel(listURL: url, extraWords: ["cat", "zzyzx"])
        #expect(model.level("cat") == 10)
        #expect(model.level("dont") == 20)
        #expect(model.level("zzyzx") == EnglishModel.extraLevel)
        #expect(model.level("cats") == 10)
    }

    @Test func englishListAsUpstreamRead() throws {
        let dir = FileManager.default.temporaryDirectory
        let twice = dir.appendingPathComponent("twice-\(UUID()).txt"), bad = dir.appendingPathComponent("bad-\(UUID()).txt")
        try Data("cat\t10\ncat\t60\ndon't\t40\ndont\t30\n".utf8).write(to: twice)
        try Data("cat\t10\n".utf8 + [0xFF, 10]).write(to: bad)
        defer { try? FileManager.default.removeItem(at: twice); try? FileManager.default.removeItem(at: bad) }
        let model = EnglishModel(listURL: twice, extraWords: [])
        #expect(model.level("cat") == 60) // the later line
        #expect(model.level("dont") == 30) // the lower of two spellings
        #expect(EnglishModel(listURL: bad, extraWords: []).level("cat") == nil)
        #expect(EnglishModel(levels: ["cat": -1]).level("cat") == -1)
    }
}
