package com.netmaru.rea

import android.annotation.SuppressLint
import android.content.Context
import android.content.SharedPreferences
import android.content.res.Configuration
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.drawable.GradientDrawable
import android.media.AudioManager
import android.os.Build
import android.util.TypedValue
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.WindowInsets
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TextView

/** The iOS keyboard's keys, drawn on one canvas in dp (KeyLayout), under a scrolling candidate
 *  bar. Keys act on touch-down, in the order pressed (as on iOS: on release, fast typing reordered
 *  keys); a character key shows its popup while held, a function key darkens. */
@SuppressLint("ViewConstructor")
class KeyboardView(context: Context, private val typist: Typist) : FrameLayout(context) {
    private val density = resources.displayMetrics.density
    private val dark = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES
    // iOS 26's system keyboard: every key the same color
    private val keyColor = if (dark) 0xFF6B6B6B.toInt() else 0xFFFFFFFF.toInt()
    private val pressedColor = if (dark) 0xFF454545.toInt() else 0xFFABB0BA.toInt()
    private val labelColor = if (dark) 0xFFFFFFFF.toInt() else 0xFF000000.toInt()
    private val secondaryColor = if (dark) 0x99EBEBF5.toInt() else 0x993C3C43.toInt()
    private val backgroundColor = if (dark) 0xFF2B2B2B.toInt() else 0xFFD1D3D9.toInt()

