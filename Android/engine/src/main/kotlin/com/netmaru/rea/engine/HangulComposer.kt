package com.netmaru.rea.engine

/**
 * Turns a sequence of 2-set keystrokes into Hangul, the same way a standard
 * 두벌식 automaton does (compound vowels, compound finals, final-consonant
 * migration to the next syllable).
 */
object HangulComposer {
    /** A syllable, or (`isSyllable` false) a jamo that never became part of a complete syllable. */
    data class Unit(val character: Char, val isSyllable: Boolean)

    data class Result(val units: List<Unit>) {
        val text: String get() = units.joinToString("") { it.character.toString() }
        val syllables: List<Char> get() = units.filter { it.isSyllable }.map { it.character }
        val strayJamo: List<Char> get() = units.filter { !it.isSyllable }.map { it.character }
        /** Jamo that can't be Korean even mid-word: stray jamo other than a consonant just
         *  typed to start the next syllable (하느님ㅇ is 하느님의 on its way, 해ㅐ is not Korean). */
        val isBroken: Boolean get() = strayJamo.size > (if (endsWithPendingConsonant) 1 else 0)
        /** True when the last unit is a lone consonant, i.e. the user has just
         *  started the next syllable. Normal while typing, suspicious at a word end. */
        val endsWithPendingConsonant: Boolean
            get() = units.lastOrNull()?.let { !it.isSyllable && Jamo.isConsonant(it.character) } ?: false
    }

    /** Returns null if any key is not a letter on the 2-set layout. */
    fun compose(keys: String): Result? {
        val jamos = ArrayList<Char>(keys.length)
        for (key in keys) jamos.add(DubeolsikLayout.jamo(key) ?: return null)
        return compose(jamos)
    }

    fun compose(jamos: List<Char>): Result {
        val units = ArrayList<Unit>()
        var cho: Char? = null
        var jung: Char? = null
        var jong: Char? = null

        fun flush() {
            val c = cho
            val v = jung
            if (c != null && v != null) {
                units.add(Unit(Jamo.syllable(c, v, jong), true))
            } else {
                if (c != null) units.add(Unit(c, false))
                if (v != null) units.add(Unit(v, false))
            }
            cho = null; jung = null; jong = null
        }

        for (j in jamos) {
            if (Jamo.isVowel(j)) {
                val v = jung
                if (v != null) {
                    val final = jong
                    val combined = Jamo.vowelCombos["$v$j"]
                    if (final != null) {
                        // 받침 moves to become the next syllable's initial: 갑+ㅏ → 가바, 값+ㅏ → 갑사
                        val split = Jamo.finalSplits[final]
                        if (split != null) {
                            jong = split.first
                            flush()
                            cho = split.second
                        } else {
                            jong = null
                            flush()
                            cho = final
                        }
                        jung = j
                    } else if (combined != null) {
                        jung = combined
                    } else {
                        flush()
                        jung = j
                    }
                } else {
                    jung = j
                }
            } else {
                val final = jong
                if (jung == null || cho == null) {
                    // Nothing to attach to (or a bare vowel): start a new syllable.
                    if (cho != null || jung != null) flush()
                    cho = j
                } else if (final != null) {
                    val combined = Jamo.finalCombos["$final$j"]
                    if (combined != null) {
                        jong = combined
                    } else {
                        flush()
                        cho = j
                    }
                } else if (Jamo.isFinal(j)) {
                    jong = j
                } else {
                    // ㄸ ㅃ ㅉ cannot be finals.
                    flush()
                    cho = j
                }
            }
        }
        flush()
        return Result(units)
    }
}
