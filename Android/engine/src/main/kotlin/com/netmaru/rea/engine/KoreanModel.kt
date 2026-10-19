package com.netmaru.rea.engine

import kotlin.math.ln
import kotlin.math.max

/**
 * Syllable-unigram model for Korean: enough to tell "안녕하세요" from the accidental
 * Hangul that English produces on a 2-set layout (e.g. "the" → "솓").
 *
 * Not ported: `Source` (tiers or corpus alone); this is Swift's default, `.max`.
 */
class KoreanModel {
    data class Score(
        /** Average log-probability per keystroke. */
        val averageLogProbability: Double,
        val composition: HangulComposer.Result,
    )

    /** Log-probability per syllable (index: syllable − 가), NaN where neither source has it. */
    private val tiers = DoubleArray(SYLLABLES) { Double.NaN }

    init {
        // The hand-curated frequency tiers (KoreanSyllables.kt).
        for ((syllables, logP) in listOf(KoreanSyllables.TIER3 to TIER3_LOG_P, KoreanSyllables.TIER2 to TIER2_LOG_P,
                                         KoreanSyllables.TIER1 to TIER1_LOG_P)) {
            for (s in syllables) if (s in '가'..'힣') tiers[s - '가'] = logP // later (more frequent) tiers overwrite earlier ones
        }
        // Counts from Korean sentences (Data/korean_syllables.txt, Tatoeba, CC-BY 2.0 FR): whichever rates a syllable higher.
        corpusCounts()?.let { counts ->
            val total = counts.values.sum().toDouble()
            val smoothing = 0.5
            val possible = 11172.0
            for ((s, n) in counts) {
                if (s !in '가'..'힣') continue
                val logP = ln((n + smoothing) / (total + smoothing * possible))
                val old = tiers[s - '가']
                tiers[s - '가'] = if (old.isNaN()) logP else max(old, logP)
            }
        }
    }

    /** A word that occurs in real Korean text (the composed Hangul, not the keys). */
    fun isKnownWord(hangul: String) = hangul in corpusWords

    /** A word in the wider vocabulary (loanwords like 월드, 서버), for English + Korean splits. */
    fun isVocabularyWord(hangul: String) = hangul in corpusWords || hangul in vocabulary

    fun syllableLogProbability(syllable: Char): Double {
        if (syllable !in '가'..'힣') return UNKNOWN_SYLLABLE_LOG_P
        return tiers[syllable - '가'].takeUnless { it.isNaN() } ?: UNKNOWN_SYLLABLE_LOG_P
    }

    /** [partial]: the token is still being typed, so a trailing lone consonant is expected rather than penalized. */
    fun score(keys: String, partial: Boolean = false): Score? {
        val composition = HangulComposer.compose(keys) ?: return null
        if (keys.isEmpty()) return null
        val units = composition.units
        val allChat = units.size >= 2 && units.all { !it.isSyllable && it.character in CHAT_JAMO }
        var total = 0.0
        for ((i, unit) in units.withIndex()) {
            total += when {
                unit.isSyllable -> syllableLogProbability(unit.character)
                allChat -> CHAT_JAMO_LOG_P
                partial && i == units.size - 1 && Jamo.isConsonant(unit.character) -> PENDING_CONSONANT_LOG_P
                else -> STRAY_JAMO_LOG_P
            }
        }
        return Score(total / keys.length, composition)
    }

    companion object {
        private const val SYLLABLES = 11172
        const val TIER1_LOG_P = -5.0
        const val TIER2_LOG_P = -6.5
        const val TIER3_LOG_P = -8.0
        const val UNKNOWN_SYLLABLE_LOG_P = -11.0
        const val STRAY_JAMO_LOG_P = -13.0
        /** Typing the first consonant of the next syllable is normal mid-word. */
        const val PENDING_CONSONANT_LOG_P = -3.5
        /** ㅋㅋ, ㅎㅎ, ㅠㅠ and friends. */
        const val CHAT_JAMO_LOG_P = -4.0
        const val CHAT_JAMO = "ㅋㅎㅠㅜㄷㅇㄱㅅㅂㄴ"

        /** Words seen in Korean sentences (Data/korean_words.txt, Tatoeba): 나는, 무슨, 내가… */
        private val corpusWords: WordList by lazy {
            WordList(Resources.lines("korean_words.txt") { lines -> lines.map { it.substringBefore('\t') }.toList() }.orEmpty())
        }

        private val vocabulary: WordList by lazy {
            WordList(Resources.lines("korean_vocabulary.txt") { it.toList() }.orEmpty())
        }

        private fun corpusCounts(): Map<Char, Int>? = Resources.lines("korean_syllables.txt") { lines ->
            val counts = HashMap<Char, Int>()
            for (line in lines) {
                val parts = line.split('\t')
                val n = parts.getOrNull(1)?.toIntOrNull()
                if (parts.size == 2 && parts[0].isNotEmpty() && n != null) counts[parts[0][0]] = n
            }
            counts
        }
    }
}
