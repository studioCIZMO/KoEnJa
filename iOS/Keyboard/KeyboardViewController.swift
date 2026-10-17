import SeamtypeCore
import UIKit

/// One QWERTY layout for Korean (2-set), English and Japanese (romaji): SeamtypeCore decides
/// per word, the 한/あ/A key cycles a word that came out in the wrong language.
/// Japanese converts to kanji with Space (AzooKey); the bar above the keys shows the candidates.
final class KeyboardViewController: UIInputViewController {
    private lazy var client = ProxyClient(proxy: textDocumentProxy)
    /// What the session was built for; the app may have changed it since.
    private var preferences: LanguagePreferences?
    private var converter: JapaneseConverter?
    private var session: InputSession!
    private var heuristic: HeuristicEngine!
    private var keyStack: KeyStack!
    private weak var switchKey: UIButton?
    private var shifted = false
    /// 123 mode: the letter keys type digits and symbols.
    private var numbers = false
    private weak var numbersKey: UIButton?
    private var letterKeys: [(button: UIButton, letter: Character)] = []
    private let candidateRow = UIStackView()
    /// Key sizes, activated once the keys are in the view (they're relative to its width).
    private var keyConstraints: [NSLayoutConstraint] = []
    private var keyButtons: [UIButton] = []

    /// 2-set (두벌식) jamo on each QWERTY key: lower case, then shifted where it differs.
    static let jamo: [Character: (String, String?)] = [
        "q": ("ㅂ", "ㅃ"), "w": ("ㅈ", "ㅉ"), "e": ("ㄷ", "ㄸ"), "r": ("ㄱ", "ㄲ"), "t": ("ㅅ", "ㅆ"),
        "y": ("ㅛ", nil), "u": ("ㅕ", nil), "i": ("ㅑ", nil), "o": ("ㅐ", "ㅒ"), "p": ("ㅔ", "ㅖ"),
        "a": ("ㅁ", nil), "s": ("ㄴ", nil), "d": ("ㅇ", nil), "f": ("ㄹ", nil), "g": ("ㅎ", nil),
        "h": ("ㅗ", nil), "j": ("ㅓ", nil), "k": ("ㅏ", nil), "l": ("ㅣ", nil),
        "z": ("ㅋ", nil), "x": ("ㅌ", nil), "c": ("ㅊ", nil), "v": ("ㅍ", nil), "b": ("ㅠ", nil),
        "n": ("ㅜ", nil), "m": ("ㅡ", nil),
    ]

    /// Character keys and function keys, like the system keyboard in light and dark.
    static let keyColor = UIColor { $0.userInterfaceStyle == .dark ? UIColor(white: 0.42, alpha: 1) : .white }
    static let functionKeyColor = UIColor {
        $0.userInterfaceStyle == .dark ? UIColor(white: 0.27, alpha: 1) : UIColor(red: 0.67, green: 0.69, blue: 0.73, alpha: 1)
    }

    /// Same engine stack as Seamtype's macOS Runtime: learned corrections, then mixed words
    /// (github에, githubで), then the heuristic. The user's language order weights the heuristic.
    static func makeHeuristic(preferences: LanguagePreferences) -> HeuristicEngine {
        let heuristic = HeuristicEngine(japanese: preferences.japanese ? JapaneseModel.shared : nil)
        heuristic.koreanOffset = preferences.koreanOffset
        heuristic.japaneseOffset += preferences.japaneseOffsetDelta
        return heuristic
    }

    static func makeSession(converter: JapaneseConverter?, preferences: LanguagePreferences,
                            heuristic: HeuristicEngine? = nil) -> InputSession {
        let heuristic = heuristic ?? makeHeuristic(preferences: preferences)
        let japanese = heuristic.japanese
        let engine = LearningEngine(base: SplittingEngine(base: heuristic, english: heuristic.english,
                                                          japanese: heuristic.segmenter))
        // Kana while typing, candidates in the bar above, as on the system's Japanese keyboard
        // (no live conversion: it guessed whole phrases wrong, はなしま → 話魔).
        return InputSession(engine: engine, converter: converter, japaneseEnabled: japanese != nil)
    }

    /// One converter for every controller: iOS keeps old keyboard controllers alive, and each
    /// would hold its own dictionary (azooKey keeps everything but the controller static).
    private static var converter: JapaneseConverter?
    private static func sharedConverter() -> JapaneseConverter? {
        if converter == nil { converter = AzooKeyConverter() }
        return converter
    }

