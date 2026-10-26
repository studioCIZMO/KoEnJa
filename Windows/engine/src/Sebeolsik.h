#pragma once

#include <functional>
#include <optional>
#include <string>

#include "HangulComposer.h"

namespace rea {

/** The layout Korean is typed on: the TIP's HKCU\Software\KoEnJa\KoreanLayout ("dubeolsik",
 *  "sebeolsikFinal", "sebeolsik390"), Swift's `KoreanLayout`. Callers don't ask it directly:
 *  `HangulComposer::compose` and `DubeolsikLayout` answer for whichever layout is current. */
enum class KoreanLayout {
    /** 두벌식 (KS X 5002), with 도깨비불: a final consonant moves to the next syllable. */
    DUBEOLSIK,
    /** 세벌식 최종: separate keys for initial, medial and final jamo, on digits and punctuation too. */
    SEBEOLSIK_FINAL,
    /** 세벌식 390: 3-set's jamo with fewer finals of their own (ㄵ is ㄴ then ㅈ), QWERTY's symbols
     *  on the unshifted keys and the digits as a keypad under the right hand. */
    SEBEOLSIK_390,
};

/** What the session types with: set from the settings when they load (Swift's `KoreanLayout.active`).
 *  Not ported: Swift's `override` and `LatinLayout` seats; keys are QWERTY's. */
inline KoreanLayout currentKoreanLayout = KoreanLayout::DUBEOLSIK;

/** One of the 3-set layouts, which `Sebeolsik` types. */
inline bool isSebeolsik() { return currentKoreanLayout != KoreanLayout::DUBEOLSIK; }

/**
 * 세벌식 최종 and 390, as macOS's keyboard layouts type them, for the 3-set layout current. Jamo
 * are kept as Hangul Compatibility Jamo, as in `Jamo`, with the position the key gives them.
 * The keys that type a digit or symbol in Korean (on 최종 ⇧H 0, ⇧B ?, [ () have no jamo here: in a
 * word they type as on QWERTY, and a word with one is not Korean. Right after a Korean word they
 * type 3-set's symbol instead (`symbol`, asked by `InputSession`).
 */
namespace Sebeolsik {
    enum class Position { Initial, Medial, Final };

    struct Key {
        char16_t jamo;
        Position position;
    };

    /** The jamo on a key (shifted characters as typed: ! is the 1 key with Shift). */
    std::optional<Key> jamo(char16_t key);

    /** The digit or symbol 3-set types on [key] (as typed) where QWERTY types another. */
    std::optional<char16_t> symbol(char16_t key);

    /** ㅋ typed two times or more on 0 (ㅋㅋ): a laugh after Korean, a number anywhere else. */
    bool isLaughter(const std::u16string& keys);

    /** Shift on a key whose shifted form is a jamo (a final, or ㅒ) is Korean typing. */
    bool isKoreanShiftKey(char16_t key);

    /** A capital with no jamo on its key (⇧H is 0 in Korean): only ever English. */
    bool hasEnglishOnlyCapital(const std::u16string& keys);

    bool startsSyllable(char16_t key);

    /** Nullopt when a key has no jamo, or when every key is a digit: a number stays a number (05
     *  isn't 큐). Only 00 has its ㅋㅋ, as a reading to switch to. */
    std::optional<HangulComposer::Result> compose(const std::u16string& keys);

    /** The keys as jamo, digits alone too; nullopt when a key has none. */
    std::optional<HangulComposer::Result> composeJamo(const std::u16string& keys);

    /** Whether a digit or punctuation key, which reaches the session as a symbol, goes on with the
     *  word being typed as Korean instead of ending it (Swift's `KoreanLayout.continuesWord`). Only
     *  3-set has jamo there (ㅂ on ;, ㅆ on 2, ㅋ on 0), and only where the jamo makes a syllable
     *  people use ([knows]): 갔 (kf2) takes its 2; 안녕! and hello! don't. A word starts on one
     *  only with an initial consonant (ㅂ ; ㅌ ' ㅋ 0), and not with 0 right after a digit (100). */
    bool continuesWord(const std::u16string& keys, char16_t key, bool afterDigit, const std::function<bool(char16_t)>& knows);
}

} // namespace rea
