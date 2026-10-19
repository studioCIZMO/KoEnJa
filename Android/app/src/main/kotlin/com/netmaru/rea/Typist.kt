package com.netmaru.rea

import android.inputmethodservice.InputMethodService

/** What the keys type; shift and 123 are the keyboard's own. */
sealed interface Key {
    data class Char(val char: kotlin.Char) : Key
    data object Space : Key
    data object Delete : Key
    data object Return : Key
    /** 한あA: the word just typed in the next language. */
    data object Cycle : Key
}

/** A word in the bar above the keys; `selected` is highlighted (the one Space/Return takes). */
data class Candidate(val text: String, val selected: Boolean = false)

/** The seam between the keyboard view and the text: phase 2 puts the engine's InputSession
 *  behind it (with an InputConnection-backed TextClient); the view doesn't change. */
interface Typist {
    fun tap(key: Key)
    val candidates: List<Candidate>
    fun choose(index: Int)
    /** The field is going away or the cursor moved: finish the word being typed. */
    fun finish()
}

/** Phase 1: every key goes straight to the app. */
class PassthroughTypist(private val service: InputMethodService) : Typist {
    override fun tap(key: Key) {
        val ic = service.currentInputConnection ?: return
        when (key) {
            is Key.Char -> ic.commitText(key.char.toString(), 1)
            Key.Space -> ic.commitText(" ", 1)
            Key.Delete -> if (ic.getSelectedText(0).isNullOrEmpty()) ic.deleteSurroundingTextInCodePoints(1, 0) else ic.commitText("", 1)
            // the field's action (Go, Send…) if it has one, else a new line
            Key.Return -> if (!service.sendDefaultEditorAction(true)) service.sendKeyChar('\n')
            Key.Cycle -> {}
        }
    }

    override val candidates = emptyList<Candidate>()
    override fun choose(index: Int) {}
    override fun finish() {}
}
