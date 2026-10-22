#include "DecisionEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "HangulComposer.h"
#include "JapaneseSegmenter.h"
#include "Jamo.h"
#include "MixedWord.h"
#include "RomajiComposer.h"
#include "Text.h"

namespace rea {

Language sentenceLanguage(Language language) {
    switch (language) {
        case Language::ENGLISH: return Language::ENGLISH;
        case Language::KOREAN: case Language::MIXED: return Language::KOREAN;
        case Language::JAPANESE: return Language::JAPANESE;
    }
    return language;
}

std::u16string raw(Language language) {
    switch (language) {
        case Language::ENGLISH: return u"english";
        case Language::KOREAN: return u"korean";
        case Language::MIXED: return u"mixed";
        case Language::JAPANESE: return u"japanese";
    }
    return u"";
}

Language nextLanguage(Language language, const std::u16string& keys, bool japanese, std::optional<bool> splits) {
    std::vector<Language> available;
    for (auto l : {Language::ENGLISH, Language::MIXED, Language::KOREAN, Language::JAPANESE}) {
        bool ok = false;
        switch (l) {
            case Language::ENGLISH: ok = true; break;
            case Language::MIXED: ok = splits ? *splits : MixedWord::split(keys).has_value(); break;
            case Language::KOREAN: ok = HangulComposer::compose(keys).has_value(); break;
            case Language::JAPANESE: ok = japanese; break;
        }
        if (ok) available.push_back(l);
    }
    std::vector<Language> clean;
    for (auto l : available) {
        bool ok = true;
        if (l == Language::KOREAN) {
            auto composed = HangulComposer::compose(keys);
            ok = composed && (composed->strayJamo().empty() || composed->isJamoAbbreviation());
        } else if (l == Language::JAPANESE) {
            ok = RomajiComposer::compose(keys).strayLetters == 0;
        }
        if (ok) clean.push_back(l);
    }
    std::vector<Language> cycle;
    if (clean.size() >= 2) {
        for (auto l : available) if (std::find(clean.begin(), clean.end(), l) != clean.end() || l == language) cycle.push_back(l);
    } else {
        cycle = available;
    }
    auto it = std::find(cycle.begin(), cycle.end(), language);
    if (it == cycle.end()) return cycle.empty() ? Language::ENGLISH : cycle.front();
    return cycle[(it - cycle.begin() + 1) % cycle.size()];
}

std::u16string DecisionEngine::render(const std::u16string& keys, Language language) {
    switch (language) {
        case Language::ENGLISH: return keys;
        case Language::KOREAN: {
            auto composed = HangulComposer::compose(keys);
            return composed ? composed->text() : keys;
        }
        case Language::MIXED: {
            // The engine's own split (hello월드, github에); the particle split for engines without one.
            auto decision = decide(keys, DecisionContext{});
            if (decision.language == Language::MIXED) return decision.output;
            auto split = MixedWord::split(keys);
            return split ? split->text() : keys;
        }
        case Language::JAPANESE: return RomajiComposer::compose(keys).text;
    }
    return keys;
}

HeuristicEngine::HeuristicEngine(const EnglishModel& english, std::shared_ptr<KoreanModel> korean, const JapaneseModel* japanese)
    : english(english), korean(std::move(korean)), japanese(japanese),
      segmenter(japanese ? std::make_shared<JapaneseSegmenter>(english, *japanese) : nullptr) {}

Decision HeuristicEngine::decide(const std::u16string& keys, const DecisionContext& context) {
    // Shift on a key that has no shifted jamo (e.g. "Hello", "API") is only ever English,
    // unless it lingers from a Korean shift key onto the next key (djEJs 어떤, dPFmf 예를):
    // then the scores decide.
    if (DubeolsikLayout::hasEnglishOnlyCapital(keys)) {
        return Decision{Language::ENGLISH, 0.99, keys, u"uppercase outside QWERTOP"};
    }

    double e = english.averageLogProbability(filter(keys, isLetter));
    // Also while typing: the preview must show what Space/Return will commit.
    bool known = english.isKnownWord(keys);
    if (known) {
        // Short dictionary words collide with real Korean (go=해, so=내, do=애),
        // so they get a weaker boost and leave room for context to decide.
        double floor = keys.size() >= 4 ? -1.4 : keys.size() == 3 ? -2.0 : -2.6;
        // to, so, go, do: everyday English just above a one-syllable Korean word (새, 내,
        // 해, 애), so they stay English unless the previous word was Korean.
        if (keys.size() == 2 && english.isCommonWord(keys)) floor = koreanKnownFloorShort + 0.1;
        if (english.level(keys).value_or(0) > EnglishModel::EXTRA_LEVEL) floor -= rareWordFloorPenalty;
        // Capitalized (The, To): a sentence starting, not ㅆ쏟 or 쌔 typed with Shift.
        if (isUpperCase(keys[0]) && all(drop(keys, 1), isLowerCase)) floor = std::max(floor, -1.4);
        e = std::max(e, floor);
    }
    if (any(keys, [](char16_t c) { return !isLetter(c); })) e -= 3; // "ra-men", "kan'i" aren't English

    auto k = korean->score(keys, context.partial);
    bool knownKorean = false;
    if (k && k->composition.strayJamo().empty()) {
        auto text = k->composition.text();
        knownKorean = korean->isKnownWord(text)
            || (!known && (korean->isVocabularyWord(text) || korean->isRareVocabularyWord(text) || korean->isKnownStem(text)));
    }
    if (k && knownKorean) {
        double floor = k->composition.syllables().size() >= 2 ? koreanKnownFloorLong : koreanKnownFloorShort;
        k->averageLogProbability = std::max(k->averageLogProbability, floor);
    }
    // A dictionary English word may already be complete, and so are chat jamo (ㅜㅜ): score
    // the Japanese reading as finished too, so the preview shows what Space will commit
    // ("to" stays to, not と; nn shows ㅜㅜ, not ん that Space turned into ㅜㅜ). So is a
    // doubled consonant with nothing after its っ (ff, ww): no Japanese word is only っ, so
    // it isn't one on its way (ff showed っf and went in as ff); って (tte) still is.
    bool chat = k && k->composition.isChat();
    bool sokuonFirst = RomajiComposer::compose(keys, true).isOnlySokuon();
    // A capital is never romaji: Japanese IMEs type ⇧ letters as themselves (⇧O is O, not お).
    bool capital = any(keys, isUpperCase);
    auto j = japanese && !capital ? japanese->score(keys, context.partial && !known && !chat && !sokuonFirst) : std::nullopt;
    // English words inside a Japanese phrase (googlede…) score as segmented Japanese,
    // not as kana with stray letters.
    auto segmented = j && j->composition.strayLetters > 0 && segmenter ? segmenter->scoredSegments(keys) : std::nullopt;
    // REA: the capitals inside an English word that leads into Japanese (iPhonedetesuto is
    // iPhone + でてすと) keep the phrase Japanese; a capital still never becomes kana.
    if (!j && capital && segmenter) {
        auto s = segmenter->scoredSegments(keys);
        if (s && std::all_of(s->segments.begin(), s->segments.end(), [](auto& segment) {
                auto japanese = std::get_if<JapaneseSegmenter::Japanese>(&segment);
                return !japanese || !any(japanese->keys, isUpperCase);
            })) {
            segmented = s;
        }
    }
    std::optional<double> japaneseScore;
    if (j) japaneseScore = std::max(j->averageLogProbability, segmented ? segmented->score / double(keys.size()) : -std::numeric_limits<double>::infinity());
    else if (segmented) japaneseScore = segmented->score / double(keys.size()); // REA: the segmented reading alone

    std::vector<std::pair<Language, double>> scores;
    scores.push_back({Language::ENGLISH, e});
    if (k) scores.push_back({Language::KOREAN, k->averageLogProbability + koreanOffset});
    if (japaneseScore) scores.push_back({Language::JAPANESE, *japaneseScore + japaneseOffset});
    std::optional<Language> previous;
    if (context.previousLanguage) previous = sentenceLanguage(*context.previousLanguage);
    auto i = std::find_if(scores.begin(), scores.end(), [&](auto& s) { return previous && s.first == *previous; });
    if (previous && i != scores.end()) {
        double bias = *previous == Language::JAPANESE ? japaneseContextBias : contextBias;
        const std::pair<Language, bool> knownIn[] = {{Language::ENGLISH, known}, {Language::KOREAN, knownKorean}, {Language::JAPANESE, false}};
        bool knownInPrevious = false, knownElsewhere = false;
        for (auto [l, isKnown] : knownIn) {
            if (l == *previous) knownInPrevious = isKnown;
            else if (isKnown) knownElsewhere = true;
        }
        if (!knownInPrevious && knownElsewhere) bias *= switchBiasFactor;
        i->second += bias;
    }
    if (context.nudge && *context.nudge != Language::MIXED) {
        auto n = std::find_if(scores.begin(), scores.end(), [&](auto& s) { return s.first == *context.nudge; });
        if (n != scores.end()) n->second += correctionBias;
    }
    std::stable_sort(scores.begin(), scores.end(), [](auto& a, auto& b) { return a.second > b.second; });

    // A dictionary English word only becomes kana on a clear win, unless we're already in Japanese
    // text or the user corrected it to Japanese once (where "same", "sake", "kite" are Japanese words).
    auto englishIndex = std::find_if(scores.begin(), scores.end(), [](auto& s) { return s.first == Language::ENGLISH; }) - scores.begin();
    if (known && scores[0].first == Language::JAPANESE && previous != Language::JAPANESE && context.nudge != Language::JAPANESE &&
        englishIndex < long(scores.size()) && scores[0].second - scores[englishIndex].second < englishWordProtection) {
        std::swap(scores[0], scores[englishIndex]);
    }

    auto [language, best] = scores[0];
    std::optional<std::pair<Language, double>> runnerUp;
    if (scores.size() > 1) runnerUp = scores[1];
    double margin = best - (runnerUp ? runnerUp->second : best - 5);
    double confidence = 1 / (1 + std::exp(-margin * 2));
    std::u16string output = keys;
    if (language == Language::KOREAN && k) output = k->composition.text();
    if (language == Language::JAPANESE) {
        if (segmented) output = JapaneseSegmenter::text(segmented->segments);
        else if (j) output = j->composition.text;
    }
    std::u16string detail;
    for (auto& [l, score] : scores) {
        if (!detail.empty()) detail += u" ";
        detail += raw(l).substr(0, 2) + format("=%.2f", score);
    }
    return Decision{language, confidence, output,
                    detail + (known ? u" (dict)" : u"") + format(" margin=%+.2f", margin),
                    runnerUp ? std::optional(runnerUp->first) : std::nullopt};
}

} // namespace rea
