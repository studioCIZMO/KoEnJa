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
        decideJoined(keys, context)?.let { return it }
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

    /** A word that kept the digits and `-`/`_` typed into it ([InputSession]: k8s, utf-8, 안녕1),
     *  read piece by piece between them, each English unless it reads as Korean by enough
     *  ([readsAsKoreanPiece]: 옵션1, 보고서_최종, 제1장; not cmd0, en_s2c, src_dir) or is a particle
     *  after the last of them (mp3를, k8s에); not a Japanese phrase, clean romaji throughout
     *  (ra-men, kyou3ji). */
    fun decideJoined(keys: String, context: DecisionContext): Decision? {
        if (keys.firstOrNull()?.isLetter() != true || !keys.any(::joinsWord)) return null
        val base = base.decide(keys, context)
        // An English word before the mark keeps it English, as it did before the mark joined it
        // (five-o, woo-hoo), and so do English words throughout (to-do, co-op); a two-letter
        // front that is also romaji (pa-tigakita, ra-men) doesn't.
        val segments = keys.split('0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_').filter(String::isNotEmpty)
        if (base.language == Language.JAPANESE && RomajiComposer.compose(keys).strayLetters == 0 &&
            !(segments.firstOrNull()?.let { it.length >= 3 && english.isKnownWord(it) } ?: false) &&
            !segments.all(english::isKnownWord)) return base

        val pieces = ArrayList<Pair<StringBuilder, Boolean>>() // text, letters
        for (ch in keys) {
            val letters = !joinsWord(ch)
            val last = pieces.lastOrNull()
            if (last != null && last.second == letters) last.first.append(ch) else pieces.add(StringBuilder().append(ch) to letters)
        }
        val output = StringBuilder()
        val languages = HashSet<Language>()
        var confidence = CONFIDENCE
        var previous: Language? = null // how the last letters before this piece went in
        for ((index, piece) in pieces.withIndex()) {
            val text = piece.first.toString()
            if (!piece.second) { output.append(text); continue }
            val beforeDigit = index + 1 < pieces.size && pieces[index + 1].first.first().isNumber
            val afterDigit = index > 0 && pieces[index - 1].first.last().isNumber
            val particle = if (index > 0 && index == pieces.size - 1) particle(after = text) else null
            if (readsAsKoreanPiece(text, beforeDigit, afterDigit, afterEnglish = previous == Language.ENGLISH,
                                   atEnd = index == pieces.size - 1)) {
                output.append(HangulComposer.composeRuns(text))
                languages.add(Language.KOREAN)
                previous = Language.KOREAN
                // Only as sure as the letters alone are (as the word without the digit would go in),
                // so a Strict app keeps an unsure one English.
                if (index == 0) confidence = minOf(confidence, this.base.decide(text, context).confidence)
            } else if (particle != null) {
                // A particle ending the word after the last digit or mark, or one letter on (mp3 + 를,
                // k8s + 에: sdp; not mp3fmf-1); not an English word there (merry-go: not 해, heh-heh: not
                // 도), nor more of one (mani-pedi).
                output.append(particle.first).append(particle.second)
                languages.add(Language.KOREAN)
                if (particle.first.isNotEmpty()) languages.add(Language.ENGLISH)
            } else {
                output.append(text)
                languages.add(Language.ENGLISH)
                previous = Language.ENGLISH
            }
        }
        val language = if (languages.size > 1) Language.MIXED else languages.firstOrNull() ?: Language.ENGLISH
        return Decision(language, confidence, output.toString(), "joined $output")
    }

    /** Letters next to a digit, `-` or `_` are code more often than the same letters alone: Korean
     *  only when the whole piece's Korean score leads its English score by `identifierCost`
     *  (옵션1, 보고서_최종, 안녕1하세요; not en_s2c as 두_s2c), and a single syllable, rarely a word
     *  in code, by `oneSyllableInIdentifier` more (not cmd0 as 층0, src_dir as src_약), unless it is
     *  counted after a number (제1장, 3월5일) or one written before one (제1, 주3회, 표1). */
    fun readsAsKoreanPiece(keys: String, beforeDigit: Boolean, afterDigit: Boolean, afterEnglish: Boolean, atEnd: Boolean): Boolean {
        val lead = koreanLead(keys) ?: return false
        val composed = HangulComposer.compose(keys) ?: return false
        // After English a particle is the English word's, at the word's end only (mp3를, mp3에서; not
        // mp3를-1, mp3에서-1); after Korean the same syllables are words of their own (해서_1, 같은_것).
        val particle = afterEnglish && composed.text in MixedWord.suffixes
        if (particle && !atEnd) return false
        val w = particles.weights
        val counted = afterDigit && !particle || beforeDigit && composed.text in NUMBER_WORDS
        return lead > w.identifierCost + (if (composed.syllables.size < 2 && !counted) w.oneSyllableInIdentifier else 0.0)
    }

    /** The piece's Korean score less its English one, over all its keys (null when it isn't clean
     *  Hangul): [ParticleSplitter]'s readings, a vocabulary word floored, English as a known word
     *  or spelled. */
    fun koreanLead(keys: String): Double? {
        if (keys.firstOrNull()?.let(DubeolsikLayout::startsSyllable) != true) return null
        if (HangulComposer.compose(keys)?.strayJamo?.isEmpty() != true) return null
        val w = particles.weights
        val korean = particles.koreanScore(keys) * keys.length
        if (!korean.isFinite()) return null
        val lower = keys.lowercase()
        val spelling = english.averageLogProbability(lower) * keys.length
        val level = english.level(lower)
        val englishScore = when {
            level != null && level <= EnglishModel.KNOWN_LEVEL -> maxOf(spelling, w.knownBase - w.perLevel * level)
            keys.uppercase() in EnglishModel.acronyms -> maxOf(spelling, w.acronym)
            else -> spelling - w.unknownWord
        }
        return korean - englishScore
    }

    /** A particle making up [keys] or starting one letter into it (fmf: 를, sdp: s + 에), unless
     *  [keys] is an English word. */
    private fun particle(after: String): Pair<String, String>? {
        if (english.isKnownWord(after)) return null
        for (start in 0 until minOf(2, after.length)) {
            val suffix = after.substring(start)
            if (!DubeolsikLayout.startsSyllable(suffix[0])) continue
            val composed = HangulComposer.compose(suffix) ?: continue
            if (composed.strayJamo.isNotEmpty() || composed.text !in MixedWord.suffixes) continue
            return after.substring(0, start) to composed.text
        }
        return null
    }

    companion object {
        /** Syllables written right before a number (제1장, 주3회, 표1): Tatoeba's Korean has 제 and 주
         *  there, and documents number 표, 장, 절, 항, 조 and 호. */
        val NUMBER_WORDS = setOf("제", "주", "표", "장", "절", "항", "조", "호")

        /** What an English word keeps while it is typed: digits, `-` and `_`. */
        fun joinsWord(ch: Char) = ch.isAscii && (ch.isNumber || ch == '-' || ch == '_')

        /** How sure a split is: above Strict ([AppPolicy.STRICT_THRESHOLD]), since each rule already
         *  asks for an English front it can stand on, so github에 splits in a terminal too. */
        const val CONFIDENCE = 0.98

        /** A capital no lingering Shift explains (RNl, 꿀 typed fast, isn't a name). */
        fun hasEnglishOnlyCapital(front: String) = DubeolsikLayout.hasEnglishOnlyCapital(front)
    }
}
