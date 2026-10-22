#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../engine/src/JapaneseConverter.h"

namespace rea {

/**
 * Kana → kanji over Mozc's OSS dictionary (Android/tools/japanese-dict/build.py): a lattice of
 * dictionary words, Viterbi with word and connection costs, N-best by backward A*. The C++ twin
 * of MozcConverter.kt.
 *
 * [dictionary] is mozc.dic, the unpacked mozc.dic.gz (~53 MB; build.sh unpacks it). It is
 * memory-mapped read-only: only the pages lookups touch are in memory, and every process the
 * TIP loads into shares them. If it can't be mapped, candidates are the kana and katakana only.
 * [learningFile] keeps committed words across launches ("reading\tsurface\tcount" lines); none = memory only.
 */
class MozcConverter : public JapaneseConverter {
public:
    explicit MozcConverter(const std::filesystem::path& dictionary, std::optional<std::filesystem::path> learningFile = std::nullopt);
    ~MozcConverter() override;
    MozcConverter(const MozcConverter&) = delete;
    MozcConverter& operator=(const MozcConverter&) = delete;

    /** Whether the dictionary was mapped. */
    bool loaded() const { return dict != nullptr; }

    std::vector<ConversionCandidate> candidates(const std::u16string& kana) override;
    void learn(const ConversionCandidate& candidate, const std::u16string& kana) override;
    void save() override;

private:
    struct Dictionary;
    struct Node;
    struct Lattice;
    /** What learn needs of a lattice node. */
    struct Word {
        std::u16string reading, surface;
        int lid;
    };

    std::unique_ptr<Dictionary> dict;
    std::optional<std::filesystem::path> learningFile;
    /** Learned "reading\tsurface" → times committed. */
    std::unordered_map<std::u16string, int> learned;
    std::u16string lastKana;
    std::unordered_map<std::u16string, std::vector<Word>> lastPaths;
};

} // namespace rea
