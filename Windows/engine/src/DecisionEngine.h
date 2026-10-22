#pragma once

#include <memory>
#include <optional>
#include <string>

#include "EnglishModel.h"
#include "JapaneseModel.h"
#include "KoreanModel.h"

namespace rea {

class JapaneseSegmenter;

enum class Language {
    ENGLISH,
    KOREAN,
    /** English word + Korean particle/ending, e.g. `github에` (see [MixedWord]). */
    MIXED,
    /** Romaji typed as Japanese (rendered as hiragana; kanji conversion happens on Space). */
    JAPANESE,
};

/** The language a sentence continues in after this word. */
Language sentenceLanguage(Language language);

/** Swift's raw value: the name used in explanations and the learning file. */
std::u16string raw(Language language);

/** What Esc switches to: english → mixed (when the keys split) → korean →
 *  japanese (when enabled) → english. Readings that can't exist for these keys
 *  are skipped (no Korean for "ra-men"), and so are garbled ones (dkssud as
 *  Japanese: dkっすd; loose consonants and ㅜ/ㅠ such as ㅁㅊ or ㅜㅜ aren't garbled) as long as a clean alternative exists — a typo can still
 *  be forced into its language when nothing reads cleanly. [splits]: whether the engine
 *  splits the keys (x에, hello월드 as well as github에); left out, the particle split decides. */
Language nextLanguage(Language language, const std::u16string& keys, bool japanese = false, std::optional<bool> splits = std::nullopt);

/** What the engine knows besides the token itself. */
struct DecisionContext {
    /** Language of the previously committed word, if it is recent enough to matter. */
    std::optional<Language> previousLanguage;
    /** The token is still being typed (used for the live preview). */
    bool partial = false;
    /** The language the user corrected these keys to once (a weak dictionary entry). */
    std::optional<Language> nudge;
};

struct Decision {
    Language language;
    /** 0.5 = coin flip, 1.0 = certain. */
    double confidence;
    /** What should be committed for this decision. */
    std::u16string output;
    /** Human-readable reasoning, for debugging. */
    std::u16string explanation;
    /** The second most likely language, when the engine compared several. */
    std::optional<Language> runnerUp;
};

/** A local decider that maps raw QWERTY keystrokes of one word to a language. */
class DecisionEngine {
public:
    virtual ~DecisionEngine() = default;
    virtual Decision decide(const std::u16string& keys, const DecisionContext& context) = 0;

    /** Renders [keys] in a given language regardless of what the engine would pick. */
    virtual std::u16string render(const std::u16string& keys, Language language);
};

/**
 * Scores the same keystrokes as English (character trigrams + word list), Korean
 * (2-set syllables) and, when enabled, Japanese (romaji → kana trigrams), each as
 * average log-probability per keystroke, nudges the previous word's language, and
 * picks the best. Confidence comes from the gap to the runner-up.
 */
class HeuristicEngine : public DecisionEngine {
public:
    const EnglishModel& english;
    std::shared_ptr<KoreanModel> korean;
    /** Null when Japanese is off or its model isn't available. */
    const JapaneseModel* japanese;

    /** Added to the previous word's language score: people mostly stay in one language. */
    double contextBias = 0.8;
    /** Bias after Japanese text. Raising it to 1.2 recovered 1 of 2,590 held-out phrases
     *  but tripled common English words turning into kana right after Japanese (0.5% → 1.6%). */
    double japaneseContextBias = 0.8;
    /** Shifts Japanese scores to the English/Korean scale (kana trigrams over ~80 symbols
     *  per ~2 keys read differently from letter trigrams). Tuned on held-out data so that
     *  English words almost never turn into kana. */
    double japaneseOffset = 0.5;
    /** Shifts Korean scores (used when the syllable model changes scale). */
    double koreanOffset = 0.0;
    /** Taken off the dictionary floor for rarer English words (SCOWL levels 40–50: dork,
     *  ahem), so a common Korean word with the same keys (애가, 모든) wins the tie. */
    double rareWordFloorPenalty = 0.3;
    /** The previous word's pull is scaled by this when the keys are a known word of another
     *  language and not of the previous one (내가 after English, `for` after Korean): that's
     *  someone switching language, not a stray reading. */
    double switchBiasFactor = 0.25;
    /** Score floors for a Korean word seen in real text (two or more syllables, one), the
     *  counterpart of the English dictionary floor: 내가, 무슨 stay Korean after English.
     *  A word only in the wider vocabulary (닥터, 했죠: too rare for Tatoeba's sentences), or a
     *  noun the lists have only with a particle (혜택), gets the long floor too, unless the keys
     *  are an English word: that list comes from subtitles, and work (재가) or ahem (모드) must
     *  stay English. */
    double koreanKnownFloorLong = -1.4;
    double koreanKnownFloorShort = -2.0;
    /** How far Japanese must lead before a known English word is turned into kana
     *  outside Japanese text. */
    double englishWordProtection = 1.0;
    /** Added to the language the user corrected the keys to once (DecisionContext::nudge):
     *  as much as the previous word's pull, so a close call turns and a clear one doesn't. */
    double correctionBias = 0.8;

    std::shared_ptr<JapaneseSegmenter> segmenter;

    HeuristicEngine(const EnglishModel& english = EnglishModel::shared(),
                    std::shared_ptr<KoreanModel> korean = std::make_shared<KoreanModel>(),
                    const JapaneseModel* japanese = nullptr);

    Decision decide(const std::u16string& keys, const DecisionContext& context) override;
};

} // namespace rea
