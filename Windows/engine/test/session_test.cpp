// Session checks: Japanese with a fake converter (Delete on a conversion and in live conversion,
// Space after a letter waiting for its vowel, the switch key on a live phrase shown as kana, and
// words that go in through a conversion counting as typed): the C++ twin of a few of Swift's
// JapaneseSessionTests. And Korean on 3-set, SebeolsikTests.swift's.
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
    if (argc != 2) { fprintf(stderr, "usage: session_test <data dir>\n"); return 2; }
    Resources::directory = argv[1];

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
    { // A number taken into the conversion goes with it.
        Test t(false);
        t.type(u"henkan ");
        t.key(ENTER);
        t.type(u"3ji ");
        CHECK(t.field.value == u"変換3時");
        t.key(DELETE);
        CHECK(t.field.value == u"変換3" && !t.session->conversion);
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

    printf("session: %d/%d\n", checks - failures, checks);
    return failures ? 1 : 0;
}
