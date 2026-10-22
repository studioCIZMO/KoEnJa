package com.netmaru.rea.engine

import java.util.Locale
import kotlin.math.max

/**
 * An English word with a particle typed straight after it (`githubdp` → `github에`,
 * `githubde` → `githubで`) is one more reading of the keys, scored like the whole-token
 * readings it competes with: average log-probability per keystroke.
 *
 * - The front scores as English: a known word gets the dictionary floors [HeuristicEngine]
 *   uses, capitals typed on purpose (API, GitHub) a higher one, anything else its spelling.
 * - The particle scores by how common it is, and switching language costs a little.
 * - The whole token scores as Korean (a Korean word, or one with a particle, gets a floor),
 *   as unknown English, and with Japanese on as kana.
 *
 * The split wins only by `margin`. `form에서` splits because 래그 is no Korean word and form
 * is everyday English; `핸들을` doesn't, because 핸들 is a word.
 */
class ParticleSplitter(
    val english: EnglishModel,
    val korean: KoreanModel = KoreanModel(),
    val japanese: JapaneseModel? = null,
    val weights: Weights = Weights(),
) {
    /** Log-probabilities for a whole word or particle, not per key, so a longer front or
     *  particle isn't favored for its length. */
    data class Weights(
        /** A known English word: `knownBase` minus `perLevel` per SCOWL level (10 = everyday). */
        var knownBase: Double = -4.0,
        var perLevel: Double = 0.15,
        /** An acronym (API, UI, PR) or a name with a capital inside (GitHub, VSCode, macOS). */
        var acronym: Double = -6.0,
        var name: Double = -7.0,
        /** Taken off once from the spelling score of a front (or token) that isn't a known word. */
        var unknownWord: Double = 3.0,
        /** Taken off once more when the front isn't a known word: an unknown word with a
         *  particle after it is rarer than an unknown word (names: adriano, porto, chandi). */
        var unknownFront: Double = 3.0,
        /** A common particle (에, 를, で…) and the other endings (처럼, 했어, まで…). */
        var coreParticle: Double = -2.5,
        var otherParticle: Double = -4.5,
        /** Once per split, for switching language inside a token. */
        var switchCost: Double = 1.0,
        /** Instead of `switchCost` for an ordinary English word and a Korean word (hello월드): rarer
         *  than a particle or an acronym or name with a word after it (github에, PC카톡). */
        var wordSwitchCost: Double = 9.0,
        /** Added to a particle of a pair (은/는, 을/를) after an acronym when it is the form the
         *  acronym's last letter takes (TM은, TV는): after another word either form may follow,
         *  after an acronym only that one does, so twice as likely (ln 2). */
        var agreement: Double = 0.7,
        /** Per key, the least a Korean word (or a word with a particle) scores. */
        var koreanWordFloor: Double = -1.4,
        /** Added per key to the kana reading's score, as [HeuristicEngine.japaneseOffset]. */
        var japaneseOffset: Double = 1.4,
        /** How far (per key) the split has to lead the best whole-token reading. */
        var margin: Double = 0.3,
    )

    sealed interface Split {
        val text: String
        data class Korean(val mixed: MixedWord) : Split { override val text get() = mixed.text }
        data class Japanese(val english: String, val particle: String) : Split { override val text get() = english + particle }
    }

    data class Scored(val split: Split, val lead: Double, val detail: String)

    /** The best split of [keys], if it beats reading the whole token as one language.
     *  Japanese particles only with [japaneseOn]. */
    fun split(keys: String, japaneseOn: Boolean): Split? =
        scored(keys, japaneseOn)?.takeIf { it.lead >= weights.margin }?.split

    /** The best split and how far it leads the best whole-token reading (for tuning). */
    fun scored(keys: String, japaneseOn: Boolean): Scored? {
        val n = keys.length
        if (n < 3 || english.isKnownWord(keys)) return null
        var best: Split? = null
        var bestScore = 0.0
        fun offer(split: Split, front: String, particle: Double, korean: String? = null) {
            val w = frontScore(front) ?: return
            var particle = particle
            if (korean != null && w == weights.acronym &&
                MixedWord.particleAgrees(korean, MixedWord.finalSoundOfAcronym(front)) == true) {
                particle += weights.agreement
            }
            val score = (w + particle - weights.switchCost) / n
            if (best == null || score > bestScore) { best = split; bestScore = score }
        }
        for (start in 2 until n) {
            val tail = keys.substring(start)
            if (!DubeolsikLayout.startsSyllable(tail[0])) continue
            val composed = HangulComposer.compose(tail) ?: continue
            if (composed.strayJamo.isNotEmpty() || composed.text !in MixedWord.suffixes) continue
            val front = keys.substring(0, start)
            offer(Split.Korean(MixedWord(front, tail, composed.text)), front,
                  if (composed.text in coreKorean) weights.coreParticle else weights.otherParticle, composed.text)
        }
        if (japaneseOn && japanese != null) {
            for (start in max(2, n - 4) until n) {
                val tail = RomajiComposer.compose(keys.substring(start))
                if (!tail.isClean || tail.text !in JapaneseSegmenter.trailingParticles) continue
                val front = keys.substring(0, start)
                offer(Split.Japanese(front, tail.text), front,
                      if (tail.text in coreJapanese) weights.coreParticle else weights.otherParticle)
            }
        }
        val (w, word) = englishThenKoreanWord(keys)
        if (best == null && word == null) return null
        val k = koreanScore(keys)
        val e = english.averageLogProbability(keys) - weights.unknownWord / n
        val j = if (japaneseOn) japaneseScore(keys) else Double.NEGATIVE_INFINITY
        val whole = maxOf(k, e, j)
        // English + Korean word is a split too, and the particle split has to beat it, but not by
        // `margin`, which is for splitting at all (funeral로 over funer + 미로).
        val detail = String.format(Locale.ROOT, "split=%.2f ko=%.2f en=%.2f ja=%.2f en+ko=%.2f",
                                   if (best != null) bestScore else Double.NEGATIVE_INFINITY, k, e, j, w)
        best?.let { if (bestScore >= w) return Scored(it, minOf(bestScore - whole, bestScore - w + weights.margin), detail) }
        word ?: return null
        return Scored(Split.Korean(word.first), word.second - whole, detail)
    }

    /** The keys as an English front and a Korean word of two syllables or more, per key
     *  (PC카톡에서, not PCzkxhr에서): the front as [frontScore], the word as the whole token is
     *  scored as Korean. A word that is only a particle is the particle split, and one with a
     *  syllable nobody writes is no word (mac + 뇃에서 doesn't stop macshot에서).
     *
     *  First: the best cut, the reading a particle split has to beat. Second: the best cut
     *  that may stand as a split itself ([mayStandAsWord]): an acronym or name and a word (PC카톡,
     *  TV광고) switch language at `switchCost`, as a particle does; an ordinary word and a Korean
     *  word (hello월드) are rarer and pay `wordSwitchCost`, so a name that spells like English
     *  stays whole (cartman isn't car스무). */
    fun englishThenKoreanWord(keys: String): Pair<Double, Pair<MixedWord, Double>?> {
        val englishOnlyCapital = DubeolsikLayout.hasEnglishOnlyCapital(keys)
        // Clean romaji (hasansowo, はさんそを) is Japanese, and keys that read as Korean throughout
        // are Korean, unless a capital no lingering Shift explains says otherwise (ThkT is 쐈).
        val mayStand = !RomajiComposer.compose(keys).isClean && (englishOnlyCapital || !readsAsKorean(keys))
        var best = Double.NEGATIVE_INFINITY
        var split: Pair<MixedWord, Double>? = null
        for (start in 2 until keys.length - 1) {
            val front = keys.substring(0, start)
            val back = keys.substring(start)
            if (!DubeolsikLayout.startsSyllable(back[0])) continue
            val composed = HangulComposer.compose(back) ?: continue
            if (composed.strayJamo.isNotEmpty() || composed.syllables.size < 2 || composed.text in MixedWord.suffixes) continue
            val knownKorean = isKoreanWordOrWordWithParticle(composed.text)
            if (!(knownKorean || readsAsKorean(back))) continue
            val (w, capitals) = frontReading(front) ?: continue
            val k = koreanScore(back) * back.length
            best = max(best, (w + k - weights.switchCost) / keys.length)
            if (!mayStand || (capitals && !mayStandAsWord(front, englishOnlyCapital, knownKorean))) continue
            val cost = if (capitals) weights.switchCost else weights.wordSwitchCost
            val score = (w + k - cost) / keys.length
            if (split == null || score > split.second) split = MixedWord(front, back, composed.text) to score
        }
        return best to split
    }

    /** Whether an acronym or name may be the front of a split, not only compete with one: when
     *  a capital no lingering Shift explains is in the keys, or a known Korean word follows and
     *  its own keys spell a syllable no list or corpus has (EU as ㄸ+ㅕ: 뗘). Korean typed fast
     *  keeps its rare syllables (삐졌어 as QLwuTdj: 삐 is rare, but seen) and 쓰정책 (TMwjdcor)
     *  stays Korean. Only the front is judged: the word after may well hold a rare syllable
     *  (또공룡이 as EHrhdfyddl). */
    private fun mayStandAsWord(front: String, englishOnlyCapital: Boolean, knownKorean: Boolean) =
        englishOnlyCapital || !isAcronym(front) || (knownKorean && !spellsSeenKorean(front))

    /** Whole Hangul of syllables seen in the corpus or a word list, however rarely (삐, not 뗘). */
    private fun spellsSeenKorean(keys: String): Boolean {
        val composed = HangulComposer.compose(keys) ?: return false
        return composed.strayJamo.isEmpty() && composed.syllables.all(korean::knowsSyllable)
    }

    /** Whole Hangul made of syllables people use. */
    fun readsAsKorean(keys: String): Boolean {
        val composed = HangulComposer.compose(keys) ?: return false
        if (composed.strayJamo.isNotEmpty()) return false
        return composed.syllables.all { korean.syllableLogProbability(it) > KoreanModel.UNKNOWN_SYLLABLE_LOG_P }
    }

    /** The front as English (log-probability of the word); null when it can't be one. */
    fun frontScore(front: String): Double? = frontReading(front)?.first

    /** [frontScore], and whether the front is an acronym or a name typed with its capitals. */
    fun frontReading(front: String): Pair<Double, Boolean>? {
        if (front.length < 2 || !front.all { it.isAsciiLetter }) return null
        // Capitals that are all shifted jamo in keys that spell clean Hangul are Korean typed
        // with shift (akffkRod: 말라깽, tkrlRns: 사기꾼), not an acronym or a name.
        val korean = front.all { !it.isUpperCase() || DubeolsikLayout.isKoreanShiftKey(it) }
            && HangulComposer.compose(front)?.strayJamo?.isEmpty() == true
        if (!korean && isAcronym(front)) return weights.acronym to true
        // A name scores lower the rarer its last part: macOS + 문서에서, not macOSans (Sans) + 서에서.
        if (!korean) nameLevel(front)?.let { return weights.name - weights.perLevel * max(0, it - 10) to true }
        // Lowercase (or a capital at the start: a name, or the start of a sentence) needs
        // 3 letters: so + が, me + で are Japanese.
        if (front.length < 3) return null
        val spelling = english.averageLogProbability(front) * front.length
        val level = english.level(front)
        if (level != null && level <= EnglishModel.KNOWN_LEVEL) {
            return max(spelling, weights.knownBase - weights.perLevel * level) to false
        }
        return spelling - weights.unknownWord - weights.unknownFront to false
    }

    /** GitHub, VSCode, iPhone, macOS: a capital inside, and the part from the last capital a
     *  known word (Hub, Code) or capitals (OS). Not APIdm. The SCOWL level of that last part
     *  (Code: 10, Hub: 35), 10 for capitals; null when [front] isn't a name. */
    fun nameLevel(front: String): Int? {
        if (!front.drop(1).any { it.isUpperCase() } || !front.any { it.isLowerCase() }) return null
        val last = front.indexOfLast { it.isUpperCase() }
        if (last < 0) return null
        if (last == front.length - 1) {
            // Ends in capitals (macOS): the run of them is the last part.
            var start = last
            while (start > 0 && front[start - 1].isUpperCase()) start -= 1
            return if (front.length - start >= 2) 10 else null
        }
        // The last part starts at the last capital (Code of VSCode, Idm of APIdm).
        val part = front.substring(last)
        if (part.length < 3 || !english.isKnownWord(part)) return null
        return english.level(part)
    }

    /** The whole token as Korean, per key. A capital with no shifted jamo (the S of Slack)
     *  can't be Korean. */
    fun koreanScore(keys: String): Double {
        if (SplittingEngine.hasEnglishOnlyCapital(keys)) return Double.NEGATIVE_INFINITY
        val score = korean.score(keys) ?: return Double.NEGATIVE_INFINITY
        val composition = score.composition
        if (composition.strayJamo.isEmpty() && isKoreanWordOrWordWithParticle(composition.text)) {
            return max(score.averageLogProbability, weights.koreanWordFloor)
        }
        return score.averageLogProbability
    }

    /** The whole token as kana, per key: only what keeps it from splitting, as the decision
     *  engine reads a capital as English. Capitals after the first letter aren't romaji
     *  (UIde is UI + で); one at the start may begin a sentence (Kyouha stays whole). */
    fun japaneseScore(keys: String): Double {
        if (keys.drop(1).any { it.isUpperCase() }) return Double.NEGATIVE_INFINITY
        val score = japanese?.score(keys) ?: return Double.NEGATIVE_INFINITY
        return score.averageLogProbability + weights.japaneseOffset
    }

    /** 핸들, 소문은, 행하는: a word of the wider vocabulary, or one with a particle or ending in
     *  the form the word's last syllable takes (씨를, not 씨이 or 씨을). */
    fun isKoreanWordOrWordWithParticle(hangul: String): Boolean {
        if (korean.isVocabularyWord(hangul)) return true
        return hangul.length > 1 && (1 until hangul.length).any { cut ->
            val particle = hangul.substring(cut)
            korean.isVocabularyWord(hangul.substring(0, cut)) && particle in MixedWord.suffixes
                && MixedWord.finalSound(hangul[cut - 1])?.let { MixedWord.particleAgrees(particle, it) } != false
        }
    }

    companion object {
        /** Japanese particles that score `coreParticle` (から, まで, より score `otherParticle`). */
        val coreJapanese = setOf("で", "に", "を", "は", "が", "の", "と", "も")
        /** Particles that follow a noun most of the time: they score `coreParticle`. */
        val coreKorean = words("에 에서 에게 을 를 은 는 이 가 도 로 으로 와 과 의 만 까지 부터 랑 이랑 한테 처럼 보다").toHashSet()

        /** API, UI, PR: 2 to 6 capitals. */
        fun isAcronym(front: String) = front.length in 2..6 && front.all { it.isUpperCase() }
    }
}
