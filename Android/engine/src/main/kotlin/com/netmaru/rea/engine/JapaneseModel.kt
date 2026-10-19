package com.netmaru.rea.engine

import kotlin.math.ln

/**
 * Kana trigram model for judging how Japanese a romaji string reads. Counts come
 * from Japanese sentences (Tatoeba, CC-BY 2.0 FR) turned into the romaji people
 * type and back into kana with [RomajiComposer] (tools/build_kana_model.py).
 *
 * Clean romaji isn't enough: "hello" composes to へっぉ, which this model scores
 * as very un-Japanese (っぉ essentially never occurs).
 */
class JapaneseModel private constructor(
    private val trigrams: CountTable,
    private val bigrams: CountTable,
    private val unigrams: CountTable,
    private val bigramContexts: CountTable,
    private val trigramContexts: CountTable,
    private val total: Float,
    private val vocabulary: Float,
) {
    data class Score(
        /** Average log-probability per keystroke, comparable with the English and Korean models. */
        val averageLogProbability: Double,
        val composition: RomajiComposer.Result,
    )

    /** Interpolated trigram probability of [c] after [a] [b] (code points). */
    private fun probability(a: Int, b: Int, c: Int): Double {
        val p1 = (unigrams[key(c)].orZero() + 1) / (total + vocabulary)
        val p2 = bigramContexts[key(b)].let { if (it.isNaN()) 0f else bigrams[key(b, c)].orZero() / it }
        val p3 = trigramContexts[key(a, b)].let { if (it.isNaN()) 0f else trigrams[key(a, b, c)].orZero() / it }
        return (0.6f * p3 + 0.3f * p2 + 0.1f * p1).toDouble()
    }

    /** Sum of log-probabilities of the kana (and the end of word unless [partial]). */
    fun logProbability(kana: String, partial: Boolean): Double {
        val seq = IntArray(2) { START } + kana.codePoints().toArray() + (if (partial) IntArray(0) else intArrayOf(END))
        var sum = 0.0
        for (i in 2 until seq.size) sum += ln(probability(seq[i - 2], seq[i - 1], seq[i]))
        return sum
    }

    fun score(keys: String, partial: Boolean = false): Score? {
        if (keys.isEmpty() || !keys.all { it.isAscii && (it.isLetter() || it in inPhraseSymbols) }) return null
        val composition = RomajiComposer.compose(keys, partial)
        val kana = composition.text.dropLast(composition.pending.length)
            .filter { !it.isAscii && it !in RomajiComposer.punctuation }
        var total = logProbability(kana, partial)
        total += composition.strayLetters * STRAY_LETTER_LOG_P
        if (composition.pending.isNotEmpty()) total += PENDING_LOG_P
        return Score(total / keys.length, composition)
    }

    /** Counts by key, sorted for binary search: an IME can't spare boxed hash maps of 100k+ entries. */
    private class CountTable(private val keys: LongArray, private val values: FloatArray) {
        /** NaN when the key isn't there. */
        operator fun get(key: Long): Float = keys.binarySearch(key).let { if (it >= 0) values[it] else Float.NaN }

        companion object {
            fun of(map: Map<Long, Float>): CountTable {
                val keys = map.keys.toLongArray().apply { sort() }
                return CountTable(keys, FloatArray(keys.size) { map.getValue(keys[it]) })
            }
        }
    }

    companion object {
        /** Non-letters that belong inside a Japanese phrase: ん (n'), ー, and the full-width symbols (、。？！「」・…). */
        val inPhraseSymbols: Set<Char> = RomajiComposer.symbols.keys + '\''

        const val STRAY_LETTER_LOG_P = -13.0
        const val PENDING_LOG_P = -3.5
        private const val START = '^'.code
        private const val END = '$'.code

        val shared: JapaneseModel? by lazy { load("kana_trigrams.txt") }
        /** The same kind of model over written Japanese (kanji + kana), for reranking conversions. */
        val text: JapaneseModel? by lazy { load("text_trigrams.txt") }

        private fun Float.orZero() = if (isNaN()) 0f else this
        private fun key(c: Int) = c.toLong()
        private fun key(b: Int, c: Int) = (b.toLong() shl 21) or c.toLong()
        private fun key(a: Int, b: Int, c: Int) = (a.toLong() shl 42) or (b.toLong() shl 21) or c.toLong()

        /** From a file of trigram<TAB>count lines; null when it's missing or empty. */
        fun load(name: String): JapaneseModel? = Resources.lines(name) { lines ->
            val trigrams = HashMap<Long, Float>()
            val trigramContexts = HashMap<Long, Float>()
            val bigrams = HashMap<Long, Float>()
            val bigramContexts = HashMap<Long, Float>()
            val unigrams = HashMap<Long, Float>()
            val symbols = HashSet<Int>()
            var total = 0f
            fun HashMap<Long, Float>.add(key: Long, count: Float) { this[key] = (this[key] ?: 0f) + count }
            for (line in lines) {
                val parts = line.split('\t')
                val count = parts.getOrNull(1)?.toFloatOrNull()
                if (parts.size != 2 || count == null) continue
                val chars = parts[0].codePoints().toArray()
                if (chars.size != 3) continue
                val (a, b, c) = chars
                trigrams.add(key(a, b, c), count)
                trigramContexts.add(key(a, b), count)
                bigrams.add(key(b, c), count)
                bigramContexts.add(key(b), count)
                unigrams.add(key(c), count)
                total += count
                symbols.addAll(chars.asList())
            }
            if (total > 0) JapaneseModel(CountTable.of(trigrams), CountTable.of(bigrams), CountTable.of(unigrams),
                                         CountTable.of(bigramContexts), CountTable.of(trigramContexts), total, symbols.size.toFloat())
            else null
        }
    }
}
