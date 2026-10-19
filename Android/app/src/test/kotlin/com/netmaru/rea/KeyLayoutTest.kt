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
        assertEquals("a", KeyLayout.nearest(all, keys.getValue("a").midX, q.y + q.h + KeyLayout.ROW_GAP * 0.6f).id)
    }
}
