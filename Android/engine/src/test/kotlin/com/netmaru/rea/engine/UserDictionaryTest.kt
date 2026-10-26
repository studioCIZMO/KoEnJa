package com.netmaru.rea.engine

import java.time.LocalDate
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** Araso 0.3.1's UserDictionaryTests and InputSessionTests on a remembered word switched away from. */
class UserDictionaryTest {
    private val day = LocalDate.of(2026, 10, 26)

    /** A remembered word switched once to another reading keeps its memory and notes the switch;
     *  the same switch again moves the memory, and typing the word as remembered drops the note. */
    @Test
    fun rememberedWordMovesOnTheSecondSwitch() {
        val dictionary = UserDictionary()
        fun entry() = dictionary.entries.single { it.keys == "tori" }
        dictionary.learn("tori", Language.ENGLISH, Language.JAPANESE, day)
        dictionary.learn("tori", Language.ENGLISH, Language.JAPANESE, day)
        assertFalse(dictionary.learn("tori", Language.KOREAN, Language.ENGLISH, day))
        assertEquals(Language.ENGLISH, entry().language)
        assertEquals(Language.KOREAN, entry().next)
        assertTrue(dictionary.used("tori", Language.ENGLISH, day))
        assertNull(entry().next)
        dictionary.learn("tori", Language.KOREAN, Language.ENGLISH, day)
        assertTrue(dictionary.learn("tori", Language.KOREAN, Language.ENGLISH, day))
        assertEquals(Language.KOREAN, entry().language)
        assertEquals(UserDictionary.Strength.SURE, entry().strength)
        assertNull(entry().next)
    }

    /** Switching a remembered word back to Araso's own reading forgets it in one go. */
    @Test
    fun backToTheOwnReadingForgetsTheWord() {
        val field = GoldenTest.Field()
        val trilingual = HeuristicEngine(korean = KoreanModel(), japanese = JapaneseModel.shared)
        val session = InputSession(DictionaryEngine(SplittingEngine(trilingual, trilingual.english, japanese = trilingual.segmenter)))
        fun type(keys: String) {
            for (ch in keys) {
                val key = if (ch == '※') InputSession.Key.Cycle else if (ch.isLetter()) InputSession.Key.Letter(ch) else InputSession.Key.Printable(ch)
                if (!session.handle(key, field)) field.insertText(ch.toString(), null)
            }
        }
        type("rjtm ")
        val own = field.text.dropLast(1)
        type("※rjtm ※x rjtm ") // corrected twice: remembered, goes in as the correction
        assertFalse(field.text.endsWith(" $own "))
        type("※")
        assertTrue(field.text.endsWith(" $own "))
        type("a rjtm ")
        assertTrue(field.text.endsWith(" $own "), field.text)
    }
}
