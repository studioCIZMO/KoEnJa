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
        keyboard?.start(Prefs.of(this), sensitive = info != null && isSensitive(info))
    }

    override fun onUpdateSelection(oldSelStart: Int, oldSelEnd: Int, newSelStart: Int, newSelEnd: Int, candidatesStart: Int, candidatesEnd: Int) {
        super.onUpdateSelection(oldSelStart, oldSelEnd, newSelStart, newSelEnd, candidatesStart, candidatesEnd)
        if (typist.selectionChanged(selecting = newSelStart != newSelEnd)) keyboard?.refreshCandidates()
    }

    override fun onFinishInputView(finishingInput: Boolean) {
        typist.finish()
        keyboard?.stop()
        super.onFinishInputView(finishingInput)
    }
}

/** A password field, or one asking keyboards not to learn from it (incognito). */
private fun isSensitive(info: EditorInfo): Boolean {
    val type = info.inputType
    val variation = type and android.text.InputType.TYPE_MASK_VARIATION
    val password = when (type and android.text.InputType.TYPE_MASK_CLASS) {
        android.text.InputType.TYPE_CLASS_TEXT -> variation in listOf(android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD,
            android.text.InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD, android.text.InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD)
        android.text.InputType.TYPE_CLASS_NUMBER -> variation == android.text.InputType.TYPE_NUMBER_VARIATION_PASSWORD
        else -> false
    }
    return password || info.imeOptions and EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING != 0
}

/** Debug builds log per-key and candidate timings (tag REA) to compare on devices. */
var TRACE = false
