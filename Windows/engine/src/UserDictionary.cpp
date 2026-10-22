#include "UserDictionary.h"

#include <algorithm>
#include <ctime>
#include <fstream>

#include "HangulComposer.h"
#include "JapaneseSegmenter.h"
#include "MixedWord.h"
#include "Resources.h"
#include "RomajiComposer.h"
#include "Text.h"

namespace rea {

using Entry = UserDictionary::Entry;

static const Language languages[] = {Language::ENGLISH, Language::KOREAN, Language::MIXED, Language::JAPANESE};

static std::optional<Language> language(const std::u16string& raw) {
    for (auto l : languages) if (rea::raw(l) == raw) return l;
    return std::nullopt;
}

bool UserDictionary::learn(const std::u16string& keys, Language language, Language shown, std::chrono::sys_days today) {
    if (keys.empty() || std::any_of(entries.begin(), entries.end(), [&](auto& e) { return e.keys == keys && e.source == Entry::Source::User; })) {
        return false;
    }
    auto index = std::find_if(entries.begin(), entries.end(), [&](auto& e) { return e.keys == keys && e.source == Entry::Source::Learned; });
    std::optional<Entry> current;
    if (index != entries.end()) current = *index;
    if (current && current->language == language && current->strength == Entry::Strength::Sure) return false;
    // Back to what the engine showed, or away from what a weak entry turned it to: the
    // nudge was wrong, and a weak entry for the reading switched back to isn't a correction.
    bool rejected = current && current->strength == Entry::Strength::Weak && current->language == shown;
    if (language == shown || rejected) {
        if (index != entries.end()) entries.erase(index);
        return false;
    }
    if (keys.size() > LONGEST_LEARNED) return false;
    bool sure = current && current->language == language;
    Entry entry{keys, language, Entry::Source::Learned, sure ? Entry::Strength::Sure : Entry::Strength::Weak, day(today)};
    if (index != entries.end()) *index = entry; else entries.push_back(entry);
    return sure;
}

bool UserDictionary::used(const std::u16string& keys, Language language, std::chrono::sys_days today) {
    auto d = day(today);
    auto index = std::find_if(entries.begin(), entries.end(), [&](auto& e) { return e.keys == keys && e.strength == Entry::Strength::Weak; });
    if (index == entries.end() || index->language != language || index->last == d) return false;
    index->last = d;
    return true;
}

bool UserDictionary::dropExpired(std::chrono::sys_days today) {
    auto before = entries.size();
    std::erase_if(entries, [&](auto& entry) {
        if (entry.strength != Entry::Strength::Weak) return false;
        auto last = date(entry.last);
        return !last || (today - *last).count() >= WEAK_LIFETIME;
    });
    return entries.size() != before;
}

UserDictionary UserDictionary::decode(const std::vector<std::u16string>& lines, std::chrono::sys_days today) {
    UserDictionary dictionary;
    for (auto& line : lines) {
        std::vector<std::u16string> parts;
        size_t start = 0;
        for (size_t tab; (tab = line.find(u'\t', start)) != std::u16string::npos; start = tab + 1) parts.push_back(line.substr(start, tab - start));
        parts.push_back(line.substr(start));
        // A LearningEngine file (0.1.x): learned twice, or once.
        if (parts.size() == 3 && (parts[0] == u"learned" || parts[0] == u"once")) {
            auto l = language(parts[2]);
            if (!l || parts[1].empty() || (parts[0] == u"once" && parts[1].size() > LONGEST_LEARNED)) continue;
            dictionary.entries.push_back({parts[1], *l, Entry::Source::Learned,
                                          parts[0] == u"learned" ? Entry::Strength::Sure : Entry::Strength::Weak, day(today)});
            continue;
        }
        auto l = parts.size() >= 3 ? language(parts[1]) : std::nullopt;
        if (!l || parts[0].empty()) continue;
        if (parts.size() == 3 && parts[2] == u"user") {
            dictionary.entries.push_back({parts[0], *l, Entry::Source::User, std::nullopt, u""});
        } else if (parts.size() == 5 && parts[2] == u"learned" && (parts[3] == u"weak" || parts[3] == u"sure")) {
            dictionary.entries.push_back({parts[0], *l, Entry::Source::Learned,
                                          parts[3] == u"sure" ? Entry::Strength::Sure : Entry::Strength::Weak, parts[4]});
        }
    }
    return dictionary;
}

std::u16string UserDictionary::encoded() const {
    std::u16string out;
    for (auto& e : entries) {
        out += e.keys + u"\t" + raw(e.language);
        if (e.source == Entry::Source::User) out += u"\tuser\n";
        else out += std::u16string(u"\tlearned\t") + (e.strength == Entry::Strength::Sure ? u"sure" : u"weak") + u"\t" + e.last + u"\n";
    }
    return out;
}

std::chrono::sys_days UserDictionary::today() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    using namespace std::chrono;
    return sys_days{year{local.tm_year + 1900} / month{unsigned(local.tm_mon + 1)} / std::chrono::day{unsigned(local.tm_mday)}};
}

