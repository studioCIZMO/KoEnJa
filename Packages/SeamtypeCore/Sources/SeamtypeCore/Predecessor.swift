import Foundation

/// Araso had other names, each under its own bundle identifier: to macOS another input method.
/// Seamtype until 0.2.0, Legato in 0.2.0. What an update from one has to find and clean up: its
/// copies, its installer receipt, its processes and its settings folder (moved by `SupportDirectory`).
public struct Predecessor: Sendable {
    public let name: String
    public let bundleID: String
    public let packageID: String

    public var appName: String { name + ".app" }
    public var supportFolder: String { name }

    /// Newest first: the settings folder is taken from the first one found.
    public static let all = [
        Predecessor(name: "Legato", bundleID: "com.nyttastudio.inputmethod.Legato",
                    packageID: "com.nyttastudio.inputmethod.Legato.pkg"),
        Predecessor(name: "Seamtype", bundleID: "com.aimdevlee.inputmethod.Seamtype",
                    packageID: "com.aimdevlee.inputmethod.Seamtype.pkg"),
    ]

    public static var bundleIDs: [String] { all.map(\.bundleID) }
}
