package com.netmaru.rea

import com.netmaru.rea.engine.EnglishModel
import com.netmaru.rea.engine.HangulComposer
import com.netmaru.rea.engine.KoreanModel

/** Korean and English typo fixes for the bar (iOS Suggestions.swift): a fix one slip away (a
 *  neighbor key, two keys swapped) is offered, and the first one is put in on Space.
 *  iOS asks UITextChecker whether a word is a typo and for completions; here only the engine's
 *  word lists decide, so autocorrect is stricter: a Korean fix must read as more common Korean
 *  (and never for two syllables), an English one must be a common word, and capitalized
 *  English words (names) are left alone.
 *  ponytail: no completions, no system spell checker; add SpellCheckerSession if fixes are missed. */
data class Suggestions(val words: List<String> = emptyList(), val autocorrect: String? = null) {
    companion object {
        /** Keys next to each key on QWERTY (same row and the rows above and below). */
        private val neighbors: Map<Char, String> = run {
            val rows = listOf("qwertyuiop", "asdfghjkl", "zxcvbnm")
            val offset = listOf(0.0, 0.5, 1.5) // each row's left edge, in keys
            rows.flatMapIndexed { r, row ->
                row.mapIndexed { i, key ->
                    key to rows.withIndex().filter { (r2, _) -> kotlin.math.abs(r2 - r) <= 1 }.flatMap { (r2, other) ->
                        other.withIndex().filter { (j, c) ->
                            c != key && kotlin.math.abs(j + offset[r2] - i - offset[r]) <= (if (r2 == r) 1.0 else 0.5)
                        }.map { it.value }
                    }.joinToString("")
                }
            }.toMap()
        }

        /** The keys with one slip: each key swapped for a neighbor, or two keys in the wrong order. */
        fun oneSlip(keys: String): List<String> {
            if (keys.length > 64) return emptyList() // URLs, identifiers: not worth the work
            val out = mutableListOf<String>()
            for (i in keys.indices) {
                for (n in neighbors[keys[i].lowercaseChar()].orEmpty()) {
                    out += keys.substring(0, i) + (if (keys[i].isUpperCase()) n.uppercaseChar() else n) + keys.substring(i + 1)
                }
                if (i + 1 < keys.length && keys[i] != keys[i + 1]) {
                    out += keys.substring(0, i) + keys[i + 1] + keys[i] + keys.substring(i + 2)
                }
            }
            return out
        }

        fun korean(word: String, keys: String, model: KoreanModel): Suggestions {
            if (word.length < 2 || model.isVocabularyWord(word) || HangulComposer.compose(keys)?.isBroken != false) return Suggestions()
            fun common(w: String) = w.sumOf { model.syllableLogProbability(it) }
            // Fixes ranked: a word seen in real text first, then by how common its syllables are.
            val fixes = oneSlip(keys).mapNotNull { HangulComposer.compose(it)?.text }.toSet()
                .filter { it.length == word.length && model.isVocabularyWord(it) }
                .sortedWith(compareByDescending<String> { model.isKnownWord(it) }
                    .thenByDescending(::common).thenBy { it })
            // Put in only when it also reads as more common Korean (짐행한다 → 진행한다, not 버그를 → 버스를),
            // and not for two syllables, too often a word our lists miss (눌려, one slip from 물려).
            return Suggestions(fixes.take(2), fixes.firstOrNull()?.takeIf { word.length >= 3 && common(it) > common(word) })
        }

        fun english(word: String, model: EnglishModel): Suggestions {
            // Lower case only: a capitalized word is likely a name (Seamtype, REA).
            if (word.length < 2 || word.any { !it.isLowerCase() } || model.level(word) != null) return Suggestions()
            val fixes = oneSlip(word).toSet().filter { model.isCommonWord(it) }.sortedBy { model.level(it) }
            return Suggestions(fixes.take(2), fixes.firstOrNull())
        }
    }
}