std::u16string UserDictionary::day(std::chrono::sys_days date) {
    std::chrono::year_month_day ymd{date};
    return format("%04d-%02u-%02u", int(ymd.year()), unsigned(ymd.month()), unsigned(ymd.day()));
}

std::optional<std::chrono::sys_days> UserDictionary::date(const std::u16string& day) {
    if (day.size() != 10 || day[4] != u'-' || day[7] != u'-') return std::nullopt;
    auto y = toInt(day.substr(0, 4)), m = toInt(day.substr(5, 2)), d = toInt(day.substr(8, 2));
    if (!y || !m || !d) return std::nullopt;
    std::chrono::year_month_day ymd{std::chrono::year{*y}, std::chrono::month{unsigned(*m)}, std::chrono::day{unsigned(*d)}};
    if (!ymd.ok()) return std::nullopt;
    return std::chrono::sys_days{ymd};
}

DictionaryEngine::DictionaryEngine(std::shared_ptr<DecisionEngine> base, const EnglishModel& english, std::shared_ptr<KoreanModel> korean,
                                   bool japaneseParticles, std::optional<std::filesystem::path> storeFile)
    : base(std::move(base)), english(english), japaneseParticles(japaneseParticles), splitter(english, std::move(korean)),
      storeFile(std::move(storeFile)) {
    if (!this->storeFile) return;
    auto lines = Resources::fileLines(*this->storeFile);
    if (!lines) return;
    store = UserDictionary::decode(*lines, UserDictionary::today());
    bool expired = store.dropExpired(UserDictionary::today());
    for (auto& e : store.entries) byKeys.emplace(e.keys, e);
    if (expired) persist();
}

std::unordered_map<std::u16string, Language> DictionaryEngine::learned() const {
    std::unordered_map<std::u16string, Language> words;
    for (auto& e : store.entries) if (e.wins()) words.emplace(e.keys, e.language);
    return words;
}

const UserDictionary::Entry* DictionaryEngine::entry(const std::u16string& keys) const {
    auto it = byKeys.find(keys);
    return it == byKeys.end() ? nullptr : &it->second;
}

Decision DictionaryEngine::decide(const std::u16string& keys, const DecisionContext& context) {
    auto e = entry(keys);
    if (e && e->wins()) {
        // Rendered by the base engine: this engine's own render(MIXED) would ask it to
        // decide the keys again, find them in the dictionary, and so on without end.
        return Decision{e->language, 1.0, base->render(keys, e->language), e->source == Entry::Source::User ? u"added" : u"learned"};
    }
    if (auto split = withParticle(keys)) return *split;
    auto nudged = context;
    if (e && e->strength == Entry::Strength::Weak) nudged.nudge = e->language;
    return base->decide(keys, nudged);
}