    // MARK: - Layout

    /// Measured on the system keyboards (Korean, English, Japanese) on an iPhone 17 Pro: 43pt keys
    /// 6pt apart, 6⅔pt from the screen edge, 54pt from row to row, a 53pt candidate bar.
    /// Every key is a multiple of one key pitch (a tenth of the width inside the margins).
    private static let gap: CGFloat = 6, margin: CGFloat = 20 / 3, keyHeight: CGFloat = 43, rowGap: CGFloat = 11

    /// What the letter keys type in 123 mode, like the system keyboard's first symbol page.
    static let symbols: [Character: Character] = Dictionary(uniqueKeysWithValues: zip(
        "qwertyuiopasdfghjklzxcvbnm", "1234567890-/:;()₩&@.,?!'\"~"))

    override func viewDidLoad() {
        super.viewDidLoad()
        let globe = needsInputModeSwitchKey
        let rows: [[(String, CGFloat)]] = [
            "qwertyuiop".map { (String($0), 1) },
            "asdfghjkl".map { (String($0), 1) },
            "zxcvbnm".map { (String($0), 1) },
            [("123", 1.25)] + (globe ? [("🌐", 1.25)] : []) + [("한あA", 1.25), ("space", globe ? 3.25 : 4.5), (".", 1), ("return", 2)],
        ]
        candidateRow.spacing = 4
        let candidates = UIScrollView()
        candidates.showsHorizontalScrollIndicator = false
        if #available(iOS 26, *) {
            // iOS 26 blurs scroll view edges next to bars; in a keyboard that blurred the whole bar.
            for edge in [candidates.topEdgeEffect, candidates.bottomEdgeEffect, candidates.leftEdgeEffect, candidates.rightEdgeEffect] {
                edge.isHidden = true
            }
        }
        candidates.addSubview(candidateRow)
        candidateRow.translatesAutoresizingMaskIntoConstraints = false

