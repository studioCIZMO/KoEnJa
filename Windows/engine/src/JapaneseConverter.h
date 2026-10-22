#pragma once

#include <string>
#include <vector>

namespace rea {

/** One conversion candidate for (a prefix of) the hiragana being converted (JapaneseConverter.swift). */
struct ConversionCandidate {
    std::u16string text;
    /** How many kana from the start this candidate covers; less than all converts the first clause. */
    int kanaCount;
    /** A completion of the reading rather than a conversion of it: listed, never shown live. */
    bool isPrediction = false;

    bool operator==(const ConversionCandidate&) const = default;
};

/** Kana → kanji conversion: a dictionary converter in the IME, fakes in tests. */
class JapaneseConverter {
public:
    virtual ~JapaneseConverter() = default;
    virtual std::vector<ConversionCandidate> candidates(const std::u16string& kana) = 0;
    /** The user committed [candidate]: rank it higher next time. */
    virtual void learn(const ConversionCandidate& candidate, const std::u16string& kana) = 0;
    /** Persist anything learned. */
    virtual void save() = 0;
    /** Candidates for all of [kana] that `candidates` leaves out, in the engine's order: the
     *  candidate window lists them after it, as macOS's Japanese input lists every homophone. */
    virtual std::vector<ConversionCandidate> moreCandidates(const std::u16string&) { return {}; }
};

} // namespace rea
