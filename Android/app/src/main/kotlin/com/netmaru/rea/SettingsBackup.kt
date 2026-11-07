package com.netmaru.rea

import android.content.SharedPreferences
import org.json.JSONObject

/** The app's settings as JSON, for a file the user keeps: only the keys the app writes (languages,
 *  options, keyboard shape, shortcuts, touch learning), each with its type. */
object SettingsBackup {
    fun export(p: SharedPreferences): String = JSONObject().apply {
        put("koenja", 1)
        for ((key, value) in p.all) when (value) {
            is Boolean, is Int, is String -> put(key, value)
            is Float -> put(key, JSONObject().put("float", value.toDouble()))
        }
    }.toString(2)

    /** The largest file restored, in bytes. */
    const val MAX_BYTES = 64 * 1024

    private val booleans = (Prefs.options + Prefs.feedback).map { it.first }.toSet() +
        setOf("nearbyKeys", "adaptToTouch", "swipeSymbols")
    private val strings = mapOf("languages" to 64, "shortcuts" to 20_000, TouchCalibration.KEY to 4_000)

    /** Replaces the settings with the file's. Every key must be one the app writes, with its type
     *  and in its range, or nothing changes (throws). */
    fun restore(p: SharedPreferences, json: String) {
        val o = JSONObject(json)
        require(o.optInt("koenja") == 1) { "not a KoEnJa settings file" }
        val writes = mutableListOf<(SharedPreferences.Editor) -> Unit>()
        for (key in o.keys()) {
            if (key == "koenja") continue
            val v = o.get(key)
            val shape = KeyShape.SETTINGS.firstOrNull { it.first == key }
            when {
                key in booleans && v is Boolean -> writes += { it.putBoolean(key, v) }
                key in strings && v is String && v.length <= strings.getValue(key) -> writes += { it.putString(key, v) }
                key == "kbHue" && v is Int && v in -1..359 -> writes += { it.putInt(key, v) }
                shape != null && v is JSONObject && v.has("float") -> {
                    val f = v.getDouble("float")
                    val (min, max, _) = shape.second
                    require(f.isFinite() && f >= min - 1e-3 && f <= max + 1e-3) { "$key out of range" }
                    writes += { it.putFloat(key, f.toFloat()) }
                }
                else -> throw IllegalArgumentException("unexpected $key")
            }
        }
        val edit = p.edit().clear()
        writes.forEach { it(edit) }
        edit.apply()
    }
}
