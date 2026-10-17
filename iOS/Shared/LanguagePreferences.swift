import Foundation

/// The languages the user types, most used first. Set in the app, read by the keyboard through
/// the shared App Group. English is always on: the layout's own letters.
///
/// Rank becomes a score bonus in the decision engine, so an ambiguous word (go = 해) goes to the
/// language that comes first. Leaving Japanese out also skips loading its model and dictionary.
struct LanguagePreferences: Equatable {
    enum Language: String, CaseIterable, Identifiable {
        case korean, japanese, english
        var id: String { rawValue }
        var name: String { ["korean": "한국어", "japanese": "日本語", "english": "English"][rawValue]! }
        var mark: String { ["korean": "한", "japanese": "あ", "english": "A"][rawValue]! }
    }

    /// Enabled languages, first = most used. Always contains English.
    var order: [Language]

    /// The usual setups: who you talk to decides the pair.
    static let presets: [(title: String, order: [Language])] = [
        ("한국어 + 영어", [.korean, .english]),
        ("日本語 + English", [.japanese, .english]),
        ("한국어 > 일본어 > 영어", [.korean, .japanese, .english]),
        ("日本語 > 한국어 > English", [.japanese, .korean, .english]),
    ]

    /// Score per rank step (average log-probability per key, the engine's scale; its context
    /// bias for the previous word's language is 0.8).
    /// ponytail: hand-picked; tune on held-out mixed text once there is some.
    static let rankStep = 0.3

    static let defaults = UserDefaults(suiteName: "group.com.netmaru.rea")
    private static let key = "languages"

    static var isConfigured: Bool { defaults?.array(forKey: key) != nil }

    static func load() -> Self {
        let saved = (defaults?.stringArray(forKey: key) ?? []).compactMap(Language.init(rawValue:))
        var order = saved.isEmpty ? [.korean, .english, .japanese] : saved
        if !order.contains(.english) { order.append(.english) }
        return Self(order: order)
    }

    func save() { Self.defaults?.set(order.map(\.rawValue), forKey: Self.key) }

    var japanese: Bool { order.contains(.japanese) }

    private func bonus(_ language: Language) -> Double? {
        order.firstIndex(of: language).map { -Double($0) * Self.rankStep }
    }

    /// For HeuristicEngine.koreanOffset (relative to English, the engine's baseline).
    var koreanOffset: Double { (bonus(.korean) ?? -10) - bonus(.english)! }
    /// Added to HeuristicEngine.japaneseOffset.
    var japaneseOffsetDelta: Double { (bonus(.japanese) ?? 0) - bonus(.english)! }

    /// The switch key's label: the enabled languages in order (한あA, あA…).
    var switchLabel: String { order.map(\.mark).joined() }
}

/// Key click sound and haptic tap, set in the app (on by default, like the system keyboard's sound).
enum KeyFeedback {
    static let soundKey = "keySound", hapticsKey = "keyHaptics"
    static var sound: Bool { LanguagePreferences.defaults?.object(forKey: soundKey) as? Bool ?? true }
    static var haptics: Bool { LanguagePreferences.defaults?.object(forKey: hapticsKey) as? Bool ?? true }
}
