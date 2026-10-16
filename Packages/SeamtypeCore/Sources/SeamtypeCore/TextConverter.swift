/// Converts a whole line of QWERTY text word by word, carrying language context
/// across words exactly as the input method does while typing.
public struct TextConverter: Sendable {
    public let engine: DecisionEngine

    public init(engine: DecisionEngine) {
        self.engine = engine
    }

    public struct Word: Sendable {
        public let keys: String
        public let decision: Decision
    }

    public func convert(_ text: String) -> (output: String, words: [Word]) {
        var output = ""
        var words: [Word] = []
        var token = ""
        var previous: Language?

        func flushToken() {
            guard !token.isEmpty else { return }
            let decision = engine.decide(token, context: DecisionContext(previousLanguage: previous))
            output += decision.output
            words.append(Word(keys: token, decision: decision))
            previous = decision.language.sentenceLanguage
            token = ""
        }

        for ch in text {
            if ch.isASCII && ch.isLetter {
                token.append(ch)
            } else {
                flushToken()
                output.append(ch)
                if ch.isNewline { previous = nil }
            }
        }
        flushToken()
        return (output, words)
    }
}
