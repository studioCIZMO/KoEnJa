package com.netmaru.rea

import com.netmaru.rea.engine.EnglishModel
import com.netmaru.rea.engine.HangulComposer
import com.netmaru.rea.engine.KoreanModel
import org.junit.Assert.assertEquals
import org.junit.Test

/** iOS TypingTests.testAutocorrect's cases, on the word lists alone. */
class SuggestionsTest {
    private val korean = KoreanModel()
    private val english = EnglishModel.load()

    private fun korean(keys: String) = Suggestions.korean(HangulComposer.compose(keys)!!.text, keys, korean).autocorrect

    @Test fun koreanTypos() {
        assertEquals("진행한다", korean("wlagodgksek"))
        assertEquals("안녕하세요", korean("dkssudgktpdu"))
        assertEquals(null, korean("snffu")) // 눌려: a word, though not in our lists
        assertEquals(null, korean("dkssudgktpdy"))
        assertEquals(null, korean("qjrmfmf")) // 버그를, not 버스를
    }

    @Test fun englishTypos() {
        assertEquals("the", Suggestions.english("teh", english).autocorrect)
        assertEquals("hello", Suggestions.english("hellp", english).autocorrect)
        assertEquals(null, Suggestions.english("hello", english).autocorrect)
        assertEquals(null, Suggestions.english("Seamtype", english).autocorrect)
    }
}
