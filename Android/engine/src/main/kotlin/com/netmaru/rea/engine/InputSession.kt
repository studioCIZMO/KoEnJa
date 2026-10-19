package com.netmaru.rea.engine

import java.util.Locale

/** NSRange: UTF-16 offsets, like Kotlin String indices. */
data class TextRange(val location: Int, val length: Int) {
    val end get() = location + length

    companion object {
        /** NSNotFound. */
        const val NOT_FOUND = Int.MAX_VALUE
    }
}

/** The text field the input method is typing into. Ranges are UTF-16, like NSString. */
interface TextClient {
    fun setMarkedText(text: String)
    /** Marked text in place of [range], text the app already has (the marked text, if any, goes too). */
    fun setMarkedText(text: String, replacing: TextRange)
    /** [replacing] null replaces the marked text (or the selection if nothing is marked). */
    fun insertText(text: String, replacing: TextRange?)
    fun selectedRange(): TextRange
    fun markedRange(): TextRange
    fun text(range: TextRange): String?
}

/**
 * Per-client typing state: the word being composed, the language of the previous
 * word, the last committed word (so the switch key right after a commit can change it), and
 * an in-progress kana→kanji conversion.
 *
 * While a word is being composed:
 *   letters        extend it; the underlined preview shows the likely language
 *   switch key     cycle it: english → english+particle (github에) → korean → japanese
 *                  ([Key.Cycle], or Esc where `policy.escapeSwitches`; otherwise Esc commits
 *                  and goes to the app)
 *   Delete         remove the last kana of a Japanese phrase; in Korean the last jamo of the
 *                  syllable being typed, then whole syllables (as the system's 2-set does);
 *                  else the last keystroke. The word keeps its language until the next letter.
 *   Space          English/Korean: commit, then a space. Japanese: convert to kanji
 *                  (with `fullWidthSpace`, a space after Japanese text is full-width; ⇧Space
 *                  types the other width)
 *   0–9            with `fullWidthDigits`, after Japanese text or inside a phrase they are part
 *                  of it, full-width (１２３, ０３－１２３４); Space offers the half-width form
 *                  Otherwise the app types them; a switch key right after digits or symbols it
 *                  typed makes them Japanese (123 → １２３, [ → 「), and in Japanese text a romaji
 *                  word straight after a number is a counter, converted with it (3ji → 3時)
 *   , . ? ! - '    inside a Japanese phrase they are part of it (、。？！ー); otherwise
 *                  they commit the word, then type the key
 *   Return         commit the word as shown, and nothing else (the next Return is the app's);
 *                  with `returnConfirmsOnly` off the app gets it too, except after Japanese
 *   anything else  commit and let the app handle the key
 * While converting to kanji (candidate window open):
 *   Space / ↓      next candidate          ↑      previous candidate
 *   1–9            commit that candidate   Return commit the selected candidate
 *   Esc / Delete   back to kana            letters/punctuation  commit, then keep typing
 *   A candidate that covers only the start of the reading commits that part and
 *   continues converting the rest.
 * Right after a word was committed with space/punctuation:
 *   switch keys    cycle that committed word the same way.
 *   Delete         the app deletes the space or punctuation; the word can still be changed.
 * A [Key.Cycle] with nothing to change is dropped, never passed to the app.
 *                  The same correction made twice is learned for good.
 *
 * Not ported (macOS only): vim mode, pausing (right ⌘), `holdSpace` (terminals), per-app policies.
 */
