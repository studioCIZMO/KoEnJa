#include "RomajiComposer.h"

#include <algorithm>

#include "Text.h"

namespace rea {

static const std::u16string vowels = u"aiueo";
static const std::u16string consonants = u"bcdfghjklmpqrstvwxyz";

const std::unordered_map<char16_t, std::u16string>& RomajiComposer::symbols() {
    static const std::unordered_map<char16_t, std::u16string> map = {
        {u',', u"、"}, {u'.', u"。"}, {u'?', u"？"}, {u'!', u"！"}, {u'-', u"ー"}, {u'/', u"・"}, {u'[', u"「"}, {u']', u"」"}, {u'~', u"〜"},
        {u'_', u"＿"}, {u'(', u"（"}, {u')', u"）"}, {u':', u"："}, {u';', u"；"}, {u'"', u"”"}, {u'@', u"＠"}, {u'#', u"＃"}, {u'$', u"＄"},
        {u'%', u"％"}, {u'&', u"＆"}, {u'*', u"＊"}, {u'+', u"＋"}, {u'=', u"＝"}, {u'<', u"＜"}, {u'>', u"＞"}, {u'{', u"｛"}, {u'}', u"｝"},
        {u'|', u"｜"}, {u'^', u"＾"}, {u'`', u"｀"}, {u'\\', u"＼"},
    };
    return map;
}

const std::unordered_set<char16_t>& RomajiComposer::punctuation() {
    static const auto set = [] {
        std::unordered_set<char16_t> s;
        for (auto& [_, full] : symbols()) if (full[0] != u'ー') s.insert(full[0]);
        return s;
    }();
    return set;
}

const std::unordered_map<char16_t, std::u16string>& RomajiComposer::fullWidthDigits() {
    static const auto map = [] {
        std::unordered_map<char16_t, std::u16string> m;
        for (char16_t c = u'0'; c <= u'9'; c++) m[c] = std::u16string(1, char16_t(c - u'0' + 0xFF10));
        return m;
    }();
    return map;
}

const std::unordered_map<char16_t, std::u16string>& RomajiComposer::numberPunctuation() {
    static const std::unordered_map<char16_t, std::u16string> map = {{u'-', u"－"}, {u'.', u"．"}, {u',', u"，"}, {u'/', u"／"}};
    return map;
}

const std::unordered_map<std::u16string, std::u16string>& RomajiComposer::table() {
    static const auto map = [] {
        std::unordered_map<std::u16string, std::u16string> m = {
            {u"a", u"あ"}, {u"i", u"い"}, {u"u", u"う"}, {u"e", u"え"}, {u"o", u"お"},
            {u"ka", u"か"}, {u"ki", u"き"}, {u"ku", u"く"}, {u"ke", u"け"}, {u"ko", u"こ"},
            {u"ga", u"が"}, {u"gi", u"ぎ"}, {u"gu", u"ぐ"}, {u"ge", u"げ"}, {u"go", u"ご"},
            {u"sa", u"さ"}, {u"si", u"し"}, {u"su", u"す"}, {u"se", u"せ"}, {u"so", u"そ"},
            {u"za", u"ざ"}, {u"zi", u"じ"}, {u"zu", u"ず"}, {u"ze", u"ぜ"}, {u"zo", u"ぞ"},
            {u"ta", u"た"}, {u"ti", u"ち"}, {u"tu", u"つ"}, {u"te", u"て"}, {u"to", u"と"},
            {u"da", u"だ"}, {u"di", u"ぢ"}, {u"du", u"づ"}, {u"de", u"で"}, {u"do", u"ど"},
            {u"na", u"な"}, {u"ni", u"に"}, {u"nu", u"ぬ"}, {u"ne", u"ね"}, {u"no", u"の"},
            {u"ha", u"は"}, {u"hi", u"ひ"}, {u"hu", u"ふ"}, {u"he", u"へ"}, {u"ho", u"ほ"},
            {u"ba", u"ば"}, {u"bi", u"び"}, {u"bu", u"ぶ"}, {u"be", u"べ"}, {u"bo", u"ぼ"},
            {u"pa", u"ぱ"}, {u"pi", u"ぴ"}, {u"pu", u"ぷ"}, {u"pe", u"ぺ"}, {u"po", u"ぽ"},
            {u"ma", u"ま"}, {u"mi", u"み"}, {u"mu", u"む"}, {u"me", u"め"}, {u"mo", u"も"},
            {u"ya", u"や"}, {u"yi", u"い"}, {u"yu", u"ゆ"}, {u"ye", u"いぇ"}, {u"yo", u"よ"},
            {u"ra", u"ら"}, {u"ri", u"り"}, {u"ru", u"る"}, {u"re", u"れ"}, {u"ro", u"ろ"},
            {u"wa", u"わ"}, {u"wi", u"うぃ"}, {u"wu", u"う"}, {u"we", u"うぇ"}, {u"wo", u"を"},
            {u"fa", u"ふぁ"}, {u"fi", u"ふぃ"}, {u"fu", u"ふ"}, {u"fe", u"ふぇ"}, {u"fo", u"ふぉ"},
            {u"va", u"ゔぁ"}, {u"vi", u"ゔぃ"}, {u"vu", u"ゔ"}, {u"ve", u"ゔぇ"}, {u"vo", u"ゔぉ"},
            {u"qa", u"くぁ"}, {u"qi", u"くぃ"}, {u"qu", u"く"}, {u"qe", u"くぇ"}, {u"qo", u"くぉ"},
            {u"shi", u"し"}, {u"chi", u"ち"}, {u"tsu", u"つ"}, {u"ji", u"じ"},
            {u"sha", u"しゃ"}, {u"shu", u"しゅ"}, {u"she", u"しぇ"}, {u"sho", u"しょ"},
            {u"cha", u"ちゃ"}, {u"chu", u"ちゅ"}, {u"che", u"ちぇ"}, {u"cho", u"ちょ"},
            {u"ja", u"じゃ"}, {u"ju", u"じゅ"}, {u"je", u"じぇ"}, {u"jo", u"じょ"},
            {u"tsa", u"つぁ"}, {u"tsi", u"つぃ"}, {u"tse", u"つぇ"}, {u"tso", u"つぉ"},
            {u"tha", u"てゃ"}, {u"thi", u"てぃ"}, {u"thu", u"てゅ"}, {u"the", u"てぇ"}, {u"tho", u"てょ"},
            {u"dha", u"でゃ"}, {u"dhi", u"でぃ"}, {u"dhu", u"でゅ"}, {u"dhe", u"でぇ"}, {u"dho", u"でょ"},
            {u"twu", u"とぅ"}, {u"dwu", u"どぅ"},
            {u"wha", u"うぁ"}, {u"whi", u"うぃ"}, {u"whu", u"う"}, {u"whe", u"うぇ"}, {u"who", u"うぉ"},
            {u"xa", u"ぁ"}, {u"xi", u"ぃ"}, {u"xu", u"ぅ"}, {u"xe", u"ぇ"}, {u"xo", u"ぉ"},
            {u"la", u"ぁ"}, {u"li", u"ぃ"}, {u"lu", u"ぅ"}, {u"le", u"ぇ"}, {u"lo", u"ぉ"},
            {u"xya", u"ゃ"}, {u"xyu", u"ゅ"}, {u"xyo", u"ょ"}, {u"lya", u"ゃ"}, {u"lyu", u"ゅ"}, {u"lyo", u"ょ"},
            {u"xtu", u"っ"}, {u"ltu", u"っ"}, {u"xtsu", u"っ"}, {u"ltsu", u"っ"}, {u"xwa", u"ゎ"}, {u"lwa", u"ゎ"},
            // As macOS and MS-IME accept them (not ca/ci/cu: they made English words read as romaji).
            {u"kwa", u"くぁ"}, {u"gwa", u"ぐぁ"}, {u"xn", u"ん"},
        };
        for (auto& [symbol, full] : symbols()) m[std::u16string(1, symbol)] = full;
        // Contracted sounds: きゃ, ぎゅ, みょ…
        const std::pair<const char16_t*, const char16_t*> rows[] = {
            {u"k", u"き"}, {u"g", u"ぎ"}, {u"s", u"し"}, {u"z", u"じ"}, {u"t", u"ち"}, {u"d", u"ぢ"},
            {u"n", u"に"}, {u"h", u"ひ"}, {u"b", u"び"}, {u"p", u"ぴ"}, {u"m", u"み"}, {u"r", u"り"},
            {u"c", u"ち"}, {u"j", u"じ"}, {u"f", u"ふ"},
        };
        for (auto [consonant, i] : rows) {
            std::u16string c = consonant, k = i;
            m[c + u"ya"] = k + u"ゃ";
            m[c + u"yi"] = k + u"ぃ";
            m[c + u"yu"] = k + u"ゅ";
            m[c + u"ye"] = k + u"ぇ";
            m[c + u"yo"] = k + u"ょ";
        }
        return m;
    }();
    return map;
}

/** The shortest table entry for each single kana (ties: alphabetical); ん as "n'" so a
 *  vowel typed after it can't take it over (n'a is んあ, nna would be んな). */
static const std::unordered_map<char16_t, std::u16string>& spelling() {
    static const auto map = [] {
        std::unordered_map<char16_t, std::u16string> m = {{u'ん', u"n'"}};
        for (auto& [key, value] : RomajiComposer::table()) {
            if (value.size() != 1) continue;
            char16_t ch = value[0];
            auto old = m.find(ch);
            if (old != m.end() && (old->second.size() < key.size() || (old->second.size() == key.size() && old->second < key))) continue;
            if (ch != u'ん') m[ch] = key;
        }
        return m;
    }();
    return map;
}

/** Every proper prefix of a table entry: what a word may end in while still being typed. */
static const std::unordered_set<std::u16string>& prefixes() {
    static const auto set = [] {
        std::unordered_set<std::u16string> s = {u"n"};
        for (auto& [key, _] : RomajiComposer::table()) for (size_t length = 1; length < key.size(); length++) s.insert(key.substr(0, length));
        return s;
    }();
    return set;
}

bool RomajiComposer::Result::isOnlySokuon() const {
    return !text.empty() && text[0] == u'っ' && all(text, [](char16_t c) { return c == u'っ' || isAsciiLetter(c); });
}

RomajiComposer::Result RomajiComposer::compose(const std::u16string& keys, bool partial) {
    // Matched in lowercase; letters that don't become kana stay as typed (⇧P waiting for its
    // vowel shows P, not p). Not ported: Swift's LatinLayout.romajiKeys (Colemak); keys are QWERTY's.
    const std::u16string& typed = keys;
    std::u16string chars = keys;
    for (auto& c : chars) if (c >= u'A' && c <= u'Z') c += 0x20;
    const size_t n = chars.size();
    std::u16string out;
    int stray = 0;
    int kana = 0;
    std::u16string pending;
    size_t i = 0;

    auto emit = [&](const std::u16string& s) { out += s; kana += int(s.size()); };

    while (i < n) {
        char16_t c = chars[i];
        std::optional<char16_t> next = i + 1 < n ? std::optional(chars[i + 1]) : std::nullopt;

        // Digits are full-width, and so is the punctuation inside a number (０３－１２３４, ３．５).
        if (auto it = fullWidthDigits().find(c); it != fullWidthDigits().end()) { emit(it->second); i += 1; continue; }
        if (i > 0 && isAsciiDigit(chars[i - 1])) {
            if (auto it = numberPunctuation().find(c); it != numberPunctuation().end()) { emit(it->second); i += 1; continue; }
        }
        if (c == u'n') {
            if (next) {
                if (*next == u'\'') { emit(u"ん"); i += 2; continue; }
                if (*next == u'n') {
                    // "nni" → んに, "nn" → ん
                    std::optional<char16_t> after = i + 2 < n ? std::optional(chars[i + 2]) : std::nullopt;
                    emit(u"ん");
                    i += (after && (contains(vowels, *after) || *after == u'y')) ? 1 : 2;
                    continue;
                }
                if (!contains(vowels, *next) && *next != u'y') { emit(u"ん"); i += 1; continue; }
            } else {
                if (partial) pending = std::u16string(1, typed[i]); else emit(u"ん");
                i += 1;
                continue;
            }
        } else if (next && *next == c && contains(consonants, c)) {
            emit(u"っ"); i += 1; continue;
        } else if (c == u't' && next == u'c' && i + 2 < n && chars[i + 2] == u'h') {
            emit(u"っ"); i += 1; continue; // matcha: "tch" → っch
        }

        bool matched = false;
        for (size_t length = std::min<size_t>(4, n - i); length >= 1; length--) {
            auto k = table().find(chars.substr(i, length));
            if (k == table().end()) continue;
            emit(k->second);
            i += length;
            matched = true;
            break;
        }
        if (matched) continue;

        if (partial && prefixes().count(chars.substr(i))) {
            pending = typed.substr(i);
            break;
        }
        out += typed[i];
        stray += 1;
        i += 1;
    }
    return Result{out + pending, stray, pending, kana};
}

std::u16string RomajiComposer::katakana(const std::u16string& hiragana) {
    std::u16string out = hiragana;
    for (auto& c : out) if (c >= 0x3041 && c <= 0x3096) c += 0x60;
    return out;
}

std::optional<std::u16string> RomajiComposer::romaji(const std::u16string& kana) {
    std::u16string out;
    for (char16_t ch : kana) {
        auto it = spelling().find(ch);
        if (it == spelling().end()) return std::nullopt;
        out += it->second;
    }
    return out;
}

} // namespace rea
