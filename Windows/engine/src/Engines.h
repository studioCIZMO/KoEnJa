#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "DecisionEngine.h"
#include "InputSession.h"

namespace rea {

/**
 * The engine stack iOS's KeyboardViewController builds (makeHeuristic/makeSession): learned
 * corrections, then mixed words (github에, githubで), then the heuristic, weighted by the user's
 * language order (LanguagePreferences).
 */
namespace Engines {
    /** Score per rank step (average log-probability per key, the engine's scale; its context
     *  bias for the previous word's language is 0.8).
     *  ponytail: hand-picked; tune on held-out mixed text once there is some. */
    constexpr double RANK_STEP = 0.3;

    /** [order]: the enabled languages, most used first; English is always there (the layout's own letters). */
    std::shared_ptr<HeuristicEngine> makeHeuristic(const std::vector<Language>& order);

    std::unique_ptr<InputSession> makeSession(
        const std::vector<Language>& order,
        std::shared_ptr<JapaneseConverter> converter,
        bool liveConversion = false,
        bool returnConfirmsOnly = true,
        bool fullWidthDigits = false,
        bool fullWidthSpace = false,
        std::optional<std::filesystem::path> learningFile = std::nullopt);
}

} // namespace rea
