package com.netmaru.rea

import org.junit.Assert.assertEquals
import org.junit.Test

class KeyLayoutTest {
    private val keys = KeyLayout.keys(412f).associateBy { it.id }

    @Test fun framesMatchIos() {
        assertEquals(33, keys.size)
        val pitch = (412f - 2 * KeyLayout.MARGIN + KeyLayout.GAP) / 10
        assertEquals(KeyLayout.MARGIN, keys.getValue("q").x, 1e-3f)
        assertEquals(pitch - KeyLayout.GAP, keys.getValue("q").w, 1e-3f)
        keys.getValue("p").let { assertEquals(412f - KeyLayout.MARGIN, it.x + it.w, 1e-3f) }
        keys.getValue("return").let { assertEquals(412f - KeyLayout.MARGIN, it.x + it.w, 1e-3f) }
        keys.getValue("⌫").let { assertEquals(412f - KeyLayout.MARGIN, it.x + it.w, 1e-3f) }
        assertEquals(4.5f * pitch - KeyLayout.GAP, keys.getValue("space").w, 1e-3f)
        // a and z rows centered; the bottom row clipped
        keys.getValue("a").let { assertEquals(KeyLayout.MARGIN + pitch / 2, it.x, 1e-3f) }
        assertEquals(206f, keys.getValue("v").midX, 1e-3f)
        assertEquals(KeyLayout.KEY_HEIGHT - KeyLayout.CLIPPED, keys.getValue("space").h, 1e-3f)
        assertEquals(KeyLayout.HEIGHT, keys.getValue("space").let { it.y + it.h }, 1e-3f)
    }

    @Test fun gapsGoToTheNearestKey() {
        val all = KeyLayout.keys(412f)
        val q = keys.getValue("q")
        assertEquals("w", KeyLayout.nearest(all, q.x + q.w + KeyLayout.GAP * 0.9f, q.y + 5).id)
        assertEquals("q", KeyLayout.nearest(all, 0f, 0f).id) // the margin above-left
        // Gboard's: the gap under a row is the row's (a thumb lands low); the next row starts at its keys' top
        val s = keys.getValue("s")
        assertEquals("w", KeyLayout.nearest(all, s.midX - 4, q.y + q.h + KeyLayout.ROW_GAP * 0.9f).id)
        assertEquals("s", KeyLayout.nearest(all, s.midX, s.y + 0.5f).id)
    }

    @Test fun curvedRowsTypeTheKeyTouched() {
        for (arc in listOf(12f, -12f)) {
            val shape = KeyShape(arc = arc)
            val all = KeyLayout.keys(412f, shape)
            for (key in all) assertEquals("arc $arc", key.id, KeyLayout.nearest(all, key.midX, key.y + key.h - 0.5f, shape).id)
        }
        val shape = KeyShape(arc = 12f)
        val all = KeyLayout.keys(412f, shape)
        assertEquals("space", KeyLayout.nearest(all, 125.44f, 198.13f, shape).id)
        assertEquals("a", KeyLayout.nearest(all, 30.35f, 97.51f, shape).id)
    }

    @Test fun learnedKeysMoveTheirTouchArea() {
        val all = KeyLayout.keys(412f)
        val n = all.first { it.id == "n" }; val m = all.first { it.id == "m" }
        val between = (n.x + n.w + m.x) / 2 - 2 // just on n's side of the gap
        assertEquals("n", KeyLayout.nearest(all, between, n.y + 10).id)
        // this person touches m 6 dp left of its center: the gap's left side is m's now
        assertEquals("m", KeyLayout.nearest(all, between, n.y + 10, offsets = mapOf("m" to (-6f to 0f))).id)
    }
}