class InputSession(
    val engine: LearningEngine,
    val converter: JapaneseConverter? = null,
    var japaneseEnabled: Boolean = false,
    /** Live conversion: a Japanese phrase is shown as its top kanji candidate while it is typed,
     *  and committed that way. */
    val liveConversion: Boolean = false,
    /** Return on an underlined word only confirms it. */
    val returnConfirmsOnly: Boolean = true,
) {
    sealed interface Key {
        data class Letter(val ch: Char) : Key
        data object Delete : Key
        data object Escape : Key
        /** A switch key other than Esc (the keyboard's switch key). */
        data object Cycle : Key
        /** Space, digit or punctuation typed by the input method. */
        data class Printable(val ch: Char) : Key
        /** ⇧Space: a space of the other width (full-width where Space types a half-width one). */
        data object ShiftSpace : Key
        data object Enter : Key
        data object Up : Key
        data object Down : Key
        /** ←/→: while converting, shorten or lengthen the first clause; otherwise like [Other]. */
        data object Left : Key
        data object Right : Key
        /** The Japanese reading as katakana / hiragana. */
        data object Katakana : Key
        data object Hiragana : Key
        /** Tab, shortcuts…: commit and let the app handle the key. */
        data object Other : Key
    }

    /** A kana→kanji conversion in progress. */
    data class Conversion(
        /** The reading still being converted. */
        val kana: String,
        /** The romaji that produced [kana], so Esc can go back to editing it. */
        val keys: String,
        val candidates: List<ConversionCandidate>,
        val selected: Int,
        /** The user chose Japanese for these keys with the switch key (going back to kana keeps that). */
        internal val userForced: Boolean = false,
        /** Digits the app had already typed just before the phrase, taken into the reading
         *  (3ji → 3じ → 3時); not part of [keys]. */
        internal val numberPrefix: String = "",
    ) {
        val current get() = candidates[selected]
        /** What the user sees: the selected candidate, then any reading it doesn't cover. */
        val display get() = current.text + kana.drop(current.kanaCount)
    }

    private data class Committed(
        val keys: String,
        /** What the engine chose before any switch. */
        val decided: Language,
        /** What it went in as (the switch key after the commit changes [language]). */
        val committedAs: Language,
        val language: Language,
        /** Exactly what was inserted: the word plus its boundary character. */
        val text: String,
        val boundary: String,
        val location: Int,
        val previous: Language?,
    ) {
        /** The same word after Delete took back its boundary character. */
        val withoutBoundary get() = copy(text = text.dropLast(boundary.length), boundary = "")
    }

    var keys = ""
        private set
    var policy = AppPolicy()
    var conversion: Conversion? = null
        private set
    private var forced: Language? = null
    /** Whether [forced] is the user's choice (the switch key) rather than "back to kana" after converting
     *  a phrase the engine read as Japanese anyway: only the user's choices are corrections. */
    private var forcedByUser = false
    /** The language the engine itself showed last, before any switch: what a commit compares
     *  against, so a forced reading is never mistaken for the engine's own choice. */
    private var enginePreview: Language? = null
    /** The last `decide` kept a short token (3 keys or fewer, like a command) English only
     *  because the app is set to Strict. Longer words keep the hysteresis: applied to them it
     *  left 8% of Korean words English, against 5% this way, for the same English kept. */
    private var keptEnglishByStrict = false
    /** "henkan" came back from conversion as "henkan'" (the final n stays ん); dropped again
     *  if the word leaves Japanese, so English doesn't get an apostrophe nobody typed. */
    private var addedNMarker = false
    private var previousLanguage: Language? = null
        set(value) {
            field = value
            if (value == null) openBrackets.clear() // a new context
        }
    private var lastCommit: Committed? = null
    /** Language of the current preview, so in-phrase symbols know where they belong. */
    private var previewLanguage: Language? = null
    /** The last key passed to the app was a digit (so "." there is a decimal point). */
    private var afterDigit = false
    /** Digits and symbols the app just typed for us, one after another, and where they start:
     *  a switch key right after them makes them Japanese (123 → １２３, [ → 「), and a Japanese
     *  phrase typed right after digits converts with them (3ji → 3時). */
    private var passedRun: Pair<String, Int>? = null
    /** The word being typed started right after a number, in Japanese text: a counter (3ji → 3時). */
    private var afterNumberInJapanese = false
    /** The language shown when Delete was pressed, held until the next letter: a shorter word
     *  may read differently (한국 → 한구, not 한rn), but it shouldn't change under the user. */
    private var pinned: Language? = null
    /** Where the Korean syllable being typed starts once Delete was used in the word: the system's
     *  2-set input has committed everything before it, so those syllables delete whole and new
     *  jamo don't join them (세요 → 세ㅇ, not 셍). Null until then: the word composes as one. */
    private var syllableStart: Int? = null
    /** Digits after Japanese text, and inside a Japanese phrase, are part of it and full-width,
     *  as addresses in Japan often need. */
    var fullWidthDigits = false
    /** A space after Japanese text is full-width. */
    var fullWidthSpace = false
    private val digitsFullWidth get() = japaneseEnabled && fullWidthDigits && policy.fullWidth
    /** Esc showed the current phrase's kana instead (the next switch changes language as usual). */
    private var liveSuppressed = false
    private var liveCache: Pair<String, ConversionCandidate?>? = null

    /** Splits English words out of a Japanese phrase (google in kyouhagoogledekensaku). */
    var segmenter: JapaneseSegmenter? =
        if (japaneseEnabled) JapaneseModel.shared?.let { JapaneseSegmenter(EnglishModel.shared, it) } else null

    /** A new text field became active: forget the sentence context. */
    fun activate() {
        closeFlipWindow()
        previousLanguage = null
        passedRun = null
    }

    /** Returns true when the key was consumed. */
    fun handle(key: Key, client: TextClient): Boolean {
        if (key == Key.ShiftSpace) {
            // With nothing being typed, the other width: full-width unless Space already types that.
            if (!(keys.isEmpty() && conversion == null && japaneseEnabled && policy.fullWidth)) {
                return handle(Key.Printable(' '), client)
            }
            closeFlipWindow()
            if (fullWidthSpace && previousLanguage == Language.JAPANESE) return false // the app types " "
            client.insertText("　", null)
            return true
        }
        if (conversion != null) return handleConverting(key, client)

        if (!(key is Key.Printable && keys.isEmpty())) afterDigit = false
        if (!(key is Key.Letter || key is Key.Printable || key == Key.Escape || key == Key.Cycle)) passedRun = null
        val pending = lastCommit
        lastCommit = null
        // Esc switches languages where it is a switch key; the others arrive as Cycle.
        val switchesLanguage = key == Key.Cycle || (key == Key.Escape && policy.escapeSwitches)
        if (switchesLanguage && keys.isEmpty() && pending != null) {
            cycle(pending, client)?.let {
                lastCommit = it
                return true
            }
        }
        // Delete right after the commit: the app takes back the space or punctuation, and the
        // word itself can still be changed (Space before looking, then Delete and the switch key).
        if (key == Key.Delete && keys.isEmpty() && pending != null && pending.boundary.isNotEmpty()) {
            lastCommit = pending.withoutBoundary
            return false
        }
        pending?.let(::finalize)
        if (switchesLanguage && keys.isEmpty()) {
            passedRunBeforeCaret(client)?.let { (text, range) ->
                passedRun = null
                startSymbol(text, client, range)
                return true
            }
        }

        when (key) {
            is Key.Letter -> {
                // A symbol on its own is confirmed by the next word, which is judged by itself.
                if (keys.isNotEmpty() && keys.all { it in RomajiComposer.symbols }) commit(client)
                if (keys.isEmpty()) { // a new word
                    lastRecorded = null
                    liveSuppressed = false
                    val cursor = client.selectedRange()
                    afterNumberInJapanese = japaneseEnabled && previousLanguage == Language.JAPANESE
                        && passedRun?.first?.lastOrNull()?.isNumber == true && cursor.location != TextRange.NOT_FOUND
                        && passedRunRange(cursor.location, client) != null
                }
                addedNMarker = false
                pinned = null
                keys += key.ch
                updatePreview(client)
                return true
            }

            Key.Delete -> {
                if (keys.isEmpty()) return false
                val shown = forced ?: pinned ?: previewLanguage
                if (forced == null) pinned = shown
                when (shown) {
                    Language.JAPANESE -> deleteKana()
                    Language.KOREAN -> deleteJamoOrSyllable()
                    else -> keys = keys.dropLast(1)
                }
                addedNMarker = false
                if (keys.isEmpty()) { forced = null; forcedByUser = false; pinned = null; syllableStart = null }
                updatePreview(client)
                return true
            }

            Key.Escape, Key.Cycle -> {
                // The switch key is the keyboard's own: with nothing to change it goes nowhere.
                if (keys.isEmpty()) return key == Key.Cycle
                // A live-converted phrase goes back to its kana first, also with an Esc that isn't a
                // switch key, as in macOS's live conversion.
                if (isLive && (forced ?: previewLanguage) == Language.JAPANESE) {
                    liveSuppressed = true
                    updatePreview(client)
                    return true
                }
                if (!switchesLanguage) {
                    // The app's Esc: the word goes in as shown, then the app gets the key.
                    commit(client)
                    return false
                }
                // Cycle from what the user sees.
                val shown = forced ?: pinned ?: previewLanguage ?: decide(partial = true).language
                val next = shown.next(keys, japaneseEnabled, engineSplits(keys))
                forced = next
                pinned = null
                forcedByUser = true
                if (next != Language.JAPANESE) dropNMarker()
                updatePreview(client)
                return true
            }

            is Key.Printable -> {
                val ch = key.ch
                if (keys.isEmpty()) {
                    val wasAfterDigit = afterDigit
                    afterDigit = ch.isNumber
                    if (previousLanguage == Language.JAPANESE) {
                        if (ch == ' ' && fullWidthSpace && japaneseEnabled && policy.fullWidth) {
                            client.insertText("　", null)
                            return true
                        }
                        // A number after Japanese text starts a Japanese word: １２３, ３がつ → ３月.
                        if (ch.isAsciiDigit && digitsFullWidth) {
                            startSymbol(ch.toString(), client)
                            return true
                        }
                    }
                    // After Japanese text a symbol is Japanese too (、。？！「」・…), underlined until Return
                    // or the next word confirms it, as in a Japanese IME; Space shows its other forms.
                    // Not inside a number (3.5, 10:30), but a bracket closes the way it was opened.
                    if (ch in RomajiComposer.symbols && ((previousLanguage == Language.JAPANESE && !wasAfterDigit) || closesFullWidth(ch))) {
                        startSymbol(ch.toString(), client)
                        return true
                    }
                    notePassed(ch, client)
                    return false
                }
                if (japaneseEnabled && belongsToJapanesePhrase(ch)) {
                    keys += ch
                    // Digits don't read as anything: the phrase stays Japanese, as it was shown.
                    if (ch.isNumber && forced == null) { forced = Language.JAPANESE; forcedByUser = false }
                    updatePreview(client)
                    return true
                }
                if (ch == ' ' && currentLanguage() == Language.JAPANESE && converter != null) {
                    startConversion(client)
                    return true
                }
                if (closesFullWidth(ch)) { // 日本語（English）
                    commit(client)
                    startSymbol(ch.toString(), client)
                    return true
                }
                val marked = client.markedRange()
                val location = if (marked.location != TextRange.NOT_FOUND) marked.location else client.selectedRange().location
                val previous = previousLanguage
                passedRun = null
                val word = finishWord()
                // Only the word goes in; the app types the space or punctuation itself, as with the
                // system's Korean input.
                client.insertText(word.text, null)
                lastCommit = Committed(word.keys, word.decided, word.language, word.language, word.text + ch, ch.toString(),
                                       location, previous)
                return false
            }

            Key.Enter -> {
                if (keys.isEmpty()) return false
                // Like a Japanese IME, Return on an underlined word only confirms it; the next Return
                // is the app's. A Japanese phrase is only confirmed either way.
                val japanese = currentLanguage() == Language.JAPANESE
                commit(client)
                return returnConfirmsOnly || japanese
            }

            Key.Katakana, Key.Hiragana -> {
                // Only for a Japanese phrase; otherwise they stay the app's.
                if (!(keys.isNotEmpty() && japaneseEnabled && (forced == Language.JAPANESE || previewLanguage == Language.JAPANESE))) {
                    commit(client)
                    return false
                }
                showKanaForms(key == Key.Katakana, client = client)
                return true
            }

            Key.Up, Key.Down, Key.Left, Key.Right, Key.Other -> {
                commit(client)
                previousLanguage = null
                return false
            }

            Key.ShiftSpace -> return false // handled above
        }
    }

    /** Commits whatever is in progress as-is (focus change, shortcut, deactivation). */
    fun commit(client: TextClient?) {
        closeFlipWindow()
        conversion?.let { conversion ->
            client?.insertText(conversion.display, null)
            noteBrackets(conversion.display)
            converter?.learn(conversion.current, conversion.kana)
            this.conversion = null
            keys = ""
            previousLanguage = Language.JAPANESE
            return
        }
        if (keys.isEmpty()) return
        val word = finishWord()
        client?.insertText(word.text, null)
    }

    /** Commits what's in progress after the caret was moved elsewhere (a tap; the app asks):
     *  the next word doesn't continue the previous sentence. */
    fun commitForNewContext(client: TextClient?) {
        commit(client)
        previousLanguage = null
    }

    /** The candidates for the Japanese phrase being typed, before Space, for a
     *  candidate bar that shows them as you type, like the system's Japanese keyboard. */
    val typingCandidates: List<ConversionCandidate>
        get() {
            if (conversion != null || converter == null || keys.isEmpty() ||
                !(forced == Language.JAPANESE || (forced == null && previewLanguage == Language.JAPANESE))) return emptyList()
            val kana = japaneseReading(partial = false)
            return if (kana.isEmpty()) emptyList() else conversionCandidates(kana)
        }

    /** Picks one of [typingCandidates], as Space then a tap on it would. */
    fun choose(candidate: ConversionCandidate, client: TextClient) {
        startConversion(client)
        val index = conversion?.candidates?.indexOf(candidate)?.takeIf { it >= 0 } ?: return
        choose(index, client)
    }

    /** Commits candidate [index] of the open conversion (e.g. tapped in the candidate bar). */
    fun choose(index: Int, client: TextClient) {
        val conversion = conversion ?: return
        if (index !in conversion.candidates.indices) return
        this.conversion = conversion.copy(selected = index)
        commitCandidate(client)
    }

    // Japanese conversion

    /** A symbol typed as its own Japanese word, shown full-width (or as the 「」・ a Japanese IME types).
     *  With [range], it takes the place of what the app typed there, in one step: an empty
     *  insertText to delete it first is dropped on the way to the app (1 became 1１). */
    private fun startSymbol(symbol: String, client: TextClient, range: TextRange? = null) {
        lastRecorded = null
        liveSuppressed = false
        keys = symbol
        forced = Language.JAPANESE
        forcedByUser = false
        if (range == null) return updatePreview(client)
        previewLanguage = Language.JAPANESE
        client.setMarkedText(japaneseDisplay(), range)
    }

    /** Keeps [passedRun] up to date with a key the app is about to type. */
    private fun notePassed(ch: Char, client: TextClient) {
        val cursor = client.selectedRange()
        if (!(japaneseEnabled && policy.fullWidth && ch.isAscii && (ch.isNumber || ch in RomajiComposer.symbols) &&
              cursor.location != TextRange.NOT_FOUND && cursor.length == 0)) {
            passedRun = null
            return
        }
        val run = passedRun
        passedRun = if (run != null && run.second + run.first.length == cursor.location) {
            (run.first + ch) to run.second
        } else {
            ch.toString() to cursor.location
        }
    }

    /** The range of [passedRun] when it is still in the text, unchanged, ending at [end]. */
    private fun passedRunRange(end: Int, client: TextClient): TextRange? {
        val (text, location) = passedRun ?: return null
        val range = TextRange(location, text.length)
        return if (range.end == end && client.text(range) == text) range else null
    }

    /** The digits and symbols just typed, and where they are, when the caret is right after them. */
    private fun passedRunBeforeCaret(client: TextClient): Pair<String, TextRange>? {
        val cursor = client.selectedRange()
        val run = passedRun ?: return null
        if (!japaneseEnabled || conversion != null || cursor.location == TextRange.NOT_FOUND || cursor.length != 0) return null
        val range = passedRunRange(cursor.location, client) ?: return null
        return run.first to range
    }

    /** Full-width openers not closed yet in this sentence, so ) ] } close them full-width
     *  after an English word too. */
    private val openBrackets = ArrayList<Char>()

    private fun closesFullWidth(ch: Char): Boolean {
        val opener = closing[ch] ?: return false
        return openBrackets.lastOrNull() == opener
    }

    /** Keeps [openBrackets] up to date with text going in. */
    private fun noteBrackets(text: String) {
        for (ch in text) {
            if (ch in pairs.values) {
                openBrackets.add(ch)
            } else {
                val opener = pairs[ch]
                if (opener != null && openBrackets.lastOrNull() == opener) openBrackets.removeAt(openBrackets.size - 1)
            }
        }
    }

    /** Whether a symbol typed after the current word continues a Japanese phrase.
     *  Sentence punctuation (、。？！) never changes the word's language: it joins only a
     *  word already read as Japanese ("hello." and "dho?" = 왜? stay what they were).
     *  The in-word marks ー and ' may also turn an English-looking start into Japanese
     *  ("ra" → "ra-" → らー), but never take a word away from Korean. */
    private fun belongsToJapanesePhrase(ch: Char): Boolean {
        if (ch.isAsciiDigit) {
            if (!digitsFullWidth) return false
            return forced == Language.JAPANESE || (forced == null && previewLanguage == Language.JAPANESE
                                                   && RomajiComposer.compose(keys, partial = true).pending in listOf("", "n"))
        }
        if (ch !in JapaneseModel.inPhraseSymbols) return false
        if (forced == Language.JAPANESE) return true
        // On the way to a Japanese text replacement shortcut with a symbol in it: gu- is ぐー,
        // even though gu alone read as 혀.
        if (japaneseEnabled && forced == null) {
            val kana = RomajiComposer.compose(keys + ch, partial = true)
            if (kana.isClean && replacements.keys.any { it.startsWith(kana.text) }) return true
        }
        // A letter still waiting for its vowel says the word may not be Japanese after all
        // (baz) is baz), not ばz）); a final n is ん (nihon. is にほん。).
        if (previewLanguage == Language.JAPANESE) {
            return ch == '-' || ch == '\'' || RomajiComposer.compose(keys, partial = true).pending in listOf("", "n")
        }
        // An English word before the mark keeps it English (to-do, co-op); ra-men isn't one.
        if (!(forced == null && (ch == '-' || ch == '\'') && previewLanguage == Language.ENGLISH &&
              !EnglishModel.shared.isKnownWord(keys) && RomajiComposer.compose(keys, partial = true).strayLetters == 0)) return false
        val context = DecisionContext(previousLanguage, partial = true)
        return engine.decide(keys + ch, context).language == Language.JAPANESE
    }

    /** The user's text replacements (shortcut → phrases; a shortcut may be there more than once).
     *  A word whose reading is a shortcut shows that way, so the app replaces it on Space as it
     *  would with the system's input; a Japanese one also offers every phrase as the first candidates. */
    var replacements: Map<String, List<String>> = emptyMap()

    /** The Korean or Japanese reading of the keys that is a text replacement shortcut (주소, ㅈㅅ,
     *  よろ), unless the user taught the engine otherwise for these keys. English shortcuts are left
     *  to the engine: "go" may be one, and still be ご in a Japanese sentence. */
    private fun shortcutReading(partial: Boolean): Pair<Language, String>? {
        if (replacements.isEmpty() || keys.isEmpty() || engine.learned[keys] != null) return null
        hangul()?.let { if (it in replacements) return Language.KOREAN to it }
        if (japaneseEnabled) {
            val kana = RomajiComposer.compose(keys, partial)
            if (kana.isClean && kana.text in replacements) return Language.JAPANESE to kana.text
        }
        return null
    }

    /** Kana for the keys, with any English words inside the phrase kept as typed. */
    private fun japaneseReading(partial: Boolean): String {
        // An English word with a particle (redde → redで) even when every key also makes kana,
        // as the engine split it; a reading the user picked by hand stays kana (れっで).
        if (!(forced == Language.JAPANESE && forcedByUser)) {
            segmenter?.englishWithParticle(keys)?.let { (word, particle) -> return word + particle }
        }
        if (!partial) {
            // Letters still waiting for their vowel when the phrase ends go in as shown
            // (watashihag → わたしはg, not わたしhag); only n becomes ん.
            val typed = RomajiComposer.compose(keys, partial = true)
            if (typed.strayLetters == 0 && typed.pending.isNotEmpty()) return RomajiComposer.compose(keys).text
            segmenter?.segment(keys)?.let { return JapaneseSegmenter.text(it) }
        }
        // While typing, a consonant at the end is just waiting for its vowel (watashihag → わたしはg),
        // not a stray letter to split an English word ("hag") out of.
        if (partial && RomajiComposer.compose(keys, partial = true).strayLetters > 0) {
            segmenter?.segment(keys)?.let { segments ->
                // While typing, the last segment may still be growing; keep its pending letters visible.
                val last = segments.last()
                return JapaneseSegmenter.text(segments.dropLast(1)) +
                    if (last is JapaneseSegmenter.Segment.Japanese) RomajiComposer.compose(last.keys, partial = true).text
                    else JapaneseSegmenter.text(listOf(last))
            }
        }
        return RomajiComposer.compose(keys, partial).text
    }

    // Live conversion

    private val isLive get() = liveConversion && !liveSuppressed && converter != null

    /** The top candidate covering all of [kana] (cached: the preview asks on every key). */
    private fun liveCandidate(kana: String): ConversionCandidate? {
        liveCache?.let { (cachedKana, candidate) -> if (cachedKana == kana) return candidate }
        var candidate = conversionCandidates(kana).firstOrNull { it.kanaCount == kana.length && !it.isPrediction }
        // A short reading only turns into kanji (私, 今日), not into katakana mid-word
        // (と → ト, こんに → コンに): those flicker while the phrase is still being typed.
        if (kana.length <= 3 && candidate != null && candidate.text.none { it.code in 0x4E00..0x9FFF }) candidate = null
        liveCache = kana to candidate
        return candidate
    }

    /** The Japanese phrase as shown while typing: kana, or with live conversion the top
     *  candidate followed by letters still waiting for their vowel (今日はいい天気でs). */
    private fun japaneseDisplay(): String {
        val reading = japaneseReading(partial = true)
        if (!isLive) return reading
        val pending = RomajiComposer.compose(keys, partial = true).pending
        if (!reading.endsWith(pending)) return reading
        val kana = reading.dropLast(pending.length)
        if (kana.isEmpty()) return reading
        val candidate = liveCandidate(kana) ?: return reading
        return candidate.text + pending
    }

    /** What a Japanese phrase commits as without the candidate window: the kana, or with
     *  live conversion the candidate that was on screen (which the converter learns). */
    private fun committedJapanese(): String {
        val reading = japaneseReading(partial = false)
        // Letters still waiting for their vowel follow the candidate, as japaneseDisplay shows
        // them (今日はいい天気でs, not a candidate for the whole reading with the s in it).
        val pending = RomajiComposer.compose(keys, partial = true).pending
        val kana = if (reading.endsWith(pending)) reading.dropLast(pending.length) else reading
        val letters = reading.drop(kana.length)
        val converter = converter
        if (!isLive || converter == null || kana.isEmpty()) return reading
        val candidate = liveCandidate(kana) ?: return reading
        converter.learn(candidate, kana)
        return candidate.text + letters
    }

    private fun startConversion(client: TextClient) {
        var kana = japaneseReading(partial = false)
        if (converter == null || kana.isEmpty()) return
        val userForced = forced == Language.JAPANESE && forcedByUser
        if (userForced) record(keys, Language.JAPANESE, committedLanguage())
        // A number the app typed just before the phrase is part of it: 3ji is 3時, not 3 and 時
        // picked out of 地, 字, 自…
        val marked = client.markedRange()
        var prefixRange: TextRange? = null
        val run = passedRun
        if (marked.location != TextRange.NOT_FOUND && run != null && run.first.lastOrNull()?.isNumber == true &&
            run.first.all { it in numberCharacters }) {
            passedRunRange(marked.location, client)?.let { range ->
                prefixRange = TextRange(range.location, range.length + marked.length)
                kana = run.first + kana
            }
        }
        val numberPrefix = if (prefixRange == null) "" else run!!.first
        passedRun = null
        var candidates = conversionCandidates(kana)
        if (numberPrefix.isNotEmpty()) {
            // The number as it was typed (3時) before other widths and spellings (３時, 三時).
            candidates = candidates.filter { it.text.startsWith(numberPrefix) } + candidates.filter { !it.text.startsWith(numberPrefix) }
        }
        // Live conversion showed the phrase without the number, so no candidate is on screen yet.
        val live = if (isLive && numberPrefix.isEmpty()) liveCandidate(kana) else null
        val phrases = replacements[kana]
        if (!phrases.isNullOrEmpty()) {
            // Text replacements come first, or right after the live candidate already on screen.
            val at = live?.let { candidates.indexOf(it).takeIf { i -> i >= 0 } }?.let { it + 1 } ?: 0
            val added = phrases.map { ConversionCandidate(it, kana.length) }
            candidates = candidates.subList(0, at).filter { it.text !in phrases } + added +
                candidates.subList(at, candidates.size).filter { it.text !in phrases }
        }
        if (candidates.isEmpty()) {
            commit(client)
            return
        }
        // With live conversion the top candidate is already on screen: Space shows the next one.
        var selected = 0
        val i = live?.let { candidates.indexOf(it) } ?: -1
        if (i >= 0 && candidates.size > 1) selected = (i + 1) % candidates.size
        liveSuppressed = false
        val conversion = Conversion(kana, keys, candidates, selected, userForced, numberPrefix)
        this.conversion = conversion
        keys = ""
        forced = null
        forcedByUser = false
        pinned = null
        syllableStart = null
        enginePreview = null
        previousLanguage = Language.JAPANESE
        val range = prefixRange
        if (range != null) {
            client.setMarkedText(conversion.display, range) // the number, then the phrase
        } else {
            client.setMarkedText(conversion.display)
        }
    }

    /** Shows the reading as katakana (or hiragana) as the selected "candidate": Return
     *  commits it, Space flips to the other form, typing on commits it and continues. */
    private fun showKanaForms(katakanaFirst: Boolean, kana: String? = null, romaji: String? = null, client: TextClient) {
        val reading = kana ?: japaneseReading(partial = false)
        val forms = listOf(ConversionCandidate(RomajiComposer.katakana(reading), reading.length),
                           ConversionCandidate(reading, reading.length))
        val conversion = Conversion(reading, romaji ?: keys, if (katakanaFirst) forms else forms.reversed(), 0)
        this.conversion = conversion
        keys = ""
        forced = null
        forcedByUser = false
        pinned = null
        syllableStart = null
        enginePreview = null
        previousLanguage = Language.JAPANESE
        client.setMarkedText(conversion.display)
    }

    /** The conversion with its first clause [step] kana shorter or longer (今日は|いいてんき):
     *  candidates for exactly that much of the reading, the rest shown as kana. A boundary
     *  never falls inside an English word (google). Null when it can't move further. */
    private fun resizeFirstClause(conversion: Conversion, step: Int): Conversion? {
        if (converter == null) return null
        val reading = conversion.kana
        var length = conversion.current.kanaCount + step
        while (length in 1 until reading.length &&
               reading[length - 1].isAsciiLetter && reading[length].isAsciiLetter) {
            length += step
        }
        if (length !in 1..reading.length || length == conversion.current.kanaCount) return null
        val clause = reading.substring(0, length)
        val candidates = conversionCandidates(clause).filter { it.kanaCount == clause.length }
        if (candidates.isEmpty()) return null
        return Conversion(conversion.kana, conversion.keys, candidates, 0, conversion.userForced, conversion.numberPrefix)
    }

    private fun handleConverting(key: Key, client: TextClient): Boolean {
        val conversion = conversion ?: return false
        when {
            key == Key.Printable(' ') || key == Key.Down || key == Key.ShiftSpace -> {
                val next = conversion.copy(selected = (conversion.selected + 1) % conversion.candidates.size)
                this.conversion = next
                client.setMarkedText(next.display)
                return true
            }

            key == Key.Up -> {
                val previous = conversion.copy(selected = (conversion.selected + conversion.candidates.size - 1) % conversion.candidates.size)
                this.conversion = previous
                client.setMarkedText(previous.display)
                return true
            }

            key == Key.Left || key == Key.Right -> {
                resizeFirstClause(conversion, if (key == Key.Left) -1 else 1)?.let { resized ->
                    this.conversion = resized
                    client.setMarkedText(resized.display)
                }
                return true
            }

            key is Key.Printable && key.ch.isNumber -> {
                val n = Character.getNumericValue(key.ch)
                if (n >= 1 && n <= minOf(9, conversion.candidates.size)) {
                    choose(n - 1, client)
                    return true
                }
                commit(client)
                return handle(key, client)
            }

            key == Key.Enter -> {
                commitCandidate(client)
                return true
            }

            key == Key.Katakana || key == Key.Hiragana -> {
                this.conversion = null
                showKanaForms(key == Key.Katakana, conversion.kana, conversion.keys, client)
                return true
            }

            key == Key.Escape || key == Key.Cycle || key == Key.Delete -> {
                this.conversion = null
                liveSuppressed = true // "back to kana" means kana, also with live conversion
                // A number taken in from before the phrase goes back as it was typed.
                if (conversion.numberPrefix.isNotEmpty()) client.insertText(conversion.numberPrefix, null)
                val kana = conversion.kana.drop(conversion.numberPrefix.length)
                // Back to editing the romaji — when it still spells the reading. After part of a
                // phrase with English in it was committed it may not; keep the kana rather than lose it.
                if (JapaneseSegmenter.reading(conversion.keys, segmenter) != kana) {
                    client.insertText(kana, null)
                    previousLanguage = Language.JAPANESE
                    return true
                }
                // A final "n" was shown as ん; keep it ("henkan" → "henkan'") instead of
                // letting it wait for a vowel again.
                keys = conversion.keys
                syllableStart = null
                if (keys.endsWith("n") && !keys.endsWith("nn")) { keys += "'"; addedNMarker = true }
                forced = Language.JAPANESE
                forcedByUser = conversion.userForced
                updatePreview(client)
                return true
            }

            key is Key.Letter || key is Key.Printable -> {
                commit(client)
                return handle(key, client)
            }

            else -> { // Other
                commit(client)
                return false
            }
        }
    }

    /** Commits the selected candidate; if it covers only the start of the reading,
     *  keeps converting the rest. */
    private fun commitCandidate(client: TextClient) {
        val conversion = conversion ?: return
        val converter = converter ?: return
        val candidate = conversion.current
        converter.learn(candidate, conversion.kana)
        client.insertText(candidate.text, null)
        noteBrackets(candidate.text)
        val rest = conversion.kana.drop(candidate.kanaCount)
        this.conversion = null
        if (rest.isEmpty()) return
        val restKeys = keys(conversion.keys, maxOf(0, candidate.kanaCount - conversion.numberPrefix.length))
        val candidates = conversionCandidates(rest)
        if (candidates.isEmpty()) {
            client.insertText(rest, null)
        } else {
            // The rest is the engine's Japanese, not something the user corrected.
            val next = Conversion(rest, restKeys, candidates, 0)
            this.conversion = next
            client.setMarkedText(next.display)
        }
    }

    /** The converter's candidates, with a number kept as typed: a reading that is only a number
     *  offers itself (１２３) and its half-width form (123) first; otherwise the candidates that keep
     *  the digits come before those that spell them out (３月, 3月, then 三月; 東京都１, not 東京都一). */
    private fun conversionCandidates(kana: String): List<ConversionCandidate> {
        val candidates = converter?.candidates(kana) ?: emptyList()
        val digits = digits(kana)
        if (digits.isEmpty()) return candidates
        val whole = candidates.filter { it.kanaCount == kana.length }
        var ordered = whole.filter { digits(it.text) == digits } + whole.filter { digits(it.text) != digits }
        if (kana.all { it in numberCharacters }) {
            ordered = listOf(ConversionCandidate(kana, kana.length), ConversionCandidate(halfWidth(kana), kana.length)) + ordered
        }
        val shown = HashSet<String>()
        return (ordered + candidates.filter { it.kanaCount != kana.length }).filter { shown.add(it.text) }
    }

    // Private

    private fun decide(partial: Boolean): Decision {
        shortcutReading(partial)?.let { (language, text) ->
            // As sure as a split word, so a Strict app shows it too.
            keptEnglishByStrict = false
            val decision = Decision(language, SplittingEngine.CONFIDENCE, text, "text replacement shortcut")
            lastTrace = Trace(keys, previousLanguage, decision, partial)
            return decision
        }
        // A word typed straight after a number in Japanese text is a counter when it is romaji
        // (3ji, 5fun, 3hon); a unit like km or pm has letters that aren't (5km stays English).
        if (afterNumberInJapanese && keys.all { it.isLetter() } && RomajiComposer.compose(keys, partial).strayLetters == 0) {
            keptEnglishByStrict = false
            val decision = Decision(Language.JAPANESE, SplittingEngine.CONFIDENCE, RomajiComposer.compose(keys, partial).text,
                                    "counter after a number")
            lastTrace = Trace(keys, previousLanguage, decision, partial)
            return decision
        }
        var d = engine.decide(keys, DecisionContext(previousLanguage, partial))
        // A word learned as Japanese while Japanese was on: with it off, the engine without what
        // was learned decides it (sushi stays sushi, not すし). Turned on again, it is learned again.
        if (d.language == Language.JAPANESE && !japaneseEnabled) {
            d = engine.base.decide(keys, DecisionContext(previousLanguage, partial))
        }
        // While typing, a consonant at the end may still be waiting for its vowel, which keeps
        // Japanese close and a Korean word under a Strict app's threshold until the word ends
        // (qud: 0.60 while typed, 0.98 at Space). The word as it would go in now decides, so the
        // preview shows what Space commits (병), not English that turns Korean on Space.
        if (partial && d.language == Language.KOREAN && d.confidence < policy.conversionThreshold) {
            val now = engine.decide(keys, DecisionContext(previousLanguage, partial = false))
            if (now.language == Language.KOREAN && now.confidence >= policy.conversionThreshold) d = now
        }
        val kept = d.language != Language.ENGLISH && d.confidence < policy.conversionThreshold
        keptEnglishByStrict = kept && policy.conversionThreshold >= AppPolicy.STRICT_THRESHOLD && keys.length <= 3
        val decision = if (kept) {
            Decision(Language.ENGLISH, 1 - d.confidence, keys,
                     String.format(Locale.ROOT, "kept English (%.2f < %.2f); ", d.confidence, policy.conversionThreshold) + d.explanation)
        } else d
        lastTrace = Trace(keys, previousLanguage, decision, partial)
        return decision
    }

    /** The last decision and what it was made from, for a typing test. */
    data class Trace(
        val keys: String,
        val previous: Language?,
        val decision: Decision,
        /** Still being typed (the preview) rather than committed. */
        val partial: Boolean,
    )

    var lastTrace: Trace? = null
        private set

    /** The language the current word will be committed in. */
    private fun currentLanguage(): Language = forced ?: pinned ?: committedLanguage()

    /** What Space/Return commits without the switch key: the full-word decision, except that a hesitant
     *  change away from the reading on screen doesn't win (unless Strict kept it English). The
     *  same rule as the preview, so what you see is what you get. */
    private fun committedLanguage(): Language {
        val d = decide(partial = false)
        val shown = enginePreview
        if (shown == null || shown == d.language) return d.language
        // Kanji on screen (live conversion): Space converts them, unless the keys are an English
        // word. A short word reads as the start of a longer Japanese one while typed and as
        // English once it ends (zi showed 地, then went in as "zi"). Kana alone keep the usual rule.
        if (shown == Language.JAPANESE && d.language == Language.ENGLISH && isLive && !keptEnglishByStrict &&
            !EnglishModel.shared.isKnownWord(keys) && RomajiComposer.compose(keys).isClean &&
            liveCandidate(japaneseReading(partial = false)) != null) {
            return shown
        }
        return if (isWeakFlipAllowed(d, shown)) d.language else shown
    }

    /** Whether [decision] may replace the reading on screen: when it's confident, when it's
     *  a dictionary English word (which is what Space/Return will commit), or when a Strict app
     *  kept a short token English: there the typed letters win over a reading shown along the
     *  way (gs showed ㅎㄴ, but gst stays gst). Only a close second is held: Th showed てぇ, and
     *  The (Korean against English, kana far behind) went in as 手ェ. */
    private fun isWeakFlipAllowed(decision: Decision, shown: Language): Boolean =
        decision.confidence >= PREVIEW_FLIP_CONFIDENCE
            || (decision.runnerUp != null && decision.runnerUp != shown)
            || (decision.language == Language.ENGLISH && (keptEnglishByStrict || EnglishModel.shared.isKnownWord(keys)))
            || preview(shown) == null

    private fun updatePreview(client: TextClient) {
        val forcedOrPinned = forced ?: pinned
        if (keys.isEmpty()) {
            previewLanguage = null
            enginePreview = null
            client.setMarkedText("")
        } else if (forcedOrPinned != null) {
            previewLanguage = forcedOrPinned
            show(when (forcedOrPinned) {
                Language.JAPANESE -> japaneseDisplay()
                Language.KOREAN -> hangul() ?: keys
                else -> engine.render(keys, forcedOrPinned)
            }, client)
        } else {
            splitOffKoreanBeforeOther(client)
            splitOffJapaneseBeforeKorean(client)
            val decision = decide(partial = true)
            var language = decision.language
            // Hysteresis: a weak flip on one keystroke (わた → "wat" → わたし) isn't shown,
            // except into a dictionary English word, which is what Space/Return will commit.
            val shown = previewLanguage
            if (shown != null && shown != language && keys.length > 1 && !isWeakFlipAllowed(decision, shown)) {
                language = shown
            }
            previewLanguage = language
            enginePreview = language
            show(if (language == Language.JAPANESE) japaneseDisplay() else preview(language) ?: keys, client)
        }
    }

    /** The word being composed as the marked text. */
    private fun show(text: String, client: TextClient) {
        client.setMarkedText(text)
    }

    /** Korean with another language typed straight after it reads as broken Hangul: 이력 then
     *  rireki (dlfurrireki, 이력갹다ㅑ), 오늘은 then google에서 (dhsmfdmsgoogledptj, 오늘은해ㅐ힏에서).
     *  When the keys only make Hangul with stray jamo, but a front of two or more ordinary
     *  syllables is followed by a known English word (or one with a particle) or by romaji the
     *  engine is sure is Japanese, the Korean goes in as typed and the rest carries on as its own
     *  word (live conversion and Space for candidates when it's Japanese: 이력履歴). */
    private fun splitOffKoreanBeforeOther(client: TextClient) {
        if (keys.length < 7 || HangulComposer.compose(keys)?.isBroken != true ||
            // Only what reads as Korean on its own: a Japanese phrase whose start happens to
            // spell Hangul (chanwo…, 초무をころした) stays whole. Judged without context, since
            // right after Japanese everything leans Japanese.
            engine.decide(keys, DecisionContext(partial = true)).language != Language.KOREAN) return
        for (cut in 4..keys.length - 3) {
            val front = keys.substring(0, cut)
            val back = keys.substring(cut)
            val korean = HangulComposer.compose(front) ?: continue
            if (korean.strayJamo.isNotEmpty() || korean.syllables.size < 2 ||
                !korean.syllables.all { koreanModel.syllableLogProbability(it) > KoreanModel.UNKNOWN_SYLLABLE_LOG_P } ||
                !isOtherLanguageWord(back)) continue
            client.insertText(korean.text, null)
            keys = back
            syllableStart = null
            previousLanguage = Language.KOREAN
            previewLanguage = null
            enginePreview = null
            return
        }
    }

    /** A known English word, one with a Korean particle (google에서), or romaji the engine is
     *  sure is Japanese (no word starts with っ or ん: that cut takes a letter from the Korean). */
    private fun isOtherLanguageWord(keys: String): Boolean {
        val decision = engine.decide(keys, DecisionContext(Language.KOREAN, partial = true))
        return when (decision.language) {
            Language.ENGLISH -> keys.length >= 3 && EnglishModel.shared.isKnownWord(keys)
            Language.MIXED -> true
            Language.JAPANESE -> {
                if (!japaneseEnabled || decision.confidence < 0.9 || keys.length < 4) return false
                val romaji = RomajiComposer.compose(keys, partial = true)
                romaji.strayLetters == 0 && romaji.text.firstOrNull().let { it != null && it != 'っ' && it != 'ん' }
            }
            Language.KOREAN -> false
        }
    }

    /** Japanese with Korean typed straight after it reads as broken Hangul: a particle
     *  (rirekifmf, 履歴를, not 갹다ㅑ를) or a word (kyouhaehtl…, 今日は도시락은). Once the Korean
     *  part is a particle or two ordinary syllables, the Japanese goes in (kanji with live
     *  conversion) and the rest carries on as Korean, so 를요, 에서 or 도시락은 can follow. */
    private fun splitOffJapaneseBeforeKorean(client: TextClient) {
        if (!japaneseEnabled || keys.length < 5 || !(HangulComposer.compose(keys)?.isBroken ?: true) ||
            // Keys that still spell romaji are more Japanese (kyouhago is きょうはご, not きょうは해).
            RomajiComposer.compose(keys, partial = true).strayLetters <= 0) return
        // The longest Japanese front first: きょうは|도시, not きょう|하ㄷ….
        for (cut in keys.length - 1 downTo 3) {
            val front = keys.substring(0, cut)
            val back = keys.substring(cut)
            if (!DubeolsikLayout.startsSyllable(back[0])) continue
            val korean = HangulComposer.compose(back) ?: continue
            if (korean.strayJamo.isNotEmpty()) continue
            if (!(korean.text in MixedWord.suffixes ||
                  (korean.syllables.size >= 2 &&
                   korean.syllables.all { koreanModel.syllableLogProbability(it) > KoreanModel.UNKNOWN_SYLLABLE_LOG_P }))) continue
            if (!RomajiComposer.compose(front).isClean) continue
            // Not an English word with a particle (linderman isn't linで그무).
            if (segmenter?.englishWithParticle(front) != null) continue
            val decision = engine.decide(front, DecisionContext(previousLanguage))
            if (decision.language != Language.JAPANESE || decision.confidence < 0.9) continue
            keys = front
            client.insertText(committedJapanese(), null)
            keys = back
            syllableStart = null
            previousLanguage = Language.JAPANESE
            forced = Language.KOREAN
            forcedByUser = false
            previewLanguage = null
            enginePreview = null
            return
        }
    }

    /** The word so far rendered in [language], or null when it has no such reading. */
    private fun preview(language: Language): String? = when (language) {
        Language.ENGLISH -> keys
        Language.KOREAN -> hangul()
        Language.MIXED -> engine.render(keys, Language.MIXED).takeIf { it != keys }
        Language.JAPANESE -> if (japaneseEnabled) japaneseReading(partial = true) else null
    }

    private class Word(val keys: String, val text: String, val language: Language, val decided: Language)

    /** Decides the current word, learns a switch-key override, and resets the buffer. */
    private fun finishWord(): Word {
        val engineChoice = committedLanguage()
        val language = forced ?: pinned ?: engineChoice
        if (language != Language.JAPANESE) dropNMarker()
        // "Back to kana" without the user's switch, or a reading kept while deleting, isn't a
        // correction: report it as the engine's.
        val decided = if ((forced != null && !forcedByUser) || (forced == null && pinned != null)) language else engineChoice
        if (forced != null && forcedByUser) record(keys, language, decided)
        val text = when (language) {
            Language.JAPANESE -> committedJapanese()
            Language.KOREAN -> hangul() ?: engine.render(keys, language)
            else -> engine.render(keys, language)
        }
        liveSuppressed = false
        noteBrackets(text)
        val word = Word(keys, text, language, decided)
        previousLanguage = language.sentenceLanguage
        keys = ""
        forced = null
        forcedByUser = false
        pinned = null
        syllableStart = null
        addedNMarker = false
        previewLanguage = null
        enginePreview = null
        return word
    }

    /** Takes the last kana off a Japanese phrase, as a Japanese IME does (かんこく → かんこ, not
     *  かんこk): the keys become a spelling of the shorter reading, keeping as much of what was
     *  typed as composes to it. A letter still waiting for its vowel goes alone (かんこk → かんこ),
     *  and so does anything in a phrase with English in it. */
    private fun deleteKana() {
        val composed = RomajiComposer.compose(keys, partial = true)
        if (composed.pending.isNotEmpty() || composed.strayLetters != 0 || composed.text.isEmpty()) {
            keys = keys.dropLast(1)
            return
        }
        keys = keys(spelling = composed.text.dropLast(1), from = keys) ?: keys.dropLast(1)
    }

    /** Korean as the system's 2-set input deletes it: jamo by jamo in the syllable being typed
     *  (한국 → 한구 → 한ㄱ → 한), then whole syllables, which it had already committed (한 → nothing). */
    private fun deleteJamoOrSyllable() {
        if (syllableStart == null) {
            val units = HangulComposer.compose(keys)?.units ?: run { keys = keys.dropLast(1); return }
            syllableStart = cut(keys, units.dropLast(1))
        }
        if (syllableStart!! < keys.length) {
            keys = keys.dropLast(1)
            return
        }
        // Nothing left being typed: a whole committed syllable goes.
        val units = HangulComposer.compose(keys)?.units ?: run { keys = keys.dropLast(1); return }
        val cut = cut(keys, units.dropLast(1))
        keys = keys.substring(0, cut)
        syllableStart = cut
    }

    /** The word as Hangul, with the syllable being typed kept apart from those before it
     *  after a Delete ([syllableStart]). */
    private fun hangul(): String? {
        val start = syllableStart
        if (start == null || start >= keys.length) return HangulComposer.compose(keys)?.text
        val front = HangulComposer.compose(keys.substring(0, start)) ?: return null
        val back = HangulComposer.compose(keys.substring(start)) ?: return null
        return front.text + back.text
    }

    private fun dropNMarker() {
        if (addedNMarker && keys.endsWith("'")) keys = keys.dropLast(1)
        addedNMarker = false
    }

    /** The last correction handed to the learning engine, for the word being typed or just
     *  committed: going back and forth (Space, switch, Space while converting) is one correction. */
    private var lastRecorded: Pair<String, Language>? = null

    private fun record(keys: String, language: Language, engineChoice: Language) {
        if (lastRecorded == (keys to language)) return
        lastRecorded = keys to language
        engine.learn(keys, language, engineChoice)
    }

    /** Whether the keys have a split reading to cycle to: the engine's (x에, hello월드), which the
     *  particle split alone doesn't find. */
    private fun engineSplits(keys: String): Boolean = engine.render(keys, Language.MIXED) != keys

    /** Replaces a just-committed word with its next rendering, but only if the caret
     *  still sits right after it and the text there is what we inserted. */
    private fun cycle(commit: Committed, client: TextClient): Committed? {
        val range = TextRange(commit.location, commit.text.length)
        val cursor = client.selectedRange()
        if (cursor.location == TextRange.NOT_FOUND || cursor.length != 0 || cursor.location != range.end ||
            client.text(range) != commit.text) return null

        val language = commit.language.next(commit.keys, japaneseEnabled, engineSplits(commit.keys))
        val reading = if (language == Language.JAPANESE) JapaneseSegmenter.reading(commit.keys, segmenter)
                      else engine.render(commit.keys, language)
        val text = reading + commit.boundary
        client.insertText(text, range)
        previousLanguage = language.sentenceLanguage
        return commit.copy(language = language, text = text)
    }

    private fun closeFlipWindow() {
        lastCommit?.let(::finalize)
        lastCommit = null
    }

    /** The undo window closed: learn where the word ended up (once, not every reading the switch
     *  passed through on the way). */
    private fun finalize(commit: Committed) {
        if (commit.language != commit.committedAs) record(commit.keys, commit.language, commit.decided)
    }

    companion object {
        private val closing = mapOf(')' to '（', ']' to '「', '}' to '｛')
        private val pairs = mapOf('）' to '（', '」' to '「', '｝' to '｛', '』' to '『', '】' to '【')

        private val koreanModel by lazy { KoreanModel() }

        /** The preview only switches language mid-word when the new reading is at least this likely. */
        const val PREVIEW_FLIP_CONFIDENCE = 0.7

        /** What a number reading is made of: digits and the punctuation inside numbers, either width. */
        val numberCharacters = "0123456789０１２３４５６７８９－．，／：-.,/:".toSet()

        /** The digits of [text] in order, as ASCII ("東京都１－２" → "12"). */
        fun digits(text: String): String = halfWidth(text).filter { it.isAsciiDigit }

        /** Full-width ASCII (！ to ～) as ASCII. */
        fun halfWidth(text: String): String =
            String(CharArray(text.length) { text[it].let { c -> if (c.code in 0xFF01..0xFF5E) c - 0xFEE0 else c } })

        /** The romaji left after the part that produced the first [kanaCount] kana: the
         *  shortest tail that composes to exactly the remaining kana ("kitte" after きっ is
         *  "te"; "konnichiha" after こん is "nichiha"). */
        fun keys(keys: String, kanaCount: Int): String {
            val rest = RomajiComposer.compose(keys).text.drop(kanaCount)
            for (i in keys.length downTo 0) {
                if (RomajiComposer.compose(keys.substring(i)).text == rest) return keys.substring(i)
            }
            return ""
        }

        /** The longest start of [from] that composes to a start of [spelling], followed by a spelling of
         *  the rest ("kya" for き is "ki"; "kitte" for きっ is "kiltu"). */
        fun keys(spelling: String, from: String): String? {
            for (cut in from.length - 1 downTo 0) {
                val front = from.substring(0, cut)
                val composed = RomajiComposer.compose(front, partial = true)
                if (composed.pending.isNotEmpty() || composed.strayLetters != 0 || !spelling.startsWith(composed.text)) continue
                val rest = RomajiComposer.romaji(spelling.drop(composed.text.length)) ?: continue
                val respelled = RomajiComposer.compose(front + rest, partial = true)
                if (respelled.pending.isEmpty() && respelled.text == spelling) return front + rest
            }
            return null
        }

        /** Where the keys of the last syllable start: the longest front that makes [earlier] alone. */
        private fun cut(keys: String, earlier: List<HangulComposer.Unit>): Int =
            (keys.length - 1 downTo 0).firstOrNull { HangulComposer.compose(keys.substring(0, it))?.units == earlier }
                ?: (keys.length - 1)
    }
}
