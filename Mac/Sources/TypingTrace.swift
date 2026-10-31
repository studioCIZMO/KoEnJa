import SeamtypeCore
import Foundation

/// How a word was decided, sent from the input method to the settings window's typing test.
/// Only for text typed into Araso's own window: what's typed anywhere else never leaves the
/// input method (distributed notifications can be seen by other processes).
struct TypingTrace: Equatable {
    static let name = Notification.Name("com.cizmo.inputmethod.KoEnJa.typingTrace")

    let keys: String
    let previous: String?
    let language: String
    let confidence: Double
    let output: String
    let explanation: String
    let partial: Bool

    /// Per-language scores (average log-probability per keystroke) when the heuristic decided:
    /// "ja=-2.41 en=-3.61 ko=-3.73 margin=+1.20".
    var scores: [(language: String, score: Double)] {
        // Only the heuristic's own comparison ("… margin=…"); a split's note (word en=…) isn't one.
        guard explanation.contains("margin=") else { return [] }
        return explanation.split(separator: " ").compactMap { part in
            let pair = part.split(separator: "=", maxSplits: 1)
            guard pair.count == 2, ["en", "ko", "ja"].contains(pair[0]),
                  let value = Double(pair[1].trimmingCharacters(in: CharacterSet(charactersIn: "+(),;"))) else { return nil }
            return (String(pair[0]), value)
        }
    }

    init(_ trace: InputSession.Trace) {
        keys = trace.keys
        previous = trace.previous?.rawValue
        language = trace.decision.language.rawValue
        confidence = trace.decision.confidence
        output = trace.decision.output
        explanation = trace.decision.explanation
        partial = trace.partial
    }

    init?(_ info: [AnyHashable: Any]?) {
        guard let info, let keys = info["keys"] as? String, let language = info["language"] as? String,
              let confidence = info["confidence"] as? Double, let output = info["output"] as? String,
              let explanation = info["explanation"] as? String, let partial = info["partial"] as? Bool else { return nil }
        self.keys = keys
        previous = info["previous"] as? String
        self.language = language
        self.confidence = confidence
        self.output = output
        self.explanation = explanation
        self.partial = partial
    }

    func post() {
        var info: [String: Any] = ["keys": keys, "language": language, "confidence": confidence, "output": output,
                                   "explanation": explanation, "partial": partial]
        if let previous { info["previous"] = previous }
        DistributedNotificationCenter.default().postNotificationName(Self.name, object: nil, userInfo: info,
                                                                     deliverImmediately: true)
    }
}
