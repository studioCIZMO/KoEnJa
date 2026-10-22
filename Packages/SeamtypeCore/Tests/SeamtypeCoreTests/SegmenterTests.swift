import Foundation
import Testing
@testable import SeamtypeCore

@Suite struct SegmenterTests {
    let segmenter = JapaneseSegmenter(english: sharedEngine.english, japanese: JapaneseModel.shared!)

    @Test(arguments: [
        ("kyouhagoogledekensaku", "きょうはgoogleでけんさく"),
        ("meetingwoshimashou", "meetingをしましょう"),
        ("serverniloginshita", "serverにloginした"),
    ])
    func splitsEnglishOutOfJapanese(keys: String, text: String) throws {
        let segments = try #require(segmenter.segment(keys))
        #expect(JapaneseSegmenter.text(segments) == text)
    }

    @Test(arguments: ["konnichiha", "wouldn", "mumbai", "hello", "brooklyn"])
    func leavesOtherInputAlone(keys: String) {
        #expect(segmenter.segment(keys) == nil)
    }

    @Test func decisionUsesSegmentation() {
        let d = trilingual.decide("kyouhagoogledekensaku", context: DecisionContext())
        #expect(d.language == .japanese)
        #expect(d.output == "きょうはgoogleでけんさく")
    }
}

@MainActor @Suite struct ConverterTests {
    @Test func dictionaryConvertsAndReranks() throws {
        let memory = FileManager.default.temporaryDirectory.appendingPathComponent("araso-conv-\(UUID().uuidString)")
        let converter = try #require(AzooKeyConverter(memory: memory))
        #expect(converter.candidates(for: "へんかん").first?.text == "変換")
        // AzooKey alone ranks 強は… first; the text model fixes it.
        #expect(converter.candidates(for: "きょうはいいてんきですね").first?.text == "今日はいい天気ですね")
        #expect(converter.candidates(for: "きょうはgoogleでけんさく").first?.text == "今日はgoogleで検索")
    }
}
