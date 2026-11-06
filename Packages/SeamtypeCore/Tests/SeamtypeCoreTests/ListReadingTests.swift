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
}
