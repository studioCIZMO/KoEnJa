#include "MixedWord.h"

#include "HangulComposer.h"
#include "Jamo.h"
#include "JapaneseSegmenter.h"
#include "ParticleSplitter.h"
#include "RomajiComposer.h"
#include "Text.h"

namespace rea {

const std::unordered_set<std::u16string>& MixedWord::suffixes() {
    static const auto list = words(uR"(
        에 에서 에게 에는 에서는 에도 에서도 의 을 를 은 는 이 가 도 로 으로 와 과 랑 이랑 하고 한테 까지 부터 만 보다
        처럼 이나 나 이야 야 이에요 예요 이다 입니다 인데 이라 이라고 라고 으로는 로는 으로도 로도 이고 이지 이랑은
        했어 했다 했어요 했는데 했고 했지 했음 해서 해야 하는 하면 하고 하기 하게 하다 해 해요 합니다 할 한 함 하자 해줘 해봐
        됐어 됐다 된 되는 되면 돼 돼요 됩니다 같은 같아 적 적인 적으로 스럽게 같이 들 들이 들을 들은 에요 이요 를요 을요
        같은건 같은거 같은게 같은걸 같은데 같다 같네 같아서 같은지 같고 같아요 인가 인지 인데요 인건 인게 인거
        이라는 라는 이란 에선 으론 로서 으로서
    )");
    static const std::unordered_set<std::u16string> set(list.begin(), list.end());
    return set;
}

const std::unordered_set<std::u16string>& MixedWord::singleLetterParticles() {
    static const auto list = words(uR"(
        에 에서 에게 에는 에서는 에도 의 을 를 은 는 이 가 도 로 으로 와 과 랑 이랑 하고 한테 까지 부터 만 보다
        처럼 이나 나 이라 이라고 라고 이랑은 으로는 로는 같은 같이
    )");
    static const std::unordered_set<std::u16string> set(list.begin(), list.end());
    return set;
}

std::optional<MixedWord> MixedWord::splitSingleLetter(const std::u16string& keys) {
    if (keys.size() < 2 || !isAsciiLetter(keys[0])) return std::nullopt;
    char16_t first = keys[0];
    auto suffixKeys = keys.substr(1);
    if (!DubeolsikLayout::startsSyllable(suffixKeys[0])) return std::nullopt;
    auto composed = HangulComposer::compose(suffixKeys);
    if (!composed) return std::nullopt;
    auto text = composed->text();
    if (!composed->strayJamo().empty() || !singleLetterParticles().count(text)) return std::nullopt;
    // 도 and 나 are the keys eh and sk, common English endings (heh, meh, yeh, tsk): after a
    // lowercase letter they stay English. A capital (X도) was typed on purpose.
    if (isLowerCase(first) && (text == u"도" || text == u"나")) return std::nullopt;
    return MixedWord{std::u16string(1, first), suffixKeys, text};
}

std::optional<MixedWord> MixedWord::split(const std::u16string& keys) {
    // Front needs at least 2 letters; try the longest suffix first.
    if (keys.size() < 3) return std::nullopt;
    for (size_t start = 2; start < keys.size(); start++) {
        auto suffixKeys = keys.substr(start);
        if (!DubeolsikLayout::startsSyllable(suffixKeys[0])) continue;
        auto composed = HangulComposer::compose(suffixKeys);
        if (!composed || !composed->strayJamo().empty() || !suffixes().count(composed->text())) continue;
        return MixedWord{keys.substr(0, start), suffixKeys, composed->text()};
    }
    return std::nullopt;
}

bool DubeolsikLayout::startsSyllable(char16_t key) {
    auto j = jamo(key);
    return j && Jamo::isConsonant(*j);
}

SplittingEngine::SplittingEngine(std::shared_ptr<DecisionEngine> base, const EnglishModel& english,
                                 std::shared_ptr<KoreanModel> korean, std::shared_ptr<JapaneseSegmenter> japanese)
    : base(std::move(base)), english(english), korean(std::move(korean)), japanese(std::move(japanese)),
      particles(std::make_shared<ParticleSplitter>(english, this->korean, this->japanese ? &this->japanese->japanese : nullptr)) {}

Decision SplittingEngine::decide(const std::u16string& keys, const DecisionContext& context) {
    // English word + particle, Korean (github에) or with Japanese on Japanese (githubで),
    // when that reading beats the whole token in one language. Before English + Korean
    // word: beautywo is beauty + を, not beau + 쇼재.
    if (auto split = particles->split(keys, japanese != nullptr)) {
        if (auto korean = std::get_if<ParticleSplitter::Korean>(&*split)) {
            return Decision{Language::MIXED, CONFIDENCE, korean->mixed.text(), u"split " + korean->mixed.english + u"+" + korean->mixed.korean};
        }
        auto& japanese = std::get<ParticleSplitter::Japanese>(*split);
        return Decision{Language::JAPANESE, CONFIDENCE, japanese.english + japanese.particle, u"split " + japanese.english + u"+" + japanese.particle};
    }
    auto single = MixedWord::splitSingleLetter(keys);
    if (single && !english.isKnownWord(keys) && !readsAsKorean(keys)) {
        return Decision{Language::MIXED, CONFIDENCE, single->text(), u"split " + single->english + u"+" + single->korean};
    }
    if (auto mixed = englishThenKoreanWord(keys)) {
        return Decision{Language::MIXED, CONFIDENCE, mixed->text(), u"split " + mixed->english + u"+" + mixed->korean};
    }
    return base->decide(keys, context);
}

std::optional<MixedWord> SplittingEngine::englishThenKoreanWord(const std::u16string& keys) {
    if (keys.size() < 5 || english.isKnownWord(keys)) return std::nullopt;
    double spelling = english.averageLogProbability(keys);
    // Clean romaji (hasansowo, はさんそを) is Japanese, not English + Hangul.
    if (spelling >= mixedWordSpellingCeiling || RomajiComposer::compose(keys).isClean()) return std::nullopt;
    for (size_t start = keys.size() - 2; start >= 2; start--) {
        auto front = keys.substr(0, start);
        auto back = keys.substr(start);
        // Capitals count only when one has no Korean meaning (ThkT is 쐈, not a name).
        if (!((hasEnglishOnlyCapital(front) && (isAcronym(front) || isCapitalizedName(front)))
              || (front.size() >= 3 && english.isKnownWord(front)))) continue;
        if (!DubeolsikLayout::startsSyllable(back[0])) continue;
        auto composed = HangulComposer::compose(back);
        if (!composed) continue;
        auto syllables = composed->syllables();
        if (!composed->strayJamo().empty() || syllables.size() < 2) continue;
        if (!(isKoreanWordOrWordWithParticle(composed->text())
              || (spelling < unknownWordSpellingCeiling
                  && all(syllables, [&](char16_t s) { return korean->syllableLogProbability(s) > KoreanModel::UNKNOWN_SYLLABLE_LOG_P; })))) continue;
        if (!(hasEnglishOnlyCapital(front) || !readsAsKorean(keys))) return std::nullopt;
        return MixedWord{front, back, composed->text()};
    }
    return std::nullopt;
}

bool SplittingEngine::isKoreanWordOrWordWithParticle(const std::u16string& hangul) const {
    if (korean->isVocabularyWord(hangul)) return true;
    for (size_t cut = 1; cut < hangul.size(); cut++) {
        if (cut >= 2 && korean->isVocabularyWord(hangul.substr(0, cut)) && MixedWord::suffixes().count(hangul.substr(cut))) return true;
    }
    return false;
}

bool SplittingEngine::readsAsKorean(const std::u16string& keys) const {
    auto composed = HangulComposer::compose(keys);
    if (!composed || !composed->strayJamo().empty()) return false;
    return all(composed->syllables(), [&](char16_t s) { return korean->syllableLogProbability(s) > KoreanModel::UNKNOWN_SYLLABLE_LOG_P; });
}

bool SplittingEngine::isAcronym(const std::u16string& front) {
    return front.size() >= 2 && front.size() <= 6 && all(front, isUpperCase)
        && any(front, [](char16_t c) { return !DubeolsikLayout::isKoreanShiftKey(c); });
}

bool SplittingEngine::isCapitalizedName(const std::u16string& front) {
    if (front.size() < 3 || !isUpperCase(front[0]) || !any(front, isLowerCase)) return false;
    if (hasEnglishOnlyCapital(front)) return true;
    if (!any(drop(front, 1), isUpperCase)) return false;
    // Every capital a shifted jamo and the keys spelling whole Hangul is Korean typed with shift,
    // even with a typo making an odd syllable (RoRmtgkgfu, 깨끗핳려), not a name (TextEdit).
    auto composed = HangulComposer::compose(front);
    return composed ? !composed->strayJamo().empty() : true;
}

bool SplittingEngine::hasEnglishOnlyCapital(const std::u16string& front) {
    return any(front, [](char16_t c) { return isUpperCase(c) && !DubeolsikLayout::isKoreanShiftKey(c); });
}

} // namespace rea
