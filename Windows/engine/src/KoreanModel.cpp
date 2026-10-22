#include "KoreanModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <limits>
#include <unordered_map>

#include "EnglishModel.h"
#include "Jamo.h"
#include "KoreanSyllables.h"
#include "Resources.h"

namespace rea {

static bool isSyllable(char16_t s) { return s >= u'가' && s <= u'힣'; }

/** Words seen in Korean sentences (Data/korean_words.txt, Tatoeba): 나는, 무슨, 내가… */
static const WordList& corpusWords() {
    static const WordList list = [] {
        std::vector<std::u16string> words;
        if (auto lines = Resources::lines("korean_words.txt")) {
            for (auto& line : *lines) words.push_back(line.substr(0, line.find(u'\t')));
        }
        return WordList(words);
    }();
    return list;
}

/** REA: FNV-1a of the UTF-8, the same on every launch. */
static uint64_t hash(std::string_view utf8) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char b : utf8) h = (h ^ b) * 0x100000001b3ULL;
    return h;
}

/** REA: a binary search of the sorted hashes. */
static bool contains(const std::vector<uint64_t>& sorted, const std::u16string& word) {
    return std::binary_search(sorted.begin(), sorted.end(), hash(utf8(word)));
}

/** Case particles, by what the noun must end in: a final consonant (을), none (를), or either. */
static const std::pair<std::u16string, std::optional<bool>> particles[] = {
    {u"을", true}, {u"은", true}, {u"이", true}, {u"과", true}, {u"으로", true},
    {u"를", false}, {u"는", false}, {u"가", false}, {u"와", false},
    {u"에", std::nullopt}, {u"의", std::nullopt}, {u"도", std::nullopt}, {u"에서", std::nullopt}, {u"만", std::nullopt},
};

/** REA: korean_vocabulary.txt (245k lines) read once, straight from its bytes, into what the
 *  lookups need, the words as sorted 64-bit hashes (as Swift keeps them for the iOS keyboard).
 *  ponytail: a word off the list hashing onto one on it is a 1 in 10^14 chance per lookup. */
struct Lists {
    std::vector<uint64_t> vocabulary, rare;
    WordList stems{{}};
    std::vector<bool> syllables = std::vector<bool>(0x10000);
};

static const Lists& lists() {
    static const Lists lists = [] {
        Lists out;
        std::vector<std::u16string> stems;
        auto addStems = [&](const std::u16string& word) {
            for (auto& [particle, needsFinal] : particles) {
                if (!endsWith(word, particle)) continue;
                auto stem = dropLast(word, particle.size());
                if (stem.size() < 2 || !isSyllable(stem.back())) continue;
                bool hasFinal = (stem.back() - 0xAC00) % 28 != 0;
                if (!needsFinal || *needsFinal == hasFinal) stems.push_back(stem);
            }
        };
        auto addSyllables = [&](const std::u16string& word) { for (char16_t c : word) out.syllables[c] = true; };
        for (auto& word : corpusWords().all()) { addStems(word); addSyllables(word); }
        std::ifstream file(Resources::directory / "korean_vocabulary.txt", std::ios::binary);
        if (Resources::directory.empty() || !file) { out.stems = WordList(std::move(stems)); return out; }
        std::string line;
        while (std::getline(file, line)) {
            auto tab = line.find('\t');
            if (tab == std::string::npos || tab == 0) continue;
            auto n = toInt(utf16(line.substr(tab + 1)));
            if (!n) continue;
            auto word = utf16(line.substr(0, tab));
            addStems(word);
            if (*n >= KoreanModel::COMMON_COUNT) {
                out.vocabulary.push_back(hash(std::string_view(line).substr(0, tab)));
                addSyllables(word);
            } else {
                out.rare.push_back(hash(std::string_view(line).substr(0, tab)));
            }
        }
        std::sort(out.vocabulary.begin(), out.vocabulary.end());
        std::sort(out.rare.begin(), out.rare.end());
        out.stems = WordList(std::move(stems));
        return out;
    }();
    return lists;
}

/** Read once (Kotlin reads it per model): every model makes the same table from it. */
static const std::optional<std::unordered_map<char16_t, int>>& corpusCounts() {
    static const auto counts = []() -> std::optional<std::unordered_map<char16_t, int>> {
        auto lines = Resources::lines("korean_syllables.txt");
        if (!lines) return std::nullopt;
        std::unordered_map<char16_t, int> counts;
        for (auto& line : *lines) {
            auto tab = line.find(u'\t');
            if (tab == std::u16string::npos || tab == 0 || line.find(u'\t', tab + 1) != std::u16string::npos) continue;
            auto n = toInt(line.substr(tab + 1));
            if (n) counts[line[0]] = *n;
        }
        return counts;
    }();
    return counts;
}

