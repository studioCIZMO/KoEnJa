// Session checks: Japanese with a fake converter (Delete on a conversion and in live conversion,
// Space after a letter waiting for its vowel, the switch key on a live phrase shown as kana, and
// words that go in through a conversion counting as typed): the C++ twin of a few of Swift's
// JapaneseSessionTests. And Korean on 3-set, SebeolsikTests.swift's, and on 2-set Araso 0.4.5's
// words (Shift held on, an acronym before a particle, digits, - and _ kept in a word).
//
// Usage: session_test <data dir>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../src/Engines.h"
#include "../src/Resources.h"
#include "../src/Sebeolsik.h"
#include "../src/Text.h"

using namespace rea;
using Key = InputSession::Key;

static int failures = 0, checks = 0;
#define CHECK(condition) do { checks++; if (!(condition)) { failures++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

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
    void clear() { value.clear(); marked = {TextRange::NOT_FOUND, 0}; }
};

/** JapaneseSessionTests' FakeConverter. */
struct FakeConverter : JapaneseConverter {
    std::vector<std::u16string> learned;
    std::vector<ConversionCandidate> candidates(const std::u16string& kana) override {
        if (kana == u"きょうはいいてんきですね") return {{u"今日はいい天気ですね", 12}, {u"今日は", 4}, {kana, 12}};
        if (kana == u"いいてんきですね") return {{u"いい天気ですね", 8}, {kana, 8}};
        if (kana == u"へんかん") return {{u"変換", 4}, {u"返還", 4}, {u"編纂", 4}};
        if (kana == u"3じ") return {{u"三時", 2}, {u"３時", 2}, {u"3時", 2}, {u"3", 1}};
        if (kana == u"やま") return {{u"山😀", 2}, {kana, 2}};
        if (kana == u"てすと") return {{u"test", 3}};
        if (kana == u"かわ") return {{u"ｶﾞ", 2}, {kana, 2}};
        if (kana == u"そら") return {{u"ｶﾞﾅ", 2}, {kana, 2}};
        if (kana == u"かず") {
            std::vector<ConversionCandidate> numbered;
            for (int i = 1; i <= 12; i++) numbered.push_back({u"数" + utf16(std::to_string(i)), 2});
            return numbered;
        }
        return {{kana, int(kana.size())}};
    }
    void learn(const ConversionCandidate& candidate, const std::u16string&) override { learned.push_back(candidate.text); }
    void save() override {}
};

struct Test {
    Field field;
    std::shared_ptr<FakeConverter> converter = std::make_shared<FakeConverter>();
    std::unique_ptr<InputSession> session;

    explicit Test(bool live, std::optional<std::filesystem::path> dictionary = std::nullopt)
        : session(Engines::makeSession({Language::KOREAN, Language::ENGLISH, Language::JAPANESE}, converter, live, true, false, false,
                                       std::move(dictionary))) {
        session->activate();
    }
    bool key(Key k) { return session->handle(k, field); }
    /** Keys the input method passes on are typed by the app, as in a real text field. */
    void type(const std::u16string& s) {
        for (char16_t ch : s) {
            if (!key(isLetter(ch) ? Key::letter(ch) : Key::printable(ch))) field.insertText(std::u16string(1, ch), std::nullopt);
        }
    }
    bool committed() const { return field.marked.location == TextRange::NOT_FOUND; }
};

static const Key DELETE{Key::Kind::Delete}, ESCAPE{Key::Kind::Escape}, CYCLE{Key::Kind::Cycle}, ENTER{Key::Kind::Enter},
                 KATAKANA{Key::Kind::Katakana};

