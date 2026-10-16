import Foundation

/// A masked password field that still reaches the input method (KakaoTalk's login, App Store's
/// password sheet). Asked to show marked text while handling a key, KakaoTalk's types that key
/// itself (lowercase when ⇧ was already down) and drops what the input method inserts; App
/// Store's loses keys. Its keys go to the app as typed, as when paused.
public enum MaskedField {
    /// Whether the focused field is one: Secure Event Input is on, held by the frontmost app,
    /// and the field reports no caret.
    /// - The frontmost app, not the client's: App Store's sheet is a PassKit extension while
    ///   App Store holds Secure Event Input. An app behind the one being typed in doesn't count.
    /// - A terminal with Secure Keyboard Entry holds it too, but reports its caret.
    /// - `owner` is `kCGSSessionSecureInputPID`, `frontmost` the frontmost app's process.
    public static func passesKeys(secureInput: Bool, owner: Int32?, frontmost: Int32?, caret: NSRange) -> Bool {
        guard secureInput, caret.location == NSNotFound, let owner else { return false }
        return owner == frontmost
    }
}
