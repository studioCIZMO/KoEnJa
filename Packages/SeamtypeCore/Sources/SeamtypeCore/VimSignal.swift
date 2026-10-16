import Foundation

/// What vim reports through `Seamtype --vim <mode>` on each mode change (scripts/vim).
public enum VimSignal: String, Sendable {
    /// Insert (or replace) mode: words are decided.
    case insert
    /// Normal, visual or command-line mode: keys go straight to vim.
    case normal
    /// Vim quit, was suspended or lost focus: whatever is in front now isn't vim.
    case off

    /// The distributed notification carrying the mode as its object, and in its user info under
    /// `appKey` the bundle identifier of the app vim runs in (`$__CFBundleIdentifier`).
    public static let notification = Notification.Name("com.aimdevlee.inputmethod.Seamtype.vimMode")
    public static let appKey = "app"
}
