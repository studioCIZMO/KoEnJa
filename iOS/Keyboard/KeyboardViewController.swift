import AudioToolbox
import SeamtypeCore
import UIKit

/// One QWERTY layout for Korean (2-set), English and Japanese (romaji): SeamtypeCore decides
/// per word, the 한/あ/A key cycles a word that came out in the wrong language.
/// Japanese converts to kanji with Space (AzooKey); the bar above the keys shows the candidates.
final class KeyboardViewController: UIInputViewController {
    private lazy var client = ProxyClient(proxy: textDocumentProxy)
    /// The engine is shared by every controller: iOS keeps old keyboard controllers alive (each
    /// coming up of the keyboard made a new one, +1-2 MB each on device), so a controller holds
    /// only its keys, and drops them when it goes off screen (azooKey keeps all but the controller
    /// static too). `preferences` is what the engine was built for; the app may have changed it.
    private static var preferences: LanguagePreferences?
    private static var session: InputSession!
    private static var heuristic: HeuristicEngine!
    private var session: InputSession { Self.session }
    private var heuristic: HeuristicEngine { Self.heuristic }
    private var converter: JapaneseConverter? { Self.preferences?.japanese == true ? Self.converter : nil }
    private var keyStack: KeyStack?
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

    /// Keys, like the system keyboard in light and dark (iOS 26: every key the same), and a
    /// function key while pressed (a character key shows its popup instead).
    static let keyColor = UIColor { $0.userInterfaceStyle == .dark ? UIColor(white: 0.42, alpha: 1) : .white }
    static let pressedKeyColor = UIColor {
        $0.userInterfaceStyle == .dark ? UIColor(white: 0.27, alpha: 1) : UIColor(red: 0.67, green: 0.69, blue: 0.73, alpha: 1)
    }

    /// Immutable syllable tables shared by every controller and splitter.
    private static let korean = KoreanModel()

    /// Same engine stack as Seamtype's macOS Runtime: learned corrections, then mixed words
    /// (github에, githubで), then the heuristic. The user's language order weights the heuristic.
    static func makeHeuristic(preferences: LanguagePreferences) -> HeuristicEngine {
        let heuristic = HeuristicEngine(korean: korean, japanese: preferences.japanese ? JapaneseModel.shared : nil)
        heuristic.koreanOffset = preferences.koreanOffset
        heuristic.japaneseOffset += preferences.japaneseOffsetDelta
        return heuristic
    }

    static func makeSession(converter: JapaneseConverter?, preferences: LanguagePreferences,
                            heuristic: HeuristicEngine? = nil) -> InputSession {
        let heuristic = heuristic ?? makeHeuristic(preferences: preferences)
        let japanese = heuristic.japanese
        let engine = LearningEngine(base: SplittingEngine(base: heuristic, english: heuristic.english,
                                                          korean: heuristic.korean, japanese: heuristic.segmenter))
        // Kana while typing, candidates in the bar above, as on the system's Japanese keyboard
        // (live conversion off by default: it guessed whole phrases wrong, はなしま → 話魔).
        let session = InputSession(engine: engine, converter: converter, japaneseEnabled: japanese != nil,
                                   liveConversion: preferences.liveConversion,
                                   returnConfirmsOnly: preferences.returnConfirmsOnly)
        session.fullWidthDigits = preferences.fullWidthDigits
        session.fullWidthSpace = preferences.fullWidthSpace
        return session
    }

    private static var converter: JapaneseConverter?

    // MARK: - Layout

    /// Measured on the system keyboards (Korean, English, Japanese) on an iPhone 17 Pro, in a
    /// screen recording: 42⅓pt keys 6pt apart, 6⅔pt from the screen edge, 54pt from row to row,
    /// a 36pt candidate bar. The system's last row reaches 3⅔pt lower than iOS lets a third-party
    /// keyboard draw (it clips there), so our last row is that much shorter (`clipped`).
    /// Every key is a multiple of one key pitch (a tenth of the width inside the margins).
    private static let gap: CGFloat = 6, margin: CGFloat = 20 / 3, keyHeight: CGFloat = 127 / 3,
                       rowGap: CGFloat = 54 - 127 / 3, barHeight: CGFloat = 36, clipped: CGFloat = 11 / 3

    /// What the letter keys type in 123 mode, like the system keyboard's first symbol page.
    static let symbols: [Character: Character] = Dictionary(uniqueKeysWithValues: zip(
        "qwertyuiopasdfghjklzxcvbnm", "1234567890-/:;()₩&@.,?!'\"~"))

