#include "Jamo.h"

#include <vector>

#include "HangulComposer.h"
#include "Sebeolsik.h"

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

bool DubeolsikLayout::isKoreanShiftKey(char16_t key) {
    if (isSebeolsik()) return Sebeolsik::isKoreanShiftKey(key);
    return shifted().count(key) > 0;
}

/** Shift still down for [key], right after [shiftKey], a Korean shift key that needed it. After
 *  a double consonant comes a vowel (djEJs, 어떤), or, when the double consonant is a final one
 *  (a vowel key before it), the next syllable's consonant (dlTDj, 있어). After ㅒ or ㅖ typed on a
 *  consonant comes a consonant (dPFmf, 예를); at the start of a word (PDF, OAuth) nothing lingers.
 *  The key before has to be lowercase: a capital there (RPA, TPS) was Shift on purpose. */
static bool lingers(char16_t shiftKey, std::optional<char16_t> twoBack, char16_t key) {
    auto before = twoBack && isLowerCase(*twoBack) ? DubeolsikLayout::jamo(*twoBack) : std::nullopt;
    auto j = DubeolsikLayout::jamo(key);
    bool consonantKey = j && Jamo::isConsonant(*j);
    auto it = shifted().find(shiftKey);
    if (it == shifted().end()) return false;
    switch (it->second) {
        case u'ㅃ': case u'ㅉ': case u'ㄸ': case u'ㄲ': case u'ㅆ':
            if (j && Jamo::isVowel(*j)) return true;
            return before && Jamo::isVowel(*before) && consonantKey;
        case u'ㅒ': case u'ㅖ':
            return before && Jamo::isConsonant(*before) && consonantKey;
        default:
            return false;
    }
}

/** The runs of consecutive capitals, as [start, end) positions. */
static std::vector<std::pair<size_t, size_t>> capitalRuns(const std::u16string& keys) {
    std::vector<std::pair<size_t, size_t>> runs;
    size_t i = 0;
    while (i < keys.size()) {
        if (!isUpperCase(keys[i])) { i++; continue; }
        size_t start = i;
        while (i < keys.size() && isUpperCase(keys[i])) i++;
        runs.push_back({start, i});
    }
    return runs;
}

/** Shift held down from a Korean shift key over the keys after it, since Shift doesn't change
 *  them: 깔끔하다 as RKFRMAgkek (held from ㄲ to ㄲ and on through 끔), 빨리 as QKFfl. The run of
 *  capitals has to start on a Korean shift key, end before the word does (a word in capitals
 *  throughout is an acronym: RKFRMA, QUICK), and the keys have to spell whole Hangul, so
 *  TFxla, PCqkd and OAuth stay English. */
static bool heldOver(size_t i, const std::u16string& keys) {
    for (auto [start, end] : capitalRuns(keys)) {
        if (i < start || i >= end) continue;
        if (!DubeolsikLayout::isKoreanShiftKey(keys[start]) || end >= keys.size() || !isLowerCase(keys[end])) return false;
        auto composed = HangulComposer::compose(keys);
        return composed && !composed->isBroken();
    }
    return false;
}

/** Why a capital with no shifted jamo is there: Shift lingering for the one key after a Korean
 *  shift key, Shift held on over it (`heldOver`), or neither, English. */
enum class UnshiftedCapital { Lingering, Held, English };

static std::vector<UnshiftedCapital> unshiftedCapitals(const std::u16string& keys) {
    bool hasLowercase = any(keys, isLowerCase);
    std::vector<UnshiftedCapital> kinds;
    for (size_t i = 0; i < keys.size(); i++) {
        if (!isUpperCase(keys[i]) || shifted().count(keys[i])) continue;
        if (hasLowercase && i >= 1 && lingers(keys[i - 1], i >= 2 ? std::optional(keys[i - 2]) : std::nullopt, keys[i])) {
            kinds.push_back(UnshiftedCapital::Lingering);
        } else {
            kinds.push_back(heldOver(i, keys) ? UnshiftedCapital::Held : UnshiftedCapital::English);
        }
    }
    return kinds;
}

static bool hasKind(const std::vector<UnshiftedCapital>& kinds, UnshiftedCapital kind) {
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

bool DubeolsikLayout::hasEnglishOnlyCapital(const std::u16string& keys) {
    if (isSebeolsik()) return Sebeolsik::hasEnglishOnlyCapital(keys);
    return hasKind(unshiftedCapitals(keys), UnshiftedCapital::English);
}

bool DubeolsikLayout::hasHeldOverCapital(const std::u16string& keys) {
    if (isSebeolsik()) return false;
    auto kinds = unshiftedCapitals(keys);
    return hasKind(kinds, UnshiftedCapital::Held) && !hasKind(kinds, UnshiftedCapital::English);
}

bool DubeolsikLayout::capitalRunsStartOnShiftKeys(const std::u16string& keys) {
    for (auto [start, end] : capitalRuns(keys)) if (!isKoreanShiftKey(keys[start])) return false;
    return true;
}

std::optional<char16_t> DubeolsikLayout::jamo(char16_t key) {
    if (isSebeolsik()) {
        auto j = Sebeolsik::jamo(key);
        return j ? std::optional(j->jamo) : std::nullopt;
    }
    if (auto it = shifted().find(key); it != shifted().end()) return it->second;
    if (!isAscii(key)) return std::nullopt;
    auto it = lower().find(isUpperCase(key) ? char16_t(key + 0x20) : key);
    if (it == lower().end()) return std::nullopt;
    return it->second;
}

} // namespace rea
