import Foundation
import Testing
@testable import SeamtypeCore

/// `Sebeolsik`'s tables against the keyboard layouts macOS's Korean input types with: every jamo on
/// its key and position, and every symbol that differs from QWERTY's. Skipped where they aren't.
@Suite struct SebeolsikKeylayoutTests {
    static let folder = "/System/Library/Input Methods/KoreanIM.app/Contents/PlugIns/KIM_Extension.appex/Contents/Resources/"

    /// ANSI key codes and the QWERTY key they are, unshifted.
    static let qwerty: [Int: Character] = [
        0: "a", 1: "s", 2: "d", 3: "f", 4: "h", 5: "g", 6: "z", 7: "x", 8: "c", 9: "v", 11: "b", 12: "q", 13: "w",
        14: "e", 15: "r", 16: "y", 17: "t", 18: "1", 19: "2", 20: "3", 21: "4", 22: "6", 23: "5", 24: "=", 25: "9",
        26: "7", 27: "-", 28: "8", 29: "0", 30: "]", 31: "o", 32: "u", 33: "[", 34: "i", 35: "p", 37: "l", 38: "j",
        39: "'", 40: "k", 41: ";", 42: "\\", 43: ",", 44: "/", 45: "n", 46: "m", 47: ".", 50: "`",
    ]
    static let shifted = Dictionary(uniqueKeysWithValues: zip("`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./",
                                                              "~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?"))

    /// What each QWERTY key (shifted ones as their character) types in `file`'s ANSI map.
    static func outputs(_ file: String) -> [Character: String]? {
        guard let text = try? String(contentsOfFile: folder + file, encoding: .utf8),
              let ansi = text.components(separatedBy: "<keyMapSet id=\"ANSI\">").dropFirst().first?
                .components(separatedBy: "</keyMapSet>").first else { return nil }
        var outputs: [Character: String] = [:]
        for index in 0...1 {
            guard let map = ansi.components(separatedBy: "<keyMap index=\"\(index)\">").dropFirst().first?
                .components(separatedBy: "</keyMap>").first else { return nil }
            for match in map.matches(of: #/<key code="(\d+)" output="([^"]*)"/#) {
                guard let code = Int(match.1), let key = qwerty[code] else { continue }
                let output = String(match.2)
                    .replacing(#/&#x([0-9A-Fa-f]+);/#) { String(Character(Unicode.Scalar(UInt32($0.1, radix: 16)!)!)) }
                    .replacingOccurrences(of: "&quot;", with: "\"")
                    .replacingOccurrences(of: "&lt;", with: "<").replacingOccurrences(of: "&gt;", with: ">")
                    .replacingOccurrences(of: "&apos;", with: "'").replacingOccurrences(of: "&amp;", with: "&")
                outputs[index == 0 ? key : shifted[key]!] = output
            }
        }
        return outputs
    }

    /// A conjoining jamo (ᄀ initial, ᅡ medial, ᆨ final) as `Sebeolsik` keeps it.
    static func jamo(_ output: String) -> Sebeolsik.Key? {
        guard let scalar = output.unicodeScalars.first, output.unicodeScalars.count == 1,
              (0x1100...0x11FF).contains(scalar.value) else { return nil }
        let position: Sebeolsik.Position = scalar.value < 0x1160 ? .initial : scalar.value < 0x11A8 ? .medial : .final
        // By name: HANGUL JONGSEONG KIYEOK is HANGUL LETTER KIYEOK (ᆨ is ㄱ).
        guard let name = scalar.properties.name?.replacing(#/ (CHOSEONG|JUNGSEONG|JONGSEONG) /#, with: " LETTER "),
              let letter = compatibilityJamo[name] else { return nil }
        return Sebeolsik.Key(jamo: letter, position: position)
    }

    static let compatibilityJamo: [String: Character] = Dictionary(uniqueKeysWithValues: (0x3131...0x3163).compactMap {
        guard let scalar = Unicode.Scalar($0), let name = scalar.properties.name else { return nil }
        return (name, Character(scalar))
    })

    @Test(arguments: [(KoreanLayout.sebeolsikFinal, "3SetHangul.keylayout"), (.sebeolsik390, "390Hangul.keylayout")])
    func matchesMacOS(_ layout: KoreanLayout, _ file: String) throws {
        guard let outputs = Self.outputs(file) else { return } // not on this system
        try KoreanLayout.$override.withValue(layout) {
            for (key, output) in outputs.sorted(by: { $0.key < $1.key }) {
                if let jamo = Self.jamo(output) {
                    #expect(Sebeolsik.jamo(onSeat: key) == jamo, "\(layout) \(key): \(output)")
                    #expect(Sebeolsik.symbol(for: key) == nil, "\(layout) \(key)")
                } else {
                    #expect(Sebeolsik.jamo(onSeat: key) == nil, "\(layout) \(key): \(output)")
                    // 최종's ⇧\ is \ as Korean keyboards print it, not macOS's full-width ￦.
                    let expected = layout == .sebeolsikFinal && key == "|" ? "\\" : output
                    let symbol = try #require(output.count == 1 ? Sebeolsik.symbol(for: key) ?? key : nil)
                    #expect(String(symbol) == expected, "\(layout) \(key)")
                }
            }
            #expect(outputs.count == 94)
        }
    }
}