    private static var keyCount = 0 // memory goes in the trace every 100 keys
    /// Keyboards alive in this process (in the trace on APPEAR): iOS should free closed ones.
    nonisolated(unsafe) private static var alive = 0
    nonisolated(unsafe) private var counted = false
    deinit { if counted { Self.alive -= 1 } }
    private static let bottomRow: Set = ["123", "🌐", "한あA", "space", ".", "return"]

    private func buildKeys() {
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
            candidates.heightAnchor.constraint(equalToConstant: Self.barHeight),
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
        if current != Self.preferences {
            Self.preferences = current
            Self.converter?.save()
            if current.japanese, Self.converter == nil { Self.converter = AzooKeyConverter() }
            Self.heuristic = Self.makeHeuristic(preferences: current)
            Self.session = Self.makeSession(converter: converter, preferences: current, heuristic: heuristic)
        }
        if keyStack == nil { buildKeys() }
        switchKey?.configuration?.attributedTitle = Self.functionTitle(current.switchLabel)
        sound = KeyFeedback.sound
        haptics = KeyFeedback.haptics
        if !counted { counted = true; Self.alive += 1 }
        TraceLog.write("APPEAR \(TraceLog.footprint) alive=\(Self.alive) \(current.order.map(\.rawValue)) host=\(textDocumentProxy.documentContextBeforeInput ?? "nil")")
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
        guard Self.session != nil, client.resetIfMovedAway() else { return }
        session.commitForNewContext(client: nil)
        refreshCandidates()
    }

    override func viewWillDisappear(_ animated: Bool) {
        repeating?.cancel()
        repeating = nil
        popup.isHidden = true
        session.commit(client: client)
        converter?.save()
        refreshCandidates()
        TraceLog.write("DISAPPEAR \(TraceLog.footprint)")
        super.viewWillDisappear(animated)
    }

