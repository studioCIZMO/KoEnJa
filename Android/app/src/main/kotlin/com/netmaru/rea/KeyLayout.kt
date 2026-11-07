package com.netmaru.rea

import kotlin.math.hypot

/** A key's id ("q", "⇧", "⌫", "123", "switch", "space", ".", "return") and frame, in dp. */
data class KeyFrame(val id: String, val x: Float, val y: Float, val w: Float, val h: Float) {
    val midX get() = x + w / 2

    /** From a point to the key's edge (0 inside). */
    fun distance(px: Float, py: Float) =
        hypot(maxOf(x - px, 0f, px - x - w), maxOf(y - py, 0f, py - y - h))
}

/** The keyboard's shape, from the settings (KeyShape.SETTINGS); the defaults are the iOS keyboard's.
 *  [height] scales the keys' height, [bottom] is space under the last row, [arc] lowers the
 *  edges (above 0) or the middle (below 0) by up to that much, curving the rows for the thumbs. */
data class KeyShape(
    val height: Float = 1f, val bottom: Float = 0f, val arc: Float = 0f, val gap: Float = KeyLayout.GAP,
    val rowGap: Float = KeyLayout.ROW_GAP, val radius: Float = 6f, val sideKeys: Float = 1.3f, val hue: Int = -1,
) {
    val keyHeight get() = KeyLayout.KEY_HEIGHT * height
    /** The keyboard's height under the bar, in dp. */
    val total get() = KeyLayout.BAR_HEIGHT + 4 * keyHeight + 3 * rowGap - KeyLayout.CLIPPED + kotlin.math.abs(arc) + bottom

    companion object {
        /** Settings key, label range (min, max, step) and default, in the order the settings list them. */
        val SETTINGS = listOf(
            Triple("kbHeight", Triple(0.8f, 1.3f, 0.05f), 1f), Triple("kbBottom", Triple(0f, 48f, 2f), 0f),
            Triple("kbArc", Triple(-12f, 12f, 1f), 0f), Triple("kbGap", Triple(2f, 12f, 1f), KeyLayout.GAP),
            Triple("kbRowGap", Triple(4f, 20f, 1f), KeyLayout.ROW_GAP), Triple("kbRadius", Triple(0f, 20f, 1f), 6f),
            Triple("kbSideKeys", Triple(1f, 1.5f, 0.05f), 1.3f),
        )

        fun of(p: android.content.SharedPreferences) = KeyShape(
            p.getFloat("kbHeight", 1f), p.getFloat("kbBottom", 0f), p.getFloat("kbArc", 0f), p.getFloat("kbGap", KeyLayout.GAP),
            p.getFloat("kbRowGap", KeyLayout.ROW_GAP), p.getFloat("kbRadius", 6f), p.getFloat("kbSideKeys", 1.3f), p.getInt("kbHue", -1),
        )
    }
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

    fun keys(width: Float, shape: KeyShape = KeyShape()): List<KeyFrame> {
        val gap = shape.gap
        val keyHeight = shape.keyHeight
        val pitch = (width - 2 * MARGIN + gap) / 10
        fun w(units: Float) = units * pitch - gap
        fun top(row: Int) = BAR_HEIGHT + row * (keyHeight + shape.rowGap)
        // a centered run of keys, each `units` wide
        fun row(row: Int, keys: List<Pair<String, Float>>, h: Float = keyHeight): List<KeyFrame> {
            var x = (width - keys.sumOf { it.second.toDouble() }.toFloat() * pitch + gap) / 2
            return keys.map { (id, units) -> KeyFrame(id, x, top(row), w(units), h).also { x += units * pitch } }
        }
        fun letters(s: String) = s.map { it.toString() to 1f }
        // shift and delete at the edges, the third row's letters centered between them
        val side = minOf(shape.sideKeys, 1.5f) // the letters start 1.5 pitches in: never into z or m
        val shift = KeyFrame("⇧", MARGIN, top(2), w(side), keyHeight)
        val delete = shift.copy(id = "⌫", x = width - MARGIN - shift.w)
        val flat = row(0, letters("qwertyuiop")) + row(1, letters("asdfghjkl")) +
            listOf(shift) + row(2, letters("zxcvbnm")) + delete +
            row(3, listOf("123" to 1.25f, "switch" to 1.25f, "space" to 4.5f, "." to 1f, "return" to 2f), keyHeight - CLIPPED)
        if (shape.arc == 0f) return flat
        // The arc: each key down by its distance from the middle (edges) or the edge (middle), squared.
        return flat.map { key ->
            val d = (2 * key.midX / width - 1).let { it * it }
            key.copy(y = key.y + if (shape.arc > 0) shape.arc * d else -shape.arc * (1 - d))
        }
    }

    /** The key a touch types, as Gboard decides (measured on a Pixel emulator): the gap between
     *  two keys is split in the middle, the gap under a key is that key's (a thumb lands a little
     *  low), and a touch anywhere else goes to the nearest key, so there are no dead zones. */
    fun nearest(keys: List<KeyFrame>, x: Float, y: Float, shape: KeyShape = KeyShape()) =
        // a key touched where it is drawn first: with the rows curved, a gap below one key can lie over the next
        keys.firstOrNull { it.distance(x, y) == 0f } ?: keys.minBy {
            it.copy(x = it.x - shape.gap / 2, w = it.w + shape.gap, h = it.h + shape.rowGap).distance(x, y)
        }

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
