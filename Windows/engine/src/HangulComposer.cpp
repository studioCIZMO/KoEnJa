#include "HangulComposer.h"

#include "Jamo.h"
#include "KoreanModel.h"
#include "Sebeolsik.h"

namespace rea {

using namespace HangulComposer;

std::u16string Result::text() const {
    std::u16string out;
    for (auto& u : units) out += u.character;
    return out;
}

std::u16string Result::syllables() const {
    std::u16string out;
    for (auto& u : units) if (u.isSyllable) out += u.character;
    return out;
}

std::u16string Result::strayJamo() const {
    std::u16string out;
    for (auto& u : units) if (!u.isSyllable) out += u.character;
    return out;
}

bool Result::isBroken() const { return strayJamo().size() > (endsWithPendingConsonant() ? 1u : 0u); }

bool Result::endsWithPendingConsonant() const {
    return !units.empty() && !units.back().isSyllable && Jamo::isConsonant(units.back().character);
}

bool Result::isChat() const {
    return units.size() >= 2 && std::all_of(units.begin(), units.end(), [](auto& u) {
        return !u.isSyllable && contains(KoreanModel::CHAT_JAMO, u.character);
    }) && (!isSebeolsik() || std::all_of(units.begin(), units.end(), [&](auto& u) { return u.character == units[0].character; }));
}

bool Result::isJamoAbbreviation() const {
    return units.size() >= 2 && std::all_of(units.begin(), units.end(), [](auto& u) {
        return !u.isSyllable && (Jamo::isConsonant(u.character) || u.character == u'ㅜ' || u.character == u'ㅠ');
    });
}

std::optional<Result> HangulComposer::compose(const std::u16string& keys) {
    if (isSebeolsik()) return Sebeolsik::compose(keys);
    std::u16string jamos;
    jamos.reserve(keys.size());
    for (char16_t key : keys) {
        auto j = DubeolsikLayout::jamo(key);
        if (!j) return std::nullopt;
        jamos += *j;
    }
    return composeJamo(jamos);
}

Result HangulComposer::composeJamo(const std::u16string& jamos) {
    std::vector<Unit> units;
    units.reserve(jamos.size());
    std::optional<char16_t> cho, jung, jong;

    auto flush = [&] {
        if (cho && jung) {
            units.push_back({Jamo::syllable(*cho, *jung, jong), true});
        } else {
            if (cho) units.push_back({*cho, false});
            if (jung) units.push_back({*jung, false});
        }
        cho.reset(); jung.reset(); jong.reset();
    };

    for (char16_t j : jamos) {
        if (Jamo::isVowel(j)) {
            if (jung) {
                auto final = jong;
                auto combined = Jamo::vowelCombos().find(std::u16string{*jung, j});
                if (final) {
                    // 받침 moves to become the next syllable's initial: 갑+ㅏ → 가바, 값+ㅏ → 갑사
                    auto split = Jamo::finalSplits().find(*final);
                    if (split != Jamo::finalSplits().end()) {
                        jong = split->second.first;
                        flush();
                        cho = split->second.second;
                    } else {
                        jong.reset();
                        flush();
                        cho = final;
                    }
                    jung = j;
                } else if (combined != Jamo::vowelCombos().end()) {
                    jung = combined->second;
                } else {
                    flush();
                    jung = j;
                }
            } else {
                jung = j;
            }
        } else {
            auto final = jong;
            if (!jung || !cho) {
                // Nothing to attach to (or a bare vowel): start a new syllable.
                if (cho || jung) flush();
                cho = j;
            } else if (final) {
                auto combined = Jamo::finalCombos().find(std::u16string{*final, j});
                if (combined != Jamo::finalCombos().end()) {
                    jong = combined->second;
                } else {
                    flush();
                    cho = j;
                }
            } else if (Jamo::isFinal(j)) {
                jong = j;
            } else {
                // ㄸ ㅃ ㅉ cannot be finals.
                flush();
                cho = j;
            }
        }
    }
    flush();
    return Result{units};
}

} // namespace rea
