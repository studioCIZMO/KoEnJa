package com.netmaru.rea.engine

import java.io.File
import java.time.LocalDate
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** Araso 0.3.1's JapaneseSessionTests that apply to the port: Delete on a conversion and in live
 *  conversion, Space after a letter waiting for its vowel, the switch key on a live phrase. */
class JapaneseSessionTest {
    private val field = GoldenTest.Field()
    private val trilingual = HeuristicEngine(korean = KoreanModel(), japanese = JapaneseModel.shared)
    private val base = SplittingEngine(trilingual, trilingual.english, japanese = trilingual.segmenter)
    private val engine = DictionaryEngine(base)
    private val converter = FakeConverter()
    private val session = InputSession(engine, converter, japaneseEnabled = true)
    private val live = InputSession(engine, converter, japaneseEnabled = true, liveConversion = true)

    /** Deterministic converter, as the Swift tests' FakeConverter. */
    class FakeConverter : JapaneseConverter {
        val learned = ArrayList<String>()
        override fun candidates(kana: String) = when (kana) {
            "きょうはいいてんきですね" -> listOf(ConversionCandidate("今日はいい天気ですね", 12), ConversionCandidate("今日は", 4),
                                                ConversionCandidate(kana, 12))
            "いいてんきですね" -> listOf(ConversionCandidate("いい天気ですね", 8), ConversionCandidate(kana, 8))
            "へんかん" -> listOf(ConversionCandidate("変換", 4), ConversionCandidate("返還", 4), ConversionCandidate("編纂", 4))
            "3じ" -> listOf(ConversionCandidate("三時", 2), ConversionCandidate("３時", 2), ConversionCandidate("3時", 2),
                            ConversionCandidate("3", 1))
            "やま" -> listOf(ConversionCandidate("山😀", 2), ConversionCandidate(kana, 2))
            "かわ" -> listOf(ConversionCandidate("ｶﾞ", 2), ConversionCandidate(kana, 2))
            "そら" -> listOf(ConversionCandidate("ｶﾞﾅ", 2), ConversionCandidate(kana, 2))
            "かず" -> (1..12).map { ConversionCandidate("数$it", 2) }
            else -> listOf(ConversionCandidate(kana, kana.length))
        }
        override fun learn(candidate: ConversionCandidate, kana: String) { learned += candidate.text }
        override fun save() {}
    }

    private fun type(keys: String, session: InputSession = this.session) {
        for (ch in keys) {
            val key = if (ch.isLetter()) InputSession.Key.Letter(ch) else InputSession.Key.Printable(ch)
            if (!session.handle(key, field)) field.insertText(ch.toString(), null)
        }
    }

    private fun clear() { field.text = ""; field.marked = TextRange(TextRange.NOT_FOUND, 0) }

    /** so switched to Japanese (そ), then me: the pick was learned for so when m came. Space converts,
     *  Esc puts the kana back as the forced word, Enter sends it in: some isn't learned on top. */
    @Test
    fun aConversionCancelledBackToTheSwitchedWordDoesNotLearnIt() {
        type("so")
        for (n in 0 until 4) { if (field.text == "そ") break; session.handle(InputSession.Key.Cycle, field) }
        assertEquals("そ", field.text)
        type("me")
        assertEquals(Language.JAPANESE, engine.entry("so")?.language)
        type(" ")
        assertNotNull(session.conversion)
        session.handle(InputSession.Key.Escape, field)
        assertNull(session.conversion)
        session.handle(InputSession.Key.Enter, field)
        assertEquals("そめ", field.text)
        assertNull(engine.entry("some"))
        assertEquals(Language.JAPANESE, engine.entry("so")?.language)
    }

    @Test
    fun aNumberIsANumber() {
        assertEquals("a number", trilingual.decide("2026", DecisionContext()).explanation)
    }

