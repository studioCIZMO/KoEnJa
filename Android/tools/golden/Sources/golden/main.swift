import Foundation
import SeamtypeCore

/// The text field: everything before the cursor, the marked part at its end.
final class Field: TextClient {
    var text = "" as NSString
    var marked = NSRange(location: NSNotFound, length: 0)
    private var end: Int { text.length }
    func setMarkedText(_ s: String) { setMarkedText(s, replacing: NSRange(location: end, length: 0)) }
    func setMarkedText(_ s: String, replacing range: NSRange) {
        let start = marked.location == NSNotFound ? range.location : min(range.location, marked.location)
        text = text.replacingCharacters(in: NSRange(location: start, length: end - start), with: s) as NSString
        marked = s.isEmpty ? NSRange(location: NSNotFound, length: 0) : NSRange(location: start, length: (s as NSString).length)
    }
    func insertText(_ s: String, replacing range: NSRange?) {
        let r = range ?? (marked.location == NSNotFound ? NSRange(location: end, length: 0) : marked)
        let start = marked.location == NSNotFound ? r.location : min(r.location, marked.location)
        text = text.replacingCharacters(in: NSRange(location: start, length: end - start), with: s) as NSString
        marked = NSRange(location: NSNotFound, length: 0)
    }
    func selectedRange() -> NSRange { NSRange(location: end, length: 0) }
    func markedRange() -> NSRange { marked }
    func text(in range: NSRange) -> String? {
        range.location >= 0 && NSMaxRange(range) <= end ? text.substring(with: range) : nil
    }
}

/// Order → offsets, as iOS LanguagePreferences (rank step 0.3, relative to English).
@MainActor func session(order: [Language]) -> InputSession {
    func bonus(_ l: Language) -> Double? { order.firstIndex(of: l).map { -Double($0) * 0.3 } }
    let japanese = order.contains(.japanese) ? JapaneseModel.shared : nil
    let heuristic = HeuristicEngine(korean: KoreanModel(), japanese: japanese)
    heuristic.koreanOffset = (bonus(.korean) ?? -10) - bonus(.english)!
    heuristic.japaneseOffset += (bonus(.japanese) ?? 0) - bonus(.english)!
    let engine = DictionaryEngine(base: SplittingEngine(base: heuristic, english: heuristic.english,
                                                        korean: heuristic.korean, japanese: heuristic.segmenter),
                                  english: heuristic.english, korean: heuristic.korean,
                                  japaneseParticles: heuristic.segmenter != nil, storeURL: nil)
    let s = InputSession(engine: engine, japaneseEnabled: japanese != nil)
    s.activate(app: nil)
    return s
}

// golden <input> <output>: one key sequence per line ("⏎" Return, "⌫" Delete, "※" switch key);
// a line "#reset" starts a new field. Output: config \t keys \t text added.
let args = CommandLine.arguments
let lines = try! String(contentsOfFile: args[1], encoding: .utf8).split(separator: "\n", omittingEmptySubsequences: false)
var out = ""
MainActor.assumeIsolated {
    for (name, order) in [("ko-en-ja", [Language.korean, .english, .japanese]), ("ko-ja-en", [.korean, .japanese, .english])] {
        var s = session(order: order)
        var field = Field()
        for line in lines where !line.isEmpty {
            if line == "#reset" { s = session(order: order); field = Field(); continue }
            let before = field.text.length
            for c in line {
                let key: InputSession.Key = switch c {
                case "⏎": .enter
                case "⌫": .delete
                case "※": .cycle
                default: c.isLetter ? .letter(c) : .printable(c)
                }
                if !s.handle(key, client: field) {
                    switch key {
                    case .enter: field.insertText("\n", replacing: nil)
                    case .delete: if field.text.length > 0 { field.text = field.text.substring(to: field.text.length - 1) as NSString }
                    case .printable(let p), .letter(let p): field.insertText(String(p), replacing: nil)
                    default: break
                    }
                }
            }
            s.commit(client: field)
            let added = field.text.length >= before ? field.text.substring(from: before) : "(deleted)"
            out += "\(name)\t\(line)\t\(added.replacingOccurrences(of: "\n", with: "⏎"))\n"
        }
    }
}
try! out.write(toFile: args[2], atomically: true, encoding: .utf8)
