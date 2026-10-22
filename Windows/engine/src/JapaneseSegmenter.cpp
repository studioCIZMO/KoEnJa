#include "JapaneseSegmenter.h"

#include <algorithm>
#include <set>
#include <unordered_map>

#include "RomajiComposer.h"
#include "Text.h"

namespace rea {

static size_t at(size_t position, int kana, int used) { return position * 8 + kana * 2 + used; }

JapaneseSegmenter::JapaneseSegmenter(const EnglishModel& english, const JapaneseModel& japanese)
    : english(english), japanese(japanese), particleSplitter(english, std::make_shared<KoreanModel>(), &japanese) {}

std::optional<std::vector<JapaneseSegmenter::Segment>> JapaneseSegmenter::segment(const std::u16string& keys) {
    auto scored = scoredSegments(keys);
    if (!scored) return std::nullopt;
    return scored->segments;
}

std::optional<JapaneseSegmenter::Scored> JapaneseSegmenter::scoredSegments(const std::u16string& keys) {
    for (auto& [cachedKeys, result] : cache) if (cachedKeys == keys) return result;
    auto result = search(keys);
    cache.emplace_front(keys, result);
    if (cache.size() > 8) cache.pop_back();
    return result;
}

std::optional<std::pair<std::u16string, std::u16string>> JapaneseSegmenter::englishWithParticle(const std::u16string& keys) const {
    auto split = particleSplitter.split(keys, true);
    if (!split) return std::nullopt;
    auto japanese = std::get_if<ParticleSplitter::Japanese>(&*split);
    if (!japanese) return std::nullopt;
    return std::pair(japanese->english, japanese->particle);
}

// Search

std::optional<JapaneseSegmenter::Scored> JapaneseSegmenter::search(const std::u16string& keys) const {
    if (RomajiComposer::compose(keys).strayLetters <= 0) return std::nullopt;
    const auto& chars = keys;
    const int n = int(chars.size());
    // Where known English words (3+ letters) are: end positions per start.
    std::vector<std::vector<int>> englishEnds(n);
    std::set<int> cuts = {0, n};
    for (int i = 0; i < n; i++) {
        if (!isLetter(chars[i])) continue;
        int j = i;
        while (j < n && j - i < MAX_ENGLISH_LENGTH && isLetter(chars[j])) {
            j += 1;
            if (j - i >= 3 && english.isKnownWord(chars.substr(i, j - i))) {
                englishEnds[i].push_back(j);
                cuts.insert(i);
                cuts.insert(j);
            }
        }
    }
    if (!(cuts.size() > 2 || std::find(englishEnds[0].begin(), englishEnds[0].end(), n) != englishEnds[0].end())) return std::nullopt;
    std::vector<int> positions(cuts.begin(), cuts.end());
    std::unordered_map<int, int> index;
    for (size_t i = 0; i < positions.size(); i++) index[positions[i]] = int(i);
    // One Japanese span runs from the start or an English word's end to the end or an
    // English word's start (two spans side by side would only score worse).
    std::set<int> englishEndSet;
    for (auto& ends : englishEnds) englishEndSet.insert(ends.begin(), ends.end());
    std::vector<bool> japaneseFrom, japaneseTo;
    for (int p : positions) {
        japaneseFrom.push_back(p == 0 || englishEndSet.count(p));
        japaneseTo.push_back(p == n || !englishEnds[p].empty());
    }

    // best[position][kana so far, capped at 3][used English]: the best path to there.
    Best best(positions.size() * 8);
    best[at(0, 0, 0)] = State{0.0};
    for (size_t p = 0; p < positions.size(); p++) {
        for (int kana = 0; kana <= 3; kana++) {
            for (int used = 0; used <= 1; used++) {
                if (!best[at(p, kana, used)]) continue;
                const double stateScore = best[at(p, kana, used)]->score;
                const int from = positions[p];
                auto offer = [&](int q, int kanaAfter, int usedAfter, double score, Segment segment) {
                    double total = stateScore + score;
                    auto& old = best[at(q, kanaAfter, usedAfter)];
                    if (!old || total > old->score) {
                        old = State{total, Back{int(p), kana, used == 1}, std::move(segment)};
                    }
                };
                if (from < n) {
                    for (int end : englishEnds[from]) {
                        auto word = chars.substr(from, end - from);
                        offer(index.at(end), kana, 1, englishLetterLogP * double(word.size()) - switchCost, English{word});
                    }
                }
                if (!japaneseFrom[p]) continue;
                for (size_t q = p + 1; q < positions.size(); q++) {
                    if (!japaneseTo[q]) continue;
                    auto piece = chars.substr(from, positions[q] - from);
                    // A letter no vowel can rescue (the g of "gl") fails every longer span too.
                    auto reading = RomajiComposer::compose(piece, true);
                    if (reading.strayLetters > 0) break;
                    auto composed = reading.pending.empty() ? reading : RomajiComposer::compose(piece);
                    // A one-kana span must be a particle (googleで), not a leftover (would + ん).
                    if (!composed.isClean() || composed.text.empty() ||
                        !(composed.text.size() >= 2 || particles().count(composed.text))) continue;
                    // Each span scores as Japanese on its own (start/end markers included).
                    offer(int(q), std::min(3, kana + int(composed.text.size())), used,
                          japanese.logProbability(composed.text, false), Japanese{piece, composed.text});
                }
            }
        }
    }

    // Needs real Japanese around the English: at least 3 kana (not mum + ばい), or exactly
    // an English word with a particle after it (github + で).
    std::optional<Scored> answer;
    const int last = int(positions.size()) - 1;
    for (int kana = 0; kana <= 3; kana++) {
        auto& state = best[at(last, kana, 1)];
        if (!state) continue;
        auto segments = path(Back{last, kana, true}, best);
        if (!(kana >= 3 || endsInParticle(segments, keys))) continue;
        if (!answer || state->score > answer->score) answer = Scored{merged(segments), state->score};
    }
    return answer;
}

std::vector<JapaneseSegmenter::Segment> JapaneseSegmenter::path(Back end, const Best& best) const {
    std::vector<Segment> segments;
    std::optional<Back> back = end;
    while (back) {
        auto& state = best[at(back->position, back->kana, back->english ? 1 : 0)];
        if (!state || !state->segment) break;
        segments.push_back(*state->segment);
        back = state->back;
    }
    std::reverse(segments.begin(), segments.end());
    return segments;
}

bool JapaneseSegmenter::endsInParticle(const std::vector<Segment>& segments, const std::u16string& keys) const {
    if (segments.size() != 2 || !std::holds_alternative<English>(segments[0])) return false;
    auto japanese = std::get_if<Japanese>(&segments[1]);
    if (!japanese) return false;
    return trailingParticles().count(japanese->kana) && !english.isKnownWord(keys);
}

std::vector<JapaneseSegmenter::Segment> JapaneseSegmenter::merged(const std::vector<Segment>& segments) {
    std::vector<Segment> out;
    for (auto& segment : segments) {
        auto japanese = std::get_if<Japanese>(&segment);
        auto previous = out.empty() ? nullptr : std::get_if<Japanese>(&out.back());
        if (japanese && previous) {
            out.back() = Japanese{previous->keys + japanese->keys, previous->kana + japanese->kana};
        } else {
            out.push_back(segment);
        }
    }
    return out;
}

const std::unordered_set<std::u16string>& JapaneseSegmenter::particles() {
    static const std::unordered_set<std::u16string> set = {u"で", u"に", u"を", u"は", u"が", u"の", u"と", u"も", u"へ", u"や", u"か", u"ね", u"よ"};
    return set;
}

const std::unordered_set<std::u16string>& JapaneseSegmenter::trailingParticles() {
    static const std::unordered_set<std::u16string> set = {u"で", u"に", u"を", u"は", u"が", u"の", u"と", u"も", u"から", u"まで", u"より"};
    return set;
}

std::u16string JapaneseSegmenter::reading(const std::u16string& keys, JapaneseSegmenter* segmenter) {
    if (segmenter) if (auto segments = segmenter->segment(keys)) return text(*segments);
    return RomajiComposer::compose(keys).text;
}

std::u16string JapaneseSegmenter::text(const std::vector<Segment>& segments) {
    std::u16string out;
    for (auto& segment : segments) {
        if (auto japanese = std::get_if<Japanese>(&segment)) out += japanese->kana;
        else out += std::get<English>(segment).word;
    }
    return out;
}

} // namespace rea
