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
    /// 세벌식 390: 3-set's jamo with fewer finals of their own (ㄵ is ㄴ then ㅈ), QWERTY's
    /// symbols on the unshifted keys and the digits as a keypad under the right hand.
    case sebeolsik390

    /// One of the 3-set layouts, which `Sebeolsik` types.
    public var isSebeolsik: Bool { self != .dubeolsik }

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
        guard isSebeolsik, !key.isLetter, let jamo = Sebeolsik.jamo(onSeat: LatinLayout.current.seat(of: key)) else { return false }
        if keys.isEmpty { return jamo.position == .initial && !(afterDigit && key.isNumber) }
        // ㅋㅋ, also after a word (잘ㅋㅋ): lone jamo, which only the decision tells from a number.
        let laugh = String((keys + String(key)).reversed().prefix { LatinLayout.current.seat(of: $0) == "0" })
        if Sebeolsik.isLaughter(laugh), case let front = String(keys.dropLast(laugh.count - 1)), front.isEmpty || Sebeolsik.composeJamo(front).map({ !$0.isBroken }) == true {
            return true
        }
        // As jamo, digits too: 0 is ㅋ, and 09 쿠 is on its way to 퀼프 (a number of them alone
        // still reads as a number when the word is decided).
        guard let before = Sebeolsik.composeJamo(keys), !before.isBroken,
              let after = Sebeolsik.composeJamo(keys + String(key)), !after.isBroken else { return false }
        // A vowel or final has to join a syllable (가 + ㅆ is 갔, ㅋ + ㅠ is 큐): left on its own, or
        // making one nobody types, it was the symbol.
        if jamo.position == .initial { return true }
        if case .syllable(let s) = after.units.last { return knows(s) }
        return false
    }
}

/// 세벌식 최종 and 390 (공병우 최종, 390), as macOS's keyboard layouts type them (390Hangul and
/// 3SetHangul.keylayout in KoreanIM.app), for the 3-set layout current. Jamo are kept as Hangul
/// Compatibility Jamo, as in `Jamo`, with the position the key gives them.
///
/// The keys that type a digit or symbol in Korean (on 최종 ⇧H 0, ⇧B ?, ⇧M ", [ () have no jamo here: in
/// a word they type as on QWERTY, and a word with one is not Korean. Right after a Korean word they
/// type 3-set's symbol instead (`symbol(for:)`, asked by `InputSession`).
public enum Sebeolsik {
    public enum Position: Sendable { case initial, medial, final }

    public struct Key: Equatable, Sendable {
        public let jamo: Character
        public let position: Position
    }

    /// The jamo of the layout current, by QWERTY key.
    private static var table: [Character: Key] { KoreanLayout.current == .sebeolsik390 ? table390 : tableFinal }

    private static func table(_ extra: [(Position, [(Character, Character)])]) -> [Character: Key] {
        var map: [Character: Key] = [:]
        func add(_ position: Position, _ pairs: [(Character, Character)]) {
            for (seat, jamo) in pairs { map[seat] = Key(jamo: jamo, position: position) }
        }
        // The keys both layouts have.
        add(.initial, [("k", "ㄱ"), ("h", "ㄴ"), ("u", "ㄷ"), ("y", "ㄹ"), ("i", "ㅁ"), (";", "ㅂ"), ("n", "ㅅ"),
                       ("j", "ㅇ"), ("l", "ㅈ"), ("o", "ㅊ"), ("0", "ㅋ"), ("'", "ㅌ"), ("p", "ㅍ"), ("m", "ㅎ")])
        add(.medial, [("f", "ㅏ"), ("r", "ㅐ"), ("6", "ㅑ"), ("t", "ㅓ"), ("c", "ㅔ"), ("e", "ㅕ"),
                      ("7", "ㅖ"), ("v", "ㅗ"), ("/", "ㅗ"), ("4", "ㅛ"), ("b", "ㅜ"), ("9", "ㅜ"), ("5", "ㅠ"),
                      ("g", "ㅡ"), ("8", "ㅢ"), ("d", "ㅣ")])
        add(.final, [("x", "ㄱ"), ("s", "ㄴ"), ("S", "ㄶ"), ("A", "ㄷ"), ("w", "ㄹ"), ("z", "ㅁ"), ("3", "ㅂ"),
                     ("X", "ㅄ"), ("q", "ㅅ"), ("2", "ㅆ"), ("a", "ㅇ"), ("Z", "ㅊ"), ("W", "ㅌ"), ("Q", "ㅍ"), ("1", "ㅎ")])
        for (position, pairs) in extra { add(position, pairs) }
        return map
    }

