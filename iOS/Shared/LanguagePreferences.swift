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
        /// In the app's language (App/*.lproj).
        var name: String { NSLocalizedString(["korean": "한국어", "japanese": "일본어", "english": "영어"][rawValue]!, comment: "") }
        var mark: String { ["korean": "한", "japanese": "あ", "english": "A"][rawValue]! }
    }

    /// Enabled languages, first = most used. Always contains English.
    var order: [Language]

    /// Seamtype's input options (its settings.json keys and defaults), applied to the keyboard.
    /// Live conversion is off by default here: the bar shows the kanji candidates instead.
    var returnConfirmsOnly = true
    var fullWidthDigits = false
    var fullWidthSpace = false
    var liveConversion = false
    /// The keyboard's own helpers, each work on the main thread between keys; off = not computed
    /// at all (the spell checker isn't even loaded when suggestions and autocorrect are both off).
    var suggestions = true
    var autocorrect = true
    var touchCorrection = true
    var nextWords = true
    /// Kej's typing help: words a neighbor key would have made (offered, never put in), learning
    /// where each key is touched from the ones picked, and a swipe up on a letter for its symbol.
    var nearbyKeys = true
    var adaptToTouch = true
    var swipeSymbols = true
    private static let options: [(String, WritableKeyPath<Self, Bool>)] = [
        ("returnConfirmsOnly", \.returnConfirmsOnly), ("fullWidthDigits", \.fullWidthDigits),
        ("fullWidthSpace", \.fullWidthSpace), ("liveConversion", \.liveConversion),
        ("suggestions", \.suggestions), ("autocorrect", \.autocorrect),
        ("touchCorrection", \.touchCorrection), ("nextWords", \.nextWords),
        ("nearbyKeys", \.nearbyKeys), ("adaptToTouch", \.adaptToTouch), ("swipeSymbols", \.swipeSymbols),
    ]

    /// The usual setups: who you talk to decides the pair.
    static var presets: [(title: String, order: [Language])] {
        [[.korean, .english], [.japanese, .english], [.korean, .japanese, .english], [.japanese, .korean, .english]]
            .map { order in (order.map(\.name).joined(separator: order.count == 2 ? " + " : " > "), order) }
    }

    /// Score per rank step (average log-probability per key, the engine's scale; its context
    /// bias for the previous word's language is 0.8).
    /// ponytail: hand-picked; tune on held-out mixed text once there is some.
    static let rankStep = 0.3

    static let defaults = UserDefaults(suiteName: "group.com.cizmo.koenja")
    private static let key = "languages"

    static var isConfigured: Bool { defaults?.array(forKey: key) != nil }

    static func load() -> Self {
        let saved = (defaults?.stringArray(forKey: key) ?? []).compactMap(Language.init(rawValue:))
        var order = saved.isEmpty ? [.korean, .english, .japanese] : saved
        if !order.contains(.english) { order.append(.english) }
        var preferences = Self(order: order)
        for (key, path) in options {
            if let value = defaults?.object(forKey: key) as? Bool { preferences[keyPath: path] = value }
        }
        return preferences
    }

    func save() {
        Self.defaults?.set(order.map(\.rawValue), forKey: Self.key)
        for (key, path) in Self.options { Self.defaults?.set(self[keyPath: path], forKey: key) }
    }

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
