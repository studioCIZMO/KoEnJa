#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "DecisionEngine.h"
#include "ParticleSplitter.h"

namespace rea {

/**
 * The user's dictionary: keys typed as a whole word, and the language they become.
 *
 * `User` entries were added by hand and stay until removed. `Learned` ones come from
 * corrections with the switch key: corrected once they are `Weak` and only nudge the
 * decision (and are dropped after [WEAK_LIFETIME] days unused), corrected the same way again
 * they are `Sure` and win.
 *
 * Kept as lines of `keys <TAB> language <TAB> user|learned [<TAB> weak|sure <TAB> yyyy-MM-dd]`
 * (Swift: dictionary.json); a LearningEngine file's `learned|once <TAB> keys <TAB> language`
 * lines are read as sure and weak entries. Unreadable lines are skipped.
 * Not ported: entries with the user's own text, per-app entries (`apps`), and the settings
 * window's add/remove/pin/forget.
 */
struct UserDictionary {
    struct Entry {
        enum class Source { User, Learned };
        enum class Strength { Weak, Sure };

        std::u16string keys;
        Language language;
        Source source;
        /** Learned entries only. */
        std::optional<Strength> strength;
        /** The day a learned entry was last corrected or typed (`yyyy-MM-dd`). */
        std::u16string last;

        /** A language entry that wins the decision: added by hand, or corrected twice. */
        bool wins() const { return strength != Strength::Weak; }
        bool operator==(const Entry&) const = default;
    };

    /** Days a weak entry lasts without being corrected or typed again. */
    static constexpr int WEAK_LIFETIME = 30;
    /** Longer tokens aren't learned: a sentence switched as one token isn't a word. */
    static constexpr size_t LONGEST_LEARNED = 20;

    std::vector<Entry> entries;

    /** Records that [keys] were corrected to [language] while [shown] was on screen.
     *  Returns true when the correction made the entry sure.
     *
     *  - Back to what the engine showed, or away from a weak entry's language while it was
     *    shown (the nudge turned the word wrongly): the learned entry goes.
     *  - The same correction as the weak entry: sure.
     *  - Otherwise: a weak entry for [language], replacing any learned one.
     *  Keys the user added by hand aren't changed by corrections. */
    bool learn(const std::u16string& keys, Language language, Language shown, std::chrono::sys_days today);

    /** A weak entry's word went in as it says: it is still in use, so it stays.
     *  Returns true when something changed. */
    bool used(const std::u16string& keys, Language language, std::chrono::sys_days today);

    /** Drops weak entries not corrected or typed for [WEAK_LIFETIME] days. */
    bool dropExpired(std::chrono::sys_days today);

    static UserDictionary decode(const std::vector<std::u16string>& lines, std::chrono::sys_days today);
    std::u16string encoded() const;

    /** Today in the local time zone, as Swift's `Calendar.current` dates it. */
    static std::chrono::sys_days today();
    static std::u16string day(std::chrono::sys_days date);
    static std::optional<std::chrono::sys_days> date(const std::u16string& day);
};

/**
 * Wraps an engine with the user's dictionary: a language entry that wins (added by hand,
 * or corrected the same way twice) decides the keys, and also the keys with a particle
 * typed straight after them (촉매를, github에, githubで, hogeで); a weak one (corrected once)
 * passes the base engine a nudge toward its language, so only a close call turns.
 *
 * [storeFile]: where the dictionary is kept; none keeps it in memory.
 * ponytail: no lock (Swift locks); TSF calls the IME on one thread.
 */
class DictionaryEngine : public DecisionEngine {
public:
    std::shared_ptr<DecisionEngine> base;
    /** For keys that are an English word as a whole: no particle split there. */
    const EnglishModel& english;
    /** Whether Japanese particles split off (Japanese on). */
    const bool japaneseParticles;

    DictionaryEngine(std::shared_ptr<DecisionEngine> base, const EnglishModel& english = EnglishModel::shared(),
                     std::shared_ptr<KoreanModel> korean = std::make_shared<KoreanModel>(), bool japaneseParticles = false,
                     std::optional<std::filesystem::path> storeFile = std::nullopt);

    /** Language entries that win: keys → language. */
    std::unordered_map<std::u16string, Language> learned() const;

    Decision decide(const std::u16string& keys, const DecisionContext& context) override;

    /** A word that wins, with a particle or ending typed straight after it: Korean as a whole
     *  (촉매를, the particle in the form the word takes), English + Korean particle as mixed
     *  (github에), English or Japanese + Japanese particle as Japanese (githubで, hogeで). The
     *  longest word first; never keys that are an English word as a whole, and not after a
     *  one-key word. An English word, as in [ParticleSplitter], needs 3 letters unless it has
     *  capitals (so + が is そが), and doesn't split keys that read as a Korean word or one with
     *  a particle (sha + 들을 is 놈들을) or as clean romaji (toki + は is ときは). The same
     *  confidence as the engine's own splits, so a Strict app splits too. */
    std::optional<Decision> withParticle(const std::u16string& keys);

    /** Records that [keys] were corrected to [language]: once is weak, the same again sure,
     *  back to the engine's choice forgets the word.
     *
     *  Pass [engineChoice] when the caller already knows what was shown (the input method
     *  always does: a fresh decision might not match). */
    void learn(const std::u16string& keys, Language language, std::optional<Language> engineChoice = std::nullopt);

    /** The word went in as [language] without a correction: a weak entry saying so stays. */
    void used(const std::u16string& keys, Language language);

private:
    /** For keys that read as a Korean word, or one with a particle, as a whole (놈들을, not sha들을). */
    ParticleSplitter splitter;
    UserDictionary store;
    /** The entries by keys, so each key typed looks its keys (and the fronts a particle may
     *  follow) up instead of going through every entry. */
    std::unordered_map<std::u16string, UserDictionary::Entry> byKeys;
    std::optional<std::filesystem::path> storeFile;

    const UserDictionary::Entry* entry(const std::u16string& keys) const;
    bool readsAsKoreanWord(const std::u16string& keys) const;
    void persist();
    static std::optional<std::u16string> koreanParticle(const std::u16string& tail);
    static std::optional<std::u16string> japaneseParticle(const std::u16string& tail);
};

} // namespace rea
