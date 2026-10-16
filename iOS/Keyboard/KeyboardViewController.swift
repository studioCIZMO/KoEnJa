import SeamtypeCore
import UIKit

/// One QWERTY layout for Korean (2-set), English and Japanese (romaji): SeamtypeCore decides
/// per word, the 한/あ/A key cycles a word that came out in the wrong language.
/// Japanese converts to kanji with Space (AzooKey); the bar above the keys shows the candidates.
final class KeyboardViewController: UIInputViewController {
    private lazy var client = ProxyClient(proxy: textDocumentProxy)
    private lazy var converter: JapaneseConverter? = AzooKeyConverter()
    private lazy var session = Self.makeSession(converter: converter)
    private var shifted = false
    private var letterKeys: [(button: UIButton, letter: Character)] = []
    private let candidateRow = UIStackView()
    /// Key sizes, activated once the keys are in the view (they're relative to its width).
    private var keyConstraints: [NSLayoutConstraint] = []

    /// 2-set (두벌식) jamo on each QWERTY key: lower case, then shifted where it differs.
    static let jamo: [Character: (String, String?)] = [
        "q": ("ㅂ", "ㅃ"), "w": ("ㅈ", "ㅉ"), "e": ("ㄷ", "ㄸ"), "r": ("ㄱ", "ㄲ"), "t": ("ㅅ", "ㅆ"),
        "y": ("ㅛ", nil), "u": ("ㅕ", nil), "i": ("ㅑ", nil), "o": ("ㅐ", "ㅒ"), "p": ("ㅔ", "ㅖ"),
        "a": ("ㅁ", nil), "s": ("ㄴ", nil), "d": ("ㅇ", nil), "f": ("ㄹ", nil), "g": ("ㅎ", nil),
        "h": ("ㅗ", nil), "j": ("ㅓ", nil), "k": ("ㅏ", nil), "l": ("ㅣ", nil),
        "z": ("ㅋ", nil), "x": ("ㅌ", nil), "c": ("ㅊ", nil), "v": ("ㅍ", nil), "b": ("ㅠ", nil),
        "n": ("ㅜ", nil), "m": ("ㅡ", nil),
    ]

    /// Same engine stack as Seamtype's macOS Runtime: learned corrections, then mixed words
    /// (github에, githubで), then the heuristic.
    static func makeSession(converter: JapaneseConverter?) -> InputSession {
        let japanese = JapaneseModel.shared
        let heuristic = HeuristicEngine(japanese: japanese)
        let engine = LearningEngine(base: SplittingEngine(base: heuristic, english: heuristic.english,
                                                          japanese: heuristic.segmenter))
        return InputSession(engine: engine, converter: converter, japaneseEnabled: japanese != nil)
    }

    // MARK: - Layout

    /// Every key is a multiple of one key width (a tenth of the keyboard), like the system keyboard.
    private static let gap: CGFloat = 6, keyHeight: CGFloat = 44

    override func viewDidLoad() {
        super.viewDidLoad()
        let globe = needsInputModeSwitchKey
        let rows: [[(String, CGFloat)]] = [
            "1234567890".map { (String($0), 1) },
            "qwertyuiop".map { (String($0), 1) },
            "asdfghjkl".map { (String($0), 1) },
            [("⇧", 1.5)] + "zxcvbnm".map { (String($0), 1) } + [("⌫", 1.5)],
            (globe ? [("🌐", 1.25)] : []) + [("한あA", 1.5), (",", 1), ("space", globe ? 3.25 : 4.5), (".", 1), ("return", 2)],
        ]
        candidateRow.spacing = 4
        let candidates = UIScrollView()
        candidates.showsHorizontalScrollIndicator = false
        candidates.addSubview(candidateRow)
        candidateRow.translatesAutoresizingMaskIntoConstraints = false

        let stack = UIStackView(arrangedSubviews: [candidates] + rows.map(makeRow))
        stack.axis = .vertical
        stack.alignment = .center
        stack.spacing = 10
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)
        NSLayoutConstraint.activate(keyConstraints + [
            stack.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            stack.topAnchor.constraint(equalTo: view.topAnchor, constant: 4),
            stack.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -6),
            candidates.widthAnchor.constraint(equalTo: view.widthAnchor, constant: -Self.gap * 2),
            candidates.heightAnchor.constraint(equalToConstant: 36),
            candidateRow.leadingAnchor.constraint(equalTo: candidates.contentLayoutGuide.leadingAnchor),
            candidateRow.trailingAnchor.constraint(equalTo: candidates.contentLayoutGuide.trailingAnchor),
            candidateRow.topAnchor.constraint(equalTo: candidates.contentLayoutGuide.topAnchor),
            candidateRow.bottomAnchor.constraint(equalTo: candidates.contentLayoutGuide.bottomAnchor),
            candidateRow.heightAnchor.constraint(equalTo: candidates.frameLayoutGuide.heightAnchor),
        ])
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        session.activate(app: nil) // iOS doesn't tell a keyboard which app it's in
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
        config.baseBackgroundColor = isCharacter ? .systemBackground : .systemGray3
        config.background.cornerRadius = 6
        config.contentInsets = .zero
        config.titleAlignment = .center
        let button = UIButton(configuration: config)
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
            button.configuration?.attributedTitle = AttributedString(title, attributes: .init([.font: UIFont.systemFont(ofSize: isCharacter ? 22 : 16)]))
        }
        if title == "🌐" {
            button.addTarget(self, action: #selector(handleInputModeList(from:with:)), for: .allTouchEvents)
        } else {
            button.addAction(UIAction { [unowned self] _ in tap(title) }, for: .touchUpInside)
        }
        return button
    }

    /// English letter, with its 2-set jamo below: "q / ㅂ", shifted "Q / ㅃ".
    private func label(_ button: UIButton, _ letter: Character) {
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
        case "⌫": key = .delete
        case "한あA": key = .cycle // always a switch key, unlike Esc (AppPolicy.standardSwitchKeys)
        case "space": key = .printable(" ")
        case "return": key = .enter
        default:
            let c = Character(shifted ? title.uppercased() : title)
            key = c.isLetter ? .letter(c) : .printable(c)
            if shifted { tap("⇧") } // one-shot shift
        }
        if !session.handle(key, client: client) { passToApp(key) }
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

    private func refreshCandidates() {
        candidateRow.arrangedSubviews.forEach { $0.removeFromSuperview() }
        guard let conversion = session.conversion else { return }
        for (index, candidate) in conversion.candidates.enumerated() {
            var config = index == conversion.selected ? UIButton.Configuration.filled() : UIButton.Configuration.plain()
            config.attributedTitle = AttributedString(candidate.text, attributes: .init([.font: UIFont.systemFont(ofSize: 18)]))
            config.baseForegroundColor = index == conversion.selected ? .systemBackground : .label
            config.baseBackgroundColor = .label
            let button = UIButton(configuration: config)
            button.addAction(UIAction { [unowned self] _ in
                session.choose(index, client: client)
                refreshCandidates()
            }, for: .touchUpInside)
            candidateRow.addArrangedSubview(button)
        }
    }
}
