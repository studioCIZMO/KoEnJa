package com.netmaru.rea.engine

import kotlin.math.min

/**
 * Finds English words typed inside a Japanese phrase, the way Japanese is written:
 * `kyouhagoogledekensaku` → きょうは + google + でけんさく.
 *
 * Only used when the whole phrase doesn't compose to clean kana (google → ごおgぇ leaves
 * a stray "g"). Splits the romaji into clean Japanese spans and known English words
 * (3+ letters), maximizing the Japanese model's score plus a fixed score per English
 * word, with a cost per switch so ordinary Japanese is never carved up.
 *
 * The input method asks on every keystroke, so the search only cuts where an English
 * word could start or end (no English word anywhere → nothing to do), and the last
 * answer is remembered.
 */
class JapaneseSegmenter(val english: EnglishModel, val japanese: JapaneseModel) {
    sealed interface Segment {
        data class Japanese(val keys: String, val kana: String) : Segment
        data class English(val word: String) : Segment
    }

    data class Scored(val segments: List<Segment>, val score: Double)

    /** Log-probability credited per letter of a known English word. */
    var englishLetterLogP = -1.6
    /** Cost of each switch into English. */
    var switchCost = 4.0

    private val cache = Cache()
    private val particleSplitter = ParticleSplitter(english, japanese = japanese)

    /** The segments, or null when the keys compose cleanly (nothing to split) or no
     *  split with both Japanese and English beats leaving stray letters. */
    fun segment(keys: String): List<Segment>? = scoredSegments(keys)?.segments

    /** Segments with their total log-probability (Japanese spans + English credit − switches). */
    fun scoredSegments(keys: String): Scored? {
        cache.get(keys)?.let { return it.result }
        val result = search(keys)
        cache.set(keys, result)
        return result
    }

    /** `githubde` → github + で, when that reading beats the whole token as kana, Korean or
     *  English ([ParticleSplitter]). */
    fun englishWithParticle(keys: String): Pair<String, String>? =
        (particleSplitter.split(keys, japaneseOn = true) as? ParticleSplitter.Split.Japanese)?.let { it.english to it.particle }

    // Search

    private class Back(val position: Int, val kana: Int, val english: Boolean)
    private class State(val score: Double, val back: Back? = null, val segment: Segment? = null)

    private fun search(keys: String): Scored? {
        if (RomajiComposer.compose(keys).strayLetters <= 0) return null
        val chars = keys
        val n = chars.length
        // Where known English words (3+ letters) are: end positions per start.
        val englishEnds = Array(n) { ArrayList<Int>() }
        val cuts = sortedSetOf(0, n)
        for (i in 0 until n) {
            if (!chars[i].isLetter()) continue
            var j = i
            while (j < n && j - i < MAX_ENGLISH_LENGTH && chars[j].isLetter()) {
                j += 1
                if (j - i >= 3 && english.isKnownWord(chars.substring(i, j))) {
                    englishEnds[i].add(j)
                    cuts.add(i)
                    cuts.add(j)
                }
            }
        }
        if (!(cuts.size > 2 || n in englishEnds[0])) return null
        val positions = cuts.toIntArray()
        val index = HashMap<Int, Int>().apply { positions.forEachIndexed { i, p -> put(p, i) } }
        // One Japanese span runs from the start or an English word's end to the end or an
        // English word's start (two spans side by side would only score worse).
        val englishEndSet = englishEnds.flatMap { it }.toSet()
        val japaneseFrom = positions.map { it == 0 || it in englishEndSet }
        val japaneseTo = positions.map { it == n || englishEnds[it].isNotEmpty() }

        // best[position][kana so far, capped at 3][used English]: the best path to there.
        val best = Array(positions.size) { Array(4) { arrayOfNulls<State>(2) } }
        best[0][0][0] = State(0.0)
        for (p in positions.indices) {
            for (kana in 0..3) {
                for (used in 0..1) {
                    val state = best[p][kana][used] ?: continue
                    val from = positions[p]
                    fun offer(q: Int, kanaAfter: Int, usedAfter: Int, score: Double, segment: Segment) {
                        val total = state.score + score
                        val old = best[q][kanaAfter][usedAfter]
                        if (old == null || total > old.score) {
                            best[q][kanaAfter][usedAfter] = State(total, Back(p, kana, used == 1), segment)
                        }
                    }
                    if (from < n) {
                        for (end in englishEnds[from]) {
                            val word = chars.substring(from, end)
                            offer(index.getValue(end), kana, 1, englishLetterLogP * word.length - switchCost, Segment.English(word))
                        }
                    }
                    if (!japaneseFrom[p]) continue
                    for (q in p + 1 until positions.size) {
                        if (!japaneseTo[q]) continue
                        val piece = chars.substring(from, positions[q])
                        // A letter no vowel can rescue (the g of "gl") fails every longer span too.
                        val reading = RomajiComposer.compose(piece, partial = true)
                        if (reading.strayLetters > 0) break
                        val composed = if (reading.pending.isEmpty()) reading else RomajiComposer.compose(piece)
                        // A one-kana span must be a particle (googleで), not a leftover (would + ん).
                        if (!composed.isClean || composed.text.isEmpty() ||
                            !(composed.text.length >= 2 || composed.text in particles)) continue
                        // Each span scores as Japanese on its own (start/end markers included).
                        offer(q, min(3, kana + composed.text.length), used,
                              japanese.logProbability(composed.text, partial = false),
                              Segment.Japanese(piece, composed.text))
                    }
                }
            }
        }

        // Needs real Japanese around the English: at least 3 kana (not mum + ばい), or exactly
        // an English word with a particle after it (github + で).
        var answer: Scored? = null
        val last = positions.size - 1
        for (kana in 0..3) {
            val state = best[last][kana][1] ?: continue
            val segments = path(Back(last, kana, true), best)
            if (!(kana >= 3 || endsInParticle(segments, keys))) continue
            if (answer == null || state.score > answer.score) answer = Scored(merged(segments), state.score)
        }
        return answer
    }

