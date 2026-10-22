package com.netmaru.rea.engine

import kotlin.math.min

/**
 * Romaji → hiragana, following the conventions of the macOS and Microsoft
 * Japanese input methods (nn/n' → ん, doubled consonants → っ, x/l small kana,
 * - → ー). Letters that can't become kana are kept and counted: like stray jamo
 * for Hangul, they are the strongest sign the user wasn't typing Japanese.
 */
object RomajiComposer {
    data class Result(
        /** Kana with any unconvertible letters left in place (こんにc while typing). */
        val text: String,
        /** Letters in the middle or (when finished) at the end that never became kana. */
        val strayLetters: Int,
        /** Letters at the end still waiting to become kana ("k", "ky", "n"); only when partial. */
        val pending: String,
        /** Number of kana produced. */
        val kanaCount: Int,
    ) {
        val isClean get() = strayLetters == 0 && pending.isEmpty()
        /** Only っ and letters (ff: っf, sss: っっs): a doubled consonant at the start with no
         *  kana after it, which no Japanese word is (って has kana after its っ). */
        val isOnlySokuon get() = text.firstOrNull() == 'っ' && text.all { it == 'っ' || it.isAsciiLetter }
    }

    /** [partial]: the word is still being typed, so a trailing "k" or "n" is waiting for its vowel rather than stray. */
    fun compose(keys: String, partial: Boolean = false): Result {
        // Matched in lowercase; letters that don't become kana stay as typed (⇧P waiting for its
        // vowel shows P, not p).
        val typed = keys
        val chars = String(CharArray(typed.length) { typed[it].let { c -> if (c.isAscii) c.lowercaseChar() else c } })
        val out = StringBuilder()
        var stray = 0
        var kana = 0
        var pending = ""
        var i = 0

        fun emit(s: String) { out.append(s); kana += s.length }

        while (i < chars.length) {
            val c = chars[i]
            val next = chars.getOrNull(i + 1)

            // Digits are full-width, and so is the punctuation inside a number (０３－１２３４, ３．５).
            fullWidthDigits[c]?.let { emit(it); i += 1; continue }
            if (i > 0 && chars[i - 1].isAsciiDigit) {
                numberPunctuation[c]?.let { emit(it); i += 1; continue }
            }
            if (c == 'n') {
                if (next != null) {
                    if (next == '\'') { emit("ん"); i += 2; continue }
                    if (next == 'n') {
                        // "nni" → んに, "nn" → ん
                        val after = chars.getOrNull(i + 2)
                        emit("ん")
                        i += if (after != null && (after in vowels || after == 'y')) 1 else 2
                        continue
                    }
                    if (next !in vowels && next != 'y') { emit("ん"); i += 1; continue }
                } else {
                    if (partial) pending = typed[i].toString() else emit("ん")
                    i += 1
                    continue
                }
            } else if (next != null && next == c && c in consonants) {
                emit("っ"); i += 1; continue
            } else if (c == 't' && next == 'c' && i + 2 < chars.length && chars[i + 2] == 'h') {
                emit("っ"); i += 1; continue // matcha: "tch" → っch
            }

            var matched = false
            for (length in min(4, chars.length - i) downTo 1) {
                val k = table[chars.substring(i, i + length)] ?: continue
                emit(k)
                i += length
                matched = true
                break
            }
            if (matched) continue

            if (partial && chars.substring(i) in prefixes) {
                pending = typed.substring(i)
                break
            }
            out.append(typed[i])
            stray += 1
            i += 1
        }
        return Result(out.toString() + pending, stray, pending, kana)
    }

    /** Hiragana → katakana (for F7-style katakana output and loanwords). */
    fun katakana(hiragana: String): String =
        String(CharArray(hiragana.length) { hiragana[it].let { c -> if (c.code in 0x3041..0x3096) c + 0x60 else c } })

    /** A spelling that composes back to [kana] (し → "si", ん → "n'", っ → "ltu"), or null if
     *  some character has none. For keys rewritten after Delete took off a whole kana. */
    fun romaji(kana: String): String? {
        val out = StringBuilder()
        for (ch in kana) out.append(spelling[ch] ?: return null)
        return out.toString()
    }

    /** Symbols as Japanese IMEs type them by default: full-width, with
     *  [ ] / ~ - as 「」・〜ー. Not ' (it ends ん in kan'i), and digits and spaces stay half-width. */
    val symbols: Map<Char, String> = mapOf(
        ',' to "、", '.' to "。", '?' to "？", '!' to "！", '-' to "ー", '/' to "・", '[' to "「", ']' to "」", '~' to "〜",
        '_' to "＿", '(' to "（", ')' to "）", ':' to "：", ';' to "；", '"' to "”", '@' to "＠", '#' to "＃", '$' to "＄",
        '%' to "％", '&' to "＆", '*' to "＊", '+' to "＋", '=' to "＝", '<' to "＜", '>' to "＞", '{' to "｛", '}' to "｝",
        '|' to "｜", '^' to "＾", '`' to "｀", '\\' to "＼",
    )

    /** What the symbols become, except ー, which is part of a word (らーめん) rather than punctuation. */
    val punctuation: Set<Char> = symbols.values.map { it[0] }.toSet() - 'ー'

    /** 0–9 as ０–９, for a Japanese phrase with a number in it. */
    val fullWidthDigits: Map<Char, String> = ('0'..'9').associateWith { (it - '0' + 0xFF10).toChar().toString() }
    /** After a digit, - . , / belong to the number (addresses, phone numbers, dates), not ー。、・. */
    val numberPunctuation: Map<Char, String> = mapOf('-' to "－", '.' to "．", ',' to "，", '/' to "／")

    private val vowels = setOf('a', 'i', 'u', 'e', 'o')
    private val consonants = "bcdfghjklmpqrstvwxyz".toSet()

