import SeamtypeCore
import SwiftUI

extension SwitchKey {
    /// The key's name in a sentence or a menu ("Right ⌥ tap").
    var displayName: String {
        switch self {
        case .rightOption: return L("Right ⌥ tap")
        case .optionSpace: return L("⌥Space")
        case .rightShift: return L("Right ⇧ tap")
        case .escape: return "Esc"
        case .shiftEscape: return "⇧Esc"
        }
    }

    /// What is printed on the keycap, as on a Mac keyboard.
    var glyph: String {
        switch self {
        case .rightOption: return "⌥"
        case .optionSpace: return "⌥ space"
        case .rightShift: return "⇧"
        case .escape: return "esc"
        case .shiftEscape: return "⇧ esc"
        }
    }

    /// Under the glyph: which of the two and how (a tap of the right one alone).
    var caption: String? {
        switch self {
        case .rightOption, .rightShift: return L("right, alone")
        case .optionSpace, .escape, .shiftEscape: return nil
        }
    }

    /// One line on what choosing it means, for its tooltip.
    var help: String {
        switch self {
        case .rightOption: return L("Tap the right ⌥ on its own. Apps ignore it, so nothing else happens.")
        case .optionSpace: return L("⌥ and Space together. It no longer types a no-break space.")
        case .rightShift: return L("Tap the right ⇧ on its own. Apps ignore it, so nothing else happens.")
        case .escape: return L("Quick, but Chromium apps (Slack, Claude) also see the Esc and may close or clear.")
        case .shiftEscape: return L("With nothing to change, it does nothing.")
        }
    }

    /// The keys by the names the keycaps and menus use ("Right ⌥ tap, ⇧Esc").
    static func names(_ keys: Set<SwitchKey>) -> String {
        keys.isEmpty ? L("None") : sorted(keys).map(\.displayName).joined(separator: ", ")
    }

    /// The keys in the settings' order.
    static func sorted(_ keys: Set<SwitchKey>) -> [SwitchKey] { allCases.filter(keys.contains) }
}

extension PauseKey {
    /// "right ⌘", "⌃⌥P", "Off": for the input menu and the settings.
    var label: String {
        switch self {
        case .off: return L("Off")
        case .tap(let tap): return L("right %@", tap.glyph)
        case .shortcut(let shortcut): return shortcut.display
        }
    }

    /// "Right ⌘ tap", "⌃⌥P", "Off": in the settings' pop-up, named as its items are.
    var title: String {
        switch self {
        case .tap(let tap): return L("Right %@ tap", tap.glyph)
        case .off, .shortcut: return label
        }
    }
}

/// A key as a keycap named as the rest of the window names it: a chosen one is filled with the
/// website's blue and checked, so on and off read without knowing the color.
struct Keycap: View {
    let key: SwitchKey
    let on: Bool
    var hovering = false
    var pressed = false
    @Environment(\.isEnabled) private var isEnabled
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: 7, style: .continuous)
        HStack(spacing: 4) {
            if on { Image(systemName: "checkmark").font(.system(size: 9, weight: .heavy)) }
            Text(verbatim: key.displayName).lineLimit(1)
        }
        .font(.system(size: 12, weight: .semibold))
        .foregroundStyle(on ? Color.white : Seam.ink2)
        .padding(.horizontal, 10).padding(.vertical, 5)
        .background {
            ZStack {
                // The key's lower edge; it sinks while pressed.
                shape.fill(on ? Seam.ink.opacity(0.35) : Seam.line).offset(y: pressed ? 0 : 2)
                shape.fill(on ? Seam.blue : Seam.card)
            }
        }
        .overlay {
            shape.strokeBorder(on ? Color.clear : (contrast == .increased ? Seam.ink : Seam.line))
        }
        .overlay { if hovering && !on { shape.fill(Seam.ink.opacity(0.04)) } }
        .offset(y: pressed ? 1.5 : 0)
        .padding(.bottom, 2)
        .opacity(isEnabled ? 1 : 0.45)
    }
}

/// Keycaps to turn on and off, one per switch key.
struct KeycapToggles: View {
    let selection: Set<SwitchKey>
    /// Taken by another key (the pause key): can't be turned on.
    var taken: SwitchKey? = nil
    let set: (SwitchKey, Bool) -> Void

    var body: some View {
        HStack(spacing: 8) {
            ForEach(SwitchKey.allCases, id: \.self) { key in
                Toggle(key.displayName, isOn: Binding(get: { selection.contains(key) }, set: { set(key, $0) }))
                    .toggleStyle(KeycapToggleStyle(key: key))
                    .disabled(key == taken && !selection.contains(key))
            }
        }
    }
}

struct KeycapToggleStyle: ToggleStyle {
    let key: SwitchKey

    func makeBody(configuration: Configuration) -> some View {
        KeycapButton(key: key, isOn: configuration.$isOn)
    }

    private struct KeycapButton: View {
        let key: SwitchKey
        @Binding var isOn: Bool
        @State private var hovering = false
        /// Off only while the pause key is this key.
        @Environment(\.isEnabled) private var isEnabled

        var body: some View {
            Button { isOn.toggle() } label: { EmptyView() }
                .buttonStyle(Press(key: key, on: isOn, hovering: hovering))
                .onHover { hovering = $0 }
                .help(isEnabled ? key.help : L("This key pauses. Choose another pause key first."))
                .accessibilityLabel(key.displayName)
                .accessibilityValue(isOn ? L("On") : L("Off"))
                .accessibilityAddTraits(isOn ? .isSelected : [])
                .animation(.snappy(duration: 0.18), value: isOn)
        }
    }

    private struct Press: ButtonStyle {
        let key: SwitchKey
        let on: Bool
        let hovering: Bool

        func makeBody(configuration: Configuration) -> some View {
            Keycap(key: key, on: on, hovering: hovering, pressed: configuration.isPressed)
                .contentShape(Rectangle())
        }
    }
}