    @Test
    fun deleteInConversionCommitsAllButTheLastCharacter() {
        type("henkan ")
        assertEquals("変換", field.text)
        assertTrue(session.handle(InputSession.Key.Delete, field))
        assertEquals("変", field.text)
        assertEquals(TextRange.NOT_FOUND, field.marked.location)
        assertTrue(session.conversion == null && session.keys.isEmpty())
        assertTrue(converter.learned.isEmpty())
        assertFalse(session.handle(InputSession.Key.Delete, field))
        field.text = field.text.dropLast(1) // the app's Delete
        type("kidesu")
        session.handle(InputSession.Key.Enter, field)
        assertEquals("きです", field.text) // a new word after what went in
        type("kyouhaiitenkidesune ")
        session.handle(InputSession.Key.Delete, field)
        assertEquals("きです今日はいい天気です", field.text)
    }

    @Test
    fun deleteInConversionDropsAWholeLastCharacter() {
        type("yama ")
        assertEquals("山😀", field.text)
        session.handle(InputSession.Key.Delete, field)
        assertEquals("山", field.text) // not 山 and half of 😀
    }

    /** The last character as Swift's dropLast() takes it: an extended grapheme cluster. */
    @Test
    fun dropLastCharacterTakesAGraphemeCluster() {
        val drop = InputSession::dropLastCharacter
        assertEquals("山", drop("山葛\uDB40\uDD00"))                 // an ideographic variation selector (U+E0100)
        assertEquals("山", drop("山👩🏽"))                             // a skin tone
        assertEquals("山", drop("山👨\u200D👩\u200D👧"))             // a ZWJ family
        assertEquals("", drop("ｶﾞ"))
        assertEquals("ｶﾞ", drop("ｶﾞﾅ"))
        assertEquals("山", drop("山か\u3099"))                       // か + combining dakuten (NFD が)
        assertEquals("A\n", drop("A\n\u0301"))                      // a mark after a newline is a character of its own
        assertEquals("山🇯🇵", drop("山🇯🇵🇰🇷"))                         // flags: regional indicators in pairs
        assertEquals("a", drop("a\r\n"))
        assertEquals("한", drop("한\u1100\u1161\u11A8"))
        assertEquals("", drop(""))
        assertEquals("A\u21A0\u200D", drop("A\u21A0\u200D\u21A0"))  // ↠ isn't pictographic
        assertEquals("A\u200B", drop("A\u200B\u0301"))              // ZWSP is a control
        assertEquals("A", drop("Aका"))                               // a spacing mark joins
    }

    /** Unicode's GraphemeBreakTest.txt: each line less its last cluster. */
    @Test
    fun graphemeBreakTest() {
        var lines = 0
        for (raw in javaClass.getResource("/GraphemeBreakTest.txt")!!.readText().lines()) {
            val line = raw.substringBefore('#')
            if (line.isBlank()) continue
            // ÷ before each cluster and at the end: the text less its last cluster ends at the one before last.
            val text = StringBuilder()
            val boundaries = ArrayList<String>()
            for (field in line.trim().split(Regex("\\s+"))) {
                when (field) {
                    "÷" -> boundaries += text.toString()
                    "×" -> {}
                    else -> text.appendCodePoint(field.toInt(16))
                }
            }
            assertEquals(boundaries.getOrElse(boundaries.size - 2) { "" }, InputSession.dropLastCharacter(text.toString()), raw)
            lines++
        }
        assertEquals(1093, lines)
    }

    @Test
    fun deleteInConversionDropsTheMarksWithTheCharacter() {
        type("kawa ")
        assertEquals("ｶﾞ", field.text)
        session.handle(InputSession.Key.Delete, field)
        assertEquals("", field.text) // ｶﾞ is one character, as in Swift
        type("sora ")
        assertEquals("ｶﾞﾅ", field.text)
        session.handle(InputSession.Key.Delete, field)
        assertEquals("ｶﾞ", field.text)
    }

    @Test
    fun aDigitPicksFromThePageOnShow() {
        type("kazu ")
        repeat(9) { session.handle(InputSession.Key.Down, field) }
        assertEquals("数10", field.text)
        type("2")
        assertEquals("数11", field.text)
        assertNull(session.conversion)
    }

    @Test
    fun deleteInConversionTakesTheNumberWithIt() {
        type("henkan ")
        session.handle(InputSession.Key.Enter, field)
        type("3ji ")
        assertEquals("変換3時", field.text)
        session.handle(InputSession.Key.Delete, field)
        assertEquals("変換3", field.text)
        assertNull(session.conversion)
    }