    private static let tableFinal = table([
        (.medial, [("G", "ㅒ")]),
        (.final, [("!", "ㄲ"), ("V", "ㄳ"), ("E", "ㄵ"), ("@", "ㄺ"), ("F", "ㄻ"), ("D", "ㄼ"), ("T", "ㄽ"),
                  ("%", "ㄾ"), ("$", "ㄿ"), ("R", "ㅀ"), ("#", "ㅈ"), ("C", "ㅋ")]),
    ])

    /// 390 has fewer finals of its own: ㄳ ㄵ ㄼ ㄽ ㄾ ㄿ are two keys (`finalCombos`).
    private static let table390 = table([
        (.medial, [("R", "ㅒ")]),
        (.final, [("!", "ㅈ"), ("E", "ㅋ"), ("D", "ㄺ"), ("F", "ㄲ"), ("C", "ㄻ"), ("V", "ㅀ")]),
    ])

    /// The digits and symbols of the layout current where QWERTY types something else.
    private static var symbols: [Character: Character] { KoreanLayout.current == .sebeolsik390 ? symbols390 : symbolsFinal }

    /// The digits and symbols 3-set final types where QWERTY types something else, by the QWERTY
    /// key as typed (? is ⇧/, which types !). Not , . + or ⇧, ⇧. (the same as on QWERTY), and no
    /// key with a jamo: ⇧1 is ㄲ, never !.
    private static let symbolsFinal: [Character: Character] = [
        // Shifted punctuation.
        "~": "※", "^": "=", "&": "“", "*": "”", "(": "'", ")": "~", "_": ";", "{": "%", "}": "/",
        "|": "\\", "\"": "·", "<": ",", ">": ".", "?": "!", "B": "?", "N": "-", "M": "\"",
        // Shifted digits on the right hand.
        "H": "0", "J": "1", "K": "2", "L": "3", ":": "4", "Y": "5", "U": "6", "I": "7", "O": "8", "P": "9",
        // Unshifted symbols.
        "`": "*", "-": ")", "=": ">", "[": "(", "]": "<", "\\": ":",
    ]

    /// 390's: QWERTY's symbols on the unshifted keys (` is ₩), and the digits as a keypad under
    /// the right hand (⇧U ⇧I ⇧O 7 8 9, ⇧J ⇧K ⇧L 4 5 6, ⇧M ⇧, ⇧. 1 2 3, ⇧N 0).
    private static let symbols390: [Character: Character] = [
        // Shifted punctuation.
        "B": "!", "G": "/", "H": "'", "T": ";", "Y": "<", "P": ">",
        // Shifted digits.
        "U": "7", "I": "8", "O": "9", "J": "4", "K": "5", "L": "6", "M": "1", "<": "2", ">": "3", "N": "0",
        "`": "₩",
    ]

    /// The digit or symbol 3-set types on `key` (as typed) where QWERTY types another.
    public static func symbol(for key: Character) -> Character? { symbols[LatinLayout.current.seat(of: key)] }

    /// ㅋ typed two times or more on 0 (ㅋㅋ): a laugh after Korean, a number anywhere else.
    public static func isLaughter(_ keys: String) -> Bool {
        keys.count >= 2 && keys.allSatisfy { LatinLayout.current.seat(of: $0) == "0" }
    }

    /// The jamo on a QWERTY key (shifted characters as typed: ! is the 1 key with Shift).
    static func jamo(onSeat seat: Character) -> Key? { table[seat] }

    /// Initial consonants typed twice: ㄱㄱ is ㄲ.
    private static let doubleInitials: [Character: Character] = ["ㄱ": "ㄲ", "ㄷ": "ㄸ", "ㅂ": "ㅃ", "ㅅ": "ㅆ", "ㅈ": "ㅉ"]
    /// Finals made of two keys, on top of 2-set's: ㄱㄱ and ㅅㅅ (최종 has keys for the rest, 390 for some).
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
    /// 큐). Only 00 has its ㅋㅋ, as a reading to switch to: the engine still reads it as a number,
    /// and `InputSession` reads it as ㅋㅋ after Korean.
    static func compose(keys: String) -> HangulComposer.Result? {
        guard !keys.allSatisfy(\.isNumber) || isLaughter(keys) else { return nil }
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
