package com.netmaru.rea.engine

import java.io.File
import java.io.InputStream

/**
 * Where the models' data files (Packages/SeamtypeCore/Sources/SeamtypeCore/Data) come from:
 * english_words.txt, kana_trigrams.txt, korean_words.txt,
 * korean_vocabulary.txt, korean_syllables.txt. Set [open] before the first model loads
 * (the app points it at its assets); by default it reads the directory named by the
 * `rea.data` system property (the engine's tests set it to the source tree).
 */
object Resources {
    var open: (String) -> InputStream? = { name ->
        System.getProperty("rea.data")?.let { File(it, name) }?.takeIf { it.isFile }?.inputStream()
    }

    /** Reads the file's non-empty lines with [block]; null when the file isn't there. */
    internal fun <T> lines(name: String, block: (Sequence<String>) -> T): T? =
        open(name)?.bufferedReader()?.useLines { block(it.filter(String::isNotEmpty)) }
}
