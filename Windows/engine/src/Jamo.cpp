#include "Jamo.h"

namespace rea {

static const std::u16string CHOSEONG = u"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";
static const std::u16string JUNGSEONG = u"ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ";
/** Index + 1 is the final's index (0 is "no final consonant"). */
static const std::u16string JONGSEONG = u"ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ";

const std::unordered_map<std::u16string, char16_t>& Jamo::vowelCombos() {
    static const std::unordered_map<std::u16string, char16_t> map = {
        {u"ㅗㅏ", u'ㅘ'}, {u"ㅗㅐ", u'ㅙ'}, {u"ㅗㅣ", u'ㅚ'},
        {u"ㅜㅓ", u'ㅝ'}, {u"ㅜㅔ", u'ㅞ'}, {u"ㅜㅣ", u'ㅟ'},
        {u"ㅡㅣ", u'ㅢ'},
    };
    return map;
}

const std::unordered_map<std::u16string, char16_t>& Jamo::finalCombos() {
    static const std::unordered_map<std::u16string, char16_t> map = {
        {u"ㄱㅅ", u'ㄳ'}, {u"ㄴㅈ", u'ㄵ'}, {u"ㄴㅎ", u'ㄶ'},
        {u"ㄹㄱ", u'ㄺ'}, {u"ㄹㅁ", u'ㄻ'}, {u"ㄹㅂ", u'ㄼ'}, {u"ㄹㅅ", u'ㄽ'}, {u"ㄹㅌ", u'ㄾ'}, {u"ㄹㅍ", u'ㄿ'}, {u"ㄹㅎ", u'ㅀ'},
        {u"ㅂㅅ", u'ㅄ'},
    };
    return map;
}

const std::unordered_map<char16_t, std::pair<char16_t, char16_t>>& Jamo::finalSplits() {
    static const auto map = [] {
        std::unordered_map<char16_t, std::pair<char16_t, char16_t>> m;
        for (auto& [pair, combined] : finalCombos()) m[combined] = {pair[0], pair[1]};
        return m;
    }();
    return map;
}

bool Jamo::isConsonant(char16_t j) { return contains(CHOSEONG, j) || contains(JONGSEONG, j); }
bool Jamo::isVowel(char16_t j) { return contains(JUNGSEONG, j); }
bool Jamo::isFinal(char16_t j) { return contains(JONGSEONG, j); }

char16_t Jamo::syllable(char16_t cho, char16_t jung, std::optional<char16_t> jong) {
    int c = int(CHOSEONG.find(cho));
    int v = int(JUNGSEONG.find(jung));
    int f = jong ? int(JONGSEONG.find(*jong)) + 1 : 0;
    return char16_t(0xAC00 + (c * 21 + v) * 28 + f);
}

static const std::unordered_map<char16_t, char16_t>& lower() {
    static const std::unordered_map<char16_t, char16_t> map = {
        {u'q', u'ㅂ'}, {u'w', u'ㅈ'}, {u'e', u'ㄷ'}, {u'r', u'ㄱ'}, {u't', u'ㅅ'}, {u'y', u'ㅛ'}, {u'u', u'ㅕ'}, {u'i', u'ㅑ'}, {u'o', u'ㅐ'}, {u'p', u'ㅔ'},
        {u'a', u'ㅁ'}, {u's', u'ㄴ'}, {u'd', u'ㅇ'}, {u'f', u'ㄹ'}, {u'g', u'ㅎ'}, {u'h', u'ㅗ'}, {u'j', u'ㅓ'}, {u'k', u'ㅏ'}, {u'l', u'ㅣ'},
        {u'z', u'ㅋ'}, {u'x', u'ㅌ'}, {u'c', u'ㅊ'}, {u'v', u'ㅍ'}, {u'b', u'ㅠ'}, {u'n', u'ㅜ'}, {u'm', u'ㅡ'},
    };
    return map;
}

static const std::unordered_map<char16_t, char16_t>& shifted() {
    static const std::unordered_map<char16_t, char16_t> map = {
        {u'Q', u'ㅃ'}, {u'W', u'ㅉ'}, {u'E', u'ㄸ'}, {u'R', u'ㄲ'}, {u'T', u'ㅆ'}, {u'O', u'ㅒ'}, {u'P', u'ㅖ'},
    };
    return map;
}

bool DubeolsikLayout::isKoreanShiftKey(char16_t key) { return shifted().count(key) > 0; }

std::optional<char16_t> DubeolsikLayout::jamo(char16_t key) {
    if (auto it = shifted().find(key); it != shifted().end()) return it->second;
    if (!isAscii(key)) return std::nullopt;
    auto it = lower().find(isUpperCase(key) ? char16_t(key + 0x20) : key);
    if (it == lower().end()) return std::nullopt;
    return it->second;
}

} // namespace rea
