package com.netmaru.rea.engine

import java.io.File
import java.time.LocalDate
import java.time.temporal.ChronoUnit

/**
 * The user's dictionary: keys typed as a whole word, and the language they become.
 *
 * `USER` entries were added by hand and stay until removed. `LEARNED` ones come from corrections
 * with the switch key: corrected once they are `WEAK` and only nudge the decision (and are dropped
 * after [WEAK_LIFETIME] days unused), corrected the same way again they are `SURE` and win. A sure
 * word switched once to another reading notes it ([Entry.next]): switched there again, that reading
 * is remembered instead; typed as remembered, the note goes.
 *
 * Not ported (the macOS settings window's): text entries, entries limited to apps, add/remove/pin,
 * lexicon.json migration (the old learning file's lines are read instead, see [DictionaryEngine]).
 */
class UserDictionary(entries: List<Entry> = emptyList()) {
    enum class Source { USER, LEARNED }
    enum class Strength { WEAK, SURE }

    data class Entry(
        val keys: String,
        val language: Language,
        val source: Source,
        /** Learned entries only. */
        val strength: Strength? = null,
        /** The day a learned entry was last corrected or typed. */
        val last: LocalDate? = null,
        /** A sure entry's word was switched once to this reading since it was last typed as remembered. */
        val next: Language? = null,
    ) {
        /** A language entry that wins the decision: added by hand, or corrected twice. */
        val wins get() = strength != Strength.WEAK
    }

    val entries = entries.toMutableList()

    /** Records that [keys] were corrected to [language] while [shown] was on screen.
     *  Returns true when the correction made the entry sure.
     *
     *  - Back to what the engine showed, or away from a weak entry's language while it was
     *    shown (the nudge turned the word wrongly): the learned entry goes.
     *  - The same correction as the weak entry: sure.
     *  - Otherwise: a weak entry for [language], replacing any learned one.
     *  Keys the user added by hand aren't changed by corrections. */
    fun learn(keys: String, language: Language, shown: Language, today: LocalDate): Boolean {
        if (keys.isEmpty() || entries.any { it.keys == keys && it.source == Source.USER }) return false
        val index = entries.indexOfFirst { it.keys == keys && it.source == Source.LEARNED }
        val current = entries.getOrNull(index)
        if (current?.language == language && current.strength == Strength.SURE) {
            entries[index] = current.copy(next = null) // chosen as remembered: a switch noted since was a slip
            return false
        }
        // Back to what the engine showed, or away from what a weak entry turned it to: the
        // nudge was wrong, and a weak entry for the reading switched back to isn't a correction.
        val rejected = current?.strength == Strength.WEAK && current.language == shown
        if (language == shown || rejected) {
            if (index >= 0) entries.removeAt(index)
            return false
        }
        // A remembered word switched to yet another reading (switching back to Araso's own forgets
        // it before this): the memory stays until the same switch comes a second time.
        if (current?.strength == Strength.SURE) {
            if (current.next != language) {
                entries[index] = current.copy(last = today, next = language)
                return false
            }
            entries[index] = Entry(keys, language, Source.LEARNED, Strength.SURE, today)
            return true
        }
        if (keys.length > LONGEST_LEARNED) return false
        val sure = current?.language == language
        val entry = Entry(keys, language, Source.LEARNED, if (sure) Strength.SURE else Strength.WEAK, today)
        if (index >= 0) entries[index] = entry else entries.add(entry)
        return sure
    }

    /** A weak entry's word went in as it says: it is still in use, so it stays. A sure one's
     *  did: a switch away from it noted since was a slip.
     *  Returns true when something changed. */
    fun used(keys: String, language: Language, today: LocalDate): Boolean {
        val noted = entries.indexOfFirst { it.keys == keys && it.strength == Strength.SURE && it.next != null }
        if (noted >= 0 && entries[noted].language == language) {
            entries[noted] = entries[noted].copy(next = null)
            return true
        }
        val index = entries.indexOfFirst { it.keys == keys && it.strength == Strength.WEAK }
        if (index < 0 || entries[index].language != language || entries[index].last == today) return false
        entries[index] = entries[index].copy(last = today)
        return true
    }

    /** Removes the keys' learned entries, leaving a word added by hand. Returns true when something was removed. */
    fun forgetLearned(keys: String): Boolean = entries.removeAll { it.keys == keys && it.source == Source.LEARNED }

    /** Drops weak entries not corrected or typed for [WEAK_LIFETIME] days. */
    fun dropExpired(today: LocalDate): Boolean = entries.removeAll { entry ->
        entry.strength == Strength.WEAK && (entry.last == null || ChronoUnit.DAYS.between(entry.last, today) >= WEAK_LIFETIME)
    }

