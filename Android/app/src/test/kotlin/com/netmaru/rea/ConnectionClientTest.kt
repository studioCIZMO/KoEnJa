package com.netmaru.rea

import android.view.inputmethod.InputConnection
import com.netmaru.rea.engine.TextRange
import org.junit.Assert.assertEquals
import org.junit.Test
import java.lang.reflect.Proxy

class ConnectionClientTest {
    /** An editor: the text before the cursor, edited the way an app does. */
    private val doc = StringBuilder()
    private val connection = Proxy.newProxyInstance(javaClass.classLoader, arrayOf(InputConnection::class.java)) { _, method, args ->
        when (method.name) {
            "getTextBeforeCursor" -> doc.takeLast(args[0] as Int).toString()
            "commitText" -> { doc.append(args[0] as CharSequence); true }
            "deleteSurroundingText" -> { doc.setLength(doc.length - (args[0] as Int)); true }
            else -> true // batch edits
        }
    } as InputConnection
    private val client = ConnectionClient { connection }

    @Test fun replacesTheRangeAndTheWord() {
        doc.append("x3")
        client.setMarkedText("じ")
        // 3ji + Space: the number and the word become 3時 (startConversion's prefixRange)
        client.setMarkedText("3時", TextRange(1, 2))
        assertEquals("x3時", doc.toString())
        client.insertText("三時", TextRange(1, 2))
        assertEquals("x三時", doc.toString())
    }
}
