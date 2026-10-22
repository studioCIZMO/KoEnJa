#pragma once

#include <optional>
#include <string>
#include <vector>

#include "HangulComposer.h"

namespace rea {

/**
 * Syllable-unigram model for Korean: enough to tell "안녕하세요" from the accidental
 * Hangul that English produces on a 2-set layout (e.g. "the" → "솓").
 *
 * Not ported: `Source` (tiers or corpus alone); this is Swift's default, `.max`.
 */
class KoreanModel {
public:
    struct Score {
        /** Average log-probability per keystroke. */
        double averageLogProbability;
        HangulComposer::Result composition;
    };

    static constexpr double TIER1_LOG_P = -5.0;
    static constexpr double TIER2_LOG_P = -6.5;
    static constexpr double TIER3_LOG_P = -8.0;
    static constexpr double UNKNOWN_SYLLABLE_LOG_P = -11.0;
    static constexpr double STRAY_JAMO_LOG_P = -13.0;
    /** Typing the first consonant of the next syllable is normal mid-word. */
    static constexpr double PENDING_CONSONANT_LOG_P = -3.5;
    /** ㅋㅋ, ㅎㅎ, ㅠㅠ and friends. */
    static constexpr double CHAT_JAMO_LOG_P = -4.0;
    static constexpr const char16_t* CHAT_JAMO = u"ㅋㅎㅠㅜㄷㅇㄱㅅㅂㄴ";

    KoreanModel();

    /** A word that occurs in real Korean text (the composed Hangul, not the keys). */
    bool isKnownWord(const std::u16string& hangul) const;

    /** A word in the wider vocabulary (loanwords like 월드, 서버), for English + Korean splits. */
    bool isVocabularyWord(const std::u16string& hangul) const;

    double syllableLogProbability(char16_t syllable) const;

    /** [partial]: the token is still being typed, so a trailing lone consonant is expected rather than penalized. */
    std::optional<Score> score(const std::u16string& keys, bool partial = false) const;

private:
    static constexpr int SYLLABLES = 11172;
    /** Log-probability per syllable (index: syllable − 가), NaN where neither source has it. */
    std::vector<double> tiers;
};

} // namespace rea
