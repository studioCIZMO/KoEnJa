#include "ParticleSplitter.h"

#include <algorithm>
#include <limits>

#include "HangulComposer.h"
#include "Jamo.h"
#include "JapaneseSegmenter.h"
#include "RomajiComposer.h"
#include "Text.h"

namespace rea {

static constexpr double NEGATIVE_INFINITY = -std::numeric_limits<double>::infinity();

std::u16string ParticleSplitter::text(const Split& split) {
    if (auto korean = std::get_if<Korean>(&split)) return korean->mixed.text();
    auto& japanese = std::get<Japanese>(split);
    return japanese.english + japanese.particle;
}

ParticleSplitter::ParticleSplitter(const EnglishModel& english, std::shared_ptr<KoreanModel> korean,
                                   const JapaneseModel* japanese)
    : english(english), korean(std::move(korean)), japanese(japanese) {}

std::optional<ParticleSplitter::Split> ParticleSplitter::split(const std::u16string& keys, bool japaneseOn) const {
    auto s = scored(keys, japaneseOn);
    if (!s || s->lead < weights.margin) return std::nullopt;
    return s->split;
}

std::optional<ParticleSplitter::Scored> ParticleSplitter::scored(const std::u16string& keys, bool japaneseOn) const {
    const size_t n = keys.size();
    if (n < 3 || english.isKnownWord(keys)) return std::nullopt;
    std::optional<Split> best;
    double bestScore = 0.0;
    auto offer = [&](Split split, const std::u16string& front, double particle) {
        auto w = frontScore(front);
        if (!w) return;
        double score = (*w + particle - weights.switchCost) / double(n);
        if (!best || score > bestScore) { best = std::move(split); bestScore = score; }
    };
    for (size_t start = 2; start < n; start++) {
        auto tail = keys.substr(start);
        if (!DubeolsikLayout::startsSyllable(tail[0])) continue;
        auto composed = HangulComposer::compose(tail);
        if (!composed || !composed->strayJamo().empty()) continue;
        auto text = composed->text();
        if (!MixedWord::suffixes().count(text)) continue;
        auto front = keys.substr(0, start);
        offer(Korean{MixedWord{front, tail, text}}, front,
              coreKorean().count(text) ? weights.coreParticle : weights.otherParticle);
    }
    if (japaneseOn && japanese) {
        for (size_t start = std::max<size_t>(2, n >= 4 ? n - 4 : 0); start < n; start++) {
            auto tail = RomajiComposer::compose(keys.substr(start));
            if (!tail.isClean() || !JapaneseSegmenter::trailingParticles().count(tail.text)) continue;
            auto front = keys.substr(0, start);
            offer(Japanese{front, tail.text}, front,
                  coreJapanese().count(tail.text) ? weights.coreParticle : weights.otherParticle);
        }
    }
    if (!best) return std::nullopt;
    double k = koreanScore(keys);
    double e = english.averageLogProbability(keys) - weights.unknownWord / double(n);
    double j = japaneseOn ? japaneseScore(keys) : NEGATIVE_INFINITY;
    double whole = std::max({k, e, j});
    auto detail = format("split=%.2f ko=%.2f en=%.2f ja=%.2f", bestScore, k, e, j);
    return Scored{*best, bestScore - whole, detail};
}

std::optional<double> ParticleSplitter::frontScore(const std::u16string& front) const {
    if (front.size() < 2 || !all(front, isAsciiLetter)) return std::nullopt;
    // Capitals that are all shifted jamo in keys that spell clean Hangul are Korean typed
    // with shift (akffkRod: 말라깽, tkrlRns: 사기꾼), not an acronym or a name.
    auto composed = HangulComposer::compose(front);
    bool korean = all(front, [](char16_t c) { return !isUpperCase(c) || DubeolsikLayout::isKoreanShiftKey(c); })
        && composed && composed->strayJamo().empty();
    if (!korean && isAcronym(front)) return weights.acronym;
    if (!korean && isName(front)) return weights.name;
    // Lowercase (or a capital at the start: a name, or the start of a sentence) needs
    // 3 letters: so + が, me + で are Japanese.
    if (front.size() < 3) return std::nullopt;
    double spelling = english.averageLogProbability(front) * double(front.size());
    auto level = english.level(front);
    if (level && *level <= EnglishModel::KNOWN_LEVEL) {
        return std::max(spelling, weights.knownBase - weights.perLevel * *level);
    }
    return spelling - weights.unknownWord - weights.unknownFront;
}

bool ParticleSplitter::isName(const std::u16string& front) const {
    if (!any(drop(front, 1), isUpperCase) || !any(front, isLowerCase)) return false;
    int last = -1;
    for (int i = int(front.size()) - 1; i >= 0; i--) if (isUpperCase(front[i])) { last = i; break; }
    if (last < 0) return false;
    if (last == int(front.size()) - 1) {
        // Ends in capitals (macOS): the run of them is the last part.
        int start = last;
        while (start > 0 && isUpperCase(front[start - 1])) start -= 1;
        return int(front.size()) - start >= 2;
    }
    // The last part starts at the last capital (Code of VSCode, Idm of APIdm).
    auto part = front.substr(last);
    return part.size() >= 3 && english.isKnownWord(part);
}

double ParticleSplitter::koreanScore(const std::u16string& keys) const {
    if (SplittingEngine::hasEnglishOnlyCapital(keys)) return NEGATIVE_INFINITY;
    auto score = korean->score(keys);
    if (!score) return NEGATIVE_INFINITY;
    auto& composition = score->composition;
    if (composition.strayJamo().empty() && isKoreanWordOrWordWithParticle(composition.text())) {
        return std::max(score->averageLogProbability, weights.koreanWordFloor);
    }
    return score->averageLogProbability;
}

double ParticleSplitter::japaneseScore(const std::u16string& keys) const {
    if (any(drop(keys, 1), isUpperCase)) return NEGATIVE_INFINITY;
    auto score = japanese ? japanese->score(keys) : std::nullopt;
    if (!score) return NEGATIVE_INFINITY;
    return score->averageLogProbability + weights.japaneseOffset;
}

bool ParticleSplitter::isKoreanWordOrWordWithParticle(const std::u16string& hangul) const {
    if (korean->isVocabularyWord(hangul)) return true;
    if (hangul.size() <= 1) return false;
    for (size_t cut = 1; cut < hangul.size(); cut++) {
        if (korean->isVocabularyWord(hangul.substr(0, cut)) && MixedWord::suffixes().count(hangul.substr(cut))) return true;
    }
    return false;
}

const std::unordered_set<std::u16string>& ParticleSplitter::coreJapanese() {
    static const std::unordered_set<std::u16string> set = {u"で", u"に", u"を", u"は", u"が", u"の", u"と", u"も"};
    return set;
}

const std::unordered_set<std::u16string>& ParticleSplitter::coreKorean() {
    static const auto list = words(u"에 에서 에게 을 를 은 는 이 가 도 로 으로 와 과 의 만 까지 부터 랑 이랑 한테 처럼 보다");
    static const std::unordered_set<std::u16string> set(list.begin(), list.end());
    return set;
}

bool ParticleSplitter::isAcronym(const std::u16string& front) {
    return front.size() >= 2 && front.size() <= 6 && all(front, isUpperCase);
}

} // namespace rea
