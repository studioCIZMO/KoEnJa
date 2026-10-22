#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

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

    /** Particles with a form after a final consonant and one after a vowel (책을, 사과를): the
     *  consonant form first. A final ㄹ takes the vowel form of 으로 (서울로). */
    static const std::vector<std::pair<std::u16string, std::u16string>>& pairedParticles();

    enum class FinalSound { Vowel, Consonant, Rieul };

    /** Whether [particle] is the form that follows a word ending as [final] describes:
     *  nullopt for a particle that has one form (에, 도, 의), else true or false (씨이, 쓰은 are wrong). */
    static std::optional<bool> particleAgrees(const std::u16string& particle, FinalSound final);

    /** How a Hangul syllable ends; nullopt for anything else. */
    static std::optional<FinalSound> finalSound(char16_t syllable);

    /** How an acronym ends when read letter by letter: L, M, N and R (엘, 엠, 엔, 알) on a final
     *  consonant, every other letter's name on a vowel (TM은, TV는). */
    static FinalSound finalSoundOfAcronym(const std::u16string& acronym);

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

    /** Scores English word + particle, and English + Korean word, against the whole token. */
    std::shared_ptr<ParticleSplitter> particles;

    SplittingEngine(std::shared_ptr<DecisionEngine> base, const EnglishModel& english,
                    std::shared_ptr<KoreanModel> korean = std::make_shared<KoreanModel>(),
                    std::shared_ptr<JapaneseSegmenter> japanese = nullptr);

    Decision decide(const std::u16string& keys, const DecisionContext& context) override;

    /** A capital no lingering Shift explains (RNl, 꿀 typed fast, isn't a name). */
    static bool hasEnglishOnlyCapital(const std::u16string& front);
};

} // namespace rea
