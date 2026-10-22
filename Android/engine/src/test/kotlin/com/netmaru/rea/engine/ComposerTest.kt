package com.netmaru.rea.engine

import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ComposerTest {
    private fun hangul(keys: String) = HangulComposer.compose(keys)!!.text

    @Test
    fun hangul() {
        assertEquals("안녕하세요", hangul("dkssudgktpdy"))
        assertEquals("한국", hangul("gksrnr"))
        assertEquals("가바", hangul("rkqk")) // 받침 moves on
        assertEquals("갑사", hangul("rkqtk")) // compound final splits
        assertEquals("값", hangul("rkqt"))
        assertEquals("과", hangul("rhk")) // compound vowel
        assertEquals("빨리", hangul("Qkffl")) // shifted jamo
        assertEquals("ㅋㅋ", hangul("zz"))
        assertEquals("앉아", hangul("dkswdk"))
        assertEquals("ㄸ", hangul("dkE").drop(1)) // ㄸ can't be a final
        assertNull(HangulComposer.compose("a1"))
    }

    @Test
    fun hangulUnits() {
        val r = HangulComposer.compose("gksrnrd")!!
        assertEquals(listOf('한', '국'), r.syllables)
        assertTrue(r.endsWithPendingConsonant)
        assertFalse(r.isBroken)
        assertTrue(HangulComposer.compose("goo")!!.isBroken) // 해ㅐ
    }

    @Test
    fun romaji() {
        fun kana(keys: String, partial: Boolean = false) = RomajiComposer.compose(keys, partial).text
        assertEquals("こんにちは", kana("konnichiha"))
        assertEquals("きって", kana("kitte"))
        assertEquals("まっちゃ", kana("matcha"))
        assertEquals("かんい", kana("kan'i"))
        assertEquals("らーめん", kana("ra-men"))
        assertEquals("しゃしん", kana("shashin"))
        assertEquals("ぁっ", kana("laltu"))
        assertEquals("０３－１２", kana("03-12"))
        assertEquals("にほん。", kana("nihon."))
        val partial = RomajiComposer.compose("kaky", partial = true)
        assertEquals("かky", partial.text)
        assertEquals("ky", partial.pending)
        assertEquals(0, partial.strayLetters)
        assertEquals("n", RomajiComposer.compose("kan", partial = true).pending)
        val hello = RomajiComposer.compose("hello")
        assertEquals("へっぉ", hello.text)
        assertTrue(hello.isClean)
        assertEquals(1, RomajiComposer.compose("google").strayLetters)
        assertEquals("カタカナ", RomajiComposer.katakana("かたかな"))
        assertEquals("kiltu", RomajiComposer.romaji("きっ"))
        assertEquals("n'", RomajiComposer.romaji("ん"))
    }

    @Test
    fun learningIsKeptInTheFile() {
        val file = File.createTempFile("lexicon", ".txt").apply { deleteOnExit() }
        val base = object : DecisionEngine {
            override fun decide(keys: String, context: DecisionContext) = Decision(Language.ENGLISH, 0.9, keys, "")
        }
        val english = EnglishModel(emptyMap())
        DictionaryEngine(base, english, storeFile = file).apply {
            learn("dkssud", Language.KOREAN)
            assertNull(learned["dkssud"]) // once is not yet learned
            learn("dkssud", Language.KOREAN)
        }
        val reloaded = DictionaryEngine(base, english, storeFile = file)
        assertEquals(Language.KOREAN, reloaded.learned["dkssud"])
        assertEquals("안녕", reloaded.decide("dkssud", DecisionContext()).output)
    }
}
