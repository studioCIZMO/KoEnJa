package com.netmaru.rea.engine

/**
 * Hangul jamo tables and the 2-set (두벌식) QWERTY layout.
 *
 * All jamo are represented as Hangul Compatibility Jamo (U+3131…U+3163),
 * which is also what gets emitted for stray, uncombined jamo.
 */
object Jamo {
    private const val CHOSEONG = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ"
    private const val JUNGSEONG = "ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ"
    /** Index + 1 is the final's index (0 is "no final consonant"). */
    private const val JONGSEONG = "ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ"

    val vowelCombos: Map<String, Char> = mapOf(
        "ㅗㅏ" to 'ㅘ', "ㅗㅐ" to 'ㅙ', "ㅗㅣ" to 'ㅚ',
        "ㅜㅓ" to 'ㅝ', "ㅜㅔ" to 'ㅞ', "ㅜㅣ" to 'ㅟ',
        "ㅡㅣ" to 'ㅢ',
    )

    val finalCombos: Map<String, Char> = mapOf(
        "ㄱㅅ" to 'ㄳ', "ㄴㅈ" to 'ㄵ', "ㄴㅎ" to 'ㄶ',
        "ㄹㄱ" to 'ㄺ', "ㄹㅁ" to 'ㄻ', "ㄹㅂ" to 'ㄼ', "ㄹㅅ" to 'ㄽ', "ㄹㅌ" to 'ㄾ', "ㄹㅍ" to 'ㄿ', "ㄹㅎ" to 'ㅀ',
        "ㅂㅅ" to 'ㅄ',
    )

    /** Inverse of [finalCombos]: compound final → (stays as final, moves to next syllable). */
    val finalSplits: Map<Char, Pair<Char, Char>> = finalCombos.entries.associate { (pair, combined) -> combined to (pair[0] to pair[1]) }

    fun isConsonant(j: Char) = CHOSEONG.indexOf(j) >= 0 || JONGSEONG.indexOf(j) >= 0
    fun isVowel(j: Char) = JUNGSEONG.indexOf(j) >= 0
    fun isFinal(j: Char) = JONGSEONG.indexOf(j) >= 0

    fun syllable(cho: Char, jung: Char, jong: Char?): Char {
        val c = CHOSEONG.indexOf(cho)
        val v = JUNGSEONG.indexOf(jung)
        val f = jong?.let { JONGSEONG.indexOf(it) + 1 } ?: 0
        return (0xAC00 + (c * 21 + v) * 28 + f).toChar()
    }
}

object DubeolsikLayout {
    private val lower = mapOf(
        'q' to 'ㅂ', 'w' to 'ㅈ', 'e' to 'ㄷ', 'r' to 'ㄱ', 't' to 'ㅅ', 'y' to 'ㅛ', 'u' to 'ㅕ', 'i' to 'ㅑ', 'o' to 'ㅐ', 'p' to 'ㅔ',
        'a' to 'ㅁ', 's' to 'ㄴ', 'd' to 'ㅇ', 'f' to 'ㄹ', 'g' to 'ㅎ', 'h' to 'ㅗ', 'j' to 'ㅓ', 'k' to 'ㅏ', 'l' to 'ㅣ',
        'z' to 'ㅋ', 'x' to 'ㅌ', 'c' to 'ㅊ', 'v' to 'ㅍ', 'b' to 'ㅠ', 'n' to 'ㅜ', 'm' to 'ㅡ',
    )
    private val shifted = mapOf(
        'Q' to 'ㅃ', 'W' to 'ㅉ', 'E' to 'ㄸ', 'R' to 'ㄲ', 'T' to 'ㅆ', 'O' to 'ㅒ', 'P' to 'ㅖ',
    )

    /** Keys whose shifted form produces a different jamo. Shift on any other
     *  letter is a signal the user is typing English. */
    fun isKoreanShiftKey(key: Char) = key in shifted

    /** A capital on a key with no shifted jamo, not counting a lingering Shift: Shift still down
     *  for the one key right after a Korean shift key (어떤 typed fast is djEJs, 바뀌 is qkRNl:
     *  Shift was still down for the vowel after ㄸ or ㄲ). Such keys read as Korean or English by
     *  their scores; any other capital (Hello, API, dHello) is only ever English. A Shift lingers
     *  between lowercase keys, so a word typed in capitals throughout (TM, RN, TL: acronyms) gets
     *  no such allowance, nor does ㅒ/ㅖ with no consonant before it (PDF, OAuth).
     *  Not ported: LatinLayout's seats (QWERTY only) and 3-set. */
    fun hasEnglishOnlyCapital(keys: String): Boolean {
        val hasLowercase = keys.any { it.isLowerCase() }
        for ((i, key) in keys.withIndex()) {
            if (!key.isUpperCase() || key in shifted) continue
            if (hasLowercase && i >= 1 && lingers(keys[i - 1], keys.getOrNull(i - 2), key)) continue
            return true
        }
        return false
    }

    /** Shift still down for [key], right after [shiftKey], a Korean shift key that needed it. After
     *  a double consonant comes a vowel (djEJs, 어떤), or, when the double consonant is a final one
     *  (a vowel key before it), the next syllable's consonant (dlTDj, 있어). After ㅒ or ㅖ typed on a
     *  consonant comes a consonant (dPFmf, 예를); at the start of a word (PDF, OAuth) nothing lingers.
     *  The key before has to be lowercase: a capital there (RPA, TPS) was Shift on purpose. */
    private fun lingers(shiftKey: Char, twoBack: Char?, key: Char): Boolean {
        val before = twoBack?.takeIf { it.isLowerCase() }?.let(::jamo)
        return when (shifted[shiftKey]) {
            'ㅃ', 'ㅉ', 'ㄸ', 'ㄲ', 'ㅆ' -> isVowelKey(key) || (before?.let(Jamo::isVowel) == true && isConsonantKey(key))
            'ㅒ', 'ㅖ' -> before?.let(Jamo::isConsonant) == true && isConsonantKey(key)
            else -> false
        }
    }

    private fun isVowelKey(key: Char) = jamo(key)?.let(Jamo::isVowel) ?: false
    private fun isConsonantKey(key: Char) = jamo(key)?.let(Jamo::isConsonant) ?: false

    /** The jamo a key produces, or null for non-letters.
     *  Uppercase letters without a shifted jamo fall back to the lowercase jamo,
     *  mirroring how the macOS 2-set keyboard behaves. */
    fun jamo(key: Char): Char? {
        shifted[key]?.let { return it }
        if (!key.isAscii) return null
        return lower[key.lowercaseChar()]
    }
}

internal val Char.isAscii get() = code < 128
internal val Char.isAsciiLetter get() = isAscii && isLetter()
internal val Char.isAsciiDigit get() = this in '0'..'9'
/** Swift's Character.isNumber: digits, and other numerals (½, ①). */
internal val Char.isNumber get() = Character.getType(this).let {
    it == Character.DECIMAL_DIGIT_NUMBER.toInt() || it == Character.LETTER_NUMBER.toInt() || it == Character.OTHER_NUMBER.toInt()
}
