#include "MixedWord.h"

#include <algorithm>
#include <cmath>

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
    if (auto decision = decideJoined(keys, context)) return *decision;
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

std::optional<Decision> SplittingEngine::decideJoined(const std::u16string& keys, const DecisionContext& context) {
    if (isSebeolsik() || keys.empty() || !isLetter(keys[0]) || !any(keys, joinsWord)) return std::nullopt;
    auto baseDecision = base->decide(keys, context);
    // An English word before the mark keeps it English, as it did before the mark joined it
    // (five-o, woo-hoo), and so do English words throughout (to-do, co-op); a two-letter
    // front that is also romaji (pa-tigakita, ra-men) doesn't.
    std::vector<std::u16string> segments;
    for (size_t i = 0, start = 0; i <= keys.size(); i++) {
        if (i < keys.size() && !joinsWord(keys[i])) continue;
        if (i > start) segments.push_back(keys.substr(start, i - start));
        start = i + 1;
    }
    auto known = [&](const std::u16string& s) { return english.isKnownWord(s); };
    if (baseDecision.language == Language::JAPANESE && RomajiComposer::compose(keys).strayLetters == 0 &&
        !(!segments.empty() && segments[0].size() >= 3 && known(segments[0])) &&
        !std::all_of(segments.begin(), segments.end(), known)) return baseDecision;

    struct Piece { std::u16string text; bool letters; };
    std::vector<Piece> pieces;
    for (char16_t ch : keys) {
        bool letters = !joinsWord(ch);
        if (!pieces.empty() && pieces.back().letters == letters) pieces.back().text += ch;
        else pieces.push_back({std::u16string(1, ch), letters});
    }
    std::u16string output;
    bool hasEnglish = false, hasKorean = false;
    double confidence = CONFIDENCE;
    std::optional<Language> previous; // how the last letters before this piece went in
    for (size_t index = 0; index < pieces.size(); index++) {
        auto& piece = pieces[index];
        if (!piece.letters) { output += piece.text; continue; }
        bool beforeDigit = index + 1 < pieces.size() && isNumber(pieces[index + 1].text.front());
        bool afterDigit = index > 0 && isNumber(pieces[index - 1].text.back());
        if (readsAsKoreanPiece(piece.text, beforeDigit, afterDigit, previous == Language::ENGLISH, index == pieces.size() - 1)) {
            output += HangulComposer::composeRuns(piece.text);
            hasKorean = true;
            previous = Language::KOREAN;
            // Only as sure as the letters alone are (as the word without the digit would go in),
            // so a Strict app keeps an unsure one English.
            if (index == 0) confidence = std::min(confidence, base->decide(piece.text, context).confidence);
        } else if (auto split = index > 0 && index == pieces.size() - 1 ? particleAfter(piece.text) : std::nullopt) {
            // A particle ending the word after the last digit or mark, or one letter on (mp3 + 를,
            // k8s + 에: sdp; not mp3fmf-1); not an English word there (merry-go: not 해, heh-heh: not
            // 도), nor more of one (mani-pedi).
            output += split->first + split->second;
            hasKorean = true;
            if (!split->first.empty()) hasEnglish = true;
        } else {
            output += piece.text;
            hasEnglish = true;
            previous = Language::ENGLISH;
        }
    }
    Language language = hasEnglish && hasKorean ? Language::MIXED : hasKorean ? Language::KOREAN : Language::ENGLISH;
    return Decision{language, confidence, output, u"joined " + output};
}

bool SplittingEngine::readsAsKoreanPiece(const std::u16string& keys, bool beforeDigit, bool afterDigit, bool afterEnglish,
                                         bool atEnd) const {
    auto lead = koreanLead(keys);
    auto composed = HangulComposer::compose(keys);
    if (!lead || !composed) return false;
    auto text = composed->text();
    // After English a particle is the English word's, at the word's end only (mp3를, mp3에서; not
    // mp3를-1, mp3에서-1); after Korean the same syllables are words of their own (해서_1, 같은_것).
    bool particle = afterEnglish && MixedWord::suffixes().count(text);
    if (particle && !atEnd) return false;
    auto& w = particles->weights;
    bool counted = (afterDigit && !particle) || (beforeDigit && numberWords().count(text));
    return *lead > w.identifierCost + (composed->syllables().size() < 2 && !counted ? w.oneSyllableInIdentifier : 0);
}

std::optional<double> SplittingEngine::koreanLead(const std::u16string& keys) const {
    if (keys.empty() || !DubeolsikLayout::startsSyllable(keys[0])) return std::nullopt;
    auto composed = HangulComposer::compose(keys);
    if (!composed || !composed->strayJamo().empty()) return std::nullopt;
    auto& w = particles->weights;
    double korean = particles->koreanScore(keys) * double(keys.size());
    if (!std::isfinite(korean)) return std::nullopt;
    auto lower = lowercase(keys);
    double spelling = english.averageLogProbability(lower) * double(keys.size());
    auto upper = keys;
    for (auto& c : upper) if (isLowerCase(c)) c -= 0x20;
    double englishScore;
    auto level = english.level(lower);
    if (level && *level <= EnglishModel::KNOWN_LEVEL) englishScore = std::max(spelling, w.knownBase - w.perLevel * double(*level));
    else if (EnglishModel::acronyms().count(upper)) englishScore = std::max(spelling, w.acronym);
    else englishScore = spelling - w.unknownWord;
    return korean - englishScore;
}

const std::unordered_set<std::u16string>& SplittingEngine::numberWords() {
    static const std::unordered_set<std::u16string> set = {u"제", u"주", u"표", u"장", u"절", u"항", u"조", u"호"};
    return set;
}

std::optional<std::pair<std::u16string, std::u16string>> SplittingEngine::particleAfter(const std::u16string& keys) const {
    if (english.isKnownWord(keys)) return std::nullopt;
    for (size_t start = 0; start < std::min<size_t>(2, keys.size()); start++) {
        auto suffix = keys.substr(start);
        if (!DubeolsikLayout::startsSyllable(suffix[0])) continue;
        auto composed = HangulComposer::compose(suffix);
        if (!composed || !composed->strayJamo().empty() || !MixedWord::suffixes().count(composed->text())) continue;
        return std::pair(keys.substr(0, start), composed->text());
    }
    return std::nullopt;
}

bool SplittingEngine::hasEnglishOnlyCapital(const std::u16string& front) { return DubeolsikLayout::hasEnglishOnlyCapital(front); }

} // namespace rea
