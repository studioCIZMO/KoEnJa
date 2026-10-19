package com.netmaru.rea.engine

/**
 * An English word with a Korean particle or ending typed straight after it,
 * e.g. `githubdp` → `github에`, `APIfmf` → `API를`, `commitgotj` → `commit해서`.
 */
data class MixedWord(val english: String, val koreanKeys: String, val korean: String) {
    val text get() = english + korean

    companion object {
        /** Particles and endings that commonly attach directly to a (loan)word. */
        val suffixes: Set<String> = words("""
            에 에서 에게 에는 에서는 에도 에서도 의 을 를 은 는 이 가 도 로 으로 와 과 랑 이랑 하고 한테 까지 부터 만 보다
            처럼 이나 나 이야 야 이에요 예요 이다 입니다 인데 이라 이라고 라고 으로는 로는 으로도 로도 이고 이지 이랑은
            했어 했다 했어요 했는데 했고 했지 했음 해서 해야 하는 하면 하고 하기 하게 하다 해 해요 합니다 할 한 함 하자 해줘 해봐
            됐어 됐다 된 되는 되면 돼 돼요 됩니다 같은 같아 적 적인 적으로 스럽게 같이 들 들이 들을 들은 에요 이요 를요 을요
            같은건 같은거 같은게 같은걸 같은데 같다 같네 같아서 같은지 같고 같아요 인가 인지 인데요 인건 인게 인거
            이라는 라는 이란 에선 으론 로서 으로서
        """).toHashSet()

        /** Particles that follow a single letter (x에, n이, a를, X축의 X): case particles only, not
         *  endings like 해/한, so short English (ago) doesn't split. */
        val singleLetterParticles: Set<String> = words("""
            에 에서 에게 에는 에서는 에도 의 을 를 은 는 이 가 도 로 으로 와 과 랑 이랑 하고 한테 까지 부터 만 보다
            처럼 이나 나 이라 이라고 라고 이랑은 으로는 로는 같은 같이
        """).toHashSet()

        /** A single letter and a particle: `xdp` → `x에`, `Xfmf` → `X를`. One key on its own can't
         *  make a Hangul syllable before another consonant, so the keys never read as Korean. */
        fun splitSingleLetter(keys: String): MixedWord? {
            if (keys.length < 2 || !keys[0].isAsciiLetter) return null
            val first = keys[0]
            val suffixKeys = keys.substring(1)
            if (!DubeolsikLayout.startsSyllable(suffixKeys[0])) return null
            val composed = HangulComposer.compose(suffixKeys) ?: return null
            if (composed.strayJamo.isNotEmpty() || composed.text !in singleLetterParticles) return null
            // 도 and 나 are the keys eh and sk, common English endings (heh, meh, yeh, tsk): after a
            // lowercase letter they stay English. A capital (X도) was typed on purpose.
            if (first.isLowerCase() && composed.text in setOf("도", "나")) return null
            return MixedWord(first.toString(), suffixKeys, composed.text)
        }

        /** Splits off the longest Korean particle/ending at the end of [keys].
         *  Only the shape is checked here; whether the front really is English is
         *  up to the caller (see [SplittingEngine]). */
        fun split(keys: String): MixedWord? {
            // Front needs at least 2 letters; try the longest suffix first.
            if (keys.length < 3) return null
            for (start in 2 until keys.length) {
                val suffixKeys = keys.substring(start)
                if (!DubeolsikLayout.startsSyllable(suffixKeys[0])) continue
                val composed = HangulComposer.compose(suffixKeys) ?: continue
                if (composed.strayJamo.isNotEmpty() || composed.text !in suffixes) continue
                return MixedWord(keys.substring(0, start), suffixKeys, composed.text)
            }
            return null
        }
    }
}

/** A Korean syllable has to start with a consonant key. */
fun DubeolsikLayout.startsSyllable(key: Char) = jamo(key)?.let(Jamo::isConsonant) ?: false

/**
 * Decides `mixed` for English and Korean typed as one token: English + particle (github에),
 * a single letter + particle (x에), English + Korean word (hello월드), and with Japanese on
 * English + Japanese particle (githubで). Never when the whole token is a known English word or
 * reads as ordinary Korean (every syllable is one people actually use).
 */
