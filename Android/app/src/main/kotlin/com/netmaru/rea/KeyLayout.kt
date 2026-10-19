package com.netmaru.rea

import kotlin.math.hypot

/** A key's id ("q", "⇧", "⌫", "123", "switch", "space", ".", "return") and frame, in dp. */
data class KeyFrame(val id: String, val x: Float, val y: Float, val w: Float, val h: Float) {
    val midX get() = x + w / 2

    /** From a point to the key's edge (0 inside). */
    fun distance(px: Float, py: Float) =
        hypot(maxOf(x - px, 0f, px - x - w), maxOf(y - py, 0f, py - y - h))
}

/** The iOS keyboard's geometry (KeyboardViewController), in dp for pt: every key a multiple of
 *  one pitch (a tenth of the width inside the margins), rows 54 apart under a 36 candidate bar. */
object KeyLayout {
    const val GAP = 6f
    const val MARGIN = 20f / 3
    const val KEY_HEIGHT = 127f / 3
    const val ROW_GAP = 54f - KEY_HEIGHT
    const val BAR_HEIGHT = 36f
    /** iOS clips its last row this much shorter than the system keyboard's. */
    const val CLIPPED = 11f / 3
    const val HEIGHT = BAR_HEIGHT + 4 * KEY_HEIGHT + 3 * ROW_GAP - CLIPPED

    fun keys(width: Float): List<KeyFrame> {
        val pitch = (width - 2 * MARGIN + GAP) / 10
        fun w(units: Float) = units * pitch - GAP
        fun top(row: Int) = BAR_HEIGHT + row * (KEY_HEIGHT + ROW_GAP)
        // a centered run of keys, each `units` wide
        fun row(row: Int, keys: List<Pair<String, Float>>, h: Float = KEY_HEIGHT): List<KeyFrame> {
            var x = (width - keys.sumOf { it.second.toDouble() }.toFloat() * pitch + GAP) / 2
            return keys.map { (id, units) -> KeyFrame(id, x, top(row), w(units), h).also { x += units * pitch } }
        }
        fun letters(s: String) = s.map { it.toString() to 1f }
        // shift and delete at the edges, the third row's letters centered between them
        val shift = KeyFrame("⇧", MARGIN, top(2), w(1.3f), KEY_HEIGHT)
        val delete = shift.copy(id = "⌫", x = width - MARGIN - shift.w)
        return row(0, letters("qwertyuiop")) + row(1, letters("asdfghjkl")) +
            listOf(shift) + row(2, letters("zxcvbnm")) + delete +
            row(3, listOf("123" to 1.25f, "switch" to 1.25f, "space" to 4.5f, "." to 1f, "return" to 2f), KEY_HEIGHT - CLIPPED)
    }

    /** No dead zones: a touch anywhere goes to the nearest key, like iOS's KeyStack. */
    fun nearest(keys: List<KeyFrame>, x: Float, y: Float) = keys.minBy { it.distance(x, y) }

    /** What the letter keys type in 123 mode, like the system keyboard's first symbol page. */
    val symbols = "qwertyuiopasdfghjklzxcvbnm".zip("1234567890-/:;()₩&@.,?!'\"~").toMap()

    /** 2-set (두벌식) jamo on each key: lower case, then shifted where it differs. */
    val jamo = mapOf(
        'q' to ("ㅂ" to "ㅃ"), 'w' to ("ㅈ" to "ㅉ"), 'e' to ("ㄷ" to "ㄸ"), 'r' to ("ㄱ" to "ㄲ"), 't' to ("ㅅ" to "ㅆ"),
        'y' to ("ㅛ" to null), 'u' to ("ㅕ" to null), 'i' to ("ㅑ" to null), 'o' to ("ㅐ" to "ㅒ"), 'p' to ("ㅔ" to "ㅖ"),
        'a' to ("ㅁ" to null), 's' to ("ㄴ" to null), 'd' to ("ㅇ" to null), 'f' to ("ㄹ" to null), 'g' to ("ㅎ" to null),
        'h' to ("ㅗ" to null), 'j' to ("ㅓ" to null), 'k' to ("ㅏ" to null), 'l' to ("ㅣ" to null),
        'z' to ("ㅋ" to null), 'x' to ("ㅌ" to null), 'c' to ("ㅊ" to null), 'v' to ("ㅍ" to null), 'b' to ("ㅠ" to null),
        'n' to ("ㅜ" to null), 'm' to ("ㅡ" to null),
    )
}
