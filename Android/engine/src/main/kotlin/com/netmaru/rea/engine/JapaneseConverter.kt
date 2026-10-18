package com.netmaru.rea.engine

/** One conversion candidate for (a prefix of) the hiragana being converted (JapaneseConverter.swift). */
data class ConversionCandidate(
    val text: String,
    /** How many kana from the start this candidate covers; less than all converts the first clause. */
    val kanaCount: Int,
    /** A completion of the reading rather than a conversion of it: listed, never shown live. */
    val isPrediction: Boolean = false,
)

/** Kana → kanji conversion: the :japanese module's dictionary converter on Android, fakes in tests. */
interface JapaneseConverter {
    fun candidates(kana: String): List<ConversionCandidate>
    /** The user committed [candidate]: rank it higher next time. */
    fun learn(candidate: ConversionCandidate, kana: String)
    /** Persist anything learned. */
    fun save()
}