    /** Through its katakana form the number is part of the form's kana; Esc puts the kana in (the
     *  keys no longer spell it) and the next Return is the app's, as in Swift. */
    @Test
    fun escapeOnTheFormOfANumberedPhrase() {
        type("henkan ")
        session.handle(InputSession.Key.Enter, field)
        type("3ji ")
        assertTrue(session.handle(InputSession.Key.Katakana, field))
        assertEquals("変換3ジ", field.text)
        assertEquals("3じ", session.conversion?.kana)
        assertEquals("ji", session.conversion?.keys)
        session.handle(InputSession.Key.Escape, field)
        assertEquals("変換3じ", field.text)
        assertNull(session.conversion)
        assertEquals(TextRange.NOT_FOUND, field.marked.location)
        assertFalse(session.handle(InputSession.Key.Enter, field))
    }

    @Test
    fun deleteInLiveConversionKeepsTheClausesBefore() {
        type("kyouhaiitenkidesune", live)
        assertEquals("今日はいい天気ですね", field.text)
        type(" ", live)
        assertEquals("今日はいいてんきですね", field.text) // the next candidate covers 今日は only
        assertTrue(live.handle(InputSession.Key.Delete, field))
        assertNull(live.conversion)
        assertEquals("きょうはいいてんきです", field.text) // 今日は|いいてんきですね: no clause before 今日は
        live.handle(InputSession.Key.Escape, field)
        live.handle(InputSession.Key.Enter, field)
        clear()

        type("kyouhaiitenkidesune", live)
        assertTrue(live.handle(InputSession.Key.Delete, field))
        assertEquals("今日はいいてんきです", field.text)
        live.handle(InputSession.Key.Delete, field)
        assertEquals("今日はいいてんきで", field.text)
        live.handle(InputSession.Key.Enter, field)
        assertEquals("今日はいいてんきで", field.text)
        assertEquals(TextRange.NOT_FOUND, field.marked.location)
        assertTrue(converter.learned.isEmpty())
        clear()

        // Esc then shows it all as kana, as after any live conversion.
        type("kyouhaiitenkidesune", live)
        live.handle(InputSession.Key.Delete, field)
        live.handle(InputSession.Key.Escape, field)
        assertEquals("きょうはいいてんきです", field.text)
    }

    /** A number taken into a live conversion goes back as typed, the phrase less a kana after it. */
    @Test
    fun deleteInLiveConversionPutsTheNumberBack() {
        type("henkan", live)
        live.handle(InputSession.Key.Enter, field)
        type("3ji ", live)
        assertTrue(field.text.startsWith("変換") && field.text.contains("時"))
        live.handle(InputSession.Key.Delete, field)
        assertEquals("変換3", field.text)
        assertTrue(live.conversion == null && live.keys.isEmpty())
    }

    @Test
    fun deleteInLiveConversionThenSpaceShowsTheFirstCandidate() {
        type("henkan ", live)
        live.handle(InputSession.Key.Delete, field)
        assertEquals("へんか", field.text)
        type("n", live)
        assertEquals("へんかn", field.text)
        type(" ", live)
        assertEquals("変換", field.text)
    }

    @Test
    fun spaceAfterAPendingLetterShowsTheFirstCandidate() {
        type("henkan", live)
        assertEquals("へんかn", field.text)
        type(" ", live)
        assertEquals("変換", field.text)
        live.handle(InputSession.Key.Escape, field)
        live.handle(InputSession.Key.Escape, field)
        clear()
        type("henkann", live)
        assertEquals("変換", field.text)
        type(" ", live)
        assertEquals("返還", field.text)
    }

    @Test
    fun lastClauseOfAConversion() {
        assertEquals("今日はいい" to 6, InputSession.dropLastClause("今日はいい天気です", "きょうはいいてんきです"))
        assertEquals("今日は" to 4, InputSession.dropLastClause("今日は箱", "きょうははこ"))
        assertEquals("googleで" to 7, InputSession.dropLastClause("googleで検索", "googleでけんさく"))
        assertEquals("" to 0, InputSession.dropLastClause("変換", "へんかん"))
        assertEquals("" to 0, InputSession.dropLastClause("きょうは", "きょうは"))
        assertEquals("" to 0, InputSession.dropLastClause("今日は天気", "ちがう"))
        // A conversion the reading can't match is given up on at once, not in exponential time.
        assertEquals("" to 0, InputSession.dropLastClause("漢あ".repeat(16) + "漢い漢", "あ".repeat(36)))
    }

