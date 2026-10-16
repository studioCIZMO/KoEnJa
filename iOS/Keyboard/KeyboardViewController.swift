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
    static func makeSession(converter: JapaneseConverter?, preferences: LanguagePreferences) -> InputSession {
        let japanese = preferences.japanese ? JapaneseModel.shared : nil
        let heuristic = HeuristicEngine(japanese: japanese)
        heuristic.koreanOffset = preferences.koreanOffset
        heuristic.japaneseOffset += preferences.japaneseOffsetDelta
        let engine = LearningEngine(base: SplittingEngine(base: heuristic, english: heuristic.english,
                                                          japanese: heuristic.segmenter))
        // Kana while typing, candidates in the bar above, as on the system's Japanese keyboard
        // (no live conversion: it guessed whole phrases wrong, はなしま → 話魔).
        return InputSession(engine: engine, converter: converter, japaneseEnabled: japanese != nil)
    }

    // MARK: - Layout

    /// Every key is a multiple of one key width (a tenth of the keyboard), and as tall and as far
    /// apart as the system keyboard's (measured on an iPhone 17 Pro screenshot: 43pt keys, 54pt
    /// from row to row, 24pt above the first row; with Japanese on, a taller bar for the kanji candidates).
    private static let gap: CGFloat = 6, keyHeight: CGFloat = 43, rowGap: CGFloat = 11
    private lazy var candidateHeight = candidateRow.superview!.heightAnchor.constraint(equalToConstant: 24)

    /// What the letter keys type in 123 mode, like the system keyboard's first symbol page.
    static let symbols: [Character: Character] = Dictionary(uniqueKeysWithValues: zip(
        "qwertyuiopasdfghjklzxcvbnm", "1234567890-/:;()₩&@.,?!'\"~"))

    override func viewDidLoad() {
        super.viewDidLoad()
        let globe = needsInputModeSwitchKey
        let rows: [[(String, CGFloat)]] = [
            "qwertyuiop".map { (String($0), 1) },
            "asdfghjkl".map { (String($0), 1) },
            [("⇧", 1.5)] + "zxcvbnm".map { (String($0), 1) } + [("⌫", 1.5)],
            [("123", 1.25)] + (globe ? [("🌐", 1.25)] : []) + [("한あA", 1.25), ("space", globe ? 3.25 : 4.5), (".", 1), ("return", 2)],
        ]
        candidateRow.spacing = 4
        let candidates = UIScrollView()
        candidates.showsHorizontalScrollIndicator = false
        candidates.addSubview(candidateRow)
        candidateRow.translatesAutoresizingMaskIntoConstraints = false

        let stack = KeyStack(arrangedSubviews: [candidates] + rows.map(makeRow))
        stack.keys = keyButtons
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
            stack.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -3),
            candidates.widthAnchor.constraint(equalTo: view.widthAnchor, constant: -Self.gap * 2),
            candidateHeight,
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
            converter = current.japanese ? AzooKeyConverter() : nil
            session = Self.makeSession(converter: converter, preferences: current)
            switchKey?.configuration?.attributedTitle = Self.functionTitle(current.switchLabel)
            candidateHeight.constant = current.japanese ? 44 : 24
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
            // units of a tenth of the width, minus one gap: every row adds up to width − gap
            button.widthAnchor.constraint(equalTo: view.widthAnchor, multiplier: units / 10, constant: -Self.gap),
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
            // ponytail: no slide-off to cancel; delete doesn't repeat yet.
            button.addAction(UIAction { [unowned self] _ in tap(title) }, for: .touchDown)
        }
        return button
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
        case "return": key = .enter
        case _ where numbers && Self.symbols[title.first!] != nil:
            key = .printable(Self.symbols[title.first!]!)
        default:
            let c = Character(shifted ? title.uppercased() : title)
            key = c.isLetter ? .letter(c) : .printable(c)
            if shifted { tap("⇧") } // one-shot shift
        }
        TraceLog.write("KEY \(title) \(key)")
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

    // MARK: - Kanji candidates

    /// Kanji candidates: while typing Japanese (tap one to pick it), and after Space (the
    /// selected one highlighted; Space moves on).
    private func refreshCandidates() {
        candidateRow.arrangedSubviews.forEach { $0.removeFromSuperview() }
        let conversion = session.conversion
        let candidates = conversion?.candidates ?? Array(session.typingCandidates.prefix(30))
        for (index, candidate) in candidates.enumerated() {
            let selected = index == conversion?.selected
            var config = selected ? UIButton.Configuration.filled() : UIButton.Configuration.plain()
            config.attributedTitle = AttributedString(candidate.text, attributes: .init([.font: UIFont.systemFont(ofSize: 20)]))
            config.baseForegroundColor = selected ? .systemBackground : .label
            config.baseBackgroundColor = .label
            let button = UIButton(configuration: config)
            button.addAction(UIAction { [unowned self] _ in
                if conversion != nil {
                    session.choose(index, client: client)
                } else {
                    session.choose(typing: candidate, client: client)
                }
                refreshCandidates()
            }, for: .touchUpInside)
            candidateRow.addArrangedSubview(button)
        }
        (candidateRow.superview as? UIScrollView)?.contentOffset = .zero
    }
}

/// The keys with no dead space: a touch in a gap between keys goes to the nearest key, as on
/// the system keyboard (the gaps are a fifth of the keyboard's area; touches there were lost).
final class KeyStack: UIStackView {
    var keys: [UIButton] = []

    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        let hit = super.hitTest(point, with: event)
        // Only the gaps: the stack itself or a row, not the candidate bar.
        guard hit === self || hit?.superview === self && hit is UIStackView else { return hit }
        let nearest = keys.min { distance(point, $0) < distance(point, $1) }
        if let nearest, distance(point, nearest) < 12 {
            TraceLog.write("NEAR \(nearest.accessibilityLabel ?? "")")
            return nearest
        }
        return hit
    }

    private func distance(_ point: CGPoint, _ key: UIView) -> CGFloat {
        let frame = key.convert(key.bounds, to: self)
        return hypot(max(frame.minX - point.x, 0, point.x - frame.maxX), max(frame.minY - point.y, 0, point.y - frame.maxY))
    }
}
