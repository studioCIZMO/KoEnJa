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

        /** Everyday phrases: reading to expected (alternatives split by |). */
        val PHRASES = listOf(
            "きょうはいいてんきですね" to "今日はいい天気ですね",
            "てんき" to "天気",
            "あしたのかいぎ" to "明日の会議",
            "かいぎのしりょうをおくります" to "会議の資料を送ります",
            "しりょう" to "資料",
            "でんしゃがおくれています" to "電車が遅れています",
            "でんしゃ" to "電車",
            "えきでまっています" to "駅で待っています",
            "ひるごはんをたべましょう" to "昼ご飯を食べましょう|昼ごはんを食べましょう",
            "しゅくだい" to "宿題",
            "べんきょうする" to "勉強する",
            "にほんごをべんきょうしています" to "日本語を勉強しています",
            "しごと" to "仕事",
            "しごとがおわったられんらくします" to "仕事が終わったら連絡します",
            "れんらく" to "連絡",
            "ありがとうございます" to "ありがとうございます|有難うございます",
            "よろしくおねがいします" to "よろしくお願いします|宜しくお願いします",
            "おねがいします" to "お願いします",
            "じかんがありますか" to "時間がありますか",
            "じかん" to "時間",
            "らいしゅうのよてい" to "来週の予定",
            "よてい" to "予定",
            "でんわばんごう" to "電話番号",
            "じゅうしょをおしえてください" to "住所を教えてください|住所を教えて下さい",
            "としょかん" to "図書館",
            "びょういんにいきます" to "病院に行きます",
            "くすり" to "薬",
            "かいものにいく" to "買い物に行く",
            "やさいとくだもの" to "野菜と果物",
            "たのしいりょこうでした" to "楽しい旅行でした",
        )
    }
}
