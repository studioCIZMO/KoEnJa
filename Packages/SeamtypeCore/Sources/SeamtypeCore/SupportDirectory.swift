import Foundation

/// `~/Library/Application Support/Araso`: settings, per-app rules, learned words and
/// AzooKey's kanji learning. `ARASO_SUPPORT_DIR` points a development copy (the settings
/// window, for screenshots) at another folder.
public enum SupportDirectory {
    /// Set by a host that knows the folder itself (the Windows input method's, `%APPDATA%\Araso`).
    nonisolated(unsafe) public static var override: URL?

    public static var url: URL? {
        if let override { return override }
        if let path = ProcessInfo.processInfo.environment["ARASO_SUPPORT_DIR"], !path.isEmpty {
            return URL(fileURLWithPath: path, isDirectory: true)
        }
        guard let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else {
            return nil
        }
        _ = movedOnce
        return base.appendingPathComponent("Araso", isDirectory: true)
    }

    /// Once per process, before anything reads the folder. Only in the app (the input method and
    /// its settings window): the tests and araso-cli read the folder too, and moving it from
    /// under a Legato or Seamtype still installed would leave that one without its settings.
    private static let movedOnce: Void = {
        // Nor in a build's self-test, which runs the app on a machine that may have Legato.
        if Bundle.main.bundleIdentifier == "com.nyttastudio.inputmethod.Araso",
           !CommandLine.arguments.contains("--selftest"),
           let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first {
            movePredecessor(in: base)
        }
    }()

    /// Legato (0.2.0) and Seamtype (before it) kept everything in `Application Support/<name>`:
    /// the first read after the update moves the newest one there, settings, dictionary and kanji
    /// learning alike. Left alone when `Araso` exists already, so nothing the user has under the
    /// new name is written over; an older one left beside it stays where it is.
    static func movePredecessor(in base: URL) {
        let new = base.appendingPathComponent("Araso", isDirectory: true)
        let files = FileManager.default
        guard !files.fileExists(atPath: new.path) else { return }
        for predecessor in Predecessor.all {
            let old = base.appendingPathComponent(predecessor.supportFolder, isDirectory: true)
            if files.fileExists(atPath: old.path) {
                try? files.moveItem(at: old, to: new)
                return
            }
        }
    }

    /// Moves a file that can't be read (hand-edited, or from a newer version) to
    /// `<name>.unreadable`, so the next save doesn't write over what the user had.
    public static func setAside(_ url: URL) {
        let aside = url.appendingPathExtension("unreadable")
        try? FileManager.default.removeItem(at: aside)
        try? FileManager.default.moveItem(at: url, to: aside)
    }
}
