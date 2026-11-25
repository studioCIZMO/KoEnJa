package com.netmaru.rea.engine

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

/** Swift's CapsLockTests: letters arrive as Shift alone types them, words read as without Caps
 *  Lock, English goes in in capitals; loose jamo throughout (two or more) are an acronym unless
 *  the dictionary remembers them. */
class CapsLockTest {
    private val trilingual = HeuristicEngine(korean = KoreanModel(), japanese = JapaneseModel.shared)
    private val base = SplittingEngine(trilingual, trilingual.english, japanese = trilingual.segmenter)

    private fun fresh(file: java.io.File? = null) = InputSession(DictionaryEngine(base, storeFile = file))

    /** Keys through [session] (※: switch key); the app types what is passed on, in capitals with Caps Lock. */
    private fun type(session: InputSession, field: GoldenTest.Field, keys: String, caps: Boolean) {
        for (ch in keys) {
            val key = if (ch == '※') InputSession.Key.Cycle else if (ch.isLetter()) InputSession.Key.Letter(ch) else InputSession.Key.Printable(ch)
            if (!session.handle(key, field, caps)) field.insertText(if (caps) CapsLockClient.capitals(ch.toString()) else ch.toString(), null)
        }
    }

    private fun typed(keys: String, caps: Boolean = true) = GoldenTest.Field().also { type(fresh(), it, keys, caps) }.text

    @Test
    fun readAsWithoutCapsLock() {
        for ((keys, text) in listOf("gkaus " to "하면 ", "dkssudgktpdy " to "안녕하세요 ", "output " to "OUTPUT ",
                                    "apifmf " to "API를 ", "Qks " to "빤 ")) assertEquals(text, typed(keys))
    }

    @Test
    fun acronymsStayEnglish() {
        for ((keys, text) in listOf("rst rd ddr " to "RST RD DDR ", "output rd " to "OUTPUT RD ",
                                    "qusghkehlsms rp dho dmd whgdk " to "변화되는 게 왜 응 좋아 ", "zz " to "ZZ ")) assertEquals(text, typed(keys))
        assertEquals("ㅋㅋ ", typed("zz ", caps = false))
    }

    @Test
    fun previewAndCommitAsWithoutCapsLock() {
        for (keys in listOf("gkaus", "dkssudgktpdy", "rjatk", "dkssudr", "whgdkgg")) {
            fun shown(caps: Boolean): List<String> {
                val field = GoldenTest.Field()
                val session = fresh()
                return keys.map { ch ->
                    session.handle(InputSession.Key.Letter(ch), field, caps)
                    if (caps) field.text else CapsLockClient.capitals(field.text)
                }
            }
            assertEquals(shown(false), shown(true))
            assertEquals(CapsLockClient.capitals(typed("$keys ", caps = false)), typed("$keys "))
        }
        assertFalse(typed("dkssudr ").contains("DKSSUDR") || typed("whgdkgg ").contains("WHGDKGG"))
    }

    @Test
    fun theDictionaryWins() {
        val file = kotlin.io.path.createTempDirectory("koenja-caps").resolve("dictionary.tsv").toFile()
        file.writeText("user\thello\tkorean\nuser\tgd\tkorean\nuser\tdz\tkorean\n")
        for ((keys, text) in listOf("hello " to "ㅗ디ㅣㅐ ", "gd " to "ㅎㅇ ", "dz " to "ㅇㅋ ")) {
            val field = GoldenTest.Field()
            type(fresh(file), field, keys, true)
            assertEquals(text, field.text)
        }
    }

    @Test
    fun switchingAndCapsLockChanges() {
        assertEquals("GKAUS ", typed("gkaus ※"))
        assertEquals("Hello ", typed("Hello ", caps = false))
        val field = GoldenTest.Field()
        val session = fresh()
        type(session, field, "dk", true)
        type(session, field, "ssud ", false)
        assertEquals("아ㄴ녕 ", field.text)
    }

    /** A candidate being chosen goes in under the Caps Lock it was typed with. */
    @Test
    fun aCandidateGoesInAsItWasTyped() {
        val converter = object : JapaneseConverter {
            override fun candidates(kana: String) = listOf(ConversionCandidate("test", kana.length))
            override fun learn(candidate: ConversionCandidate, kana: String) {}
            override fun save() {}
        }
        val session = InputSession(DictionaryEngine(base), converter, japaneseEnabled = true)
        val field = GoldenTest.Field()
        type(session, field, "tesuto ", false)
        assertNotNull(session.conversion)
        type(session, field, "a", true)
        assertTrue(field.text.startsWith("test"), field.text)
    }
}
