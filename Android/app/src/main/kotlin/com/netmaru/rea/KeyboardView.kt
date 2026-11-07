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
import android.os.SystemClock
import android.util.Log
import android.util.TypedValue
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.WindowInsets
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TextView

/** Swipe up this far (dp) on a letter for its symbol; space moved this far sideways starts moving
 *  the cursor, then one character per step. */
private const val SWIPE_UP = 18f
private const val CURSOR_START = 14f
private const val CURSOR_STEP = 9f

/** The iOS keyboard's keys, drawn on one canvas in dp (KeyLayout), under a scrolling candidate
 *  bar. Keys act on touch-down, in the order pressed (as on iOS: on release, fast typing reordered
 *  keys); a character key shows its popup while held, a function key darkens. */
@SuppressLint("ViewConstructor")
class KeyboardView(context: Context, private val typist: Typist) : FrameLayout(context) {
    private val density = resources.displayMetrics.density
    private val dark = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES
    // iOS 26's system keyboard: every key the same color; a hue from the settings tints them
    private var keyColor = 0
    private var pressedColor = 0
    private val labelColor = if (dark) 0xFFFFFFFF.toInt() else 0xFF000000.toInt()
    private val secondaryColor = if (dark) 0x99EBEBF5.toInt() else 0x993C3C43.toInt()
    private var backgroundColor = 0
    private var shape = KeyShape()
    private var prefs: SharedPreferences? = null
    private var calibration: TouchCalibration? = null
    /** Learned keys' touch areas moved by where this person touches them (TouchCalibration). */
    private var offsets = emptyMap<String, Pair<Float, Float>>()
    private var adaptToTouch = true
    /** Swipe up on a letter key types the symbol 123 mode puts there (shown in its corner). */
    private var swipeSymbols = true
    /** Per pointer: where it went down (dp), and whether a gesture took it over. */
    private val downAt = mutableMapOf<Int, Pair<Float, Float>>()
    private val gestured = mutableSetOf<Int>()
    /** Per letter pointer: the word's keys before its letter, for a swipe up to replace it. */
    private val keysBefore = mutableMapOf<Int, String>()
    /** The pointer whose key typed last (-1: a repeat delete): a swipe up replaces only its own letter. */
    private var lastInput = -1
    /** The one pointer moving the cursor from space. */
    private var cursorPointer: Int? = null
    /** Space held and moved sideways: the cursor moves, one character per [CURSOR_STEP] dp. */
    private var cursorFrom: Float? = null

    private fun color(shape: KeyShape) {
        fun hsv(s: Float, v: Float) = android.graphics.Color.HSVToColor(floatArrayOf(shape.hue.toFloat(), s, v))
        val tinted = shape.hue >= 0
        keyColor = if (tinted) hsv(0.08f, if (dark) 0.38f else 1f) else if (dark) 0xFF6B6B6B.toInt() else 0xFFFFFFFF.toInt()
        pressedColor = if (tinted) hsv(0.2f, if (dark) 0.26f else 0.74f) else if (dark) 0xFF454545.toInt() else 0xFFABB0BA.toInt()
        backgroundColor = if (tinted) hsv(0.2f, if (dark) 0.18f else 0.86f) else if (dark) 0xFF2B2B2B.toInt() else 0xFFD1D3D9.toInt()
        setBackgroundColor(backgroundColor)
        (popup.background as? GradientDrawable)?.setColor(keyColor)
    }

    /** The shape changed in the settings while the keyboard is up (its try field): redrawn at once. */
    private val shapeChanged = SharedPreferences.OnSharedPreferenceChangeListener { p, key ->
        if (key != null && (key.startsWith("kb"))) applyShape(KeyShape.of(p))
        // cleared in the settings (or learned): what the keys use comes from the file again
        if (key == null || key == TouchCalibration.KEY) { calibration = TouchCalibration(p); refreshOffsets() }
    }

    private fun applyShape(new: KeyShape) {
        val resized = new.total != shape.total
        shape = new
        color(new)
        if (width > 0) keys = KeyLayout.keys(width / density, new)
        refreshOffsets()
        if (resized) requestLayout()
        shown = emptyList()
        refreshCandidates()
        invalidate()
    }

