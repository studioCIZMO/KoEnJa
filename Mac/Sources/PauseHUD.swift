import Cocoa

/// Araso's icon shown for a moment under the caret when it pauses (struck through) or
/// resumes in a text field. The menu bar icon can't show it (the bundle is signed, so the icon
/// is fixed). It looks like the system's own input source indicator (macOS 26 and later): a
/// white icon on an accent-colored capsule, centered under the caret. The struck-through icon
/// is drawn at build time (scripts/make-icon.swift --slash).
@MainActor enum PauseHUD {
    private static var panel: NSPanel?
    private static var imageView: NSImageView?
    /// Bumped on every show, so an older fade-out leaves a newer icon alone.
    private static var shown = 0

    /// The system indicator's capsule (the icon in it is the menu bar icon at its own size).
    private static let size = NSSize(width: 30, height: CaretPanel.indicatorHeight)

    private static let icons: (on: NSImage, paused: NSImage)? = {
        func load(_ name: String) -> NSImage? {
            let image = Bundle.main.url(forResource: name, withExtension: "tiff").flatMap(NSImage.init(contentsOf:))
            image?.isTemplate = true
            return image
        }
        guard let on = load("KoEnJa"), let paused = load("KoEnJaPaused") else { return nil }
        return (on, paused)
    }()

    /// `caret` is the caret's line in screen coordinates, or `.zero` when the app doesn't say.
    static func show(paused: Bool, caret: NSRect) {
        guard let icons else { return } // not built into an app bundle (development)
        let (panel, imageView) = make()
        imageView.image = paused ? icons.paused : icons.on
        imageView.setAccessibilityLabel(paused ? L("KoEnJa paused") : L("KoEnJa resumed"))
        panel.contentView?.layer?.backgroundColor = CaretPanel.indicatorFill.cgColor
        panel.setFrame(NSRect(origin: CaretPanel.origin(for: size, caret: caret, gap: CaretPanel.indicatorGap, centered: true), size: size),
                       display: true)
        panel.alphaValue = 1
        panel.orderFrontRegardless()

        shown += 1
        let current = shown
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) {
            guard current == shown else { return }
            NSAnimationContext.runAnimationGroup({ $0.duration = 0.25; panel.animator().alphaValue = 0 }) {
                MainActor.assumeIsolated { if current == shown { panel.orderOut(nil) } }
            }
        }
    }

    private static func make() -> (NSPanel, NSImageView) {
        if let panel, let imageView { return (panel, imageView) }
        let panel = CaretPanel.make(size: size)

        let capsule = NSView()
        capsule.wantsLayer = true
        capsule.layer?.cornerRadius = size.height / 2
        capsule.layer?.cornerCurve = .continuous

        let imageView = NSImageView()
        imageView.contentTintColor = .white
        imageView.imageScaling = .scaleNone
        imageView.translatesAutoresizingMaskIntoConstraints = false
        capsule.addSubview(imageView)
        NSLayoutConstraint.activate([
            imageView.centerXAnchor.constraint(equalTo: capsule.centerXAnchor),
            imageView.centerYAnchor.constraint(equalTo: capsule.centerYAnchor),
        ])
        panel.contentView = capsule
        self.panel = panel
        self.imageView = imageView
        return (panel, imageView)
    }
}
