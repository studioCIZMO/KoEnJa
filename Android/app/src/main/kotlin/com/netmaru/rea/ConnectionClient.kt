package com.netmaru.rea

import android.view.inputmethod.InputConnection
import com.netmaru.rea.engine.TextClient
import com.netmaru.rea.engine.TextRange

/** The engine's TextClient on an InputConnection, as iOS's ProxyClient on UITextDocumentProxy.
 *
 *  The word being composed goes in as ordinary text, not composing text: fields can drop or
 *  commit composing text on their own when the focus moves (iOS lost a name typed in Safari
 *  that way), and each app underlines it differently. Each change deletes the end of the word
 *  that differs and types the new end, like the system's Korean keyboards.
 *
 *  Ranges are measured in the text before the cursor (getTextBeforeCursor). InputSession only
 *  replaces ranges that end at the cursor, and those are deleted back from it.
 *  ponytail: CONTEXT chars of it; past that, ranges remembered from earlier keys shift (as on
 *  iOS, whose context is shorter); use the absolute cursor (onUpdateSelection) if that bites. */
class ConnectionClient(private val connection: () -> InputConnection?) : TextClient {
    /** The word being composed, already in the document before the cursor. */
    var marked = ""
        private set

    fun before(): String? = connection()?.getTextBeforeCursor(CONTEXT, 0)?.toString()

    /** Committed text before the cursor. */
    private fun committed(): String {
        val before = before().orEmpty()
        return if (marked.isNotEmpty() && before.endsWith(marked)) before.dropLast(marked.length) else before
    }

    override fun setMarkedText(text: String) {
        replaceMarked(text, typingOn = true)
        marked = text
    }

    override fun setMarkedText(text: String, replacing: TextRange) = edit {
        deleteBack(replacing)
        setMarkedText(text)
    }

    override fun insertText(text: String, replacing: TextRange?) = edit {
        if (replacing != null) {
            deleteBack(replacing)
            connection()?.commitText(text, 1)
        } else {
            replaceMarked(text) // the word turns into the committed text
        }
    }

    override fun selectedRange() = TextRange(committed().length + marked.length, 0)

    override fun markedRange() =
        if (marked.isEmpty()) TextRange(TextRange.NOT_FOUND, 0) else TextRange(committed().length, marked.length)

    override fun text(range: TextRange): String? {
        val all = committed() + marked
        if (range.location < 0 || range.location > all.length || range.length < 0 || range.length > all.length - range.location) return null
        return all.substring(range.location, range.end)
    }

    /** After the app says the selection changed: whether the word being composed is no longer
     *  right before the cursor (the user moved it, an undo, autofill). Then it's forgotten as typed.
     *  ponytail: a suffix check; the cursor moved to right after the same text isn't noticed. */
    fun resetIfMovedAway(): Boolean {
        if (marked.isEmpty() || before()?.endsWith(marked) != false) return false
        marked = ""
        return true
    }

    /** Turns the word on screen into [text], touching only the end that differs (거 → 건: one
     *  delete, one insert). If the text before the cursor no longer ends with the word, it's left
     *  alone; [text] is still typed if the user is typing on, but not for a commit. */
    private fun replaceMarked(text: String, typingOn: Boolean = false) {
        val ic = connection()
        val word = marked
        marked = ""
        if (ic == null) return
        if (word.isEmpty() || before()?.endsWith(word) != true) {
            if (text.isNotEmpty() && (word.isEmpty() || typingOn)) ic.commitText(text, 1)
            return
        }
        val same = word.commonPrefixWith(text).length // never splits a surrogate pair
        ic.beginBatchEdit()
        if (word.length > same) ic.deleteSurroundingText(word.length - same, 0)
        if (text.length > same) ic.commitText(text.substring(same), 1)
        ic.endBatchEdit()
    }

    /** Deletes [range], which ends at the cursor, the word being composed included (3じ before 3時):
     *  measured before the word goes, which shortens the text it's measured in. */
    private fun deleteBack(range: TextRange) {
        val length = text(range)?.length ?: 0
        val word = marked.length
        replaceMarked("")
        if (length > word) connection()?.deleteSurroundingText(length - word, 0)
    }

    private fun edit(block: () -> Unit) {
        val ic = connection()
        ic?.beginBatchEdit()
        block()
        ic?.endBatchEdit()
    }

    private companion object {
        const val CONTEXT = 4000
    }
}