    private var keys = emptyList<KeyFrame>()
    private val pressed = mutableMapOf<Int, KeyFrame>() // by pointer id
    private var shifted = false
    /** 123 mode: the letter keys type digits and symbols. */
    private var numbers = false
    private var switchLabel = ""
    private var sound = true
    private var haptics = true
    /** The ringer allows key clicks; read when the keyboard comes up, not per key (a binder call). */
    private var ringer = true
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
        visibility = INVISIBLE // not GONE: showing it then doesn't lay the keyboard out again
    }
    /** The candidates on the bar, to rebuild it only when they change. */
    private var shown = emptyList<Candidate>()

    init {
        setWillNotDraw(false)
        color(shape)
        val margin = (KeyLayout.MARGIN * density).toInt()
        addView(bar, LayoutParams(LayoutParams.MATCH_PARENT, (KeyLayout.BAR_HEIGHT * density).toInt()).apply {
            leftMargin = margin; rightMargin = margin
        })
        addView(popup, LayoutParams(0, (48 * density).toInt()))
        typist.onCandidates = ::refreshCandidates
        typist.keyCenter = { c -> keys.firstOrNull { it.id == c.toString() }?.let { it.midX to it.y + it.h / 2 } }
        typist.onLearn = { n, dx, dy ->
            calibration?.add(n, dx, dy)
            refreshOffsets()
        }
    }

    private fun refreshOffsets() {
        val letter = keys.firstOrNull { it.id == "q" }
        offsets = if (adaptToTouch && letter != null) calibration?.offsets(letter.w, letter.h).orEmpty() else emptyMap()
    }

    /** The keyboard comes up: settings may have changed in the app. */
    fun start(prefs: SharedPreferences, sensitive: Boolean = false) {
        // Nearby-key suggestions and touch learning (Kej's): neither in a password field or one that asks for no learning.
        typist.nearbyKeys = prefs.getBoolean("nearbyKeys", true) && !sensitive
        adaptToTouch = prefs.getBoolean("adaptToTouch", true) && !sensitive
        swipeSymbols = prefs.getBoolean("swipeSymbols", true)
        typist.learning = adaptToTouch && typist.nearbyKeys
        calibration = TouchCalibration(prefs)
        switchLabel = Prefs.switchLabel(prefs)
        sound = prefs.getBoolean("keySound", true)
        haptics = prefs.getBoolean("keyHaptics", true)
        ringer = audio.ringerMode == AudioManager.RINGER_MODE_NORMAL
        shifted = false
        numbers = false
        if (this.prefs !== prefs) { this.prefs?.unregisterOnSharedPreferenceChangeListener(shapeChanged); this.prefs = prefs }
        prefs.registerOnSharedPreferenceChangeListener(shapeChanged)
        applyShape(KeyShape.of(prefs))
    }

    fun stop() {
        prefs?.unregisterOnSharedPreferenceChangeListener(shapeChanged)
        pressed.clear()
        removeCallbacks(repeatDelete)
        popup.visibility = INVISIBLE
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
        super.onMeasure(widthMeasureSpec, MeasureSpec.makeMeasureSpec((shape.total * density).toInt() + bar, MeasureSpec.EXACTLY))
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        keys = KeyLayout.keys(w / density, shape)
        refreshOffsets()
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
            canvas.drawRoundRect(rect, shape.radius, shape.radius, paint)
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
            if (swipeSymbols) {
                paint.textSize = 9f
                canvas.drawText(KeyLayout.symbols.getValue(key.id[0]).toString(), key.x + key.w - 5f, key.y + 10f, paint)
            }
        }
        canvas.restore()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                if (keys.isEmpty()) return true
                val start = SystemClock.uptimeMillis()
                val startNanos = SystemClock.elapsedRealtimeNanos()
                val x = e.getX(i) / density
                val y = e.getY(i) / density
                val key = KeyLayout.nearest(keys, x, y, shape, offsets)
                pressed[e.getPointerId(i)] = key
                downAt[e.getPointerId(i)] = x to y
                feedback(key.id)
                if (isCharacter(key.id)) showPopup(key)
                // Space acts on release: held and moved sideways, it moves the cursor instead.
                // Another key while a space is held: that space goes first, as pressed.
                if (key.id != "space") {
                    for ((p, k) in pressed) if (k.id == "space" && p != e.getPointerId(i) && gestured.add(p)) { tap("space"); lastInput = p }
                    if (key.id.length == 1 && key.id[0].isLetter()) keysBefore[e.getPointerId(i)] = typist.keysNow
                    tap(key.id, x to y)
                    lastInput = e.getPointerId(i)
                }
                if (key.id == "⌫") { removeCallbacks(repeatDelete); postDelayed(repeatDelete, 400) }
                invalidate()
                // handle: touch-down to keys done; lag: the touch's age when it reached us
                if (TRACE) Log.d("REA", "key ${key.id} handle ${(SystemClock.elapsedRealtimeNanos() - startNanos) / 1000}us lag ${start - e.eventTime}ms")
            }
            MotionEvent.ACTION_MOVE -> for (p in 0 until e.pointerCount) moved(e.getPointerId(p), e.getX(p) / density, e.getY(p) / density)
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(i)
                if (pressed[id]?.id == "space" && id !in gestured) { tap("space"); lastInput = id }
                release(id)
            }
            MotionEvent.ACTION_CANCEL -> pressed.keys.toList().forEach(::release).also { cursorFrom = null }
        }
        return true
    }

    /** A finger moving on a key: up from a letter types its symbol once; sideways on space moves
     *  the cursor. Anything else stays the key pressed. */
    private fun moved(pointer: Int, x: Float, y: Float) {
        val key = pressed[pointer] ?: return
        val (x0, y0) = downAt[pointer] ?: return
        if (key.id == "space") {
            if (pointer !in gestured && kotlin.math.abs(x - x0) < CURSOR_START) return
            if (cursorPointer != null && cursorPointer != pointer) return
            if (gestured.add(pointer)) { cursorFrom = x0; cursorPointer = pointer }
            val from = cursorFrom ?: x0
            val steps = ((x - from) / CURSOR_STEP).toInt()
            if (steps != 0) { typist.moveCursor(steps); cursorFrom = from + steps * CURSOR_STEP }
            return
        }
        if (swipeSymbols && !numbers && pointer !in gestured && key.id.length == 1 && key.id[0].isLetter() && y0 - y > SWIPE_UP) {
            gestured += pointer
            // only while its letter is the last thing typed (no other finger, no repeat delete since)
            if (lastInput != pointer) return
            val symbol = KeyLayout.symbols[key.id[0]] ?: return
            typist.replaceLastKey(keysBefore[pointer] ?: return, symbol)
            popup.text = symbol.toString()
        }
    }

    private fun release(pointer: Int) {
        downAt.remove(pointer)
        gestured.remove(pointer)
        keysBefore.remove(pointer)
        if (cursorPointer == pointer) cursorPointer = null
        val key = pressed.remove(pointer) ?: return
        if (key.id == "⌫") removeCallbacks(repeatDelete)
        if (popup.tag == key) popup.visibility = INVISIBLE
        invalidate()
    }

    /** Delete repeats while held: after 0.4s, every 0.1s (azooKey's timing). */
    private val repeatDelete: Runnable = Runnable {
        lastInput = -1
        feedback("⌫")
        tap("⌫")
        postDelayed(repeatDelete, 100)
    }

    /** The pressed character, enlarged above the key, as on the system keyboard. Moved, not laid
     *  out (a fixed size, resized only for a wider key): a key press redraws, it doesn't re-layout. */
    private fun showPopup(key: KeyFrame) {
        val w = key.w + 16
        popup.tag = key
        popup.text = title(key.id)
        if (popup.layoutParams.width != (w * density).toInt()) popup.layoutParams = LayoutParams((w * density).toInt(), (48 * density).toInt())
        popup.translationX = (key.midX - w / 2).coerceIn(2f, width / density - w - 2) * density
        popup.translationY = maxOf(key.y - 52, 0f) * density
        popup.visibility = VISIBLE
    }

    /** The system's click sounds (silent unless the ringer is on) and a light tap, on touch-down. */
    private fun feedback(id: String) {
        if (sound && ringer) audio.playSoundEffect(when (id) {
            "⌫" -> AudioManager.FX_KEYPRESS_DELETE
            "space" -> AudioManager.FX_KEYPRESS_SPACEBAR
            "return" -> AudioManager.FX_KEYPRESS_RETURN
            else -> AudioManager.FX_KEYPRESS_STANDARD
        }, -1f)
        if (haptics) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
    }

    private fun tap(id: String, touch: Pair<Float, Float>? = null) {
        when (id) {
            "⇧" -> shifted = !shifted
            "123" -> { numbers = !numbers; shifted = false }
            "⌫" -> typist.tap(Key.Delete)
            "switch" -> typist.tap(Key.Cycle)
            "space" -> typist.tap(Key.Space)
            "return" -> typist.tap(Key.Return)
            else -> {
                val symbol = if (numbers) KeyLayout.symbols[id[0]] else null
                // The key touched, as Gboard types it: no swapping a letter for its neighbor (typos
                // are fixed by the word, at its end).
                typist.tap(Key.Char(symbol ?: if (shifted) id[0].uppercaseChar() else id[0]), touch.takeIf { symbol == null })
                if (symbol == null) shifted = false // one-shot shift
            }
        }
    }

    /** The typist's words; a tap picks one. Unchanged words (most keys) leave the bar alone. */
    fun refreshCandidates() {
        if (typist.candidates == shown) return
        shown = typist.candidates
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
