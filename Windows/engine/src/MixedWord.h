#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "DecisionEngine.h"

namespace rea {

class ParticleSplitter;

/**
 * An English word with a Korean particle or ending typed straight after it,
 * e.g. `githubdp` → `github에`, `APIfmf` → `API를`, `commitgotj` → `commit해서`.
 */
struct MixedWord {
    std::u16string english;
    std::u16string koreanKeys;
    std::u16string korean;

    std::u16string text() const { return english + korean; }

    /** Particles and endings that commonly attach directly to a (loan)word. */
    static const std::unordered_set<std::u16string>& suffixes();

    /** Particles that follow a single letter (x에, n이, a를, X축의 X): case particles only, not
     *  endings like 해/한, so short English (ago) doesn't split. */
    static const std::unordered_set<std::u16string>& singleLetterParticles();

    /** A single letter and a particle: `xdp` → `x에`, `Xfmf` → `X를`. One key on its own can't
     *  make a Hangul syllable before another consonant, so the keys never read as Korean. */
    static std::optional<MixedWord> splitSingleLetter(const std::u16string& keys);

    /** Splits off the longest Korean particle/ending at the end of [keys].
     *  Only the shape is checked here; whether the front really is English is
     *  up to the caller (see [SplittingEngine]). */
    static std::optional<MixedWord> split(const std::u16string& keys);
};

namespace DubeolsikLayout {
    /** A Korean syllable has to start with a consonant key. */
    bool startsSyllable(char16_t key);
}

/**
 * Decides `mixed` for English and Korean typed as one token: English + particle (github에),
 * a single letter + particle (x에), English + Korean word (hello월드), and with Japanese on
 * English + Japanese particle (githubで). Never when the whole token is a known English word or
 * reads as ordinary Korean (every syllable is one people actually use).
 */
class SplittingEngine : public DecisionEngine {
public:
    /** How sure a split is: above Strict (AppPolicy::STRICT_THRESHOLD), since each rule already
     *  asks for an English front it can stand on, so github에 splits in a terminal too. */
    static constexpr double CONFIDENCE = 0.98;

    std::shared_ptr<DecisionEngine> base;
    const EnglishModel& english;
    std::shared_ptr<KoreanModel> korean;
    /** With Japanese on: English word + Japanese particle (githubで), decided the same way. */
    std::shared_ptr<JapaneseSegmenter> japanese;

    /** English word + Korean word (hello월드) only when the whole token spells less like
     *  English than this; stricter still when the Korean half isn't in the word lists. */
    double mixedWordSpellingCeiling = -2.7;
    double unknownWordSpellingCeiling = -3.2;

    /** Scores English word + particle against the whole token. */
    std::shared_ptr<ParticleSplitter> particles;

    SplittingEngine(std::shared_ptr<DecisionEngine> base, const EnglishModel& english,
                    std::shared_ptr<KoreanModel> korean = std::make_shared<KoreanModel>(),
                    std::shared_ptr<JapaneseSegmenter> japanese = nullptr);

    Decision decide(const std::u16string& keys, const DecisionContext& context) override;

    /** An English word with a whole Korean word typed straight after it, no space:
     *  `hellodnjfem` → `hello월드`, `designvhsxm` → `design폰트`. The front has to be a known
     *  English word (or a name typed with capitals) and the back at least two syllables:
     *  a known Korean word (optionally with a particle), or, when the whole token spells
     *  nothing like English, any Hangul made of syllables people use. Prefers the longest
     *  English front. Names that spell like English stay whole (cartman isn't car스무). */
    std::optional<MixedWord> englishThenKoreanWord(const std::u16string& keys);

    /** UI, AI, OS, API: capitals typed on purpose, with at least one that has no shifted
     *  jamo (so it can't be ㅃㅉㄸㄲㅆㅒㅖ). Short ones count too (UI같은건). */
    static bool isAcronym(const std::u16string& front);

    /** Slack, TextEdit, VSCode, GitHub: a name typed with its capitals, so English even when
     *  it isn't in the word list. Starts with a capital, has lowercase too, and has either a
     *  capital with no shifted jamo or another capital inside (PascalCase). Korean typed with
     *  shift (TkdRjvnf, 쌍꺼풀) also passes the second test; `readsAsKorean` still stops it. */
    static bool isCapitalizedName(const std::u16string& front);

    static bool hasEnglishOnlyCapital(const std::u16string& front);

private:
    bool isKoreanWordOrWordWithParticle(const std::u16string& hangul) const;
    bool readsAsKorean(const std::u16string& keys) const;
};

} // namespace rea
