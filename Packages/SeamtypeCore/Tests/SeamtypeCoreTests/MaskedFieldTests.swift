import Foundation
import Testing
@testable import SeamtypeCore

/// Masked password fields that reach the input method get the keys as typed (`MaskedField`).
@Suite struct MaskedFieldTests {
    let noCaret = NSRange(location: NSNotFound, length: 0)
    let kakao: Int32 = 501, appStore: Int32 = 502, terminal: Int32 = 503

    @Test func kakaoTalksMaskedFieldPassesKeys() {
        #expect(MaskedField.passesKeys(secureInput: true, owner: kakao, frontmost: kakao, caret: noCaret))
    }

    /// Shown as plain text, KakaoTalk's field lets Secure Event Input go: words are decided.
    @Test func thePlainFieldDecidesWords() {
        #expect(!MaskedField.passesKeys(secureInput: false, owner: nil, frontmost: kakao, caret: noCaret))
    }

    /// App Store's sheet is a PassKit extension; App Store, in front, holds Secure Event Input.
    @Test func appStoresSheetPassesKeys() {
        #expect(MaskedField.passesKeys(secureInput: true, owner: appStore, frontmost: appStore, caret: noCaret))
    }

    /// A terminal with Secure Keyboard Entry holds it but reports its caret.
    @Test func aTerminalWithSecureKeyboardEntryDecidesWords() {
        #expect(!MaskedField.passesKeys(secureInput: true, owner: terminal, frontmost: terminal,
                                        caret: NSRange(location: 42, length: 0)))
    }

    /// Held by an app behind the one being typed in: not this field's.
    @Test func secureInputHeldBehindDecidesWords() {
        #expect(!MaskedField.passesKeys(secureInput: true, owner: terminal, frontmost: kakao, caret: noCaret))
    }

    /// No owner to compare (the system didn't say): words are decided, as before.
    @Test func anUnknownOwnerDecidesWords() {
        #expect(!MaskedField.passesKeys(secureInput: true, owner: nil, frontmost: kakao, caret: noCaret))
        #expect(!MaskedField.passesKeys(secureInput: true, owner: kakao, frontmost: nil, caret: noCaret))
    }
}
