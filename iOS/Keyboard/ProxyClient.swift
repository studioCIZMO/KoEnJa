import SeamtypeCore
import UIKit

/// SeamtypeCore's `TextClient` on top of iOS's `UITextDocumentProxy`.
///
/// The word being composed goes in as ordinary text, not marked text: web forms drop marked
/// text when the focus moves to the next field (a name typed in Safari vanished), and the
/// keyboard only hears about it afterwards. Each change deletes the end of the word that
/// differs and types the new end, like the system's Korean keyboard does.
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
    /// The word being composed, already in the document before the cursor.
    private(set) var marked = ""

    init(proxy: UITextDocumentProxy) { self.proxy = proxy }

    /// Committed text before the cursor.
    private var committed: String {
        let before = proxy.documentContextBeforeInput ?? ""
        return !marked.isEmpty && before.hasSuffix(marked) ? String(before.dropLast(marked.count)) : before
    }

    /// The proxy's view of the text before the cursor, for the trace.
    private var context: String { "ctx=[\(proxy.documentContextBeforeInput ?? "nil")] marked=[\(marked)]" }

    func setMarkedText(_ text: String) {
        TraceLog.write("setMarked [\(text)] \(context)")
        replaceMarked(with: text, typingOn: true)
        marked = text
    }

    func setMarkedText(_ text: String, replacing range: NSRange) {
        TraceLog.write("setMarked [\(text)] replacing \(range) \(context)")
        replaceMarked(with: "")
        deleteBack(range)
        setMarkedText(text)
    }

    func insertText(_ text: String, replacing range: NSRange?) {
        TraceLog.write("insert [\(text)] replacing \(range.map { "\($0)" } ?? "nil") \(context)")
        defer { TraceLog.write("  after \(context)") }
        if let range {
            replaceMarked(with: "")
            deleteBack(range)
            proxy.insertText(text)
        } else {
            replaceMarked(with: text) // the word turns into the committed text
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

    /// After the app says the text or selection changed: whether the word being composed is
    /// no longer right before the cursor (the user moved it, an undo, autofill). Then it's
    /// forgotten as typed, so the session can start afresh without deleting anything.
    /// ponytail: a suffix check, not azooKey's log of expected edits; the cursor moved to
    /// right after the same text again isn't noticed.
    func resetIfMovedAway() -> Bool {
        guard !marked.isEmpty, let before = proxy.documentContextBeforeInput, !before.hasSuffix(marked) else { return false }
        TraceLog.write("moved away from [\(marked)] \(context)")
        marked = ""
        return true
    }

    /// Turns the word on screen into `text`, touching only the end that differs (거 → 건: one
    /// delete, one insert). If the text before the cursor no longer ends with the word (the
    /// cursor moved, another field), the word is left alone; `text` is still typed if the user
    /// is typing on, but not for a commit (the keyboard closing after the focus moved).
    private func replaceMarked(with text: String, typingOn: Bool = false) {
        defer { marked = "" }
        guard !marked.isEmpty else {
            if !text.isEmpty { proxy.insertText(text) }
            return
        }
        if let before = proxy.documentContextBeforeInput, !before.hasSuffix(marked) {
            TraceLog.write("  stale marked [\(marked)] \(context)")
            if typingOn, !text.isEmpty { proxy.insertText(text) }
            return
        }
        let same = zip(marked, text).prefix { $0 == $1 }.count
        for _ in 0..<(marked.count - same) { proxy.deleteBackward() }
        let rest = String(text.dropFirst(same))
        if !rest.isEmpty { proxy.insertText(rest) }
    }

    /// deleteBackward removes one character (grapheme) at a time.
    private func deleteBack(_ range: NSRange) {
        guard let text = text(in: range) else { return TraceLog.write("  deleteBack \(range) NO TEXT \(context)") }
        TraceLog.write("  deleteBack \(text.count) [\(text)]")
        for _ in 0..<text.count { proxy.deleteBackward() }
    }
}
