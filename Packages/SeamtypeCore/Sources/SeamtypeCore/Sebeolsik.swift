/// The layout Korean is typed on: `settings.json`'s `"koreanLayout"`.
///
/// Like `LatinLayout`, it goes by the QWERTY key (the seat), whatever English layout types the
/// letters. Callers don't ask it directly: `HangulComposer.compose(keys:)` and `DubeolsikLayout`'s
/// statics answer for whichever layout is current, so the decision code reads keys the same way
/// for both.
public enum KoreanLayout: String, Codable, CaseIterable, Sendable {
    /// 두벌식 (KS X 5002), with 도깨비불: a final consonant moves to the next syllable.
    case dubeolsik
    /// 세벌식 최종: separate keys for initial, medial and final jamo, on digits and punctuation too.
    case sebeolsikFinal

    /// What the input controllers type with: set from the settings when they load.
    nonisolated(unsafe) public static var active: KoreanLayout = .dubeolsik
    /// For a test (or a tool) that types on another layout without touching `active`.
    @TaskLocal public static var override: KoreanLayout?
    public static var current: KoreanLayout { override ?? active }

    /// Whether a digit or punctuation key, which reaches the session as a symbol, goes on with the
    /// word being typed as Korean instead of ending it. Only 3-set has jamo there (ㅂ on ;, ㅆ on 2,
    /// ㅋ on 0), and only where the jamo makes a syllable people use (`knows`): 갔 (kf2) takes its
    /// 2; 안녕! (ㄲ can't follow 녕's ㅇ), 세요! (욖) and hello! don't. A word starts on one only
    /// with an initial consonant (ㅂ ; ㅌ ' ㅋ 0), and not with 0 right after a digit (100 is a number).
    public func continuesWord(_ keys: String, with key: Character, afterDigit: Bool,
                              knows: (Character) -> Bool) -> Bool {
        guard self == .sebeolsikFinal, !key.isLetter, let jamo = SebeolsikFinal.jamo(onSeat: LatinLayout.current.seat(of: key)) else { return false }
        if keys.isEmpty { return jamo.position == .initial && !(afterDigit && key.isNumber) }
        // As jamo, digits too: 0 is ㅋ, and 09 쿠 is on its way to 퀼프 (a number of them alone
        // still reads as a number when the word is decided).
        guard let before = SebeolsikFinal.composeJamo(keys), !before.isBroken,
              let after = SebeolsikFinal.composeJamo(keys + String(key)), !after.isBroken else { return false }
        // A vowel or final has to join a syllable (가 + ㅆ is 갔, ㅋ + ㅠ is 큐): left on its own, or
        // making one nobody types, it was the symbol.
        if jamo.position == .initial { return true }
        if case .syllable(let s) = after.units.last { return knows(s) }
        return false
    }
}

/// 세벌식 최종 (공병우 최종), as macOS's and libhangul's 3-set final type it. Jamo are kept as
/// Hangul Compatibility Jamo, as in `Jamo`, with the position the key gives them.
///
/// The shifted keys that type a digit or symbol in Korean (⇧H 0, ⇧B ?, ⇧M ") have no jamo here: they
/// type as on QWERTY, and a word with one is not Korean.
public enum SebeolsikFinal {
    public enum Position: Sendable { case initial, medial, final }

    public struct Key: Equatable, Sendable {
        public let jamo: Character
        public let position: Position
    }

    private static let table: [Character: Key] = {
        var map: [Character: Key] = [:]
        func add(_ position: Position, _ pairs: [(Character, Character)]) {
            for (seat, jamo) in pairs { map[seat] = Key(jamo: jamo, position: position) }
        }
        add(.initial, [("k", "ㄱ"), ("h", "ㄴ"), ("u", "ㄷ"), ("y", "ㄹ"), ("i", "ㅁ"), (";", "ㅂ"), ("n", "ㅅ"),
                       ("j", "ㅇ"), ("l", "ㅈ"), ("o", "ㅊ"), ("0", "ㅋ"), ("'", "ㅌ"), ("p", "ㅍ"), ("m", "ㅎ")])
        add(.medial, [("f", "ㅏ"), ("r", "ㅐ"), ("6", "ㅑ"), ("G", "ㅒ"), ("t", "ㅓ"), ("c", "ㅔ"), ("e", "ㅕ"),
                      ("7", "ㅖ"), ("v", "ㅗ"), ("/", "ㅗ"), ("4", "ㅛ"), ("b", "ㅜ"), ("9", "ㅜ"), ("5", "ㅠ"),
                      ("g", "ㅡ"), ("8", "ㅢ"), ("d", "ㅣ")])
        add(.final, [("x", "ㄱ"), ("!", "ㄲ"), ("V", "ㄳ"), ("s", "ㄴ"), ("E", "ㄵ"), ("S", "ㄶ"), ("A", "ㄷ"),
                     ("w", "ㄹ"), ("@", "ㄺ"), ("F", "ㄻ"), ("D", "ㄼ"), ("T", "ㄽ"), ("%", "ㄾ"), ("$", "ㄿ"),
                     ("R", "ㅀ"), ("z", "ㅁ"), ("3", "ㅂ"), ("X", "ㅄ"), ("q", "ㅅ"), ("2", "ㅆ"), ("a", "ㅇ"),
                     ("#", "ㅈ"), ("Z", "ㅊ"), ("C", "ㅋ"), ("W", "ㅌ"), ("Q", "ㅍ"), ("1", "ㅎ")])
        return map
    }()

