#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "Text.h"

namespace rea {

/**
 * Hangul jamo tables and the 2-set (두벌식) QWERTY layout.
 *
 * All jamo are represented as Hangul Compatibility Jamo (U+3131…U+3163),
 * which is also what gets emitted for stray, uncombined jamo.
 */
namespace Jamo {
    const std::unordered_map<std::u16string, char16_t>& vowelCombos();
    const std::unordered_map<std::u16string, char16_t>& finalCombos();
    /** Inverse of [finalCombos]: compound final → (stays as final, moves to next syllable). */
    const std::unordered_map<char16_t, std::pair<char16_t, char16_t>>& finalSplits();

    bool isConsonant(char16_t j);
    bool isVowel(char16_t j);
    bool isFinal(char16_t j);
    char16_t syllable(char16_t cho, char16_t jung, std::optional<char16_t> jong);
}

namespace DubeolsikLayout {
    /** Keys whose shifted form produces a different jamo. Shift on any other
     *  letter is a signal the user is typing English. */
    bool isKoreanShiftKey(char16_t key);

    /** The jamo a key produces, or nullopt for non-letters.
     *  Uppercase letters without a shifted jamo fall back to the lowercase jamo,
     *  mirroring how the macOS 2-set keyboard behaves. */
    std::optional<char16_t> jamo(char16_t key);
}

} // namespace rea
