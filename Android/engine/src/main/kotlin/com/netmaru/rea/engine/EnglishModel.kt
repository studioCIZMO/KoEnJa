package com.netmaru.rea.engine

import java.io.File
import kotlin.math.ln
import kotlin.math.min

/**
 * Character-trigram language model plus a word list with how common each word is, from
 * SCOWL (Data/english_words.txt: word<TAB>level, 10 = most common … 60).
 *
 * [raw]: word → level; [spelling]: the words letter trigrams are trained on (null: the list's).
 */
class EnglishModel(raw: Map<String, Int>, spelling: Sequence<String>? = null) {
    // Symbols: 0 = word boundary, 1...26 = a...z
    private val trigrams = FloatArray(SYMBOLS * SYMBOLS * SYMBOLS)
    private val contexts = FloatArray(SYMBOLS * SYMBOLS)
    /** SCOWL level of each word (lowercase, letters only). */
    private val levels = HashMap<String, Int>(raw.size * 2)
    private val smoothing = 0.5f

    init {
        val n = SYMBOLS
        for ((rawWord, level) in raw) {
            val word = rawWord.filter { it != '\'' }
            if (word.isEmpty() || !word.all { it.isAsciiLetter }) continue
            // Many 1–2 letter list entries aren't words people type ("ra", "ko"); only real ones count.
            if (word.length > 2 || word in shortWords) levels[word] = min(levels[word] ?: level, level)
        }
        for (word in spelling ?: levels.keys.asSequence()) {
            if (word.isEmpty() || !word.all { it.isAsciiLetter }) continue
            val codes = encode(word)
            for (i in 2 until codes.size) {
                trigrams[(codes[i - 2] * n + codes[i - 1]) * n + codes[i]] += 1f
                contexts[codes[i - 2] * n + codes[i - 1]] += 1f
            }
        }
    }

    val wordCount get() = levels.size

    /** Average natural-log probability per symbol (letters + end marker).
     *  Real English words land around -1.8…-2.4; keyboard mash around -3.5 or lower. */
    fun averageLogProbability(token: String): Double {
        val n = SYMBOLS
        val codes = encode(token.lowercase())
        if (codes.size <= 3) return -10.0
        var total = 0.0
        for (i in 2 until codes.size) {
            val c = trigrams[(codes[i - 2] * n + codes[i - 1]) * n + codes[i]]
            val t = contexts[codes[i - 2] * n + codes[i - 1]]
            total += ln((c + smoothing) / (t + smoothing * n.toFloat())).toDouble()
        }
        return total / (codes.size - 2)
    }

    /** The word's level, also for regular inflections of a listed word (null: not a word). */
    fun level(token: String): Int? {
        val w = token.lowercase().filter { it != '\'' }
        levels[w]?.let { return it }
        var best: Int? = null
        fun consider(stem: String) { levels[stem]?.let { l -> best = min(best ?: l, l) } }
        // Stems of 3+ letters: ths isn't the+s, ehs isn't eh+s.
        for (suffix in listOf("s", "es", "ed", "d", "ing", "ly", "er", "ers", "est")) {
            if (w.length <= suffix.length + 2 || !w.endsWith(suffix)) continue
            val stem = w.dropLast(suffix.length)
            consider(stem)
            consider(stem + "e")
            // running → run, stopped → stop
            if (stem.length >= 2 && stem[stem.length - 2] == stem.last()) consider(stem.dropLast(1))
        }
        // stories → story, denied → deny, heavier → heavy
        for (suffix in listOf("ies", "ied", "ier", "iest", "iers")) {
            if (w.length > suffix.length + 1 && w.endsWith(suffix)) consider(w.dropLast(suffix.length) + "y")
        }
        return best
    }

    /** A word people use (SCOWL level ≤ 50, or one of [commonWords]). */
    fun isKnownWord(token: String) = level(token)?.let { it <= KNOWN_LEVEL } ?: false