    companion object {
        /** Days a weak entry lasts without being corrected or typed again. */
        const val WEAK_LIFETIME = 30
        /** Longer tokens aren't learned: a sentence switched as one token isn't a word. */
        const val LONGEST_LEARNED = 20
    }
}

/**
 * Wraps an engine with the user's dictionary: a language entry that wins (added by hand,
 * or corrected the same way twice) decides the keys, and also the keys with a particle
 * typed straight after them (촉매를, github에, githubで, hogeで); a weak one (corrected once)
 * passes the base engine a nudge toward its language, so only a close call turns.
 *
 * [storeFile]: where it is kept (lines of `user|sure|weak <TAB> keys <TAB> language [<TAB> yyyy-MM-dd [<TAB> next]]`;
 * LearningEngine's `learned|once` lines are read as sure and weak); null keeps it in memory.
 */
class DictionaryEngine(
    val base: DecisionEngine,
    /** For keys that are an English word as a whole: no particle split there. */
    val english: EnglishModel = EnglishModel.shared,
    korean: KoreanModel = KoreanModel(),
    /** Whether Japanese particles split off (Japanese on). */
    val japaneseParticles: Boolean = false,
    private val storeFile: File? = null,
    private val today: () -> LocalDate = { LocalDate.now() },
) : DecisionEngine {
    /** For keys that read as a Korean word, or one with a particle, as a whole (놈들을, not sha들을). */
    private val splitter = ParticleSplitter(english, korean)
    private val store = read(storeFile, today())
    /** The entries by keys, so each key typed looks its keys (and the fronts a particle may follow) up. */
    private var byKeys: Map<String, UserDictionary.Entry> = store.entries.associateBy { it.keys }

    /** Language entries that win: keys → language. */
    val learned: Map<String, Language> @Synchronized get() = store.entries.filter { it.wins }.associate { it.keys to it.language }

    override fun decide(keys: String, context: DecisionContext): Decision {
        val entry = synchronized(this) { byKeys[keys] }
        if (entry != null && entry.wins) {
            // Rendered by the base engine: this engine's own render(MIXED) would ask it to
            // decide the keys again, find them in the dictionary, and so on without end.
            return Decision(entry.language, 1.0, base.render(keys, entry.language),
                            if (entry.source == UserDictionary.Source.USER) "added" else "learned")
        }
        withParticle(keys)?.let { return it }
        return base.decide(keys, if (entry != null) context.copy(nudge = entry.language) else context)
    }

    /** A word that wins, with a particle or ending typed straight after it: Korean as a whole
     *  (촉매를, the particle in the form the word takes), English + Korean particle as mixed
     *  (github에), English or Japanese + Japanese particle as Japanese (githubで, hogeで). The
     *  longest word first; never keys that are an English word as a whole, and not after a
     *  one-key word. An English word, as in [ParticleSplitter], needs 3 letters unless it has
     *  capitals (so + が is そが), and doesn't split keys that read as a Korean word or one with
     *  a particle (sha + 들을 is 놈들을) or as clean romaji (toki + は is ときは). The same
     *  confidence as the engine's own splits, so a Strict app splits too. */
    fun withParticle(keys: String): Decision? {
        if (keys.length < 3 || english.isKnownWord(keys)) return null
        val fronts = synchronized(this) {
            (keys.length - 1 downTo 2).mapNotNull { length -> byKeys[keys.substring(0, length)]?.takeIf { it.wins } }
        }
        for (word in fronts) {
            val tail = keys.substring(word.keys.length)
            val how = (if (word.source == UserDictionary.Source.USER) "added " else "learned ") + word.keys + "+particle"
            when (word.language) {
                Language.KOREAN -> {
                    val front = HangulComposer.compose(word.keys)
                    if (front == null || front.strayJamo.isNotEmpty()) continue
                    val particle = koreanParticle(tail) ?: continue
                    val last = front.text.lastOrNull() ?: continue
                    if (MixedWord.finalSound(last)?.let { MixedWord.particleAgrees(particle, it) } == false) continue
                    return Decision(Language.KOREAN, SplittingEngine.CONFIDENCE, front.text + particle, how)
                }
                Language.ENGLISH -> {
                    if (word.keys.length < 3 && !word.keys.any { it.isUpperCase() }) continue
                    val particle = koreanParticle(tail)
                    if (particle != null && !readsAsKoreanWord(keys)) {
                        return Decision(Language.MIXED, SplittingEngine.CONFIDENCE, MixedWord(word.keys, tail, particle).text, how)
                    }
                    if (japaneseParticles && !RomajiComposer.compose(keys).isClean) {
                        japaneseParticle(tail)?.let {
                            return Decision(Language.JAPANESE, SplittingEngine.CONFIDENCE, word.keys + it, how)
                        }
                    }
                }
                Language.JAPANESE -> {
                    val front = RomajiComposer.compose(word.keys)
                    if (!japaneseParticles || !front.isClean) continue
                    val particle = japaneseParticle(tail) ?: continue
                    return Decision(Language.JAPANESE, SplittingEngine.CONFIDENCE, front.text + particle, how)
                }
                Language.MIXED -> continue
            }
        }
        return null
    }

    private fun readsAsKoreanWord(keys: String): Boolean {
        val composed = HangulComposer.compose(keys) ?: return false
        return composed.strayJamo.isEmpty() && splitter.isKoreanWordOrWordWithParticle(composed.text)
    }

    /** Records that [keys] were corrected to [language]: once is weak, the same again sure,
     *  back to the engine's choice forgets the word.
     *
     *  Pass [engineChoice] when the caller already knows what was shown (the input method
     *  always does: a fresh decision might not match). */
    fun learn(keys: String, language: Language, engineChoice: Language? = null) {
        val shown = engineChoice ?: decide(keys, DecisionContext()).language
        val own = ownDecision(keys, DecisionContext()).language
        val day = today()
        persist(synchronized(this) {
            val before = store.entries.toList()
            // Switched away from the reading the dictionary gave, back to Araso's own: forgotten in one go.
            val learned = store.entries.firstOrNull { it.keys == keys && it.source == UserDictionary.Source.LEARNED }
            if (!(language == own && learned?.language == shown && store.forgetLearned(keys))) store.learn(keys, language, shown, day)
            changed(before)
        })
    }

    /** The keys decided by the engine under the dictionary alone, as if the user had never
     *  corrected or added them: Araso's own reading. */
    fun ownDecision(keys: String, context: DecisionContext): Decision = base.decide(keys, context.copy(nudge = null))

    /** The word went in as [language] without a correction: a weak entry saying so stays. */
    fun used(keys: String, language: Language) {
        val day = today()
        persist(synchronized(this) {
            val before = store.entries.toList()
            store.used(keys, language, day)
            changed(before)
        })
    }

    private fun changed(before: List<UserDictionary.Entry>): String? {
        if (store.entries == before) return null
        byKeys = store.entries.associateBy { it.keys }
        return text(store.entries)
    }

    private fun persist(snapshot: String?) {
        val file = storeFile ?: return
        snapshot ?: return
        write(file, snapshot)
    }

    private companion object {
        /** The tail as one of the particles and endings that attach to a word (에, 를, 에서, 했어). */
        fun koreanParticle(tail: String): String? {
            if (tail.isEmpty() || !DubeolsikLayout.startsSyllable(tail[0])) return null
            val composed = HangulComposer.compose(tail) ?: return null
            return composed.text.takeIf { composed.strayJamo.isEmpty() && it in MixedWord.suffixes }
        }

        /** The tail as a Japanese particle (で, に, から). */
        fun japaneseParticle(tail: String): String? {
            val composed = RomajiComposer.compose(tail)
            return composed.text.takeIf { composed.isClean && it in JapaneseSegmenter.trailingParticles }
        }

        /** Unreadable lines (hand-edited, or from a newer version) are skipped; expired weak entries dropped. */
        fun read(file: File?, today: LocalDate): UserDictionary {
            val entries = ArrayList<UserDictionary.Entry>()
            file?.takeIf { it.isFile }?.forEachLine { line ->
                val parts = line.split('\t')
                val language = Language.entries.firstOrNull { it.raw == parts.getOrNull(2) } ?: return@forEachLine
                if (parts[1].isEmpty() || entries.any { it.keys == parts[1] }) return@forEachLine
                val last = parts.getOrNull(3)?.let { runCatching { LocalDate.parse(it) }.getOrNull() } ?: today
                entries += when (parts[0]) {
                    "user" -> UserDictionary.Entry(parts[1], language, UserDictionary.Source.USER)
                    "sure", "learned" -> UserDictionary.Entry(parts[1], language, UserDictionary.Source.LEARNED, UserDictionary.Strength.SURE, last,
                                                              Language.entries.firstOrNull { it.raw == parts.getOrNull(4) })
                    "weak", "once" -> UserDictionary.Entry(parts[1], language, UserDictionary.Source.LEARNED, UserDictionary.Strength.WEAK, last)
                    else -> return@forEachLine
                }
            }
            return UserDictionary(entries).apply { if (dropExpired(today) && file != null) write(file, text(this.entries)) }
        }

        fun text(entries: List<UserDictionary.Entry>) = buildString {
            for (e in entries) {
                val kind = e.strength?.name?.lowercase() ?: "user"
                append("$kind\t${e.keys}\t${e.language.raw}${e.last?.let { "\t$it" } ?: ""}${e.next?.let { "\t${it.raw}" } ?: ""}\n")
            }
        }

        fun write(file: File, text: String) {
            runCatching {
                file.parentFile?.mkdirs()
                val temp = File(file.path + ".tmp")
                temp.writeText(text)
                temp.renameTo(file)
            }
        }
    }
}
