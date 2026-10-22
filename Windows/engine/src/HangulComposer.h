#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rea {

/**
 * Turns a sequence of 2-set keystrokes into Hangul, the same way a standard
 * 두벌식 automaton does (compound vowels, compound finals, final-consonant
 * migration to the next syllable).
 */
namespace HangulComposer {
    /** A syllable, or (`isSyllable` false) a jamo that never became part of a complete syllable. */
    struct Unit {
        char16_t character;
        bool isSyllable;
        bool operator==(const Unit&) const = default;
    };

    struct Result {
        std::vector<Unit> units;

        std::u16string text() const;
        std::u16string syllables() const;
        std::u16string strayJamo() const;
        /** Jamo that can't be Korean even mid-word: stray jamo other than a consonant just
         *  typed to start the next syllable (하느님ㅇ is 하느님의 on its way, 해ㅐ is not Korean). */
        bool isBroken() const;
        /** True when the last unit is a lone consonant, i.e. the user has just
         *  started the next syllable. Normal while typing, suspicious at a word end. */
        bool endsWithPendingConsonant() const;
        /** ㅋㅋ, ㅠㅠ, ㅜㅜ: two or more of the jamo typed on their own in chat. Korean as it is, and
         *  already complete: they don't go on to make syllables. (KoreanModel.swift's extension.) */
        bool isChat() const;
        /** Two or more loose consonants, or ㅜ/ㅠ: what chat abbreviates to (ㅁㅊ, ㅈㅅ, ㄹㅇ, ㅜㅜ).
         *  A reading worth switching to, though only the common ones ([isChat]) score as Korean. */
        bool isJamoAbbreviation() const;
    };

    /** Nullopt if any key is not a letter on the 2-set layout. */
    std::optional<Result> compose(const std::u16string& keys);

    Result composeJamo(const std::u16string& jamos);
}

} // namespace rea