std::optional<Decision> DictionaryEngine::withParticle(const std::u16string& keys) {
    if (keys.size() < 3 || english.isKnownWord(keys)) return std::nullopt;
    for (size_t length = keys.size() - 1; length >= 2; length--) {
        auto word = entry(keys.substr(0, length));
        if (!word || !word->wins()) continue;
        auto tail = keys.substr(length);
        auto how = (word->source == Entry::Source::User ? u"added " : u"learned ") + word->keys + u"+particle";
        switch (word->language) {
            case Language::KOREAN: {
                auto front = HangulComposer::compose(word->keys);
                auto particle = koreanParticle(tail);
                if (!front || !front->strayJamo().empty() || !particle || front->units.empty()) continue;
                auto final = MixedWord::finalSound(front->text().back());
                if (final && MixedWord::particleAgrees(*particle, *final) == false) continue;
                return Decision{Language::KOREAN, SplittingEngine::CONFIDENCE, front->text() + *particle, how};
            }
            case Language::ENGLISH: {
                if (word->keys.size() < 3 && !any(word->keys, isUpperCase)) continue;
                if (auto particle = koreanParticle(tail); particle && !readsAsKoreanWord(keys)) {
                    MixedWord mixed{word->keys, tail, *particle};
                    return Decision{Language::MIXED, SplittingEngine::CONFIDENCE, mixed.text(), how};
                }
                if (japaneseParticles && !RomajiComposer::compose(keys).isClean()) {
                    if (auto particle = japaneseParticle(tail)) {
                        return Decision{Language::JAPANESE, SplittingEngine::CONFIDENCE, word->keys + *particle, how};
                    }
                }
                continue;
            }
            case Language::JAPANESE: {
                auto front = RomajiComposer::compose(word->keys);
                auto particle = japaneseParticle(tail);
                if (!japaneseParticles || !front.isClean() || !particle) continue;
                return Decision{Language::JAPANESE, SplittingEngine::CONFIDENCE, front.text + *particle, how};
            }
            case Language::MIXED:
                continue;
        }
    }
    return std::nullopt;
}

bool DictionaryEngine::readsAsKoreanWord(const std::u16string& keys) const {
    auto composed = HangulComposer::compose(keys);
    if (!composed || !composed->strayJamo().empty()) return false;
    return splitter.isKoreanWordOrWordWithParticle(composed->text());
}

/** The tail as one of the particles and endings that attach to a word (에, 를, 에서, 했어). */
std::optional<std::u16string> DictionaryEngine::koreanParticle(const std::u16string& tail) {
    if (tail.empty() || !DubeolsikLayout::startsSyllable(tail[0])) return std::nullopt;
    auto composed = HangulComposer::compose(tail);
    if (!composed || !composed->strayJamo().empty() || !MixedWord::suffixes().count(composed->text())) return std::nullopt;
    return composed->text();
}

/** The tail as a Japanese particle (で, に, から). */
std::optional<std::u16string> DictionaryEngine::japaneseParticle(const std::u16string& tail) {
    auto composed = RomajiComposer::compose(tail);
    if (!composed.isClean() || !JapaneseSegmenter::trailingParticles().count(composed.text)) return std::nullopt;
    return composed.text;
}

void DictionaryEngine::learn(const std::u16string& keys, Language language, std::optional<Language> engineChoice) {
    Language shown = engineChoice ? *engineChoice : decide(keys, DecisionContext{}).language;
    auto before = store.entries;
    store.learn(keys, language, shown, UserDictionary::today());
    if (store.entries != before) persist();
}

void DictionaryEngine::used(const std::u16string& keys, Language language) {
    if (store.used(keys, language, UserDictionary::today())) persist();
}

void DictionaryEngine::persist() {
    byKeys.clear();
    for (auto& e : store.entries) byKeys.emplace(e.keys, e);
    if (!storeFile) return;
    std::error_code error;
    if (storeFile->has_parent_path()) std::filesystem::create_directories(storeFile->parent_path(), error);
    auto temp = *storeFile;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        if (!out) return;
        out << utf8(store.encoded());
        if (!out) return;
    }
    std::filesystem::rename(temp, *storeFile, error);
}

} // namespace rea
