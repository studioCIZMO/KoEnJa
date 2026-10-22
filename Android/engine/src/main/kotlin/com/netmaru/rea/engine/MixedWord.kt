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

        /** Particles with a form after a final consonant and one after a vowel (책을, 사과를): the
         *  consonant form first. A final ㄹ takes the vowel form of 으로 (서울로). */
        val pairedParticles: List<Pair<String, String>> = listOf(
            "을" to "를", "은" to "는", "이" to "가", "과" to "와", "이랑" to "랑", "이랑은" to "랑은",
            "으로" to "로", "으로는" to "로는", "으로도" to "로도", "으론" to "론", "으로서" to "로서",
            "이나" to "나", "이야" to "야", "이라" to "라", "이라고" to "라고", "이라는" to "라는", "이란" to "란",
            "이에요" to "예요",
        )

        /** Whether [particle] is the form that follows a word ending as [final] describes:
         *  null for a particle that has one form (에, 도, 의), else true or false (씨이, 쓰은 are wrong). */
        fun particleAgrees(particle: String, final: FinalSound): Boolean? {
            for ((consonant, vowel) in pairedParticles) {
                if (particle == consonant) return final == FinalSound.CONSONANT || (final == FinalSound.RIEUL && !consonant.startsWith("으"))
                if (particle == vowel) return final == FinalSound.VOWEL || (final == FinalSound.RIEUL && consonant.startsWith("으"))
            }
            return null
        }

        enum class FinalSound { VOWEL, CONSONANT, RIEUL }

        /** How a Hangul syllable ends; null for anything else. */
        fun finalSound(syllable: Char): FinalSound? {
            if (syllable !in '가'..'힣') return null
            return when ((syllable - '가') % 28) {
                0 -> FinalSound.VOWEL
                8 -> FinalSound.RIEUL
                else -> FinalSound.CONSONANT
            }
        }

        /** How an acronym ends when read letter by letter: L, M, N and R (엘, 엠, 엔, 알) on a final
         *  consonant, every other letter's name on a vowel (TM은, TV는). */
        fun finalSoundOfAcronym(acronym: String): FinalSound = when (acronym.lastOrNull()?.uppercaseChar()) {
            'L', 'R' -> FinalSound.RIEUL
            'M', 'N' -> FinalSound.CONSONANT
            else -> FinalSound.VOWEL
        }

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
    /** Scores English word + particle, and English + Korean word, against the whole token. */
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
        if (single != null && !english.isKnownWord(keys) && !particles.readsAsKorean(keys)) {
            return Decision(Language.MIXED, CONFIDENCE, single.text, "split ${single.english}+${single.korean}")
        }
        return base.decide(keys, context)
    }

    companion object {
        /** How sure a split is: above Strict ([AppPolicy.STRICT_THRESHOLD]), since each rule already
         *  asks for an English front it can stand on, so github에 splits in a terminal too. */
        const val CONFIDENCE = 0.98

        /** A capital no lingering Shift explains (RNl, 꿀 typed fast, isn't a name). */
        fun hasEnglishOnlyCapital(front: String) = DubeolsikLayout.hasEnglishOnlyCapital(front)
    }
}
