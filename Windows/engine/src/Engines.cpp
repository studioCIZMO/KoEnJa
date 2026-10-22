#include "Engines.h"

#include <algorithm>

#include "JapaneseSegmenter.h"
#include "MixedWord.h"

namespace rea {

/** Immutable syllable tables shared by every heuristic and splitter. */
static std::shared_ptr<KoreanModel> korean() {
    static const auto model = std::make_shared<KoreanModel>();
    return model;
}

std::shared_ptr<HeuristicEngine> Engines::makeHeuristic(const std::vector<Language>& order) {
    auto bonus = [&](Language language) -> std::optional<double> {
        auto it = std::find(order.begin(), order.end(), language);
        if (it == order.end()) return std::nullopt;
        return double(-(it - order.begin())) * RANK_STEP;
    };
    double english = bonus(Language::ENGLISH).value_or(0.0);
    bool japanese = std::find(order.begin(), order.end(), Language::JAPANESE) != order.end();
    auto heuristic = std::make_shared<HeuristicEngine>(EnglishModel::shared(), korean(), japanese ? JapaneseModel::shared() : nullptr);
    heuristic->koreanOffset = bonus(Language::KOREAN).value_or(-10.0) - english;
    heuristic->japaneseOffset += bonus(Language::JAPANESE).value_or(0.0) - english;
    return heuristic;
}

std::unique_ptr<InputSession> Engines::makeSession(const std::vector<Language>& order, std::shared_ptr<JapaneseConverter> converter,
                                                   bool liveConversion, bool returnConfirmsOnly, bool fullWidthDigits,
                                                   bool fullWidthSpace, std::optional<std::filesystem::path> learningFile) {
    auto heuristic = makeHeuristic(order);
    auto splitting = std::make_shared<SplittingEngine>(heuristic, heuristic->english, heuristic->korean, heuristic->segmenter);
    auto engine = std::make_shared<LearningEngine>(splitting, std::move(learningFile));
    // Kana while typing, candidates in the bar above, as on the system's Japanese keyboard
    // (live conversion off by default: it guessed whole phrases wrong, はなしま → 話魔).
    auto session = std::make_unique<InputSession>(engine, std::move(converter), heuristic->japanese != nullptr, liveConversion, returnConfirmsOnly);
    session->fullWidthDigits = fullWidthDigits;
    session->fullWidthSpace = fullWidthSpace;
    return session;
}

} // namespace rea
