#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
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
        /** Instead of `switchCost` for an ordinary English word and a Korean word (hello월드): rarer
         *  than a particle or an acronym or name with a word after it (github에, PC카톡). */
        double wordSwitchCost = 9.0;
        /** Added to a particle of a pair (은/는, 을/를) after an acronym when it is the form the
         *  acronym's last letter takes (TM은, TV는): after another word either form may follow,
         *  after an acronym only that one does, so twice as likely (ln 2). */
        double agreement = 0.7;
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

    /** A split that may stand on its own, and its score. */
    struct WordSplit {
        MixedWord word;
        double score;
    };

    /** The keys as an English front and a Korean word of two syllables or more, per key
     *  (PC카톡에서, not PCzkxhr에서): the front as `frontScore`, the word as the whole token is
     *  scored as Korean. A word that is only a particle is the particle split, and one with a
     *  syllable nobody writes is no word (mac + 뇃에서 doesn't stop macshot에서).
     *
     *  `first` is the best cut, the reading a particle split has to beat. `second` is the best cut
     *  that may stand as a split itself (`mayStandAsWord`): an acronym or name and a word (PC카톡,
     *  TV광고) switch language at `switchCost`, as a particle does; an ordinary word and a Korean
     *  word (hello월드) are rarer and pay `wordSwitchCost`, so a name that spells like English
     *  stays whole (cartman isn't car스무). */
    std::pair<double, std::optional<WordSplit>> englishThenKoreanWord(const std::u16string& keys) const;

    /** Whole Hangul made of syllables people use. */
    bool readsAsKorean(const std::u16string& keys) const;

    /** The front as English (log-probability of the word); nullopt when it can't be one. */
    std::optional<double> frontScore(const std::u16string& front) const;

    /** `frontScore`, and whether the front is an acronym or a name typed with its capitals. */
    std::optional<std::pair<double, bool>> frontReading(const std::u16string& front) const;

    /** GitHub, VSCode, iPhone, macOS: a capital inside, and the part from the last capital a
     *  known word (Hub, Code) or capitals (OS). Not APIdm. The SCOWL level of that last part
     *  (Code: 10, Hub: 35), 10 for capitals; nullopt when [front] isn't a name. */
    std::optional<int> nameLevel(const std::u16string& front) const;

    /** The whole token as Korean, per key. A capital with no shifted jamo (the S of Slack)
     *  can't be Korean. */
    double koreanScore(const std::u16string& keys) const;

    /** The whole token as kana, per key: only what keeps it from splitting, as the decision
     *  engine reads a capital as English. Capitals after the first letter aren't romaji
     *  (UIde is UI + で); one at the start may begin a sentence (Kyouha stays whole). */
    double japaneseScore(const std::u16string& keys) const;

    /** 핸들, 소문은, 행하는: a word of the wider vocabulary, or one with a particle or ending in
     *  the form the word's last syllable takes (씨를, not 씨이 or 씨을). */
    bool isKoreanWordOrWordWithParticle(const std::u16string& hangul) const;

    /** Japanese particles that score `coreParticle` (から, まで, より score `otherParticle`). */
    static const std::unordered_set<std::u16string>& coreJapanese();
    /** Particles that follow a noun most of the time: they score `coreParticle`. */
    static const std::unordered_set<std::u16string>& coreKorean();

    /** API, UI, PR: 2 to 6 capitals. */
    static bool isAcronym(const std::u16string& front);

private:
    /** Whether an acronym or name may be the front of a split, not only compete with one: when
     *  a capital no lingering Shift explains is in the keys, or a known Korean word follows and
     *  its own keys spell a syllable no list or corpus has (EU as ㄸ+ㅕ: 뗘). Korean typed fast
     *  keeps its rare syllables (삐졌어 as QLwuTdj: 삐 is rare, but seen) and 쓰정책 (TMwjdcor)
     *  stays Korean. Only the front is judged: the word after may well hold a rare syllable
     *  (또공룡이 as EHrhdfyddl). */
    bool mayStandAsWord(const std::u16string& front, bool englishOnlyCapital, bool knownKorean) const;

    /** Whole Hangul of syllables seen in the corpus or a word list, however rarely (삐, not 뗘). */
    bool spellsSeenKorean(const std::u16string& keys) const;
};

} // namespace rea
