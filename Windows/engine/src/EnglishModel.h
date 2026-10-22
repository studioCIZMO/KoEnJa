#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rea {

/** Words sorted for binary search: an IME can't spare a hash set's overhead per word. */
class WordList {
public:
    explicit WordList(std::vector<std::u16string> words);
    size_t size() const { return sorted.size(); }
    const std::vector<std::u16string>& all() const { return sorted; }
    bool contains(std::u16string_view word) const { return indexOf(word) >= 0; }
    /** The word's place in sorted order, -1 if it isn't there. */
    int indexOf(std::u16string_view word) const;

private:
    std::vector<std::u16string> sorted;
};

/**
 * Character-trigram language model plus a word list with how common each word is, from
 * SCOWL (Data/english_words.txt: word<TAB>level, 10 = most common … 60).
 *
 * [raw]: word → level; [spelling]: the words letter trigrams are trained on (nullptr: the list's).
 */
class EnglishModel {
public:
    /** Words up to this level count as English words (a score floor, protection from kana). */
    static constexpr int KNOWN_LEVEL = 50;
    /** Words up to this level are everyday English (you, sense, machine, for, got). */
    static constexpr int COMMON_LEVEL = 20;
    /** Level given to [commonWords] (tech terms and slang SCOWL lacks): known, not "common". */
    static constexpr int EXTRA_LEVEL = 35;
    /** Letter trigrams come from this list when it's there: its 235k words separate English
     *  spelling from Korean keys better than SCOWL's 119k (measured). Windows doesn't ship it, so
     *  the IME brings a copy as [SPELLING_LIST] in Resources::directory; elsewhere the system's
     *  [SPELLING_LIST_PATH] stands in. Not on Windows: there that path would be on whatever drive
     *  the host process happens to be on. */
    static constexpr const char* SPELLING_LIST = "words.txt";
    static constexpr const char* SPELLING_LIST_PATH = "/usr/share/dict/words";

    /** [extraWords]: words at [EXTRA_LEVEL] on top of [raw] ([commonWords]). */
    EnglishModel(const std::unordered_map<std::u16string, int>& raw, const std::vector<std::u16string>* spelling = nullptr,
                 const std::vector<std::u16string>& extraWords = {});

    /** Loading the word list takes a moment; share one instance. */
    static const EnglishModel& shared();

    /** From english_words.txt; a missing file leaves only [commonWords]. Trigrams are trained
     *  on [spellingPath]'s words when it can be read, otherwise on the list. */
    static EnglishModel load(const std::optional<std::filesystem::path>& spellingPath = std::nullopt);

    /** [SPELLING_LIST] in Resources::directory if it is there, else (not on Windows) [SPELLING_LIST_PATH]. */
    static std::optional<std::filesystem::path> spellingListPath();

    /** Average natural-log probability per symbol (letters + end marker).
     *  Real English words land around -1.8…-2.4; keyboard mash around -3.5 or lower. */
    double averageLogProbability(const std::u16string& token) const;

    /** The word's level, also for regular inflections of a listed word (nullopt: not a word). */
    std::optional<int> level(const std::u16string& token) const;

    /** A word people use (SCOWL level ≤ 50, or one of [commonWords]). */
    bool isKnownWord(const std::u16string& token) const;

    /** Everyday English (level ≤ 20): kept English even right after Korean or Japanese. */
    bool isCommonWord(const std::u16string& token) const;

    static const std::unordered_set<std::u16string>& shortWords();
    /** Everyday/technical words SCOWL is missing or ranks rare. */
    static const std::vector<std::u16string>& commonWords();

private:
    static constexpr int SYMBOLS = 27;
    // Symbols: 0 = word boundary, 1...26 = a...z
    std::vector<float> trigrams = std::vector<float>(SYMBOLS * SYMBOLS * SYMBOLS);
    std::vector<float> contexts = std::vector<float>(SYMBOLS * SYMBOLS);
    /** The words (lowercase, letters only) and each one's SCOWL level. */
    WordList words;
    std::vector<signed char> levels;
    /** The levels a guessed inflection may take from its stem: the list's own, not [commonWords]
     *  (absent: `levels`). REA: only the words [commonWords] changed (nullopt: not listed). */
    std::unordered_map<std::u16string, std::optional<int>> stemLevels;
    static constexpr float smoothing = 0.5f;

    std::optional<int> levelOf(std::u16string_view word) const;
};

} // namespace rea