    /** Everyday English (level ≤ 20): kept English even right after Korean or Japanese. */
    fun isCommonWord(token: String) = level(token)?.let { it <= COMMON_LEVEL } ?: false

    companion object {
        private const val SYMBOLS = 27

        /** Words up to this level count as English words (a score floor, protection from kana). */
        const val KNOWN_LEVEL = 50
        /** Words up to this level are everyday English (you, sense, machine, for, got). */
        const val COMMON_LEVEL = 20
        /** Level given to [commonWords] (tech terms and slang SCOWL lacks): known, not "common". */
        const val EXTRA_LEVEL = 35
        /** Letter trigrams come from this list when it's there (macOS ships it, Android doesn't): its
         *  235k words separate English spelling from Korean keys better than SCOWL's 119k (measured). */
        const val SPELLING_LIST_PATH = "/usr/share/dict/words"

        /** Loading the word list takes a moment; share one instance. */
        val shared: EnglishModel by lazy { load(spellingPath = SPELLING_LIST_PATH) }

        /** From english_words.txt; a missing file leaves only [commonWords]. Trigrams are trained
         *  on [spellingPath]'s words when it can be read, otherwise on the list. */
        fun load(spellingPath: String? = null, extraWords: List<String> = commonWords): EnglishModel {
            val levels = HashMap<String, Int>()
            Resources.lines("english_words.txt") { lines ->
                for (line in lines) {
                    val parts = line.split('\t')
                    val level = parts.getOrNull(1)?.toIntOrNull()
                    if (parts.size == 2 && level != null) levels[parts[0]] = level
                }
            }
            for (word in extraWords) levels[word.lowercase()] = min(levels[word.lowercase()] ?: EXTRA_LEVEL, EXTRA_LEVEL)
            val file = spellingPath?.let(::File)?.takeIf { it.canRead() }
                ?: return EnglishModel(levels)
            return file.bufferedReader().useLines { lines ->
                EnglishModel(levels, lines.filter(String::isNotEmpty).map { it.lowercase() } + extraWords.asSequence())
            }
        }

        /** [^, ^, letters..., $] with ^ and $ both encoded as 0. */
        private fun encode(word: String): IntArray {
            val codes = IntArray(word.length + 3)
            var count = 2
            for (ch in word) if (ch in 'a'..'z') codes[count++] = ch - 'a' + 1
            return codes.copyOf(count + 1)
        }

        val shortWords: Set<String> = words("""
            a i ad ah am an as at aw ax be by do eh go ha he hi hm if in is it lo ma me mo my no of oh ok on or ow ox pa
            pc so to tv uh um up us we ya ye yo id ex vs ai ui ux os js ts
        """).toHashSet()

        /** Everyday/technical words SCOWL is missing or ranks rare. */
        val commonWords: List<String> = words("""
            ok okay hi hey yeah yep nope lol omg btw thx thanks pls please email emails app apps api apis url urls
            http https www github git commit push pull merge branch repo json yaml xml html css js ts swift python java
            kotlin rust golang npm yarn pnpm node react vue svelte docker kubernetes ios macos android iphone ipad mac wifi bluetooth usb
            login logout signup username password online offline website blog vlog youtube google slack notion figma
            debug deploy refactor config backend frontend database cache server client async await todo todos ai llm gpt
            claude chatgpt laptop smartphone selfie emoji internet podcast startup dev devs devops ui ux
            wanna gonna gotta kinda sorta outta lemme gimme dunno yeah yup nah ain cuz tho thru hiya kiddo sayin
            goin doin nothin somethin y'all ya yo hey oi aye
            wouldn couldn shouldn didn doesn isn wasn weren hasn haven hadn needn mustn aren mightn
            makeup brooklyn shakespeare einstein techno pesto americano cosmo promo garde kanji kana romaji ramen
        """)
    }
}

/** Whitespace-separated words of a word-list literal. */
internal fun words(text: String): List<String> = text.split(' ', '\n').filter(String::isNotBlank).map(String::trim)
