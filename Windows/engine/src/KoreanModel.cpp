#include "KoreanModel.h"

#include <cmath>
#include <limits>
#include <unordered_map>

#include "EnglishModel.h"
#include "Jamo.h"
#include "KoreanSyllables.h"
#include "Resources.h"

namespace rea {

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

static const WordList& vocabulary() {
    static const WordList list(Resources::lines("korean_vocabulary.txt").value_or(std::vector<std::u16string>{}));
    return list;
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

static bool isSyllable(char16_t s) { return s >= u'가' && s <= u'힣'; }

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
    return corpusWords().contains(hangul) || vocabulary().contains(hangul);
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
    auto& units = composition->units;
    bool allChat = units.size() >= 2 && std::all_of(units.begin(), units.end(), [](auto& u) {
        return !u.isSyllable && contains(CHAT_JAMO, u.character);
    });
    double total = 0.0;
    for (size_t i = 0; i < units.size(); i++) {
        auto& unit = units[i];
        if (unit.isSyllable) total += syllableLogProbability(unit.character);
        else if (allChat) total += CHAT_JAMO_LOG_P;
        else if (partial && i == units.size() - 1 && Jamo::isConsonant(unit.character)) total += PENDING_CONSONANT_LOG_P;
        else total += STRAY_JAMO_LOG_P;
    }
    return Score{total / double(keys.size()), *composition};
}

} // namespace rea
