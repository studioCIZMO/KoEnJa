package com.netmaru.rea

import android.inputmethodservice.InputMethodService
import android.view.inputmethod.EditorInfo

class ReaKeyboardService : InputMethodService() {
    // phase 2: the engine's InputSession behind the same seam
    private val typist: Typist = PassthroughTypist(this)
    private var keyboard: KeyboardView? = null

    override fun onCreateInputView() = KeyboardView(this, typist).also { keyboard = it }

    override fun onStartInputView(info: EditorInfo?, restarting: Boolean) {
        keyboard?.start(Prefs.of(this))
    }

    override fun onFinishInputView(finishingInput: Boolean) {
        typist.finish()
        keyboard?.stop()
        super.onFinishInputView(finishingInput)
    }
}