class SplittingEngine(
    val base: DecisionEngine,
    val english: EnglishModel,
    val korean: KoreanModel = KoreanModel(),
    /** With Japanese on: English word + Japanese particle (githubで), decided the same way. */
    val japanese: JapaneseSegmenter? = null,
) : DecisionEngine {
    /** English word + Korean word (hello월드) only when the whole token spells less like
     *  English than this; stricter still when the Korean half isn't in the word lists. */
    var mixedWordSpellingCeiling = -2.7
    var unknownWordSpellingCeiling = -3.2

    /** Scores English word + particle against the whole token. */
    val particles = ParticleSplitter(english, korean, japanese?.japanese)

    override fun decide(keys: String, context: DecisionContext): Decision {
        // English word + particle, Korean (github에) or with Japanese on Japanese (githubで),
        // when that reading beats the whole token in one language. Before English + Korean
        // word: beautywo is beauty + を, not beau + 쇼재.
        when (val split = particles.split(keys, japaneseOn = japanese != null)) {
            is ParticleSplitter.Split.Korean -> return Decision(Language.MIXED, CONFIDENCE, split.mixed.text,
                                                                "split ${split.mixed.english}+${split.mixed.korean}")
            is ParticleSplitter.Split.Japanese -> return Decision(Language.JAPANESE, CONFIDENCE, split.english + split.particle,
                                                                  "split ${split.english}+${split.particle}")
            null -> {}
        }
        val single = MixedWord.splitSingleLetter(keys)
        if (single != null && !english.isKnownWord(keys) && !readsAsKorean(keys)) {
            return Decision(Language.MIXED, CONFIDENCE, single.text, "split ${single.english}+${single.korean}")
        }
        englishThenKoreanWord(keys)?.let {
            return Decision(Language.MIXED, CONFIDENCE, it.text, "split ${it.english}+${it.korean}")
        }
        return base.decide(keys, context)
    }

    /** An English word with a whole Korean word typed straight after it, no space:
     *  `hellodnjfem` → `hello월드`, `designvhsxm` → `design폰트`. The front has to be a known
     *  English word (or a name typed with capitals) and the back at least two syllables:
     *  a known Korean word (optionally with a particle), or, when the whole token spells
     *  nothing like English, any Hangul made of syllables people use. Prefers the longest
     *  English front. Names that spell like English stay whole (cartman isn't car스무). */
    fun englishThenKoreanWord(keys: String): MixedWord? {
        if (keys.length < 5 || english.isKnownWord(keys)) return null
        val spelling = english.averageLogProbability(keys)
        // Clean romaji (hasansowo, はさんそを) is Japanese, not English + Hangul.
        if (spelling >= mixedWordSpellingCeiling || RomajiComposer.compose(keys).isClean) return null
        for (start in keys.length - 2 downTo 2) {
            val front = keys.substring(0, start)
            val back = keys.substring(start)
            // Capitals count only when one has no Korean meaning (ThkT is 쐈, not a name).
            if (!((hasEnglishOnlyCapital(front) && (isAcronym(front) || isCapitalizedName(front)))
                  || (front.length >= 3 && english.isKnownWord(front)))) continue
            if (!DubeolsikLayout.startsSyllable(back[0])) continue
            val composed = HangulComposer.compose(back) ?: continue
            if (composed.strayJamo.isNotEmpty() || composed.syllables.size < 2) continue
            if (!(isKoreanWordOrWordWithParticle(composed.text)
                  || (spelling < unknownWordSpellingCeiling
                      && composed.syllables.all { korean.syllableLogProbability(it) > KoreanModel.UNKNOWN_SYLLABLE_LOG_P }))) continue
            if (!(hasEnglishOnlyCapital(front) || !readsAsKorean(keys))) return null
            return MixedWord(front, back, composed.text)
        }
        return null
    }

    private fun isKoreanWordOrWordWithParticle(hangul: String): Boolean {
        if (korean.isVocabularyWord(hangul)) return true
        return (1 until hangul.length).any { cut ->
            cut >= 2 && korean.isVocabularyWord(hangul.substring(0, cut)) && hangul.substring(cut) in MixedWord.suffixes
        }
    }

    private fun readsAsKorean(keys: String): Boolean {
        val composed = HangulComposer.compose(keys) ?: return false
        if (composed.strayJamo.isNotEmpty()) return false
        return composed.syllables.all { korean.syllableLogProbability(it) > KoreanModel.UNKNOWN_SYLLABLE_LOG_P }
    }

    companion object {
        /** How sure a split is: above Strict ([AppPolicy.STRICT_THRESHOLD]), since each rule already
         *  asks for an English front it can stand on, so github에 splits in a terminal too. */
        const val CONFIDENCE = 0.98

        /** UI, AI, OS, API: capitals typed on purpose, with at least one that has no shifted
         *  jamo (so it can't be ㅃㅉㄸㄲㅆㅒㅖ). Short ones count too (UI같은건). */
        fun isAcronym(front: String) = front.length in 2..6 && front.all { it.isUpperCase() }
            && front.any { !DubeolsikLayout.isKoreanShiftKey(it) }

        /** Slack, TextEdit, VSCode, GitHub: a name typed with its capitals, so English even when
         *  it isn't in the word list. Starts with a capital, has lowercase too, and has either a
         *  capital with no shifted jamo or another capital inside (PascalCase). Korean typed with
         *  shift (TkdRjvnf, 쌍꺼풀) also passes the second test; `readsAsKorean` still stops it. */
        fun isCapitalizedName(front: String): Boolean {
            if (front.length < 3 || !front[0].isUpperCase() || !front.any { it.isLowerCase() }) return false
            if (hasEnglishOnlyCapital(front)) return true
            if (!front.drop(1).any { it.isUpperCase() }) return false
            // Every capital a shifted jamo and the keys spelling whole Hangul is Korean typed with shift,
            // even with a typo making an odd syllable (RoRmtgkgfu, 깨끗핳려), not a name (TextEdit).
            return HangulComposer.compose(front)?.let { it.strayJamo.isNotEmpty() } ?: true
        }

        fun hasEnglishOnlyCapital(front: String) = front.any { it.isUpperCase() && !DubeolsikLayout.isKoreanShiftKey(it) }
    }
}