static std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) { fprintf(stderr, "usage: session_test <data dir> [<test resources dir>]\n"); return 2; }
    Resources::directory = argv[1];
    // Android/engine/src/test/resources, as golden_test takes it; by default found from this file.
    std::filesystem::path resources = argc == 3 ? std::filesystem::path(argv[2])
        : std::filesystem::path(__FILE__).parent_path() / "../../../Android/engine/src/test/resources";

    { // Without live conversion, Delete commits the conversion less its last character (変換 → 変).
        Test t(false);
        t.type(u"henkan ");
        CHECK(t.field.value == u"変換");
        CHECK(t.key(DELETE));
        CHECK(t.field.value == u"変" && t.committed());
        CHECK(!t.session->conversion && t.session->keys.empty());
        CHECK(t.converter->learned.empty());
        CHECK(!t.key(DELETE));
        t.field.value = dropLast(t.field.value, 1); // the app's Delete
        t.type(u"kidesu");
        t.key(ENTER);
        CHECK(t.field.value == u"きです");
        t.type(u"kyouhaiitenkidesune ");
        t.key(DELETE);
        CHECK(t.field.value == u"きです今日はいい天気です");
    }
    { // Delete takes the whole last character, both halves of a surrogate pair.
        Test t(false);
        t.type(u"yama ");
        CHECK(t.field.value == u"山😀");
        t.key(DELETE);
        CHECK(t.field.value == u"山");
    }
    { // The last character as Swift's dropLast() takes it: an extended grapheme cluster.
        auto drop = InputSession::dropLastCharacter;
        CHECK(drop(u"山葛\U000E0100") == u"山");          // an ideographic variation selector
        CHECK(drop(u"山👩🏽") == u"山");                    // a skin tone
        CHECK(drop(u"山👨\u200D👩\u200D👧") == u"山");     // a ZWJ family
        CHECK(drop(u"ｶﾞ") == u"" && drop(u"ｶﾞﾅ") == u"ｶﾞ");
        CHECK(drop(u"山か\u3099") == u"山");              // か + combining dakuten (NFD が)
        CHECK(drop(u"A\n\u0301") == u"A\n");             // a mark after a newline is a character of its own
        CHECK(drop(u"山🇯🇵🇰🇷") == u"山🇯🇵");                // flags: regional indicators in pairs
        CHECK(drop(u"a\r\n") == u"a" && drop(u"한\u1100\u1161\u11A8") == u"한" && drop(u"") == u"");
        CHECK(drop(u"A\u21A0\u200D\u21A0") == u"A\u21A0\u200D"); // ↠ isn't pictographic
        CHECK(drop(u"A\u200B\u0301") == u"A\u200B");             // ZWSP is a control
        CHECK(drop(u"Aका") == u"A");                              // a spacing mark joins
    }
    { // Unicode's GraphemeBreakTest.txt: each line less its last cluster.
        std::ifstream file(resources / "GraphemeBreakTest.txt", std::ios::binary);
        CHECK(file.good());
        int lines = 0;
        for (std::string line; std::getline(file, line);) {
            line = line.substr(0, line.find('#'));
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            // ÷ before each cluster and at the end: the text less its last cluster ends at the one before last.
            std::u16string text;
            std::vector<std::u16string> boundaries;
            std::istringstream fields(line);
            for (std::string field; fields >> field;) {
                if (field == "\xC3\xB7") boundaries.push_back(text);
                else if (field != "\xC3\x97") { // ×: no boundary
                    char32_t c = char32_t(std::stoul(field, nullptr, 16));
                    if (c >= 0x10000) text += {char16_t(0xD800 + ((c - 0x10000) >> 10)), char16_t(0xDC00 + ((c - 0x10000) & 0x3FF))};
                    else text += char16_t(c);
                }
            }
            auto expected = boundaries.size() >= 2 ? boundaries[boundaries.size() - 2] : u"";
            lines++;
            if (InputSession::dropLastCharacter(text) != expected) {
                failures++;
                fprintf(stderr, "GraphemeBreakTest: %s\n", line.c_str());
            }
            checks++;
        }
        CHECK(lines == 1093);
    }
    { // ... and the marks after it: ｶﾞ is one character, as in Swift.
        Test t(false);
        t.type(u"kawa ");
        CHECK(t.field.value == u"ｶﾞ");
        t.key(DELETE);
        CHECK(t.field.value == u"");
        t.field.clear();
        t.session->activate();
        t.type(u"sora ");
        CHECK(t.field.value == u"ｶﾞﾅ");
        t.key(DELETE);
        CHECK(t.field.value == u"ｶﾞ");
    }
    { // A digit picks from the page of candidates on show.
        Test t(false);
        t.type(u"kazu ");
        for (int i = 0; i < 9; i++) t.key(Key{Key::Kind::Down});
        CHECK(t.field.value == u"数10");
        t.type(u"2");
        CHECK(t.field.value == u"数11" && !t.session->conversion);
    }
    { // A number taken into the conversion goes with it.
        Test t(false);
        t.type(u"henkan ");
        t.key(ENTER);
        t.type(u"3ji ");
        CHECK(t.field.value == u"変換3時");
        t.key(DELETE);
        CHECK(t.field.value == u"変換3" && !t.session->conversion);
    }
    { // ... and through its katakana form (F7): the 3 is part of the form's kana, as in Swift; Esc then
      // puts the kana in (the keys no longer spell it) and the next Return is the app's.
        Test t(false);
        t.type(u"henkan ");
        t.key(ENTER);
        t.type(u"3ji ");
        CHECK(t.key(KATAKANA));
        CHECK(t.field.value == u"変換3ジ");
        CHECK(t.session->conversion && t.session->conversion->kana == u"3じ" && t.session->conversion->keys == u"ji");
        t.key(ESCAPE);
        CHECK(t.field.value == u"変換3じ" && t.committed() && !t.session->conversion);
        CHECK(!t.key(ENTER));
    }
    { // With live conversion: back to typing the phrase, its last clause as kana less one.
        Test t(true);
        t.type(u"kyouhaiitenkidesune");
        CHECK(t.field.value == u"今日はいい天気ですね");
        t.type(u" ");
        CHECK(t.field.value == u"今日はいいてんきですね"); // the next candidate covers 今日は only
        CHECK(t.key(DELETE));
        CHECK(!t.session->conversion);
        CHECK(t.field.value == u"きょうはいいてんきです");
        t.key(ESCAPE);
        t.key(ENTER);
        t.field.clear();

        t.type(u"kyouhaiitenkidesune");
        CHECK(t.key(DELETE));
        CHECK(t.field.value == u"今日はいいてんきです");
        t.key(DELETE);
        CHECK(t.field.value == u"今日はいいてんきで");
        t.key(ENTER);
        CHECK(t.field.value == u"今日はいいてんきで" && t.committed());
        CHECK(t.converter->learned.empty());
        t.field.clear();

        // Esc then shows it all as kana, as after any live conversion.
        t.type(u"kyouhaiitenkidesune");
        t.key(DELETE);
        t.key(ESCAPE);
        CHECK(t.field.value == u"きょうはいいてんきです");
    }
    { // A number taken into a live conversion goes back as typed, the phrase less a kana after it.
        Test t(true);
        t.type(u"henkan");
        t.key(ENTER);
        t.type(u"3ji ");
        CHECK(startsWith(t.field.value, u"変換") && contains(t.field.value, u'時'));
        t.key(DELETE);
        CHECK(t.field.value == u"変換3");
        CHECK(!t.session->conversion && t.session->keys.empty());
    }
    { // After a Delete the end stays kana (へんかn); Space then shows the first candidate.
        Test t(true);
        t.type(u"henkan ");
        t.key(DELETE);
        CHECK(t.field.value == u"へんか");
        t.type(u"n");
        CHECK(t.field.value == u"へんかn");
        t.type(u" ");
        CHECK(t.field.value == u"変換");
    }
    { // Space after a letter waiting for its vowel shows the first candidate; after a candidate, the next.
        Test t(true);
        t.type(u"henkan");
        CHECK(t.field.value == u"へんかn");
        t.type(u" ");
        CHECK(t.field.value == u"変換");
        t.key(ESCAPE);
        t.key(ESCAPE);
        t.field.clear();
        t.type(u"henkann");
        CHECK(t.field.value == u"変換");
        t.type(u" ");
        CHECK(t.field.value == u"返還");
    }
    { // The switch key on a live phrase showing its kana anyway switches at once; a converted one shows its kana first.
        Test t(true);
        t.type(u"kyouha");
        CHECK(t.field.value == u"きょうは");
        CHECK(t.key(CYCLE));
        CHECK(t.field.value == u"kyouha");
    }
    {
        Test t(true);
        t.type(u"kyouhaiitenkidesune");
        t.key(CYCLE);
        CHECK(t.field.value == u"きょうはいいてんきですね");
        t.key(CYCLE);
        CHECK(t.field.value.find(u"きょう") == std::u16string::npos);
    }
    { // Where Esc isn't a switch key it cancels a phrase showing its kana anyway at once.
        Test t(true);
        t.session->policy.escapeSwitches = false;
        t.type(u"kyouha");
        CHECK(t.key(ESCAPE));
        CHECK(t.field.value.empty() && t.session->keys.empty());
    }
    { // The last clause of a conversion.
        using R = std::pair<std::u16string, int>;
        CHECK(InputSession::dropLastClause(u"今日はいい天気です", u"きょうはいいてんきです") == R(u"今日はいい", 6));
        CHECK(InputSession::dropLastClause(u"今日は箱", u"きょうははこ") == R(u"今日は", 4));
        CHECK(InputSession::dropLastClause(u"googleで検索", u"googleでけんさく") == R(u"googleで", 7));
        CHECK(InputSession::dropLastClause(u"変換", u"へんかん") == R(u"", 0));
        CHECK(InputSession::dropLastClause(u"きょうは", u"きょうは") == R(u"", 0));
        CHECK(InputSession::dropLastClause(u"今日は天気", u"ちがう") == R(u"", 0));
        // A conversion the reading can't match is given up on at once, not in exponential time.
        std::u16string text, reading(36, u'あ');
        for (int i = 0; i < 16; i++) text += u"漢あ";
        CHECK(InputSession::dropLastClause(text + u"漢い漢", reading) == R(u"", 0));
    }
    for (bool katakana : {false, true}) { // A word that goes in through its conversion or kana forms was typed again.
        auto file = std::filesystem::temp_directory_path() / (std::string("koenja-session-") + (katakana ? "k" : "s") + ".txt");
        auto corrected = utf8(UserDictionary::day(UserDictionary::today() - std::chrono::days{20}));
        { std::ofstream(file, std::ios::binary) << "kyouha\tjapanese\tlearned\tweak\t" + corrected + "\n"; }
        {
            Test t(false, file);
            t.type(u"kyouha");
            t.key(katakana ? KATAKANA : Key::printable(u' '));
            CHECK(t.session->conversion.has_value());
            t.key(ENTER);
            CHECK(t.field.value == (katakana ? u"キョウハ" : u"きょうは"));
        }
        CHECK(readFile(file) == "kyouha\tjapanese\tlearned\tweak\t" + utf8(UserDictionary::day(UserDictionary::today())) + "\n");
        std::filesystem::remove(file);
    }
    { // A word switched to Japanese and put in its kana forms learns the switch: twice is for good.
        Test t(false);
        for (int i = 0; i < 2; i++) {
            t.type(u"sushi");
            for (int n = 0; n < 4 && t.field.value != u"すし"; n++) t.key(CYCLE);
            CHECK(t.field.value == u"すし");
            t.key(KATAKANA);
            t.key(ENTER);
            CHECK(t.field.value == u"スシ");
            t.field.clear();
            t.session->activate();
        }
        auto learned = t.session->engine->learned();
        CHECK(learned.count(u"sushi") && learned[u"sushi"] == Language::JAPANESE);
    }
    { // A remembered word stays until the same switch away from it comes twice; typed as remembered, the switch was a slip.
        Test t(false);
        auto& engine = *t.session->engine;
        engine.learn(u"dkssud", Language::ENGLISH, Language::KOREAN);
        engine.learn(u"dkssud", Language::ENGLISH, Language::KOREAN);
        engine.learn(u"dkssud", Language::JAPANESE, Language::ENGLISH);
        CHECK(engine.learned()[u"dkssud"] == Language::ENGLISH);
        engine.used(u"dkssud", Language::ENGLISH);
        engine.learn(u"dkssud", Language::JAPANESE, Language::ENGLISH);
        CHECK(engine.learned()[u"dkssud"] == Language::ENGLISH);
        engine.learn(u"dkssud", Language::JAPANESE, Language::ENGLISH);
        CHECK(engine.learned()[u"dkssud"] == Language::JAPANESE);
    }
    { // learning off (a password field): a correction teaches nothing
        Test t(false);
        auto& engine = *t.session->engine;
        engine.learning = false;
        engine.learn(u"dkssud", Language::ENGLISH, Language::KOREAN);
        engine.learn(u"dkssud", Language::ENGLISH, Language::KOREAN);
        CHECK(!engine.learned().count(u"dkssud"));
        engine.learning = true;
        engine.learn(u"dkssud", Language::ENGLISH, Language::KOREAN);
        engine.learn(u"dkssud", Language::ENGLISH, Language::KOREAN);
        CHECK(engine.learned()[u"dkssud"] == Language::ENGLISH);
    }
    { // learning off: a word typed again changes nothing, in memory or on disk
        auto file = std::filesystem::temp_directory_path() / "koenja-session-learning-off.txt";
        auto saved = "so\tkorean\tlearned\tweak\t" + utf8(UserDictionary::day(UserDictionary::today() - std::chrono::days{1})) + "\n";
        { std::ofstream(file, std::ios::binary) << saved; }
        {
            Test t(false, file);
            auto& engine = *t.session->engine;
            auto before = *engine.entry(u"so");
            engine.learning = false;
            engine.used(u"so", Language::KOREAN);
            CHECK(readFile(file) == saved && *engine.entry(u"so") == before);
            engine.learning = true;
            engine.used(u"so", Language::KOREAN);
            CHECK(readFile(file) != saved);
        }
        std::filesystem::remove(file);
    }
    { // Two apps each correcting a word once: sure, as one app correcting it twice.
        auto file = std::filesystem::temp_directory_path() / "koenja-session-two-corrections.txt";
        std::filesystem::remove(file);
        {
            Test a(false, file), b(false, file);
            a.session->engine->learn(u"so", Language::KOREAN, Language::ENGLISH);
            b.session->engine->learn(u"so", Language::KOREAN, Language::ENGLISH);
            Test c(false, file);
            auto entry = c.session->engine->entry(u"so");
            CHECK(entry && entry->strength == UserDictionary::Entry::Strength::Sure);
        }
        std::filesystem::remove(file);
    }
    { // Two apps on one learning.txt, one word: another app's promotion to sure, or its removal, stands
      // when this one only typed the word again (used: its day).
        auto file = std::filesystem::temp_directory_path() / "koenja-session-same-word.txt";
        auto weak = "so\tkorean\tlearned\tweak\t" + utf8(UserDictionary::day(UserDictionary::today() - std::chrono::days{1})) + "\n";
        for (bool forget : {false, true}) {
            { std::ofstream(file, std::ios::binary) << weak; }
            {
                Test a(false, file), b(false, file);
                if (forget) a.session->engine->learn(u"so", Language::ENGLISH, Language::KOREAN); // back to the engine's: gone
                else a.session->engine->learn(u"so", Language::KOREAN, Language::ENGLISH);        // the same again: sure
                b.session->engine->used(u"so", Language::KOREAN);
                Test c(false, file);
                auto entry = c.session->engine->entry(u"so");
                if (forget) CHECK(!entry);
                else CHECK(entry && entry->strength == UserDictionary::Entry::Strength::Sure);
            }
        }
        std::filesystem::remove(file);
    }
