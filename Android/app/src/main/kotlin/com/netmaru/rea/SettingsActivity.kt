package com.netmaru.rea

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView

/** The iOS app's settings: turning the keyboard on, language order, input options, key feedback,
 *  and a field to try it in. */
class SettingsActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = Prefs.of(this)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad, pad, pad) }
        fun header(text: String) = list.addView(TextView(this).apply {
            this.text = text
            setTextAppearance(android.R.style.TextAppearance_Material_Subhead)
            setPadding(0, pad, 0, pad / 2)
        })
        fun note(text: String) = list.addView(TextView(this).apply { this.text = text; alpha = 0.7f })
        fun toggles(items: List<Triple<String, String, Boolean>>) = items.forEach { (key, label, default) ->
            list.addView(Switch(this).apply {
                text = label
                isChecked = prefs.getBoolean(key, default)
                setOnCheckedChangeListener { _, on -> prefs.edit().putBoolean(key, on).apply() }
            })
        }

        header("써 보기") // first: below, the keyboard would cover it
        list.addView(EditText(this).apply { hint = "dkssudgktpdy / hello / kanjihenkan" })

        header("키보드 켜기")
        note("설정에서 한영일을 켠 다음, 입력 방법으로 고르세요.")
        list.addView(Button(this).apply {
            text = "키보드 설정 열기"
            setOnClickListener { startActivity(Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)) }
        })
        list.addView(Button(this).apply {
            text = "입력 방법 고르기"
            setOnClickListener { getSystemService(InputMethodManager::class.java).showInputMethodPicker() }
        })

        header("주 사용 언어")
        val current = TextView(this)
        fun showOrder() { current.text = Prefs.order(prefs).withIndex().joinToString("  ") { "${it.index + 1} ${it.value.label}" } }
        showOrder()
        list.addView(current)
        list.addView(RadioGroup(this).apply {
            Prefs.presets.forEach { (title, order) ->
                addView(RadioButton(context).apply {
                    text = title
                    id = View.generateViewId()
                    isChecked = order == Prefs.order(prefs)
                    setOnClickListener { Prefs.setOrder(prefs, order); showOrder() }
                })
            }
        })
        note("위에 있는 언어일수록 애매한 단어(go → 해)가 그 언어로 입력됩니다. 영어는 항상 켜져 있습니다.")

        header("입력 옵션")
        toggles(Prefs.options)
        note("Mac용 Seamtype과 같은 옵션입니다. 라이브 변환을 켜면 치는 동안 한자로 바뀌고, 끄면 위쪽 막대에서 한자를 고릅니다.")

        header("키 피드백")
        toggles(Prefs.feedback)

        header("오픈소스 라이선스")
        note("KoEnJa의 언어 엔진은 Araso(MIT 라이선스, github.com/nyttastudio/araso)를 바탕으로 합니다.")
        list.addView(Button(this).apply {
            text = "라이선스 전문 보기"
            setOnClickListener {
                val text = listOf("THIRD_PARTY_NOTICES.txt", "mozc.dic.LICENSE.txt")
                    .joinToString("\n\n") { assets.open(it).bufferedReader().use { r -> r.readText() } }
                AlertDialog.Builder(context).setTitle("오픈소스 라이선스")
                    .setView(ScrollView(context).apply { addView(TextView(context).apply { this.text = text; setPadding(pad, pad, pad, pad); setTextIsSelectable(true) }) })
                    .setPositiveButton(android.R.string.ok, null).show()
            }
        })

        setContentView(ScrollView(this).apply { fitsSystemWindows = true; addView(list) })
    }
}
