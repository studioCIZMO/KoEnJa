package com.netmaru.rea

import android.content.SharedPreferences

/** Where this person touches each key, learned from the nearby-key fixes they pick: a fix that
 *  swapped key t for its neighbor n means they meant n and touched where they did, so that point
 *  (relative to n's center) is a sample for n. After [MIN_SAMPLES] samples, n's touch area moves
 *  by their average, at most [MAX_SHIFT] of the key's size each way (KeyLayout.nearest).
 *  Kept in the app's preferences ("touchCalibration": "key:count:sumX:sumY;…", in dp). */
class TouchCalibration(private val prefs: SharedPreferences) {
    private val samples = HashMap<Char, FloatArray>()

    init {
        prefs.getString(KEY, null)?.split(';')?.forEach { entry ->
            val p = entry.split(':')
            if (p.size == 4 && p[0].length == 1) {
                val v = p.drop(1).mapNotNull { it.toFloatOrNull() }
                if (v.size == 3) samples[p[0][0]] = v.toFloatArray()
            }
        }
    }

    fun add(key: Char, dx: Float, dy: Float) {
        val s = samples.getOrPut(key.lowercaseChar()) { FloatArray(3) }
        if (s[0] >= 200) { s[0] /= 2; s[1] /= 2; s[2] /= 2 } // recent touches count more, and it can't grow without end
        s[0] += 1; s[1] += dx; s[2] += dy
        prefs.edit().putString(KEY, samples.entries.joinToString(";") { (k, v) -> "$k:${v[0]}:${v[1]}:${v[2]}" }).apply()
    }

    /** Each learned key's shift (dx, dy) in dp, for keys `w` × `h` dp. */
    fun offsets(w: Float, h: Float): Map<String, Pair<Float, Float>> = samples.filter { it.value[0] >= MIN_SAMPLES }
        .map { (k, v) -> k.toString() to ((v[1] / v[0]).coerceIn(-w * MAX_SHIFT, w * MAX_SHIFT) to (v[2] / v[0]).coerceIn(-h * MAX_SHIFT, h * MAX_SHIFT)) }
        .toMap()

    companion object {
        const val KEY = "touchCalibration"
        const val MIN_SAMPLES = 8
        const val MAX_SHIFT = 0.3f
        fun clear(prefs: SharedPreferences) = prefs.edit().remove(KEY).apply()
    }
}
