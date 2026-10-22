package com.netmaru.rea.engine

import java.util.Locale
import kotlin.math.exp
import kotlin.math.max

enum class Language {
    ENGLISH,
    KOREAN,
    /** English word + Korean particle/ending, e.g. `github에` (see [MixedWord]). */
    MIXED,
    /** Romaji typed as Japanese (rendered as hiragana; kanji conversion happens on Space). */
    JAPANESE;

    /** The language a sentence continues in after this word. */
    val sentenceLanguage: Language
        get() = when (this) {
            ENGLISH -> ENGLISH
            KOREAN, MIXED -> KOREAN
            JAPANESE -> JAPANESE
        }

    /** What Esc switches to: english → mixed (when the keys split) → korean →
     *  japanese (when enabled) → english. Readings that can't exist for these keys
     *  are skipped (no Korean for "ra-men"), and so are garbled ones (dkssud as
     *  Japanese: dkっすd; loose consonants and ㅜ/ㅠ such as ㅁㅊ or ㅜㅜ aren't garbled) as long as a clean alternative exists — a typo can still
     *  be forced into its language when nothing reads cleanly. [splits]: whether the engine
     *  splits the keys (x에, hello월드 as well as github에); left out, the particle split decides. */
    fun next(keys: String, japanese: Boolean = false, splits: Boolean? = null): Language {
        val available = listOf(ENGLISH, MIXED, KOREAN, JAPANESE).filter {
            when (it) {
                ENGLISH -> true
                MIXED -> splits ?: (MixedWord.split(keys) != null)
                KOREAN -> HangulComposer.compose(keys) != null
                JAPANESE -> japanese
            }
        }
        val clean = available.filter {
            when (it) {
                ENGLISH, MIXED -> true
                KOREAN -> HangulComposer.compose(keys)?.let { it.strayJamo.isEmpty() || it.isJamoAbbreviation } == true
                JAPANESE -> RomajiComposer.compose(keys).strayLetters == 0
            }
        }
        val cycle = if (clean.size >= 2) available.filter { it in clean || it == this } else available
        val index = cycle.indexOf(this)
        if (index < 0) return cycle.firstOrNull() ?: ENGLISH
        return cycle[(index + 1) % cycle.size]
    }
}

/** What the engine knows besides the token itself. */
data class DecisionContext(
    /** Language of the previously committed word, if it is recent enough to matter. */
    val previousLanguage: Language? = null,
    /** The token is still being typed (used for the live preview). */
    val partial: Boolean = false,
    /** The language the user corrected these keys to once (a weak dictionary entry). */
    val nudge: Language? = null,
    /** The app typed into, for the user's dictionary entries limited to apps. */
    val app: String? = null,
)

data class Decision(
    val language: Language,
    /** 0.5 = coin flip, 1.0 = certain. */
    val confidence: Double,
    /** What should be committed for this decision. */
    val output: String,
    /** Human-readable reasoning, for debugging. */
    val explanation: String,
    /** The second most likely language, when the engine compared several. */
    val runnerUp: Language? = null,
)

/** A local decider that maps raw QWERTY keystrokes of one word to a language. */
interface DecisionEngine {
    fun decide(keys: String, context: DecisionContext): Decision

    /** Renders [keys] in a given language regardless of what the engine would pick. */
    fun render(keys: String, language: Language): String = when (language) {
        Language.ENGLISH -> keys
        Language.KOREAN -> HangulComposer.compose(keys)?.text ?: keys
        Language.MIXED -> {
            // The engine's own split (hello월드, github에); the particle split for engines without one.
            val decision = decide(keys, DecisionContext())
            if (decision.language == Language.MIXED) decision.output else MixedWord.split(keys)?.text ?: keys
        }
        Language.JAPANESE -> RomajiComposer.compose(keys).text
    }
}

/**
 * Scores the same keystrokes as English (character trigrams + word list), Korean
 * (2-set syllables) and, when enabled, Japanese (romaji → kana trigrams), each as
 * average log-probability per keystroke, nudges the previous word's language, and
 * picks the best. Confidence comes from the gap to the runner-up.
 */