KoreanModel::KoreanModel() : tiers(SYLLABLES, std::numeric_limits<double>::quiet_NaN()) {
    // The hand-curated frequency tiers (KoreanSyllables.h).
    const std::pair<const char16_t*, double> sources[] = {
        {KoreanSyllables::TIER3, TIER3_LOG_P}, {KoreanSyllables::TIER2, TIER2_LOG_P}, {KoreanSyllables::TIER1, TIER1_LOG_P}};
    for (auto [syllables, logP] : sources) {
        for (const char16_t* s = syllables; *s; s++) if (isSyllable(*s)) tiers[*s - u'가'] = logP; // later (more frequent) tiers overwrite earlier ones
    }
    // Counts from Korean sentences (Data/korean_syllables.txt, Tatoeba, CC-BY 2.0 FR): whichever rates a syllable higher.
    if (auto& counts = corpusCounts()) {
        long long sum = 0;
        for (auto& [_, n] : *counts) sum += n;
        double total = double(sum);
        double smoothing = 0.5;
        double possible = 11172.0;
        for (auto& [s, n] : *counts) {
            if (!isSyllable(s)) continue;
            double logP = std::log((n + smoothing) / (total + smoothing * possible));
            double old = tiers[s - u'가'];
            tiers[s - u'가'] = std::isnan(old) ? logP : std::max(old, logP);
        }
    }
}

bool KoreanModel::isKnownWord(const std::u16string& hangul) const { return corpusWords().contains(hangul); }

bool KoreanModel::isVocabularyWord(const std::u16string& hangul) const {
    return corpusWords().contains(hangul) || rea::contains(lists().vocabulary, hangul); // REA: hashes
}

bool KoreanModel::isRareVocabularyWord(const std::u16string& hangul) const { return rea::contains(lists().rare, hangul); } // REA: hashes

bool KoreanModel::isKnownStem(const std::u16string& hangul) const { return lists().stems.contains(hangul); }

bool KoreanModel::knowsSyllable(char16_t syllable) const {
    return (isSyllable(syllable) && !std::isnan(tiers[syllable - u'가'])) || lists().syllables[syllable];
}

double KoreanModel::syllableLogProbability(char16_t syllable) const {
    if (!isSyllable(syllable)) return UNKNOWN_SYLLABLE_LOG_P;
    double p = tiers[syllable - u'가'];
    return std::isnan(p) ? UNKNOWN_SYLLABLE_LOG_P : p;
}

std::optional<KoreanModel::Score> KoreanModel::score(const std::u16string& keys, bool partial) const {
    auto composition = HangulComposer::compose(keys);
    if (!composition) return std::nullopt;
    if (keys.empty()) return std::nullopt;
    double total = logProbability(*composition, partial);
    // The consonant typed last may have joined the syllable before as its final (개 + ㅎ
    // shows 갷) while it is the next syllable's first (소개하는): score that reading too.
    if (partial && !composition->isChat()) {
        auto last = DubeolsikLayout::jamo(keys.back());
        if (last && Jamo::isConsonant(*last) && !composition->units.empty() && composition->units.back().isSyllable) {
            auto before = HangulComposer::compose(dropLast(keys, 1));
            if (before && before->units.size() == composition->units.size() && before->units.back().isSyllable) {
                total = std::max(total, logProbability(*before, false) + PENDING_CONSONANT_LOG_P - PENDING_FINAL_PENALTY);
            }
        }
    }
    return Score{total / double(keys.size()), *composition};
}

double KoreanModel::logProbability(const HangulComposer::Result& composition, bool partial) const {
    auto& units = composition.units;
    bool allChat = composition.isChat();
    double total = 0.0;
    for (size_t i = 0; i < units.size(); i++) {
        auto& unit = units[i];
        if (unit.isSyllable) total += syllableLogProbability(unit.character);
        else if (allChat) total += CHAT_JAMO_LOG_P;
        else if (partial && i == units.size() - 1 && Jamo::isConsonant(unit.character)) total += PENDING_CONSONANT_LOG_P;
        else total += STRAY_JAMO_LOG_P;
    }
    return total;
}

} // namespace rea
