#pragma once

#include <deque>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "EnglishModel.h"
#include "JapaneseModel.h"
#include "ParticleSplitter.h"

namespace rea {

/**
 * Finds English words typed inside a Japanese phrase, the way Japanese is written:
 * `kyouhagoogledekensaku` → きょうは + google + でけんさく.
 *
 * Only used when the whole phrase doesn't compose to clean kana (google → ごおgぇ leaves
 * a stray "g"). Splits the romaji into clean Japanese spans and known English words
 * (3+ letters), maximizing the Japanese model's score plus a fixed score per English
 * word, with a cost per switch so ordinary Japanese is never carved up.
 *
 * The input method asks on every keystroke, so the search only cuts where an English
 * word could start or end (no English word anywhere → nothing to do), and the last
 * answer is remembered.
 */
class JapaneseSegmenter {
public:
    struct Japanese { std::u16string keys, kana; };
    struct English { std::u16string word; };
    using Segment = std::variant<Japanese, English>;

    struct Scored {
        std::vector<Segment> segments;
        double score;
    };

    /** Longest English word looked for inside a phrase. */
    static constexpr int MAX_ENGLISH_LENGTH = 20;

    const EnglishModel& english;
    const JapaneseModel& japanese;

    /** Log-probability credited per letter of a known English word. */
    double englishLetterLogP = -1.6;
    /** Cost of each switch into English. */
    double switchCost = 4.0;

    JapaneseSegmenter(const EnglishModel& english, const JapaneseModel& japanese);

    /** The segments, or nullopt when the keys compose cleanly (nothing to split) or no
     *  split with both Japanese and English beats leaving stray letters. */
    std::optional<std::vector<Segment>> segment(const std::u16string& keys);

    /** Segments with their total log-probability (Japanese spans + English credit − switches). */
    std::optional<Scored> scoredSegments(const std::u16string& keys);

    /** `githubde` → github + で, when that reading beats the whole token as kana, Korean or
     *  English ([ParticleSplitter]). */
    std::optional<std::pair<std::u16string, std::u16string>> englishWithParticle(const std::u16string& keys) const;

    static const std::unordered_set<std::u16string>& particles();
    /** Case particles that may follow an English noun on their own: githubで, slackから.
     *  Not ね/よ/か/や/へ/って: names end in them far more often (jayne, rebecka, claudette). */
    static const std::unordered_set<std::u16string>& trailingParticles();

    /** The final reading of romaji keys: kana, with English words kept when a segmenter finds them. */
    static std::u16string reading(const std::u16string& keys, JapaneseSegmenter* segmenter);

    /** Kana with the English words left in place: きょうはgoogleでけんさく. */
    static std::u16string text(const std::vector<Segment>& segments);

private:
    struct Back { int position, kana; bool english; };
    struct State {
        double score;
        std::optional<Back> back;
        std::optional<Segment> segment;
    };
    using Best = std::vector<std::optional<State>>; // [position][kana so far, capped at 3][used English]

    /** The last few answers: the preview, the decision and the commit ask about the same keys.
     *  ponytail: no lock (Kotlin synchronizes); TSF calls the IME on one thread. */
    std::deque<std::pair<std::u16string, std::optional<Scored>>> cache;
    ParticleSplitter particleSplitter;

    std::optional<Scored> search(const std::u16string& keys) const;
    std::vector<Segment> path(Back end, const Best& best) const;
    /** Exactly an English word plus one particle, where the whole isn't itself English. */
    bool endsInParticle(const std::vector<Segment>& segments, const std::u16string& keys) const;
    /** Adjacent Japanese spans join into one (きょうは + でけんさく stay separate only around English). */
    static std::vector<Segment> merged(const std::vector<Segment>& segments);
};

} // namespace rea
