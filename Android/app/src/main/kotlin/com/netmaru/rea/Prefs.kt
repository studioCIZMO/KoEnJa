package com.netmaru.rea

import android.content.Context
import android.content.SharedPreferences

enum class Language(val label: String, val mark: String) {
    KOREAN("한국어", "한"), JAPANESE("日本語", "あ"), ENGLISH("English", "A")
}

/** The settings the app writes and the keyboard reads (the iOS app's keys and defaults). */
object Prefs {
    fun of(context: Context): SharedPreferences = context.getSharedPreferences("rea", Context.MODE_PRIVATE)

    /** The usual setups: who you talk to decides the pair. */
    val presets = listOf(
        "한국어 + 영어" to listOf(Language.KOREAN, Language.ENGLISH),
        "日本語 + English" to listOf(Language.JAPANESE, Language.ENGLISH),
        "한국어 > 일본어 > 영어" to listOf(Language.KOREAN, Language.JAPANESE, Language.ENGLISH),
        "日本語 > 한국어 > English" to listOf(Language.JAPANESE, Language.KOREAN, Language.ENGLISH),
    )

    /** Enabled languages, most used first; English is always on. */
    fun order(p: SharedPreferences): List<Language> {
        val saved = p.getString("languages", null)?.split(",")?.mapNotNull { name -> Language.entries.find { it.name == name } }
        val order = if (saved.isNullOrEmpty()) listOf(Language.KOREAN, Language.ENGLISH, Language.JAPANESE) else saved
        return if (Language.ENGLISH in order) order else order + Language.ENGLISH
    }

    fun setOrder(p: SharedPreferences, order: List<Language>) =
        p.edit().putString("languages", order.joinToString(",") { it.name }).apply()

    /** The switch key's label: the enabled languages in order (한あA, あA…). */
    fun switchLabel(p: SharedPreferences) = order(p).joinToString("") { it.mark }

    /** Seamtype's input options, then key feedback: key, label, default. */
    val options = listOf(
        Triple("returnConfirmsOnly", "Return은 입력 중인 단어만 확정", true),
        Triple("fullWidthDigits", "일본어 뒤 숫자를 전각으로 (１２３)", false),
        Triple("fullWidthSpace", "일본어 뒤 스페이스를 전각으로", false),
        Triple("liveConversion", "일본어 라이브 변환", false),
    )
    val feedback = listOf(Triple("keySound", "키 소리", true), Triple("keyHaptics", "햅틱", true))
}
