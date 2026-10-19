package com.netmaru.rea.engine

import java.io.File

/**
 * The engine stack iOS's KeyboardViewController builds (makeHeuristic/makeSession): learned
 * corrections, then mixed words (github에, githubで), then the heuristic, weighted by the user's
 * language order (LanguagePreferences).
 */
object Engines {
    /** Score per rank step (average log-probability per key, the engine's scale; its context
     *  bias for the previous word's language is 0.8).
     *  ponytail: hand-picked; tune on held-out mixed text once there is some. */
    const val RANK_STEP = 0.3

    /** Immutable syllable tables shared by every heuristic and splitter. */
    private val korean by lazy { KoreanModel() }

    /** [order]: the enabled languages, most used first; English is always there (the layout's own letters). */
    fun makeHeuristic(order: List<Language>): HeuristicEngine {
        fun bonus(language: Language) = order.indexOf(language).takeIf { it >= 0 }?.let { -it * RANK_STEP }
        val english = bonus(Language.ENGLISH) ?: 0.0
        val heuristic = HeuristicEngine(korean = korean, japanese = if (Language.JAPANESE in order) JapaneseModel.shared else null)
        heuristic.koreanOffset = (bonus(Language.KOREAN) ?: -10.0) - english
        heuristic.japaneseOffset += (bonus(Language.JAPANESE) ?: 0.0) - english
        return heuristic
    }

    fun makeSession(
        order: List<Language>,
        converter: JapaneseConverter?,
        liveConversion: Boolean = false,
        returnConfirmsOnly: Boolean = true,
        fullWidthDigits: Boolean = false,
        fullWidthSpace: Boolean = false,
        learningFile: File? = null,
    ): InputSession {
        val heuristic = makeHeuristic(order)
        val engine = LearningEngine(SplittingEngine(heuristic, heuristic.english, heuristic.korean, heuristic.segmenter), learningFile)
        // Kana while typing, candidates in the bar above, as on the system's Japanese keyboard
        // (live conversion off by default: it guessed whole phrases wrong, はなしま → 話魔).
        val session = InputSession(engine, converter, heuristic.japanese != null, liveConversion, returnConfirmsOnly)
        session.fullWidthDigits = fullWidthDigits
        session.fullWidthSpace = fullWidthSpace
        return session
    }
}
