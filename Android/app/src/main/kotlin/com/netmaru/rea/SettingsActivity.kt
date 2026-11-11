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
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView

/** The iOS app's settings: turning the keyboard on, language order, input options, key feedback,
 *  and a field to try it in. */
private const val EXPORT = 1
private const val IMPORT = 2

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

        // The keyboard's shape, under the try field: open the keyboard there and it changes as you drag.
        header(tr("키보드 모양", "キーボードの形", "Keyboard shape"))
        note(tr("위 칸에서 키보드를 연 채로 움직이면 바로 바뀝니다.", "上の欄でキーボードを開いたまま動かすと、すぐに変わります。", "Open the keyboard in the field above and it changes as you drag."))
        val names = mapOf(
            "kbHeight" to tr("키 높이", "キーの高さ", "Key height"), "kbBottom" to tr("아래 여백", "下の余白", "Space below"),
            "kbArc" to tr("가운데·양끝 높낮이", "中央・両端の高さ", "Middle and edge height"), "kbGap" to tr("키 간격", "キーの間隔", "Key spacing"),
            "kbRowGap" to tr("줄 간격", "行の間隔", "Row spacing"), "kbRadius" to tr("모서리 둥글기", "角の丸み", "Corner radius"),
            "kbSideKeys" to tr("Shift·Delete 너비", "Shift・Delete の幅", "Shift and Delete width"),
        )
        val sliders = mutableListOf<() -> Unit>()
        fun slider(label: String, steps: Int, get: () -> Int, show: (Int) -> String, set: (Int) -> Unit) {
            val title = TextView(this)
            list.addView(title)
            list.addView(SeekBar(this).apply {
                max = steps
                fun refresh() { progress = get(); title.text = "$label  ${show(progress)}" }
                refresh()
                sliders += ::refresh
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(bar: SeekBar, value: Int, fromUser: Boolean) {
                        if (!fromUser) return
                        set(value); title.text = "$label  ${show(value)}"
                    }
                    override fun onStartTrackingTouch(bar: SeekBar) {}
                    override fun onStopTrackingTouch(bar: SeekBar) {}
                })
            })
        }
        for ((key, range, default) in KeyShape.SETTINGS) {
            val (min, max, step) = range
            fun value(i: Int) = min + i * step
            slider(names.getValue(key), Math.round((max - min) / step), { Math.round((prefs.getFloat(key, default) - min) / step) },
                { i -> if (key == "kbHeight" || key == "kbSideKeys") "×%.2f".format(value(i)) else "%.0f dp".format(value(i)) },
                { i -> prefs.edit().putFloat(key, value(i)).apply() })
        }
        // 0: the system keyboard's grays; then every 10° of hue
        slider(tr("색", "色", "Color"), 36, { prefs.getInt("kbHue", -1).let { if (it < 0) 0 else it / 10 + 1 } },
            { i -> if (i == 0) tr("기본", "標準", "Default") else "${(i - 1) * 10}°" },
            { i -> prefs.edit().putInt("kbHue", if (i == 0) -1 else (i - 1) * 10).apply() })
        list.addView(Button(this).apply {
            text = tr("모양 기본값으로", "形を標準に戻す", "Reset the shape")
            setOnClickListener {
                prefs.edit().apply { KeyShape.SETTINGS.forEach { remove(it.first) }; remove("kbHue") }.apply()
                sliders.forEach { it() }
            }
        })

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

        header(tr("오타 도움", "入力の補助", "Typing assistance"))
        toggles(listOf(
            Triple("nearbyKeys", tr("주변 키 후보", "隣のキーの候補", "Nearby-key suggestions"), true),
            Triple("adaptToTouch", tr("내 터치에 맞추기", "タッチに合わせる", "Adapt to my touch"), true),
            Triple("swipeSymbols", tr("위로 쓸어 기호 입력", "上にスワイプで記号", "Swipe up for symbols"), true),
        ))
        note(tr("주변 키 후보: 맞는 단어라도 옆 키 쪽을 눌렀으면 그 키로 바꾼 단어를 위쪽 막대에 보여 줍니다(자동으로 바꾸지 않음). 내 터치에 맞추기: 그런 후보를 고르면 키마다 누른 위치를 기억하고, 한 키에 8번 쌓이면 그 키의 인식 범위를 조금 옮깁니다. 비밀번호 칸에서는 둘 다 꺼집니다. 글자 키를 위로 쓸면 키 구석의 기호가, 스페이스를 누른 채 옆으로 밀면 커서가 움직입니다.",
            "隣のキーの候補：正しい単語でも隣のキー寄りを押したときは、そのキーにした単語を上のバーに出します（自動では変えません）。タッチに合わせる：その候補を選ぶとキーごとに押した位置を覚え、1つのキーで8回たまるとそのキーの範囲を少し動かします。パスワード欄ではどちらもオフです。文字キーを上にスワイプするとキーの隅の記号が、スペースを押したまま横に動かすとカーソルが動きます。",
            "Nearby-key suggestions: even for a correct word, a touch toward a neighbor key offers the word that key makes in the bar (never put in by itself). Adapt to my touch: picking one remembers where you touched each key; after 8 for a key, its touch area moves a little. Both are off in password fields. Swipe up on a letter for the symbol in its corner; hold space and slide to move the cursor."))
        list.addView(Button(this).apply {
            text = tr("터치 기록 지우기", "タッチの記録を消す", "Clear touch learning")
            setOnClickListener { TouchCalibration.clear(prefs) }
        })

        // Text shortcuts: typed keys offer a saved phrase in the bar.
        header(tr("텍스트 단축어", "テキストショートカット", "Text shortcuts"))
        note(tr("단축어를 치면 위쪽 막대에 문장이 나오고, 누르면 입력됩니다. 예: brb → Be right back",
            "ショートカットを打つと上のバーに文が出て、押すと入力されます。例：brb → Be right back",
            "Type a shortcut and its phrase shows in the bar; tap it to put it in. Example: brb → Be right back"))
        val shortcutList = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        fun showShortcuts() {
            shortcutList.removeAllViews()
            for ((keys, phrase) in Prefs.shortcuts(prefs)) shortcutList.addView(LinearLayout(this).apply {
                addView(TextView(context).apply { text = "$keys  →  $phrase" }, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
                addView(Button(context).apply {
                    text = tr("삭제", "削除", "Delete")
                    setOnClickListener { Prefs.setShortcuts(prefs, Prefs.shortcuts(prefs).filter { it.first != keys }); showShortcuts() }
                })
            })
        }
        showShortcuts()
        list.addView(shortcutList)
        val newKeys = EditText(this).apply { hint = "brb"; isSingleLine = true }
        val newPhrase = EditText(this).apply { hint = tr("문구 (예: Be right back)", "文（例：Be right back）", "Phrase (e.g. Be right back)"); isSingleLine = true }
        list.addView(newKeys); list.addView(newPhrase)
        list.addView(Button(this).apply {
            text = tr("단축어 추가", "ショートカットを追加", "Add shortcut")
            setOnClickListener {
                val keys = newKeys.text.toString().trim().lowercase()
                val phrase = newPhrase.text.toString().trim()
                // letters only (a-z): what reaches the word being typed; digits go to the app as typed
                if (keys.isEmpty() || phrase.isEmpty() || keys.any { it !in 'a'..'z' } || keys.length > 32 || phrase.length > 500) return@setOnClickListener
                Prefs.setShortcuts(prefs, Prefs.shortcuts(prefs).filter { it.first != keys } + (keys to phrase))
                newKeys.text.clear(); newPhrase.text.clear(); showShortcuts()
            }
        })

        // Backup: the settings (not what was typed or learned as words) to a file and back.
        header(tr("설정 백업", "設定のバックアップ", "Settings backup"))
        note(tr("설정, 키보드 모양, 단축어, 터치 기록만 저장합니다. 입력한 글과 학습한 단어는 넣지 않습니다. 기기의 Google 백업도 같은 범위만 백업합니다.",
            "設定、キーボードの形、ショートカット、タッチの記録だけを保存します。入力した文章や学習した単語は入れません。端末の Google バックアップも同じ範囲だけです。",
            "Saves the settings, keyboard shape, shortcuts and touch learning only, never what you typed or the words learned. The phone's Google backup takes the same."))
        list.addView(Button(this).apply {
            text = tr("파일로 내보내기", "ファイルに書き出す", "Export to a file")
            setOnClickListener {
                startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                    .setType("application/json").putExtra(Intent.EXTRA_TITLE, "koenja-settings.json"), EXPORT)
            }
        })
        list.addView(Button(this).apply {
            text = tr("파일에서 복원", "ファイルから復元", "Restore from a file")
            setOnClickListener {
                startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"), IMPORT)
            }
        })

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

    @Deprecated("startActivityForResult: no AndroidX here")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data ?: return
        if (resultCode != RESULT_OK) return
        val prefs = Prefs.of(this)
        val done = runCatching {
            when (requestCode) {
                EXPORT -> contentResolver.openOutputStream(uri)!!.use { it.write(SettingsBackup.export(prefs).toByteArray()) }
                IMPORT -> SettingsBackup.restore(prefs, contentResolver.openInputStream(uri)!!.use {
                    val out = java.io.ByteArrayOutputStream()
                    val buffer = ByteArray(8192)
                    while (true) {
                        val n = it.read(buffer)
                        if (n < 0) break
                        out.write(buffer, 0, n)
                        require(out.size() <= SettingsBackup.MAX_BYTES) { "too big" }
                    }
                    out.toByteArray().decodeToString()
                })
            }
        }.isSuccess
        android.widget.Toast.makeText(this, if (done) tr("완료했습니다.", "完了しました。", "Done.") else tr("읽거나 쓰지 못했습니다.", "読み書きできませんでした。", "Couldn't read or write the file."), android.widget.Toast.LENGTH_SHORT).show()
        if (done && requestCode == IMPORT) recreate()
    }

    private val guide by lazy { LinearLayout(this).apply { orientation = LinearLayout.VERTICAL } }

    override fun onResume() {
        super.onResume()
        // Once it's on, picking it is the button's one step (apps can't read which keyboard is in use).
        val on = getSystemService(InputMethodManager::class.java).enabledInputMethodList.any { it.packageName == packageName }
        guide.visibility = if (on) View.GONE else View.VISIBLE
    }
}