    private var keys = emptyList<KeyFrame>()
    private val pressed = mutableMapOf<Int, KeyFrame>() // by pointer id
    private var shifted = false
    /** 123 mode: the letter keys type digits and symbols. */
    private var numbers = false
    private var switchLabel = ""
    private var sound = true
    private var haptics = true
    private val audio = context.getSystemService(AudioManager::class.java)

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }
    private val rect = RectF()

    private val candidateRow = LinearLayout(context)
    private val bar = HorizontalScrollView(context).apply {
        isHorizontalScrollBarEnabled = false
        addView(candidateRow)
    }
    private val popup = TextView(context).apply {
        setTextSize(TypedValue.COMPLEX_UNIT_DIP, 32f)
        setTextColor(labelColor)
        gravity = Gravity.CENTER
        background = GradientDrawable().apply { setColor(keyColor); cornerRadius = 8 * density }
        elevation = 2 * density
        visibility = GONE
    }

    init {
        setWillNotDraw(false)
        setBackgroundColor(backgroundColor)
        val margin = (KeyLayout.MARGIN * density).toInt()
        addView(bar, LayoutParams(LayoutParams.MATCH_PARENT, (KeyLayout.BAR_HEIGHT * density).toInt()).apply {
            leftMargin = margin; rightMargin = margin
        })
        addView(popup)
    }

    /** The keyboard comes up: settings may have changed in the app. */
    fun start(prefs: SharedPreferences) {
        switchLabel = Prefs.switchLabel(prefs)
        sound = prefs.getBoolean("keySound", true)
        haptics = prefs.getBoolean("keyHaptics", true)
        shifted = false
        numbers = false
        refreshCandidates()
        invalidate()
    }

    fun stop() {
        pressed.clear()
        removeCallbacks(repeatDelete)
        popup.visibility = GONE
        refreshCandidates()
    }

    /** The navigation bar under the keys: the gesture handle and the IME's back/switch buttons
     *  (a caption bar, so systemBars, not navigationBars: 126px vs 63 on a Pixel). */
    private fun navigationBar(): Int {
        val insets = rootWindowInsets ?: return 0
        @Suppress("DEPRECATION")
        return if (Build.VERSION.SDK_INT >= 30) insets.getInsets(WindowInsets.Type.systemBars()).bottom else insets.systemWindowInsetBottom
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val bar = navigationBar()
        if (bar != paddingBottom) setPadding(0, 0, 0, bar)
        super.onMeasure(widthMeasureSpec, MeasureSpec.makeMeasureSpec((KeyLayout.HEIGHT * density).toInt() + bar, MeasureSpec.EXACTLY))
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        keys = KeyLayout.keys(w / density)
    }

    private fun isCharacter(id: String) = id.length == 1 && id != "⇧" && id != "⌫"

    /** What a key shows: the letter (its case), the symbol in 123 mode, or the function's name. */
    private fun title(id: String): String = when {
        id == "switch" -> switchLabel
        id == "123" -> if (numbers) "ABC" else "123"
        id.length > 1 || !id[0].isLetter() -> id
        numbers -> KeyLayout.symbols.getValue(id[0]).toString()
        shifted -> id.uppercase()
        else -> id
    }

    override fun onDraw(canvas: Canvas) {
        canvas.save()
        canvas.scale(density, density)
        for (key in keys) {
            rect.set(key.x, key.y, key.x + key.w, key.y + key.h)
            paint.color = if (key in pressed.values && !isCharacter(key.id)) pressedColor else keyColor
            canvas.drawRoundRect(rect, 6f, 6f, paint)
            val letter = key.id.length == 1 && key.id[0].isLetter() && !numbers
            paint.color = labelColor
            paint.textSize = if (letter) 20f else if (isCharacter(key.id)) 22f else 16f
            val title = paint.fontMetrics
            if (!letter) {
                canvas.drawText(title(key.id), key.midX, rect.centerY() - (title.ascent + title.descent) / 2, paint)
                continue
            }
            // English letter with its 2-set jamo below: "q / ㅂ", shifted "Q / ㅃ"
            val jamo = KeyLayout.jamo.getValue(key.id[0]).let { if (shifted) it.second ?: it.first else it.first }
            val titleHeight = title.descent - title.ascent
            val top = rect.centerY() - (titleHeight + 11f * 1.17f) / 2
            canvas.drawText(title(key.id), key.midX, top - title.ascent, paint)
            paint.textSize = 11f
            paint.color = secondaryColor
            canvas.drawText(jamo, key.midX, top + titleHeight - paint.fontMetrics.ascent, paint)
        }
        canvas.restore()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                if (keys.isEmpty()) return true
                val key = KeyLayout.nearest(keys, e.getX(i) / density, e.getY(i) / density)
                pressed[e.getPointerId(i)] = key
                feedback(key.id)
                if (isCharacter(key.id)) showPopup(key)
                tap(key.id)
                if (key.id == "⌫") { removeCallbacks(repeatDelete); postDelayed(repeatDelete, 400) }
                invalidate()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> release(e.getPointerId(i))
            MotionEvent.ACTION_CANCEL -> pressed.keys.toList().forEach(::release)
        }
        return true
    }

    private fun release(pointer: Int) {
        val key = pressed.remove(pointer) ?: return
        if (key.id == "⌫") removeCallbacks(repeatDelete)
        if (popup.tag == key) popup.visibility = GONE
        invalidate()
    }

    /** Delete repeats while held: after 0.4s, every 0.1s (azooKey's timing). */
    private val repeatDelete: Runnable = Runnable {
        feedback("⌫")
        tap("⌫")
        postDelayed(repeatDelete, 100)
    }

    /** The pressed character, enlarged above the key, as on the system keyboard. */
    private fun showPopup(key: KeyFrame) {
        val w = key.w + 16
        popup.tag = key
        popup.text = title(key.id)
        popup.layoutParams = LayoutParams((w * density).toInt(), (48 * density).toInt()).apply {
            leftMargin = ((key.midX - w / 2).coerceIn(2f, this@KeyboardView.width / density - w - 2) * density).toInt()
            topMargin = (maxOf(key.y - 52, 0f) * density).toInt()
        }
        popup.visibility = VISIBLE
    }

    /** The system's click sounds (silent unless the ringer is on) and a light tap, on touch-down. */
    private fun feedback(id: String) {
        if (sound && audio.ringerMode == AudioManager.RINGER_MODE_NORMAL) audio.playSoundEffect(when (id) {
            "⌫" -> AudioManager.FX_KEYPRESS_DELETE
            "space" -> AudioManager.FX_KEYPRESS_SPACEBAR
            "return" -> AudioManager.FX_KEYPRESS_RETURN
            else -> AudioManager.FX_KEYPRESS_STANDARD
        }, -1f)
        if (haptics) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
    }

    private fun tap(id: String) {
        when (id) {
            "⇧" -> shifted = !shifted
            "123" -> { numbers = !numbers; shifted = false }
            "⌫" -> typist.tap(Key.Delete)
            "switch" -> typist.tap(Key.Cycle)
            "space" -> typist.tap(Key.Space)
            "return" -> typist.tap(Key.Return)
            else -> {
                val symbol = if (numbers) KeyLayout.symbols[id[0]] else null
                typist.tap(Key.Char(symbol ?: if (shifted) id[0].uppercaseChar() else id[0]))
                if (symbol == null) shifted = false // one-shot shift
            }
        }
        refreshCandidates()
    }

    /** The typist's words; a tap picks one. */
    private fun refreshCandidates() {
        candidateRow.removeAllViews()
        typist.candidates.forEachIndexed { index, candidate ->
            candidateRow.addView(TextView(context).apply {
                text = candidate.text
                setTextSize(TypedValue.COMPLEX_UNIT_DIP, 20f)
                gravity = Gravity.CENTER
                val pad = (12 * density).toInt()
                setPadding(pad, 0, pad, 0)
                setTextColor(if (candidate.selected) backgroundColor else labelColor)
                if (candidate.selected) background = GradientDrawable().apply { setColor(labelColor); cornerRadius = 8 * density }
                setOnClickListener { typist.choose(index); refreshCandidates() }
            }, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.MATCH_PARENT).apply {
                marginEnd = (4 * density).toInt()
            })
        }
        bar.scrollTo(0, 0)
    }
}
