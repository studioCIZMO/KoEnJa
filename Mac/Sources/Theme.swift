import SeamtypeCore
import Cocoa
import SwiftUI

/// KoEnJa's palette, as its website (cizmo.net/KoEnJa): neutral paper and ink, green for actions,
/// blue for Korean and amber for Japanese, with the dark counterpart. Each color follows the
/// window's appearance.
enum Seam {
    static let bg = dynamic(0xf4f4f1, 0x131413)
    static let bg2 = dynamic(0xebebe7, 0x1a1b1c)
    static let card = dynamic(0xffffff, 0x1e1f21)
    static let ink = dynamic(0x1b1c1e, 0xf2f2ee)
    static let ink2 = dynamic(0x6b7076, 0xa3a8ae)
    static let ink3 = dynamic(0x666b71, 0x80858b)
    static let line = dynamic(0xe5e5e1, 0x2f3033)
    /// Actions and choices (the website's primary green).
    static let blue = dynamic(0x1e7a63, 0x4fb397)
    /// The dot on the chosen item.
    static let yellow = dynamic(0x1e7a63, 0x4fb397)
    /// The mark's plate, as the app icon's.
    static let plate = dynamic(0xdadad6, 0x2a2b2d)
    static let english = dynamic(0x9a9fa5, 0x8a9096)
    static let korean = dynamic(0x2b5a9e, 0x86aeea)
    static let japanese = dynamic(0xd9822b, 0xe9a65c)
    /// Japanese as text, darker to read on paper.
    static let japaneseInk = dynamic(0x8a4207, 0xe9a65c)
    static let warning = dynamic(0xb45309, 0xf59e0b)

    private static func dynamic(_ light: UInt32, _ dark: UInt32, darkAlpha: CGFloat = 1) -> Color {
        Color(nsColor: NSColor(name: nil) { appearance in
            appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
                ? rgb(dark, alpha: darkAlpha) : rgb(light, alpha: 1)
        })
    }

    private static func rgb(_ hex: UInt32, alpha: CGFloat) -> NSColor {
        NSColor(srgbRed: CGFloat(hex >> 16 & 0xff) / 255, green: CGFloat(hex >> 8 & 0xff) / 255,
                blue: CGFloat(hex & 0xff) / 255, alpha: alpha)
    }
}

extension Language {
    /// The language's color on the website: its underline in the typing demo.
    var tint: Color {
        switch self {
        case .english: return Seam.english
        case .korean: return Seam.korean
        case .mixed: return Seam.ink3
        case .japanese: return Seam.japanese
        }
    }

    /// The color for the language's name as text.
    var ink: Color { self == .japanese ? Seam.japaneseInk : tint }
}

// MARK: - Pane pieces

/// A settings pane: a scrolling column on the paper color, its title line first. About opens with
/// the mark itself instead.
struct Pane<Content: View>: View {
    let tab: SettingsTab
    var showsHeader = true
    @ViewBuilder let content: Content

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                if showsHeader { PaneHeader(tab: tab) }
                content
            }
            .padding(.horizontal, 28).padding(.top, 30).padding(.bottom, 28)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .background(Seam.bg)
    }
}

/// The pane's title and, after it on the line, what it's for.
struct PaneHeader: View {
    let tab: SettingsTab

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 10) {
            Text(tab.title)
                .font(.system(size: 20, weight: .bold)).tracking(-0.3)
                .foregroundStyle(Seam.ink)
                .accessibilityAddTraits(.isHeader)
            Text(tab.subtitle)
                .font(.system(size: 12.5)).foregroundStyle(Seam.ink2)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

/// The mark where it is small: 韓英日 doesn't read under about 40 pt, so the plate holds 한영일.
struct SmallMark: View {
    var height: CGFloat = 20

    var body: some View {
        RoundedRectangle(cornerRadius: height * 0.28, style: .continuous)
            .fill(Seam.plate)
            .overlay {
                Text(verbatim: "한영일")
                    .font(.system(size: height * 0.5, weight: .heavy))
                    .foregroundStyle(Seam.ink)
            }
            .frame(width: height * 2.4, height: height)
            .accessibilityHidden(true)
    }
}

/// A word with the website's language underline.
struct Underlined: View {
    let text: String
    let color: Color
    var size: CGFloat = 22

    var body: some View {
        Text(verbatim: text)
            .font(.system(size: size, weight: .bold))
            .foregroundStyle(Seam.ink)
            .padding(.bottom, 3)
            .overlay(alignment: .bottom) { Capsule().fill(color).frame(height: 3) }
    }
}

/// A titled group of rows on a white card, as the website's cards.
struct SeamCard<Content: View>: View {
    var title: String? = nil
    @ViewBuilder let content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if let title {
                Text(title)
                    .font(.system(size: 12, weight: .semibold)).foregroundStyle(Seam.ink2)
                    .padding(.leading, 2)
                    .accessibilityAddTraits(.isHeader)
            }
            VStack(alignment: .leading, spacing: 0) {
                _VariadicView.Tree(Divided()) { content }
            }
            .background(Seam.card, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous).strokeBorder(Seam.line))
        }
    }

    /// A hairline between the card's rows.
    private struct Divided: _VariadicView_MultiViewRoot {
        func body(children: _VariadicView.Children) -> some View {
            let last = children.last?.id
            ForEach(children) { child in
                child
                if child.id != last { Rectangle().fill(Seam.line).frame(height: 1).padding(.leading, 16) }
            }
        }
    }
}

/// One setting: its name and what it does at the left, the control at the right.
struct SeamRow<Trailing: View>: View {
    let title: String
    var detail: String? = nil
    @ViewBuilder let trailing: Trailing

    var body: some View {
        HStack(alignment: .center, spacing: 16) {
            RowLabel(title: title, detail: detail)
            Spacer(minLength: 8)
            trailing
        }
        .padding(.horizontal, 16).padding(.vertical, 12)
    }
}

/// A row's name and the line under it.
struct RowLabel: View {
    let title: String
    var detail: String? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title).font(.system(size: 13, weight: .medium)).foregroundStyle(Seam.ink)
            if let detail {
                Text(detail).font(.system(size: 12)).foregroundStyle(Seam.ink2)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }
}

/// A row with a switch.
struct SeamToggleRow: View {
    let title: String
    var detail: String? = nil
    @Binding var isOn: Bool

    var body: some View {
        SeamRow(title: title, detail: detail) {
            Toggle(title, isOn: $isOn).toggleStyle(.switch).labelsHidden().controlSize(.small)
        }
    }
}

/// A small rounded label in a color of the palette.
struct Badge: View {
    let title: String
    let fill: Color
    let ink: Color

    var body: some View {
        Text(title)
            .font(.system(size: 11, weight: .semibold))
            .lineLimit(1).fixedSize()
            .foregroundStyle(ink)
            .padding(.horizontal, 7).padding(.vertical, 2)
            .background(fill, in: Capsule())
    }
}
