#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace rea {

/**
 * Romaji → hiragana, following the conventions of the macOS and Microsoft
 * Japanese input methods (nn/n' → ん, doubled consonants → っ, x/l small kana,
 * - → ー). Letters that can't become kana are kept and counted: like stray jamo
 * for Hangul, they are the strongest sign the user wasn't typing Japanese.
 */
namespace RomajiComposer {
    struct Result {
        /** Kana with any unconvertible letters left in place (こんにc while typing). */
        std::u16string text;
        /** Letters in the middle or (when finished) at the end that never became kana. */
        int strayLetters;
        /** Letters at the end still waiting to become kana ("k", "ky", "n"); only when partial. */
        std::u16string pending;
        /** Number of kana produced. */
        int kanaCount;

        bool isClean() const { return strayLetters == 0 && pending.empty(); }
    };

    /** [partial]: the word is still being typed, so a trailing "k" or "n" is waiting for its vowel rather than stray. */
    Result compose(const std::u16string& keys, bool partial = false);

    /** Hiragana → katakana (for F7-style katakana output and loanwords). */
    std::u16string katakana(const std::u16string& hiragana);

    /** A spelling that composes back to [kana] (し → "si", ん → "n'", っ → "ltu"), or nullopt if
     *  some character has none. For keys rewritten after Delete took off a whole kana. */
    std::optional<std::u16string> romaji(const std::u16string& kana);

    /** Symbols as Japanese IMEs type them by default: full-width, with
     *  [ ] / ~ - as 「」・〜ー. Not ' (it ends ん in kan'i), and digits and spaces stay half-width. */
    const std::unordered_map<char16_t, std::u16string>& symbols();

    /** What the symbols become, except ー, which is part of a word (らーめん) rather than punctuation. */
    const std::unordered_set<char16_t>& punctuation();

    /** 0–9 as ０–９, for a Japanese phrase with a number in it. */
    const std::unordered_map<char16_t, std::u16string>& fullWidthDigits();
    /** After a digit, - . , / belong to the number (addresses, phone numbers, dates), not ー。、・. */
    const std::unordered_map<char16_t, std::u16string>& numberPunctuation();

    const std::unordered_map<std::u16string, std::u16string>& table();
}

} // namespace rea
