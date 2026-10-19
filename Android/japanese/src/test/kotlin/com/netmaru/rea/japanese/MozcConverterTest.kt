package com.netmaru.rea.japanese

import com.netmaru.rea.engine.ConversionCandidate
import java.io.File
import kotlin.system.measureNanoTime
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class MozcConverterTest {
    private fun top(kana: String) = converter.candidates(kana).first().text

    @Test fun sentences() {
        assertEquals("今日はいい天気ですね", top("きょうはいいてんきですね"))
        assertEquals("会議の資料を送ります", top("かいぎのしりょうをおくります"))
        assertEquals("東京に行きます", top("とうきょうにいきます"))
        assertEquals("正解か", top("せいかいか"))
    }

    @Test fun candidateShape() {
        val kana = "きょうはいいてんきですね"
        val all = converter.candidates(kana)
        println(all)
        assertTrue(all.size in 10..25)
        assertEquals(all.size, all.map { it.text }.toSet().size)
        val firstPartial = all.indexOfFirst { it.kanaCount < kana.length }
        assertTrue(firstPartial > 0 && all.drop(firstPartial).dropLast(2).all { it.kanaCount < kana.length })
        assertTrue(all.any { it.text == "今日は" && it.kanaCount == 4 })
        assertEquals(listOf(kana, "キョウハイイテンキデスネ"), all.takeLast(2).map { it.text })
        assertEquals("、。", top("、。"))
    }

    @Test fun phrases() {
        val misses = PHRASES.filter { (kana, want) -> top(kana) !in want.split('|') }
        misses.forEach { (kana, want) -> println("MISS $kana → ${top(kana)} (want $want)") }
        val accuracy = 1.0 - misses.size.toDouble() / PHRASES.size
        println("top-1 accuracy: ${PHRASES.size - misses.size}/${PHRASES.size} = ${"%.0f".format(accuracy * 100)}%")
        assertTrue(accuracy >= 0.8)
    }

    @Test fun learning() {
        val file = File.createTempFile("mozc-learning", ".txt").apply { delete() }
        val learner = MozcConverter(learningFile = file)
        val alternative = learner.candidates("じこ").first { it.text == "自己" }
        learner.learn(alternative, "じこ")
        assertEquals("自己", learner.candidates("じこ").first().text)
        learner.save()
        assertEquals("自己", MozcConverter(learningFile = file).candidates("じこ").first().text)
        learner.learn(ConversionCandidate("事故", 2), "じこ")
        assertEquals("事故", learner.candidates("じこ").first().text)
        file.delete()
    }

    @Test fun performance() {
        val kana = "かいぎのしりょうをおくりますのでかくにん" // 20 kana
        repeat(20) { converter.candidates(kana) }
        val ms = measureNanoTime { repeat(50) { converter.candidates(kana) } } / 50 / 1e6
        println("candidates(20 kana): %.1f ms; load %d ms, heap +%d MB".format(ms, loadMs, heapMb))
        assertTrue(ms < 50)
        assertTrue(loadMs < 3000) // 1.5 s target; slack for a cold test JVM
    }

    companion object {
        var loadMs = 0L
        var heapMb = 0L
        val converter: MozcConverter by lazy {
            val rt = Runtime.getRuntime()
            System.gc()
            val before = rt.totalMemory() - rt.freeMemory()
            val c = MozcConverter()
            loadMs = measureNanoTime { c.candidates("あ") } / 1_000_000
            System.gc()
            heapMb = (rt.totalMemory() - rt.freeMemory() - before) / (1 shl 20)
            c
        }

        /** The user's text, phrase by phrase: reading to expected (alternatives split by |). */
        val PHRASES = listOf(
            "まちがいか" to "間違いか",
            "かんけいない" to "関係ない",
            "びょうき" to "病気",
            "けっこん" to "結婚",
            "りこん" to "離婚",
            "わざわい" to "災い",
            "にんげんかんけい" to "人間関係",
            "かんがえて" to "考えて",
        )
    }
}
