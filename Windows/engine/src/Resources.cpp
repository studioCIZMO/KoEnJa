#include "Resources.h"

#include <cstdlib>
#include <fstream>

#include "Text.h"

namespace rea {

std::filesystem::path Resources::directory = std::getenv("REA_DATA") ? std::getenv("REA_DATA") : "";

std::optional<std::vector<std::u16string>> Resources::lines(const std::string& name) {
    if (directory.empty()) return std::nullopt;
    return fileLines(directory / name);
}

std::optional<std::vector<std::u16string>> Resources::fileLines(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::vector<std::u16string> out;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(utf16(line));
    }
    return out;
}

} // namespace rea
