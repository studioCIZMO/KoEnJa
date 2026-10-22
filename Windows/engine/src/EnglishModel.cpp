#include "EnglishModel.h"

#include <algorithm>
#include <cmath>

#include "Resources.h"
#include "Text.h"

namespace rea {

WordList::WordList(std::vector<std::u16string> words) : sorted(std::move(words)) {
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
}

int WordList::indexOf(std::u16string_view word) const {
    auto it = std::lower_bound(sorted.begin(), sorted.end(), word);
    return it != sorted.end() && *it == word ? int(it - sorted.begin()) : -1;
}

/** [^, ^, letters..., $] with ^ and $ both encoded as 0. */
static std::vector<int> encode(const std::u16string& word) {
    std::vector<int> codes(2);
    codes.reserve(word.size() + 3);
    for (char16_t ch : word) if (ch >= u'a' && ch <= u'z') codes.push_back(ch - u'a' + 1);
    codes.push_back(0);
    return codes;
}

static std::u16string withoutApostrophes(const std::u16string& s) { return filter(s, [](char16_t c) { return c != u'\''; }); }

static WordList makeWords(const std::unordered_map<std::u16string, int>& levels) {
    std::vector<std::u16string> keys;
    keys.reserve(levels.size());
    for (auto& [word, _] : levels) keys.push_back(word);
    return WordList(std::move(keys));
}

static std::unordered_map<std::u16string, int> wordLevels(const std::unordered_map<std::u16string, int>& raw) {
    std::unordered_map<std::u16string, int> levels;
    levels.reserve(raw.size() * 2);
    for (auto& [rawWord, level] : raw) {
        auto word = withoutApostrophes(rawWord);
        if (word.empty() || !all(word, isAsciiLetter)) continue;
        // Many 1–2 letter list entries aren't words people type ("ra", "ko"); only real ones count.
        if (word.size() > 2 || EnglishModel::shortWords().count(word)) {
            auto old = levels.find(word);
            levels[word] = old == levels.end() ? level : std::min(old->second, level);
        }
    }
    return levels;
}

EnglishModel::EnglishModel(const std::unordered_map<std::u16string, int>& raw, const std::vector<std::u16string>* spelling,
                           const std::vector<std::u16string>& extraWords)
    : words(std::vector<std::u16string>{}) {
    const int n = SYMBOLS;
    auto levels = wordLevels(raw);
    // REA: upstream normalizes the list with the extra words in and keeps a second copy without.
    std::unordered_map<std::u16string, int> extra;
    for (auto& word : extraWords) extra[lowercase(word)] = EXTRA_LEVEL;
    for (auto& [word, _] : wordLevels(extra)) {
        auto old = levels.find(word);
        stemLevels[word] = old == levels.end() ? std::nullopt : std::optional(old->second);
        levels[word] = old == levels.end() ? EXTRA_LEVEL : std::min(old->second, int(EXTRA_LEVEL));
    }
    auto train = [&](const std::u16string& word) {
        if (word.empty() || !all(word, isAsciiLetter)) return;
        auto codes = encode(word);
        for (size_t i = 2; i < codes.size(); i++) {
            trigrams[(codes[i - 2] * n + codes[i - 1]) * n + codes[i]] += 1.0f;
            contexts[codes[i - 2] * n + codes[i - 1]] += 1.0f;
        }
    };
    if (spelling) for (auto& word : *spelling) train(word);
    else for (auto& [word, _] : levels) train(word);
    words = makeWords(levels);
    this->levels.assign(words.size(), 0);
    for (auto& [word, level] : levels) this->levels[words.indexOf(word)] = (signed char)level;
}

std::optional<int> EnglishModel::levelOf(std::u16string_view word) const {
    int i = words.indexOf(word);
    if (i < 0) return std::nullopt;
    return int(levels[i]);
}

double EnglishModel::averageLogProbability(const std::u16string& token) const {
    const int n = SYMBOLS;
    auto codes = encode(lowercase(token));
    if (codes.size() <= 3) return -10.0;
    double total = 0.0;
    for (size_t i = 2; i < codes.size(); i++) {
        float c = trigrams[(codes[i - 2] * n + codes[i - 1]) * n + codes[i]];
        float t = contexts[codes[i - 2] * n + codes[i - 1]];
        // Float arithmetic and a float log (Kotlin's ln(Float): the double log rounded to float).
        float p = (c + smoothing) / (t + smoothing * float(n));
        total += double(float(std::log(double(p))));
    }
    return total / double(codes.size() - 2);
}

std::optional<int> EnglishModel::level(const std::u16string& token) const {
    const auto lowered = withoutApostrophes(lowercase(token));
    const std::u16string_view w = lowered;
    if (auto l = levelOf(w)) return l;
    std::optional<int> best;
    // SCOWL lists the inflections of its words, so a guessed one counts only for a stem the
    // list itself ranks above its rarest known level: thru (50, and 35 only as one of
    // [commonWords]) + s is no word, and thrus is 소견.
    auto consider = [&](std::u16string_view stem) {
        // REA: stemLevels holds only the words [commonWords] changed.
        auto listed = stemLevels.find(std::u16string(stem));
        auto l = listed != stemLevels.end() ? listed->second : levelOf(stem);
        if (l && *l < KNOWN_LEVEL) best = best ? std::min(*best, *l) : *l;
    };
    // Stems of 3+ letters: ths isn't the+s, ehs isn't eh+s.
    for (std::u16string_view suffix : {u"s", u"es", u"ed", u"d", u"ing", u"ly", u"er", u"ers", u"est"}) {
        if (w.size() <= suffix.size() + 2 || !w.ends_with(suffix)) continue;
        auto stem = w.substr(0, w.size() - suffix.size());
        consider(stem);
        consider(std::u16string(stem) + u"e");
        // running → run, stopped → stop
        if (stem.size() >= 2 && stem[stem.size() - 2] == stem.back()) consider(stem.substr(0, stem.size() - 1));
    }
    // stories → story, denied → deny, heavier → heavy
    for (std::u16string_view suffix : {u"ies", u"ied", u"ier", u"iest", u"iers"}) {
        if (w.size() > suffix.size() + 1 && w.ends_with(suffix)) consider(std::u16string(w.substr(0, w.size() - suffix.size())) + u"y");
    }
    return best;
}

bool EnglishModel::isKnownWord(const std::u16string& token) const {
    auto l = level(token);
    return l && *l <= KNOWN_LEVEL;
}

bool EnglishModel::isCommonWord(const std::u16string& token) const {
    auto l = level(token);
    return l && *l <= COMMON_LEVEL;
}

const EnglishModel& EnglishModel::shared() {
    static const EnglishModel model = load(spellingListPath());
    return model;
}

std::optional<std::filesystem::path> EnglishModel::spellingListPath() {
    std::error_code error;
    if (!Resources::directory.empty() && std::filesystem::is_regular_file(Resources::directory / SPELLING_LIST, error)) {
        return Resources::directory / SPELLING_LIST;
    }
#ifdef _WIN32
    return std::nullopt;
#else
    return SPELLING_LIST_PATH;
#endif
}

EnglishModel EnglishModel::load(const std::optional<std::filesystem::path>& spellingPath) {
    std::unordered_map<std::u16string, int> levels;
    if (auto lines = Resources::lines("english_words.txt")) {
        for (auto& line : *lines) {
            auto tab = line.find(u'\t');
            if (tab == std::u16string::npos || line.find(u'\t', tab + 1) != std::u16string::npos) continue;
            if (auto level = toInt(line.substr(tab + 1))) levels[line.substr(0, tab)] = *level;
        }
    }
    // REA: the extra words go in in the constructor, not into a copy of the 119k-word list.
    auto lines = spellingPath ? Resources::fileLines(*spellingPath) : std::nullopt;
    if (!lines) return EnglishModel(levels, nullptr, commonWords());
    std::vector<std::u16string> spelling;
    spelling.reserve(lines->size() + commonWords().size());
    for (auto& line : *lines) spelling.push_back(lowercase(line));
    spelling.insert(spelling.end(), commonWords().begin(), commonWords().end());
    return EnglishModel(levels, &spelling, commonWords());
}

const std::unordered_set<std::u16string>& EnglishModel::shortWords() {
    static const auto list = rea::words(uR"(
        a i ad ah am an as at aw ax be by do eh go ha he hi hm if in is it lo ma me mo my no of oh ok on or ow ox pa
        pc so to tv uh um up us we ya ye yo id ex vs ai ui ux os js ts
    )");
    static const std::unordered_set<std::u16string> set(list.begin(), list.end());
    return set;
}

const std::vector<std::u16string>& EnglishModel::commonWords() {
    static const auto list = rea::words(uR"(
        ok okay hi hey yeah yep nope lol omg btw thx thanks pls please email emails app apps api apis url urls
        http https www github git commit push pull merge branch repo json yaml xml html css js ts swift python java
        kotlin rust golang npm yarn pnpm node react vue svelte docker kubernetes ios macos android iphone ipad mac wifi bluetooth usb
        login logout signup username password online offline website blog vlog youtube google slack notion figma
        debug deploy refactor config backend frontend database cache server client async await todo todos ai llm gpt
        claude chatgpt laptop smartphone selfie emoji internet podcast startup dev devs devops ui ux
        wanna gonna gotta kinda sorta outta lemme gimme dunno yeah yup nah ain cuz tho thru hiya kiddo sayin
        goin doin nothin somethin y'all ya yo hey oi aye
        wouldn couldn shouldn didn doesn isn wasn weren hasn haven hadn needn mustn aren mightn
        makeup brooklyn shakespeare einstein techno pesto americano cosmo promo garde kanji kana romaji ramen
    )");
    return list;
}

} // namespace rea