    /// Off screen: drop the keys (iOS may keep this controller); they're rebuilt if it comes back.
    override func viewDidDisappear(_ animated: Bool) {
        super.viewDidDisappear(animated)
        view.subviews.forEach { $0.removeFromSuperview() }
        candidateRow.arrangedSubviews.forEach { $0.removeFromSuperview() }
        keyStack = nil
        keyButtons = []
        letterKeys = []
        keyConstraints = []
        shifted = false
        numbers = false
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
        config.background.cornerRadius = 6
        config.contentInsets = .zero
        config.titleAlignment = .center
        let button = UIButton(configuration: config)
        button.configurationUpdateHandler = { button in
            button.configuration?.background.backgroundColor = button.isHighlighted && !isCharacter ? Self.pressedKeyColor : Self.keyColor
        }
        keyButtons.append(button)
        button.accessibilityLabel = ["⇧": "shift", "⌫": "delete", "🌐": "next keyboard", "한あA": "switch language"][title] ?? title
        keyConstraints += [
            // units of a pitch, minus one gap: a row of 10 units spans the width inside the margins
            button.widthAnchor.constraint(equalTo: view.widthAnchor, multiplier: units / 10,
                                          constant: (Self.gap - 2 * Self.margin) * units / 10 - Self.gap),
            button.heightAnchor.constraint(equalToConstant: Self.bottomRow.contains(title) ? Self.keyHeight - Self.clipped : Self.keyHeight),
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
                feedback(title)
                if isCharacter { showPopup(over: button) }
                tap(title)
            }, for: .touchDown)
            if isCharacter {
                button.addAction(UIAction { [unowned self, unowned button] _ in
                    if popupKey === button { popup.isHidden = true }
                }, for: [.touchUpInside, .touchUpOutside, .touchCancel])
            }
            if title == "⌫" { repeatWhileHeld(button) }
        }
        return button
    }

    /// The pressed character, enlarged above the key, as on the system keyboard.
    private weak var popupKey: UIButton?
    private lazy var popup: UILabel = {
        let label = UILabel()
        label.font = .systemFont(ofSize: 32)
        label.textAlignment = .center
        label.layer.backgroundColor = Self.keyColor.resolvedColor(with: traitCollection).cgColor
        label.layer.cornerRadius = 8
        label.layer.shadowOpacity = 0.3
        label.layer.shadowRadius = 1
        label.layer.shadowOffset = CGSize(width: 0, height: 1)
        label.isHidden = true
        view.addSubview(label)
        return label
    }()

    private func showPopup(over key: UIButton) {
        popupKey = key
        let frame = key.convert(key.bounds, to: view)
        let width = frame.width + 16
        popup.text = key.configuration?.attributedTitle.map { String($0.characters) }
        popup.layer.backgroundColor = Self.keyColor.resolvedColor(with: view.traitCollection).cgColor
        popup.frame = CGRect(x: min(max(frame.midX - width / 2, 2), view.bounds.width - width - 2),
                             y: max(frame.minY - 52, 0), width: width, height: 48)
        if popup.superview == nil { view.addSubview(popup) }
        popup.isHidden = false
    }

    /// The system keyboard's click sounds (as azooKey found them: 1104 letters, 1155 delete, 1156
    /// other keys) and a light tap, on touch-down. Sound follows the silent switch; haptics need
    /// Full Access. Both can be turned off in the app.
    private static let haptic = UIImpactFeedbackGenerator(style: .light)
    private var sound = true, haptics = true

    private func feedback(_ title: String) {
        if sound {
            let id: SystemSoundID = title == "⌫" ? 1155 : title.count == 1 && title != "⇧" || title == "space" ? 1104 : 1156
            // played off the main thread: azooKey found it sometimes came out very loud otherwise
            Task.detached { AudioServicesPlaySystemSound(id) }
        }
        if haptics, hasFullAccess { Self.haptic.impactOccurred(intensity: 0.7) }
    }

    /// Delete repeats while held: after 0.4s, every 0.1s (azooKey's timing).
    private var repeating: Task<Void, Never>?

    private func repeatWhileHeld(_ button: UIButton) {
        button.addAction(UIAction { [unowned self] _ in
            repeating?.cancel()
            repeating = Task { @MainActor [weak self] in
                try? await Task.sleep(nanoseconds: 400_000_000)
                while !Task.isCancelled {
                    guard self != nil else { return }
                    self?.feedback("⌫")
                    self?.tap("⌫")
                    try? await Task.sleep(nanoseconds: 100_000_000)
                }
            }
        }, for: .touchDown)
        button.addAction(UIAction { [unowned self] _ in repeating?.cancel(); repeating = nil }, for: [.touchUpInside, .touchUpOutside, .touchCancel])
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
        case "space":
            // Kana with romaji left over (てh) goes in as English on Space: its typo fix too.
            let leftover = session.lastTrace?.decision.language == .japanese && client.marked.contains(where: \.isASCII)
            // A word straight after . / @ : (README.md, a URL, an address) isn't one to correct.
            let attached = textDocumentProxy.documentContextBeforeInput?.dropLast(client.marked.count).last
                .map { $0.isPunctuation || $0.isSymbol } ?? false
            if !attached, let fix = suggestions.autocorrect ?? (leftover ? Suggestions.english(word: session.keys, model: heuristic.english).autocorrect : nil) {
                TraceLog.write("AUTOCORRECT \(client.marked) → \(fix)")
                replaceWord(with: fix)
            }
            key = .printable(" ")
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
        Self.keyCount += 1
        TraceLog.write("KEY \(title) \(key)" + (Self.keyCount % 100 == 0 ? " #\(Self.keyCount) \(TraceLog.footprint)" : ""))
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
        guard !numbers, !session.keys.isEmpty, let keyStack, let point = keyStack.lastTouch,
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

    // MARK: - Candidates

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
        suggestions = Suggestions()
        let word = client.marked
        if candidates.isEmpty, !word.isEmpty, let trace = session.lastTrace, trace.keys == session.keys {
            var typed = word
            switch trace.decision.language {
            // The word in the language decided (the preview may still show another: てh for teh).
            case .korean: typed = HangulComposer.compose(keys: session.keys)?.text ?? word
                suggestions = .korean(word: typed, keys: session.keys, model: heuristic.korean)
            case .english: typed = session.keys
                suggestions = .english(word: typed, model: heuristic.english)
            default: break
            }
            if !suggestions.words.isEmpty {
                // The word as typed, then the suggestions; a tap puts the word in with a space.
                addCandidate("“\(typed)”", selected: false) { [unowned self] in
                    replaceWord(with: typed)
                    textDocumentProxy.insertText(" ")
                }
                for suggestion in suggestions.words {
                    addCandidate(suggestion, selected: suggestion == suggestions.autocorrect) { [unowned self] in
                        replaceWord(with: suggestion)
                        textDocumentProxy.insertText(" ")
                    }
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

    private var suggestions = Suggestions()

    /// Finish the session, replacing only the differing end of the displayed word.
    private func replaceWord(with text: String) {
        guard !client.marked.isEmpty,
              textDocumentProxy.documentContextBeforeInput?.hasSuffix(client.marked) == true else { return }
        session.commit(client: nil) // finish without replacing the displayed word with a new decision
        session.commit(client: nil) // and close the 한あA window on the word as typed
        client.insertText(text, replacing: nil)
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

    /// Where touches are counted from, relative to where they land. Traced touches landed 3-6pt
    /// below key centers while our rows sat 4pt above the system keyboard's; with the rows where
    /// the system puts them, the traced offset averages out to none.
    /// ponytail: one fixed shift; calibrate from the touch offsets traced with each key.
    static let touchShift = CGVector(dx: 0, dy: 0)

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
