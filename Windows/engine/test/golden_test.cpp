// Replays golden_input.txt as Android/tools/golden (the Swift engine) did and compares with
// golden.tsv, its output: config \t keys \t text added. The C++ twin of GoldenTest.kt.
//
// Usage: golden_test <data dir> <dir with golden_input.txt and golden.tsv>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "../src/Engines.h"
#include "../src/Resources.h"
#include "../src/Text.h"

using namespace rea;

/** The text field: everything before the cursor ([value]), the marked part at its end. */
struct Field : TextClient {
    std::u16string value;
    TextRange marked{TextRange::NOT_FOUND, 0};

    int end() const { return int(value.size()); }
    void setMarkedText(const std::u16string& t) override { setMarkedText(t, TextRange{end(), 0}); }
    void setMarkedText(const std::u16string& t, TextRange replacing) override {
        int start = marked.location == TextRange::NOT_FOUND ? replacing.location : std::min(replacing.location, marked.location);
        value = value.substr(0, start) + t;
        marked = t.empty() ? TextRange{TextRange::NOT_FOUND, 0} : TextRange{start, int(t.size())};
    }
    void insertText(const std::u16string& t, std::optional<TextRange> replacing) override {
        TextRange r = replacing ? *replacing : marked.location == TextRange::NOT_FOUND ? TextRange{end(), 0} : marked;
        int start = marked.location == TextRange::NOT_FOUND ? r.location : std::min(r.location, marked.location);
        value = value.substr(0, start) + t;
        marked = TextRange{TextRange::NOT_FOUND, 0};
    }
    TextRange selectedRange() override { return {end(), 0}; }
    TextRange markedRange() override { return marked; }
    std::optional<std::u16string> text(TextRange range) override {
        if (range.location >= 0 && range.end() <= end()) return value.substr(range.location, range.length);
        return std::nullopt;
    }
};

static std::unique_ptr<InputSession> session(const std::vector<Language>& order) {
    auto s = Engines::makeSession(order, nullptr);
    s->activate();
    return s;
}

static std::vector<std::u16string> split(const std::u16string& s, char16_t separator) {
    std::vector<std::u16string> out;
    size_t start = 0;
    for (size_t at; (at = s.find(separator, start)) != std::u16string::npos; start = at + 1) out.push_back(s.substr(start, at - start));
    out.push_back(s.substr(start));
    return out;
}

static std::u16string resource(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { fprintf(stderr, "missing %s\n", path.c_str()); exit(2); }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return utf16(buffer.str());
}

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: golden_test <data dir> <golden dir>\n"); return 2; }
    Resources::directory = argv[1];
    std::string golden = argv[2];
    auto lines = split(resource(golden + "/golden_input.txt"), u'\n');
    std::vector<std::u16string> expected;
    for (auto& line : split(resource(golden + "/golden.tsv"), u'\n')) if (!line.empty()) expected.push_back(line);

    // Models load on first use, outside the timing.
    auto loadStart = std::chrono::steady_clock::now();
    EnglishModel::shared();
    JapaneseModel::shared();
    session({Language::KOREAN, Language::ENGLISH, Language::JAPANESE});
    double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStart).count();

    std::vector<std::u16string> actual;
    const std::pair<std::u16string, std::vector<Language>> configs[] = {
        {u"ko-en-ja", {Language::KOREAN, Language::ENGLISH, Language::JAPANESE}},
        {u"ko-ja-en", {Language::KOREAN, Language::JAPANESE, Language::ENGLISH}},
    };
    long keyCount = 0;
    auto start = std::chrono::steady_clock::now();
    for (auto& [name, order] : configs) {
        auto s = session(order);
        Field field;
        for (auto& line : lines) {
            if (line.empty()) continue;
            if (line == u"#reset") { s = session(order); field = Field(); continue; }
            size_t before = field.value.size();
            for (char16_t c : line) {
                keyCount++;
                using Kind = InputSession::Key::Kind;
                InputSession::Key key = c == u'⏎' ? InputSession::Key{Kind::Enter}
                    : c == u'⌫' ? InputSession::Key{Kind::Delete}
                    : c == u'※' ? InputSession::Key{Kind::Cycle}
                    : isLetter(c) ? InputSession::Key::letter(c) : InputSession::Key::printable(c);
                if (!s->handle(key, field)) {
                    switch (key.kind) {
                        case Kind::Enter: field.insertText(u"\n", std::nullopt); break;
                        case Kind::Delete: if (!field.value.empty()) field.value.pop_back(); break;
                        case Kind::Printable: case Kind::Letter: field.insertText(std::u16string(1, key.ch), std::nullopt); break;
                        default: break;
                    }
                }
            }
            s->commit(&field);
            std::u16string added = field.value.size() >= before ? field.value.substr(before) : u"(deleted)";
            std::u16string shown;
            for (char16_t c : added) shown += c == u'\n' ? std::u16string(u"⏎") : std::u16string(1, c);
            actual.push_back(name + u"\t" + line + u"\t" + shown);
        }
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    if (expected.size() != actual.size()) {
        printf("line count: expected %zu, actual %zu\n", expected.size(), actual.size());
        return 1;
    }
    size_t passed = 0, shownMismatches = 0;
    std::map<std::u16string, std::pair<int, int>> counts;
    std::vector<std::u16string> order;
    for (size_t i = 0; i < expected.size(); i++) {
        auto e = split(expected[i], u'\t');
        if (!counts.count(e[0])) order.push_back(e[0]);
        auto& count = counts[e[0]];
        count.second++;
        if (expected[i] == actual[i]) { passed++; count.first++; continue; }
        if (shownMismatches++ < 20) {
            printf("MISMATCH [%s] %s\n  expected: %s\n  actual:   %s\n", utf8(e[0]).c_str(), utf8(e[1]).c_str(),
                   utf8(e[2]).c_str(), utf8(split(actual[i], u'\t')[2]).c_str());
        }
    }
    for (auto& config : order) printf("%s: %d/%d\n", utf8(config).c_str(), counts[config].first, counts[config].second);
    printf("golden: %zu/%zu\n", passed, expected.size());
    printf("models loaded in %.0f ms; replay %.1f ms for %ld keys (%.1f us/key)\n", loadMs, ms, keyCount, ms * 1000 / keyCount);
    return passed == expected.size() ? 0 : 1;
}
