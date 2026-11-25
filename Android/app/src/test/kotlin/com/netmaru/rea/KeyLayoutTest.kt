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

    @Test fun marginsAndSideGapsBelongToTheEndLetters() {
        val all = KeyLayout.keys(412f)
        val a = keys.getValue("a"); val l = keys.getValue("l"); val z = keys.getValue("z"); val m = keys.getValue("m")
        val shift = keys.getValue("⇧"); val delete = keys.getValue("⌫")
        // the half-key margins beside the home row, at any height of the row (and the gap under it)
        assertEquals("a", KeyLayout.nearest(all, 2f, a.y + 2).id)
        assertEquals("a", KeyLayout.nearest(all, 2f, a.midY).id)
        assertEquals("a", KeyLayout.nearest(all, a.x - 1, a.y + a.h + KeyLayout.ROW_GAP / 2).id)
        assertEquals("l", KeyLayout.nearest(all, 410f, l.y + 2).id)
        assertEquals("l", KeyLayout.nearest(all, 410f, l.midY).id)
        assertEquals("q", KeyLayout.nearest(all, 2f, a.y - 1).id) // the gap under q is q's
        // the gaps beside shift and delete are z's and m's; the keys themselves stay theirs
        assertEquals("z", KeyLayout.nearest(all, shift.x + shift.w + 0.5f, z.midY).id)
        assertEquals("z", KeyLayout.nearest(all, z.x - 0.5f, z.midY).id)
        assertEquals("⇧", KeyLayout.nearest(all, shift.midX, z.midY).id)
        assertEquals("⇧", KeyLayout.nearest(all, 2f, z.midY).id)
        assertEquals("m", KeyLayout.nearest(all, delete.x - 0.5f, m.midY).id)
        assertEquals("m", KeyLayout.nearest(all, m.x + m.w + 0.5f, m.midY).id)
        assertEquals("⌫", KeyLayout.nearest(all, 410f, m.midY).id)
        // curved rows too, and the side keys' outer margins stay theirs however the rows bend
        for (arc in listOf(12f, -12f)) {
            val shape = KeyShape(arc = arc)
            val curved = KeyLayout.keys(412f, shape).associateBy { it.id }
            val all2 = curved.values.toList()
            assertEquals("arc $arc", "a", KeyLayout.nearest(all2, 2f, curved.getValue("a").midY, shape).id)
            assertEquals("arc $arc", "m", KeyLayout.nearest(all2, curved.getValue("⌫").x - 0.5f, curved.getValue("m").midY, shape).id)
            assertEquals("arc $arc", "⇧", KeyLayout.nearest(all2, 2f, curved.getValue("⇧").y + 0.5f, shape).id)
            assertEquals("arc $arc", "⌫", KeyLayout.nearest(all2, 410f, curved.getValue("⌫").y + 0.5f, shape).id)
        }
        // a learned z area overlapping shift doesn't hand shift's margin to z
        val shape = KeyShape(sideKeys = 1.5f)
        val wide = KeyLayout.keys(412f, shape)
        assertEquals("⇧", KeyLayout.nearest(wide, 2f, z.midY, shape, mapOf("z" to (-10f to 0f))).id)
        assertEquals("⌫", KeyLayout.nearest(wide, 410f, m.midY, shape, mapOf("m" to (10f to 0f))).id)
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