#ifndef _WIN32
    { // The lock not taken (its file unwritable): nothing written, nothing lost; the next save writes it all.
        auto file = std::filesystem::temp_directory_path() / "koenja-session-locked-out.txt";
        auto lock = file;
        lock += ".lock";
        std::filesystem::remove(file);
        { std::ofstream{lock}; }
        std::filesystem::permissions(lock, std::filesystem::perms::none);
        {
            Test a(false, file);
            a.session->engine->learn(u"go", Language::KOREAN, Language::ENGLISH);
            CHECK(!std::filesystem::exists(file));
            std::filesystem::permissions(lock, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
            a.session->engine->learn(u"so", Language::KOREAN, Language::ENGLISH);
            Test b(false, file);
            CHECK(b.session->engine->entry(u"go") && b.session->engine->entry(u"so"));
        }
        std::filesystem::remove(file);
        std::filesystem::remove(lock);
    }
#endif
    { // Two apps on one learning.txt (the TIP's engine per app): each save keeps what the other saved,
      // and a word one of them forgets goes from the file too.
        auto file = std::filesystem::temp_directory_path() / "koenja-session-two-apps.txt";
        std::filesystem::remove(file);
        {
            Test a(false, file), b(false, file);
            a.session->engine->learn(u"so", Language::KOREAN, Language::ENGLISH);
            b.session->engine->learn(u"go", Language::KOREAN, Language::ENGLISH);
            Test c(false, file);
            CHECK(c.session->engine->entry(u"so") && c.session->engine->entry(u"go"));
            a.session->engine->learn(u"so", Language::ENGLISH, Language::KOREAN); // back to the engine's: forgotten
            Test d(false, file);
            CHECK(!d.session->engine->entry(u"so") && d.session->engine->entry(u"go"));
        }
        std::filesystem::remove(file);
        auto lock = file;
        std::filesystem::remove(lock += ".lock");
    }
    { // rememberedWordMovesOnTheSecondSwitch (UserDictionaryTest.kt)
        UserDictionary dictionary;
        auto day = UserDictionary::today();
        auto entry = [&] { return *std::find_if(dictionary.entries.begin(), dictionary.entries.end(), [](auto& e) { return e.keys == u"tori"; }); };
        dictionary.learn(u"tori", Language::ENGLISH, Language::JAPANESE, day);
        dictionary.learn(u"tori", Language::ENGLISH, Language::JAPANESE, day);
        CHECK(!dictionary.learn(u"tori", Language::KOREAN, Language::ENGLISH, day));
        CHECK(entry().language == Language::ENGLISH && entry().next == Language::KOREAN);
        CHECK(dictionary.used(u"tori", Language::ENGLISH, day));
        CHECK(!entry().next);
        dictionary.learn(u"tori", Language::KOREAN, Language::ENGLISH, day);
        CHECK(dictionary.learn(u"tori", Language::KOREAN, Language::ENGLISH, day));
        CHECK(entry().language == Language::KOREAN && entry().strength == UserDictionary::Entry::Strength::Sure && !entry().next);
        // The note is kept in the file.
        CHECK(UserDictionary::decode({u"tori\tenglish\tlearned\tsure\t2026-01-01\tkorean"}, day).entries[0].next == Language::KOREAN);
    }
    { // backToTheOwnReadingForgetsTheWord (UserDictionaryTest.kt): switching a remembered word back to Araso's own forgets it.
        Test t(false);
        auto type = [&](const std::u16string& s) {
            for (char16_t ch : s) {
                if (ch == u'*') t.key(CYCLE);
                else if (!t.key(isLetter(ch) ? Key::letter(ch) : Key::printable(ch))) t.field.insertText(std::u16string(1, ch), std::nullopt);
            }
        };
        type(u"rjtm ");
        auto own = dropLast(t.field.value, 1);
        type(u"*rjtm *x rjtm "); // corrected twice: remembered, goes in as the correction
        CHECK(!endsWith(t.field.value, u" " + own + u" "));
        type(u"*");
        CHECK(endsWith(t.field.value, u" " + own + u" "));
        type(u"a rjtm ");
        CHECK(endsWith(t.field.value, u" " + own + u" "));
    }
    { // A number is a number.
        Test t(false);
        CHECK(t.session->engine->decide(u"123", DecisionContext{}).explanation == u"a number");
    }

    { // CapsLockTests.swift: letters arrive as Shift alone types them, words read as without Caps Lock,
      // English goes in in capitals; loose jamo throughout (two or more) are an acronym unless the
      // dictionary remembers them. The app types what is passed on, in capitals with Caps Lock.
        auto fresh = [](std::optional<std::filesystem::path> file = std::nullopt) {
            return Engines::makeSession({Language::KOREAN, Language::ENGLISH}, nullptr, false, true, false, false, std::move(file));
        };
        auto type = [](InputSession& session, Field& field, const std::u16string& s, bool caps) {
            for (char16_t ch : s) {
                Key key = ch == u'※' ? CYCLE : isLetter(ch) ? Key::letter(ch) : Key::printable(ch);
                if (!session.handle(key, field, caps)) field.insertText(caps ? CapsLockClient::capitals(std::u16string(1, ch)) : std::u16string(1, ch), std::nullopt);
            }
        };
        auto typed = [&](const std::u16string& s, bool caps = true) {
            Field field;
            auto session = fresh();
            type(*session, field, s, caps);
            return field.value;
        };
        // readAsWithoutCapsLock
        CHECK(typed(u"gkaus ") == u"하면 ");
        CHECK(typed(u"dkssudgktpdy ") == u"안녕하세요 ");
        CHECK(typed(u"output ") == u"OUTPUT ");
        CHECK(typed(u"apifmf ") == u"API를 ");
        CHECK(typed(u"Qks ") == u"빤 ");
        // acronymsStayEnglish, chatJamoAreAnAcronym, withoutCapsLockLooseJamoStayAsBefore
        CHECK(typed(u"rst rd ddr ") == u"RST RD DDR ");
        CHECK(typed(u"output rd ") == u"OUTPUT RD ");
        CHECK(typed(u"qusghkehlsms rp dho dmd whgdk ") == u"변화되는 게 왜 응 좋아 ");
        CHECK(typed(u"zz ") == u"ZZ ");
        CHECK(typed(u"zz ", false) == u"ㅋㅋ ");
        // switchingTheWordJustCommitted, capsLockOffKeepsTheCase
        CHECK(typed(u"gkaus ※") == u"GKAUS ");
        CHECK(typed(u"Hello ", false) == u"Hello ");
        { // capsLockOffInsideAWordStartsANewOne
            Field field;
            auto session = fresh();
            type(*session, field, u"dk", true);
            type(*session, field, u"ssud ", false);
            CHECK(field.value == u"아ㄴ녕 ");
        }
        // previewAsWithoutCapsLock, commitAsWithoutCapsLock
        for (std::u16string keys : {u"gkaus", u"dkssudgktpdy", u"rjatk", u"dkssudr", u"whgdkgg"}) {
            auto shown = [&](bool caps) {
                Field field;
                auto session = fresh();
                std::vector<std::u16string> out;
                for (char16_t ch : keys) {
                    session->handle(Key::letter(ch), field, caps);
                    out.push_back(caps ? field.value : CapsLockClient::capitals(field.value));
                }
                return out;
            };
            CHECK(shown(true) == shown(false));
            CHECK(typed(keys + u" ") == CapsLockClient::capitals(typed(keys + u" ", false)));
        }
        CHECK(typed(u"dkssudr ").find(u"DKSSUDR") == std::u16string::npos);
        CHECK(typed(u"whgdkgg ").find(u"WHGDKGG") == std::u16string::npos);
        // theDictionaryWins: a word added by hand keeps its language with Caps Lock.
        auto file = std::filesystem::temp_directory_path() / "koenja-session-caps.txt";
        { std::ofstream(file, std::ios::binary) << "hello\tkorean\tuser\ngd\tkorean\tuser\ndz\tkorean\tuser\n"; }
        for (auto [keys, text] : {std::pair{u"hello ", u"ㅗ디ㅣㅐ "}, {u"gd ", u"ㅎㅇ "}, {u"dz ", u"ㅇㅋ "}}) {
            Field field;
            auto session = fresh(file);
            type(*session, field, keys, true);
            CHECK(field.value == text);
        }
        std::filesystem::remove(file);
        { // aCandidateGoesInAsItWasTyped
            Test t(false);
            type(*t.session, t.field, u"tesuto ", false);
            CHECK(t.session->conversion.has_value());
            type(*t.session, t.field, u"a", true);
            CHECK(t.field.value.starts_with(u"test"));
        }
        { // sebeolsikJamoKeysStartANewWordToo
            currentKoreanLayout = KoreanLayout::SEBEOLSIK_FINAL;
            Field one, two;
            type(*fresh(), one, u"kf2 ", true);
            auto session = fresh();
            type(*session, two, u"kf", true);
            type(*session, two, u"2 ", false);
            currentKoreanLayout = KoreanLayout::DUBEOLSIK;
            CHECK(one.value == u"갔 ");
            CHECK(two.value != one.value && two.value.starts_with(u"가"));
        }
    }
    { // SebeolsikTests.swift: Korean on 3-set final and 390, digits and symbols arriving as the app's printable keys.
        auto compose = [](const std::u16string& keys) {
            currentKoreanLayout = KoreanLayout::SEBEOLSIK_FINAL;
            auto c = HangulComposer::compose(keys);
            currentKoreanLayout = KoreanLayout::DUBEOLSIK;
            return c ? std::optional(c->text()) : std::nullopt;
        };
        CHECK(compose(u"jfsheamfncj4") == u"안녕하세요");
        CHECK(compose(u"kf2uf") == u"갔다");      // ㅆ final on 2
        CHECK(compose(u"kkd") == u"끼");          // ㄱㄱ is ㄲ
        CHECK(compose(u"j/f") == u"와");          // ㅗ on / for compound vowels
        CHECK(compose(u"i9tmr") == u"뭐해");
        CHECK(compose(u"m8") == u"희");
        CHECK(compose(u"jtXjt") == u"없어");      // ㅄ on ⇧X
        CHECK(compose(u"jd@uf") == u"읽다");      // ㄺ on ⇧2
        CHECK(compose(u"k/rsofSjf") == u"괜찮아");
        CHECK(compose(u"kfxx") == u"갂");         // ㄱㄱ final is ㄲ
        CHECK(compose(u"kfxf") == u"각ㅏ");       // no 도깨비불
        CHECK(compose(u"jfshea!") == u"안녕ㄲ");
        CHECK(!compose(u"2026"));                 // a number stays a number
        CHECK(compose(u"00") == u"ㅋㅋ");
        CHECK(!compose(u"0"));
        CHECK(!compose(u"Hello"));                // ⇧H is 0 in Korean: no jamo
        CHECK(!compose(u"kf?"));
        CHECK(HangulComposer::compose(u"dkssud")->text() == u"안녕"); // 2-set is unchanged
        CHECK(!HangulComposer::compose(u"2026"));

        // ⎋ is a switch key.
        auto type = [](const std::u16string& text, KoreanLayout layout = KoreanLayout::SEBEOLSIK_FINAL) {
            currentKoreanLayout = layout;
            Field field;
            auto session = Engines::makeSession({Language::KOREAN, Language::ENGLISH}, nullptr);
            session->activate();
            for (char16_t ch : text) {
                Key key = ch == u'⎋' ? CYCLE : isLetter(ch) ? Key::letter(ch) : Key::printable(ch);
                if (!session->handle(key, field) && ch != u'⎋') field.insertText(std::u16string(1, ch), std::nullopt);
            }
            currentKoreanLayout = KoreanLayout::DUBEOLSIK;
            return field.value;
        };
        // digitsAndSymbolsTypeJamoInsideAWord
        CHECK(type(u"jfshea hello ") == u"안녕 hello ");
        CHECK(type(u"kf2uf ") == u"갔다 ");
        CHECK(type(u";f3 ") == u"밥 ");
        CHECK(type(u"0tpd ") == u"커피 ");
        CHECK(type(u"mr2uf. ") == u"했다. ");
        CHECK(type(u"mfqngjvf3 ") == u"핫스왑 ");
        CHECK(type(u"or3't ") == u"챕터 ");
        CHECK(type(u"0e3hduf ") == u"켭니다 ");
        CHECK(type(u"ldqkb#jgs ") == u"짓궂은 ");
        CHECK(type(u"jfspf! ") == u"안팎 ");
        // symbolsAfterKoreanAreThreeSets
        CHECK(type(u"lfw ldhr2jtB ") == u"잘 지냈어? ");
        CHECK(type(u"jvyrsifsjduf? ") == u"오랜만이다! ");
        CHECK(type(u"jvyrsifsjduf?B ") == u"오랜만이다!? ");
        CHECK(type(u"jvyrsifsjduf.B ") == u"오랜만이다.? ");
        CHECK(type(u"ldLH ") == u"지30 ");
        CHECK(type(u"mfqngjvf3[") == u"핫스왑(");
        CHECK(type(u"mfqngjvf3N ") == u"핫스왑- ");
        CHECK(type(u"jfsheamfncj4 Bob ") == u"안녕하세요 Bob ");
        CHECK(type(u"hello? ") == u"hello? ");
        CHECK(type(u"hello[B] ") == u"hello[B] ");
        CHECK(type(u"jvyrsifsjduf? B ") == u"오랜만이다! B ");
        CHECK(type(u"kgbB ") == u"kgbB ");
        CHECK(type(u"mmB ") == u"ㅎㅎ? ");
        // aSwitchKeyTakesTheSymbolBackToTheKey
        CHECK(type(u"ldhr2jtB⎋") == u"ldhr2jtB");
        CHECK(type(u"ldhr2jtB⎋⎋") == u"지냈어?");
        // zerosLaughAfterKorean
        CHECK(type(u"lfw 0000 ") == u"잘 ㅋㅋㅋㅋ ");
        CHECK(type(u"lfw0000 ") == u"잘ㅋㅋㅋㅋ ");
        CHECK(type(u"lfw 0000? ") == u"잘 ㅋㅋㅋㅋ! ");
        CHECK(type(u"0000 ") == u"0000 ");
        CHECK(type(u"0000⎋ ") == u"ㅋㅋㅋㅋ ");
        CHECK(type(u"lfw 0000 ⎋") == u"잘 0000 ");
        CHECK(type(u"lfw 0 ") == u"잘 0 ");
        CHECK(type(u"lfw 1,000 ") == u"잘 1,000 ");
        CHECK(type(u"lfw 10:00 ") == u"잘 10:00 ");
        CHECK(type(u"lfw 100 ") == u"잘 100 ");
        // sebeolsik390
        auto type390 = [&](const std::u16string& text) { return type(text, KoreanLayout::SEBEOLSIK_390); };
        CHECK(type390(u"jfs!jf ") == u"앉아 ");
        CHECK(type390(u"ufDjgs ") == u"닭은 ");
        CHECK(type390(u"kfXjd ") == u"값이 ");
        CHECK(type390(u"htxqjd ") == u"넋이 ");
        CHECK(type390(u"mfqngjvf3 ") == u"핫스왑 ");
        CHECK(type390(u"jfsheamfncj4B ") == u"안녕하세요! ");
        CHECK(type390(u"lfw ldhr2jt? ") == u"잘 지냈어? ");
        CHECK(type390(u"lcM ") == u"제1 ");
        CHECK(type390(u"lfw 0000 ") == u"잘 ㅋㅋㅋㅋ ");
        CHECK(type390(u"hello[B] ") == u"hello[B] ");
        CHECK(type390(u"ldhr2jtB⎋") == u"ldhr2jtB");
        // symbolsThatDontFitEndTheWord
        CHECK(type(u"jfshea! ") == u"안녕! ");
        CHECK(type(u"jfsheamfncj4! ") == u"안녕하세요! ");
        CHECK(type(u";f! ") == u"밖 ");
        CHECK(type(u"hello! ") == u"hello! ");
        CHECK(type(u"100 ") == u"100 ");
        CHECK(type(u"2026 ") == u"2026 ");
        CHECK(type(u"2026 0tpd ") == u"2026 커피 ");
        CHECK(type(u"it's ") == u"it's ");
        // deleteTakesBackTheLastJamo
        currentKoreanLayout = KoreanLayout::SEBEOLSIK_FINAL;
        Field field;
        auto session = Engines::makeSession({Language::KOREAN, Language::ENGLISH}, nullptr);
        for (char16_t ch : std::u16string(u"kf2uf"))
            if (!session->handle(isLetter(ch) ? Key::letter(ch) : Key::printable(ch), field)) field.insertText(std::u16string(1, ch), std::nullopt);
        session->handle(DELETE, field);
        CHECK(field.value == u"갔ㄷ");
        session->handle(DELETE, field);
        session->handle(DELETE, field);
        CHECK(field.value == u"가");
        currentKoreanLayout = KoreanLayout::DUBEOLSIK;
    }

    { // 2-set, Araso 0.4.5: JoinedWordTests, FixingTests and MixedAndPolicyTests.
        auto type = [](const std::u16string& text) {
            Field field;
            auto session = Engines::makeSession({Language::KOREAN, Language::ENGLISH, Language::JAPANESE}, nullptr);
            session->activate();
            for (char16_t ch : text) {
                Key key = ch == u'⎋' ? CYCLE : isLetter(ch) ? Key::letter(ch) : Key::printable(ch);
                if (!session->handle(key, field) && ch != u'⎋') field.insertText(std::u16string(1, ch), std::nullopt);
            }
            return field.value;
        };
        // An acronym keeps its particle; Shift held on over whole Hangul is Korean.
        CHECK(type(u"RNARkwl ") == u"RNA까지 ");
        CHECK(type(u"RNAdmf ") == u"꿈을 ");
        CHECK(type(u"WYSIWYGdl ") == u"WYSIWYG이 ");
        CHECK(type(u"RKFRMAgkek ") == u"깔끔하다 ");
        CHECK(type(u"QKFfl ") == u"빨리 ");
        CHECK(type(u"EHSms ") == u"또는 ");
        // digitsStayInTheWordBeingTyped, theSwitchKeyChangesTheWholeWord
        CHECK(type(u"k8s ") == u"k8s ");
        CHECK(type(u"k8s ⎋") == u"ㅏ8ㄴ ");
        CHECK(type(u"utf-8⎋") == u"ㅕㅅㄹ-8");
        // aKoreanWordKeepsItsDigits, and code stays English
        CHECK(type(u"dhqtus1 ") == u"옵션1 ");
        CHECK(type(u"dkssud-1 ⎋") == u"dkssud-1 ");
        CHECK(type(u"vy1 wn3ghl ") == u"표1 주3회 ");
        CHECK(type(u"cmd0 en_s2c src_dir ") == u"cmd0 en_s2c src_dir ");
        CHECK(type(u"co-op ") == u"co-op ");
        // aKoreanParticleAfterTheDigitsSplits
        CHECK(type(u"mp3fmf utf-8dmf mp3fmf-1 ") == u"mp3를 utf-8을 mp3fmf-1 ");
    }

    { // so switched to Japanese (そ), then me: the pick was learned for so when m came. Space converts,
      // Esc puts the kana back as the forced word, Enter sends it in: some isn't learned on top.
        Test t(false);
        t.type(u"so");
        for (int n = 0; n < 4 && t.field.value != u"そ"; n++) t.key(CYCLE);
        CHECK(t.field.value == u"そ");
        t.type(u"me");
        auto so = t.session->engine->entry(u"so");
        CHECK(so && so->language == Language::JAPANESE);
        t.type(u" ");
        CHECK(t.session->conversion.has_value());
        t.key(ESCAPE);
        CHECK(!t.session->conversion.has_value());
        t.key(ENTER);
        CHECK(t.field.value == u"そめ");
        CHECK(!t.session->engine->entry(u"some"));
        auto soAfter = t.session->engine->entry(u"so");
        CHECK(soAfter && soAfter->language == Language::JAPANESE);
    }


    printf("session: %d/%d\n", checks - failures, checks);
    return failures ? 1 : 0;
}
