package com.netmaru.rea.japanese

import java.io.File
import kotlin.test.Test

/** Top-1 and top-10 sentence accuracy on "kana\tkanji" lines (REA_EVAL=<file>; REA_EVAL_DIC=<mozc.dic.gz>,
 *  the bundled one by default). Skipped without REA_EVAL. */
class ConversionEval {
    @Test fun eval() {
        val lines = System.getenv("REA_EVAL")?.let { File(it).readLines() } ?: return
        val dic = System.getenv("REA_EVAL_DIC")?.ifEmpty { null }
        val converter = if (dic != null) MozcConverter({ File(dic).inputStream() }) else MozcConverter()
        var top1 = 0; var top10 = 0; var nanos = 0L
        for (line in lines) {
            val (kana, kanji) = line.split('\t')
            val start = System.nanoTime()
            val texts = converter.candidates(kana).filter { it.kanaCount == kana.length }.map { it.text }
            nanos += System.nanoTime() - start
            if (texts.firstOrNull() == kanji) top1++
            if (kanji in texts.take(10)) top10++
        }
        println("EVAL ${dic ?: "bundled"}: top1 $top1/${lines.size} (${top1 * 1000 / lines.size / 10.0}%), " +
            "top10 $top10/${lines.size} (${top10 * 1000 / lines.size / 10.0}%), ${nanos / lines.size / 1000} us/sentence")
    }
}
