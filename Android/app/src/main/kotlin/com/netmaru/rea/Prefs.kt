package com.netmaru.rea

import android.content.Context
import android.content.SharedPreferences
import com.netmaru.rea.engine.Language

val Language.label get() = when (this) {
    Language.KOREAN -> tr("한국어", "韓国語", "Korean")
    Language.JAPANESE -> tr("일본어", "日本語", "Japanese")
    else -> tr("영어", "英語", "English")
}

/** The app's text in the phone's language: Korean, Japanese, else English. */
fun tr(ko: String, ja: String, en: String) = when (java.util.Locale.getDefault().language) { "ko" -> ko; "ja" -> ja; else -> en }
val Language.mark get() = mapOf(Language.KOREAN to "한", Language.JAPANESE to "あ").getOrElse(this) { "A" }

/** The settings the app writes and the keyboard reads (the iOS app's keys and defaults). */
object Prefs {
    fun of(context: Context): SharedPreferences = context.getSharedPreferences("rea", Context.MODE_PRIVATE)

    /** The usual setups: who you talk to decides the pair. */
    val presets get() = listOf(
        listOf(Language.KOREAN, Language.ENGLISH),
        listOf(Language.JAPANESE, Language.ENGLISH),
        listOf(Language.KOREAN, Language.JAPANESE, Language.ENGLISH),
        listOf(Language.JAPANESE, Language.KOREAN, Language.ENGLISH),
    ).map { order -> order.joinToString(if (order.size == 2) " + " else " > ") { it.label } to order }

    /** Enabled languages, most used first; English is always on. */
    fun order(p: SharedPreferences): List<Language> {
        val saved = p.getString("languages", null)?.split(",")?.mapNotNull { name -> Language.entries.find { it.name == name } }
        val order = if (saved.isNullOrEmpty()) listOf(Language.KOREAN, Language.ENGLISH, Language.JAPANESE) else saved
        return if (Language.ENGLISH in order) order else order + Language.ENGLISH
    }

    fun setOrder(p: SharedPreferences, order: List<Language>) =
        p.edit().putString("languages", order.joinToString(",") { it.name }).apply()

    /** Text shortcuts ("brb" → "Be right back"), in the order added: lines of keys TAB phrase. */
    fun shortcuts(p: SharedPreferences): List<Pair<String, String>> =
        p.getString("shortcuts", null)?.lines()?.mapNotNull { line ->
            line.split('\t', limit = 2).takeIf { it.size == 2 && it[0].isNotBlank() }?.let { it[0] to it[1] }
        }.orEmpty()

    fun setShortcuts(p: SharedPreferences, list: List<Pair<String, String>>) =
        p.edit().putString("shortcuts", list.joinToString("\n") { (k, v) -> "$k\t${v.replace('\n', ' ')}" }).apply()

    /** The switch key's label: the enabled languages in order (한あA, あA…). */
    fun switchLabel(p: SharedPreferences) = order(p).joinToString("") { it.mark }

    /** Seamtype's input options (Typist reads them in this order), then key feedback: key, label, default. */
    val options get() = listOf(
        Triple("returnConfirmsOnly", tr("Return은 입력 중인 단어만 확정", "Return は入力中の単語だけを確定", "Return only confirms the word being typed"), true),
        Triple("fullWidthDigits", tr("일본어 뒤 숫자를 전각으로 (１２３)", "日本語のあとの数字を全角に（１２３）", "Full-width digits after Japanese (１２３)"), false),
        Triple("fullWidthSpace", tr("일본어 뒤 스페이스를 전각으로", "日本語のあとのスペースを全角に", "Full-width space after Japanese"), false),
        Triple("liveConversion", tr("일본어 라이브 변환", "日本語ライブ変換", "Japanese live conversion"), false),
    )
    val feedback get() = listOf(Triple("keySound", tr("키 소리", "キー音", "Key sound"), true), Triple("keyHaptics", tr("햅틱", "触覚フィードバック", "Haptics"), true))
}