    val table: Map<String, String> = HashMap<String, String>().apply {
        putAll(mapOf(
            "a" to "あ", "i" to "い", "u" to "う", "e" to "え", "o" to "お",
            "ka" to "か", "ki" to "き", "ku" to "く", "ke" to "け", "ko" to "こ",
            "ga" to "が", "gi" to "ぎ", "gu" to "ぐ", "ge" to "げ", "go" to "ご",
            "sa" to "さ", "si" to "し", "su" to "す", "se" to "せ", "so" to "そ",
            "za" to "ざ", "zi" to "じ", "zu" to "ず", "ze" to "ぜ", "zo" to "ぞ",
            "ta" to "た", "ti" to "ち", "tu" to "つ", "te" to "て", "to" to "と",
            "da" to "だ", "di" to "ぢ", "du" to "づ", "de" to "で", "do" to "ど",
            "na" to "な", "ni" to "に", "nu" to "ぬ", "ne" to "ね", "no" to "の",
            "ha" to "は", "hi" to "ひ", "hu" to "ふ", "he" to "へ", "ho" to "ほ",
            "ba" to "ば", "bi" to "び", "bu" to "ぶ", "be" to "べ", "bo" to "ぼ",
            "pa" to "ぱ", "pi" to "ぴ", "pu" to "ぷ", "pe" to "ぺ", "po" to "ぽ",
            "ma" to "ま", "mi" to "み", "mu" to "む", "me" to "め", "mo" to "も",
            "ya" to "や", "yi" to "い", "yu" to "ゆ", "ye" to "いぇ", "yo" to "よ",
            "ra" to "ら", "ri" to "り", "ru" to "る", "re" to "れ", "ro" to "ろ",
            "wa" to "わ", "wi" to "うぃ", "wu" to "う", "we" to "うぇ", "wo" to "を",
            "fa" to "ふぁ", "fi" to "ふぃ", "fu" to "ふ", "fe" to "ふぇ", "fo" to "ふぉ",
            "va" to "ゔぁ", "vi" to "ゔぃ", "vu" to "ゔ", "ve" to "ゔぇ", "vo" to "ゔぉ",
            "qa" to "くぁ", "qi" to "くぃ", "qu" to "く", "qe" to "くぇ", "qo" to "くぉ",
            "shi" to "し", "chi" to "ち", "tsu" to "つ", "ji" to "じ",
            "sha" to "しゃ", "shu" to "しゅ", "she" to "しぇ", "sho" to "しょ",
            "cha" to "ちゃ", "chu" to "ちゅ", "che" to "ちぇ", "cho" to "ちょ",
            "ja" to "じゃ", "ju" to "じゅ", "je" to "じぇ", "jo" to "じょ",
            "tsa" to "つぁ", "tsi" to "つぃ", "tse" to "つぇ", "tso" to "つぉ",
            "tha" to "てゃ", "thi" to "てぃ", "thu" to "てゅ", "the" to "てぇ", "tho" to "てょ",
            "dha" to "でゃ", "dhi" to "でぃ", "dhu" to "でゅ", "dhe" to "でぇ", "dho" to "でょ",
            "twu" to "とぅ", "dwu" to "どぅ",
            "wha" to "うぁ", "whi" to "うぃ", "whu" to "う", "whe" to "うぇ", "who" to "うぉ",
            "xa" to "ぁ", "xi" to "ぃ", "xu" to "ぅ", "xe" to "ぇ", "xo" to "ぉ",
            "la" to "ぁ", "li" to "ぃ", "lu" to "ぅ", "le" to "ぇ", "lo" to "ぉ",
            "xya" to "ゃ", "xyu" to "ゅ", "xyo" to "ょ", "lya" to "ゃ", "lyu" to "ゅ", "lyo" to "ょ",
            "xtu" to "っ", "ltu" to "っ", "xtsu" to "っ", "ltsu" to "っ", "xwa" to "ゎ", "lwa" to "ゎ",
            // As macOS and MS-IME accept them (not ca/ci/cu: they made English words read as romaji).
            "kwa" to "くぁ", "gwa" to "ぐぁ", "xn" to "ん",
        ))
        for ((symbol, full) in symbols) put(symbol.toString(), full)
        // Contracted sounds: きゃ, ぎゅ, みょ…
        val rows = listOf("k" to "き", "g" to "ぎ", "s" to "し", "z" to "じ", "t" to "ち", "d" to "ぢ",
                          "n" to "に", "h" to "ひ", "b" to "び", "p" to "ぴ", "m" to "み", "r" to "り",
                          "c" to "ち", "j" to "じ", "f" to "ふ")
        for ((consonant, i) in rows) {
            put(consonant + "ya", i + "ゃ")
            put(consonant + "yi", i + "ぃ")
            put(consonant + "yu", i + "ゅ")
            put(consonant + "ye", i + "ぇ")
            put(consonant + "yo", i + "ょ")
        }
    }

    /** The shortest table entry for each single kana (ties: alphabetical); ん as "n'" so a
     *  vowel typed after it can't take it over (n'a is んあ, nna would be んな). */
    private val spelling: Map<Char, String> = HashMap<Char, String>().apply {
        put('ん', "n'")
        for ((key, value) in table) {
            if (value.length != 1) continue
            val ch = value[0]
            val old = get(ch)
            if (old != null && (old.length < key.length || (old.length == key.length && old < key))) continue
            if (ch != 'ん') put(ch, key)
        }
    }

    /** Every proper prefix of a table entry: what a word may end in while still being typed. */
    private val prefixes: Set<String> = HashSet<String>().apply {
        add("n")
        for (key in table.keys) for (length in 1 until key.length) add(key.take(length))
    }
}