    private fun path(end: Back, best: Array<Array<Array<State?>>>): List<Segment> {
        val segments = ArrayList<Segment>()
        var at: Back? = end
        while (at != null) {
            val state = best[at.position][at.kana][if (at.english) 1 else 0] ?: break
            segments.add(state.segment ?: break)
            at = state.back
        }
        return segments.asReversed()
    }

    /** Exactly an English word plus one particle, where the whole isn't itself English. */
    private fun endsInParticle(segments: List<Segment>, keys: String): Boolean {
        if (segments.size != 2 || segments[0] !is Segment.English) return false
        val kana = (segments[1] as? Segment.Japanese)?.kana ?: return false
        return kana in trailingParticles && !english.isKnownWord(keys)
    }

    /** Adjacent Japanese spans join into one (きょうは + でけんさく stay separate only around English). */
    private fun merged(segments: List<Segment>): List<Segment> {
        val out = ArrayList<Segment>()
        for (segment in segments) {
            val previous = out.lastOrNull()
            if (segment is Segment.Japanese && previous is Segment.Japanese) {
                out[out.size - 1] = Segment.Japanese(previous.keys + segment.keys, previous.kana + segment.kana)
            } else {
                out.add(segment)
            }
        }
        return out
    }

    /** The last few answers: the preview, the decision and the commit ask about the same keys. */
    private class Cache {
        class Entry(val keys: String, val result: Scored?)
        private val entries = ArrayDeque<Entry>()

        @Synchronized fun get(keys: String): Entry? = entries.firstOrNull { it.keys == keys }

        @Synchronized fun set(keys: String, result: Scored?) {
            entries.addFirst(Entry(keys, result))
            if (entries.size > 8) entries.removeLast()
        }
    }

    companion object {
        /** Longest English word looked for inside a phrase. */
        const val MAX_ENGLISH_LENGTH = 20

        val particles = setOf("で", "に", "を", "は", "が", "の", "と", "も", "へ", "や", "か", "ね", "よ")
        /** Case particles that may follow an English noun on their own: githubで, slackから.
         *  Not ね/よ/か/や/へ/って: names end in them far more often (jayne, rebecka, claudette). */
        val trailingParticles = setOf("で", "に", "を", "は", "が", "の", "と", "も", "から", "まで", "より")

        /** The final reading of romaji keys: kana, with English words kept when a segmenter finds them. */
        fun reading(keys: String, segmenter: JapaneseSegmenter?): String =
            segmenter?.segment(keys)?.let(::text) ?: RomajiComposer.compose(keys).text

        /** Kana with the English words left in place: きょうはgoogleでけんさく. */
        fun text(segments: List<Segment>): String = segments.joinToString("") {
            when (it) {
                is Segment.Japanese -> it.kana
                is Segment.English -> it.word
            }
        }
    }
}