        // The third row's letters line up under the others; shift and delete take what's left.
        var views: [UIView] = [candidates, makeRow(rows[0]), makeRow(rows[1])]
        let thirdRow = UIStackView(arrangedSubviews: [makeKey("⇧", units: 1.3), makeRow(rows[2]), makeKey("⌫", units: 1.3)])
        thirdRow.distribution = .equalSpacing
        views += [thirdRow, makeRow(rows[3])]
        let stack = KeyStack(arrangedSubviews: views)
        stack.keys = keyButtons
        keyStack = stack
        stack.axis = .vertical
        stack.alignment = .center
        stack.spacing = Self.rowGap
        stack.setCustomSpacing(0, after: candidates)
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)
        NSLayoutConstraint.activate(keyConstraints + [
            stack.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            stack.topAnchor.constraint(equalTo: view.topAnchor),
            stack.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            candidates.widthAnchor.constraint(equalTo: view.widthAnchor, constant: -Self.margin * 2),
            candidates.heightAnchor.constraint(equalToConstant: 53),
            thirdRow.widthAnchor.constraint(equalTo: view.widthAnchor, constant: -Self.margin * 2),
            candidateRow.leadingAnchor.constraint(equalTo: candidates.contentLayoutGuide.leadingAnchor),
            candidateRow.trailingAnchor.constraint(equalTo: candidates.contentLayoutGuide.trailingAnchor),
            candidateRow.topAnchor.constraint(equalTo: candidates.contentLayoutGuide.topAnchor),
            candidateRow.bottomAnchor.constraint(equalTo: candidates.contentLayoutGuide.bottomAnchor),
            candidateRow.heightAnchor.constraint(equalTo: candidates.frameLayoutGuide.heightAnchor),
        ])
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        let current = LanguagePreferences.load()
        if current != preferences {
            preferences = current
            converter?.save()
            converter = current.japanese ? Self.sharedConverter() : nil
            heuristic = Self.makeHeuristic(preferences: current)
            session = Self.makeSession(converter: converter, preferences: current, heuristic: heuristic)
            switchKey?.configuration?.attributedTitle = Self.functionTitle(current.switchLabel)
        }
        TraceLog.write("APPEAR \(current.order.map(\.rawValue)) host=\(textDocumentProxy.documentContextBeforeInput ?? "nil")")
        session.activate(app: nil) // iOS doesn't tell a keyboard which app it's in
        // Apps can ask for a dark keyboard even in light mode (password fields, dark UIs).
        view.overrideUserInterfaceStyle = textDocumentProxy.keyboardAppearance == .dark ? .dark : .unspecified
    }

    private static func functionTitle(_ title: String, size: CGFloat = 16) -> AttributedString {
        AttributedString(title, attributes: .init([.font: UIFont.systemFont(ofSize: size)]))
    }

    override func textDidChange(_ textInput: UITextInput?) {
        TraceLog.write("textDidChange ctx=[\(textDocumentProxy.documentContextBeforeInput ?? "nil")] client.marked=[\(client.marked)]")
        documentChanged()
    }

    override func selectionDidChange(_ textInput: UITextInput?) { documentChanged() }

    /// The cursor moved away from the word being composed: it stays as typed, the next key
    /// starts a new word there (like azooKey, which ends the composition on a cursor jump).
    private func documentChanged() {
        guard session != nil, client.resetIfMovedAway() else { return }
        session.commitForNewContext(client: nil)
        refreshCandidates()
    }

    override func viewWillDisappear(_ animated: Bool) {
        session.commit(client: client)
        converter?.save()
        refreshCandidates()
        super.viewWillDisappear(animated)
    }

    private func makeRow(_ keys: [(String, CGFloat)]) -> UIStackView {
        let row = UIStackView(arrangedSubviews: keys.map { makeKey($0.0, units: $0.1) })
        row.spacing = Self.gap
        return row
    }

    private func makeKey(_ title: String, units: CGFloat) -> UIButton {
        let letter = title.count == 1 ? title.first! : nil
        let isCharacter = letter.map { !"⇧⌫".contains($0) } ?? false
        var config = UIButton.Configuration.filled()
        config.baseForegroundColor = .label
        config.baseBackgroundColor = isCharacter ? Self.keyColor : Self.functionKeyColor
        config.background.cornerRadius = 6
        config.contentInsets = .zero
        config.titleAlignment = .center
        let button = UIButton(configuration: config)
        keyButtons.append(button)
        button.accessibilityLabel = ["⇧": "shift", "⌫": "delete", "🌐": "next keyboard", "한あA": "switch language"][title] ?? title
        keyConstraints += [
            // units of a pitch, minus one gap: a row of 10 units spans the width inside the margins
            button.widthAnchor.constraint(equalTo: view.widthAnchor, multiplier: units / 10,
                                          constant: (Self.gap - 2 * Self.margin) * units / 10 - Self.gap),
            button.heightAnchor.constraint(equalToConstant: Self.keyHeight),
        ]
        if let letter, letter.isLetter {
            letterKeys.append((button, letter))
            label(button, letter)
        } else {
            button.configuration?.attributedTitle = Self.functionTitle(title, size: isCharacter ? 22 : 16)
        }
        if title == "한あA" { switchKey = button }
        if title == "123" { numbersKey = button }
        if title == "🌐" {
            button.addTarget(self, action: #selector(handleInputModeList(from:with:)), for: .allTouchEvents)
        } else {
            // On touch-down, in the order keys are pressed: on release, fast typing (the next key
            // down before this one is up) dropped and reordered keys (안되었다 came in as 안뇠다).
            // ponytail: no slide-off to cancel.
            button.addAction(UIAction { [unowned self, unowned button] _ in
                if let o = keyStack?.offset(of: button) { TraceLog.write(String(format: "TOUCH %@ %.1f %.1f", title, o.dx, o.dy)) }
                tap(title)
            }, for: .touchDown)
            if title == "⌫" { repeatWhileHeld(button) }
        }
        return button
    }

    /// Delete repeats while held: after 0.4s, every 0.1s (azooKey's timing).
    private func repeatWhileHeld(_ button: UIButton) {
        var repeating: Task<Void, Never>?
        button.addAction(UIAction { [unowned self] _ in
            repeating = Task { @MainActor in
                try? await Task.sleep(nanoseconds: 400_000_000)
                while !Task.isCancelled {
                    tap("⌫")
                    try? await Task.sleep(nanoseconds: 100_000_000)
                }
            }
        }, for: .touchDown)
        button.addAction(UIAction { _ in repeating?.cancel() }, for: [.touchUpInside, .touchUpOutside, .touchCancel])
    }

    /// English letter, with its 2-set jamo below: "q / ㅂ", shifted "Q / ㅃ"; in 123 mode the symbol.
    private func label(_ button: UIButton, _ letter: Character) {
        if numbers {
            button.configuration?.attributedTitle = Self.functionTitle(String(Self.symbols[letter]!), size: 22)
            button.configuration?.attributedSubtitle = nil
            return
        }
        let jamo = Self.jamo[letter].map { shifted ? ($0.1 ?? $0.0) : $0.0 } ?? ""
        button.configuration?.attributedTitle = AttributedString(shifted ? letter.uppercased() : String(letter),
                                                                 attributes: .init([.font: UIFont.systemFont(ofSize: 20)]))
        button.configuration?.attributedSubtitle = AttributedString(jamo, attributes: .init([
            .font: UIFont.systemFont(ofSize: 11), .foregroundColor: UIColor.secondaryLabel]))
        button.configuration?.titlePadding = 0
    }

    // MARK: - Keys

    private func tap(_ title: String) {
        let key: InputSession.Key
        switch title {
        case "⇧":
            shifted.toggle()
            letterKeys.forEach { label($0.button, $0.letter) }
            return
        case "123":
            numbers.toggle()
            if shifted { shifted = false }
            numbersKey?.configuration?.attributedTitle = Self.functionTitle(numbers ? "ABC" : "123")
            letterKeys.forEach { label($0.button, $0.letter) }
            return
        case "⌫": key = .delete
        case "한あA": key = .cycle // always a switch key, unlike Esc (AppPolicy.standardSwitchKeys)
        case "space": key = .printable(" ")
        case "return":
            // Japanese being typed: return takes the first candidate (highlighted), like the
            // system keyboard; the kana alone stays a tap on that candidate away.
            if let first = session.typingCandidates.first {
                TraceLog.write("KEY return → \(first.text)")
                session.choose(typing: first, client: client)
                refreshCandidates()
                return
            }
            key = .enter
        case _ where numbers && Self.symbols[title.first!] != nil:
            key = .printable(Self.symbols[title.first!]!)
        default:
            let letter = Character(title).isLetter ? corrected(Character(title)) : Character(title)
            let c = Character(shifted ? letter.uppercased() : String(letter))
            key = c.isLetter ? .letter(c) : .printable(c)
            if shifted { tap("⇧") } // one-shot shift
        }
        TraceLog.write("KEY \(title) \(key)")
        (converter as? AzooKeyConverter)?.forgetNextWords() // a commit by this key brings new ones
        if !session.handle(key, client: client) {
            TraceLog.write("  passToApp")
            passToApp(key)
        }
        if let trace = session.lastTrace { TraceLog.write("  decided \(trace.decision.language) [\(trace.keys)] \(trace.decision.explanation)") }
        refreshCandidates()
    }

    /// What the app would do with a key the session didn't take.
    private func passToApp(_ key: InputSession.Key) {
        switch key {
        case .letter(let c), .printable(let c): textDocumentProxy.insertText(String(c))
        case .delete: textDocumentProxy.deleteBackward()
        case .enter: textDocumentProxy.insertText("\n")
        default: break
        }
    }

    // MARK: - Typing correction

    /// A touch near the edge of a letter key, mid-word: the neighbor wins when it makes the word
    /// much more likely (fgem → fhem, 로드). Mozc and azooKey also correct by touch position.
    /// ponytail: one key at a time, a fixed band and margin; tune them with traces.
    private func corrected(_ letter: Character) -> Character {
        guard !numbers, !session.keys.isEmpty, let point = keyStack.lastTouch,
              let typed = letterKeys.first(where: { $0.letter == letter }) else { return letter }
        let neighbor = letterKeys.filter { $0.letter != letter }
            .map { (letter: $0.letter, distance: keyStack.distance(point, $0.button)) }
            .min { $0.distance < $1.distance }
        guard let neighbor, neighbor.distance < Self.gap + typed.button.bounds.width * Self.correctionBand else { return letter }
        let keys = session.keys
        let shift: (Character) -> String = { [shifted] in shifted ? $0.uppercased() : String($0) }
        let typedScore = Self.plausibility(keys + shift(letter), heuristic)
        let neighborScore = Self.plausibility(keys + shift(neighbor.letter), heuristic)
        TraceLog.write("NEAR-EDGE \(letter)/\(neighbor.letter) \(typedScore) \(neighborScore)")
        return neighborScore > typedScore + Self.correctionMargin ? neighbor.letter : letter
    }

    /// How close to the neighbor counts as "near the edge" (share of a key's width past the gap),
    /// and how much more likely (log-probability per key) the neighbor's word must be.
    static let correctionBand: CGFloat = 0.25, correctionMargin = 1.0

    /// How likely the keys are as a word in any enabled language (average log-probability per key).
    static func plausibility(_ keys: String, _ heuristic: HeuristicEngine) -> Double {
        [heuristic.english.averageLogProbability(keys.lowercased()),
         heuristic.korean.score(keys: keys, partial: true)?.averageLogProbability,
         heuristic.japanese?.score(keys: keys, partial: true)?.averageLogProbability]
            .compactMap { $0 }.max() ?? -.infinity
    }

    // MARK: - Kanji candidates

    /// Kanji candidates: while typing Japanese (tap one to pick it), after Space (the selected
    /// one highlighted; Space moves on), and after a commit what usually comes next (東京 → で).
    private func refreshCandidates() {
        candidateRow.arrangedSubviews.forEach { $0.removeFromSuperview() }
        let conversion = session.conversion
        let candidates = conversion?.candidates ?? Array(session.typingCandidates.prefix(30))
        for (index, candidate) in candidates.enumerated() {
            addCandidate(candidate.text, selected: index == (conversion?.selected ?? 0)) { [unowned self] in
                if conversion != nil {
                    session.choose(index, client: client)
                } else {
                    session.choose(typing: candidate, client: client)
                }
            }
        }
        if candidates.isEmpty, session.keys.isEmpty, let azooKey = converter as? AzooKeyConverter {
            // ponytail: one step; azooKey chains predictions (learning each pick with the last).
            for word in azooKey.nextWords().prefix(30) {
                addCandidate(word, selected: false) { [unowned self] in
                    textDocumentProxy.insertText(word)
                    azooKey.forgetNextWords()
                }
            }
        }
        (candidateRow.superview as? UIScrollView)?.contentOffset = .zero
    }

    private func addCandidate(_ text: String, selected: Bool, action: @escaping () -> Void) {
        var config = selected ? UIButton.Configuration.filled() : UIButton.Configuration.plain()
        config.attributedTitle = AttributedString(text, attributes: .init([.font: UIFont.systemFont(ofSize: 20)]))
        config.baseForegroundColor = selected ? .systemBackground : .label
        config.baseBackgroundColor = .label
        let button = UIButton(configuration: config)
        button.addAction(UIAction { [unowned self] _ in
            action()
            refreshCandidates()
        }, for: .touchUpInside)
        candidateRow.addArrangedSubview(button)
    }
}