    /** With live conversion a switch key takes a converted phrase back to its kana first, but a
     *  phrase showing its kana anyway (きょうは, whose first candidate is the kana) switches at once. */
    @Test
    fun switchKeyOnAKanaShownLivePhraseSwitchesAtOnce() {
        type("kyouha", live)
        assertEquals("きょうは", field.text)
        assertTrue(live.handle(InputSession.Key.Cycle, field))
        assertEquals("kyouha", field.text)
    }

    @Test
    fun switchKeyOnAConvertedLivePhraseShowsTheKanaFirst() {
        type("kyouhaiitenkidesune", live)
        assertEquals("今日はいい天気ですね", field.text)
        live.handle(InputSession.Key.Cycle, field)
        assertEquals("きょうはいいてんきですね", field.text)
        live.handle(InputSession.Key.Cycle, field)
        assertFalse(field.text.contains("きょう"))
    }

    @Test
    fun escapeCancelsAKanaShownLivePhrase() {
        type("kyouha", live)
        assertTrue(live.handle(InputSession.Key.Escape, field))
        assertTrue(field.text.isEmpty() && live.keys.isEmpty())
    }

    /** A word that goes in through its conversion or its kana forms was typed again, as one that
     *  goes in with Return: a weak correction to it stays, and a switch away from a remembered one
     *  noted since was a slip. (Not ported: the list of words the dictionary decided.) */
    @Test
    fun convertedWordIsTypedAgain() {
        for (katakana in listOf(false, true)) {
            val lex = File.createTempFile("rea-lj", ".tsv").apply { delete(); deleteOnExit() }
            val corrected = LocalDate.of(2026, 9, 21)
            val typed = corrected.plusDays(20)
            val before = DictionaryEngine(base, storeFile = lex, today = { corrected })
            before.learn("kyouha", Language.JAPANESE, Language.ENGLISH) // once: weak
            before.learn("sugoi", Language.JAPANESE, Language.ENGLISH)
            before.learn("sugoi", Language.JAPANESE, Language.ENGLISH) // twice: remembered
            before.learn("sugoi", Language.KOREAN, Language.JAPANESE) // a switch away, noted
            assertTrue(lex.readText().contains("sure\tsugoi\t${Language.JAPANESE.raw}\t$corrected\t${Language.KOREAN.raw}\n"), lex.readText())
            val session = InputSession(DictionaryEngine(base, storeFile = lex, today = { typed }), FakeConverter(), japaneseEnabled = true)
            clear()
            for (word in listOf("kyouha", "sugoi")) {
                type(word, session)
                session.handle(if (katakana) InputSession.Key.Katakana else InputSession.Key.Printable(' '), field)
                assertNotNull(session.conversion)
                session.handle(InputSession.Key.Enter, field)
            }
            assertEquals(if (katakana) "キョウハスゴイ" else "きょうはすごい", field.text)
            assertTrue(lex.readText().contains("weak\tkyouha\t${Language.JAPANESE.raw}\t$typed"), lex.readText())
            assertTrue(lex.readText().contains("sure\tsugoi\t${Language.JAPANESE.raw}\t$corrected\n"), lex.readText())
        }
    }

    /** A word switched to Japanese and put in its kana forms learns the switch, as with Space. */
    @Test
    fun kanaFormsLearnTheSwitch() {
        val lex = File.createTempFile("rea-lj", ".tsv").apply { delete(); deleteOnExit() }
        val session = InputSession(DictionaryEngine(base, storeFile = lex), FakeConverter(), japaneseEnabled = true)
        type("sushi", session)
        assertEquals("sushi", field.text)
        repeat(4) { if (field.text != "すし") session.handle(InputSession.Key.Cycle, field) }
        assertEquals("すし", field.text)
        session.handle(InputSession.Key.Katakana, field)
        session.handle(InputSession.Key.Enter, field)
        assertEquals("スシ", field.text)
        assertTrue(lex.readText().contains("weak\tsushi\t${Language.JAPANESE.raw}"), lex.readText())
    }
}