    /// The jamo on a QWERTY key (shifted characters as typed: ! is the 1 key with Shift).
    static func jamo(onSeat seat: Character) -> Key? { table[seat] }

    /// Initial consonants typed twice: ㄱㄱ is ㄲ.
    private static let doubleInitials: [Character: Character] = ["ㄱ": "ㄲ", "ㄷ": "ㄸ", "ㅂ": "ㅃ", "ㅅ": "ㅆ", "ㅈ": "ㅉ"]
    /// Finals made of two keys, on top of 2-set's: ㄱㄱ and ㅅㅅ (최종 has keys for the rest).
    private static let finalCombos = Jamo.finalCombos.merging(["ㄱㄱ": "ㄲ", "ㅅㅅ": "ㅆ"]) { a, _ in a }

    /// Whether `key` (as typed) has a jamo here.
    static func jamo(for key: Character) -> Key? { jamo(onSeat: LatinLayout.current.seat(of: key)) }

    /// Shift on a key whose shifted form is a jamo (a final, or ㅒ) is Korean typing.
    static func isKoreanShiftKey(_ key: Character) -> Bool {
        let seat = LatinLayout.current.seat(of: key)
        return seat.isUppercase && table[seat] != nil
    }

    /// A capital with no jamo on its key (⇧H is 0 in Korean): only ever English.
    static func hasEnglishOnlyCapital(_ keys: String) -> Bool {
        keys.contains { key in
            let seat = LatinLayout.current.seat(of: key)
            return seat.isUppercase && table[seat] == nil
        }
    }

    static func startsSyllable(_ key: Character) -> Bool { jamo(for: key)?.position == .initial }

    /// Nil when a key has no jamo, or when every key is a digit: a number stays a number (05 isn't
    /// 큐, 00 isn't ㅋㅋ).
    static func compose(keys: String) -> HangulComposer.Result? {
        guard !keys.allSatisfy(\.isNumber) else { return nil }
        return composeJamo(keys)
    }

    /// The keys as jamo, digits alone too; nil when a key has none.
    static func composeJamo(_ keys: String) -> HangulComposer.Result? {
        guard !keys.isEmpty else { return nil }
        var units: [HangulComposer.Unit] = []
        var cho: Character?, jung: Character?, jong: Character?

        func flush() {
            if let c = cho, let v = jung {
                units.append(.syllable(Jamo.syllable(cho: c, jung: v, jong: jong)))
            } else {
                for j in [cho, jung, jong] { if let j { units.append(.jamo(j)) } }
            }
            cho = nil; jung = nil; jong = nil
        }

        for key in keys {
            guard let typed = jamo(for: key) else { return nil }
            let j = typed.jamo
            switch typed.position {
            case .initial:
                if let c = cho, jung == nil, let double = doubleInitials[c], c == j {
                    cho = double
                } else {
                    flush()
                    cho = j
                }
            case .medial:
                if let v = jung, jong == nil, let combined = Jamo.vowelCombos[String([v, j])] {
                    jung = combined
                } else if jung == nil, jong == nil {
                    jung = j
                } else {
                    flush()
                    jung = j
                }
            case .final:
                if cho != nil, jung != nil, jong == nil {
                    jong = j
                } else if let f = jong, let combined = finalCombos[String([f, j])] {
                    jong = combined
                } else {
                    flush()
                    units.append(.jamo(j))
                }
            }
        }
        flush()
        return HangulComposer.Result(units: units)
    }
}
