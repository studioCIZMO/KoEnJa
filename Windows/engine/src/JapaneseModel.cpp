#include "JapaneseModel.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "Resources.h"
#include "Text.h"

namespace rea {

static constexpr int START = '^';
static constexpr int END = '$';

static float orZero(float f) { return std::isnan(f) ? 0.0f : f; }
static int64_t key(int c) { return c; }
static int64_t key(int b, int c) { return (int64_t(b) << 21) | c; }
static int64_t key(int a, int b, int c) { return (int64_t(a) << 42) | (int64_t(b) << 21) | c; }

static std::vector<int> codePoints(const std::u16string& s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size(); i++) {
        int c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
        }
        out.push_back(c);
    }
    return out;
}

JapaneseModel::CountTable::CountTable(const std::unordered_map<int64_t, float>& map) {
    for (auto& [k, _] : map) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    for (auto k : keys) values.push_back(map.at(k));
}

float JapaneseModel::CountTable::operator[](int64_t key) const {
    auto it = std::lower_bound(keys.begin(), keys.end(), key);
    return it != keys.end() && *it == key ? values[it - keys.begin()] : NAN;
}

const std::unordered_set<char16_t>& JapaneseModel::inPhraseSymbols() {
    static const auto set = [] {
        std::unordered_set<char16_t> s = {u'\''};
        for (auto& [symbol, _] : RomajiComposer::symbols()) s.insert(symbol);
        return s;
    }();
    return set;
}

const JapaneseModel* JapaneseModel::shared() {
    static const std::optional<JapaneseModel> model = load("kana_trigrams.txt");
    return model ? &*model : nullptr;
}

// Float arithmetic throughout, as in Kotlin; the build turns off FMA contraction so the
// products round the same way.
double JapaneseModel::probability(int a, int b, int c) const {
    float p1 = (orZero(unigrams[key(c)]) + 1) / (total + vocabulary);
    float bc = bigramContexts[key(b)];
    float p2 = std::isnan(bc) ? 0.0f : orZero(bigrams[key(b, c)]) / bc;
    float tc = trigramContexts[key(a, b)];
    float p3 = std::isnan(tc) ? 0.0f : orZero(trigrams[key(a, b, c)]) / tc;
    return double(0.6f * p3 + 0.3f * p2 + 0.1f * p1);
}

double JapaneseModel::logProbability(const std::u16string& kana, bool partial) const {
    std::vector<int> seq = {START, START};
    for (int c : codePoints(kana)) seq.push_back(c);
    if (!partial) seq.push_back(END);
    double sum = 0.0;
    for (size_t i = 2; i < seq.size(); i++) sum += std::log(probability(seq[i - 2], seq[i - 1], seq[i]));
    return sum;
}

std::optional<JapaneseModel::Score> JapaneseModel::score(const std::u16string& keys, bool partial) const {
    if (keys.empty() || !all(keys, [](char16_t c) { return isAscii(c) && (isLetter(c) || inPhraseSymbols().count(c)); })) return std::nullopt;
    auto composition = RomajiComposer::compose(keys, partial);
    auto kana = filter(dropLast(composition.text, composition.pending.size()),
                       [](char16_t c) { return !isAscii(c) && !RomajiComposer::punctuation().count(c); });
    double total = logProbability(kana, partial);
    total += composition.strayLetters * STRAY_LETTER_LOG_P;
    if (!composition.pending.empty()) total += PENDING_LOG_P;
    return Score{total / double(keys.size()), composition};
}

std::optional<JapaneseModel> JapaneseModel::load(const std::string& name) {
    auto lines = Resources::lines(name);
    if (!lines) return std::nullopt;
    std::unordered_map<int64_t, float> trigrams, trigramContexts, bigrams, bigramContexts, unigrams;
    std::unordered_set<int> symbols;
    float total = 0;
    for (auto& line : *lines) {
        auto tab = line.find(u'\t');
        if (tab == std::u16string::npos || line.find(u'\t', tab + 1) != std::u16string::npos) continue;
        auto count = toFloat(line.substr(tab + 1));
        if (!count) continue;
        auto chars = codePoints(line.substr(0, tab));
        if (chars.size() != 3) continue;
        int a = chars[0], b = chars[1], c = chars[2];
        trigrams[key(a, b, c)] += *count;
        trigramContexts[key(a, b)] += *count;
        bigrams[key(b, c)] += *count;
        bigramContexts[key(b)] += *count;
        unigrams[key(c)] += *count;
        total += *count;
        symbols.insert(chars.begin(), chars.end());
    }
    if (!(total > 0)) return std::nullopt;
    JapaneseModel model;
    model.trigrams = CountTable(trigrams);
    model.bigrams = CountTable(bigrams);
    model.unigrams = CountTable(unigrams);
    model.bigramContexts = CountTable(bigramContexts);
    model.trigramContexts = CountTable(trigramContexts);
    model.total = total;
    model.vocabulary = float(symbols.size());
    return model;
}

} // namespace rea
