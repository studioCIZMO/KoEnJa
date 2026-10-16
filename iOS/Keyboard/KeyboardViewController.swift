import SeamtypeCore
import UIKit

/// One QWERTY layout for Korean (2-set), English and Japanese (romaji): SeamtypeCore decides
/// per word, the 한/あ/A key cycles a word that came out in the wrong language.
final class KeyboardViewController: UIInputViewController {
    private lazy var client = ProxyClient(proxy: textDocumentProxy)
    private lazy var session = Self.makeSession()
    private var shifted = false
    private var letterButtons: [UIButton] = []

    /// Same engine stack as Seamtype's macOS Runtime: learned corrections, then mixed words
    /// (github에, githubで), then the heuristic. No kanji converter yet: Japanese commits kana.
    static func makeSession() -> InputSession {
        let japanese = JapaneseModel.shared
        let heuristic = HeuristicEngine(japanese: japanese)
        let engine = LearningEngine(base: SplittingEngine(base: heuristic, english: heuristic.english,
                                                          japanese: heuristic.segmenter))
        return InputSession(engine: engine, japaneseEnabled: japanese != nil)
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        let rows: [[String]] = [
            Array("1234567890").map(String.init),
            Array("qwertyuiop").map(String.init),
            Array("asdfghjkl").map(String.init),
            ["⇧"] + Array("zxcvbnm").map(String.init) + ["⌫"],
            (needsInputModeSwitchKey ? ["🌐"] : []) + ["한あA", ",", "space", ".", "return"],
        ]
        let stack = UIStackView(arrangedSubviews: rows.map(makeRow))
        stack.axis = .vertical
        stack.spacing = 8
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 3),
            stack.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -3),
            stack.topAnchor.constraint(equalTo: view.topAnchor, constant: 8),
            stack.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -8),
        ])
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        session.activate(app: nil) // iOS doesn't tell a keyboard which app it's in
    }

    override func viewWillDisappear(_ animated: Bool) {
        session.commit(client: client)
        super.viewWillDisappear(animated)
    }

    private func makeRow(_ titles: [String]) -> UIStackView {
        let row = UIStackView(arrangedSubviews: titles.map(makeKey))
        row.spacing = 5
        row.distribution = .fillProportionally
        return row
    }

    private func makeKey(_ title: String) -> UIButton {
        var config = UIButton.Configuration.filled()
        config.title = title
        config.baseForegroundColor = .label
        config.baseBackgroundColor = title.count == 1 && title != "⇧" && title != "⌫" ? .systemBackground : .systemGray3
        config.contentInsets = NSDirectionalEdgeInsets(top: 10, leading: 2, bottom: 10, trailing: 2)
        let button = UIButton(configuration: config)
        button.accessibilityLabel = ["⇧": "shift", "⌫": "delete", "🌐": "next keyboard", "한あA": "switch language"][title] ?? title
        if title == "space" {
            button.widthAnchor.constraint(greaterThanOrEqualToConstant: 140).isActive = true
        }
        if title == "🌐" {
            button.addTarget(self, action: #selector(handleInputModeList(from:with:)), for: .allTouchEvents)
        } else {
            button.addAction(UIAction { [unowned self] _ in tap(title) }, for: .touchUpInside)
        }
        if title.count == 1, title.first!.isLetter { letterButtons.append(button) }
        return button
    }

    private func tap(_ title: String) {
        let key: InputSession.Key
        switch title {
        case "⇧":
            shifted.toggle()
            letterButtons.forEach { $0.configuration?.title = shifted ? $0.configuration?.title?.uppercased() : $0.configuration?.title?.lowercased() }
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
}
