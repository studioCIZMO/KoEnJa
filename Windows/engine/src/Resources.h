#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rea {

/**
 * Where the models' data files (Packages/SeamtypeCore/Sources/SeamtypeCore/Data) come from:
 * english_words.txt, kana_trigrams.txt, korean_words.txt,
 * korean_vocabulary.txt, korean_syllables.txt, and words.txt (EnglishModel::SPELLING_LIST). Set [directory] before the first model loads
 * (the IME points it at its install folder); by default it is the REA_DATA environment variable.
 */
namespace Resources {
    extern std::filesystem::path directory;

    /** The file's non-empty lines; nullopt when the file isn't there. */
    std::optional<std::vector<std::u16string>> lines(const std::string& name);

    /** The same for a file by its own path. */
    std::optional<std::vector<std::u16string>> fileLines(const std::filesystem::path& path);
}

} // namespace rea