/// The keys with no dead space: a touch in a gap between keys goes to the nearest key, as on
/// the system keyboard (the gaps are a fifth of the keyboard's area; touches there were lost).
final class KeyStack: UIStackView {
    var keys: [UIButton] = []
    /// Where the latest touch went down, for typing correction.
    private(set) var lastTouch: CGPoint?

    /// Fingers land below and right of the key they aim at (traces: 9 of 11 neighbor typos were
    /// the key below or to the right); the system keyboard shifts touches the same way.
    /// ponytail: one fixed shift; calibrate from the touch offsets traced with each key.
    static let touchShift = CGVector(dx: -1.5, dy: -3)

    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        let hit = super.hitTest(point, with: event)
        // On a key or in a gap between keys (a row), not the candidate bar: the key nearest the
        // shifted point.
        guard hit is UIButton || hit is UIStackView, let bar = arrangedSubviews.first,
              hit?.isDescendant(of: bar) == false else { return hit }
        let shifted = CGPoint(x: point.x + Self.touchShift.dx, y: point.y + Self.touchShift.dy)
        lastTouch = shifted
        guard let nearest = keys.min(by: { distance(shifted, $0) < distance(shifted, $1) }),
              distance(shifted, nearest) < 12 else { return hit }
        if nearest !== hit { TraceLog.write("NEAR \(nearest.accessibilityLabel ?? "") (was \((hit as? UIButton)?.accessibilityLabel ?? "gap"))") }
        return nearest
    }

    /// Where the touch was from the key's center, for calibrating `touchShift`.
    func offset(of key: UIView) -> CGVector? {
        guard let lastTouch else { return nil }
        let frame = key.convert(key.bounds, to: self)
        return CGVector(dx: lastTouch.x - Self.touchShift.dx - frame.midX, dy: lastTouch.y - Self.touchShift.dy - frame.midY)
    }

    func distance(_ point: CGPoint, _ key: UIView) -> CGFloat {
        let frame = key.convert(key.bounds, to: self)
        return hypot(max(frame.minX - point.x, 0, point.x - frame.maxX), max(frame.minY - point.y, 0, point.y - frame.maxY))
    }
}
