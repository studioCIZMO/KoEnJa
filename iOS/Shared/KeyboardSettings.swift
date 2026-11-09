import UIKit

/// The keyboard's shape and color, set in the app (Android's KeyShape, same keys and ranges):
/// key height (× the system's), space under the keys, the gaps, corner radius, shift/delete width
/// and a hue for the keys (nil: the system keyboard's grays).
/// ponytail: no curved rows (Android's kbArc); add with a manual layout if asked for.
struct KeyShape: Equatable {
    var height = 1.0, bottom = 0.0, gap = 6.0, rowGap = 54 - 127 / 3.0, radius = 6.0, sideKeys = 1.3
    var hue: Double?

    /// Settings key, (min, max, step), its path, in the order the app lists them.
    static let settings: [(key: String, range: (Double, Double, Double), path: WritableKeyPath<Self, Double>)] = [
        ("kbHeight", (0.8, 1.3, 0.05), \.height), ("kbBottom", (0, 48, 2), \.bottom),
        ("kbGap", (2, 12, 1), \.gap), ("kbRowGap", (4, 20, 1), \.rowGap),
        ("kbRadius", (0, 20, 1), \.radius), ("kbSideKeys", (1, 1.5, 0.05), \.sideKeys),
    ]
    static let hueKey = "kbHue"

    static func load() -> Self {
        let d = LanguagePreferences.defaults
        var shape = Self()
        for s in settings {
            if let v = d?.object(forKey: s.key) as? Double { shape[keyPath: s.path] = min(max(v, s.range.0), s.range.1) }
        }
        if let h = d?.object(forKey: hueKey) as? Double, h >= 0 { shape.hue = min(h, 360) }
        return shape
    }

    static func reset() {
        for s in settings { LanguagePreferences.defaults?.removeObject(forKey: s.key) }
        LanguagePreferences.defaults?.removeObject(forKey: hueKey)
    }

    /// Keys, pressed function keys and the keyboard behind them, tinted by `hue` (Android's values).
    func colors() -> (key: UIColor, pressed: UIColor, background: UIColor?) {
        guard let hue else {
            return (UIColor { $0.userInterfaceStyle == .dark ? UIColor(white: 0.42, alpha: 1) : .white },
                    UIColor { $0.userInterfaceStyle == .dark ? UIColor(white: 0.27, alpha: 1) : UIColor(red: 0.67, green: 0.69, blue: 0.73, alpha: 1) },
                    nil)
        }
        let h = hue / 360
        func hsb(_ s: Double, _ light: Double, _ dark: Double) -> UIColor {
            UIColor { UIColor(hue: h, saturation: s, brightness: $0.userInterfaceStyle == .dark ? dark : light, alpha: 1) }
        }
        return (hsb(0.08, 1, 0.38), hsb(0.2, 0.74, 0.26), hsb(0.2, 0.86, 0.18))
    }
}

/// Where this person touches each key, learned from the nearby-key fixes they pick (Android's
/// TouchCalibration): a fix that swapped key t for its neighbor n means they meant n where they
/// touched, a sample for n relative to its center. After `minSamples`, n's touch area moves by
/// their average, at most `maxShift` of the key's size each way (KeyStack.hitTest).
/// In the App Group ("touchCalibration": "key:count:sumX:sumY;…", in pt).
final class TouchCalibration {
    static let key = "touchCalibration", minSamples = 8.0, maxShift = 0.3
    private var samples: [Character: (n: Double, x: Double, y: Double)] = [:]

    init() {
        for entry in (LanguagePreferences.defaults?.string(forKey: Self.key) ?? "").split(separator: ";") {
            let p = entry.split(separator: ":")
            guard p.count == 4, p[0].count == 1, let n = Double(p[1]), let x = Double(p[2]), let y = Double(p[3]) else { continue }
            samples[p[0].first!] = (n, x, y)
        }
    }

    func add(_ key: Character, dx: Double, dy: Double) {
        var s = samples[key] ?? (0, 0, 0)
        if s.n >= 200 { s = (s.n / 2, s.x / 2, s.y / 2) } // recent touches count more, and it can't grow without end
        samples[key] = (s.n + 1, s.x + dx, s.y + dy)
        LanguagePreferences.defaults?.set(samples.map { "\($0.key):\($0.value.n):\($0.value.x):\($0.value.y)" }.joined(separator: ";"), forKey: Self.key)
    }

    /// The learned shift of `key`, for a key `size` big.
    func offset(_ key: Character, size: CGSize) -> CGVector? {
        guard let s = samples[key], s.n >= Self.minSamples else { return nil }
        func clamp(_ v: Double, _ side: CGFloat) -> CGFloat { CGFloat(min(max(v, -side * Self.maxShift), side * Self.maxShift)) }
        return CGVector(dx: clamp(s.x / s.n, size.width), dy: clamp(s.y / s.n, size.height))
    }

    static func clear() { LanguagePreferences.defaults?.removeObject(forKey: key) }
}

/// Text shortcuts ("brb" → "Be right back"), in the order added: lines of keys TAB phrase
/// (Android's Prefs.shortcuts). The phrase comes first in the bar when its keys are typed.
enum Shortcuts {
    static let key = "shortcuts"

    static func load() -> [(keys: String, phrase: String)] {
        (LanguagePreferences.defaults?.string(forKey: key) ?? "").split(separator: "\n").compactMap { line in
            let p = line.split(separator: "\t", maxSplits: 1)
            return p.count == 2 && !p[0].isEmpty ? (String(p[0]), String(p[1])) : nil
        }
    }

    static func save(_ list: [(keys: String, phrase: String)]) {
        LanguagePreferences.defaults?.set(list.map { "\($0.keys)\t\($0.phrase.replacingOccurrences(of: "\n", with: " "))" }
            .joined(separator: "\n"), forKey: key)
    }

    /// Keys a shortcut can have: letters a–z, up to 32 (what the keyboard's letter keys type).
    static func isValid(_ keys: String) -> Bool { (1...32).contains(keys.count) && keys.allSatisfy { ("a"..."z").contains($0) } }
}
