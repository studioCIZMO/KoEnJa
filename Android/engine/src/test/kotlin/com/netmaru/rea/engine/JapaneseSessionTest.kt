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
    fun deleteInConversionTakesTheNumberWithIt() {
        type("henkan ")
        session.handle(InputSession.Key.Enter, field)
        type("3ji ")
        assertEquals("変換3時", field.text)
        session.handle(InputSession.Key.Delete, field)
        assertEquals("変換3", field.text)
        assertNull(session.conversion)
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
