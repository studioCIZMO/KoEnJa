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
        fun offer(split: Split, front: String, particle: Double) {
            val w = frontScore(front) ?: return
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
                  if (composed.text in coreKorean) weights.coreParticle else weights.otherParticle)
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
        val split = best ?: return null
        val k = koreanScore(keys)
        val e = english.averageLogProbability(keys) - weights.unknownWord / n
        val j = if (japaneseOn) japaneseScore(keys) else Double.NEGATIVE_INFINITY
        val whole = maxOf(k, e, j)
        val detail = String.format(Locale.ROOT, "split=%.2f ko=%.2f en=%.2f ja=%.2f", bestScore, k, e, j)
        return Scored(split, bestScore - whole, detail)
    }

    /** The front as English (log-probability of the word); null when it can't be one. */
    fun frontScore(front: String): Double? {
        if (front.length < 2 || !front.all { it.isAsciiLetter }) return null
        // Capitals that are all shifted jamo in keys that spell clean Hangul are Korean typed
        // with shift (akffkRod: 말라깽, tkrlRns: 사기꾼), not an acronym or a name.
        val korean = front.all { !it.isUpperCase() || DubeolsikLayout.isKoreanShiftKey(it) }
            && HangulComposer.compose(front)?.strayJamo?.isEmpty() == true
        if (!korean && isAcronym(front)) return weights.acronym
        if (!korean && isName(front)) return weights.name
        // Lowercase (or a capital at the start: a name, or the start of a sentence) needs
        // 3 letters: so + が, me + で are Japanese.
        if (front.length < 3) return null
        val spelling = english.averageLogProbability(front) * front.length
        val level = english.level(front)
        if (level != null && level <= EnglishModel.KNOWN_LEVEL) {
            return max(spelling, weights.knownBase - weights.perLevel * level)
        }
        return spelling - weights.unknownWord - weights.unknownFront
    }

    /** GitHub, VSCode, iPhone, macOS: a capital inside, and the part from the last capital a
     *  known word (Hub, Code) or capitals (OS). Not APIdm. */
    fun isName(front: String): Boolean {
        if (!front.drop(1).any { it.isUpperCase() } || !front.any { it.isLowerCase() }) return false
        val last = front.indexOfLast { it.isUpperCase() }
        if (last < 0) return false
        if (last == front.length - 1) {
            // Ends in capitals (macOS): the run of them is the last part.
            var start = last
            while (start > 0 && front[start - 1].isUpperCase()) start -= 1
            return front.length - start >= 2
        }
        // The last part starts at the last capital (Code of VSCode, Idm of APIdm).
        val part = front.substring(last)
        return part.length >= 3 && english.isKnownWord(part)
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

    /** The whole token as kana, per key. Capitals after the first letter aren't romaji
     *  (UIde is UI + で); one at the start may begin a sentence (Kyouha). */
    fun japaneseScore(keys: String): Double {
        if (keys.drop(1).any { it.isUpperCase() }) return Double.NEGATIVE_INFINITY
        val score = japanese?.score(keys) ?: return Double.NEGATIVE_INFINITY
        return score.averageLogProbability + weights.japaneseOffset
    }

    /** 핸들, 소문은, 행하는: a word of the wider vocabulary, or one with a particle or ending. */
    fun isKoreanWordOrWordWithParticle(hangul: String): Boolean {
        if (korean.isVocabularyWord(hangul)) return true
        return hangul.length > 1 && (1 until hangul.length).any { cut ->
            korean.isVocabularyWord(hangul.substring(0, cut)) && hangul.substring(cut) in MixedWord.suffixes
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
