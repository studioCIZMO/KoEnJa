#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <variant>

#include "EnglishModel.h"
#include "JapaneseModel.h"
#include "KoreanModel.h"
#include "MixedWord.h"

namespace rea {

/**
 * An English word with a particle typed straight after it (`githubdp` → `github에`,
 * `githubde` → `githubで`) is one more reading of the keys, scored like the whole-token
 * readings it competes with: average log-probability per keystroke.
 *
 * - The front scores as English: a known word gets the dictionary floors [HeuristicEngine]
 *   uses, capitals typed on purpose (API, GitHub) a higher one, anything else its spelling.
 * - The particle scores by how common it is, and switching language costs a little.
 * - The whole token scores as Korean (a Korean word, or one with a particle, gets a floor),
 *   as unknown English, and with Japanese on as kana.
 *
 * The split wins only by `margin`. `form에서` splits because 래그 is no Korean word and form
 * is everyday English; `핸들을` doesn't, because 핸들 is a word.
 */
class ParticleSplitter {
public:
    /** Log-probabilities for a whole word or particle, not per key, so a longer front or
     *  particle isn't favored for its length. */
    struct Weights {
        /** A known English word: `knownBase` minus `perLevel` per SCOWL level (10 = everyday). */
        double knownBase = -4.0;
        double perLevel = 0.15;
        /** An acronym (API, UI, PR) or a name with a capital inside (GitHub, VSCode, macOS). */
        double acronym = -6.0;
        double name = -7.0;
        /** Taken off once from the spelling score of a front (or token) that isn't a known word. */
        double unknownWord = 3.0;
        /** Taken off once more when the front isn't a known word: an unknown word with a
         *  particle after it is rarer than an unknown word (names: adriano, porto, chandi). */
        double unknownFront = 3.0;
        /** A common particle (에, 를, で…) and the other endings (처럼, 했어, まで…). */
        double coreParticle = -2.5;
        double otherParticle = -4.5;
        /** Once per split, for switching language inside a token. */
        double switchCost = 1.0;
        /** Per key, the least a Korean word (or a word with a particle) scores. */
        double koreanWordFloor = -1.4;
        /** Added per key to the kana reading's score, as [HeuristicEngine::japaneseOffset]. */
        double japaneseOffset = 1.4;
        /** How far (per key) the split has to lead the best whole-token reading. */
        double margin = 0.3;
    };

    struct Korean { MixedWord mixed; };
    struct Japanese { std::u16string english, particle; };
    using Split = std::variant<Korean, Japanese>;
    static std::u16string text(const Split& split);

    struct Scored {
        Split split;
        double lead;
        std::u16string detail;
    };

    const EnglishModel& english;
    std::shared_ptr<KoreanModel> korean;
    const JapaneseModel* japanese;
    /** Tunable after construction (Kotlin takes them as a constructor argument). */
    Weights weights;

    ParticleSplitter(const EnglishModel& english, std::shared_ptr<KoreanModel> korean = std::make_shared<KoreanModel>(),
                     const JapaneseModel* japanese = nullptr);

    /** The best split of [keys], if it beats reading the whole token as one language.
     *  Japanese particles only with [japaneseOn]. */
    std::optional<Split> split(const std::u16string& keys, bool japaneseOn) const;

    /** The best split and how far it leads the best whole-token reading (for tuning). */
    std::optional<Scored> scored(const std::u16string& keys, bool japaneseOn) const;

    /** The front as English (log-probability of the word); nullopt when it can't be one. */
    std::optional<double> frontScore(const std::u16string& front) const;

    /** GitHub, VSCode, iPhone, macOS: a capital inside, and the part from the last capital a
     *  known word (Hub, Code) or capitals (OS). Not APIdm. */
    bool isName(const std::u16string& front) const;

    /** The whole token as Korean, per key. A capital with no shifted jamo (the S of Slack)
     *  can't be Korean. */
    double koreanScore(const std::u16string& keys) const;

    /** The whole token as kana, per key. Capitals after the first letter aren't romaji
     *  (UIde is UI + で); one at the start may begin a sentence (Kyouha). */
    double japaneseScore(const std::u16string& keys) const;

    /** 핸들, 소문은, 행하는: a word of the wider vocabulary, or one with a particle or ending. */
    bool isKoreanWordOrWordWithParticle(const std::u16string& hangul) const;

    /** Japanese particles that score `coreParticle` (から, まで, より score `otherParticle`). */
    static const std::unordered_set<std::u16string>& coreJapanese();
    /** Particles that follow a noun most of the time: they score `coreParticle`. */
    static const std::unordered_set<std::u16string>& coreKorean();

    /** API, UI, PR: 2 to 6 capitals. */
    static bool isAcronym(const std::u16string& front);
};

} // namespace rea
