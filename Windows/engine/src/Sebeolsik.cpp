#include "Sebeolsik.h"

#include <unordered_map>
#include <utility>
#include <vector>

#include "Jamo.h"
#include "Text.h"

namespace rea {

using Position = Sebeolsik::Position;
using Table = std::unordered_map<char16_t, Sebeolsik::Key>;
using Pairs = std::vector<std::pair<char16_t, char16_t>>;

static Table table(const std::vector<std::pair<Position, Pairs>>& extra) {
    Table map;
    auto add = [&](Position position, const Pairs& pairs) {
        for (auto [seat, jamo] : pairs) map[seat] = {jamo, position};
    };
    // The keys both layouts have.
    add(Position::Initial, {{u'k', u'ㄱ'}, {u'h', u'ㄴ'}, {u'u', u'ㄷ'}, {u'y', u'ㄹ'}, {u'i', u'ㅁ'}, {u';', u'ㅂ'}, {u'n', u'ㅅ'},
                            {u'j', u'ㅇ'}, {u'l', u'ㅈ'}, {u'o', u'ㅊ'}, {u'0', u'ㅋ'}, {u'\'', u'ㅌ'}, {u'p', u'ㅍ'}, {u'm', u'ㅎ'}});
    add(Position::Medial, {{u'f', u'ㅏ'}, {u'r', u'ㅐ'}, {u'6', u'ㅑ'}, {u't', u'ㅓ'}, {u'c', u'ㅔ'}, {u'e', u'ㅕ'},
                           {u'7', u'ㅖ'}, {u'v', u'ㅗ'}, {u'/', u'ㅗ'}, {u'4', u'ㅛ'}, {u'b', u'ㅜ'}, {u'9', u'ㅜ'}, {u'5', u'ㅠ'},
                           {u'g', u'ㅡ'}, {u'8', u'ㅢ'}, {u'd', u'ㅣ'}});
    add(Position::Final, {{u'x', u'ㄱ'}, {u's', u'ㄴ'}, {u'S', u'ㄶ'}, {u'A', u'ㄷ'}, {u'w', u'ㄹ'}, {u'z', u'ㅁ'}, {u'3', u'ㅂ'},
                          {u'X', u'ㅄ'}, {u'q', u'ㅅ'}, {u'2', u'ㅆ'}, {u'a', u'ㅇ'}, {u'Z', u'ㅊ'}, {u'W', u'ㅌ'}, {u'Q', u'ㅍ'}, {u'1', u'ㅎ'}});
    for (auto& [position, pairs] : extra) add(position, pairs);
    return map;
}

static const Table& tableFinal() {
    static const Table map = table({
        {Position::Medial, {{u'G', u'ㅒ'}}},
        {Position::Final, {{u'!', u'ㄲ'}, {u'V', u'ㄳ'}, {u'E', u'ㄵ'}, {u'@', u'ㄺ'}, {u'F', u'ㄻ'}, {u'D', u'ㄼ'}, {u'T', u'ㄽ'},
                           {u'%', u'ㄾ'}, {u'$', u'ㄿ'}, {u'R', u'ㅀ'}, {u'#', u'ㅈ'}, {u'C', u'ㅋ'}}},
    });
    return map;
}

/** 390 has fewer finals of its own: ㄳ ㄵ ㄼ ㄽ ㄾ ㄿ are two keys (`finalCombos`). */
static const Table& table390() {
    static const Table map = table({
        {Position::Medial, {{u'R', u'ㅒ'}}},
        {Position::Final, {{u'!', u'ㅈ'}, {u'E', u'ㅋ'}, {u'D', u'ㄺ'}, {u'F', u'ㄲ'}, {u'C', u'ㄻ'}, {u'V', u'ㅀ'}}},
    });
    return map;
}

/** The jamo of the layout current, by QWERTY key. */
static const Table& currentTable() { return currentKoreanLayout == KoreanLayout::SEBEOLSIK_390 ? table390() : tableFinal(); }

/** The digits and symbols 3-set final types where QWERTY types something else, by the QWERTY key
 *  as typed (? is ⇧/, which types !). Not , . + or ⇧, ⇧. (the same as on QWERTY), and no key with
 *  a jamo: ⇧1 is ㄲ, never !. */
static const std::unordered_map<char16_t, char16_t>& symbolsFinal() {
    static const std::unordered_map<char16_t, char16_t> map = {
        // Shifted punctuation.
        {u'~', u'※'}, {u'^', u'='}, {u'&', u'“'}, {u'*', u'”'}, {u'(', u'\''}, {u')', u'~'}, {u'_', u';'}, {u'{', u'%'}, {u'}', u'/'},
        {u'|', u'\\'}, {u'"', u'·'}, {u'<', u','}, {u'>', u'.'}, {u'?', u'!'}, {u'B', u'?'}, {u'N', u'-'}, {u'M', u'"'},
        // Shifted digits on the right hand.
        {u'H', u'0'}, {u'J', u'1'}, {u'K', u'2'}, {u'L', u'3'}, {u':', u'4'}, {u'Y', u'5'}, {u'U', u'6'}, {u'I', u'7'}, {u'O', u'8'}, {u'P', u'9'},
        // Unshifted symbols.
        {u'`', u'*'}, {u'-', u')'}, {u'=', u'>'}, {u'[', u'('}, {u']', u'<'}, {u'\\', u':'},
    };
    return map;
}

/** 390's: QWERTY's symbols on the unshifted keys (` is ₩), and the digits as a keypad under the
 *  right hand (⇧U ⇧I ⇧O 7 8 9, ⇧J ⇧K ⇧L 4 5 6, ⇧M ⇧, ⇧. 1 2 3, ⇧N 0). */
static const std::unordered_map<char16_t, char16_t>& symbols390() {
    static const std::unordered_map<char16_t, char16_t> map = {
        // Shifted punctuation.
        {u'B', u'!'}, {u'G', u'/'}, {u'H', u'\''}, {u'T', u';'}, {u'Y', u'<'}, {u'P', u'>'},
        // Shifted digits.
        {u'U', u'7'}, {u'I', u'8'}, {u'O', u'9'}, {u'J', u'4'}, {u'K', u'5'}, {u'L', u'6'}, {u'M', u'1'}, {u'<', u'2'}, {u'>', u'3'}, {u'N', u'0'},
        {u'`', u'₩'},
    };
    return map;
}

std::optional<Sebeolsik::Key> Sebeolsik::jamo(char16_t key) {
    auto& map = currentTable();
    auto it = map.find(key);
    if (it == map.end()) return std::nullopt;
    return it->second;
}

std::optional<char16_t> Sebeolsik::symbol(char16_t key) {
    auto& map = currentKoreanLayout == KoreanLayout::SEBEOLSIK_390 ? symbols390() : symbolsFinal();
    auto it = map.find(key);
    if (it == map.end()) return std::nullopt;
    return it->second;
}

bool Sebeolsik::isLaughter(const std::u16string& keys) {
    return keys.size() >= 2 && all(keys, [](char16_t c) { return c == u'0'; });
}

bool Sebeolsik::isKoreanShiftKey(char16_t key) { return isUpperCase(key) && jamo(key); }

bool Sebeolsik::hasEnglishOnlyCapital(const std::u16string& keys) {
    return any(keys, [](char16_t key) { return isUpperCase(key) && !jamo(key); });
}

bool Sebeolsik::startsSyllable(char16_t key) {
    auto j = jamo(key);
    return j && j->position == Position::Initial;
}

/** Initial consonants typed twice: ㄱㄱ is ㄲ. */
static const std::unordered_map<char16_t, char16_t>& doubleInitials() {
    static const std::unordered_map<char16_t, char16_t> map = {{u'ㄱ', u'ㄲ'}, {u'ㄷ', u'ㄸ'}, {u'ㅂ', u'ㅃ'}, {u'ㅅ', u'ㅆ'}, {u'ㅈ', u'ㅉ'}};
    return map;
}

/** Finals made of two keys, on top of 2-set's: ㄱㄱ and ㅅㅅ (최종 has keys for the rest, 390 for some). */
static const std::unordered_map<std::u16string, char16_t>& finalCombos() {
    static const auto map = [] {
        auto m = Jamo::finalCombos();
        m.insert({{u"ㄱㄱ", u'ㄲ'}, {u"ㅅㅅ", u'ㅆ'}});
        return m;
    }();
    return map;
}

std::optional<HangulComposer::Result> Sebeolsik::compose(const std::u16string& keys) {
    if (all(keys, isNumber) && !isLaughter(keys)) return std::nullopt;
    return composeJamo(keys);
}

std::optional<HangulComposer::Result> Sebeolsik::composeJamo(const std::u16string& keys) {
    if (keys.empty()) return std::nullopt;
    std::vector<HangulComposer::Unit> units;
    std::optional<char16_t> cho, jung, jong;

    auto flush = [&] {
        if (cho && jung) {
            units.push_back({Jamo::syllable(*cho, *jung, jong), true});
        } else {
            for (auto j : {cho, jung, jong}) if (j) units.push_back({*j, false});
        }
        cho.reset(); jung.reset(); jong.reset();
    };

    for (char16_t key : keys) {
        auto typed = jamo(key);
        if (!typed) return std::nullopt;
        char16_t j = typed->jamo;
        switch (typed->position) {
            case Position::Initial: {
                auto d = cho ? doubleInitials().find(*cho) : doubleInitials().end();
                if (cho && !jung && d != doubleInitials().end() && *cho == j) {
                    cho = d->second;
                } else {
                    flush();
                    cho = j;
                }
                break;
            }
            case Position::Medial: {
                auto combined = jung ? Jamo::vowelCombos().find(std::u16string{*jung, j}) : Jamo::vowelCombos().end();
                if (jung && !jong && combined != Jamo::vowelCombos().end()) {
                    jung = combined->second;
                } else if (!jung && !jong) {
                    jung = j;
                } else {
                    flush();
                    jung = j;
                }
                break;
            }
            case Position::Final: {
                auto combined = jong ? finalCombos().find(std::u16string{*jong, j}) : finalCombos().end();
                if (cho && jung && !jong) {
                    jong = j;
                } else if (jong && combined != finalCombos().end()) {
                    jong = combined->second;
                } else {
                    flush();
                    units.push_back({j, false});
                }
                break;
            }
        }
    }
    flush();
    return HangulComposer::Result{units};
}

bool Sebeolsik::continuesWord(const std::u16string& keys, char16_t key, bool afterDigit, const std::function<bool(char16_t)>& knows) {
    auto typed = jamo(key);
    if (!isSebeolsik() || isLetter(key) || !typed) return false;
    if (keys.empty()) return typed->position == Position::Initial && !(afterDigit && isNumber(key));
    // ㅋㅋ, also after a word (잘ㅋㅋ): lone jamo, which only the decision tells from a number.
    auto withKey = keys + key;
    size_t zeros = 0;
    while (zeros < withKey.size() && withKey[withKey.size() - 1 - zeros] == u'0') zeros++;
    if (isLaughter(withKey.substr(withKey.size() - zeros))) {
        auto front = dropLast(keys, zeros - 1);
        if (front.empty()) return true;
        if (auto c = composeJamo(front); c && !c->isBroken()) return true;
    }
    // As jamo, digits too: 0 is ㅋ, and 09 쿠 is on its way to 퀼프 (a number of them alone still
    // reads as a number when the word is decided).
    auto before = composeJamo(keys), after = composeJamo(withKey);
    if (!before || before->isBroken() || !after || after->isBroken()) return false;
    // A vowel or final has to join a syllable (가 + ㅆ is 갔, ㅋ + ㅠ is 큐): left on its own, or
    // making one nobody types, it was the symbol.
    if (typed->position == Position::Initial) return true;
    if (!after->units.empty() && after->units.back().isSyllable) return knows(after->units.back().character);
    return false;
}

} // namespace rea
