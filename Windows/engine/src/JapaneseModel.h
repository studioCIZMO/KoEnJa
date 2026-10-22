#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RomajiComposer.h"

namespace rea {

/**
 * Kana trigram model for judging how Japanese a romaji string reads. Counts come
 * from Japanese sentences (Tatoeba, CC-BY 2.0 FR) turned into the romaji people
 * type and back into kana with [RomajiComposer] (tools/build_kana_model.py).
 *
 * Clean romaji isn't enough: "hello" composes to へっぉ, which this model scores
 * as very un-Japanese (っぉ essentially never occurs).
 */
class JapaneseModel {
public:
    struct Score {
        /** Average log-probability per keystroke, comparable with the English and Korean models. */
        double averageLogProbability;
        RomajiComposer::Result composition;
    };

    static constexpr double STRAY_LETTER_LOG_P = -13.0;
    static constexpr double PENDING_LOG_P = -3.5;

    /** Non-letters that belong inside a Japanese phrase: ん (n'), ー, and the full-width symbols (、。？！「」・…). */
    static const std::unordered_set<char16_t>& inPhraseSymbols();

    /** Nullptr when the data file is missing. */
    static const JapaneseModel* shared();

    /** From a file of trigram<TAB>count lines; nullopt when it's missing or empty. */
    static std::optional<JapaneseModel> load(const std::string& name);

    /** Sum of log-probabilities of the kana (and the end of word unless [partial]). */
    double logProbability(const std::u16string& kana, bool partial) const;

    std::optional<Score> score(const std::u16string& keys, bool partial = false) const;

private:
    /** Counts by key, sorted for binary search: an IME can't spare hash maps of 100k+ entries. */
    class CountTable {
    public:
        CountTable() = default;
        explicit CountTable(const std::unordered_map<int64_t, float>& map);
        /** NaN when the key isn't there. */
        float operator[](int64_t key) const;

    private:
        std::vector<int64_t> keys;
        std::vector<float> values;
    };

    CountTable trigrams, bigrams, unigrams, bigramContexts, trigramContexts;
    float total = 0;
    float vocabulary = 0;

    JapaneseModel() = default;
    /** Interpolated trigram probability of [c] after [a] [b] (code points). */
    double probability(int a, int b, int c) const;
};

} // namespace rea
