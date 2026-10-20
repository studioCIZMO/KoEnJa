package com.netmaru.rea.engine

import kotlin.test.Test
import kotlin.test.assertEquals

/**
 * Replays golden_input.txt as Android/tools/golden (the Swift engine) did and compares with
 * golden.tsv, its output: config \t keys \t text added.
 */
class GoldenTest {
    /** The text field: everything before the cursor, the marked part at its end. */
    private class Field : TextClient {
        var text = ""
        var marked = TextRange(TextRange.NOT_FOUND, 0)
        private val end get() = text.length
        override fun setMarkedText(text: String) = setMarkedText(text, TextRange(end, 0))
        override fun setMarkedText(text: String, replacing: TextRange) {
            val start = if (marked.location == TextRange.NOT_FOUND) replacing.location else minOf(replacing.location, marked.location)
            this.text = this.text.substring(0, start) + text
            marked = if (text.isEmpty()) TextRange(TextRange.NOT_FOUND, 0) else TextRange(start, text.length)
        }
        override fun insertText(text: String, replacing: TextRange?) {
            val r = replacing ?: if (marked.location == TextRange.NOT_FOUND) TextRange(end, 0) else marked
            val start = if (marked.location == TextRange.NOT_FOUND) r.location else minOf(r.location, marked.location)
            this.text = this.text.substring(0, start) + text
            marked = TextRange(TextRange.NOT_FOUND, 0)
        }
        override fun selectedRange() = TextRange(end, 0)
        override fun markedRange() = marked
        override fun text(range: TextRange) = if (range.location >= 0 && range.end <= end) text.substring(range.location, range.end) else null
    }

    private fun session(order: List<Language>) = Engines.makeSession(order, converter = null).apply { activate() }

    @Test
    fun matchesSwift() {
        val lines = resource("golden_input.txt").split("\n")
        val expected = resource("golden.tsv").split("\n").filter { it.isNotEmpty() }
        val actual = ArrayList<String>()
        val configs = listOf("ko-en-ja" to listOf(Language.KOREAN, Language.ENGLISH, Language.JAPANESE),
                             "ko-ja-en" to listOf(Language.KOREAN, Language.JAPANESE, Language.ENGLISH))
        for ((name, order) in configs) {
            var s = session(order)
            var field = Field()
            for (line in lines) {
                if (line.isEmpty()) continue
                if (line == "#reset") { s = session(order); field = Field(); continue }
                val before = field.text.length
                for (c in line) {
                    val key = when (c) {
                        '⏎' -> InputSession.Key.Enter
                        '⌫' -> InputSession.Key.Delete
                        '※' -> InputSession.Key.Cycle
                        else -> if (c.isLetter()) InputSession.Key.Letter(c) else InputSession.Key.Printable(c)
                    }
                    if (!s.handle(key, field)) {
                        when (key) {
                            InputSession.Key.Enter -> field.insertText("\n", null)
                            InputSession.Key.Delete -> if (field.text.isNotEmpty()) field.text = field.text.dropLast(1)
                            is InputSession.Key.Printable -> field.insertText(key.ch.toString(), null)
                            is InputSession.Key.Letter -> field.insertText(key.ch.toString(), null)
                            else -> {}
                        }
                    }
                }
                s.commit(field)
                val added = if (field.text.length >= before) field.text.substring(before) else "(deleted)"
                actual.add("$name\t$line\t${added.replace("\n", "⏎")}")
            }
        }
        assertEquals(expected.size, actual.size, "line count")
        var passed = 0
        val counts = LinkedHashMap<String, IntArray>()
        for ((e, a) in expected.zip(actual)) {
            val count = counts.getOrPut(e.substringBefore('\t')) { IntArray(2) }
            count[1]++
            if (e == a) { passed++; count[0]++; continue }
            val (config, keys, want) = e.split('\t')
            println("MISMATCH [$config] $keys\n  expected: $want\n  actual:   ${a.split('\t')[2]}")
        }
        for ((config, count) in counts) println("$config: ${count[0]}/${count[1]}")
        println("golden: $passed/${expected.size}")
        assertEquals(expected.size, passed, "golden mismatches")
    }

    /** Previews as SeamtypeCore shows them (ko-ja-en): an English word's first letters stay letters. */
    @Test
    fun shortPrefixesStayLetters() {
        val cases = mapOf(
            "up" to "u → up",
            "table" to "t → ta → tab → tabl → table",
            "chairs" to "c → ch → cha → ちゃい → chair → chairs",
            "watashi" to "w → わ → わt → わた → わたs → わたsh → わたし",
        )
        for ((keys, want) in cases) {
            val s = session(listOf(Language.KOREAN, Language.JAPANESE, Language.ENGLISH))
            val field = Field()
            val shown = ArrayList<String>()
            for (c in keys) {
                s.handle(InputSession.Key.Letter(c), field)
                val marked = field.text.substring(field.marked.location)
                if (shown.lastOrNull() != marked) shown.add(marked)
            }
            assertEquals(want, shown.joinToString(" → "), keys)
        }
    }

    private fun resource(name: String) = javaClass.getResource("/$name")!!.readText()
}
