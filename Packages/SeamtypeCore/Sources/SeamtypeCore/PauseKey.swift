import Foundation

/// The key that pauses Araso in a text field and resumes it (settings.json's `pauseKey`):
///
///     "pauseKey": "rightCommand"                                  // a right modifier tapped alone
///     "pauseKey": { "keyCode": 35, "modifiers": ["control", "option"], "key": "P" }   // ⌃⌥P
///     "pauseKey": "off"
///
/// One key for every app. A key that also switches languages (right ⌥, right ⇧, ⌥Space) pauses
/// instead where an app chose it; the settings window doesn't offer such a pair.
public enum PauseKey: Equatable, Sendable {
    case off
    case tap(Tap)
    case shortcut(Shortcut)

    public static let standard = PauseKey.tap(.rightCommand)

    /// A right-hand modifier pressed and released on its own.
    public enum Tap: String, CaseIterable, Sendable {
        case rightCommand, rightOption, rightShift, rightControl

        /// The virtual key code of the modifier key.
        public var keyCode: UInt16 {
            switch self {
            case .rightCommand: return 54
            case .rightOption: return 61
            case .rightShift: return 60
            case .rightControl: return 62
            }
        }

        public var glyph: String {
            switch self {
            case .rightCommand: return "⌘"
            case .rightOption: return "⌥"
            case .rightShift: return "⇧"
            case .rightControl: return "⌃"
            }
        }

        /// The switch key that is the same key, if any.
        public var switchKey: SwitchKey? {
            switch self {
            case .rightOption: return .rightOption
            case .rightShift: return .rightShift
            case .rightCommand, .rightControl: return nil
            }
        }
    }

    /// A key with modifiers (⌃⌥P), or a function key on its own (F5).
    public struct Shortcut: Codable, Equatable, Sendable {
        public var keyCode: UInt16
        public var modifiers: KeyModifiers
        /// The key as printed on it ("P", "F5", "Space"), for showing the shortcut.
        public var key: String

        public init(keyCode: UInt16, modifiers: KeyModifiers, key: String) {
            self.keyCode = keyCode
            self.modifiers = modifiers
            self.key = key
        }

        /// As in a menu: ⌃⌥⇧⌘ in that order, then the key.
        public var display: String { modifiers.glyphs + key }

        public func matches(keyCode: UInt16, modifiers: KeyModifiers) -> Bool {
            keyCode == self.keyCode && modifiers == self.modifiers
        }

        /// Why it can't pause, or nil when it can. Without ⌘, ⌃ or ⌥ a key types (only a
        /// function key may go alone), and Araso's own keys stay theirs.
        public func problem(switchKeys: Set<SwitchKey>) -> Problem? {
            if modifiers.isDisjoint(with: [.command, .control, .option]) {
                if !Self.functionKeys.contains(keyCode) { return .types }
                if [Self.f6, Self.f7].contains(keyCode) { return .arasos }
            }
            if modifiers == .control, [Self.j, Self.k].contains(keyCode) { return .arasos }
            if modifiers == .option, keyCode == Self.space, switchKeys.contains(.optionSpace) { return .switchKey }
            return nil
        }

        public enum Problem: Equatable, Sendable {
            /// It would type a character.
            case types
            /// ⌃J, ⌃K, F6, F7: hiragana and katakana.
            case arasos
            /// ⌥Space, chosen to switch languages.
            case switchKey
        }

        static let space: UInt16 = 49, j: UInt16 = 38, k: UInt16 = 40, f6: UInt16 = 97, f7: UInt16 = 98
        /// F1–F20's key codes, in order.
        public static let functionKeys: [UInt16] = [122, 120, 99, 118, 96, 97, 98, 100, 101, 109,
                                                       103, 111, 105, 107, 113, 106, 64, 79, 80, 90]
    }

    /// The switch key this pause key takes, so the two are never chosen together.
    public var switchKey: SwitchKey? {
        switch self {
        case .off: return nil
        case .tap(let tap): return tap.switchKey
        case .shortcut(let shortcut):
            return shortcut.modifiers == .option && shortcut.keyCode == Shortcut.space ? .optionSpace : nil
        }
    }
}

extension PauseKey: Codable {
    public init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if let name = try? container.decode(String.self) {
            if name == "off" { self = .off; return }
            guard let tap = Tap(rawValue: name) else {
                throw DecodingError.dataCorruptedError(in: container, debugDescription: "unknown pause key \(name)")
            }
            self = .tap(tap)
        } else {
            self = .shortcut(try container.decode(Shortcut.self))
        }
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        switch self {
        case .off: try container.encode("off")
        case .tap(let tap): try container.encode(tap.rawValue)
        case .shortcut(let shortcut): try container.encode(shortcut)
        }
    }
}

/// The modifiers of a shortcut, written by name in settings.json.
public struct KeyModifiers: OptionSet, Hashable, Sendable {
    public let rawValue: Int
    public init(rawValue: Int) { self.rawValue = rawValue }

    public static let control = KeyModifiers(rawValue: 1 << 0)
    public static let option = KeyModifiers(rawValue: 1 << 1)
    public static let shift = KeyModifiers(rawValue: 1 << 2)
    public static let command = KeyModifiers(rawValue: 1 << 3)

    private static let named: [(KeyModifiers, String, String)] = [
        (.control, "control", "⌃"), (.option, "option", "⌥"), (.shift, "shift", "⇧"), (.command, "command", "⌘"),
    ]

    /// ⌃⌥⇧⌘, as macOS orders them.
    public var glyphs: String { Self.named.filter { contains($0.0) }.map(\.2).joined() }
}

extension KeyModifiers: Codable {
    public init(from decoder: Decoder) throws {
        let names = try decoder.singleValueContainer().decode([String].self)
        self = KeyModifiers(Self.named.filter { names.contains($0.1) }.map(\.0))
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(Self.named.filter { contains($0.0) }.map(\.1))
    }
}
