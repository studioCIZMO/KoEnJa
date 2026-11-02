#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "DecisionEngine.h"
#include "Text.h"

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

    /** A word that kept the digits and `-`/`_` typed into it (`InputSession`: k8s, utf-8, 안녕1),
     *  read piece by piece between them, each English unless it reads as Korean by enough
     *  (`readsAsKoreanPiece`: 옵션1, 보고서_최종, 제1장; not cmd0, en_s2c, src_dir) or is a particle
     *  after the last of them (mp3를, k8s에); not a Japanese phrase, clean romaji throughout
     *  (ra-men, kyou3ji). */
    std::optional<Decision> decideJoined(const std::u16string& keys, const DecisionContext& context);

    /** Letters next to a digit, `-` or `_` are code more often than the same letters alone: Korean
     *  only when the whole piece's Korean score leads its English score by `identifierCost`
     *  (옵션1, 보고서_최종, 안녕1하세요; not en_s2c as 두_s2c), and a single syllable, rarely a word
     *  in code, by `oneSyllableInIdentifier` more (not cmd0 as 층0, src_dir as src_약), unless it is
     *  counted after a number (제1장, 3월5일) or one written before one (제1, 주3회, 표1). */
    bool readsAsKoreanPiece(const std::u16string& keys, bool beforeDigit, bool afterDigit, bool afterEnglish, bool atEnd) const;

    /** The piece's Korean score less its English one, over all its keys (nullopt when it isn't clean
     *  Hangul): `ParticleSplitter`'s readings, a vocabulary word floored, English as a known word
     *  or spelled. */
    std::optional<double> koreanLead(const std::u16string& keys) const;

    /** Syllables written right before a number (제1장, 주3회, 표1): Tatoeba's Korean has 제 and 주
     *  there, and documents number 표, 장, 절, 항, 조 and 호. */
    static const std::unordered_set<std::u16string>& numberWords();

    /** What an English word keeps while it is typed: digits, `-` and `_`. */
    static bool joinsWord(char16_t ch) { return isAsciiDigit(ch) || ch == u'-' || ch == u'_'; }

    /** A capital no lingering Shift explains (RNl, 꿀 typed fast, isn't a name). */
    static bool hasEnglishOnlyCapital(const std::u16string& front);

private:
    /** A particle making up [keys] or starting one letter into it (fmf: 를, sdp: s + 에), unless
     *  [keys] is an English word: (front, particle). */
    std::optional<std::pair<std::u16string, std::u16string>> particleAfter(const std::u16string& keys) const;
};

} // namespace rea
