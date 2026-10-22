package com.netmaru.rea.engine

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** The REA: changes to SeamtypeCore, as its tests check them (URLRunTests, JapaneseSessionTests). */
class ReaSessionTest {
    private val field = GoldenTest.Field()
    private val trilingual = HeuristicEngine(korean = KoreanModel(), japanese = JapaneseModel.shared)
    private val engine = DictionaryEngine(SplittingEngine(trilingual, trilingual.english, japanese = trilingual.segmenter))

    private object Converter : JapaneseConverter {
        override fun candidates(kana: String) = listOf(ConversionCandidate(kana, kana.length))
        override fun learn(candidate: ConversionCandidate, kana: String) {}
        override fun save() {}
    }

    /** Keys through [session] (⎋: Esc); the app types what is passed on. */
    private fun type(session: InputSession, keys: String) {
        for (ch in keys) {
            val key = if (ch == '⎋') InputSession.Key.Escape else if (ch.isAsciiLetter) InputSession.Key.Letter(ch) else InputSession.Key.Printable(ch)
            if (!session.handle(key, field)) field.insertText(ch.toString(), null)
        }
    }

    @Test
    fun addressParticleAndDotfile() {
        for ((keys, text) in listOf("x@gmail.comdmfh " to "x@gmail.com으로 ", "www.a.comdp " to "www.a.com에 ",
                                    "https://x.io/dp " to "https://x.io/dp ", "https://x.io/yorkfmf " to "https://x.io/yorkfmf ",
                                    ".env " to ".env ", "...dma " to "...음 ")) {
            field.text = ""
            type(InputSession(engine).apply { policy = AppPolicy(escapeSwitches = true) }, keys)
            assertEquals(text, field.text, keys)
        }
    }

    @Test
    fun capitalizedEnglishWordBeforeJapanese() {
        val session = InputSession(engine, Converter, japaneseEnabled = true)
        type(session, "iPhonedetesutoshimashita")
        assertTrue(session.handle(InputSession.Key.Enter, field))
        assertEquals("iPhoneでてすとしました", field.text)
    }

    /** A short word's first letters stay letters while typed, and Space commits them as shown. */
    @Test
    fun spaceCommitsAShortPrefixAsShown() {
        val session = InputSession(engine, Converter, japaneseEnabled = true)
        type(session, "zio")
        assertEquals("zio", field.text)
        type(session, " ")
        assertNull(session.conversion)
        type(session, "taco ")
        assertEquals("zio taco ", field.text)
    }
}
