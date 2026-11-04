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
import android.widget.ImageView
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

        header(tr("써 보기", "試し打ち", "Try it")) // first: below, the keyboard would cover it
        list.addView(EditText(this).apply { hint = "dkssudgktpdy / hello / kanjihenkan" })

        header(tr("키보드 켜기", "キーボードをオンにする", "Turn on the keyboard"))
        // The steps with the system's own screens, shown until KoEnJa is turned on.
        fun step(text: String, image: Int) = guide.apply {
            addView(TextView(this@SettingsActivity).apply { this.text = text; setPadding(0, pad / 2, 0, pad / 4) })
            addView(ImageView(this@SettingsActivity).apply {
                setImageResource(image); adjustViewBounds = true
                layoutParams = LinearLayout.LayoutParams((280 * resources.displayMetrics.density).toInt(), LinearLayout.LayoutParams.WRAP_CONTENT)
            })
        }
        step(tr("1. 「키보드 설정 열기」를 누르고 목록에서 한영일을 켭니다.", "1. 「キーボード設定を開く」を押し、一覧で KoEnJa をオンにします。", "1. Tap “Open keyboard settings” and turn on KoEnJa in the list."), R.drawable.setup_1)
        step(tr("2. 주의 창에서 확인을 누릅니다. 입력한 내용은 기기 밖으로 보내지 않습니다.", "2. 注意の画面で OK を押します。入力した内容は端末の外に送りません。", "2. Tap OK on the notice. What you type never leaves the phone."), R.drawable.setup_2)
        step(tr("3. 「입력 방법 고르기」를 누르고 한영일을 고릅니다.", "3. 「入力方法を選ぶ」を押し、KoEnJa を選びます。", "3. Tap “Choose input method” and pick KoEnJa."), R.drawable.setup_3)
        list.addView(Button(this).apply {
            text = tr("키보드 설정 열기", "キーボード設定を開く", "Open keyboard settings")
            setOnClickListener { startActivity(Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)) }
        })
        list.addView(Button(this).apply {
            text = tr("입력 방법 고르기", "入力方法を選ぶ", "Choose input method")
            setOnClickListener { getSystemService(InputMethodManager::class.java).showInputMethodPicker() }
        })

        list.addView(guide)

        header(tr("주 사용 언어", "よく使う言語", "Your languages"))
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
        note(tr("위에 있는 언어일수록 애매한 단어(go → 해)가 그 언어로 입력됩니다. 영어는 항상 켜져 있습니다.", "上の言語ほど、あいまいな単語（go → 해）がその言語で入力されます。英語は常にオンです。", "The higher a language, the more an ambiguous word (go → 해) goes to it. English is always on."))

        header(tr("입력 옵션", "入力オプション", "Input options"))
        toggles(Prefs.options)
        note(tr("Mac용 Seamtype과 같은 옵션입니다. 라이브 변환을 켜면 치는 동안 한자로 바뀌고, 끄면 위쪽 막대에서 한자를 고릅니다.", "Mac 版 Seamtype と同じオプションです。ライブ変換をオンにすると打ちながら漢字になり、オフでは上のバーから漢字を選びます。", "The same options as Seamtype for Mac. With live conversion on, kanji appear as you type; off, you pick them from the bar above."))

        header(tr("키 피드백", "キーのフィードバック", "Key feedback"))
        toggles(Prefs.feedback)

        header(tr("오픈소스 라이선스", "オープンソースライセンス", "Open-source licenses"))
        note(tr("KoEnJa의 언어 엔진은 Araso(MIT 라이선스, github.com/nyttastudio/araso)를 바탕으로 합니다.", "KoEnJa の言語エンジンは Araso（MIT ライセンス、github.com/nyttastudio/araso）をもとにしています。", "The KoEnJa language engine is based on Araso (MIT License, github.com/nyttastudio/araso)."))
        list.addView(Button(this).apply {
            text = tr("라이선스 전문 보기", "ライセンス全文を見る", "View full licenses")
            setOnClickListener {
                val text = listOf("THIRD_PARTY_NOTICES.txt", "mozc.dic.LICENSE.txt")
                    .joinToString("\n\n") { assets.open(it).bufferedReader().use { r -> r.readText() } }
                AlertDialog.Builder(context).setTitle(tr("오픈소스 라이선스", "オープンソースライセンス", "Open-source licenses"))
                    .setView(ScrollView(context).apply { addView(TextView(context).apply { this.text = text; setPadding(pad, pad, pad, pad); setTextIsSelectable(true) }) })
                    .setPositiveButton(android.R.string.ok, null).show()
            }
        })

        setContentView(ScrollView(this).apply { fitsSystemWindows = true; addView(list) })
    }

    private val guide by lazy { LinearLayout(this).apply { orientation = LinearLayout.VERTICAL } }

    override fun onResume() {
        super.onResume()
        // Once it's on, picking it is the button's one step (apps can't read which keyboard is in use).
        val on = getSystemService(InputMethodManager::class.java).enabledInputMethodList.any { it.packageName == packageName }
        guide.visibility = if (on) View.GONE else View.VISIBLE
    }
}
