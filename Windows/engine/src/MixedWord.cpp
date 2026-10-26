#include "MixedWord.h"

#include "HangulComposer.h"
#include "Jamo.h"
#include "JapaneseSegmenter.h"
#include "ParticleSplitter.h"
#include "RomajiComposer.h"
#include "Sebeolsik.h"
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

const std::vector<std::pair<std::u16string, std::u16string>>& MixedWord::pairedParticles() {
    static const std::vector<std::pair<std::u16string, std::u16string>> list = {
        {u"을", u"를"}, {u"은", u"는"}, {u"이", u"가"}, {u"과", u"와"}, {u"이랑", u"랑"}, {u"이랑은", u"랑은"},
        {u"으로", u"로"}, {u"으로는", u"로는"}, {u"으로도", u"로도"}, {u"으론", u"론"}, {u"으로서", u"로서"},
        {u"이나", u"나"}, {u"이야", u"야"}, {u"이라", u"라"}, {u"이라고", u"라고"}, {u"이라는", u"라는"}, {u"이란", u"란"},
        {u"이에요", u"예요"},
    };
    return list;
}

std::optional<bool> MixedWord::particleAgrees(const std::u16string& particle, FinalSound final) {
    for (auto& [consonant, vowel] : pairedParticles()) {
        if (particle == consonant) return final == FinalSound::Consonant || (final == FinalSound::Rieul && !startsWith(consonant, u"으"));
        if (particle == vowel) return final == FinalSound::Vowel || (final == FinalSound::Rieul && startsWith(consonant, u"으"));
    }
    return std::nullopt;
}

std::optional<MixedWord::FinalSound> MixedWord::finalSound(char16_t syllable) {
    if (syllable < 0xAC00 || syllable > 0xD7A3) return std::nullopt;
    switch ((syllable - 0xAC00) % 28) {
        case 0: return FinalSound::Vowel;
        case 8: return FinalSound::Rieul;
        default: return FinalSound::Consonant;
    }
}

MixedWord::FinalSound MixedWord::finalSoundOfAcronym(const std::u16string& acronym) {
    char16_t last = acronym.empty() ? 0 : acronym.back();
    if (last >= u'a' && last <= u'z') last -= 0x20;
    if (last == u'L' || last == u'R') return FinalSound::Rieul;
    if (last == u'M' || last == u'N') return FinalSound::Consonant;
    return FinalSound::Vowel;
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
    if (isSebeolsik()) return Sebeolsik::startsSyllable(key);
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
    if (single && !english.isKnownWord(keys) && !particles->readsAsKorean(keys)) {
        return Decision{Language::MIXED, CONFIDENCE, single->text(), u"split " + single->english + u"+" + single->korean};
    }
    return base->decide(keys, context);
}

bool SplittingEngine::hasEnglishOnlyCapital(const std::u16string& front) { return DubeolsikLayout::hasEnglishOnlyCapital(front); }

} // namespace rea
