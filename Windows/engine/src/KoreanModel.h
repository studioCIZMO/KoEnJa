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
    /** The same for a consonant that joined the syllable before as its final (갷 of 소개ㅎ),
     *  a little less sure: alr is 미국 on its way and also already (tuned on preview traces of
     *  the top 10,000 English and Korean words). */
    static constexpr double PENDING_FINAL_PENALTY = 1.0;
    /** ㅋㅋ, ㅎㅎ, ㅠㅠ and friends. */
    static constexpr double CHAT_JAMO_LOG_P = -4.0;
    static constexpr const char16_t* CHAT_JAMO = u"ㅋㅎㅠㅜㄷㅇㄱㅅㅂㄴ";

    KoreanModel();

    /** A word that occurs in real Korean text (the composed Hangul, not the keys). */
    bool isKnownWord(const std::u16string& hangul) const;

    /** A word in the wider vocabulary (loanwords like 월드, 서버; 혜택), for English + Korean splits
     *  and the heuristic's Korean word floor. */
    bool isVocabularyWord(const std::u16string& hangul) const;

    /** A word the subtitles have fewer than [COMMON_COUNT] times (촉매, twice): for the heuristic's
     *  Korean word floor only. The tail holds dialect and typos too (어뗘, 쓔벌, 꺼이), so it
     *  stays out of the splits and of [knowsSyllable], which `EU정책` and `RJ이` rely on. */
    bool isRareVocabularyWord(const std::u16string& hangul) const;

    /** A noun the lists only have with a case particle (혜택 from 혜택을, 혜택으로): they
     *  count words with their particles, so a bare noun can be missing from both. */
    bool isKnownStem(const std::u16string& hangul) const;

    /** Where `korean_vocabulary.txt` stops being the vocabulary: the old 50k list's last count. */
    static constexpr int COMMON_COUNT = 10;

    double syllableLogProbability(char16_t syllable) const;

    /** A syllable seen at all: in the tiers, the corpus or a word of the lists, however rarely
     *  (삐, 쪄, 뾰, 쭈 are; 뗘, 쎠, 쓔 aren't). Rarer than [UNKNOWN_SYLLABLE_LOG_P] still means
     *  someone typed it. */
    bool knowsSyllable(char16_t syllable) const;

    /** [knowsSyllable], or in a word the subtitles have only a few times (왑 of 나왑, 챕 of 챕터들,
     *  탭): for 3-set's digit and symbol keys, which go on with the word only into a syllable
     *  someone types. A key there can only add a final or a vowel, never make 뗘, so the tail's
     *  dialect and typos matter less than its loanwords. */
    bool knowsSyllableIncludingRare(char16_t syllable) const;

    /** [partial]: the token is still being typed, so a trailing lone consonant is expected rather than penalized. */
    std::optional<Score> score(const std::u16string& keys, bool partial = false) const;

private:
    static constexpr int SYLLABLES = 11172;
    /** Log-probability per syllable (index: syllable − 가), NaN where neither source has it. */
    std::vector<double> tiers;

    double logProbability(const HangulComposer::Result& composition, bool partial) const;
};

} // namespace rea