class HeuristicEngine(
    val english: EnglishModel = EnglishModel.shared,
    val korean: KoreanModel = KoreanModel(),
    /** Null when Japanese is off or its model isn't available. */
    val japanese: JapaneseModel? = null,
) : DecisionEngine {
    /** Added to the previous word's language score: people mostly stay in one language. */
    var contextBias = 0.8
    /** Bias after Japanese text. Raising it to 1.2 recovered 1 of 2,590 held-out phrases
     *  but tripled common English words turning into kana right after Japanese (0.5% → 1.6%). */
    var japaneseContextBias = 0.8
    /** Shifts Japanese scores to the English/Korean scale (kana trigrams over ~80 symbols
     *  per ~2 keys read differently from letter trigrams). Tuned on held-out data so that
     *  English words almost never turn into kana. */
    var japaneseOffset = 0.5
    /** Shifts Korean scores (used when the syllable model changes scale). */
    var koreanOffset = 0.0
    /** Taken off the dictionary floor for rarer English words (SCOWL levels 40–50: dork,
     *  ahem), so a common Korean word with the same keys (애가, 모든) wins the tie. */
    var rareWordFloorPenalty = 0.3
    /** The previous word's pull is scaled by this when the keys are a known word of another
     *  language and not of the previous one (내가 after English, `for` after Korean): that's
     *  someone switching language, not a stray reading. */
    var switchBiasFactor = 0.25
    /** Score floors for a Korean word seen in real text (two or more syllables, one), the
     *  counterpart of the English dictionary floor: 내가, 무슨 stay Korean after English.
     *  A word only in the wider vocabulary (닥터, 했죠: too rare for Tatoeba's sentences), or a
     *  noun the lists have only with a particle (혜택), gets the long floor too, unless the keys
     *  are an English word: that list comes from subtitles, and work (재가) or ahem (모드) must
     *  stay English. */
    var koreanKnownFloorLong = -1.4
    var koreanKnownFloorShort = -2.0
    /** How far Japanese must lead before a known English word is turned into kana
     *  outside Japanese text. */
    var englishWordProtection = 1.0
    /** Added to the language the user corrected the keys to once ([DecisionContext.nudge]):
     *  as much as the previous word's pull, so a close call turns and a clear one doesn't. */
    var correctionBias = 0.8

    val segmenter: JapaneseSegmenter? = japanese?.let { JapaneseSegmenter(english, it) }

    override fun decide(keys: String, context: DecisionContext): Decision {
        // Shift on a key that has no shifted jamo (e.g. "Hello", "API") is only ever English,
        // unless it lingers from a Korean shift key onto the next key (djEJs 어떤, dPFmf 예를):
        // then the scores decide.
        if (DubeolsikLayout.hasEnglishOnlyCapital(keys)) {
            return Decision(Language.ENGLISH, 0.99, keys, "uppercase outside QWERTOP")
        }

        var e = english.averageLogProbability(keys.filter { it.isLetter() })
        // Also while typing: the preview must show what Space/Return will commit.
        val known = english.isKnownWord(keys)
        if (known) {
            // Short dictionary words collide with real Korean (go=해, so=내, do=애),
            // so they get a weaker boost and leave room for context to decide.
            var floor = if (keys.length >= 4) -1.4 else if (keys.length == 3) -2.0 else -2.6
            // to, so, go, do: everyday English just above a one-syllable Korean word (새, 내,
            // 해, 애), so they stay English unless the previous word was Korean.
            if (keys.length == 2 && english.isCommonWord(keys)) floor = koreanKnownFloorShort + 0.1
            if ((english.level(keys) ?: 0) > EnglishModel.EXTRA_LEVEL) floor -= rareWordFloorPenalty
            // Capitalized (The, To): a sentence starting, not ㅆ쏟 or 쌔 typed with Shift.
            if (keys[0].isUpperCase() && keys.drop(1).all { it.isLowerCase() }) floor = max(floor, -1.4)
            e = max(e, floor)
        }
        if (keys.any { !it.isLetter() }) e -= 3 // "ra-men", "kan'i" aren't English

        var k = korean.score(keys, context.partial)
        val knownKorean = k?.let {
            val text = it.composition.text
            it.composition.strayJamo.isEmpty() && (korean.isKnownWord(text)
                || (!known && (korean.isVocabularyWord(text) || korean.isRareVocabularyWord(text) || korean.isKnownStem(text))))
        } ?: false
        if (k != null && knownKorean) {
            val floor = if (k.composition.syllables.size >= 2) koreanKnownFloorLong else koreanKnownFloorShort
            k = KoreanModel.Score(max(k.averageLogProbability, floor), k.composition)
        }
        // A dictionary English word may already be complete, and so are chat jamo (ㅜㅜ): score
        // the Japanese reading as finished too, so the preview shows what Space will commit
        // ("to" stays to, not と; nn shows ㅜㅜ, not ん that Space turned into ㅜㅜ). So is a
        // doubled consonant with nothing after its っ (ff, ww): no Japanese word is only っ, so
        // it isn't one on its way (ff showed っf and went in as ff); って (tte) still is.
        val chat = k?.composition?.isChat == true
        val sokuonFirst = RomajiComposer.compose(keys, partial = true).isOnlySokuon
        // A capital is never romaji: Japanese IMEs type ⇧ letters as themselves (⇧O is O, not お).
        val j = if (keys.any { it.isUpperCase() }) null
            else japanese?.score(keys, context.partial && !known && !chat && !sokuonFirst)
        // English words inside a Japanese phrase (googlede…) score as segmented Japanese,
        // not as kana with stray letters.
        var segmented = if (j != null && j.composition.strayLetters > 0) segmenter?.scoredSegments(keys) else null
        // REA: the capitals inside an English word that leads into Japanese (iPhonedetesuto is
        // iPhone + でてすと) keep the phrase Japanese; a capital still never becomes kana.
        if (j == null && keys.any { it.isUpperCase() }) {
            segmenter?.scoredSegments(keys)?.let { s ->
                if (s.segments.all { it !is JapaneseSegmenter.Segment.Japanese || !it.keys.any(Char::isUpperCase) }) segmented = s
            }
        }
        val japaneseScore = j?.let { max(it.averageLogProbability, segmented?.let { s -> s.score / keys.length } ?: Double.NEGATIVE_INFINITY) }
            ?: segmented?.let { it.score / keys.length } // REA: the segmented reading alone

        val languages = ArrayList<Language>(3)
        val values = ArrayList<Double>(3)
        languages.add(Language.ENGLISH); values.add(e)
        if (k != null) { languages.add(Language.KOREAN); values.add(k.averageLogProbability + koreanOffset) }
        if (japaneseScore != null) { languages.add(Language.JAPANESE); values.add(japaneseScore + japaneseOffset) }
        val previous = context.previousLanguage?.sentenceLanguage
        val i = languages.indexOf(previous)
        if (previous != null && i >= 0) {
            var bias = if (previous == Language.JAPANESE) japaneseContextBias else contextBias
            val knownIn = mapOf(Language.ENGLISH to known, Language.KOREAN to knownKorean, Language.JAPANESE to false)
            if (knownIn[previous] == false && knownIn.any { it.key != previous && it.value }) bias *= switchBiasFactor
            values[i] += bias
        }
        val nudge = context.nudge
        if (nudge != null && nudge != Language.MIXED) languages.indexOf(nudge).takeIf { it >= 0 }?.let { values[it] += correctionBias }
        val scores = languages.indices.map { languages[it] to values[it] }.sortedByDescending { it.second }.toMutableList()

        // A dictionary English word only becomes kana on a clear win, unless we're already
        // in Japanese text or the user corrected it to Japanese once (where "same", "sake", "kite" are Japanese words).
        val englishIndex = scores.indexOfFirst { it.first == Language.ENGLISH }
        if (known && scores[0].first == Language.JAPANESE && previous != Language.JAPANESE && context.nudge != Language.JAPANESE &&
            englishIndex >= 0 && scores[0].second - scores[englishIndex].second < englishWordProtection) {
            scores[0] = scores[englishIndex].also { scores[englishIndex] = scores[0] }
        }

        val (language, best) = scores[0]
        val runnerUp = scores.getOrNull(1)
        val margin = best - (runnerUp?.second ?: (best - 5))
        val confidence = 1 / (1 + exp(-margin * 2))
        val output = when (language) {
            Language.KOREAN -> k?.composition?.text ?: keys
            Language.JAPANESE -> segmented?.let { JapaneseSegmenter.text(it.segments) } ?: j?.composition?.text ?: keys
            else -> keys
        }
        val detail = scores.joinToString(" ") { String.format(Locale.ROOT, "%s=%.2f", it.first.raw.take(2), it.second) }
        return Decision(language, confidence, output,
                        detail + (if (known) " (dict)" else "") + String.format(Locale.ROOT, " margin=%+.2f", margin),
                        runnerUp?.first)
    }
}

/** Swift's raw value: the name used in explanations and the learning file. */
val Language.raw get() = name.lowercase()
