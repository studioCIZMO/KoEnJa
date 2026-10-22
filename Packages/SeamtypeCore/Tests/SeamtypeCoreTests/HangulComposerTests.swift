import Testing
@testable import SeamtypeCore

@Suite struct HangulComposerTests {
    @Test(arguments: [
        ("dkssudgktpdy", "안녕하세요"),
        ("gksrmf", "한글"),
        ("rkqt", "값"),
        ("rkqtdl", "값이"),
        ("dlTek", "있다"),
        ("dhkswjsgl", "완전히"),
        ("ehlwl", "되지"),
        ("dmlwk", "의자"),
        ("ekfrdl", "닭이"),
        ("rhkswls", "관진"),
        ("Qkffl", "빨리"),
        ("dkQk", "아빠"),
        ("zzz", "ㅋㅋㅋ"),
        ("rk", "가"),
        ("r", "ㄱ"),
        ("k", "ㅏ"),
    ])
    func composes(keys: String, expected: String) {
        #expect(HangulComposer.compose(keys: keys)?.text == expected)
    }

    @Test func doubleConsonantThatCannotBeFinalStartsNewSyllable() {
        // 아 + ㄸ: ㄸ can't be 받침, so it waits as the next initial.
        let result = HangulComposer.compose(keys: "dkE")!
        #expect(result.text == "아ㄸ")
        #expect(result.endsWithPendingConsonant)
    }

    @Test func englishProducesStrayJamo() {
        let result = HangulComposer.compose(keys: "hello")!
        #expect(!result.strayJamo.isEmpty)
    }

    @Test func rejectsNonLetters() {
        #expect(HangulComposer.compose(keys: "ab1") == nil)
    }
}
