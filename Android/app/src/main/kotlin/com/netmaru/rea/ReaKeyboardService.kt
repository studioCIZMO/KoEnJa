package com.netmaru.rea

import android.content.pm.ApplicationInfo
import android.inputmethodservice.InputMethodService
import android.view.inputmethod.EditorInfo
import com.netmaru.rea.engine.Resources

class ReaKeyboardService : InputMethodService() {
    private val typist = Typist(this)
    private var keyboard: KeyboardView? = null

    override fun onCreate() {
        super.onCreate()
        TRACE = applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        // the engine's word lists and trigrams, packaged as assets
        Resources.open = { name -> runCatching { assets.open(name) }.getOrNull() }
    }

    override fun onCreateInputView() = KeyboardView(this, typist).also { keyboard = it }

    override fun onStartInputView(info: EditorInfo?, restarting: Boolean) {
        // The app restarted the field (its text may be new) without onFinishInputView: the word stays as typed.
        if (restarting) typist.finish()
        typist.start(Prefs.of(this))
        keyboard?.start(Prefs.of(this))
    }

    override fun onUpdateSelection(oldSelStart: Int, oldSelEnd: Int, newSelStart: Int, newSelEnd: Int, candidatesStart: Int, candidatesEnd: Int) {
        super.onUpdateSelection(oldSelStart, oldSelEnd, newSelStart, newSelEnd, candidatesStart, candidatesEnd)
        if (typist.selectionChanged()) keyboard?.refreshCandidates()
    }

    override fun onFinishInputView(finishingInput: Boolean) {
        typist.finish()
        keyboard?.stop()
        super.onFinishInputView(finishingInput)
    }
}

/** Debug builds log per-key and candidate timings (tag REA) to compare on devices. */
var TRACE = false
