"""Mixed romaji/English segmentation search, modelled on Seamtype's JapaneseSegmenter and
Meltype's FindSpansScored: kyouhagoogledekensaku -> きょうは + google + でけんさく.

    python3 search.py kyouhagoogledekensaku   # segment
    python3 search.py                          # run self-check
"""
import sys
from bisect import bisect_left

# ponytail: tiny built-in word list; pass your own set (one word per line file) for real use.
ENGLISH = {
    "google", "github", "slack", "meeting", "email", "server", "update", "python", "deploy",
    "review", "commit", "branch", "search", "design", "test", "code", "app", "api", "file",
    "goodbye", "good", "goal", "golf",
}
SWITCH_COST = 4.0    # per English span, so ordinary Japanese isn't carved up
STRAY_COST = 10.0    # per letter that stays unreadable romaji
MIN_ENGLISH = 3      # shorter "words" (de, ha, no) are almost always Japanese

# --- romaji -> kana -------------------------------------------------------------------
_ROWS = {"": "あいうえお", "k": "かきくけこ", "s": "さしすせそ", "t": "たちつてと", "n": "なにぬねの",
         "h": "はひふへほ", "m": "まみむめも", "r": "らりるれろ", "g": "がぎぐげご", "z": "ざじずぜぞ",
         "d": "だぢづでど", "b": "ばびぶべぼ", "p": "ぱぴぷぺぽ"}
KANA = {c + v: k for c, row in _ROWS.items() for v, k in zip("aiueo", row)}
KANA.update(ya="や", yu="ゆ", yo="よ", wa="わ", wo="を", shi="し", chi="ち", tsu="つ", fu="ふ", ji="じ",
            fa="ふぁ", fi="ふぃ", fe="ふぇ", fo="ふぉ")
for c, row in _ROWS.items():
    if c:
        KANA.update({c + "y" + v: row[1] + s for v, s in zip("auo", "ゃゅょ")})
for c, i in (("sh", "し"), ("ch", "ち"), ("j", "じ")):
    KANA.update({c + v: i + s for v, s in zip("auo", "ゃゅょ")})
KANA["-"] = "ー"


def compose(romaji):
    """Kana and the number of letters that couldn't be read (the g in ごおgぇ)."""
    out, stray, i, s = [], 0, 0, romaji.lower()
    while i < len(s):
        nxt = s[i + 1:i + 2]
        if nxt == s[i] and s[i] not in "aiueon-":
            out.append("っ"); i += 1; continue
        for size in (3, 2, 1):
            if s[i:i + size] in KANA:
                out.append(KANA[s[i:i + size]]); i += size; break
        else:
            if s[i] == "n":  # ん: nn, or n before a consonant/end
                out.append("ん")
                i += 2 if nxt == "n" and s[i + 2:i + 3] not in tuple("aiueoy") else 1
            else:
                out.append(s[i]); stray += 1; i += 1
    return "".join(out), stray


# --- the search -----------------------------------------------------------------------
def segment(keys, english=ENGLISH):
    """[("ja", kana) | ("en", word)] with the best total score.

    Cuts are only tried where an English word could start or end (Seamtype's pruning), then a
    Viterbi pass keeps the best path to each cut: English span = -SWITCH_COST, Japanese span =
    -STRAY_COST per unreadable letter. Longer English words win ties (Meltype's length bonus).
    """
    kana, stray = compose(keys)
    if stray == 0:  # composes cleanly: nothing to search (Seamtype's guard)
        return [("ja", kana)]
    n, low = len(keys), keys.lower()
    words = {}  # start -> [end, ...] of known English words
    for i in range(n):
        for j in range(i + MIN_ENGLISH, n + 1):
            if low[i:j] in english:
                words.setdefault(i, []).append(j)
    ends = {j for js in words.values() for j in js}
    cuts = sorted({0, n} | set(words) | ends)
    ja_from = {c for c in cuts if c == 0 or c in ends}
    ja_to = {c for c in cuts if c == n or c in words}

    best = {0: (0.0, None, None)}  # cut -> (score, previous cut, segment)
    for p in cuts:  # sorted, so every path into p is final before we leave p
        if p not in best:
            continue
        score = best[p][0]
        moves = [(j, -SWITCH_COST + 0.01 * (j - p), ("en", keys[p:j])) for j in words.get(p, [])]
        if p in ja_from:
            for q in cuts:
                if q > p and q in ja_to:
                    k, s = compose(keys[p:q])
                    moves.append((q, -STRAY_COST * s, ("ja", k)))
        for q, delta, seg in moves:
            if q not in best or score + delta > best[q][0]:
                best[q] = (score + delta, p, seg)

    out, at = [], n
    while at:
        _, at, seg = best[at][0], best[at][1], best[at][2]
        out.append(seg)
    return out[::-1]


# --- prefix search for predictions (Meltype's Predictor.StartingWith) ------------------
def complete(prefix, sorted_words, limit=5):
    """Words starting with prefix, via binary search on a sorted list."""
    i, out = bisect_left(sorted_words, prefix), []
    while i < len(sorted_words) and sorted_words[i].startswith(prefix) and len(out) < limit:
        if sorted_words[i] != prefix:
            out.append(sorted_words[i])
        i += 1
    return out


def _check():
    assert compose("kyouha") == ("きょうは", 0)
    assert compose("google")[1] > 0
    assert segment("kyouhagoogledekensaku") == [("ja", "きょうは"), ("en", "google"), ("ja", "でけんさく")]
    assert segment("ashitanomeetingwotsuika") == [("ja", "あしたの"), ("en", "meeting"), ("ja", "をついか")]
    assert segment("githubde") == [("en", "github"), ("ja", "で")]
    assert segment("watashihagakuseidesu") == [("ja", "わたしはがくせいです")]  # no carving up
    assert segment("kensaku") == [("ja", "けんさく")]
    words = sorted(ENGLISH)
    assert complete("goo", words) == ["good", "goodbye", "google"]
    assert complete("zzz", words) == []
    print("ok")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        print(" + ".join(f"{t}:{s}" for t, s in segment(sys.argv[1])))
    else:
        _check()
