import SeamtypeCore
import UIKit

/// SeamtypeCore's `TextClient` on top of iOS's `UITextDocumentProxy`.
///
/// A keyboard extension can't address the document by absolute position, so ranges are
/// measured in the text before the cursor that the proxy shares (UTF-16, like NSString).
/// InputSession only ever replaces ranges that end at the cursor (the digits before a phrase,
/// the word committed just before), and those are deleted back from the cursor.
/// ponytail: a range that doesn't end at the cursor is treated as if it did; move the cursor
/// with adjustTextPosition if the session ever starts using one.
@MainActor
final class ProxyClient: TextClient {
    let proxy: UITextDocumentProxy
    /// The marked text we set; the proxy doesn't tell us.
    private(set) var marked = ""

    init(proxy: UITextDocumentProxy) { self.proxy = proxy }

    /// Committed text before the cursor (the proxy may or may not include marked text in its context).
    private var committed: String {
        let before = proxy.documentContextBeforeInput ?? ""
        return !marked.isEmpty && before.hasSuffix(marked) ? String(before.dropLast(marked.count)) : before
    }

    /// The proxy's view of the text before the cursor, for the trace.
    private var context: String { "ctx=[\(proxy.documentContextBeforeInput ?? "nil")] marked=[\(marked)]" }

    func setMarkedText(_ text: String) {
        TraceLog.write("setMarked [\(text)] \(context)")
        marked = text
        proxy.setMarkedText(text, selectedRange: NSRange(location: (text as NSString).length, length: 0))
        if text.isEmpty { proxy.unmarkText() }
    }

    func setMarkedText(_ text: String, replacing range: NSRange) {
        TraceLog.write("setMarked [\(text)] replacing \(range) \(context)")
        clearMarked()
        deleteBack(range)
        setMarkedText(text)
    }

    func insertText(_ text: String, replacing range: NSRange?) {
        TraceLog.write("insert [\(text)] replacing \(range.map { "\($0)" } ?? "nil") \(context)")
        defer { TraceLog.write("  after \(context)") }
        if let range {
            clearMarked()
            deleteBack(range)
            proxy.insertText(text)
        } else if !marked.isEmpty {
            // Marked text turns into the committed text.
            proxy.setMarkedText(text, selectedRange: NSRange(location: (text as NSString).length, length: 0))
            proxy.unmarkText()
            marked = ""
        } else {
            proxy.insertText(text)
        }
    }

    func selectedRange() -> NSRange {
        NSRange(location: (committed as NSString).length + (marked as NSString).length, length: 0)
    }

    func markedRange() -> NSRange {
        marked.isEmpty ? NSRange(location: NSNotFound, length: 0)
            : NSRange(location: (committed as NSString).length, length: (marked as NSString).length)
    }

    func text(in range: NSRange) -> String? {
        let all = (committed + marked) as NSString
        guard range.location != NSNotFound, NSMaxRange(range) <= all.length else { return nil }
        return all.substring(with: range)
    }

    private func clearMarked() {
        guard !marked.isEmpty else { return }
        marked = ""
        proxy.setMarkedText("", selectedRange: NSRange(location: 0, length: 0))
        proxy.unmarkText()
    }

    /// deleteBackward removes one character (grapheme) at a time.
    private func deleteBack(_ range: NSRange) {
        guard let text = text(in: range) else { return TraceLog.write("  deleteBack \(range) NO TEXT \(context)") }
        TraceLog.write("  deleteBack \(text.count) [\(text)]")
        for _ in 0..<text.count { proxy.deleteBackward() }
    }
}
