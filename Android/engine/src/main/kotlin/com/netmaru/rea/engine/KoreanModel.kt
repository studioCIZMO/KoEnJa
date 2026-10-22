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

    /** A word in the wider vocabulary (loanwords like 월드, 서버; 혜택), for English + Korean splits
     *  and the heuristic's Korean word floor. */
    fun isVocabularyWord(hangul: String) = hangul in corpusWords || contains(lists.vocabulary, hangul) // REA: hashes

    /** A word the subtitles have fewer than [COMMON_COUNT] times (촉매, twice): for the heuristic's
     *  Korean word floor only. The tail holds dialect and typos too (어뗘, 쓔벌, 꺼이), so it
     *  stays out of the splits and of [knowsSyllable], which `EU정책` and `RJ이` rely on. */
    fun isRareVocabularyWord(hangul: String) = contains(lists.rare, hangul) // REA: hashes

    /** A noun the lists only have with a case particle (혜택 from 혜택을, 혜택으로): they
     *  count words with their particles, so a bare noun can be missing from both. */
    fun isKnownStem(hangul: String) = hangul in lists.stems // REA: lists

    /** A syllable seen at all: in the tiers, the corpus or a word of the lists, however rarely
     *  (삐, 쪄, 뾰, 쭈 are; 뗘, 쎠, 쓔 aren't). Rarer than [UNKNOWN_SYLLABLE_LOG_P] still means
     *  someone typed it. */
    fun knowsSyllable(syllable: Char) =
        syllable in '가'..'힣' && (!tiers[syllable - '가'].isNaN() || lists.syllables[syllable - '가']) // REA: lists

    fun syllableLogProbability(syllable: Char): Double {
        if (syllable !in '가'..'힣') return UNKNOWN_SYLLABLE_LOG_P
        return tiers[syllable - '가'].takeUnless { it.isNaN() } ?: UNKNOWN_SYLLABLE_LOG_P
    }

    /** [partial]: the token is still being typed, so a trailing lone consonant is expected rather than penalized. */
    fun score(keys: String, partial: Boolean = false): Score? {
        val composition = HangulComposer.compose(keys) ?: return null
        if (keys.isEmpty()) return null
        var total = logProbability(composition, partial)
        // The consonant typed last may have joined the syllable before as its final (개 + ㅎ
        // shows 갷) while it is the next syllable's first (소개하는): score that reading too.
        // (2-set only in Swift; Android has no 3-set.)
        if (partial && !composition.isChat && DubeolsikLayout.jamo(keys.last())?.let(Jamo::isConsonant) == true &&
            composition.units.last().isSyllable) {
            val before = HangulComposer.compose(keys.dropLast(1))
            if (before != null && before.units.size == composition.units.size && before.units.lastOrNull()?.isSyllable == true) {
                total = max(total, logProbability(before, false) + PENDING_CONSONANT_LOG_P - PENDING_FINAL_PENALTY)
            }
        }
        return Score(total / keys.length, composition)
    }

    private fun logProbability(composition: HangulComposer.Result, partial: Boolean): Double {
        val units = composition.units
        val allChat = composition.isChat
        var total = 0.0
        for ((i, unit) in units.withIndex()) {
            total += when {
                unit.isSyllable -> syllableLogProbability(unit.character)
                allChat -> CHAT_JAMO_LOG_P
                partial && i == units.size - 1 && Jamo.isConsonant(unit.character) -> PENDING_CONSONANT_LOG_P
                else -> STRAY_JAMO_LOG_P
            }
        }
        return total
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
        /** The same for a consonant that joined the syllable before as its final (갷 of 소개ㅎ),
         *  a little less sure: alr is 미국 on its way and also already (tuned on preview traces of
         *  the top 10,000 English and Korean words). */
        const val PENDING_FINAL_PENALTY = 1.0
        /** ㅋㅋ, ㅎㅎ, ㅠㅠ and friends. */
        const val CHAT_JAMO_LOG_P = -4.0
        const val CHAT_JAMO = "ㅋㅎㅠㅜㄷㅇㄱㅅㅂㄴ"

        /** Words seen in Korean sentences (Data/korean_words.txt, Tatoeba): 나는, 무슨, 내가… */
        private val corpusWords: WordList by lazy {
            WordList(Resources.lines("korean_words.txt") { lines -> lines.map { it.substringBefore('\t') }.toList() }.orEmpty())
        }

        /** Where korean_vocabulary.txt stops being `vocabulary`: the old 50k list's last count. */
        const val COMMON_COUNT = 10

        /** Case particles, by what the noun must end in: a final consonant (을), none (를), or either. */
        private val particles: List<Pair<String, Boolean?>> = listOf(
            "을" to true, "은" to true, "이" to true, "과" to true, "으로" to true,
            "를" to false, "는" to false, "가" to false, "와" to false,
            "에" to null, "의" to null, "도" to null, "에서" to null, "만" to null,
        )

        private class Lists(val vocabulary: LongArray, val rare: LongArray, val stems: WordList, val syllables: BooleanArray)

        // REA: korean_vocabulary.txt (245k lines) read once into what the lookups need, the words as
        // sorted 64-bit hashes (as the Swift engine keeps them for the iOS keyboard's memory).
        // ponytail: a word off the list hashing onto one on it is a 1 in 10^14 chance per lookup.
        private val lists: Lists by lazy {
            val stems = HashSet<String>()
            val syllables = BooleanArray(SYLLABLES)
            fun addStems(word: String) {
                for ((particle, final) in particles) {
                    if (!word.endsWith(particle)) continue
                    val stem = word.dropLast(particle.length)
                    if (stem.length < 2 || stem.last() !in '가'..'힣') continue
                    val hasFinal = (stem.last() - '가') % 28 != 0
                    if (final == null || final == hasFinal) stems.add(stem)
                }
            }
            fun addSyllables(word: String) { for (c in word) if (c in '가'..'힣') syllables[c - '가'] = true }
            Resources.lines("korean_words.txt") { lines -> for (line in lines) line.substringBefore('\t').let { addStems(it); addSyllables(it) } }
            var vocabulary = LongArray(1 shl 16)
            var rare = LongArray(1 shl 16)
            var v = 0
            var r = 0
            Resources.lines("korean_vocabulary.txt") { lines ->
                for (line in lines) {
                    val tab = line.indexOf('\t')
                    if (tab <= 0) continue
                    val n = line.substring(tab + 1).toIntOrNull() ?: continue
                    val word = line.substring(0, tab)
                    addStems(word)
                    if (n >= COMMON_COUNT) {
                        if (v == vocabulary.size) vocabulary = vocabulary.copyOf(v * 2)
                        vocabulary[v++] = hash(word)
                        addSyllables(word)
                    } else {
                        if (r == rare.size) rare = rare.copyOf(r * 2)
                        rare[r++] = hash(word)
                    }
                }
            }
            Lists(vocabulary.copyOf(v).apply { sort() }, rare.copyOf(r).apply { sort() }, WordList(stems), syllables)
        }

        /** REA: a binary search of the sorted hashes. */
        private fun contains(sorted: LongArray, word: String) = sorted.binarySearch(hash(word)) >= 0

        /** REA: FNV-1a of the UTF-8, as the Swift engine's. */
        private fun hash(word: String): Long {
            var h = -0x340d631b7bdddcdbL // 0xcbf29ce484222325
            for (b in word.toByteArray(Charsets.UTF_8)) h = (h xor (b.toLong() and 0xff)) * 0x100000001b3L
            return h
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

/** ㅋㅋ, ㅠㅠ, ㅜㅜ: two or more of the jamo typed on their own in chat. Korean as it is, and
 *  already complete: they don't go on to make syllables. (3-set's rule not ported.) */
val HangulComposer.Result.isChat: Boolean
    get() = units.size >= 2 && units.all { !it.isSyllable && it.character in KoreanModel.CHAT_JAMO }

/** Two or more loose consonants, or ㅜ/ㅠ: what chat abbreviates to (ㅁㅊ, ㅈㅅ, ㄹㅇ, ㅜㅜ).
 *  A reading worth switching to, though only the common ones ([isChat]) score as Korean. */
val HangulComposer.Result.isJamoAbbreviation: Boolean
    get() = units.size >= 2 && units.all { !it.isSyllable && (Jamo.isConsonant(it.character) || it.character == 'ㅜ' || it.character == 'ㅠ') }
